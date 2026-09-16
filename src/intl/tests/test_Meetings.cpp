// Repeating meetings test: the steppers for each repeat type (days of the
// week and weeks of the month, a date of the month or of the year, a
// period, a week of the year), NextMeeting/PrevMeeting/GetNextMeetingTime
// over a template frame with deleted and replaced exceptions and a stop
// date, and GetAllMeetings over a meeting soup and a repeating-meeting
// soup on a host store, plain and unique, from C++ and NewtonScript.
// Runs over a standalone kernel heap and object heap without ROM objects.
#include "Meetings.h"
#include "Soups.h"
#include "Cursors.h"
#include "Entries.h"
#include "StoreObject.h"
#include "host/HostStore.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Compiler.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "RSSymbols.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <stdlib.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static Ref SYMBOL(const char* name) { return Intern((char*) name); }

const long kDay = kMinutesPerDay;
const long kWednesday3Oct1990 = 45630125;					// 14:05
const long kMidnight3Oct1990 = kWednesday3Oct1990 - 14 * 60 - 5;


static Ref
Eval(const char* source)
{
	if (getenv("EVAL_TRACE")) fprintf(stderr, "eval: %s\n", source);
	RefVar fn(ParseString(RefVar(MakeString(source))));
	return InterpretBlock(fn, RefVar(gVarFrame));
}


// a date's fields
static Boolean
DateIs(const TDate& date, long year, long month, long day)
{
	if (date.fYear == year && date.fMonth == month && date.fDate == day)
		return true;
	fprintf(stderr, "  date is %ld-%02ld-%02ld, day of week %ld\n", date.fYear, date.fMonth, date.fDate, date.fDayOfWeek);
	return false;
}


static TDate
Date(long year, long month, long day)
{
	TDate date;
	date.fYear = year;
	date.fMonth = month;
	date.fDate = day;
	date.CleanUpFields();
	return date;
}


