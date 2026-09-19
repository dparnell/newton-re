// Host unit test for the object manager (src/os600/kernel/ObjectManager.*):
// the request handlers as the monitor proc dispatches them, and the object
// table's scavenge proc.  Object creation (ObjectAlloc) is not reconstructed
// yet, so objects are made by hand.

#include "ObjectManager.h"
#include "ObjectTable.h"
#include "Task.h"
#include "Scheduler.h"
#include "Monitor.h"
#include "Port.h"
#include "Semaphore.h"
#include "Domain.h"
#include "Environment.h"
#include "MemArchManager.h"
#include "KernelGlobals.h"
#include "OSErrors.h"

#include <new>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static TObjectTable table;
static TScheduler scheduler;
static TMemArchManager memArch;
static TObjectManager manager;
static TMonitor monitor;
static TDoubleQContainer copyTasks(offsetof(TTask, fCopyQItem));

// The ROM's ObjectAlloc hands out memory that has been cleared, and a
// kernel object's constructor clears only the fields the ROM's does - a
// TSemaphoreGroup's semaphore array and its count are not among them.  An
// object made by hand here has to start cleared for the same reason, or
// its destructor frees whatever the host's heap happened to leave behind:
// the test segfaulted on about one run in fifteen, always while destroying
// the semaphore group.
template <class T> static T* NewCleared()
{
	void* memory = ::operator new(sizeof(T));
	memset(memory, 0, sizeof(T));
	return new (memory) T;
}

static TTask* MakeTask(ULong priority, TObjectId owner = 1)
{
	TTask* t = NewCleared<TTask>();
	t->fPriority = priority;
	table.Add(t, kTaskType, owner);
	return t;
}

static Boolean IsScheduled(TTask* t)	{ return (t->fState & kTaskState_Scheduled) != 0; }

// a request from `caller`, as the monitor would present it
static NewtonErr Request(TTask* caller, long selector, ObjectMessage& msg)
{
	monitor.fCaller = caller;
	return manager.MonitorProc(selector, &msg);
}

static ObjectMessage Msg(ULong size, TObjectId id)
{
	ObjectMessage m;
	memset(&m, 0, sizeof(m));
	m.fSize = size;
	m.fObjectId = id;
	return m;
}

