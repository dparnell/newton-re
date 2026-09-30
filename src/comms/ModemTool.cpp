/*
	File:		comms/ModemTool.cpp

	Contains:	TClassOneModem - its life, its requests, events and timer,
				the serial port and the modem's set-up, the reads and
				writes, and the options; and TModemService (ModemTool.h).
				The commands are ModemToolCommands.cpp, identifying the
				modem, connecting and hanging up ModemToolConnect.cpp, the
				fax side ModemToolFax.cpp.

	Reconstructed from the MP2x00 US ROM (0x0005cce0-0x000651cc, the service
	0x0011fad4-0x0011fb58); each function cites its origin.
*/

#include "ModemTool.h"
#include "CommErrors.h"
#include "NewtErrors.h"
#include "OSErrors.h"
#include "CommToolOptions.h"
#include "CommAddresses.h"
#include "OptionArray.h"
#include "NewtonMemory.h"
#include "AEvents.h"
#include "SystemEvents.h"
#include "VirtualMemory.h"

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/*------------------------------------------------------------------------------
	TClassOneModemCmdReply
------------------------------------------------------------------------------*/

// ROM 0x0005cce0 __ct__22TClassOneModemCmdReplyFv
// DEVIATION (pointer size): the host's size (the ROM's 0x3c).
TClassOneModemCmdReply::TClassOneModemCmdReply()
{
	fSize = sizeof(TClassOneModemCmdReply);
}


/*------------------------------------------------------------------------------
	TModemService
------------------------------------------------------------------------------*/

PROTOCOL_IMPL_SOURCE_MACRO(TModemService)
PROTOCOL_CLASSINFO(TModemService, "TCMService", "serv\0mods\0\0", 0x20000, 0, nil)

// ROM 0x0011fadc New__13TModemServiceFv
TModemService*
TModemService::New()
{
	return this;
}


// ROM 0x0011fae0 Delete__13TModemServiceFv
void
TModemService::Delete()
{ }


// ROM 0x0011fae4 Start__13TModemServiceFP12TOptionArrayUlP12TServiceInfo
NewtonErr
TModemService::Start(TOptionArray* options, ULong serviceId, TServiceInfo* serviceInfo)
{
	TClassOneModem tool(serviceId);
	NewtonErr err = StartCommTool(&tool, serviceId, serviceInfo);
	if (err == noErr)
		err = OpenCommTool(serviceInfo->GetPortId(), options, this);
	return err;
}


// ROM 0x0011fb58 DoneStarting__13TModemServiceFP7TAEventUlP12TServiceInfo
NewtonErr
TModemService::DoneStarting(TAEvent* event, ULong size, TServiceInfo* serviceInfo)
{
	return ((TCommToolReply*) event)->fResult;
}


void
RegisterModemService(void)
{
	TModemService::ClassInfo()->Register();
}


/*------------------------------------------------------------------------------
	TClassOneModem - its life
------------------------------------------------------------------------------*/

// ROM 0x0005d294 __ct__14TClassOneModemFUl
TClassOneModem::TClassOneModem(ULong serviceId)
	: TMNP(serviceId)
{ }


// ROM 0x0005f7d4 __dt__14TClassOneModemFv
TClassOneModem::~TClassOneModem()
{ }


// ROM 0x00060d2c GetSizeOf__14TClassOneModemFv
// DEVIATION (pointer size): the host's size (the ROM's 0xba4).
ULong
TClassOneModem::GetSizeOf()
{
	return sizeof(TClassOneModem);
}


// ROM 0x0038aae4 (unnamed) GetToolName - the vtable's +0x14
UChar*
TClassOneModem::GetToolName()
{
	return (UChar*) "Class One Modem";
}


// ROM 0x00065030 GetToolCapabilities__14TClassOneModemFv
ULong
TClassOneModem::GetToolCapabilities()
{
	return 0;
}


// ROM 0x0005fc84 TaskConstructor__14TClassOneModemFv
// MNP's set-up, then the modem's: the ids of the modems the ROM knows, the
// generic profile (answers waited for 2 seconds at 19200 bps), the command
// list with its "AT" and carriage return, the answers' one-byte buffer,
// the timer's message, and the dialing preferences' string to fill in.
NewtonErr
TClassOneModem::TaskConstructor()
{
	TULockStack lockStack;
	// BUG: the strings for EC only, EC falling back and cellular point at
	// this empty string on the stack while SetModemProfile copies them into
	// the profile (after which they point into the profile) - which is
	// why later comparisons of fCellularStr with a string on the stack never
	// hold (C1IdGetIdCmdResponse, C1IdACLSetProfile).
	UChar empty[1];
	empty[0] = 0;
	NewtonErr err = LockStack(&lockStack, 0x800);
	if (err != noErr)
		return err;
	fDialPrefs = nil;
	if ((err = TMNP::TaskConstructor()) != noErr)
		return err;
	fSCCService = 3;
	fModemIdStrings[0] = (const UChar*) kModemIdStr224;
	fModemIdStrings[1] = (const UChar*) kModemIdStr96V24A;
	fModemIdStrings[3] = (const UChar*) kModemIdStr96V24B;
	fModemIdStrings[2] = (const UChar*) kModemIdStr96V24D;
	fModemIdStrings[4] = (const UChar*) kModemIdStr96V24F;
	fModemIdStrings[5] = (const UChar*) kModemIdStr9624ACW;
	fModemIdStrings[6] = (const UChar*) kModemIdStrACL;
	fModemIdStrings[7] = (const UChar*) kModemIdStrELSA;
	fModemIdStrings[8] = (const UChar*) kModemIdStrBicHS;
	fListenTimer = 0;
	fModemFlags = kModemFlagDataClass;
	fHCodeModem = false;
	fCapKilled = false;
	fDataMode = 0;
	{
		TCMOModemECType ecType;
		fECType = ecType.fType;
	}
	fProfile = nil;
	fIdString = (const UChar*) kModemIdStrUnknown;
	fNoECStr = (const UChar*) kModemCmdStrNoECGeneric;
	fECOnlyStr = empty;
	fECFallBackStr = empty;
	fDirectStr = (const UChar*) kModemCmdStrDirect;
	fCellularStr = empty;
	if ((err = SetModemProfile()) == noErr)
	{
		fField5C8 = 1;
		fSerialSpeed = fProfile->fSerialSpeed;
		fResultLength = 0;
		if ((err = fCmdList.Init(false)) == noErr
		&&  (err = fCmdPrefix.Init((void*) cmdPrefix, strlen(cmdPrefix))) == noErr
		&&  (err = fCmdSuffix.Init((void*) cmdSuffix, strlen(cmdSuffix))) == noErr
		&&  (err = fResultList.Init(false)) == noErr
		&&  (err = fResultSegment.Init(1)) == noErr)
		{
			fResultList.InsertLast(&fResultSegment);
			if ((err = fModemTimerMsg.Init(false)) == noErr)
			{
				fModemTimerMsgId = fModemTimerMsg.GetMsgId();
				fLastResultTime = GetGlobalTime();
				fDialPrefs = (UChar*) NewPtr(strlen(kModemDialPrefStr) + 1);
				if (fDialPrefs != nil)
				{
					BlockMove(kModemDialPrefStr, fDialPrefs, strlen(kModemDialPrefStr) + 1);
					fResArbReleasing = false;
					fTAPIEvent = 0;
					fZeroStuffFlags = 0;
					fZeroBuffer = nil;
					fC2HangUpCode = 0;
					fC2HangUpHi = 0;
					fNextCommandDelay = 0;
					fCustomResponses = nil;
					return noErr;
				}
				// (the error of a failed NewPtr is not returned: the ROM
				// answers noErr with the task destroyed)
			}
		}
	}
	TaskDestructor();
	return err;
}


