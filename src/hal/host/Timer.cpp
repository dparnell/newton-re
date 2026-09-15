/*
	File:		hal/host/Timer.cpp

	Contains:	The clock and alarm for host builds.  By default the clock only
				moves when the host advances it (tests; the task runtime's idle
				task jumps it to the next deadline), which keeps timer behaviour
				deterministic.  HostUseRealClock(true) reads the host's steady
				clock instead, scaled to the Newton's 3.6864 MHz.  The alarm is
				recorded so the host can fire the timer engine when the clock
				passes it.
*/

#include <chrono>				// before the DDK headers, whose macros upset libc++
#include <thread>

#include "hal/Timer.h"
#include "hal/host/Host.h"
#include "CompMath.h"

static Int64	gHostClock = {0, 0};
static Boolean	gHostRealClock = false;
static std::chrono::steady_clock::time_point gHostClockStart;
Boolean			gHostAlarmArmed = false;
Int64			gHostAlarmTime = {0, 0};

// 3.6864 MHz: 3686.4 ticks per millisecond, i.e. 4608 ticks per 1.25 ms
static Int64
HostSteadyClock()
{
	auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - gHostClockStart).count();
	unsigned long long ticks = (unsigned long long) ns * 4608 / 1250000;
	Int64 t = { (SLong) (ticks >> 32), (ULong) ticks };
	return t;
}

void
HostUseRealClock(Boolean real)
{
	gHostRealClock = real;
	gHostClockStart = std::chrono::steady_clock::now();
}

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

void
HostSleepUntil(const Int64* time)
{
	if (!gHostRealClock)
	{
		if (CompCompare(time, &gHostClock) > 0)
			gHostClock = *time;
		return;
	}
	for (;;)
	{
		Int64 now = HostSteadyClock();
		if (CompCompare(&now, time) >= 0)
			return;
		Int64 left = *time;
		CompSub(&now, &left);
		unsigned long long ticks = ((unsigned long long) (ULong) left.hi << 32) | left.lo;
		std::this_thread::sleep_for(std::chrono::nanoseconds(ticks * 1250000 / 4608));
	}
}

extern "C" void
GetClock(Int64* outTime)
{
	*outTime = gHostRealClock ? HostSteadyClock() : gHostClock;
}

extern "C" Boolean
SetAlarm(const TTime* time)
{
	Int64 now;
	GetClock(&now);
	if (time->time.hi < 0)
		return false;
	if (CompCompare(&time->time, &now) <= 0)
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

// the host clock is 64 bits wide: nothing to catch up on
extern "C" void
UpdateClock(void)
{
}
