/*
	File:		Port.cpp

	Contains:	TPort, message completion, and the port / shared-memory
				system calls.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	SMemCopyToKernelGlue / SMemCopyFromKernelGlue (SWI 15/16) set up a copy
	that the SWI handler then performs in the caller's context; they belong
	with the syscall layer and follow there.
*/

#include "Port.h"
#include "Monitor.h"
#include "Task.h"
#include "Scheduler.h"
#include "TimerEngine.h"
#include "ObjectTable.h"
#include "KernelObjects.h"
#include "KernelGlobals.h"
#include "OSErrors.h"
#include "hal/Atomic.h"
#include "UserObjects.h"
#include "UserPorts.h"

#include <stddef.h>
#include <stdint.h>


// A message on the receive side accepts a sender when its filter matches
// the sender's message type.
static inline Boolean
FilterMatches(ULong filter, ULong msgType)
{
	return filter == (ULong) kMsgType_MatchAll || (filter & msgType) != 0;
}


/* -------------------------------------------------------------------------------
	TPort
------------------------------------------------------------------------------- */

// (No constructor symbol in the ROM; ports are set up by the object manager.
// Both queues link messages through TSharedMemMsg::fPortQItem.)
TPort::TPort()
	: fSenders(offsetof(TSharedMemMsg, fPortQItem)), fReceivers(offsetof(TSharedMemMsg, fPortQItem))
{
}


// Fail every queued message with `result`; used by Reset and destruction.
static void
DrainSenders(TDoubleQContainer& queue, long result)
{
	TSharedMemMsg* msg;
	while ((msg = (TSharedMemMsg*) queue.Remove()) != nil)
	{
		gTimerEngine->Remove(msg);
		ExitFIQAtomic();
		msg->CompleteSender(result);
		EnterFIQAtomic();
	}
}

static void
DrainReceivers(TDoubleQContainer& queue, long result)
{
	TSharedMemMsg* msg;
	while ((msg = (TSharedMemMsg*) queue.Remove()) != nil)
	{
		gTimerEngine->Remove(msg);
		ExitFIQAtomic();
		msg->CompleteReceiver(nil, result);
		EnterFIQAtomic();
	}
}


// ROM 0x00191b40 __dt__5TPortFv
TPort::~TPort()
{
	EnterFIQAtomic();
	DrainSenders(fSenders, kError_Port_No_Longer_Exists);
	DrainReceivers(fReceivers, kError_Port_No_Longer_Exists);
	ExitFIQAtomic();
}


// ROM 0x00191914 Reset__5TPortFUlT1
// Fails the queued senders and/or receivers, as aborted or as timed out.
NewtonErr
TPort::Reset(ULong senderFlags, ULong receiverFlags)
{
	EnterFIQAtomic();
	if (senderFlags != 0)
		DrainSenders(fSenders, (senderFlags & kPortFlags_Abort) ? kError_Call_Aborted : kError_Message_Timed_Out);
	if (receiverFlags != 0)
		DrainReceivers(fReceivers, (receiverFlags & kPortFlags_Abort) ? kError_Call_Aborted : kError_Message_Timed_Out);
	ExitFIQAtomic();
	return noErr;
}


// ROM 0x00191a7c ResetFilter__5TPortFP13TSharedMemMsgUl
// Changes the filter of a queued receive and delivers a now-matching sender.
NewtonErr
TPort::ResetFilter(TSharedMemMsg* msg, ULong filter)
{
	TSharedMemMsg* receiver = (TSharedMemMsg*) fReceivers.Peek();
	while (receiver != nil && receiver != msg)
		receiver = (TSharedMemMsg*) fReceivers.GetNext(receiver);
	if (receiver == nil)
		return kError_No_Message_Waiting;

	msg->fFilter = filter;
	for (TSharedMemMsg* sender = (TSharedMemMsg*) fSenders.Peek(); sender != nil; sender = (TSharedMemMsg*) fSenders.GetNext(sender))
	{
		if (FilterMatches(msg->fFilter, sender->fMsgFlags))
		{
			fSenders.RemoveFromQueue(sender);
			msg->CompleteReceiver(sender, noErr);
			return noErr;
		}
	}
	return noErr;
}


