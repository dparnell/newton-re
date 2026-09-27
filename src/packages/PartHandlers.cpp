/*
	File:		packages/PartHandlers.cpp

	Contains:	TPartHandler, TPartEventHandler and PackageManagerPortId
				(PartHandlers.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "PartHandlers.h"
#include "PackageEvents.h"
#include "PackageManager.h"
#include "AppWorld.h"
#include "Pipes.h"
#include "UserPorts.h"
#include "UserTasks.h"
#include "NameServer.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"

#include <string.h>
#include <stdio.h>

extern const ExceptionName exPipeException;


// ROM 0x00182174 PackageManagerPortId__Fv
// The manager's port, from the name server ("pckm", a TUPort); 0 when it
// is not there.
TObjectId
PackageManagerPortId()
{
	TUNameServer nameServer;
	char name[8];
	strncpy(name, "pckm", 5);
	ULong portId;
	ULong spec;
	if (nameServer.Lookup(name, (char*) "TUPort", &portId, &spec) != noErr)
		portId = 0;
	return portId;
}


/*------------------------------------------------------------------------------
	T P a r t H a n d l e r
------------------------------------------------------------------------------*/

// ROM 0x00181d4c __ct__12TPartHandlerFv
TPartHandler::TPartHandler()
{
	fEventHandler = nil;
	fType = 'none';
	fInitFailed = true;
	fAsyncMessage = nil;
	fRegisterEvent = nil;
}


// ROM 0x00181da4 __dt__12TPartHandlerFv
TPartHandler::~TPartHandler()
{
	Unregister();
	if (fEventHandler != nil)
		delete fEventHandler;
	if (fAsyncMessage != nil)
		delete fAsyncMessage;
	delete fRegisterEvent;
}


// ROM 0x001821ec Init__12TPartHandlerFUl
// The handler set up for its part type: an event handler in the current
// world for the manager's events, and the type registered with the
// manager (asynchronously: Register does not wait for the answer, which
// Unregister collects).
NewtonErr
TPartHandler::Init(ULong type)
{
	long err = noErr;
	fType = type;
	fEventHandler = new TPartEventHandler(this);
	if (fEventHandler == nil)
		err = MemError();
	fAsyncMessage = new TUAsyncMessage;
	if (fAsyncMessage != nil)
	{
		TAppWorld* world = (TAppWorld*) GetGlobals();
		fRegisterEvent = new TPkRegisterEvent(fType, *world->GetMyPort());
		if (fRegisterEvent != nil
		 && (err = fAsyncMessage->Init(true)) == noErr
		 && (err = fEventHandler->Init(kPackageEventId, kNewtEventClass)) == noErr)
			err = Register();
	}
	if (err == noErr)
		fInitFailed = false;
	return err;
}


// ROM 0x001822b0 Register__12TPartHandlerFv
// The register event sent to the manager without waiting for the answer;
// then a moment's sleep, which lets the manager take it.
NewtonErr
TPartHandler::Register()
{
	TUPort port(PackageManagerPortId());
	long err = port.SendRPC(fAsyncMessage, fRegisterEvent, sizeof(TPkRegisterEvent),
							fRegisterEvent, sizeof(TPkRegisterEvent), kNoTimeout, nil, 1);
	Sleep(10 * kMilliseconds);
	return err;
}


// ROM 0x00182350 Unregister__12TPartHandlerFv
// The type taken out of the manager's registry - once the registration
// has been answered, and only when it was taken.  ==> kError_No_Memory
// for a handler that never had its async message.
NewtonErr
TPartHandler::Unregister()
{
	TPkUnregisterEvent event(fType);
	TUPort port(PackageManagerPortId());
	if (fAsyncMessage == nil)
		return kError_No_Memory;
	long err = fAsyncMessage->BlockTillDone(nil, nil, nil, nil);
	if (err == noErr && (err = fRegisterEvent->fEventError) == noErr)
	{
		ULong replySize;
		err = port.SendRPC(&replySize, &event, sizeof(event), &event, sizeof(event));
		if (err == noErr)
			err = event.fEventError;
	}
	return err;
}


