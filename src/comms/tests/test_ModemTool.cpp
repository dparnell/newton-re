// The modem tool's commands and answers (comms/ModemTool.h), without a
// modem: the AT commands PrepareCommand builds (and the ROM's bugs in them
// - the +FEA= thrown away, UiToA's digits of the low byte), a long number
// dialed in pieces, the dialing preferences' string, and the modem's
// answers parsed (CONNECT and its speed, a Class 2 number and page status,
// a line the table does not have).  The dialing out and answering
// themselves are ctests host.NewtonModemDial and host.NewtonModemAnswer,
// against tools/modem/fakemodem.py.

#include "ModemTool.h"
#include "NewtErrors.h"
#include "CommErrors.h"
#include "Boot.h"
#include "UserBoot.h"
#include "host/TaskRuntime.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fflush(stdout); } } while (0)


// the tool with what its commands need set up as TaskConstructor does
class TTestModem : public TClassOneModem
{
public:
	TTestModem() : TClassOneModem('mods') { }

	void Setup()
	{
		static UChar empty[1] = { 0 };
		fModemFlags = kModemFlagDataClass;
		fProfile = nil;
		fIdString = (const UChar*) kModemIdStrUnknown;
		fNoECStr = (const UChar*) kModemCmdStrNoECGeneric;
		fECOnlyStr = empty;
		fECFallBackStr = empty;
		fDirectStr = (const UChar*) kModemCmdStrDirect;
		fCellularStr = empty;
		EXPECT(SetModemProfile() == noErr);
		fCmdList.Init(false);
		fCmdPrefix.Init((void*) cmdPrefix, strlen(cmdPrefix));
		fCmdSuffix.Init((void*) cmdSuffix, strlen(cmdSuffix));
		fDialPrefs = (UChar*) NewPtr(strlen(kModemDialPrefStr) + 1);
		strcpy((char*) fDialPrefs, kModemDialPrefStr);
		fCustomResponses = nil;
		fTAPIService.fActive = false;
		fFaxClass.fClass = kModemFaxClass0;
		fPhoneNumber = nil;
		fPhoneNumberLength = 0;
		fDialled = 0;
		fHCodeModem = false;
	}

	// the command list as a string
	const char* Command(ULong command, NewtonErr* err = nil)
	{
		static char text[256];
		NewtonErr result = PrepareCommand(command);
		if (err != nil)
			*err = result;
		text[0] = 0;
		if (result != noErr)
			return text;
		fCmdList.Seek(0, kSeekFromBeginning);
		size_t n = 0;
		while (!fCmdList.AtEOF() && n < sizeof(text) - 1)
			text[n++] = (char) fCmdList.Get();
		text[n] = 0;
		return text;
	}

	using TClassOneModem::ParseModemRsp;
	using TClassOneModem::UpdateDialOptionsStr;
	using TClassOneModem::CheckForErrorResult;
	using TClassOneModem::C2ParsePTS;
	using TClassOneModem::fReply;
	using TClassOneModem::fSRegister;
	using TClassOneModem::fSRegisterValue;
	using TClassOneModem::fPhoneNumber;
	using TClassOneModem::fPhoneNumberLength;
	using TClassOneModem::fDialled;
	using TClassOneModem::fCommandTimeout;
	using TClassOneModem::fControl;
	using TClassOneModem::fFaxClass;
	using TClassOneModem::fDialPrefs;
	using TClassOneModem::fDialing;
	using TClassOneModem::fProfile;
};


