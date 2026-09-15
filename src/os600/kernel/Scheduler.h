/*
	File:		Scheduler.h

	Contains:	TScheduler, the kernel's priority scheduler, and the C
				entry points around it.

				Tasks ready to run sit in one of 32 FIFO buckets, one per
				priority; fPriorityMask has a bit set for each non-empty
				bucket and fCurrentBucket is the highest of them.  Scheduling
				picks the head of the highest bucket (or a task another part
				of the kernel has nominated in fPreferredTask), the idle task
				when nothing is ready.  A timer interrupt (StartScheduler) makes
				the running task give way to equal-priority tasks.

				Layout note: the vptr sits at +0x10, after the 16-byte
				TKernelObject prefix - this compiler lays bases out in
				declaration order and puts the vptr after them.  (Our host
				build does not reproduce that; nothing depends on it.)

	Reconstructed from:	TScheduler 0x001ce518-0x001cebc4, ScheduleTask 0x00193908,
				UnScheduleTask 0x0019391c, WantSchedule 0x001cebc8,
				StartScheduler 0x001ce87c, StopScheduler 0x001ce8e8
*/

#ifndef __SCHEDULER_H
#define __SCHEDULER_H

#ifndef __TASK_H
#include "Task.h"
#endif


// ROM size 0x120
class TScheduler : public TKernelObject, public TTaskContainer
{
	public:
						TScheduler();

		void			Add(TTask* task);
		void			AddWhenNotCurrent(TTask* task);
		virtual TTaskContainer*	Remove(TTask* task);
		TTask*			RemoveHighestPriority();
		TTask*			Schedule();
		void			UpdateCurrentBucket();

	private:
		ULong			fCurrentBucket;					// +0x14  highest priority with a ready task
		ULong			fPriorityMask;					// +0x18  bit n set when fQueue[n] is not empty
		TTaskQueue		fQueue[kNumberOfPriorities];	// +0x1c
	public:
		TTask*			fPreferredTask;					// +0x11c task to run next, if it is at the current priority
														//        (set directly by message completion, TSharedMemMsg::CompleteMsg)
};


// C entry points used throughout the kernel (they act on gKernelScheduler)
void	ScheduleTask(TTask* task);				// GenericSWI 26
void	UnScheduleTask(TTask* task);
void	WantSchedule();							// ask for a reschedule at the next opportunity
void	HoldSchedule();							// defer reschedules until the matching AllowSchedule
void	AllowSchedule();
void	StartScheduler();						// arm the time-slice interrupt
void	StopScheduler();

#endif	/* __SCHEDULER_H */
