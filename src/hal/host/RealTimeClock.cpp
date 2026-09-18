/*
	File:		hal/host/RealTimeClock.cpp

	Contains:	The real-time clock (hal/RealTimeClock.h) for host builds.
				The Newton's is a counter in the Voyager ASIC that runs on its
				own; the host has no second clock, so this one is the system
				clock (hal/Timer.h) converted to seconds plus a base the
				machine's time is set with - which means it does not keep
				running when the host build is not, but everything that reads
				it sees a monotonic second count that can be set.

				The alarm is recorded rather than delivered: the task runtime's
				HostDeliverInterrupts fires TRealTimeClock::Alarm when the
				count passes it, as the alarm interrupt would.
*/

#include "hal/RealTimeClock.h"
#include "hal/Timer.h"
#include "hal/host/Host.h"
#include "NewtonTime.h"
#include "OSErrors.h"

static ULong	gHostRealTimeClockBase = 0;		// seconds at the origin of the system clock

Boolean			gHostRTCAlarmArmed = false;
ULong			gHostRTCAlarmSeconds = 0;


// the system clock in seconds, which is what the count counts from its base
static ULong
ClockSeconds(void)
{
	TTime now;
	GetClock(&now.time);
	return now.ConvertTo(kSeconds);
}


extern "C" ULong
GetRealTimeClock(void)
{
	return gHostRealTimeClockBase + ClockSeconds();
}


extern "C" void
WriteRealTimeClock(ULong seconds)
{
	gHostRealTimeClockBase = seconds - ClockSeconds();
}


extern "C" long
SetRealTimeClockAlarm(ULong seconds)
{
	gHostRTCAlarmArmed = true;
	gHostRTCAlarmSeconds = seconds;
	return noErr;
}


extern "C" long
ClearRealTimeClockAlarm(void)
{
	gHostRTCAlarmArmed = false;
	return noErr;
}


// The system-clock time the armed alarm is due, for the idle task to sleep
// until.  The count only moves with the system clock, so a second of it is
// kSeconds ticks; an alarm already due comes back as the time now.
Boolean
HostRTCAlarmDeadline(Int64* outTime)
{
	if (!gHostRTCAlarmArmed)
		return false;
	ULong now = GetRealTimeClock();
	ULong left = gHostRTCAlarmSeconds > now ? gHostRTCAlarmSeconds - now : 0;
	TTime delay(left, kSeconds);
	Int64 deadline = delay.time;
	Int64 clock;
	GetClock(&clock);
	CompAdd(&clock, &deadline);
	*outTime = deadline;
	return true;
}
