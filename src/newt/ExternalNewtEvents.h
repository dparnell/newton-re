/*
	File:		newt/ExternalNewtEvents.h

	Contains:	How code outside the newt world - another task, a
				package's native code - gets something done in it.

				SendRunScriptEvent sends the world a 'scpt event: a method
				of a root view variable run with a binary of some data
				(HandleRunScriptEvent, NewtWorld.cpp), the answer coming
				back.  A 'xnwt event (TExternalNewtEvent) is handed to a
				TExternalNewtEventHandler - a protocol a package supplies,
				made by the event's type as its capability the first time
				one of that type comes and kept in
				gExternalNewtEventHandlerList (DeleteExternalNewtEventHandler
				takes one out again).  Nothing in the ROM implements the
				protocol; the functions are in the public jump table for
				packages.

	Reconstructed from the MP2x00 US ROM (0x0030b7a4-0x0030bb84,
	0x00385b20-0x00385b68); each function cites its origin.
*/

#ifndef __NEWT_EXTERNALNEWTEVENTS_H
#define __NEWT_EXTERNALNEWTEVENTS_H

#ifndef __AEVENTS_H
#include "AEvents.h"
#endif
#ifndef __PROTOCOLS_H
#include "Protocols.h"
#endif

class TExternalNewtEvent : public TAEvent
{
public:
	ULong			fEvent;				// +0x08  'xnwt
	ULong			fType;				// +0x0c  which handler (its capability)
	long			fError;				// +0x10  set by HandleExternalNewtEvent
	// (what follows is the handler's)
};

PROTOCOL TExternalNewtEventHandler : public TProtocol
{
public:
	static TExternalNewtEventHandler*	New(const char* implementation);	// ROM 0x00385b20 New__25TExternalNewtEventHandlerSFPc
	void			Delete();												// ROM 0x00385b4c Delete__25TExternalNewtEventHandlerFv

	VIRTUAL void	HandleEvent(TExternalNewtEvent* event) ENDVIRTUAL;		// ROM 0x00385b68 HandleEvent__25TExternalNewtEventHandlerFP18TExternalNewtEvent
};

void		HandleExternalNewtEvent(TExternalNewtEvent* event);		// ROM 0x0030b8a0 HandleExternalNewtEvent__FP18TExternalNewtEvent
NewtonErr	DeleteExternalNewtEventHandler(ULong type, TExternalNewtEventHandler* handler, Boolean destroy);	// ROM 0x0030ba08 DeleteExternalNewtEventHandler__FUlP25TExternalNewtEventHandlerUc
NewtonErr	SendRunScriptEvent(const char* variable, const char* method, void* data, long size, long* result);	// ROM 0x0030b7a4 SendRunScriptEvent__FPcN21lPl

#endif	/* __NEWT_EXTERNALNEWTEVENTS_H */
