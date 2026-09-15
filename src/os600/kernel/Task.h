/*
	File:		Task.h

	Contains:	TTask, the kernel's task control block, and the queues tasks
				are kept in (TTaskQItem, TTaskQueue, TTaskContainer).

				A task is a kernel object (kTaskType) holding the saved
				register set, the stack and globals area, the environment the
				task runs in, its priority, and the links by which it sits in a
				scheduler bucket or waits on a port, semaphore or monitor.

				The layout is that of the ROM (0x104 bytes); members whose use
				has not been established yet are named by their offset.  The
				DDK does not declare this class - only TUTask, the user-side
				handle (UserTasks.h).

	Reconstructed from:	TTask::TTask 0x00250248, TTask::Init 0x00250368,
				TTask::~TTask 0x0025079c, TTaskQueue 0x0032e8a0-0x0032ea90,
				TScheduler 0x001ce518-0x001cebc4, Scheduler 0x001ce5c0
*/

#ifndef __TASK_H
#define __TASK_H

#ifndef __KERNELOBJECT_H
#include "KernelObject.h"
#endif
#ifndef __DOUBLEQ_H
#include "DoubleQ.h"
#endif

#include <stdint.h>

// A machine register as saved in a task.  The ROM's are 32 bits; a host
// build widens them to its pointer size so that a pointer or a code address
// passed through a register survives (docs/host-runtime.md).  Kernel code
// that reads a register as an id or a selector casts it to ULong.
typedef uintptr_t TRegister;

class TTask;
class TTaskQueue;
class TEnvironment;

// Bits of TTask::fState.  The enum is kernel-private (not in the DDK); the
// values are the ones the ROM code uses and are named by what the code does
// with them.
enum KernelObjectState
{
	kTaskState_Scheduled		= 0x00020000,	// in a TScheduler bucket, ready to run (TTaskQueue::Add from TScheduler)
	kTaskState_KillPending		= 0x00400000,	// ObjectScavenger: task removal deferred while fInsideMonitorId != 0;
												// a monitor caller with it set resumes in TaskKillSelf (TMonitor::Release)
	kTaskState_FaultMonitorCall	= 0x00800000,	// the pending monitor call is a fault (set by the abort handler, cleared
												// by MonitorDispatchKernelGlue): the caller's registers are the message
	kTaskState_KilledSelf		= 0x00000002,	// the task asked the object manager to delete it (TaskKillSelf);
												// TMonitor::Release never resumes it, the next request removes it
	kTaskState_Unknown0008		= 0x00000008,	// ObjectSuspend refuses a task without it (kError_Cannot_Suspend_Blocked_Task);
												// nothing found yet that sets it
	kTaskState_StackFromNewStack= 0x02000000	// stack came from NewStack (freed via the stack manager), not malloc
};

const ULong kNumberOfPriorities = 32;			// priorities 0-31 select a TScheduler bucket


// ROM size 0x08 - the links by which a task hangs in a TTaskQueue
class TTaskQItem
{
	public:
						TTaskQItem();

		TTask*			fNext;		// +0x00
		TTask*			fPrev;		// +0x04
};


// Something tasks can be queued in and asked to leave: the scheduler and
// (later) ports, semaphores and monitors override Remove.
class TTaskContainer
{
	public:
		virtual TTaskContainer*	Remove(TTask* task);	// ROM: base does nothing
};


// ROM size 0x08 - a FIFO of tasks linked through their fTaskQItem
class TTaskQueue
{
	public:
						TTaskQueue();

		void			CheckBeforeAdd(TTask* task);
		void			Add(TTask* task, KernelObjectState state, TTaskContainer* container);
		TTask*			Remove(KernelObjectState state);				// pop the head
		Boolean			RemoveFromQueue(TTask* task, KernelObjectState state);
		TTask*			FindAndRemove(ULong id, KernelObjectState state);
		TTask*			Peek();

