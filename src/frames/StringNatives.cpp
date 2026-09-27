/*
	File:		frames/StringNatives.cpp

	Contains:	The string functions of NewtonScript (the F... natives the
				built-in functions frame names StrLen, StrConcat, SubStr,
				StrPos, Upcase, TrimString, ParamStr, ...), over TRichString
				(RichString.h) and the Unicode utilities (utility/Unicode.h).

	The rich-string natives (MakeRichString, DecodeRichString, StripInk)
	are in frames/RichString.cpp, beside the class they work on; they are
	registered here with the rest.

	NOT YET RECONSTRUCTED: the ink word counts the length functions
	answer as for plain text, the number parser TNumberParser
	(StringToNumber reads with the C library), the international number
	formats (FormattedNumberStr) and StripDiacriticals.
*/

#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "REPTranslators.h"
#include "RichString.h"
#include "Unicode.h"
#include "NumberFormat.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "NSErrors.h"
#include "NewtonExceptions.h"
#include "NewtonMemory.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int		SymbolCompareLexRef(Ref sym1, Ref sym2);		// Symbols.cpp

// a UniChar in a ref, or a type error
static UniChar
CharOf(RefArg r)
{
	Ref ref = r;
	if (!ISCHAR(ref))
		ThrowBadTypeWithFrameData(kNSErrNotACharacter, r);
	return RCHAR(ref);
}

/* -------------------------------------------------------------------------------
	Lengths, comparison
------------------------------------------------------------------------------- */

// ROM 0x001fd0a8 FStrLen__FRC6RefVarT1
// The characters of a string (its ink words counting one each).
Ref
FStrLen(RefArg /*rcvr*/, RefArg str)
{
	TRichString s(str);
	return MAKEINT(s.Length());
}


// ROM 0x001fecc4 StrEmpty__FRC6RefVar
Boolean
StrEmpty(RefArg str)
{
	if ((Ref) str == NILREF)
		return true;
	TRichString s(str);
	return s.Length() == 0;
}


// ROM 0x001fed18 FStrFilled
Ref
FStrFilled(RefArg /*rcvr*/, RefArg str)
{
	return StrEmpty(str) ? NILREF : TRUEREF;
}


// ROM 0x001fd644 FStringer__FRC6RefVarT1
// The elements of the array written one after another as a string, each
// as StringObject writes it: measured first, then written into a string
// of the size they came to.  (The Stringer a script calls is Printer.cpp's
// FFramesStringer; this is the one the C code - the Assistant - uses.)
Ref
FStringer(RefArg /*rcvr*/, RefArg array)
{
	long total = 1;
	long length;
	long count = Length(array);
	RefVar element;
	for (long i = 0; i < count; i++)
	{
		element = GetArraySlotRef(array, i);
		StringObject(element, nil, length, 0x7fffffff);
		total += length;
	}
	RefVar str(AllocateBinary(RSSYMstring, total * sizeof(UniChar)));
	if (Length(array) > 0)
	{
		UniChar* p = (UniChar*) BinaryData(str);
		for (long i = 0; i < count; i++)
		{
			element = GetArraySlotRef(array, i);
			StringObject(element, p, length, 0x7fffffff);
			p += length;
		}
	}
	return str;
}


// ROM 0x001fedf4 FStrEqual__FRC6RefVarN21
// The same characters, cases apart (the same object is equal at once;
// strings of different sizes are not).
Ref
FStrEqual(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	if (EQRef(a, b) && IsString(a))
		return TRUEREF;
	if (Length(a) != Length(b) && IsString(a) && IsString(b))
		return NILREF;
	TRichString sa(a);
	TRichString sb(b);
	return sa.CompareSubStringCommon(sb, 0, -1, false) == 0 ? TRUEREF : NILREF;
}


// ROM 0x001fc8d4 FStrExactCompare__FRC6RefVarN21
// ==> < 0, 0, > 0 as a compares to b, cases counting.
Ref
FStrExactCompare(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	if (EQRef(a, b) && IsString(a))
		return MAKEINT(0);
	TRichString sa(a);
	TRichString sb(b);
	return MAKEINT(sa.CompareSubStringCommon(sb, 0, -1, true));
}


// ROM 0x002b6d48 FStrCompare
// ==> < 0, 0, > 0 as a compares to b, cases folded.
Ref
FStrCompare(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	if (EQRef(a, b) && IsString(a))
		return MAKEINT(0);
	TRichString sa(a);
	TRichString sb(b);
	return MAKEINT(sa.CompareSubStringCommon(sb, 0, -1, false));
}


// ROM 0x00358da8 FSymbolCompareLex
Ref
FSymbolCompareLex(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	if (!IsSymbol(a))
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, a);
	if (!IsSymbol(b))
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, b);
	return MAKEINT(SymbolCompareLexRef(a, b));
}


// ROM 0x003146c8 StrBeginsWith__FRC6RefVarT1
int
StrBeginsWith(RefArg str, RefArg prefix)
{
	TRichString s(str);
	TRichString p(prefix);
	if (s.Length() < p.Length())
		return false;
	return s.CompareSubStringCommon(p, 0, p.Length(), false) == 0;
}


// ROM 0x00314764 FBeginsWith
Ref
FBeginsWith(RefArg /*rcvr*/, RefArg str, RefArg prefix)
{
	return StrBeginsWith(str, prefix) ? TRUEREF : NILREF;
}


// ROM 0x0031478c StrEndsWith__FRC6RefVarT1
int
StrEndsWith(RefArg str, RefArg suffix)
{
	TRichString s(str);
	TRichString x(suffix);
	if (s.Length() < x.Length())
		return false;
	return s.CompareSubStringCommon(x, s.Length() - x.Length(), x.Length(), false) == 0;
}


// ROM 0x00314888 FEndsWith
Ref
FEndsWith(RefArg /*rcvr*/, RefArg str, RefArg suffix)
{
	return StrEndsWith(str, suffix) ? TRUEREF : NILREF;
}


/* -------------------------------------------------------------------------------
	Pieces
------------------------------------------------------------------------------- */

