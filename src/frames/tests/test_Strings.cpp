// String and array natives test: NewtonScript source run over the ROM
// image exercising the string functions (StrLen, StrConcat, SubStr,
// StrPos, Upcase, TrimString, ParamStr, ...), the sorts (Sort, StableSort,
// ShellSort, InsertionSort) with their test symbols, closures and keys,
// the searches (LSearch, BSearchLeft, BFind, BInsert, BDelete, ...), the
// set operations (BMerge, BIntersect, BDifference, SetUnion, ...), the
// binary accessors (ExtractLong, StuffByte, ...) and the unordered
// comparisons; and TRichString directly.

#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "Compiler.h"
#include "REPTranslators.h"
#include "RichString.h"
#include "ROMImport.h"
#include "ROMConstants.h"
#include "Unicode.h"
#include "HostOrder.h"
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


// source compiled and run at the top level
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
#define THROWS(expr) do { gThrown = nil; gThrownCode = 0; long valueDepth = gInterpreter->ValuePosition(); newton_try { (void) (expr); } newton_catch_all { gThrown = _info.exception.name; if (Subexception((ExceptionName) gThrown, (ExceptionName) "type.ref")) { RefStruct* data = (RefStruct*) _info.exception.data; if (IsFrame(*data)) gThrownCode = RINT(GetFrameSlot(*data, RSSYMerrorcode)); } else gThrownCode = (long) (Long) _info.exception.data; } end_try; gInterpreter->fValueStack.Reset(valueDepth); } while (0)

static Boolean
StringEquals(RefArg str, const char* text)
{
	if (!IsString(str))
		return false;
	const UniChar* s = GetCString(str);
	for (; *text != 0; text++, s++)
		if (*s != (UniChar) (unsigned char) *text)
			return false;
	return *s == 0;
}

// the object printed (anything printed before it dropped)
static const char*
Printed(RefArg obj)
{
	Printed();
	PrintObject(obj, 0);
	return Printed();
}

#define EXPECT_INT(source, value) do { Ref r = Eval(source); if (!ISINT(r) || RINT(r) != (value)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, source); } } while (0)
#define EXPECT_NIL(source) do { Ref r = Eval(source); if (r != NILREF) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, source); } } while (0)
#define EXPECT_TRUE(source) do { Ref r = Eval(source); if (r != TRUEREF) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, source); } } while (0)
#define EXPECT_STRING(source, text) do { RefVar r(Eval(source)); if (!StringEquals(r, text)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, source); } } while (0)
#define EXPECT_CHAR(source, c) do { Ref r = Eval(source); if (r != MAKECHAR(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, source); } } while (0)
#define EXPECT_PRINTS(source, text) do { RefVar r(Eval(source)); const char* p = Printed(r); if (strcmp(p, text) != 0) { failures++; fprintf(stderr, "FAIL %s:%d: %s printed %s\n", __FILE__, __LINE__, source, p); } } while (0)
#define EXPECT_THROWS(source, code) do { THROWS(Eval(source)); if (gThrownCode != (code)) { failures++; fprintf(stderr, "FAIL %s:%d: %s threw %s %ld\n", __FILE__, __LINE__, source, gThrown ? gThrown : "nothing", gThrownCode); } } while (0)


// The character tables of the ROM's 'unicode frame (InitUnicode): Mac
// Roman each way, the case and diacritical conversions, the delimiters.
static void
TestUnicodeTables()
{
	EXPECT(gUnicodeInited && gUnicode[kMacRomanEncoding].fToUnicode != nil && gUnicode[kASCIIEncoding].fToUnicode == nil);
	unsigned char mac[4] = { 0x8e, 0xa5, 'a', 0 };		// e acute, bullet
	UniChar uni[4];
	ConvertToUnicode(mac, uni, kMacRomanEncoding, 3);
	EXPECT(uni[0] == 0xe9 && uni[1] == 0x2022 && uni[2] == 'a' && uni[3] == 0);
	EXPECT(U_CONST_CHAR(0xab) == 0xb4 && U_CONST_CHAR(0xc1) == 0xa1 && U_CONST_CHAR('z') == 'z');
	UniChar back[4] = { 0xe9, 0x2022, 0x4e2d, 0 };		// the third has no Mac Roman
	unsigned char narrow[4];
	ConvertFromUnicode(back, narrow, kMacRomanEncoding, 3);
	EXPECT(narrow[0] == 0x8e && narrow[1] == 0xa5 && narrow[2] == 0x1a && narrow[3] == 0);
	EXPECT(A_CONST_CHAR(0xb4) == (char) 0xab && A_CONST_CHAR('q') == 'q');
	// cases: a-umlaut, sharp s, plain letters
	UniChar text[5] = { 0xe4, 'b', 'C', 0xdf, 0 };
	UppercaseText(text, 4);
	EXPECT(text[0] == 0xc4 && text[1] == 'B' && text[2] == 'C' && text[3] == 0xdf);
	LowercaseText(text, 4);
	EXPECT(text[0] == 0xe4 && text[1] == 'b' && text[2] == 'c');
	NoDiacriticsText(text, 4);
	EXPECT(text[0] == 'a' && text[1] == 'b');
	UniChar e[2] = { 0xe9, 0 };
	UppercaseNoDiacriticsText(e, 1);
	EXPECT(e[0] == 'E');
	// StripDiacriticals: in place, over the same table
	UniChar accented[5] = { 0xe9, 't', 0xe9, 0xe4, 0 };
	RefVar strip(MakeString(accented));
	SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("accented")), strip);
	EXPECT_STRING("StripDiacriticals(accented)", "etea");
	EXPECT(StringEquals(strip, "etea"));
	EXPECT(UToUpper(0xe9) == 0xc9 && UToLower(0xc9) == 0xe9 && ToggleCase('a') == 'A' && ToggleCase('A') == 'a' && ToggleCase('1') == '1');
	EXPECT(IsAlphabet(0xe4) && IsAlphabet(0xdf) && !IsAlphabet('1') && !IsAlphabet(0x2022));
	// the break table: what ends a word (not '_', as the ROM has it)
	EXPECT(IsDelimiter(' ') && IsDelimiter(',') && IsDelimiter('?') && IsDelimiter('@') && !IsDelimiter('a') && !IsDelimiter('_'));
}


// A rich string built by hand: the text in ASCII, with '#' standing for an
// ink word, and the ink words' bytes in text order.  The layout is the
// one SetFormatAndLength reads - the text, padding to a word, a blob per
// ink word (its length halfword, its bytes, padding) and the trailer.
static Ref
MakeInkString(const char* text, const char* const* inks, long inkCount)
{
	long length = (long) strlen(text);
	long inkOffset = (length * (long) sizeof(UniChar) + 5) & ~3;
	long inkBytes = 0;
	for (long i = 0; i < inkCount; i++)
		inkBytes += (long) InkBlobSize((ULong) strlen(inks[i]));
	RefVar str(AllocateBinary(RSSYMstring, inkOffset + inkBytes + 4));
	char* base = (char*) BinaryData(str);
	UniChar* chars = (UniChar*) base;
	for (long i = 0; i < length; i++)
		chars[i] = text[i] == '#' ? kInkChar : (UniChar) text[i];
	chars[length] = 0;
	char* ink = base + inkOffset;
	for (long i = 0; i < inkCount; i++)
	{
		long n = (long) strlen(inks[i]);
		*(UniChar*) ink = (UniChar) n;
		memcpy(ink + sizeof(UniChar), inks[i], (size_t) n);
		ink += InkBlobSize((ULong) n);
	}
	ULong trailer = ((ULong) length << 4) | 1;
	UniChar* end = (UniChar*) (base + inkOffset + inkBytes + 4);
	end[-2] = (UniChar) (trailer >> 16);
	end[-1] = (UniChar) trailer;
	return str;
}


