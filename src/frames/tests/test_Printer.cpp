// Printer test: the REP started on a stdio stream (a temporary file the
// test reads back), and objects printed as NewtonScript source -
// integers, immediates, characters, symbols, strings, reals, binaries,
// arrays, frames, path expressions, functions, nesting, cycles and the
// printDepth/printLength/prettyPrint globals; the bytecode disassembler;
// objects as strings (SPrintObject); the printing natives; the REP's
// exception reports; and Print's formats.

#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "REPTranslators.h"
#include "ROMImport.h"
#include "ROMConstants.h"
#include "Unicode.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static Ref SYMBOL(const char* name) { return Intern((char*) name); }

static FILE* gOutput = nil;

// what was printed since the last look, as a C string
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
	// (start over: the stream is truncated by reopening its position)
	fseek(gOutput, 0, SEEK_SET);
	return buffer;
}

// the text an object prints as
static const char*
Text(RefArg obj, long indent = 0)
{
	PrintObject(obj, indent);
	return Printed();
}

#define EXPECT_PRINTS(obj, text) do { const char* got = Text(obj); if (strcmp(got, text) != 0) { failures++; fprintf(stderr, "FAIL %s:%d: printed \"%s\", expected \"%s\"\n", __FILE__, __LINE__, got, text); } } while (0)
#define EXPECT_PRINTED(text) do { const char* got = Printed(); if (strcmp(got, text) != 0) { failures++; fprintf(stderr, "FAIL %s:%d: printed \"%s\", expected \"%s\"\n", __FILE__, __LINE__, got, text); } } while (0)

// the file is reused: after each look the position is reset, so the next
// output overwrites - truncate it so that stale text cannot show
static void
Reset()
{
	fflush(gOutput);
	rewind(gOutput);
}


static void
TestScalars()
{
	EXPECT_PRINTS(RefVar(MAKEINT(42)), "42");
	EXPECT_PRINTS(RefVar(MAKEINT(-5)), "-5");
	EXPECT_PRINTS(RefVar(NILREF), "NIL");
	EXPECT_PRINTS(RefVar(TRUEREF), "TRUE");
	EXPECT_PRINTS(RefVar(MAKECHAR('a')), "$a");
	EXPECT_PRINTS(RefVar(MAKECHAR('\\')), "$\\");
	EXPECT_PRINTS(RefVar(MAKECHAR(10)), "$\\0A");
	EXPECT_PRINTS(RefVar(MAKECHAR(0x263a)), "$\\u263A");
	EXPECT_PRINTS(RefVar(kSymbolClass), "<symbol class>");
	EXPECT_PRINTS(RefVar(kWeakArrayClass), "<weak array class>");
	EXPECT_PRINTS(RefVar(kDeclawedRef), "<bad pkg ref>");
	EXPECT_PRINTS(RefVar(MAKEIMMED(kImmedSpecial, 7)), "#72");
	EXPECT_PRINTS(RefVar(SYMBOL("sym")), "sym");
	EXPECT_PRINTS(RefVar(SYMBOL("_under9")), "_under9");
	EXPECT_PRINTS(RefVar(SYMBOL("hello world")), "|hello world|");
	EXPECT_PRINTS(RefVar(SYMBOL("9lives")), "|9lives|");
	EXPECT_PRINTS(RefVar(SYMBOL("a|b\\c")), "|a\\|b\\\\c|");
	EXPECT_PRINTS(RefVar(SYMBOL("evt.ex.foo")), "|evt.ex.foo|");
	EXPECT_PRINTS(RefVar(MakeString("hello")), "\"hello\"");
	EXPECT_PRINTS(RefVar(MakeReal(1.5)), "1.50000");
	EXPECT_PRINTS(RefVar(MakeReal(-2.0)), "-2.00000");
	// a long string prints in pieces
	{
		char text[600];
		memset(text, 'x', 599);
		text[599] = 0;
		RefVar str(MakeString(text));
		const char* got = Text(str);
		EXPECT(strlen(got) == 601 && got[0] == '"' && got[600] == '"' && got[300] == 'x');
	}
	// binaries: with a symbol class, with a non-symbol class
	RefVar bin(AllocateBinary(SYMBOL("blob"), 3));
	EXPECT_PRINTS(bin, "<blob, length 3>");
	RefVar classFrame(AllocateFrame());
	SetFrameSlot(classFrame, SYMBOL("k"), RefVar(MAKEINT(1)));
	RefVar bin2(AllocateBinary(classFrame, 2));
	EXPECT_PRINTS(bin2, "<Binary, class {k: 1}, length 2>");
}


