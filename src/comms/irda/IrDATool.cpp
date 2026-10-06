/*
	File:		comms/irda/IrDATool.cpp

	Contains:	TIrDATool and TIrDAService (IrDATool.h).

	Reconstructed from the MP2x00 US ROM (0x000edde0-0x000ef35c); each
	function cites its origin.
*/

#include "IrDATool.h"
#include "IrGlue.h"
#include "IrDscInfo.h"
#include "HALOptions.h"
#include "BufferList.h"
#include "NewtErrors.h"
#include "CommErrors.h"
#include "NewtonTime.h"

// the timers' kinds beyond the link's (TIrLAP)
#define kIrListenTimer		0x29				// a listen given up

#define kIrDiscoveryTime	(2 * 60 * kSeconds)	// discovering, and listening, for so long
#define kIrListenTime		(2 * 60 * kSeconds)


// ROM 0x000edef4 __ct__9TIrDAToolFUl
TIrDATool::TIrDATool(ULong serviceId)
	: TAsyncSerTool(serviceId)
{
	fGlue = nil;
	fSIR = nil;
	fUserData = nil;
}


// ROM 0x000edfdc __dt__9TIrDAToolFv
TIrDATool::~TIrDATool()
{ }


// ROM 0x000ef240 GetSizeOf__9TIrDAToolFv
// DEVIATION (pointer size): the host's size (the ROM's 0x690).
ULong
TIrDATool::GetSizeOf()
{
	return sizeof(TIrDATool);
}


// ROM 0x000eea24 GetToolName__9TIrDAToolFv
UChar*
TIrDATool::GetToolName()
{
	return (UChar*) "IrDA";
}


// ROM 0x000eec98 TaskConstructor__9TIrDAToolFv
// The built-in IR; the SIR framer and the glue; the timers' messages; the
// user data's segment; a transport of up to 0x1fffe00 bytes a message.
NewtonErr
TIrDATool::TaskConstructor()
{
	NewtonErr err = TAsyncSerTool::TaskConstructor();
	if (err == noErr)
	{
		fSCCService = 2;
		err = -7000;
		fSIR = new TIrSIR(&fInBuf, &fOutBuf);
		if (fSIR != nil && (fGlue = new TIrGlue) != nil)
		{
			if ((err = fGlue->Init(this)) == noErr
			 && (err = fTimer1Msg.Init(true)) == noErr)
			{
				fTimer1MsgId = fTimer1Msg.GetMsgId();
				fTimer1Event.fField8 = 0;
				fTimer1Event.fAEventID = 'irda';
				fTimer1Event.fKind = 0;
				if ((err = fTimer2Msg.Init(true)) == noErr)
				{
					fTimer2MsgId = fTimer2Msg.GetMsgId();
					fTimer2Event.fKind = 0;
					fTimer2Event.fAEventID = 'irda';
					fProtocolType.protocol = 4;
					fTimer2Event.fField8 = 0;
					err = -7000;
					fUserData = new CBufferSegment;
					if (fUserData != nil
					 && (err = fUserData->Init(fConnectUserData.fData, sizeof(fConnectUserData.fData), false, 0, -1)) == noErr)
					{
						fTransportInfo.serviceType = 1;
						fTransportInfo.flags = 8;
						fTransportInfo.tdsu = 0x1fffe00;
						fTransportInfo.opt = -1;
						return noErr;
					}
				}
			}
		}
	}
	TaskDestructor();
	return err;
}


// ROM 0x000eeffc TaskDestructor__9TIrDAToolFv
void
TIrDATool::TaskDestructor()
{
	TAsyncSerTool::TaskDestructor();
	if (fGlue != nil)
	{
		delete fGlue;
		fGlue = nil;
	}
	if (fSIR != nil)
	{
		delete fSIR;
		fSIR = nil;
	}
	if (fUserData != nil)
	{
		delete fUserData;
		fUserData = nil;
	}
}


// ROM 0x000ef354 HandleInternalEvent__9TIrDAToolFv
// The stack's run queue, after every message.
void
TIrDATool::HandleInternalEvent()
{
	fGlue->HandleInternalEvent();
}


