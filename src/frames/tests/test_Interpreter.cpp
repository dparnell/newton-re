// Interpreter test: the object system over the ROM image (InitObjects
// starts the interpreter and binds the core built-ins); then NewtonScript
// functions assembled here (a small bytecode assembler with labels) -
// arithmetic, locals, loops, frames and arrays, paths, globals, sends and
// _proto inheritance, closures, exception handlers - and functions of the
// ROM's own (built-in NewtonScript functions that need only what is
// reconstructed) run through NSCall and friends.

#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "ROMImport.h"
#include "ROMConstants.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static Ref SYMBOL(const char* name) { return Intern((char*) name); }


/* -------------------------------------------------------------------------------
	A bytecode assembler
	An instruction is a << 3 | b; b == 7 means a 16-bit big-endian operand
	follows.  Branch targets are labels resolved when the function is made;
	a handler's pc (new-handlers takes it as an integer on the stack) too.
------------------------------------------------------------------------------- */

class Asm
{
public:
	Asm() : fSize(0), fNumLabels(0), fNumFixups(0), fLiterals(AllocateArray(SYMBOL("literals"), 0)) { }

	// an instruction with a small (0-6) or a 16-bit operand
	Asm&	op(long a, long b)
	{
		if (b >= 0 && b < 7)
			fCode[fSize++] = (unsigned char) (a << 3 | b);
		else
		{
			fCode[fSize++] = (unsigned char) (a << 3 | 7);
			fCode[fSize++] = (unsigned char) ((b >> 8) & 0xff);
			fCode[fSize++] = (unsigned char) (b & 0xff);
		}
		return *this;
	}
	Asm&	simple(long b)				{ return op(0, b); }
	Asm&	pushConstant(Ref r)			{ return op(kBCPushConstant, (long) r & 0xffff); }
	Asm&	pushInt(long i)				{ return pushConstant(MAKEINT(i)); }
	Asm&	freq(long fn)				{ return op(kBCFreqFunc, fn); }
	// a literal: pushed, or named for find-var and friends
	long	literal(Ref r)
	{
		long index = Length(fLiterals);
		AddArraySlot(fLiterals, RefVar(r));
		return index;
	}
	Asm&	push(Ref r)					{ return op(kBCPush, literal(r)); }
	Asm&	opLiteral(long a, Ref r)	{ return op(a, literal(r)); }
	// labels: a branch is emitted in the 16-bit form and fixed up at function()
	long	newLabel()					{ fLabels[fNumLabels] = -1; return fNumLabels++; }
	Asm&	label(long l)				{ fLabels[l] = fSize; return *this; }
	Asm&	branch(long a, long l)		{ return fixup(a, l, false); }
	Asm&	pushLabel(long l)			{ return fixup(kBCPushConstant, l, true); }		// MAKEINT(pc) of the label
	// the function object: [class, instructions, literals, argFrame, numArgs | numLocals << 16]
	Ref		function(long numArgs, long numLocals, RefArg argFrame = RefVar(NILREF))
	{
		for (long i = 0; i < fNumFixups; i++)
		{
			long target = fLabels[fFixups[i].fLabel];
			if (target < 0)
				printf("Asm: unresolved label\n");
			if (fFixups[i].fAsInt)
				target = (long) MAKEINT(target);
			fCode[fFixups[i].fAt + 1] = (unsigned char) (target >> 8);
			fCode[fFixups[i].fAt + 2] = (unsigned char) target;
		}
		RefVar fn(AllocateArray(RefVar(NILREF), 5));
		SetArraySlotRef(fn, kFunctionClassSlot, kFuncClass);
		RefVar instructions(AllocateBinary(SYMBOL("instructions"), fSize));
		memcpy(BinaryData(instructions), fCode, fSize);
		SetArraySlotRef(fn, kFunctionInstructionsSlot, instructions);
		SetArraySlotRef(fn, kFunctionLiteralsSlot, fLiterals);
		SetArraySlotRef(fn, kFunctionArgFrameSlot, argFrame);
		SetArraySlotRef(fn, kFunctionNumArgsSlot, MAKEINT(numArgs | (numLocals << 16)));
		return fn;
	}

private:
	struct Fixup { long fAt; long fLabel; Boolean fAsInt; };
	Asm&	fixup(long a, long l, Boolean asInt)
	{
		fFixups[fNumFixups].fAt = fSize;
		fFixups[fNumFixups].fLabel = l;
		fFixups[fNumFixups].fAsInt = asInt;
		fNumFixups++;
		return op(a, 0x7fff);
	}

