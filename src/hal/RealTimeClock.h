/*
	File:		hal/RealTimeClock.h

	Contains:	The real-time clock: a free-running count of seconds that keeps
				time while the machine is asleep, and a one-shot alarm on it.
				It is a different clock from hal/Timer.h's - that one counts
				3.6864 MHz ticks since the machine came up and is what the
				scheduler and the timer engine run on, this one survives sleep
				and is what a diary alarm is set against.

				The MessagePad's is in the Voyager ASIC: the count reads from
				0x0F181000 (twice, until two reads agree, because it ticks
				asynchronously), it is set by writing the same register, the
				alarm's match register is 0x0F181400 and a match raises the
				alarm interrupt (0x401 in the interrupt controller).  Every
				hardware or host port provides the four calls below; the kernel
				keeps its sixteen named alarms over them
				(os600/kernel/RealTimeClock.h).

	ROM:		GetRealTimeClock 0x0019c4cc, SetRealTimeClockAlarm 0x0019c4ec,
				ClearRealTimeClockAlarm 0x0019c510; the write of the count
				itself is inline in TRealTimeClock::SetRealTimeClock
				(0x0019c404), which spins until the count reads back.
*/

#ifndef __HAL_REALTIMECLOCK_H
#define __HAL_REALTIMECLOCK_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

extern "C" {

// the count, in seconds
ULong	GetRealTimeClock(void);

// set it (the caller spins until GetRealTimeClock answers the new value)
void	WriteRealTimeClock(ULong seconds);

// the alarm: fire when the count reaches seconds.  ==> noErr when armed
long	SetRealTimeClockAlarm(ULong seconds);
long	ClearRealTimeClockAlarm(void);

}

#endif	/* __HAL_REALTIMECLOCK_H */
