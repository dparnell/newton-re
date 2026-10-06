/*
	File:		comms/AsyncSerialTool.cpp

	Contains:	TAsyncSerTool and TAsyncService (SerialTool.h).

	Reconstructed from the MP2x00 US ROM (0x0003913c-0x0003b14c); each
	function cites its origin.
*/

#include "SerialTool.h"
#include "FIQTimer.h"
#include "Atomic.h"
#include "BufferList.h"
#include "NewtonExceptions.h"
#include "NewtErrors.h"
#include "CommErrors.h"
#include "CompMath.h"
#include "host/RomBugs.h"


// ROM 0x0003913c __ct__13TAsyncSerToolFUl
TAsyncSerTool::TAsyncSerTool(ULong serviceId)
	: TSerTool(serviceId)
{ }


// ROM 0x000391dc __dt__13TAsyncSerToolFv
TAsyncSerTool::~TAsyncSerTool()
{ }


// ROM 0x0003a660 GetSizeOf__13TAsyncSerToolFv
// DEVIATION (pointer size): the host's size (the ROM's 0x4b0).
ULong
TAsyncSerTool::GetSizeOf()
{
	return sizeof(TAsyncSerTool);
}


// ROM 0x0003a100 GetToolName__13TAsyncSerToolFv
UChar*
TAsyncSerTool::GetToolName()
{
	return (UChar*) "Async Serial Tool";
}


// ROM 0x000396b0 TaskConstructor__13TAsyncSerToolFv
// CTS changing more than 100 times a second is a clock (ExtStatusInt).
NewtonErr
TAsyncSerTool::TaskConstructor()
{
	NewtonErr err = TSerTool::TaskConstructor();
	if (err == noErr)
	{
		fIntMask = 0;
		fIntFlags = 0;
		fCarrierTimer = nil;
		fRxDMAState = kSerDMANone;
		fTxDMAState = kSerDMANone;
		fFlowChar = 0;
		fInDoInput = false;
		fCTSChanges = 0;
		fCTSChangeLimit = 100;
		fCTSInterval = kSeconds;
		fCTSIntervalEnd = TimeFromNow(kSeconds);
		SetInputSendForIntDelay(fMiscConfig.inputDelay);
	}
	return err;
}


// ROM 0x00039a28 TaskDestructor__13TAsyncSerToolFv
void
TAsyncSerTool::TaskDestructor()
{
	TSerTool::TaskDestructor();
}


/*------------------------------------------------------------------------------
	Buffers and the chip
------------------------------------------------------------------------------*/

// ROM 0x0003aeac AllocateBuffers__13TAsyncSerToolFv
// The chip put in asynchronous mode, the two buffers made (wired, for a
// DMA channel), the input's flow control levels (a quarter of the buffer
// from either end, but no more than 0x200 from the top or from each
// other), the DMA channels started where the chip has them, and the
// carrier timer.
NewtonErr
TAsyncSerTool::AllocateBuffers()
{
	// (the ROM leaves the result as whatever the caller left in r5 when the
	// chip is not a version 2 one; nought here)
	NewtonErr err = noErr;
	fTxDMAState = kSerDMANone;
	if (fFeatures & kSerFeatureVersion2)
		err = fChip->SetSerialMode(fConfigureForOutput ? kSerModeHalfDuplex : kSerModeAsync);
	if (err != noErr)
		return err;
	fBuffersAllocated = true;
	Boolean txDMA = !fMiscConfig.disableOutputDMA
				 && (fFeatures & kSerFeatureAsyncTxDMA) && (fFeatures & kSerFeatureWireAsyncTxDMABuf);
	Boolean rxDMA = !fMiscConfig.disableInputDMA
				 && (fFeatures & kSerFeatureAsyncRxDMA) && (fFeatures & kSerFeatureWireAsyncRxDMABuf);
	err = fInBuf.Allocate(fBuffers.fRecvSize, fBuffers.fRecvMarkers, rxDMA ? kCircleBufWired : kCircleBufLocked, 0);
	if (err == noErr)
		err = fOutBuf.Allocate(fBuffers.fSendSize, 0, txDMA ? kCircleBufWired : kCircleBufLocked, 0);
	if (err != noErr)
		return err;
	ULong size;
	fInBuf.DMABufInfo(&size, nil, nil, nil);
	ULong low = size >> 2;
	ULong high = size - low;
	if (size - high > 0x200)
		high = size - 0x200;
	if (high - low > 0x200)
		low = high - 0x200;
	fInputHighWater = high;
	fInputLowWater = low;
	if (!fMiscConfig.disableInputDMA && (fFeatures & kSerFeatureAsyncRxDMA)
	&&  fChip->InitRxDMA(&fInBuf, fInputLowWater, RxMultiByteInterrupt) == noErr)
		fRxDMAState = kSerDMAIdle;
	if (txDMA && fChip->InitTxDMA(&fOutBuf, TxDMAInterrupt) == noErr)
		fTxDMAState = kSerDMAIdle;
	if (fCarrierTimer == nil)
	{
		EnterFIQAtomic();
		fCarrierTimer = fFIQTimer->AcquireFIQTimer(CarrierTimerInterrupt, this);
		ExitFIQAtomic();
		if (fCarrierTimer == nil)
			err = -10007;			// (kOSErrNoMemory)
	}
	return err;
}


// ROM 0x00039234 DeallocateBuffers__13TAsyncSerToolFv
void
TAsyncSerTool::DeallocateBuffers()
{
	if (fBuffersAllocated)
	{
		fBuffersAllocated = false;
		fInBuf.Deallocate();
		fOutBuf.Deallocate();
	}
	fRxDMAState = kSerDMANone;
	fTxDMAState = kSerDMANone;
	EnterFIQAtomic();
	fFIQTimer->ReleaseFIQTimer(fCarrierTimer);
	fCarrierTimer = nil;
	ExitFIQAtomic();
}


