// The fast timers (hal/FIQTimer.h) and the delay timer (DelayTimer.h) over
// the host's clock: a timer set runs its handler once the match interrupt
// comes (delivered here by hand, as the host's interrupt sources would),
// the shortest delay is kFIQTimerMinDelay, several timers come due in turn,
// and the busy waits wait.  No OS boot.

#include "FIQTimer.h"
#include "NewtErrors.h"
#include "NewtonTime.h"
#include "DelayTimer.h"
#include "HostInterruptSources.h"
#include "hal/Timer.h"
#include "Host.h"

#include <stdio.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static int	gFired[4];
static ULong	gArgs[4];

static void
Fire(void* refCon, ULong arg)
{
	long i = (long) (intptr_t) refCon;
	gFired[i]++;
	gArgs[i] = arg;
}


// the interrupt sources given until nothing is due, or the time runs out
static void
RunFor(ULong units)
{
	ULong start = FIQTimerCounter();
	while (FIQTimerCounter() - start < units)
	{
		Int64 now;
		GetClock(&now);
		HostDeliverInterruptSources(&now);
	}
}


int
main()
{
	HostUseRealClock(true);		// (the busy waits need a clock that moves)
	EXPECT(InitFIQTimer() == noErr);
	TFIQTimer* t = GetFIQTimerObject();
	EXPECT(t != nil && t == gFIQTimer);

	FIQTimer* a = t->AcquireFIQTimer(Fire, (void*) 0);
	FIQTimer* b = t->AcquireFIQTimer(Fire, (void*) 1);
	FIQTimer* c = t->AcquireFIQTimer(Fire, (void*) 2);
	FIQTimer* d = t->AcquireFIQTimer(Fire, (void*) 3);
	EXPECT(a && b && c && d);
	EXPECT(t->AcquireFIQTimer(Fire, (void*) 9) == nil);		// four only

	t->SetFIQTimer(a, 1, 0xa);						// (brought up to kFIQTimerMinDelay)
	EXPECT(a->fDelay == kFIQTimerMinDelay && a->fActive == 1);
	t->SetFIQTimer(b, 20 * kMilliseconds, 0xb);
	RunFor(5 * kMilliseconds);
	EXPECT(gFired[0] == 1 && gArgs[0] == 0xa && a->fActive == 0);
	EXPECT(gFired[1] == 0 && b->fActive == 1);
	RunFor(30 * kMilliseconds);
	EXPECT(gFired[1] == 1 && gArgs[1] == 0xb);

	// reset before it comes due: never runs
	t->SetFIQTimer(c, 5 * kMilliseconds, 0xc);
	t->ResetFIQTimer(c);
	RunFor(10 * kMilliseconds);
	EXPECT(gFired[2] == 0);

	t->ReleaseFIQTimer(d);
	EXPECT(d->fProc == nil);
	t->ReleaseFIQTimers((void*) 0);
	EXPECT(a->fProc == nil && b->fProc != nil);
	EXPECT(t->AcquireFIQTimer(Fire, (void*) 3) != nil);

	// the delay timer
	TDelayTimer delay;
	ULong start = delay.GetHardwareTime();
	delay.ShortTimerDelay(2 * kMilliseconds);
	EXPECT(delay.GetHardwareTime() - start >= 2 * kMilliseconds);
	EXPECT(delay.ConvertToHardwareTime(10) == 11);
	delay.ResetTimeOut(3 * kMilliseconds);
	EXPECT(!delay.TimedOut() || FIQTimerCounter() - start >= 3 * kMilliseconds);
	delay.ShortTimerDelayUntil(3 * kMilliseconds);
	EXPECT(delay.TimedOut());
	EXPECT(delay.TimedOut(1));

	printf("test_FIQTimer: %s\n", failures == 0 ? "all passed" : "FAILED");
	return failures != 0;
}
