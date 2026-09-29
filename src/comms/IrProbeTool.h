/*
	File:		comms/IrProbeTool.h

	Contains:	The IR probe ("IrProbe", serv 'pkir'): what a 2.1 MessagePad
				asks first when it beams, to learn whether the other side
				speaks IrDA or only Sharp IR (docs/comms/README.md,
				"Beaming - the plan").  The beamer opens it, connects (or
				listens and accepts), reads the answer from its 'irpt'
				option (protocol 8, irUsingIrDA; 7, the Newton's Sharp
				protocols; 1, an old Sharp device) and closes it again.

				Connecting, it alternates the IR port between the two
				modulations: four IrDA TEST frames (addressed to all,
				carrying 'prbe' and a parameter, sent through TIrSIR) with
				a tenth of a second for the echo each, then a Sharp
				negotiation offer (protocols 0xf: IrDA among them) or
				every third time an ENQ, a tenth of a second for the
				answer - and round again, for about two minutes.  An echoed
				TEST frame is IrDA; a Sharp answer or SYN is Sharp IR.
				Listening, it waits in auto-receive (hearing either kind:
				the port's status says which the bytes came as) for half a
				second at a time: a TEST frame is echoed and answers IrDA,
				two ENQs or an offer without IrDA answer Sharp IR.

				The field names are ours, their order the ROM's (offsets
				noted, for the ROM's layout of 0x5bc bytes).  IRProbeService
				is the comm manager service that starts one.

	Reconstructed from the MP2x00 US ROM (TIrProbeTool 0x000f6fe0-0x000f7e74,
	IRProbeService 0x000e89d8-0x000e8a60); each function cites its origin.
*/

#ifndef __COMMS_IRPROBETOOL_H
#define __COMMS_IRPROBETOOL_H

#ifndef __COMMS_SHARPIRTOOL_H
#include "SharpIRTool.h"
#endif

#include "IrSIR.h"


class TIrProbeTool : public TAsyncSerTool
{
public:
						TIrProbeTool(ULong serviceId);
	virtual				~TIrProbeTool();

	virtual ULong		GetSizeOf();

protected:
	virtual NewtonErr	TaskConstructor();
	virtual void		TaskDestructor();
	virtual UChar*		GetToolName();
	virtual void		HandleRequest(TUMsgToken& msgToken, ULong msgType);
	virtual NewtonErr	OpenStart(TOptionArray* options);
	virtual void		ConnectStart();
	virtual void		ListenStart();
	virtual ULong		ProcessOptionStart(TOption* theOption, ULong label, ULong opcode);
	virtual NewtonErr	AddDefaultOptions(TOptionArray* options);
	virtual NewtonErr	AddCurrentOptions(TOptionArray* options);
	virtual void		TerminateConnection();
	virtual void		TerminateComplete();
	virtual NewtonErr	AllocateBuffers();
	virtual NewtonErr	SetSerialChipSelect(TCMOSerialHardware* opt);
	virtual void		DoOutput();
	virtual void		DoInput();

	// the non-virtuals
	void				StartTimer(ULong delay, ULong kind);
	void				StopTimer();
	void				TimerComplete(ULong kind);
	void				StartTransmit();
	void				StopTransmit();
	void				StartReceive();
	void				StopReceive();
	void				OutputComplete();
	void				InputComplete();
	void				NextState(ULong event);
	Boolean				RecdIrDATestFrame(ULong parameter, UByte address);
	void				SendIrDATestFrame(ULong parameter, UByte address);
	ULong				SharpFillOutputBuffer();
	ULong				SharpEmptyInputBuffer();
	void				SwitchIrLink(ULong link);

	TCMOSlowIRProtocolType	fProtocolType;		// +0x4b0  the answer (protocol +0x4bc, options +0x4c0)
	ULong				fPhase;					// +0x4c4  0 listening, 1 probing IrDA, 2 probing Sharp
	ULong				fLink;					// +0x4c8  the port's mode: 1 IrDA, 2 Sharp
	ULong				fRxKind;				// +0x4cc  what is coming in: 0 not known yet, 1 IrDA, 2 Sharp
	ULong				fTicks;					// +0x4d0  the timers run out so far
	UByte				fSharpSent;				// +0x4d4  the last Sharp packet sent: 5 ENQ, 0x85 an offer
	TUAsyncMessage		fTimerMsg;				// +0x4d8
	TObjectId			fTimerMsgId;			// +0x4e8
	TSharpIRTimerEvent	fTimerEvent;			// +0x4ec  (fKind +0x4f8)
	UByte				fPacket[0x40];			// +0x4fc  a Sharp packet (five bytes of lead-in first) or an IrDA frame
	UByte*				fBuffer;				// +0x53c  fPacket
	ULong				fSharpRxState;			// +0x540  0 idle, 1 lead-in, 2 the packet
	ULong				fSharpRxIndex;			// +0x544
	ULong				fSharpRxLength;			// +0x548
	ULong				fSharpTxLength;			// +0x54c
	ULong				fSharpTxIndex;			// +0x550
	ULong				fIrDATries;				// +0x554
	ULong				fSharpTries;			// +0x558
	ULong				fIrDATryLimit;			// +0x55c
	ULong				fSharpTryLimit;			// +0x560
	ULong				fOffers;				// +0x564  offers since the last ENQ
	ULong				fENQs;					// +0x568  Sharp ENQs heard, listening
	TIrSIR*				fSIR;					// +0x56c
	ULong				fTestParameter;			// +0x570  'prbe'
	CBufferSegment		fRxSegment;				// +0x574  an IrDA frame's data, over fPacket
	TIrLAPPutBuffer		fFrame;					// +0x59c  an IrDA frame to send, over fPacket
};


PROTOCOL IRProbeService : public TCMService
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(IRProbeService);
	IRProbeService*		New();
	void				Delete();
	NewtonErr			Start(TOptionArray* options, ULong serviceId, TServiceInfo* serviceInfo);
	NewtonErr			DoneStarting(TAEvent* event, ULong size, TServiceInfo* serviceInfo);
};

#endif	/* __COMMS_IRPROBETOOL_H */
