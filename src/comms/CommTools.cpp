/*
	File:		comms/CommTools.cpp

	Contains:	TCommTool (CommTools.h), the comm tool requests and replies
				(the DDK's CommTool.h), the options the tool itself keeps
				(CommToolOptions.h, CommOptions.h's TCMOTransportInfo), and
				StartCommTool/OpenCommTool/ServiceToPort.

	Reconstructed from the MP2x00 US ROM (0x0006ccc4-0x00070424,
	0x0007097c-0x00070aec); each function cites its origin.
*/

#include "CommTools.h"
#include "BufferList.h"
#include "CMService.h"
#include "NameServer.h"
#include "NewtonMemory.h"
#include "NewtErrors.h"
#include "OSErrors.h"
#include "CommErrors.h"
#include "SharedTypes.h"
#include "UserSharedMem.h"

#include <stdio.h>

extern "C" long	SMemCopyToSharedSWI(ULong id, void* buffer, ULong size, ULong offset, ULong sendersMsgId, ULong signature);
extern "C" long	SMemMsgMsgDoneSWI(ULong msgId, long result, ULong signature);


// the comm tool task's priority (StartCommTool)
#define kCommToolTaskPriority	13

// The data length of an option class: the ROM's constructors write the
// ROM's size, the host its own (DEVIATION: pointer size).
#define OPTION_DATA_LENGTH(cls)	(sizeof(cls) - sizeof(TOption))

// A name-server name for a four-character id: the characters in the order
// the ROM's BlockMove of a big-endian word leaves them, and a nul.
static void
FourCharName(ULong id, char name[5])
{
	name[0] = id >> 24;
	name[1] = id >> 16;
	name[2] = id >> 8;
	name[3] = id;
	name[4] = 0;
}


// ---------------------------------------------------------------------------
//	The options a comm tool keeps
// ---------------------------------------------------------------------------

// ROM 0x0006cd04 __ct__13TCMOIdleTimerFv
TCMOIdleTimer::TCMOIdleTimer()
	: TOption(kOptionType)
{
	SetLabel('citr');
	SetLength(OPTION_DATA_LENGTH(TCMOIdleTimer));
	fValue = 120;
}


// ROM 0x0006cd58 __ct__15TCMOListenTimerFv
TCMOListenTimer::TCMOListenTimer()
	: TOption(kOptionType)
{
	SetLabel('cltr');
	SetLength(OPTION_DATA_LENGTH(TCMOListenTimer));
	fValue = 120;
}


// ROM 0x0006cdac __ct__17TCMOCTConnectInfoFv
TCMOCTConnectInfo::TCMOCTConnectInfo()
	: TOption(kOptionType)
{
	SetLabel('ctci');
	SetLength(OPTION_DATA_LENGTH(TCMOCTConnectInfo));
	fErrorFree = false;
	fSupportsCallBack = false;
	fViaAppleTalk = false;
	fAppleTalkAddr = 0;
	fConnectBitsPerSecond = 0;
}


// ROM 0x0006ce10 __ct__23TCMOToolSpecificOptionsFv
TCMOToolSpecificOptions::TCMOToolSpecificOptions()
	: TOption(kOptionType)
{
	SetLabel('ctso');
	SetLength(OPTION_DATA_LENGTH(TCMOToolSpecificOptions));
	fTargetServiceIdentifier = 0;
}


// ROM 0x0006ce64 __ct__16TCMOPassiveClaimFv
TCMOPassiveClaim::TCMOPassiveClaim()
	: TOption(kOptionType)
{
	SetLabel('cpcm');
	SetLength(OPTION_DATA_LENGTH(TCMOPassiveClaim));
	fPassiveClaim = false;
}


// ROM 0x0006ceb8 __ct__16TCMOPassiveStateFv
TCMOPassiveState::TCMOPassiveState()
	: TOption(kOptionType)
{
	SetLabel('cpst');
	SetLength(OPTION_DATA_LENGTH(TCMOPassiveState));
	fPassiveState = false;
}


// ROM 0x0006cbd8 __ct__17TCMOTransportInfoFv
TCMOTransportInfo::TCMOTransportInfo()
	: TOption(kOptionType)
{
	SetLabel('tinf');
	SetLength(OPTION_DATA_LENGTH(TCMOTransportInfo));
	serviceType = 0;
	flags = 0;
	tdsu = T_INVALID;
	etdsu = T_INVALID;
	connect = T_INVALID;
	discon = T_INVALID;
	addr = T_INVALID;
	opt = T_INVALID;
}


// ---------------------------------------------------------------------------
//	The requests and replies
//	(DEVIATION: a reply's fSize is the host's sizeof; the ROM's are 0x10,
//	0x14, 0x18 and 0x24.)
// ---------------------------------------------------------------------------

// ROM 0x0006ccc4 __ct__15TCommToolAEventFv
TCommToolAEvent::TCommToolAEvent()
{
	fAEventID = 'comt';
}


// ROM 0x0006d44c __ct__23TCommToolControlRequestFv
TCommToolControlRequest::TCommToolControlRequest()
{
	fOpCode = 0;
}


// ROM 0x0006e234 __ct__14TCommToolReplyFv
TCommToolReply::TCommToolReply()
{
	fResult = noErr;
	fSize = sizeof(TCommToolReply);
}


// ROM 0x0006e738 __ct__19TCommToolPutRequestFv
TCommToolPutRequest::TCommToolPutRequest()
{
	fData = nil;
	fValidCount = 0;
	fOutside = false;
	fFrameData = false;
	fEndOfFrame = false;
}


// ROM 0x0006ecc8 __ct__17TCommToolPutReplyFv
TCommToolPutReply::TCommToolPutReply()
{
	fPutBytesCount = 0;
	fSize = sizeof(TCommToolPutReply);
}


// ROM 0x0006f250 __ct__19TCommToolGetRequestFv
TCommToolGetRequest::TCommToolGetRequest()
{
	fData = nil;
	fThreshold = 0;
	fNonBlocking = false;
	fFrameData = false;
	fOutside = false;
}


// ROM 0x0006f57c __ct__17TCommToolGetReplyFv
TCommToolGetReply::TCommToolGetReply()
{
	fEndOfFrame = false;
	fGetBytesCount = 0;
	fSize = sizeof(TCommToolGetReply);
}


// ROM 0x0006f8ec __ct__20TCommToolOpenRequestFv
TCommToolOpenRequest::TCommToolOpenRequest()
{
	fOpCode = kCommToolOpen;
	fOptions = nil;
	fOptionCount = 0;
	fOutside = false;
}


// ROM 0x0006fb54 __ct__18TCommToolOpenReplyFv
TCommToolOpenReply::TCommToolOpenReply()
{
	fPortId = 0;
	fSize = sizeof(TCommToolOpenReply);
}


// ROM 0x0006fd08 __ct__23TCommToolConnectRequestFv
TCommToolConnectRequest::TCommToolConnectRequest()
{
	fOpCode = kCommToolConnect;
	fReserved1 = 0;
	fReserved2 = 0;
	fOptions = nil;
	fOptionCount = 0;
	fData = nil;
	fSequence = 0;
	fOutside = false;
}


// ROM 0x0006d488 __ct__21TCommToolConnectReplyFv
TCommToolConnectReply::TCommToolConnectReply()
{
	fSequence = 0;
	fSize = sizeof(TCommToolConnectReply);
}


// ROM 0x0006d794 __ct__26TCommToolDisconnectRequestFv
TCommToolDisconnectRequest::TCommToolDisconnectRequest()
{
	fOpCode = kCommToolDisconnect;
	fDisconnectData = nil;
	fSequence = 0;
	fReason = 0;
	fOutside = false;
}


// ROM 0x0006dba4 __ct__20TCommToolBindRequestFv
TCommToolBindRequest::TCommToolBindRequest()
{
	fOpCode = kCommToolBind;
	fReserved1 = 0;
	fReserved2 = 0;
	fOutside = false;
}


// ROM 0x0006e034 __ct__18TCommToolBindReplyFv
TCommToolBindReply::TCommToolBindReply()
{
	fWastedULong = 0;
	fSize = sizeof(TCommToolBindReply);
}


// ROM 0x0006e078 __ct__26TCommToolOptionMgmtRequestFv
TCommToolOptionMgmtRequest::TCommToolOptionMgmtRequest()
{
	fOpCode = kCommToolOptionMgmt;
	fOptions = nil;
	fOptionCount = 0;
	fRequestOpCode = 0;
	fOutside = false;
	fCopyBack = false;
}


// ROM 0x0006e0cc __ct__27TCommToolGetProtAddrRequestFv
TCommToolGetProtAddrRequest::TCommToolGetProtAddrRequest()
{
	fOpCode = kCommToolGetProtAddr;
	fBoundAddr = nil;
	fPeerAddr = nil;
	fOutside = false;
}


// ROM 0x0006e118 __ct__22TCommToolGetEventReplyFv
TCommToolGetEventReply::TCommToolGetEventReply()
{
	fEventCode = 0;
	fEventTime.time.hi = 0;
	fEventTime.time.lo = 0;
	fEventData = 0;
	fSize = sizeof(TCommToolGetEventReply);
}


// ROM 0x0006e17c __ct__20TCommToolKillRequestFv
TCommToolKillRequest::TCommToolKillRequest()
{
	fRequestsToKill = (CommToolRequestType) (kCommToolRequestTypeGet | kCommToolRequestTypePut
					| kCommToolRequestTypeControl | kCommToolRequestTypeGetEvent);
}


// ROM 0x0006e1b8 __ct__22TCommToolStatusRequestFv
TCommToolStatusRequest::TCommToolStatusRequest()
{
	fStatusInfoId = 0;
}


// ROM 0x0006e1f4 __ct__22TCommToolResArbRequestFv
TCommToolResArbRequest::TCommToolResArbRequest()
{
	fResNamePtr = nil;
	fResTypePtr = nil;
}


// ROM 0x0006e278 __ct__21TCommToolMsgContainerFv
TCommToolMsgContainer::TCommToolMsgContainer()
{
	fRequestPending = false;
	fRequestMsgSize = 0;
}


