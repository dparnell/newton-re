/*
	File:		comms/IrProbeTool.cpp

	Contains:	TIrProbeTool and IRProbeService (IrProbeTool.h).

	Reconstructed from the MP2x00 US ROM (0x000f6fe0-0x000f7e74,
	0x000e89d8-0x000e8a60); each function cites its origin.
*/

#include "IrProbeTool.h"
#include "HALOptions.h"
#include "NewtErrors.h"
#include "CommErrors.h"
#include "NewtonTime.h"

// what the timers are timing (the events they are)
#define kProbeListenTick	2			// half a second listening
#define kProbeIrDAReply		0xc			// a tenth of a second for the TEST frame's echo
#define kProbeSharpReply	0x11		// and for a Sharp answer

// the words of a TEST frame lie big-endian in the frame, as on the air
static ULong
GetWordBE(const UByte* p)
{
	return ((ULong) p[0] << 24) | ((ULong) p[1] << 16) | ((ULong) p[2] << 8) | p[3];
}

static void
SetWordBE(UByte* p, ULong w)
{
	p[0] = (UByte) (w >> 24);
	p[1] = (UByte) (w >> 16);
	p[2] = (UByte) (w >> 8);
	p[3] = (UByte) w;
}


// ROM 0x000f6fe0 __ct__12TIrProbeToolFUl
TIrProbeTool::TIrProbeTool(ULong serviceId)
	: TAsyncSerTool(serviceId)
{
	fSIR = nil;
	fLink = 0;
}


// ROM 0x000f7070 __dt__12TIrProbeToolFv
TIrProbeTool::~TIrProbeTool()
{ }


// ROM 0x000f7da0 GetSizeOf__12TIrProbeToolFv
// DEVIATION (pointer size): the host's size (the ROM's 0x5bc).
ULong
TIrProbeTool::GetSizeOf()
{
	return sizeof(TIrProbeTool);
}


// ROM 0x000f72ec GetToolName__12TIrProbeToolFv
UChar*
TIrProbeTool::GetToolName()
{
	return (UChar*) "IrProbe";
}


// ROM 0x000f7a24 TaskConstructor__12TIrProbeToolFv
// The built-in IR; no put or get of the client's ever (-1 stands in); input
// looked at after 10 ms; the SIR framer over the tool's buffers, the
// timer's message and the receive segment over the packet.
NewtonErr
TIrProbeTool::TaskConstructor()
{
	NewtonErr err = TAsyncSerTool::TaskConstructor();
	if (err == noErr)
	{
		fProtocolType.protocol = 0;
		fSCCService = 2;
		fGetBuffer = (CBufferList*) -1;
		fProtocolType.options = 0;
		fPutBuffer = (CBufferList*) -1;
		SetInputSendForIntDelay(0x8ffc);
		fBuffer = fPacket;
		fSIR = new TIrSIR(&fInBuf, &fOutBuf);
		if (fSIR == nil)
			err = -7000;
		else
		{
			err = fTimerMsg.Init(true);
			if (err == noErr)
			{
				fTimerMsgId = fTimerMsg.GetMsgId();
				fTimerEvent.fAEventID = 'slir';
				fTimerEvent.fField8 = 0;
				fTimerEvent.fKind = 0;
				err = fRxSegment.Init(fBuffer, sizeof(fPacket), false, 0, -1);
				if (err == noErr)
					return noErr;
			}
		}
	}
	TaskDestructor();
	return err;
}


// ROM 0x000f7d68 TaskDestructor__12TIrProbeToolFv
void
TIrProbeTool::TaskDestructor()
{
	TAsyncSerTool::TaskDestructor();
	if (fSIR != nil)
	{
		delete fSIR;
		fSIR = nil;
	}
}


