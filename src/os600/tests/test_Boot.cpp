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

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// what the scenario observed
static Boolean ksrvRan = false;
static TObjectId ksrvTaskId = 0;
static long portInitErr = -1, spawnErr = -1, rpcErr = -1;
static char reply[32];
static ULong replySize = 0;
static int echoServed = 0;
static TObjectId echoTaskId = 0, echoConstructedIn = 0;
static Int64 beforeSleep, afterSleep;
static long semErr = -1;
static Boolean semHeld = false;
static ULong objectsBefore = 0, objectsAfter = 0;

static ULong CountObjects()
{
	ULong n = 0;
	TObjectTableIterator iter(gObjectTable, 0);
	while (iter.GetNextTableId() != 0)
		n++;
	return n;
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
	EXPECT(portInitErr == noErr);
	EXPECT(spawnErr == noErr && echoConstructedIn != 0 && echoConstructedIn != ksrvTaskId && echoTaskId == echoConstructedIn);
	EXPECT(rpcErr == noErr && echoServed == 1);
	EXPECT(replySize == 13 && strcmp(reply, "HELLO NEWTON") == 0);
	EXPECT(afterSleep.lo - beforeSleep.lo >= 10 * kMilliseconds);
	EXPECT(semErr == noErr && semHeld);
	EXPECT(objectsAfter <= objectsBefore + 1);			// the echo task (owned by nobody, awaiting scavenge) may remain
	EXPECT(gNumberOfTaskSwaps >= 10);

	if (failures == 0)
		printf("test_Boot: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
