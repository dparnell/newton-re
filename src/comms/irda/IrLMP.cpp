/*
	File:		comms/irda/IrLMP.cpp

	Contains:	TIrLMP and TLSAPConn (IrLMP.h).

	Reconstructed from the MP2x00 US ROM (0x000f5e84-0x000f6fe0); each
	function cites its origin.
*/

#include "IrLMP.h"
#include "IrGlue.h"
#include "IrLAP.h"
#include "IrDscInfo.h"
#include "BufferSegment.h"
#include "CommErrors.h"
#include "NewtonTime.h"
#include "host/RomBugs.h"

// an LM control frame's opcodes
#define kLMConnect			0x01
#define kLMConnectConfirm	0x81
#define kLMDisconnect		0x02


/*------------------------------------------------------------------------------
	TIrLMP
------------------------------------------------------------------------------*/

// ROM 0x000f5e84 __ct__6TIrLMPFv
TIrLMP::TIrLMP()
{
	fState = kIrLMPReady;
	fTickerUsers = 0;
	fLAPConn = nil;
}


// ROM 0x000f5ed4 __dt__6TIrLMPFv
TIrLMP::~TIrLMP()
{
	DeInit();
}


// ROM 0x000f5fac Init__6TIrLMPFP7TIrGlueP6TIrLAP
NewtonErr
TIrLMP::Init(TIrGlue* glue, TIrLAP* lap)
{
	fIrGlue = glue;
	fLAP = lap;
	NewtonErr err = TIrStream::Init(glue);
	if (err == noErr)
	{
		err = -7000;
		fLAPConn = new TIrLAPConn;
		if (fLAPConn != nil && (err = fLAPConn->Init(glue, lap)) == noErr)
			return noErr;
	}
	DeInit();
	return err;
}


// ROM 0x000f6020 Reset__6TIrLMPFv
void
TIrLMP::Reset(void)
{
	fState = kIrLMPReady;
	fTickerUsers = 0;
	if (fLAPConn != nil)
		fLAPConn->Reset();
}


// ROM 0x000f603c DeInit__6TIrLMPFv
void
TIrLMP::DeInit(void)
{
	if (fLAPConn != nil)
	{
		delete fLAPConn;
		fLAPConn = nil;
	}
}


// ROM 0x000f5f1c Demultiplexor__6TIrLMPFP14CBufferSegment
void
TIrLMP::Demultiplexor(CBufferSegment* buffer)
{
	fLAPConn->Demultiplexor(buffer);
}


// ROM 0x000f5f24 FillInLMPDUHeader__6TIrLMPFP16TIrDataXferEventPUc
ULong
TIrLMP::FillInLMPDUHeader(TIrDataXferEvent* event, UByte* buffer)
{
	return fLAPConn->FillInLMPDUHeader(event, buffer);
}


// ROM 0x000f5f2c StartOneSecTicker__6TIrLMPFv
// The ticker runs while anybody wants it.
void
TIrLMP::StartOneSecTicker(void)
{
	if (fTickerUsers++ == 0)
		fIrGlue->StartTimer2(kSeconds, kIrOneSecTick);
}


// ROM 0x000f5f50 StopOneSecTicker__6TIrLMPFv
void
TIrLMP::StopOneSecTicker(void)
{
	if (fTickerUsers == 0)
		return;
	if (--fTickerUsers == 0)
		fIrGlue->StopTimer2();
}


// ROM 0x000f5f74 TimerComplete__6TIrLMPFUl
void
TIrLMP::TimerComplete(ULong kind)
{
	fLAPConn->TimerComplete(kind);
	if (fTickerUsers != 0)
		fIrGlue->StartTimer2(kSeconds, kIrOneSecTick);
}


// ROM 0x000f6070 NextState__6TIrLMPFUl
void
TIrLMP::NextState(ULong event)
{
	if (fState == kIrLMPReady)
		HandleReadyStateEvent(event);
	else if (fState == kIrLMPDiscover)
		HandleDiscoverStateEvent(event);
	else if (fState == kIrLMPResolveAddress)
		HandleResolveAddressStateEvent(event);
}


