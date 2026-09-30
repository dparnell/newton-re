/*
	File:		comms/fax/FaxTool.cpp

	Contains:	TFaxTool's life, its requests and replies, the pages'
				buffers, the timer, the requests it sends the modem tool,
				binding and the options; the fax options and TFaxService
				(FaxTool.h).  The phases are FaxToolPhases.cpp, Class 2
				FaxToolClass2.cpp.

	Reconstructed from the MP2x00 US ROM (0x000b4a30-0x000b4eb4,
	0x000b962c-0x000b9794, 0x000bb2dc-0x000bd6a0); each function cites its
	origin.
*/

#include "FaxTool.h"
#include "CommErrors.h"
#include "NewtErrors.h"
#include "OSErrors.h"
#include "CommManagerInterface.h"
#include "OptionArray.h"
#include "NewtonMemory.h"
#include "NewtonTime.h"
#include "stores/LargeObjects.h"

#include <stdlib.h>
#include <string.h>

#define OPTION_DATA_LENGTH(cls)	(sizeof(cls) - sizeof(TOption))

// The ROM's malloc and free are the pointer heap's; the host's are the C
// library's, so the tool's blocks are NewPtr'd and DisposPtr'd as the
// ROM's are.
static void*
FaxMalloc(size_t size)
{
	return NewPtr(size);
}

static void
FaxFree(void* p)
{
	DisposPtr((Ptr) p);
}


/*------------------------------------------------------------------------------
	The fax options.
------------------------------------------------------------------------------*/

// ROM 0x000b4a30 __ct__16TCMOFaxPageSetUpFv
TCMOFaxPageSetUp::TCMOFaxPageSetUp()
	: TOption(kOptionType)
{
	SetLabel(kCMOFaxPageSetUp);
	SetLength(OPTION_DATA_LENGTH(TCMOFaxPageSetUp));
	fLength = 0;
	fWidth = 0;
	fResolution = 3;
}


// ROM 0x000b4a90 __ct__15TCMOFaxPassThruFv
TCMOFaxPassThru::TCMOFaxPassThru()
	: TOption(kOptionType)
{
	SetLabel(kCMOFaxPassThru);
	SetLength(OPTION_DATA_LENGTH(TCMOFaxPassThru));
	fPassThru = false;
}


// ROM 0x000b4ae4 __ct__26TCMOFaxEnableProgressEventFv
TCMOFaxEnableProgressEvent::TCMOFaxEnableProgressEvent()
	: TOption(kOptionType)
{
	SetLabel(kCMOFaxEnableProgressEvent);
	SetLength(OPTION_DATA_LENGTH(TCMOFaxEnableProgressEvent));
	fLines = 0;
}


// ROM 0x000b4b38 __ct__16TCMOFaxDirectionFv
TCMOFaxDirection::TCMOFaxDirection()
	: TOption(kOptionType)
{
	SetLabel(kCMOFaxDirection);
	SetLength(OPTION_DATA_LENGTH(TCMOFaxDirection));
	fSend = true;
	fReceive = false;
}


// ROM 0x000b4b94 __ct__18TCMOFaxSessionInfoFv
TCMOFaxSessionInfo::TCMOFaxSessionInfo()
	: TOption(kOptionType)
{
	SetLabel(kCMOFaxSessionInfo);
	SetLength(OPTION_DATA_LENGTH(TCMOFaxSessionInfo));
	fHorizontalRes = 0;
	fVerticalRes = 0;
	fLength = 2;
	fWidth = 0;
	fBitRate = 0;
}


// ROM 0x000b4c00 __ct__15TCMOFaxRemoteIdFv
TCMOFaxRemoteId::TCMOFaxRemoteId()
	: TOption(kOptionType)
{
	SetLabel(kCMOFaxRemoteId);
	SetLength(OPTION_DATA_LENGTH(TCMOFaxRemoteId));
	fId[0] = 0;
}


// ROM 0x000b4c54 __ct__14TCMOFaxLocalIdFv
TCMOFaxLocalId::TCMOFaxLocalId()
	: TOption(kOptionType)
{
	SetLabel(kCMOFaxLocalId);
	SetLength(OPTION_DATA_LENGTH(TCMOFaxLocalId));
	fId[0] = 0;
}


// ROM 0x000b4ca8 __ct__22TCMOFaxMinScanLineTimeFv
TCMOFaxMinScanLineTime::TCMOFaxMinScanLineTime()
	: TOption(kOptionType)
{
	SetLabel(kCMOFaxMinScanLineTime);
	SetLength(OPTION_DATA_LENGTH(TCMOFaxMinScanLineTime));
	fTime = 1000;
}


// ROM 0x000b4cfc __ct__16TCMOFaxStartPageFv
TCMOFaxStartPage::TCMOFaxStartPage()
	: TOptionExtended(kOptionType)
{
	SetLabel(kCMOFaxStartPage);
	SetLength(OPTION_DATA_LENGTH(TCMOFaxStartPage));
}


// ROM 0x000b4d48 __ct__21TCMOFaxConfigSendBandFv
TCMOFaxConfigSendBand::TCMOFaxConfigSendBand()
	: TOption(kOptionType)
{
	SetLabel(kCMOFaxConfigSendBand);
	SetLength(OPTION_DATA_LENGTH(TCMOFaxConfigSendBand));
	fLines = 0;
	fBytesPerLine = 0;
	fLeftOffset = 0;
}


// ROM 0x000b4da4 __ct__17TCMOFaxEndMessageFv
TCMOFaxEndMessage::TCMOFaxEndMessage()
	: TOptionExtended(kOptionType)
{
	SetLabel(kCMOFaxEndMessage);
	SetLength(OPTION_DATA_LENGTH(TCMOFaxEndMessage));
	fLastPage = false;
	fPageAccepted = true;
}


/*------------------------------------------------------------------------------
	TFaxService
------------------------------------------------------------------------------*/

PROTOCOL_IMPL_SOURCE_MACRO(TFaxService)
PROTOCOL_CLASSINFO(TFaxService, "TCMService", "serv\0faxs\0\0", 0x20000, 0, nil)

// ROM 0x000b4e08 New__11TFaxServiceFv
TFaxService*
TFaxService::New()
{
	return this;
}


// ROM 0x000b4e0c Delete__11TFaxServiceFv
void
TFaxService::Delete()
{ }


// ROM 0x000b4e10 Start__11TFaxServiceFP12TOptionArrayUlP12TServiceInfo
NewtonErr
TFaxService::Start(TOptionArray* options, ULong serviceId, TServiceInfo* serviceInfo)
{
	TFaxTool tool(serviceId);
	NewtonErr err = StartCommTool(&tool, serviceId, serviceInfo);
	if (err == noErr)
		err = OpenCommTool(serviceInfo->GetPortId(), options, this);
	return err;
}


// ROM 0x000b4e84 DoneStarting__11TFaxServiceFP7TAEventUlP12TServiceInfo
NewtonErr
TFaxService::DoneStarting(TAEvent* event, ULong size, TServiceInfo* serviceInfo)
{
	return ((TCommToolReply*) event)->fResult;
}


void
RegisterFaxService(void)
{
	TFaxService::ClassInfo()->Register();
}


/*------------------------------------------------------------------------------
	TFaxTool: life.
------------------------------------------------------------------------------*/

// ROM 0x000b962c __ct__8TFaxToolFUl
TFaxTool::TFaxTool(ULong serviceId)
	: TCommTool(serviceId)
{ }