// ROM 0x000ef2fc HandleRequest__9TIrDAToolFR10TUMsgTokenUl
// A timer run out: to the link, or a listen given up.
void
TIrDATool::HandleRequest(TUMsgToken& msgToken, ULong msgType)
{
	ULong kind;
	if (msgToken.GetMsgId() == fTimer1MsgId)
	{
		kind = fTimer1Event.fKind;
		fTimer1Event.fKind = 0;
	}
	else if (msgToken.GetMsgId() == fTimer2MsgId)
	{
		kind = fTimer2Event.fKind;
		fTimer2Event.fKind = 0;
		if (kind == kIrListenTimer)
		{
			DoListenComplete(kIRErrTimeout);
			return;
		}
	}
	else
	{
		TSerTool::HandleRequest(msgToken, msgType);
		return;
	}
	fGlue->TimerComplete(kind);
}


// ROM 0x000ee708 OpenStart__9TIrDAToolFP12TOptionArray
NewtonErr
TIrDATool::OpenStart(TOptionArray* options)
{
	return TCommTool::OpenStart(options);
}


/*------------------------------------------------------------------------------
	Connecting
------------------------------------------------------------------------------*/

// ROM 0x000ee70c ConnectStart__9TIrDAToolFv
// Discoveries for two minutes, each 600 ms of listening and a tenth of a
// second a slot.
void
TIrDATool::ConnectStart()
{
	NewtonErr err = TurnOn();
	if (err != noErr)
	{
		ConnectComplete(err);
		return;
	}
	ULong slots = fDiscovery.fProbeSlots;
	fDiscoveriesLeft = kIrDiscoveryTime / (600 * kMilliseconds + slots * 100 * kMilliseconds);
	fGlue->DiscoverStart(slots, fDiscovery.fMediaBusyCheck & 0xff);
}


// ROM 0x000ee788 DoDiscoverComplete__9TIrDAToolFlP5CList
// The first device found with the service hints asked for: our class
// registered, then connected to its LSAP (given, or looked up in its IAS
// by its class); none, discovered again until the time is up.
void
TIrDATool::DoDiscoverComplete(NewtonErr result, CList* devices)
{
	if (result != noErr)
	{
		StartAbort(result);
		return;
	}
	fDiscovery.fPeerDevAddr = 0;
	for (ArrayIndex i = 0; i < devices->Count(); i++)
	{
		TIrDscInfo* device = (TIrDscInfo*) devices->At(i);
		if (device->fHints & fDiscovery.fPeerServiceHints)
		{
			fDiscovery.fPeerServiceHints = device->fHints;
			fDiscovery.fPeerDevAddr = device->fDevAddr;
			break;
		}
	}
	if (fDiscovery.fPeerDevAddr == 0)
	{
		if (--fDiscoveriesLeft > 0)
			fGlue->DiscoverStart(fDiscovery.fProbeSlots, fDiscovery.fMediaBusyCheck & 0xff);
		else
			StartAbort(kIRErrTimeout);
		return;
	}
	NewtonErr err = fGlue->RegisterMyNameAndLSAPId(fConnectionInfo.fClassNames, fConnectAttrName.fName, fConnectionInfo.fMyLSAPId);
	if (err != noErr)
	{
		StartAbort(err);
		return;
	}
	if (fConnectionInfo.fPeerLSAPId != 0)
	{
		StartConnect(fConnectionInfo.fPeerLSAPId);
		return;
	}
	// the other side's class name follows ours, word aligned
	fGlue->LSAPLookupStart(fDiscovery.fPeerDevAddr, fConnectionInfo.fClassNames + ((fConnectionInfo.fMyNameLength + 4) & ~3), fConnectAttrName.fName);
}


// ROM 0x000ee8b8 DoLSAPLookupComplete__9TIrDAToolFlUl
void
TIrDATool::DoLSAPLookupComplete(NewtonErr result, ULong lsapId)
{
	if (result == noErr)
		StartConnect(lsapId);
	else
		StartAbort(result);
}