// ROM 0x0003ac30 TurnOnSerChip__13TAsyncSerToolFv
// The chip configured and powered on (an exception from it answered as
// kOSErrXXX), reconfigured, input DMA started, the modem interrupts set up,
// the flow control state read off the lines, and the transmitter enabled
// unless the misc config asks otherwise.
NewtonErr
TAsyncSerTool::TurnOnSerChip()
{
	NewtonErr err = noErr;
	fChipOn = false;
	SetIOParms(&fIOParms);
	newton_try
	{
		if (fBreakFraming.fUseHighSpeedClock)
		{
			THMOHiSpeedClockOption clock;
			if (fFeatures & kSerFeatureVersion2)
				fChip->ProcessOption(&clock);
		}
		fChip->PowerOn();
	}
	newton_catch("evt.ex")
	{
		err = -10059;
	}
	end_try;
	if (err == noErr)
	{
		EnterFIQAtomic();
		fChip->Reconfigure();
		if (!fOutFlow.useSoftFlowControl && fRxDMAState == kSerDMAIdle)
		{
			fChip->RxDMAControl(kDMAStart | kDMANotifyOnNext);
			fRxDMAState = kSerDMARunning;
		}
		if (fTxDMAState != kSerDMANone)
			fChip->SetIntSourceEnable(kSerIntSrcTxBufEmpty, false);
		ConfigureModemInterrupts();
		if (fOutFlow.useHardFlowControl)
			fOutFlow.hardFlowBlocked = !HSKiOn();
		fOutFlow.softFlowBlocked = false;
		Boolean enable = !fMiscConfig.txdOffUntilSend;
		if (fMiscConfig.txdOnIfGPiOn && GPiOn())
			enable = true;
		if ((fMiscConfig.txdOnIfHSKiOn && HSKiOn()) || enable)
			SetTxDTransceiverEnable(true);
		fChipOn = true;
		ExitFIQAtomic();
	}
	return err;
}


// ROM 0x0003adfc TurnOffSerChip__13TAsyncSerToolFv
void
TAsyncSerTool::TurnOffSerChip()
{
	if (!fChipOn)
		return;
	if (fChip != nil)
		fChip->PowerOff();
	if (fRxDMAState == kSerDMARunning)
		fRxDMAState = kSerDMAIdle;
	fChipOn = false;
}


// ROM 0x0003ae40 ConfigureModemInterrupts__13TAsyncSerToolFv
// DCD's interrupt on if a carrier event is wanted, CTS's if a CTS event is
// or hardware flow control is on.
void
TAsyncSerTool::ConfigureModemInterrupts()
{
	EnterFIQAtomic();
	ULong enables = fEventEnables.serEventEnables;
	fChip->SetIntSourceEnable(kSerIntSrcDCD, (enables & 0x0C) != 0);
	fChip->SetIntSourceEnable(kSerIntSrcCTS, (enables & 0x70) != 0 || fOutFlow.useHardFlowControl);
	fSerialStatus = fChip->GetSerialStatus();
	ExitFIQAtomic();
}


// ROM 0x00039878 BytesAvailable__13TAsyncSerToolFRUl
void
TAsyncSerTool::BytesAvailable(ULong& count)
{
	count = fInBuf.BufferCount();
}


// ROM 0x00039928 GPiOn__13TAsyncSerToolFv
// (GPi is DCD.)
Boolean
TAsyncSerTool::GPiOn()
{
	return (fSerialStatus & kSerialDCDAsserted) != 0;
}


// ROM 0x0003993c HSKiOn__13TAsyncSerToolFv
// (HSKi is CTS.)
Boolean
TAsyncSerTool::HSKiOn()
{
	return (fSerialStatus & kSerialCTSAsserted) != 0;
}


// ROM 0x00039698 SyncInputBuffer__13TAsyncSerToolFv
// What the input DMA channel has read brought into the buffer.
void
TAsyncSerTool::SyncInputBuffer()
{
	if (fRxDMAState != kSerDMARunning)
		return;
	fChip->RxDMAControl(kDMASync | kDMANotifyOnNext);
}


// ROM 0x00039898 FlushInputBytes__13TAsyncSerToolFv
// (The ROM sets the buffer's start to its end directly.)
void
TAsyncSerTool::FlushInputBytes()
{
	if (fRxDMAState == kSerDMARunning)
		fChip->RxDMAControl(kDMASync | kDMANotifyOnNext);
	ULong value;
	while (fInBuf.GetEOMMark(&value) != 0xFFFFFFFF)
		;
	fInBuf.fStart = fInBuf.fEnd;
}


// ROM 0x000398c8 FlushOutputBytes__13TAsyncSerToolFv
// ==> how many bytes were thrown away.
ULong
TAsyncSerTool::FlushOutputBytes()
{
	EnterFIQAtomic();
	ULong count = fOutBuf.BufferCount();
	if (fTxDMAState == kSerDMARunning || fTxDMAState == kSerDMASuspended)
	{
		if (fChip != nil)
			fChip->TxDMAControl(kDMAStop);
		fTxDMAState = kSerDMAIdle;
	}
	fOutBuf.FlushBytes();
	ExitFIQAtomic();
	return count;
}


/*------------------------------------------------------------------------------
	Statistics
------------------------------------------------------------------------------*/

// ROM 0x0003928c ResetStats__13TAsyncSerToolFv
void
TAsyncSerTool::ResetStats()
{
	fStats.parityErrCount = 0;
	fStats.framingErrCount = 0;
	fStats.softOverrunCount = 0;
	fStats.hardOverrunCount = 0;
}


// ROM 0x000392a4 GetStats__13TAsyncSerToolFP17TCMOSerialIOStats
// The counts answered and started again.
void
TAsyncSerTool::GetStats(TCMOSerialIOStats* opt)
{
	fStats.fGPiState = GPiOn();
	fStats.fHSKiState = HSKiOn();
	opt->CopyDataFrom(&fStats);
	fStats.parityErrCount = 0;
	fStats.framingErrCount = 0;
	fStats.softOverrunCount = 0;
	fStats.hardOverrunCount = 0;
}


// ROM 0x000392e8 UpdateStats__13TAsyncSerToolFUl
void
TAsyncSerTool::UpdateStats(ULong status)
{
	if (status & kSerialRxParityErr)
		fStats.parityErrCount++;
	if (status & kSerialRxFramingErr)
		fStats.framingErrCount++;
	if (status & kSerialRxOverrun)
		fStats.hardOverrunCount++;
	if (status & kSerialRxSoftOverrun)
		fStats.softOverrunCount++;
}


/*------------------------------------------------------------------------------
	Options
------------------------------------------------------------------------------*/

