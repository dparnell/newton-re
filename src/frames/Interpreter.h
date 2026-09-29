/*
	File:		frames/Interpreter.h

	Contains:	The NewtonScript interpreter: TInterpreter and its two stacks
				(TRefStack, TIntrpStack), the VM state of a call, the
				bytecodes, the entry points C++ uses to call NewtonScript
				(NSCall, NSSend, DoBlock, DoMessage, ...), variable lookup
				(GetVariable, FindImplementor, SetVariable and the TICache
				lookup caches) and the global function table.

	Reconstructed from the MP2x00 US ROM: the interpreter at
	0x002ecbd0-0x002f70a4 and 0x002d3cc4-0x002d428c, the lookup caches at
	0x002ff53c-0x003015fc, the stacks at 0x001a4600-0x001a4b80.  The DDK
	has no header for any of this; the layouts are the ROM's (TInterpreter
	0x80 bytes, TRefStack 0x10, TRefStructStack 0x18, TICache 0x10).

	A function object is one of: a NewtonScript function (an array of
	class kFuncClass 0x32: [class, instructions, literals, argFrame,
	numArgs | numLocals << 16]), a native function (class 0x132: [class,
	funcPtr, numArgs]; funcPtr the address of a C function - in the ROM's
	own functions a jump-table address, bound to a host implementation by
	NativeFunctions.h), a native function with code in a binary (0x232, or a
	binCFunction frame: ARM code, which a host cannot run) or a 1.x
	CodeBlock frame {class, instructions, literals, argFrame, numArgs}.

	The interpreter runs bytecodes (kBC...) over a value stack and a control
	stack of VM states (six refs each: pc, function, locals, implementor,
	receiver, stack-frame word); the ROM keeps a parallel stack of pointers
	to the value slots so that a slot can be passed where a RefVar is
	expected (TRefStructStack) - kept here, see StackRef().
*/

#ifndef __INTERPRETER_H
#define __INTERPRETER_H

#ifndef __OBJECTHEAP_H
#include "ObjectHeap.h"
#endif

class TInterpreter;
struct Exception;


/* -------------------------------------------------------------------------------
	Function objects
------------------------------------------------------------------------------- */

const Ref	kNativeFuncClass = 0x132;			// kFuncClass with FUNCKIND 1: a C function
const Ref	kBinaryNativeFuncClass = 0x232;		// FUNCKIND 2: code in a binary object

// the slots of a NewtonScript function array (kFuncClass)
const long	kFunctionClassSlot = 0;
const long	kFunctionInstructionsSlot = 1;
const long	kFunctionLiteralsSlot = 2;
const long	kFunctionArgFrameSlot = 3;
const long	kFunctionNumArgsSlot = 4;		// numArgs | numLocals << 16
// a native function's
const long	kNativeFuncPtrSlot = 1;
const long	kNativeNumArgsSlot = 2;
const long	kNativeDocStringSlot = 3;
// the slots of an argFrame (a function's lexical environment)
const long	kArgFrameNextArgFrameSlot = 0;
const long	kArgFrameParentSlot = 1;
const long	kArgFrameImplementorSlot = 2;
const long	kArgFrameFirstArgSlot = 3;

Boolean		IsFunction(RefArg obj);
long		GetFunctionArgCount(RefArg fn);
Boolean		IsNativeFunction(RefArg fn);
Ref			MakeCFunction(void* funcPtr, long numArgs, const char* docString);


/* -------------------------------------------------------------------------------
	Bytecodes
	An instruction is a byte a << 3 | b; b == 7 means a 16-bit operand
	follows.  a == 0 are the simple instructions (b says which).
------------------------------------------------------------------------------- */

