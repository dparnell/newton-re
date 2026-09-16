/*
	File:		frames/DebugAPI.h

	Contains:	TNSDebugAPI, the debugger's view of an interpreter's call
				stack: each stack frame's function, program counter,
				receiver, implementor and variables (its arguments and
				locals on the value stack or in its argFrame), and the
				temporaries above them; and what is built on it - the REP's
				stack trace (REPStackTrace), the NTK's frame info
				(NTKStackFrameInfo), the names of well-known objects
				(SearchForObjectName) and the natives StackTrace,
				BreakLoop, ExitBreakLoop, SetDebugMode, Write, Load, stats.

	The ROM's TNSDebugAPI is one word (the interpreter); frame indexes
	count from the bottom of the stack (0 the outermost call).
*/

#ifndef __DEBUGAPI_H
#define __DEBUGAPI_H

#include "Interpreter.h"

class TNSDebugAPI
{
public:
				TNSDebugAPI(TInterpreter* interpreter);
				~TNSDebugAPI();

	Boolean		AccurateStack(void);
	long		NumStackFrames(void);
	VMState*	StackFrameAt(long index);

	Ref			Function(long index);
	void		SetFunction(long index, RefArg fn);
	long		PC(long index);						// -1 in a native call
	void		SetPC(long index, long pc);
	Ref			Receiver(long index);
	void		SetReceiver(long index, RefArg receiver);
	Ref			Implementor(long index);
	void		SetImplementor(long index, RefArg implementor);

	Ref			Locals(long index);					// an array of the frame's variables (args first)
	Ref			GetVar(long index, long varIndex);
	void		SetVar(long index, long varIndex, RefArg value);
	Ref			FindVar(long index, RefArg name);	// through the frame's argFrame
	void		SetFindVar(long index, RefArg name, RefArg value);

	long		StackStart(long index);				// the value-stack index of the frame's first variable
	long		NumTemps(long index);
	Ref			TempValue(long index, long tempIndex);
	void		SetTempValue(long index, long tempIndex, RefArg value);
	void		Return(long index, RefArg value);	// NOT YET RECONSTRUCTED

	TInterpreter*	fInterpreter;		// +0x00
};

TNSDebugAPI*	NewNSDebugAPI(TInterpreter* interpreter);
void			DeleteNSDebugAPI(TNSDebugAPI* api);

long	FunctionStackSize(RefArg fn);			// the variables a call keeps on the value stack (-1: in an argFrame)
Ref		FunctionDebugName(RefArg fn);			// its 'DebuggerInfo or 'debug name
Ref		GetNameFromDebugHash(RefArg hash);
Ref		CheckForObjectName(RefArg context, const char* contextName, RefArg obj);
Ref		SearchForObjectName(RefArg obj);		// "vars", "vars.foo", "functions.bar" ...
Ref		NTKStackFrameInfo(TNSDebugAPI& api, long index);
void	NTKStackTrace(void* interpreter);		// NOT YET RECONSTRUCTED
void	REPBreakLoop(void);
void	BreakLoop(void);

#endif	/* __DEBUGAPI_H */
