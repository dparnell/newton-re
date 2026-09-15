/*
	File:		Monitor.cpp

	Contains:	TMonitor and the monitor system calls.

	Reconstructed from the MP2100 D ROM; each function cites its origin.

	The monitor task's registers are set up by hand: MonitorEntryGlue expects
	r0 = fMonitorObject, r1 = selector, r2 = userObject and r3 = fProc, and
	starts on a fresh stack (sp = fGlobalsBase).  The caller's saved r0 carries
	the result back; a fault monitor's caller keeps its r0 when the result is 0.
*/

#include "Monitor.h"
#include "KernelGlobals.h"
#include "KernelObjects.h"
#include "ObjectTable.h"
#include "Port.h"
#include "Reboot.h"
#include "Scheduler.h"
#include "OSErrors.h"
#include "hal/Atomic.h"
#include "MonitorGlue.h"

#include <stddef.h>
#include <stdint.h>


// register numbers in TTask::fRegister
enum { kcR0 = 0, kcR1, kcR2, kcR3, kcR11 = 11, kcSP = 13, kcPC = 15 };

// the registers a fault monitor sees through fMsgId: r0-r15, the PSR and the
// words up to and including the state, 100 bytes in the ROM
const ULong kFaultRegisterBlockSize = offsetof(TTask, fUnknown70) + sizeof(ULong) - offsetof(TTask, fRegister);

const ULong kPSRModeMask = 0x1f;
const ULong kUserMode = 0x10;

static inline ULong RegisterFromPointer(const void* p)	{ return (ULong) (uintptr_t) p; }


static void
DeleteTaskOnMonitorQProc(void* monitor, char* task)
{
	((TMonitor*) monitor)->DeleteTaskOnMonitorQ((TTask*) task);
}


// ROM 0x001215c8 __ct__8TMonitorFv
TMonitor::TMonitor()
	: fQueue(offsetof(TTask, fMonitorQItem), DeleteTaskOnMonitorQProc, this)
{
	fQueueCount = 0;
	fMonitorObject = nil;
	fCaller = nil;
	fProc = nil;
	fMsgId = 0;
	fSuspended = 0;
	fMonitorTaskId = 0;
	fCallerIsCopying = false;
}


// ROM 0x00121a28 __dt__8TMonitorFv
// Fails every waiter, then lets the object table destroy the message and the
// monitor task.
TMonitor::~TMonitor()
{
	Suspend(kMonitor_Suspended);
	gObjectTable->Remove(fMsgId);
	gObjectTable->Remove(fMonitorTaskId);
}


// ROM 0x00121630 FlushTasksOnMonitor__8TMonitorFv
// Every waiter runs again with whatever its r0 held; the current call is left alone.
void
TMonitor::FlushTasksOnMonitor()
{
	TTask* task;
	while ((task = (TTask*) fQueue.Remove()) != nil)
	{
		fQueueCount--;
		ScheduleTask(task);
	}
}


// ROM 0x00121890 SetCallerRegister__8TMonitorFiUl
void
TMonitor::SetCallerRegister(int reg, ULong value)
{
	if (fCaller != nil)
		fCaller->fRegister[reg] = value;
}


// ROM 0x00121a78 SetResult__8TMonitorFP5TTaskl
// A fault monitor answering 0 leaves the faulting task's r0 as it was.
void
TMonitor::SetResult(TTask* task, long result)
{
	if ((task->fState & kTaskState_FaultMonitorCall) && result == 0)
		return;
	task->fRegister[kcR0] = result;
}


// ROM 0x00121a94 Suspend__8TMonitorFUl
// Waiters are failed with kError_No_Such_Monitor only while a call is in
// progress (the queue is empty otherwise).  Returns true if nothing is inside.
Boolean
TMonitor::Suspend(ULong flags)
{
	fSuspended |= flags;
	if (fCaller != nil)
	{
		TTask* task;
		while ((task = (TTask*) fQueue.Remove()) != nil)
		{
			SetResult(task, kError_No_Such_Monitor);
			ScheduleTask(task);
			fQueueCount--;
		}
		if (fCaller != nil)
			return false;
	}
	return true;
}


// ROM 0x00121b28 DeleteTaskOnMonitorQ__8TMonitorFP5TTask
void
TMonitor::DeleteTaskOnMonitorQ(TTask* task)
{
	EnterAtomic();
	fQueueCount--;
	ExitAtomic();
}


