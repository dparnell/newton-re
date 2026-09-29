/*
	File:		comms/irda/IrLAP.h

	Contains:	IrLAP, the link access layer of the IrDA stack: TIrLAP,
				the link itself, and TIrLAPConn, its connection side (the
				LSAP connections over the one link).

				TIrLAP is HDLC-like in normal response mode over the SIR
				frames the tool sends and receives through TIrSIR.  Out of
				a connection (NDM) it discovers - XID commands to all in
				1, 6, 8 or 16 slots of 25 ms, answering each other station's
				XID in a slot chosen at random - and connects (SNRM with the
				quality of service wanted, answered by UA with the other
				side's; the lower device address wins a connection race);
				connected, the primary polls and the secondary answers,
				each sending up to a window of I frames (3-bit sequence
				numbers, acknowledged by N(R)) and then passing the turn
				with the P/F bit; RR and RNR say whether a station can take
				more, REJ asks for frames again, FRMR reports a frame it
				could not take, DISC and RD end it (answered UA, or DM out
				of a connection).  The timers: F (the answer to a poll), WD
				(the secondary's watchdog), the turn-around wait, and the
				link disconnect threshold as a count of them.  A TEST
				frame is echoed whatever the state.

				The states: 0 NDM, 1 query (discovering), 2 connect (SNRM
				sent), 3 listen (UA sent), 4 reply (answering a discovery),
				5 and 6 primary receive and transmit, 7 primary close,
				8 and 9 secondary receive and transmit, 10 secondary close.
				The timers are events 0x1c-0x25 (TimerComplete).

				TIrLAPConn keeps the LSAP connections on the link (a
				connect or listen starts the link for all of them), hands
				each frame received to the connection it is for
				(Demultiplexor: a get waiting, or kept until one comes) and
				answers a frame for no connection with a disconnect.

				Words in frames are big-endian as the ROM stores them.  The
				field names are ours, their order the ROM's (offsets noted,
				for the ROM's 0x17c and 0x30 bytes).

	Reconstructed from the MP2x00 US ROM (0x000f23dc-0x000f5e84); each
	function cites its origin.
*/

#ifndef __COMMS_IRLAP_H
#define __COMMS_IRLAP_H

#include "IrStream.h"
#include "IrLMP.h"
#include "IrSIR.h"

class TIrQOS;

// the link's states
enum
{
	kIrLAPDisconnected = 0,
	kIrLAPQuery,
	kIrLAPConnect,
	kIrLAPListen,
	kIrLAPReply,
	kIrLAPPriReceive,
	kIrLAPPriTransmit,
	kIrLAPPriClose,
	kIrLAPSecReceive,
	kIrLAPSecTransmit,
	kIrLAPSecClose
};

// the link's timers
enum
{
	kIrMediaBusyTimer		= 0x1c,				// listening for other traffic before discovery
	kIrSlotTimer			= 0x1d,				// a discovery slot
	kIrQueryInputTimer		= 0x1e,				// a slot's answer still coming in
	kIrQueryTimer			= 0x1f,				// answering a discovery
	kIrBackoffTimer			= 0x20,				// before trying SNRM again
	kIrFinalTimer			= 0x21,				// F: the answer to a poll
	kIrPollTimer			= 0x22,				// the primary's turn running out
	kIrWatchdogTimer		= 0x23,				// WD: the secondary's (and a listen's) watchdog
	kIrTurnaroundTimer		= 0x24				// the other side's turn-around time
};

// what a frame's control byte says (fRxType)
enum
{
	kIrIFrame		= 0x00,
	kIrRR			= 0x01,
	kIrUI			= 0x03,
	kIrRNR			= 0x05,
	kIrREJ			= 0x09,
	kIrSREJ			= 0x0d,
	kIrDM			= 0x0f,
	kIrXIDCmd		= 0x2f,
	kIrDISC			= 0x43,						// (RD as a response)
	kIrUA			= 0x63,
	kIrSNRM			= 0x83,
	kIrFRMR			= 0x87,
	kIrXIDRsp		= 0xaf,
	kIrTEST			= 0xe3
};

// what is received wrong (fRxErrors)
enum
{
	kIrNrUnexpected	= 0x01,						// N(R) is not V(S): frames are outstanding
	kIrNsUnexpected	= 0x02,						// N(S) is not V(R)
	kIrNrInvalid	= 0x04,						// N(R) acknowledges a frame not sent
	kIrNsInvalid	= 0x08						// N(S) outside the window
};


// An XID frame as it lies in the link's frame buffer: the address and
// control, then the information (big-endian words).
struct TXIDPacket
{
	UByte				fPad;					// +0x00
	UByte				fAddress;				// +0x01
	UByte				fControl;				// +0x02
	UByte				fFormat;				// +0x03  1
	UByte				fSrcDevAddr[4];			// +0x04
	UByte				fDstDevAddr[4];			// +0x08
	UByte				fFlags;					// +0x0c  the slot count's code, 4: pick a new address
	UByte				fSlot;					// +0x0d  0xff the last
	UByte				fVersion;				// +0x0e
	UByte				fInfo[1];				// +0x0f  the discovery information (in the final slot)
};