// ROM 0x0006e2bc __ct__19TCommToolOptionInfoFv
TCommToolOptionInfo::TCommToolOptionInfo()
{
	fOptionsState = 0;
	fChannelNum = kCommToolControlChannel;
	fOptions = nil;
	fCurOptPtr = nil;
	fOptionsIterator = nil;
}


// ---------------------------------------------------------------------------
//	TCommTool: construction and the task
// ---------------------------------------------------------------------------

// ROM 0x0006e2fc __ct__9TCommToolFUl
TCommTool::TCommTool(ULong serviceId)
{
	fConnectParms.udata = nil;
	fConnectParms.sequence = 0;
	fHeapSize = kCommToolDefaultHeapSize;
	fServiceId = serviceId;
	fToolHeap = nil;
}


// ROM 0x0006e3b8 __ct__9TCommToolFUll
TCommTool::TCommTool(ULong serviceId, Size heapSize)
{
	fConnectParms.udata = nil;
	fConnectParms.sequence = 0;
	fHeapSize = heapSize;
	fServiceId = serviceId;
	fToolHeap = nil;
}


// ROM 0x0006e470 __dt__9TCommToolFv
// (The tool task's copy gives its heap back; the parent's never made one.)
TCommTool::~TCommTool()
{
	if (fToolHeap != nil)
	{
		SetHeap(fSavedHeap);
		DestroyVMHeap(fToolHeap);
		fToolHeap = nil;
	}
}


// ROM 0x0006e4f8 TaskConstructor__9TCommToolFv
// In the tool's task: a heap of its own, the port registered, and the two
// buffer lists an outside get or put is read through.
NewtonErr
TCommTool::TaskConstructor()
{
	fChannelFilter = 0xFFFFFFFF;
	fDone = false;
	fPortRegistered = false;
	fToolState = 0;
	fGetBufferList = nil;
	fPutBufferList = nil;
	fRequestsToKill = 0;
	fPassiveClaim = false;
	fPassiveState = false;
	fField1D2 = false;
	fForwardRequest = nil;
	fForwardMsg = nil;
	fOptionsInfo.fChannelNum = kCommToolControlChannel;
	fForwardReply = nil;
	fPutOptionsInfo.fChannelNum = kCommToolPutChannel;
	fGetOptionsInfo.fChannelNum = kCommToolGetChannel;
	fConnectParms.udata = nil;
	fTimerInterval = 0;
	fTimeout = 0;
	fSavedHeap = GetHeap();

	NewtonErr err = NewVMHeap(0, fHeapSize, &fToolHeap, 0);
	if (err == noErr)
	{
		SetHeap(fToolHeap);
		err = CreatePort(fServiceId, fToolPort);
		if (err == noErr)
		{
			fGetBufferList = CBufferList::New();
			if (fGetBufferList != nil)
			{
				fPutBufferList = CBufferList::New();
				if (fPutBufferList != nil)
				{
					if ((err = fGetBufferList->Init(false)) == noErr
					&&  (err = fPutBufferList->Init(false)) == noErr)
					{
						fGetBufferList->Insert(&fGetShadow);
						fPutBufferList->Insert(&fPutShadow);
						return noErr;
					}
					TaskDestructor();
					return err;
				}
			}
			err = MemError();
		}
	}
	TaskDestructor();
	return err;
}


// ROM 0x0006e634 TaskDestructor__9TCommToolFv
void
TCommTool::TaskDestructor()
{
	UnRegisterPort();
	if (fGetBufferList != nil)
	{
		fGetBufferList->Delete();
		fGetBufferList = nil;
	}
	if (fPutBufferList != nil)
	{
		fPutBufferList->Delete();
		fPutBufferList = nil;
	}
}


// ROM 0x0006df84 CreatePort__9TCommToolFUlR6TUPort
// The tool's port, registered with the name server as (the task's id in
// decimal, the service's four characters) - what ServiceToPort looks up.
NewtonErr
TCommTool::CreatePort(ULong serviceId, TUPort& port)
{
	NewtonErr err = port.Init();
	if (err == noErr)
	{
		char type[5], name[16];
		FourCharName(serviceId, type);
		sprintf(name, "%d", (int) (ULong) fChildTask);
		TUNameServer ns;
		err = ns.RegisterName(name, type, port.fId, 0);
		if (err == noErr)
			fPortRegistered = true;
	}
	return err;
}


// ROM 0x0006e688 UnRegisterPort__9TCommToolFv
void
TCommTool::UnRegisterPort()
{
	if (!fPortRegistered)
		return;
	char type[5], name[16];
	FourCharName(fServiceId, type);
	sprintf(name, "%d", (int) (ULong) fChildTask);
	TUNameServer ns;
	ns.UnRegisterName(name, type);
	fPortRegistered = false;
}


// ROM 0x0006e784 GetToolPort__9TCommToolFUlR6TUPort
// Another tool's port, registered under its service's four characters
// alone.
NewtonErr
TCommTool::GetToolPort(ULong serviceId, TUPort& port)
{
	char name[5];
	FourCharName(serviceId, name);
	TUNameServer ns;
	TObjectId id;
	ULong spec;
	NewtonErr err = ns.Lookup(name, name, &id, &spec);
	if (err == noErr)
		port.CopyObject(id);
	return err;
}


// ROM 0x0006e810 InitAsyncRPCMsg__9TCommToolFR14TUAsyncMessageUl
// An async RPC whose reply comes back to the tool's own port (TaskMain
// hands it to HandleReply with this refCon).
NewtonErr
TCommTool::InitAsyncRPCMsg(TUAsyncMessage& asyncMsg, ULong refCon)
{
	NewtonErr err = asyncMsg.Init(true);
	if (err != noErr)
		return err;
	err = asyncMsg.SetUserRefCon(refCon);
	if (err != noErr)
		return err;
	asyncMsg.SetCollectorPort(fToolPort.fId);
	return err;
}


// ROM 0x0006e708 SetChannelFilter__9TCommToolF19CommToolRequestTypeUc
void
TCommTool::SetChannelFilter(CommToolRequestType msgType, Boolean enable)
{
	if (enable)
		fChannelFilter |= msgType;
	else
		fChannelFilter &= ~msgType;
}


// ROM 0x0006e720 RequestTypeToChannelNumber__9TCommToolF19CommToolRequestType
CommToolChannelNumber
TCommTool::RequestTypeToChannelNumber(CommToolRequestType msgType)
{
	int channel = 0;
	Long type = msgType;
	while ((type >>= 1) != 0)
		channel++;
	return (CommToolChannelNumber) channel;
}


// ROM 0x0006e864 HandleInternalEvent__9TCommToolFv
void
TCommTool::HandleInternalEvent()
{
}


// ROM 0x0006eb58 HandleTimerTick__9TCommToolFv
void
TCommTool::HandleTimerTick()
{
}


// ROM 0x0006e868 TaskMain__9TCommToolFv
// Take the requests: each goes to its channel's Prep... (the channel then
// closed until it is answered); a reply to one of the tool's own async
// RPCs goes to HandleReply, anything else to HandleRequest.  A timer
// interval makes HandleTimerTick run that often.
void
TCommTool::TaskMain()
{
	TUMsgToken token;
	ULong msgType;
	while (!fDone)
	{
		Boolean ticked = false;
		TTime start;
		start = GetGlobalTime();
		NewtonErr err = fToolPort.Receive(&fRequestSize, fRequest, sizeof(fRequest), &token, &msgType, fTimeout, fChannelFilter, false, false);
		if (err == kError_Message_Timed_Out)
		{
			HandleTimerTick();
			ticked = true;
			fTimeout = fTimerInterval;
		}
		else if (err == noErr)
		{
			ULong type = msgType & 0x7FFFFF;
			if (msgType & 0x2000000)
			{
				// the reply to an async RPC of the tool's own
				ULong refCon;
				if (token.GetUserRefCon(&refCon) == noErr)
					HandleReply(refCon, msgType);
			}
			else if (type > kCommToolRequestTypeResArb || type == 0)
				HandleRequest(token, msgType);
			else
			{
				CommToolChannelNumber channel = RequestTypeToChannelNumber((CommToolRequestType) type);
				TCommToolMsgContainer& container = fRequests[channel];
				container.fRequestMsgSize = fRequestSize;
				container.fMsgToken = token;
				container.fRequestPending = true;
				SetChannelFilter((CommToolRequestType) type, false);
				switch (channel)
				{
				case kCommToolGetChannel:
					PrepGetRequest();
					break;
				case kCommToolPutChannel:
					PrepPutRequest();
					break;
				case kCommToolControlChannel:
					PrepControlRequest(type);
					break;
				case kCommToolGetEventChannel:
					GetCommEvent();
					break;
				case kCommToolKillChannel:
					PrepKillRequest();
					break;
				case kCommToolStatusChannel:
					DoStatus(ControlRequest()->fOpCode, type);
					break;
				case kCommToolResArbChannel:
					PrepResArbRequest();
					break;
				default:
					break;
				}
			}
		}
		if (fTimerInterval != 0 && !ticked)
		{
			TTime now;
			now = GetGlobalTime();
			ULong elapsed = now.time.lo - start.time.lo;
			if ((ULong) fTimeout < elapsed)
			{
				HandleTimerTick();
				fTimeout = fTimerInterval;
			}
			else
				fTimeout -= elapsed;
		}
		HandleInternalEvent();
	}
}


// ROM 0x0006eb0c HandleReply__9TCommToolFUlT1
// The reply to options forwarded to the tool below (refCon 20000).
void
TCommTool::HandleReply(ULong userRefCon, ULong msgType)
{
	if (userRefCon != kToolMsgForwardOpt)
		return;
	fOptionsInfo.fOptionsState &= ~(kOptionsNotProcessed | kOptionsForwardReqActive);
	ProcessOptionsComplete(fForwardReply->fResult, &fOptionsInfo);
}


// ROM 0x0006eb40 HandleRequest__9TCommToolFR10TUMsgTokenUl
// A message that is no request of the base class's: refused.
void
TCommTool::HandleRequest(TUMsgToken& msgToken, ULong msgType)
{
	SMemMsgMsgDoneSWI(msgToken.GetMsgId(), kError_Bad_Parameters, msgToken.GetSignature());
}


