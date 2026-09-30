/*
	File:		comms/fax/FaxTool.h

	Contains:	TFaxTool, the fax tool (serv 'faxs'): a fax machine, the
				ITU-T T.30 procedure worked over the modem tool below it
				(comms/ModemTool.h, 'mods'), and TFaxService, which starts
				it.

				OpenStart starts the modem tool through the comm manager
				(the fax tool's own option array names it) and keeps its
				port.  BindStart asks the modem what it can do over a
				series of option-management requests (BindGetModemOptions:
				the enabled capabilities and framing, the fax classes, the
				class set, the Class 1 modulations) and takes the modem's
				fax class - 2 (Class 1), 4 (Class 2) or 8 (Class 2.0).

				With a Class 1 modem the fax tool runs T.30 itself, one
				phase at a time, every step a control request to the modem
				tool (TModemControlRequest: kModemCtlC1SendHDLC, ..RecvHDLC,
				..SendData, ..RecvData) answered to HandleReply, which
				ModemReqComplete hands to the phase in hand (fPhase):

				A (StartPhaseA): the call - a connect or a listen request
				to the modem tool (PhaseAConnectModem, PhaseAAcceptModem);

				B (StartPhaseB): the pre-message procedure - the called
				machine sends CSI and DIS, the caller answers TSI and DCS,
				then the training check (TCF: a second and a half of
				noughts at the page's speed) and CFR, or FTT and a slower
				speed (AdjustSpeedForFTT);

				C (StartPhaseC): the page - sent a line at a time, each MH
				coded (EncodeT4) into one of two TFaxLineBufs while the
				other is with the modem (SendNextLine, PutMessage), then
				RTC; or received a buffer at a time into two more, fed to
				a TT4FaxLine (comms/fax/T4FaxLine.h) and decoded into the
				client's buffer a line at a time (DecodeLinesBuf);

				D (StartPhaseD): the post-message procedure - MPS, EOM or
				EOP, and the answer MCF, RTP or RTN (CopyQualityResponse
				decides it from how many lines were bad);

				E (StartPhaseE): DCN, and the modem hung up
				(GetNextTermProc's termination procs: CancelTimer,
				CancelModemCmd, StartPhaseE, HangUp).

				With a Class 2 or 2.0 modem the modem runs T.30 and the
				tool only sequences its commands - C2StateUpdate and
				C20StateUpdate, event-driven state machines over the modem
				tool's Class 2 control requests (kModemCtlC2First..).

				A page's frames and their FIFs are T.30's: HDLC 0xff, the
				control field (0x13 on the final frame), the FCF (its X bit
				the least significant: 0x80 DIS, 0x82 DCS, 0x40 CSI, 0x42
				TSI, 0x84 CFR, 0x44 FTT, 0x8c MCF, 0x4c RTN, 0xcc RTP, 0x2e
				EOP, 0x4e MPS, 0x8e EOM, 0xfa DCN, ...), then the FIF.  The
				tool treats the first four bytes of a DIS or DCS as one
				big-endian word (TT30Capabilities) - T.30 bit n is byte
				(n - 1) / 8, bit (n - 1) % 8 - so 0x3c0000 is the data rate
				(bits 11-14), 0x400000 fine resolution (15), 0x800000 2-D
				coding (16), 0x300 the width (17-18), 0xc00 the length
				(19-20), 0x7000 the minimum scan line time (21-23), 0x8000
				the extend bit (24), 0x20000 the receiver bit (10) and
				0x10000 the transmitter bit (9).

				The client (the fax endpoint) drives a page with options:
				'fsgp' starts one, 'feom' ends it, and between them its
				puts (sending: a band of scan lines, 'fcsb' saying how many
				and how wide) or gets (receiving: decoded scan lines) are
				the page's.

				The ROM's class; its declaration is not in the DDK, so the
				names of the fields are ours, their order the ROM's
				(offsets noted, for the ROM's layout of 0x88c bytes).  The
				virtuals are in the ROM's vtable order (TFaxTool's vtable
				0x0002002c).

				DEVIATION (pointer size): the tool's size is the host's,
				and the requests and replies sent to the modem tool carry
				their host sizes.

	Reconstructed from the MP2x00 US ROM (0x000b1fa0-0x000b9628,
	0x000bb2dc-0x000bd6a0); each function cites its origin.
	docs/comms/README.md, "Fax".
*/