	unsigned char	fCode[256];
	long			fSize;
	long			fLabels[16];
	long			fNumLabels;
	Fixup			fFixups[16];
	long			fNumFixups;
	RefVar			fLiterals;
};


// what a call throws, as an exception name (nil for nothing) and its data.
// As in the ROM, an exception nothing in NewtonScript handles leaves the
// call's arguments on the value stack (Run restores the depth it was
// entered at, after the arguments were pushed), so the stack is reset here
// to what it was before the call.
static const char* gThrown = nil;
static long gThrownCode = 0;
#define THROWS(expr, exname) do { gThrown = nil; long valueDepth = gInterpreter->ValuePosition(); newton_try { (void) (expr); } newton_catch((ExceptionName) exname) { gThrown = _info.exception.name; gThrownCode = (long) (Long) _info.exception.data; } end_try; gInterpreter->fValueStack.Reset(valueDepth); } while (0)

// the interpreter back where it started: nothing on the value stack, one state
#define EXPECT_STACKS_CLEAN() do { if (gInterpreter->ValuePosition() != -1 || gInterpreter->ControlPosition() != 5) { failures++; fprintf(stderr, "FAIL %s:%d: stacks value %ld control %ld\n", __FILE__, __LINE__, gInterpreter->ValuePosition(), gInterpreter->ControlPosition()); } } while (0)

// an argFrame with the three hidden slots and then the named ones
static Ref
MakeArgFrame(const char* a = nil, const char* b = nil)
{
	RefVar tags(AllocateArray(RSSYMarray, 0));
	AddArraySlot(tags, RefVar(RSSYM_nextargframe));
	AddArraySlot(tags, RefVar(RSSYM_parent));
	AddArraySlot(tags, RefVar(SYMBOL("_implementor")));
	if (a != nil)
		AddArraySlot(tags, RefVar(SYMBOL(a)));
	if (b != nil)
		AddArraySlot(tags, RefVar(SYMBOL(b)));
	return AllocateFrameWithMap(RefVar(AllocateMapWithTags(RefVar(NILREF), tags)));
}


static void
TestArithmetic()
{
	// fn(a, b) a + b * 2
	Asm a;
	a.op(kBCGetVar, 3).op(kBCGetVar, 4).pushInt(2).freq(kFFMultiply).freq(kFFAdd).simple(kBCReturn);
	RefVar fn(a.function(2, 0));
	EXPECT(IsFunction(fn) && GetFunctionArgCount(fn) == 2);
	EXPECT(RINT(NSCall(fn, RefVar(MAKEINT(3)), RefVar(MAKEINT(4)))) == 11);
	EXPECT(RINT(NSCall(fn, RefVar(MAKEINT(-3)), RefVar(MAKEINT(4)))) == 5);
	RefVar real(NSCall(fn, RefVar(MakeReal(1.5)), RefVar(MAKEINT(1))));
	EXPECT(ISREAL(real) && CDouble(real) == 3.5);
	EXPECT_STACKS_CLEAN();

	// the wrong number of arguments
	THROWS(NSCall(fn, RefVar(MAKEINT(1))), "evt.ex.fr.intrp");
	EXPECT(gThrown != nil && gThrownCode == kNSErrWrongNumberOfArgs);
	EXPECT_STACKS_CLEAN();

	// comparison and not: fn(a) if a > 10 then 'big else if not (a = 10) then 'small else 'ten
	Asm b;
	long notBig = b.newLabel(), notSmall = b.newLabel();
	b.op(kBCGetVar, 3).pushInt(10).freq(kFFGreaterThan).branch(kBCBranchIfFalse, notBig);
	b.push(SYMBOL("big")).simple(kBCReturn);
	b.label(notBig).op(kBCGetVar, 3).pushInt(10).freq(kFFEquals).freq(kFFNot).branch(kBCBranchIfFalse, notSmall);
	b.push(SYMBOL("small")).simple(kBCReturn);
	b.label(notSmall).push(SYMBOL("ten")).simple(kBCReturn);
	RefVar fn2(b.function(1, 0));
	EXPECT(NSCall(fn2, RefVar(MAKEINT(11))) == SYMBOL("big"));
	EXPECT(NSCall(fn2, RefVar(MAKEINT(3))) == SYMBOL("small"));
	EXPECT(NSCall(fn2, RefVar(MAKEINT(10))) == SYMBOL("ten"));
	EXPECT_STACKS_CLEAN();
}


