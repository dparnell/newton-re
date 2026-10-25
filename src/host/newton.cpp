/*
	File:		host/newton.cpp

	Contains:	newton: the Newton OS run on the host - the kernel booted
				(OsBoot), the loader's 'main' task running the NewtonScript
				world (newt/NewtWorld.h) over a window on the host display,
				the mouse as the pen and the keys as the keyboard (the
				window on the host display, host/HostWindow.h; the keyboard
				tool's stand-in, host/HostKeyboard.h, runs as a task from
				the kernel services hook).  The object file built from the
				committed ROM source tree (romsrc/) gives the world its
				objects (the fonts, the prototypes, the built-in
				applications); a NewtonScript file can be run once the world
				is up, as the ROM's boot runs its bootTestScript.

	newton [--rom image | --objects file] [--heap bytes] [--display WxH[xdepth]] [--colour] [--scale n]
	       [--script file.ns] [--headless seconds] [--store file|none] [--erase]
	       [--flash-size mb] [--flat-flash] [--kernel-heap mb] [--task-stack kb]
	       [--bake-objects image [--bake-base address]]
	       [--package file.pkg]... [--card file] [--microphone-tone hz] [--tcp-echo port]
	       [--serial-port port|none] [--ir-peer listen:port|host:port] [--print-dir dir]
	       [--ipp-printer uri]

	--ipp-printer offers a printer on the network to the Print slip, as
	"IPP printer (PostScript)" and "IPP printer (HP PCL)": the ROM's
	PostScript and HP PCL drivers, their jobs sent by IPP to the URI
	(ipp://host:631/ipp/print; print/host/HostIPP.h).  NEWTON_IPP_PRINTER
	in the environment does the same.

	--print-dir is where the host's printer (print/host/HostPrinter.h:
	"Host printer (PNG files)" in the Print slip's Choose Other Printer)
	writes each page it prints, as print-001.png, print-002.png, ...
	(default: the working directory).

	By default newton boots on the reconstructed data: the object file the
	build makes from the committed ROM source tree (<build>/romsrc-objects.bin,
	romsrc/README.md; docs/rom-free/README.md), with no ROM image anywhere.
	It is looked for in NEWTON_OBJECTS, beside the program, in the directory
	above it (in a macOS application, Contents/Resources) and at the build's own path (host/HostObjectsFile.h); when there
	is none newton says how to build it and stops - it does not go looking
	for a ROM image.  --objects names another object file (an edited tree's).
	--rom boots the original ROM image instead (build/MP2x00US/rom.bin, or
	the AIF image in DebugRom/), which is how the reconstructed data is
	checked against the ROM (ctest host.NewtonNoROMSameScreen).

	--colour makes the screen an eight-bit colour one (qd/Colour.h,
	docs/qd/colour.md): its pixel values index a palette, and the colours
	applications give (PackRGB patterns, text colours, colour bitmaps) are
	drawn in colour where the ROM draws them in grays.  An extension: the
	MessagePad has no colour screen.  --display's depth is taken as 8.

	--limit ends a run with the window open after the seconds, as
	--headless ends one without; --window-pen hands a script's taps
	(HostTabletTap, and the calibration screen's targets tapped for it) to
	the window as the mouse's clicks are, so that a test goes the way a
	person's clicks go (host/HostWindow.h's HostWindowPostPen; ctest
	host.NewtonWindowPen).

	--headless runs without a window for the seconds (a snapshot of the
	display can be written by the script: ScreenSnapshot), or until the
	script calls HostQuit() - so the seconds are a limit, and a test that
	waits on what it is waiting for rather than for a fixed time ends as
	soon as it is done.  The sound it plays is kept
	(hal/host/HostSoundDriver.h) and counted at the end;
	with --microphone-tone the microphone hears a sine of that frequency,
	and the end says how much of what was played was that tone.

	--store names the file the internal store is kept in between runs:
	the internal flash itself (hal/host/HostFlash.h), with the ROM's flash
	store on it (stores/flash/: TNewInternalFlash, TFlashStore, TMuxStore,
	made by InitPSSManager).  A new file is a sparse image, which holds
	only what has been written and grows with it; --flash-size says how
	big the flash it stands for is, in megabytes (4, 8, 16, 32, 64 - the
	default, the most the ROM's code takes unchanged - or 128;
	docs/stores/README.md, "Bigger flash"), and --flat-flash makes it a
	flat file of that many bytes instead (4 MB unless --flash-size says
	otherwise; at 4 or 8 MB, Einstein's own).  Without --store a windowed
	run keeps it in the user's own directory (host/HostAppDefaults.h's
	HostDefaultStoreFile: %LOCALAPPDATA%\Newton\Internal.store on Windows,
	~/.local/newton/Internal.store on Linux and macOS) and a headless one
	keeps it in memory; --store none keeps a windowed one's in memory too.

	--kernel-heap (megabytes; 32 unless given) and --task-stack (kilobytes,
	each task thread's stack; 8 MB unless given, Unix hosts only - Windows
	threads keep the system's size) size what the host gives the OS, with
	--heap (the NewtonScript heap, inside the kernel heap) and --flash-size,
	to a small device's budget (docs/host-tdeck-pro.md, "A device-sized
	run"); NEWTON_MEMORY_REPORT in the environment prints at exit how much
	of the kernel heap was used (host/HostHeapCheck.h).

	--bake-objects bakes the object file (--objects, or the default) into
	an image and stops: the ROM's objects as newton would import them, laid
	out in one region at --bake-base (an address; 0x20000000000 on a 64-bit
	host and 0x50000000 on a 32-bit one unless given) and written out as
	they lie (frames/ROMImport.h).  --objects takes such an image as it
	takes an object file, and maps it there read-only - nothing read,
	converted or held - as a small device maps it from flash
	(docs/host-tdeck-pro.md, "The baked object image").  An image is a
	host build's own layout: bake it with the build that maps it.  A file that is there keeps its size and
	format; tools/stores/flashimage.py converts between the two.  Set the machine up once and every boot after that
	comes up on the Notepad.  --erase throws that
	file away first and starts again at the Setup assistant, which is what
	holding the power switch down through a reset does on the machine.

	--card puts a memory card in socket 0: a file in Einstein's
	TLinearCard layout (hal/host/HostCard.h), a blank 4 MB flash card
	made when there is none.  The card server finds it and the machine
	mounts its store (a blank one is formatted when the user says so); a
	script can take it out and put it back (HostRemoveCard,
	HostInsertCard - src/host/demo/card.ns).

	--tcp-echo runs a TCP echo server on 127.0.0.1 at the port, for a
	script's endpoint to talk to (comms/host/HostEchoServer.h,
	src/host/demo/echo.ns); a port of 0 takes a free one, which the
	script asks for with HostEchoPort(), and which is printed as "[host]
	echo port N" - how the tests keep two runs at once apart.  HostGetEnv(name) answers a host environment variable
	(tools/host/httpserve.py's NEWTON_HTTP_PORT).

	--serial-port is the TCP port the Newton's external serial port listens
	on (hal/host/HostSerialChip.h): 3679 unless given, as Einstein's, so a
	desktop program that docks with an emulated Newton over TCP (NCX, or
	UnixNPI with its serial port given as a TCP one) connects to
	localhost:3679 and speaks the serial dock protocol (MNP, then 'dock')
	over it; 0 picks a free port, none does without.  Once listening it
	prints "[host] serial port N" (tools/dock/dock.py waits for that line).
	The docker is the Connection application's (comms/Docker.h), started
	by its Connect button or by autodock.

	--cell-port is a TCP port on 127.0.0.1 a cellular module connects to
	(hal/host/HostCellular.h: the engine that drives a SIMCom module in AT
	commands, as the T-Deck Pro's 4G variant carries); 0 picks a free one.
	Once listening it prints "[host] cell port N";
	tools/modem/fakecell.py is the module (docs/host-tdeck-pro-phone.md).

	--gps gives the machine a GPS receiver (hal/host/HostGPS.h), plugged
	into a serial port as GPS software expects one - NMEA 0183 sentences
	each second (GPS Map, GPS Monitor, GPS View in fixtures/packages/apps).
	The port is serial port 2 ('mdem': GPS Map's "Serial Port 2", GPS
	Monitor's "Internal Modem"), so the external one stays free for
	docking; --gps-port 1 plugs it into the external port instead, for
	software that never asks which port (GPS View).  The source:
	sim[:lat,lon[,knots]] a pretend one going
	round a circle, fixed:lat,lon[,alt] one standing still, nmea:file a
	log replayed, tcp:host:port or gpsd[:host[:port]] a receiver on the
	network, serial:device[@baud] one plugged into the host (COM3,
	/dev/ttyUSB0).  --gps-kind tripmate makes it a DeLorme Tripmate (the
	ASTRAL handshake first: GPS View).  Plugged into the external port it
	is the port's cable and nobody docks over the network.  The Host
	preferences panel's "GPS receiver on" and "GPS receiver" move it,
	unplug it or change its kind (docs/comms/gps.md).

	--mesh-air lan[:port] makes this newton a LoRa mesh node
	(hal/host/HostMesh.h: a MeshCore chat node, as the T-Deck Pro is)
	whose radio is a pretend one: packets are datagrams to the UDP
	multicast group 239.255.78.121 on that port (3685 by default), which
	other newtons and tools/mesh's meshpeer on the same air hear.  The
	node's identity and contacts are kept beside the store
	("<store>.mesh-identity", ...; --erase forgets them too); --mesh-name
	is its name until the Mesh application gives it another
	(docs/host-tdeck-pro-mesh.md).

	The lock screen (host/HostQuickSheet.h) is installed on every run:
	applications leave notifications on it while it is locked
	(docs/host-lock-screen.md), and the Host preferences panel's
	"Auto-lock" locks it once the pen and the keys have been left alone
	so long.  The T-Deck Pro's quick sheet, which locks it by hand, is
	opened on the board by a finger pulled down from the screen's top
	edge; here only a script opens it (GetGlobalVar('|QuickSheet:host|):
	Pulled(), demo/quicksheet.ns).  --quick-sheet, which installed them
	for testing before they were always there, is still taken and does
	nothing.

	--ir-peer puts the Newton's built-in IR port on a TCP connection to
	another newton (hal/host/HostIRChip.h), so the two can beam to each
	other: one is given listen:PORT (0 picks a free port) and the other
	HOST:PORT.  Once set up it prints "[host] IR port N".  Without it the
	IR port is still there, with nobody in front of it: Beam looks for a
	receiver and finds none, as a MessagePad alone does.

	--ir-lan puts it on the LAN medium instead: a UDP multicast group
	(239.255.78.119, PORT or 3681) that every newton given --ir-lan on the
	network joins, so any of them can beam to any other without knowing
	its address - whoever is listening answers, as with IR.  It joins on
	every IPv4 interface that can multicast; --ir-lan-interface ADDRESS
	keeps it to one (127.0.0.1: this machine only).  It prints
	"[host] IR port N (LAN)".

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
#include "HostWindow.h"
#include "HostPenReplay.h"
#include "HostAudio.h"
#include "UserBoot.h"
#include "UserTasks.h"
#include "os600/kernel/Boot.h"
#include "os600/kernel/host/TaskRuntime.h"
#include "hal/host/Host.h"
#include "HostStores.h"
#include "HostPackages.h"
#include "HostOrientation.h"
#include "HostSettings.h"
#include "HostQuickSheet.h"
#include "HostDeveloper.h"
#include "HostMapping.h"
#include "ROMImport.h"
#include "memory/host/KernelHeap.h"
#include "HostFontProvider.h"
#include "HostWordEngines.h"
#include "HostTablet.h"
#include "HostCard.h"
#include "HostFlash.h"
#include "HostHeapCheck.h"
#include "HostSoundDriver.h"
#include "HostEchoServer.h"
#include "HostCellular.h"
#include "HostGPS.h"
#include "HostMesh.h"
#include "HostSerialChip.h"
#include "HostIRChip.h"
#include "FIQTimer.h"
#include "SerialTool.h"
#include "SharpIRTool.h"
#include "MNP.h"
#include "CommManager.h"
#include "SCPLoader.h"
#include "ModemTool.h"
#include "FaxTool.h"
#include "HostLink.h"
#include "HostCellProvider.h"
#include "HostMeshApp.h"
#include "print/host/HostPrinter.h"
#include "print/host/HostIPP.h"
#include "power/host/HostPowerSwitch.h"
#include "HostAppDefaults.h"
#include "HostObjectsFile.h"
#include "HostRestart.h"
#include "HostCStack.h"
#include "FunctionProfiler.h"
#include "Colour.h"
#include "os600/kernel/host/TaskRuntime.h"
#include "Journal.h"			// InstallJournalHostNatives
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

#ifdef _WIN32
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
__declspec(dllimport) unsigned short __stdcall RtlCaptureStackBackTrace(unsigned long skip, unsigned long count, void** trace, unsigned long* hash);
__declspec(dllimport) void* __stdcall GetCurrentProcess(void);
__declspec(dllimport) int __stdcall GetProcessTimes(void* process, unsigned long long* creation, unsigned long long* exit,
													 unsigned long long* kernel, unsigned long long* user);
__declspec(dllimport) void* __stdcall GetCurrentThread(void);
__declspec(dllimport) int __stdcall GetThreadTimes(void* thread, unsigned long long* creation, unsigned long long* exit,
													unsigned long long* kernel, unsigned long long* user);
}
#else
// The same two things from a Unix host: the processor time the program has
// used, and where the image was loaded (dladdr answers the base of the
// object a symbol is in).  These headers bring in no names of the
// Newton's, so they need no keeping apart.
#include <sys/resource.h>
#include <time.h>
#include <dlfcn.h>
#ifdef __APPLE__
#include <sys/ucontext.h>		// (<ucontext.h> there wants _XOPEN_SOURCE: only the type is used)
#else
#include <ucontext.h>
#endif
#ifdef __GLIBC__
#include <malloc.h>
#include <pthread.h>		// (not <thread>: libc++'s reaches <locale.h>, which a cross build on a
						// case-insensitive file system finds as src/intl/Locale.h - docs/host-remarkable.md)
#endif
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif
#endif
#include <stdlib.h>
#include <string.h>

#ifndef NEWTON_DEFAULT_OBJECTS
#define NEWTON_DEFAULT_OBJECTS "romsrc-objects.bin"
#endif

static long gScale = 1;
static long gHeadlessSeconds = 0;
static long gSerialPort = kHostSerialPort;	// --serial-port: -1 none
static long gCellPort = -1;					// --cell-port: -1 none
static const char* gMeshAir = nil;			// --mesh-air: "lan[:port]"
static const char* gMeshName = nil;			// --mesh-name
static const char* gGPS = nil;				// --gps: the GPS receiver's source
static const char* gGPSKind = nil;			// --gps-kind: nmea or tripmate
static long gGPSPort = 2;					// --gps-port: 2 serial port 2 ('mdem'), 1 the external one
static const char* gIRPeer = nil;			// --ir-peer, or the LAN medium's "lan[:PORT][@ADDRESS]"
static char gIRLan[80];						// --ir-lan, --ir-lan-interface
static const char* gIRLanPort = nil;
static const char* gIRLanInterface = nil;
static Boolean gIRLanAsked = false;
static long gToneFrequency = 0;			// --microphone-tone: the null microphone's test tone
static Boolean gWindowed = true;
static std::atomic<bool> gScriptQuit(false);	// HostQuit(): the run ended by the script
static const char* gScriptPath = nil;			// --script: HostInclude's names are beside it


static int
Usage(void)
{
	fprintf(stderr, "usage: newton [--objects file | --rom image] [--heap bytes] [--display WxH[xdepth]] [--colour] [--scale n]\n"
					"              [--script file.ns] [--headless seconds] [--limit seconds] [--window-pen]\n"
					"              [--store file|none] [--erase]\n"
					"              [--flash-size mb] [--flat-flash] [--kernel-heap mb] [--task-stack kb]\n"
					"              [--bake-objects image [--bake-base address]]\n"
					"              [--package file.pkg]... [--card file] [--microphone-tone hz] [--tcp-echo port]\n"
					"              [--serial-port port|none] [--ir-peer listen:port|host:port] [--print-dir dir]\n"
					"              [--ir-lan [port]] [--ir-lan-interface address] [--cell-port port]\n"
					"              [--mesh-air lan[:port]] [--mesh-name name]\n"
					"              [--gps sim[:lat,lon[,knots]]|fixed:lat,lon[,alt]|nmea:file|tcp:host:port|gpsd[:host[:port]]|serial:device[@baud]]\n"
					"              [--gps-kind nmea|tripmate] [--gps-port 1|2]\n"
					"              [--ipp-printer uri]\n"
					"By default it boots the object file built from romsrc/ (NEWTON_OBJECTS overrides);\n"
					"--rom boots a ROM image instead.\n"
#if NEWTON_NS64
					"This is newton64, the 64-bit NewtonScript flavour: integers are 62 bits, while every\n"
					"store, package and NSOF format stays 32-bit (docs/frames/64bit.md).\n"
#endif
					);
	return 2;
}


// the world's boot on the host: the ROM image, the object system, the
// display and the toolbox; then the window over the display
static void
NewtonBoot(void)
{
	HostHeapCheckInstall();		// (NEWTON_HEAPCHECK: host/HostHeapCheck.h)
	// the cellular module, when there is one: the engine started before the
	// newt world boots, whose PreMain registers it as a telephone provider
	// when it is there (comms/host/HostCellProvider.h; it waits for the
	// module to connect)
	if (gCellPort >= 0)
	{
		unsigned short port;
		if (HostCellularListen((unsigned short) gCellPort, &port))
		{
			printf("[host] cell port %u\n", (unsigned) port);
			fflush(stdout);
		}
		else
			fprintf(stderr, "[host] no cell port on %ld\n", gCellPort);
	}
	// the mesh node, on its pretend air: started before the newt world
	// boots, as the cellular module is, so the Mesh application is there
	if (gMeshAir != nil && !HostMeshAirStart(gMeshAir, HostGetStoreFile(), gMeshName))
		fprintf(stderr, "[host] no mesh air (%s)\n", gMeshAir);
	// the GPS receiver: its thread started before the newt world boots;
	// plugged into the serial port once that is made (below)
	if (gGPS != nil)
	{
		char why[160];
		if (HostGPSStart(gGPS, why, sizeof(why)))
		{
			if (gGPSKind != nil && strcmp(gGPSKind, "tripmate") == 0)
				HostGPSSetKind(kHostGPSTripmate);
			fprintf(stderr, "[host] GPS receiver: %s\n", gGPS);
		}
		else
			fprintf(stderr, "[host] no GPS receiver: %s\n", why);
	}
	HostBootNewtWorld();
	// DEVIATION: the ROM's boot starts the timers and the serial hardware
	// (InitializeCommHardware); the host does it here, the external port a
	// TCP socket
	NewtonErr timerErr = InitFIQTimer();	// (the IR port is always there)
	if (gSerialPort >= 0)
	{
		NewtonErr err = timerErr;
		if (err == noErr)
			err = HostSerialChipInstall((unsigned short) gSerialPort);
		if (err == noErr)
		{
			printf("[host] serial port %u\n", (unsigned) HostSerialChipPort());
			fflush(stdout);
		}
		else
			fprintf(stderr, "[host] no serial port on %ld (%ld)\n", gSerialPort, (long) err);
	}
	// DEVIATION: the built-in IR is the Voyager chip's; the host's is a TCP
	// connection to another newton or, with no --ir-peer, a port with nobody
	// in front of it - Beam then finds nobody, as a MessagePad alone does
	{
		NewtonErr err = timerErr;
		// (the Host preferences panel switches beaming over the network on
		// and off: the LAN medium newton was given, or "lan", and back to
		// the --ir-peer or nobody)
		HostSettingsSetBeamPeers(gIRLanAsked ? gIRPeer : nil, gIRLanAsked ? nil : gIRPeer);
		HostSettingsSetSerialPort(gSerialPort);
		if (timerErr == noErr)
			HostSettingsPlugGPS(gGPSPort == 1);
		if (err == noErr)
			err = HostIRChipInstall(gIRPeer);
		if (err == noErr)
		{
			if (gIRPeer != nil)
			{
				printf("[host] IR port %u%s\n", (unsigned) HostIRChipPort(HostIRChipInstalled()), gIRLanAsked ? " (LAN)" : "");
				fflush(stdout);
			}
		}
		else
			fprintf(stderr, "[host] no IR port at %s (%ld)\n", gIRPeer != nil ? gIRPeer : "(none)", (long) err);
	}
	THostScreenDriver* display = HostDisplay();
	if (ColourScreen())
		HostWindowSetPalette(ColourPalette());		// (--colour: the display's values are the palette's)
	if (gWindowed && !HostWindowStart(display->Width(), display->Height(), display->Pixels(), "Newton", gScale))
		fprintf(stderr, "newton: no window on this host; running headless\n");
}


// --window-pen: a script's taps (HostTabletTap, the calibration targets'
// taps) given to the window as the mouse's are, rather than put on the
// panel straight away - the path a person's clicks take
static void
WindowPenTap(long x, long y, Boolean down)
{
	long h, v;
	if (!down && HostTabletCalibrationTargetAt(&h, &v) && h == x && v == y)
	{
		// a calibration target let go: it must be on the display the window
		// shows - the target held long enough to darken (inverted), and the
		// top of the screen, where the page under the calibration has its
		// pictures, white
		THostScreenDriver* display = HostDisplay();
		const unsigned char* pixels = display->Pixels();
		long width = display->Width(), height = display->Height();
		long box = 0, top = 0;
		for (long row = v - 9; row < v + 10; row++)
			for (long col = h - 9; col < h + 10; col++)
				if (row >= 0 && row < height && col >= 0 && col < width && pixels[row * width + col] > 128)
					box++;
		for (long row = 0; row < 38 && row < height; row++)
			for (long col = 40; col < width - 40; col++)
				if (pixels[row * width + col] > 128 && (labs(col - h) > 10 || labs(row - v) > 10))
					top++;
		if (box >= 150 && top < 5)
			printf("[host] window pen: calibration target at %ld,%ld shown\n", h, v);
		else
			printf("[host] window pen: FAILED: calibration target at %ld,%ld not on the display (%ld dark in it, %ld at the top)\n", h, v, box, top);
		fflush(stdout);
	}
	HostWindowPostPen(x, y, down ? 0 : 2);
}


// HostPenReplay(path): a pen recorded with NEWTON_RM_PENLOG played back
// through the window's pen (host/HostPenReplay.h); HostPenReplayDone()
static Ref
FHostPenReplay(RefArg /*rcvr*/, RefArg path)
{
	if (!IsString(path))
		ThrowBadTypeWithFrameData(kNSErrNotAString, path);
	char buffer[1024];
	long n = Length(path) / 2 - 1;
	const UniChar* text = (const UniChar*) BinaryData(path);
	long i = 0;
	for (; i < n && i < (long) sizeof(buffer) - 1; i++)
		buffer[i] = (char) text[i];
	buffer[i] = 0;
	return MAKEINT(HostPenReplayStart(buffer));
}