#ifndef __COMMS_FAX_FAXTOOL_H
#define __COMMS_FAX_FAXTOOL_H

#ifndef __COMMS_COMMTOOLS_H
#include "CommTools.h"
#endif
#include "ModemTool.h"
#include "T4FaxLine.h"
#include "SerialOptions.h"
#include "toolbox/ByteOrder.h"

class TStore;


/*------------------------------------------------------------------------------
	The fax options.
------------------------------------------------------------------------------*/

#define kCMOFaxPageSetUp			'fpsu'
#define kCMOFaxPassThru				'fpt '
#define kCMOFaxEnableProgressEvent	'fepe'
#define kCMOFaxDirection			'fdir'
#define kCMOFaxSessionInfo			'fsif'
#define kCMOFaxRemoteId				'frid'
#define kCMOFaxLocalId				'flid'
#define kCMOFaxMinScanLineTime		'fmsl'
#define kCMOFaxStartPage			'fsgp'
#define kCMOFaxConfigSendBand		'fcsb'
#define kCMOFaxEndMessage			'feom'

// 'fpsu': the page's resolution and size
class TCMOFaxPageSetUp : public TOption
{
public:
					TCMOFaxPageSetUp();

	ULong			fLength;				// +0x0c  T.30's length (0 A4, 1 B4, 2 unlimited)
	ULong			fWidth;					// +0x10  T.30's width (0 1728 pixels, 1 2048, 2 2432)
	ULong			fResolution;			// +0x14  1 standard, 3 fine
};

// 'fpt ': (never read by the tool)
class TCMOFaxPassThru : public TOption
{
public:
					TCMOFaxPassThru();

	Boolean			fPassThru;				// +0x0c
};

// 'fepe': a progress event every so many lines received (0: none)
class TCMOFaxEnableProgressEvent : public TOption
{
public:
					TCMOFaxEnableProgressEvent();

	ULong			fLines;					// +0x0c
};

// 'fdir': sending, receiving or both
class TCMOFaxDirection : public TOption
{
public:
					TCMOFaxDirection();

	Boolean			fSend;					// +0x0c
	Boolean			fReceive;				// +0x0d
};

// 'fsif': the session agreed (what a received page is)
class TCMOFaxSessionInfo : public TOption
{
public:
					TCMOFaxSessionInfo();

	ULong			fHorizontalRes;			// +0x0c  dots per inch (204)
	ULong			fVerticalRes;			// +0x10  lines per inch (98 standard, 196 fine)
	ULong			fLength;				// +0x14  T.30's length
	ULong			fWidth;					// +0x18  pixels
	ULong			fBitRate;				// +0x1c  bits a second
};

// 'frid', 'flid': the other machine's identity (CSI or TSI), and ours
class TCMOFaxRemoteId : public TOption
{
public:
					TCMOFaxRemoteId();

	UChar			fId[0x18];				// +0x0c  a C string (20 characters and a nought)
};

class TCMOFaxLocalId : public TOption
{
public:
					TCMOFaxLocalId();

	UChar			fId[0x18];				// +0x0c
};

// 'fmsl': the page's minimum scan line time
class TCMOFaxMinScanLineTime : public TOption
{
public:
					TCMOFaxMinScanLineTime();

	ULong			fTime;					// +0x0c  (0x1380 to 0x1388, T.30's codes)
};

// 'fsgp': a page starts (answered asynchronously)
class TCMOFaxStartPage : public TOptionExtended
{
public:
					TCMOFaxStartPage();
};

// 'fcsb': how the client's bands of scan lines are laid out
class TCMOFaxConfigSendBand : public TOption
{
public:
					TCMOFaxConfigSendBand();

	ULong			fLines;					// +0x0c  lines a band
	ULong			fBytesPerLine;			// +0x10  bytes a line in the band
	ULong			fLeftOffset;			// +0x14  white pixels before each line
};

