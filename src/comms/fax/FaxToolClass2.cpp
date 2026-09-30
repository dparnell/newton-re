/*
	File:		comms/fax/FaxToolClass2.cpp

	Contains:	TFaxTool over a Class 2 or Class 2.0 modem (FaxTool.h): the
				modem runs T.30 itself, and the tool sequences its commands
				- C2StateUpdate (Class 2) and C20StateUpdate (Class 2.0),
				state machines over fC2State driven by events.  An event is
				what the modem tool answered, turned into a number by
				C2ModemReqComplete: 0x14 and up for its result codes (0x14
				OK, 0x15 CONNECT, ... 0x2b +FCSI, 0x2c +FDIS, 0x2f +FET,
				0x30 +FHNG, 0x38 +FPTS, ... 0x4e one it does not know), 0x20
				no response, 9 or 10 a framing option set (or not); and 1
				to 8 the tool's own (1 connect, 2 listen, 3 the call made,
				4 the call failed, 5 a page started, 6 a page ended, 7 phase
				D, 8 the page's data sent).  The Class 2 commands
				themselves are the modem tool's (ModemTool.h: C2DoCommand,
				control requests kModemCtlC2First..).

	Reconstructed from the MP2x00 US ROM (0x000b1fa0-0x000b4a30); each
	function cites its origin.
*/

#include "FaxTool.h"
#include "CommErrors.h"
#include "NewtErrors.h"
#include "OSErrors.h"
#include "OptionArray.h"

#include <string.h>


/*------------------------------------------------------------------------------
	The modem's answers as events.
------------------------------------------------------------------------------*/

// ROM 0x000b1fa0 C2InitSubSystem__8TFaxToolFv
NewtonErr
TFaxTool::C2InitSubSystem()
{
	fC2State = 1;
	fC2Framing = 0;
	return noErr;
}


// ROM 0x000b25d0 C2ModemReqComplete__8TFaxToolFP22TClassOneModemCmdReply
// The modem tool's answer as an event for the state machine, what the
// answer carried kept (the other machine's id, its DIS or DCS, the page's
// status, the hang-up status).
void
TFaxTool::C2ModemReqComplete(TClassOneModemCmdReply* reply)
{
	ULong event = 0;
	if (fC2Framing == 0)
	{
		if (reply->fResult != noErr)
			event = (reply->fResult == kModemErrNoResponse) ? 0x20 : 0x4e;
		else
		{
			ULong code = reply->fResultCode;
			switch (code)
			{
			case 0: case 1: case 2: case 3: case 4:
				event = 0x14 + code;
				break;
			case 6: event = 0x19; break;
			case 7: event = 0x1a; break;
			case 8: event = 0x1b; break;
			case 0xd: event = 0x1c; break;
			case 0xf: event = 0x1d; break;
			case 0x10: event = 0x1e; break;
			case 0x11: event = 0x1f; break;
			case 0x12: event = 0x29; break;
			case 0x13: event = 0x28; break;
			case 0x14: event = 0x2a; break;
			case 0x15: event = 0x2b; goto remoteId;
			case 0x16: event = 0x2c; goto dis;
			case 0x17: event = 0x2d; break;
			case 0x18: event = 0x2e; break;
			case 0x19: event = 0x2f; goto pageStatus;
			case 0x1a: event = 0x30; goto hangUpStatus;
			case 0x1b: event = 0x31; break;
			case 0x1c: event = 0x32; break;
			case 0x1f: event = 0x33; break;
			case 0x20: event = 0x34; break;
			case 0x21: event = 0x35; break;
			case 0x22: event = 0x36; break;
			case 0x23: event = 0x37; break;
			case 0x24: event = 0x38; goto pts;
			case 0x25: event = 0x39; break;
			case 0x27: event = 0x3a; goto remoteId;
			case 0x28: event = 0x3b; break;
			case 0x29: event = 0x3c; break;
			case 0x2a: event = 0x3d; break;
			case 0x2b: event = 0x3e; break;
			case 0x2c: event = 0x3f; goto hangUpStatus;
			case 0x2d: event = 0x40; goto dis;
			case 0x2e: event = 0x41; break;
			case 0x2f: event = 0x42; goto dis;
			case 0x30: event = 0x43; break;
			case 0x31: event = 0x44; goto remoteId;
			case 0x32: event = 0x45; goto remoteId;
			case 0x33: event = 0x46; goto remoteId;
			case 0x34: event = 0x47; break;
			case 0x35: event = 0x48; break;
			case 0x36: event = 0x49; break;
			case 0x37: event = 0x4a; goto pageStatus;
			case 0x38: event = 0x4b; goto pts;
			case 0x39: event = 0x4c; break;
			case 0x3a: event = 0x4d; break;
			case 5: case 9: case 0xa: case 0xb: case 0xc: case 0xe:
			case 0x1d: case 0x1e: case 0x26:
				break;
			default:
				event = 0x4e;
				break;
			remoteId:
				strncpy((char*) fRemoteId.fId, (const char*) fModemReply.fText, 0x15);
				fRemoteId.fId[0x14] = 0;
				break;
			dis:
				fC2RemoteDIS = fModemReply.fDIS;
				break;
			pageStatus:
				fC2PageStatus = fModemReply.fText[0];
				break;
			hangUpStatus:
				fC2HangUpStatus = fModemReply.fText[0];
				break;
			pts:
				fC2PTS = fModemReply.fPTS;
				break;
			}
		}
	}
	else if (fC2Framing == 1)
	{
		event = (reply->fResult != noErr) ? 10 : 9;
		fC2Framing = 0;
	}
	else
		return;
	if (event == 0)
		return;
	if (fFaxClass != 4)
		C20StateUpdate(event);
	else
		C2StateUpdate(event);
}


/*------------------------------------------------------------------------------
	Class 2.
------------------------------------------------------------------------------*/

