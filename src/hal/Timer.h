/*
	File:		hal/Timer.h

	Contains:	The system clock and the alarm the kernel's timer engine uses.

				The Newton keeps a 64-bit tick count (3.6864 MHz, see
				NewtonTime.h) as gTimerSample {hi, lo}: `lo` is the last value
				read from the free-running 32-bit counter at 0x0F181800 and `hi`
				counts its wraps; GetClock re-reads the counter and bumps `hi`
				when it has wrapped.  SetAlarm programs match register 1 to the
				low word of an absolute time if that time is still ahead.

	ROM:		GetClock 0x003a3d9c, SetAlarm 0x003a3dc8, DisableAlarm1 0x003a3d3c
				(assembly; DisableAlarm1 also masks the alarm interrupt, bit 0x20,
				in the interrupt controller at 0x0F184000 / gIntMaskShadowReg)
*/

#ifndef __HAL_TIMER_H
#define __HAL_TIMER_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

extern "C" {
void	GetClock(Int64* outTime);				// current time in ticks
Boolean	SetAlarm(const TTime* time);			// true if armed; false if the time has already passed (or is negative)
void	DisableAlarm1(void);					// disarm
void	UpdateClock(void);						// sample the counter so a wrap is not missed (ROM 0x003a3dc0)
}

#endif	/* __HAL_TIMER_H */