static void
TestSteppers()
{
	// every Monday and Wednesday: Wednesday the 3rd stays, Thursday the 4th goes to Monday the 8th
	TDate date = Date(1990, 10, 3);
	EXPECT(date.fDayOfWeek == 3);
	NextDayOfWeek(&date, kMonday | kWednesday | kEveryWeek);
	EXPECT(DateIs(date, 1990, 10, 3));
	date = Date(1990, 10, 4);
	NextDayOfWeek(&date, kMonday | kWednesday | kEveryWeek);
	EXPECT(DateIs(date, 1990, 10, 8) && date.fDayOfWeek == 1);
	date.fDate++;
	date.CleanUpFields();
	NextDayOfWeek(&date, kMonday | kWednesday | kEveryWeek);
	EXPECT(DateIs(date, 1990, 10, 10));
	// across the month's end: Saturday the 27th -> Monday the 29th, Tuesday the 30th -> Wednesday the 31st -> Monday 5 November
	date = Date(1990, 10, 27);
	NextDayOfWeek(&date, kMonday | kWednesday | kEveryWeek);
	EXPECT(DateIs(date, 1990, 10, 29));
	date = Date(1990, 11, 1);
	NextDayOfWeek(&date, kMonday | kWednesday | kEveryWeek);
	EXPECT(DateIs(date, 1990, 11, 5));
	// the second Friday of the month, the last Wednesday
	date = Date(1990, 10, 3);
	NextMeeting(&date, 0, kFriday | kSecondWeek, kWeekInMonth);
	EXPECT(DateIs(date, 1990, 10, 12));
	date = Date(1990, 10, 3);
	NextMeeting(&date, 0, kWednesday | kLastWeek, kWeekInMonth);
	EXPECT(DateIs(date, 1990, 10, 31));
	date = Date(1990, 11, 1);
	NextMeeting(&date, 0, kWednesday | kLastWeek, kWeekInMonth);
	EXPECT(DateIs(date, 1990, 11, 28));
	// the 15th of the month; the 31st clamped in a short month
	date = Date(1990, 10, 3);
	NextMeeting(&date, 0, 15, kDateInMonth);
	EXPECT(DateIs(date, 1990, 10, 15));
	date = Date(1990, 10, 20);
	NextMeeting(&date, 0, 15, kDateInMonth);
	EXPECT(DateIs(date, 1990, 11, 15));
	date = Date(1990, 12, 20);
	NextMeeting(&date, 0, 15, kDateInMonth);
	EXPECT(DateIs(date, 1991, 1, 15));
	date = Date(1990, 11, 5);
	NextMeeting(&date, 0, 31, kDateInMonth);
	EXPECT(DateIs(date, 1990, 11, 30));
	// Christmas
	date = Date(1990, 10, 3);
	NextMeeting(&date, 0, (12 << 8) | 25, kDateInYear);
	EXPECT(DateIs(date, 1990, 12, 25));
	date = Date(1990, 12, 26);
	NextMeeting(&date, 0, (12 << 8) | 25, kDateInYear);
	EXPECT(DateIs(date, 1991, 12, 25));
	date = Date(1991, 1, 1);
	NextMeeting(&date, 0, (2 << 8) | 29, kDateInYear);
	EXPECT(DateIs(date, 1991, 2, 28));
	// every ten days from the 3rd of October
	ULong firstDay = Date(1990, 10, 3).TotalDays();
	date = Date(1990, 10, 3);
	NextMeeting(&date, 0, (firstDay << 8) | 10, kPeriod);
	EXPECT(DateIs(date, 1990, 10, 3));
	date = Date(1990, 10, 4);
	NextMeeting(&date, 0, (firstDay << 8) | 10, kPeriod);
	EXPECT(DateIs(date, 1990, 10, 13));
	date = Date(1990, 10, 24);
	NextMeeting(&date, 0, (firstDay << 8) | 10, kPeriod);
	EXPECT(DateIs(date, 1990, 11, 2));
	date = Date(1990, 9, 1);
	NextMeeting(&date, 0, (firstDay << 8) | 10, kPeriod);
	EXPECT(DateIs(date, 1990, 10, 3));
	// the fourth Thursday of November
	date = Date(1990, 10, 3);
	NextMeeting(&date, 0, (11 << 12) | kThursday | kFourthWeek, kWeekInYear);
	EXPECT(DateIs(date, 1990, 11, 22));
	date = Date(1990, 11, 23);
	NextMeeting(&date, 0, (11 << 12) | kThursday | kFourthWeek, kWeekInYear);
	EXPECT(DateIs(date, 1991, 11, 28));
	date = Date(1990, 11, 10);
	NextMeeting(&date, 0, (11 << 12) | kThursday | kFourthWeek, kWeekInYear);
	EXPECT(DateIs(date, 1990, 11, 22));
	// never
	date = Date(1990, 10, 3);
	NextMeeting(&date, 0, 0, kNever);
	EXPECT(DateIs(date, 1990, 10, 3));
	// back: the Monday/Wednesday before the 10th is the 8th; the 15th before the 3rd of October is the 15th of September
	date = Date(1990, 10, 10);
	simplePrevMeeting(&date, kMonday | kWednesday | kEveryWeek, kDayOfWeek);
	EXPECT(DateIs(date, 1990, 10, 8));
	date = Date(1990, 10, 3);
	simplePrevMeeting(&date, 15, kDateInMonth);
	EXPECT(DateIs(date, 1990, 9, 15));
	date = Date(1990, 10, 3);
	simplePrevMeeting(&date, (12 << 8) | 25, kDateInYear);
	EXPECT(DateIs(date, 1989, 12, 25));
}


// a repeat template frame
static Ref
Template(long start, long repeatType, ULong mtgInfo, long stop = kForeverStopDate)
{
	RefVar frame(AllocateFrame());
	SetFrameSlot(frame, RSSYMclass, RSSYMmeeting);
	SetFrameSlot(frame, RSSYMmtgstartdate, RefVar(MAKEINT(start)));
	SetFrameSlot(frame, RSSYMmtgduration, RefVar(MAKEINT(60)));
	SetFrameSlot(frame, RSSYMrepeattype, RefVar(MAKEINT(repeatType)));
	SetFrameSlot(frame, RSSYMmtginfo, RefVar(MAKEINT(mtgInfo)));
	SetFrameSlot(frame, RSSYMmtgstopdate, RefVar(MAKEINT(stop)));
	SetFrameSlot(frame, RSSYMexceptions, RefVar(NILREF));
	return frame;
}


static Ref
Meeting(long start)
{
	RefVar frame(AllocateFrame());
	SetFrameSlot(frame, RSSYMclass, RSSYMmeeting);
	SetFrameSlot(frame, RSSYMmtgstartdate, RefVar(MAKEINT(start)));
	SetFrameSlot(frame, RSSYMmtgduration, RefVar(MAKEINT(30)));
	return frame;
}


