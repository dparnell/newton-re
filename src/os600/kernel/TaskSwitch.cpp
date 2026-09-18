/*
	File:		TaskSwitch.cpp

	Contains:	Scheduler(), SwapInGlobals, DoDeferrals and the accounting
				system calls.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
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
#include "hal/MMU.h"
#include "Environment.h"


// ROM 0x001cc1ec Scheduler
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


// ROM 0x003ad698 SWIBoot +0xb8 (the common SWI exit, assembly)
// Nothing is scheduled from inside an atomic section.  Otherwise the work
// interrupt level deferred is done, and if a reschedule is due Scheduler()
// picks; the time slice is armed if StartScheduler was asked for (twice in
// the ROM when the pick is the same task - harmless).  The pick becomes
// gCurrentTask; the caller switches to it if it is not the task that was
// running, loading DomainAccessFor(pick) into the MMU on the way.
TTask*
SWIExitSchedule()
{
	if (!InAtomicSection())
	{
		if (gWantDeferred)
			DoDeferrals();
		if (gSchedule)
		{
			TTask* next = Scheduler();
			if (gWantSchedulerToRun)
				StartScheduler();
			gCurrentTask = next;
		}
	}
	if (gWantSchedulerToRun)
		StartScheduler();
	return gCurrentTask;
}


// ROM 0x003ad698 SWIBoot +0x364 (the exit path's domain access computation, assembly)
// A task may touch its environment's domains, those of the environment a
// shared-memory copy switched it to, and - for a monitor task - those of
// every task in the chain of callers it is serving.
ULong
DomainAccessFor(TTask* task)
{
	ULong access = 0;
	if (task->fEnvironment != nil)
		access |= task->fEnvironment->fDomainAccess;
	if (task->fCopyEnvironment != nil)
		access |= task->fCopyEnvironment->fDomainAccess;
	for (TTask* caller = task->fMonitorCaller; caller != nil && caller->fEnvironment != nil; caller = caller->fMonitorCaller)
		access |= caller->fEnvironment->fDomainAccess;
	return access;
}


// ROM 0x0025215c SwapInGlobals
void
SwapInGlobals(TTask* task)
{
	gCurrentTaskId = task->fId;
	gCurrentGlobals = task->fGlobals;
	gCurrentMonitorId = task->fMonitorId;
}


// ROM 0x00148298 DoDeferrals
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


// ROM 0x00252054 ResetAccountTimeKernelGlue__Fv
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


// ROM 0x002520dc GetNextTaskIdKernelGlue__FUlPUl
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
