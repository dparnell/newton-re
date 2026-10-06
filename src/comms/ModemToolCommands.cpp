/*
	File:		comms/ModemToolCommands.cpp

	Contains:	TClassOneModem's commands and the modem's answers
				(ModemTool.h): building a command, putting it, reading the
				answer a line at a time and parsing it.

	Reconstructed from the MP2x00 US ROM (0x0005cd1c-0x0005d294,
	0x0005d558-0x0005d5d8, 0x0005ed4c-0x0005edfc, 0x0005f1ec-0x0005f214,
	0x00063294-0x000632bc, 0x0006345c-0x00064eb8); each function cites its
	origin.
*/

#include "ModemTool.h"
#include "CommErrors.h"
#include "NewtErrors.h"
#include "host/RomBugs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// ROM 0x0005edb4 reverse__FPUc
static void
reverse(UChar* s)
{
	ULong j = strlen((char*) s) - 1;
	if (j == 0)
		return;
	for (ULong i = 0; i < j; i++, j--)
	{
		UChar c = s[i];
		s[i] = s[j];
		s[j] = c;
	}
}


// ROM 0x0005ed4c UiToA__FUlPUc
// ROM BUG (fixed): each digit is taken from the low byte of what is left
// (n & 0xff, then % 10), so a number above 255 comes out wrong - 300 as
// "304".  The modem's S-registers and silences rarely need more.  The fix
// takes each digit from the whole number.
void
UiToA(ULong n, UChar* s)
{
	ULong i = 0;
	do
	{
		s[i++] = (RomBugFixed() ? (n % 10) : ((n & 0xff) % 10)) + '0';
		n /= 10;
	} while (n > 0);
	s[i] = 0;
	reverse(s);
}


// ROM 0x00063294 BeginModemCommand__14TClassOneModemFUl
NewtonErr
TClassOneModem::BeginModemCommand(ULong command)
{
	NewtonErr err = PrepareCommand(command);
	if (err == noErr)
		PutCommand();
	return err;
}


// ROM 0x000637dc BuildCommand__14TClassOneModemFPCUcPUcUlT2T3T2T3
// The command list: the command and up to three pieces after it, each its
// given length or, given none, its C string's.  (The prefix and suffix are
// PrepareCommand's.)
NewtonErr
TClassOneModem::BuildCommand(const UChar* command, UChar* arg1, ULong length1, UChar* arg2, ULong length2,
							 UChar* arg3, ULong length3)
{
	NewtonErr err;
	if ((err = fCmdList.RemoveAll()) != noErr)
		return err;
	if ((err = fCmdSegment.Init((void*) command, strlen((const char*) command))) != noErr)
		return err;
	if ((err = fCmdList.InsertLast(&fCmdSegment)) != noErr)
		return err;
	if (arg1 == nil)
		return err;
	if ((err = fCmdArg1.Init(arg1, length1 != 0 ? length1 : strlen((char*) arg1))) != noErr)
		return err;
	fCmdList.InsertLast(&fCmdArg1);
	if (arg2 == nil)
		return err;
	if ((err = fCmdArg2.Init(arg2, length2 != 0 ? length2 : strlen((char*) arg2))) != noErr)
		return err;
	fCmdList.InsertLast(&fCmdArg2);
	if (arg3 == nil)
		return err;
	if ((err = fCmdArg3.Init(arg3, length3 != 0 ? length3 : strlen((char*) arg3))) == noErr)
		fCmdList.InsertLast(&fCmdArg3);
	return err;
}


