/*
	File:		intl/RepeatText.cpp

	Contains:	RepeatInfoToText - a repeating meeting's pattern said in
				words ("Every Monday and Thursday", "The second and last
				Friday of every month", "Every other Tuesday, March 4,
				1997") - and the packing of the pattern it reads.

				A pattern (the Dates application's mtgInfo) is packed into
				one integer, read differently for each kind of repeat:

				  - the days of the week are bits 0x800 (Sunday) down to
				    0x20 (Saturday);
				  - the weeks of the month are bits 0x10 (the first) down
				    to 0x01 (the last);
				  - the dates of the month are six bits each, the first in
				    the low six;
				  - a date in the year is its month in bits 8-15 and its
				    day in bits 0-7;
				  - every other week counts days since 1904 above bit 8.

				The phrases themselves are the ROM's array of repeat texts
				(magic pointer 24: "Every day", "first" ... "last", "Every
				^0 and ^1", ...), filled in by ParamString.

	Reconstructed from the MP2x00 US ROM (0x00120f60-0x00121ebc); each
	function cites its origin.
*/

#include "Dates.h"
#include "Locale.h"
#include "NumberFormat.h"
#include "ObjectHeap.h"
#include "Frames.h"
#include "NativeFunctions.h"
#include "RSSymbols.h"
#include "Unicode.h"

#include <string.h>


// the ROM's array of repeat phrases (magic pointer 24)
static const Ref kRepeatTexts = MAKEMAGICPTR(24);
// the ROM's date format specs frame (magic pointer 66)
static const Ref kDateSpecs = MAKEMAGICPTR(66);

// the phrases' indexes
enum
{
	kEveryDay = 0,
	kEveryOther = 6,		// "Every other ^0"
	kEveryOneDay = 7,		// "Every ^0" .. 12: "Every ^0, ^1, ^2, ^3, ^4, and ^5"
	kWeekInMonth = 13,		// "The ^0 ^1 of every month" .. 16: four weeks
	kDateInMonth = 17,		// "The ^0 of every month"
	kDateInYear = 18,		// "Every ^0"
	kWeekInYear = 19		// "The ^0 ^1 of every ^2"
};

const long kRepeatTextMax = 99;			// the ROM's limit on the text (ParamString's max)


// ROM 0x00120f60 UnPackDates__FPll
// The dates of the month a pattern holds: six bits each from the low end,
// up to five; ==> how many (at least one, even of nought).
long
UnPackDates(long* dates, long pattern)
{
	long i = 0;
	do
	{
		dates[i] = pattern & 0x3f;
		pattern >>= 6;
		if (pattern == 0)
			break;
		i++;
	} while (i < 5);
	return i + 1;
}


// ROM 0x001210dc UnPackWeeks__FPll
// The weeks of the month a pattern holds, 1 (bit 0x10) to 5, the last
// (bit 0x01); ==> how many.
long
UnPackWeeks(long* weeks, long pattern)
{
	long n = 0;
	if (pattern & 0x10)
		weeks[n++] = 1;
	if (pattern & 0x08)
		weeks[n++] = 2;
	if (pattern & 0x04)
		weeks[n++] = 3;
	if (pattern & 0x02)
		weeks[n++] = 4;
	if (pattern & 0x01)
		weeks[n++] = 5;
	return n;
}


// ROM 0x00121158 UnPackDays__FPll
// The days of the week a pattern holds, 0 (Sunday, bit 0x800) to 6
// (Saturday, bit 0x20); ==> how many.
long
UnPackDays(long* days, long pattern)
{
	long n = 0;
	ULong bit = 0x800;
	for (long day = 0; day < 7; day++, bit >>= 1)
		if (pattern & bit)
			days[n++] = day;
	return n;
}


// ROM 0x00121190 CalendarString__Fl
// One of the repeat phrases.  (The ROM answers its characters; the host
// answers the string, and its characters are taken just before they are
// used, the object heap moving objects when it allocates.)
static Ref
CalendarString(long index)
{
	return GetArraySlotRef(RefVar(kRepeatTexts), index);
}


// ROM 0x001211dc DayMaskToIndex__Fl
// The day of the week a single day bit is (bits 0x800 down to 0x20).
long
DayMaskToIndex(long mask)
{
	long day = 6;
	for (long bits = mask >> 6; bits != 0; bits >>= 1)
		day--;
	return day;
}


// ROM 0x00121200 DayName__Fl
// A day of the week's name.  (As CalendarString: the string.)
static Ref
DayName(long day)
{
	return GetDayName(day);
}


// ParamString over strings: each one's characters, taken at the call.
static void
ParamStrings(UniChar* dest, RefArg proto, const RefVar* args, long count)
{
	const UniChar* s[6];
	static const UniChar kEmpty[1] = { 0 };
	for (long i = 0; i < 6; i++)
		s[i] = (i < count && NOTNIL(args[i])) ? GetCString(args[i]) : kEmpty;
	ParamString(dest, kRepeatTextMax, GetCString(proto), s[0], s[1], s[2], s[3], s[4], s[5]);
}


// ROM 0x00121238 EveryDayString__FlPUs
// "Every day", or "Every Monday", "Every Monday and Thursday", ... up to
// six days.
void
EveryDayString(long pattern, UniChar* text)
{
	long days[7];
	long count = UnPackDays(days, pattern);
	if (count == 7)
	{
		RefVar all(CalendarString(kEveryDay));
		Ustrcat(text, GetCString(all));
		return;
	}
	if (count == 0)
		return;
	RefVar names[6];
	for (long i = 0; i < count; i++)
		names[i] = DayName(days[i]);
	ParamStrings(text, RefVar(CalendarString(kEveryOneDay + count - 1)), names, count);
}