// ROM 0x00314584 Substring__FRC6RefVarlT2
// count characters of str from start (-1: to the end), as a new string of
// str's class.
Ref
Substring(RefArg str, long start, long count)
{
	if (count == -1)
	{
		TRichString s(str);
		count = s.Length();
	}
	else if (!IsString(str))
		ThrowBadTypeWithFrameData(kNSErrNotAString, str);
	RefVar result(AllocateBinary(RefVar(ClassOf(str)), sizeof(UniChar)));
	*(UniChar*) BinaryData(result) = 0;
	StrMunger(result, 0, -1, str, start, count);
	return result;
}


// ROM 0x00314660 FSubstr
Ref
FSubstr(RefArg /*rcvr*/, RefArg str, RefArg start, RefArg count)
{
	return Substring(str, RINT(start), (Ref) count == NILREF ? -1 : RINT(count));
}


// ROM 0x001fc970 FStrConcat__FRC6RefVarN21
// A new string: a with b appended.
Ref
FStrConcat(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	RefVar result(Clone(a));
	TRichString s(result);
	TRichString t(b);
	s.InsertRange(t, 0, t.Length(), s.Length());
	return result;
}


/* -------------------------------------------------------------------------------
	Building a string a piece at a time

	SmartStart, SmartConcat and SmartStop are how a script builds a long
	string without making a new object for every piece: SmartStart(size)
	hands back a string object of that many bytes with nothing in it,
	SmartConcat(str, count, item) writes the next piece at the character
	`count` and answers the count the next call should use, and
	SmartStop(str, count) cuts the object back to what was written.

	Ink is what makes them "smart".  Once a rich string goes in, the
	buffer has to become a rich string too - and a rich string keeps its
	own length, so from then on the count the caller passes is ignored and
	the string's length is the answer.
------------------------------------------------------------------------------- */

// The frame SmartConcat throws for a character that cannot go into a
// string: {errorCode, value}, under the interpreter's exception rather
// than the frames one (ThrowExFramesWithBadValue, Objects.cpp, is the same
// frame under the other name).  The ROM builds it inline in each branch.
static void
ThrowExInterpreterWithBadValue(NewtonErr errorCode, RefArg value)
{
	RefVar frame(AllocateFrame());
	SetFrameSlot(frame, RSSYMerrorcode, RefVar(MAKEINT(errorCode)));
	SetFrameSlot(frame, RSSYMvalue, value);
	ThrowRefException(exInterpreterWithFrameData, frame);
}


// A character that may be written into a string: neither a NUL, which
// would end it, nor the ink character, which stands for a word of ink and
// may only be put there by the ink itself.
static UniChar
SmartChar(RefArg item)
{
	UniChar ch = (UniChar) RCHAR((Ref) item);
	if (ch == 0 || ch == kInkChar)
		ThrowExInterpreterWithBadValue(kNSErrNotACharacter, item);
	return ch;
}


// ROM 0x001fd0d8 FSmartStart__FRC6RefVarT1
// An empty string of `size` bytes to build in.  Only the first character
// is cleared - the rest of the object is whatever AllocateBinary left
// there, which is why SmartStop has to cut the object back afterwards.
// (A size of less than two writes past the end of the object; the ROM
// does not check, and nor does this.)
Ref
FSmartStart(RefArg /*rcvr*/, RefArg size)
{
	RefVar str(AllocateBinary(RefVar(RSSYMstring), RINT(size)));
	*(UniChar*) BinaryData(str) = 0;
	return str;
}


// ROM 0x001fd13c FSmartConcat__FRC6RefVarN31
// `item` - a string or a single character - written into the buffer at
// character `count`, and the count for the next piece.  The buffer grows
// by 0x80 bytes, or by the item's length when that is more, whenever the
// piece would not fit.
Ref
FSmartConcat(RefArg /*rcvr*/, RefArg str, RefArg count, RefArg item)
{
	if (IsRichString(str))
	{
		// the buffer already carries ink, so the piece goes on the end
		// through the rich string and `count` is not looked at
		TRichString dst(str);
		if (ISCHAR((Ref) item))
		{
			UniChar text[2];
			text[0] = SmartChar(item);
			text[1] = 0;
			TRichString src(text);
			dst.InsertRange(src, 0, src.Length(), dst.Length());
		}
		else
		{
			TRichString src(item);
			dst.InsertRange(src, 0, src.Length(), dst.Length());
		}
		return MAKEINT(dst.Length());
	}

	if (ISCHAR((Ref) item))
	{
		UniChar ch = SmartChar(item);			// checked before the room is made
		long size = Length(str);
		long at = RINT(count);
		long length = at * 2 + 2;
		if ((ULong) (size - 2) < (ULong) length)
			SetLength(str, size + 0x80);
		// (the ROM stores the character's two bytes itself, high one
		//  first, which is a UniChar written the ARM's way round)
		GetCString(str)[at] = ch;
		return MAKEINT(length >> 1);
	}

	if (IsRichString(item))
	{
		// ink into a plain buffer: the buffer becomes a rich string, so it
		// is first cut back to the text written so far - the room left to
		// build in goes with it
		SetLength(str, Ustrlen((UniChar*) BinaryData(str)) * 2 + 2);
		TRichString dst(str);
		TRichString src(item);
		dst.InsertRange(src, 0, src.Length(), dst.Length());
		return MAKEINT(dst.Length());
	}

	long size = Length(str);
	long itemLength = Length(item) - 2;			// its text, without the terminator
	long at = RINT(count) * 2;
	long length = at + itemLength;
	if ((ULong) (size - 2) < (ULong) length)
	{
		long grow = itemLength + 2;
		if (grow < 0x80)
			grow = 0x80;
		SetLength(str, grow + size);
	}
	UniChar* from = GetCString(item);
	UniChar* to = GetCString(str);
	Ustrcpy(to + (at >> 1), from);
	return MAKEINT(length >> 1);
}


// ROM 0x001fd5f0 FSmartStop__FRC6RefVarN21
// The object cut back to the `count` characters written into it and the
// NUL after them.  A buffer that has become a rich string is left alone:
// it is already exactly as long as its text and its ink.
Ref
FSmartStop(RefArg /*rcvr*/, RefArg str, RefArg count)
{
	if (!IsRichString(str))
		SetLength(str, RINT(count) * 2 + 2);
	return NILREF;
}