// ---------------------------------------------------------------------------
//	Answering requests
// ---------------------------------------------------------------------------

// ROM 0x0006d910 CompleteRequest__9TCommToolFR10TUMsgTokenl
void
TCommTool::CompleteRequest(TUMsgToken& msgToken, NewtonErr result)
{
	if (msgToken.GetReplyId() == 0)
		return;
	TCommToolReply reply;
	reply.fResult = result;
	msgToken.ReplyRPC(&reply, reply.fSize, 0);
}


// ROM 0x0006d958 CompleteRequest__9TCommToolFR10TUMsgTokenlR14TCommToolReply
NewtonErr
TCommTool::CompleteRequest(TUMsgToken& msgToken, NewtonErr result, TCommToolReply& reply)
{
	if (msgToken.GetReplyId() == 0)
		return noErr;
	reply.fResult = result;
	if (&reply != (TCommToolReply*) -1 && reply.fSize != 0)
	{
		NewtonErr err = SMemCopyToSharedSWI(msgToken.GetReplyId(), &reply, reply.fSize, 0, msgToken.GetMsgId(), msgToken.GetSignature());
		if (err != noErr)
		{
			SMemMsgMsgDoneSWI(msgToken.GetMsgId(), 0, msgToken.GetSignature());
			return err;
		}
	}
	return SMemMsgMsgDoneSWI(msgToken.GetMsgId(), 0, msgToken.GetSignature());
}


// ROM 0x0006d980 CompleteRequest__9TCommToolF21CommToolChannelNumberl
// Answer the channel's request in hand, and open the channel again.
void
TCommTool::CompleteRequest(CommToolChannelNumber channel, NewtonErr result)
{
	TCommToolMsgContainer& container = fRequests[channel];
	if (!container.fRequestPending)
		return;
	container.fRequestPending = false;
	SetChannelFilter(ChannelNumberToRequestType(channel), true);
	if (container.fMsgToken.GetReplyId() == 0)
		return;
	TCommToolReply reply;
	reply.fResult = result;
	container.fMsgToken.ReplyRPC(&reply, reply.fSize, 0);
}


// ROM 0x0006da0c CompleteRequest__9TCommToolF21CommToolChannelNumberlR14TCommToolReply
NewtonErr
TCommTool::CompleteRequest(CommToolChannelNumber channel, NewtonErr result, TCommToolReply& reply)
{
	TCommToolMsgContainer& container = fRequests[channel];
	if (!container.fRequestPending)
		return noErr;
	container.fRequestPending = false;
	SetChannelFilter(ChannelNumberToRequestType(channel), true);
	TUMsgToken& token = container.fMsgToken;
	if (token.GetReplyId() == 0)
		return noErr;			// (the ROM answers the token's address)
	reply.fResult = result;
	if (&reply != (TCommToolReply*) -1 && reply.fSize != 0)
	{
		NewtonErr err = SMemCopyToSharedSWI(token.GetReplyId(), &reply, reply.fSize, 0, token.GetMsgId(), token.GetSignature());
		if (err != noErr)
		{
			SMemMsgMsgDoneSWI(token.GetMsgId(), 0, token.GetSignature());
			return err;
		}
	}
	return SMemMsgMsgDoneSWI(token.GetMsgId(), 0, token.GetSignature());
}


// ROM 0x0006d858 FlushChannel__9TCommToolF19CommToolRequestTypel
// Answer every request of the type waiting at the port with flushResult.
NewtonErr
TCommTool::FlushChannel(CommToolRequestType filter, NewtonErr flushResult)
{
	TUMsgToken token;
	TCommToolControlRequest request;
	TCommToolReply reply;
	reply.fResult = flushResult;
	ULong size;
	while (fToolPort.Receive(&size, &request, sizeof(request), &token, nil, 0, filter, true, false) == noErr)
	{
		if (token.GetReplyId() != 0)
			token.ReplyRPC(&reply, sizeof(reply), 0);
	}
	return noErr;
}


// ROM 0x0006d7f4 GetConnectState__9TCommToolFv
// The status request's answer: the tool's state word written into the
// shared-memory object the request names.  (NOT YET: the ROM goes through a
// CMemObject, which it never frees - a leak - and which answers the object
// as memory of its own when it is; the host writes through TUSharedMem, what
// CMemObject does for another task's object.)
NewtonErr
TCommTool::GetConnectState()
{
	TCommToolStatusRequest* request = (TCommToolStatusRequest*) fRequest;
	if (request->fStatusInfoId == 0)
		return noErr;
	TUSharedMem mem;
	mem.fId = request->fStatusInfoId;
	uint32_t state = fToolState;
	return mem.CopyToShared(&state, 4, 0, nil);
}


// ROM 0x0006f1d8 DoStatus__9TCommToolFUlT1
void
TCommTool::DoStatus(ULong opCode, ULong msgType)
{
	NewtonErr err = kCommErrBadCommand;
	if (opCode == kCommToolGetConnectState)
		err = GetConnectState();
	CompleteRequest(kCommToolStatusChannel, err);
}


// ---------------------------------------------------------------------------
//	Aborting and terminating a connection
// ---------------------------------------------------------------------------

// ROM 0x0006dc3c HoldAbort__9TCommToolFv
void
TCommTool::HoldAbort()
{
	fAbortLock++;
}


// ROM 0x0006dc4c AllowAbort__9TCommToolFv
// Let a held abort go ahead: the last one out starts the termination.
void
TCommTool::AllowAbort()
{
	if ((fToolState & kToolStateWantAbort) && fAbortLock == 1)
	{
		fToolState |= kToolStateTerminating;
		TerminateConnection();
		return;
	}
	fAbortLock--;
}


// ROM 0x0006dc8c StartAbort__9TCommToolFl
NewtonErr
TCommTool::StartAbort(NewtonErr abortError)
{
	if (fToolState & kToolStateWantAbort)
		return fAbortErr;
	if (fToolState & (kToolStateConnecting | kToolStateConnected))
	{
		HoldAbort();
		fToolState |= kToolStateWantAbort;
		fAbortErr = abortError;
		AllowAbort();
		return noErr;
	}
	return kCommErrNotConnected;
}


// ROM 0x0006dce8 ShouldAbort__9TCommToolFUll
// A step of the connection finished (stateFlag cleared): whether the tool
// is aborting - starting one if the step failed.
Boolean
TCommTool::ShouldAbort(ULong stateFlag, NewtonErr result)
{
	fToolState &= ~stateFlag;
	if (result == noErr)
	{
		if ((fToolState & kToolStateWantAbort) == 0)
			return false;
	}
	else
	{
		if (fTerminationEvent == 0)
			fTerminationEvent = 2;
		StartAbort(result);
	}
	return true;
}


// ROM 0x0006dd3c TerminateConnection__9TCommToolFv
// Run the termination procs, phase by phase, while they finish at once (a
// proc that answers false will call TerminateConnection again when it is
// done); the last phase (flag nought) is TerminateComplete.
void
TCommTool::TerminateConnection()
{
	Boolean more = true;
	do
	{
		TerminateProcPtr proc;
		GetNextTermProc(fTerminationPhase, fTerminationFlag, proc);
		fTerminationPhase++;
		if (fTerminationFlag == 0)
		{
			TerminateComplete();
			return;
		}
		if (fToolState & fTerminationFlag)
			more = proc(this);
	} while (more);
}


// ROM 0x0006ddc8 TerminateComplete__9TCommToolFv
// The connection is gone: whatever was waiting on it is answered with the
// abort's error, and unless the client asked for it (fTerminationEvent 2)
// a disconnect event is posted.
void
TCommTool::TerminateComplete()
{
	NewtonErr err = fAbortErr;
	fAbortErr = noErr;
	fToolState &= 0xFFFFFF38;
	if (fRequests[kCommToolGetChannel].fRequestPending)
		TCommTool::GetComplete(err, false, 0);
	if (fRequests[kCommToolPutChannel].fRequestPending)
		TCommTool::PutComplete(err, 0);
	if (fToolState & kToolStateClosing)
		CloseComplete(noErr);
	else if (fRequests[kCommToolControlChannel].fRequestPending)
	{
		switch (fControlOpCode)
		{
		case kCommToolConnect:
			ConnectComplete(err);
			break;
		case kCommToolListen:
			ListenComplete(err);
			break;
		case kCommToolAccept:
			AcceptComplete(err);
			break;
		case kCommToolDisconnect:
			DisconnectComplete(noErr);
			break;
		case kCommToolRelease:
			ReleaseComplete(noErr);
			break;
		case kCommToolOptionMgmt:
			OptionMgmtComplete(err);
			break;
		default:
			CompleteRequest(kCommToolControlChannel, err);
			break;
		}
	}
	KillRequestComplete(kCommToolRequestTypeControl, noErr);
	if (fTerminationEvent != 2)
	{
		fEventReply.fEventCode = kCommToolEventDisconnected;
		fEventReply.fEventTime = GetGlobalTime();
		fEventReply.fEventData = fTerminationEvent;
		fEventReply.fServiceId = fServiceId;
		fEventReply.fResult = PostCommEvent(fEventReply, noErr);
	}
}


// A termination proc that is never called: the base class has no phases.
static Boolean
NoTerminationProc(void*)
{
	return false;
}


// ROM 0x0006df74 GetNextTermProc__9TCommToolFUlRUlRPFPv_Uc
// (The ROM hands back the address of an unrelated function; the flag being
// nought, it is never called.)
void
TCommTool::GetNextTermProc(ULong terminationPhase, ULong& terminationFlag, TerminateProcPtr& terminationProc)
{
	terminationFlag = 0;
	terminationProc = NoTerminationProc;
}


// ---------------------------------------------------------------------------
//	Events
// ---------------------------------------------------------------------------

// ROM 0x0006f29c GetCommEvent__9TCommToolFv
// A get-event request: an event that could not be posted when it happened
// is posted now.  ROM BUG: it looks for kCommErrNoEventPending (-16016)
// where PostCommEvent answers kCommErrNoRequestPending (-16015), so a
// disconnect with no get-event request waiting is never posted; kept.
void
TCommTool::GetCommEvent()
{
	if (fEventReply.fResult != kCommErrNoEventPending)
		return;
	PostCommEvent(fEventReply, noErr);
	fEventReply.fResult = kCommErrNoEventPending;
}


