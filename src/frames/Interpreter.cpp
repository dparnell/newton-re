/*
	File:		frames/Interpreter.cpp

	Contains:	The NewtonScript interpreter (Interpreter.h): the stacks, the
				VM states, calling and returning, the bytecode loop, exception
				handlers, and the entry points C++ uses to run NewtonScript.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
	The ROM has two bytecode loops: SlowRun (0x002cc66c), which handles
	tracing and breakpoints, and FastRun (0x002c8848), a copy of it with the
	instruction pointer and stack pointer held in registers and the common
	cases open-coded, chosen while nothing needs SlowRun (fFastLoop).  The
	two compute the same; here FastRun runs SlowRun's loop
	(NOT YET RECONSTRUCTED: FastRun1 and its Fast... helpers, 0x002c7440-
	0x002c9aa0, as a separate loop).

	Two things of the ROM's are not a host's: native functions in binary
	objects (ARM code) cannot be run, and the frames function profiler
	(gFramesFunctionProfiler) is not reconstructed.
*/

#include "Interpreter.h"
#include "NativeFunctions.h"
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "NSErrors.h"
#include "OSErrors.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"

#include <string.h>

TInterpreter*	gInterpreter = nil;
TInterpreter*	gInterpreterList = nil;
Ref				gFreqFuncs = NILREF;
Ref				gConstantsFrame = NILREF;
Ref				gFramesBreakPoints = NILREF;
Boolean			gFramesBreakPointsEnabled = false;
long			gAccurateStackTrace = 0;
Boolean			gUseCFunctionDocStrings = false;
Ref				gCFunctionPrototype = NILREF;
Ref				gCodeBlockPrototype = NILREF;		// CodeBlock::fgPrototype (the ROM's 0x005cecc1)
Ref				gDebugCodeBlockPrototype = NILREF;	// DebugCodeBlock::fgPrototype (0x005cf119)

// the frequently called functions and their argument counts (0x0c1022e8)
const FreqFuncInfo gFreqFuncInfo[kNumFreqFuncs] = {
	{ "+", 2 }, { "-", 2 }, { "aref", 2 }, { "setAref", 3 }, { "=", 2 }, { "not", 1 }, { "<>", 2 },
	{ "*", 2 }, { "/", 2 }, { "div", 2 }, { "<", 2 }, { ">", 2 }, { ">=", 2 }, { "<=", 2 },
	{ "band", 2 }, { "bor", 2 }, { "bnot", 2 }, { "newiterator", 2 }, { "length", 1 }, { "clone", 1 },
	{ "setClass", 2 }, { "addArraySlot", 2 }, { "stringer", 1 }, { "hasPath", 2 }, { "ClassOf", 1 },
};
const long gNumFreqFuncs = kNumFreqFuncs;

// the stacks: the ROM gives each 64 KB from the stack manager (NewStack,
// paged in as used); here a fixed number of refs
const long	kRefStackRefs = 16384;


/* -------------------------------------------------------------------------------
	TRefStack
------------------------------------------------------------------------------- */

// ROM 0x001a6f5c TRefStackMark__FPv
static void
TRefStackMark(void* stack)
{
	TRefStack* s = (TRefStack*) stack;
	for (Ref* p = s->fBase; p < s->fTop; p++)
		DIYGCMark(*p);
}


// ROM 0x001a6f90 TRefStackUpdate__FPv
static void
TRefStackUpdate(void* stack)
{
	TRefStack* s = (TRefStack*) stack;
	for (Ref* p = s->fBase; p < s->fTop; p++)
		*p = DIYGCUpdate(*p);
}


