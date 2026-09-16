// Compiler test: NewtonScript source compiled (ParseString) and run
// (InterpretBlock) over the ROM image - constants, operators and their
// precedence, locals, globals, control flow, loops, foreach, frames,
// arrays and paths, functions, closures, sends, exceptions, constants
// and exists; 1.x code blocks as well as 2.x functions; the lexer's
// tokens; compiler errors; ParseFile; the Compile native.

#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "Compiler.h"
#include "REPTranslators.h"
#include "ROMImport.h"
#include "ROMConstants.h"
#include "Unicode.h"
#include "NSErrors.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static Ref SYMBOL(const char* name) { return Intern((char*) name); }

static FILE* gOutput = nil;

// what was printed since the last look
static const char*
Printed()
{
	static char buffer[4096];
	fflush(gOutput);
	long length = ftell(gOutput);
	if (length > (long) sizeof(buffer) - 1)
		length = sizeof(buffer) - 1;
	rewind(gOutput);
	size_t got = fread(buffer, 1, length, gOutput);
	buffer[got] = 0;
	rewind(gOutput);
	return buffer;
}


// source compiled and run at the top level (in gVarFrame, as the REP runs it)
static Ref
Eval(const char* source)
{
	RefVar fn(ParseString(RefVar(MakeString(source))));
	if ((Ref) fn == NILREF)
		return NILREF;
	return InterpretBlock(fn, RefVar(gVarFrame));
}

// the exception name and frames error code of a failure
static const char* gThrown = nil;
static long gThrownCode = 0;
static Ref gThrownValue = NILREF;
#define THROWS(expr) do { gThrown = nil; gThrownCode = 0; gThrownValue = NILREF; long valueDepth = gInterpreter->ValuePosition(); newton_try { (void) (expr); } newton_catch_all { gThrown = _info.exception.name; if (Subexception((ExceptionName) gThrown, (ExceptionName) "type.ref")) { RefStruct* data = (RefStruct*) _info.exception.data; if (IsFrame(*data)) { gThrownCode = RINT(GetFrameSlot(*data, RSSYMerrorcode)); if (FrameHasSlot(*data, RSSYMvalue)) gThrownValue = GetFrameSlot(*data, RSSYMvalue); } } else gThrownCode = (long) (Long) _info.exception.data; } end_try; gInterpreter->fValueStack.Reset(valueDepth); } while (0)

#define EXPECT_INT(source, value) do { Ref r = Eval(source); if (!ISINT(r) || RINT(r) != (value)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, source); } } while (0)
#define EXPECT_NIL(source) do { Ref r = Eval(source); if (r != NILREF) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, source); } } while (0)
#define EXPECT_TRUE(source) do { Ref r = Eval(source); if (r != TRUEREF) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, source); } } while (0)
#define EXPECT_STRING(source, text) do { RefVar r(Eval(source)); if (!IsString(r) || !StringEquals(r, text)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, source); } } while (0)
#define EXPECT_SYMBOL(source, name) do { Ref r = Eval(source); if (r != SYMBOL(name)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, source); } } while (0)

static Boolean
StringEquals(RefArg str, const char* text)
{
	const UniChar* s = GetCString(str);
	for (; *text != 0; text++, s++)
		if (*s != (UniChar) (unsigned char) *text)
			return false;
	return *s == 0;
}


