/*
	File:		hal/host/HostFIQTimer.cpp

	Contains:	The fast timers' hardware for a host build (hal/FIQTimer.h):
				the free-running counter is the system clock's low word,
				the match register a value kept here, and the match
				interrupt a host interrupt source (HostInterruptSources.h)
				due when the clock reaches it.  And the delay timer's
				counter (TDelayTimer, hal/DelayTimer.cpp) is the same.

	Host code (no ROM counterpart).
*/

#include "FIQTimer.h"
#include "HostInterruptSources.h"
#include "hal/Timer.h"
#include "CompMath.h"

static TFIQTimer*	gHostFIQTimer = nil;
static Boolean		gHostFIQEnabled = false;
static ULong		gHostFIQMatch = 0;


ULong
FIQTimerCounter(void)
{
	Int64 now;
	GetClock(&now);
	return now.lo;
}


void
FIQTimerSetMatch(ULong value)
{
	gHostFIQMatch = value;
}


void
FIQTimerEnable(void)
{
	gHostFIQEnabled = true;
}


void
FIQTimerDisable(void)
{
	gHostFIQEnabled = false;
}


// Due when the counter has reached the match (the difference taken as
// signed, so the counter's wrap is no matter).
static Boolean
FIQDeadline(Int64* when)
{
	if (gHostFIQTimer == nil || !gHostFIQEnabled)
		return false;
	GetClock(when);
	long ahead = (long) (int32_t) (gHostFIQMatch - when->lo);
	if (ahead > 0)
	{
		Int64 delta = { 0, (ULong) ahead };
		CompAdd(&delta, when);
	}
	return true;
}


static void
FIQDeliver(void)
{
	if (gHostFIQTimer != nil && gHostFIQEnabled)
		gHostFIQTimer->FIQTimerInterrupt();
}


void
FIQTimerInstallHardware(TFIQTimer* timer)
{
	gHostFIQTimer = timer;
	HostRegisterInterruptSource(FIQDeadline, FIQDeliver);
}
