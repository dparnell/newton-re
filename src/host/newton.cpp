/*
	File:		host/newton.cpp

	Contains:	newton: the Newton OS run on the host - the kernel booted
				(OsBoot), the loader's 'main' task running the NewtonScript
				world (newt/NewtWorld.h) over a window on the host display,
				the mouse as the pen and the keys as the keyboard (the
				window on Windows, host/win32/HostWindow.h; the keyboard
				tool's stand-in, host/HostKeyboard.h, runs as a task from
				the kernel services hook).  The ROM image gives the world
				its objects (the fonts, the prototypes); a NewtonScript
				file can be run once the world is up, as the ROM's boot
				runs its bootTestScript.

	newton [--rom image | --objects file] [--heap bytes] [--display WxH[xdepth]] [--scale n]
	       [--script file.ns] [--headless seconds] [--store file] [--erase]
	       [--package file.pkg]... [--microphone-tone hz] [--tcp-echo port]
	       [--serial-port port|none] [--ir-peer listen:port|host:port]

	--objects boots on the object file built from the ROM source tree
	(tools/newton-rom/analysis/romsrc.py build -o) with no ROM image: the
	ROM-free track's step 3 (docs/rom-free/README.md).  Without the image
	there is no ROM extension, so none of its packages (the Setup
	assistant among them) is there.

	--headless runs without a window for the seconds (a snapshot of the
	display can be written by the script: ScreenSnapshot), or until the
	script calls HostQuit() - so the seconds are a limit, and a test that
	waits on what it is waiting for rather than for a fixed time ends as
	soon as it is done.  The sound it plays is kept
	(hal/host/HostSoundDriver.h) and counted at the end;
	with --microphone-tone the microphone hears a sine of that frequency,
	and the end says how much of what was played was that tone.

	--store names the file the internal store is kept in between runs:
	the internal flash itself, in Einstein's layout (a 4 MB bank, or two
	in an 8 MB file; hal/host/HostFlash.h), with the ROM's flash store on
	it (stores/flash/: TNewInternalFlash, TFlashStore, TMuxStore, made by
	InitPSSManager).  Set the machine up once and every boot after that
	comes up on the Notepad.  --erase throws that
	file away first and starts again at the Setup assistant, which is what
	holding the power switch down through a reset does on the machine.

	--tcp-echo runs a TCP echo server on 127.0.0.1 at the port, for a
	script's endpoint to talk to (comms/host/HostEchoServer.h,
	src/host/demo/echo.ns).

	--serial-port is the TCP port the Newton's external serial port listens
	on (hal/host/HostSerialChip.h): 3679 unless given, as Einstein's, so a
	desktop program that docks with an emulated Newton over TCP (NCX, or
	UnixNPI with its serial port given as a TCP one) connects to
	localhost:3679 and speaks the serial dock protocol (MNP, then 'dock')
	over it; 0 picks a free port, none does without.  Once listening it
	prints "[host] serial port N" (tools/dock/dock.py waits for that line).
	The docker is the Connection application's (comms/Docker.h), started
	by its Connect button or by autodock.

	--ir-peer puts the Newton's built-in IR port on a TCP connection to
	another newton (hal/host/HostIRChip.h), so the two can beam to each
	other: one is given listen:PORT (0 picks a free port) and the other
	HOST:PORT.  Once set up it prints "[host] IR port N".

	--package installs a package once the machine is up, onto the internal
	store as one arriving from the Newton Connection is (as many as wanted,
	in order), so with --store it is activated again at every boot after;
	a .pkg file dropped onto the window is installed the same way
	(host/HostPackages.h).
*/