enum {
	kBCPop = 0, kBCDup, kBCReturn, kBCPushSelf, kBCSetLexScope, kBCIterNext, kBCIterDone, kBCPopHandlers,
};
enum {
	kBCPush = 3, kBCPushConstant, kBCCall, kBCInvoke, kBCSend, kBCSendIfDefined, kBCResend,
	kBCResendIfDefined, kBCBranch, kBCBranchIfTrue, kBCBranchIfFalse, kBCFindVar, kBCGetVar,
	kBCMakeFrame, kBCMakeArray, kBCGetPath, kBCSetPath, kBCSetVar, kBCFindAndSetVar, kBCIncrVar,
	kBCBranchIfLoopNotDone, kBCFreqFunc, kBCNewHandlers,
};
// the frequently called functions (gFreqFuncInfo): the first seven are done
// in the interpreter loop, the rest go through gFreqFuncs
enum {
	kFFAdd = 0, kFFSubtract, kFFAref, kFFSetAref, kFFEquals, kFFNot, kFFNotEquals, kFFMultiply,
	kFFDivide, kFFDiv, kFFLessThan, kFFGreaterThan, kFFGreaterOrEqual, kFFLessOrEqual, kFFBAnd,
	kFFBOr, kFFBNot, kFFNewIterator, kFFLength, kFFClone, kFFSetClass, kFFAddArraySlot,
	kFFStringer, kFFHasPath, kFFClassOf,
	kNumFreqFuncs
};
struct FreqFuncInfo
{
	const char*	fName;
	long		fNumArgs;
};
extern const FreqFuncInfo	gFreqFuncInfo[kNumFreqFuncs];	// 0x0c1022e8
extern const long			gNumFreqFuncs;					// 0x0c1023b0


/* -------------------------------------------------------------------------------
	Stacks
------------------------------------------------------------------------------- */

// A stack of Refs the collector knows (a DIY marker/updater).  The ROM
// gives each a 64 KB stack from the stack manager (NewStack), paged in as
// it grows; here a fixed allocation.
class TRefStack
{
public:
				TRefStack();
				~TRefStack();
	void		Reset(long depth);					// back to depth refs
	void		PushNILs(long count);
	long		Depth(void) const				{ return fTop - fBase; }

	Ref*		fTop;			// +0x00  where the next push goes
	Ref*		fBase;			// +0x04
	Ref*		fLimit;			// +0x08  (the ROM: the range its release proc reports)
	long		fSize;			// +0x0c  (the ROM: 300)
};

// A TRefStack with a parallel stack of pointers to its slots, filled as
// the stack grows (Fill), so that a slot is addressable as a RefVar: a
// RefVar is one pointer to a RefHandle whose first word is the ref, and
// the pointer-stack entry for slot i points at slot i (its "stackPos" is
// then slot i + 1, never read).  StackRef(i) is that RefVar.
class TRefStructStack : public TRefStack
{
public:
				TRefStructStack();
				~TRefStructStack();
	void		Fill(void);
	const RefVar&	StackRef(long index)		{ return *(const RefVar*) &fHandles[index]; }

	RefHandle**	fHandles;		// +0x10  one per slot, (RefHandle*) &fBase[i]
	RefHandle**	fHandlesEnd;	// +0x14  filled up to here
};

// The VM state of a call: six refs on the control stack, addressed
// through the pointer stack so that each field is a RefVar.
struct VMState
{
	RefHandle*	fPC;			// +0x00  MAKEINT(pc) of the caller's instruction to resume; MAKEINT(-1) in a native call
	RefHandle*	fFunction;		// +0x04
	RefHandle*	fLocals;		// +0x08  the argFrame (1.x, closures) - or NILREF when the locals are on the value stack
	RefHandle*	fImplementor;	// +0x0c
	RefHandle*	fReceiver;		// +0x10
	RefHandle*	fStackFrame;	// +0x14  MAKEINT(base << 6 | flags): base the value-stack index of the frame, flag 1 locals on the stack, 2 a send
};
const long	kVMStateSize = 6;
const long	kStackFrameLocalsOnStack = 1;
const long	kStackFrameIsSend = 2;
inline Ref&	StateRef(RefHandle* field)			{ return field->ref; }
inline const RefVar& StateVar(RefHandle*& field)	{ return *(const RefVar*) &field; }

class TIntrpStack : public TRefStructStack
{
public:
	VMState*	NewState(void);
	VMState*	DupState(void);
	VMState*	PrevState(void);
	VMState*	StateAt(long index);
};


/* -------------------------------------------------------------------------------
	Exception handlers
	new-handlers makes a handler record (an array of nine) and chains it on
	the interpreter's handler context.
------------------------------------------------------------------------------- */

enum {
	kHandlerNext = 0, kHandlerValueDepth, kHandlerControlDepth, kHandlerFunction, kHandlerReceiver,
	kHandlerImplementor, kHandlerExceptions, kHandlerException, kHandlerLocals,
	kHandlerSize
};