// ROM 0x000ee8c8 StartConnect__9TIrDAToolFUl
// With the connect user data.
void
TIrDATool::StartConnect(ULong lsapId)
{
	fSecondary = false;
	fUserData->Reset();
	fUserData->Hide(sizeof(fConnectUserData.fData) - fConnectUserData.fDataLength, kSeekFromEnd);
	fGlue->ConnectStart(fDiscovery.fPeerDevAddr, lsapId, fUserData);
}


// ROM 0x000ee92c DoConnectComplete__9TIrDAToolFl
void
TIrDATool::DoConnectComplete(NewtonErr result)
{
	if (result != noErr)
	{
		StartAbort(result);
		return;
	}
	fSecondary = !fGlue->ConnectedAsPrimary();
	if (!fSecondary)
		fConnect.connectOptions |= 2;
	else
		fConnect.connectOptions &= ~2;
	UpdateOptionsAfterConnectOrListen();
	ConnectComplete(result);
}


/*------------------------------------------------------------------------------
	Listening and accepting
------------------------------------------------------------------------------*/

// ROM 0x000ee990 ListenStart__9TIrDAToolFv
// Our class registered and listened for, for two minutes.
void
TIrDATool::ListenStart()
{
	NewtonErr err = TurnOn();
	if (err != noErr)
	{
		ListenComplete(err);
		return;
	}
	fSecondary = false;
	err = fGlue->RegisterMyNameAndLSAPId(fConnectionInfo.fClassNames, fConnectAttrName.fName, fConnectionInfo.fMyLSAPId);
	if (err != noErr)
	{
		StartAbort(err);
		return;
	}
	StartTimer2(kIrListenTime, kIrListenTimer);
	fGlue->ListenStart(fUserData);
}


// ROM 0x000eea34 DoListenComplete__9TIrDAToolFl
void
TIrDATool::DoListenComplete(NewtonErr result)
{
	StopTimer2();
	if (result != noErr)
	{
		StartAbort(result);
		return;
	}
	fConnect.connectOptions &= ~2;
	UpdateOptionsAfterConnectOrListen();
	ListenComplete(result);
}


// ROM 0x000eea88 AcceptStart__9TIrDAToolFv
// With the accept's user data.
void
TIrDATool::AcceptStart()
{
	fUserData->Reset();
	fUserData->Hide(sizeof(fConnectUserData.fData) - fConnectUserData.fDataLength, kSeekFromEnd);
	fGlue->AcceptStart(fUserData);
}


// ROM 0x000eead8 DoAcceptComplete__9TIrDAToolFl
void
TIrDATool::DoAcceptComplete(NewtonErr result)
{
	if (result != noErr)
		StartAbort(result);
	else
		AcceptComplete(result);
}


// ROM 0x000eeae8 UpdateOptionsAfterConnectOrListen__9TIrDAToolFv
// What was agreed, for the options to say; the other side's user data.
void
TIrDATool::UpdateOptionsAfterConnectOrListen(void)
{
	fReceiveBuffers.fSize = fGlue->fMyQOS.GetDataSize();
	fReceiveBuffers.fCount = fGlue->fMyQOS.GetWindowSize();
	fLinkDisconnect.fTimeout = fGlue->fMyQOS.GetLinkDiscThresholdTime() / kSeconds;
	fConnectUserData.fDataLength = fUserData->Position();
}


/*------------------------------------------------------------------------------
	Gets and puts
------------------------------------------------------------------------------*/

// ROM 0x000eeb4c StartOutput__9TIrDAToolFP11CBufferList
// With the chip off the put is completed before fPutBuffer is stored, so
// DoPutComplete asks a nil buffer its position (ROM bug: see
// TAsyncSerTool::DoPutComplete, where it is fixed).
void

TIrDATool::StartOutput(CBufferList* clientBuffer)
{
	if (!fChipOn)
	{
		DoPutComplete(kSerErr_ToolNotReady);
		return;
	}
	fPutBuffer = clientBuffer;
	clientBuffer->Seek(0, kSeekFromBeginning);
	fPutSize = clientBuffer->GetSize();
	fGlue->PutStart(clientBuffer);
}