// ROM 0x000b3244 C2StateUpdate__8TFaxToolFUl
void
TFaxTool::C2StateUpdate(ULong event)
{
	ULong state = fC2State;
	UChar done;
	switch (fC2State)
	{
	case 1:
		if (event == 1 || event == 2)
		{
			state = (event == 1) ? 2 : 3;
			fC2ConfigStep = 0;
			if (C2ConfigModem(&done) != noErr)
				StartAbort(kModemErrNoResponse);
		}
		else
			StartAbort(kFaxToolErrProtocolError);
		break;

	case 2:
		C2TransCfgMdm(state);
		break;

	case 3:
		C2RecvCfgMdm(state);
		break;

	case 4:
		if (event == 3)
		{
			state = 0xa;
			C2GetModemRsp(60000, state);
		}
		else if (event == 4)
			state = 1;
		else
			C2AbortSession(state, kFaxToolErrProtocolError);
		break;

	case 5:
		switch (event)
		{
		case 0x2c:
			if (fToolState & kToolStateConnecting)
			{
				C2SetSessionParameters();
				C2PhaseBProcessOptions();
			}
			C2GetModemRsp(20000, state);
			break;
		case 0x2a:
		case 0x35:
		case 0x3a:
			C2GetModemRsp(20000, state);
			break;
		case 3:
			C2GetModemRsp(60000, state);
			break;
		case 4:
			state = 1;
			break;
		case 0x14:
			if (C2ValidateDCS(fC2RemoteDIS, fC2LocalDIS))
				C2RecvFDR_Cmd(state);
			else
				C2AbortSession(state, kFaxToolErrProtocolError);
			break;
		case 0x30:
			C2FHNG_Rsp(state);
			break;
		case 0x20:
			C2AbortSession(state, kModemErrNoResponse);
			break;
		default:
			C2AbortSession(state, kFaxToolErrProtocolError);
			break;
		}
		break;

	case 0xa:
		switch (event)
		{
		case 0x2a:
		case 0x2b:
		case 0x2d:
		case 0x34:
			C2GetModemRsp(60000, state);
			break;
		case 0x14:
			C2TransFDT_Cmd(state);
			break;
		case 0x17:
		case 0x18:
		case 0x19:
		case 0x1a:
			state = 1;
			break;
		case 0x30:
			C2FHNG_Rsp(state);
			break;
		case 0x20:
			C2AbortSession(state, kModemErrNoResponse);
			break;
		default:
			C2AbortSession(state, kFaxToolErrProtocolError);
			break;
		}
		break;

	case 0xb:
		if (event == 5)
			C2TransFDT_Rsp(state);
		else
			C2AbortSession(state, kFaxToolErrProtocolError);
		break;

	case 0xc:
	case 0xd:
		switch (event)
		{
		case 5:
			if (fC2State == 0xd)
			{
				state = 0xc;
				break;
			}
			C2AbortSession(state, kFaxToolErrProtocolError);
			break;
		case 0x15:
			if (fC2State == 0xd)
				state = 0xb;
			else
				C2TransFDT_Rsp(state);
			break;
		case 0x2c:
			if (fToolState & kToolStateConnecting)
			{
				C2PhaseBProcessOptions();
				ConnectComplete(noErr);
			}
			C2GetModemRsp(60000, state);
			break;
		case 0x30:
			C2FHNG_Rsp(state);
			break;
		case 0x20:
			C2AbortSession(state, kModemErrNoResponse);
			break;
		default:
			C2AbortSession(state, kFaxToolErrProtocolError);
			break;
		}
		break;

	case 0xe:
		switch (event)
		{
		case 6:
			DoEndPage();
			break;
		case 7:
			C2GetModemRsp(60000, state);
			break;
		case 8:
			SendEOMCont();
			break;
		case 0x14:
			C2TransFET_Cmd(state);
			break;
		default:
			C2AbortSession(state, kFaxToolErrProtocolError);
			break;
		}
		break;

	case 0xf:
		switch (event)
		{
		case 0x14:
			C2TransFET_Rsp(state);
			break;
		case 0x30:
			C2FHNG_Rsp(state);
			break;
		case 0x38:
			C2TransFPTS_Rsp(state);
			break;
		case 0x20:
			C2AbortSession(state, kModemErrNoResponse);
			break;
		default:
			C2AbortSession(state, kFaxToolErrProtocolError);
			break;
		}
		break;

	case 0x15:
		switch (event)
		{
		case 0x29:
		case 0x2a:
		case 0x2c:
		case 0x35:
		case 0x3a:
			C2GetModemRsp(20000, state);
			break;
		case 0x15:
			C2RecvFDR_Rsp(state);
			break;
		case 0x30:
			C2FHNG_Rsp(state);
			break;
		case 0x20:
			C2AbortSession(state, kModemErrNoResponse);
			break;
		default:
			C2AbortSession(state, kFaxToolErrProtocolError);
			break;
		}
		break;

	case 0x16:
		if (event == 7)
		{
			state = 0x17;
			C2GetModemRsp(10000, state);
		}
		else
			C2AbortSession(state, kFaxToolErrProtocolError);
		break;

	case 0x17:
	case 0x19:
		switch (event)
		{
		case 0x2f:
		case 0x38:
			C2GetModemRsp(10000, state);
			break;
		case 6:
			if (fC2State == 0x17)
			{
				state = 0x19;
				break;
			}
			C2AbortSession(state, kFaxToolErrProtocolError);
			break;
		case 0x14:
			if (fC2State == 0x17)
				state = 0x18;
			else
				C2RecvCopyQualityCheck(state);
			break;
		case 0x30:
			C2FHNG_Rsp(state);
			break;
		case 0x20:
			C2AbortSession(state, kModemErrNoResponse);
			break;
		default:
			C2AbortSession(state, kFaxToolErrProtocolError);
			break;
		}
		break;

	case 0x18:
		if (event == 6)
			C2RecvCopyQualityCheck(state);
		else
			C2AbortSession(state, kFaxToolErrProtocolError);
		break;

	case 0x1a:
		switch (event)
		{
		case 0x14:
			C2RecvFDR_Cmd(state);
			break;
		case 0x30:
			C2FHNG_Rsp(state);
			break;
		case 0x20:
			C2AbortSession(state, kModemErrNoResponse);
			break;
		default:
			C2AbortSession(state, kFaxToolErrProtocolError);
			break;
		}
		break;

	case 0x1e:
		if (event == 0x30)
			C2FHNG_Rsp(state);
		else if (event == 0x20)
			C2AbortSession(state, kModemErrNoResponse);
		else
			C2AbortSession(state, kFaxToolErrProtocolError);
		break;

	case 0x1f:
		if (event == 0x14)
		{
			state = 1;
			if (StartAbort(kFaxToolErrProtocolError) != noErr)
				TerminateConnection();
		}
		else if (event == 0x20)
		{
			StartAbort(kModemErrNoResponse);
			state = 1;
		}
		else
			C2GetModemRsp((event == 0x30) ? 4000 : 3000, state);
		break;

	case 0x20:
		if (event == 0x14)
			C2FHNG_OK_Rsp(state);
		else
		{
			StartAbort(kFaxToolErrProtocolError);
			state = 1;
		}
		break;

	default:
		C2AbortSession(state, kFaxToolErrProtocolError);
		break;
	}
	fC2State = state;
}


// ROM 0x000b3840 C2FHNG_Rsp__8TFaxToolFRUl
// +FHNG: the OK that follows it awaited.
void
TFaxTool::C2FHNG_Rsp(ULong& state)
{
	C2GetModemRsp(5000, state);
	state = 0x20;
}


