/*
	File:		kernel/host/TaskRuntime.cpp

	Contains:	The host task runtime: threads standing in for the ARM's saved
				register sets, a baton standing in for the processor.

	The baton: every task's thread waits on its own condition until the
	runtime marks it running; the thread giving up the processor marks the
	next one and then waits itself.  At no time do two of them run, so the
	kernel needs no more protection than on the MessagePad.

	What a switch does is what SWIBoot's exit path does after picking:
	gCurrentTask is already the pick (SWIExitSchedule); the resumed thread
	calls SwapInGlobals and loads the domain access word, then carries on
	where it stopped - inside its stub, which then reads its results from the
	saved registers - or, if it is new or the kernel changed its pc, at the
	function the pc names.
*/

#include <condition_variable>		// before the DDK headers, whose macros upset libc++
#include <map>
#include <mutex>
#include <atomic>
#include <chrono>
#include <thread>

#include "TaskRuntime.h"
#include "Boot.h"
#include "TaskSwitch.h"
#include "Scheduler.h"
#include "TimerEngine.h"
#include "KernelGlobals.h"
#include "CompMath.h"
#include "hal/Atomic.h"

#include "hal/Timer.h"
#include "hal/RealTimeClock.h"
#include "RealTimeClock.h"
#include "hal/MMU.h"
#include "hal/host/Host.h"
#include "hal/host/HostInterruptSources.h"
#include "MonitorGlue.h"

#include <stdio.h>
#include <stdlib.h>


struct HostTaskContext
{
	std::thread			fThread;
	Boolean				fHasThread = false;
	Boolean				fRunning = false;		// holds the baton (or has just been handed it)
	std::condition_variable	fTurn;				// what the task's thread waits on for the baton
};

// Each task's thread waits for the baton on a condition of its own
// (HostTaskContext::fTurn), so that handing it over wakes the one thread
// that takes it.  (With one condition shared by all of them every handover
// woke every parked thread - some ninety in a booted newton - to look and
// sleep again, which was most of the processor time the host used while
// the Newton sat idle.)
//
// The baton, the run's own condition and the contexts with theirs are made
// once and never destroyed.  The run ends with task threads still parked on
// them (see below, and docs/host-runtime.md: "the run ends by leaving parked
// threads to the process exit"), and destroying a condition variable
// somebody is waiting on is undefined: glibc's pthread_cond_destroy waits
// for its waiters to leave, so a static one destroyed on the way out of
// main hung the process after every check had passed - which is what every
// test that boots the OS did on Linux.  Windows' own destructor happens not
// to wait, which is why it was never seen there.  (A deleted task's context
// stays too: its thread is parked on its condition.)
static std::mutex&						gBaton = *new std::mutex;
static std::condition_variable&			gBatonChanged = *new std::condition_variable;
static std::map<TTask*, HostTaskContext*>	gContexts;
static TTask*							gRunningTask = nil;		// whose thread holds the baton
static std::atomic<unsigned long>		gHandovers(0);			// bumped every time the baton is taken
void									(*gHostStallReportHook)(void) = nil;
static Boolean							gStopRequested = false;
Boolean									gHostTasksStopping = false;


static HostTaskContext*
ContextFor(TTask* task)
{
	HostTaskContext*& ctx = gContexts[task];
	if (ctx == nil)
		ctx = new HostTaskContext;
	return ctx;
}


// What SWIBoot does on the way back into a task.
static void
Resume(TTask* task)
{
	SwapInGlobals(task);
	SetDomainAccessControl(DomainAccessFor(task));
}


// Waits until `task` holds the baton (the calling thread is its thread).
static void
WaitForBaton(TTask* task, std::unique_lock<std::mutex>& lock)
{
	HostTaskContext* ctx = ContextFor(task);
	ctx->fTurn.wait(lock, [&] { return ctx->fRunning || gStopRequested; });
	if (gStopRequested)
	{
		// the run is over; this thread has nothing more to do and is left here
		ctx->fTurn.wait(lock, [] { return false; });
	}
	gRunningTask = task;
	gHandovers.fetch_add(1);
}