// ROM 0x00060448 TaskDestructor__14TClassOneModemFv
void
TClassOneModem::TaskDestructor()
{
	if (fDialPrefs != nil)
		DisposPtr((Ptr) fDialPrefs);
	if (fProfile != nil)
		DisposPtr((Ptr) fProfile);
	TMNP::TaskDestructor();
}


// ROM 0x0005fb7c OpenStart__14TClassOneModemFP12TOptionArray
NewtonErr
TClassOneModem::OpenStart(TOptionArray* options)
{
	NewtonErr err = TCommTool::OpenStart(options);
	if (err == noErr)
		fConnectSpeed = 0;
	return err;
}


// ROM 0x0005fba0 BindStart__14TClassOneModemFv
// The modem is identified when the chip is bound.
void
TClassOneModem::BindStart()
{
	fModemFlags |= kModemFlagIdentifying;
	fIdentifyState = 1;
	TSerTool::BindStart();
}


// ROM 0x0005fbb8 BindComplete__14TClassOneModemFl
void
TClassOneModem::BindComplete(NewtonErr result)
{
	if (result == noErr)
		result = TurnOn();
	if (result != noErr)
	{
		C1IdModemComplete(result);
		return;
	}
	ResetSerialDrvr(fProfile->fSerialSpeed, 0, 0, 8);
	SetInputSendForIntDelay(11058);
	fIdentifyState = 2;
	C1IdModem();
}


// ROM 0x0005fc44 UnbindStart__14TClassOneModemFv
void
TClassOneModem::UnbindStart()
{
	TurnOff();
	fHCodeModem = false;
	fVoiceSupport.fSupportsVoice = false;
	fPrefs.fNavigatorOpen = false;
	SetCDOption();
	TSerTool::UnbindStart();
}


// ROM 0x0005fb6c ReleaseStart__14TClassOneModemFv
NewtonErr
TClassOneModem::ReleaseStart()
{
	if (fDataMode != 2)
		return TCommTool::ReleaseStart();
	return TMNP::ReleaseStart();
}


// ROM 0x00062530 HandleReply__14TClassOneModemFUlT1
void
TClassOneModem::HandleReply(ULong userRefCon, ULong msgType)
{
	TCommTool::HandleReply(userRefCon, msgType);
}


/*------------------------------------------------------------------------------
	TClassOneModem - requests, events and the timer
------------------------------------------------------------------------------*/

// ROM 0x00061ae4 HandleRequest__14TClassOneModemFR10TUMsgTokenUl
// The modem's own timer, by its type; anything else is MNP's - and if the
// machine has just woken while the modem was waiting for a call, the
// modem is looked at again (an external one is given 2 seconds).
void
TClassOneModem::HandleRequest(TUMsgToken& msgToken, ULong msgType)
{
	if (msgToken.GetMsgId() == fModemTimerMsgId)
	{
		fModemFlags &= ~kModemFlagTimer;
		switch (fRequest[0])
		{
		case kModemTimerCommand:
		{
			ULong flags = fModemFlags;
			if (((flags & kModemFlagPacketPut) && !(flags & kModemFlagPutConnected))
			||  ((flags & kModemFlagPacketGet) && !(flags & kModemFlagFrameUnfinished)))
			{
				if (fPacketState != 10)
				{
					if (flags & kModemFlagPacketKill)
						return;
					fModemFlags |= kModemFlagPacketTimedOut;
					C1PktAbort();
					return;
				}
			}
			TimeOutCmdResult();
			break;
		}
		case kModemTimerReset:
			C1IdModem();
			break;
		case kModemTimerPutCommand:
			PutCommand();
			break;
		case kModemTimerPacket:
		case kModemTimerSilence:
			C1PktContinue(noErr);
			break;
		case kModemTimerPacketHDLC:
		case kModemTimerPacketData:
			if (fModemFlags & kModemFlagPacketKill)
				return;
			fModemFlags |= kModemFlagPacketTimedOut;
			C1PktAbort();
			break;
		case kModemTimerListen:
			StartAbort(kCommErrListenerTimeOut);
			break;
		case kModemTimerPowerOn:
			fConnectState = 1;
			if (!fResArbReleasing)
				ConnectModemContinue(noErr);
			break;
		case kModemTimerC2Setup:
			C2PktGetBytesSetupCont();
			break;
		}
		return;
	}

	Boolean powerOn = false;
	TPowerEvent* event = (TPowerEvent*) fRequest;
	if (fRequestSize >= 0x0C && event->fAEventID == kAESystemEventID && event->fSysEventType == kSysEvent_PowerOn)
		powerOn = true;
	TMNP::HandleRequest(msgToken, msgType);
	if (powerOn
	&&  (fToolState & kToolStateListenMode)
	&&  !(fToolState & kToolStateWantAbort)
	&&  fConnectState == 8
	&&  !fResArbReleasing)
	{
		fPoweredUp = true;
		AbortCommand();
		if (fChipSpec.fHWLoc != 'extr')
		{
			fConnectState = 1;
			ConnectModemContinue(noErr);
			return;
		}
		fConnectState = 13;
		NewtonErr err = PostTimer(kModemTimerPowerOn, 2000);
		if (err != noErr)
			StartAbort(err);
	}
}


