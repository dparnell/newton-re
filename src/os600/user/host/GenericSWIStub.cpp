/*
	File:		user/host/GenericSWIStub.cpp

	Contains:	GenericSWI, the variadic stub (UserGlobals.h declares it as
				`long GenericSWI(...)`, which C++ cannot pick arguments from, so
				this definition names the selector and lives in its own file).
				SWI 5: the selector and up to four arguments, one kernel routine.

	ROM:		GenericSWI 0x003a4af4 (assembly)
*/

#include "Newton.h"
#include "KernelGlobals.h"
#include "Task.h"
#include "GenericSWI.h"
#include "host/TaskRuntime.h"
#include "OSErrors.h"

#include <stdarg.h>

extern "C" long
GenericSWI(ULong selector, ...)
{
	va_list args;
	va_start(args, selector);
	ULong p1 = va_arg(args, ULong);
	ULong p2 = va_arg(args, ULong);
	ULong p3 = va_arg(args, ULong);
	ULong p4 = va_arg(args, ULong);
	va_end(args);
	// no task to make the call (before OsBoot, or after the run has ended):
	// refused, as SWI.cpp's Enter refuses the rest
	if (gHostTasksStopping || gCurrentTask == nil)
		return kError_Call_Aborted;
	TTask* self = gCurrentTask;
	self->fRegister[kcPC] = kResumeInStub;
	long result = GenericSWIHandler(selector, p1, p2, p3, p4);
	self->fRegister[kcR0] = (TRegister) result;		// (as SWI.cpp's ExitWithResult: the answer stands if the task is switched out at the exit)
	if (HostSWIExit(self, kResumeInStub))
		return (long) self->fRegister[kcR0];
	return result;
}
