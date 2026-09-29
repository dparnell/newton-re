/*
	File:		comms/SharpIRTool.h

	Contains:	Sharp IR ("SlowIR", serv 'slir'): the beaming protocol of the
				Newton 1.x and 2.0, and the one a 2.1 MessagePad falls back
				on for an older Newton (docs/comms/README.md, "Beaming - the
				plan").  TSharpIRTool is an async serial tool on the built-in
				IR ('infr', its SCC service 2) with the IR port in Sharp's
				ASK mode, 9600 bps, 8 bits, odd parity; it replaces the
				byte stream with packets:

				  lead-in	so many bytes of 0 (or 0xff) before a packet
				  			(5, 10 or 40 by the speed);
				  control	[type] 0x82 c - c ENQ (5), ACK (6), NAK (0x15),
				  			SYN (0x16) or CAN (0x18);
				  negotiate	0x90 0x85|0x86 protocols speeds - the offer and
				  			the answer (the protocols a side can speak,
				  			irUsingSharpIR/NewtIR/SeniorIR, and the speeds);
				  data		[type] 0x81 0x10 seq(2) 0x01 0x40 0xfe len(2)
				  			data checksum(2) - up to 0x200 bytes, the words
				  			little-endian, the checksum the data's bytes
				  			summed; the last packet of a put numbered 0xffff.

				The type byte is the protocol's (0x96 Sharp, 0x9b the
				Newton's own; a negotiation's 0x90).  A connection is made
				by one side offering (0x85) and the other answering (0x86)
				or, symmetric, both offering; the protocol and speed agreed
				are then used for the data.  A data packet is sent after an
				ENQ answered by SYN, and acknowledged by ACK or refused by
				NAK (three tries); a get answers ENQ with SYN and each good
				packet with ACK.  The timers are two delayed messages to the
				tool's own port, each carrying what its expiry means.

				The state machine (NextState) is the ROM's, events and
				states numbered as it numbers them; comments name them.
				The field names are ours, their order the ROM's (offsets
				noted, for the ROM's layout of 0x790 bytes).

				TIRService is the comm manager service that starts one.

	Reconstructed from the MP2x00 US ROM (TSharpIRTool 0x001e07b4-0x001e2bbc,
	TIRService 0x000e8f20-0x000e9034); each function cites its origin.
*/

#ifndef __COMMS_SHARPIRTOOL_H
#define __COMMS_SHARPIRTOOL_H

#ifndef __COMMS_SERIALTOOL_H
#include "SerialTool.h"
#endif

#ifndef __COMMMANAGERINTERFACE_H
#include "CommManagerInterface.h"
#endif


// a timer's message: what its expiry means (HandleRequest)
struct TSharpIRTimerEvent : public TAEvent
{
	ULong				fField8;				// +0x08
	ULong				fKind;					// +0x0c  1 control, 2 data, 3 negotiate timed out; 4, 5 events 0xd, 0x1c
};


class TSharpIRTool : public TAsyncSerTool
{
public:
						TSharpIRTool(ULong serviceId);
	virtual				~TSharpIRTool();

	virtual ULong		GetSizeOf();

protected:
	virtual NewtonErr	TaskConstructor();
	virtual void		TaskDestructor();
	virtual UChar*		GetToolName();
	virtual void		HandleRequest(TUMsgToken& msgToken, ULong msgType);
	virtual NewtonErr	OpenStart(TOptionArray* options);
	virtual void		ConnectStart();
	virtual void		ListenStart();
	virtual void		AcceptStart();
	virtual ULong		ProcessOptionStart(TOption* theOption, ULong label, ULong opcode);
	virtual NewtonErr	AddDefaultOptions(TOptionArray* options);
	virtual NewtonErr	AddCurrentOptions(TOptionArray* options);
	virtual void		KillPut();
	virtual void		KillGet();
	virtual void		TerminateComplete();
	virtual void		SetChannelFilter(CommToolRequestType request, Boolean enable);
	virtual NewtonErr	AllocateBuffers();
	virtual void		SetIOParms(TCMOSerialIOParms* opt);
	virtual NewtonErr	SetSerialChipSelect(TCMOSerialHardware* opt);
	virtual void		StartOutput(CBufferList* clientBuffer);
	virtual void		DoOutput();
	virtual void		StartInput(CBufferList* clientBuffer);
	virtual void		DoInput();
	virtual ULong		FillOutputBuffer();
	virtual ULong		EmptyInputBuffer(ULong* markerValue);

