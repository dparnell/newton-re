// The NIE's protoFSM re-expressions (thirdparty/nie/NIENatives.h), called
// through NewtonScript as the package's own function objects would be: a
// 0x232 function over the NIE's real code binary (read out of inetenbl.pkg,
// so its length and hash are the registered ones) at each function's
// offset, with a closure carrying the function's literals (an argument
// frame: _nextArgFrame, _parent, _implementor, _literals, then any locals).

#include "NIENatives.h"
#include "NIERuntime.h"
#include "PackageNativeCPU.h"
#include "Soups.h"
#include "PackageNatives.h"
#include "Interpreter.h"
#include "Compiler.h"
#include "ObjectHeap.h"
#include "Frames.h"
#include "RSSymbols.h"
#include "NSErrors.h"
#include "ROMImport.h"
#include "REPTranslators.h"
#include <stdlib.h>
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
static bool gOnCPU = false;		// (the checks running on the package's own code)
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


// a function's literal i replaced by the value of some NewtonScript
static void
SetLiteral(RefArg fn, long i, const char* source)
{
	RefVar closure(GetArraySlot(fn, 3));
	SetArraySlot(RefVar(GetFrameSlot(closure, RefVar(Sym("_literals")))), i, RefVar(Eval(source)));
}


static void
TestEvents(void)
{
	static const char* const kCheck[] = { "fsm_private_context" };
	static const char* const kQueue[] = { "queue" };
	static const char* const kDoEvent[] = { "protoFSM:DoEvent", "DoEvent_Check", "pendingEventQueue", "EnQueue",
		"pendingParamsQueue", "busy", "engineView", "SetupIdle", "turtle", "Array", "AddDelayedSend" };
	static const char* const kUnique[] = { "protoFSM:DoUniqueEvent", "DoEvent_Check", "path", "=", "LSearch", "DoEvent" };
	static const char* const kSetupDone[] = { "delay", "SetupIdle" };
	static const char* const kTrim[] = { "sep", "EndsWith", "StrLen", "StrMunger" };

	// AddDelayedSend recorded rather than done
	SetFrameSlot(RefVar(GetGFunctionFrame()), RefVar(Sym("AddDelayedSend")),
		RefVar(Eval("func(v, m, a, d) begin sent := [v, m, a, d]; nil end")));
	Eval("sent := nil");

	RefVar events(Eval("{queue: []}"));
	RefVar params(Eval("{queue: []}"));
	SetFrameSlot(events, RefVar(Sym("EnQueue")), RefVar(NativeFunction(0x7dc0, 1, kQueue, 1)));
	SetFrameSlot(params, RefVar(Sym("EnQueue")), RefVar(NativeFunction(0x7dc0, 1, kQueue, 1)));
	RefVar ctx(Eval("{busy: nil, engineView: 'theView, turtle: 'theTurtle}"));
	SetFrameSlot(ctx, RefVar(Sym("pendingEventQueue")), events);
	SetFrameSlot(ctx, RefVar(Sym("pendingParamsQueue")), params);
	RefVar fsm(Eval("{}"));
	SetFrameSlot(fsm, RefVar(Sym("fsm_private_context")), ctx);
	SetFrameSlot(fsm, RefVar(Sym("DoEvent_Check")), RefVar(NativeFunction(0xd4cc, 1, kCheck, 1)));
	SetFrameSlot(fsm, RefVar(Sym("DoEvent")), RefVar(NativeFunction(0x29ec, 2, kDoEvent, 11)));
	RefVar unique(NativeFunction(0xe9f0, 2, kUnique, 6));
	SetLiteral(unique, 2, "'pendingEventQueue.queue");
	SetFrameSlot(fsm, RefVar(Sym("DoUniqueEvent")), unique);
	SetFrameSlot(RefVar(gVarFrame), RefVar(Sym("fsm")), fsm);

	// the first event queues and wakes the engine
	EXPECT(ISNIL(Eval("fsm:DoEvent('go, 'p1)")));
	EXPECT(NOTNIL(Eval("fsm.fsm_private_context.busy")));
	EXPECT(EQRef(Eval("fsm.fsm_private_context.pendingEventQueue.queue[0]"), Sym("go")));
	EXPECT(EQRef(Eval("fsm.fsm_private_context.pendingParamsQueue.queue[0]"), Sym("p1")));
	EXPECT(EQRef(Eval("sent[0]"), Sym("theView")));
	EXPECT(EQRef(Eval("sent[1]"), Sym("SetupIdle")));
	EXPECT(EQRef(Eval("ClassOf(sent[2])"), Sym("Array")));
	EXPECT(EQRef(Eval("sent[2][0]"), Sym("theTurtle")));
	EXPECT(RINT(Eval("sent[3]")) == 1);

	// a second, while the engine is busy, only queues
	Eval("sent := nil");
	Eval("fsm:DoEvent('stop, 'p2)");
	EXPECT(ISNIL(Eval("sent")));
	EXPECT(RINT(Eval("Length(fsm.fsm_private_context.pendingEventQueue.queue)")) == 2);

	// DoUniqueEvent: not an event already pending
	EXPECT(ISNIL(Eval("fsm:DoUniqueEvent('go, 'p3)")));
	EXPECT(RINT(Eval("Length(fsm.fsm_private_context.pendingEventQueue.queue)")) == 2);
	Eval("fsm:DoUniqueEvent('new, 'p4)");
	EXPECT(RINT(Eval("Length(fsm.fsm_private_context.pendingEventQueue.queue)")) == 3);
	EXPECT(EQRef(Eval("fsm.fsm_private_context.pendingParamsQueue.queue[2]"), Sym("p4")));

	// no context: nothing done
	RefVar dead(Eval("{fsm_private_context: nil}"));
	SetFrameSlot(dead, RefVar(Sym("DoEvent_Check")), RefVar(NativeFunction(0xd4cc, 1, kCheck, 1)));
	SetFrameSlot(dead, RefVar(Sym("DoEvent")), RefVar(NativeFunction(0x29ec, 2, kDoEvent, 11)));
	SetFrameSlot(RefVar(gVarFrame), RefVar(Sym("dead")), dead);
	EXPECT(ISNIL(Eval("dead:DoEvent('go, nil)")));

	// the periodic view's setup: :SetupIdle(delay)
	RefVar periodic(Eval("{delay: 5, SetupIdle: func(d) d * 2}"));
	SetFrameSlot(periodic, RefVar(Sym("viewSetupDoneScript")), RefVar(NativeFunction(0xdbdc, 0, kSetupDone, 2)));
	SetFrameSlot(RefVar(gVarFrame), RefVar(Sym("periodic")), periodic);
	EXPECT(RINT(Eval("periodic:viewSetupDoneScript()")) == 10);

	// the trailing separator taken off
	RefVar trim(NativeFunction(0x8670, 1, kTrim, 4));
	SetLiteral(trim, 0, "\", \"");
	SetFrameSlot(RefVar(gVarFrame), RefVar(Sym("trim")), trim);
	EXPECT(NOTNIL(Eval("StrEqual(call trim with (\"a: 1, b: 2, \"), \"a: 1, b: 2\")")));
	EXPECT(NOTNIL(Eval("StrEqual(call trim with (\"abc\"), \"abc\")")));
}


