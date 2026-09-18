/*
	File:		frames/ScriptBoot.cpp

	Contains:	InitScriptGlobals, RunInitScripts (ScriptBoot.h).

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "ScriptBoot.h"
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


// ROM 0x001ef108 InitFormFunctions__FRC6RefVar
// Nothing in this ROM: the form functions the name suggests are all in the
// ROM's own function frame by the time the boot runs, so the call is left
// where it is rather than dropped.
void
InitFormFunctions(RefArg /*functions*/)
{ }


// ROM 0x001f3c40 InitScriptGlobals__Fv
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


// ROM 0x001f3eec RunInitScripts__Fv
// The ROM's boot block: the soups of its soupDef table made on the
// internal store with their initial entries, and then its seven init
// functions (@549: PreSetupUserConfig, StartAutoFaxReceive, StartSniffing,
// StartAutoCallReceive, SetBatteryTypes, CheckSerialNumber,
// ReadPreferences) invoked one after another.  As above, a script that
// throws does not stop the boot - but it does stop the block, and the ROM
// guards neither the soups nor the functions, so one throw costs every
// init function after it.
//
// That is what happens here today: PreSetupUserConfig, the first of them,
// asks the internal store for a soup called "Names" and sends Query to
// what it gets.  The ROM's own boot table makes twelve soups and "Names"
// is not among them - the Names application's soup is made by the Cardfile
// package in the ROM extension, which cannot install while
// LoadHighROMFramesPackages and the part handlers are NOT YET
// RECONSTRUCTED (newt/NewtWorld.cpp's PreMain and MainConstructor).  So
// the other six init functions never run.
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