// ROM 0x000bb2dc __dt__8TFaxToolFv
TFaxTool::~TFaxTool()
{ }


// ROM 0x000bb464 GetSizeOf__8TFaxToolFv
// DEVIATION (pointer size): the host's size (the ROM's 0x88c).
ULong
TFaxTool::GetSizeOf()
{
	return sizeof(TFaxTool);
}


// ROM 0x000bb514 GetToolName__8TFaxToolFv
UChar*
TFaxTool::GetToolName()
{
	return (UChar*) "Fax Tool";
}


// ROM 0x000bb808 TaskConstructor__8TFaxToolFv
NewtonErr
TFaxTool::TaskConstructor()
{
	NewtonErr err;
	if ((err = TCommTool::TaskConstructor()) != noErr)
		return err;
	fFaxFlags = 0;
	fPhaseBStep = 1;
	fPhase = kFaxPhaseA;
	fPhaseDStep = 1;
	fRetries = 3;
	fModulation = 0x40;
	fLastCommand = 0;
	fMinScanLineTime = 0;
	SetDefaultCapabilities();
	SetIdentification((const UChar*) "", fLocalIdReversed);
	fLocalId[0] = 0;
	fTCBuffer = nil;
	fTCSize = 0;
	fSendBufIndex = 0;
	fBandLines = 0;
	fBandBytesPerLine = 0;
	fLineBuffer = nil;
	fPages = 0;
	fClientBuffer = nil;
	fProgressEvent = 0;
	fReceiveBufferSize = 0x200;
	fProgressLines = 0;
	fRemoteId.SetOpCode(opGetCurrent);
	if ((err = fFrameList.Init(false)) != noErr
	||  (err = fSendList.Init(true)) != noErr)
		goto fail;
	for (int i = 0; i < 2; i++)
	{
		if ((err = fSendBufs[i].fList.Init(false)) != noErr)
			goto fail;
		fSendBufs[i].fList.InsertLast(&fSendBufs[i].fSegment);
		fSendBufs[i].fBuffer = nil;
		fSendBufs[i].fCount = 0;
		fSendBufs[i].fInUse = false;
		fSendBufs[i].fFull = false;
		if ((err = fReceiveBufs[i].fList.Init(false)) != noErr)
			goto fail;
		fReceiveBufs[i].fList.InsertLast(&fReceiveBufs[i].fSegment);
		fReceiveBufs[i].fBuffer = nil;
		fReceiveBufs[i].fCount = 0;
		fReceiveBufs[i].fInUse = false;
		fReceiveBufs[i].fFull = false;
		fDecodedLines[i] = nil;
	}
	fT4Ring = nil;
	fT4.Init(nil, 0);
	if ((err = Init()) != noErr)
		goto fail;
	C2InitSubSystem();
	return noErr;

fail:
	TaskDestructor();
	return err;
}


// ROM 0x000bc540 TaskDestructor__8TFaxToolFv
void
TFaxTool::TaskDestructor()
{
	TCommTool::TaskDestructor();
}


// ROM 0x000bb470 Init__8TFaxToolFv
// The messages the modem tool answers (the kill's refCon is set when it is
// sent), the timer's, and the received frame's buffer.
NewtonErr
TFaxTool::Init()
{
	NewtonErr err;
	if ((err = InitAsyncRPCMsg(fKillMsg, 0)) == noErr
	&&  (err = InitAsyncRPCMsg(fModemMsg, 1)) == noErr
	&&  (err = fTimerMsg.Init(true)) == noErr
	&&  (err = fFrameSegment.Init(fFrame, sizeof(fFrame), false, 0, -1)) == noErr)
		fFrameList.InsertLast(&fFrameSegment);
	return err;
}


// ROM 0x000bb528 OpenStart__8TFaxToolFP12TOptionArray
// The modem tool started, as the options name it, and its port kept.
NewtonErr
TFaxTool::OpenStart(TOptionArray* options)
{
	NewtonErr err;
	if ((err = TCommTool::OpenStart(options)) == noErr)
	{
		TServiceInfo serviceInfo;
		if ((err = CMStartServiceInternal(options, &serviceInfo)) == noErr)
			fModemPort.CopyObject(serviceInfo.GetPortId());
	}
	return err;
}


// ROM 0x000bb570 CloseComplete__8TFaxToolFl
// The modem tool closed too.
void
TFaxTool::CloseComplete(NewtonErr result)
{
	TCommToolReply reply;
	TCommToolControlRequest request;
	request.fOpCode = kCommToolClose;
	ULong replySize;
	NewtonErr err = fModemPort.SendRPC(&replySize, &request, sizeof(request), &reply, sizeof(reply), 0, kCommToolRequestTypeControl);
	TCommTool::CloseComplete(result != noErr ? result : err);
}


// ROM 0x000bb608 ConnectStart__8TFaxToolFv
void
TFaxTool::ConnectStart()
{
	if (fFaxClass == 4)
	{
		fPhase = kFaxPhaseClass2;
		C2StateUpdate(1);
	}
	else if (fFaxClass == 8)
	{
		fPhase = kFaxPhaseClass2;
		C20StateUpdate(1);
	}
	else
	{
		fFaxFlags |= kFaxFlagCaller;
		StartPhaseA();
	}
}


// ROM 0x000bb640 ListenStart__8TFaxToolFv
void
TFaxTool::ListenStart()
{
	if (fFaxClass == 4)
	{
		fPhase = kFaxPhaseClass2;
		C2StateUpdate(2);
	}
	else if (fFaxClass == 8)
	{
		fPhase = kFaxPhaseClass2;
		C20StateUpdate(2);
	}
	else
	{
		fFaxFlags &= ~kFaxFlagCaller;
		StartPhaseA();
	}
}


// ROM 0x000bb678 AcceptStart__8TFaxToolFv
// (The call was answered by the listen already.)
void
TFaxTool::AcceptStart()
{
	AcceptComplete(noErr);
}


/*------------------------------------------------------------------------------
	TFaxTool: binding - what the modem can do.
------------------------------------------------------------------------------*/

// ROM 0x000bb684 BindStart__8TFaxToolFv
// The bind passed to the modem tool, with a receive buffer of 0x2c00 bytes
// asked of the serial tool when this machine receives.
void
TFaxTool::BindStart()
{
	TCMOSerialBuffers buffers;
	NewtonErr err;
	fBindOptions = nil;
	fBindRequest.fOptions = fOptionsInfo.fOptions;
	fBindRequest.fOutside = false;
	fBindRequest.fOpCode = kCommToolBind;
	if (fLocalCaps.Word() & kT30Receiver)
	{
		if (fBindRequest.fOptions == nil)
		{
			fBindOptions = new TOptionArray;
			if (fBindOptions == nil)
			{
				err = kError_No_Memory;
				goto done;
			}
			if ((err = fBindOptions->Init()) != noErr)
				goto cleanUp;
			fBindRequest.fOptions = fBindOptions;
		}
		buffers.fRecvSize = 0x2c00;
		buffers.SetOpCode(opSetNegotiate);
		if ((err = fBindRequest.fOptions->InsertOptionAt(fBindRequest.fOptions->GetArrayCount(), &buffers)) != noErr)
			goto cleanUp;
	}
	err = fModemPort.SendRPC(&fModemMsg, &fBindRequest, sizeof(fBindRequest), &fModemReply, sizeof(fModemReply), 0, nil, kCommToolRequestTypeControl);
	if (err == noErr)
	{
		fPhase = kFaxPhaseBind;
		fBindStep = 1;
		fToolState |= kFaxToolStateModemRequest;
		return;
	}
	if (fLocalCaps.Word() & kT30Receiver)
		fBindRequest.fOptions->RemoveOptionAt(fBindRequest.fOptions->GetArrayCount() - 1);

cleanUp:
	if (fBindOptions != nil)
	{
		delete fBindOptions;
		fBindOptions = nil;
	}
done:
	BindComplete(err);
}