// ROM 0x00121b50 SetUpEntry__8TMonitorFP5TTask
// Points the monitor task at the caller's request and schedules it.  A
// kSuspendMonitor request is answered here (result 0) without running the
// proc: returns false and the caller is not the current call.  Nothing is
// dispatched once a safe reboot is waiting (Reboot); the caller stays blocked.
Boolean
TMonitor::SetUpEntry(TTask* caller)
{
	if ((caller->fState & kTaskState_FaultMonitorCall) == 0)
	{
		if (caller->fRegister[kcR1] == (ULong) kSuspendMonitor)
		{
			SetResult(caller, noErr);
			fQueueCount--;
			return false;
		}
		fMonitorTask->fRegister[kcR1] = caller->fRegister[kcR1];
	}
	else
	{
		SMemSetBufferKernelGlue(fMsgId, &caller->fRegister[0], kFaultRegisterBlockSize, kSMemReadOnly);
		fMonitorTask->fRegister[kcR1] = (ULong) kMonitorFaultSelector;
	}

	EnterAtomic();
	if (gWantReboot)
	{
		ExitAtomic();
		return true;
	}
	if (fRebootProtected)
		gRebootProtectCount++;
	ExitAtomic();

	fCaller = caller;
	caller->fInsideMonitorId = fId;
	fCallerIsCopying = caller->fCopyMemId != 0;
	fMonitorTask->fRegister[kcPC] = RegisterFromPointer((void*) MonitorEntryGlue);
	fMonitorTask->fRegister[kcR3] = RegisterFromPointer((void*) fProc);
	fMonitorTask->fRegister[kcR2] = caller->fRegister[kcR2];
	fMonitorTask->fRegister[kcR0] = RegisterFromPointer(fMonitorObject);
	fMonitorTask->fRegister[kcR11] = caller->fRegister[kcR11];
	fMonitorTask->fRegister[kcSP] = fMonitorTask->fGlobalsBase;
	fMonitorTask->fMonitorCaller = caller;
	ScheduleTask(fMonitorTask);
	return true;
}


// ROM 0x00121c94 Aquire__8TMonitorFv
// The current task calls the monitor with the selector in its saved r1 and
// the user object in r2.  It leaves the run queues; if the monitor is idle its
// task is dispatched at once, otherwise the caller waits its turn.  A suspend
// request may only come from the owner and takes effect immediately for
// anyone arriving after it.
NewtonErr
TMonitor::Aquire()
{
	TTask* caller = gCurrentTask;
	if (fSuspended & (kMonitor_Suspended | kMonitor_SuspendRequested))
		return kError_No_Such_Monitor;

	if ((caller->fState & kTaskState_FaultMonitorCall) == 0 && caller->fRegister[kcR1] == (ULong) kSuspendMonitor)
	{
		if (caller->fOwnerId != fOwnerId)
			return kError_Object_Not_Owned_By_Task;
		Suspend(kMonitor_SuspendRequested);
	}

	UnScheduleTask(caller);
	if (++fQueueCount == 1)
	{
		if (!SetUpEntry(caller))
			ScheduleTask(caller);
	}
	else
		fQueue.Add(caller);
	return noErr;
}


// ROM 0x00121d4c Release__8TMonitorFl
// The monitor task has finished a call.  Its caller is normally rescheduled
// with the result and preferred to run next; a caller that has been killed
// meanwhile, or a faulting task the fault monitor condemned, resumes in
// TaskKillSelf instead, and one waiting for memory is parked on
// gBlockedOnMemory.  The monitor task stops and the next waiter is dispatched
// (SetUpEntry, inlined in the ROM).  A reboot that waited for this
// reboot-protected call happens here.
Boolean
TMonitor::Release(long result)
{
	EnterAtomic();
	if (fRebootProtected && --gRebootProtectCount == 0 && gWantReboot)
		Restart();
	ExitAtomic();

	TTask* caller = fCaller;
	ULong resumePC = caller->fRegister[kcPC];
	if (caller->fState & (kTaskState_KillPending | kTaskState_KilledSelf))
		resumePC = RegisterFromPointer((void*) TaskKillSelf);
	else if ((caller->fState & kTaskState_FaultMonitorCall) && result == kFaultMonitorResult_Kill)
		resumePC = RegisterFromPointer((void*) TaskKillSelf);
	else if ((caller->fState & kTaskState_FaultMonitorCall) && result == kFaultMonitorResult_BlockOnMemory)
		gBlockedOnMemory->Add(caller);
	else
	{
		gKernelScheduler->fPreferredTask = caller;
		ScheduleTask(caller);
		SetResult(caller, result);
	}
	caller->fRegister[kcPC] = resumePC;
	caller->fInsideMonitorId = 0;
	fQueueCount--;
	UnScheduleTask(fMonitorTask);
	fCaller = nil;

	if (fSuspended & kMonitor_Suspended)
		return true;
	TTask* next = (TTask*) fQueue.Remove();
	if (next == nil)
		return false;
	// NOTE: the ROM does not reschedule `next` when SetUpEntry answers a
	// kSuspendMonitor request from the queue (Aquire does); that waiter
	// would never run.  Reproduced as found.
	return SetUpEntry(next);
}