// ROM 0x0019211c Send__5TPortFP13TSharedMemMsgUl
// `flags` are kPortFlags_* including the kernel-internal ones the glue adds.
NewtonErr
TPort::Send(TSharedMemMsg* msg, ULong flags)
{
	if ((flags & kPortFlags_TimerWanted) && msg->fTimeout != 0)
	{
		if (!gTimerEngine->QueueTimeout(msg))
		{
			msg->fStatus = kError_Message_Timed_Out;
			return kError_Message_Timed_Out;
		}
		msg->fTimerFlags |= kSMemMsgTimer_SenderTimeout;
	}

	TSharedMemMsg* receiver = (TSharedMemMsg*) fReceivers.Peek();
	while (receiver != nil && !FilterMatches(receiver->fFilter, msg->fMsgFlags))
		receiver = (TSharedMemMsg*) fReceivers.GetNext(receiver);

	if (flags & kPortFlags_CanRemoveTask)
		UnScheduleTask(gCurrentTask);
	if (receiver == nil)
	{
		if (flags & kPortFlags_Urgent)
			fSenders.AddToFront(msg);
		else
			fSenders.Add(msg);
	}
	else
	{
		if (flags & kPortFlags_ScheduleOnSend)
			WantSchedule();
		fReceivers.RemoveFromQueue(receiver);
		receiver->CompleteReceiver(msg, noErr);
	}
	return noErr;
}


// ROM 0x00192330 Receive__5TPortFP13TSharedMemMsgUl
NewtonErr
TPort::Receive(TSharedMemMsg* msg, ULong flags)
{
	NewtonErr result = noErr;
	if ((flags & kPortFlags_TimerWanted) && msg->fTimeout != 0 && !gTimerEngine->QueueTimeout(msg))
	{
		result = kError_Message_Timed_Out;
	}
	else
	{
		TSharedMemMsg* sender = (TSharedMemMsg*) fSenders.Peek();
		while (sender != nil && !FilterMatches(msg->fFilter, sender->fMsgFlags))
			sender = (TSharedMemMsg*) fSenders.GetNext(sender);

		if (flags & kPortFlags_IsMsgAvail)
		{
			// a peek: report, do not take
			msg->fStatus = 0;
			gTimerEngine->Remove(msg);
			if (sender == nil)
				result = kError_No_Message_Waiting;
		}
		else if (sender == nil)
		{
			if (flags & kPortFlags_ReceiveOnMsgAvail)
			{
				gTimerEngine->Remove(msg);
				result = kError_No_Message_Waiting;
			}
			else
			{
				if (flags & kPortFlags_CanRemoveTask)
					UnScheduleTask(gCurrentTask);
				if (flags & kPortFlags_Urgent)
					fReceivers.AddToFront(msg);
				else
					fReceivers.Add(msg);
			}
		}
		else
		{
			fSenders.RemoveFromQueue(sender);
			msg->CompleteReceiver(sender, noErr);
		}
		if (result == noErr)
			return noErr;
	}
	msg->fStatus = result;
	return result;
}


/* -------------------------------------------------------------------------------
	Message completion
------------------------------------------------------------------------------- */

// ROM 0x001e0524 CompleteMsg__13TSharedMemMsgFUcUll
// Finishes a call on this message: cancels its timer and queue membership,
// releases any copy in progress, then tells whoever is waiting - a blocked
// task gets the result and the message details in r0-r4 and runs next; a
// notify port gets the message re-sent (unless `abort`, when that is an
// error); otherwise the result is just recorded in fStatus.
void
TSharedMemMsg::CompleteMsg(Boolean abort, ULong flags, long result)
{
	fMsgFlags = 0;
	gTimerEngine->Remove(this);
	if (fPortQItem.fContainer != nil)
		fPortQItem.fContainer->DeleteFromQueue(this);
	if (fCopyingTaskId != 0)
	{
		TTask* task;
		if (ConvertIdToObj(kTaskType, fCopyingTaskId, &task) == noErr)
			LowLevelCopyDoneFromKernelGlue(result == noErr ? kError_Copy_Aborted : result, task, 0);
	}
	fCopyingTaskId = 0;
	fCurrentSequence = 0;

	NewtonErr err;
	switch (ObjectType(fNotifyId))
	{
	case kNoType:
		fStatus = result;
		return;
	case kPortType:
		{
			TPort* port;
			err = ConvertIdToObj(kPortType, fNotifyId, &port);
			if (err == noErr)
			{
				if (!abort)
				{
					fStatus = result;
					fMsgFlags = flags;
					port->Send(this, 0);
					return;
				}
				err = kError_Nested_Collection;
			}
		}
		break;
	case kTaskType:
		{
			TTask* task;
			err = ConvertIdToObj(kTaskType, fNotifyId, &task);
			if (err == noErr)
			{
				task->fRegister[0] = result;
				task->fRegister[1] = fSenderMsgId;
				task->fRegister[2] = fSenderReplyMemId;
				task->fRegister[3] = fSenderMsgFlags;
				task->fRegister[4] = fSequenceNo;
				gKernelScheduler->fPreferredTask = task;
				ScheduleTask(task);
				fStatus = result;
				return;
			}
		}
		break;
	default:
		err = kError_Im_Totally_Confused;
		break;
	}
	fStatus = err;
}


