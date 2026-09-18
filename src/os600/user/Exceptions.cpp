/*
	File:		user/Exceptions.cpp

	Contains:	The Newton exception system (NewtonExceptions.h): a chain of
				CatchHeaders - try handlers (setjmp buffers), cleanup handlers
				(unwind_protect) and boundary markers - hanging off the current
				task's globals (gFirstCatch, the word below GetGlobals()) in user
				mode, or off a global per interrupt mode.  Throw walks the chain,
				running cleanups, and longjmps into the first try handler; with
				none left, a task inside a monitor hands the exception to its
				caller (MonitorThrowSWI), otherwise the machine reboots.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Newton.h"
#include "NewtonExceptions.h"
#include "UserGlobals.h"
#include "KernelGlobals.h"
#include "Reboot.h"
#include "OSErrors.h"
#include "hal/Atomic.h"
#include "hal/System.h"

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

// the handler chains of the two interrupt modes (unnamed in the symbol table)
static CatchHeader*	gFIQFirstCatch = nil;		// 0x0c100d1c
static CatchHeader*	gIRQFirstCatch = nil;		// 0x0c100d20

// the CPU mode's low four bits: 0 user, 1 FIQ, 2 IRQ, 3 supervisor
enum { kMode_User = 0, kMode_FIQ = 1, kMode_IRQ = 2 };

static const char kMsgExceptionName[] = "evt.ex.msg";


// ROM 0x000b00bc InitializeExceptionGlobals
void
InitializeExceptionGlobals(ExceptionGlobals* globals)
{
	globals->firstCatch = nil;
}


// ROM 0x000b03fc GetExceptionHandler__Fv
// The head of the handler chain for the mode the CPU is in.
CatchHeader*
GetExceptionHandler()
{
	switch (GetCPUMode() & 0xf)
	{
	case kMode_User:	return ((ExceptionGlobals*) ((char*) GetGlobals() - sizeof(ExceptionGlobals)))->firstCatch;
	case kMode_FIQ:		return gFIQFirstCatch;
	case kMode_IRQ:		return gIRQFirstCatch;
	default:			return nil;
	}
}


// ROM 0x000b0010 SetExceptionHandler__FP11CatchHeader
void
SetExceptionHandler(CatchHeader* handler)
{
	switch (GetCPUMode() & 0xf)
	{
	case kMode_User:	((ExceptionGlobals*) ((char*) GetGlobals() - sizeof(ExceptionGlobals)))->firstCatch = handler; break;
	case kMode_FIQ:		gFIQFirstCatch = handler; break;
	case kMode_IRQ:		gIRQFirstCatch = handler; break;
	}
}


// ROM 0x000b01a8 AddExceptionHandler
void
AddExceptionHandler(CatchHeader* i)
{
	i->next = GetExceptionHandler();
	SetExceptionHandler(i);
}


// ROM 0x000b01cc RemoveExceptionHandler
void
RemoveExceptionHandler(CatchHeader* i)
{
	SetExceptionHandler(i->next);
}


// ROM 0x000aff9c Subexception
// Exception names are dotted paths; a name is a sub-exception of another if
// the other is a prefix of it.  A name may list several alternatives
// separated by ';'.
int
Subexception(ExceptionName sub, ExceptionName super)
{
	for (;;)
	{
		char* alternative = strchr(sub, ';');
		if (strncmp(super, sub, strlen(super)) == 0)
			return 1;
		if (alternative == nil)
			return 0;
		sub = alternative + 1;
	}
}


// ROM 0x002f5610 ForgetDeveloperNotified__FPc
static void
ForgetDeveloperNotified(ExceptionName /*name*/)
{
	// NOT YET RECONSTRUCTED: drops the exception from gDeveloperNotified, the
	// list of exceptions the debugger was told about (the developer
	// notification is part of the debugger support)
}


