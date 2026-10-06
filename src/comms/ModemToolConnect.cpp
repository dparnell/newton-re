/*
	File:		comms/ModemToolConnect.cpp

	Contains:	TClassOneModem's connections (ModemTool.h): identifying the
				modem when the port is bound (the C1Id steps), connecting
				and listening (the C1Cnct steps), hanging up, and the
				telephone API's commands.

	Reconstructed from the MP2x00 US ROM (0x0005d724-0x0005db88,
	0x0005ff58-0x00060d2c, 0x000614c4-0x000627c8); each function cites its
	origin.
*/

#include "ModemTool.h"
#include "CommErrors.h"
#include "NewtErrors.h"
#include "CommToolOptions.h"
#include "host/RomBugs.h"

#include <stdlib.h>
#include <string.h>


/*------------------------------------------------------------------------------
	Identifying the modem
	At bind: "AT" until it answers, "&FE0V1" (reset to the factory settings,
	no echo, words for answers), "AT" again, then "I4" (or "I0") - the
	answer compared with the ids of the modems the ROM knows, each given a
	profile of its own command strings - then "S0=0" (no answering by
	itself) and "I5" (a line current sense chip: voice).
------------------------------------------------------------------------------*/

// ROM 0x000617d8 C1IdModem__14TClassOneModemFv
void
TClassOneModem::C1IdModem()
{
	if (fModemFlags & kModemFlagIdentifyKilled)
		return;
	switch (fIdentifyState)
	{
	case 2:		C1IdBegin(); break;
	case 3:		C1IdModemComplete(noErr); break;
	case 4:		C1IdWakeUp(); break;
	case 5:		C1IdReset(); break;
	case 6:		C1IdWait4Reset(); break;
	case 7:		C1IdCheck4Response(); break;
	case 8:		C1IdAreYouThere(); break;
	case 9:		C1IdGetModemId(); break;
	case 10:	C1IdGetIdCmdResponse(); break;
	case 11:	C1IdACLCheckForMNP10(); break;
	case 12:	C1IdACLGetMNP10CmdResponse(); break;
	case 13:	C1IdACLSetV32bis(); break;
	case 14:	C1IdACLCheckForV32bis(); break;
	case 15:	C1IdACLCheckV32bisCmdResponse(); break;
	case 16:	C1IdACLCheckSetAutoModeResponse(); break;
	case 17:	C1IdCheck4HCode(); break;
	case 18:	C1IdGetCheck4HCodeCmdResponse(); break;
	case 19:	C1IdSetS0(); break;
	case 20:	C1IdCheckForLCS(); break;
	}
}


// ROM 0x00061704 C1IdModemComplete__14TClassOneModemFl
// Identified: Class 1's framing (DLE ETX, no header or check), the carrier
// watched, the 'mfax option (if asked for) told the capabilities; the bind
// done.
void
TClassOneModem::C1IdModemComplete(NewtonErr result)
{
	fModemFlags &= ~kModemFlagIdentifying;
	if (result != noErr)
		CleanUp();
	else if (!fPrefs.fNavigatorOpen)
	{
		TCMOFramingParms framing;
		framing.doHeader = false;
		framing.doOutFCS = false;
		framing.doInFCS = false;
		framing.escapeChar = 0x10;
		framing.eomChar = 0x03;
		SetFramingCtl(&framing);
		SetCDOption();
		if (fOptionsInfo.fOptions != nil)
		{
			TOptionIterator iter(fOptionsInfo.fOptions);
			TOption* option = iter.FindOption(kCMOModemFaxCapabilities);
			if (option != nil)
				option->CopyDataFrom(&fFaxCapabilities);
		}
	}
	TCommTool::BindComplete(result);
}


// ROM 0x00061890 C1IdBegin__14TClassOneModemFv
void
TClassOneModem::C1IdBegin()
{
	if (fPrefs.fNavigatorOpen)
	{
		fModemId = 9;
		C1IdModemComplete(noErr);
		return;
	}
	fModemFlags &= ~kModemFlagI4Tried;
	fIdentifyState = 4;
	NewtonErr err = BeginModemCommand(kModemCmdAttention);
	if (err != noErr)
		C1IdModemComplete(err);
}


// ROM 0x000618f8 C1IdWakeUp__14TClassOneModemFv
// (An echo of the "AT" skipped; answered or not, the modem is reset.)
void
TClassOneModem::C1IdWakeUp()
{
	if (fReply.fResultCode == kModemResultUnknown && memcmp(fReply.fText, "AT", 2) == 0)
	{
		GetCommandResult();
		return;
	}
	fIdentifyState = 5;
	NewtonErr err = BeginModemCommand(kModemCmdRecallFactory);
	if (err != noErr)
		C1IdModemComplete(err);
}


// ROM 0x00061968 C1IdReset__14TClassOneModemFv
// Reset: given half a second.
void
TClassOneModem::C1IdReset()
{
	if (fReply.fResultCode == kModemResultUnknown && memcmp(fReply.fText, "AT", 2) == 0)
	{
		GetCommandResult();
		return;
	}
	if (fSerialSpeed != fProfile->fSerialSpeed)
		ResetSerialDrvr(fProfile->fSerialSpeed, 0, 0, 8);
	if (fCommandError != noErr || fReply.fResultCode != kModemResultOK)
	{
		C1IdWait4Reset();
		return;
	}
	NewtonErr err = PostTimer(kModemTimerReset, 500);
	fIdentifyState = 6;
	if (err != noErr)
		C1IdModemComplete(err);
}


// ROM 0x00061a20 C1IdWait4Reset__14TClassOneModemFv
void
TClassOneModem::C1IdWait4Reset()
{
	fIdentifyState = 7;
	NewtonErr err = BeginModemCommand(kModemCmdAttention);
	if (err != noErr)
		C1IdModemComplete(err);
}


// ROM 0x00061a5c C1IdCheck4Response__14TClassOneModemFv
// A modem that answers (if the preferences want one) asked its id.
void
TClassOneModem::C1IdCheck4Response()
{
	if (!fPrefs.fCheckResponse)
	{
		C1IdModemComplete(noErr);
		return;
	}
	if (fCommandError != noErr)
	{
		C1IdModemComplete(fCommandError);
		return;
	}
	if (fReply.fResultCode != kModemResultOK)
	{
		C1IdModemComplete(kModemErrNotSupported);
		return;
	}
	fModemFlags |= kModemFlagI4Tried;
	fIdentifyState = 9;
	NewtonErr err = BeginModemCommand(kModemCmdIdentify4);
	if (err != noErr)
		C1IdModemComplete(err);
}


