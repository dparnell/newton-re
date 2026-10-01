/*
	File:		comms/SniffIRTool.cpp

	Contains:	TSniffIRTool and IRSniffService (SniffIRTool.h).

	Reconstructed from the MP2x00 US ROM (0x001e2c5c-0x001e34c0,
	0x000e9034-0x000e90bc); each function cites its origin.
*/

#include "SniffIRTool.h"
#include "HALOptions.h"
#include "CommErrors.h"
#include "NewtonTime.h"
#include "UserPorts.h"
#include "NameServer.h"

// how long after a failed start, or after telling the newt world, the
// sniffer tries again
#define kSniffRetryDelay	(2000 * kMilliseconds)		// (0x707ce0)

// the 'newt event that tells the newt world another machine is beaming
struct TSniffNewtEvent : public TAEvent
{
	ULong				fType;					// +0x08  'irMC
};


// ROM 0x001e2c5c __ct__12TSniffIRToolFUl
TSniffIRTool::TSniffIRTool(ULong serviceId)
	: TAsyncSerTool(serviceId)
{ }


// ROM 0x001e2cb4 __dt__12TSniffIRToolFv
TSniffIRTool::~TSniffIRTool()
{ }


// ROM 0x001e3430 GetSizeOf__12TSniffIRToolFv
// DEVIATION (pointer size): the host's size (the ROM's 0x4c8).
ULong
TSniffIRTool::GetSizeOf()
{
	return sizeof(TSniffIRTool);
}


// ROM 0x001e2dc4 GetToolName__12TSniffIRToolFv
UChar*
TSniffIRTool::GetToolName()
{
	return (UChar*) "SniffIR";
}


// ROM 0x001e31c8 TaskConstructor__12TSniffIRToolFv
// The built-in IR, claimed passively (so that a tool that wants the port
// takes it); input looked at after 18 ms, without DMA; powered, not yet
// connected, idle.
NewtonErr
TSniffIRTool::TaskConstructor()
{
	fSerFlags |= 1;
	NewtonErr err = TAsyncSerTool::TaskConstructor();
	if (err != noErr)
		return err;
	fSCCService = 2;
	fPassiveClaim = true;
	fPassiveState = true;
	fMiscConfig.inputDelay = 0x47fe;
	fPowered = true;
	fConnected = false;
	fState = 0;
	return noErr;
}


// ROM 0x001e342c TaskDestructor__12TSniffIRToolFv
void
TSniffIRTool::TaskDestructor()
{
	TAsyncSerTool::TaskDestructor();
}


// ROM 0x001e3160 ConnectStart__12TSniffIRToolFv
// Connecting and listening are the same: the endpoint is there, so sniff.
void
TSniffIRTool::ConnectStart()
{
	fConnected = true;
	NextState(kSniffMayStart);
	ConnectComplete(noErr);
}


// ROM 0x001e3128 ListenStart__12TSniffIRToolFv
void
TSniffIRTool::ListenStart()
{
	fConnected = true;
	NextState(kSniffMayStart);
	ListenComplete(noErr);
}


// ROM 0x001e3198 TerminateComplete__12TSniffIRToolFv
void
TSniffIRTool::TerminateComplete()
{
	fConnected = false;
	NextState(kSniffStop);
	TSerTool::TerminateComplete();
}


// ROM 0x001e2cf4 ResArbReleaseStart__12TSniffIRToolFPUcT1
// Another tool wants the port: given up for good (fField1D2 keeps the
// sniffer from starting again on this endpoint).
void
TSniffIRTool::ResArbReleaseStart(UChar* resName, UChar* resType)
{
	fField1D2 = true;
	NextState(kSniffStop);
	TSerTool::ResArbReleaseStart(resName, resType);
}


// ROM 0x001e2d34 ResArbClaimNotification__12TSniffIRToolFPUcT1
void
TSniffIRTool::ResArbClaimNotification(UChar* resName, UChar* resType)
{
	TSerTool::ResArbClaimNotification(resName, resType);
	NextState(kSniffMayStart);
}


