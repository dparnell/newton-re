/*
	File:		TaskGlobals.h

	Contains:	The per-task globals block.  TTask::Init lays it out at the top
				of every task's stack, just below the copy of the task's object;
				gCurrentGlobals (TTask::fGlobals, SwapInGlobals) points at its END,
				and code reaches the fields at negative offsets from there - the
				exception system's gFirstCatch is the last word
				(NewtonExceptions.h: GetGlobals() - sizeof(ExceptionGlobals)),
				the memory manager keeps the task's current heap at -0x10.

	Reconstructed from:	TTask::Init 0x00250368 (the block it builds), the heap
				code's uses of gCurrentGlobals (e.g. operator delete 0x00144e60)
*/

#ifndef __TASKGLOBALS_H
#define __TASKGLOBALS_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif
#ifndef __NEWTONEXCEPTIONS_H
#include "NewtonExceptions.h"
#endif

// ROM size 0x54
struct TaskGlobals
{
	ULong				fUnknown00[12];		// +0x00  not written by TTask::Init
	ULong				fUnknown30;			// +0x30  0
	TObjectId			fHeapDomainId;		// +0x34  the environment's heap domain
	VAddr				fStackTop;			// +0x38
	VAddr				fStackBase;			// +0x3c
	TObjectId			fTaskId;			// +0x40
	void*				fCurrentHeap;		// +0x44  the environment's heap; the memory manager's SetHeap changes it
	ULong				fUnknown48;			// +0x48  0; the memory manager clears it after a free
	ULong				fTaskName;			// +0x4c
	ExceptionGlobals	fExceptionGlobals;	// +0x50  firstCatch
};

// the size the ROM reserves for the block (0x54 on the MessagePad)
const ULong kTaskGlobalsSize = sizeof(TaskGlobals);

#endif	/* __TASKGLOBALS_H */