// ROM 0x0005ec64 PostTimer__14TClassOneModemFUlT1
// The timer message sent to the tool's own port after so many milliseconds.
NewtonErr
TClassOneModem::PostTimer(ULong type, ULong milliseconds)
{
	fModemTimerType = type;
	TTime delay(milliseconds, kMilliseconds);
	TTime when = GetGlobalTime();
	CompAdd(&delay.time, &when.time);
	fModemTimerTime = when;
	NewtonErr err = fToolPort.Send(&fModemTimerMsg, &fModemTimerType, sizeof(fModemTimerType), 0, &fModemTimerTime, 0, false);
	if (err == noErr)
		fModemFlags |= kModemFlagTimer;
	return err;
}


// ROM 0x0005ec2c AbortTimer__14TClassOneModemFv
void
TClassOneModem::AbortTimer()
{
	if (fModemFlags & kModemFlagTimer)
	{
		fModemTimerMsg.Abort();
		fModemFlags &= ~kModemFlagTimer;
	}
}


// ROM 0x0005edfc HandleTimerTick__14TClassOneModemFv
// (The tick runs only while a page's data is being filled in.)
void
TClassOneModem::HandleTimerTick()
{
	ZeroStuffing();
	TCommTool::HandleTimerTick();
}


// ROM 0x0005f214 DoControl__14TClassOneModemFUlT1
NewtonErr
TClassOneModem::DoControl(ULong opCode, ULong msgType)
{
	switch (opCode)
	{
	case kModemCtlC1SendHDLC:
	case kModemCtlC1SendData:
		DoTransPkt();
		return noErr;
	case kModemCtlC1RecvHDLC:
	case kModemCtlC1RecvData:
		DoRecvPkt();
		return noErr;
	case kModemCtlGetResult:
		// DEVIATION (pointer size): the host's size of the request (the
		// ROM copies 0x2c bytes)
		memcpy(&fControl, fRequest, sizeof(fControl));
		fCommandTimeout = fProfile->fCommandTimeout;
		if (fControl.fTimeout != 0)
			fCommandTimeout = fControl.fTimeout;
		GetCommandResult();
		return noErr;
	case kModemCtlC2RecvPageData:
		C2ModemRecvPgData();
		return noErr;
	}
	if (opCode >= kModemCtlC2First && opCode <= kModemCtlC2Last)
		return C2DoCommand(opCode);
	return TMNP::DoControl(opCode, msgType);
}


// ROM 0x0006333c DoKillControl__14TClassOneModemFUl
// A connect is aborted; identifying the modem, a packet, or an option's
// question of the modem stopped.
NewtonErr
TClassOneModem::DoKillControl(ULong msgType)
{
	if (fToolState & kToolStateConnecting)
	{
		if (fTerminationEvent == 0)
			fTerminationEvent = 2;
		return StartAbort(kCommErrConnectionAborted);
	}
	if (fModemFlags & kModemFlagIdentifying)
	{
		if (fIdentifyState == 1)
			return TSerTool::DoKillControl(msgType);
		fModemFlags |= kModemFlagIdentifyKilled;
		AbortCommand();
		C1IdModemComplete(kCommErrRequestCanceled);
		KillRequestComplete(kCommToolRequestTypeControl, noErr);
		fModemFlags &= ~kModemFlagIdentifyKilled;
		return noErr;
	}
	if (fModemFlags & (kModemFlagPacketPut | kModemFlagPacketGet | kModemFlagPacketLine))
	{
		fModemFlags |= kModemFlagPacketKill;
		if (!(fModemFlags & kModemFlagPacketTimedOut))
			C1PktAbort();
		return noErr;
	}
	if (fModemFlags & kModemFlagCapabilities)
	{
		fCapKilled = true;
		AbortCommand();
		C1GetCapComplete(kCommErrRequestCanceled);
		KillRequestComplete(kCommToolRequestTypeControl, noErr);
		fCapKilled = false;
		return noErr;
	}
	KillRequestComplete(kCommToolRequestTypeControl, kCommErrBadCommand);
	return noErr;
}


// ROM 0x0005f8d0 GetCommEvent__14TClassOneModemFv
void
TClassOneModem::GetCommEvent()
{
	if (PostTapiEvent() == kCommErrNoEventPending)
		TSerTool::GetCommEvent();
}


