/*
	File:		comms/SerialEndpoint.cpp

	Contains:	TSerialEndpoint and its parameter blocks (SerialEndpoint.h).

	Reconstructed from the MP2x00 US ROM (0x001d9968-0x001dd574); each
	function cites its origin.
*/

#include "SerialEndpoint.h"
#include "BufferList.h"
#include "List.h"
#include "AppWorld.h"
#include "NewtonMemory.h"
#include "NewtErrors.h"
#include "OSErrors.h"
#include "CommErrors.h"

// an address given to Listen, Accept or Connect: not supported (-36001)
#define kEndpointErrNoAddresses		(-36001)


// ---------------------------------------------------------------------------
//	The parameter blocks
// ---------------------------------------------------------------------------

// ROM 0x001d9970 __ct__11TCommToolPBFUlT1Uc
TCommToolPB::TCommToolPB(ULong requestType, ULong clientRefCon, Boolean async)
{
	Init(true);
	fClientRefCon = clientRefCon;
	fRequestType = requestType;
	fAsync = async;
}


// ROM 0x001dcec4 __ct__18TCommToolControlPBFUllT1Uc
TCommToolControlPB::TCommToolControlPB(ULong opCode, Long eventCode, ULong clientRefCon, Boolean async)
	: TCommToolPB(kCommToolRequestTypeControl, clientRefCon, async)
{
	fEvent.fEventCode = eventCode;
	fRequest.fOpCode = opCode;
}


// ROM 0x001dd504 __ct__15TCommToolBindPBFUlT1Ucl
TCommToolBindPB::TCommToolBindPB(ULong opCode, ULong clientRefCon, Boolean async, Long eventCode)
	: TCommToolPB(kCommToolRequestTypeControl, clientRefCon, async)
{
	fRequest.fOpCode = opCode;
	fEvent.fEventCode = eventCode;
}


// ROM 0x001d9a38 __ct__18TCommToolConnectPBFUllT1Uc
TCommToolConnectPB::TCommToolConnectPB(ULong opCode, Long eventCode, ULong clientRefCon, Boolean async)
	: TCommToolPB(kCommToolRequestTypeControl, clientRefCon, async)
{
	fEvent.fEventCode = eventCode;
	fRequest.fOpCode = opCode;
}


// ROM 0x001d9aac Prepare__18TCommToolConnectPBFP12TOptionArrayP14CBufferSegmentPlUc
// The request's options (none when the array is empty), data and sequence;
// for an asynchronous call the event carries the same for the client.
NewtonErr
TCommToolConnectPB::Prepare(TOptionArray* options, CBufferSegment* data, Long* sequence, Boolean sync)
{
	fRequest.fOutside = false;
	fRequest.fOptions = nil;
	fRequest.fOptionCount = 0;
	fRequest.fData = nil;
	fRequest.fSequence = 0;
	if (sequence != nil)
		fRequest.fSequence = *sequence;
	if (options != nil && options->GetArrayCount() > 0)
	{
		fRequest.fOptions = options;
		fRequest.fOptionCount = options->GetArrayCount();
	}
	if (data != nil)
		fRequest.fData = data;
	if (!sync)
	{
		fEvent.fError = noErr;
		fEvent.fOptions = options;
		fEvent.fData = data;
		fEvent.fAddr = nil;
		fEvent.fSequence = 0;
		fAsync = true;
	}
	else
		fAsync = false;
	return noErr;
}


// ROM 0x001d99c8 __ct__21TCommToolDisconnectPBFUlUc
TCommToolDisconnectPB::TCommToolDisconnectPB(ULong clientRefCon, Boolean async)
	: TCommToolPB(kCommToolRequestTypeControl, clientRefCon, async)
{
	fRequest.fOpCode = kCommToolDisconnect;
	fEvent.fEventCode = kEndpointEventDisconnectComplete;
}


// ROM 0x001d9b38 __ct__18TCommToolOptMgmtPBFUlUc
TCommToolOptMgmtPB::TCommToolOptMgmtPB(ULong clientRefCon, Boolean async)
	: TCommToolPB(kCommToolRequestTypeControl, clientRefCon, async)
{
	fRequest.fOpCode = kCommToolOptionMgmt;
	fEvent.fEventCode = kEndpointEventOptMgmtComplete;
}


// ROM 0x001da1cc __ct__16TCommToolAbortPBFUlT1Uc
TCommToolAbortPB::TCommToolAbortPB(ULong requestsToKill, ULong clientRefCon, Boolean async)
	: TCommToolPB(kCommToolRequestTypeKill, clientRefCon, async)
{
	fRequest.fRequestsToKill = (CommToolRequestType) requestsToKill;
}


// ROM 0x001d9e64 __ct__16TCommToolEventPBFUl
TCommToolEventPB::TCommToolEventPB(ULong clientRefCon)
	: TCommToolPB(kCommToolRequestTypeGetEvent, clientRefCon, true)
{
}


// ROM 0x001da690 __ct__14TCommToolPutPBFUlUc
TCommToolPutPB::TCommToolPutPB(ULong clientRefCon, Boolean async)
	: TCommToolPB(kCommToolRequestTypePut, clientRefCon, async)
{
	fEvent.fData = nil;
	fEvent.fBuffer = nil;
	fEvent.fOptions = nil;
	fEvent.fCount = 0;
	fSegment = nil;
	fList = nil;
}


// ROM 0x001db730 __dt__14TCommToolPutPBFv
TCommToolPutPB::~TCommToolPutPB()
{
	if (fSegment != nil)
		delete fSegment;
	if (fList != nil)
		delete fList;
}


// ROM 0x001dbfe0 __ct__14TCommToolGetPBFUlUc
TCommToolGetPB::TCommToolGetPB(ULong clientRefCon, Boolean async)
	: TCommToolPB(kCommToolRequestTypeGet, clientRefCon, async)
{
	fEvent.fData = nil;
	fEvent.fBuffer = nil;
	fEvent.fOptions = nil;
	fEvent.fCount = 0;
	fEvent.fFlags = 0;
	fSegment = nil;
	fList = nil;
	fFramed = false;
}


// ROM 0x001dc68c __dt__14TCommToolGetPBFv
TCommToolGetPB::~TCommToolGetPB()
{
	if (fSegment != nil)
		delete fSegment;
	if (fList != nil)
		delete fList;
}


// ---------------------------------------------------------------------------
//	TSerialEndpoint: making and deleting
// ---------------------------------------------------------------------------

PROTOCOL_IMPL_SOURCE_MACRO(TSerialEndpoint)		// ROM 0x001d9968 Sizeof__15TSerialEndpointSFv
PROTOCOL_CLASSINFO(TSerialEndpoint, "TEndpoint", "", 0, 0, nil)	// ROM 0x00383360 ClassInfo__15TSerialEndpointSFv


// ROM 0x001d9ba8 New__15TSerialEndpointFv
TSerialEndpoint*
TSerialEndpoint::New()
{
	fPending = nil;
	fPutPBs = nil;
	fGetPBs = nil;
	fEventPB = nil;
	fDeferredDisconnect = nil;
	fAbortSync = nil;
	fEventSync = nil;
	fInSyncCall = false;
	fWaitingForEvent = false;
	fAborting = false;
	fSyncAbort = false;
	fToolIsRunning = false;
	fEventHandler = nil;
	fInfo = nil;
	return this;
}


// ROM 0x001d9be8 Delete__15TSerialEndpointFv
// (The tool is closed first, if it is still running.)
void
TSerialEndpoint::Delete()
{
	NukePending();
	if (fToolIsRunning)
	{
		fState = kUnbnd;
		fAborting = false;
		fInSyncCall = false;
		Close();
	}
	if (fGetPBs != nil)
		NukeGetPBList();
	if (fPutPBs != nil)
		NukePutPBList();
	if (fEventPB != nil)
		delete fEventPB;
	if (fAbortSync != nil)
		delete fAbortSync;
	if (fEventSync != nil)
		delete fEventSync;
	if (fEventHandler != nil)
		delete fEventHandler;
	if (fInfo != nil)
		delete fInfo;
}


// ROM 0x001d9c90 DeleteLeavingTool__15TSerialEndpointFv
// Let go of the tool without closing it: whatever is in flight aborted, the
// event request killed.
TObjectId
TSerialEndpoint::DeleteLeavingTool()
{
	NewtonErr err = noErr;
	if (fAborting)
		return kEndpointErrAborting;
	if (fState == kUninit)
		return kEndpointErrBadState;
	if ((!IsPending(kEitherCall) || (err = nAbort(true)) == noErr)
	&&  (!fToolIsRunning || (err = KillKillKill(kCommToolRequestTypeGetEvent, fEventPB)) == noErr))
	{
		fInSyncCall = false;
		fToolIsRunning = false;
		Delete();
	}
	// (the ROM answers the error where TEndpoint's answers the port)
	return err;
}


// ROM 0x001d9d34 HandleEvent__15TSerialEndpointFUlP7TAEventT1
Boolean
TSerialEndpoint::HandleEvent(ULong msgType, TAEvent* event, ULong msgSize)
{
	return false;
}