// ROM 0x000eebc4 DoPutDataComplete__9TIrDAToolFlUl
void
TIrDATool::DoPutDataComplete(NewtonErr result, ULong bytes)
{
	PutComplete(result, bytes);
}


// ROM 0x000eebcc StartInput__9TIrDAToolFP11CBufferList
// (No framed gets.)
void
TIrDATool::StartInput(CBufferList* clientBuffer)
{
	fGetBuffer = clientBuffer;
	clientBuffer->Seek(0, kSeekFromBeginning);
	fGetSize = clientBuffer->GetSize();
	if (fGetFramed)
	{
		DoGetDataComplete(kCommErrBadCommand, 0);
		return;
	}
	ULong threshold = fGetSize;
	if (fGetImmediate && (Long) fGetThresholdLeft < (Long) threshold)
		threshold = fGetThresholdLeft;
	fGlue->GetStart(clientBuffer, threshold);
}


// ROM 0x000eec64 DoGetDataComplete__9TIrDAToolFlUl
void
TIrDATool::DoGetDataComplete(NewtonErr result, ULong bytes)
{
	fGetSize -= bytes;
	GetComplete(result, false, bytes);
}


// ROM 0x000eec88 KillPut__9TIrDAToolFv
void
TIrDATool::KillPut()
{
	fGlue->CancelPutStart();
}


// ROM 0x000eec90 DoCancelPutComplete__9TIrDAToolFl
void
TIrDATool::DoCancelPutComplete(NewtonErr result)
{
	KillPutComplete(result);
}


// ROM 0x000eedf8 KillGet__9TIrDAToolFv
void
TIrDATool::KillGet()
{
	fGlue->CancelGetStart();
}


// ROM 0x000eee00 DoCancelGetComplete__9TIrDAToolFl
void
TIrDATool::DoCancelGetComplete(NewtonErr result)
{
	KillGetComplete(result);
}


/*------------------------------------------------------------------------------
	Ending
------------------------------------------------------------------------------*/

// ROM 0x000eee08 StartTerminate__9TIrDAToolFl
void
TIrDATool::StartTerminate(NewtonErr reason)
{
	StartAbort(reason);
}


// ROM 0x000eee0c TerminateConnection__9TIrDAToolFv
void
TIrDATool::TerminateConnection()
{
	fGlue->DisconnectStart(fAbortErr);
}


// ROM 0x000eee18 TerminateComplete__9TIrDAToolFv
void
TIrDATool::TerminateComplete()
{
	if (fSIR != nil)
		fSIR->Reset();
	TSerTool::TerminateComplete();
}


// ROM 0x000eee40 PostAsyncEvent__9TIrDAToolFUl
// An event for the endpoint (1: the link is going).
void
TIrDATool::PostAsyncEvent(ULong data)
{
	TCommToolGetEventReply reply;
	reply.fEventCode = 1;
	reply.fEventTime = GetGlobalTime();
	reply.fEventData = data;
	reply.fServiceId = 'irda';
	PostCommEvent(reply, noErr);
}


/*------------------------------------------------------------------------------
	The timers
------------------------------------------------------------------------------*/

// ROM 0x000eee9c StartTimer1__9TIrDAToolFUli
void
TIrDATool::StartTimer1(ULong delay, int kind)
{
	fTimer1Event.fKind = kind;
	TTime when = TimeFromNow(delay);
	fToolPort.Send(&fTimer1Msg, &fTimer1Event, sizeof(fTimer1Event), kNoTimeout, &when);
}


// ROM 0x000eef18 StopTimer1__9TIrDAToolFv
void
TIrDATool::StopTimer1(void)
{
	if (fTimer1Event.fKind == 0)
		return;
	fTimer1Msg.Abort();
	fTimer1Event.fKind = 0;
}