int main()
{
	table.Init();
	table.SetScavengeProc(ObjectScavenger);
	gObjectTable = &table;
	gKernelScheduler = &scheduler;
	gTheMemArchManager = &memArch;
	gTheObjectManager = &manager;
	gTheObjectManagerMonitor = &monitor;
	gCopyTasks = &copyTasks;
	TTask* idle = MakeTask(0);
	gIdleTask = idle;
	gCurrentTask = idle;
	TTask* owner = MakeTask(10);
	TTask* other = MakeTask(10);
	TTask* worker = MakeTask(10, owner->fId);
	table.Add(&monitor, kMonitorType, 1);

	// --- start, suspend --------------------------------------------------------
	ObjectMessage m = Msg(kObjectMessage_HeaderSize, worker->fId);
	EXPECT(Request(owner, kObjectMgr_Start, m) == noErr && IsScheduled(worker));
	EXPECT(Request(owner, kObjectMgr_Suspend, m) == kError_Cannot_Suspend_Blocked_Task && IsScheduled(worker));
	worker->fState |= kTaskState_Unknown0008;
	EXPECT(Request(owner, kObjectMgr_Suspend, m) == noErr && !IsScheduled(worker));
	m = Msg(kObjectMessage_HeaderSize, 0x9990 | kTaskType);
	EXPECT(Request(owner, kObjectMgr_Suspend, m) == noErr);			// unknown task: nothing to do
	m = Msg(kObjectMessage_HeaderSize, monitor.fId);
	EXPECT(Request(owner, kObjectMgr_Start, m) == kError_Bad_Parameters);	// not a task
	m = Msg(kObjectMessage_HeaderSize + 4, worker->fId);
	EXPECT(Request(owner, kObjectMgr_Start, m) == kError_Bad_Parameters);	// wrong size
	EXPECT(Request(owner, kObjectMgr_Unused2, m) == kError_Bad_Parameters);

	// --- registers ---------------------------------------------------------------
	worker->fRegister[3] = 0x333;
	m = Msg(kObjectMessage_GetRegisterSize, worker->fId);
	m.fRegister.fNumber = 3;
	EXPECT(Request(owner, kObjectMgr_GetRegister, m) == noErr && m.fValue == 0x333);
	m.fRegister.fNumber = 16;
	EXPECT(Request(owner, kObjectMgr_GetRegister, m) == kError_Bad_Register_Number);
	m = Msg(kObjectMessage_SetRegisterSize, worker->fId);
	m.fRegister.fNumber = 15;
	m.fRegister.fValue = 0x8000;
	EXPECT(Request(owner, kObjectMgr_SetRegister, m) == noErr && worker->fRegister[15] == 0x8000);
	m.fSize = kObjectMessage_GetRegisterSize;
	EXPECT(Request(owner, kObjectMgr_SetRegister, m) == kError_Bad_Parameters);

	// --- content ------------------------------------------------------------------
	worker->fName = 'WORK';
	worker->fTaskTime.hi = 1;
	worker->fTaskTime.lo = 2;
	worker->fStackSize = 0x1000;
	worker->fPtrsUsed = 3;
	worker->fHandlesUsed = 4;
	worker->fMaxMemoryUsed = 5;
	m = Msg(kObjectMessage_GetContentSize, worker->fId);
	m.fContentRequest.fKind = 1;
	EXPECT(Request(owner, kObjectMgr_GetContent, m) == noErr);
	ObjectContentReply* content = (ObjectContentReply*) &m;
	EXPECT(content->fPriority == 10 && content->fName == 'WORK' && content->fTaskTimeHi == 1 && content->fTaskTimeLo == 2);
	EXPECT(content->fStackSize == 0x1000 && content->fPtrsUsed == 3 && content->fHandlesUsed == 4 && content->fMaxMemoryUsed == 5);
	EXPECT(gHoldScheduleLevel == 0);
	m = Msg(kObjectMessage_GetContentSize, 0x9990 | kTaskType);
	m.fContentRequest.fKind = 1;
	EXPECT(Request(owner, kObjectMgr_GetContent, m) == kError_Task_No_Longer_Exists);
	m.fContentRequest.fKind = 2;
	EXPECT(Request(owner, kObjectMgr_GetContent, m) == kError_Bad_Parameters);

	// --- domains and environments -----------------------------------------------------
	TKDomain* domain = NewCleared<TKDomain>();
	table.Add(domain, kDomainType, owner->fId);
	memArch.AddDomain(domain);
	TEnvironment* env = NewCleared<TEnvironment>();
	table.Add(env, kEnvironmentType, owner->fId);
	env->Init(nil);
	m = Msg(kObjectMessage_AddDomainSize, env->fId);
	m.fEnvDomain.fDomainId = domain->fId;
	m.fEnvDomain.fIsManager = true;
	m.fEnvDomain.fIsStack = true;
	EXPECT(Request(owner, kObjectMgr_AddDomain, m) == noErr);
	EXPECT(env->fDomainAccess == (5 | (3 << (2 * domain->fNumber))) && env->fStackDomainId == domain->fId);
	m.fSize = kObjectMessage_RemoveDomainSize;
	EXPECT(Request(owner, kObjectMgr_RemoveDomain, m) == noErr && env->fDomainAccess == 5);
	m.fObjectId = domain->fId;												// not an environment
	EXPECT(Request(owner, kObjectMgr_RemoveDomain, m) == kError_Bad_Parameters);
	m = Msg(kObjectMessage_SetFaultMonitorSize, 0);
	m.fDomain.fMonitorId = domain->fId;
	m.fDomain.fBase = monitor.fId;
	EXPECT(Request(owner, kObjectMgr_SetFaultMonitor, m) == noErr);
	EXPECT(domain->fFaultMonitorId == monitor.fId && gFaultMonitorTable[domain->fNumber].fMonitorId == monitor.fId);
	m.fDomain.fBase = 0x9990 | kMonitorType;
	EXPECT(Request(owner, kObjectMgr_SetFaultMonitor, m) == kError_Bad_ObjectId);
	m.fDomain.fMonitorId = 0x9990 | kDomainType;
	EXPECT(Request(owner, kObjectMgr_SetFaultMonitor, m) == kError_Bad_Parameters);

	// --- destroy: owners only; the scavenger picks the destructor --------------------------
	TPort* port = NewCleared<TPort>();
	TObjectId portId = table.Add(port, kPortType, owner->fId);
	m = Msg(kObjectMessage_HeaderSize, portId);
	EXPECT(Request(other, kObjectMgr_Destroy, m) == kError_Object_Not_Owned_By_Task && table.Get(portId) != nil);
	EXPECT(Request(owner, kObjectMgr_Destroy, m) == noErr && table.Get(portId) == nil);
	EXPECT(Request(owner, kObjectMgr_Destroy, m) == kError_Bad_ObjectId);
	EXPECT(ObjectDestroy(&m, kObjectMessage_HeaderSize, 0) == kError_Bad_ObjectId);
	// environments and domains are never scavenged
	m = Msg(kObjectMessage_HeaderSize, env->fId);
	EXPECT(Request(owner, kObjectMgr_Destroy, m) == noErr && table.Get(env->fId) == env);
	EXPECT(ObjectScavenger(domain, 0) == nil && ObjectScavenger(env, 0) == nil);
	// a task inside a monitor is only marked
	worker->fInsideMonitorId = monitor.fId;
	EXPECT(ObjectScavenger(worker, 0) == nil && (worker->fState & kTaskState_KillPending));
	// a busy monitor is suspended and left for later; an idle one goes
	TMonitor* busy = NewCleared<TMonitor>();
	table.Add(busy, kMonitorType, owner->fId);
	busy->fCaller = worker;
	EXPECT(ObjectScavenger(busy, 0) == nil && busy->fSuspended == kMonitor_Suspended);
	busy->fCaller = nil;
	EXPECT(ObjectScavenger(busy, 0) == (ObjectDestructorProcPtr) DeleteMonitor);
	TPort* port2 = NewCleared<TPort>();
	table.Add(port2, kPortType, owner->fId);
	EXPECT(ObjectScavenger(port2, 0) == (ObjectDestructorProcPtr) DeletePort);
	TSemaphoreGroup* group = NewCleared<TSemaphoreGroup>();
	table.Add(group, kSemGroupType, owner->fId);
	EXPECT(ObjectScavenger(group, 0) == (ObjectDestructorProcPtr) DeleteSemGroup);

	// --- a task kills itself ----------------------------------------------------------------
	TTask* dying = MakeTask(10);
	m = Msg(kObjectMessage_HeaderSize, 0);
	EXPECT(Request(dying, kObjectMgr_KillSelf, m) == noErr);
	EXPECT((dying->fState & kTaskState_KilledSelf) && manager.fTaskToDelete == dying->fId && table.Get(dying->fId) == dying);
	m = Msg(kObjectMessage_HeaderSize, group->fId);
	EXPECT(Request(owner, kObjectMgr_Destroy, m) == noErr);				// the next request removes it
	EXPECT(manager.fTaskToDelete == 0);
	EXPECT(table.Get(dying->fId) == nil && gTaskDestroyed == false);	// deleted, and the scavenge that followed cleared the flag

	// --- a destroyed task's leftovers are scavenged at the end of the request ------------------
	gTaskDestroyed = true;
	TPort* orphan = NewCleared<TPort>();
	TObjectId orphanId = table.Add(orphan, kPortType, 0x9990 | kTaskType);	// owner does not exist
	m = Msg(kObjectMessage_GetRegisterSize, worker->fId);
	EXPECT(Request(owner, kObjectMgr_GetRegister, m) == noErr);
	EXPECT(!gTaskDestroyed && table.Get(orphanId) == nil);

	if (failures == 0)
		printf("test_ObjectManager: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