static void
TestPeriodic(void)
{
	static const char* const kIdle[] = { "evt.ex", "unique", "event", "params", "fsm", "DoUniqueEvent", "DoEvent",
		"occurrences", "delay" };
	static const char* const kCheck[] = { "fsm_private_context" };
	static const char* const kKill[] = { "protoFSM:KillPeriodicEvent", "DoEvent_Check", "engineView", "ChildViewFrames",
		"IsArray", "=", "event", "LFetch", "RemoveStepView" };

	// the periodic view posts its event and counts it
	RefVar view(Eval("{unique: nil, event: 'tick, params: 'pp, occurrences: 2, delay: 60, "
		"fsm: {DoEvent: func(e, p) posted := [e, p], DoUniqueEvent: func(e, p) uposted := [e, p]}}"));
	SetFrameSlot(view, RefVar(Sym("viewIdleScript")), RefVar(NativeFunction(0xd5e4, 0, kIdle, 9)));
	SetFrameSlot(RefVar(gVarFrame), RefVar(Sym("pview")), view);
	Eval("posted := nil; uposted := nil");
	EXPECT(RINT(Eval("pview:viewIdleScript()")) == 60);
	EXPECT(EQRef(Eval("posted[0]"), Sym("tick")));
	EXPECT(EQRef(Eval("posted[1]"), Sym("pp")));
	EXPECT(RINT(Eval("pview.occurrences")) == 1);
	EXPECT(ISNIL(Eval("pview:viewIdleScript()")));		// the last occurrence
	EXPECT(RINT(Eval("pview.occurrences")) == 0);

	// unique: the event is posted only when it is not already pending
	Eval("pview.unique := 'unique; pview.occurrences := 5; posted := nil");
	EXPECT(RINT(Eval("pview:viewIdleScript()")) == 60);
	EXPECT(ISNIL(Eval("posted")));
	EXPECT(EQRef(Eval("uposted[0]"), Sym("tick")));

	// an evt.ex exception while posting is swallowed
	Eval("pview.fsm := {DoUniqueEvent: func(e, p) Throw('|evt.ex.msg|, \"boom\")}");
	EXPECT(RINT(Eval("pview:viewIdleScript()")) == 60);
	// and one while counting leaves the script answering nil
	Eval("pview.occurrences := 'many");
	EXPECT(ISNIL(Eval("pview:viewIdleScript()")));

	// KillPeriodicEvent: the engine view's child posting the event removed
	SetFrameSlot(RefVar(GetGFunctionFrame()), RefVar(Sym("RemoveStepView")),
		RefVar(Eval("func(parent, child) begin removed := [parent, child]; 'removed end")));
	RefVar machine(Eval("{fsm_private_context: {engineView: {name: 'engine, "
		"ChildViewFrames: func() [{event: 'a}, {event: 'b}]}}}"));
	SetFrameSlot(machine, RefVar(Sym("DoEvent_Check")), RefVar(NativeFunction(0xd4cc, 1, kCheck, 1)));
	SetFrameSlot(machine, RefVar(Sym("KillPeriodicEvent")), RefVar(NativeFunction(0xde38, 1, kKill, 9)));
	SetFrameSlot(RefVar(gVarFrame), RefVar(Sym("machine")), machine);
	Eval("removed := nil");
	EXPECT(EQRef(Eval("machine:KillPeriodicEvent('b)"), Sym("removed")));
	EXPECT(EQRef(Eval("removed[0].name"), Sym("engine")));
	EXPECT(EQRef(Eval("removed[1].event"), Sym("b")));
	Eval("removed := nil");
	EXPECT(ISNIL(Eval("machine:KillPeriodicEvent('z)")));
	EXPECT(ISNIL(Eval("removed")));
	// an engine view with no children: nothing
	Eval("machine.fsm_private_context.engineView := {}");
	EXPECT(ISNIL(Eval("machine:KillPeriodicEvent('a)")));
}


