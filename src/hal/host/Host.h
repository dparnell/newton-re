/*
	File:		hal/host/Host.h

	Contains:	Controls the host HAL exposes to tests and to a host runtime:
				the fake clock and the recorded interrupt/alarm state.
*/

#ifndef __HAL_HOST_H
#define __HAL_HOST_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

void			HostAdvanceClock(ULong ticks);
void			HostSetClock(const Int64* time);
void			HostUseRealClock(Boolean real);		// the host's steady clock instead of the controllable one
void			HostSleepUntil(const Int64* time);	// real clock: sleep; controllable clock: jump to `time`
extern Boolean	gHostAlarmArmed;
extern Int64	gHostAlarmTime;
extern Boolean	gHostRTCAlarmArmed;		// the real-time clock's alarm (hal/RealTimeClock.h)
extern ULong	gHostRTCAlarmSeconds;
Boolean			HostRTCAlarmDeadline(Int64* outTime);	// when it is due on the system clock
extern Boolean	gHostInterruptEnabled;
extern Boolean	gHostTimeSliceArmed;
extern Int64	gHostTimeSliceDeadline;
extern ULong	gHostResetCount;
extern void	(*gHostResetHook)(void);			// run by Reset() after recording it; the task runtime ends the run with it
extern ULong	gHostDomainAccess;
extern Boolean	gHostPoweredOff;

#endif	/* __HAL_HOST_H */
