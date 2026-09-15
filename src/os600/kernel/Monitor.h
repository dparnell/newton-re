/*
	File:		Monitor.h

	Contains:	TMonitor, the kernel's synchronous-call mechanism (kMonitorType).
				A monitor owns a task that runs its proc on behalf of callers
				one at a time; callers queue on it (through TTask::fMonitorQItem)
				and are resumed with the result.  User side: TUMonitor
				(UserMonitor.h), through MonitorDispatchSWI / MonitorExitSWI.

				Only the layout is here for now; the methods
				(0x001215c8-0x00121d4c) follow.

	Reconstructed from:	TMonitor::TMonitor 0x001215c8, TMonitor::Init 0x00121900,
				LocalToGlobalId 0x00193ea0
*/

#ifndef __MONITOR_H
#define __MONITOR_H

#ifndef __TASK_H
#include "Task.h"
#endif

typedef void (*MonitorProcPtr)(void* monitorObject, ULong selector, void* userObject);


// ROM size 0x48
class TMonitor : public TKernelObject
{
	public:
		ULong			fUnknown10;			// +0x10
		ULong			fUnknown14;			// +0x14
		void*			fMonitorObject;		// +0x18  first argument to fProc
		TObjectId		fMsgId;				// +0x1c  the monitor's TSharedMemMsg; kBuiltInSMemMonitorFaultId resolves to it
		ULong			fUnknown20;			// +0x20
		TDoubleQContainer fQueue;			// +0x24  callers waiting to enter (links TTask::fMonitorQItem, destructor DeleteTaskOnMonitorQ)
		MonitorProcPtr	fProc;				// +0x38
		TTask*			fMonitorTask;		// +0x3c  the task that executes fProc
		TObjectId		fMonitorTaskId;		// +0x40
		Boolean			fFaultMonitor;		// +0x44
		Boolean			fRebootProtected;	// +0x45
		Boolean			fUnknown46;			// +0x46
};

#endif	/* __MONITOR_H */
