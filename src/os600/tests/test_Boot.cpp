// Boot test: OsBoot builds the kernel, makes the idle and 'user' tasks and
// hands over to the runtime; UserBoot spawns 'ksrv', which here runs a
// scenario with the real user-side classes: a TUTaskWorld echo server spawned
// through the object manager, an RPC to it over a TUPort, a Sleep through the
// null port and the timer engine, and a TULockingSemaphore.  Nothing is built
// by hand: every object comes from the object manager monitor.

#include "Boot.h"
#include "KernelGlobals.h"
#include "ObjectTable.h"
#include "Task.h"
#include "Monitor.h"
#include "Port.h"
#include "UserMonitor.h"
#include "OSErrors.h"
#include "UserBoot.h"
#include "UserGlobals.h"
#include "UserPorts.h"
#include "UserTasks.h"
#include "UserSemaphore.h"
#include "host/TaskRuntime.h"
#include "hal/Timer.h"
#include "MemObjManager.h"
#include "Environment.h"
#include "Domain.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// what the scenario observed
static Boolean ksrvRan = false;
static TObjectId ksrvTaskId = 0;
static TObjectId ksrvEnvironmentId = 0;
static long portInitErr = -1, spawnErr = -1, rpcErr = -1;
static char reply[32];
static ULong replySize = 0;
static int echoServed = 0;
static TObjectId echoTaskId = 0, echoConstructedIn = 0;
static Int64 beforeSleep, afterSleep;
static long semErr = -1;
static Boolean semHeld = false;
static ULong objectsBefore = 0, objectsAfter = 0;
static long caughtData = 0, caughtMsg = 0, cleanupRan = 0, monitorCaught = 0, monitorProcRan = 0;
static Boolean afterThrowReached = false;

// a database lookup after the run (the user-mode calls are refused then)
static Boolean FindId(MemObjType type, ULong name, TObjectId* outId)
{
	MemObjEntry entry;
	if (MemObjManager::PrimGetEntryByName(type, name, &entry) != noErr)
		return false;
	*outId = (TObjectId) entry.fValue;
	return true;
}

static ULong CountObjects()
{
	ULong n = 0;
	TObjectTableIterator iter(gObjectTable, 0);
	while (iter.GetNextTableId() != 0)
		n++;
	return n;
}


static void Cleanup(void* what)
{
	cleanupRan += *(int*) what;
}

// a monitor whose proc throws: the exception must land in the caller
static long ThrowingMonitorProc(void*, ULong selector, void*)
{
	monitorProcRan++;
	Throw((ExceptionName) "evt.ex.monitor", (void*) (uintptr_t) selector, nil);
	return -1;
}


// a task world that echoes one RPC in upper case, then ends
class TEchoServer : public TUTaskWorld
{
	public:
						TEchoServer(TObjectId portId) : fPortId(portId), fMarker(0xEC40) {}
		virtual ULong	GetSizeOf()			{ return sizeof(TEchoServer); }
		virtual long	TaskConstructor();
		virtual void	TaskMain();

		TObjectId		fPortId;
		ULong			fMarker;
};

long
TEchoServer::TaskConstructor()
{
	echoConstructedIn = gCurrentTaskId;
	return fMarker == 0xEC40 ? noErr : -1;		// the copy on the child's stack must be intact
}

void
TEchoServer::TaskMain()
{
	echoTaskId = gCurrentTaskId;
	TUPort port(fPortId);
	char buffer[32];
	ULong size = 0;
	TUMsgToken token;
	if (port.Receive(&size, buffer, sizeof(buffer), &token) == noErr)
	{
		for (ULong i = 0; i < size; i++)
			if (buffer[i] >= 'a' && buffer[i] <= 'z')
				buffer[i] -= 'a' - 'A';
		echoServed++;
		token.ReplyRPC(buffer, size);
	}
}


