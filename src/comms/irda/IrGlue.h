/*
	File:		comms/irda/IrGlue.h

	Contains:	TIrGlue, the top of the IrDA stack: what the IrDA tool
				(IrDATool.h) asks for - discover, look an LSAP up in the
				other side's IAS database, connect, listen, accept, get,
				put, cancel, disconnect - made into requests for the
				layers below, and their answers handed back to the tool.
				It owns the layers (the link, the multiplexer, the LSAP
				connection, the IAS client and server and database), the
				event blocks and the run queue the layers' state machines
				go through (NextStateMachine, HandleInternalEvent), and it
				passes the link's needs on to the tool (the serial port,
				the timers).

				A get fills the client's buffer straight from the frames
				when it has room for a whole one, and otherwise goes
				through a receive buffer of the data size, so that a frame
				is never cut; a put is cut into frames of the other side's
				data size less the LMPDU header, a window of them at a
				time.

				The field names are ours, their order the ROM's (offsets
				noted, for the ROM's 0xb8 bytes).

	Reconstructed from the MP2x00 US ROM (0x000ef538-0x000f0cc8); each
	function cites its origin.
*/

#ifndef __COMMS_IRGLUE_H
#define __COMMS_IRGLUE_H

#include "IrStream.h"
#include "IrDscInfo.h"
#include "IrQOS.h"

class TIrDATool;
class TIrLMP;
class TIrLAP;
class TIASService;
class TIASServer;
class TIASClient;
class TIrLAPPutBuffer;
class CBufferSegment;
class TCMOSlowIRStats;

// the glue's states
enum
{
	kIrGlueDisconnected = 0,
	kIrGlueDiscovering,
	kIrGlueNameServerLookup,
	kIrGlueConnecting,
	kIrGlueListening,
	kIrGlueAccepting,
	kIrGlueConnected
};


class TIrGlue : public TIrStream
{
public:
						TIrGlue();
	virtual				~TIrGlue();
	virtual void		NextState(ULong event);

	NewtonErr			Init(TIrDATool* tool);
	void				DeInit(Boolean all);

	// the tool's requests
	void				DiscoverStart(ULong numSlots, Boolean mediaBusyCheck);
	void				LSAPLookupStart(ULong devAddr, UByte* className, UByte* attrName);
	void				ConnectStart(ULong devAddr, ULong lsapId, CBuffer* data);
	void				ListenStart(CBuffer* data);
	void				AcceptStart(CBuffer* data);
	void				GetStart(CBuffer* buffer, ULong threshold);
	void				PutStart(CBuffer* buffer);
	void				CancelGetStart(void);
	void				CancelPutStart(void);
	void				DisconnectStart(NewtonErr reason);
	NewtonErr			RegisterMyNameAndLSAPId(UByte* className, UByte* attrName, ULong lsapId);

	// their answers
	void				HandleDiscoverComplete(void);
	void				HandleNameServerConnectComplete(void);
	void				HandleNameServerLookupComplete(void);
	void				HandleNameServerReleaseComplete(void);
	void				HandleConnectComplete(void);
	void				HandleListenComplete(void);
	void				HandleAcceptComplete(void);
	void				HandleGetComplete(void);
	void				HandlePutComplete(void);
	void				HandleCancelGetComplete(void);
	void				HandleCancelPutComplete(void);
	void				HandleDisconnectComplete(void);
	void				DiscoverComplete(NewtonErr result, CList* devices);
	void				LSAPLookupComplete(NewtonErr result, ULong lsapId);
	void				ConnectComplete(NewtonErr result);
	void				ListenComplete(NewtonErr result);
	void				AcceptComplete(NewtonErr result);
	void				GetComplete(NewtonErr result);
	void				PutComplete(NewtonErr result);
	void				CancelGetComplete(NewtonErr result);
	void				CancelPutComplete(NewtonErr result);
	void				DisconnectComplete(void);

	// the states
	void				HandleDisconnectedStateEvent(ULong event);
	void				HandleDiscoveringStateEvent(ULong event);
	void				HandleNameServerLookupStateEvent(ULong event);
	void				HandleConnectingStateEvent(ULong event);
	void				HandleListeningStateEvent(ULong event);
	void				HandleAcceptingStateEvent(ULong event);
	void				HandleConnectedStateEvent(ULong event);

