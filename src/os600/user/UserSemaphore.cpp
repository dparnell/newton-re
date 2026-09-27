/*
	File:		user/UserSemaphore.cpp

	Contains:	TUSemaphoreOpList, TUSemaphoreGroup, TULockingSemaphore and
				TURdWrSemaphore (UserSemaphore.h).  A group is an array of
				counting semaphores in the kernel; an op list is a set of
				(semaphore, delta) operations applied all-or-nothing by
				SemaphoreOpGlue (SWI 11).  The locking semaphore keeps a word in
				user memory (the group's ref con) that is swapped atomically so an
				uncontended acquire never enters the kernel; the read/write
				semaphore is two counters with fixed op lists.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "UserSemaphore.h"
#include "UserGlobals.h"
#include "os600/ObjectMessage.h"
#include "os600/GenericSWISelectors.h"
#include "OSErrors.h"
#include "hal/Atomic.h"

#include <stdarg.h>
#include <stdlib.h>

TUSemaphoreOpList	TULockingSemaphore::faquireOP;
TUSemaphoreOpList	TULockingSemaphore::freleaseOP;
TUSemaphoreOpList	TURdWrSemaphore::faquireWrOP;
TUSemaphoreOpList	TURdWrSemaphore::freleaseWrOP;
TUSemaphoreOpList	TURdWrSemaphore::faquireRdOP;
TUSemaphoreOpList	TURdWrSemaphore::freleaseRdOP;


/* -------------------------------------------------------------------------------
	TUSemaphoreOpList
------------------------------------------------------------------------------- */

// ROM 0x0025a1c8 Init__17TUSemaphoreOpListFUle
// The ops (MAKESEMLISTITEM words) follow the count as varargs.
long
TUSemaphoreOpList::Init(ULong numInList, ...)
{
	if (numInList == 0)
		return kError_Bad_Parameters;
	ULong msgSize = kObjectMessage_SemListSize + numInList * sizeof(ULong);
	ObjectMessage* msg = (ObjectMessage*) malloc(msgSize + sizeof(ObjectMessage));
	if (msg == nil)
		return kError_Could_Not_Create_Object;
	va_list args;
	va_start(args, numInList);
	for (ULong i = 0; i < numInList; i++)
		msg->fSemList.fOps[i] = va_arg(args, ULong);
	va_end(args);
	msg->fSemList.fCount = numInList;
	long err = MakeObject(kObjectSemList, msg, msgSize);
	free(msg);
	return err;
}


/* -------------------------------------------------------------------------------
	TUSemaphoreGroup
------------------------------------------------------------------------------- */

// ROM 0x0025a270 Init__16TUSemaphoreGroupFUl
long
TUSemaphoreGroup::Init(ULong num)
{
	ObjectMessage msg;
	msg.fSemGroup.fCount = num;
	return MakeObject(kObjectSemGroup, &msg, kObjectMessage_SemGroupSize);
}


// ROM 0x0025a45c SemOp__16TUSemaphoreGroupFUl8SemFlags
long
TUSemaphoreGroup::SemOp(TObjectId semListId, SemFlags flags)
{
	return SemaphoreOpGlue(fId, semListId, flags);
}


// ROM 0x0025a464 SemOp__16TUSemaphoreGroupFP17TUSemaphoreOpList8SemFlags
long
TUSemaphoreGroup::SemOp(TUSemaphoreOpList* semListObj, SemFlags flags)
{
	return SemaphoreOpGlue(fId, semListObj->fId, flags);
}


// ROM 0x0025a470 SetRefCon__16TUSemaphoreGroupFPv
// (The ROM's function serves both modes; this is the user-mode path, the
// kernel body is SemGroupSetRefCon.)
long
TUSemaphoreGroup::SetRefCon(void* refCon)
{
	return GenericSWI(kGeneric_SemGroupSetRefCon, fId, (ULong) (uintptr_t) refCon);
}


// ROM 0x0025a478 GetRefCon__16TUSemaphoreGroupFPPv
long
TUSemaphoreGroup::GetRefCon(void** pRefCon)
{
	return GenericWithReturnSWI(kGeneric_SemGroupGetRefCon, fId, 0, 0, (ULong*) pRefCon, nil, nil);
}


/* -------------------------------------------------------------------------------
	TULockingSemaphore
------------------------------------------------------------------------------- */

// ROM 0x0025a480 StaticInit__18TULockingSemaphoreSFv
// The two op lists every locking semaphore uses: acquire = wait for
// semaphore 0 to be zero then add one; release = subtract one.
long
TULockingSemaphore::StaticInit()
{
	long err = faquireOP.Init(2, MAKESEMLISTITEM(0, 0), MAKESEMLISTITEM(0, 1));
	if (err != noErr)
		return err;
	return freleaseOP.Init(1, MAKESEMLISTITEM(0, -1));
}


