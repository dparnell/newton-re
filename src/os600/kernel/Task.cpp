/*
	File:		Task.cpp

	Contains:	TTask construction and the task queues.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
	TTask::Init, FreeStack, ~TTask and SetBequeathId depend on the memory
	system, environments and monitors and follow with those.
*/

#include "Task.h"


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
	fMonitor = nil;
	fUnknowndc[0] = fUnknowndc[1] = fUnknowndc[2] = fUnknowndc[3] = fUnknowndc[4] = 0;
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
	fUnknownd8 = 0;
	fState = 0;
	fGlobalsBase = 0;
	fEnvironment = nil;
	fUnknown78 = 0;
	fUnknown7c = 0;
	fBequeathId = 0;
	fInheritedId = 0;
	fPriority = 0;
}
