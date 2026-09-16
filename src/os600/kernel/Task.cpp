/*
	File:		Task.cpp

	Contains:	TTask construction and the task queues.

	Reconstructed from the MP2100 D ROM; each function cites its origin.

	TTask::Init builds the new task's stack.  Above the stack proper sits the
	task's globals block (TaskGlobals) and above that a copy of the object the
	task was given, so that the object lives in the task's own memory; the
	copying goes through the user-side shared-memory API (TUSharedMem) because
	the memory may belong to another environment.  Stacks come from the stack
	manager (NewStack, VirtualMemory.h) once the OS is running and from the
	heap before that.
*/

#include "Task.h"
#include "KernelObjects.h"
#include "KernelGlobals.h"
#include "ObjectTable.h"
#include "Scheduler.h"
#include "SharedMem.h"
#include "Environment.h"
#include "OSErrors.h"
#include "CompMath.h"
#include "os600/TaskGlobals.h"
#include "UserSharedMem.h"
#include "VirtualMemory.h"
#include "MonitorGlue.h"
#include "NewtonMemory.h"

#include <string.h>

void (*gTaskDeletedHook)(TTask* task) = nil;


/* -------------------------------------------------------------------------------
	TTaskQItem
------------------------------------------------------------------------------- */

// ROM 0x0032e8a0 __ct__10TTaskQItemFv
TTaskQItem::TTaskQItem()
{
	fNext = nil;
	fPrev = nil;
}


/* -------------------------------------------------------------------------------
	TTaskContainer
------------------------------------------------------------------------------- */

// ROM 0x0024e958 Remove__14TTaskContainerFP5TTask
TTaskContainer*
TTaskContainer::Remove(TTask* /*task*/)
{
	return this;
}


/* -------------------------------------------------------------------------------
	TTaskQueue
------------------------------------------------------------------------------- */

// ROM 0x0032e918 __ct__10TTaskQueueFv
TTaskQueue::TTaskQueue()
{
	fHead = nil;
	fTail = nil;
}


// ROM 0x0032e94c CheckBeforeAdd__10TTaskQueueFP5TTask
// A no-op in the release ROM; presumably a debug hook.
void
TTaskQueue::CheckBeforeAdd(TTask* /*task*/)
{
}


// ROM 0x0032e950 Add__10TTaskQueueFP5TTask17KernelObjectStateP14TTaskContainer
// Appends the task, marks it with the state bit and records the container.
void
TTaskQueue::Add(TTask* task, KernelObjectState state, TTaskContainer* container)
{
	task->fTaskQItem.fNext = nil;
	CheckBeforeAdd(task);
	if (fHead == nil)
	{
		fHead = task;
		// As TDoubleQContainer::Add does, the ROM records the queue itself as
		// the first task's fPrev; nothing follows fPrev from the head.
		task->fTaskQItem.fPrev = (TTask*) this;
	}
	else
	{
		fTail->fTaskQItem.fNext = task;
		task->fTaskQItem.fPrev = fTail;
	}
	fTail = task;
	task->fState |= state;
	task->fContainer = container;
}


// ROM 0x0032e9b8 Remove__10TTaskQueueF17KernelObjectState
TTask*
TTaskQueue::Remove(KernelObjectState state)
{
	TTask* task = fHead;
	if (task != nil)
	{
		fHead = task->fTaskQItem.fNext;
		if (fHead == nil)
			fTail = nil;
		else
			fHead->fTaskQItem.fPrev = nil;
		task->fTaskQItem.fPrev = nil;
		task->fTaskQItem.fNext = nil;
		task->fState &= ~state;
		task->fContainer = nil;
	}
	return task;
}


// ROM 0x0032ea00 RemoveFromQueue__10TTaskQueueFP5TTask17KernelObjectState
// Takes the task out if it carries the state bit; true if it did.
Boolean
TTaskQueue::RemoveFromQueue(TTask* task, KernelObjectState state)
{
	if (task == nil || (task->fState & state) == 0)
		return false;

	TTask* next = task->fTaskQItem.fNext;
	TTask* prev = task->fTaskQItem.fPrev;
	if (fHead == task)
	{
		if (fTail == fHead)
		{
			fHead = nil;
			fTail = nil;
		}
		else
		{
			fHead = next;
			next->fTaskQItem.fPrev = nil;
		}
	}
	else
	{
		prev->fTaskQItem.fNext = next;
		if (fTail == task)
			fTail = prev;
		else
			next->fTaskQItem.fPrev = prev;
	}
	task->fTaskQItem.fPrev = nil;
	task->fTaskQItem.fNext = nil;
	task->fState &= ~state;
	task->fContainer = nil;
	return true;
}