// 'feom': a page ends (answered asynchronously)
class TCMOFaxEndMessage : public TOptionExtended
{
public:
					TCMOFaxEndMessage();

	Boolean			fLastPage;				// +0x14  sending: no page follows; receiving: another does
	Boolean			fPageAccepted;			// +0x15  receiving: the client kept the page
};


/*------------------------------------------------------------------------------
	TT30Capabilities
	A DIS, DTC or DCS FIF: eight bytes, the first four read as one big-endian
	word (see the head of this file), the fifth as the high byte of a second.
------------------------------------------------------------------------------*/

struct TT30Capabilities
{
	UByte			fBytes[8];

	ULong			Word() const			{ return GetBigEndianWord(fBytes); }
	void			SetWord(ULong w)		{ PutBigEndianWord(fBytes, (unsigned int) w); }
	ULong			Word1() const			{ return GetBigEndianWord(fBytes + 4); }
	void			SetWord1(ULong w)		{ PutBigEndianWord(fBytes + 4, (unsigned int) w); }
};

// the word's fields
#define kT30Transmitter				0x00010000		// bit 9
#define kT30Receiver				0x00020000		// bit 10
#define kT30DataRate				0x003c0000		// bits 11-14
#define kT30DataRateShift			18
#define kT30FineResolution			0x00400000		// bit 15
#define kT30TwoDimensional			0x00800000		// bit 16
#define kT30Width					0x00000300		// bits 17-18
#define kT30WidthShift				8
#define kT30Length					0x00000c00		// bits 19-20
#define kT30LengthShift				10
#define kT30MinScanTime				0x00007000		// bits 21-23
#define kT30MinScanTimeShift		12
#define kT30Extend					0x00008000		// bit 24


/*------------------------------------------------------------------------------
	TFaxLineBuf
	A buffer of a page's code on its way to or from the modem: a segment
	over the block, and a list of that one segment to hand the modem tool.
------------------------------------------------------------------------------*/

struct TFaxLineBuf
{
	CBufferSegment	fSegment;				// +0x00
	UChar*			fBuffer;				// +0x28  (malloc)
	ULong			fCount;					// +0x2c  the bytes in it
	Boolean			fInUse;					// +0x30  with the modem
	Boolean			fFull;					// +0x31  sending: filled, to go; receiving: filled, to decode
	ULong			fField34;				// +0x34  (unused)
	CBufferList		fList;					// +0x38  fSegment alone
};


// fFaxFlags
#define kFaxFlagSend				0x00000001		// 'fdir' fSend
#define kFaxFlagReceive				0x00000002		// 'fdir' fReceive
#define kFaxFlagCaller				0x00000004		// this machine called
#define kFaxFlagTransmitter			0x00000008		// this machine sends the pages (a DCS is ours)
#define kFaxFlagPageReceived		0x00000010		// a page received, the client not yet told by 'feom'
#define kFaxFlagNSF					0x00000020		// the other machine sent NSF
#define kFaxFlagDCSReceived			0x00000040
#define kFaxFlagDISReceived			0x00000080
#define kFaxFlagEndMessage			0x00000100		// the client's 'feom' in hand, to be answered
#define kFaxFlagTrained				0x00000400		// the training check passed
#define kFaxFlagRTC					0x00000800		// the received page's end (an empty line) decoded
#define kFaxFlagSendLineWaiting		0x00001000		// a line to be sent once the line-time timer is up
#define kFaxFlagPageStarted			0x00008000		// 'fsgp'
#define kFaxFlagReceivingLines		0x00010000		// a page's lines being received
#define kFaxFlagEndPage				0x00020000		// the client's 'feom' for a page being sent
#define kFaxFlagPageDataEnded		0x00100000		// the modem's data ended (the carrier gone)
#define kFaxFlagPostMessage			0x00200000		// MPS/EOM/EOP received, awaiting the client
#define kFaxFlagEOMSent				0x00800000		// the page's RTC queued
#define kFaxFlagLastPage			0x01000000		// 'feom' fLastPage: EOP after this page
#define kFaxFlagPageGood			0x02000000		// MCF
#define kFaxFlagPageRetrain			0x04000000		// RTP
#define kFaxFlagPageBad				0x08000000		// RTN
#define kFaxFlagCarrierKillOwed		0x20000000		// (cleared by TimeOutKillComplete only - see it)
#define kFaxFlagDataStarted			0x40000000		// a received page's data arrived
#define kFaxFlagBlackout			0x80000000		// the blackout timer (a page's data late) is out

