/*
	File:		assist/AssistStrings.cpp

	Contains:	The Assistant's string tidying - see AssistStrings.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "AssistStrings.h"
#include "Unicode.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "SortTables.h"
#include "Interpreter.h"
#include "NewtonMemory.h"
#include "RSSymbols.h"

#include <string.h>
#include <ctype.h>

// frames/StringNatives.cpp - the same ROM file's neighbours, already
// reconstructed there because the string functions a script sees use them
Ref		SplitString(RefArg rcvr, RefArg str);						// ROM 0x000833e4 SplitString__FRC6RefVarT1
ULong	StringLeftTrim(RefArg str);									// ROM 0x0008421c StringLeftTrim__FRC6RefVar
ULong	StringRightTrim(RefArg str);								// ROM 0x0008418c StringRightTrim__FRC6RefVar
Ref		FFindStringInArray(RefArg rcvr, RefArg array, RefArg str);	// ROM 0x001fe3c8 FFindStringInArray__FRC6RefVarN21


// ROM 0x00086e50 Bstrcpy__FPUcT1
// strcpy under another name.
unsigned char*
Bstrcpy(unsigned char* to, const unsigned char* from)
{
	unsigned char* out = to;
	unsigned char c;
	do
	{
		c = *from++;
		*out++ = c;
	}
	while (c != 0);
	return to;
}


// ROM 0x0007d334 DownCase__FPUc
// The C string lowercased in place, a byte at a time.
unsigned char*
DownCase(unsigned char* str)
{
	size_t length = strlen((char*) str);
	for (size_t i = 0; i < length; i++)
		str[i] = (unsigned char) tolower((int) str[i]);
	return str;
}


// ROM 0x0008337c NStringCat__FRC6RefVarN21
// `b` appended to `a` in place: `a` is grown by b's length (less one
// terminator) and b's characters are copied on to its end.  ==> `a`.
Ref
NStringCat(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	long lengthA = Length(a);
	long lengthB = Length(b);
	SetLength(a, lengthA + lengthB - 2);
	UniChar* from = GetCString(b);
	UniChar* to = GetCString(a);
	Ustrcat(to, from);
	return a;
}


// ROM 0x00083668 StringAssoc__FRC6RefVarN21
// StrAssoc(str, alist): the first element of the list - itself an array -
// whose first element is the string (StrEqual); nil for none, or when the
// list holds something that is not an array before it is reached.
Ref
StringAssoc(RefArg /*rcvr*/, RefArg str, RefArg alist)
{
	if (!IsString(str) || !IsArray(alist))
		return NILREF;
	RefVar element;
	long count = Length(alist);
	for (long i = 0; i < count; i++)
	{
		element = GetArraySlotRef(alist, i);
		if (!IsArray(element))
			break;
		if (NOTNIL(FStrEqual(RefVar(), RefVar(GetArraySlotRef(element, 0)), str)))
			return element;
	}
	return NILREF;
}


// ROM 0x00083794 StringShorten__FRC6RefVarT1
// StringShorten(str): a string that ends in a parenthesis with the
// parenthesised part - and the character before it, the space - taken
// off: "Daniel (work)" is "Daniel".  The string itself when it does not
// end in ")", nil when there is no "(" after the first character.
Ref
StringShorten(RefArg /*rcvr*/, RefArg str)
{
	UniChar close = U_CONST_CHAR(')');
	UniChar open = U_CONST_CHAR('(');
	if (ISNIL(str))
		return NILREF;
	RefVar copy(Clone(str));
	ULong chars = (ULong) Length(copy) / sizeof(UniChar);		// the nul among them
	UniChar* text = (UniChar*) BinaryData(copy);
	if (text[chars - 2] != close)
		return str;
	long found = 0;
	for (long i = (long) chars - 2; i >= 0; i--)
		if (text[i] == open)
		{
			found = i;
			break;
		}
	if (found == 0)
		return NILREF;
	text[found - 1] = 0;
	return MakeString(text);
}


// ROM 0x000838c4 StringAnnotate__FRC6RefVarN21
// StringAnnotate(str, note): "str (note)".
Ref
StringAnnotate(RefArg /*rcvr*/, RefArg str, RefArg note)
{
	RefVar parts(AllocateArray(RSSYMarray, 5));
	SetArraySlotRef(parts, 0, str);
	SetArraySlotRef(parts, 1, MakeString(" "));
	SetArraySlotRef(parts, 2, MakeString("("));
	SetArraySlotRef(parts, 3, note);
	SetArraySlotRef(parts, 4, MakeString(")"));
	return FStringer(RefVar(), parts);
}


