/*
	File:		comms/ModemToolFax.cpp

	Contains:	TClassOneModem's fax side (ModemTool.h): the fax class and
				capabilities asked of the modem for an option, the Class 1
				packets (an HDLC frame or a page's data sent or received,
				with fill bytes while the page's data runs short), and the
				Class 2 commands and page data.

	Reconstructed from the MP2x00 US ROM (0x0005dbfc-0x0005e980,
	0x0005ee1c-0x0005f0c0, 0x0005f3dc-0x0005f7d4, 0x000627c8-0x00063294,
	0x00065038-0x000651cc); each function cites its origin.
*/

#include "ModemTool.h"
#include "CommErrors.h"
#include "NewtErrors.h"
#include "OSErrors.h"
#include "NewtonMemory.h"

#include <stdlib.h>
#include <string.h>


/*------------------------------------------------------------------------------
	The fax class and capabilities
	'mfax (the classes, then each Class 1 modulation list), 'mf1c (the
	modulations alone), 'mfsq (the classes) and 'mfsc (the class set) are
	asked of the modem a command at a time - fOptionLabel says which, and
	its answer is ANDed with what the 'mfec option allows.
------------------------------------------------------------------------------*/

// ROM 0x000627c8 C1GetCapStart__14TClassOneModemFv
void
TClassOneModem::C1GetCapStart()
{
	TCMOModemFaxCapabilities* capabilities = (TCMOModemFaxCapabilities*) fOptionsInfo.fCurOptPtr;
	capabilities->fServiceClasses = 0;
	capabilities->fTransmitDataMods = 0;
	capabilities->fTransmitHDLCMods = 1;
	capabilities->fReceiveDataMods = 0;
	capabilities->fReceiveHDLCMods = 1;
	fModemFlags |= kModemFlagCapabilities;
	fCapState = 1;
	fOptionLabel = kCMOModemFaxCapabilities;
	C1GetCapContinue(noErr);
}


// ROM 0x0006281c C1GetCapContinue__14TClassOneModemFl
// "+FCLASS=?", then (in Class 1) "+FTM=?", "+FTH=?", "+FRM=?" and "+FRH=?",
// and the class put back.  An answer's lines are read until OK (no answer
// at all counts as OK).
void
TClassOneModem::C1GetCapContinue(NewtonErr result)
{
	if (fCapKilled)
		return;
	if (result == kModemErrNoResponse)
		fReply.fResultCode = kModemResultError;
	else if (result != noErr)
		goto complete;
	{
		TCMOModemFaxCapabilities* capabilities = (TCMOModemFaxCapabilities*) fOptionsInfo.fCurOptPtr;
		Boolean done = (result == kModemErrNoResponse || fReply.fResultCode == kModemResultOK);
		switch (fCapState)
		{
		case 1:
			fCapState = 3;
			if ((result = BeginModemCommand(kModemCmdQueryFaxClass)) == noErr)
				return;
			goto complete;
		case 2:
			if (fReply.fResultCode != kModemResultOK)
				goto complete;
			if (fProfile->fSerialSpeed != 19200)
				ResetSerialDrvr(19200, 0, 0, 8);
			fCapState = 5;
			if ((result = BeginModemCommand(kModemCmdQueryC1TransmitData)) == noErr)
				return;
			goto complete;
		case 3:
			if (fReply.fResultCode != kModemResultUnknown)
				goto complete;
			C1GetCapExtractResult(&capabilities->fServiceClasses);
			fCapState = 4;
			break;
		case 4:
			if (!done)
				break;
			memcpy(&fSavedFaxClass, &fFaxClass, sizeof(TCMOModemFaxClass));
			fFaxClass.fClass = kModemFaxClass1;
			fCapState = 2;
			if ((result = BeginModemCommand(kModemCmdSetFaxClass)) == noErr)
				return;
			goto complete;
		case 5:
			if (fReply.fResultCode != kModemResultUnknown)
				goto complete;
			C1GetCapExtractResult(&capabilities->fTransmitDataMods);
			fCapState = 6;
			break;
		case 6:
			if (!done)
				break;
			fCapState = 7;
			if ((result = BeginModemCommand(kModemCmdQueryC1TransmitHDLC)) == noErr)
				return;
			goto complete;
		case 7:
			if (fReply.fResultCode != kModemResultUnknown)
				goto complete;
			C1GetCapExtractResult(&capabilities->fTransmitHDLCMods);
			fCapState = 8;
			break;
		case 8:
			if (!done)
				break;
			fCapState = 9;
			if ((result = BeginModemCommand(kModemCmdQueryC1ReceiveData)) == noErr)
				return;
			goto complete;
		case 9:
			if (fReply.fResultCode != kModemResultUnknown)
				goto complete;
			C1GetCapExtractResult(&capabilities->fReceiveDataMods);
			fCapState = 10;
			break;
		case 10:
			if (!done)
				break;
			fCapState = 11;
			if ((result = BeginModemCommand(kModemCmdQueryC1ReceiveHDLC)) == noErr)
				return;
			goto complete;
		case 11:
			if (fReply.fResultCode != kModemResultUnknown)
				goto complete;
			C1GetCapExtractResult(&capabilities->fReceiveHDLCMods);
			fCapState = 12;
			break;
		case 12:
			if (!done)
				break;
			memcpy(&fFaxClass, &fSavedFaxClass, sizeof(TCMOModemFaxClass));
			fCapState = 13;
			if ((result = BeginModemCommand(kModemCmdSetFaxClass)) == noErr)
				return;
			goto complete;
		case 13:
			result = done ? noErr : kModemErrCommandFailure;
			if (fSavedFaxClass.fClass == kModemFaxClass0 && fSerialSpeed != fProfile->fSerialSpeed)
				ResetSerialDrvr(fProfile->fSerialSpeed, 0, 0, 8);
			goto complete;
		default:
			return;
		}
		GetCommandResult();
		return;
	}
complete:
	C1GetCapComplete(result);
}