// ROM 0x001a6e64 __ct__9TRefStackFv
TRefStack::TRefStack()
{
	fSize = 300;
	fBase = (Ref*) NewPtr(kRefStackRefs * sizeof(Ref));
	if (fBase == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	fTop = fBase;
	fLimit = fBase + kRefStackRefs;					// (the ROM: base + 0x498, what its release proc reports)
	DIYGCRegister(this, TRefStackMark, TRefStackUpdate);
}


// ROM 0x001a6f28 __dt__9TRefStackFv
TRefStack::~TRefStack()
{
	DIYGCUnregister(this);
	DisposPtr((Ptr) fBase);
}


// ROM 0x001a6b9c Reset__9TRefStackFl
// Back to depth refs (never up).
void
TRefStack::Reset(long depth)
{
	long current = Depth() - 1;
	if (current <= depth)
		return;
	fTop -= current - depth;
}


// ROM 0x001a6be0 PushNILs__9TRefStackFl
void
TRefStack::PushNILs(long count)
{
	if (fTop + count > fLimit)							// DEVIATION: the ROM's stack pages in; this one ends
		Throw(exOutOfStack, nil, nil);
	Ref* p = fTop;
	fTop = p + count;
	for (long i = 0; i < count; i++)
		p[i] = NILREF;
}


/* -------------------------------------------------------------------------------
	TRefStructStack, TIntrpStack
------------------------------------------------------------------------------- */

// ROM 0x001a6ff8 __ct__15TRefStructStackFv
TRefStructStack::TRefStructStack()
{
	fHandles = (RefHandle**) NewPtr(kRefStackRefs * sizeof(RefHandle*));
	if (fHandles == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	fHandlesEnd = fHandles;
}


// ROM 0x001a7098 __dt__15TRefStructStackFv
TRefStructStack::~TRefStructStack()
{
	DisposPtr((Ptr) fHandles);
}


// ROM 0x001a70d4 Fill__15TRefStructStackFv
// Pointers for every slot in use.
void
TRefStructStack::Fill(void)
{
	long filled = fHandlesEnd - fHandles;
	long depth = Depth();
	for (long i = filled; i < depth; i++)
		fHandles[i] = (RefHandle*) &fBase[i];
	fHandlesEnd = fHandles + depth;
}


// the state whose six refs end at the top of the stack
static inline VMState*
TopState(TIntrpStack* stack)
{
	if (stack->fHandlesEnd - stack->fHandles < stack->Depth())
		stack->Fill();
	return (VMState*) &stack->fHandles[stack->Depth() - kVMStateSize];
}


// ROM 0x001a6c70 NewState__11TIntrpStackFv
VMState*
TIntrpStack::NewState(void)
{
	PushNILs(kVMStateSize);
	return TopState(this);
}


// ROM 0x001a6cfc DupState__11TIntrpStackFv
VMState*
TIntrpStack::DupState(void)
{
	if (fTop + kVMStateSize > fLimit)					// DEVIATION: see PushNILs
		Throw(exOutOfStack, nil, nil);
	Ref* p = fTop;
	for (long i = 0; i < kVMStateSize; i++)
		p[i] = p[i - kVMStateSize];
	fTop = p + kVMStateSize;
	return TopState(this);
}


// ROM 0x001a6da0 PrevState__11TIntrpStackFv
VMState*
TIntrpStack::PrevState(void)
{
	fTop -= kVMStateSize;
	return TopState(this);
}


// ROM 0x001a6e10 StateAt__11TIntrpStackFl
VMState*
TIntrpStack::StateAt(long index)
{
	if (fHandlesEnd - fHandles < Depth())
		Fill();
	return (VMState*) &fHandles[index * kVMStateSize];
}


/* -------------------------------------------------------------------------------
	Function objects
------------------------------------------------------------------------------- */

// ROM 0x002d1604 IsFunction__FRC6RefVar
Boolean
IsFunction(RefArg obj)
{
	Ref r = obj;
	if (!ISPTR(r) || (ObjectFlags(r) & kObjSlotted) == 0 || Length(r) == 0)
		return false;
	RefVar theClass(GetArraySlotRef(r, kFunctionClassSlot));
	Ref c = theClass;
	if (c == kFuncClass || c == kNativeFuncClass || c == kBinaryNativeFuncClass)
		return true;
	return EQRef(c, RSSYMcodeblock) || EQRef(c, RSSYMbincfunction);
}


// ROM 0x002af4e4 IsNativeFunction__FRC6RefVar
Boolean
IsNativeFunction(RefArg fn)
{
	Ref c = GetArraySlotRef(fn, kFunctionClassSlot);
	return c == kNativeFuncClass || c == kBinaryNativeFuncClass || EQRef(c, RSSYMbincfunction);
}


// ROM 0x002d16dc GetFunctionArgCount__FRC6RefVar
long
GetFunctionArgCount(RefArg fn)
{
	if (!IsFunction(fn))
		ThrowBadTypeWithFrameData(kNSErrNotAFunction, fn);
	RefVar theClass(GetArraySlotRef(fn, kFunctionClassSlot));
	Ref c = theClass;
	if (c == kFuncClass)
		return RVALUE(ObjArraySlots(OBJ(fn))[kFunctionNumArgsSlot]) & 0xffff;
	if (c == kNativeFuncClass)
		return RVALUE(ObjArraySlots(OBJ(fn))[kNativeNumArgsSlot]);
	Ref n;
	if (c == kBinaryNativeFuncClass)
		n = GetArraySlotRef(fn, 2);
	else if (EQRef(c, RSSYMcodeblock))
		n = GetArraySlotRef(fn, 4);
	else if (EQRef(c, RSSYMbincfunction))
		n = GetFrameSlotRef(fn, RSSYMnumargs);
	else
		return 0;
	return RINT(n);
}


// ROM 0x002d1548 MakeCFunction__FPFRC6RefVare_llPc
// A native function object over a C function (its address in the funcPtr
// slot as an integer-tagged word).
Ref
MakeCFunction(void* funcPtr, long numArgs, const char* docString)
{
	RefVar fn(AllocateArray(RefVar(NILREF), gUseCFunctionDocStrings ? 4 : 3));
	SetArraySlotRef(fn, kFunctionClassSlot, kNativeFuncClass);
	SetArraySlotRef(fn, kNativeFuncPtrSlot, (Ref) funcPtr);
	SetArraySlotRef(fn, kNativeNumArgsSlot, MAKEINT(numArgs));
	if (gUseCFunctionDocStrings)
		SetArraySlotRef(fn, kNativeDocStringSlot, MakeString(docString));
	return fn;
}


// ROM 0x002d0ce4 NativeEntry__FRC6RefVarlPP9RefHandle
// The code of a native function and its closure, given the argument count
// matches; nil for a function that is not native.  NOT YET RECONSTRUCTED:
// code in binary objects (0x232, binCFunction) is ARM code.
void*
NativeEntry(RefArg fn, long numArgs, RefHandle** closure)
{
	Ref c = GetArraySlotRef(fn, kFunctionClassSlot);
	if (c == kNativeFuncClass)
	{
		if (numArgs != RVALUE(ObjArraySlots(OBJ(fn))[kNativeNumArgsSlot]))
			Throw(exInterpreter, (void*) kNSErrWrongNumberOfArgs, nil);
		(*closure)->ref = NILREF;
		return (void*) ObjArraySlots(OBJ(fn))[kNativeFuncPtrSlot];
	}
	if (c == kBinaryNativeFuncClass || EQRef(c, RSSYMbincfunction))
		Throw(exInterpreter, (void*) kNSErrNativeNotReconstructed, nil);
	return nil;
}


/* -------------------------------------------------------------------------------
	TInterpreter
------------------------------------------------------------------------------- */

// ROM 0x002ce86c __ct__12TInterpreterFv
TInterpreter::TInterpreter()
{
	fNext = gInterpreterList;
	gInterpreterList = this;
	fField78 = 0;
	fField64 = 0;
	fTraceLevel = 0;
	fField68 = 0;
	fField69 = 0;
	fExceptionStackIndex = 0;
	fVMState = fCtrlStack.NewState();
	StateRef(fVMState->fPC) = MAKEINT(0);
	SetFastLoopFlag();
}


// ROM 0x002ce958 __dt__12TInterpreterFv
TInterpreter::~TInterpreter()
{
	for (TInterpreter** link = &gInterpreterList; *link != nil; link = &(*link)->fNext)
		if (*link == this)
		{
			*link = fNext;
			break;
		}
}


// ROM 0x002cea00 PushValue__12TInterpreterFRC6RefVar
void
TInterpreter::PushValue(RefArg value)
{
	if (fValueStack.fTop >= fValueStack.fLimit)			// DEVIATION: see TRefStack::PushNILs
		Throw(exOutOfStack, nil, nil);
	*fValueStack.fTop++ = value;
}


// ROM 0x002cea18 PopValue__12TInterpreterFv
Ref
TInterpreter::PopValue(void)
{
	return *--fValueStack.fTop;
}


// ROM 0x002cea2c PeekValue__12TInterpreterFl
Ref
TInterpreter::PeekValue(long depth)
{
	return fValueStack.fTop[-1 - depth];
}


// ROM 0x002cea40 SetValue__12TInterpreterFlT1
void
TInterpreter::SetValue(long depth, Ref value)
{
	fValueStack.fTop[-1 - depth] = value;
}


// ROM 0x002cea54 ValuePosition__12TInterpreterFv
long
TInterpreter::ValuePosition(void)
{
	return fValueStack.Depth() - 1;
}


// ROM 0x002cea70 PeekControl__12TInterpreterFl
Ref
TInterpreter::PeekControl(long depth)
{
	return fCtrlStack.fTop[-1 - depth];
}


// ROM 0x002cea84 SetControl__12TInterpreterFlT1
void
TInterpreter::SetControl(long depth, Ref value)
{
	fCtrlStack.fTop[-1 - depth] = value;
}


// ROM 0x002cea98 ControlPosition__12TInterpreterFv
long
TInterpreter::ControlPosition(void)
{
	return fCtrlStack.Depth() - 1;
}


// ROM 0x002d0b40 GetReceiver__12TInterpreterFv
Ref
TInterpreter::GetReceiver(void)
{
	return StateRef(fVMState->fReceiver);
}


// ROM 0x002d0b50 GetImplementor__12TInterpreterFv
Ref
TInterpreter::GetImplementor(void)
{
	return StateRef(fVMState->fImplementor);
}


// ROM 0x002d0b60 IsSend__12TInterpreterFv
Boolean
TInterpreter::IsSend(void)
{
	return fIsSend;
}


// ROM 0x002d0b68 SetCallEnv__12TInterpreterFv
void
TInterpreter::SetCallEnv(void)
{
	fIsSend = false;
}


// ROM 0x002d0b74 SetSendEnv__12TInterpreterFRC6RefVarT1
void
TInterpreter::SetSendEnv(RefArg receiver, RefArg implementor)
{
	StateRef(fVMState->fReceiver) = receiver;
	StateRef(fVMState->fImplementor) = implementor;
	fIsSend = true;
}


// ROM 0x002cc478 SetFastLoopFlag__12TInterpreterFv
// FastRun may run when nothing wants SlowRun: no stack-trace accuracy,
// tracing, breakpoints or profiling, and instructions that stay put.
void
TInterpreter::SetFastLoopFlag(void)
{
	fFastLoop = gAccurateStackTrace == 0 && fTraceLevel == 0 && !gFramesBreakPointsEnabled && fInstructionsFixed;
}


// ROM 0x002cfbc8 SetFlags__12TInterpreterFv
// The loop's view of the current function: its instructions and literals,
// where its locals are.
void
TInterpreter::SetFlags(void)
{
	ObjHeader* fn = OBJ(StateRef(fVMState->fFunction));
	fInstructions = ObjArraySlots(fn)[kFunctionInstructionsSlot];
	fLiterals = ObjArraySlots(fn)[kFunctionLiteralsSlot];
	fInstructionsFixed = (ObjectFlags(fInstructions) & (kObjLocked | kObjReadOnly)) != 0;
	Ref frame = StateRef(fVMState->fStackFrame);
	if ((RVALUE(frame) & kStackFrameLocalsOnStack) == 0)
		fLocalsOnStack = false;
	else
	{
		fLocalsIndex = RVALUE(frame) >> 6;
		fLocalsOnStack = true;
	}
	SetFastLoopFlag();
}


/* -------------------------------------------------------------------------------
	Calling
------------------------------------------------------------------------------- */

// the stack-frame word of a state: base << 6 | flags, as an integer ref.
// The base is the stack index of local 0 and is negative for a function
// with fewer than three arguments (the three hidden locals come first), so
// the shift is done unsigned - the ROM's ARM shift did not care.
static inline Ref
StackFrameWord(long base, long flags)
{
	return MAKEINT((long) (((ULong) base << 6) | (ULong) flags));
}


// ROM 0x002ceab4 TopLevelCall__12TInterpreterFRC6RefVarT1
// Start running fn (no arguments) at the top of the control stack: a
// NewtonScript function's locals go on the value stack, its argFrame (if
// it has one) is cloned for its closure; a 1.x CodeBlock's cloned argFrame
// holds everything.  Other function kinds are ignored.
void
TInterpreter::TopLevelCall(RefArg fn, RefArg /*receiver*/)
{
	Ref theClass = GetArraySlotRef(fn, kFunctionClassSlot);
	if (theClass == kFuncClass)
	{
		fVMState = fCtrlStack.NewState();
		ObjHeader* f = NoFaultObjectPtr(fn);
		Ref numArgs = RVALUE(ObjArraySlots(f)[kFunctionNumArgsSlot]);
		long numLocals = numArgs >> 16;
		StateRef(fVMState->fFunction) = fn;
		fPC = 0;
		if ((numArgs & 0xffff) != 0)
			Throw(exInterpreter, (void*) kNSErrWrongNumberOfArgs, nil);
		StateRef(fVMState->fStackFrame) = StackFrameWord(fValueStack.Depth() - 3, kStackFrameLocalsOnStack | kStackFrameIsSend);
		if (ObjArraySlots(f)[kFunctionArgFrameSlot] != NILREF)
		{
			RefVar locals(ObjArraySlots(f)[kFunctionArgFrameSlot]);
			locals = Clone(locals);
			ObjHeader* l = OBJ(locals);
			StateRef(fVMState->fReceiver) = ObjArraySlots(l)[kArgFrameParentSlot];
			StateRef(fVMState->fImplementor) = ObjArraySlots(l)[kArgFrameImplementorSlot];
			StateRef(fVMState->fLocals) = locals;
		}
		if (numLocals != 0)
			fValueStack.PushNILs(numLocals);
		SetFlags();
		return;
	}
	if (!EQRef(theClass, RSSYMcodeblock))
		return;
	fVMState = fCtrlStack.NewState();
	StateRef(fVMState->fFunction) = fn;
	fPC = 0;
	if (RINT(GetArraySlotRef(fn, 4)) != 0)
		Throw(exInterpreter, (void*) kNSErrWrongNumberOfArgs, nil);
	RefVar locals(GetArraySlotRef(fn, 3));
	locals = Clone(locals);
	StateRef(fVMState->fStackFrame) = StackFrameWord(fValueStack.Depth() - 3, kStackFrameIsSend);
	ObjHeader* l = OBJ(locals);
	StateRef(fVMState->fReceiver) = ObjArraySlots(l)[kArgFrameParentSlot];
	StateRef(fVMState->fImplementor) = ObjArraySlots(l)[kArgFrameImplementorSlot];
	StateRef(fVMState->fLocals) = locals;
	SetFlags();
}


// ROM 0x002cebc8 Call__12TInterpreterFRC6RefVarl
// Call fn with numArgs arguments on the value stack.  A NewtonScript
// function becomes the current one (answers false: the loop runs it); a
// native one is called here, its result replacing the arguments, and the
// caller's state restored (answers true).
Boolean
TInterpreter::Call(RefArg fn, long numArgs)
{
	StateRef(fVMState->fPC) = MAKEINT(fPC);
	Ref theClass = GetArraySlotRef(fn, kFunctionClassSlot);
	if ((theClass & 0xff) == kFuncClass)
	{
		long kind = FUNCKIND(theClass) & 0xfff;
		if (kind == 0)
		{
			fVMState = fCtrlStack.NewState();
			CallPlainCodeBlock(fn, numArgs, 0);
			return false;
		}
		if (kind == 1)
		{
			VMState* caller = fVMState;
			fVMState = fCtrlStack.NewState();
			StateRef(fVMState->fReceiver) = StateRef(caller->fReceiver);
			StateRef(fVMState->fStackFrame) = StackFrameWord(fValueStack.Depth() - numArgs, 0);
			fIsSend = false;
			CallPlainCFunction(fn, numArgs);
			return true;
		}
	}
	if (theClass != kBinaryNativeFuncClass && !EQRef(theClass, RSSYMbincfunction))
	{
		if (!EQRef(theClass, RSSYMcodeblock))
			ThrowBadTypeWithFrameData(kNSErrNotAFunction, fn);
		fVMState = fCtrlStack.NewState();
		CallCodeBlock(fn, numArgs, 0);
		return false;
	}
	fIsSend = false;
	fVMState = fCtrlStack.NewState();
	StateRef(fVMState->fStackFrame) = StackFrameWord(fValueStack.Depth() - numArgs, 0);
	CallCFunction(fn, numArgs, theClass != kBinaryNativeFuncClass);
	fVMState = fCtrlStack.PrevState();
	fPC = RINT(StateRef(fVMState->fPC));
	if (StateRef(fVMState->fFunction) != NILREF && fPC != -1)
		SetFlags();
	return true;
}


// ROM 0x002cef08 Send__12TInterpreterFRC6RefVarN21l
// As Call, with the receiver and implementor of the message.
Boolean
TInterpreter::Send(RefArg receiver, RefArg implementor, RefArg fn, long numArgs)
{
	StateRef(fVMState->fPC) = MAKEINT(fPC);
	fVMState = fCtrlStack.NewState();
	StateRef(fVMState->fReceiver) = receiver;
	StateRef(fVMState->fImplementor) = implementor;
	Ref theClass = GetArraySlotRef(fn, kFunctionClassSlot);
	if ((theClass & 0xff) == kFuncClass)
	{
		long kind = FUNCKIND(theClass) & 0xfff;
		if (kind == 0)
		{
			CallPlainCodeBlock(fn, numArgs, 1);
			return false;
		}
		if (kind == 1)
		{
			fIsSend = true;
			CallPlainCFunction(fn, numArgs);
			return true;
		}
	}
	if (theClass != kBinaryNativeFuncClass && !EQRef(theClass, RSSYMbincfunction))
	{
		if (!EQRef(theClass, RSSYMcodeblock))
			ThrowBadTypeWithFrameData(kNSErrNotAFunction, fn);
		CallCodeBlock(fn, numArgs, 1);
		return false;
	}
	StateRef(fVMState->fLocals) = StateRef(fVMState->fReceiver);
	fIsSend = true;
	CallCFunction(fn, numArgs, theClass != kBinaryNativeFuncClass);
	fVMState = fCtrlStack.PrevState();
	fPC = RINT(StateRef(fVMState->fPC));
	if (StateRef(fVMState->fFunction) != NILREF && fPC != -1)
		SetFlags();
	return true;
}


// ROM 0x002cf1e0 CallCodeBlock__12TInterpreterFRC6RefVarlT2
// A 1.x function: the arguments go from the stack into a clone of its
// argFrame, which is the locals frame; flags: 1 a send (the receiver and
// implementor are the state's, stored into the frame), 2 (stack-frame bit).
void
TInterpreter::CallCodeBlock(RefArg fn, long numArgs, long flags)
{
	StateRef(fVMState->fFunction) = fn;
	fPC = 0;
	if (RINT(GetArraySlotRef(fn, 4)) != numArgs)
		Throw(exInterpreter, (void*) kNSErrWrongNumberOfArgs, nil);
	RefVar locals(GetArraySlotRef(fn, 3));
	locals = Clone(locals);
	for (long i = 0; i < numArgs; i++)
	{
		Ref arg = *--fValueStack.fTop;
		ObjArraySlots(OBJ(locals))[kArgFrameFirstArgSlot + numArgs - 1 - i] = arg;
	}
	StateRef(fVMState->fStackFrame) = StackFrameWord(fValueStack.Depth() - 3, (flags & 2) ? kStackFrameIsSend : 0);
	ObjHeader* l = OBJ(locals);
	if ((flags & 1) == 0)
	{
		StateRef(fVMState->fReceiver) = ObjArraySlots(l)[kArgFrameParentSlot];
		StateRef(fVMState->fImplementor) = ObjArraySlots(l)[kArgFrameImplementorSlot];
	}
	else
	{
		ObjArraySlots(l)[kArgFrameParentSlot] = StateRef(fVMState->fReceiver);
		ObjArraySlots(l)[kArgFrameImplementorSlot] = StateRef(fVMState->fImplementor);
	}
	StateRef(fVMState->fLocals) = locals;
	SetFlags();
}


// ROM 0x002cf390 CallPlainCodeBlock__12TInterpreterFRC6RefVarlT2
// A NewtonScript function: the arguments stay on the value stack, the
// locals are pushed after them (NILREF), and the argFrame, if there is
// one, is cloned for the closure's _nextArgFrame, _parent and
// _implementor - a send fills the latter two from the state when the
// argFrame leaves them 0 for it.
void
TInterpreter::CallPlainCodeBlock(RefArg fn, long numArgs, long flags)
{
	ObjHeader* f = NoFaultObjectPtr(fn);
	Ref numArgsWord = RVALUE(ObjArraySlots(f)[kFunctionNumArgsSlot]);
	long numLocals = numArgsWord >> 16;
	StateRef(fVMState->fFunction) = fn;
	fPC = 0;
	if (numArgs != (numArgsWord & 0xffff))
		Throw(exInterpreter, (void*) kNSErrWrongNumberOfArgs, nil);
	long frameFlags = (flags & 2) ? (kStackFrameLocalsOnStack | kStackFrameIsSend) : kStackFrameLocalsOnStack;
	StateRef(fVMState->fStackFrame) = StackFrameWord(fValueStack.Depth() - numArgs - 3, frameFlags);
	if (ObjArraySlots(f)[kFunctionArgFrameSlot] != NILREF)
	{
		RefVar locals(ObjArraySlots(f)[kFunctionArgFrameSlot]);
		locals = Clone(locals);
		ObjHeader* l = OBJ(locals);
		if ((flags & 1) == 0)
		{
			StateRef(fVMState->fReceiver) = ObjArraySlots(l)[kArgFrameParentSlot];
			StateRef(fVMState->fImplementor) = ObjArraySlots(l)[kArgFrameImplementorSlot];
		}
		else
		{
			if (ObjArraySlots(l)[kArgFrameParentSlot] == 0)
				ObjArraySlots(l)[kArgFrameParentSlot] = NILREF;
			else
				ObjArraySlots(l)[kArgFrameParentSlot] = StateRef(fVMState->fReceiver);
			if (ObjArraySlots(l)[kArgFrameImplementorSlot] == 0)
				ObjArraySlots(l)[kArgFrameImplementorSlot] = NILREF;
			else
				ObjArraySlots(l)[kArgFrameImplementorSlot] = StateRef(fVMState->fImplementor);
		}
		StateRef(fVMState->fLocals) = locals;
	}
	if (numLocals != 0)
		fValueStack.PushNILs(numLocals);
	SetFlags();
}


// ROM 0x002cf534 CallCFunction__12TInterpreterFRC6RefVarli
// A native function with its code in a binary: the closure (if any) goes
// on the stack as an extra argument.  isFrame: a binCFunction frame rather
// than a 0x232 array.  NOT YET RECONSTRUCTED: running the ARM code
// (NativeEntry throws).
void
TInterpreter::CallCFunction(RefArg fn, long numArgs, int /*isFrame*/)
{
	StateRef(fVMState->fFunction) = fn;
	fPC = -1;
	StateRef(fVMState->fPC) = MAKEINT(-1);
	RefVar closure;
	NativeEntry(fn, numArgs, &closure.h);
}


// ROM 0x002cf718 CallPlainCFunction__12TInterpreterFRC6RefVarl
// A native function: called with the arguments on the value stack, its
// result replacing them; then back to the caller's state.
void
TInterpreter::CallPlainCFunction(RefArg fn, long numArgs)
{
	StateRef(fVMState->fFunction) = fn;
	fPC = -1;
	StateRef(fVMState->fPC) = MAKEINT(-1);
	ObjHeader* f = NoFaultObjectPtr(fn);
	if (numArgs != RVALUE(ObjArraySlots(f)[kNativeNumArgsSlot]))
	{
		StateRef(fVMState->fLocals) = NILREF;
		Throw(exInterpreter, (void*) kNSErrWrongNumberOfArgs, nil);
	}
	Ref result = CallCFuncPtr((void*) ObjArraySlots(f)[kNativeFuncPtrSlot], numArgs);
	fValueStack.fTop -= numArgs - 1;
	fValueStack.fTop[-1] = result;
	if (fTraceLevel != 0)
		TraceReturn();
	fVMState = fCtrlStack.PrevState();
	fPC = RINT(StateRef(fVMState->fPC));
	if (StateRef(fVMState->fFunction) != NILREF && fPC != -1)
		SetFlags();
}


// ROM 0x002cf8b0 CallCFuncPtr__12TInterpreterFPFRC6RefVare_ll
// Call a C function with the receiver and the numArgs values at the top of
// the stack, each passed as the RefVar of its slot (StackRef).  A ROM
// address goes through the native registry (NativeFunctions.h).
Ref
TInterpreter::CallCFuncPtr(void* funcPtr, long numArgs)
{
	if (fValueStack.fHandlesEnd - fValueStack.fHandles < fValueStack.Depth())
		fValueStack.Fill();
	long first = fValueStack.Depth() - numArgs;
	const RefVar& rcvr = StateVar(fVMState->fReceiver);
	void* fn = funcPtr;
	if ((ULong) funcPtr < kROMCodeLimit)
	{
		long boundArgs;
		fn = ResolveNativeFunction((ULong) funcPtr, &boundArgs);
		if (fn == nil)
			Throw(exInterpreter, (void*) kNSErrNativeNotReconstructed, nil);
	}
	#define ARG(i)	fValueStack.StackRef(first + (i))
	switch (numArgs)
	{
	case 0:	return ((NativeFn0) fn)(rcvr);
	case 1:	return ((NativeFn1) fn)(rcvr, ARG(0));
	case 2:	return ((NativeFn2) fn)(rcvr, ARG(0), ARG(1));
	case 3:	return ((NativeFn3) fn)(rcvr, ARG(0), ARG(1), ARG(2));
	case 4:	return ((NativeFn4) fn)(rcvr, ARG(0), ARG(1), ARG(2), ARG(3));
	case 5:	return ((NativeFn5) fn)(rcvr, ARG(0), ARG(1), ARG(2), ARG(3), ARG(4));
	case 6:	return ((NativeFn6) fn)(rcvr, ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5));
	}
	#undef ARG
	Throw(exInterpreter, (void*) kNSErrTooManyArgs, nil);
	return NILREF;
}


// ROM 0x002cfa54 Return__12TInterpreterF19FramesProfilingKind
// Return from the current function: its result (the top of the value
// stack) replaces its frame when the locals were on the stack; back to the
// caller's state, dropping the exception handlers of the frames left.
void
TInterpreter::Return(FramesProfilingKind /*kind*/)
{
	RefVar fn(StateRef(fVMState->fFunction));
	if (fTraceLevel != 0)
		TraceReturn();
	Ref frame = RVALUE(StateRef(fVMState->fStackFrame));
	if ((frame & kStackFrameLocalsOnStack) != 0)
	{
		Ref result = fValueStack.fTop[-1];
		Ref* p = fValueStack.fBase + (frame >> 6) + 3;
		fValueStack.fTop = p + 1;
		*p = result;
	}
	fVMState = fCtrlStack.PrevState();
	fPC = RVALUE(StateRef(fVMState->fPC));
	if (ControlPosition() < fExceptionStackIndex)
		PopHandlers();
	if (StateRef(fVMState->fFunction) != NILREF && fPC != -1)
		SetFlags();
}


/* -------------------------------------------------------------------------------
	Running
------------------------------------------------------------------------------- */

// ROM 0x002cc45c UndefinedBytecode__Fv
static void
UndefinedBytecode(void)
{
	Throw(exInterpreter, (void*) kNSErrUndefinedBytecode, nil);
}


// the out-of-bounds frame the aref instructions throw
static void
ThrowOutOfBoundsException(RefArg obj, long index)
{
	RefVar frame(AllocateFrame());
	SetFrameSlot(frame, RSSYMerrorcode, RefVar(MAKEINT(kNSErrOutOfBounds)));
	SetFrameSlot(frame, RSSYMvalue, obj);
	SetFrameSlot(frame, RSSYMindex, RefVar(MAKEINT(index)));
	ThrowRefException(exFramesWithFrameData, frame);
}


// the =/<> of the loop: identical refs, numbers by value, else EQ
static Boolean
FreqEqual(RefArg a, RefArg b)
{
	Ref ra = a, rb = b;
	if (((ra | rb) & 1) == 0)
		return ra == rb;
	Boolean aReal = ISREAL(ra), bReal = ISREAL(rb);
	if (aReal && bReal)
		return CDouble(a) == CDouble(b);
	if (aReal || bReal)
	{
		if ((ra & 3) != 0 && (rb & 3) != 0)
			return false;
		return CoerceToDouble(a) == CoerceToDouble(b);
	}
	return EQRef(ra, rb) != 0;
}


// ROM 0x002cc66c SlowRun__12TInterpreterFl
// The bytecode loop: instructions of the current function, until a return
// takes the control stack below baseDepth (answers true) or the fast loop
// may take over (false).
Boolean
TInterpreter::SlowRun(long baseDepth)
{
	Boolean traceVars = gInterpreter->fTraceLevel > 1;
	RefVar arg1, arg2, arg3, arg4;
	Ref* sp;
	#define PUSH(v)		(*fValueStack.fTop++ = (v))
	#define POP()		(*--fValueStack.fTop)
	#define TOP()		(fValueStack.fTop[-1])
	#define LOCAL(i)	(fLocalsOnStack ? fValueStack.fBase[fLocalsIndex + (i)] : ObjArraySlots(OBJ(StateRef(fVMState->fLocals)))[i])
	#define LITERAL(i)	(ObjArraySlots(OBJ((Ref) fLiterals))[i])
	for (;;)
	{
		Boolean returned = false;
		do {
			if (gFramesBreakPointsEnabled)
				HandleBreakPoints();
			const unsigned char* instr = (const unsigned char*) BinaryData(fInstructions) + fPC;
			unsigned op = instr[0];
			long b = op & 7;
			if (b == 7)
			{
				b = ((long) instr[1] << 8) | instr[2];
				fPC += 3;
			}
			else
				fPC += 1;
			switch (op >> 3)
			{
			case 0:												// the simple instructions
				switch (op)
				{
				case kBCPop:
					fValueStack.fTop--;
					break;
				case kBCDup:
					sp = fValueStack.fTop;
					*sp = sp[-1];
					fValueStack.fTop = sp + 1;
					break;
				case kBCReturn:
					Return(kProfileReturn);
					returned = true;
					break;
				case kBCPushSelf:
					PUSH(StateRef(fVMState->fReceiver));
					break;
				case kBCSetLexScope:
				{
					// a closure: a copy of the function whose argFrame's
					// _nextArgFrame, _parent and _implementor are this call's
					arg1 = POP();
					arg1 = Clone(arg1);
					arg2 = ObjArraySlots(OBJ((Ref) arg1))[kFunctionArgFrameSlot];
					arg2 = Clone(arg2);
					ObjHeader* af = OBJ((Ref) arg2);
					ObjArraySlots(af)[kArgFrameNextArgFrameSlot] = StateRef(fVMState->fLocals);
					if (!fLocalsOnStack)
					{
						ObjArraySlots(af)[kArgFrameParentSlot] = StateRef(fVMState->fReceiver);
						ObjArraySlots(af)[kArgFrameImplementorSlot] = StateRef(fVMState->fImplementor);
					}
					else
					{
						if (ObjArraySlots(af)[kArgFrameParentSlot] == 0)
							ObjArraySlots(af)[kArgFrameParentSlot] = NILREF;
						else
							ObjArraySlots(af)[kArgFrameParentSlot] = StateRef(fVMState->fReceiver);
						if (ObjArraySlots(af)[kArgFrameImplementorSlot] == 0)
							ObjArraySlots(af)[kArgFrameImplementorSlot] = NILREF;
						else
							ObjArraySlots(af)[kArgFrameImplementorSlot] = StateRef(fVMState->fImplementor);
						// an argFrame of nothing but nils is no argFrame
						if (Length(arg2) == 3 && ObjArraySlots(af)[0] == NILREF && ObjArraySlots(af)[1] == NILREF && ObjArraySlots(af)[2] == NILREF)
							arg2 = NILREF;
					}
					ObjArraySlots(OBJ((Ref) arg1))[kFunctionArgFrameSlot] = arg2;
					PUSH((Ref) arg1);
					break;
				}
				case kBCIterNext:
					arg1 = POP();
					ForEachLoopNext(arg1);
					break;
				case kBCIterDone:
					arg1 = POP();
					PUSH(ForEachLoopDone(arg1) ? TRUEREF : NILREF);
					break;
				case kBCPopHandlers:
					if (b == 7)
						fExceptionContext = GetArraySlotRef(fExceptionContext, kHandlerNext);
					else
						UndefinedBytecode();
					break;
				default:
					UndefinedBytecode();
				}
				break;

			case kBCPush:
				PUSH(LITERAL(b));
				break;

			case kBCPushConstant:
				if (op == 0x27)
					b = (short) b;									// a signed 16-bit ref
				PUSH((Ref) b);
				break;

			case kBCCall:											// the global function named by the symbol on the stack
			{
				arg1 = POP();
				long exists;
				arg2 = UnsafeGetFrameSlot(gFunctionFrame, arg1, &exists);
				if (!exists)
					ThrowExInterpreterWithSymbol(kNSErrUndefinedGlobalFunction, arg1);
				if (fTraceLevel != 0)
					TraceCall(arg1, b);
				returned = !Call(arg2, b);
				break;
			}

			case kBCInvoke:											// the function on the stack
				arg1 = POP();
				if (fTraceLevel != 0)
					TraceApply(arg1, b);
				returned = !Call(arg1, b);
				break;

			case kBCSend:
			case kBCSendIfDefined:
			{
				if (fTraceLevel != 0)
				{
					RefVar message(fValueStack.fTop[-1]);
					RefVar receiver(fValueStack.fTop[-2]);
					TraceSend(receiver, message, b, 0);
				}
				arg1 = POP();										// the message
				arg2 = POP();										// the receiver
				if (XFindImplementor(arg2, arg1, &arg3, &arg4))
					returned = !Send(arg2, arg3, arg4, b);
				else if ((op >> 3) == kBCSend)
					ThrowExInterpreterWithSymbol(kNSErrUndefinedMethod, arg1);
				else
				{
					fValueStack.fTop -= b;
					PUSH(NILREF);
					if (fTraceLevel != 0)
						TraceReturn();
				}
				break;
			}

			case kBCResend:
			case kBCResendIfDefined:
			{
				if (fTraceLevel != 0)
				{
					RefVar message(fValueStack.fTop[-1]);
					TraceSend(StateVar(fVMState->fReceiver), message, b, 1);
				}
				arg1 = POP();
				if (XFindProtoImplementor(StateVar(fVMState->fImplementor), arg1, &arg2, &arg3))
					returned = !Send(StateVar(fVMState->fReceiver), arg2, arg3, b);
				else if ((op >> 3) == kBCResend)
					ThrowExInterpreterWithSymbol(kNSErrUndefinedMethod, arg1);
				else
				{
					fValueStack.fTop -= b;
					PUSH(NILREF);
					if (fTraceLevel != 0)
						TraceReturn();
				}
				break;
			}

			case kBCBranch:
				fPC = b;
				break;

			case kBCBranchIfTrue:
				if (POP() != NILREF)
					fPC = b;
				break;

			case kBCBranchIfFalse:
				if (POP() == NILREF)
					fPC = b;
				break;

			case kBCFindVar:										// a variable by name: locals, receiver's chain, globals
			{
				arg1 = LITERAL(b);
				long exists = 0;
				if (traceVars)
				{
					if (!fLocalsOnStack)
						arg2 = GetVariable(StateVar(fVMState->fLocals), arg1, &exists, 1);
					else if (StateRef(fVMState->fLocals) != NILREF)
						arg2 = GetVariable(StateVar(fVMState->fLocals), arg1, &exists, 1);
					else if (StateRef(fVMState->fReceiver) != NILREF)
						arg2 = GetVariable(StateVar(fVMState->fReceiver), arg1, &exists, 0);
					if (!exists)
					{
						RefVar vars(gVarFrame);
						TraceGet(vars, vars, arg1);
						arg2 = UnsafeGetFrameSlot(gVarFrame, arg1, &exists);
					}
				}
				else
				{
					if (!fLocalsOnStack || StateRef(fVMState->fLocals) != NILREF)
						arg2 = XGetVariable(StateVar(fVMState->fLocals), arg1, &exists, 1);
					else
						arg2 = XGetVariable(StateVar(fVMState->fReceiver), arg1, &exists, 0);
					if (!exists)
						arg2 = UnsafeGetFrameSlot(gVarFrame, arg1, &exists);
				}
				if (!exists)
					ThrowExInterpreterWithSymbol(kNSErrUndefinedVariable, arg1);
				PUSH((Ref) arg2);
				break;
			}

			case kBCGetVar:
				PUSH(LOCAL(b));
				break;

			case kBCMakeFrame:										// b values and a map on the stack
			{
				arg1 = POP();										// the map
				arg1 = AllocateFrameWithMap(arg1);
				ObjHeader* f = OBJ((Ref) arg1);
				for (long i = 0; i < b; i++)
					ObjArraySlots(f)[i] = fValueStack.fTop[i - b];
				fValueStack.fTop -= b;
				PUSH((Ref) arg1);
				break;
			}

			case kBCMakeArray:										// b values and a class; b == -1: a length and a class
			{
				arg1 = POP();										// the class
				if (b == 0xffff)
				{
					long length = RINT(POP());
					PUSH(AllocateArray(arg1, length));
				}
				else
				{
					arg1 = AllocateArray(arg1, b);
					ObjHeader* a = OBJ((Ref) arg1);
					for (long i = 0; i < b; i++)
						ObjArraySlots(a)[i] = fValueStack.fTop[i - b];
					fValueStack.fTop -= b;
					PUSH((Ref) arg1);
				}
				break;
			}

			case kBCGetPath:										// b == 0: nil for a nil object; else an error
				arg1 = POP();										// the path
				arg2 = POP();										// the object
				if ((Ref) arg2 == NILREF)
				{
					if (b == 0)
						PUSH(NILREF);
					else
						ThrowExFramesWithBadValue(kNSErrPathFailed, arg1);
				}
				else
				{
					if (traceVars)
						TraceGet(arg2, arg2, arg1);
					PUSH(GetFramePath(arg2, arg1));
				}
				break;

			case kBCSetPath:										// b == 1: the value stays on the stack
			{
				arg1 = POP();										// the value
				arg2 = POP();										// the path
				arg3 = POP();										// the object
				if (traceVars)
					TraceSet(arg3, arg3, arg2, arg1);
				// (1.x functions and ROM code set a frame's own slots only)
				if (!fLocalsOnStack || InROMObjectArea((Ref) fInstructions))
					SetFramePathFor1XFunctions(arg3, arg2, arg1);
				else
					SetFramePath(arg3, arg2, arg1);
				if (b != 0)
					PUSH((Ref) arg1);
				break;
			}

			case kBCSetVar:
				if (fLocalsOnStack)
					fValueStack.fBase[fLocalsIndex + b] = POP();
				else
				{
					Ref value = POP();
					ObjArraySlots(OBJ(StateRef(fVMState->fLocals)))[b] = value;
				}
				break;

			case kBCFindAndSetVar:									// set a variable found as find-var does; globals only where allowed
			{
				arg1 = LITERAL(b);
				arg2 = POP();
				long flags;
				Boolean isSend = (RVALUE(StateRef(fVMState->fStackFrame)) & kStackFrameIsSend) != 0;
				if (fLocalsOnStack && StateRef(fVMState->fLocals) == NILREF)
				{
					flags = isSend ? (kSetVarSetGlobal | kSetVarMakeGlobal) : kSetVarSetGlobal;
					if (!SetVariableOrGlobal(StateVar(fVMState->fReceiver), arg1, arg2, flags))
					{
						// no receiver to take it: the locals become a frame of their own
						arg3 = Clone(RefVar(Rcanonicalfakecontext));
						ObjArraySlots(OBJ((Ref) arg3))[kArgFrameParentSlot] = StateRef(fVMState->fReceiver);
						SetFrameSlot(arg3, arg1, arg2);
						StateRef(fVMState->fLocals) = arg3;
					}
				}
				else
				{
					flags = isSend ? (kSetVarLookupLocals | kSetVarSetGlobal | kSetVarMakeGlobal) : (kSetVarLookupLocals | kSetVarSetGlobal | kSetVarSetInContext);
					SetVariableOrGlobal(StateVar(fVMState->fLocals), arg1, arg2, flags);
				}
				break;
			}

			case kBCIncrVar:										// local b += top; the sum stays on the stack
			{
				Ref local = LOCAL(b);
				Ref incr = TOP();
				if (((local | incr) & 3) != 0)
				{
					if ((local & 3) != 0)
						_RINTError(local);
					if ((incr & 3) != 0)
						_RINTError(incr);
				}
				Ref sum = local + incr;
				if (fLocalsOnStack)
					fValueStack.fBase[fLocalsIndex + b] = sum;
				else
					ObjArraySlots(OBJ(StateRef(fVMState->fLocals)))[b] = sum;
				PUSH(sum);
				break;
			}

			case kBCBranchIfLoopNotDone:							// for loop: incr, limit, index on the stack
			{
				long incr = RINT(fValueStack.fTop[-1]);
				long limit = RINT(fValueStack.fTop[-2]);
				long index = RINT(fValueStack.fTop[-3]);
				fValueStack.fTop -= 3;
				if ((incr > 0 && index <= limit) || (incr < 0 && index >= limit))
					fPC = b;
				else if (incr == 0)
					Throw(exInterpreter, (void*) kNSErrZeroForLoopIncr, nil);
				break;
			}

			case kBCFreqFunc:
				switch (op)
				{
				case 0xc0:											// +
				{
					if (fTraceLevel != 0)
						TraceFreqCall(kFFAdd);
					Ref rb = POP();
					Ref ra = TOP();
					if (((ra | rb) & 3) == 0)
						TOP() = ra + rb;
					else
					{
						arg1 = ra;
						arg2 = rb;
						TOP() = NumberAdd(arg1, arg2);
					}
					if (fTraceLevel != 0)
						TraceReturn();
					break;
				}
				case 0xc1:											// -
				{
					if (fTraceLevel != 0)
						TraceFreqCall(kFFSubtract);
					Ref rb = POP();
					Ref ra = TOP();
					if (((ra | rb) & 3) == 0)
						TOP() = ra - rb;
					else
					{
						arg1 = ra;
						arg2 = rb;
						TOP() = NumberSubtract(arg1, arg2);
					}
					if (fTraceLevel != 0)
						TraceReturn();
					break;
				}
				case 0xc2:											// aref
				{
					if (fTraceLevel != 0)
						TraceFreqCall(kFFAref);
					arg2 = POP();									// the index
					arg1 = TOP();									// the array or string
					long index = RINT(arg2);
					ULong flags = ObjectFlags(arg1);
					if ((flags & kObjSlotted) == 0)
					{
						if (!IsInstance(arg1, RSSYMstring))
							ThrowBadTypeWithFrameData(kNSErrNotAnArrayOrString, arg1);
						// NOT YET RECONSTRUCTED: TRichString (ink in strings); the characters are the UniChars
						long length = Length(arg1) / 2 - 1;
						if (index < 0 || index >= length)
						{
							if (!fLocalsOnStack && index == length)
								TOP() = MAKECHAR(0);
							else
								ThrowOutOfBoundsException(arg1, index);
						}
						else
							TOP() = MAKECHAR(((UniChar*) BinaryData(arg1))[index]);
					}
					else if ((flags & kObjFrame) != 0)
						ThrowBadTypeWithFrameData(kNSErrNotAnArrayOrString, arg1);
					else
						TOP() = GetArraySlotRef(arg1, index);
					if (fTraceLevel != 0)
						TraceReturn();
					break;
				}
				case 0xc3:											// setAref
				{
					if (fTraceLevel != 0)
						TraceFreqCall(kFFSetAref);
					arg3 = POP();									// the value
					arg2 = POP();									// the index
					arg1 = TOP();									// the array or string
					long index = RINT(arg2);
					ULong flags = ObjectFlags(arg1);
					if ((flags & kObjSlotted) == 0)
					{
						if (!IsInstance(arg1, RSSYMstring))
							ThrowBadTypeWithFrameData(kNSErrNotAnArrayOrString, arg1);
						long length = Length(arg1) / 2 - 1;
						if (index < 0 || index >= length)
						{
							if (index != length || fLocalsOnStack || RCHAR(arg3) != 0)
								ThrowOutOfBoundsException(arg1, index);
							// (a 1.x function may store the terminating 0: the string shrinks to it)
							SetLength(arg1, (index + 1) * 2);
						}
						else
						{
							UniChar c = RCHAR(arg3);
							if (c == 0)
								SetLength(arg1, (index + 1) * 2);
							else
								((UniChar*) BinaryData(arg1))[index] = c;
						}
					}
					else if ((flags & kObjFrame) != 0)
						ThrowBadTypeWithFrameData(kNSErrNotAnArrayOrString, arg1);
					else
						SetArraySlotRef(arg1, index, arg3);
					TOP() = arg3;
					if (fTraceLevel != 0)
						TraceReturn();
					break;
				}
				case 0xc4:											// =
					if (fTraceLevel != 0)
						TraceFreqCall(kFFEquals);
					arg2 = POP();
					arg1 = TOP();
					TOP() = FreqEqual(arg1, arg2) ? TRUEREF : NILREF;
					if (fTraceLevel != 0)
						TraceReturn();
					break;
				case 0xc5:											// not
					if (fTraceLevel != 0)
						TraceFreqCall(kFFNot);
					TOP() = (TOP() == NILREF) ? TRUEREF : NILREF;
					if (fTraceLevel != 0)
						TraceReturn();
					break;
				case 0xc6:											// <>
					if (fTraceLevel != 0)
						TraceFreqCall(kFFNotEquals);
					arg2 = POP();
					arg1 = TOP();
					TOP() = FreqEqual(arg1, arg2) ? NILREF : TRUEREF;
					if (fTraceLevel != 0)
						TraceReturn();
					break;
				default:											// the others: the function objects of gFreqFuncs
					arg1 = GetArraySlotRef(gFreqFuncs, b);
					returned = !Call(arg1, gFreqFuncInfo[b].fNumArgs);
					break;
				}
				break;

			case kBCNewHandlers:									// b (symbol, pc) pairs on the stack
			{
				arg1 = AllocateArray(RSSYMarray, kHandlerSize);
				arg2 = AllocateArray(RSSYMarray, b * 2);
				ObjHeader* pairs = OBJ((Ref) arg2);
				for (long i = 0; i < b * 2; i++)
					ObjArraySlots(pairs)[i] = fValueStack.fTop[i - b * 2];
				fValueStack.fTop -= b * 2;
				ObjHeader* h = OBJ((Ref) arg1);
				ObjArraySlots(h)[kHandlerNext] = fExceptionContext;
				ObjArraySlots(h)[kHandlerValueDepth] = MAKEINT(fValueStack.Depth() - 1);
				ObjArraySlots(h)[kHandlerControlDepth] = MAKEINT(fCtrlStack.Depth() - 1);
				ObjArraySlots(h)[kHandlerFunction] = StateRef(fVMState->fFunction);
				ObjArraySlots(h)[kHandlerReceiver] = StateRef(fVMState->fReceiver);
				ObjArraySlots(h)[kHandlerImplementor] = StateRef(fVMState->fImplementor);
				ObjArraySlots(h)[kHandlerLocals] = StateRef(fVMState->fLocals);
				ObjArraySlots(h)[kHandlerExceptions] = arg2;
				fExceptionContext = (Ref) arg1;
				fExceptionStackIndex = fCtrlStack.Depth() - 1;
				break;
			}

			default:
				UndefinedBytecode();
			}
		} while (!returned);
		if (ControlPosition() < baseDepth)
			return true;
		if (fFastLoop)
			return false;
	}
	#undef PUSH
	#undef POP
	#undef TOP
	#undef LOCAL
	#undef LITERAL
}


// ROM 0x002c8848 FastRun__12TInterpreterFl
// NOT YET RECONSTRUCTED: FastRun1 (0x002c88d8), the open-coded copy of the
// loop; it computes what SlowRun does.
Boolean
TInterpreter::FastRun(long baseDepth)
{
	Boolean fastLoop = fFastLoop;
	fFastLoop = false;								// (so that SlowRun runs to the end)
	Boolean done = SlowRun(baseDepth);
	fFastLoop = fastLoop;
	return done;
}


// ROM 0x002cc4f4 AlternatingLoops__12TInterpreterFl
void
TInterpreter::AlternatingLoops(long baseDepth)
{
	if (!fFastLoop)
	{
		for (;;)
		{
			if (SlowRun(baseDepth))
				return;
			if (FastRun(baseDepth))
				return;
		}
	}
	for (;;)
	{
		if (FastRun(baseDepth))
			return;
		if (SlowRun(baseDepth))
			return;
	}
}


// ROM 0x002cc540 Run__12TInterpreterFv
// Run the current function until it returns to the control depth it was
// called at.  An exception nothing in NewtonScript handles resets the
// stacks to what they were and goes on to the C++ handlers.
void
TInterpreter::Run(void)
{
	TraceSetOptions();
	StackState* saved = GetStackStateBlock();
	StackState state;
	state.fControlDepth = saved->fControlDepth;
	state.fValueDepth = saved->fValueDepth;
	state.fHandlers = saved->fHandlers;
	state.fField64 = saved->fField64;
	DisposeStackStateBlock(saved);
	long baseDepth = ControlPosition();
	for (;;)
	{
		newton_try
		{
			IncrementCurrentStackPos();
			AlternatingLoops(baseDepth);
			DecrementCurrentStackPos();
		}
		newton_catch(exRootException)
		{
			DecrementCurrentStackPos();
			ClearRefHandles();
			if (!HandleException(&_info.exception, baseDepth, state))
				rethrow;
		}
		end_try;
		if (ControlPosition() < baseDepth)
			return;
	}
}


/* -------------------------------------------------------------------------------
	Exceptions
------------------------------------------------------------------------------- */

// ROM 0x002c7370 GetStackState__Fv
static void
GetStackState(StackState* state)
{
	state->fControlDepth = gInterpreter->ControlPosition();
	state->fValueDepth = gInterpreter->ValuePosition();
	state->fHandlers = gInterpreter->fExceptionContext;
	state->fField64 = gInterpreter->fField64;
}


// ROM 0x002cfc64 GetStackStateBlock__Fv
StackState*
GetStackStateBlock(void)
{
	StackState* state = new StackState;
	if (state == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	GetStackState(state);
	return state;
}


// ROM 0x002d0c08 DisposeStackStateBlock__FP10StackState
void
DisposeStackStateBlock(StackState* state)
{
	delete state;
}


// ROM 0x002ce810 ResetStack__FRC10StackState
void
ResetStack(const StackState& state)
{
	gInterpreter->fCtrlStack.Reset(state.fControlDepth);
	gInterpreter->fValueStack.Reset(state.fValueDepth);
	gInterpreter->fExceptionContext = state.fHandlers;
	gInterpreter->fField64 = state.fField64;
}


// ROM 0x002cfe00 PopHandlers__12TInterpreterFv
// Drop the handler records of control depths deeper than the current one.
void
TInterpreter::PopHandlers(void)
{
	while ((Ref) fExceptionContext != NILREF)
	{
		long depth = RINT(GetArraySlotRef(fExceptionContext, kHandlerControlDepth));
		if (depth <= ControlPosition())
			return;
		fExceptionContext = GetArraySlotRef(fExceptionContext, kHandlerNext);
	}
}


// ROM 0x002d001c TranslateException__12TInterpreterFP9Exception
// The frame a NewtonScript handler sees: {name, and error: the code, data:
// the ref, or message: the text}.
Ref
TInterpreter::TranslateException(Exception* exception)
{
	RefVar frame(AllocateFrame());
	SetFrameSlot(frame, RSSYMname, RefVar(Intern(exception->name)));
	if (Subexception(exception->name, (ExceptionName) "evt.ex.msg"))
		SetFrameSlot(frame, RSSYMmessage, RefVar(MakeString((const char*) exception->data)));
	else if (Subexception(exception->name, (ExceptionName) "type.ref"))
		SetFrameSlot(frame, RSSYMdata, *(RefStruct*) exception->data);
	else
		SetFrameSlot(frame, RSSYMerror, RefVar(MAKEINT((long) (Long) exception->data)));
	return frame;
}


// ROM 0x002d0138 ExceptionBeingHandled__12TInterpreterFv
Ref
TInterpreter::ExceptionBeingHandled(void)
{
	RefVar handler(fExceptionContext);
	RefVar exception;
	while ((Ref) handler != NILREF)
	{
		exception = GetArraySlotRef(handler, kHandlerException);
		if ((Ref) exception != NILREF)
			return exception;
		handler = GetArraySlotRef(handler, kHandlerNext);
	}
	return NILREF;
}


// ROM 0x002cfcf4 DeveloperNotified__FP9Exception
// NOT YET RECONSTRUCTED: the REP's record of exceptions already reported.
static Boolean
DeveloperNotified(Exception* /*exception*/)
{
	return true;
}


// ROM 0x002d0574 HandleException__12TInterpreterFP9ExceptionlR10StackState
// Look for a NewtonScript handler (a try in a function still on the
// control stack, at baseDepth or deeper) whose exception name covers this
// one: the stacks unwind to it, its frame's state is restored and the
// function resumes at the handler's pc with the exception translated into
// the handler record; answers whether one was found - else the stacks are
// reset to the state Run started with.
Boolean
TInterpreter::HandleException(Exception* exception, long baseDepth, StackState& state)
{
	ExceptionName name = exception->name;
	if (!DeveloperNotified(exception) && GetFrameSlotRef(gVarFrame, RSSYMbreakonthrows) != NILREF)
	{
		// NOT YET RECONSTRUCTED: POutTranslator::ExceptionNotify(gREPout) and the BreakLoop
	}
	RefVar handler(fExceptionContext);
	RefVar exceptions;
	while ((Ref) handler != NILREF)
	{
		long depth = RINT(GetArraySlotRef(handler, kHandlerControlDepth));
		if (depth < baseDepth)
			break;
		if (GetArraySlotRef(handler, kHandlerException) == NILREF)
		{
			exceptions = GetArraySlotRef(handler, kHandlerExceptions);
			long count = Length(exceptions);
			for (long i = 0; i < count; i += 2)
			{
				Ref sym = GetArraySlotRef(exceptions, i);
				if (Subexception(name, (ExceptionName) SymbolName(sym)))
				{
					long valueDepth = RINT(GetArraySlotRef(handler, kHandlerValueDepth));
					fValueStack.fTop -= ValuePosition() - valueDepth;
					fCtrlStack.fTop -= ControlPosition() - depth;
					fVMState = fCtrlStack.StateAt(ControlPosition() / kVMStateSize);
					StateRef(fVMState->fFunction) = GetArraySlotRef(handler, kHandlerFunction);
					fPC = RINT(GetArraySlotRef(exceptions, i + 1));
					StateRef(fVMState->fReceiver) = GetArraySlotRef(handler, kHandlerReceiver);
					StateRef(fVMState->fImplementor) = GetArraySlotRef(handler, kHandlerImplementor);
					StateRef(fVMState->fLocals) = GetArraySlotRef(handler, kHandlerLocals);
					SetFlags();
					RefVar translated(TranslateException(exception));
					SetArraySlotRef(handler, kHandlerException, translated);
					fExceptionContext = handler;
					return true;
				}
			}
		}
		handler = GetArraySlotRef(handler, kHandlerNext);
	}
	ResetStack(state);
	fVMState = fCtrlStack.PrevState();
	fPC = RVALUE(StateRef(fVMState->fPC));
	if (ControlPosition() < fExceptionStackIndex)
		PopHandlers();
	if (StateRef(fVMState->fFunction) != NILREF && fPC != -1)
		SetFlags();
	return false;
}


/* -------------------------------------------------------------------------------
	Sends set up from C++
------------------------------------------------------------------------------- */

// ROM 0x002d0ba8 SetupSend__FRC6RefVarT1lR6RefVar
// The function for a message to receiver, and its implementor; NILREF
// when there is none and ifDefined, else an error.
Ref
SetupSend(RefArg receiver, RefArg message, long ifDefined, RefVar& implementor)
{
	implementor = FindImplementor(receiver, message);
	if ((Ref) implementor == NILREF)
	{
		if (ifDefined)
			return NILREF;
		ThrowExInterpreterWithSymbol(kNSErrUndefinedMethod, message);
	}
	return GetFrameSlotRef(implementor, message);
}


// ROM 0x002d0c30 SetupResend__FRC6RefVarlR6RefVar
// The same for a resend: from the _proto of the implementor given.
Ref
SetupResend(RefArg message, long ifDefined, RefVar& implementor)
{
	implementor = GetFrameSlotRef(implementor, RSSYM_proto);
	if ((Ref) implementor == NILREF)
	{
		if (ifDefined)
			return NILREF;
		ThrowExInterpreterWithSymbol(kNSErrNoProtoForResend, message);
	}
	implementor = FindProtoImplementor(implementor, message);
	if ((Ref) implementor == NILREF)
	{
		if (ifDefined)
			return NILREF;
		ThrowExInterpreterWithSymbol(kNSErrUndefinedMethod, message);
	}
	return GetFrameSlotRef(implementor, message);
}


// ROM 0x002d0ed0 SetLexScope__FRC6RefVarN31
// A closure: fn with its argFrame's _nextArgFrame, _parent and
// _implementor set (an argFrame of nils becomes none).
Ref
SetLexScope(RefArg fn, RefArg locals, RefArg receiver, RefArg implementor)
{
	RefVar argFrame(GetArraySlotRef(fn, kFunctionArgFrameSlot));
	if ((Ref) argFrame == NILREF)
		return fn;
	argFrame = Clone(argFrame);
	ObjHeader* af = OBJ((Ref) argFrame);
	ObjArraySlots(af)[kArgFrameNextArgFrameSlot] = locals;
	if (ObjArraySlots(af)[kArgFrameParentSlot] == 0)
		ObjArraySlots(af)[kArgFrameParentSlot] = NILREF;
	else
		ObjArraySlots(af)[kArgFrameParentSlot] = receiver;
	if (ObjArraySlots(af)[kArgFrameImplementorSlot] == 0)
		ObjArraySlots(af)[kArgFrameImplementorSlot] = NILREF;
	else
		ObjArraySlots(af)[kArgFrameImplementorSlot] = implementor;
	if (Length(argFrame) == 3 && ObjArraySlots(af)[0] == NILREF && ObjArraySlots(af)[1] == NILREF && ObjArraySlots(af)[2] == NILREF)
		argFrame = NILREF;
	RefVar closure(Clone(fn));
	ObjArraySlots(OBJ((Ref) closure))[kFunctionArgFrameSlot] = argFrame;
	return closure;
}


/* -------------------------------------------------------------------------------
	The current call, seen from natives
------------------------------------------------------------------------------- */

// ROM 0x002aef4c GetLocalFromStack__12TInterpreterFRC6RefVarT1
// NOT YET RECONSTRUCTED: the debugger's access to a frame's locals by name.
Ref
TInterpreter::GetLocalFromStack(RefArg /*frameIndex*/, RefArg /*name*/)
{
	return NILREF;
}


// ROM 0x002af000 SetLocalOnStack__12TInterpreterFRC6RefVarN21
void
TInterpreter::SetLocalOnStack(RefArg /*frameIndex*/, RefArg /*name*/, RefArg /*value*/)
{ }


// ROM 0x002af0b8 GetSelfFromStack__12TInterpreterFRC6RefVar
Ref
TInterpreter::GetSelfFromStack(RefArg /*frameIndex*/)
{
	return NILREF;
}


// ROM 0x002aef38 StackTrace__12TInterpreterFv
// NOT YET RECONSTRUCTED: gREPout's StackTrace.
void
TInterpreter::StackTrace(void)
{ }


// ROM 0x002cc10c StackTrace__Fv
void
StackTrace(void)
{
	gInterpreter->StackTrace();
}


// Tracing and breakpoints print through the REP's translator, which is
// not reconstructed yet (TInterpreter::Trace... at 0x00333560-0x00334000,
// HandleBreakPoints 0x002af1e0).  NOT YET RECONSTRUCTED.
void TInterpreter::HandleBreakPoints(void) { }
void TInterpreter::SetBreakPoints(RefArg breakPoints) { gFramesBreakPoints = breakPoints; }
void TInterpreter::EnableBreakPoints(Boolean enable) { gFramesBreakPointsEnabled = enable; }
void TInterpreter::TraceSetOptions(void) { }
void TInterpreter::TraceGet(RefArg, RefArg, RefArg) { }
void TInterpreter::TraceSet(RefArg, RefArg, RefArg, RefArg) { }
void TInterpreter::TraceCall(RefArg, long) { }
void TInterpreter::TraceApply(RefArg, long) { }
void TInterpreter::TraceSend(RefArg, RefArg, long, long) { }
void TInterpreter::TraceFreqCall(long) { }
void TInterpreter::TraceReturn(void) { }


/* -------------------------------------------------------------------------------
	Calling NewtonScript from C++
	Each pushes its arguments, calls, runs the interpreter to the return and
	pops the result; an exception unwinds the control stack to the state
	the call was made in and goes on.
------------------------------------------------------------------------------- */

// the run of a call made from C++, unwinding the states on an exception
static Ref
RunCall(RefArg fn, long numArgs, Boolean isSend, RefArg receiver, RefArg implementor)
{
	VMState* saved = gInterpreter->fVMState;
	newton_try
	{
		Boolean done = isSend ? gInterpreter->Send(receiver, implementor, fn, numArgs) : gInterpreter->Call(fn, numArgs);
		if (!done)
			gInterpreter->Run();
	}
	newton_catch_all
	{
		while (gInterpreter->fVMState != saved)
			gInterpreter->fVMState = gInterpreter->fCtrlStack.PrevState();
		rethrow;
	}
	end_try;
	return gInterpreter->PopValue();
}


// ROM 0x002ca0c4 InterpretBlock__FRC6RefVarT1
Ref
InterpretBlock(RefArg fn, RefArg receiver)
{
	gInterpreter->TopLevelCall(fn, receiver);
	gInterpreter->Run();
	return gInterpreter->PopValue();
}


// ROM 0x002ca0fc PushArgArray__FRC6RefVar
long
PushArgArray(RefArg args)
{
	if ((Ref) args == NILREF)
		return 0;
	long count = Length(args);
	for (long i = 0; i < count; i++)
	{
		RefVar arg(Slots(args)[i]);
		gInterpreter->PushValue(arg);
	}
	return count;
}


// ROM 0x002ca5d4 DoCall__FRC6RefVarl
Ref
DoCall(RefArg fn, long numArgs)
{
	if (!IsFunction(fn))
		ThrowBadTypeWithFrameData(kNSErrNotAFunction, fn);
	if (gInterpreter->fTraceLevel != 0)
		gInterpreter->TraceApply(fn, numArgs);
	return RunCall(fn, numArgs, false, fn, fn);
}


// ROM 0x002cad28 DoSend__FRC6RefVarN21l
Ref
DoSend(RefArg receiver, RefArg implementor, RefArg message, long numArgs)
{
	if (gInterpreter->fTraceLevel != 0)
		gInterpreter->TraceSend(receiver, message, numArgs, 0);
	if ((Ref) implementor == NILREF)
		ThrowExInterpreterWithSymbol(kNSErrUndefinedMethod, message);
	RefVar fn(GetFrameSlotRef(implementor, message));
	return RunCall(fn, numArgs, true, receiver, implementor);
}


// ROM 0x002cc418 DoBlock__FRC6RefVarT1
Ref
DoBlock(RefArg fn, RefArg args)
{
	if (!IsFunction(fn))
		ThrowBadTypeWithFrameData(kNSErrNotAFunction, fn);
	long numArgs = PushArgArray(args);
	if (gInterpreter->fTraceLevel != 0)
		gInterpreter->TraceApply(fn, numArgs);
	return RunCall(fn, numArgs, false, fn, fn);
}


// ROM 0x002cc28c DoScript__FRC6RefVarN21
// fn as a method of receiver.
Ref
DoScript(RefArg receiver, RefArg fn, RefArg args)
{
	if (!IsFunction(fn))
		ThrowBadTypeWithFrameData(kNSErrNotAFunction, fn);
	long numArgs = PushArgArray(args);
	if (gInterpreter->fTraceLevel != 0)
		gInterpreter->TraceSend(receiver, RefVar(NILREF), numArgs, 0);
	return RunCall(fn, numArgs, true, receiver, receiver);
}


// ROM 0x002cb5cc DoMessage__FRC6RefVarN21
Ref
DoMessage(RefArg receiver, RefArg message, RefArg args)
{
	if (!IsSymbol(message))
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, message);
	RefVar implementor(FindImplementor(receiver, message));
	long numArgs = PushArgArray(args);
	return DoSend(receiver, implementor, message, numArgs);
}


// ROM 0x002cc11c DoMessageIfDefined__FRC6RefVarN21Pl
Ref
DoMessageIfDefined(RefArg receiver, RefArg message, RefArg args, long* defined)
{
	if (!IsSymbol(message))
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, message);
	RefVar implementor(FindImplementor(receiver, message));
	if ((Ref) implementor == NILREF)
	{
		if (defined != nil)
			*defined = 0;
		return NILREF;
	}
	if (defined != nil)
		*defined = 1;
	long numArgs = PushArgArray(args);
	return DoSend(receiver, implementor, message, numArgs);
}