// A task's thread: run whatever the saved pc names, for as long as the kernel
// keeps redirecting it.
static void
Trampoline(TTask* task)
{
	{
		std::unique_lock<std::mutex> lock(gBaton);
		WaitForBaton(task, lock);
	}
	Resume(task);
	for (;;)
	{
		try
		{
			// entered as on the ARM: with r0-r3 as its arguments
			typedef void (*EntryProc)(TRegister, TRegister, TRegister, TRegister);
			EntryProc entry = (EntryProc) task->fRegister[kcPC];
			TRegister* r = task->fRegister;
			entry(r[kcR0], r[kcR1], r[kcR2], r[kcR3]);
			// a task proc that returns goes to BadExit (the ROM sets lr to it
			// in TTask::Init)
			task->fRegister[kcPC] = (TRegister) BadExit;
		}
		catch (TTaskRedirect&)
		{
			// fRegister[kcPC] is the new pc; go round
		}
	}
}


// The kernel changed the task's pc while it was blocked.  A function that
// never returns (Throw longjmps to a handler on this stack, TaskKillSelf
// ends the task) is entered here, with the stack as it is - as the ARM would
// resume it; MonitorEntryGlue needs the stack empty, so for it the stub is
// unwound to the trampoline, which calls it.
static void
Redirect(TTask* self)
{
	TRegister pc = self->fRegister[kcPC];
	if (pc == (TRegister) MonitorEntryGlue)
		throw TTaskRedirect{ pc };
	typedef void (*EntryProc)(TRegister, TRegister, TRegister, TRegister);
	TRegister* r = self->fRegister;
	((EntryProc) pc)(r[kcR0], r[kcR1], r[kcR2], r[kcR3]);
	fprintf(stderr, "[host] a redirected task entry returned\n");
	abort();
}


// Hands the baton to `next` and waits until `self` has it again.
static void
SwitchTo(TTask* self, TTask* next)
{
	std::unique_lock<std::mutex> lock(gBaton);
	HostTaskContext* ctx = ContextFor(next);
	ctx->fRunning = true;
	if (!ctx->fHasThread)
	{
		ctx->fHasThread = true;
		ctx->fThread = std::thread(Trampoline, next);
		ctx->fThread.detach();
	}
	ContextFor(self)->fRunning = false;
	ctx->fTurn.notify_one();
	WaitForBaton(self, lock);
}


static void DeliverDueInterrupts();

// The handlers run in what is IRQ mode on the MessagePad: gHostInterruptLevel
// makes IsSuperMode say so, so that a dual-mode routine a handler calls
// (GetGlobalTime, from the serial tool's receive interrupt) takes the
// supervisor path instead of making a system call on the interrupted task's
// behalf - which Enter refuses (SWI.cpp).
void
HostDeliverInterrupts()
{
	gHostInterruptLevel++;
	DeliverDueInterrupts();
	gHostInterruptLevel--;
}


static void
DeliverDueInterrupts()
{
	Int64 now;
	GetClock(&now);
	if (gHostAlarmArmed && CompCompare(&now, &gHostAlarmTime) >= 0)
	{
		gHostAlarmArmed = false;
		TimerInterruptHandler();
	}
	if (gHostRTCAlarmArmed && GetRealTimeClock() >= gHostRTCAlarmSeconds)
	{
		gHostRTCAlarmArmed = false;
		TRealTimeClock::Alarm();				// the real-time clock's alarm interrupt
	}
	if (gHostTimeSliceArmed && gHostInterruptEnabled && CompCompare(&now, &gHostTimeSliceDeadline) >= 0)
	{
		gHostTimeSliceArmed = false;
		PreEmptiveTimerInterruptHandler();
	}
	HostDeliverInterruptSources(&now);			// the host drivers' interrupts (hal/host/HostInterruptSources.h)
}


