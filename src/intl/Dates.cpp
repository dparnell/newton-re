/*
	File:		intl/Dates.cpp

	Contains:	TDate and the NewtonScript date functions (Dates.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	The ROM divides with __rt_udiv/__rt_sdiv (quotient and remainder at
	once); here / and %.
*/

#include "Dates.h"
#include "Locale.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "Unicode.h"
#include "REPTranslators.h"
#include "NewtonTime.h"
#include "OSErrors.h"
#include <string.h>


// the years TDate formats and counts: 1904..2919 (a year of -1 is unset)
static inline Boolean
YearInRange(long year)
{
	return (year < 0xb60 || year - 0xb60 < 8) && year != -1;
}

static inline Boolean
IsLeapYear(long year)
{
	return (year & 3) == 0 && (year % 100 != 0 || year % 400 == 0);
}

// the next element type or format of a spec: three bits at a time
// ROM 0x0008cd94 GetNextElementType__FPUl
static ULong
GetNextElementType(ULong* spec)
{
	ULong element = *spec & 7;
	*spec >>= 3;
	return element;
}

// ROM 0x0008ce28 GetNextElementFormat__FPUl
static ULong
GetNextElementFormat(ULong* spec)
{
	ULong format = *spec & 7;
	*spec >>= 3;
	return format;
}

// A string object's text (locked while used); nil for a non-string.
static void
AppendStringObject(UniChar* str, RefArg obj, ULong max)
{
	if ((Ref) obj == NILREF)
		return;
	LockRef(obj);
	Ustrncat(str, (const UniChar*) BinaryData(obj), max);
	UnlockRef(obj);
}

static void
CopyStringObject(UniChar* str, RefArg obj, ULong max)
{
	str[0] = 0;
	AppendStringObject(str, obj, max);
}


/*------------------------------------------------------------------------------
	T D a t e
------------------------------------------------------------------------------*/

// ROM 0x00089ad0 __ct__5TDateFv
TDate::TDate()
{
	fYear = 0;
	fMonth = 0;
	fDate = 0;
	fHour = 0;
	fMinute = 0;
	fSecond = 0;
	fDayOfWeek = 0;
}


// ROM 0x0008c6c4 __ct__5TDateFUl
TDate::TDate(ULong minutes)
{
	InitWithMinutes(minutes);
}


// ROM 0x0008cda8 __ct__5TDateFPCUsPUlUl
TDate::TDate(const UniChar* str, ULong* consumed, ULong length)
{
	StringToDate(str, consumed, length);
}


// ROM 0x0008e75c InitWithMinutes__5TDateFUl
// The fields from minutes since 1904: the day's year and day of the
// year through the four-year cycle (1461 days), the month and date
// through the 128ths of a month (3919/128 = 30.6 days, from March), the
// day of the week from the day count (1904 began on a Friday).
void
TDate::InitWithMinutes(ULong minutes)
{
	ULong days = minutes / kMinutesPerDay;
	ULong minutesOfDay = minutes % kMinutesPerDay;
	ULong quarters = days * 4;
	long year = quarters / 1461 + kEpochYear;
	ULong dayOfYear = (quarters % 1461) >> 2;
	ULong februaryEnd = ((year & 3) == 0) ? 60 : 59;
	long monthBase = 1;
	if (dayOfYear >= februaryEnd)
	{
		monthBase = 3;
		dayOfYear -= februaryEnd;
	}
	ULong scaled = dayOfYear * 128 + 71;
	fMonth = scaled / 3919 + monthBase;
	fDate = (scaled % 3919) / 128 + 1;
	fYear = year;
	fHour = minutesOfDay / 60;
	fMinute = minutesOfDay % 60;
	fSecond = 0;
	fDayOfWeek = (days + 5) % 7;
}


// ROM 0x0008e72c InitWithSeconds__5TDateFUl
void
TDate::InitWithSeconds(ULong seconds)
{
	InitWithMinutes(seconds / 60);
	fSecond = seconds % 60;
}


// ROM 0x0008e450 SetCurrentTime__5TDateFv
void
TDate::SetCurrentTime(void)
{
	InitWithMinutes(RealClock());
}


// ROM 0x0008c9c8 CleanUpFields__5TDateFv
void
TDate::CleanUpFields(void)
{
	InitWithMinutes(TotalMinutes());
}


// ROM 0x0008e4f8 InitWithDateFrame__5TDateFRC6RefVarUc
// The fields from a date frame's integer slots; a missing or non-integer
// slot is -1, or the start of 1904 when fillIn (then the fields are
// normalised).  A nil frame changes nothing.
void
TDate::InitWithDateFrame(RefArg frame, Boolean fillIn)
{
	if ((Ref) frame == NILREF)
		return;
	Ref value = GetFrameSlotRef(frame, RSSYMyear);
	fYear = ISINT(value) ? RVALUE(value) : (fillIn ? kEpochYear : -1);
	value = GetFrameSlotRef(frame, RSSYMmonth);
	fMonth = ISINT(value) ? RVALUE(value) : (fillIn ? 1 : -1);
	value = GetFrameSlotRef(frame, RSSYMdate);
	fDate = ISINT(value) ? RVALUE(value) : (fillIn ? 1 : -1);
	value = GetFrameSlotRef(frame, RSSYMhour);
	fHour = ISINT(value) ? RVALUE(value) : (fillIn ? 0 : -1);
	value = GetFrameSlotRef(frame, RSSYMminute);
	fMinute = ISINT(value) ? RVALUE(value) : (fillIn ? 0 : -1);
	value = GetFrameSlotRef(frame, RSSYMsecond);
	fSecond = ISINT(value) ? RVALUE(value) : (fillIn ? 0 : -1);
	if (fillIn)
		CleanUpFields();
}