// The modem tool's options asked for, in fModemOptions.
static NewtonErr
SendOptionRequest(TUPort& port, TUAsyncMessage& msg, TCommToolOptionMgmtRequest& request, TOptionArray* options,
				  TClassOneModemCmdReply& reply)
{
	request.fRequestOpCode = 0x500;
	request.fOptions = options;
	request.fOutside = false;
	return port.SendRPC(&msg, &request, sizeof(request), &reply, sizeof(reply), 0, nil, kCommToolRequestTypeControl);
}


// ROM 0x000bb9d8 BindGetModemOptions__8TFaxToolFl
// Each step's answer from the modem tool, and the next question: 1 the bind
// answered - the enabled capabilities and the framing; 2 - the fax classes;
// 3 - the class set; 4 - the Class 1 modulations (5), or Class 2's +FDIS=?
// (6, 7) or Class 2.0's (0x12, 0x13).  The bind is complete when the last
// is answered.
void
TFaxTool::BindGetModemOptions(NewtonErr result)
{
	NewtonErr err = result;
	switch (fBindStep)
	{
	case 1:
		if (fLocalCaps.Word() & kT30Receiver)
		{
			if (fBindRequest.fOptions != nil)
			{
				fBindRequest.fOptions->RemoveOptionAt(fBindRequest.fOptions->GetArrayCount() - 1);
				if (result != noErr)
					break;
			}
			if (fBindOptions != nil)
			{
				delete fBindOptions;
				fBindOptions = nil;
				fBindRequest.fOptions = nil;
			}
		}
		if (result != noErr)
			break;
		{
			TCMOModemFaxEnabledCaps caps;
			TCMOFramingParms framing;
			if ((err = fModemOptions.Init()) != noErr)
				break;
			caps.SetOpCode(opGetCurrent);
			if ((err = fModemOptions.InsertOptionAt(fModemOptions.GetArrayCount(), &caps)) != noErr)
				break;
			framing.SetOpCode(opGetCurrent);
			if ((err = fModemOptions.InsertOptionAt(fModemOptions.GetArrayCount(), &framing)) != noErr)
				break;
			fBindStep = 2;
			if ((err = SendOptionRequest(fModemPort, fModemMsg, fOptionRequest, &fModemOptions, fModemReply)) != noErr)
				break;
		}
		return;

	case 2:
		if (result != noErr)
			break;
		{
			TOptionIterator iter(&fModemOptions);
			TOption* option = iter.FindOption(kCMOModemFaxEnabledCaps);
			if (option == nil)
			{
				err = kFaxToolErrBase;
				break;
			}
			if ((err = ((TOptionExtended*) option)->GetExtendedResult()) != noErr)
				break;
			if ((err = (NewtonErr) (SByte) option->GetOpCodeResults()) != noErr)
				break;
			fModemCaps.CopyDataFrom(option);
			if ((option = iter.FindOption(kCMOFramingParms)) == nil)
			{
				err = kFaxToolErrBase;
				break;
			}
			fFraming.CopyDataFrom(option);
			TCMOModemFaxClassesSupported classes;
			if ((err = fModemOptions.RemoveAllOptions()) != noErr)
				break;
			classes.SetOpCode(opGetCurrent);
			if ((err = fModemOptions.InsertOptionAt(fModemOptions.GetArrayCount(), &classes)) != noErr)
				break;
			fBindStep = 3;
			if ((err = SendOptionRequest(fModemPort, fModemMsg, fOptionRequest, &fModemOptions, fModemReply)) != noErr)
				break;
		}
		return;

	case 3:
		if (result != noErr)
			break;
		{
			TOptionIterator iter(&fModemOptions);
			TCMOModemFaxClass faxClass;
			TOption* option = iter.FindOption(kCMOModemFaxClassesSupported);
			if (option == nil)
			{
				err = kFaxToolErrBase;
				break;
			}
			if ((err = ((TOptionExtended*) option)->GetExtendedResult()) != noErr)
				break;
			if ((err = (NewtonErr) (SByte) option->GetOpCodeResults()) != noErr)
				break;
			ULong classes = ((TCMOModemFaxClassesSupported*) option)->fClasses;
			ULong cls;
			if (classes & 2)
				cls = 2;
			else if (classes & 4)
				cls = 4;
			else if (classes & 8)
				cls = 8;
			else
			{
				err = kModemErrNotSupported;
				break;
			}
			fFaxClass = cls;
			faxClass.fClass = cls;
			if ((err = fModemOptions.RemoveAllOptions()) != noErr)
				break;
			faxClass.SetOpCode(opSetRequired);
			if ((err = fModemOptions.InsertOptionAt(fModemOptions.GetArrayCount(), &faxClass)) != noErr)
				break;
			if ((err = SendOptionRequest(fModemPort, fModemMsg, fOptionRequest, &fModemOptions, fModemReply)) != noErr)
				break;
			fBindStep = 4;
		}
		return;

	case 4:
		if (result != noErr)
			break;
		if (fFaxClass == 2)
		{
			if ((err = fModemOptions.RemoveAllOptions()) != noErr)
				break;
			TCMOModemFaxClass1Cap cap;
			cap.SetOpCode(opGetCurrent);
			if ((err = fModemOptions.InsertOptionAt(fModemOptions.GetArrayCount(), &cap)) != noErr)
				break;
			if ((err = SendOptionRequest(fModemPort, fModemMsg, fOptionRequest, &fModemOptions, fModemReply)) != noErr)
				break;
			fBindStep = 5;
		}
		else if (fFaxClass == 4)
		{
			fModemRequest.fTimeout = 0;
			fBindStep = 6;
			if ((err = PostModemCommand(0x122)) != noErr)
				break;
		}
		else if (fFaxClass == 8)
		{
			fModemRequest.fTimeout = 0;
			fBindStep = 0x12;
			if ((err = PostModemCommand(0x144)) != noErr)
				break;
		}
		else
		{
			err = kModemErrNoResponse;
			break;
		}
		return;

	case 5:
		if (result != noErr)
			break;
		{
			TOptionIterator iter(&fModemOptions);
			TOption* option = iter.FindOption(kCMOModemFaxClass1Cap);
			if (option == nil)
				err = kFaxToolErrBase;
			else if ((err = ((TOptionExtended*) option)->GetExtendedResult()) != noErr)
				;
			else if ((err = (NewtonErr) (SByte) option->GetOpCodeResults()) != noErr)
				;
			else
			{
				TCMOModemFaxClass1Cap* cap = (TCMOModemFaxClass1Cap*) option;
				err = SetModemCapabilities(2, cap->fTransmitDataMods, cap->fTransmitHDLCMods,
											  cap->fReceiveDataMods, cap->fReceiveHDLCMods);
			}
		}
		// the bind is complete (or failed)
		break;

	case 6:
	case 0x12:
		if (result == kModemErrNoResponse)
			break;
		if (fModemReply.fResultCode != kModemResultOK)
		{
			// the +FDIS=? answer's text; its OK next
			if ((err = C2ParseDISResponse(fModemReply.fText, fC2ModemDIS)) != noErr
			||  (err = PostModemCommand(kModemCtlGetResult)) != noErr)
				break;
			return;
		}
		if (result != noErr)
			break;
		if (fBindStep == 6)
		{
			fBindStep = 7;
			if ((err = PostModemCommand(0x120)) != noErr)
				break;
		}
		else
		{
			fBindStep = 0x13;
			fModemRequest.fBytes[0] = '?';
			if ((err = PostModemCommand(0x158)) != noErr)
				break;
		}
		return;

	case 7:
	case 0x13:
		if (result == kModemErrNoResponse || fModemReply.fResultCode == kModemResultOK)
			break;
		if ((err = PostModemCommand(kModemCtlGetResult)) != noErr)
			break;
		return;

	default:
		return;
	}
	BindComplete(err);
}