// ROM 0x00314c1c TrimString__FRC6RefVar
// The white space at both ends removed, in place.
void
TrimString(RefArg str)
{
	TRichString s(str);
	long length = s.Length();
	long leading = 0;
	while (leading < length && IsWhiteSpace(s.GetChar(leading)))
		leading++;
	long trailing = 0;
	while (trailing < length - leading && IsWhiteSpace(s.GetChar(length - 1 - trailing)))
		trailing++;
	if (trailing > 0)
		s.DeleteRange(length - trailing, trailing);
	if (leading > 0)
		s.DeleteRange(0, leading);
}


// ROM 0x00314d58 FTrimString
Ref
FTrimString(RefArg /*rcvr*/, RefArg str)
{
	TrimString(str);
	return str;
}


// ROM 0x00314d7c CharacterPosition__FRC6RefVarUsl
// The index of c in str at or after start, -1 for none.
long
CharacterPosition(RefArg str, UniChar c, long start)
{
	TRichString s(str);
	for (; start < s.Length(); start++)
		if (s.GetChar(start) == c)
			return start;
	return -1;
}


// ROM 0x00314de8 FCharPos
Ref
FCharPos(RefArg /*rcvr*/, RefArg str, RefArg c, RefArg start)
{
	long position = CharacterPosition(str, CharOf(c), RINT(start));
	return position == -1 ? NILREF : MAKEINT(position);
}


// ROM 0x00314e70 StrPosition__FRC6RefVarT1l
// The index of substr in str at or after start (cases folded), -1 for none.
long
StrPosition(RefArg str, RefArg substr, long start)
{
	TRichString s(str);
	TRichString sub(substr);
	if (start < 0)
		start = 0;
	else if (start > s.Length() - sub.Length())
		return -1;
	for (; start + sub.Length() <= s.Length(); start++)
		if (s.CompareSubStringCommon(sub, start, sub.Length(), false) == 0)
			return start;
	return -1;
}


// ROM 0x00314f2c FStrPos
Ref
FStrPos(RefArg /*rcvr*/, RefArg str, RefArg substr, RefArg start)
{
	long position = StrPosition(str, substr, RINT(start));
	return position == -1 ? NILREF : MAKEINT(position);
}


// ROM 0x00314f78 StrReplace__FRC6RefVarN21l
// Up to count (-1: all) occurrences of substr in str replaced by
// replacement, in place; ==> how many were.
long
StrReplace(RefArg str, RefArg substr, RefArg replacement, long count)
{
	if (!IsString(str))
		ThrowBadTypeWithFrameData(kNSErrNotAString, str);
	if (!IsString(substr))
		ThrowBadTypeWithFrameData(kNSErrNotAString, substr);
	if (!IsString(replacement))
		ThrowBadTypeWithFrameData(kNSErrNotAString, replacement);
	if (count == -1)
		count = Length(str) / sizeof(UniChar);
	long strLength = Length(str) / sizeof(UniChar) - 1;
	long subLength = Length(substr) / sizeof(UniChar) - 1;
	if (strLength < subLength)
		return 0;
	long growth = Length(replacement) / sizeof(UniChar) - 1 - subLength;
	long position = 0;
	long replaced = 0;
	while (replaced < count && (position = StrPosition(str, substr, position)) != -1)
	{
		StrMunger(str, position, subLength, replacement, 0, -1);
		position += subLength + growth;
		strLength += growth;
		replaced++;
		if (strLength < position)
			break;
	}
	return replaced;
}


// ROM 0x003150e0 FStrReplace
Ref
FStrReplace(RefArg /*rcvr*/, RefArg str, RefArg substr, RefArg replacement, RefArg count)
{
	return MAKEINT(StrReplace(str, substr, replacement, (Ref) count == NILREF ? -1 : RINT(count)));
}


// ROM 0x001fe6c4 FGetChar
// The character at index (an ink word's ink: NOT YET, the ink character).
Ref
FGetChar(RefArg /*rcvr*/, RefArg str, RefArg index)
{
	TRichString s(str);
	return MAKECHAR(s.GetChar(RINT(index)));
}


// ROM 0x001fe750 FSetChar
// The character at index replaced (by an ink word: NOT YET).
Ref
FSetChar(RefArg /*rcvr*/, RefArg str, RefArg index, RefArg c)
{
	TRichString s(str);
	s.SetChar(RINT(index), CharOf(c));
	return str;
}


/* -------------------------------------------------------------------------------
	Case
------------------------------------------------------------------------------- */

// ROM 0x003148b0 StrUpcase__FRC6RefVar
void
StrUpcase(RefArg str)
{
	if (!IsString(str))
		ThrowBadTypeWithFrameData(kNSErrNotAString, str);
	LockRef(str);
	UniChar* text = (UniChar*) BinaryData(str);
	UppercaseText(text, Ustrlen(text));
	UnlockRef(str);
}


// ROM 0x00314914 FUpcase
// A string upper-cased in place, or a character upper-cased.
Ref
FUpcase(RefArg /*rcvr*/, RefArg obj)
{
	if (ISCHAR((Ref) obj))
		return MAKECHAR(UToUpper(RCHAR((Ref) obj)));
	StrUpcase(obj);
	return obj;
}


// ROM 0x003149b0 StrDowncase__FRC6RefVar
void
StrDowncase(RefArg str)
{
	if (!IsString(str))
		ThrowBadTypeWithFrameData(kNSErrNotAString, str);
	LockRef(str);
	UniChar* text = (UniChar*) BinaryData(str);
	LowercaseText(text, Ustrlen(text));
	UnlockRef(str);
}


// ROM 0x00314a14 FDowncase
Ref
FDowncase(RefArg /*rcvr*/, RefArg obj)
{
	if (ISCHAR((Ref) obj))
		return MAKECHAR(UToLower(RCHAR((Ref) obj)));
	StrDowncase(obj);
	return obj;
}


// ROM 0x00314ab0 StrCapitalize__FRC6RefVar
// The first character upper-cased.
void
StrCapitalize(RefArg str)
{
	if (!IsString(str))
		ThrowBadTypeWithFrameData(kNSErrNotAString, str);
	TRichString s(str);
	if (s.Length() != 0)
	{
		LockRef(str);
		UppercaseText((UniChar*) BinaryData(str), 1);
		UnlockRef(str);
	}
}