// ROM 0x0025a4c8 Init__18TULockingSemaphoreFv
// The user-side word lives in the heap and is published as the group's ref
// con, so other handles on the same semaphore (CopyObject) find it.
long
TULockingSemaphore::Init()
{
	fSem = (ULong*) malloc(sizeof(ULong));
	if (fSem == nil)
		return kError_No_Memory;
	*fSem = 0;
	long err = TUSemaphoreGroup::Init(1);
	if (err == noErr)
		SetRefCon(fSem);
	else
	{
		free(fSem);
		fSem = nil;
	}
	return err;
}


// ROM 0x0025a540 CopyObject__18TULockingSemaphoreFUl
void
TULockingSemaphore::CopyObject(TObjectId id)
{
	TUObject::CopyObject(id);
	GetRefCon((void**) &fSem);
}


// ROM 0x0025a564 __dt__18TULockingSemaphoreFv
// The word is freed with the object only if we made it (the kernel object
// goes with ~TUObject).
TULockingSemaphore::~TULockingSemaphore()
{
	if (fObjectCreatedByUs && fSem != nil)
		free(fSem);
}


// ROM 0x0025a298 Acquire__18TULockingSemaphoreF8SemFlags
// Claim the word with our task id.  If someone holds it, wait on the
// kernel semaphore (the holder's Release bumps it) and try again; the
// kernel semaphore is left as it was once the word is ours.
long
TULockingSemaphore::Acquire(SemFlags flags)
{
	long err = noErr;
	Boolean waited = false;
	for (;;)
	{
		if (Swap(fSem, gCurrentTaskId) == 0)
		{
			if (!waited)
				return err;
			break;
		}
		err = SemOp(&freleaseOP, flags);
		waited = true;
		if (err != noErr)
			break;
	}
	SemOp(&faquireOP, kNoWaitOnBlock);
	return err;
}


// ROM 0x0025a31c Release__18TULockingSemaphoreFv
// Let go of the word; if another task took it meanwhile (it is waiting in
// Acquire) wake it through the kernel semaphore - without waiting: the
// ROM passes kNoWaitOnBlock (1), so a semaphore already raised answers
// kError_Semaphore_Would_Cause_Block (which TForkWorld::ReleaseMutex takes
// as success) where waiting for it to come down would block the releaser
// for good.
long
TULockingSemaphore::Release()
{
	if (Swap(fSem, 0) != gCurrentTaskId)
		return SemOp(&faquireOP, kNoWaitOnBlock);
	return noErr;
}


/* -------------------------------------------------------------------------------
	TURdWrSemaphore
------------------------------------------------------------------------------- */

// ROM 0x0025a368 StaticInit__15TURdWrSemaphoreSFv
// Semaphore 0 counts writers, semaphore 1 readers.  A writer waits for both
// to be zero and takes 0; a reader waits for no writer and takes one of 1.
long
TURdWrSemaphore::StaticInit()
{
	long err = faquireWrOP.Init(3, MAKESEMLISTITEM(0, 0), MAKESEMLISTITEM(0, 1), MAKESEMLISTITEM(1, 0));
	if (err != noErr)
		return err;
	err = freleaseWrOP.Init(1, MAKESEMLISTITEM(0, -1));
	if (err != noErr)
		return err;
	err = faquireRdOP.Init(2, MAKESEMLISTITEM(0, 0), MAKESEMLISTITEM(1, 1));
	if (err != noErr)
		return err;
	return freleaseRdOP.Init(1, MAKESEMLISTITEM(1, -1));
}


// ROM 0x0025a400 Init__15TURdWrSemaphoreFv
long
TURdWrSemaphore::Init()
{
	return TUSemaphoreGroup::Init(2);
}


// ROM 0x0025a41c AcquireWr__15TURdWrSemaphoreF8SemFlags
long
TURdWrSemaphore::AcquireWr(SemFlags flags)
{
	return SemOp(&faquireWrOP, flags);
}


// ROM 0x0025a42c ReleaseWr__15TURdWrSemaphoreFv
long
TURdWrSemaphore::ReleaseWr()
{
	return SemOp(&freleaseWrOP, kWaitOnBlock);
}


// ROM 0x0025a43c AcquireRd__15TURdWrSemaphoreF8SemFlags
long
TURdWrSemaphore::AcquireRd(SemFlags flags)
{
	return SemOp(&faquireRdOP, flags);
}


// ROM 0x0025a44c ReleaseRd__15TURdWrSemaphoreFv
long
TURdWrSemaphore::ReleaseRd()
{
	return SemOp(&freleaseRdOP, kWaitOnBlock);
}