// ROM 0x001e0514 CompleteSender__13TSharedMemMsgFl
// (The ROM inlines the tail of CompleteMsg; the flags forwarded to a notify
// port are kSMemMsgFlags_CompleteToSenderPort.)
void
TSharedMemMsg::CompleteSender(long result)
{
	CompleteMsg(false, kSMemMsgFlags_CompleteToSenderPort, result);
}


// ROM 0x001e0474 CompleteReceiver__13TSharedMemMsgFP13TSharedMemMsgl
// A receive has met a sender (result == noErr) or failed.  The sender's
// identity is recorded for the receiver; unless the sender was itself a
// completion forwarded to a port ("collected"), it is queued on this message
// to wait for the receiver's MsgDone, stamped with a sequence number.
void
TSharedMemMsg::CompleteReceiver(TSharedMemMsg* sender, long result)
{
	Boolean collected = false;
	if (result == noErr)
	{
		fSenderReplyMemId = sender->fReplyMemId;
		fSenderMsgFlags = sender->fMsgFlags;
		fSenderMsgId = sender->fId;
		fSequenceNo = 0;
		collected = (sender->fMsgFlags & kSMemMsgFlags_CompleteToPortMask) != 0;
		if (collected)
			sender->fMsgFlags = 0;
		else
		{
			fSenders.Add(sender);
			sender->fCurrentSequence = sender->fSequenceCounter;
			fSequenceNo = sender->fSequenceCounter;
			if (++sender->fSequenceCounter == 0)
				sender->fSequenceCounter = 1;
		}
	}
	CompleteMsg(collected, kSMemMsgFlags_CompleteToReceiverPort, result);
}


// ROM 0x001e0694 __dt__13TSharedMemMsgFv
// Senders still waiting for our reply are failed; a call in progress on the
// message itself is completed with an error.
TSharedMemMsg::~TSharedMemMsg()
{
	EnterFIQAtomic();
	TSharedMemMsg* sender;
	while ((sender = (TSharedMemMsg*) fSenders.Remove()) != nil)
	{
		gTimerEngine->Remove(sender);
		ExitFIQAtomic();
		sender->CompleteSender(kError_Receiver_Object_No_Longer_Exists);
		EnterFIQAtomic();
	}
	ExitFIQAtomic();
	gTimerEngine->Remove(this);
	if (fStatus == kSMemMsgStatus_InProgress || (fMsgFlags & kSMemMsgFlags_CompleteToPortMask))
		CompleteMsg(true, 0, kError_SharedMemMsg_No_Longer_Exists);
}


/* -------------------------------------------------------------------------------
	Deferred work
------------------------------------------------------------------------------- */

// ROM 0x00191e10 NotifySend__FP13TSharedMemMsg
// A delayed or interrupt-level send whose time has come.
void
NotifySend(TSharedMemMsg* msg)
{
	TPort* port;
	NewtonErr err = ConvertIdToObj(kPortType, msg->fPortId, &port);
	if (err == noErr)
		err = port->Send(msg, msg->fFilter);		// the send flags were parked in fFilter
	if (err != noErr)
		msg->CompleteSender(err);
}


// ROM 0x00191e60 NotifyTimeout__FP13TSharedMemMsg
void
NotifyTimeout(TSharedMemMsg* msg)
{
	if (msg->fTimerFlags & kSMemMsgTimer_SenderTimeout)
		msg->CompleteSender(kError_Message_Timed_Out);
	else
		msg->CompleteReceiver(nil, kError_Message_Timed_Out);
}


// ROM 0x00255d70 DeferredNotify__Fv
void
DeferredNotify()
{
	for (;;)
	{
		EnterAtomic();
		TSharedMemMsg* msg = (TSharedMemMsg*) gTimerDeferred->Remove();
		ExitAtomic();
		if (msg == nil)
			break;
		Boolean timedOut = (msg->fTimerFlags & kSMemMsgTimer_Timeout) != 0;
		msg->fTimerFlags &= ~kSMemMsgTimer_Generic;
		if (timedOut)
			NotifyTimeout(msg);
		else
			NotifySend(msg);
	}
}