// ROM 0x00062b64 C1GetCapExtractResult__14TClassOneModemFPUl
// The numbers of an answer ("0,1,2" or "24,48,72,96"), each made a bit:
// the classes (0 data, 1, 2, 2.0 as 20) when asking them, else the
// modulations.
void
TClassOneModem::C1GetCapExtractResult(ULong* capabilities)
{
	char number[12];
	UChar* s = fResultBuffer;
	Long length = 0;
	Boolean last = false;
	do
	{
		UChar c = *s;
		if (c == 0 || c == ',')
		{
			if (c == 0)
				last = true;
			number[length] = 0;
			Long value = atoi(number);
			if (fCapState == 3)
			{
				switch (value)
				{
				case 0:		*capabilities |= kModemFaxClass0; break;
				case 1:		*capabilities |= kModemFaxClass1; break;
				case 2:		*capabilities |= kModemFaxClass2; break;
				case 20:	*capabilities |= kModemFaxClass20; break;
				}
			}
			else
			{
				switch (value)
				{
				case 3:		*capabilities |= 0x0001; break;
				case 24:	*capabilities |= 0x0002; break;
				case 48:	*capabilities |= 0x0004; break;
				case 72:	*capabilities |= 0x0008; break;
				case 73:	*capabilities |= 0x0010; break;
				case 74:	*capabilities |= 0x0020; break;
				case 96:	*capabilities |= 0x0040; break;
				case 97:	*capabilities |= 0x0080; break;
				case 98:	*capabilities |= 0x0100; break;
				case 121:	*capabilities |= 0x0200; break;
				case 122:	*capabilities |= 0x0400; break;
				case 145:	*capabilities |= 0x0800; break;
				case 146:	*capabilities |= 0x1000; break;
				}
			}
			length = 0;
		}
		else if (length < 11 && c >= '0' && c <= '9')
			number[length++] = c;
		s++;
	} while (!last);
}


// ROM 0x00062d20 C1GetCapComplete__14TClassOneModemFl
void
TClassOneModem::C1GetCapComplete(NewtonErr result)
{
	fModemFlags &= ~kModemFlagCapabilities;
	TCMOModemFaxCapabilities* capabilities = (TCMOModemFaxCapabilities*) fOptionsInfo.fCurOptPtr;
	capabilities->fServiceClasses &= fFaxEnabledCaps.fServiceClasses;
	capabilities->fTransmitDataMods &= fFaxEnabledCaps.fTransmitDataMods;
	capabilities->fTransmitHDLCMods &= fFaxEnabledCaps.fTransmitHDLCMods;
	capabilities->fReceiveDataMods &= fFaxEnabledCaps.fReceiveDataMods;
	capabilities->fReceiveHDLCMods &= fFaxEnabledCaps.fReceiveHDLCMods;
	capabilities->SetExtendedResult(result);
	ProcessOptionComplete(result != noErr ? opFailure : opSuccess);
}


// ROM 0x00062db4 GetSrvcClsSupported__14TClassOneModemFl
// 'mfsq: "+FCLASS=?".
void
TClassOneModem::GetSrvcClsSupported(NewtonErr result)
{
	TCMOModemFaxClassesSupported* classes = (TCMOModemFaxClassesSupported*) fOptionsInfo.fCurOptPtr;
	switch (fOptionState)
	{
	case 0:
		fModemFlags |= kModemFlagCapabilities;
		fCapState = 3;
		fOptionLabel = kCMOModemFaxClassesSupported;
		classes->fClasses = 0;
		if ((result = BeginModemCommand(kModemCmdQueryFaxClass)) == noErr)
		{
			fOptionState = 1;
			return;
		}
		break;
	case 1:
		if (fReply.fResultCode != kModemResultUnknown)
			break;
		C1GetCapExtractResult(&classes->fClasses);
		fOptionState = 2;
		fCapState &= ~3;
		GetCommandResult();
		return;
	case 2:
		if (result != kModemErrNoResponse && fReply.fResultCode != kModemResultOK)
		{
			GetCommandResult();
			return;
		}
		classes->fClasses &= fFaxEnabledCaps.fServiceClasses;
		result = noErr;
		break;
	default:
		result = kModemErrNotSupported;
		break;
	}
	fModemFlags &= ~kModemFlagCapabilities;
	classes->SetExtendedResult(result);
	ProcessOptionComplete(result != noErr ? opFailure : opSuccess);
}


