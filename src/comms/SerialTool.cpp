/*
	File:		comms/SerialTool.cpp

	Contains:	TSerTool and TSerToolReply (SerialTool.h).

	Reconstructed from the MP2x00 US ROM (0x001b8538-0x001b9d00); each
	function cites its origin.
*/

#include "SerialTool.h"
#include "FIQTimer.h"
#include "Atomic.h"
#include "BufferList.h"
#include "SystemEvents.h"
#include "VirtualMemory.h"
#include "UserSharedMem.h"
#include "NewtErrors.h"
#include "CommErrors.h"


// ROM 0x001b8538 __ct__13TSerToolReplyFv
// DEVIATION (pointer size): the reply's size is the host's (the ROM's 0x30).
TSerToolReply::TSerToolReply()
{
	fSize = sizeof(TSerToolReply);
}


// ROM 0x001b8574 __ct__8TSerToolFUl
// Told of power on and off (3).
TSerTool::TSerTool(ULong serviceId)
	: TCommTool(serviceId)
{
	fSerFlags = 3;
}


// ROM 0x001b8a68 __dt__8TSerToolFv
TSerTool::~TSerTool()
{ }


/*------------------------------------------------------------------------------
	The chip: claimed, bound, turned on
------------------------------------------------------------------------------*/

// ROM 0x001b8718 LookUpSerialChip__8TSerToolFUl
// The chip at a location; a PCMCIA slot's ('slot' - either) says which.
NewtonErr
TSerTool::LookUpSerialChip(ULong hwLocation)
{
	SerialChipID id = fRegistry->FindByLocation(hwLocation);
	if (id == kNilSerChipID)
		return kCommErrResourceNotAvailable;
	fChipId = id;
	fChip = fRegistry->GetChipPtr(id);
	if (hwLocation == kHWLocPCMCIAAnySlot)
		fChipSpec.fHWLoc = fRegistry->GetChipLocation(id);
	return noErr;
}


// ROM 0x001b8810 ClaimSerialChip__8TSerToolFv
// Which chip: the one 'sers/'schp named, or else the one side B means (the
// infrared), the service's default, what the 'scc service asks for (the
// infrared; a PCMCIA slot, either), and failing all of them the external
// port.  Claimed from the registry for the tool's port; a version 2 chip
// is asked what it is (its features, into fChipSpec).
NewtonErr
TSerTool::ClaimSerialChip()
{
	if (fChipClaimed)
		return noErr;
	NewtonErr err = kCommErrResourceNotAvailable;
	ULong location;
	if (!fChipSpecSet)
	{
		ULong alternative = kHWLocBuiltInIR;
		NewtonErr found;
		location = kHWLocBuiltInIR;
		if (fSCCSide == sideB)
			found = LookUpSerialChip(location);
		else
		{
			ULong defaultLocation;
			found = fRegistry->GetDefaultChip(fServiceId, &defaultLocation);
			if (found == noErr)
			{
				location = defaultLocation;
				found = LookUpSerialChip(location);
			}
		}
		if (found == noErr)
			goto claim;
		if (fSCCService == 2)
		{
			if (LookUpSerialChip(alternative) == noErr)
				goto claim;
		}
		else if (fSCCService == 3)
		{
			if (LookUpSerialChip(kHWLocPCMCIASlot1) == noErr)
				goto claim;
			alternative = kHWLocPCMCIASlot2;
			if (LookUpSerialChip(alternative) == noErr)
				goto claim;
		}
		location = kHWLocExternalSerial;
	}
	else
	{
		SerialChipID id = fRegistry->FindByOption(&fChipSpec);
		if (id != kNilSerChipID)
			fChipSpec.fHWLoc = fRegistry->GetChipLocation(id);
		location = fChipSpec.fHWLoc;
	}
	{
		NewtonErr lookErr = LookUpSerialChip(location);
		if (lookErr != noErr)
			return lookErr;
	}
claim:
	if (fChip != nil
	&&  (err = fRegistry->ClaimSerialChip(fChipId, fPassiveClaim, fToolPort.fId)) == noErr)
	{
		fChipClaimed = true;
		fField1D2 = fPassiveClaim;
		const TClassInfo* info = fChip->ClassInfo();
		if (info == nil || info->GetCapability(kCapability_Version_2) == nil)
			fFeatures = kSerFeatureDefaults;
		else
		{
			fFeatures = fChip->GetFeatures();
			fChipSpec.Reset();
			fChipSpec.SetOpCode(opGetCurrent);
			fChip->ProcessOption(&fChipSpec);
		}
		fHalfDuplex = (fFeatures & kSerFeatureTxConfigNeeded) != 0;
		fConfigureForOutput = fHalfDuplex;
	}
	return err;
}