static void
TestAggregates()
{
	RefVar arr(AllocateArray(RSSYMarray, 3));
	SetArraySlotRef(arr, 0, MAKEINT(1));
	SetArraySlotRef(arr, 1, MAKEINT(2));
	SetArraySlotRef(arr, 2, MAKEINT(3));
	EXPECT_PRINTS(arr, "[1, 2, 3]");
	RefVar typed(AllocateArray(SYMBOL("points"), 2));
	SetArraySlotRef(typed, 0, MAKECHAR('x'));
	SetArraySlotRef(typed, 1, SYMBOL("y"));
	EXPECT_PRINTS(typed, "[points: $x, y]");
	RefVar empty(AllocateArray(RSSYMarray, 0));
	EXPECT_PRINTS(empty, "[]");
	RefVar frame(AllocateFrame());
	SetFrameSlot(frame, SYMBOL("a"), RefVar(MAKEINT(1)));
	SetFrameSlot(frame, SYMBOL("b"), RefVar(MakeString("x")));
	EXPECT_PRINTS(frame, "{a: 1, b: \"x\"}");
	EXPECT_PRINTS(RefVar(AllocateFrame()), "{}");
	// funcPtr and viewCObject slots print as hex
	RefVar native(AllocateFrame());
	SetFrameSlot(native, RSSYMfuncptr, RefVar(MAKEINT(0x100)));
	SetFrameSlot(native, RSSYMviewcobject, RefVar(MAKEINT(0x2a)));
	EXPECT_PRINTS(native, "{funcPtr: 0x400, viewCObject: 0x2A}");
	// a path expression
	RefVar path(AllocateArray(RSSYMpathexpr, 2));
	SetArraySlotRef(path, 0, SYMBOL("a"));
	SetArraySlotRef(path, 1, SYMBOL("b"));
	EXPECT_PRINTS(path, "a.b");
	SetArraySlotRef(path, 1, MAKEINT(3));
	EXPECT_PRINTS(path, "[pathExpr: a, 3]");
	// nesting: an aggregate inside puts each element on its own line, indented past the bracket
	RefVar nested(AllocateArray(RSSYMarray, 2));
	SetArraySlotRef(nested, 0, MAKEINT(1));
	SetArraySlotRef(nested, 1, frame);
	EXPECT_PRINTS(nested, "[1, \n {a: 1, b: \"x\"}]");
	RefVar outer(AllocateFrame());
	SetFrameSlot(outer, SYMBOL("inner"), nested);
	EXPECT_PRINTS(outer, "{inner: [1, \n         {a: 1, b: \"x\"}]}");
	// an aggregate with a non-symbol class
	RefVar classed(AllocateArray(frame, 1));
	SetArraySlotRef(classed, 0, MAKEINT(9));
	EXPECT_PRINTS(classed, "[{a: 1, b: \"x\"}: \n 9]");
	// without prettyPrint everything stays on one line
	SetFrameSlot(RefVar(gVarFrame), RSSYMprettyprint, RefVar(NILREF));
	EXPECT_PRINTS(outer, "{inner: [1, {a: 1, b: \"x\"}]}");
	SetFrameSlot(RefVar(gVarFrame), RSSYMprettyprint, RefVar(TRUEREF));
	// printLength
	SetFrameSlot(RefVar(gVarFrame), RSSYMprintlength, RefVar(MAKEINT(2)));
	EXPECT_PRINTS(arr, "[1, 2, ...]");
	SetFrameSlot(RefVar(gVarFrame), RSSYMprintlength, RefVar(NILREF));
	EXPECT_PRINTS(arr, "[1, 2, 3]");
	// printDepth: beyond it aggregates print by address
	SetFrameSlot(RefVar(gVarFrame), RSSYMprintdepth, RefVar(MAKEINT(0)));
	{
		// (the text first: printing may collect, and move the objects)
		char expected[64];
		char got[64];
		strncpy(got, Text(outer), sizeof(got) - 1);
		snprintf(expected, sizeof(expected), "{inner: [#%lX]}", (long) (Ref) nested);
		EXPECT(strcmp(got, expected) == 0);
		strncpy(got, Text(nested), sizeof(got) - 1);
		snprintf(expected, sizeof(expected), "[1, \n {#%lX}]", (long) ForwardReference(frame));	// (the frame moved when its slots were added)
		EXPECT(strcmp(got, expected) == 0);
		// a magic pointer's frame
		EXPECT_PRINTS(RefVar(AllocateArray(RSSYMarray, 0)), "[]");
		RefVar holder(AllocateArray(RSSYMarray, 1));
		SetArraySlotRef(holder, 0, MAKEMAGICPTR(872));
		EXPECT_PRINTS(holder, "[{@872}]");
	}
	SetFrameSlot(RefVar(gVarFrame), RSSYMprintdepth, RefVar(MAKEINT(3)));
	// a cycle
	RefVar self(AllocateFrame());
	SetFrameSlot(self, SYMBOL("me"), self);
	EXPECT_PRINTS(self, "{me: <0>}");
	RefVar ring(AllocateArray(RSSYMarray, 1));
	RefVar ring2(AllocateArray(RSSYMarray, 1));
	SetArraySlotRef(ring, 0, ring2);
	SetArraySlotRef(ring2, 0, ring);
	EXPECT_PRINTS(ring, "[[<0>]]");
	// indentation carries into the lines after the first
	PrintObject(nested, 4);
	EXPECT_PRINTED("[1, \n     {a: 1, b: \"x\"}]");
}


