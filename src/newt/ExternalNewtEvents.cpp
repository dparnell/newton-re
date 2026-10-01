/*
	File:		newt/ExternalNewtEvents.cpp

	Contains:	The newt world's events from outside: run a script, and
				hand an event to a package's handler (ExternalNewtEvents.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "ExternalNewtEvents.h"
#include "NewtWorld.h"
#include "List.h"
#include "ItemTester.h"
#include "UserPorts.h"
#include "NewtErrors.h"
#include "OSErrors.h"
#include "NewtonExceptions.h"

#include <string.h>


// an entry of the list: a type and the handler made for it
struct ExternalNewtEventHandlerEntry
{
	ULong						fType;		// +0x00
	TExternalNewtEventHandler*	fHandler;	// +0x04
};

// ROM 0x0c1054b8 gExternalNewtEventHandlerList
static CList*	gExternalNewtEventHandlerList = nil;


/*------------------------------------------------------------------------------
	The list's two searches: by type, and by handler
------------------------------------------------------------------------------*/

class TExternalNewtEventListTypeTester : public CItemTester
{
public:
	void					SetEventType(ULong type);					// ROM 0x0030baf8 SetEventType__32TExternalNewtEventListTypeTesterFUl
	virtual CompareResult	TestItem(const void* item) const;			// ROM 0x0030bb00 TestItem__32TExternalNewtEventListTypeTesterCFPCv

	ULong					fType;		// +0x04
};

class TExternalNewtEventListHandlerTester : public CItemTester
{
public:
	void					SetEventHandler(TExternalNewtEventHandler* handler);	// ROM 0x0030bb18 SetEventHandler__35TExternalNewtEventListHandlerTesterFP25TExternalNewtEventHandler
	virtual CompareResult	TestItem(const void* item) const;			// ROM 0x0030bb6c TestItem__35TExternalNewtEventListHandlerTesterCFPCv

	TExternalNewtEventHandler*	fHandler;	// +0x04
};


// ROM 0x0030baf8 SetEventType__32TExternalNewtEventListTypeTesterFUl
void
TExternalNewtEventListTypeTester::SetEventType(ULong type)
{
	fType = type;
}


// ROM 0x0030bb00 TestItem__32TExternalNewtEventListTypeTesterCFPCv
CompareResult
TExternalNewtEventListTypeTester::TestItem(const void* item) const
{
	return fType == ((const ExternalNewtEventHandlerEntry*) item)->fType ? kItemEqualCriteria : kItemLessThanCriteria;
}


// ROM 0x0030bb18 SetEventHandler__35TExternalNewtEventListHandlerTesterFP25TExternalNewtEventHandler
void
TExternalNewtEventListHandlerTester::SetEventHandler(TExternalNewtEventHandler* handler)
{
	fHandler = handler;
}


// ROM 0x0030bb6c TestItem__35TExternalNewtEventListHandlerTesterCFPCv
CompareResult
TExternalNewtEventListHandlerTester::TestItem(const void* item) const
{
	return ((const ExternalNewtEventHandlerEntry*) item)->fHandler == fHandler ? kItemEqualCriteria : kItemLessThanCriteria;
}


/*------------------------------------------------------------------------------
	The protocol
------------------------------------------------------------------------------*/

// ROM 0x00385b20 New__25TExternalNewtEventHandlerSFPc
TExternalNewtEventHandler*
TExternalNewtEventHandler::New(const char* implementation)
{
	TExternalNewtEventHandler* p = (TExternalNewtEventHandler*) AllocInstanceByName("TExternalNewtEventHandler", implementation);
	return p != nil ? (TExternalNewtEventHandler*) p->GlueNew() : nil;
}


// ROM 0x00385b4c Delete__25TExternalNewtEventHandlerFv
void
TExternalNewtEventHandler::Delete()
{
	GlueDelete();
}


/*------------------------------------------------------------------------------
	The events
------------------------------------------------------------------------------*/