// ROM 0x0005f8fc PostTapiEvent__14TClassOneModemFv
// A telephone API event (T_TAPIEVT with the event in its data) if one is
// waiting.
NewtonErr
TClassOneModem::PostTapiEvent()
{
	if (fTAPIEvent == 0)
		return kCommErrNoEventPending;
	TCommToolGetEventReply event;
	event.fEventCode = T_TAPIEVT;
	event.fEventTime.Set(0, 0);
	event.fEventData = fTAPIEvent;
	event.fServiceId = fServiceId;
	if (TCommTool::PostCommEvent(event, noErr) == noErr)
		fTAPIEvent = 0;
	return noErr;
}


// ROM 0x0005f97c PostCommEvent__14TClassOneModemFR22TCommToolGetEventReplyl
// The carrier dropping is not an event: if the preferences say so, a data
// connection is dropped with it.
NewtonErr
TClassOneModem::PostCommEvent(TCommToolGetEventReply& theEvent, NewtonErr result)
{
	if (theEvent.fEventCode == kCommToolEventSerialEvent && (theEvent.fEventData & kSerialEventDCDNegatedMask))
	{
		if (fPrefs.fDropOnCarrierLoss
		&&  result == noErr
		&&  !(fTAPIService.fActive && fTAPIService.fActive2)
		&&  (fModemFlags & kModemFlagDataClass)
		&&  (fToolState & kToolStateConnected))
			StartAbort(kCommErrNotConnected);
		return noErr;
	}
	return TCommTool::PostCommEvent(theEvent, result);
}


// ROM 0x0005f9f8 ResArbReleaseStart__14TClassOneModemFPUcT1
// The port wanted by someone else: the modem hung up first.
void
TClassOneModem::ResArbReleaseStart(UChar* resName, UChar* resType)
{
	fResArbReleasing = true;
	if (fConnectState != 13 && !HangUp())
		return;
	TurnOff();
	ResArbReleaseComplete(noErr);
}


// ROM 0x0005fa50 ResArbClaimNotification__14TClassOneModemFPUcT1
// The port back: turned on again, and a modem that was connecting or
// talking to the telephone API woken with an "AT".
void
TClassOneModem::ResArbClaimNotification(UChar* resName, UChar* resType)
{
	if (!fResArbReleasing)
	{
		TSerTool::ResArbClaimNotification(resName, resType);
		return;
	}
	fResArbReleasing = false;
	CompleteRequest(kCommToolResArbChannel, noErr);
	if (!(fToolState & kToolStateBound))
		return;
	NewtonErr err = TurnOn();
	if (err == noErr)
	{
		ResetSerialDrvr(fProfile->fSerialSpeed, 0, 0, 8);
		if (fConnectState == 13)
			return;
		if (!(fToolState & kToolStateConnecting) && !(fTAPIService.fActive && fTAPIService.fActive2))
			return;
		fModemFlags |= kModemFlagConnecting;
		fConnectState = 12;
		fCommandTimeout = 2000;
		err = BuildCommand((const UChar*) cmdPrefix, (UChar*) cmdSuffix, 0, nil, 0, nil, 0);
		if (err == noErr)
		{
			PutCommand();
			return;
		}
	}
	StartAbort(err);
}


/*------------------------------------------------------------------------------
	TClassOneModem - reads and writes
	Once connected, data goes through MNP (fDataMode 2) or straight through
	the serial tool; a command's put and get and a packet's are the modem's.
------------------------------------------------------------------------------*/

// ROM 0x00062ed4 PutBytes__14TClassOneModemFP11CBufferList
void
TClassOneModem::PutBytes(CBufferList* clientBuffer)
{
	if (fDataMode != 2)
		TSerTool::PutBytes(clientBuffer);
	else
		TMNP::PutBytes(clientBuffer);
}


// ROM 0x00063750 PutFramedBytes__14TClassOneModemFP11CBufferListUc
void
TClassOneModem::PutFramedBytes(CBufferList* clientBuffer, Boolean endOfFrame)
{
	if (fDataMode != 2)
		TSerTool::PutFramedBytes(clientBuffer, endOfFrame);
	else
		TMNP::PutFramedBytes(clientBuffer, endOfFrame);
}


// ROM 0x0005d410 PutComplete__14TClassOneModemFlUl
// A command's put, a page's data or fill bytes, a packet's put, or the
// client's.
void
TClassOneModem::PutComplete(NewtonErr result, ULong putBytesCount)
{
	if (fModemFlags & kModemFlagPutting)
	{
		fPutBuffer = nil;
		PutCommandComplete(result);
		return;
	}
	if (fZeroStuffFlags & kZeroStuffOn)
	{
		if (!(fZeroStuffFlags & kZeroStuffFillPut))
		{
			// the page's data gone: the fill counted again from nought
			fZeroStuffStart.Set(0, 0);
			fZeroStuffSent = 0;
			fPutBuffer = nil;
			C1PktContinue(result);
		}
		else
			fZeroStuffSent += putBytesCount;
		fZeroStuffFlags &= ~(kZeroStuffDataPut | kZeroStuffFillPut);
		if (!(fZeroStuffFlags & kZeroStuffKilling) && !fControl.fPacket.fFinal)
			ZeroStuffing();
		return;
	}
	if (fModemFlags & kModemFlagPacketPut)
	{
		fPutBuffer = nil;
		C1PktContinue(result);
	}
	else
		TMNP::PutComplete(result, putBytesCount);
}


// ROM 0x0005dbec KillPut__14TClassOneModemFv
void
TClassOneModem::KillPut()
{
	if (fDataMode != 2)
		TFramedAsyncSerTool::KillPut();
	else
		TMNP::KillPut();
}


// ROM 0x00063310 KillPutComplete__14TClassOneModemFl
// (Not the modem's own kills.)
void
TClassOneModem::KillPutComplete(NewtonErr result)
{
	if (!(fModemFlags & kModemFlagAborting) && !(fZeroStuffFlags & kZeroStuffKilling))
		TMNP::KillPutComplete(result);
}