// the fax tool's fToolState bits (above TCommTool's)
#define kFaxToolStateHangUp			0x01000000		// the modem to be hung up
#define kFaxToolStatePhaseE			0x10000000		// DCN to be sent
#define kFaxToolStateModemRequest	0x20000000		// a request is out to the modem tool
#define kFaxToolStateTimer			0x40000000		// the timer message is out

// fPhase
enum
{
	kFaxPhaseA = 0, kFaxPhaseB, kFaxPhaseC, kFaxPhaseD, kFaxPhaseE,
	kFaxPhaseBind, kFaxPhase6, kFaxPhaseUnbind, kFaxPhaseClass2
};

// fTimerType: what TimerComplete does
#define kFaxTimerDIS				3		// no DIS: DISTimeOut
#define kFaxTimerResponse			7		// no response: ResponseTimeOut
#define kFaxTimerLineTime			11		// the minimum scan line time: send the next line
#define kFaxTimerAbort				12		// (kFaxToolErrNoRemoteSignal)
#define kFaxTimerCRP				15		// CRPRetransmitTimeOut
#define kFaxTimerBlackout			16		// PhaseDBlackoutTimeout
#define kFaxTimerDataLate			17		// kFaxToolErrTransmissionFailed
#define kFaxTimerHangUp				18		// DCN sent: the modem request cancelled

// the command last sent (fLastCommand), which RetransCommand repeats
#define kFaxCmdDTC					1
#define kFaxCmdDCS					2
#define kFaxCmdPostMessage			3
#define kFaxCmdDIS					4
#define kFaxCmdTCF					5
#define kFaxCmdGetCommand			6
#define kFaxCmdCFR					7
#define kFaxCmdFTT					8
#define kFaxCmdResponse				9

// fPostMessage: the post-message command received
#define kFaxPostEOM					1
#define kFaxPostEOP					2
#define kFaxPostMPS					3


class TFaxTool : public TCommTool
{
public:
						TFaxTool(ULong serviceId);
	virtual				~TFaxTool();

	virtual ULong		GetSizeOf();

protected:
	virtual NewtonErr	TaskConstructor();
	virtual void		TaskDestructor();
	virtual UChar*		GetToolName();
	virtual void		HandleRequest(TUMsgToken& msgToken, ULong msgType);
	virtual void		HandleReply(ULong userRefCon, ULong msgType);
	virtual NewtonErr	DoKillControl(ULong msgType);
	virtual void		GetCommEvent();
	virtual NewtonErr	OpenStart(TOptionArray* options);
	virtual void		CloseComplete(NewtonErr result);
	virtual void		ConnectStart();
	virtual void		ListenStart();
	virtual void		AcceptStart();
	virtual void		BindStart();
	virtual void		UnbindStart();
	virtual ULong		ProcessOptionStart(TOption* theOption, ULong label, ULong opcode);
	virtual TUPort*		ForwardOptions();
	virtual ULong		ProcessPutBytesOptionStart(TOption* theOption, ULong label, ULong opcode);
	virtual ULong		ProcessGetBytesOptionStart(TOption* theOption, ULong label, ULong opcode);
	virtual void		PutBytes(CBufferList* clientBuffer);
	virtual void		PutFramedBytes(CBufferList* clientBuffer, Boolean endOfFrame);
	virtual void		PutComplete(NewtonErr result, ULong putBytesCount);
	virtual void		KillPut();
	virtual void		GetBytes(CBufferList* clientBuffer);
	virtual void		GetFramedBytes(CBufferList* clientBuffer);
	virtual void		GetComplete(NewtonErr result, Boolean endOfFrame = false, ULong getBytesCount = 0);
	virtual void		KillGet();
	virtual void		TerminateComplete();
	virtual void		GetNextTermProc(ULong terminationPhase, ULong& terminationFlag, TerminateProcPtr& terminationProc);

