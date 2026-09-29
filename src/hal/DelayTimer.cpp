/*
	File:		hal/DelayTimer.cpp

	Contains:	TDelayTimer (the DDK's DelayTimer.h): short busy waits and
				timeouts in hardware time units, the free-running timer's
				ticks.

				DEVIATION (hardware): the ROM reads the timer's counter
				register (0x0f181800) through fTimerCounterReg; a host has
				no register to point at, so each read is the port's
				FIQTimerCounter (hal/FIQTimer.h), the same counter.

	Reconstructed from the MP2x00 US ROM (0x0008e978-0x0008ea7c); each
	function cites its origin.
*/

#include "NewtonTypes.h"
#include "NewtonTime.h"
#include "DelayTimer.h"
#include "FIQTimer.h"


// ROM 0x0008e978 __ct__11TDelayTimerFv
TDelayTimer::TDelayTimer()
{
	fTimerCounterReg = nil;
}


// ROM 0x0008e9ac ConvertToHardwareTime__11TDelayTimerFUl
THardwareTimeUnits
TDelayTimer::ConvertToHardwareTime(TTimeout time)
{
	return time + 1;
}


// ROM 0x0008e9b4 ConvertFromHardwareTime__11TDelayTimerFUl
TTimeout
TDelayTimer::ConvertFromHardwareTime(THardwareTimeUnits time)
{
	return time;
}


// ROM 0x0008e9bc GetHardwareTime__11TDelayTimerFv
THardwareTimeUnits
TDelayTimer::GetHardwareTime()
{
	return FIQTimerCounter();
}


// ROM 0x0008e9c8 ShortTimerDelay__11TDelayTimerFUl
void
TDelayTimer::ShortTimerDelay(THardwareTimeUnits delay)
{
	ULong start = FIQTimerCounter();
	while (FIQTimerCounter() - start < delay)
		;
}


// ROM 0x0008e9f4 ResetTimeOut__11TDelayTimerFUl
void
TDelayTimer::ResetTimeOut(THardwareTimeUnits delay)
{
	ULong now = FIQTimerCounter();
	fTimeOutDelay = delay;
	fTimeOutStart = now;
}


// ROM 0x0008ea08 ShortTimerDelayUntil__11TDelayTimerFUl
void
TDelayTimer::ShortTimerDelayUntil(THardwareTimeUnits delay)
{
	while (FIQTimerCounter() - fTimeOutStart < delay)
		;
}


// ROM 0x0008ea34 TimedOut__11TDelayTimerFv
FastBoolean
TDelayTimer::TimedOut()
{
	return fTimeOutDelay <= FIQTimerCounter() - fTimeOutStart;
}


// ROM 0x0008ea58 TimedOut__11TDelayTimerFUl
FastBoolean
TDelayTimer::TimedOut(THardwareTimeUnits delay)
{
	return delay <= FIQTimerCounter() - fTimeOutStart;
}
