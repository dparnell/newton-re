/*
	File:		SharedMem.h

	Contains:	TSharedMem, a kernel object describing a buffer one task exposes
				to others (kSharedMemType), and TSharedMemMsg, the message that
				travels through a TPort (kSharedMemMsgType): a shared buffer plus
				everything needed to deliver it, time it out, and complete it.

				User code sees them as TUSharedMem / TUSharedMemMsg
				(UserSharedMem.h) through SWIs 13-23.

				Layouts are the ROM's; members whose use has not been established
				are named by their offset.

	Reconstructed from:	TSharedMem::Init 0x001e1e6c, TSharedMemMsg 0x001e2594/0x001e280c,
				the SMem*KernelGlue routines 0x001e1e88-0x001e2740, TTimerEngine 0x00253cd4-0x002542d0,
				TSharedMemMsg::Complete* 0x001e288c-0x001e2aac
*/

#ifndef __SHAREDMEM_H
#define __SHAREDMEM_H

#ifndef __KERNELOBJECT_H
#include "KernelObject.h"
#endif
#ifndef __DOUBLEQ_H
#include "DoubleQ.h"
#endif

class TEnvironment;
class TSharedMemMsg;

// TSharedMem::fFlags are the kSMemReadOnly / kSMemNoSizeChangeOnCopyTo bits of SharedTypes.h.

// TSharedMemMsg::fTimerFlags - how the message is queued in the timer engine
enum
{
	kSMemMsgTimer_Delay		= 0x00000200,	// waiting for its absolute fExpiryTime (delayed send)
	kSMemMsgTimer_Timeout	= 0x00000400,	// waiting for fTimeout to run out
	kSMemMsgTimer_Generic	= 0x00000600	// TTimerEngine::QueueTimer: caller-supplied notify proc
};

// TSharedMemMsg::fMsgFlags, in addition to the kPortSend_* / kSMemMsgFlags_* bits of SharedTypes.h
enum
{
	kSMemMsgFlags_CompleteToReceiverPort	= 0x01000000,	// on completion, re-send to the notify port (receiver side)
	kSMemMsgFlags_CompleteToSenderPort		= 0x02000000,	// ... (sender side)
	kSMemMsgFlags_CompleteToPortMask		= 0x03000000
};

const long kSMemMsgStatus_InProgress = 1;		// TSharedMemMsg::fStatus while a call is outstanding
const ULong kSMemMsgNoTimeout = 0xFFFFFFFF;		// TSharedMemMsg::fTimeout when none is wanted

typedef void (*TimerNotifyProcPtr)(void* data);


// ROM size 0x24
class TSharedMem : public TKernelObject
{
	public:
		NewtonErr		Init(TEnvironment* environment);

		void*			fBuffer;		// +0x10
		ULong			fSize;			// +0x14  capacity
		ULong			fCurSize;		// +0x18  bytes in use (SMemGetSize reports this)
		ULong			fFlags;			// +0x1c  kSMem*
		TEnvironment*	fEnvironment;	// +0x20  the owner's environment; copies switch to it
};


// ROM size 0xa8
class TSharedMemMsg : public TSharedMem
{
	public:
						TSharedMemMsg();
						~TSharedMemMsg();

		NewtonErr		Init(TEnvironment* environment);
		void			CompleteMsg(Boolean abort, ULong flags, long result);
		void			CompleteSender(long result);
		void			CompleteReceiver(TSharedMemMsg* sender, long result);

		ULong			fTimeout;		// +0x24  ticks, or kSMemMsgNoTimeout
		Int64			fExpiryTime;	// +0x28  absolute time the timer engine fires
		TDoubleQItem	fTimerQItem;	// +0x30  link in TTimerEngine / gTimerDeferred
		ULong			fTimerFlags;	// +0x3c  kSMemMsgTimer_*
		TObjectId		fPortId;		// +0x40  port the message was sent to (for delayed sends)
		long			fStatus;		// +0x44  kSMemMsgStatus_InProgress, or the completion result
		void*			fUserRefCon;	// +0x48
		ULong			fReplyMemId;	// +0x4c  handed to the receiver as fSenderReplyMemId
		ULong			fMsgFlags;		// +0x50  kPortSend_* / kSMemMsgFlags_* / kSMemMsgFlags_CompleteTo*
		ULong			fFilter;		// +0x54  receive: message types accepted (kMsgType_MatchAll = any);
										//        delayed send: the send flags to use when the delay is up
		TObjectId		fSenderMsgId;	// +0x58  receiver side: id of the message that was received
		ULong			fSenderReplyMemId;	// +0x5c  receiver side: the sender's fReplyMemId
		ULong			fSenderMsgFlags;	// +0x60  receiver side: the sender's fMsgFlags
		ULong			fSequenceNo;	// +0x64  receiver side: sequence number to quote in MsgDone
		TObjectId		fMsgAvailPortId;// +0x68  port to notify when a message arrives (SMemMsgSetMsgAvailPort)
		TObjectId		fNotifyId;		// +0x6c  task blocked on, or port to forward to, when this message completes
		TObjectId		fSenderTaskId;	// +0x70
		ULong			fCurrentSequence;	// +0x74  sequence of the call in progress (checked against caller's)
		ULong			fSequenceCounter;	// +0x78  next sequence number to hand out (starts at 1, never 0)
		TObjectId		fCopyingTaskId;	// +0x7c  task in the middle of a low-level copy into/out of us
		TDoubleQItem	fPortQItem;		// +0x80  link in a port's sender/receiver queue (+0x88 = that queue)
		TDoubleQContainer fSenders;		// +0x8c  receiver side: sender messages awaiting our MsgDone
		void*			fNotifyData;	// +0xa0  timer engine notify argument
		TimerNotifyProcPtr fNotifyProc;	// +0xa4  timer engine notify (QueueNotify for timeouts/delays)
};

#endif	/* __SHAREDMEM_H */
