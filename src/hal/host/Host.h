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
// told when the Newton has nothing to run (true: the idle task waiting for
// its next deadline, or the machine asleep in PlatformPowerOffSystem) and
// when it runs again (false) - a device lets its processor slow down
// meanwhile (src/host/esp32/main/tdeck_power.cpp)
extern void	(*gHostIdleHook)(bool idle);
// a device's wait while the machine sleeps (PlatformPowerOffSystem): up to
// so many milliseconds, cut short by the wake (HostPowerWake calls it) - so
// that its processor can sleep too; the host's own looks every 10 ms (nil,
// both: src/host/esp32/main/tdeck_power.cpp has them)
extern void	(*gHostSleepWait)(ULong milliseconds);
extern void	(*gHostSleepWake)(void);
// the host's monotonic clock in nanoseconds, whichever clock the Newton's
// timer is on (what the host's measurements - the NewtonScript profiler,
// frames/FunctionProfiler.h - time by)
unsigned long long	HostMonotonicNanoseconds(void);
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
extern long		gHostInterruptLevel;	// > 0 while an interrupt is being delivered: the processor is in IRQ/FIQ mode, IsSuperMode answers true

// Physical memory a host device keeps (a flash bank), for
// VirtualAddressToPointer (hal/MMU.h) to find behind a mapped section;
// HostClearSections forgets every section AddNewSecPNJT recorded (a test
// starting afresh).
void			HostRegisterPhysicalMemory(PAddr base, ULong size, Ptr memory);
void			HostUnregisterPhysicalMemory(PAddr base);
void			HostClearSections(void);

#endif	/* __HAL_HOST_H */