// ROM 0x002cbdec DoProtoMessage__FRC6RefVarN21
Ref
DoProtoMessage(RefArg receiver, RefArg message, RefArg args)
{
	if (!IsSymbol(message))
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, message);
	RefVar implementor(FindProtoImplementor(receiver, message));
	long numArgs = PushArgArray(args);
	return DoSend(receiver, implementor, message, numArgs);
}


// ROM 0x002cc1d4 DoProtoMessageIfDefined__FRC6RefVarN21Pl
Ref
DoProtoMessageIfDefined(RefArg receiver, RefArg message, RefArg args, long* defined)
{
	if (!IsSymbol(message))
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, message);
	RefVar implementor(FindProtoImplementor(receiver, message));
	if ((Ref) implementor == NILREF)
	{
		if (defined != nil)
			*defined = 0;
		return NILREF;
	}
	if (defined != nil)
		*defined = 1;
	long numArgs = PushArgArray(args);
	return DoSend(receiver, implementor, message, numArgs);
}


// ROM 0x002ca188 NSCall__FRC6RefVar
Ref
NSCall(RefArg fn)
{
	if (!IsFunction(fn))
		ThrowBadTypeWithFrameData(kNSErrNotAFunction, fn);
	return DoCall(fn, 0);
}


// ROM 0x002ca1c0 NSCall__FRC6RefVarT1
Ref
NSCall(RefArg fn, RefArg a1)
{
	if (!IsFunction(fn))
		ThrowBadTypeWithFrameData(kNSErrNotAFunction, fn);
	gInterpreter->PushValue(a1);
	return DoCall(fn, 1);
}


