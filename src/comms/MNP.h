/*
	File:		comms/MNP.h

	Contains:	TMNP, the MNP tool ("MNP Tool", serv 'mnps): the error-
				correcting link the serial dock (and the modem's MNP) runs
				over, built on the framed async serial tool - every MNP
				frame is one SYN DLE STX ... DLE ETX CRC frame of
				TFramedAsyncSerTool.  The frames are LR (link request: the
				parameters negotiated), LA (link acknowledge: the last
				sequence number received and the credit left), LT (link
				transfer: numbered data), LN/LNA (attention; ignored) and LD
				(link disconnect).  Class 4 ("optimised") frames have short
				fixed headers; class 5 and V.42bis compression may be
				negotiated.

				The link's state lives in a TMNP_CCB (the connection control
				block) the tool allocates when it connects or listens: the
				eight transmit buffers (TXmitBufDscrptr, a ring of LT frames
				being filled, sent and acknowledged), the receive list and
				the decompressed data waiting for the client, the timers
				(counted down once a second by the tool's own timer message),
				and the statistics.

				The ROM's classes; their declarations are not in the DDK (the
				options are, MNPOptions.h), so the names of the fields are
				ours, their order the ROM's (offsets noted, for the ROM's
				layout).  The virtuals are the ROM's (TMNP's vtable
				0x0001f418).

				DEVIATION (pointer size): the tool's and the CCB's size are
				the host's; the compressors' callbacks and the termination
				procs, member functions in the ROM called with the tool as
				their first argument, are static members taking it.

	Reconstructed from the MP2x00 US ROM (0x001174e8-0x0011b840); each
	function cites its origin.  docs/comms/README.md, "The desktop
	connection (Dock)".
*/

#ifndef __COMMS_MNP_H
#define __COMMS_MNP_H

#ifndef __COMMS_SERIALTOOL_H
#include "SerialTool.h"
#endif
#include "MNPOptions.h"
#include "BufferList.h"
#include "BufferSegment.h"

struct TMNPClass5Vars;
struct TCompressVars;

// a compressor's output (a byte) and flush, and a decompressor's output
typedef void	(*MNPByteProc)(void* refCon, UByte byte);

// the CCB's fFlags
#define kMNPOriginator			0x00000001		// connecting (cleared when connected as acceptor)
#define kMNPAcceptor			0x00000002		// listening
#define kMNPDuplicateLT			0x00000004		// the last LT was a repeat of the one before
#define kMNPLAReceived			0x00000010		// an LA to process
#define kMNPDataReceived		0x00000020		// data to give the client
#define kMNPImmediateGet		0x00000040		// the client's get is GetBytesImmediate
#define kMNPKillingRcv			0x00001000		// CancelRcv's kill of the frame get
#define kMNPKillingXmit			0x00002000		// KillWrite's kill of the frame put
#define kMNPLRSent				0x00100000
#define kMNPFillingLT			0x00200000		// XmitBufferLT is filling a buffer
#define kMNPSendingLT			0x00800000		// the frame being sent is an LT
#define kMNPRetransmit			0x01000000		// an LT is to be sent again
#define kMNPZeroCredit			0x04000000		// the last LA said we have no room
#define kMNPLAPending			0x08000000		// an LA to send when the line is free
#define kMNPLAWanted			0x10000000		// an LA to send after the frame being sent
#define kMNPRcvPending			0x20000000		// RcvBuffer is working
#define kMNPLDReceived			0x40000000
#define kMNPTimerFailed			0x80000000

// the tool state bits TMNP adds (fToolState)
#define kMNPToolStateTimer		0x04000000		// the one-second timer is running
#define kMNPToolStateXmit		0x08000000		// a frame is being sent
#define kMNPToolStateXmitAbort	0x10000000		// the transmit abort timer is running
#define kMNPToolStateSendLD		0x20000000		// an LD is to be sent on termination
#define kMNPToolStateRcv		0x40000000		// the frame get is outstanding
#define kMNPToolStateCCB		0x80000000		// a CCB is allocated