// ROM 0x0006399c PrepareCommand__14TClassOneModemFUl
// The command numbered (kModemCmd...) built into fCmdList between "AT" and
// the carriage return, with the time its answer is to be waited for.  A
// command of text made up here is made up in fResultBuffer (the answer's
// buffer, free until the command has gone).
NewtonErr
TClassOneModem::PrepareCommand(ULong command)
{
	NewtonErr err;
	if (command == kModemCmdDial)
	{
		// the number, as much of it as the modem takes at once (the rest
		// dialed after a ";" by the next dial command)
		ULong count = fPhoneNumberLength - fDialled;
		Long limit = fProfile->fMaxCommandLength;
		if (limit >= 0x80)
			limit = 0x80;
		UChar* more = (UChar*) dialModReturn;
		if (count > (ULong) (limit - 2))
			count = limit - 3;
		else if (!(fTAPIService.fActive && fTAPIService.fActive2))
			more = nil;
		const UChar* dial = (const UChar*) (fDialing.fDTMFToneDialing ? cmdDialTone : cmdDialPulse);
		if (fPhoneNumber == nil)
			err = BuildCommand(dial, more, 0, nil, 0, nil, 0);
		else
		{
			ULong length;
			if ((fFaxEnabledCaps.fServiceClasses & 0x800000) == 0 && fPrefs.fStripDashes)
			{
				fDialString[0] = 0;
				length = 0;
				for (ULong i = 0; i < count; i++)
				{
					UChar c = fPhoneNumber[fDialled + i];
					if (c != '-')
						fDialString[length++] = c;
				}
			}
			else
			{
				memcpy(fDialString, fPhoneNumber + fDialled, count);
				length = count;
			}
			err = BuildCommand(dial, fDialString, length, more, 0, nil, 0);
		}
		fDialled += count;
		if (fTAPIService.fActive && fTAPIService.fActive2)
		{
			ULong wait = fDialing.fWaitBeforeBlindDial;
			if (wait <= 4)
				wait = 4;
			fCommandTimeout = wait * 1000 + count * fDialing.fCommaDelay * 1000 + 6000;
		}
		else
			fCommandTimeout = fDialing.fWaitForCarrier * 1000 + 60000;
	}
	else
	{
		if (command < 0x1f)
			fCommandTimeout = fProfile->fCommandTimeout;
		UChar* text = fResultBuffer;
		UChar* bytes = fControl.fBytes;
		const char* plain = nil;			// a command of a fixed string
		const char* withByte = nil;			// a command and one character
		switch (command)
		{
		case kModemCmdRecallFactory:
			plain = cmdRecallFactorySettings;
			break;
		case kModemCmdSetFaxClass:
		{
			const char* fclass;
			switch (fFaxClass.fClass)
			{
			case kModemFaxClass1:	fclass = "1"; break;
			case kModemFaxClass2:	fclass = "2"; break;
			case kModemFaxClass20:	fclass = "2.0"; break;
			default:				fclass = "0"; break;
			}
			sprintf((char*) text, cmdFClass, fclass);
			break;
		}
		case kModemCmdHangUp:
			// a data connection the modem was pacing hung up (not at the
			// speed it is spoken to at, or with MNP 10) is given 20 seconds
			if (!(fTAPIService.fActive && fTAPIService.fActive2)
			&&  (fModemFlags & kModemFlagDataClass)
			&&  (fModemFlags & (kModemFlagHangUpSent | kModemFlagHungUp))
			&&  (fToolState & kToolStateConnected)
			&&  (fSerialSpeed != fConnectSpeed || (fECType & 8) != 0))
				fCommandTimeout = 20000;
			plain = cmdHangUp;
			break;
		case kModemCmdAnswer:
			fCommandTimeout = 0;
			plain = cmdAnswer;
			break;
		case kModemCmdOnLine:
			fCommandTimeout = 0;
			plain = cmdOriginate;
			break;
		case kModemCmdIdentify0:
			plain = cmdIdentify;
			break;
		case kModemCmdIdentify3:
			plain = cmdIdentify3;
			break;
		case kModemCmdIdentify4:
			plain = cmdIdentify4;
			break;
		case kModemCmdIdentify5:
			plain = cmdIdentify5;
			break;
		case kModemCmdSecondaryDefaults:
			plain = cmdSecondaryDefaults;
			break;
		case kModemCmdC1Silence:
			UiToA(fControl.fPacket.fDuration / 10 + 1, fCmdParm1);
			fCommandTimeout = fControl.fPacket.fDuration + 500;
			err = BuildCommand((const UChar*) cmdC1FTS, fCmdParm1, 0, nil, 0, nil, 0);
			goto built;
		case kModemCmdC1TransmitData:
			err = BuildCommand((const UChar*) cmdC1FTM, (UChar*) GetModParamStr(fControl.fPacket.fModulation), 0, nil, 0, nil, 0);
			fCommandTimeout = 3200;
			goto built;
		case kModemCmdC1ReceiveData:
			err = BuildCommand((const UChar*) cmdC1FRM, (UChar*) GetModParamStr(fControl.fPacket.fModulation), 0, nil, 0, nil, 0);
			fCommandTimeout = 3200;
			goto built;
		case kModemCmdC1TransmitHDLC:
			err = BuildCommand((const UChar*) cmdC1FTH, (UChar*) GetModParamStr(fControl.fPacket.fModulation), 0, nil, 0, nil, 0);
			fCommandTimeout = 3200;
			goto built;
		case kModemCmdC1ReceiveHDLC:
			err = BuildCommand((const UChar*) cmdC1FRH, (UChar*) GetModParamStr(fControl.fPacket.fModulation), 0, nil, 0, nil, 0);
			fCommandTimeout = fControl.fPacket.fDuration;
			goto built;
		case kModemCmdAttention:
			fCmdList.RemoveAll();
			goto framed;
		case kModemCmdSetSRegister:
			UiToA(fSRegister, fCmdParm1);
			UiToA(fSRegisterValue, fCmdParm2);
			err = BuildCommand((const UChar*) cmdSRegister, fCmdParm1, 0, (UChar*) cmdEqual, 0, fCmdParm2, 0);
			goto built;
		case kModemCmdReadSRegister:
			UiToA(fSRegister, fCmdParm1);
			err = BuildCommand((const UChar*) cmdSRegister, fCmdParm1, 0, (UChar*) cmdReport, 0, nil, 0);
			goto built;
		case kModemCmdQueryFaxClass:
			sprintf((char*) text, cmdFClass, cmdReport);
			break;
		case kModemCmdQueryC1TransmitData:
			err = BuildCommand((const UChar*) cmdC1FTM, (UChar*) cmdReport, 0, nil, 0, nil, 0);
			goto built;
		case kModemCmdQueryC1TransmitHDLC:
			err = BuildCommand((const UChar*) cmdC1FTH, (UChar*) cmdReport, 0, nil, 0, nil, 0);
			goto built;
		case kModemCmdQueryC1ReceiveData:
			err = BuildCommand((const UChar*) cmdC1FRM, (UChar*) cmdReport, 0, nil, 0, nil, 0);
			goto built;
		case kModemCmdQueryC1ReceiveHDLC:
			err = BuildCommand((const UChar*) cmdC1FRH, (UChar*) cmdReport, 0, nil, 0, nil, 0);
			goto built;
		case kModemCmdSpeaker:
			plain = fTAPISpeaker.fSpeakerOn ? cmdSpeakerOn : cmdSpeakerOff;
			break;
		case kModemCmdTAPIOffHook:
			plain = cmdTAPIOffHook;
			break;
		case kModemCmdTAPIOnHook:
			// the X code: 1 blind and deaf to busy, 2 dial tone, 3 busy, 4 both
			if (fDialing.fDetectDialTone)
				fCmdParm1[0] = fDialing.fDetectBusy ? '4' : '2';
			else
				fCmdParm1[0] = fDialing.fDetectBusy ? '3' : '1';
			withByte = cmdTAPIOnHook;
			bytes = fCmdParm1;
			break;
		case kModemCmdTAPIBlindDial:
			plain = cmdTAPITurnOnBlindDial;
			break;

		// Class 2 and 2.0
		case 0x20:	plain = cmdC2FAA; break;
		case 0x21:	withByte = cmdC2FBOR; break;
		case 0x22:	sprintf((char*) text, cmdC2FCIG, bytes); break;
		case 0x23:	withByte = cmdC2FCQ; break;
		case 0x24:	plain = cmdC2FCQ_Q; break;
		case 0x25:	withByte = cmdC2FCR; break;
		case 0x26:	plain = cmdC2FCR_Q; break;
		case 0x27:	sprintf((char*) text, cmdC2FDCC, bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7]); break;
		case 0x28:	plain = cmdC2FDCC_Q; break;
		case 0x29:	withByte = cmdC2FDFFC; break;
		case 0x2a:	plain = cmdC2FDFFC_Q; break;
		case 0x2b:	sprintf((char*) text, cmdC2FDIS, bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7]); break;
		case 0x2c:	plain = cmdC2FDR; break;
		case 0x2d:	plain = cmdC2FDT; break;
		case 0x2e:	withByte = cmdC2FECM; break;
		case 0x2f:	plain = cmdC2FECM_Q; break;
		case 0x30:	withByte = cmdC2FET; break;
		case 0x31:	plain = cmdC2FK; break;
		case 0x32:	sprintf((char*) text, cmdC2FLID, bytes); break;
		case 0x33:	withByte = cmdC2FLNFC; break;
		case 0x34:	plain = cmdC2FLNFC_Q; break;
		case 0x35:	withByte = cmdC2FLPL; break;
		case 0x36:	plain = cmdC2FLPL_Q; break;
		case 0x37:	plain = cmdC2FMDL_Q; break;
		case 0x38:	plain = cmdC2FMFR_Q; break;
		case 0x39:	withByte = cmdC2FMINSP; break;
		case 0x3a:	withByte = cmdC2FPTS; break;
		case 0x3b:	withByte = cmdC2FREL; break;
		case 0x3c:	plain = cmdC2FREV_Q; break;
		case 0x3d:	withByte = cmdC2FSPL; break;
		case 0x3e:	withByte = cmdC2FVRFC; break;
		case 0x3f:	plain = cmdC2FVRFC_Q; break;
		case 0x40:	withByte = cmdC2FWDFC; break;
		case 0x41:	plain = cmdC2FWDFC_Q; break;
		case 0x42:	plain = cmdC20FDT; break;
		case 0x43:	plain = cmdC20FDR; break;
		case 0x44:	plain = cmdC20FKS; break;
		case 0x45:	withByte = cmdC20FIP; break;
		case 0x46:	plain = cmdC20FMI; break;
		case 0x47:	plain = cmdC20FMM; break;
		case 0x48:	plain = cmdC20FMR; break;
		case 0x49:	sprintf((char*) text, cmdC20FCC, bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7]); break;
		case 0x4a:	plain = cmdC20FCC_Q; break;
		case 0x4b:	sprintf((char*) text, cmdC20FIS, bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7]); break;
		case 0x4c:	plain = cmdC20FCS; break;
		case 0x4d:	sprintf((char*) text, cmdC20FLI, bytes); break;
		case 0x4e:	sprintf((char*) text, cmdC20FPI, bytes); break;
		case 0x4f:	withByte = cmdC20FLP; break;
		case 0x50:	plain = cmdC20FLP_Q; break;
		case 0x51:	withByte = cmdC20FSP; break;
		case 0x52:	plain = cmdC20FSP_Q; break;
		case 0x53:	sprintf((char*) text, cmdC20FNR, bytes[0], bytes[1], bytes[2], bytes[3]); break;
		case 0x54:	withByte = cmdC20FIE; break;
		case 0x55:	withByte = cmdC20FPS; break;
		case 0x56:
			if (bytes[0] != '?')
				sprintf((char*) text, cmdC20FCQ_2, bytes[0], bytes[1]);
			else
				sprintf((char*) text, cmdC20FCQ, bytes[0]);
			break;
		case 0x57:	plain = cmdC20FCQ_Q; break;
		case 0x58:	withByte = cmdC20FLO; break;
		case 0x59:	withByte = cmdC20FPR; break;
		case 0x5a:	withByte = cmdC20FPP; break;
		case 0x5b:	withByte = cmdC20FBO; break;
		case 0x5c:
			// ROM BUG (fixed): the "+FEA=" built here is thrown away - the
			// case runs on into the next, whose "+FCR=" replaces it.  The
			// fix sends the "+FEA=".
			if (RomBugFixed())
			{
				withByte = cmdC20FEA;
				break;
			}
			BuildCommand((const UChar*) cmdC20FEA, bytes, 1, nil, 0, nil, 0);
			withByte = cmdC20FCR;
			break;
		case 0x5e:	withByte = cmdC20FCR; break;
		case 0x5f:	plain = cmdC20FCR_Q; break;
		case 0x60:	withByte = cmdC20FBU; break;
		case 0x61:	sprintf((char*) text, cmdC20FRQ, bytes[0], bytes[1]); break;
		case 0x62:	withByte = cmdC20FAA; break;
		case 0x63:	withByte = cmdC20FCT; break;
		case 0x64:	plain = cmdC20FHS; break;
		case 0x65:	withByte = cmdC20FRY; break;
		case 0x66:	withByte = cmdC20FMS; break;
		case 0x67:	sprintf((char*) text, cmdC20FFC, bytes[0], bytes[1], bytes[2], bytes[3]); break;
		case 0x68:	plain = cmdC20FBS; break;

		default:	// 0, 0xd, 0x12, 0x1f, 0x5d and beyond
			return kCommErrBadCommand;
		}
		if (withByte != nil)
			err = BuildCommand((const UChar*) withByte, bytes, 1, nil, 0, nil, 0);
		else
			err = BuildCommand(plain != nil ? (const UChar*) plain : text, nil, 0, nil, 0, nil, 0);
	}