static void KernelServicesScenario()
{
	ksrvRan = true;
	ksrvTaskId = gCurrentTaskId;
	ksrvEnvironmentId = gCurrentTask->fEnvironment->fId;
	objectsBefore = CountObjects();

	TUPort port;
	portInitErr = port.Init();

	TEchoServer* echo = new TEchoServer(port);
	spawnErr = echo->StartTask(true, false, kNoTimeout, 0x1000, kUserTaskPriority, 'echo');

	char request[] = "hello newton";
	memset(reply, 0, sizeof(reply));
	rpcErr = port.SendRPC(&replySize, request, sizeof(request), reply, sizeof(reply));

	GetClock(&beforeSleep);
	Sleep(10 * kMilliseconds);
	GetClock(&afterSleep);

	TULockingSemaphore sem;
	semErr = sem.Init();
	if (semErr == noErr)
	{
		sem.Acquire();
		semHeld = true;
		sem.Release();
	}

	// --- exceptions: try/catch, a message, an unwind_protect, and one thrown inside a monitor
	newton_try
	{
		Throw((ExceptionName) "evt.ex.test.deep", (void*) 42, nil);
		afterThrowReached = true;
	}
	newton_catch("evt.ex.other")
	{
		caughtData = -1;
	}
	newton_catch("evt.ex.test")
	{
		caughtData = (long) (uintptr_t) CurrentException()->data;
	}
	end_try;
	newton_try
	{
		int mark = 5;
		unwind_protect
		{
			ThrowMsg((char*) "boom");
		}
		on_unwind
		{
			Cleanup(&mark);
		}
		end_unwind;
	}
	newton_catch(exMsgException)
	{
		caughtMsg = strcmp((const char*) CurrentException()->data, "boom") == 0;
	}
	end_try;
	TUMonitor thrower;
	if (thrower.Init((MonitorProcPtr) ThrowingMonitorProc, 0x1000) == noErr)
	{
		newton_try
		{
			thrower.InvokeRoutine(7, nil);
			monitorCaught = -1;
		}
		newton_catch("evt.ex.monitor")
		{
			monitorCaught = (long) (uintptr_t) CurrentException()->data;
		}
		end_try;
	}

	delete echo;
	objectsAfter = CountObjects();
	HostStopTasks();
}


int main()
{
	gHostKernelServicesTask = KernelServicesScenario;
	OsBoot();

	EXPECT(ksrvRan && ksrvTaskId != 0);
	EXPECT(gIdleTask != nil && gIdleTask->fName == 'idle' && gTheObjectManagerMonitor != nil);
	EXPECT(gUObjectMgrMonitor != nil && gUObjectMgrMonitor->fId == gTheObjectManagerMonitor->fId);
	EXPECT(gUNullPort != nil && gUNullPort->fId == gNullPort->fId);
	EXPECT(gOSIsRunning);
	// the memory object database is populated: every domain and environment of
	// the tables exists (the run is over, so the kernel-mode primitives are used)
	TObjectId id = 0;
	for (int i = 0; i < 9; i++)
	{
		DomainInfo dinfo;
		EXPECT(MemObjManager::PrimGetDomainInfo(i, &dinfo) == noErr && FindId(kMemObjDomain, dinfo.Name(), &id) && ObjectType(id) == kDomainType);
		TKDomain* d = (TKDomain*) gObjectTable->Get(id);
		EXPECT(d != nil && d->fBase == dinfo.Base() && d->fSize == dinfo.Size() && d->fNumber >= 0);
		if (dinfo.Name() == 'krnl')
			EXPECT(d->fNumber == 2);
	}
	EXPECT(FindId(kMemObjEnvironment, 'ksrv', &id) && id == ksrvEnvironmentId);	// 'ksrv' ran in its environment
	EXPECT(FindId(kMemObjEnvironment, 'user', &id) && ObjectType(id) == kEnvironmentType);
	TEnvironment* user = (TEnvironment*) gObjectTable->Get(id);
	TObjectId userDomainId = 0;
	FindId(kMemObjDomain, 'user', &userDomainId);
	EXPECT(user != nil && user->fHeapDomainId == userDomainId && user->fStackDomainId == userDomainId);
	EXPECT(FindId(kMemObjEnvironment, 'prot', &id));
	TEnvironment* prot = (TEnvironment*) gObjectTable->Get(id);
	TObjectId protDomainId = 0;
	FindId(kMemObjDomain, 'prot', &protDomainId);
	TKDomain* protDomain = (TKDomain*) gObjectTable->Get(protDomainId);
	EXPECT(prot != nil && protDomain != nil && ((prot->fDomainAccess >> (2 * protDomain->fNumber)) & 3) == 3);	// manages 'prot'
	EXPECT(((user->fDomainAccess >> (2 * protDomain->fNumber)) & 3) == 1);								// 'user' is a client of it
	EXPECT(portInitErr == noErr);
	EXPECT(spawnErr == noErr && echoConstructedIn != 0 && echoConstructedIn != ksrvTaskId && echoTaskId == echoConstructedIn);
	EXPECT(rpcErr == noErr && echoServed == 1);
	EXPECT(replySize == 13 && strcmp(reply, "HELLO NEWTON") == 0);
	EXPECT(afterSleep.lo - beforeSleep.lo >= 10 * kMilliseconds);
	EXPECT(semErr == noErr && semHeld);
	EXPECT(caughtData == 42 && !afterThrowReached);
	EXPECT(caughtMsg == 1 && cleanupRan == 5);
	EXPECT(monitorProcRan == 1 && monitorCaught == 7);
	EXPECT(objectsAfter <= objectsBefore + 5);			// the echo and monitor tasks (owned by nobody, awaiting scavenge) may remain
	EXPECT(gNumberOfTaskSwaps >= 10);

	if (failures == 0)
		printf("test_Boot: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
