/*
	File:		comms/irda/IrLAPConn.cpp

	Contains:	TIrLAPConn (IrLAP.h).

	Reconstructed from the MP2x00 US ROM (0x000f2474-0x000f3144); each
	function cites its origin.
*/

#include "IrLAP.h"
#include "IrGlue.h"
#include "IrLMP.h"
#include "BufferSegment.h"
#include "ListIterator.h"
#include "CommErrors.h"


// ROM 0x000f2474 __ct__10TIrLAPConnFv
TIrLAPConn::TIrLAPConn()
{
	fState = kIrLAPConnStandby;
	fConnected = false;
	fPeerDevAddr = 0;
	fLSAPConns = nil;
	fGetRequests = nil;
	fPendingData = nil;
}


// ROM 0x000f24d0 __dt__10TIrLAPConnFv
TIrLAPConn::~TIrLAPConn()
{
	DeInit();
}


// ROM 0x000f2b88 Init__10TIrLAPConnFP7TIrGlueP6TIrLAP
NewtonErr
TIrLAPConn::Init(TIrGlue* glue, TIrLAP* lap)
{
	fIrGlue = glue;
	fLAP = lap;
	NewtonErr err = TIrStream::Init(glue);
	if (err == noErr)
	{
		err = -7000;
		if ((fLSAPConns = new CList) != nil
		 && (fGetRequests = new CList) != nil
		 && (fPendingData = new CList) != nil)
			return noErr;
	}
	DeInit();
	return err;
}


// ROM 0x000f2c08 Reset__10TIrLAPConnFv
void
TIrLAPConn::Reset(void)
{
	fState = kIrLAPConnStandby;
}


// ROM 0x000f2c14 DeInit__10TIrLAPConnFv
void
TIrLAPConn::DeInit(void)
{
	if (fLSAPConns != nil)
	{
		delete fLSAPConns;
		fLSAPConns = nil;
	}
	if (fGetRequests != nil)
	{
		delete fGetRequests;
		fGetRequests = nil;
	}
	if (fPendingData != nil)
	{
		delete fPendingData;
		fPendingData = nil;
	}
}


// ROM 0x000f2c74 NextState__10TIrLAPConnFUl
void
TIrLAPConn::NextState(ULong event)
{
	if (fState == kIrLAPConnStandby)
		HandleStandbyStateEvent(event);
	else if (fState == kIrLAPConnConnectOrListen)
		HandleConnectOrListenStateEvent(event);
	else if (fState == kIrLAPConnActive)
		HandleActiveStateEvent(event);
}


// ROM 0x000f2c94 HandleStandbyStateEvent__10TIrLAPConnFUl
// The first connect or listen brings the link up.
void
TIrLAPConn::HandleStandbyStateEvent(ULong event)
{
	TIrEvent* current = fCurrentEvent;
	if (event == kIrConnectRequest || event == kIrListenRequest)
	{
		fLSAPConns->InsertAt(fLSAPConns->Count(), current->fLSAPConn);
		if (event == kIrConnectRequest)
			fPeerDevAddr = current->fDevAddr;
		fState = kIrLAPConnConnectOrListen;
		fLAP->EnqueueEvent(current);
	}
	else if (event == kIrDisconnectRequest)
		fLAP->EnqueueEvent(current);
	else if (event == kIrDisconnectReply)
		current->fLSAPConn->EnqueueEvent(current);
}


