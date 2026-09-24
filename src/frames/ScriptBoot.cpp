/*
	File:		frames/ScriptBoot.cpp

	Contains:	InitScriptGlobals, RunInitScripts (ScriptBoot.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "ScriptBoot.h"
#include "NativeFunctions.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "NewtonExceptions.h"

#include <stdio.h>

extern const ExceptionName exRootException;


// Both boot blocks swallow an exception, as the ROM does - a boot that
// fails part way is better than none.  The ROM says nothing about it; the
// host says what it swallowed, because a block that gives up half way
// leaves the system quietly short of whatever the rest of it would have
// done, and there is no other sign of it.
static void
ReportSwallowed(const char* block, Exception* exception)
{
	fprintf(stderr, "[boot] %s gave up on %s\n", block, exception->name);
	fflush(stderr);
}


// ROM 0x001eccf0 InitFormFunctions__FRC6RefVar
// Nothing in this ROM: the form functions the name suggests are all in the
// ROM's own function frame by the time the boot runs, so the call is left
// where it is rather than dropped.
void
InitFormFunctions(RefArg /*functions*/)
{ }


// ROM 0x001f1828 InitScriptGlobals__Fv
// The globals frame is rebuilt on the ROM's starter map rather than grown
// slot by slot: a clone of `Rvarsmapstarter` takes whatever gVarFrame
// already holds and then becomes gVarFrame, so that the globals the ROM's
// scripts look up sit in the map the ROM built for them.  Then the class
// hierarchy, the function frame, and the ROM's own boot block.
void
InitScriptGlobals(void)
{
	RefVar vars(Clone(RefVar(Rvarsmapstarter)));
	{
		RefVar existing(gVarFrame);
		TObjectIterator iter(existing);
		for (; !iter.Done(); iter.Next())
			SetFrameSlot(vars, RefVar(iter.Tag()), RefVar(iter.Value()));
	}
	ReplaceObjectRef(gVarFrame, vars);

	gInheritanceFrame = Clone(RefVar(Rinitialinheritanceframe));
	SetFrameSlot(RefVar(gVarFrame), RSSYMclasses, RefVar(gInheritanceFrame));

	InitFormFunctions(RefVar(gFunctionFrame));

	// (the ROM also points slotCacheRefs at Rslotcachetable's slots here; the
	// host keeps the array itself in gSlotCacheTable, set when the first view
	// is built, because its heap moves objects about)

	{
		RefVar funky(Rgfunky);
		TObjectIterator iter(funky);
		for (; !iter.Done(); iter.Next())
			SetFrameSlot(RefVar(gFunctionFrame), RefVar(iter.Tag()), RefVar(iter.Value()));
	}
	SetFrameSlot(RefVar(gVarFrame), RSSYMfunctions, RefVar(gFunctionFrame));

	newton_try
	{
		DoBlock(RefVar(Rbootinitnsglobals), RefVar(NILREF));
	}
	newton_catch(exRootException)
	{
		ReportSwallowed("InitScriptGlobals", CurrentException());
	}
	end_try;
}


// ROM 0x001f1ad4 RunInitScripts__Fv
// The ROM's boot block: the soups of its soupDef table made on the
// internal store with their initial entries, and then its seven init
// functions (@549: PreSetupUserConfig, StartAutoFaxReceive, StartSniffing,
// StartAutoCallReceive, SetBatteryTypes, CheckSerialNumber,
// ReadPreferences) invoked one after another.  As above, a script that
// throws does not stop the boot - but it does stop the block, and the ROM
// guards neither the soups nor the functions, so one throw costs every
// init function after it.
//
// That is what happens here, and it is the ROM's own doing.
// PreSetupUserConfig, the first of them, asks the internal store for a
// soup called "Names" and sends Query to what it gets, without looking to
// see whether it got one.  Nothing the boot does makes a soup of that
// name: the boot table makes twelve (SystemAlarmSoup, InBox, OutBox,
// Packages, Notes, Calendar, Repeat Meetings, Calendar Notes, Repeat
// Notes, To do, To Do List, Directory), and the ROM extension's packages
// do not install until PreMain, which is after this.  "Names" is the
// Cardfile's own soup, and the Cardfile makes it when it is first
// used rather than when it installs.  Both ROMs we have are the same
// here - the MP2100 D and the MP2x00 US boot tables both lack it - so
// this is not a localisation slip but something the machine gets away
// with because the internal store of a Newton that has ever run the
// Names application already has the soup.  On a store that has never
// been written to, which is what the host starts with, it throws.
//
// It costs the six init functions after it: StartAutoFaxReceive,
// StartSniffing, StartAutoCallReceive, SetBatteryTypes, CheckSerialNumber
// and ReadPreferences.  Make a soup called "Names" and all seven run
// through - which is how the diagnosis was checked, not something this
// does: the reconstruction is meant to do what the ROM does.
void
RunInitScripts(void)
{
	newton_try
	{
		DoBlock(RefVar(Rbootruninitscripts), RefVar(NILREF));
	}
	newton_catch(exRootException)
	{
		ReportSwallowed("RunInitScripts", CurrentException());
	}
	end_try;
}


// ROM 0x001f1b5c FRunInitScripts__FRC6RefVar
// RunInitScripts(): the InstallScript of every part that is installed
// run, which is what makes the ROM's soups.
static Ref
FRunInitScripts(RefArg /*rcvr*/)
{
	RunInitScripts();
	return NILREF;
}


void
RegisterScriptBootNatives(void)
{
	RegisterNativeFunction("FRunInitScripts__FRC6RefVar", (void*) FRunInitScripts, 0);
}
