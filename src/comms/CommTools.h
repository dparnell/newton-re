/*
	File:		comms/CommTools.h

	Contains:	TCommTool, the base class of every communications tool: a
				task of its own (a TUTaskWorld) that takes requests on a port
				and answers them, for an endpoint in another task.

				The requests are the DDK's CommTool.h classes, each sent with
				a message type that is its channel's bit: get, put, control,
				get-event, kill, status and resource arbitration.  TaskMain
				receives one at a time, keeping each channel's message token
				in its TCommToolMsgContainer and closing that channel's filter
				until the request is answered (CompleteRequest), so a tool has
				at most one request of each kind in hand.  A control request's
				op code (kCommToolOpen, ..Connect, ..Listen, ..Accept,
				..Disconnect, ..Release, ..Bind, ..Unbind, ..OptionMgmt,
				..GetProtAddr) goes to DoControl; each of them first has the
				options it came with processed - one at a time, through
				ProcessOptionStart, which a tool overrides for its own labels
				(the result goes back into each option) - and then calls the
				subclass's ...Start, which calls the matching ...Complete when
				the work is done, which answers the request.  Gets and puts
				go the same way to the subclass's GetBytes/PutBytes (their
				data a CBufferList in the tool's own memory, or a
				shared-memory object the tool reads through a
				CShadowBufferSegment when the request says it is "outside").

				Disconnecting, releasing and closing a connected tool is an
				*abort*: the state gets kToolStateWantAbort and
				TerminateConnection runs the subclass's termination procs
				(GetNextTermProc) phase by phase, then TerminateComplete
				answers whatever is outstanding and posts the disconnect
				event.

				A tool is started by its service (StartCommTool: a task, and
				the tool's port registered with the name server under the
				task's id and the service's four characters, where
				ServiceToPort finds it) and opened with an open request
				(OpenCommTool).

				DEVIATION (pointer size): the ROM's replies carry their own
				size (fSize), and a request's size tells an old request from
				one with options (0x1c for a get or put, 0x20 for a bind);
				the host uses sizeof, the requests having pointer-sized
				fields.  The request buffer is sized for the host's requests
				rather than the ROM's 0x40 bytes.

	Reconstructed from the MP2x00 US ROM (0x0006ccc4-0x00070424,
	0x0007097c-0x00070aec); each function cites its origin.
	docs/comms/README.md.
*/

#ifndef __COMMS_COMMTOOLS_H
#define __COMMS_COMMTOOLS_H

#ifndef __COMMTOOL_H
#include "CommTool.h"
#endif
#ifndef __COMMS_OPTIONS_H
#include "Options.h"
#endif
#ifndef __SHADOWBUFFERSEGMENT_H
#include "ShadowBufferSegment.h"
#endif

class TServiceInfo;
class TCMService;


// the channel's request type
inline CommToolRequestType
ChannelNumberToRequestType(CommToolChannelNumber channel)
{
	return (CommToolRequestType) (1 << channel);
}


class TCommTool : public TUTaskWorld
{
public:
						TCommTool(ULong serviceId);
						TCommTool(ULong serviceId, Size heapSize);
	virtual				~TCommTool();

protected:
	// TUTaskWorld
	virtual NewtonErr	TaskConstructor();
	virtual void		TaskDestructor();
	virtual void		TaskMain();