static void
TestFunctions()
{
	// the ROM's functions: a NewtonScript one and a native one
	RefVar fn(GetFrameSlot(RefVar(Rbuiltinfunctions), SYMBOL("OnlyOneRoutingSlip")));
	char expected[64];
	char got[64];
	strncpy(got, Text(fn), sizeof(got) - 1);
	snprintf(expected, sizeof(expected), "<function, 0 arg(s) #%lX>", (long) (Ref) fn);
	EXPECT(strcmp(got, expected) == 0);
	RefVar length(GetFrameSlot(RefVar(Rbuiltinfunctions), SYMBOL("Length")));
	strncpy(got, Text(length), sizeof(got) - 1);
	snprintf(expected, sizeof(expected), "<native function, 1 arg(s) #%lX>", (long) (Ref) length);
	EXPECT(strcmp(got, expected) == 0);
	// without prettyPrint a function is a frame; its instructions print when asked
	SetFrameSlot(RefVar(gVarFrame), RSSYMprettyprint, RefVar(NILREF));
	const char* text = Text(fn);
	EXPECT(strncmp(text, "{class: #32, instructions: <instructions, length 2>, literals: NIL, argFrame: NIL, numArgs: 0", 60) == 0);
	SetFrameSlot(RefVar(gVarFrame), RSSYMprintinstructions, RefVar(TRUEREF));
	text = Text(fn);
	EXPECT(strstr(text, "instructions: <Instrs: push-constant 2, return>") != nil);
	SetFrameSlot(RefVar(gVarFrame), RSSYMprintinstructions, RefVar(NILREF));
	SetFrameSlot(RefVar(gVarFrame), RSSYMprettyprint, RefVar(TRUEREF));
	// the disassembler
	Disassemble(fn);
	EXPECT_PRINTED("   0: push-constant NIL\n   1: return\n");
	RefVar getGlobalVar(GetFrameSlot(RefVar(Rbuiltinfunctions), SYMBOL("GetGlobalVar")));
	Disassemble(getGlobalVar);
	EXPECT_PRINTED("   0: find-var vars\n   1: get-var 3\n   2: get-path 1\n   3: return\n");
	RefVar addHandler(GetFrameSlot(RefVar(Rbuiltinfunctions), SYMBOL("AddPowerOffHandler")));
	Disassemble(addHandler);
	EXPECT_PRINTED("   0: find-var OldPowerOffHandlers\n   1: get-var 3\n   2: freq-func 21 [addArraySlot/2]\n   5: return\n");
	// not a function
	Boolean threw = false;
	newton_try
	{
		Disassemble(RefVar(MAKEINT(1)));
	}
	newton_catch_all
	{
		threw = true;
	}
	end_try;
	EXPECT(threw);
	Reset();
}