static void
TestLoops()
{
	// fn(n) local s := 0; for i := 1 to n do s := s + i; s
	// locals: 3 n, 4 s, 5 i, 6 limit, 7 incr
	Asm a;
	long body = a.newLabel(), done = a.newLabel();
	a.pushInt(0).op(kBCSetVar, 4);												// s := 0
	a.pushInt(1).op(kBCSetVar, 5);												// i := 1
	a.op(kBCGetVar, 3).op(kBCSetVar, 6);										// limit := n
	a.pushInt(1).op(kBCSetVar, 7);												// incr := 1
	a.op(kBCGetVar, 7).op(kBCGetVar, 5).op(kBCGetVar, 6).branch(kBCBranchIfLoopNotDone, body);	// incr, index, limit
	a.branch(kBCBranch, done);
	a.label(body).op(kBCGetVar, 4).op(kBCGetVar, 5).freq(kFFAdd).op(kBCSetVar, 4);	// s := s + i
	a.op(kBCGetVar, 7).op(kBCIncrVar, 5).op(kBCGetVar, 6).branch(kBCBranchIfLoopNotDone, body);	// incr-var leaves incr, index
	a.label(done).op(kBCGetVar, 4).simple(kBCReturn);
	RefVar fn(a.function(1, 4));
	EXPECT(RINT(NSCall(fn, RefVar(MAKEINT(10)))) == 55);
	EXPECT(RINT(NSCall(fn, RefVar(MAKEINT(0)))) == 0);
	EXPECT(RINT(NSCall(fn, RefVar(MAKEINT(100)))) == 5050);
	EXPECT_STACKS_CLEAN();

	// foreach: fn(arr) local s := 0; foreach v in arr do s := s + v; s
	// locals: 3 arr, 4 s, 5 iter, 6 v
	Asm b;
	long loop = b.newLabel(), test = b.newLabel();
	b.pushInt(0).op(kBCSetVar, 4);
	b.op(kBCGetVar, 3).pushConstant(NILREF).freq(kFFNewIterator).op(kBCSetVar, 5);
	b.branch(kBCBranch, test);
	b.label(loop).op(kBCGetVar, 5).pushInt(1).freq(kFFAref).op(kBCSetVar, 6);	// v := iter[1]
	b.op(kBCGetVar, 4).op(kBCGetVar, 6).freq(kFFAdd).op(kBCSetVar, 4);
	b.op(kBCGetVar, 5).simple(kBCIterNext);
	b.label(test).op(kBCGetVar, 5).simple(kBCIterDone).branch(kBCBranchIfFalse, loop);
	b.op(kBCGetVar, 4).simple(kBCReturn);
	RefVar fn2(b.function(1, 3));
	RefVar arr(AllocateArray(RSSYMarray, 4));
	for (long i = 0; i < 4; i++)
		SetArraySlotRef(arr, i, MAKEINT(i * 10));
	EXPECT(RINT(NSCall(fn2, arr)) == 60);
	RefVar empty(AllocateArray(RSSYMarray, 0));
	EXPECT(RINT(NSCall(fn2, empty)) == 0);
	// a frame's values too
	RefVar frame(AllocateFrame());
	SetFrameSlot(frame, SYMBOL("a"), RefVar(MAKEINT(1)));
	SetFrameSlot(frame, SYMBOL("b"), RefVar(MAKEINT(2)));
	EXPECT(RINT(NSCall(fn2, frame)) == 3);
	EXPECT_STACKS_CLEAN();
}


