/*
	File:		frames/DebugAPI.cpp

	Contains:	TNSDebugAPI (DebugAPI.h), the REP's stack trace and the
				debugging natives.

	A call's VMState on the control stack (Interpreter.h) says where its
	variables are: a 2.x function keeps its arguments and locals on the
	value stack from the frame's base (fStackFrame) + 3 (three hidden
	slots), a native function's arguments start at the base itself, and a
	1.x CodeBlock (or a closure with an argFrame) keeps them in fLocals,
	the argFrame, after its three _nextArgFrame/_parent/_implementor slots.
*/

#include "DebugAPI.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "REPTranslators.h"
#include "Compiler.h"
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "NSErrors.h"
#include "NewtonExceptions.h"

#include <string.h>


/* -------------------------------------------------------------------------------
	TNSDebugAPI
------------------------------------------------------------------------------- */

// ROM 0x002d42e4 __ct__11TNSDebugAPIFP12TInterpreter
TNSDebugAPI::TNSDebugAPI(TInterpreter* interpreter)
{
	fInterpreter = interpreter;
}


// ROM 0x002d4314 __dt__11TNSDebugAPIFv
TNSDebugAPI::~TNSDebugAPI()
{ }


// ROM 0x002d31b4 NewNSDebugAPI__FP12TInterpreter
TNSDebugAPI*
NewNSDebugAPI(TInterpreter* interpreter)
{
	return new TNSDebugAPI(interpreter);
}


// ROM 0x002d31c0 DeleteNSDebugAPI__FP11TNSDebugAPI
void
DeleteNSDebugAPI(TNSDebugAPI* api)
{
	delete api;
}


// ROM 0x002d4320 AccurateStack__11TNSDebugAPIFv
// The stack is exact when the interpreter runs its slow loop (SetDebugMode).
Boolean
TNSDebugAPI::AccurateStack(void)
{
	return gAccurateStackTrace != 0;
}


// ROM 0x002d4338 NumStackFrames__11TNSDebugAPIFv
// The calls on the control stack (the six-ref states, the topmost one
// being the running call's).
long
TNSDebugAPI::NumStackFrames(void)
{
	return (fInterpreter->fCtrlStack.Depth() - 1) / kVMStateSize;
}


// ROM 0x002d2464 StackFrameAt__11TNSDebugAPIFl
// The state of call index (0 the outermost).
VMState*
TNSDebugAPI::StackFrameAt(long index)
{
	if (index < 0 || index >= NumStackFrames())
		ThrowExFramesWithBadValue(kNSErrOutOfRange, RefVar(MAKEINT(index)));
	return fInterpreter->fCtrlStack.StateAt(index + 1);
}


// ROM 0x002d24c8 Function__11TNSDebugAPIFl
Ref
TNSDebugAPI::Function(long index)
{
	return StateRef(StackFrameAt(index)->fFunction);
}


// ROM 0x002d24e4 SetFunction__11TNSDebugAPIFlRC6RefVar
void
TNSDebugAPI::SetFunction(long index, RefArg fn)
{
	StateRef(StackFrameAt(index)->fFunction) = fn;
}


// ROM 0x002d250c PC__11TNSDebugAPIFl
long
TNSDebugAPI::PC(long index)
{
	Ref pc = StateRef(StackFrameAt(index)->fPC);
	return ISINT(pc) ? RINT(pc) : -1;
}


// ROM 0x002d2534 SetPC__11TNSDebugAPIFlT1
void
TNSDebugAPI::SetPC(long index, long pc)
{
	StateRef(StackFrameAt(index)->fPC) = MAKEINT(pc);
}


// ROM 0x002d2558 Receiver__11TNSDebugAPIFl
Ref
TNSDebugAPI::Receiver(long index)
{
	return StateRef(StackFrameAt(index)->fReceiver);
}


// ROM 0x002d2574 SetReceiver__11TNSDebugAPIFlRC6RefVar
void
TNSDebugAPI::SetReceiver(long index, RefArg receiver)
{
	StateRef(StackFrameAt(index)->fReceiver) = receiver;
}