// One of the eight transmit buffers: an LT frame's header and data.
class TXmitBufDscrptr
{
public:
						TXmitBufDscrptr();
						~TXmitBufDscrptr();

	TXmitBufDscrptr*	fNext;					// +0x00
	TXmitBufDscrptr*	fPrev;					// +0x04
	ULong				fCount;					// +0x08  data bytes in it
	ULong				fCapacity;				// +0x0c  data bytes it may hold
	UByte				fSeq;					// +0x10
	UByte				fState;					// +0x11  0 free, 1 filling, 2 full, 3 sent, 4 to send again
	UByte				fRetransmitted;			// +0x12
	CBufferList			fList;					// +0x14  over fSegment
	CBufferSegment		fSegment;				// +0x34  over fFrame
	UByte				fFrame[0x10a];			// +0x5c  the header, then the data
};


// The connection control block.
class TMNP_CCB
{
public:
						TMNP_CCB();
	NewtonErr			Init();

	ULong				fFlags;					// +0x000  kMNP...
	ULong				fOptFlags;				// +0x004  1 class 4 allowed, 2 ARA mode, 4 stream mode max
	Boolean				fSendingLD;				// +0x008
	ULong				fCompressionAllowed;	// +0x00c  kMNPCompression... ('mnpc)
	TCompressVars*		fV42;					// +0x010
	UByte				fV42P0;					// +0x014  V.42bis: direction
	UByte				fV42P2;					// +0x015  V.42bis: the longest string
	UByte				fV42P1Hi;				// +0x018  V.42bis: the dictionary size (big-endian)
	UByte				fV42P1Lo;				// +0x019
	TMNPClass5Vars*		fClass5;				// +0x01c
	void*				fCompressRefCon;		// +0x020
	void*				fDecompressRefCon;		// +0x024
	MNPByteProc			fCompress;				// +0x028
	MNPByteProc			fFlush;					// +0x02c
	MNPByteProc			fDecompress;			// +0x030
	ULong				fCompression;			// +0x034  the compression negotiated
	ULong				fMinFree;				// +0x038  what a buffer must have left for the compressor
	ULong				fMaxExpansion;			// +0x03c  what one byte may decompress to
	TCircleBuf			fReadBuf;				// +0x040  decompressed, for the client (0x302)
	TCircleBuf			fWriteBuf;				// +0x068  the client's, to compress (0x104)
	ULong				fField90;				// +0x090  (0x1e)
	ULong				fField94;				// +0x094  (0x32)
	UByte				fDisconnectReason;		// +0x098
	UByte				fUserReason;			// +0x099
	UByte				fWindow;				// +0x09a  k: frames in flight
	UByte				fClass4;				// +0x09b  optimised frames
	ULong				fMaxDataSize;			// +0x09c  N401
	ULong				fDataSize;				// +0x0a0
	TXmitBufDscrptr*	fXmitCur;				// +0x0a4  the next to send
	ULong				fXmitState;				// +0x0a8  1 idle, 2 sending an LT in pieces, 3 its end
	TXmitBufDscrptr*	fXmitRetrans;			// +0x0ac  a retransmission to go before it
	UByte				fSendLimit;				// +0x0b0  the first sequence number we may not send
	UByte				fUnacked;				// +0x0b1  the oldest not acknowledged
	UByte				fLastSent;				// +0x0b2
	ULong				fOutstanding;			// +0x0b4
	ULong				fSentOfCur;				// +0x0b8  of fXmitCur's data, how much has gone
	CBufferList*		fClientPut;				// +0x0bc
	TXmitBufDscrptr*	fXmitFill;				// +0x0c0  the one being filled
	TXmitBufDscrptr		fXmit[8];				// +0x0c4
	CBufferSegment		fLASegment;				// +0x0c04
	CBufferList			fLAList;				// +0x0c2c
	UByte				fLAFrame[10];			// +0x0c4c
	ULong				fHeaderLength;			// +0x0c58  of an LT (4, or 2 for class 4)
	ULong				fFieldC5C;				// +0x0c5c
	UByte				fRcvSeq;				// +0x0c60  the last received in order
	UByte				fRcvUnacked;			// +0x0c61  received since the last LA
	UByte				fRcvHeaderLength;		// +0x0c62
	UByte				fRcvType;				// +0x0c63
	ULong				fRcvDataLength;			// +0x0c64
	CBufferList*		fClientGet;				// +0x0c68
	ULong				fClientGetSize;			// +0x0c6c
	ULong				fClientGetLeft;			// +0x0c70
	ULong				fClientGetThreshold;	// +0x0c74
	CBufferList			fRcvList;				// +0x0c78
	CBufferSegment		fRcvSegment;			// +0x0c98
	TCircleBuf			fRcvData;				// +0x0cc0  received, to decompress (0x820)
	UByte				fAckCredit;				// +0x0ce8  the other end's credit
	UByte				fAckSeq;				// +0x0ce9  the last it received
	ULong				fInactivityTime;		// +0x0cec  seconds
	ULong				fAcceptorTime;			// +0x0cf0
	ULong				fRetransTime;			// +0x0cf4
	ULong				fWindowTime;			// +0x0cf8
	ULong				fRetransTimer;			// +0x0cfc  each counted down a second at a time
	ULong				fAckTimer;				// +0x0d00
	ULong				fWindowTimer;			// +0x0d04
	ULong				fInactivityTimer;		// +0x0d08
	ULong				fAcceptorTimer;			// +0x0d0c
	ULong				fAckDelay;				// +0x0d10
	TCMOFramingParms	fSavedFraming;			// +0x0d14
	TCMOMNPStatistics	fStats;					// +0x0d28
};