// The string's characters, with '#' for each ink word.
static Boolean
InkTextIs(const TRichString& r, const char* expect)
{
	if (r.Length() != (long) strlen(expect))
		return false;
	const UniChar* text = r.GrabPtr();
	Boolean same = true;
	for (long i = 0; expect[i] != 0; i++)
	{
		UniChar want = expect[i] == '#' ? kInkChar : (UniChar) expect[i];
		if (text[i] != want)
			same = false;
	}
	r.ReleasePtr();
	return same;
}


// A rich string's ink: the blobs are kept in text order after the text,
// moved with the characters they belong to, and compared by their bytes.
static void
TestRichStringInk()
{
	const char* two[] = { "AAAA", "BB" };
	RefVar str(MakeInkString("a#b#c", two, 2));
	TRichString r(str);
	EXPECT(r.Verify() == 0);
	EXPECT(r.Format() == kRichStringFormatInk && r.Length() == 5);
	EXPECT(r.NumInkWords() == 2);
	EXPECT(r.NumInkWordsInRange(0, 2) == 1 && r.NumInkWordsInRange(2, 3) == 1);
	EXPECT(r.NumInkWordsInRange(0, 5) == 2 && r.NumInkWordsInRange(0, 1) == 0);
	EXPECT(r.InkWordNoAtOffset(1) == 0 && r.InkWordNoAtOffset(3) == 1);
	EXPECT(r.InkWordNoAtOffset(0) == -1 && r.InkWordNoAtOffset(4) == -1);
	// "a", the first ink word, "b", the second, "c"
	EXPECT(r.NumInkAndTextRunsInRange(0, 5) == 5);
	EXPECT(r.NumInkAndTextRunsInRange(0, 3) == 3);

	ULong offset, size;
	r.GetInkData(0, 5, &offset, &size);
	EXPECT(offset == 0 && size == InkBlobSize(4) + InkBlobSize(2));
	r.GetInkData(2, 3, &offset, &size);
	EXPECT(offset == InkBlobSize(4) && size == InkBlobSize(2));
	r.GetInkData(0, 1, &offset, &size);
	EXPECT(size == 0);
	RefVar word(r.CloneInkWordNo(1));
	EXPECT(IsInkWord(word) && Length(word) == 2 && memcmp(BinaryData(word), "BB", 2) == 0);

	// the first ink word deleted: the second moves up with its character
	r.DeleteRange(1, 1);
	EXPECT(InkTextIs(r, "ab#c"));
	EXPECT(r.Format() == kRichStringFormatInk && r.NumInkWords() == 1);
	EXPECT(Length(str) == (long) (((4 * sizeof(UniChar) + 5) & ~3) + InkBlobSize(2) + 4));
	word = r.CloneInkWordNo(0);
	EXPECT(Length(word) == 2 && memcmp(BinaryData(word), "BB", 2) == 0);

	// and the last one: with no ink left the string is plain again
	r.DeleteRange(2, 1);
	EXPECT(InkTextIs(r, "abc"));
	EXPECT(r.Format() == kRichStringFormatPlain && r.NumInkWords() == 0);
	EXPECT(Length(str) == 4 * (long) sizeof(UniChar));
	EXPECT(StringEquals(str, "abc"));

	// ink put into a plain string makes it rich, and brings its blob
	const char* one[] = { "CCC" };
	RefVar source(MakeInkString("<#>", one, 1));
	TRichString src(source);
	r.InsertRange(src, 1, 1, 1);
	EXPECT(InkTextIs(r, "a#bc"));
	EXPECT(r.Format() == kRichStringFormatInk && r.NumInkWords() == 1);
	EXPECT(r.Verify() == 0);
	word = r.CloneInkWordNo(0);
	EXPECT(Length(word) == 3 && memcmp(BinaryData(word), "CCC", 3) == 0);

	// two ink words compare by their data, not as the character that
	// stands for them
	const char* aa[] = { "AAAA" };
	const char* bb[] = { "AAAB" };
	RefVar first(MakeInkString("#", aa, 1));
	RefVar second(MakeInkString("#", bb, 1));
	RefVar same(MakeInkString("#", aa, 1));
	TRichString sf(first), ss(second), sm(same);
	EXPECT(sf.CompareSubStringCommon(sm, 0, -1, false) == 0);
	EXPECT(sf.CompareSubStringCommon(ss, 0, -1, false) < 0);
	EXPECT(ss.CompareSubStringCommon(sf, 0, -1, false) > 0);

	// a rich string read and written as NewtonScript reads it: through
	// TRichString, so its bound is its text (5), not the binary, which
	// carries the ink after the text
	RefVar rs(MakeInkString("a#b#c", two, 2));
	SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("rs")), rs);
	EXPECT_CHAR("aref(rs, 4)", 'c');
	EXPECT_CHAR("aref(rs, 1)", kInkChar);
	EXPECT_THROWS("aref(rs, 5)", kNSErrOutOfBounds);
	EXPECT_CHAR("rs[1]", kInkChar);					// the interpreter's aref too
	// GetChar at an ink character: a copy of the word
	RefVar got(Eval("GetChar(rs, 3)"));
	EXPECT(IsInkWord(got) && Length(got) == 2 && memcmp(BinaryData(got), "BB", 2) == 0);
	// SetChar with an ink word: spliced in as one character with its blob
	Eval("SetChar(rs, 0, GetChar(rs, 1))");
	TRichString after(rs);
	EXPECT(InkTextIs(after, "##b#c") && after.NumInkWords() == 3 && after.Verify() == 0);
	RefVar firstWord(after.CloneInkWordNo(0));
	EXPECT(Length(firstWord) == 4 && memcmp(BinaryData(firstWord), "AAAA", 4) == 0);
	// ... with something else: nothing changes
	Eval("SetChar(rs, 2, 7)");
	EXPECT(InkTextIs(TRichString(rs), "##b#c"));
	// a character over an ink word takes the ink out with it
	Eval("SetChar(rs, 0, $z)");
	TRichString replaced(rs);
	EXPECT(InkTextIs(replaced, "z#b#c") && replaced.NumInkWords() == 2);
	// setAref may not store 0 or the ink character
	EXPECT_THROWS("setAref(rs, 2, $\\u0000)", kNSErrBadCharForString);
	EXPECT_THROWS("setAref(rs, 2, $\\uF700)", kNSErrBadCharForString);
	EXPECT_THROWS("rs[2] := $\\uF700", kNSErrBadCharForString);
	Eval("setAref(rs, 2, $y)");
	EXPECT(InkTextIs(TRichString(rs), "z#y#c"));

	// StrMunger edits through TRichString: the ink moves with its character
	RefVar munged(MakeInkString("a#b#c", two, 2));
	StrMunger(munged, 0, 1, RefVar(MakeString("xy")), 0, -1);
	TRichString m(munged);
	EXPECT(InkTextIs(m, "xy#b#c") && m.NumInkWords() == 2 && m.Verify() == 0);
	RefVar secondWord(m.CloneInkWordNo(1));
	EXPECT(Length(secondWord) == 2 && memcmp(BinaryData(secondWord), "BB", 2) == 0);
	StrMunger(munged, 2, 2, RefVar(NILREF), 0, 0);			// the first word and "b" out
	TRichString m2(munged);
	EXPECT(InkTextIs(m2, "xy#c") && m2.NumInkWords() == 1 && m2.Verify() == 0);
	StrMunger(munged, 0, -1, RefVar(MakeString("plain")), 0, -1);	// all of it: plain again
	EXPECT(StringEquals(munged, "plain") && TRichString(munged).Format() == kRichStringFormatPlain);

	// Stringer (the & operator) carries a rich string's ink over
	const char* three[] = { "CCC" };
	SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("rs1")), RefVar(MakeInkString("a#b#c", two, 2)));
	SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("rs2")), RefVar(MakeInkString("d#", three, 1)));
	RefVar joined(Eval("rs1 & \"-\" & rs2"));
	TRichString j(joined);
	EXPECT(InkTextIs(j, "a#b#c-d#") && j.NumInkWords() == 3 && j.Verify() == 0);
	RefVar w0(j.CloneInkWordNo(0)), w1(j.CloneInkWordNo(1)), w2(j.CloneInkWordNo(2));
	EXPECT(Length(w0) == 4 && memcmp(BinaryData(w0), "AAAA", 4) == 0);
	EXPECT(Length(w1) == 2 && memcmp(BinaryData(w1), "BB", 2) == 0);
	EXPECT(Length(w2) == 3 && memcmp(BinaryData(w2), "CCC", 3) == 0);
}