class TIrLAP : public TIrStream
{
public:
						TIrLAP();
	virtual				~TIrLAP();
	virtual void		NextState(ULong event);

	NewtonErr			Init(TIrGlue* glue, TIrLMP* lmp);
	void				DeInit(void);
	void				Reset(void);

	// the states
	void				HandleDisconnectedStateEvent(ULong event);
	void				HandleQueryStateEvent(ULong event);
	void				HandleConnectStateEvent(ULong event);
	void				HandleListenStateEvent(ULong event);
	void				HandleReplyStateEvent(ULong event);
	void				HandleNDMDisconnectRequest(void);
	void				HandlePriReceiveStateEvent(ULong event);
	void				HandlePriTransmitStateEvent(ULong event);
	void				HandlePriCloseStateEvent(ULong event);
	void				HandleSecReceiveStateEvent(ULong event);
	void				HandleSecTransmitStateEvent(ULong event);
	void				HandleSecCloseStateEvent(ULong event);

	// the connection
	NewtonErr			ParseNegotiateAndInitConnState(UByte primary);
	void				ConnLstnComplete(NewtonErr result);
	void				DisconnectComplete(NewtonErr result);
	void				ApplyDefaultConnParms(void);
	void				NotConnectedCompletion(void);
	void				UpdateNrReceived(void);
	void				ResendRejectedFrames(void);
	void				ProcessRecdInfoOrSuperFrame(void);
	void				CancelPutRequest(void);
	void				CancelPendingPutRequests(TLSAPConn* lsapConn, NewtonErr result);
	void				PutComplete(TIrDataXferEvent* event, NewtonErr result);
	void				PostponePutRequest(void);
	void				CopyStatsTo(TCMOSlowIRStats* stats);
	void				ResetStats(void);

	// the frames
	void				StartDataReceive(void);
	void				ReleaseInputBuffer(CBufferSegment* buffer);
	void				FreeGetBuffers(void);
	void				PrepareFRMRResponse(void);
	void				OutputXIDCommand(void);
	void				OutputXIDResponse(TXIDPacket& packet);
	void				OutputSNRMCommand(void);
	void				OutputUAResponse(void);
	void				OutputFRMRResponse(void);
	void				OutputControlFrame(UByte control);
	void				OutputDataFrame(TIrDataXferEvent* event, UByte final);
	Boolean				GotData(UByte* buffer, ULong length);
	Boolean				RecdCmd(UByte type);
	Boolean				RecdPollCmd(UByte type);
	Boolean				RecdRsp(UByte type);
	Boolean				RecdFinalRsp(UByte type);
	void				HandleTestFrame(void);
	void				TestFrameComplete(void);

	// the tool, through the glue
	void				StartTimer(ULong delay, int kind);
	void				StopTimer(void);
	void				TimerComplete(ULong kind);
	void				StartOutput(TIrLAPPutBuffer* frame, ULong extraBOFs);
	void				StopOutput(void);
	void				StartInput(CBufferSegment* buffer);
	void				StopInput(void);
	Boolean				InputHappening(void);
	void				OutputComplete(void);
	void				InputComplete(UByte address, UByte control);

