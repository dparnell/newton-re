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
struct InterruptObject;

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

#endif	/* __KERNELGLOBALS_H */
