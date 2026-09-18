/*
	File:		intl/Dates.h

	Contains:	TDate, a calendar date and time of day: the fields of a
				moment (year, month, date, hour, minute, second, day of week,
				Sunday 0) and the moment as minutes (or seconds, hours, days)
				since the start of 1904, the Newton's epoch; formatted with a
				locale's long date, short date and time formats (Locale.h)
				under a format spec (Rdatetimestrspecs: a spec is a run of
				3-bit element types each followed by a 3-bit format: date
				elements 1 day, 2 day of week, 3 month, 4 year with formats
				1 long, 2 abbreviated, 3 terse, 4 short, 5 numeric; time
				elements 1 hour, 2 minute, 3 second, 4 AM/PM, 5 the suffix;
				a spec of 0 is every element).  The NewtonScript date
				functions (Time, Date, DateNTime, TimeStr, LongDateStr,
				TotalMinutes, ...) are here too.

	The ROM's layout: TDate 0x28 (seven longs and three RefVars: the
	format frames).

	Reconstructed from the MP2x00 US ROM (0x00089ad0-0x0008aa4c,
	0x0008c664-0x0008e8cc); each function cites its origin.  NOT YET
	RECONSTRUCTED: reading a date or time out of a string
	(StringToDateFields: the recognition system's lexical dictionaries).
	The meeting and repeat functions are Meetings.h.
*/

#ifndef __DATES_H
#define __DATES_H

#ifndef __OBJECTS_H
#include "objects.h"
#endif

const long kEpochYear = 1904;
const long kLastYear = 2919;				// the last year TDate handles (0xb60 + 7)
const long kMinutesPerDay = 1440;
const ULong kSecondsFrom1904To1993 = 0xa7693a00;	// TimeInSeconds counts seconds from the start of 1993

// the date element types and formats of a spec
enum
{
	kDateElementNone = 0, kDateElementDay, kDateElementDayOfWeek, kDateElementMonth, kDateElementYear, kDateElementDayOfYear,
	kTimeElementHour = 1, kTimeElementMinute, kTimeElementSecond, kTimeElementAMPM, kTimeElementSuffix
};
enum
{
	kElementFormatDefault = 0, kElementFormatLong, kElementFormatAbbr, kElementFormatTerse, kElementFormatShort, kElementFormatNumeric
};

// a date frame's status from StringToDateFrame
const long kDateParsedAll = 0;
const long kDateParsedNone = -1;
const long kDateParsedSome = 2;

class TDate
{
public:
				TDate();
				TDate(ULong minutes);
				TDate(const UniChar* str, ULong* consumed, ULong length);

	void		InitWithMinutes(ULong minutes);
	void		InitWithSeconds(ULong seconds);
	void		InitWithDateFrame(RefArg frame, Boolean fillIn);		// fillIn: missing slots get 1904/1/1 0:00:00, else -1
	void		SetCurrentTime(void);
	void		CleanUpFields(void);									// the fields normalised through the minutes

	long		TotalDays(void) const;
	long		TotalHours(void) const;
	long		TotalMinutes(void) const;
	long		TotalSeconds(void) const;
	long		DaysInMonth(void) const;
	long		DaysInYear(void) const;									// the day of the year
	Boolean		IsValidDate(void) const;
	void		IncrementMonth(long delta);

	void		SetFormatResource(RefArg locale);						// the formats of the locale (nil: the current)
	void		SetFormatResource(RefArg longDateFormat, RefArg shortDateFormat, RefArg timeFormat);
	void		LongDateString(ULong spec, UniChar* str, ULong max);
	void		ShortDateString(ULong spec, UniChar* str, ULong max);
	void		TimeString(ULong spec, UniChar* str, ULong max);
	void		DateElementString(ULong element, ULong format, UniChar* str, ULong max, Boolean longForm);

	long		StringToDateFields(const UniChar* str, ULong* consumed, ULong length);	// NOT YET: kDateParsedNone
	Ref			StringToDateFrame(const UniChar* str, ULong* consumed, ULong length);
	long		StringToDate(const UniChar* str, ULong* consumed, ULong length);
	long		StringToTime(const UniChar* str, ULong* consumed, ULong length);

	long		fYear;				// +0x00
	long		fMonth;				// +0x04  1..12
	long		fDate;				// +0x08  1..31
	long		fHour;				// +0x0c
	long		fMinute;			// +0x10
	long		fSecond;			// +0x14
	long		fDayOfWeek;			// +0x18  0 Sunday
	RefVar		fLongDateFormat;	// +0x1c
	RefVar		fShortDateFormat;	// +0x20
	RefVar		fTimeFormat;		// +0x24
};

Boolean	operator<(const TDate& a, const TDate& b);
Boolean	operator>(const TDate& a, const TDate& b);
Boolean	operator==(const TDate& a, const TDate& b);

Ref		ToObject(const TDate& date);			// a canonicalDate frame
Ref		GetDayName(long dayOfWeek);
long	WeekNumCalc(long minutes, long firstDayOfWeek);

void	RegisterDateNatives(void);
void	InitDatePrototypes(void);				// host: Rcanonicaldate when no ROM objects are imported

#endif	/* __DATES_H */