static void
TestStrings()
{
	long length;
	UniChar buffer[64];
	EXPECT(StringObject(RefVar(MAKEINT(42)), buffer, length, 63) && length == 2 && buffer[0] == '4' && buffer[1] == '2' && buffer[2] == 0);
	EXPECT(StringObject(RefVar(MAKEINT(-7)), buffer, length, 63) && length == 2 && buffer[0] == '-');
	EXPECT(StringObject(RefVar(NILREF), buffer, length, 63) && length == 0);
	EXPECT(StringObject(RefVar(MAKECHAR('z')), buffer, length, 63) && length == 1 && buffer[0] == 'z');
	EXPECT(StringObject(RefVar(SYMBOL("abc")), buffer, length, 63) && length == 3 && buffer[2] == 'c');
	EXPECT(StringObject(RefVar(MakeReal(2.5)), buffer, length, 63) && length == 3 && buffer[1] == '.');
	EXPECT(StringObject(RefVar(MakeString("hello")), buffer, length, 3) && length == 3 && buffer[3] == 0);
	EXPECT(StringObject(RefVar(MakeString("hello")), nil, length, 100) && length == 5);
	EXPECT(!StringObject(RefVar(AllocateFrame()), buffer, length, 63) && length == 0);
	RefVar str(SPrintObject(RefVar(MAKEINT(123))));
	EXPECT(IsString(str) && Length(str) == 8 && Ustrcmp(GetCString(str), (const UniChar*) u"123") == 0);
	str = SPrintObject(RefVar(SYMBOL("sym")));
	EXPECT(IsString(str) && Ustrcmp(GetCString(str), (const UniChar*) u"sym") == 0);
	str = SPrintObject(RefVar(MakeString("copy")));
	EXPECT(IsString(str) && Ustrcmp(GetCString(str), (const UniChar*) u"copy") == 0);
	EXPECT(!IsRichString(str) && GetStringFormat(str) == 0);
	// the natives
	str = NSCallGlobalFn(RefVar(SYMBOL("SPrintObject")), RefVar(MAKEINT(7)));
	EXPECT(IsString(str) && Ustrcmp(GetCString(str), (const UniChar*) u"7") == 0);
	EXPECT(ISNIL(NSCallGlobalFn(RefVar(SYMBOL("Print")), RefVar(MAKEINT(7)))));
	EXPECT_PRINTED("7\n");
	NSCallGlobalFn(RefVar(SYMBOL("Display")), RefVar(SYMBOL("x")));
	EXPECT_PRINTED("x");
	// ASCIIString
	RefVar ascii(ASCIIString(RefVar(MakeString("plain"))));
	EXPECT(EQ(ClassOf(ascii), RSSYMasciistring) && strcmp(BinaryData(ascii), "plain") == 0);
}


