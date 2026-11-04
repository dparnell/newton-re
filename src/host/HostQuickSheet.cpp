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
static bool	gLockSleeps = false;


void
HostQuickSheetSetLockSleeps(bool sleeps)
{
	gLockSleeps = sleeps;
}


// HostLockSleeps(): whether the machine sleeps while the screen is locked
static Ref
FHostLockSleeps(RefArg /*rcvr*/)
{
	return gLockSleeps ? TRUEREF : NILREF;
}


static bool	(*gPowerOff)(void) = nil;


void
HostQuickSheetSetPowerOff(bool (*off)(void))
{
	gPowerOff = off;
}


// HostCanPowerOff(): whether the sheet has a Power off button
static Ref
FHostCanPowerOff(RefArg /*rcvr*/)
{
	return gPowerOff != nil ? TRUEREF : NILREF;
}


// HostPowerOff(): the board switched off; ==> nil if it could not be
static Ref
FHostPowerOff(RefArg /*rcvr*/)
{
	return gPowerOff != nil && gPowerOff() ? TRUEREF : NILREF;
}


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


// HostUserActivity(): the count of the user's pen samples and keys (only its
// changes mean anything: an integer that wraps)
static Ref
FHostUserActivity(RefArg /*rcvr*/)
{
	return MAKEINT(HostUserActivity() & 0x0FFFFFFF);
}


void
HostInstallQuickSheet(void)
{
	RefVar functions(gFunctionFrame);
	SetFrameSlot(functions, RefVar(Intern((char*) "HostScreenLocked")), RefVar(MakeCFunction((void*) FHostScreenLocked, 1, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "HostUserActivity")), RefVar(MakeCFunction((void*) FHostUserActivity, 0, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "HostLockSleeps")), RefVar(MakeCFunction((void*) FHostLockSleeps, 0, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "HostCanPowerOff")), RefVar(MakeCFunction((void*) FHostCanPowerOff, 0, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "HostPowerOff")), RefVar(MakeCFunction((void*) FHostPowerOff, 0, nil)));
	newton_try
	{
		RefVar fn(ParseString(RefVar(MakeString(gHostQuickSheetSource))));
		RefVar sheet(InterpretBlock(fn, RefVar()));
		SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "QuickSheet:host")), sheet);
		// (the ScreenLock functions defined, the power-off function registered)
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
