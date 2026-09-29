/*
	File:		comms/Endpoint.cpp

	Contains:	TEndpoint's glue and non-virtual methods, the endpoint
				events, TEndpointClient, TEndpointEventHandler,
				TEndpointTimer, SendRPC, TCMOEndpointName and CMGetEndpoint
				(Endpoint.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Endpoint.h"
#include "CommManager.h"
#include "AppWorld.h"
#include "NewtErrors.h"
#include "OSErrors.h"
#include "SharedTypes.h"

#include <string.h>

// ROM: kCMErr_NoEndPointExists
#define kCMErr_NoEndPointExists		(-26006)


// ---------------------------------------------------------------------------
//	TEndpoint
// ---------------------------------------------------------------------------

// ROM 0x00382a0c New__9TEndpointSFPc
TEndpoint*
TEndpoint::New(char* implementation)
{
	TEndpoint* p = (TEndpoint*) AllocInstanceByName("TEndpoint", implementation);
	return p != nil ? (TEndpoint*) p->GlueNew() : nil;
}


// ROM 0x00382a38 Delete__9TEndpointFv
void
TEndpoint::Delete()
{
	GlueDelete();
}


// ROM 0x000ac2e0 InitBaseEndpoint__9TEndpointFP21TEndpointEventHandler
NewtonErr
TEndpoint::InitBaseEndpoint(TEndpointEventHandler* handler)
{
	NewtonErr err = noErr;
	fState = kUninit;
	fEventHandler = handler;
	fClientRefCon = 0;
	fSync = true;
	fToolIsRunning = true;
	fInfo = new TCMOTransportInfo;
	if (fInfo == nil)
		err = kEndpointErrNoPBs;
	return err;
}


// ROM 0x000ad82c DestroyBaseEndpoint__9TEndpointFv
void
TEndpoint::DestroyBaseEndpoint()
{
	if (fEventHandler != nil)
		delete fEventHandler;
	if (fInfo != nil)
		delete fInfo;
}


// ROM 0x000ad860 DeleteLeavingTool__9TEndpointFv
// (What it answers is the tool's port, for somebody else to use.)
TObjectId
TEndpoint::DeleteLeavingTool()
{
	TObjectId portId = fEventHandler->GetServicePortId();
	fToolIsRunning = false;
	Delete();
	return portId;
}


// ROM 0x000ad894 GetInfo__9TEndpointFP17TCMOTransportInfo
NewtonErr
TEndpoint::GetInfo(TCMOTransportInfo* info)
{
	if (fInfo == nil)
		return kError_Bad_Parameters;
	*info = *fInfo;
	return noErr;
}


// ROM 0x000ada18 SetClientHandler__9TEndpointFUl
void
TEndpoint::SetClientHandler(ULong clientHandler)
{
	fClientRefCon = clientHandler;
}


// ROM 0x000ada20 UseForks__9TEndpointFUc
Boolean
TEndpoint::UseForks(Boolean justDoIt)
{
	return true;
}


// ROM 0x000ad8d0 EasyOpen__9TEndpointFUl
NewtonErr
TEndpoint::EasyOpen(ULong clientHandler)
{
	NewtonErr err = Open(clientHandler);
	if (err == noErr)
		err = nBind(nil, kNoTimeout, true);
	if (err != noErr)
		return err;
	return nConnect(nil, nil, nil, kNoTimeout, true);
}


// ROM 0x000ad928 EasyConnect__9TEndpointFUlP12TOptionArrayT1
NewtonErr
TEndpoint::EasyConnect(ULong clientHandler, TOptionArray* options, TTimeout timeOut)
{
	NewtonErr err = Open(clientHandler);
	if (err == noErr)
		err = nBind(nil, timeOut, true);
	if (err != noErr)
		return err;
	return nConnect(options, nil, nil, timeOut, true);
}


// ROM 0x000ad98c EasyClose__9TEndpointFv
// Disconnect (or release), unbind and close; a disconnect or release that
// answers kEndpointErrBadState goes on to the unbind.
NewtonErr
TEndpoint::EasyClose()
{
	NewtonErr err;
	if (fState == kDataXfer)
		err = nDisconnect(nil, 0, 0, kNoTimeout, true);
	else if (fState == kInRel)
		err = nRelease(kNoTimeout, true);
	else
		goto unbind;
	if (err != noErr && err != kEndpointErrBadState)
		goto done;
unbind:
	err = nUnBind(kNoTimeout, true);
done:
	if (err != noErr)
		return err;
	return Close();
}


// ---------------------------------------------------------------------------
//	The endpoint events
// ---------------------------------------------------------------------------

// ROM 0x000ac614 __ct__14TEndpointEventFv
TEndpointEvent::TEndpointEvent()
{
	fAEventID = 'endp';
	fError = noErr;
	fClient = 0;
	fEventCode = 0;
	fReserved = 0;
	fTime = GetGlobalTime();
}


// ROM 0x000acd08 __ct__14TEndpointEventFlUlT1
TEndpointEvent::TEndpointEvent(NewtonErr error, ULong client, Long eventCode)
{
	fAEventID = 'endp';
	fError = error;
	fReserved = 0;
	fEventCode = eventCode;
	fClient = client;
	fTime = GetGlobalTime();
}


// ROM 0x000acd6c __ct__17TSndCompleteEventFv
TSndCompleteEvent::TSndCompleteEvent()
{
	fBuffer = nil;
	fData = nil;
	fCount = 0;
	fOptions = nil;
}


// ROM 0x000acdb4 __ct__17TRcvCompleteEventFv
TRcvCompleteEvent::TRcvCompleteEvent()
{
	fBuffer = nil;
	fData = nil;
	fCount = 0;
	fFlags = 0;
	fOptions = nil;
}


// ROM 0x000ace00 __ct__18TBindCompleteEventFv
TBindCompleteEvent::TBindCompleteEvent()
{
	fQueueLength = 0;
}


// ROM 0x000ace3c __ct__18TBindCompleteEventFlUlT1
TBindCompleteEvent::TBindCompleteEvent(NewtonErr error, ULong client, Long eventCode)
	: TEndpointEvent(error, client, eventCode)
{
	fQueueLength = 0;
}


// ROM 0x000ace90 __ct__13TDefaultEventFv
TDefaultEvent::TDefaultEvent()
{
	fData = 0;
}


// ROM 0x000acecc __ct__16TDisconnectEventFv
TDisconnectEvent::TDisconnectEvent()
{
	fDisconnectData = nil;
	fReason = 0;
	fSequence = 0;
}


// ROM 0x000acf10 __ct__16TDisconnectEventFlUl
TDisconnectEvent::TDisconnectEvent(NewtonErr error, ULong client)
	: TEndpointEvent(error, client, kEndpointEventDisconnect)
{
	fDisconnectData = nil;
	fReason = 0;
	fSequence = 0;
}


// ROM 0x000acf68 __ct__25TGetProtAddrCompleteEventFlUl
TGetProtAddrCompleteEvent::TGetProtAddrCompleteEvent(NewtonErr error, ULong client)
	: TEndpointEvent(error, client, kEndpointEventGetProtAddr)
{
	fBoundAddr = nil;
	fPeerAddr = nil;
}


// ROM 0x000acfbc __ct__21TOptMgmtCompleteEventFv
TOptMgmtCompleteEvent::TOptMgmtCompleteEvent()
{
	fOptions = nil;
}


// ROM 0x000acff8 __ct__21TOptMgmtCompleteEventFlUl
TOptMgmtCompleteEvent::TOptMgmtCompleteEvent(NewtonErr error, ULong client)
	: TEndpointEvent(error, client, kEndpointEventOptMgmtComplete)
{
	fOptions = nil;
}


// ROM 0x000ad048 __ct__21TConnectCompleteEventFv
TConnectCompleteEvent::TConnectCompleteEvent()
{
	fAddr = nil;
	fOptions = nil;
	fData = nil;
	fSequence = 0;
}


// ROM 0x000ad090 __ct__21TConnectCompleteEventFlUlT1
TConnectCompleteEvent::TConnectCompleteEvent(NewtonErr error, ULong client, Long eventCode)
	: TEndpointEvent(error, client, eventCode)
{
	fAddr = nil;
	fOptions = nil;
	fData = nil;
	fSequence = 0;
}


// ---------------------------------------------------------------------------
//	TEndpointClient
// ---------------------------------------------------------------------------

// ROM 0x000ac3cc __ct__15TEndpointClientFv
TEndpointClient::TEndpointClient()
{
	fEndpoint = nil;
}


// ROM 0x000ac424 __dt__15TEndpointClientFv
TEndpointClient::~TEndpointClient()
{
}


// ROM 0x000ac464 Init__15TEndpointClientFP9TEndpointUlT2
NewtonErr
TEndpointClient::Init(TEndpoint* endpoint, ULong eventId, ULong eventClass)
{
	NewtonErr err = TAEventHandler::Init(eventId, eventClass);
	fEndpoint = endpoint;
	endpoint->SetClientHandler((ULong) this);
	return err;
}


// ROM 0x000ac4a0 AETestEvent__15TEndpointClientFP7TAEvent
Boolean
TEndpointClient::AETestEvent(TAEvent* event)
{
	return ((TEndpointEvent*) event)->fClient == (ULong) this;
}


// ROM 0x000ac4b8 AEHandlerProc__15TEndpointClientFP10TUMsgTokenPUlP7TAEvent
// An endpoint event, to the method for its code.
void
TEndpointClient::AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event)
{
	TEndpointEvent* ev = (TEndpointEvent*) event;
	switch (ev->fEventCode)
	{
	case kEndpointEventDisconnect:			Disconnect(ev); break;
	case kEndpointEventRelease:				Release(ev); break;
	case kEndpointEventAbortComplete:		AbortComplete(ev); break;
	case kEndpointEventUnBindComplete:		UnBindComplete(ev); break;
	case kEndpointEventBindComplete:		BindComplete(ev); break;
	case kEndpointEventDisconnectComplete:	DisconnectComplete(ev); break;
	case kEndpointEventReleaseComplete:		ReleaseComplete(ev); break;
	case kEndpointEventConnectComplete:		ConnectComplete(ev); break;
	case kEndpointEventAcceptComplete:		AcceptComplete(ev); break;
	case kEndpointEventListenComplete:		ListenComplete(ev); break;
	case kEndpointEventOptMgmtComplete:		OptMgmtComplete(ev); break;
	case kEndpointEventGetProtAddr:			GetProtAddr(ev); break;
	case kEndpointEventSndComplete:			SndComplete(ev); break;
	case kEndpointEventRcvComplete:			RcvComplete(ev); break;
	default:								Default(ev); break;
	}
}


// ROM 0x000ac600 AECompletionProc__15TEndpointClientFP10TUMsgTokenPUlP7TAEvent
void
TEndpointClient::AECompletionProc(TUMsgToken* token, ULong* size, TAEvent* event)
{
}


// ROM 0x000ac3a4 Default__15TEndpointClientFP14TEndpointEvent
void TEndpointClient::Default(TEndpointEvent* event) { }
// ROM 0x000ac604 Disconnect__15TEndpointClientFP14TEndpointEvent
void TEndpointClient::Disconnect(TEndpointEvent* event) { Default(event); }
// ROM 0x000ac60c Release__15TEndpointClientFP14TEndpointEvent
void TEndpointClient::Release(TEndpointEvent* event) { Default(event); }
// ROM 0x000ac420 DefaultComplete__15TEndpointClientFP14TEndpointEvent
void TEndpointClient::DefaultComplete(TEndpointEvent* event) { }
// ROM 0x000ac3a8 SndComplete__15TEndpointClientFP14TEndpointEvent
void TEndpointClient::SndComplete(TEndpointEvent* event) { }
// ROM 0x000ac3ac RcvComplete__15TEndpointClientFP14TEndpointEvent
void TEndpointClient::RcvComplete(TEndpointEvent* event) { }
// ROM 0x000ac3b0 GetProtAddr__15TEndpointClientFP14TEndpointEvent
void TEndpointClient::GetProtAddr(TEndpointEvent* event) { }
// ROM 0x000ac3c0 OptMgmtComplete__15TEndpointClientFP14TEndpointEvent
void TEndpointClient::OptMgmtComplete(TEndpointEvent* event) { }
// ROM 0x000ac3b4 ListenComplete__15TEndpointClientFP14TEndpointEvent
void TEndpointClient::ListenComplete(TEndpointEvent* event) { }
// ROM 0x000ac3b8 ConnectComplete__15TEndpointClientFP14TEndpointEvent
void TEndpointClient::ConnectComplete(TEndpointEvent* event) { }
// ROM 0x000ac3bc AcceptComplete__15TEndpointClientFP14TEndpointEvent
void TEndpointClient::AcceptComplete(TEndpointEvent* event) { }
// ROM 0x000ac3c4 ReleaseComplete__15TEndpointClientFP14TEndpointEvent
void TEndpointClient::ReleaseComplete(TEndpointEvent* event) { }
// ROM 0x000ac3c8 DisconnectComplete__15TEndpointClientFP14TEndpointEvent
void TEndpointClient::DisconnectComplete(TEndpointEvent* event) { }
// ROM 0x000ac414 BindComplete__15TEndpointClientFP14TEndpointEvent
void TEndpointClient::BindComplete(TEndpointEvent* event) { }
// ROM 0x000ac418 UnBindComplete__15TEndpointClientFP14TEndpointEvent
void TEndpointClient::UnBindComplete(TEndpointEvent* event) { }
// ROM 0x000ac41c AbortComplete__15TEndpointClientFP14TEndpointEvent
void TEndpointClient::AbortComplete(TEndpointEvent* event) { }


// ---------------------------------------------------------------------------
//	TEndpointEventHandler
// ---------------------------------------------------------------------------

// ROM 0x000acc9c __ct__21TEndpointEventHandlerFP9TEndpointUc
TEndpointEventHandler::TEndpointEventHandler(TEndpoint* endpoint, Boolean handleAborts)
{
	fEndpoint = endpoint;
	fBlocked = false;
	fAborted = false;
	fHandleAborts = handleAborts;
}


// ROM 0x000ac678 __dt__21TEndpointEventHandlerFv
TEndpointEventHandler::~TEndpointEventHandler()
{
}


// ROM 0x000ac6d0 Init__21TEndpointEventHandlerFUlN21
// Events for the service's id in the 'newt class, the service's port, and
// the port Block waits on.  (The ROM makes it with MakeObject; the host
// through TPseudoSyncState::Init, which does the same.)
NewtonErr
TEndpointEventHandler::Init(TObjectId servicePortId, ULong eventId, ULong eventClass)
{
	NewtonErr err = TAEventHandler::Init(eventId, eventClass);
	if (err != noErr)
		return err;
	fServicePort.CopyObject(servicePortId);
	return fSyncState.Init();
}


// ROM 0x000ac710 CallService__21TEndpointEventHandlerFUlP14TUAsyncMessageP7TAEventlT3T4N21Uc
// A request to the tool.  Asynchronously: an async RPC whose reply comes to
// the world's port (a timer set on it first, when there is a timeout).
// Synchronously: the world forked (when forking is enabled), its mutex given
// up while the RPC waits, and taken back.
NewtonErr
TEndpointEventHandler::CallService(ULong msgType, TUAsyncMessage* asyncMsg, TAEvent* request, ULong requestSize,
									TAEvent* reply, ULong replySize, TTimeout timeout, ULong refCon, Boolean sync)
{
	NewtonErr err;
	if (!sync)
	{
		TAppWorld* world = (TAppWorld*) GetGlobals();
		err = asyncMsg->SetCollectorPort(*world->GetMyPort());
		if (err == noErr && (err = AddTimer(timeout, refCon)) == noErr)
		{
			err = SendRPC(this, &fServicePort, asyncMsg, request, requestSize, reply, replySize, 0, nil, msgType, false);
			if (err != noErr)
				KillTimer(refCon);
		}
	}
	else
	{
		TForkWorld* world = (TForkWorld*) GetGlobals();
		err = world->Fork(nil);
		if (err == noErr && (err = world->ReleaseMutex()) == noErr)
		{
			ULong returnSize;
			err = fServicePort.SendRPC(&returnSize, request, requestSize, reply, replySize, timeout, msgType, false);
			NewtonErr mutexErr = ((TForkWorld*) GetGlobals())->AcquireMutex();
			if (err == noErr && mutexErr != noErr)
				err = mutexErr;
		}
	}
	return err;
}


// ROM 0x000ac874 CallServiceNoForks__21TEndpointEventHandlerFUlP14TUAsyncMessageP7TAEventlT3T4N21Uc
// The same, without forking the world for a synchronous call.
NewtonErr
TEndpointEventHandler::CallServiceNoForks(ULong msgType, TUAsyncMessage* asyncMsg, TAEvent* request, ULong requestSize,
									TAEvent* reply, ULong replySize, TTimeout timeout, ULong refCon, Boolean sync)
{
	NewtonErr err;
	if (!sync)
	{
		TAppWorld* world = (TAppWorld*) GetGlobals();
		err = asyncMsg->SetCollectorPort(*world->GetMyPort());
		if (err == noErr && (err = AddTimer(timeout, refCon)) == noErr)
		{
			err = SendRPC(this, &fServicePort, asyncMsg, request, requestSize, reply, replySize, 0, nil, msgType, false);
			if (err != noErr)
				KillTimer(refCon);
		}
	}
	else
	{
		TForkWorld* world = (TForkWorld*) GetGlobals();
		err = world->ReleaseMutex();
		if (err == noErr)
		{
			ULong returnSize;
			err = fServicePort.SendRPC(&returnSize, request, requestSize, reply, replySize, timeout, msgType, false);
			NewtonErr mutexErr = ((TForkWorld*) GetGlobals())->AcquireMutex();
			if (err == noErr && mutexErr != noErr)
				err = mutexErr;
		}
	}
	return err;
}


// ROM 0x000ac9c4 UseForks__21TEndpointEventHandlerFUc
Boolean
TEndpointEventHandler::UseForks(Boolean useForks)
{
	return true;
}


// ROM 0x000ac9cc Block__21TEndpointEventHandlerFUl
// Wait (the world forked meanwhile) until Unblock or an abort.
NewtonErr
TEndpointEventHandler::Block(ULong timeout)
{
	if (fBlocked)
		return -36030;
	fBlocked = true;
	NewtonErr err = fSyncState.Block(timeout);
	fBlocked = false;
	if (err == noErr)
	{
		if (fAborted)
		{
			fAborted = false;
			err = kError_Call_Aborted;
		}
	}
	return err;
}


// ROM 0x000aca28 Unblock__21TEndpointEventHandlerFv
void
TEndpointEventHandler::Unblock()
{
	if (!fBlocked)
		return;
	TUnblockEvent event(&fSyncState);
	TForkWorld* world = (TForkWorld*) GetGlobals();
	world->ReleaseMutex();
	fSyncState.Send(&event, sizeof(event));
	((TForkWorld*) GetGlobals())->AcquireMutex();
}


// ROM 0x000aca3c AETestEvent__21TEndpointEventHandlerFP7TAEvent
Boolean
TEndpointEventHandler::AETestEvent(TAEvent* event)
{
	return ((TEndpointEvent*) event)->fClient == (ULong) this;
}


// ROM 0x000aca54 AEHandlerProc__21TEndpointEventHandlerFP10TUMsgTokenPUlP7TAEvent
// An event for the endpoint - or an abort ('abrt), when it handles aborts;
// a message wanting a reply gets one at once.
void
TEndpointEventHandler::AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event)
{
	if (fHandleAborts && ((TEndpointEvent*) event)->fError == 'abrt')
		Abort();
	else
	{
		TAppWorld* world = (TAppWorld*) GetGlobals();
		fEndpoint->HandleEvent(world->AEGetMsgType(), event, *size);
	}
	if (token != nil && token->GetReplyId() != 0)
		((TAppWorld*) GetGlobals())->AEReplyImmed();
}


// ROM 0x000acad8 AECompletionProc__21TEndpointEventHandlerFP10TUMsgTokenPUlP7TAEvent
// The reply to one of the endpoint's asynchronous calls.
void
TEndpointEventHandler::AECompletionProc(TUMsgToken* token, ULong* size, TAEvent* event)
{
	fEndpoint->HandleComplete(token, size, event);
}


// ROM 0x000acc34 IdleProc__21TEndpointEventHandlerFP10TUMsgTokenPUlP7TAEvent
void
TEndpointEventHandler::IdleProc(TUMsgToken* token, ULong* size, TAEvent* event)
{
}


// ROM 0x000acad4 DoEventLoop__21TEndpointEventHandlerFUl
void
TEndpointEventHandler::DoEventLoop(TTimeout timeout)
{
}


// ROM 0x000acae0 GetServicePortId__21TEndpointEventHandlerFv
TObjectId
TEndpointEventHandler::GetServicePortId()
{
	return fServicePort.fId;
}


// ROM 0x000acae8 AddToAppWorld__21TEndpointEventHandlerFv
NewtonErr
TEndpointEventHandler::AddToAppWorld()
{
	return -1;
}


// ROM 0x000acaf0 RemoveFromAppWorld__21TEndpointEventHandlerFv
NewtonErr
TEndpointEventHandler::RemoveFromAppWorld()
{
	return -1;
}


// ROM 0x000acaf8 HandleAborts__21TEndpointEventHandlerFUc
// (It changes nothing: the ROM's is empty.)
void
TEndpointEventHandler::HandleAborts(Boolean handleAborts)
{
}


// ROM 0x000acafc Abort__21TEndpointEventHandlerFv
void
TEndpointEventHandler::Abort()
{
	fAborted = true;
	fEndpoint->nAbort(true);
	if (!fBlocked)
		return;
	fSyncState.Unblock();
	fBlocked = false;
}


// ROM 0x000acb40 Timeout__21TEndpointEventHandlerFP14TEndpointTimer
void
TEndpointEventHandler::Timeout(TEndpointTimer* timer)
{
	fEndpoint->Timeout(timer->GetRefCon());
	if (timer != nil)
		delete timer;
}


// ROM 0x000acb70 AddTimer__21TEndpointEventHandlerFUlT1
NewtonErr
TEndpointEventHandler::AddTimer(TTimeout timeout, ULong refCon)
{
	NewtonErr err = noErr;
	if (timeout != 0)
	{
		TEndpointTimer* timer = new TEndpointTimer(this, ((TAppWorld*) GetGlobals())->GetTimerQueue(), refCon);
		if (timer == nil)
			err = MemError();
		else if (!timer->Prime(timeout))
			delete timer;
	}
	return err;
}


// ROM 0x000acbe8 KillTimer__21TEndpointEventHandlerFUl
NewtonErr
TEndpointEventHandler::KillTimer(ULong refCon)
{
	TTimerElement* timer = ((TAppWorld*) GetGlobals())->GetTimerQueue()->Cancel(refCon);
	if (timer != nil)
		delete timer;
	return noErr;
}


// ROM 0x000acc30 TerminateEventLoop__21TEndpointEventHandlerFv
void
TEndpointEventHandler::TerminateEventLoop()
{
}


// ROM 0x000acc38 __ct__14TEndpointTimerFP21TEndpointEventHandlerP11TTimerQueueUl
TEndpointTimer::TEndpointTimer(TEndpointEventHandler* handler, TTimerQueue* queue, ULong refCon)
	: TTimerElement(queue, refCon)
{
	fHandler = handler;
}


// ROM 0x000acc90 Timeout__14TEndpointTimerFv
// (The handler's Timeout in line: the endpoint told, and the timer gone.)
void
TEndpointTimer::Timeout()
{
	fHandler->fEndpoint->Timeout(GetRefCon());
	delete this;
}


// ROM 0x000312b4 SendRPC__FP14TAEventHandlerP6TUPortP14TUAsyncMessagePvUlT4N25P5TTimeT5Uc
// (What SendRPCGoo answers is not passed on: the ROM answers the refCon's
// result.)
NewtonErr
SendRPC(TAEventHandler* handler, TUPort* port, TUAsyncMessage* asyncMsg, void* request, ULong requestSize,
		void* reply, ULong replySize, TTimeout timeout, TTime* when, ULong msgType, Boolean urgent)
{
	NewtonErr err = asyncMsg->SetUserRefCon((ULong) handler);
	if (err == noErr)
		port->SendRPC(asyncMsg, request, requestSize, reply, replySize, timeout, when, msgType, urgent);
	return err;
}


// ---------------------------------------------------------------------------
//	CMGetEndpoint
// ---------------------------------------------------------------------------

// ROM 0x0006cb6c __ct__16TCMOEndpointNameFv
TCMOEndpointName::TCMOEndpointName()
	: TOption(kOptionType)
{
	SetLabel('endp');
	SetLength(sizeof(TCMOEndpointName) - sizeof(TOption));
	memcpy(fClassName, "TSerialEndpoint", 16);
}


// ROM 0x0006b838 CMGetEndpoint__FP12TOptionArrayPP9TEndpointUc
NewtonErr
CMGetEndpoint(TOptionArray* options, TEndpoint** endPoint, Boolean handleAborts)
{
	NewtonErr err = kCMErr_NoEndPointExists;
	*endPoint = nil;
	TCMOEndpointName defaultName;
	TServiceInfo serviceInfo;
	TCMOEndpointName* name;
	{
		TOptionIterator iter(options);
		name = (TCMOEndpointName*) iter.FindOption('endp');
		if (name == nil)
			name = &defaultName;
		*endPoint = (TEndpoint*) NewByName("TEndpoint", name->fClassName);
	}
	if (*endPoint == nil)
		return err;
	TEndpointEventHandler* handler = new TEndpointEventHandler(*endPoint, handleAborts);
	if (handler != nil)
	{
		err = CMStartService(options, &serviceInfo);
		if (err == noErr)
		{
			handler->Init(serviceInfo.GetPortId(), serviceInfo.GetServiceId(), kNewtEventClass);
			err = (*endPoint)->InitBaseEndpoint(handler);
		}
		if (err == noErr)
			return noErr;
	}
	(*endPoint)->Delete();
	*endPoint = nil;
	return err;
}