// ROM 0x0006f2e0 DoKillGetCommEvent__9TCommToolFv
void
TCommTool::DoKillGetCommEvent()
{
	TCommToolGetEventReply reply;
	CompleteRequest(kCommToolGetEventChannel, kCommErrRequestCanceled, reply);
	KillRequestComplete(kCommToolRequestTypeGetEvent, noErr);
}


// ROM 0x0006f328 PostCommEvent__9TCommToolFR22TCommToolGetEventReplyl
NewtonErr
TCommTool::PostCommEvent(TCommToolGetEventReply& theEvent, NewtonErr result)
{
	if (!fRequests[kCommToolGetEventChannel].fRequestPending)
		return kCommErrNoRequestPending;
	CompleteRequest(kCommToolGetEventChannel, result, theEvent);
	return noErr;
}


// ---------------------------------------------------------------------------
//	Gets and puts
// ---------------------------------------------------------------------------

// ROM 0x0006eb5c PrepGetRequest__9TCommToolFv
// A get: the client's buffer (an outside one read through fGetShadow), its
// options processed, then GetBytes, GetFramedBytes or GetBytesImmediate.
void
TCommTool::PrepGetRequest()
{
	TCommToolGetRequest* request = (TCommToolGetRequest*) fRequest;
	NewtonErr err = kCommErrNotConnected;
	if ((fToolState & kToolStateConnected)
	&&  (err = kCommErrReleasingConnection, (fToolState & kToolStateRelease) == 0))
	{
		if (request->fOutside)
		{
			err = fGetShadow.Init((TObjectId) (ULong) request->fData, 0, -1);
			if (err != noErr)
				goto complete;
			fGetBufferList->Reset();
			request->fData = fGetBufferList;
		}
		if (fRequests[kCommToolGetChannel].fRequestMsgSize == sizeof(TCommToolGetRequest))
		{
			fGetOptionsInfo.fOptionsState = 0;
			if (request->fOutside)
			{
				fGetOptionsInfo.fOptionsState = kOptionsOutside;
				fGetOptionsInfo.fOptionCount = request->fOptionCount;
			}
			fGetOptionsInfo.fChannelNum = kCommToolGetChannel;
			fGetOptionsInfo.fOptions = request->fOptions;
		}
		fGetData = request->fData;
		fGetNonBlocking = request->fNonBlocking;
		fGetFrameData = request->fFrameData;
		fGetThreshold = request->fThreshold;
		ProcessOptions(&fGetOptionsInfo);
		return;
	}
complete:
	CompleteRequest(kCommToolGetChannel, err);
}


// ROM 0x0006ec54 GetOptionsComplete__9TCommToolFl
void
TCommTool::GetOptionsComplete(NewtonErr result)
{
	if (result != noErr)
		GetComplete(result, false, 0);
	else if (fGetFrameData)
		GetFramedBytes(fGetData);
	else if (fGetNonBlocking)
		GetBytesImmediate(fGetData, fGetThreshold);
	else
		GetBytes(fGetData);
}


// ROM 0x0006ed0c GetBytesImmediate__9TCommToolFP11CBufferListl
void
TCommTool::GetBytesImmediate(CBufferList* clientBuffer, Size threshold)
{
	GetBytes(clientBuffer);
}


// ROM 0x0006db38 GetComplete__9TCommToolFlUcUl
void
TCommTool::GetComplete(NewtonErr result, Boolean endOfFrame, ULong getBytesCount)
{
	TCommToolGetReply reply;
	if (endOfFrame)
		reply.fEndOfFrame = true;
	reply.fGetBytesCount = getBytesCount;
	NewtonErr err = ProcessOptionsCleanUp(result, &fGetOptionsInfo);
	CompleteRequest(kCommToolGetChannel, err, reply);
}


// ROM 0x0006dbf0 KillGetComplete__9TCommToolFl
void
TCommTool::KillGetComplete(NewtonErr result)
{
	TCommToolGetReply reply;
	CompleteRequest(kCommToolGetChannel, kCommErrRequestCanceled, reply);
	KillRequestComplete(kCommToolRequestTypeGet, result);
}


// ROM 0x0006ed14 PrepPutRequest__9TCommToolFv
// A put: the client's data (an outside one read through fPutShadow, as much
// of it as fValidCount says), its options processed, then PutBytes or
// PutFramedBytes.
void
TCommTool::PrepPutRequest()
{
	TCommToolPutRequest* request = (TCommToolPutRequest*) fRequest;
	NewtonErr err = kCommErrNotConnected;
	if ((fToolState & kToolStateConnected)
	&&  (err = kCommErrReleasingConnection, (fToolState & kToolStateRelease) == 0))
	{
		if (request->fOutside)
		{
			err = fPutShadow.Init((TObjectId) (ULong) request->fData, 0, request->fValidCount);
			if (err != noErr)
				goto complete;
			fPutBufferList->Seek(0, kSeekFromBeginning);
			request->fData = fPutBufferList;
		}
		if (fRequests[kCommToolPutChannel].fRequestMsgSize == sizeof(TCommToolPutRequest))
		{
			fPutOptionsInfo.fOptionsState = 0;
			if (request->fOutside)
			{
				fPutOptionsInfo.fOptionsState = kOptionsOutside;
				fPutOptionsInfo.fOptionCount = request->fOptionCount;
			}
			fPutOptionsInfo.fChannelNum = kCommToolPutChannel;
			fPutOptionsInfo.fOptions = request->fOptions;
		}
		fPutData = request->fData;
		fPutFrameData = request->fFrameData;
		fPutEndOfFrame = request->fEndOfFrame;
		ProcessOptions(&fPutOptionsInfo);
		return;
	}
complete:
	CompleteRequest(kCommToolPutChannel, err);
}


// ROM 0x0006ee10 PutOptionsComplete__9TCommToolFl
void
TCommTool::PutOptionsComplete(NewtonErr result)
{
	if (result != noErr)
		PutComplete(result, 0);
	else if (fPutFrameData)
		PutFramedBytes(fPutData, fPutEndOfFrame);
	else
		PutBytes(fPutData);
}


// ROM 0x0006da88 PutComplete__9TCommToolFlUl
// (A release waiting for the put to finish goes ahead.)
void
TCommTool::PutComplete(NewtonErr result, ULong putBytesCount)
{
	TCommToolPutReply reply;
	reply.fPutBytesCount = putBytesCount;
	NewtonErr err = ProcessOptionsCleanUp(result, &fPutOptionsInfo);
	CompleteRequest(kCommToolPutChannel, err, reply);
	if (fToolState & kToolStateRelease)
		ReleaseStart();
}


// ROM 0x0006db00 KillPutComplete__9TCommToolFl
// (KillRequestComplete in line, in the ROM.)
void
TCommTool::KillPutComplete(NewtonErr result)
{
	CompleteRequest(kCommToolPutChannel, kCommErrRequestCanceled);
	KillRequestComplete(kCommToolRequestTypePut, result);
}


// ---------------------------------------------------------------------------
//	Kill and resource arbitration requests
// ---------------------------------------------------------------------------

// ROM 0x0006ee74 PrepKillRequest__9TCommToolFv
// Kill the kinds of request the kill request names: those waiting at the
// port are answered kCommErrRequestCanceled, the one in hand on each
// channel is handed to the subclass's KillGet/KillPut/DoKillControl/
// DoKillGetCommEvent, whose ...Complete clears its bit; the kill request is
// answered when none is left.
void
TCommTool::PrepKillRequest()
{
	TCommToolKillRequest* request = (TCommToolKillRequest*) fRequest;
	NewtonErr err;
	fRequestsToKill = request->fRequestsToKill;
	fKillError = noErr;
	if (fRequestsToKill & kCommToolRequestTypeGet)
	{
		if ((err = FlushChannel(kCommToolRequestTypeGet, kCommErrRequestCanceled)) != noErr)
			goto complete;
		if (fRequests[kCommToolGetChannel].fRequestPending)
			KillGet();
		else
			fRequestsToKill &= ~kCommToolRequestTypeGet;
	}
	if (fRequestsToKill & kCommToolRequestTypePut)
	{
		if ((err = FlushChannel(kCommToolRequestTypePut, kCommErrRequestCanceled)) != noErr)
			goto complete;
		if (fRequests[kCommToolPutChannel].fRequestPending)
			KillPut();
		else
			fRequestsToKill &= ~kCommToolRequestTypePut;
	}
	if (fRequestsToKill & kCommToolRequestTypeControl)
	{
		if ((err = FlushChannel(kCommToolRequestTypeControl, kCommErrRequestCanceled)) != noErr)
			goto complete;
		if (fRequests[kCommToolControlChannel].fRequestPending)
		{
			// options forwarded to the tool below are killed there too
			TUPort* forward = ForwardOptions();
			if (forward != nil
			&&  (fOptionsInfo.fOptionsState & kOptionsInProgress)
			&&  (fOptionsInfo.fOptionsState & kOptionsForwardReqActive))
			{
				TCommToolKillRequest kill;
				TCommToolReply reply;
				kill.fRequestsToKill = kCommToolRequestTypeControl;
				ULong replySize;
				err = forward->SendRPC(&replySize, &kill, sizeof(kill), &reply, sizeof(reply), 0, kCommToolRequestTypeKill);
				if (err != noErr)
					goto complete;
			}
			DoKillControl(kCommToolRequestTypeKill);
		}
		else
			fRequestsToKill &= ~kCommToolRequestTypeControl;
	}
	if (fRequestsToKill & kCommToolRequestTypeGetEvent)
	{
		if ((err = FlushChannel(kCommToolRequestTypeGetEvent, kCommErrRequestCanceled)) != noErr)
			goto complete;
		if (fRequests[kCommToolGetEventChannel].fRequestPending)
			DoKillGetCommEvent();
		else
			fRequestsToKill &= ~kCommToolRequestTypeGetEvent;
	}
	if (fRequestsToKill != 0)
		return;
	err = noErr;
complete:
	CompleteRequest(kCommToolKillChannel, err);
}


