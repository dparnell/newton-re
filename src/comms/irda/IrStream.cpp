/*
	File:		comms/irda/IrStream.cpp

	Contains:	TIrStream (IrStream.h).

	Reconstructed from the MP2x00 US ROM (0x000f8bc4-0x000f8d64); each
	function cites its origin.
*/

#include "IrStream.h"
#include "IrGlue.h"
#include "NewtErrors.h"

#include <stdio.h>
#include <stdlib.h>
#include <typeinfo>


// NEWTON_TRACE_IRDA set in the environment: the stack's events, and the
// link's frames, on stderr (the host's own)
Boolean
IrDATrace(void)
{
	static int trace = -1;
	if (trace < 0)
		trace = getenv("NEWTON_TRACE_IRDA") != nil;
	return trace;
}


// ROM 0x000f8bc4 __ct__9TIrStreamFv
TIrStream::TIrStream()
{
	fGlue = nil;
	fNextEvent = nil;
	fCurrentEvent = nil;
	fPendingEvents = nil;
}


// ROM 0x000f8c0c __dt__9TIrStreamFv
TIrStream::~TIrStream()
{
	if (fPendingEvents != nil)
	{
		delete fPendingEvents;
		fPendingEvents = nil;
	}
}


// ROM 0x000f8c5c Init__9TIrStreamFP7TIrGlue
NewtonErr
TIrStream::Init(TIrGlue* glue)
{
	fGlue = glue;
	fPendingEvents = new CList;
	return fPendingEvents != nil ? noErr : -7000;
}


// ROM 0x000f8c90 EnqueueEvent__9TIrStreamFP8TIrEvent
// Queued (the first straight into fNextEvent, the rest at the front of the
// list, which is taken from its end) and the stream put in the glue's run
// queue.
NewtonErr
TIrStream::EnqueueEvent(TIrEvent* event)
{
	if (fNextEvent == nil)
		fNextEvent = event;
	else
		fPendingEvents->InsertAt(0, event);
	fGlue->NextStateMachine(this);
	return noErr;
}


// ROM 0x000f8cd4 DequeueEvent__9TIrStreamFv
NewtonErr
TIrStream::DequeueEvent(void)
{
	fCurrentEvent = fNextEvent;
	fNextEvent = (TIrEvent*) fPendingEvents->At(fPendingEvents->Count() - 1);
	if (fNextEvent != nil)
		fPendingEvents->RemoveElementsAt(fPendingEvents->Count() - 1, 1);
	return noErr;
}


// ROM 0x000f8d24 ProcessNextEvent__9TIrStreamFv
// Every event queued, each to NextState in turn.  (NEWTON_TRACE_IRDA set
// in the environment prints each on stderr: the host's own.)
NewtonErr
TIrStream::ProcessNextEvent(void)
{
	Boolean trace = IrDATrace();
	for (;;)
	{
		DequeueEvent();
		if (fCurrentEvent == nil)
			return noErr;
		if (trace)
			fprintf(stderr, "[irda %p] %s event 0x%x result %ld\n", (void*) fGlue, typeid(*this).name(),
				fCurrentEvent->fEvent, (long) fCurrentEvent->fResult);
		NextState(fCurrentEvent->fEvent);
	}
}