// ROM 0x0005e384 GetBytes__14TClassOneModemFP11CBufferList
void
TClassOneModem::GetBytes(CBufferList* clientBuffer)
{
	if (fDataMode != 2)
		TSerTool::GetBytes(clientBuffer);
	else
		TMNP::GetBytes(clientBuffer);
}


// ROM 0x0005e8fc GetFramedBytes__14TClassOneModemFP11CBufferList
void
TClassOneModem::GetFramedBytes(CBufferList* clientBuffer)
{
	if (fDataMode != 2)
		TSerTool::GetFramedBytes(clientBuffer);
	else
		TMNP::GetFramedBytes(clientBuffer);
}


// ROM 0x0005eda4 GetBytesImmediate__14TClassOneModemFP11CBufferListl
void
TClassOneModem::GetBytesImmediate(CBufferList* clientBuffer, Size threshold)
{
	if (fDataMode != 2)
		TSerTool::GetBytesImmediate(clientBuffer, threshold);
	else
		TMNP::GetBytesImmediate(clientBuffer, threshold);
}


// ROM 0x0005f134 GetComplete__14TClassOneModemFlUcUl
// A byte of a command's answer, a packet's frame (whether it ended kept in
// the reply's first byte), or the client's.
void
TClassOneModem::GetComplete(NewtonErr result, Boolean endOfFrame, ULong getBytesCount)
{
	if (fModemFlags & kModemFlagGetting)
	{
		fGetBuffer->Hide(fGetSize, kSeekFromEnd);
		fGetBuffer = nil;
		GetCommandResultComplete(result);
		return;
	}
	if (fModemFlags & kModemFlagPacketGet)
	{
		fGetBuffer->Hide(fGetSize, kSeekFromEnd);
		fGetBuffer = nil;
		fReply.fText[0] = endOfFrame;
		C1PktContinue(result);
		return;
	}
	TMNP::GetComplete(result, endOfFrame, getBytesCount);
}


// ROM 0x0005f1dc KillGet__14TClassOneModemFv
void
TClassOneModem::KillGet()
{
	if (fDataMode != 2)
		TFramedAsyncSerTool::KillGet();
	else
		TMNP::KillGet();
}


// ROM 0x0006332c KillGetComplete__14TClassOneModemFl
void
TClassOneModem::KillGetComplete(NewtonErr result)
{
	if (!(fModemFlags & kModemFlagAborting))
		TMNP::KillGetComplete(result);
}


/*------------------------------------------------------------------------------
	TClassOneModem - the serial port and the modem's set-up
------------------------------------------------------------------------------*/

// ROM 0x0005d4d0 ResetSerialDrvr__14TClassOneModemFUllN22
// The port set to a speed and framing, with flow control off.
void
TClassOneModem::ResetSerialDrvr(ULong speed, long stopBits, long parity, long dataBits)
{
	TCMOSerialIOParms parms;
	parms.fSpeed = speed;
	parms.fParity = parity;
	parms.fDataBits = dataBits;
	parms.fStopBits = stopBits;
	SetIOParms(&parms);
	fSerialSpeed = speed;
	fInFlowParms.useSoftFlowControl = false;
	fInFlowParms.useHardFlowControl = false;
	fOutFlowParms.useSoftFlowControl = false;
	fOutFlowParms.useHardFlowControl = false;
	SetInputFlowControl(&fInFlowParms);
	SetOutputFlowControl(&fOutFlowParms);
}


// ROM 0x00064eb8 AdjustForReset__14TClassOneModemFv
// The modem reset to its data class ("&FE0V1" answered OK): flow control
// off.
void
TClassOneModem::AdjustForReset()
{
	if (fCommandError != noErr || fReply.fResultCode != kModemResultOK || (fModemFlags & kModemFlagDataClass))
		return;
	fModemFlags |= kModemFlagDataClass;
	fOutFlowParms.useSoftFlowControl = false;
	fOutFlowParms.useHardFlowControl = false;
	fInFlowParms.useSoftFlowControl = false;
	fInFlowParms.useHardFlowControl = false;
	SetOutputFlowControl(&fOutFlowParms);
	SetInputFlowControl(&fInFlowParms);
}


// ROM 0x00064f24 AdjustForConnectSpeed__14TClassOneModemFv
// Connected: CTS/RTS flow control if the modem paces the line (and it is
// wanted), else the port set to the speed it connected at; the carrier
// watched; the 'mspd option told the speed.  (Answering a fax, XON/XOFF
// or CTS/RTS on input.)
void
TClassOneModem::AdjustForConnectSpeed()
{
	if (fModemFlags & kModemFlagDataClass)
	{
		if (fModemFlags & kModemFlagHardwareFlow)
		{
			fOutFlowParms.useHardFlowControl = true;
			SetOutputFlowControl(&fOutFlowParms);
			if ((fECType & 0x0e) && fProfile->fMNP10)
			{
				fInFlowParms.useHardFlowControl = true;
				SetInputFlowControl(&fInFlowParms);
			}
		}
		else if (fSerialSpeed != fConnectSpeed)
			ResetSerialDrvr(fConnectSpeed, 0, 0, 8);
		SetCDOption();
		if (fOptionsInfo.fOptions != nil)
		{
			TOptionIterator iter(fOptionsInfo.fOptions);
			TOption* option = iter.FindOption(kCMOModemConnectSpeed);
			if (option != nil)
				((TCMOModemConnectSpeed*) option)->fSpeed = fConnectSpeed;
		}
		return;
	}
	if (fToolState & kToolStateListenMode)
		return;
	if (!(fModemFlags & kModemFlagHardwareFlow))
		fInFlowParms.useSoftFlowControl = true;
	else
		fInFlowParms.useHardFlowControl = true;
	SetInputFlowControl(&fInFlowParms);
}