static void
TestProtoClone(void)
{
	static const char* const kClone[] = { "IsFrame", "IsFunction", "evt.ex.msg", "message", "Throw", "map", "f" };
	RefVar clone(NativeFunction(0x8124, 1, kClone, 7));
	SetLiteral(clone, 3, "\"ProtoClone only works with frames.\"");
	RefVar closure(GetArraySlot(clone, 3));
	RefVar lits(GetFrameSlot(closure, RefVar(Sym("_literals"))));
	SetArraySlot(lits, 5, RefVar(SharedFrameMap(RefVar(Eval("{_proto: nil}")))));
	// f, as the package has it: the function itself, in its closure
	SetFrameSlot(closure, RefVar(Sym("f")), clone);
	SetFrameSlot(RefVar(gVarFrame), RefVar(Sym("protoClone")), clone);

	Eval("orig := {a: 1, sub: {b: 2, deep: {c: 3}}, fn: func() 4, arr: [5]}");
	RefVar c(Eval("copy := call protoClone with (orig)"));
	EXPECT(IsFrame(c));
	EXPECT(EQRef(Eval("copy._proto"), Eval("orig")));
	EXPECT(RINT(Eval("copy.a")) == 1);						// through _proto
	EXPECT(ISNIL(Eval("GetSlot(copy, 'a)")));
	EXPECT(NOTNIL(Eval("GetSlot(copy, 'sub)")));				// frames copied
	EXPECT(EQRef(Eval("copy.sub._proto"), Eval("orig.sub")));
	EXPECT(EQRef(Eval("copy.sub.deep._proto"), Eval("orig.sub.deep")));
	EXPECT(ISNIL(Eval("GetSlot(copy, 'fn)")));				// functions not
	EXPECT(ISNIL(Eval("GetSlot(copy, 'arr)")));				// nor arrays
	Eval("copy.sub.b := 20");
	EXPECT(RINT(Eval("orig.sub.b")) == 2);

	// not a frame: evt.ex.msg with the message
	Ref thrown = NILREF;
	newton_try
	{
		Eval("call protoClone with ([1])");
	}
	newton_catch_all
	{
		if (Subexception(_info.exception.name, "evt.ex.msg"))
			thrown = TRUEREF;
	}
	end_try;
	EXPECT(NOTNIL(thrown));
	long caught = RINT(Eval("try call protoClone with (func() 1) onexception |evt.ex.msg| do 1"));
	EXPECT(caught == 1);
}