// ROM 0x000bc1ac UnbindStart__8TFaxToolFv
void
TFaxTool::UnbindStart()
{
	fBindRequest.fOptions = fOptionsInfo.fOptions;
	fBindRequest.fOutside = false;
	fBindRequest.fOpCode = kCommToolUnbind;
	NewtonErr err = fModemPort.SendRPC(&fModemMsg, &fBindRequest, sizeof(fBindRequest), &fModemReply, sizeof(fModemReply), 0, nil, kCommToolRequestTypeControl);
	if (err == noErr)
	{
		fPhase = kFaxPhaseUnbind;
		fToolState |= kFaxToolStateModemRequest;
		return;
	}
	UnbindComplete(err);
}


// ROM 0x000b784c SetModemCapabilities__8TFaxToolFUlN41
// The modem's modulations kept, if it can be a fax machine at all: Class 2
// or 2.0 (0xc), or Class 1 (2) with V.21 for its frames both ways.
NewtonErr
TFaxTool::SetModemCapabilities(ULong classes, ULong transmitDataMods, ULong transmitHDLCMods,
							   ULong receiveDataMods, ULong receiveHDLCMods)
{
	if ((classes & 0xe) == 0)
		return kModemErrNotSupported;
	if ((classes & 0xc) == 0 && !((transmitHDLCMods & 1) && (receiveHDLCMods & 1)))
		return kModemErrNotSupported;
	fTransmitMods = transmitDataMods;
	fReceiveMods = receiveDataMods;
	return noErr;
}


// ROM 0x000b789c FastestDataRate__8TFaxToolFUl
// The T.30 data rate code (DIS/DCS bits 11-14) for the fastest of a
// modem's modulations: 0xb V.17, 3 V.29 and V.27 ter, 1 V.29, 2 V.27 ter, 0
// V.27 ter's fall-back.
ULong
TFaxTool::FastestDataRate(ULong modulations)
{
	if (modulations & 0x1000)
		return 0xb;
	if (modulations & 0x800)
		return 3;
	if ((modulations & 4) && (modulations & 0x40))
		return 3;
	if (modulations & 0x40)
		return 1;
	if (modulations & 4)
		return 2;
	return 0;
}


// ROM 0x000b7830 SetDefaultCapabilities__8TFaxToolFv
// Our DIS: V.27 ter and V.29, fine resolution, 1728 pixels, A4, 20 ms, a
// receiver and a transmitter, the extend bit clear.
void
TFaxTool::SetDefaultCapabilities()
{
	fLocalCaps.SetWord(0x0041F880);
	fLocalCaps.SetWord1(fLocalCaps.Word1() & 0x00ffffff);
}


/*------------------------------------------------------------------------------
	TFaxTool: requests, replies and the requests to the modem tool.
------------------------------------------------------------------------------*/

// ROM 0x000bc268 HandleRequest__8TFaxToolFR10TUMsgTokenUl
// The only request the tool takes on its own port is its timer's.
void
TFaxTool::HandleRequest(TUMsgToken& msgToken, ULong msgType)
{
	if (fRequest[0] != 0xd)
		CompleteRequest(msgToken, kCommErrBadCommand);
	else
		TimerComplete();
}


// ROM 0x000bc4f4 HandleReply__8TFaxToolFUlT1
void
TFaxTool::HandleReply(ULong userRefCon, ULong msgType)
{
	if (userRefCon == 1)
		ModemReqComplete();
	else if (userRefCon == 4)
		TerminateConnection();
	else if (userRefCon == 0xa)
		TimeOutKillComplete();
	else if (userRefCon == 0xe)
		KillRequestComplete(kCommToolRequestTypeControl, fKillReply.fResult);
	else
		TCommTool::HandleReply(userRefCon, msgType);
}


// ROM 0x000bc418 DoKillControl__8TFaxToolFUl
// A connection is aborted; a bind or an unbind has its request to the
// modem tool killed.  (The ROM answers whatever is in hand; nothing reads
// it.)
NewtonErr
TFaxTool::DoKillControl(ULong msgType)
{
	if (fToolState & (kToolStateConnecting | kToolStateConnected))
		return StartAbort(kCommErrRequestCanceled);
	if (fPhase != kFaxPhaseBind && fPhase != kFaxPhaseUnbind)
		return noErr;
	NewtonErr err;
	if ((err = fKillMsg.SetUserRefCon(0xe)) == noErr)
	{
		fKillRequest.fRequestsToKill = kCommToolRequestTypeControl;
		if ((err = fModemPort.SendRPC(&fKillMsg, &fKillRequest, sizeof(fKillRequest), &fKillReply, sizeof(fKillReply), 0, nil, kCommToolRequestTypeKill)) == noErr)
			return noErr;
	}
	KillRequestComplete(kCommToolRequestTypeControl, err);
	return noErr;
}


// ROM 0x000bcdb8 PostModemCommand__8TFaxToolFUl
// A control request to the modem tool, answered to ModemReqComplete.
NewtonErr
TFaxTool::PostModemCommand(ULong opCode)
{
	fModemRequest.fOpCode = opCode;
	NewtonErr err = fModemPort.SendRPC(&fModemMsg, &fModemRequest, sizeof(fModemRequest), &fModemReply, sizeof(fModemReply), 0, nil, kCommToolRequestTypeControl);
	if (err == noErr)
		fToolState |= kFaxToolStateModemRequest;
	return err;
}


// ROM 0x000bcd3c PostRecvPkt__8TFaxToolFUlT1P11CBufferListT1Uc
// A frame or a page's data received into data (reset first).
NewtonErr
TFaxTool::PostRecvPkt(ULong opCode, ULong modulation, CBufferList* data, ULong duration, Boolean final)
{
	fModemRequest.fPacket.fDuration = duration;
	fModemRequest.fPacket.fModulation = modulation;
	fModemRequest.fPacket.fData = data;
	fModemRequest.fPacket.fFinal = final;
	data->Reset();
	return PostModemCommand(opCode);
}


// ROM 0x000bcd8c PostTransPkt__8TFaxToolFUlT1P11CBufferListT1Uc
NewtonErr
TFaxTool::PostTransPkt(ULong opCode, ULong modulation, CBufferList* data, ULong duration, Boolean final)
{
	fModemRequest.fPacket.fModulation = modulation;
	fModemRequest.fPacket.fData = data;
	fModemRequest.fPacket.fDuration = duration;
	fModemRequest.fPacket.fFinal = final;
	return PostModemCommand(opCode);
}