// ROM 0x00062ee4 SetServiceClass__14TClassOneModemFl
// 'mfsc: "+FCLASS=" the class; the port's speed then the one it wants.
// (A refusal completes the option with the error it was given, which may be
// none.)
void
TClassOneModem::SetServiceClass(NewtonErr result)
{
	switch (fOptionState)
	{
	case 3:
		fModemFlags |= kModemFlagCapabilities;
		fOptionLabel = kCMOModemFaxClass;
		if ((result = BeginModemCommand(kModemCmdSetFaxClass)) == noErr)
		{
			fOptionState = 4;
			return;
		}
		break;
	case 4:
		if (fReply.fResultCode != kModemResultOK)
			break;
		result = noErr;
		fNextCommandDelay = 600;
		if (fFaxClass.fClass == kModemFaxClass0)
		{
			if (fSerialSpeed != fProfile->fSerialSpeed)
				ResetSerialDrvr(fProfile->fSerialSpeed, 0, 0, 8);
		}
		else if (fProfile->fSerialSpeed != 19200)
			ResetSerialDrvr(19200, 0, 0, 8);
		break;
	default:
		result = kModemErrNotSupported;
		break;
	}
	fModemFlags &= ~kModemFlagCapabilities;
	((TOptionExtended*) fOptionsInfo.fCurOptPtr)->SetExtendedResult(result);
	ProcessOptionComplete(result != noErr ? opFailure : opSuccess);
}


// ROM 0x00063000 C1GetFaxCapabilities__14TClassOneModemFl
// 'mf1c: the Class 1 modulations (the modem must be in Class 1).
void
TClassOneModem::C1GetFaxCapabilities(NewtonErr result)
{
	TCMOModemFaxClass1Cap* capabilities = (TCMOModemFaxClass1Cap*) fOptionsInfo.fCurOptPtr;
	Boolean done = (result == kModemErrNoResponse || fReply.fResultCode == kModemResultOK);
	switch (fOptionState)
	{
	case 5:
		fModemFlags |= kModemFlagCapabilities;
		fOptionLabel = kCMOModemFaxClass1Cap;
		if (fFaxClass.fClass != kModemFaxClass1)
		{
			result = kModemErrCommandFailure;
			break;
		}
		capabilities->fTransmitDataMods = 0;
		capabilities->fTransmitHDLCMods = 1;
		capabilities->fReceiveDataMods = 0;
		capabilities->fReceiveHDLCMods = 1;
		if ((result = BeginModemCommand(kModemCmdQueryC1TransmitData)) == noErr)
		{
			fOptionState = 6;
			return;
		}
		break;
	case 6:
		if (fReply.fResultCode != kModemResultUnknown)
			break;
		C1GetCapExtractResult(&capabilities->fTransmitDataMods);
		fOptionState = 7;
		GetCommandResult();
		return;
	case 7:
		if (!done)
		{
			GetCommandResult();
			return;
		}
		fOptionState = 8;
		if ((result = BeginModemCommand(kModemCmdQueryC1TransmitHDLC)) == noErr)
			return;
		break;
	case 8:
		if (fReply.fResultCode != kModemResultUnknown)
			break;
		C1GetCapExtractResult(&capabilities->fTransmitHDLCMods);
		fOptionState = 9;
		GetCommandResult();
		return;
	case 9:
		if (!done)
		{
			GetCommandResult();
			return;
		}
		fOptionState = 10;
		if ((result = BeginModemCommand(kModemCmdQueryC1ReceiveData)) == noErr)
			return;
		break;
	case 10:
		if (fReply.fResultCode != kModemResultUnknown)
			break;
		C1GetCapExtractResult(&capabilities->fReceiveDataMods);
		fOptionState = 11;
		GetCommandResult();
		return;
	case 11:
		if (!done)
		{
			GetCommandResult();
			return;
		}
		fOptionState = 12;
		if ((result = BeginModemCommand(kModemCmdQueryC1ReceiveHDLC)) == noErr)
			return;
		break;
	case 12:
		if (fReply.fResultCode != kModemResultUnknown)
			break;
		C1GetCapExtractResult(&capabilities->fReceiveHDLCMods);
		fOptionState = 13;
		GetCommandResult();
		return;
	case 13:
		if (!done)
		{
			GetCommandResult();
			return;
		}
		capabilities->fTransmitDataMods &= fFaxEnabledCaps.fTransmitDataMods;
		capabilities->fTransmitHDLCMods &= fFaxEnabledCaps.fTransmitHDLCMods;
		capabilities->fReceiveDataMods &= fFaxEnabledCaps.fReceiveDataMods;
		capabilities->fReceiveHDLCMods &= fFaxEnabledCaps.fReceiveHDLCMods;
		result = noErr;
		break;
	default:
		result = kModemErrNotSupported;
		break;
	}
	fModemFlags &= ~kModemFlagCapabilities;
	capabilities->SetExtendedResult(result);
	ProcessOptionComplete(result != noErr ? opFailure : opSuccess);
}