static void
TestRichString()
{
	RefVar str(MakeString("hello"));
	TRichString s(str);
	EXPECT(s.Length() == 5);
	EXPECT(s.Format() == kRichStringFormatPlain);
	EXPECT(s.GetChar(1) == 'e');
	EXPECT(s.Verify() == 0);
	s.SetChar(0, 'j');
	EXPECT(StringEquals(str, "jello"));
	RefVar other(MakeString("XYZ"));
	TRichString o(other);
	s.InsertRange(o, 1, 2, 5);
	EXPECT(StringEquals(str, "jelloYZ"));
	s.DeleteRange(0, 2);
	EXPECT(StringEquals(str, "lloYZ"));
	EXPECT(s.Length() == 5);
	s.MungeRange(1, 2, &o, 0, 3);
	EXPECT(StringEquals(str, "lXYZYZ"));
	EXPECT(Length(str) == 7 * (long) sizeof(UniChar));
	// comparison: cases folded unless exact
	RefVar a(MakeString("abc"));
	RefVar b(MakeString("ABC"));
	TRichString sa(a), sb(b);
	EXPECT(sa.CompareSubStringCommon(sb, 0, -1, false) == 0);
	EXPECT(sa.CompareSubStringCommon(sb, 0, -1, true) != 0);
	// C string views
	UniChar text[8];
	ConvertToUnicode("abcdef", text, kMacRomanEncoding, 7);		// (encoding 0 has no table once the ROM's are installed)
	TRichString c(text);
	EXPECT(c.Length() == 6 && c.GetChar(5) == 'f');
	EXPECT(sa.CompareSubStringCommon(c, 0, 3, false) < 0);		// "abc" vs "abcdef"
	// a rich string: text, ink, trailer
	{
		RefVar rich(AllocateBinary(RSSYMstring, 12));
		UniChar* p = (UniChar*) BinaryData(rich);
		p[0] = 'a'; p[1] = kInkChar; p[2] = 0;		// text of 2 (with one ink word), padded
		p[3] = 0;
		ULong trailer = (2 << 4) | 1;
		p[4] = (UniChar) (trailer >> 16); p[5] = (UniChar) trailer;
		TRichString r(rich);
		EXPECT(r.Format() == kRichStringFormatInk);
		EXPECT(r.Length() == 2);
		EXPECT(IsRichString(rich));
		EXPECT(Eval("IsRichString(\"plain\")") == NILREF);
	}
}