// ROM 0x00314b2c FCapitalize
Ref
FCapitalize(RefArg /*rcvr*/, RefArg str)
{
	StrCapitalize(str);
	return str;
}


// ROM 0x00314b50 StrCapitalizeWords__FRC6RefVar
// The first character of each word upper-cased.
void
StrCapitalizeWords(RefArg str)
{
	if (!IsString(str))
		ThrowBadTypeWithFrameData(kNSErrNotAString, str);
	LockRef(str);
	UniChar* text = (UniChar*) BinaryData(str);
	Boolean inWord = false;
	for (; *text != 0; text++)
	{
		Boolean delimiter = IsDelimiter(*text);
		if (inWord)
		{
			if (delimiter)
				inWord = false;
		}
		else if (!delimiter)
		{
			UppercaseText(text, 1);
			inWord = true;
		}
	}
	UnlockRef(str);
}


// ROM 0x00314bf8 FCapitalizeWords
Ref
FCapitalizeWords(RefArg /*rcvr*/, RefArg str)
{
	StrCapitalizeWords(str);
	return str;
}


/* -------------------------------------------------------------------------------
	Characters and numbers
------------------------------------------------------------------------------- */

// ROM 0x001fed3c FIsAlphaNumeric
Ref
FIsAlphaNumeric(RefArg /*rcvr*/, RefArg c)
{
	return IsAlphaNumeric(CharOf(c)) ? TRUEREF : NILREF;
}


// ROM 0x001fed98 FIsWhiteSpace
Ref
FIsWhiteSpace(RefArg /*rcvr*/, RefArg c)
{
	return IsWhiteSpace(CharOf(c)) ? TRUEREF : NILREF;
}


// ROM 0x001fe914 FIsInkChar
Ref
FIsInkChar(RefArg /*rcvr*/, RefArg c)
{
	return CharOf(c) == kInkChar ? TRUEREF : NILREF;
}


// ROM 0x001fe594 FIsRichString__FRC6RefVarT1
Ref
FIsRichString(RefArg /*rcvr*/, RefArg str)
{
	return IsRichString(str) ? TRUEREF : NILREF;
}


// ROM 0x001feb38 FIsValidString
Ref
FIsValidString(RefArg /*rcvr*/, RefArg str)
{
	if (!IsString(str))
		return NILREF;
	TRichString s(str);
	return s.Verify() == 0 ? TRUEREF : NILREF;
}


// ROM 0x001feb90 FStripDiacriticals
// NOT YET RECONSTRUCTED: StripDiacriticalsText's table; the string is
// answered as it is.
Ref
FStripDiacriticals(RefArg /*rcvr*/, RefArg str)
{
	if (!IsString(str))
		ThrowBadTypeWithFrameData(kNSErrNotAString, str);
	return str;
}


// ROM 0x001fe66c FNumberStr__FRC6RefVarT1
// A number's text (nil for anything else).
Ref
FNumberStr(RefArg /*rcvr*/, RefArg number)
{
	Ref ref = number;
	if (!ISINT(ref) && !ISREAL(ref))
		return NILREF;
	UniChar text[32];
	long length;
	StringObject(number, text, length, 31);
	return MakeString(text);
}


// ROM 0x001fec10 FStringToNumber__FRC6RefVarT1
// The number a string spells, as a real; nil for none.  NOT YET
// RECONSTRUCTED: TNumberParser (the locale's separators); strtod.
Ref
FStringToNumber(RefArg /*rcvr*/, RefArg str)
{
	long length = Length(str) / sizeof(UniChar) - 1;
	if (length <= 0)
		return NILREF;
	char* text = new char[length + 1];
	ConvertFromUnicode(GetCString(str), text, kMacRomanEncoding, length);
	char* end;
	double value = strtod(text, &end);
	Boolean parsed = end != text;
	delete[] text;
	if (!parsed || !isfinite(value))
		return NILREF;
	return MakeReal(value);
}


// ROM 0x001fc6b4 FIsFiniteNumber__FRC6RefVarT1
// An integer, or a real that is finite.
static Ref
FIsFiniteNumber(RefArg /*rcvr*/, RefArg number)
{
	Ref ref = number;
	if (ISINT(ref))
		return TRUEREF;
	if (!ISREAL(ref) || !isfinite(CDouble(number)))
		return NILREF;
	return TRUEREF;
}


// Host: the ROM's error strings (this ROM's German ones) when its objects
// are not imported.
static Ref
ErrorString(Ref& romString, const char* text)
{
	if (romString == NILREF)
	{
		AddGCRoot(romString);
		romString = MakeString(text);
	}
	return Clone(RefVar(romString));
}


// ROM 0x001fc724 FFormattedNumberStr__FRC6RefVarN21
// A number formatted in the locale: by a printf format string (NumberString)
// or a format spec integer (IntegerStringSpec/NumberStringSpec: the
// kFormat... bits); the error strings for a number that is not finite,
// too large or too small, nil for a string that does not fit.
Ref
FFormattedNumberStr(RefArg /*rcvr*/, RefArg number, RefArg format)
{
	if (FIsFiniteNumber(RefVar(NILREF), number) == NILREF)
		return ErrorString(Rerrnotanumber, "Keine Zahl");
	UniChar text[64];
	long result;
	if (IsString(format))
	{
		char fmt[16];
		ConvertFromUnicode(GetCString(format), fmt, kMacRomanEncoding, 15);
		result = NumberString(CoerceToDouble(number), text, 63, fmt);
	}
	else if (ISINT((Ref) number))
		result = IntegerStringSpec(RINT(number), text, 63, RINT(format));
	else
		result = NumberStringSpec(CoerceToDouble(number), text, 63, RINT(format));
	if (result == 0)
		return MakeString(text);
	if (result == kNumberTooLarge)
		return ErrorString(Rerrnumbertoolarge, "Zahl zu gro\xdf");
	if (result == kNumberTooSmall)
		return ErrorString(Rerrnumbertoosmall, "Zahl zu klein");
	return NILREF;
}


/* -------------------------------------------------------------------------------
	Searching
------------------------------------------------------------------------------- */