// ROM 0x002d259c Implementor__11TNSDebugAPIFl
Ref
TNSDebugAPI::Implementor(long index)
{
	return StateRef(StackFrameAt(index)->fImplementor);
}


// ROM 0x002d25b8 SetImplementor__11TNSDebugAPIFlRC6RefVar
void
TNSDebugAPI::SetImplementor(long index, RefArg implementor)
{
	StateRef(StackFrameAt(index)->fImplementor) = implementor;
}


// the value-stack base of a state's frame (fStackFrame is MAKEINT(base << 6 | flags))
static inline long
FrameBase(VMState* state)
{
	return (long) ((ULong) StateRef(state->fStackFrame) >> 8);
}


// the number of variables of a 2.x function: numArgs + numLocals
static inline long
FunctionNumVars(RefArg fn)
{
	long numArgs = RINT(GetArraySlotRef(fn, kFunctionNumArgsSlot));
	return (numArgs & 0xffff) + (numArgs >> 16);
}


// ROM 0x002d25e0 Locals__11TNSDebugAPIFl
// An array of the call's variables, arguments first.
Ref
TNSDebugAPI::Locals(long index)
{
	VMState* state = StackFrameAt(index);
	RefVar fn(StateRef(state->fFunction));
	RefVar theClass(ClassOf(fn));
	long base = FrameBase(state);
	Ref* values = fInterpreter->fValueStack.fBase;
	RefVar locals;
	long count;
	if (EQRef(theClass, RSSYM_function))
	{
		count = FunctionNumVars(fn);
		locals = AllocateArray(RSSYMarray, count);
		for (long i = 0; i < count; i++)
			SetArraySlotRef(locals, i, values[base + 3 + i]);
	}
	else if (IsNativeFunction(fn))
	{
		count = GetFunctionArgCount(fn);
		locals = AllocateArray(RSSYMarray, count);
		for (long i = 0; i < count; i++)
			SetArraySlotRef(locals, i, values[base + i]);
	}
	else
	{
		// a CodeBlock (or anything else): the argFrame after its three hidden slots
		RefVar argFrame(StateRef(state->fLocals));
		count = Length(argFrame) - 3;
		locals = AllocateArray(RSSYMarray, count);
		for (long i = 0; i < count; i++)
			SetArraySlotRef(locals, i, GetArraySlotRef(argFrame, i + 3));
	}
	return locals;
}


// ROM 0x002d290c GetVar__11TNSDebugAPIFlT1
// The call's variable varIndex (arguments first).
Ref
TNSDebugAPI::GetVar(long index, long varIndex)
{
	VMState* state = StackFrameAt(index);
	RefVar fn(StateRef(state->fFunction));
	RefVar theClass(ClassOf(fn));
	long base = FrameBase(state);
	Ref* values = fInterpreter->fValueStack.fBase;
	if (EQRef(theClass, RSSYM_function))
	{
		if (varIndex < 0 || varIndex >= FunctionNumVars(fn))
			ThrowExFramesWithBadValue(kNSErrOutOfRange, RefVar(MAKEINT(varIndex)));
		return values[base + 3 + varIndex];
	}
	if (IsNativeFunction(fn))
	{
		if (varIndex < 0 || varIndex >= GetFunctionArgCount(fn))
			ThrowExFramesWithBadValue(kNSErrOutOfRange, RefVar(MAKEINT(varIndex)));
		return values[base + varIndex];
	}
	RefVar argFrame(StateRef(state->fLocals));
	if (varIndex < 0 || varIndex >= Length(argFrame) - 3)
		ThrowExFramesWithBadValue(kNSErrOutOfRange, RefVar(MAKEINT(varIndex)));
	return GetArraySlotRef(argFrame, varIndex + 3);
}


