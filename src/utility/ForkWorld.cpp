/*
	File:		utility/ForkWorld.cpp

	Contains:	TForkWorld (AppWorld.h): a task world that may spawn forks -
				further tasks on copies of itself - all sharing one mutex, so
				that only one of the family runs at a time.  The main world
				makes the mutex in TaskConstructor and runs PreMain/TheMain/
				PostMain under it; a fork (ForkInit from its parent) runs
				TheMain under the same mutex.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
	Layout (0x30 bytes): the TUTaskWorld (0x18), fMutex +0x18, fIsMain
	+0x1c, fRunsMain +0x1d, fRunning +0x1e, fForkingEnabled +0x1f, fParent
	+0x20, fStackSize +0x24, fPriority +0x28, fName +0x2c.
*/

#include "AppWorld.h"
#include "NewtonMemory.h"
#include "OSErrors.h"


// ROM 0x000cc2dc __ct__10TForkWorldFv
TForkWorld::TForkWorld()
{
	fIsMain = true;
	fRunsMain = true;
	fRunning = false;
	fForkingEnabled = true;
	fParent = nil;
	fStackSize = kSpawnedTaskStackSize;
	fName = 0;
	fPriority = kUserTaskPriority;
	fMutex = nil;
}


// ROM 0x000cc358 __dt__10TForkWorldFv
TForkWorld::~TForkWorld()
{ }


// ROM 0x000cc594 GetSizeOf__10TForkWorldFv
ULong
TForkWorld::GetSizeOf()
{
	return sizeof(TForkWorld);
}


// ROM 0x000cc610 MainInit__10TForkWorldFUlT1
long
TForkWorld::MainInit(ULong name, ULong stackSize)
{
	fName = name;
	fStackSize = stackSize;
	return StartTask(true, false, kNoTimeout, stackSize, fPriority, name);
}


// ROM 0x000cc64c MainInit__10TForkWorldFUlN31
long
TForkWorld::MainInit(ULong name, ULong stackSize, ULong priority, TObjectId environment)
{
	fName = name;
	fStackSize = stackSize;
	fPriority = priority;
	return StartTask(true, false, kNoTimeout, stackSize, priority, name, environment);
}


// ROM 0x000cc6a4 TaskConstructor__10TForkWorldFv
// In the new task: the main world makes the family's mutex, a fork joins
// its parent's; then the main or fork constructor.
long
TForkWorld::TaskConstructor()
{
	if (!fIsMain)
	{
		fMutex->fWorlds++;
		fMutex->fForks++;
		return ForkConstructor(fParent);
	}
	fMutex = new TForkMutex;
	if (fMutex == nil)
		return MemError();
	long err = fMutex->Init();
	if (err != noErr)
		return err;
	fMutex->fWorlds++;
	fMutex->fForks++;
	return MainConstructor();
}


// ROM 0x000cc76c TaskDestructor__10TForkWorldFv
void
TForkWorld::TaskDestructor()
{
	fMutex->fWorlds--;
	if (!fRunsMain)
	{
		ForkDestructor();
		return;
	}
	MainDestructor();
	if (fMutex != nil)
		delete fMutex;
}


// ROM 0x000cc398 TaskMain__10TForkWorldFv
// The main code runs under the mutex: the main world's PreMain first, then
// TheMain if it runs a main and PreMain succeeded; a fork's TheMain straight
// away.
void
TForkWorld::TaskMain()
{
	if (AcquireMutex() != noErr)
	{
		ReleaseMutex();
		return;
	}
	fRunning = true;
	Boolean runMain = true;
	if (fIsMain)
	{
		long err = PreMain();
		runMain = fRunsMain && err == noErr;
	}
	if (runMain)
		TheMain();
	if (fRunsMain)
		PostMain();
	ReleaseMutex();
}


// ROM 0x000cc424 MainConstructor__10TForkWorldFv
long
TForkWorld::MainConstructor()
{
	return TUTaskWorld::TaskConstructor();
}


// ROM 0x000cc428 MainDestructor__10TForkWorldFv
void
TForkWorld::MainDestructor()
{
	TUTaskWorld::TaskDestructor();
}


// ROM 0x000cc438 ForkInit__10TForkWorldFP10TForkWorld
// A fork takes its parent's settings and mutex.
long
TForkWorld::ForkInit(TForkWorld* parent)
{
	fIsMain = false;
	fMutex = parent->fMutex;
	fParent = parent;
	fStackSize = parent->fStackSize;
	fPriority = parent->fPriority;
	fName = parent->fName;
	fForkingEnabled = parent->fForkingEnabled;
	return noErr;
}


// ROM 0x000cc42c ForkConstructor__10TForkWorldFP10TForkWorld
long
TForkWorld::ForkConstructor(TForkWorld* /*parent*/)
{
	return noErr;
}


// ROM 0x000cc434 ForkDestructor__10TForkWorldFv
void
TForkWorld::ForkDestructor()
{ }


// ROM 0x000cc7d4 PreMain__10TForkWorldFv
long
TForkWorld::PreMain()
{
	return noErr;
}


// ROM 0x000cc7dc PostMain__10TForkWorldFv
void
TForkWorld::PostMain()
{ }


// ROM 0x000cc590 ForkSwitch__10TForkWorldFUc
void
TForkWorld::ForkSwitch(Boolean /*acquired*/)
{ }


// ROM 0x000cc69c MakeFork__10TForkWorldFv
long
TForkWorld::MakeFork()
{
	return noErr;
}


// ROM 0x000cc474 Fork__10TForkWorldFP10TForkWorld
// Spawns a fork, once the world is running with forking enabled: the given
// world is initialised from this one, or (with none) MakeFork makes one -
// if this world runs a main.
long
TForkWorld::Fork(TForkWorld* fork)
{
	if (!fForkingEnabled || !fRunning)
		return noErr;
	if (fork == nil)
	{
		if (!fRunsMain)
			return noErr;
		return MakeFork();
	}
	return fork->ForkInit(this);
}


// ROM 0x000cc57c EnableForking__10TForkWorldFUc
Boolean
TForkWorld::EnableForking(Boolean enable)
{
	Boolean was = fForkingEnabled;
	fForkingEnabled = enable;
	return was;
}


// ROM 0x000cc59c AcquireMutex__10TForkWorldFv
long
TForkWorld::AcquireMutex()
{
	long err = fMutex->Acquire(kWaitOnBlock);
	ForkSwitch(true);
	return err;
}


// ROM 0x000cc5d8 ReleaseMutex__10TForkWorldFv
// (a release that would not block anybody is not an error)
long
TForkWorld::ReleaseMutex()
{
	ForkSwitch(false);
	long err = fMutex->Release();
	if (err == kError_Semaphore_Would_Cause_Block)
		err = noErr;
	return err;
}


// ROM 0x000cc554 Yield__10TForkWorldFv
// Lets another of the family run.
void
TForkWorld::Yield()
{
	if (ReleaseMutex() == noErr)
		AcquireMutex();
}