// what Run saves to reset the stacks after an exception nothing handled
struct StackState
{
	long		fControlDepth;	// +0x00
	long		fValueDepth;	// +0x04
	RefStruct	fHandlers;		// +0x08
	long		fField64;		// +0x0c  TInterpreter::fField64
};
StackState*	GetStackStateBlock(void);
void		DisposeStackStateBlock(StackState* state);
void		ResetStack(const StackState& state);


/* -------------------------------------------------------------------------------
	TInterpreter
------------------------------------------------------------------------------- */

enum FramesProfilingKind { kProfileCall = 0, kProfileSend, kProfileReturn, kProfileUnwind };

class TInterpreter
{
public:
				TInterpreter();
				~TInterpreter();

	// running
	void		Run(void);
	void		AlternatingLoops(long baseDepth);
	Boolean		FastRun(long baseDepth);
	Boolean		SlowRun(long baseDepth);
	void		SetFastLoopFlag(void);
	void		SetFlags(void);

	// calling
	void		TopLevelCall(RefArg fn, RefArg receiver);
	Boolean		Call(RefArg fn, long numArgs);				// true: done (a native), false: Run it
	Boolean		Send(RefArg receiver, RefArg implementor, RefArg fn, long numArgs);
	void		CallCodeBlock(RefArg fn, long numArgs, long flags);
	void		CallPlainCodeBlock(RefArg fn, long numArgs, long flags);
	void		CallCFunction(RefArg fn, long numArgs, int isFrame);
	void		CallPlainCFunction(RefArg fn, long numArgs);
	Ref			CallCFuncPtr(void* funcPtr, long numArgs);
	Ref			CallPackageNative(RefArg code, ULong offset, long numArgs);	// host: frames/PackageNatives.h
	void		Return(FramesProfilingKind kind);

	// the value stack
	void		PushValue(RefArg value);
	Ref			PopValue(void);
	Ref			PeekValue(long depth);
	void		SetValue(long depth, Ref value);
	long		ValuePosition(void);
	Ref			PeekControl(long depth);
	void		SetControl(long depth, Ref value);
	long		ControlPosition(void);

	// exceptions
	void		PopHandlers(void);
	Ref			TranslateException(Exception* exception);
	Ref			ExceptionBeingHandled(void);
	Boolean		HandleException(Exception* exception, long baseDepth, StackState& state);

	// the current call
	Ref			GetReceiver(void);
	Ref			GetImplementor(void);
	Boolean		IsSend(void);
	void		SetCallEnv(void);
	void		SetSendEnv(RefArg receiver, RefArg implementor);
	Ref			GetLocalFromStack(RefArg frameIndex, RefArg name);
	void		SetLocalOnStack(RefArg frameIndex, RefArg name, RefArg value);
	Ref			GetSelfFromStack(RefArg frameIndex);
	void		StackTrace(void);

	// tracing and breakpoints (NOT YET RECONSTRUCTED: see Interpreter.cpp)
	void		HandleBreakPoints(void);
	void		SetBreakPoints(RefArg breakPoints);
	void		EnableBreakPoints(Boolean enable);
	void		TraceSetOptions(void);
	void		TraceGet(RefArg context, RefArg foundIn, RefArg name);
	void		TraceSet(RefArg context, RefArg foundIn, RefArg name, RefArg value);
	void		TraceCall(RefArg fnName, long numArgs);
	void		TraceApply(RefArg fn, long numArgs);
	void		TraceSend(RefArg receiver, RefArg message, long numArgs, long kind);
	void		TraceFreqCall(long index);
	void		TraceReturn(void);

	TInterpreter*	fNext;				// +0x00  gInterpreterList
	long			fID;				// +0x04
	TIntrpStack		fCtrlStack;			// +0x08
	TIntrpStack		fValueStack;		// +0x20
	RefStruct		fExceptionContext;	// +0x38  the chain of handler records (new-handlers)
	long			fExceptionStackIndex;	// +0x3c  the control depth the newest handlers belong to
	RefStruct		fLiterals;			// +0x40
	RefStruct		fInstructions;		// +0x44
	Boolean			fInstructionsFixed;	// +0x48  the instructions binary is read-only or locked: its pointer keeps
	VMState*		fVMState;			// +0x4c
	long			fPC;				// +0x50
	Boolean			fLocalsOnStack;		// +0x54
	long			fLocalsIndex;		// +0x58  the value-stack index of the frame's first slot
	Boolean			fIsSend;			// +0x5c
	Boolean			fFastLoop;			// +0x60  FastRun may run (no tracing, breakpoints or profiling)
	long			fField64;			// +0x64  (saved and restored with the stack state)
	UByte			fField68;			// +0x68
	UByte			fField69;			// +0x69
	RefStruct		fField6C;			// +0x6c
	RefStruct		fField70;			// +0x70
	RefStruct		fField74;			// +0x74
	long			fField78;			// +0x78
	long			fTraceLevel;		// +0x7c  0 none, 1 calls, 2 variable access too
};