// ROM 0x00065038 GetModParamStr__14TClassOneModemFUl
// A modulation bit's T.31 number.
const UChar*
TClassOneModem::GetModParamStr(ULong modulation)
{
	switch (modulation)
	{
	case 0x0001:	return (const UChar*) paramV21Ch2Mod;
	case 0x0002:	return (const UChar*) paramV27Ter24Mod;
	case 0x0004:	return (const UChar*) paramV27Ter48Mod;
	case 0x0008:	return (const UChar*) paramV29_72Mod;
	case 0x0010:	return (const UChar*) paramV17_72Mod;
	case 0x0020:	return (const UChar*) paramV17st_72Mod;
	case 0x0040:	return (const UChar*) paramV29_96Mod;
	case 0x0080:	return (const UChar*) paramV17_96Mod;
	case 0x0100:	return (const UChar*) paramV17st_96Mod;
	case 0x0200:	return (const UChar*) paramV17_12Mod;
	case 0x0400:	return (const UChar*) paramV17st_12Mod;
	case 0x0800:	return (const UChar*) paramV17_14Mod;
	case 0x1000:	return (const UChar*) paramV17st_14Mod;
	}
	return nil;
}


// ROM 0x00065128 GetModBaudRate__14TClassOneModemFUl
// A modulation bit's bits per second.
ULong
TClassOneModem::GetModBaudRate(ULong modulation)
{
	switch (modulation)
	{
	case 0x0001:	return 300;
	case 0x0002:	return 2400;
	case 0x0004:	return 4800;
	case 0x0008: case 0x0010: case 0x0020:
					return 7200;
	case 0x0040: case 0x0080: case 0x0100:
					return 9600;
	case 0x0200: case 0x0400:
					return 12000;
	case 0x0800: case 0x1000:
					return 14400;
	}
	return 0;
}


/*------------------------------------------------------------------------------
	Class 1 packets
	The fax transport's control requests: an HDLC frame (V.21, "+FTH=3")
	or a page's data ("+FTM=" its modulation) sent - after a silence if the
	request asks for one - or received ("+FRH=3", "+FRM="), each framed by
	DLE ETX once the modem has said CONNECT; after the last frame or the
	page's end the modem's OK (or CONNECT: it is still sending) read.  The
	client's own gets and puts are held off meanwhile.  fPacketState is the
	step; fReply.fText[0] whether the frame received ended.
------------------------------------------------------------------------------*/

// ROM 0x0005dd2c DoTransPkt__14TClassOneModemFv
void
TClassOneModem::DoTransPkt()
{
	if (ShouldAbort(0, noErr))
		return;
	NewtonErr err = kCommErrNotConnected;
	if (fToolState & kToolStateConnected)
	{
		// DEVIATION (pointer size): the host's size of the request (the ROM
		// copies 0x2c bytes)
		memcpy(&fControl, fRequest, sizeof(fControl));
		fModemFlags = (fModemFlags & ~kModemFlagFrameUnfinished) | kModemFlagPacketPut;
		if ((fModemFlags & kModemFlagPacketLine) || fFaxClass.fClass == kModemFaxClass2 || fFaxClass.fClass == kModemFaxClass20)
		{
			C1PktPutBytes(kModemResultConnect);
			return;
		}
		if ((err = BlockGetAndPutChannel()) == noErr)
		{
			if (fModemFlags & kModemFlagFaxAnswer)
			{
				// answering: the modem is sending already
				fModemFlags = (fModemFlags & ~kModemFlagFaxAnswer) | kModemFlagPutConnected;
				C1PktPutBytes(kModemResultConnect);
			}
			else if (fModemFlags & kModemFlagPutConnected)
				C1PktPutBytes(kModemResultConnect);
			else if (fControl.fPacket.fDuration == 0)
				C1PktTransCmd(kModemResultOK);
			else
				C1PktTransSilent();
			return;
		}
	}
	C1PktComplete(err);
}


// ROM 0x0005dc88 DoRecvPkt__14TClassOneModemFv
void
TClassOneModem::DoRecvPkt()
{
	if (ShouldAbort(0, noErr))
		return;
	NewtonErr err = kCommErrNotConnected;
	if ((fToolState & kToolStateConnected) && (err = BlockGetAndPutChannel()) == noErr)
	{
		// DEVIATION (pointer size): the host's size of the request
		memcpy(&fControl, fRequest, sizeof(fControl));
		fModemFlags = (fModemFlags & ~kModemFlagPutConnected) | kModemFlagPacketGet;
		if (fModemFlags & kModemFlagFaxOriginate)
		{
			// calling: the modem is receiving already
			fModemFlags &= ~kModemFlagFaxOriginate;
			C1PktGetBytes(kModemResultConnect);
		}
		else if (!(fModemFlags & kModemFlagFrameUnfinished))
			C1PktRecvCmd();
		else
			C1PktGetBytes(kModemResultConnect);
		return;
	}
	C1PktComplete(err);
}


// ROM 0x0005de08 C1PktComplete__14TClassOneModemFl
void
TClassOneModem::C1PktComplete(NewtonErr result)
{
	if (!(fModemFlags & kModemFlagPacketLine))
		UnblockGetAndPutChannel();
	fModemFlags &= ~(kModemFlagPacketPut | kModemFlagPacketGet);
	CompleteRequest(kCommToolControlChannel, result, fReply);
}


