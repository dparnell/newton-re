// Host unit test for the task-switch cluster (src/os600/kernel/TaskSwitch.*):
// Scheduler()'s choice and accounting, SwapInGlobals, DoDeferrals and the
// accounting system calls.  The host clock is driven by hand.

#include "TaskSwitch.h"
#include "Task.h"
#include "Scheduler.h"
#include "ObjectTable.h"
#include "SharedMem.h"
#include "KernelGlobals.h"
#include "OSErrors.h"
#include "hal/host/Host.h"
#include "hal/Timer.h"

#include <stddef.h>
#include <stdio.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static TObjectTable table;
static TScheduler scheduler;
static TDoubleQContainer timerDeferred(offsetof(TSharedMemMsg, fTimerQItem));
static TDoubleQContainer deferredSends(offsetof(TSharedMemMsg, fTimerQItem));

// Scheduler() picks; the SWI exit path (assembly) makes the pick current
static TTask* Switch()
{
	TTask* t = Scheduler();
	gCurrentTask = t;
	return t;
}

static TTask* MakeTask(ULong priority)
{
	TTask* t = new TTask;
	t->fPriority = priority;
	table.Add(t, kTaskType, 1);
	return t;
}

int main()
{
	table.Init();
	gObjectTable = &table;
	gKernelScheduler = &scheduler;
	gTimerDeferred = &timerDeferred;
	gDeferredSends = &deferredSends;
	Int64 t0 = { 0, 1000 };
	HostSetClock(&t0);

	TTask* idle = MakeTask(0);
	gIdleTask = idle;
	TTask* a = MakeTask(10);
	TTask* b = MakeTask(10);
	a->fGlobals = (void*) a;
	a->fMonitorId = 0x1230 | kMonitorType;

	// --- with nothing runnable the idle task runs; no accounting until asked ---
	EXPECT(Switch() == idle);
	EXPECT(gCurrentMemCountTask == idle && gNumberOfTaskSwaps == 1);
	EXPECT(gCurrentTimedTask == nil && gTaskEndTimeInvalid);			// gCountTaskTime is off
	SwapInGlobals(a);
	EXPECT(gCurrentTaskId == a->fId && gCurrentGlobals == (void*) a && gCurrentMonitorId == a->fMonitorId);

	// --- memory accounting: heap growth since the last swap is charged to the outgoing task
	ScheduleTask(a);
	gPtrsUsed = 100;
	gHandlesUsed = 50;
	EXPECT(Switch() == a && gNumberOfTaskSwaps == 2);
	EXPECT(idle->fPtrsUsed == 100 && idle->fHandlesUsed == 50 && idle->fMaxMemoryUsed == 150);
	EXPECT(gSavedPtrsUsed == 100 && gSavedHandlesUsed == 50);
	gPtrsUsed = 40;														// freed some
	ScheduleTask(b);
	EXPECT(Switch() == b && gNumberOfTaskSwaps == 3);
	EXPECT(a->fPtrsUsed == (ULong) -60 && a->fHandlesUsed == 0);
	EXPECT(a->fMaxMemoryUsed == (ULong) -60);							// the ROM compares unsigned: a net free looks huge
	EXPECT(Switch() == a && Switch() == b);						// round robin at the same priority
	ULong swaps = gNumberOfTaskSwaps;
	UnScheduleTask(a);
	EXPECT(Switch() == b && gNumberOfTaskSwaps == swaps);			// same task again: nothing charged

	// --- time accounting: the first timed swap only starts the clock ---------
	gCountTaskTime = true;
	HostAdvanceClock(500);
	ScheduleTask(a);
	EXPECT(Switch() == a);
	EXPECT(!gTaskEndTimeInvalid && gCurrentTimedTask == a && gTaskTimeStart.lo == 1500 && gFirstTaskEndTime.lo == 1500);
	EXPECT(a->fTaskTime.lo == 0 && b->fTaskTime.lo == 0);
	HostAdvanceClock(300);
	gIRQInterruptOverHead = 20;											// spent in handlers meanwhile
	gFIQInterruptOverHead = 5;
	EXPECT(Switch() == b);
	EXPECT(a->fTaskTime.lo == 275 && a->fTaskTime.hi == 0);				// 300 less the 25 of overhead
	EXPECT(gIRQInterruptOverHead == 0 && gFIQInterruptOverHead == 0);
	EXPECT(gIRQAccumulatedIntOverHead == 20 && gFIQAccumulatedIntOverHead == 5);
	EXPECT(gTaskTimeStart.lo == 1800 && gLastTaskEndTime.lo == 1800 && gCurrentTimedTask == b);
	HostAdvanceClock(100);
	EXPECT(Switch() == a && b->fTaskTime.lo == 100);
	HostAdvanceClock(50);
	EXPECT(Switch() == b && a->fTaskTime.lo == 325);

	// --- the accounting system calls --------------------------------------------
	EXPECT(ResetAccountTimeKernelGlue() == noErr);
	EXPECT(a->fTaskTime.lo == 0 && b->fTaskTime.lo == 0 && gTaskTimeStart.lo == 0 && gTaskTimeStart.hi == 0);
	TObjectId id = 0;
	EXPECT(GetNextTaskIdKernelGlue(0, &id) == noErr && id == idle->fId);
	EXPECT(GetNextTaskIdKernelGlue(idle->fId, &id) == noErr && id == a->fId);
	EXPECT(GetNextTaskIdKernelGlue(a->fId, &id) == noErr && id == b->fId);
	EXPECT(GetNextTaskIdKernelGlue(b->fId, &id) == kError_Task_Does_Not_Exist);
	EXPECT(GetNextTaskIdKernelGlue(0x9990 | kTaskType, &id) == kError_Task_Does_Not_Exist);
	TSharedMemMsg* msg = new TSharedMemMsg;
	msg->Init(nil);
	table.Add(msg, kSharedMemMsgType, a->fId);
	EXPECT(GetNextTaskIdKernelGlue(b->fId, &id) == kError_Task_Does_Not_Exist);	// a message is not a task

	// --- deferrals ----------------------------------------------------------------------
	gWantDeferred = true;
	DoDeferrals();
	EXPECT(!gWantDeferred);

	if (failures == 0)
		printf("test_TaskSwitch: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
