// Boot test: OsBoot builds the kernel, makes the idle and 'user' tasks and
// hands over to the runtime; UserBoot spawns 'ksrv', which here runs a
// scenario with the real user-side classes: a TUTaskWorld echo server spawned
// through the object manager, an RPC to it over a TUPort, a Sleep through the
// null port and the timer engine, a TULockingSemaphore, exceptions, the name
// server, system events, gestalt and a timer queue.  Nothing is built
// by hand: every object comes from the object manager monitor.

#include "Boot.h"
#include "CompMath.h"
#include "hal/host/Host.h"
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
#include "NameServer.h"
#include "UCErrors.h"
#include "SystemEvents.h"
#include "NewtonGestalt.h"
#include "TimerQueue.h"
#include "AppWorld.h"
#include "Loader.h"
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
static long semWakeErr = -1;
static long semNoWaitErr = -1;
static ULong objectsBefore = 0, objectsAfter = 0;
static long caughtData = 0, caughtMsg = 0, cleanupRan = 0, monitorCaught = 0, monitorProcRan = 0;
static Boolean afterThrowReached = false;
static long nsMissing = 0, nsRegister = 0, nsDuplicate = 0, nsLookup = 0, nsUnregister = 0, nsGone = 0;
static ULong nsThing = 0, nsSpec = 0;
static TObjectId scenarioPortId = 0;
static long waiterErr = -1;
static ULong waiterThing = 0, waiterSpec = 0;
static TObjectId waiterTaskId = 0;
static long sysEventRegister = 0, sysEventDuplicate = 0, sysEventSend = 0, sysEventNobody = 0, sysEventUnregister = 0;
static char sysEventSeen[16];
static int sysEventListened = 0;
static long gestaltErr = -1, gestaltRegErr = -1, gestaltReplaceErr = -1, gestaltMineErr = -1;
static ULong gestaltVersion = 0, gestaltMineSize = 0;
static ULong gestaltMine[2] = { 0, 0 };
static int timerFired[3] = { 0, 0, 0 };
static int timerOrder = 0;
static Int64 timerStart, timerEnd;
static Boolean timerCancelled = false;
static long timedReceiveErr = -1;
static Boolean timedReceiveGot = false;
static ULong timeUnitsMs = 0, timeConverted = 0;
static TObjectId mainTaskId = 0, mainEnvironmentId = 0;
static ULong mainTaskName = 0;
static long appWorldInit = -1, appWorldLookup = -1, appWorldRPC = -1, appWorldNoHandler = -1, appWorldIdles = 0, appWorldSysEvents = 0, appWorldDone = 0;
static ULong appWorldReplySize = 0;
static TObjectId appWorldTaskId = 0, appWorldRegisteredPort = 0;
static char appWorldSeen[16], appWorldReply[16];

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


// a task world that waits for a name to be registered
class TWaiter : public TUTaskWorld
{
	public:
		virtual ULong	GetSizeOf()			{ return sizeof(TWaiter); }
		virtual void	TaskMain();
};

void
TWaiter::TaskMain()
{
	waiterTaskId = gCurrentTaskId;
	TUNameServer nameServer;
	waiterErr = nameServer.WaitForRegister((char*) "late", (char*) "test", &waiterThing, &waiterSpec);
}

// a task world that takes one system event on its port and answers it
class TListener : public TUTaskWorld
{
	public:
						TListener(TObjectId portId) : fPortId(portId) {}
		virtual ULong	GetSizeOf()			{ return sizeof(TListener); }
		virtual void	TaskMain();

		TObjectId		fPortId;
};

void
TListener::TaskMain()
{
	TUPort port(fPortId);
	ULong size = 0;
	TUMsgToken token;
	memset(sysEventSeen, 0, sizeof(sysEventSeen));
	if (port.Receive(&size, sysEventSeen, sizeof(sysEventSeen), &token) == noErr)
	{
		sysEventListened++;
		token.ReplyRPC(nil, 0, noErr);
	}
}


// a timer that notes when, and in what order, it fired; the last one sends
// the port a message so that the timed receive has something to return
static TUPort* timerPortForWake = nil;
static TUAsyncMessage timerWake;

class TTestTimer : public TTimerElement
{
	public:
						TTestTimer(TTimerQueue* q, ULong refCon) : TTimerElement(q, refCon) {}
		virtual void	Timeout();
};

void
TTestTimer::Timeout()
{
	timerFired[GetRefCon()] = ++timerOrder;
	if (GetRefCon() == 0)
	{
		static char done[] = "done";		// the buffer must outlive this call: the receiver copies it later
		timerPortForWake->Send(&timerWake, done, sizeof(done));
	}
}


