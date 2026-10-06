/*
	File:		intl/LexParse.cpp

	Contains:	The parse buffer and its converters, and the number parser
				(LexParse.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	The number parser works in the ARM's FPA doubles; here in C doubles,
	the same IEEE arithmetic.
*/

#include "LexParse.h"
#include "Dates.h"
#include "Unicode.h"
#include <string.h>
#include <stdlib.h>


// ROM 0x0c100f8c-0x0c100f98 - the lexicons the locale carries
Handle		gTimeLexDictionary = nil;
Handle		gDateLexDictionary = nil;
Handle		gPhoneLexDictionary = nil;
Handle		gNumberLexDictionary = nil;

ParseStringProcPtr	gParseStringProc = nil;
ReplaceDictionaryHandleProcPtr	gReplaceDictionaryHandleProc = nil;


void
CallReplaceDictionaryHandle(Handle* dictionary, RefArg slot)
{
	if (gReplaceDictionaryHandleProc != nil)
		gReplaceDictionaryHandleProc(dictionary, slot);
}


long
CallParseString(Handle dictionary, void* into, const UniChar* str, ULong* consumed, ULong length)
{
	if (gParseStringProc == nil)
	{
		*consumed = 0;
		return -1;
	}
	return gParseStringProc(dictionary, into, str, consumed, length);
}


/*------------------------------------------------------------------------------
	T h e   p a r s e   b u f f e r
------------------------------------------------------------------------------*/

// ROM 0x00181ac8 InitParseBuffer__FP12TParseBuffer
void
InitParseBuffer(TParseBuffer* buffer)
{
	buffer->fChars[0] = 0;
	buffer->fCount = 0;
	buffer->fConverter = 0;
}


// ROM 0x00181adc ConvertBuffer__FP12TParseBufferPv
// What the buffer holds put into the date (or the number parser) `into`
// by its converter.  ==> 0 when the value is out of range for the field,
// 1 otherwise (a converter this does not know leaves `into` alone).
long
ConvertBuffer(TParseBuffer* buffer, void* into)
{
	ULong converter = buffer->fConverter;
	if (buffer->fCount == 0 && converter == 0)
		return 1;
	buffer->fChars[buffer->fCount] = 0;
	Long32 value = (Long32) atol(buffer->fChars);
	TDate* date = (TDate*) into;
	TNumberParser* number = (TNumberParser*) into;
	switch (converter)
	{
	case 5:		// a year: two digits, or 1904..2099
		if (value < 100 || (value >= 1904 && value <= 2099))
		{
			date->fYear = value;
			return 1;
		}
		return 0;
	case 6:		// a month (0 passes)
		if (value > 12)
			return 0;
		date->fMonth = value;
		break;
	case 7:		// a date
		if (value < 32)
		{
			date->fDate = value;
			return 1;
		}
		return 0;
	case 9:		// an hour
		if (value < 25)
		{
			date->fHour = value;
			break;
		}
		return 0;
	case 10:	// a minute
		if (value < 60)
		{
			date->fMinute = value;
			return 1;
		}
		return 0;
	case 11:	// a second
		if (value < 60)
		{
			date->fSecond = value;
			return 1;
		}
		return 0;
	case 12:	// am: twelve is nought
		if (date->fHour == 12)
			date->fHour = 0;
		break;
	case 13:	// pm: before twelve goes on twelve (an unset hour is left be)
		if ((ULong32) date->fHour > 11)
			return 1;
		date->fHour = date->fHour + 12;
		break;
	case 14:
		number->SetInteger(buffer->fChars);
		break;
	case 15:
		number->SetDecimal(buffer->fChars);
		break;
	case 16:
		number->SetSign(0);
		break;
	case 0x15: case 0x16: case 0x17: case 0x18: case 0x19: case 0x1a:
	case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f: case 0x20:
		// a month by name
		date->fMonth = converter - 0x14;
		break;
	case 0x27:	// tomorrow: today's year and month, and the date after
				// today's (not put right at the end of the month here)
		{
			TDate today;
			today.SetCurrentTime();
			date->fYear = today.fYear;
			date->fMonth = today.fMonth;
			date->fDate = today.fDate + 1;
		}
		break;
	case 0x28: case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: case 0x2e:
		// a day of the week by name
		date->fDayOfWeek = converter - 0x28;
		break;
	case 0x33:
		number->SetPrefix(buffer->fChars);
		break;
	case 0x34:
		number->SetSuffix(buffer->fChars);
		break;
	default:
		break;
	}
	return 1;
}