static void
TestStringFunctions()
{
	EXPECT_INT("StrLen(\"hello\")", 5);
	EXPECT_INT("StrLen(\"\")", 0);
	EXPECT_STRING("StrConcat(\"foo\", \"bar\")", "foobar");
	EXPECT_STRING("\"foo\" & \"bar\" && \"baz\"", "foobar baz");
	EXPECT_STRING("SubStr(\"hello world\", 6, 5)", "world");
	EXPECT_STRING("SubStr(\"hello world\", 6, nil)", "world");
	EXPECT_STRING("SubStr(\"hello\", 0, 2)", "he");
	EXPECT_TRUE("StrEqual(\"Hello\", \"hello\")");
	EXPECT_NIL("StrEqual(\"Hello\", \"hello!\")");
	EXPECT_INT("StrExactCompare(\"a\", \"a\")", 0);
	EXPECT_TRUE("StrExactCompare(\"a\", \"B\") > 0");
	EXPECT_TRUE("StrExactCompare(\"A\", \"b\") < 0");
	EXPECT_INT("StrCompare(\"abc\", \"ABC\")", 0);
	EXPECT_TRUE("StrCompare(\"abc\", \"abd\") < 0");
	EXPECT_TRUE("StrCompare(\"abcd\", \"abc\") > 0");
	EXPECT_TRUE("BeginsWith(\"hello\", \"HE\")");
	EXPECT_NIL("BeginsWith(\"hello\", \"hello!\")");
	EXPECT_TRUE("EndsWith(\"hello\", \"LLO\")");
	EXPECT_NIL("EndsWith(\"hello\", \"ell\")");
	EXPECT_TRUE("StrFilled(\"x\")");
	EXPECT_NIL("StrFilled(\"\")");
	EXPECT_NIL("StrFilled(nil)");

	// case
	EXPECT_STRING("Upcase(\"hello World\")", "HELLO WORLD");
	EXPECT_STRING("Downcase(\"Hello WORLD\")", "hello world");
	EXPECT_CHAR("Upcase($a)", 'A');
	EXPECT_CHAR("Downcase($Z)", 'z');
	EXPECT_STRING("Capitalize(\"hello world\")", "Hello world");
	EXPECT_STRING("CapitalizeWords(\"hello big world\")", "Hello Big World");
	EXPECT_STRING("CapitalizeWords(\"o'neil-smith\")", "O'Neil-Smith");
	EXPECT_STRING("local s := \"abc\"; Upcase(s); s", "ABC");			// in place

	// trimming, searching, replacing
	EXPECT_STRING("TrimString(\"  hello  \")", "hello");
	EXPECT_STRING("TrimString(\"\\t x\")", "x");
	EXPECT_STRING("TrimString(\"   \")", "");
	// SplitString: the words between the spaces.  The array starts one slot
	// long and grows, so a string with no words in it answers [nil].
	EXPECT_INT("Length(SplitString(\"Daniel Parnell\"))", 2);
	EXPECT_STRING("SplitString(\"Daniel Parnell\")[0]", "Daniel");
	EXPECT_STRING("SplitString(\"Daniel Parnell\")[1]", "Parnell");
	EXPECT_INT("Length(SplitString(\"  John   Q  Public  \"))", 3);
	EXPECT_STRING("SplitString(\"  John   Q  Public  \")[2]", "Public");
	EXPECT_STRING("SplitString(\"one\")[0]", "one");
	EXPECT_INT("Length(SplitString(\"\"))", 1);
	EXPECT_NIL("SplitString(\"\")[0]");
	EXPECT_NIL("SplitString(\"   \")[0]");
	// StringFilter: the six modes, over the characters in the set
	EXPECT_STRING("StringFilter(\"a1b2c3\", \"0123456789\", 'passAll)", "123");
	EXPECT_STRING("StringFilter(\"a1b2c3\", \"0123456789\", 'rejectAll)", "abc");
	EXPECT_STRING("StringFilter(\"aa  bb   cc\", \" \", 'passOne)", "aa bb cc");		// a run in the set cut to one
	EXPECT_STRING("StringFilter(\"aa  bb   cc\", \" \", 'rejectOne)", "a  b   c");	// a run outside it cut to one
	EXPECT_STRING("StringFilter(\"  hello  world \", \" \", 'rejectBeginning)", "hello  world ");
	EXPECT_STRING("StringFilter(\"xxABCxx\", \"ABC\", 'passBeginning)", "ABCxx");
	EXPECT_STRING("StringFilter(\"abc\", \"\", 'rejectAll)", "abc");					// an empty set matches nothing
	EXPECT_STRING("StringFilter(\"abc\", \"\", 'passAll)", "");
	EXPECT_STRING("StringFilter(\"\", \" \", 'rejectBeginning)", "");
	// SubstituteChars: a character for the one in the same place in the
	// replacement, the replacement starting again when it is the shorter
	EXPECT_STRING("SubstituteChars(\"a-b-c\", \"-\", \"+\")", "a+b+c");
	EXPECT_STRING("SubstituteChars(\"abcd\", \"abc\", \"xyz\")", "xyzd");
	EXPECT_STRING("SubstituteChars(\"abcd\", \"abc\", \"z\")", "zzzd");
	EXPECT_STRING("SubstituteChars(\"hello\", \"xyz\", \"abc\")", "hello");
	// nothing substituted: the very string that went in comes back
	EXPECT_TRUE("local s := \"hello\"; SubstituteChars(s, \"xyz\", \"abc\") = s");
	// something substituted: a clone, the original left as it was
	EXPECT_STRING("local s := \"a-b\"; SubstituteChars(s, \"-\", \"+\"); s", "a-b");
	EXPECT_INT("CharPos(\"hello\", $l, 0)", 2);
	EXPECT_INT("CharPos(\"hello\", $l, 3)", 3);
	EXPECT_NIL("CharPos(\"hello\", $z, 0)");
	EXPECT_INT("StrPos(\"hello world\", \"WOR\", 0)", 6);
	EXPECT_INT("StrPos(\"hello world\", \"o\", 5)", 7);
	EXPECT_NIL("StrPos(\"hello\", \"lo!\", 0)");
	EXPECT_NIL("StrPos(\"hello\", \"h\", 1)");
	EXPECT_INT("StrPos(\"hello\", \"\", 2)", 2);
	EXPECT_INT("local s := \"a-b-c-d\"; StrReplace(s, \"-\", \"+\", nil)", 3);
	EXPECT_STRING("local s := \"a-b-c-d\"; StrReplace(s, \"-\", \"+\", nil); s", "a+b+c+d");
	EXPECT_STRING("local s := \"a-b-c-d\"; StrReplace(s, \"-\", \"--\", 2); s", "a--b--c-d");
	EXPECT_STRING("local s := \"aXbXc\"; StrReplace(s, \"x\", \"\", nil); s", "abc");
	EXPECT_INT("local s := \"abc\"; StrReplace(s, \"abcd\", \"x\", nil)", 0);
	EXPECT_CHAR("GetChar(\"hello\", 1)", 'e');
	EXPECT_STRING("local s := \"hello\"; SetChar(s, 0, $j); s", "jello");
	EXPECT_INT("FindStringInArray([\"a\", \"b\", \"c\"], \"b\")", 1);
	EXPECT_NIL("FindStringInArray([\"a\", \"b\", \"c\"], \"B\")");
	EXPECT_TRUE("FindStringInFrame({a: 1, b: {c: \"hello world\"}}, [\"WORLD\"], nil)");
	EXPECT_NIL("FindStringInFrame({a: 1, b: {c: \"hello world\"}}, [\"ORLD\"], nil)");		// words only
	EXPECT_NIL("FindStringInFrame({a: 1, b: {c: \"hello world\"}}, [\"hello\", \"moon\"], nil)");
	EXPECT_TRUE("FindStringInFrame({a: \"the moon\", b: {c: \"hello world\"}}, [\"hello\", \"moon\"], nil)");
	EXPECT_TRUE("FindStringInFrame({a: [\"x\", \"the moon\"]}, [\"moon\"], nil)");
	EXPECT_PRINTS("FindStringInFrame({a: \"hi\", b: {c: \"hello hi\"}}, [\"hi\"], true)", "[\"hi\", a, 0, \"hello hi\", b.c, 6]");
	EXPECT_PRINTS("FindStringInFrame({a: \"hi hi\"}, [\"hi\"], true)", "[\"hi hi\", a, 0, \"hi hi\", a, 3]");
	EXPECT_PRINTS("FindStringInFrame({a: \"hi hi\"}, [\"hi\"], 'first)", "[\"hi hi\", a, 0]");
	EXPECT_NIL("FindStringInFrame({a: \"hi\"}, [\"yo\"], true)");

	// characters
	EXPECT_TRUE("IsAlphaNumeric($a)");
	EXPECT_NIL("IsAlphaNumeric($-)");
	EXPECT_TRUE("IsWhiteSpace($ )");
	EXPECT_NIL("IsWhiteSpace($x)");
	EXPECT_NIL("IsInkChar($x)");
	EXPECT_TRUE("IsValidString(\"abc\")");
	EXPECT_NIL("IsValidString(42)");
	EXPECT_TRUE("SymbolCompareLex('abc, 'abd) < 0");
	EXPECT_INT("SymbolCompareLex('Abc, 'abc)", 0);

	// numbers
	EXPECT_STRING("NumberStr(42)", "42");
	EXPECT_STRING("NumberStr(-7)", "-7");
	EXPECT_NIL("NumberStr(\"x\")");
	// StringToNumber reads through the locale's number dictionary, which
	// the recognition system opens and walks (intl/LexParse.h): without it
	// nothing is read (host.NewtonDateParse reads numbers with it)
	EXPECT_NIL("StringToNumber(\"42\")");
	EXPECT_NIL("StringToNumber(\"abc\")");
	EXPECT_NIL("StringToNumber(\"\")");
	EXPECT_STRING("FormattedNumberStr(3.14159, \"%.2f\")", "3.14");

	// ParamStr
	EXPECT_STRING("ParamStr(\"^0 and ^1\", [\"a\", 1])", "a and 1");
	EXPECT_STRING("ParamStr(\"^1^0\", [\"x\", \"y\"])", "yx");
	EXPECT_STRING("ParamStr(\"no params\", [])", "no params");
	EXPECT_STRING("ParamStr(\"^?0yes|no|\", [\"x\"])", "yes");
	EXPECT_STRING("ParamStr(\"^?0yes|no|\", [nil])", "no");
	EXPECT_STRING("ParamStr(\"^?0yes|no|\", [\"\"])", "no");
	EXPECT_STRING("ParamStr(\"^?0^0 items|none|.\", [\"3\"])", "3 items.");
	EXPECT_STRING("ParamStr(\"^?0^0 items|none|.\", [nil])", "none.");
	EXPECT_STRING("ParamStr(\"a ^^ b\", [])", "a ^ b");
	EXPECT_STRING("ParamStr(\"^0\", ['sym])", "sym");
	EXPECT_STRING("ParamStr(\"^0\", [[1, 2]])", "");		// only what Stringer prints
	EXPECT_STRING("ParamStr(\"x^0y\", [\"\"])", "xy");

	// errors
	EXPECT_THROWS("StrLen(42)", kNSErrNotAString);
	EXPECT_THROWS("Upcase(42)", kNSErrNotAString);
	EXPECT_THROWS("CharPos(\"x\", 42, 0)", kNSErrNotACharacter);
	EXPECT_THROWS("SymbolCompareLex(\"a\", 'b)", kNSErrNotASymbol);
}


// SmartStart/SmartConcat/SmartStop build a string a piece at a time in one
// object: a buffer, the pieces written at the count the last call gave
// back, and the object cut to fit at the end.
static void
TestSmartStrings()
{
	EXPECT_STRING("local s := SmartStart(64); local n := 0; "
				  "n := SmartConcat(s, n, \"Hello\"); n := SmartConcat(s, n, \" \"); "
				  "n := SmartConcat(s, n, \"world\"); SmartStop(s, n); s", "Hello world");
	// a single character is a piece too
	EXPECT_STRING("local s := SmartStart(64); local n := SmartConcat(s, 0, \"ab\"); "
				  "n := SmartConcat(s, n, $c); SmartStop(s, n); s", "abc");
	// the count that comes back is the characters written so far
	EXPECT_INT("local s := SmartStart(64); SmartConcat(s, 0, \"abc\")", 3);
	EXPECT_INT("local s := SmartStart(64); SmartConcat(s, 3, $x)", 4);
	// SmartStop cuts the object back to the text and its terminator
	EXPECT_INT("local s := SmartStart(100); local n := SmartConcat(s, 0, \"abc\"); "
			   "SmartStop(s, n); Length(s)", 8);
	// a buffer too small for the pieces grows as they go in
	EXPECT_INT("local s := SmartStart(4); local n := 0; "
			   "for i := 1 to 40 do n := SmartConcat(s, n, \"ab\"); SmartStop(s, n); StrLen(s)", 80);
	EXPECT_TRUE("local s := SmartStart(4); local n := 0; "
				"for i := 1 to 40 do n := SmartConcat(s, n, $z); SmartStop(s, n); "
				"StrLen(s) = 40 and BeginsWith(s, \"zzzz\")");
	// and a piece longer than the 0x80 bytes it grows by makes room for itself
	EXPECT_INT("local s := SmartStart(4); local long := SmartStart(400); "
			   "local m := 0; for i := 1 to 150 do m := SmartConcat(long, m, $q); SmartStop(long, m); "
			   "local n := SmartConcat(s, 0, long); SmartStop(s, n); StrLen(s)", 150);
	// a character that cannot go into a string
	EXPECT_THROWS("local s := SmartStart(8); SmartConcat(s, 0, $\\u0000)", kNSErrNotACharacter);
	EXPECT_THROWS("local s := SmartStart(8); SmartConcat(s, 0, $\\uF700)", kNSErrNotACharacter);
}