static void
TestConstants()
{
	EXPECT_INT("42", 42);
	EXPECT_INT("-7", -7);
	EXPECT_INT("0x1F", 31);
	EXPECT_NIL("nil");
	EXPECT_TRUE("true");
	EXPECT_NIL("NIL");
	EXPECT_TRUE("TRUE");
	EXPECT_SYMBOL("'foo", "foo");
	EXPECT_SYMBOL("'|hello world|", "hello world");
	EXPECT_STRING("\"hello\"", "hello");
	EXPECT_STRING("\"a\\tb\\nc\\\\\"", "a\tb\rc\\");
	EXPECT_STRING("\"\\u00410042\\u\"", "AB");
	EXPECT_STRING("\"abc\" \"def\"", "abcdef");
	EXPECT(Eval("$a") == MAKECHAR('a'));
	EXPECT(Eval("$\\n") == MAKECHAR('\r'));
	EXPECT(Eval("$\\41") == MAKECHAR('A'));
	EXPECT(Eval("$\\u263A") == MAKECHAR(0x263a));
	{
		RefVar r(Eval("1.5"));
		EXPECT(ISREAL(r) && CDouble(r) == 1.5);
		r = Eval("2.5e2");
		EXPECT(ISREAL(r) && CDouble(r) == 250.0);
		r = Eval("-0.25");
		EXPECT(ISREAL(r) && CDouble(r) == -0.25);
	}
	EXPECT(Eval("@872") == MAKEMAGICPTR(872));
	// quoted expressions
	{
		RefVar r(Eval("'[1, 2, 3]"));
		EXPECT(IsArray(r) && Length(r) == 3 && RINT(GetArraySlot(r, 2)) == 3);
		r = Eval("'{a: 1, b: \"x\"}");
		EXPECT(IsFrame(r) && RINT(GetFrameSlot(r, SYMBOL("a"))) == 1);
		r = Eval("'a.b.c");
		EXPECT(IsArray(r) && EQ(ClassOf(r), RSSYMpathexpr) && Length(r) == 3);
		r = Eval("'[foo: 1, -2, -1.5]");
		EXPECT(IsArray(r) && ClassOf(r) == SYMBOL("foo") && RINT(GetArraySlot(r, 1)) == -2);
	}
	// comments
	EXPECT_INT("1 + /* two */ 2 // and a line comment\n + 3", 6);
}


static void
TestOperators()
{
	EXPECT_INT("2 + 3 * 4", 14);
	EXPECT_INT("(2 + 3) * 4", 20);
	EXPECT_INT("10 - 4 - 3", 3);
	EXPECT_INT("2 * 3 + 4 * 5", 26);
	EXPECT_INT("-5 + 2", -3);
	EXPECT_INT("- (5 + 2)", -7);
	EXPECT_INT("7 div 2", 3);
	EXPECT_INT("7 mod 3", 1);
	EXPECT_INT("1 << 4", 16);
	EXPECT_INT("256 >> 4", 16);
	EXPECT_INT("1 + 2 << 1", 5);					// << binds tighter than +
	{
		RefVar r(Eval("7 / 2"));
		EXPECT(ISREAL(r) && CDouble(r) == 3.5);
		r = Eval("1.5 * 2");
		EXPECT(ISREAL(r) && CDouble(r) == 3.0);
	}
	EXPECT_TRUE("3 < 4");
	EXPECT_NIL("3 > 4");
	EXPECT_TRUE("3 <= 3 and 4 >= 4");
	EXPECT_TRUE("1 = 1");
	EXPECT_TRUE("1 <> 2");
	EXPECT_NIL("1 <> 1");
	EXPECT_TRUE("'a = 'a");
	EXPECT_TRUE("not nil");
	EXPECT_NIL("not 1");
	EXPECT_TRUE("nil or 5 > 2");
	EXPECT_NIL("nil and 5 > 2");
	EXPECT_INT("if 1 < 2 and 2 < 3 then 1 else 2", 1);
	EXPECT_TRUE("1 < 2 or 2 > 3");
	// and/or as values
	EXPECT_NIL("1 and nil");
	EXPECT_TRUE("nil or true");
	// concatenation
	EXPECT_STRING("\"abc\" & \"def\"", "abcdef");
	EXPECT_STRING("\"a\" && \"b\"", "a b");
	EXPECT_STRING("\"n=\" & 42 & $! & 'sym & nil", "n=42!sym");
	EXPECT_STRING("\"r=\" & 1.5", "r=1.5");
}