// ROM 0x001d9d3c HandleComplete__15TSerialEndpointFP10TUMsgTokenPUlP7TAEvent
// A reply to an asynchronous call: the event request's, or one on the
// pending list (its timer killed) - to the Handle...Reply for its kind.
Boolean
TSerialEndpoint::HandleComplete(TUMsgToken* msgToken, ULong* msgSize, TAEvent* event)
{
	if (fEventPB->GetMsgId() == msgToken->GetMsgId())
		HandleEventReply(fEventPB);
	else
	{
		TCommToolPB* pb = nil;
		Boolean found = false;
		for (ArrayIndex i = 0; i < fPending->GetArraySize(); i++)
		{
			pb = (TCommToolPB*) fPending->At(i);
			if (pb->GetMsgId() == msgToken->GetMsgId())
			{
				fPending->RemoveElementsAt(i, 1);
				found = true;
				break;
			}
		}
		if (found)
		{
			fEventHandler->KillTimer((ULong) pb);
			switch (pb->fRequestType)
			{
			case kCommToolRequestTypeGet:
				HandleGetReply((TCommToolGetPB*) pb);
				break;
			case kCommToolRequestTypePut:
				HandlePutReply((TCommToolPutPB*) pb);
				break;
			case kCommToolRequestTypeControl:
				HandleControlReply((TCommToolControlPB*) pb);
				break;
			case kCommToolRequestTypeKill:
				HandleAbortReply((TCommToolAbortPB*) pb);
				break;
			}
		}
	}
	return false;
}


// ROM 0x001d9e58 AddToAppWorld__15TSerialEndpointFv
NewtonErr
TSerialEndpoint::AddToAppWorld()
{
	return kEndpointErrNotSupported;
}


// ROM 0x001d9eb8 RemoveFromAppWorld__15TSerialEndpointFv
NewtonErr
TSerialEndpoint::RemoveFromAppWorld()
{
	return kEndpointErrNotSupported;
}


// ---------------------------------------------------------------------------
//	Open and close
// ---------------------------------------------------------------------------

// ROM 0x001d9ec4 Open__15TSerialEndpointFUl
// The lists made, and the event request posted.
NewtonErr
TSerialEndpoint::Open(ULong clientHandler)
{
	if (fState != kUninit)
		return kEndpointErrBadState;
	if (IsPending(kSyncCall))
		return kEndpointErrPending;
	if (fAborting)
		return kEndpointErrAborting;
	fState = kInFlux;
	NewtonErr err;
	if ((err = InitPending()) == noErr
	&&  (err = InitGetPBList()) == noErr
	&&  (err = InitPutPBList()) == noErr)
	{
		fEventPB = new TCommToolEventPB(0);
		if (fEventPB == nil)
			err = MemError();
		else
		{
			fClientRefCon = clientHandler;
			err = PostEventRequest(fEventPB);
		}
	}
	fState = (err == noErr) ? kUnbnd : kUninit;
	return err;
}


// ROM 0x001d9f98 Close__15TSerialEndpointFv
// The event request killed and the tool closed (a close request), the
// lists given back.
NewtonErr
TSerialEndpoint::Close()
{
	NewtonErr err = noErr;
	if ((ULong) fState > kUnbnd)
		return kEndpointErrBadState;
	if (IsPending(kEitherCall))
		return kEndpointErrPending;
	if (fAborting)
		return kEndpointErrAborting;
	if (fToolIsRunning)
	{
		if (fState != kUninit)
			KillKillKill(kCommToolRequestTypeGetEvent, fEventPB);
		TCommToolControlPB close(kCommToolClose, 0, 0, false);
		fInSyncCall = true;
		err = fEventHandler->CallService(kCommToolRequestTypeControl, &close, &close.fRequest, sizeof(close.fRequest), &close.fReply, sizeof(close.fReply), 0, 0, true);
		fInSyncCall = false;
		fToolIsRunning = false;
		if (err == noErr)
			err = close.fReply.fResult;
	}
	fState = kUninit;
	NukePending();
	NukeGetPBList();
	NukePutPBList();
	if (fEventPB != nil)
		delete fEventPB;
	fEventPB = nil;
	return err;
}


// ROM 0x001da0ec Abort__15TSerialEndpointFv
NewtonErr
TSerialEndpoint::Abort()
{
	return nAbort(true);
}


// ROM 0x001da0f4 SetSync__15TSerialEndpointFUc
// (It will not go synchronous while asynchronous calls are pending.)
Boolean
TSerialEndpoint::SetSync(Boolean sync)
{
	Boolean was = fSync;
	if (was != sync && (!sync || !IsPending(kAsyncCall)))
		fSync = sync;
	return was;
}


// ---------------------------------------------------------------------------
//	The 1.x calls: the 2.0 ones, synchronous as the endpoint is
// ---------------------------------------------------------------------------

// ROM 0x001da140 GetProtAddr__15TSerialEndpointFP12TOptionArrayT1Ul
NewtonErr
TSerialEndpoint::GetProtAddr(TOptionArray* bndAddr, TOptionArray* peerAddr, TTimeout timeOut)
{
	return kEndpointErrNotSupported;
}


// ROM 0x001da14c OptMgmt__15TSerialEndpointFUlP12TOptionArrayT1
NewtonErr
TSerialEndpoint::OptMgmt(ULong arrayOpCode, TOptionArray* options, TTimeout timeOut)
{
	return nOptMgmt(arrayOpCode, options, timeOut, fSync);
}


// ROM 0x001da170 Bind__15TSerialEndpointFP12TOptionArrayPlUl
// (The queue length is not used.)
NewtonErr
TSerialEndpoint::Bind(TOptionArray* addr, Long* qlen, TTimeout timeOut)
{
	return nBind(addr, timeOut, fSync);
}


// ROM 0x001da17c UnBind__15TSerialEndpointFUl
NewtonErr
TSerialEndpoint::UnBind(TTimeout timeOut)
{
	return nUnBind(timeOut, fSync);
}


// ROM 0x001da184 Listen__15TSerialEndpointFP12TOptionArrayT1P14CBufferSegmentPlUl
NewtonErr
TSerialEndpoint::Listen(TOptionArray* addr, TOptionArray* opt, CBufferSegment* data, Long* seq, TTimeout timeOut)
{
	if (addr != nil)
		return kEndpointErrNoAddresses;
	return nListen(opt, data, seq, timeOut, fSync);
}


// ROM 0x001da22c Accept__15TSerialEndpointFP9TEndpointP12TOptionArrayT2P14CBufferSegmentlUl
NewtonErr
TSerialEndpoint::Accept(TEndpoint* resfd, TOptionArray* addr, TOptionArray* opt, CBufferSegment* data, Long seq, TTimeout timeOut)
{
	if (addr != nil)
		return kEndpointErrNoAddresses;
	return nAccept(resfd, opt, data, seq, timeOut, fSync);
}


// ROM 0x001da27c Connect__15TSerialEndpointFP12TOptionArrayT1P14CBufferSegmentPlUl
NewtonErr
TSerialEndpoint::Connect(TOptionArray* addr, TOptionArray* opt, CBufferSegment* data, Long* seq, TTimeout timeOut)
{
	if (addr != nil)
		return kEndpointErrNoAddresses;
	return nConnect(opt, data, seq, timeOut, fSync);
}


// ROM 0x001da2c4 Disconnect__15TSerialEndpointFP14CBufferSegmentlT2
// (It makes the endpoint synchronous for good.)
NewtonErr
TSerialEndpoint::Disconnect(CBufferSegment* data, Long reason, Long seq)
{
	fSync = true;
	return nDisconnect(data, reason, seq, 0, true);
}


// ROM 0x001da2fc Release__15TSerialEndpointFUl
NewtonErr
TSerialEndpoint::Release(TTimeout timeOut)
{
	return nRelease(timeOut, fSync);
}


// ROM 0x001da304 Snd__15TSerialEndpointFPUcRlUlT3
NewtonErr
TSerialEndpoint::Snd(UByte* buf, Size& nBytes, ULong flags, TTimeout timeOut)
{
	Size count = nBytes;
	NewtonErr err = nSnd(buf, &count, flags, timeOut, fSync, nil);
	nBytes = count;
	return err;
}


// ROM 0x001da354 Snd__15TSerialEndpointFP14CBufferSegmentUlT2
NewtonErr
TSerialEndpoint::Snd(CBufferSegment* buf, ULong flags, TTimeout timeOut)
{
	return nSnd(buf, flags, timeOut, fSync, nil);
}


// ROM 0x001da384 Rcv__15TSerialEndpointFPUcRllPUlUl
NewtonErr
TSerialEndpoint::Rcv(UByte* buf, Size& nBytes, Size thresh, ULong* flags, TTimeout timeOut)
{
	Size count = nBytes;
	NewtonErr err = nRcv(buf, &count, thresh, flags, timeOut, fSync, nil);
	nBytes = count;
	return err;
}


// ROM 0x001da3dc Rcv__15TSerialEndpointFP14CBufferSegmentlPUlUl
NewtonErr
TSerialEndpoint::Rcv(CBufferSegment* buf, Size thresh, ULong* flags, TTimeout timeOut)
{
	return nRcv(buf, thresh, flags, timeOut, fSync, nil);
}