	TIrGlue*			fIrGlue;				// +0x14  (the stream's fGlue too)
	TIrLMP*				fLMP;					// +0x18
	UByte				fState;					// +0x1c
	UByte				fConnAddr;				// +0x1d  the connection address (1-0x7e)
	ULong				fMyDevAddr;				// +0x20
	UByte				fMaxSlot;				// +0x24  the slot count less one
	UByte				fSlot;					// +0x25  0xff the final
	UByte				fDiscoveryFlags;		// +0x26
	Boolean				fFirstXID;				// +0x27  answering: the first XID of a discovery
	Boolean				fReplied;				// +0x28  answering: our XID sent
	ULong				fConflictDevAddr;		// +0x2c  -1 none
	ULong				fNewDevAddr;			// +0x30  answering: the address picked when asked to
	ULong				fPeerDevAddr;			// +0x34
	TIrEvent*			fRequest;				// +0x38  the discover, connect or listen in hand
	TIrEvent*			fDisconnectRequest;		// +0x3c
	TIrQOS*				fMyQOS;					// +0x40
	TIrQOS*				fPeerQOS;				// +0x44
	UByte				fVr;					// +0x48  V(R)
	UByte				fVs;					// +0x49  V(S)
	UByte				fNrProcessed;			// +0x4a  the frames acknowledged up to
	UByte				fWindow;				// +0x4b  frames left in this turn
	Boolean				fLocalBusy;				// +0x4c
	Boolean				fRemoteBusy;			// +0x4d
	Boolean				fSetLocalBusy;			// +0x4e  the last receive buffer is taken: say RNR
	Boolean				fClearLocalBusy;		// +0x4f  a buffer is free again: say RR
	Boolean				fDisconnectPending;		// +0x50
	Boolean				fDisconnectWithUA;		// +0x51  the secondary answers a DISC (rather than asking with RD)
	Boolean				fTestFrameActive;		// +0x52
	UByte				fTestHeader[2];			// +0x54
	Boolean				fFRMRPending;			// +0x58
	UByte				fFRMRInfo[3];			// +0x59
	ULong				fRetryCount;			// +0x5c
	ULong				fRetryWarning;			// +0x60  N1: the tool told the link is going
	ULong				fRetryLimit;			// +0x64  N2: the link is gone
	ULong				fFramesResent;			// +0x68  (the statistics)
	ULong				fProtocolErrors;		// +0x6c
	TIrEvent			fLocalBusyEvent;		// +0x78  (8 bytes in the ROM)
	UByte				fExtraBOFs;				// +0x80
	UByte				fMyWindowSize;			// +0x81
	UByte				fPeerWindowSize;		// +0x82
	ULong				fMyMaxTurnTime;			// +0x84
	ULong				fPeerMaxTurnTime;		// +0x88
	ULong				fWatchdogTime;			// +0x8c  the other's turn-around time and a quarter
	ULong				fPeerMinTurnTime;		// +0x90
	Boolean				fPrimary;				// +0x94
	Boolean				fNeedGetBuffer;			// +0x95  the last one went up with a frame
	Boolean				fMoreToSend;			// +0x96
	UByte				fSent;					// +0x97  the frame sent (0 an I frame)
	UByte				fRxControl;				// +0x98  the frame received
	UByte				fRxCommand;				// +0x99  its C/R bit
	UByte				fRxConnAddr;			// +0x9a
	UByte				fRxPF;					// +0x9b
	UByte				fRxNr;					// +0x9c
	UByte				fRxNs;					// +0x9d
	UByte				fRxType;				// +0x9e
	UByte				fUnacked;				// +0x9f  a bit for each frame sent and not acknowledged
	UByte				fRxWindow;				// +0xa0  a bit for each N(S) that may come
	UByte				fRxErrors;				// +0xa1
	UByte				fFrame[0x40];			// +0xa4  a control frame to send
	CBufferSegment		fControlSegment;		// +0xe4  the frame received when no get buffer takes it
	Boolean				fInputActive;			// +0x10c
	Boolean				fOutputActive;			// +0x10d
	UByte				fFreeGetBuffers;		// +0x10e  a bit each
	ULong				fNumGetBuffers;			// +0x110  (the window size)
	CBufferSegment*		fGetBuffers[8];			// +0x114
	CBufferSegment*		fInputBuffer;			// +0x134
	CList*				fPutRequests;			// +0x138  (the next at the end)
	TIrLAPPutBuffer		fPutBuffer;				// +0x13c
	TIrDataXferEvent*	fSentFrames[8];			// +0x15c  by N(S), until acknowledged
};


// the connection side's states
enum
{
	kIrLAPConnStandby = 0,
	kIrLAPConnConnectOrListen,
	kIrLAPConnActive
};


class TIrLAPConn : public TIrStream
{
public:
						TIrLAPConn();
	virtual				~TIrLAPConn();
	virtual void		NextState(ULong event);

	NewtonErr			Init(TIrGlue* glue, TIrLAP* lap);
	void				Reset(void);
	void				DeInit(void);
	void				HandleStandbyStateEvent(ULong event);
	void				HandleConnectOrListenStateEvent(ULong event);
	void				HandleActiveStateEvent(ULong event);
	void				HandleGetDataRequest(void);
	void				Demultiplexor(CBufferSegment* buffer);
	Boolean				ExtractHeader(CBufferSegment* buffer, TLMPDUHeader& header, ULong& length);
	Boolean				DataDelivered(TIrDataXferEvent* request, TLMPDUHeader& header, ULong headerLength, CBufferSegment* buffer);
	void				ReplyToInvalidFrame(TLMPDUHeader& header, UByte opCode, UByte info);
	ULong				FillInLMPDUHeader(TIrDataXferEvent* event, UByte* buffer);
	void				CleanupPendingGetRequestsAndReplies(TLSAPConn* lsapConn, NewtonErr result);
	void				CancelPendingGetRequests(TLSAPConn* lsapConn, NewtonErr result);
	void				TimerComplete(ULong kind);

	TIrGlue*			fIrGlue;				// +0x14  (the stream's fGlue too)
	TIrLAP*				fLAP;					// +0x18
	UByte				fState;					// +0x1c
	Boolean				fConnected;				// +0x1d
	ULong				fPeerDevAddr;			// +0x20
	CList*				fLSAPConns;				// +0x24
	CList*				fGetRequests;			// +0x28  gets waiting for a frame
	CList*				fPendingData;			// +0x2c  frames waiting for a get
};

#endif	/* __COMMS_IRLAP_H */