// ROM 0x0005de54 C1PktAbort__14TClassOneModemFv
// The packet given up (timed out or killed): the put or get killed, flow
// control off, and "AT" to stop the modem; its answer completes the
// request (fPacketState 10).
void
TClassOneModem::C1PktAbort()
{
	fModemFlags |= kModemFlagPacketAborted;
	if (fZeroStuffFlags & kZeroStuffOn)
		ZeroStuffingDeinit();
	AbortCommand();
	if (fModemFlags & kModemFlagPacketPut)
		TFramedAsyncSerTool::KillPut();
	else if (fModemFlags & kModemFlagPacketGet)
	{
		TFramedAsyncSerTool::KillGet();
		SetInputSendForIntDelay(11058);
	}
	fOutFlowParms.useHardFlowControl = false;
	fOutFlowParms.useSoftFlowControl = false;
	fInFlowParms.useHardFlowControl = false;
	fInFlowParms.useSoftFlowControl = false;
	SetOutputFlowControl(&fOutFlowParms);
	SetInputFlowControl(&fInFlowParms);
	fModemFlags &= ~(kModemFlagFrameUnfinished | kModemFlagPacketLine | kModemFlagPacketAborted | kModemFlagPutConnected);
	fPacketState = 10;
	NewtonErr err = BeginModemCommand(kModemCmdAttention);
	if (err != noErr)
	{
		C1PktComplete(kCommErrRequestCanceled);
		KillRequestComplete(kCommToolRequestTypeControl, err);
	}
}


// ROM 0x0005df48 C1PktContinue__14TClassOneModemFl
// The packet's next step, or its end.
void
TClassOneModem::C1PktContinue(NewtonErr result)
{
	if (fModemFlags & kModemFlagPacketAborted)
		return;
	if (fPacketState == 10)
	{
		// the "AT" after an abort answered
		if (fModemFlags & kModemFlagPacketKill)
		{
			C1PktComplete(kCommErrRequestCanceled);
			KillRequestComplete(kCommToolRequestTypeControl, result);
		}
		else
			C1PktComplete(kModemErrNoCarrier);
		fModemFlags &= ~(kModemFlagPacketKill | kModemFlagPacketTimedOut);
	}
	if (result != noErr)
	{
		if (result == kModemErrNoResponse)
			result = (fPacketState == 8 && fControl.fOpCode == kModemCtlC1RecvHDLC) ? kModemErrRcvPktFlagsTimeOut : kModemErrCommandFailure;
		else
		{
			if (result == kSerErr_AsyncError && (fControl.fOpCode == kModemCtlC1RecvData || fControl.fOpCode == kModemCtlC1SendData))
				fModemFlags &= ~kModemFlagPacketLine;
			else
				fModemFlags &= ~(kModemFlagFrameUnfinished | kModemFlagPacketLine | kModemFlagPutConnected);
		}
		SetInputSendForIntDelay(11058);
		AbortTimer();
		C1PktComplete(result);
		return;
	}
	switch (fPacketState)
	{
	case 1:		C1PktTransSilent(); break;
	case 2:		C1PktTransCmd(fReply.fResultCode); break;
	case 3:		C1PktRecvCmd(); break;
	case 4:		C1PktPutBytes(fReply.fResultCode); break;
	case 5:		C1PktGetBytes(fReply.fResultCode); break;
	case 6:		C1PktGetPutResult(); break;
	case 7:		C1PktCheckPutResult(fReply.fResultCode); break;
	case 8:		C1PktGetPktCRC(); break;
	case 9:		C1PktCheckPktCRC(fReply.fResultCode); break;
	case 11:	C1PktCheckFRMResult(fReply.fResultCode); break;
	case 12:	C2PktGetBytes(); break;
	case 13:	C2PktCheckResult(fReply.fResultCode); break;
	}
}


// ROM 0x0005e150 C1PktTransSilent__14TClassOneModemFv
// The silence asked for waited out before the command.
void
TClassOneModem::C1PktTransSilent()
{
	fPacketState = 2;
	NewtonErr err = PostTimer(kModemTimerSilence, fControl.fPacket.fDuration);
	if (err != noErr)
		C1PktComplete(err);
}


// ROM 0x0005e190 C1PktTransCmd__14TClassOneModemF16Class1CmdResults
// "+FTH=" or "+FTM=".
void
TClassOneModem::C1PktTransCmd(ULong result)
{
	NewtonErr err = kModemErrCommandFailure;
	if (result != kModemResultUnknown)
	{
		fPacketState = 4;
		if (fControl.fOpCode == kModemCtlC1SendHDLC)
			err = BeginModemCommand(kModemCmdC1TransmitHDLC);
		else if (fControl.fOpCode == kModemCtlC1SendData)
			err = BeginModemCommand(kModemCmdC1TransmitData);
		else
			err = kCommErrBadParameter;
		if (err == noErr)
			return;
	}
	C1PktComplete(err);
}


// ROM 0x0005e204 C1PktRecvCmd__14TClassOneModemFv
// "+FRH=" or "+FRM=".
void
TClassOneModem::C1PktRecvCmd()
{
	NewtonErr err;
	fPacketState = 5;
	if (fControl.fOpCode == kModemCtlC1RecvHDLC)
		err = BeginModemCommand(kModemCmdC1ReceiveHDLC);
	else if (fControl.fOpCode == kModemCtlC1RecvData)
		err = BeginModemCommand(kModemCmdC1ReceiveData);
	else
		err = kCommErrBadParameter;
	if (err != noErr)
		C1PktComplete(err);
}


