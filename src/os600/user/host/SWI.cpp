/*
	File:		user/host/SWI.cpp

	Contains:	The system-call stubs (UserGlobals.h) for a host build.  On the
				MessagePad each is `swi #n` and SWIBoot does the rest; here each
				does what SWIBoot's case for it does - save what the kernel
				expects in the calling task's registers, call the kernel glue -
				and then HostSWIExit, the common exit path (docs/host-runtime.md).

				Results: a glue that may block writes the caller's result into
				its saved r0 (and r1-r4), so a stub reads them back from there
				after the exit path; a glue that returns its result in r0 and
				never blocks the caller is read directly unless a switch happened
				in between, when the saved r0 is what the caller would have seen.

	ROM:		the SWI stubs 0x0038a890-0x0038ad28 and 0x003a4534-0x003a4b7c,
				the SWIBoot cases 0x003a40d0-0x003a4ad4 (assembly)
*/

#include "UserGlobals.h"
#include "KernelGlobals.h"
#include "Task.h"
#include "Port.h"
#include "Monitor.h"
#include "Semaphore.h"
#include "GenericSWI.h"
#include "host/TaskRuntime.h"
#include "OSErrors.h"

#include <string.h>


// The exit for a glue that left the result in the caller's saved r0.
static inline long
ExitWithSavedResult(TTask* self)
{
	HostSWIExit(self, kResumeInStub);
	return (long) self->fRegister[kcR0];
}


// The exit for a glue that returned its result: the saved r0 stands if the
// task was switched out meanwhile.
static inline long
ExitWithResult(TTask* self, long result)
{
	if (HostSWIExit(self, kResumeInStub))
		return (long) self->fRegister[kcR0];
	return result;
}


// nil when there is no task to make the call - before OsBoot, in a host
// program that runs user-side code without booting the OS at all
// (build/host/host/newtonscript is one), or once the run has ended (static
// destructors, say): the call is refused rather than crashing on a task
// that is not there.
static inline TTask*
Enter()
{
	if (gHostTasksStopping || gCurrentTask == nil)
		return nil;
	TTask* self = gCurrentTask;
	self->fRegister[kcPC] = kResumeInStub;
	return self;
}


/* -------------------------------------------------------------------------------
	Ports
------------------------------------------------------------------------------- */

// SWI 0 (0x003a4aec GetPortSWI)
extern "C" ULong
GetPortSWI(ULong what)
{
	TTask* self = Enter();
	if (self == nil)
		return 0;
	return (ULong) ExitWithResult(self, (long) GetPortInfo(what));
}


// SWI 1
extern "C" long
PortSendSWI(ULong portId, ULong msgId, ULong replyId, ULong msgType, ULong flags)
{
	TTask* self = Enter();
	if (self == nil)
		return kError_Call_Aborted;
	PortSendKernelGlue(portId, msgId, replyId, msgType, flags);
	return ExitWithSavedResult(self);
}


// SWI 2: the sender's message, reply memory, type and sequence come back in r1-r4
extern "C" long
PortReceiveSWI(ULong portId, ULong msgId, ULong msgFilter, ULong flags, ULong* senderMsgId, ULong* replyMemId, ULong* returnMsgType, ULong* signature)
{
	TTask* self = Enter();
	if (self == nil)
		return kError_Call_Aborted;
	PortReceiveKernelGlue(portId, msgId, msgFilter, flags);
	long result = ExitWithSavedResult(self);
	if (result == noErr)
	{
		if (senderMsgId != nil)		*senderMsgId = (ULong) self->fRegister[kcR1];
		if (replyMemId != nil)		*replyMemId = (ULong) self->fRegister[kcR2];
		if (returnMsgType != nil)	*returnMsgType = (ULong) self->fRegister[kcR3];
		if (signature != nil)		*signature = (ULong) self->fRegister[kcR4];
	}
	return result;
}


// SWI 33
extern "C" long
PortResetFilterSWI(ULong portId, ULong msgId, ULong msgFilter)
{
	TTask* self = Enter();
	if (self == nil)
		return kError_Call_Aborted;
	PortResetFilterKernelGlue(portId, msgId, msgFilter);
	return ExitWithSavedResult(self);
}


/* -------------------------------------------------------------------------------
	Shared memory and messages
------------------------------------------------------------------------------- */

// SWI 13
extern "C" long
SMemSetBufferSWI(ULong id, void* buffer, ULong size, ULong permissions)
{
	TTask* self = Enter();
	if (self == nil)
		return kError_Call_Aborted;
	return ExitWithResult(self, SMemSetBufferKernelGlue(id, buffer, size, permissions));
}