// ROM 0x000f72cc HandleRequest__12TIrProbeToolFR10TUMsgTokenUl
void
TIrProbeTool::HandleRequest(TUMsgToken& msgToken, ULong msgType)
{
	if (msgToken.GetMsgId() != fTimerMsgId)
	{
		TSerTool::HandleRequest(msgToken, msgType);
		return;
	}
	ULong kind = fTimerEvent.fKind;
	fTimerEvent.fKind = 0;
	TimerComplete(kind);
}


// ROM 0x000f7158 OpenStart__12TIrProbeToolFP12TOptionArray
NewtonErr
TIrProbeTool::OpenStart(TOptionArray* options)
{
	return TCommTool::OpenStart(options);
}


// ROM 0x000f715c ConnectStart__12TIrProbeToolFv
void
TIrProbeTool::ConnectStart()
{
	NewtonErr err = TurnOn();
	if (err == noErr)
		NextState(6);
	else
		ConnectComplete(err);
}


// ROM 0x000f71a0 ListenStart__12TIrProbeToolFv
void
TIrProbeTool::ListenStart()
{
	NewtonErr err = TurnOn();
	if (err == noErr)
		NextState(0);
	else
		ListenComplete(err);
}


// ROM 0x000f71e4 TerminateConnection__12TIrProbeToolFv
void
TIrProbeTool::TerminateConnection()
{
	StopTimer();
	StopReceive();
	StopTransmit();
	TerminateComplete();
}


// ROM 0x000f7218 TerminateComplete__12TIrProbeToolFv
void
TIrProbeTool::TerminateComplete()
{
	TSerTool::TerminateComplete();
}


// ROM 0x000f711c ProcessOptionStart__12TIrProbeToolFP7TOptionUlT2
// 'irpt': the answer, read only.
ULong
TIrProbeTool::ProcessOptionStart(TOption* theOption, ULong label, ULong opcode)
{
	if (label != kCMOSlowIRProtocolType)
		return TAsyncSerTool::ProcessOptionStart(theOption, label, opcode);
	if (opcode == opSetNegotiate || opcode == opSetRequired)
		return opReadOnly;
	if (opcode != opGetCurrent)
		return opFailure;
	return theOption->CopyDataFrom(&fProtocolType);
}


// ROM 0x000f7e70 AddDefaultOptions__12TIrProbeToolFP12TOptionArray
NewtonErr
TIrProbeTool::AddDefaultOptions(TOptionArray* options)
{
	return TAsyncSerTool::AddDefaultOptions(options);
}


// ROM 0x000f70e0 AddCurrentOptions__12TIrProbeToolFP12TOptionArray
NewtonErr
TIrProbeTool::AddCurrentOptions(TOptionArray* options)
{
	NewtonErr err = options->InsertOptionAt(options->GetArrayCount(), &fProtocolType);
	if (err == noErr)
		err = TAsyncSerTool::AddCurrentOptions(options);
	return err;
}


// ROM 0x000f7dac AllocateBuffers__12TIrProbeToolFv
NewtonErr
TIrProbeTool::AllocateBuffers()
{
	return TAsyncSerTool::AllocateBuffers();
}


// ROM 0x000f7db0 SetSerialChipSelect__12TIrProbeToolFP18TCMOSerialHardware
NewtonErr
TIrProbeTool::SetSerialChipSelect(TCMOSerialHardware* opt)
{
	NewtonErr err = TSerTool::SetSerialChipSelect(opt);
	if (fSCCService == 0)
	{
		opt->fSCCService = 2;
		fSCCService = 2;
	}
	return err;
}


// ROM 0x000f7de0 SwitchIrLink__12TIrProbeToolFUl
// The port to IrDA (1: SIR at 3/16, auto-receive, no parity) or Sharp's
// ASK (2: odd parity).
void
TIrProbeTool::SwitchIrLink(ULong link)
{
	if (fLink == link)
		return;
	THMOSerIRLinkConfig config;
	TCMOSerialIOParms parms;
	if (link == 1)
	{
		parms.fParity = kNoParity;
		config.fIRLinkMode = kSerIRLink_IRDA_3_16;
		config.fConfigFlags = kSerIRLinkCfg_AutoRx;
	}
	else
	{
		parms.fParity = kOddParity;
		config.fIRLinkMode = kSerIRLink_SharpIR;
	}
	config.SetOpCode(opSetRequired);
	fChip->ProcessOption(&config);
	TSerTool::SetIOParms(&parms);
	fLink = link;
}