built:
	if (err != noErr)
		return err;
framed:
	fCmdList.InsertFirst(&fCmdPrefix);
	fCmdList.InsertLast(&fCmdSuffix);
	return noErr;
}


// ROM 0x0006345c PutCommand__14TClassOneModemFv
// The command put - but no sooner than the command delay (fNextCommandDelay
// once, else the profile's) after the modem's last answer: until then a
// timer.  A packet's command goes at once.
void
TClassOneModem::PutCommand()
{
	if ((fModemFlags & (kModemFlagPacketPut | kModemFlagPacketGet)) == 0)
	{
		if (fNextCommandDelay == 0)
			fCommandDelayTime = TTime(fProfile->fCommandDelay, kMilliseconds);
		else
		{
			fCommandDelayTime = TTime(fNextCommandDelay, kMilliseconds);
			fNextCommandDelay = 0;
		}
		TTime elapsed = GetGlobalTime();
		CompSub(&fLastResultTime.time, &elapsed.time);
		if (CompCompare(&elapsed.time, &fCommandDelayTime.time) < 0)
		{
			TTime left = fCommandDelayTime;
			CompSub(&elapsed.time, &left.time);
			NewtonErr err = PostTimer(kModemTimerPutCommand, left.ConvertTo(kMilliseconds));
			if (err != noErr)
				PutCommandComplete(err);
			return;
		}
	}
	FlushInputBytes();
	fModemFlags |= kModemFlagPutting;
	TSerTool::PutBytes(&fCmdList);
}


