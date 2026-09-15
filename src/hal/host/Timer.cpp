/*
	File:		hal/host/Timer.cpp

	Contains:	A controllable clock for host builds.  The time only moves when
				the host (a test, or later a host scheduler loop) advances it,
				which keeps timer behaviour deterministic.  The alarm is recorded
				so the host can fire the timer engine when the clock passes it.
*/

#include "hal/Timer.h"
#include "CompMath.h"

static Int64	gHostClock = {0, 0};
Boolean			gHostAlarmArmed = false;
Int64			gHostAlarmTime = {0, 0};

void
HostAdvanceClock(ULong ticks)
{
	Int64 delta = {0, ticks};
	CompAdd(&delta, &gHostClock);
}

void
HostSetClock(const Int64* time)
{
	gHostClock = *time;
}

extern "C" void
GetClock(Int64* outTime)
{
	*outTime = gHostClock;
}

extern "C" Boolean
SetAlarm(const TTime* time)
{
	if (time->time.hi < 0)
		return false;
	if (CompCompare(&time->time, &gHostClock) <= 0)
		return false;
	gHostAlarmArmed = true;
	gHostAlarmTime = time->time;
	return true;
}

extern "C" void
DisableAlarm1(void)
{
	gHostAlarmArmed = false;
}
