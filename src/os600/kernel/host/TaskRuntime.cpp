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
#include <thread>

#include "TaskRuntime.h"
#include "Boot.h"
#include "TaskSwitch.h"
#include "Scheduler.h"
#include "TimerEngine.h"
#include "KernelGlobals.h"
#include "CompMath.h"
#include "hal/Timer.h"
#include "hal/MMU.h"
#include "hal/host/Host.h"
#include "MonitorGlue.h"

#include <stdio.h>
#include <stdlib.h>


struct HostTaskContext
{
	std::thread			fThread;
	Boolean				fHasThread = false;
	Boolean				fRunning = false;		// holds the baton (or has just been handed it)
};

static std::mutex						gBaton;
static std::condition_variable			gBatonChanged;
static std::map<TTask*, HostTaskContext*>	gContexts;
static TTask*							gRunningTask = nil;		// whose thread holds the baton
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
	gBatonChanged.wait(lock, [&] { return ctx->fRunning || gStopRequested; });
	if (gStopRequested)
	{
		// the run is over; this thread has nothing more to do and is left here
		gBatonChanged.wait(lock, [] { return false; });
	}
	gRunningTask = task;
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
	gBatonChanged.notify_all();
	WaitForBaton(self, lock);
}


void
HostDeliverInterrupts()
{
	Int64 now;
	GetClock(&now);
	if (gHostAlarmArmed && CompCompare(&now, &gHostAlarmTime) >= 0)
	{
		gHostAlarmArmed = false;
		TimerInterruptHandler();
	}
	if (gHostTimeSliceArmed && gHostInterruptEnabled && CompCompare(&now, &gHostTimeSliceDeadline) >= 0)
	{
		gHostTimeSliceArmed = false;
		PreEmptiveTimerInterruptHandler();
	}
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
	if (self->fRegister[kcPC] != marker)
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


// ROM 0x001ce924 SleepTask__Fv
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
	// this thread is a task's: it parks here for good
	gBatonChanged.wait(lock, [] { return false; });
}


void
HostTaskDeleted(TTask* task)
{
	std::unique_lock<std::mutex> lock(gBaton);
	// the thread, if any, is parked in WaitForBaton and never runs again; its
	// context stays allocated for it, only the task is forgotten
	gContexts.erase(task);
}