// ROM 0x00039950 AddDefaultOptions__13TAsyncSerToolFP12TOptionArray
NewtonErr
TAsyncSerTool::AddDefaultOptions(TOptionArray* options)
{
	TCMOOutputFlowControlParms outFlow;
	TCMOInputFlowControlParms inFlow;
	NewtonErr err = options->InsertOptionAt(options->GetArrayCount(), &inFlow);
	if (err == noErr)
		err = options->InsertOptionAt(options->GetArrayCount(), &outFlow);
	if (err == noErr)
		TSerTool::AddDefaultOptions(options);		// (what it answers is not looked at)
	return err;
}


// ROM 0x000399b4 AddCurrentOptions__13TAsyncSerToolFP12TOptionArray
// (TSerTool's, inline.)
NewtonErr
TAsyncSerTool::AddCurrentOptions(TOptionArray* options)
{
	NewtonErr err = options->InsertOptionAt(options->GetArrayCount(), &fInFlow);
	if (err == noErr)
		err = options->InsertOptionAt(options->GetArrayCount(), &fOutFlow);
	if (err == noErr)
		err = options->InsertOptionAt(options->GetArrayCount(), &fBreakFraming);
	if (err != noErr)
		return err;
	return TSerTool::AddCurrentOptions(options);
}


// ROM 0x00039a2c ProcessOptionStart__13TAsyncSerToolFP7TOptionUlT2
// The async tool's options; anything else is TSerTool's (inline in the ROM).
ULong
TAsyncSerTool::ProcessOptionStart(TOption* opt, ULong label, ULong opcode)
{
	Boolean set = (opcode == opSetNegotiate || opcode == opSetRequired);
	TOption* to = opt;
	TOption* from = opt;
	switch (label)
	{
	case kCMOSerialDiscard:
		// (the bytes of the option: discard input, discard output)
		if (fChipOn && opcode == opSetRequired)
		{
			UByte* discard = (UByte*) (opt + 1);
			if (discard[0])
				FlushInputBytes();
			if (discard[1])
				FlushOutputBytes();
			return noErr;
		}
		return opFailure;

	case kCMOInputFlowControlParms:
		if (set)
		{
			SetInputFlowControl((TCMOInputFlowControlParms*) opt);
			return noErr;
		}
		if (opcode == opGetDefault)
		{
			TCMOInputFlowControlParms def;
			opt->CopyDataFrom(&def);
			return noErr;
		}
		from = &fInFlow;
		break;

	case kCMOOutputFlowControlParms:
		if (set)
		{
			SetOutputFlowControl((TCMOOutputFlowControlParms*) opt);
			return noErr;
		}
		if (opcode == opGetDefault)
		{
			TCMOOutputFlowControlParms def;
			opt->CopyDataFrom(&def);
			return noErr;
		}
		from = &fOutFlow;
		break;

	case kCMOBreakFraming:
		if (set)
			to = &fBreakFraming;
		else if (opcode == opGetDefault)
		{
			TCMOBreakFraming def;
			opt->CopyDataFrom(&def);
			return noErr;
		}
		else
			from = &fBreakFraming;
		break;

	case kCMOSerialBuffers:
		// (only before the buffers are made)
		if (set)
		{
			if (fBuffersAllocated)
				return opFailure;
			to = &fBuffers;
		}
		else if (opcode == opGetDefault)
		{
			TCMOSerialBuffers def;
			opt->CopyDataFrom(&def);
			return noErr;
		}
		else
			from = &fBuffers;
		break;

	case kCMOSerialDTRControl:
		if (!fChipOn)
			return opFailure;
		if (set)
		{
			Boolean assert = ((TCMOSerialDTRControl*) opt)->fAssertDTR;
			SetSerialOutputs(assert ? kSerialOutputDTR : 0, assert ? 0 : kSerialOutputDTR);
			return noErr;
		}
		{
			TCMOSerialDTRControl dtr;
			if (opcode == opGetCurrent)
				dtr.fAssertDTR = (GetSerialOutputs() & kSerialOutputDTR) != 0;
			opt->CopyDataFrom(&dtr);
		}
		return noErr;

	case kCMOSerialEventEnables:
		if (set)
		{
			SetEventEnables((TCMOSerialEventEnables*) opt);
			return noErr;
		}
		if (opcode == opGetDefault)
		{
			TCMOSerialEventEnables def;
			opt->CopyDataFrom(&def);
			return noErr;
		}
		from = &fEventEnables;
		break;

	case kCMOSerialIOStats:
		if (set)
			return opReadOnly;
		if (opcode == opGetCurrent)
		{
			GetStats((TCMOSerialIOStats*) opt);
			return noErr;
		}
		return opFailure;

	case kCMOSerialMiscConfig:
		if (set)
		{
			fMiscConfig.CopyDataFrom(opt);
			SetInputSendForIntDelay(fMiscConfig.inputDelay);
			return noErr;
		}
		if (opcode == opGetDefault)
		{
			TCMOSerialMiscConfig def;
			opt->CopyDataFrom(&def);
			return noErr;
		}
		from = &fMiscConfig;
		break;

	default:
		return TSerTool::ProcessOptionStart(opt, label, opcode);
	}
	to->CopyDataFrom(from);
	return noErr;
}


// ROM 0x00039d88 SetOutputFlowControl__13TAsyncSerToolFP26TCMOOutputFlowControlParms
// With the chip off, simply kept.  On: XON/XOFF turned on or off stops or
// starts the input DMA (the tool must see each byte to find them), and
// hardware flow control turned on reads CTS or, turned off, lets the
// output go.
// ROM BUG (fixed): the modem interrupts are configured before the new
// hardware flow control setting is recorded, so turning it on does not
// enable CTS's interrupt (nor turning it off disable it) until they are
// configured again.  The fix records the setting first, then configures
// them.
NewtonErr
TAsyncSerTool::SetOutputFlowControl(TCMOOutputFlowControlParms* opt)
{
	if (!fChipOn)
		return fOutFlow.CopyDataFrom(opt);
	fOutFlow.xonChar = opt->xonChar;
	fOutFlow.xoffChar = opt->xoffChar;
	Boolean wasSoft = fOutFlow.useSoftFlowControl;
	Boolean soft = opt->useSoftFlowControl;
	fOutFlow.useSoftFlowControl = soft;
	if (soft != wasSoft)
	{
		fOutFlow.softFlowBlocked = false;
		if (fRxDMAState != kSerDMANone)
		{
			if (!soft || fRxDMAState != kSerDMARunning)
			{
				fChip->RxDMAControl(kDMAStart | kDMANotifyOnNext);
				fRxDMAState = kSerDMARunning;
			}
			else
			{
				fChip->RxDMAControl(kDMAStop);
				fRxDMAState = kSerDMAIdle;
			}
		}
	}
	Boolean hard = opt->useHardFlowControl;
	if (hard == fOutFlow.useHardFlowControl)
		return noErr;
	if (RomBugFixed())
	{
		fOutFlow.useHardFlowControl = hard;
		ConfigureModemInterrupts();
	}
	else
	{
		ConfigureModemInterrupts();
		fOutFlow.useHardFlowControl = hard;
	}
	if (hard)
		fOutFlow.hardFlowBlocked = !HSKiOn();
	else
	{
		EnterFIQAtomic();
		ContinueOutputIH(true);
		ExitFIQAtomic();
	}
	return noErr;
}