// ROM 0x001b86d0 UnclaimSerialChip__8TSerToolFv
// (Claiming it for port nought gives it back.)
void
TSerTool::UnclaimSerialChip()
{
	if (!fChipClaimed)
		return;
	fRegistry->ClaimSerialChip(fChipId, fPassiveClaim, 0);
	fChipClaimed = false;
	fChip = nil;
	fChipId = kNilSerChipID;
	fFeatures = 0;
}


// ROM 0x001b864c BindToSerChip__8TSerToolFv
// The tool's interrupt handlers installed in the chip, and the buffers
// allocated.
NewtonErr
TSerTool::BindToSerChip()
{
	fChip = fRegistry->GetChipPtr(fChipId);
	if (fChip == nil)
		return kCommErrResourceNotAvailable;
	SCCChannelInts handlers;
	GetChannelIntHandlers(&handlers);
	NewtonErr err = fChip->InstallChipHandler(this, &handlers);
	if (err == noErr)
	{
		fChipBound = true;
		if (!fBuffersAllocated)
			err = AllocateBuffers();
	}
	return err;
}


// ROM 0x001b8604 UnbindToSerChip__8TSerToolFv
void
TSerTool::UnbindToSerChip()
{
	if (fBuffersAllocated)
		DeallocateBuffers();
	if (fChip != nil)
		fChip->RemoveChipHandler(this);
	fChipBound = false;
}


// ROM 0x001b8b44 TurnOn__8TSerToolFv
// Bound (if it is not yet) and the chip turned on; the events forgotten.
NewtonErr
TSerTool::TurnOn()
{
	if (fBuffersAllocated && fChipBound && fChipOn)
		return noErr;
	fEventData = 0;
	fEventTime.time.hi = 0;
	fEventTime.time.lo = 0;
	if (!fChipClaimed)
		return kCommErrResourceNotAvailable;
	if (!fChipBound)
	{
		NewtonErr err = BindToSerChip();
		if (err != noErr)
			return err;
	}
	return TurnOnSerChip();
}


// ROM 0x001b8bdc TurnOff__8TSerToolFv
void
TSerTool::TurnOff()
{
	TurnOffSerChip();
	UnbindToSerChip();
}


// ROM 0x001b9900 CleanUp__8TSerToolFv
void
TSerTool::CleanUp()
{
	if (fChipOn)
		TurnOffSerChip();
	if (fChipBound)
		UnbindToSerChip();
	if (fChipClaimed)
		UnclaimSerialChip();
}


// ROM 0x001b9aa8 PowerOnEvent__8TSerToolFUl
// A chip that was on (or turned off by the power going) turned on again.
// (fPoweredOff is never cleared.)
void
TSerTool::PowerOnEvent(ULong reason)
{
	if (!fChipOn && !fPoweredOff)
		return;
	fChipOn = false;
	TurnOnSerChip();
}


// ROM 0x001b9ad4 PowerOffEvent__8TSerToolFUl
void
TSerTool::PowerOffEvent(ULong reason)
{
	if (!fChipOn)
		return;
	fPoweredOff = true;
	TurnOffSerChip();
}


/*------------------------------------------------------------------------------
	The task
------------------------------------------------------------------------------*/

