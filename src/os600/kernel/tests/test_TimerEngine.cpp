// Host unit test for TTimerEngine (src/os600/kernel/TimerEngine.*) driven by
// the host HAL's controllable clock (src/hal/host/Timer.cpp).

#include "TimerEngine.h"
#include "KernelGlobals.h"
#include "CompMath.h"
#include "hal/Timer.h"
#include "hal/host/Host.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static int fired[8];
static int numFired = 0;
static void Notify(void* data)		{ fired[numFired++] = (int) (intptr_t) data; }

static TSharedMemMsg* MakeMsg()
{
	TSharedMemMsg* m = new TSharedMemMsg;
	m->Init(nil);
	return m;
}

// what the alarm interrupt would do once the clock reaches the alarm
static void RunAlarmIfDue()
{
	Int64 now;
	GetClock(&now);
	if (gHostAlarmArmed && CompCompare(&now, &gHostAlarmTime) >= 0)
	{
		gHostAlarmArmed = false;			// the hardware alarm is one-shot
		TimerInterruptHandler();
	}
}

int main()
{
	TTimerEngine engine;
	TDoubleQContainer deferred(offsetof(TSharedMemMsg, fTimerQItem));
	gTimerEngine = &engine;
	gTimerDeferred = &deferred;
	gWantDeferred = false;
	Int64 start = {0, 1000};
	HostSetClock(&start);

	// generic timers fire in time order, each once, re-arming for the next
	TSharedMemMsg* a = MakeMsg();
	TSharedMemMsg* b = MakeMsg();
	TSharedMemMsg* c = MakeMsg();
	EXPECT(engine.QueueTimer(b, 200, (void*) 2, Notify));
	EXPECT(engine.QueueTimer(a, 100, (void*) 1, Notify));
	EXPECT(engine.QueueTimer(c, 300, (void*) 3, Notify));
	EXPECT(!engine.QueueTimer(a, 50, (void*) 9, Notify));				// already queued
	EXPECT(engine.Peek() == a && engine.GetNext(a) == b && engine.GetNext(b) == c);
	EXPECT(gHostAlarmArmed && gHostAlarmTime.lo == 1100);
	EXPECT((a->fTimerFlags & kSMemMsgTimer_Generic) == kSMemMsgTimer_Generic);

	HostAdvanceClock(150);						// t = 1150: a is due, b is not
	RunAlarmIfDue();
	EXPECT(numFired == 1 && fired[0] == 1);
	EXPECT(a->fTimerFlags == 0 && engine.Peek() == b && gHostAlarmTime.lo == 1200);
	HostAdvanceClock(200);						// t = 1350: b and c both due
	RunAlarmIfDue();
	EXPECT(numFired == 3 && fired[1] == 2 && fired[2] == 3);
	EXPECT(engine.Peek() == nil && !gHostAlarmArmed && gTimerInterruptCount == 2);	// (Alarm leaves a drained queue's stale alarm alone)

	// a timer already in the past is not queued: Queue reports it as expired
	EXPECT(!engine.QueueTimer(a, 0, (void*) 4, Notify));
	EXPECT(engine.Peek() == nil && a->fTimerFlags == 0 && numFired == 3);

	// timeouts and delays defer completion to the scheduler path via gTimerDeferred
	TSharedMemMsg* t = MakeMsg();
	t->fTimeout = 500;
	EXPECT(engine.QueueTimeout(t));
	EXPECT(t->fTimerFlags == kSMemMsgTimer_Timeout && t->fExpiryTime.lo == 1350 + 500);
	TSharedMemMsg* d = MakeMsg();
	d->fExpiryTime.lo = 1600;
	EXPECT(engine.QueueDelay(d));
	EXPECT(engine.Peek() == d && engine.GetNext(d) == t);
	TSharedMemMsg* n = MakeMsg();
	n->fTimeout = kSMemMsgNoTimeout;
	EXPECT(!engine.QueueTimeout(n));
	HostAdvanceClock(300);						// t = 1650: d is due
	RunAlarmIfDue();
	EXPECT(gWantDeferred && deferred.Peek() == d && deferred.GetNext(d) == nil);
	EXPECT(d->fTimerFlags == kSMemMsgTimer_Delay);	// cleared by whoever completes it (Remove)

	// Remove: from the deferred queue, from the head (re-arms), from the middle
	engine.Remove(d);
	EXPECT(deferred.Peek() == nil && d->fTimerFlags == 0);
	TSharedMemMsg* e = MakeMsg();
	EXPECT(engine.QueueTimer(e, 1000, (void*) 5, Notify));		// behind t (1850)
	engine.Remove(t);
	EXPECT(engine.Peek() == e && gHostAlarmTime.lo == 2650 && t->fTimerFlags == 0);
	engine.Remove(e);
	EXPECT(engine.Peek() == nil && !gHostAlarmArmed);

	if (failures == 0)
		printf("test_TimerEngine: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