// ROM 0x00061cd0 C1IdAreYouThere__14TClassOneModemFv
// (No step leads here.)
void
TClassOneModem::C1IdAreYouThere()
{
	if ((fCommandError == noErr && fReply.fResultCode == kModemResultOK) || !fPrefs.fCheckResponse)
	{
		C1IdCheck4Response();
		return;
	}
	C1IdModemComplete(fCommandError != noErr ? fCommandError : kModemErrNotSupported);
}


// ROM 0x00061d04 C1IdI4CmdFailed__14TClassOneModemFv
// "I4" not understood: "I0"; that not either: an unknown modem.
void
TClassOneModem::C1IdI4CmdFailed()
{
	if (fModemFlags & kModemFlagI4Tried)
	{
		fIdentifyState = 9;
		fModemFlags &= ~kModemFlagI4Tried;
		NewtonErr err = BeginModemCommand(kModemCmdIdentify0);
		if (err != noErr)
			C1IdModemComplete(err);
		return;
	}
	fModemId = 9;
	fCommandError = noErr;
	fReply.fResultCode = kModemResultOK;
	fIdentifyState = 10;
	C1IdGetIdCmdResponse();
}


// ROM 0x00061d78 C1IdGetModemId__14TClassOneModemFv
// The id line compared with the modems the ROM knows (9: none of them; 10:
// the one the profile names).
void
TClassOneModem::C1IdGetModemId()
{
	NewtonErr err = fCommandError;
	if (err == kModemErrNoResponse)
	{
		C1IdI4CmdFailed();
		return;
	}
	if (err == kSerErr_AsyncError)
	{
		GetCommandResult();
		return;
	}
	if (err != noErr)
	{
		C1IdModemComplete(err);
		return;
	}
	if (fReply.fResultCode != kModemResultUnknown)
	{
		C1IdI4CmdFailed();
		return;
	}
	Boolean found = false;
	ULong i = 0;
	do
	{
		if (strcmp((const char*) fModemIdStrings[i], (const char*) fResultBuffer) == 0)
			found = true;
		i++;
	} while (i < 9 && !found);
	fModemId = i;
	if (found)
		fModemId = i - 1;
	else if (strcmp((const char*) fIdString, (const char*) fResultBuffer) == 0)
		fModemId = 10;
	fIdentifyState = 10;
	GetCommandResult();
}


// ROM 0x00061e4c C1IdGetIdCmdResponse__14TClassOneModemFv
// The modem known: its profile - the id, the configuration strings (none
// for error correction if it has none), what it can do, the speed it is
// spoken to at.  An ACL modem is asked about MNP 10 first, a 224 its ROM's
// version; everything else goes on to "S0=0".
void
TClassOneModem::C1IdGetIdCmdResponse()
{
	UChar empty[1];
	empty[0] = 0;
	NewtonErr err = fCommandError;
	if (err == noErr)
	{
		if (fReply.fResultCode == kModemResultUnknown)
		{
			GetCommandResult();
			return;
		}
	}
	else if (err == kSerErr_AsyncError)
	{
		GetCommandResult();
		return;
	}
	else if (err != kModemErrNoResponse)
		goto failed;

	switch (fModemId)
	{
	case 6:		// the ACL: MNP 10?
		fSRegister = 40;
		fIdentifyState = 11;
		if ((err = BeginModemCommand(kModemCmdReadSRegister)) != noErr)
			goto failed;
		return;

	case 0:		// the 224
		fIdString = (const UChar*) kModemIdStr224;
		fNoECStr = (const UChar*) kModemCmdStr224;
		fECOnlyStr = empty;
		fECFallBackStr = empty;
		fDirectStr = (const UChar*) kModemCmdStr224;
		fCellularStr = empty;
		if ((err = SetModemProfile()) != noErr)
			goto failed;
		fProfile->fECTypes = 7;
		fProfile->fCellularSupported = false;
		fProfile->fMNP10 = false;
		fProfile->fPassThrough = true;
		fProfile->fCommandTimeout = 500;
		fProfile->fMaxCommandLength = 40;
		fProfile->fCommandDelay = 25;
		fIdentifyState = 17;
		if ((err = BeginModemCommand(kModemCmdIdentify3)) != noErr)
			goto failed;
		return;

	case 9:		// unknown
		err = kModemErrNotSupported;
		if (fPrefs.fUnknownRefused)
			goto failed;
		fIdString = (const UChar*) kModemIdStrUnknown;
		fNoECStr = (const UChar*) kModemCmdStrNoECGeneric;
		fECOnlyStr = empty;
		fECFallBackStr = empty;
		fDirectStr = (const UChar*) kModemCmdStrDirect;
		fCellularStr = empty;
		if ((err = SetModemProfile()) != noErr)
			goto failed;
		break;

	case 7:		// the ELSA MicroLink
		fIdString = (const UChar*) kModemIdStrELSA;
		fNoECStr = (const UChar*) kModemCmdStrNoECELSA;
		fECOnlyStr = (const UChar*) kModemCmdStrECOnlyELSA;
		fECFallBackStr = (const UChar*) kModemCmdStrECFallBackELSA;
		fCellularStr = empty;
		fDirectStr = (const UChar*) kModemCmdStrDirectELSA;
		if ((err = SetModemProfile()) != noErr)
			goto failed;
		fProfile->fECTypes = 7;
		fProfile->fCellularSupported = false;
		fProfile->fMNP10 = true;
		fProfile->fPassThrough = false;
		fProfile->fCommandTimeout = 500;
		fProfile->fMaxCommandLength = 0xff;
		fProfile->fCommandDelay = 25;
		break;

	case 8:		// the Bic HS
		fIdString = (const UChar*) kModemIdStrBicHS;
		fNoECStr = (const UChar*) kModemCmdStrNoECBicHS;
		fECOnlyStr = (const UChar*) kModemCmdStrECOnlyBicHS;
		fECFallBackStr = (const UChar*) kModemCmdStrECFallBackBicHS;
		fCellularStr = empty;
		fDirectStr = (const UChar*) kModemCmdStrDirectBicHS;
		if ((err = SetModemProfile()) != noErr)
			goto failed;
		fProfile->fECTypes = 0xfe;
		fProfile->fSerialSpeed = 38400;
		fProfile->fCellularSupported = false;
		fProfile->fMNP10 = true;
		fProfile->fPassThrough = false;
		fProfile->fCommandTimeout = 500;
		fProfile->fMaxCommandLength = 40;
		fProfile->fCommandDelay = 25;
		break;

	case 5:		// the 9624AC-W
		fIdString = (const UChar*) kModemIdStr9624ACW;
		fNoECStr = (const UChar*) kModemCmdStrNoECGeneric;
		fCellularStr = empty;
		fECOnlyStr = empty;
		fDirectStr = (const UChar*) kModemCmdStrDirect;
		fECFallBackStr = empty;
		if ((err = SetModemProfile()) != noErr)
			goto failed;
		fProfile->fECTypes = 7;
		fProfile->fCellularSupported = false;
		fProfile->fMNP10 = false;
		fProfile->fPassThrough = true;
		fProfile->fCommandTimeout = 500;
		fProfile->fMaxCommandLength = 0xfc;
		fProfile->fCommandDelay = 25;
		break;

	case 10:	// the profile's own
		break;

	default:	// the Rockwell 96V24s (1 to 4)
		switch (fModemId)
		{
		case 1:
			fIdString = (const UChar*) kModemIdStr96V24A;
			fCellularStr = (const UChar*) kModemCmdStrCellular;
			break;
		case 2:
			fIdString = (const UChar*) kModemIdStr96V24D;
			fCellularStr = (const UChar*) kModemCmdStrCellular;
			break;
		case 3:
			fIdString = (const UChar*) kModemIdStr96V24B;
			fCellularStr = empty;
			break;
		case 4:
			fIdString = (const UChar*) kModemIdStr96V24F;
			fCellularStr = empty;
			break;
		}
		fNoECStr = (const UChar*) kModemCmdStrNoEC;
		fECOnlyStr = (const UChar*) kModemCmdStrECOnly;
		fECFallBackStr = (const UChar*) kModemCmdStrECFallBack;
		fDirectStr = (const UChar*) kModemCmdStrDirect;
		if ((err = SetModemProfile()) != noErr)
			goto failed;
		fProfile->fECTypes = 7;
		// ROM BUG (fixed): SetModemProfile has pointed fCellularStr into
		// the new profile, so it is never the empty string on the stack:
		// cellular is always said to be supported.  The fix asks whether
		// the string is empty.
		fProfile->fCellularSupported = RomBugFixed() ? (fCellularStr != nil && fCellularStr[0] != 0) : (fCellularStr != empty);
		fProfile->fMNP10 = true;
		fProfile->fPassThrough = false;
		fProfile->fCommandTimeout = 500;
		fProfile->fMaxCommandLength = 0xfc;
		fProfile->fCommandDelay = 25;
		break;
	}
	fSRegister = 0;
	fSRegisterValue = 0;
	fIdentifyState = 19;
	if ((err = BeginModemCommand(kModemCmdSetSRegister)) == noErr)
		return;
failed:
	C1IdModemComplete(err);
}


