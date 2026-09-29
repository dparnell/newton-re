/*
	File:		comms/MNP.cpp

	Contains:	TMNP, TMNP_CCB, TXmitBufDscrptr, TMNPService and the MNP
				options (MNP.h).

	Reconstructed from the MP2x00 US ROM (0x00116ac0-0x00116b14,
	0x001174e8-0x0011b840); each function cites its origin.
*/

#include "MNP.h"
#include "CommErrors.h"
#include "NewtErrors.h"
#include "CommToolOptions.h"
#include "OptionArray.h"

#include <string.h>

#define OPTION_DATA_LENGTH(cls)	(sizeof(cls) - sizeof(TOption))

// the frame types
enum
{
	kMNPLR = 1,
	kMNPLD = 2,
	kMNPLT = 4,
	kMNPLA = 5,
	kMNPLN = 6,
	kMNPLNA = 7
};

// the termination event and abort errors the tool reports
#define kMNPErrNegotiation		(-20001)
#define kMNPErrConnectTimeOut	(-20002)
#define kMNPErrNotConnected		(-20003)
#define kMNPErrConnectRetries	(-20006)
#define kMNPErrIncompatible		(-20009)
#define kMNPErrHandshake		(-20010)

#define kMNPTimerTick			1
#define kMNPTimerXmitAbort		2


/*------------------------------------------------------------------------------
	The options
------------------------------------------------------------------------------*/

// ROM 0x001186b4 __ct__18TCMOMNPCompressionFv
// 'mnpc: none, class 5 or V.42bis.
TCMOMNPCompression::TCMOMNPCompression()
	: TOption(kOptionType)
{
	SetLabel(kCMOMNPCompression);
	SetLength(OPTION_DATA_LENGTH(TCMOMNPCompression));
	fCompressionType = kMNPCompressionNone | kMNPCompressionMNP5 | kMNPCompressionV42bis;
}


// ROM 0x00118708 __ct__15TCMOMNPAllocateFv
TCMOMNPAllocate::TCMOMNPAllocate()
	: TOption(kOptionType)
{
	SetLabel(kCMOMNPAllocate);
	SetLength(OPTION_DATA_LENGTH(TCMOMNPAllocate));
	fMNPAlloc = true;
}


// ROM 0x0011875c __ct__15TCMOMNPDataRateFv
// 'eter: 2400.
TCMOMNPDataRate::TCMOMNPDataRate()
	: TOption(kOptionType)
{
	SetLabel(kCMOMNPDataRate);
	SetLength(OPTION_DATA_LENGTH(TCMOMNPDataRate));
	fDataRate = 2400;
}


// ROM 0x001187b0 __ct__23TCMOMNPSpeedNegotiationFv
TCMOMNPSpeedNegotiation::TCMOMNPSpeedNegotiation()
	: TOption(kOptionType)
{
	SetLabel(kCMOMNPSpeedNegotiation);
	SetLength(OPTION_DATA_LENGTH(TCMOMNPSpeedNegotiation));
	fRequestedSpeed = 57600;
}


// ROM 0x00118804 __ct__17TCMOMNPStatisticsFv
TCMOMNPStatistics::TCMOMNPStatistics()
	: TOption(kOptionType)
{
	SetLabel(kCMOMNPStatistics);
	SetLength(OPTION_DATA_LENGTH(TCMOMNPStatistics));
	fAdaptValue = 196;
	fLTRetransCount = 0;
	fLRRetransCount = 0;
	fRetransTotal = 0;
	fRcvBrokenTotal = 0;
	fForceAckTotal = 0;
	fRcvAsyncErrTotal = 0;
	fFramesRcvd = 0;
	fFramesXmited = 0;
	fBytesRcvd = 0;
	fBytesXmited = 0;
	fWriteBytesIn = 0;
	fWriteBytesOut = 0;
	fReadBytesIn = 0;
	fReadBytesOut = 0;
	fWriteFlushCount = 0;
}


// ROM 0x00118898 __ct__19TCMOMNPDebugConnectFv
TCMOMNPDebugConnect::TCMOMNPDebugConnect()
	: TOption(kOptionType)
{
	SetLabel(kCMOMNPDebugConnect);
	SetLength(OPTION_DATA_LENGTH(TCMOMNPDebugConnect));
	fARACompatibleMode = false;
	fClass4 = true;
	fStreamModeMax = true;
	fMaxCredit = 8;
	fMaxDataSize = 64;
}


/*------------------------------------------------------------------------------
	The CCB
------------------------------------------------------------------------------*/

// ROM 0x00116ac0 __ct__15TXmitBufDscrptrFv
TXmitBufDscrptr::TXmitBufDscrptr()
{
	fCount = 0;
	fCapacity = 0;
	fSeq = 0;
	fState = 0;
	fRetransmitted = 0;
}


// ROM 0x0011a670 __dt__15TXmitBufDscrptrFv
TXmitBufDscrptr::~TXmitBufDscrptr()
{ }


// ROM 0x0011a8ac __ct__8TMNP_CCBFv
TMNP_CCB::TMNP_CCB()
{
	fV42 = nil;
	fField90 = 0x1e;
	fClass5 = nil;
	fField94 = 0x32;
	fInactivityTime = 0;
	fAcceptorTime = 0;
	fRetransTime = 0;
	fWindowTime = 0;
	fAckDelay = 0;
	fFlags = 0;
	fOptFlags = 0;
}


// ROM 0x0011ae10 Init__8TMNP_CCBFv
// The transmit buffers made a ring, each a list over its own frame; the LA
// frame's list.
NewtonErr
TMNP_CCB::Init()
{
	NewtonErr err;
	for (ULong i = 0; ; )
	{
		ULong next = i + 1;
		fXmit[i].fPrev = &fXmit[(i - 1) & 7];
		fXmit[i].fNext = &fXmit[next & 7];
		err = fXmit[i].fList.Init(false);
		if (err != noErr)
			return err;
		err = fXmit[i].fSegment.Init(fXmit[i].fFrame, sizeof(fXmit[i].fFrame), false, 0, -1);
		if (err != noErr)
			return err;
		fXmit[i].fList.InsertLast(&fXmit[i].fSegment);
		i = next;
		if (i > 7)
			break;
	}
	err = fLAList.Init(false);
	if (err == noErr)
	{
		err = fLASegment.Init(fLAFrame, 10, false, 0, -1);
		if (err == noErr)
			fLAList.InsertLast(&fLASegment);
	}
	return err;
}


/*------------------------------------------------------------------------------
	The tool
------------------------------------------------------------------------------*/

// ROM 0x0011b610 __ct__4TMNPFUl
TMNP::TMNP(ULong serviceId)
	: TFramedAsyncSerTool(serviceId)
{ }


// ROM 0x0011b67c __dt__4TMNPFv
TMNP::~TMNP()
{ }


// ROM 0x0011b838 GetSizeOf__4TMNPFv
// DEVIATION (pointer size): the host's size (the ROM's 0x5b0).
ULong
TMNP::GetSizeOf()
{
	return sizeof(TMNP);
}


// ROM 0x0011a6b0 GetToolName__4TMNPFv
UChar*
TMNP::GetToolName()
{
	return (UChar*) "MNP Tool";
}


// ROM 0x0011b6cc TaskConstructor__4TMNPFv
// 2400 bps until told otherwise; a 1K input buffer, frames of up to 0x104
// bytes; the timer's message.
NewtonErr
TMNP::TaskConstructor()
{
	NewtonErr err = TFramedAsyncSerTool::TaskConstructor();
	if (err != noErr)
		return err;
	fDataRate = 2400;
	fCCB = nil;
	fRequestedSpeed = 0;
	fNegotiatedSpeed = 0;
	fAllocate = true;
	fBuffers.fRecvSize = 0x400;
	fFrameBufSize = 0x104;
	err = fTimerMsg.Init(false);
	if (err == noErr)
	{
		fTimerMsgId = fTimerMsg.GetMsgId();
		err = noErr;
	}
	return err;
}


// ROM 0x0011b740 TaskDestructor__4TMNPFv
void
TMNP::TaskDestructor()
{
	TFramedAsyncSerTool::TaskDestructor();
}


// ROM 0x0011a6c4 HandleRequest__4TMNPFR10TUMsgTokenUl
// The tool's own timer message: which timer it is.
void
TMNP::HandleRequest(TUMsgToken& msgToken, ULong msgType)
{
	if (msgToken.GetMsgId() != fTimerMsgId)
	{
		TSerTool::HandleRequest(msgToken, msgType);
		return;
	}
	ULong type = fRequest[0];
	if (type == kMNPTimerTick)
		HandleTickTimer();
	else if (type == kMNPTimerXmitAbort)
		HandleXmitAbortTimer();
	else
		CompleteRequest(msgToken, kCommErrBadCommand);
}


// ROM 0x0011a6f4 DoControl__4TMNPFUlT1
// 0x100 is "compress a file" (answered at once); 0x101 is refused.  (So
// TSerTool's 0x100, turning the chip on or off, is out of reach.)
NewtonErr
TMNP::DoControl(ULong opCode, ULong msgType)
{
	if (opCode == 0x100)
	{
		DoCompressFile();
		return noErr;
	}
	if (opCode == 0x101)
	{
		CompleteRequest(kCommToolControlChannel, kCommErrBadCommand);
		return noErr;
	}
	return TSerTool::DoControl(opCode, msgType);
}


// ROM 0x0011b604 DoCompressFile__4TMNPFv
void
TMNP::DoCompressFile()
{
	CompleteRequest(kCommToolControlChannel, noErr);
}


// ROM 0x0011ab4c OpenAlloc__4TMNPFv
// The CCB made: the compressors allowed, the receive frame's list, the
// three buffers, the idle and listen times.
// ROM BUG (kept): a CCB that cannot be allocated answers noErr.
NewtonErr
TMNP::OpenAlloc()
{
	NewtonErr err = noErr;
	fCCB = new TMNP_CCB;
	if (fCCB == nil)
		goto failed;
	if ((err = fCCB->Init()) != noErr)
		goto failed;
	fCCB->fCompressionAllowed = fCompressionOpt.fCompressionType;
	if ((fCCB->fCompressionAllowed & kMNPCompressionV42bis)
	&&  (err = V42CreateCompressVars(&fCCB->fV42)) != noErr)
		goto failed;
	if ((fCCB->fCompressionAllowed & kMNPCompressionMNP5)
	&&  (err = MNPC5Open(&fCCB->fClass5)) != noErr)
		goto failed;
	if ((err = fCCB->fRcvList.Init(false)) != noErr)
		goto failed;
	if ((err = fCCB->fRcvSegment.Init(0x10a)) != noErr)
		goto failed;
	fCCB->fRcvList.InsertLast(&fCCB->fRcvSegment);
	if ((err = fCCB->fRcvData.Allocate(0x820)) != noErr
	||  (err = fCCB->fReadBuf.Allocate(0x302)) != noErr
	||  (err = fCCB->fWriteBuf.Allocate(0x104)) != noErr)
		goto failed;
	fCCB->fAcceptorTime = fListenTime;
	fCCB->fInactivityTime = fIdleTime;
	return noErr;

failed:
	FreeCCB();
	return err;
}