// ROM 0x00039e68 SetInputFlowControl__13TAsyncSerToolFP25TCMOInputFlowControlParms
// With the chip off, simply kept.  On: XON/XOFF turned off sends an XON
// if the input was stopped by one (though it leaves softFlowBlocked set),
// and hardware flow control turned off raises RTS.
NewtonErr
TAsyncSerTool::SetInputFlowControl(TCMOInputFlowControlParms* opt)
{
	if (!fChipOn)
		return fInFlow.CopyDataFrom(opt);
	fInFlow.xonChar = opt->xonChar;
	fInFlow.xoffChar = opt->xoffChar;
	Boolean wasSoft = fInFlow.useSoftFlowControl;
	fInFlow.useSoftFlowControl = opt->useSoftFlowControl;
	if (!opt->useSoftFlowControl)
	{
		if (wasSoft && fInFlow.softFlowBlocked && fFlowChar != fInFlow.xonChar)
		{
			fFlowChar = fInFlow.xonChar;
			ContinueOutputST(true);
		}
	}
	Boolean wasHard = fInFlow.useHardFlowControl;
	fInFlow.useHardFlowControl = opt->useHardFlowControl;
	if (opt->useHardFlowControl || !wasHard)
		return noErr;
	EnterFIQAtomic();
	if (fChip != nil)
		fChip->SetSerialOutputs(kSerialOutputRTS);
	ExitFIQAtomic();
	return noErr;
}


// ROM 0x00039f14 SetEventEnables__13TAsyncSerToolFP22TCMOSerialEventEnables
// ROM BUG (fixed): the events wanted replace fIntMask's event bits by
// masking the old value with the event mask rather than its complement, so
// the output-done and input-ready bits are cleared too - a put or get in
// hand when 'sevt is set is no longer told when its data has gone or come.
// The fix keeps the bits outside the event mask.

void
TAsyncSerTool::SetEventEnables(TCMOSerialEventEnables* opt)
{
	fEventEnables.CopyDataFrom(opt);
	if (RomBugFixed())
		fIntMask = (fIntMask & ~kSerIntEventMask) | (fEventEnables.serEventEnables & kSerIntEventMask);
	else
		fIntMask = (fIntMask & kSerIntEventMask) | (fEventEnables.serEventEnables & kSerIntEventMask);
	if (!fChipOn)
		return;
	ConfigureModemInterrupts();
}


// ROM 0x0003ade8 SetInputSendForIntDelay__13TAsyncSerToolFUl
void
TAsyncSerTool::SetInputSendForIntDelay(ULong delay)
{
	fSendForIntDelay = delay;
}


// ROM 0x0003adf0 RestoreInputSendForIntDelay__13TAsyncSerToolFv
void
TAsyncSerTool::RestoreInputSendForIntDelay()
{
	fSendForIntDelay = fMiscConfig.inputDelay;
}


/*------------------------------------------------------------------------------
	Output
------------------------------------------------------------------------------*/

// ROM 0x0003932c DoOutput__13TAsyncSerToolFv
// The output buffer filled from the put and the transmitter started (a
// new put framed with a break first); the put answered when it is all
// in the buffer.
void
TAsyncSerTool::DoOutput()
{
	if (fMiscConfig.txdOffUntilSend)
	{
		SetTxDTransceiverEnable(true);
		fMiscConfig.txdOffUntilSend = false;
	}
	ULong result = FillOutputBuffer();
	if (result == 5)
		result = noErr;
	else if (result == noErr)
	{
		fIntMask |= kSerIntOutputDone;
		if (!fOutputStarting)
		{
			EnterFIQAtomic();
			ContinueOutputIH(true);
		}
		else
		{
			fOutputStarting = false;
			if (fConfigureForOutput)
				fChip->ConfigureForOutput(true);
			EnterFIQAtomic();
			DoBreakFraming();
			ContinueOutputIH(true);
		}
		ExitFIQAtomic();
		return;
	}
	DoPutComplete(result);
}


// ROM 0x000393e0 FillOutputBuffer__13TAsyncSerToolFv
// As much of the put as there is room for (round the wrap in two).
// ==> 0, or 5 when there is no more (a repeated frame starts the buffer
// again).
ULong
TAsyncSerTool::FillOutputBuffer()
{
	if (fPutSize == 0)
		return 5;
	if (fBreakFraming.fRepeatCount != 0)
		fOutBuf.Reset();
	CBufferList* data = fPutBuffer;
	ULong result = 0;
	ULong room = fOutBuf.BufferSpace();
	if (room != 0)
	{
		ULong toEnd = fOutBuf.fBufferSize - fOutBuf.fEnd;
		if (fOutBuf.fStart == 0)
			toEnd -= 1;
		UByte* p = fOutBuf.fBuffer + fOutBuf.fEnd;
		ULong got;
		if (toEnd < room)
		{
			got = data->Getn(p, toEnd);
			if (got == toEnd)
				got += data->Getn(fOutBuf.fBuffer, room - toEnd);
		}
		else
			got = data->Getn(p, room);
		fOutBuf.UpdateEnd(got);
		fPutSize -= got;
		if (got == 0)
			result = 5;
	}
	return result;
}