// --- an application world: a handler that answers 'tst1' events, an idler,
// a system event handler, and a 'stop' event that ends the loop

class TTestEvent : public TAEvent
{
	public:
						TTestEvent(AEEventID id) { fAEventID = id; }
		char			fText[12];
};

class TTestHandler : public TAEventHandler
{
	public:
		virtual void	AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event);
		virtual void	IdleProc(TUMsgToken* token, ULong* size, TAEvent* event);
};

void
TTestHandler::AEHandlerProc(TUMsgToken* /*token*/, ULong* /*size*/, TAEvent* event)
{
	TTestEvent* request = (TTestEvent*) event;
	strcpy(appWorldSeen, request->fText);
	for (char* c = request->fText; *c; c++)
		if (*c >= 'a' && *c <= 'z')
			*c -= 'a' - 'A';
	SetReply(sizeof(TTestEvent), request);		// the event, upper-cased, is the reply
}

void
TTestHandler::IdleProc(TUMsgToken* /*token*/, ULong* /*size*/, TAEvent* /*event*/)
{
	appWorldIdles++;
	if (appWorldIdles < 3)
		StartIdle();
}

class TStopHandler : public TAEventHandler
{
	public:
		virtual void	AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event);
};

void
TStopHandler::AEHandlerProc(TUMsgToken* /*token*/, ULong* /*size*/, TAEvent* /*event*/)
{
	appWorldDone++;
	((TAppWorld*) GetGlobals())->AETerminateLoop();
}

class TTestSysEventHandler : public TSystemEventHandler
{
	public:
		virtual void	AnySystemEvents(TAEvent* event)		{ appWorldSysEvents++; }
};

class TTestWorld : public TAppWorld
{
	public:
		virtual ULong	GetSizeOf()			{ return sizeof(TTestWorld); }
		virtual long	MainConstructor();
		virtual void	MainDestructor();

		TTestHandler*			fHandler;
		TStopHandler*			fStopHandler;
		TTestSysEventHandler*	fSysEventHandler;
};

long
TTestWorld::MainConstructor()
{
	long err = TAppWorld::MainConstructor();
	if (err != noErr)
		return err;
	appWorldTaskId = gCurrentTaskId;
	fHandler = new TTestHandler;
	fHandler->Init('tst1');
	fHandler->InitIdler(5 * kMilliseconds);
	fStopHandler = new TStopHandler;
	fStopHandler->Init('stop');
	fSysEventHandler = new TTestSysEventHandler;
	return fSysEventHandler->Init('tsev');
}

void
TTestWorld::MainDestructor()
{
	// (the world deletes the handlers it holds)
	TAppWorld::MainDestructor();
}


// what the 'main' task (UserMain) would be: the NewtonScript world
static TTime wrapBefore, wrapAfter, wrapLater;

static void MainTaskScenario()
{
	mainTaskId = gCurrentTaskId;
	mainTaskName = gCurrentTask->fName;
	mainEnvironmentId = gCurrentTask->fEnvironment->fId;
}