// ROM 0x002d2af4 SetVar__11TNSDebugAPIFlT1RC6RefVar
void
TNSDebugAPI::SetVar(long index, long varIndex, RefArg value)
{
	VMState* state = StackFrameAt(index);
	RefVar fn(StateRef(state->fFunction));
	RefVar theClass(ClassOf(fn));
	long base = FrameBase(state);
	Ref* values = fInterpreter->fValueStack.fBase;
	if (EQRef(theClass, RSSYM_function))
	{
		if (varIndex < 0 || varIndex >= FunctionNumVars(fn))
			ThrowExFramesWithBadValue(kNSErrOutOfRange, RefVar(MAKEINT(varIndex)));
		values[base + 3 + varIndex] = value;
	}
	else if (IsNativeFunction(fn))
	{
		if (varIndex < 0 || varIndex >= GetFunctionArgCount(fn))
			ThrowExFramesWithBadValue(kNSErrOutOfRange, RefVar(MAKEINT(varIndex)));
		values[base + varIndex] = value;
	}
	else
	{
		RefVar argFrame(StateRef(state->fLocals));
		if (varIndex < 0 || varIndex >= Length(argFrame) - 3)
			ThrowExFramesWithBadValue(kNSErrOutOfRange, RefVar(MAKEINT(varIndex)));
		SetArraySlotRef(argFrame, varIndex + 3, value);
	}
}


// ROM 0x002d2ce8 FindVar__11TNSDebugAPIFlRC6RefVar
// A variable by name through the call's argFrame (and its parents).
Ref
TNSDebugAPI::FindVar(long index, RefArg name)
{
	VMState* state = StackFrameAt(index);
	if (StateRef(state->fLocals) == NILREF)
		ThrowExInterpreterWithSymbol(kNSErrUndefinedVariable, name);
	long exists;
	RefVar value(XGetVariable(StateVar(state->fLocals), name, &exists, 1));
	if (!exists)
		ThrowExInterpreterWithSymbol(kNSErrUndefinedVariable, name);
	return value;
}


// ROM 0x002d2d70 SetFindVar__11TNSDebugAPIFlRC6RefVarT2
void
TNSDebugAPI::SetFindVar(long index, RefArg name, RefArg value)
{
	VMState* state = StackFrameAt(index);
	if (StateRef(state->fLocals) == NILREF || !SetVariableOrGlobal(StateVar(state->fLocals), name, value, 1))
		ThrowExInterpreterWithSymbol(kNSErrUndefinedVariable, name);
}


// ROM 0x002d2dec FunctionStackSize__FRC6RefVar
// The value-stack slots a call of fn keeps its variables in: a 2.x
// function's arguments and locals, a native's arguments; -1 for a
// CodeBlock (they are in its argFrame).
long
FunctionStackSize(RefArg fn)
{
	RefVar theClass(ClassOf(fn));
	if (EQRef(theClass, RSSYM_function))
		return FunctionNumVars(fn);
	if (IsNativeFunction(fn))
		return GetFunctionArgCount(fn);
	return -1;
}


// ROM 0x002d2ec4 StackStart__11TNSDebugAPIFl
// The value-stack index of the call's first variable; for the index past
// the last call, the top of the stack.
long
TNSDebugAPI::StackStart(long index)
{
	if (index == NumStackFrames())
		return fInterpreter->fValueStack.Depth() - 1;
	return FrameBase(StackFrameAt(index)) + 3;
}


// ROM 0x002d2f28 NumTemps__11TNSDebugAPIFl
// The value-stack slots above the call's variables and below the next call.
long
TNSDebugAPI::NumTemps(long index)
{
	long next = StackStart(index + 1);
	long start = StackStart(index);
	RefVar fn(Function(index));
	return next - (start + FunctionStackSize(fn));
}


// ROM 0x002d2f94 TempValue__11TNSDebugAPIFlT1
Ref
TNSDebugAPI::TempValue(long index, long tempIndex)
{
	long start = StackStart(index);
	RefVar fn(Function(index));
	long position = start + FunctionStackSize(fn) + tempIndex;
	if (tempIndex < 0 || position >= StackStart(index + 1))
		ThrowExFramesWithBadValue(kNSErrOutOfRange, RefVar(MAKEINT(tempIndex)));
	return fInterpreter->fValueStack.fBase[position];
}


// ROM 0x002d3038 SetTempValue__11TNSDebugAPIFlT1RC6RefVar
void
TNSDebugAPI::SetTempValue(long index, long tempIndex, RefArg value)
{
	long start = StackStart(index);
	RefVar fn(Function(index));
	long position = start + FunctionStackSize(fn) + tempIndex;
	if (tempIndex < 0 || position >= StackStart(index + 1))
		ThrowExFramesWithBadValue(kNSErrOutOfRange, RefVar(MAKEINT(tempIndex)));
	fInterpreter->fValueStack.fBase[position] = value;
}


