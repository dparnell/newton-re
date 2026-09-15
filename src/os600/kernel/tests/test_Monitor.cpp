// Host unit test for monitors (src/os600/kernel/Monitor.*).  A monitor is
// assembled by hand (TMonitor::Init needs the memory system), tasks call it
// through the dispatch glue with the selector in their saved r1, and the
// monitor task "returns" through the exit glue.

#include "Monitor.h"
#include "Task.h"
#include "Scheduler.h"
#include "ObjectTable.h"
#include "SharedMem.h"
#include "KernelGlobals.h"
#include "KernelObjects.h"
#include "Reboot.h"
#include "OSErrors.h"
#include "MonitorGlue.h"
#include "hal/host/Host.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static TObjectTable table;
static TScheduler scheduler;
static TDoubleQContainer blockedOnMemory(offsetof(TTask, fMonitorQItem));

static TTask* MakeTask(ULong priority)
{
	TTask* t = new TTask;
	t->fPriority = priority;
	t->fPSR = 0x10;
	table.Add(t, kTaskType, 1);
	return t;
}

// the running task is never in a scheduler bucket; a blocking call leaves gCurrentTask nil
static void Run(TTask* t)
{
	if (t->fState & kTaskState_Scheduled)
		scheduler.Remove(t);
	gCurrentTask = t;
}

static Boolean IsScheduled(TTask* t)	{ return (t->fState & kTaskState_Scheduled) != 0; }
static Boolean IsBlocked(TTask* t)		{ return !IsScheduled(t) && gCurrentTask != t; }

static TRegister Reg(const void* p)		{ return (TRegister) p; }

static void MonitorProc(void*, ULong, void*) {}

// a monitor as TMonitor::Init would leave it, minus the real stack
static TMonitor* MakeMonitor(TTask* owner, int object)
{
	TMonitor* m = new TMonitor;
	table.Add(m, kMonitorType, owner->fId);
	m->fProc = MonitorProc;
	m->fMonitorObject = (void*) (uintptr_t) object;
	m->fMonitorTask = new TTask;
	m->fMonitorTask->fPriority = 20;
	m->fMonitorTask->fGlobalsBase = 0x8000;
	m->fMonitorTaskId = table.Add(m->fMonitorTask, kTaskType, 1);
	m->fMonitorTask->fMonitorId = m->fId;
	TSharedMemMsg* msg = new TSharedMemMsg;
	msg->Init(nil);
	m->fMsgId = table.Add(msg, kSharedMemMsgType, m->fId);
	return m;
}

// what the SWI stub and MonitorDispatchKernelGlue do for a caller
static NewtonErr Call(TTask* caller, TMonitor* m, long selector, void* userObject)
{
	Run(caller);
	caller->fRegister[0] = m->fId;
	caller->fRegister[1] = selector;
	caller->fRegister[2] = Reg(userObject);
	caller->fRegister[15] = 0x1000;
	return MonitorDispatchKernelGlue();
}

// the monitor task has run its proc and returns `result`
static NewtonErr Exit(TMonitor* m, long result)
{
	Run(m->fMonitorTask);
	gCurrentMonitorId = m->fId;
	return MonitorExitKernelGlue(result);
}