// ROM 0x00062278 C1IdACLCheckForMNP10__14TClassOneModemFv
// S40 bit 0: MNP 10 - without it, no cellular string.
void
TClassOneModem::C1IdACLCheckForMNP10()
{
	NewtonErr err = fCommandError;
	if (err == noErr)
	{
		if (fReply.fResultCode != kModemResultUnknown)
			err = CheckForErrorResult(fReply.fResultCode);
	}
	if (err != noErr)
	{
		C1IdModemComplete(err);
		return;
	}
	if ((atoi((char*) fResultBuffer) & 1) == 0)
		fCellularStr = nil;
	fIdentifyState = 12;
	GetCommandResult();
}


// ROM 0x000622e8 C1IdACLGetMNP10CmdResponse__14TClassOneModemFv
void
TClassOneModem::C1IdACLGetMNP10CmdResponse()
{
	NewtonErr err = fCommandError;
	if (err == noErr)
	{
		if (fReply.fResultCode != kModemResultOK)
			err = kModemErrNotSupported;
		else
		{
			fIdentifyState = 13;
			if ((err = BuildCommand((const UChar*) kModemCmdStrACLSet144, nil, 0, nil, 0, nil, 0)) == noErr)
			{
				PutCommand();
				return;
			}
		}
	}
	C1IdModemComplete(err);
}


// ROM 0x00062368 C1IdACLSetV32bis__14TClassOneModemFv
// "ATF10" taken: S37 asked.  ROM BUG (fixed): a refusal was meant to go on
// with the profile regardless, but the result code is compared with an
// error number (kModemErrCommandFailure), which it never is, so any
// refusal ends the identification with kModemErrNotSupported.  The fix
// compares it with ERROR's result code (kModemResultError).
void
TClassOneModem::C1IdACLSetV32bis()
{
	NewtonErr err = fCommandError;
	if (err == noErr)
	{
		if (fReply.fResultCode == kModemResultOK)
		{
			fSRegister = 37;
			fIdentifyState = 14;
			if ((err = BeginModemCommand(kModemCmdReadSRegister)) == noErr)
				return;
		}
		else if (RomBugFixed() ? (fReply.fResultCode == kModemResultError) : ((NewtonErr) fReply.fResultCode == kModemErrCommandFailure))
		{
			fProfile->fECTypes = 0x2e;
			C1IdACLSetProfile();
			return;
		}
		else
			err = kModemErrNotSupported;
	}
	C1IdModemComplete(err);
}


// ROM 0x000623f4 C1IdACLCheckForV32bis__14TClassOneModemFv
// S37 = 11: V.32bis.
void
TClassOneModem::C1IdACLCheckForV32bis()
{
	NewtonErr err = fCommandError;
	if (err == noErr)
	{
		if (fReply.fResultCode != kModemResultUnknown)
			err = CheckForErrorResult(fReply.fResultCode);
	}
	if (err != noErr)
	{
		C1IdModemComplete(err);
		return;
	}
	fProfile->fECTypes = ((atoi((char*) fResultBuffer) & 0x0f) == 11) ? 0xfe : 0x2e;
	fIdentifyState = 15;
	GetCommandResult();
}


// ROM 0x00062470 C1IdACLCheckV32bisCmdResponse__14TClassOneModemFv
void
TClassOneModem::C1IdACLCheckV32bisCmdResponse()
{
	NewtonErr err = fCommandError;
	if (err == noErr)
	{
		if (fReply.fResultCode != kModemResultOK)
			err = kModemErrNotSupported;
		else
		{
			fIdentifyState = 16;
			if ((err = BuildCommand((const UChar*) kModemCmdStrACLSetAutoMode, nil, 0, nil, 0, nil, 0)) == noErr)
			{
				PutCommand();
				return;
			}
		}
	}
	C1IdModemComplete(err);
}