/*------------------------------------------------------------------------------
	The timer
------------------------------------------------------------------------------*/

// ROM 0x000f721c StartTimer__12TIrProbeToolFUli
void
TIrProbeTool::StartTimer(ULong delay, ULong kind)
{
	fTimerEvent.fKind = kind;
	TTime when = TimeFromNow(delay);
	fToolPort.Send(&fTimerMsg, &fTimerEvent, sizeof(fTimerEvent), kNoTimeout, &when);
}


// ROM 0x000f7298 StopTimer__12TIrProbeToolFv
void
TIrProbeTool::StopTimer()
{
	if (fTimerEvent.fKind == 0)
		return;
	fTimerMsg.Abort();
	fTimerEvent.fKind = 0;
}


// ROM 0x000f753c TimerComplete__12TIrProbeToolFUl
void
TIrProbeTool::TimerComplete(ULong kind)
{
	NextState(kind);
}


/*------------------------------------------------------------------------------
	Sending and receiving
------------------------------------------------------------------------------*/

// ROM 0x000f72fc StartTransmit__12TIrProbeToolFv
// The transmitter on; an IrDA frame through the SIR framer (ten extra
// BOFs), a Sharp packet after five bytes of lead-in.
void
TIrProbeTool::StartTransmit()
{
	if (fFeatures & kSerFeatureTxConfigNeeded)
		fChip->ConfigureForOutput(true);
	FlushOutputBytes();
	if (fLink == 1)
		fSIR->StartTransmit(&fFrame, 10);
	else
	{
		fSharpTxIndex = 0;
		fSharpTxLength += 5;
		fBuffer[0] = fBuffer[1] = fBuffer[2] = fBuffer[3] = 0;
		fBuffer[4] = 0;
	}
	DoOutput();
}


// ROM 0x000f737c StopTransmit__12TIrProbeToolFv
void
TIrProbeTool::StopTransmit()
{
	fIntMask &= ~kSerIntOutputDone;
	if (fFeatures & kSerFeatureTxConfigNeeded)
		fChip->ConfigureForOutput(false);
}


// ROM 0x000f73a0 DoOutput__12TIrProbeToolFv
void
TIrProbeTool::DoOutput()
{
	ULong result = (fLink == 1) ? fSIR->FillOutputBuffer() : SharpFillOutputBuffer();
	if (result == 1)
	{
		fIntMask |= kSerIntOutputDone;
		ContinueOutputST(true);
		return;
	}
	StopTransmit();
	OutputComplete();
}


// ROM 0x000f7bcc SharpFillOutputBuffer__12TIrProbeToolFv
ULong
TIrProbeTool::SharpFillOutputBuffer()
{
	if (fSharpTxIndex >= fSharpTxLength)
		return 0;
	do
	{
		if (fOutBuf.PutNextByte(fBuffer[fSharpTxIndex]) != kCircleBufOK)
			return 1;
		fSharpTxIndex++;
	} while (fSharpTxIndex < fSharpTxLength);
	return 1;
}


// ROM 0x000f7408 StartReceive__12TIrProbeToolFv
// Listening in IrDA mode what comes in is not known until the port says;
// in Sharp's it is Sharp.
void
TIrProbeTool::StartReceive()
{
	FlushInputBytes();
	fRxKind = (fLink == 1) ? 0 : 2;
	fSIR->StartReceive(&fRxSegment, 0x7f, false);
	fSharpRxState = 0;
	DoInput();
}


// ROM 0x000f7460 StopReceive__12TIrProbeToolFv
void
TIrProbeTool::StopReceive()
{
	fIntMask &= ~kSerIntInputReady;
}