// ROM 0x000b386c C2FHNG_OK_Rsp__8TFaxToolFRUl
// The session over: whatever the client has in hand answered - the
// connect, the get, the 'fsgp' or 'feom' - kFaxToolErrTransmissionFailed
// if the modem said the call failed or the last page was bad.
void
TFaxTool::C2FHNG_OK_Rsp(ULong& state)
{
	NewtonErr result = noErr;
	UChar hangUp = fC2HangUpStatus;
	if (hangUp != 0 || fC2PTS.fStatus == '2' || fC2PTS.fStatus == '4')
		result = kFaxToolErrTransmissionFailed;
	if (fToolState & kToolStateListenMode)
		StartAbort(kFaxToolErrProtocolError);
	else if (fToolState & kToolStateConnecting)
		ConnectComplete(result);
	else if (fClientBuffer != nil)
	{
		GetComplete(kFaxToolErrProtocolError, fReceiveState == 3, fClientBuffer->GetSize());
		StartAbort(kFaxToolErrProtocolError);
	}
	else if (fOptionsInfo.fCurOptPtr != nil && fOptionsInfo.fCurOptPtr->Label() == kCMOFaxStartPage)
		StartPageComplete(result);
	else if (fOptionsInfo.fCurOptPtr != nil && fOptionsInfo.fCurOptPtr->Label() == kCMOFaxEndMessage)
		EndPageComplete(result);
	else if (hangUp != 0)
		StartAbort(kFaxToolErrProtocolError);
	else
		TerminateConnection();
	state = 1;
}


// ROM 0x000b39ac C2RecvFDR_Cmd__8TFaxToolFRUl
// +FDR: a page to be received (the listen complete, a 'feom' in hand for a
// page that was not the last answered).
void
TFaxTool::C2RecvFDR_Cmd(ULong& state)
{
	fFaxFlags |= kFaxFlagReceivingLines;
	fReceiveState = 1;
	fDecodeBufIndex = 0;
	fReceiveBufs[1].fFull = false;
	fReceiveBufs[0].fFull = false;
	if (fToolState & kToolStateListenMode)
		ListenComplete(noErr);
	if ((fFaxFlags & kFaxFlagEndMessage) && !((TCMOFaxEndMessage*) fOptionsInfo.fCurOptPtr)->fLastPage)
	{
		fFaxFlags &= ~kFaxFlagEndMessage;
		((TOptionExtended*) fOptionsInfo.fCurOptPtr)->SetExtendedResult(noErr);
		ProcessOptionComplete(opSuccess);
	}
	fModemRequest.fTimeout = 60000;
	if (PostModemCommand(0x126) == noErr)
	{
		FreeReceiveBuffers();
		if (AllocateReceiveBuffers() == noErr)
		{
			state = 0x15;
			return;
		}
	}
	C2AbortSession(state, kFaxToolErrProtocolError);
}


// ROM 0x000b3a9c C2RecvFDR_Rsp__8TFaxToolFRUl
void
TFaxTool::C2RecvFDR_Rsp(ULong& state)
{
	StartPhaseC();
	state = 0x16;
}


// ROM 0x000b3abc C2AbortSession__8TFaxToolFRUll
// +FK (the session aborted) and the tool with it.  BUG: error is not used -
// the tool always aborts with kModemErrNoResponse.
void
TFaxTool::C2AbortSession(ULong& state, NewtonErr error)
{
	fModemRequest.fTimeout = 10000;
	if (PostModemCommand(0x12b) == noErr)
		state = 0x1f;
	StartAbort(kModemErrNoResponse);
}


// ROM 0x000b3b0c C2ConfigModem__8TFaxToolFPUc
// The modem set up a command at a time as each is answered (fC2ConfigStep):
// +FCLASS=2, +FCR, +FLID (our id), +FDIS (our capabilities), +FLPL, +FAA;
// done when the last is answered.
NewtonErr
TFaxTool::C2ConfigModem(UChar* done)
{
	*done = 0;
	switch (fC2ConfigStep)
	{
	case 0:
		fModemRequest.fTimeout = 2000;
		fC2ConfigStep = 2;
		return PostModemCommand(0x11a);
	case 2:
		if (fModemReply.fResult != noErr || (fModemReply.fResultCode != 0 && fModemReply.fResultCode != 4))
			return kModemErrNoResponse;
		fC2ConfigStep = 3;
		fModemRequest.fBytes[0] = '0';
		return PostModemCommand(0x133);
	case 3:
		if (fModemReply.fResult != noErr || (fModemReply.fResultCode != 0 && fModemReply.fResultCode != 4))
			return kModemErrNoResponse;
		fC2ConfigStep = 4;
		strncpy((char*) fModemRequest.fBytes, (const char*) fLocalId, 0x15);
		fModemRequest.fBytes[0x14] = 0;
		return PostModemCommand(0x12c);
	case 4:
		{
			if (fModemReply.fResult != noErr || (fModemReply.fResultCode != 0 && fModemReply.fResultCode != 4))
				return kModemErrNoResponse;
			fC2ConfigStep = 5;
			NewtonErr err = C2DisFromCapabilities(fC2LocalDIS, fC2ModemDIS);
			memmove(fModemRequest.fBytes, fC2LocalDIS.fParms, sizeof(fC2LocalDIS.fParms));
			if (err != noErr)
				return err;
			return PostModemCommand(0x121);
		}
	case 5:
		if (fModemReply.fResult != noErr || fModemReply.fResultCode != 0)
			return kModemErrNoResponse;
		fC2ConfigStep = 6;
		fModemRequest.fBytes[0] = fDirection.fSend ? '0' : '1';
		return PostModemCommand(0x11f);
	case 6:
		if (fModemReply.fResult != noErr || fModemReply.fResultCode != 0)
			return kModemErrNoResponse;
		fC2ConfigStep = 7;
		fModemRequest.fBytes[0] = '0';
		return PostModemCommand(0x11b);
	case 7:
		if (fModemReply.fResult != noErr || fModemReply.fResultCode != 0)
			return kModemErrNoResponse;
		*done = 1;
		return noErr;
	}
	return kFaxToolErrProtocolError;
}