// the foreach iterator (Builtins.cpp)
Boolean	ForEachLoopReset(RefArg iter, RefArg obj);
Boolean	ForEachLoopDone(RefArg iter);
Boolean	ForEachLoopNext(RefArg iter);
Ref		NumberAdd(RefArg a, RefArg b);
Ref		NumberSubtract(RefArg a, RefArg b);
Ref		NumberMultiply(RefArg a, RefArg b);
Ref		NumberDivide(RefArg a, RefArg b);

extern TInterpreter*	gInterpreter;			// 0x0c1025a4 gNewtGlobals + 4: the current task's
extern TInterpreter*	gInterpreterList;		// every interpreter
extern Ref				gFreqFuncs;				// 0x0c102544  the function objects of gFreqFuncInfo
extern Ref				gConstantsFrame;		// 0x0c1023b4
extern Ref				gFramesBreakPoints;		// 0x0c102558
extern Boolean			gFramesBreakPointsEnabled;	// 0x0c102554
extern long				gAccurateStackTrace;	// 0x0c10255c
extern Boolean			gUseCFunctionDocStrings;	// 0x0c102548
extern Ref				gCFunctionPrototype;	// 0x0c102564 CFunction::fgPrototype
extern Ref				gCodeBlockPrototype;	// CodeBlock::fgPrototype (the ROM's 0x005cecc1)
extern Ref				gDebugCodeBlockPrototype;	// DebugCodeBlock::fgPrototype (0x005cf119)

void		InitInterpreter(void);
void		InitFunctions(void);
TInterpreter*	GetGInterpreter(void);
Ref			GetGFunctionFrame(void);
long		GetCurrentInterpreterID(void);
TInterpreter*	GetTInterpreter(long id);


/* -------------------------------------------------------------------------------
	Calling NewtonScript from C++
------------------------------------------------------------------------------- */

Ref		InterpretBlock(RefArg fn, RefArg receiver);
long	PushArgArray(RefArg args);
Ref		DoCall(RefArg fn, long numArgs);				// the args pushed already
Ref		DoSend(RefArg receiver, RefArg implementor, RefArg message, long numArgs);
Ref		DoBlock(RefArg fn, RefArg args);
void	ForgetDeveloperNotified(char* name);	// ROM 0x002f5610 ForgetDeveloperNotified__FPc - an exception's breakOnThrows report forgotten
Ref		DoScript(RefArg receiver, RefArg fn, RefArg args);
Ref		DoMessage(RefArg receiver, RefArg message, RefArg args);
Ref		DoMessageIfDefined(RefArg receiver, RefArg message, RefArg args, long* defined);
Ref		DoProtoMessage(RefArg receiver, RefArg message, RefArg args);
Ref		DoProtoMessageIfDefined(RefArg receiver, RefArg message, RefArg args, long* defined);
Ref		NSCall(RefArg fn);
Ref		NSCall(RefArg fn, RefArg a1);
Ref		NSCall(RefArg fn, RefArg a1, RefArg a2);
Ref		NSCall(RefArg fn, RefArg a1, RefArg a2, RefArg a3);
Ref		NSCall(RefArg fn, RefArg a1, RefArg a2, RefArg a3, RefArg a4);
Ref		NSCall(RefArg fn, RefArg a1, RefArg a2, RefArg a3, RefArg a4, RefArg a5);
Ref		NSCall(RefArg fn, RefArg a1, RefArg a2, RefArg a3, RefArg a4, RefArg a5, RefArg a6);
Ref		NSCallWithArgArray(RefArg fn, RefArg args);
Ref		NSSend(RefArg receiver, RefArg message);
Ref		NSSend(RefArg receiver, RefArg message, RefArg a1);
Ref		NSSend(RefArg receiver, RefArg message, RefArg a1, RefArg a2);
Ref		NSSend(RefArg receiver, RefArg message, RefArg a1, RefArg a2, RefArg a3);
Ref		NSSendWithArgArray(RefArg receiver, RefArg message, RefArg args);
Ref		NSSendIfDefined(RefArg receiver, RefArg message);
Ref		NSSendIfDefined(RefArg receiver, RefArg message, RefArg a1);
Ref		NSSendIfDefined(RefArg receiver, RefArg message, RefArg a1, RefArg a2);
Ref		NSSendProto(RefArg receiver, RefArg message);
Ref		NSSendProto(RefArg receiver, RefArg message, RefArg a1);
Ref		NSSendProtoIfDefined(RefArg receiver, RefArg message);
Ref		NSSendProtoIfDefined(RefArg receiver, RefArg message, RefArg a1);
Ref		NSGetGlobalFn(RefArg name);
Ref		NSCallGlobalFn(RefArg name);
Ref		NSCallGlobalFn(RefArg name, RefArg a1);
Ref		NSCallGlobalFn(RefArg name, RefArg a1, RefArg a2);
Ref		NSCallGlobalFn(RefArg name, RefArg a1, RefArg a2, RefArg a3);
Ref		NSCallGlobalFnWithArgArray(RefArg name, RefArg args);
void	StackTrace(void);

