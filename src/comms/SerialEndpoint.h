/*
	File:		comms/SerialEndpoint.h

	Contains:	TSerialEndpoint, the endpoint that talks to a comm tool - the
				only TEndpoint implementation in the ROM, whatever the
				service (the name is historical: CMGetEndpoint makes one
				unless an 'endp option names another) - and the parameter
				blocks it sends the tool its requests in.

				Each request goes in a TCommTool...PB: an async message (so
				the same block serves a synchronous and an asynchronous call),
				the request, the tool's reply, and the endpoint event the
				client is told about it with.  A synchronous call waits for
				the reply (TEndpointEventHandler::CallService); an
				asynchronous one keeps its PB on the pending list until
				HandleComplete finds it by its message id, when the
				Handle...Reply for its kind updates the endpoint's state and
				tells the client.  The put and get PBs are recycled through
				free lists.  An event request is kept outstanding from Open
				on, so the tool can say when the connection has gone.

				fInSyncCall is set while a synchronous call is in progress
				and cleared by an abort, which is how a call finds it was
				aborted (kError_Call_Aborted).

				DEVIATION (pointer size): request and reply sizes are the
				host's sizeof, not the ROM's.

	Reconstructed from the MP2x00 US ROM (0x001d9968-0x001dd504,
	0x00383360); each function cites its origin.  docs/comms/README.md.
*/

#ifndef __COMMS_SERIALENDPOINT_H
#define __COMMS_SERIALENDPOINT_H

#ifndef __ENDPOINT_H
#include "Endpoint.h"
#endif
#ifndef __COMMS_COMMTOOLS_H
#include "CommTools.h"
#endif

class CList;
class CBufferList;


//--------------------------------------------------------------------------------
//		The parameter blocks
//--------------------------------------------------------------------------------

class TCommToolPB : public TUAsyncMessage			// 0x1c bytes
{
public:
						TCommToolPB(ULong requestType, ULong clientRefCon, Boolean async);
	// DEVIATION: virtual, so a PB is freed as what it is wherever it is
	// freed (the ROM frees every PB on its pending list as a TUAsyncMessage)
	virtual				~TCommToolPB()		{ }

	ULong				fRequestType;		// +0x10  kCommToolRequestType...
	ULong				fClientRefCon;		// +0x14
	Boolean				fAsync;				// +0x18  the client is told when it completes
};

class TCommToolControlPB : public TCommToolPB		// 0x58
{
public:
						TCommToolControlPB(ULong opCode, Long eventCode, ULong clientRefCon, Boolean async);
	TCommToolControlRequest	fRequest;		// +0x1c
	TCommToolReply		fReply;				// +0x28
	TEndpointEvent		fEvent;				// +0x38
};

class TCommToolBindPB : public TCommToolPB			// 0x70
{
public:
						TCommToolBindPB(ULong opCode, ULong clientRefCon, Boolean async, Long eventCode);
	TCommToolBindRequest	fRequest;		// +0x1c
	TCommToolReply		fReply;				// +0x3c
	TBindCompleteEvent	fEvent;				// +0x4c
};

class TCommToolConnectPB : public TCommToolPB		// 0x88
{
public:
						TCommToolConnectPB(ULong opCode, Long eventCode, ULong clientRefCon, Boolean async);
	NewtonErr			Prepare(TOptionArray* options, CBufferSegment* data, Long* sequence, Boolean sync);
	TCommToolConnectRequest	fRequest;		// +0x1c
	TCommToolConnectReply	fReply;			// +0x44
	TConnectCompleteEvent	fEvent;			// +0x58
};

class TCommToolDisconnectPB : public TCommToolPB	// 0x74
{
public:
						TCommToolDisconnectPB(ULong clientRefCon, Boolean async);
	TCommToolDisconnectRequest	fRequest;	// +0x1c
	TCommToolReply		fReply;				// +0x38  (fResult is where a deferred disconnect keeps its timeout)
	TDisconnectEvent	fEvent;				// +0x48
};

class TCommToolOptMgmtPB : public TCommToolPB		// 0x6c
{
public:
						TCommToolOptMgmtPB(ULong clientRefCon, Boolean async);
	TCommToolOptionMgmtRequest	fRequest;	// +0x1c
	TCommToolReply		fReply;				// +0x38
	TOptMgmtCompleteEvent	fEvent;			// +0x48
};

class TCommToolAbortPB : public TCommToolPB			// 0x38
{
public:
						TCommToolAbortPB(ULong requestsToKill, ULong clientRefCon, Boolean async);
	TCommToolKillRequest	fRequest;		// +0x1c
	TCommToolReply		fReply;				// +0x28
};

