/*
	File:		comms/fax/FaxToolPhases.cpp

	Contains:	TFaxTool's T.30 phases over a Class 1 modem, A to E
				(FaxTool.h).

	Reconstructed from the MP2x00 US ROM (0x000b4e8c-0x000b9628); each
	function cites its origin.
*/

#include "FaxTool.h"
#include "CommErrors.h"
#include "NewtErrors.h"
#include "OSErrors.h"
#include "OptionArray.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"
#include "stores/LargeObjects.h"

#include <string.h>


// ROM 0x000b4e8c (unnamed) - ReverseBits
// Each byte's bits reversed (a Class 2 modem sends a page's data most
// significant bit first).
static void
ReverseBits(UChar* p, ULong count)
{
	for ( ; count != 0; count--, p++)
		*p = kFaxBitReverse[*p];
}


// A received frame's FIF as capabilities: five bytes cleared, then as much
// of the FIF as there is, up to eight.  (Bytes five to seven are left as
// they were when the FIF is shorter than eight.)
static void
CopyFIF(TT30Capabilities& caps, CBufferList& frame, const UChar* fif)
{
	memset(&caps, 0, 5);
	Long length = frame.GetSize() - 3;
	BlockMove(fif, &caps, length < 8 ? length : 8);
}


/*------------------------------------------------------------------------------
	Phase A: the call.
------------------------------------------------------------------------------*/

// ROM 0x000b4eb4 StartPhaseA__8TFaxToolFv
void
TFaxTool::StartPhaseA()
{
	ULong flags = fFaxFlags;
	if (fDirection.fSend)
		flags |= kFaxFlagSend | kFaxFlagTransmitter;
	else
		flags &= ~(kFaxFlagSend | kFaxFlagTransmitter);
	if (fDirection.fReceive)
		flags |= kFaxFlagReceive;
	else
		flags &= ~kFaxFlagReceive;
	flags &= ~(0x0e800000 | 0x00078000 | 0x000005f0);
	fFaxFlags = flags;
	fPhase = kFaxPhaseA;
	fRetries = 3;
	switch ((fLocalCaps.Word() & kT30Width) >> kT30WidthShift)
	{
	case 0:
		fBytesPerLine = 216;
		fLineBufferSize = 0x438;
		break;
	case 1:
		fBytesPerLine = 256;
		fLineBufferSize = 0x500;
		break;
	case 2:
		fBytesPerLine = 304;
		fLineBufferSize = 0x5f0;
		break;
	}
	if (flags & kFaxFlagSend)
	{
		NewtonErr err;
		if ((err = AllocateLineBuffers()) != noErr)
		{
			StartAbort(err);
			return;
		}
	}
	fReceiveBufIndex = 0;
	fDecodeBufIndex = 0;
	fSendBufIndex = 0;
	fLinesLeft = 0;
	fSendBufs[0].fFull = false;
	fSendBufs[1].fFull = false;
	fSendBufs[0].fInUse = false;
	fSendBufs[1].fInUse = false;
	fSendBufs[0].fCount = 0;
	fSendBufs[1].fCount = 0;
	fReceiveBufs[0].fFull = false;
	fReceiveBufs[1].fFull = false;
	fReceiveBufs[0].fInUse = false;
	fReceiveBufs[1].fInUse = false;
	fReceiveBufs[0].fCount = 0;
	fReceiveBufs[1].fCount = 0;
	fPages = 0;
	fConnectRequest.fOpCode = fControlOpCode;
	fConnectRequest.fOptions = fOptionsInfo.fOptions;
	fConnectRequest.fOutside = false;
	fPhaseAStep = 1;
	PhaseAModemReqComplete(noErr);
}


// ROM 0x000b8f6c PhaseAModemReqComplete__8TFaxToolFl
void
TFaxTool::PhaseAModemReqComplete(NewtonErr result)
{
	if (fToolState & kToolStateWantAbort)
		return;
	switch (fPhaseAStep)
	{
	case 1:
		PhaseAConnectModem(result);
		break;
	case 2:
		PhaseAAcceptModem(result);
		break;
	case 3:
		PhaseAComplete(result);
		break;
	}
}


// ROM 0x000b94b8 PhaseAConnectModem__8TFaxToolFl
// The client's connect or listen passed to the modem tool.
void
TFaxTool::PhaseAConnectModem(NewtonErr result)
{
	if (result == noErr)
	{
		fToolState |= kFaxToolStateModemRequest;
		result = fModemPort.SendRPC(&fModemMsg, &fConnectRequest, sizeof(fConnectRequest), &fModemReply, sizeof(fModemReply), 0, nil, kCommToolRequestTypeControl);
		if (result == noErr)
		{
			fPhaseAStep = (fConnectRequest.fOpCode == kCommToolListen) ? 2 : 3;
			return;
		}
	}
	PhaseAComplete(result);
}


// ROM 0x000b9570 PhaseAAcceptModem__8TFaxToolFl
// A call came to the listen: the modem tool told to accept it.
void
TFaxTool::PhaseAAcceptModem(NewtonErr result)
{
	if (result == noErr)
	{
		fToolState |= kFaxToolStateHangUp;
		fConnectRequest.fOpCode = kCommToolAccept;
		fConnectRequest.fOptions = nil;
		fConnectRequest.fOptionCount = 0;
		fConnectRequest.fData = nil;
		result = fModemPort.SendRPC(&fModemMsg, &fConnectRequest, sizeof(fConnectRequest), &fModemReply, sizeof(fModemReply), 0, nil, kCommToolRequestTypeControl);
		if (result == noErr)
		{
			fPhaseAStep = 3;
			return;
		}
	}
	StartAbort(result);
}


// ROM 0x000b5004 PhaseAComplete__8TFaxToolFl
void
TFaxTool::PhaseAComplete(NewtonErr result)
{
	if (fFaxClass == 4)
	{
		fPhase = kFaxPhaseClass2;
		if (result == noErr)
		{
			C2StateUpdate(3);
			return;
		}
		C2StateUpdate(4);
	}
	else if (fFaxClass == 8)
	{
		fPhase = kFaxPhaseClass2;
		if (result == noErr)
		{
			C20StateUpdate(3);
			return;
		}
		C20StateUpdate(4);
	}
	else if (result == noErr)
	{
		fToolState |= kFaxToolStateHangUp;
		StartPhaseB();
		return;
	}
	StartAbort(result);
}


/*------------------------------------------------------------------------------
	Phase B: the pre-message procedure.
------------------------------------------------------------------------------*/

// ROM 0x000b5ad0 StartPhaseB__8TFaxToolFv
// The caller waits for the called machine's CSI and DIS (35 seconds, T1);
// the called machine sends them.
void
TFaxTool::StartPhaseB()
{
	fPhase = kFaxPhaseB;
	fFaxFlags &= ~(kFaxFlagPostMessage | kFaxFlagNSF | kFaxFlagDCSReceived | kFaxFlagDISReceived);
	fPostMessage = 0;
	if (fFaxFlags & kFaxFlagCaller)
	{
		NewtonErr err;
		fPhaseBStep = 1;
		if ((err = PostRecvPkt(kModemCtlC1RecvHDLC, 1, &fFrameList, 3250, false)) != noErr
		||  (err = PostTimer(kFaxTimerDIS, 35, kSeconds)) != noErr)
			StartAbort(err);
	}
	else
	{
		fPhaseBStep = 2;
		fRetries = 5;
		PutInitialId(0);
	}
}


// ROM 0x000b6bc4 ReStartPhaseB__8TFaxToolFv
// The called machine waits for the next command.
void
TFaxTool::ReStartPhaseB()
{
	if (fFaxFlags & kFaxFlagCaller)
	{
		StartAbort(kFaxToolErrProtocolError);
		return;
	}
	fFaxFlags &= ~(0x00210000 | 0x000004e0);
	fPhase = kFaxPhaseB;
	fPhaseBStep = 3;
	GetCommand();
}


// ROM 0x000b50a8 PhaseBPktComplete__8TFaxToolFl
void
TFaxTool::PhaseBPktComplete(NewtonErr result)
{
	if (fToolState & (kFaxToolStateModemRequest | kToolStateWantAbort))
		return;
	switch (fPhaseBStep)
	{
	case 1:
		PhaseBGetInitialID(result);
		break;
	case 2:
		PhaseBPutInitialID(result);
		break;
	case 3:
		GetCommandComplete(result);
		break;
	case 4:
		PhaseBPutCommandToRcv(result);
		break;
	case 6:
		GetTrainingCheckComplete(result);
		break;
	case 7:
		PhaseBRespondToFTT(result);
		break;
	case 8:
		PutTrainingCheckComplete(result);
		break;
	case 9:
		PhaseBGetResponse(result);
		break;
	case 0xb:
		PhaseBWaitForSignalGone(result);
		break;
	case 0xe:
		PhaseBComplete(result);
		break;
	}
}


// ROM 0x000b5124 PhaseBGetInitialID__8TFaxToolFl
// A frame from the called machine; no carrier, a carrier of the wrong kind
// or a bad frame and it is listened for again.
void
TFaxTool::PhaseBGetInitialID(NewtonErr result)
{
	switch (result)
	{
	case kModemErrNoCarrier:
	case kModemErrRcvPktFlagsTimeOut:
	case kModemErrNoFaxCarrier:
	case kModemErrCommandFailure:
	case kSerErr_CRCError:
		if ((result = PostRecvPkt(kModemCtlC1RecvHDLC, 1, &fFrameList, 3250, false)) == noErr)
			return;
		break;
	case kCommErrRequestCanceled:
		return;
	case noErr:
		PhaseBProcessInitialID();
		return;
	}
	StartAbort(result);
}