// ROM 0x0005e26c C1PktPutBytes__14TClassOneModemF16Class1CmdResults
// CONNECT: the frame or data put, framed (the modem's flow control on the
// way out; a page's data padded out with fill bytes).
void
TClassOneModem::C1PktPutBytes(ULong result)
{
	NewtonErr err;
	if (result != kModemResultConnect)
	{
		err = CheckForErrorResult(result);
		if (err == noErr)
			err = kModemErrCommandFailure;
		C1PktComplete(err);
		return;
	}
	if (!(fModemFlags & kModemFlagPacketLine))
	{
		if (!(fModemFlags & kModemFlagHardwareFlow))
		{
			fOutFlowParms.useSoftFlowControl = true;
			fInFlowParms.useSoftFlowControl = false;
		}
		else
		{
			fOutFlowParms.useHardFlowControl = true;
			fInFlowParms.useHardFlowControl = false;
		}
		SetOutputFlowControl(&fOutFlowParms);
		SetInputFlowControl(&fInFlowParms);
		if (fControl.fOpCode == kModemCtlC1SendData && (err = ZeroStuffingInit()) != noErr)
		{
			C1PktComplete(err);
			return;
		}
	}
	fModemFlags |= kModemFlagPacketLine | kModemFlagPutConnected;
	if (fZeroStuffFlags & kZeroStuffOn)
	{
		if (fZeroStuffFlags & kZeroStuffFillPut)
		{
			fZeroStuffFlags |= kZeroStuffKilling;
			TFramedAsyncSerTool::KillPut();
			fZeroStuffFlags &= ~kZeroStuffKilling;
		}
		fZeroStuffFlags |= kZeroStuffDataPut;
	}
	fPacketState = 6;
	fModemFlags |= kModemFlagPacketLine | kModemFlagPutConnected;
	TSerTool::PutFramedBytes(fControl.fPacket.fData, fControl.fPacket.fFinal);
}


// ROM 0x0005e394 C1PktGetBytes__14TClassOneModemF16Class1CmdResults
// CONNECT: a frame got (an HDLC frame within 3 seconds, data within the
// request's time).
void
TClassOneModem::C1PktGetBytes(ULong result)
{
	NewtonErr err;
	if (result != kModemResultConnect)
	{
		err = CheckForErrorResult(result);
		if (err == noErr)
			err = kModemErrCommandFailure;
		C1PktComplete(err);
		return;
	}
	if (!(fModemFlags & kModemFlagFrameUnfinished))
	{
		if (!(fModemFlags & kModemFlagHardwareFlow))
		{
			fOutFlowParms.useSoftFlowControl = false;
			fInFlowParms.useSoftFlowControl = true;
		}
		else
		{
			fOutFlowParms.useHardFlowControl = false;
			fInFlowParms.useHardFlowControl = true;
		}
		SetOutputFlowControl(&fOutFlowParms);
		SetInputFlowControl(&fInFlowParms);
	}
	if (fControl.fOpCode == kModemCtlC1RecvHDLC)
	{
		if ((err = PostTimer(kModemTimerPacketHDLC, 3000)) != noErr)
		{
			C1PktComplete(err);
			return;
		}
	}
	else
	{
		if ((err = PostTimer(kModemTimerPacketData, fControl.fPacket.fDuration)) != noErr)
		{
			C1PktComplete(err);
			return;
		}
		SetInputSendForIntDelay(184300);
	}
	fPacketState = 8;
	TSerTool::GetFramedBytes(fControl.fPacket.fData);
}


// ROM 0x0005e680 C1PktGetPutResult__14TClassOneModemFv
// The last of the page's data or frames gone: the modem's answer (within
// 15 seconds).
void
TClassOneModem::C1PktGetPutResult()
{
	if (fControl.fPacket.fFinal)
	{
		fPacketState = 7;
		if (fZeroStuffFlags & kZeroStuffOn)
			ZeroStuffingDeinit();
		if (fFaxClass.fClass != kModemFaxClass2 && fFaxClass.fClass != kModemFaxClass20)
		{
			fCommandTimeout = 15000;
			fModemFlags &= ~kModemFlagPacketLine;
			GetCommandResult();
			return;
		}
	}
	C1PktComplete(noErr);
}


// ROM 0x0005e6f8 C1PktCheckPutResult__14TClassOneModemF16Class1CmdResults
// CONNECT: the modem goes on sending (another frame to come); OK: it has
// stopped.
void
TClassOneModem::C1PktCheckPutResult(ULong result)
{
	NewtonErr err = noErr;
	if (result == kModemResultConnect)
		fModemFlags |= kModemFlagPutConnected;
	else if (result == kModemResultOK)
		fModemFlags &= ~kModemFlagPutConnected;
	else
	{
		err = CheckForErrorResult(result);
		if (err == noErr)
			err = kModemErrCommandFailure;
	}
	C1PktComplete(err);
}