// ROM 0x002d30e8 Return__11TNSDebugAPIFlRC6RefVar
// NOT YET RECONSTRUCTED: unwinding the interpreter to call index with a
// value (the ROM throws exFrames kNSErrBadArgs... through the handlers).
void
TNSDebugAPI::Return(long /*index*/, RefArg /*value*/)
{
	Throw(exFrames, (void*) kNSErrNativeNotReconstructed, nil);
}


/* -------------------------------------------------------------------------------
	Names
------------------------------------------------------------------------------- */

// ROM 0x001e9a1c GetNameFromDebugHash__FRC6RefVar
// The name behind a debug hash: the global function DebugHashToName's
// answer when there is one, else the hash's digits.
Ref
GetNameFromDebugHash(RefArg hash)
{
	RefVar name;
	if ((Ref) hash == NILREF)
		return NILREF;
	RefVar fn(GetFrameSlotRef(gFunctionFrame, Intern((char*) "DebugHashToName")));
	if ((Ref) fn == NILREF)
	{
		if (!ISINT((Ref) hash))
			return NILREF;
		UniChar digits[20];
		IntegerString(RINT(hash), digits);
		name = MakeString(digits);
	}
	else
	{
		RefVar args(AllocateArray(RSSYMarray, 1));
		SetArraySlotRef(args, 0, hash);
		name = DoBlock(fn, args);
	}
	return name;
}


// ROM 0x002d3ea4 FunctionDebugName__FRC6RefVar
// A function's name from its 'DebuggerInfo slot (an integer hash is the
// name itself here), else from its 'debug hash.
Ref
FunctionDebugName(RefArg fn)
{
	RefVar name;
	Ref info = GetFrameSlotRef(fn, RSSYMdebuggerinfo);
	if (ISINT(info))
		name = info;
	else if (FrameHasSlotRef(fn, RSSYMdebug))
	{
		name = GetFrameSlotRef(fn, RSSYMdebug);
		if (ISINT((Ref) name))
			name = GetNameFromDebugHash(name);
		else
			name = NILREF;
	}
	return name;
}


// ROM 0x002d2340 CheckForObjectName__FRC6RefVarPcT1
// obj named as context (contextName) or as one of its slots
// ("contextName.slot"); nil when neither.
Ref
CheckForObjectName(RefArg context, const char* contextName, RefArg obj)
{
	if (EQRef(obj, context))
		return MakeString(contextName);
	RefVar slotName(FindSlotName(context, obj));
	if ((Ref) slotName == NILREF)
		return NILREF;
	RefVar parts(AllocateArray(RSSYMarray, 3));
	SetArraySlotRef(parts, 0, MakeString(contextName));
	SetArraySlotRef(parts, 1, MAKECHAR('.'));
	SetArraySlotRef(parts, 2, slotName);
	return FFramesStringer(RefVar(NILREF), parts);
}


// ROM 0x002d2808 SearchForObjectName__FRC6RefVar
// The name of a well-known object: the globals frame or one of its
// slots ("vars", "vars.foo"), the global functions ("functions.bar"),
// the ROM's built-in functions.
Ref
SearchForObjectName(RefArg obj)
{
	RefVar name(CheckForObjectName(RefVar(gVarFrame), "vars", obj));
	if ((Ref) name == NILREF)
	{
		name = CheckForObjectName(RefVar(gFunctionFrame), "functions", obj);
		if ((Ref) name == NILREF && Rbuiltinfunctions != NILREF)
			name = CheckForObjectName(RefVar(Rbuiltinfunctions), "functions", obj);
	}
	return name;
}


// the name of a receiver or implementor frame: its 'DebuggerInfo or
// 'debug slot (a hash looked up), else a well-known object's name
static Ref
FrameDebugName(RefArg frame)
{
	RefVar name;
	if (FrameHasSlotRef(frame, RSSYMdebuggerinfo))
		name = GetFrameSlotRef(frame, RSSYMdebuggerinfo);
	else if (FrameHasSlotRef(frame, RSSYMdebug))
		name = GetFrameSlotRef(frame, RSSYMdebug);
	else
		name = SearchForObjectName(frame);
	if ((Ref) name != NILREF && ISINT((Ref) name))
		name = GetNameFromDebugHash(name);
	return name;
}