	// gets and puts
	NewtonErr			InitBuffers(void);
	void				ResetRecvBufferState(void);
	void				InitGetRequest(TIrDataXferEvent* event);
	Boolean				CheckGetDone(ULong bytes, Boolean fromRecvBuffer);
	NewtonErr			InitPutRequests(CBuffer* buffer, ULong offset, ULong size);
	void				DeleteDiscoveredDevicesList(Boolean all);
	NewtonErr			InitNameService(void);

	// the run queue and the event blocks
	void				NextStateMachine(TIrStream* stream);
	void				HandleInternalEvent(void);
	NewtonErr			InitEventBlockList(void);
	void				DeleteEventBlockList(Boolean all);
	TIrEvent*			GrabEventBlock(ULong event, ULong size);
	void				ReleaseEventBlock(TIrEvent* block);
	NewtonErr			ObtainLSAPId(ULong& lsapId);
	void				ReleaseLSAPId(UByte lsapId);

	// the link's needs, passed to the tool
	void				PostAsyncEvent(ULong data);
	void				StartTerminate(NewtonErr reason);
	void				StartTimer1(ULong delay, int kind);
	void				StopTimer1(void);
	void				StartTimer2(ULong delay, int kind);
	void				StopTimer2(void);
	void				StartTransmit(TIrLAPPutBuffer* frame, ULong extraBOFs);
	void				StopTransmit(void);
	void				StartReceive(CBufferSegment* buffer, UByte address, UByte keepLong);
	void				StopReceive(void);
	Boolean				MediaBusy(void);
	Boolean				ReceivingInput(void);
	void				SetMediaBusy(UByte busy);
	void				ChangeSpeed(ULong bitsPerSec);

	// and the tool's news, passed to the link
	void				TimerComplete(ULong kind);
	void				OutputComplete(void);
	void				InputComplete(UByte address, UByte control);
	Boolean				ConnectedAsPrimary(void);
	void				CopyStatsTo(TCMOSlowIRStats* stats);
	void				ResetStats(void);

	TIrDATool*			fTool;					// +0x14
	TIrLMP*				fLMP;					// +0x18
	TIrLAP*				fLAP;					// +0x1c
	TIASService*		fNameService;			// +0x20  this station's IAS database
	TIASServer*			fNameServer;			// +0x24
	TIASClient*			fNameClient;			// +0x28
	ULong				fMyLSAPId;				// +0x2c
	TLSAPConn*			fLSAPConn;				// +0x30
	UByte				fState;					// +0x34
	UByte				fDisconnectState;		// +0x35  0 none, 1 asked, then 2-6 the layers told in turn
	UByte				fPeerWindowSize;		// +0x36
	ULong				fPeerLSAPId;			// +0x38  a lookup's answer
	ULong				fMaxPutSize;			// +0x3c
	ULong				fLSAPIdsInUse;			// +0x40  a bit each (0, the IAS's, always)
	NewtonErr			fLookupResult;			// +0x44
	NewtonErr			fDisconnectReason;		// +0x48
	TIrDscInfo			fDscInfo;				// +0x4c  this station's discovery information
	CList*				fDiscoveredDevices;		// +0x6c  TIrDscInfos
	TIrQOS				fMyQOS;					// +0x70
	TIrQOS				fPeerQOS;				// +0x78
	CBufferSegment*		fRecvBuffer;			// +0x80
	ULong				fRecvBufferSize;		// +0x84
	ULong				fGetThreshold;			// +0x88
	Boolean				fGetDirect;				// +0x8c  frames go straight into the client's buffer
	CBuffer*			fGetBuffer;				// +0x90  the client's
	UByte*				fLookupClassName;		// +0x94
	UByte*				fLookupAttrName;		// +0x98
	ULong				fGetBytes;				// +0x9c  got so far
	ULong				fPutBytesSent;			// +0xa0
	UByte				fPutsPending;			// +0xa4
	NewtonErr			fPutResult;				// +0xa8
	TIrStream*			fCurrentStream;			// +0xac  the stream the run queue runs next
	CList*				fPendingStreams;		// +0xb0  those after it (the next at the end)
	CList*				fFreeEventBlocks;		// +0xb4
};

#endif	/* __COMMS_IRGLUE_H */