// ROM 0x000b51d4 PhaseBProcessInitialID__8TFaxToolFv
// NSF, CSI and DIS; at the final frame the caller answers - TSI and DCS if
// it sends and the called machine receives, or CIG and DTC the other way.
void
TFaxTool::PhaseBProcessInitialID()
{
	UChar fcf = fFrame[2];
	if (fcf == 0x20)
		fFaxFlags |= kFaxFlagNSF;
	else if (fcf == 0x40)
		GetIdentification(fFrame + 3, fRemoteId.fId, fFrameList.GetSize() - 3);
	else if (fcf == 0x80)
	{
		fFaxFlags |= kFaxFlagDISReceived;
		TT30Capabilities dis;
		CopyFIF(dis, fFrameList, fFrame + 3);
		if (CompatibleRemoteRcvr(dis) && (fFaxFlags & kFaxFlagSend))
		{
			BuildDCS(dis, fSessionCaps);
			fFaxFlags |= kFaxFlagTransmitter;
		}
		else if (CompatibleRemoteXmtr(dis) && (fFaxFlags & kFaxFlagReceive))
			fFaxFlags &= ~kFaxFlagTransmitter;
	}

	if (fFrame[1] == 0x13)
	{
		KillTimer();
		if (fFaxFlags & kFaxFlagDISReceived)
			fPhaseBStep = (fFaxFlags & kFaxFlagTransmitter) ? 4 : 5;
		if (fPhaseBStep == 4)
		{
			fToolState |= kFaxToolStatePhaseE;
			fRetries = 3;
			fTrainTries = 1;
			fSpeedTries = 1;
			PutCommandToRcv(0x6e);
			return;
		}
		if (fPhaseBStep == 5)
		{
			fToolState |= kFaxToolStatePhaseE;
			fRetries = 3;
			fTrainTries = 1;
			PutCommandToXmit(10);
			return;
		}
	}
	else if (fPhaseBStep == 1)
	{
		NewtonErr err;
		if ((err = PostRecvPkt(kModemCtlC1RecvHDLC, 1, &fFrameList, 3250, false)) != noErr)
			StartAbort(err);
		return;
	}
	StartAbort(kFaxToolErrIncompatibleRemoteUnit);
}


// ROM 0x000b5408 PhaseBPutInitialID__8TFaxToolFl
// The called machine's CSI sent: its DIS next (with the minimum scan line
// time and the fastest rate the modem receives); the DIS sent, the
// response awaited.
void
TFaxTool::PhaseBPutInitialID(NewtonErr result)
{
	if (fToolState & kToolStateWantAbort)
		return;
	if (result != noErr)
	{
		RetransCommand(0);
		return;
	}
	if (fFrameHeader[2] == 0x40)
	{
		ULong word = fLocalCaps.Word() & ~kT30MinScanTime;
		if (fMinScanLineTime >= 40)
			word |= 0x4000;
		else if (fMinScanLineTime >= 20)
			;
		else if (fMinScanLineTime >= 10)
			word |= 0x2000;
		else if (fMinScanLineTime >= 5)
			word |= 0x1000;
		else
			word |= 0x7000;
		fLocalCaps.SetWord(word);
		ULong rate = FastestDataRate(fReceiveMods & fModemCaps.fReceiveDataMods);
		fLocalCaps.SetWord((fLocalCaps.Word() & ~kT30DataRate) | ((rate & 0xf) << kT30DataRateShift));
		NewtonErr err;
		if ((err = BuildControlFrame(0x80, fLocalCaps.fBytes, 5, true)) != noErr
		||  (err = PostTransPkt(kModemCtlC1SendHDLC, 1, &fSendList, 10, true)) != noErr)
			StartAbort(err);
		return;
	}
	fPhaseBStep = 9;
	GetResponse();
}


// ROM 0x000b553c PhaseBPutCommandToRcv__8TFaxToolFl
// TSI sent: the DCS; the DCS sent: the training check.
void
TFaxTool::PhaseBPutCommandToRcv(NewtonErr result)
{
	if (fToolState & kToolStateWantAbort)
		return;
	UChar tsi = (fFaxFlags & kFaxFlagCaller) ? 0x43 : 0x42;
	if (fFrameHeader[2] != tsi)
	{
		PutTrainingCheck(0x48);
		return;
	}
	ULong rate = FastestDataRate(fTransmitMods & fModemCaps.fTransmitDataMods);
	fLocalCaps.SetWord((fLocalCaps.Word() & ~kT30DataRate) | ((rate & 0xf) << kT30DataRateShift));
	NewtonErr err;
	if ((err = BuildControlFrame(tsi + 0x40, fSessionCaps.fBytes, fDCSLength, true)) != noErr
	||  (err = PostTransPkt(kModemCtlC1SendHDLC, 1, &fSendList, 0, true)) != noErr)
		StartAbort(err);
}


// ROM 0x000b56a0 PhaseBGetResponse__8TFaxToolFl
// The response to the command sent (DCS, DTC or DIS): no carrier and the
// command goes again; a bad frame and it goes again after a pause.
void
TFaxTool::PhaseBGetResponse(NewtonErr result)
{
	switch (result)
	{
	case kModemErrNoCarrier:
	case kModemErrRcvPktFlagsTimeOut:
	case kModemErrCommandFailure:
		KillTimer();
		if (!(fFaxFlags & kFaxFlagCaller))
			fPhaseBStep = 2;
		else
			fPhaseBStep = (fFaxFlags & kFaxFlagTransmitter) ? 4 : 5;
		RetransCommand(0);
		return;
	case kModemErrNoFaxCarrier:
	case kSerErr_CRCError:
		KillTimer();
		if (fLastCommand == kFaxCmdDCS)
		{
			fPhaseBStep = 4;
			RetransCommand(0xd2);
		}
		else if (fLastCommand == kFaxCmdDTC)
		{
			fPhaseBStep = 5;
			RetransCommand(0xd2);
		}
		else if (fLastCommand == kFaxCmdDIS)
		{
			fPhaseBStep = 2;
			RetransCommand(0x898);
		}
		else
			StartAbort(kFaxToolErrProtocolError);
		return;
	case kCommErrRequestCanceled:
		return;
	case noErr:
		if (fLastCommand == kFaxCmdDCS)
			PhaseBProcessDCSResponse();
		else if (fLastCommand == kFaxCmdDTC)
			PhaseBProcessDTCResponse();
		else if (fLastCommand == kFaxCmdDIS)
			PhaseBProcessDISResponse();
		return;
	}
	StartAbort(result);
}


// ROM 0x000b57dc PhaseBRespondToFTT__8TFaxToolFl
void
TFaxTool::PhaseBRespondToFTT(NewtonErr result)
{
	if (result == noErr)
	{
		fPhaseBStep = 3;
		GetCommand();
	}
	else
		RetransCommand(0x48);
}


// ROM 0x000b57f4 GetCommandComplete__8TFaxToolFl
// A command received, for phase B or D; nothing more after EOM and the
// procedure starts again.
void
TFaxTool::GetCommandComplete(NewtonErr result)
{
	switch (result)
	{
	case kModemErrNoCarrier:
		if (fPostMessage == kFaxPostEOM)
			StartPhaseB();
		else
			StartAbort(kFaxToolErrNoRemoteSignal);
		return;
	case kModemErrRcvPktFlagsTimeOut:
	case kModemErrNoFaxCarrier:
	case kSerErr_CRCError:
		fLastCommand = kFaxCmdGetCommand;
		RetransCommand(0);
		return;
	case kCommErrRequestCanceled:
		return;
	case noErr:
		if (fPhase == kFaxPhaseB)
			PhaseBProcessCommand();
		else if (fPhase == kFaxPhaseD)
			PhaseDProcessCommand();
		else
			StartAbort(kFaxToolErrProtocolError);
		return;
	}
	StartAbort(result);
}


// ROM 0x000b5884 PhaseBProcessCommand__8TFaxToolFv
// The called machine's command: TSI and DCS (the training check follows),
// or DIS/DTC, which have it send its own again.  BUG: whether the DCS was
// valid is a register only a DCS in this frame sets - a final frame that
// is not a DCS after one that was reads what the register held (false
// here).
void
TFaxTool::PhaseBProcessCommand()
{
	ULong duration = 0;
	Boolean valid = false;
	NewtonErr err;
	UChar fcf = fFrame[2];
	switch (fcf)
	{
	case 0x80:
	case 0x81:
		fPhaseBStep = 2;
		break;
	case 0x41:
		break;
	case 0x42:
	case 0x43:
		GetIdentification(fFrame + 3, fRemoteId.fId, fFrameList.GetSize() - 3);
		break;
	case 0x82:
	case 0x83:
		{
			fFaxFlags |= kFaxFlagDCSReceived;
			TT30Capabilities dcs;
			CopyFIF(dcs, fFrameList, fFrame + 3);
			if (!(valid = ValidateDCS(dcs)))
			{
				duration = 0x9c4;
				fPhaseBStep = 2;
			}
			else
			{
				SetSessionParameters(dcs);
				if (fFaxFlags & kFaxFlagReceive)
				{
					FreeReceiveBuffers();
					if ((err = AllocateReceiveBuffers()) != noErr)
					{
						StartAbort(err);
						return;
					}
				}
			}
		}
		break;
	default:
		fPhaseBStep = 0xd;
		break;
	}

	if (fFrame[1] == 0x13)
	{
		if ((fFaxFlags & kFaxFlagDCSReceived) && valid)
			fPhaseBStep = 6;
		if (fPhaseBStep == 2)
			PutInitialId(duration);
		else if (fPhaseBStep == 6)
		{
			err = AllocateTCBuffer(1);
			fTCCount = 0;
			fTCZeroRun = 0;
			fTCLongestRun = 0;
			if (err == noErr)
				GetTrainingCheck();
			else
				StartAbort(err);
		}
		else
		{
			if (fPhaseBStep == 0xd)
				fToolState &= ~kFaxToolStatePhaseE;
			StartAbort(kFaxToolErrNoRemoteSignal);
		}
	}
	else if (fPhaseBStep == 9)
	{
		if ((err = PostRecvPkt(kModemCtlC1RecvHDLC, 1, &fFrameList, 3250, false)) != noErr)
			StartAbort(err);
	}
	else if (fPhaseBStep == 3)
		GetCommand();
	else
		StartAbort(kFaxToolErrIncompatibleRemoteUnit);
}