// ROM 0x00181e10 GetSourcePtr__12TPartHandlerFv
// The part's data, for a memory source; nil for a stream.
Ptr
TPartHandler::GetSourcePtr()
{
	if (!IsMemory(fSourceType))
		return nil;
	return (Ptr) fPartInfo->data;
}


// ROM 0x00181e38 Copy__12TPartHandlerFPv
// The part's bytes copied to data: straight from memory (a throw is
// kError_Unexpected_End_Of_Pkg_Part), or through Expand for a stream.
// NOT YET RECONSTRUCTED: the stream (a CShadowRingBuffer and a CPartPipe
// over the sender's shared buffer, handed to Expand): the host answers
// kError_Call_Not_Implemented.
NewtonErr
TPartHandler::Copy(void* data)
{
	NewtonErr err = noErr;
	if (!IsMemory(fSourceType))
		return kError_Call_Not_Implemented;
	newton_try
	{
		BlockMove((void*) fSource.stream.bufferId, data, fPartInfo->size);
	}
	newton_catch_all
	{
		err = kError_Unexpected_End_Of_Pkg_Part;
	}
	end_try;
	return err;
}


// ROM 0x00181f70 Expand__12TPartHandlerFPvP5CPipeP8PartInfo
// The default expansion: the part's bytes read from the pipe as they are.
// ==> a pipe exception's error; any other exception is passed on.
NewtonErr
TPartHandler::Expand(void* data, CPipe* pipe, PartInfo* info)
{
	NewtonErr err = noErr;
	newton_try
	{
		long count = info->size;
		Boolean eof = false;
		pipe->ReadChunk(data, count, eof);
	}
	newton_catch(exPipeException)
	{
		err = (NewtonErr) (Long) CurrentException()->data;
	}
	end_try;
	return err;
}


// ROM 0x00182018 GetBackupInfo__12TPartHandlerFRC6PartIdUllP8PartInfoT2PUc
// (by default a part is not backed up)
NewtonErr
TPartHandler::GetBackupInfo(const PartId& /*partId*/, PartType /*partType*/, RemoveObjPtr /*removePtr*/, PartInfo* partInfo,
							ULong /*lastBackupDate*/, Boolean* needsBackup)
{
	*needsBackup = false;
	partInfo->sizeInMemory = 0;
	partInfo->size = 0;
	partInfo->infoSize = 0;
	partInfo->compressed = false;
	return noErr;
}


// ROM 0x0018203c Backup__12TPartHandlerFRC6PartIdlP5CPipe
NewtonErr
TPartHandler::Backup(const PartId& /*partId*/, RemoveObjPtr /*removePtr*/, CPipe* /*pipe*/)
{
	return noErr;
}


// ROM 0x00182440 Install__12TPartHandlerFP19TPkPartInstallEvent
// A part install event taken: the part's source noted, the handler's
// Install called (a throw becomes its error), and the manager answered -
// unless Install has done so already (ReplyImmed).
void
TPartHandler::Install(TPkPartInstallEvent* installEvent)
{
	fAccept = true;
	fSourceType = installEvent->fSourceType;
	fSource.stream.bufferId = installEvent->fSource.stream.bufferId;
	fSource.stream.messagePortId = installEvent->fSource.stream.messagePortId;
	fPartInfo = &installEvent->fPartInfo;
	fReplied = false;
	NewtonErr err = noErr;
	newton_try
	{
		err = Install(installEvent->fPartId, fSourceType, &installEvent->fPartInfo);
	}
	newton_catch_all
	{
		err = (NewtonErr) (Long) CurrentException()->data;
	}
	end_try;
	if (!fReplied)
		ReplyImmed(err);
}


