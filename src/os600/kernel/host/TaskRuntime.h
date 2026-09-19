/*
	File:		kernel/host/TaskRuntime.h

	Contains:	The host's stand-in for SWIBoot's context switch and for the idle
				task's wait-for-interrupt (docs/host-runtime.md).

				One host thread per TTask; only the thread of gCurrentTask runs.
				A system-call stub calls the kernel glue, then HostSWIExit: the
				exit path's decisions (SWIExitSchedule) and, if another task was
				picked, the hand-over to its thread.  Interrupts (the timer
				engine's alarm, the scheduler's time slice) are taken at those
				points and in the idle task, never concurrently.

				Started with HostRunTasks(idle) from the main thread, which
				returns when a task calls HostStopTasks.  Threads of tasks still
				parked then are left where they are; the process exit reaps them.

	ROM:		SWIBoot 0x003a4018-0x003a44c0 (assembly), SleepTask 0x001ce924,
				MonitorEntryGlue 0x0038ac98 (the redirect convention)
*/

#ifndef __HOST_TASKRUNTIME_H
#define __HOST_TASKRUNTIME_H

#ifndef __TASK_H
#include "Task.h"
#endif

// Thrown out of a stub when the task is resumed at a pc the kernel changed
// (Throw, TaskKillSelf, MonitorEntryGlue); the task's thread trampoline
// catches it and calls the function at fRegister[kcPC] with r0-r3 as its
// arguments, as the ARM would enter it.
struct TTaskRedirect
{
	TRegister		fPC;
};

enum { kcR0 = 0, kcR1, kcR2, kcR3, kcR4, kcR11 = 11, kcSP = 13, kcLR = 14, kcPC = 15 };

// A stub's resume marker: any value no host function lives at.  A stub that
// wants "skip the retry" semantics (semaphores) uses marker + 4 as well.
const TRegister kResumeInStub = 0x100;

void		HostRunTasks(TTask* idle);				// main thread: run until HostStopTasks; `idle` is gIdleTask's stand-in entry
void		HostStopTasks();						// from a task: end HostRunTasks
Boolean		HostSWIExit(TTask* self, TRegister marker);	// after the glue: deliver due interrupts, exit path, switch if picked;
													// true if a switch happened (results are then in self's registers).
													// Throws TTaskRedirect if the pc is no longer `marker` on resume.
void		HostDeliverInterrupts();				// run the handlers of due alarms (called with the baton)
void		HostIdleTask();							// the idle task's body: wait for the next deadline, deliver, reschedule
void		HostTaskDeleted(TTask* task);			// ~TTask: forget the task's thread

// A host thread that is none of the machine's - the window's, say - must
// not make a system call: a stub would take gCurrentTask for its own and
// the exit path would park the wrong thread in the runtime's place, with
// two threads then believing they are the running task.  Such a thread
// says so once, and the stubs refuse its calls instead.
void		HostAlienThread();						// the calling thread is not a task's
Boolean		HostIsAlienThread();					// whether it said so

// DEVIATION: the ARM takes a timer interrupt at whatever instruction a
// task is on, so the MessagePad's kernel can preempt a task that never
// enters it at all.  The host cannot - its interrupts are delivered
// where a task calls into the kernel and in the idle task - so a task
// that only computes keeps the baton for ever, and the ROM has loops
// that count on being preempted.  A long-running loop offers this as a
// point the kernel may take one at instead.
void		HostPreemptionPoint();					// a due interrupt taken here; the baton may be handed on

// A machine that has stopped.  Every task blocked with nobody able to
// run looks exactly like an idle one from outside - the window still
// repaints, the CPU is quiet - so the watchdog says so instead: a
// thread of the host's own that notices the baton has not been handed
// on for that many seconds and prints what every task was doing.  It
// only watches; it never touches the runtime.
void		HostWatchdogStart(long seconds);

extern Boolean	gHostTasksStopping;

#endif	/* __HOST_TASKRUNTIME_H */