	// TFaxTool's own (+0x128)
	virtual NewtonErr	Init();

	// binding and the modem tool
	void				BindGetModemOptions(NewtonErr result);
	NewtonErr			PostModemCommand(ULong opCode);
	NewtonErr			PostRecvPkt(ULong opCode, ULong modulation, CBufferList* data, ULong duration, Boolean final);
	NewtonErr			PostTransPkt(ULong opCode, ULong modulation, CBufferList* data, ULong duration, Boolean final);
	void				ModemReqComplete();
	Boolean				KillModemRequest(ULong refCon, CommToolRequestType requestType, ULong stateFlag);
	void				TimeOutKillComplete();
	NewtonErr			SetModemCapabilities(ULong classes, ULong transmitDataMods, ULong transmitHDLCMods,
											 ULong receiveDataMods, ULong receiveHDLCMods);
	ULong				FastestDataRate(ULong modulations);
	void				SetDefaultCapabilities();

	// the timer
	NewtonErr			PostTimer(ULong type, ULong amount, TimeUnits units);
	void				TimerComplete();
	void				KillTimer();

	// the termination procs
	static Boolean		CancelTimer(void* tool);
	static Boolean		CancelModemCmd(void* tool);
	static Boolean		StartPhaseE(void* tool);
	static Boolean		HangUp(void* tool);
	Boolean				CancelTimer();
	Boolean				CancelModemCmd();
	Boolean				StartPhaseE();
	Boolean				HangUp();

	// identities and frames
	void				GetIdentification(const UChar* from, UChar* to, ULong length);
	void				SetIdentification(const UChar* from, UChar* to);
	NewtonErr			BuildControlFrame(UChar fcf, UChar* fif, ULong fifLength, Boolean final);

	// the page's buffers
	NewtonErr			AllocateLineBuffers();
	void				FreeLineBuffers();
	NewtonErr			AllocateReceiveBuffers();
	void				FreeReceiveBuffers();
	ULong				FigureTCSize(ULong bitRate);
	NewtonErr			AllocateTCBuffer(UChar extra);
	void				FreeTCBuffer();

	// pages, for the client
	NewtonErr			DoStartPage();
	void				StartPageComplete(NewtonErr result);
	NewtonErr			DoEndPage();
	void				EndPageComplete(NewtonErr result);
	NewtonErr			PostFaxEvent();

	// phase A
	void				StartPhaseA();
	void				PhaseAModemReqComplete(NewtonErr result);
	void				PhaseAConnectModem(NewtonErr result);
	void				PhaseAAcceptModem(NewtonErr result);
	void				PhaseAComplete(NewtonErr result);

	// phase B
	void				StartPhaseB();
	void				ReStartPhaseB();
	void				PhaseBPktComplete(NewtonErr result);
	void				PhaseBGetInitialID(NewtonErr result);
	void				PhaseBProcessInitialID();
	void				PhaseBPutInitialID(NewtonErr result);
	void				PhaseBPutCommandToRcv(NewtonErr result);
	void				PhaseBGetResponse(NewtonErr result);
	void				PhaseBRespondToFTT(NewtonErr result);
	void				PhaseBProcessCommand();
	void				PhaseBProcessDCSResponse();
	void				PhaseBProcessDTCResponse();
	void				PhaseBProcessDISResponse();
	void				PhaseBWaitForSignalGone(NewtonErr result);
	void				PhaseBProcessOptions();
	void				PhaseBComplete(NewtonErr result);
	void				PhaseBPutPostMsgCmd(UChar fcf, ULong duration);
	void				GetCommandComplete(NewtonErr result);
	void				BuildDCS(TT30Capabilities& remote, TT30Capabilities& dcs);
	Boolean				CompatibleRemoteRcvr(TT30Capabilities& remote);
	Boolean				CompatibleRemoteXmtr(TT30Capabilities& remote);
	Boolean				ValidateDCS(TT30Capabilities& dcs);
	void				SetSessionParameters(TT30Capabilities& dcs);
	NewtonErr			AdjustSpeedForFTT();
	void				PutCommandToRcv(ULong duration);
	void				PutCommandToXmit(ULong duration);
	void				PutInitialId(ULong duration);
	void				PutTrainingCheck(ULong duration);
	void				PutTrainingCheckComplete(NewtonErr result);
	void				GetTrainingCheck();
	void				GetTrainingCheckComplete(NewtonErr result);
	void				VerifyTrainingCheck(ULong count);
	void				GetCommand();
	void				GetResponse();
	void				RetransCommand(ULong duration);
	void				DISTimeOut();
	void				ResponseTimeOut();