// ROM 0x0032e8d4 FindAndRemove__10TTaskQueueFUl17KernelObjectState
TTask*
TTaskQueue::FindAndRemove(ULong id, KernelObjectState state)
{
	for (TTask* task = fHead; task != nil; task = task->fTaskQItem.fNext)
	{
		if (task->fUnknown9c == id)
		{
			RemoveFromQueue(task, state);
			return task;
		}
	}
	return nil;
}


// ROM 0x0032ea84 Peek__10TTaskQueueFv
TTask*
TTaskQueue::Peek()
{
	return fHead;
}


/* -------------------------------------------------------------------------------
	TTask
------------------------------------------------------------------------------- */

// ROM 0x00250248 __ct__5TTaskFv
// The queue items construct themselves; the remaining members the ROM
// clears are listed in its order.
TTask::TTask()
{
	fInsideMonitorId = 0;
	fCopySavedPC = 0;
	fCopyResult = 0;
	fCopySize = 0;
	fCopyMemId = 0;
	fCopyMsgId = 0;
	fSharedMemId = 0;
	fSharedMemMsgId = 0;
	fGlobals = nil;
	fTaskTime.hi = 0;
	fTaskTime.lo = 0;
	fStackSize = 0;
	fPtrsUsed = 0;
	fHandlesUsed = 0;
	fMaxMemoryUsed = 0;
	fStackBase = 0;
	fMonitorId = 0;
	fState = 0;
	fGlobalsBase = 0;
	fEnvironment = nil;
	fCopyEnvironment = nil;
	fMonitorCaller = nil;
	fBequeathId = 0;
	fInheritedId = 0;
	fPriority = 0;
}


// ROM 0x00250330 SetBequeathId__5TTaskFUl
// Our objects go to task `id` when we die (DeleteTask), and it remembers
// whom it inherits from.
void
TTask::SetBequeathId(TObjectId id)
{
	fBequeathId = id;
	TTask* heir;
	if (ConvertIdToObj(kTaskType, id, &heir) == noErr)
		heir->fInheritedId = fId;
}