// a native function's view of a call: the function's closure and code
void*	NativeEntry(RefArg fn, long numArgs, RefHandle** closure);
Ref		SetLexScope(RefArg fn, RefArg locals, RefArg receiver, RefArg implementor);
Ref		SetupSend(RefArg receiver, RefArg message, long ifDefined, RefVar& implementor);
Ref		SetupResend(RefArg message, long ifDefined, RefVar& implementor);


/* -------------------------------------------------------------------------------
	Variable lookup (VariableLookup.cpp)
	A variable is looked for in the locals (the argFrame chain), then up the
	_proto chain of each frame in the _parent chain of the receiver; a
	global is a slot of gVarFrame.  Results are cached (TICache): the
	context and symbol map to the frame the slot was found in and its index.
------------------------------------------------------------------------------- */

class TICache
{
public:
				TICache(long bits);
				~TICache();
	void		Clear(void);
	void		ClearSymbol(Ref sym, ULong32 hash);
	void		ClearFrame(Ref frame);
	Boolean		Lookup(Ref context, Ref sym, Ref* foundIn, Ref* value, long* exists, long* index);
	Boolean		LookupValue(Ref context, Ref sym, Ref* value, long* exists);
	void		Insert(Ref context, Ref sym, Ref foundIn, long index);
	void		Mark(void);
	void		Update(void);
	static void	DIYMarkTICache(void* cache);
	static void	DIYUpdateTICache(void* cache);

	struct Entry
	{
		Ref		fContext;		// 0: empty
		Ref		fSymbol;
		ULong32	fHash;
		Ref		fFoundIn;		// 0: the symbol is known not to be there
		long	fIndex;
	};
	Entry*		fEntries;		// +0x00
	long		fSize;			// +0x04  1 << bits
	long		fShift;			// +0x08  32 - bits
	long		fMask;			// +0x0c  size - 1
};

extern TICache*	gGetVarCache;		// 0x0c1025bc (5 bits)
extern TICache*	gProtoCache;		// 6 bits
extern TICache*	gROProtoCache;		// 6 bits: for frames in ROM and packages
extern TICache*	gFindImpCache;		// 6 bits

void	InitICache(void);
Ref		XGetVariable(RefArg context, RefArg name, long* exists, int lookupLocals);
Ref		GetVariable(RefArg context, RefArg name, long* exists, int lookupLocals);
Boolean	XFindImplementor(RefArg receiver, RefArg name, RefVar* implementor, RefVar* value);
Boolean	XFindProtoImplementor(RefArg receiver, RefArg name, RefVar* implementor, RefVar* value);
Ref		FindImplementor(RefArg receiver, RefArg name);
Ref		FindProtoImplementor(RefArg receiver, RefArg name);
Boolean	SetVariableOrGlobal(RefArg context, RefArg name, RefArg value, long flags);
Boolean	SetVariable(RefArg context, RefArg name, RefArg value);
// SetVariableOrGlobal's flags
const long	kSetVarLookupLocals = 1;
const long	kSetVarSetGlobal = 2;			// an existing global may be set
const long	kSetVarSetInContext = 4;		// else the context gets the slot
const long	kSetVarMakeGlobal = 8;			// else a global is made

#endif	/* __INTERPRETER_H */
