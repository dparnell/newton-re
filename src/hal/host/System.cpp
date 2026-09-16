/*
	File:		hal/host/System.cpp

	Contains:	Whole-machine control for a host build: a reset is recorded (and
				reported) rather than performed, so tests can observe it.
*/

#include "hal/System.h"

#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

ULong	gHostResetCount = 0;
void	(*gHostResetHook)(void) = nil;
Boolean	gHostPoweredOff = false;

extern "C" NewtonErr
Reset(void)
{
	gHostResetCount++;
	fprintf(stderr, "[host] machine reset requested\n");
	if (gHostResetHook != nil)
		gHostResetHook();
	return 0;
}

extern "C" void
DisableAllInterrupts(void)
{
}

extern "C" void
IOPowerOffAll(void)
{
	gHostPoweredOff = true;
	fprintf(stderr, "[host] power off requested\n");
}

// Every call on the host takes the user-mode path (the system-call stubs),
// so the dual-mode ROM routines exercise it.
extern "C" Boolean
IsSuperMode(void)
{
	return false;
}

// A MessagePad 2100 has 4 MB of DRAM.
extern "C" ULong
GetRamSize(void)
{
	return 4 * 1024 * 1024;
}

// The host runs everything as a user-mode task.
extern "C" ULong
GetCPUMode(void)
{
	return 0x10;
}

// The host thread's stack, from the operating system.
extern "C" void
GetStackBounds(const void** low, const void** high)
{
#ifdef _WIN32
	ULONG_PTR lowLimit, highLimit;
	GetCurrentThreadStackLimits(&lowLimit, &highLimit);
	*low = (const void*) lowLimit;
	*high = (const void*) highLimit;
#else
	pthread_attr_t attr;
	void* base = nil;
	size_t size = 0;
	if (pthread_getattr_np(pthread_self(), &attr) == 0)
	{
		pthread_attr_getstack(&attr, &base, &size);
		pthread_attr_destroy(&attr);
	}
	*low = base;
	*high = (const char*) base + size;
#endif
}
