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

extern const ExceptionName exRootException;


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
	}
	end_try;
}


// ROM 0x001f3eec RunInitScripts__Fv
// The ROM's boot block that asks each installed part to run its
// InstallScript.  As above, a script that throws does not stop the boot.
void
RunInitScripts(void)
{
	newton_try
	{
		DoBlock(RefVar(Rbootruninitscripts), RefVar(NILREF));
	}
	newton_catch(exRootException)
	{
	}
	end_try;
}