Boolean
HostSWIExit(TTask* self, TRegister marker)
{
	HostDeliverInterrupts();
	TTask* next = SWIExitSchedule();
	if (next == self)
	{
		SetDomainAccessControl(DomainAccessFor(self));
		return false;
	}
	SwitchTo(self, next);
	Resume(self);
	// (marker + 4 is not a redirection: it is the semaphore stub's "one word
	// further on" - a blocked op failed by TSemaphore's destructor - which
	// the stub reads itself)
	if (self->fRegister[kcPC] != marker && self->fRegister[kcPC] != marker + 4)
		Redirect(self);
	return true;
}


// The idle task: SleepTask's PauseSystem loop.  With nothing to wait for on
// the controllable clock the system is finished - every task is blocked with
// no timer to wake it - and the run ends.
void
HostIdleTask()
{
	TTask* self = gCurrentTask;
	for (;;)
	{
		Boolean haveDeadline = false;
		Int64 deadline = { 0, 0 };
		if (gHostAlarmArmed)
		{
			deadline = gHostAlarmTime;
			haveDeadline = true;
		}
		if (gHostTimeSliceArmed && gHostInterruptEnabled && (!haveDeadline || CompCompare(&gHostTimeSliceDeadline, &deadline) < 0))
		{
			deadline = gHostTimeSliceDeadline;
			haveDeadline = true;
		}
		Int64 rtc;
		if (HostRTCAlarmDeadline(&rtc) && (!haveDeadline || CompCompare(&rtc, &deadline) < 0))
		{
			deadline = rtc;
			haveDeadline = true;
		}
		Int64 source;
		if (HostInterruptSourcesDeadline(&source) && (!haveDeadline || CompCompare(&source, &deadline) < 0))
		{
			deadline = source;
			haveDeadline = true;
		}
		if (haveDeadline)
			HostSleepUntil(&deadline);
		else if (!gSchedule && !gWantDeferred)
		{
			fprintf(stderr, "[host] idle with nothing to wait for: stopping\n");
			HostStopTasks();
		}
		HostSWIExit(self, self->fRegister[kcPC]);
	}
}


// ROM 0x001cc550 SleepTask__Fv
// The idle loop: on the MessagePad `for (;;) PauseSystem();`, waking for each
// interrupt; here the boot thread hands over to the runtime, whose idle task
// body does the waiting.  Returns when the run ends (the ROM's never does).
void
SleepTask()
{
	HostRunTasks(gIdleTask);
}


static void
ResetEndsTheRun()
{
	HostStopTasks();
}


void
HostRunTasks(TTask* idle)
{
	gStopRequested = false;
	gHostTasksStopping = false;
	gHostResetHook = ResetEndsTheRun;
	gTaskDeletedHook = HostTaskDeleted;
	gCurrentTask = idle;
	idle->fRegister[kcPC] = (TRegister) HostIdleTask;
	std::unique_lock<std::mutex> lock(gBaton);
	HostTaskContext* ctx = ContextFor(idle);
	ctx->fRunning = true;
	ctx->fHasThread = true;
	ctx->fThread = std::thread(Trampoline, idle);
	ctx->fThread.detach();
	gBatonChanged.wait(lock, [] { return gStopRequested; });
}


void
HostStopTasks()
{
	std::unique_lock<std::mutex> lock(gBaton);
	gStopRequested = true;
	gHostTasksStopping = true;
	gBatonChanged.notify_all();
	for (std::map<TTask*, HostTaskContext*>::iterator i = gContexts.begin(); i != gContexts.end(); ++i)
		i->second->fTurn.notify_all();
	// this thread is a task's: it parks here for good
	gBatonChanged.wait(lock, [] { return false; });
}