// ROM 0x001fe3c8 FFindStringInArray__FRC6RefVarN21
// The index of the string in an array of strings (cases counting), nil
// for none.
Ref
FFindStringInArray(RefArg /*rcvr*/, RefArg array, RefArg str)
{
	if (!IsString(str) || !IsArray(array))
		return NILREF;
	long count = Length(array);
	TRichString s(str);
	RefVar element;
	for (long i = 0; i < count; i++)
	{
		element = GetArraySlotRef(array, i);
		TRichString e(element);
		if (s.CompareSubStringCommon(e, 0, -1, true) == 0)
			return MAKEINT(i);
	}
	return NILREF;
}


static Boolean	gOneResultOnly;		// 0x0c101d14  FindStringInFrame's 'first: one match per string


// The positions of str in text at the start of words (anywhere, when str
// begins with a delimiter), cases folded; ==> whether any; the matches
// are reported through report until it says to stop.
template <typename Report>
static Boolean
FindWordsInString(const TRichString& text, const TRichString& str, Report report)
{
	Boolean found = false;
	long strLength = str.Length();
	long last = text.Length() - strLength;
	Boolean anywhere = IsDelimiter(str.GetChar(0));
	Boolean atWordStart = true;
	for (long pos = 0; pos <= last; pos++)
	{
		Boolean delimiter = IsDelimiter(text.GetChar(pos));
		if (delimiter)
		{
			atWordStart = true;
			if (!anywhere)
				continue;
		}
		else if (!atWordStart)
			continue;
		atWordStart = false;
		if (text.CompareSubStringCommon(str, pos, strLength, false) == 0)
		{
			found = true;
			if (!report(pos))
				break;
			pos += strLength - 1;
			if (gOneResultOnly)
				break;
		}
	}
	return found;
}


// ROM 0x001fdd58 RecurseFindStringInFrame__FRC6RefVarN21RC11TRichStringl
// ==> whether str is in a string of obj (a string itself at depth 0, or
// a frame or array, its slotted values searched to a depth of ten);
// with a results array, every match adds [string, path (its slot's tag,
// or the pathExpr to it), position].
static Ref
RecurseFindStringInFrame(RefArg obj, RefArg path, RefArg results, const TRichString& str, long depth)
{
	RefVar found;
	Boolean haveResults = (Ref) results != NILREF;
	if (depth == 0 && IsString(obj))
	{
		TRichString text(obj);
		if (FindWordsInString(text, str, [&](long pos) -> Boolean {
				found = TRUEREF;
				if (!haveResults)
					return false;
				AddArraySlot(results, obj);
				AddArraySlot(results, RefVar(NILREF));
				AddArraySlot(results, RefVar(MAKEINT(pos)));
				return true;
			}))
			found = TRUEREF;
		return found;
	}
	TObjectIterator iter(obj);
	RefVar value, sub;
	for (; !iter.Done(); iter.Next())
	{
		value = iter.fValue;
		if (IsString(value))
		{
			TRichString text(value);
			if (FindWordsInString(text, str, [&](long pos) -> Boolean {
					found = TRUEREF;
					if (!haveResults)
						return false;
					sub = iter.fTag;
					if (depth > 0)
					{
						sub = Clone(path);
						SetArraySlotRef(sub, depth, iter.fTag);
						SetLength(sub, depth + 1);
					}
					AddArraySlot(results, value);
					AddArraySlot(results, sub);
					AddArraySlot(results, RefVar(MAKEINT(pos)));
					return true;
				}))
				found = TRUEREF;
		}
		else if (ISPTR((Ref) value) && (ObjectFlags(value) & kObjSlotted) && depth + 1 < 10)
		{
			if (haveResults)
				SetArraySlotRef(path, depth, iter.fTag);
			sub = RecurseFindStringInFrame(value, path, results, str, depth + 1);
			if ((Ref) sub != NILREF && !haveResults)
				found = TRUEREF;
		}
	}
	return found;
}


// ROM 0x001fe1bc FFindStringInFrame__FRC6RefVarN31
// Every string of strings looked for in the frame's strings (nested to
// ten levels, matching at the start of words, cases folded): with
// results nil ==> true when all are found; else an array of
// [string, path, position] for each match (nil when a string is not
// found), only the first match of each for 'first.
Ref
FFindStringInFrame(RefArg /*rcvr*/, RefArg frame, RefArg strings, RefArg results)
{
	gOneResultOnly = EQRef(results, RSSYMfirst);
	RefVar path;
	RefVar found;
	if ((Ref) results == NILREF)
		found = TRUEREF;
	else
	{
		path = AllocateArray(RSSYMarray, 10);
		SetClass(path, RSSYMpathexpr);
		found = AllocateArray(RSSYMarray, 0);
	}
	TObjectIterator iter(strings);
	RefVar r;
	for (; !iter.Done(); iter.Next())
	{
		TRichString str(iter.fValue);
		if ((Ref) results == NILREF)
		{
			r = RecurseFindStringInFrame(frame, path, RefVar(NILREF), str, 0);
			if ((Ref) r == NILREF)
				found = NILREF;
		}
		else
		{
			long before = Length(found);
			RecurseFindStringInFrame(frame, path, found, str, 0);
			if (Length(found) == before)
				found = NILREF;
		}
	}
	return found;
}


/* -------------------------------------------------------------------------------
	ParamStr
------------------------------------------------------------------------------- */