// ROM 0x001214a8 WeekInMonthString__FlPUs
// "The second Friday of every month", "The first and last Friday of every
// month", ... - one day in some of the weeks; every day of those weeks is
// EveryDayString's.
void
WeekInMonthString(long pattern, UniChar* text)
{
	long day = DayMaskToIndex(pattern & 0xfe0);
	if ((pattern & 0x1f) == 0x1f)
	{
		EveryDayString(pattern, text);
		return;
	}
	long weeks[5];
	long count = UnPackWeeks(weeks, pattern);
	if (count == 0 || count > 4)
		return;
	// the weeks' ordinals, then the day
	RefVar words[5];
	for (long i = 0; i < count; i++)
		words[i] = CalendarString(weeks[i]);
	words[count] = DayName(day);
	ParamStrings(text, RefVar(CalendarString(kWeekInMonth + count - 1)), words, count + 1);
}


// ROM 0x00121670 EveryOtherWeekString__FlPUs
// "Every other Tuesday, March 4, 1997": the day the repeat started from
// (days since 1904 above bit 8) in the long day-of-week form.
void
EveryOtherWeekString(long pattern, UniChar* text)
{
	TDate date;
	date.InitWithMinutes((ULong) ((pattern >> 8) * 1440));
	ULong spec = (ULong) RINT(GetFrameSlotRef(RefVar(kDateSpecs), RSSYMlongdayofweekstrspec));
	UniChar dateText[100];
	dateText[0] = 0;
	LongDateString((ULong) date.TotalMinutes(), spec, dateText, 100, RefVar(NILREF));
	RefVar proto(CalendarString(kEveryOther));
	ParamString(text, kRepeatTextMax, GetCString(proto), dateText);
}


// ROM 0x00121754 DateInYearString__FlPUsT1
// "Every March 4": the month (bits 8-15) and day (0-7) in the year given,
// in the month-and-day form.
void
DateInYearString(long pattern, UniChar* text, long year)
{
	ULong spec = (ULong) RINT(GetFrameSlotRef(RefVar(kDateSpecs), RSSYMmonthdaystrspec));
	TDate date;
	date.fYear = year;
	date.fMonth = ((ULong) pattern >> 8) & 0xff;
	date.fDate = pattern & 0xff;
	UniChar dateText[100];
	dateText[0] = 0;
	LongDateString((ULong) date.TotalMinutes(), spec, dateText, 100, RefVar(NILREF));
	RefVar proto(CalendarString(kDateInYear));
	ParamString(text, kRepeatTextMax, GetCString(proto), dateText);
}


// ROM 0x00121cfc WeekInYearString__FlPUs
// "The first Monday of every September": a day (bits 0x800-0x20) in a week
// of the month (the first week bit) of a month (bits 12-15).
void
WeekInYearString(long pattern, UniChar* text)
{
	long day = DayMaskToIndex(pattern & 0xfe0);
	long weeks[5];
	UnPackWeeks(weeks, pattern);
	TDate date;
	date.fMonth = (pattern >> 12) & 0xf;
	UniChar month[22];
	month[0] = 0;
	date.DateElementString(3, 1, month, 0x14, true);
	RefVar name(DayName(day));
	RefVar week(CalendarString(weeks[0]));
	RefVar proto(CalendarString(kWeekInYear));
	ParamString(text, kRepeatTextMax, GetCString(proto), GetCString(week), GetCString(name), month);
}


// ROM 0x00121dc8 DatesInMonthString__FlPUs
// "The 15th of every month": the first date of the pattern as the system
// locale's short ordinal.
void
DatesInMonthString(long pattern, UniChar* text)
{
	RefVar international(GetProtoVariable(RefVar(gVarFrame), RSSYMinternational, nil));
	RefVar bundle(GetProtoVariable(international, RSSYMsystemlocalebundle, nil));
	RefVar ordinals(GetProtoVariable(bundle, RSSYMshortordinals, nil));
	long dates[5];
	UnPackDates(dates, pattern);
	RefVar ordinal(GetArraySlotRef(ordinals, dates[0]));
	RefVar proto(CalendarString(kDateInMonth));
	ParamString(text, kRepeatTextMax, GetCString(proto), GetCString(ordinal));
}


// ROM 0x00121ebc FRepeatInfoToText__FRC6RefVarN31
// RepeatInfoToText(pattern, kind, time): the pattern of a repeating meeting
// in words, by its kind - 0 days of the week, 1 a day in weeks of the
// month, 2 dates of the month, 3 a date in the year (in the year of time,
// in minutes), 4 every other week, 7 a day in a week of a month of the
// year; 5 and 6 (and anything else) answer the empty string.
Ref
FRepeatInfoToText(RefArg /*rcvr*/, RefArg pattern, RefArg kind, RefArg time)
{
	long info = RINT(pattern);
	long type = RINT(kind);
	UniChar text[kRepeatTextMax + 1];
	text[0] = 0;
	switch (type)
	{
	case 0:
		EveryDayString(info, text);
		break;
	case 1:
		WeekInMonthString(info, text);
		break;
	case 2:
		DatesInMonthString(info, text);
		break;
	case 3:
		{
			TDate date;
			date.InitWithMinutes((ULong) RINT(time));
			DateInYearString(info, text, date.fYear);
		}
		break;
	case 4:
		EveryOtherWeekString(info, text);
		break;
	case 7:
		WeekInYearString(info, text);
		break;
	}
	return MakeString(text);
}


void
RegisterRepeatTextNatives(void)
{
	RegisterNativeFunction("FRepeatInfoToText__FRC6RefVarN31", (void*) FRepeatInfoToText, 3);
}