static void
TestObjects()
{
	// fn(x) local f := {a: x, b: [x, 2]}; f.b[1] := f.a + 1; return f
	// locals: 3 x, 4 f
	RefVar tags(AllocateArray(RSSYMarray, 2));
	SetArraySlotRef(tags, 0, SYMBOL("a"));
	SetArraySlotRef(tags, 1, SYMBOL("b"));
	RefVar map(AllocateMapWithTags(RefVar(NILREF), tags));
	Asm a;
	a.op(kBCGetVar, 3);																// x
	a.op(kBCGetVar, 3).pushInt(2).push(RSSYMarray).op(kBCMakeArray, 2);				// [x, 2] (class 'Array)
	a.push(map).op(kBCMakeFrame, 2).op(kBCSetVar, 4);								// f := {a: x, b: [..]}
	a.op(kBCGetVar, 4).push(SYMBOL("b")).op(kBCGetPath, 0);							// f.b
	a.pushInt(1);
	a.op(kBCGetVar, 4).push(SYMBOL("a")).op(kBCGetPath, 0).pushInt(1).freq(kFFAdd);	// f.a + 1
	a.freq(kFFSetAref).simple(kBCPop);												// f.b[1] := ...
	a.op(kBCGetVar, 4).simple(kBCReturn);
	RefVar fn(a.function(1, 1));
	RefVar result(NSCall(fn, RefVar(MAKEINT(5))));
	EXPECT(IsFrame(result) && RINT(GetFrameSlot(result, SYMBOL("a"))) == 5);
	RefVar b(GetFrameSlot(result, SYMBOL("b")));
	EXPECT(IsArray(b) && Length(b) == 2 && RINT(GetArraySlot(b, 0)) == 5 && RINT(GetArraySlot(b, 1)) == 6);
	EXPECT(EQ(ClassOf(b), RSSYMarray));
	EXPECT_STACKS_CLEAN();

	// set-path with the value kept, get-path of nil
	Asm c;
	c.op(kBCGetVar, 3).push(SYMBOL("z")).pushInt(9).op(kBCSetPath, 1).simple(kBCReturn);
	RefVar fn2(c.function(1, 0));
	RefVar target(AllocateFrame());
	EXPECT(RINT(NSCall(fn2, target)) == 9 && RINT(GetFrameSlot(target, SYMBOL("z"))) == 9);
	Asm d;
	d.op(kBCGetVar, 3).push(SYMBOL("z")).op(kBCGetPath, 0).simple(kBCReturn);
	RefVar fn3(d.function(1, 0));
	EXPECT(ISNIL(NSCall(fn3, RefVar(NILREF))));
	EXPECT(RINT(NSCall(fn3, target)) == 9);
	// the aref of a string is a character; out of bounds throws
	Asm e;
	e.op(kBCGetVar, 3).op(kBCGetVar, 4).freq(kFFAref).simple(kBCReturn);
	RefVar fn4(e.function(2, 0));
	RefVar str(MakeString("hey"));
	EXPECT(RCHAR(NSCall(fn4, str, RefVar(MAKEINT(1)))) == 'e');
	THROWS(NSCall(fn4, str, RefVar(MAKEINT(5))), "evt.ex.fr");
	EXPECT(gThrown != nil);
	EXPECT_STACKS_CLEAN();
}