// ROM 0x002ca210 NSCall__FRC6RefVarN21
Ref
NSCall(RefArg fn, RefArg a1, RefArg a2)
{
	if (!IsFunction(fn))
		ThrowBadTypeWithFrameData(kNSErrNotAFunction, fn);
	gInterpreter->PushValue(a1);
	gInterpreter->PushValue(a2);
	return DoCall(fn, 2);
}


// ROM 0x002ca270 NSCall__FRC6RefVarN31
Ref
NSCall(RefArg fn, RefArg a1, RefArg a2, RefArg a3)
{
	if (!IsFunction(fn))
		ThrowBadTypeWithFrameData(kNSErrNotAFunction, fn);
	gInterpreter->PushValue(a1);
	gInterpreter->PushValue(a2);
	gInterpreter->PushValue(a3);
	return DoCall(fn, 3);
}


// ROM 0x002ca2e0 NSCall__FRC6RefVarN41
Ref
NSCall(RefArg fn, RefArg a1, RefArg a2, RefArg a3, RefArg a4)
{
	if (!IsFunction(fn))
		ThrowBadTypeWithFrameData(kNSErrNotAFunction, fn);
	gInterpreter->PushValue(a1);
	gInterpreter->PushValue(a2);
	gInterpreter->PushValue(a3);
	gInterpreter->PushValue(a4);
	return DoCall(fn, 4);
}