// ROM 0x0005e758 C1PktCheckFRMResult__14TClassOneModemF16Class1CmdResults
// The page's data ended: NO CARRIER or OK is right, ERROR a bad frame.
void
TClassOneModem::C1PktCheckFRMResult(ULong result)
{
	NewtonErr err;
	if (result == kModemResultOK || result == kModemResultNoCarrier)
		err = noErr;
	else if (result == kModemResultError)
		err = kSerErr_CRCError;
	else
	{
		err = CheckForErrorResult(result);
		if (err == noErr)
			err = kModemErrCommandFailure;
	}
	C1PktComplete(err);
}


// ROM 0x0005e7b4 C1PktGetPktCRC__14TClassOneModemFv
// A frame got: an HDLC frame's end has the modem's verdict on its check
// (OK or ERROR) to read; the page's data's end its NO CARRIER.  A frame
// that did not end is completed as it is, and the next get goes on with
// it.
void
TClassOneModem::C1PktGetPktCRC()
{
	AbortTimer();
	if (fFaxClass.fClass == kModemFaxClass2 || fFaxClass.fClass == kModemFaxClass20)
	{
		if (fReply.fText[0] == 0)
			fModemFlags |= kModemFlagFrameUnfinished;
		else
		{
			fModemFlags &= ~kModemFlagFrameUnfinished;
			SetInputSendForIntDelay(11058);
		}
		C1PktComplete(noErr);
		return;
	}
	if (fControl.fOpCode == kModemCtlC1RecvData)
	{
		SetInputSendForIntDelay(11058);
		if (fReply.fText[0] == 0)
		{
			fModemFlags |= kModemFlagFrameUnfinished;
			C1PktComplete(noErr);
			return;
		}
		fModemFlags &= ~kModemFlagFrameUnfinished;
		fPacketState = 11;
		fCommandTimeout = 3000;
	}
	else
	{
		if (fReply.fText[0] == 0)
		{
			C1PktComplete(noErr);
			return;
		}
		fPacketState = 9;
	}
	GetCommandResult();
}


// ROM 0x0005e89c C1PktCheckPktCRC__14TClassOneModemF16Class1CmdResults
void
TClassOneModem::C1PktCheckPktCRC(ULong result)
{
	NewtonErr err;
	if (result == kModemResultOK)
		err = noErr;
	else if (result == kModemResultError)
		err = kSerErr_CRCError;
	else if (result == kModemResultNoCarrier)
		err = kModemErrRcvPktFlagsTimeOut;
	else
	{
		err = CheckForErrorResult(result);
		if (err == noErr)
			err = kModemErrCommandFailure;
	}
	C1PktComplete(err);
}


/*------------------------------------------------------------------------------
	Fill bytes
	While a page's data is being sent, the modem must never run dry: every
	100 ms (the tool's timer tick) the bytes the modulation should have
	sent by now (plus 4%) are compared with those given it, and noughts
	make up the difference - at least 25, at most 120 ms' worth.
------------------------------------------------------------------------------*/

// ROM 0x0005ef78 ZeroStuffingInit__14TClassOneModemFv
NewtonErr
TClassOneModem::ZeroStuffingInit()
{
	if (fZeroBuffer != nil)
		ZeroStuffingDeinit();
	ULong bytesPerSecond = GetModBaudRate(fControl.fPacket.fModulation) >> 3;
	fZeroStuffRate = bytesPerSecond + (bytesPerSecond * 4) / 100;
	fZeroBufferSize = (fZeroStuffRate * 120) / 1000;
	fZeroBuffer = (UChar*) NewPtrClear(fZeroBufferSize);
	NewtonErr err;
	if (fZeroBuffer == nil)
		err = kError_No_Memory;
	else if ((err = fZeroSegment.Init(fZeroBuffer, fZeroBufferSize)) == noErr
		 &&  (err = fZeroList.Init(false)) == noErr)
	{
		fZeroList.InsertLast(&fZeroSegment);
		fZeroStuffStart.Set(0, 0);
		fZeroStuffSent = 0;
		fZeroStuffFlags = kZeroStuffOn;
		fTimerInterval = 368600;		// 100 ms
		fTimeout = 368600;
	}
	return err;
}


// ROM 0x0005ee1c ZeroStuffing__14TClassOneModemFv
void
TClassOneModem::ZeroStuffing()
{
	if (!(fZeroStuffFlags & kZeroStuffOn) || (fZeroStuffFlags & kZeroStuffDataPut))
		return;
	TTime nought(0, 0);
	if (CompCompare(&fZeroStuffStart.time, &nought.time) == 0)
		fZeroStuffStart = GetGlobalTime();
	TTime elapsed = GetGlobalTime();
	CompSub(&fZeroStuffStart.time, &elapsed.time);
	TTime tick(368600, 0);
	CompAdd(&tick.time, &elapsed.time);
	ULong due = (fZeroStuffRate * elapsed.ConvertTo(kMilliseconds)) / 1000;
	if (fZeroStuffSent >= due)
		return;
	ULong count = due - fZeroStuffSent;
	if ((Long) count >= (Long) fZeroBufferSize)
		count = fZeroBufferSize;
	if (count < 25)
		count = 25;
	if (fZeroSegment.Init(fZeroBuffer, count) != noErr)
		return;
	fZeroStuffFlags |= kZeroStuffFillPut;
	TSerTool::PutFramedBytes(&fZeroList, false);
}