static void KernelServicesScenario()
{
	ksrvRan = true;
	ksrvTaskId = gCurrentTaskId;
	ksrvEnvironmentId = gCurrentTask->fEnvironment->fId;
	objectsBefore = CountObjects();

	TUPort port;
	portInitErr = port.Init();
	scenarioPortId = port;

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
		// a release that finds another task's id in the word wakes it by
		// raising the kernel semaphore - without waiting, so one that finds
		// the semaphore already raised answers at once instead of blocking
		// the releaser for good
		ULong* word = nil;
		sem.GetRefCon((void**) &word);
		*word = gCurrentTaskId + 1;
		semWakeErr = sem.Release();
		*word = gCurrentTaskId + 1;
		semNoWaitErr = sem.Release();
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

	// --- the name server: register/lookup/unregister, a waiter released by a registration
	{
		TUNameServer nameServer;
		nsMissing = nameServer.Lookup((char*) "echo", (char*) kTUPort, &nsThing, &nsSpec);
		nsRegister = nameServer.RegisterName((char*) "echo", (char*) kTUPort, port, 0x5eC);
		nsDuplicate = nameServer.RegisterName((char*) "echo", (char*) kTUPort, 1, 2);
		nsLookup = nameServer.Lookup((char*) "echo", (char*) kTUPort, &nsThing, &nsSpec);
		nsUnregister = nameServer.UnRegisterName((char*) "echo", (char*) kTUPort);
		nsGone = nameServer.Lookup((char*) "echo", (char*) kTUPort, &nsThing, &nsSpec);

		TWaiter* waiter = new TWaiter;
		if (waiter->StartTask(true, false, kNoTimeout, 0x1000, kUserTaskPriority, 'wait') == noErr)
		{
			Sleep(5 * kMilliseconds);					// let it get to WaitForRegister
			nameServer.RegisterName((char*) "late", (char*) "test", 0x1a7e, 0x5bec);
			Sleep(5 * kMilliseconds);					// let it hear
		}
		delete waiter;
	}

	// --- system events: a listener registers its port, a sender's message reaches it
	{
		TUPort listenerPort;
		listenerPort.Init();
		TListener* listener = new TListener(listenerPort);
		TSystemEvent event('test');
		sysEventNobody = event.RegisterForSystemEvent(listenerPort) == noErr ? event.UnRegisterForSystemEvent(listenerPort) : -1;
		if (listener->StartTask(true, false, kNoTimeout, 0x1000, kUserTaskPriority, 'lstn') == noErr)
		{
			sysEventRegister = event.RegisterForSystemEvent(listenerPort);
			sysEventDuplicate = event.RegisterForSystemEvent(listenerPort);
			TSendSystemEvent sender('test');
			if (sender.Init() == noErr)
			{
				char payload[] = "ping";
				sysEventSend = sender.SendSystemEvent(payload, sizeof(payload));
			}
			sysEventUnregister = event.UnRegisterForSystemEvent(listenerPort);
		}
		delete listener;
	}

	// --- gestalt: a system selector, and one of our own registered with the name server
	{
		TUGestalt gestalt;
		TGestaltVersion version;
		gestaltErr = gestalt.Gestalt(kGestalt_Version, &version, sizeof(version));
		gestaltVersion = version.fVersion;
		ULong mine[2] = { 0xCAFE, 0xF00D };
		gestaltRegErr = gestalt.RegisterGestalt(0x03000001, mine, sizeof(mine));
		gestaltReplaceErr = gestalt.ReplaceGestalt(kGestalt_SystemInfo, mine, sizeof(mine));	// a system selector: refused
		gestaltMineSize = sizeof(gestaltMine);
		gestaltMineErr = gestalt.Gestalt(0x03000001, gestaltMine, &gestaltMineSize);
	}

	// --- the timer queue: three timers, one cancelled, over a timed receive
	{
		TTimerPort timerPort;
		if (timerPort.Init() == noErr && timerWake.Init(false) == noErr)
		{
			timerPortForWake = &timerPort;
			TTestTimer late(timerPort.GetQueue(), 0), early(timerPort.GetQueue(), 1), never(timerPort.GetQueue(), 2);
			GetClock(&timerStart);
			late.Prime(30 * kMilliseconds);
			never.Prime(20 * kMilliseconds);
			early.Prime(10 * kMilliseconds);
			timerCancelled = never.Cancel() && !never.IsPrimed() && late.IsPrimed();
			// the receive waits through the two timeouts and returns with the
			// message the last timer sends
			ULong size = 0;
			char buffer[8];
			timedReceiveErr = timerPort.TimedReceive(&size, buffer, sizeof(buffer), nil, nil, kMsgType_MatchAll, false, false);
			timedReceiveGot = size == 5 && strcmp(buffer, "done") == 0;
			GetClock(&timerEnd);
		}
		TTime ms(1, kMilliseconds);
		timeUnitsMs = ms;
		TTime later = TimeFromNow(3 * kMilliseconds);
		TTime now = GetGlobalTime();
		timeConverted = (later - now).ConvertTo(kMilliseconds);
	}

	// --- the clock past 2^32 ticks (19.4 minutes of 3.6864 MHz): read from
	// a task it carries into the high word and keeps going
	{
		Int64 clock;
		GetClock(&clock);
		Int64 nearWrap = { clock.hi, 0xFFFFF000 };
		HostSetClock(&nearWrap);
		wrapBefore = GetGlobalTime();
		HostAdvanceClock(0x2000);
		wrapAfter = GetGlobalTime();
		HostAdvanceClock(0x2000);
		wrapLater = GetGlobalTime();
	}

	// --- an app world: found by name, sent an RPC event, a system event, and told to stop
	{
		TTestWorld* world = new TTestWorld;
		appWorldInit = world->Init('test', true, 0x2000);
		TUNameServer nameServer;
		ULong spec = 0;
		appWorldLookup = nameServer.Lookup((char*) "test", (char*) kTUPort, &appWorldRegisteredPort, &spec);
		if (appWorldLookup == noErr)
		{
			TUPort worldPort(appWorldRegisteredPort);
			TTestEvent request('tst1');
			strcpy(request.fText, "event");
			TTestEvent reply('----');
			memset(appWorldReply, 0, sizeof(appWorldReply));
			appWorldRPC = worldPort.SendRPC(&appWorldReplySize, &request, sizeof(request), &reply, sizeof(reply));
			strcpy(appWorldReply, reply.fText);
			TTestEvent unknown('none');
			appWorldNoHandler = worldPort.SendRPC(&appWorldReplySize, &unknown, sizeof(unknown), &reply, sizeof(reply));
			TSendSystemEvent sysEvent('tsev');
			if (sysEvent.Init() == noErr)
			{
				TAESystemEvent payload('tsev');
				sysEvent.SendSystemEvent(&payload, sizeof(payload));
			}
			Sleep(20 * kMilliseconds);				// the idler fires three times
			TTestEvent stop('stop');
			worldPort.Send(&stop, sizeof(stop));
			Sleep(5 * kMilliseconds);				// the world winds down
		}
		delete world;
	}

	delete echo;
	objectsAfter = CountObjects();
	HostStopTasks();
}


