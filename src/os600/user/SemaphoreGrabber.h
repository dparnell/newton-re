/*
	File:		os600/user/SemaphoreGrabber.h

	Contains:	TULockingSemaphoreGrabber: a locking semaphore held for as
				long as the grabber lives - acquired by the constructor and
				released by the destructor (which the ROM has inline at each
				use).  A nil semaphore is allowed and grabs nothing, which is
				how the internal flash, made at boot before there are
				semaphores, runs the same code later with its lock.  The
				non-blocking form holds the semaphore only if it could have
				it at once; ask Held() which.

	Not in the DDK.
*/

#ifndef __SEMAPHOREGRABBER_H
#define __SEMAPHOREGRABBER_H

#ifndef __USERSEMAPHORE_H
#include "UserSemaphore.h"
#endif

class TULockingSemaphoreGrabber
{
public:
	enum eNonBlockOption { kNonBlocking };

					TULockingSemaphoreGrabber(TULockingSemaphore* semaphore);						// ROM 0x0013b6d4 __ct__25TULockingSemaphoreGrabberFP18TULockingSemaphore
					TULockingSemaphoreGrabber(TULockingSemaphore* semaphore, eNonBlockOption);	// ROM 0x0013bd44 __ct__25TULockingSemaphoreGrabberFP18TULockingSemaphoreQ225TULockingSemaphoreGrabber15eNonBlockOption
					~TULockingSemaphoreGrabber()	{ if (fSemaphore != nil) fSemaphore->Release(); }

	long			DoAquire(TULockingSemaphore* semaphore);										// ROM 0x0013afa8 DoAquire__25TULockingSemaphoreGrabberFP18TULockingSemaphore
	Boolean			Held() const	{ return fSemaphore != nil; }

private:
	TULockingSemaphore*	fSemaphore;		// +0x00  nil when nothing is held
};

#endif	/* __SEMAPHOREGRABBER_H */