// ROM 0x001fd77c ParamStrParse__FP11TRichStringlUcT3RC6RefVar
// One pass over str from start: ^0-^9 replaced by the parameter's text
// (SPrintObject for a non-string), ^?N<text>|<text>| by the first text
// when parameter N is neither nil nor empty, else the second; on the
// last pass (stripping) ^^ and ^| lose their ^ and the remaining
// substitutions are dropped.  skipping: the text is only scanned.  Stops
// at a | or the end; ==> the position stopped at.
static ULong
ParamStrParse(TRichString* str, long start, Boolean skipping, Boolean stripping, RefArg params)
{
	RefVar param;
	ULong pos = start;
	while (pos < (ULong) str->Length())
	{
		UniChar c = str->GetChar(pos);
		if (c != '^')
		{
			if (c == '|')
				break;
			pos++;
			continue;
		}
		ULong next = pos + 1;
		if (next >= (ULong) str->Length())
		{
			pos = next;
			continue;
		}
		UniChar d = str->GetChar(next);
		if (d >= '0' && d <= '9')
		{
			if (skipping)
			{
				pos += 2;
				continue;
			}
			if (stripping)
			{
				str->DeleteRange(pos, 2);
				continue;
			}
			long index = d - '0';
			param = GetArraySlotRef(params, index);
			if (!IsString(param))
				param = SPrintObject(param);
			TRichString p(param);
			long length = p.Length();
			str->MungeRange(pos, 2, &p, 0, length);
			pos += length;
			continue;
		}
		if (d == '?')
		{
			UniChar which = str->GetChar(pos + 2);
			str->DeleteRange(pos, 3);
			if (which >= '0' && which <= '9')
			{
				param = GetArraySlotRef(params, which - '0');
				Boolean present = !((Ref) param == NILREF || (IsString(param) && StrEmpty(param)));
				ULong first = ParamStrParse(str, pos, !present, stripping, params);
				ULong second = first < (ULong) str->Length() ? first + 1 : first;
				ULong end = ParamStrParse(str, second, present, stripping, params);
				if (present)
				{
					// the first text stays: the second (from first's |) goes
					ULong count = end - first;
					if (end < (ULong) str->Length())
						count++;
					str->DeleteRange(first, count);
					pos = first;
				}
				else
				{
					// the second stays: the first (to and including its |) goes, and the second's |
					ULong count = first - pos;
					if (first < (ULong) str->Length())
						count++;
					str->DeleteRange(pos, count);
					pos = end - second + pos;
					if (pos < (ULong) str->Length())
						str->DeleteRange(pos, 1);
				}
			}
			else
			{
				ULong first = ParamStrParse(str, pos, true, stripping, params);
				pos = ParamStrParse(str, first + 1, true, stripping, params);
			}
			continue;
		}
		if ((d == '^' || d == '|') && stripping)
		{
			str->DeleteRange(pos, 1);
			pos++;
			continue;
		}
		pos += 2;
	}
	return pos;
}


// ROM 0x001fdca8 FParamStr__FRC6RefVarN21
// The template's ^N parameters substituted (three passes, so that a
// parameter's own ^N are substituted too), then its ^^ and ^| escapes
// stripped; ==> a new string.
Ref
FParamStr(RefArg /*rcvr*/, RefArg templateStr, RefArg params)
{
	RefVar result(Clone(templateStr));
	TRichString str(result);
	for (int pass = 0; pass < 4; pass++)
	{
		ULong pos = 0;
		while (pos < (ULong) str.Length())
		{
			pos = ParamStrParse(&str, pos, false, pass == 3, params);
			if (pos != (ULong) str.Length())
				pos++;
		}
	}
	return result;
}


/* -------------------------------------------------------------------------------
	Registration
------------------------------------------------------------------------------- */

// ROM 0x001fc9f4 FSubstituteChars
// SubstituteChars(str, oldChars, newChars): the string with each of the
// characters of `oldChars` replaced by the character in the same place in
// `newChars` - what turns the assistant's typed text into the form a soup
// entry keeps.  The string itself is never written on: the first
// substitution clones it and the clone is what is changed and answered, so
// a string with nothing to substitute comes back as the very object that
// went in.  A rich string keeps its ink, the replacement going in through
// TRichString::MungeRange; kInkChar is never substituted, because it
// stands for an ink word rather than being a character of its own.
//
// `newChars` is walked in step with `oldChars` and starts again from its
// beginning when it is the shorter of the two.  ROM BUG: the wrap looks at
// the character after the one it just used, so with an empty `newChars`
// the first look is already one past the end of the string; the ROM reads
// it too, and the reconstruction reads the same word rather than guarding
// a case the ROM does not.
static Ref
FSubstituteChars(RefArg /*rcvr*/, RefArg str, RefArg oldChars, RefArg newChars)
{
	TRichString result;
	TRichString replacement(newChars);
	RefVar clone;				// nil until a character really is replaced
	for (long i = 0; ; i++)
	{
		// re-read each turn: a Clone or a MungeRange may have moved the
		// blocks these point into
		UniChar ch = ((const UniChar*) BinaryData(str))[i];
		if (ch == 0)
			break;
		const UniChar* from = (const UniChar*) BinaryData(oldChars);
		const UniChar* to = (const UniChar*) BinaryData(newChars);
		long k = 0;
		for (long j = 0; from[j] != 0; j++)
		{
			if (from[j] == ch && ch != kInkChar)
			{
				if (ISNIL(clone))
				{
					clone = Clone(str);
					result.SetStringData(clone);
				}
				result.MungeRange(i, 1, &replacement, k, 1);
				break;
			}
			k++;
			if (to[k] == 0)
				k = 0;
		}
	}
	return NOTNIL(clone) ? (Ref) clone : (Ref) str;
}