static void
TestGlobalsAndCalls()
{
	// a global variable and function, through the bytecodes
	RefVar vars(gVarFrame);
	SetFrameSlot(vars, SYMBOL("gCount"), RefVar(MAKEINT(1)));
	// fn() gCount := gCount + 1; Length([1, 2, 3])
	Asm a;
	a.opLiteral(kBCFindVar, SYMBOL("gCount")).pushInt(1).freq(kFFAdd).opLiteral(kBCFindAndSetVar, SYMBOL("gCount"));
	a.pushInt(1).pushInt(2).pushInt(3).push(RSSYMarray).op(kBCMakeArray, 3);
	a.push(SYMBOL("Length")).op(kBCCall, 1).simple(kBCReturn);
	RefVar fn(a.function(0, 0));
	EXPECT(RINT(NSCall(fn)) == 3);
	EXPECT(RINT(GetFrameSlot(vars, SYMBOL("gCount"))) == 2);
	EXPECT(RINT(NSCall(fn)) == 3 && RINT(GetFrameSlot(vars, SYMBOL("gCount"))) == 3);
	EXPECT_STACKS_CLEAN();
	// an undefined variable and an undefined function
	Asm b;
	b.opLiteral(kBCFindVar, SYMBOL("noSuchVariable")).simple(kBCReturn);
	RefVar fn2(b.function(0, 0));
	THROWS(NSCall(fn2), "evt.ex.fr.intrp");
	EXPECT(gThrown != nil);
	Asm c;
	c.push(SYMBOL("noSuchFunction")).op(kBCCall, 0).simple(kBCReturn);
	RefVar fn3(c.function(0, 0));
	THROWS(NSCall(fn3), "evt.ex.fr.intrp");
	EXPECT(gThrown != nil);
	EXPECT_STACKS_CLEAN();
	// a global function defined here, called by another
	RefVar functions(gFunctionFrame);
	Asm twice;
	twice.op(kBCGetVar, 3).pushInt(2).freq(kFFMultiply).simple(kBCReturn);
	SetFrameSlot(functions, SYMBOL("Twice"), RefVar(twice.function(1, 0)));
	Asm d;
	d.op(kBCGetVar, 3).push(SYMBOL("Twice")).op(kBCCall, 1).push(SYMBOL("Twice")).op(kBCCall, 1).simple(kBCReturn);
	RefVar fn4(d.function(1, 0));
	EXPECT(RINT(NSCall(fn4, RefVar(MAKEINT(5)))) == 20);
	EXPECT(RINT(NSCallGlobalFn(RefVar(SYMBOL("Twice")), RefVar(MAKEINT(7)))) == 14);
	// invoke: the function itself on the stack
	Asm e;
	e.op(kBCGetVar, 4).op(kBCGetVar, 3).op(kBCInvoke, 1).simple(kBCReturn);
	RefVar fn5(e.function(2, 0));
	EXPECT(RINT(NSCall(fn5, RefVar(GetFrameSlot(functions, SYMBOL("Twice"))), RefVar(MAKEINT(4)))) == 8);
	// natives called directly
	EXPECT(RINT(NSCallGlobalFn(RefVar(SYMBOL("Max")), RefVar(MAKEINT(3)), RefVar(MAKEINT(9)))) == 9);
	EXPECT(NSCallGlobalFn(RefVar(SYMBOL("ClassOf")), RefVar(MAKEINT(3))) == RSSYMint);
	// an unreconstructed native
	THROWS(NSCallGlobalFn(RefVar(SYMBOL("Sleep")), RefVar(MAKEINT(1))), "evt.ex.fr.intrp");
	EXPECT(gThrown != nil && gThrownCode == kNSErrNativeNotReconstructed);
	EXPECT_STACKS_CLEAN();
}