// ROM 0x000bce48 ModemReqComplete__8TFaxToolFv
// The modem tool's answer, to the phase in hand.
void
TFaxTool::ModemReqComplete()
{
	fToolState &= ~kFaxToolStateModemRequest;
	NewtonErr result = fModemReply.fResult;
	switch (fPhase)
	{
	case kFaxPhaseA:
		PhaseAModemReqComplete(result);
		break;
	case kFaxPhaseB:
		PhaseBPktComplete(result);
		break;
	case kFaxPhaseC:
		if (fFaxClass == 8 && fC2Framing == 1)
			C2ModemReqComplete(&fModemReply);
		else
			PhaseCPktComplete(result);
		break;
	case kFaxPhaseD:
		PhaseDPktComplete(result);
		break;
	case kFaxPhaseE:
		PhaseEPktComplete(result);
		break;
	case kFaxPhaseBind:
		BindGetModemOptions(result);
		break;
	case kFaxPhaseUnbind:
		UnbindComplete(result);
		break;
	case kFaxPhaseClass2:
		C2ModemReqComplete(&fModemReply);
		break;
	}
}


// ROM 0x000bd08c KillModemRequest__8TFaxToolFUl19CommToolRequestTypeT1
// The modem tool's request of the type given killed, the answer coming to
// HandleReply with refCon; true (and stateFlag cleared) when it could not
// be asked.
Boolean
TFaxTool::KillModemRequest(ULong refCon, CommToolRequestType requestType, ULong stateFlag)
{
	if (fKillMsg.SetUserRefCon(refCon) == noErr)
	{
		fKillRequest.fRequestsToKill = requestType;
		if (fModemPort.SendRPC(&fKillMsg, &fKillRequest, sizeof(fKillRequest), &fKillReply, sizeof(fKillReply), 0, nil, kCommToolRequestTypeKill) == noErr)
			return false;
	}
	fToolState &= ~stateFlag;
	return true;
}


// ROM 0x000bcd28 TimeOutKillComplete__8TFaxToolFv
// A response that never came, killed: the command sent again.  BUG: the
// modem request's flag is cleared in fFaxFlags rather than fToolState,
// where PostModemCommand set it - the kill's own answer having cleared
// nothing, fToolState keeps kFaxToolStateModemRequest until the next
// request's answer clears it.
void
TFaxTool::TimeOutKillComplete()
{
	fFaxFlags &= ~kFaxFlagCarrierKillOwed;
	RetransCommand(10);
}


/*------------------------------------------------------------------------------
	TFaxTool: the timer.
------------------------------------------------------------------------------*/

// ROM 0x000bd298 PostTimer__8TFaxToolFUlT19TimeUnits
// The timer message (content 0xd) sent to the tool's own port after so long.
NewtonErr
TFaxTool::PostTimer(ULong type, ULong amount, TimeUnits units)
{
	fTimerType = type;
	fTimerContent = 0xd;
	TTime delay(amount, units);
	TTime when = GetGlobalTime();
	CompAdd(&delay.time, &when.time);
	fTimerTime = when;
	NewtonErr err = fToolPort.Send(&fTimerMsg, &fTimerContent, sizeof(fTimerContent), 0, &fTimerTime, 0, false);
	if (err == noErr)
		fToolState |= kFaxToolStateTimer;
	return err;
}


// ROM 0x000bd37c TimerComplete__8TFaxToolFv
void
TFaxTool::TimerComplete()
{
	fToolState &= ~kFaxToolStateTimer;
	switch (fTimerType)
	{
	case kFaxTimerCRP:
		CRPRetransmitTimeOut();
		break;
	case kFaxTimerDIS:
		DISTimeOut();
		break;
	case kFaxTimerResponse:
		ResponseTimeOut();
		break;
	case kFaxTimerLineTime:
		if (fFaxFlags & kFaxFlagSendLineWaiting)
		{
			fFaxFlags &= ~kFaxFlagSendLineWaiting;
			SendNextLine();
		}
		else if (PostTimer(kFaxTimerDataLate, 15, kSeconds) == noErr)
			fFaxFlags |= kFaxFlagBlackout;
		break;
	case kFaxTimerAbort:
		StartAbort(kFaxToolErrNoRemoteSignal);
		break;
	case kFaxTimerBlackout:
		PhaseDBlackoutTimeout();
		break;
	case kFaxTimerDataLate:
		StartAbort(kFaxToolErrTransmissionFailed);
		break;
	case kFaxTimerHangUp:
		if (CancelModemCmd())
			TerminateConnection();
		break;
	}
}


// ROM 0x000bd48c KillTimer__8TFaxToolFv
void
TFaxTool::KillTimer()
{
	fToolState &= ~kFaxToolStateTimer;
	fTimerMsg.Abort();
}


/*------------------------------------------------------------------------------
	TFaxTool: termination.
------------------------------------------------------------------------------*/

// ROM 0x000bd1e4 GetNextTermProc__8TFaxToolFUlRUlRPFPv_Uc
void
TFaxTool::GetNextTermProc(ULong terminationPhase, ULong& terminationFlag, TerminateProcPtr& terminationProc)
{
	switch (terminationPhase)
	{
	case 0:
		terminationFlag = kFaxToolStateTimer;
		terminationProc = (TerminateProcPtr) CancelTimer;
		break;
	case 1:
		terminationFlag = kFaxToolStateModemRequest;
		terminationProc = (TerminateProcPtr) CancelModemCmd;
		break;
	case 2:
		terminationFlag = kFaxToolStatePhaseE;
		terminationProc = (TerminateProcPtr) StartPhaseE;
		break;
	case 3:
		terminationFlag = kFaxToolStateHangUp;
		terminationProc = (TerminateProcPtr) HangUp;
		break;
	default:
		terminationFlag = 0;
		terminationProc = nil;
		break;
	}
}


Boolean
TFaxTool::CancelTimer(void* tool)
{
	return ((TFaxTool*) tool)->CancelTimer();
}

Boolean
TFaxTool::CancelModemCmd(void* tool)
{
	return ((TFaxTool*) tool)->CancelModemCmd();
}

Boolean
TFaxTool::StartPhaseE(void* tool)
{
	return ((TFaxTool*) tool)->StartPhaseE();
}

Boolean
TFaxTool::HangUp(void* tool)
{
	return ((TFaxTool*) tool)->HangUp();
}


// ROM 0x000bd008 CancelTimer__8TFaxToolFv
Boolean
TFaxTool::CancelTimer()
{
	KillTimer();
	return true;
}


// ROM 0x000bd020 CancelModemCmd__8TFaxToolFv
// The modem tool's control request killed, the answer (refCon 4) going on
// with the termination.
Boolean
TFaxTool::CancelModemCmd()
{
	return KillModemRequest(4, kCommToolRequestTypeControl, kFaxToolStateModemRequest);
}


// ROM 0x000bd144 HangUp__8TFaxToolFv
// The modem tool disconnected (answered in phase E: TerminateConnection).
Boolean
TFaxTool::HangUp()
{
	fPhase = kFaxPhaseE;
	fToolState &= ~kFaxToolStateHangUp;
	fModemRequest.fOpCode = kCommToolDisconnect;
	NewtonErr err = fModemPort.SendRPC(&fModemMsg, &fModemRequest, sizeof(fModemRequest), &fModemReply, sizeof(fModemReply), 0, nil, kCommToolRequestTypeControl);
	return err != noErr;
}