// SWI 14: size, buffer (if the caller owns it) and ref con in r1-r3
extern "C" long
SMemGetSizeSWI(ULong id, ULong* returnSize, void** returnBuffer, ULong* refConPtr)
{
	TTask* self = Enter();
	if (self == nil)
		return kError_Call_Aborted;
	long result = ExitWithResult(self, SMemGetSizeKernelGlue(id));
	if (result == noErr)
	{
		if (returnSize != nil)		*returnSize = (ULong) self->fRegister[kcR1];
		if (returnBuffer != nil)	*returnBuffer = (void*) self->fRegister[kcR2];
		if (refConPtr != nil)		*refConPtr = (ULong) self->fRegister[kcR3];
	}
	return result;
}


// SWI 15, 16 (0x0038aa40, 0x0038aaa0): the glue sets the copy up in the
// task's registers; the ROM then returns into a copy loop in user mode that
// ends with SWI 26 (LowLevelCopyDone), which puts the result in r0, the size
// in r1 and the pc back where the stub was called.  Here the loop is a memcpy.
static long
RunCopy(TTask* self, long setup, ULong* returnSize)
{
	if (setup == kSMemCopy_Words || setup == kSMemCopy_Bytes)
	{
		memcpy((void*) self->fRegister[kcR0], (const void*) self->fRegister[kcR1], (size_t) self->fRegister[kcR2]);
		LowLevelCopyDoneFromKernelGlue(noErr, self, kResumeInStub);
		gCopyDone = false;
		setup = (long) self->fRegister[kcR0];
	}
	if (returnSize != nil)
		*returnSize = (ULong) self->fRegister[kcR1];
	return ExitWithResult(self, setup);
}

extern "C" long
SMemCopyToSharedSWI(ULong id, void* buffer, ULong size, ULong offset, ULong sendersMsgId, ULong signature)
{
	TTask* self = Enter();
	if (self == nil)
		return kError_Call_Aborted;
	return RunCopy(self, SMemCopyToKernelGlue(id, buffer, size, offset, sendersMsgId, signature), nil);
}

extern "C" long
SMemCopyFromSharedSWI(ULong id, void* buffer, ULong size, ULong offset, ULong sendersMsgId, ULong signature, ULong* returnSize)
{
	TTask* self = Enter();
	if (self == nil)
		return kError_Call_Aborted;
	return RunCopy(self, SMemCopyFromKernelGlue(id, buffer, size, offset, sendersMsgId, signature), returnSize);
}


// SWI 17
extern "C" long
SMemMsgSetTimerParmsSWI(ULong msgId, ULong timeout, ULong timeLow, ULong timeHigh)
{
	TTask* self = Enter();
	if (self == nil)
		return kError_Call_Aborted;
	return ExitWithResult(self, SMemMsgSetTimerParmsKernelGlue(msgId, timeout, timeLow, timeHigh));
}


// SWI 18
extern "C" long
SMemMsgSetMsgAvailPortSWI(ULong msgId, ULong availPort)
{
	TTask* self = Enter();
	if (self == nil)
		return kError_Call_Aborted;
	return ExitWithResult(self, SMemMsgSetMsgAvailPortKernelGlue(msgId, availPort));
}


// SWI 19: the sender's task id in r1
extern "C" long
SMemMsgGetSenderTaskIdSWI(ULong msgId, void* senderTaskId)
{
	TTask* self = Enter();
	if (self == nil)
		return kError_Call_Aborted;
	long result = ExitWithResult(self, SMemMsgGetSenderTaskIdKernelGlue(msgId));
	if (result == noErr && senderTaskId != nil)
		*(ULong*) senderTaskId = (ULong) self->fRegister[kcR1];
	return result;
}


// SWI 20
extern "C" long
SMemMsgSetUserRefConSWI(ULong msgId, ULong refCon)
{
	TTask* self = Enter();
	if (self == nil)
		return kError_Call_Aborted;
	return ExitWithResult(self, SMemMsgSetUserRefConKernelGlue(msgId, (void*) (uintptr_t) refCon));
}


// SWI 21: the ref con in r1
extern "C" long
SMemMsgGetUserRefConSWI(ULong msgId, ULong* refConPtr)
{
	TTask* self = Enter();
	if (self == nil)
		return kError_Call_Aborted;
	long result = ExitWithResult(self, SMemMsgGetUserRefConKernelGlue(msgId));
	if (result == noErr && refConPtr != nil)
		*refConPtr = (ULong) self->fRegister[kcR1];
	return result;
}