	// phase C
	void				StartPhaseC();
	void				PhaseCPktComplete(NewtonErr result);
	void				PhaseCSendProgressEvent(ULong lines);
	NewtonErr			BufferNextLine(TFaxLineBuf& buf);
	void				SendNextLine();
	void				PutMessage(UChar final);
	void				SendEOM();
	void				SendEOMCont();
	void				ReceiveNextLinesBuf(TFaxLineBuf* buf);
	NewtonErr			DecodeLinesBuf();

	// phase D
	void				StartPhaseD();
	void				PhaseDPktComplete(NewtonErr result);
	void				PhaseDGetResponse(NewtonErr result);
	void				PhaseDProcessResponse(NewtonErr result);
	void				PhaseDProcessEOPResponse();
	void				PhaseDProcessMPSResponse();
	void				PhaseDWaitForSignalGone(NewtonErr result);
	void				PhaseDPutPostMsgCmd(ULong duration);
	void				PhaseDProcessCommand();
	void				PhaseDPutCRP(NewtonErr result);
	void				PhaseDPutMCF(NewtonErr result);
	void				PhaseDProcessReceivedPageConfirmation();
	void				PhaseDBlackoutTimeout();
	void				CRPRetransmitTimeOut();
	UChar				CopyQualityResponse();

	// phase E
	void				PhaseEPktComplete(NewtonErr result);

	// Class 2 and 2.0
	NewtonErr			C2InitSubSystem();
	void				C2ModemReqComplete(TClassOneModemCmdReply* reply);
	void				C2StateUpdate(ULong event);
	void				C2FHNG_Rsp(ULong& state);
	void				C2FHNG_OK_Rsp(ULong& state);
	void				C2RecvFDR_Cmd(ULong& state);
	void				C2RecvFDR_Rsp(ULong& state);
	void				C2AbortSession(ULong& state, NewtonErr error);
	NewtonErr			C2ConfigModem(UChar* done);
	NewtonErr			C2DisFromCapabilities(FaxClass2FDIS& dis, FaxClass2FDIS& modem);
	void				C2RecvCfgMdm(ULong& state);
	void				C2RecvCopyQualityCheck(ULong& state);
	void				C2SetSessionParameters();
	void				C2TransCfgMdm(ULong& state);
	NewtonErr			C2ParseDISResponse(UChar* response, FaxClass2FDIS& dis);
	Boolean				C2ValidateDCS(FaxClass2FDIS& dcs, FaxClass2FDIS& dis);
	void				C2TransFDT_Cmd(ULong& state);
	void				C2TransFET_Cmd(ULong& state);
	void				C2TransFET_Rsp(ULong& state);
	void				C2GetModemRsp(ULong timeout, ULong& state);
	void				C2TransFDT_Rsp(ULong& state);
	void				C2PhaseBProcessOptions();
	void				C2TransFPTS_Rsp(ULong& state);
	void				C20StateUpdate(ULong event);
	void				C20RecvFDR_Rsp(ULong& state);
	void				C20AbortSession(ULong& state, NewtonErr error);
	NewtonErr			C20ConfigModem(UChar* done);
	void				C20RecvCfgMdm(ULong& state);
	void				C20RecvCopyQualityCheck(ULong& state);
	void				C20TransCfgMdm(ULong& state);
	void				C20TransFDT_Cmd(ULong& state);
	void				C20GetModemRsp(ULong timeout, ULong& state);
	void				C20TransFDT_Rsp(ULong& state);
	void				C20TransPhaseCSendEOM(ULong& state);
	void				C20FHS_Rsp(ULong& state);
	void				C20FHS_OK_Rsp(ULong& state);
	void				C20RecvFDR_Cmd(ULong& state);