// ROM 0x000bd250 TerminateComplete__8TFaxToolFv
void
TFaxTool::TerminateComplete()
{
	fSendList.DeleteAll();
	FreeLineBuffers();
	FreeReceiveBuffers();
	FreeTCBuffer();
	C2InitSubSystem();
	TCommTool::TerminateComplete();
}


/*------------------------------------------------------------------------------
	TFaxTool: identities and frames.
------------------------------------------------------------------------------*/

// ROM 0x000b78dc GetIdentification__8TFaxToolFPCUcCPUcUl
// A CSI/TSI FIF (20 characters, the last first) as a C string, the spaces
// it was padded with dropped; nothing unless the FIF is 20 characters (22
// with an FCS).  BUG: an FIF of spaces alone has the scan run on before
// its start, into whatever precedes it.
void
TFaxTool::GetIdentification(const UChar* from, UChar* to, ULong length)
{
	if (length != 0x14 && length != 0x16)
	{
		to[0] = 0;
		return;
	}
	long i = 19;
	while (from[i] == ' ')
		i--;
	long j = 0;
	for ( ; i >= 0; i--)
		to[j++] = from[i];
	to[j] = 0;
}


// ROM 0x000b7934 SetIdentification__8TFaxToolFCPCUcCPUc
// A C string as a CSI/TSI FIF: backwards, padded to 20 with spaces.
void
TFaxTool::SetIdentification(const UChar* from, UChar* to)
{
	long length = strlen((const char*) from);
	long i = 19;
	if (length <= 19)
		for ( ; i >= length; i--)
			to[i] = ' ';
	for (long j = 0; i >= 0; i--, j++)
		to[i] = from[j];
}


// ROM 0x000bcf04 BuildControlFrame__8TFaxToolFUcPUcUlT1
// A frame to send in fSendList: the address, the control field (final or
// not) and the FCF, then the FIF, each a segment of its own.
NewtonErr
TFaxTool::BuildControlFrame(UChar fcf, UChar* fif, ULong fifLength, Boolean final)
{
	fSendList.DeleteAll();
	fFrameHeader[0] = 0xff;
	fFrameHeader[1] = final ? 0x13 : 0x03;
	fFrameHeader[2] = fcf;
	CBufferSegment* segment = new CBufferSegment;
	if (segment == nil)
		return kError_No_Memory;
	NewtonErr err;
	if ((err = segment->Init(fFrameHeader, 3, false, 0, -1)) != noErr)
		return err;
	fSendList.InsertLast(segment);
	if (fif != nil && fifLength != 0)
	{
		if ((segment = new CBufferSegment) == nil)
			return kError_No_Memory;
		if ((err = segment->Init(fif, fifLength, false, 0, -1)) == noErr)
			fSendList.InsertLast(segment);
	}
	return err;
}


/*------------------------------------------------------------------------------
	TFaxTool: the page's buffers.
------------------------------------------------------------------------------*/

// ROM 0x000bc728 AllocateLineBuffers__8TFaxToolFv
// The two send buffers (twice a line buffer each) and a line of the band.
NewtonErr
TFaxTool::AllocateLineBuffers()
{
	NewtonErr err = noErr;
	for (int i = 0; i < 2; i++)
	{
		TFaxLineBuf& buf = fSendBufs[i];
		if ((buf.fBuffer = (UChar*) FaxMalloc(fLineBufferSize * 2)) == nil)
			return kError_No_Memory;
		if ((err = buf.fSegment.Init(buf.fBuffer, fLineBufferSize * 2, false, 0, -1)) != noErr)
			return err;
		buf.fList.Reset();
	}
	if (fLineBuffer == nil)
	{
		if ((fLineBuffer = (UChar*) FaxMalloc(fBytesPerLine)) == nil)
			return kError_No_Memory;
	}
	return err;
}


// ROM 0x000bd030 FreeLineBuffers__8TFaxToolFv
void
TFaxTool::FreeLineBuffers()
{
	for (int i = 0; i < 2; i++)
		if (fSendBufs[i].fBuffer != nil)
		{
			FaxFree(fSendBufs[i].fBuffer);
			fSendBufs[i].fBuffer = nil;
		}
	if (fLineBuffer != nil)
	{
		FaxFree(fLineBuffer);
		fLineBuffer = nil;
	}
}


// ROM 0x000bd5cc AllocateReceiveBuffers__8TFaxToolFv
// The two receive buffers, two decoded lines and the decoder's ring.
NewtonErr
TFaxTool::AllocateReceiveBuffers()
{
	NewtonErr err = noErr;
	for (int i = 0; i < 2; i++)
	{
		TFaxLineBuf& buf = fReceiveBufs[i];
		if ((buf.fBuffer = (UChar*) FaxMalloc(fReceiveBufferSize)) == nil)
			return kError_No_Memory;
		if ((err = buf.fSegment.Init(buf.fBuffer, fReceiveBufferSize, false, 0, -1)) != noErr)
			return err;
		buf.fList.Reset();
		if ((fDecodedLines[i] = (UChar*) FaxMalloc(fBytesPerLine)) == nil)
			return kError_No_Memory;
	}
	if ((fT4Ring = (UChar*) FaxMalloc(fLineBufferSize)) == nil)
		return kError_No_Memory;
	fT4.Init(fT4Ring, fLineBufferSize);
	return err;
}


// ROM 0x000bb3dc FreeReceiveBuffers__8TFaxToolFv
void
TFaxTool::FreeReceiveBuffers()
{
	for (int i = 0; i < 2; i++)
	{
		if (fReceiveBufs[i].fBuffer != nil)
		{
			FaxFree(fReceiveBufs[i].fBuffer);
			fReceiveBufs[i].fBuffer = nil;
		}
		if (fDecodedLines[i] != nil)
		{
			FaxFree(fDecodedLines[i]);
			fDecodedLines[i] = nil;
		}
	}
	if (fT4Ring != nil)
	{
		FaxFree(fT4Ring);
		fT4Ring = nil;
		fT4.Init(nil, 0);
	}
}


// ROM 0x000bd4a4 FigureTCSize__8TFaxToolFUl
// The training check's bytes: a second and a half of the speed (0 for a
// speed it does not know).
ULong
TFaxTool::FigureTCSize(ULong bitRate)
{
	switch (bitRate)
	{
	case 9600:	return 1800;
	case 2400:	return 450;
	case 4800:	return 900;
	case 7200:	return 1350;
	case 12000:	return 2250;
	case 14400:	return 2700;
	}
	return 0;
}


// ROM 0x000bd518 AllocateTCBuffer__8TFaxToolFUc
// A buffer for the training check at fBitRate, a quarter bigger with extra
// (receiving, for a check that runs long).
NewtonErr
TFaxTool::AllocateTCBuffer(UChar extra)
{
	NewtonErr err = noErr;
	ULong size = FigureTCSize(fBitRate);
	if (size == 0)
		return kFaxToolErrProtocolError;
	if (extra)
		size += size >> 2;
	if (fTCBuffer != nil && fTCSize < size)
		FreeTCBuffer();
	fTCSize = size;
	if (fTCBuffer == nil)
	{
		if ((fTCBuffer = (UChar*) NewPtrClear(size)) == nil)
			err = MemError();
	}
	return err;
}


// ROM 0x000bd5a0 FreeTCBuffer__8TFaxToolFv
void
TFaxTool::FreeTCBuffer()
{
	if (fTCBuffer != nil)
	{
		FaxFree(fTCBuffer);
		fTCBuffer = nil;
	}
}


