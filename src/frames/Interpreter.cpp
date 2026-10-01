/*
	File:		frames/Interpreter.cpp

	Contains:	The NewtonScript interpreter (Interpreter.h): the stacks, the
				VM states, calling and returning, the bytecode loop, exception
				handlers, and the entry points C++ uses to run NewtonScript.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	The ROM has two bytecode loops: SlowRun (0x002f1ee0), which handles
	tracing and breakpoints, and FastRun (0x002ee0a8), a copy of it with the
	instruction pointer and stack pointer held in registers and the common
	cases open-coded, chosen while nothing needs SlowRun (fFastLoop).  The
	two compute the same (FastRun1 and its Fast... helpers, 0x002ecca0-
	0x002ef8a8, follow SlowRun).

	Two things of the ROM's are not a host's: native functions in binary
	objects (ARM code) cannot be run, and the frames function profiler
	(gFramesFunctionProfiler) is not reconstructed.
*/

#include "Interpreter.h"
#include "PackageNatives.h"
#include "NativeFunctions.h"
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "NSErrors.h"
#include "OSErrors.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"
#include "RichString.h"		// aref/setAref read strings through TRichString

#include "host/TaskRuntime.h"

#include "REPTranslators.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ULong	gInterpreterPreemptCount = 0;	// the bytecodes between preemption points

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

// ROM 0x001a49dc TRefStackMark__FPv
static void
TRefStackMark(void* stack)
{
	TRefStack* s = (TRefStack*) stack;
	for (Ref* p = s->fBase; p < s->fTop; p++)
		DIYGCMark(*p);
}


// ROM 0x001a4a10 TRefStackUpdate__FPv
static void
TRefStackUpdate(void* stack)
{
	TRefStack* s = (TRefStack*) stack;
	for (Ref* p = s->fBase; p < s->fTop; p++)
		*p = DIYGCUpdate(*p);
}


// ROM 0x001a48e4 __ct__9TRefStackFv
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


// ROM 0x001a49a8 __dt__9TRefStackFv
TRefStack::~TRefStack()
{
	DIYGCUnregister(this);
	DisposPtr((Ptr) fBase);
}


// ROM 0x001a461c Reset__9TRefStackFl
// Back to depth refs (never up).
void
TRefStack::Reset(long depth)
{
	long current = Depth() - 1;
	if (current <= depth)
		return;
	fTop -= current - depth;
}


// ROM 0x001a4660 PushNILs__9TRefStackFl
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

// ROM 0x001a4a78 __ct__15TRefStructStackFv
TRefStructStack::TRefStructStack()
{
	fHandles = (RefHandle**) NewPtr(kRefStackRefs * sizeof(RefHandle*));
	if (fHandles == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	fHandlesEnd = fHandles;
}


// ROM 0x001a4b18 __dt__15TRefStructStackFv
TRefStructStack::~TRefStructStack()
{
	DisposPtr((Ptr) fHandles);
}


// ROM 0x001a4b54 Fill__15TRefStructStackFv
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


// ROM 0x001a46f0 NewState__11TIntrpStackFv
VMState*
TIntrpStack::NewState(void)
{
	PushNILs(kVMStateSize);
	return TopState(this);
}


// ROM 0x001a477c DupState__11TIntrpStackFv
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


// ROM 0x001a4820 PrevState__11TIntrpStackFv
VMState*
TIntrpStack::PrevState(void)
{
	fTop -= kVMStateSize;
	return TopState(this);
}


// ROM 0x001a4890 StateAt__11TIntrpStackFl
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

// ROM 0x002f6e48 IsFunction__FRC6RefVar
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


// ROM 0x002d4270 IsNativeFunction__FRC6RefVar
Boolean
IsNativeFunction(RefArg fn)
{
	Ref c = GetArraySlotRef(fn, kFunctionClassSlot);
	return c == kNativeFuncClass || c == kBinaryNativeFuncClass || EQRef(c, RSSYMbincfunction);
}


// ROM 0x002f6f20 GetFunctionArgCount__FRC6RefVar
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


// ROM 0x002f6d8c MakeCFunction__FPFRC6RefVare_llPc
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


// ROM 0x002f6558 NativeEntry__FRC6RefVarlPP9RefHandle
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
	{
		// Host: a function whose body is ARM code in a binary object - what
		// NTK calls a native function.  There is nothing to call on another
		// machine, so it has to be reconstructed by hand; say which one it
		// is rather than throwing a bare code.
		fprintf(stderr, "[frames] a native (ARM) function (class %08lx)%s",
				(unsigned long) c, "\n");
		if (getenv("NEWTON_TRACE_MISSING") != nil && gREPout != nil)
			StackTrace();
		Throw(exInterpreter, (void*) kNSErrNativeNotReconstructed, nil);
	}
	return nil;
}


/* -------------------------------------------------------------------------------
	TInterpreter
------------------------------------------------------------------------------- */

// ROM 0x002f40e0 __ct__12TInterpreterFv
TInterpreter::TInterpreter()
{
	fNext = gInterpreterList;
	gInterpreterList = this;
	fTraceDepth = 0;
	fTraceIndent = 0;
	fTraceLevel = 0;
	fTraceVariables = 0;
	fTracePrintCalls = 0;
	fExceptionStackIndex = 0;
	fVMState = fCtrlStack.NewState();
	StateRef(fVMState->fPC) = MAKEINT(0);
	SetFastLoopFlag();
}


// ROM 0x002f41cc __dt__12TInterpreterFv
TInterpreter::~TInterpreter()
{
	for (TInterpreter** link = &gInterpreterList; *link != nil; link = &(*link)->fNext)
		if (*link == this)
		{
			*link = fNext;
			break;
		}
}


// ROM 0x002f4274 PushValue__12TInterpreterFRC6RefVar
void
TInterpreter::PushValue(RefArg value)
{
	if (fValueStack.fTop >= fValueStack.fLimit)			// DEVIATION: see TRefStack::PushNILs
		Throw(exOutOfStack, nil, nil);
	*fValueStack.fTop++ = value;
}


// ROM 0x002f428c PopValue__12TInterpreterFv
Ref
TInterpreter::PopValue(void)
{
	return *--fValueStack.fTop;
}


// ROM 0x002f42a0 PeekValue__12TInterpreterFl
Ref
TInterpreter::PeekValue(long depth)
{
	return fValueStack.fTop[-1 - depth];
}


// ROM 0x002f42b4 SetValue__12TInterpreterFlT1
void
TInterpreter::SetValue(long depth, Ref value)
{
	fValueStack.fTop[-1 - depth] = value;
}


// ROM 0x002f42c8 ValuePosition__12TInterpreterFv
long
TInterpreter::ValuePosition(void)
{
	return fValueStack.Depth() - 1;
}


// ROM 0x002f42e4 PeekControl__12TInterpreterFl
Ref
TInterpreter::PeekControl(long depth)
{
	return fCtrlStack.fTop[-1 - depth];
}


// ROM 0x002f42f8 SetControl__12TInterpreterFlT1
void
TInterpreter::SetControl(long depth, Ref value)
{
	fCtrlStack.fTop[-1 - depth] = value;
}


// ROM 0x002f430c ControlPosition__12TInterpreterFv
long
TInterpreter::ControlPosition(void)
{
	return fCtrlStack.Depth() - 1;
}


// ROM 0x002f63b4 GetReceiver__12TInterpreterFv
Ref
TInterpreter::GetReceiver(void)
{
	return StateRef(fVMState->fReceiver);
}


// ROM 0x002f63c4 GetImplementor__12TInterpreterFv
Ref
TInterpreter::GetImplementor(void)
{
	return StateRef(fVMState->fImplementor);
}


// ROM 0x002f63d4 IsSend__12TInterpreterFv
Boolean
TInterpreter::IsSend(void)
{
	return fIsSend;
}


// ROM 0x002f63dc SetCallEnv__12TInterpreterFv
void
TInterpreter::SetCallEnv(void)
{
	fIsSend = false;
}


// ROM 0x002f63e8 SetSendEnv__12TInterpreterFRC6RefVarT1
void
TInterpreter::SetSendEnv(RefArg receiver, RefArg implementor)
{
	StateRef(fVMState->fReceiver) = receiver;
	StateRef(fVMState->fImplementor) = implementor;
	fIsSend = true;
}


// ROM 0x002f1cec SetFastLoopFlag__12TInterpreterFv
// FastRun may run when nothing wants SlowRun: no stack-trace accuracy,
// tracing, breakpoints or profiling, and instructions that stay put.
void
TInterpreter::SetFastLoopFlag(void)
{
	fFastLoop = gAccurateStackTrace == 0 && fTraceLevel == 0 && !gFramesBreakPointsEnabled && fInstructionsFixed;
}


// ROM 0x002f543c SetFlags__12TInterpreterFv
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


// ROM 0x002f4328 TopLevelCall__12TInterpreterFRC6RefVarT1
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


// ROM 0x002f443c Call__12TInterpreterFRC6RefVarl
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


// ROM 0x002f477c Send__12TInterpreterFRC6RefVarN21l
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


// ROM 0x002f4a54 CallCodeBlock__12TInterpreterFRC6RefVarlT2
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


// ROM 0x002f4c04 CallPlainCodeBlock__12TInterpreterFRC6RefVarlT2
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


// ROM 0x002f4da8 CallCFunction__12TInterpreterFRC6RefVarli
// A native function with its code in a binary - a 0x232 array [class,
// code, numArgs, closure, offset] or (isFrame) a binCFunction frame: the
// argument count checked, the closure (if any) pushed as one argument
// more, the code called with the receiver and the arguments, and its
// result replacing them on the stack.
// DEVIATION: the code is ARM, which the host cannot call: a host
// re-expression registered for it runs instead, else the fallback (an ARM
// interpreter), else it throws - frames/PackageNatives.h.
void
TInterpreter::CallCFunction(RefArg fn, long numArgs, int isFrame)
{
	StateRef(fVMState->fFunction) = fn;
	fPC = -1;
	StateRef(fVMState->fPC) = MAKEINT(-1);
	RefVar closure;
	RefVar code;
	long expected;
	ULong offset;
	if (!isFrame)
	{
		Ref* slots = ObjArraySlots(NoFaultObjectPtr(fn));
		expected = RVALUE(slots[2]);
		closure = slots[3];
		code = slots[1];
		offset = RVALUE(slots[4]);
	}
	else
	{
		expected = RINT(GetFrameSlotRef(fn, RSSYMnumargs));
		closure = GetFrameSlotRef(fn, RSSYMclosure);
		code = GetFrameSlotRef(fn, RSSYMcode);
		offset = RINT(GetFrameSlotRef(fn, RSSYMoffset));
	}
	if (expected != numArgs)
		Throw(exInterpreter, (void*) kNSErrWrongNumberOfArgs, nil);
	if (NOTNIL(closure))
	{
		numArgs++;
		PushValue(closure);
	}
	RefVar result(CallPackageNative(code, offset, numArgs));
	fValueStack.fTop -= numArgs;
	PushValue(result);
	if (fTraceLevel != 0)
		TraceReturn();
}


