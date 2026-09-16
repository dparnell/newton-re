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
