/*
	File:		hal/host/HostInterruptSources.h

	Contains:	Interrupt sources for a host build: hardware a host driver
				stands in for whose interrupt the runtime delivers the way
				it delivers the timers' and the real-time clock's.

				A source is two functions.  `deadline` answers whether an
				interrupt is due at all and, if one is, the system-clock
				time it falls due (GetClock's units); `deliver` is the
				interrupt handler, run when that time has come.  The task
				runtime (os600/kernel/host/TaskRuntime.cpp) asks every
				source at each safe point - the exit path of a system call,
				the idle task's loop - and delivers the ones that are due
				(HostDeliverInterruptSources), and the idle task folds the
				earliest deadline into the time it sleeps until
				(HostInterruptSourcesDeadline), so a source's interrupt
				wakes a machine that has nothing else to do.

				The rules are the runtime's (docs/host-runtime.md): `deliver`
				runs on whichever task's thread holds the baton, at interrupt
				level, and may do what an ARM interrupt handler may - a
				SendForInterrupt, a flag for the deferred-work path - but no
				system call.  `deadline` may be asked from the same place
				and must only read.  A host thread that is not a task (an
				audio device's callback) must never call either; it can only
				leave something for them to read.

				The ROM has no such registry: each interrupt is wired to its
				handler by the interrupt controller.  This is the host's
				stand-in for that wiring, as the RTC's alarm check in
				HostDeliverInterrupts is.
*/

#ifndef __HOSTINTERRUPTSOURCES_H
#define __HOSTINTERRUPTSOURCES_H

#include "Newton.h"

typedef Boolean	(*HostInterruptDeadlineProc)(Int64* when);	// ==> true and the time, when one is due
typedef void	(*HostInterruptDeliverProc)(void);

// A source added; answers false when the table is full (eight sources).
// Registering the same pair twice adds it once.
Boolean	HostRegisterInterruptSource(HostInterruptDeadlineProc deadline, HostInterruptDeliverProc deliver);
void	HostUnregisterInterruptSource(HostInterruptDeadlineProc deadline, HostInterruptDeliverProc deliver);

// The runtime's side: every source that is due at `now` delivered (a
// source's deadline is asked again after it is delivered, so one whose
// handler arms the next interrupt is not delivered twice in a row), and
// the earliest deadline of those not yet due (false when there is none).
void	HostDeliverInterruptSources(const Int64* now);
Boolean	HostInterruptSourcesDeadline(Int64* deadline);

#endif	/* __HOSTINTERRUPTSOURCES_H */