static void
TestSends()
{
	// a frame with a method reading its slots: {x: 10, m: func(a) x + a}
	Asm method;
	method.opLiteral(kBCFindVar, SYMBOL("x")).op(kBCGetVar, 3).freq(kFFAdd).simple(kBCReturn);
	RefVar m(method.function(1, 0));
	RefVar frame(AllocateFrame());
	SetFrameSlot(frame, SYMBOL("x"), RefVar(MAKEINT(10)));
	SetFrameSlot(frame, SYMBOL("m"), m);
	EXPECT(RINT(NSSend(frame, RefVar(SYMBOL("m")), RefVar(MAKEINT(5)))) == 15);
	// through a _proto, with self's own x
	RefVar child(AllocateFrame());
	SetFrameSlot(child, RSSYM_proto, frame);
	SetFrameSlot(child, SYMBOL("x"), RefVar(MAKEINT(100)));
	EXPECT(RINT(NSSend(child, RefVar(SYMBOL("m")), RefVar(MAKEINT(5)))) == 105);
	EXPECT(ISNIL(NSSendIfDefined(child, RefVar(SYMBOL("nothing")))));
	THROWS(NSSend(child, RefVar(SYMBOL("nothing"))), "evt.ex.fr.intrp");
	EXPECT(gThrown != nil);
	EXPECT_STACKS_CLEAN();
	// send from bytecode: fn(f) f:m(1) + f:?m(2)  (args, receiver, message)
	Asm a;
	a.pushInt(1).op(kBCGetVar, 3).push(SYMBOL("m")).op(kBCSend, 1);
	a.pushInt(2).op(kBCGetVar, 3).push(SYMBOL("m")).op(kBCSendIfDefined, 1);
	a.freq(kFFAdd).simple(kBCReturn);
	RefVar fn(a.function(1, 0));
	EXPECT(RINT(NSCall(fn, child)) == 101 + 102);
	// self, and a method setting a slot of self (find-and-set-var on the receiver)
	Asm setter;
	setter.op(kBCGetVar, 3).opLiteral(kBCFindAndSetVar, SYMBOL("x")).simple(kBCPushSelf).simple(kBCReturn);
	RefVar setX(setter.function(1, 0));
	SetFrameSlot(frame, SYMBOL("setX"), setX);
	EXPECT(NSSend(child, RefVar(SYMBOL("setX")), RefVar(MAKEINT(7))) == (Ref) child);
	EXPECT(RINT(GetFrameSlot(child, SYMBOL("x"))) == 7 && RINT(GetFrameSlot(frame, SYMBOL("x"))) == 10);
	// resend: inherited:m(1) from a child method (args, message)
	Asm resender;
	resender.pushInt(1).push(SYMBOL("m")).op(kBCResend, 1).simple(kBCReturn);
	RefVar childM(resender.function(0, 0));
	SetFrameSlot(child, SYMBOL("m"), childM);
	EXPECT(RINT(NSSend(child, RefVar(SYMBOL("m")))) == 8);
	// the _parent chain: a variable of the parent
	RefVar parent(AllocateFrame());
	SetFrameSlot(parent, SYMBOL("y"), RefVar(MAKEINT(1000)));
	SetFrameSlot(child, RSSYM_parent, parent);
	Asm getY;
	getY.opLiteral(kBCFindVar, SYMBOL("y")).simple(kBCReturn);
	SetFrameSlot(frame, SYMBOL("getY"), RefVar(getY.function(0, 0)));
	EXPECT(RINT(NSSend(child, RefVar(SYMBOL("getY")))) == 1000);
	EXPECT(RINT(DoMessage(child, RefVar(SYMBOL("getY")), RefVar(NILREF))) == 1000);
	EXPECT_STACKS_CLEAN();
}


static void
TestClosures()
{
	// outer(n): x := n (a captured variable, in the argFrame); return func() x * 2
	Asm inner;
	inner.opLiteral(kBCFindVar, SYMBOL("x")).pushInt(2).freq(kFFMultiply).simple(kBCReturn);
	RefVar innerFn(inner.function(0, 0, RefVar(MakeArgFrame())));
	Asm outer;
	outer.op(kBCGetVar, 3).opLiteral(kBCFindAndSetVar, SYMBOL("x")).push(innerFn).simple(kBCSetLexScope).simple(kBCReturn);
	RefVar outerFn(outer.function(1, 0, RefVar(MakeArgFrame("x"))));
	RefVar closure(NSCall(outerFn, RefVar(MAKEINT(21))));
	EXPECT(IsFunction(closure) && (Ref) closure != (Ref) innerFn);
	EXPECT(RINT(NSCall(closure)) == 42);
	RefVar closure2(NSCall(outerFn, RefVar(MAKEINT(5))));
	EXPECT(RINT(NSCall(closure2)) == 10 && RINT(NSCall(closure)) == 42);
	EXPECT_STACKS_CLEAN();
	// a counter: the closure changes the captured variable
	Asm bump;
	bump.opLiteral(kBCFindVar, SYMBOL("count")).pushInt(1).freq(kFFAdd).opLiteral(kBCFindAndSetVar, SYMBOL("count"));
	bump.opLiteral(kBCFindVar, SYMBOL("count")).simple(kBCReturn);
	RefVar bumpFn(bump.function(0, 0, RefVar(MakeArgFrame())));
	Asm maker;
	maker.pushInt(0).opLiteral(kBCFindAndSetVar, SYMBOL("count")).push(bumpFn).simple(kBCSetLexScope).simple(kBCReturn);
	RefVar makerFn(maker.function(0, 0, RefVar(MakeArgFrame("count"))));
	RefVar counter(NSCall(makerFn));
	EXPECT(RINT(NSCall(counter)) == 1 && RINT(NSCall(counter)) == 2 && RINT(NSCall(counter)) == 3);
	EXPECT_STACKS_CLEAN();
}


