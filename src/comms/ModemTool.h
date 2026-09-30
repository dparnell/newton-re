/*
	File:		comms/ModemTool.h

	Contains:	TClassOneModem, the modem tool ("Class One Modem", serv
				'mods'): a Hayes modem on a serial port, spoken to in AT
				commands.  It is the MNP tool (comms/MNP.h) underneath, so
				once the modem has connected the link runs directly
				(fDataMode 1: bytes straight through the serial tool) or
				with MNP error correction done here rather than in the modem
				(fDataMode 2: TMNP's own).

				Everything the tool asks the modem is a command built by
				PrepareCommand out of a number (0 to 0x68: "&FE0V1", "I4",
				"+FCLASS=1", "D" and a number, ...) into fCmdList - the
				"AT" prefix, up to four segments and the carriage return -
				put by PutCommand no sooner than the profile's command delay
				after the last answer, and answered a line at a time by
				GetCommandResult, a byte at a time into fResultBuffer,
				ParseModemRsp turning the line into a result code
				(modemRspParseTable: OK 0, CONNECT 1, RING 2, NO CARRIER 3,
				ERROR 4, ... a line it does not know 0x3b, its text kept in
				the reply).  ModemCommandComplete then hands the result to
				whichever of the tool's state machines is running, by the
				flags in fModemFlags:

				identifying the modem at bind (the C1Id* functions over
				fIdentifyState: "AT" until it answers, "&FE0V1", then "I4"
				or "I0" compared with the ids of the modems the ROM knows,
				each given a profile of the command strings that set it up
				- ModemStrings.h);

				connecting or listening (the C1Cnct* functions over
				fConnectState: the configuration string for the error
				correction wanted, the dialing preferences, "+FCLASS=0",
				then "D" and the number (a piece at a time if it is longer
				than the modem takes), or waiting for RING and answering
				"A" after the rings asked for; CONNECT and its speed make
				the connection, with MNP started over it if the error
				correction is to be ours);

				hanging up (the HangUp* functions: "H0", "+++" if it will
				not answer, "&FE0V1", "S0=0");

				the fax capabilities (C1GetCap*: "+FCLASS=?", then the
				Class 1 modulations "+FTM=?", "+FTH=?", "+FRM=?", "+FRH=?");

				and the Class 1 packets the fax transport sends and receives
				(the C1Pkt* functions over fPacketState: "+FTH=3" or
				"+FTM=<modulation>", CONNECT, the HDLC frame or the page's
				data framed by DLE ETX, OK - with fill bytes sent while a
				page's data runs short, ZeroStuffing), and the Class 2
				commands (C2DoCommand).

				A client's control request (opcodes 0x114 to 0x162) carries
				a TModemControlRequest; the answer is fReply, a
				TClassOneModemCmdReply with the result code and text.

				The ROM's class; its declaration is not in the DDK, so the
				names of the fields are ours, their order the ROM's (offsets
				noted, for the ROM's layout of 0xba4 bytes).  The virtuals
				are in the ROM's vtable order (TClassOneModem's vtable
				0x0001d494).

				DEVIATION (pointer size): the tool's size is the host's, and
				the control request and reply carry their host sizes.

	Reconstructed from the MP2x00 US ROM (0x0005cce0-0x000651cc, the service
	0x0011fad4-0x0011fb58); each function cites its origin.
	docs/comms/README.md, "The modem".
*/

#ifndef __COMMS_MODEMTOOL_H
#define __COMMS_MODEMTOOL_H

#ifndef __COMMS_MNP_H
#include "MNP.h"
#endif
#include "ModemOptions.h"
#include "ModemStrings.h"

// the control requests a client may send (fControl.fOpCode)
#define kModemCtlC1SendHDLC			0x114		// an HDLC frame sent ("+FTH=")
#define kModemCtlC1SendData			0x115		// a page's data sent ("+FTM=")
#define kModemCtlC1RecvHDLC			0x116		// an HDLC frame received ("+FRH=")
#define kModemCtlC1RecvData			0x117		// a page's data received ("+FRM=")
#define kModemCtlGetResult			0x118		// the modem's next answer read
#define kModemCtlC2RecvPageData		0x119		// a Class 2 page's data received
#define kModemCtlC2First			0x11a		// 0x11a to 0x162: Class 2 commands (C2DoCommand)
#define kModemCtlC2Last				0x162

