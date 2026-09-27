/*
	File:		KernelGlobals.h

	Contains:	Kernel-wide global variables.  In the ROM these live in the
				kernel data area at 0x0C100800 (RAM_RW / RAM_ZI in Ghidra) and
				carry these names in the symbol table.

				Defined in KernelGlobals.cpp; the boot code initialises them.
*/

#ifndef __KERNELGLOBALS_H
#define __KERNELGLOBALS_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

class TObjectTable;
class TTask;
class TScheduler;
class TTimerEngine;
class TDoubleQContainer;
class TUObject;
class TMonitor;
class TPort;
struct InterruptObject;
struct SGlobalsThatLiveAcrossReboot;

extern TObjectTable*	gObjectTable;				// 0x0c1010b8  the kernel object table
extern TObjectTable*	gTheMemArchObjTbl;			// 0x0c101254  physical pages (kPhysType/kExtPhysType) live here
extern TTask*			gCurrentTask;				// 0x0c1010e8  task currently running
extern ULong			gNextGlobalUniqueId;		// 0x0c102638  last object number handed out
extern Boolean			gWrappedGlobalUniqueId;		// 0x0c102634  set once the object numbers have wrapped

// scheduling
extern TScheduler*		gKernelScheduler;			// 0x0c1010c0
extern TTask*			gIdleTask;					// 0x0c1010b4  runs when nothing else is ready
extern ULong			gTaskPriority;				// 0x0c101b5c  priority of the running task
extern Boolean			gSchedule;					// 0x0c1010d4  a reschedule is due
extern Boolean			gScheduleRequested;			// 0x0c1010c4  ...but was held off (gHoldScheduleLevel)
extern ULong			gHoldScheduleLevel;			// 0x0c1010c8  nesting count of "no rescheduling now"
extern Boolean			gWantSchedulerToRun;		// 0x0c101c08  the time-slice timer should be (re)started
extern Boolean			gSchedulerRunning;			// 0x0c101c0c  the time-slice timer is armed
extern InterruptObject*	gSchedulerIntObj;			// 0x0c100f58  its interrupt source

// timers
extern TTimerEngine*	gTimerEngine;				// 0x0c1010cc  (address per literal pool use)
extern TDoubleQContainer* gTimerDeferred;			// 0x0c101138  expired messages awaiting completion by the scheduler path
extern Boolean			gWantDeferred;				// 0x0c101118  gTimerDeferred is not empty
extern ULong			gTimerInterruptCount;		// 0x0c101664  statistics

// ports and shared memory
extern TDoubleQContainer* gCopyTasks;				// 0x0c101124  tasks in the middle of a shared-memory copy (TTask::fCopyQItem)
extern TDoubleQContainer* gDeferredSends;			// 0x0c10112c  sends made from interrupt level (TSharedMemMsg::fTimerQItem)
extern Boolean			gCopyDone;					// 0x0c101130  the current task's copy finished (checked by the SWI handler)
extern TPort*			gNullPort;					// 0x0c1010bc  the well-known ports GetPortInfo hands out; the null port never receives
extern TUObject*		gNameServer;				// 0x0c101654
extern TMonitor*		gTheObjectManagerMonitor;	// 0x0c101794  the monitor that makes and destroys kernel objects (ObjectManager.h)

// monitors and reboot
extern TObjectId		gCurrentMonitorId;			// 0x0c101148  monitor whose task is running (MonitorExit/Throw act on it)
extern TDoubleQContainer* gBlockedOnMemory;			// 0x0c101128  tasks parked by a fault monitor (TTask::fMonitorQItem)
extern ULong			gRebootProtectCount;		// 0x0c10113c  reboot-protected monitor calls in progress
extern Boolean			gWantReboot;				// 0x0c101140  a safe Reboot is waiting for them to finish
extern SGlobalsThatLiveAcrossReboot gGlobalsThatLiveAcrossReboot;	// 0x0c103490

// the running task and its accounts (TaskSwitch.*)
extern TObjectId		gCurrentTaskId;				// 0x0c101144  set by SwapInGlobals
extern void*			gCurrentGlobals;			// 0x0c10114c  the running task's globals block (TTask::fGlobals)
extern TTask*			gCurrentMemCountTask;		// 0x0c1010f0  task being charged for heap use
extern ULong			gPtrsUsed;					// 0x0c101104  the heap's running totals (memory manager)
extern ULong			gHandlesUsed;				// 0x0c101100
extern ULong			gSavedPtrsUsed;				// 0x0c10110c  ...as they were at the last task swap
extern ULong			gSavedHandlesUsed;			// 0x0c101108
extern ULong			gNumberOfTaskSwaps;			// 0x0c101c10
extern Boolean			gCountTaskTime;				// 0x0c1010f8  account run time per task (off in the ROM image)
extern TTask*			gCurrentTimedTask;			// 0x0c1010ec  task being charged for time
extern Int64			gTaskTimeStart;				// 0x0c101110  when it started running
extern Int64			gLastTaskEndTime;			// 0x0c101fb4
extern Int64			gFirstTaskEndTime;			// 0x0c101fac
extern Boolean			gTaskEndTimeInvalid;		// 0x0c101c14  true until the first swap has been timed
extern Int64			gDeadTaskTime;				// 0x0c101fa4  run time of tasks that no longer exist (~TTask)
extern ULong			gFIQInterruptOverHead;		// 0x0c100f38  ticks spent in interrupt handlers since the last swap
extern ULong			gIRQInterruptOverHead;		// 0x0c100f3c
extern ULong			gFIQAccumulatedIntOverHead;	// 0x0c101c1c  ...and altogether
extern ULong			gIRQAccumulatedIntOverHead;	// 0x0c101c18

// boot
extern ULong			gMainCPUType;				// 0x0c1008dc  the processor (LowLevelGetCPUType: 3 a StrongARM)
extern Fixed			gMainCPUClockSpeed;			// 0x0c1008e0  its clock in MHz, 16.16 (Gestalt's fCpuSpeed)
extern Boolean			gOSIsRunning;				// 0x0c10111c  set by UserBoot once the stack manager and heaps exist (TTask::Init)

// object manager
extern Boolean			gTaskDestroyed;				// 0x0c10178c  a task was deleted: scavenge what it owned (TObjectManager::MonitorProc)
extern ULong			gMonitorTaskPriority;		// 0x0c101624  priority of monitor tasks (TMonitor::Init)

#endif	/* __KERNELGLOBALS_H */