class TCommToolEventPB : public TCommToolPB			// 0x4c
{
public:
						TCommToolEventPB(ULong clientRefCon);
	TCommToolControlRequest	fRequest;		// +0x1c
	TCommToolGetEventReply	fReply;			// +0x28
};

class TCommToolPutPB : public TCommToolPB			// 0x84
{
public:
						TCommToolPutPB(ULong clientRefCon, Boolean async);
						~TCommToolPutPB();
	TCommToolPutRequest	fRequest;			// +0x1c
	TCommToolPutReply	fReply;				// +0x38
	TSndCompleteEvent	fEvent;				// +0x4c
	CBufferSegment*		fSegment;			// +0x7c  over the client's bytes
	CBufferList*		fList;				// +0x80  what the request carries
};

class TCommToolGetPB : public TCommToolPB			// 0x90
{
public:
						TCommToolGetPB(ULong clientRefCon, Boolean async);
						~TCommToolGetPB();
	TCommToolGetRequest	fRequest;			// +0x1c
	TCommToolGetReply	fReply;				// +0x38
	TRcvCompleteEvent	fEvent;				// +0x50
	CBufferSegment*		fSegment;			// +0x84
	CBufferList*		fList;				// +0x88
	Boolean				fFramed;			// +0x8c
};


//--------------------------------------------------------------------------------
//		TSerialEndpoint
//--------------------------------------------------------------------------------