#include "NewtWorld.h"
#include "HostViews.h"
#include "HostScreen.h"
#include "HostKeyboard.h"
#include "win32/HostWindow.h"
#include "win32/HostAudio.h"
#include "UserBoot.h"
#include "UserTasks.h"
#include "os600/kernel/Boot.h"
#include "os600/kernel/host/TaskRuntime.h"
#include "hal/host/Host.h"
#include "HostStores.h"
#include "HostPackages.h"
#include "HostHeapCheck.h"
#include "HostSoundDriver.h"
#include "HostEchoServer.h"
#include "HostSerialChip.h"
#include "HostIRChip.h"
#include "FIQTimer.h"
#include "SerialTool.h"
#include "SharpIRTool.h"
#include "MNP.h"
#include "ModemTool.h"
#include "FaxTool.h"
#include "HostLink.h"
#include "os600/kernel/host/TaskRuntime.h"
#include "REPTranslators.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "Compiler.h"
#include "NSErrors.h"
#include <atomic>
#include <math.h>
#include <stdio.h>
#include <signal.h>

// Just enough of Windows to be told the machine fell over; including
// <windows.h> here would bring in its own Polygon and Sleep, which are
// the Newton's names too.
extern "C" {
struct HostExceptionRecord
{
	unsigned long	fCode;
	unsigned long	fFlags;
	void*			fNext;
	void*			fAddress;
};
struct HostExceptionPointers
{
	HostExceptionRecord*	fRecord;
	void*					fContext;
};
typedef long (__stdcall *HostExceptionFilter)(HostExceptionPointers*);
__declspec(dllimport) HostExceptionFilter __stdcall SetUnhandledExceptionFilter(HostExceptionFilter filter);
__declspec(dllimport) void* __stdcall GetModuleHandleA(const char* name);
__declspec(dllimport) void* __stdcall GetCurrentProcess(void);
__declspec(dllimport) int __stdcall GetProcessTimes(void* process, unsigned long long* creation, unsigned long long* exit,
													 unsigned long long* kernel, unsigned long long* user);
}
#include <stdlib.h>
#include <string.h>

#ifndef NEWTON_DEFAULT_ROM_IMAGE
#define NEWTON_DEFAULT_ROM_IMAGE "build/MP2x00US/rom.bin"
#endif

static long gScale = 1;
static long gHeadlessSeconds = 0;
static long gSerialPort = kHostSerialPort;	// --serial-port: -1 none
static const char* gIRPeer = nil;			// --ir-peer
static long gToneFrequency = 0;			// --microphone-tone: the null microphone's test tone
static Boolean gWindowed = true;
static std::atomic<bool> gScriptQuit(false);	// HostQuit(): the run ended by the script
static const char* gScriptPath = nil;			// --script: HostInclude's names are beside it


static int
Usage(void)
{
	fprintf(stderr, "usage: newton [--rom image | --objects file] [--heap bytes] [--display WxH[xdepth]] [--scale n]\n"
					"              [--script file.ns] [--headless seconds] [--store file] [--erase]\n"
					"              [--package file.pkg]... [--microphone-tone hz] [--tcp-echo port]\n"
					"              [--serial-port port|none] [--ir-peer listen:port|host:port]\n");
	return 2;
}


// the world's boot on the host: the ROM image, the object system, the
// display and the toolbox; then the window over the display
static void
NewtonBoot(void)
{
	HostHeapCheckInstall();		// (NEWTON_HEAPCHECK: host/HostHeapCheck.h)
	HostBootNewtWorld();
	// DEVIATION: the ROM's boot starts the timers and the serial hardware
	// (InitializeCommHardware) and its loader registers the serial and MNP
	// services; the host does it here, the external port a TCP socket
	NewtonErr timerErr = noErr;
	if (gSerialPort >= 0 || gIRPeer != nil)
		timerErr = InitFIQTimer();
	if (gSerialPort >= 0)
	{
		NewtonErr err = timerErr;
		if (err == noErr)
			err = HostSerialChipInstall((unsigned short) gSerialPort);
		if (err == noErr)
		{
			RegisterSerialCommServices();
			RegisterMNPService();
			RegisterModemService();
			RegisterFaxService();
			printf("[host] serial port %u\n", (unsigned) HostSerialChipPort());
			fflush(stdout);
		}
		else
			fprintf(stderr, "[host] no serial port on %ld (%ld)\n", gSerialPort, (long) err);
	}
	// DEVIATION: the built-in IR is the Voyager chip's; the host's is a TCP
	// connection to another newton
	if (gIRPeer != nil)
	{
		NewtonErr err = timerErr;
		if (err == noErr)
			err = HostIRChipInstall(gIRPeer);
		if (err == noErr)
		{
			RegisterIRCommServices();
			printf("[host] IR port %u\n", (unsigned) HostIRChipPort(HostIRChipInstalled()));
			fflush(stdout);
		}
		else
			fprintf(stderr, "[host] no IR port at %s (%ld)\n", gIRPeer, (long) err);
	}
	THostScreenDriver* display = HostDisplay();
	if (gWindowed && !HostWindowStart(display->Width(), display->Height(), display->Pixels(), "Newton", gScale))
		fprintf(stderr, "newton: no window on this host; running headless\n");
}


