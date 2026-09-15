/*
	File:		hal/host/Atomic.cpp

	Contains:	Critical sections for a host (Linux/Windows/macOS) build.
				The Newton kernel is single-processor and uses these to keep
				interrupt handlers out; on a host the kernel runs on one thread,
				so a recursive mutex gives the same guarantee if it ever runs
				on several.  The nesting counts mirror gAtomicNestCount /
				gAtomicFIQNestCount in the ROM.
*/

#include "hal/Atomic.h"

#include <mutex>

static std::recursive_mutex	gAtomicLock;
static int					gAtomicNestCount = 0;
static int					gAtomicFIQNestCount = 0;

extern "C" void
EnterAtomic(void)
{
	gAtomicLock.lock();
	gAtomicNestCount++;
}

extern "C" void
ExitAtomic(void)
{
	gAtomicNestCount--;
	gAtomicLock.unlock();
}

extern "C" void
EnterFIQAtomic(void)
{
	gAtomicLock.lock();
	gAtomicFIQNestCount++;
}

extern "C" void
ExitFIQAtomic(void)
{
	gAtomicFIQNestCount--;
	gAtomicLock.unlock();
}