/*------------------------------------------------------------------------------
	TFaxTool: the client's gets and puts, and its pages.
------------------------------------------------------------------------------*/

// ROM 0x000bc544 PutBytes__8TFaxToolFP11CBufferList
// A band of the page's scan lines.
void
TFaxTool::PutBytes(CBufferList* clientBuffer)
{
	if (!(fFaxFlags & kFaxFlagPageStarted))
	{
		StartAbort(kFaxToolErrNotSendingPage);
		return;
	}
	if (fToolState & kToolStateWantAbort)
		return;
	fClientBuffer = clientBuffer;
	fLinesLeft = fBandLines;
	SendNextLine();
}


// ROM 0x000bc574 PutFramedBytes__8TFaxToolFP11CBufferListUc
void
TFaxTool::PutFramedBytes(CBufferList* clientBuffer, Boolean endOfFrame)
{
	PutComplete(kCommErrBadCommand, 0);
}


// ROM 0x000bc588 PutComplete__8TFaxToolFlUl
void
TFaxTool::PutComplete(NewtonErr result, ULong putBytesCount)
{
	fClientBuffer = nil;
	TCommTool::PutComplete(result, putBytesCount);
}


// ROM 0x000bc594 KillPut__8TFaxToolFv
void
TFaxTool::KillPut()
{
	if (fClientBuffer == nil)
	{
		KillPutComplete(kCommErrNoRequestPending);
		return;
	}
	PutComplete(kCommErrRequestCanceled, fClientBuffer->GetSize());
	KillPutComplete(noErr);
}


// ROM 0x000bc5f8 GetBytes__8TFaxToolFP11CBufferList
void
TFaxTool::GetBytes(CBufferList* clientBuffer)
{
	GetComplete(kCommErrBadCommand, false, 0);
}


// ROM 0x000bc610 GetFramedBytes__8TFaxToolFP11CBufferList
// As many of the received page's lines as the client's buffer takes.
void
TFaxTool::GetFramedBytes(CBufferList* clientBuffer)
{
	NewtonErr err;
	if (!(fFaxFlags & kFaxFlagReceive))
		err = kFaxToolErrNotSendingPage;
	else
	{
		if (fToolState & kToolStateWantAbort)
			return;
		fClientBuffer = clientBuffer;
		fLinesLeft = clientBuffer->GetSize() / fBytesPerLine;
		if ((err = DecodeLinesBuf()) == noErr)
			return;
	}
	GetComplete(err, false, 0);
}


// ROM 0x000bc694 GetComplete__8TFaxToolFlUcUl
void
TFaxTool::GetComplete(NewtonErr result, Boolean endOfFrame, ULong getBytesCount)
{
	fClientBuffer = nil;
	TCommTool::GetComplete(result, endOfFrame, getBytesCount);
}


// ROM 0x000bc6a4 KillGet__8TFaxToolFv
void
TFaxTool::KillGet()
{
	if (fClientBuffer == nil)
	{
		KillGetComplete(kCommErrNoRequestPending);
		return;
	}
	GetComplete(kCommErrRequestCanceled, fReceiveState == 3, fClientBuffer->GetSize());
	KillGetComplete(noErr);
}


// ROM 0x000bc280 DoStartPage__8TFaxToolFv
// 'fsgp': a page to send (or receive) - phase C, or a retrain first after
// the last page's RTN.
NewtonErr
TFaxTool::DoStartPage()
{
	if (!(fToolState & kToolStateConnected))
		return kCommErrNotConnected;
	if (fFaxFlags & kFaxFlagPageStarted)
		return kCommErrCommandInProgress;
	if (fToolState & kToolStateWantAbort)
		return noErr;
	fFaxFlags &= ~kFaxFlagLastPage;
	if (fFaxClass == 4)
		C2StateUpdate(5);
	else if (fFaxClass == 8)
		C20StateUpdate(5);
	else if (fPages == 0 || fPhaseDStep == 9)
		StartPhaseC();
	else if (fPhaseDStep == 8)
	{
		fRetries = 3;
		fTrainTries = 1;
		fPhase = kFaxPhaseB;
		fPhaseBStep = 4;
		PutCommandToRcv(1);
	}
	else
		return kCommErrBadCommand;
	return noErr;
}


// ROM 0x000bc348 StartPageComplete__8TFaxToolFl
void
TFaxTool::StartPageComplete(NewtonErr result)
{
	((TOptionExtended*) fOptionsInfo.fCurOptPtr)->SetExtendedResult(result);
	if (result == noErr)
	{
		fFaxFlags |= kFaxFlagPageStarted;
		ProcessOptionComplete(opSuccess);
	}
	else
		ProcessOptionComplete(opFailure);
}


// ROM 0x000bc384 DoEndPage__8TFaxToolFv
// 'feom' sending: the page's RTC, then its post-message command - EOP if
// the option says it is the last.
NewtonErr
TFaxTool::DoEndPage()
{
	if (!(fToolState & kToolStateConnected))
		return kCommErrNotConnected;
	if (!(fFaxFlags & kFaxFlagPageStarted))
		return kFaxToolErrNotSendingPage;
	if (fToolState & kToolStateWantAbort)
		return noErr;
	fFaxFlags = (fFaxFlags & ~kFaxFlagPageStarted) | kFaxFlagEndPage;
	if (((TCMOFaxEndMessage*) fOptionsInfo.fCurOptPtr)->fLastPage)
		fFaxFlags |= kFaxFlagLastPage;
	SendEOM();
	return noErr;
}


// ROM 0x000bc3f4 EndPageComplete__8TFaxToolFl
void
TFaxTool::EndPageComplete(NewtonErr result)
{
	((TOptionExtended*) fOptionsInfo.fCurOptPtr)->SetExtendedResult(result);
	ProcessOptionComplete(result != noErr ? opFailure : opSuccess);
}


// ROM 0x000b6bfc PostFaxEvent__8TFaxToolFv
// The progress event (6: so many lines received), if there is one to post.
NewtonErr
TFaxTool::PostFaxEvent()
{
	if (fProgressEvent == 0)
		return kCommErrNoEventPending;
	TCommToolGetEventReply event;
	event.fEventCode = 6;
	event.fEventTime = TTime((ULong) 0, (ULong) 0);
	event.fEventData = fProgressEvent;
	event.fServiceId = fServiceId;
	if (PostCommEvent(event, noErr) == noErr)
		fProgressEvent = 0;
	return noErr;
}


// ROM 0x000b6b98 GetCommEvent__8TFaxToolFv
void
TFaxTool::GetCommEvent()
{
	if (PostFaxEvent() == kCommErrNoEventPending)
		TCommTool::GetCommEvent();
}


/*------------------------------------------------------------------------------
	TFaxTool: options.
------------------------------------------------------------------------------*/

// ROM 0x000bc718 ForwardOptions__8TFaxToolFv
// Options the tool does not know go on to the modem tool.
TUPort*
TFaxTool::ForwardOptions()
{
	return &fModemPort;
}


// ROM 0x000bc720 ProcessPutBytesOptionStart__8TFaxToolFP7TOptionUlT2
ULong
TFaxTool::ProcessPutBytesOptionStart(TOption* theOption, ULong label, ULong opcode)
{
	return ProcessOptionStart(theOption, label, opcode);
}


