/*
	File:		intl/NumberFormat.cpp

	Contains:	Numbers as text in the current locale.  The C library prints
				the digits (sprintf with the caller's format, or the fixed
				formats of a format spec); _IntlNumberMunge and NumberString
				then put the locale's separators, signs and currency around
				them.  ParamString and the ^0/^1 prototype strings the
				number format is cached into.

	Reconstructed from the MP2100 D ROM (0x000ed358-0x000ed5d8,
	0x000ee5a4-0x000ef27c); each function cites its origin.  The ROM
	prints a format spec's digits with its C library's _fp_display
	(seventeen significant digits, the rest '<'/'>' markers made zeros);
	the host's snprintf prints the exact expansion instead, so very large
	or precise numbers can differ in their low digits.
*/

#include "NumberFormat.h"
#include "Locale.h"
#include "Unicode.h"
#include "NewtonMemory.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

// ROM 0x0c10108c gNumberGroupWidth
long		gNumberGroupWidth = 3;
// ROM 0x0c101090 gNumberLeadingZero
Boolean		gNumberLeadingZero = false;
// ROM 0x0c101098 gPositiveNumProto
UniChar*	gPositiveNumProto = nil;
// ROM 0x0c10109c gNegativeNumProto
UniChar*	gNegativeNumProto = nil;
// ROM 0x0c1010a0 gPositiveIntProto
UniChar*	gPositiveIntProto = nil;
// ROM 0x0c1010a4 gNegativeIntProto
UniChar*	gNegativeIntProto = nil;

const long	kMaxDigits = 21;			// _IntlNumberMunge refuses longer digit strings
const long	kMaxPartLength = 32;		// NumberString's grouped integer and fraction parts


// the locale cache's string; the C locale's when the cache is not made (a
// host without the international utilities: ".", "," and "-")
static const UniChar*
CachedString(RefStruct LocaleCache::* which)
{
	static const UniChar empty[1] = { 0 };
	static const UniChar point[2] = { '.', 0 };
	static const UniChar comma[2] = { ',', 0 };
	static const UniChar minus[2] = { '-', 0 };
	if (gLocaleCache == nil)
	{
		if (which == &LocaleCache::fDecimalPoint)
			return point;
		if (which == &LocaleCache::fGroupSepStr)
			return comma;
		if (which == &LocaleCache::fMinusPrefix)
			return minus;
		return empty;
	}
	if ((Ref) (gLocaleCache->*which) == NILREF)
		return empty;
	return GetCString(gLocaleCache->*which);
}


/*------------------------------------------------------------------------------
	P r o t o t y p e   s t r i n g s
	The number format's pieces around ^0 (the integer digits) and ^1 (the
	fraction), made once and kept until the locale changes.
------------------------------------------------------------------------------*/

// ROM 0x000ed530 PositiveIntProtoStr__Fv
UniChar*
PositiveIntProtoStr(void)
{
	if (gPositiveIntProto == nil)
	{
		UniChar* proto = (UniChar*) NewPtrClear(6);
		if (proto != nil)
		{
			ConvertToUnicode("^0", proto, kMacRomanEncoding, 2);
			gPositiveIntProto = proto;
		}
	}
	return gPositiveIntProto;
}


// ROM 0x000ed584 NegativeIntProtoStr__Fv
UniChar*
NegativeIntProtoStr(void)
{
	if (gNegativeIntProto == nil)
	{
		const UniChar* positive = PositiveIntProtoStr();
		const UniChar* prefix = CachedString(&LocaleCache::fMinusPrefix);
		const UniChar* suffix = CachedString(&LocaleCache::fMinusSuffix);
		UniChar* proto = (UniChar*) NewPtrClear((Ustrlen(positive) + Ustrlen(prefix) + Ustrlen(suffix)) * sizeof(UniChar) + 2);
		if (proto != nil)
		{
			Ustrcpy(proto, prefix);
			Ustrcat(proto, positive);
			Ustrcat(proto, suffix);
			gNegativeIntProto = proto;
		}
	}
	return gNegativeIntProto;
}


// ROM 0x000ed358 PositiveNumberProtoStr__Fv
UniChar*
PositiveNumberProtoStr(void)
{
	if (gPositiveNumProto == nil)
	{
		const UniChar* decimalPoint = CachedString(&LocaleCache::fDecimalPoint);
		long length = Ustrlen(decimalPoint);
		UniChar* proto = (UniChar*) NewPtrClear(length * sizeof(UniChar) + 10);
		if (proto != nil)
		{
			proto[0] = '^';
			proto[1] = '0';
			Ustrcpy(proto + 2, decimalPoint);
			proto[length + 2] = '^';
			proto[length + 3] = '1';
			gPositiveNumProto = proto;
		}
	}
	return gPositiveNumProto;
}