// ROM 0x000eef4c StartTimer2__9TIrDAToolFUli
void
TIrDATool::StartTimer2(ULong delay, int kind)
{
	fTimer2Event.fKind = kind;
	TTime when = TimeFromNow(delay);
	fToolPort.Send(&fTimer2Msg, &fTimer2Event, sizeof(fTimer2Event), kNoTimeout, &when);
}


// ROM 0x000eefc8 StopTimer2__9TIrDAToolFv
void
TIrDATool::StopTimer2(void)
{
	if (fTimer2Event.fKind == 0)
		return;
	fTimer2Msg.Abort();
	fTimer2Event.fKind = 0;
}


/*------------------------------------------------------------------------------
	The port
------------------------------------------------------------------------------*/

// ROM 0x000ef06c ChangeSpeed__9TIrDAToolFUl
InterfaceSpeed
TIrDATool::ChangeSpeed(ULong bitsPerSec)
{
	return TSerTool::ChangeSpeed(bitsPerSec);
}


// ROM 0x000ef25c AllocateBuffers__9TIrDAToolFv
// A receive buffer to take a whole frame; the port in IrDA mode.
NewtonErr
TIrDATool::AllocateBuffers()
{
	if (fBuffers.fRecvSize < fReceiveBuffers.fSize + 0x20)
		fBuffers.fRecvSize = fReceiveBuffers.fSize + 0x20;
	if (fBuffers.fSendSize < fBuffers.fRecvSize)
		fBuffers.fSendSize = fBuffers.fRecvSize;
	THMOSerIRLinkConfig config;
	config.SetOpCode(opSetRequired);
	config.fIRLinkMode = kSerIRLink_IRDA_3_16;
	fChip->ProcessOption(&config);
	return TAsyncSerTool::AllocateBuffers();
}


// ROM 0x000ef2cc SetSerialChipSelect__9TIrDAToolFP18TCMOSerialHardware
NewtonErr
TIrDATool::SetSerialChipSelect(TCMOSerialHardware* opt)
{
	NewtonErr err = TSerTool::SetSerialChipSelect(opt);
	if (fSCCService == 0)
	{
		opt->fSCCService = 2;
		fSCCService = 2;
	}
	return err;
}


// ROM 0x000ef070 StartTransmit__9TIrDAToolFP15TIrLAPPutBufferUl
void
TIrDATool::StartTransmit(TIrLAPPutBuffer* frame, ULong extraBOFs)
{
	if (fFeatures & kSerFeatureTxConfigNeeded)
		fChip->ConfigureForOutput(true);
	FlushOutputBytes();
	fSIR->StartTransmit(frame, extraBOFs);
	DoOutput();
}


// ROM 0x000ef0c4 StopTransmit__9TIrDAToolFv
// (What the port heard of itself meanwhile thrown away.)
void
TIrDATool::StopTransmit(void)
{
	fIntMask &= ~kSerIntOutputDone;
	if (fFeatures & kSerFeatureTxConfigNeeded)
		fChip->ConfigureForOutput(false);
	FlushInputBytes();
}


// ROM 0x000ef100 TxDataSent__9TIrDAToolFv
void
TIrDATool::TxDataSent()
{
	if (fOutBuf.BufferCount() == 0)
		DoOutput();
}


// ROM 0x000ef130 DoOutput__9TIrDAToolFv
void
TIrDATool::DoOutput()
{
	if (fSIR->FillOutputBuffer() == 1)
	{
		fIntMask |= kSerIntOutputDone;
		ContinueOutputST(true);
		return;
	}
	StopTransmit();
	fGlue->OutputComplete();
}


// ROM 0x000ef180 StartReceive__9TIrDAToolFP14CBufferSegmentUcT2
void
TIrDATool::StartReceive(CBufferSegment* buffer, UByte address, UByte keepLong)
{
	SetInputSendForIntDelay(0x47fe);
	fSIR->StartReceive(buffer, address, keepLong);
	DoInput();
}


// ROM 0x000ef1cc StopReceive__9TIrDAToolFv
void
TIrDATool::StopReceive(void)
{
	fIntMask &= ~kSerIntInputReady;
}