class TMNP : public TFramedAsyncSerTool
{
public:
						TMNP(ULong serviceId);
	virtual				~TMNP();

	virtual ULong		GetSizeOf();

	// the compressors' outputs
	static void			MNPCompressOut(void* tool, UByte byte);
	static void			MNPNilFlush(void* tool, UByte byte);
	static void			MNPDecompressOut(void* tool, UByte byte);

protected:
	virtual NewtonErr	TaskConstructor();
	virtual void		TaskDestructor();
	virtual UChar*		GetToolName();
	virtual void		HandleRequest(TUMsgToken& msgToken, ULong msgType);
	virtual NewtonErr	DoControl(ULong opCode, ULong msgType);
	virtual void		ConnectStart();
	virtual void		ListenStart();
	virtual void		AcceptStart();
	virtual NewtonErr	ReleaseStart();
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
	virtual void		GetNextTermProc(ULong terminationPhase, ULong& terminationFlag, TerminateProcPtr& terminationProc);

	// TMNP's own (+0x1b4; nothing calls it)
	virtual NewtonErr	Disconnect();

	// the connection
	NewtonErr			OpenAlloc();
	void				FreeCCB();
	NewtonErr			ConnectPreflight();
	void				InitConnectParms();
	void				ResetLink();
	void				InitFrameBufs();
	void				EnterConnectedState();
	void				SetRetransTimer();
	Boolean				ParamNegotiation(Boolean acceptor);
	ULong				ReceiveCredit();

	// the timers
	void				SetTimer(ULong seconds, ULong type);
	void				SetXmitAbortTimer();
	void				HandleTickTimer();
	void				HandleXmitAbortTimer();
	void				RetransTimeOut();
	void				AckTimeOut();
	void				WindowTimeOut();
	void				InactiveTimeOut();
	void				AcceptorTimeOut();
	void				DoCompressFile();