static void
TestVariables()
{
	EXPECT_INT("local x := 5; x * 2", 10);
	EXPECT_INT("local a, b := 3; a := b + 1; a", 4);
	EXPECT_INT("local x := 1, y := 2; x + y", 3);
	EXPECT_NIL("local z; z");
	EXPECT_INT("local x := 1; x := x + 1; x := x * 3; x", 6);
	// an assignment's value
	EXPECT_INT("local x; (x := 7) + 1", 8);
	// typed locals (the type is ignored)
	EXPECT_INT("local int n := 3; n", 3);
	// globals
	EXPECT_INT("global gTest := 11; gTest + 1", 12);
	EXPECT_INT("gTest := gTest + 1; gTest", 12);
	EXPECT(RINT(GetFrameSlot(RefVar(gVarFrame), SYMBOL("gTest"))) == 12);
	// begin/end
	EXPECT_INT("begin local x := 2; x * x end", 4);
	EXPECT_NIL("begin end");
	// constants
	// (top-level constants stay in gConstantsFrame, as the ROM keeps them)
	EXPECT_INT("constant kSeven := 7; kSeven * 2", 14);
	EXPECT_INT("constant kThree := 3, kFour := 4; kThree + kFour", 7);		// (after the first, a constant must be a literal)
	EXPECT_INT("constant kMinusTwo := -2; -kMinusTwo", 2);
	EXPECT_INT("kSeven + kThree", 10);
	// an undefined variable
	THROWS(Eval("noSuchVariable + 1"));
	EXPECT(gThrown != nil && gThrownCode == kNSErrUndefinedVariable);
}


static void
TestControl()
{
	EXPECT_INT("if 1 < 2 then 10 else 20", 10);
	EXPECT_INT("if 1 > 2 then 10 else 20", 20);
	EXPECT_NIL("if 1 > 2 then 10");
	EXPECT_INT("if 1 > 2 then 10 else if 2 > 1 then 30 else 40", 30);
	EXPECT_INT("local x := 0; if 1 < 2 then x := 5; x", 5);
	// loops
	EXPECT_INT("local s := 0; for i := 1 to 10 do s := s + i; s", 55);
	EXPECT_INT("local s := 0; for i := 10 to 1 by -2 do s := s + i; s", 30);
	EXPECT_INT("local s := 0; for i := 1 to 0 do s := s + 1; s", 0);
	EXPECT_INT("local i := 0; while i < 5 do i := i + 1; i", 5);
	EXPECT_INT("local i := 0; repeat i := i + 1 until i >= 3; i", 3);
	EXPECT_INT("local i := 0; loop begin i := i + 1; if i = 4 then break end; i", 4);
	EXPECT_INT("local i := 0; loop begin i := i + 1; if i = 4 then break i * 10 end", 40);
	EXPECT_INT("local s := 0; for i := 1 to 100 do begin if i > 3 then break; s := s + i end; s", 6);
	EXPECT_NIL("for i := 1 to 3 do i");
	// foreach
	EXPECT_INT("local s := 0; foreach v in [1, 2, 3] do s := s + v; s", 6);
	EXPECT_INT("local s := 0; foreach k, v in {a: 1, b: 2} do s := s + v; s", 3);
	EXPECT_SYMBOL("local last; foreach k, v in {a: 1, b: 2} do last := k; last", "b");
	{
		RefVar r(Eval("foreach v in [1, 2, 3] collect v * v"));
		EXPECT(IsArray(r) && Length(r) == 3 && RINT(GetArraySlot(r, 2)) == 9);
		r = Eval("foreach k, v in {a: 1, b: 2} collect k");
		EXPECT(IsArray(r) && Length(r) == 2 && GetArraySlot(r, 1) == SYMBOL("b"));
		r = Eval("foreach v deeply in [1, [2, 3]] collect v");
		EXPECT(IsArray(r) && Length(r) == 2);				// (deeply is for frames: their _proto chains)
		r = Eval("foreach k, v deeply in {_proto: {a: 1}, b: 2} collect v");
		EXPECT(IsArray(r) && Length(r) == 2 && RINT(GetArraySlot(r, 0)) == 2 && RINT(GetArraySlot(r, 1)) == 1);
	}
	EXPECT_INT("local n := 0; foreach v in [1, 2, 3, 4] do begin if v = 3 then break; n := n + 1 end; n", 2);
	// return
	EXPECT_INT("return 5; 6", 5);
	EXPECT_NIL("return");
}