// ROM 0x001b923c TaskConstructor__8TSerToolFv
// The defaults (side A of SCC "1", the chip found by location), the system
// events asked for, the two messages the tool sends itself, and the stack
// and the tool itself locked down (interrupt handlers run in them).
NewtonErr
TSerTool::TaskConstructor()
{
	fPutBuffer = nil;
	fGetBuffer = nil;
	fSCCChip = (SCCChip) "1";			// (the ROM's own copy of SCC1's string, 0x001d9958)
	fSCCSide = sideA;
	fSCCService = 0;
	fChipSpecSet = false;
	fChip = nil;
	fChipId = kNilSerChipID;
	fFeatures = 0;
	fDataMask = 0xFF;
	fFIQTimer = GetFIQTimerObject();
	fRegistry = GetSerialChipRegistry();
	fChipClaimed = false;
	fBuffersAllocated = false;
	fChipBound = false;
	fChipOn = false;
	fPoweredOff = false;
	fHalfDuplex = false;
	fEventData = 0;
	fEventTime.time.hi = 0;
	fEventTime.time.lo = 0;

	NewtonErr err = TCommTool::TaskConstructor();
	if (err != noErr)
		return err;
	if (fSerFlags & 3)
	{
		TSystemEvent event(0);
		if (fSerFlags & 2)
		{
			event.SetEvent(kSysEvent_PowerOn);
			if ((err = event.RegisterForSystemEvent(fToolPort.fId, 0, 0)) != noErr)
				return err;
		}
		if (fSerFlags & 1)
		{
			event.SetEvent(kSysEvent_PowerOff);
			if ((err = event.RegisterForSystemEvent(fToolPort.fId, 0, 0)) != noErr)
				return err;
		}
	}
	err = fIHMsg.Init(true);
	if (err == noErr)
	{
		fIHMsgId = fIHMsg.GetMsgId();
		fIHEvent.fAEventID = 'aser';
		err = fWakeMsg.Init(true);
		if (err == noErr)
		{
			fWakeMsgId = fWakeMsg.GetMsgId();
			fWakeEvent.fAEventID = 'aser';
			err = LockStack(&fLockStack, 0x400);
			if (err == noErr)
				err = LockHeapRange((VAddr) this, (VAddr) this + GetSizeOf(), true);
		}
	}
	return err;
}


// ROM 0x001b9650 TaskDestructor__8TSerToolFv
void
TSerTool::TaskDestructor()
{
	fFIQTimer->ReleaseFIQTimers(this);
	KillWakeUp();
	if (fSerFlags & 3)
	{
		TSystemEvent event(0);
		if (fSerFlags & 2)
		{
			event.SetEvent(kSysEvent_PowerOn);
			event.UnRegisterForSystemEvent(fToolPort.fId);
		}
		if (fSerFlags & 1)
		{
			event.SetEvent(kSysEvent_PowerOff);
			event.UnRegisterForSystemEvent(fToolPort.fId);
		}
	}
	CleanUp();
	TCommTool::TaskDestructor();
}


// ROM 0x001b9af8 HandleRequest__8TSerToolFR10TUMsgTokenUl
// The interrupt handlers' message and the wake-up are the tool's own; a
// power event is passed to the chip too; anything else is the base class's.
void
TSerTool::HandleRequest(TUMsgToken& msgToken, ULong msgType)
{
	if (msgToken.GetMsgId() == fIHMsgId)
	{
		IHReqHandler();
		CompleteRequest(msgToken, noErr);
		return;
	}
	if (msgToken.GetMsgId() == fWakeMsgId)
	{
		WakeUpHandler();
		CompleteRequest(msgToken, noErr);
		return;
	}
	TPowerEvent* event = (TPowerEvent*) fRequest;
	ULong type = event->fSysEventType;
	if (fRequestSize < 0x0C || event->fAEventID != kAESystemEventID)
	{
		TCommTool::HandleRequest(msgToken, msgType);
		return;
	}
	if (type == kSysEvent_PowerOff)
		PowerOffEvent(event->fReason);
	if (fFeatures & kSerFeatureVersion2)
		fChip->SysEventNotify(type);
	if (type == kSysEvent_PowerOn)
		PowerOnEvent(event->fReason);
	msgToken.ReplyRPC(nil, 0, 0);
}