// the commands PrepareCommand builds
enum
{
	kModemCmdNone = 0,
	kModemCmdRecallFactory = 1,		// "&FE0V1"
	kModemCmdSetFaxClass,			// "+FCLASS=" 0, 1, 2 or 2.0
	kModemCmdDial,					// "DT"/"DP" and (a piece of) the number
	kModemCmdHangUp,				// "H0"
	kModemCmdAnswer,				// "A"
	kModemCmdOnLine,				// "O0"
	kModemCmdIdentify0,				// "I0"
	kModemCmdIdentify3,				// "I3"
	kModemCmdIdentify4,				// "I4"
	kModemCmdIdentify5,				// "I5"
	kModemCmdSecondaryDefaults,		// "%J&P1%D0"
	kModemCmdC1Silence,				// "+FTS=" (0xc)
	kModemCmdC1TransmitData = 0xe,	// "+FTM=" and a modulation
	kModemCmdC1ReceiveData,			// "+FRM="
	kModemCmdC1TransmitHDLC,		// "+FTH="
	kModemCmdC1ReceiveHDLC,			// "+FRH="
	kModemCmdAttention = 0x13,		// "AT" alone
	kModemCmdSetSRegister,			// "S<n>=<v>"
	kModemCmdReadSRegister,			// "S<n>?"
	kModemCmdQueryFaxClass,			// "+FCLASS=?"
	kModemCmdQueryC1TransmitData,	// "+FTM=?"
	kModemCmdQueryC1TransmitHDLC,	// "+FTH=?"
	kModemCmdQueryC1ReceiveData,	// "+FRM=?"
	kModemCmdQueryC1ReceiveHDLC,	// "+FRH=?"
	kModemCmdSpeaker,				// "M0" / "M1" (0x1b)
	kModemCmdTAPIOffHook,			// "X1H1"
	kModemCmdTAPIOnHook,			// "H0X<n>"
	kModemCmdTAPIBlindDial,			// "X1"
	kModemCmdC2First = 0x20,		// 0x20 to 0x68: Class 2 and 2.0 commands
	kModemCmdC2Last = 0x68
};

// the result codes (modemRspParseTable; fReply.fResultCode)
#define kModemResultOK				0
#define kModemResultConnect			1
#define kModemResultRing			2
#define kModemResultNoCarrier		3
#define kModemResultError			4
#define kModemResultNoDialTone		6
#define kModemResultBusy			7
#define kModemResultNoAnswer		8
#define kModemResultFCError			16
#define kModemResultConnectN		17		// "CONNECT <speed>"
#define kModemResultUnknown			0x3b	// a line the table does not have

// the timer messages' types (fTimerType)
#define kModemTimerCommand			1		// no answer to a command
#define kModemTimerReset			2		// the modem given time to reset
#define kModemTimerPutCommand		3		// the command delay over
#define kModemTimerPacket			4		// the packet machine continued
#define kModemTimerPacketHDLC		5		// no HDLC frame
#define kModemTimerPacketData		6		// no data
#define kModemTimerListen			7		// the listen timer
#define kModemTimerSilence			8		// a silence sent
#define kModemTimerPowerOn			9		// the modem powered up again
#define kModemTimerC2Setup			10		// a Class 2 page's start

// fModemFlags
#define kModemFlagDataClass			0x00000001		// the modem reset to its data class (it paces the serial line itself)
#define kModemFlagHardwareFlow		0x00000002		// CTS/RTS flow control once connected
#define kModemFlagPutting			0x00000004		// a command's put in hand
#define kModemFlagGetting			0x00000008		// an answer's get in hand
#define kModemFlagTimer				0x00000010		// the timer message is out
#define kModemFlagAborting			0x00000020		// AbortCommand is killing the put and get
#define kModemFlagEscaped			0x00000080		// hanging up: "+++" sent
#define kModemFlagHangUpSent		0x00000100		// hanging up: "H0" sent
#define kModemFlagHungUp			0x00000200		// hanging up: "H0" answered OK
#define kModemFlagIdentifying		0x00000400
#define kModemFlagConnecting		0x00000800
#define kModemFlagHangingUp			0x00001000
#define kModemFlagHangUpAT			0x00002000		// hanging up: "AT" sent
#define kModemFlagHangUpReset		0x00008000		// hanging up: "&FE0V1" sent
#define kModemFlagCapabilities		0x00010000		// an option's answer being asked of the modem
#define kModemFlagI4Tried			0x00020000
#define kModemFlagPacketPut			0x00040000
#define kModemFlagPacketGet			0x00080000
#define kModemFlagPutConnected		0x00100000		// the modem still sending (CONNECT) after a packet's data
#define kModemFlagPacketAborted		0x00200000
#define kModemFlagFaxOriginate		0x00400000		// connected for fax, calling
#define kModemFlagPacketLine		0x00800000		// the modulation left on between packets
#define kModemFlagS0Sent			0x01000000		// hanging up: "S0=0" sent
#define kModemFlagTAPIOffHook		0x02000000
#define kModemFlagTAPICommand		0x04000000
#define kModemFlagIdentifyKilled	0x08000000
#define kModemFlagPacketKill		0x10000000		// the packet's control request is being killed
#define kModemFlagPacketTimedOut	0x20000000
#define kModemFlagFaxAnswer			0x40000000		// connected for fax, answering
#define kModemFlagFrameUnfinished	0x80000000		// the last frame received did not end