// HostQuit(): the run ended, as closing the window or the headless time
// running out ends it
static Ref
FHostQuit(RefArg /*rcvr*/)
{
	gScriptQuit.store(true);
	if (gWindowed)
		HostKeyboardQuit();
	return NILREF;
}


// HostInclude(name): the NewtonScript file of that name in the --script
// file's own directory run, as the script itself is - which is how the
// demos share src/host/demo/common.ns (the pen and keys, the waits on a
// condition, the Setup assistant walked).  ==> the last form's result.
static Ref
FHostInclude(RefArg /*rcvr*/, RefArg name)
{
	if (!IsString(name))
		ThrowBadTypeWithFrameData(kNSErrNotAString, name);
	char file[512];
	long n = Length(name) / 2 - 1;
	const UniChar* text = (const UniChar*) BinaryData(name);
	char path[1024];
	for (long i = 0; i < n && i < (long) sizeof(file) - 1; i++)
		file[i] = (char) text[i];
	file[n < (long) sizeof(file) - 1 ? n : (long) sizeof(file) - 1] = 0;
	const char* dir = gScriptPath != nil ? gScriptPath : "";
	const char* slash = strrchr(dir, '/');
	const char* back = strrchr(dir, '\\');
	if (back != nil && (slash == nil || back > slash))
		slash = back;
	snprintf(path, sizeof(path), "%.*s%s", slash != nil ? (int) (slash - dir + 1) : 0, dir, file);
	return ParseFile(path);
}


// HostCPUTime(): the milliseconds of processor time the program has used,
// in the kernel and out of it - what a benchmark measures the work by,
// the wall clock being taken up as well by the animations' and the
// scripts' own waits (src/host/demo/drawbench.ns)
static Ref
FHostCPUTime(RefArg /*rcvr*/)
{
	unsigned long long creation, exited, kernel, user;
	if (!GetProcessTimes(GetCurrentProcess(), &creation, &exited, &kernel, &user))
		return NILREF;
	return MAKEINT((long) ((kernel + user) / 10000));		// (hundreds of nanoseconds)
}


