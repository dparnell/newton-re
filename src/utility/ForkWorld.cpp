/*
	File:		utility/ForkWorld.cpp

	Contains:	TForkWorld (AppWorld.h): a task world that may spawn forks -
				further tasks on copies of itself - all sharing one mutex, so
				that only one of the family runs at a time.  The main world
				makes the mutex in TaskConstructor and runs PreMain/TheMain/
				PostMain under it; a fork (ForkInit from its parent) runs
				TheMain under the same mutex.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	Layout (0x30 bytes): the TUTaskWorld (0x18), fMutex +0x18, fIsMain
	+0x1c, fRunsMain +0x1d, fRunning +0x1e, fForkingEnabled +0x1f, fParent
	+0x20, fStackSize +0x24, fPriority +0x28, fName +0x2c.
*/

#include "AppWorld.h"
#include "NewtonMemory.h"
#include "OSErrors.h"


// ROM 0x000cb188 __ct__10TForkWorldFv
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


// ROM 0x000cb204 __dt__10TForkWorldFv
TForkWorld::~TForkWorld()
{ }


// ROM 0x000cb440 GetSizeOf__10TForkWorldFv
ULong
TForkWorld::GetSizeOf()
{
	return sizeof(TForkWorld);
}


// ROM 0x000cb4bc MainInit__10TForkWorldFUlT1
long
TForkWorld::MainInit(ULong name, ULong stackSize)
{
	fName = name;
	fStackSize = stackSize;
	return StartTask(true, false, kNoTimeout, stackSize, fPriority, name);
}


// ROM 0x000cb4f8 MainInit__10TForkWorldFUlN31
long
TForkWorld::MainInit(ULong name, ULong stackSize, ULong priority, TObjectId environment)
{
	fName = name;
	fStackSize = stackSize;
	fPriority = priority;
	return StartTask(true, false, kNoTimeout, stackSize, priority, name, environment);
}


// ROM 0x000cb550 TaskConstructor__10TForkWorldFv
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


// ROM 0x000cb618 TaskDestructor__10TForkWorldFv
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


// ROM 0x000cb244 TaskMain__10TForkWorldFv
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


// ROM 0x000cb2d0 MainConstructor__10TForkWorldFv
long
TForkWorld::MainConstructor()
{
	return TUTaskWorld::TaskConstructor();
}


// ROM 0x000cb2d4 MainDestructor__10TForkWorldFv
void
TForkWorld::MainDestructor()
{
	TUTaskWorld::TaskDestructor();
}


// ROM 0x000cb2e4 ForkInit__10TForkWorldFP10TForkWorld
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


// ROM 0x000cb2d8 ForkConstructor__10TForkWorldFP10TForkWorld
long
TForkWorld::ForkConstructor(TForkWorld* /*parent*/)
{
	return noErr;
}


// ROM 0x000cb2e0 ForkDestructor__10TForkWorldFv
void
TForkWorld::ForkDestructor()
{ }


// ROM 0x000cb680 PreMain__10TForkWorldFv
long
TForkWorld::PreMain()
{
	return noErr;
}


// ROM 0x000cb688 PostMain__10TForkWorldFv
void
TForkWorld::PostMain()
{ }


// ROM 0x000cb43c ForkSwitch__10TForkWorldFUc
void
TForkWorld::ForkSwitch(Boolean /*acquired*/)
{ }


// ROM 0x000cb548 MakeFork__10TForkWorldFv
// (none: the ROM answers noErr, which Fork takes as no object)
TForkWorld*
TForkWorld::MakeFork()
{
	return nil;
}


// ROM 0x000cb320 Fork__10TForkWorldFP10TForkWorld
// Spawns a fork, once the world is running with forking enabled: the given
// world - or, with none, one MakeFork makes, if this world runs a main -
// is initialised from this one and started as a task of its own, which
// copies it (so the object here is deleted either way).  The fork takes
// the main code over: this world stops running it (its event loop ends
// when it gets back to it) and its objects go to the fork when its task
// ends.  Forking disabled, or not running yet, is not an error: nothing
// happens.
long
TForkWorld::Fork(TForkWorld* fork)
{
	if (!fForkingEnabled || !fRunning)
		return noErr;
	if (fork == nil)
	{
		if (!fRunsMain)
			return noErr;
		fork = MakeFork();
	}
	if (fork == nil)
		return kError_Could_Not_Create_Object;
	long err = fork->ForkInit(this);
	if (err == noErr
	 && (err = fork->StartTask(true, false, kNoTimeout, fork->fStackSize, fork->fPriority, fork->fName)) == noErr)
	{
		SetBequeathId(fork->GetChildTaskId());
		fRunsMain = false;
	}
	delete fork;
	return err;
}


// ROM 0x000cb428 EnableForking__10TForkWorldFUc
Boolean
TForkWorld::EnableForking(Boolean enable)
{
	Boolean was = fForkingEnabled;
	fForkingEnabled = enable;
	return was;
}


// ROM 0x000cb448 AcquireMutex__10TForkWorldFv
long
TForkWorld::AcquireMutex()
{
	long err = fMutex->Acquire(kWaitOnBlock);
	ForkSwitch(true);
	return err;
}


// ROM 0x000cb484 ReleaseMutex__10TForkWorldFv
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


// ROM 0x000cb400 Yield__10TForkWorldFv
// Lets another of the family run.
void
TForkWorld::Yield()
{
	if (ReleaseMutex() == noErr)
		AcquireMutex();
}