	private:
		TTask*			fHead;		// +0x00
		TTask*			fTail;		// +0x04
};


// ROM size 0x104
class TTask : public TKernelObject
{
	public:
						TTask();
						~TTask();

		// TTask::Init 0x00250368 and FreeStack 0x00250308 need the memory
		// system (NewStack, LockHeapRange, TSharedMem); they follow with it.

		void			SetBequeathId(TObjectId id);

		TRegister		fRegister[16];		// +0x10  r0-r15 as saved on a context switch; r13 = stack pointer,
											//        r14 = BadExit, r15 = task entry point when the task is new
		ULong			fPSR;				// +0x50  0x10 (user mode) for a new task
		ULong			fUnknown54[5];		// +0x54
		TObjectId		fUnknown68;			// +0x68  set to fId by Init
		ULong			fState;				// +0x6c  KernelObjectState bits
		ULong			fUnknown70;			// +0x70
		TEnvironment*	fEnvironment;		// +0x74  the environment (domains) the task runs in
		TEnvironment*	fCopyEnvironment;	// +0x78  environment a shared-memory copy switches to (SMemCopyTo/From)
		TTask*			fMonitorCaller;		// +0x7c  monitor task: the caller it is currently serving (TMonitor::SetUpEntry)
		ULong			fPriority;			// +0x80  0..kNumberOfPriorities-1
		ULong			fName;				// +0x84  four-character name, e.g. 'UNAM'
		VAddr			fStackTop;			// +0x88  end of the stack allocation
		VAddr			fStackBase;			// +0x8c  start of the allocation (nil when there is no stack)
		TTaskContainer*	fContainer;			// +0x90  what the task is queued in (see TTaskQueue::Add)
		TTaskQItem		fTaskQItem;			// +0x94  links in that queue
		ULong			fUnknown9c;			// +0x9c  matched by TTaskQueue::FindAndRemove
		void*			fGlobals;			// +0xa0  the task's globals area (top of stack + 0x54)
		Int64			fTaskTime;			// +0xa4  accumulated run time (Scheduler)
		ULong			fStackSize;			// +0xac  fStackTop - fGlobalsBase
		ULong			fPtrsUsed;			// +0xb0  memory accounting (Scheduler)
		ULong			fHandlesUsed;		// +0xb4
		ULong			fMaxMemoryUsed;		// +0xb8
		TDoubleQItem	fCopyQItem;			// +0xbc  link in gCopyTasks while doing a shared-memory copy
		TDoubleQItem	fMonitorQItem;		// +0xc8  link in a TMonitor's queue of callers (+0xd0 = that queue)
		TObjectId		fInsideMonitorId;	// +0xd4  the monitor this task is calling, while inside it (ObjectScavenger defers its death)
		TObjectId		fMonitorId;			// +0xd8  the monitor this task serves, if it is a monitor task
		TRegister		fCopySavedPC;		// +0xdc  a shared-memory copy in progress: pc to resume at,
		long			fCopyResult;		// +0xe0  result to report (kError_Size_To_Large_Copy_Truncated or 0),
		ULong			fCopySize;			// +0xe4  size to report in r1,
		TObjectId		fCopyMemId;			// +0xe8  the TSharedMem being copied,
		TObjectId		fCopyMsgId;			// +0xec  the message it belongs to (fCopyingTaskId points back)
		TObjectId		fSharedMemId;		// +0xf0  TSharedMem the task's stack/globals are exposed through
		TObjectId		fSharedMemMsgId;	// +0xf4  TSharedMemMsg registered for the task
		VAddr			fGlobalsBase;		// +0xf8  initial stack pointer / start of the 0x54-byte globals block
		TObjectId		fBequeathId;		// +0xfc  task to hand our objects to when we die
		TObjectId		fInheritedId;		// +0x100 task that bequeathed to us (SetBequeathId on the other task)
};

#endif	/* __TASK_H */