// ROM 0x0030b8a0 HandleExternalNewtEvent__FP18TExternalNewtEvent
// The event handed to the handler for its type: the one already made, or
// one made now - the protocol's implementation with the type (as four
// characters) for its capability - and kept.  The error is
// kError_No_Memory when there is no room for the list or the entry,
// kError_Not_Registered when no implementation answers, and
// kError_Im_Totally_Confused when the handler throws.
void
HandleExternalNewtEvent(TExternalNewtEvent* event)
{
	TExternalNewtEventListTypeTester tester;
	event->fError = 0;
	if (gExternalNewtEventHandlerList == nil
	 && (gExternalNewtEventHandlerList = new CList) == nil)
	{
		event->fError = kError_No_Memory;
		return;
	}
	tester.SetEventType(event->fType);
	ArrayIndex index;
	ExternalNewtEventHandlerEntry* entry = (ExternalNewtEventHandlerEntry*) gExternalNewtEventHandlerList->Search(&tester, index);
	if (entry == nil)
	{
		// the type as a capability: its four characters, the byte after them nought
		char capability[5];
		ULong type = event->fType;
		capability[0] = (char) (type >> 24);
		capability[1] = (char) (type >> 16);
		capability[2] = (char) (type >> 8);
		capability[3] = (char) type;
		capability[4] = 0;
		TExternalNewtEventHandler* handler = (TExternalNewtEventHandler*) NewByName("TExternalNewtEventHandler", nil, capability);
		if (handler == nil)
			event->fError = kError_Not_Registered;
		else
		{
			entry = new ExternalNewtEventHandlerEntry;
			if (entry == nil)
				event->fError = kError_No_Memory;
			else
			{
				entry->fType = event->fType;
				entry->fHandler = handler;
				gExternalNewtEventHandlerList->InsertAt(gExternalNewtEventHandlerList->GetArraySize(), entry);
			}
		}
	}
	if (entry != nil)
	{
		newton_try
		{
			entry->fHandler->HandleEvent(event);
		}
		newton_catch_all
		{
			event->fError = kError_Im_Totally_Confused;
		}
		end_try;
	}
}


// ROM 0x0030ba08 DeleteExternalNewtEventHandler__FUlP25TExternalNewtEventHandlerUc
// The handler taken out of the list - the one named, or the one for the
// type when none is - and, if asked, deleted.  ==> 0, kError_Not_Registered when there
// is none, kError_Im_Totally_Confused when deleting it throws.
NewtonErr
DeleteExternalNewtEventHandler(ULong type, TExternalNewtEventHandler* handler, Boolean destroy)
{
	NewtonErr err = noErr;
	TExternalNewtEventListTypeTester byType;
	TExternalNewtEventListHandlerTester byHandler;
	CItemTester* tester;
	if (handler == nil)
	{
		byType.SetEventType(type);
		tester = &byType;
	}
	else
	{
		byHandler.SetEventHandler(handler);
		tester = &byHandler;
	}
	ArrayIndex index;
	// ROM QUIRK, kept: the list is searched without looking whether it was
	// ever made (nothing calls this before an event has come)
	ExternalNewtEventHandlerEntry* entry = (ExternalNewtEventHandlerEntry*) gExternalNewtEventHandlerList->Search(tester, index);
	if (entry == nil)
		return kError_Not_Registered;
	gExternalNewtEventHandlerList->RemoveElementsAt(index, 1);
	if (destroy)
	{
		newton_try
		{
			entry->fHandler->Delete();
		}
		newton_catch_all
		{
			err = kError_Im_Totally_Confused;
		}
		end_try;
	}
	delete entry;
	return err;
}


// ROM 0x0030b7a4 SendRunScriptEvent__FPcN21lPl
// A 'scpt event sent to the newt world (and waited for): the root view's
// variable sent the method with a binary of the data.  ==> the send's
// error, or the script's; result: the method's answer.
NewtonErr
SendRunScriptEvent(const char* variable, const char* method, void* data, long size, long* result)
{
	TRunScriptEvent event(variable, method);
	TRunScriptEvent reply(variable, method);
	event.fData = data;
	event.fSize = size;
	ULong replySize;
	// (host: the event is its host size, not the ROM's 0x9c bytes)
	NewtonErr err = gNewtPort->SendRPC(&replySize, &event, sizeof(event), &reply, sizeof(reply));
	if (err == noErr && (err = reply.fError) == noErr)
		*result = reply.fResult;
	return err;
}
