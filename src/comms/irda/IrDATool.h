/*
	File:		comms/irda/IrDATool.h

	Contains:	The IrDA tool ("IrDA", serv 'irda'): a comm tool whose
				connection is an IrDA link - the stack's glue (TIrGlue)
				over the built-in IR port, framed by TIrSIR.

				Connecting it discovers (for about two minutes, a slot
				count's worth of discoveries at a time) until a device with
				the service hints asked for answers, registers this side's
				class and LSAP in its IAS database, looks the other side's
				class up in the other's (unless the LSAP is given) and
				connects to it; listening it registers and waits (two
				minutes at most) for the other side to connect, which it
				then accepts.  Gets and puts go through the glue; the
				options are the IrDA ones (discovery, receive buffers, the
				link disconnect time, the connection's class names and
				LSAPs, its user data and the LSAP attribute's name), the
				speed and the protocol type ('irpt': 4) and the link's
				statistics.

				The field names are ours, their order the ROM's (offsets
				noted, for the ROM's layout of 0x690 bytes).  TIrDAService
				is the comm manager service that starts one.

	Reconstructed from the MP2x00 US ROM (TIrDATool 0x000edef4-0x000ef35c,
	TIrDAService 0x000edde0-0x000edef4); each function cites its origin.
*/

#ifndef __COMMS_IRDATOOL_H
#define __COMMS_IRDATOOL_H

#ifndef __COMMS_SHARPIRTOOL_H
#include "SharpIRTool.h"
#endif

#include "IrSIR.h"

class TIrGlue;


class TIrDATool : public TAsyncSerTool
{
public:
						TIrDATool(ULong serviceId);
	virtual				~TIrDATool();

	virtual ULong		GetSizeOf();

	// the glue's news
	void				DoDiscoverComplete(NewtonErr result, CList* devices);
	void				DoLSAPLookupComplete(NewtonErr result, ULong lsapId);
	void				DoConnectComplete(NewtonErr result);
	void				DoListenComplete(NewtonErr result);
	void				DoAcceptComplete(NewtonErr result);
	void				DoPutDataComplete(NewtonErr result, ULong bytes);
	void				DoGetDataComplete(NewtonErr result, ULong bytes);
	void				DoCancelPutComplete(NewtonErr result);
	void				DoCancelGetComplete(NewtonErr result);
	void				StartTerminate(NewtonErr reason);
	void				PostAsyncEvent(ULong data);

	// and its needs
	void				StartTimer1(ULong delay, int kind);
	void				StopTimer1(void);
	void				StartTimer2(ULong delay, int kind);
	void				StopTimer2(void);
	InterfaceSpeed		ChangeSpeed(ULong bitsPerSec);
	void				StartTransmit(TIrLAPPutBuffer* frame, ULong extraBOFs);
	void				StopTransmit(void);
	void				StartReceive(CBufferSegment* buffer, UByte address, UByte keepLong);
	void				StopReceive(void);
	Boolean				MediaBusy(void);
	Boolean				ReceivingInput(void);
	void				SetMediaBusy(UByte busy);

protected:
	virtual NewtonErr	TaskConstructor();
	virtual void		TaskDestructor();
	virtual UChar*		GetToolName();
	virtual void		HandleInternalEvent();
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
	virtual void		TerminateConnection();
	virtual void		TerminateComplete();
	virtual NewtonErr	AllocateBuffers();
	virtual NewtonErr	SetSerialChipSelect(TCMOSerialHardware* opt);
	virtual void		StartOutput(CBufferList* clientBuffer);
	virtual void		DoOutput();
	virtual void		StartInput(CBufferList* clientBuffer);
	virtual void		DoInput();
	virtual void		TxDataSent();
	virtual void		RxDataAvailable();

	friend class TIrGlue;				// (it ends the connection with TerminateComplete)

	void				StartConnect(ULong lsapId);
	void				UpdateOptionsAfterConnectOrListen(void);

public:
	TIrGlue*			fGlue;					// +0x4b0
	TIrSIR*				fSIR;					// +0x4b4
	TCMOSlowIRStats		fStats;					// +0x4b8
	TCMOSlowIRConnect	fConnect;				// +0x4e0  (connectOptions +0x4ec: 2 when connected as primary)
	TCMOSlowIRProtocolType	fProtocolType;		// +0x4f0
	TCMOIrDADiscovery	fDiscovery;				// +0x504  (fPeerDevAddr +0x51c)
	TCMOIrDAReceiveBuffers	fReceiveBuffers;	// +0x524
	TCMOIrDALinkDisconnect	fLinkDisconnect;	// +0x538
	TCMOIrDAConnectionInfo	fConnectionInfo;	// +0x548
	TCMOIrDAConnectAttrName	fConnectAttrName;	// +0x5a4
	TCMOIrDAConnectUserData	fConnectUserData;	// +0x5f0
	Boolean				fSecondary;				// +0x63c  connected as the link's secondary
	CBufferSegment*		fUserData;				// +0x640  over fConnectUserData.fData
	TUAsyncMessage		fTimer1Msg;				// +0x644  the link's timers
	TObjectId			fTimer1MsgId;			// +0x654
	TSharpIRTimerEvent	fTimer1Event;			// +0x658  (fKind +0x664: 0 when not running)
	TUAsyncMessage		fTimer2Msg;				// +0x668  the ticker's and a listen's
	TObjectId			fTimer2MsgId;			// +0x678
	TSharpIRTimerEvent	fTimer2Event;			// +0x67c  (fKind +0x688)
	ULong				fDiscoveriesLeft;		// +0x68c
};


PROTOCOL TIrDAService : public TCMService
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TIrDAService);
	TIrDAService*		New();
	void				Delete();
	NewtonErr			Start(TOptionArray* options, ULong serviceId, TServiceInfo* serviceInfo);
	NewtonErr			DoneStarting(TAEvent* event, ULong size, TServiceInfo* serviceInfo);
};

#endif	/* __COMMS_IRDATOOL_H */
