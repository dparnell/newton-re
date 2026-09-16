/*
	File:		utility/Unicode.h

	Contains:	Unicode strings: the UniChar string functions (Ustrlen and
				friends, the ROM's UnicodeUtils at 0x002547ec-0x0025498c)
				and the conversions between Unicode and 8-bit encodings.
				The DDK has no header for these; the declarations are the
				ROM's (symbols.txt) and this file is ours.

	NOT YET RECONSTRUCTED: the encoding tables InitUnicode (0x00254b80)
	installs - ConvertToUnicode/ConvertFromUnicode are what the ROM does
	before it: bytes widened as they are, and characters over 0x7f
	narrowed to 0x1a.  Encoding 1 (kMacRomanEncoding) is the one the
	frames code asks for.
*/

#ifndef __UNICODE_H
#define __UNICODE_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

// the encodings ConvertToUnicode/ConvertFromUnicode take (the ROM's
// table indices; only their identity matters until the tables are done)
const long	kASCIIEncoding = 0;
const long	kMacRomanEncoding = 1;

extern "C" {
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
// one character each way (the compiler's)
UniChar	U_CONST_CHAR(unsigned char c);
char	A_CONST_CHAR(UniChar c);

// character classes (the ROM's UnicodeUtils; NOT YET RECONSTRUCTED: the
// case tables UppercaseNoDiacriticsText and LowercaseText - letters and
// cases are Latin-1's here)
Boolean	IsAlphabet(UniChar c);
Boolean	IsDigit(UniChar c);
Boolean	IsHexDigit(UniChar c);
Boolean	IsAlphaNumeric(UniChar c);
Boolean	IsWhiteSpace(UniChar c);
Boolean	IsSpace(UniChar c);
Boolean	IsTab(UniChar c);
Boolean	IsBreaker(UniChar c);
UniChar	UToLower(UniChar c);
UniChar	UToUpper(UniChar c);

#endif	/* __UNICODE_H */