int main()
{
	table.Init();
	gObjectTable = &table;
	gKernelScheduler = &scheduler;
	gBlockedOnMemory = &blockedOnMemory;

	TTask* idle = MakeTask(0);
	gIdleTask = idle;
	Run(idle);
	TTask* owner = MakeTask(10);
	TTask* a = MakeTask(10);
	TTask* b = MakeTask(10);
	TMonitor* m = MakeMonitor(owner, 0x1234);
	TTask* mt = m->fMonitorTask;
	int userObject;

	// --- a call dispatches the monitor task and blocks the caller ------------
	EXPECT(Call(a, m, 7, &userObject) == noErr);
	EXPECT(IsBlocked(a) && m->fCaller == a && m->fQueueCount == 1);
	EXPECT(IsScheduled(mt) && mt->fMonitorCaller == a && a->fInsideMonitorId == m->fId);
	EXPECT(mt->fRegister[0] == 0x1234 && mt->fRegister[1] == 7 && mt->fRegister[2] == Reg(&userObject));
	EXPECT(mt->fRegister[3] == Reg((void*) MonitorProc) && mt->fRegister[15] == Reg((void*) MonitorEntryGlue));
	EXPECT(mt->fRegister[13] == 0x8000);

	// a second caller waits its turn
	EXPECT(Call(b, m, 8, nil) == noErr);
	EXPECT(IsBlocked(b) && m->fQueue.Peek() == b && m->fQueueCount == 2);

	// --- the proc returns: the caller gets the result and runs next, b is dispatched
	EXPECT(Exit(m, 42) == noErr);
	EXPECT(IsScheduled(a) && a->fRegister[0] == 42 && a->fRegister[15] == 0x1000 && scheduler.fPreferredTask == a);
	EXPECT(a->fInsideMonitorId == 0 && m->fQueueCount == 1);
	EXPECT(m->fCaller == b && IsScheduled(mt) && mt->fRegister[1] == 8 && mt->fMonitorCaller == b);
	EXPECT(Exit(m, -1) == noErr);
	EXPECT(IsScheduled(b) && (long) b->fRegister[0] == -1 && m->fCaller == nil && m->fQueueCount == 0);
	EXPECT(!IsScheduled(mt));

	// --- bad ids and registers ------------------------------------------------
	Run(a);
	a->fRegister[0] = 0x9990 | kMonitorType;
	EXPECT(MonitorDispatchKernelGlue() == kError_Bad_ObjectId);
	gCurrentMonitorId = 0x9990 | kMonitorType;
	EXPECT(MonitorExitKernelGlue(0) == kError_Bad_ObjectId);
	EXPECT(MonitorFlushKernelGlue(gCurrentMonitorId) == kError_Bad_ObjectId);
	m->SetCallerRegister(5, 99);											// no caller: ignored

	// --- an exception in the proc unwinds in the caller -----------------------
	EXPECT(Call(a, m, 1, nil) == noErr);
	Run(mt);
	gCurrentMonitorId = m->fId;
	char name[] = "evt.ex.test";
	EXPECT(MonitorThrowKernelGlue(name, (void*) 0x55, nil) == noErr);
	EXPECT(IsScheduled(a) && a->fRegister[0] == Reg(name) && a->fRegister[1] == 0x55 && a->fRegister[2] == 0);
	EXPECT(a->fRegister[15] == Reg((void*) Throw) && m->fCaller == nil);
	// ... unless the caller was not in user mode: that is a reboot
	EXPECT(Call(a, m, 1, nil) == noErr);
	a->fPSR = 0x13;
	Run(mt);
	EXPECT(MonitorThrowKernelGlue(name, nil, nil) == noErr && gHostResetCount == 1);
	a->fPSR = 0x10;
	EXPECT(Exit(m, 0) == noErr);

	// --- flush lets waiters go without an answer -------------------------------
	EXPECT(Call(a, m, 1, nil) == noErr && Call(b, m, 2, nil) == noErr);
	b->fRegister[0] = 0x7777;
	Run(idle);
	EXPECT(MonitorFlushKernelGlue(m->fId) == noErr);
	EXPECT(IsScheduled(b) && b->fRegister[0] == 0x7777 && m->fQueueCount == 1 && m->fCaller == a);
	EXPECT(Exit(m, 0) == noErr);

	// --- a killed caller resumes in TaskKillSelf --------------------------------
	EXPECT(Call(a, m, 1, nil) == noErr);
	a->fState |= kTaskState_KillPending;
	EXPECT(Exit(m, 0) == noErr);
	EXPECT(!IsScheduled(a) && a->fRegister[15] == Reg((void*) TaskKillSelf));
	a->fState &= ~kTaskState_KillPending;

	// --- fault monitor calls -----------------------------------------------------
	m->fFaultMonitor = true;
	Run(a);
	a->fRegister[0] = 0xabc;
	a->fRegister[15] = 0x2000;
	a->fState |= kTaskState_FaultMonitorCall;
	EXPECT(m->Aquire() == noErr);
	TSharedMemMsg* msg;
	EXPECT(ConvertIdToObj(kSharedMemMsgType, m->fMsgId, &msg) == noErr);
	EXPECT(msg->fBuffer == &a->fRegister[0] && msg->fSize == kFaultRegisterBlockSize && msg->fFlags == kSMemReadOnly);
	EXPECT(mt->fRegister[1] == (ULong) kMonitorFaultSelector);
	EXPECT(Exit(m, 0) == noErr);
	EXPECT(IsScheduled(a) && a->fRegister[0] == 0xabc);				// 0: r0 untouched
	Run(a);
	EXPECT(m->Aquire() == noErr && Exit(m, kFaultMonitorResult_BlockOnMemory) == noErr);
	EXPECT(!IsScheduled(a) && blockedOnMemory.Peek() == a && a->fRegister[15] == 0x2000);
	blockedOnMemory.RemoveFromQueue(a);
	Run(a);
	EXPECT(m->Aquire() == noErr && Exit(m, kFaultMonitorResult_Kill) == noErr);
	EXPECT(!IsScheduled(a) && a->fRegister[15] == Reg((void*) TaskKillSelf));
	a->fState &= ~kTaskState_FaultMonitorCall;
	m->fFaultMonitor = false;

	// --- reboot protection -------------------------------------------------------
	m->fRebootProtected = true;
	EXPECT(Call(a, m, 1, nil) == noErr && gRebootProtectCount == 1);
	EXPECT(Reboot(kError_Sorry_System_Failure, 0, true) == noErr && gWantReboot && gHostResetCount == 1);
	EXPECT(Call(b, m, 1, nil) == noErr);
	EXPECT(Exit(m, 0) == noErr && gHostResetCount == 2 && gRebootProtectCount == 0);
	EXPECT(m->fCaller == nil && !IsScheduled(mt) && IsBlocked(b));	// b is held back: nothing dispatches now
	gWantReboot = false;
	m->fRebootProtected = false;
	m->fQueueCount = 0;												// (the machine would have restarted)
	ScheduleTask(b);

	// --- suspending -----------------------------------------------------------------
	EXPECT(Call(a, m, kSuspendMonitor, nil) == kError_Object_Not_Owned_By_Task);
	EXPECT(m->fSuspended == 0);
	EXPECT(Call(owner, m, kSuspendMonitor, nil) == noErr);				// answered without running the proc
	EXPECT(m->fSuspended == kMonitor_SuspendRequested && IsScheduled(owner) && owner->fRegister[0] == noErr);
	EXPECT(m->fQueueCount == 0 && m->fCaller == nil && !IsScheduled(mt));
	EXPECT(Call(a, m, 1, nil) == kError_No_Such_Monitor && gCurrentTask == a);
	m->fSuspended = 0;

	// --- destroying the monitor fails its waiters and disposes of its objects ----
	EXPECT(Call(a, m, 1, nil) == noErr && Call(b, m, 2, nil) == noErr);
	TObjectId msgId = m->fMsgId, taskId = m->fMonitorTaskId;
	table.Remove(m->fId);
	DeleteMonitor(m);
	EXPECT(IsScheduled(b) && b->fRegister[0] == kError_No_Such_Monitor);
	EXPECT(table.Get(msgId) == nil && table.Get(taskId) == nil);

	if (failures == 0)
		printf("test_Monitor: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