// ROM 0x000b3d0c C2DisFromCapabilities__8TFaxToolFR13FaxClass2FDIST1
// Our +FDIS parameters from our capabilities: resolution, the fastest rate
// both we and the modem have, width, length, no 2-D, no ECM, no binary
// file transfer, and the scan time.  BUG: the last rate test (V.27 ter at
// 2400) answers '0' either way.
NewtonErr
TFaxTool::C2DisFromCapabilities(FaxClass2FDIS& dis, FaxClass2FDIS& modem)
{
	ULong local = fLocalCaps.Word();
	switch ((local & kT30Length) >> kT30LengthShift)
	{
	case 1:		dis.fParms[3] = '1'; break;
	case 2:		dis.fParms[3] = '2'; break;
	default:	dis.fParms[3] = '0'; break;
	}
	switch ((local & kT30Width) >> kT30WidthShift)
	{
	case 1:		dis.fParms[2] = '1'; break;
	case 2:		dis.fParms[2] = '2'; break;
	default:	dis.fParms[2] = '0'; break;
	}
	dis.fParms[0] = (local & kT30FineResolution) ? '1' : '0';
	UChar rates = modem.fParms[1];
	ULong mods = fModemCaps.fTransmitDataMods;
	if ((rates & 0x10) && (mods & 0x800))
		dis.fParms[1] = '5';
	else if ((rates & 0x20) && (mods & 0x200))
		dis.fParms[1] = '4';
	else if ((rates & 8) && (mods & 0xc0))
		dis.fParms[1] = '3';
	else if ((rates & 4) && (mods & 0x18))
		dis.fParms[1] = '2';
	else if ((rates & 2) && (mods & 4))
		dis.fParms[1] = '1';
	else if ((rates & 1) && (mods & 2))
		dis.fParms[1] = '0';
	else
		dis.fParms[1] = '0';
	dis.fParms[4] = '0';
	dis.fParms[5] = '0';
	dis.fParms[6] = '0';
	if (fMinScanLineTime >= 40)
		dis.fParms[7] = '7';
	else if (fMinScanLineTime >= 20)
		dis.fParms[7] = '5';
	else if (fMinScanLineTime >= 10)
		dis.fParms[7] = '3';
	else if (fMinScanLineTime >= 5)
		dis.fParms[7] = '1';
	else
		dis.fParms[7] = '0';
	return noErr;
}


// ROM 0x000b3e70 C2RecvCfgMdm__8TFaxToolFRUl
void
TFaxTool::C2RecvCfgMdm(ULong& state)
{
	UChar done;
	if (C2ConfigModem(&done) != noErr)
		StartAbort(kModemErrNoResponse);
	else if (done)
	{
		state = 5;
		fFaxFlags &= ~kFaxFlagCaller;
		StartPhaseA();
	}
}


// ROM 0x000b3ed8 C2RecvCopyQualityCheck__8TFaxToolFRUl
// The page received and the client told ('feom'): the page's quality
// judged, and +FPS set if the modem's differs; the next +FDR.
void
TFaxTool::C2RecvCopyQualityCheck(ULong& state)
{
	TCMOFaxEndMessage* option = (TCMOFaxEndMessage*) fOptionsInfo.fCurOptPtr;
	if (!option->fPageAccepted)
		C2AbortSession(state, kFaxToolErrProtocolError);
	((TCMOFaxEndMessage*) fOptionsInfo.fCurOptPtr)->fLastPage = (fC2PageStatus == '2');
	((TOptionExtended*) fOptionsInfo.fCurOptPtr)->SetExtendedResult(noErr);
	UChar status;
	switch (CopyQualityResponse())
	{
	case 0x4c:
	case 0x4d:
		status = '2';
		break;
	case 0xcc:
	case 0xcd:
		status = '3';
		break;
	default:
		status = '1';
		break;
	}
	if (fC2PTS.fStatus != status)
	{
		fModemRequest.fTimeout = 60000;
		fModemRequest.fBytes[0] = status;
		if (PostModemCommand(0x134) == noErr)
			state = 0x1a;
		else
			C2AbortSession(state, kFaxToolErrProtocolError);
		return;
	}
	C2RecvFDR_Cmd(state);
	if (fC2PageStatus == '2' || fC2PageStatus == '6')
		state = 0x1e;
}


// ROM 0x000b3fe4 C2SetSessionParameters__8TFaxToolFv
// The session the modem negotiated (+FDCS): resolution, width, speed.
void
TFaxTool::C2SetSessionParameters()
{
	switch (fC2RemoteDIS.fParms[0])
	{
	case '0':	fVerticalRes = 0x62; break;
	case '1':	fVerticalRes = 0xc4; break;
	default:	return;
	}
	fHorizontalRes = 0xcc;
	switch (fC2RemoteDIS.fParms[2])
	{
	case '0':
		fBytesPerLine = 216;
		fLineBufferSize = 0x438;
		break;
	case '1':
		fBytesPerLine = 256;
		fLineBufferSize = 0x500;
		break;
	case '2':
		fBytesPerLine = 304;
		fLineBufferSize = 0x5f0;
		break;
	default:
		return;
	}
	switch (fC2RemoteDIS.fParms[1])
	{
	case '0':	fBitRate = 2400; break;
	case '1':	fBitRate = 4800; break;
	case '2':	fBitRate = 7200; break;
	case '3':	fBitRate = 9600; break;
	case '4':	fBitRate = 12000; break;
	case '5':	fBitRate = 14400; break;
	default:	return;
	}
	fReceiveTimeout = (fReceiveBufferSize * 8000) / fBitRate + 5000;
}


// ROM 0x000b40ec C2TransCfgMdm__8TFaxToolFRUl
void
TFaxTool::C2TransCfgMdm(ULong& state)
{
	UChar done;
	if (C2ConfigModem(&done) != noErr)
		StartAbort(kModemErrNoResponse);
	else if (done)
	{
		state = 4;
		fFaxFlags |= kFaxFlagCaller;
		StartPhaseA();
	}
}


// ROM 0x000b4324 (unnamed) - C2ParseHex
// A hexadecimal number (either case), the pointer moved past it.
static ULong
C2ParseHex(UChar** text)
{
	ULong value = 0;
	UChar* p = *text;
	for ( ; ; p++)
	{
		int c = *p;
		if (c >= '0' && c <= '9')
			value = c + (value << 4) - '0';
		else if ((c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))
		{
			if (c >= 'a' && c <= 'z')
				c -= 0x20;
			value = c + (value << 4) - 0x37;
		}
		else
			break;
	}
	*text = p;
	return value;
}


static void
SkipSpaces(UChar*& p)
{
	while (*p == ' ')
		p++;
}