	// the virtuals, in the ROM's vtable order
	virtual UChar*		GetToolName() = 0;
	virtual void		HandleInternalEvent();
	virtual void		HandleRequest(TUMsgToken& msgToken, ULong msgType);
	virtual void		HandleReply(ULong userRefCon, ULong msgType);
	virtual void		HandleTimerTick();
	virtual NewtonErr	DoControl(ULong opCode, ULong msgType);
	virtual NewtonErr	DoKillControl(ULong msgType);
	virtual void		DoStatus(ULong opCode, ULong msgType);
	virtual void		GetCommEvent();
	virtual void		DoKillGetCommEvent();
	virtual NewtonErr	PostCommEvent(TCommToolGetEventReply& theEvent, NewtonErr result);
	virtual NewtonErr	OpenStart(TOptionArray* options);
	virtual NewtonErr	OpenComplete();
	virtual Boolean		Close();
	virtual void		CloseComplete(NewtonErr result);
	virtual void		ConnectStart();
	virtual void		ConnectComplete(NewtonErr result);
	virtual void		ListenStart();
	virtual void		ListenComplete(NewtonErr result);
	virtual void		AcceptStart();
	virtual void		AcceptComplete(NewtonErr result);
	virtual void		DisconnectComplete(NewtonErr result);
	virtual NewtonErr	ReleaseStart();
	virtual void		ReleaseComplete(NewtonErr result);
	virtual void		BindStart();
	virtual void		BindComplete(NewtonErr result);
	virtual void		UnbindStart();
	virtual void		UnbindComplete(NewtonErr result);
	virtual void		GetProtAddr();
	virtual void		OptionMgmt(TCommToolOptionMgmtRequest* request);
	virtual void		OptionMgmtComplete(NewtonErr result);
	virtual void		ProcessOptions(TCommToolOptionInfo* info);
	virtual void		ProcessOptionsContinue(TCommToolOptionInfo* info);
	virtual void		ProcessOptionsComplete(NewtonErr result, TCommToolOptionInfo* info);
	virtual NewtonErr	ProcessOptionsCleanUp(NewtonErr result, TCommToolOptionInfo* info);
	virtual void		ProcessCommOptionComplete(ULong status, TCommToolOptionInfo* info);
	virtual ULong		ProcessOptionStart(TOption* theOption, ULong label, ULong opcode);
	virtual void		ProcessOptionComplete(ULong status);
	virtual void		ProcessOption(TOption* theOption, ULong label, ULong opcode);
	virtual TUPort*		ForwardOptions();
	virtual NewtonErr	AddDefaultOptions(TOptionArray* options);
	virtual NewtonErr	AddCurrentOptions(TOptionArray* options);
	virtual ULong		ProcessPutBytesOptionStart(TOption* theOption, ULong label, ULong opcode);
	virtual void		ProcessPutBytesOptionComplete(ULong status);
	virtual ULong		ProcessGetBytesOptionStart(TOption* theOption, ULong label, ULong opcode);
	virtual void		ProcessGetBytesOptionComplete(ULong status);
	virtual void		PutBytes(CBufferList* clientBuffer) = 0;
	virtual void		PutFramedBytes(CBufferList* clientBuffer, Boolean endOfFrame) = 0;
	virtual void		PutComplete(NewtonErr result, ULong putBytesCount);
	virtual void		KillPut() = 0;
	virtual void		KillPutComplete(NewtonErr result);
	virtual void		GetBytes(CBufferList* clientBuffer) = 0;
	virtual void		GetFramedBytes(CBufferList* clientBuffer) = 0;
	virtual void		GetBytesImmediate(CBufferList* clientBuffer, Size threshold);
	virtual void		GetComplete(NewtonErr result, Boolean endOfFrame = false, ULong getBytesCount = 0);
	virtual void		KillGet() = 0;
	virtual void		KillGetComplete(NewtonErr result);
	virtual void		PrepGetRequest();
	virtual void		GetOptionsComplete(NewtonErr result);
	virtual void		PrepPutRequest();
	virtual void		PutOptionsComplete(NewtonErr result);
	virtual void		ResArbRelease(UChar* resName, UChar* resType);
	virtual void		ResArbReleaseStart(UChar* resName, UChar* resType);
	virtual void		ResArbReleaseComplete(NewtonErr result);
	virtual void		ResArbClaimNotification(UChar* resName, UChar* resType);
	virtual void		TerminateConnection();
	virtual void		TerminateComplete();
	virtual void		GetNextTermProc(ULong terminationPhase, ULong& terminationFlag, TerminateProcPtr& terminationProc);
	virtual void		SetChannelFilter(CommToolRequestType msgType, Boolean enable);

	// the non-virtual helpers
	NewtonErr			CreatePort(ULong serviceId, TUPort& port);
	void				UnRegisterPort();
	NewtonErr			GetToolPort(ULong serviceId, TUPort& port);
	NewtonErr			InitAsyncRPCMsg(TUAsyncMessage& asyncMsg, ULong refCon);
	CommToolChannelNumber	RequestTypeToChannelNumber(CommToolRequestType msgType);
	void				CompleteRequest(TUMsgToken& msgToken, NewtonErr result);
	NewtonErr			CompleteRequest(TUMsgToken& msgToken, NewtonErr result, TCommToolReply& reply);
	void				CompleteRequest(CommToolChannelNumber channel, NewtonErr result);
	NewtonErr			CompleteRequest(CommToolChannelNumber channel, NewtonErr result, TCommToolReply& reply);
	NewtonErr			FlushChannel(CommToolRequestType filter, NewtonErr flushResult);
	NewtonErr			GetConnectState();
	void				HoldAbort();
	void				AllowAbort();
	NewtonErr			StartAbort(NewtonErr abortError);
	Boolean				ShouldAbort(ULong stateFlag, NewtonErr result);
	void				PrepControlRequest(ULong msgType);
	void				PrepKillRequest();
	void				KillRequestComplete(CommToolRequestType requestTypeKilled, NewtonErr killResult);
	void				PrepResArbRequest();
	void				ProcessControlOptions(Boolean outside, TOptionArray* options, ULong optionCount);
	void				ProcessOptions(TOptionArray* options);
	void				Open();
	void				OpenOptionsComplete(NewtonErr result);
	void				OpenContinue();
	NewtonErr			ImportConnectPB(TCommToolConnectRequest* request);
	NewtonErr			CopyBackConnectPB(NewtonErr result);
	NewtonErr			ConnectCheck();
	void				Connect();
	void				ConnectOptionsComplete(NewtonErr result);
	void				Listen();
	void				ListenOptionsComplete(NewtonErr result);
	void				Accept();
	void				AcceptOptionsComplete(NewtonErr result);
	NewtonErr			Disconnect();
	void				Release();
	void				Bind();
	void				BindOptionsComplete(NewtonErr result);
	void				Unbind();