// ROM 0x002d31d0 NTKStackFrameInfo__FR11TNSDebugAPIl
// The NTK's description of call index: a frame {codeBlock, programCounter,
// receiver, implementor} (the stackFrameInfo prototype) with names where
// they can be found.
Ref
NTKStackFrameInfo(TNSDebugAPI& api, long index)
{
	RefVar fn(api.Function(index));
	RefVar receiver(api.Receiver(index));
	RefVar implementor(api.Implementor(index));
	RefVar info(Clone(RefVar(Rstackframeinfoframeprototype != NILREF ? Rstackframeinfoframeprototype : AllocateFrame())));
	RefVar name;
	if ((Ref) fn != NILREF)
	{
		name = FunctionDebugName(fn);
		if ((Ref) name == NILREF)
		{
			RefVar slotName;
			if ((Ref) implementor != NILREF)
				slotName = FindSlotName(implementor, fn);
			if ((Ref) slotName == NILREF)
				slotName = SearchForObjectName(fn);
			if ((Ref) slotName != NILREF)
				SetFrameSlot(info, RSSYMcodeblock, slotName);
		}
		else
			SetFrameSlot(info, RSSYMcodeblock, name);
	}
	SetFrameSlot(info, RSSYMprogramcounter, RefVar(MAKEINT(api.PC(index))));
	if ((Ref) receiver != NILREF)
	{
		name = FrameDebugName(receiver);
		if ((Ref) name != NILREF)
			SetFrameSlot(info, RSSYMreceiver, name);
	}
	if ((Ref) implementor != NILREF)
	{
		name = FrameDebugName(implementor);
		if ((Ref) name != NILREF)
			SetFrameSlot(info, RSSYMimplementor, name);
	}
	return info;
}


// ROM 0x002d3510 NTKStackTrace__FPv
// NOT YET RECONSTRUCTED: the NTK's stack trace over its connection.
void
NTKStackTrace(void* /*interpreter*/)
{ }


/* -------------------------------------------------------------------------------
	The REP's stack trace
------------------------------------------------------------------------------- */

// ROM 0x002d3104 PrintWellKnownObject__FRC6RefVarl
// An aggregate by its well-known name ("(vars.foo)") or by address and
// contents; anything else printed plainly.
void
PrintWellKnownObject(RefArg obj, long indent)
{
	if (IsAggregate(obj))
	{
		RefVar name(SearchForObjectName(obj));
		if ((Ref) name == NILREF)
		{
			long n = gREPout->Print("(#%lX) ", (long) (Ref) obj);
			PrintObject(obj, indent + n);
		}
		else
		{
			RefVar ascii(ASCIIString(name));
			gREPout->Print("(%s)", BinaryData(ascii));
		}
	}
	else
		gREPout->ConsumeFrame(obj, 0, indent);
}