// Host: a package's native function called - its host re-expression, the
// fallback, or the error (frames/PackageNatives.h).
Ref
TInterpreter::CallPackageNative(RefArg code, ULong offset, long numArgs)
{
	long boundArgs = 0;
	PackageNativeKey key;
	void* fn = FindPackageNative(code, offset, &boundArgs, &key);
	if (fn != nil)
	{
		if (boundArgs != numArgs)
			Throw(exInterpreter, (void*) kNSErrWrongNumberOfArgs, nil);
		return CallCFuncPtr(fn, numArgs);
	}
	PackageNativeFallback fallback = GetPackageNativeFallback();
	if (fallback != nil)
	{
		if (fValueStack.fHandlesEnd - fValueStack.fHandles < fValueStack.Depth())
			fValueStack.Fill();
		long first = fValueStack.Depth() - numArgs;
		const RefVar* args[8];
		if (numArgs > 8)
			Throw(exInterpreter, (void*) kNSErrTooManyArgs, nil);
		for (long i = 0; i < numArgs; i++)
			args[i] = &fValueStack.StackRef(first + i);
		return fallback(code, offset, StateVar(fVMState->fReceiver), numArgs, args);
	}
	fprintf(stderr, "[frames] a package's native (ARM) function: code of %lu bytes, hash 0x%08lx, offset 0x%lx - no host re-expression and no fallback\n",
			(unsigned long) key.fCodeLength, (unsigned long) key.fCodeHash, (unsigned long) key.fOffset);
	if (getenv("NEWTON_TRACE_MISSING") != nil && gREPout != nil)
		StackTrace();
	Throw(exInterpreter, (void*) kNSErrNativeNotReconstructed, nil);
	return NILREF;
}


// ROM 0x002f4f8c CallPlainCFunction__12TInterpreterFRC6RefVarl
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


// The machine adds and subtracts two integer Refs as they stand - both
// have a tag of nought, so the words add - and the result wraps in the
// ARM's thirty-two bits, which is what makes a NewtonScript integer a
// thirty-bit one.  A Ref here is pointer-sized, so the sum is cut back to
// the word the machine would have had.  The callers add in unsigned
// arithmetic, so the sum wraps rather than overflows whatever the width.
//
// NEWTON_NS64 (docs/frames/64bit.md): an integer is as wide as a Ref, so
// the sum is kept whole - it wraps only at the Ref's own width, 2^62.
static inline Ref
WordRef(Ref value)
{
#if NEWTON_NS64
	return value;
#else
	return (Ref) (int) (ULong32) value;
#endif
}


// Host: a script that asks for a ROM native we have not written yet should
// say which one, so that the next thing to reconstruct is obvious rather
// than a bare error code.  The boot is full of these while it is being
// filled in, and this is how they are found.
//
// With NEWTON_TRACE_MISSING set in the environment it also prints the
// script stack that asked for it, which is how to tell which of the ROM's
// own functions wants the thing.
static void
NoteMissingNative(ULong funcPtr)
{
	const ROMNativeEntry* entry = ROMNativeAt((unsigned int) funcPtr);
	if (entry != nil)
		fprintf(stderr, "[frames] native not reconstructed: %s (ROM 0x%08x %s)\n",
			entry->fName, entry->fTarget, entry->fSymbol);
	else
		fprintf(stderr, "[frames] native not reconstructed: funcPtr 0x%08lx\n",
			(unsigned long) funcPtr);
	if (getenv("NEWTON_TRACE_MISSING") != nil && gREPout != nil)
		StackTrace();
}


// ROM 0x002f5124 CallCFuncPtr__12TInterpreterFPFRC6RefVare_ll
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
		{
			NoteMissingNative((ULong) funcPtr);
			Throw(exInterpreter, (void*) kNSErrNativeNotReconstructed, nil);
		}
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


// ROM 0x002f52c8 Return__12TInterpreterF19FramesProfilingKind
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

// ROM 0x002f1cd0 UndefinedBytecode__Fv
static void
UndefinedBytecode(void)
{
	Throw(exInterpreter, (void*) kNSErrUndefinedBytecode, nil);
}


// ROM 0x002ef144 ThrowOutOfBoundsException__FRC6RefVarl
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


