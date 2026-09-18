/*
	File:		user/SystemEvents.cpp

	Contains:	TSystemEvent and TSendSystemEvent (SystemEvents.h): the client
				side of the name server's system events.  A port registers
				for an event ('pwon', 'pwof', 'card', ...); a sender hands the
				name server a message, which it delivers to every registered
				port in turn.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "SystemEvents.h"
#include "UserGlobals.h"
#include "OSErrors.h"
#include "NewtonMemory.h"
#include "os600/ObjectMessage.h"

// the message type system events are sent to the name server with
static const ULong kSysEventMsgType = 1;


// ROM 0x001313a0 __ct__12TSystemEventFUl
TSystemEvent::TSystemEvent(SystemEvent event)
{
	fEvent = event;
	fNameServerPort.CopyObject(GetPortSWI(kGetNameServerPort));
}


// ROM 0x001313fc SetEvent__12TSystemEventFUl
void
TSystemEvent::SetEvent(SystemEvent event)
{
	fEvent = event;
}


// ROM 0x00131404 RegisterForSystemEvent__12TSystemEventFUlN21
NewtonErr
TSystemEvent::RegisterForSystemEvent(TObjectId portId, ULong sendFilter, TTimeout timeout)
{
	TSysEventRequest request;
	TNameServerReply reply;
	request.fCommand = kRegisterForSystemEvent;
	request.fTheEvent = fEvent;
	request.fSysEventObjId = portId;
	request.fSysEventTimeOut = timeout;
	request.fSysEventSendFilter = sendFilter;
	ULong returnSize;
	return fNameServerPort.SendRPC(&returnSize, &request, sizeof(request), &reply, sizeof(reply));
}


// ROM 0x001314a0 UnRegisterForSystemEvent__12TSystemEventFUl
NewtonErr
TSystemEvent::UnRegisterForSystemEvent(TObjectId portId)
{
	TSysEventRequest request;
	TNameServerReply reply;
	request.fCommand = kUnRegisterForSystemEvent;
	request.fTheEvent = fEvent;
	request.fSysEventObjId = portId;
	ULong returnSize;
	return fNameServerPort.SendRPC(&returnSize, &request, sizeof(request), &reply, sizeof(reply));
}


// ROM 0x001313f4 Init__16TSendSystemEventFv
// The shared memory message that carries the event to the registrants.
NewtonErr
TSendSystemEvent::Init()
{
	ObjectMessage msg;
	return fMsgToSend.MakeObject(kObjectSharedMemMsg, &msg, kObjectMessage_HeaderSize);
}


// ROM 0x0013152c SendSystemEvent__16TSendSystemEventFPvUl
// Synchronous: returns when every registrant has had the message (the name
// server replies then), or at once with kError_Not_Registered if nobody has.
NewtonErr
TSendSystemEvent::SendSystemEvent(void* message, ULong messageSize)
{
	TSysEventRequest request;
	NewtonErr err = fMsgToSend.SetBuffer(message, messageSize, kSMemReadOnly);
	if (err == noErr)
	{
		request.fCommand = kSendSystemEvent;
		request.fTheEvent = fEvent;
		request.fSysEventObjId = fMsgToSend;
		ULong returnSize;
		err = fNameServerPort.SendRPC(&returnSize, &request, sizeof(request), nil, 0, kNoTimeout, kSysEventMsgType);
	}
	return err;
}


// ROM 0x001315d8 SendSystemEvent__16TSendSystemEventFP14TUAsyncMessagePvUlT2T3
// Asynchronous, through the caller's async message; a reply buffer (given
// the message as its initial content) collects what a registrant replies.
NewtonErr
TSendSystemEvent::SendSystemEvent(TUAsyncMessage* asyncMessage, void* message, ULong messageSize, void* reply, ULong replySize)
{
	if (message == (void*) (uintptr_t) (ULong) kPortSend_BufferAlreadySet)
		return kError_Bad_Parameters;
	if (reply != (void*) (uintptr_t) (ULong) kPortSend_BufferAlreadySet)
	{
		if (replySize > messageSize)
			replySize = messageSize;
		TUSharedMem replyMem(asyncMessage->GetReplyMemId());
		NewtonErr err = replyMem.SetBuffer(reply, replySize, kSMemReadWrite);
		if (err != noErr)
			return err;
		if (replySize != 0)
			BlockMove(message, reply, replySize);
	}
	NewtonErr err = fMsgToSend.SetBuffer(message, messageSize, kSMemReadOnly);
	if (err == noErr)
	{
		fMsgToNameServer.fCommand = kSendSystemEvent;
		fMsgToNameServer.fTheEvent = fEvent;
		fMsgToNameServer.fSysEventObjId = fMsgToSend;
		err = fNameServerPort.SendRPC(asyncMessage, &fMsgToNameServer, sizeof(fMsgToNameServer), nil, 0, kNoTimeout, nil, kSysEventMsgType);
	}
	return err;
}