// ROM 0x00191c0c SendForInterrupt__FUlN21PvN31P5TTimeUc
// A send from interrupt level: nothing may block and no object may be made,
// so the message is filled in and queued on gDeferredSends for
// PortDeferredSendNotify to hand to the port once the interrupt is over.
// The message must be one the caller made earlier and is not in use
// (fStatus is set to in-progress under the FIQ lock so two interrupts
// cannot take the same one), and the send flags it would have passed are
// parked in fFilter, which a message being sent has no other use for.
long
SendForInterrupt(TObjectId portId, TObjectId msgId, TObjectId replyId, void* content, ULong size,
                 ULong msgType, TTimeout timeout, TTime* futureTimeToSend, Boolean urgent)
{
	long err = noErr;
	TSharedMemMsg* msg = ObjectType(msgId) == kSharedMemMsgType ? (TSharedMemMsg*) gObjectTable->Get(msgId) : nil;
	if (msg == nil)
		err = kError_Bad_ObjectId;
	else if (replyId == 0 || (err = ConvertIdToObj(kSharedMemType, replyId, nil)) == noErr)
	{
		EnterFIQAtomic();
		if (msg->fStatus == kSMemMsgStatus_InProgress || (msg->fMsgFlags & kSMemMsgFlags_CompleteToPortMask) != 0)
		{
			ExitFIQAtomic();
			err = kError_Message_Already_Posted;
		}
		else
		{
			msg->fStatus = kSMemMsgStatus_InProgress;
			gWantDeferred = true;
			gDeferredSends->Add(msg);
			// (the ROM, when this is an FIQ, fires the timer alarm so that the
			// IRQ side runs soon; the host has no FIQ mode - IsFIQMode/FireAlarm
			// are NOT YET RECONSTRUCTED)
			ExitFIQAtomic();
			msg->fFilter = kPortFlags_ScheduleOnSend | kPortFlags_TimerWanted | kPortFlags_Async
						 | (urgent ? kPortFlags_Urgent : 0);
			if (futureTimeToSend == nil)
			{
				msg->fTimerFlags = 0;
				msg->fTimeout = timeout;
				msg->fExpiryTime.hi = 0;
				msg->fExpiryTime.lo = 0;
			}
			else
			{
				msg->fTimerFlags = kSMemMsgTimer_DeferredSend;
				msg->fTimeout = timeout;
				msg->fExpiryTime = futureTimeToSend->time;
			}
			msg->fBuffer = content;
			msg->fSize = size;
			msg->fReplyMemId = replyId;
			msg->fMsgFlags = msgType;
			msg->fFlags = 1;
			msg->fCurSize = size;
			msg->fPortId = portId;
			msg->fSenderTaskId = 0;
			msg->fNotifyId = msg->fMsgAvailPortId;
		}
	}
	return err;
}


// ROM 0x00191dac PortDeferredSendNotify__Fv
void
PortDeferredSendNotify()
{
	for (;;)
	{
		EnterFIQAtomic();
		TSharedMemMsg* msg = (TSharedMemMsg*) gDeferredSends->Remove();
		ExitFIQAtomic();
		if (msg == nil)
			break;
		if ((msg->fTimerFlags & kSMemMsgTimer_DeferredSend) == 0 || !gTimerEngine->QueueDelay(msg))
			NotifySend(msg);
	}
}


// ROM 0x001499dc CheckCopyTask__Fv
// Fails copies whose shared memory object has gone (or lost its owner).
void
CheckCopyTask()
{
	TTask* task = (TTask*) gCopyTasks->Peek();
	while (task != nil)
	{
		TTask* next = (TTask*) gCopyTasks->GetNext(task);
		TKernelObject* mem = gObjectTable->Get(task->fCopyMemId);
		Boolean alive = mem != nil && (mem->fOwnerId == mem->fId || gObjectTable->Exists(mem->fOwnerId));
		if (!alive)
			LowLevelCopyDoneFromKernelGlue(kError_Bad_ObjectId, task, 0);
		task = next;
	}
}


// ROM 0x001dfde8 LowLevelCopyDoneFromKernelGlue
// SWI 26, also called internally: a shared-memory copy has finished (or must
// be abandoned).  The task gets its result in r0, the size in r1 and resumes
// at the pc saved when the copy was set up.
NewtonErr
LowLevelCopyDoneFromKernelGlue(NewtonErr result, TTask* task, TRegister pc)
{
	if (!gCopyTasks->RemoveFromQueue(task))
	{
		task->fRegister[15] = pc;
		return kError_Bad_Parameters;
	}
	if (task == gCurrentTask)
		gCopyDone = true;
	if (result == noErr)
		result = task->fCopyResult;
	TSharedMemMsg* msg = nil;
	ConvertIdToObj(kSharedMemMsgType, task->fCopyMsgId, &msg);
	if (msg != nil)
		msg->fCopyingTaskId = 0;
	task->fRegister[1] = task->fCopySize;
	task->fCopyEnvironment = nil;
	task->fRegister[15] = task->fCopySavedPC;
	task->fRegister[0] = result;
	task->fCopyMemId = 0;
	task->fCopyMsgId = 0;
	if (task->fMonitorQItem.fContainer != nil)
		task->fMonitorQItem.fContainer->DeleteFromQueue(task);
	return noErr;
}


/* -------------------------------------------------------------------------------
	Destructors used by ObjectScavenger
------------------------------------------------------------------------------- */

