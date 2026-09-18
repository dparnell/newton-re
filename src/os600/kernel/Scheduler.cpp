/*
	File:		Scheduler.cpp

	Contains:	TScheduler and the scheduling entry points.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	Scheduler() (0x001cc1ec), the routine the SWI/IRQ/abort handlers call
	to pick the next task, adds per-task memory and time accounting on top of
	TScheduler::Schedule and follows once the time base exists.
*/

#include "Scheduler.h"
#include "KernelGlobals.h"
#include "hal/Interrupts.h"
#include "hal/Atomic.h"


/* -------------------------------------------------------------------------------
	TScheduler
------------------------------------------------------------------------------- */

// ROM 0x001cc144 __ct__10TSchedulerFv
TScheduler::TScheduler()
{
	fCurrentBucket = 0;
	fPriorityMask = 0;
	fPreferredTask = nil;
}


// ROM 0x001cc1b0 UpdateCurrentBucket__10TSchedulerFv
// Lowers fCurrentBucket to the next non-empty bucket.  Bucket 0 is never
// tested: it is the answer when nothing higher is ready.
void
TScheduler::UpdateCurrentBucket()
{
	for (long bucket = (long) fCurrentBucket - 1; bucket > 0; bucket--)
	{
		if (fPriorityMask & (1UL << bucket))
		{
			fCurrentBucket = bucket;
			return;
		}
	}
	fCurrentBucket = 0;
}


// ROM 0x001cc564 Add__10TSchedulerFP5TTask
// Makes the task ready.  If the idle task is running, or the new task's
// priority is at least the running task's, ask for a reschedule.
void
TScheduler::Add(TTask* task)
{
	ULong priority = task->fPriority;
	fPriorityMask |= 1UL << priority;
	if (fCurrentBucket < priority)
		fCurrentBucket = priority;
	fQueue[priority].Add(task, kTaskState_Scheduled, this);
	if (gCurrentTask == gIdleTask)
		WantSchedule();
	if (gTaskPriority <= priority)
		gWantSchedulerToRun = true;
}


// ROM 0x001cc5e0 AddWhenNotCurrent__10TSchedulerFP5TTask
void
TScheduler::AddWhenNotCurrent(TTask* task)
{
	if (task == gCurrentTask)
		return;
	Add(task);
}


// ROM 0x001cc5f8 Remove__10TSchedulerFP5TTask
// Takes a task out of the ready queues.  Removing the running task just
// forgets it as current and forces a reschedule.
TTaskContainer*
TScheduler::Remove(TTask* task)
{
	if (task == nil)
		return this;
	if (gCurrentTask == task)
	{
		gCurrentTask = nil;
		WantSchedule();
		gWantSchedulerToRun = true;
		return this;
	}
	ULong priority = task->fPriority;
	fQueue[priority].RemoveFromQueue(task, kTaskState_Scheduled);
	if (fQueue[priority].Peek() == nil)
	{
		fPriorityMask &= ~(1UL << priority);
		if (fCurrentBucket == priority)
			UpdateCurrentBucket();
	}
	return this;
}


// ROM 0x001cc690 RemoveHighestPriority__10TSchedulerFv
// Dequeues the task to run next: the preferred task if there is one at the
// current priority, else the head of the highest non-empty bucket.  When a
// bucket empties the time-slice timer is stopped (Schedule/Add restart it).
TTask*
TScheduler::RemoveHighestPriority()
{
	TTask* task = fPreferredTask;
	if (task != nil)
	{
		fPreferredTask = nil;
		ULong priority = task->fPriority;
		if (fCurrentBucket == priority && fQueue[priority].RemoveFromQueue(task, kTaskState_Scheduled))
		{
			gWantSchedulerToRun = true;
			if (fQueue[priority].Peek() != nil)
				return task;
			fPriorityMask &= ~(1UL << priority);
			UpdateCurrentBucket();
			StopScheduler();
			return task;
		}
	}

	task = fQueue[fCurrentBucket].Peek();
	if (task == nil)
		return nil;
	fQueue[fCurrentBucket].RemoveFromQueue(task, kTaskState_Scheduled);
	if (fQueue[fCurrentBucket].Peek() != nil)
		return task;
	fPriorityMask &= ~(1UL << fCurrentBucket);
	UpdateCurrentBucket();
	StopScheduler();
	return task;
}


// ROM 0x001cc780 Schedule__10TSchedulerFv
// The running task (unless it is the idle task) goes back to the end of its
// bucket; the next task comes out.  Returns the task to switch to.
TTask*
TScheduler::Schedule()
{
	if (gCurrentTask != nil && gCurrentTask != gIdleTask)
		Add(gCurrentTask);
	gSchedule = false;
	TTask* task = RemoveHighestPriority();
	if (task == nil)
	{
		StopScheduler();
		task = gIdleTask;
	}
	gTaskPriority = task->fPriority;
	return task;
}


/* -------------------------------------------------------------------------------
	Entry points
------------------------------------------------------------------------------- */

// ROM 0x001918e8 ScheduleTask__FP5TTask
// (The ROM inlines TScheduler::Add here; the effect is AddWhenNotCurrent.)
void
ScheduleTask(TTask* task)
{
	gKernelScheduler->AddWhenNotCurrent(task);
}


// ROM 0x001918fc UnScheduleTask__FP5TTask
void
UnScheduleTask(TTask* task)
{
	gKernelScheduler->Remove(task);
}


// ROM 0x001cc7f4 WantSchedule__Fv
// While scheduling is held off (gHoldScheduleLevel > 0) the request is only
// noted, to be acted on when the hold is released.
void
WantSchedule()
{
	if (gHoldScheduleLevel == 0)
		gSchedule = true;
	else
		gScheduleRequested = true;
}


// ROM 0x001cc820 HoldSchedule__Fv
void
HoldSchedule()
{
	gHoldScheduleLevel++;
}


// ROM 0x001cc838 AllowSchedule__Fv
// Releasing the last hold acts on a reschedule that was requested meanwhile.
void
AllowSchedule()
{
	EnterAtomic();
	if (--gHoldScheduleLevel == 0 && gScheduleRequested)
	{
		gScheduleRequested = false;
		gSchedule = true;
	}
	ExitAtomic();
}


// ROM 0x001cc4a8 StartScheduler
// Arms the time-slice interrupt.  The ROM programs the Voyager timer
// directly: match register 0x0F182C00 = counter 0x0F181800 - 8 + 0x12000
// (0x12000 ticks of the 3.6864 MHz clock, i.e. 20 ms); the HAL does that.
void
StartScheduler()
{
	if (!gSchedulerRunning)
	{
		DisableInterrupt(gSchedulerIntObj);
		SetTimeSliceAlarm(kSchedulerTimeSlice);
		gSchedulerRunning = true;
		QuickEnableInterrupt(gSchedulerIntObj);
	}
	gWantSchedulerToRun = false;
}


// ROM 0x001cc460 PreEmptiveTimerInterruptHandler
// The slice is up: reschedule (whoever was preferred has had its chance) and
// arm the next slice.  The ROM writes match register 0x0F182C00 = counter +
// 0x11ff8 directly; the HAL does that.
void
PreEmptiveTimerInterruptHandler()
{
	WantSchedule();
	gKernelScheduler->fPreferredTask = nil;
	SetTimeSliceAlarm(kSchedulerTimeSlice);
}


// ROM 0x001cc514 StopScheduler__Fv
void
StopScheduler()
{
	if (gSchedulerRunning)
		DisableInterrupt(gSchedulerIntObj);
	gSchedulerRunning = false;
	gWantSchedulerToRun = false;
}