// ROM 0x001da418 WaitForEvent__15TSerialEndpointFUl
// (HandleEventReply unblocks it.  The ROM goes on to Init a TPseudoSyncState
// it could not make.)
NewtonErr
TSerialEndpoint::WaitForEvent(TTimeout timeOut)
{
	fWaitingForEvent = true;
	if (fEventSync == nil)
	{
		fEventSync = new TPseudoSyncState;
		NewtonErr err = fEventSync->Init();
		if (err != noErr)
			return err;
	}
	NewtonErr err = fEventSync->Block(timeOut);
	fWaitingForEvent = false;
	return err;
}


// ---------------------------------------------------------------------------
//	The 2.0 calls
// ---------------------------------------------------------------------------

// ROM 0x001da47c nBind__15TSerialEndpointFP12TOptionArrayUlUc
NewtonErr
TSerialEndpoint::nBind(TOptionArray* opt, TTimeout timeOut, Boolean sync)
{
	if (fState != kUnbnd)
		return kEndpointErrBadState;
	if (sync && IsPending(kSyncCall))
		return kEndpointErrPending;
	if (fAborting)
		return kEndpointErrAborting;
	fState = kInFlux;
	NewtonErr err;
	TCommToolBindPB* pb = new TCommToolBindPB(kCommToolBind, fClientRefCon, !sync, kEndpointEventBindComplete);
	if (pb == nil)
		err = MemError();
	else
	{
		if (opt == nil || opt->GetArrayCount() < 1)
		{
			pb->fRequest.fOptions = nil;
			pb->fRequest.fOptionCount = 0;
		}
		else
		{
			pb->fRequest.fOptions = opt;
			pb->fRequest.fOutside = false;
			pb->fRequest.fOptionCount = opt->GetArrayCount();
			pb->fRequest.fCopyBack = true;
		}
		if (!sync)
			pb->SetCollectorPort(*((TAppWorld*) GetGlobals())->GetMyPort());
		else
			fInSyncCall = true;
		err = fEventHandler->CallService(kCommToolRequestTypeControl, pb, &pb->fRequest, sizeof(pb->fRequest), &pb->fReply, sizeof(pb->fReply), timeOut, (ULong) pb, sync);
		if (err == kError_Message_Timed_Out)
			KillKillKill(kCommToolRequestTypeControl, pb);
	}
	if (!sync)
	{
		if (err == noErr)
			fPending->InsertAt(fPending->GetArraySize(), pb);
		else
		{
			if (pb != nil)
				delete pb;
			fState = kUnbnd;
		}
	}
	else
	{
		if (err == noErr)
			err = pb->fReply.fResult;
		fState = (err == noErr) ? kIdle : kUnbnd;
		if (!fInSyncCall)
		{
			// aborted meanwhile: undone
			if (err == noErr)
			{
				nUnBind(0, true);
				err = kError_Call_Aborted;
			}
		}
		else
			fInSyncCall = false;
		if (pb != nil)
			delete pb;
	}
	return err;
}


// ROM 0x001db238 nUnBind__15TSerialEndpointFUlUc
NewtonErr
TSerialEndpoint::nUnBind(TTimeout timeOut, Boolean sync)
{
	if (fState != kIdle)
		return kEndpointErrBadState;
	if (sync && IsPending(kSyncCall))
		return kEndpointErrPending;
	if (fAborting)
		return kEndpointErrAborting;
	fState = kInFlux;
	NewtonErr err;
	TCommToolBindPB* pb = new TCommToolBindPB(kCommToolUnbind, fClientRefCon, !sync, kEndpointEventUnBindComplete);
	if (pb == nil)
		err = MemError();
	else
	{
		if (!sync)
			pb->SetCollectorPort(*((TAppWorld*) GetGlobals())->GetMyPort());
		else
			fInSyncCall = true;
		err = fEventHandler->CallService(kCommToolRequestTypeControl, pb, &pb->fRequest, sizeof(pb->fRequest), &pb->fReply, sizeof(pb->fReply), timeOut, (ULong) pb, sync);
		if (err == kError_Message_Timed_Out)
			KillKillKill(kCommToolRequestTypeControl, pb);
	}
	if (!sync)
	{
		if (err == noErr)
			fPending->InsertAt(fPending->GetArraySize(), pb);
		else
		{
			if (pb != nil)
				delete pb;
			fState = kIdle;
		}
	}
	else
	{
		if (err == noErr)
			err = pb->fReply.fResult;
		fState = (err == noErr) ? kUnbnd : kIdle;
		if (fInSyncCall)
			fInSyncCall = false;
		if (pb != nil)
			delete pb;
	}
	return err;
}


// ROM 0x001da70c nListen__15TSerialEndpointFP12TOptionArrayP14CBufferSegmentPlUlUc
NewtonErr
TSerialEndpoint::nListen(TOptionArray* opt, CBufferSegment* data, Long* seq, TTimeout timeOut, Boolean sync)
{
	if (fState != kIdle)
		return kEndpointErrBadState;
	if (sync && IsPending(kSyncCall))
		return kEndpointErrPending;
	if (fAborting)
		return kEndpointErrAborting;
	fState = kInListen;
	NewtonErr err;
	TCommToolConnectPB* pb = new TCommToolConnectPB(kCommToolListen, kEndpointEventListenComplete, fClientRefCon, !sync);
	if (pb == nil)
		err = MemError();
	else if ((err = pb->Prepare(opt, data, seq, sync)) == noErr)
	{
		if (!sync)
			pb->SetCollectorPort(*((TAppWorld*) GetGlobals())->GetMyPort());
		else
			fInSyncCall = true;
		err = fEventHandler->CallService(kCommToolRequestTypeControl, pb, &pb->fRequest, sizeof(pb->fRequest), &pb->fReply, sizeof(pb->fReply), timeOut, (ULong) pb, sync);
		if (err == kError_Message_Timed_Out)
			KillKillKill(kCommToolRequestTypeControl, pb);
	}
	if (!sync)
	{
		if (err == noErr)
			fPending->InsertAt(fPending->GetArraySize(), pb);
		else
		{
			if (pb != nil)
				delete pb;
			fState = kIdle;
		}
	}
	else
	{
		if (err == noErr)
			err = pb->fReply.fResult;
		if (err == noErr)
		{
			if (seq != nil)
				*seq = pb->fReply.fSequence;
			fState = kInCon;
		}
		else
			fState = kIdle;
		if (!fInSyncCall)
		{
			if (err == noErr)
			{
				nDisconnect(nil, 0, 0, 0, true);
				err = kError_Call_Aborted;
			}
		}
		else
			fInSyncCall = false;
		if (pb != nil)
			delete pb;
	}
	return err;
}


// ROM 0x001da948 nAccept__15TSerialEndpointFP9TEndpointP12TOptionArrayP14CBufferSegmentlUlUc
// (Only on the listening endpoint itself.)
NewtonErr
TSerialEndpoint::nAccept(TEndpoint* resfd, TOptionArray* opt, CBufferSegment* data, Long seq, TTimeout timeOut, Boolean sync)
{
	if (fState != kInCon)
		return kEndpointErrBadState;
	if (sync && IsPending(kSyncCall))
		return kEndpointErrPending;
	if (fAborting)
		return kEndpointErrAborting;
	if (resfd != this)
		return kEndpointErrBadEndpoint;
	NewtonErr err;
	TCommToolConnectPB* pb = new TCommToolConnectPB(kCommToolAccept, kEndpointEventAcceptComplete, fClientRefCon, !sync);
	if (pb == nil)
		err = MemError();
	else if ((err = pb->Prepare(opt, data, &seq, sync)) == noErr)
	{
		if (!sync)
			pb->SetCollectorPort(*((TAppWorld*) GetGlobals())->GetMyPort());
		else
			fInSyncCall = true;
		// (the ROM's reply size here is 0x10, the ConnectReply's 0x14 less its sequence)
		err = fEventHandler->CallService(kCommToolRequestTypeControl, pb, &pb->fRequest, sizeof(pb->fRequest), &pb->fReply, sizeof(TCommToolReply), timeOut, (ULong) pb, sync);
		if (err == kError_Message_Timed_Out)
			KillKillKill(kCommToolRequestTypeControl, pb);
	}
	if (!sync)
	{
		if (err == noErr)
			fPending->InsertAt(fPending->GetArraySize(), pb);
		else
		{
			if (pb != nil)
				delete pb;
			fState = kIdle;
		}
	}
	else
	{
		if (err == noErr)
			err = pb->fReply.fResult;
		fState = (err == noErr) ? kDataXfer : kIdle;
		if (!fInSyncCall)
		{
			if (err == noErr)
			{
				nDisconnect(nil, 0, 0, 0, true);
				err = kError_Call_Aborted;
			}
		}
		else
			fInSyncCall = false;
		if (pb != nil)
			delete pb;
	}
	return err;
}