// ROM 0x000ef1dc RxDataAvailable__9TIrDAToolFv
void
TIrDATool::RxDataAvailable()
{
	DoInput();
}


// ROM 0x000ef1e4 DoInput__9TIrDAToolFv
void
TIrDATool::DoInput()
{
	fIntMask |= kSerIntInputReady;
	SyncInputBuffer();
	if (fSIR->EmptyInputBuffer() == 1)
		return;
	StopReceive();
	fGlue->InputComplete(fSIR->fRxAddress, fSIR->fRxControl);
}


// ROM 0x000ef238 MediaBusy__9TIrDAToolFv
Boolean
TIrDATool::MediaBusy(void)
{
	return fSIR->MediaBusy();
}


// ROM 0x000ef248 ReceivingInput__9TIrDAToolFv
Boolean
TIrDATool::ReceivingInput(void)
{
	return fSIR->ReceivingInput();
}


// ROM 0x000ef250 SetMediaBusy__9TIrDAToolFUc
void
TIrDATool::SetMediaBusy(UByte busy)
{
	fSIR->SetMediaBusy(busy);
}


/*------------------------------------------------------------------------------
	The options
------------------------------------------------------------------------------*/

// ROM 0x000ee03c AddDefaultOptions__9TIrDAToolFP12TOptionArray
NewtonErr
TIrDATool::AddDefaultOptions(TOptionArray* options)
{
	TCMOIrDADiscovery discovery;
	TCMOIrDAReceiveBuffers receiveBuffers;
	TCMOIrDALinkDisconnect linkDisconnect;
	TCMOIrDAConnectionInfo connectionInfo;
	TCMOIrDAConnectAttrName attrName;
	TCMOIrDAConnectUserData userData;
	TCMOSlowIRProtocolType protocolType;
	NewtonErr err = options->InsertOptionAt(options->GetArrayCount(), &discovery);
	if (err == noErr)
		err = options->InsertOptionAt(options->GetArrayCount(), &receiveBuffers);
	if (err == noErr)
		err = options->InsertOptionAt(options->GetArrayCount(), &linkDisconnect);
	if (err == noErr)
		err = options->InsertOptionAt(options->GetArrayCount(), &connectionInfo);
	if (err == noErr)
		err = options->InsertOptionAt(options->GetArrayCount(), &attrName);
	if (err == noErr)
		err = options->InsertOptionAt(options->GetArrayCount(), &userData);
	if (err == noErr)
	{
		protocolType.protocol = 4;
		err = options->InsertOptionAt(options->GetArrayCount(), &protocolType);
	}
	if (err == noErr)
		err = TAsyncSerTool::AddDefaultOptions(options);
	return err;
}


// ROM 0x000ee148 AddCurrentOptions__9TIrDAToolFP12TOptionArray
NewtonErr
TIrDATool::AddCurrentOptions(TOptionArray* options)
{
	NewtonErr err = options->InsertOptionAt(options->GetArrayCount(), &fReceiveBuffers);
	if (err == noErr)
		err = options->InsertOptionAt(options->GetArrayCount(), &fLinkDisconnect);
	if (err == noErr)
		err = options->InsertOptionAt(options->GetArrayCount(), &fDiscovery);
	if (err == noErr)
		err = options->InsertOptionAt(options->GetArrayCount(), &fConnectionInfo);
	if (err == noErr)
		err = options->InsertOptionAt(options->GetArrayCount(), &fConnectAttrName);
	if (err == noErr)
	{
		fSIR->CopyStatsTo(&fStats);
		err = options->InsertOptionAt(options->GetArrayCount(), &fStats);
	}
	if (err == noErr)
		err = options->InsertOptionAt(options->GetArrayCount(), &fConnect);
	if (err == noErr)
		err = options->InsertOptionAt(options->GetArrayCount(), &fProtocolType);
	if (err == noErr)
		err = TAsyncSerTool::AddCurrentOptions(options);
	return err;
}