// ROM 0x00039428 DoPutComplete__13TAsyncSerToolFl
// ROM BUG (fixed): the put's buffer is asked its position whether or not
// there is one - and TIrDATool::StartOutput and TSharpIRTool::StartOutput
// complete a put refused with the chip off before they have stored its
// buffer, so fPutBuffer is nil (or a finished put's) and the MessagePad
// calls through whatever the field holds; the host crashed.  The fix
// answers no bytes sent when there is no buffer.
void
TAsyncSerTool::DoPutComplete(NewtonErr result)
{
	fIntMask &= ~kSerIntOutputDone;
	if (fConfigureForOutput)
		fChip->ConfigureForOutput(false);
	if (RomBugFixed())
		PutComplete(result, fPutBuffer != nil ? fPutBuffer->Position() : 0);
	else
		PutComplete(result, fPutBuffer->Position());
}



// ROM 0x00039484 KillPut__13TAsyncSerToolFv
// The put in hand answered with what had been sent of it.
void
TAsyncSerTool::KillPut()
{
	ULong flushed = FlushOutputBytes();
	if (fPutBuffer != nil)
	{
		fIntMask &= ~kSerIntOutputDone;
		if (fConfigureForOutput)
			fChip->ConfigureForOutput(false);
		ULong taken = fPutBuffer->Position();
		PutComplete(kCommErrRequestCanceled, taken > flushed ? taken - flushed : 0);
	}
	KillPutComplete(noErr);
}


// ROM 0x00039f70 TxDataSent__13TAsyncSerToolFv
// The output buffer has emptied: fill it again.
void
TAsyncSerTool::TxDataSent()
{
	if (fPutBuffer == nil)
		return;
	if (fOutBuf.BufferCount() != 0)
		return;
	DoOutput();
}


// ROM 0x0003a0d4 ContinueOutputST__13TAsyncSerToolFUc
void
TAsyncSerTool::ContinueOutputST(Boolean start)
{
	EnterFIQAtomic();
	ContinueOutputIH(start);
	ExitFIQAtomic();
}


// ROM 0x0003a11c StartOutputST__13TAsyncSerToolFv
void
TAsyncSerTool::StartOutputST()
{
	EnterFIQAtomic();
	DoBreakFraming();
	ContinueOutputIH(true);
	ExitFIQAtomic();
}


// ROM 0x0003a1cc OutputStopped__13TAsyncSerToolFv
// By an XOFF, or by CTS.
Boolean
TAsyncSerTool::OutputStopped()
{
	if (fOutFlow.useSoftFlowControl && fOutFlow.softFlowBlocked)
		return true;
	if (fOutFlow.useHardFlowControl && fOutFlow.hardFlowBlocked)
		return true;
	return false;
}


// ROM 0x0003a208 SuspendTxDMA__13TAsyncSerToolFv
void
TAsyncSerTool::SuspendTxDMA()
{
	if (fTxDMAState != kSerDMARunning)
		return;
	fTxDMAState = kSerDMASuspended;
	fChip->TxDMAControl(kDMASuspend);
}


// ROM 0x0003a224 ContinueOutputIH__13TAsyncSerToolFUc
// The transmitter kept going (unless output is stopped and there is no
// flow control character to send): a byte at a time while the chip has
// room, or the flow control character and then the DMA channel.
void
TAsyncSerTool::ContinueOutputIH(Boolean start)
{
	if (OutputStopped() && fFlowChar == 0)
		return;
	TSerialChip* chip = fChip;
	if (chip == nil)
		return;
	if (fTxDMAState == kSerDMANone)
	{
		UByte byte;
		if (chip->TxBufEmpty() && GetNextOutChar(&byte))
		{
			fChip->PutByte(byte);
			while (fChip->TxBufEmpty() && GetMoreOutChars(&byte))
				fChip->PutByte(byte);
		}
		return;
	}
	if (fFlowChar != 0)
	{
		if (!chip->TxBufEmpty())
			fChip->SetIntSourceEnable(kSerIntSrcTxBufEmpty, true);
		if (!fChip->TxBufEmpty())
			return;
		fChip->PutByte(fFlowChar);
		fFlowChar = 0;
		fChip->SetIntSourceEnable(kSerIntSrcTxBufEmpty, false);
	}
	if (fOutBuf.BufferCount() == 0)
	{
		if (fIntMask & kSerIntOutputDone)
		{
			fIntFlags |= kSerIntOutputDone;
			IHRequest(0);
		}
	}
	else if (fTxDMAState != kSerDMARunning)
	{
		fChip->SetIntSourceEnable(kSerIntSrcTxBufEmpty, false);
		fTxDMAState = kSerDMARunning;
		fChip->TxDMAControl(kDMAStart);
	}
}


// ROM 0x0003a3a8 DoBreakFraming__13TAsyncSerToolFv
// A break of fBreakOnTime (once what was sent before has gone, or twice
// that long has passed), then fBreakOffTime of quiet - busy waits.
void
TAsyncSerTool::DoBreakFraming()
{
	if (fBreakFraming.fBreakOnTime != 0)
	{
		fDelayTimer.ResetTimeOut(fBreakFraming.fBreakOnTime << 1);
		if (fFeatures & kSerFeatureAllSent)
			while (!fChip->AllSent() && !fDelayTimer.TimedOut())
				;
		fChip->SetBreak(true);
		fDelayTimer.ShortTimerDelay(fBreakFraming.fBreakOnTime);
		fChip->SetBreak(false);
	}
	if (fBreakFraming.fBreakOffTime != 0)
		fDelayTimer.ShortTimerDelay(fBreakFraming.fBreakOffTime);
}


// ROM 0x0003a440 GetNextOutChar__13TAsyncSerToolFPUc
// The flow control character first; then the buffer (a repeated frame
// sent again from its start, after its break).  None left: the task told
// the output has gone.
// ROM QUIRK (kept): the first byte of a repeated frame is not masked to
// the data bits.
Boolean
TAsyncSerTool::GetNextOutChar(UByte* byte)
{
	if (fFlowChar != 0)
	{
		*byte = fFlowChar & fDataMask;
		fFlowChar = 0;
		return true;
	}
	if (OutputStopped())
		return false;
	if (fOutBuf.GetNextByte(byte) == kCircleBufOK)
	{
		*byte = fDataMask & *byte;
		return true;
	}
	if (fBreakFraming.fRepeatCount != 0)
	{
		fBreakFraming.fRepeatCount--;
		fOutBuf.ResetStart();
		DoBreakFraming();
		if (fOutBuf.GetNextByte(byte) == kCircleBufOK)
			return true;
	}
	if (fIntMask & kSerIntOutputDone)
	{
		fIntFlags |= kSerIntOutputDone;
		IHRequest(0);
	}
	return false;
}