static void
TestSorting()
{
	EXPECT_PRINTS("Sort([3, 1, 2], '|<|, nil)", "[1, 2, 3]");
	EXPECT_PRINTS("Sort([3, 1, 2], '|>|, nil)", "[3, 2, 1]");
	EXPECT_PRINTS("Sort([1.5, 1, 2], '|<|, nil)", "[1, 1.50000, 2]");
	EXPECT_PRINTS("Sort([\"pear\", \"Apple\", \"banana\"], '|str<|, nil)", "[\"Apple\", \"banana\", \"pear\"]");
	EXPECT_PRINTS("Sort([\"pear\", \"Apple\", \"banana\"], '|str>|, nil)", "[\"pear\", \"banana\", \"Apple\"]");
	EXPECT_PRINTS("Sort([$c, $a, $b], '|chr<|, nil)", "[$a, $b, $c]");
	EXPECT_PRINTS("Sort([$c, $a, $b], '|chr>|, nil)", "[$c, $b, $a]");
	EXPECT_PRINTS("Sort(['c, 'a, 'b], '|sym<|, nil)", "[a, b, c]");
	EXPECT_PRINTS("Sort(['c, 'a, 'b], '|sym>|, nil)", "[c, b, a]");
	// a closure test, and keys
	EXPECT_PRINTS("Sort([3, 1, 2], func(a, b) b - a, nil)", "[3, 2, 1]");
	EXPECT_PRINTS("Sort([{n: 3}, {n: 1}, {n: 2}], '|<|, 'n)", "[{n: 1}, {n: 2}, {n: 3}]");
	EXPECT_PRINTS("Sort([{p: {n: 3}}, {p: {n: 1}}], '|<|, 'p.n)", "[{p: {n: 1}}, {p: {n: 3}}]");
	EXPECT_PRINTS("Sort([[9, 3], [8, 1], [7, 2]], '|<|, 1)", "[[8, 1], [7, 2], [9, 3]]");
	EXPECT_PRINTS("Sort([3, 1, 2], '|<|, func(x) -x)", "[3, 2, 1]");
	// runs longer than the insertion-sort cutoff, sorted in place
	EXPECT_PRINTS("local a := []; for i := 30 to 1 by -1 do AddArraySlot(a, i * 7 mod 31); Sort(a, '|<|, nil); [a[0], a[1], a[29], Length(a)]", "[1, 2, 30, 30]");
	EXPECT_TRUE("local a := []; for i := 1 to 200 do AddArraySlot(a, (i * 7919) mod 1000); Sort(a, '|<|, nil); local ok := true; for i := 1 to 199 do if a[i - 1] > a[i] then ok := nil; ok");
	EXPECT_TRUE("local a := []; for i := 1 to 200 do AddArraySlot(a, (i * 7919) mod 1000); Sort(a, '|>|, nil); local ok := true; for i := 1 to 199 do if a[i - 1] < a[i] then ok := nil; ok");
	EXPECT_PRINTS("local a := [2, 1]; Sort(a, '|<|, nil); a", "[1, 2]");
	EXPECT_PRINTS("Sort([], '|<|, nil)", "[]");
	EXPECT_PRINTS("Sort([1], '|<|, nil)", "[1]");

	// the other sorts
	EXPECT_PRINTS("ShellSort([5, 3, 1, 4, 2], '|<|, nil)", "[1, 2, 3, 4, 5]");
	EXPECT_TRUE("local a := []; for i := 1 to 100 do AddArraySlot(a, (i * 7919) mod 1000); ShellSort(a, '|<|, nil); local ok := true; for i := 1 to 99 do if a[i - 1] > a[i] then ok := nil; ok");
	EXPECT_PRINTS("InsertionSort([5, 3, 1, 4, 2], '|>|, nil)", "[5, 4, 3, 2, 1]");
	// StableSort keeps equal keys in order
	EXPECT_PRINTS("StableSort([{k: 2, v: 1}, {k: 1, v: 2}, {k: 2, v: 3}, {k: 1, v: 4}], '|<|, 'k)",
		"[{k: 1, v: 2}, {k: 1, v: 4}, {k: 2, v: 1}, {k: 2, v: 3}]");
	EXPECT_PRINTS("StableSort([3, 1, 2], '|<|, nil)", "[1, 2, 3]");
	EXPECT_TRUE("local a := []; for i := 1 to 150 do AddArraySlot(a, {k: (i * 7919) mod 20, i: i}); StableSort(a, '|<|, 'k); local ok := true; for i := 1 to 149 do if a[i - 1].k > a[i].k or (a[i - 1].k = a[i].k and a[i - 1].i > a[i].i) then ok := nil; ok");

	// errors
	EXPECT_THROWS("Sort(42, '|<|, nil)", kNSErrNotAnArray);
	EXPECT_THROWS("Sort([1], 'bogus, nil)", kNSErrBadArgs);
	EXPECT_THROWS("Sort([1, 2], '|=|, nil)", kNSErrBadArgs);
	EXPECT_THROWS("Sort([1, 2], func(a, b) nil, nil)", kNSErrNotAnInteger);
	EXPECT_THROWS("Sort([\"a\", 1], '|str<|, nil)", kNSErrNotAString);
}


