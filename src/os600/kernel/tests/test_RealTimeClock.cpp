// Host unit test for TRealTimeClock (src/os600/kernel/RealTimeClock.*) over
// the host's real-time clock (src/hal/host/RealTimeClock.cpp), which follows
// the controllable system clock.  The alarms here are the handler kind: the
// message kind needs a port and a task, and is exercised by the newt world
// (SetSysAlarm) instead.

#include "RealTimeClock.h"
#include "hal/RealTimeClock.h"
#include "hal/Timer.h"
#include "hal/host/Host.h"
#include "CompMath.h"
#include "OSErrors.h"
#include "host/RomBugs.h"

#include <stdint.h>

#include <stdio.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static int fired[8];
static int numFired = 0;

static long Handler(void* data)
{
	fired[numFired++] = (int) (intptr_t) data;
	return 42;
}

// the clock moved on, and the alarm interrupt taken when it is due
static void
Advance(ULong seconds)
{
	TTime delta(seconds, kSeconds);
	HostAdvanceClock(delta.time.lo);
	if (gHostRTCAlarmArmed && GetRealTimeClock() >= gHostRTCAlarmSeconds)
	{
		gHostRTCAlarmArmed = false;			// the hardware alarm is one-shot
		TRealTimeClock::Alarm();
	}
}


int
main()
{
	Int64 start = {0, 0};
	HostSetClock(&start);
	WriteRealTimeClock(1000);
	EXPECT(GetRealTimeClock() == 1000);

	// names of our own
	EXPECT(TRealTimeClock::CheckIn('test') == noErr && TRealTimeClock::FindSlot('test') == 0);
	ULong first = 'one ', second = 'two ';
	EXPECT(TRealTimeClock::CheckIn(first) == noErr && TRealTimeClock::CheckIn(second) == noErr);
	EXPECT(TRealTimeClock::FindSlot(first) == 1 && TRealTimeClock::FindSlot(second) == 2);
	EXPECT(TRealTimeClock::FindSlot('none') == -1);

	// an alarm at an absolute second: armed in the hardware, then fired once
	EXPECT(TRealTimeClock::SetAlarm('test', 1010, Handler, (void*) 1, true, false) == noErr);
	EXPECT(gHostRTCAlarmArmed && gHostRTCAlarmSeconds == 1010);
	ULong active = 0;
	TTime when;
	long error = -1;
	EXPECT(TRealTimeClock::AlarmStatus('test', &active, &when, &error) == noErr);
	EXPECT(active != 0 && when.ConvertTo(kSeconds) == 1010 && error == noErr);
	Advance(5);
	EXPECT(numFired == 0 && gHostRTCAlarmArmed);
	Advance(5);
	EXPECT(numFired == 1 && fired[0] == 1);
	EXPECT(TRealTimeClock::AlarmStatus('test', &active, &when, &error) == noErr && active == 0 && error == 42);
	Advance(10);
	EXPECT(numFired == 1);				// it fires once

	// two alarms: the earlier one is what the hardware is set to, and each
	// fires as its own second arrives
	EXPECT(TRealTimeClock::SetAlarm(first, 1100, Handler, (void*) 2, true, false) == noErr);
	EXPECT(TRealTimeClock::SetAlarm(second, 1050, Handler, (void*) 3, true, false) == noErr);
	EXPECT(gHostRTCAlarmSeconds == 1050);
	Advance(30);						// 1050
	EXPECT(numFired == 2 && fired[1] == 3 && gHostRTCAlarmSeconds == 1100);
	Advance(50);						// 1100
	EXPECT(numFired == 3 && fired[2] == 2);

	// a relative alarm counts from now; cleared before it comes due, it does
	// not fire, and the slot's name survives
	EXPECT(TRealTimeClock::SetAlarm('test', 20, Handler, (void*) 4, true, true) == noErr);
	EXPECT(gHostRTCAlarmSeconds == GetRealTimeClock() + 20);
	EXPECT(TRealTimeClock::ClearAlarm('test') == noErr);
	Advance(30);
	EXPECT(numFired == 3 && TRealTimeClock::FindSlot('test') == 0);

	// an alarm whose second has already gone fires on the way out of SetAlarm
	EXPECT(TRealTimeClock::SetAlarm('test', GetRealTimeClock() - 1, Handler, (void*) 5, true, false) == noErr);
	EXPECT(numFired == 4 && fired[3] == 5);

	// setting the clock carries a relative alarm with it, and leaves an
	// absolute one where it was
	EXPECT(TRealTimeClock::SetAlarm(first, 60, Handler, (void*) 6, true, true) == noErr);
	ULong due = gHostRTCAlarmSeconds;
	EXPECT(TRealTimeClock::SetRealTimeClock(GetRealTimeClock() + 3600) == noErr);
	EXPECT(gHostRTCAlarmSeconds == due + 3600);
	EXPECT(TRealTimeClock::ClearAlarm(first) == noErr);

	// checking out frees the slot and disarms it
	TRealTimeClock::CheckOut('test');
	TRealTimeClock::CheckOut(first);
	TRealTimeClock::CheckOut(second);
	EXPECT(TRealTimeClock::FindSlot('test') == -1 && TRealTimeClock::FindSlot(first) == -1);
	EXPECT(TRealTimeClock::AlarmStatus('test', &active, &when, &error) == -1);

	// and a name for every slot, but no more
	for (long i = 0; i < kRealTimeAlarmCount; i++)
		EXPECT(TRealTimeClock::CheckIn('full') == noErr);
	EXPECT(TRealTimeClock::CheckIn('over') == -2);
	for (long i = 0; i < kRealTimeAlarmCount; i++)
		TRealTimeClock::CheckOut('full');

	// the names NewName hands out start at 0, and 0 is what a free slot's
	// name is - so the first one is checked into a slot that still reads as
	// free, the next CheckIn takes that slot away from it, and looking the
	// name up afterwards finds whichever slot is free instead.  The Newton
	// gets away with it because the newt world's alarm
	// (TNewtWorld::MainConstructor) is the only name ever asked for.
	// (NEWTON_ROM_BUGS=1: a ROM bug, now fixed by default)
	SetRomBugFixed(false);
	ULong handed = 99;
	EXPECT(TRealTimeClock::NewName(&handed) == noErr && handed == 0);
	EXPECT(TRealTimeClock::FindSlot(handed) == 0);
	EXPECT(TRealTimeClock::CheckIn('next') == noErr && TRealTimeClock::FindSlot('next') == 0);
	EXPECT(TRealTimeClock::FindSlot(handed) == 1);
	TRealTimeClock::CheckOut('next');
	SetRomBugFixed(true);

	// fixed: 0 is never handed out, so a name keeps its slot
	TRealTimeClock::fNextName = 0;
	ULong fixedName = 0;
	EXPECT(TRealTimeClock::NewName(&fixedName) == noErr && fixedName == 1);
	long fixedSlot = TRealTimeClock::FindSlot(fixedName);
	EXPECT(fixedSlot >= 0);
	EXPECT(TRealTimeClock::CheckIn('more') == noErr && TRealTimeClock::FindSlot('more') != fixedSlot);
	EXPECT(TRealTimeClock::FindSlot(fixedName) == fixedSlot);

	if (failures == 0)
		printf("test_RealTimeClock: all passed\n");
	return failures == 0 ? 0 : 1;
}