// ROM 0x000635c8 PutCommandComplete__14TClassOneModemFl
void
TClassOneModem::PutCommandComplete(NewtonErr result)
{
	fModemFlags &= ~kModemFlagPutting;
	if (fModemFlags & kModemFlagAborting)
		return;
	if (result == noErr)
		GetCommandResult();
	else
		ModemCommandComplete(result);
}


// ROM 0x0005d558 PutEscapeCmd__14TClassOneModemFv
// "+++" alone, to bring the modem back to its commands.
NewtonErr
TClassOneModem::PutEscapeCmd()
{
	fCmdList.RemoveAll();
	NewtonErr err = fCmdSegment.Init((void*) cmdEscape2CmdMode, strlen(cmdEscape2CmdMode));
	if (err == noErr)
	{
		fCmdList.InsertLast(&fCmdSegment);
		PutCommand();
	}
	return err;
}


// ROM 0x00064ca4 GetCommandResult__14TClassOneModemFv
// The next line of the modem's answer read, a byte at a time, within the
// command timeout (if there is one).
void
TClassOneModem::GetCommandResult()
{
	if (fCommandTimeout != 0)
	{
		NewtonErr err = PostTimer(kModemTimerCommand, fCommandTimeout);
		if (err != noErr)
		{
			ModemCommandComplete(err);
			return;
		}
	}
	fModemFlags |= kModemFlagGetting;
	fField5C8 = 1;
	fResultLength = 0;
	fResultList.Reset();
	TSerTool::GetBytes(&fResultList);
}