// ROM 0x000ee254 ProcessOptionStart__9TIrDAToolFP7TOptionUlT2
// The IrDA options: set (checked, and the quality of service asked for
// changed to suit), their defaults, or what is current.
ULong
TIrDATool::ProcessOptionStart(TOption* theOption, ULong label, ULong opcode)
{
	Boolean set = (opcode == opSetNegotiate || opcode == opSetRequired);
	switch (label)
	{
	case kCMOIrDADiscovery:
		{
			TCMOIrDADiscovery* opt = (TCMOIrDADiscovery*) theOption;
			if (set)
			{
				if (opt->fProbeSlots != 1 && opt->fProbeSlots != 6 && opt->fProbeSlots != 8 && opt->fProbeSlots != 16)
					return opFailure;
				fGlue->fDscInfo.fHints = opt->fMyServiceHints | 2;
				return fDiscovery.CopyDataFrom(theOption);
			}
			if (opcode == opGetDefault)
			{
				TCMOIrDADiscovery defaults;
				return theOption->CopyDataFrom(&defaults);
			}
			return theOption->CopyDataFrom(&fDiscovery);
		}

	case kCMOIrDAReceiveBuffers:
		{
			TCMOIrDAReceiveBuffers* opt = (TCMOIrDAReceiveBuffers*) theOption;
			if (set)
			{
				if (fGlue->fMyQOS.SetDataSize(opt->fSize) != noErr
				 || fGlue->fMyQOS.SetWindowSize(opt->fCount) != noErr)
					return opFailure;
				return fReceiveBuffers.CopyDataFrom(theOption);
			}
			if (opcode == opGetDefault)
			{
				TCMOIrDAReceiveBuffers defaults;
				return theOption->CopyDataFrom(&defaults);
			}
			return theOption->CopyDataFrom(&fReceiveBuffers);
		}

	case kCMOIrDALinkDisconnect:
		{
			TCMOIrDALinkDisconnect* opt = (TCMOIrDALinkDisconnect*) theOption;
			if (set)
			{
				if (fGlue->fMyQOS.SetLinkDiscThresholdTime(opt->fTimeout * kSeconds) != noErr)
					return opFailure;
				return fLinkDisconnect.CopyDataFrom(theOption);
			}
			if (opcode == opGetDefault)
			{
				TCMOIrDALinkDisconnect defaults;
				return theOption->CopyDataFrom(&defaults);
			}
			return theOption->CopyDataFrom(&fLinkDisconnect);
		}

	case kCMOIrDAConnectionInfo:
		{
			TCMOIrDAConnectionInfo* opt = (TCMOIrDAConnectionInfo*) theOption;
			if (set)
			{
				if (opt->fMyNameLength + opt->fPeerNameLength + 2 > 0x3d)
					return opFailure;
				return fConnectionInfo.CopyDataFrom(theOption);
			}
			if (opcode == opGetDefault)
			{
				TCMOIrDAConnectionInfo defaults;
				return theOption->CopyDataFrom(&defaults);
			}
			return theOption->CopyDataFrom(&fConnectionInfo);
		}

	case kCMOIrDAConnectAttrName:
		{
			TCMOIrDAConnectAttrName* opt = (TCMOIrDAConnectAttrName*) theOption;
			if (set)
			{
				if (opt->fNameLength > 0x3c)
					return opFailure;
				ULong result = fConnectAttrName.CopyDataFrom(theOption);
				fConnectAttrName.fName[fConnectAttrName.fNameLength] = 0;
				return result;
			}
			if (opcode == opGetDefault)
			{
				TCMOIrDAConnectAttrName defaults;
				return theOption->CopyDataFrom(&defaults);
			}
			return theOption->CopyDataFrom(&fConnectAttrName);
		}

	case kCMOIrDAConnectUserData:
		{
			TCMOIrDAConnectUserData* opt = (TCMOIrDAConnectUserData*) theOption;
			if (set)
			{
				if (opt->fDataLength > 0x3c)
					return opFailure;
				fConnectUserData.CopyDataFrom(theOption);
				return opSuccess;				// (the copy's result is not answered)
			}
			if (opcode == opGetDefault)
			{
				TCMOIrDAConnectUserData defaults;
				return theOption->CopyDataFrom(&defaults);
			}
			return theOption->CopyDataFrom(&fConnectUserData);
		}

	case kCMOSlowIRConnect:
		if (set)
			return fConnect.CopyDataFrom(theOption);
		if (opcode == opGetDefault)
		{
			TCMOSlowIRConnect defaults;
			return theOption->CopyDataFrom(&defaults);
		}
		return theOption->CopyDataFrom(&fConnect);

	case kCMOSlowIRProtocolType:
		if (set)
		{
			ULong protocol = ((TCMOSlowIRProtocolType*) theOption)->protocol;
			if (protocol != 0 && protocol != 4)
				return opFailure;
			return fProtocolType.CopyDataFrom(theOption);
		}
		if (opcode == opGetDefault)
		{
			TCMOSlowIRProtocolType defaults;
			defaults.protocol = 4;
			return theOption->CopyDataFrom(&defaults);
		}
		return theOption->CopyDataFrom(&fProtocolType);

	case kCMOSlowIRStats:
		// the statistics, read only; reading them clears them
		if (set)
			return opReadOnly;
		if (opcode != opGetCurrent)
			return opFailure;
		fGlue->CopyStatsTo(&fStats);
		fSIR->CopyStatsTo(&fStats);
		theOption->CopyDataFrom(&fStats);
		theOption->SetOpCodeResult(0);
		fGlue->ResetStats();
		fSIR->ResetStats();
		{
			TCMOSlowIRStats cleared;
			fStats = cleared;
		}
		return opSuccess;

	case kCMOSerialBitRate:
		if (set)
		{
			if (fGlue->fMyQOS.SetBaudRate(((TCMOSerialBitRate*) theOption)->fBitsPerSecond) != noErr)
				return opFailure;
			return opSuccess;					// (nothing kept: the QOS is)
		}
		if (opcode == opGetDefault)
		{
			TCMOSerialBitRate defaults;
			return theOption->CopyDataFrom(&defaults);
		}
		((TCMOSerialBitRate*) theOption)->fBitsPerSecond = fIOParms.fSpeed;
		return opSuccess;
	}
	return TAsyncSerTool::ProcessOptionStart(theOption, label, opcode);
}