// PreMain's host hook: the program's globals (HostQuit among them), and the
// host's link for the Newton Internet Enabler (comms/host/HostLink.h: it
// waits for the NIE)
static void
NewtonPreMain(void)
{
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostQuit")), RefVar(MakeCFunction((void*) FHostQuit, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostInclude")), RefVar(MakeCFunction((void*) FHostInclude, 1, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostCPUTime")), RefVar(MakeCFunction((void*) FHostCPUTime, 0, nil)));
	HostInstallPackageGlobal();
	HostLinkStart();
}


// the kernel services hook: the keyboard tool's task (which also ends the
// run when the window closes); headless, a task that ends it after the
// seconds
static void
HeadlessTimer(void)
{
	// (a tenth of a second at a time, the queued packages sent to the
	// world between; a TTimeout is 32 bits of 3.6864 MHz ticks, which is
	// under ten minutes, so a long run could not be slept in one anyway)
	for (ULong left = gHeadlessSeconds * 10; left > 0 && !gScriptQuit.load(); left--)
	{
		HostSendQueuedPackages();
		Sleep(100 * kMilliseconds);
	}
	HostStopTasks();
}


static void
KernelServices(void)
{
	if (gWindowed)
		HostKeyboardToolTask();
	else
		HeadlessTimer();
}


// A machine that falls over takes its NewtonScript stack with it, and
// that is the part worth seeing: which of the ROM's own scripts was
// running.  The handler is not what a signal handler is supposed to do -
// it prints, and printing is not safe here - but it is a good deal
// better than an exit code, and the process is going down anyway.
// the watchdog's extra line: what the interpreter was in the middle of
static void
ReportTheScript(void)
{
	if (gREPout != nil && gInterpreter != nil)
	{
		fprintf(stderr, "[host]   the NewtonScript stack:\n");
		gREPout->StackTrace(gInterpreter);
	}
}


static void
HostCrashed(const char* what, unsigned long code, void* where)
{
	static long once = 0;
	if (once++ != 0)
		_exit(139);
	// the address on its own says nothing - the image is loaded wherever
	// Windows puts it - so the offset into the image goes with it, which
	// is what tools/host/whichfunction.py takes to name the function
	void* base = GetModuleHandleA(nil);
	fprintf(stderr, "[host] the machine fell over: %s (%#lx) at %p (image + %#lx)\n",
		what, code, where, (unsigned long) ((char*) where - (char*) base));
	if (gREPout != nil && gInterpreter != nil)
		gREPout->StackTrace(gInterpreter);
	fflush(stderr);
	_exit(139);
}


static void
HostCrashedSignal(int signal)
{
	HostCrashed("a signal", (unsigned long) signal, nil);
}


// On Windows a bad access is a structured exception rather than a signal,
// and nothing turns it into one here, so the filter is what actually
// catches the machine falling over - including in the window's own
// thread.
static long __stdcall
HostCrashedFilter(HostExceptionPointers* info)
{
	HostCrashed("an exception", info->fRecord->fCode, info->fRecord->fAddress);
	return 0;		// (never reached: HostCrashed does not come back)
}


int
main(int argc, char** argv)
{
	SetUnhandledExceptionFilter(HostCrashedFilter);
	signal(SIGSEGV, HostCrashedSignal);
	signal(SIGILL, HostCrashedSignal);
	signal(SIGFPE, HostCrashedSignal);
	signal(SIGABRT, HostCrashedSignal);
	const char* romImage = NEWTON_DEFAULT_ROM_IMAGE;
	long heapSize = 0x400000;
	long width = 320, height = 480, depth = 4;
	const char* script = nil;
	// the file the internal store is kept in between runs, and whether
	// to throw it away first - which is the machine's own way back to
	// the Setup assistant (holding the power switch down through a reset
	// asks whether to erase the internal store, and this is that)
	const char* storeFile = nil;
	Boolean erase = false;
	for (int i = 1; i < argc; i++)
	{
		if ((strcmp(argv[i], "--rom") == 0 || strcmp(argv[i], "--objects") == 0) && i + 1 < argc)
			romImage = argv[++i];			// (a ROM image, or the object file built from the ROM source tree)
		else if (strcmp(argv[i], "--heap") == 0 && i + 1 < argc)
			heapSize = strtol(argv[++i], nil, 0);
		else if (strcmp(argv[i], "--display") == 0 && i + 1 < argc)
		{
			char* rest;
			width = strtol(argv[++i], &rest, 0);
			height = *rest == 'x' ? strtol(rest + 1, &rest, 0) : 0;
			depth = *rest == 'x' ? strtol(rest + 1, &rest, 0) : 4;
			if (width <= 0 || height <= 0)
				return Usage();
		}
		else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc)
			gScale = strtol(argv[++i], nil, 0);
		else if (strcmp(argv[i], "--script") == 0 && i + 1 < argc)
			script = argv[++i];
		else if (strcmp(argv[i], "--microphone-tone") == 0 && i + 1 < argc)
			gToneFrequency = strtol(argv[++i], nil, 0);
		else if (strcmp(argv[i], "--tcp-echo") == 0 && i + 1 < argc)
		{
			long port = strtol(argv[++i], nil, 0);
			if (HostStartEchoServer((uint16_t) port) == 0)
				fprintf(stderr, "newton: no echo server on port %ld\n", port);
		}
		else if (strcmp(argv[i], "--ir-peer") == 0 && i + 1 < argc)
			gIRPeer = argv[++i];
		else if (strcmp(argv[i], "--serial-port") == 0 && i + 1 < argc)
		{
			i++;
			gSerialPort = strcmp(argv[i], "none") == 0 ? -1 : strtol(argv[i], nil, 0);
		}
		else if (strcmp(argv[i], "--headless") == 0 && i + 1 < argc)
		{
			gHeadlessSeconds = strtol(argv[++i], nil, 0);
			gWindowed = false;
		}
		else if (strcmp(argv[i], "--store") == 0 && i + 1 < argc)
			storeFile = argv[++i];
		else if (strcmp(argv[i], "--erase") == 0)
			erase = true;
		else if (strcmp(argv[i], "--package") == 0 && i + 1 < argc)
			HostQueuePackageFile(argv[++i]);
		else
			return Usage();
	}
	if (erase && storeFile != nil && remove(storeFile) == 0)
		fprintf(stderr, "[host] %s erased; the machine starts new\n", storeFile);
	HostSetStoreFile(storeFile);
	// a machine that stops dead says so rather than sitting there looking
	// idle, and says what script it was running when it stopped
	gHostStallReportHook = ReportTheScript;
	HostWatchdogStart(10);
	HostUseRealClock(true);
	HostConfigureNewtWorld(romImage, heapSize, width, height, depth);
	gNewtBootTestScript = script;
	gScriptPath = script;
	gNewtHostBoot = NewtonBoot;
	gNewtHostPreMain = NewtonPreMain;
	NewtInstallUserMain();
	gHostKernelServicesTask = KernelServices;
	// the sound hardware (hal/host/HostSoundDriver.h): with a window, the
	// loudspeaker (win32/HostAudio.h); headless, or with no audio device,
	// the null backend, which keeps what was played
	// and the microphone (the same file's waveIn), when there is one
	static const HostSoundBackend kLoudspeaker = { HostAudioPlay, nil };
	static const HostSoundBackend kLoudspeakerAndMicrophone = { HostAudioPlay, HostMicrophoneRecord };
	Boolean loud = gWindowed && HostAudioOpen(kHostSoundRate);
	Boolean hearing = loud && HostMicrophoneOpen(kHostSoundRate);
	HostInstallSoundDriver(hearing ? &kLoudspeakerAndMicrophone : loud ? &kLoudspeaker : nil);
	// headless, the null microphone can hear a test tone: a minute of it
	static short* tone = nil;
	const long kToneSamples = 60 * kHostSoundRate;
	if (!hearing && gToneFrequency > 0)
	{
		tone = (short*) malloc(kToneSamples * sizeof(short));
		if (tone != nil)
		{
			for (long i = 0; i < kToneSamples; i++)
				tone[i] = (short) (10000.0 * sin(2 * 3.14159265358979 * gToneFrequency * i / kHostSoundRate));
			HostSoundSetSource(tone, kToneSamples);
		}
	}
	OsBoot();
	HostStopEchoServer();
	HostWindowStop();
	if (hearing)
		HostMicrophoneClose();
	if (loud)
		HostAudioClose();
	long played = 0;
	const short* samples = HostSoundCaptured(&played);
	if (played > 0)
		fprintf(stderr, "[host] sound: %ld samples played\n", played);
	if (played > 0 && gToneFrequency > 0)
	{
		// how much of what was played is the test tone (Goertzel over the
		// whole of it, against all the energy there)
		double w = 2 * 3.14159265358979 * gToneFrequency / kHostSoundRate, c = 2 * cos(w);
		double s1 = 0, s2 = 0, energy = 0;
		for (long i = 0; i < played; i++)
		{
			double s0 = samples[i] + c * s1 - s2;
			s2 = s1;
			s1 = s0;
			energy += (double) samples[i] * samples[i];
		}
		double power = (s1 * s1 + s2 * s2 - c * s1 * s2) * 2 / played;
		fprintf(stderr, "[host] sound: the %ld Hz test tone makes up %.0f%% of what was played\n",
				gToneFrequency, energy > 0 ? 100 * power / energy : 0.0);
	}
	return 0;
}