static void
TestNextMeeting()
{
	// Mondays and Wednesdays at 14:05 from Wednesday 3 October 1990
	RefVar tmpl(Template(kWednesday3Oct1990, kDayOfWeek, kMonday | kWednesday | kEveryWeek));
	SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("tmpl")), tmpl);
	SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("when")), RefVar(MAKEINT(kWednesday3Oct1990)));
	EXPECT(RINT(Eval("NextMeeting(when, tmpl)")) == kWednesday3Oct1990);
	EXPECT(RINT(Eval("NextMeeting(when + 1, tmpl)")) == kWednesday3Oct1990 + 5 * kDay);			// Monday the 8th
	EXPECT(RINT(Eval("NextMeeting(when - 10 * 1440, tmpl)")) == kWednesday3Oct1990);				// not before the first
	EXPECT(RINT(Eval("NextMeeting(when + 5 * 1440, tmpl)")) == kWednesday3Oct1990 + 5 * kDay);	// at the instance
	EXPECT(RINT(Eval("NextMeeting(when + 5 * 1440 + 1, tmpl)")) == kWednesday3Oct1990 + 7 * kDay);	// Wednesday the 10th
	EXPECT(RINT(Eval("GetNextMeetingTime(tmpl, when + 1)")) == kWednesday3Oct1990 + 5 * kDay);
	// back
	EXPECT(RINT(Eval("PrevMeeting(when + 5 * 1440, tmpl)")) == kWednesday3Oct1990);
	EXPECT(RINT(Eval("PrevMeeting(when + 7 * 1440, tmpl)")) == kWednesday3Oct1990 + 5 * kDay);
	EXPECT(RINT(Eval("PrevMeeting(when + 30 * 1440, tmpl)")) == kWednesday3Oct1990 + 28 * kDay);	// Wednesday the 31st
	EXPECT(RINT(Eval("PrevMeeting(when, tmpl)")) == 0);
	EXPECT(RINT(Eval("PrevMeeting(when - 1, tmpl)")) == 0);
	// Monday the 8th deleted: the next is Wednesday the 10th
	Eval("tmpl.exceptions := [[when + 5 * 1440, nil]]");
	EXPECT(RINT(Eval("NextMeeting(when + 1, tmpl)")) == kWednesday3Oct1990 + 7 * kDay);
	EXPECT(RINT(Eval("PrevMeeting(when + 7 * 1440, tmpl)")) == kWednesday3Oct1990);
	// Monday the 8th moved to Tuesday the 9th at 10:00: that is the next
	long tuesday = kMidnight3Oct1990 + 6 * kDay + 10 * 60;
	Eval("tmpl.exceptions := [[when + 5 * 1440, {class: 'meeting, mtgStartDate: when - 14 * 60 - 5 + 6 * 1440 + 10 * 60, mtgDuration: 30}]]");
	EXPECT(RINT(Eval("NextMeeting(when + 1, tmpl)")) == tuesday);
	EXPECT(RINT(Eval("NextMeeting(when + 6 * 1440 + 10 * 60 + 1, tmpl)")) == kWednesday3Oct1990 + 7 * kDay);
	EXPECT(RINT(Eval("PrevMeeting(when + 7 * 1440, tmpl)")) == tuesday);
	// stopped after Monday the 8th
	Eval("tmpl.exceptions := nil");
	Eval("tmpl.mtgStopDate := when + 6 * 1440");
	EXPECT(RINT(Eval("NextMeeting(when + 1, tmpl)")) == kWednesday3Oct1990 + 5 * kDay);
	EXPECT(RINT(Eval("NextMeeting(when + 5 * 1440 + 1, tmpl)")) == 0);
	EXPECT(RINT(Eval("PrevMeeting(when + 30 * 1440, tmpl)")) == kWednesday3Oct1990 + 5 * kDay);
	// a malformed template is repaired - and written back, which throws for a frame that is no soup entry
	Eval("tmpl.mtgInfo := nil");
	Boolean threw = false;
	newton_try
	{
		Eval("NextMeeting(when, tmpl)");
	}
	newton_catch_all
	{
		threw = true;
	}
	end_try;
	EXPECT(threw);
	EXPECT(RINT(Eval("tmpl.mtgInfo")) == 0);
	// a monthly meeting on the 15th at 9:00 from 15 October 1990
	long fifteenth = kMidnight3Oct1990 + 12 * kDay + 9 * 60;
	SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("monthly")), RefVar(Template(fifteenth, kDateInMonth, 15)));
	EXPECT(RINT(Eval("NextMeeting(when, monthly)")) == fifteenth);
	EXPECT(RINT(Eval("NextMeeting(when + 12 * 1440, monthly)")) == fifteenth + 31 * kDay);		// 15 November
	EXPECT(RINT(Eval("PrevMeeting(when + 60 * 1440, monthly)")) == fifteenth + 31 * kDay);
	EXPECT(RINT(Eval("PrevMeeting(when + 40 * 1440, monthly)")) == fifteenth);
}