int main()
{
	gHostKernelServicesTask = KernelServicesScenario;
	gHostUserMain = MainTaskScenario;
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
	EXPECT(semWakeErr == noErr && semNoWaitErr == kError_Semaphore_Would_Cause_Block);
	EXPECT(caughtData == 42 && !afterThrowReached);
	EXPECT(caughtMsg == 1 && cleanupRan == 5);
	EXPECT(monitorProcRan == 1 && monitorCaught == 7);
	EXPECT(nsMissing == kError_Not_Registered && nsRegister == noErr && nsDuplicate == kError_Already_Registered);
	EXPECT(nsLookup == noErr && nsThing == scenarioPortId && nsSpec == 0x5eC);
	EXPECT(nsUnregister == noErr && nsGone == kError_Not_Registered);
	EXPECT(waiterTaskId != 0 && waiterErr == noErr && waiterThing == 0x1a7e && waiterSpec == 0x5bec);
	EXPECT(sysEventNobody == noErr && sysEventRegister == noErr && sysEventDuplicate == kError_Already_Registered);
	EXPECT(sysEventSend == noErr && sysEventListened == 1 && strcmp(sysEventSeen, "ping") == 0);
	EXPECT(sysEventUnregister == noErr);
	EXPECT(gestaltErr == noErr && gestaltVersion == 1);
	EXPECT(gestaltRegErr == noErr && gestaltReplaceErr == kError_Bad_Parameters);
	EXPECT(gestaltMineErr == noErr && gestaltMine[0] == 0xCAFE && gestaltMine[1] == 0xF00D && gestaltMineSize == sizeof(gestaltMine));
	EXPECT(timerCancelled && timerFired[1] == 1 && timerFired[0] == 2 && timerFired[2] == 0);
	EXPECT(timedReceiveErr == noErr && timedReceiveGot);
	EXPECT(timerEnd.lo - timerStart.lo >= 30 * kMilliseconds);
	EXPECT(timeUnitsMs == kMilliseconds && timeConverted == 3);
	EXPECT(wrapAfter.time.hi == wrapBefore.time.hi + 1 && wrapAfter.time.lo == 0x1000 && wrapLater.time.lo == 0x3000);
	EXPECT(wrapAfter.ConvertTo(kMacTicks) == (0x100001000ull + kMacTicks / 2) / kMacTicks);
	EXPECT(appWorldInit == noErr && appWorldTaskId != 0 && appWorldLookup == noErr && appWorldRegisteredPort != 0);
	EXPECT(appWorldRPC == noErr && strcmp(appWorldSeen, "event") == 0 && strcmp(appWorldReply, "EVENT") == 0 && appWorldReplySize == sizeof(TTestEvent));
	EXPECT(appWorldNoHandler == eNoHandler);
	// the loader world ran in the 'user' environment and started the 'main' task there
	EXPECT(mainTaskId != 0 && mainTaskName == 'main' && FindId(kMemObjEnvironment, 'user', &id) && id == mainEnvironmentId);
	EXPECT(appWorldIdles == 3 && appWorldSysEvents == 1 && appWorldDone == 1);
	// what the scenario leaves behind: the echo task and the monitor's task
	// (each a task and its two shared memory objects) await the scavenger,
	// and the monitor itself is not deleted
	EXPECT(objectsAfter <= objectsBefore + 7 + 9);		// plus the waiter, listener and app world tasks
	EXPECT(gNumberOfTaskSwaps >= 10);

	if (failures == 0)
		printf("test_Boot: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