/*------------------------------------------------------------------------------
	T h e   n u m b e r   p a r s e r
------------------------------------------------------------------------------*/

// ROM 0x00147330 DecimalStrToDouble__FPc
// The digits after a decimal point as the fraction they make: the
// trailing zeros dropped, then worked from the last digit back, each
// step (digit + so far) / 10.
double
DecimalStrToDouble(char* digits)
{
	double fraction = 0.0;
	if (digits != nil)
	{
		long i = (long) strlen(digits) - 1;
		// ROM BUG (fixed): the zeros are dropped with no check for the start
		// of the string, so one of nothing but zeros (or an empty one) is
		// read backwards out of the buffer until a byte that is not '0' -
		// which the host cannot reproduce; it stops at the start instead
		// (DEVIATION).  Stopping there is also the fix (a fraction of
		// nought), so both paths are one (no RomBugFixed() test).
		while (i >= 0 && digits[i] == '0')
			i--;
		for ( ; i >= 0; i--)
			fraction = ((double) ((long) (UByte) digits[i] - '0') + fraction) / 10.0;
	}
	return fraction;
}


// ROM 0x001473a4 __ct__13TNumberParserFv
TNumberParser::TNumberParser()
{
	Reset();
}


// ROM 0x001473d8 Reset__13TNumberParserFv
void
TNumberParser::Reset(void)
{
	fInteger = 0.0;
	fDecimal = 0.0;
	fDecimalLength = 0;
	fPrefix[0] = 0;
	fSuffix[0] = 0;
	SetNumberType(kNumberTypeNone);
	SetSign(1);
	fField24 = 0;
}


// ROM 0x00147420 StringToNumber__13TNumberParserFPCUsPUlUl
// The number the string spells through the locale's number dictionary:
// the integer and decimal parts added, negated when a minus sign was
// read (a sum of nought stays nought); 0 when nothing was parsed.
double
TNumberParser::StringToNumber(const UniChar* str, ULong* consumed, ULong length)
{
	double result = 0.0;
	*consumed = 0;
	if (str == nil || length == 0)
		return result;
	Reset();
	if (CallParseString(gNumberLexDictionary, this, str, consumed, length) == -1)
		return result;
	result = fInteger + fDecimal;
	if (result > 0.0 && fSign == 0)
		result = -result;
	return result;
}


// ROM 0x001474bc SetInteger__13TNumberParserFPc
void
TNumberParser::SetInteger(char* digits)
{
	if (digits != nil)
	{
		char* end;
		fInteger = strtod(digits, &end);
	}
}


// ROM 0x001474e8 SetDecimal__13TNumberParserFPc
void
TNumberParser::SetDecimal(char* digits)
{
	if (digits == nil)
		return;
	fDecimalLength = strlen(digits);
	fDecimal = DecimalStrToDouble(digits);
}


// ROM 0x00147520 SetPrefix__13TNumberParserFPc
void
TNumberParser::SetPrefix(char* prefix)
{
	strncpy(fPrefix, prefix, 3);
}


// ROM 0x0014752c SetSuffix__13TNumberParserFPc
void
TNumberParser::SetSuffix(char* suffix)
{
	strncpy(fSuffix, suffix, 3);
}


// ROM 0x00147538 SetSign__13TNumberParserFUc
void
TNumberParser::SetSign(UByte positive)
{
	fSign = positive;
}


// ROM 0x00147540 SetNumberType__13TNumberParserF11TNumberType
void
TNumberParser::SetNumberType(TNumberType type)
{
	fType = type;
}