// ROM 0x000624f0 C1IdACLCheckSetAutoModeResponse__14TClassOneModemFv
void
TClassOneModem::C1IdACLCheckSetAutoModeResponse()
{
	NewtonErr err = fCommandError;
	if (err == noErr)
		err = CheckForErrorResult(fReply.fResultCode);
	if (err == noErr)
		C1IdACLSetProfile();
	else
		C1IdModemComplete(err);
}


// ROM 0x00062534 C1IdACLSetProfile__14TClassOneModemFv
// The ACL's profile (its error correction as found); then "S0=0".
void
TClassOneModem::C1IdACLSetProfile()
{
	UChar empty[1];
	empty[0] = 0;
	fIdString = (const UChar*) kModemIdStrACL;
	fNoECStr = (const UChar*) kModemCmdStrNoEC;
	fECOnlyStr = (const UChar*) kModemCmdStrECOnly;
	fECFallBackStr = (const UChar*) kModemCmdStrECFallBack;
	fCellularStr = (fCellularStr != nil) ? (const UChar*) kModemCmdStrCellular : empty;
	fDirectStr = (const UChar*) kModemCmdStrDirect;
	ULong ecTypes = fProfile->fECTypes;
	NewtonErr err = SetModemProfile();
	if (err == noErr)
	{
		// ROM BUG (fixed): as in C1IdGetIdCmdResponse, fCellularStr now
		// points into the profile: cellular is always said to be supported.
		// The fix asks whether the string is empty.
		fProfile->fCellularSupported = RomBugFixed() ? (fCellularStr != nil && fCellularStr[0] != 0) : (fCellularStr != empty);
		fProfile->fMNP10 = true;
		fProfile->fPassThrough = false;
		fProfile->fECTypes = ecTypes;
		fProfile->fSerialSpeed = 38400;
		fProfile->fCommandTimeout = 500;
		fProfile->fMaxCommandLength = 0xfc;
		fProfile->fCommandDelay = 25;
		fSRegister = 0;
		fSRegisterValue = 0;
		fIdentifyState = 19;
		if ((err = BeginModemCommand(kModemCmdSetSRegister)) == noErr)
			return;
	}
	C1IdModemComplete(err);
}


// ROM 0x00062654 C1IdCheck4HCode__14TClassOneModemFv
// The 224's "I3": its H-code version?
void
TClassOneModem::C1IdCheck4HCode()
{
	if (fCommandError != noErr)
	{
		C1IdModemComplete(fCommandError);
		return;
	}
	if (strlen((char*) fResultBuffer) == 13 && strcmp(kModemHCodeVersionStr224, (char*) fResultBuffer + 5) == 0)
		fHCodeModem = true;
	fIdentifyState = 18;
	GetCommandResult();
}


// ROM 0x000626c0 C1IdGetCheck4HCodeCmdResponse__14TClassOneModemFv
void
TClassOneModem::C1IdGetCheck4HCodeCmdResponse()
{
	fSRegister = 0;
	fSRegisterValue = 0;
	fIdentifyState = 19;
	NewtonErr err = BeginModemCommand(kModemCmdSetSRegister);
	if (err != noErr)
		C1IdModemComplete(err);
}


// ROM 0x00062708 C1IdSetS0__14TClassOneModemFv
void
TClassOneModem::C1IdSetS0()
{
	NewtonErr err = fCommandError;
	if (err == noErr)
		err = CheckForErrorResult(fReply.fResultCode);
	if (err == noErr)
	{
		fIdentifyState = 20;
		if ((err = BeginModemCommand(kModemCmdIdentify5)) == noErr)
			return;
	}
	C1IdModemComplete(err);
}


// ROM 0x00062760 C1IdCheckForLCS__14TClassOneModemFv
// "I5"'s lines until OK (or anything else - no error ends it): "LCS" is a
// line current sense chip, so voice.
void
TClassOneModem::C1IdCheckForLCS()
{
	NewtonErr err = fCommandError;
	if (err == noErr)
		err = CheckForErrorResult(fReply.fResultCode);
	if (err != kModemErrNotSupported)
	{
		C1IdModemComplete(noErr);
		return;
	}
	if (strcmp(resultLCS, (char*) fResultBuffer) == 0)
		fVoiceSupport.fSupportsVoice = true;
	GetCommandResult();
}


/*------------------------------------------------------------------------------
	Connecting and listening
	The configuration string for the connection wanted, the dialing
	preferences, the class (+FCLASS=1 for a fax), then the number dialed or
	the rings counted and answered; CONNECT and its speed make the
	connection - with MNP started over it if the error correction is to be
	the Newton's.
------------------------------------------------------------------------------*/

// ROM 0x0005ff58 ConnectStart__14TClassOneModemFv
void
TClassOneModem::ConnectStart()
{
	if (!(fToolState & kToolStateBound))
	{
		ConnectComplete(kCommErrNotBound);
		return;
	}
	if (fOptionsInfo.fOptions != nil)
	{
		TOptionIterator iter(fOptionsInfo.fOptions);
		TOption* option = iter.FindOption(kCMOTAPIService);
		if (option != nil)
			fTAPIService.CopyDataFrom(option);
	}
	fPassiveState = false;
	SetInputSendForIntDelay(11058);
	if (fPrefs.fNavigatorOpen)
	{
		ConnectComplete(noErr);
		return;
	}
	fConnectState = 1;
	ConnectModemContinue(noErr);
}


// ROM 0x00060020 ConnectComplete__14TClassOneModemFl
void
TClassOneModem::ConnectComplete(NewtonErr result)
{
	if (result == noErr)
		EnterConnectedState();
	TCommTool::ConnectComplete(result);
}


// ROM 0x0006004c EnterConnectedState__14TClassOneModemFv
void
TClassOneModem::EnterConnectedState()
{
	if (fPrefs.fNavigatorOpen || (!(fTAPIService.fActive && fTAPIService.fActive2) && !fConnectType.fFax))
		RestoreInputSendForIntDelay();
	fToolState &= ~0x4000;
}


// ROM 0x000600a8 ListenStart__14TClassOneModemFv
// ROM BUG (fixed): not bound, the listen is completed as a connect.  The
// fix completes it as a listen.
void
TClassOneModem::ListenStart()
{
	if (!(fToolState & kToolStateBound))
	{
		if (RomBugFixed())
			ListenComplete(kCommErrNotBound);
		else
			ConnectComplete(kCommErrNotBound);
		return;
	}
	SetInputSendForIntDelay(11058);
	if (fPrefs.fNavigatorOpen)
	{
		ListenComplete(noErr);
		return;
	}
	fConnectState = 1;
	fListenStartTime = GetGlobalTime();
	ConnectModemContinue(noErr);
}