// ROM 0x002f1ee0 SlowRun__12TInterpreterFl
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
			// DEVIATION: the host cannot interrupt a task at an arbitrary
			// instruction, so the loop offers the kernel a point to preempt
			// it at every so often (kernel/host/TaskRuntime.h).  A ROM
			// script that polls without ever waiting - the views' tracking
			// loops do - would otherwise keep the processor for good.
			if ((++gInterpreterPreemptCount & 0x3ff) == 0)
				HostPreemptionPoint();
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

			case kBCBranchIfLoopNotDone:							// for loop: incr, index, limit on the stack
			{
				long limit = RINT(fValueStack.fTop[-1]);
				long index = RINT(fValueStack.fTop[-2]);
				long incr = RINT(fValueStack.fTop[-3]);
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
						TOP() = WordRef((Ref) ((ULong) ra + (ULong) rb));
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
						TOP() = WordRef((Ref) ((ULong) ra - (ULong) rb));
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
						// read through TRichString (0x002f3628): a rich string's
						// bound is its text's length, not the binary's
						TRichString s(arg1);
						long length = s.Length();
						if (index < 0 || index >= length)
						{
							if (!fLocalsOnStack && index == length)
								TOP() = MAKECHAR(0);
							else
								ThrowOutOfBoundsException(arg1, index);
						}
						else
							TOP() = MAKECHAR(s.GetChar(index));
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
						// read and written through TRichString (0x002f3800): a
						// character goes in with SetChar (an ink word's through
						// MungeRange); a 0 cuts the string there with StrMunger -
						// which a 1.x function may do, storing the terminator -
						// unless the locals are on the stack; the ink character
						// itself may never be stored
						TRichString s(arg1);
						long length = s.Length();
						if (index < 0 || index >= length)
						{
							if (index != length || fLocalsOnStack || RCHAR(arg3) != 0)
								ThrowOutOfBoundsException(arg1, index);
							StrMunger(arg1, index, -1, RefVar(NILREF), 0, -1);
						}
						else
						{
							UniChar c = RCHAR(arg3);
							if (c != 0 && c != kInkChar)
								s.SetChar(index, c);
							else if (fLocalsOnStack || c != 0)
							{
								RefVar data(AllocateFrame());
								SetFrameSlot(data, RSSYMerrorcode, RefVar(MAKEINT(kNSErrBadCharForString)));
								SetFrameSlot(data, RSSYMvalue, arg3);
								ThrowRefException(exInterpreterWithFrameData, data);
							}
							else
								StrMunger(arg1, index, -1, RefVar(NILREF), 0, -1);
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


/* -------------------------------------------------------------------------------
	The fast loop
	FastRun1 is SlowRun with the instruction pointer and the value stack's top
	held in locals and the common instructions open-coded; everything else is
	a Fast... helper handed the loop's state, which the loop writes back before
	the call and reads again after it.  It runs only while nothing needs
	SlowRun (fFastLoop: no tracing, breakpoints or profiling, and instructions
	that cannot move - ROM or package code, or locked), which is why it may
	keep a pointer into the instructions and the literals across a call.
------------------------------------------------------------------------------- */

// the loop's state (the ROM's FastRunState, 0x24 bytes)
struct FastRunState
{
	const unsigned char*	fPC;			// +00  the next instruction
	TIntrpStack*			fStack;			// +04  the value stack (its fTop is the loop's sp)
	TInterpreter*			fInterpreter;	// +08
	RefVar					fArg1;			// +0C  four RefVars the helpers work in
	RefVar					fArg2;			// +10
	RefVar					fArg3;			// +14
	RefVar					fArg4;			// +18
	Ref*					fLiterals;		// +1C  the literals' slots (nil: none)
	const unsigned char*	fInstructions;	// +20  the instructions' first byte
};


// a function object the fast loop calls itself: one in the ROM or a
// package, whose class word and slots cannot change under it (the ROM's
// address test, below 0x03800000 or in 0x60000000-0x67ffffff; here the ROM
// object area, as for gROProtoCache)
static inline Boolean
IsFastFunction(Ref fn)
{
	return RTAG(fn) == kTagPointer && InROMObjectArea(fn);
}


// the operand of an instruction whose low bits are 7: a big-endian halfword
static inline long
FastOperand(const unsigned char* at)
{
	return ((long) at[0] << 8) | at[1];
}


// ROM 0x002ecfe8 FastDoCall__FP12FastRunStatelT2
// A function called with numArgs arguments on the stack.  A NewtonScript
// function or a C one in the ROM is called here: the first becomes the
// current function (answers true: the loop must take up the new state), the
// second is called without a VM state of its own, in the caller's (answers
// false: the loop goes on where it was).  Anything else goes through Call.
static Boolean
FastDoCall(FastRunState* state, Ref fn, long numArgs)
{
	TInterpreter* interp = state->fInterpreter;
	long pc = state->fPC - state->fInstructions;
	if (IsFastFunction(fn))
	{
		ObjHeader* f = OBJ(fn);
		Ref theClass = ObjArraySlots(f)[kFunctionClassSlot];
		if (theClass == kFuncClass)
		{
			StateRef(interp->fVMState->fPC) = MAKEINT(pc);
			VMState* callee = interp->fCtrlStack.NewState();
			interp->fVMState = callee;
			f = OBJ(fn);
			Ref numArgsWord = RVALUE(ObjArraySlots(f)[kFunctionNumArgsSlot]);
			StateRef(callee->fFunction) = fn;
			interp->fPC = 0;
			if (numArgs != (numArgsWord & 0xffff))
			{
				Throw(exInterpreter, (void*) kNSErrWrongNumberOfArgs, nil);
				return false;
			}
			TRefStack* stack = state->fStack;
			StateRef(callee->fStackFrame) = StackFrameWord(stack->Depth() - numArgs - 3, kStackFrameLocalsOnStack);
			Ref argFrame = ObjArraySlots(OBJ(fn))[kFunctionArgFrameSlot];
			if (argFrame != NILREF)
			{
				StateRef(callee->fLocals) = argFrame;
				StateRef(callee->fLocals) = Clone(StateVar(callee->fLocals));
				ObjHeader* l = OBJ(StateRef(callee->fLocals));
				StateRef(callee->fReceiver) = ObjArraySlots(l)[kArgFrameParentSlot];
				StateRef(callee->fImplementor) = ObjArraySlots(l)[kArgFrameImplementorSlot];
			}
			stack->PushNILs(numArgsWord >> 16);
			interp->SetFlags();
			return true;
		}
		if (theClass == kNativeFuncClass)
		{
			if (numArgs != RVALUE(ObjArraySlots(f)[kNativeNumArgsSlot]))
			{
				Throw(exInterpreter, (void*) kNSErrWrongNumberOfArgs, nil);
				return false;
			}
			interp->fIsSend = false;
			Ref result = interp->CallCFuncPtr((void*) ObjArraySlots(OBJ(fn))[kNativeFuncPtrSlot], numArgs);
			Ref* top = state->fStack->fTop - (numArgs - 1);
			top[-1] = result;
			state->fStack->fTop = top;
			interp->fPC = pc;
			interp->SetFlags();
			return false;
		}
	}
	interp->fPC = pc;
	state->fArg1 = fn;
	return !interp->Call(state->fArg1, numArgs);
}


// ROM 0x002ed218 FastDoSend__FP12FastRunStateRC6RefVarN22l
// The same for a message sent to receiver and found in implementor.  A C
// function in the ROM gets a VM state holding the receiver and implementor
// and nothing else, and is gone again when it returns.
static Boolean
FastDoSend(FastRunState* state, RefArg receiver, RefArg implementor, RefArg fn, long numArgs)
{
	TInterpreter* interp = state->fInterpreter;
	Ref f = fn;
	if (IsFastFunction(f))
	{
		Ref theClass = ObjArraySlots(OBJ(f))[kFunctionClassSlot];
		if (theClass == kFuncClass)
		{
			StateRef(interp->fVMState->fPC) = MAKEINT(state->fPC - state->fInstructions);
			VMState* callee = interp->fCtrlStack.NewState();
			interp->fVMState = callee;
			StateRef(callee->fReceiver) = receiver;
			StateRef(callee->fImplementor) = implementor;
			Ref numArgsWord = RVALUE(ObjArraySlots(OBJ(fn))[kFunctionNumArgsSlot]);
			StateRef(callee->fFunction) = fn;
			interp->fPC = 0;
			if (numArgs != (numArgsWord & 0xffff))
			{
				Throw(exInterpreter, (void*) kNSErrWrongNumberOfArgs, nil);
				return false;
			}
			TRefStack* stack = state->fStack;
			StateRef(callee->fStackFrame) = StackFrameWord(stack->Depth() - numArgs - 3, kStackFrameLocalsOnStack);
			Ref argFrame = ObjArraySlots(OBJ(fn))[kFunctionArgFrameSlot];
			if (argFrame != NILREF)
			{
				StateRef(callee->fLocals) = argFrame;
				StateRef(callee->fLocals) = Clone(StateVar(callee->fLocals));
				ObjHeader* l = OBJ(StateRef(callee->fLocals));
				if (ObjArraySlots(l)[kArgFrameParentSlot] == 0)
					ObjArraySlots(l)[kArgFrameParentSlot] = NILREF;
				else
					ObjArraySlots(l)[kArgFrameParentSlot] = StateRef(callee->fReceiver);
				ObjArraySlots(l)[kArgFrameImplementorSlot] = ObjArraySlots(l)[kArgFrameImplementorSlot] != 0
					? StateRef(callee->fImplementor) : NILREF;
			}
			stack->PushNILs(numArgsWord >> 16);
			interp->SetFlags();
			return true;
		}
		if (theClass == kNativeFuncClass)
		{
			if (numArgs == RVALUE(ObjArraySlots(OBJ(fn))[kNativeNumArgsSlot]))
			{
				VMState* callee = interp->fCtrlStack.NewState();
				interp->fVMState = callee;
				interp->fIsSend = true;
				StateRef(callee->fReceiver) = receiver;
				StateRef(callee->fImplementor) = implementor;
				Ref result = interp->CallCFuncPtr((void*) ObjArraySlots(OBJ(fn))[kNativeFuncPtrSlot], numArgs);
				Ref* top = state->fStack->fTop - (numArgs - 1);
				top[-1] = result;
				state->fStack->fTop = top;
				interp->fVMState = interp->fCtrlStack.PrevState();
				interp->SetFlags();
			}
			else
				Throw(exInterpreter, (void*) kNSErrWrongNumberOfArgs, nil);
			return false;
		}
	}
	interp->fPC = state->fPC - state->fInstructions;
	return !interp->Send(receiver, implementor, fn, numArgs);
}


// ROM 0x002ed4ec FastInvoke__FP12FastRunStatel
// call: the function on the stack.  (The ROM's copy of FastDoCall's body
// is inlined here; this calls it, which is the same code.)
static Boolean
FastInvoke(FastRunState* state, long numArgs)
{
	Ref fn = *--state->fStack->fTop;
	return FastDoCall(state, fn, numArgs);
}


// ROM 0x002ed508 FastCall__FP12FastRunStatel
// call: the global function named by the symbol on the stack.
static Boolean
FastCall(FastRunState* state, long numArgs)
{
	state->fArg1 = *--state->fStack->fTop;
	long exists;
	Ref fn = UnsafeGetFrameSlot(gFunctionFrame, state->fArg1, &exists);
	if (!exists)
	{
		ThrowExInterpreterWithSymbol(kNSErrUndefinedGlobalFunction, state->fArg1);
		return false;
	}
	return FastDoCall(state, fn, numArgs);
}


// ROM 0x002ed598 FastSend__FP12FastRunStatel
static Boolean
FastSend(FastRunState* state, long numArgs)
{
	Ref* top = state->fStack->fTop;
	state->fArg1 = top[-1];									// the message
	state->fArg2 = top[-2];									// the receiver
	state->fStack->fTop = top - 2;
	if (!XFindImplementor(state->fArg2, state->fArg1, &state->fArg3, &state->fArg4))
	{
		ThrowExInterpreterWithSymbol(kNSErrUndefinedMethod, state->fArg1);
		return false;
	}
	return FastDoSend(state, state->fArg2, state->fArg3, state->fArg4, numArgs);
}


// ROM 0x002ed644 FastSendIfDefined__FP12FastRunStatel
static Boolean
FastSendIfDefined(FastRunState* state, long numArgs)
{
	Ref* top = state->fStack->fTop;
	state->fArg1 = top[-1];
	state->fArg2 = top[-2];
	state->fStack->fTop = top - 2;
	if (XFindImplementor(state->fArg2, state->fArg1, &state->fArg3, &state->fArg4))
		return FastDoSend(state, state->fArg2, state->fArg3, state->fArg4, numArgs);
	Ref* p = state->fStack->fTop - numArgs;
	*p = NILREF;
	state->fStack->fTop = p + 1;
	return false;
}


// ROM 0x002ed6f4 FastResend__FP12FastRunStatel
static Boolean
FastResend(FastRunState* state, long numArgs)
{
	state->fArg1 = *--state->fStack->fTop;
	VMState* vm = state->fInterpreter->fVMState;
	if (!XFindProtoImplementor(StateVar(vm->fImplementor), state->fArg1, &state->fArg2, &state->fArg3))
	{
		ThrowExInterpreterWithSymbol(kNSErrUndefinedMethod, state->fArg1);
		return false;
	}
	vm = state->fInterpreter->fVMState;
	return FastDoSend(state, StateVar(vm->fReceiver), state->fArg2, state->fArg3, numArgs);
}


// ROM 0x002ed798 FastResendIfDefined__FP12FastRunStatel
static Boolean
FastResendIfDefined(FastRunState* state, long numArgs)
{
	state->fArg1 = *--state->fStack->fTop;
	VMState* vm = state->fInterpreter->fVMState;
	if (XFindProtoImplementor(StateVar(vm->fImplementor), state->fArg1, &state->fArg2, &state->fArg3))
	{
		vm = state->fInterpreter->fVMState;
		return FastDoSend(state, StateVar(vm->fReceiver), state->fArg2, state->fArg3, numArgs);
	}
	Ref* p = state->fStack->fTop - numArgs;
	*p = NILREF;
	state->fStack->fTop = p + 1;
	return false;
}


// ROM 0x002ed840 FastSetLexScope__FP12FastRunStatel
// set-lex-scope: a closure made of the function on the stack (as SlowRun).
static Boolean
FastSetLexScope(FastRunState* state, long)
{
	TInterpreter* interp = state->fInterpreter;
	state->fArg1 = *--state->fStack->fTop;
	state->fArg1 = Clone(state->fArg1);
	state->fArg2 = ObjArraySlots(OBJ((Ref) state->fArg1))[kFunctionArgFrameSlot];
	state->fArg2 = Clone(state->fArg2);
	ObjHeader* af = OBJ((Ref) state->fArg2);
	VMState* vm = interp->fVMState;
	ObjArraySlots(af)[kArgFrameNextArgFrameSlot] = StateRef(vm->fLocals);
	if (!interp->fLocalsOnStack)
	{
		ObjArraySlots(af)[kArgFrameParentSlot] = StateRef(vm->fReceiver);
		ObjArraySlots(af)[kArgFrameImplementorSlot] = StateRef(vm->fImplementor);
	}
	else
	{
		if (ObjArraySlots(af)[kArgFrameParentSlot] == 0)
			ObjArraySlots(af)[kArgFrameParentSlot] = NILREF;
		else
			ObjArraySlots(af)[kArgFrameParentSlot] = StateRef(vm->fReceiver);
		if (ObjArraySlots(af)[kArgFrameImplementorSlot] == 0)
			ObjArraySlots(af)[kArgFrameImplementorSlot] = NILREF;
		else
			ObjArraySlots(af)[kArgFrameImplementorSlot] = StateRef(vm->fImplementor);
		if (Length(state->fArg2) == 3 && ObjArraySlots(af)[0] == NILREF && ObjArraySlots(af)[1] == NILREF && ObjArraySlots(af)[2] == NILREF)
			state->fArg2 = NILREF;
	}
	ObjArraySlots(OBJ((Ref) state->fArg1))[kFunctionArgFrameSlot] = state->fArg2;
	*state->fStack->fTop++ = state->fArg1;
	return false;
}


// ROM 0x002ecca0 FastIterNext__FP12FastRunStatel
// iter-next: the iterator on the stack stepped on, then popped.
static Boolean
FastIterNext(FastRunState* state, long)
{
	TRefStructStack* stack = state->fStack;
	if (stack->fHandlesEnd - stack->fHandles < stack->Depth())
		stack->Fill();
	ForEachLoopNext(stack->StackRef(stack->Depth() - 1));
	stack->fTop--;
	return false;
}


// ROM 0x002ecd18 FastIterDone__FP12FastRunStatel
// iter-done: whether the iterator on the stack is done, in its place.
static Boolean
FastIterDone(FastRunState* state, long)
{
	TRefStructStack* stack = state->fStack;
	if (stack->fHandlesEnd - stack->fHandles < stack->Depth())
		stack->Fill();
	Boolean done = ForEachLoopDone(stack->StackRef(stack->Depth() - 1));
	stack->fTop[-1] = done ? TRUEREF : NILREF;
	return false;
}


// ROM 0x002eda8c FastUnary1Ext__FP12FastRunStatel
// The simple instructions with a halfword operand: pop-handlers (7) alone.
static Boolean
FastUnary1Ext(FastRunState* state, long)
{
	long which = (short) FastOperand(state->fPC);
	state->fPC += 2;
	if (which == 7)
	{
		TInterpreter* interp = state->fInterpreter;
		interp->fExceptionContext = GetArraySlotRef(interp->fExceptionContext, kHandlerNext);
	}
	else
		UndefinedBytecode();
	return false;
}


// ROM 0x002ed9b8 FastBranchIfLoopNotDone__FP12FastRunStatel
static Boolean
FastBranchIfLoopNotDone(FastRunState* state, long target)
{
	Ref* top = state->fStack->fTop;
	long limit = RINT(top[-1]);
	long index = RINT(top[-2]);
	long incr = RINT(top[-3]);
	state->fStack->fTop = top - 3;
	if ((incr > 0 && index <= limit) || (incr < 0 && index >= limit))
		state->fPC = state->fInstructions + target;
	else if (incr == 0)
		Throw(exInterpreter, (void*) kNSErrZeroForLoopIncr, nil);
	return false;
}


// ROM 0x002edaf8 FastFindVar__FP12FastRunStatel
// find-var: a variable by name, from the locals (or the receiver when the
// locals are on the stack and there is no frame of them), then the globals.
static Boolean
FastFindVar(FastRunState* state, long b)
{
	TInterpreter* interp = state->fInterpreter;
	state->fArg1 = state->fLiterals[b];
	VMState* vm = interp->fVMState;
	long exists;
	if (!interp->fLocalsOnStack || StateRef(vm->fLocals) != NILREF)
		state->fArg2 = XGetVariable(StateVar(vm->fLocals), state->fArg1, &exists, 1);
	else
		state->fArg2 = XGetVariable(StateVar(vm->fReceiver), state->fArg1, &exists, 0);
	if (!exists)
		state->fArg2 = UnsafeGetFrameSlot(gVarFrame, state->fArg1, &exists);
	if (!exists)
		ThrowExInterpreterWithSymbol(kNSErrUndefinedVariable, state->fArg1);
	else
		*state->fStack->fTop++ = state->fArg2;
	return false;
}


// ROM 0x002edbf8 FastFindAndSetVar__FP12FastRunStatel
// find-and-set-var (as SlowRun).
static Boolean
FastFindAndSetVar(FastRunState* state, long b)
{
	TInterpreter* interp = state->fInterpreter;
	state->fArg1 = state->fLiterals[b];
	state->fArg2 = *--state->fStack->fTop;
	VMState* vm = interp->fVMState;
	Boolean isSend = (RVALUE(StateRef(vm->fStackFrame)) & kStackFrameIsSend) != 0;
	long flags;
	if (interp->fLocalsOnStack && StateRef(vm->fLocals) == NILREF)
	{
		flags = isSend ? (kSetVarSetGlobal | kSetVarMakeGlobal) : kSetVarSetGlobal;
		if (SetVariableOrGlobal(StateVar(vm->fReceiver), state->fArg1, state->fArg2, flags))
			return false;
		state->fArg3 = Clone(RefVar(Rcanonicalfakecontext));
		ObjArraySlots(OBJ((Ref) state->fArg3))[kArgFrameParentSlot] = StateRef(vm->fReceiver);
		SetFrameSlot(state->fArg3, state->fArg1, state->fArg2);
		StateRef(vm->fLocals) = state->fArg3;
		return false;
	}
	flags = isSend ? (kSetVarLookupLocals | kSetVarSetGlobal | kSetVarMakeGlobal) : (kSetVarLookupLocals | kSetVarSetGlobal | kSetVarSetInContext);
	SetVariableOrGlobal(StateVar(vm->fLocals), state->fArg1, state->fArg2, flags);
	return false;
}


// ROM 0x002edd60 FastMakeArray__FP12FastRunStatel
static Boolean
FastMakeArray(FastRunState* state, long b)
{
	Ref* top = state->fStack->fTop;
	state->fArg1 = top[-1];									// the class
	if (b == 0xffff)
	{
		long length = RINT(top[-2]);
		top[-2] = AllocateArray(state->fArg1, length);
		state->fStack->fTop = top - 1;
	}
	else
	{
		state->fArg1 = AllocateArray(state->fArg1, b);
		top = state->fStack->fTop;
		Ref* slots = ObjArraySlots(OBJ((Ref) state->fArg1));
		for (long i = 0; i < b; i++)
			slots[i] = top[i - 1 - b];
		top[-1 - b] = state->fArg1;
		state->fStack->fTop = top - b;
	}
	return false;
}


// ROM 0x002ede40 FastMakeFrame__FP12FastRunStatel
static Boolean
FastMakeFrame(FastRunState* state, long b)
{
	Ref* top = state->fStack->fTop;
	state->fArg1 = top[-1];									// the map
	state->fArg1 = AllocateFrameWithMap(state->fArg1);
	top = state->fStack->fTop;
	Ref* slots = ObjArraySlots(OBJ((Ref) state->fArg1));
	for (long i = 0; i < b; i++)
		slots[i] = top[i - 1 - b];
	top[-1 - b] = state->fArg1;
	state->fStack->fTop = top - b;
	return false;
}


// ROM 0x002edee8 FastNewHandlers__FP12FastRunStatel
static Boolean
FastNewHandlers(FastRunState* state, long b)
{
	TInterpreter* interp = state->fInterpreter;
	state->fArg1 = AllocateArray(RSSYMarray, kHandlerSize);
	state->fArg2 = AllocateArray(RSSYMarray, b * 2);
	Ref* pairs = ObjArraySlots(OBJ((Ref) state->fArg2));
	for (long i = 0; i < b * 2; i++)
		pairs[i] = state->fStack->fTop[i - b * 2];
	state->fStack->fTop -= b * 2;
	Ref* h = ObjArraySlots(OBJ((Ref) state->fArg1));
	VMState* vm = interp->fVMState;
	h[kHandlerNext] = interp->fExceptionContext;
	h[kHandlerValueDepth] = MAKEINT(state->fStack->Depth() - 1);
	h[kHandlerControlDepth] = MAKEINT(interp->fCtrlStack.Depth() - 1);
	h[kHandlerFunction] = StateRef(vm->fFunction);
	h[kHandlerReceiver] = StateRef(vm->fReceiver);
	h[kHandlerImplementor] = StateRef(vm->fImplementor);
	h[kHandlerLocals] = StateRef(vm->fLocals);
	h[kHandlerExceptions] = state->fArg2;
	interp->fExceptionContext = (Ref) state->fArg1;
	interp->fExceptionStackIndex = interp->fCtrlStack.Depth() - 1;
	return false;
}


// ROM 0x002ee090 FastUndefined__FP12FastRunStatel
static Boolean
FastUndefined(FastRunState*, long)
{
	UndefinedBytecode();
	return false;
}


// ROM 0x002ecd94 FastFreqFuncGeneral__FP12FastRunStatel
// freq-func with a halfword operand: the arithmetic and comparisons of two
// integers done here (* div < > >= <= band bor bnot), anything else - and
// every other function - called through gFreqFuncs as FastDoCall calls.
// (DEVIATION: div by nought throws exDivideByZero, as FDiv does, where the
// ROM's __rt_sdiv traps.)
static Boolean
FastFreqFuncGeneral(FastRunState* state, long)
{
	long which = (short) FastOperand(state->fPC);
	state->fPC += 2;
	Ref* top = state->fStack->fTop;
	Ref a, b;
	Ref result;
	switch (which)
	{
	case kFFMultiply:
		a = top[-2];
		b = top[-1];
		if (((a | b) & 3) != 0)
			break;
#if NEWTON_NS64
		top[-2] = (Ref) ((ULong) a * (ULong) RVALUE(b));		// wraps at the Ref's width
#else
		top[-2] = WordRef((Ref) ((ULong32) a * (ULong32) RVALUE(b)));
#endif
		state->fStack->fTop = top - 1;
		return false;
	case kFFDiv:
		a = top[-2];
		b = top[-1];
		if (((a | b) & 3) != 0)
			break;
		if (RVALUE(b) == 0)
			Throw(exDivideByZero, nil, nil);
#if NEWTON_NS64
		// the one quotient that does not fit, -2^61 div -1, wraps as the sum would
		top[-2] = (RVALUE(b) == -1) ? (Ref) (0 - (ULong) a) : (Ref) ((ULong) (RVALUE(a) / RVALUE(b)) << 2);
#else
		top[-2] = WordRef((Ref) ((ULong32) ((Long32) RVALUE(a) / (Long32) RVALUE(b)) << 2));
#endif
		state->fStack->fTop = top - 1;
		return false;
	case kFFLessThan:
	case kFFGreaterThan:
	case kFFGreaterOrEqual:
	case kFFLessOrEqual:
	{
		a = top[-2];
		b = top[-1];
		if (((a | b) & 3) != 0)
			break;
		long x = RVALUE(a), y = RVALUE(b);
		Boolean yes = which == kFFLessThan ? x < y
					: which == kFFGreaterThan ? x > y
					: which == kFFGreaterOrEqual ? x >= y
					: x <= y;
		top[-2] = yes ? TRUEREF : NILREF;
		state->fStack->fTop = top - 1;
		return false;
	}
	case kFFBAnd:
	case kFFBOr:
		a = top[-2];
		b = top[-1];
		if (((a | b) & 3) != 0)
			break;
		result = which == kFFBAnd ? (a & b) : (a | b);
		top[-2] = MAKEINT(RVALUE(result));
		state->fStack->fTop = top - 1;
		return false;
	case kFFBNot:											// (ROM QUIRK: its operand is not checked)
		top[-1] = MAKEINT(~RVALUE(top[-1]));
		return false;
	}
	// (the ROM's copy of FastDoCall's body, with the function and its
	// argument count out of the tables)
	Ref fn = ObjArraySlots(OBJ(gFreqFuncs))[which];
	return FastDoCall(state, fn, gFreqFuncInfo[which].fNumArgs);
}


// ROM 0x002ef1f0 FastComplicatedAref__FP12FastRunStatelT2i
// aref of something not a plain array: a string read through TRichString
// (reading just past the end of one answers a nul character outside a
// function with its locals on the stack), anything else an error.
static void
FastComplicatedAref(FastRunState* state, long index, long flags, Boolean localsOnStack)
{
	if ((flags & kObjSlotted) == 0)
	{
		if (IsInstance(state->fArg1, RSSYMstring))
		{
			TRichString s(state->fArg1);
			long length = s.Length();
			if (index < 0 || index >= length)
			{
				if (!localsOnStack && index == length)
					state->fStack->fTop[-1] = MAKECHAR(0);
				else
					ThrowOutOfBoundsException(state->fArg1, index);
			}
			else
				state->fStack->fTop[-1] = MAKECHAR(s.GetChar(index));
			return;
		}
	}
	ThrowBadTypeWithFrameData(kNSErrNotAnArrayOrString, state->fArg1);
}


// ROM 0x002ef314 FastComplicatedSetAref__FP12FastRunStatelT2i
// setAref of something not a plain writable array: a read-only array (or
// frame) is an error of its own, a frame or anything else not a string the
// usual one; a string is written through TRichString as SlowRun does.
static void
FastComplicatedSetAref(FastRunState* state, long index, long flags, Boolean localsOnStack)
{
	if ((flags & kObjSlotted) != 0)
	{
		if ((flags & kObjReadOnly) != 0)
		{
			RefVar data(AllocateFrame());
			SetFrameSlot(data, RSSYMerrorcode, RefVar(MAKEINT(kNSErrObjectReadOnly)));
			SetFrameSlot(data, RSSYMvalue, state->fArg1);
			ThrowRefException(exFramesWithFrameData, data);
		}
		ThrowBadTypeWithFrameData(kNSErrNotAnArrayOrString, state->fArg1);
	}
	if (!IsInstance(state->fArg1, RSSYMstring))
		ThrowBadTypeWithFrameData(kNSErrNotAnArrayOrString, state->fArg1);
	TRichString s(state->fArg1);
	long length = s.Length();
	if (index < 0 || index >= length)
	{
		if (index != length || localsOnStack || RCHAR(state->fArg2) != 0)
			ThrowOutOfBoundsException(state->fArg1, index);
		StrMunger(state->fArg1, index, -1, RefVar(NILREF), 0, -1);
	}
	else
	{
		UniChar c = RCHAR(state->fArg2);
		if (c != 0 && c != kInkChar)
			s.SetChar(index, c);
		else if (localsOnStack || c != 0)
		{
			RefVar data(AllocateFrame());
			SetFrameSlot(data, RSSYMerrorcode, RefVar(MAKEINT(kNSErrBadCharForString)));
			SetFrameSlot(data, RSSYMvalue, state->fArg2);
			ThrowRefException(exInterpreterWithFrameData, data);
		}
		else
			StrMunger(state->fArg1, index, -1, RefVar(NILREF), 0, -1);
	}
	state->fStack->fTop[-1] = state->fArg2;
}


// ROM 0x002ef73c FastComplicatedEqual__FP12FastRunState
// = of two refs one of which is a pointer (FreqEqual's pointer cases; the
// numbers go through FastPartiallyRealEqual 0x002ef660, the same compare)
static void
FastComplicatedEqual(FastRunState* state)
{
	state->fStack->fTop[-1] = FreqEqual(state->fArg1, state->fArg2) ? TRUEREF : NILREF;
}


// ROM 0x002ef8a8 FastComplicatedNotEqual__FP12FastRunState
// (and FastPartiallyRealNotEqual 0x002ef7cc)
static void
FastComplicatedNotEqual(FastRunState* state)
{
	state->fStack->fTop[-1] = FreqEqual(state->fArg1, state->fArg2) ? NILREF : TRUEREF;
}


// ROM 0x002ee0a8 FastRun__12TInterpreterFl
// The fast loop over its own state (four RefVars for its helpers).
Boolean
TInterpreter::FastRun(long baseDepth)
{
	FastRunState state;
	state.fInterpreter = this;
	state.fStack = &fValueStack;
	return FastRun1(baseDepth, state);
}


// ROM 0x002ee138 FastRun1__12TInterpreterFlR12FastRunState
// The loop: the current function's instructions and literals found, then
// one instruction after another, until a call or a return changes the
// function (the state is taken up again) or takes the control stack below
// baseDepth (answers true) or leaves a function the fast loop may not run
// (answers false, and SlowRun takes over).
//
// The instruction pointer and the stack's top live in locals; before a
// helper is called they are written back to the state and the stack, and
// read again after.
//
// DEVIATION: they are written back too before an inline case calls out to
// something that may allocate - GetFramePath, SetFramePath, the number
// arithmetic - since a collection marks the value stack only up to its
// fTop.  The ROM writes the top back only for the helpers, so a value
// pushed since (a real just made by an addition, say) could be collected
// while still on the stack; that is heap damage, not a result to keep.
Boolean
TInterpreter::FastRun1(long baseDepth, FastRunState& state)
{
	#define SYNC()		(fValueStack.fTop = sp, state.fPC = pc)
	#define RELOAD()	(sp = fValueStack.fTop, pc = state.fPC)
	#define LOCALS()	(fValueStack.fBase + fLocalsIndex)
	for (;;)
	{
		state.fInstructions = (const unsigned char*) BinaryData(fInstructions);
		state.fPC = state.fInstructions + fPC;
		state.fLiterals = (Ref) fLiterals == NILREF ? nil : ObjArraySlots(OBJ((Ref) fLiterals));
		const unsigned char* pc = state.fPC;
		Ref* sp = fValueStack.fTop;
		Boolean changed = false;
		while (!changed)
		{
			// DEVIATION: as SlowRun, a point every so often for the kernel to
			// preempt the task at (the stack written back first, so that a
			// collection meanwhile marks all of it)
			if ((++gInterpreterPreemptCount & 0x3ff) == 0)
			{
				SYNC();
				HostPreemptionPoint();
			}
			unsigned op = *pc++;
			long b = op & 7;
			if (b == 7 && (op >> 3) != 0 && (op >> 3) != kBCFreqFunc)
			{
				b = FastOperand(pc);
				pc += 2;
			}
			#define HELPER(fn, arg)	do { SYNC(); changed = fn(&state, (arg)); RELOAD(); } while (0)
			switch (op >> 3)
			{
			case 0:
				switch (op)
				{
				case kBCPop:
					sp--;
					break;
				case kBCDup:
					*sp = sp[-1];
					sp++;
					break;
				case kBCReturn:
				{
					// (Return, open-coded: the result replaces the frame when the
					// locals were on the stack; back to the caller's state)
					if (fLocalsOnStack)
					{
						Ref result = sp[-1];
						Ref* p = fValueStack.fBase + (RVALUE(StateRef(fVMState->fStackFrame)) >> 6) + 3;
						*p = result;
						sp = p + 1;
					}
					fValueStack.fTop = sp;
					fVMState = fCtrlStack.PrevState();
					fPC = RVALUE(StateRef(fVMState->fPC));
					if (ControlPosition() < fExceptionStackIndex)
						PopHandlers();
					if (StateRef(fVMState->fFunction) != NILREF && fPC != -1)
						SetFlags();
					state.fPC = pc;
					changed = true;
					break;
				}
				case kBCPushSelf:
					*sp++ = StateRef(fVMState->fReceiver);
					break;
				case kBCSetLexScope:
					HELPER(FastSetLexScope, 0);
					break;
				case kBCIterNext:
					HELPER(FastIterNext, 0);
					break;
				case kBCIterDone:
					HELPER(FastIterDone, 0);
					break;
				case kBCPopHandlers:
					HELPER(FastUnary1Ext, 0);
					break;
				}
				break;

			case kBCPush:
				*sp++ = state.fLiterals[b];
				break;

			case kBCPushConstant:
				if (op == 0x27)
					b = (short) b;
				*sp++ = (Ref) b;
				break;

			case kBCCall:
				HELPER(FastCall, b);
				break;
			case kBCInvoke:
				HELPER(FastInvoke, b);
				break;
			case kBCSend:
				HELPER(FastSend, b);
				break;
			case kBCSendIfDefined:
				HELPER(FastSendIfDefined, b);
				break;
			case kBCResend:
				HELPER(FastResend, b);
				break;
			case kBCResendIfDefined:
				HELPER(FastResendIfDefined, b);
				break;

			case kBCBranch:
				pc = state.fInstructions + b;
				break;
			case kBCBranchIfTrue:
				if (*--sp != NILREF)
					pc = state.fInstructions + b;
				break;
			case kBCBranchIfFalse:
				if (*--sp == NILREF)
					pc = state.fInstructions + b;
				break;

			case kBCFindVar:
				HELPER(FastFindVar, b);
				break;

			case kBCGetVar:
				if (!fLocalsOnStack)
					*sp = ObjArraySlots(OBJ(StateRef(fVMState->fLocals)))[b];
				else
					*sp = LOCALS()[b];
				sp++;
				break;

			case kBCMakeFrame:
				HELPER(FastMakeFrame, b);
				break;
			case kBCMakeArray:
				HELPER(FastMakeArray, b);
				break;

			case kBCGetPath:
				if (b == 0)
				{
					if (sp[-2] == NILREF)
						sp[-2] = NILREF;
					else
					{
						state.fArg1 = sp[-1];						// the path
						state.fArg2 = sp[-2];						// the object
						SYNC();
						sp[-2] = GetFramePath(state.fArg2, state.fArg1);
					}
					sp--;
				}
				else if (b == 1)
				{
					if (sp[-2] == NILREF)
					{
						state.fArg1 = sp[-1];
						ThrowExFramesWithBadValue(kNSErrPathFailed, state.fArg1);
						return false;
					}
					state.fArg1 = sp[-1];
					state.fArg2 = sp[-2];
					SYNC();
					sp[-2] = GetFramePath(state.fArg2, state.fArg1);
					sp--;
				}
				else
				{
					HELPER(FastUndefined, 0);
				}
				break;

			case kBCSetPath:
				if (b == 0 || b == 1)
				{
					state.fArg1 = sp[-1];							// the value
					state.fArg2 = sp[-2];							// the path
					state.fArg3 = sp[-3];							// the object
					SYNC();
					// (the ROM asks whether the instructions are below 0x00800000,
					// in the ROM; here whether they are in the ROM object area)
					if (!fLocalsOnStack || InROMObjectArea((Ref) fInstructions))
						SetFramePathFor1XFunctions(state.fArg3, state.fArg2, state.fArg1);
					else
						SetFramePath(state.fArg3, state.fArg2, state.fArg1);
					if (b == 0)
						sp -= 3;
					else
					{
						sp[-3] = state.fArg1;
						sp -= 2;
					}
				}
				else
				{
					HELPER(FastUndefined, 0);
				}
				break;

			case kBCSetVar:
				if (!fLocalsOnStack)
					ObjArraySlots(OBJ(StateRef(fVMState->fLocals)))[b] = sp[-1];
				else
					LOCALS()[b] = sp[-1];
				sp--;
				break;

			case kBCFindAndSetVar:
				HELPER(FastFindAndSetVar, b);
				break;

			case kBCIncrVar:
			{
				Ref* local = !fLocalsOnStack ? &ObjArraySlots(OBJ(StateRef(fVMState->fLocals)))[b] : &LOCALS()[b];
				Ref value = *local;
				Ref incr = sp[-1];
				if (((value | incr) & 3) == 0)
				{
					Ref sum = WordRef((Ref) ((ULong) incr + (ULong) value));
					*local = sum;
					*sp++ = sum;
				}
				else
				{
					if ((value & 3) != 0)
						_RINTError(value);
					if ((incr & 3) != 0)
						_RINTError(incr);
				}
				break;
			}

			case kBCBranchIfLoopNotDone:
				HELPER(FastBranchIfLoopNotDone, b);
				break;

			case kBCFreqFunc:
				switch (op)
				{
				case 0xc0:											// +
				{
					Ref rb = sp[-1];
					Ref ra = sp[-2];
					if (((ra | rb) & 3) == 0)
						sp[-2] = WordRef((Ref) ((ULong) ra + (ULong) rb));
					else
					{
						state.fArg1 = ra;
						state.fArg2 = rb;
						SYNC();
						sp[-2] = NumberAdd(state.fArg1, state.fArg2);
					}
					sp--;
					break;
				}
				case 0xc1:											// -
				{
					Ref rb = sp[-1];
					Ref ra = sp[-2];
					if (((ra | rb) & 3) == 0)
						sp[-2] = WordRef((Ref) ((ULong) ra - (ULong) rb));
					else
					{
						state.fArg1 = ra;
						state.fArg2 = rb;
						SYNC();
						sp[-2] = NumberSubtract(state.fArg1, state.fArg2);
					}
					sp--;
					break;
				}
				case 0xc2:											// aref: a plain array open-coded
				{
					long index = RINT(sp[-1]);
					ObjHeader* o = OBJ(sp[-2]);
					ULong flags = ObjFlags(o);
					if ((flags & (kObjSlotted | kObjFrame)) == kObjSlotted)
					{
						if (index < 0 || index >= ObjArrayLength(o))
						{
							SYNC();
							ThrowOutOfBoundsException(RefVar(sp[-2]), index);
							return false;
						}
						sp[-2] = ObjArraySlots(o)[index];
						sp--;
					}
					else
					{
						state.fArg1 = sp[-2];
						sp--;
						SYNC();
						FastComplicatedAref(&state, index, flags & 0xff, fLocalsOnStack);
						RELOAD();
					}
					break;
				}
				case 0xc3:											// setAref: a plain writable array open-coded
				{
					state.fArg2 = sp[-1];							// the value
					long index = RINT(sp[-2]);
					ObjHeader* o = OBJ(sp[-3]);
					ULong flags = ObjFlags(o);
					if ((flags & (kObjSlotted | kObjFrame | kObjReadOnly)) == kObjSlotted)
					{
						if (index < 0 || index >= ObjArrayLength(o))
						{
							SYNC();
							ThrowOutOfBoundsException(RefVar(sp[-3]), index);
							return false;
						}
						ObjArraySlots(o)[index] = state.fArg2;
						sp[-3] = state.fArg2;
						sp -= 2;
					}
					else
					{
						state.fArg1 = sp[-3];
						sp -= 2;
						SYNC();
						FastComplicatedSetAref(&state, index, flags & 0xff, fLocalsOnStack);
						RELOAD();
					}
					break;
				}
				case 0xc4:											// =: two refs neither a pointer open-coded
				{
					Ref rb = sp[-1];
					Ref ra = sp[-2];
					if (((ra | rb) & 1) == 0)
					{
						sp[-2] = ra == rb ? TRUEREF : NILREF;
						sp--;
					}
					else
					{
						state.fArg1 = ra;
						state.fArg2 = rb;
						sp--;
						SYNC();
						FastComplicatedEqual(&state);
						RELOAD();
					}
					break;
				}
				case 0xc5:											// not
					sp[-1] = sp[-1] == NILREF ? TRUEREF : NILREF;
					break;
				case 0xc6:											// <>
				{
					Ref rb = sp[-1];
					Ref ra = sp[-2];
					if (((ra | rb) & 1) == 0)
					{
						sp[-2] = ra == rb ? NILREF : TRUEREF;
						sp--;
					}
					else
					{
						state.fArg1 = ra;
						state.fArg2 = rb;
						sp--;
						SYNC();
						FastComplicatedNotEqual(&state);
						RELOAD();
					}
					break;
				}
				case 0xc7:											// the others, their index a halfword
					HELPER(FastFreqFuncGeneral, 0);
					break;
				}
				break;

			case kBCNewHandlers:
				HELPER(FastNewHandlers, b);
				break;

			default:
				HELPER(FastUndefined, 0);
				break;
			}
			#undef HELPER
		}
		if (ControlPosition() < baseDepth)
			return true;
		if (!fFastLoop)
			return false;
	}
	#undef SYNC
	#undef RELOAD
	#undef LOCALS
}


// ROM 0x002f1d68 AlternatingLoops__12TInterpreterFl
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


// ROM 0x002f1db4 Run__12TInterpreterFv
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
	state.fTraceIndent = saved->fTraceIndent;
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

// ROM 0x002ecbd0 GetStackState__Fv
static void
GetStackState(StackState* state)
{
	state->fControlDepth = gInterpreter->ControlPosition();
	state->fValueDepth = gInterpreter->ValuePosition();
	state->fHandlers = gInterpreter->fExceptionContext;
	state->fTraceIndent = gInterpreter->fTraceIndent;
}


// ROM 0x002f54d8 GetStackStateBlock__Fv
StackState*
GetStackStateBlock(void)
{
	StackState* state = new StackState;
	if (state == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	GetStackState(state);
	return state;
}


// ROM 0x002f647c DisposeStackStateBlock__FP10StackState
void
DisposeStackStateBlock(StackState* state)
{
	delete state;
}


// ROM 0x002f4084 ResetStack__FRC10StackState
void
ResetStack(const StackState& state)
{
	gInterpreter->fCtrlStack.Reset(state.fControlDepth);
	gInterpreter->fValueStack.Reset(state.fValueDepth);
	gInterpreter->fExceptionContext = state.fHandlers;
	gInterpreter->fTraceIndent = state.fTraceIndent;
}


// ROM 0x002f5674 PopHandlers__12TInterpreterFv
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


// ROM 0x002f5890 TranslateException__12TInterpreterFP9Exception
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


// ROM 0x002f59ac ExceptionBeingHandled__12TInterpreterFv
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


// The exceptions the developer has already been told of (and broken into
// the REP for) while breakOnThrows is set: a list of their names, each a
// copy the exception was pointed at when it was remembered - which is why
// the names are compared as pointers, not as strings.
struct DeveloperNotifiedEntry
{
	DeveloperNotifiedEntry*	fNext;		// +0x00
	char*					fName;		// +0x04
};
// ROM 0x0c10546c gDeveloperNotified
static DeveloperNotifiedEntry*	gDeveloperNotified = nil;


// ROM 0x002f5568 DeveloperNotified__FP9Exception
// Whether this very exception has been reported already.
static Boolean
DeveloperNotified(Exception* exception)
{
	for (DeveloperNotifiedEntry* e = gDeveloperNotified; e != nil; e = e->fNext)
		if (e->fName == exception->name)
			return true;
	return false;
}


// ROM 0x002f55a4 RememberDeveloperNotified__FP9Exception
// The exception's name copied, the exception pointed at the copy, and the
// copy put on the list, so that the same exception rethrown further out is
// not reported again.  Out of memory, nothing is remembered.
static void
RememberDeveloperNotified(Exception* exception)
{
	char* name = new char[strlen(exception->name) + 1];
	if (name == nil)
		return;
	DeveloperNotifiedEntry* entry = new DeveloperNotifiedEntry;
	if (entry == nil)
	{
		delete[] name;
		return;
	}
	strcpy(name, exception->name);
	exception->name = name;
	entry->fName = name;
	entry->fNext = gDeveloperNotified;
	gDeveloperNotified = entry;
}


// ROM 0x002f5610 ForgetDeveloperNotified__FPc
// The entry of that name (the copy's pointer) off the list and given back.
void
ForgetDeveloperNotified(char* name)
{
	for (DeveloperNotifiedEntry** link = &gDeveloperNotified; *link != nil; link = &(*link)->fNext)
	{
		DeveloperNotifiedEntry* entry = *link;
		if (entry->fName == name)
		{
			*link = entry->fNext;
			delete[] entry->fName;
			delete entry;
			return;
		}
	}
}


// ROM 0x002f5de8 HandleException__12TInterpreterFP9ExceptionlR10StackState
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
		// breakOnThrows: the developer told, the break loop entered (the
		// global function BreakLoop), and the exception remembered so that
		// it breaks only once on its way out.  (DEVIATION: the ROM always
		// has a REP; a host program without one is not told.)
		if (gREPout != nil)
			gREPout->ExceptionNotify(exception);
		RefVar args(NILREF);
		RefVar breakLoop(GetFrameSlotRef(gFunctionFrame, RSSYMbreakloop));
		DoBlock(breakLoop, args);
		RememberDeveloperNotified(exception);
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

// ROM 0x002f641c SetupSend__FRC6RefVarT1lR6RefVar
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


// ROM 0x002f64a4 SetupResend__FRC6RefVarlR6RefVar
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


// ROM 0x002f6744 SetLexScope__FRC6RefVarN31
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

// ROM 0x002f1980 StackTrace__Fv
void
StackTrace(void)
{
	gInterpreter->StackTrace();
}


/* -------------------------------------------------------------------------------
	Tracing and breakpoints
	vars.trace turns tracing on for the next Run: 'functions prints each call
	and return, 'full the same and every variable read and written with the
	objects printed whole, any other non-nil value everything with objects
	other than symbols and numbers printed as their addresses; another
	symbol traces only inside calls of that function, and a frame says what
	to trace - its name (a function), slot (one slot; true is any),
	contextFrame (only under that frame) and functions slots - or, when it
	is a view (it has a viewCObject), traces only under it.  A trace line
	goes to the REP's out translator, indented by the depth of the call.
------------------------------------------------------------------------------- */

// ROM 0x0035e580 IsParent__FRC6RefVarT1
// Whether frame is context or one of the frames up its _parent chain.
static Boolean
IsParent(RefArg frame, RefArg context)
{
	RefVar current(context);
	while ((Ref) current != NILREF)
	{
		if (EQRef(current, frame))
			return true;
		current = GetFrameSlotRef(current, RSSYM_parent);
	}
	return false;
}


// ROM 0x0035e4f4 TaciturnPrintObject__12TInterpreterFRC6RefVarl
// An object printed in a trace line: a pointer object other than a symbol
// or a real as its address, unless the trace is 'full.
void
TInterpreter::TaciturnPrintObject(RefArg obj, long indent)
{
	Ref r = obj;
	if ((r & 1) != 0 && !IsSymbol(r) && !ISREAL(r) && fTraceLevel != 3)
	{
		gREPout->Print("#%lX", (unsigned long) r);
		return;
	}
	gREPout->ConsumeFrame(obj, 0, indent);
}


// ROM 0x0035e79c TraceSetOptions__12TInterpreterFv
// vars.trace read into the interpreter's trace settings (Run does it on
// every call from C++).
void
TInterpreter::TraceSetOptions(void)
{
	RefVar trace(GetFrameSlotRef(gVarFrame, RSSYMtrace));
	if ((Ref) trace == NILREF)
		fTraceLevel = 0;
	else
	{
		fTraceLevel = 2;
		fTracePrintCalls = 1;
		fTraceVariables = 1;
		fTraceFunction = NILREF;
		fTraceContext = NILREF;
		fTraceSlot = NILREF;
		if (IsSymbol(trace))
		{
			if (EQRef(trace, RSSYMfunctions))
			{
				fTraceLevel = 1;
				fTraceVariables = 0;
			}
			else if (EQRef(trace, RSSYMfull))
				fTraceLevel = 3;
			else
				fTraceFunction = trace;
		}
		else if (IsFrame(trace))
		{
			if (FrameHasSlotRef(trace, RSSYMviewcobject))
				fTraceContext = trace;
			else
			{
				if (FrameHasSlotRef(trace, RSSYMfunctions))
					fTraceVariables = GetFrameSlotRef(trace, RSSYMfunctions) != NILREF;
				if (FrameHasSlotRef(trace, RSSYMname))
					fTraceFunction = GetFrameSlotRef(trace, RSSYMname);
				if (FrameHasSlotRef(trace, RSSYMcontextframe))
					fTraceContext = GetFrameSlotRef(trace, RSSYMcontextframe);
				if (FrameHasSlotRef(trace, RSSYMslot))
				{
					RefVar slot(GetFrameSlotRef(trace, RSSYMslot));
					if (!EQRef(slot, TRUEREF))
					{
						fTraceVariables = (Ref) slot != NILREF;
						fTraceSlot = slot;
						fTracePrintCalls = 0;
					}
				}
			}
		}
	}
	SetFastLoopFlag();
}


// ROM 0x0035ee38 TraceArgs__12TInterpreterFlN21
// The arguments of a call, off the value stack, first to last.
void
TInterpreter::TraceArgs(long numArgs, long first, long indent)
{
	for (long i = numArgs + first - 1; i >= first; i--)
	{
		RefVar arg(fValueStack.fTop[-1 - i]);
		TaciturnPrintObject(arg, indent);
		if (i > first)
			gREPout->Print(", ");
	}
}


// ROM 0x0035eec8 TraceMethod__12TInterpreterFRC6RefVarT1PclT4
// A call's trace line - "(receiver):name(args)" - and one level deeper.
void
TInterpreter::TraceMethod(RefArg receiver, RefArg name, const char* nameString, long numArgs, long first)
{
	if ((Ref) fTraceFunction != NILREF)
	{
		if (fTraceDepth != 0 || EQRef(fTraceFunction, name))
			fTraceDepth++;
		if (fTraceDepth == 0)
			return;
	}
	if ((Ref) fTraceContext != NILREF)
	{
		if (!IsParent(receiver, StateVar(fVMState->fLocals)))
			return;
		if (!IsParent(fTraceContext, StateVar(fVMState->fLocals)))
			return;
	}
	if (fTracePrintCalls)
	{
		gREPout->Print("%*s", (int) fTraceIndent, " ");
		if ((Ref) receiver != NILREF)
		{
			long indent = gREPout->Print("(");
			TaciturnPrintObject(receiver, indent);
			gREPout->Print("):");
		}
		long indent = gREPout->Print("%s(", nameString);
		TraceArgs(numArgs, first, indent);
		gREPout->Print(")\r");
	}
	fTraceIndent += 4;
}


// ROM 0x0035e600 TraceSend__12TInterpreterFRC6RefVarT1lT3
void
TInterpreter::TraceSend(RefArg receiver, RefArg message, long numArgs, long kind)
{
	const char* name = (Ref) message == NILREF ? "---" : SymbolName(message);
	TraceMethod(receiver, message, name, numArgs, kind);
}


// ROM 0x0035e658 TraceFreqCall__12TInterpreterFl
void
TInterpreter::TraceFreqCall(long index)
{
	TraceMethod(RefVar(NILREF), RefVar(NILREF), gFreqFuncInfo[index].fName, gFreqFuncInfo[index].fNumArgs, 0);
}


// ROM 0x0035e6d0 TraceApply__12TInterpreterFRC6RefVarl
void
TInterpreter::TraceApply(RefArg, long numArgs)
{
	TraceMethod(RefVar(NILREF), RefVar(NILREF), "[call with]", numArgs, 0);
}


// ROM 0x0035e740 TraceCall__12TInterpreterFRC6RefVarl
void
TInterpreter::TraceCall(RefArg fnName, long numArgs)
{
	TraceMethod(RefVar(NILREF), fnName, SymbolName(fnName), numArgs, 0);
}


// ROM 0x0035ea38 TraceGet__12TInterpreterFRC6RefVarN21
// A variable read: "value <= (context/foundIn).name".
void
TInterpreter::TraceGet(RefArg context, RefArg foundIn, RefArg name)
{
	if (!fTraceVariables)
		return;
	if ((Ref) fTraceSlot != NILREF && !EQRef(name, fTraceSlot))
		return;
	if ((Ref) fTraceFunction != NILREF && fTraceDepth == 0)
		return;
	if ((Ref) fTraceContext != NILREF && !IsParent(fTraceContext, StateVar(fVMState->fLocals)))
		return;
	long indent = gREPout->Print("%*s", (int) fTraceIndent, " ");
	RefVar value(IsSymbol(name) ? GetFrameSlotRef(foundIn, name) : GetFramePath(foundIn, name));
	TaciturnPrintObject(value, indent);
	indent = gREPout->Print(" <= (");
	TaciturnPrintObject(context, indent);
	if (!EQRef(foundIn, context))
	{
		indent = gREPout->Print("/");
		TaciturnPrintObject(foundIn, indent);
	}
	indent = gREPout->Print(").");
	TaciturnPrintObject(name, indent);
	gREPout->Print("\r");
}


// ROM 0x0035ec00 TraceSet__12TInterpreterFRC6RefVarN31
// A variable written: "(context/foundIn).name := value".
void
TInterpreter::TraceSet(RefArg context, RefArg foundIn, RefArg name, RefArg value)
{
	if (!fTraceVariables)
		return;
	if ((Ref) fTraceSlot != NILREF && !EQRef(name, fTraceSlot))
		return;
	if ((Ref) fTraceFunction != NILREF && fTraceDepth == 0)
		return;
	if ((Ref) fTraceContext != NILREF && !IsParent(fTraceContext, StateVar(fVMState->fLocals)))
		return;
	long indent = gREPout->Print("%*s(", (int) fTraceIndent, " ");
	gInterpreter->TaciturnPrintObject(context, indent);
	if (!EQRef(foundIn, context))
	{
		indent = gREPout->Print("/");
		gInterpreter->TaciturnPrintObject(foundIn, indent);
	}
	indent = gREPout->Print(").");
	gInterpreter->TaciturnPrintObject(name, indent);
	indent = gREPout->Print(" := ");
	gInterpreter->TaciturnPrintObject(value, indent);
	gREPout->Print("\r");
}


// ROM 0x0035ed6c TraceReturn__12TInterpreterFUc
// A return's trace line - "=> result", the result printed when asked - and
// one level back out.
void
TInterpreter::TraceReturn(UChar printValue)
{
	if ((Ref) fTraceFunction != NILREF)
	{
		if (fTraceDepth == 0)
			return;
		fTraceDepth--;
	}
	long was = fTraceIndent;
	Boolean print = false;
	if (was != 0)
	{
		fTraceIndent = was - 4;
		print = fTracePrintCalls;
	}
	if (was != 0 && print)
	{
		long indent = gREPout->Print("%*s=> ", (int) fTraceIndent, " ");
		RefVar result(fValueStack.fTop[-1]);
		if (printValue)
			TaciturnPrintObject(result, indent);
		gREPout->Print("\r");
	}
}


// ROM 0x0035ee30 TraceReturn__12TInterpreterFv
void
TInterpreter::TraceReturn(void)
{
	TraceReturn((UChar) 1);
}


// ROM 0x002d3f6c HandleBreakPoints__12TInterpreterFv
// Before each instruction while breakpoints are on: the breakpoints'
// programCounter array is looked through for one at this instruction of
// this function that is not disabled; a temporary one is taken out as it
// is hit, and the array and then the breakpoints frame dropped once empty.
// A hit enters the break loop (the global function BreakLoop).
void
TInterpreter::HandleBreakPoints(void)
{
	if (gFramesBreakPoints == NILREF)
		return;
	RefVar points(GetFrameSlotRef(gFramesBreakPoints, RSSYMprogramcounter));
	if ((Ref) points == NILREF)
		return;
	Boolean hit = false;
	TObjectIterator iter(points, false);
	while (!iter.Done())
	{
		RefVar point(iter.Value());
		if (RINT(GetFrameSlotRef(point, RSSYMprogramcounter)) == fPC
		 && EQRef((Ref) fInstructions, GetFrameSlotRef(point, RSSYMinstructions))
		 && GetFrameSlotRef(point, RSSYMdisabled) == NILREF)
		{
			hit = true;
			if (GetFrameSlotRef(point, RSSYMtemporary) != NILREF)
				ArrayRemoveCount(points, RINT(iter.Tag()), 1);
		}
		iter.Next();
	}
	if (Length(points) == 0)
		RemoveSlot(RefVar(gFramesBreakPoints), RSSYMprogramcounter);
	if (Length(gFramesBreakPoints) == 0)
		gFramesBreakPoints = NILREF;
	if (hit)
		DoBlock(RefVar(GetFrameSlotRef(gFunctionFrame, RSSYMbreakloop)), RefVar(NILREF));
}


// ROM 0x002d41ac SetBreakPoints__12TInterpreterFRC6RefVar
Ref
TInterpreter::SetBreakPoints(RefArg breakPoints)
{
	Ref was = gFramesBreakPoints;
	gFramesBreakPoints = breakPoints;
	return was;
}


// ROM 0x002d41c8 EnableBreakPoints__12TInterpreterFUc
Boolean
TInterpreter::EnableBreakPoints(Boolean enable)
{
	Boolean was = gFramesBreakPointsEnabled;
	gFramesBreakPointsEnabled = enable;
	SetFastLoopFlag();
	return was;
}


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


// ROM 0x002ef938 InterpretBlock__FRC6RefVarT1
Ref
InterpretBlock(RefArg fn, RefArg receiver)
{
	gInterpreter->TopLevelCall(fn, receiver);
	gInterpreter->Run();
	return gInterpreter->PopValue();
}


// ROM 0x002ef970 PushArgArray__FRC6RefVar
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


// ROM 0x002efe48 DoCall__FRC6RefVarl
Ref
DoCall(RefArg fn, long numArgs)
{
	if (!IsFunction(fn))
		ThrowBadTypeWithFrameData(kNSErrNotAFunction, fn);
	if (gInterpreter->fTraceLevel != 0)
		gInterpreter->TraceApply(fn, numArgs);
	return RunCall(fn, numArgs, false, fn, fn);
}


// ROM 0x002f059c DoSend__FRC6RefVarN21l
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


// ROM 0x002f1c8c DoBlock__FRC6RefVarT1
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


// ROM 0x002f1b00 DoScript__FRC6RefVarN21
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


// ROM 0x002f0e40 DoMessage__FRC6RefVarN21
Ref
DoMessage(RefArg receiver, RefArg message, RefArg args)
{
	if (!IsSymbol(message))
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, message);
	RefVar implementor(FindImplementor(receiver, message));
	long numArgs = PushArgArray(args);
	return DoSend(receiver, implementor, message, numArgs);
}


// ROM 0x002f1990 DoMessageIfDefined__FRC6RefVarN21Pl
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


// ROM 0x002f1660 DoProtoMessage__FRC6RefVarN21
Ref
DoProtoMessage(RefArg receiver, RefArg message, RefArg args)
{
	if (!IsSymbol(message))
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, message);
	RefVar implementor(FindProtoImplementor(receiver, message));
	long numArgs = PushArgArray(args);
	return DoSend(receiver, implementor, message, numArgs);
}


// ROM 0x002f1a48 DoProtoMessageIfDefined__FRC6RefVarN21Pl
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


// ROM 0x002ef9fc NSCall__FRC6RefVar
Ref
NSCall(RefArg fn)
{
	if (!IsFunction(fn))
		ThrowBadTypeWithFrameData(kNSErrNotAFunction, fn);
	return DoCall(fn, 0);
}


// ROM 0x002efa34 NSCall__FRC6RefVarT1
Ref
NSCall(RefArg fn, RefArg a1)
{
	if (!IsFunction(fn))
		ThrowBadTypeWithFrameData(kNSErrNotAFunction, fn);
	gInterpreter->PushValue(a1);
	return DoCall(fn, 1);
}


// ROM 0x002efa84 NSCall__FRC6RefVarN21
Ref
NSCall(RefArg fn, RefArg a1, RefArg a2)
{
	if (!IsFunction(fn))
		ThrowBadTypeWithFrameData(kNSErrNotAFunction, fn);
	gInterpreter->PushValue(a1);
	gInterpreter->PushValue(a2);
	return DoCall(fn, 2);
}


// ROM 0x002efae4 NSCall__FRC6RefVarN31
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


// ROM 0x002efb54 NSCall__FRC6RefVarN41
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


// ROM 0x002efbd4 NSCall__FRC6RefVarN51
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


// ROM 0x002efc64 NSCall__FRC6RefVarN61
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


// ROM 0x002efd04 NSCallWithArgArray__FRC6RefVarT1
Ref
NSCallWithArgArray(RefArg fn, RefArg args)
{
	if (!IsFunction(fn))
		ThrowBadTypeWithFrameData(kNSErrNotAFunction, fn);
	long numArgs = PushArgArray(args);
	return DoCall(fn, numArgs);
}


// ROM 0x002efd48 NSSend__FRC6RefVarT1
Ref
NSSend(RefArg receiver, RefArg message)
{
	if (!IsSymbol(message))
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, message);
	RefVar implementor(FindImplementor(receiver, message));
	return DoSend(receiver, implementor, message, 0);
}