// ROM 0x000f7470 DoInput__12TIrProbeToolFv
// The first bytes in: the port asked which kind they came as (its
// auto-receive status); then read as an IrDA frame or a Sharp packet.
void
TIrProbeTool::DoInput()
{
	fIntMask |= kSerIntInputReady;
	SyncInputBuffer();
	if (fRxKind == 0 && fInBuf.BufferCount() > 0)
	{
		THMOSerIRLinkConfig config;
		config.fIRLinkMode = kSerIRLink_IRDA_Any;
		config.fConfigFlags = kSerIRLinkCfg_AutoRx;
		config.SetOpCode(opGetCurrent);
		fChip->ProcessOption(&config);
		fRxKind = (config.fStatus & kSerIRLinkSts_IRDADetect) ? 1 : 2;
	}
	ULong result = (fRxKind == 1) ? fSIR->EmptyInputBuffer() : SharpEmptyInputBuffer();
	if (result == 1)
		return;
	StopReceive();
	InputComplete();
}


// ROM 0x000f7c34 SharpEmptyInputBuffer__12TIrProbeToolFv
// A Sharp packet: lead-in zeros, then 0x96 or 0x90, and three or four
// bytes in all (0x82 in the second makes a control packet).  ==> 0 when
// it is in, 1 while it is not.  (With the port in IrDA mode a byte the
// chip marked is taken only with the marker 0x40.)
ULong
TIrProbeTool::SharpEmptyInputBuffer()
{
	for (;;)
	{
		UByte byte;
		ULong value;
		ULong result = fInBuf.GetNextByte(&byte, &value);
		if (result == kCircleBufEmpty)
		{
			if (fSharpRxState == 2 && fSharpRxIndex >= fSharpRxLength)
				return 0;
			return 1;
		}
		if (result == kCircleBufEOM && (fLink == 2 || value != 0x40))
			continue;
		if (fSharpRxState == 0)
		{
			if (byte == 0)
			{
				fSharpRxIndex = 0;
				fSharpRxState = 1;
			}
			continue;
		}
		if (fSharpRxState == 1)
		{
			if (byte != 0x96 && byte != 0x90)
			{
				if (byte != 0)
					fSharpRxState = 0;
				continue;
			}
			fSharpRxLength = 4;
			fSharpRxState = 2;
		}
		else if (fSharpRxState != 2)
			continue;
		if (fSharpRxIndex >= sizeof(fPacket))
			continue;
		if (fSharpRxIndex == 1)
			fSharpRxLength = (byte == 0x82) ? 3 : 4;
		fBuffer[fSharpRxIndex++] = byte;
	}
}


// ROM 0x000f7540 OutputComplete__12TIrProbeToolFv
void
TIrProbeTool::OutputComplete()
{
	if (fPhase == 0)
		NextState(5);
	else if (fPhase == 1)
		NextState(8);
	else if (fPhase == 2)
		NextState(0xe);
}


// ROM 0x000f756c InputComplete__12TIrProbeToolFv
void
TIrProbeTool::InputComplete()
{
	if (fPhase == 0)
		NextState(fRxKind == 1 ? 4 : 3);
	else if (fPhase == 1)
		NextState(fRxKind == 1 ? 0xb : 0xa);
	else if (fPhase == 2)
		NextState(0x10);
}


// ROM 0x000f7b2c RecdIrDATestFrame__12TIrProbeToolFUlUc
// A TEST frame (control 0xf3) to the address given, carrying the parameter.
Boolean
TIrProbeTool::RecdIrDATestFrame(ULong parameter, UByte address)
{
	return fSIR->fRxAddress == (address | 0xfe)
		&& fSIR->fRxControl == 0xf3
		&& GetWordBE(fBuffer + 4) == parameter;
}