// ROM 0x000f2d10 HandleConnectOrListenStateEvent__10TIrLAPConnFUl
// The link coming up: more connections wait with the first, and all are
// told how it went.
void
TIrLAPConn::HandleConnectOrListenStateEvent(ULong event)
{
	TIrEvent* current = fCurrentEvent;
	switch (event)
	{
	case kIrConnectRequest:
	case kIrListenRequest:
		fLSAPConns->InsertAt(fLSAPConns->Count(), current->fLSAPConn);
		break;
	case kIrConnectReply:
	case kIrListenReply:
		{
			CListIterator iter(fLSAPConns);
			for (TLSAPConn* lsapConn = (TLSAPConn*) iter.FirstItem(); iter.More(); lsapConn = (TLSAPConn*) iter.NextItem())
			{
				TIrLSAPConnEvent* pending = lsapConn->GetPendConnLstn();
				if (pending->fEvent == kIrConnectRequest)
					pending->fEvent = kIrConnectReply;
				else if (pending->fEvent == kIrListenRequest)
					pending->fEvent = kIrListenReply;
				pending->fResult = current->fResult;
				pending->fDevAddr = current->fDevAddr;
				pending->fPassive = !fLAP->fPrimary;
				lsapConn->EnqueueEvent(pending);
			}
		}
		if (current->fResult == noErr)
		{
			fPeerDevAddr = current->fDevAddr;
			fConnected = true;
			fState = kIrLAPConnActive;
		}
		else
		{
			for (ArrayIndex i = fLSAPConns->Count() - 1; i >= 0; i--)
			{
				TLSAPConn* lsapConn = (TLSAPConn*) fLSAPConns->At(i);
				fLSAPConns->Remove(lsapConn);
				CleanupPendingGetRequestsAndReplies(lsapConn, current->fResult);
			}
			fPeerDevAddr = 0;
			fState = kIrLAPConnStandby;
		}
		break;
	case kIrDisconnectRequest:
		{
			NewtonErr removed = fLSAPConns->Remove(current->fLSAPConn);
			CleanupPendingGetRequestsAndReplies(current->fLSAPConn, current->fResult);
			// the last connection gone while the stack disconnects: the
			// link too; otherwise the connection alone
			if (removed == noErr && fLSAPConns->Count() == 0 && fIrGlue->fDisconnectState != 0)
				fLAP->EnqueueEvent(current);
			else
			{
				current->fEvent = kIrDisconnectReply;
				current->fLSAPConn->EnqueueEvent(current);
			}
		}
		break;
	case kIrDisconnectReply:
		fPeerDevAddr = 0;
		fConnected = false;
		fState = kIrLAPConnStandby;
		current->fLSAPConn->EnqueueEvent(current);
		break;
	}
}


// ROM 0x000f2f1c HandleActiveStateEvent__10TIrLAPConnFUl
// The link up: a new connection is answered at once.
void
TIrLAPConn::HandleActiveStateEvent(ULong event)
{
	TIrEvent* current = fCurrentEvent;
	switch (event)
	{
	case kIrConnectRequest:
	case kIrListenRequest:
		fLSAPConns->InsertAt(fLSAPConns->Count(), current->fLSAPConn);
		current->fEvent = (event == kIrConnectRequest) ? kIrConnectReply : kIrListenReply;
		current->fDevAddr = fPeerDevAddr;
		current->fPassive = !fLAP->fPrimary;
		current->fLSAPConn->EnqueueEvent(current);
		break;
	case kIrGetDataRequest:
		HandleGetDataRequest();
		break;
	case kIrCancelGetRequest:
		CancelPendingGetRequests(current->fLSAPConn, kCommErrRequestCanceled);
		current->fEvent = kIrCancelGetReply;
		current->fResult = noErr;
		current->fLSAPConn->EnqueueEvent(current);
		break;
	case kIrDisconnectRequest:
		{
			NewtonErr removed = fLSAPConns->Remove(current->fLSAPConn);
			CleanupPendingGetRequestsAndReplies(current->fLSAPConn, current->fResult);
			if (removed == noErr && fLSAPConns->Count() == 0 && fIrGlue->fDisconnectState != 0)
				fLAP->EnqueueEvent(current);
			else
			{
				current->fEvent = kIrDisconnectReply;
				current->fLSAPConn->EnqueueEvent(current);
			}
		}
		break;
	case kIrDisconnectReply:
		fPeerDevAddr = 0;
		fConnected = false;
		fState = kIrLAPConnStandby;
		current->fLSAPConn->EnqueueEvent(current);
		break;
	}
}


// ROM 0x000f3064 HandleGetDataRequest__10TIrLAPConnFv
// A frame waiting for this connection delivered, or the get kept until
// one comes.
void
TIrLAPConn::HandleGetDataRequest(void)
{
	Boolean delivered = false;				// (never set in the ROM)
	TIrDataXferEvent* request = fCurrentEvent;
	CListIterator iter(fPendingData);
	for (CBufferSegment* buffer = (CBufferSegment*) iter.FirstItem(); iter.More(); buffer = (CBufferSegment*) iter.NextItem())
	{
		TLMPDUHeader header;
		ULong headerLength;
		ExtractHeader(buffer, header, headerLength);
		if (DataDelivered(request, header, headerLength, buffer))
		{
			fPendingData->Remove(buffer);
			return;
		}
	}
	if (!delivered)
		fGetRequests->InsertAt(fGetRequests->Count(), request);
}