// ROM 0x0003a544 GetMoreOutChars__13TAsyncSerToolFPUc
Boolean
TAsyncSerTool::GetMoreOutChars(UByte* byte)
{
	ULong result = fOutBuf.GetNextByte(byte);
	*byte = fDataMask & *byte;
	return result == kCircleBufOK;
}


// ROM 0x0003a714 TxBEmptyInt__13TAsyncSerToolFv
// The transmitter has room: more bytes, or (none) its interrupt reset.
void
TAsyncSerTool::TxBEmptyInt(void* tool)
{
	TAsyncSerTool* self = (TAsyncSerTool*) tool;
	if (self->fChip == nil)
		return;
	if (self->fTxDMAState == kSerDMANone)
	{
		UByte byte;
		if (!self->GetNextOutChar(&byte))
			self->fChip->ResetTxBEmpty();
		else
		{
			self->fChip->PutByte(byte);
			while (self->fChip->TxBufEmpty() && self->GetMoreOutChars(&byte))
				self->fChip->PutByte(byte);
		}
	}
	else
	{
		self->fChip->ResetTxBEmpty();
		self->ContinueOutputIH(true);
	}
}


// ROM 0x0003a7c4 TxDMAInterrupt__13TAsyncSerToolFv
// The output DMA channel has finished: a repeated frame again, and on.
void
TAsyncSerTool::TxDMAInterrupt(void* tool)
{
	TAsyncSerTool* self = (TAsyncSerTool*) tool;
	self->fTxDMAState = kSerDMAIdle;
	if (self->fChip == nil)
		return;
	if (self->fBreakFraming.fRepeatCount != 0)
	{
		self->fBreakFraming.fRepeatCount--;
		self->fOutBuf.ResetStart();
		self->DoBreakFraming();
	}
	self->ContinueOutputIH(true);
}


/*------------------------------------------------------------------------------
	Input
------------------------------------------------------------------------------*/

// ROM 0x00039510 DoInput__13TAsyncSerToolFv
// The get filled from the input buffer (an immediate get with nothing to
// read answered kCommErrNoDataAvailable at once); then the input let go
// again if flow control stopped it and the buffer has drained.
void
TAsyncSerTool::DoInput()
{
	if (fInDoInput)
		return;
	fInDoInput = true;
	if (fGetImmediate && fGetThresholdLeft == 0)
	{
		SyncInputBuffer();
		if (fInBuf.BufferCount() == 0)
		{
			GetComplete(kCommErrNoDataAvailable, false, 0);
			fInDoInput = false;
			return;
		}
	}
	Boolean again;
	do
	{
		again = false;
		fIntMask |= kSerIntInputReady;
		SyncInputBuffer();
		ULong value = 0;
		ULong result = EmptyInputBuffer(&value);
		Boolean endOfFrame = false;
		if (result != noErr)
		{
			if (result == kCircleBufCountExhausted)
				result = noErr;
			else if (result == kSerEndOfFrameMarker)
			{
				result = noErr;
				endOfFrame = true;
			}
			DoGetComplete(result, endOfFrame);
			if (fGetBuffer != nil)
				again = true;
		}
	} while (again);
	if ((fInFlow.useSoftFlowControl || fInFlow.useHardFlowControl)
	&&  fInBuf.BufferCount() < fInputLowWater)
	{
		if (fInFlow.useHardFlowControl && fInFlow.hardFlowBlocked)
		{
			SetHSKo(true);
			fInFlow.hardFlowBlocked = false;
		}
		if (fInFlow.useSoftFlowControl && fInFlow.softFlowBlocked)
		{
			fInFlow.softFlowBlocked = false;
			if (fFlowChar != fInFlow.xonChar)
			{
				fFlowChar = fInFlow.xonChar;
				ContinueOutputST(false);
			}
		}
	}
	fInDoInput = false;
}


// ROM 0x0003972c EmptyInputBuffer__13TAsyncSerToolFPUl
// As much of the input as the get has room for (an immediate get counting
// down its threshold).  ==> 0 (more wanted), kCircleBufCountExhausted (the
// get is done), kSerEndOfFrameMarker (a frame's end), or kSerErr_AsyncError
// (a byte that came with an error).
ULong
TAsyncSerTool::EmptyInputBuffer(ULong* markerValue)
{
	ULong result;
	if (!fGetImmediate)
		result = fInBuf.CopyOut(fGetBuffer, &fGetSize, markerValue);
	else
	{
		ULong count = fGetSize;
		result = fInBuf.CopyOut(fGetBuffer, &count, markerValue);
		ULong got = fGetSize - count;
		if (result == kCircleBufOK && fGetThresholdLeft <= got)
			result = kCircleBufCountExhausted;
		fGetSize -= got;
		fGetThresholdLeft -= got;
	}
	if (result == kCircleBufEOM)
	{
		result = *markerValue;
		if (result != kSerEndOfFrameMarker)
			result = kSerErr_AsyncError;
	}
	return result;
}


// ROM 0x00039828 DoGetComplete__13TAsyncSerToolFlUc
void
TAsyncSerTool::DoGetComplete(NewtonErr result, Boolean endOfFrame)
{
	fIntMask &= ~kSerIntInputReady;
	GetComplete(result, endOfFrame, fGetBuffer->Position());
}


// ROM 0x000397d4 KillGet__13TAsyncSerToolFv
void
TAsyncSerTool::KillGet()
{
	if (fGetBuffer != nil)
		DoGetComplete(kCommErrRequestCanceled, false);
	FlushInputBytes();
	KillGetComplete(noErr);
}


// ROM 0x00039fac RxDataAvailable__13TAsyncSerToolFv
void
TAsyncSerTool::RxDataAvailable()
{
	if (fGetBuffer == nil)
		return;
	DoInput();
}


// ROM 0x0003a164 DataInObserver__13TAsyncSerToolFUc
// An XON or XOFF from the other end (output XON/XOFF on) starts or stops
// the output and is not kept.
Boolean
TAsyncSerTool::DataInObserver(UByte byte)
{
	if (!fOutFlow.useSoftFlowControl)
		return false;
	if (byte == fOutFlow.xonChar)
	{
		fOutFlow.softFlowBlocked = false;
		ContinueOutputIH(true);
		return true;
	}
	if (byte == fOutFlow.xoffChar)
	{
		fOutFlow.softFlowBlocked = true;
		SuspendTxDMA();
		return true;
	}
	return false;
}