PROTOCOL TSerialEndpoint : public TEndpoint			// 0x44 bytes
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TSerialEndpoint);

	TSerialEndpoint*	New(void);
	void				Delete(void);
	TObjectId			DeleteLeavingTool(void);

	Boolean				HandleEvent(ULong msgType, TAEvent* event, ULong msgSize);
	Boolean				HandleComplete(TUMsgToken* msgToken, ULong* msgSize, TAEvent* event);
	NewtonErr			AddToAppWorld(void);
	NewtonErr			RemoveFromAppWorld(void);
	NewtonErr			Open(ULong clientHandler);
	NewtonErr			Close(void);
	NewtonErr			Abort(void);
	Boolean				SetSync(Boolean sync);
	NewtonErr			GetProtAddr(TOptionArray* bndAddr, TOptionArray* peerAddr, TTimeout timeOut);
	NewtonErr			OptMgmt(ULong arrayOpCode, TOptionArray* options, TTimeout timeOut);
	NewtonErr			Bind(TOptionArray* addr, Long* qlen, TTimeout timeOut);
	NewtonErr			UnBind(TTimeout timeOut);
	NewtonErr			Listen(TOptionArray* addr, TOptionArray* opt, CBufferSegment* data, Long* seq, TTimeout timeOut);
	NewtonErr			Accept(TEndpoint* resfd, TOptionArray* addr, TOptionArray* opt, CBufferSegment* data, Long seq, TTimeout timeOut);
	NewtonErr			Connect(TOptionArray* addr, TOptionArray* opt, CBufferSegment* data, Long* seq, TTimeout timeOut);
	NewtonErr			Disconnect(CBufferSegment* data, Long reason, Long seq);
	NewtonErr			Release(TTimeout timeOut);
	NewtonErr			Snd(UByte* buf, Size& nBytes, ULong flags, TTimeout timeOut);
	NewtonErr			Rcv(UByte* buf, Size& nBytes, Size thresh, ULong* flags, TTimeout timeOut);
	NewtonErr			Snd(CBufferSegment* buf, ULong flags, TTimeout timeOut);
	NewtonErr			Rcv(CBufferSegment* buf, Size thresh, ULong* flags, TTimeout timeOut);
	NewtonErr			WaitForEvent(TTimeout timeOut);
	NewtonErr			nBind(TOptionArray* opt, TTimeout timeOut, Boolean sync);
	NewtonErr			nListen(TOptionArray* opt, CBufferSegment* data, Long* seq, TTimeout timeOut, Boolean sync);
	NewtonErr			nAccept(TEndpoint* resfd, TOptionArray* opt, CBufferSegment* data, Long seq, TTimeout timeOut, Boolean sync);
	NewtonErr			nConnect(TOptionArray* opt, CBufferSegment* data, Long* seq, TTimeout timeOut, Boolean sync);
	NewtonErr			nRelease(TTimeout timeOut, Boolean sync);
	NewtonErr			nDisconnect(CBufferSegment* data, Long reason, Long seq, TTimeout timeOut, Boolean sync);
	NewtonErr			nUnBind(TTimeout timeOut, Boolean sync);
	NewtonErr			nOptMgmt(ULong arrayOpCode, TOptionArray* options, TTimeout timeOut, Boolean sync);
	NewtonErr			nSnd(UByte* buf, Size* count, ULong flags, TTimeout timeOut, Boolean sync, TOptionArray* opt);
	NewtonErr			nRcv(UByte* buf, Size* count, Size thresh, ULong* flags, TTimeout timeOut, Boolean sync, TOptionArray* opt);
	NewtonErr			nSnd(CBufferSegment* buf, ULong flags, TTimeout timeOut, Boolean sync, TOptionArray* opt);
	NewtonErr			nRcv(CBufferSegment* buf, Size thresh, ULong* flags, TTimeout timeOut, Boolean sync, TOptionArray* opt);
	NewtonErr			nAbort(Boolean sync);
	NewtonErr			Timeout(ULong refCon);
	Boolean				IsPending(ULong which);

	// the rest
	NewtonErr			SetState(Long state);
	TCommToolDisconnectPB*	PrepDisconnect(CBufferSegment* data, Long reason, Long seq, Boolean sync);
	NewtonErr			SendDisconnect(TCommToolDisconnectPB* pb, TTimeout timeOut, Boolean sync);
	void				HandlePutReply(TCommToolPutPB* pb);
	void				HandleGetReply(TCommToolGetPB* pb);
	void				HandleControlReply(TCommToolControlPB* pb);
	void				HandleEventReply(TCommToolEventPB* pb);
	void				HandleAbortReply(TCommToolAbortPB* pb);
	void				HandleOptMgmtReply(TCommToolOptMgmtPB* pb);
	void				HandleConnectReply(TCommToolConnectPB* pb);
	void				HandleListenReply(TCommToolConnectPB* pb);
	void				HandleAcceptReply(TCommToolConnectPB* pb);
	void				HandleReleaseReply(TCommToolControlPB* pb);
	void				HandleDisconnectReply(TCommToolDisconnectPB* pb);
	void				HandleBindReply(TCommToolBindPB* pb);
	void				HandleUnBindReply(TCommToolBindPB* pb);
	NewtonErr			SendBytes(TCommToolPutPB* pb, Size* count, ULong flags, TTimeout timeOut, Boolean sync, TOptionArray* opt);
	NewtonErr			RecvBytes(TCommToolGetPB* pb, Size* count, Size thresh, ULong* flags, TTimeout timeOut, Boolean sync, TOptionArray* opt);
	NewtonErr			PostEventRequest(TCommToolEventPB* pb);
	NewtonErr			PostKillRequest(ULong requestsToKill, Boolean async);
	NewtonErr			PrepareAbort(ULong requestsToKill, Boolean sync);
	NewtonErr			InitPending(void);
	void				NukePending(void);
	NewtonErr			InitPutPBList(void);
	TCommToolPutPB*		GrabPutPB(Boolean withSegment);
	void				ReleasePutPB(TCommToolPutPB* pb);
	void				NukePutPBList(void);
	NewtonErr			InitGetPBList(void);
	TCommToolGetPB*		GrabGetPB(Boolean withSegment);
	void				ReleaseGetPB(TCommToolGetPB* pb);
	void				NukeGetPBList(void);
	NewtonErr			KillKillKill(ULong requestsToKill, TUAsyncMessage* pb);
	NewtonErr			eWorldSnd(CBufferSegment* buf, ULong flags, TTimeout timeOut, Boolean sync, TOptionArray* opt);
	NewtonErr			eWorldSendBytes(TCommToolPutPB* pb, Size* count, ULong flags, TTimeout timeOut, Boolean sync, TOptionArray* opt);

	CList*				fPending;			// +0x24  the asynchronous calls in flight
	CList*				fPutPBs;			// +0x28  free put PBs
	CList*				fGetPBs;			// +0x2c  free get PBs
	TCommToolEventPB*	fEventPB;			// +0x30  the outstanding event request
	TCommToolDisconnectPB*	fDeferredDisconnect;	// +0x34  a disconnect waiting for an abort
	TPseudoSyncState*	fAbortSync;			// +0x38  what a synchronous abort waits on
	TPseudoSyncState*	fEventSync;			// +0x3c  what WaitForEvent waits on
	Boolean				fInSyncCall;		// +0x40
	Boolean				fWaitingForEvent;	// +0x41
	Boolean				fAborting;			// +0x42
	Boolean				fSyncAbort;			// +0x43
};

#endif	/* __COMMS_SERIALENDPOINT_H */
