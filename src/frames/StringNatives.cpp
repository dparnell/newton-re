/*
	File:		frames/StringNatives.cpp

	Contains:	The string functions of NewtonScript (the F... natives the
				built-in functions frame names StrLen, StrConcat, SubStr,
				StrPos, Upcase, TrimString, ParamStr, ...), over TRichString
				(RichString.h) and the Unicode utilities (utility/Unicode.h).

	NOT YET RECONSTRUCTED: the ink of rich strings (MakeRichString,
	DecodeRichString, StripInk, the ink word counts answer as for plain
	text), the number parser TNumberParser (StringToNumber reads with the
	C library), the international number formats (FormattedNumberStr), the
	sort tables behind the comparisons and StripDiacriticals.
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

void
RegisterStringNatives(void)
{
	RegisterNativeFunction("FStrLen__FRC6RefVarT1", (void*) FStrLen, 1);
	RegisterNativeFunction("FStrFilled", (void*) FStrFilled, 1);
	RegisterNativeFunction("FStrEqual__FRC6RefVarN21", (void*) FStrEqual, 2);
	RegisterNativeFunction("FStrExactCompare__FRC6RefVarN21", (void*) FStrExactCompare, 2);
	RegisterNativeFunction("FStrCompare", (void*) FStrCompare, 2);
	RegisterNativeFunction("FSymbolCompareLex", (void*) FSymbolCompareLex, 2);
	RegisterNativeFunction("FBeginsWith", (void*) FBeginsWith, 2);
	RegisterNativeFunction("FEndsWith", (void*) FEndsWith, 2);
	RegisterNativeFunction("FSubstr", (void*) FSubstr, 3);
	RegisterNativeFunction("FStrConcat__FRC6RefVarN21", (void*) FStrConcat, 2);
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
}