// ROM 0x0006f094 KillRequestComplete__9TCommToolF19CommToolRequestTypel
// One kind of request killed; the kill request is answered (with the first
// error any of them had) when it was the last.
void
TCommTool::KillRequestComplete(CommToolRequestType requestTypeKilled, NewtonErr killResult)
{
	fRequestsToKill &= ~requestTypeKilled;
	if (killResult != noErr && fKillError == noErr)
		fKillError = killResult;
	if (fRequestsToKill != 0)
		return;
	CompleteRequest(kCommToolKillChannel, fKillError);
}


// ROM 0x0006f214 DoKillControl__9TCommToolFUl
// Kill the control request in hand: a connect (or a release) is aborted, as
// Disconnect does; anything else is simply cancelled.
NewtonErr
TCommTool::DoKillControl(ULong msgType)
{
	if ((fToolState & kToolStateConnecting) || (fToolState & kToolStateRelease))
	{
		if (fTerminationEvent == 0)
			fTerminationEvent = 2;
		if (fToolState & kToolStateWantAbort)
			return fAbortErr;
		if ((fToolState & (kToolStateConnecting | kToolStateConnected)) == 0)
			return kCommErrNotConnected;
		HoldAbort();
		fToolState |= kToolStateWantAbort;
		fAbortErr = kCommErrConnectionAborted;
		AllowAbort();
		return noErr;
	}
	fRequestsToKill &= ~kCommToolRequestTypeControl;
	if (fKillError == noErr)
		fKillError = kCommErrBadCommand;
	if (fRequestsToKill == 0)
		CompleteRequest(kCommToolKillChannel, fKillError);
	return noErr;
}


// ROM 0x0006f0c8 PrepResArbRequest__9TCommToolFv
void
TCommTool::PrepResArbRequest()
{
	TCommToolResArbRequest* request = (TCommToolResArbRequest*) fRequest;
	if (request->fOpCode == kCommToolResArbRelease)
		ResArbRelease(request->fResNamePtr, request->fResTypePtr);
	else if (request->fOpCode == kCommToolResArbClaimNotification)
		ResArbClaimNotification(request->fResNamePtr, request->fResTypePtr);
}


// ROM 0x0006fcbc ResArbRelease__9TCommToolFPUcT1
// Asked to give a resource up: only a passive claim in the passive state
// can.
void
TCommTool::ResArbRelease(UChar* resName, UChar* resType)
{
	if (!fPassiveClaim || !fPassiveState)
		ResArbReleaseComplete(kError_Resource_Claimed);
	else
		ResArbReleaseStart(resName, resType);
}


// ROM 0x0006fcec ResArbReleaseStart__9TCommToolFPUcT1
void
TCommTool::ResArbReleaseStart(UChar* resName, UChar* resType)
{
	CompleteRequest(kCommToolResArbChannel, kError_Resource_Claimed);
}


// ROM 0x0006fcfc ResArbReleaseComplete__9TCommToolFl
void
TCommTool::ResArbReleaseComplete(NewtonErr result)
{
	CompleteRequest(kCommToolResArbChannel, result);
}


// ROM 0x0006fd64 ResArbClaimNotification__9TCommToolFPUcT1
void
TCommTool::ResArbClaimNotification(UChar* resName, UChar* resType)
{
	CompleteRequest(kCommToolResArbChannel, kError_Resource_Unclaimed);
}


// ---------------------------------------------------------------------------
//	Control requests
// ---------------------------------------------------------------------------

// ROM 0x0006ee64 PrepControlRequest__9TCommToolFUl
void
TCommTool::PrepControlRequest(ULong msgType)
{
	DoControl(ControlRequest()->fOpCode, msgType);
}


// ROM 0x0006ff00 ProcessControlOptions__9TCommToolFUcP12TOptionArrayUl
// Process a control request's options (an outside array is a shared-memory
// object of optionCount options); DoControl's cases carry on from
// ProcessOptionsComplete.
void
TCommTool::ProcessControlOptions(Boolean outside, TOptionArray* options, ULong optionCount)
{
	fOptionsInfo.fOptionsState = 0;
	if (outside)
	{
		fOptionsInfo.fOptionsState = kOptionsOutside;
		fOptionsInfo.fOptionCount = optionCount;
	}
	fOptionsInfo.fChannelNum = kCommToolControlChannel;
	fOptionsInfo.fOptions = options;
	ProcessOptions(&fOptionsInfo);
}


// ROM 0x0006f114 DoControl__9TCommToolFUlT1
// A control request.  (What it answers is not looked at; the ROM's is
// sometimes a pointer.)
NewtonErr
TCommTool::DoControl(ULong opCode, ULong msgType)
{
	NewtonErr err;
	fControlOpCode = opCode;
	switch (opCode)
	{
	case kCommToolOpen:
		{
			TCommToolOpenRequest* request = (TCommToolOpenRequest*) fRequest;
			ProcessControlOptions(request->fOutside, request->fOptions, request->fOptionCount);
			return noErr;
		}

	case kCommToolClose:
		return Close();

	case kCommToolConnect:
	case kCommToolListen:
		err = ConnectCheck();
		if (err == noErr)
		{
			TCommToolConnectRequest* request = (TCommToolConnectRequest*) fRequest;
			err = ImportConnectPB(request);
			if (err != noErr)
			{
				if (opCode == kCommToolConnect)
					ConnectComplete(err);
				else
					ListenComplete(err);
				return noErr;
			}
			ProcessControlOptions(request->fOutside, request->fOptions, request->fOptionCount);
			return noErr;
		}
		break;

	case kCommToolAccept:
		if (fToolState & kToolStateListenMode)
		{
			TCommToolConnectRequest* request = (TCommToolConnectRequest*) fRequest;
			err = ImportConnectPB(request);
			if (err != noErr)
			{
				AcceptComplete(err);
				return noErr;
			}
			ProcessControlOptions(request->fOutside, request->fOptions, request->fOptionCount);
			return noErr;
		}
		err = kCommErrNotConnected;
		break;

	case kCommToolDisconnect:
		if ((fToolState & (kToolStateConnecting | kToolStateConnected)) == 0)
		{
			DisconnectComplete(kCommErrNotConnected);
			return noErr;
		}
		fToolState |= kToolStateDisconnectReq;
		if (fTerminationEvent == 0)
			fTerminationEvent = 2;
		if (fToolState & kToolStateWantAbort)
			return fAbortErr;
		if ((fToolState & (kToolStateConnecting | kToolStateConnected)) == 0)
			return kCommErrNotConnected;
		HoldAbort();
		fToolState |= kToolStateWantAbort;
		fAbortErr = kCommErrConnectionAborted;
		AllowAbort();
		return noErr;

	case kCommToolRelease:
		err = kCommErrNotConnected;
		if (fToolState & (kToolStateConnecting | kToolStateConnected))
		{
			if (ShouldAbort(0, noErr))
				return true;
			fToolState |= kToolStateRelease;
			if (fTerminationEvent == 0)
				fTerminationEvent = 3;
			return ReleaseStart();
		}
		break;

	case kCommToolBind:
		{
			err = kCommErrAlreadyConnected;
			TOptionArray* options = nil;
			ULong optionCount = 0;
			if ((fToolState & (kToolStateConnecting | kToolStateConnected)) == 0)
			{
				if ((fToolState & kToolStateBound) == 0)
				{
					TCommToolBindRequest* request = (TCommToolBindRequest*) fRequest;
					if (fRequests[kCommToolControlChannel].fRequestMsgSize == sizeof(TCommToolBindRequest))
					{
						options = request->fOptions;
						optionCount = request->fOptionCount;
					}
					ProcessControlOptions(request->fOutside, options, optionCount);
					return noErr;
				}
				err = kCommErrAlreadyBound;
			}
		}
		break;

	case kCommToolUnbind:
		if ((fToolState & (kToolStateConnecting | kToolStateConnected)) == 0)
		{
			if (fToolState & kToolStateBound)
			{
				UnbindStart();
				return noErr;
			}
			err = noErr;
		}
		else
			err = kCommErrAlreadyConnected;
		UnbindComplete(err);
		return noErr;

	case kCommToolOptionMgmt:
		OptionMgmt((TCommToolOptionMgmtRequest*) fRequest);
		return noErr;

	case kCommToolGetProtAddr:
		GetProtAddr();
		return noErr;

	default:
		err = kCommErrBadCommand;
		break;
	}
	CompleteRequest(kCommToolControlChannel, err);
	return noErr;
}


// ROM 0x0006d7e4 GetProtAddr__9TCommToolFv
void
TCommTool::GetProtAddr()
{
	CompleteRequest(kCommToolControlChannel, kCommErrBadCommand);
}


// ---------------------------------------------------------------------------
//	Open and close
// ---------------------------------------------------------------------------

// ROM 0x0006f364 Open__9TCommToolFv
void
TCommTool::Open()
{
	TCommToolOpenRequest* request = (TCommToolOpenRequest*) fRequest;
	ProcessControlOptions(request->fOutside, request->fOptions, request->fOptionCount);
}


// ROM 0x0006f378 OpenOptionsComplete__9TCommToolFl
void
TCommTool::OpenOptionsComplete(NewtonErr result)
{
	TCommToolOpenReply reply;
	if (result == noErr && (result = OpenStart(fOptionsInfo.fOptions)) == noErr)
		OpenContinue();
	else
	{
		ProcessOptionsCleanUp(result, &fOptionsInfo);
		CompleteRequest(kCommToolControlChannel, result, reply);
		Close();
	}
}


// ROM 0x0006f408 OpenContinue__9TCommToolFv
// OpenComplete answers kCall_In_Progress (1) when it will call OpenContinue
// again; otherwise the open request is answered with the tool's port.
void
TCommTool::OpenContinue()
{
	TCommToolOpenReply reply;
	NewtonErr err = OpenComplete();
	if (err != 1)
	{
		NewtonErr cleanUpErr = ProcessOptionsCleanUp(err, &fOptionsInfo);
		if (err == noErr)
			err = cleanUpErr;
		reply.fPortId = fToolPort.fId;
		CompleteRequest(kCommToolControlChannel, err, reply);
		if (err != noErr)
			Close();
	}
}