// ROM 0x000b43a4 (unnamed) - C2ParseParameter
// One parameter of a Class 2 answer ("+FDIS: 1,5,0,..." or its range form
// "(0,1),(0-5),..."), as a set of bits: each number n (up to 32) or range
// n-m sets its bits; a list in parentheses (or with list true) goes on over
// its commas.  kModemErrNotSupported for anything else.
static NewtonErr
C2ParseParameter(UChar** text, ULong* value, UChar list)
{
	NewtonErr err = noErr;
	UChar* p = *text;
	Boolean paren = false;
	if (*p == '+')
	{
		for ( ; ; )
		{
			if (*++p == 0)
				goto open;
			if (*p == '=')
				break;
		}
		p++;
	}
	SkipSpaces(p);
	if (*p == '(')
	{
		p++;
		paren = true;
	}
open:
	*value = 0;
	for ( ; ; )
	{
		int c = *p;
		if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')))
			goto bad;
		Long low = (Long) C2ParseHex(&p);
		if (low < 0 || low > 0x20)
			goto bad;
		SkipSpaces(p);
		if (*p == '-')
		{
			p++;
			SkipSpaces(p);
			Long high = (Long) C2ParseHex(&p);
			if (high < low || high > 0x20)
				goto bad;
			SkipSpaces(p);
			// (a shift of 32 is nothing, as the ARM's is)
			for (Long bit = low; bit <= high; bit++)
				*value |= (bit < 32) ? (1UL << bit) : 0;
		}
		else
			*value |= (low < 32) ? (1UL << low) : 0;
		c = *p;
		if (c == 0)
			goto done;
		if (c == ')')
		{
			p++;
			if (paren)
				goto done;
			goto bad;
		}
		if (c != ',')
			goto bad;
		if (!paren && !list)
			goto done;
		p++;
	}
bad:
	err = kModemErrNotSupported;
done:
	*text = p;
	return err;
}


// ROM 0x000b4154 C2ParseDISResponse__8TFaxToolFPUcR13FaxClass2FDIS
// +FDIS=? answered: the first seven parameters, each as its set of bits.
// BUG: the eighth (the scan time) is never parsed.
NewtonErr
TFaxTool::C2ParseDISResponse(UChar* response, FaxClass2FDIS& dis)
{
	ULong value;
	for (int i = 0; i < 6; i++)
	{
		dis.fParms[i] = 0;
		if (C2ParseParameter(&response, &value, false) == noErr)
			dis.fParms[i] = (UChar) value;
		if (*response == 0)
			return kModemErrNotSupported;
		response++;
	}
	dis.fParms[6] = 0;
	if (C2ParseParameter(&response, &value, false) == noErr)
		dis.fParms[6] = (UChar) value;
	return noErr;
}


// ROM 0x000b45e8 C2ValidateDCS__8TFaxToolFR13FaxClass2FDIST1
// Whether the session asks for no more than we offered, parameter by
// parameter (the first seven).
Boolean
TFaxTool::C2ValidateDCS(FaxClass2FDIS& dcs, FaxClass2FDIS& dis)
{
	for (int i = 0; i < 7; i++)
		if (dcs.fParms[i] > dis.fParms[i])
			return false;
	return true;
}


// ROM 0x000b4648 C2TransFDT_Cmd__8TFaxToolFRUl
// +FDT: a page to send.
void
TFaxTool::C2TransFDT_Cmd(ULong& state)
{
	fModemRequest.fTimeout = 60000;
	if (PostModemCommand(0x127) == noErr)
		state = 0xd;
	else
		StartAbort(kModemErrNoResponse);
}


// ROM 0x000b469c C2TransFET_Cmd__8TFaxToolFRUl
// +FET: the page ended - EOP (2) after the last, MPS (0) otherwise.
void
TFaxTool::C2TransFET_Cmd(ULong& state)
{
	fModemRequest.fBytes[0] = (fFaxFlags & kFaxFlagLastPage) ? '2' : '0';
	fModemRequest.fTimeout = 20000;
	if (PostModemCommand(0x12a) == noErr)
		state = 0xf;
	else
		StartAbort(kModemErrNoResponse);
}


// ROM 0x000b4704 C2TransFET_Rsp__8TFaxToolFRUl
// +FET answered: the client's 'feom' answered (kFaxToolErrTransmissionFailed
// if the page status was a bad one); after a good last page the session is
// ended, otherwise the next page's +FDT.
void
TFaxTool::C2TransFET_Rsp(ULong& state)
{
	NewtonErr result = noErr;
	if (fC2PTS.fStatus == '2' || fC2PTS.fStatus == '4')
		result = kFaxToolErrTransmissionFailed;
	EndPageComplete(result);
	if (result == noErr && (fFaxFlags & kFaxFlagLastPage))
		C2AbortSession(state, kFaxToolErrProtocolError);
	else
		C2TransFDT_Cmd(state);
}


// ROM 0x000b4778 C2GetModemRsp__8TFaxToolFUlRUl
// The modem's next answer awaited (for timeout milliseconds).
void
TFaxTool::C2GetModemRsp(ULong timeout, ULong& state)
{
	fModemRequest.fTimeout = timeout;
	if (PostModemCommand(kModemCtlGetResult) != noErr)
		StartAbort(kModemErrNoResponse);
}


// ROM 0x000b47b0 C2TransFDT_Rsp__8TFaxToolFRUl
void
TFaxTool::C2TransFDT_Rsp(ULong& state)
{
	StartPhaseC();
	state = 0xe;
}


// ROM 0x000b47d0 C2PhaseBProcessOptions__8TFaxToolFv
// As PhaseBProcessOptions, from the session the modem negotiated.  (A
// parameter it does not know ends the work there, 'frid' included.)
void
TFaxTool::C2PhaseBProcessOptions()
{
	if (fOptionsInfo.fOptions == nil)
		return;
	TOptionIterator iter(fOptionsInfo.fOptions);
	TOption* option = iter.FindOption(kCMOFaxSessionInfo);
	if (option != nil)
	{
		if (option->GetOpCode() != opGetCurrent)
			option->SetOpCodeResult(opFailure);
		else
		{
			TCMOFaxSessionInfo* info = (TCMOFaxSessionInfo*) option;
			switch (fC2RemoteDIS.fParms[0])
			{
			case '0':
				info->fHorizontalRes = 0xcc;
				info->fVerticalRes = 0x62;
				break;
			case '1':
				info->fHorizontalRes = 0xcc;
				info->fVerticalRes = 0xc4;
				break;
			default:
				return;
			}
			switch (fC2RemoteDIS.fParms[3])
			{
			case '0':	info->fLength = 0; break;
			case '1':	info->fLength = 1; break;
			case '2':	info->fLength = 0; break;
			default:	return;
			}
			info->fWidth = 0x6c0;
			switch (fC2RemoteDIS.fParms[7])
			{
			case '0':	fMinScanTime = 0; break;
			case '1':	fMinScanTime = 5; break;
			case '2':
			case '3':	fMinScanTime = 10; break;
			case '4':
			case '5':	fMinScanTime = 20; break;
			case '6':
			case '7':	fMinScanTime = 40; break;
			default:
				fMinScanTime = 0;
				return;
			}
			switch (fC2RemoteDIS.fParms[1])
			{
			case '0':	fBitRate = 2400; break;
			case '1':	fBitRate = 4800; break;
			case '2':	fBitRate = 7200; break;
			case '3':	fBitRate = 9600; break;
			case '4':	fBitRate = 12000; break;
			case '5':	fBitRate = 14400; break;
			default:	return;
			}
			TCMOFaxSessionInfo defaults;
			if (option->Length() >= defaults.Length())
				info->fBitRate = fBitRate;
			option->SetOpCodeResult(opSuccess);
			option->SetProcessed();
		}
	}
	option = iter.FindOption(kCMOFaxRemoteId);
	if (option != nil)
	{
		if (option->GetOpCode() == opGetCurrent)
		{
			option->CopyDataFrom(&fRemoteId);
			option->SetOpCodeResult(opSuccess);
		}
		else if (option->GetOpCode() == opGetDefault)
		{
			TCMOFaxRemoteId defaults;
			option->CopyDataFrom(&defaults);
			option->SetOpCodeResult(opSuccess);
		}
		else
			option->SetOpCodeResult(opFailure);
		option->SetProcessed();
	}
}