// A whole machine: protoFSM's methods as the package's native functions,
// states in an ancestor frame, driven by DoEvent and DoEvent_Loop.
static void
TestLoop(void)
{
	static const char* const kCheck[] = { "fsm_private_context" };
	static const char* const kQueue[] = { "queue" };
	static const char* const kDeQueue[] = { "queue", "RemoveSlot" };
	static const char* const kDoEvent[] = { "protoFSM:DoEvent", "DoEvent_Check", "pendingEventQueue", "EnQueue",
		"pendingParamsQueue", "busy", "engineView", "SetupIdle", "turtle", "Array", "AddDelayedSend" };
	static const char* const kProto[] = { "_proto" };
	static const char* const kLoop[] = { "protoFSM:DoEvent_Loop", "DoEvent_Check", "pendingState", "stateCache",
		"isNewPendingState", "ancestors", "MCollectAncestorStates", "eventCache", "pendingEventQueue", "Peek",
		"MCollectAncestorEvents", "fsm_private_event:Release", "UnknownEvent", "pendingParamsQueue", "DebugFSM",
		"UnknownState", "NilState", "currentStateFrame", "currentEventFrame", "DeQueue",
		"currentState", "currentEvent", "currentParams", "map", "engineView",
		"ChildViewFrames", "IsArray", "Array", "scope", "State",
		"RemoveStepView", "map", "action", "PreAction", "TraceFSM",
		"level", "evt.ex", "Perform", "CurrentException", "ExceptionHandler",
		"PostAction", "nextState", "waitView", "Release", "terminal",
		"Reset", "closeWaitView", "AddDelayedCall", "busy", "IsEmpty",
		"nextNoIdle", "turtle", "Dispose" };

	SetFrameSlot(RefVar(GetGFunctionFrame()), RefVar(Sym("AddDelayedSend")),
		RefVar(Eval("func(v, m, a, d) begin sent := [v, m, a, d]; nil end")));
	SetFrameSlot(RefVar(GetGFunctionFrame()), RefVar(Sym("AddDelayedCall")),
		RefVar(Eval("func(f, a, d) begin called := [f, a, d]; nil end")));
	SetFrameSlot(RefVar(GetGFunctionFrame()), RefVar(Sym("RemoveStepView")),
		RefVar(Eval("func(parent, child) AddArraySlot(removed, child)")));

	// the prototype: every method native
	RefVar proto(Eval("{}"));
	RefVar queueProto(Eval("{}"));
	SetFrameSlot(queueProto, RefVar(Sym("EnQueue")), RefVar(NativeFunction(0x7dc0, 1, kQueue, 1)));
	SetFrameSlot(queueProto, RefVar(Sym("DeQueue")), RefVar(NativeFunction(0x7b78, 0, kDeQueue, 2)));
	SetFrameSlot(queueProto, RefVar(Sym("Peek")), RefVar(NativeFunction(0x7a1c, 0, kQueue, 1)));
	SetFrameSlot(queueProto, RefVar(Sym("IsEmpty")), RefVar(NativeFunction(0x8004, 0, kQueue, 1)));
	SetFrameSlot(RefVar(gVarFrame), RefVar(Sym("queueProto")), queueProto);
	Eval("queueProto.Reset := func() queue := []");
	SetFrameSlot(proto, RefVar(Sym("DoEvent_Check")), RefVar(NativeFunction(0xd4cc, 1, kCheck, 1)));
	SetFrameSlot(proto, RefVar(Sym("DoEvent")), RefVar(NativeFunction(0x29ec, 2, kDoEvent, 11)));
	SetFrameSlot(proto, RefVar(Sym("MCollectAncestorStates")), RefVar(NativeFunction(0xe65c, 2, kProto, 1)));
	SetFrameSlot(proto, RefVar(Sym("MCollectAncestorEvents")), RefVar(NativeFunction(0xe808, 3, kProto, 1)));
	RefVar loop(NativeFunction(0x2ff8, 0, kLoop, 53));
	RefVar lits(GetFrameSlot(RefVar(GetArraySlot(loop, 3)), RefVar(Sym("_literals"))));
	RefVar map(SharedFrameMap(RefVar(Eval("{_proto: nil, _parent: nil}"))));
	SetArraySlot(lits, 23, map);
	SetArraySlot(lits, 31, map);
	SetArraySlot(lits, 46, RefVar(Eval("func(ctx) if ctx.waitView then ctx.waitView:Close()")));
	SetFrameSlot(proto, RefVar(Sym("DoEvent_Loop")), loop);
	SetFrameSlot(RefVar(gVarFrame), RefVar(Sym("fsmProto")), proto);

	Eval("trace := []; debug := []; actions := []; removed := []; handled := nil;"
		"states := {"
		"  Idle: {Go: {action: func(what) AddArraySlot(actions, what), nextState: 'Running},"
		"         Fail: {action: func() Throw('|evt.ex.msg|, \"oops\")},"
		"         Hurry: {action: func() AddArraySlot(actions, 'hurry), nextNoIdle: true}},"
		"  Running: {Stop: {action: func() AddArraySlot(actions, 'stop), nextState: 'Idle}}"
		"};"
		"engine := {ChildViewFrames: func() [{scope: 'State, n: 1}, {scope: 'Machine, n: 2}]};"
		"machine := {_proto: fsmProto, currentStateFrame: nil, currentEventFrame: nil,"
		"  TraceFSM: func(what, s, e, p) AddArraySlot(trace, what),"
		"  DebugFSM: func(why, s, e, p) AddArraySlot(debug, [why, s, e]),"
		"  ExceptionHandler: func(ex) handled := ex,"
		"  fsm_private_context: {pendingState: 'Idle, stateCache: nil, isNewPendingState: true,"
		"    ancestors: [states], eventCache: nil, busy: nil, engineView: engine, level: 0, turtle: 5,"
		"    waitView: nil, Release: nil,"
		"    pendingEventQueue: {_proto: queueProto, queue: []},"
		"    pendingParamsQueue: {_proto: queueProto, queue: []}}}");

	// one event: its action done, the next state pending, nothing more to do
	Eval("sent := nil; machine:DoEvent('Go, ['hello])");
	EXPECT(EQRef(Eval("sent[1]"), Sym("SetupIdle")));
	EXPECT(ISNIL(Eval("machine:DoEvent_Loop()")));
	EXPECT(EQRef(Eval("actions[0]"), Sym("hello")));
	EXPECT(EQRef(Eval("machine.fsm_private_context.pendingState"), Sym("Running")));
	EXPECT(EQRef(Eval("machine.fsm_private_context.currentState"), Sym("Idle")));
	EXPECT(NOTNIL(Eval("machine.fsm_private_context.isNewPendingState")));
	EXPECT(ISNIL(Eval("machine.fsm_private_context.busy")));
	EXPECT(RINT(Eval("machine.fsm_private_context.level")) == 0);
	EXPECT(EQRef(Eval("trace[0]"), Sym("PreAction")));
	EXPECT(EQRef(Eval("trace[1]"), Sym("PostAction")));
	EXPECT(EQRef(Eval("trace[2]"), Sym("nextState")));
	EXPECT(RINT(Eval("Length(removed)")) == 1);			// the State-scoped child view
	EXPECT(RINT(Eval("removed[0].n")) == 1);
	EXPECT(EQRef(Eval("machine.currentStateFrame._parent"), Eval("machine")));
	EXPECT(EQRef(Eval("machine.currentEventFrame._parent"), Eval("machine.currentStateFrame")));

	// two events: the first done, the machine still busy, the idle the turtle
	Eval("machine:DoEvent('Stop, []); machine:DoEvent('Bogus, [])");
	EXPECT(RINT(Eval("machine:DoEvent_Loop()")) == 5);
	EXPECT(EQRef(Eval("actions[1]"), Sym("stop")));
	EXPECT(NOTNIL(Eval("machine.fsm_private_context.busy")));
	// an event the state does not know: DebugFSM, and the event dropped
	EXPECT(ISNIL(Eval("machine:DoEvent_Loop()")));
	EXPECT(EQRef(Eval("debug[0][0]"), Sym("UnknownEvent")));
	EXPECT(EQRef(Eval("debug[0][1]"), Sym("Idle")));
	EXPECT(EQRef(Eval("debug[0][2]"), Sym("Bogus")));
	EXPECT(RINT(Eval("Length(machine.fsm_private_context.pendingEventQueue.queue)")) == 0);
	EXPECT(ISNIL(Eval("machine.currentEventFrame")));

	// an action that throws: the machine's ExceptionHandler told
	Eval("machine:DoEvent('Fail, [])");
	EXPECT(ISNIL(Eval("machine:DoEvent_Loop()")));
	EXPECT(NOTNIL(Eval("handled")));
	EXPECT(EQRef(Eval("handled.name"), Sym("evt.ex.msg")));
	EXPECT(RINT(Eval("machine.fsm_private_context.level")) == 0);

	// nextNoIdle: the next event is taken straight away
	Eval("machine:DoEvent('Hurry, []); machine:DoEvent('Go, ['again])");
	EXPECT(ISNIL(Eval("machine:DoEvent_Loop()")));
	EXPECT(EQRef(Eval("actions[2]"), Sym("hurry")));
	EXPECT(EQRef(Eval("actions[3]"), Sym("again")));

	// a wait view and a terminal state: the queues reset, the view closed later
	Eval("states.Done := {terminal: true};"
		"states.Running.Finish := {nextState: 'Done};"
		"machine.fsm_private_context.waitView := {Close: func() closed := true};"
		"called := nil; machine:DoEvent('Finish, []); machine:DoEvent('Stop, [])");
	EXPECT(ISNIL(Eval("machine:DoEvent_Loop()")));
	EXPECT(EQRef(Eval("machine.fsm_private_context.pendingState"), Sym("Done")));
	EXPECT(RINT(Eval("Length(machine.fsm_private_context.pendingEventQueue.queue)")) == 0);
	EXPECT(NOTNIL(Eval("called")));
	Eval("closed := nil; call called[0] with (called[1][0])");
	EXPECT(NOTNIL(Eval("closed")));

	// no context: nil
	Eval("machine.fsm_private_context := nil");
	EXPECT(ISNIL(Eval("machine:DoEvent_Loop()")));
}