// ROM 0x001dab88 nConnect__15TSerialEndpointFP12TOptionArrayP14CBufferSegmentPlUlUc
NewtonErr
TSerialEndpoint::nConnect(TOptionArray* opt, CBufferSegment* data, Long* seq, TTimeout timeOut, Boolean sync)
{
	if (fState != kIdle)
		return kEndpointErrBadState;
	if (sync && IsPending(kSyncCall))
		return kEndpointErrPending;
	if (fAborting)
		return kEndpointErrAborting;
	fState = kOutCon;
	NewtonErr err;
	TCommToolConnectPB* pb = new TCommToolConnectPB(kCommToolConnect, kEndpointEventConnectComplete, fClientRefCon, !sync);
	if (pb == nil)
		err = MemError();
	else if ((err = pb->Prepare(opt, data, seq, sync)) == noErr)
	{
		if (!sync)
			pb->SetCollectorPort(*((TAppWorld*) GetGlobals())->GetMyPort());
		else
			fInSyncCall = true;
		// (the ROM's reply size here is 0x10, the ConnectReply's 0x14 less its sequence)
		err = fEventHandler->CallService(kCommToolRequestTypeControl, pb, &pb->fRequest, sizeof(pb->fRequest), &pb->fReply, sizeof(TCommToolReply), timeOut, (ULong) pb, sync);
		if (err == kError_Message_Timed_Out)
			KillKillKill(kCommToolRequestTypeControl, pb);
	}
	if (!sync)
	{
		if (err == noErr)
		{
			fPending->InsertAt(fPending->GetArraySize(), pb);
			return noErr;
		}
		fState = kIdle;
	}
	else
	{
		if (err == noErr)
			err = pb->fReply.fResult;
		fState = (err == noErr) ? kDataXfer : kIdle;
		if (!fInSyncCall)
		{
			if (err == noErr)
			{
				nDisconnect(nil, 0, 0, 0, true);
				err = kError_Call_Aborted;
			}
		}
		else
			fInSyncCall = false;
	}
	if (pb != nil)
		delete pb;
	return err;
}


// ROM 0x001dad98 PrepDisconnect__15TSerialEndpointFP14CBufferSegmentlT2Uc
// (A failed allocation with MemError noErr answers the nil anyway.)
TCommToolDisconnectPB*
TSerialEndpoint::PrepDisconnect(CBufferSegment* data, Long reason, Long seq, Boolean sync)
{
	TCommToolDisconnectPB* pb = new TCommToolDisconnectPB(fClientRefCon, !sync);
	if (pb == nil)
	{
		MemError();
		return nil;
	}
	pb->fRequest.fOutside = false;
	pb->fRequest.fSequence = seq;
	pb->fRequest.fReason = reason;
	pb->fRequest.fDisconnectData = data;
	return pb;
}


// ROM 0x001dae24 SendDisconnect__15TSerialEndpointFP21TCommToolDisconnectPBUlUc
NewtonErr
TSerialEndpoint::SendDisconnect(TCommToolDisconnectPB* pb, TTimeout timeOut, Boolean sync)
{
	fState = kInFlux;
	if (!sync)
		pb->SetCollectorPort(*((TAppWorld*) GetGlobals())->GetMyPort());
	else
		fInSyncCall = true;
	NewtonErr err = fEventHandler->CallService(kCommToolRequestTypeControl, pb, &pb->fRequest, sizeof(pb->fRequest), &pb->fReply, sizeof(pb->fReply), timeOut, (ULong) pb, sync);
	if (err == kError_Message_Timed_Out)
		KillKillKill(kCommToolRequestTypeControl, pb);
	if (!sync)
	{
		if (err == noErr)
		{
			fPending->InsertAt(fPending->GetArraySize(), pb);
			return noErr;
		}
		fState = kDataXfer;
		return err;
	}
	if (err == noErr)
		err = pb->fReply.fResult;
	if (err == kCommErrNotConnected)
	{
		err = kEndpointErrBadState;
		fState = kIdle;
	}
	else if (err != noErr)
		fState = kDataXfer;
	else
		fState = kIdle;
	if (fInSyncCall)
		fInSyncCall = false;
	return err;
}


// ROM 0x001daf3c nDisconnect__15TSerialEndpointFP14CBufferSegmentlT2UlUc
// Whatever is pending aborted first; an asynchronous disconnect that had to
// abort waits for the abort's reply (fDeferredDisconnect, its timeout kept
// in the reply's result).
NewtonErr
TSerialEndpoint::nDisconnect(CBufferSegment* data, Long reason, Long seq, TTimeout timeOut, Boolean sync)
{
	Boolean deferred = false;
	if (fState != kDataXfer && fState != kInRel && fState != kOutRel && fState != kInCon)
		return kEndpointErrBadState;
	if (fAborting)
		return kEndpointErrAborting;
	NewtonErr err;
	if (IsPending(kEitherCall))
	{
		deferred = !sync;
		if ((err = nAbort(sync)) != noErr)
			return err;
	}
	err = noErr;
	TCommToolDisconnectPB* pb = PrepDisconnect(data, reason, seq, sync);
	if (pb == nil)
		err = MemError();
	else if (deferred)
	{
		fDeferredDisconnect = pb;
		pb->fReply.fResult = timeOut;
	}
	else
	{
		err = SendDisconnect(pb, timeOut, sync);
		if (sync || err != noErr)
			delete pb;
	}
	return err;
}


// ROM 0x001db05c nRelease__15TSerialEndpointFUlUc
NewtonErr
TSerialEndpoint::nRelease(TTimeout timeOut, Boolean sync)
{
	if (fState != kDataXfer && fState != kInRel)
		return kEndpointErrBadState;
	if (sync && IsPending(kSyncCall))
		return kEndpointErrPending;
	if (fAborting)
		return kEndpointErrAborting;
	NewtonErr err;
	TCommToolControlPB* pb = new TCommToolControlPB(kCommToolRelease, kEndpointEventReleaseComplete, fClientRefCon, !sync);
	if (pb == nil)
		err = MemError();
	else
	{
		fState = kOutRel;
		if (!sync)
			pb->SetCollectorPort(*((TAppWorld*) GetGlobals())->GetMyPort());
		else
			fInSyncCall = true;
		err = fEventHandler->CallService(kCommToolRequestTypeControl, pb, &pb->fRequest, sizeof(pb->fRequest), &pb->fReply, sizeof(pb->fReply), timeOut, (ULong) pb, sync);
		if (err == kError_Message_Timed_Out)
			KillKillKill(kCommToolRequestTypeControl, pb);
	}
	if (!sync)
	{
		if (err == noErr)
			fPending->InsertAt(fPending->GetArraySize(), pb);
		else
		{
			if (pb != nil)
				delete pb;
			fState = kDataXfer;
		}
	}
	else
	{
		if (err == noErr)
			err = pb->fReply.fResult;
		if (err == kCommErrNotConnected || err == noErr)
		{
			err = (err == kCommErrNotConnected) ? kEndpointErrBadState : noErr;
			fState = kIdle;
		}
		else
			fState = kDataXfer;
		if (fInSyncCall)
			fInSyncCall = false;
		if (pb != nil)
			delete pb;
	}
	return err;
}


// ROM 0x001db3f0 nOptMgmt__15TSerialEndpointFUlP12TOptionArrayT1Uc
NewtonErr
TSerialEndpoint::nOptMgmt(ULong arrayOpCode, TOptionArray* options, TTimeout timeOut, Boolean sync)
{
	if (fState == kUninit)
		return kEndpointErrBadState;
	if (sync && IsPending(kSyncCall))
		return kEndpointErrPending;
	if (fAborting)
		return kEndpointErrAborting;
	if (options == nil || options->GetArrayCount() == 0)
		return kEndpointErrNoOptions;
	NewtonErr err;
	TCommToolOptMgmtPB* pb = new TCommToolOptMgmtPB(0, !sync);
	if (pb == nil)
		err = MemError();
	else
	{
		pb->fRequest.fOptions = options;
		pb->fRequest.fOutside = false;
		pb->fRequest.fOptionCount = options->GetArrayCount();
		pb->fRequest.fRequestOpCode = arrayOpCode;
		pb->fRequest.fCopyBack = true;
		if (!sync)
		{
			pb->fEvent.fError = noErr;
			pb->fEvent.fClient = fClientRefCon;
			pb->fEvent.fEventCode = kEndpointEventOptMgmtComplete;
			pb->fEvent.fOptions = options;
			pb->SetCollectorPort(*((TAppWorld*) GetGlobals())->GetMyPort());
		}
		else
			fInSyncCall = true;
		err = fEventHandler->CallService(kCommToolRequestTypeControl, pb, &pb->fRequest, sizeof(pb->fRequest), &pb->fReply, sizeof(pb->fReply), timeOut, (ULong) pb, sync);
		if (err == kError_Message_Timed_Out)
			KillKillKill(kCommToolRequestTypeControl, pb);
	}
	if (!sync)
	{
		if (err == noErr)
		{
			fPending->InsertAt(fPending->GetArraySize(), pb);
			return noErr;
		}
	}
	else
	{
		if (err == noErr)
			err = pb->fReply.fResult;
		if (!fInSyncCall)
		{
			if (err == noErr)
				err = kError_Call_Aborted;
		}
		else
			fInSyncCall = false;
	}
	if (pb != nil)
		delete pb;
	return err;
}