// ROM 0x001fcc48 FStringFilter
// StringFilter(str, chars, mode): the string with some of its characters
// taken out, by which of the six modes is asked for.  `chars` is a string
// of the characters to look for; a character is "in" when it appears in
// it.  The modes come in pairs, one looking at the characters in the set
// and one at the characters outside it:
//
//   'passAll          only the characters in the set are kept
//   'rejectAll        only the characters outside it are kept
//   'passOne          a run of characters in the set is cut down to its
//                     first
//   'rejectOne        a run of characters outside it is cut down to its
//                     first
//   'passBeginning    the leading characters that are not in the set are
//                     dropped, and from the first one that is, the rest
//                     of the string stands
//   'rejectBeginning  the leading characters that are in the set are
//                     dropped - trimming leading spaces, say - and the
//                     rest stands
//
// A rich string keeps its ink: the two beginning modes delete the range
// they dropped from a clone of the original (so the ink moves with the
// text), and 'passOne and 'rejectAll copy the ink block over and write
// the new character count into the trailing word.  'passAll and
// 'rejectOne do not, so a rich string filtered by those loses its ink -
// the ROM's own omission, kept here.
static Ref
FStringFilter(RefArg /*rcvr*/, RefArg str, RefArg chars, RefArg mode)
{
	Boolean isRich = IsRichString(str);
	long size = Length(str);
	Length(chars);				// (the ROM asks and throws away the answer)
	RefVar result(AllocateBinary(RSSYMstring, size));
	const UniChar* srcBase = (const UniChar*) BinaryData(str);
	const UniChar* src = srcBase;
	UniChar* dst = (UniChar*) BinaryData(result);
	UniChar* out = dst;
	const UniChar* filter = (const UniChar*) BinaryData(chars);
	Boolean stopped = false;
	long taken = 0;				// how many characters of the source were looked at
	long run = 0;
	for (;;)
	{
		UniChar ch = *src++;
		*out = ch;
		UniChar* next = out + 1;
		if (ch == 0)
			break;
		long nextRun = run;
		if (!stopped)
		{
			Boolean inSet = false;
			for (long i = 0; filter[i] != 0; i++)
			{
				if (filter[i] == ch)
				{
					inSet = true;
					break;
				}
			}
			if (inSet)
			{
				if (EQRef(mode, RSSYMrejectall) || EQRef(mode, RSSYMrejectbeginning))
					next = out;
				if (EQRef(mode, RSSYMpassone))
				{
					nextRun = run + 1;
					if (run > 0)
						next--;
				}
				if (EQRef(mode, RSSYMpassbeginning))
				{
					if (isRich)
						break;
					stopped = true;
				}
				if (EQRef(mode, RSSYMrejectone))
					nextRun = 0;
			}
			else
			{
				if (EQRef(mode, RSSYMpassall) || EQRef(mode, RSSYMpassbeginning))
					next = out;
				if (EQRef(mode, RSSYMrejectone))
				{
					nextRun = run + 1;
					if (run > 0)
						next--;
				}
				if (EQRef(mode, RSSYMrejectbeginning))
				{
					if (isRich)
						break;
					stopped = true;
				}
				if (EQRef(mode, RSSYMpassone))
					nextRun = 0;
			}
		}
		taken++;
		out = next;
		run = nextRun;
	}
	long length = Ustrlen(dst);
	long used = (length + 1) * sizeof(UniChar);
	if (isRich)
	{
		if (EQRef(mode, RSSYMrejectbeginning) || EQRef(mode, RSSYMpassbeginning))
		{
			// the text and its ink kept together: what was dropped is taken
			// out of a copy of the original instead
			result = Clone(str);
			TRichString rich(result);
			rich.DeleteRange(0, taken);
			return result;
		}
		if (EQRef(mode, RSSYMpassone) || EQRef(mode, RSSYMrejectall))
		{
			long srcText = ((Ustrlen(srcBase) + 1) * (long) sizeof(UniChar) + 3) & ~3;
			long inkSize = Length(str) - srcText;
			memmove((char*) dst + ((used + 3) & ~3), (const char*) srcBase + srcText, inkSize);
			used = ((used + 3) & ~3) + inkSize;
			*(ULong*) ((char*) dst + used - 4) = (ULong) ((length << 4) | 1);
		}
	}
	SetLength(result, used);
	return result;
}

// ROM 0x0007d78c NewASCIIString__FRC6RefVar
// The string's text as single bytes (Mac Roman), in a pointer the caller
// disposes of; nil when there was no room for it.
char*
NewASCIIString(RefArg str)
{
	long size = Length(str);
	char* text = (char*) NewPtr(size / 2);
	if (text != nil)
		ConvertFromUnicode(GetCString(str), text, kMacRomanEncoding, 0x7fffffff);
	return text;
}


// ROM 0x0008421c StringLeftTrim__FRC6RefVar
// The index of the first character that is not a space.  It stops at the
// last character of the object - the terminating nul - so a string of
// nothing but spaces answers that index rather than running off the end.
ULong
StringLeftTrim(RefArg str)
{
	ULong count = (ULong) Length(str) / sizeof(UniChar);	// the characters, the nul among them
	const UniChar* text = (const UniChar*) BinaryData(str);
	ULong i = 0;
	if (count != 1)
	{
		do
		{
			if (text[i] != ' ')
				break;
			i++;
		}
		while (i < count - 1);
	}
	return i;
}


// ROM 0x0008418c StringRightTrim__FRC6RefVar
// Meant to be the index just past the last character that is not a space
// - but it starts one past the terminating nul, so its first step lands
// on the nul, which is not a space, and it stops there every time.  It
// therefore always answers the string's length and trims nothing: a ROM
// bug, kept.  SplitString, its only caller, does not notice, because the
// trailing spaces it hands back are separators there anyway.
ULong
StringRightTrim(RefArg str)
{
	ULong i = (ULong) Length(str) / sizeof(UniChar);
	const UniChar* text = (const UniChar*) BinaryData(str);
	do
		i--;
	while (text[i] == ' ');
	return i;
}


// ROM 0x000833e4 SplitString__FRC6RefVarT1
// SplitString(str): the string's words - the runs of characters between
// spaces - as an array of strings.  The assistant splits a typed name
// with it.  The array is made one slot long to start with and grown as
// words are found, so a string with no words in it answers [nil] rather
// than an empty array.
//
// The word being gathered is kept as single bytes in a 'string binary
// that is locked while it is written to and grown a byte at a time; each
// word becomes a string of its own through MakeString.
Ref
SplitString(RefArg /*rcvr*/, RefArg str)
{
	RefVar result;
	RefVar piece;
	long size = Length(str);
	char* ascii = NewASCIIString(str);
	result = AllocateArray(RSSYMarray, 1);
	piece = AllocateBinary(RSSYMstring, 1);
	LockRef(piece);
	if (size != 0)
	{
		long slot = 0;
		ULong i = StringLeftTrim(str);
		ULong end = StringRightTrim(str);
		long length = 0;
		if (i < end)
		{
			do
			{
				if (ascii[i] == ' ')
				{
					if (length != 0)
					{
						UnlockRef(piece);
						SetLength(piece, length + 1);
						LockRef(piece);
						char* word = BinaryData(piece);
						word[length] = '\0';
						SetLength(result, slot + 1);
						SetArraySlotRef(result, slot, MakeString(word));
						length = 0;
						UnlockRef(piece);
						piece = AllocateBinary(RSSYMstring, 1);
						LockRef(piece);
						slot++;
					}
				}
				else
				{
					UnlockRef(piece);
					SetLength(piece, length + 1);
					LockRef(piece);
					char* word = BinaryData(piece);
					word[length] = ascii[i];
					length++;
				}
				i++;
			}
			while (i < end);
			if (length != 0)
			{
				UnlockRef(piece);
				SetLength(piece, length + 1);
				LockRef(piece);
				char* word = BinaryData(piece);
				word[length] = '\0';
				SetLength(result, slot + 1);
				SetArraySlotRef(result, slot, MakeString(word));
			}
		}
	}
	UnlockRef(piece);
	DisposPtr(ascii);
	return result;
}