// ROM 0x000b5b90 PhaseBProcessDCSResponse__8TFaxToolFv
// The caller's DCS answered: CFR (phase B done), FTT (slower), CRP, or the
// called machine's DIS again.
void
TFaxTool::PhaseBProcessDCSResponse()
{
	NewtonErr err;
	switch (fFrame[2])
	{
	case 0x44:
	case 0x45:
		fPhaseBStep = 0xa;
		break;
	case 0x20:
		fFaxFlags |= kFaxFlagNSF;
		break;
	case 0x1a:
	case 0x1b:
		fPhaseBStep = 0xb;
		break;
	case 0x40:
		GetIdentification(fFrame + 3, fRemoteId.fId, fFrameList.GetSize() - 3);
		break;
	case 0x84:
	case 0x85:
		fPhaseBStep = 0xe;
		break;
	case 0x80:
		{
			fFaxFlags |= kFaxFlagDISReceived;
			TT30Capabilities dis;
			CopyFIF(dis, fFrameList, fFrame + 3);
			if (CompatibleRemoteRcvr(dis) && (fFaxFlags & kFaxFlagSend))
			{
				BuildDCS(dis, fSessionCaps);
				fPhaseBStep = 4;
				fFaxFlags |= kFaxFlagTransmitter;
				fSpeedTries = 1;
				fRetries = 3;
			}
			else if (CompatibleRemoteXmtr(dis) && (fFaxFlags & kFaxFlagReceive))
			{
				fFaxFlags &= ~kFaxFlagTransmitter;
				fPhaseBStep = 5;
			}
		}
		break;
	case 0xfa:
	case 0xfb:
		fPhaseBStep = 0xd;
		break;
	}

	if (fFrame[1] == 0x13)
	{
		KillTimer();
		switch (fPhaseBStep)
		{
		case 4:
			if (++fTrainTries > 3)
				break;
			PutCommandToRcv(0x6e);
			return;
		case 5:
			if (++fTrainTries > 3)
				break;
			PutCommandToXmit(10);
			return;
		case 0xa:
			if ((err = AdjustSpeedForFTT()) != noErr)
			{
				StartAbort(err);
				return;
			}
			fPhaseBStep = 4;
			fTrainTries = 1;
			fRetries = 3;
			PutCommandToRcv(0x6e);
			return;
		case 0xb:
			fPhaseBStep = 4;
			RetransCommand(0xd2);
			return;
		case 0xd:
			fToolState &= ~kFaxToolStatePhaseE;
			break;
		case 0xe:
			PhaseBComplete(noErr);
			return;
		}
		StartAbort(kFaxToolErrNoRemoteSignal);
	}
	else if (fPhaseBStep == 9)
	{
		if ((err = PostRecvPkt(kModemCtlC1RecvHDLC, 1, &fFrameList, 3250, false)) != noErr)
			StartAbort(err);
	}
	else
		StartAbort(kFaxToolErrIncompatibleRemoteUnit);
}


// ROM 0x000b5e9c PhaseBProcessDTCResponse__8TFaxToolFv
void
TFaxTool::PhaseBProcessDTCResponse()
{ }


// ROM 0x000b5ea0 PhaseBProcessDISResponse__8TFaxToolFv
// The called machine's DIS answered: TSI and DCS (the training check
// follows), FTT, CRP, DCN - or, by a caller, its own DIS.  BUG: as
// PhaseBProcessCommand, the DCS's validity is a register only a DCS sets.
void
TFaxTool::PhaseBProcessDISResponse()
{
	ULong duration = 0;
	Boolean valid = false;
	NewtonErr err;
	switch (fFrame[2])
	{
	case 0x44:
	case 0x45:
		fPhaseBStep = 0xa;
		break;
	case 0x41:
		break;
	case 0x1a:
	case 0x1b:
		fPhaseBStep = 0xb;
		break;
	case 0x20:
		fFaxFlags |= kFaxFlagNSF;
		break;
	case 0x40:
	case 0x42:
	case 0x43:
		GetIdentification(fFrame + 3, fRemoteId.fId, fFrameList.GetSize() - 3);
		break;
	case 0x84:
	case 0x85:
		fPhaseBStep = 0xe;
		break;
	case 0x80:
		if (!(fFaxFlags & kFaxFlagCaller))
		{
			fPhaseBStep = 2;
			break;
		}
		{
			fFaxFlags |= kFaxFlagDISReceived;
			TT30Capabilities dis;
			CopyFIF(dis, fFrameList, fFrame + 3);
			if (CompatibleRemoteRcvr(dis) && (fFaxFlags & kFaxFlagSend))
			{
				BuildDCS(dis, fSessionCaps);
				fPhaseBStep = 4;
				fFaxFlags |= kFaxFlagTransmitter;
				fRetries = 3;
				fSpeedTries = 1;
			}
			else if (CompatibleRemoteXmtr(dis) && (fFaxFlags & kFaxFlagReceive))
			{
				fFaxFlags &= ~kFaxFlagTransmitter;
				fPhaseBStep = 5;
			}
		}
		break;
	case 0x81:
		fPhaseBStep = 2;
		break;
	case 0x82:
	case 0x83:
		{
			fFaxFlags |= kFaxFlagDCSReceived;
			TT30Capabilities dcs;
			CopyFIF(dcs, fFrameList, fFrame + 3);
			if (!(valid = ValidateDCS(dcs)))
			{
				duration = 0x9c4;
				fPhaseBStep = 2;
				break;
			}
			SetSessionParameters(dcs);
			if (fFaxFlags & kFaxFlagReceive)
			{
				FreeReceiveBuffers();
				if ((err = AllocateReceiveBuffers()) != noErr)
				{
					StartAbort(err);
					return;
				}
			}
		}
		break;
	case 0xfa:
	case 0xfb:
		fPhaseBStep = 0xd;
		break;
	}

	if (fFrame[1] == 0x13)
	{
		KillTimer();
		if ((fFaxFlags & kFaxFlagDCSReceived) && valid && !(fFaxFlags & kFaxFlagTransmitter))
			fPhaseBStep = 6;
		switch (fPhaseBStep)
		{
		case 2:
			PutInitialId(duration);
			return;
		case 4:
			if (++fTrainTries > 3)
				break;
			PutCommandToRcv(0x6e);
			return;
		case 5:
			if (++fTrainTries > 3)
				break;
			PutCommandToXmit(10);
			return;
		case 6:
			err = AllocateTCBuffer(1);
			fTCCount = 0;
			fTCZeroRun = 0;
			fTCLongestRun = 0;
			if (err != noErr)
			{
				StartAbort(err);
				return;
			}
			GetTrainingCheck();
			return;
		case 0xa:
			if ((err = AdjustSpeedForFTT()) != noErr)
			{
				StartAbort(err);
				return;
			}
			fRetries = 3;
			fTrainTries = 1;
			fPhaseBStep = 4;
			PutCommandToRcv(0x6e);
			return;
		case 0xb:
			fPhaseBStep = 4;
			RetransCommand(0xd2);
			return;
		case 0xd:
			fToolState &= ~kFaxToolStatePhaseE;
			break;
		case 0xe:
			PhaseBComplete(noErr);
			return;
		}
		StartAbort(kFaxToolErrNoRemoteSignal);
	}
	else if (fPhaseBStep == 9)
	{
		if ((err = PostRecvPkt(kModemCtlC1RecvHDLC, 1, &fFrameList, 3250, false)) != noErr)
			StartAbort(err);
	}
	else
		StartAbort(kFaxToolErrIncompatibleRemoteUnit);
}


// ROM 0x000b62fc PhaseBWaitForSignalGone__8TFaxToolFl
void
TFaxTool::PhaseBWaitForSignalGone(NewtonErr result)
{
	switch (result)
	{
	case kSerErr_CRCError:
	case kModemErrNoFaxCarrier:
	case noErr:
		if ((result = PostRecvPkt(kModemCtlC1RecvHDLC, 1, &fFrameList, 3250, true)) == noErr)
			return;
		break;
	case kModemErrNoCarrier:
		KillTimer();
		fPhaseBStep = (fFaxFlags & kFaxFlagTransmitter) ? 4 : 5;
		RetransCommand(0xc8);
		return;
	case kCommErrRequestCanceled:
		return;
	}
	StartAbort(result);
}


