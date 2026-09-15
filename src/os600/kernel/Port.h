/*
	File:		Port.h

	Contains:	TPort, the kernel's message port (kPortType): two queues of
				TSharedMemMsgs, senders waiting for a receiver and receivers
				waiting for a sender.  A send meets the first queued receiver
				whose filter matches the message type (or vice versa); the
				receiver's message is then completed with the sender's
				identity and the sender waits, queued on the receiver's message,
				until the receiver replies with MsgDone.

				Also here: the system-call bodies for ports and shared-memory
				messages (SWI 0-2, 13-23, 26, 33, GenericSWI 67), and message
				completion, which is where a blocked task gets its results
				written into its saved registers and is rescheduled.

				User side: TUPort / TUSharedMemMsg (UserPorts.h, UserSharedMem.h).

	Reconstructed from:	TPort 0x00193934-0x00194490, TSharedMemMsg::Complete* 0x001e288c-0x001e2aac,
				the SMem*KernelGlue routines 0x001e1e88-0x001e2740, GetPortInfo 0x000dcf6c,
				DeletePort 0x0014a480, DeleteSharedMem(Msg) 0x0014a4b0/0x0014a4c8,
				NotifySend/NotifyTimeout 0x00193e30/0x00193e80, DeferredNotify 0x00253e24,
				PortDeferredSendNotify 0x00193dcc, CheckCopyTask 0x0014b538,
				LowLevelCopyDoneFromKernelGlue 0x001e2200
*/

#ifndef __PORT_H
#define __PORT_H

#ifndef __SHAREDMEM_H
#include "SharedMem.h"
#endif
#ifndef __TASK_H
#include "Task.h"
#endif

const ULong kSMemMsgTimer_SenderTimeout = 0x00000080;	// TSharedMemMsg::fTimerFlags: the timeout belongs to a send
const ULong kSMemMsgTimer_DeferredSend = 0x00000020;	// TSharedMemMsg::fTimerFlags: a send from interrupt level wants a delay


// ROM size 0x38
class TPort : public TKernelObject
{
	public:
						TPort();
						~TPort();

		NewtonErr		Send(TSharedMemMsg* msg, ULong flags);
		NewtonErr		Receive(TSharedMemMsg* msg, ULong flags);
		NewtonErr		Reset(ULong senderFlags, ULong receiverFlags);		// kPortFlags_Abort / kPortFlags_Timeout
		NewtonErr		ResetFilter(TSharedMemMsg* msg, ULong filter);

		TDoubleQContainer	fSenders;		// +0x10  messages waiting for a receiver (linked through fPortQItem)
		TDoubleQContainer	fReceivers;		// +0x24  receive messages waiting for a sender
};


// system calls.  A glue returning void leaves its result in gCurrentTask's
// saved r0 (r1-r4 for the rest); one returning NewtonErr hands it back in r0
// directly - in the ROM these are void C functions whose last call's result
// happens to be left in r0, and the SWI stubs rely on it.
TObjectId	GetPortInfo(ULong which);												// SWI 0
void		PortSendKernelGlue(TObjectId portId, TObjectId msgId, TObjectId replyMemId, ULong msgType, ULong flags);	// SWI 1
void		PortReceiveKernelGlue(TObjectId portId, TObjectId msgId, ULong filter, ULong flags);	// SWI 2
void		PortResetFilterKernelGlue(TObjectId portId, TObjectId msgId, ULong filter);	// SWI 33
void		PortResetKernelGlue(TObjectId portId, ULong senderFlags, ULong receiverFlags);	// GenericSWI 67
NewtonErr	SMemSetBufferKernelGlue(TObjectId id, void* buffer, ULong size, ULong permissions);	// SWI 13
NewtonErr	SMemGetSizeKernelGlue(TObjectId id);									// SWI 14

// SWI 15/16: set a copy up in the calling task's registers (r0 = destination,
// r1 = source, r2 = bytes) and return kSMemCopy_Words / kSMemCopy_Bytes for the
// SWI handler to run the copy loop as the task, ending in LowLevelCopyDone
// (SWI 26); 0 when there is nothing to copy (r2 = 0), else an error.
enum { kSMemCopy_Bytes = 1, kSMemCopy_Words = 4 };
long		SMemCopyToKernelGlue(TObjectId id, void* buffer, ULong size, ULong offset, TObjectId sendersMsgId, ULong signature);
long		SMemCopyFromKernelGlue(TObjectId id, void* buffer, ULong size, ULong offset, TObjectId sendersMsgId, ULong signature);
NewtonErr	SMemMsgSetTimerParmsKernelGlue(TObjectId msgId, ULong timeout, ULong delayLo, ULong delayHi);	// SWI 17
NewtonErr	SMemMsgSetMsgAvailPortKernelGlue(TObjectId msgId, TObjectId portId);	// SWI 18
NewtonErr	SMemMsgGetSenderTaskIdKernelGlue(TObjectId msgId);						// SWI 19
NewtonErr	SMemMsgSetUserRefConKernelGlue(TObjectId msgId, void* refCon);			// SWI 20
NewtonErr	SMemMsgGetUserRefConKernelGlue(TObjectId msgId);						// SWI 21
void		SMemMsgCheckForDoneKernelGlue(TObjectId msgId, ULong flags);			// SWI 22
NewtonErr	SMemMsgMsgDoneKernelGlue(TObjectId msgId, long result, ULong sequence);	// SWI 23
NewtonErr	LowLevelCopyDoneFromKernelGlue(NewtonErr result, TTask* task, TRegister pc);	// SWI 26

// deferred work run from the scheduler path
void		DeferredNotify();				// messages whose timer fired (gTimerDeferred)
void		PortDeferredSendNotify();		// sends made from interrupt level (gDeferredSends)
void		NotifySend(TSharedMemMsg* msg);
void		NotifyTimeout(TSharedMemMsg* msg);
void		CheckCopyTask();

// destructors used by ObjectScavenger
void		DeletePort(TPort* port);
void		DeleteSharedMem(TSharedMem* mem);
void		DeleteSharedMemMsg(TSharedMemMsg* msg);

#endif	/* __PORT_H */