// ROM 0x000f6090 HandleReadyStateEvent__6TIrLMPFUl
// Discovery and puts to the link; connections and gets to its connection
// side; a disconnect for the link (no LSAP connection) to the link, the
// answer to the glue.
void
TIrLMP::HandleReadyStateEvent(ULong event)
{
	TIrEvent* current = fCurrentEvent;
	switch (event)
	{
	case kIrDiscoverRequest:
		current->fDevAddr = kIrAllDevices;
		fState = kIrLMPDiscover;
		fLAP->EnqueueEvent(current);
		break;
	case kIrConnectRequest:
	case kIrConnectReply:
	case kIrListenRequest:
	case kIrListenReply:
	case kIrGetDataRequest:
	case kIrCancelGetRequest:
		fLAPConn->EnqueueEvent(current);
		break;
	case kIrPutDataRequest:
	case kIrCancelPutRequest:
		fLAP->EnqueueEvent(current);
		break;
	case kIrDisconnectRequest:
		if (current->fLSAPConn == nil)
			fLAP->EnqueueEvent(current);
		else
			fLAPConn->EnqueueEvent(current);
		break;
	case kIrDisconnectReply:
		if (current->fLSAPConn == nil)
			fIrGlue->EnqueueEvent(current);
		else
			fLAPConn->EnqueueEvent(current);
		break;
	}
}


// ROM 0x000f6150 HandleDiscoverStateEvent__6TIrLMPFUl
// Two devices answering with one address: the first asked to pick another
// (the discovery again, addressed to it).
void
TIrLMP::HandleDiscoverStateEvent(ULong event)
{
	TIrEvent* current = fCurrentEvent;
	if (event == kIrDiscoverReply)
	{
		if (current->fResult == noErr && current->fPassiveDiscovery == 0
		 && AddrConflicts(current->fDiscoveredList, true))
		{
			current->fEvent = kIrDiscoverRequest;
			current->fDevAddr = fConflicts[--fNumConflicts];
			fState = kIrLMPResolveAddress;
			fLAP->EnqueueEvent(current);
		}
		else
		{
			fState = kIrLMPReady;
			fIrGlue->EnqueueEvent(current);
		}
	}
	else if (event == kIrDisconnectRequest)
		fLAP->EnqueueEvent(current);
	else if (event == kIrDisconnectReply)
		fIrGlue->EnqueueEvent(current);
}


// ROM 0x000f6204 HandleResolveAddressStateEvent__6TIrLMPFUl
void
TIrLMP::HandleResolveAddressStateEvent(ULong event)
{
	TIrEvent* current = fCurrentEvent;
	if (event == kIrDiscoverReply)
	{
		AddrConflicts(current->fDiscoveredList, false);
		if (current->fResult != noErr || fNumConflicts == 0)
		{
			fState = kIrLMPReady;
			fIrGlue->EnqueueEvent(current);
		}
		else
		{
			// ROM BUG (fixed): the block goes back to the link as a discover
			// reply (not a request, as for the first conflict), which the
			// link ignores: with more than one conflict the discovery never
			// ends.  The fix makes it a request again.
			if (RomBugFixed())
				current->fEvent = kIrDiscoverRequest;
			current->fDevAddr = fConflicts[--fNumConflicts];
			fLAP->EnqueueEvent(current);

		}
	}
	else if (event == kIrDisconnectRequest)
		fLAP->EnqueueEvent(current);
	else if (event == kIrDisconnectReply)
		fIrGlue->EnqueueEvent(current);
}