// ROM 0x0011b744 FreeCCB__4TMNPFv
void
TMNP::FreeCCB()
{
	if (fCCB == nil)
		return;
	if (fCCB->fV42 != nil)
	{
		V42DisposeCompressVars(fCCB->fV42);
		fCCB->fV42 = nil;
	}
	if (fCCB->fClass5 != nil)
	{
		MNPC5Close(fCCB->fClass5);
		fCCB->fClass5 = nil;
	}
	delete fCCB;
	fCCB = nil;
}


// ROM 0x0011ad60 ConnectPreflight__4TMNPFv
// The chip on, the CCB made, the framing MNP's (the tool's own kept to put
// back), the link reset and the first frame asked for.
NewtonErr
TMNP::ConnectPreflight()
{
	TCMOFramingParms framing;
	NewtonErr err = TurnOn();
	if (err == noErr && (err = OpenAlloc()) == noErr)
	{
		fToolState |= kMNPToolStateCCB;
		GetFramingCtl(&fCCB->fSavedFraming);
		framing.doHeader = true;
		framing.doOutFCS = true;
		framing.doInFCS = true;
		framing.escapeChar = 0x10;
		framing.eomChar = 0x03;
		SetFramingCtl(&framing);
		InitConnectParms();
		ResetLink();
		RcvInit();
	}
	return err;
}


// ROM 0x001174e8 InitConnectParms__4TMNPFv
// What the link will ask for, out of the debug connect option.
void
TMNP::InitConnectParms()
{
	fToolState &= ~kMNPToolStateSendLD;
	fCCB->fWindow = fDebugConnect.fMaxCredit;
	fCCB->fClass4 = fDebugConnect.fClass4;
	fCCB->fMaxDataSize = fDebugConnect.fMaxDataSize;
	if (fDebugConnect.fStreamModeMax)
		fCCB->fOptFlags |= 4;
	UByte p2;
	if (!fDebugConnect.fARACompatibleMode)
		p2 = 0xfa;
	else
	{
		fCCB->fOptFlags |= 3;
		p2 = 0x20;
	}
	fCCB->fV42P2 = p2;
	if (fCCB->fOptFlags & 1)
		fCCB->fOptFlags |= 2;
	else
		fCCB->fOptFlags &= ~2;
	fCCB->fFlags &= ~(0xe0000000 | 0x300000 | 0x3000 | 0x33);
	TCMOMNPStatistics stats;
	fCCB->fStats = stats;
	fCCB->fUserReason = 0;
	fCCB->fDisconnectReason = 0;
	fCCB->fFlags = 0;
	fCCB->fSendingLD = false;
	fCCB->fDataSize = fCCB->fMaxDataSize;
	fCCB->fV42P0 = 3;
	fCCB->fV42P1Lo = 0;
	fCCB->fV42P1Hi = 4;
	fCCB->fCompression = fCCB->fCompressionAllowed;
	fCCB->fCompressRefCon = this;
	fCCB->fDecompressRefCon = this;
	fCCB->fCompress = MNPCompressOut;
	fCCB->fFlush = MNPNilFlush;
	fCCB->fDecompress = MNPDecompressOut;
	fCCB->fMinFree = 1;
	fCCB->fMaxExpansion = 1;
	fCCB->fClientPut = nil;
	fCCB->fClientGet = nil;
	if (fDataRate <= 1200)
		fCCB->fWindowTime = 7;
	else
		fCCB->fWindowTime = 3;
	fCCB->fRetransTime = 3;
	fCCB->fXmitState = 1;
	fNegotiatedSpeed = 0;
	fSavedDataRate = fDataRate;
}


// ROM 0x001176d8 ResetLink__4TMNPFv
// Sequence numbers and buffers back to the start, the timers stopped; an
// acceptor's inactivity and window timers, or an originator's listen
// timer, started.
void
TMNP::ResetLink()
{
	fCCB->fStats.fLTRetransCount = 0;
	fCCB->fStats.fLRRetransCount = 0;
	fCCB->fXmitRetrans = nil;
	fCCB->fFlags &= ~(0x1d800000 | kMNPDuplicateLT);
	fCCB->fUnacked = 1;
	fCCB->fSendLimit = fCCB->fWindow + 1;
	fCCB->fLastSent = 0;
	fCCB->fOutstanding = 0;
	fCCB->fSentOfCur = 0;
	fCCB->fXmitFill = &fCCB->fXmit[1];
	fCCB->fXmitCur = &fCCB->fXmit[1];
	for (int i = 0; i < 8; i++)
	{
		fCCB->fXmit[i].fCount = 0;
		fCCB->fXmit[i].fSeq = 0;
		fCCB->fXmit[i].fState = 0;
		fCCB->fXmit[i].fRetransmitted = 0;
	}
	fCCB->fRcvSeq = 0;
	fCCB->fRcvUnacked = 0;
	fCCB->fRetransTimer = 0;
	fCCB->fAckTimer = 0;
	fCCB->fWindowTimer = 0;
	fCCB->fInactivityTimer = 0;
	fCCB->fAcceptorTimer = 0;
	if (fToolState & kToolStateConnected)
	{
		fCCB->fInactivityTimer = fCCB->fInactivityTime;
		if (fCCB->fWindow > 1)
			fCCB->fWindowTimer = fCCB->fWindowTime;
		return;
	}
	if ((fToolState & kToolStateConnecting) && (fCCB->fFlags & kMNPAcceptor))
		fCCB->fAcceptorTimer = fCCB->fAcceptorTime;
}


// ROM 0x00117a24 InitFrameBufs__4TMNPFv
// Each LT's header: [4, LT, 1, 1, seq], or class 4's [2, LT, seq].
void
TMNP::InitFrameBufs()
{
	if (fCCB->fClass4)
	{
		for (int i = 0; i < 8; i++)
		{
			fCCB->fXmit[i].fFrame[0] = 2;
			fCCB->fXmit[i].fFrame[1] = kMNPLT;
		}
		fCCB->fHeaderLength = 2;
		return;
	}
	for (int i = 0; i < 8; i++)
	{
		fCCB->fXmit[i].fFrame[0] = 4;
		fCCB->fXmit[i].fFrame[1] = kMNPLT;
		fCCB->fXmit[i].fFrame[2] = 1;
		fCCB->fXmit[i].fFrame[3] = 1;
	}
	fCCB->fHeaderLength = 4;
}


// ROM 0x00117ad0 EnterConnectedState__4TMNPFv
void
TMNP::EnterConnectedState()
{
	fCCB->fRetransTimer = 0;
	fCCB->fStats.fLRRetransCount = 0;
	fCCB->fInactivityTimer = fCCB->fInactivityTime;
	if (fCCB->fWindow > 1)
		fCCB->fWindowTimer = fCCB->fWindowTime;
	InitFrameBufs();
	SetRetransTimer();
	if (fCCB->fFlags & kMNPAcceptor)
	{
		fCCB->fFlags &= ~(kMNPOriginator | kMNPAcceptor);
		AcceptComplete(noErr);
	}
	else
		ConnectComplete(noErr);
}


// ROM 0x00117b68 SetRetransTimer__4TMNPFv
// The retransmission time in seconds, by the line's speed, the frame size
// and the window.
void
TMNP::SetRetransTimer()
{
	ULong rate = fDataRate;
	ULong t = 2;
	ULong size = fCCB->fDataSize;
	int window = fCCB->fWindow;
	if (rate >= 38400)
		t = 2;
	else if (rate >= 19200)
		t = (size <= 0x40 || window <= 4) ? 2 : 3;
	else if (rate >= 9600)
		t = (size <= 0x40 || window <= 4) ? 2 : 4;
	else if (rate >= 4800)
	{
		if (size > 0x40)
			t = (window > 4) ? 6 : 3;
		else
			t = (window <= 4) ? 2 : 3;
	}
	else if (rate >= 2400)
	{
		if (size > 0x40)
			t = (window <= 4) ? 5 : 10;
		else
			t = (window > 4) ? 4 : 3;
	}
	else
	{
		if (size > 0x40)
			t = (window <= 4) ? 0xb : 0x13;
		else
			t = (window > 4) ? 7 : 4;
	}
	fCCB->fRetransTime = t;
}