// ROM 0x001b9c0c IHRequest__8TSerToolFUl
// A message to the tool's own port from interrupt level (after delay).
void
TSerTool::IHRequest(ULong delay)
{
	if (delay != 0)
	{
		TTime when = TimeFromNow(delay);
		SendForInterrupt(fToolPort.fId, fIHMsgId, 0, nil, 0, 0x4000000, 0, &when, true);
		return;
	}
	SendForInterrupt(fToolPort.fId, fIHMsgId, 0, nil, 0, 0x4000000, 0, nil, true);
}


// ROM 0x001b951c WakeUpHandler__8TSerToolFv
// The break ends.
void
TSerTool::WakeUpHandler()
{
	SetBreak(false);
}


// ROM 0x001b9488 SendWakeUp__8TSerToolFUl
NewtonErr
TSerTool::SendWakeUp(ULong delay)
{
	TTime when = TimeFromNow(delay);
	return fToolPort.Send(&fWakeMsg, &fWakeEvent, sizeof(TAEvent), 0, &when, 0, false);
}


// ROM 0x001b94f8 KillWakeUp__8TSerToolFv
NewtonErr
TSerTool::KillWakeUp()
{
	NewtonErr err = fWakeMsg.Abort();
	if (err == -10039)			// (the message was not queued)
		err = noErr;
	return err;
}


/*------------------------------------------------------------------------------
	The chip's lines
------------------------------------------------------------------------------*/

// ROM 0x001b9420 ChangeSpeed__8TSerToolFUl
// ==> the speed the chip could do (0 with no chip).
InterfaceSpeed
TSerTool::ChangeSpeed(ULong bitsPerSec)
{
	fIOParms.fSpeed = bitsPerSec;
	if (fChip == nil)
		return 0;
	InterfaceSpeed speed = fChip->SetSpeed(bitsPerSec);
	fIOParms.fSpeed = speed;
	if (fChipOn)
	{
		if (fFeatures & kSerFeatureWaitForAllSent)
			fChip->WaitForAllSent();
		EnterFIQAtomic();
		if (fChip != nil)
			fChip->Reconfigure();
		ExitFIQAtomic();
	}
	return speed;
}


// ROM 0x001b91e8 SetIOParms__8TSerToolFP17TCMOSerialIOParms
void
TSerTool::SetIOParms(TCMOSerialIOParms* opt)
{
	fIOParms.CopyDataFrom(opt);
	if (fChip != nil)
		fChip->SetIOParms(&fIOParms);
	ChangeSpeed(opt->fSpeed);
	fDataMask = 0xFF >> (8 - fIOParms.fDataBits);
}


// ROM 0x001b9524 SetBreak__8TSerToolFUc
void
TSerTool::SetBreak(Boolean assert)
{
	EnterFIQAtomic();
	if (fChip != nil)
		fChip->SetBreak(assert);
	ExitFIQAtomic();
}


// ROM 0x001b9554 SetTxDTransceiverEnable__8TSerToolFUc
void
TSerTool::SetTxDTransceiverEnable(Boolean enable)
{
	EnterFIQAtomic();
	if (fFeatures & kSerFeatureTriStateTxD)
		fChip->SetTxDTransceiverEnable(enable);
	ExitFIQAtomic();
}


// ROM 0x001b9588 SetHSKo__8TSerToolFUc
// (HSKo is RTS.)
void
TSerTool::SetHSKo(Boolean assert)
{
	EnterFIQAtomic();
	if (fChip != nil)
	{
		if (assert)
			fChip->SetSerialOutputs(kSerialOutputRTS);
		else
			fChip->ClearSerialOutputs(kSerialOutputRTS);
	}
	ExitFIQAtomic();
}


// ROM 0x001b95cc SetSerialOutputs__8TSerToolFUlT1
void
TSerTool::SetSerialOutputs(ULong toSet, ULong toClear)
{
	EnterFIQAtomic();
	if (fChip != nil)
	{
		if (toSet != 0)
			fChip->SetSerialOutputs(toSet);
		if (toClear != 0)
			fChip->ClearSerialOutputs(toClear);
	}
	ExitFIQAtomic();
}


