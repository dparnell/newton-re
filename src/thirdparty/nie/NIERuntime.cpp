/*
	File:		thirdparty/nie/NIERuntime.cpp

	Contains:	The NIE's native-code runtime routines (NIERuntime.h).
*/

#include "NIERuntime.h"
#include "Interpreter.h"
#include "ObjectHeap.h"
#include "NSErrors.h"
#include "Frames.h"

Ref FAref(RefArg rcvr, RefArg obj, RefArg index);		// frames/Builtins.cpp


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