// ROM 0x001db5d8 nSnd__15TSerialEndpointFPUcPlUlT3UcP12TOptionArray
// The bytes put through a segment over them.
NewtonErr
TSerialEndpoint::nSnd(UByte* buf, Size* count, ULong flags, TTimeout timeOut, Boolean sync, TOptionArray* opt)
{
	if (fState != kDataXfer)
		return kEndpointErrBadState;
	if (sync && IsPending(kSyncCall))
		return kEndpointErrPending;
	if (fAborting)
		return kEndpointErrAborting;
	if (*count == 0)
		return noErr;
	NewtonErr err;
	TCommToolPutPB* pb = GrabPutPB(true);
	if (pb == nil)
		return kEndpointErrNoPBs;
	if (!sync)
	{
		pb->fAsync = true;
		pb->fEvent.fData = nil;
		pb->fEvent.fBuffer = buf;
		pb->fEvent.fOptions = opt;
	}
	else
		pb->fAsync = false;
	if ((err = pb->fSegment->Init(buf, *count, false, 0, -1)) == noErr
	&&  (err = pb->fList->InsertLast(pb->fSegment)) == noErr)
		err = SendBytes(pb, count, flags, timeOut, sync, opt);
	if (sync || err != noErr)
		ReleasePutPB(pb);
	return err;
}


// ROM 0x001db794 nSnd__15TSerialEndpointFP14CBufferSegmentUlT2UcP12TOptionArray
// ROM BUG: the PB's fAsync is not set here, so an asynchronous send of a
// segment tells the client only if the PB last served an asynchronous call;
// kept.
NewtonErr
TSerialEndpoint::nSnd(CBufferSegment* buf, ULong flags, TTimeout timeOut, Boolean sync, TOptionArray* opt)
{
	if (fState != kDataXfer)
		return kEndpointErrBadState;
	if (sync && IsPending(kSyncCall))
		return kEndpointErrPending;
	if (fAborting)
		return kEndpointErrAborting;
	if (buf->GetSize() == 0)
		return noErr;
	NewtonErr err;
	TCommToolPutPB* pb = GrabPutPB(false);
	if (pb == nil)
		return kEndpointErrNoPBs;
	pb->fEvent.fBuffer = nil;
	pb->fEvent.fData = buf;
	pb->fEvent.fOptions = opt;
	Size count = buf->GetSize();
	if ((err = pb->fList->InsertLast(buf)) == noErr)
		err = SendBytes(pb, &count, flags, timeOut, sync, opt);
	if (sync || err != noErr)
		ReleasePutPB(pb);
	return err;
}


// ROM 0x001db8cc nRcv__15TSerialEndpointFPUcPllPUlUlUcP12TOptionArray
NewtonErr
TSerialEndpoint::nRcv(UByte* buf, Size* count, Size thresh, ULong* flags, TTimeout timeOut, Boolean sync, TOptionArray* opt)
{
	if (fState != kDataXfer)
		return kEndpointErrBadState;
	if (sync && IsPending(kSyncCall))
		return kEndpointErrPending;
	if (fAborting)
		return kEndpointErrAborting;
	if (*count == 0)
	{
		*flags = 0;
		return noErr;
	}
	NewtonErr err;
	TCommToolGetPB* pb = GrabGetPB(true);
	if (pb == nil)
		return kEndpointErrNoPBs;
	if (!sync)
	{
		pb->fAsync = true;
		pb->fEvent.fData = nil;
		pb->fEvent.fBuffer = buf;
		pb->fEvent.fOptions = opt;
	}
	else
		pb->fAsync = false;
	if ((err = pb->fSegment->Init(buf, *count, false, 0, -1)) == noErr
	&&  (err = pb->fList->InsertLast(pb->fSegment)) == noErr)
		err = RecvBytes(pb, count, thresh, flags, timeOut, sync, opt);
	if (sync || err != noErr)
		ReleaseGetPB(pb);
	return err;
}


// ROM 0x001dba2c nRcv__15TSerialEndpointFP14CBufferSegmentlPUlUlUcP12TOptionArray
// (A synchronous receive into a segment hides what did not come.)
NewtonErr
TSerialEndpoint::nRcv(CBufferSegment* buf, Size thresh, ULong* flags, TTimeout timeOut, Boolean sync, TOptionArray* opt)
{
	if (fState != kDataXfer)
		return kEndpointErrBadState;
	if (sync && IsPending(kSyncCall))
		return kEndpointErrPending;
	if (fAborting)
		return kEndpointErrAborting;
	if (buf->GetSize() == 0)
	{
		*flags = 0;
		return noErr;
	}
	NewtonErr err;
	TCommToolGetPB* pb = GrabGetPB(false);
	if (pb == nil)
		return kEndpointErrNoPBs;
	if (!sync)
	{
		pb->fAsync = true;
		pb->fEvent.fData = buf;
		pb->fEvent.fBuffer = nil;
		pb->fEvent.fOptions = opt;
	}
	else
		pb->fAsync = false;
	Size count = buf->GetSize();
	if ((err = pb->fList->InsertLast(buf)) == noErr
	&&  (err = RecvBytes(pb, &count, thresh, flags, timeOut, sync, opt), sync)
	&&  err == noErr)
	{
		Size size = buf->GetSize();
		if (count < size)
			buf->Hide(size - count, kSeekFromEnd);
		buf->Seek(0, kSeekFromBeginning);
	}
	if (sync || err != noErr)
		ReleaseGetPB(pb);
	return err;
}


// ROM 0x001dbbe0 nAbort__15TSerialEndpointFUc
NewtonErr
TSerialEndpoint::nAbort(Boolean sync)
{
	return PrepareAbort(kCommToolRequestTypeGet | kCommToolRequestTypePut | kCommToolRequestTypeControl, sync);
}


// ROM 0x001dcb54 PrepareAbort__15TSerialEndpointFUlUc
// Kill the requests: asynchronously, a kill request whose reply tells the
// client; synchronously, a synchronous call in progress is killed at once
// (KillKillKill), and asynchronous ones by a kill request whose reply this
// waits for.  (nAbort is this in line in the ROM.)
NewtonErr
TSerialEndpoint::PrepareAbort(ULong requestsToKill, Boolean sync)
{
	if (fAborting)
		return kEndpointErrAborting;
	NewtonErr err = noErr;
	if (!sync)
		err = PostKillRequest(requestsToKill, true);
	else if (!IsPending(kSyncCall) || IsPending(kAsyncCall))
	{
		if (IsPending(kEitherCall))
		{
			if (fAbortSync == nil)
			{
				fAbortSync = new TPseudoSyncState;
				if (fAbortSync == nil)
					return MemError();
				if ((err = fAbortSync->Init()) != noErr)
					return err;
			}
			err = PostKillRequest(requestsToKill, false);
			if (err == noErr)
			{
				fSyncAbort = true;
				err = fAbortSync->Block(0);
			}
		}
	}
	else
	{
		fAborting = true;
		err = KillKillKill(requestsToKill, nil);
		fAborting = false;
		fInSyncCall = false;
	}
	return err;
}


// ROM 0x001dbc00 Timeout__15TSerialEndpointFUl
// A call's timer went off: the call, if still pending, killed.
NewtonErr
TSerialEndpoint::Timeout(ULong refCon)
{
	for (ArrayIndex i = 0; i < fPending->GetArraySize(); i++)
	{
		TCommToolPB* pb = (TCommToolPB*) fPending->At(i);
		if ((ULong) pb == refCon)
			return KillKillKill(pb->fRequestType, nil);
	}
	return noErr;
}


// ROM 0x001dbc74 IsPending__15TSerialEndpointFUl
Boolean
TSerialEndpoint::IsPending(ULong which)
{
	Boolean pending = false;
	if (which & kSyncCall)
		pending = fInSyncCall;
	if (which & kAsyncCall)
		pending |= (fPending != nil && fPending->GetArraySize() > 0);
	return pending;
}


// ROM 0x001dbcb8 SetState__15TSerialEndpointFl
NewtonErr
TSerialEndpoint::SetState(Long state)
{
	if (fState != kUnbnd)
		return kEndpointErrBadState;
	if (state >= 0 && state < 9)
	{
		fState = state;
		return noErr;
	}
	return kCommErrBadParameter;
}


// ---------------------------------------------------------------------------
//	Sending and receiving
// ---------------------------------------------------------------------------

// ROM 0x001dc6f0 SendBytes__15TSerialEndpointFP14TCommToolPutPBPlUlT3UcP12TOptionArray
// The put request: framed if flags has 2, the end of the frame unless flags
// has 1 (more to come).
NewtonErr
TSerialEndpoint::SendBytes(TCommToolPutPB* pb, Size* count, ULong flags, TTimeout timeOut, Boolean sync, TOptionArray* opt)
{
	pb->fRequest.fOutside = false;
	if (opt == nil || opt->GetArrayCount() < 1)
	{
		pb->fRequest.fOptions = nil;
		pb->fRequest.fOptionCount = 0;
	}
	else
	{
		pb->fRequest.fOptions = opt;
		pb->fRequest.fOptionCount = opt->GetArrayCount();
	}
	pb->fRequest.fValidCount = -1;
	pb->fRequest.fData = pb->fList;
	pb->fRequest.fFrameData = (flags & 2) != 0;
	pb->fRequest.fEndOfFrame = (flags & 1) == 0;
	if (!sync)
		pb->SetCollectorPort(*((TAppWorld*) GetGlobals())->GetMyPort());
	else
		fInSyncCall = true;
	NewtonErr err = fEventHandler->CallService(kCommToolRequestTypePut, pb, &pb->fRequest, sizeof(pb->fRequest), &pb->fReply, sizeof(pb->fReply), timeOut, (ULong) pb, sync);
	if (err == kError_Message_Timed_Out)
		KillKillKill(kCommToolRequestTypePut, pb);
	if (!sync)
	{
		if (err == noErr)
		{
			fPending->InsertAt(fPending->GetArraySize(), pb);
			return noErr;
		}
	}
	else
	{
		if (err == noErr)
			err = pb->fReply.fResult;
		if (err == noErr)
			*count = pb->fReply.fPutBytesCount;
		if (!fInSyncCall)
		{
			if (err == noErr)
			{
				err = kError_Call_Aborted;
				goto failed;
			}
		}
		else
			fInSyncCall = false;
	}
	if (err == noErr)
		return noErr;
failed:
	*count = 0;
	return err;
}


