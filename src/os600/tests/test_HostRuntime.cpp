// Host runtime test (docs/host-runtime.md): tasks on threads, talking through
// the real system-call stubs, scheduled by the real kernel.  A client task
// sends three messages to a server task and gets replies, calls a monitor
// twice, has a receive time out through the timer engine (the idle task
// advances the controllable clock to the deadline), then ends the run.

#include "Task.h"
#include "Scheduler.h"
#include "ObjectTable.h"
#include "Port.h"
#include "Monitor.h"
#include "TimerEngine.h"
#include "KernelGlobals.h"
#include "OSErrors.h"
#include "MonitorGlue.h"
#include "UserGlobals.h"
#include "host/TaskRuntime.h"
#include "hal/Timer.h"

#include <stddef.h>
#include <stdio.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static TObjectTable table;
static TScheduler scheduler;
static TTimerEngine timerEngine;
static TDoubleQContainer timerDeferred(offsetof(TSharedMemMsg, fTimerQItem));
static TDoubleQContainer copyTasks(offsetof(TTask, fCopyQItem));
static TDoubleQContainer deferredSends(offsetof(TSharedMemMsg, fTimerQItem));
static TDoubleQContainer blockedOnMemory(offsetof(TTask, fMonitorQItem));

static TObjectId portId, serverMsgId, clientMsgId, monitorId;
static TTask* serverTask;
static TTask* clientTask;

// what the tasks observed, checked by the main thread afterwards
static int serverReceived = 0;
static ULong serverTypes[3];
static long clientReplies[3];
static long monitorResults[2];
static int monitorCalls = 0;
static long timeoutResult = 0;
static TObjectId monitorSawCaller = 0;
static Boolean clientFinished = false;

static TTask* MakeTask(ULong priority, ULong name)
{
	TTask* t = new TTask;
	t->fPriority = priority;
	t->fName = name;
	t->fPSR = 0x10;
	table.Add(t, kTaskType, 1);
	return t;
}

static TSharedMemMsg* MakeMsg(TObjectId owner)
{
	TSharedMemMsg* m = new TSharedMemMsg;
	m->Init(nil);
	table.Add(m, kSharedMemMsgType, owner);
	return m;
}


// --- the tasks -----------------------------------------------------------------

static void ServerEntry(TRegister, TRegister, TRegister, TRegister)
{
	for (int i = 0; i < 3; i++)
	{
		ULong senderMsgId = 0, replyMemId = 0, msgType = 0, signature = 0;
		long err = PortReceiveSWI(portId, serverMsgId, kMsgType_MatchAll, 0, &senderMsgId, &replyMemId, &msgType, &signature);
		if (err != noErr || senderMsgId != clientMsgId)
			break;
		serverTypes[serverReceived++] = msgType;
		SMemMsgMsgDoneSWI(senderMsgId, 100 + i, signature);
	}
	// nothing more to do: wait forever on the port
	PortReceiveSWI(portId, serverMsgId, kMsgType_MatchAll, 0, nil, nil, nil, nil);
}

static long MonitorProc(void* monitorObject, ULong selector, void* userObject)
{
	monitorCalls++;
	monitorSawCaller = gCurrentMonitorId;
	return (long) selector * 2 + (long) (uintptr_t) monitorObject + *(int*) userObject;
}

static void ClientEntry(TRegister r0, TRegister, TRegister, TRegister)
{
	EXPECT(r0 == 0xC1);
	for (int i = 0; i < 3; i++)
		clientReplies[i] = PortSendSWI(portId, clientMsgId, 0, 0x100 + i, 0);

	int userObject = 7;
	monitorResults[0] = MonitorDispatchSWI(monitorId, 5, &userObject);
	monitorResults[1] = MonitorDispatchSWI(monitorId, 8, &userObject);

	SMemMsgSetTimerParmsSWI(clientMsgId, 1000, 0, 0);
	timeoutResult = PortReceiveSWI(portId, clientMsgId, kMsgType_MatchAll, kPortFlags_WantTimeout, nil, nil, nil, nil);

	clientFinished = true;
	HostStopTasks();
}


int main()
{
	table.Init();
	gObjectTable = &table;
	gKernelScheduler = &scheduler;
	gTimerEngine = &timerEngine;
	gTimerDeferred = &timerDeferred;
	gCopyTasks = &copyTasks;
	gDeferredSends = &deferredSends;
	gBlockedOnMemory = &blockedOnMemory;

	TTask* idle = MakeTask(kIdleTaskPriority, 'idle');
	gIdleTask = idle;
	gCurrentTask = idle;											// as OsBoot leaves it before the first task is added
	serverTask = MakeTask(kUserTaskPriority, 'serv');
	clientTask = MakeTask(kUserTaskPriority, 'clnt');

	TPort* port = new TPort;
	portId = table.Add(port, kPortType, serverTask->fId);
	serverMsgId = MakeMsg(serverTask->fId)->fId;
	clientMsgId = MakeMsg(clientTask->fId)->fId;

	// a monitor, as TMonitor::Init would build it
	TMonitor* monitor = new TMonitor;
	monitorId = table.Add(monitor, kMonitorType, 1);
	monitor->fProc = (MonitorProcPtr) MonitorProc;
	monitor->fMonitorObject = (void*) 1000;
	monitor->fMonitorTask = MakeTask(gMonitorTaskPriority, 'MNTR');
	monitor->fMonitorTaskId = monitor->fMonitorTask->fId;
	monitor->fMonitorTask->fMonitorId = monitorId;
	monitor->fMsgId = MakeMsg(monitorId)->fId;

	serverTask->fRegister[kcPC] = (TRegister) ServerEntry;
	clientTask->fRegister[kcPC] = (TRegister) ClientEntry;
	clientTask->fRegister[kcR0] = 0xC1;
	ScheduleTask(serverTask);
	ScheduleTask(clientTask);

	HostRunTasks(idle);

	EXPECT(clientFinished);
	EXPECT(serverReceived == 3 && serverTypes[0] == 0x100 && serverTypes[1] == 0x101 && serverTypes[2] == 0x102);
	EXPECT(clientReplies[0] == 100 && clientReplies[1] == 101 && clientReplies[2] == 102);
	EXPECT(monitorCalls == 2 && monitorResults[0] == 5 * 2 + 1000 + 7 && monitorResults[1] == 8 * 2 + 1000 + 7);
	EXPECT(monitorSawCaller == monitorId);
	EXPECT(monitor->fCaller == nil && monitor->fQueueCount == 0);
	EXPECT(timeoutResult == kError_Message_Timed_Out);
	Int64 now;
	GetClock(&now);
	EXPECT(now.lo >= 1000);												// the idle task moved the clock to the deadline
	EXPECT(gNumberOfTaskSwaps >= 8);

	if (failures == 0)
		printf("test_HostRuntime: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