// ROM 0x000bc7dc ProcessGetBytesOptionStart__8TFaxToolFP7TOptionUlT2
ULong
TFaxTool::ProcessGetBytesOptionStart(TOption* theOption, ULong label, ULong opcode)
{
	return ProcessOptionStart(theOption, label, opcode);
}


// ROM 0x000bc7e4 ProcessOptionStart__8TFaxToolFP7TOptionUlT2
// The fax options, set (opSetNegotiate, opSetRequired) or read (the
// default, or the current).  'fsgp' and 'feom' set are answered later
// (opInProgress).
ULong
TFaxTool::ProcessOptionStart(TOption* theOption, ULong label, ULong opcode)
{
	ULong status = opSuccess;
	Boolean set = (opcode == opSetNegotiate || opcode == opSetRequired);
	switch (label)
	{
	case kCMOFaxDirection:
		if (set)
		{
			fDirection.CopyDataFrom(theOption);
			ULong word = fLocalCaps.Word() & ~kT30Receiver;
			word |= (fDirection.fReceive & 1) << 17;
			word &= ~kT30Transmitter;
			word |= (fDirection.fSend & 1) << 16;
			fLocalCaps.SetWord(word);
		}
		else if (opcode == opGetDefault)
		{
			TCMOFaxDirection defaults;
			theOption->CopyDataFrom(&defaults);
		}
		else
			theOption->CopyDataFrom(&fDirection);
		break;

	case kCMOFaxLocalId:
		{
			UChar* id = ((TCMOFaxLocalId*) theOption)->fId;
			if (set)
			{
				SetIdentification(id, fLocalIdReversed);
				strncpy((char*) fLocalId, (const char*) id, 0x14);
				fLocalId[0x14] = 0;
			}
			else if (opcode == opGetDefault)
			{
				TCMOFaxLocalId defaults;
				theOption->CopyDataFrom(&defaults);
			}
			else
				GetIdentification(fLocalIdReversed, id, 0x14);
		}
		break;

	case kCMOFaxPageSetUp:
		{
			TCMOFaxPageSetUp* setUp = (TCMOFaxPageSetUp*) theOption;
			if (set)
			{
				ULong word = fLocalCaps.Word();
				word = (word & ~kT30Length) | ((setUp->fLength & 3) << kT30LengthShift);
				word = (word & ~kT30Width) | ((setUp->fWidth & 3) << kT30WidthShift);
				word = (word & ~kT30FineResolution) | (((setUp->fResolution & 2) != 0) << 22);
				fLocalCaps.SetWord(word);
			}
			else if (opcode == opGetDefault)
			{
				TCMOFaxPageSetUp defaults;
				theOption->CopyDataFrom(&defaults);
			}
			else
			{
				setUp->fLength = (fLocalCaps.Word() & kT30Length) >> kT30LengthShift;
				setUp->fWidth = (fLocalCaps.Word() & kT30Width) >> kT30WidthShift;
				setUp->fResolution = (fLocalCaps.Word() & kT30FineResolution) ? 3 : 1;
			}
		}
		break;

	case kCMOFaxRemoteId:
		if (set)
			status = opFailure;
		else if (opcode == opGetDefault)
		{
			TCMOFaxRemoteId defaults;
			theOption->CopyDataFrom(&defaults);
		}
		else
			theOption->CopyDataFrom(&fRemoteId);
		break;

	case kCMOFaxMinScanLineTime:
		if (set)
		{
			ULong time = ((TCMOFaxMinScanLineTime*) theOption)->fTime;
			if (time > 0x1388)
			{
				status = opFailure;
				break;
			}
			fMinScanLineTime = time;
			// a line waiting on the old time goes now
			if ((fToolState & kFaxToolStateTimer) && fTimerType == kFaxTimerLineTime)
			{
				KillTimer();
				TimerComplete();
			}
		}
		else if (opcode == opGetDefault)
		{
			TCMOFaxMinScanLineTime defaults;
			theOption->CopyDataFrom(&defaults);
		}
		else
			((TCMOFaxMinScanLineTime*) theOption)->fTime = fMinScanLineTime;
		break;

	case kCMOFaxConfigSendBand:
		{
			TCMOFaxConfigSendBand* band = (TCMOFaxConfigSendBand*) theOption;
			if (set)
			{
				fBandLines = band->fLines;
				fBandBytesPerLine = band->fBytesPerLine;
				fBandLeftOffset = band->fLeftOffset;
				if (fBandLines == 0 || fBandBytesPerLine > fBytesPerLine)
					status = opFailure;
			}
			else if (opcode == opGetDefault)
			{
				TCMOFaxConfigSendBand defaults;
				theOption->CopyDataFrom(&defaults);
			}
			else
			{
				band->fLines = fBandLines;
				band->fBytesPerLine = fBandBytesPerLine;
				band->fLeftOffset = fBandLeftOffset;
			}
		}
		break;

	case kCMOFaxStartPage:
		if (set)
		{
			NewtonErr err = DoStartPage();
			if (err == noErr)
				status = opInProgress;
			else
			{
				((TOptionExtended*) theOption)->SetExtendedResult(err);
				status = opFailure;
			}
		}
		else
		{
			TCMOFaxStartPage defaults;
			theOption->CopyDataFrom(&defaults);
		}
		break;

	case kCMOFaxEndMessage:
		if (set)
		{
			NewtonErr err = noErr;
			if (fFaxFlags & kFaxFlagSend)
			{
				if (!(fToolState & kToolStateConnected))
					err = kCommErrNotConnected;
				if (!(fFaxFlags & kFaxFlagPageStarted))
					err = kFaxToolErrNotSendingPage;
			}
			else if (!(fToolState & kToolStateConnected) || !(fFaxFlags & kFaxFlagPageReceived))
				err = kCommErrNotConnected;
			if (err != noErr)
			{
				((TOptionExtended*) theOption)->SetExtendedResult(err);
				status = opFailure;
				break;
			}
			status = opInProgress;
			fFaxFlags |= kFaxFlagEndMessage;
			if (fFaxClass == 2)
			{
				if (fFaxFlags & kFaxFlagSend)
					DoEndPage();
				else
				{
					fFaxFlags &= ~kFaxFlagPageReceived;
					if (fFaxFlags & kFaxFlagPostMessage)
						PhaseDProcessReceivedPageConfirmation();
				}
			}
			else if (fFaxClass == 4)
				C2StateUpdate(6);
			else if (fFaxClass == 8)
				C20StateUpdate(6);
			else
				status = opFailure;
		}
		else
		{
			// BUG: the default read is a start page's, not an end message's
			TCMOFaxStartPage defaults;
			theOption->CopyDataFrom(&defaults);
		}
		break;

	case kCMOFaxEnableProgressEvent:
		if (set)
			fProgressLines = ((TCMOFaxEnableProgressEvent*) theOption)->fLines;
		else if (opcode == opGetDefault)
		{
			TCMOFaxEnableProgressEvent defaults;
			theOption->CopyDataFrom(&defaults);
		}
		else
			((TCMOFaxEnableProgressEvent*) theOption)->fLines = fProgressLines;
		break;

	case kCMOModemFaxCapabilities:
		if (set)
			fModemCaps.CopyDataFrom(theOption);
		else if (opcode == opGetDefault)
		{
			TCMOModemFaxCapabilities defaults;
			theOption->CopyDataFrom(&defaults);
		}
		else
			theOption->CopyDataFrom(&fModemCaps);
		break;

	default:
		return TCommTool::ProcessOptionStart(theOption, label, opcode);
	}
	return status;
}