// ROM 0x002ca360 NSCall__FRC6RefVarN51
Ref
NSCall(RefArg fn, RefArg a1, RefArg a2, RefArg a3, RefArg a4, RefArg a5)
{
	if (!IsFunction(fn))
		ThrowBadTypeWithFrameData(kNSErrNotAFunction, fn);
	gInterpreter->PushValue(a1);
	gInterpreter->PushValue(a2);
	gInterpreter->PushValue(a3);
	gInterpreter->PushValue(a4);
	gInterpreter->PushValue(a5);
	return DoCall(fn, 5);
}


// ROM 0x002ca3f0 NSCall__FRC6RefVarN61
Ref
NSCall(RefArg fn, RefArg a1, RefArg a2, RefArg a3, RefArg a4, RefArg a5, RefArg a6)
{
	if (!IsFunction(fn))
		ThrowBadTypeWithFrameData(kNSErrNotAFunction, fn);
	gInterpreter->PushValue(a1);
	gInterpreter->PushValue(a2);
	gInterpreter->PushValue(a3);
	gInterpreter->PushValue(a4);
	gInterpreter->PushValue(a5);
	gInterpreter->PushValue(a6);
	return DoCall(fn, 6);
}


// ROM 0x002ca490 NSCallWithArgArray__FRC6RefVarT1
Ref
NSCallWithArgArray(RefArg fn, RefArg args)
{
	if (!IsFunction(fn))
		ThrowBadTypeWithFrameData(kNSErrNotAFunction, fn);
	long numArgs = PushArgArray(args);
	return DoCall(fn, numArgs);
}


