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
	0x0008c664-0x0008e8cc); each function cites its origin.  A date
	or time is read out of a string through the locale's lexical
	dictionaries (LexParse.h).  The meeting and repeat functions are Meetings.h.
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

	long		StringToDateFields(const UniChar* str, ULong* consumed, ULong length);
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
	// The three format frames are RefStructs, not RefVars: the ROM's
	// constructor writes 0 into each handle's stack position, so
	// ClearRefHandles leaves them alone.  It has to - a TDate is
	// embedded in a view (TMonthView's fDate), which outlives the
	// event that made it, and a freed handle written through later
	// corrupts the free chain.  There is no ~TDate in the ROM, so
	// the three handles are leaked; see docs/curiosities.md.
	RefStruct	fLongDateFormat;	// +0x1c
	RefStruct	fShortDateFormat;	// +0x20
	RefStruct	fTimeFormat;		// +0x24
};

Boolean	operator<(const TDate& a, const TDate& b);
Boolean	operator>(const TDate& a, const TDate& b);
Boolean	operator==(const TDate& a, const TDate& b);

Ref		ToObject(const TDate& date);			// a canonicalDate frame
Ref		GetDayName(long dayOfWeek);
Ref		FTimeInSeconds(RefArg rcvr);		// TimeInSeconds(): the seconds since 1993
long	WeekNumCalc(long minutes, long firstDayOfWeek);

void	RegisterDateNatives(void);
void	RegisterRepeatTextNatives(void);		// RepeatText.cpp: RepeatInfoToText (registered by RegisterDateNatives)
Ref		FRepeatInfoToText(RefArg rcvr, RefArg pattern, RefArg kind, RefArg time);	// ROM 0x00121ebc FRepeatInfoToText__FRC6RefVarN31
void	InitDatePrototypes(void);				// host: Rcanonicaldate when no ROM objects are imported

// DEVIATION (the owner's decision, 2026-09-30): the year-2010 fix
// (docs/intl/year-2010.md).  A script's seconds (TimeInSeconds' count from
// 1993, a 30-bit NewtonScript integer that wrapped on 5 January 2010) are
// read back as the time within 2^29 seconds of now that they stand for,
// rather than as seconds after 1993.  NEWTON_ROM_2010_BUG=1 keeps the
// ROM's own arithmetic.
Boolean	Fix2010(void);							// the fix is in force
void	SetFix2010(Boolean inForce);			// (tests: in force or not, whatever NEWTON_ROM_2010_BUG says)
ULong	ClockSecondsFromScriptSeconds(Long seconds);	// the real-clock second (from 1904) a script's second stands for
#if NEWTON_NS64
Boolean	NS64DeviceTime(void);					// NEWTON_NS64_TIME=device: TimeInSeconds wrapped as the device's (docs/frames/64bit.md)
#endif
void	InstallFix2010(void);					// the script functions that read seconds back, replaced (after InitScriptGlobals)

#endif	/* __DATES_H */