static void
TestObjects()
{
	{
		RefVar r(Eval("[1, 2, 3]"));
		EXPECT(IsArray(r) && Length(r) == 3 && RINT(GetArraySlot(r, 1)) == 2);
		r = Eval("[point: 1, 2]");
		EXPECT(IsArray(r) && ClassOf(r) == SYMBOL("point") && Length(r) == 2);
		r = Eval("[]");
		EXPECT(IsArray(r) && Length(r) == 0);
		r = Eval("{a: 1, b: 2 + 3}");
		EXPECT(IsFrame(r) && RINT(GetFrameSlot(r, SYMBOL("b"))) == 5);
		r = Eval("{}");
		EXPECT(IsFrame(r) && Length(r) == 0);
		r = Eval("{a: [1, {b: 2}]}");
		EXPECT(IsFrame(r));
	}
	EXPECT_INT("{a: 1, b: [1, 2]}.b[1]", 2);
	EXPECT_INT("local f := {a: 1}; f.a := 5; f.a", 5);
	EXPECT_INT("local f := {a: {b: 1}}; f.a.b := 9; f.a.b", 9);
	EXPECT_INT("local f := {a: {b: 1}}; f.a.b", 1);
	EXPECT_INT("local a := [1, 2, 3]; a[1] := 20; a[1] + a[2]", 23);
	EXPECT_INT("local f := {x: 3}; f.('x) + 1", 4);
	EXPECT_INT("local f := {x: 3}; local s := 'x; f.(s)", 3);
	EXPECT_NIL("{a: 1}.b");
	EXPECT_INT("Length([1, 2, 3, 4])", 4);
	EXPECT_INT("local a := [1]; AddArraySlot(a, 2); Length(a)", 2);
	EXPECT_INT("local a := Array(3, 0); a[2] := 4; a[2]", 4);
	// exists
	EXPECT_TRUE("global gExistsTest := 1; gExistsTest exists");
	EXPECT_NIL("noSuchVar exists");
	EXPECT_NIL("local x := 1; x exists");				// (as in the ROM: HasVar sees no stack locals)
	EXPECT_TRUE("{a: 1}.a exists");
	EXPECT_NIL("{a: 1}.b exists");
	EXPECT_TRUE("{a: 1}:a exists");
	EXPECT_NIL("{a: 1}:b exists");
	EXPECT_TRUE("local f := {a: {b: 1}}; f.a.b exists");
	// the global functions frame
	EXPECT_TRUE("IsInstance({class: 'nameRef}, 'nameRef)");
	EXPECT_TRUE("IsNameRef({class: 'nameRef})");
}


static void
TestFunctions()
{
	EXPECT_INT("local f := func(x) x + 1; call f with (3)", 4);
	EXPECT_INT("local f := func(x, y) x * y; call f with (3, 4)", 12);
	EXPECT_INT("call func() 9 with ()", 9);
	// closures
	EXPECT_INT("local n := 10; local f := func(x) x + n; call f with (5)", 15);
	EXPECT_INT("local f := func(a) func(b) a + b; local g := call f with (1); call g with (2)", 3);
	EXPECT_INT("local count := 0; local inc := func() count := count + 1; call inc with (); call inc with (); count", 2);
	EXPECT_INT("local make := func(a) func() a * 2; local f1 := call make with (1); local f2 := call make with (5); (call f1 with ()) + (call f2 with ())", 12);
	// global functions
	EXPECT_INT("func Twice(x) x * 2; Twice(4)", 8);
	EXPECT_INT("global Thrice(x) x * 3; Thrice(4) + Twice(1)", 14);
	EXPECT(IsFunction(GetFrameSlot(RefVar(gFunctionFrame), SYMBOL("Twice"))));
	EXPECT_INT("func Fact(n) if n <= 1 then 1 else n * Fact(n - 1); Fact(5)", 120);
	// a native from the ROM's frame, an unreconstructed one
	EXPECT_INT("Max(3, 9) + Min(3, 9)", 12);
	THROWS(Eval("Sleep(1)"));
	EXPECT(gThrown != nil && gThrownCode == kNSErrNativeNotReconstructed);
	// the wrong number of arguments
	THROWS(Eval("Twice(1, 2)"));
	EXPECT(gThrown != nil && gThrownCode == kNSErrWrongNumberOfArgs);
	// compiled functions are 2.x: arguments and locals on the stack, closed variables in the argFrame
	{
		RefVar fn(Eval("func(x, y) begin local z := x + y; z * 2 end"));
		EXPECT(IsFunction(fn) && GetArraySlotRef(fn, kFunctionClassSlot) == kFuncClass);
		EXPECT(RINT(GetArraySlotRef(fn, kFunctionNumArgsSlot)) == (2 | (1 << 16)));
		EXPECT(GetArraySlotRef(fn, kFunctionArgFrameSlot) == NILREF);
		EXPECT(RINT(NSCall(fn, RefVar(MAKEINT(1)), RefVar(MAKEINT(2)))) == 6);
		// the prototype of a nested function that closes over n: an argFrame of n, self and inherited unused
		RefVar top(ParseString(RefVar(MakeString("func(n) func() n"))));
		RefVar outer(GetArraySlot(RefVar(GetArraySlotRef(top, kFunctionLiteralsSlot)), 0));
		EXPECT(IsFunction(outer));
		RefVar argFrame(GetArraySlotRef(outer, kFunctionArgFrameSlot));
		EXPECT(IsFrame(argFrame) && FrameHasSlot(argFrame, SYMBOL("n")) && Length(argFrame) == 4);
		EXPECT(GetArraySlotRef(argFrame, 1) == 0 && GetArraySlotRef(argFrame, 2) == 0);
		// the debug information names the variables
		RefVar dbg(GetArraySlotRef(fn, 5));
		EXPECT(IsArray(dbg) && ClassOf(dbg) == SYMBOL("dbg1") && Length(dbg) == 4);
		EXPECT(GetArraySlot(dbg, 1) == SYMBOL("x") && GetArraySlot(dbg, 3) == SYMBOL("z"));
	}
	// native declarations parse (the code is the same here)
	EXPECT_INT("local f := func native(x) x + 1; call f with (1)", 2);
}