// ROM 0x000b4a20 C2TransFPTS_Rsp__8TFaxToolFRUl
void
TFaxTool::C2TransFPTS_Rsp(ULong& state)
{
	C2GetModemRsp(60000, state);
}


/*------------------------------------------------------------------------------
	Class 2.0.
------------------------------------------------------------------------------*/

// ROM 0x000b1fb8 C20StateUpdate__8TFaxToolFUl
void
TFaxTool::C20StateUpdate(ULong event)
{
	ULong state = fC2State;
	UChar done;
	switch (fC2State)
	{
	case 1:
		if (event == 1 || event == 2)
		{
			state = (event == 1) ? 0x34 : 0x35;
			fC2ConfigStep = 0;
			if (C20ConfigModem(&done) != noErr)
				StartAbort(kModemErrNoResponse);
		}
		else
			StartAbort(kFaxToolErrProtocolError);
		break;

	case 0x34:
		C20TransCfgMdm(state);
		break;

	case 0x35:
		C20RecvCfgMdm(state);
		break;

	case 0x36:
		if (event == 3)
		{
			state = 0x3c;
			C20GetModemRsp(60000, state);
		}
		else if (event == 4)
			state = 1;
		else
			C20AbortSession(state, kFaxToolErrProtocolError);
		break;

	case 0x37:
		switch (event)
		{
		case 0x3f:
			C20FHS_Rsp(state);
			break;
		case 3:
			C20GetModemRsp(60000, state);
			break;
		case 4:
			state = 1;
			break;
		case 0x14:
			if (C2ValidateDCS(fC2RemoteDIS, fC2LocalDIS))
				C20RecvFDR_Cmd(state);
			else
				C20AbortSession(state, kFaxToolErrProtocolError);
			break;
		case 0x40:
			if (fToolState & kToolStateConnecting)
			{
				C2SetSessionParameters();
				C2PhaseBProcessOptions();
			}
			C20GetModemRsp(20000, state);
			break;
		case 0x3c:
		case 0x44:
		case 0x48:
			C20GetModemRsp(20000, state);
			break;
		case 0x20:
			C20AbortSession(state, kModemErrNoResponse);
			break;
		default:
			C20AbortSession(state, kFaxToolErrProtocolError);
			break;
		}
		break;

	case 0x3c:
		switch (event)
		{
		case 0x3c:
		case 0x41:
		case 0x45:
		case 0x48:
			C20GetModemRsp(60000, state);
			break;
		case 0x3f:
			C20FHS_Rsp(state);
			break;
		case 0x14:
			C20TransFDT_Cmd(state);
			break;
		case 0x17:
		case 0x18:
		case 0x19:
		case 0x1a:
			state = 1;
			break;
		case 0x20:
			C20AbortSession(state, kModemErrNoResponse);
			break;
		default:
			C20AbortSession(state, kFaxToolErrProtocolError);
			break;
		}
		break;

	case 0x3d:
		if (event == 5)
			C20TransFDT_Rsp(state);
		else
			C20AbortSession(state, kFaxToolErrProtocolError);
		break;

	case 0x3e:
	case 0x3f:
		switch (event)
		{
		case 5:
			if (fC2State == 0x3f)
			{
				state = 0x3e;
				break;
			}
			C20AbortSession(state, kFaxToolErrProtocolError);
			break;
		case 0x15:
			if (fC2State == 0x3f)
				state = 0x3d;
			else
				C20TransFDT_Rsp(state);
			break;
		case 0x3f:
			C20FHS_Rsp(state);
			break;
		case 0x40:
			if (fToolState & kToolStateConnecting)
			{
				C2PhaseBProcessOptions();
				ConnectComplete(noErr);
			}
			C20GetModemRsp(60000, state);
			break;
		case 0x20:
			C20AbortSession(state, kModemErrNoResponse);
			break;
		default:
			C20AbortSession(state, kFaxToolErrProtocolError);
			break;
		}
		break;

	case 0x40:
		switch (event)
		{
		case 9:
			SendEOMCont();
			break;
		case 6:
			DoEndPage();
			break;
		case 7:
			C20GetModemRsp(15000, state);
			break;
		case 8:
			C20TransPhaseCSendEOM(state);
			break;
		case 0x14:
			EndPageComplete(noErr);
			C20TransFDT_Cmd(state);
			break;
		case 0x18:
			EndPageComplete(kFaxToolErrTransmissionFailed);
			C20TransFDT_Cmd(state);
			break;
		case 0x3f:
			C20FHS_Rsp(state);
			break;
		default:
			C20AbortSession(state, kFaxToolErrProtocolError);
			break;
		}
		break;

	case 0x47:
		switch (event)
		{
		case 0x3c:
		case 0x40:
		case 0x44:
		case 0x48:
			C20GetModemRsp(20000, state);
			break;
		case 0x15:
			C20RecvFDR_Rsp(state);
			break;
		case 0x3f:
			C20FHS_Rsp(state);
			break;
		case 0x20:
			C20AbortSession(state, kModemErrNoResponse);
			break;
		default:
			C20AbortSession(state, kFaxToolErrProtocolError);
			break;
		}
		break;

	case 0x48:
		if (event == 7)
		{
			state = 0x49;
			C20GetModemRsp(10000, state);
		}
		else
			C20AbortSession(state, kFaxToolErrProtocolError);
		break;

	case 0x49:
	case 0x4b:
		switch (event)
		{
		case 0x3f:
			C20FHS_Rsp(state);
			break;
		case 6:
			if (fC2State == 0x49)
			{
				state = 0x4b;
				break;
			}
			C20AbortSession(state, kFaxToolErrProtocolError);
			break;
		case 0x14:
		case 0x18:
			if (fC2State == 0x49)
				state = 0x4a;
			else
				C20RecvCopyQualityCheck(state);
			break;
		case 0x4a:
		case 0x4b:
			C20GetModemRsp(10000, state);
			break;
		case 0x20:
			C20AbortSession(state, kModemErrNoResponse);
			break;
		default:
			C20AbortSession(state, kFaxToolErrProtocolError);
			break;
		}
		break;

	case 0x4a:
		if (event == 6)
			C20RecvCopyQualityCheck(state);
		else
			C20AbortSession(state, kFaxToolErrProtocolError);
		break;

	case 0x4c:
		switch (event)
		{
		case 0x14:
			C20RecvFDR_Cmd(state);
			break;
		case 0x3f:
			C20FHS_Rsp(state);
			break;
		case 0x20:
			C20AbortSession(state, kModemErrNoResponse);
			break;
		default:
			C20AbortSession(state, kFaxToolErrProtocolError);
			break;
		}
		break;

	case 0x50:
		if (event == 0x3f)
			C20FHS_Rsp(state);
		else if (event == 0x20)
			C20AbortSession(state, kModemErrNoResponse);
		else
			C20AbortSession(state, kFaxToolErrProtocolError);
		break;

	case 0x51:
		if (event == 0x14)
		{
			state = 1;
			if (StartAbort(kFaxToolErrProtocolError) != noErr)
				TerminateConnection();
		}
		else if (event == 0x20)
		{
			StartAbort(kModemErrNoResponse);
			state = 1;
		}
		else
			C20GetModemRsp((event == 0x3f) ? 4000 : 3000, state);
		break;

	case 0x52:
		if (event == 0x14)
			C20FHS_OK_Rsp(state);
		else
		{
			StartAbort(kFaxToolErrProtocolError);
			state = 1;
		}
		break;

	default:
		C20AbortSession(state, kFaxToolErrProtocolError);
		break;
	}
	fC2State = state;
}