// ROM 0x000f2698 Demultiplexor__10TIrLAPConnFP14CBufferSegment
// A frame received: to the get waiting for it, else kept for the
// connection it is for; a frame for no connection answered with a
// disconnect (an access mode request refused).
void
TIrLAPConn::Demultiplexor(CBufferSegment* buffer)
{
	TLMPDUHeader header;
	ULong headerLength;
	Boolean taken = false;
	Boolean valid = ExtractHeader(buffer, header, headerLength);
	if (!valid || (header.fOpCode & 0x7f) == 3)
	{
		fLAP->ReleaseInputBuffer(buffer);
		if (valid && header.fOpCode == 3 && header.fMore[0] <= 1)
			ReplyToInvalidFrame(header, 0x83, 0xff);
		return;
	}
	{
		CListIterator iter(fGetRequests);
		for (TIrDataXferEvent* request = (TIrDataXferEvent*) iter.FirstItem(); iter.More(); request = (TIrDataXferEvent*) iter.NextItem())
		{
			if (DataDelivered(request, header, headerLength, buffer))
			{
				fGetRequests->Remove(request);
				taken = true;
				break;
			}
		}
	}
	if (!taken)
	{
		CListIterator iter(fLSAPConns);
		for (TLSAPConn* lsapConn = (TLSAPConn*) iter.FirstItem(); iter.More(); lsapConn = (TLSAPConn*) iter.NextItem())
		{
			if (lsapConn->YourData(header, true))
			{
				fPendingData->InsertAt(fPendingData->Count(), buffer);
				taken = true;
				break;
			}
		}
	}
	if (taken)
		return;
	fLAP->ReleaseInputBuffer(buffer);
	UByte reason;
	if (header.fOpCode == 0)
		reason = 6;							// data on a disconnected LSAP
	else if (header.fOpCode == 1)
		reason = 8;							// no LSAP to connect to
	else
		reason = 1;
	ReplyToInvalidFrame(header, 2, reason);
}


// ROM 0x000f2880 ReplyToInvalidFrame__10TIrLAPConnFR12TLMPDUHeaderUcT2
// A control frame back to where the frame came from, for no connection.
void
TIrLAPConn::ReplyToInvalidFrame(TLMPDUHeader& header, UByte opCode, UByte info)
{
	TIrDataXferEvent* event = fIrGlue->GrabEventBlock(kIrPutDataRequest, 0x1c);
	if (event == nil)
		return;
	event->fLSAPConn = nil;
	event->fBuffer = nil;
	event->fOffset = 0;
	event->fLength = 0;
	event->fDstLSAPId = header.fSrcLSAPId | 0x80;
	event->fSrcLSAPId = header.fDstLSAPId & 0x7f;
	event->fOpCode = opCode;
	event->fInfo = info;
	fLAP->EnqueueEvent(event);
}


// ROM 0x000f28f4 ExtractHeader__10TIrLAPConnFP14CBufferSegmentR12TLMPDUHeaderRUl
// The LMPDU header at the frame's start: two bytes for data, four for a
// connect, its confirm or a disconnect, all of it for an access mode
// frame; LSAPs above 0x6f are not valid.
Boolean
TIrLAPConn::ExtractHeader(CBufferSegment* buffer, TLMPDUHeader& header, ULong& length)
{
	buffer->Seek(0, kSeekFromBeginning);
	ULong count = buffer->Getn((UByte*) &header, 8);
	if (count < 2)
		return false;
	if (header.fDstLSAPId & 0x80)
	{
		header.fDstLSAPId &= 0x7f;
		if (count == 2)
			return false;
		if (count == 3)
			header.fInfo = 0;
		switch (header.fOpCode)
		{
		case 3:
		case 0x83:
			break;
		case 1:
		case 2:
		case 0x81:
			if ((Long) count >= 4)
				count = 4;
			break;
		default:
			return false;
		}
	}
	else
	{
		count = 2;
		header.fOpCode = 0;
		header.fInfo = 0;
	}
	if (header.fDstLSAPId <= 0x6f && header.fSrcLSAPId <= 0x6f)
	{
		length = count;
		return true;
	}
	return false;
}