// ROM 0x000f62a0 AddrConflicts__6TIrLMPFP5CListUc
// Devices with an address seen already (ours among them) taken out of the
// list, and noted (at most eight) when asked.
Boolean
TIrLMP::AddrConflicts(CList* devices, Boolean addToConflicts)
{
	ULong seen[0x11];
	ULong numSeen = 1;
	Boolean conflicts = false;
	seen[0] = fLAP->fMyDevAddr;
	if (addToConflicts)
		fNumConflicts = 0;
	for (ArrayIndex i = devices->Count() - 1; i >= 0; i--)
	{
		TIrDscInfo* device = (TIrDscInfo*) devices->At(i);
		ULong addr = device->fDevAddr;
		Boolean found = false;					// (never set in the ROM)
		ULong j;
		for (j = 0; j < numSeen; j++)
		{
			if (seen[j] == addr)
			{
				conflicts = true;
				if (addToConflicts && fNumConflicts < 8)
					fConflicts[fNumConflicts++] = addr;
				devices->RemoveElementsAt(i, 1);
				if (device != nil)
					delete device;
				break;
			}
		}
		if (j < numSeen)
			continue;
		if (!found && numSeen < 0x11)
			seen[numSeen++] = addr;
	}
	return conflicts;
}


/*------------------------------------------------------------------------------
	TLSAPConn
------------------------------------------------------------------------------*/

// ROM 0x000f639c __ct__9TLSAPConnFv
TLSAPConn::TLSAPConn()
{
	fState = kLSAPConnDisconnected;
	fPendConnLstn = nil;
	fMyLSAPId = 0xff;
	fTimerRunning = false;
}


// ROM 0x000f63f4 __dt__9TLSAPConnFv
TLSAPConn::~TLSAPConn()
{
	DeInit();
}


// ROM 0x000f68e0 Init__9TLSAPConnFP7TIrGlueP6TIrLMPP9TIrStream
NewtonErr
TLSAPConn::Init(TIrGlue* glue, TIrLMP* lmp, TIrStream* client)
{
	fIrGlue = glue;
	fLMP = lmp;
	fClient = client;
	NewtonErr err = TIrStream::Init(glue);
	if (err == noErr)
		return noErr;
	DeInit();
	return err;
}


// ROM 0x000f6bbc DeInit__9TLSAPConnFv
// Our LSAP given back (not the IAS's 0).
void
TLSAPConn::DeInit(void)
{
	if (fMyLSAPId != 0xff && fMyLSAPId != 0)
		fIrGlue->ReleaseLSAPId(fMyLSAPId);
	fMyLSAPId = 0xff;
}


// ROM 0x000f6bb4 AssignId__9TLSAPConnFUl
void
TLSAPConn::AssignId(ULong lsapId)
{
	fMyLSAPId = lsapId;
}


// ROM 0x000f682c GetPendConnLstn__9TLSAPConnFv
TIrLSAPConnEvent*
TLSAPConn::GetPendConnLstn(void)
{
	return fPendConnLstn;
}


// ROM 0x000f6a44 YourData__9TLSAPConnFR12TLMPDUHeaderUc
// A frame to our LSAP from the other's - or, while we listen (the other's
// not known yet), an LM connect from any, which makes it the other's
// unless we are only asking.
Boolean
TLSAPConn::YourData(TLMPDUHeader& header, UByte connecting)
{
	if (fMyLSAPId != header.fDstLSAPId)
		return false;
	if (header.fSrcLSAPId == fPeerLSAPId)
		return true;
	if (fPeerLSAPId == 0xff && header.fOpCode == kLMConnect)
	{
		if (connecting == 0)
			fPeerLSAPId = header.fSrcLSAPId;
		return true;
	}
	return false;
}


// ROM 0x000f6bec NextState__9TLSAPConnFUl
void
TLSAPConn::NextState(ULong event)
{
	switch (fState)
	{
	case kLSAPConnDisconnected:			HandleDisconnectedStateEvent(event); break;
	case kLSAPConnConnectPending:		HandleConnectPendingStateEvent(event); break;
	case kLSAPConnConnect:				HandleConnectStateEvent(event); break;
	case kLSAPConnListenPending:		HandleListenPendingStateEvent(event); break;
	case kLSAPConnListen:				HandleListenStateEvent(event); break;
	case kLSAPConnAccept:				HandleAcceptStateEvent(event); break;
	case kLSAPConnDataTransferReady:	HandleDataTransferReadyStateEvent(event); break;
	}
}