// ROM 0x001b9618 GetSerialOutputs__8TSerToolFv
SerialOutputControl
TSerTool::GetSerialOutputs()
{
	SerialOutputControl outputs = 0;
	EnterFIQAtomic();
	if (fChip != nil)
		outputs = fChip->GetSerialOutputs();
	ExitFIQAtomic();
	return outputs;
}


/*------------------------------------------------------------------------------
	Options
------------------------------------------------------------------------------*/

// ROM 0x001b912c SetSerialChipSpec__8TSerToolFP18TCMOSerialChipSpec
// Only before a chip is claimed.
NewtonErr
TSerTool::SetSerialChipSpec(TCMOSerialChipSpec* opt)
{
	if (fChip != nil)
		return opFailure;
	fChipSpec.CopyDataFrom(opt);
	fChipSpecSet = true;
	return noErr;
}


// ROM 0x001b916c SetSerialChipLocation__8TSerToolFP19TCMOSerialHWChipLoc
// A location names the chip; none, a service says what kind.
NewtonErr
TSerTool::SetSerialChipLocation(TCMOSerialHWChipLoc* opt)
{
	if (fChip != nil)
		return opFailure;
	ULong location = opt->fHWLoc;
	ULong service = opt->fService;
	fChipSpec.fHWLoc = location;
	if (location == 0)
	{
		if (service != 0)
			fSCCService = service;
	}
	else
		fChipSpecSet = true;
	return noErr;
}


// ROM 0x001b91b0 SetSerialChipSelect__8TSerToolFP18TCMOSerialHardware
NewtonErr
TSerTool::SetSerialChipSelect(TCMOSerialHardware* opt)
{
	if (fChip != nil)
		return opFailure;
	fSCCChip = opt->fSCCChip;
	fSCCSide = opt->fSCCSide;
	fSCCService = opt->fSCCService;
	return noErr;
}


// ROM 0x001b8c08 AddDefaultOptions__8TSerToolFP12TOptionArray
NewtonErr
TSerTool::AddDefaultOptions(TOptionArray* options)
{
	TCMOSerialIOParms ioParms;
	TCMOSerialHWChipLoc chipLoc;
	TCMOSerialHalfDuplex halfDuplex;
	NewtonErr err = options->InsertOptionAt(options->GetArrayCount(), &ioParms);
	if (err == noErr)
		err = options->InsertOptionAt(options->GetArrayCount(), &chipLoc);
	if (err == noErr)
		err = options->InsertOptionAt(options->GetArrayCount(), &halfDuplex);
	if (err == noErr)
		TCommTool::AddDefaultOptions(options);		// (what it answers is not looked at)
	return err;
}


// ROM 0x001b8c8c AddCurrentOptions__8TSerToolFP12TOptionArray
// (TCommTool's are not added.)
NewtonErr
TSerTool::AddCurrentOptions(TOptionArray* options)
{
	NewtonErr err = options->InsertOptionAt(options->GetArrayCount(), &fIOParms);
	if (err == noErr)
		err = options->InsertOptionAt(options->GetArrayCount(), &fChipSpec);
	if (err == noErr)
	{
		TCMOSerialHWChipLoc chipLoc;
		chipLoc.fHWLoc = fChipSpec.fHWLoc;
		err = options->InsertOptionAt(options->GetArrayCount(), &chipLoc);
	}
	if (err == noErr)
	{
		TCMOSerialHalfDuplex halfDuplex;
		halfDuplex.fHalfDuplex = fHalfDuplex;
		err = options->InsertOptionAt(options->GetArrayCount(), &halfDuplex);
	}
	return err;
}