// ROM 0x00064d24 GetCommandResultComplete__14TClassOneModemFl
// A byte of the answer: carriage returns and XON/XOFF ignored, the line
// ended by a line feed (an empty line, or the modem echoing an "AT"
// command, skipped), then parsed - "CONNECT" alone is 300 bps.
void
TClassOneModem::GetCommandResultComplete(NewtonErr result)
{
	Boolean lineDone = false;
	fModemFlags &= ~kModemFlagGetting;
	if (fModemFlags & kModemFlagAborting)
		return;
	if (result == noErr)
	{
		fResultList.Seek(0, kSeekFromBeginning);
		UByte c = fResultList.Get();
		if (c == 0x0d || c == 0x11 || c == 0x13)
			;
		else if (c == 0x0a)
		{
			if (fResultLength != 0)
			{
				if (fResultLength >= 2
				&&  fResultBuffer[0] == (UChar) cmdPrefix[0] && fResultBuffer[1] == (UChar) cmdPrefix[1])
					fResultLength = 0;
				else
					lineDone = true;
			}
		}
		else if (fResultLength < 0x100)
			fResultBuffer[fResultLength++] = c;
		if (lineDone)
		{
			// ROM BUG (fixed): a line of 0x100 characters has its
			// terminator written past the buffer (into fModemIdStrings).
			// The fix writes it over the last character instead.
			if (RomBugFixed() && fResultLength >= sizeof(fResultBuffer))
				fResultLength = sizeof(fResultBuffer) - 1;
			fResultBuffer[fResultLength] = 0;
			ParseModemRsp(fResultBuffer);
			if (fReply.fResultCode == kModemResultConnect)
				fConnectSpeed = 300;
			else if (fReply.fResultCode == kModemResultConnectN)
			{
				fConnectSpeed = atoi((char*) fResultBuffer + strlen(resultConnectN));
				fReply.fResultCode = kModemResultConnect;
			}
			ModemCommandComplete(noErr);
			return;
		}
	}
	else if (result != kSerErr_AsyncError)
	{
		ModemCommandComplete(result);
		return;
	}
	fModemFlags |= kModemFlagGetting;
	fResultList.Reset();
	TSerTool::GetBytes(&fResultList);
}