// ROM 0x000f6c30 HandleDisconnectedStateEvent__9TLSAPConnFUl
// A connect or listen goes down (the link comes up for it); anything
// else is answered at once, a get or put not connected.
void
TLSAPConn::HandleDisconnectedStateEvent(ULong event)
{
	TIrEvent* current;
	switch (event)
	{
	case kIrConnectRequest:
	case kIrListenRequest:
		current = fCurrentEvent;
		PassRequestToLMP();
		SaveCurrentRequest();
		fPendConnLstn = fCurrentEvent;
		fConnectData = current->fConnectData;
		if (event == kIrConnectRequest)
		{
			fConnecting = true;
			fPeerLSAPId = current->fLSAPId;
			fState = kLSAPConnConnectPending;
		}
		else
		{
			fConnecting = false;
			fPeerLSAPId = 0xff;
			fState = kLSAPConnListenPending;
		}
		return;
	case kIrConnectReply:
	case kIrGetDataReply:
	case kIrPutDataReply:
	case kIrDisconnectReply:
		fCurrentEvent->fEvent = fCurrentEvent->fPendingEvent + 1;
		break;
	case kIrDisconnectRequest:
		fCurrentEvent->fEvent = kIrDisconnectReply;
		break;
	case kIrGetDataRequest:
	case kIrPutDataRequest:
		fCurrentEvent->fEvent++;
		fCurrentEvent->fResult = kCommErrNotConnected;
		break;
	default:
		return;
	}
	fClient->EnqueueEvent(fCurrentEvent);
}


// ROM 0x000f6d34 HandleConnectPendingStateEvent__9TLSAPConnFUl
// The link up: our LM connect sent (or, if the link came up the other way
// with the other LSAP known, its awaited).
void
TLSAPConn::HandleConnectPendingStateEvent(ULong event)
{
	TIrEvent* current = fCurrentEvent;
	switch (event)
	{
	case kIrConnectReply:
		if (current->fResult != noErr)
			DisconnectStart(current->fResult, nil);
		else if (current->fPassive != 0 && fPeerLSAPId != 0)
		{
			fState = kLSAPConnListen;
			GetControlFrame();
		}
		else
			PutControlFrame(kLMConnect, 0);
		break;
	case kIrPutDataReply:
		if (current->fResult != noErr)
			DisconnectStart(current->fResult, nil);
		else
		{
			StartConnectTimer();
			fState = kLSAPConnConnect;
			GetControlFrame();
		}
		break;
	case kIrDisconnectRequest:
		SaveCurrentRequest();
		PassRequestToLMP();
		break;
	case kIrDisconnectReply:
		fState = kLSAPConnDisconnected;
		if (InternalDisconnectRequest())
			ConnLstnComplete(fDisconnectReason);
		else
			fClient->EnqueueEvent(fCurrentEvent);
		break;
	}
}


// ROM 0x000f6e24 HandleConnectStateEvent__9TLSAPConnFUl
// Waiting for the other LSAP's confirm (thirty seconds).
void
TLSAPConn::HandleConnectStateEvent(ULong event)
{
	TIrEvent* current = fCurrentEvent;
	switch (event)
	{
	case kIrGetDataReply:
		StopConnectTimer();
		if (current->fResult != noErr)
			DisconnectStart(current->fResult, nil);
		else if (current->fOpCode == kLMConnect)
			DisconnectStart(kIrDAErrConnectionRace, nil);
		else if (current->fOpCode == kLMDisconnect)
			DisconnectStart(TranslateReasonToError(current->fInfo), nil);
		else if (current->fOpCode == kLMConnectConfirm)
		{
			fState = kLSAPConnDataTransferReady;
			ConnLstnComplete(noErr);
		}
		break;
	case kIrDisconnectRequest:
		StopConnectTimer();
		SaveCurrentRequest();
		PassRequestToLMP();
		break;
	case kIrDisconnectReply:
		fState = kLSAPConnDisconnected;
		if (InternalDisconnectRequest())
			ConnLstnComplete(fDisconnectReason);
		else
			fClient->EnqueueEvent(fCurrentEvent);
		break;
	case kIrOneSecTick:
		StopConnectTimer();
		DisconnectStart(kIrDAErrNonResponsiveLMMuxClient, fPendConnLstn);
		break;
	}
}


