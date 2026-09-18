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


// ROM 0x002563f0 Ustrcpy
UniChar*
Ustrcpy(UniChar* dest, const UniChar* src)
{
	UniChar* d = dest;
	while ((*d++ = *src++) != 0)
		;
	return dest;
}


// ROM 0x00256418 Ustrncpy
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


// ROM 0x00256738 Ustrcat
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


// ROM 0x00256774 Ustrncat
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


// ROM 0x002567e8 Ustrlen
long
Ustrlen(const UniChar* s)
{
	const UniChar* p = s;
	while (*p != 0)
		p++;
	return p - s;
}


// ROM 0x00256810 Ustrchr
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


// ROM 0x00256840 Umbstrlen
// (the same as Ustrlen: the ROM's strings hold no multi-byte characters)
long
Umbstrlen(const UniChar* s)
{
	return Ustrlen(s);
}


// ROM 0x00256868 Umbstrnlen
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


// ROM 0x002568a8 Ustrcmp
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


// ROM 0x002568d8 Umemset
void
Umemset(UniChar* dest, UniChar c, long n)
{
	while (n-- > 0)
		*dest++ = c;
}


CharEncoding	gUnicode[kNumberOfEncodings];		// ROM 0x0c107790 gUnicode
static long		gEncodingCount = 0;					// ROM 0x0c1048a8
Boolean			gUnicodeInited = false;				// ROM 0x0c104ee0 gUnicodeInited
Boolean			gHasUnicode = false;				// ROM 0x0c104edc gHasUnicode
const UniChar*	gASCIItoUnicodeTable = nil;			// ROM 0x0c104ee4 gASCIItoUnicodeTable
const unsigned char*	gASCIIBreakTable = nil;		// ROM 0x0c104ee8 gASCIIBreakTable
const unsigned char*	gCharClass = nil;			// ROM 0x0c1048ac
const unsigned char*	gTypeList = nil;			// ROM 0x0c1048b0
const signed char*		gUpperList = nil;			// ROM 0x0c1048b4
const signed char*		gLowerList = nil;			// ROM 0x0c1048b8
const signed char*		gUpperNoMarkList = nil;		// ROM 0x0c1048bc
const signed char*		gNoMarkList = nil;			// ROM 0x0c1048c0


// a big-endian halfword of a mapping binary
static inline ULong
MapHalf(const unsigned char* p)
{
	return (p[0] << 8) | p[1];
}


// ROM 0x00257f4c GetMappingInfo__FPvP12TEncodingMapPPFv_l
// The map filled from the mapping binary's header (kind, size, flags,
// segment count) and its tables located: kind 0's UniChars follow the
// header; kind 4's ends, starts and offsets (segment count halfwords
// each) come first, then the bytes.  ==> the converter for the kind.
void
GetMappingInfo(const void* mapping, TEncodingMap* map, void** converter)
{
	const unsigned char* p = (const unsigned char*) mapping;
	map->fKind = (UShort) MapHalf(p);
	map->fSize = MapHalf(p + 2);
	map->fFlags = MapHalf(p + 4);
	ULong segments = MapHalf(p + 6);
	map->fSegments = segments;
	const unsigned char* data = p + 8;
	if (map->fKind == 0)
	{
		map->fTable = data;
		*converter = (void*) ConvertToUnicodeFunc_Contiguous8;
	}
	else if (map->fKind == 4)
	{
		map->fEnds = (const UniChar*) data;
		data += segments * 2;
		map->fStarts = (const UniChar*) data;
		data += segments * 2;
		map->fOffsets = (const short*) data;
		map->fTable = data + segments * 2;
		*converter = (void*) ConvertFromUnicodeFunc_Segmented16;
	}
	else
		*converter = (void*) -1;
}


// ROM 0x00257524 InstallCharEncoding__FUsPcT2PFPCUsPvT2l_vPFPCvPUsPvl_v
// An encoding's maps and converters put in the table (ids 0-4; both
// converters needed).
void
InstallCharEncoding(UShort encoding, void* fromMap, void* toMap, ConvertFromUnicodeProcPtr fromUnicode, ConvertToUnicodeProcPtr toUnicode)
{
	if (encoding > 4)
		return;
	if (fromUnicode == nil || toUnicode == nil)
		return;
	gUnicode[encoding].fFromMap = fromMap;
	gUnicode[encoding].fFromUnicode = fromUnicode;
	gUnicode[encoding].fToMap = toMap;
	gUnicode[encoding].fToUnicode = toUnicode;
	gEncodingCount++;
}


