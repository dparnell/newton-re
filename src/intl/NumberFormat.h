/*
	File:		intl/NumberFormat.h

	Contains:	Numbers as text in the current locale: the number format's
				decimal point, group separator and width, minus prefix and
				suffix, currency prefix and suffix (Locale.h caches them),
				applied to a number printed by the C library (NumberString:
				a printf format; NumberStringSpec/IntegerStringSpec: a
				format spec word, the NewtonScript FormattedNumberStr's).
				ParamString substitutes ^0..^9 in a Unicode prototype.

	Reconstructed from the MP2x00 US ROM (0x000ebd80-0x000ec000,
	0x000ecfec-0x000edc24); each function cites its origin.
*/

#ifndef __NUMBERFORMAT_H
#define __NUMBERFORMAT_H

#ifndef __NEWTONTYPES_H
#include "NewtonTypes.h"
#endif

// the format spec of FormattedNumberStr (the ROM's symbols do not name
// these; the names describe what the bits do)
const ULong kFormatDecimalPlacesMask = 0x0f;		// with kFormatDecimalPlaces: how many
const ULong kFormatCurrency = 0x10;					// the currency prefix and suffix
const ULong kFormatGroupDigits = 0x20;				// the group separator every groupWidth digits
const ULong kFormatParenthesizeNegative = 0x40;		// (n) instead of the minus prefix and suffix
const ULong kFormatDecimalPlaces = 0x80;			// a real with the low bits' decimal places (an integer becomes a real)
const ULong kFormatPercent = 0x100;					// times 100 with a % suffix
const ULong kFormatSignificant = 0x200;				// with kFormatDecimalPlaces: trailing zeros dropped

// results
const long kNumberTooLarge = -2;
const long kNumberTooSmall = -3;
const long kNumberStringTooLong = -10;

extern long		gNumberGroupWidth;			// 0x0c10108c  digits per group (3)
extern Boolean	gNumberLeadingZero;			// 0x0c101090  a zero before the decimal point
extern UniChar*	gPositiveNumProto;			// 0x0c101098  "^0<decimal point>^1"
extern UniChar*	gNegativeNumProto;			// 0x0c10109c  "<minus prefix>^0<decimal point>^1<minus suffix>"
extern UniChar*	gPositiveIntProto;			// 0x0c1010a0  "^0"
extern UniChar*	gNegativeIntProto;			// 0x0c1010a4  "<minus prefix>^0<minus suffix>"

UniChar*	PositiveNumberProtoStr(void);
UniChar*	NegativeNumberProtoStr(void);
UniChar*	PositiveIntProtoStr(void);
UniChar*	NegativeIntProtoStr(void);

void	ParamString(UniChar* dest, const long max, const UniChar* proto, ...);

long	_IntlNumberMunge(const char* digits, UniChar* str, Boolean negative, ULong intLength, ULong max, ULong flags);
long	IntegerStringSpec(Long n, UniChar* str, ULong max, ULong flags);
long	NumberStringSpec(double d, UniChar* str, ULong max, ULong flags);
long	NumberString(double d, UniChar* str, ULong max, const char* format);

#endif	/* __NUMBERFORMAT_H */
