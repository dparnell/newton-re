/*
	File:		hal/host/HostIdleWait.cpp

	Contains:	The idle task's wait, cut short by a wake (HostIdleWait.h).
				Host code (no ROM counterpart).
*/

#include "HostIdleWait.h"

#ifdef _WIN32

// a high-resolution waitable timer (Timer.cpp: a plain wait ends on the
// system's 15.6 ms tick) and an auto-reset event that a wake sets, waited
// on together
extern "C" {
__declspec(dllimport) void* __stdcall CreateWaitableTimerExW(void* attributes, const wchar_t* name, unsigned long flags, unsigned long access);
__declspec(dllimport) int __stdcall SetWaitableTimer(void* timer, const long long* due, long period, void* routine, void* arg, int resume);
__declspec(dllimport) void* __stdcall CreateEventW(void* attributes, int manualReset, int initialState, const wchar_t* name);
__declspec(dllimport) int __stdcall SetEvent(void* event);
__declspec(dllimport) unsigned long __stdcall WaitForMultipleObjects(unsigned long count, void* const* handles, int waitAll, unsigned long milliseconds);
__declspec(dllimport) void __stdcall Sleep(unsigned long milliseconds);
}

static void*
WakeEvent(void)
{
	static void* event = CreateEventW(nullptr, 0, 0, nullptr);		// (auto-reset, not set)
	return event;
}


bool
HostIdleWaitNanoseconds(unsigned long long ns)
{
	const unsigned long	kHighResolution = 0x00000002;		// CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
	const unsigned long	kTimerAllAccess = 0x001F0003;		// TIMER_ALL_ACCESS
	const unsigned long	kWaitObject0 = 0, kInfinite = 0xFFFFFFFF;
	static thread_local void* timer = nullptr;
	static thread_local bool tried = false;
	if (!tried)
	{
		tried = true;
		timer = CreateWaitableTimerExW(nullptr, nullptr, kHighResolution, kTimerAllAccess);
	}
	void* event = WakeEvent();
	long long due = -(long long) (ns / 100);		// (relative, in 100 ns units)
	if (timer != nullptr && event != nullptr && due < 0 && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, 0))
	{
		void* handles[2] = { event, timer };
		return WaitForMultipleObjects(2, handles, 0, kInfinite) == kWaitObject0;
	}
	Sleep((unsigned long) (ns / 1000000));
	return false;
}


void
HostIdleWake(void)
{
	void* event = WakeEvent();
	if (event != nullptr)
		SetEvent(event);
}

#else

#include <chrono>
#include <mutex>
#include <condition_variable>

static std::mutex				gLock;
static std::condition_variable	gWake;
static bool						gWoken = false;


bool
HostIdleWaitNanoseconds(unsigned long long ns)
{
	std::unique_lock<std::mutex> hold(gLock);
	bool woken = gWake.wait_for(hold, std::chrono::nanoseconds(ns), [] { return gWoken; });
	gWoken = false;
	return woken;
}


void
HostIdleWake(void)
{
	{
		std::lock_guard<std::mutex> hold(gLock);
		gWoken = true;
	}
	gWake.notify_all();
}

#endif