// ROM 0x002ca4d4 NSSend__FRC6RefVarT1
Ref
NSSend(RefArg receiver, RefArg message)
{
	if (!IsSymbol(message))
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, message);
	RefVar implementor(FindImplementor(receiver, message));
	return DoSend(receiver, implementor, message, 0);
}


// ROM 0x002ca548 NSSend__FRC6RefVarN21
Ref
NSSend(RefArg receiver, RefArg message, RefArg a1)
{
	if (!IsSymbol(message))
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, message);
	RefVar implementor(FindImplementor(receiver, message));
	gInterpreter->PushValue(a1);
	return DoSend(receiver, implementor, message, 1);
}


// ROM 0x002ca6b4 NSSend__FRC6RefVarN31
Ref
NSSend(RefArg receiver, RefArg message, RefArg a1, RefArg a2)
{
	if (!IsSymbol(message))
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, message);
	RefVar implementor(FindImplementor(receiver, message));
	gInterpreter->PushValue(a1);
	gInterpreter->PushValue(a2);
	return DoSend(receiver, implementor, message, 2);
}


// ROM 0x002ca750 NSSend__FRC6RefVarN41
Ref
NSSend(RefArg receiver, RefArg message, RefArg a1, RefArg a2, RefArg a3)
{
	if (!IsSymbol(message))
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, message);
	RefVar implementor(FindImplementor(receiver, message));
	gInterpreter->PushValue(a1);
	gInterpreter->PushValue(a2);
	gInterpreter->PushValue(a3);
	return DoSend(receiver, implementor, message, 3);
}