static void
TestSends()
{
	EXPECT_INT("{x: 5, m: func(y) x + y}:m(3)", 8);
	EXPECT_INT("local o := {x: 5, m: func() self.x * 2}; o:m()", 10);
	EXPECT_INT("local p := {m: func(a) a + 1}; local c := {_proto: p}; c:m(1)", 2);
	EXPECT_INT("local p := {m: func(a) a + 1}; local c := {_proto: p, m: func(a) inherited:m(a) * 10}; c:m(1)", 20);
	EXPECT_NIL("{x: 1}:?nothing()");
	EXPECT_INT("local o := {v: 3, get: func() v}; o:get()", 3);
	EXPECT_INT("local o := {v: 3, set: func(n) begin v := n; self end}; o:set(7).v", 7);
	EXPECT_INT("local o := {v: 1, get: func() :get2(), get2: func() v + 1}; o:get()", 2);
	EXPECT_INT("local o := {v: 2, get: func() begin local f := func() self.v; call f with () end}; o:get()", 2);
	THROWS(Eval("{x: 1}:nothing()"));
	EXPECT(gThrown != nil && gThrownCode == kNSErrUndefinedMethod);
	// SetValue-style: a method assigning a slot of self
	EXPECT_INT("local o := {v: 1, bump: func() v := v + 1}; o:bump(); o:bump(); o.v", 3);
}


static void
TestExceptions()
{
	// a symbol without ;type.ref throws an error code; with it, a ref
	EXPECT_INT("try Throw('|evt.ex.foo|, 42) onexception |evt.ex.foo| do CurrentException().error", 42);
	EXPECT_INT("try Throw('|evt.ex.foo;type.ref.frame|, {v: 5}) onexception |evt.ex.foo| do CurrentException().data.v", 5);
	EXPECT_SYMBOL("try Throw('|evt.ex.foo|, 1) onexception |evt.ex.foo| do CurrentException().name", "evt.ex.foo");
	EXPECT_INT("try 1 + 1 onexception |evt.ex.foo| do 99", 2);
	EXPECT_INT("try Throw('|evt.ex.foo|, 1) onexception |evt.ex.bar| do 1 onexception |evt.ex.foo| do 2", 2);
	EXPECT_INT("try begin Throw('|evt.ex.foo|, 1); 5 end onexception |evt.ex| do 3", 3);
	EXPECT_INT("try 1 + \"a\" onexception |evt.ex.fr| do CurrentException().data.errorCode", kNSErrNotANumber);
	EXPECT_INT("local r := 0; try r := 1 onexception |evt.ex| do r := 2; r", 1);
	// nested and rethrown
	EXPECT_INT("try begin try Throw('|evt.ex.foo|, 7) onexception |evt.ex.bar| do 0 end onexception |evt.ex.foo| do CurrentException().error", 7);
	EXPECT_INT("try begin try Throw('|evt.ex.foo|, 7) onexception |evt.ex.foo| do Rethrow() end onexception |evt.ex.foo| do CurrentException().error + 1", 8);
	// an unhandled one reaches C++
	THROWS(Eval("Throw('|evt.ex.foo|, 3)"));
	EXPECT(gThrown != nil && strcmp(gThrown, "evt.ex.foo") == 0 && gThrownCode == 3);
	EXPECT(gInterpreter->ControlPosition() == 5);
}