// fZeroStuffFlags
#define kZeroStuffOn				0x00000001		// fill bytes sent while a page's data runs short
#define kZeroStuffKilling			0x00000002
#define kZeroStuffDataPut			0x00000004		// the page's data is being put
#define kZeroStuffFillPut			0x00000008		// fill bytes are being put


// Class 2's +FDIS/+FDCS: eight parameter characters
struct FaxClass2FDIS
{
	UChar				fParms[8];
};

// Class 2's +FPTS: the page's status character and four numbers
struct FaxClass2FPTS
{
	ULong				fStatus;				// +0x00  the first digit, as a character
	ULong				fField04;				// +0x04
	ULong				fField08;				// +0x08
	ULong				fField0C;				// +0x0c
	ULong				fField10;				// +0x10  (never parsed)
};


// The answer to a control request: the modem's last result and its text.
class TClassOneModemCmdReply : public TCommToolReply
{
public:
						TClassOneModemCmdReply();

	ULong				fResultCode;			// +0x10  kModemResult...
	union
	{
		UChar			fText[0x28];			// +0x14  (a line the table does not have, or a value parsed out of it)
		FaxClass2FDIS	fDIS;
		FaxClass2FPTS	fPTS;
	};
};


// A control request's parameters (0x2c bytes in the ROM, copied whole
// into fControl).
struct TModemControlRequest : public TCommToolControlRequest
{
	ULong				fField0C;				// +0x0c
	ULong				fTimeout;				// +0x10  milliseconds (0: the profile's)
	union
	{
		struct
		{
			ULong		fModulation;			// +0x14  the modulation bit (ModemOptions.h)
			CBufferList*	fData;				// +0x18  the frame or data
			ULong		fDuration;				// +0x1c  milliseconds: a silence, or the wait for data
			Boolean		fFinal;					// +0x20  the last of a page's data / frames
		}				fPacket;
		UChar			fBytes[0x18];			// +0x14  a Class 2 command's characters or string
	};
};


class TClassOneModem : public TMNP
{
public:
						TClassOneModem(ULong serviceId);
	virtual				~TClassOneModem();

	virtual ULong		GetSizeOf();

protected:
	virtual NewtonErr	TaskConstructor();
	virtual void		TaskDestructor();
	virtual UChar*		GetToolName();
	virtual void		HandleRequest(TUMsgToken& msgToken, ULong msgType);
	virtual void		HandleReply(ULong userRefCon, ULong msgType);
	virtual void		HandleTimerTick();
	virtual NewtonErr	DoControl(ULong opCode, ULong msgType);
	virtual NewtonErr	DoKillControl(ULong msgType);
	virtual void		GetCommEvent();
	virtual NewtonErr	PostCommEvent(TCommToolGetEventReply& theEvent, NewtonErr result);
	virtual NewtonErr	OpenStart(TOptionArray* options);
	virtual void		ConnectStart();
	virtual void		ConnectComplete(NewtonErr result);
	virtual void		ListenStart();
	virtual void		AcceptStart();
	virtual void		AcceptComplete(NewtonErr result);
	virtual NewtonErr	ReleaseStart();
	virtual void		BindStart();
	virtual void		BindComplete(NewtonErr result);
	virtual void		UnbindStart();
	virtual ULong		ProcessOptionStart(TOption* theOption, ULong label, ULong opcode);
	virtual void		PutBytes(CBufferList* clientBuffer);
	virtual void		PutFramedBytes(CBufferList* clientBuffer, Boolean endOfFrame);
	virtual void		PutComplete(NewtonErr result, ULong putBytesCount);
	virtual void		KillPut();
	virtual void		KillPutComplete(NewtonErr result);
	virtual void		GetBytes(CBufferList* clientBuffer);
	virtual void		GetFramedBytes(CBufferList* clientBuffer);
	virtual void		GetBytesImmediate(CBufferList* clientBuffer, Size threshold);
	virtual void		GetComplete(NewtonErr result, Boolean endOfFrame = false, ULong getBytesCount = 0);
	virtual void		KillGet();
	virtual void		KillGetComplete(NewtonErr result);
	virtual void		ResArbReleaseStart(UChar* resName, UChar* resType);
	virtual void		ResArbClaimNotification(UChar* resName, UChar* resType);
	virtual void		TerminateComplete();
	virtual void		GetNextTermProc(ULong terminationPhase, ULong& terminationFlag, TerminateProcPtr& terminationProc);