// ROM 0x0006f498 OpenStart__9TCommToolFP12TOptionArray
NewtonErr
TCommTool::OpenStart(TOptionArray* options)
{
	return noErr;
}


// ROM 0x0006f4a0 OpenComplete__9TCommToolFv
NewtonErr
TCommTool::OpenComplete()
{
	return noErr;
}


// ROM 0x0006f4a8 Close__9TCommToolFv
// A connected tool aborts first (TerminateComplete then closes it);
// otherwise every request waiting is answered kCommErrToolBusy and the tool
// closes at once.
Boolean
TCommTool::Close()
{
	ULong state = fToolState;
	fToolState = state | kToolStateClosing;
	if (state & (kToolStateConnecting | kToolStateConnected))
	{
		if (fTerminationEvent == 0)
			fTerminationEvent = 2;
		StartAbort(kCommErrConnectionAborted);
		return false;
	}
	for (ULong type = kCommToolRequestTypeGet; (Long) type < 0x11; type *= 2)
		FlushChannel((CommToolRequestType) type, kCommErrToolBusy);
	CloseComplete(noErr);
	return true;
}


// ROM 0x0006f538 CloseComplete__9TCommToolFl
// TaskMain ends after this.
void
TCommTool::CloseComplete(NewtonErr result)
{
	fDone = true;
	fToolState &= ~kToolStateClosing;
	UnRegisterPort();
	CompleteRequest(kCommToolControlChannel, result);
}


// ---------------------------------------------------------------------------
//	Connect, listen and accept
// ---------------------------------------------------------------------------

// ROM 0x0006f5c4 ImportConnectPB__9TCommToolFP23TCommToolConnectRequest
// The connect request's data, an outside one read through a new
// CShadowBufferSegment.  ROM BUG: it checks the *old* fConnectParms.udata
// rather than the segment it just made, so an outside connect with data
// fails with kError_No_Memory (leaking the segment) unless the previous
// connect left its data behind; kept.
NewtonErr
TCommTool::ImportConnectPB(TCommToolConnectRequest* request)
{
	fConnectParms.sequence = request->fSequence;
	fConnectParms.outside = request->fOutside;
	if (!request->fOutside)
	{
		fConnectParms.udata = request->fData;
		return noErr;
	}
	if (request->fData == nil)
		return noErr;
	CShadowBufferSegment* segment = new CShadowBufferSegment;
	if (fConnectParms.udata == nil)
		return kError_No_Memory;
	NewtonErr err = segment->Init((TObjectId) (ULong) request->fData, 0, -1);
	if (err == noErr)
		fConnectParms.udata = segment;
	return err;
}


// ROM 0x0006f648 CopyBackConnectPB__9TCommToolFl
NewtonErr
TCommTool::CopyBackConnectPB(NewtonErr result)
{
	if (fConnectParms.outside && fConnectParms.udata != nil)
		delete fConnectParms.udata;
	fConnectParms.udata = nil;
	return ProcessOptionsCleanUp(result, &fOptionsInfo);
}


// ROM 0x0006f69c ConnectCheck__9TCommToolFv
// A connect or listen may start: the tool is connecting from now on.
NewtonErr
TCommTool::ConnectCheck()
{
	if (fToolState & (kToolStateConnecting | kToolStateConnected))
		return kCommErrAlreadyConnected;
	fToolState |= kToolStateConnecting;
	fTerminationFlag = 0;
	fTerminationPhase = 0;
	fTerminationEvent = 0;
	fAbortLock = 0;
	fEventReply.fResult = noErr;
	return noErr;
}


// ROM 0x0006f6dc Connect__9TCommToolFv
void
TCommTool::Connect()
{
	NewtonErr err = ConnectCheck();
	if (err == noErr)
	{
		TCommToolConnectRequest* request = (TCommToolConnectRequest*) fRequest;
		err = ImportConnectPB(request);
		if (err != noErr)
			ConnectComplete(err);
		else
			ProcessControlOptions(request->fOutside, request->fOptions, request->fOptionCount);
		return;
	}
	CompleteRequest(kCommToolControlChannel, err);
}


// ROM 0x0006f750 ConnectOptionsComplete__9TCommToolFl
void
TCommTool::ConnectOptionsComplete(NewtonErr result)
{
	if (result != noErr)
		ConnectComplete(result);
	else
		ConnectStart();
}


// ROM 0x0006f768 ConnectStart__9TCommToolFv
void
TCommTool::ConnectStart()
{
	ConnectComplete(noErr);
}


// ROM 0x0006f774 ConnectComplete__9TCommToolFl
// Connected (or not): a 'ctci option in the request is filled in with the
// connection's description.
void
TCommTool::ConnectComplete(NewtonErr result)
{
	fToolState &= ~kToolStateConnecting;
	if (result == noErr)
		fToolState |= kToolStateConnected;
	TOption* option;
	if (fOptionsInfo.fOptions != nil && result == noErr
	&&  (option = fOptionsInfo.fOptionsIterator->FindOption('ctci')) != nil)
	{
		if (option->GetOpCode() == opGetCurrent)
		{
			option->CopyDataFrom(&fConnectInfo);
			option->SetOpCodeResult(opSuccess);
		}
		else
			option->SetOpCodeResult(opFailure);
		option->SetProcessed();
	}
	TCommToolConnectReply reply;
	reply.fSequence = fConnectParms.sequence;
	NewtonErr err = CopyBackConnectPB(result);
	CompleteRequest(kCommToolControlChannel, err, reply);
}


// ROM 0x0006f83c Listen__9TCommToolFv
void
TCommTool::Listen()
{
	NewtonErr err = ConnectCheck();
	if (err == noErr)
	{
		TCommToolConnectRequest* request = (TCommToolConnectRequest*) fRequest;
		err = ImportConnectPB(request);
		if (err != noErr)
			ListenComplete(err);
		else
			ProcessControlOptions(request->fOutside, request->fOptions, request->fOptionCount);
		return;
	}
	CompleteRequest(kCommToolControlChannel, err);
}


// ROM 0x0006f8b0 ListenOptionsComplete__9TCommToolFl
void
TCommTool::ListenOptionsComplete(NewtonErr result)
{
	if (result == noErr)
	{
		fToolState |= kToolStateListenMode;
		ListenStart();
	}
	else
		ListenComplete(result);
}


// ROM 0x0006f8e0 ListenStart__9TCommToolFv
void
TCommTool::ListenStart()
{
	ListenComplete(noErr);
}


// ROM 0x0006f938 ListenComplete__9TCommToolFl
// (A listen that worked leaves the tool connecting, in listen mode, until
// the accept.)
void
TCommTool::ListenComplete(NewtonErr result)
{
	TCommToolConnectReply reply;
	if (result != noErr)
		fToolState &= ~(kToolStateConnecting | kToolStateListenMode);
	reply.fSequence = fConnectParms.sequence;
	NewtonErr err = CopyBackConnectPB(result);
	CompleteRequest(kCommToolControlChannel, err, reply);
}


// ROM 0x0006f998 Accept__9TCommToolFv
void
TCommTool::Accept()
{
	if (fToolState & kToolStateListenMode)
	{
		TCommToolConnectRequest* request = (TCommToolConnectRequest*) fRequest;
		NewtonErr err = ImportConnectPB(request);
		if (err != noErr)
			AcceptComplete(err);
		else
			ProcessControlOptions(request->fOutside, request->fOptions, request->fOptionCount);
		return;
	}
	CompleteRequest(kCommToolControlChannel, kCommErrNotConnected);
}


// ROM 0x0006fa10 AcceptOptionsComplete__9TCommToolFl
void
TCommTool::AcceptOptionsComplete(NewtonErr result)
{
	if (result != noErr)
		AcceptComplete(result);
	else
		AcceptStart();
}


// ROM 0x0006fa28 AcceptStart__9TCommToolFv
void
TCommTool::AcceptStart()
{
	AcceptComplete(noErr);
}


// ROM 0x0006fa34 AcceptComplete__9TCommToolFl
void
TCommTool::AcceptComplete(NewtonErr result)
{
	fToolState &= ~kToolStateListenMode;
	ConnectComplete(result);
}


// ---------------------------------------------------------------------------
//	Disconnect and release
// ---------------------------------------------------------------------------

// ROM 0x0006fa50 Disconnect__9TCommToolFv
NewtonErr
TCommTool::Disconnect()
{
	if ((fToolState & (kToolStateConnecting | kToolStateConnected)) == 0)
	{
		DisconnectComplete(kCommErrNotConnected);
		return noErr;
	}
	fToolState |= kToolStateDisconnectReq;
	if (fTerminationEvent == 0)
		fTerminationEvent = 2;
	if (fToolState & kToolStateWantAbort)
		return fAbortErr;
	if ((fToolState & (kToolStateConnecting | kToolStateConnected)) == 0)
		return kCommErrNotConnected;
	HoldAbort();
	fToolState |= kToolStateWantAbort;
	fAbortErr = kCommErrConnectionAborted;
	AllowAbort();
	return noErr;
}


// ROM 0x0006fa9c DisconnectComplete__9TCommToolFl
void
TCommTool::DisconnectComplete(NewtonErr result)
{
	fToolState &= ~kToolStateDisconnectReq;
	CompleteRequest(kCommToolControlChannel, result);
}


// ROM 0x0006fab4 Release__9TCommToolFv
void
TCommTool::Release()
{
	if (fToolState & (kToolStateConnecting | kToolStateConnected))
	{
		if (!ShouldAbort(0, noErr))
		{
			fToolState |= kToolStateRelease;
			if (fTerminationEvent == 0)
				fTerminationEvent = 3;
			ReleaseStart();
		}
		return;
	}
	CompleteRequest(kCommToolControlChannel, kCommErrNotConnected);
}