// ROM 0x0005eabc SetCDOption__14TClassOneModemFv
// The carrier dropping told as a serial event after it has been gone 3
// seconds (15 above 2400 bps) - unless the navigator has the line.
void
TClassOneModem::SetCDOption()
{
	TCMOSerialEventEnables events;
	if (fPrefs.fNavigatorOpen)
		events.serEventEnables = 0;
	else
	{
		events.serEventEnables = kSerialEventDCDNegatedMask;
		ULong seconds = (fConnectSpeed <= 2400) ? fPrefs.fCarrierDownSlow : fPrefs.fCarrierDownFast;
		events.carrierDetectDownTime = seconds * kSeconds;
	}
	TFramedAsyncSerTool::ProcessOptionStart(&events, events.Label(), opSetRequired);
}


// ROM 0x0005f0c0 SetSpeakerVolume__14TClassOneModemFUc
// A PC card modem's sound routed to the speaker at the dialing option's
// volume (1 to 3), or not.
void
TClassOneModem::SetSpeakerVolume(UByte speakerOn)
{
	if (fChip == nil)
		return;
	TCMOPCMCIAModemSound sound;
	UByte volume = 0;
	if (speakerOn)
	{
		if (fDialing.fSpeakerVolume == '1')
			volume = 1;
		else if (fDialing.fSpeakerVolume == '2')
			volume = 2;
		else if (fDialing.fSpeakerVolume == '3')
			volume = 3;
	}
	sound.fEnableModemSound = volume;
	fChip->ProcessOption(&sound);
}


// ROM 0x0005d5d8 UpdateDialOptionsStr__14TClassOneModemFv
// The dialing options put into the dialing preferences' string
// ("ATM1L2X4S7=060S8=001S6=003S0=000"): the speaker, its volume, the X code
// (dial tone and busy detection), the wait for the carrier (S7 - nine
// seconds less on the 224's H-code version unless dialing blind), the
// comma's pause (S8) and the wait before dialing blind (S6).
void
TClassOneModem::UpdateDialOptionsStr()
{
	fDialPrefs[3] = fDialing.fSpeakerOn ? '1' : '0';
	if (fDialing.fDetectDialTone)
		fDialPrefs[7] = fDialing.fDetectBusy ? '4' : '2';
	else
		fDialPrefs[7] = fDialing.fDetectBusy ? '3' : '1';
	fDialPrefs[5] = fDialing.fSpeakerVolume;
	UByte carrierWait = fDialing.fWaitForCarrier;
	if (fHCodeModem
	&&  !(fTAPIService.fActive && fTAPIService.fActive2)
	&&  !fConnectType.fFax
	&&  fDialPrefs[7] != '1')
		carrierWait -= 9;
	IToARegisterValue(carrierWait, fDialPrefs + 0x0b);
	IToARegisterValue(fDialing.fWaitBeforeBlindDial, fDialPrefs + 0x17);
	IToARegisterValue(fDialing.fCommaDelay, fDialPrefs + 0x11);
}


// ROM 0x0005d6c8 IToARegisterValue__14TClassOneModemFUcPUc
// Three decimal digits.
void
TClassOneModem::IToARegisterValue(UByte value, UChar* digits)
{
	digits[0] = value / 100 + '0';
	UByte rest = value % 100;
	digits[1] = rest / 10 + '0';
	digits[2] = rest % 10 + '0';
}


// ROM 0x0005e980 SetActiveConfigStrs__14TClassOneModemFP16TCMOModemProfile
void
TClassOneModem::SetActiveConfigStrs(TCMOModemProfile* profile)
{
	fIdString = profile->GetModemString(0);
	fNoECStr = profile->GetModemString(1);
	fECOnlyStr = profile->GetModemString(2);
	fECFallBackStr = profile->GetModemString(3);
	fCellularStr = profile->GetModemString(4);
	fDirectStr = profile->GetModemString(5);
}


// ROM 0x0005e9f8 SetModemProfile__14TClassOneModemFv
// A new profile (the defaults) made of the six strings in hand, which are
// then taken from it.
NewtonErr
TClassOneModem::SetModemProfile()
{
	ULong size = strlen((const char*) fIdString) + strlen((const char*) fNoECStr) + strlen((const char*) fECOnlyStr)
			   + strlen((const char*) fECFallBackStr) + strlen((const char*) fCellularStr) + strlen((const char*) fDirectStr) + 6;
	// DEVIATION (pointer size): the host's size of the fields (the ROM's 0x28)
	TCMOModemProfile* profile = (TCMOModemProfile*) NewPtr(ModemProfileSize(size));
	if (profile == nil)
		return kError_No_Memory;
	if (fProfile != nil)
		DisposPtr((Ptr) fProfile);
	fProfile = new (profile) TCMOModemProfile(size);
	fProfile->SetModemStrings(fIdString, fNoECStr, fECOnlyStr, fECFallBackStr, fCellularStr, fDirectStr);
	SetActiveConfigStrs(fProfile);
	return noErr;
}


// ROM 0x0005eb2c InitPhoneNumberInfo__14TClassOneModemFv
// The number to dial: the phone number address among the options (marked
// done).  None is an error unless dialing by hand.
NewtonErr
TClassOneModem::InitPhoneNumberInfo()
{
	NewtonErr err = kCommErrNoPhoneNumber;
	fPhoneNumberLength = 0;
	fPhoneNumber = nil;
	fDialled = 0;
	if (fDialing.fManualDial && !(fTAPIService.fActive && fTAPIService.fActive2))
		return noErr;
	if (fOptionsInfo.fOptions == nil)
		return err;
	TOptionIterator iter(fOptionsInfo.fOptions);
	for (TOption* option = iter.FirstOption(); iter.More(); option = iter.NextOption())
	{
		if (option->Label() == kCMARouteLabel)
		{
			TCMAPhoneNumber* number = (TCMAPhoneNumber*) option;
			if (number->fType == kPhoneNumber)
			{
				fPhoneNumber = (UChar*) (number + 1);
				fPhoneNumberLength = number->fPhoneLen;
				if (fPhoneNumberLength != 0)
					err = noErr;
			}
			option->SetOpCodeResult(opSuccess);
			option->SetProcessed();
		}
	}
	return err;
}