// ROM 0x000b63ec PhaseBProcessOptions__8TFaxToolFv
// The session agreed ('fsif') and the other machine's identity ('frid')
// put in the options of the control request in hand (the connect or the
// listen), if it asks for them.
void
TFaxTool::PhaseBProcessOptions()
{
	if (fOptionsInfo.fOptions == nil)
		return;
	TOptionIterator iter(fOptionsInfo.fOptions);
	TOption* option = iter.FindOption(kCMOFaxSessionInfo);
	if (option != nil)
	{
		TCMOFaxSessionInfo* info = (TCMOFaxSessionInfo*) option;
		info->fHorizontalRes = fHorizontalRes;
		info->fVerticalRes = fVerticalRes;
		info->fLength = (fSessionCaps.Word() & kT30Length) >> kT30LengthShift;
		info->fWidth = fBytesPerLine * 8;
		TCMOFaxSessionInfo defaults;
		if (option->Length() >= defaults.Length())
			info->fBitRate = fBitRate;
		option->SetOpCodeResult(opSuccess);
		option->SetProcessed();
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


// ROM 0x000b6528 PhaseBComplete__8TFaxToolFl
// The caller's connect is complete; the called machine goes on to the
// page.
void
TFaxTool::PhaseBComplete(NewtonErr result)
{
	if (result != noErr)
	{
		RetransCommand(0x48);
		return;
	}
	if ((fToolState & kToolStateConnecting) && (fFaxFlags & kFaxFlagCaller))
	{
		PhaseBProcessOptions();
		ConnectComplete(noErr);
	}
	else
		StartPhaseC();
}


// ROM 0x000b8840 PhaseBPutPostMsgCmd__8TFaxToolFUcUl
// A frame of an FCF alone (CFR, FTT, MCF, CRP, ...), the final one.
void
TFaxTool::PhaseBPutPostMsgCmd(UChar fcf, ULong duration)
{
	NewtonErr err;
	if ((err = BuildControlFrame(fcf, nil, 0, true)) != noErr
	||  (err = PostTransPkt(kModemCtlC1SendHDLC, 1, &fSendList, duration, true)) != noErr)
		StartAbort(err);
}


// ROM 0x000b7998 BuildDCS__8TFaxToolFR16TT30CapabilitiesT1
// The DCS for a remote DIS: the fastest rate both have, fine resolution
// and 2-D coding if both have them, our width and length, a minimum scan
// line time the other machine can live with, and (if it extends its DIS)
// the error correction bits both have.
void
TFaxTool::BuildDCS(TT30Capabilities& remote, TT30Capabilities& dcs)
{
	fDCSLength = 3;
	ULong word = dcs.Word();
	word &= ~0xff000000;
	word &= ~kT30Transmitter;
	word |= kT30Receiver;
	dcs.SetWord(word);
	fRateCode = (remote.Word() & kT30DataRate) >> kT30DataRateShift;
	switch (FastestDataRate(fTransmitMods & fModemCaps.fTransmitDataMods) & fRateCode)
	{
	case 0:
		dcs.SetWord(dcs.Word() & ~kT30DataRate);
		fBitRate = 2400;
		fModulation = 2;
		break;
	case 1:
	case 3:
	case 7:
		dcs.SetWord((dcs.Word() & ~kT30DataRate) | 0x40000);
		fBitRate = 9600;
		fModulation = 0x40;
		break;
	case 2:
		dcs.SetWord((dcs.Word() & ~kT30DataRate) | 0x80000);
		fBitRate = 4800;
		fModulation = 4;
		break;
	case 0xb:
		dcs.SetWord((dcs.Word() & ~kT30DataRate) | 0x200000);
		fBitRate = 14400;
		fModulation = 0x1000;
		break;
	}
	ULong fine = ((fLocalCaps.Word() >> 22) & 1) & ((remote.Word() >> 22) & 1);
	word = (dcs.Word() & ~kT30FineResolution) | (fine << 22);
	dcs.SetWord(word);
	fVerticalRes = (word & kT30FineResolution) ? 0xc4 : 0x62;
	fHorizontalRes = 0xcc;
	ULong twoD = ((fLocalCaps.Word() >> 23) & 1) & ((remote.Word() >> 23) & 1);
	word = (word & ~kT30TwoDimensional) | (twoD << 23);
	word = (word & ~kT30Width) | (fLocalCaps.Word() & kT30Width);
	word = (word & ~kT30Length) | (fLocalCaps.Word() & kT30Length);
	dcs.SetWord(word);
	switch ((remote.Word() & kT30MinScanTime) >> kT30MinScanTimeShift)
	{
	case 0:
		goto ms20;
	case 1:
		goto ms5;
	case 2:
		goto ms10;
	case 3:
		if (word & kT30FineResolution)
			goto ms10;
		goto ms20;
	case 4:
		goto ms40;
	case 5:
		if (word & kT30FineResolution)
			goto ms20;
		goto ms40;
	case 6:
		if (word & kT30FineResolution)
			goto ms5;
		goto ms10;
	case 7:
		dcs.SetWord(word | 0x7000);
		fMinScanTime = 0;
		break;
	ms5:
		dcs.SetWord((word & ~kT30MinScanTime) | 0x1000);
		fMinScanTime = 5;
		break;
	ms10:
		dcs.SetWord((word & ~kT30MinScanTime) | 0x2000);
		fMinScanTime = 10;
		break;
	ms20:
		dcs.SetWord(word & ~kT30MinScanTime);
		fMinScanTime = 20;
		break;
	ms40:
		dcs.SetWord((word & ~kT30MinScanTime) | 0x4000);
		fMinScanTime = 40;
		break;
	}
	if (remote.Word() & kT30Extend)
	{
		fDCSLength++;
		word = dcs.Word() | kT30Extend;
		ULong local = fLocalCaps.Word();
		ULong other = remote.Word();
		word = (word & ~1) | (local & other & 1);
		word = (word & ~2) | ((((local >> 1) & 1) & ((other >> 1) & 1)) << 1);
		word = (word & ~4) | ((((local >> 2) & 1) & ((other >> 2) & 1)) << 2);
		word &= ~8;
		word = (word & ~0x10) | ((((local >> 4) & 1) & ((other >> 4) & 1)) << 4);
	}
	else
		word = dcs.Word() & ~(kT30Extend | 0x1f);
	word &= ~(0x60 | 0x80);
	dcs.SetWord(word);
	dcs.SetWord1(dcs.Word1() & 0x00ffffff);
}


// ROM 0x000b7d64 CompatibleRemoteRcvr__8TFaxToolFR16TT30Capabilities
// Whether the other machine can receive our pages: a receiver, as wide
// (an extended DIS with bits 25 and 33 and the next byte's first set
// counts as no width at all) and as long as ours.
Boolean
TFaxTool::CompatibleRemoteRcvr(TT30Capabilities& remote)
{
	ULong word = remote.Word();
	if (!(word & kT30Receiver))
		return false;
	ULong width;
	if ((word & kT30Extend) && (word & 0x80) && (remote.Word1() & 0x80000000) && (remote.Word1() & 0x01000000))
		width = 0;
	else
		width = (word & kT30Width) >> kT30WidthShift;
	ULong local = fLocalCaps.Word();
	switch ((local & kT30Width) >> kT30WidthShift)
	{
	case 1:
		if (!(width & 3))
			return false;
		break;
	case 2:
		if (!(width & 2))
			return false;
		break;
	}
	switch ((local & kT30Length) >> kT30LengthShift)
	{
	case 1:
		return ((word & kT30Length) >> kT30LengthShift) == 1;
	case 2:
		return (word & kT30Length) == 0x800;
	}
	return true;
}


// ROM 0x000b7e10 CompatibleRemoteXmtr__8TFaxToolFR16TT30Capabilities
Boolean
TFaxTool::CompatibleRemoteXmtr(TT30Capabilities& remote)
{
	return (remote.Word() & kT30Transmitter) != 0;
}


// ROM 0x000b7e24 ValidateDCS__8TFaxToolFR16TT30Capabilities
// Whether a DCS asks only for what we (and our modem) can do.
Boolean
TFaxTool::ValidateDCS(TT30Capabilities& dcs)
{
	ULong word = dcs.Word();
	ULong local = fLocalCaps.Word();
	if ((word & kT30TwoDimensional) && !(local & kT30TwoDimensional))
		return false;
	if ((word & kT30FineResolution) && !(local & kT30FineResolution))
		return false;
	if ((word & kT30Extend) && (word & 4) && !(local & 4))
		return false;
	ULong localWidth = (local & kT30Width) >> kT30WidthShift;
	switch ((word & kT30Width) >> kT30WidthShift)
	{
	case 1:
		if (localWidth != 1)
			return false;
		break;
	case 2:
		if (localWidth != 1 && localWidth != 2)
			return false;
		break;
	}
	ULong localLength = (local & kT30Length) >> kT30LengthShift;
	switch ((word & kT30Length) >> kT30LengthShift)
	{
	case 1:
		if (localLength != 1 && localLength != 2)
			return false;
		break;
	case 2:
		if (localLength != 2)
			return false;
		break;
	}
	switch ((word & kT30DataRate) >> kT30DataRateShift)
	{
	case 0:
	case 2:
		return (fReceiveMods & 4) != 0;
	case 1:
		return (fReceiveMods & 0x40) != 0;
	case 3:
		return (fReceiveMods & 8) != 0;
	case 4:
		return (fReceiveMods & 0x800) != 0;
	case 6:
		return (fReceiveMods & 0x200) != 0;
	case 8:
		return (fReceiveMods & 0x1000) != 0;
	case 9:
		return (fReceiveMods & 0x80) != 0;
	case 0xa:
		return (fReceiveMods & 0x400) != 0;
	case 0xb:
		return (fReceiveMods & 0x10) != 0;
	}
	return true;
}


// ROM 0x000b7fec SetSessionParameters__8TFaxToolFR16TT30Capabilities
// A DCS received: the page's resolution, width, speed and modulation, and
// how long a receive buffer may take to fill.
void
TFaxTool::SetSessionParameters(TT30Capabilities& dcs)
{
	fSessionCaps = dcs;
	ULong word = dcs.Word();
	fVerticalRes = (word & kT30FineResolution) ? 0xc4 : 0x62;
	fHorizontalRes = 0xcc;
	switch ((word & kT30Width) >> kT30WidthShift)
	{
	case 0:
		fBytesPerLine = 216;
		fLineBufferSize = 0x438;
		break;
	case 1:
		fBytesPerLine = 256;
		fLineBufferSize = 0x500;
		break;
	case 2:
		fBytesPerLine = 304;
		fLineBufferSize = 0x5f0;
		break;
	}
	switch ((word & kT30DataRate) >> kT30DataRateShift)
	{
	case 0:
		fBitRate = 2400;
		fModulation = 2;
		break;
	case 1:
		fBitRate = 9600;
		fModulation = 0x40;
		break;
	case 2:
		fBitRate = 4800;
		fModulation = 4;
		break;
	case 3:
		fBitRate = 7200;
		fModulation = 8;
		break;
	case 4:
		fBitRate = 14400;
		fModulation = 0x800;
		break;
	case 6:
		fBitRate = 12000;
		fModulation = 0x200;
		break;
	case 8:
		fBitRate = 14400;
		fModulation = 0x1000;
		break;
	case 9:
		fBitRate = 9600;
		fModulation = 0x80;
		break;
	case 0xa:
		fBitRate = 12000;
		fModulation = 0x400;
		break;
	case 0xb:
		fBitRate = 7200;
		fModulation = 0x10;
		break;
	}
	fReceiveTimeout = (fReceiveBufferSize * 8000) / fBitRate + 5000;
}


// ROM 0x000b922c AdjustSpeedForFTT__8TFaxToolFv
// FTT (or RTN, PIN): the next slower speed - V.17 14400 to 12000 to V.29
// 9600 to 7200 to V.27 ter 4800 to 2400; at 2400 three tries and it is
// given up (kFaxToolErrFailureToTrain).
NewtonErr
TFaxTool::AdjustSpeedForFTT()
{
	ULong word = fSessionCaps.Word();
	ULong current = (word & kT30DataRate) >> kT30DataRateShift;
	switch (FastestDataRate(fTransmitMods & fModemCaps.fTransmitDataMods) & fRateCode)
	{
	case 0:
		goto tryAgain;
	case 1:
		if ((word & kT30DataRate) == 0x40000)
			goto v29at7200;
		goto tryAgain;
	case 2:
		if (current == 2)
			goto v27at2400;
		goto tryAgain;
	case 3:
	case 7:
		switch (current)
		{
		case 0:
			goto tryAgain;
		case 2:
			goto v27at2400;
		case 3:
			goto v27at4800;
		}
		goto v29at7200;
	case 0xb:
		switch (current)
		{
		case 0:
			goto tryAgain;
		case 1:
		case 9:
			goto v29at7200;
		case 2:
			goto v27at2400;
		case 3:
		case 0xb:
			goto v27at4800;
		case 4:
		case 8:
			fSessionCaps.SetWord((word & ~kT30DataRate) | 0x280000);
			fBitRate = 12000;
			fModulation = 0x400;
			return noErr;
		case 6:
		case 0xa:
			fSessionCaps.SetWord((word & ~kT30DataRate) | 0x40000);
			fBitRate = 9600;
			fModulation = 0x40;
			return noErr;
		}
		return noErr;
	}
	return kFaxToolErrFailureToTrain;

tryAgain:
	if (++fSpeedTries > 3)
		return kFaxToolErrFailureToTrain;
	return noErr;

v29at7200:
	fSessionCaps.SetWord((word & ~kT30DataRate) | 0xc0000);
	fModulation = 8;
	fBitRate = 7200;
	return noErr;

v27at2400:
	fSessionCaps.SetWord(word & ~kT30DataRate);
	fModulation = 2;
	fBitRate = 2400;
	return noErr;

v27at4800:
	fSessionCaps.SetWord((word & ~kT30DataRate) | 0x80000);
	fModulation = 4;
	fBitRate = 4800;
	return noErr;
}


// The modulation the training check goes at: V.17's long training for a
// V.17 page.
static ULong
TrainingModulation(ULong modulation)
{
	if (modulation == 0x1000)
		return 0x800;
	if (modulation == 0x400)
		return 0x200;
	return modulation;
}


// ROM 0x000b81e8 PutCommandToRcv__8TFaxToolFUl
// TSI (the DCS follows): this machine will send.
void
TFaxTool::PutCommandToRcv(ULong duration)
{
	NewtonErr err;
	fLastCommand = kFaxCmdDCS;
	if ((err = AllocateTCBuffer(0)) != noErr
	||  (err = BuildControlFrame((fFaxFlags & kFaxFlagCaller) ? 0x43 : 0x42, fLocalIdReversed, 0x14, false)) != noErr
	||  (err = PostTransPkt(kModemCtlC1SendHDLC, 1, &fSendList, duration, true)) != noErr)
		StartAbort(err);
}


// ROM 0x000b828c PutCommandToXmit__8TFaxToolFUl
// CIG (for DTC): this machine will receive.
void
TFaxTool::PutCommandToXmit(ULong duration)
{
	NewtonErr err;
	fLastCommand = kFaxCmdDTC;
	if ((err = BuildControlFrame(0x41, fLocalIdReversed, 0x14, false)) != noErr
	||  (err = PostTransPkt(kModemCtlC1SendHDLC, 1, &fSendList, duration, true)) != noErr)
		StartAbort(err);
}


// ROM 0x000b88b8 PutInitialId__8TFaxToolFUl
// CSI (the DIS follows).
void
TFaxTool::PutInitialId(ULong duration)
{
	NewtonErr err;
	fLastCommand = kFaxCmdDIS;
	if ((err = BuildControlFrame(0x40, fLocalIdReversed, 0x14, false)) != noErr
	||  (err = PostTransPkt(kModemCtlC1SendHDLC, 1, &fSendList, duration, true)) != noErr)
		StartAbort(err);
}


// ROM 0x000b8310 PutTrainingCheck__8TFaxToolFUl
// The training check: the TC buffer's noughts at the page's speed.
void
TFaxTool::PutTrainingCheck(ULong duration)
{
	NewtonErr err = kError_No_Memory;
	fPhaseBStep = 8;
	fSendList.DeleteAll();
	CBufferSegment* segment = new CBufferSegment;
	if (segment != nil
	&&  (err = segment->Init(fTCBuffer, fTCSize, false, 0, -1)) == noErr)
	{
		fSendList.InsertLast(segment);
		if ((err = PostTransPkt(kModemCtlC1SendData, TrainingModulation(fModulation), &fSendList, duration, true)) == noErr)
			return;
	}
	StartAbort(err);
}


// ROM 0x000b83e8 PutTrainingCheckComplete__8TFaxToolFl
void
TFaxTool::PutTrainingCheckComplete(NewtonErr result)
{
	fLastCommand = kFaxCmdDCS;
	if (result != noErr)
	{
		RetransCommand(0);
		return;
	}
	FreeTCBuffer();
	fPhaseBStep = 9;
	fRetries = 3;
	GetResponse();
}


// ROM 0x000b8434 GetTrainingCheck__8TFaxToolFv
// The training check received into the TC buffer (through the frame list,
// its own segment set aside meanwhile).  DEVIATION: a segment that cannot
// be made aborts with whatever error register the caller left (the ROM's
// r5 is not yet set); the host says kError_No_Memory.
void
TFaxTool::GetTrainingCheck()
{
	NewtonErr err = kError_No_Memory;
	fPhaseBStep = 6;
	fFaxFlags &= ~kFaxFlagTrained;
	fFrameList.RemoveAll();
	CBufferSegment* segment = new CBufferSegment;
	if (segment != nil
	&&  (err = segment->Init(fTCBuffer, fTCSize, false, 0, -1)) == noErr)
	{
		fFrameList.InsertLast(segment);
		if ((err = PostRecvPkt(kModemCtlC1RecvData, TrainingModulation(fModulation), &fFrameList, 7000, false)) == noErr)
			return;
	}
	StartAbort(err);
}


// ROM 0x000b85c4 GetTrainingCheckComplete__8TFaxToolFl
// The training check received, a piece at a time until its carrier ends:
// good if it was within a quarter of a second and a half's bytes and its
// longest run of noughts was two thirds of that - CFR (and the listen is
// complete) - or FTT.
void
TFaxTool::GetTrainingCheckComplete(NewtonErr result)
{
	NewtonErr err;
	fRetries = 3;
	if (result != noErr)
	{
		fPhaseBStep = 7;
		FreeTCBuffer();
		fFrameList.DeleteAll();
		if ((err = fFrameSegment.Init(fFrame, sizeof(fFrame), false, 0, -1)) != noErr)
		{
			StartAbort(err);
			return;
		}
		fFrameList.InsertLast(&fFrameSegment);
		fLastCommand = kFaxCmdFTT;
		PhaseBPutPostMsgCmd(0x44, 0x48);
		return;
	}
	fTCCount += fFrameList.Position();
	VerifyTrainingCheck(fFrameList.Position());
	if (fModemReply.fText[0])
	{
		// the check's carrier ended
		FreeTCBuffer();
		fFrameList.DeleteAll();
		if ((err = fFrameSegment.Init(fFrame, sizeof(fFrame), false, 0, -1)) != noErr)
		{
			StartAbort(err);
			return;
		}
		fFrameList.InsertLast(&fFrameSegment);
		ULong size = FigureTCSize(fBitRate);
		ULong tolerance = (size * 25) / 100;
		if (fTCCount > size + tolerance
		||  fTCCount < size - tolerance
		||  fTCLongestRun < (size * 2) / 3)
		{
			fPhaseBStep = 7;
			fLastCommand = kFaxCmdFTT;
			PhaseBPutPostMsgCmd(0x44, 10);
			return;
		}
		fFaxFlags |= kFaxFlagReceivingLines | kFaxFlagTrained;
		fReceiveState = 1;
		fDecodeBufIndex = 0;
		fPhaseBStep = 0xe;
		fReceiveBufs[1].fFull = false;
		fReceiveBufs[0].fFull = false;
		PhaseBProcessOptions();
		if (fToolState & kToolStateListenMode)
			ListenComplete(noErr);
		fLastCommand = kFaxCmdCFR;
		PhaseBPutPostMsgCmd(0x84, 10);
		return;
	}
	fFrameList.Reset();
	if ((err = PostRecvPkt(kModemCtlC1RecvData, TrainingModulation(fModulation), &fFrameList, 7000, false)) != noErr)
	{
		FreeTCBuffer();
		StartAbort(err);
	}
}


// ROM 0x000b944c VerifyTrainingCheck__8TFaxToolFUl
// The runs of noughts in the training check's bytes received.
void
TFaxTool::VerifyTrainingCheck(ULong count)
{
	for (ULong i = 0; i < count; i++)
	{
		if (fTCBuffer[i] == 0)
			fTCZeroRun++;
		else
		{
			if (fTCZeroRun > fTCLongestRun)
				fTCLongestRun = fTCZeroRun;
			fTCZeroRun = 0;
		}
	}
	if (fTCZeroRun > fTCLongestRun)
		fTCLongestRun = fTCZeroRun;
}


// ROM 0x000b893c GetCommand__8TFaxToolFv
// A command awaited (6.7 seconds).
void
TFaxTool::GetCommand()
{
	NewtonErr err;
	fLastCommand = kFaxCmdGetCommand;
	fRetries = 1;
	if ((err = PostRecvPkt(kModemCtlC1RecvHDLC, 1, &fFrameList, 6700, false)) != noErr)
		StartAbort(err);
}


// ROM 0x000b89a4 GetResponse__8TFaxToolFv
// A response awaited (T4, 3 seconds - timed out after 6.5).
void
TFaxTool::GetResponse()
{
	NewtonErr err;
	if ((err = PostRecvPkt(kModemCtlC1RecvHDLC, 1, &fFrameList, 3250, false)) != noErr
	||  (err = PostTimer(kFaxTimerResponse, 6500, kMilliseconds)) != noErr)
		StartAbort(err);
}


// ROM 0x000b8a1c RetransCommand__8TFaxToolFUl
// The last command sent again after duration milliseconds of silence, up to
// fRetries times.
void
TFaxTool::RetransCommand(ULong duration)
{
	NewtonErr err;
	if (fPhase == kFaxPhaseB && fLastCommand == kFaxCmdTCF)
	{
		GetTrainingCheckComplete(kFaxToolErrFailureToTrain);
		return;
	}
	if (--fRetries < 0)
	{
		if (fLastCommand == kFaxCmdGetCommand)
		{
			if (fPostMessage == kFaxPostEOM)
			{
				StartPhaseB();
				return;
			}
		}
		else if (fLastCommand == kFaxCmdDIS)
			fToolState |= kFaxToolStatePhaseE;
		StartAbort(kFaxToolErrNoRemoteSignal);
		return;
	}
	if (fPhase == kFaxPhaseB)
	{
		switch (fLastCommand)
		{
		case kFaxCmdDTC:
			fPhaseBStep = 5;
			PutCommandToXmit(duration);
			return;
		case kFaxCmdDCS:
			fPhaseBStep = 4;
			PutCommandToRcv(duration);
			return;
		case kFaxCmdDIS:
			fPhaseBStep = 2;
			PutInitialId(duration);
			return;
		case kFaxCmdGetCommand:
			if ((err = PostRecvPkt(kModemCtlC1RecvHDLC, 1, &fFrameList, 4000, false)) != noErr)
				StartAbort(err);
			return;
		case kFaxCmdCFR:
			PhaseBPutPostMsgCmd(0x84, duration);
			return;
		case kFaxCmdFTT:
			PhaseBPutPostMsgCmd(0x44, duration);
			return;
		}
	}
	else if (fPhase == kFaxPhaseD)
	{
		if (fLastCommand == kFaxCmdGetCommand)
		{
			if ((err = PostRecvPkt(kModemCtlC1RecvHDLC, 1, &fFrameList, 4000, false)) != noErr)
				StartAbort(err);
		}
		else if (fLastCommand != kFaxCmdResponse)
			PhaseDPutPostMsgCmd(duration);
		else
		{
			UChar fcf;
			if (fFaxFlags & kFaxFlagPageGood)
				fcf = 0x8c;
			else if (fFaxFlags & kFaxFlagPageRetrain)
				fcf = 0xcc;
			else
				fcf = 0x4c;
			PhaseBPutPostMsgCmd(fcf, 0x48);
		}
		return;
	}
	StartAbort(kFaxToolErrProtocolError);
}


// ROM 0x000b9168 DISTimeOut__8TFaxToolFv
void
TFaxTool::DISTimeOut()
{
	StartAbort(kFaxToolErrNoRemoteSignal);
}


// ROM 0x000b9174 ResponseTimeOut__8TFaxToolFv
// No response: the receive killed (TimeOutKillComplete sends the command
// again).
void
TFaxTool::ResponseTimeOut()
{
	NewtonErr err;
	if ((err = fKillMsg.SetUserRefCon(0xa)) == noErr)
	{
		fKillRequest.fRequestsToKill = kCommToolRequestTypeControl;
		if ((err = fModemPort.SendRPC(&fKillMsg, &fKillRequest, sizeof(fKillRequest), &fKillReply, sizeof(fKillReply), 0, nil, kCommToolRequestTypeKill)) == noErr)
		{
			fToolState |= kFaxToolStateModemRequest;
			return;
		}
	}
	StartAbort(err);
}


/*------------------------------------------------------------------------------
	Phase C: the page.
------------------------------------------------------------------------------*/

// ROM 0x000b746c StartPhaseC__8TFaxToolFv
// Sending: the first lines coded and the 'fsgp' answered; receiving: the
// 'feom' in hand answered and the page's data received.
void
TFaxTool::StartPhaseC()
{
	fMinLineBytes = (Long) (fMinScanTime * fBitRate) / 8000;
	ULong flags = fFaxFlags & ~(0xcc000000 | 0x00340000 | kFaxFlagRTC);
	fFaxFlags = flags;
	fPhase = kFaxPhaseC;
	fPostMessage = 0;
	fReceiveState = 1;
	fProgressEvent = 0;
	fLinesSinceFlush = 0;
	fStore = nil;
	if ((flags & kFaxFlagTransmitter) && (flags & kFaxFlagSend))
	{
		SendNextLine();
		StartPageComplete(noErr);
	}
	else if (!(flags & kFaxFlagTransmitter) && (flags & kFaxFlagReceive))
	{
		if (flags & kFaxFlagEndMessage)
		{
			fFaxFlags &= ~kFaxFlagEndMessage;
			((TOptionExtended*) fOptionsInfo.fCurOptPtr)->SetExtendedResult(noErr);
			ProcessOptionComplete(opSuccess);
		}
		fFaxFlags |= kFaxFlagReceivingLines;
		fBadLines = 0;
		fLines = 0;
		fDecodedIndex = 0;
		fReceiveBufIndex = 0;
		fDecodeBufIndex = 0;
		fReceiveBuffer = fReceiveBufs[0].fBuffer;
		fT4.Reset();
		fReceiveBufs[1].fFull = false;
		fReceiveBufs[0].fFull = false;
		fReceiveBufs[1].fInUse = false;
		fReceiveBufs[0].fInUse = false;
		ReceiveNextLinesBuf(&fReceiveBufs[fReceiveBufIndex]);
	}
	else
		StartAbort(kFaxToolErrIncompatibleRemoteUnit);
}


// ROM 0x000b6584 PhaseCPktComplete__8TFaxToolFl
// Sending: a buffer sent, the next one's lines coded; receiving: a buffer
// received, decoded into the client's lines - and when the modem says the
// data ended, phase D.
void
TFaxTool::PhaseCPktComplete(NewtonErr result)
{
	if (fToolState & kToolStateWantAbort)
		return;
	ULong flags = fFaxFlags;
	if (flags & kFaxFlagTransmitter)
	{
		if (result != noErr)
		{
			StartAbort(result);
			return;
		}
		if (flags & kFaxFlagBlackout)
		{
			CancelTimer();
			fFaxFlags &= ~kFaxFlagBlackout;
		}
		SendBuf().fFull = false;
		SendBuf().fInUse = false;
		SendBuf().fCount = 0;
		fSendBufIndex ^= 1;
		if (fFaxFlags & kFaxFlagEOMSent)
		{
			fFaxFlags &= ~kFaxFlagEOMSent;
			StartPhaseD();
			return;
		}
		if ((fToolState & kFaxToolStateTimer) && fTimerType == kFaxTimerLineTime)
		{
			fFaxFlags |= kFaxFlagSendLineWaiting;
			return;
		}
		SendNextLine();
		return;
	}

	if (result == kModemErrNoFaxCarrier || result == kModemErrNoCarrier)
	{
		if (flags & kFaxFlagDataStarted)
			StartAbort(result);
		else if (!(flags & kFaxFlagPageGood))
			ReStartPhaseB();
		else
			StartPhaseD();
		return;
	}
	if (result == kSerErr_AsyncError)
	{
		fBadLines++;
		fLines++;
	}
	else if (result != noErr)
	{
		StartAbort(result);
		return;
	}
	fFaxFlags = flags | kFaxFlagDataStarted;
	TFaxLineBuf& buf = fReceiveBufs[fReceiveBufIndex];
	buf.fFull = true;
	buf.fInUse = false;
	buf.fCount = buf.fList.GetSize();
	if (fFaxClass == 4)
		ReverseBits(buf.fBuffer, buf.fCount);
	fReceiveBufIndex ^= 1;
	if (fModemReply.fText[0])
	{
		// the page's data ended
		fFaxFlags = (fFaxFlags & ~kFaxFlagPageGood) | kFaxFlagPageDataEnded | kFaxFlagPageReceived;
		StartPhaseD();
	}
	else if (!fReceiveBufs[fReceiveBufIndex].fFull)
		ReceiveNextLinesBuf(&fReceiveBufs[fReceiveBufIndex]);
	NewtonErr err;
	if ((err = DecodeLinesBuf()) != noErr)
		StartAbort(err);
}


// ROM 0x000b6790 DecodeLinesBuf__8TFaxToolFv
// The received buffers fed to the decoder and decoded into the client's
// buffer a line at a time, as many as it takes (fLinesLeft) - the page's
// first end of line skipped, a bad line counted (and the last good one
// given again), the page's end (an empty line) noted.  A client's buffer
// that is a large object on a store (a large binary) is flushed every 50
// lines.  When the buffer is full or the page decoded, the get is
// answered.  DEVIATION: when the flush fails the ROM returns without
// removing its exception handler, leaving one that points into a frame
// that has gone; the host removes it.
NewtonErr
TFaxTool::DecodeLinesBuf()
{
	NewtonErr err = noErr;
	if (fClientBuffer == nil)
		return noErr;
	TFaxLineBuf* buf = &fReceiveBufs[fDecodeBufIndex];
	if (fReceiveState == 1 && !buf->fFull)
		return noErr;
	if (fLinesLeft > 0)
	{
		do
		{
			if (fReceiveState == 1)
			{
				int left = (int) buf->fCount, appended;
				Boolean room = fT4.AppendTo(&fReceiveBuffer, &left, &appended);
				buf->fCount = left;
				if (room)
				{
					// all of it taken: the next buffer
					buf->fCount = 0;
					buf->fFull = false;
					fDecodeBufIndex ^= 1;
					fReceiveBuffer = fReceiveBufs[fDecodeBufIndex].fBuffer;
					if (!(fFaxFlags & kFaxFlagPageDataEnded))
					{
						TFaxLineBuf& next = fReceiveBufs[fReceiveBufIndex];
						if (!next.fInUse && !next.fFull)
							ReceiveNextLinesBuf(&next);
					}
					if (fReceiveBufs[fDecodeBufIndex].fFull == true)
					{
						buf = &fReceiveBufs[fDecodeBufIndex];
						left = (int) buf->fCount;
						room = fT4.AppendTo(&fReceiveBuffer, &left, &appended);
						buf->fCount = left;
					}
					if (room)
					{
						if (fFaxFlags & kFaxFlagPageDataEnded)
							fReceiveState = 2;
						else
							break;
					}
				}
			}
			else if (fT4.GetLength() <= 0)
			{
				fFaxFlags &= ~kFaxFlagReceivingLines;
				fReceiveState = 3;
				break;
			}

			// a line
			if (fLines == 0)
			{
				if (fT4.SkipPastEOL())
					fLines++;
				continue;
			}
			int bytes;
			Boolean ok = fT4.DecodeLine(fDecodedLines[fDecodedIndex], fBytesPerLine, bytes, fReceiveState == 2);
			if (fFaxFlags & kFaxFlagRTC)
				continue;
			if (!ok)
			{
				fBadLines++;
				fDecodedIndex ^= 1;
			}
			else if (bytes == 0)
			{
				fFaxFlags |= kFaxFlagRTC;
				if (fReceiveState == 2)
				{
					fReceiveState = 3;
					break;
				}
				continue;
			}
			fLines++;
			Boolean thrown = false, flushFailed = false;
			newton_try
			{
				fClientBuffer->Putn(fDecodedLines[fDecodedIndex], fBytesPerLine);
				if (++fLinesSinceFlush > 50 && fReceiveState != 2)
				{
					fLinesSinceFlush = 0;
					if (fStore == nil)
					{
						CBufferSegment* segment = (CBufferSegment*) fClientBuffer->First();
						err = VAddrToStore(&fStore, &fObjectId, (ULong) (segment->fBufStart - 0x1c));
					}
					if (fStore != nil && (err = FlushLargeObject(fStore, fObjectId)) != noErr)
						flushFailed = true;
				}
			}
			newton_catch_all
			{
				thrown = true;
			}
			end_try;
			if (thrown)
				return kError_No_Memory;
			if (flushFailed)
				return err;
			fDecodedIndex ^= 1;
			fLinesLeft--;
			if (fProgressLines != 0 && fLines % fProgressLines == 0)
				PhaseCSendProgressEvent(fLines);
		} while (fLinesLeft > 0);
	}
	if (fLinesLeft == 0 || fReceiveState == 3)
	{
		fClientBuffer->Hide(fClientBuffer->GetSize() - fClientBuffer->Position(), kSeekFromEnd);
		GetComplete(noErr, fReceiveState == 3, fClientBuffer->GetSize());
	}
	return err;
}


// ROM 0x000b6b90 PhaseCSendProgressEvent__8TFaxToolFUl
void
TFaxTool::PhaseCSendProgressEvent(ULong lines)
{
	fProgressEvent = lines;
	PostFaxEvent();
}


// ROM 0x000b8d10 BufferNextLine__8TFaxToolFR11TFaxLineBuf
// The client's lines coded into a send buffer not with the modem, while it
// has room for a line and the band has lines; the band done, the put is
// answered.
NewtonErr
TFaxTool::BufferNextLine(TFaxLineBuf& buf)
{
	if (buf.fInUse)
		return noErr;
	while (fLinesLeft != 0 && buf.fCount <= fLineBufferSize)
	{
		Size got = fClientBuffer->Getn(fLineBuffer, fBandBytesPerLine);
		if (got != (Size) fBandBytesPerLine)
			return kError_No_Memory;
		int length = EncodeT4(fLineBuffer, (int) got, buf.fBuffer + buf.fCount, (int) fLineBufferSize,
							  0x6c0, (int) fBandLeftOffset, (int) fMinLineBytes);
		if (length < 0)
			return kCommErrBufferOverflow;
		buf.fCount += length;
		if (--fLinesLeft == 0)
			PutComplete(noErr, fClientBuffer->GetSize());
	}
	if ((Long) buf.fCount > 0)
		buf.fFull = true;
	return noErr;
}


// ROM 0x000b8e28 SendNextLine__8TFaxToolFv
// Both send buffers filled as far as they can be, the one in turn sent;
// the page ended and both empty, its RTC.
void
TFaxTool::SendNextLine()
{
	NewtonErr err;
	if ((err = BufferNextLine(SendBuf())) == noErr)
	{
		if (SendBuf().fFull && !SendBuf().fInUse && fPhase == kFaxPhaseC)
			PutMessage(0);
		if ((err = BufferNextLine(OtherSendBuf())) == noErr)
		{
			if (fFaxFlags & kFaxFlagEndPage)
				SendEOM();
			return;
		}
	}
	StartAbort(err);
}


// ROM 0x000b8c14 PutMessage__8TFaxToolFUc
// The send buffer in turn to the modem, then a wait of the minimum scan
// line time before the next (or 15 seconds for the modem's answer).
void
TFaxTool::PutMessage(UChar final)
{
	if (fFaxFlags & kFaxFlagSendLineWaiting)
		return;
	TFaxLineBuf& buf = SendBuf();
	NewtonErr err;
	if ((err = buf.fSegment.Init(buf.fBuffer, buf.fCount, false, 0, -1)) != noErr
	||  (err = PostTransPkt(kModemCtlC1SendData, fModulation, &buf.fList, 0, final)) != noErr)
	{
		StartAbort(err);
		return;
	}
	buf.fInUse = true;
	if (fMinScanLineTime != 0)
		PostTimer(kFaxTimerLineTime, fMinScanLineTime, kMilliseconds);
	else if (PostTimer(kFaxTimerDataLate, 15, kSeconds) == noErr)
		fFaxFlags |= kFaxFlagBlackout;
}


// ROM 0x000b8eec SendEOM__8TFaxToolFv
// The page's end once both send buffers are empty.
void
TFaxTool::SendEOM()
{
	if (SendBuf().fFull || SendBuf().fInUse)
		return;
	if (OtherSendBuf().fFull || OtherSendBuf().fInUse || fPhase != kFaxPhaseC)
		return;
	if (fFaxClass == 2)
		SendEOMCont();
	else if (fFaxClass == 4)
		C2StateUpdate(8);
	else if (fFaxClass == 8)
		C20StateUpdate(8);
	else
		StartAbort(kFaxToolErrProtocolError);
}


// ROM 0x000b8f98 SendEOMCont__8TFaxToolFv
// RTC sent as the page's last data.
void
TFaxTool::SendEOMCont()
{
	TFaxLineBuf& buf = SendBuf();
	buf.fList.Reset();
	buf.fCount = T4AddRTC(buf.fBuffer);
	NewtonErr err;
	if ((err = buf.fSegment.Init(buf.fBuffer, buf.fCount, false, 0, -1)) != noErr)
	{
		StartAbort(err);
		return;
	}
	buf.fFull = true;
	PutMessage(1);
	fFaxFlags = (fFaxFlags & ~kFaxFlagEndPage) | kFaxFlagEOMSent;
	fPages++;
}


// ROM 0x000b9084 ReceiveNextLinesBuf__8TFaxToolFP11TFaxLineBuf
// A receive buffer's worth of the page asked of the modem.
void
TFaxTool::ReceiveNextLinesBuf(TFaxLineBuf* buf)
{
	NewtonErr err;
	if ((err = buf->fSegment.Init(buf->fBuffer, fReceiveBufferSize, false, 0, -1)) == noErr)
	{
		CBufferList* list = &buf->fList;
		if (fFaxClass == 4 || fFaxClass == 8)
		{
			fModemRequest.fPacket.fData = list;
			fModemRequest.fPacket.fModulation = fModulation;
			fModemRequest.fPacket.fDuration = fReceiveTimeout;
			fModemRequest.fPacket.fFinal = false;
			list->Reset();
			err = PostModemCommand(kModemCtlC2RecvPageData);
		}
		else
			err = PostRecvPkt(kModemCtlC1RecvData, fModulation, list, fReceiveTimeout, false);
		if (err == noErr)
		{
			buf->fInUse = true;
			return;
		}
	}
	StartAbort(err);
}


/*------------------------------------------------------------------------------
	Phase D: the post-message procedure.
------------------------------------------------------------------------------*/

// ROM 0x000b7cd8 StartPhaseD__8TFaxToolFv
// Sending: MPS or EOP; receiving: the command awaited.
void
TFaxTool::StartPhaseD()
{
	if (fFaxClass == 4)
	{
		fPhase = kFaxPhaseClass2;
		C2StateUpdate(7);
	}
	else if (fFaxClass == 8)
	{
		fPhase = kFaxPhaseClass2;
		C20StateUpdate(7);
	}
	else
	{
		fPhase = kFaxPhaseD;
		if (fFaxFlags & kFaxFlagTransmitter)
		{
			fRetries = 3;
			fPhaseDStep = 1;
			PhaseDPutPostMsgCmd(0x37);
		}
		else
		{
			fPhaseDStep = 7;
			fPostMessage = 0;
			GetCommand();
		}
	}
}


// ROM 0x000b6c84 PhaseDPktComplete__8TFaxToolFl
void
TFaxTool::PhaseDPktComplete(NewtonErr result)
{
	if (fToolState & (kFaxToolStateModemRequest | kToolStateWantAbort))
		return;
	switch (fPhaseDStep)
	{
	case 2:
		PhaseDGetResponse(result);
		break;
	case 3:
		PhaseDProcessResponse(result);
		break;
	case 5:
		PhaseDWaitForSignalGone(result);
		break;
	case 7:
		GetCommandComplete(result);
		break;
	case 0xa:
		PhaseDPutMCF(result);
		break;
	case 0xd:
		PhaseDPutCRP(result);
		break;
	}
}


// ROM 0x000b6ce8 PhaseDGetResponse__8TFaxToolFl
void
TFaxTool::PhaseDGetResponse(NewtonErr result)
{
	if (result == noErr)
	{
		fPhaseDStep = 3;
		GetResponse();
	}
	else
		RetransCommand(0);
}


// ROM 0x000b6d00 PhaseDProcessResponse__8TFaxToolFl
void
TFaxTool::PhaseDProcessResponse(NewtonErr result)
{
	switch (result)
	{
	case kModemErrNoCarrier:
	case kModemErrRcvPktFlagsTimeOut:
	case kModemErrCommandFailure:
		KillTimer();
		fPhaseDStep = 1;
		RetransCommand(0xc8);
		return;
	case kModemErrNoFaxCarrier:
	case kSerErr_CRCError:
		KillTimer();
		RetransCommand(0xd2);
		return;
	case kCommErrRequestCanceled:
		return;
	case noErr:
		if (fFaxFlags & kFaxFlagLastPage)
			PhaseDProcessEOPResponse();
		else
			PhaseDProcessMPSResponse();
		return;
	}
	StartAbort(result);
}


// ROM 0x000b6dc0 PhaseDProcessEOPResponse__8TFaxToolFv
// EOP answered: MCF (or RTP, PIP) and the page is done; RTN or PIN, the
// speed down and the page (the client told kFaxToolErrTransmissionFailed)
// to be sent again; CRP and EOP goes again.
void
TFaxTool::PhaseDProcessEOPResponse()
{
	NewtonErr pageResult = noErr;
	NewtonErr err;
	switch (fFrame[2])
	{
	case 0x8c:
	case 0x8d:
	case 0xac:
	case 0xad:
	case 0xcc:
	case 0xcd:
		fPhaseDStep = 6;
		break;
	case 0x1a:
	case 0x1b:
		fPhaseDStep = 5;
		break;
	case 0x2c:
	case 0x2d:
	case 0x4c:
	case 0x4d:
		pageResult = kFaxToolErrTransmissionFailed;
		if ((err = AdjustSpeedForFTT()) != noErr)
		{
			StartAbort(err);
			return;
		}
		fPhaseDStep = 8;
		break;
	case 0xfa:
	case 0xfb:
		fPhaseDStep = 0xb;
		break;
	}

	if (fFrame[1] == 0x13)
	{
		KillTimer();
		switch (fPhaseDStep)
		{
		case 8:
			fRetries = 3;
			fTrainTries = 1;
			// fall through
		case 6:
			EndPageComplete(pageResult);
			return;
		case 5:
			RetransCommand(0xd2);
			return;
		case 0xb:
			fToolState &= ~kFaxToolStatePhaseE;
			break;
		}
		StartAbort(kFaxToolErrNoRemoteSignal);
	}
	else if (fPhaseDStep == 3)
	{
		if ((err = PostRecvPkt(kModemCtlC1RecvHDLC, 1, &fFrameList, 3250, false)) != noErr)
			StartAbort(err);
	}
	else
		StartAbort(kFaxToolErrNoRemoteSignal);
}


// ROM 0x000b6f70 PhaseDProcessMPSResponse__8TFaxToolFv
// MPS answered: MCF and the next page follows; RTN or PIN, the speed down
// and a retrain first (RTP and PIP a retrain at the same speed); CRP and
// MPS goes again.
void
TFaxTool::PhaseDProcessMPSResponse()
{
	NewtonErr pageResult = noErr;
	NewtonErr err;
	switch (fFrame[2])
	{
	case 0x8c:
	case 0x8d:
		fPhaseDStep = 9;
		break;
	case 0x1a:
	case 0x1b:
		fPhaseDStep = 5;
		break;
	case 0x2c:
	case 0x2d:
	case 0x4c:
	case 0x4d:
		pageResult = kFaxToolErrTransmissionFailed;
		if ((err = AdjustSpeedForFTT()) != noErr)
		{
			StartAbort(err);
			return;
		}
		fPhaseDStep = 8;
		break;
	case 0xac:
	case 0xad:
	case 0xcc:
	case 0xcd:
		fPhaseDStep = 8;
		break;
	case 0xfa:
	case 0xfb:
		fPhaseDStep = 0xb;
		break;
	}

	if (fFrame[1] == 0x13)
	{
		KillTimer();
		switch (fPhaseDStep)
		{
		case 8:
			fRetries = 3;
			fTrainTries = 1;
			// fall through
		case 9:
			EndPageComplete(pageResult);
			return;
		case 5:
			RetransCommand(0xd2);
			return;
		case 0xb:
			fToolState &= ~kFaxToolStatePhaseE;
			break;
		}
		StartAbort(kFaxToolErrNoRemoteSignal);
	}
	else if (fPhaseDStep == 3)
	{
		if ((err = PostRecvPkt(kModemCtlC1RecvHDLC, 1, &fFrameList, 3250, false)) != noErr)
			StartAbort(err);
	}
	else
		StartAbort(kFaxToolErrNoRemoteSignal);
}


// ROM 0x000b7120 PhaseDWaitForSignalGone__8TFaxToolFl
void
TFaxTool::PhaseDWaitForSignalGone(NewtonErr result)
{
	switch (result)
	{
	case kSerErr_CRCError:
	case kModemErrNoFaxCarrier:
	case noErr:
		if ((result = PostRecvPkt(kModemCtlC1RecvHDLC, 1, &fFrameList, 3250, true)) == noErr)
			return;
		break;
	case kModemErrNoCarrier:
		KillTimer();
		fPhaseDStep = 1;
		RetransCommand(0xc8);
		return;
	case kCommErrRequestCanceled:
		return;
	}
	StartAbort(result);
}


// ROM 0x000b7204 PhaseDPutPostMsgCmd__8TFaxToolFUl
// EOP after the last page, MPS after any other.
void
TFaxTool::PhaseDPutPostMsgCmd(ULong duration)
{
	fPhaseDStep = 2;
	fLastCommand = kFaxCmdPostMessage;
	UChar fcf;
	if (fFaxFlags & kFaxFlagLastPage)
		fcf = (fFaxFlags & kFaxFlagCaller) ? 0x2f : 0x2e;
	else
		fcf = (fFaxFlags & kFaxFlagCaller) ? 0x4f : 0x4e;
	NewtonErr err;
	if ((err = BuildControlFrame(fcf, nil, 0, true)) != noErr
	||  (err = PostTransPkt(kModemCtlC1SendHDLC, 1, &fSendList, duration, true)) != noErr)
		StartAbort(err);
}


// ROM 0x000b72b8 PhaseDProcessCommand__8TFaxToolFv
// The receiver's post-message command: MPS, EOP or EOM (priority or not),
// answered once the client has had the page ('feom') - or, if it has not
// yet, after it has had 2.25 seconds more.
void
TFaxTool::PhaseDProcessCommand()
{
	NewtonErr err = noErr;
	switch (fFrame[2])
	{
	case 0x2e:
	case 0x2f:
	case 0x3e:
	case 0x3f:
		fFaxFlags |= kFaxFlagPostMessage;
		fPhaseDStep = 0xa;
		fPostMessage = kFaxPostEOP;
		break;
	case 0x4e:
	case 0x4f:
	case 0x5e:
	case 0x5f:
		fFaxFlags |= kFaxFlagPostMessage;
		fPhaseDStep = 0xa;
		fPostMessage = kFaxPostMPS;
		break;
	case 0x8e:
	case 0x8f:
	case 0x9e:
	case 0x9f:
		fFaxFlags |= kFaxFlagPostMessage;
		fPhaseDStep = 0xa;
		fPostMessage = kFaxPostEOM;
		break;
	case 0x41:
	case 0x43:
	case 0x80:
	case 0x81:
	case 0x82:
	case 0x83:
		err = kFaxToolErrProtocolError;
		fPhaseDStep = 0xb;
		break;
	default:
		err = kFaxToolErrNoRemoteSignal;
		fPhaseDStep = 0xb;
		break;
	}

	if (fFrame[1] != 0x13)
	{
		StartAbort(kFaxToolErrNoRemoteSignal);
		return;
	}
	KillTimer();
	if (fPhaseDStep == 0xb)
	{
		if (fFaxFlags & kFaxFlagEndMessage)
		{
			fFaxFlags &= ~kFaxFlagEndMessage;
			((TOptionExtended*) fOptionsInfo.fCurOptPtr)->SetExtendedResult(noErr);
			ProcessOptionComplete(opSuccess);
		}
		fToolState &= ~kFaxToolStatePhaseE;
		StartAbort(err);
		return;
	}
	if (!(fFaxFlags & kFaxFlagPageReceived))
	{
		PhaseDProcessReceivedPageConfirmation();
		return;
	}
	if ((err = PostTimer(kFaxTimerBlackout, 2250, kMilliseconds)) != noErr)
		StartAbort(err);
}


// ROM 0x000b7458 CRPRetransmitTimeOut__8TFaxToolFv
void
TFaxTool::CRPRetransmitTimeOut()
{
	fPhaseDStep = 0xd;
	PhaseBPutPostMsgCmd(0x1a, 0x48);
}


// ROM 0x000b75b0 PhaseDPutCRP__8TFaxToolFl
void
TFaxTool::PhaseDPutCRP(NewtonErr result)
{
	fPhaseDStep = 0xa;
	if (!(fFaxFlags & kFaxFlagPageReceived))
	{
		PhaseDProcessReceivedPageConfirmation();
		return;
	}
	NewtonErr err;
	if ((err = PostTimer(kFaxTimerCRP, 1000, kMilliseconds)) != noErr)
		StartAbort(err);
}


// ROM 0x000b7608 PhaseDPutMCF__8TFaxToolFl
// The page's confirmation sent: after MPS and MCF the next page; after EOP
// the DCN awaited; after EOM (or a bad page) phase B again.
void
TFaxTool::PhaseDPutMCF(NewtonErr result)
{
	if (result != noErr)
	{
		RetransCommand(0x48);
		return;
	}
	KillTimer();
	switch (fPostMessage)
	{
	case kFaxPostEOM:
		ReStartPhaseB();
		return;
	case kFaxPostEOP:
		if (fFaxFlags & kFaxFlagPageBad)
		{
			ReStartPhaseB();
			return;
		}
		fPhaseDStep = 7;
		GetCommand();
		return;
	case kFaxPostMPS:
		fPostMessage = 0;
		if (fFaxFlags & kFaxFlagPageGood)
		{
			fPhase = kFaxPhaseC;
			StartPhaseC();
		}
		else
			ReStartPhaseB();
		return;
	}
	StartAbort(kFaxToolErrProtocolError);
}


// ROM 0x000b76ac PhaseDProcessReceivedPageConfirmation__8TFaxToolFv
// The client has had the page ('feom': whether it kept it, and told whether
// it was the last): the page's quality judged and confirmed - MCF, RTP or
// RTN - or, after EOP in a retrain, the training check again.
void
TFaxTool::PhaseDProcessReceivedPageConfirmation()
{
	if (fFaxFlags & kFaxFlagPageGood)
	{
		fRetries = 3;
		PhaseBPutPostMsgCmd(0x8c, 0x48);
		return;
	}
	TCMOFaxEndMessage* option = (TCMOFaxEndMessage*) fOptionsInfo.fCurOptPtr;
	if (!option->fPageAccepted)
	{
		KillTimer();
		StartAbort(kError_No_Memory);
		return;
	}
	option->fLastPage = (fPostMessage == kFaxPostEOP);
	NewtonErr err;
	switch (fPhaseDStep)
	{
	case 6:
		err = AllocateTCBuffer(1);
		fTCCount = 0;
		fTCZeroRun = 0;
		fTCLongestRun = 0;
		if (err != noErr)
		{
			StartAbort(err);
			return;
		}
		fPhase = kFaxPhaseB;
		GetTrainingCheck();
		return;
	case 7:
		return;
	case 0xa:
		{
			KillTimer();
			fRetries = 3;
			UChar fcf = CopyQualityResponse();
			switch (fcf)
			{
			case 0x8c:
			case 0x8d:
				fFaxFlags |= kFaxFlagPageGood;
				break;
			case 0x4c:
			case 0x4d:
				fFaxFlags |= kFaxFlagPageBad;
				break;
			case 0xcc:
			case 0xcd:
				fFaxFlags |= kFaxFlagPageRetrain;
				break;
			}
			fLastCommand = kFaxCmdResponse;
			fRetries = 3;
			PhaseBPutPostMsgCmd(fcf, 0x48);
		}
		return;
	}
}


// ROM 0x000b94ac PhaseDBlackoutTimeout__8TFaxToolFv
void
TFaxTool::PhaseDBlackoutTimeout()
{
	fPhaseDStep = 7;
	GetCommand();
}


// ROM 0x000b8170 CopyQualityResponse__8TFaxToolFv
// MCF if at most 5.2% of the page's lines were bad, RTP up to 15.2%, RTN
// beyond.
UChar
TFaxTool::CopyQualityResponse()
{
	ULong caller = fFaxFlags & kFaxFlagCaller;
	if (fLines != 0)
	{
		ULong perMille = (fBadLines * 1000) / fLines;
		if (perMille > 0x34)
		{
			if (perMille > 0x98)
				return caller ? 0x4d : 0x4c;
			return caller ? 0xcd : 0xcc;
		}
	}
	return caller ? 0x8d : 0x8c;
}


/*------------------------------------------------------------------------------
	Phase E: the release.
------------------------------------------------------------------------------*/

// ROM 0x000b8510 StartPhaseE__8TFaxToolFv
// DCN sent, the modem request cancelled three seconds later if it has not
// answered (a termination proc: true when it could not be started).
Boolean
TFaxTool::StartPhaseE()
{
	fPhase = kFaxPhaseE;
	fToolState &= ~kFaxToolStatePhaseE;
	if (BuildControlFrame((fFaxFlags & kFaxFlagCaller) ? 0xfb : 0xfa, nil, 0, true) != noErr
	||  PostTransPkt(kModemCtlC1SendHDLC, 1, &fSendList, 10, true) != noErr
	||  PostTimer(kFaxTimerHangUp, 3000, kMilliseconds) != noErr)
		return true;
	return false;
}


// ROM 0x000b77fc PhaseEPktComplete__8TFaxToolFl
void
TFaxTool::PhaseEPktComplete(NewtonErr result)
{
	KillTimer();
	if (result == kCommErrRequestCanceled)
		return;
	TerminateConnection();
}