// ROM 0x00148924 DeletePort__FP5TPort
void
DeletePort(TPort* port)
{
	if (port != nil)
		delete port;
}


// ROM 0x00148954 DeleteSharedMem__FP10TSharedMem
// (The ROM inlines CheckCopyTask after the delete.)
void
DeleteSharedMem(TSharedMem* mem)
{
	delete mem;
	CheckCopyTask();
}


// ROM 0x0014896c DeleteSharedMemMsg__FP13TSharedMemMsg
void
DeleteSharedMemMsg(TSharedMemMsg* msg)
{
	if (msg != nil)
		delete msg;
	CheckCopyTask();
}


/* -------------------------------------------------------------------------------
	System calls.  The SWI handler passes the caller's r1-r4 as arguments and
	the glue leaves results in the caller's saved registers.
------------------------------------------------------------------------------- */

static inline void
SetResult(NewtonErr err)
{
	gCurrentTask->fRegister[0] = err;
}


// ROM 0x000dbccc GetPortInfo
// SWI 0: the ids of the well-known ports.
TObjectId
GetPortInfo(ULong which)
{
	switch (which)
	{
	case 0:		return gTheObjectManagerMonitor->fId;
	case 1:		return gNullPort->fId;
	case 2:		return gNameServer->fId;
	}
	return 0;
}


// ROM 0x00191f88 PortSendKernelGlue
// SWI 1.  Sets the message up from the caller's parameters and sends it; a
// synchronous send blocks the caller (kPortFlags_CanRemoveTask) until the
// receiver's MsgDone completes the message into the caller's registers.
void
PortSendKernelGlue(TObjectId portId, TObjectId msgId, TObjectId replyMemId, ULong msgType, ULong flags)
{
	SetResult(noErr);
	TPort* port;
	TSharedMemMsg* msg;
	NewtonErr err = ConvertIdToObj(kPortType, portId, &port);
	if (err == noErr)
		err = ConvertIdToObj(kSharedMemMsgType, msgId, &msg);
	if (err == noErr && replyMemId != 0)
		err = ConvertIdToObj(kSharedMemType, replyMemId, nil);
	if (err == noErr)
	{
		EnterFIQAtomic();
		if (msg->fStatus == kSMemMsgStatus_InProgress || (msg->fMsgFlags & kSMemMsgFlags_CompleteToPortMask))
		{
			ExitFIQAtomic();
			SetResult(kError_Message_Already_Posted);
			return;
		}
		msg->fStatus = kSMemMsgStatus_InProgress;
		ExitFIQAtomic();

		ULong sendFlags = flags & kPortFlags_ReservedMask;
		msg->fReplyMemId = LocalToGlobalId(replyMemId);
		msg->fMsgFlags = msgType & kMsgType_ReservedMask;
		if (msg->fMsgFlags == 0)
			msg->fMsgFlags = kMsgType_NoMsgTypeSet;
		msg->fPortId = portId;
		msg->fSenderTaskId = gCurrentTask->fId;
		msg->fTimerFlags = 0;
		if (flags & kPortFlags_WantTimeout)
			sendFlags |= kPortFlags_TimerWanted;
		if (flags & kPortFlags_Async)
			msg->fNotifyId = msg->fMsgAvailPortId;
		else
		{
			msg->fNotifyId = msg->fSenderTaskId;
			sendFlags |= kPortFlags_CanRemoveTask;
		}
		if ((sendFlags & kPortFlags_WantDelay) && gTimerEngine->QueueDelay(msg))
		{
			// sent later by NotifySend, with these flags, once the delay is up
			if (sendFlags & kPortFlags_CanRemoveTask)
				UnScheduleTask(gCurrentTask);
			msg->fFilter = sendFlags & ~kPortFlags_CanRemoveTask;
			return;
		}
		err = port->Send(msg, sendFlags);
	}
	if (err != noErr)
		SetResult(err);
}