// ROM 0x00060134 AcceptStart__14TClassOneModemFv
void
TClassOneModem::AcceptStart()
{
	if (fDataMode == 2)
		TMNP::AcceptStart();
	else if (!fPrefs.fNavigatorOpen)
		ConnectModemComplete();
	else
		AcceptComplete(noErr);
}


// ROM 0x00060164 AcceptComplete__14TClassOneModemFl
void
TClassOneModem::AcceptComplete(NewtonErr result)
{
	if (result == noErr)
		EnterConnectedState();
	TCommTool::AcceptComplete(result);
}


// ROM 0x00060190 ConnectModemComplete__14TClassOneModemFv
void
TClassOneModem::ConnectModemComplete()
{
	fModemFlags &= ~kModemFlagConnecting;
	if (fToolState & kToolStateListenMode)
		AcceptComplete(noErr);
	else
		ConnectComplete(noErr);
}


// ROM 0x000601c0 ConnectModemContinue__14TClassOneModemFl
void
TClassOneModem::ConnectModemContinue(NewtonErr result)
{
	if (result != noErr)
	{
		StartAbort(result);
		return;
	}
	switch (fConnectState)
	{
	case 1:		C1CnctCheckCountryConfig(); break;
	case 2:		C1CnctBegin(); break;
	case 3:		C1CnctConfigModem(); break;
	case 4:		C1CnctDialPrefs(); break;
	case 5:		C1CnctSetClass(); break;
	case 6:		C1CnctSetClassBaud(); break;
	case 7:		C1CnctDial(); break;
	case 8:		C1CnctWaitForConnect(); break;
	case 9:		C1CnctCheckRingCount(); break;
	case 10:	C1CnctGetCheckRingCountResponse(); break;
	case 11:	C1CnctConnectComplete(); break;
	}
}


// ROM 0x00060230 C1CnctCheckCountryConfig__14TClassOneModemFv
// The port at the modem's speed (a direct connection's, if that is what
// is wanted), the dialing options put in; in Japan the 224 needs its
// secondary defaults.  ROM BUG (fixed): that command failing ends it as an
// identification (C1IdModemComplete), not a connect.  The fix completes
// the connect with the error, as the other steps do.
void
TClassOneModem::C1CnctCheckCountryConfig()
{
	fModemFlags |= kModemFlagConnecting;
	fCommandTimeout = fProfile->fCommandTimeout;
	ResetSerialDrvr(fPrefs.fDirectConnect ? fPrefs.fDirectSpeed : fSerialSpeed, 0, 0, 8);
	UpdateDialOptionsStr();
	fConnectState = 2;
	if (fDialing.fCountryCode == 81 && fModemId == 0)
	{
		NewtonErr err = BeginModemCommand(kModemCmdSecondaryDefaults);
		if (err != noErr)
		{
			if (RomBugFixed())
				ConnectComplete(err);
			else
				C1IdModemComplete(err);
		}
	}
	else
		C1CnctBegin();
}


// ROM 0x000602d0 C1CnctBegin__14TClassOneModemFv
// The configuration string for the connection: the telephone API's and a
// fax's without error correction (or a direct one), a data connection's
// by the error correction wanted and what the modem has (cellular, EC
// only, EC falling back, or none when the Newton's MNP is to do it).
void
TClassOneModem::C1CnctBegin()
{
	const UChar* config;
	if (fTAPIService.fActive && fTAPIService.fActive2)
		config = fProfile->fPassThrough ? fDirectStr : fNoECStr;
	else if (fConnectType.fFax)
	{
		config = fNoECStr;
		if (!fProfile->fPassThrough)
			fModemFlags |= kModemFlagHardwareFlow;
	}
	else if (!fConnectType.fData)
	{
		ConnectComplete(kCommErrBadParameter);
		return;
	}
	else if (fPrefs.fDirectConnect || fProfile->fPassThrough)
		config = fDirectStr;
	else
	{
		fModemFlags |= kModemFlagHardwareFlow;
		if (fDialing.fCellular)
			config = fCellularStr;
		else if (!(fECType & 0x10) && (fECType & 0x0e) && fProfile->fMNP10)
			config = (fECType & 1) ? fECFallBackStr : fECOnlyStr;
		else
			config = fNoECStr;
	}
	if (config != nil && strlen((const char*) config) != 0 && fPrefs.fConfigureModem)
	{
		fConnectState = 3;
		fCommandTimeout = fProfile->fCommandTimeout;
		NewtonErr err = BuildCommand(config, nil, 0, nil, 0, nil, 0);
		if (err == noErr)
			PutCommand();
		else
			ConnectComplete(err);
		return;
	}
	fReply.fResultCode = kModemResultOK;
	C1CnctConfigModem();
}


// ROM 0x0006047c C1CnctConfigModem__14TClassOneModemFv
// The dialing preferences sent.
void
TClassOneModem::C1CnctConfigModem()
{
	if (fReply.fResultCode != kModemResultOK)
	{
		ConnectComplete(kModemErrCommandFailure);
		return;
	}
	if (fDialPrefs != nil && strlen((char*) fDialPrefs) != 0 && fPrefs.fSendDialPrefs)
	{
		NewtonErr err = BuildCommand(fDialPrefs, nil, 0, nil, 0, nil, 0);
		if (err != noErr)
		{
			ConnectComplete(err);
			return;
		}
		fConnectState = 4;
		PutCommand();
		return;
	}
	fReply.fResultCode = kModemResultOK;
	C1CnctDialPrefs();
}


// ROM 0x00060524 C1CnctDialPrefs__14TClassOneModemFv
// A fax: the modem put in its fax class.
void
TClassOneModem::C1CnctDialPrefs()
{
	if (fReply.fResultCode != kModemResultOK)
	{
		StartAbort(kModemErrCommandFailure);
		return;
	}
	if (!fConnectType.fFax)
	{
		C1CnctSetClass();
		return;
	}
	fModemFlags &= ~kModemFlagDataClass;
	fConnectState = 5;
	fCapState = 2;
	NewtonErr err = BeginModemCommand(kModemCmdSetFaxClass);
	if (err != noErr)
		StartAbort(err);
}