// ObjectToString's f, for the test: what it can see, then "x: 1, "
static const ULong kProbeOffset = 0x10;		// (inside GetGInterpreter's code: no function starts there)

static Ref
ProbePrinter(RefArg rcvr, RefArg x, RefArg depth, RefArg closure)
{
	RefVar env(NIEEnvironment(closure));
	RefVar seen(MakeArray(7));
	SetArraySlot(seen, 0, x);
	SetArraySlot(seen, 1, depth);
	for (long i = 0; i < 5; i++)
		SetArraySlot(seen, 2 + i, RefVar(NIEFindVariable(env, RefVar(NIELiteral(closure, i)))));
	SetFrameSlot(RefVar(gVarFrame), RefVar(Sym("seen")), seen);
	if (EQRef(x, Sym("boom")))
		ThrowMsg((char*) "no");
	if (EQRef(x, Sym("mem")))
		Throw((ExceptionName) "evt.ex.outofmem", nil, nil);
	return MakeString("x: 1, ");
}


static void
TestObjectToString(void)
{
	static const char* const kObj[] = { "backIndex", "Array", "backList", "printProto", "GetGlobalVar",
		"runProto", "printParent", "runParent", "printReal", "floatFormat",
		"IsString", "fmt", "printDepth", "maxDepth", "IsNumber",
		"n", "printLength", "maxLength", "p", "p",
		"f", "f", "evt.ex.outofmem", "evt.ex", "nomem", "exc" };
	static const char* const kTrim[] = { "sep", "EndsWith", "StrLen", "StrMunger" };

	RefVar fn(NativeFunction(0xc498, 1, kObj, 26));
	// the closure with the function's locals after its literals, as NTK lays it out
	RefVar lits(GetFrameSlot(RefVar(GetArraySlot(fn, 3)), RefVar(Sym("_literals"))));
	RefVar closure(Eval("{_nextArgFrame: nil, _parent: nil, _implementor: nil, _literals: nil, backIndex: nil,"
		" backList: nil, maxDepth: nil, maxLength: nil, runParent: nil, runProto: nil, f: nil, p: nil, floatFormat: nil}"));
	SetFrameSlot(closure, RefVar(Sym("_literals")), lits);
	SetArraySlot(fn, 3, closure);
	SetLiteral(fn, 11, "\"%.16e\"");
	SetLiteral(fn, 15, "65535");
	SetLiteral(fn, 24, "\"<insufficient memory>\"");
	SetLiteral(fn, 25, "\"<exception occurred>\"");
	RefVar trim(NativeFunction(0x8670, 1, kTrim, 4));
	SetLiteral(trim, 0, "\", \"");
	SetArraySlot(lits, 18, trim);
	SetFrameSlot(RefVar(gVarFrame), RefVar(Sym("objToString")), fn);
	SetFrameSlot(RefVar(gVarFrame), RefVar(Sym("vars")), RefVar(gVarFrame));		// (GetGlobalVar reads vars)
	Eval("seen := nil; printDepth := nil; printLength := nil; printReal := nil; printProto := nil; printParent := nil");

	// (not on the CPU: the package's own code calls a native function of its
	// binary straight through NativeEntry, so a host double registered at
	// an offset of it is never asked there)
	if (!gOnCPU)
	{
		// f, a test double in native code (registered at an offset of the
		// binary no function uses), sees the settings through its lexical scope
		// as the package's own printer does
		static const char* const kProbe[] = { "maxDepth", "maxLength", "floatFormat", "runProto", "backList" };
		RefVar probe(NativeFunction(kProbeOffset, 2, kProbe, 5));
		SetArraySlot(lits, 20, probe);
		Eval("seen := nil; first := call objToString with ('a)");
		EXPECT(NOTNIL(Eval("StrEqual(first, \"x: 1\")")));
		EXPECT(EQRef(Eval("seen[0]"), Sym("a")));
		EXPECT(RINT(Eval("seen[1]")) == 0);
		EXPECT(RINT(Eval("seen[2]")) == 65535);
		EXPECT(RINT(Eval("seen[3]")) == 65535);
		EXPECT(NOTNIL(Eval("StrEqual(seen[4], \"%.16e\")")));
		EXPECT(ISNIL(Eval("seen[5]")));
		EXPECT(RINT(Eval("Length(seen[6])")) == 1);
		// the global settings
		Eval("printDepth := 3; printLength := -1; printReal := \"%g\"; printProto := 'yes");
		Eval("call objToString with ('a)");
		EXPECT(RINT(Eval("seen[2]")) == 3);
		EXPECT(RINT(Eval("seen[3]")) == 65535);
		EXPECT(NOTNIL(Eval("StrEqual(seen[4], \"%g\")")));
		EXPECT(NOTNIL(Eval("seen[5]")));
		Eval("printDepth := nil; printLength := nil; printReal := nil; printProto := nil");
		// exceptions become a message
		EXPECT(NOTNIL(Eval("StrEqual(call objToString with ('boom), \"<exception occurred>\")")));
		EXPECT(NOTNIL(Eval("StrEqual(call objToString with ('mem), \"<insufficient memory>\")")));
	}

	// the package's own printer (0x14649), its literals as NewtonScript source
	static const char* const kPrinterLits[] = {
		"'IsValid", "\"<invalid object reference>\"", "'IsMagicPtr", "\"@+\"", "'refOf",
		"'Array", "'IsFunction", "'GetFunctionArgCount", "\"func(^0 arg^?1|s|)\"", "'ParamStr",
		"'IsFrame", "'backList", "'|=|", "'LSearch", "'backIndex",
		"\"<\"", "\">\"", "\"{<\"", "\"> \"", "'maxDepth",
		"\"+\"", "'maxLength", "\"...\"", "\": \"", "'_parent",
		"'runParent", "'_proto", "'runProto", "\"<ignored>, \"", "'f",
		"'p", "\"}\"", "'IsArray", "\"<\"", "\">\"",
		"\"[<\"", "\"> \"", "\": \"", "\"+\"", "\"...\"",
		"\"]\"", "'IsString", "'IsSymbol", "'IsInteger", "\"+\"",
		"\"\"", "'NumberStr", "'IsNumber", "\"+\"", "\"\"",
		"'floatFormat", "'FormattedNumberStr", "'IsImmediate", "\"nil\"", "\"true\"",
		"'SPrintObject", "'IsBinary", "\"<\"", "\", length \"", "\">\"",
		"\", \"", "'stringer" };
	static const char* const kNone[] = { "x" };
	RefVar printer(NativeFunction(0x8898, 2, kNone, 1));
	RefVar printerLits(MakeArray(62));
	for (long i = 0; i < 62; i++)
		SetArraySlot(printerLits, i, RefVar(Eval(kPrinterLits[i])));
	SetFrameSlot(RefVar(GetArraySlot(printer, 3)), RefVar(Sym("_literals")), printerLits);
	SetArraySlot(lits, 20, printer);
	Eval("printed := call objToString with ({a: 1, b: \"x\", c: [2, 'y], d: {_parent: 'hidden}, e: nil})");
	RefVar printed(Eval("printed"));
	if (IsString(printed))
	{
		const UniChar* u = (const UniChar*) BinaryData(printed);
		printf("  ObjectToString: ");
		for (long i = 0; u[i] != 0 && i < 200; i++)
			putchar(u[i] < 128 ? (int) u[i] : '?');
		printf("\n");
	}
	EXPECT(NOTNIL(Eval("StrEqual(printed, \"{<1> a: +1, b: \\\"x\\\", c: [<2> +2, 'y], d: {<3> _parent: <ignored>}, e: nil}\")")));
	// a frame met again is printed as its index in the back list
	Eval("cyc := {a: 1}; cyc.me := cyc");
	EXPECT(NOTNIL(Eval("StrEqual(call objToString with (cyc), \"{<1> a: +1, me: <1>}\")")));
	EXPECT(NOTNIL(Eval("StrEqual(call objToString with ('sym), \"'sym\")")));
	EXPECT(NOTNIL(Eval("StrEqual(call objToString with (-3), \"-3\")")));
	EXPECT(NOTNIL(Eval("StrEqual(call objToString with (true), \"true\")")));
	EXPECT(NOTNIL(Eval("StrEqual(call objToString with (func(a, b) a), \"func(2 args)\")")));
	EXPECT(NOTNIL(Eval("StrEqual(call objToString with (func(a) a), \"func(1 arg)\")")));
	// printDepth: deeper frames only by reference
	Eval("printDepth := 0");
	EXPECT(NOTNIL(Eval("BeginsWith(call objToString with ({a: {b: 1}}), \"{<1> a: {<2> +\")")));
	Eval("printDepth := nil; printLength := 1");
	EXPECT(NOTNIL(Eval("StrEqual(call objToString with ([1, 2, 3]), \"[<1> +1, ...]\")")));
	Eval("printLength := nil");
}