	// TClassOneModem's own (+0x1b8)
	virtual ULong		GetToolCapabilities();

	// commands
	NewtonErr			BeginModemCommand(ULong command);
	NewtonErr			PrepareCommand(ULong command);
	NewtonErr			BuildCommand(const UChar* command, UChar* arg1, ULong length1, UChar* arg2, ULong length2,
									 UChar* arg3, ULong length3);
	void				PutCommand();
	void				PutCommandComplete(NewtonErr result);
	NewtonErr			PutEscapeCmd();
	void				GetCommandResult();
	void				GetCommandResultComplete(NewtonErr result);
	void				TimeOutCmdResult();
	void				AbortCommand();
	void				ModemCommandComplete(NewtonErr result);
	void				ParseModemRsp(UChar* response);
	NewtonErr			CheckForErrorResult(ULong result);
	NewtonErr			C2ParseDIS(UChar* response, FaxClass2FDIS* dis);
	void				C2ParsePTS(UChar* response, FaxClass2FPTS* pts);
	void				C2ParsePhoneNum(UChar* number, UChar* response);

	// the serial port and the modem's set-up
	void				ResetSerialDrvr(ULong speed, long stopBits, long parity, long dataBits);
	void				AdjustForReset();
	void				AdjustForConnectSpeed();
	void				SetCDOption();
	void				SetSpeakerVolume(UByte speakerOn);
	void				UpdateDialOptionsStr();
	void				IToARegisterValue(UByte value, UChar* digits);
	void				SetActiveConfigStrs(TCMOModemProfile* profile);
	NewtonErr			SetModemProfile();
	NewtonErr			InitPhoneNumberInfo();
	NewtonErr			BlockGetAndPutChannel();
	NewtonErr			UnblockGetAndPutChannel();

	// the timer
	NewtonErr			PostTimer(ULong type, ULong milliseconds);
	void				AbortTimer();

	// identifying the modem
	void				C1IdModem();
	void				C1IdModemComplete(NewtonErr result);
	void				C1IdBegin();
	void				C1IdWakeUp();
	void				C1IdReset();
	void				C1IdWait4Reset();
	void				C1IdCheck4Response();
	void				C1IdAreYouThere();
	void				C1IdI4CmdFailed();
	void				C1IdGetModemId();
	void				C1IdGetIdCmdResponse();
	void				C1IdACLCheckForMNP10();
	void				C1IdACLGetMNP10CmdResponse();
	void				C1IdACLSetV32bis();
	void				C1IdACLCheckForV32bis();
	void				C1IdACLCheckV32bisCmdResponse();
	void				C1IdACLCheckSetAutoModeResponse();
	void				C1IdACLSetProfile();
	void				C1IdCheck4HCode();
	void				C1IdGetCheck4HCodeCmdResponse();
	void				C1IdSetS0();
	void				C1IdCheckForLCS();

	// connecting and listening
	void				ConnectModemContinue(NewtonErr result);
	void				ConnectModemComplete();
	void				EnterConnectedState();
	void				C1CnctCheckCountryConfig();
	void				C1CnctBegin();
	void				C1CnctConfigModem();
	void				C1CnctDialPrefs();
	void				C1CnctSetClass();
	void				C1CnctSetClassBaud();
	void				C1CnctDial();
	void				C1CnctWaitForConnect();
	Boolean				C1CnctCheckAndSetListenTimer();
	void				C1CnctCheckRingCount();
	void				C1CnctGetCheckRingCountResponse();
	void				C1CnctConnectComplete();