// The end of the chain: nobody catches the exception.  In a monitor the
// caller inherits it; otherwise the exception's data is destroyed and the
// machine warm-reboots with kError_Sorry_System_Error.
static void
Unhandled(ExceptionName name, void* data, ExceptionDestructor destructor)
{
	ULong mode = GetCPUMode() & 0xf;
	if (mode == kMode_User)
	{
		if (gCurrentMonitorId != 0)
			MonitorThrowSWI(name, data, (void*) destructor);
	}
	else if (mode != kMode_FIQ && mode != kMode_IRQ)
		return;
	char message[0x100];
	if (Subexception(name, (ExceptionName) kMsgExceptionName))
		snprintf(message, sizeof(message), "Unhandled exception: %s--warm reboot!", (const char*) data);
	else
		snprintf(message, sizeof(message), "Unhandled exception %s, warm reboot!", name);
	fprintf(stderr, "%s\n", message);
	ForgetDeveloperNotified(name);
	if (data != nil && destructor != nil)
		destructor(data);
	EnterFIQAtomic();
	Reboot(kError_Sorry_System_Error, 0, false);
}


// The walk Throw, ThrowMsg and NextHandler share: cleanups run (and are
// marked done, kExceptionCleanupDone) on the way to the first try handler,
// which is unlinked, told the exception and jumped into.  A boundary marker
// ends the walk.
static void
Dispatch(ExceptionName name, void* data, ExceptionDestructor destructor)
{
	CatchHeader* i = GetExceptionHandler();
	while (i != nil)
	{
		if (i->catchType == kExceptionHandler)
		{
			SetExceptionHandler(i->next);
			ExceptionHandler* handler = (ExceptionHandler*) i;
			handler->exception.destructor = destructor;
			handler->exception.data = data;
			handler->exception.name = name;
			longjmp(handler->state, 1);
		}
		if (i->catchType != kExceptionCleanup)
			break;
		i->catchType = kExceptionCleanupDone;
		ExceptionCleanup* protect = (ExceptionCleanup*) i;		// (`cleanup` is a macro of NewtonExceptions.h)
		protect->function(protect->object);
		CatchHeader* next = GetExceptionHandler();
		if (next == i)
			next = i->next;
		i = next;
	}
	Unhandled(name, data, destructor);
}


// ROM 0x000b00c8 Throw
void
Throw(ExceptionName name, void* data, ExceptionDestructor destructor)
{
	Dispatch(name, data, destructor);
}


// ROM 0x000afff8 ThrowMsg
void
ThrowMsg(char* msg)
{
	Dispatch((ExceptionName) kMsgExceptionName, msg, nil);
}


// ROM 0x000b03f0 NextHandler
// rethrow: the exception a handler caught goes on to the next one.
void
NextHandler(ExceptionHandler* i)
{
	Dispatch(i->exception.name, i->exception.data, i->exception.destructor);
}


// ROM 0x000b0058 ExitHandler
// end_try.  If the handler is no longer the innermost try handler it caught
// an exception, which is now over: its data is destroyed.  Otherwise the
// try block ended normally: the handler leaves the chain (searched for if
// cleanups have been pushed in front of it).
void
ExitHandler(ExceptionHandler* i)
{
	CatchHeader* first = GetExceptionHandler();
	while (first != nil && first->catchType == kExceptionCleanup)
		first = first->next;
	if ((CatchHeader*) i != first)
	{
		ForgetDeveloperNotified(i->exception.name);
		if (i->exception.data != nil && i->exception.destructor != nil)
			i->exception.destructor(i->exception.data);
		return;
	}
	CatchHeader* head = GetExceptionHandler();
	if (head != (CatchHeader*) i)
	{
		for (CatchHeader* prev = head; prev != nil; prev = prev->next)
		{
			if (prev->next == (CatchHeader*) i)
			{
				prev->next = i->header.next;
				return;
			}
		}
		return;
	}
	SetExceptionHandler(i->header.next);
}