// ROM 0x000f6f40 HandleListenPendingStateEvent__9TLSAPConnFUl
void
TLSAPConn::HandleListenPendingStateEvent(ULong event)
{
	TIrEvent* current = fCurrentEvent;
	switch (event)
	{
	case kIrListenReply:
		if (current->fResult != noErr)
			DisconnectStart(current->fResult, nil);
		else
		{
			fState = kLSAPConnListen;
			GetControlFrame();
		}
		break;
	case kIrDisconnectRequest:
		SaveCurrentRequest();
		PassRequestToLMP();
		break;
	case kIrDisconnectReply:
		fState = kLSAPConnDisconnected;
		if (InternalDisconnectRequest())
			ConnLstnComplete(fDisconnectReason);
		else
			fClient->EnqueueEvent(fCurrentEvent);
		break;
	}
}


// ROM 0x000f643c HandleListenStateEvent__9TLSAPConnFUl
// Waiting for the other LSAP's LM connect: a listen is then answered (an
// accept follows), a connect whose link came up the other way confirmed.
void
TLSAPConn::HandleListenStateEvent(ULong event)
{
	TIrEvent* current = fCurrentEvent;
	switch (event)
	{
	case kIrGetDataReply:
		if (current->fResult != noErr)
			DisconnectStart(current->fResult, nil);
		else if (current->fOpCode == 0 || current->fOpCode == kLMConnectConfirm)
			PutControlFrame(kLMDisconnect, 6);
		else if (current->fOpCode == kLMConnect)
		{
			fState = kLSAPConnAccept;
			if (current->fPendingEvent == kIrListenRequest)
			{
				current->fLSAPId = fPeerLSAPId;
				ConnLstnComplete(noErr);
			}
			else
				PutControlFrame(kLMConnectConfirm, 0);
		}
		break;
	case kIrPutDataReply:
		if (current->fResult == noErr)
			GetControlFrame();
		else
			DisconnectStart(current->fResult, nil);
		break;
	case kIrDisconnectRequest:
		SaveCurrentRequest();
		PassRequestToLMP();
		break;
	case kIrDisconnectReply:
		fState = kLSAPConnDisconnected;
		if (InternalDisconnectRequest())
			ConnLstnComplete(fDisconnectReason);
		else
			fClient->EnqueueEvent(fCurrentEvent);
		break;
	}
}


// ROM 0x000f6558 HandleAcceptStateEvent__9TLSAPConnFUl
// The confirm sent with the accept's user data.
void
TLSAPConn::HandleAcceptStateEvent(ULong event)
{
	TIrEvent* current = fCurrentEvent;
	switch (event)
	{
	case kIrAcceptRequest:
		SaveCurrentRequest();
		fConnectData = current->fConnectData;
		PutControlFrame(kLMConnectConfirm, 0);
		break;
	case kIrPutDataReply:
		if (current->fPendingEvent == kIrDisconnectRequest)
			DisconnectStart(kCommErrRequestCanceled, nil);
		else if (current->fResult != noErr)
			DisconnectStart(current->fResult, nil);
		else
		{
			fState = kLSAPConnDataTransferReady;
			ConnLstnComplete(noErr);
		}
		break;
	case kIrDisconnectRequest:
		SaveCurrentRequest();
		PutControlFrame(kLMDisconnect, 1);
		break;
	case kIrDisconnectReply:
		fState = kLSAPConnDisconnected;
		ConnLstnComplete(fDisconnectReason);
		break;
	}
}