// ROM 0x001dc860 RecvBytes__15TSerialEndpointFP14TCommToolGetPBPllPUlUlUcP12TOptionArray
// The get request: framed (the whole frame) if *flags has 2, otherwise
// non-blocking with the threshold; what comes back says whether there is
// more of the frame (flags 1).
NewtonErr
TSerialEndpoint::RecvBytes(TCommToolGetPB* pb, Size* count, Size thresh, ULong* flags, TTimeout timeOut, Boolean sync, TOptionArray* opt)
{
	pb->fRequest.fOutside = false;
	if (opt == nil || opt->GetArrayCount() < 1)
	{
		pb->fRequest.fOptions = nil;
		pb->fRequest.fOptionCount = 0;
	}
	else
	{
		pb->fRequest.fOptions = opt;
		pb->fRequest.fOptionCount = opt->GetArrayCount();
	}
	Boolean framed = (*flags & 2) != 0;
	pb->fFramed = framed;
	pb->fRequest.fData = pb->fList;
	if (framed)
	{
		pb->fRequest.fFrameData = true;
		pb->fRequest.fThreshold = 0;
		pb->fRequest.fNonBlocking = false;
	}
	else
	{
		pb->fRequest.fFrameData = false;
		pb->fRequest.fThreshold = thresh;
		pb->fRequest.fNonBlocking = true;
	}
	if (!sync)
		pb->SetCollectorPort(*((TAppWorld*) GetGlobals())->GetMyPort());
	else
		fInSyncCall = true;
	NewtonErr err = fEventHandler->CallService(kCommToolRequestTypeGet, pb, &pb->fRequest, sizeof(pb->fRequest), &pb->fReply, sizeof(pb->fReply), timeOut, (ULong) pb, sync);
	if (err == kError_Message_Timed_Out)
		KillKillKill(kCommToolRequestTypeGet, pb);
	if (!sync)
	{
		if (err == noErr)
			fPending->InsertAt(fPending->GetArraySize(), pb);
	}
	else
	{
		if (err == noErr)
			err = pb->fReply.fResult;
		if (err == noErr)
		{
			*count = pb->fReply.fGetBytesCount;
			*flags = framed ? 2 : 0;
			if (!pb->fReply.fEndOfFrame)
				*flags |= 1;
		}
		if (!fInSyncCall)
		{
			if (err == noErr)
				err = kError_Call_Aborted;
		}
		else
			fInSyncCall = false;
	}
	if (err != noErr)
		*count = 0;
	return err;
}


// ROM 0x001dd25c eWorldSnd__15TSerialEndpointFP14CBufferSegmentUlT2UcP12TOptionArray
// nSnd of a segment through eWorldSendBytes.  (The ROM does not test for an
// empty segment's early return the way nSnd does: an empty one is sent.)
NewtonErr
TSerialEndpoint::eWorldSnd(CBufferSegment* buf, ULong flags, TTimeout timeOut, Boolean sync, TOptionArray* opt)
{
	if (fState != kDataXfer)
		return kEndpointErrBadState;
	if (sync && IsPending(kSyncCall))
		return kEndpointErrPending;
	if (fAborting)
		return kEndpointErrAborting;
	if (buf->GetSize() == 0)
		return noErr;
	NewtonErr err;
	TCommToolPutPB* pb = GrabPutPB(false);
	if (pb == nil)
		return kEndpointErrNoPBs;
	pb->fEvent.fBuffer = nil;
	pb->fEvent.fData = buf;
	pb->fEvent.fOptions = opt;
	Size count = buf->GetSize();
	if ((err = pb->fList->InsertLast(buf)) == noErr)
		err = eWorldSendBytes(pb, &count, flags, timeOut, sync, opt);
	if (sync || err != noErr)
		ReleasePutPB(pb);
	return err;
}


// ROM 0x001dd394 eWorldSendBytes__15TSerialEndpointFP14TCommToolPutPBPlUlT3UcP12TOptionArray
// SendBytes without forking the world.
NewtonErr
TSerialEndpoint::eWorldSendBytes(TCommToolPutPB* pb, Size* count, ULong flags, TTimeout timeOut, Boolean sync, TOptionArray* opt)
{
	pb->fRequest.fOutside = false;
	if (opt == nil || opt->GetArrayCount() < 1)
	{
		pb->fRequest.fOptions = nil;
		pb->fRequest.fOptionCount = 0;
	}
	else
	{
		pb->fRequest.fOptions = opt;
		pb->fRequest.fOptionCount = opt->GetArrayCount();
	}
	pb->fRequest.fValidCount = -1;
	pb->fRequest.fData = pb->fList;
	pb->fRequest.fFrameData = (flags & 2) != 0;
	pb->fRequest.fEndOfFrame = (flags & 1) == 0;
	if (!sync)
		pb->SetCollectorPort(*((TAppWorld*) GetGlobals())->GetMyPort());
	else
		fInSyncCall = true;
	NewtonErr err = fEventHandler->CallServiceNoForks(kCommToolRequestTypePut, pb, &pb->fRequest, sizeof(pb->fRequest), &pb->fReply, sizeof(pb->fReply), timeOut, (ULong) pb, sync);
	if (err == kError_Message_Timed_Out)
		KillKillKill(kCommToolRequestTypePut, pb);
	if (!sync)
	{
		if (err == noErr)
		{
			fPending->InsertAt(fPending->GetArraySize(), pb);
			return noErr;
		}
	}
	else
	{
		if (err == noErr)
			err = pb->fReply.fResult;
		if (err == noErr)
			*count = pb->fReply.fPutBytesCount;
		if (!fInSyncCall)
		{
			if (err == noErr)
			{
				err = kError_Call_Aborted;
				goto failed;
			}
		}
		else
			fInSyncCall = false;
	}
	if (err == noErr)
		return noErr;
failed:
	*count = 0;
	return err;
}


// ---------------------------------------------------------------------------
//	The replies to asynchronous calls
// ---------------------------------------------------------------------------

// ROM 0x001dbcf4 HandlePutReply__15TSerialEndpointFP14TCommToolPutPB
// The client told of an asynchronous send: the PB's event (0x30 bytes)
// with the reply's error and the count of bytes put.
void
TSerialEndpoint::HandlePutReply(TCommToolPutPB* pb)
{
	if (fClientRefCon != 0 && pb->fAsync)
	{
		TSndCompleteEvent event = pb->fEvent;
		event.fError = pb->fReply.fResult;
		event.fCount = pb->fReply.fPutBytesCount;
		event.fClient = fClientRefCon;
		event.fEventCode = kEndpointEventSndComplete;
		ReleasePutPB(pb);
		ULong size = sizeof(event);
		((TEndpointClient*) fClientRefCon)->AEHandlerProc(nil, &size, &event);
		return;
	}
	ReleasePutPB(pb);
}


// ROM 0x001dbdc0 HandleGetReply__15TSerialEndpointFP14TCommToolGetPB
// A receive into the client's segment hides what did not come; the client
// is told the count, and whether the frame goes on.
void
TSerialEndpoint::HandleGetReply(TCommToolGetPB* pb)
{
	CBufferSegment* segment = pb->fEvent.fData;
	if (segment != nil && pb->fReply.fResult == noErr)
	{
		ULong count = pb->fReply.fGetBytesCount;
		ULong size = segment->GetSize();
		if (count < size)
			segment->Hide(size - count, kSeekFromEnd);
		segment->Seek(0, kSeekFromBeginning);
	}
	if (fClientRefCon != 0 && pb->fAsync)
	{
		TRcvCompleteEvent event = pb->fEvent;
		event.fError = pb->fReply.fResult;
		event.fClient = fClientRefCon;
		event.fEventCode = kEndpointEventRcvComplete;
		event.fCount = pb->fReply.fGetBytesCount;
		event.fFlags = pb->fFramed ? 2 : 0;
		if (!pb->fReply.fEndOfFrame)
			event.fFlags |= 1;
		ReleaseGetPB(pb);
		ULong size = sizeof(event);
		((TEndpointClient*) fClientRefCon)->AEHandlerProc(nil, &size, &event);
		return;
	}
	ReleaseGetPB(pb);
}