// ROM 0x000ed440 NegativeNumberProtoStr__Fv
UniChar*
NegativeNumberProtoStr(void)
{
	if (gNegativeNumProto == nil)
	{
		const UniChar* positive = PositiveNumberProtoStr();
		const UniChar* prefix = CachedString(&LocaleCache::fMinusPrefix);
		const UniChar* suffix = CachedString(&LocaleCache::fMinusSuffix);
		UniChar* proto = (UniChar*) NewPtrClear((Ustrlen(positive) + Ustrlen(prefix) + Ustrlen(suffix)) * sizeof(UniChar) + 2);
		if (proto != nil)
		{
			Ustrcpy(proto, prefix);
			Ustrcat(proto, positive);
			Ustrcat(proto, suffix);
			gNegativeNumProto = proto;
		}
	}
	return gNegativeNumProto;
}


/*------------------------------------------------------------------------------
	P a r a m S t r i n g
------------------------------------------------------------------------------*/

// ROM 0x000ef0d8 ParamString__FPUsClPCUse
// The prototype copied to dest (at most max characters), each ^digit
// replaced by a UniChar string argument: the arguments are taken in the
// order the markers appear, the digit says which of them to insert (nil
// inserts nothing).  Up to ten markers.
void
ParamString(UniChar* dest, const long max, const UniChar* proto, ...)
{
	const UniChar* args[10] = { nil, nil, nil, nil, nil, nil, nil, nil, nil, nil };
	long positions[10];
	long count = 0;
	va_list ap;
	va_start(ap, proto);
	const UniChar* p = proto;
	while (*p != 0)
	{
		if (*p == '^')
		{
			p++;
			if (*p == 0)
				break;
			if ((ULong) (*p - '0') < 10)
			{
				args[count] = va_arg(ap, const UniChar*);
				positions[count] = (p - proto) - 1;
				count++;
			}
		}
		p++;
	}
	va_end(ap);
	long protoLength = p - proto;

	long room = max;
	long next = 0;
	long nextPosition = (count > 0) ? positions[0] : -1;
	UniChar* d = dest;
	for (long i = 0; i < protoLength && room != 0; i++)
	{
		if (proto[i] == 0)
			break;
		if (i == nextPosition)
		{
			i++;
			const UniChar* arg = args[(ULong) (proto[i] - '0') & 0xff];
			if (arg != nil)
			{
				long length = Ustrlen(arg);
				if ((ULong) room < (ULong) length)
				{
					Ustrncpy(d, arg, room);
					room = 0;
				}
				else
				{
					Ustrcpy(d, arg);
					room -= length;
					d += length;
				}
			}
			next++;
			nextPosition = (next < count) ? positions[next] : -1;
		}
		else
		{
			*d++ = proto[i];
			room--;
		}
	}
	*d = 0;
}


/*------------------------------------------------------------------------------
	F o r m a t   s p e c s
------------------------------------------------------------------------------*/

// the digits of the integer part grouped: the group separator every
// gNumberGroupWidth digits from the right
static UniChar*
GroupDigits(const char* digits, long count, UniChar* dest)
{
	const UniChar* separator = CachedString(&LocaleCache::fGroupSepStr);
	long separatorLength = Ustrlen(separator);
	long first = count % gNumberGroupWidth;
	if (first == 0)
		first = gNumberGroupWidth;
	UniChar* d = dest;
	if (count != 0)
	{
		for (;;)
		{
			ConvertToUnicode(digits, d, kMacRomanEncoding, first);
			count -= first;
			d += first;
			digits += first;
			if (count == 0)
				break;
			Ustrcpy(d, separator);
			d += separatorLength;
			first = gNumberGroupWidth;
		}
	}
	*d = 0;
	return d;
}