// ROM 0x00129484 FStrHexDump__FRC6RefVarN21
// StrHexDump(binary, groupSize): the bytes of a binary as hexadecimal
// digits, with a space after every `groupSize` of them (0 for no
// spaces).  Two digits a byte, capital letters.
static Ref
FStrHexDump(RefArg /*rcvr*/, RefArg binary, RefArg groupSize)
{
	long group = ISNIL(groupSize) ? 0 : RINT(groupSize);
	long count = Length(binary);
	long size = count * 4 + 2;
	if (group != 0)
		size += (count / group) * 2;
	RefVar result(AllocateBinary(RSSYMstring, size));
	const UByte* bytes = (const UByte*) BinaryData(binary);
	UniChar* out = (UniChar*) BinaryData(result);
	for (long i = 0; i < count; i++)
	{
		UByte byte = bytes[i];
		UByte high = (UByte) (byte >> 4);
		*out++ = (UniChar) (high < 10 ? high + '0' : high + 'A' - 10);
		UByte low = (UByte) (byte & 0xf);
		*out++ = (UniChar) (low < 10 ? low + '0' : low + 'A' - 10);
		if (group != 0 && (i + 1) % group == 0)
			*out++ = ' ';
	}
	*out = 0;
	return result;
}


// ROM 0x001fe5b8 FStringFormat
// StringFormat(string): which of the three forms a string object is in -
// plain text, or a rich string with ink in it (frames/RichString.h's
// trailer word).
static Ref
FStringFormat(RefArg /*rcvr*/, RefArg str)
{
	return MAKEINT(GetStringFormat(str));
}


void
RegisterStringNatives(void)
{
	RegisterNativeFunction("FStringFormat", (void*) FStringFormat, 1);
	RegisterNativeFunction("FStrHexDump__FRC6RefVarN21", (void*) FStrHexDump, 2);
	RegisterNativeFunction("FStrLen__FRC6RefVarT1", (void*) FStrLen, 1);
	RegisterNativeFunction("FStrFilled", (void*) FStrFilled, 1);
	RegisterNativeFunction("FStrEqual__FRC6RefVarN21", (void*) FStrEqual, 2);
	RegisterNativeFunction("FMakeRichString__FRC6RefVarN21", (void*) FMakeRichString, 2);
	RegisterNativeFunction("FGetRichString__FRC6RefVar", (void*) FGetRichString, 0);
	RegisterNativeFunction("FDecodeRichString__FRC6RefVarN21", (void*) FDecodeRichString, 2);
	RegisterNativeFunction("FStripInk", (void*) FStripInk, 2);
	RegisterNativeFunction("FStrExactCompare__FRC6RefVarN21", (void*) FStrExactCompare, 2);
	RegisterNativeFunction("FStrCompare", (void*) FStrCompare, 2);
	RegisterNativeFunction("FSymbolCompareLex", (void*) FSymbolCompareLex, 2);
	RegisterNativeFunction("FBeginsWith", (void*) FBeginsWith, 2);
	RegisterNativeFunction("FEndsWith", (void*) FEndsWith, 2);
	RegisterNativeFunction("FSubstr", (void*) FSubstr, 3);
	RegisterNativeFunction("FStrConcat__FRC6RefVarN21", (void*) FStrConcat, 2);
	RegisterNativeFunction("FSmartStart__FRC6RefVarT1", (void*) FSmartStart, 1);
	RegisterNativeFunction("FSmartConcat__FRC6RefVarN31", (void*) FSmartConcat, 3);
	RegisterNativeFunction("FSmartStop__FRC6RefVarN21", (void*) FSmartStop, 2);
	RegisterNativeFunction("FTrimString", (void*) FTrimString, 1);
	RegisterNativeFunction("FCharPos", (void*) FCharPos, 3);
	RegisterNativeFunction("FStrPos", (void*) FStrPos, 3);
	RegisterNativeFunction("FStrReplace", (void*) FStrReplace, 4);
	RegisterNativeFunction("FGetChar", (void*) FGetChar, 2);
	RegisterNativeFunction("FSetChar", (void*) FSetChar, 3);
	RegisterNativeFunction("FUpcase", (void*) FUpcase, 1);
	RegisterNativeFunction("FDowncase", (void*) FDowncase, 1);
	RegisterNativeFunction("FCapitalize", (void*) FCapitalize, 1);
	RegisterNativeFunction("FCapitalizeWords", (void*) FCapitalizeWords, 1);
	RegisterNativeFunction("FIsAlphaNumeric", (void*) FIsAlphaNumeric, 1);
	RegisterNativeFunction("FIsWhiteSpace", (void*) FIsWhiteSpace, 1);
	RegisterNativeFunction("FIsInkChar", (void*) FIsInkChar, 1);
	RegisterNativeFunction("FIsRichString__FRC6RefVarT1", (void*) FIsRichString, 1);
	RegisterNativeFunction("FIsValidString", (void*) FIsValidString, 1);
	RegisterNativeFunction("FStripDiacriticals", (void*) FStripDiacriticals, 1);
	RegisterNativeFunction("FNumberStr__FRC6RefVarT1", (void*) FNumberStr, 1);
	RegisterNativeFunction("FStringToNumber__FRC6RefVarT1", (void*) FStringToNumber, 1);
	RegisterNativeFunction("FFormattedNumberStr__FRC6RefVarN21", (void*) FFormattedNumberStr, 2);
	RegisterNativeFunction("FFindStringInArray__FRC6RefVarN21", (void*) FFindStringInArray, 2);
	RegisterNativeFunction("FFindStringInFrame__FRC6RefVarN31", (void*) FFindStringInFrame, 3);
	RegisterNativeFunction("FParamStr__FRC6RefVarN21", (void*) FParamStr, 2);
	RegisterNativeFunction("FStringFilter", (void*) FStringFilter, 3);
	RegisterNativeFunction("FSubstituteChars", (void*) FSubstituteChars, 3);
	RegisterNativeFunction("SplitString__FRC6RefVarT1", (void*) SplitString, 1);
}
