/*
	File:		os600/user/SemaphoreGrabber.cpp

	Contains:	TULockingSemaphoreGrabber (SemaphoreGrabber.h).
*/

#include "SemaphoreGrabber.h"
#include "NewtErrors.h"


// ROM 0x0013b6d4 __ct__25TULockingSemaphoreGrabberFP18TULockingSemaphore
TULockingSemaphoreGrabber::TULockingSemaphoreGrabber(TULockingSemaphore* semaphore)
{
	fSemaphore = semaphore;
	if (semaphore != nil)
		semaphore->Acquire(kWaitOnBlock);
}


// ROM 0x0013bd44 __ct__25TULockingSemaphoreGrabberFP18TULockingSemaphoreQ225TULockingSemaphoreGrabber15eNonBlockOption
TULockingSemaphoreGrabber::TULockingSemaphoreGrabber(TULockingSemaphore* semaphore, eNonBlockOption)
{
	if (semaphore == nil || semaphore->Acquire(kNoWaitOnBlock) != noErr)
		fSemaphore = nil;
	else
		fSemaphore = semaphore;
}


// ROM 0x0013afa8 DoAquire__25TULockingSemaphoreGrabberFP18TULockingSemaphore
// Let go of what is held and take another.  (The ROM has Acquire inline:
// the swap of the task id into the semaphore word, and the kernel's semop
// when another task holds it.)
long
TULockingSemaphoreGrabber::DoAquire(TULockingSemaphore* semaphore)
{
	if (fSemaphore != nil)
		fSemaphore->Release();
	fSemaphore = semaphore;
	if (semaphore == nil)
		return noErr;
	return semaphore->Acquire(kWaitOnBlock);
}
