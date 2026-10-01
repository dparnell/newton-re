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

// The host's sleep, to the nanosecond asked.  On Windows a plain sleep
// (Sleep, and std::this_thread::sleep_for over it) ends on the system's
// timer tick - 15.6 ms unless some program has asked for finer - so every
// wait the idle task made for the next Newton timer (a pen sample a tick,
// the inker's, a delayed action's millisecond) was rounded up to it, and
// a soak round on Windows took six times as long as on Linux on the same
// steps.  A high-resolution waitable timer (Windows 10 1803 and later)
// sleeps to the asked time without changing the whole system's tick as
// timeBeginPeriod would; one per thread that sleeps.
#ifdef _WIN32
extern "C" {
__declspec(dllimport) void* __stdcall CreateWaitableTimerExW(void* attributes, const wchar_t* name, unsigned long flags, unsigned long access);
__declspec(dllimport) int __stdcall SetWaitableTimer(void* timer, const long long* due, long period, void* routine, void* arg, int resume);
__declspec(dllimport) unsigned long __stdcall WaitForSingleObject(void* handle, unsigned long milliseconds);
}

static void
HostSleepNanoseconds(unsigned long long ns)
{
	const unsigned long	kHighResolution = 0x00000002;		// CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
	const unsigned long	kTimerAllAccess = 0x001F0003;		// TIMER_ALL_ACCESS
	static thread_local void* timer = nullptr;
	static thread_local bool tried = false;
	if (!tried)
	{
		tried = true;
		timer = CreateWaitableTimerExW(nullptr, nullptr, kHighResolution, kTimerAllAccess);
	}
	long long due = -(long long) (ns / 100);		// (relative, in 100 ns units)
	if (timer != nullptr && due < 0 && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, 0))
		WaitForSingleObject(timer, 0xFFFFFFFF);
	else
		std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
}
#else
static void
HostSleepNanoseconds(unsigned long long ns)
{
	std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
}
#endif

// 3.6864 MHz: 3686.4 ticks per millisecond, i.e. 4608 ticks per 1.25 ms
static Int64
HostSteadyClock()
{
	auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - gHostClockStart).count();
	unsigned long long ticks = (unsigned long long) ns * 4608 / 1250000;
	// (lo is the low 32 bits alone: ULong is wider than that on a 64-bit
	// host, and a lo carrying the high part counted it twice once the
	// clock passed 2^32 ticks - 19.4 minutes - which jumped the time)
	Int64 t = { (SLong) (ticks >> 32), (ULong) (uint32_t) ticks };
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
		HostSleepNanoseconds(ticks * 1250000 / 4608);
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