// ROM 0x0005f088 ZeroStuffingDeinit__14TClassOneModemFv
void
TClassOneModem::ZeroStuffingDeinit()
{
	fTimerInterval = 0;
	fTimeout = 0;
	fZeroStuffFlags = 0;
	if (fZeroBuffer != nil)
	{
		DisposPtr((Ptr) fZeroBuffer);
		fZeroBuffer = nil;
	}
}


/*------------------------------------------------------------------------------
	Class 2
	The fax transport's Class 2 and 2.0 commands (control requests 0x11a to
	0x162 are the commands 0x20 to 0x68), answered with the modem's
	result; a page's data received framed by DLE ETX after a DC2.
------------------------------------------------------------------------------*/

// ROM 0x0005f3dc C2DoCommand__14TClassOneModemFUl
NewtonErr
TClassOneModem::C2DoCommand(ULong opCode)
{
	// DEVIATION (pointer size): the host's size of the request
	memcpy(&fControl, fRequest, sizeof(fControl));
	NewtonErr err;
	if (opCode >= kModemCtlC2First && opCode <= kModemCtlC2Last)
	{
		ULong command = opCode - kModemCtlC2First + kModemCmdC2First;
		if (opCode == 0x127)				// "+FDT": the page's data follows
		{
			if ((err = BlockGetAndPutChannel()) != noErr)
				goto failed;
		}
		fCommandTimeout = (fControl.fTimeout != 0) ? fControl.fTimeout : fProfile->fCommandTimeout;
		if ((err = BeginModemCommand(command)) == noErr)
			return noErr;
	}
	else
		err = kModemErrCommandFailure;
failed:
	CompleteRequest(kCommToolControlChannel, err, fReply);
	return noErr;
}


// ROM 0x0005dbfc C2ModemRecvPgData__14TClassOneModemFv
void
TClassOneModem::C2ModemRecvPgData()
{
	if (ShouldAbort(0, noErr))
		return;
	NewtonErr err = kCommErrNotConnected;
	if ((fToolState & kToolStateConnected) && (err = BlockGetAndPutChannel()) == noErr)
	{
		// DEVIATION (pointer size): the host's size of the request
		memcpy(&fControl, fRequest, sizeof(fControl));
		fModemFlags = (fModemFlags & ~kModemFlagPutConnected) | kModemFlagPacketGet;
		if (!(fModemFlags & kModemFlagFrameUnfinished))
			C2PktGetBytesSetup();
		else
			C2PktGetBytes();
		return;
	}
	C1PktComplete(err);
}


// ROM 0x0005e494 C2PktGetBytesSetup__14TClassOneModemFv
// The page's start: flow control inward, a DC2 to have the modem send, and
// a fifth of a second.
void
TClassOneModem::C2PktGetBytesSetup()
{
	if (!(fModemFlags & kModemFlagHardwareFlow))
	{
		fOutFlowParms.useSoftFlowControl = false;
		fInFlowParms.useSoftFlowControl = true;
	}
	else
	{
		fOutFlowParms.useHardFlowControl = false;
		fInFlowParms.useHardFlowControl = true;
	}
	SetOutputFlowControl(&fOutFlowParms);
	SetInputFlowControl(&fInFlowParms);
	SetInputSendForIntDelay(184300);
	fCmdList.RemoveAll();
	NewtonErr err = fCmdSegment.Init((void*) cmdDC2, 1);
	if (err == noErr)
	{
		fCmdList.InsertLast(&fCmdSegment);
		TSerTool::PutBytes(&fCmdList);
		if ((err = PostTimer(kModemTimerC2Setup, 200)) == noErr)
			return;
	}
	C1PktComplete(err);
}


// ROM 0x0005e580 C2PktGetBytesSetupCont__14TClassOneModemFv
// Hardly anything come yet: a DC1 (XON) too.
void
TClassOneModem::C2PktGetBytesSetupCont()
{
	ULong count;
	BytesAvailable(count);
	if (count < 3)
	{
		fCmdList.RemoveAll();
		NewtonErr err = fCmdSegment.Init((void*) cmdDC1, 1);
		if (err != noErr)
		{
			C1PktComplete(err);
			return;
		}
		fCmdList.InsertLast(&fCmdSegment);
		TSerTool::PutBytes(&fCmdList);
	}
	C2PktGetBytes();
}


// ROM 0x0005e624 C2PktGetBytes__14TClassOneModemFv
void
TClassOneModem::C2PktGetBytes()
{
	NewtonErr err = PostTimer(kModemTimerPacketData, fControl.fPacket.fDuration);
	if (err != noErr)
	{
		C1PktComplete(err);
		return;
	}
	fPacketState = 8;
	TSerTool::GetFramedBytes(fControl.fPacket.fData);
}


// ROM 0x0005e66c C2PktCheckResult__14TClassOneModemF16Class1CmdResults
// +FPTS: the page received.
void
TClassOneModem::C2PktCheckResult(ULong result)
{
	C1PktComplete(result == 0x24 ? noErr : kModemErrCommandFailure);
}