// ROM 0x000ee5a4 _IntlNumberMunge__FPcPUsUcUlN24
// The digits (an integer part of intLength characters - 0: all of them -
// then a '.' and the fraction) put into str with the locale's pieces:
// the group separators when asked, the decimal point, the minus prefix
// and suffix (or parentheses) when negative, the currency prefix and
// suffix, a percent sign.  ==> 0, kNumberTooLarge for more than twenty
// digits, kNumberStringTooLong when max is too small.
long
_IntlNumberMunge(const char* digits, UniChar* str, Boolean negative, ULong intLength, ULong max, ULong flags)
{
	ULong length = strlen(digits);
	if (length >= (ULong) kMaxDigits)
		return kNumberTooLarge;
	UniChar intPart[kMaxPartLength * 2];
	UniChar fraction[kMaxPartLength];
	UniChar prefix[kMaxPartLength];
	UniChar suffix[kMaxPartLength];
	long fractionLength = 0;
	if (intLength == 0)
		intLength = length;
	long intPartLength;
	if ((flags & kFormatGroupDigits) == 0)
	{
		ConvertToUnicode(digits, intPart, kMacRomanEncoding, intLength);
		intPart[intLength] = 0;
		intPartLength = intLength;
	}
	else
		intPartLength = GroupDigits(digits, intLength, intPart) - intPart;
	if (intLength != length)
	{
		fractionLength = length - intLength - 1;
		ConvertToUnicode(digits + intLength + 1, fraction, kMacRomanEncoding, fractionLength);
		fraction[fractionLength] = 0;
	}

	UniChar* p = prefix;
	if (negative)
	{
		if ((flags & kFormatParenthesizeNegative) == 0)
		{
			const UniChar* minusPrefix = CachedString(&LocaleCache::fMinusPrefix);
			Ustrcpy(p, minusPrefix);
			p += Ustrlen(minusPrefix);
		}
		else
			*p++ = '(';
	}
	if (flags & kFormatCurrency)
	{
		const UniChar* currencyPrefix = CachedString(&LocaleCache::fCurrencyPrefix);
		Ustrcpy(p, currencyPrefix);
		p += Ustrlen(currencyPrefix);
	}
	*p = 0;
	long prefixLength = p - prefix;

	p = suffix;
	if (flags & kFormatCurrency)
	{
		const UniChar* currencySuffix = CachedString(&LocaleCache::fCurrencySuffix);
		Ustrcpy(p, currencySuffix);
		p += Ustrlen(currencySuffix);
	}
	if (flags & kFormatPercent)
		*p++ = '%';
	if (negative)
	{
		if ((flags & kFormatParenthesizeNegative) == 0)
		{
			const UniChar* minusSuffix = CachedString(&LocaleCache::fMinusSuffix);
			Ustrcpy(p, minusSuffix);
			p += Ustrlen(minusSuffix);
		}
		else
			*p++ = ')';
	}
	*p = 0;
	long suffixLength = p - suffix;

	if (max < (ULong) (prefixLength + intPartLength + fractionLength + suffixLength))
		return kNumberStringTooLong;
	p = str;
	if (prefixLength != 0)
	{
		Ustrcpy(p, prefix);
		p += prefixLength;
	}
	if (intPartLength != 0)
	{
		Ustrcpy(p, intPart);
		p += intPartLength;
	}
	if (fractionLength != 0)
	{
		const UniChar* decimalPoint = CachedString(&LocaleCache::fDecimalPoint);
		Ustrcpy(p, decimalPoint);
		p += Ustrlen(decimalPoint);
		Ustrcpy(p, fraction);
		p += fractionLength;
	}
	if (suffixLength != 0)
	{
		Ustrcpy(p, suffix);
		p += suffixLength;
	}
	*p = 0;
	return 0;
}


// ROM 0x0005f6f4 UiToA__FUlPUc
// An unsigned integer's decimal digits.
static void
UiToA(ULong n, char* str)
{
	snprintf(str, 16, "%lu", (unsigned long) n);
}


// ROM 0x000eea4c IntegerStringSpec__FlPUsUlT3
// An integer under a format spec: as a real when decimal places are
// asked for; times 100 for a percentage.
long
IntegerStringSpec(long n, UniChar* str, ULong max, ULong flags)
{
	if (flags & kFormatDecimalPlaces)
		return NumberStringSpec((double) n, str, max, flags);
	if (flags & kFormatPercent)
		n *= 100;
	Boolean negative = n < 0;
	if (negative)
		n = -n;
	char digits[20];
	UiToA(n, digits);
	return _IntlNumberMunge(digits, str, negative, 0, max, flags);
}


// ROM 0x000eeae0 NumberStringSpec__FdPUsUlT3
// A real under a format spec: six decimal places with the trailing zeros
// dropped, or the spec's decimal places (kept, or dropped when
// kFormatSignificant), times 100 for a percentage; then munged.
long
NumberStringSpec(double d, UniChar* str, ULong max, ULong flags)
{
	Boolean stripZeros = true;
	long places = 6;
	if (flags & kFormatDecimalPlaces)
	{
		places = flags & kFormatDecimalPlacesMask;
		if ((flags & kFormatSignificant) == 0)
			stripZeros = false;
	}
	if (flags & kFormatPercent)
		d *= 100.0;
	Boolean negative = d < 0.0;
	if (negative)
		d = -d;
	char digits[400];
	long length = snprintf(digits, sizeof(digits), "%.*f", (int) places, d);
	if (length < 0 || length >= (long) sizeof(digits))
		return kNumberTooLarge;
	if (stripZeros && strchr(digits, '.') != nil)
	{
		while (length > 0 && digits[length - 1] == '0')
			length--;
		if (length > 0 && digits[length - 1] == '.')
			length--;
		digits[length] = 0;
	}
	ULong intLength = length;
	const char* point = strchr(digits, '.');
	if (point != nil)
		intLength = point - digits;
	return _IntlNumberMunge(digits, str, negative, intLength, max, flags);
}


