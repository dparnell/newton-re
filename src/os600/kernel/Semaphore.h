/*
	File:		Semaphore.h

	Contains:	Kernel semaphores.  A TSemaphoreGroup (kSemGroupType) owns an
				array of counting TSemaphores; a TSemaphoreOpList (kSemListType)
				is a list of (semaphore number, delta) operations - see
				MAKESEMLISTITEM in KernelTypes.h - applied atomically to a
				group by SemOp: every operation must succeed, or the whole list
				is unwound and the calling task blocks (kWaitOnBlock) or gets
				kError_Semaphore_Would_Cause_Block (kNoWaitOnBlock).

				A delta of 0 waits for the semaphore to be zero; a negative
				delta that would take it below zero waits for an increment.
				This is what the user-side TUSemaphoreGroup / TULockingSemaphore
				/ TURdWrSemaphore (UserSemaphore.h) are built on, through SWI 11
				(DoSemaphoreOp).

	Reconstructed from:	TSemaphore 0x001d7504-0x001d769c, TSemaphoreGroup 0x001d7260-0x001d7478,
				TSemaphoreOpList 0x001d747c-0x001d7500, DoSemaphoreOp 0x001d70e8
*/

#ifndef __SEMAPHORE_H
#define __SEMAPHORE_H

#ifndef __TASK_H
#include "Task.h"
#endif
#ifndef __KERNELTYPES_H
#include "KernelTypes.h"
#endif

const ULong kTaskState_BlockedOnSemaphore = 0x00100000;		// TTask::fState bit while queued on a TSemaphore


// ROM size 0x28
class TSemaphore : public TKernelObject, public TTaskContainer
{
	public:
						TSemaphore();
						~TSemaphore();

		virtual TTaskContainer*	Remove(TTask* task);		// stop the task waiting here

		void			BlockOnZero(TTask* task, SemFlags flags);
		void			BlockOnInc(TTask* task, SemFlags flags);
		void			WakeTasksOnZero();
		void			WakeTasksOnInc();

		long			fValue;			// +0x14
		TTaskQueue		fZeroTasks;		// +0x18  tasks waiting for fValue to reach zero
		TTaskQueue		fIncTasks;		// +0x20  tasks waiting for fValue to be incremented
};


// ROM size 0x18
class TSemaphoreOpList : public TKernelObject
{
	public:
						~TSemaphoreOpList();

		NewtonErr		Init(ULong count, ULong* ops);

		ULong*			fOps;			// +0x10  MAKESEMLISTITEM(semNum, delta) entries
		ULong			fCount;			// +0x14
};


// ROM size 0x1c
class TSemaphoreGroup : public TKernelObject
{
	public:
						~TSemaphoreGroup();

		NewtonErr		Init(ULong count);
		NewtonErr		SemOp(TSemaphoreOpList* list, SemFlags flags, TTask* task);
		void			UnWindOp(TSemaphoreOpList* list, long count);

		TSemaphore*		fSemaphores;	// +0x10
		ULong			fCount;			// +0x14
		void*			fRefCon;		// +0x18  user data (SemGroupSetRefCon / GetRefCon)
};


// SWI 11: apply an op list to a group on behalf of a task
NewtonErr	DoSemaphoreOp(TObjectId groupId, TObjectId listId, SemFlags flags, TTask* task);

// GenericSWI 40 / 41 (kernel-mode side; the user-mode side issues the SWI)
NewtonErr	SemGroupSetRefCon(TObjectId groupId, void* refCon);
NewtonErr	SemGroupGetRefCon(TObjectId groupId, void** outRefCon);

// destructors used by ObjectScavenger
void		DeleteSemList(TSemaphoreOpList* list);
void		DeleteSemGroup(TSemaphoreGroup* group);

// sets the result a blocked task will see when it resumes (its r0)
void		MarkMessageDone(TTask* task, long result);

#endif	/* __SEMAPHORE_H */