static void
TestCodeBlocks()
{
	// compilercompatibility 0: 1.x code blocks, everything in the argFrame
	SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("compilercompatibility")), RefVar(MAKEINT(0)));
	RefVar fn(ParseString(RefVar(MakeString("local x := 3; x + 1"))));
	EXPECT(IsFrame(fn) && EQ(ClassOf(fn), RSSYMcodeblock));
	EXPECT(RINT(GetArraySlotRef(fn, kFunctionNumArgsSlot)) == 0);
	RefVar argFrame(GetArraySlotRef(fn, kFunctionArgFrameSlot));
	EXPECT(IsFrame(argFrame) && FrameHasSlot(argFrame, SYMBOL("x")));
	EXPECT(RINT(InterpretBlock(fn, RefVar(gVarFrame))) == 4);
	EXPECT_INT("local s := 0; for i := 1 to 4 do s := s + i; s", 10);
	EXPECT_INT("local f := func(a, b) a - b; call f with (9, 4)", 5);
	EXPECT_INT("local n := 10; local f := func(x) x + n; call f with (5)", 15);
	EXPECT_INT("{x: 5, m: func(y) x + y}:m(3)", 8);
	EXPECT_INT("try Throw('|evt.ex.foo|, 42) onexception |evt.ex.foo| do CurrentException().error", 42);
	{
		RefVar f(Eval("func(x, y) x + y"));
		EXPECT(IsFrame(f) && EQ(ClassOf(f), RSSYMcodeblock) && RINT(GetArraySlotRef(f, kFunctionNumArgsSlot)) == 2);
		EXPECT(RINT(NSCall(f, RefVar(MAKEINT(2)), RefVar(MAKEINT(3)))) == 5);
	}
	RemoveSlot(RefVar(gVarFrame), RefVar(SYMBOL("compilercompatibility")));
	EXPECT(GetArraySlotRef(RefVar(Eval("func() 1")), kFunctionClassSlot) == kFuncClass);
}


static void
TestErrors()
{
	THROWS(Eval("1 +"));
	EXPECT(gThrown != nil && strcmp(gThrown, "evt.ex.fr.comp;type.ref.frame") == 0 && gThrownCode == kNSErrSyntaxError);
	{
		RefVar message(ASCIIString(gThrownValue));
		const char* text = BinaryData(message);
		if (strncmp(text, "syntax error--read end-of-file, but wanted ", 43) != 0)
		{
			failures++;
			fprintf(stderr, "FAIL: syntax error message: %s\n", text);
		}
	}
	THROWS(Eval("local y := 1; constant y := 2"));
	EXPECT(gThrownCode == kNSErrLocalIsConstant);
	THROWS(Eval("constant kC := 1; kC := 2"));
	EXPECT(gThrownCode == kNSErrAssignToConstant);
	THROWS(Eval("constant kD := 1 + 1"));
	EXPECT(gThrownCode == kNSErrNonConstantInitializer);
	THROWS(Eval("\"unterminated"));
	EXPECT(gThrownCode == kNSErrEOFInString);
	THROWS(Eval("1 ^ 2"));
	EXPECT(gThrownCode == kNSErrBadCharacter && gThrownValue == MAKECHAR('^'));
	THROWS(Eval("$\\zz"));
	EXPECT(gThrownCode == kNSErrBadCharEscape);
	THROWS(Eval("4000000000"));
	EXPECT(gThrownCode == kNSErrIntegerTooLarge);
	THROWS(Eval("@x"));
	EXPECT(gThrownCode == kNSErrBadMagicPointerRef);
	THROWS(Eval("break"));
	EXPECT(gThrown != nil && strcmp(gThrown, "evt.ex.msg") == 0);
	THROWS(Eval("foreach x in [1] frob x"));
	EXPECT(gThrownCode == kNSErrSyntaxError);
	// warnings go to the REP
	Eval("1 = 1");
	EXPECT(strstr(Printed(), "### Warning: = at top level...did you mean := ?") != nil);
	Eval("local x; x; 1");
	EXPECT(strstr(Printed(), "Warning: Statement has no effect") == nil);
	Eval("1 + 1; 2");
	EXPECT(strstr(Printed(), "Warning: Statement has no effect") != nil);
	Eval("local f := func(a, a) a");
	EXPECT(strstr(Printed(), "Duplicate argument name: a") != nil);
	// a syntax error's file and line
	THROWS(Eval("1;\n2;\n3 +;"));
	EXPECT(gThrownCode == kNSErrSyntaxError);
}


