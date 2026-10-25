/*
	File:		host/HostQuickSheet.cpp

	Contains:	The quick sheet and the lock screen (HostQuickSheet.h).
*/

#include <stdio.h>
#include "HostQuickSheet.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "Compiler.h"
#include "NativeFunctions.h"
#include "NewtonExceptions.h"
#include "hal/host/HostPower.h"

extern const char gHostQuickSheetSource[];		// HostQuickSheet.ns (HostQuickSheetSource.cpp, generated)

static void	(*gLockHook)(bool on) = nil;
static bool	gInstalled = false;
static long	gAutoLockSeconds = 0;				// (0: never)


void
HostQuickSheetSetLockHook(void (*locked)(bool on))
{
	gLockHook = locked;
}


// HostScreenLocked(on): the lock screen opened (on true) or closed - the host told
static Ref
FHostScreenLocked(RefArg /*rcvr*/, RefArg on)
{
	fprintf(stderr, "[host] the screen %s\n", NOTNIL(on) ? "locked" : "unlocked");
	if (gLockHook != nil)
		gLockHook(NOTNIL(on));
	return TRUEREF;
}


void
HostQuickSheetSetAutoLock(long seconds)
{
	gAutoLockSeconds = seconds > 0 ? seconds : 0;
	// (the lock screen's watch, every 30 seconds while it was off, begun
	//  again so that the new time counts from now)
	if (!gInstalled)
		return;
	newton_try
	{
		RefVar sheet(GetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "QuickSheet:host"))));
		if (NOTNIL(sheet))
			NSSend(sheet, RefVar(Intern((char*) "Rewatch")));
	}
	newton_catch_all
	{
		fprintf(stderr, "[host] the auto-lock's watch was not begun again: %s\n", CurrentException()->name);
	}
	end_try;
}


long
HostQuickSheetAutoLock(void)
{
	return gAutoLockSeconds;
}


// HostUserActivity(): the count of the user's pen samples and keys (only its
// changes mean anything: an integer that wraps)
static Ref
FHostUserActivity(RefArg /*rcvr*/)
{
	return MAKEINT(HostUserActivity() & 0x0FFFFFFF);
}


// HostAutoLockSeconds(): how long the user may leave the machine alone
// before the screen locks (0: never)
static Ref
FHostAutoLockSeconds(RefArg /*rcvr*/)
{
	return MAKEINT(gAutoLockSeconds);
}


// HostSetAutoLock(seconds): ... set (the Host panel's choice does it; a test
// sets seconds of its own) ==> true
static Ref
FHostSetAutoLock(RefArg /*rcvr*/, RefArg seconds)
{
	HostQuickSheetSetAutoLock(ISINT(seconds) ? RINT(seconds) : 0);
	return TRUEREF;
}


void
HostInstallQuickSheet(void)
{
	RefVar functions(gFunctionFrame);
	SetFrameSlot(functions, RefVar(Intern((char*) "HostScreenLocked")), RefVar(MakeCFunction((void*) FHostScreenLocked, 1, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "HostUserActivity")), RefVar(MakeCFunction((void*) FHostUserActivity, 0, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "HostAutoLockSeconds")), RefVar(MakeCFunction((void*) FHostAutoLockSeconds, 0, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "HostSetAutoLock")), RefVar(MakeCFunction((void*) FHostSetAutoLock, 1, nil)));
	newton_try
	{
		RefVar fn(ParseString(RefVar(MakeString(gHostQuickSheetSource))));
		RefVar sheet(InterpretBlock(fn, RefVar()));
		SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "QuickSheet:host")), sheet);
		// (the ScreenLock functions defined, the auto-lock's watch begun)
		NSSend(sheet, RefVar(Intern((char*) "Start")));
		gInstalled = true;
	}
	newton_catch_all
	{
		fprintf(stderr, "[host] the quick sheet was not installed: %s\n", CurrentException()->name);
	}
	end_try;
}


bool
HostQuickSheetInstalled(void)
{
	return gInstalled;
}


void
HostQuickSheetPulled(void)
{
	if (!gInstalled)
		return;
	newton_try
	{
		RefVar sheet(GetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "QuickSheet:host"))));
		if (NOTNIL(sheet))
			NSSend(sheet, RefVar(Intern((char*) "Pulled")));
	}
	newton_catch_all
	{
		fprintf(stderr, "[host] the quick sheet did not open: %s\n", CurrentException()->name);
	}
	end_try;
}
