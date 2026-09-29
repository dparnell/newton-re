// The NIE's protoFSM re-expressions (thirdparty/nie/NIENatives.h), called
// through NewtonScript as the package's own function objects would be: a
// 0x232 function over the NIE's real code binary (read out of inetenbl.pkg,
// so its length and hash are the registered ones) at each function's
// offset, with a closure carrying the function's literals (an argument
// frame: _nextArgFrame, _parent, _implementor, _literals, then any locals).

#include "NIENatives.h"
#include "NIERuntime.h"
#include "PackageNatives.h"
#include "Interpreter.h"
#include "Compiler.h"
#include "ObjectHeap.h"
#include "Frames.h"
#include "RSSymbols.h"
#include "NSErrors.h"
#include "ROMImport.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static RefStruct* gCodeRef = nil;
#define gCode (*gCodeRef)

static Ref
Eval(const char* source)
{
	RefVar fn(ParseString(RefVar(MakeString(source))));
	return InterpretBlock(fn, RefVar(gVarFrame));
}

static Ref
Sym(const char* name)
{
	return Intern((char*) name);
}

// a native function object as the package holds it: [0x232, code, numArgs,
// closure, offset]; the closure [_nextArgFrame, _parent, _implementor,
// literals]
static Ref
NativeFunction(long offset, long numArgs, const char* const* literals, long count)
{
	RefVar lits(MakeArray(count));
	for (long i = 0; i < count; i++)
		SetArraySlot(lits, i, RefVar(Sym(literals[i])));
	RefVar closure(Eval("{_nextArgFrame: nil, _parent: nil, _implementor: nil, _literals: nil}"));
	SetFrameSlot(closure, RefVar(Sym("_literals")), lits);
	RefVar fn(MakeArray(5));
	SetArraySlotRef(fn, 0, kBinaryNativeFuncClass);
	SetArraySlot(fn, 1, gCode);
	SetArraySlotRef(fn, 2, MAKEINT(numArgs));
	SetArraySlot(fn, 3, closure);
	SetArraySlotRef(fn, 4, MAKEINT(offset));
	return fn;
}

static void
TestQueue(void)
{
	static const char* const kQueue[] = { "queue" };
	static const char* const kDeQueue[] = { "queue", "RemoveSlot" };
	RefVar q(Eval("{queue: []}"));
	SetFrameSlot(q, RefVar(Sym("EnQueue")), RefVar(NativeFunction(0x7dc0, 1, kQueue, 1)));
	SetFrameSlot(q, RefVar(Sym("DeQueue")), RefVar(NativeFunction(0x7b78, 0, kDeQueue, 2)));
	SetFrameSlot(q, RefVar(Sym("Peek")), RefVar(NativeFunction(0x7a1c, 0, kQueue, 1)));
	SetFrameSlot(q, RefVar(Sym("GetQueueSize")), RefVar(NativeFunction(0x7ef0, 0, kQueue, 1)));
	SetFrameSlot(q, RefVar(Sym("IsEmpty")), RefVar(NativeFunction(0x8004, 0, kQueue, 1)));
	SetFrameSlot(RefVar(gVarFrame), RefVar(Sym("q")), q);

	EXPECT(NOTNIL(Eval("q:IsEmpty()")));
	EXPECT(ISNIL(Eval("q:Peek()")));
	EXPECT(ISNIL(Eval("q:DeQueue()")));
	EXPECT(ISNIL(Eval("q:EnQueue('a)")));
	Eval("q:EnQueue('b)");
	Eval("q:EnQueue('c)");
	EXPECT(RINT(Eval("q:GetQueueSize()")) == 3);
	EXPECT(ISNIL(Eval("q:IsEmpty()")));
	EXPECT(EQRef(Eval("q:Peek()"), Sym("a")));
	EXPECT(EQRef(Eval("q:DeQueue()"), Sym("a")));
	EXPECT(EQRef(Eval("q:DeQueue()"), Sym("b")));
	EXPECT(RINT(Eval("q:GetQueueSize()")) == 1);

	// no queue: undefined variable, as the native code throws
	RefVar lone(Eval("{}"));
	SetFrameSlot(lone, RefVar(Sym("GetQueueSize")), RefVar(NativeFunction(0x7ef0, 0, kQueue, 1)));
	SetFrameSlot(RefVar(gVarFrame), RefVar(Sym("lone")), lone);
	long thrown = 0;
	newton_try
	{
		Eval("lone:GetQueueSize()");
	}
	newton_catch_all
	{
		RefStruct* data = (RefStruct*) CurrentException()->data;
		if (data != nil && IsFrame(*data))
			thrown = RINT(GetFrameSlot(*data, RSSYMerrorcode));
	}
	end_try;
	EXPECT(thrown == kNSErrUndefinedVariable);
}