static void
TestFiles()
{
	FILE* f = fopen("test_Compiler.tmp.ns", "w");
	EXPECT(f != nil);
	if (f == nil)
		return;
	fputs("global gFileVar := 40;\n// a comment\nfunc FileFn(x) x + gFileVar;\nFileFn(2);\n", f);
	fclose(f);
	RefVar result(ParseFile("test_Compiler.tmp.ns"));
	EXPECT(ISINT(result) && RINT(result) == 42);
	EXPECT(RINT(GetFrameSlot(RefVar(gVarFrame), SYMBOL("gFileVar"))) == 40);
	EXPECT_INT("FileFn(3)", 43);
	remove("test_Compiler.tmp.ns");
	// a file with an error reports it and answers nil
	f = fopen("test_Compiler.tmp.ns", "w");
	fputs("1;\n2 +;\n", f);
	fclose(f);
	Printed();
	result = ParseFile("test_Compiler.tmp.ns");
	EXPECT(strstr(Printed(), "!!! Exception: evt.ex.fr.comp;type.ref.frame") != nil);
	remove("test_Compiler.tmp.ns");
	// the natives
	{
		RefVar fn(NSCallGlobalFn(RefVar(SYMBOL("Compile")), RefVar(MakeString("3 * 3"))));
		EXPECT(IsFunction(fn) && RINT(NSCall(fn)) == 9);
		Printed();
		Disassemble(RefVar(Eval("func() 1 + 2")));
		EXPECT(strcmp(Printed(), "   0: push-constant 1\n   1: push-constant 2\n   4: freq-func 0 [+/2]\n   5: return\n") == 0);
	}
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_ROM_IMAGE) != noErr)
	{
		printf("test_Compiler: cannot import %s\n", NEWTON_ROM_IMAGE);
		return 1;
	}
	gObjectHeapSize = 0x100000;
	InitObjects();
	gOutput = tmpfile();
	if (gOutput == nil)
	{
		printf("test_Compiler: no temporary file\n");
		return 1;
	}
	HostInitREP(gOutput);
	Printed();
	newton_try
	{
		TestConstants();
		TestOperators();
		TestVariables();
		TestControl();
		TestObjects();
		TestFunctions();
		TestSends();
		TestExceptions();
		TestCodeBlocks();
		TestErrors();
		TestFiles();
	}
	newton_catch_all
	{
		failures++;
		fprintf(stderr, "FAIL: unhandled exception %s\n", _info.exception.name);
		if (Subexception((ExceptionName) _info.exception.name, (ExceptionName) "type.ref"))
		{
			// the exception's frame, on stderr
			RefStruct* data = (RefStruct*) _info.exception.data;
			POutTranslator* saved = gREPout;
			PStdioOutTranslator err;
			err.New();
			FILE* stream = stderr;
			err.Init(&stream);
			gREPout = &err;
			PrintObject(*data, 0);
			gREPout->Print("\r");
			gREPout = saved;
		}
	}
	end_try;
	fclose(gOutput);
	if (failures == 0)
		printf("test_Compiler: all passed\n");
	else
		printf("test_Compiler: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