// ROM 0x00060598 C1CnctSetClass__14TClassOneModemFv
// A fax is spoken to at 19200 bps.
void
TClassOneModem::C1CnctSetClass()
{
	NewtonErr err = (fModemId == 9) ? kModemErrNotSupported : kModemErrCommandFailure;
	if (fReply.fResultCode != kModemResultOK)
	{
		StartAbort(err);
		return;
	}
	if (!fConnectType.fFax || fProfile->fSerialSpeed == 19200)
	{
		C1CnctSetClassBaud();
		return;
	}
	ResetSerialDrvr(19200, 0, 0, 8);
	fConnectState = 6;
	if ((err = BeginModemCommand(kModemCmdAttention)) != noErr)
		StartAbort(err);
}


// ROM 0x00060634 C1CnctSetClassBaud__14TClassOneModemFv
// The modem set up: the telephone API's call waited for, or the call
// answered ("A" if already rung, else the rings counted), made ("O0" if it
// is up already, else dialed).
void
TClassOneModem::C1CnctSetClassBaud()
{
	if (fReply.fResultCode != kModemResultOK && fProfile->fSerialSpeed != 19200)
	{
		fReply.fResultCode = kModemResultOK;
		ResetSerialDrvr(19200, 0, 0, 8);
	}
	NewtonErr err = kModemErrCommandFailure;
	if (fReply.fResultCode != kModemResultOK)
		goto failed;
	fToolState |= 0x1000;
	fC2HangUpCode = 0;
	fC2HangUpHi = 0;
	if (fTAPIService.fActive && fTAPIService.fActive2)
	{
		if (fToolState & kToolStateConnecting)
			C1CnctConnectComplete();
		else
			fModemFlags &= ~kModemFlagConnecting;
		fCommandTimeout = 0;
		GetCommandResult();
		return;
	}
	if (fToolState & kToolStateListenMode)
	{
		if (fConnectType.fAlreadyConnected)
		{
			SetSpeakerVolume(fDialing.fSpeakerOn);
			fConnectState = 8;
			if ((err = BeginModemCommand(kModemCmdAnswer)) == noErr)
				return;
		}
		else
		{
			fSRegister = 1;
			fConnectState = 9;
			if ((err = BeginModemCommand(kModemCmdReadSRegister)) == noErr)
				return;
		}
		goto failed;
	}
	SetSpeakerVolume(fDialing.fSpeakerOn);
	if (fConnectType.fAlreadyConnected)
	{
		fConnectState = 11;
		if ((err = BeginModemCommand(kModemCmdOnLine)) == noErr)
			return;
	}
	else if ((err = InitPhoneNumberInfo()) == noErr)
	{
		fConnectState = 7;
		if ((err = BeginModemCommand(kModemCmdDial)) == noErr)
			return;
	}
failed:
	StartAbort(err);
}


// ROM 0x000607cc C1CnctDial__14TClassOneModemFv
// The rest of a number too long to dial at once.
void
TClassOneModem::C1CnctDial()
{
	if (fDialled == fPhoneNumberLength)
	{
		C1CnctConnectComplete();
		return;
	}
	NewtonErr err = CheckForErrorResult(fReply.fResultCode);
	if (err == noErr && (err = BeginModemCommand(kModemCmdDial)) == noErr)
		return;
	StartAbort(err);
}


// ROM 0x0006082c C1CnctWaitForConnect__14TClassOneModemFv
// Listening: a RING has the rings counted; anything else waited past
// (within the listen timer).
void
TClassOneModem::C1CnctWaitForConnect()
{
	AbortTimer();
	NewtonErr err = CheckForErrorResult(fReply.fResultCode);
	if (err != noErr)
		goto failed;
	if (fReply.fResultCode == kModemResultConnect)
	{
		C1CnctConnectComplete();
		return;
	}
	if (fReply.fResultCode == kModemResultRing)
	{
		fSRegister = 1;
		fConnectState = 9;
		if ((err = BeginModemCommand(kModemCmdReadSRegister)) == noErr)
			return;
		goto failed;
	}
	if (fFaxClass.fClass == kModemFaxClass2 || fFaxClass.fClass == kModemFaxClass20)
	{
		C1CnctConnectComplete();
		return;
	}
	if (C1CnctCheckAndSetListenTimer())
	{
		fCommandTimeout = 0;
		GetCommandResult();
	}
	return;
failed:
	StartAbort(err);
}


// ROM 0x000608dc C1CnctCheckAndSetListenTimer__14TClassOneModemFv
// false if the listen timer has run out (the listen aborted) or cannot be
// set.
Boolean
TClassOneModem::C1CnctCheckAndSetListenTimer()
{
	if (fListenTimer == 0)
		return true;
	TTime elapsed = GetGlobalTime();
	CompSub(&fListenStartTime.time, &elapsed.time);
	TTime limit(fListenTimer, kSeconds);
	if (CompCompare(&elapsed.time, &limit.time) >= 0)
	{
		StartAbort(kCommErrListenerTimeOut);
		return false;
	}
	NewtonErr err = PostTimer(kModemTimerListen, fCommandTimeout);
	if (err != noErr)
	{
		StartAbort(err);
		return false;
	}
	return true;
}


// ROM 0x000609b4 C1CnctCheckRingCount__14TClassOneModemFv
// S1, the rings so far.
void
TClassOneModem::C1CnctCheckRingCount()
{
	NewtonErr err = fCommandError;
	if (err == noErr && fReply.fResultCode != kModemResultUnknown)
		err = CheckForErrorResult(fReply.fResultCode);
	if (err != noErr)
	{
		StartAbort(err);
		return;
	}
	fRingCount = atoi((char*) fResultBuffer);
	fConnectState = 10;
	GetCommandResult();
}


// ROM 0x00060a1c C1CnctGetCheckRingCountResponse__14TClassOneModemFv
// Enough rings: answered.  Not yet: waited for the next.
void
TClassOneModem::C1CnctGetCheckRingCountResponse()
{
	NewtonErr err = fCommandError;
	if (err == noErr && fReply.fResultCode != kModemResultOK)
		err = kModemErrCommandFailure;
	if (err != noErr)
		goto failed;
	if (fPoweredUp || fRingCount < fDialing.fRingToAnswerAfter)
	{
		fPoweredUp = false;
		if (!C1CnctCheckAndSetListenTimer())
			return;
		fConnectState = 8;
		fCommandTimeout = 0;
		GetCommandResult();
		return;
	}
	SetSpeakerVolume(fDialing.fSpeakerOn);
	fConnectState = 8;
	if ((err = BeginModemCommand(kModemCmdAnswer)) == noErr)
		return;
failed:
	StartAbort(err);
}


