/*
	File:		TaskSwitch.h

	Contains:	What the SWI and interrupt exit paths call when it is time to
				pick another task: Scheduler() chooses it and charges the task
				that ran for its time and memory, SwapInGlobals makes the chosen
				task current, DoDeferrals runs the work interrupt handlers put off
				(deferred sends, timer completions).  The accounting system calls
				live here too.

				SWIExitSchedule is the decision half of the SWI/interrupt exit path
				(SWIBoot 0x003a40d0-, assembly): deferrals, the reschedule, the
				time slice, and which task runs next; DomainAccessFor is the
				domain access word it loads for that task.  The other half - save
				the old task's registers, load the new one's - is the context
				switch proper and belongs to the HAL (or, on a host, the task
				runtime, docs/host-runtime.md).

	Reconstructed from:	Scheduler 0x001ce5c0, SwapInGlobals 0x00250214,
				DoDeferrals 0x00149df4, ResetAccountTimeKernelGlue 0x0025010c,
				GetNextTaskIdKernelGlue 0x00250194, SWIBoot exit path 0x003a40d0
*/

#ifndef __TASKSWITCH_H
#define __TASKSWITCH_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

class TTask;

TTask*		Scheduler();								// the task to run next (may be the same one)
TTask*		SWIExitSchedule();							// the exit path's decisions; returns the task to run, now gCurrentTask
ULong		DomainAccessFor(TTask* task);				// the domain access word to run it with
void		SwapInGlobals(TTask* task);					// make it current: gCurrentTaskId, gCurrentGlobals, gCurrentMonitorId
void		DoDeferrals();								// run what interrupt level deferred (gWantDeferred)

NewtonErr	ResetAccountTimeKernelGlue();				// GenericSWI 5: zero every task's run time
NewtonErr	GetNextTaskIdKernelGlue(TObjectId afterId, TObjectId* outId);	// GenericSWI 6: walk the tasks

#endif	/* __TASKSWITCH_H */