// ROM 0x002caa60 NSSendWithArgArray__FRC6RefVarN21
Ref
NSSendWithArgArray(RefArg receiver, RefArg message, RefArg args)
{
	if (!IsSymbol(message))
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, message);
	RefVar implementor(FindImplementor(receiver, message));
	long numArgs = PushArgArray(args);
	return DoSend(receiver, implementor, message, numArgs);
}


// ROM 0x002cb154 NSSendIfDefined__FRC6RefVarT1
Ref
NSSendIfDefined(RefArg receiver, RefArg message)
{
	if (!IsSymbol(message))
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, message);
	RefVar implementor(FindImplementor(receiver, message));
	if ((Ref) implementor == NILREF)
		return NILREF;
	return DoSend(receiver, implementor, message, 0);
}


// ROM 0x002cb1e4 NSSendIfDefined__FRC6RefVarN21
Ref
NSSendIfDefined(RefArg receiver, RefArg message, RefArg a1)
{
	if (!IsSymbol(message))
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, message);
	RefVar implementor(FindImplementor(receiver, message));
	if ((Ref) implementor == NILREF)
		return NILREF;
	gInterpreter->PushValue(a1);
	return DoSend(receiver, implementor, message, 1);
}


// ROM 0x002cb28c NSSendIfDefined__FRC6RefVarN31
Ref
NSSendIfDefined(RefArg receiver, RefArg message, RefArg a1, RefArg a2)
{
	if (!IsSymbol(message))
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, message);
	RefVar implementor(FindImplementor(receiver, message));
	if ((Ref) implementor == NILREF)
		return NILREF;
	gInterpreter->PushValue(a1);
	gInterpreter->PushValue(a2);
	return DoSend(receiver, implementor, message, 2);
}