// ROM 0x00060acc C1CnctConnectComplete__14TClassOneModemFv
// CONNECT: the connection's speed, and the data mode - straight through, or
// MNP over it when the Newton does the error correction (required, or
// wanted and the modem has no MNP 10).  A Class 2 fax's hang-up status
// ends it.
void
TClassOneModem::C1CnctConnectComplete()
{
	NewtonErr err;
	SetSpeakerVolume(0);
	if (fFaxClass.fClass == kModemFaxClass2 || fFaxClass.fClass == kModemFaxClass20)
	{
		if (fReply.fResultCode == 0x1a || fReply.fResultCode == 0x2c)
		{
			fC2HangUpCode = fReply.fText[0];
			fC2HangUpHi = 0;
			fCommandTimeout = 0;
			GetCommandResult();
			return;
		}
		UShort code = (UShort) (fC2HangUpHi << 8 | fC2HangUpCode);
		if (code != 0)
		{
			fReply.fResultCode = (fFaxClass.fClass == kModemFaxClass2) ? 0x1a : 0x2c;
			fReply.fText[0] = (UChar) code;
			switch (code)
			{
			case 1: case 3: case 4: case 0x0b: case 0x0e:
				err = kModemErrNoCarrier;
				break;
			case 0x0a:
				err = kModemErrCommandFailure;
				break;
			case 0x0d:
				err = kModemErrLineBusy;
				break;
			case 0x0f:
				err = kModemErrNoDialTone;
				break;
			default:
				err = kModemErrNotSupported;
				break;
			}
			StartAbort(err);
			return;
		}
	}
	fPassiveState = false;
	if (fReply.fResultCode == kModemResultUnknown)
	{
		fCommandTimeout = fProfile->fCommandTimeout;
		GetCommandResult();
		return;
	}
	if ((err = CheckForErrorResult(fReply.fResultCode)) != noErr)
	{
		StartAbort(err);
		return;
	}
	fConnectState = 2;
	fConnectInfo.fErrorFree = false;
	fConnectInfo.fSupportsCallBack = false;
	fConnectInfo.fViaAppleTalk = false;
	fConnectInfo.fConnectBitsPerSecond = fConnectSpeed;
	fDataMode = 1;
	if (fTAPIService.fActive && fTAPIService.fActive2)
	{
		ConnectModemComplete();
		return;
	}
	AdjustForConnectSpeed();
	if (fConnectType.fFax)
	{
		if (fToolState & kToolStateListenMode)
		{
			fModemFlags |= kModemFlagFaxAnswer;
			ListenComplete(noErr);
		}
		else
		{
			fModemFlags |= kModemFlagFaxOriginate;
			ConnectModemComplete();
		}
		return;
	}
	if (!fConnectType.fData)
	{
		StartAbort(kCommErrIncompatibleRemote);
		return;
	}
	// (the byte the ROM sets at +0xd of the connect info)
	fConnectInfo.fSupportsCallBack = true;
	if ((fECType & 0x10) || ((fECType & 0x0e) && !fProfile->fMNP10))
	{
		fToolState |= 0x4000;
		fDataMode = 2;
		fDataRate = fConnectSpeed;
		if (!(fToolState & kToolStateListenMode))
			TMNP::ConnectStart();
		else
		{
			fListenTime = 12;
			TMNP::ListenStart();
		}
		return;
	}
	fDataMode = 0;
	if (fToolState & kToolStateListenMode)
		ListenComplete(noErr);
	else
		ConnectModemComplete();
}


/*------------------------------------------------------------------------------
	Hanging up
	The termination procs (GetNextTermProc): MNP's connect cancelled, and
	the modem hung up - "H0" (after "+++" if it will not listen), "&FE0V1"
	and "S0=0".
------------------------------------------------------------------------------*/

Boolean
TClassOneModem::CancelMNPConnect(void* tool)
{
	return ((TClassOneModem*) tool)->CancelMNPConnect();
}


// ROM 0x0005d724 CancelMNPConnect__14TClassOneModemFv
// MNP failing to connect (an MNP error) when the error correction may fall
// back: the termination called off, and the connection made without it.
Boolean
TClassOneModem::CancelMNPConnect()
{
	fToolState &= ~0x4000;
	if (!(fECType & 1))
		return true;
	if ((fECType & 0x10) || (fRequestsToKill & 4))
		return true;
	if (fAbortErr < ERRBASE_MNP - 11 || fAbortErr > ERRBASE_MNP)
		return true;
	fToolState &= ~(kToolStateWantAbort | kToolStateTerminating);
	fAbortErr = noErr;
	fTerminationFlag = 0;
	fTerminationPhase = 0;
	fTerminationEvent = 0;
	fDataMode = 1;
	fAbortLock = 0;
	if ((fToolState & kToolStateListenMode) && fControlOpCode == 4)
		ListenComplete(noErr);
	else
		ConnectModemComplete();
	return false;
}


Boolean
TClassOneModem::HangUp(void* tool)
{
	return ((TClassOneModem*) tool)->HangUp();
}


// ROM 0x0005da9c HangUp__14TClassOneModemFv
// The speaker and flow control off; if connecting or connected (and the
// preferences say so) "H0".  true: nothing to wait for.
Boolean
TClassOneModem::HangUp()
{
	SetSpeakerVolume(0);
	fOutFlowParms.useSoftFlowControl = false;
	fOutFlowParms.useHardFlowControl = false;
	SetOutputFlowControl(&fOutFlowParms);
	fInFlowParms.useSoftFlowControl = false;
	fInFlowParms.useHardFlowControl = false;
	SetInputFlowControl(&fInFlowParms);
	fModemFlags &= ~kModemFlagHardwareFlow;
	AbortCommand();
	if (fPrefs.fHangUpAtDisconnect && (fToolState & (kToolStateConnecting | kToolStateConnected)))
	{
		SetInputSendForIntDelay(11058);
		fModemFlags |= kModemFlagHangingUp;
		if (BeginModemCommand(kModemCmdHangUp) == noErr)
			return false;
	}
	return true;
}


// ROM 0x0005d9dc HangUpContinue__14TClassOneModemFl
// A hang-up command answered: the next, or the termination on (the port
// given up if it was wanted elsewhere).
void
TClassOneModem::HangUpContinue(NewtonErr result)
{
	if (!HangUpModemComplete())
		return;
	fModemFlags &= ~kModemFlagHangingUp;
	fToolState &= ~0x1000;
	if (fResArbReleasing)
	{
		TurnOff();
		ResArbReleaseComplete(noErr);
	}
	else
		ResetSerialDrvr(fProfile->fSerialSpeed, 0, 0, 8);
	if (fResArbReleasing && !ShouldAbort(0, noErr))
		return;
	TerminateConnection();
}


