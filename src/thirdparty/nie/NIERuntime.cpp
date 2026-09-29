/*
	File:		thirdparty/nie/NIERuntime.cpp

	Contains:	The NIE's native-code runtime routines (NIERuntime.h).
*/

#include "NIERuntime.h"
#include "Interpreter.h"
#include "ObjectHeap.h"
#include "NSErrors.h"
#include "Frames.h"
#include "NewtonExceptions.h"

Ref FAref(RefArg rcvr, RefArg obj, RefArg index);		// frames/Builtins.cpp
Ref FSubtract(RefArg rcvr, RefArg a, RefArg b);		// frames/Builtins.cpp
Ref FEqual(RefArg rcvr, RefArg a, RefArg b);			// frames/Builtins.cpp
Ref FGreaterThan(RefArg rcvr, RefArg a, RefArg b);	// frames/Builtins.cpp
Ref FUnorderedLessOrGreater(RefArg rcvr, RefArg a, RefArg b);
Ref FAdd(RefArg rcvr, RefArg a, RefArg b);
Ref FLessThan(RefArg rcvr, RefArg a, RefArg b);
void IncrementCurrentStackPos(void);				// frames/ObjectHeap.cpp
void DecrementCurrentStackPos(void);
void ClearRefHandles(void);


Ref
NIELiteral(RefArg closure, long i)
{
	return GetArraySlotRef(RefVar(GetArraySlotRef(closure, 3)), i);
}


// (inline in every function: GetGInterpreter, IsSend, Clone, GetReceiver,
// GetImplementor)
Ref
NIEEnvironment(RefArg closure)
{
	TInterpreter* interpreter = GetGInterpreter();
	RefVar env(Clone(closure));
	if (interpreter->IsSend())
	{
		SetArraySlotRef(env, 1, interpreter->GetReceiver());
		SetArraySlotRef(env, 2, interpreter->GetImplementor());
	}
	return env;
}


Ref
NIESelf(RefArg closure)
{
	TInterpreter* interpreter = GetGInterpreter();
	if (interpreter->IsSend())
		return interpreter->GetReceiver();
	return GetArraySlotRef(closure, 1);
}


// NIE inetenbl.pkg part 1 +0x171c (find variable)
// GetVariable with the locals looked in; then a slot of the global
// variables frame (magic pointer 1.1); then the error.
Ref
NIEFindVariable(RefArg env, RefArg symbol)
{
	long exists = 0;
	RefVar value(GetVariable(env, symbol, &exists, 1));
	if (exists != 0)
		return value;
	if (FrameHasSlotRef(gVarFrame, symbol))
		return GetFrameSlotRef(gVarFrame, symbol);
	ThrowExInterpreterWithSymbol(kNSErrUndefinedVariable, symbol);
	return NILREF;
}


// NIE inetenbl.pkg part 1 +0x1354 (global function)
// The function frame's slot (GetGFunctionFrame), nil being an error.
Ref
NIEGlobalFunction(RefArg symbol)
{
	RefVar fn(GetFrameSlotRef(RefVar(GetGFunctionFrame()), symbol));
	if (ISNIL(fn))
		ThrowExInterpreterWithSymbol(kNSErrUndefinedGlobalFunction, symbol);
	return fn;
}


// NIE inetenbl.pkg part 1 +0x1cdc (aref)
Ref
NIEAref(RefArg obj, RefArg index)
{
	return FAref(RefVar(), obj, index);
}


// NIE inetenbl.pkg part 1 +0x1c68 (subtract)
// Two integers are subtracted as Refs (the tag bits cancel), wrapping as
// the ARM does; anything else goes to FSubtract.
Ref
NIESubtract(RefArg a, RefArg b)
{
	if (ISINT(a) && ISINT(b))
		return MAKEINT((long) ((ULong) RINT(a) - (ULong) RINT(b)));
	return FSubtract(RefVar(), a, b);
}


// NIE inetenbl.pkg part 1 +0x1ba0 (=)
// Two integers compare as words; anything else (either not an integer)
// through FEqual.
bool
NIEEqual(RefArg a, RefArg b)
{
	if (ISINT(a) && ISINT(b))
		return (Ref) a == (Ref) b;
	return NOTNIL(FEqual(RefVar(), a, b));
}


// NIE inetenbl.pkg part 1 +0x1af0 (>)
bool
NIEGreaterThan(RefArg a, RefArg b)
{
	if (ISINT(a) && ISINT(b))
		return RINT(a) > RINT(b);
	return NOTNIL(FGreaterThan(RefVar(), a, b));
}


// NIE inetenbl.pkg part 1 +0x1794 (set variable)
void
NIESetVariable(RefArg env, RefArg symbol, RefArg value)
{
	SetVariableOrGlobal(env, symbol, value, 1);
}


// (inline in each function that has a try: GetStackStateBlock,
// IncrementCurrentStackPos, setjmp, AddExceptionHandler; on a throw
// DecrementCurrentStackPos, ClearRefHandles, ResetStackStateBlock, and
// Subexception against the handler's name, which the code keeps inline)
void
NIETryEvtEx(void (*body)(void*), void (*handler)(void*, Exception*), void* data)
{
	StackState* state = GetStackStateBlock();
	IncrementCurrentStackPos();
	newton_try
	{
		body(data);
		DecrementCurrentStackPos();
		DisposeStackStateBlock(state);
	}
	newton_catch_all
	{
		DecrementCurrentStackPos();
		ClearRefHandles();
		ResetStack(*state);
		DisposeStackStateBlock(state);
		if (!Subexception(_info.exception.name, "evt.ex"))
			rethrow;
		handler(data, &_info.exception);
	}
	end_try;
}


bool
NIETryEvtEx(void (*body)(void*), void* data)
{
	bool caught = false;
	StackState* state = GetStackStateBlock();
	IncrementCurrentStackPos();
	newton_try
	{
		body(data);
		DecrementCurrentStackPos();
		DisposeStackStateBlock(state);
	}
	newton_catch_all
	{
		DecrementCurrentStackPos();
		ClearRefHandles();
		ResetStack(*state);
		DisposeStackStateBlock(state);
		if (!Subexception(_info.exception.name, "evt.ex"))
			rethrow;
		caught = true;
	}
	end_try;
	return caught;
}


// NIE inetenbl.pkg part 1 +0x1bf8 (<>)
bool
NIENotEqual(RefArg a, RefArg b)
{
	if (ISINT(a) && ISINT(b))
		return (Ref) a != (Ref) b;
	return NOTNIL(FUnorderedLessOrGreater(RefVar(), a, b));
}


// NIE inetenbl.pkg part 1 +0x1c24 (+)
Ref
NIEAdd(RefArg a, RefArg b)
{
	if (ISINT(a) && ISINT(b))
		return MAKEINT((long) ((ULong) RINT(a) + (ULong) RINT(b)));
	return FAdd(RefVar(), a, b);
}


// NIE inetenbl.pkg part 1 +0x1a2c (<)
bool
NIELessThan(RefArg a, RefArg b)
{
	if (ISINT(a) && ISINT(b))
		return RINT(a) < RINT(b);
	return NOTNIL(FLessThan(RefVar(), a, b));
}