// ROM 0x000f7b5c SendIrDATestFrame__12TIrProbeToolFUlUc
// A TEST frame: address (to all, 0xff, or the answer, 0xfe), 0xf3, 'prbe'
// and the parameter.
void
TIrProbeTool::SendIrDATestFrame(ULong parameter, UByte address)
{
	fTestParameter = 'prbe';
	fBuffer[2] = address | 0xfe;
	fBuffer[3] = 0xf3;
	SetWordBE(fBuffer + 4, fTestParameter);
	SetWordBE(fBuffer + 8, parameter);
	fFrame.SetControlBuffer(fBuffer + 2, 10, true);
	StartTransmit();
}


// ROM 0x000f75b8 NextState__12TIrProbeToolFUl
void
TIrProbeTool::NextState(ULong event)
{
	for (;;)
	{
		switch (event)
		{
		case 0:		// listen: IrDA mode, in auto-receive
			fENQs = 0;
			fTicks = 0;
			SwitchIrLink(1);
			fPhase = 0;
			// fall through
		case 1:
			StartTimer(500 * kMilliseconds, kProbeListenTick);
			StartReceive();
			return;
		case kProbeListenTick:	// two minutes of half seconds and nobody
			if (++fTicks != 0xf0)
			{
				StartTimer(500 * kMilliseconds, kProbeListenTick);
				return;
			}
			StopReceive();
			StartAbort(kIRErrTimeout);
			return;
		case 3:		// listening, a Sharp packet: two ENQs, or an offer without IrDA, is Sharp IR
		{
			StopTimer();
			UByte* p = fBuffer;
			if (p[0] == 0x96)
			{
				if (p[2] != 5)
				{
					event = 1;
					continue;
				}
				fENQs++;
			}
			else if (p[0] == 0x90 && p[1] == 0x85)
			{
				if (!(p[2] & irUsingIrDA))
					fENQs++;
			}
			else
			{
				event = 1;
				continue;
			}
			if (fENQs < 2)
			{
				event = 1;
				continue;
			}
			fProtocolType.protocol = 7;
			ListenComplete(noErr);
			return;
		}
		case 4:		// listening, an IrDA frame: a TEST frame is echoed
			StopTimer();
			if (RecdIrDATestFrame(0xffffffff, 1))
			{
				SendIrDATestFrame(GetWordBE(fBuffer), 0);
				return;
			}
			// fall through
		case 5:		// IrDA
			fProtocolType.protocol = irUsingIrDA;
			ListenComplete(noErr);
			return;
		case 6:		// connect: IrDA four times, then Sharp once
			fTicks = 0;
			fOffers = 0;
			fIrDATries = 0;
			fSharpTryLimit = 1;
			fIrDATryLimit = 4;
			// fall through
		case 7:
			fPhase = 1;
			SwitchIrLink(1);
			fSharpTries = 0;
			SendIrDATestFrame(0xffffffff, 1);
			return;
		case 8:		// the TEST frame sent: a tenth of a second for its echo
			StartTimer(100 * kMilliseconds, kProbeIrDAReply);
			StartReceive();
			return;
		case 9:
			return;
		case 0xa:	// probing IrDA, a Sharp negotiation: a device that speaks only Sharp - ask it in Sharp
		{
			UByte* p = fBuffer;
			if (p[0] != 0x90)
				return;
			if (p[1] != 0xff && p[2] != 0xff)
				return;
			StopTimer();
			fIrDATryLimit = 1;
			fSharpTryLimit = 4;
			fOffers = 0;
			event = 0xd;
			continue;
		}
		case 0xb:	// probing IrDA, an IrDA frame: the echo is IrDA
			StopTimer();
			if (RecdIrDATestFrame(fTestParameter, 0))
			{
				fProtocolType.protocol = irUsingIrDA;
				ConnectComplete(noErr);
				return;
			}
			event = (++fIrDATries == fIrDATryLimit) ? 0xd : 7;
			continue;
		case kProbeIrDAReply:	// no echo
			StopReceive();
			if (++fTicks == 0x3c7)
			{
				StartAbort(kIRErrTimeout);
				return;
			}
			event = (++fIrDATries == fIrDATryLimit) ? 0xd : 7;
			continue;
		case 0xd:	// Sharp mode: an offer (protocols 0xf, IrDA among them), or every third time an ENQ
		{
			fPhase = 2;
			SwitchIrLink(2);
			fIrDATries = 0;
			UByte* p = fBuffer + 5;
			UByte sent;
			if (fOffers == 2)
			{
				p[0] = 0x96;
				p[1] = 0x82;
				p[2] = 5;
				sent = 5;
				fSharpTxLength = 3;
				fOffers = 0;
			}
			else
			{
				p[0] = 0x90;
				p[1] = 0x85;
				p[2] = 0xf;
				p[3] = 3;
				sent = 0x85;
				fSharpTxLength = 4;
				fOffers++;
			}
			fSharpSent = sent;
			StartTransmit();
			return;
		}
		case 0xe:	// sent: a tenth of a second for the answer
			StartTimer(100 * kMilliseconds, kProbeSharpReply);
			StartReceive();
			return;
		case 0xf:
			return;
		case 0x10:	// probing Sharp, a Sharp packet
		{
			StopTimer();
			UByte* p = fBuffer;
			if (p[0] == 0x96)
			{
				if (p[2] == 0x16)		// SYN: an old Sharp device
				{
					fProtocolType.protocol = irUsingSharpIR;
					fProtocolType.options = irUsing9600;
					ConnectComplete(noErr);
					return;
				}
				event = (++fSharpTries == fSharpTryLimit) ? 7 : 0xd;
				continue;
			}
			if (p[1] == 0x86)			// the answer: what it said
			{
				if (fSharpSent == 5)
				{
					event = 0xd;
					continue;
				}
				fProtocolType.protocol = p[2];
				fProtocolType.options = p[3];
				ConnectComplete(noErr);
				return;
			}
			if (p[1] == 0xff || p[2] == 0xff)
			{
				fOffers = 0;
				event = 0xd;
				continue;
			}
			event = (++fSharpTries == fSharpTryLimit) ? 7 : 0xd;
			continue;
		}
		case kProbeSharpReply:	// no answer
			StopReceive();
			if (++fTicks == 0x3c7)
			{
				StartAbort(kIRErrTimeout);
				return;
			}
			event = (++fSharpTries == fSharpTryLimit) ? 7 : 0xd;
			continue;
		default:
			return;
		}
	}
}