static void
TestSearching()
{
	// linear
	EXPECT_INT("LSearch([1, 2, 3, 2], 2, 0, '|=|, nil)", 1);
	EXPECT_INT("LSearch([1, 2, 3, 2], 2, 2, '|=|, nil)", 3);
	EXPECT_NIL("LSearch([1, 2, 3], 4, 0, '|=|, nil)");
	EXPECT_NIL("LSearch([1, 2, 3], 1, 3, '|=|, nil)");
	EXPECT_INT("LSearch([\"a\", \"B\", \"c\"], \"b\", 0, '|str=|, nil)", 1);
	EXPECT_INT("LSearch([{n: 1}, {n: 2}], 2, 0, '|=|, 'n)", 1);
	EXPECT_INT("LSearch([1, 2, 3], 2, 0, func(a, b) a = b, nil)", 1);
	EXPECT_INT("LSearch([1, 2, 3], 2, 0, func(a, b) a - b, nil)", 1);
	EXPECT_INT("LSearch([1, 2, 3], 2, 0, '|<|, nil)", 1);
	EXPECT_PRINTS("LFetch([{n: 1}, {n: 2}], 2, 0, '|=|, 'n)", "{n: 2}");
	EXPECT_NIL("LFetch([{n: 1}, {n: 2}], 3, 0, '|=|, 'n)");
	EXPECT_THROWS("LSearch([1, 2], 1, -1, '|=|, nil)", kNSErrOutOfBounds);

	// binary
	EXPECT_INT("BSearchLeft([1, 2, 2, 3], 2, '|<|, nil)", 1);
	EXPECT_INT("BSearchRight([1, 2, 2, 3], 2, '|<|, nil)", 2);
	EXPECT_INT("BSearchLeft([1, 2, 2, 3], 5, '|<|, nil)", 4);
	EXPECT_INT("BSearchRight([1, 2, 2, 3], 0, '|<|, nil)", -1);
	EXPECT_INT("BSearchLeft([], 5, '|<|, nil)", 0);
	EXPECT_INT("BFind([1, 2, 2, 3], 2, '|<|, nil)", 1);
	EXPECT_INT("BFindRight([1, 2, 2, 3], 2, '|<|, nil)", 2);
	EXPECT_NIL("BFind([1, 2, 2, 3], 4, '|<|, nil)");
	EXPECT_NIL("BFindRight([1, 2, 2, 3], 0, '|<|, nil)");
	EXPECT_PRINTS("BFetch([{n: 1}, {n: 2}], 2, '|<|, 'n)", "{n: 2}");
	EXPECT_PRINTS("BFetchRight([{n: 1, i: 0}, {n: 1, i: 1}], 1, '|<|, 'n)", "{n: 1, i: 1}");
	EXPECT_NIL("BFetch([{n: 1}, {n: 2}], 3, '|<|, 'n)");
	EXPECT_INT("BFind([\"a\", \"b\", \"c\"], \"B\", '|str<|, nil)", 1);
	// insertion
	EXPECT_PRINTS("local a := [1, 3]; BInsert(a, 2, '|<|, nil, nil); a", "[1, 2, 3]");
	EXPECT_INT("local a := [1, 3]; BInsert(a, 2, '|<|, nil, nil)", 1);
	EXPECT_INT("local a := [1, 3]; BInsert(a, 4, '|<|, nil, nil)", 2);
	EXPECT_INT("local a := [1, 3]; BInsert(a, 0, '|<|, nil, nil)", 0);
	EXPECT_PRINTS("local a := [{n: 1, i: 0}, {n: 1, i: 1}]; BInsert(a, {n: 1, i: 2}, '|<|, 'n, nil); a", "[{n: 1, i: 2}, {n: 1, i: 0}, {n: 1, i: 1}]");
	EXPECT_PRINTS("local a := [{n: 1, i: 0}, {n: 1, i: 1}]; BInsertRight(a, {n: 1, i: 2}, '|<|, 'n, nil); a", "[{n: 1, i: 0}, {n: 1, i: 1}, {n: 1, i: 2}]");
	EXPECT_NIL("local a := [1, 2, 3]; BInsert(a, 2, '|<|, nil, true)");
	EXPECT_PRINTS("local a := [1, 2, 3]; BInsert(a, 2, '|<|, nil, true); a", "[1, 2, 3]");
	EXPECT_INT("local a := [1, 2, 3]; BInsert(a, 2, '|<|, nil, 'returnElt)", 2);
	EXPECT_PRINTS("local a := [{n: 1}]; BInsert(a, {n: 1, new: true}, '|<|, 'n, 'returnElt)", "{n: 1}");
	EXPECT_PRINTS("local a := [{n: 1}]; BInsert(a, {n: 2}, '|<|, 'n, 'returnElt)", "{n: 2}");
	EXPECT_NIL("local a := [1, 2, 3]; BInsertRight(a, 2, '|<|, nil, true)");
	EXPECT_INT("local a := [1, 2, 3]; BInsertRight(a, 4, '|<|, nil, true)", 3);
	// deletion
	EXPECT_INT("local a := [1, 2, 2, 2, 3]; BDelete(a, 2, '|<|, nil, nil)", 3);
	EXPECT_PRINTS("local a := [1, 2, 2, 2, 3]; BDelete(a, 2, '|<|, nil, nil); a", "[1, 3]");
	EXPECT_PRINTS("local a := [1, 2, 2, 2, 3]; BDelete(a, 2, '|<|, nil, 2); a", "[1, 2, 3]");
	EXPECT_INT("local a := [1, 2, 3]; BDelete(a, 5, '|<|, nil, nil)", 0);
	EXPECT_INT("local a := [1, 2, 3]; BDelete(a, 3, '|<|, nil, 10)", 1);
}


static void
TestSets()
{
	// ordered
	EXPECT_PRINTS("BMerge([1, 3, 5], [2, 3, 4], '|<|, nil, nil)", "[1, 2, 3, 3, 4, 5]");
	EXPECT_PRINTS("BMerge([1, 3, 5], [2, 3, 4], '|<|, nil, true)", "[1, 2, 3, 4, 5]");
	EXPECT_PRINTS("BMerge([1, 1, 2], [2, 2], '|<|, nil, true)", "[1, 1, 2, 2]");	// uniqueOnly drops only pairs across the arrays
	EXPECT_PRINTS("BMerge([], [1, 2], '|<|, nil, nil)", "[1, 2]");
	EXPECT_PRINTS("BMerge([1, 2], [], '|<|, nil, nil)", "[1, 2]");
	EXPECT_PRINTS("BMerge([\"a\", \"c\"], [\"B\"], '|str<|, nil, nil)", "[\"a\", \"B\", \"c\"]");
	EXPECT_PRINTS("BIntersect([1, 2, 3, 4], [2, 4, 6], '|<|, nil, true)", "[2, 4]");
	EXPECT_PRINTS("BIntersect([1, 2, 3, 4], [2, 4, 6], '|<|, nil, nil)", "[2, 2, 4, 4]");
	EXPECT_PRINTS("BIntersect([1, 3], [2, 4], '|<|, nil, true)", "[]");
	EXPECT_PRINTS("BDifference([1, 2, 3, 4], [2, 4, 6], '|<|, nil)", "[1, 3]");
	EXPECT_PRINTS("BDifference([1, 2, 3, 4], [], '|<|, nil)", "[1, 2, 3, 4]");
	EXPECT_PRINTS("BDifference([1, 1, 2], [1], '|<|, nil)", "[2]");
	EXPECT_PRINTS("BMerge([{n: 1}, {n: 3}], [{n: 2}], '|<|, 'n, nil)", "[{n: 1}, {n: 2}, {n: 3}]");
	EXPECT_THROWS("BMerge(1, [], '|<|, nil, nil)", kNSErrUnexpectedImmediate);
	EXPECT_THROWS("BMerge(\"x\", [], '|<|, nil, nil)", kNSErrNotAnArray);

	// unordered
	EXPECT_INT("SetOverlaps([1, 2, 3], [5, 3])", 2);
	EXPECT_NIL("SetOverlaps([1, 2, 3], [5, 6])");
	EXPECT_PRINTS("SetUnion([1, 2], [2, 3], nil)", "[1, 2, 2, 3]");
	EXPECT_PRINTS("SetUnion([1, 2], [2, 3], true)", "[1, 2, 3]");
	EXPECT_PRINTS("SetUnion(nil, [2, 3], true)", "[2, 3]");
	EXPECT_PRINTS("SetUnion([1], nil, true)", "[1]");
	EXPECT_PRINTS("SetUnion(nil, nil, true)", "[]");
	EXPECT_PRINTS("SetDifference([1, 2, 3, 2], [2])", "[1, 3, 2]");
	EXPECT_PRINTS("SetDifference([1, 2, 3], nil)", "[1, 2, 3]");
	EXPECT_NIL("SetDifference(nil, [1])");
	EXPECT_THROWS("SetUnion(1, [], nil)", kNSErrNotAnArray);
}


