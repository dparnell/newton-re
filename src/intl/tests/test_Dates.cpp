// Dates test (src/intl/Dates.h, Locale.h): TDate's calendar arithmetic
// (minutes since 1904 to fields and back, leap years, days in a month,
// months incremented), and the date and time strings formatted with the
// German locale bundle of the MP2100 D ROM (the ROM's objects imported,
// vars.international pointing at the bundle as the boot script would),
// through TDate and the NewtonScript functions (Date, DateNTime,
// LongDateStr, ShortDateStr, TimeStr, HourMinute, ShortDate,
// TotalMinutes, WeekNumber, IncrementMonth, IsValidDate).

#include "Dates.h"
#include "Locale.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "ROMImport.h"
#include "ROMConstants.h"
#include "Compiler.h"
#include "Interpreter.h"
#include "RSSymbols.h"
#include "Unicode.h"
#include "NewtonTime.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static Ref SYMBOL(const char* name) { return Intern((char*) name); }

const ULong kGermanyBundle = 0x003c10ed;		// the ROM's locale bundle 'Germany (nsfunctions.py --object 0x3c10ed)
const long kWednesday3Oct1990 = 45630125;		// 14:05, minutes since 1904


static Ref
Eval(const char* source)
{
	RefVar fn(ParseString(RefVar(MakeString(source))));
	return InterpretBlock(fn, RefVar(gVarFrame));
}


static Boolean
UStringIs(const UniChar* str, const char* text)
{
	for (; *text != 0; text++, str++)
		if (*str != (UniChar) (unsigned char) *text)
			return false;
	return *str == 0;
}


static Boolean
StringIs(RefArg str, const char* text)
{
	return IsString(str) && UStringIs(GetCString(str), text);
}


static void
PrintU(const char* label, const UniChar* str)
{
	printf("%s: ", label);
	for (; *str != 0; str++)
		printf("%c", *str < 0x100 ? (char) *str : '?');
	printf("\n");
}


