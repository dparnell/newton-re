/*
	File:		utility/Unicode.h

	Contains:	Unicode strings: the UniChar string functions (Ustrlen and
				friends, the ROM's UnicodeUtils at 0x002547ec-0x0025498c)
				and the conversions between Unicode and 8-bit encodings.
				The DDK has no header for these; the declarations are the
				ROM's (symbols.txt) and this file is ours.

	The encodings are tables InitUnicode (0x00254b80, frames/UnicodeTables.h
	- it reads the ROM's 'unicode frame) installs with InstallCharEncoding:
	a mapping binary (GetMappingInfo) and its converter each way - kind 0
	"contiguous 8": 256 UniChars indexed by the byte; kind 4 "segmented
	16": ranges of Unicode (starts, ends, offsets into a byte table), 0x1a
	for a character outside them.  Until they are installed
	ConvertToUnicode/ConvertFromUnicode do what the ROM does before
	InitUnicode: bytes widened as they are, characters over 0x7f narrowed
	to 0x1a.  Encoding 1 (kMacRomanEncoding) is the one the frames code
	asks for.  The case conversions (UppercaseText & co.) go through the
	'unicode frame's charClass table (a class per Mac Roman character)
	and the per-class deltas (upperList, lowerList, upperNoMarkList,
	noMarkList); IsDelimiter through the ASCII break table.  Before
	InitUnicode the host's UToLower/UToUpper/IsAlphabet know Latin-1's
	letters (the ROM would read through null pointers).
*/

#ifndef __UNICODE_H
#define __UNICODE_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

// the encodings ConvertToUnicode/ConvertFromUnicode take (the ROM's
// table indices; the 'unicode frame installs 1-4, so 0 converts nothing
// once the tables are in)
const long	kASCIIEncoding = 0;
const long	kMacRomanEncoding = 1;

extern "C" {
UniChar*	Ustrcpy(UniChar* dest, const UniChar* src);
UniChar*	Ustrncpy(UniChar* dest, const UniChar* src, long n);		// at most n, always terminated (dest holds n + 1)
UniChar*	Ustrcat(UniChar* dest, const UniChar* src);
UniChar*	Ustrncat(UniChar* dest, const UniChar* src, long n);
long		Ustrlen(const UniChar* s);
UniChar*	Ustrchr(const UniChar* s, UniChar c);
long		Umbstrlen(const UniChar* s);
long		Umbstrnlen(const UniChar* s, long unused, long n);
int			Ustrcmp(const UniChar* a, const UniChar* b);
void		Umemset(UniChar* dest, UniChar c, long n);
}

// 8-bit characters to UniChars (a 0 ends the output), at most n
void	ConvertToUnicode(const void* src, UniChar* dest, long encoding, long n);
// UniChars to 8-bit characters (a 0 ends the output), at most n
void	ConvertFromUnicode(const UniChar* src, void* dest, long encoding, long n);
long	ConvertUnicodeChar(const UniChar* src, char* dest, long encoding);						// ROM 0x0025668c: one character; ==> the bytes it made
void	ConvertUnicodeCharacters(const UniChar* src, char* dest, long encoding, long n);		// ROM 0x002566bc: n bytes' worth (encoding 0: n bytes moved)
// one character each way (the compiler's)
UniChar	U_CONST_CHAR(unsigned char c);
char	A_CONST_CHAR(UniChar c);