// ROM 0x002efdbc NSSend__FRC6RefVarN21
Ref
NSSend(RefArg receiver, RefArg message, RefArg a1)
{
	if (!IsSymbol(message))
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, message);
	RefVar implementor(FindImplementor(receiver, message));
	gInterpreter->PushValue(a1);
	return DoSend(receiver, implementor, message, 1);
}


// ROM 0x002eff28 NSSend__FRC6RefVarN31
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


// ROM 0x002effc4 NSSend__FRC6RefVarN41
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


// ROM 0x002f02d4 NSSendWithArgArray__FRC6RefVarN21
Ref
NSSendWithArgArray(RefArg receiver, RefArg message, RefArg args)
{
	if (!IsSymbol(message))
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, message);
	RefVar implementor(FindImplementor(receiver, message));
	long numArgs = PushArgArray(args);
	return DoSend(receiver, implementor, message, numArgs);
}


// ROM 0x002f09c8 NSSendIfDefined__FRC6RefVarT1
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


// ROM 0x002f0a58 NSSendIfDefined__FRC6RefVarN21
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


// ROM 0x002f0b00 NSSendIfDefined__FRC6RefVarN31
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


// ROM 0x002f0354 NSSendProto__FRC6RefVarT1
Ref
NSSendProto(RefArg receiver, RefArg message)
{
	if (!IsSymbol(message))
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, message);
	RefVar implementor(FindProtoImplementor(receiver, message));
	return DoSend(receiver, implementor, message, 0);
}