// ROM 0x000839f0 StringDicer__FRC6RefVarUl
// SplitString with the returns, line feeds and tabs as separators too,
// and no more than `limit` words: the one that would be word `limit`
// stops it.
Ref
StringDicer(RefArg str, ULong limit)
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
		ULong slot = 0;
		ULong i = StringLeftTrim(str);
		ULong end = StringRightTrim(str);
		long length = 0;
		if (i < end)
		{
			do
			{
				char c = ascii[i];
				if (c == ' ' || c == '\r' || c == '\n' || c == '\t')
				{
					if (length != 0)
					{
						UnlockRef(piece);
						SetLength(piece, length + 1);
						LockRef(piece);
						char* word = BinaryData(piece);
						word[length] = 0;
						if (slot == limit)
							goto done;
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
				word[length] = 0;
				if (slot != limit)
				{
					SetLength(result, slot + 1);
					SetArraySlotRef(result, slot, MakeString(word));
				}
			}
		}
	}
done:
	UnlockRef(piece);
	DisposPtr(ascii);
	return result;
}


// ROM 0x00083c9c DSStringEQ__FRC6RefVarN21
// Whether the two strings are the same, case aside.
Ref
DSStringEQ(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	const UniChar* sb = GetCString(b);
	const UniChar* sa = GetCString(a);
	return CompareStringNoCase(sa, sb) == 0 ? TRUEREF : NILREF;
}


// ROM 0x00083cd8 DSPartialStrMatch__FRC6RefVar6RefVarT2
// Whether every word of `words` is one of `of` (case aside) - nil when
// there are more of the one than of the other.
Ref
DSPartialStrMatch(RefArg /*rcvr*/, RefVar words, RefVar of)
{
	RefVar word;
	RefVar candidate;
	RefVar found;
	long count = Length(words);
	long available = Length(of);
	if (available < count)
		return NILREF;
	for (long i = 0; i < count; i++)
	{
		word = GetArraySlotRef(words, i);
		found = NILREF;
		if (available < 1)
			return NILREF;
		long j;
		for (j = 0; j < available; j++)
		{
			candidate = GetArraySlotRef(of, j);
			if (NOTNIL(candidate))
			{
				found = DSStringEQ(RefVar(), word, candidate);
				if (EQRef(found, TRUEREF))
					break;
			}
		}
		if (j == available && ISNIL(found))
			return NILREF;
	}
	return TRUEREF;
}


// ROM 0x00083e98 DSHasStopString__FRC6RefVarT1
// Whether any of the words - of two to four characters - is on the stop
// list (magic pointer 270: "the", "and" and the like).
Ref
DSHasStopString(RefArg /*rcvr*/, RefArg words)
{
	RefVar word;
	RefVar stopList(MAKEMAGICPTR(270));
	ULong count = (ULong) Length(words);
	for (ULong i = 0; i < count; i++)
	{
		word = GetArraySlotRef(words, i);
		ULong chars = (ULong) Length(word) / sizeof(UniChar) - 1;
		if (chars > 1 && chars < 5 && NOTNIL(FFindStringInArray(RefVar(), stopList, word)))
			return TRUEREF;
	}
	return NILREF;
}


// ROM 0x00083fa8 DSPrevSubStr__FRC6RefVarN21
// PrevSubStr(str, index): the index just after the space before the
// character at `index` (the start of the word it is in); 0 when there is
// none.  The test looks only at the low byte of each character, so any
// character ending in 0x20 counts as a space (a ROM quirk, kept).
Ref
DSPrevSubStr(RefArg /*rcvr*/, RefArg str, RefArg index)
{
	const UniChar* text = (const UniChar*) BinaryData(str);
	Long i = RINT(index);
	for ( ; i >= 1; i--)
		if ((text[i] & 0xff) == 0x20)
			return MAKEINT(i + 1);
	return MAKEINT(0);
}


