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


// ROM 0x00394318 MonitorEntryGlue (assembly)
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


// ROM 0x003943ac TaskKillSelf (assembly)
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


// ROM 0x003ae158 BadExit
extern "C" void
BadExit(void)
{
	TaskKillSelf();
}