// ROM 0x001dbf24 HandleControlReply__15TSerialEndpointFP18TCommToolControlPB
void
TSerialEndpoint::HandleControlReply(TCommToolControlPB* pb)
{
	switch (pb->fRequest.fOpCode)
	{
	case kCommToolConnect:
		HandleConnectReply((TCommToolConnectPB*) pb);
		break;
	case kCommToolListen:
		HandleListenReply((TCommToolConnectPB*) pb);
		break;
	case kCommToolAccept:
		HandleAcceptReply((TCommToolConnectPB*) pb);
		break;
	case kCommToolDisconnect:
		HandleDisconnectReply((TCommToolDisconnectPB*) pb);
		break;
	case kCommToolRelease:
		HandleReleaseReply(pb);
		break;
	case kCommToolBind:
		HandleBindReply((TCommToolBindPB*) pb);
		break;
	case kCommToolUnbind:
		HandleUnBindReply((TCommToolBindPB*) pb);
		break;
	case kCommToolOptionMgmt:
		HandleOptMgmtReply((TCommToolOptMgmtPB*) pb);
		break;
	}
	delete pb;
}


// ROM 0x001dc064 HandleEventReply__15TSerialEndpointFP16TCommToolEventPB
// The tool's event: a disconnect makes the endpoint idle, a release puts it
// in kInRel; the event request posted again, the client told (as a default
// event), and a WaitForEvent let go.
void
TSerialEndpoint::HandleEventReply(TCommToolEventPB* pb)
{
	if (pb->fReply.fResult != kCommErrRequestCanceled)
	{
		TDefaultEvent event;
		ULong size = 0;
		if (pb->fReply.fEventCode == 2)
			fState = kIdle;
		else if (pb->fReply.fEventCode == 3)
			fState = kInRel;
		if (fClientRefCon != 0)
		{
			size = 0x24;
			event.fTime.time.lo = pb->fReply.fEventTime.time.lo;
			event.fData = pb->fReply.fServiceId;
		}
		PostEventRequest(pb);
		TEndpointClient* client = (TEndpointClient*) fClientRefCon;
		if (client != nil)
			client->AEHandlerProc(nil, &size, &event);
	}
	if (fWaitingForEvent)
	{
		TUnblockEvent unblock(fEventSync);
		((TForkWorld*) GetGlobals())->ReleaseMutex();
		fEventSync->Send(&unblock, sizeof(unblock));
		((TForkWorld*) GetGlobals())->AcquireMutex();
	}
}


// ROM 0x001dc158 HandleAbortReply__15TSerialEndpointFP16TCommToolAbortPB
// A kill request's reply: a synchronous abort let go; else a deferred
// disconnect sent now; else the client told.
void
TSerialEndpoint::HandleAbortReply(TCommToolAbortPB* pb)
{
	fAborting = false;
	fInSyncCall = false;
	if (!fSyncAbort)
	{
		TCommToolDisconnectPB* disconnect = fDeferredDisconnect;
		if (disconnect == nil)
		{
			if (fClientRefCon != 0 && pb->fAsync)
			{
				TEndpointEvent event;
				event.fError = pb->fReply.fResult;
				event.fClient = fClientRefCon;
				event.fEventCode = kEndpointEventAbortComplete;
				ULong size = 0x20;
				((TEndpointClient*) fClientRefCon)->AEHandlerProc(nil, &size, &event);
			}
		}
		else
		{
			NewtonErr err = pb->fReply.fResult;
			if (err == noErr)
				err = SendDisconnect(disconnect, disconnect->fReply.fResult, false);
			if (err != noErr)
			{
				fDeferredDisconnect->fReply.fResult = err;
				disconnect = fDeferredDisconnect;
				HandleDisconnectReply(disconnect);
				if (disconnect != nil)
					delete disconnect;
			}
		}
	}
	else
	{
		fSyncAbort = false;
		fAbortSync->Unblock();
	}
	if (pb != nil)
		delete pb;
}


// ROM 0x001dc298 HandleOptMgmtReply__15TSerialEndpointFP18TCommToolOptMgmtPB
void
TSerialEndpoint::HandleOptMgmtReply(TCommToolOptMgmtPB* pb)
{
	if (fClientRefCon != 0 && pb->fAsync)
	{
		pb->fEvent.fError = pb->fReply.fResult;
		ULong size = 0x24;
		((TEndpointClient*) fClientRefCon)->AEHandlerProc(nil, &size, &pb->fEvent);
	}
}


// ROM 0x001dc2ec HandleConnectReply__15TSerialEndpointFP18TCommToolConnectPB
void
TSerialEndpoint::HandleConnectReply(TCommToolConnectPB* pb)
{
	if (pb->fReply.fResult == noErr)
		fState = kDataXfer;
	else if (fState == kOutCon)
		fState = kIdle;
	if (fClientRefCon != 0 && pb->fAsync)
	{
		pb->fEvent.fError = pb->fReply.fResult;
		pb->fEvent.fSequence = pb->fReply.fSequence;
		ULong size = 0x30;
		((TEndpointClient*) fClientRefCon)->AEHandlerProc(nil, &size, &pb->fEvent);
	}
}


// ROM 0x001dc36c HandleListenReply__15TSerialEndpointFP18TCommToolConnectPB
void
TSerialEndpoint::HandleListenReply(TCommToolConnectPB* pb)
{
	if (pb->fReply.fResult == noErr)
		fState = kInCon;
	else if (fState == kInListen)
		fState = kIdle;
	if (fClientRefCon != 0 && pb->fAsync)
	{
		pb->fEvent.fError = pb->fReply.fResult;
		pb->fEvent.fSequence = pb->fReply.fSequence;
		ULong size = 0x30;
		((TEndpointClient*) fClientRefCon)->AEHandlerProc(nil, &size, &pb->fEvent);
	}
}


// ROM 0x001dc3ec HandleAcceptReply__15TSerialEndpointFP18TCommToolConnectPB
void
TSerialEndpoint::HandleAcceptReply(TCommToolConnectPB* pb)
{
	if (pb->fReply.fResult == noErr)
		fState = kDataXfer;
	else if (fState == kInCon)
		fState = kIdle;
	if (fClientRefCon != 0 && pb->fAsync)
	{
		pb->fEvent.fError = pb->fReply.fResult;
		pb->fEvent.fSequence = pb->fReply.fSequence;
		ULong size = 0x30;
		((TEndpointClient*) fClientRefCon)->AEHandlerProc(nil, &size, &pb->fEvent);
	}
}


// ROM 0x001dc46c HandleReleaseReply__15TSerialEndpointFP18TCommToolControlPB
void
TSerialEndpoint::HandleReleaseReply(TCommToolControlPB* pb)
{
	Long state = kIdle;
	if (pb->fReply.fResult != noErr)
	{
		if (pb->fReply.fResult == kCommErrNotConnected)
			pb->fReply.fResult = kEndpointErrBadState;
		else
		{
			if (fState != kOutRel)
				goto tell;
			state = kDataXfer;
		}
	}
	fState = state;
tell:
	if (fClientRefCon != 0 && pb->fAsync)
	{
		pb->fEvent.fError = pb->fReply.fResult;
		ULong size = 0x20;
		((TEndpointClient*) fClientRefCon)->AEHandlerProc(nil, &size, &pb->fEvent);
	}
}


// ROM 0x001dc4fc HandleDisconnectReply__15TSerialEndpointFP21TCommToolDisconnectPB
void
TSerialEndpoint::HandleDisconnectReply(TCommToolDisconnectPB* pb)
{
	if (fDeferredDisconnect != nil)
		fDeferredDisconnect = nil;
	Long state = kIdle;
	if (pb->fReply.fResult != noErr)
	{
		if (pb->fReply.fResult == kCommErrNotConnected)
			pb->fReply.fResult = kEndpointErrBadState;
		else
		{
			if (fState != kInFlux)
				goto tell;
			state = kDataXfer;
		}
	}
	fState = state;
tell:
	if (fClientRefCon != 0 && pb->fAsync)
	{
		pb->fEvent.fError = pb->fReply.fResult;
		ULong size = 0x2c;
		((TEndpointClient*) fClientRefCon)->AEHandlerProc(nil, &size, &pb->fEvent);
	}
}


// ROM 0x001dc59c HandleBindReply__15TSerialEndpointFP15TCommToolBindPB
void
TSerialEndpoint::HandleBindReply(TCommToolBindPB* pb)
{
	if (pb->fReply.fResult == noErr)
		fState = kIdle;
	else if (fState == kInFlux)
		fState = kUnbnd;
	if (fClientRefCon != 0 && pb->fAsync)
	{
		pb->fEvent.fError = pb->fReply.fResult;
		ULong size = 0x24;
		((TEndpointClient*) fClientRefCon)->AEHandlerProc(nil, &size, &pb->fEvent);
	}
}


// ROM 0x001dc614 HandleUnBindReply__15TSerialEndpointFP15TCommToolBindPB
void
TSerialEndpoint::HandleUnBindReply(TCommToolBindPB* pb)
{
	if (pb->fReply.fResult == noErr)
		fState = kUnbnd;
	else if (fState == kInFlux)
		fState = kIdle;
	if (fClientRefCon != 0 && pb->fAsync)
	{
		pb->fEvent.fError = pb->fReply.fResult;
		ULong size = 0x24;
		((TEndpointClient*) fClientRefCon)->AEHandlerProc(nil, &size, &pb->fEvent);
	}
}


// ---------------------------------------------------------------------------
//	Event and kill requests
// ---------------------------------------------------------------------------