// ROM 0x001e34a4 PowerOnEvent__12TSniffIRToolFUl
// (Not TSerTool's: the sniffer simply starts again, or stops.)
void
TSniffIRTool::PowerOnEvent(ULong /*reason*/)
{
	fPowered = true;
	NextState(kSniffMayStart);
}


// ROM 0x001e34b0 PowerOffEvent__12TSniffIRToolFUl
void
TSniffIRTool::PowerOffEvent(ULong /*reason*/)
{
	fPowered = false;
	NextState(kSniffStop);
}


// ROM 0x001e3274 AddDefaultOptions__12TSniffIRToolFP12TOptionArray
NewtonErr
TSniffIRTool::AddDefaultOptions(TOptionArray* options)
{
	TCMOSlowIRSniff sniff;
	NewtonErr err = options->InsertOptionAt(options->GetArrayCount(), &sniff);
	if (err == noErr)
		err = TAsyncSerTool::AddDefaultOptions(options);
	return err;
}


// ROM 0x001e32b8 AddCurrentOptions__12TSniffIRToolFP12TOptionArray
NewtonErr
TSniffIRTool::AddCurrentOptions(TOptionArray* options)
{
	NewtonErr err = options->InsertOptionAt(options->GetArrayCount(), &fSniff);
	if (err == noErr)
		err = TAsyncSerTool::AddCurrentOptions(options);
	return err;
}


// ROM 0x001e32f8 ProcessOptionStart__12TSniffIRToolFP7TOptionUlT2
// 'irsn: whether to sniff.  ROM BUG kept: every opcode copies the tool's own
// option *into* the one given (CopyDataFrom the wrong way round), so a set
// changes nothing - it only clears the option's status and starts or stops
// the sniffing by the setting the tool already had; the default answers a
// fresh option's.  Anything else is the serial tool's.
ULong
TSniffIRTool::ProcessOptionStart(TOption* theOption, ULong label, ULong opcode)
{
	if (label != kCMOSlowIRSniff)
		return TAsyncSerTool::ProcessOptionStart(theOption, label, opcode);
	if (opcode == opSetNegotiate || opcode == opSetRequired)
	{
		theOption->CopyDataFrom(&fSniff);
		theOption->SetOpCodeResult(0);
		NextState(fSniff.sniffEnable ? kSniffMayStart : kSniffStop);
	}
	else if (opcode == opGetDefault)
	{
		TCMOSlowIRSniff sniff;
		theOption->CopyDataFrom(&sniff);
	}
	else
		theOption->CopyDataFrom(&fSniff);
	return noErr;
}


// ROM 0x001e343c AllocateBuffers__12TSniffIRToolFv
// Small buffers (64 bytes each way: only the first bytes of a beam are
// looked at), no DMA.
NewtonErr
TSniffIRTool::AllocateBuffers()
{
	fMiscConfig.disableInputDMA = true;
	fMiscConfig.disableOutputDMA = true;
	fBuffers.fSendSize = 0x40;
	fBuffers.fRecvSize = 0x40;
	return TAsyncSerTool::AllocateBuffers();
}


// ROM 0x001e3458 SetIOParms__12TSniffIRToolFP17TCMOSerialIOParms
// Not the speed and framing (TSerTool's): the port is put into IrDA mode
// with auto-receive, so that it hears either kind of IR and says which.
void
TSniffIRTool::SetIOParms(TCMOSerialIOParms* /*opt*/)
{
	THMOSerIRLinkConfig config;
	config.SetOpCode(opSetRequired);
	config.fIRLinkMode = kSerIRLink_IRDA_3_16;
	config.fConfigFlags = kSerIRLinkCfg_AutoRx;
	fChip->ProcessOption(&config);
}


// ROM 0x001e2d64 DoOutput__12TSniffIRToolFv
// The sniffer sends nothing.  ROM QUIRK kept: a put is refused by
// completing the *get* (GetComplete, not PutComplete) with
// kCommErrNotSupported.
void
TSniffIRTool::DoOutput()
{
	if (fPutBuffer == nil)
		return;
	GetComplete(kCommErrNotSupported, false, 0);
}