// ROM 0x0008e80c TotalDays__5TDateCFv
// Days since the start of 1904: the years' (a quarter day each for the
// leap years), the months' before this one (from March, 30.6 days each,
// January and February's before that) and the date's.
long
TDate::TotalDays(void) const
{
	long years = fYear < kEpochYear ? 0 : fYear - kEpochYear;
	long days = fDate - 1 + ((years * 1461 + 3) >> 2);
	long months = fMonth - 1;
	if (months > 1)
	{
		months = fMonth - 3;
		days += ((fYear & 3) == 0) ? 60 : 59;
	}
	return days + ((months * 3917 + 52) >> 7);
}


// ROM 0x0008e878 TotalHours__5TDateCFv
long
TDate::TotalHours(void) const
{
	return fHour + TotalDays() * 24;
}


// ROM 0x0008e89c TotalMinutes__5TDateCFv
long
TDate::TotalMinutes(void) const
{
	return fMinute + fHour * 60 + TotalDays() * kMinutesPerDay;
}


// ROM 0x0008c734 TotalSeconds__5TDateCFv
// (The ROM's 32-bit word wraps past 2^31 seconds, in 1972; so does this.)
long
TDate::TotalSeconds(void) const
{
	ULong32 seconds = (ULong32) fSecond + (ULong32) fMinute * 60 + (ULong32) fHour * 3600 + (ULong32) TotalDays() * 86400;
	return (long) (Long32) seconds;
}


// ROM 0x0008c778 DaysInMonth__5TDateCFv
// 0 for a month out of 1..12; February's by the year (29 for a year out
// of TDate's range).
long
TDate::DaysInMonth(void) const
{
	switch (fMonth)
	{
	case 1: case 3: case 5: case 7: case 8: case 10: case 12:
		return 31;
	case 4: case 6: case 9: case 11:
		return 30;
	case 2:
		if (YearInRange(fYear) && !IsLeapYear(fYear))
			return 28;
		return 29;
	default:
		return 0;
	}
}


