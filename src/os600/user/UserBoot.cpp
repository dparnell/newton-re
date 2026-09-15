/*
	File:		UserBoot.cpp

	Contains:	UserInit, UserBoot and InitialKSRVTask.

	Reconstructed from the MP2100 D ROM; each function cites its origin.  The
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
#include "OSErrors.h"

#include <stdio.h>
#include <stdlib.h>

void (*gHostKernelServicesTask)() = nil;


// ROM 0x002574fc UserInit__Fv
// The user side's handles on the well-known kernel objects (GetPortInfo).
void
UserInit()
{
	gUObjectMgrMonitor = new TUMonitor(GetPortSWI(kGetObjectPort));
	gUNullPort = new TUPort(GetPortSWI(kGetNullPort));
}


// ROM 0x002d1860 UserBoot__Fv
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
	// NOT YET RECONSTRUCTED: AddSemaphoreToHeap(GetHeap()); InitMemArchObjs();
	// InitDomainsAndEnvironments(); MemObjManager::FindHeapRef('user',
	// &SkiaHeapBase); InitROMDomainManager()
	gOSIsRunning = true;
	// NOT YET RECONSTRUCTED: srand(TURealTimeAlarm::Time().ConvertTo(kSeconds))
	srand(1);
	TUTask ksrv;
	TObjectId ksrvEnvId = 0;
	if (MemObjManager::FindEnvironmentId('ksrv', &ksrvEnvId) == noErr && ksrv.Init((TaskProcPtr) InitialKSRVTask, 0x6800, 0, nil, kUserTaskPriority, 'ksrv', ksrvEnvId) == noErr)
		ksrv.Start();
	SetBequeathId(gIdleTask->fId);
}


// ROM 0x002d1954 InitialKSRVTask__Fv
// The kernel services: protocol registry, stdio, the name server, the ROM
// domain manager, the package manager (in the 'prot' environment) and the
// first application world ('drvl', in the 'user' environment).
long
InitialKSRVTask()
{
	// NOT YET RECONSTRUCTED: StartupProtocolRegistry(); InitStdIO();
	// InitNameServer(); RegisterROMDomainManager();
	// InitializePackageManager(FindEnvironmentId('prot'));
	// TAppWorld('drvl').Init(...) in FindEnvironmentId('user')
	if (gHostKernelServicesTask != nil)
		gHostKernelServicesTask();
	return noErr;
}