static void
TestBinaries()
{
	// a binary of 8 bytes: 01 02 03 04 ff fe 00 41
	const char* make = "local b := MakeBinary(8, 'data); StuffByte(b, 0, 1); StuffByte(b, 1, 2); StuffByte(b, 2, 3); StuffByte(b, 3, 4); StuffByte(b, 4, 255); StuffByte(b, 5, 254); StuffByte(b, 6, 0); StuffByte(b, 7, 65); ";
	char source[512];
#define BIN(expr) (snprintf(source, sizeof(source), "%s%s", make, expr), source)
	EXPECT_INT(BIN("ExtractByte(b, 4)"), 255);
	EXPECT_INT(BIN("ExtractWord(b, 0)"), 0x0102);
	EXPECT_INT(BIN("ExtractWord(b, 4)"), -2);
	EXPECT_INT(BIN("ExtractWord(b, 1)"), 0x0203);
	EXPECT_INT(BIN("ExtractLong(b, 0)"), 0x01020304);
	EXPECT_INT(BIN("ExtractLong(b, 1)"), 0x020304ff);
	EXPECT_INT(BIN("ExtractXLong(b, 4)"), (long) (0xfffe0041UL >> 3));
	EXPECT_CHAR(BIN("ExtractChar(b, 7)"), 'A');
	EXPECT_CHAR(BIN("ExtractUniChar(b, 6)"), 'A');
	EXPECT_STRING(BIN("ExtractCString(b, 6)"), "");
	EXPECT_STRING(BIN("StuffByte(b, 7, 0); ExtractCString(b, 4)"), "\xff\xfe");
	EXPECT_TRUE(BIN("BinEqual(ExtractBytes(b, 2, 2, 'x), ExtractBytes(b, 2, nil, 'y)) = nil"));
	EXPECT_TRUE(BIN("BinEqual(ExtractBytes(b, 2, 2, 'x), ExtractBytes(b, 2, 2, 'y))"));
	EXPECT_INT(BIN("Length(ExtractBytes(b, 6, nil, 'x))"), 2);
	EXPECT_TRUE(BIN("ClassOf(ExtractBytes(b, 6, nil, 'x)) = 'x"));
	EXPECT_INT(BIN("StuffWord(b, 0, -3); ExtractWord(b, 0)"), -3);
	EXPECT_INT(BIN("StuffLong(b, 4, -100000); ExtractLong(b, 4)"), -100000);
	EXPECT_INT(BIN("StuffUniChar(b, 0, $Z); ExtractByte(b, 1)"), 'Z');
	EXPECT_CHAR(BIN("StuffChar(b, 0, $Q); ExtractChar(b, 0)"), 'Q');
	EXPECT_STRING(BIN("StuffCString(b, 0, \"hi\"); ExtractCString(b, 0)"), "hi");
	EXPECT_STRING(BIN("StuffPString(b, 0, \"hey\"); ExtractPString(b, 0)"), "hey");
	EXPECT_INT(BIN("StuffPString(b, 0, \"hey\"); ExtractByte(b, 0)"), 3);
	// a string's bytes are big-endian UniChars as on a MessagePad, whatever
	// the host keeps (frames/BinaryBytes.h)
	EXPECT_INT("ExtractByte(\"AB\", 1)", 'A');
	EXPECT_INT("ExtractWord(\"AB\", 2)", 'B');
	EXPECT_CHAR("ExtractUniChar(\"AB\", 2)", 'B');
	EXPECT_STRING("local s := \"xx\"; StuffUniChar(s, 0, $Q); s", "Qx");
	EXPECT_STRING("local s := \"xx\"; StuffByte(s, 3, 90); s", "xZ");
	EXPECT_STRING("ExtractBytes(\"hello\", 2, 6, 'string)", "ell");
	// a package's name BinaryMunger'd out of a binary into a string (Newt's
	// Cape's addFile): the binary's big-endian UniChars read as characters
	EXPECT_STRING(BIN("StuffUniChar(b, 0, $H); StuffUniChar(b, 2, $i); BinaryMunger(Clone(\"\"), 0, 0, b, 0, 4)"), "Hi");
	EXPECT_INT(BIN("BinaryMunger(b, 0, 2, \"Q\", 0, 2); ExtractWord(b, 0)"), 'Q');
	EXPECT_STRING("BinaryMunger(Clone(\"ab\"), 2, 0, \"cd\", 0, 4)", "acdb");
	// SetClass between a string and a binary keeps the bytes a script sees:
	// a binary of big-endian UniChars becomes the string they spell, and a
	// string made a binary has its UniChars high byte first
	EXPECT_STRING(BIN("StuffUniChar(b, 0, $O); StuffUniChar(b, 2, $K); StuffWord(b, 4, 0); SetLength(b, 6); SetClass(b, 'string)"), "OK");
	EXPECT_INT("local s := SetClass(Clone(\"AB\"), 'binary); ExtractByte(s, 0) * 256 + ExtractByte(s, 1)", 'A');
	EXPECT_STRING("SetClass(SetClass(Clone(\"xyz\"), 'binary), 'string)", "xyz");
	// a text shape's data is text too: made 'textData by MakeText and turned
	// back into a 'string by a ROM script (the Extras drawer's labels)
	EXPECT_STRING("SetClass(SetClass(Clone(\"Calls\"), 'textData), 'string)", "Calls");
	EXPECT_STRING("SetClass(SetClass(Clone(\"Dock\"), 'TextBox), 'string)", "Dock");
	EXPECT(HostOrderOfClassName("textData") == kHostUniChars && HostOrderOfClassName("TextBox") == kHostUniChars);
	// a string class by inheritance (the ROM's initialInheritanceFrame)
	SetFrameSlot(RefVar(gInheritanceFrame), RefVar(Intern("phone")), RefVar(RSSYMstring));
	EXPECT(HostOrderOfClassName("phone") == kHostUniChars && HostOrderOfClassName("string.foo") == kHostUniChars
		&& HostOrderOfClassName("Real") == kHostReal && HostOrderOfClassName("boundsRect") == kHostHalfwords
		&& HostOrderOfClassName("bits") == kROMOrder);
	EXPECT_STRING("SetClass(Clone(\"555\"), 'phone)", "555");
	EXPECT_INT(BIN("StuffUniChar(b, 0, $7); StuffWord(b, 2, 0); SetLength(b, 4); StrLen(SetClass(b, 'phone))"), 1);
	RemoveSlot(RefVar(gInheritanceFrame), RefVar(Intern("phone")));
	// 0x3fffffff is thirty ones, and a Newton integer is thirty bits, so
	// the literal is -1 before it ever reaches StuffLong - which is what
	// comes back out of the four bytes it wrote
	EXPECT_INT(BIN("StuffLong(b, 0, 0x3fffffff); ExtractLong(b, 0)"), -1);
	// errors
	EXPECT_THROWS(BIN("ExtractByte(b, 8)"), kNSErrBadArgs);
	EXPECT_THROWS(BIN("ExtractLong(b, 5)"), kNSErrBadArgs);
	EXPECT_THROWS(BIN("ExtractByte(b, -1)"), kNSErrBadArgs);
	EXPECT_THROWS("ExtractByte([1], 0)", kNSErrBadArgs);
	EXPECT_THROWS(BIN("StuffLong(b, 0, 0x3fffffff); StuffByte(b, 0, 0x7f); ExtractLong(b, 0)"), kNSErrLongOutOfRange);
	EXPECT_THROWS(BIN("StuffByte(b, 6, 9); ExtractCString(b, 0)"), kNSErrBadArgs);		// no terminator in the data
	EXPECT_THROWS(BIN("StuffCString(b, 6, \"long\")"), kNSErrBadArgs);
	EXPECT_THROWS("BinEqual(1, 2)", kNSErrNotABinaryObject);
	// a read-only object (the ROM's)
	EXPECT_THROWS("StuffByte([1], 0, 1)", kNSErrBadArgs);	// not a binary
	{
		RefVar ro(Eval("MakeBinary(4, 'x)"));
		ObjHeader* obj = OBJ(ro);
		SetObjFlags(obj, ObjFlags(obj) | kObjReadOnly);
		SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("roBinary")), ro);
		EXPECT_THROWS("StuffByte(roBinary, 0, 1)", kNSErrObjectReadOnly);
		obj = OBJ(ro);
		SetObjFlags(obj, ObjFlags(obj) & ~(ULong) kObjReadOnly);
	}
#undef BIN
}