// ROM 0x00192224 PortReceiveKernelGlue
// SWI 2.
void
PortReceiveKernelGlue(TObjectId portId, TObjectId msgId, ULong filter, ULong flags)
{
	SetResult(noErr);
	TPort* port;
	TSharedMemMsg* msg;
	NewtonErr err = ConvertIdToObj(kPortType, portId, &port);
	if (err == noErr)
		err = ConvertIdToObj(kSharedMemMsgType, msgId, &msg);
	if (err == noErr)
	{
		EnterFIQAtomic();
		if (msg->fStatus == kSMemMsgStatus_InProgress || (msg->fMsgFlags & kSMemMsgFlags_CompleteToPortMask))
		{
			ExitFIQAtomic();
			SetResult(kError_Message_Already_Posted);
			return;
		}
		msg->fStatus = kSMemMsgStatus_InProgress;
		ExitFIQAtomic();

		ULong receiveFlags = flags & kPortFlags_ReservedMask;
		msg->fMsgFlags = 0;
		msg->fFilter = filter;
		msg->fSenderTaskId = gCurrentTask->fId;
		msg->fTimerFlags = 0;
		if (flags & kPortFlags_Async)
			msg->fNotifyId = msg->fMsgAvailPortId;
		else
		{
			msg->fNotifyId = msg->fSenderTaskId;
			receiveFlags |= kPortFlags_CanRemoveTask;
		}
		if (flags & kPortFlags_WantTimeout)
			receiveFlags |= kPortFlags_TimerWanted;
		err = port->Receive(msg, receiveFlags);
	}
	if (err != noErr)
		SetResult(err);
}


// ROM 0x00191a00 PortResetFilterKernelGlue
// SWI 33.
void
PortResetFilterKernelGlue(TObjectId portId, TObjectId msgId, ULong filter)
{
	SetResult(noErr);
	TPort* port;
	TSharedMemMsg* msg;
	NewtonErr err = ConvertIdToObj(kPortType, portId, &port);
	if (err == noErr)
		err = ConvertIdToObj(kSharedMemMsgType, msgId, &msg);
	if (err == noErr)
		err = port->ResetFilter(msg, filter);
	if (err != noErr)
		SetResult(err);
}


// ROM 0x00192470 PortResetKernelGlue__FUlN21
// GenericSWI 67.
void
PortResetKernelGlue(TObjectId portId, ULong senderFlags, ULong receiverFlags)
{
	SetResult(noErr);
	TPort* port;
	NewtonErr err = ConvertIdToObj(kPortType, portId, &port);
	if (err == noErr)
		err = port->Reset(senderFlags, receiverFlags);
	if (err != noErr)
		SetResult(err);
}


// ROM 0x001dfee8 SMemSetBufferKernelGlue
// SWI 13.
NewtonErr
SMemSetBufferKernelGlue(TObjectId id, void* buffer, ULong size, ULong permissions)
{
	TSharedMem* mem;
	NewtonErr err = ConvertMemOrMsgIdToObj(id, &mem);
	if (err == noErr)
	{
		mem->fBuffer = buffer;
		mem->fSize = size;
		mem->fCurSize = size;
		mem->fFlags = permissions;
	}
	return err;
}


// ROM 0x001dff38 SMemGetSizeKernelGlue
// SWI 14: r1 = size in use, r2 = the buffer address if the caller (or a task
// it inherits from through the bequeath chain) owns the object, r3 = the
// user ref con if it is a message.
NewtonErr
SMemGetSizeKernelGlue(TObjectId id)
{
	TSharedMem* mem;
	NewtonErr err = ConvertMemOrMsgIdToObj(id, &mem);
	if (err != noErr)
		return err;
	gCurrentTask->fRegister[1] = mem->fCurSize;

	TObjectId me = gCurrentTask->fId;
	TObjectId owner = mem->fOwnerId;
	Boolean ownsIt = true;
	while (owner != me)
	{
		TTask* task;
		if (owner == 0 || ConvertIdToObj(kTaskType, owner, &task) != noErr)
		{
			ownsIt = false;
			break;
		}
		owner = task->fBequeathId;
	}
	gCurrentTask->fRegister[2] = ownsIt ? (TRegister) mem->fBuffer : 0;

	TSharedMemMsg* msg;
	if (ConvertIdToObj(kSharedMemMsgType, id, &msg) == noErr)
		gCurrentTask->fRegister[3] = (TRegister) msg->fUserRefCon;
	else
		gCurrentTask->fRegister[3] = 0;
	return noErr;
}


// The checks a copy makes on the message it is done under (if any): the
// caller must hold the message's current sequence and the message must be
// in progress with no other copy under way.  Returns the message.
static NewtonErr
CopyMessageCheck(TObjectId sendersMsgId, ULong signature, TSharedMemMsg** outMsg)
{
	*outMsg = nil;
	if (sendersMsgId == 0)
		return noErr;
	TSharedMemMsg* msg;
	NewtonErr err = ConvertIdToObj(kSharedMemMsgType, sendersMsgId, &msg);
	if (err != noErr)
		return err;
	if (msg->fCopyingTaskId != 0)
		return kError_Call_Already_In_Progress;
	if (msg->fStatus != kSMemMsgStatus_InProgress)
		return kError_Call_Not_In_Progress;
	if (msg->fCurrentSequence != signature)
		return kError_Bad_Signature;
	*outMsg = msg;
	return noErr;
}


