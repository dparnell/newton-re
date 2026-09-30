/*
	File:		hal/host/HostPower.h

	Contains:	The host's stand-ins for the MessagePad's power hardware
				(hal/Power.h): the machine asleep and what wakes it.

				A MessagePad asleep is turned off, its RAM kept; the power
				switch, the card lock, the serial port's input line, the
				interconnect or the real-time clock's alarm turns it on
				again.  The host cannot turn itself off, so asleep the task
				that put it to sleep - holding the scheduler, as the ROM's
				CyclePower does - waits in PlatformPowerOffSystem, and the
				rest of the machine with it, until something wakes it:

				  - the power switch - a key of the host's (newton's window:
				    F12, host/HostKeyboard.cpp), and, since a host has no
				    switch within reach of the pen, a tap on the window or
				    any key (DEVIATION: a MessagePad's pen does not wake it);
				  - a real-time clock alarm falling due;
				  - a test's wake, armed with HostPowerWakeAfter.

				With no window and no test wake armed nothing could ever
				wake it, so the machine does not sleep at all and comes
				straight back with nothing to report (as the host always
				did before it slept).  Everything here may be called from
				any thread: the window's calls are plain memory writes.
*/

#ifndef __HAL_HOST_POWER_H
#define __HAL_HOST_POWER_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

// The bit of the power event word the host's power switch sets (the
// Voyager's switch is a GPIO line whose bit TranslatePowerEvent does not
// name: it wakes the machine 'because).  DEVIATION: the bit is the host's.
enum { kHostPowerEventSwitch = 0x00000100 };

void	HostPowerWake(ULong events);			// something woke the machine (while it sleeps; otherwise ignored)
Boolean	HostPowerAsleep(void);					// in PlatformPowerOffSystem now
ULong	HostPowerSleeps(void);					// how many times the machine has gone to sleep
void	HostPowerWindowOpened(void);			// a window is up: the pen and the keys can wake it
void	HostPowerWakeAfter(ULong milliseconds);	// a test's wake: the next sleep ends with the switch after so long asleep

#endif	/* __HAL_HOST_POWER_H */
