// Host unit test for TTaskQueue and TScheduler (src/os600/kernel/Task.*, Scheduler.*).

#include "Scheduler.h"
#include "KernelGlobals.h"
#include "hal/Interrupts.h"

#include <stdio.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

extern Boolean gHostInterruptEnabled;
extern ULong gHostTimeSliceAlarm;

static TTask* MakeTask(ULong priority)
{
	TTask* t = new TTask;
	t->fPriority = priority;
	return t;
}

int main()
{
	// --- TTaskQueue -------------------------------------------------------
	TTaskQueue q;
	TTask* a = MakeTask(5);
	TTask* b = MakeTask(5);
	TTask* c = MakeTask(5);
	TTaskContainer container;
	EXPECT(q.Peek() == nil && q.Remove(kTaskState_Scheduled) == nil);
	q.Add(a, kTaskState_Scheduled, &container);
	q.Add(b, kTaskState_Scheduled, &container);
	q.Add(c, kTaskState_Scheduled, &container);
	EXPECT(q.Peek() == a);
	EXPECT((a->fState & kTaskState_Scheduled) && a->fContainer == &container);
	EXPECT(!q.RemoveFromQueue(a, kTaskState_KillPending));	// wrong state bit: not ours
	EXPECT(q.RemoveFromQueue(b, kTaskState_Scheduled));	// middle
	EXPECT(b->fState == 0 && b->fContainer == nil);
	EXPECT(q.Remove(kTaskState_Scheduled) == a);			// head
	EXPECT(q.Peek() == c);
	EXPECT(q.RemoveFromQueue(c, kTaskState_Scheduled));	// last
	EXPECT(q.Peek() == nil);
	c->fUnknown9c = 0x1234;
	q.Add(a, kTaskState_Scheduled, nil);
	q.Add(c, kTaskState_Scheduled, nil);
	EXPECT(q.FindAndRemove(0x1234, kTaskState_Scheduled) == c && q.Peek() == a && q.Remove(kTaskState_Scheduled) == a);

	// --- TScheduler -------------------------------------------------------
	TScheduler scheduler;
	TTask* idle = MakeTask(0);
	gKernelScheduler = &scheduler;
	gIdleTask = idle;
	gCurrentTask = idle;
	gTaskPriority = 0;
	gSchedule = gScheduleRequested = gWantSchedulerToRun = gSchedulerRunning = false;
	gHoldScheduleLevel = 0;

	// nothing ready: the idle task runs
	EXPECT(scheduler.Schedule() == idle && gTaskPriority == 0);

	// adding a task while idle asks for a reschedule
	TTask* lo = MakeTask(3);
	TTask* hi = MakeTask(10);
	TTask* hi2 = MakeTask(10);
	scheduler.Add(lo);
	EXPECT(gSchedule && gWantSchedulerToRun);
	gSchedule = false;

	// highest priority first; equal priorities round-robin; the running task
	// goes back to the end of its bucket
	scheduler.Add(hi);
	scheduler.Add(hi2);
	TTask* t = scheduler.Schedule();
	EXPECT(t == hi && gTaskPriority == 10);
	gCurrentTask = t;
	t = scheduler.Schedule();
	EXPECT(t == hi2);
	gCurrentTask = t;
	t = scheduler.Schedule();
	EXPECT(t == hi);						// hi2 was re-queued behind hi
	gCurrentTask = t;

	// remove both high tasks: the low one gets its turn, then idle
	scheduler.Remove(hi2);
	scheduler.Remove(hi);					// the running task: forgotten as current
	EXPECT(gCurrentTask == nil && gSchedule);
	t = scheduler.Schedule();
	EXPECT(t == lo && gTaskPriority == 3);
	gCurrentTask = t;
	scheduler.Remove(lo);
	EXPECT(gCurrentTask == nil);
	t = scheduler.Schedule();
	EXPECT(t == idle && gTaskPriority == 0 && !gSchedulerRunning);
	gCurrentTask = idle;

	// ScheduleTask / UnScheduleTask go through gKernelScheduler; the current task is not re-added
	ScheduleTask(lo);
	ScheduleTask(idle);
	EXPECT(scheduler.Schedule() == lo);
	gCurrentTask = lo;
	UnScheduleTask(lo);
	EXPECT(gCurrentTask == nil);

	// WantSchedule respects the hold level
	gSchedule = gScheduleRequested = false;
	gHoldScheduleLevel = 1;
	WantSchedule();
	EXPECT(!gSchedule && gScheduleRequested);
	gHoldScheduleLevel = 0;
	WantSchedule();
	EXPECT(gSchedule);

	// the time-slice timer is armed once and stopped
	gWantSchedulerToRun = true;
	StartScheduler();
	EXPECT(gSchedulerRunning && gHostInterruptEnabled && gHostTimeSliceAlarm == kSchedulerTimeSlice && !gWantSchedulerToRun);
	StopScheduler();
	EXPECT(!gSchedulerRunning && !gHostInterruptEnabled);

	if (failures == 0)
		printf("test_Scheduler: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
