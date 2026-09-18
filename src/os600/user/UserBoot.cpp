/*
	File:		UserBoot.cpp

	Contains:	UserInit, UserBoot and InitialKSRVTask.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.  The
	memory-system and service start-ups these call are marked NOT YET
	RECONSTRUCTED where they belong.
*/

#include "UserBoot.h"
#include "UserGlobals.h"
#include "UserMonitor.h"
#include "UserPorts.h"
#include "UserTasks.h"
#include "UserSemaphore.h"
#include "KernelGlobals.h"
#include "Task.h"
#include "MemObjManager.h"
#include "NameServerImpl.h"
#include "Loader.h"
#include "Protocols.h"
#include "OSErrors.h"

#include <stdio.h>
#include <stdlib.h>

void (*gHostKernelServicesTask)() = nil;


// ROM 0x00259434 UserInit__Fv
// The user side's handles on the well-known kernel objects (GetPortInfo).
void
UserInit()
{
	gUObjectMgrMonitor = new TUMonitor(GetPortSWI(kGetObjectPort));
	gUNullPort = new TUPort(GetPortSWI(kGetNullPort));
}


// ROM 0x002f70a4 UserBoot__Fv
// The 'user' task: the semaphore classes' shared op lists, the kernel heap's
// semaphore, the user-level memory architecture (page managers, stack
// manager, the domains and environments of the memory object database),
// then the OS is running; the clock seeds the random numbers; the kernel
// services task is made in the 'ksrv' environment and started, and this
// task's objects are left to the idle task when it ends.
void
UserBoot()
{
	TULockingSemaphore::StaticInit();
	TURdWrSemaphore::StaticInit();
	// NOT YET RECONSTRUCTED: AddSemaphoreToHeap(GetHeap()); InitMemArchObjs()
	InitDomainsAndEnvironments();
	// NOT YET RECONSTRUCTED: MemObjManager::FindHeapRef('user', &SkiaHeapBase);
	// InitROMDomainManager()
	gOSIsRunning = true;
	// NOT YET RECONSTRUCTED: srand(TURealTimeAlarm::Time().ConvertTo(kSeconds))
	srand(1);
	TUTask ksrv;
	TObjectId ksrvEnvId = 0;
	if (MemObjManager::FindEnvironmentId('ksrv', &ksrvEnvId) == noErr && ksrv.Init((TaskProcPtr) InitialKSRVTask, 0x6800, 0, nil, kUserTaskPriority, 'ksrv', ksrvEnvId) == noErr)
		ksrv.Start();
	SetBequeathId(gIdleTask->fId);
}


// ROM 0x002f7198 InitialKSRVTask__Fv
// The kernel services: protocol registry, stdio, the name server, the ROM
// domain manager, the package manager (in the 'prot' environment) and the
// loader world ('drvl', in the 'user' environment), which starts the rest;
// this task's objects go to the idle task when it ends.
long
InitialKSRVTask()
{
	StartupProtocolRegistry();
	// NOT YET RECONSTRUCTED: InitStdIO
	InitNameServer();
	// NOT YET RECONSTRUCTED: RegisterROMDomainManager
	TObjectId envId;
	if (MemObjManager::FindEnvironmentId('prot', &envId) == noErr)
	{
		// NOT YET RECONSTRUCTED: InitializePackageManager(envId)
	}
	if (MemObjManager::FindEnvironmentId('user', &envId) == noErr)
	{
		TLoader loader;
		loader.Init('drvl', true, kSpawnedTaskStackSize, kUserTaskPriority, envId);
	}
	if (gHostKernelServicesTask != nil)
		gHostKernelServicesTask();
	return SetBequeathId(gIdleTask->fId);
}