// ROM 0x002f03c8 NSSendProto__FRC6RefVarN21
Ref
NSSendProto(RefArg receiver, RefArg message, RefArg a1)
{
	if (!IsSymbol(message))
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, message);
	RefVar implementor(FindProtoImplementor(receiver, message));
	gInterpreter->PushValue(a1);
	return DoSend(receiver, implementor, message, 1);
}


// ROM 0x002f1054 NSSendProtoIfDefined__FRC6RefVarT1
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


// ROM 0x002f10e4 NSSendProtoIfDefined__FRC6RefVarN21
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


// ROM 0x002f16e0 NSGetGlobalFn__FRC6RefVar
Ref
NSGetGlobalFn(RefArg name)
{
	Ref fn = GetFrameSlotRef(gFunctionFrame, name);
	if (fn == NILREF)
		ThrowExInterpreterWithSymbol(kNSErrUndefinedGlobalFunction, name);
	return fn;
}


// ROM 0x002f1728 NSCallGlobalFn__FRC6RefVar
Ref
NSCallGlobalFn(RefArg name)
{
	RefVar fn(NSGetGlobalFn(name));
	return NSCall(fn);
}


// ROM 0x002f1760 NSCallGlobalFn__FRC6RefVarT1
Ref
NSCallGlobalFn(RefArg name, RefArg a1)
{
	RefVar fn(NSGetGlobalFn(name));
	return NSCall(fn, a1);
}