// Records a copy in the calling task: its registers carry the addresses and
// size for the copy loop, the fCopy* fields what to report when it is done
// (LowLevelCopyDone), and the task joins gCopyTasks.  Word copy if everything
// is aligned.
static long
SetUpCopy(TSharedMem* mem, TSharedMemMsg* msg, TObjectId memId, TObjectId msgId, void* destination, const void* source, ULong size, long result, ULong reportSize)
{
	TTask* task = gCurrentTask;
	if (msg != nil)
		msg->fCopyingTaskId = task->fId;
	task->fCopyMsgId = msgId;
	task->fCopySavedPC = task->fRegister[15];
	task->fCopySize = reportSize;
	task->fRegister[2] = size;
	task->fRegister[1] = (TRegister) source;
	task->fRegister[0] = (TRegister) destination;
	task->fCopyEnvironment = mem->fEnvironment;
	gCopyTasks->Add(task);
	task->fCopyResult = result;
	task->fCopyMemId = memId;
	Boolean aligned = (task->fRegister[2] & 3) == 0 && (task->fRegister[1] & 3) == 0 && (task->fRegister[0] & 3) == 0;
	return aligned ? kSMemCopy_Words : kSMemCopy_Bytes;
}


// ROM 0x001dfa70 SMemCopyToKernelGlue
// SWI 15: copy the caller's buffer into the shared memory at offset.  A
// read-only memory refuses; a copy past the end is truncated
// (kError_Size_To_Large_Copy_Truncated) and, unless
// kSMemNoSizeChangeOnCopyTo, the size in use grows to what was written.
long
SMemCopyToKernelGlue(TObjectId id, void* buffer, ULong size, ULong offset, TObjectId sendersMsgId, ULong signature)
{
	gCurrentTask->fRegister[2] = 0;
	TSharedMem* mem;
	NewtonErr err = ConvertMemOrMsgIdToObj(id, &mem);
	if (err != noErr)
		return err;
	if (mem->fFlags & kSMemReadOnly)
		return kError_SMem_Mode_Violation;
	TSharedMemMsg* msg;
	if ((err = CopyMessageCheck(sendersMsgId, signature, &msg)) != noErr)
		return err;

	long result = noErr;
	if (mem->fSize < size + offset)
	{
		result = kError_Size_To_Large_Copy_Truncated;
		size = mem->fSize - offset;
		if ((mem->fFlags & kSMemNoSizeChangeOnCopyTo) == 0)
			mem->fCurSize = mem->fSize;
	}
	else if ((mem->fFlags & kSMemNoSizeChangeOnCopyTo) == 0)
		mem->fCurSize = size + offset;
	if (size == 0)
		return result;
	return SetUpCopy(mem, msg, id, sendersMsgId, (char*) mem->fBuffer + offset, buffer, size, result, 0);
}


// ROM 0x001dfc44 SMemCopyFromKernelGlue
// SWI 16: copy out of the shared memory from offset into the caller's
// buffer, no more than is in use; the size copied is reported in r1.
long
SMemCopyFromKernelGlue(TObjectId id, void* buffer, ULong size, ULong offset, TObjectId sendersMsgId, ULong signature)
{
	gCurrentTask->fRegister[2] = 0;
	TSharedMem* mem;
	NewtonErr err = ConvertMemOrMsgIdToObj(id, &mem);
	if (err != noErr)
		return err;
	TSharedMemMsg* msg;
	if ((err = CopyMessageCheck(sendersMsgId, signature, &msg)) != noErr)
		return err;

	if (size != 0 && offset >= mem->fCurSize)
		return noErr;
	if (mem->fCurSize < size + offset)
		size = mem->fCurSize - offset;
	if (size == 0)
		return noErr;
	return SetUpCopy(mem, msg, id, sendersMsgId, buffer, (char*) mem->fBuffer + offset, size, noErr, size);
}


// ROM 0x001e0040 SMemMsgSetTimerParmsKernelGlue
// SWI 17.  Refused while the message is on the timer queue.
NewtonErr
SMemMsgSetTimerParmsKernelGlue(TObjectId msgId, ULong timeout, ULong delayLo, ULong delayHi)
{
	TSharedMemMsg* msg;
	NewtonErr err = ConvertIdToObj(kSharedMemMsgType, msgId, &msg);
	if (err != noErr)
		return err;
	if (msg->fTimerFlags & kSMemMsgTimer_Generic)
		return kError_Call_Already_In_Progress;
	msg->fTimeout = timeout;
	msg->fExpiryTime.hi = delayHi;
	msg->fExpiryTime.lo = delayLo;
	return noErr;
}