// ROM 0x002d35bc REPStackTrace__FPv
// The calls from the innermost out: each function (by the slot it is in
// or its well-known name, else by address), its pc (or [native]), its
// receiver and its variables (arguments marked), printed with the
// globals' stackTracePrintDepth (0 when not an integer).
void
REPStackTrace(void* interpreter)
{
	TNSDebugAPI api((TInterpreter*) interpreter);
	long numFrames = api.NumStackFrames();
	if (numFrames == 0)
		return;
	RefVar savedPrintDepth(GetFrameSlotRef(gVarFrame, RSSYMprintdepth));
	Ref traceDepth = GetFrameSlotRef(gVarFrame, Intern((char*) "stackTracePrintDepth"));
	SetFrameSlot(RefVar(gVarFrame), RSSYMprintdepth, RefVar(ISINT(traceDepth) ? traceDepth : MAKEINT(0)));
	if (!api.AccurateStack())
		gREPout->Print("\r(The stack trace may be inaccurate; use SetDebugMode(true) for an accurate one.)\r");
	gREPout->Print("\rStack trace:\r");
	RefVar fn, receiver, implementor, slotName, locals, value, tag;
	for (long i = numFrames - 1; i >= 0; i--)
	{
		newton_try
		{
			fn = api.Function(i);
			receiver = api.Receiver(i);
			implementor = api.Implementor(i);
			long indent = gREPout->Print("%4d : ", (int) i);
			slotName = NILREF;
			if ((Ref) fn == NILREF)
				gREPout->Print("[incomplete stack frame]\r");
			else
			{
				if ((Ref) implementor != NILREF)
					slotName = FindSlotName(implementor, fn);
				if ((Ref) slotName == NILREF)
					PrintWellKnownObject(fn, indent);
				else
				{
					gREPout->Print("#%lX.", (long) (Ref) implementor);
					PrintObject(slotName, 0);
				}
				if (IsNativeFunction(fn))
					gREPout->Print(" [native]\r");
				else
					gREPout->Print(" : %ld\r", api.PC(i));
				if ((Ref) receiver != NILREF)
				{
					indent = gREPout->Print("       Receiver: ");
					PrintWellKnownObject(receiver, indent);
					gREPout->Print("\r");
				}
				if (!EQRef(ClassOf(fn), RSSYMcodeblock))
				{
					locals = api.Locals(i);
					long count = Length(locals);
					long numArgs = GetFunctionArgCount(fn);
					for (long j = 0; j < count; j++)
					{
						indent = gREPout->Print("       %ld", j);
						if (j < numArgs)
							indent += gREPout->Print(" [arg %ld]", j);
						indent += gREPout->Print(": ");
						value = GetArraySlotRef(locals, j);
						PrintWellKnownObject(value, indent);
						gREPout->Print("\r");
					}
				}
				else
				{
					// a CodeBlock's variables by the names in its argFrame's map
					RefVar argFrame(GetArraySlotRef(fn, kFunctionArgFrameSlot));
					RefVar map(ObjClass(OBJ((Ref) argFrame)));
					long count = Length(argFrame) - 3;
					long numArgs = RINT(GetArraySlotRef(fn, kFunctionNumArgsSlot));
					for (long j = 0; j < count; j++)
					{
						tag = GetTag(map, j + 3, nil);
						indent = gREPout->Print("       %ld %s", j, SymbolName(tag));
						if (j < numArgs)
							indent += gREPout->Print(" [arg %ld]", j);
						indent += gREPout->Print(": ");
						value = api.FindVar(i, tag);
						PrintWellKnownObject(value, indent);
						gREPout->Print("\r");
					}
				}
			}
		}
		newton_catch_all
		{
			gREPout->Print("  *** Skipping bad stack frame\r");
		}
		end_try;
	}
	SetFrameSlot(RefVar(gVarFrame), RSSYMprintdepth, savedPrintDepth);
}


// ROM 0x002d3cc4 StackTrace__12TInterpreterFv
void
TInterpreter::StackTrace(void)
{
	gREPout->StackTrace(this);
}


// ROM 0x002d3cd8 GetLocalFromStack__12TInterpreterFRC6RefVarT1
// A call's variable by index or by name.
Ref
TInterpreter::GetLocalFromStack(RefArg frameIndex, RefArg name)
{
	TNSDebugAPI api(this);
	if (ISINT((Ref) name))
		return api.GetVar(RINT(frameIndex), RINT(name));
	return api.FindVar(RINT(frameIndex), name);
}


// ROM 0x002d3d8c SetLocalOnStack__12TInterpreterFRC6RefVarN21
void
TInterpreter::SetLocalOnStack(RefArg frameIndex, RefArg name, RefArg value)
{
	TNSDebugAPI api(this);
	if (ISINT((Ref) name))
		api.SetVar(RINT(frameIndex), RINT(name), value);
	else
		api.SetFindVar(RINT(frameIndex), name, value);
}


// ROM 0x002d3e44 GetSelfFromStack__12TInterpreterFRC6RefVar
Ref
TInterpreter::GetSelfFromStack(RefArg frameIndex)
{
	TNSDebugAPI api(this);
	return api.Receiver(RINT(frameIndex));
}