// ROM 0x001b8d40 ProcessOptionStart__8TSerToolFP7TOptionUlT2
// A set is opSetNegotiate or opSetRequired; a get, opGetDefault (the
// option's default) or opGetCurrent.  An option the tool does not know is
// the base class's, and then (a version 2 chip) the chip's.
ULong
TSerTool::ProcessOptionStart(TOption* opt, ULong label, ULong opcode)
{
	Boolean set = (opcode == opSetNegotiate || opcode == opSetRequired);
	switch (label)
	{
	case kCMOSerialHardware:
		if (set)
			return SetSerialChipSelect((TCMOSerialHardware*) opt);
		if (opcode == opGetDefault)
		{
			TCMOSerialHardware def;
			opt->CopyDataFrom(&def);
			return noErr;
		}
		((TCMOSerialHardware*) opt)->fSCCChip = fSCCChip;
		((TCMOSerialHardware*) opt)->fSCCSide = fSCCSide;
		((TCMOSerialHardware*) opt)->fSCCService = fSCCService;
		return noErr;

	case kCMOSerialHalfDuplex:
		if (!fChipBound)
			break;
		if (set)
		{
			Boolean on;
			if (!((TCMOSerialHalfDuplex*) opt)->fHalfDuplex)
			{
				if (fFeatures & kSerFeatureTxConfigNeeded)
					return opFailure;
				on = false;
				fHalfDuplex = false;
			}
			else
			{
				on = true;
				fHalfDuplex = true;
				if ((fFeatures & kSerFeatureVersion2) == 0)
					return noErr;
			}
			fConfigureForOutput = on;
			return noErr;
		}
		{
			TCMOSerialHalfDuplex halfDuplex;
			if (opcode == opGetCurrent)
				halfDuplex.fHalfDuplex = fHalfDuplex;
			opt->CopyDataFrom(&halfDuplex);
		}
		return noErr;

	case kCMOSerialBytesAvailable:
		if (set)
			return opReadOnly;
		if (fChipOn)
		{
			ULong count;
			BytesAvailable(count);
			((TCMOSerialBytesAvailable*) opt)->fBytesAvailable = count;
			return noErr;
		}
		break;

	case kCMOSerialBitRate:
		if (set)
			((TCMOSerialBitRate*) opt)->fBitsPerSecond = ChangeSpeed(((TCMOSerialBitRate*) opt)->fBitsPerSecond);
		else if (opcode == opGetDefault)
		{
			TCMOSerialBitRate def;
			opt->CopyDataFrom(&def);
		}
		else
			((TCMOSerialBitRate*) opt)->fBitsPerSecond = fIOParms.fSpeed;
		return noErr;

	case kCMOSerialBreak:
		// (the break's length: the DDK's TCMOSerialBreak declares no field)
		if (set && fChipOn && SendWakeUp(*(ULong*) (opt + 1)) == noErr)
		{
			SetBreak(true);
			return noErr;
		}
		break;

	case kCMOSerialHWChipLoc:
		if (set)
			return SetSerialChipLocation((TCMOSerialHWChipLoc*) opt);
		if (opcode == opGetDefault)
		{
			TCMOSerialHWChipLoc def;
			opt->CopyDataFrom(&def);
			return noErr;
		}
		((TCMOSerialHWChipLoc*) opt)->fHWLoc = fChipSpec.fHWLoc;
		return noErr;

	case kCMOSerialCircuitControl:
		if (fChipOn && opcode != opGetDefault)
		{
			TCMOSerialCircuitControl* control = (TCMOSerialCircuitControl*) opt;
			if (set)
				SetSerialOutputs(control->fSerOutToSet, control->fSerOutToClear);
			control->fSerOutState = GetSerialOutputs();
			control->fSerInState = fSerialStatus;
			return noErr;
		}
		break;

	case kCMOSerialChipSpec:
		if (set)
			return SetSerialChipSpec((TCMOSerialChipSpec*) opt);
		if (opcode == opGetDefault)
		{
			TCMOSerialChipSpec def;
			opt->CopyDataFrom(&def);
			return noErr;
		}
		opt->CopyDataFrom(&fChipSpec);
		return noErr;

	case kCMOSerialIOParms:
		if (set)
			SetIOParms((TCMOSerialIOParms*) opt);
		else if (opcode == opGetDefault)
		{
			TCMOSerialIOParms def;
			opt->CopyDataFrom(&def);
			return noErr;
		}
		opt->CopyDataFrom(&fIOParms);
		return noErr;

	default:
		{
			ULong result = TCommTool::ProcessOptionStart(opt, label, opcode);
			if (result != (ULong) opBadOpCode && result != (ULong) opNotSupported)
				return result;
			if ((fFeatures & kSerFeatureVersion2) == 0)
				return result;
			return fChip->ProcessOption(opt);
		}
	}
	return opFailure;
}