static Ref
FHostPenReplayDone(RefArg /*rcvr*/)
{
	return HostPenReplayDone() ? TRUEREF : NILREF;
}


// HostPenTap(x, y): a tap at a display point, as the pen would make it
// (HostPenReplayDone says when it has been played)
static Ref
FHostPenTap(RefArg /*rcvr*/, RefArg x, RefArg y)
{
	if (!ISINT(x) || !ISINT(y))
		return NILREF;
	return MAKEINT(HostPenTapStart((double) RINT(x), (double) RINT(y)));
}


// HostRebootCount(): how many times the machine has restarted in this run
// (host/HostRestart.h)
static Ref
FHostRebootCount(RefArg /*rcvr*/)
{
	return MAKEINT(HostRebootCount());
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


// HostStoreFile(): the --store file's path, or nil - for a script that
// keeps a file of its own beside the store (a memory card's), so a test
// run in a directory of its own (tools/host/stress.py) keeps it there too
static Ref
FHostStoreFile(RefArg /*rcvr*/)
{
	const char* path = HostGetStoreFile();
	return path == nil ? NILREF : MakeString(path);
}


// HostEchoPort(): the port the --tcp-echo server listens on, or nil - a
// test gives --tcp-echo 0 and the host picks a free one, so two runs at
// once (two build directories, tools/host/stress.py) never meet
static long gEchoPort = 0;

static Ref
FHostEchoPort(RefArg /*rcvr*/)
{
	return gEchoPort == 0 ? NILREF : MAKEINT(gEchoPort);
}


// HostGetEnv(name): the host environment variable, or nil - how a script
// hears what the program that started newton set up for it (the port
// tools/host/httpserve.py serves on, NEWTON_HTTP_PORT)
static Ref
FHostGetEnv(RefArg /*rcvr*/, RefArg name)
{
	if (!IsString(name))
		ThrowBadTypeWithFrameData(kNSErrNotAString, name);
	char key[256];
	long n = Length(name) / 2 - 1;
	const UniChar* text = (const UniChar*) BinaryData(name);
	for (long i = 0; i < n && i < (long) sizeof(key) - 1; i++)
		key[i] = (char) text[i];
	key[n < (long) sizeof(key) - 1 ? n : (long) sizeof(key) - 1] = 0;
	const char* value = getenv(key);
	return value == nil ? NILREF : MakeString(value);
}


// HostSoundBootPlaying(): whether the boot sound is still playing (and
// what plays meanwhile is not counted) - a script that measures its
// sounds waits for it
static Ref
FHostSoundBootPlaying(RefArg /*rcvr*/)
{
	return MAKEBOOLEAN(HostSoundSettingAside());
}


// HostSoundSamples(): how many samples the host's sound driver has been
// given to play so far - a test's way of hearing that something made a
// sound (a click, a slip's show sound)
static Ref
FHostSoundSamples(RefArg /*rcvr*/)
{
	return MAKEINT(HostSoundPlayedCount());
}


// HostCPUTime(): the milliseconds of processor time the program has used,
// in the kernel and out of it - what a benchmark measures the work by,
// the wall clock being taken up as well by the animations' and the
// scripts' own waits (src/host/demo/drawbench.ns)
static Ref
FHostCPUTime(RefArg /*rcvr*/)
{
#ifdef _WIN32
	unsigned long long creation, exited, kernel, user;
	if (!GetProcessTimes(GetCurrentProcess(), &creation, &exited, &kernel, &user))
		return NILREF;
	return MAKEINT((long) ((kernel + user) / 10000));		// (hundreds of nanoseconds)
#else
	struct rusage usage;
	if (getrusage(RUSAGE_SELF, &usage) != 0)
		return NILREF;
	long ms = (long) ((usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1000
					+ (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1000);
	return MAKEINT(ms);
#endif
}


// HostThreadCPUTime(): the same for the thread running the calling task
// alone - the script's own work and the drawing it does, without what the
// host's other threads (the interrupt and timer pollers, the window) use
// meanwhile, which is noise to a benchmark (src/host/demo/scriptbench.ns)
static Ref
FHostThreadCPUTime(RefArg /*rcvr*/)
{
#ifdef _WIN32
	unsigned long long creation, exited, kernel, user;
	if (!GetThreadTimes(GetCurrentThread(), &creation, &exited, &kernel, &user))
		return NILREF;
	return MAKEINT((long) ((kernel + user) / 10000));
#else
	struct timespec now;
	if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) != 0)
		return NILREF;
	return MAKEINT((long) (now.tv_sec * 1000 + now.tv_nsec / 1000000));
#endif
}


// PreMain's host hook: the program's globals (HostQuit among them), the
// host's link for the Newton Internet Enabler (comms/host/HostLink.h: it
// waits for the NIE), and the host's printer (print/host/HostPrinter.h)
static void
NewtonPreMain(void)
{
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostQuit")), RefVar(MakeCFunction((void*) FHostQuit, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostRebootCount")), RefVar(MakeCFunction((void*) FHostRebootCount, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostPenReplay")), RefVar(MakeCFunction((void*) FHostPenReplay, 1, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostPenReplayDone")), RefVar(MakeCFunction((void*) FHostPenReplayDone, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostPenTap")), RefVar(MakeCFunction((void*) FHostPenTap, 2, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostInclude")), RefVar(MakeCFunction((void*) FHostInclude, 1, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostCPUTime")), RefVar(MakeCFunction((void*) FHostCPUTime, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostThreadCPUTime")), RefVar(MakeCFunction((void*) FHostThreadCPUTime, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostStoreFile")), RefVar(MakeCFunction((void*) FHostStoreFile, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostEchoPort")), RefVar(MakeCFunction((void*) FHostEchoPort, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostGetEnv")), RefVar(MakeCFunction((void*) FHostGetEnv, 1, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostSoundSamples")), RefVar(MakeCFunction((void*) FHostSoundSamples, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostSoundBootPlaying")), RefVar(MakeCFunction((void*) FHostSoundBootPlaying, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostEntryHints")), RefVar(MakeCFunction((void*) FHostEntryHints, 1, nil)));
	HostInstallPackageGlobal();
	HostInstallOrientationGlobal();
	HostLinkStart();
	HostCellProviderStart();			// (comms/host/HostCellProvider.h: the cellular module, when there is one, for the Phone transport)
	HostMeshAppStart();					// (comms/host/HostMeshApp.h: the Mesh application, when there is a mesh node)
	HostInstallPrinter();
	HostInstallPowerGlobals();			// (power/host/HostPowerSwitch.h: HostPowerSwitch(), HostWakeAfter(ms), ...)
	HostInstallFonts();					// (the host's fonts, offered when asked for: before the Host panel, which may turn them on)
	HostInstallQuickSheet();			// (host/HostQuickSheet.h: the lock screen and its notifications, the auto-lock, the T-Deck Pro's quick sheet - before the Host panel, which lists the auto-lock)
	HostInstallSettings();				// (the Host preferences panel, after Network Printers)
	HostInstallDeveloper();				// (the Developer preferences panel, after it: the Prefs list's last)
	HostInstallWordEngines();			// (the host's handwriting engines in the Handwriting Recognition slip)
	InstallJournalHostNatives();		// (testing/Journal.h: JournalReplayWords, for the recognisers' replay metrics)
	// the boot sound (TNotebook::InitToolbox), which may still be playing,
	// kept out of what is counted as played, which is what the script's
	// sounds are measured by
	HostSoundSetAside();
}


