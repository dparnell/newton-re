/*
	File:		comms/SniffIRTool.h

	Contains:	The IR sniffer ("SniffIR", serv 'snif'): what listens on the
				IR port for another machine beaming at this one when the
				In/Out Box's "Receive beams automatically" is on
				(userConfiguration.zapAutoReceive).  StartIRSniffing opens an
				endpoint on it (Beamer.cpp's SendSniffCommand); the tool
				claims the port passively, so any other tool that wants it
				takes it away, and listens in auto-receive (either kind of
				IR).  When the bytes that came look like the start of a beam
				- an IrDA frame (BOF 0xc0, the broadcast address 0xff, an XID
				or TEST command) or a Sharp IR packet (a lead-in of 0x96,
				0x9b or 0x90, then 0x85, or 0x82 and an ENQ) - it lets go of
				the chip, tells the newt world ('irMC', which runs the root's
				IRConnectRequest: the Beam transport's ReceiveRequest) and
				tries again two seconds later.  The Beam receive's own
				endpoint closes the sniffer meanwhile (StopIRSniffing in its
				Initialize) and opens it again when it is done (TearDown).

				The field names are ours, their order the ROM's (offsets
				noted, for the ROM's layout of 0x4c8 bytes).  IRSniffService
				is the comm manager service that starts one.

	Reconstructed from the MP2x00 US ROM (TSniffIRTool 0x001e2c5c-0x001e34c0,
	IRSniffService 0x000e9034-0x000e90bc); each function cites its origin.
*/

#ifndef __COMMS_SNIFFIRTOOL_H
#define __COMMS_SNIFFIRTOOL_H

#ifndef __COMMS_SERIALTOOL_H
#include "SerialTool.h"
#endif

#ifndef __COMMMANAGERINTERFACE_H
#include "CommManagerInterface.h"
#endif

#include "SerialOptions.h"


class TSniffIRTool : public TAsyncSerTool
{
public:
						TSniffIRTool(ULong serviceId);
	virtual				~TSniffIRTool();

	virtual ULong		GetSizeOf();

	// the events the sniffer's states move on (NextState)
	enum IRSniffEvent
	{
		kSniffNothing = 0,
		kSniffMayStart,			// powered, connected, enabled: start sniffing
		kSniffStop,				// powered off, terminated or the port given up
		kSniffStart,
		kSniffRetry,
		kSniffWakeUp,			// the two seconds after a failed start or a find are up
		kSniffInput				// bytes came
	};

protected:
	virtual NewtonErr	TaskConstructor();
	virtual void		TaskDestructor();
	virtual UChar*		GetToolName();
	virtual void		ConnectStart();
	virtual void		ListenStart();
	virtual ULong		ProcessOptionStart(TOption* theOption, ULong label, ULong opcode);
	virtual NewtonErr	AddDefaultOptions(TOptionArray* options);
	virtual NewtonErr	AddCurrentOptions(TOptionArray* options);
	virtual void		ResArbReleaseStart(UChar* resName, UChar* resType);
	virtual void		ResArbClaimNotification(UChar* resName, UChar* resType);
	virtual void		TerminateComplete();
	virtual void		PowerOnEvent(ULong reason);
	virtual void		PowerOffEvent(ULong reason);
	virtual NewtonErr	AllocateBuffers();
	virtual void		SetIOParms(TCMOSerialIOParms* opt);
	virtual void		DoOutput();
	virtual void		DoInput();
	virtual void		WakeUpHandler();
	virtual void		TxDataSent();
	virtual void		RxDataAvailable();

	// the non-virtuals
	void				StartReceive();
	void				StopReceive();
	Boolean				CheckBufferForValidInput();
	Boolean				CheckBufferForIrDAData();
	Boolean				CheckBufferForSharpData();
	void				NextState(ULong event);
	NewtonErr			SniffStart();
	void				SniffStop();
	void				NotifyUser();

	Boolean				fPowered;				// +0x4b0
	Boolean				fConnected;				// +0x4b1  connected or listening
	Boolean				fIrDADetected;			// +0x4b2  the port's status when the bytes came: IrDA (else Sharp IR)
	ULong				fState;					// +0x4b4  0 idle, 1 starting, 2 sniffing, 3 waiting to try again
	TCMOSlowIRSniff		fSniff;					// +0x4b8  ('irsn: sniffEnable +0x4c4)
};


PROTOCOL IRSniffService : public TCMService
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(IRSniffService);
	IRSniffService*		New();
	void				Delete();
	NewtonErr			Start(TOptionArray* options, ULong serviceId, TServiceInfo* serviceInfo);
	NewtonErr			DoneStarting(TAEvent* event, ULong size, TServiceInfo* serviceInfo);
};

#endif	/* __COMMS_SNIFFIRTOOL_H */