// ROM 0x001dca18 PostEventRequest__15TSerialEndpointFP16TCommToolEventPB
NewtonErr
TSerialEndpoint::PostEventRequest(TCommToolEventPB* pb)
{
	if (fState == kUninit)
		return kEndpointErrBadState;
	pb->fRequest.fOpCode = 0;
	pb->SetCollectorPort(*((TAppWorld*) GetGlobals())->GetMyPort());
	return fEventHandler->CallService(kCommToolRequestTypeGetEvent, pb, &pb->fRequest, sizeof(pb->fRequest), &pb->fReply, sizeof(pb->fReply), 0, 0, false);
}


// ROM 0x001dca94 PostKillRequest__15TSerialEndpointFUlUc
// (While it is in flight the endpoint is aborting.)
NewtonErr
TSerialEndpoint::PostKillRequest(ULong requestsToKill, Boolean async)
{
	NewtonErr err;
	TCommToolAbortPB* pb = new TCommToolAbortPB(requestsToKill, fClientRefCon, async);
	if (pb == nil)
		err = MemError();
	else
	{
		pb->SetCollectorPort(*((TAppWorld*) GetGlobals())->GetMyPort());
		err = fEventHandler->CallService(kCommToolRequestTypeKill, pb, &pb->fRequest, sizeof(pb->fRequest), &pb->fReply, sizeof(pb->fReply), 0, 0, false);
		if (err == noErr)
		{
			fPending->InsertAt(fPending->GetArraySize(), pb);
			fAborting = true;
		}
		else
			delete pb;
	}
	return err;
}


// ROM 0x001dd1c8 KillKillKill__15TSerialEndpointFUlP14TUAsyncMessage
// A synchronous kill request, and the message aborted; the tool's "not
// connected any more" (kCommErrConnectionAborted) is no error.
NewtonErr
TSerialEndpoint::KillKillKill(ULong requestsToKill, TUAsyncMessage* pb)
{
	TCommToolKillRequest kill;
	TCommToolReply reply;
	kill.fRequestsToKill = (CommToolRequestType) requestsToKill;
	NewtonErr err = fEventHandler->CallService(kCommToolRequestTypeKill, nil, &kill, sizeof(kill), &reply, sizeof(reply), 0, 0, true);
	if (pb != nil && err == noErr)
		err = pb->Abort();
	if (err == noErr)
		err = reply.fResult;
	if (err == kCommErrConnectionAborted)
		err = noErr;
	return err;
}


// ---------------------------------------------------------------------------
//	The lists
// ---------------------------------------------------------------------------

// ROM 0x001dcc58 InitPending__15TSerialEndpointFv
NewtonErr
TSerialEndpoint::InitPending()
{
	NewtonErr err = noErr;
	fPending = new CList;
	if (fPending == nil)
		err = MemError();
	return err;
}


// ROM 0x001dcc90 NukePending__15TSerialEndpointFv
// Every call in flight aborted and its PB freed.
void
TSerialEndpoint::NukePending()
{
	if (fPending != nil)
	{
		for (ArrayIndex i = fPending->GetArraySize(); i-- > 0; )
		{
			TCommToolPB* pb = (TCommToolPB*) fPending->At(i);
			pb->Abort();
			fPending->RemoveElementsAt(i, 1);
			if (pb != nil)
				delete pb;		// (the ROM destroys it as a TUAsyncMessage whatever it is)
		}
		delete fPending;
	}
	fPending = nil;
}


// ROM 0x001dcd24 InitPutPBList__15TSerialEndpointFv
// (One put PB made to start with.)
NewtonErr
TSerialEndpoint::InitPutPBList()
{
	fPutPBs = new CList;
	if (fPutPBs == nil)
		return MemError();
	TCommToolPutPB* pb = GrabPutPB(false);
	if (pb != nil)
		return fPutPBs->InsertElementsBefore(fPutPBs->GetArraySize(), &pb, 1);
	return kEndpointErrNoPBs;
}


// ROM 0x001dcd78 GrabPutPB__15TSerialEndpointFUc
// A put PB off the free list, or a new one (its buffer list made); with a
// segment of its own when the caller's bytes are to go in one.
TCommToolPutPB*
TSerialEndpoint::GrabPutPB(Boolean withSegment)
{
	TCommToolPutPB* pb;
	NewtonErr err;
	ArrayIndex count = fPutPBs->GetArraySize();
	if (count == 0)
	{
		pb = new TCommToolPutPB(fClientRefCon, false);
		if (pb == nil)
			goto memError;
		pb->fList = CBufferList::New();
		if (pb->fList == nil)
			goto memError;
		if ((err = pb->fList->Init(false)) != noErr)
			goto failed;
	}
	else
	{
		pb = (TCommToolPutPB*) fPutPBs->At(count - 1);
		fPutPBs->RemoveElementsAt(fPutPBs->GetArraySize() - 1, 1);
	}
	if (!withSegment || pb->fSegment != nil)
		return pb;
	pb->fSegment = new CBufferSegment;
	if (pb->fSegment != nil)
		return pb;
memError:
	err = MemError();
failed:
	if (err != noErr)
	{
		if (pb != nil)
			delete pb;
		pb = nil;
	}
	return pb;
}


// ROM 0x001dce48 ReleasePutPB__15TSerialEndpointFP14TCommToolPutPB
// Back on the free list, emptied (or freed, with no list).
void
TSerialEndpoint::ReleasePutPB(TCommToolPutPB* pb)
{
	if (pb == nil)
		return;
	if (fPutPBs != nil)
	{
		pb->fList->RemoveAll();
		if (pb->fSegment != nil)
			pb->fSegment->Init(nil, 0, false, 0, -1);
		fPutPBs->InsertElementsBefore(fPutPBs->GetArraySize(), &pb, 1);
		return;
	}
	delete pb;
}


// ROM 0x001dcf38 NukePutPBList__15TSerialEndpointFv
void
TSerialEndpoint::NukePutPBList()
{
	// (the ROM reads the list's count without looking whether there is a
	// list - a Close after a failed Open faults; the host looks)
	if (fPutPBs != nil)
	for (ArrayIndex i = fPutPBs->GetArraySize(); i-- > 0; )
	{
		TCommToolPutPB* pb = (TCommToolPutPB*) fPutPBs->At(i);
		fPutPBs->RemoveElementsAt(i, 1);
		if (pb != nil)
			delete pb;
	}
	if (fPutPBs != nil)
		delete fPutPBs;
	fPutPBs = nil;
}


// ROM 0x001dcfb0 InitGetPBList__15TSerialEndpointFv
NewtonErr
TSerialEndpoint::InitGetPBList()
{
	fGetPBs = new CList;
	if (fGetPBs == nil)
		return MemError();
	TCommToolGetPB* pb = GrabGetPB(false);
	if (pb != nil)
		return fGetPBs->InsertElementsBefore(fGetPBs->GetArraySize(), &pb, 1);
	return kEndpointErrNoPBs;
}


// ROM 0x001dd004 GrabGetPB__15TSerialEndpointFUc
TCommToolGetPB*
TSerialEndpoint::GrabGetPB(Boolean withSegment)
{
	TCommToolGetPB* pb;
	NewtonErr err;
	ArrayIndex count = fGetPBs->GetArraySize();
	if (count == 0)
	{
		pb = new TCommToolGetPB(fClientRefCon, false);
		if (pb == nil)
			goto memError;
		pb->fList = CBufferList::New();
		if (pb->fList == nil)
			goto memError;
		if ((err = pb->fList->Init(false)) != noErr)
			goto failed;
	}
	else
	{
		pb = (TCommToolGetPB*) fGetPBs->At(count - 1);
		fGetPBs->RemoveElementsAt(fGetPBs->GetArraySize() - 1, 1);
	}
	if (!withSegment || pb->fSegment != nil)
		return pb;
	pb->fSegment = new CBufferSegment;
	if (pb->fSegment != nil)
		return pb;
memError:
	err = MemError();
failed:
	if (err != noErr)
	{
		if (pb != nil)
			delete pb;
		pb = nil;
	}
	return pb;
}


// ROM 0x001dd0d4 ReleaseGetPB__15TSerialEndpointFP14TCommToolGetPB
void
TSerialEndpoint::ReleaseGetPB(TCommToolGetPB* pb)
{
	if (pb == nil)
		return;
	if (fGetPBs != nil)
	{
		pb->fList->RemoveAll();
		if (pb->fSegment != nil)
			pb->fSegment->Init(nil, 0, false, 0, -1);
		fGetPBs->InsertElementsBefore(fGetPBs->GetArraySize(), &pb, 1);
		return;
	}
	delete pb;
}


// ROM 0x001dd150 NukeGetPBList__15TSerialEndpointFv
void
TSerialEndpoint::NukeGetPBList()
{
	// (the ROM does not look whether there is a list; the host does)
	if (fGetPBs != nil)
	for (ArrayIndex i = fGetPBs->GetArraySize(); i-- > 0; )
	{
		TCommToolGetPB* pb = (TCommToolGetPB*) fGetPBs->At(i);
		fGetPBs->RemoveElementsAt(i, 1);
		if (pb != nil)
			delete pb;
	}
	if (fGetPBs != nil)
		delete fGetPBs;
	fGetPBs = nil;
}