// ROM 0x002f17a0 NSCallGlobalFn__FRC6RefVarN21
Ref
NSCallGlobalFn(RefArg name, RefArg a1, RefArg a2)
{
	RefVar fn(NSGetGlobalFn(name));
	return NSCall(fn, a1, a2);
}


// ROM 0x002f17e8 NSCallGlobalFn__FRC6RefVarN31
Ref
NSCallGlobalFn(RefArg name, RefArg a1, RefArg a2, RefArg a3)
{
	RefVar fn(NSGetGlobalFn(name));
	return NSCall(fn, a1, a2, a3);
}


// ROM 0x002f1940 NSCallGlobalFnWithArgArray__FRC6RefVarT1
Ref
NSCallGlobalFnWithArgArray(RefArg name, RefArg args)
{
	RefVar fn(NSGetGlobalFn(name));
	return NSCallWithArgArray(fn, args);
}


/* -------------------------------------------------------------------------------
	Start-up
------------------------------------------------------------------------------- */

// the prototype of the compiler's code blocks when the ROM's is not
// imported: {class: 'CodeBlock, instructions, literals, argFrame, numArgs
// (, DebuggerInfo)}, the slots in the order the interpreter indexes them
static Ref
MakeCodeBlockPrototype(Boolean debug)
{
	RefVar tags(AllocateArray(RSSYMarray, debug ? 6 : 5));
	SetArraySlotRef(tags, 0, RSSYMclass);
	SetArraySlotRef(tags, 1, RSSYMinstructions);
	SetArraySlotRef(tags, 2, RSSYMliterals);
	SetArraySlotRef(tags, 3, Intern((char*) "argFrame"));
	SetArraySlotRef(tags, 4, RSSYMnumargs);
	if (debug)
		SetArraySlotRef(tags, 5, RSSYMdebuggerinfo);
	RefVar map(AllocateMapWithTags(RefVar(NILREF), tags));
	RefVar prototype(AllocateFrameWithMap(map));
	SetArraySlotRef(prototype, 0, RSSYMcodeblock);
	return prototype;
}


