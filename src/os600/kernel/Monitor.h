/*
	File:		Monitor.h

	Contains:	TMonitor, the kernel's synchronous-call mechanism (kMonitorType).
				A monitor owns a task that runs its proc on behalf of callers one
				at a time.  A caller (MonitorDispatchSWI) is taken off the run
				queues; if the monitor is idle its task is pointed at
				MonitorEntryGlue with the call's arguments and scheduled, otherwise
				the caller waits in fQueue (through TTask::fMonitorQItem).  When the
				proc returns (MonitorExitSWI) the caller gets the result in r0 and
				runs next, and the next waiter is dispatched.

				Fault monitors are called by the abort handler on behalf of the
				faulting task (kTaskState_FaultMonitorCall): the monitor's shared
				memory message fMsgId is pointed at the caller's saved registers so
				the handler can inspect and patch them, the selector is
				kMonitorFaultSelector, and the result decides the caller's fate
				(kFaultMonitorResult_*).

				Suspend: a call with selector kSuspendMonitor from the monitor's
				owner marks it suspended; later callers get kError_No_Such_Monitor
				and queued ones are failed.  Destroying a monitor suspends it fully.

				A reboot-protected monitor holds gRebootProtectCount while a call
				is inside it; Reboot(..., safe) waits for the count to drop.

				User side: TUMonitor (UserMonitor.h).

	Reconstructed from:	TMonitor 0x001215c8-0x00121d4c, MonitorDispatchKernelGlue
				0x00121678 and the other glue that follows it, DeleteMonitor
				0x0014a4e8, MonitorDispatchSWI case 0x0038abf8
*/

#ifndef __MONITOR_H
#define __MONITOR_H

#ifndef __TASK_H
#include "Task.h"
#endif
#ifndef __SHAREDTYPES_H
#include "SharedTypes.h"		// MonitorProcPtr, kSuspendMonitor, kMonitorFaultSelector
#endif

#include <stddef.h>

// TMonitor::fSuspended bits
enum
{
	kMonitor_Suspended			= 1,	// ~TMonitor / DeleteMonitor: refuse all calls, fail the queue
	kMonitor_SuspendRequested	= 2		// a kSuspendMonitor call is pending or done
};

// the registers a fault monitor sees through fMsgId: r0-r15, the PSR and the
// words up to and including the state, 100 bytes in the ROM
const ULong kFaultRegisterBlockSize = offsetof(TTask, fUnknown70) + sizeof(ULong) - offsetof(TTask, fRegister);

// results a fault monitor's proc can return, interpreted by TMonitor::Release;
// anything else is handed to the faulting task as r0 (0: leave r0 alone)
const long kFaultMonitorResult_Kill = 4;			// the faulting task resumes in TaskKillSelf
const long kFaultMonitorResult_BlockOnMemory = 5;	// it is parked on gBlockedOnMemory until a page arrives


// ROM size 0x48
class TMonitor : public TKernelObject
{
	public:
						TMonitor();
						~TMonitor();
		NewtonErr		Init(MonitorProcPtr proc, ULong stackSize, void* monitorObject, TEnvironment* environment, Boolean faultMonitor, ULong name, Boolean rebootProtected);

		NewtonErr		Aquire();						// [sic] enter from gCurrentTask; blocks it
		Boolean			Release(TRegister result);		// leave: resume the caller, dispatch the next (result: a register's worth, `long` in the ROM)
		Boolean			Suspend(ULong flags);			// true when no call is in progress
		void			FlushTasksOnMonitor();			// let all waiters go, unanswered
		void			SetCallerRegister(int reg, TRegister value);

		void			SetResult(TTask* task, TRegister result);
		Boolean			SetUpEntry(TTask* caller);		// false if the entry was answered without running the proc
		void			DeleteTaskOnMonitorQ(TTask* task);	// fQueue's destructor: a waiter died

		ULong			fQueueCount;		// +0x10  callers inside or waiting, including fCaller
		ULong			fSuspended;			// +0x14  kMonitor_* bits
		void*			fMonitorObject;		// +0x18  first argument to fProc
		TObjectId		fMsgId;				// +0x1c  the monitor's TSharedMemMsg; kBuiltInSMemMonitorFaultId resolves to it
		TTask*			fCaller;			// +0x20  the task whose call is running, nil when idle
		TDoubleQContainer fQueue;			// +0x24  callers waiting to enter (links TTask::fMonitorQItem)
		MonitorProcPtr	fProc;				// +0x38
		TTask*			fMonitorTask;		// +0x3c  the task that executes fProc
		TObjectId		fMonitorTaskId;		// +0x40
		Boolean			fFaultMonitor;		// +0x44
		Boolean			fRebootProtected;	// +0x45
		Boolean			fCallerIsCopying;	// +0x46  fCaller has a shared-memory copy in progress (fCopyMemId)
};


// system calls (SWI 27, 28, 29, 32); the SWI handler saves the caller's
// registers in gCurrentTask first
NewtonErr	MonitorDispatchKernelGlue();									// monitor id, selector, user object in r0-r2
NewtonErr	MonitorExitKernelGlue(long result);
NewtonErr	MonitorThrowKernelGlue(char* name, void* data, void (*destructor)(void*));
NewtonErr	MonitorFlushKernelGlue(TObjectId monitorId);

void		DeleteMonitor(TMonitor* monitor);								// the object table's destructor for kMonitorType

#endif	/* __MONITOR_H */