static void
TestREP()
{
	// Print's formats: widths, longs, %U
	UniChar uni[4] = { 'a', 'b', 'c', 0 };
	long n = gREPout->Print("%d|%5d|%-3ld|%lX|%s|%U|%c|%*s|%%", 1, 2, 3L, 255L, "s", uni, 'q', 3, "");
	EXPECT_PRINTED("1|    2|3  |FF|s|abc|q|   |%");
	EXPECT(n == 28);
	REPprintf("%s=%ld", "x", 10L);
	EXPECT_PRINTED("x=10");
	// a long line
	char big[2000];
	memset(big, 'y', 1999);
	big[1999] = 0;
	gREPout->Print("%s", big);
	EXPECT(strlen(Printed()) == 1999);
	// exceptions the REP reports
	Exception ex;
	ex.name = (ExceptionName) "evt.ex.msg";
	ex.data = (void*) "oops";
	REPExceptionNotify(&ex);
	EXPECT_PRINTED("    !!! Exception: oops\n");
	ex.name = (ExceptionName) "evt.ex.fr.intrp";
	ex.data = (void*) (long) -48803;
	REPExceptionNotify(&ex);
	EXPECT_PRINTED("    !!! Exception: evt.ex.fr.intrp (-48803)\n");
	RefVar data(AllocateFrame());
	SetFrameSlot(data, RSSYMerrorcode, RefVar(MAKEINT(-48404)));
	SetFrameSlot(data, RSSYMvalue, RefVar(MakeString("v")));
	RefStruct* dataStruct = new RefStruct(data);
	ex.name = (ExceptionName) "evt.ex.fr.type;type.ref.frame";
	ex.data = (void*) dataStruct;
	REPExceptionNotify(&ex);
	EXPECT_PRINTED("    !!! Exception: evt.ex.fr.type;type.ref.frame {errorCode: -48404, value: \"v\"}\n");
	// with a file and line, which are removed
	SetFrameSlot(data, RSSYMfilename, RefVar(MakeString("test.ns")));
	SetFrameSlot(data, RSSYMlinenumber, RefVar(MAKEINT(12)));
	REPExceptionNotify(&ex);
	EXPECT_PRINTED("    File \"test.ns\"; Line 12 !!! Exception: evt.ex.fr.type;type.ref.frame {errorCode: -48404, value: \"v\"}\n");
	EXPECT(!FrameHasSlot(data, RSSYMfilename));
	delete dataStruct;
	// a real exception while printing is reported in place
	RefVar bad(AllocateFrame());
	SetFrameSlot(bad, SYMBOL("r"), RefVar(AllocateBinary(RSSYMreal, 2)));		// a real of the wrong size
	const char* text = Text(bad);
	EXPECT(strncmp(text, "{r: ", 4) == 0);
	// the null translator prints nothing
	POutTranslator* null = nil;
	EXPECT(CreateNullOutTranslator(&null) == noErr && null != nil);
	EXPECT(null->Print("%d", 1) == 0);
	POutTranslator* saved = gREPout;
	gREPout = null;
	PrintObject(RefVar(MAKEINT(1)), 0);
	gREPout = saved;
	null->Delete();
	EXPECT_PRINTED("");
	// the REP's globals
	EXPECT(RINT(GetFrameSlot(RefVar(gVarFrame), RSSYMprintdepth)) == 3);
	EXPECT(GetFrameSlot(RefVar(gVarFrame), RSSYMvars) == gVarFrame);
	EXPECT(GetFrameSlot(RefVar(gVarFrame), RSSYMfunctions) == gFunctionFrame);
	EXPECT(gREPContext == gVarFrame && gREPin != nil);
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_ROM_IMAGE) != noErr)
	{
		printf("test_Printer: cannot import %s\n", NEWTON_ROM_IMAGE);
		return 1;
	}
	gObjectHeapSize = 0x100000;
	InitObjects();
	EXPECT(gREPout != nil);												// the printer's null translator
	gOutput = tmpfile();
	if (gOutput == nil)
	{
		printf("test_Printer: no temporary file\n");
		return 1;
	}
	HostInitREP(gOutput);
	EXPECT_PRINTED("\nWelcome to NewtonScript!\n\n");
	newton_try
	{
		TestScalars();
		TestAggregates();
		TestFunctions();
		TestStrings();
		TestREP();
	}
	newton_catch_all
	{
		failures++;
		fprintf(stderr, "FAIL: unhandled exception %s\n", _info.exception.name);
	}
	end_try;
	fclose(gOutput);
	if (failures == 0)
		printf("test_Printer: all passed\n");
	else
		printf("test_Printer: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