static void
TestExceptions()
{
	// fn(code) try Throw('|evt.ex.foo|, code) onexception |evt.ex.foo| do return CurrentException().error + 1
	Asm a;
	long handler = a.newLabel();
	a.push(SYMBOL("evt.ex.foo")).pushLabel(handler).op(kBCNewHandlers, 1);
	a.push(SYMBOL("evt.ex.foo")).op(kBCGetVar, 3).push(SYMBOL("Throw")).op(kBCCall, 2);
	a.pushInt(0).simple(kBCReturn);
	a.label(handler).push(SYMBOL("CurrentException")).op(kBCCall, 0).push(SYMBOL("error")).op(kBCGetPath, 0);
	a.pushInt(1).freq(kFFAdd).simple(kBCReturn);
	RefVar fn(a.function(1, 0));
	EXPECT(RINT(NSCall(fn, RefVar(MAKEINT(41)))) == 42);
	EXPECT_STACKS_CLEAN();
	EXPECT(ISNIL(gInterpreter->fExceptionContext));
	// an exception the function does not handle goes on to C++
	Asm b;
	long handler2 = b.newLabel();
	b.push(SYMBOL("evt.ex.foo")).pushLabel(handler2).op(kBCNewHandlers, 1);
	b.push(SYMBOL("evt.ex.bar")).op(kBCGetVar, 3).push(SYMBOL("Throw")).op(kBCCall, 2);	// Throw('evt.ex.bar, code)
	b.pushInt(0).simple(kBCReturn);
	b.label(handler2).pushInt(-1).simple(kBCReturn);
	RefVar fn2(b.function(1, 0));
	THROWS(NSCall(fn2, RefVar(MAKEINT(3))), "evt.ex.bar");
	EXPECT(gThrown != nil && strcmp(gThrown, "evt.ex.bar") == 0 && gThrownCode == 3);
	EXPECT_STACKS_CLEAN();
	EXPECT(ISNIL(gInterpreter->fExceptionContext));
	// a frames exception (a bad type) caught by a handler for evt.ex.fr
	Asm c;
	long handler3 = c.newLabel();
	c.push(SYMBOL("evt.ex.fr")).pushLabel(handler3).op(kBCNewHandlers, 1);
	c.op(kBCGetVar, 3).pushInt(1).freq(kFFAdd).simple(kBCReturn);
	c.label(handler3).push(SYMBOL("CurrentException")).op(kBCCall, 0).push(SYMBOL("data")).op(kBCGetPath, 0).simple(kBCReturn);
	RefVar fn3(c.function(1, 0));
	EXPECT(RINT(NSCall(fn3, RefVar(MAKEINT(1)))) == 2);
	RefVar data(NSCall(fn3, RefVar(MakeString("x"))));
	EXPECT(IsFrame(data) && RINT(GetFrameSlot(data, RSSYMerrorcode)) == kNSErrNotANumber);
	EXPECT_STACKS_CLEAN();
	// nested calls: the inner throw is handled by the outer function
	Asm thrower;
	thrower.push(SYMBOL("evt.ex.foo")).pushInt(5).push(SYMBOL("Throw")).op(kBCCall, 2).simple(kBCReturn);
	RefVar functions(gFunctionFrame);
	SetFrameSlot(functions, SYMBOL("Thrower"), RefVar(thrower.function(0, 0)));
	Asm d;
	long handler4 = d.newLabel();
	d.push(SYMBOL("evt.ex.foo")).pushLabel(handler4).op(kBCNewHandlers, 1);
	d.push(SYMBOL("Thrower")).op(kBCCall, 0).simple(kBCReturn);
	d.label(handler4).push(SYMBOL("CurrentException")).op(kBCCall, 0).push(SYMBOL("error")).op(kBCGetPath, 0).simple(kBCReturn);
	RefVar fn4(d.function(0, 0));
	EXPECT(RINT(NSCall(fn4)) == 5);
	EXPECT_STACKS_CLEAN();
	EXPECT(ISNIL(gInterpreter->fExceptionContext));
}


