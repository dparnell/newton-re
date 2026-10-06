/*
	File:		host/HostWordEngines.cpp

	Contains:	The host's handwriting engines offered in the Handwriting
				Recognition slip - HostWordEngines.h.
*/

#include <stdio.h>
#include "HostWordEngines.h"
#include "WordEngines.h"
#include "NewtWorld.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "Compiler.h"
#include "NativeFunctions.h"
#include "NewtonExceptions.h"

extern const char gHostWordEnginesSource[];	// HostWordEngines.ns (HostWordEnginesSource.cpp, generated)


void
HostInstallWordEngines(void)
{
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostWordEngines")),
				 RefVar(MakeCFunction((void*) FHostWordEngines, 0, nil)));
	newton_try
	{
		RefVar fn(ParseString(RefVar(MakeString(gHostWordEnginesSource))));
		RefVar slip(InterpretBlock(fn, RefVar()));
		NSSend(slip, RefVar(Intern((char*) "Start")));
	}
	newton_catch_all
	{
		fprintf(stderr, "[host] the handwriting engines were not added to the Handwriting Recognition slip: %s\n",
				CurrentException()->name);
	}
	end_try;
}
