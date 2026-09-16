/*
	File:		user/UserTime.cpp

	Contains:	The time calls of NewtonTime.h: GetGlobalTime, GetTaskTime,
				TimeFromNow, and TTime's unit arithmetic.  Time is the
				kernel's clock (hal/Timer.h GetClock: 3.6864 MHz ticks in an
				Int64); user mode asks for it with GenericSWI's kGetTaskTime
				(task 0 is the global clock).

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "NewtonTime.h"
#include "UserGlobals.h"
#include "KernelGlobals.h"
#include "CompMath.h"
#include "hal/System.h"
#include "hal/Timer.h"
#include "os600/GenericSWISelectors.h"


// ROM 0x0013ec68 GetGlobalTime
// Dual-mode: the kernel reads the clock, a task asks for it (host: so does
// code running with no task at all, as the standalone tests do).
extern "C" TTime
GetGlobalTime(void)
{
	TTime now;
	if (IsSuperMode() || gCurrentTask == nil)
		GetClock(&now.time);
	else
	{
		ULong lo, hi;
		GenericWithReturnSWI(kGeneric_GetTaskTime, 0, 0, 0, &lo, &hi, nil);
		now.time.hi = (SLong) hi;
		now.time.lo = lo;
	}
	return now;
}


// ROM 0x0013ed10 GetTaskTime
// The time a task has run (the current task by default).
extern "C" TTime
GetTaskTime(TObjectId timeForTaskId)
{
	if (timeForTaskId == 0)
		timeForTaskId = gCurrentTaskId;
	ULong lo, hi;
	GenericWithReturnSWI(kGeneric_GetTaskTime, timeForTaskId, 0, 0, &lo, &hi, nil);
	TTime time;
	time.time.hi = (SLong) hi;
	time.time.lo = lo;
	return time;
}


// ROM 0x0013ed8c TimeFromNow
extern "C" TTime
TimeFromNow(TTimeout deltaTime)
{
	Int64 delta;
	delta.hi = 0;
	delta.lo = deltaTime;
	TTime now = GetGlobalTime();
	Int64 then = now.time;
	CompAdd(&delta, &then);
	TTime result;
	result.time = then;
	return result;
}


// ROM 0x0033349c Set__5TTimeFUl9TimeUnits
// amount * units, in two halves so that the signed multiply cannot overflow.
void
TTime::Set(ULong amount, TimeUnits units)
{
	CompMul((long) (amount >> 1), (long) units << 1, &time);
	if (amount & 1)
	{
		Int64 one;
		one.hi = 0;
		one.lo = units;
		CompAdd(&one, &time);
	}
}


// ROM 0x003334e8 __ct__5TTimeFUl9TimeUnits
TTime::TTime(ULong amount, TimeUnits units)
{
	Set(amount, units);
}


// ROM 0x0033352c ConvertTo__5TTimeF9TimeUnits
// The time in the given units, rounded to nearest (dividing by twice the
// unit keeps the quotient in range).
ULong
TTime::ConvertTo(TimeUnits units)
{
	long remainder;
	long half = CompDiv(&time, (long) units << 1, &remainder);
	ULong result = (ULong) half * 2;
	if (remainder >= (long) units)
		result++;
	return result;
}


/* -------------------------------------------------------------------------------
	The real-time clock (seconds and minutes since 1 Jan 1904).
	NOT YET RECONSTRUCTED: TURealTimeAlarm (the RTC hardware) and the
	GMT/daylight-saving offsets - the host keeps a settable base and adds
	the global clock's seconds to it.
------------------------------------------------------------------------------- */

static ULong	gRealClockBase = 0;				// seconds at boot (SetRealClockSeconds)


// ROM 0x00253630 RealClockSeconds__Fv
ULong
RealClockSeconds(void)
{
	TTime now = GetGlobalTime();
	return gRealClockBase + now.ConvertTo(kSeconds);
}


// ROM 0x00253670 SetRealClockSeconds__FUl
void
SetRealClockSeconds(ULong seconds)
{
	TTime now = GetGlobalTime();
	gRealClockBase = seconds - now.ConvertTo(kSeconds);
}


// ROM 0x002531b0 Ticks__Fv
// The time in Macintosh ticks (sixtieths of a second), 31 bits.
ULong
Ticks(void)
{
	TTime now = GetGlobalTime();
	return now.ConvertTo(kMacTicks) & 0x7fffffff;
}


// ROM 0x002536ac RealClock__Fv
ULong
RealClock(void)
{
	return RealClockSeconds() / 60;
}


// ROM 0x002536cc SetRealClock__FUl
void
SetRealClock(ULong minutes)
{
	SetRealClockSeconds(minutes * 60);
}
