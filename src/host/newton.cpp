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
	       [--script file.ns] [--headless seconds]

	--headless runs without a window for the seconds (a snapshot of the
	display can be written by the script: ScreenSnapshot).
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
#include "REPTranslators.h"
#include "Interpreter.h"
#include <stdio.h>
#include <signal.h>
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
	fprintf(stderr, "usage: newton [--rom image] [--heap bytes] [--display WxH[xdepth]] [--scale n] [--script file.ns] [--headless seconds]\n");
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
	Sleep(gHeadlessSeconds * kSeconds);
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
static void
HostCrashed(int signal)
{
	fprintf(stderr, "[host] the machine fell over (signal %d)\n", signal);
	if (gREPout != nil && gInterpreter != nil)
		gREPout->StackTrace(gInterpreter);
	fflush(stderr);
	_exit(139);
}


int
main(int argc, char** argv)
{
	signal(SIGSEGV, HostCrashed);
	signal(SIGILL, HostCrashed);
	signal(SIGFPE, HostCrashed);
	signal(SIGABRT, HostCrashed);
	const char* romImage = NEWTON_DEFAULT_ROM_IMAGE;
	long heapSize = 0x400000;
	long width = 320, height = 480, depth = 4;
	const char* script = nil;
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
		else
			return Usage();
	}
	HostUseRealClock(true);
	HostConfigureNewtWorld(romImage, heapSize, width, height, depth);
	gNewtBootTestScript = script;
	gNewtHostBoot = NewtonBoot;
	NewtInstallUserMain();
	gHostKernelServicesTask = KernelServices;
	OsBoot();
	HostWindowStop();
	return 0;
}