// ROM 0x001e2d60 TxDataSent__12TSniffIRToolFv
void
TSniffIRTool::TxDataSent()
{ }


// ROM 0x001e2dd4 RxDataAvailable__12TSniffIRToolFv
void
TSniffIRTool::RxDataAvailable()
{
	DoInput();
}


// ROM 0x001e2ddc DoInput__12TSniffIRToolFv
// Bytes in: the port asked which kind of IR they came as, and the buffer
// looked at for the start of a beam.
void
TSniffIRTool::DoInput()
{
	fIntMask |= 0x40000000;
	SyncInputBuffer();
	if (fInBuf.BufferCount() == 0)
		return;
	THMOSerIRLinkConfig config;
	config.fIRLinkMode = kSerIRLink_IRDA_Any;
	config.fConfigFlags = kSerIRLinkCfg_AutoRx;
	config.SetOpCode(opGetCurrent);
	fChip->ProcessOption(&config);
	fIrDADetected = (config.fStatus & kSerIRLinkSts_IRDADetect) != 0;
	NextState(kSniffInput);
}


// ROM 0x001e2d58 WakeUpHandler__12TSniffIRToolFv
void
TSniffIRTool::WakeUpHandler()
{
	NextState(kSniffWakeUp);
}


// ROM 0x001e2d90 StartReceive__12TSniffIRToolFv
void
TSniffIRTool::StartReceive()
{
	FlushInputBytes();
	DoInput();
}


// ROM 0x001e2db4 StopReceive__12TSniffIRToolFv
void
TSniffIRTool::StopReceive()
{
	fIntMask &= ~0x40000000;
}


// ROM 0x001e2e68 CheckBufferForValidInput__12TSniffIRToolFv
Boolean
TSniffIRTool::CheckBufferForValidInput()
{
	return fIrDADetected ? CheckBufferForIrDAData() : CheckBufferForSharpData();
}


// ROM 0x001e2e94 CheckBufferForIrDAData__12TSniffIRToolFv
// An IrDA frame beginning: BOF (0xc0), the broadcast address (0xff), and an
// XID command (0x3f) or a TEST command (0xf3) - a discovery or the probe's
// TEST frame.  The bytes looked at are used up; an error byte breaks no
// sequence.
Boolean
TSniffIRTool::CheckBufferForIrDAData()
{
	Boolean found = false;
	ULong state = 0;
	do
	{
		UByte byte;
		ULong value;
		ULong result = fInBuf.GetNextByte(&byte, &value);
		if (result == 0)
		{
			if (state == 0)
			{
				if (byte == 0xc0)
					state = 1;
			}
			else if (state == 1)
				state = (byte == 0xff) ? 2 : 0;
			else if (state == 2)
			{
				if (byte == 0x3f || byte == 0xf3)
				{
					found = true;
					break;
				}
				state = 0;
			}
		}
		else if (result == 2)
			break;
	} while (!found);
	return found;
}


// ROM 0x001e2f3c CheckBufferForSharpData__12TSniffIRToolFv
// A Sharp IR packet beginning: a lead-in byte (0x96, 0x9b or 0x90) and then
// 0x85, or 0x82 and an ENQ (5).  A byte with an error marker counts only if
// the marker is 0x40; one with any other marker is passed over without
// looking whether the buffer has run out.
Boolean
TSniffIRTool::CheckBufferForSharpData()
{
	Boolean found = false;
	ULong state = 0;
	do
	{
		UByte byte;
		ULong value;
		ULong result = fInBuf.GetNextByte(&byte, &value);
		if (result == 1 && value != 0x40)
			continue;
		if (result == 0 || result == 1)
		{
			if (state == 0)
			{
				if (byte == 0x96 || byte == 0x9b || byte == 0x90)
					state = 1;
			}
			else if (state == 1)
			{
				if (byte == 0x85)
					found = true;
				else if (byte == 0x82)
					state = 2;
				else
					state = 0;
			}
			else if (state == 2)
			{
				if (byte == 5)
					found = true;
				else
					state = 0;
			}
		}
		if (result == 2)
			break;
	} while (!found);
	return found;
}