// ROM 0x0018251c Remove__12TPartHandlerFP18TPkPartRemoveEvent
void
TPartHandler::Remove(TPkPartRemoveEvent* removeEvent)
{
	Remove(removeEvent->fPartId, removeEvent->fPartType, removeEvent->fRemoveObjPtr);
}


// ROM 0x00182530 SetRemoveObjPtr__12TPartHandlerFl
void
TPartHandler::SetRemoveObjPtr(RemoveObjPtr obj)
{
	fRemoveObjPtr = obj;
}


// ROM 0x00182538 RejectPart__12TPartHandlerFv
void
TPartHandler::RejectPart()
{
	fAccept = false;
}


// ROM 0x00182544 ReplyImmed__12TPartHandlerFl
// The manager answered now: the error, the remove object and whether the
// part was taken.
void
TPartHandler::ReplyImmed(NewtonErr err)
{
	TPkPartInstallEventReply reply;
	fReplied = true;
	reply.fAccepted = fAccept;
	reply.fRemoveObjPtr = fRemoveObjPtr;
	reply.fEventError = err;
	fEventHandler->SetReply(sizeof(TPkPartInstallEventReply), &reply);
	fEventHandler->ReplyImmed();
}


/*------------------------------------------------------------------------------
	T P a r t E v e n t H a n d l e r
------------------------------------------------------------------------------*/

// ROM 0x00182044 __ct__17TPartEventHandlerFP12TPartHandler
TPartEventHandler::TPartEventHandler(TPartHandler* handler)
{
	fHandler = handler;
}


// ROM 0x0018208c AETestEvent__17TPartEventHandlerFP7TAEvent
// Only the part events for this handler's type: every 'newt/'pckm event
// goes down the same chain of handlers, one per part type.
Boolean
TPartEventHandler::AETestEvent(TAEvent* event)
{
	TPkBaseEvent* pkEvent = (TPkBaseEvent*) event;
	ULong code = pkEvent->fEventCode;
	if (code == kPkBeginLoadEvent || code == kPkBackupEvent)
		return false;
	if (code == kPkPartInstallEvent)
		return ((TPkPartInstallEvent*) event)->fPartInfo.type == (ULong) fHandler->fType;
	if (code == kPkPartRemoveEvent)
		return ((TPkPartRemoveEvent*) event)->fPartType == (ULong) fHandler->fType;
	return false;
}


// ROM 0x00182108 AEHandlerProc__17TPartEventHandlerFP10TUMsgTokenPUlP7TAEvent
// A part to install - its info and compressor made to point at the copies
// the event carries, then as TPartHandler::Install - or one to remove.
void
TPartEventHandler::AEHandlerProc(TUMsgToken* /*token*/, ULong* size, TAEvent* event)
{
	if (*size < sizeof(TPkBaseEvent))
		return;
	TPkBaseEvent* pkEvent = (TPkBaseEvent*) event;
	if (pkEvent->fEventCode == kPkPartInstallEvent)
	{
		TPkPartInstallEvent* installEvent = (TPkPartInstallEvent*) event;
		installEvent->fPartInfo.info = installEvent->fInfo;
		installEvent->fPartInfo.compressor = installEvent->fPartInfo.compressed ? installEvent->fCompressor : (char*) "";
		fHandler->Install(installEvent);
	}
	else if (pkEvent->fEventCode == kPkPartRemoveEvent)
	{
		TPkPartRemoveEvent* removeEvent = (TPkPartRemoveEvent*) event;
		fHandler->Remove(removeEvent->fPartId, removeEvent->fPartType, removeEvent->fRemoveObjPtr);
	}
}


// ROM 0x00182170 AECompletionProc__17TPartEventHandlerFP10TUMsgTokenPUlP7TAEvent
void
TPartEventHandler::AECompletionProc(TUMsgToken* /*token*/, ULong* /*size*/, TAEvent* /*event*/)
{ }
