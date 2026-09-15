/*
	File:		user/host/MonitorGlue.cpp

	Contains:	The user-mode entry points of MonitorGlue.h for a host build.  The
				kernel makes a task resume at one of these by setting its saved
				pc; the host task runtime then calls it with no arguments, and it
				reads what the kernel left in the task's registers
				(docs/host-runtime.md).
*/

#include "MonitorGlue.h"
#include "UserGlobals.h"
#include "KernelGlobals.h"
#include "Task.h"
#include "host/TaskRuntime.h"
#include "os600/ObjectMessage.h"

#include <stdio.h>
#include <stdlib.h>


// ROM 0x0038ac98 MonitorEntryGlue (assembly)
// r0 = monitor object, r1 = selector, r2 = user object, r3 = the proc; the
// proc's result goes back through MonitorExitSWI, which does not return -
// the next call re-enters here.
extern "C" void
MonitorEntryGlue(void)
{
	TRegister* r = gCurrentTask->fRegister;
	long (*proc)(void*, ULong, void*) = (long (*)(void*, ULong, void*)) r[kcR3];
	long result = proc((void*) r[kcR0], (ULong) r[kcR1], (void*) r[kcR2]);
	MonitorExitSWI(result, nil);
	fprintf(stderr, "MonitorExitSWI returned to MonitorEntryGlue\n");
	abort();
}


// ROM 0x0038ad2c TaskKillSelf (assembly)
// Asks the object manager monitor to delete the calling task; the request
// never returns ("Task did not kill self properly!!!" otherwise).
extern "C" void
TaskKillSelf(void)
{
	TObjectId taskId = gCurrentTaskId;
	ULong monitorId = GetPortSWI(kGetObjectPort);
	MonitorDispatchSWI(monitorId, kObjectMgr_KillSelf, (void*) (uintptr_t) taskId);
	fprintf(stderr, "Task did not kill self properly!!!\n");
	abort();
}


// Throw (jump table 0x01bdff88): the exception name, data and destructor
// arrive in r0-r2 when a caller is redirected here by MonitorThrowKernelGlue
// (the runtime enters a redirected function with r0-r3 as its arguments).
// The Newton exception system (NewtonExceptions.h) is not reconstructed yet.
extern "C" void
Throw(char* name, void* data, void (*destructor)(void*))
{
	(void) data;
	(void) destructor;
	fprintf(stderr, "Throw(%s): the exception system is not reconstructed yet\n", name ? name : "");
	abort();
}