static void
Scenario(void)
{
	TTestModem* modem = new TTestModem;
	modem->Setup();

	// the generic profile
	EXPECT(modem->fProfile->fSerialSpeed == 19200);
	EXPECT(modem->fProfile->fCommandTimeout == 2000);
	EXPECT(strcmp((char*) modem->fProfile->GetModemString(0), "Unknown") == 0);
	EXPECT(strcmp((char*) modem->fProfile->GetModemString(5), kModemCmdStrDirect) == 0);
	EXPECT(modem->fProfile->GetModemString(6) == nil);

	// plain commands
	EXPECT(strcmp(modem->Command(kModemCmdAttention), "AT\r") == 0);
	EXPECT(strcmp(modem->Command(kModemCmdRecallFactory), "AT&FE0V1\r") == 0);
	EXPECT(modem->fCommandTimeout == 2000);
	EXPECT(strcmp(modem->Command(kModemCmdIdentify4), "ATI4\r") == 0);
	EXPECT(strcmp(modem->Command(kModemCmdAnswer), "ATA\r") == 0);
	EXPECT(modem->fCommandTimeout == 0);
	modem->fSRegister = 1;
	modem->fSRegisterValue = 0;
	EXPECT(strcmp(modem->Command(kModemCmdReadSRegister), "ATS1?\r") == 0);
	modem->fSRegister = 7;
	modem->fSRegisterValue = 60;
	EXPECT(strcmp(modem->Command(kModemCmdSetSRegister), "ATS7=60\r") == 0);
	EXPECT(strcmp(modem->Command(kModemCmdQueryFaxClass), "AT+FCLASS=?\r") == 0);
	modem->fFaxClass.fClass = kModemFaxClass1;
	EXPECT(strcmp(modem->Command(kModemCmdSetFaxClass), "AT+FCLASS=1\r") == 0);
	modem->fFaxClass.fClass = kModemFaxClass20;
	EXPECT(strcmp(modem->Command(kModemCmdSetFaxClass), "AT+FCLASS=2.0\r") == 0);
	modem->fFaxClass.fClass = kModemFaxClass0;

	// UiToA's bug: each digit taken from the low byte
	UChar digits[8];
	UiToA(40, digits);
	EXPECT(strcmp((char*) digits, "40") == 0);
	UiToA(300, digits);
	EXPECT(strcmp((char*) digits, "304") == 0);

	// Class 1
	modem->fControl.fPacket.fModulation = 0x40;
	EXPECT(strcmp(modem->Command(kModemCmdC1TransmitData), "AT+FTM=96\r") == 0);
	EXPECT(modem->fCommandTimeout == 3200);
	modem->fControl.fPacket.fModulation = 1;
	modem->fControl.fPacket.fDuration = 3000;
	EXPECT(strcmp(modem->Command(kModemCmdC1ReceiveHDLC), "AT+FRH=3\r") == 0);
	EXPECT(modem->fCommandTimeout == 3000);
	modem->fControl.fPacket.fDuration = 200;
	EXPECT(strcmp(modem->Command(kModemCmdC1Silence), "AT+FTS=21\r") == 0);
	EXPECT(modem->fCommandTimeout == 700);

	// Class 2: the eight characters, one character, and the +FEA= lost
	memcpy(modem->fControl.fBytes, "10230000", 8);
	EXPECT(strcmp(modem->Command(0x27), "AT+FDCC=1,0,2,3,0,0,0,0\r") == 0);
	EXPECT(strcmp(modem->Command(0x21), "AT+FBOR=1\r") == 0);
	EXPECT(strcmp(modem->Command(0x5c), "AT+FCR=1\r") == 0);
	strcpy((char*) modem->fControl.fBytes, "408 555 1212");
	EXPECT(strcmp(modem->Command(0x32), "AT+FLID=\"408 555 1212\"\r") == 0);
	NewtonErr err;
	modem->Command(0x1f, &err);
	EXPECT(err == kCommErrBadCommand);

	// dialing: the dashes taken out; the wait for the carrier
	UChar number[] = "555-1212";
	modem->fPhoneNumber = number;
	modem->fPhoneNumberLength = 8;
	modem->fDialled = 0;
	EXPECT(strcmp(modem->Command(kModemCmdDial), "ATDT5551212\r") == 0);
	EXPECT(modem->fDialled == 8);
	EXPECT(modem->fCommandTimeout == 60000 + 55 * 1000);
	modem->fDialing.fDTMFToneDialing = 0;
	modem->fDialled = 0;
	EXPECT(strcmp(modem->Command(kModemCmdDial), "ATDP5551212\r") == 0);
	modem->fDialing.fDTMFToneDialing = 1;
	// a number longer than a command may be: 37 characters and ";", then
	// the rest
	UChar longNumber[] = "0123456789012345678901234567890123456789012345";
	modem->fPhoneNumber = longNumber;
	modem->fPhoneNumberLength = 46;
	modem->fDialled = 0;
	EXPECT(strcmp(modem->Command(kModemCmdDial), "ATDT0123456789012345678901234567890123456;\r") == 0);
	EXPECT(modem->fDialled == 37);
	EXPECT(strcmp(modem->Command(kModemCmdDial), "ATDT789012345\r") == 0);
	EXPECT(modem->fDialled == 46);

	// the dialing preferences
	modem->UpdateDialOptionsStr();
	EXPECT(strcmp((char*) modem->fDialPrefs, "ATM1L2X4S7=055S8=001S6=004S0=000\r") == 0);
	modem->fDialing.fDetectDialTone = 0;
	modem->fDialing.fSpeakerOn = 0;
	modem->fDialing.fWaitForCarrier = 30;
	modem->UpdateDialOptionsStr();
	EXPECT(strcmp((char*) modem->fDialPrefs, "ATM0L2X3S7=030S8=001S6=004S0=000\r") == 0);

	// answers
	modem->ParseModemRsp((UChar*) "OK");
	EXPECT(modem->fReply.fResultCode == kModemResultOK);
	modem->ParseModemRsp((UChar*) "CONNECT 9600");
	EXPECT(modem->fReply.fResultCode == kModemResultConnectN);
	modem->ParseModemRsp((UChar*) "CONNECT");
	EXPECT(modem->fReply.fResultCode == kModemResultConnect);
	modem->ParseModemRsp((UChar*) "NO CARRIER");
	EXPECT(modem->fReply.fResultCode == kModemResultNoCarrier);
	EXPECT(modem->CheckForErrorResult(kModemResultNoCarrier) == kModemErrNoCarrier);
	EXPECT(modem->CheckForErrorResult(kModemResultBusy) == kModemErrLineBusy);
	EXPECT(modem->CheckForErrorResult(kModemResultOK) == noErr);
	modem->ParseModemRsp((UChar*) "fakemodem 1.0");
	EXPECT(modem->fReply.fResultCode == kModemResultUnknown);
	EXPECT(strcmp((char*) modem->fReply.fText, "fakemodem 1.0") == 0);
	modem->ParseModemRsp((UChar*) "+FCSI: \"  408 555 1212  \"");
	EXPECT(modem->fReply.fResultCode == 0x15);
	EXPECT(strcmp((char*) modem->fReply.fText, "408 555 1212") == 0);
	modem->ParseModemRsp((UChar*) "+FDIS: 1,5,2,2,0,0,0,5");
	EXPECT(modem->fReply.fResultCode == 0x17);
	EXPECT(memcmp(modem->fReply.fDIS.fParms, "15220005", 8) == 0);
	modem->ParseModemRsp((UChar*) "+FDIS: 1,5,2");
	EXPECT(modem->fReply.fResultCode == (ULong) kModemErrCommandFailure);
	modem->ParseModemRsp((UChar*) "+FPTS:1,1728,1100,0");
	EXPECT(modem->fReply.fResultCode == 0x24);
	EXPECT(modem->fReply.fPTS.fStatus == '1');
	EXPECT(modem->fReply.fPTS.fField04 == 1728);
	EXPECT(modem->fReply.fPTS.fField08 == 1100);
	EXPECT(modem->fReply.fPTS.fField0C == 0);
	modem->ParseModemRsp((UChar*) "+FHNG: 12");
	EXPECT(modem->fReply.fResultCode == 0x1a);
	EXPECT(modem->fReply.fText[0] == 12);

	HostStopTasks();
}


int
main()
{
	gHostKernelServicesTask = Scenario;
	OsBoot();
	if (failures == 0)
		printf("test_ModemTool: all passed\n");
	else
		printf("test_ModemTool: %d failures\n", failures);
	return failures != 0;
}