static void
TestCalendar()
{
	// the epoch: Friday, 1 January 1904
	TDate epoch((ULong) 0);
	EXPECT(epoch.fYear == 1904 && epoch.fMonth == 1 && epoch.fDate == 1 && epoch.fHour == 0 && epoch.fMinute == 0 && epoch.fDayOfWeek == 5);
	EXPECT(epoch.TotalMinutes() == 0 && epoch.TotalDays() == 0);
	// Wednesday, 3 October 1990, 14:05
	TDate date((ULong) kWednesday3Oct1990);
	EXPECT(date.fYear == 1990 && date.fMonth == 10 && date.fDate == 3 && date.fHour == 14 && date.fMinute == 5 && date.fSecond == 0 && date.fDayOfWeek == 3);
	EXPECT(date.TotalMinutes() == kWednesday3Oct1990);
	EXPECT(date.TotalHours() == kWednesday3Oct1990 / 60 && date.TotalDays() == kWednesday3Oct1990 / 1440);
	EXPECT(date.DaysInMonth() == 31 && date.DaysInYear() == 276);
	EXPECT(date.IsValidDate());
	// seconds
	TDate withSeconds;
	withSeconds.InitWithSeconds((ULong) kWednesday3Oct1990 * 60 + 42);
	EXPECT(withSeconds.fSecond == 42 && withSeconds.fMinute == 5 && (ULong32) withSeconds.TotalSeconds() == (ULong32) ((unsigned long long) kWednesday3Oct1990 * 60 + 42));
	EXPECT(!(withSeconds > date) && !(date < withSeconds) && !(date == withSeconds));	// the order is by the minute, equality by the second
	withSeconds.fSecond = 0;
	EXPECT(date == withSeconds);
	TDate later(kWednesday3Oct1990 + 1);
	EXPECT(later > date && date < later && !(later == date));
	// every day of a few years round-trips
	for (long day = 0; day < 366 * 12; day += 7)
	{
		TDate d((ULong) day * 1440 + 123);
		EXPECT(d.TotalMinutes() == day * 1440 + 123);
		EXPECT(d.fDayOfWeek == (day + 5) % 7);
		EXPECT(d.IsValidDate());
	}
	// leap years: 1904, 1996, 2000 are; 1900 (out of range) counts as one, 1999 is not
	TDate feb;
	feb.fMonth = 2;
	feb.fDate = 1;
	feb.fYear = 1996;
	EXPECT(feb.DaysInMonth() == 29);
	feb.fYear = 1999;
	EXPECT(feb.DaysInMonth() == 28);
	feb.fYear = 2000;
	EXPECT(feb.DaysInMonth() == 29);
	feb.fYear = 2100;
	EXPECT(feb.DaysInMonth() == 28);
	feb.fYear = 1904;
	feb.fDate = 29;
	EXPECT(feb.IsValidDate() && feb.DaysInYear() == 60);
	feb.fDate = 30;
	EXPECT(!feb.IsValidDate());
	feb.fMonth = 13;
	EXPECT(!feb.IsValidDate() && feb.DaysInMonth() == 0);
	// March 1st 1996 is day 61
	TDate march;
	march.fYear = 1996;
	march.fMonth = 3;
	march.fDate = 1;
	EXPECT(march.DaysInYear() == 61);
	march.fYear = 1997;
	EXPECT(march.DaysInYear() == 60);
	// the last of the month, then months on and back
	TDate jan31((ULong) 0);
	jan31.fDate = 31;
	jan31.CleanUpFields();
	EXPECT(jan31.fMonth == 1 && jan31.fDate == 31 && jan31.fDayOfWeek == (30 + 5) % 7);
	jan31.IncrementMonth(1);
	EXPECT(jan31.fMonth == 2 && jan31.fDate == 29 && jan31.fYear == 1904);		// clipped to February 1904's 29 days
	jan31.IncrementMonth(11);
	EXPECT(jan31.fMonth == 1 && jan31.fYear == 1905 && jan31.fDate == 29);
	jan31.IncrementMonth(-13);
	EXPECT(jan31.fMonth == 12 && jan31.fYear == 1903);		// (the year follows even before 1904)
	TDate early((ULong) 0);
	early.IncrementMonth(-1);
	EXPECT(early.fMonth == 1 && early.fYear == 1904);			// nothing before the epoch
	// a date frame
	RefVar frame(AllocateFrame());
	SetFrameSlot(frame, RSSYMyear, RefVar(MAKEINT(1990)));
	SetFrameSlot(frame, RSSYMmonth, RefVar(MAKEINT(10)));
	SetFrameSlot(frame, RSSYMdate, RefVar(MAKEINT(3)));
	SetFrameSlot(frame, RSSYMhour, RefVar(MAKEINT(14)));
	SetFrameSlot(frame, RSSYMminute, RefVar(MAKEINT(5)));
	TDate fromFrame;
	fromFrame.InitWithDateFrame(frame, true);
	EXPECT(fromFrame.TotalMinutes() == kWednesday3Oct1990 && fromFrame.fDayOfWeek == 3 && fromFrame.fSecond == 0);
	TDate partial;
	partial.InitWithDateFrame(RefVar(Eval("{month: 4}")), false);
	EXPECT(partial.fMonth == 4 && partial.fYear == -1 && partial.fDate == -1 && partial.fHour == -1);
	partial.InitWithDateFrame(RefVar(Eval("{month: 4}")), true);
	EXPECT(partial.fMonth == 4 && partial.fYear == 1904 && partial.fDate == 1 && partial.fHour == 0);
	// as a frame
	RefVar obj(ToObject(date));
	EXPECT(IsFrame(obj) && RINT(GetFrameSlotRef(obj, RSSYMyear)) == 1990 && RINT(GetFrameSlotRef(obj, RSSYMdayofweek)) == 3
		&& RINT(GetFrameSlotRef(obj, RSSYMdaysinmonth)) == 31 && RINT(GetFrameSlotRef(obj, RSSYMhour)) == 14);
	// week numbers: 3 Oct 1990 is in ISO week 40; from January 1st it is week 40 with Monday weeks
	EXPECT(WeekNumCalc(kWednesday3Oct1990, 1) == 40);
	TDate jan1((ULong) 0);
	jan1.fYear = 1990;
	EXPECT(WeekNumCalc(jan1.TotalMinutes(), 1) == 1);
}