// ROM 0x000f6630 HandleDataTransferReadyStateEvent__9TLSAPConnFUl
// Connected: data both ways; a control frame received ends it.
void
TLSAPConn::HandleDataTransferReadyStateEvent(ULong event)
{
	TIrEvent* current = fCurrentEvent;
	switch (event)
	{
	case kIrGetDataRequest:
		SaveCurrentRequest();
		GetDataFrame(false);
		break;
	case kIrGetDataReply:
		if (current->fResult != noErr)
		{
			if (current->fResult == kCommErrRequestCanceled)
				fClient->EnqueueEvent(current);
			else
				DisconnectStart(current->fResult, nil);
		}
		else if (current->fOpCode == 0)
			fClient->EnqueueEvent(current);
		else if (current->fOpCode == kLMConnect)
			PutControlFrame(kLMDisconnect, 9);
		else if (current->fOpCode == kLMDisconnect)
			DisconnectStart(TranslateReasonToError(current->fInfo), nil);
		else
			GetDataFrame(true);
		break;
	case kIrPutDataRequest:
		SaveCurrentRequest();
		PutDataFrame();
		break;
	case kIrPutDataReply:
		if (!InternalPutRequest())
			fClient->EnqueueEvent(current);
		else if (current->fPendingEvent == kIrReleaseRequest || current->fPendingEvent == kIrDisconnectRequest)
			DisconnectStart(fDisconnectReason, nil);
		else
			DisconnectStart(kIrDAErrHalfOpen, nil);
		break;
	case kIrCancelGetRequest:
	case kIrCancelPutRequest:
		PassRequestToLMP();
		break;
	case kIrCancelGetReply:
	case kIrCancelPutReply:
		fClient->EnqueueEvent(fCurrentEvent);
		break;
	case kIrReleaseRequest:
	case kIrDisconnectRequest:
		SaveCurrentRequest();
		fDisconnectReason = fCurrentEvent->fResult;
		PutControlFrame(kLMDisconnect, 1);
		break;
	case kIrDisconnectReply:
		fState = kLSAPConnDisconnected;
		if (InternalDisconnectRequest())
		{
			current = fCurrentEvent;
			current->fEvent = current->fPendingEvent + 1;
			current->fResult = fDisconnectReason;
		}
		fClient->EnqueueEvent(fCurrentEvent);
		break;
	}
}


// ROM 0x000f67ec SaveCurrentRequest__9TLSAPConnFv
void
TLSAPConn::SaveCurrentRequest(void)
{
	fCurrentEvent->fPendingEvent = fCurrentEvent->fEvent;
}


// ROM 0x000f67fc InternalDisconnectRequest__9TLSAPConnFv
// The disconnect is ours, not asked for from above.
Boolean
TLSAPConn::InternalDisconnectRequest(void)
{
	return fCurrentEvent->fPendingEvent != kIrDisconnectRequest;
}


// ROM 0x000f6814 InternalPutRequest__9TLSAPConnFv
Boolean
TLSAPConn::InternalPutRequest(void)
{
	return fCurrentEvent->fPendingEvent != kIrPutDataRequest;
}


// ROM 0x000f6834 PassRequestToLMP__9TLSAPConnFv
void
TLSAPConn::PassRequestToLMP(void)
{
	fCurrentEvent->fLSAPConn = this;
	fLMP->EnqueueEvent(fCurrentEvent);
}


// ROM 0x000f6844 DisconnectStart__9TLSAPConnFlP16TIrLSAPConnEvent
void
TLSAPConn::DisconnectStart(NewtonErr reason, TIrLSAPConnEvent* event)
{
	if (event == nil)
		event = fCurrentEvent;
	event->fEvent = kIrDisconnectRequest;
	event->fLSAPConn = this;
	event->fResult = reason;
	fDisconnectReason = reason;
	fLMP->EnqueueEvent(event);
}


// ROM 0x000f686c GetControlFrame__9TLSAPConnFv
// A get of the next frame into the user data buffer.
void
TLSAPConn::GetControlFrame(void)
{
	TIrEvent* current = fCurrentEvent;
	if (fConnectData != nil)
		fConnectData->Reset();
	current->fEvent = kIrGetDataRequest;
	current->fResult = noErr;
	current->fOffset = 0;
	current->fBuffer = fConnectData;
	current->fLength = (fConnectData != nil) ? fConnectData->GetSize() : 0;
	PassRequestToLMP();
}


