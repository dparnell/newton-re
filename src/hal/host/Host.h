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
extern Boolean	gHostAlarmArmed;
extern Int64	gHostAlarmTime;
extern Boolean	gHostInterruptEnabled;
extern ULong	gHostTimeSliceAlarm;

#endif	/* __HAL_HOST_H */