// ROM 0x0003a584 HandleCharIn__13TAsyncSerToolFUcUl
// A byte in (one with an error kept with a marker of its status; the
// buffer full, a soft overrun marked), the input stopped if it is filling,
// and the task told (after fSendForIntDelay, so bytes come in batches).
void
TAsyncSerTool::HandleCharIn(UByte byte, ULong status)
{
	UByte b = fDataMask & byte;
	ULong result;
	if (status == 0)
	{
		if (DataInObserver(b))
			return;
		result = fInBuf.PutNextByte(b);
	}
	else
		result = fInBuf.PutNextByte(b, status);
	if (result == kCircleBufFull)
	{
		status += kSerialRxSoftOverrun;
		fInBuf.PutEOM(kSerialRxSoftOverrun);
	}
	if (status != 0)
		UpdateStats(status);
	if (fInFlow.useSoftFlowControl || fInFlow.useHardFlowControl)
		DoInputFlowControl();
	if ((fIntMask & kSerIntInputReady) && (fIntFlags & kSerIntInputReady) == 0)
	{
		fIntFlags |= kSerIntInputReady;
		IHRequest(fSendForIntDelay);
	}
}


// ROM 0x0003a668 DoInputFlowControl__13TAsyncSerToolFv
// The input buffer past its high water: RTS dropped, and an XOFF sent
// ahead of the data.
void
TAsyncSerTool::DoInputFlowControl()
{
	if (!fInFlow.useSoftFlowControl && !fInFlow.useHardFlowControl)
		return;
	if (fInBuf.BufferCount() <= fInputHighWater)
		return;
	if (fInFlow.useHardFlowControl && !fInFlow.hardFlowBlocked)
	{
		SetHSKo(false);
		fInFlow.hardFlowBlocked = true;
	}
	if (fInFlow.useSoftFlowControl && !fInFlow.softFlowBlocked)
	{
		fInFlow.softFlowBlocked = true;
		if (fFlowChar != fInFlow.xoffChar)
		{
			fFlowChar = fInFlow.xoffChar;
			SuspendTxDMA();
			ContinueOutputIH(true);
		}
	}
}


// ROM 0x0003aa9c RxCAvailInt__13TAsyncSerToolFv
// A byte has come (and, a chip that says a byte's status, all the rest in
// its FIFO).
void
TAsyncSerTool::RxCAvailInt(void* tool)
{
	TAsyncSerTool* self = (TAsyncSerTool*) tool;
	if (self->fChip == nil)
		return;
	self->HandleCharIn(self->fChip->GetByte(), 0);
	if (self->fFeatures & kSerFeatureGetErrByte)
	{
		while (self->fChip->RxBufFull())
		{
			UByte byte;
			RxErrorStatus status = self->fChip->GetByteAndStatus(&byte);
			self->HandleCharIn(byte, status);
		}
	}
}


// ROM 0x0003aae4 RxCSpecialInt__13TAsyncSerToolFv
// A byte with an error.
void
TAsyncSerTool::RxCSpecialInt(void* tool)
{
	TAsyncSerTool* self = (TAsyncSerTool*) tool;
	TSerialChip* chip = self->fChip;
	if (chip == nil)
		return;
	UByte byte;
	ULong status;
	if ((self->fFeatures & kSerFeatureGetErrByte) == 0)
	{
		byte = chip->GetByte();
		status = self->fChip->GetRxErrorStatus();
	}
	else
		status = chip->GetByteAndStatus(&byte);
	self->HandleCharIn(byte, status & 0xFF);
}


// ROM 0x0003ab40 EmptyInFIFO__13TAsyncSerToolFv
void
TAsyncSerTool::EmptyInFIFO()
{
	while (fChip->RxBufFull())
	{
		UByte byte;
		RxErrorStatus status = fChip->GetByteAndStatus(&byte);
		HandleCharIn(byte, status);
	}
}


// ROM 0x0003abc8 RxMultiByteInterrupt__13TAsyncSerToolFUl
// The input DMA channel has brought in bytes.
void
TAsyncSerTool::RxMultiByteInterrupt(void* tool, RxErrorStatus status)
{
	TAsyncSerTool* self = (TAsyncSerTool*) tool;
	if (status != 0)
		self->UpdateStats(status);
	if (self->fInFlow.useSoftFlowControl || self->fInFlow.useHardFlowControl)
		self->DoInputFlowControl();
	if ((self->fIntMask & kSerIntInputReady) && (self->fIntFlags & kSerIntInputReady) == 0)
	{
		self->fIntFlags |= kSerIntInputReady;
		self->IHRequest(self->fSendForIntDelay);
	}
}


/*------------------------------------------------------------------------------
	The lines and the task's side of the interrupts
------------------------------------------------------------------------------*/

// ROM 0x0003ab94 GetChannelIntHandlers__13TAsyncSerToolFP14SCCChannelInts
void
TAsyncSerTool::GetChannelIntHandlers(SCCChannelInts* handlers)
{
	handlers->TxBEmptyIntHandler = TxBEmptyInt;
	handlers->ExtStsIntHandler = ExtStatusInt;
	handlers->RxCAvailIntHandler = RxCAvailInt;
	handlers->RxCSpecialIntHandler = RxCSpecialInt;
}