static void
TestComparisons()
{
	// strings are ordered as the machine collates them (TRichString::
	// CompareSubStringCommon over the sort table), not by their codes:
	// 'a' (97) comes before 'B' (66), and case alone makes no order
	EXPECT_TRUE("\"a\" < \"B\"");
	EXPECT_TRUE("\"apple\" < \"Banana\"");
	EXPECT_NIL("\"a\" < \"A\"");
	EXPECT_TRUE("\"a\" <= \"A\" and \"A\" >= \"a\"");
	EXPECT_TRUE("UnorderedOrGreater(2, 1)");
	EXPECT_NIL("UnorderedOrGreater(1, 2)");
	EXPECT_TRUE("UnorderedOrGreater(2.5, 1)");
	EXPECT_TRUE("UnorderedGreaterOrEqual(1, 1)");
	EXPECT_NIL("UnorderedGreaterOrEqual(1, 1.5)");
	EXPECT_TRUE("UnorderedOrLess(1, 2)");
	EXPECT_NIL("UnorderedOrLess(2, 2)");
	EXPECT_TRUE("UnorderedLessOrEqual(2, 2)");
	EXPECT_NIL("UnorderedLessOrEqual(3, 2.0)");
	EXPECT_TRUE("UnorderedOrEqual(2, 2.0)");
	EXPECT_NIL("UnorderedOrEqual(2, 3)");
	EXPECT_NIL("Unordered(1, 2)");
	EXPECT_NIL("Unordered(1.0, 2)");
	EXPECT_TRUE("LessOrGreater(1, 2)");
	EXPECT_NIL("LessOrGreater(2, 2.0)");
	EXPECT_TRUE("LessEqualOrGreater(1, 2.0)");
	// a NaN is unordered
	{
		SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("nan")), RefVar(MakeReal(__builtin_nan(""))));
		EXPECT_TRUE("Unordered(nan, 1)");
		EXPECT_TRUE("UnorderedOrGreater(nan, 1)");
		EXPECT_TRUE("UnorderedOrEqual(1, nan)");
		EXPECT_NIL("LessOrGreater(nan, 1)");
		EXPECT_NIL("LessEqualOrGreater(nan, 1)");
		EXPECT_NIL("nan < 1");
	}
	EXPECT_THROWS("Unordered('a, 1)", kNSErrNotANumber);

	// forLoop and the sibling slots
	EXPECT_PRINTS("local a := []; forLoop(1, 3, func(i) AddArraySlot(a, i * i)); a", "[1, 4, 9]");
	EXPECT_INT("local f := {_proto: {x: 1}, y: 2}; getSiblingSlot(f, 'x)", 1);
	EXPECT_TRUE("local f := {_proto: {x: 1}, y: 2}; hasSiblingSlot(f, 'x)");
	EXPECT_NIL("local f := {_proto: {x: 1}, y: 2}; hasSiblingSlot(f, 'z)");
	EXPECT_THROWS("getSiblingSlot(nil, 'x)", kNSErrNilContext);
}


// A NewtonScript integer is thirty bits, whatever the host's word is: a
// Ref is the machine's word with two tag bits, and everything that makes
// one cuts it back to that.  Arithmetic wraps where the ARM wraps, and a
// literal too big to hold is a compile error.
static void
TestThirtyBitIntegers()
{
	// the ends of the range, and over each of them
	EXPECT_INT("536870911", 536870911);
	EXPECT_INT("-536870912", -536870912);
	EXPECT_INT("local a := 536870911; local b := 1; a + b", -536870912);
	EXPECT_INT("local a := -536870912; local b := 1; a - b", 536870911);
	EXPECT_INT("local a := 536870911; a * 2", -2);
	EXPECT_INT("1000000000 + 1000000000", -147483648);
	EXPECT_INT("local a := 400000000; local b := 400000000; a + b", -273741824);
	// a hexadecimal literal of thirty ones is -1, not a thousand million
	EXPECT_INT("0x3fffffff", -1);
	// and one that cannot be held at all does not compile
	EXPECT_THROWS("1073741824", kNSErrIntegerTooLarge);
}


static void
TestMath()
{
	EXPECT_TRUE("acosh(1.0) = 0.0");
	EXPECT_TRUE("asinh(0) = 0.0");
	EXPECT_TRUE("atanh(0) = 0.0");
	EXPECT_TRUE("expm1(0) = 0.0");
	EXPECT_TRUE("log1p(0) = 0.0");
	EXPECT_TRUE("ldexp(1.5, 3) = 12.0");
	EXPECT_TRUE("scalb(3, 2) = 12.0");
	EXPECT_TRUE("logb(8.0) = 3.0");
	EXPECT_TRUE("erf(0) = 0.0");
	EXPECT_TRUE("erfc(0) = 1.0");
	EXPECT_TRUE("gamma(5) = 24.0");
	EXPECT_TRUE("lgamma(1) = 0.0");
	EXPECT_TRUE("rint(2.5) = 2.0");
	EXPECT_TRUE("nearbyint(3.5) = 4.0");
	EXPECT_INT("rinttol(2.5)", 2);
	EXPECT_INT("rinttol(3.5)", 4);
	EXPECT_TRUE("remainder(7, 2) = -1.0");
	EXPECT_TRUE("copysign(3, -1) = -3.0");
	EXPECT_TRUE("nextafterd(1.0, 2.0) > 1.0");
	EXPECT_TRUE("isnormal(1.0)");
	EXPECT_NIL("isnormal(0.0)");
	EXPECT_TRUE("isfinite(1)");
	EXPECT_TRUE("IsFiniteNumber(1.5)");
	EXPECT_NIL("isnan(1.0)");
	EXPECT_TRUE("isnan(nan)");
	EXPECT_NIL("isfinite(nan)");
	EXPECT_INT("signbit(-2.0)", 1);
	EXPECT_INT("signbit(2.0)", 0);
	EXPECT_TRUE("fdim(5, 3) = 2.0");
	EXPECT_TRUE("fdim(3, 5) = 0.0");
	EXPECT_TRUE("compound(0.5, 2) = 2.25");
	EXPECT_TRUE("fabs(annuity(0.1, 2) - 1.7355371900826446) < 0.000000000001");
	EXPECT_PRINTS("remquo(7, 2)", "[-1.00000, 4]");
	EXPECT_PRINTS("randomx(1)", "[16807.0, 16807.0]");
	EXPECT_TRUE("local r := Random(3, 7); r >= 3 and r <= 7");
	EXPECT_INT("Random(5, 5)", 5);
	EXPECT_THROWS("Random(7, 3)", kNSErrBadArgs);
	EXPECT_TRUE("local s := GetRandomState(); local a := Random(0, 1000000); SetRandomState(s); Random(0, 1000000) = a");
	EXPECT_TRUE("ClassOf(GetRandomState()) = 'randomState");
	EXPECT_TRUE("local rounding := fegetround(); fesetround(rounding); fegetround() = rounding");
	EXPECT_INT("feclearexcept(fegetexcept(-1)); fetestexcept(-1)", 0);
	EXPECT_TRUE("local env := fegetenv(); fesetenv(env); fegetenv() = env");
	EXPECT_INT("GetFunctionArgCount(func(a, b) nil)", 2);
	EXPECT_INT("GetFunctionArgCount(func() nil)", 0);
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_OBJECTS) != noErr)
	{
		printf("test_Strings: cannot import %s\n", NEWTON_OBJECTS);
		return 1;
	}
	gObjectHeapSize = 0x100000;
	InitObjects();
	gOutput = tmpfile();
	if (gOutput == nil)
	{
		printf("test_Strings: no temporary file\n");
		return 1;
	}
	HostInitREP(gOutput);
	SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("prettyPrint")), RefVar(NILREF));
	Printed();
	newton_try
	{
		TestUnicodeTables();
		TestRichString();
		TestRichStringInk();
		TestStringFunctions();
		TestSmartStrings();
		TestSorting();
		TestSearching();
		TestSets();
		TestBinaries();
		TestComparisons();
		TestThirtyBitIntegers();
		TestMath();
	}
	newton_catch_all
	{
		failures++;
		fprintf(stderr, "FAIL: unhandled exception %s\n", _info.exception.name);
		if (Subexception((ExceptionName) _info.exception.name, (ExceptionName) "type.ref"))
		{
			RefStruct* data = (RefStruct*) _info.exception.data;
			POutTranslator* saved = gREPout;
			gREPout = saved;
			PrintObject(*data, 0);
			fprintf(stderr, "%s\n", Printed());
		}
	}
	end_try;
	if (failures == 0)
		printf("test_Strings: all tests passed\n");
	else
		printf("test_Strings: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