// ROM 0x002b5104 InitFunctions__Fv
// The global function frame, and the prototypes of code blocks
// (CodeBlock::fgPrototype and DebugCodeBlock::fgPrototype: the ROM's
// frames Rcodeblockprototype/Rdebugcodeblockprototype, made here when
// the ROM's objects are not imported).
void
InitFunctions(void)
{
	gFunctionFrame = AllocateFrame();
	AddGCRoot(gCodeBlockPrototype);
	AddGCRoot(gDebugCodeBlockPrototype);
	if (Rcodeblockprototype != NILREF)
	{
		gCodeBlockPrototype = Rcodeblockprototype;
		gDebugCodeBlockPrototype = Rdebugcodeblockprototype;
	}
	else
	{
		gCodeBlockPrototype = MakeCodeBlockPrototype(false);
		gDebugCodeBlockPrototype = MakeCodeBlockPrototype(true);
	}
}


// ROM 0x002f686c InitInterpreter__Fv
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


// ROM 0x002f6394 GetGInterpreter__Fv
TInterpreter*
GetGInterpreter(void)
{
	return gInterpreter;
}


// ROM 0x002f63a4 GetGFunctionFrame__Fv
Ref
GetGFunctionFrame(void)
{
	return gFunctionFrame;
}


// ROM 0x002f6e34 GetCurrentInterpreterID__Fv
long
GetCurrentInterpreterID(void)
{
	return gInterpreter->fID;
}


// ROM 0x002f6d54 GetTInterpreter__Fl
TInterpreter*
GetTInterpreter(long id)
{
	for (TInterpreter* i = gInterpreterList; i != nil; i = i->fNext)
		if (i->fID == id)
			return i;
	return nil;
}