// ROM 0x001e00ac SMemMsgSetMsgAvailPortKernelGlue
// SWI 18.
NewtonErr
SMemMsgSetMsgAvailPortKernelGlue(TObjectId msgId, TObjectId portId)
{
	TSharedMemMsg* msg;
	NewtonErr err = ConvertIdToObj(kSharedMemMsgType, msgId, &msg);
	if (err != noErr)
		return err;
	if (portId != 0 && (err = ConvertIdToObj(kPortType, portId, nil)) != noErr)
		return err;
	msg->fMsgAvailPortId = portId;
	return noErr;
}


// ROM 0x001e0104 SMemMsgGetSenderTaskIdKernelGlue
// SWI 19: r1 = sender task.
NewtonErr
SMemMsgGetSenderTaskIdKernelGlue(TObjectId msgId)
{
	TSharedMemMsg* msg;
	NewtonErr err = ConvertIdToObj(kSharedMemMsgType, msgId, &msg);
	if (err == noErr)
		gCurrentTask->fRegister[1] = msg->fSenderTaskId;
	return err;
}


// ROM 0x001e0148 SMemMsgSetUserRefConKernelGlue
// SWI 20.
NewtonErr
SMemMsgSetUserRefConKernelGlue(TObjectId msgId, void* refCon)
{
	TSharedMemMsg* msg;
	NewtonErr err = ConvertIdToObj(kSharedMemMsgType, msgId, &msg);
	if (err == noErr)
		msg->fUserRefCon = refCon;
	return err;
}


// ROM 0x001e01c4 SMemMsgGetUserRefConKernelGlue
// SWI 21: r1 = ref con.
NewtonErr
SMemMsgGetUserRefConKernelGlue(TObjectId msgId)
{
	TSharedMemMsg* msg;
	NewtonErr err = ConvertIdToObj(kSharedMemMsgType, msgId, &msg);
	if (err == noErr)
		gCurrentTask->fRegister[1] = (TRegister) msg->fUserRefCon;
	return err;
}


// ROM 0x001e0290 SMemMsgCheckForDoneKernelGlue
// SWI 22.  Reports a message's state in r0-r4; with kSMemMsgFlags_BlockTillDone
// the caller waits for completion, with kSMemMsgFlags_Abort the call is
// aborted.  r0 is kSMemMsgStatus_InProgress while nothing has completed.
void
SMemMsgCheckForDoneKernelGlue(TObjectId msgId, ULong flags)
{
	SetResult(noErr);
	TSharedMemMsg* msg;
	NewtonErr err = ConvertIdToObj(kSharedMemMsgType, msgId, &msg);
	if (err == noErr)
	{
		gCurrentTask->fRegister[1] = msg->fSenderMsgId;
		gCurrentTask->fRegister[2] = msg->fSenderReplyMemId;
		gCurrentTask->fRegister[3] = msg->fSenderMsgFlags;
		gCurrentTask->fRegister[4] = msg->fSequenceNo;
		err = msg->fStatus;
		if (err == kSMemMsgStatus_InProgress || (msg->fMsgFlags & kSMemMsgFlags_CompleteToPortMask))
		{
			if (msg->fMsgFlags & kSMemMsgFlags_CompleteToPortMask)
			{
				// a completion that was forwarded to a port: collect it here
				long result = (err == kSMemMsgStatus_InProgress) ? noErr : err;
				msg->fNotifyId = gCurrentTask->fId;
				msg->CompleteMsg(false, msg->fMsgFlags, result);
				return;
			}
			if (flags & kSMemMsgFlags_BlockTillDone)
			{
				if (ObjectType(msg->fNotifyId) != kTaskType)
				{
					msg->fNotifyId = gCurrentTask->fId;
					UnScheduleTask(gCurrentTask);
					return;
				}
				err = kError_Another_Task_Already_Blocking;
			}
			else if (flags & kSMemMsgFlags_Abort)
			{
				msg->fNotifyId = gCurrentTask->fId;
				msg->CompleteMsg(true, 0, kError_Call_Aborted);
				return;
			}
		}
	}
	if (err != noErr)
		SetResult(err);
}


// ROM 0x001e0208 SMemMsgMsgDoneKernelGlue
// SWI 23: the receiver replies to the sender it took from the port.
NewtonErr
SMemMsgMsgDoneKernelGlue(TObjectId msgId, long result, ULong sequence)
{
	TSharedMemMsg* msg;
	NewtonErr err = ConvertIdToObj(kSharedMemMsgType, msgId, &msg);
	if (err != noErr)
		return err;
	if (msg->fStatus != kSMemMsgStatus_InProgress)
		return kError_Call_Not_In_Progress;
	if (msg->fCurrentSequence != sequence)
		return kError_Bad_Signature;
	if (msg->fPortQItem.fContainer == nil)
		return kError_MsgDone_Not_Expected;
	msg->fPortQItem.fContainer->DeleteFromQueue(msg);
	msg->CompleteSender(result);
	return noErr;
}