// ROM 0x000b2948 C20RecvFDR_Rsp__8TFaxToolFRUl
void
TFaxTool::C20RecvFDR_Rsp(ULong& state)
{
	FreeReceiveBuffers();
	if (AllocateReceiveBuffers() == noErr)
	{
		StartPhaseC();
		state = 0x48;
	}
	else
		C20AbortSession(state, kFaxToolErrProtocolError);
}


// ROM 0x000b299c C20AbortSession__8TFaxToolFRUll
// +FKS and the tool aborted.  BUG: error is not used, as in C2AbortSession.
void
TFaxTool::C20AbortSession(ULong& state, NewtonErr error)
{
	fModemRequest.fTimeout = 10000;
	if (PostModemCommand(0x13e) == noErr)
		state = 0x51;
	StartAbort(kModemErrNoResponse);
}


// ROM 0x000b29ec C20ConfigModem__8TFaxToolFPUc
// As C2ConfigModem, in Class 2.0's commands: +FCLASS=2.0, +FCR, +FLI, +FCC,
// +FLP, +FAP, +FNR.
NewtonErr
TFaxTool::C20ConfigModem(UChar* done)
{
	*done = 0;
	switch (fC2ConfigStep)
	{
	case 0:
		fModemRequest.fTimeout = 2000;
		fC2ConfigStep = 2;
		fModemRequest.fBytes[0] = '0';
		return PostModemCommand(0x15c);
	case 2:
		if (fModemReply.fResult != noErr || (fModemReply.fResultCode != 0 && fModemReply.fResultCode != 4))
			return kModemErrNoResponse;
		fC2ConfigStep = 3;
		fModemRequest.fBytes[0] = '0';
		return PostModemCommand(0x160);
	case 3:
		if (fModemReply.fResult != noErr || (fModemReply.fResultCode != 0 && fModemReply.fResultCode != 4))
			return kModemErrNoResponse;
		fC2ConfigStep = 4;
		strncpy((char*) fModemRequest.fBytes, (const char*) fLocalId, 0x15);
		fModemRequest.fBytes[0x14] = 0;
		return PostModemCommand(0x147);
	case 4:
		{
			if (fModemReply.fResult != noErr || (fModemReply.fResultCode != 0 && fModemReply.fResultCode != 4))
				return kModemErrNoResponse;
			fC2ConfigStep = 5;
			NewtonErr err = C2DisFromCapabilities(fC2LocalDIS, fC2ModemDIS);
			memmove(fModemRequest.fBytes, fC2LocalDIS.fParms, sizeof(fC2LocalDIS.fParms));
			if (err != noErr)
				return err;
			return PostModemCommand(0x143);
		}
	case 5:
		if (fModemReply.fResult != noErr || fModemReply.fResultCode != 0)
			return kModemErrNoResponse;
		fC2ConfigStep = 6;
		fModemRequest.fBytes[0] = fDirection.fSend ? '0' : '1';
		return PostModemCommand(0x158);
	case 6:
		if (fModemReply.fResult != noErr || fModemReply.fResultCode != 0)
			return kModemErrNoResponse;
		fC2ConfigStep = 7;
		fModemRequest.fBytes[0] = fDirection.fReceive ? '1' : '0';
		return PostModemCommand(0x155);
	case 7:
		if (fModemReply.fResult != noErr || fModemReply.fResultCode != 0)
			return kModemErrNoResponse;
		fC2ConfigStep = 8;
		fModemRequest.fBytes[0] = '1';
		fModemRequest.fBytes[1] = '1';
		fModemRequest.fBytes[2] = '1';
		fModemRequest.fBytes[3] = '0';
		return PostModemCommand(0x14d);
	case 8:
		if (fModemReply.fResult != noErr || fModemReply.fResultCode != 0)
			return kModemErrNoResponse;
		*done = 1;
		return noErr;
	}
	return kFaxToolErrProtocolError;
}


// ROM 0x000b2c3c C20RecvCfgMdm__8TFaxToolFRUl
void
TFaxTool::C20RecvCfgMdm(ULong& state)
{
	UChar done;
	if (C20ConfigModem(&done) != noErr)
		StartAbort(kModemErrNoResponse);
	else if (done)
	{
		state = 0x37;
		fFaxFlags &= ~kFaxFlagCaller;
		StartPhaseA();
	}
}


