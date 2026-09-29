// Host interrupt sources (hal/host/HostInterruptSources.h, docs/host-runtime.md):
// a source's deadline wakes the idle task and its handler is delivered at
// that time.  The kernel services task registers a fake source due 30 ms
// from now and then sleeps for 100 ms: the only thing that can make the
// idle task stop at 30 ms is the source's deadline, so the handler running
// at exactly that time (on the controllable clock) shows both that the
// deadline was folded into the idle task's wait and that the interrupt was
// delivered when it fell due.  A second source registered and removed
// again is never asked.  The handler runs at interrupt level: IsSuperMode
// answers true there, as the CPSR's mode bits would in IRQ mode, so a
// dual-mode routine (GetGlobalTime) reads the clock itself - and a system
// call made anyway is refused - instead of writing the interrupted task's
// saved registers (docs/host-runtime.md).

#include "Boot.h"
#include "CompMath.h"
#include "hal/host/Host.h"
#include "hal/host/HostInterruptSources.h"
#include "hal/Timer.h"
#include "hal/System.h"
#include "NewtonTime.h"
#include "os600/GenericSWISelectors.h"
#include "OSErrors.h"
#include "KernelGlobals.h"
#include "Task.h"
#include "UserBoot.h"
#include "UserGlobals.h"
#include "UserTasks.h"
#include "host/TaskRuntime.h"

#include <stdio.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static Boolean	gArmed = false;
static Int64	gDue = { 0, 0 };
static Int64	gDeliveredAt = { 0, 0 };
static int		gDeliveries = 0;
static int		gRemovedAsked = 0;
static Boolean	gSuperInHandler = false;
static Boolean	gSuperInTask = true;
static Int64	gHandlerTime = { 0, 0 };
static long		gHandlerSWI = 0;
static TRegister	gTaskR1Before = 0, gTaskR1After = 1;

static Boolean
FakeDeadline(Int64* when)
{
	if (!gArmed)
		return false;
	*when = gDue;
	return true;
}

static void
FakeDeliver(void)
{
	gArmed = false;						// one-shot, as a hardware interrupt is once taken
	GetClock(&gDeliveredAt);
	gDeliveries++;
	gSuperInHandler = IsSuperMode();
	gTaskR1Before = gCurrentTask != nil ? gCurrentTask->fRegister[kcR1] : 0;
	gHandlerTime = GetGlobalTime().time;						// the clock, read directly
	ULong lo = 0, hi = 0;
	gHandlerSWI = GenericWithReturnSWI(kGeneric_GetTaskTime, 0, 0, 0, &lo, &hi, nil);	// refused
	gTaskR1After = gCurrentTask != nil ? gCurrentTask->fRegister[kcR1] : 0;
}

static Boolean
RemovedDeadline(Int64* /*when*/)
{
	gRemovedAsked++;
	return false;
}

static void
RemovedDeliver(void)
{ }

static Int64	gSleptAt = { 0, 0 };
static Int64	gWokeAt = { 0, 0 };

static void
KernelServices(void)
{
	gSuperInTask = IsSuperMode();
	EXPECT(HostRegisterInterruptSource(RemovedDeadline, RemovedDeliver));
	HostUnregisterInterruptSource(RemovedDeadline, RemovedDeliver);

	EXPECT(HostRegisterInterruptSource(FakeDeadline, FakeDeliver));
	EXPECT(HostRegisterInterruptSource(FakeDeadline, FakeDeliver));	// twice is once
	GetClock(&gSleptAt);
	gDue = gSleptAt;
	Int64 delta = { 0, 30 * kMilliseconds };
	CompAdd(&delta, &gDue);
	gArmed = true;
	Sleep(100 * kMilliseconds);
	GetClock(&gWokeAt);
	HostUnregisterInterruptSource(FakeDeadline, FakeDeliver);
	HostStopTasks();
}


int
main()
{
	gHostKernelServicesTask = KernelServices;
	OsBoot();

	EXPECT(gDeliveries == 1);
	EXPECT(CompCompare(&gDeliveredAt, &gDue) == 0);		// at the deadline, not at the sleep's end
	Int64 woke = gSleptAt;
	Int64 slept = { 0, 100 * kMilliseconds };
	CompAdd(&slept, &woke);
	EXPECT(CompCompare(&gWokeAt, &woke) >= 0);			// and the sleep still ran its course
	EXPECT(gRemovedAsked == 0);
	EXPECT(gSuperInHandler && !gSuperInTask);
	EXPECT(CompCompare(&gHandlerTime, &gDeliveredAt) == 0);
	EXPECT(gHandlerSWI == kError_Call_Aborted);
	EXPECT(gTaskR1After == gTaskR1Before);				// the interrupted task's registers untouched

	if (failures == 0)
		printf("test_HostInterruptSources: all passed\n");
	return failures == 0 ? 0 : 1;
}
