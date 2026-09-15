/*
	File:		Semaphore.cpp

	Contains:	Kernel semaphores: TSemaphore, TSemaphoreGroup, TSemaphoreOpList
				and the system-call entry points around them.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "Semaphore.h"
#include "Scheduler.h"
#include "ObjectTable.h"
#include "KernelGlobals.h"
#include "OSErrors.h"

#include <string.h>


// The semaphore a MAKESEMLISTITEM entry names, and its delta.
static inline ULong OpSemaphore(ULong item)		{ return item >> 16; }
static inline long OpDelta(ULong item)			{ return (short) (item & 0xffff); }


/* -------------------------------------------------------------------------------
	TSemaphore
------------------------------------------------------------------------------- */

// ROM 0x001d7504 __ct__10TSemaphoreFv
TSemaphore::TSemaphore()
{
	fValue = 0;
}


// ROM 0x001d7560 __dt__10TSemaphoreFv
// Tasks still waiting are released with kError_Semaphore_Group_No_Longer_Exists.
// Their saved pc is advanced by one instruction, past the retry the SWI stub
// would otherwise perform.
TSemaphore::~TSemaphore()
{
	TTask* task;
	while ((task = fZeroTasks.Remove((KernelObjectState) kTaskState_BlockedOnSemaphore)) != nil)
	{
		task->fRegister[15] += 4;
		ScheduleTask(task);
		MarkMessageDone(task, kError_Semaphore_Group_No_Longer_Exists);
	}
	while ((task = fIncTasks.Remove((KernelObjectState) kTaskState_BlockedOnSemaphore)) != nil)
	{
		task->fRegister[15] += 4;
		ScheduleTask(task);
		MarkMessageDone(task, kError_Semaphore_Group_No_Longer_Exists);
	}
}


static Boolean
QueueHolds(TTaskQueue& queue, TTask* task)
{
	for (TTask* t = queue.Peek(); t != nil; t = t->fTaskQItem.fNext)
		if (t == task)
			return true;
	return false;
}


// ROM 0x001d7634 Remove__10TSemaphoreFP5TTask
// DEVIATION: the ROM calls fZeroTasks.RemoveFromQueue and then
// fIncTasks.RemoveFromQueue unconditionally, and TTaskQueue::RemoveFromQueue
// trusts the state bit rather than checking membership.  For a task waiting
// in fIncTasks the first call therefore unlinks it through the wrong queue,
// writing to the predecessor's link (which for the first waiter is the
// queue object itself + 0x94, i.e. past the end of the semaphore) and, for the
// last waiter, to address 0x98 - which on the Newton is read-only ROM and is
// silently dropped.  The reconstruction removes the task from the queue that
// holds it.  (The ROM returns the second call's result; it is unused.)
TTaskContainer*
TSemaphore::Remove(TTask* task)
{
	if (QueueHolds(fZeroTasks, task))
		fZeroTasks.RemoveFromQueue(task, (KernelObjectState) kTaskState_BlockedOnSemaphore);
	else if (QueueHolds(fIncTasks, task))
		fIncTasks.RemoveFromQueue(task, (KernelObjectState) kTaskState_BlockedOnSemaphore);
	return this;
}


// ROM 0x001d7668 BlockOnZero__10TSemaphoreFP5TTask8SemFlags
void
TSemaphore::BlockOnZero(TTask* task, SemFlags flags)
{
	if ((flags & kNoWaitOnBlock) == 0)
	{
		UnScheduleTask(task);
		fZeroTasks.Add(task, (KernelObjectState) kTaskState_BlockedOnSemaphore, this);
	}
}


// ROM 0x001d719c BlockOnInc__10TSemaphoreFP5TTask8SemFlags
void
TSemaphore::BlockOnInc(TTask* task, SemFlags flags)
{
	if ((flags & kNoWaitOnBlock) == 0)
	{
		UnScheduleTask(task);
		fIncTasks.Add(task, (KernelObjectState) kTaskState_BlockedOnSemaphore, this);
	}
}


// ROM 0x001d71d8 WakeTasksOnZero__10TSemaphoreFv
void
TSemaphore::WakeTasksOnZero()
{
	TTask* task = fZeroTasks.Remove((KernelObjectState) kTaskState_BlockedOnSemaphore);
	if (task == nil)
		return;
	do
	{
		ScheduleTask(task);
	} while ((task = fZeroTasks.Remove((KernelObjectState) kTaskState_BlockedOnSemaphore)) != nil);
	WantSchedule();
}


// ROM 0x001d721c WakeTasksOnInc__10TSemaphoreFv
void
TSemaphore::WakeTasksOnInc()
{
	TTask* task = fIncTasks.Remove((KernelObjectState) kTaskState_BlockedOnSemaphore);
	if (task == nil)
		return;
	do
	{
		ScheduleTask(task);
	} while ((task = fIncTasks.Remove((KernelObjectState) kTaskState_BlockedOnSemaphore)) != nil);
	WantSchedule();
}


/* -------------------------------------------------------------------------------
	TSemaphoreOpList
------------------------------------------------------------------------------- */

// ROM 0x001d747c Init__16TSemaphoreOpListFUlPUl
NewtonErr
TSemaphoreOpList::Init(ULong count, ULong* ops)
{
	fOps = new ULong[count];
	if (fOps == nil)
	{
		fCount = 0;
		return -1;
	}
	memmove(fOps, ops, count * sizeof(ULong));		// BlockMove
	fCount = count;
	return noErr;
}


// ROM 0x001d74d0 __dt__16TSemaphoreOpListFv
TSemaphoreOpList::~TSemaphoreOpList()
{
	if (fOps != nil)
		delete[] fOps;
}