// ROM 0x000f691c PutControlFrame__9TLSAPConnFUcT1
// An LM control frame (a connect and its confirm carrying the user data).
void
TLSAPConn::PutControlFrame(UByte opCode, UByte info)
{
	CBuffer* data = nil;
	TIrEvent* current = fCurrentEvent;
	if (opCode == kLMConnect || opCode == kLMConnectConfirm)
		data = fConnectData;
	current->fEvent = kIrPutDataRequest;
	current->fBuffer = data;
	current->fResult = noErr;
	current->fOffset = 0;
	current->fLength = (data != nil) ? data->GetSize() : 0;
	current->fDstLSAPId = fPeerLSAPId;
	current->fSrcLSAPId = fMyLSAPId;
	current->fOpCode = opCode;
	current->fInfo = info;
	PassRequestToLMP();
}


// ROM 0x000f69a4 GetDataFrame__9TLSAPConnFUc
// A get from above, put aside while a control frame is read - or, restored,
// sent down again.
void
TLSAPConn::GetDataFrame(UByte restore)
{
	TIrEvent* current = fCurrentEvent;
	if (restore)
	{
		current->fEvent = kIrGetDataRequest;
		current->fResult = noErr;
		current->fBuffer = fGetBuffer;
		current->fOffset = fGetOffset;
		current->fLength = fGetLength;
	}
	else
	{
		fGetBuffer = current->fBuffer;
		fGetOffset = current->fOffset;
		fGetLength = current->fLength;
	}
	PassRequestToLMP();
}


// ROM 0x000f69f8 PutDataFrame__9TLSAPConnFv
void
TLSAPConn::PutDataFrame(void)
{
	TIrEvent* current = fCurrentEvent;
	current->fDstLSAPId = fPeerLSAPId;
	current->fSrcLSAPId = fMyLSAPId;
	current->fOpCode = 0;
	current->fInfo = 0;
	PassRequestToLMP();
}


// ROM 0x000f6a1c ConnLstnComplete__9TLSAPConnFl
// The connect or listen answered (in whatever block is current).
void
TLSAPConn::ConnLstnComplete(NewtonErr result)
{
	TIrEvent* current = fCurrentEvent;
	fPendConnLstn = nil;
	current->fEvent = current->fPendingEvent + 1;
	current->fResult = result;
	fClient->EnqueueEvent(current);
}


// ROM 0x000f6a98 StartConnectTimer__9TLSAPConnFv
void
TLSAPConn::StartConnectTimer(void)
{
	fTimerRunning = true;
	fTicks = 0;
	fLMP->StartOneSecTicker();
}


// ROM 0x000f6ab0 StopConnectTimer__9TLSAPConnFv
void
TLSAPConn::StopConnectTimer(void)
{
	fTimerRunning = false;
	fLMP->StopOneSecTicker();
}


// ROM 0x000f6ac0 OneSecTickerComplete__9TLSAPConnFv
// Thirty ticks: the connect timed out.
void
TLSAPConn::OneSecTickerComplete(void)
{
	if (!fTimerRunning)
		return;
	if (++fTicks < 30)
		return;
	NextState(kIrOneSecTick);
}


// ROM 0x000f6af8 TranslateReasonToError__9TLSAPConnFUc
// An LM disconnect's reason (1-10) as the IrDA error of the same name.
NewtonErr
TLSAPConn::TranslateReasonToError(UByte reason)
{
	switch (reason)
	{
	case 1:		return kIrDAErrUserRequestedDisconnect;
	case 2:		return kIrDAErrLAPUnexpectedDisconnect;
	case 3:		return kIrDAErrLAPFailedConnection;
	case 4:		return kIrDAErrLAPReset;
	case 5:		return kIrDAErrLMMuxInitiatedDisconnect;
	case 6:		return kIrDAErrDataSentOnDiscLSAPConn;
	case 7:		return kIrDAErrNonResponsiveLMMuxClient;
	case 8:		return kIrDAErrNoAvailableLMMuxClient;
	case 9:		return kIrDAErrHalfOpen;
	case 10:	return kIrDAErrIllegalSourceAddress;
	default:	return kIrDAErrProtocolError;
	}
}