// Each group of checks; an exception stops only its own group.  Answers the
// number of groups an exception stopped.
static int
RunAll(void)
{
	static const struct { const char* name; void (*test)(void); } kTests[] = {
		{ "queue", TestQueue }, { "engine", TestEngine }, { "events", TestEvents },
		{ "periodic", TestPeriodic }, { "ProtoClone", TestProtoClone }, { "loop", TestLoop },
		{ "ObjectToString", TestObjectToString } };
	int stopped = 0;
	for (size_t i = 0; i < sizeof(kTests) / sizeof(kTests[0]); i++)
	{
		newton_try
		{
			kTests[i].test();
		}
		newton_catch_all
		{
			stopped++;
			failures++;
			printf("FAIL %s: exception %s\n", kTests[i].name, CurrentException()->name);
			if (Subexception(CurrentException()->name, "evt.ex.fr"))
			{
				RefStruct* data = (RefStruct*) CurrentException()->data;
				if (data != nil && IsFrame(*data))
				{
					printf("  errorCode %ld\n", (long) RINT(GetFrameSlot(*data, RSSYMerrorcode)));
					RefVar sym(GetFrameSlot(*data, RefVar(Sym("symbol"))));
					if (IsSymbol(sym))
						printf("  symbol %s\n", SymbolName(sym));
				}
			}
		}
		end_try;
	}
	return stopped;
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
	RegisterSoupNatives();		// (IsValid, which the printer asks)
	if (getenv("NIE_TEST_REP") != nil)		// (the REP on stdout: NEWTON_TRACE_EXCEPTIONS then prints each throw)
		HostInitREP(stdout);

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

	// first the oracle: the package's own ARM code on the CPU interpreter
	// (src/armcpu), nothing registered but the test's printer double; the
	// same checks must pass there - a check that fails on one side and not
	// the other is a re-expression (or the CPU) that is wrong
	RegisterPackageNative(kNIECodeLength, kNIECodeHash, kProbeOffset, (void*) ProbePrinter, 3, "(test) f");
	InstallPackageNativeCPU();
	gOnCPU = true;
	printf("test_NIEProtoFSM: on the CPU (the package's own code):\n");
	int before = failures;
	RunAll();
	printf("test_NIEProtoFSM: %d check(s) failed on the CPU\n", failures - before);

	// then the re-expressions
	gOnCPU = false;
	SetPackageNativeFallback(nil);
	RegisterNIENatives();
	// the re-expressions are what run: no CPU fallback now (a function the
	// registry missed would throw, not be emulated), and every function the
	// package holds is found with its own argument count
	EXPECT(GetPackageNativeFallback() == nil);
	{
		static const struct { ULong offset; long numArgs; } kAll[] = {
			{ 0x29ec, 3 }, { 0x2ff8, 1 }, { 0x7a1c, 1 }, { 0x7b78, 1 }, { 0x7dc0, 2 }, { 0x7ef0, 1 },
			{ 0x8004, 1 }, { 0x8124, 2 }, { 0x8898, 3 }, { 0x8670, 2 }, { 0xc498, 2 }, { 0xd4cc, 2 },
			{ 0xd5e4, 1 }, { 0xdbdc, 1 }, { 0xde38, 2 }, { 0xe43c, 1 }, { 0xe65c, 3 }, { 0xe808, 4 }, { 0xe9f0, 3 } };
		for (size_t i = 0; i < sizeof(kAll) / sizeof(kAll[0]); i++)
		{
			long numArgs = -1;
			void* fn = FindPackageNative(gCode, kAll[i].offset, &numArgs, nil);
			if (fn == nil || numArgs != kAll[i].numArgs)
				printf("  offset %#lx: %s\n", (unsigned long) kAll[i].offset, fn == nil ? "not registered" : "wrong argument count");
			EXPECT(fn != nil && numArgs == kAll[i].numArgs);
		}
	}
	RunAll();

	printf("test_NIEProtoFSM: %s\n", failures == 0 ? "all passed" : "FAILED");
	return failures != 0;
}