// ROM 0x0005e90c BlockGetAndPutChannel__14TClassOneModemFv
// The client's gets and puts held off while a packet or a Class 2 page is
// under way (busy if one is in hand).
NewtonErr
TClassOneModem::BlockGetAndPutChannel()
{
	if (fRequests[kCommToolPutChannel].fRequestPending || fRequests[kCommToolGetChannel].fRequestPending)
		return kCommErrToolBusy;
	SetChannelFilter((CommToolRequestType) (kCommToolRequestTypeGet | kCommToolRequestTypePut), false);
	return noErr;
}


// ROM 0x0005e958 UnblockGetAndPutChannel__14TClassOneModemFv
NewtonErr
TClassOneModem::UnblockGetAndPutChannel()
{
	SetChannelFilter((CommToolRequestType) (kCommToolRequestTypeGet | kCommToolRequestTypePut), true);
	return noErr;
}


// ROM 0x0005db50 GetNextTermProc__14TClassOneModemFUlRUlRPFPv_Uc
// Terminating: the MNP connect cancelled (phase 7) and the modem hung up
// (phase 8) before MNP's own.
void
TClassOneModem::GetNextTermProc(ULong terminationPhase, ULong& terminationFlag, TerminateProcPtr& terminationProc)
{
	if (terminationPhase == 7)
	{
		terminationFlag = 0x4000;
		terminationProc = CancelMNPConnect;
	}
	else if (terminationPhase == 8)
	{
		terminationFlag = 0x1000;
		terminationProc = HangUp;
	}
	else
		TMNP::GetNextTermProc(terminationPhase, terminationFlag, terminationProc);
}


// ROM 0x0005db88 TerminateComplete__14TClassOneModemFv
// (TCommTool's, past the serial tool's.)
void
TClassOneModem::TerminateComplete()
{
	fModemFlags &= ~(kModemFlagFaxAnswer | kModemFlagFaxOriginate | kModemFlagConnecting);
	fDataMode = 0;
	fResArbReleasing = false;
	SetInputSendForIntDelay(11058);
	if (fZeroStuffFlags & kZeroStuffOn)
		ZeroStuffingDeinit();
	TCommTool::TerminateComplete();
}


/*------------------------------------------------------------------------------
	TClassOneModem - the options
------------------------------------------------------------------------------*/