// ROM 0x0003a81c ExtStatusInt__13TAsyncSerToolFv
// A line has changed (a chip that is not version 2 has CTS the other way
// up and no DSR or ring): the chip gone, CTS (flow control; changing more
// than fCTSChangeLimit times in fCTSInterval on a chip whose CTS can be a
// clock, its interrupt turned off), DCD (a carrier lost after
// carrierDetectDownTime, by the carrier timer), DSR, break - each an event
// the task is told of if it asked.
void
TAsyncSerTool::ExtStatusInt(void* tool)
{
	TAsyncSerTool* self = (TAsyncSerTool*) tool;
	ULong events = 0;
	ULong before = self->fIntFlags;
	if (self->fChip == nil)
		return;
	ULong status = self->fChip->GetSerialStatus();
	if ((self->fFeatures & kSerFeatureVersion2) == 0)
		status = (status ^ kSerialCTSAsserted) & ~(kSerialRIAsserted | kSerialDSRAsserted);
	if (status == kSerialChipGone)
	{
		self->fChip = nil;
		self->fFeatures = 0;
		self->fIntFlags |= kSerIntChipGone;
		self->IHRequest(0);
		return;
	}
	self->fChip->ResetSerialStatus();
	ULong changed = self->fSerialStatus ^ (status & 0xFF);
	self->fSerialStatus = status;
	if (changed & kSerialCTSAsserted)
	{
		if (self->fOutFlow.useHardFlowControl)
		{
			Boolean blocked = (status & kSerialCTSAsserted) == 0;
			self->fOutFlow.hardFlowBlocked = blocked;
			if (blocked)
				self->SuspendTxDMA();
			else
				self->ContinueOutputIH(true);
		}
		if (++self->fCTSChanges > self->fCTSChangeLimit && (self->fFeatures & kSerFeatureCTSClock))
		{
			self->fCTSChanges = 0;
			TTime now = GetGlobalTime();
			if (CompCompare(&now.time, &self->fCTSIntervalEnd.time) < 0)
			{
				self->fChip->SetIntSourceEnable(kSerIntSrcCTS, false);
				events = 0x40;
			}
			self->fCTSIntervalEnd = TimeFromNow(self->fCTSInterval);
		}
		events |= (status & kSerialCTSAsserted) ? 0x20 : 0x10;
	}
	if (changed & kSerialDCDAsserted)
	{
		if ((status & kSerialDCDAsserted) == 0)
		{
			if (self->fIntMask & 4)
			{
				if (self->fEventEnables.carrierDetectDownTime == 0)
					events |= 4;
				else
				{
					EnterFIQAtomic();
					self->fFIQTimer->SetFIQTimer(self->fCarrierTimer, self->fEventEnables.carrierDetectDownTime, 0);
					ExitFIQAtomic();
				}
			}
		}
		else
		{
			events |= 8;
			if (self->fEventEnables.carrierDetectDownTime != 0)
			{
				EnterFIQAtomic();
				self->fFIQTimer->ResetFIQTimer(self->fCarrierTimer);
				ExitFIQAtomic();
			}
		}
	}
	if (changed & kSerialDSRAsserted)
		events |= 0x100;
	if (changed & kSerialBreak)
		events |= (status & kSerialBreak) ? 1 : 2;
	ULong flags = self->fIntFlags | (self->fIntMask & events);
	self->fIntFlags = flags;
	if (flags != before)
	{
		Int64 zero = { 0, 0 };
		if (CompCompare(&self->fEventTime.time, &zero) == 0)
			self->fEventTime = GetGlobalTime();
		self->IHRequest(0);
	}
}


// ROM 0x0003aa68 CarrierTimerInterrupt__13TAsyncSerToolFUl
// The carrier has been down for carrierDetectDownTime.
void
TAsyncSerTool::CarrierTimerInterrupt(void* tool, ULong arg)
{
	TAsyncSerTool* self = (TAsyncSerTool*) tool;
	if ((self->fIntMask & 4) == 0)
		return;
	if (self->fIntFlags & 4)
		return;
	self->fIntFlags |= 4;
	self->IHRequest(0);
}


// ROM 0x0003a14c IHRequest__13TAsyncSerToolFUl
// Only one message on its way at a time.
void
TAsyncSerTool::IHRequest(ULong delay)
{
	if (fIntFlags & kSerIntRequested)
		return;
	fIntFlags |= kSerIntRequested;
	TSerTool::IHRequest(delay);
}


// ROM 0x00039fe8 IHReqHandler__13TAsyncSerToolFv
// In the task: what the interrupt handlers found, taken and done - the
// chip gone aborts the connection; else the output refilled, the input
// read, the events posted.
void
TAsyncSerTool::IHReqHandler()
{
	ULong flags = Swap(&fIntFlags, 0);
	if (flags & kSerIntChipGone)
	{
		if (fGetBuffer != nil)
			DoGetComplete(kCommErrConnectionAborted, false);
		if (fPutBuffer != nil)
			DoPutComplete(kCommErrConnectionAborted);
		TurnOff();
		StartAbort(kCommErrConnectionAborted);
		return;
	}
	ULong wanted = fIntMask & flags;
	if (wanted & kSerIntOutputDone)
		TxDataSent();
	if (wanted & kSerIntInputReady)
		RxDataAvailable();
	if (wanted & kSerIntEventMask)
		SerialEvents(wanted & kSerIntEventMask);
}


// ROM 0x00039fc8 SerialEvents__13TAsyncSerToolFUl
// The events the client asked for added to those pending, and posted if
// that changed anything.
void
TAsyncSerTool::SerialEvents(ULong events)
{
	ULong before = fEventData;
	fEventData = before | (fEventEnables.serEventEnables & events);
	if (fEventData != before)
		PostSerialEvent();
}


/*------------------------------------------------------------------------------
	TAsyncService
------------------------------------------------------------------------------*/

PROTOCOL_IMPL_SOURCE_MACRO(TAsyncService)
PROTOCOL_CLASSINFO(TAsyncService, "TCMService", "serv\0aser\0\0", 0x20000, 0, nil)	// ROM 0x00382c4c ClassInfo__13TAsyncServiceSFv

// ROM 0x0003b0cc New__13TAsyncServiceFv
TAsyncService*
TAsyncService::New()
{
	return this;
}


// ROM 0x0003b0d0 Delete__13TAsyncServiceFv
void
TAsyncService::Delete()
{ }


// ROM 0x0003b0d4 Start__13TAsyncServiceFP12TOptionArrayUlP12TServiceInfo
// The tool's task started (this copy is the parent's, thrown away once the
// task has its own) and opened with the endpoint's options.
NewtonErr
TAsyncService::Start(TOptionArray* options, ULong serviceId, TServiceInfo* serviceInfo)
{
	TAsyncSerTool tool(serviceId);
	NewtonErr err = StartCommTool(&tool, serviceId, serviceInfo);
	if (err == noErr)
		err = OpenCommTool(serviceInfo->GetPortId(), options, this);
	return err;
}


// ROM 0x0003b144 DoneStarting__13TAsyncServiceFP7TAEventUlP12TServiceInfo
// What the open answered.
NewtonErr
TAsyncService::DoneStarting(TAEvent* event, ULong size, TServiceInfo* serviceInfo)
{
	return ((TCommToolReply*) event)->fResult;
}


void
RegisterSerialCommServices(void)
{
	TAsyncService::ClassInfo()->Register();
	TFramedAsyncService::ClassInfo()->Register();
}
