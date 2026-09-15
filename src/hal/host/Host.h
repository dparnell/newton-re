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
extern Boolean	gHostInterruptEnabled;
extern Boolean	gHostTimeSliceArmed;
extern Int64	gHostTimeSliceDeadline;
extern ULong	gHostResetCount;
extern ULong	gHostDomainAccess;
extern Boolean	gHostPoweredOff;

#endif	/* __HAL_HOST_H */