	// the send buffer in use, and the other one
	TFaxLineBuf&		SendBuf()			{ return fSendBufs[fSendBufIndex]; }
	TFaxLineBuf&		OtherSendBuf()		{ return fSendBufs[fSendBufIndex ^ 1]; }

	ULong				fFaxFlags;				// +0x26c  kFaxFlag...
	ULong				fPhase;					// +0x270  kFaxPhase...
	ULong				fPhaseAStep;			// +0x274  1 connect, 2 accept, 3 done
	ULong				fPhaseBStep;			// +0x278
	ULong				fReceiveState;			// +0x27c  1 receiving, 2 the data ended, 3 the page decoded
	ULong				fPhaseDStep;			// +0x280
	Long				fRetries;				// +0x284  of the command in hand
	ULong				fTrainTries;			// +0x288
	ULong				fSpeedTries;			// +0x28c  (AdjustSpeedForFTT)
	ULong				fTCZeroRun;				// +0x290  the training check: the run of noughts in hand
	ULong				fTCLongestRun;			// +0x294  the longest
	ULong				fTCCount;				// +0x298  the bytes received
	ULong				fLastCommand;			// +0x29c  kFaxCmd...
	ULong				fModulation;			// +0x2a0  the page's modulation bit (ModemOptions.h)
	TT30Capabilities	fLocalCaps;				// +0x2a4  our DIS
	ULong				fPostMessage;			// +0x2ac  kFaxPost...
	ULong				fBitRate;				// +0x2b0  the page's bits a second
	ULong				fRateCode;				// +0x2b4  the data rate field last offered
	ULong				fTransmitMods;			// +0x2b8  the modem's +FTM modulations
	ULong				fReceiveMods;			// +0x2bc  its +FRM modulations
	ULong				fReceiveTimeout;		// +0x2c0  milliseconds a receive buffer may take
	TCMOFaxDirection	fDirection;				// +0x2c4  (fSend +0x2d0, fReceive +0x2d1)
	ULong				fHorizontalRes;			// +0x2d4  (204)
	ULong				fVerticalRes;			// +0x2d8  (98 or 196)
	ULong				fBytesPerLine;			// +0x2dc  (216, 256 or 304)
	ULong				fLineBufferSize;		// +0x2e0  (0x438, 0x500 or 0x5f0)
	TUPort				fModemPort;				// +0x2e4  the modem tool's
	TT30Capabilities	fSessionCaps;			// +0x2ec  the DCS
	ULong				fDCSLength;				// +0x2f4
	ULong				fMinScanTime;			// +0x2f8  milliseconds
	ULong				fMinScanLineTime;		// +0x2fc  ('fmsl')
	ULong				fMinLineBytes;			// +0x300  the bytes a line takes at the minimum scan time
	ULong				fProgressLines;			// +0x304  ('fepe')
	ULong				fProgressEvent;			// +0x308  the lines to report (0: none)
	TCMOModemFaxEnabledCaps	fModemCaps;			// +0x30c  ('mfec', 'mfax')
	UChar*				fTCBuffer;				// +0x334
	ULong				fTCSize;				// +0x338
	TFaxLineBuf			fSendBufs[2];			// +0x33c
	ULong				fReceiveBufferSize;		// +0x3ec  (0x200)
	ULong				fSendBufIndex;			// +0x3f0
	ULong				fReceiveBufIndex;		// +0x3f4  the one to receive into
	ULong				fDecodeBufIndex;		// +0x3f8  the one to decode
	ULong				fBandLines;				// +0x3fc  ('fcsb')
	ULong				fLinesLeft;				// +0x400  of the client's buffer
	ULong				fBandBytesPerLine;		// +0x404  ('fcsb')
	ULong				fBandLeftOffset;		// +0x408  ('fcsb')
	CBufferList*		fClientBuffer;			// +0x40c  the put's or the get's
	UChar*				fLineBuffer;			// +0x410  a line of the band (malloc)
	TFaxLineBuf			fReceiveBufs[2];		// +0x414
	UChar*				fReceiveBuffer;			// +0x4c4  (set, never read)
	UChar*				fDecodedLines[2];		// +0x4c8
	ULong				fDecodedIndex;			// +0x4d0
	TT4FaxLine			fT4;					// +0x4d4
	UChar*				fT4Ring;				// +0x508
	ULong				fField50C;				// +0x50c
	ULong				fLines;					// +0x510  of the page received
	ULong				fBadLines;				// +0x514
	TModemControlRequest	fModemRequest;		// +0x518  (its fOpCode +0x520)
	TCommToolBindRequest	fBindRequest;		// +0x544
	TCommToolConnectRequest	fConnectRequest;	// +0x564
	TUAsyncMessage		fModemMsg;				// +0x58c  (refCon 1)
	TClassOneModemCmdReply	fModemReply;		// +0x59c  (fResult +0x5a4, fResultCode +0x5ac, fText +0x5b0)
	TCommToolKillRequest	fKillRequest;		// +0x5d8
	TUAsyncMessage		fKillMsg;				// +0x5e4  (refCon 0xa or 0xe)
	TCommToolReply		fKillReply;				// +0x5f4
	TOptionArray		fModemOptions;			// +0x604
	TCommToolOptionMgmtRequest	fOptionRequest;	// +0x61c
	ULong				fTimerContent;			// +0x638  (0xd, what HandleRequest knows the timer by)
	ULong				fTimerType;				// +0x63c  kFaxTimer...
	TUAsyncMessage		fTimerMsg;				// +0x640
	TTime				fTimerTime;				// +0x650
	CBufferList			fFrameList;				// +0x658  a received frame
	CBufferSegment		fFrameSegment;			// +0x678
	UChar				fFrame[0x106];			// +0x6a0  (0xff, control +0x6a1, FCF +0x6a2, FIF +0x6a3)
	CBufferList			fSendList;				// +0x7a8  a frame to send (its segments deleted)
	UChar				fFrameHeader[4];		// +0x7c8  0xff, control, FCF
	TCMOFaxRemoteId		fRemoteId;				// +0x7cc  (its fId +0x7d8)
	UChar				fLocalIdReversed[0x14];	// +0x7f0  CSI/TSI's FIF: the id backwards, padded with spaces
	ULong				fPages;					// +0x804  sent
	ULong				fLinesSinceFlush;		// +0x808
	TStore*				fStore;					// +0x80c  the client's buffer's large object's
	ULong				fObjectId;				// +0x810
	TOptionArray*		fBindOptions;			// +0x814
	ULong				fFaxClass;				// +0x818  2 Class 1, 4 Class 2, 8 Class 2.0
	ULong				fBindStep;				// +0x81c
	ULong				fC2State;				// +0x820
	ULong				fC2ConfigStep;			// +0x824
	ULong				fC2Framing;				// +0x828  1: a framing option is out to the modem
	FaxClass2FDIS		fC2ModemDIS;			// +0x82c  the modem's
	FaxClass2FDIS		fC2RemoteDIS;			// +0x834  the other machine's (+FDIS/+FDCS)
	FaxClass2FDIS		fC2LocalDIS;			// +0x83c  ours
	UChar				fC2PageStatus;			// +0x844  (+FET)
	UChar				fC2HangUpStatus;		// +0x845  (+FHNG/+FHS)
	FaxClass2FPTS		fC2PTS;					// +0x848  (+FPTS; the ROM copies 0x18 bytes, this and the word after)
	ULong				fField85C;				// +0x85c
	UChar				fLocalId[0x18];			// +0x860  ('flid', 20 characters and a nought at +0x874)
	TCMOFramingParms	fFraming;				// +0x878
};


PROTOCOL TFaxService : public TCMService
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TFaxService);
	TFaxService*		New();
	void				Delete();
	NewtonErr			Start(TOptionArray* options, ULong serviceId, TServiceInfo* serviceInfo);
	NewtonErr			DoneStarting(TAEvent* event, ULong size, TServiceInfo* serviceInfo);
};

void	RegisterFaxService(void);

#endif	/* __COMMS_FAX_FAXTOOL_H */