	// the termination procs
	static Boolean		CancelTimer(void* tool);
	static Boolean		CancelXmit(void* tool);
	static Boolean		CancelXmitAbortTimer(void* tool);
	static Boolean		XmitLD(void* tool);
	static Boolean		CancelRcv(void* tool);
	static Boolean		CleanupCCB(void* tool);
	void				KillWrite();

	// receiving
	void				RcvInit();
	void				RcvFrame();
	void				RcvFrameComplete(NewtonErr result, Boolean endOfFrame);
	void				RcvBrokenFrame(NewtonErr result);
	void				RcvProcessFrame();
	void				RcvLR();
	void				RcvLD();
	void				RcvLT();
	void				RcvLA();
	void				RcvLN();
	void				RcvLNA();
	void				ProcessLA();
	void				RcvStartBuffer();
	void				RcvBuffer();
	void				GetBytesStart(CBufferList* clientBuffer);

	// sending
	void				XmitLR();
	void				XmitLT();
	Boolean				XmitLA(ULong pendingFlag);
	void				XmitNAck();
	void				XmitFrameComplete(NewtonErr result, ULong count);
	void				XmitLDComplete(NewtonErr result, ULong count);
	void				XmitPostRequest(CBufferList* frame, Boolean endOfFrame);
	void				XmitStartBuffer();
	void				XmitInitBuffer(TXmitBufDscrptr* buffer);
	void				XmitBufferLT();
	void				XmitLTContinue();

	TCMOMNPCompression	fCompressionOpt;		// +0x54c  (its fCompressionType +0x558)
	ULong				fDataRate;				// +0x55c  ('eter)
	ULong				fSavedDataRate;			// +0x560
	ULong				fRequestedSpeed;		// +0x564  ('mnpn)
	ULong				fNegotiatedSpeed;		// +0x568
	TCMOMNPDebugConnect	fDebugConnect;			// +0x56c
	ULong				fIdleTime;				// +0x580  ('citr)
	ULong				fListenTime;			// +0x584  ('cltr)
	TMNP_CCB*			fCCB;					// +0x588
	Boolean				fAllocate;				// +0x58c  ('mnpa)
	ULong				fTimerType;				// +0x590  1 tick, 2 transmit abort
	TUAsyncMessage		fTimerMsg;				// +0x594
	TTime				fTimerTime;				// +0x5a4
	TObjectId			fTimerMsgId;			// +0x5ac
};


PROTOCOL TMNPService : public TCMService
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TMNPService);
	TMNPService*		New();
	void				Delete();
	NewtonErr			Start(TOptionArray* options, ULong serviceId, TServiceInfo* serviceInfo);
	NewtonErr			DoneStarting(TAEvent* event, ULong size, TServiceInfo* serviceInfo);
};

void	RegisterMNPService(void);


// MNP class 5 compression (MNPClass5.cpp)
NewtonErr	MNPC5Open(TMNPClass5Vars** vars);
void		MNPC5Close(TMNPClass5Vars* vars);
void		MNPC5Init(TMNPClass5Vars* vars, MNPByteProc compressOut, MNPByteProc decompressOut, void* refCon);
void		MNPC5CompressHook(void* vars, UByte byte);
void		MNPC5FlushHook(void* vars, UByte byte);
void		MNPC5DecompressHook(void* vars, UByte byte);

// V.42bis compression (V42bis.cpp)
NewtonErr	V42CreateCompressVars(TCompressVars** vars);
void		V42DisposeCompressVars(TCompressVars* vars);
void		V42InitCompress(TCompressVars* vars, ULong directions, ULong dictionarySize, ULong maxString,
							MNPByteProc compressOut, MNPByteProc decompressOut, void* refCon);
void		BTEncode(void* vars, UByte byte);
void		BTFlush(void* vars, UByte byte);
void		BTDecode(void* vars, UByte byte);

#endif	/* __COMMS_MNP_H */