// ROM 0x002caae0 NSSendProto__FRC6RefVarT1
Ref
NSSendProto(RefArg receiver, RefArg message)
{
	if (!IsSymbol(message))
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, message);
	RefVar implementor(FindProtoImplementor(receiver, message));
	return DoSend(receiver, implementor, message, 0);
}


// ROM 0x002cab54 NSSendProto__FRC6RefVarN21
Ref
NSSendProto(RefArg receiver, RefArg message, RefArg a1)
{
	if (!IsSymbol(message))
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, message);
	RefVar implementor(FindProtoImplementor(receiver, message));
	gInterpreter->PushValue(a1);
	return DoSend(receiver, implementor, message, 1);
}


// ROM 0x002cb7e0 NSSendProtoIfDefined__FRC6RefVarT1
Ref
NSSendProtoIfDefined(RefArg receiver, RefArg message)
{
	if (!IsSymbol(message))
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, message);
	RefVar implementor(FindProtoImplementor(receiver, message));
	if ((Ref) implementor == NILREF)
		return NILREF;
	return DoSend(receiver, implementor, message, 0);
}


// ROM 0x002cb870 NSSendProtoIfDefined__FRC6RefVarN21
Ref
NSSendProtoIfDefined(RefArg receiver, RefArg message, RefArg a1)
{
	if (!IsSymbol(message))
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, message);
	RefVar implementor(FindProtoImplementor(receiver, message));
	if ((Ref) implementor == NILREF)
		return NILREF;
	gInterpreter->PushValue(a1);
	return DoSend(receiver, implementor, message, 1);
}


// ROM 0x002cbe6c NSGetGlobalFn__FRC6RefVar
Ref
NSGetGlobalFn(RefArg name)
{
	Ref fn = GetFrameSlotRef(gFunctionFrame, name);
	if (fn == NILREF)
		ThrowExInterpreterWithSymbol(kNSErrUndefinedGlobalFunction, name);
	return fn;
}


// ROM 0x002cbeb4 NSCallGlobalFn__FRC6RefVar
Ref
NSCallGlobalFn(RefArg name)
{
	RefVar fn(NSGetGlobalFn(name));
	return NSCall(fn);
}


// ROM 0x002cbeec NSCallGlobalFn__FRC6RefVarT1
Ref
NSCallGlobalFn(RefArg name, RefArg a1)
{
	RefVar fn(NSGetGlobalFn(name));
	return NSCall(fn, a1);
}


// ROM 0x002cbf2c NSCallGlobalFn__FRC6RefVarN21
Ref
NSCallGlobalFn(RefArg name, RefArg a1, RefArg a2)
{
	RefVar fn(NSGetGlobalFn(name));
	return NSCall(fn, a1, a2);
}


// ROM 0x002cbf74 NSCallGlobalFn__FRC6RefVarN31
Ref
NSCallGlobalFn(RefArg name, RefArg a1, RefArg a2, RefArg a3)
{
	RefVar fn(NSGetGlobalFn(name));
	return NSCall(fn, a1, a2, a3);
}


// ROM 0x002cc0cc NSCallGlobalFnWithArgArray__FRC6RefVarT1
Ref
NSCallGlobalFnWithArgArray(RefArg name, RefArg args)
{
	RefVar fn(NSGetGlobalFn(name));
	return NSCallWithArgArray(fn, args);
}


/* -------------------------------------------------------------------------------
	Start-up
------------------------------------------------------------------------------- */

// ROM 0x002901d8 InitFunctions__Fv
// The global function frame, and the prototypes of 1.x code blocks (ROM
// frames: NOT YET RECONSTRUCTED until named by the ROM constants).
void
InitFunctions(void)
{
	gFunctionFrame = AllocateFrame();
}


// ROM 0x002d0ff8 InitInterpreter__Fv
// The lookup caches, the prototype of native function objects (a map of
// class, funcPtr, numArgs and, when wanted, docString), the global
// function frame, the frequently called functions (from the built-in
// functions), the constants frame and the interpreter itself.
// Host: the ROM's native functions are the code its built-in functions
// frame points at; here their host implementations are bound first
// (RegisterBuiltinNatives) and, with no ROM objects imported, given
// function objects of their own (InstallHostNatives) so that the
// frequently called functions can be found.
// NOT YET RECONSTRUCTED: the task's stack limits for the debugger
// (GetTaskStackInfo into gNewtGlobals).
void
InitInterpreter(void)
{
	RegisterBuiltinNatives();
	InitICache();
	{
		RefVar tags(AllocateArray(RefVar(NILREF), gUseCFunctionDocStrings ? 4 : 3));
		SetArraySlotRef(tags, 0, RSSYMclass);
		SetArraySlotRef(tags, 1, RSSYMfuncptr);
		SetArraySlotRef(tags, 2, RSSYMnumargs);
		if (gUseCFunctionDocStrings)
			SetArraySlotRef(tags, 3, RSSYMdocstring);
		RefVar map(AllocateMapWithTags(RefVar(NILREF), tags));
		gCFunctionPrototype = AllocateFrameWithMap(map);
		AddGCRoot(gCFunctionPrototype);
		SetArraySlotRef(gCFunctionPrototype, 0, Intern((char*) "CFunction"));
	}
	InitFunctions();
	InstallHostNatives();
	AddGCRoot(gFreqFuncs);
	gFreqFuncs = AllocateArray(RSSYMarray, gNumFreqFuncs);
	{
		RefVar sym, fn;
		for (long i = 0; i < gNumFreqFuncs; i++)
		{
			sym = Intern((char*) gFreqFuncInfo[i].fName);
			fn = GetFrameSlotRef(gFunctionFrame, sym);
			SetArraySlotRef(gFreqFuncs, i, fn);
		}
	}
	gConstantsFrame = AllocateFrame();
	AddGCRoot(gConstantsFrame);
	gInterpreter = new TInterpreter;
	gInterpreter->fID = 0;
	gFramesBreakPointsEnabled = false;
	AddGCRoot(gFramesBreakPoints);
	gFramesBreakPoints = NILREF;
}


// ROM 0x002d0b20 GetGInterpreter__Fv
TInterpreter*
GetGInterpreter(void)
{
	return gInterpreter;
}


// ROM 0x002d0b30 GetGFunctionFrame__Fv
Ref
GetGFunctionFrame(void)
{
	return gFunctionFrame;
}


// ROM 0x002d15f0 GetCurrentInterpreterID__Fv
long
GetCurrentInterpreterID(void)
{
	return gInterpreter->fID;
}


// ROM 0x002d1510 GetTInterpreter__Fl
TInterpreter*
GetTInterpreter(long id)
{
	for (TInterpreter* i = gInterpreterList; i != nil; i = i->fNext)
		if (i->fID == id)
			return i;
	return nil;
}