// ROM 0x000b2ca4 C20RecvCopyQualityCheck__8TFaxToolFRUl
// As C2RecvCopyQualityCheck: +FPS set if it differs, the next +FDR.
void
TFaxTool::C20RecvCopyQualityCheck(ULong& state)
{
	TCMOFaxEndMessage* option = (TCMOFaxEndMessage*) fOptionsInfo.fCurOptPtr;
	if (!option->fPageAccepted)
		C20AbortSession(state, kFaxToolErrProtocolError);
	((TCMOFaxEndMessage*) fOptionsInfo.fCurOptPtr)->fLastPage = (fC2PageStatus == '2');
	((TOptionExtended*) fOptionsInfo.fCurOptPtr)->SetExtendedResult(noErr);
	UChar status;
	switch (CopyQualityResponse())
	{
	case 0x4c:
	case 0x4d:
		status = '2';
		break;
	case 0xcc:
	case 0xcd:
		status = '3';
		break;
	default:
		status = '1';
		break;
	}
	if (fC2PTS.fStatus != status)
	{
		fModemRequest.fTimeout = 60000;
		fModemRequest.fBytes[0] = status;
		if (PostModemCommand(0x14f) == noErr)
			state = 0x4c;
		else
			C20AbortSession(state, kFaxToolErrProtocolError);
		return;
	}
	C20RecvFDR_Cmd(state);
	if (fC2PageStatus == '2' || fC2PageStatus == '6')
		state = 0x50;
}


// ROM 0x000b2db4 C20TransCfgMdm__8TFaxToolFRUl
void
TFaxTool::C20TransCfgMdm(ULong& state)
{
	UChar done;
	if (C20ConfigModem(&done) != noErr)
		StartAbort(kModemErrNoResponse);
	else if (done)
	{
		state = 0x36;
		fFaxFlags |= kFaxFlagCaller;
		StartPhaseA();
	}
}


// ROM 0x000b2e1c C20TransFDT_Cmd__8TFaxToolFRUl
void
TFaxTool::C20TransFDT_Cmd(ULong& state)
{
	fModemRequest.fTimeout = 60000;
	if (PostModemCommand(0x13c) == noErr)
		state = 0x3f;
	else
		StartAbort(kModemErrNoResponse);
}


// ROM 0x000b2e6c C20GetModemRsp__8TFaxToolFUlRUl
void
TFaxTool::C20GetModemRsp(ULong timeout, ULong& state)
{
	fModemRequest.fTimeout = timeout;
	if (PostModemCommand(kModemCtlGetResult) != noErr)
		StartAbort(kModemErrNoResponse);
}


// ROM 0x000b2ea4 C20TransFDT_Rsp__8TFaxToolFRUl
void
TFaxTool::C20TransFDT_Rsp(ULong& state)
{
	StartPhaseC();
	state = 0x40;
}


// ROM 0x000b2ec4 C20TransPhaseCSendEOM__8TFaxToolFRUl
// Class 2.0 ends a page's data with DLE and ',' (another page) or '.' (the
// last): the modem tool's framing option set so.
void
TFaxTool::C20TransPhaseCSendEOM(ULong& state)
{
	TCMOFramingParms framing;
	framing = fFraming;
	framing.eomChar = (fFaxFlags & kFaxFlagLastPage) ? '.' : ',';
	if (fModemOptions.RemoveAllOptions() == noErr)
	{
		framing.SetOpCode(opSetRequired);
		if (fModemOptions.InsertOptionAt(fModemOptions.GetArrayCount(), &framing) == noErr)
		{
			fOptionRequest.fRequestOpCode = 0x500;
			fOptionRequest.fOptions = &fModemOptions;
			fOptionRequest.fOutside = false;
			if (fModemPort.SendRPC(&fModemMsg, &fOptionRequest, sizeof(fOptionRequest), &fModemReply, sizeof(fModemReply), 0, nil, kCommToolRequestTypeControl) == noErr)
			{
				fC2Framing = 1;
				return;
			}
		}
	}
	C20AbortSession(state, kFaxToolErrProtocolError);
}


// ROM 0x000b2fec C20FHS_Rsp__8TFaxToolFRUl
// +FHS: the OK that follows it awaited.
void
TFaxTool::C20FHS_Rsp(ULong& state)
{
	C20GetModemRsp(5000, state);
	state = 0x52;
}


// ROM 0x000b3018 C20FHS_OK_Rsp__8TFaxToolFRUl
// As C2FHNG_OK_Rsp.
void
TFaxTool::C20FHS_OK_Rsp(ULong& state)
{
	NewtonErr result = noErr;
	UChar hangUp = fC2HangUpStatus;
	if (hangUp != 0 || fC2PTS.fStatus == '2' || fC2PTS.fStatus == '4')
		result = kFaxToolErrTransmissionFailed;
	if (fToolState & kToolStateListenMode)
		StartAbort(kFaxToolErrProtocolError);
	else if (fToolState & kToolStateConnecting)
		ConnectComplete(result);
	else if (fClientBuffer != nil)
	{
		GetComplete(kFaxToolErrProtocolError, fReceiveState == 3, fClientBuffer->GetSize());
		StartAbort(kFaxToolErrProtocolError);
	}
	else if (fOptionsInfo.fCurOptPtr != nil && fOptionsInfo.fCurOptPtr->Label() == kCMOFaxStartPage)
		StartPageComplete(result);
	else if (fOptionsInfo.fCurOptPtr != nil && fOptionsInfo.fCurOptPtr->Label() == kCMOFaxEndMessage)
		EndPageComplete(result);
	else if (hangUp != 0)
		StartAbort(kFaxToolErrProtocolError);
	else
		TerminateConnection();
	state = 1;
}


// ROM 0x000b3158 C20RecvFDR_Cmd__8TFaxToolFRUl
// As C2RecvFDR_Cmd, Class 2.0's +FDR.
void
TFaxTool::C20RecvFDR_Cmd(ULong& state)
{
	fFaxFlags |= kFaxFlagReceivingLines;
	fReceiveState = 1;
	fDecodeBufIndex = 0;
	fReceiveBufs[1].fFull = false;
	fReceiveBufs[0].fFull = false;
	if (fToolState & kToolStateListenMode)
		ListenComplete(noErr);
	if ((fFaxFlags & kFaxFlagEndMessage) && !((TCMOFaxEndMessage*) fOptionsInfo.fCurOptPtr)->fLastPage)
	{
		fFaxFlags &= ~kFaxFlagEndMessage;
		((TOptionExtended*) fOptionsInfo.fCurOptPtr)->SetExtendedResult(noErr);
		ProcessOptionComplete(opSuccess);
	}
	fModemRequest.fTimeout = 60000;
	if (PostModemCommand(0x13d) == noErr)
	{
		FreeReceiveBuffers();
		if (AllocateReceiveBuffers() == noErr)
		{
			state = 0x47;
			return;
		}
	}
	StartAbort(kModemErrNoResponse);
}