// host: the character tables InitUnicode (0x00254b80) stores in the globals
void
InstallCharTables(const unsigned char* charClass, const unsigned char* typeList, const signed char* upperList, const signed char* lowerList, const signed char* upperNoMarkList, const signed char* noMarkList, const unsigned char* breakTable)
{
	gCharClass = charClass;
	gTypeList = typeList;
	gUpperList = upperList;
	gLowerList = lowerList;
	gUpperNoMarkList = upperNoMarkList;
	gNoMarkList = noMarkList;
	gASCIIBreakTable = breakTable;
	gASCIItoUnicodeTable = (const UniChar*) ((TEncodingMap*) gUnicode[kMacRomanEncoding].fToMap)->fTable;
	gUnicodeInited = true;
	gHasUnicode = true;
}


// the UniChars of a mapping binary are big-endian
static inline UniChar
MapChar(const UniChar* table, long i)
{
	const unsigned char* p = (const unsigned char*) (table + i);
	return (UniChar) ((p[0] << 8) | p[1]);
}


// ROM 0x00258480 ConvertToUnicodeFunc_Contiguous8__FPCvPUsPvl
// Each byte looked up in the map's 256 UniChars, to a 0 or n.
void
ConvertToUnicodeFunc_Contiguous8(const void* src, UniChar* dest, void* map, long n)
{
	const UniChar* table = (const UniChar*) ((TEncodingMap*) map)->fTable;
	const unsigned char* s = (const unsigned char*) src;
	long i = 0;
	while (i < n && *s != 0)
	{
		*dest++ = MapChar(table, *s++);
		i++;
	}
	*dest = 0;
}


// ROM 0x002586bc ConvertFromUnicodeFunc_Segmented16__FPCUsPUcPvl
// Each character's segment is the first whose end is not below it; a
// character below the segment's start has no byte (0x1a), else the byte
// is the table's at the character plus the segment's offset.
void
ConvertFromUnicodeFunc_Segmented16(const UniChar* src, void* dest, void* map, long n)
{
	TEncodingMap* m = (TEncodingMap*) map;
	const unsigned char* table = (const unsigned char*) m->fTable;
	unsigned char* d = (unsigned char*) dest;
	long i = 0;
	while (i < n && *src != 0)
	{
		ULong c = *src++;
		long seg = 0;
		while (MapChar(m->fEnds, seg) < c)
			seg++;
		unsigned char b;
		if (c < MapChar(m->fStarts, seg))
			b = 0x1a;
		else
			b = table[(UShort) ((short) MapChar((const UniChar*) m->fOffsets, seg) + c)];
		*d++ = b;
		i++;
	}
	*d = 0;
}


// ROM 0x002572ec ConvertToUnicode__FPCvPUslT3
// Through the encoding's converter once the tables are in (nothing for
// an encoding without one); before that bytes widened as they are, to a
// 0 or n.
void
ConvertToUnicode(const void* src, UniChar* dest, long encoding, long n)
{
	if (gUnicodeInited)
	{
		if (gUnicode[encoding].fToUnicode != nil)
			gUnicode[encoding].fToUnicode(src, dest, gUnicode[encoding].fToMap, n);
		return;
	}
	const unsigned char* s = (const unsigned char*) src;
	long i = 0;
	while (i < n && *s != 0)
	{
		*dest++ = *s++;
		i++;
	}
	*dest = 0;
}


