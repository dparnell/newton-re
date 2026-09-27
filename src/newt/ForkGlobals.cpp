/*
	File:		newt/ForkGlobals.cpp

	Contains:	A fork of the newt world's globals (NewtGlobals, NewtWorld.h):
				each fork has an interpreter of its own - a new id, its
				stack positions counted from id << 16 so that what a fork
				leaves on the ref stack is told apart - and a QuickDraw port
				and temporary buffer of its own.  TNewtWorld::ForkConstructor
				makes them in the new task and ForkDestructor gives them
				back; ForkSwitch makes the running fork's the current ones.

				The ROM keeps the frames half with the interpreter
				(0x002f6bb4) and the QuickDraw half with the ports
				(0x002e46ec); they are here because the host's frames and
				qd libraries are below the newt world whose globals they
				fill in.  NOT YET RECONSTRUCTED: the task's stack limits
				(GetTaskStackInfo into fStackBase), which the main
				interpreter's InitInterpreter leaves out too.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "NewtWorld.h"
#include "Interpreter.h"
#include "Ports.h"
#include "NewtonMemory.h"


// ROM 0x002f6bb4 InitForkGlobalsForFrames__FP11NewtGlobals
// The new fork's interpreter (in gNewtGlobals, which ForkConstructor has
// made the fork's), numbered with the first id above the parent's that no
// interpreter has.
long
InitForkGlobalsForFrames(NewtGlobals* parent)
{
	NewtGlobals* globals = gNewtGlobals;
	globals->fInterpreter = new TInterpreter;
	if (globals->fInterpreter == nil)
		return MemError();
	// (the ROM's new interpreter is already on the list with whatever id
	// its block held; the host's starts at nought, which no search matches)
	globals->fInterpreter->fID = 0;
	long id = (long) (parent->fStackPos >> 16);
	do
		id++;
	while (GetTInterpreter(id) != nil);
	globals->fInterpreter->fID = id;
	globals->fStackPos = (ULong) id << 16;
	return noErr;
}


// ROM 0x002f6cc0 DestroyForkGlobalsForFrames__FP11NewtGlobals
void
DestroyForkGlobalsForFrames(NewtGlobals* globals)
{
	if (globals->fInterpreter != nil)
		delete globals->fInterpreter;
}


// ROM 0x002e46ec InitForkGlobalsForQD__FP11NewtGlobals
// The fork's own port, opened on the screen, and its temporary buffer.
long
InitForkGlobalsForQD(NewtGlobals* /*parent*/)
{
	NewtGlobals* globals = GetNewtGlobals();
	globals->fPort = (GrafPort*) NewPtr(sizeof(GrafPort));
	if (globals->fPort != nil)
	{
		OpenPort(globals->fPort);
		globals->fTempBuf = AllocNewTempBuf();
		if (globals->fTempBuf != nil)
		{
			globals->fTempBuf2 = globals->fTempBuf;
			return noErr;
		}
		ClosePort(globals->fPort);
		DisposPtr((Ptr) globals->fPort);
	}
	return MemError();
}


// ROM 0x0033f658 InvalidateQDTempBuf__Fv
// The main world's globals: no temporary buffer of its own, marked with
// -0x400 so that DeleteNewTempBuf leaves it be when the main world ends
// as a fork.
void
InvalidateQDTempBuf(void)
{
	GetNewtGlobals()->fTempBuf = (void*) -0x400;
	GetNewtGlobals()->fTempBuf2 = nil;
}


// ROM 0x002e474c DestroyForkGlobalsForQD__FP11NewtGlobals
// (the port closed unless it is the screen's own)
void
DestroyForkGlobalsForQD(NewtGlobals* globals)
{
	if (globals->fPort != nil && globals->fPort != &gGrafPort)
	{
		ClosePort(globals->fPort);
		DisposPtr((Ptr) globals->fPort);
	}
	DeleteNewTempBuf((char*) globals->fTempBuf);
}
