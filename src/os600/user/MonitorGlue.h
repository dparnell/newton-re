/*
	File:		MonitorGlue.h

	Contains:	User-mode entry points the kernel makes a task resume at.  In the
				ROM they are hand-written assembly in the user-side library; the
				kernel only ever stores their addresses into a task's saved pc.

				MonitorEntryGlue	0x0038ac98	a monitor task starts here for each
										call: r0 = monitor object, r1 = selector,
										r2 = user object, r3 = the monitor proc.
										Checks it is in user mode ("Zot!  Check
										SVC mode in MonitorEntryGlue"), calls the
										proc, then issues MonitorExitSWI with its
										result.
				TaskKillSelf		0x0038ad2c	asks the object manager monitor
										(GetPortInfo(0), selector 0xff, r2 = own
										task id) to delete the calling task.
				(Throw, which MonitorThrowKernelGlue makes a caller resume at, is
				the exception system's: user/Exceptions.cpp.)

				The host build supplies bodies that only report they were reached:
				a task's saved registers are never actually resumed on the host yet.
*/

#ifndef __MONITORGLUE_H
#define __MONITORGLUE_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

extern "C" {
void	MonitorEntryGlue(void);
void	TaskKillSelf(void);
void	BadExit(void);				// where a task proc that returns ends up: TaskKillSelf (0x003a4ad8, a branch)
}

#endif	/* __MONITORGLUE_H */