// ROM 0x00117c80 ParamNegotiation__4TMNPFUc
// An LR's parameters (the receive list at the parameters) against what we
// asked for: 1 the constant, 2 the framing mode (only octet, 2, will do),
// 3 the window, 4 N401, 8 optimisation, 9 compression, 0x0e V.42bis,
// 0xc5 the speed.  An originator (answering an LR we sent) must know
// every parameter; the compression settled, and the connect info filled
// in.  ==> true if the link can go ahead.
// ROM BUG (kept): in the speed parameter, a sub-parameter other than 1 is
// counted but its bytes are not read past.
Boolean
TMNP::ParamNegotiation(Boolean acceptor)
{
	Boolean ok = true;
	ULong compression = 0;
	Boolean haveOpt = false, haveN401 = false, haveWindow = false, haveFraming = false;
	ULong framing = 0, opt = 0;
	CBufferList& list = fCCB->fRcvList;
	list.Get();
	int type;
	while ((type = list.Get()) != -1 && ok)
	{
		ULong length = list.Get() & 0xff;
		switch (type)
		{
		case 1:
			list.Seek(6, kSeekFromHere);
			break;
		case 2:
			{
				ULong v = list.Get() & 0xff;
				if ((int) v > 2)
					v = 2;
				framing = v & 0xff;
				haveFraming = true;
			}
			break;
		case 3:
			{
				UByte w = fCCB->fWindow;
				UByte v = list.Get() & 0xff;
				if (w < v)
					v = w;
				fCCB->fWindow = v;
				haveWindow = true;
			}
			break;
		case 4:
			{
				UByte lo = list.Get();
				UByte hi = list.Get();
				Long v = (hi << 8) | lo;
				Long cur = fCCB->fMaxDataSize;
				if (cur >= v)
					cur = v;
				fCCB->fMaxDataSize = cur;
				haveN401 = true;
				fCCB->fDataSize = cur;
			}
			break;
		case 8:
			opt = list.Get() & 0xff;
			haveOpt = true;
			break;
		case 9:
			{
				ULong v = list.Get();
				if ((v & 0x1c) && (fCCB->fCompressionAllowed & kMNPCompressionV42bis))
				{
					Long dict = (fCCB->fV42P1Hi << 8) | fCCB->fV42P1Lo;
					if ((v & 0x10) && dict >= 0x800)
					{
						fCCB->fV42P1Lo = 0;
						fCCB->fV42P1Hi = 8;
					}
					else if ((v & 8) && dict >= 0x400)
					{
						fCCB->fV42P1Lo = 0;
						fCCB->fV42P1Hi = 4;
					}
					else if ((v & 4) && dict >= 0x200)
					{
						fCCB->fV42P1Lo = 0;
						fCCB->fV42P1Hi = 2;
					}
					fCCB->fV42P2 = 0x20;
					fCCB->fV42P0 = 3;
					compression = kMNPCompressionV42bis;
				}
				else if (compression == 0)
				{
					if ((v & 2) && (fCCB->fCompressionAllowed & kMNPCompressionMNP7))
						compression = kMNPCompressionMNP7;
					else if ((v & 1) && (fCCB->fCompressionAllowed & kMNPCompressionMNP5))
						compression = kMNPCompressionMNP5;
				}
			}
			break;
		case 0x0e:
			{
				int p0 = list.Get();
				if (p0 == 0 || (fCCB->fCompressionAllowed & kMNPCompressionV42bis) == 0)
				{
					list.Seek(3, kSeekFromHere);
					break;
				}
				fCCB->fV42P0 = p0;
				UByte b0 = list.Get();
				UByte b1 = list.Get();
				Long p1 = (b0 << 8) | b1;
				if (p1 < 0x200)
				{
					// (ARA's dictionary of 8, taken as 2K; not recorded)
					if ((fCCB->fOptFlags & 2) == 0 || p1 != 8)
					{
						ok = false;
						break;
					}
					Long p2 = list.Get() & 0xff;
					if (p2 < 6 || p2 > 0xfa)
					{
						ok = false;
						break;
					}
					Long cur = fCCB->fV42P2;
					if (cur >= p2)
						cur = p2;
					fCCB->fV42P2 = cur;
				}
				else
				{
					fCCB->fOptFlags &= ~2;
					Long dict = (fCCB->fV42P1Hi << 8) | fCCB->fV42P1Lo;
					if (dict >= p1)
						dict = p1;
					fCCB->fV42P1Lo = dict;
					fCCB->fV42P1Hi = dict >> 8;
					Long p2 = list.Get() & 0xff;
					if (p2 < 6 || p2 > 0xfa)
					{
						ok = false;
						break;
					}
					Long cur = fCCB->fV42P2;
					if (cur < p2)
						p2 = cur;
					fCCB->fV42P2 = p2;
				}
				compression = kMNPCompressionV42bis;
			}
			break;
		case 0xc5:
			for (int i = 0; i < (int) length && ok; )
			{
				int sub = list.Get();
				if (sub == -1)
					ok = false;
				else
				{
					ULong subLength = list.Get() & 0xff;
					if (sub == 1)
					{
						UByte b[4];
						for (int j = 0; j < 4; j++)
							b[j] = list.Get();
						ULong rate = ((ULong) b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
						if (fRequestedSpeed < rate)
							rate = fRequestedSpeed;
						fNegotiatedSpeed = rate;
						if (!acceptor)
							fDataRate = ChangeSpeed(rate);
					}
					i += subLength;
				}
				i += 2;
			}
			break;
		default:
			list.Seek(length, kSeekFromHere);
			if (!acceptor)
				ok = false;
			break;
		}
	}

	if (!(haveFraming && framing == 2))
		ok = false;
	else if (ok)
	{
		if (!haveWindow)
			fCCB->fWindow = 1;
		fCCB->fSendLimit = fCCB->fUnacked + fCCB->fWindow;
		if (haveN401)
		{
			if (haveOpt && (opt & 1) && fCCB->fMaxDataSize == 0x40)
				fCCB->fDataSize = 0x100;
			else
				fCCB->fOptFlags &= ~4;
		}
		else
		{
			fCCB->fMaxDataSize = 0x104;
			fCCB->fDataSize = 0x104;
			fCCB->fOptFlags &= ~4;
		}
		if (haveOpt)
		{
			if ((opt & 2) == 0)
				fCCB->fClass4 = false;
			if ((fCCB->fOptFlags & 4) == 0)
				fCCB->fStats.fAdaptValue = fCCB->fDataSize;
		}
		else
			fCCB->fClass4 = false;
		if (compression == 0)
			fCCB->fCompression = kMNPCompressionNone;
		else if (compression == kMNPCompressionMNP5)
		{
			fCCB->fCompression = kMNPCompressionMNP5;
			fCCB->fCompressRefCon = fCCB->fClass5;
			fCCB->fCompress = MNPC5CompressHook;
			fCCB->fFlush = MNPC5FlushHook;
			fCCB->fMinFree = 3;
			fCCB->fMaxExpansion = 0xfa;
			fCCB->fDecompressRefCon = fCCB->fClass5;
			fCCB->fDecompress = MNPC5DecompressHook;
			MNPC5Init(fCCB->fClass5, MNPCompressOut, MNPDecompressOut, this);
		}
		else if (compression == kMNPCompressionMNP7)
			fCCB->fCompression = kMNPCompressionMNP7;
		else if (compression == kMNPCompressionV42bis)
		{
			fCCB->fCompression = kMNPCompressionV42bis;
			ULong directions;
			UByte p0 = fCCB->fV42P0;
			if (p0 == 3)
				directions = 3;
			else if (fCCB->fFlags & kMNPAcceptor)
				directions = (p0 == 1) ? 2 : 1;
			else
				directions = (p0 != 1) ? 2 : 1;
			if (directions & 1)
			{
				fCCB->fCompressRefCon = fCCB->fV42;
				fCCB->fCompress = V42EncodeHook;
				fCCB->fFlush = V42FlushHook;
				fCCB->fMinFree = 6;
			}
			if (directions & 2)
			{
				fCCB->fDecompressRefCon = fCCB->fV42;
				fCCB->fDecompress = V42DecodeHook;
				fCCB->fMaxExpansion = fCCB->fV42P2;
			}
			V42InitCompress(fCCB->fV42, directions, (fCCB->fV42P1Hi << 8) | fCCB->fV42P1Lo, fCCB->fV42P2,
							MNPCompressOut, MNPDecompressOut, this);
		}
		if ((fCCB->fCompressionAllowed & fCCB->fCompression) == 0)
		{
			ok = false;
			fCCB->fDisconnectReason = 3;
		}
		goto options;
	}
	fCCB->fDisconnectReason = 3;

options:
	if (fOptionsInfo.fOptions != nil)
	{
		TOptionIterator iter(fOptionsInfo.fOptions);
		TCMOMNPCompression* c = (TCMOMNPCompression*) iter.FindOption(kCMOMNPCompression);
		if (c != nil)
		{
			c->fCompressionType = compression;
			c->SetOpCodeResult((fCCB->fCompressionAllowed & fCCB->fCompression) ? 0xff : 0);
		}
	}
	fConnectInfo.fErrorFree = true;
	fConnectInfo.fSupportsCallBack = true;
	fConnectInfo.fViaAppleTalk = false;
	ULong bps = fDataRate;
	if (fCCB->fCompression & (kMNPCompressionMNP5 | kMNPCompressionV42bis))
		bps = (bps * 3) >> 1;
	fConnectInfo.fConnectBitsPerSecond = bps;
	return ok;
}


// ROM 0x00118528 ReceiveCredit__4TMNPFv
// How many more frames of the negotiated size there is room for (no more
// than the window).
ULong
TMNP::ReceiveCredit()
{
	ULong space = fCCB->fRcvData.BufferSpace();
	ULong credit = space / fCCB->fDataSize;
	if (fCCB->fWindow < credit)
		credit = fCCB->fWindow;
	return credit;
}


/*------------------------------------------------------------------------------
	Connecting
------------------------------------------------------------------------------*/

// ROM 0x0011ac98 ConnectStart__4TMNPFv
// An originator sends LR and waits for one back.
void
TMNP::ConnectStart()
{
	NewtonErr err = ConnectPreflight();
	if (err != noErr)
	{
		StartAbort(err);
		return;
	}
	HoldAbort();
	fCCB->fRetransTimer = fCCB->fRetransTime;
	XmitLR();
	SetTimer(1, kMNPTimerTick);
	AllowAbort();
}


// ROM 0x0011acf8 ListenStart__4TMNPFv
// An acceptor waits for an LR (for the listen time).
void
TMNP::ListenStart()
{
	NewtonErr err = ConnectPreflight();
	if (err != noErr)
	{
		StartAbort(err);
		return;
	}
	HoldAbort();
	fCCB->fFlags |= kMNPAcceptor;
	fCCB->fAcceptorTimer = fCCB->fAcceptorTime;
	SetTimer(1, kMNPTimerTick);
	AllowAbort();
}


// ROM 0x0011af38 AcceptStart__4TMNPFv
// The acceptor answers the LR with its own.
void
TMNP::AcceptStart()
{
	fCCB->fRetransTimer = fCCB->fRetransTime;
	fCCB->fFlags |= kMNPOriginator;
	XmitLR();
}


// ROM 0x0011af58 Disconnect__4TMNPFv
NewtonErr
TMNP::Disconnect()
{
	if ((fToolState & kToolStateWantAbort) == 0
	&&  (fToolState & (kToolStateConnecting | kToolStateConnected)) && fCCB != nil)
	{
		fCCB->fUserReason = 0;
		fCCB->fDisconnectReason = 0xff;
	}
	return TCommTool::Disconnect();
}


// ROM 0x0011af8c ReleaseStart__4TMNPFv
// With nothing left to send, the connection aborted.
NewtonErr
TMNP::ReleaseStart()
{
	if (fRequests[kCommToolPutChannel].fRequestPending || fCCB->fOutstanding != 0)
		return noErr;
	UByte state = fCCB->fXmitCur->fState;
	if (state == 0 || state == 4)
		return StartAbort(kCommErrConnectionAborted);
	return noErr;
}


/*------------------------------------------------------------------------------
	Timers
------------------------------------------------------------------------------*/

// ROM 0x0011b36c SetTimer__4TMNPFUlT1
// A message to the tool's own port in so many seconds, saying which timer.
// (A tick and the transmit abort share one message.)
void
TMNP::SetTimer(ULong seconds, ULong type)
{
	fTimerType = type;
	TTime delay(seconds, (TimeUnits) 0x384000);
	TTime now = GetGlobalTime();
	TTime when = now;
	CompAdd(&delay.time, &when.time);
	fTimerTime = when;
	NewtonErr err = fToolPort.Send(&fTimerMsg, &fTimerType, sizeof(fTimerType), 0, &fTimerTime, 0, false);
	if (err != noErr)
	{
		fCCB->fFlags |= kMNPTimerFailed;
		StartAbort(err);
	}
	else
		fToolState |= kMNPToolStateTimer;
}


// ROM 0x0011b464 SetXmitAbortTimer__4TMNPFv
void
TMNP::SetXmitAbortTimer()
{
	SetTimer(fCCB->fRetransTime, kMNPTimerXmitAbort);
	fToolState |= kMNPToolStateXmitAbort;
}


// ROM 0x0011b498 HandleXmitAbortTimer__4TMNPFv
// The frame being sent at termination did not go: killed, and on.
void
TMNP::HandleXmitAbortTimer()
{
	fToolState &= ~kMNPToolStateXmitAbort;
	KillWrite();
	TerminateConnection();
}


// ROM 0x0011b4cc HandleTickTimer__4TMNPFv
// A second: each running timer counted down, and the one that runs out
// acted on.
void
TMNP::HandleTickTimer()
{
	if (ShouldAbort(kMNPToolStateTimer, noErr))
		return;
	HoldAbort();
	if (fCCB->fRetransTimer > 0 && --fCCB->fRetransTimer == 0)
		RetransTimeOut();
	if (fCCB->fAckTimer > 0 && --fCCB->fAckTimer == 0)
		AckTimeOut();
	if (fCCB->fWindowTimer > 0 && --fCCB->fWindowTimer == 0)
		WindowTimeOut();
	if (fCCB->fInactivityTimer > 0 && --fCCB->fInactivityTimer == 0)
		InactiveTimeOut();
	if (fCCB->fAcceptorTimer > 0 && --fCCB->fAcceptorTimer == 0)
		AcceptorTimeOut();
	SetTimer(1, kMNPTimerTick);
	AllowAbort();
}


// ROM 0x00118564 RetransTimeOut__4TMNPFv
// Connecting: the LR again (up to four times); connected: the oldest LT
// not acknowledged again (up to eleven times).
void
TMNP::RetransTimeOut()
{
	if (fToolState & kToolStateConnecting)
	{
		if (fCCB->fStats.fLRRetransCount > 3)
		{
			fCCB->fDisconnectReason = 1;
			if (fTerminationEvent == 0)
				fTerminationEvent = 0x11;
			StartAbort(kMNPErrConnectRetries);
			return;
		}
		if ((fCCB->fFlags & kMNPAcceptor) && fNegotiatedSpeed != 0)
			fDataRate = ChangeSpeed(fSavedDataRate);
		XmitLR();
		return;
	}
	if (fCCB->fStats.fLTRetransCount >= 11)
	{
		fCCB->fDisconnectReason = 4;
		if (fTerminationEvent == 0)
			fTerminationEvent = 0xb;
		StartAbort(kMNPErrNotConnected);
		return;
	}
	TXmitBufDscrptr* oldest = &fCCB->fXmit[fCCB->fUnacked & 7];
	if (fCCB->fFlags & kMNPSendingLT)
		fCCB->fXmitRetrans = oldest;
	else
		fCCB->fXmitCur = oldest;
	fCCB->fFlags |= kMNPRetransmit;
	if (fCCB->fClass4 && fCCB->fStats.fAdaptValue > 0x20)
		fCCB->fStats.fAdaptValue -= 0x18;
	XmitLT();
}


// ROM 0x0011868c AckTimeOut__4TMNPFv
void
TMNP::AckTimeOut()
{
	if (fToolState & kToolStateConnected)
		XmitLA(kMNPLAPending);
}


// ROM 0x001186a0 WindowTimeOut__4TMNPFv
void
TMNP::WindowTimeOut()
{
	if (fToolState & kToolStateConnected)
		XmitLA(kMNPLAPending);
}


// ROM 0x00117824 InactiveTimeOut__4TMNPFv
void
TMNP::InactiveTimeOut()
{
	fCCB->fDisconnectReason = 5;
	if (fTerminationEvent == 0)
		fTerminationEvent = 0xc;
	StartAbort(kMNPErrNotConnected);
}


// ROM 0x0011784c AcceptorTimeOut__4TMNPFv
void
TMNP::AcceptorTimeOut()
{
	if (fTerminationEvent == 0)
		fTerminationEvent = 0x12;
	fCCB->fDisconnectReason = 0xff;
	StartAbort(kMNPErrConnectTimeOut);
}


/*------------------------------------------------------------------------------
	Termination
------------------------------------------------------------------------------*/

// ROM 0x00117978 GetNextTermProc__4TMNPFUlRUlRPFPv_Uc
// The timer stopped; the frame being sent finished (or given a while);
// an LD sent; the frame get killed; the CCB freed.
void
TMNP::GetNextTermProc(ULong terminationPhase, ULong& terminationFlag, TerminateProcPtr& terminationProc)
{
	TerminateProcPtr proc = CancelXmitAbortTimer;
	switch (terminationPhase)
	{
	case 0:		terminationFlag = kMNPToolStateTimer;		proc = CancelTimer;		break;
	case 1:		terminationFlag = kMNPToolStateXmit;		proc = CancelXmit;		break;
	case 2:
	case 4:		terminationFlag = kMNPToolStateXmitAbort;							break;
	case 3:		terminationFlag = kMNPToolStateSendLD;		proc = XmitLD;			break;
	case 5:		terminationFlag = kMNPToolStateRcv;			proc = CancelRcv;		break;
	case 6:		terminationFlag = kMNPToolStateCCB;			proc = CleanupCCB;		break;
	default:	terminationFlag = 0;												break;
	}
	terminationProc = proc;
}


// ROM 0x00117888 CancelTimer__4TMNPFv
Boolean
TMNP::CancelTimer(void* tool)
{
	TMNP* self = (TMNP*) tool;
	self->fTimerMsg.Abort();
	self->fToolState &= ~kMNPToolStateTimer;
	return true;
}


// ROM 0x001178b8 CancelXmit__4TMNPFv
// The other end has gone (an LD came): the frame killed; else it is given
// the retransmission time.
Boolean
TMNP::CancelXmit(void* tool)
{
	TMNP* self = (TMNP*) tool;
	if (self->fCCB->fFlags & kMNPLDReceived)
	{
		self->KillWrite();
		return true;
	}
	self->SetXmitAbortTimer();
	return false;
}


// ROM 0x001178ec CancelXmitAbortTimer__4TMNPFv
Boolean
TMNP::CancelXmitAbortTimer(void* tool)
{
	TMNP* self = (TMNP*) tool;
	self->fToolState &= ~kMNPToolStateXmitAbort;
	CancelTimer(tool);
	return true;
}


// ROM 0x00117910 CancelRcv__4TMNPFv
Boolean
TMNP::CancelRcv(void* tool)
{
	TMNP* self = (TMNP*) tool;
	self->fCCB->fFlags |= kMNPKillingRcv;
	self->TFramedAsyncSerTool::KillGet();
	return true;
}


// ROM 0x00117938 CleanupCCB__4TMNPFv
// The tool's framing put back, and the CCB freed.
Boolean
TMNP::CleanupCCB(void* tool)
{
	TMNP* self = (TMNP*) tool;
	self->fToolState &= ~kMNPToolStateCCB;
	self->SetFramingCtl(&self->fCCB->fSavedFraming);
	self->FreeCCB();
	return true;
}


// ROM 0x00117874 KillWrite__4TMNPFv
void
TMNP::KillWrite()
{
	fCCB->fFlags |= kMNPKillingXmit;
	TFramedAsyncSerTool::KillPut();
}


/*------------------------------------------------------------------------------
	The client's requests
------------------------------------------------------------------------------*/

// ROM 0x0011a718 PutBytes__4TMNPFP11CBufferList
// The client's data taken a piece at a time into LT frames.
void
TMNP::PutBytes(CBufferList* clientBuffer)
{
	if (clientBuffer->GetSize() == 0)
	{
		TCommTool::PutComplete(noErr, 0);
		return;
	}
	if (fToolState & kToolStateWantAbort)
		return;
	fCCB->fClientPut = clientBuffer;
	XmitStartBuffer();
}


// ROM 0x0011a778 PutFramedBytes__4TMNPFP11CBufferListUc
void
TMNP::PutFramedBytes(CBufferList* clientBuffer, Boolean endOfFrame)
{
	TCommTool::PutComplete(kCommErrBadCommand, 0);
}


// ROM 0x0011a788 PutComplete__4TMNPFlUl
// A frame of the tool's own has gone (an LD, or anything else); the
// client's put is TSerTool's.
void
TMNP::PutComplete(NewtonErr result, ULong putBytesCount)
{
	if ((fToolState & kMNPToolStateXmit) == 0)
	{
		TSerTool::PutComplete(result, putBytesCount);
		return;
	}
	fPutBuffer = nil;
	if (fCCB->fSendingLD)
	{
		fCCB->fSendingLD = false;
		XmitLDComplete(result, putBytesCount);
		return;
	}
	HoldAbort();
	XmitFrameComplete(result, putBytesCount);
	AllowAbort();
}


// ROM 0x0011a814 KillPut__4TMNPFv
void
TMNP::KillPut()
{
	TCommTool::PutComplete(kCommErrRequestCanceled, fCCB->fClientPut->Position());
	fCCB->fClientPut = nil;
	KillPutComplete(noErr);
}


// ROM 0x0011a868 KillPutComplete__4TMNPFl
// ROM QUIRK (kept): the flag looked at is CancelRcv's (kMNPKillingRcv) where
// KillWrite's would be expected (KillGetComplete looks at KillWrite's), so
// the frame put KillWrite kills is answered as a kill of the client's put.
void
TMNP::KillPutComplete(NewtonErr result)
{
	if (fCCB != nil && (fCCB->fFlags & kMNPKillingRcv))
	{
		fCCB->fFlags &= ~kMNPKillingRcv;
		return;
	}
	TCommTool::KillPutComplete(result);
}


// ROM 0x0011a88c GetBytes__4TMNPFP11CBufferList
void
TMNP::GetBytes(CBufferList* clientBuffer)
{
	fCCB->fFlags &= ~kMNPImmediateGet;
	fCCB->fClientGetThreshold = 0;
	GetBytesStart(clientBuffer);
}


// ROM 0x0011a990 GetFramedBytes__4TMNPFP11CBufferList
void
TMNP::GetFramedBytes(CBufferList* clientBuffer)
{
	TCommTool::GetComplete(kCommErrBadCommand, false, 0);
}


// ROM 0x0011a9a4 GetBytesImmediate__4TMNPFP11CBufferListl
void
TMNP::GetBytesImmediate(CBufferList* clientBuffer, Size threshold)
{
	fCCB->fFlags |= kMNPImmediateGet;
	fCCB->fClientGetThreshold = threshold;
	GetBytesStart(clientBuffer);
}


// ROM 0x0011a9c0 GetBytesStart__4TMNPFP11CBufferList
void
TMNP::GetBytesStart(CBufferList* clientBuffer)
{
	fCCB->fClientGetSize = clientBuffer->GetSize();
	fCCB->fClientGetLeft = fCCB->fClientGetSize;
	if (fCCB->fClientGetSize == 0)
	{
		TCommTool::GetComplete(noErr, false, 0);
		return;
	}
	if (fToolState & kToolStateWantAbort)
		return;
	fCCB->fClientGet = clientBuffer;
	RcvStartBuffer();
}


// ROM 0x0011aa3c GetComplete__4TMNPFlUcUl
// The frame get has finished (else it is the client's, TSerTool's).
void
TMNP::GetComplete(NewtonErr result, Boolean endOfFrame, ULong getBytesCount)
{
	if ((fToolState & kMNPToolStateRcv) == 0)
	{
		TSerTool::GetComplete(result, endOfFrame, getBytesCount);
		return;
	}
	fGetBuffer->Hide(fGetSize, kSeekFromEnd);
	fGetBuffer = nil;
	HoldAbort();
	RcvFrameComplete(result, endOfFrame);
	AllowAbort();
}


// ROM 0x0011aabc KillGet__4TMNPFv
void
TMNP::KillGet()
{
	TCommTool::GetComplete(kCommErrRequestCanceled, false, fCCB->fClientGet->Position());
	fCCB->fClientGet = nil;
	fCCB->fClientGetSize = 0;
	fCCB->fClientGetLeft = 0;
	KillGetComplete(noErr);
}


// ROM 0x0011ab28 KillGetComplete__4TMNPFl
// (See KillPutComplete: the flag is KillWrite's.)
void
TMNP::KillGetComplete(NewtonErr result)
{
	if (fCCB != nil && (fCCB->fFlags & kMNPKillingXmit))
	{
		fCCB->fFlags &= ~kMNPKillingXmit;
		return;
	}
	TCommTool::KillGetComplete(result);
}


// ROM 0x0011afc4 ProcessOptionStart__4TMNPFP7TOptionUlT2
// The MNP options, the idle and listen timers; 'sbav answers what has been
// received; a discard is refused; anything else the framed tool's.
// ROM BUG (kept): a current 'mdct is read out of the CCB without looking
// whether there is one.
ULong
TMNP::ProcessOptionStart(TOption* opt, ULong label, ULong opcode)
{
	Boolean set = (opcode == opSetNegotiate || opcode == opSetRequired);
	ULong* value = (ULong*) (opt + 1);
	switch (label)
	{
	case kCMOListenTimer:
		if (set)
		{
			ULong v = *value;
			fListenTime = v;
			if (fCCB != nil)
			{
				fCCB->fAcceptorTime = v;
				if (fCCB->fAcceptorTimer != 0)
					fCCB->fAcceptorTimer = v;
			}
		}
		else if (opcode == opGetDefault)
		{
			TCMOListenTimer def;
			opt->CopyDataFrom(&def);
		}
		else
			*value = fListenTime;
		return noErr;

	case kCMOMNPAllocate:
		if (set)
			fAllocate = *(UByte*) value;
		else if (opcode == opGetDefault)
		{
			TCMOMNPAllocate def;
			opt->CopyDataFrom(&def);
		}
		else
			*(UByte*) value = fAllocate;
		return noErr;

	case kCMOMNPCompression:
		if (set)
			fCompressionOpt.CopyDataFrom(opt);
		else if (opcode == opGetDefault)
		{
			TCMOMNPCompression def;
			opt->CopyDataFrom(&def);
		}
		else
			opt->CopyDataFrom(&fCompressionOpt);
		return noErr;

	case kCMOMNPDataRate:
		if (set)
			fDataRate = *value;
		else
			*value = (opcode == opGetDefault) ? 2400 : fDataRate;
		return noErr;

	case kCMOMNPStatistics:
		if (set)
		{
			if (fCCB == nil)
				return opFailure;
			fCCB->fStats = *(TCMOMNPStatistics*) opt;		// (the whole option)
			return noErr;
		}
		if (opcode == opGetDefault)
		{
			TCMOMNPStatistics def;
			opt->CopyDataFrom(&def);
			return noErr;
		}
		if (fCCB == nil)
			return opFailure;
		opt->CopyDataFrom(&fCCB->fStats);
		return noErr;

	case kCMOIdleTimer:
		if (set)
		{
			ULong v = *value;
			fIdleTime = v;
			if (fCCB != nil)
				fCCB->fInactivityTime = v;
		}
		else if (opcode == opGetDefault)
		{
			TCMOIdleTimer def;
			opt->CopyDataFrom(&def);
		}
		else
			*value = fIdleTime;
		return noErr;

	case kCMOSerialBytesAvailable:
		if (set)
			return opReadOnly;
		if ((fToolState & kToolStateConnected) == 0 || fCCB == nil)
			*value = 0;
		else
			*value = fCCB->fRcvData.BufferCount();
		return noErr;

	case kCMOMNPDebugConnect:
		if (set)
		{
			if (fToolState & kToolStateConnected)
				return opFailure;
			fDebugConnect.CopyDataFrom(opt);
			return noErr;
		}
		if (opcode == opGetDefault)
		{
			TCMOMNPDebugConnect def;
			opt->CopyDataFrom(&def);
			return noErr;
		}
		{
			TCMOMNPDebugConnect* d = (TCMOMNPDebugConnect*) opt;
			d->fMaxCredit = fCCB->fWindow;
			d->fClass4 = fCCB->fClass4;
			d->fMaxDataSize = fCCB->fMaxDataSize;
			d->fStreamModeMax = (fCCB->fOptFlags & 4) != 0;
			d->fARACompatibleMode = (fCCB->fOptFlags & 2) != 0;
		}
		return noErr;

	case kCMOMNPSpeedNegotiation:
		if (set)
			fRequestedSpeed = *value;
		else if (opcode == opGetDefault)
		{
			TCMOMNPSpeedNegotiation def;
			opt->CopyDataFrom(&def);
		}
		else
			*value = fRequestedSpeed;
		return noErr;

	case kCMOSerialDiscard:
		return opFailure;

	default:
		return TFramedAsyncSerTool::ProcessOptionStart(opt, label, opcode);
	}
}


/*------------------------------------------------------------------------------
	Receiving
------------------------------------------------------------------------------*/

// ROM 0x00118900 RcvInit__4TMNPFv
void
TMNP::RcvInit()
{
	fCCB->fAckDelay = 0;
	fCCB->fFieldC5C = 1;
	fCCB->fRcvData.FlushBytes();
	FlushInputBytes();
	RcvFrame();
}


// ROM 0x00118948 RcvFrame__4TMNPFv
// The next frame asked of the framed tool.
void
TMNP::RcvFrame()
{
	if (fToolState & kToolStateWantAbort)
		return;
	fCCB->fRcvList.Reset();
	fToolState |= kMNPToolStateRcv;
	TSerTool::GetFramedBytes(&fCCB->fRcvList);
}


// ROM 0x00118bc0 RcvFrameComplete__4TMNPFlUc
// A frame has come (or a broken one: a bad CRC, a serial error, one that
// did not end); the next asked for; an LA and received data dealt with.
void
TMNP::RcvFrameComplete(NewtonErr result, Boolean endOfFrame)
{
	NewtonErr abortErr = noErr;
	Boolean good = true;
	if (result == kSerErr_CRCError)
		good = false;
	else if (result == kSerErr_AsyncError)
	{
		good = false;
		fCCB->fStats.fRcvAsyncErrTotal++;
	}
	else if (result != noErr)
		abortErr = result;
	else if (!endOfFrame)
		good = false;
	if (ShouldAbort(kMNPToolStateRcv, abortErr))
		return;
	if (good)
		RcvProcessFrame();
	else
		RcvBrokenFrame(result);
	RcvFrame();
	if (fCCB->fFlags & kMNPLAReceived)
		ProcessLA();
	if (fCCB->fFlags & kMNPDataReceived)
		RcvStartBuffer();
}


// ROM 0x00118c98 RcvBrokenFrame__4TMNPFl
void
TMNP::RcvBrokenFrame(NewtonErr result)
{
	fCCB->fStats.fRcvBrokenTotal++;
	if (fToolState & kToolStateConnecting)
	{
		fToolState |= kMNPToolStateSendLD;
		if (fCCB->fFlags & kMNPAcceptor)
			return;
		if (fCCB->fStats.fLRRetransCount <= 3)
		{
			XmitLR();
			return;
		}
		fCCB->fDisconnectReason = 4;
		if (fTerminationEvent == 0)
			fTerminationEvent = 0x11;
		StartAbort(kMNPErrConnectRetries);
		return;
	}
	if (fToolState & kToolStateConnected)
		XmitNAck();
}


// ROM 0x00118d08 RcvProcessFrame__4TMNPFv
// A good frame, by its type.
void
TMNP::RcvProcessFrame()
{
	fCCB->fStats.fFramesRcvd++;
	fCCB->fInactivityTimer = fCCB->fInactivityTime;
	if (fToolState & kToolStateConnecting)
		fToolState |= kMNPToolStateSendLD;
	fCCB->fRcvList.Seek(0, kSeekFromBeginning);
	fCCB->fRcvHeaderLength = fCCB->fRcvList.Get();
	fCCB->fRcvType = fCCB->fRcvList.Get();
	switch (fCCB->fRcvType)
	{
	case kMNPLNA:	RcvLNA();	return;
	case kMNPLR:	RcvLR();	return;
	case kMNPLD:	RcvLD();	return;
	case kMNPLT:	RcvLT();	return;
	case kMNPLA:	RcvLA();	return;
	case kMNPLN:	RcvLN();	return;
	default:
		fCCB->fDisconnectReason = 0xfe;
		if (fTerminationEvent == 0)
			fTerminationEvent = 0xe;
		StartAbort(kMNPErrNotConnected);
		return;
	}
}


// ROM 0x00118998 RcvLN__4TMNPFv
void
TMNP::RcvLN()
{ }


// ROM 0x0011899c RcvLNA__4TMNPFv
void
TMNP::RcvLNA()
{ }


// ROM 0x00118e30 RcvLR__4TMNPFv
// Connecting: an originator's answer (connected, and LA sent), or an
// acceptor's request (the listen answered; a repeat of it answered again).
void
TMNP::RcvLR()
{
	if ((fToolState & kToolStateConnecting) == 0)
	{
		if (fToolState & kToolStateConnected)
			XmitNAck();
		return;
	}
	if ((fCCB->fFlags & kMNPAcceptor) == 0)
	{
		if (ParamNegotiation(false))
		{
			if (fNegotiatedSpeed != 0)
				fDataRate = ChangeSpeed(fNegotiatedSpeed);
			EnterConnectedState();
			XmitLA(kMNPLAPending);
			return;
		}
	}
	else
	{
		if (fCCB->fFlags & kMNPOriginator)
		{
			RetransTimeOut();
			return;
		}
		fCCB->fAcceptorTimer = 0;
		if (ParamNegotiation(true))
		{
			ListenComplete(noErr);
			return;
		}
	}
	if (fTerminationEvent == 0)
		fTerminationEvent = 0x14;
	StartAbort(kMNPErrNegotiation);
}


// ROM 0x00118f30 RcvLD__4TMNPFv
// The other end has disconnected: its reason becomes the termination
// event.
void
TMNP::RcvLD()
{
	int type;
	CBufferList& list = fCCB->fRcvList;
	while ((type = list.Get()) != -1)
	{
		int length = list.Get();
		if (type == 1)
			fCCB->fDisconnectReason = list.Get();
		else if (type == 2)
			fCCB->fUserReason = list.Get();
		else
			list.Seek(length, kSeekFromHere);
	}
	NewtonErr err = kMNPErrNotConnected;
	fCCB->fFlags |= kMNPLDReceived;
	if (fTerminationEvent == 0)
	{
		switch (fCCB->fDisconnectReason)
		{
		case 1:		fTerminationEvent = 2;	err = kMNPErrHandshake;		break;
		case 2:		fTerminationEvent = 3;	err = kMNPErrIncompatible;	break;
		case 3:		fTerminationEvent = 4;	err = kMNPErrNegotiation;	break;
		case 4:		fTerminationEvent = 5;	break;
		case 5:		fTerminationEvent = 6;	break;
		case 6:		fTerminationEvent = 7;	break;
		case 0xfe:	fTerminationEvent = 8;	break;
		case 0xff:	fTerminationEvent = 9;	break;
		default:	fTerminationEvent = 1;	break;
		}
	}
	StartAbort(err);
}


// ROM 0x001190bc RcvLT__4TMNPFv
// Data: the next in sequence (and room for it) is kept and acknowledged
// (at once when half the window has come, else after a while); a repeat of
// the last is ignored once; anything else is answered with an LA saying
// what we have.
void
TMNP::RcvLT()
{
	if ((fToolState & kToolStateConnecting) && (fCCB->fFlags & kMNPOriginator))
	{
		EnterConnectedState();
		XmitLT();
	}
	if ((fToolState & kToolStateConnected) == 0)
		return;
	UByte seq;
	CBufferList& list = fCCB->fRcvList;
	if (fCCB->fClass4)
		seq = list.Get() & 0xff;
	else
	{
		Boolean found = false;
		int n = 1;
		if (fCCB->fRcvHeaderLength >= 1)
		{
			do
			{
				int type = list.Get();
				n++;
				int length = list.Get();
				n += length;
				if (type == 1)
				{
					seq = list.Get() & 0xff;
					found = true;
				}
				else
					list.Seek(length, kSeekFromHere);
			} while (fCCB->fRcvHeaderLength >= n);
		}
		if (!found)
		{
			fCCB->fDisconnectReason = 0xfe;
			if (fTerminationEvent == 0)
				fTerminationEvent = 0xe;
			StartAbort(kMNPErrNotConnected);
			return;
		}
	}
	list.Hide(fCCB->fRcvHeaderLength + 1, kSeekFromBeginning);
	fCCB->fRcvDataLength = list.GetSize();
	UByte last = fCCB->fRcvSeq;
	if (seq != (UByte) (last + 1))
	{
		if (last == seq && (fCCB->fFlags & kMNPDuplicateLT) == 0)
		{
			fCCB->fFlags |= kMNPDuplicateLT;
			return;
		}
		fCCB->fFlags &= ~kMNPDuplicateLT;
		XmitNAck();
		return;
	}
	if (ReceiveCredit() == 0)
	{
		fCCB->fFlags &= ~kMNPDuplicateLT;
		XmitNAck();
		return;
	}
	if (fCCB->fRcvDataLength > fCCB->fDataSize)
	{
		fCCB->fDisconnectReason = 0xfe;
		if (fTerminationEvent == 0)
			fTerminationEvent = 0xe;
		StartAbort(kMNPErrNotConnected);
		return;
	}
	fCCB->fFlags &= ~kMNPDuplicateLT;
	fCCB->fRcvSeq++;
	fCCB->fRcvUnacked++;
	if (fCCB->fAckDelay != 0)
	{
		fCCB->fAckDelay = 0;
		if (fCCB->fWindow > 1)
			fCCB->fWindowTimer = fCCB->fWindowTime;
	}
	fCCB->fStats.fBytesRcvd += fCCB->fRcvDataLength;
	fCCB->fRcvData.CopyIn(&list, &fCCB->fRcvDataLength);
	if ((int) fCCB->fRcvUnacked >= ((int) fCCB->fWindow >> 1))
		XmitLA(kMNPLAPending);
	else if (!XmitLA(kMNPLAWanted) && fCCB->fAckTimer == 0)
		fCCB->fAckTimer = fCCB->fRetransTime >> 1;
	fCCB->fFlags |= kMNPDataReceived;
}


// ROM 0x001193a4 RcvLA__4TMNPFv
// An acknowledgement: the last sequence number received and the credit,
// noted for ProcessLA.
void
TMNP::RcvLA()
{
	if (fCCB->fFlags & kMNPOriginator)
	{
		EnterConnectedState();
		XmitLT();
	}
	if ((fToolState & kToolStateConnected) == 0)
		return;
	CBufferList& list = fCCB->fRcvList;
	if (fCCB->fClass4)
	{
		fCCB->fAckSeq = list.Get();
		fCCB->fAckCredit = list.Get();
	}
	else
	{
		Boolean haveCredit = false, haveSeq = false;
		int type = list.Get();
		if (type != -1)
		{
			do
			{
				int length = list.Get();
				if (type == 1)
				{
					fCCB->fAckSeq = list.Get();
					haveSeq = true;
				}
				else if (type == 2)
				{
					fCCB->fAckCredit = list.Get();
					haveCredit = true;
				}
				else
					list.Seek(length, kSeekFromHere);
				type = list.Get();
			} while (type != -1);
			if (haveCredit && haveSeq)
				goto noted;
		}
		fCCB->fDisconnectReason = 0xfe;
		if (fTerminationEvent == 0)
			fTerminationEvent = 0xe;
		StartAbort(kMNPErrNotConnected);
		return;
	}
noted:
	fCCB->fFlags |= kMNPLAReceived;
}


// ROM 0x0011952c ProcessLA__4TMNPFv
// The frames the LA acknowledges freed (the window opened by its credit);
// an LA acknowledging nothing new is a request to send the oldest again
// (up to eleven times).
void
TMNP::ProcessLA()
{
	ULong acked = 0;
	fCCB->fFlags &= ~kMNPLAReceived;
	ULong flags = fCCB->fFlags;
	UByte last = fCCB->fLastSent;
	UByte first = fCCB->fUnacked;
	ULong outstanding = (UByte) (last - first + 1);
	if (outstanding != 0)
	{
		UByte ackSeq = fCCB->fAckSeq;
		UByte next = ackSeq + 1;
		acked = (UByte) (next - first);
		if ((int) acked > (int) outstanding || acked == 0)
		{
			TXmitBufDscrptr* oldest = &fCCB->fXmit[first & 7];
			if (flags & kMNPRetransmit)
			{
				if (fCCB->fXmitRetrans != oldest && fCCB->fXmitCur != oldest)
					return;
				fCCB->fStats.fLTRetransCount++;
			}
			if (fCCB->fStats.fLTRetransCount >= 11)
			{
				fCCB->fDisconnectReason = 4;
				if (fTerminationEvent == 0)
					fTerminationEvent = 0xb;
				StartAbort(kMNPErrNotConnected);
			}
			else
			{
				fCCB->fRetransTimer = 0;
				if (fCCB->fFlags & kMNPSendingLT)
					fCCB->fXmitRetrans = oldest;
				else
					fCCB->fXmitCur = oldest;
				fCCB->fFlags |= kMNPRetransmit;
				if (fCCB->fClass4 && fCCB->fStats.fAdaptValue > 0x20)
					fCCB->fStats.fAdaptValue -= 0x18;
				XmitLT();
			}
		}
		else
		{
			if (flags & kMNPRetransmit)
			{
				TXmitBufDscrptr* r = fCCB->fXmitRetrans;
				if (r == nil)
					r = fCCB->fXmitCur;
				if ((int) acked > (int) (UByte) (r->fSeq - first))
				{
					TXmitBufDscrptr* after = &fCCB->fXmit[next & 7];
					if (ackSeq == last)
						fCCB->fFlags = flags & ~kMNPRetransmit;
					if (fCCB->fFlags & kMNPSendingLT)
						fCCB->fXmitRetrans = after;
					else
						fCCB->fXmitCur = after;
				}
			}
			UByte old = fCCB->fUnacked;
			fCCB->fUnacked = old + acked;
			fCCB->fOutstanding -= acked;
			if (fCCB->fOutstanding == 0)
				fCCB->fRetransTimer = 0;
			else
				fCCB->fRetransTimer = fCCB->fRetransTime;
			fCCB->fStats.fLTRetransCount = 0;
			TXmitBufDscrptr* b = &fCCB->fXmit[old & 7];
			for (int n = acked; n > 0; n = (UByte) (n - 1))
			{
				if (b->fRetransmitted && (fCCB->fFlags & kMNPSendingLT) && fCCB->fXmitCur == b)
					b->fState = 4;
				else
					b->fState = 0;
				b = b->fNext;
			}
			if (fToolState & kToolStateRelease)
				ReleaseStart();
		}
	}
	fCCB->fSendLimit = fCCB->fUnacked + fCCB->fAckCredit;
	if (fCCB->fAckCredit != 0 && fCCB->fXmitCur->fState != 0 && (fToolState & kMNPToolStateXmit) == 0)
		XmitLT();
	if (acked != 0)
		XmitStartBuffer();
}


// ROM 0x001189a0 RcvStartBuffer__4TMNPFv
void
TMNP::RcvStartBuffer()
{
	fCCB->fFlags &= ~kMNPDataReceived;
	if (fCCB->fFlags & kMNPRcvPending)
		return;
	if (fCCB->fClientGet != nil)
	{
		fCCB->fFlags |= kMNPRcvPending;
		RcvBuffer();
	}
}


// ROM 0x001189d4 RcvBuffer__4TMNPFv
// The received data decompressed into fReadBuf and handed to the client's
// get as it fills (an immediate get once its threshold has come); an LA
// once there is room again after saying there was none.
void
TMNP::RcvBuffer()
{
	for (;;)
	{
		ULong count = fCCB->fReadBuf.BufferCount();
		if (fCCB->fClientGetLeft <= count)
		{
			if (fCCB->fClientGetLeft != 0)
				fCCB->fReadBuf.CopyOut(fCCB->fClientGet, &fCCB->fClientGetLeft, nil);
			fCCB->fStats.fReadBytesOut += fCCB->fClientGetSize;
			TCommTool::GetComplete(noErr, false, fCCB->fClientGetSize);
			fCCB->fClientGet = nil;
			fCCB->fClientGetSize = 0;
			fCCB->fClientGetLeft = 0;
			break;
		}
		if (fCCB->fReadBuf.BufferSpace() <= fCCB->fMaxExpansion)
		{
			fCCB->fReadBuf.CopyOut(fCCB->fClientGet, &fCCB->fClientGetLeft, nil);
			count = 0;
		}
		UByte byte;
		if (fCCB->fRcvData.GetNextByte(&byte) != kCircleBufEmpty)
		{
			fCCB->fStats.fReadBytesIn++;
			fCCB->fDecompress(fCCB->fDecompressRefCon, byte);
			continue;
		}
		if ((fCCB->fFlags & kMNPImmediateGet)
		&&  fCCB->fClientGetSize - fCCB->fClientGetLeft + count >= fCCB->fClientGetThreshold)
		{
			if (count != 0)
				fCCB->fReadBuf.CopyOut(fCCB->fClientGet, &fCCB->fClientGetLeft, nil);
			fCCB->fClientGet->Hide(fCCB->fClientGetLeft, kSeekFromEnd);
			ULong got = fCCB->fClientGetSize - fCCB->fClientGetLeft;
			fCCB->fStats.fReadBytesOut += got;
			TCommTool::GetComplete(noErr, false, got);
			fCCB->fClientGet = nil;
			fCCB->fClientGetSize = 0;
			fCCB->fClientGetLeft = 0;
		}
		break;
	}
	fCCB->fFlags &= ~kMNPRcvPending;
	if ((fCCB->fFlags & kMNPZeroCredit) && ReceiveCredit() != 0)
		XmitLA(kMNPLAPending);
}


// ROM 0x00118bb0 MNPDecompressOut__4TMNPFUc
void
TMNP::MNPDecompressOut(void* tool, UByte byte)
{
	((TMNP*) tool)->fCCB->fReadBuf.PutNextByte(byte);
}


/*------------------------------------------------------------------------------
	Sending
------------------------------------------------------------------------------*/

// ROM 0x0011a03c XmitPostRequest__4TMNPFP11CBufferListUc
// A frame (or a piece of one) to the framed tool.
void
TMNP::XmitPostRequest(CBufferList* frame, Boolean endOfFrame)
{
	fToolState |= kMNPToolStateXmit;
	TSerTool::PutFramedBytes(frame, endOfFrame);
}


// ROM 0x00119858 XmitLR__4TMNPFv
// The link request: the constant, octet framing, the window, N401, class 4
// (with stream mode), the compression we would have, and the speed.
void
TMNP::XmitLR()
{
	if ((fToolState & kToolStateConnecting) == 0 || (fToolState & kMNPToolStateXmit))
		return;
	TXmitBufDscrptr& buf = fCCB->fXmit[0];
	UByte* p = buf.fFrame;
	buf.fList.Reset();
	UByte length = 0x14;
	if (fCCB->fClass4)
		length = 0x17;
	ULong compression = fCCB->fCompression;
	if (compression != 0)
	{
		if (fCCB->fOptFlags & 2)
		{
			if (compression & 0xe)
				length += 3;
		}
		else
		{
			if (compression & 6)
				length += 3;
			if (compression & 8)
				length += 6;
		}
	}
	p[0] = length;
	p[1] = kMNPLR;
	p[2] = 2;
	p[3] = 1;	p[4] = 6;	p[5] = 1;	p[6] = 0;	p[7] = 0;	p[8] = 0;	p[9] = 0;	p[10] = 0xff;
	p[11] = 2;	p[12] = 1;	p[13] = 2;
	p[14] = 3;	p[15] = 1;	p[16] = fCCB->fWindow;
	p[17] = 4;	p[18] = 2;	p[19] = fCCB->fMaxDataSize;	p[20] = (fCCB->fMaxDataSize >> 8) & 0xff;
	int n = 0x15;
	if (fCCB->fClass4)
	{
		p[21] = 8;
		p[22] = 1;
		p[23] = ((fCCB->fOptFlags & 4) ? 1 : 0) | 2;
		n = 0x18;
	}
	compression = fCCB->fCompression;
	if (fCCB->fOptFlags & 2)
	{
		if (compression & 0xe)
		{
			p[n++] = 9;
			p[n++] = 1;
			p[n] = 0;
			if ((fCCB->fCompression & 8) && fCCB->fV42P0 == 3)
			{
				ULong dict = (fCCB->fV42P1Hi << 8) | fCCB->fV42P1Lo;
				if (dict == 0x200)
					p[n] = 4;
				else if (dict == 0x400)
					p[n] = 8;
				else if (dict == 0x800)
					p[n] = 0x10;
			}
			if (fCCB->fCompression & 4)
				p[n] |= 2;
			if (fCCB->fCompression & 2)
				p[n] |= 1;
			n++;
		}
	}
	else
	{
		if (compression & 6)
		{
			p[n++] = 9;
			p[n++] = 1;
			p[n] = 0;
			if (fCCB->fCompression & 4)
				p[n] = 2;
			if (fCCB->fCompression & 2)
				p[n] |= 1;
			n++;
		}
		if (fCCB->fCompression & 8)
		{
			p[n++] = 0xe;
			p[n++] = 4;
			p[n++] = fCCB->fV42P0;
			p[n++] = fCCB->fV42P1Hi;
			p[n++] = fCCB->fV42P1Lo;
			p[n++] = fCCB->fV42P2;
		}
	}
	ULong speed = (fCCB->fFlags & kMNPAcceptor) ? fNegotiatedSpeed : fRequestedSpeed;
	if (speed != 0)
	{
		p[0] += 6;
		p[n++] = 0xc5;
		p[n++] = 6;
		p[n++] = 1;
		p[n++] = 4;
		p[n++] = speed >> 24;
		p[n++] = speed >> 16;
		p[n++] = speed >> 8;
		p[n++] = speed;
	}
	buf.fList.Hide(0x10a - n, kSeekFromEnd);
	fCCB->fFlags |= kMNPLRSent;
	XmitPostRequest(&buf.fList, true);
	fCCB->fStats.fLRRetransCount++;
	fCCB->fRetransTimer = fCCB->fRetransTime;
}


// ROM 0x00119c10 XmitLD__4TMNPFv
// The disconnect: the reason and the user's reason.  ==> false while it is
// being sent (the transmit abort timer running).
// ROM QUIRK (kept): the LA frame's list is not cut down to the LD's eight
// bytes, so the ten bytes of the LA frame's buffer go.
Boolean
TMNP::XmitLD(void* tool)
{
	TMNP* self = (TMNP*) tool;
	TMNP_CCB* ccb = self->fCCB;
	if (ccb->fFlags & kMNPLDReceived)
	{
		self->fToolState &= ~kMNPToolStateSendLD;
		return true;
	}
	ccb->fSendingLD = true;
	ccb->fLAList.Reset();
	ccb->fLAFrame[0] = 7;
	ccb->fLAFrame[1] = kMNPLD;
	ccb->fLAFrame[2] = 1;
	ccb->fLAFrame[3] = 1;
	ccb->fLAFrame[4] = ccb->fDisconnectReason;
	ccb->fLAFrame[5] = 2;
	ccb->fLAFrame[6] = 1;
	ccb->fLAFrame[7] = ccb->fUserReason;
	self->XmitPostRequest(&ccb->fLAList, true);
	self->SetXmitAbortTimer();
	return false;
}


// ROM 0x00119cf0 XmitLT__4TMNPFv
// The next LT (fXmitCur): sent again when it is a retransmission, or sent
// if the window allows; its header and sequence number go first
// (XmitLTContinue sends the data).
void
TMNP::XmitLT()
{
	TXmitBufDscrptr* buf = fCCB->fXmitCur;
	if (fToolState & kMNPToolStateXmit)
		return;
	UByte state = buf->fState;
	if (state == 0)
		return;
	if (state == 3)
	{
		if ((fCCB->fFlags & kMNPRetransmit) == 0)
			return;
		UByte limit = fCCB->fSendLimit;
		if ((int) (UByte) (buf->fSeq - limit) < 8 && buf->fSeq != limit)
			return;
		buf->fRetransmitted = 1;
		fCCB->fStats.fRetransTotal++;
		if (buf->fSeq == fCCB->fLastSent)
			fCCB->fStats.fLTRetransCount++;
	}
	else
	{
		if ((UByte) (fCCB->fSendLimit - fCCB->fUnacked) <= fCCB->fOutstanding)
			return;
		fCCB->fOutstanding++;
	}
	fCCB->fFlags |= kMNPSendingLT;
	fCCB->fSentOfCur = 0;
	CBufferList* list = &buf->fList;
	list->Reset();
	buf->fFrame[fCCB->fHeaderLength] = buf->fSeq;
	list->Hide(0x10a - (fCCB->fHeaderLength + 1), kSeekFromEnd);
	fCCB->fXmitState = 2;
	XmitPostRequest(list, false);
	if (fCCB->fRetransTimer == 0)
		fCCB->fRetransTimer = fCCB->fRetransTime;
}


// ROM 0x0011a58c XmitLTContinue__4TMNPFv
// The data put in the LT since the last piece went; when there is none,
// the frame's end.
void
TMNP::XmitLTContinue()
{
	Boolean end = false;
	TXmitBufDscrptr* buf = fCCB->fXmitCur;
	ULong newBytes = buf->fCount - fCCB->fSentOfCur;
	ULong from = fCCB->fHeaderLength + fCCB->fSentOfCur + 1;
	CBufferList* list = &buf->fList;
	list->Reset();
	list->Hide(from, kSeekFromBeginning);
	list->Hide(0x10a - (from + newBytes), kSeekFromEnd);
	if (newBytes != 0)
		fCCB->fSentOfCur += newBytes;
	else
	{
		if (buf->fState == 1)
		{
			fCCB->fXmitFill = buf->fNext;
			buf->fState = 2;
		}
		end = true;
		fCCB->fXmitState = 3;
	}
	XmitPostRequest(list, end);
}


// ROM 0x00119e58 XmitFrameComplete__4TMNPFlUl
// A frame (or a piece of an LT) has gone.
void
TMNP::XmitFrameComplete(NewtonErr result, ULong count)
{
	Boolean moved = false;
	if (result == noErr)
	{
		if (fCCB->fXmitState == 2)
		{
			XmitLTContinue();
			return;
		}
		fCCB->fStats.fFramesXmited++;
	}
	if (ShouldAbort(kMNPToolStateXmit, result))
		return;
	if (fCCB->fFlags & kMNPSendingLT)
	{
		fCCB->fXmitState = 1;
		TXmitBufDscrptr* buf = fCCB->fXmitCur;
		if (buf->fState != 4)
			buf->fState = 3;
		else
			buf->fState = 0;
		ULong flags = fCCB->fFlags & ~kMNPSendingLT;
		fCCB->fFlags = flags;
		if (!buf->fRetransmitted)
		{
			fCCB->fLastSent++;
			fCCB->fStats.fBytesXmited += buf->fCount;
		}
		else if (buf->fSeq == fCCB->fLastSent && fCCB->fXmitRetrans == nil)
			fCCB->fFlags = flags & ~kMNPRetransmit;
		if (fCCB->fXmitRetrans == nil)
			fCCB->fXmitCur = buf->fNext;
		else
		{
			fCCB->fXmitCur = fCCB->fXmitRetrans;
			moved = true;
			fCCB->fXmitRetrans = nil;
		}
	}
	else if (fCCB->fFlags & kMNPLRSent)
	{
		fCCB->fFlags &= ~kMNPLRSent;
		if ((fCCB->fFlags & kMNPAcceptor) && fNegotiatedSpeed != 0)
			fDataRate = ChangeSpeed(fNegotiatedSpeed);
	}
	if (fCCB->fFlags & kMNPLAPending)
		XmitLA(kMNPLAPending);
	if (fToolState & kToolStateConnected)
	{
		if (moved && (fCCB->fFlags & kMNPRetransmit) == 0)
			XmitStartBuffer();
		XmitLT();
	}
	if (fCCB->fFlags & kMNPLAWanted)
		XmitLA(kMNPLAWanted);
}


// ROM 0x0011a008 XmitLDComplete__4TMNPFlUl
void
TMNP::XmitLDComplete(NewtonErr result, ULong count)
{
	if (result == noErr)
		fCCB->fStats.fFramesXmited++;
	fToolState &= ~(kMNPToolStateXmit | kMNPToolStateSendLD);
	TerminateConnection();
}


// ROM 0x0011a050 XmitLA__4TMNPFUl
// An acknowledgement: the last received and our credit.  With the line
// busy, only noted (pendingFlag) for later.  ==> true if it was sent.
Boolean
TMNP::XmitLA(ULong pendingFlag)
{
	if (fToolState & kMNPToolStateXmit)
	{
		fCCB->fFlags |= pendingFlag;
		return false;
	}
	fCCB->fFlags &= ~(kMNPLAPending | kMNPLAWanted);
	fCCB->fAckTimer = 0;
	if (fCCB->fWindow > 1)
	{
		if (fCCB->fWindowTime <= fCCB->fAckDelay)
			fCCB->fWindowTimer = fCCB->fAckDelay;
		else
			fCCB->fWindowTimer = fCCB->fWindowTime;
	}
	fCCB->fRcvUnacked = 0;
	fCCB->fLAList.Reset();
	int n;
	if (fCCB->fClass4)
	{
		fCCB->fLAFrame[0] = 3;
		fCCB->fLAFrame[1] = kMNPLA;
		n = 2;
	}
	else
	{
		fCCB->fLAFrame[0] = 7;
		fCCB->fLAFrame[1] = kMNPLA;
		fCCB->fLAFrame[2] = 1;
		fCCB->fLAFrame[3] = 1;
		n = 4;
	}
	fCCB->fLAFrame[n++] = fCCB->fRcvSeq;
	if (!fCCB->fClass4)
	{
		fCCB->fLAFrame[n++] = 2;
		fCCB->fLAFrame[n++] = 1;
	}
	fCCB->fLAFrame[n] = ReceiveCredit();
	if (fCCB->fLAFrame[n] != 0)
		fCCB->fFlags &= ~kMNPZeroCredit;
	else
		fCCB->fFlags |= kMNPZeroCredit;
	fCCB->fLAList.Hide(10 - (n + 1), kSeekFromEnd);
	XmitPostRequest(&fCCB->fLAList, true);
	return true;
}


// ROM 0x0011a200 XmitNAck__4TMNPFv
// A frame out of order: an LA at once (the delay before the next such LA
// starting at an eighth of the retransmission time), or, while that delay
// runs, the ack timer set to it and the delay doubled (up to twice the
// retransmission time).
void
TMNP::XmitNAck()
{
	if (fCCB->fAckDelay > 0)
	{
		if (fCCB->fAckTimer > 0)
			return;
		fCCB->fAckTimer = fCCB->fAckDelay;
		fCCB->fStats.fForceAckTotal++;
		if (fCCB->fAckDelay >= fCCB->fRetransTime << 1)
			return;
		ULong delay = fCCB->fAckDelay << 1;
		fCCB->fAckDelay = delay;
		if (delay > fCCB->fRetransTime << 1)
			fCCB->fAckDelay = fCCB->fRetransTime << 1;
		return;
	}
	fCCB->fStats.fForceAckTotal++;
	fCCB->fAckDelay = fCCB->fRetransTime >> 3;
	if (fCCB->fAckDelay == 0)
		fCCB->fAckDelay = 1;
	XmitLA(kMNPLAPending);
}


// ROM 0x0011a29c MNPCompressOut__4TMNPFUc
// A byte (compressed, or not) into the LT being filled.
void
TMNP::MNPCompressOut(void* tool, UByte byte)
{
	TMNP* self = (TMNP*) tool;
	self->fCCB->fStats.fWriteBytesOut++;
	TXmitBufDscrptr* buf = self->fCCB->fXmitFill;
	if (buf->fState == 0)
		self->XmitInitBuffer(buf);
	buf->fFrame[self->fCCB->fHeaderLength + buf->fCount + 1] = byte;
	buf->fCount++;
	if (buf->fCount < buf->fCapacity)
		return;
	buf->fState = 2;
	self->fCCB->fXmitFill = buf->fNext;
}


// ROM 0x0011a328 MNPNilFlush__4TMNPFv
void
TMNP::MNPNilFlush(void* tool, UByte byte)
{ }


// ROM 0x0011a32c XmitStartBuffer__4TMNPFv
// The client's data (or what the compressor holds) into LTs.
void
TMNP::XmitStartBuffer()
{
	if (fCCB->fFlags & kMNPFillingLT)
		return;
	if (!fRequests[kCommToolPutChannel].fRequestPending && fCCB->fWriteBuf.BufferCount() == 0)
		return;
	fCCB->fFlags |= kMNPFillingLT;
	if (fCCB->fXmitFill->fState == 0)
		XmitInitBuffer(fCCB->fXmitFill);
	XmitBufferLT();
}


// ROM 0x0011a39c XmitInitBuffer__4TMNPFP15TXmitBufDscrptr
// A buffer started: the sequence number after the one before, and its
// size (class 4 adapting it between 32 and the negotiated size).
void
TMNP::XmitInitBuffer(TXmitBufDscrptr* buf)
{
	buf->fCount = 0;
	buf->fSeq = buf->fPrev->fSeq + 1;
	buf->fRetransmitted = 0;
	buf->fState = 1;
	XmitLT();
	ULong capacity;
	if (!fCCB->fClass4)
		capacity = fCCB->fDataSize;
	else
	{
		if (fCCB->fStats.fAdaptValue < fCCB->fDataSize)
			fCCB->fStats.fAdaptValue++;
		ULong adapt = fCCB->fStats.fAdaptValue;
		if (adapt <= 0x20)
			capacity = 0x20;
		else if (adapt <= 0x40)
			capacity = 0x40;
		else if (adapt <= 0x80)
			capacity = 0x80;
		else
			capacity = fCCB->fDataSize;
	}
	buf->fCapacity = capacity;
}


// ROM 0x0011a440 XmitBufferLT__4TMNPFv
// The client's put taken into fWriteBuf and on through the compressor into
// LT buffers while there are buffers to fill; the put answered when all of
// it has been taken, the compressor flushed when there is no more.
void
TMNP::XmitBufferLT()
{
	for (;;)
	{
		TXmitBufDscrptr* buf = fCCB->fXmitFill;
		UByte state = buf->fState;
		if (state != 1 && state != 0)
			break;
		if (buf->fNext->fState != 0 && state != 0
		&&  buf->fCapacity - buf->fCount < fCCB->fMinFree)
			break;
		UByte byte;
		if (fCCB->fWriteBuf.GetNextByte(&byte) == kCircleBufEmpty)
		{
			if (fCCB->fClientPut != nil)
			{
				ULong count = 0;
				if (fCCB->fWriteBuf.CopyIn(fCCB->fClientPut, &count) == kCircleBufNothingCopied)
				{
					TCommTool::PutComplete(noErr, fCCB->fClientPut->GetSize());
					fCCB->fClientPut = nil;
				}
				continue;
			}
			fCCB->fStats.fWriteFlushCount++;
			fCCB->fFlush(fCCB->fCompressRefCon, 0);
			break;
		}
		fCCB->fStats.fWriteBytesIn++;
		fCCB->fCompress(fCCB->fCompressRefCon, byte);
		if (fToolState & kToolStateWantAbort)
			break;
	}
	fCCB->fFlags &= ~kMNPFillingLT;
}


/*------------------------------------------------------------------------------
	TMNPService
------------------------------------------------------------------------------*/

PROTOCOL_IMPL_SOURCE_MACRO(TMNPService)
PROTOCOL_CLASSINFO(TMNPService, "TCMService", "serv\0mnps\0\0", 0x20000, 0, nil)

// ROM 0x001197d8 New__11TMNPServiceFv
TMNPService*
TMNPService::New()
{
	return this;
}


// ROM 0x001197dc Delete__11TMNPServiceFv
void
TMNPService::Delete()
{ }


// ROM 0x001197e0 Start__11TMNPServiceFP12TOptionArrayUlP12TServiceInfo
NewtonErr
TMNPService::Start(TOptionArray* options, ULong serviceId, TServiceInfo* serviceInfo)
{
	TMNP tool(serviceId);
	NewtonErr err = StartCommTool(&tool, serviceId, serviceInfo);
	if (err == noErr)
		err = OpenCommTool(serviceInfo->GetPortId(), options, this);
	return err;
}


// ROM 0x00119850 DoneStarting__11TMNPServiceFP7TAEventUlP12TServiceInfo
NewtonErr
TMNPService::DoneStarting(TAEvent* event, ULong size, TServiceInfo* serviceInfo)
{
	return ((TCommToolReply*) event)->fResult;
}


void
RegisterMNPService(void)
{
	TMNPService::ClassInfo()->Register();
}