// ROM 0x0005d7e0 HangUpModemComplete__14TClassOneModemFv
// The hang-up's steps: "H0" not answered - "+++" and "H0" again; answered
// - "AT" (unless it said OK), then "&FE0V1", then (a known modem) "S0=0".
// true when done.
Boolean
TClassOneModem::HangUpModemComplete()
{
	NewtonErr err;
	ULong flags = fModemFlags;
	if (flags & kModemFlagS0Sent)
	{
		fModemFlags = flags & ~kModemFlagS0Sent;
		return true;
	}
	if (flags & kModemFlagHangUpReset)
	{
		fModemFlags = flags & ~kModemFlagHangUpReset;
		AdjustForReset();
		if (fSerialSpeed != fProfile->fSerialSpeed)
			ResetSerialDrvr(fProfile->fSerialSpeed, 0, 0, 8);
		if (fCommandError != noErr || fReply.fResultCode != kModemResultOK)
			return true;
		if (fModemId == 0)
			return true;
		fModemFlags |= kModemFlagS0Sent;
		fSRegister = 0;
		fSRegisterValue = 0;
		if ((err = BeginModemCommand(kModemCmdSetSRegister)) == noErr)
			return false;
	}
	else if (flags & kModemFlagHangUpSent)
	{
		fModemFlags = (flags & ~(kModemFlagHangUpSent | kModemFlagHungUp)) | kModemFlagHangUpReset;
		if ((err = BeginModemCommand(kModemCmdRecallFactory)) == noErr)
			return false;
	}
	else if (flags & kModemFlagEscaped)
	{
		if (fCommandError == noErr && fReply.fResultCode == kModemResultOK)
			fModemFlags = flags | kModemFlagHungUp;
		fModemFlags = (fModemFlags & ~kModemFlagEscaped) | kModemFlagHangUpSent;
		if ((err = BeginModemCommand(kModemCmdHangUp)) == noErr)
			return false;
	}
	else if (fCommandError == kModemErrNoResponse)
	{
		fModemFlags = flags | kModemFlagEscaped;
		if ((err = PutEscapeCmd()) == noErr)
			return false;
	}
	else if (fCommandError != noErr && fCommandError != kSerErr_AsyncError)
		return true;
	else if ((flags & kModemFlagHangUpAT) || fReply.fResultCode == kModemResultOK)
	{
		fModemFlags = (flags & ~kModemFlagHangUpAT) | kModemFlagHangUpReset;
		if ((err = BeginModemCommand(kModemCmdRecallFactory)) == noErr)
			return false;
	}
	else
	{
		fModemFlags = flags | kModemFlagHangUpAT;
		if ((err = BeginModemCommand(kModemCmdAttention)) == noErr)
			return false;
	}
	ResetSerialDrvr(fProfile->fSerialSpeed, 0, 0, 8);
	fCommandError = err;
	return true;
}


/*------------------------------------------------------------------------------
	The telephone API
	With 'taps (a voice call), the modem's commands go out as options ask
	('disc, 'outg, 'sdgt, 'tasp) and its answers between them are read all
	the time: RING and BUSY are posted as events.
------------------------------------------------------------------------------*/

// ROM 0x000614c4 ProcessTAPICommand__14TClassOneModemFUl
// ROM BUG (fixed): the speaker is set, then a second sound option made
// here (whose constructor turns the sound on) is given to the chip as
// well, so the speaker ends on whatever was asked.  The fix leaves the
// speaker as it was set.
ULong
TClassOneModem::ProcessTAPICommand(ULong command)
{
	if (!(fTAPIService.fActive && fTAPIService.fActive2 && (fToolState & kToolStateConnected)) || fResArbReleasing)
		return opFailure;
	AbortCommand();
	if (fChip != nil)
	{
		TCMOPCMCIAModemSound sound;
		if (fTAPISpeaker.fSpeakerOn
		&&  (command == kModemCmdTAPIOffHook || command == kModemCmdDial || command == kModemCmdSpeaker))
			SetSpeakerVolume(1);
		else
			SetSpeakerVolume(0);
		if (!RomBugFixed())
			fChip->ProcessOption(&sound);

	}
	if (BeginModemCommand(command) != noErr)
		return opFailure;
	fModemFlags |= kModemFlagTAPICommand;
	return opInProgress;
}


// ROM 0x00061594 TAPICommandComplete__14TClassOneModemFv
// An option's command answered (a long number's next piece dialed, the
// line taken off hook after the number) and the option completed; or an
// answer between commands - RING or BUSY an event.  Then the next answer
// read.
void
TClassOneModem::TAPICommandComplete()
{
	NewtonErr err = noErr;
	if (fCommandError == noErr)
		err = CheckForErrorResult(fReply.fResultCode);
	if (fModemFlags & kModemFlagTAPICommand)
	{
		fModemFlags &= ~kModemFlagTAPICommand;
		ULong label = fOptionsInfo.fCurOptPtr->Label();
		if (err == noErr && (label == 'outg' || label == 'sdgt') && fDialled != fPhoneNumberLength)
		{
			if (ProcessTAPICommand(kModemCmdDial) != opInProgress)
				ProcessOptionComplete(opFailure);
		}
		else
		{
			if (label == 'outg')
			{
				if (fModemFlags & kModemFlagTAPIOffHook)
					fModemFlags &= ~kModemFlagTAPIOffHook;
				else if (err == noErr)
				{
					if (ProcessTAPICommand(kModemCmdTAPIBlindDial) == opInProgress)
						fModemFlags |= kModemFlagTAPIOffHook;
				}
			}
			if (!(fModemFlags & kModemFlagTAPIOffHook))
				ProcessOptionComplete(err != noErr ? opFailure : opSuccess);
		}
	}
	else if (err == noErr)
	{
		if (fReply.fResultCode == kModemResultRing)
			fTAPIEvent = 1;
		else if (fReply.fResultCode == kModemResultBusy)
			fTAPIEvent = 0x0b;
		PostTapiEvent();
	}
	if (fModemFlags & kModemFlagTAPICommand)
		return;
	if (ShouldAbort(0, noErr))
		return;
	fCommandTimeout = 0;
	GetCommandResult();
}