/* -------------------------------------------------------------------------------
	System calls.  The SWI handler has saved all of the caller's registers in
	gCurrentTask; the value returned here reaches the caller in r0 unless a
	task switch intervenes, in which case its saved r0 is what it sees.
------------------------------------------------------------------------------- */

// ROM 0x00121678 MonitorDispatchKernelGlue
// SWI 27: r0 = monitor id, r1 = selector, r2 = user object.  Coming through
// here (rather than from the abort handler) it is never a fault call.
NewtonErr
MonitorDispatchKernelGlue()
{
	TMonitor* monitor;
	NewtonErr err = ConvertIdToObj(kMonitorType, gCurrentTask->fRegister[kcR0], &monitor);
	if (err == noErr)
	{
		gCurrentTask->fState &= ~kTaskState_FaultMonitorCall;
		err = monitor->Aquire();
	}
	return err;
}


// ROM 0x00121748 MonitorExitKernelGlue
// SWI 28, from MonitorEntryGlue when the proc returns: the monitor task's
// current monitor gives the result to the caller.
NewtonErr
MonitorExitKernelGlue(long result)
{
	TMonitor* monitor;
	NewtonErr err = ConvertIdToObj(kMonitorType, gCurrentMonitorId, &monitor);
	if (err == noErr)
		monitor->Release(result);
	return err;
}


// ROM 0x0012178c MonitorThrowKernelGlue
// SWI 29: an exception escaped the monitor proc.  The caller is made to
// resume in Throw with the same (name, data, destructor) so it unwinds there -
// provided it was in user mode; a caller in the middle of a shared-memory
// copy has the copy failed with kError_UnResolvedFault instead.
NewtonErr
MonitorThrowKernelGlue(char* name, void* data, void (*destructor)(void*))
{
	TMonitor* monitor;
	NewtonErr err = ConvertIdToObj(kMonitorType, gCurrentMonitorId, &monitor);
	if (err != noErr)
		return err;

	long result = (long) RegisterFromPointer(name);
	if (!monitor->fCallerIsCopying)
	{
		if ((monitor->fCaller->fPSR & kPSRModeMask) != kUserMode)
		{
			CantThrowInUndefinedModeReboot();
			return noErr;
		}
		monitor->SetCallerRegister(kcR1, RegisterFromPointer(data));
		monitor->SetCallerRegister(kcR2, RegisterFromPointer((void*) destructor));
		monitor->SetCallerRegister(kcPC, RegisterFromPointer((void*) Throw));
	}
	else
	{
		LowLevelCopyDoneFromKernelGlue(kError_UnResolvedFault, monitor->fCaller, monitor->fCaller->fRegister[kcPC]);
		result = kError_UnResolvedFault;
	}
	monitor->Release(result);
	return noErr;
}


// ROM 0x0012185c MonitorFlushKernelGlue
// SWI 32.
NewtonErr
MonitorFlushKernelGlue(TObjectId monitorId)
{
	TMonitor* monitor;
	NewtonErr err = ConvertIdToObj(kMonitorType, monitorId, &monitor);
	if (err == noErr)
		monitor->FlushTasksOnMonitor();
	return err;
}


// ROM 0x0014a4e8 DeleteMonitor__FP8TMonitor
void
DeleteMonitor(TMonitor* monitor)
{
	if (monitor != nil)
		delete monitor;
}