/*------------------------------------------------------------------------------
	TIrDAService ('irda')
------------------------------------------------------------------------------*/

PROTOCOL_IMPL_SOURCE_MACRO(TIrDAService)
PROTOCOL_CLASSINFO(TIrDAService, "TCMService", "serv\0irda\0\0", 0x20000, 0, nil)	// ROM 0x00382dec ClassInfo__12TIrDAServiceSFv

// ROM 0x000edde8 New__12TIrDAServiceFv
TIrDAService*
TIrDAService::New()
{
	return this;
}


// ROM 0x000eddec Delete__12TIrDAServiceFv
void
TIrDAService::Delete()
{ }


// ROM 0x000eddf0 Start__12TIrDAServiceFP12TOptionArrayUlP12TServiceInfo
// The tool already running (its port named for the service): that one;
// otherwise a tool started and opened with the endpoint's options.
NewtonErr
TIrDAService::Start(TOptionArray* options, ULong serviceId, TServiceInfo* serviceInfo)
{
	TUPort port;
	NewtonErr err = ServiceToPort(serviceId, &port);
	if (err == kError_Not_Registered)
	{
		TIrDATool tool(serviceId);
		err = StartCommTool(&tool, serviceId, serviceInfo);
		if (err != noErr)
			return err;
		err = OpenCommTool(serviceInfo->GetPortId(), options, this);
	}
	else if (err == noErr)
	{
		serviceInfo->SetPortId(port.fId);
		serviceInfo->SetServiceId(serviceId);
	}
	return err;
}


// ROM 0x000edeec DoneStarting__12TIrDAServiceFP7TAEventUlP12TServiceInfo
// What the open answered.
NewtonErr
TIrDAService::DoneStarting(TAEvent* event, ULong size, TServiceInfo* serviceInfo)
{
	return ((TCommToolReply*) event)->fResult;
}