// ROM 0x001e3008 NextState__12TSniffIRToolFQ212TSniffIRTool12IRSniffEvent
// 1: start, if powered, connected, enabled and not given up; 2: stop; 3 and
// 4: turn the port on (failing that, try again in two seconds) and listen;
// 5: the two seconds are up; 6: bytes came - a beam beginning lets go of
// the port, tells the newt world, and tries again in two seconds.
void
TSniffIRTool::NextState(ULong event)
{
	switch (event)
	{
	case kSniffMayStart:
		if (!(fPowered && fConnected && fSniff.sniffEnable))
			return;
		if (fField1D2)
			return;
		fState = 1;
		NextState(kSniffStart);
		return;

	case kSniffStop:
		if (fState == 2 || fState == 3)
		{
			StopReceive();
			KillWakeUp();
		}
		if (fChipOn)
			SniffStop();
		fState = 0;
		return;

	case kSniffStart:
	case kSniffRetry:
		if (SniffStart() != noErr)
			break;
		StartReceive();
		fState = 2;
		return;

	case kSniffWakeUp:
		NextState(kSniffRetry);
		return;

	case kSniffInput:
		if (!CheckBufferForValidInput())
			return;
		SniffStop();
		NotifyUser();
		break;

	default:
		return;
	}
	fState = 3;
	SendWakeUp(kSniffRetryDelay);
}


// ROM 0x001e3228 SniffStart__12TSniffIRToolFv
NewtonErr
TSniffIRTool::SniffStart()
{
	return TurnOn();
}


// ROM 0x001e3230 SniffStop__12TSniffIRToolFv
// The port turned off and unbound, its buffers kept.
void
TSniffIRTool::SniffStop()
{
	Boolean allocated = fBuffersAllocated;
	fBuffersAllocated = false;
	TurnOffSerChip();
	UnbindToSerChip();
	fBuffersAllocated = allocated;
}


// ROM 0x001e33a8 NotifyUser__12TSniffIRToolFv
// The newt world told ('newt/'idle, type 'irMC), which runs the root's
// IRConnectRequest.  DEVIATION: the ROM sends it to gNewtPort, the newt
// world's (a library above this one): the host finds that port by its name,
// as the alert manager does.
void
TSniffIRTool::NotifyUser()
{
	TSniffNewtEvent event;
	event.fAEventClass = 'newt';
	event.fAEventID = 'idle';
	event.fType = 'irMC';
	TUNameServer nameServer;
	TObjectId newtPort = 0;
	ULong spec;
	if (nameServer.Lookup("newt", "TUPort", &newtPort, &spec) == noErr)
	{
		TUPort port(newtPort);
		port.Send(&event, sizeof(event));
	}
}


/*------------------------------------------------------------------------------
	IRSniffService ('snif')
------------------------------------------------------------------------------*/

PROTOCOL_IMPL_SOURCE_MACRO(IRSniffService)
PROTOCOL_CLASSINFO(IRSniffService, "TCMService", "serv\0snif\0\0", 0x20000, 0, nil)

// ROM 0x000e903c New__14IRSniffServiceFv
IRSniffService*
IRSniffService::New()
{
	return this;
}


// ROM 0x000e9040 Delete__14IRSniffServiceFv
void
IRSniffService::Delete()
{ }


// ROM 0x000e9044 Start__14IRSniffServiceFP12TOptionArrayUlP12TServiceInfo
NewtonErr
IRSniffService::Start(TOptionArray* options, ULong serviceId, TServiceInfo* serviceInfo)
{
	TSniffIRTool tool(serviceId);
	NewtonErr err = StartCommTool(&tool, serviceId, serviceInfo);
	if (err == noErr)
		err = OpenCommTool(serviceInfo->GetPortId(), options, this);
	return err;
}


// ROM 0x000e90b8 DoneStarting__14IRSniffServiceFP7TAEventUlP12TServiceInfo
NewtonErr
IRSniffService::DoneStarting(TAEvent* event, ULong size, TServiceInfo* serviceInfo)
{
	return ((TCommToolReply*) event)->fResult;
}
