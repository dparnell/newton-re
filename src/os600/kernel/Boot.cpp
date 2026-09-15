/*
	File:		Boot.cpp

	Contains:	OsBoot and the initialisation routines it calls.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
	Steps that belong to subsystems not reconstructed yet - the MMU-side of the
	memory architecture, the memory object database (MemObjManager), the
	real-time clock, the tablet - are marked NOT YET RECONSTRUCTED where they
	would go, so the boot's shape is the ROM's.
*/

#include "Boot.h"
#include "KernelGlobals.h"
#include "KernelObjects.h"
#include "ObjectTable.h"
#include "ObjectManager.h"
#include "Task.h"
#include "Scheduler.h"
#include "TimerEngine.h"
#include "SharedMem.h"
#include "Port.h"
#include "Environment.h"
#include "Domain.h"
#include "MemArchManager.h"
#include "TaskSwitch.h"
#include "MemObjManager.h"
#include "os600/TaskGlobals.h"
#include "OSErrors.h"
#include "hal/Atomic.h"
#include "hal/Interrupts.h"
#include "hal/Timer.h"
#include "hal/System.h"
#include "UserBoot.h"

#include <stddef.h>
#include <stdlib.h>


void*			gKernelHeap = nil;
TObjectId		gKernelDomainId = 0;

// the message the timer engine's overflow detector rides on
static TSharedMemMsg	gOverflowDetectMsg;		// 0x0c101668


// ROM 0x001e22c8 TaskInCopyKilled__FPvP5TTask
// gCopyTasks' destructor proc: a task deleted mid-copy lets go of the message
// it was copying under.
static void
TaskInCopyKilled(void* /*unused*/, TTask* task)
{
	TSharedMemMsg* msg = nil;
	ConvertIdToObj(kSharedMemMsgType, task->fCopyMsgId, &msg);
	if (msg != nil)
		msg->fCopyingTaskId = 0;
}


// ROM 0x001e2804 InitSMemManager__Fv
static NewtonErr
InitSMemManager()
{
	return noErr;
}


// ROM 0x000fb658 InitGlobalWorld__Fv
// The scheduler, the task queues shared memory copies and fault monitors
// use, the object manager and the null port (owned by nobody, never receives).
void
InitGlobalWorld()
{
	gSchedule = false;
	gKernelScheduler = new TScheduler;
	gCopyTasks = new TDoubleQContainer(offsetof(TTask, fCopyQItem), (DestructorProcPtr) TaskInCopyKilled, nil);
	gBlockedOnMemory = new TDoubleQContainer(offsetof(TTask, fMonitorQItem));
	gDeferredSends = new TDoubleQContainer(offsetof(TSharedMemMsg, fTimerQItem));
	InitSMemManager();
	TObjectId kernelEnvId = 0;
	MemObjManager::FindEnvironmentId('krnl', &kernelEnvId);
	InitObjectManager(ObjectType(kernelEnvId) == kEnvironmentType ? (TEnvironment*) gObjectTable->Get(kernelEnvId) : nil);
	TPort* nullPort = new TPort;
	gNullPort = nullPort;
	if (nullPort != nil)
		gObjectTable->Add(nullPort, kPortType, 1);
}


const long kKernelDomainNumber = 2;			// VirtualMemory.h: kKernelDomainHeapDomainNumber


// ROM 0x000ea698 InitKernelDomainAndEnvironment__Fv
// The kernel's environment ('krnl') on the kernel heap, and its domain -
// number 2, covering the range the memory object database gives for 'krnl'.
void
InitKernelDomainAndEnvironment()
{
	TEnvironment* env = new TEnvironment;
	env->Init(gKernelHeap);
	TObjectId envId;
	RegisterObject(env, kEnvironmentType, 1, &envId);
	TKDomain* domain = new TKDomain;
	TObjectId domainId;
	RegisterObject(domain, kDomainType, 1, &domainId);
	DomainInfo info;
	MemObjManager::GetDomainInfoByName('krnl', &info);
	domain->InitWithDomainNumber(0, info.Base(), info.Size(), kKernelDomainNumber);
	env->Add(domain, false, false, false);
	MemObjManager::RegisterEnvironmentId('krnl', envId);
	MemObjManager::RegisterDomainId('krnl', domainId);
	gKernelDomainId = domainId;
}