// ROM 0x0005f1ec TimeOutCmdResult__14TClassOneModemFv
void
TClassOneModem::TimeOutCmdResult()
{
	AbortCommand();
	ModemCommandComplete(kModemErrNoResponse);
}


// ROM 0x000632bc AbortCommand__14TClassOneModemFv
void
TClassOneModem::AbortCommand()
{
	fModemFlags |= kModemFlagAborting;
	AbortTimer();
	if (fModemFlags & kModemFlagPutting)
		TFramedAsyncSerTool::KillPut();
	if (fModemFlags & kModemFlagGetting)
		TFramedAsyncSerTool::KillGet();
	fModemFlags &= ~kModemFlagAborting;
}


// ROM 0x000635e8 ModemCommandComplete__14TClassOneModemFl
// The command done: its answer handed to the state machine that is running.
void
TClassOneModem::ModemCommandComplete(NewtonErr result)
{
	AbortTimer();
	fCommandError = result;
	fLastResultTime = GetGlobalTime();
	if (fModemFlags & kModemFlagIdentifying)
		C1IdModem();
	else if (fModemFlags & kModemFlagHangingUp)
		HangUpContinue(result);
	else if (fModemFlags & kModemFlagConnecting)
	{
		if (fConnectState == 12)
		{
			// (the "AT" after the chip was claimed again)
			result = noErr;
			fConnectState = 1;
		}
		ConnectModemContinue(result);
	}
	else if (fModemFlags & (kModemFlagPacketPut | kModemFlagPacketGet))
		C1PktContinue(result);
	else if (fModemFlags & kModemFlagCapabilities)
	{
		if (fOptionLabel == kCMOModemFaxClass1Cap)
			C1GetFaxCapabilities(result);
		else if (fOptionLabel == kCMOModemFaxCapabilities)
			C1GetCapContinue(result);
		else if (fOptionLabel == kCMOModemFaxClass)
			SetServiceClass(result);
		else if (fOptionLabel == kCMOModemFaxClassesSupported)
			GetSrvcClsSupported(result);
	}
	else if (fTAPIService.fActive && fTAPIService.fActive2)
		TAPICommandComplete();
	else if (fFaxClass.fClass == kModemFaxClass2 || fFaxClass.fClass == kModemFaxClass20)
	{
		// a Class 2 command's answer (a Class 1 kModemCtlGetResult is
		// never answered)
		if (fReply.fResultCode == kModemResultConnect)
			UnblockGetAndPutChannel();
		CompleteRequest(kCommToolControlChannel, result, fReply);
	}
}