// A task is being deleted (gTaskDeletedHook, from ~TTask).  Its thread, if
// any, is parked in WaitForBaton and never runs again; its context stays
// allocated for it, only the task is forgotten - a new TTask at the same
// address must get a context (and thread) of its own.
// The point a loop that never enters the kernel offers it to preempt
// the task at (see TaskRuntime.h).  What the ROM's view tracking does
// is the case that needs it: a NewtonScript loop reads the stroke over
// and over without ever waiting, and on the MessagePad the inker task
// still gets the processor and finishes the stroke, so the loop ends.
// Here nothing would, and the machine would spin for good.
//
// Nothing happens unless an interrupt is actually due, and never inside
// an atomic section - SWIExitSchedule would refuse to switch there
// anyway - so a run on the controllable clock stays as deterministic as
// it was.
// The four characters of a task's name, as the kernel keeps it.
static void
TaskName(TTask* task, char* out)
{
	ULong name = task != nil ? task->fName : 0;
	for (long i = 0; i < 4; i++)
	{
		char c = (char) (name >> (24 - i * 8));
		out[i] = (c >= 32 && c < 127) ? c : '?';
	}
	out[4] = 0;
}


// What every task was doing when the machine stopped.
static void
ReportTheStall(long seconds)
{
	char name[8];
	fprintf(stderr, "[host] the machine has not run a task for %ld seconds:\n", seconds);
	TaskName(gCurrentTask, name);
	fprintf(stderr, "[host]   the kernel's current task is %s (%p)\n",
			gCurrentTask != nil ? name : "none", (void*) gCurrentTask);
	TaskName(gRunningTask, name);
	fprintf(stderr, "[host]   the baton was last taken by %s (%p)\n",
			gRunningTask != nil ? name : "none", (void*) gRunningTask);
	int atomic = 0, fiq = 0;
	HostAtomicNesting(&atomic, &fiq);
	fprintf(stderr, "[host]   atomic sections: %d, FIQ atomic: %d%s\n", atomic, fiq,
			atomic + fiq > 0 ? "  <- the scheduler may not switch here" : "");
	fprintf(stderr, "[host]   alarm %s, time slice %s, interrupts %s, deferred %s, schedule %s\n",
			gHostAlarmArmed ? "armed" : "off",
			gHostTimeSliceArmed ? "armed" : "off",
			gHostInterruptEnabled ? "on" : "off",
			gWantDeferred ? "wanted" : "none",
			gSchedule ? "wanted" : "none");
	for (std::map<TTask*, HostTaskContext*>::iterator i = gContexts.begin(); i != gContexts.end(); ++i)
	{
		TaskName(i->first, name);
		fprintf(stderr, "[host]   task %s (%p) state %#lx priority %lu%s\n",
				name, (void*) i->first, (unsigned long) i->first->fState,
				(unsigned long) i->first->fPriority,
				i->second->fRunning ? "  <- holds the baton" : "");
	}
	if (gHostStallReportHook != nil)
		gHostStallReportHook();
	fflush(stderr);
}


static void
WatchdogMain(long seconds)
{
	unsigned long was = gHandovers.load();
	long quiet = 0;
	Boolean told = false;
	while (!gStopRequested)
	{
		std::this_thread::sleep_for(std::chrono::seconds(1));
		unsigned long now = gHandovers.load();
		if (now != was)
		{
			was = now;
			quiet = 0;
			told = false;
			continue;
		}
		if (++quiet >= seconds && !told)
		{
			told = true;
			ReportTheStall(quiet);
		}
	}
}


void
HostWatchdogStart(long seconds)
{
	if (seconds <= 0)
		return;
	std::thread watchdog(WatchdogMain, seconds);
	watchdog.detach();
}


void
HostPreemptionPoint()
{
	TTask* self = gCurrentTask;
	if (self == nil || gHostTasksStopping || HostIsAlienThread() || InAtomicSection())
		return;
	HostSWIExit(self, self->fRegister[kcPC]);
}


// The threads that are none of the machine's (see TaskRuntime.h).
static thread_local Boolean	gAlienThread = false;

void
HostAlienThread()
{
	gAlienThread = true;
}


Boolean
HostIsAlienThread()
{
	return gAlienThread;
}


void
HostTaskDeleted(TTask* task)
{
	std::unique_lock<std::mutex> lock(gBaton);
	gContexts.erase(task);
}
