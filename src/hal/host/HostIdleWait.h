/*
	File:		hal/host/HostIdleWait.h

	Contains:	The idle task's wait (hal/host/Timer.cpp's HostSleepUntil,
				os600/kernel/host/TaskRuntime.cpp's HostIdleTask): up to so
				many nanoseconds, cut short by HostIdleWake from any thread
				- a host thread that has left the machine something to
				deliver (the pen down, a key queued, the power switch) wakes
				it at once, so that nothing on the Newton's side need look
				for it every few milliseconds.  Before, the idle task slept
				to the next Newton timer and only that ended it: the host's
				keyboard task looked every 10 ms so that a key or a tap was
				not kept waiting, which woke the machine a hundred times a
				second with nothing to do (docs/host-runtime.md, "The idle
				task's wait").

				A wake that comes while nobody waits is kept: the next wait
				returns at once.  Windows keeps its high-resolution waitable
				timer (Timer.cpp says why) with an event beside it; elsewhere
				a condition variable.  No Newton headers here: the standard
				library's thread headers and the Newton's do not mix.
*/

#ifndef __HAL_HOST_IDLEWAIT_H
#define __HAL_HOST_IDLEWAIT_H

// ==> true if woken (HostIdleWake), false if the time ran out
bool	HostIdleWaitNanoseconds(unsigned long long ns);
void	HostIdleWake(void);

#endif	/* __HAL_HOST_IDLEWAIT_H */