// ROM 0x00063764 CheckForErrorResult__14TClassOneModemFUl
NewtonErr
TClassOneModem::CheckForErrorResult(ULong result)
{
	switch (result)
	{
	case kModemResultNoCarrier:		return kModemErrNoCarrier;
	case kModemResultError:			return kModemErrCommandFailure;
	case kModemResultNoDialTone:	return kModemErrNoDialTone;
	case kModemResultBusy:			return kModemErrLineBusy;
	case kModemResultNoAnswer:		return kModemErrNoAnswer;
	case kModemResultFCError:		return kModemErrNoFaxCarrier;
	case kModemResultUnknown:		return kModemErrNotSupported;
	}
	return noErr;
}


// ROM 0x0005cfc0 ParseModemRsp__14TClassOneModemFPUc
// A line of the modem's answer made a result code (the table's entry it
// starts with, or kModemResultUnknown), a Class 2 answer's values parsed
// into the reply, an unknown line's text kept.
void
TClassOneModem::ParseModemRsp(UChar* response)
{
	ULong code = kModemResultUnknown;
	const ModemResponse* entry = fCustomResponses;
	if (entry != nil && entry->fString != nil)
	{
		do
		{
			if (memcmp(response, entry->fString, entry->fLength) == 0)
			{
				code = entry->fResult;
				break;
			}
			entry++;
		} while (entry->fString != nil);
	}
	if (code == kModemResultUnknown)
	{
		for (entry = modemRspParseTable; entry->fString != nil; entry++)
			if (memcmp(response, entry->fString, entry->fLength) == 0)
			{
				code = entry->fResult;
				break;
			}
	}
	NewtonErr err;
	switch (code)
	{
	case 0x15: case 0x27: case 0x31: case 0x32:				// +FCSI: +FTSI: +FTI: +FCI:
		C2ParsePhoneNum(fReply.fText, response);
		break;
	case 0x16: case 0x17: case 0x18: case 0x2d: case 0x2e:	// +FDCS: +FDIS: +FDTC: +FCS: +FIS:
		if ((err = C2ParseDIS(response, &fReply.fDIS)) != noErr)
			code = err;
		break;
	case 0x19:												// +FET:
		if (fFaxClass.fClass == kModemFaxClass2)
			;
		else if (fFaxClass.fClass == kModemFaxClass20)
			code = 0x37;
		else
			break;
		fReply.fText[0] = (response[5] == ' ') ? response[6] : response[5];
		break;
	case 0x1a:												// +FHNG:
		fReply.fText[0] = atoi((char*) response + 6);
		break;
	case 0x2c:												// +FHS:
		fReply.fText[0] = atoi((char*) response + 5);
		break;
	case 0x24: case 0x38:									// +FPTS: +FPS
		C2ParsePTS(response, &fReply.fPTS);
		break;
	case 0x1b:												// +FHR:
		if (fFaxClass.fClass == kModemFaxClass20)
			code = 0x3a;
		break;
	case 0x1c:												// +FHT:
		if (fFaxClass.fClass == kModemFaxClass20)
			code = 0x39;
		break;
	case kModemResultUnknown:
		strncpy((char*) fReply.fText, (char*) response, 0x27);
		fReply.fText[0x27] = 0;
		break;
	}
	fReply.fResultCode = code;
}


