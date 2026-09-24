/*
	File:		assist/AssistStrings.cpp

	Contains:	The Assistant's string tidying - see AssistStrings.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "AssistStrings.h"
#include "Unicode.h"
#include "RSSymbols.h"

#include <string.h>
#include <ctype.h>

// frames/StringNatives.cpp - the same ROM file's neighbours, already
// reconstructed there because the string functions a script sees use them
Ref		SplitString(RefArg rcvr, RefArg str);						// ROM 0x000833e4 SplitString__FRC6RefVarT1


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