// ROM 0x0006fb24 ReleaseStart__9TCommToolFv
// An orderly release is an abort once the put in hand has gone (PutComplete
// calls this again).  (With a put in hand the ROM answers the tool's own
// address; nothing looks at it.)
NewtonErr
TCommTool::ReleaseStart()
{
	if (fRequests[kCommToolPutChannel].fRequestPending)
		return noErr;
	if (fToolState & kToolStateWantAbort)
		return fAbortErr;
	if ((fToolState & (kToolStateConnecting | kToolStateConnected)) == 0)
		return kCommErrNotConnected;
	HoldAbort();
	fToolState |= kToolStateWantAbort;
	fAbortErr = kCommErrConnectionAborted;
	AllowAbort();
	return noErr;
}


// ROM 0x0006fb3c ReleaseComplete__9TCommToolFl
void
TCommTool::ReleaseComplete(NewtonErr result)
{
	fToolState &= ~kToolStateRelease;
	CompleteRequest(kCommToolControlChannel, result);
}


// ---------------------------------------------------------------------------
//	Bind and unbind
// ---------------------------------------------------------------------------

// ROM 0x0006fb98 Bind__9TCommToolFv
void
TCommTool::Bind()
{
	NewtonErr err = kCommErrAlreadyConnected;
	TOptionArray* options = nil;
	ULong optionCount = 0;
	if ((fToolState & (kToolStateConnecting | kToolStateConnected)) == 0)
	{
		if ((fToolState & kToolStateBound) == 0)
		{
			TCommToolBindRequest* request = (TCommToolBindRequest*) fRequest;
			if (fRequests[kCommToolControlChannel].fRequestMsgSize == sizeof(TCommToolBindRequest))
			{
				options = request->fOptions;
				optionCount = request->fOptionCount;
			}
			ProcessControlOptions(request->fOutside, options, optionCount);
			return;
		}
		err = kCommErrAlreadyBound;
	}
	CompleteRequest(kCommToolControlChannel, err);
}


// ROM 0x0006fbf4 BindOptionsComplete__9TCommToolFl
void
TCommTool::BindOptionsComplete(NewtonErr result)
{
	if (result != noErr)
		BindComplete(result);
	else
		BindStart();
}


// ROM 0x0006fc0c BindStart__9TCommToolFv
void
TCommTool::BindStart()
{
	BindComplete(noErr);
}


// ROM 0x0006fc18 BindComplete__9TCommToolFl
void
TCommTool::BindComplete(NewtonErr result)
{
	NewtonErr err = ProcessOptionsCleanUp(result, &fOptionsInfo);
	if (err == noErr)
		fToolState |= kToolStateBound;
	CompleteRequest(kCommToolControlChannel, err);
}


// ROM 0x0006fc5c Unbind__9TCommToolFv
void
TCommTool::Unbind()
{
	if ((fToolState & (kToolStateConnecting | kToolStateConnected)) == 0 && (fToolState & kToolStateBound))
		UnbindStart();
	else
		UnbindComplete(noErr);		// ROM BUG: r1 is not set, so the result is whatever the state test left in it; the host passes noErr
}


// ROM 0x0006fc94 UnbindStart__9TCommToolFv
void
TCommTool::UnbindStart()
{
	UnbindComplete(noErr);
}


// ROM 0x0006fca0 UnbindComplete__9TCommToolFl
void
TCommTool::UnbindComplete(NewtonErr result)
{
	if (result == noErr)
		fToolState &= ~kToolStateBound;
	CompleteRequest(kCommToolControlChannel, result);
}


// ---------------------------------------------------------------------------
//	Options
// ---------------------------------------------------------------------------

// ROM 0x0006fd74 OptionMgmt__9TCommToolFP26TCommToolOptionMgmtRequest
// opProcess: the options processed one by one; opGetDefault/opGetCurrent:
// an array of the tool's defaults (current values) copied into the
// request's shared-memory object.
void
TCommTool::OptionMgmt(TCommToolOptionMgmtRequest* request)
{
	NewtonErr err = noErr;
	ULong opCode = request->fRequestOpCode;
	if (opCode == opProcess)
	{
		ProcessControlOptions(request->fOutside, request->fOptions, request->fOptionCount);
		return;
	}
	if (request->fOptions != nil)
	{
		err = kError_No_Memory;
		if (opCode == opGetDefault || opCode == opGetCurrent)
		{
			TOptionArray* options = new TOptionArray;
			if (options != nil)
			{
				err = options->Init();
				if (err == noErr)
				{
					err = (opCode == opGetDefault) ? AddDefaultOptions(options) : AddCurrentOptions(options);
					if (request->fOutside && err == noErr)
						err = options->CopyToShared((TObjectId) (ULong) request->fOptions);
				}
				delete options;
			}
		}
		else
			err = kCommErrBadCommand;
	}
	// ROM BUG: the reply is sent with r4's size - the request pointer, as it
	// happens, not the reply's; the host sends the reply's.
	CompleteRequest(kCommToolControlChannel, err);
}


// ROM 0x0006feb8 OptionMgmtComplete__9TCommToolFl
void
TCommTool::OptionMgmtComplete(NewtonErr result)
{
	NewtonErr err = ProcessOptionsCleanUp(result, &fOptionsInfo);
	CompleteRequest(kCommToolControlChannel, err);
}


// ROM 0x0006feec AddDefaultOptions__9TCommToolFP12TOptionArray
NewtonErr
TCommTool::AddDefaultOptions(TOptionArray* options)
{
	return noErr;
}


// ROM 0x0006fef4 AddCurrentOptions__9TCommToolFP12TOptionArray
NewtonErr
TCommTool::AddCurrentOptions(TOptionArray* options)
{
	return noErr;
}


// ROM 0x0006fefc ProcessOptions__9TCommToolFP12TOptionArray
void
TCommTool::ProcessOptions(TOptionArray* options)
{
}


// ROM 0x0006ff38 ProcessOptions__9TCommToolFP19TCommToolOptionInfo
// Start processing a request's options: an outside array is copied in from
// its shared-memory object first; each option then goes through
// ProcessOptionsContinue.
void
TCommTool::ProcessOptions(TCommToolOptionInfo* info)
{
	NewtonErr err = kError_No_Memory;
	TOptionArray* options = info->fOptions;
	if (options == nil)
	{
		err = noErr;
		goto complete;
	}
	if (info->fOptionsState & kOptionsOutside)
	{
		TOptionArray* copy = new TOptionArray;
		info->fOptions = copy;
		if (copy == nil)
			goto complete;
		info->fOptionsState |= kOptionsArrayAllocated;
		err = copy->Init((TObjectId) (ULong) options, info->fOptionCount);
		if (err != noErr)
			goto complete;
	}
	info->fOptionsIterator = new TOptionIterator(info->fOptions);
	if (info->fOptionsIterator != nil)
	{
		info->fOptionsState |= kOptionsInProgress;
		ProcessOptionsContinue(info);
		return;
	}
	err = kError_No_Memory;
complete:
	ProcessOptionsComplete(err, info);
}


// ROM 0x00070004 ProcessOptionsContinue__9TCommToolFP19TCommToolOptionInfo
// Process the options one at a time: a service option or one already
// processed is passed over; a 'ctso (tool-specific) option for this tool is
// marked and passed over, one for another tool ends the processing there;
// an option for another service is marked not processed (opNotFound... the
// forwarding's cue); the rest go to the channel's Process...OptionStart,
// whose answer is the option's result - or kCall_In_Progress (1), when the
// subclass calls Process...OptionComplete later.
// ROM BUG: when the 'ctso for another tool is the last option the request
// is never completed; kept.  (The result carries over from one option to
// the next when a channel has no ...OptionStart, which never happens.)
void
TCommTool::ProcessOptionsContinue(TCommToolOptionInfo* info)
{
	ULong result = 0;
	for ( ; ; )
	{
		TOption* option = info->fOptionsIterator->CurrentOption();
		info->fCurOptPtr = option;
		if (option == nil)
		{
			ProcessOptionsComplete(noErr, info);
			return;
		}
		info->fOptionsIterator->NextOption();
		option = info->fCurOptPtr;
		ULong opcode = option->GetOpCode();
		if (option->IsProcessed() || option->IsService())
			continue;
		if (option->Label() == 'ctso')
		{
			if (((TCMOToolSpecificOptions*) option)->fTargetServiceIdentifier == fServiceId)
			{
				option->SetProcessed();
				continue;
			}
			if (!info->fOptionsIterator->More())
				return;
			info->fOptionsState |= kOptionsNotProcessed;
			ProcessOptionsComplete(noErr, info);
			return;
		}
		if (option->IsServiceSpecific() && ((TOptionExtended*) option)->ServiceLabel() != fServiceId)
		{
			result = opNotFound;
			goto notProcessed;
		}
		if (opcode != opSetNegotiate && opcode != opSetRequired && opcode != opGetDefault && opcode != opGetCurrent)
			result = opBadOpCode;
		else
		{
			if (info->fChannelNum == kCommToolGetChannel)
				result = ProcessGetBytesOptionStart(option, option->Label(), opcode);
			else if (info->fChannelNum == kCommToolPutChannel)
				result = ProcessPutBytesOptionStart(option, option->Label(), opcode);
			else if (info->fChannelNum == kCommToolControlChannel)
				result = ProcessOptionStart(option, option->Label(), opcode);
			if (result == 1)
				return;
			if (result == opNotFound)
				goto notProcessed;
		}
		option = info->fCurOptPtr;
		option->SetOpCodeResult(result);
		option->SetProcessed();
		continue;

	notProcessed:
		info->fOptionsState |= kOptionsNotProcessed;
		option = info->fCurOptPtr;
		option->SetOpCodeResult(0xFC);
	}
}