// ROM 0x000f29e0 DataDelivered__10TIrLAPConnFP16TIrDataXferEventR12TLMPDUHeaderUlP14CBufferSegment
// A frame for the connection a get is from: its data (after the header)
// into the get's buffer at the get's offset, and the get answered.
Boolean
TIrLAPConn::DataDelivered(TIrDataXferEvent* request, TLMPDUHeader& header, ULong headerLength, CBufferSegment* buffer)
{
	if (!request->fLSAPConn->YourData(header, false))
		return false;
	Size copied = 0;
	Long available = (buffer->fBufLimit - buffer->fBufStart) - headerLength;
	if (request->fBuffer != nil && available > 0)
	{
		request->fBuffer->Seek(request->fOffset, kSeekFromBeginning);
		copied = request->fBuffer->Putn(buffer->fBufStart + headerLength, available);
	}
	request->fEvent = kIrGetDataReply;
	request->fLength = copied;
	request->fResult = noErr;
	request->fOpCode = header.fOpCode;
	request->fInfo = header.fInfo;
	request->fLSAPConn->EnqueueEvent(request);
	fLAP->ReleaseInputBuffer(buffer);
	return true;
}


// ROM 0x000f2ac0 FillInLMPDUHeader__10TIrLAPConnFP16TIrDataXferEventPUc
// ==> its length: two bytes for data, four for control, five for an access
// mode confirm.
ULong
TIrLAPConn::FillInLMPDUHeader(TIrDataXferEvent* event, UByte* buffer)
{
	buffer[0] = event->fDstLSAPId;
	buffer[1] = event->fSrcLSAPId;
	if (event->fOpCode == 0)
		return 2;
	buffer[0] |= 0x80;
	buffer[2] = event->fOpCode;
	buffer[3] = event->fInfo;
	if (event->fOpCode == 0x83)
	{
		buffer[4] = 0;
		return 5;
	}
	return 4;
}


// ROM 0x000f2518 CleanupPendingGetRequestsAndReplies__10TIrLAPConnFP9TLSAPConnl
// A connection gone: its gets answered and its frames thrown away.
void
TIrLAPConn::CleanupPendingGetRequestsAndReplies(TLSAPConn* lsapConn, NewtonErr result)
{
	CancelPendingGetRequests(lsapConn, result);
	if (fPendingData == nil)
		return;
	CListIterator iter(fPendingData);
	for (CBufferSegment* buffer = (CBufferSegment*) iter.FirstItem(); iter.More(); buffer = (CBufferSegment*) iter.NextItem())
	{
		TLMPDUHeader header;
		ULong headerLength;
		ExtractHeader(buffer, header, headerLength);
		if (lsapConn->YourData(header, true))
		{
			fPendingData->Remove(buffer);
			fLAP->ReleaseInputBuffer(buffer);
		}
	}
}


// ROM 0x000f25dc CancelPendingGetRequests__10TIrLAPConnFP9TLSAPConnl
void
TIrLAPConn::CancelPendingGetRequests(TLSAPConn* lsapConn, NewtonErr result)
{
	if (fGetRequests == nil)
		return;
	CListIterator iter(fGetRequests);
	for (TIrDataXferEvent* request = (TIrDataXferEvent*) iter.FirstItem(); iter.More(); request = (TIrDataXferEvent*) iter.NextItem())
	{
		if (request->fLSAPConn == lsapConn)
		{
			fGetRequests->Remove(request);
			request->fEvent = kIrGetDataReply;
			request->fResult = (result != noErr) ? result : kCommErrRequestCanceled;
			lsapConn->EnqueueEvent(request);
		}
	}
}


// ROM 0x000f2b1c TimerComplete__10TIrLAPConnFUl
// The one-second tick, to every connection.
void
TIrLAPConn::TimerComplete(ULong kind)
{
	CListIterator iter(fLSAPConns);
	for (TLSAPConn* lsapConn = (TLSAPConn*) iter.FirstItem(); iter.More(); lsapConn = (TLSAPConn*) iter.NextItem())
		lsapConn->OneSecTickerComplete();
}