// ROM 0x00060d38 ProcessOptionStart__14TClassOneModemFP7TOptionUlT2
// The modem's options: set, their defaults, or what is in force.  The fax
// class and capabilities are asked of the modem (the answer 1, in
// progress, until ProcessOptionComplete).  Anything else is MNP's.
ULong
TClassOneModem::ProcessOptionStart(TOption* theOption, ULong label, ULong opcode)
{
	Boolean set = (opcode == opSetNegotiate || opcode == opSetRequired);
	Boolean getDefault = (opcode == opGetDefault);
	TOptionExtended* extended = (TOptionExtended*) theOption;
	switch (label)
	{
	case kCMOModemFaxClassesSupported:
		if (set)
			return opFailure;
		if (getDefault)
		{
			TCMOModemFaxClassesSupported classes;
			theOption->CopyDataFrom(&classes);
			return opSuccess;
		}
		if (!(fToolState & kToolStateBound))
			goto notBound;
		if (fToolState & (kToolStateConnected | kToolStateListenMode))
			goto busy;
		fOptionState = 0;
		GetSrvcClsSupported(noErr);
		return opInProgress;

	case kCMOModemDialing:
		if (set)
		{
			fDialing.CopyDataFrom(theOption);
			UpdateDialOptionsStr();
		}
		else if (getDefault)
		{
			TCMOModemDialing dialing;
			theOption->CopyDataFrom(&dialing);
		}
		else
			theOption->CopyDataFrom(&fDialing);
		return opSuccess;

	case 'disc':
		if (set)
			return ProcessTAPICommand(kModemCmdTAPIOnHook);
		return opFailure;

	case 'answ':
		// BUG: the answer is the command number (kModemCmdTAPIOffHook) that
		// should have been given to ProcessTAPICommand, as a status
		if (set)
			return kModemCmdTAPIOffHook;
		return opFailure;

	case kCMOListenTimer:
		if (set)
			fListenTimer = ((TCMOListenTimer*) theOption)->fValue;
		else if (getDefault)
		{
			TCMOListenTimer timer;
			theOption->CopyDataFrom(&timer);
		}
		else
			((TCMOListenTimer*) theOption)->fValue = fListenTimer;
		return opSuccess;

	case kCMOHandsetManagement:
		if (set)
			return opFailure;
		if (getDefault)
		{
			TCMOHandsetManagement handset;
			theOption->CopyDataFrom(&handset);
		}
		else
			((TCMOHandsetManagement*) theOption)->fManaged = !fVoiceSupport.fSupportsVoice;
		return opSuccess;

	case kCMOModemConnectType:
		if (set)
		{
			fConnectType.CopyDataFrom(theOption);
			if (theOption->Length() < fConnectType.Length())
				fConnectType.fAlreadyConnected = false;
			fTAPIService.fActive = fConnectType.fVoice;
			fTAPIService.fActive2 = fConnectType.fVoice;
		}
		else if (getDefault)
		{
			TCMOModemConnectType connectType;
			theOption->CopyDataFrom(&connectType);
		}
		else
			theOption->CopyDataFrom(&fConnectType);
		return opSuccess;

	case kCMOModemFaxCapabilities:
		if (set)
		{
			// (into the enabled capabilities)
			fFaxEnabledCaps.CopyDataFrom(theOption);
			return opSuccess;
		}
		if (getDefault)
		{
			TCMOModemFaxCapabilities capabilities;
			theOption->CopyDataFrom(&capabilities);
			return opSuccess;
		}
		if (!(fToolState & kToolStateBound))
			goto notBound;
		if (fToolState & (kToolStateConnected | kToolStateListenMode))
			goto busy;
		C1GetCapStart();
		return opInProgress;

	case kCMOModemECType:
		if (set)
			fECType = ((TCMOModemECType*) theOption)->fType;
		else if (getDefault)
			((TCMOModemECType*) theOption)->fType = 7;
		else
			((TCMOModemECType*) theOption)->fType = (fToolState & kToolStateConnected) ? fDataMode : fECType;
		return opSuccess;

	case kCMOModemFaxClass1Cap:
		if (set)
			return opFailure;
		if (getDefault)
		{
			TCMOModemFaxClass1Cap capabilities;
			theOption->CopyDataFrom(&capabilities);
			return opSuccess;
		}
		if (!(fToolState & kToolStateBound))
			goto notBound;
		if (fToolState & (kToolStateConnected | kToolStateListenMode))
			goto busy;
		fOptionState = 5;
		C1GetFaxCapabilities(noErr);
		return opInProgress;

	case kCMOModemFaxEnabledCaps:
		if (set)
			fFaxEnabledCaps.CopyDataFrom(theOption);
		else if (getDefault)
		{
			TCMOModemFaxEnabledCaps capabilities;
			theOption->CopyDataFrom(&capabilities);
		}
		else
			theOption->CopyDataFrom(&fFaxEnabledCaps);
		return opSuccess;

	case kCMOModemFaxClass:
		if (set)
		{
			if (!(fToolState & kToolStateBound))
				goto notBound;
			if (fToolState & (kToolStateConnected | kToolStateListenMode))
				goto busy;
			fFaxClass.CopyDataFrom(theOption);
			fOptionState = 3;
			SetServiceClass(noErr);
			return opInProgress;
		}
		if (getDefault)
		{
			TCMOModemFaxClass faxClass;
			theOption->CopyDataFrom(&faxClass);
		}
		else
			theOption->CopyDataFrom(&fFaxClass);
		return opSuccess;

	case kCMOSerialBytesAvailable:
	case kCMOSerialDiscard:
		if (fDataMode != 2)
			TFramedAsyncSerTool::ProcessOptionStart(theOption, label, opcode);
		else
			TMNP::ProcessOptionStart(theOption, label, opcode);
		return opSuccess;

	case kCMOModemConnectSpeed:
		if (set)
			return opFailure;
		if (getDefault)
		{
			TCMOModemConnectSpeed speed;
			theOption->CopyDataFrom(&speed);
		}
		else
			((TCMOModemConnectSpeed*) theOption)->fSpeed = fConnectSpeed;
		return opSuccess;

	case kCMOModemPrefs:
		if (set)
		{
			fPrefs.CopyDataFrom(theOption);
			SetCDOption();
			// (1.x's preferences, without fStripDashes, strip the dashes)
			TCMOModemPrefs_Ver_1 oldPrefs;
			if (theOption->Length() == oldPrefs.Length())
				fPrefs.fStripDashes = true;
		}
		else if (getDefault)
		{
			TCMOModemPrefs prefs;
			theOption->CopyDataFrom(&prefs);
		}
		else
			theOption->CopyDataFrom(&fPrefs);
		return opSuccess;

	case kCMOModemProfile:
		if (set)
		{
			SetActiveConfigStrs((TCMOModemProfile*) theOption);
			if (SetModemProfile() != noErr)
				return opFailure;
			fProfile->CopyDataFrom(theOption);
		}
		else if (getDefault)
		{
			// BUG: the default is made on the stack with no room for its
			// strings, so the six bytes of them copied are whatever lies
			// past it
			UByte space[ModemProfileSize(6)];
			TCMOModemProfile* profile = new (space) TCMOModemProfile(6);
			theOption->CopyDataFrom(profile);
		}
		else
			theOption->CopyDataFrom(fProfile);
		return opSuccess;

	case kCMOModemVoiceSupport:
		if (set)
			return opFailure;
		if (getDefault)
		{
			TCMOModemVoiceSupport voice;
			theOption->CopyDataFrom(&voice);
		}
		else
			theOption->CopyDataFrom(&fVoiceSupport);
		return opSuccess;

	case 'outg':
		if (!set)
			return opFailure;
		InitPhoneNumberInfo();
		if (fPhoneNumber != nil)
			fConnectState = 7;
		return ProcessTAPICommand(kModemCmdDial);

	case 'sdgt':
		if (!set)
			return opFailure;
		InitPhoneNumberInfo();
		return ProcessTAPICommand(kModemCmdDial);

	case kCMOTAPIService:
		if (set)
		{
			fTAPIService.CopyDataFrom(theOption);
			fConnectType.fVoice = fTAPIService.fActive && fTAPIService.fActive2;
		}
		else if (getDefault)
		{
			// BUG: the default given is a connect type's, not a TAPI service's
			TCMOModemConnectType connectType;
			theOption->CopyDataFrom(&connectType);
		}
		else
			theOption->CopyDataFrom(&fTAPIService);
		return opSuccess;

	case kCMOTAPISpeaker:
		if (!set)
			return opFailure;
		fTAPISpeaker.CopyDataFrom(theOption);
		return ProcessTAPICommand(kModemCmdSpeaker);
	}
	return TMNP::ProcessOptionStart(theOption, label, opcode);

notBound:
	extended->SetExtendedResult(kCommErrNotBound);
	return opFailure;
busy:
	extended->SetExtendedResult(kCommErrToolBusy);
	return opFailure;
}
