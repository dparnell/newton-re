/*
	File:		comms/host/HostLink.cpp

	Contains:	HostLinkStart (HostLink.h).

	Host code (no ROM counterpart).
*/

#include "HostLink.h"
#include "Interpreter.h"
#include "Compiler.h"
#include "ObjectHeap.h"
#include "Frames.h"
#include "NewtonExceptions.h"

#include <stdio.h>

extern const char gHostLinkSource[];		// HostLink.ns (HostLinkSource.cpp, generated)


void
HostLinkStart(void)
{
	newton_try
	{
		RefVar fn(ParseString(RefVar(MakeString(gHostLinkSource))));
		RefVar hostLink(InterpretBlock(fn, RefVar()));
		RefVar name(Intern((char*) "HostLink:host"));
		SetFrameSlot(RefVar(gVarFrame), name, hostLink);		// (a global variable, as DefGlobalVar makes one)
		NSSend(hostLink, RefVar(Intern((char*) "Start")));
	}
	newton_catch_all
	{
		fprintf(stderr, "[host] the host's link for the NIE did not start: %s\n", CurrentException()->name);
	}
	end_try;
}