// SWI 22: like a receive, the completed message's details in r1-r4
extern "C" long
SMemMsgCheckForDoneSWI(ULong msgId, ULong flags, ULong* sentById, ULong* replyMemId, ULong* msgType, ULong* signature)
{
	TTask* self = Enter();
	if (self == nil)
		return kError_Call_Aborted;
	SMemMsgCheckForDoneKernelGlue(msgId, flags);
	long result = ExitWithSavedResult(self);
	if (result == noErr)
	{
		if (sentById != nil)	*sentById = (ULong) self->fRegister[kcR1];
		if (replyMemId != nil)	*replyMemId = (ULong) self->fRegister[kcR2];
		if (msgType != nil)		*msgType = (ULong) self->fRegister[kcR3];
		if (signature != nil)	*signature = (ULong) self->fRegister[kcR4];
	}
	return result;
}


// SWI 23
extern "C" long
SMemMsgMsgDoneSWI(ULong msgId, long result, ULong signature)
{
	TTask* self = Enter();
	if (self == nil)
		return kError_Call_Aborted;
	return ExitWithResult(self, SMemMsgMsgDoneKernelGlue(msgId, result, signature));
}


/* -------------------------------------------------------------------------------
	Monitors
------------------------------------------------------------------------------- */

// SWI 27 (0x0038abf8): every register is saved; the glue reads the monitor
// id, selector and user object from r0-r2
extern "C" long
MonitorDispatchSWI(ULong monitorId, long selector, void* userObject)
{
	TTask* self = Enter();
	if (self == nil)
		return kError_Call_Aborted;
	self->fRegister[kcR0] = monitorId;
	self->fRegister[kcR1] = (TRegister) selector;
	self->fRegister[kcR2] = (TRegister) userObject;
	return ExitWithResult(self, MonitorDispatchKernelGlue());
}


// SWI 28: the monitor task is done; it never returns here - when the
// monitor is next entered it is resumed at MonitorEntryGlue (a redirect)
extern "C" long
MonitorExitSWI(long monitorResult, void* /*continuationPC*/)
{
	TTask* self = Enter();
	if (self == nil)
		return kError_Call_Aborted;
	return ExitWithResult(self, MonitorExitKernelGlue(monitorResult));
}


// SWI 29
extern "C" long
MonitorThrowSWI(char* name, void* data, void* destructor)
{
	TTask* self = Enter();
	if (self == nil)
		return kError_Call_Aborted;
	return ExitWithResult(self, MonitorThrowKernelGlue(name, data, (void (*)(void*)) destructor));
}


// SWI 32
extern "C" long
MonitorFlushSWI(ULong monitorId)
{
	TTask* self = Enter();
	if (self == nil)
		return kError_Call_Aborted;
	return ExitWithResult(self, MonitorFlushKernelGlue(monitorId));
}


/* -------------------------------------------------------------------------------
	Semaphores
------------------------------------------------------------------------------- */

// SWI 11 (0x003a4b7c SemaphoreOpGlue).  A blocked op is retried when the task
// is woken: the kernel resumes it at the SWI itself, or one word further on
// to fail instead (TSemaphore's destructor) - here markers kResumeInStub and
// kResumeInStub + 4.
extern "C" long
SemaphoreOpGlue(ULong groupId, ULong listId, ULong flags)
{
	for (;;)
	{
		TTask* self = Enter();
		if (self == nil)
			return kError_Call_Aborted;
		long result = DoSemaphoreOp(groupId, listId, (SemFlags) flags, self);
		if (!HostSWIExit(self, kResumeInStub))
			return result;
		if (self->fRegister[kcPC] == kResumeInStub + 4)
			return (long) self->fRegister[kcR0];
	}
}


/* -------------------------------------------------------------------------------
	GenericSWI
------------------------------------------------------------------------------- */

// GenericSWI itself is in GenericSWIStub.cpp (variadic).

// SWI 5 (0x003a4b40 GenericWithReturnSWI): r1-r3 come back through the pointers
extern "C" long
GenericWithReturnSWI(ULong selector, ULong p1, ULong p2, ULong p3, ULong* rp1, ULong* rp2, ULong* rp3)
{
	TTask* self = Enter();
	if (self == nil)
		return kError_Call_Aborted;
	long result = ExitWithResult(self, GenericSWIHandler(selector, p1, p2, p3, 0));
	if (rp1 != nil)	*rp1 = (ULong) self->fRegister[kcR1];
	if (rp2 != nil)	*rp2 = (ULong) self->fRegister[kcR2];
	if (rp3 != nil)	*rp3 = (ULong) self->fRegister[kcR3];
	return result;
}