// ROM 0x00084064 GlueStrings__FRC6RefVarT1
// The array of strings joined into one, a space between each pair.
Ref
GlueStrings(RefArg /*rcvr*/, RefArg strings)
{
	RefVar result(MakeString(""));
	long count = Length(strings);
	if (count != 0)
	{
		RefVar space(MakeString(" "));
		for (long i = 0; i < count; i++)
		{
			result = NStringCat(RefVar(), result, RefVar(GetArraySlotRef(strings, i)));
			if (i < count - 1)
				result = NStringCat(RefVar(), result, space);
		}
	}
	return result;
}


// ROM 0x00084294 GenerateSubstrings__FRC6RefVarT1
// Every run of consecutive words of the string as a string of its own,
// the longest first: "call John Smith" gives "call John Smith", then
// "call John" and "John Smith", then the three single words.  That is
// (n*n + n)/2 of them for n words, which is how big the array is made.
Ref
GenerateSubstrings(RefArg /*rcvr*/, RefArg str)
{
	RefVar words(SplitString(RefVar(), str));
	long count = Length(words);
	RefVar result(AllocateArray(RSSYMarray, (count * count + count) / 2));
	RefVar piece;
	long out = 0;
	for (long end = count - 1; end >= 0; end--)
	{
		long last = end;
		for (long first = 0; last < count; first++, last++)
		{
			piece = MakeString("");
			for (long k = first; k <= last; k++)
			{
				NStringCat(RefVar(), piece, RefVar(GetArraySlotRef(words, k)));
				if (k < last)
					NStringCat(RefVar(), piece, RefVar(MakeString(" ")));
			}
			SetArraySlot(result, out, piece);
			out++;
		}
	}
	return result;
}


// ROM 0x00084518 CleanString__FRC6RefVarT1
// The returns, line feeds and tabs of the string turned into spaces, in
// place, so that everything after it can treat white space as one thing.
// The terminator is left alone.  ==> the string.
Ref
CleanString(RefArg /*rcvr*/, RefArg str)
{
	long length = Length(str) >> 1;
	UniChar* text = GetCString(str);
	if (length != 1)
	{
		for (long i = 0; i < length - 1; i++)
		{
			UniChar c = text[i];
			if (c == 0x0d || c == 0x0a || c == 0x09 || c == ' ')
				text[i] = ' ';
		}
	}
	return str;
}


// ROM 0x000845c0 TrimBlanksAndPunct__FRC6RefVarT1 +0x1c
// White space, or one of the marks a sentence may be wrapped in.
//
// BUG (the ROM's): the last two are 0xc7 and 0xc8, which are the *Mac
// Roman* codes for the guillemets « and ».  The string is Unicode by the
// time it gets here, so what is actually matched is Ç and È; the
// guillemets themselves (0x00ab, 0x00bb) go untrimmed.  Kept as it is.
static Boolean
IsBlankOrPunct(UniChar c)
{
	return c == 0x0d || c == 0x0a || c == 0x09 || c == ' '
		|| c == '!' || c == '"' || c == '\'' || c == '(' || c == ')'
		|| c == ',' || c == '-' || c == '.' || c == ':' || c == ';' || c == '?'
		|| c == 0x2018 || c == 0x2019 || c == 0x201c || c == 0x201d
		|| c == 0xc7 || c == 0xc8;
}


// ROM 0x000845c0 TrimBlanksAndPunct__FRC6RefVarT1
// The white space and punctuation taken off both ends of the string.  A
// string that is nothing but punctuation answers nil; one that has none
// at either end answers itself, so the caller has to use the answer.
Ref
TrimBlanksAndPunct(RefArg /*rcvr*/, RefArg str)
{
	UniChar* text = GetCString(str);
	long length = Ustrlen(text);
	long first = 0;
	while (first < length && IsBlankOrPunct(text[first]))
		first++;
	if (first == length)
		return NILREF;
	long last = length - 1;
	while (first < last && IsBlankOrPunct(text[last]))
		last--;
	if (first != 0 || last + 1 != length)
	{
		RefVar result(AllocateBinary(RSSYMstring, (last + 2 - first) * 2));
		((UniChar*) BinaryData(result))[0] = 0;
		StrMunger(result, 0, -1, str, first, last + 1 - first);
		return result;
	}
	return str;
}


// ROM 0x000847c8 MakeLowerCase__FRC6RefVarT1
// The string lowercased in place, through the locale's case table
// (utility/Unicode.h).  ==> the string.
Ref
MakeLowerCase(RefArg /*rcvr*/, RefArg str)
{
	LowercaseText(GetCString(str), 0x7fffffff);
	return str;
}