// ROM 0x000eec44 NumberString__FdPUsUlPc
// A real printed by a printf format, then localised: the integer digits
// grouped, the sign as the minus prefix and suffix, the decimal point
// the locale's (an exponent form only gets the decimal point); what
// precedes the first digit (a width's spaces) and follows the number is
// carried over.  ==> 0, kNumberTooLarge/kNumberTooSmall beyond the
// doubles or when max is too small for a positive/negative number.
long
NumberString(double d, UniChar* str, ULong max, const char* format)
{
	str[0] = 0;
	if (d > 1.7976931348623155e+308)
		return kNumberTooLarge;
	if (d < -1.7976931348623155e+308)
		return kNumberTooSmall;
	Boolean positive = !(d < 0.0);
	if (!positive)
		d = -d;
	char buffer[64];
	snprintf(buffer, sizeof(buffer), format, d);

	const char* point = nil;			// the '.'
	const char* intStart = nil;			// the first digit
	const char* suffix = nil;			// what follows the number
	long prefixLength = 0;				// what precedes the first digit
	long intDigits = 0;
	long suffixLength = 0;
	for (const char* p = buffer; *p != 0; p++)
	{
		char c = *p;
		if (c == '.')
		{
			point = p;
			continue;
		}
		if (c == 'e')
		{
			// an exponent form: reprinted with its sign, the decimal point the locale's
			if (!positive)
				snprintf(buffer, sizeof(buffer), format, -d);
			const char* dot = strchr(buffer, '.');
			if (dot == nil)
				ConvertToUnicode(buffer, str, kMacRomanEncoding, 0x7fffffff);
			else
			{
				long before = dot - buffer;
				const UniChar* decimalPoint = CachedString(&LocaleCache::fDecimalPoint);
				ConvertToUnicode(buffer, str, kMacRomanEncoding, before);
				Ustrcpy(str + before, decimalPoint);
				ConvertToUnicode(dot + 1, str + before + Ustrlen(decimalPoint), kMacRomanEncoding, 0x7fffffff);
			}
			return 0;
		}
		if (c < '0' || c > '9')
		{
			if (point != nil && suffix == nil)
				suffix = p;
		}
		else
		{
			if (intDigits == 0)
				intStart = p;
			if (point == nil)
				intDigits++;
		}
		if (intDigits == 0)
			prefixLength++;
	}
	if (suffix != nil)
		suffixLength = strlen(suffix);

	const UniChar* separator = CachedString(&LocaleCache::fGroupSepStr);
	long separatorLength = Ustrlen(separator);
	const UniChar* proto;
	if (positive)
		proto = (point == nil) ? PositiveIntProtoStr() : PositiveNumberProtoStr();
	else
		proto = (point == nil) ? NegativeIntProtoStr() : NegativeNumberProtoStr();
	long groupedLength = separatorLength * ((intDigits - 1) / gNumberGroupWidth) + intDigits;
	long fractionLength = (point != nil) ? (long) strlen(point + 1) - suffixLength : 0;
	if (max <= (ULong) (Ustrlen(proto) + prefixLength + suffixLength + groupedLength + fractionLength))
		return positive ? kNumberTooLarge : kNumberTooSmall;
	if (groupedLength >= kMaxPartLength || fractionLength >= kMaxPartLength)
		return 0;

	UniChar grouped[kMaxPartLength];
	UniChar fraction[kMaxPartLength];
	grouped[0] = 0;
	if (intStart != nil)
		GroupDigits(intStart, intDigits, grouped);
	fraction[0] = 0;
	if (point != nil)
		ConvertToUnicode(point + 1, fraction, kMacRomanEncoding, fractionLength);
	if (prefixLength != 0)
	{
		ConvertToUnicode(buffer, str, kMacRomanEncoding, prefixLength);
		str += prefixLength;
	}
	ParamString(str, max, proto, grouped, fraction);
	if (suffixLength != 0)
	{
		long length = Ustrlen(str);
		ConvertToUnicode(suffix, str + length, kMacRomanEncoding, suffixLength);
	}
	return 0;
}