// ROM 0x002587f0 ConvertFromUnicode__FPCUsPvlT3
// Through the encoding's converter once the tables are in; before that
// characters over 0x7f become 0x1a.
void
ConvertFromUnicode(const UniChar* src, void* dest, long encoding, long n)
{
	if (gUnicodeInited)
	{
		if (gUnicode[encoding].fFromUnicode != nil)
			gUnicode[encoding].fFromUnicode(src, dest, gUnicode[encoding].fFromMap, n);
		return;
	}
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

// ROM 0x00257320 U_CONST_CHAR
UniChar
U_CONST_CHAR(unsigned char c)
{
	UniChar u[2];
	unsigned char a[2] = { c, 0 };
	ConvertToUnicode(a, u, kMacRomanEncoding, 1);
	return u[0];
}


// ROM 0x0025733c A_CONST_CHAR
char
A_CONST_CHAR(UniChar c)
{
	UniChar u[2] = { c, 0 };
	char a[2];
	ConvertFromUnicode(u, a, kMacRomanEncoding, 1);
	return a[0];
}


// ROM 0x00257724 ConvertTextCase__FPUslPSc
// Each character (to a 0 or n) taken to Mac Roman (as it is below 0x80),
// its class's delta added (none for 0x1a: no Mac Roman character), and
// the result taken back to Unicode when it is over 0x7f.
void
ConvertTextCase(UniChar* text, long n, const signed char* deltas)
{
	for (long i = 0; i < n; i++, text++)
	{
		ULong c = *text;
		if (c == 0)
			return;
		unsigned char a = c < 0x80 ? (unsigned char) c : (unsigned char) A_CONST_CHAR((UniChar) c);
		if (a == 0x1a)
			continue;
		a = (unsigned char) (a + deltas[gCharClass[a]]);
		*text = a > 0x7f ? U_CONST_CHAR(a) : (UniChar) a;
	}
}


// ROM 0x002577b4 UppercaseText__FPUsl
// (host: Latin-1's letters before InitUnicode)
void
UppercaseText(UniChar* text, long n)
{
	if (gUnicodeInited)
		ConvertTextCase(text, n, gUpperList);
	else
		for (long i = 0; i < n && text[i] != 0; i++)
			text[i] = UToUpper(text[i]);
}


// ROM 0x002577c4 LowercaseText__FPUsl
void
LowercaseText(UniChar* text, long n)
{
	if (gUnicodeInited)
		ConvertTextCase(text, n, gLowerList);
	else
		for (long i = 0; i < n && text[i] != 0; i++)
			text[i] = UToLower(text[i]);
}


// ROM 0x002577d4 NoDiacriticsText__FPUsl
// (host: nothing before InitUnicode)
void
NoDiacriticsText(UniChar* text, long n)
{
	if (gUnicodeInited)
		ConvertTextCase(text, n, gNoMarkList);
}


// ROM 0x0025792c UppercaseNoDiacriticsText__FPUsl
// (host: UppercaseText before InitUnicode)
void
UppercaseNoDiacriticsText(UniChar* text, long n)
{
	if (gUnicodeInited)
		ConvertTextCase(text, n, gUpperNoMarkList);
	else
		UppercaseText(text, n);
}


// ROM 0x0025793c ToggleCase__FUs
// Lowercased when that changes it, else uppercased.
UniChar
ToggleCase(UniChar c)
{
	UniChar u = c;
	LowercaseText(&u, 1);
	if (u == c)
		UppercaseText(&u, 1);
	return u;
}


// ROM 0x0025798c UToLower__FUs
// LowercaseText's one character.  Host: before InitUnicode (no tables)
// ASCII and Latin-1 letters.
UniChar
UToLower(UniChar c)
{
	if (gUnicodeInited)
	{
		UniChar u = c;
		LowercaseText(&u, 1);
		return u;
	}
	if ((c >= 'A' && c <= 'Z') || (c >= 0xc0 && c <= 0xde && c != 0xd7))
		return c + 0x20;
	return c;
}


// (host: the one-character form of UppercaseText, 0x0025587c)
UniChar
UToUpper(UniChar c)
{
	if (gUnicodeInited)
	{
		UniChar u = c;
		UppercaseText(&u, 1);
		return u;
	}
	if ((c >= 'a' && c <= 'z') || (c >= 0xe0 && c <= 0xfe && c != 0xf7))
		return c - 0x20;
	return c;
}


// ROM 0x00257374 IsAlphabet__FUs
// A letter: uppercased without diacriticals it is A-Z (or the German sharp
// s, 0xdf, which has no upper case).  Host: before InitUnicode Latin-1's
// letters are taken.
Boolean
IsAlphabet(UniChar c)
{
	if (gUnicodeInited)
	{
		UniChar u = c;
		UppercaseNoDiacriticsText(&u, 1);
		return (u >= 'A' && u <= 'Z') || u == 0xdf;
	}
	UniChar u = UToUpper(c);
	if (u >= 0xc0 && u <= 0xde && u != 0xd7)
		return true;
	return (u >= 'A' && u <= 'Z') || u == 0xdf;
}


// ROM 0x002573cc IsDigit__FUs
Boolean
IsDigit(UniChar c)
{
	return c >= '0' && c <= '9';
}


// ROM 0x002573f4 IsHexDigit__FUs
Boolean
IsHexDigit(UniChar c)
{
	return IsDigit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}


// ROM 0x0025744c IsAlphaNumeric__FUs
Boolean
IsAlphaNumeric(UniChar c)
{
	return IsDigit(c) || IsAlphabet(c);
}


// ROM 0x002574ec IsSpace__FUs
Boolean
IsSpace(UniChar c)
{
	return c == ' ';
}


// ROM 0x00257508 IsTab__FUs
Boolean
IsTab(UniChar c)
{
	return c == '\t';
}


// ROM 0x00257590 IsBreaker__FUs
Boolean
IsBreaker(UniChar c)
{
	return c == '\n' || c == '\r';
}


// ROM 0x00257574 IsReturn__FUs
Boolean
IsReturn(UniChar c)
{
	return c == '\r';
}


// ROM 0x002575b0 IsDelimiter__FUs
// The ASCII break table's byte for the Mac Roman character.  Host:
// before InitUnicode anything but a letter or digit.
Boolean
IsDelimiter(UniChar c)
{
	if (gASCIIBreakTable == nil)
		return !IsAlphaNumeric(c);
	unsigned char a = c < 0x80 ? (unsigned char) c : (unsigned char) A_CONST_CHAR(c);
	return gASCIIBreakTable[a] != 0;
}


// ROM 0x00257494 IsWhiteSpace__FUs
Boolean
IsWhiteSpace(UniChar c)
{
	return IsSpace(c) || IsTab(c) || IsBreaker(c);
}