/*------------------------------------------------------------------------------
	IRProbeService ('pkir')
------------------------------------------------------------------------------*/

PROTOCOL_IMPL_SOURCE_MACRO(IRProbeService)
PROTOCOL_CLASSINFO(IRProbeService, "TCMService", "serv\0pkir\0\0", 0x20000, 0, nil)	// ROM 0x00382f88 ClassInfo__14IRProbeServiceSFv

// ROM 0x000e89e0 New__14IRProbeServiceFv
IRProbeService*
IRProbeService::New()
{
	return this;
}


// ROM 0x000e89e4 Delete__14IRProbeServiceFv
void
IRProbeService::Delete()
{ }


// ROM 0x000e89e8 Start__14IRProbeServiceFP12TOptionArrayUlP12TServiceInfo
NewtonErr
IRProbeService::Start(TOptionArray* options, ULong serviceId, TServiceInfo* serviceInfo)
{
	TIrProbeTool tool(serviceId);
	NewtonErr err = StartCommTool(&tool, serviceId, serviceInfo);
	if (err == noErr)
		err = OpenCommTool(serviceInfo->GetPortId(), options, this);
	return err;
}


// ROM 0x000e8a5c DoneStarting__14IRProbeServiceFP7TAEventUlP12TServiceInfo
NewtonErr
IRProbeService::DoneStarting(TAEvent* event, ULong size, TServiceInfo* serviceInfo)
{
	return ((TCommToolReply*) event)->fResult;
}