static void
TestEngine(void)
{
	static const char* const kCheck[] = { "fsm_private_context" };
	static const char* const kIdle[] = { "fsm", "DoEvent_Loop" };
	static const char* const kProto[] = { "_proto" };

	// DoEvent_Check: the context, whatever it is asked
	RefVar m(Eval("{fsm_private_context: 'ctx}"));
	SetFrameSlot(m, RefVar(Sym("DoEvent_Check")), RefVar(NativeFunction(0xd4cc, 1, kCheck, 1)));
	SetFrameSlot(RefVar(gVarFrame), RefVar(Sym("m")), m);
	EXPECT(EQRef(Eval("m:DoEvent_Check('anything)"), Sym("ctx")));

	// the engine view's idle: fsm:DoEvent_Loop()
	RefVar engine(Eval("{fsm: {DoEvent_Loop: func() 42}}"));
	SetFrameSlot(engine, RefVar(Sym("viewIdleScript")), RefVar(NativeFunction(0xe43c, 0, kIdle, 2)));
	SetFrameSlot(RefVar(gVarFrame), RefVar(Sym("engine")), engine);
	EXPECT(RINT(Eval("engine:viewIdleScript()")) == 42);

	// the ancestors' states, each put behind the ones before it
	SetFrameSlot(RefVar(gVarFrame), RefVar(Sym("collectStates")), RefVar(NativeFunction(0xe65c, 2, kProto, 1)));
	SetFrameSlot(RefVar(gVarFrame), RefVar(Sym("collectEvents")), RefVar(NativeFunction(0xe808, 3, kProto, 1)));
	Eval("anc := [{Idle: {a: 1, Go: {x: 1}}}, {Busy: {}}, {Idle: {b: 2, Go: {y: 3}}}]");
	RefVar r(Eval("call collectStates with (anc, 'Idle)"));
	EXPECT(IsFrame(r) && RINT(GetFrameSlot(r, RefVar(Sym("b")))) == 2);
	SetFrameSlot(RefVar(gVarFrame), RefVar(Sym("collected")), r);
	EXPECT(RINT(Eval("collected.a")) == 1);		// (the first ancestor's, behind the last's)
	EXPECT(ISNIL(Eval("call collectStates with (anc, 'None)")));
	EXPECT(ISNIL(Eval("call collectStates with (nil, 'Idle)")));
	EXPECT(RINT(Eval("call collectEvents with (anc, 'Idle, 'Go).y")) == 3);
	EXPECT(RINT(Eval("call collectEvents with (anc, 'Idle, 'Go).x")) == 1);
	EXPECT(ISNIL(Eval("call collectEvents with (anc, 'Busy, 'Go)")));
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_ROM_IMAGE) != noErr)
	{
		printf("test_NIEProtoFSM: cannot import %s\n", NEWTON_ROM_IMAGE);
		return 1;
	}
	gObjectHeapSize = 0x200000;
	InitObjects();

	FILE* f = fopen(NIE_PACKAGE, "rb");
	if (f == nil)
	{
		printf("test_NIEProtoFSM: no %s\n", NIE_PACKAGE);
		return 1;
	}
	static unsigned char code[kNIECodeLength];
	fseek(f, 0x2f88 + 12, SEEK_SET);
	size_t got = fread(code, 1, sizeof(code), f);
	fclose(f);
	EXPECT(got == kNIECodeLength);
	EXPECT(PackageNativeCodeHash(code, kNIECodeLength) == kNIECodeHash);
	gCodeRef = new RefStruct;
	gCode = AllocateBinary(RSSYMbinary, kNIECodeLength);
	memcpy(BinaryData(gCode), code, kNIECodeLength);

	RegisterNIENatives();
	newton_try
	{
		TestQueue();
		TestEngine();
	}
	newton_catch_all
	{
		failures++;
		printf("FAIL: exception %s\n", CurrentException()->name);
		RefStruct* data = (RefStruct*) CurrentException()->data;
		if (data != nil && IsFrame(*data))
			printf("  errorCode %ld\n", (long) RINT(GetFrameSlot(*data, RSSYMerrorcode)));
	}
	end_try;

	printf("test_NIEProtoFSM: %s\n", failures == 0 ? "all passed" : "FAILED");
	return failures != 0;
}