/* -------------------------------------------------------------------------------
	The break loop and the debugging natives
------------------------------------------------------------------------------- */

// ROM 0x002b8050 BreakLoop__Fv
// Forms read and run until ExitBreakLoop sets the done flag.
void
BreakLoop(void)
{
	do
		REPIdle();
	while (!*gBreakLoopDone);
}


// ROM 0x002b808c REPBreakLoop__Fv
void
REPBreakLoop(void)
{
	newton_try
	{
		gREPout->EnterBreakLoop((int) gREPLevel);
		BreakLoop();
	}
	cleanup
	{
		gREPout->ExitBreakLoop();
	}
	end_try;
	gREPout->ExitBreakLoop();
}


// ROM 0x002b8134 FBreakLoop
// A nested REP in the receiver's context.
Ref
FBreakLoop(RefArg rcvr)
{
	RefVar savedContext(gREPContext);
	Boolean* savedDone = gBreakLoopDone;
	Boolean done = false;
	gREPContext = rcvr;
	gREPLevel++;
	gBreakLoopDone = &done;
	newton_try
	{
		REPBreakLoop();
	}
	cleanup
	{
		gREPContext = savedContext;
		gREPLevel--;
		gBreakLoopDone = savedDone;
	}
	end_try;
	gREPContext = savedContext;
	gREPLevel--;
	gBreakLoopDone = savedDone;
	return NILREF;
}


// ROM 0x002b81fc FExitBreakLoop
Ref
FExitBreakLoop(RefArg /*rcvr*/)
{
	if (gBreakLoopDone == nil)
		Throw(exInterpreter, (void*) kNSErrNotInBreakLoop, nil);
	*gBreakLoopDone = true;
	return NILREF;
}


// ROM 0x002b8248 FStackTrace
Ref
FStackTrace(RefArg /*rcvr*/)
{
	StackTrace();
	return NILREF;
}


// ROM 0x002b8260 FSetDebugMode
// Accurate stack traces on or off (the interpreter then runs its slow
// loop); ==> whether they were on.
Ref
FSetDebugMode(RefArg /*rcvr*/, RefArg on)
{
	Boolean wasOn = gAccurateStackTrace != 0;
	gAccurateStackTrace = (Ref) on != NILREF;
	return wasOn ? TRUEREF : NILREF;
}


// ROM 0x002b7f38 FWrite
// A string's text or a character printed as it is; anything else as
// PrintObject prints it.
Ref
FWrite(RefArg /*rcvr*/, RefArg obj)
{
	Ref ref = obj;
	if (ISCHAR(ref))
	{
		UniChar text[2] = { RCHAR(ref), 0 };
		gREPout->Print("%U", text);
	}
	else if (IsString(obj))
		SafelyPrintString(GetCString(obj));
	else
		PrintObject(obj, 0);
	return NILREF;
}


// ROM 0x002b8290 FLoad
// A text file of NewtonScript compiled and run form by form.
Ref
FLoad(RefArg /*rcvr*/, RefArg filename)
{
	RefVar ascii(ASCIIString(filename));
	return ParseFile(BinaryData(ascii));
}


// ROM 0x002b8328 FStats
// The heap's free space and largest free block printed; ==> the free space.
Ref
FStats(RefArg /*rcvr*/)
{
	ULong freeSpace, largestFree;
	gHeap->Statistics(&freeSpace, &largestFree);
	gREPout->Print("Free: %d, Largest: %d\r", (int) freeSpace, (int) largestFree);
	return MAKEINT(freeSpace);
}


#define NATIVE(symbol, fn, n)	RegisterNativeFunction(symbol, (void*) (NativeFn##n) fn, n)

void
RegisterDebugNatives(void)
{
	NATIVE("FBreakLoop", FBreakLoop, 0);
	NATIVE("FExitBreakLoop", FExitBreakLoop, 0);
	NATIVE("FStackTrace", FStackTrace, 0);
	NATIVE("FSetDebugMode", FSetDebugMode, 1);
	NATIVE("FWrite", FWrite, 1);
	NATIVE("FLoad", FLoad, 1);
	NATIVE("FStats", FStats, 0);
}