/* -------------------------------------------------------------------------------
	TSemaphoreGroup
------------------------------------------------------------------------------- */

// ROM 0x001d7260 Init__15TSemaphoreGroupFUl
NewtonErr
TSemaphoreGroup::Init(ULong count)
{
	fRefCon = nil;
	fSemaphores = new TSemaphore[count];
	if (fSemaphores == nil)
	{
		fCount = 0;
		return -1;
	}
	fCount = count;
	return noErr;
}


// ROM 0x001d72ac __dt__15TSemaphoreGroupFv
TSemaphoreGroup::~TSemaphoreGroup()
{
	if (fSemaphores != nil)
		delete[] fSemaphores;
}


// ROM 0x001d72ec UnWindOp__15TSemaphoreGroupFP16TSemaphoreOpListl
// Reverses the first `count` operations of the list.
void
TSemaphoreGroup::UnWindOp(TSemaphoreOpList* list, long count)
{
	for (long i = count - 1; i >= 0; i--)
	{
		ULong item = list->fOps[i];
		if (OpSemaphore(item) <= fCount)
			fSemaphores[OpSemaphore(item)].fValue -= OpDelta(item);
	}
}


// ROM 0x001d733c SemOp__15TSemaphoreGroupFP16TSemaphoreOpList8SemFlagsP5TTask
// Applies the whole list or none of it.  Note the ROM's range check is
// `semaphore <= fCount`, one past the array; kept as found.
NewtonErr
TSemaphoreGroup::SemOp(TSemaphoreOpList* list, SemFlags flags, TTask* task)
{
	for (ULong i = 0; i < list->fCount; i++)
	{
		ULong item = list->fOps[i];
		if (OpSemaphore(item) > fCount)
			continue;
		TSemaphore* semaphore = &fSemaphores[OpSemaphore(item)];
		long delta = OpDelta(item);
		if (delta == 0)
		{
			if (semaphore->fValue != 0)
			{
				semaphore->BlockOnZero(task, flags);
				UnWindOp(list, i);
				return kError_Semaphore_Would_Cause_Block;
			}
		}
		else
		{
			long newValue = semaphore->fValue + delta;
			if (delta < 1 && newValue < 0)
			{
				semaphore->BlockOnInc(task, flags);
				UnWindOp(list, i);
				return kError_Semaphore_Would_Cause_Block;
			}
			semaphore->fValue = newValue;
		}
	}
	for (ULong i = 0; i < list->fCount; i++)
	{
		ULong item = list->fOps[i];
		if (OpSemaphore(item) > fCount)
			continue;
		TSemaphore* semaphore = &fSemaphores[OpSemaphore(item)];
		long delta = OpDelta(item);
		if (delta > 0)
			semaphore->WakeTasksOnInc();
		else if (delta < 0 && semaphore->fValue == 0)
			semaphore->WakeTasksOnZero();
	}
	return noErr;
}


/* -------------------------------------------------------------------------------
	Entry points
------------------------------------------------------------------------------- */

// ROM 0x001d70e0 MarkMessageDone__FP5TTaskl
void
MarkMessageDone(TTask* task, long result)
{
	task->fRegister[0] = result;
}


// ROM 0x001d70e8 DoSemaphoreOp
// SWI 11.  (The ROM inlines TSemaphoreGroup::SemOp here.)
NewtonErr
DoSemaphoreOp(TObjectId groupId, TObjectId listId, SemFlags flags, TTask* task)
{
	TSemaphoreGroup* group = nil;
	if (ObjectType(groupId) == kSemGroupType)
		group = (TSemaphoreGroup*) gObjectTable->Get(groupId);
	if (group == nil)
		return kError_Bad_Semaphore_GroupId;
	TSemaphoreOpList* list = nil;
	if (ObjectType(listId) == kSemListType)
		list = (TSemaphoreOpList*) gObjectTable->Get(listId);
	if (list == nil)
		return kError_Bad_Semaphore_Op_ListId;
	return group->SemOp(list, flags, task);
}


// ROM 0x000da57c SemGroupSetRefCon__FUlPv
// The ROM function serves both modes: in user mode it issues GenericSWI 40
// instead.  That path belongs to the user-side syscall layer; this is the
// kernel-mode body.
NewtonErr
SemGroupSetRefCon(TObjectId groupId, void* refCon)
{
	TSemaphoreGroup* group = nil;
	if (ObjectType(groupId) == kSemGroupType)
		group = (TSemaphoreGroup*) gObjectTable->Get(groupId);
	if (group == nil)
		return kError_Bad_Semaphore_GroupId;
	group->fRefCon = refCon;
	return noErr;
}


// ROM 0x000da600 SemGroupGetRefCon__FUlPPv
// Kernel-mode body; in user mode the ROM issues GenericSWI 41.
NewtonErr
SemGroupGetRefCon(TObjectId groupId, void** outRefCon)
{
	TSemaphoreGroup* group = nil;
	if (ObjectType(groupId) == kSemGroupType)
		group = (TSemaphoreGroup*) gObjectTable->Get(groupId);
	if (group == nil)
		return kError_Bad_Semaphore_GroupId;
	*outRefCon = group->fRefCon;
	return noErr;
}


// ROM 0x0014a490 DeleteSemList__FP16TSemaphoreOpList
void
DeleteSemList(TSemaphoreOpList* list)
{
	if (list != nil)
		delete list;
}


// ROM 0x0014a4a0 DeleteSemGroup__FP15TSemaphoreGroup
void
DeleteSemGroup(TSemaphoreGroup* group)
{
	if (group != nil)
		delete group;
}