	// TSharpIRTool's own (+0x1b4): an abort resets the state machine first
	virtual NewtonErr	StartAbort(NewtonErr abortError);

	// the non-virtuals
	void				StartTimer1(ULong delay, ULong kind);
	void				StopTimer1();
	void				StartTimer2(ULong delay, ULong kind);
	void				StopTimer2();
	void				StartTransmit(Long packetKind);
	void				StopTransmit();
	void				PrepLeadIn();
	void				PrepDataPacket();
	void				PrepControlPacket(UByte control);
	void				PrepNegotiatePacket(UByte what, UByte protocols, UByte speeds);
	void				StartReceive(Long packetKind);
	void				StopReceive();
	NewtonErr			ReceiveLeadIn();
	NewtonErr			ReceiveControl(UByte expected);
	void				HandleControl(NewtonErr result);
	NewtonErr			ReceiveData();
	Boolean				CheckReceiveDone();
	void				HandleData(NewtonErr result);
	NewtonErr			ReceiveNegotiate(UByte expected);
	void				HandleNegotiate(NewtonErr result);
	void				NextState(ULong event);
	void				AbortReceive(NewtonErr error);
	void				AbortSend(NewtonErr error);
	void				DoListenComplete(NewtonErr result);
	void				DoConnectComplete(NewtonErr result);
	InterfaceSpeed		SelectSpeed(Boolean reset);
	InterfaceSpeed		SelectProtocol(Boolean reset);
	void				ResetStateMachine();

	UByte				fPacket[0x210];			// +0x4b0  the packet in hand (a data packet's from +4)
	ULong				fLeadInCount;			// +0x6c0  lead-in bytes still to send
	ULong				fSequence;				// +0x6c4  the next data packet's number (0xffff: the last was)
	ULong				fRxSequence;			// +0x6c8  the number of the one received
	ULong				fDataLength;			// +0x6cc
	ULong				fPacketLength;			// +0x6d0
	ULong				fPacketIndex;			// +0x6d4
	UByte				fLeadInByte;			// +0x6d8
	TUAsyncMessage		fTimer1Msg;				// +0x6dc
	TObjectId			fTimer1MsgId;			// +0x6ec
	TSharpIRTimerEvent	fTimer1Event;			// +0x6f0  (fKind +0x6fc: 0 when not running)
	TUAsyncMessage		fTimer2Msg;				// +0x700
	TObjectId			fTimer2MsgId;			// +0x710
	TSharpIRTimerEvent	fTimer2Event;			// +0x714  (fKind +0x720)
	Long				fRetries;				// +0x724  a data packet's tries left
	ULong				fState;					// +0x728
	ULong				fRxState;				// +0x72c  how far a packet has been read (1-3 lead-in, 4 its type, 5-9 its body)
	ULong				fSavedState;			// +0x730  the state to go back to after an ENQ
	TCMOSlowIRProtocolType	fProtocolType;		// +0x734  (protocol +0x740, options +0x744)
	TCMOSlowIRStats		fStats;					// +0x748
	TCMOSlowIRConnect	fConnect;				// +0x770  (connectOptions +0x77c)
	Long				fPacketKind;			// +0x780  -1 control, 0 data, else negotiate
	Boolean				fResend;				// +0x784  the data packet is sent again as it is
	Boolean				fOfferCrossed;			// +0x785  symmetric: the other side offered too
	UByte				fProtocolsOffered;		// +0x786  (7: Sharp, the Newton's, Senior)
	ULong				fLastRxSequence;		// +0x788
	ULong				fLastRxChecksum;		// +0x78c
};


PROTOCOL TIRService : public TCMService
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TIRService);
	TIRService*			New();
	void				Delete();
	NewtonErr			Start(TOptionArray* options, ULong serviceId, TServiceInfo* serviceInfo);
	NewtonErr			DoneStarting(TAEvent* event, ULong size, TServiceInfo* serviceInfo);
};

// the IR services put in the protocol registry (the kernel services task's
// job, the registry being a monitor)
void	RegisterIRCommServices(void);

#endif	/* __COMMS_SHARPIRTOOL_H */