// ROM 0x0008c84c DaysInYear__5TDateCFv
// The day of the year (January 1st is 1).
long
TDate::DaysInYear(void) const
{
	static const long kDaysBefore[] = { 0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
	long before = 0;
	if (fMonth >= 1 && fMonth <= 12)
	{
		before = kDaysBefore[fMonth];
		if (fMonth >= 3 && (!YearInRange(fYear) || IsLeapYear(fYear)))
			before++;
	}
	return fDate + before;
}


// ROM 0x0008c978 IsValidDate__5TDateCFv
// The month and date are set, the month is one and the date within it.
Boolean
TDate::IsValidDate(void) const
{
	return fMonth != -1 && fDate != -1 && (ULong) fMonth < 13 && (ULong) fDate <= (ULong) DaysInMonth();
}


// ROM 0x0008c9ec IncrementMonth__5TDateFl
// delta months on (or back), the year following, never before 1904 or
// past the last year; the date clipped to the month.
void
TDate::IncrementMonth(long delta)
{
	long month = fMonth + delta;
	long year = fYear;
	if (year == kEpochYear)
	{
		if (month < 1)
			return;
	}
	else if (!YearInRange(year) && year != -1 && month > 12)
		return;
	long years = (month - 1) / 12;
	fMonth = ((month - 1) % 12 + 12) % 12 + 1;
	if (YearInRange(year))
	{
		if (month < 1)
			years = month / 12 - 1;
		fYear = year + years;
	}
	long days = DaysInMonth();
	if ((ULong) fDate > (ULong) days)
		fDate = days;
}


// ROM 0x0008e420 __lt__FRC5TDateT1
Boolean
operator<(const TDate& a, const TDate& b)
{
	return (ULong) a.TotalMinutes() < (ULong) b.TotalMinutes();
}


// ROM 0x0008e474 __gt__FRC5TDateT1
Boolean
operator>(const TDate& a, const TDate& b)
{
	return (ULong) b.TotalMinutes() < (ULong) a.TotalMinutes();
}


// ROM 0x0008e4a4 __eq__FRC5TDateT1
Boolean
operator==(const TDate& a, const TDate& b)
{
	return a.fYear == b.fYear && a.fMonth == b.fMonth && a.fDate == b.fDate
		&& a.fHour == b.fHour && a.fMinute == b.fMinute && a.fSecond == b.fSecond;
}


/*------------------------------------------------------------------------------
	F o r m a t s
------------------------------------------------------------------------------*/

// ROM 0x0008cb50 SetFormatResource__5TDateFRC6RefVar
// The three formats from the locale bundle (the current one for nil),
// those not set already.
void
TDate::SetFormatResource(RefArg locale)
{
	if ((Ref) locale == NILREF)
	{
		SetFormatResource(RefVar(NILREF), RefVar(NILREF), RefVar(NILREF));
		return;
	}
	if ((Ref) fLongDateFormat == NILREF)
		fLongDateFormat = GetProtoVariable(locale, RSSYMlongdateformat, nil);
	if ((Ref) fShortDateFormat == NILREF)
		fShortDateFormat = GetProtoVariable(locale, RSSYMshortdateformat, nil);
	if ((Ref) fTimeFormat == NILREF)
		fTimeFormat = GetProtoVariable(locale, RSSYMtimeformat, nil);
}


// ROM 0x0008cc78 SetFormatResource__5TDateFRC6RefVarN21
// The three formats given, the current locale's for any that is nil.
void
TDate::SetFormatResource(RefArg longDateFormat, RefArg shortDateFormat, RefArg timeFormat)
{
	fLongDateFormat = longDateFormat;
	fShortDateFormat = shortDateFormat;
	fTimeFormat = timeFormat;
	if ((Ref) fLongDateFormat == NILREF || (Ref) fShortDateFormat == NILREF || (Ref) fTimeFormat == NILREF)
	{
		RefVar locale(GetCurrentLocale());
		if ((Ref) fLongDateFormat == NILREF)
			fLongDateFormat = GetProtoVariable(locale, RSSYMlongdateformat, nil);
		if ((Ref) fShortDateFormat == NILREF)
			fShortDateFormat = GetProtoVariable(locale, RSSYMshortdateformat, nil);
		if ((Ref) fTimeFormat == NILREF)
			fTimeFormat = GetProtoVariable(locale, RSSYMtimeformat, nil);
	}
}


// ROM 0x0008ce3c LongDateString__5TDateFUlPUsT1
// The date in the long format: the elements the spec asks for (all four
// for 0; the year dropped when out of range) in the locale's
// longDateOrder, each in the spec's format (the order's when 0),
// separated by the longDateDelim strings (the first before everything,
// then one after each element of the order that is not the last written).
void
TDate::LongDateString(ULong spec, UniChar* str, ULong max)
{
	if ((Ref) fLongDateFormat == NILREF)
	{
		RefVar locale(GetCurrentLocale());
		fLongDateFormat = GetProtoVariable(locale, RSSYMlongdateformat, nil);
	}
	Ref orderRef = GetProtoVariable(fLongDateFormat, RSSYMlongdateorder, nil);
	ULong order = orderRef == NILREF ? 0 : RINT(orderRef);
	RefVar delims(GetProtoVariable(fLongDateFormat, RSSYMlongdatedelim, nil));

	// what the spec asks for
	Boolean wantDay = false, wantDayOfWeek = false, wantMonth = false, wantYear = false;
	ULong dayFormat = 0, dayOfWeekFormat = 0, monthFormat = 0, yearFormat = 0;
	long count = 0;
	if (spec == 0)
	{
		wantDay = wantDayOfWeek = wantMonth = wantYear = true;
		count = 4;
	}
	else
	{
		while (spec != 0)
		{
			ULong element = GetNextElementType(&spec);
			ULong format = GetNextElementFormat(&spec);
			count++;
			switch (element)
			{
			case kDateElementDay:		wantDay = true;			dayFormat = format;			break;
			case kDateElementDayOfWeek:	wantDayOfWeek = true;	dayOfWeekFormat = format;	break;
			case kDateElementMonth:		wantMonth = true;		monthFormat = format;		break;
			case kDateElementYear:		wantYear = true;		yearFormat = format;		break;
			}
		}
	}
	if (wantYear && !YearInRange(fYear))
	{
		wantYear = false;
		count--;
	}

	CopyStringObject(str, RefVar(GetArraySlotRef(delims, 0)), max);
	UniChar element[32];
	for (long i = 0; order != 0; i++)
	{
		ULong type = GetNextElementType(&order);
		ULong format = GetNextElementFormat(&order);
		Boolean wanted;
		switch (type)
		{
		case kDateElementDay:		wanted = wantDay;		if (dayFormat != 0) format = dayFormat;					break;
		case kDateElementDayOfWeek:	wanted = wantDayOfWeek;	if (dayOfWeekFormat != 0) format = dayOfWeekFormat;		break;
		case kDateElementMonth:		wanted = wantMonth;		if (monthFormat != 0) format = monthFormat;				break;
		case kDateElementYear:		wanted = wantYear;		if (yearFormat != 0) format = yearFormat;				break;
		case kDateElementNone:		wanted = false;			break;
		default:					wanted = true;			count++;								break;
		}
		if (!wanted)
			continue;
		count--;
		DateElementString(type, format, element, 31, true);
		if (count != 0 || order == 0)
			AppendStringObject(element, RefVar(GetArraySlotRef(delims, i + 1)), 31);
		Ustrncat(str, element, max);
	}
}


// ROM 0x0008d194 ShortDateString__5TDateFUlPUsT1
// The date in the short format: the day, month and year (those the spec
// asks for; the year dropped when out of range) numerically in the
// locale's shortDateOrder with the shortDateDelim strings.
void
TDate::ShortDateString(ULong spec, UniChar* str, ULong max)
{
	if ((Ref) fShortDateFormat == NILREF)
	{
		RefVar locale(GetCurrentLocale());
		fShortDateFormat = GetProtoVariable(locale, RSSYMshortdateformat, nil);
	}
	ULong order = RINT(GetProtoVariable(fShortDateFormat, RSSYMshortdateorder, nil));
	RefVar delims(GetProtoVariable(fShortDateFormat, RSSYMshortdatedelim, nil));

	Boolean wantDay = false, wantMonth = false, wantYear = false;
	long count = 0;
	if (spec == 0)
	{
		wantDay = wantMonth = wantYear = true;
		count = 3;
	}
	else
	{
		while (spec != 0)
		{
			ULong element = GetNextElementType(&spec);
			GetNextElementFormat(&spec);
			count++;
			if (element == kDateElementDay)
				wantDay = true;
			else if (element == kDateElementMonth)
				wantMonth = true;
			else if (element == kDateElementYear)
				wantYear = true;
		}
	}
	if (wantYear && !YearInRange(fYear))
	{
		wantYear = false;
		count--;
	}

	CopyStringObject(str, RefVar(GetArraySlotRef(delims, 0)), max);
	UniChar element[32];
	for (long i = 0; order != 0; i++)
	{
		ULong type = GetNextElementType(&order);
		GetNextElementFormat(&order);
		Boolean wanted;
		switch (type)
		{
		case kDateElementDay:		wanted = wantDay;	break;
		case kDateElementMonth:		wanted = wantMonth;	break;
		case kDateElementYear:		wanted = wantYear;	break;
		case kDateElementNone:		wanted = false;		break;
		default:					wanted = true;		count++;	break;
		}
		if (!wanted)
			continue;
		count--;
		DateElementString(type, kElementFormatNumeric, element, 31, false);
		if (count != 0 || order == 0)
			AppendStringObject(element, RefVar(GetArraySlotRef(delims, i + 1)), 31);
		Ustrncat(str, element, max);
	}
}


// ROM 0x0008d440 TimeString__5TDateFUlPUsT1
// The time: the hour (in the locale's timeCycle: 1 is 12-hour, midnight
// and noon by its midNightForm and noonForm), the minute and second
// with the separators, each with a leading zero when the format asks
// (a ...LeadingZ of 0), then the morning/evening string of a 12-hour
// cycle or the suffix - those the spec asks for (all for 0).
void
TDate::TimeString(ULong spec, UniChar* str, ULong max)
{
	str[0] = 0;
	Boolean wantHour = false, wantMinute = false, wantSecond = false, wantAMPM = false, wantSuffix = false;
	if (spec == 0)
		wantHour = wantMinute = wantSecond = wantAMPM = wantSuffix = true;
	else
	{
		while (spec != 0)
		{
			ULong element = GetNextElementType(&spec);
			GetNextElementFormat(&spec);
			switch (element)
			{
			case kTimeElementHour:		wantHour = true;	break;
			case kTimeElementMinute:	wantMinute = true;	break;
			case kTimeElementSecond:	wantSecond = true;	break;
			case kTimeElementAMPM:		wantAMPM = true;	break;
			case kTimeElementSuffix:	wantSuffix = true;	break;
			}
		}
	}
	if ((Ref) fTimeFormat == NILREF)
	{
		RefVar locale(GetCurrentLocale());
		fTimeFormat = GetProtoVariable(locale, RSSYMtimeformat, nil);
	}
	Boolean twelveHour = RINT(GetProtoVariable(fTimeFormat, RSSYMtimecycle, nil)) == 1;
	if (twelveHour)
		wantSuffix = false;
	UniChar number[16];
	if (wantHour)
	{
		ULong hour = fHour;
		if (hour == 0)
		{
			long form = RINT(GetProtoVariable(fTimeFormat, RSSYMmidnightform, nil));
			if (form == 1)
				hour = 12;
			else if (form == 2)
				hour = 24;
		}
		else if (hour == 12)
		{
			if (twelveHour && RINT(GetProtoVariable(fTimeFormat, RSSYMnoonform, nil)) == 0)
				hour = 0;
		}
		else if (twelveHour && hour > 12)
			hour -= 12;
		if (hour < 10 && RINT(GetProtoVariable(fTimeFormat, RSSYMhourleadingz, nil)) == 0)
			Ustrncat(str, gZeroStr, max);
		IntegerString(hour, number);
		Ustrncat(str, number, max);
		if (wantMinute || wantSecond)
			AppendStringObject(str, RefVar(GetProtoVariable(fTimeFormat, RSSYMtimesepstr1, nil)), max);
	}
	if (wantMinute)
	{
		if (fMinute < 10 && RINT(GetProtoVariable(fTimeFormat, RSSYMminuteleadingz, nil)) == 0)
			Ustrncat(str, gZeroStr, max);
		IntegerString(fMinute, number);
		Ustrncat(str, number, max);
		if (wantSecond)
			AppendStringObject(str, RefVar(GetProtoVariable(fTimeFormat, RSSYMtimesepstr2, nil)), max);
	}
	if (wantSecond)
	{
		if (fSecond < 10 && RINT(GetProtoVariable(fTimeFormat, RSSYMsecondleadingz, nil)) == 0)
			Ustrncat(str, gZeroStr, max);
		IntegerString(fSecond, number);
		Ustrncat(str, number, max);
	}
	if (wantAMPM && twelveHour)
		AppendStringObject(str, RefVar(GetProtoVariable(fTimeFormat, fHour < 12 ? RSSYMmorningstr : RSSYMeveningstr, nil)), max);
	else if (wantSuffix)
		AppendStringObject(str, RefVar(GetProtoVariable(fTimeFormat, RSSYMsuffixstr, nil)), max);
}


// ROM 0x0008d958 DateElementString__5TDateFUlT1PUsT1Uc
// One element: the day (numeric, a leading zero when the format's
// dayLeadingZ is 0), the day of the week (the cached names of the
// format: 1 long, 2 abbreviated, 3 terse, 4 short, each falling back on
// the longer), the month (the names by the same formats, from the
// format frame - the long names the cached ones - or 5 numeric with the
// short format's monthLeadingZ), the year (numeric; the short form's
// last two digits when the short format's yearLeading is 1); then the
// element's suffix of the long or short format.
void
TDate::DateElementString(ULong element, ULong format, UniChar* str, ULong max, Boolean longForm)
{
	str[0] = 0;
	if ((Ref) fLongDateFormat == NILREF || (Ref) fShortDateFormat == NILREF)
	{
		RefVar locale(GetCurrentLocale());
		fLongDateFormat = GetProtoVariable(locale, RSSYMlongdateformat, nil);
		fShortDateFormat = GetProtoVariable(locale, RSSYMshortdateformat, nil);
	}
	RefVar theFormat(longForm ? fLongDateFormat : fShortDateFormat);
	RefVar names;
	RefVar suffix;
	UniChar number[16];
	switch (element)
	{
	case kDateElementDay:
		if (fDate < 10)
		{
			IntegerString(fDate, number);
			if (RINT(GetProtoVariable(theFormat, RSSYMdayleadingz, nil)) == 0)
				Ustrcpy(str, gZeroStr);
			Ustrncat(str, number, max);
		}
		else
			IntegerString(fDate, str);
		suffix = GetProtoVariable(theFormat, longForm ? RSSYMlongdaysuffix : RSSYMshortdaysuffix, nil);
		break;

	case kDateElementDayOfWeek:
		if (format == kElementFormatShort)
			names = gLocaleCache->fShortDayOfWeek;
		if ((Ref) names == NILREF && (format == kElementFormatShort || format == kElementFormatTerse))
			names = gLocaleCache->fTerseDayOfWeek;
		if ((Ref) names == NILREF && format >= kElementFormatAbbr && format <= kElementFormatShort)
			names = gLocaleCache->fAbbrDayOfWeek;
		if ((Ref) names == NILREF)
			names = gLocaleCache->fLongDayOfWeek;
		if ((Ref) names != NILREF)
			CopyStringObject(str, RefVar(GetArraySlotRef(names, fDayOfWeek)), max);
		break;

	case kDateElementMonth:
		switch (format)
		{
		case kElementFormatShort:
			names = GetProtoVariable(theFormat, RSSYMshortmonth, nil);
			if ((Ref) names != NILREF)
				break;
			// fall through
		case kElementFormatTerse:
			names = GetProtoVariable(theFormat, RSSYMtersemonth, nil);
			if ((Ref) names != NILREF)
				break;
			// fall through
		case kElementFormatAbbr:
			names = GetProtoVariable(theFormat, RSSYMabbrmonth, nil);
			if ((Ref) names != NILREF)
				break;
			// fall through
		case kElementFormatLong:
			names = gLocaleCache->fLongMonth;
			break;
		case kElementFormatNumeric:
			if (fMonth < 10)
			{
				IntegerString(fMonth, number);
				if (RINT(GetProtoVariable(fShortDateFormat, RSSYMmonthleadingz, nil)) == 0)
					Ustrncpy(str, gZeroStr, max);
				Ustrcat(str, number);
			}
			else
				IntegerString(fMonth, str);
			break;
		}
		if ((Ref) names != NILREF)
			CopyStringObject(str, RefVar(GetArraySlotRef(names, fMonth - 1)), max);
		suffix = GetProtoVariable(theFormat, longForm ? RSSYMlongmonthsuffix : RSSYMshortmonthsuffix, nil);
		break;

	case kDateElementYear:
		{
			IntegerString(fYear, number);
			long skip = 0;
			if (!longForm && RINT(GetProtoVariable(fShortDateFormat, RSSYMyearleading, nil)) == 1)
			{
				skip = Ustrlen(number) - 2;
				if (skip < 0)
					skip = 0;
			}
			Ustrncpy(str, number + skip, max);
			suffix = GetProtoVariable(theFormat, longForm ? RSSYMlongyearsuffix : RSSYMshortyearsuffix, nil);
		}
		break;

	default:
		break;
	}
	if ((Ref) suffix != NILREF)
		AppendStringObject(str, suffix, max);
}


/*------------------------------------------------------------------------------
	S t r i n g s   t o   d a t e s
	NOT YET RECONSTRUCTED: the fields are parsed with the locale's time and
	date lexical dictionaries (ParseString over gTimeLexDictionary and
	gDateLexDictionary, the recognition system's); nothing is parsed here.
------------------------------------------------------------------------------*/

// ROM 0x0008de6c StringToDateFields__5TDateFPCUsPUlUl
// The time then the date parsed out of the string (the later one first
// when they come in that order), a two-digit year put in the current
// century; *consumed the characters used.  ==> 0 all used, 2 some, -1
// none.
long
TDate::StringToDateFields(const UniChar* /*str*/, ULong* consumed, ULong /*length*/)
{
	fHour = fMinute = fSecond = -1;
	fYear = fMonth = fDate = fDayOfWeek = -1;
	*consumed = 0;
	return kDateParsedNone;
}


// ROM 0x0008e034 StringToDateFrame__5TDateFPCUsPUlUl
// A canonicalDate frame of the fields parsed (the year clipped to the
// last year), its status the parse's.
Ref
TDate::StringToDateFrame(const UniChar* str, ULong* consumed, ULong length)
{
	RefVar frame(Clone(RefVar(Rcanonicaldate)));
	long status = StringToDateFields(str, consumed, length);
	if (status == kDateParsedAll || status == kDateParsedSome)
	{
		SetFrameSlot(frame, RSSYMyear, RefVar(MAKEINT(YearInRange(fYear) ? fYear : kLastYear + 1)));
		if (fMonth != -1)
			SetFrameSlot(frame, RSSYMmonth, RefVar(MAKEINT(fMonth)));
		if (fDate != -1)
			SetFrameSlot(frame, RSSYMdate, RefVar(MAKEINT(fDate)));
		if (fHour != -1)
			SetFrameSlot(frame, RSSYMhour, RefVar(MAKEINT(fHour)));
		if (fMinute != -1)
			SetFrameSlot(frame, RSSYMminute, RefVar(MAKEINT(fMinute)));
		if (fSecond != -1)
			SetFrameSlot(frame, RSSYMsecond, RefVar(MAKEINT(fSecond)));
		if (fDayOfWeek != -1)
			SetFrameSlot(frame, RSSYMdayofweek, RefVar(MAKEINT(fDayOfWeek)));
	}
	SetFrameSlot(frame, RSSYMstatus, RefVar(MAKEINT(status)));
	return frame;
}


// ROM 0x0008e270 StringToDate__5TDateFPCUsPUlUl
// The fields parsed over the current time: what the string leaves out
// stays as now (a day of the week alone moves the date to it; a time
// without minutes has 0), then normalised.
long
TDate::StringToDate(const UniChar* str, ULong* consumed, ULong length)
{
	SetCurrentTime();
	long minute = fMinute;
	long date = fDate;
	long hour = fHour;
	long dayOfWeek = fDayOfWeek;
	long month = fMonth;
	long year = fYear;
	long status = StringToDateFields(str, consumed, length);
	if (status == kDateParsedAll || status == kDateParsedSome)
	{
		if (fDayOfWeek != -1 && fDate == -1)
			fDate = date + (fDayOfWeek + 7 - dayOfWeek) % 7;
		if (fYear == -1)
			fYear = year;
		if (fMonth == -1)
			fMonth = month;
		if (fDate == -1)
			fDate = date;
		Boolean noTime = fHour == -1 && fMinute == -1 && fSecond == -1;
		if (fHour == -1)
			fHour = hour;
		if (fMinute == -1)
			fMinute = noTime ? minute : 0;
		if (fSecond == -1)
			fSecond = 0;
		CleanUpFields();
	}
	return status;
}


// ROM 0x0008e384 StringToTime__5TDateFPCUsPUlUl
// The time parsed over today's date; everything 0 when nothing is.
long
TDate::StringToTime(const UniChar* /*str*/, ULong* consumed, ULong length)
{
	SetCurrentTime();
	fHour = 0;
	fMinute = 0;
	fSecond = 0;
	*consumed = 0;								// NOT YET RECONSTRUCTED: ParseString(gTimeLexDictionary, ...)
	if (*consumed == 0)
	{
		fYear = fMonth = fDate = fHour = fMinute = fSecond = fDayOfWeek = 0;
		return kDateParsedNone;
	}
	return *consumed < length ? kDateParsedSome : kDateParsedAll;
}


/*------------------------------------------------------------------------------
	F r a m e s
------------------------------------------------------------------------------*/

// ROM 0x0008a64c ToObject__FRC5TDate
// A canonicalDate frame {year, month, date, dayOfWeek, hour, minute,
// second, daysInMonth} of the date (a year out of range answers as the
// year after the last).
Ref
ToObject(const TDate& date)
{
	RefVar frame(Clone(RefVar(Rcanonicaldate)));
	long year = date.fYear;
	if (!YearInRange(year) || year == -1)
		year = kLastYear + 1;
	SetFrameSlot(frame, RSSYMyear, RefVar(MAKEINT(year)));
	SetFrameSlot(frame, RSSYMmonth, RefVar(MAKEINT(date.fMonth)));
	SetFrameSlot(frame, RSSYMdate, RefVar(MAKEINT(date.fDate)));
	SetFrameSlot(frame, RSSYMdayofweek, RefVar(MAKEINT(date.fDayOfWeek)));
	SetFrameSlot(frame, RSSYMhour, RefVar(MAKEINT(date.fHour)));
	SetFrameSlot(frame, RSSYMminute, RefVar(MAKEINT(date.fMinute)));
	SetFrameSlot(frame, RSSYMsecond, RefVar(MAKEINT(date.fSecond)));
	SetFrameSlot(frame, RSSYMdaysinmonth, RefVar(MAKEINT(date.DaysInMonth())));
	return frame;
}


// ROM 0x00089ef8 GetLongDateSlot__FRC6RefVar
static Ref
GetLongDateSlot(RefArg slot)
{
	RefVar format(GetLocaleSlot(RefVar(NILREF), RSSYMlongdateformat));
	return GetProtoVariable(format, slot, nil);
}


// ROM 0x00089f5c GetDayName__Fl
Ref
GetDayName(long dayOfWeek)
{
	RefVar names(GetLongDateSlot(RSSYMlongdofweek));
	return GetArraySlotRef(names, dayOfWeek);
}


// ROM 0x0008a364 WeekNumCalc__FlT1
// The week of the year the minutes fall in, weeks starting on
// firstDayOfWeek: the ISO way (a week belongs to the year holding its
// Thursday) when the locale's weekNumberType is 1, else counted from
// January 1st.
long
WeekNumCalc(long minutes, long firstDayOfWeek)
{
	TDate date(minutes);
	RefVar locale(GetCurrentLocale());
	Ref typeRef = GetProtoVariable(locale, RSSYMweeknumbertype, nil);
	Boolean iso = ISINT(typeRef) && RVALUE(typeRef) == 1;
	long weekBase = 1;
	if (iso)
	{
		long dayInWeek = (date.fDayOfWeek + 7 - firstDayOfWeek) % 7;
		minutes += (3 - dayInWeek) * kMinutesPerDay;			// to the week's Thursday
		date.InitWithMinutes(minutes);
	}
	TDate first;
	first.fYear = date.fYear;
	first.fMonth = 1;
	first.fDate = 1;
	long firstMinutes = first.TotalMinutes();
	first.InitWithMinutes(firstMinutes);
	long firstDayInWeek = (first.fDayOfWeek + 7 - firstDayOfWeek) % 7;
	if (iso && firstDayInWeek > 3)
		weekBase = 0;
	long days = (minutes - firstMinutes) / kMinutesPerDay;
	return (days + firstDayInWeek) / 7 + weekBase;
}


/*------------------------------------------------------------------------------
	T h e   n a t i v e s
------------------------------------------------------------------------------*/

// The minutes an argument names: an integer, else now.
static ULong
MinutesArg(RefArg arg)
{
	return ISINT(arg) ? (ULong) RVALUE(arg) : RealClock();
}


// ROM 0x00089b4c FTime__FRC6RefVar
static Ref
FTime(RefArg /*rcvr*/)
{
	return MAKEINT(RealClock());
}


// ROM 0x00089b64 FTimeInSeconds__FRC6RefVar
Ref
FTimeInSeconds(RefArg /*rcvr*/)
{
	return MAKEINT((long) (Long32) (RealClockSeconds() - kSecondsFrom1904To1993));
}


// ROM 0x00089c5c FTicks__FRC6RefVar
static Ref
FTicks(RefArg /*rcvr*/)
{
	return MAKEINT(Ticks());
}


// ROM 0x00255548 FSleep
// Sleep(ticks): the task waits that many sixtieths of a second (Wait,
// os600/user/NewtonTime.h - a send to the null port with the timeout).
// The time and date setters call it between the steps of a held arrow,
// which is what paces them.  ==> nil.
static Ref
FSleep(RefArg /*rcvr*/, RefArg ticks)
{
	Wait(RINT(ticks));
	return NILREF;
}


// ROM 0x00089b88 FIsValidDate__FRC6RefVarT1
// A string parsed, or a date frame's fields.
static Ref
FIsValidDate(RefArg /*rcvr*/, RefArg date)
{
	TDate theDate;
	Boolean valid = false;
	if (IsString(date))
	{
		ULong consumed;
		theDate.StringToDateFrame(GetCString(date), &consumed, (ULong) -1);
		valid = theDate.IsValidDate();
	}
	else if (IsFrame(date))
	{
		theDate.InitWithDateFrame(date, false);
		valid = theDate.IsValidDate();
	}
	return MAKEBOOLEAN(valid);
}


// ROM 0x00089c74 FStringToTime__FRC6RefVarT1
static Ref
FStringToTime(RefArg /*rcvr*/, RefArg str)
{
	ULong length = (Length(str) - 2) >> 1;
	if (length == 0)
		return NILREF;
	TDate date;
	ULong consumed;
	long status = date.StringToTime(GetCString(str), &consumed, length);
	if (status == kDateParsedAll || status == kDateParsedSome)
		return MAKEINT(date.TotalMinutes());
	return NILREF;
}


// ROM 0x00089d38 FHourMinute__FRC6RefVarT1
// The time (now for nil) in the short time format.
static Ref
FHourMinute(RefArg /*rcvr*/, RefArg minutes)
{
	ULong spec = RINT(GetFrameSlotRef(Rdatetimestrspecs, RSSYMshorttimestrspec));
	UniChar str[64];
	TimeString(MinutesArg(minutes), spec, str, 63, RefVar(NILREF));
	return MakeString(str);
}


// ROM 0x00089dec FDateNTime__FRC6RefVarT1
// The date (every element, short) and the short time.
static Ref
FDateNTime(RefArg /*rcvr*/, RefArg minutes)
{
	ULong spec = RINT(GetFrameSlotRef(Rdatetimestrspecs, RSSYMshorttimestrspec));
	ULong when = MinutesArg(minutes);
	UniChar str[64];
	UniChar time[64];
	ShortDateString(when, 0, str, 63, RefVar(NILREF));
	TimeString(when, spec, time, 63, RefVar(NILREF));
	Ustrcat(str, gSpaceStr);
	Ustrncat(str, time, 63);
	return MakeString(str);
}


// ROM 0x00089fa0 FShortDate__FRC6RefVarT1
// The abbreviated day of the week, then the month and day.
static Ref
FShortDate(RefArg /*rcvr*/, RefArg minutes)
{
	ULong when = MinutesArg(minutes);
	ULong daySpec = RINT(GetFrameSlotRef(Rdatetimestrspecs, RSSYMabbrdayofweekstrspec));
	ULong dateSpec = RINT(GetFrameSlotRef(Rdatetimestrspecs, RSSYMmonthdaystrspec));
	UniChar str[64];
	UniChar date[64];
	LongDateString(when, daySpec, str, 63, RefVar(NILREF));
	ShortDateString(when, dateSpec, date, 63, RefVar(NILREF));
	Ustrcat(str, gSpaceStr);
	Ustrncat(str, date, 63);
	return MakeString(str);
}


// ROM 0x0008a0dc FLongDateStr__FRC6RefVarN21
static Ref
FLongDateStr(RefArg /*rcvr*/, RefArg minutes, RefArg spec)
{
	UniChar str[64];
	LongDateString(MinutesArg(minutes), RINT(spec), str, 63, RefVar(NILREF));
	return MakeString(str);
}


// ROM 0x0008a174 FShortDateStr__FRC6RefVarN21
static Ref
FShortDateStr(RefArg /*rcvr*/, RefArg minutes, RefArg spec)
{
	UniChar str[64];
	ShortDateString(MinutesArg(minutes), RINT(spec), str, 63, RefVar(NILREF));
	return MakeString(str);
}


// ROM 0x0008a248 FTimeStr__FRC6RefVarN21
static Ref
FTimeStr(RefArg /*rcvr*/, RefArg minutes, RefArg spec)
{
	UniChar str[64];
	TimeString(MinutesArg(minutes), RINT(spec), str, 63, RefVar(NILREF));
	return MakeString(str);
}


// ROM 0x0008a2e0 FTimeFrameStr
static Ref
FTimeFrameStr(RefArg /*rcvr*/, RefArg dateFrame, RefArg spec)
{
	if (!IsFrame(dateFrame))
		return NILREF;
	UniChar str[64];
	TimeFrameString(dateFrame, RINT(spec), str, 63, RefVar(NILREF));
	return MakeString(str);
}


// ROM 0x0008a20c FSetTimeInSeconds
static Ref
FSetTimeInSeconds(RefArg /*rcvr*/, RefArg seconds)
{
	SetRealClockSeconds((ULong) RINT(seconds) + kSecondsFrom1904To1993);
	return NILREF;
}


// ROM 0x0008a5c4 FSetTime__FRC6RefVarT1
static Ref
FSetTime(RefArg /*rcvr*/, RefArg minutes)
{
	SetRealClock(RINT(minutes));
	return NILREF;
}


// ROM 0x0008a4dc FWeekNumber
// The week number of the minutes, weeks starting on firstDayOfWeek (nil:
// the firstDayOfWeek preference, else the locale's, else Sunday).
static Ref
FWeekNumber(RefArg /*rcvr*/, RefArg minutes, RefArg firstDayOfWeek)
{
	long first = 0;
	if ((Ref) firstDayOfWeek == NILREF)
	{
		RefVar pref(GetPreference(RSSYMfirstdayofweek));
		if ((Ref) pref == NILREF)
		{
			RefVar locale(GetCurrentLocale());
			pref = GetProtoVariable(locale, RSSYMfirstdayofweek, nil);
		}
		if ((Ref) pref != NILREF)
			first = RINT(pref);
	}
	else
		first = RINT(firstDayOfWeek);
	return MAKEINT(WeekNumCalc(RINT(minutes), first));
}


// ROM 0x0008a5f4 FTotalMinutes__FRC6RefVarT1
static Ref
FTotalMinutes(RefArg /*rcvr*/, RefArg dateFrame)
{
	TDate date;
	date.InitWithDateFrame(dateFrame, true);
	return MAKEINT(date.TotalMinutes());
}


// ROM 0x0008a7fc FDate__FRC6RefVarT1
static Ref
FDate(RefArg /*rcvr*/, RefArg minutes)
{
	TDate date;
	date.InitWithMinutes(RINT(minutes));
	return ToObject(date);
}


// ROM 0x0008a868 FDateFromSeconds
static Ref
FDateFromSeconds(RefArg /*rcvr*/, RefArg seconds)
{
	TDate date;
	date.InitWithSeconds((ULong) RINT(seconds) + kSecondsFrom1904To1993);
	return ToObject(date);
}


// ROM 0x0008a8dc FStringToDateFrame__FRC6RefVarT1
static Ref
FStringToDateFrame(RefArg /*rcvr*/, RefArg str)
{
	ULong length = (Length(str) - 2) >> 1;
	if (length == 0)
		return NILREF;
	TDate date;
	ULong consumed;
	return date.StringToDateFrame(GetCString(str), &consumed, length);
}


// ROM 0x0008a988 FStringToDate__FRC6RefVarT1
static Ref
FStringToDate(RefArg /*rcvr*/, RefArg str)
{
	ULong length = (Length(str) - 2) >> 1;
	if (length == 0)
		return NILREF;
	TDate date;
	ULong consumed;
	long status = date.StringToDate(GetCString(str), &consumed, length);
	if (status == kDateParsedAll || status == kDateParsedSome)
		return MAKEINT(date.TotalMinutes());
	return NILREF;
}


// ROM 0x0008caa8 FIncrementMonth__FRC6RefVarN21
// The minutes delta months on; nil when the result would be out of the
// integer's range, the largest integer when past 2^29 minutes.
static Ref
FIncrementMonth(RefArg /*rcvr*/, RefArg minutes, RefArg delta)
{
	long when = RINT(minutes);
	long months = RINT(delta);
	if (when + months < 0)
		return NILREF;
	if (when + months >= 0x1fffffff)
		return MAKEINT(0x1ffffffe);
	TDate date((ULong) when);
	date.IncrementMonth(months);
	return MAKEINT(date.TotalMinutes());
}


// Host: canonicalDate when the ROM's objects are not imported: the
// frame FDate and StringToDateFrame clone.
void
InitDatePrototypes(void)
{
	if (Rcanonicaldate != NILREF)
		return;
	AddGCRoot(Rcanonicaldate);
	RefVar frame(AllocateFrame());
	Ref tags[] = { RSSYMyear, RSSYMmonth, RSSYMdate, RSSYMdayofweek, RSSYMhour, RSSYMminute, RSSYMsecond, RSSYMdaysinmonth };
	for (long i = 0; i < 8; i++)
		SetFrameSlot(frame, RefVar(tags[i]), RefVar(NILREF));
	Rcanonicaldate = frame;
}


void
RegisterDateNatives(void)
{
	RegisterRepeatTextNatives();
	RegisterNativeFunction("FTime__FRC6RefVar", (void*) FTime, 0);
	RegisterNativeFunction("FTimeInSeconds__FRC6RefVar", (void*) FTimeInSeconds, 0);
	RegisterNativeFunction("FTicks__FRC6RefVar", (void*) FTicks, 0);
	RegisterNativeFunction("FSleep", (void*) FSleep, 1);
	RegisterNativeFunction("FIsValidDate__FRC6RefVarT1", (void*) FIsValidDate, 1);
	RegisterNativeFunction("FStringToTime__FRC6RefVarT1", (void*) FStringToTime, 1);
	RegisterNativeFunction("FHourMinute__FRC6RefVarT1", (void*) FHourMinute, 1);
	RegisterNativeFunction("FDateNTime__FRC6RefVarT1", (void*) FDateNTime, 1);
	RegisterNativeFunction("FShortDate__FRC6RefVarT1", (void*) FShortDate, 1);
	RegisterNativeFunction("FLongDateStr__FRC6RefVarN21", (void*) FLongDateStr, 2);
	RegisterNativeFunction("FShortDateStr__FRC6RefVarN21", (void*) FShortDateStr, 2);
	RegisterNativeFunction("FTimeStr__FRC6RefVarN21", (void*) FTimeStr, 2);
	RegisterNativeFunction("FTimeFrameStr", (void*) FTimeFrameStr, 2);
	RegisterNativeFunction("FSetTimeInSeconds", (void*) FSetTimeInSeconds, 1);
	RegisterNativeFunction("FSetTime__FRC6RefVarT1", (void*) FSetTime, 1);
	RegisterNativeFunction("FWeekNumber", (void*) FWeekNumber, 2);
	RegisterNativeFunction("FTotalMinutes__FRC6RefVarT1", (void*) FTotalMinutes, 1);
	RegisterNativeFunction("FDate__FRC6RefVarT1", (void*) FDate, 1);
	RegisterNativeFunction("FDateFromSeconds", (void*) FDateFromSeconds, 1);
	RegisterNativeFunction("FStringToDateFrame__FRC6RefVarT1", (void*) FStringToDateFrame, 1);
	RegisterNativeFunction("FStringToDate__FRC6RefVarT1", (void*) FStringToDate, 1);
	RegisterNativeFunction("FIncrementMonth__FRC6RefVarN21", (void*) FIncrementMonth, 2);
	InitDatePrototypes();
}
