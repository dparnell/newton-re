/*
	File:		utility/Unicode.cpp

	Contains:	The UniChar string functions and the conversions to and
				from 8-bit encodings (Unicode.h).

	The ROM's UniChar routines read and write their characters a byte at
	a time (UniChars in the ROM's strings are big-endian and need not be
	aligned); here a UniChar is the host's, as the object heap's strings
	are (frames/Objects.cpp MakeString).
*/

#include "Unicode.h"


// ROM 0x002544a4 Ustrcpy
UniChar*
Ustrcpy(UniChar* dest, const UniChar* src)
{
	UniChar* d = dest;
	while ((*d++ = *src++) != 0)
		;
	return dest;
}


// ROM 0x002544cc Ustrncpy
// At most n UniChars of the string, and a terminator after them when
// the string is longer (dest holds n + 1).
UniChar*
Ustrncpy(UniChar* dest, const UniChar* src, long n)
{
	UniChar* d = dest;
	for (;;)
	{
		if (n-- == 0)
		{
			*d = 0;
			break;
		}
		UniChar c = *src++;
		*d++ = c;
		if (c == 0)
			break;
	}
	return dest;
}


// ROM 0x002547ec Ustrcat
UniChar*
Ustrcat(UniChar* dest, const UniChar* src)
{
	UniChar* d = dest;
	while (*d != 0)
		d++;
	while ((*d++ = *src++) != 0)
		;
	return dest;
}


// ROM 0x00254828 Ustrncat
UniChar*
Ustrncat(UniChar* dest, const UniChar* src, long n)
{
	UniChar* d = dest;
	while (*d != 0)
		d++;
	for (;;)
	{
		if (n == 0)
		{
			*d = 0;
			return dest;
		}
		n--;
		if ((*d++ = *src++) == 0)
			return dest;
	}
}


// ROM 0x0025489c Ustrlen
long
Ustrlen(const UniChar* s)
{
	const UniChar* p = s;
	while (*p != 0)
		p++;
	return p - s;
}


// ROM 0x002548c4 Ustrchr
UniChar*
Ustrchr(const UniChar* s, UniChar c)
{
	for (;;)
	{
		if (*s == c)
			return (UniChar*) s;
		if (*s == 0)
			return nil;
		s++;
	}
}


// ROM 0x002548f4 Umbstrlen
// (the same as Ustrlen: the ROM's strings hold no multi-byte characters)
long
Umbstrlen(const UniChar* s)
{
	return Ustrlen(s);
}


// ROM 0x0025491c Umbstrnlen
// The length of s, at most n.
long
Umbstrnlen(const UniChar* s, long /*unused*/, long n)
{
	long length = 0;
	while (s[length] != 0)
	{
		if (--n == 0)
			return length + 1;
		length++;
	}
	return length;
}


// ROM 0x0025495c Ustrcmp
int
Ustrcmp(const UniChar* a, const UniChar* b)
{
	for (;;)
	{
		int diff = (int) *a - (int) *b;
		if (diff != 0)
			return diff;
		if (*a == 0)
			return 0;
		a++;
		b++;
	}
}


// ROM 0x0025498c Umemset
void
Umemset(UniChar* dest, UniChar c, long n)
{
	while (n-- > 0)
		*dest++ = c;
}


// ROM 0x002553a0 ConvertToUnicode__FPCvPUslT3
// NOT YET RECONSTRUCTED: the encoding tables (gUnicode, after InitUnicode);
// this is what the ROM does before they are installed.
void
ConvertToUnicode(const void* src, UniChar* dest, long /*encoding*/, long n)
{
	const unsigned char* s = (const unsigned char*) src;
	long i = 0;
	while (i < n && *s != 0)
	{
		*dest++ = *s++;
		i++;
	}
	*dest = 0;
}


// ROM 0x002568b8 ConvertFromUnicode__FPCUsPvlT3
// NOT YET RECONSTRUCTED: the encoding tables; this is what the ROM does
// before they are installed - characters over 0x7f become 0x1a.
void
ConvertFromUnicode(const UniChar* src, void* dest, long /*encoding*/, long n)
{
	unsigned char* d = (unsigned char*) dest;
	long i = 0;
	while (i < n && *src != 0)
	{
		UniChar c = *src++;
		*d++ = (c < 0x80) ? (unsigned char) c : 0x1a;
		i++;
	}
	*d = 0;
}


/* -------------------------------------------------------------------------------
	Characters
------------------------------------------------------------------------------- */

// ROM 0x002553d4 U_CONST_CHAR
UniChar
U_CONST_CHAR(unsigned char c)
{
	UniChar u[2];
	unsigned char a[2] = { c, 0 };
	ConvertToUnicode(a, u, kMacRomanEncoding, 1);
	return u[0];
}


// ROM 0x002553f0 A_CONST_CHAR
char
A_CONST_CHAR(UniChar c)
{
	UniChar u[2] = { c, 0 };
	char a[2];
	ConvertFromUnicode(u, a, kMacRomanEncoding, 1);
	return a[0];
}


// ROM 0x00255a54 UToLower__FUs
// NOT YET RECONSTRUCTED: LowercaseText's tables; ASCII and Latin-1 letters.
UniChar
UToLower(UniChar c)
{
	if ((c >= 'A' && c <= 'Z') || (c >= 0xc0 && c <= 0xde && c != 0xd7))
		return c + 0x20;
	return c;
}


// (host: the one-character form of UppercaseText, 0x0025587c)
UniChar
UToUpper(UniChar c)
{
	if ((c >= 'a' && c <= 'z') || (c >= 0xe0 && c <= 0xfe && c != 0xf7))
		return c - 0x20;
	return c;
}


// ROM 0x00255428 IsAlphabet__FUs
// A letter: uppercased without diacriticals it is A-Z (or the German sharp
// s, 0xdf, which has no upper case).  NOT YET RECONSTRUCTED:
// UppercaseNoDiacriticsText; Latin-1's letters are taken.
Boolean
IsAlphabet(UniChar c)
{
	UniChar u = UToUpper(c);
	if (u >= 0xc0 && u <= 0xde && u != 0xd7)
		return true;
	return (u >= 'A' && u <= 'Z') || u == 0xdf;
}


// ROM 0x00255494 IsDigit__FUs
Boolean
IsDigit(UniChar c)
{
	return c >= '0' && c <= '9';
}


// ROM 0x002554bc IsHexDigit__FUs
Boolean
IsHexDigit(UniChar c)
{
	return IsDigit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}


// ROM 0x00255514 IsAlphaNumeric__FUs
Boolean
IsAlphaNumeric(UniChar c)
{
	return IsDigit(c) || IsAlphabet(c);
}


// ROM 0x002555b4 IsSpace__FUs
Boolean
IsSpace(UniChar c)
{
	return c == ' ';
}


// ROM 0x002555d0 IsTab__FUs
Boolean
IsTab(UniChar c)
{
	return c == '\t';
}


// ROM 0x00255658 IsBreaker__FUs
Boolean
IsBreaker(UniChar c)
{
	return c == '\n' || c == '\r';
}


// ROM 0x0025555c IsWhiteSpace__FUs
Boolean
IsWhiteSpace(UniChar c)
{
	return IsSpace(c) || IsTab(c) || IsBreaker(c);
}
