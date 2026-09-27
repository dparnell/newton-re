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

	newton [--rom image] [--heap bytes] [--display WxH[xdepth]] [--scale n]
	       [--script file.ns] [--headless seconds] [--store file] [--erase]
	       [--package file.pkg]...

	--headless runs without a window for the seconds (a snapshot of the
	display can be written by the script: ScreenSnapshot).

	--store names the file the internal store is kept in between runs,
	which is what the flash is on the machine: set the machine up once and
	every boot after that comes up on the Notepad.  --erase throws that
	file away first and starts again at the Setup assistant, which is what
	holding the power switch down through a reset does on the machine.

	--package installs a package once the machine is up, through the
	package manager as one arriving in memory is installed (as many as
	wanted, in order); a .pkg file dropped onto the window is installed
	the same way (host/HostPackages.h).
*/

#include "NewtWorld.h"
#include "HostViews.h"
#include "HostScreen.h"
#include "HostKeyboard.h"
#include "win32/HostWindow.h"
#include "UserBoot.h"
#include "UserTasks.h"
#include "os600/kernel/Boot.h"
#include "os600/kernel/host/TaskRuntime.h"
#include "hal/host/Host.h"
#include "HostStores.h"
#include "HostPackages.h"
#include "os600/kernel/host/TaskRuntime.h"
#include "REPTranslators.h"
#include "Interpreter.h"
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
}
#include <stdlib.h>
#include <string.h>

#ifndef NEWTON_DEFAULT_ROM_IMAGE
#define NEWTON_DEFAULT_ROM_IMAGE "build/MP2x00US/rom.bin"
#endif

static long gScale = 1;
static long gHeadlessSeconds = 0;
static Boolean gWindowed = true;


static int
Usage(void)
{
	fprintf(stderr, "usage: newton [--rom image] [--heap bytes] [--display WxH[xdepth]] [--scale n]\n"
					"              [--script file.ns] [--headless seconds] [--store file] [--erase]\n"
					"              [--package file.pkg]...\n");
	return 2;
}


// the world's boot on the host: the ROM image, the object system, the
// display and the toolbox; then the window over the display
static void
NewtonBoot(void)
{
	HostBootNewtWorld();
	THostScreenDriver* display = HostDisplay();
	if (gWindowed && !HostWindowStart(display->Width(), display->Height(), display->Pixels(), "Newton", gScale))
		fprintf(stderr, "newton: no window on this host; running headless\n");
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
	for (ULong left = gHeadlessSeconds * 10; left > 0; left--)
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
		if (strcmp(argv[i], "--rom") == 0 && i + 1 < argc)
			romImage = argv[++i];
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
	gNewtHostBoot = NewtonBoot;
	gNewtHostPreMain = HostInstallPackageGlobal;
	NewtInstallUserMain();
	gHostKernelServicesTask = KernelServices;
	OsBoot();
	HostWindowStop();
	return 0;
}
