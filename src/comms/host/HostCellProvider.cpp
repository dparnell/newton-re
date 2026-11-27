/*
	File:		comms/host/HostCellProvider.cpp

	Contains:	HostCellProviderStart (HostCellProvider.h).

	Host code (no ROM counterpart).
*/

#include "HostCellProvider.h"
#include "HostCellular.h"
#include "HostAppIcon.h"
#include "Interpreter.h"
#include "Compiler.h"
#include "ObjectHeap.h"
#include "Frames.h"
#include "NewtonExceptions.h"

#include <stdio.h>

extern const char gHostCellSource[];		// HostCell.ns (HostCellSource.cpp, generated)
extern const char gHostTextSource[];		// HostText.ns (HostTextSource.cpp, generated)
extern const char gHostMessagesSource[];	// HostMessages.ns (HostMessagesSource.cpp, generated)
extern const char gHostPhoneSource[];		// HostPhone.ns (HostPhoneSource.cpp, generated)


// A source evaluated, kept in the global and started
static void
StartSource(const char* source, const char* global, const char* what)
{
	newton_try
	{
		RefVar fn(ParseString(RefVar(MakeString(source))));
		RefVar frame(InterpretBlock(fn, RefVar()));
		SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) global)), frame);
		NSSend(frame, RefVar(Intern((char*) "Start")));
	}
	newton_catch_all
	{
		fprintf(stderr, "[host] %s did not start: %s\n", what, CurrentException()->name);
	}
	end_try;
}


void
HostCellProviderStart(void)
{
	if (!HostCellularStarted())
	{
		// (no module: Messages nowhere to be seen, though the store had it)
		HostRemoveAppIcon("Messages:host");
		// (and the Extras drawer's Calls icon the ROM's Calls again)
		HostRestoreCallsIcon();
		return;
	}
	StartSource(gHostCellSource, "HostCell:host", "the cellular provider");
	StartSource(gHostTextSource, "HostText:host", "the text message transport");
	StartSource(gHostMessagesSource, "HostMessages:host", "the Messages application");
	StartSource(gHostPhoneSource, "HostPhone:host", "the Phone application");
}