/*------------------------------------------------------------------------------
	Requests
------------------------------------------------------------------------------*/

// ROM 0x001b9704 DoControl__8TSerToolFUlT1
// kSerToolTurnOnOff turns the chip on or off (and answers noErr whatever
// TurnOn said - the ROM's); anything else is the base class's.
NewtonErr
TSerTool::DoControl(ULong opCode, ULong msgType)
{
	if (opCode != kSerToolTurnOnOff)
		return TCommTool::DoControl(opCode, msgType);
	if (((TSerToolTurnOnOffRequest*) fRequest)->fTurnOn)
		TurnOn();
	else
		TurnOff();
	fReply.fResult = noErr;
	ControlComplete(fReply);
	return noErr;
}


// ROM 0x001b99bc ControlComplete__8TSerToolFR14TCommToolReply
void
TSerTool::ControlComplete(TCommToolReply& reply)
{
	CompleteRequest(kCommToolControlChannel, reply.fResult, reply);
}


// ROM 0x001b8ac0 DoKillControl__8TSerToolFUl
// A bind waiting on a passive claim is given up; anything else is the
// base class's.
NewtonErr
TSerTool::DoKillControl(ULong msgType)
{
	if (fRequests[kCommToolControlChannel].fRequestPending && fControlOpCode == kCommToolBind && fField1D2)
	{
		UnclaimSerialChip();
		fField1D2 = false;
		BindComplete(kCommErrRequestCanceled);
		KillRequestComplete(kCommToolRequestTypeControl, noErr);
		return noErr;
	}
	return TCommTool::DoKillControl(msgType);
}


// ROM 0x001b9c98 BindStart__8TSerToolFv
// A passive claim that succeeded waits (for the chip to be given up to it)
// before the bind is answered.
void
TSerTool::BindStart()
{
	NewtonErr err = ClaimSerialChip();
	if (err == noErr && fField1D2)
		return;
	BindComplete(err);
}


// ROM 0x001b9cd8 UnbindStart__8TSerToolFv
void
TSerTool::UnbindStart()
{
	UnclaimSerialChip();
	UnbindComplete(noErr);
}


// ROM 0x001b89e0 ConnectStart__8TSerToolFv
void
TSerTool::ConnectStart()
{
	ConnectComplete(TurnOn());
}


// ROM 0x001b8a10 ListenStart__8TSerToolFv
void
TSerTool::ListenStart()
{
	ListenComplete(TurnOn());
}


// ROM 0x001b8a40 TerminateComplete__8TSerToolFv
void
TSerTool::TerminateComplete()
{
	TurnOff();
	TCommTool::TerminateComplete();
}


// ROM 0x001b87d8 ResArbReleaseStart__8TSerToolFPUcT1
// Another tool wants the chip: given up, and claimed again passively.
void
TSerTool::ResArbReleaseStart(UChar* resName, UChar* resType)
{
	TurnOff();
	fField1D2 = true;
	ResArbReleaseComplete(noErr);
}


// ROM 0x001b8780 ResArbClaimNotification__8TSerToolFPUcT1
// The chip is the tool's again: a bind waiting on it is answered.
void
TSerTool::ResArbClaimNotification(UChar* resName, UChar* resType)
{
	fField1D2 = false;
	if (fRequests[kCommToolControlChannel].fRequestPending && fControlOpCode == kCommToolBind)
		BindComplete(noErr);
	CompleteRequest(kCommToolResArbChannel, noErr);
}