// ROM 0x00250368 Init__5TTaskFPFPvUlT2_vUlPvN32P12TEnvironment
// taskId is the task's own id (passed to proc as its third argument, so it
// arrives as a void*); dataId a shared memory holding the object to copy.
// The stack allocation is stack | TaskGlobals | object; sp starts at the
// globals block, r0 points at the object, r1 is its size, r2 the id.
NewtonErr
TTask::Init(TaskProcPtr proc, ULong stackSize, void* taskId, TObjectId dataId, ULong priority, ULong name, TEnvironment* environment)
{
	TUSharedMem data(dataId);
	ULong dataSize = 0;
	NewtonErr err;
	if (dataId != 0 && (err = data.GetSize(&dataSize, nil)) != noErr)
		return err;
	fRegister[2] = (TRegister) taskId;
	fRegister[1] = dataSize;
	dataSize = (dataSize + 3) & ~3;
	stackSize = (stackSize + 3) & ~3;
	ULong topSize = dataSize + kTaskGlobalsSize;

	if (!gOSIsRunning)
	{
		fState &= ~kTaskState_StackFromNewStack;
		// the ROM's malloc (0x001e5068) is NewPtr: the kernel heap
		fStackBase = (VAddr) NewPtr(stackSize + topSize);
		if (fStackBase == 0)
			return kError_Could_Not_Create_Object;
		fGlobalsBase = fStackBase + stackSize;
		fStackTop = fGlobalsBase + topSize;
	}
	else
	{
		fState |= kTaskState_StackFromNewStack;
		err = NewStack(environment->fStackDomainId, stackSize + topSize, fId, &fStackTop, &fStackBase);
		if (err != noErr)
		{
			fStackBase = 0;
			return err;
		}
		fGlobalsBase = fStackTop - topSize;
		if ((err = LockHeapRange(fGlobalsBase, fStackTop, false)) != noErr)
			return err;
		if ((err = LockHeapRange(fGlobalsBase, fGlobalsBase + 0x30, false)) != noErr)
			return err;
	}

	// the task's own shared memory and message
	TSharedMem* mem = new TSharedMem;
	if (mem != nil && mem->Init(environment) != noErr)
	{
		delete mem;
		mem = nil;
	}
	RegisterObject(mem, kSharedMemType, fId, &fSharedMemId);
	TSharedMemMsg* msg = new TSharedMemMsg;
	if (msg != nil && msg->Init(environment) != noErr)
	{
		delete msg;
		msg = nil;
	}
	RegisterObject(msg, kSharedMemMsgType, fId, &fSharedMemMsgId);
	if (fSharedMemId == 0 || fSharedMemMsgId == 0)
		return kError_Could_Not_Create_Object;

	// the globals block and the object, copied in through the shared memory
	fGlobals = (void*) (fGlobalsBase + kTaskGlobalsSize);
	TaskGlobals globals;
	memset(&globals, 0, sizeof(globals));
	InitializeExceptionGlobals(&globals.fExceptionGlobals);
	globals.fTaskId = fId;
	globals.fUnknown30 = 0;
	globals.fMemError = noErr;
	globals.fTaskName = name;
	globals.fStackTop = fGlobalsBase + topSize;
	globals.fStackBase = fStackBase;
	globals.fCurrentHeap = environment->fHeap;
	globals.fHeapDomainId = environment->fHeapDomainId;
	TUSharedMem stack(fSharedMemId);
	if ((err = stack.SetBuffer((void*) fGlobalsBase, topSize, kSMemReadWrite)) != noErr)
		return err;
	if ((err = stack.CopyToShared(&globals, kTaskGlobalsSize, 0, nil)) != noErr)
		return err;
	VAddr end = fGlobalsBase + topSize;
	for (VAddr addr = end - dataSize; addr < end; addr += 0x100)
	{
		char chunk[0x100];
		ULong size = end - addr;
		if (size > sizeof(chunk))
			size = sizeof(chunk);
		ULong offset = addr - fGlobalsBase;
		ULong got;
		if ((err = data.CopyFromShared(&got, chunk, size, offset - kTaskGlobalsSize, nil)) != noErr)
			return err;
		if ((err = stack.CopyToShared(chunk, size, offset, nil)) != noErr)
			return err;
	}
	if (fState & kTaskState_StackFromNewStack)
		UnlockHeapRange(fGlobalsBase, fStackTop);

	fStackSize = fStackTop - fGlobalsBase;
	fUnknown68 = fId;
	fRegister[13] = fGlobalsBase;
	fRegister[0] = (TRegister) fGlobals;
	fContainer = nil;
	fPSR = kUserMode;
	fRegister[15] = (TRegister) proc;
	fRegister[10] = 0;
	fRegister[11] = 0;
	fRegister[12] = 0;
	fRegister[4] = 0;
	fRegister[6] = 0;
	fRegister[7] = 0;
	fRegister[8] = 0;
	fRegister[9] = 0;
	fRegister[14] = (TRegister) BadExit;
	fRegister[5] = (TRegister) this;
	fPriority = priority;
	fEnvironment = environment;
	fName = name;
	environment->IncrRefCount();
	return noErr;
}


// ROM 0x00250308 FreeStack__5TTaskFv
// A stack from the stack manager goes back to it (the ROM asks the stack
// manager's monitor directly, request 4: FreePagedMem); one from the heap is
// freed.
void
TTask::FreeStack()
{
	if (fStackBase == 0)
		return;
	if (fState & kTaskState_StackFromNewStack)
		FreePagedMem(fStackBase);
	else
		DisposPtr((Ptr) fStackBase);		// the ROM's free (0x001e506c)
}


// ROM 0x0025079c __dt__5TTaskFv
// Leaves whatever queues the task is in, returns its stack, adds its run
// time to the dead tasks' total, lets go of its environment (deleting it if
// that was its last user) and removes its shared memory and message.
TTask::~TTask()
{
	if (fMonitorQItem.fContainer != nil)
		fMonitorQItem.fContainer->DeleteFromQueue(this);
	if (fCopyQItem.fContainer != nil)
		fCopyQItem.fContainer->DeleteFromQueue(this);
	UnScheduleTask(this);
	FreeStack();
	CompAdd(&fTaskTime, &gDeadTaskTime);
	if (fEnvironment != nil && fEnvironment->DecrRefCount())
		delete fEnvironment;
	gObjectTable->Remove(fSharedMemId);
	gObjectTable->Remove(fSharedMemMsgId);
	if (gTaskDeletedHook != nil)
		gTaskDeletedHook(this);
}