static TStore*
NewStore(ULong size = 0x80000)
{
	TStore* store = (TStore*) THostStore::ClassInfo()->New();
	EXPECT(store != nil);
	EXPECT(store->Init(nil, size, 0, 0, kStoreIsInternal, nil) == noErr);
	EXPECT(store->Format() == noErr);
	return store;
}


static Ref
IndexSpec(Ref path, const char* type)
{
	RefVar spec(AllocateFrame());
	SetFrameSlot(spec, RSSYMstructure, RSSYMslot);
	SetFrameSlot(spec, RSSYMpath, RefVar(path));
	SetFrameSlot(spec, RSSYMtype, RefVar(SYMBOL(type)));
	return spec;
}


static void
TestGetAllMeetings()
{
	TStore* store = NewStore();
	RefVar storeObject(RegisterTStore(store));
	RefVar specs(AllocateArray(RSSYMarray, 1));
	SetArraySlotRef(specs, 0, IndexSpec(RSSYMmtgstartdate, "int"));
	RefVar soup(StoreCreateSoup(storeObject, RefVar(MakeString("Calendar")), specs));
	SetArraySlotRef(specs, 0, IndexSpec(RSSYMmtgstopdate, "int"));
	RefVar repeatSoup(StoreCreateSoup(storeObject, RefVar(MakeString("Repeat Meetings")), specs));
	// a plain meeting on Thursday the 4th at 9:00, one on Friday the 12th, and the Monday/Wednesday template
	long thursday = kMidnight3Oct1990 + kDay + 9 * 60;
	SoupAdd(soup, RefVar(Meeting(thursday)));
	SoupAdd(soup, RefVar(Meeting(kMidnight3Oct1990 + 9 * kDay + 9 * 60)));
	RefVar tmpl(SoupAdd(repeatSoup, RefVar(Template(kWednesday3Oct1990, kDayOfWeek, kMonday | kWednesday | kEveryWeek))));

	// the week from Wednesday the 3rd: the instances of the 3rd, 8th and 10th around Thursday's meeting, in order
	RefVar meetings(GetAllMeetings(soup, repeatSoup, kMidnight3Oct1990, kMidnight3Oct1990 + 8 * kDay, false));
	EXPECT(IsArray(meetings) && Length(meetings) == 4);
	if (IsArray(meetings) && Length(meetings) == 4)
	{
		long times[4] = { kWednesday3Oct1990, thursday, kWednesday3Oct1990 + 5 * kDay, kWednesday3Oct1990 + 7 * kDay };
		for (long i = 0; i < 4; i++)
		{
			RefVar meeting(GetArraySlotRef(meetings, i));
			EXPECT(RINT(GetFrameSlotRef(meeting, RSSYMmtgstartdate)) == times[i]);
			if (i == 1)
				EXPECT(GetFrameSlotRef(meeting, RSSYMrepeattemplate) == NILREF);
			else
			{
				EXPECT(EQRef(GetFrameSlotRef(meeting, RSSYMrepeattemplate), tmpl));
				EXPECT(EQRef(GetFrameSlotRef(meeting, RSSYMviewstationery), RSSYMrepeatingmeeting));
				EXPECT(EQRef(GetFrameSlotRef(meeting, RSSYMclass), RSSYMmeeting));
			}
		}
	}
	// only the first instance when unique
	meetings = GetAllMeetings(soup, repeatSoup, kMidnight3Oct1990, kMidnight3Oct1990 + 8 * kDay, true);
	EXPECT(IsArray(meetings) && Length(meetings) == 2);
	// a day with nothing: nil; the meeting soup alone; the repeat soup alone
	EXPECT(GetAllMeetings(soup, repeatSoup, kMidnight3Oct1990 + 2 * kDay, kMidnight3Oct1990 + 3 * kDay, false) == NILREF);
	meetings = GetAllMeetings(soup, RefVar(NILREF), kMidnight3Oct1990, kMidnight3Oct1990 + 8 * kDay, false);
	EXPECT(IsArray(meetings) && Length(meetings) == 1);
	meetings = GetAllMeetings(RefVar(NILREF), repeatSoup, kMidnight3Oct1990, kMidnight3Oct1990 + 8 * kDay, false);
	EXPECT(IsArray(meetings) && Length(meetings) == 3);
	// Monday the 8th deleted, Wednesday the 10th moved to Thursday the 11th at 16:00
	long thursday11th = kMidnight3Oct1990 + 8 * kDay + 16 * 60;
	RefVar replacement(Meeting(thursday11th));
	RefVar exceptions(MakeArray(0));
	RefVar pair(MakeArray(2));
	SetArraySlotRef(pair, 0, MAKEINT(kWednesday3Oct1990 + 5 * kDay));
	AddArraySlot(exceptions, pair);
	pair = MakeArray(2);
	SetArraySlotRef(pair, 0, MAKEINT(kWednesday3Oct1990 + 7 * kDay));
	SetArraySlotRef(pair, 1, replacement);
	AddArraySlot(exceptions, pair);
	SetFrameSlot(tmpl, RSSYMexceptions, exceptions);
	EntryChange(tmpl);
	meetings = GetAllMeetings(soup, repeatSoup, kMidnight3Oct1990, kMidnight3Oct1990 + 9 * kDay, false);
	EXPECT(IsArray(meetings) && Length(meetings) == 3);
	if (IsArray(meetings) && Length(meetings) == 3)
	{
		EXPECT(RINT(GetFrameSlotRef(RefVar(GetArraySlotRef(meetings, 0)), RSSYMmtgstartdate)) == kWednesday3Oct1990);
		EXPECT(RINT(GetFrameSlotRef(RefVar(GetArraySlotRef(meetings, 1)), RSSYMmtgstartdate)) == thursday);
		EXPECT(RINT(GetFrameSlotRef(RefVar(GetArraySlotRef(meetings, 2)), RSSYMmtgstartdate)) == thursday11th);
		EXPECT(EQRef(GetFrameSlotRef(RefVar(GetArraySlotRef(meetings, 2)), RSSYMrepeattemplate), tmpl));	// the replacement knows its template
	}
	// the replacement lies outside the range: not included
	meetings = GetAllMeetings(soup, repeatSoup, kMidnight3Oct1990, kMidnight3Oct1990 + 8 * kDay, false);
	EXPECT(IsArray(meetings) && Length(meetings) == 2);
	// from NewtonScript: a day by default
	SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("soup")), soup);
	SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("rsoup")), repeatSoup);
	SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("midnight")), RefVar(MAKEINT(kMidnight3Oct1990)));
	EXPECT(RINT(Eval("Length(GetAllMeetings(soup, rsoup, midnight, nil))")) == 1);
	EXPECT(RINT(Eval("GetAllMeetings(soup, rsoup, midnight, nil)[0].mtgStartDate")) == kWednesday3Oct1990);
	EXPECT(RINT(Eval("Length(GetAllMeetings(soup, rsoup, midnight, midnight + 9 * 1440))")) == 3);
	EXPECT(RINT(Eval("Length(GetAllMeetingsUnique(soup, rsoup, midnight, midnight + 9 * 1440))")) == 3);	// the first instance, the plain one and the replacement
	EXPECT(Eval("GetAllMeetings(soup, rsoup, midnight + 2 * 1440, nil)") == NILREF);
	// a template whose slots went bad is repaired and written back
	Eval("foreach m in GetAllMeetings(nil, rsoup, midnight, nil) do m.repeatTemplate.repeatType := 'weekly");
	EXPECT(Eval("GetAllMeetings(nil, rsoup, midnight, midnight + 9 * 1440)") == NILREF);			// the bad template's instances dropped ...
	EXPECT(RINT(GetFrameSlotRef(tmpl, RSSYMrepeattype)) == 0);										// ... and the template repaired (kDayOfWeek)
	EXPECT(RINT(Eval("Length(GetAllMeetings(soup, rsoup, midnight, midnight + 9 * 1440))")) == 3);	// so that it works again
	RemoveTStore(store);
	store->Delete();
}


int
main()
{
	InitHostStandaloneHeap();
	gObjectHeapSize = 0x200000;
	InitObjects();
	InitQueries();
	RegisterDateNatives();
	RegisterMeetingNatives();
	InstallHostNatives();
	SetFrameSlot(RefVar(gVarFrame), RSSYMvars, RefVar(gVarFrame));
	SetFrameSlot(RefVar(gVarFrame), RSSYMfunctions, RefVar(gFunctionFrame));
	newton_try
	{
		TestSteppers();
		TestNextMeeting();
		TestGetAllMeetings();
	}
	newton_catch_all
	{
		failures++;
		fprintf(stderr, "FAIL: unhandled exception %s (%ld)\n", _info.exception.name, (long) (Long) _info.exception.data);
	}
	end_try;
	if (failures == 0)
		printf("test_Meetings: all passed\n");
	else
		printf("test_Meetings: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