	// hanging up (the termination procs)
	static Boolean		CancelMNPConnect(void* tool);
	static Boolean		HangUp(void* tool);
	Boolean				CancelMNPConnect();
	Boolean				HangUp();
	void				HangUpContinue(NewtonErr result);
	Boolean				HangUpModemComplete();

	// the telephone API
	ULong				ProcessTAPICommand(ULong command);
	void				TAPICommandComplete();
	NewtonErr			PostTapiEvent();

	// the fax capabilities and class
	void				C1GetCapStart();
	void				C1GetCapContinue(NewtonErr result);
	void				C1GetCapExtractResult(ULong* capabilities);
	void				C1GetCapComplete(NewtonErr result);
	void				GetSrvcClsSupported(NewtonErr result);
	void				SetServiceClass(NewtonErr result);
	void				C1GetFaxCapabilities(NewtonErr result);
	const UChar*		GetModParamStr(ULong modulation);
	ULong				GetModBaudRate(ULong modulation);

	// the Class 1 packets
	void				DoTransPkt();
	void				DoRecvPkt();
	void				C1PktComplete(NewtonErr result);
	void				C1PktAbort();
	void				C1PktContinue(NewtonErr result);
	void				C1PktTransSilent();
	void				C1PktTransCmd(ULong result);
	void				C1PktRecvCmd();
	void				C1PktPutBytes(ULong result);
	void				C1PktGetBytes(ULong result);
	void				C1PktGetPutResult();
	void				C1PktCheckPutResult(ULong result);
	void				C1PktCheckFRMResult(ULong result);
	void				C1PktGetPktCRC();
	void				C1PktCheckPktCRC(ULong result);
	NewtonErr			ZeroStuffingInit();
	void				ZeroStuffing();
	void				ZeroStuffingDeinit();

	// Class 2
	NewtonErr			C2DoCommand(ULong opCode);
	void				C2ModemRecvPgData();
	void				C2PktGetBytesSetup();
	void				C2PktGetBytesSetupCont();
	void				C2PktGetBytes();
	void				C2PktCheckResult(ULong result);