	// the request in hand, as each kind of request
	TCommToolControlRequest*	ControlRequest()	{ return (TCommToolControlRequest*) fRequest; }

	friend NewtonErr	StartCommTool(TCommTool* commTool, ULong serviceId, TServiceInfo* serviceInfo);

	ULong				fToolState;				// +0x18  kToolState...
	ULong				fTerminationFlag;		// +0x1c  the termination proc's state flag
	ULong				fTerminationPhase;		// +0x20
	NewtonErr			fAbortErr;				// +0x24
	ULong				fTerminationEvent;		// +0x28  what the disconnect event says
	Long				fAbortLock;				// +0x2c  HoldAbort's count
	TCMOCTConnectInfo	fConnectInfo;			// +0x30
	ULong				fRequestSize;			// +0x48  the size of the request received
	ULong				fRequest[16];			// +0x4c  the request (DEVIATION: host-sized)
	TUPort				fToolPort;				// +0x8c
	TCommToolMsgContainer	fRequests[kCommToolNumChannels];	// +0x94  each channel's request in hand
	ConnectParms		fConnectParms;			// +0x13c
	TCMOTransportInfo	fTransportInfo;			// +0x148
	ULong				fRequestsToKill;		// +0x174
	TCommToolOptionInfo	fOptionsInfo;			// +0x178  the control channel's options
	TCommToolOptionInfo	fGetOptionsInfo;		// +0x190
	TCommToolOptionInfo	fPutOptionsInfo;		// +0x1a8
	CBufferList*		fPutData;				// +0x1c0  the put in hand
	Boolean				fPutFrameData;			// +0x1c4
	Boolean				fPutEndOfFrame;			// +0x1c5
	Boolean				fGetNonBlocking;		// +0x1c6  the get in hand
	Boolean				fGetFrameData;			// +0x1c7
	CBufferList*		fGetData;				// +0x1c8
	Size				fGetThreshold;			// +0x1cc
	Boolean				fPassiveClaim;			// +0x1d0  ('cpcm)
	Boolean				fPassiveState;			// +0x1d1  ('cpst)
	Boolean				fField1D2;				// +0x1d2  (cleared by TaskConstructor; the IR sniffer sets it when it gives the port up)
	ULong				fControlOpCode;			// +0x1d4  the control request in hand
	TCommToolGetEventReply	fEventReply;		// +0x1d8  the disconnect event
	NewtonErr			fKillError;				// +0x1fc
	Boolean				fDone;					// +0x200  TaskMain ends
	Boolean				fPortRegistered;		// +0x201
	ULong				fServiceId;				// +0x204
	ULong				fChannelFilter;			// +0x208  the request types TaskMain receives
	TCommToolOptionMgmtRequest*	fForwardRequest;	// +0x20c  options forwarded to the tool below
	TUAsyncMessage*		fForwardMsg;			// +0x210
	TCommToolReply*		fForwardReply;			// +0x214
	CBufferList*		fGetBufferList;			// +0x218  over fGetShadow
	CBufferList*		fPutBufferList;			// +0x21c  over fPutShadow
	CShadowBufferSegment	fGetShadow;			// +0x220  an outside get's data
	CShadowBufferSegment	fPutShadow;			// +0x23c  an outside put's data
	Heap				fSavedHeap;				// +0x258
	Heap				fToolHeap;				// +0x25c
	Size				fHeapSize;				// +0x260
	ULong				fTimerInterval;			// +0x264  HandleTimerTick's period (0: none)
	TTimeout			fTimeout;				// +0x268  what is left of it
};


// Start a tool's task and say where its port is; open it with its options
// (asynchronously - the service's DoneStarting is told when the tool has
// answered).
NewtonErr	StartCommTool(TCommTool* commTool, ULong serviceId, TServiceInfo* serviceInfo);
NewtonErr	OpenCommTool(TObjectId portId, TOptionArray* options, TCMService* service);

// A service's port: registered by a tool task under its task id and the
// service's four characters (ServiceToPort(ULong, TUPort*) looks under
// task id nought).
NewtonErr	ServiceToPort(ULong serviceId, TUPort* port);
NewtonErr	ServiceToPort(ULong serviceId, TUPort* port, TObjectId taskId);

#endif	/* __COMMS_COMMTOOLS_H */