// ROM 0x0005cd1c C2ParsePhoneNum__14TClassOneModemFPUcT1
// A Class 2 answer's quoted number: what follows the first quote, spaces
// first skipped, up to the closing quote, trailing spaces trimmed - 21
// characters at most.  ROM BUG (fixed): the count is a halfword the
// skipped spaces use up too, and running out while skipping takes it past
// nought to 0xffff, so a number after 21 spaces is copied without a limit.
// The fix skips the spaces without counting them.
void
TClassOneModem::C2ParsePhoneNum(UChar* number, UChar* response)
{
	UChar* start = number;
	while (*response != 0 && *response != '"')
		response++;
	if (*response == '"')
		response++;
	if (RomBugFixed())
	{
		while (*response == ' ')
			response++;
		UShort count = 21;
		while (*response != 0 && *response != '"' && --count != 0)
			*number++ = *response++;
		*number = 0;
		number--;
		while (number >= start && *number == ' ')
		{
			*number = 0;
			number--;
		}
		return;
	}
	UShort n = 21;

	for ( ; ; response++)
	{
		if (*response == 0)
			goto done;
		if (*response != ' ')
			break;
		if (--n == 0)
			goto count;
	}
	for ( ; ; )
	{
		if (*response == 0 || *response == '"')
			break;
count:
		if (--n == 0)
			break;
		*number++ = *response++;
	}
done:
	*number = 0;
	number--;
	while (*number == ' ' && number >= start)
	{
		*number = 0;
		number--;
	}
}


// ROM 0x0005cdbc C2ParseDIS__14TClassOneModemFPUcP13FaxClass2FDIS
// "+FDIS: a,b,c,d,e,f,g,h" - the eight characters (which must be single
// characters).
NewtonErr
TClassOneModem::C2ParseDIS(UChar* response, FaxClass2FDIS* dis)
{
	if (*response == ' ')
		response++;
	for ( ; *response != 0; response++)
		if (*response == ':')
		{
			if (*++response == ' ')
				response++;
			break;
		}
	if (strlen((char*) response) != 15)
		return kModemErrCommandFailure;
	for (int i = 0; i < 8; i++)
		dis->fParms[i] = response[i * 2];
	return noErr;
}


// ROM 0x0005ce64 C2ParsePTS__14TClassOneModemFPUcP13FaxClass2FPTS
// "+FPTS: s,a,b,c": the first digit after the colon as a character, then
// the numbers after it.  (The fourth number is never parsed.)
void
TClassOneModem::C2ParsePTS(UChar* response, FaxClass2FPTS* pts)
{
	pts->fField04 = 0;
	pts->fField08 = 0;
	pts->fField0C = 0;
	pts->fField10 = 0;
	for ( ; *response != 0; response++)
		if (*response == ':')
		{
			do
				response++;
			while (*response != 0 && (*response < '0' || *response > '9'));
			break;
		}
	pts->fStatus = *response;
	if (*response == 0)
		return;
	ULong* fields[3] = { &pts->fField04, &pts->fField08, &pts->fField0C };
	for (int i = 0; i < 3; i++)
	{
		do
		{
			response++;
			if (*response == 0)
				return;
		} while (*response < '0' || *response > '9');
		while (*response >= '0' && *response <= '9')
		{
			*fields[i] = *fields[i] * 10 + (*response - '0');
			response++;
		}
		response--;
	}
}