	ULong				fModemFlags;			// +0x5b0  kModemFlag...
	ULong				fConnectState;			// +0x5b4  the C1Cnct step (1 to 13)
	ULong				fIdentifyState;			// +0x5b8  the C1Id step (1 idle, 2 to 20)
	ULong				fPacketState;			// +0x5bc  the C1Pkt step
	ULong				fCapState;				// +0x5c0  the C1GetCap step
	NewtonErr			fCommandError;			// +0x5c4  the last command's error
	ULong				fField5C8;				// +0x5c8  (set to 1, never read)
	TTime				fLastResultTime;		// +0x5cc  when the last command finished
	TTime				fCommandDelayTime;		// +0x5d4  how long the next command must wait after it
	TTime				fListenStartTime;		// +0x5dc
	TClassOneModemCmdReply	fReply;				// +0x5e4  (its fResultCode +0x5f4, its text +0x5f8)
	TModemControlRequest	fControl;			// +0x620  the control request in hand
	TCMOOutputFlowControlParms	fOutFlowParms;	// +0x64c
	TCMOInputFlowControlParms	fInFlowParms;	// +0x660
	CBufferList			fCmdList;				// +0x674  the command being sent
	CBufferSegment		fCmdPrefix;				// +0x694  "AT"
	CBufferSegment		fCmdSegment;			// +0x6bc  the command
	CBufferSegment		fCmdArg1;				// +0x6e4  and up to three pieces after it
	CBufferSegment		fCmdArg2;				// +0x70c
	CBufferSegment		fCmdArg3;				// +0x734
	CBufferSegment		fCmdSuffix;				// +0x75c  "\r"
	ULong				fSerialSpeed;			// +0x784  the speed the modem is spoken to at
	ULong				fConnectSpeed;			// +0x788  the speed it connected at
	ULong				fCommandTimeout;		// +0x78c  milliseconds an answer is waited for (0: for ever)
	ULong				fResultLength;			// +0x790  of the line in fResultBuffer
	ULong				fModemId;				// +0x794  the index of the modem's id (9 unknown, 10 its profile's own)
	const UChar*		fIdString;				// +0x798  the modem's id and its five configuration strings
	const UChar*		fNoECStr;				// +0x79c
	const UChar*		fECOnlyStr;				// +0x7a0
	const UChar*		fECFallBackStr;			// +0x7a4
	const UChar*		fCellularStr;			// +0x7a8
	const UChar*		fDirectStr;				// +0x7ac
	ULong				fECType;				// +0x7b0  ('mecp')
	ULong				fDataMode;				// +0x7b4  0 none, 1 direct, 2 MNP
	TCMOModemDialing	fDialing;				// +0x7b8
	TCMOModemProfile*	fProfile;				// +0x7d8  (NewPtr)
	TCMOModemPrefs		fPrefs;					// +0x7dc
	TCMOModemConnectType	fConnectType;		// +0x804
	TCMOModemVoiceSupport	fVoiceSupport;		// +0x818
	TCMOModemFaxEnabledCaps	fFaxEnabledCaps;	// +0x828
	TCMOModemFaxCapabilities	fFaxCapabilities;	// +0x850
	TCMOModemFaxClass	fFaxClass;				// +0x878  (its fClass +0x88c)
	TCMOModemFaxClass	fSavedFaxClass;			// +0x890
	TCMOModemFaxClass1Cap	fFaxClass1Cap;		// +0x8a8
	TCMOTAPIService		fTAPIService;			// +0x8cc
	TCMOTAPISpeaker		fTAPISpeaker;			// +0x8dc
	ULong				fTAPIEvent;				// +0x8ec  to post
	Boolean				fResArbReleasing;		// +0x8f0
	UByte				fSRegister;				// +0x8f1  kModemCmdSetSRegister's, kModemCmdReadSRegister's
	UByte				fSRegisterValue;		// +0x8f2
	Boolean				fHCodeModem;			// +0x8f3  the 224's H-code version
	Boolean				fPoweredUp;				// +0x8f4  the machine woke while the modem waited for a call
	Boolean				fCapKilled;				// +0x8f5
	UByte				fRingCount;				// +0x8f6
	ULong				fListenTimer;			// +0x8f8  seconds ('cltr)
	UChar*				fPhoneNumber;			// +0x8fc  ('outg / 'sdgt)
	ULong				fPhoneNumberLength;		// +0x900
	ULong				fDialled;				// +0x904  how much of it has been dialed
	ULong				fField908[3];			// +0x908
	ULong				fModemTimerType;		// +0x914  kModemTimer...
	TUAsyncMessage		fModemTimerMsg;			// +0x918
	TTime				fModemTimerTime;		// +0x928
	TObjectId			fModemTimerMsgId;		// +0x930
	UChar				fResultBuffer[0x100];	// +0x934  the line being answered
	const UChar*		fModemIdStrings[9];		// +0xa34
	CBufferList			fResultList;			// +0xa58  over fResultSegment
	CBufferSegment		fResultSegment;			// +0xa78  one byte
	UChar				fCmdParm1[4];			// +0xaa0  a number, as digits
	UChar				fCmdParm2[4];			// +0xaa4
	UChar*				fDialPrefs;				// +0xaa8  kModemDialPrefStr, with the dialing options put in
	ULong				fZeroStuffFlags;		// +0xaac  kZeroStuff...
	TTime				fZeroStuffStart;		// +0xab0
	ULong				fZeroStuffRate;			// +0xab8  bytes a second
	ULong				fZeroStuffSent;			// +0xabc
	CBufferSegment		fZeroSegment;			// +0xac0
	CBufferList			fZeroList;				// +0xae8
	UChar*				fZeroBuffer;			// +0xb08  (NewPtrClear)
	ULong				fZeroBufferSize;		// +0xb0c
	UChar				fDialString[0x80];		// +0xb10  the piece of the number being dialed
	ULong				fOptionState;			// +0xb90  an option being answered asynchronously: its step
	ULong				fOptionLabel;			// +0xb94  and its label
	UByte				fC2HangUpHi;			// +0xb98  Class 2's hang-up status (+FHNG/+FHS), big-endian
	UByte				fC2HangUpCode;			// +0xb99
	ULong				fNextCommandDelay;		// +0xb9c  milliseconds before the next command (0: the profile's)
	const ModemResponse*	fCustomResponses;	// +0xba0  (never set)
};


PROTOCOL TModemService : public TCMService
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TModemService);
	TModemService*		New();
	void				Delete();
	NewtonErr			Start(TOptionArray* options, ULong serviceId, TServiceInfo* serviceInfo);
	NewtonErr			DoneStarting(TAEvent* event, ULong size, TServiceInfo* serviceInfo);
};

void	RegisterModemService(void);

// UiToA: n's decimal digits into s
void	UiToA(ULong n, UChar* s);

#endif	/* __COMMS_MODEMTOOL_H */