static void
TestStrings()
{
	UniChar str[64];
	LongDateString(kWednesday3Oct1990, 0, str, 63, RefVar(NILREF));
	PrintU("long, all", str);
	EXPECT(UStringIs(str, "Mittwoch, 3. 10 1990"));
	ULong spec = RINT(GetFrameSlotRef(Rdatetimestrspecs, SYMBOL("longDateStrSpec")));
	LongDateString(kWednesday3Oct1990, spec, str, 63, RefVar(NILREF));
	PrintU("long", str);
	EXPECT(UStringIs(str, "Mittwoch, 3. Oktober 1990"));
	spec = RINT(GetFrameSlotRef(Rdatetimestrspecs, RSSYMabbrdatestrspec));
	LongDateString(kWednesday3Oct1990, spec, str, 63, RefVar(NILREF));
	PrintU("abbr", str);
	EXPECT(UStringIs(str, "Mit, 3. Okt. 1990"));
	ShortDateString(kWednesday3Oct1990, 0, str, 63, RefVar(NILREF));
	PrintU("short", str);
	EXPECT(UStringIs(str, "3.10.1990"));
	spec = RINT(GetFrameSlotRef(Rdatetimestrspecs, SYMBOL("yearMonthDayStrSpec")));
	ShortDateString(kWednesday3Oct1990, spec, str, 63, RefVar(NILREF));
	PrintU("year month day", str);
	TimeString(kWednesday3Oct1990, 0, str, 63, RefVar(NILREF));
	PrintU("time, all", str);
	EXPECT(UStringIs(str, "14:05:00 Uhr"));
	spec = RINT(GetFrameSlotRef(Rdatetimestrspecs, RSSYMshorttimestrspec));
	TimeString(kWednesday3Oct1990, spec, str, 63, RefVar(NILREF));
	PrintU("short time", str);
	EXPECT(UStringIs(str, "14:05"));
	TimeString(kWednesday3Oct1990 - 14 * 60 + 9 * 60 + 2, spec, str, 63, RefVar(NILREF));		// 9:07
	PrintU("9:07", str);
	EXPECT(UStringIs(str, "9:07"));
	TimeString(kWednesday3Oct1990 - 14 * 60 - 5, spec, str, 63, RefVar(NILREF));				// midnight
	PrintU("midnight", str);
	EXPECT(UStringIs(str, "0:00"));

	// the NewtonScript functions
	SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("when")), RefVar(MAKEINT(kWednesday3Oct1990)));
	EXPECT(StringIs(RefVar(Eval("LongDateStr(when, 0)")), "Mittwoch, 3. 10 1990"));
	EXPECT(StringIs(RefVar(Eval("ShortDateStr(when, 0)")), "3.10.1990"));
	EXPECT(StringIs(RefVar(Eval("TimeStr(when, 0)")), "14:05:00 Uhr"));
	EXPECT(StringIs(RefVar(Eval("HourMinute(when)")), "14:05"));
	EXPECT(StringIs(RefVar(Eval("DateNTime(when)")), "3.10.1990 14:05"));
	RefVar shortDate(Eval("ShortDate(when)"));
	PrintU("ShortDate", GetCString(shortDate));
	EXPECT(StringIs(shortDate, "Mit 3.10."));
	RefVar dateFrame(Eval("Date(when)"));
	EXPECT(IsFrame(dateFrame) && RINT(GetFrameSlotRef(dateFrame, RSSYMyear)) == 1990 && RINT(GetFrameSlotRef(dateFrame, RSSYMminute)) == 5);
	EXPECT(RINT(Eval("TotalMinutes({year: 1990, month: 10, date: 3, hour: 14, minute: 5})")) == kWednesday3Oct1990);
	EXPECT(RINT(Eval("TotalMinutes(Date(when))")) == kWednesday3Oct1990);
	EXPECT(RINT(Eval("WeekNumber(when, 1)")) == 40);
	EXPECT(RINT(Eval("Date(IncrementMonth(when, 3)).month")) == 1 && RINT(Eval("Date(IncrementMonth(when, 3)).year")) == 1991);
	EXPECT(RINT(Eval("Date(IncrementMonth(when, -10)).month")) == 12 && RINT(Eval("Date(IncrementMonth(when, -10)).date")) == 3);
	EXPECT(Eval("IsValidDate({year: 1990, month: 2, date: 29})") == NILREF);
	EXPECT(Eval("IsValidDate({year: 1996, month: 2, date: 29})") == TRUEREF);
	EXPECT(RINT(Eval("Time()")) > 0 && RINT(Eval("Ticks()")) >= 0);
	EXPECT(RINT(Eval("Date(Time()).year")) >= 2024);
	EXPECT(RINT(Eval("DateFromSeconds(TimeInSeconds()).year")) == RINT(Eval("Date(Time()).year")));
	EXPECT(StringIs(RefVar(Eval("TimeFrameStr({hour: 7, minute: 30, second: 5}, 0)")), "7:30:05 Uhr"));
	// the locale
	EXPECT(EQRef(Eval("GetLocale()"), GetCurrentLocale()));
	EXPECT(StringIs(RefVar(GetLocaleSlot(RefVar(NILREF), RSSYMtitle)), "Deutschland"));
	EXPECT(EQRef(GetLocaleSlot(RSSYMlocalesym), SYMBOL("Germany")));
	EXPECT(StringIs(RefVar(GetDayName(0)), "Sonntag"));
	// a string cannot be parsed yet: nil, and a frame with status -1
	EXPECT(Eval("StringToDate(\"3.10.1990\")") == NILREF);
	EXPECT(RINT(Eval("StringToDateFrame(\"3.10.1990\").status")) == -1);
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_ROM_BIN) != noErr)
	{
		printf("test_Dates: cannot import %s\n", NEWTON_ROM_BIN);
		return 1;
	}
	gObjectHeapSize = 0x100000;
	InitObjects();
	// what the boot script makes: vars.international with the locale bundle
	RefVar intl(AllocateFrame());
	SetFrameSlot(intl, RSSYMcurrentlocalebundle, RefVar(TranslateROMRef(kGermanyBundle)));
	SetFrameSlot(RefVar(gVarFrame), RSSYMinternational, intl);
	SetFrameSlot(RefVar(gVarFrame), RSSYMuserconfiguration, RefVar(AllocateFrame()));
	EXPECT(InitInternationalUtils() == noErr);
	// the host's clock is the real-time clock: seconds since 1904 (2082844800 of them before 1970)
	SetRealClockSeconds((ULong) ((unsigned long long) time(NULL) + 2082844800ULL));
	EXPECT(EQRef(GetCurrentLocale(), TranslateROMRef(kGermanyBundle)));
	EXPECT(IsArray(gLocaleCache->fLongDayOfWeek) && Length(gLocaleCache->fLongDayOfWeek) == 7);
	newton_try
	{
		TestCalendar();
		TestStrings();
	}
	newton_catch_all
	{
		failures++;
		fprintf(stderr, "FAIL: unhandled exception %s (%ld)\n", _info.exception.name, (long) (Long) _info.exception.data);
	}
	end_try;
	if (failures == 0)
		printf("test_Dates: all passed\n");
	else
		printf("test_Dates: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