// the encoding tables (installed by InitUnicode)
struct TEncodingMap			// 0x20 bytes, from a mapping binary (GetMappingInfo)
{
	UShort			fKind;			// +0x00  0: contiguous 8-bit to Unicode; 4: segmented Unicode to 8-bit
	UShort			fUnused;
	ULong			fSize;			// +0x04  the binary's halfword at 2 (256: the table's entries)
	ULong			fFlags;			// +0x08  the halfword at 4
	ULong			fSegments;		// +0x0c  the halfword at 6: the segment count
	const void*		fTable;			// +0x10  kind 0: the UniChars; kind 4: the bytes
	const UniChar*	fStarts;		// +0x14  kind 4: each segment's first Unicode character
	const UniChar*	fEnds;			// +0x18  kind 4: each segment's last
	const short*	fOffsets;		// +0x1c  kind 4: added to the character for its byte's index
};
typedef void (*ConvertFromUnicodeProcPtr)(const UniChar* src, void* dest, void* map, long n);
typedef void (*ConvertToUnicodeProcPtr)(const void* src, UniChar* dest, void* map, long n);
struct CharEncoding			// (ROM gUnicode, 0x0c104858): one per encoding id (0-4)
{
	void*						fFromMap;
	ConvertFromUnicodeProcPtr	fFromUnicode;
	void*						fToMap;
	ConvertToUnicodeProcPtr		fToUnicode;
};
const long kNumberOfEncodings = 5;
extern CharEncoding	gUnicode[kNumberOfEncodings];
extern Boolean		gUnicodeInited;				// 0x0c101fd4  the tables are in
extern Boolean		gHasUnicode;				// 0x0c101fd0
extern const UniChar*	gASCIItoUnicodeTable;	// 0x0c101fd8  Mac Roman's kind 0 table
extern const unsigned char*	gASCIIBreakTable;	// 0x0c101fdc  a byte per Mac Roman character: a delimiter
extern const unsigned char*	gCharClass;			// 0x0c1048ac  a class per Mac Roman character
extern const unsigned char*	gTypeList;			// 0x0c1048b0  the classes' types
extern const signed char*	gUpperList;			// 0x0c1048b4  the classes' deltas to upper case
extern const signed char*	gLowerList;			// 0x0c1048b8  ... to lower case
extern const signed char*	gUpperNoMarkList;	// 0x0c1048bc  ... to upper case without diacriticals
extern const signed char*	gNoMarkList;		// 0x0c1048c0  ... without diacriticals

void	GetMappingInfo(const void* mapping, TEncodingMap* map, void** converter);		// ==> the converter for the kind ((void*) -1 for an unknown one)
void	InstallCharEncoding(UShort encoding, void* fromMap, void* toMap, ConvertFromUnicodeProcPtr fromUnicode, ConvertToUnicodeProcPtr toUnicode);
void	ConvertToUnicodeFunc_Contiguous8(const void* src, UniChar* dest, void* map, long n);
void	ConvertFromUnicodeFunc_Segmented16(const UniChar* src, void* dest, void* map, long n);
void	InstallCharTables(const unsigned char* charClass, const unsigned char* typeList, const signed char* upperList, const signed char* lowerList, const signed char* upperNoMarkList, const signed char* noMarkList, const unsigned char* breakTable);	// host: what InitUnicode stores

// case (in place, n characters or to a 0)
void	ConvertTextCase(UniChar* text, long n, const signed char* deltas);
void	UppercaseText(UniChar* text, long n);
void	LowercaseText(UniChar* text, long n);
void	NoDiacriticsText(UniChar* text, long n);
void	UppercaseNoDiacriticsText(UniChar* text, long n);
UniChar	ToggleCase(UniChar c);

// character classes (the ROM's UnicodeUtils, over the case tables InitUnicode
// installs - frames/UnicodeTables.h; before it, letters and cases are
// Latin-1's)
Boolean	IsAlphabet(UniChar c);
Boolean	IsDigit(UniChar c);
Boolean	IsHexDigit(UniChar c);
Boolean	IsAlphaNumeric(UniChar c);
Boolean	IsWhiteSpace(UniChar c);
Boolean	IsSpace(UniChar c);
Boolean	IsTab(UniChar c);
Boolean	IsBreaker(UniChar c);
Boolean	IsReturn(UniChar c);
Boolean	IsDelimiter(UniChar c);			// (host: not a letter or digit, before InitUnicode)
UniChar	UToLower(UniChar c);
UniChar	UToUpper(UniChar c);

#endif	/* __UNICODE_H */