// ROM 0x000701a0 ProcessOptionsComplete__9TCommToolFlP19TCommToolOptionInfo
// The options are done: a control request's that this tool did not know
// are forwarded to the tool below, if there is one (the reply comes back
// to HandleReply); then the request carries on - a get or put to its
// ...OptionsComplete, a control request by its op code.
void
TCommTool::ProcessOptionsComplete(NewtonErr result, TCommToolOptionInfo* info)
{
	if (result == noErr)
	{
		if ((info->fOptionsState & kOptionsNotProcessed) && info->fChannelNum == kCommToolControlChannel && fControlOpCode != kCommToolOpen)
		{
			TUPort* forward = ForwardOptions();
			if (forward != nil)
			{
				NewtonErr err;
				if ((info->fOptionsState & kOptionsForwardMsgAllocated) == 0)
				{
					err = kError_No_Memory;
					if ((fForwardRequest = new TCommToolOptionMgmtRequest) == nil
					||  (fForwardReply = new TCommToolReply) == nil
					||  (fForwardMsg = new TUAsyncMessage) == nil
					||  (err = InitAsyncRPCMsg(*fForwardMsg, kToolMsgForwardOpt)) != noErr)
					{
						ProcessOptionsComplete(err, info);
						return;
					}
					fForwardRequest->fOptions = info->fOptions;
					fForwardRequest->fOutside = false;
					fForwardRequest->fRequestOpCode = opProcess;
					info->fOptionsState |= kOptionsForwardMsgAllocated;
				}
				err = forward->SendRPC(fForwardMsg, fForwardRequest, sizeof(TCommToolOptionMgmtRequest), fForwardReply, sizeof(TCommToolReply), 0, nil, kCommToolRequestTypeControl);
				if (err != noErr)
				{
					ProcessOptionsComplete(err, info);
					return;
				}
				info->fOptionsState |= kOptionsForwardReqActive;
				return;
			}
		}
	}
	else
		ProcessOptionsCleanUp(result, info);

	switch (info->fChannelNum)
	{
	case kCommToolGetChannel:
		GetOptionsComplete(result);
		break;
	case kCommToolPutChannel:
		PutOptionsComplete(result);
		break;
	case kCommToolControlChannel:
		switch (fControlOpCode)
		{
		case kCommToolOpen:
			OpenOptionsComplete(result);
			break;
		case kCommToolConnect:
			ConnectOptionsComplete(result);
			break;
		case kCommToolListen:
			ListenOptionsComplete(result);
			break;
		case kCommToolAccept:
			AcceptOptionsComplete(result);
			break;
		case kCommToolBind:
			BindOptionsComplete(result);
			break;
		case kCommToolOptionMgmt:
			OptionMgmtComplete(result);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}


// ROM 0x0006d4cc ProcessOptionsCleanUp__9TCommToolFlP19TCommToolOptionInfo
// A request's options finished with: an outside array written back to the
// client's object (if all went well), a copy made of it freed, the iterator
// freed, and a control request's forwarding message given back.
NewtonErr
TCommTool::ProcessOptionsCleanUp(NewtonErr result, TCommToolOptionInfo* info)
{
	if ((info->fOptionsState & kOptionsOutside) && info->fOptions != nil && result == noErr)
		result = info->fOptions->ShadowCopyBack();
	ULong state = info->fOptionsState;
	info->fOptionsState = state & 0xFFFFFFC4;
	if (state & kOptionsArrayAllocated)
	{
		if (info->fOptions != nil)
			delete info->fOptions;
		info->fOptionsState &= ~kOptionsArrayAllocated;
	}
	info->fOptions = nil;
	if (info->fOptionsIterator != nil)
	{
		delete info->fOptionsIterator;
		info->fOptionsIterator = nil;
	}
	if (info->fChannelNum == kCommToolControlChannel)
	{
		if (fForwardRequest != nil)
		{
			delete fForwardRequest;
			fForwardRequest = nil;
		}
		if (fForwardMsg != nil)
		{
			delete fForwardMsg;
			fForwardMsg = nil;
		}
		if (fForwardReply != nil)
		{
			delete fForwardReply;
			fForwardReply = nil;
		}
	}
	return result;
}


// ROM 0x0006d5ac ForwardOptions__9TCommToolFv
// The port of the tool below this one, to hand the options it does not know
// to; the base class is the bottom of its stack.
TUPort*
TCommTool::ForwardOptions()
{
	return nil;
}


// ROM 0x0006d5b4 ProcessOptionStart__9TCommToolFP7TOptionUlT2
// The options every tool answers: the passive claim and state, the service
// id ('sid : the service and the tool's port) and the transport info.  An
// option it does not know is opNotFound, for the tool below.
ULong
TCommTool::ProcessOptionStart(TOption* theOption, ULong label, ULong opcode)
{
	if (label == 'cpcm')
	{
		TCMOPassiveClaim* option = (TCMOPassiveClaim*) theOption;
		if (opcode == opSetNegotiate || opcode == opSetRequired)
		{
			if (fToolState & kToolStateBound)
				return opFailure;
			fPassiveClaim = option->fPassiveClaim;
			return opSuccess;
		}
		if (opcode != opGetDefault)
		{
			option->fPassiveClaim = fPassiveClaim;
			return opSuccess;
		}
		TCMOPassiveClaim defaults;
		theOption->CopyDataFrom(&defaults);
		return opSuccess;
	}
	if (label == 'cpst')
	{
		TCMOPassiveState* option = (TCMOPassiveState*) theOption;
		if (opcode == opSetNegotiate || opcode == opSetRequired)
		{
			fPassiveState = option->fPassiveState;
			return opSuccess;
		}
		if (opcode != opGetDefault)
		{
			option->fPassiveState = fPassiveState;
			return opSuccess;
		}
		TCMOPassiveState defaults;
		theOption->CopyDataFrom(&defaults);
		return opSuccess;
	}
	if (label == 'sid ')
	{
		if (opcode != opGetDefault)
		{
			TCMOServiceIdentifier* option = (TCMOServiceIdentifier*) theOption;
			option->fServiceId = fServiceId;
			option->fPortId = fToolPort.fId;
			return opSuccess;
		}
		// ROM BUG: the default for 'sid is a passive claim's
		TCMOPassiveClaim defaults;
		theOption->CopyDataFrom(&defaults);
		return opSuccess;
	}
	if (label == 'tinf')
	{
		if (opcode == opSetNegotiate || opcode == opSetRequired)
			return opReadOnly;
		if (opcode != opGetCurrent)
			return opFailure;
		theOption->CopyDataFrom(&fTransportInfo);
		return opSuccess;
	}
	return opNotFound;
}


// ROM 0x0006d70c ProcessOption__9TCommToolFP7TOptionUlT2
void
TCommTool::ProcessOption(TOption* theOption, ULong label, ULong opcode)
{
}


// ROM 0x0006d710 ProcessOptionComplete__9TCommToolFUl
void
TCommTool::ProcessOptionComplete(ULong status)
{
	ProcessCommOptionComplete(status, &fOptionsInfo);
}


// ROM 0x0006d6fc ProcessPutBytesOptionStart__9TCommToolFP7TOptionUlT2
ULong
TCommTool::ProcessPutBytesOptionStart(TOption* theOption, ULong label, ULong opcode)
{
	return ProcessOptionStart(theOption, label, opcode);
}


// ROM 0x0006d728 ProcessPutBytesOptionComplete__9TCommToolFUl
void
TCommTool::ProcessPutBytesOptionComplete(ULong status)
{
	ProcessCommOptionComplete(status, &fPutOptionsInfo);
}


// ROM 0x0006d704 ProcessGetBytesOptionStart__9TCommToolFP7TOptionUlT2
ULong
TCommTool::ProcessGetBytesOptionStart(TOption* theOption, ULong label, ULong opcode)
{
	return ProcessOptionStart(theOption, label, opcode);
}


// ROM 0x0006d71c ProcessGetBytesOptionComplete__9TCommToolFUl
void
TCommTool::ProcessGetBytesOptionComplete(ULong status)
{
	ProcessCommOptionComplete(status, &fGetOptionsInfo);
}


// ROM 0x0006d734 ProcessCommOptionComplete__9TCommToolFUlP19TCommToolOptionInfo
// An option the subclass finished later: its result recorded, and on to the
// next.
void
TCommTool::ProcessCommOptionComplete(ULong status, TCommToolOptionInfo* info)
{
	TOption* option = info->fCurOptPtr;
	if (status == opNotFound)
	{
		info->fOptionsState |= kOptionsNotProcessed;
		option->SetOpCodeResult(0xFC);
	}
	else
	{
		option->SetOpCodeResult(status);
		option->SetProcessed();
	}
	ProcessOptionsContinue(info);
}


// ---------------------------------------------------------------------------
//	Starting a tool
// ---------------------------------------------------------------------------

// ROM 0x0007097c StartCommTool__FP9TCommToolUlP12TServiceInfo
NewtonErr
StartCommTool(TCommTool* commTool, ULong serviceId, TServiceInfo* serviceInfo)
{
	NewtonErr err = commTool->StartTask(true, false, 0, kCommToolStackSize, kCommToolTaskPriority, serviceId);
	if (err == noErr)
	{
		TUPort port;
		err = ServiceToPort(serviceId, &port, commTool->fChildTask);
		if (err == noErr)
		{
			serviceInfo->SetPortId(port.fId);
			serviceInfo->SetServiceId(serviceId);
		}
	}
	return err;
}


// ROM 0x0006ca84 SetPortId__12TServiceInfoFUl
void
TServiceInfo::SetPortId(TObjectId portId)
{
	fFlags = kServiceByPort;
	fPortId = portId;
}


// ROM 0x0006ca94 SetServiceId__12TServiceInfoFUl
void
TServiceInfo::SetServiceId(ULong serviceId)
{
	fServiceId = serviceId;
}


// ROM 0x0006b5c0 ServiceToPort__FUlP6TUPort
NewtonErr
ServiceToPort(ULong serviceId, TUPort* port)
{
	TUNameServer ns;
	char name[16], type[5];
	FourCharName(serviceId, type);
	sprintf(name, "%d", 0);
	TObjectId id;
	ULong spec;
	NewtonErr err = ns.Lookup(name, type, &id, &spec);
	if (err == noErr)
		port->CopyObject(id);
	return err;
}


// ROM 0x0006ba74 ServiceToPort__FUlP6TUPortT1
NewtonErr
ServiceToPort(ULong serviceId, TUPort* port, TObjectId taskId)
{
	TUNameServer ns;
	char name[16], type[5];
	FourCharName(serviceId, type);
	sprintf(name, "%d", (int) taskId);
	TObjectId id;
	ULong spec;
	NewtonErr err = ns.Lookup(name, type, &id, &spec);
	if (err == noErr)
		port->CopyObject(id);
	return err;
}