// ROM 0x001b975c PutBytes__8TSerToolFP11CBufferList
void
TSerTool::PutBytes(CBufferList* clientBuffer)
{
	fPutFramed = false;
	fPutEOF = false;
	StartOutput(clientBuffer);
}


// ROM 0x001b9778 PutFramedBytes__8TSerToolFP11CBufferListUc
void
TSerTool::PutFramedBytes(CBufferList* clientBuffer, Boolean endOfFrame)
{
	fPutEOF = endOfFrame;
	fPutFramed = true;
	StartOutput(clientBuffer);
}


// ROM 0x001b9794 StartOutput__8TSerToolFP11CBufferList
// Not with the chip off.
void
TSerTool::StartOutput(CBufferList* clientBuffer)
{
	if (!fChipOn)
	{
		PutComplete(kSerErr_ToolNotReady, 0);
		return;
	}
	fPutBuffer = clientBuffer;
	clientBuffer->Seek(0, kSeekFromBeginning);
	fPutSize = clientBuffer->GetSize();
	fOutputStarting = true;
	DoOutput();
}


// ROM 0x001b9818 PutComplete__8TSerToolFlUl
void
TSerTool::PutComplete(NewtonErr result, ULong putBytesCount)
{
	fPutBuffer = nil;
	TCommTool::PutComplete(result, putBytesCount);
}


// ROM 0x001b9824 GetBytes__8TSerToolFP11CBufferList
void
TSerTool::GetBytes(CBufferList* clientBuffer)
{
	fGetFramed = false;
	fGetImmediate = false;
	StartInput(clientBuffer);
}


// ROM 0x001b9840 GetBytesImmediate__8TSerToolFP11CBufferListl
void
TSerTool::GetBytesImmediate(CBufferList* clientBuffer, Size threshold)
{
	fGetFramed = false;
	fGetImmediate = true;
	fGetThresholdLeft = threshold;
	StartInput(clientBuffer);
}


// ROM 0x001b9864 GetFramedBytes__8TSerToolFP11CBufferList
void
TSerTool::GetFramedBytes(CBufferList* clientBuffer)
{
	fGetFramed = true;
	fGetImmediate = false;
	StartInput(clientBuffer);
}


// ROM 0x001b9884 StartInput__8TSerToolFP11CBufferList
void
TSerTool::StartInput(CBufferList* clientBuffer)
{
	fGetBuffer = clientBuffer;
	clientBuffer->Seek(0, kSeekFromBeginning);
	fGetSize = clientBuffer->GetSize();
	if (!fChipOn)
	{
		GetComplete(kSerErr_ToolNotReady, false, 0);
		return;
	}
	DoInput();
}


// ROM 0x001b9964 GetComplete__8TSerToolFlUcUl
// The part of the buffer that was not filled hidden.
void
TSerTool::GetComplete(NewtonErr result, Boolean endOfFrame, ULong getBytesCount)
{
	fGetBuffer->Hide(fGetSize, kSeekFromEnd);
	fGetBuffer = nil;
	TCommTool::GetComplete(result, endOfFrame, getBytesCount);
}


// ROM 0x001b99cc GetCommEvent__8TSerToolFv
// A serial event if there is one, else the base class's.
void
TSerTool::GetCommEvent()
{
	if (PostSerialEvent() == kCommErrNoEventPending)
		TCommTool::GetCommEvent();
}


// ROM 0x001b99f8 PostSerialEvent__8TSerToolFv
// The pending serial events (with the status lines in the top byte) posted.
NewtonErr
TSerTool::PostSerialEvent()
{
	if (fEventData == 0)
		return kCommErrNoEventPending;
	TCommToolGetEventReply reply;
	reply.fEventCode = 1;
	reply.fEventTime = fEventTime;
	reply.fEventData = fEventData;
	reply.fServiceId = fServiceId;
	reply.fEventData = reply.fEventData | ((fSerialStatus & 0x3F) << 24);
	if (PostCommEvent(reply, noErr) == noErr)
	{
		fEventData = 0;
		fEventTime.time.hi = 0;
		fEventTime.time.lo = 0;
	}
	return noErr;
}
