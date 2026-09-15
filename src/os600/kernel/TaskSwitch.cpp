/*
	File:		TaskSwitch.cpp

	Contains:	Scheduler(), SwapInGlobals, DoDeferrals and the accounting
				system calls.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "TaskSwitch.h"
#include "Task.h"
#include "Scheduler.h"
#include "ObjectTable.h"
#include "Port.h"
#include "TimerEngine.h"
#include "KernelGlobals.h"
#include "OSErrors.h"
#include "CompMath.h"
#include "hal/Atomic.h"
#include "hal/Timer.h"


// ROM 0x001ce5c0 Scheduler
// Asks TScheduler for the next task and, if it differs from the one being
// charged, settles the outgoing task's accounts: memory, from the heap's
// running totals since the last swap, and (when gCountTaskTime is on) run
// time, as the clock advance since the last swap less the time spent in
// interrupt handlers meanwhile.  The first swap only starts the clock.
TTask*
Scheduler()
{
	TTask* task = gKernelScheduler->Schedule();

	if (gCurrentMemCountTask != task)
	{
		if (gCurrentMemCountTask != nil)
		{
			TTask* t = gCurrentMemCountTask;
			t->fPtrsUsed += gPtrsUsed - gSavedPtrsUsed;
			t->fHandlesUsed += gHandlesUsed - gSavedHandlesUsed;
			ULong used = t->fPtrsUsed + t->fHandlesUsed;
			if (used > t->fMaxMemoryUsed)
				t->fMaxMemoryUsed = used;
		}
		gSavedHandlesUsed = gHandlesUsed;
		gSavedPtrsUsed = gPtrsUsed;
		gNumberOfTaskSwaps++;
		gCurrentMemCountTask = task;
	}

	if (gCountTaskTime && gCurrentTimedTask != task)
	{
		Int64 now;
		GetClock(&now);
		gLastTaskEndTime = now;
		if (!gTaskEndTimeInvalid)
		{
			if (gCurrentTimedTask != nil)
			{
				ULong fiq = Swap(&gFIQInterruptOverHead, 0);
				ULong irq = Swap(&gIRQInterruptOverHead, 0);
				gIRQAccumulatedIntOverHead += irq;
				gFIQAccumulatedIntOverHead += fiq;
				Int64 elapsed = now;
				CompSub(&gTaskTimeStart, &elapsed);
				Int64 overhead = { 0, irq + fiq };
				CompSub(&overhead, &elapsed);
				CompAdd(&gCurrentTimedTask->fTaskTime, &elapsed);
				gCurrentTimedTask->fTaskTime = elapsed;
			}
		}
		else
		{
			gFirstTaskEndTime = now;
			gTaskEndTimeInvalid = false;
		}
		gTaskTimeStart = now;
		gCurrentTimedTask = task;
	}
	return task;
}


// ROM 0x00250214 SwapInGlobals
void
SwapInGlobals(TTask* task)
{
	gCurrentTaskId = task->fId;
	gCurrentGlobals = task->fGlobals;
	gCurrentMonitorId = task->fMonitorId;
}


// ROM 0x00149df4 DoDeferrals
// Interrupt handlers set gWantDeferred instead of doing work that needs the
// scheduler's context; the SWI exit path calls this to do it, repeating
// while more arrives.  The external page tracker's deferral is not yet
// reconstructed (gExtPageTrackerMgr).
void
DoDeferrals()
{
	EnterAtomic();
	while (gWantDeferred)
	{
		gWantDeferred = false;
		ExitAtomic();
		PortDeferredSendNotify();
		DeferredNotify();
		EnterAtomic();
	}
	ExitAtomic();
}


// ROM 0x0025010c ResetAccountTimeKernelGlue__Fv
// GenericSWI 5.
NewtonErr
ResetAccountTimeKernelGlue()
{
	TObjectTableIterator iter(gObjectTable, 0);
	EnterAtomic();
	TTask* task;
	while ((task = (TTask*) iter.GetNextTypedObject(kTaskType)) != nil)
	{
		task->fTaskTime.hi = 0;
		task->fTaskTime.lo = 0;
	}
	gTaskTimeStart.hi = 0;
	gTaskTimeStart.lo = 0;
	ExitAtomic();
	return noErr;
}


// ROM 0x00250194 GetNextTaskIdKernelGlue__FUlPUl
// GenericSWI 6: the id of the next task in object-table order after afterId
// (0 for the first); kError_Task_Does_Not_Exist when there is none or
// afterId is not in the table.
NewtonErr
GetNextTaskIdKernelGlue(TObjectId afterId, TObjectId* outId)
{
	TObjectTableIterator iter(gObjectTable, 0);
	if (!iter.SetCurrentPosition(afterId))
		return kError_Task_Does_Not_Exist;
	TObjectId id;
	while ((id = iter.GetNextTableId()) != 0)
	{
		if (ObjectType(id) == kTaskType)
		{
			*outId = id;
			return noErr;
		}
	}
	return kError_Task_Does_Not_Exist;
}
