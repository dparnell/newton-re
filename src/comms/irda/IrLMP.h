/*
	File:		comms/irda/IrLMP.h

	Contains:	IrLMP, the link management layer of the IrDA stack:
				TIrLMP, the multiplexer, and TLSAPConn, one connection
				between a service access point (LSAP) here and one there.

				TIrLMP sends each request on to the layer that handles it:
				a discovery to the link (TIrLAP), and if two of the devices
				found have the same address it asks them to pick new ones
				(the conflict list, AddrConflicts); a connection's and a
				transfer's to the link's connection side (TIrLAPConn); a
				put straight to the link.  It keeps the one-second ticker
				the LSAP connections time their connects by.

				TLSAPConn is one LSAP connection's state machine
				(disconnected, connect pending, connect, listen pending,
				listen, accept, data transfer ready): connecting sends the
				other LSAP an LM connect (opcode 1, answered 0x81) once the
				link is up and waits thirty seconds for the answer; data
				goes in frames with a two-byte LMPDU header (the two LSAPs);
				a disconnect is opcode 2 with a reason, which
				TranslateReasonToError makes an error.  Requests from above
				keep their block: its fPendingEvent says what it was.

				The field names are ours, their order the ROM's (offsets
				noted, for the ROM's 0x48 and 0x40 bytes).

	Reconstructed from the MP2x00 US ROM (0x000f5e84-0x000f6fe0); each
	function cites its origin.
*/

#ifndef __COMMS_IRLMP_H
#define __COMMS_IRLMP_H

#include "IrStream.h"

class TIrLAP;
class TIrLAPConn;
class CBufferSegment;

// the LMPDU header of a frame received (TIrLAPConn::ExtractHeader)
struct TLMPDUHeader
{
	UByte				fDstLSAPId;				// +0x00
	UByte				fSrcLSAPId;				// +0x01
	UByte				fOpCode;				// +0x02  a control frame's (0 in a data frame)
	UByte				fInfo;					// +0x03  its parameter
	UByte				fMore[4];				// +0x04  the rest an access mode frame carries
};

// the multiplexer's states
enum
{
	kIrLMPReady = 0,
	kIrLMPDiscover,
	kIrLMPResolveAddress
};


class TIrLMP : public TIrStream
{
public:
						TIrLMP();
	virtual				~TIrLMP();
	virtual void		NextState(ULong event);

	NewtonErr			Init(TIrGlue* glue, TIrLAP* lap);
	void				Reset(void);
	void				DeInit(void);
	void				Demultiplexor(CBufferSegment* buffer);
	ULong				FillInLMPDUHeader(TIrDataXferEvent* event, UByte* buffer);
	void				StartOneSecTicker(void);
	void				StopOneSecTicker(void);
	void				TimerComplete(ULong kind);
	void				HandleReadyStateEvent(ULong event);
	void				HandleDiscoverStateEvent(ULong event);
	void				HandleResolveAddressStateEvent(ULong event);
	Boolean				AddrConflicts(CList* devices, Boolean addToConflicts);

	TIrGlue*			fIrGlue;				// +0x14  (the stream's fGlue too)
	TIrLAPConn*			fLAPConn;				// +0x18
	TIrLAP*				fLAP;					// +0x1c
	UByte				fState;					// +0x20
	UByte				fTickerUsers;			// +0x21
	ULong				fNumConflicts;			// +0x24
	ULong				fConflicts[8];			// +0x28  device addresses found twice
};


// an LSAP connection's states
enum
{
	kLSAPConnDisconnected = 0,
	kLSAPConnConnectPending,
	kLSAPConnConnect,
	kLSAPConnListenPending,
	kLSAPConnListen,
	kLSAPConnAccept,
	kLSAPConnDataTransferReady
};


class TLSAPConn : public TIrStream
{
public:
						TLSAPConn();
	virtual				~TLSAPConn();
	virtual void		NextState(ULong event);

	NewtonErr			Init(TIrGlue* glue, TIrLMP* lmp, TIrStream* client);
	void				DeInit(void);
	void				AssignId(ULong lsapId);
	Boolean				YourData(TLMPDUHeader& header, UByte connecting);
	TIrLSAPConnEvent*	GetPendConnLstn(void);
	void				OneSecTickerComplete(void);

	void				HandleDisconnectedStateEvent(ULong event);
	void				HandleConnectPendingStateEvent(ULong event);
	void				HandleConnectStateEvent(ULong event);
	void				HandleListenPendingStateEvent(ULong event);
	void				HandleListenStateEvent(ULong event);
	void				HandleAcceptStateEvent(ULong event);
	void				HandleDataTransferReadyStateEvent(ULong event);

	void				SaveCurrentRequest(void);
	Boolean				InternalDisconnectRequest(void);
	Boolean				InternalPutRequest(void);
	void				PassRequestToLMP(void);
	void				DisconnectStart(NewtonErr reason, TIrLSAPConnEvent* event);
	void				GetControlFrame(void);
	void				PutControlFrame(UByte opCode, UByte info);
	void				GetDataFrame(UByte restore);
	void				PutDataFrame(void);
	void				ConnLstnComplete(NewtonErr result);
	void				StartConnectTimer(void);
	void				StopConnectTimer(void);
	NewtonErr			TranslateReasonToError(UByte reason);

	UByte				fState;					// +0x14
	Boolean				fConnecting;			// +0x15  this side asked for the connection
	TIrGlue*			fIrGlue;				// +0x18  (the stream's fGlue too)
	TIrLMP*				fLMP;					// +0x1c
	TIrStream*			fClient;				// +0x20  who the answers go to
	NewtonErr			fDisconnectReason;		// +0x24
	TIrLSAPConnEvent*	fPendConnLstn;			// +0x28  the connect or listen in hand
	CBuffer*			fConnectData;			// +0x2c  the user data a connect carries
	CBuffer*			fGetBuffer;				// +0x30  a get put aside while a control frame is read
	ULong				fGetOffset;				// +0x34
	ULong				fGetLength;				// +0x38
	UByte				fMyLSAPId;				// +0x3c  0xff none
	UByte				fPeerLSAPId;			// +0x3d  0xff any
	Boolean				fTimerRunning;			// +0x3e
	UByte				fTicks;					// +0x3f
};

#endif	/* __COMMS_IRLMP_H */