// ROM 0x00045c84 InitCGlobals +0x374 (the memory object database)
// InitCGlobals, before OsBoot, picks the domain table for the RAM fitted and
// lays the memory object database out in RAM (gMemObjHeap, at a computed
// address); here the database is allocated.
void
InitMemObjDatabase(ULong ramSize)
{
	SelectDomainTable(ramSize);
	ULong size;
	ComputeMemObjDatabaseSize(&size);
	gMemObjHeap = malloc(size);
	BuildMemObjDatabase();
}


// ROM 0x0011e660 InitMemArchCore__Fv
// The memory architecture's own object table, its manager and the fault
// monitor table.
void
InitMemArchCore()
{
	// NOT YET RECONSTRUCTED: gPrimaryTable = GetPrimaryTablePhysBaseAfterGlobalsInitied()
	gTheMemArchObjTbl = new TObjectTable;
	gTheMemArchObjTbl->Init();
	// NOT YET RECONSTRUCTED: gThePageManager, gThePageTableManager, InitPTable()
	gTheMemArchManager = new TMemArchManager;
	for (ULong i = 0; i < kNumberOfDomains; i++)
	{
		gFaultMonitorTable[i].fDomainId = 0;
		gFaultMonitorTable[i].fMonitorId = 0;
	}
}


// ROM 0x0013ebac RestartTimerOverflowDetect__FPv
// Keeps the 64-bit clock's wrap count right by sampling the counter at least
// once per 0xd2f0000 ticks (about an hour).
static void
RestartTimerOverflowDetect(void* /*unused*/)
{
	UpdateClock();
	gTimerEngine->QueueTimer(&gOverflowDetectMsg, 0xd2f0000, nil, RestartTimerOverflowDetect);
}


// ROM 0x0013ec34 StartTime
void
StartTime()
{
	gTimerInterruptCount = 0;
	gTimerEngine->Start();
	UpdateClock();
	gTimerEngine->QueueTimer(&gOverflowDetectMsg, 0xd2f0000, nil, RestartTimerOverflowDetect);
}


// ROM 0x00149c1c OsBoot
// The boot runs with a task and environment on its own stack standing in as
// the current ones until the idle task exists; from SwapInGlobals on, the boot
// context *is* the idle task.  Everything up to the first task's start happens
// with FIQs off.
void
OsBoot()
{
	TTask bootTask;
	TEnvironment bootEnvironment;
	TaskGlobals bootGlobals;					// the ROM's 0x58-byte scratch below its globals pointer
	bootTask.fPriority = 0x15;
	gCurrentTask = &bootTask;
	bootTask.fEnvironment = &bootEnvironment;
	gCurrentGlobals = &bootGlobals + 1;

	InitMemObjDatabase(GetRamSize());			// InitCGlobals's work, done before OsBoot on the MessagePad
	HInitInterrupts();
	InitInterruptTables();
	gObjectTable = new TObjectTable;
	gObjectTable->Init();
	InitMemArchCore();
	InitKernelDomainAndEnvironment();
	TObjectId kernelEnvId = 0;
	MemObjManager::FindEnvironmentId('krnl', &kernelEnvId);
	TEnvironment* kernelEnv = ObjectType(kernelEnvId) == kEnvironmentType ? (TEnvironment*) gObjectTable->Get(kernelEnvId) : nil;

	EnterFIQAtomic();
	InitGlobalWorld();
	InitTime();
	// NOT YET RECONSTRUCTED: TRealTimeClock::InitRealTimeClock 0x0019ed98
	UserInit();

	gIdleTask = new TTask;
	RegisterObject(gIdleTask, kTaskType, 1, nil);
	gIdleTask->Init((TaskProcPtr) OsBoot, 0, nil, 0, kIdleTaskPriority, 'idle', kernelEnv);
	gCurrentTask = gIdleTask;
	SwapInGlobals(gIdleTask);

	TTask* userTask = new TTask;
	TObjectId userTaskId;
	RegisterObject(userTask, kTaskType, 1, &userTaskId);
	userTask->Init((TaskProcPtr) UserBoot, 0x800, (void*) (uintptr_t) userTaskId, 0, kKernelTaskPriority, 'user', kernelEnv);

	StartTime();
	// NOT YET RECONSTRUCTED: TabBoot 0x0024e150 (the tablet)
	gCountTaskTime = true;
	StopScheduler();
	StartScheduler();
	ExitFIQAtomic();
	EnterAtomic();
	gKernelScheduler->Add(userTask);
	ExitAtomic();
	SleepTask();
}