static void
TestROMFunctions()
{
	// the ROM's own NewtonScript functions, over what is reconstructed
	RefVar vars(gVarFrame);
	SetFrameSlot(vars, SYMBOL("vars"), vars);
	SetFrameSlot(vars, SYMBOL("functions"), RefVar(gFunctionFrame));
	EXPECT(ISNIL(NSCallGlobalFn(RefVar(SYMBOL("OnlyOneRoutingSlip")))));
	SetFrameSlot(vars, SYMBOL("aGlobal"), RefVar(MAKEINT(123)));
	EXPECT(RINT(NSCallGlobalFn(RefVar(SYMBOL("GetGlobalVar")), RefVar(SYMBOL("aGlobal")))) == 123);
	EXPECT(NSCallGlobalFn(RefVar(SYMBOL("GlobalVarExists")), RefVar(SYMBOL("aGlobal"))) == TRUEREF);
	EXPECT(ISNIL(NSCallGlobalFn(RefVar(SYMBOL("GlobalVarExists")), RefVar(SYMBOL("notAGlobal")))));
	NSCallGlobalFn(RefVar(SYMBOL("DefGlobalVar")), RefVar(SYMBOL("another")), RefVar(MAKEINT(9)));
	EXPECT(RINT(GetFrameSlot(vars, SYMBOL("another"))) == 9);
	EXPECT(ISNIL(NSCallGlobalFn(RefVar(SYMBOL("AliasFromObj")), RefVar(NILREF))));
	RefVar nameRef(AllocateFrame());
	SetFrameSlot(nameRef, RSSYMclass, RefVar(SYMBOL("nameRef")));
	EXPECT(NSCallGlobalFn(RefVar(SYMBOL("IsNameRef")), nameRef) == TRUEREF);
	EXPECT(ISNIL(NSCallGlobalFn(RefVar(SYMBOL("IsNameRef")), RefVar(MAKEINT(1)))));
	RefVar handlers(AllocateArray(RSSYMarray, 0));
	SetFrameSlot(vars, SYMBOL("OldPowerOffHandlers"), handlers);
	NSCallGlobalFn(RefVar(SYMBOL("AddPowerOffHandler")), RefVar(SYMBOL("handler1")));
	EXPECT(Length(handlers) == 1 && GetArraySlot(handlers, 0) == SYMBOL("handler1"));
	EXPECT_STACKS_CLEAN();
	// the ROM's functions survive collections under them
	for (int i = 0; i < 20; i++)
	{
		AllocateBinary(RSSYMstring, 10000);
		EXPECT(RINT(NSCallGlobalFn(RefVar(SYMBOL("GetGlobalVar")), RefVar(SYMBOL("aGlobal")))) == 123);
	}
	EXPECT_STACKS_CLEAN();
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_OBJECTS) != noErr)
	{
		printf("test_Interpreter: cannot import %s\n", NEWTON_OBJECTS);
		return 1;
	}
	gObjectHeapSize = 0x100000;
	InitObjects();											// starts the interpreter and binds the natives
	EXPECT(gInterpreter != nil && IsFrame(gFunctionFrame) && Length(gFreqFuncs) == kNumFreqFuncs);
	EXPECT(IsFunction(GetArraySlot(gFreqFuncs, kFFMultiply)));
	newton_try
	{
		TestArithmetic();
		TestLoops();
		TestObjects();
		TestGlobalsAndCalls();
		TestSends();
		TestClosures();
		TestExceptions();
		TestROMFunctions();
	}
	newton_catch_all
	{
		// a frames exception escaped a test: say which
		failures++;
		ExceptionName name = (ExceptionName) _info.exception.name;
		long code = (long) (Long) _info.exception.data;
		if (Subexception(name, (ExceptionName) exFrames) && _info.exception.data != nil)
		{
			RefStruct* data = (RefStruct*) _info.exception.data;
			if (IsFrame(*data) && FrameHasSlot(*data, RSSYMerrorcode))
				code = RINT(GetFrameSlot(*data, RSSYMerrorcode));
		}
		fprintf(stderr, "FAIL: unhandled exception %s (%ld)\n", name, code);
	}
	end_try;
	if (failures == 0)
		printf("test_Interpreter: all passed\n");
	else
		printf("test_Interpreter: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