// the kernel services hook: the keyboard tool's task (which also ends the
// run when the window closes); headless, a task that ends it after the
// seconds
static void
HeadlessTimer(void)
{
	// (a tenth of a second at a time, the queued packages sent to the
	// world between; a TTimeout is 32 bits of 3.6864 MHz ticks, which is
	// under ten minutes, so a long run could not be slept in one anyway;
	// the end is a time on the clock, not a count of sleeps, which a busy
	// machine wakes from late - a 4-minute run went on for 5 1/2)
	TTime end = GetGlobalTime() + TTime(gHeadlessSeconds, kSeconds);	// (not TimeFromNow: a TTimeout of seconds overflows past 582)
	while (!gScriptQuit.load())
	{
		TTime now = GetGlobalTime();
		if (CompCompare(&now.time, &end.time) >= 0)
			break;
		HostSendQueuedPackages();
		HostSendAskedOrientation();
		Sleep(100 * kMilliseconds);
	}
	NSProfilerFinish();				// (NEWTON_NS_PROFILE's report, written while a task can still read the heap)
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


static void HostCrashedSignal(int signal);

// the stack pointer of the context that fell over (a Unix host's signal
// handler sets it): when the fault is a jump to somewhere outside the
// program - a call through a bad pointer - the unwinder stops there, and
// the return addresses still on the stack are what says who made the call
static void*	gCrashedSP = nil;

static void
HostCrashed(const char* what, unsigned long code, void* where)
{
	static long once = 0;
	if (once++ != 0)
		_exit(139);
	// the address on its own says nothing - the image is loaded wherever
	// the system puts it - so the offset into the image goes with it, which
	// is what tools/host/whichfunction.py takes to name the function
	char* base = HostImageBase();
	fprintf(stderr, "[host] the machine fell over: %s (%#lx) at %p (image + %#lx)\n",
		what, code, where, (unsigned long) ((char*) where - base));
	// and the C stack: on Windows the filter runs on the stack of the
	// thread that fell over, and the walk goes on through the exception
	// dispatcher into the frames that faulted; on a glibc host the signal
	// handler's backtrace() goes through the signal frame the same way (the
	// first few offsets are the dispatcher's or the handler's own)
	{
		void* trace[40];
		int n = HostCaptureCStack(trace, 40, 0);
		if (n > 0)
		{
			fprintf(stderr, "[host] the C stack, as image offsets:");
			for (int i = 0; i < n; i++)
				if ((char*) trace[i] >= base && (char*) trace[i] < base + 0x10000000)
					fprintf(stderr, " %#lx", (unsigned long) ((char*) trace[i] - base));
			fprintf(stderr, "\n");
		}
	}
	if (gCrashedSP != nil)
	{
		// (the words at the stack pointer that point into the program:
		//  return addresses, the innermost first)
		fprintf(stderr, "[host] the words on the stack that point into the program, as image offsets:");
		const uintptr_t* sp = (const uintptr_t*) gCrashedSP;
		for (int i = 0; i < 64; i++)
			if ((char*) sp[i] >= base && (char*) sp[i] < base + 0x10000000 && (char*) sp[i] != (char*) where)
				fprintf(stderr, " %#lx", (unsigned long) ((char*) sp[i] - base));
		fprintf(stderr, "\n");
	}
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


#ifndef _WIN32
// A Unix host's signal with where it happened: the faulting instruction
// out of the interrupted context
static void
HostCrashedSignalAt(int signal, siginfo_t* info, void* context)
{
	void* where = info != nil ? info->si_addr : nil;
#if defined(__x86_64__) && defined(__linux__)
	if (context != nil)
	{
		where = (void*) ((ucontext_t*) context)->uc_mcontext.gregs[REG_RIP];
		gCrashedSP = (void*) ((ucontext_t*) context)->uc_mcontext.gregs[REG_RSP];
	}
#elif defined(__aarch64__) && defined(__linux__)
	if (context != nil)
		where = (void*) ((ucontext_t*) context)->uc_mcontext.pc;
#elif defined(__i386__) && defined(__linux__)
	if (context != nil)
	{
		where = (void*) ((ucontext_t*) context)->uc_mcontext.gregs[REG_EIP];
		gCrashedSP = (void*) ((ucontext_t*) context)->uc_mcontext.gregs[REG_ESP];
	}
#elif defined(__arm__) && defined(__linux__)
	if (context != nil)
	{
		where = (void*) ((ucontext_t*) context)->uc_mcontext.arm_pc;
		gCrashedSP = (void*) ((ucontext_t*) context)->uc_mcontext.arm_sp;
	}
#elif defined(__APPLE__) && defined(__x86_64__)
	if (context != nil)
		where = (void*) ((ucontext_t*) context)->uc_mcontext->__ss.__rip;
#elif defined(__APPLE__) && defined(__aarch64__)
	if (context != nil)
		where = (void*) ((ucontext_t*) context)->uc_mcontext->__ss.__pc;
#else
	(void) context;
#endif
	HostCrashed("a signal", (unsigned long) signal, where);
}


#ifdef __linux__
// The stack sampler's signal (tools/host/stacksample.py and profile.py
// on Linux, which cannot read another process's registers without
// ptrace): a thread sent SIGRTMIN+3 (tgkill) appends one line to the
// sample file - its thread id, the interrupted instruction and then the
// stack's return addresses, each as an offset into the image (- for one
// outside it) - and carries on.  The file is NEWTON_SAMPLE_FILE, else
// /tmp/newton-sample-<pid>.txt.  Only async-signal-safe calls in here.
static char		gSampleFile[512];
static char*	gSampleBase = nil;

static char*
SampleHex(char* out, unsigned long value)
{
	char digits[20];
	int n = 0;
	do
	{
		digits[n++] = "0123456789abcdef"[value & 15];
		value >>= 4;
	} while (value != 0);
	*out++ = '0';
	*out++ = 'x';
	while (n > 0)
		*out++ = digits[--n];
	return out;
}

static char*
SampleAddress(char* out, void* address)
{
	if (gSampleBase != nil && (char*) address >= gSampleBase && (char*) address < gSampleBase + 0x10000000)
		return SampleHex(out, (unsigned long) ((char*) address - gSampleBase));
	*out++ = '-';
	return out;
}

static void
HostSampleSignal(int /*signal*/, siginfo_t* /*info*/, void* context)
{
	int saved = errno;
	void* pc = nil;
#if defined(__x86_64__) && defined(__linux__)
	pc = (void*) ((ucontext_t*) context)->uc_mcontext.gregs[REG_RIP];
#elif defined(__aarch64__) && defined(__linux__)
	pc = (void*) ((ucontext_t*) context)->uc_mcontext.pc;
#else
	(void) context;
#endif
	char line[2048];
	char* out = line;
	out = SampleHex(out, (unsigned long) syscall(SYS_gettid));
	*out++ = ' ';
	out = SampleAddress(out, pc);
	void* trace[64];
	int n = HostCaptureCStack(trace, 64, 1);		// (not this handler)
	for (int i = 0; i < n && out < line + sizeof(line) - 24; i++)
	{
		*out++ = ' ';
		out = SampleAddress(out, trace[i]);
	}
	*out++ = '\n';
	int fd = open(gSampleFile, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd >= 0)
	{
		ssize_t wrote = write(fd, line, (size_t) (out - line));
		(void) wrote;
		close(fd);
	}
	errno = saved;
}

static void
HostInstallSampler(void)
{
	const char* file = getenv("NEWTON_SAMPLE_FILE");
	if (file != nil)
		snprintf(gSampleFile, sizeof(gSampleFile), "%s", file);
	else
		snprintf(gSampleFile, sizeof(gSampleFile), "/tmp/newton-sample-%ld.txt", (long) getpid());
	gSampleBase = HostImageBase();
	void* warm[4];
	HostCaptureCStack(warm, 4, 0);		// (backtrace's first call loads libgcc: not in a handler)
	struct sigaction action;
	memset(&action, 0, sizeof(action));
	action.sa_sigaction = HostSampleSignal;
	action.sa_flags = SA_SIGINFO | SA_RESTART;
	sigemptyset(&action.sa_mask);
	sigaction(SIGRTMIN + 3, &action, nil);
}
#else
// (a macOS host's sampler would take the same line from a signal sent with
// pthread_kill from inside, or the tools would use the host's own
// sample(1) - docs/host-macos.md)
static void
HostInstallSampler(void)
{ }
#endif


#ifdef __GLIBC__
// NEWTON_MALLOC_STATS=<seconds>: glibc's own figures for the C heap every
// that many seconds on stderr - in use (uordblks), free inside the heap
// (fordblks: fragmentation), mapped whole (hblkhd) and the heap's size
// (arena) - so that a growing process can be told apart into a leak (in
// use grows), fragmentation (free grows) or big blocks (tools/host/
// smapswatch.py, docs/host-lp64.md).  A host thread of its own, which
// makes no Newton call.
static void
HostSetUpMalloc(void)
{
	// One malloc arena, not one per thread: the host runs one Newton task at
	// a time, so its twenty-odd task threads never contend for the C heap,
	// but glibc gives each thread that allocates an arena of its own (up to
	// eight a core), and the free memory scattered over them grew the
	// process's resident set for the first quarter of an hour of a soak
	// (docs/host-lp64.md, "Memory over time").  MALLOC_ARENA_MAX in the
	// environment still has the last word.
	if (getenv("MALLOC_ARENA_MAX") == nil)
		mallopt(M_ARENA_MAX, 1);
	const char* every = getenv("NEWTON_MALLOC_STATS");
	if (every == nil || atoi(every) <= 0)
		return;
	static long seconds;
	seconds = atoi(every);
	pthread_t thread;
	if (pthread_create(&thread, nil, [](void*) -> void* {
		for (long t = 0; ; t += seconds)
		{
#if __GLIBC__ > 2 || __GLIBC_MINOR__ >= 33
			struct mallinfo2 m = mallinfo2();
#else
			struct mallinfo m = mallinfo();		// (an older glibc - a cross build for a device: int-sized counts)
#endif
			fprintf(stderr, "[malloc] %lds arena %zu inuse %zu free %zu mmapped %zu (%zu blocks) top %zu\n",
					t, (size_t) m.arena, (size_t) m.uordblks, (size_t) m.fordblks, (size_t) m.hblkhd, (size_t) m.hblks, (size_t) m.keepcost);
			sleep((unsigned) seconds);
		}
		return (void*) nil;
	}, nil) == 0)
		pthread_detach(thread);
}
#else
static void
HostSetUpMalloc(void)
{ }
#endif


static void
HostCatchCrashes(void)
{
	struct sigaction action;
	memset(&action, 0, sizeof(action));
	action.sa_sigaction = HostCrashedSignalAt;
	action.sa_flags = SA_SIGINFO;
	sigemptyset(&action.sa_mask);
	int signals[] = { SIGSEGV, SIGILL, SIGFPE, SIGABRT, SIGBUS };
	for (int s : signals)
		sigaction(s, &action, nil);
	HostInstallSampler();
}
#endif


#ifdef _WIN32
// On Windows a bad access is a structured exception rather than a signal,
// and nothing turns it into one here, so the filter is what actually
// catches the machine falling over - including in the window's own
// thread.  A Unix host raises a signal for it, which the handlers below
// take, so there is nothing to install there.
static long __stdcall
HostCrashedFilter(HostExceptionPointers* info)
{
	HostCrashed("an exception", info->fRecord->fCode, info->fRecord->fAddress);
	return 0;		// (never reached: HostCrashed does not come back)
}
#endif


// The machine: the OS booted and run until it ends, then the sound closed,
// what was played reported and - if the machine reset itself - newton
// started again (==> the exit status).  main runs it through
// HostWindowRunMachine, on the thread the host's window leaves it.
static int		gArgc = 0;
static char**	gArgv = nil;
static Boolean	gLoud = false;			// the loudspeaker is open
static Boolean	gHearing = false;		// and the microphone

static int
RunMachine(void)
{
	OsBoot();
	HostStopEchoServer();
	HostWindowStop();
	if (gHearing)
		HostMicrophoneClose();
	if (gLoud)
		HostAudioClose();
	long played = 0;
	const short* samples = HostSoundCaptured(&played);
	if (HostSoundSetAsideCount() > 0)
		fprintf(stderr, "[host] sound: the boot sound, %ld samples, not counted\n", HostSoundSetAsideCount());
	if (HostSoundPlayedCount() > 0)
		fprintf(stderr, "[host] sound: %ld samples played\n", HostSoundPlayedCount());
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
	// the machine reset itself (Reboot, Restart): booted again
	// (host/HostRestart.h; DEVIATION, a new process for the jump to the
	// reset vector)
	int restarted = HostRestartIfReset(gArgc, gArgv, gScriptQuit.load());
	if (restarted >= 0)
		return restarted;
	return 0;
}


int
main(int argc, char** argv)
{
#if NEWTON_NS64
	fprintf(stderr, "[host] newton64: the 64-bit NewtonScript flavour - 62-bit integers, 32-bit formats (docs/frames/64bit.md)\n");
#endif
	// (Newton.app opened in the Finder: a home of its own, HostAppDefaults.h)
	argv = HostAppDefaults(&argc, argv);
#ifdef _WIN32
	SetUnhandledExceptionFilter(HostCrashedFilter);
#endif
#ifdef _WIN32
	signal(SIGSEGV, HostCrashedSignal);
#else
	HostCatchCrashes();
#endif
	// tracing: stdout (Print, the traces' stacks) unbuffered, so that it
	// stays in order with stderr (the traces' first lines) in one log;
	// NEWTON_UNBUFFERED for a watcher reading the output as it comes
	// (tools/host/soak.py)
	if (getenv("NEWTON_TRACE_MISSING") != nil || getenv("NEWTON_TRACE_EXCEPTIONS") != nil
	 || getenv("NEWTON_UNBUFFERED") != nil)
		setvbuf(stdout, nil, _IONBF, 0);
#ifdef _WIN32
	signal(SIGILL, HostCrashedSignal);
	signal(SIGFPE, HostCrashedSignal);
	signal(SIGABRT, HostCrashedSignal);
#endif
	const char* romImage = nil;				// (nil: the object file, HostDefaultObjectsFile)
	Boolean bootImage = false;
	long heapSize = 0x400000;
	long width = 320, height = 480, depth = 4;
	const char* script = nil;
	// the file the internal store is kept in between runs, and whether
	// to throw it away first - which is the machine's own way back to
	// the Setup assistant (holding the power switch down through a reset
	// asks whether to erase the internal store, and this is that)
	const char* storeFile = nil;
	Boolean storeGiven = false;
	Boolean erase = false;
	// what a store file that is not there yet is made as (HostSetNewFlash):
	// 0 is the default - 64 MB for a sparse image, the most the ROM's flash
	// code takes unchanged (docs/stores/README.md, "Bigger flash"), which
	// costs nothing until it is written; a flat file is all its size on the
	// disk at once, so it stays Einstein's 4 MB unless asked
	ULong flashSize = 0;
	const char* bakeImage = nil;		// --bake-objects
	uintptr_t bakeBase = 0;
	Boolean flatFlash = false;
	for (int i = 1; i < argc; i++)
	{
		if ((strcmp(argv[i], "--rom") == 0 || strcmp(argv[i], "--objects") == 0) && i + 1 < argc)
		{
			// (either is told by its signature: a ROM image, or the object
			// file built from the ROM source tree)
			bootImage = strcmp(argv[i], "--rom") == 0;
			romImage = argv[++i];
		}
		else if (strcmp(argv[i], "--heap") == 0 && i + 1 < argc)
			heapSize = strtol(argv[++i], nil, 0);
		else if (strcmp(argv[i], "--kernel-heap") == 0 && i + 1 < argc)
		{
			long mb = strtol(argv[++i], nil, 0);
			if (mb < 1)
				return Usage();
			HostSetKernelHeapSize((Size) mb << 20);
		}
		else if (strcmp(argv[i], "--bake-objects") == 0 && i + 1 < argc)
			bakeImage = argv[++i];
		else if (strcmp(argv[i], "--bake-base") == 0 && i + 1 < argc)
			bakeBase = (uintptr_t) strtoull(argv[++i], nil, 0);
		else if (strcmp(argv[i], "--task-stack") == 0 && i + 1 < argc)
		{
			long kb = strtol(argv[++i], nil, 0);
			if (kb < 1)
				return Usage();
			HostSetTaskStackSize((size_t) kb << 10);
		}
		else if (strcmp(argv[i], "--display") == 0 && i + 1 < argc)
		{
			char* rest;
			width = strtol(argv[++i], &rest, 0);
			height = *rest == 'x' ? strtol(rest + 1, &rest, 0) : 0;
			depth = *rest == 'x' ? strtol(rest + 1, &rest, 0) : 4;
			if (width <= 0 || height <= 0)
				return Usage();
		}
		else if (strcmp(argv[i], "--colour") == 0 || strcmp(argv[i], "--color") == 0)
			SetColourScreen(true);
		else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc)
			gScale = strtol(argv[++i], nil, 0);
		else if (strcmp(argv[i], "--script") == 0 && i + 1 < argc)
			script = argv[++i];
		else if (strcmp(argv[i], "--print-dir") == 0 && i + 1 < argc)
			HostSetPrintDirectory(argv[++i]);
		else if (strcmp(argv[i], "--ipp-printer") == 0 && i + 1 < argc)
			HostSetIPPPrinter(argv[++i]);
		else if (strcmp(argv[i], "--microphone-tone") == 0 && i + 1 < argc)
			gToneFrequency = strtol(argv[++i], nil, 0);
		else if (strcmp(argv[i], "--tcp-echo") == 0 && i + 1 < argc)
		{
			long port = strtol(argv[++i], nil, 0);
			gEchoPort = HostStartEchoServer((uint16_t) port);
			if (gEchoPort == 0)
				fprintf(stderr, "newton: no echo server on port %ld\n", port);
			else
			{
				// (tools/modem/fakemodem.py --incoming echo waits for it)
				printf("[host] echo port %ld\n", gEchoPort);
				fflush(stdout);
			}
		}
		else if (strcmp(argv[i], "--ir-peer") == 0 && i + 1 < argc)
			gIRPeer = argv[++i];
		else if (strcmp(argv[i], "--ir-lan") == 0)
		{
			gIRLanAsked = true;
			// (the port is optional: a number next, or not)
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				gIRLanPort = argv[++i];
		}
		else if (strcmp(argv[i], "--ir-lan-interface") == 0 && i + 1 < argc)
			gIRLanInterface = argv[++i];
		else if (strcmp(argv[i], "--cell-port") == 0 && i + 1 < argc)
			gCellPort = strtol(argv[++i], nil, 0);
		else if (strcmp(argv[i], "--mesh-air") == 0 && i + 1 < argc)
			gMeshAir = argv[++i];
		else if (strcmp(argv[i], "--quick-sheet") == 0)
			;							// (the quick sheet and the lock screen are always installed now)
		else if (strcmp(argv[i], "--mesh-name") == 0 && i + 1 < argc)
			gMeshName = argv[++i];
		else if (strcmp(argv[i], "--gps") == 0 && i + 1 < argc)
			gGPS = argv[++i];
		else if (strcmp(argv[i], "--gps-kind") == 0 && i + 1 < argc)
			gGPSKind = argv[++i];
		else if (strcmp(argv[i], "--gps-port") == 0 && i + 1 < argc)
			gGPSPort = strtol(argv[++i], nil, 0) == 1 ? 1 : 2;
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
		else if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc)
			HostKeyboardSetTimeLimit((ULong) strtol(argv[++i], nil, 0));
		else if (strcmp(argv[i], "--window-pen") == 0)
			gHostTabletTapHook = WindowPenTap;
		else if (strcmp(argv[i], "--store") == 0 && i + 1 < argc)
		{
			storeFile = argv[++i];
			storeGiven = true;
			if (strcmp(storeFile, "none") == 0)
				storeFile = nil;
		}
		else if (strcmp(argv[i], "--erase") == 0)
			erase = true;
		else if (strcmp(argv[i], "--flash-size") == 0 && i + 1 < argc)
		{
			long mb = strtol(argv[++i], nil, 0);
			if (mb <= 0 || mb > 128 || !HostFlashValidSize((ULong) mb << 20))
			{
				fprintf(stderr, "newton: --flash-size is 4, 8, 16, 32, 64 or 128 (megabytes)\n");
				return 2;
			}
			flashSize = (ULong) mb << 20;
		}
		else if (strcmp(argv[i], "--flat-flash") == 0)
			flatFlash = true;
		else if (strcmp(argv[i], "--package") == 0 && i + 1 < argc)
			HostQueuePackageFile(argv[++i]);
		else if (strcmp(argv[i], "--card") == 0 && i + 1 < argc)
		{
			const char* card = argv[++i];
			FILE* exists = fopen(card, "rb");
			if (exists != nil)
				fclose(exists);
			else if (HostCardCreate(card, 4, "Host card") == noErr)
				fprintf(stderr, "[host] %s: a new 4 MB flash card\n", card);
			if (HostCardInsert(0, card) != noErr)
				fprintf(stderr, "[host] %s is not a card image\n", card);
		}
		else
			return Usage();
	}
	if (gIRLanAsked || gIRLanInterface != nil)
	{
		gIRLanAsked = true;
		snprintf(gIRLan, sizeof(gIRLan), "lan%s%s%s%s", gIRLanPort != nil ? ":" : "", gIRLanPort != nil ? gIRLanPort : "",
				 gIRLanInterface != nil ? "@" : "", gIRLanInterface != nil ? gIRLanInterface : "");
		gIRPeer = gIRLan;
	}
	if (romImage == nil)
	{
		romImage = HostDefaultObjectsFile(argv[0], NEWTON_DEFAULT_OBJECTS);
		if (romImage == nil)
		{
			HostObjectsFileMissing("newton", NEWTON_DEFAULT_OBJECTS);
			return 1;
		}
	}
	else if (bootImage)
		fprintf(stderr, "[host] booting the ROM image %s (--rom), not the reconstructed data\n", romImage);
	FILE* readable = fopen(romImage, "rb");
	if (readable == nil)
	{
		fprintf(stderr, "newton: cannot read %s\n", romImage);
		if (!bootImage)
			HostObjectsFileMissing("newton", NEWTON_DEFAULT_OBJECTS);
		return 1;
	}
	fclose(readable);
	// a baked object image is made and mapped through the host's mapping
	// (host/HostMapping.h)
	SetHostMappers(HostReserveAt, HostMapFileAt);
	if (bakeImage != nil)
	{
		uintptr_t base = bakeBase != 0 ? bakeBase : DefaultBakedObjectsBase();
		NewtonErr err = BakeBuiltObjectsFile(romImage, bakeImage, base, (size_t) 32 << 20);
		if (err != noErr)
		{
			fprintf(stderr, "newton: %s could not be baked into %s at %#llx (%ld)\n",
					romImage, bakeImage, (unsigned long long) base, (long) err);
			return 1;
		}
		fprintf(stderr, "[host] %s baked into %s, to be mapped at %#llx\n", romImage, bakeImage, (unsigned long long) base);
		return 0;
	}
	if (!HostObjectsFileMatches(romImage))
	{
		HostObjectsFileMismatch("newton", romImage, NEWTON_DEFAULT_OBJECTS);
		return 1;
	}
	// a windowed run with no --store keeps the machine in the user's own
	// directory (HostDefaultStoreFile); a headless one - a test, a script -
	// keeps it in memory, as --store none does, so as not to touch it
	if (!storeGiven && gWindowed)
	{
		storeFile = HostDefaultStoreFile();
		if (storeFile != nil)
			fprintf(stderr, "[host] the internal store is %s (--store names another, --store none keeps it in memory)\n", storeFile);
	}
	if (erase && storeFile != nil && remove(storeFile) == 0)
		fprintf(stderr, "[host] %s erased; the machine starts new\n", storeFile);
	if (erase && storeFile != nil)
	{
		// (the mesh node's blobs beside it: HostMeshAir.cpp)
		static const char* const kMeshBlobs[] = { "identity", "settings", "contacts" };
		char path[1100];
		for (const char* blob : kMeshBlobs)
		{
			snprintf(path, sizeof(path), "%s.mesh-%s", storeFile, blob);
			remove(path);
		}
	}
	HostSetStoreFile(storeFile);
	if (flashSize == 0)
		flashSize = flatFlash ? kHostFlashBankSize : (ULong) 64 << 20;
	HostSetNewFlash(flashSize, flatFlash);
	// a machine that stops dead says so rather than sitting there looking
	// idle, and says what script it was running when it stopped
	gHostStallReportHook = ReportTheScript;
	HostWatchdogStart(10);
#ifndef _WIN32
	HostSetUpMalloc();
#endif
	HostUseRealClock(true);
	HostRestartReceive();		// (a restarted newton: the reboot reason, the window's place)
	HostSettingsSetFile(storeFile);			// (the Host panel's settings for the next start: the screen size)
	HostDeveloperSetReportFile(storeFile);	// (the Developer panel's profile written beside the store)
	if (gWindowed)
	{
		HostSettingsReadStartup();
		HostWindowPreferredDisplay(&width, &height);
	}
	HostSettingsNoteDisplay(width, height);
	if (HostSettingsColourAtStart())
		SetColourScreen(true);					// (the Host panel's "Colour screen (next start)", kept beside the store)
	if (ColourScreen())
		depth = 8;								// (--colour: an eight-bit screen, its values a palette's)
	HostConfigureNewtWorld(romImage, heapSize, width, height, depth);
	gNewtBootTestScript = script;
	gScriptPath = script;
	// a script's pen is a test's: the calibration screen's targets (the
	// Setup assistant's, a rotation's) are tapped for it
	// (hal/host/HostTablet.h; HostTabletAutoCalibrate(nil) to tap them itself)
	if (script != nil)
		HostTabletAutoCalibrate(true);
	// the ROM's services, which the comm manager registers as it starts in the
	// newt world, before the host's boot (CommManager.h's CMAddROMServices:
	// they are in the libraries above it)
	CMAddROMServices(RegisterFaxService);
	CMAddROMServices(RegisterModemService);
	CMAddROMServices(RegisterMNPService);
	CMAddROMServices(RegisterSerialCommServices);
	CMAddROMServices(RegisterIRCommServices);
	RegisterSCPLoader();		// (comms/SCPLoader.h: the comm manager starts the docking loader through it)
	gNewtHostBoot = NewtonBoot;
	gNewtHostPreMain = NewtonPreMain;
	NewtInstallUserMain();
	gHostKernelServicesTask = KernelServices;
	// the sound hardware (hal/host/HostSoundDriver.h): with a window, the
	// loudspeaker (HostAudio.h); headless, or with no audio device,
	// the null backend, which keeps what was played
	// and the microphone (the same file's waveIn), when there is one
	static const HostSoundBackend kLoudspeaker = { HostAudioPlay, nil };
	static const HostSoundBackend kLoudspeakerAndMicrophone = { HostAudioPlay, HostMicrophoneRecord };
	gLoud = gWindowed && HostAudioOpen(kHostSoundRate);
	gHearing = gLoud && HostMicrophoneOpen(kHostSoundRate);
	HostInstallSoundDriver(gHearing ? &kLoudspeakerAndMicrophone : gLoud ? &kLoudspeaker : nil);
	// headless, the null microphone can hear a test tone: a minute of it
	static short* tone = nil;
	const long kToneSamples = 60 * kHostSoundRate;
	if (!gHearing && gToneFrequency > 0)
	{
		tone = (short*) malloc(kToneSamples * sizeof(short));
		if (tone != nil)
		{
			for (long i = 0; i < kToneSamples; i++)
				tone[i] = (short) (10000.0 * sin(2 * 3.14159265358979 * gToneFrequency * i / kHostSoundRate));
			HostSoundSetSource(tone, kToneSamples);
		}
	}
	// the machine run where the host's window wants it: on macOS a thread
	// of its own, the main thread being AppKit's (HostWindow.h)
	gArgc = argc;
	gArgv = argv;
	return HostWindowRunMachine(RunMachine, gWindowed);
}
