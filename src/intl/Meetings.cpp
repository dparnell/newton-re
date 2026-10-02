/*
	File:		intl/Meetings.cpp

	Contains:	The repeating meetings: stepping a TDate to the next (or
				previous) instance of a repeat template, the instances of
				the templates in a range merged with their exceptions and
				the plain meetings of the meeting soup, and the NewtonScript
				functions over them.

	A repeat template (an entry of the repeating-meeting soup) holds
	mtgStartDate (the first instance, minutes since 1904: its time of day
	is every instance's), mtgDuration, repeatType and mtgInfo (Meetings.h
	says what the word encodes for each type), mtgStopDate and exceptions:
	an array of [date, entry] pairs - the instance at date is deleted
	(entry nil) or replaced by entry, a meeting of its own.

	Reconstructed from the MP2x00 US ROM (0x0008aa4c-0x0008c6c4); each
	function cites its origin.
*/

#include "Meetings.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "NativeFunctions.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "NewtonExceptions.h"
#include "Cursors.h"
#include "Soups.h"
#include "Entries.h"


/*------------------------------------------------------------------------------
	S t e p p i n g   a   d a t e
------------------------------------------------------------------------------*/

// ROM 0x0008aa4c NextDayOfWeek__FP5TDateUl
// The date moved to the next (this one included) of the days of the week
// mtgInfo asks for, then, unless every week is wanted, to the next of its
// weeks of the month (the last-week bit means the last such day of the
// month); a step past the month's end goes into the next month.
void
NextDayOfWeek(TDate* date, ULong mtgInfo)
{
	ULong daysInMonth = date->DaysInMonth();
	ULong dayBit = kSunday >> (date->fDayOfWeek & 0xff);
	for (long i = 0; i < 7; i++)
	{
		if (mtgInfo & kEveryDay & dayBit)
		{
			date->fDate += i;
			break;
		}
		dayBit >>= 1;
		if (dayBit < kSaturday)
			dayBit = kSunday;
	}
	date->CleanUpFields();
	if ((mtgInfo & kEveryWeek) != kEveryWeek)
	{
		long day = date->fDate;
		ULong weekBit = kFirstWeek >> ((ULong) (day - 1) / 7);
		for (long i = 0; i < 29; i += 7)
		{
			if (weekBit == kLastWeek && daysInMonth < (ULong) (day + i))
				i -= 7;
			if (mtgInfo & kEveryWeek & weekBit)
			{
				date->fDate = day + i;
				date->fDayOfWeek = (ULong) (date->fDayOfWeek + i) % 7;
				break;
			}
			weekBit >>= 1;
			if (weekBit == 0)
				weekBit = kFirstWeek;
		}
	}
	if ((ULong) date->fDate > daysInMonth)
	{
		date->fDate -= daysInMonth;
		if (++date->fMonth > 12)
		{
			date->fMonth = 1;
			date->fYear++;
		}
	}
}


// ROM 0x0008ab80 NextDateOfMonth__FP5TDateUl
// The date moved to the wanted date of the month (mtgInfo's low six bits),
// in the next month when that is already past (or the date is 0), clamped
// to the month's length.  The day of the week is shifted by the days
// moved as the ROM does it (a 32-bit unsigned remainder, so only right
// within the month; the callers normalise the date before reading it).
// DEVIATION: the ROM leaves the shift uninitialised when the wanted date
// is past the month's end; the shift to the clamped date is used.
void
NextDateOfMonth(TDate* date, ULong mtgInfo)
{
	ULong today = date->fDate;
	ULong wanted = mtgInfo & 0x3f;
	if (wanted == 0 || wanted < today)
	{
		if (++date->fMonth > 12)
		{
			date->fMonth = 1;
			date->fYear++;
		}
	}
	ULong daysInMonth = date->DaysInMonth();
	ULong newDate = (wanted <= daysInMonth) ? wanted : daysInMonth;
	ULong shift = (ULong32) (newDate - today) % 7;
	date->fDate = newDate;
	date->fDayOfWeek = (ULong) (date->fDayOfWeek + shift) % 7;
}


// ROM 0x0008bc68 NextDateOfYear__FP5TDateUl
// The date moved to the wanted month (mtgInfo bits 8-15) and date (bits
// 0-7), next year when that is already past, the date clamped to the
// month's length.
void
NextDateOfYear(TDate* date, ULong mtgInfo)
{
	ULong month = (mtgInfo >> 8) & 0xff;
	ULong day = mtgInfo & 0xff;
	if (month < (ULong) date->fMonth || (month == (ULong) date->fMonth && day < (ULong) date->fDate))
		date->fYear++;
	date->fMonth = month;
	long daysInMonth = date->DaysInMonth();
	if (daysInMonth < (long) day)
		day = daysInMonth;
	date->fDate = day;
}


// ROM 0x0008c290 NextDateByWeekInYear__FP5TDateUl
// The date moved to the wanted day of the wanted week of the wanted month
// (mtgInfo bits 12-15) of the year: this year's when it is not yet past,
// next year's otherwise.
void
NextDateByWeekInYear(TDate* date, ULong mtgInfo)
{
	ULong month = (mtgInfo >> 12) & 0xf;
	TDate first;
	first.fYear = date->fYear;
	first.fMonth = month;
	first.fDate = 1;
	if (month == (ULong) date->fMonth)
	{
		first.CleanUpFields();
		NextDayOfWeek(&first, mtgInfo);
		if ((ULong) date->fDate <= (ULong) first.fDate)
		{
			date->fDate = first.fDate;
			return;
		}
		first.fYear++;
	}
	else if (month < (ULong) date->fMonth)
		first.fYear++;
	first.fDate = 1;
	first.CleanUpFields();
	NextDayOfWeek(&first, mtgInfo);
	date->fYear = first.fYear;
	date->fMonth = first.fMonth;
	date->fDate = first.fDate;
}


// ROM 0x0008c354 NextPeriod__FP5TDateUlT2
// The date moved to the next day a whole number of periods (mtgInfo bits
// 0-7, in days) after the first instance's day (bits 8-31: its TotalDays);
// forward to the first when before it.  The ROM's second argument is
// unused (its callers pass 0).
// DEVIATION: a period of 0 (a division by zero in the ROM) leaves the
// date where it is.
void
NextPeriod(TDate* date, ULong /*unused*/, ULong mtgInfo)
{
	long period = mtgInfo & 0xff;
	long sinceFirst = date->TotalDays() - (long) (mtgInfo >> 8);
	if (sinceFirst < 1)
		date->fDate -= sinceFirst;
	else if (period != 0)
	{
		long ahead = period - sinceFirst % period;
		if (ahead == period)
			ahead = 0;
		date->fDate += ahead;
	}
	date->CleanUpFields();
}


// ROM 0x0008c3b0 NextMeeting__FP5TDateUlN22
// The date moved to the repeat's next instance on or after it, by its
// type (the ROM inlines the steppers; kNever and the unused type 6 leave
// the date alone).
void
NextMeeting(TDate* date, ULong unused, ULong mtgInfo, ULong repeatType)
{
	switch (repeatType)
	{
	case kDayOfWeek:
	case kWeekInMonth:
		NextDayOfWeek(date, mtgInfo);
		break;
	case kDateInMonth:
		NextDateOfMonth(date, mtgInfo);
		break;
	case kDateInYear:
		NextDateOfYear(date, mtgInfo);
		break;
	case kPeriod:
		NextPeriod(date, unused, mtgInfo);
		break;
	case kWeekInYear:
		NextDateByWeekInYear(date, mtgInfo);
		break;
	default:
		break;
	}
}


// ROM 0x0008ba44 simplePrevMeeting__FP5TDateUlT2
// The date moved back towards the previous instance: a month, a year or
// two weeks back and forward again to the type's next instance (the
// day-of-week types step forward from the month before until the date is
// reached and keep the step before that).  FPrevMeeting refines the
// result with FNextMeeting.
void
simplePrevMeeting(TDate* date, ULong mtgInfo, ULong repeatType)
{
	switch (repeatType)
	{
	case kDayOfWeek:
	case kWeekInMonth:
		{
			TDate before;
			TDate work(*date);
			work.fMonth--;
			do
			{
				before = work;
				work.fDate++;
				work.CleanUpFields();
				NextDayOfWeek(&work, mtgInfo);
			} while ((ULong) work.TotalMinutes() < (ULong) date->TotalMinutes());
			*date = before;
		}
		break;
	case kDateInMonth:
		date->fMonth--;
		NextDateOfMonth(date, mtgInfo);
		break;
	case kDateInYear:
		date->fYear--;
		NextDateOfYear(date, mtgInfo);
		break;
	case kPeriod:
		date->fDate -= 14;
		date->CleanUpFields();
		break;
	case kWeekInYear:
		date->fYear--;
		NextDateByWeekInYear(date, mtgInfo);
		break;
	default:
		break;
	}
}


/*------------------------------------------------------------------------------
	T h e   i n s t a n c e s
------------------------------------------------------------------------------*/

// ROM 0x0008c400 MakeMeetingFrame__FUlRC6RefVar
// An instance frame (protoInstanceOfRepeatingMeeting: viewStationery
// 'RepeatingMeeting, class 'meeting) at minutes, of the template; a
// cribNote template's instance is a cribNote, the viewBounds carried.
Ref
MakeMeetingFrame(ULong minutes, RefArg repeatTemplate)
{
	RefVar frame(Clone(RefVar(Rprotoinstanceofrepeatingmeeting)));
	RefVar stationery(GetFrameSlotRef(repeatTemplate, RSSYMviewstationery));
	if (EQRef(stationery, RSSYMcribnote))
		SetFrameSlot(frame, RSSYMviewstationery, RSSYMcribnote);
	RefVar bounds(GetFrameSlotRef(repeatTemplate, RSSYMviewbounds));
	if ((Ref) bounds != NILREF)
		SetFrameSlot(frame, RSSYMviewbounds, bounds);
	SetFrameSlot(frame, RSSYMmtgstartdate, RefVar(MAKEINT(minutes)));
	SetFrameSlot(frame, RSSYMrepeattemplate, repeatTemplate);
	return frame;
}


// ROM 0x0008c664 SnoopIntegerSlot__FRC6RefVarT1l
// The slot set to the default unless it holds an integer.
static void
SnoopIntegerSlot(RefArg frame, RefArg slot, long value)
{
	if (!ISINT(GetFrameSlotRef(frame, slot)))
		SetFrameSlot(frame, slot, RefVar(MAKEINT(value)));
}


// ROM 0x0008ac18 FixupRepeatFrame__FRC6RefVar
// A template that failed to read repaired: its integer slots defaulted
// (mtgStartDate the current hour, mtgInfo and repeatType 0, mtgStopDate
// forever, mtgDuration an hour), the first malformed exception dropped,
// and the entry written back when its store can be written (an error
// there ignored).
void
FixupRepeatFrame(RefArg entry)
{
	TDate now;
	now.SetCurrentTime();
	long minutes = now.TotalMinutes();
	SnoopIntegerSlot(entry, RSSYMmtgstartdate, minutes - minutes % 60);
	SnoopIntegerSlot(entry, RSSYMmtginfo, 0);
	SnoopIntegerSlot(entry, RSSYMrepeattype, 0);
	SnoopIntegerSlot(entry, RSSYMmtgstopdate, kForeverStopDate);
	SnoopIntegerSlot(entry, RSSYMmtgduration, 60);
	RefVar exceptions(GetFrameSlotRef(entry, RSSYMexceptions));
	RefVar exception;
	RefVar replacement;
	RefVar when;
	if ((Ref) exceptions != NILREF)
	{
		long count = Length(exceptions);
		for (long i = 0; i < count; i++)
		{
			exception = GetArraySlotRef(exceptions, i);
			when = GetArraySlotRef(exception, 0);
			replacement = GetArraySlotRef(exception, 1);
			if (!ISINT(when) || ((Ref) replacement != NILREF && !ISINT(GetFrameSlotRef(replacement, RSSYMmtgstartdate))))
			{
				ArrayRemoveCount(exceptions, i, 1);
				break;
			}
		}
	}
	RefVar store(EntryStore(entry));
	if (StoreIsReadOnly(store) == NILREF)
	{
		newton_try
		{
			EntryChange(entry);
		}
		newton_catch_all
		{ }
		end_try;
	}
}


// ROM 0x0008b91c FindExceptionMeetingInRange__FRC6RefVarlT2
// The exceptions of a template against a candidate instance at end: ==> 0
// when the instance is an exception (deleted or replaced), else the
// earliest replacement meeting's time in [start, end), else -1.
long
FindExceptionMeetingInRange(RefArg exceptions, long start, long end)
{
	long earliest = end + 1;
	Boolean isException = false;
	if ((Ref) exceptions != NILREF)
	{
		long count = Length(exceptions);
		RefVar exception;
		RefVar replacement;
		for (long i = 0; i < count; i++)
		{
			exception = GetArraySlotRef(exceptions, i);
			replacement = GetArraySlotRef(exception, 1);
			if (RINT(GetArraySlotRef(exception, 0)) == end)
				isException = true;
			if ((Ref) replacement != NILREF)
			{
				Long when = RINT(GetFrameSlotRef(replacement, RSSYMmtgstartdate));
				if (start <= when && when < earliest)
					earliest = when;
			}
		}
	}
	if (earliest == end + 1)
		return isException ? 0 : -1;
	return earliest;
}


// ROM 0x0008c534 PopMeeting__FRC6RefVarPlP6RefVarT2
// The next instance of the list and its mtgStartDate (nil and 0 at the end).
static void
PopMeeting(RefArg meetings, long* index, RefVar* meeting, long* when)
{
	if (*index < Length(meetings))
	{
		*meeting = GetArraySlotRef(meetings, (*index)++);
		*when = RINT(GetFrameSlotRef(*meeting, RSSYMmtgstartdate));
	}
	else
	{
		*meeting = NILREF;
		*when = 0;
	}
}


// ROM 0x0008c5d0 PopException__FRC6RefVarPlP6RefVarT2
// The next exception of the list and its date (nil and 0 at the end).
static void
PopException(RefArg exceptions, long* index, RefVar* exception, long* when)
{
	if (*index < Length(exceptions))
	{
		*exception = GetArraySlotRef(exceptions, (*index)++);
		*when = RINT(GetArraySlotRef(*exception, 0));
	}
	else
	{
		*exception = NILREF;
		*when = 0;
	}
}


// ROM 0x0008ae5c AddException__FRC6RefVarT1lT3
// An exception's replacement meeting, when it has one in [start, end),
// inserted into the meetings in mtgStartDate order.
static void
AddException(RefArg meetings, RefArg exception, long start, long end)
{
	RefVar replacement(GetArraySlotRef(exception, 1));
	if ((Ref) replacement != NILREF)
	{
		Long when = RINT(GetFrameSlotRef(replacement, RSSYMmtgstartdate));
		if (when < end && start <= when)
			FBInsert(RefVar(NILREF), meetings, replacement, RSSYM_3C, RSSYMmtgstartdate, RefVar(NILREF));
	}
}


// ROM 0x0008af34 MergeMeetingLists__FRC6RefVarN21lT4
// The generated instances (in time order) and the template's exceptions
// (in date order) merged into the meetings: an exception at an instance's
// time replaces it (its replacement meeting added when in range), the
// other instances and replacements are inserted in mtgStartDate order.
static void
MergeMeetingLists(RefArg meetings, RefArg instances, RefArg exceptions, long start, long end)
{
	long meetingIndex = 0;
	long exceptionIndex = 0;
	RefVar meeting;
	RefVar exception;
	long meetingTime;
	long exceptionTime;
	PopMeeting(instances, &meetingIndex, &meeting, &meetingTime);
	PopException(exceptions, &exceptionIndex, &exception, &exceptionTime);
	for (;;)
	{
		long soonest = (meetingTime > 0) ? meetingTime : exceptionTime;
		if (soonest < 1)
			return;
		if (meetingTime == 0 || (exceptionTime != 0 && exceptionTime < meetingTime))
		{
			AddException(meetings, exception, start, end);
			PopException(exceptions, &exceptionIndex, &exception, &exceptionTime);
		}
		else if (exceptionTime == 0 || meetingTime < exceptionTime)
		{
			FBInsert(RefVar(NILREF), meetings, meeting, RSSYM_3C, RSSYMmtgstartdate, RefVar(NILREF));
			PopMeeting(instances, &meetingIndex, &meeting, &meetingTime);
		}
		else
		{
			AddException(meetings, exception, start, end);
			PopException(exceptions, &exceptionIndex, &exception, &exceptionTime);
			PopMeeting(instances, &meetingIndex, &meeting, &meetingTime);
		}
	}
}


// ROM 0x0008b624 GetInstanceMeetings__FRC6RefVarT1
// The entries of the cursor (the meeting soup's, over the range) into the
// meetings array.
void
GetInstanceMeetings(RefArg meetings, RefArg cursor)
{
	RefVar entry(CursorEntry(cursor));
	long count = CursorObj(cursor)->CountEntries();
	if (count > 0)
	{
		SetLength(meetings, count);
		for (long i = 0; (Ref) entry != NILREF; i++)
		{
			SetArraySlotRef(meetings, i, entry);
			entry = CursorNext(cursor);
		}
	}
}


// ROM 0x0008b128 GetRepeatingMeetings__FRC6RefVarT1UlT3Uc
// For each template the cursor yields (the repeating-meeting soup's, over
// the templates not yet stopped): its instances in [start, end) generated
// by NextMeeting (from the range's start, or the day after when the
// template's time of day is already past on that day; only the first
// when unique), its exceptions sorted by date (a replacement meeting
// given a repeatTemplate slot), and both merged into the meetings.  A
// template whose slots cannot be read is repaired (FixupRepeatFrame) and
// the collection stops there.
void
GetRepeatingMeetings(RefArg meetings, RefArg cursor, ULong start, ULong end, Boolean unique)
{
	TDate rangeStart(start);
	TDate templateDate;
	TDate date;
	RefVar entry(CursorEntry(cursor));
	newton_try
	{
		RefVar instances;
		RefVar exceptionList;
		RefVar exceptions;
		RefVar exception;
		RefVar replacement;
		while ((Ref) entry != NILREF)
		{
			instances = MakeArray(0);
			exceptionList = MakeArray(0);
			ULong mtgStart = RINT(GetFrameSlotRef(entry, RSSYMmtgstartdate));
			RINT(GetFrameSlotRef(entry, RSSYMmtgduration));			// checked to be an integer
			ULong mtgInfo = RINT(GetFrameSlotRef(entry, RSSYMmtginfo));
			ULong repeatType = RINT(GetFrameSlotRef(entry, RSSYMrepeattype));
			ULong mtgStop = RINT(GetFrameSlotRef(entry, RSSYMmtgstopdate));
			templateDate.InitWithMinutes(mtgStart);
			exceptions = GetFrameSlotRef(entry, RSSYMexceptions);
			if ((Ref) exceptions != NILREF)
			{
				long count = Length(exceptions);
				for (long i = 0; i < count; i++)
				{
					exception = GetArraySlotRef(exceptions, i);
					replacement = GetArraySlotRef(exception, 1);
					if ((Ref) replacement != NILREF)
						SetFrameSlot(replacement, RSSYMrepeattemplate, entry);
					FBInsert(RefVar(NILREF), exceptionList, exception, RSSYM_3C, RefVar(MAKEINT(0)), RefVar(NILREF));
				}
			}
			if (mtgStart < end && start < mtgStop)
			{
				date = rangeStart;
				if (mtgStart % kMinutesPerDay < start % kMinutesPerDay)
				{
					date.fDate++;
					date.CleanUpFields();
				}
				date.fHour = templateDate.fHour;
				date.fMinute = templateDate.fMinute;
				ULong when;
				do
				{
					NextMeeting(&date, 0, mtgInfo, repeatType);
					when = date.TotalMinutes();
					if (when > mtgStop)
						break;
					if (mtgStart <= when && when < end)
					{
						AddArraySlot(instances, RefVar(MakeMeetingFrame(when, entry)));
						if (unique)
							break;
					}
					date.fDate++;
					date.CleanUpFields();
					if ((ULong) date.fDate > (ULong) date.DaysInMonth())
					{
						date.fDate++;
						date.CleanUpFields();
					}
				} while (when < end);
			}
			MergeMeetingLists(meetings, instances, exceptionList, start, end);
			entry = CursorNext(cursor);
		}
	}
	newton_catch_all
	{
		FixupRepeatFrame(entry);
	}
	end_try;
}


// ROM 0x0008b6bc GetAllMeetings__FRC6RefVarT1lT3Uc
// The meetings in [start, end): the meeting soup's entries by mtgStartDate
// (dateQuerySpec) and the repeating-meeting soup's instances (its
// templates by mtgStopDate, repeatQuerySpec); nil when there are none.
Ref
GetAllMeetings(RefArg meetingSoup, RefArg repeatSoup, long start, long end, Boolean unique)
{
	RefVar cursor;
	RefVar meetings(MakeArray(0));
	if ((Ref) meetingSoup != NILREF)
	{
		RefVar spec(Clone(RefVar(Rdatequeryspec)));
		SetFrameSlot(spec, RSSYMbeginkey, RefVar(MAKEINT(start)));
		SetFrameSlot(spec, RSSYMendexclkey, RefVar(MAKEINT(end)));
		cursor = SoupQuery(meetingSoup, spec);
		GetInstanceMeetings(meetings, cursor);
	}
	if ((Ref) repeatSoup != NILREF)
	{
		cursor = SoupQuery(repeatSoup, RefVar(Rrepeatqueryspec));
		GetRepeatingMeetings(meetings, cursor, start, end, unique);
	}
	if (Length(meetings) == 0)
		meetings = NILREF;
	return meetings;
}


/*------------------------------------------------------------------------------
	N e w t o n S c r i p t
------------------------------------------------------------------------------*/

// ROM 0x0008b824 FGetAllMeetings__FRC6RefVarN41
// GetAllMeetings(meetingSoup, repeatSoup, start, end): end nil is a day.
static Ref
FGetAllMeetings(RefArg /*rcvr*/, RefArg meetingSoup, RefArg repeatSoup, RefArg start, RefArg end)
{
	Long from = RINT(start);
	long to = ((Ref) end == NILREF) ? from + kMinutesPerDay : RINT(end);
	return GetAllMeetings(meetingSoup, repeatSoup, from, to, false);
}


// ROM 0x0008b8a0 FGetAllMeetingsUnique__FRC6RefVarN41
// The same with one instance per template.
static Ref
FGetAllMeetingsUnique(RefArg /*rcvr*/, RefArg meetingSoup, RefArg repeatSoup, RefArg start, RefArg end)
{
	Long from = RINT(start);
	long to = ((Ref) end == NILREF) ? from + kMinutesPerDay : RINT(end);
	return GetAllMeetings(meetingSoup, repeatSoup, from, to, true);
}


// ROM 0x0008bcc8 FNextMeeting__FRC6RefVarN21
// NextMeeting(startTime, repeatTemplate): the time of the template's first
// instance at or after startTime that is not an exception (a replacement
// meeting in between counts as the instance); 0 past the stop date, or
// when the template is malformed (then repaired).
static Ref
FNextMeeting(RefArg /*rcvr*/, RefArg startTime, RefArg repeatTemplate)
{
	volatile long result = 0;
	newton_try
	{
		Long start = RINT(startTime);
		Long mtgStart = RINT(GetFrameSlotRef(repeatTemplate, RSSYMmtgstartdate));
		ULong mtgInfo = RINT(GetFrameSlotRef(repeatTemplate, RSSYMmtginfo));
		ULong repeatType = RINT(GetFrameSlotRef(repeatTemplate, RSSYMrepeattype));
		Long mtgStop = RINT(GetFrameSlotRef(repeatTemplate, RSSYMmtgstopdate));
		RefVar exceptions(GetFrameSlotRef(repeatTemplate, RSSYMexceptions));
		TDate date((mtgStart < start) ? start : mtgStart);
		TDate templateDate(mtgStart);
		date.fHour = templateDate.fHour;
		date.fMinute = templateDate.fMinute;
		Boolean advance = mtgStart < start && (mtgStart % kMinutesPerDay < start % kMinutesPerDay);
		long when;
		long found;
		for (;;)
		{
			if (advance)
			{
				date.fDate++;
				date.CleanUpFields();
			}
			advance = true;
			NextMeeting(&date, 0, mtgInfo, repeatType);
			when = date.TotalMinutes();
			if (mtgStop < when)
				when = kNoMeetingTime;
			found = FindExceptionMeetingInRange(exceptions, start, when);
			if (found != 0)
				break;
		}
		if (found == -1)
			result = (when == kNoMeetingTime) ? 0 : when;
		else
			result = found;
	}
	newton_catch_all
	{
		FixupRepeatFrame(repeatTemplate);
		result = 0;
	}
	end_try;
	return MAKEINT(result);
}


// ROM 0x0008bf28 FPrevMeeting__FRC6RefVarN21
// PrevMeeting(startTime, repeatTemplate): the time of the template's last
// instance before startTime (0 when there is none): a step back with
// simplePrevMeeting, checked and refined forward with NextMeeting so that
// the exceptions are honoured.
static Ref
FPrevMeeting(RefArg rcvr, RefArg startTime, RefArg repeatTemplate)
{
	volatile long result = 0;
	newton_try
	{
		Long start = RINT(startTime);
		Long mtgStart = RINT(GetFrameSlotRef(repeatTemplate, RSSYMmtgstartdate));
		Long mtgStop = RINT(GetFrameSlotRef(repeatTemplate, RSSYMmtgstopdate));
		ULong mtgInfo = RINT(GetFrameSlotRef(repeatTemplate, RSSYMmtginfo));
		ULong repeatType = RINT(GetFrameSlotRef(repeatTemplate, RSSYMrepeattype));
		Long first = RINT(FNextMeeting(rcvr, RefVar(MAKEINT(0)), repeatTemplate));
		if (first <= start)
		{
			TDate date((start < mtgStop) ? start : mtgStop);
			TDate templateDate(mtgStart);
			date.fHour = templateDate.fHour;
			date.fMinute = templateDate.fMinute;
			if (start % kMinutesPerDay < mtgStart % kMinutesPerDay)
			{
				date.fDate--;
				date.CleanUpFields();
			}
			long candidate = 0;
			for (;;)
			{
				simplePrevMeeting(&date, mtgInfo, repeatType);
				long when = date.TotalMinutes();
				if (when < mtgStart)
					when = 0;
				Long next = RINT(FNextMeeting(rcvr, RefVar(MAKEINT(when)), repeatTemplate));
				if (next < start && next != 0)
				{
					candidate = next;
					break;
				}
				if (when == 0)
					break;
				date.fDate--;
				date.CleanUpFields();
			}
			if (candidate != 0)
			{
				for (;;)
				{
					Long next = RINT(FNextMeeting(rcvr, RefVar(MAKEINT(candidate + 1)), repeatTemplate));
					if (start <= next || next == 0)
						break;
					candidate = (candidate == next) ? next + kMinutesPerDay : next;
				}
			}
			result = candidate;
		}
	}
	newton_catch_all
	{
		FixupRepeatFrame(repeatTemplate);
		result = 0;
	}
	end_try;
	return MAKEINT(result);
}


// ROM 0x0008c248 FGetNextMeetingTime__FRC6RefVarN21
// GetNextMeetingTime(repeatTemplate, startTime): NextMeeting the other way round.
static Ref
FGetNextMeetingTime(RefArg /*rcvr*/, RefArg repeatTemplate, RefArg startTime)
{
	return FNextMeeting(RefVar(NILREF), startTime, repeatTemplate);
}


void
RegisterMeetingNatives(void)
{
	RegisterNativeFunction("FGetAllMeetings__FRC6RefVarN41", (void*) FGetAllMeetings, 4);
	RegisterNativeFunction("FGetAllMeetingsUnique__FRC6RefVarN41", (void*) FGetAllMeetingsUnique, 4);
	RegisterNativeFunction("FNextMeeting__FRC6RefVarN21", (void*) FNextMeeting, 2);
	RegisterNativeFunction("FPrevMeeting__FRC6RefVarN21", (void*) FPrevMeeting, 2);
	RegisterNativeFunction("FGetNextMeetingTime__FRC6RefVarN21", (void*) FGetNextMeetingTime, 2);
	InitMeetingPrototypes();
}


// Host: the ROM's query specs (dateQuerySpec 0x6297e1: type 'index,
// indexPath 'mtgStartDate, beginKey 0, endExclKey nil; repeatQuerySpec
// 0x62e551: indexPath 'mtgStopDate, beginKey 0) and the instance
// prototype (0x50f82d) when the ROM's objects are not imported.
void
InitMeetingPrototypes(void)
{
	if (Rdatequeryspec != NILREF)
		return;
	AddGCRoot(Rdatequeryspec);
	AddGCRoot(Rrepeatqueryspec);
	AddGCRoot(Rprotoinstanceofrepeatingmeeting);
	RefVar spec(AllocateFrame());
	SetFrameSlot(spec, RSSYMtype, RSSYMindex);
	SetFrameSlot(spec, RSSYMindexpath, RSSYMmtgstartdate);
	SetFrameSlot(spec, RSSYMbeginkey, RefVar(MAKEINT(0)));
	SetFrameSlot(spec, RSSYMendexclkey, RefVar(NILREF));
	Rdatequeryspec = spec;
	spec = AllocateFrame();
	SetFrameSlot(spec, RSSYMtype, RSSYMindex);
	SetFrameSlot(spec, RSSYMindexpath, RSSYMmtgstopdate);
	SetFrameSlot(spec, RSSYMbeginkey, RefVar(MAKEINT(0)));
	Rrepeatqueryspec = spec;
	RefVar proto(AllocateFrame());
	SetFrameSlot(proto, RSSYMviewstationery, RSSYMrepeatingmeeting);
	SetFrameSlot(proto, RSSYMclass, RSSYMmeeting);
	SetFrameSlot(proto, RSSYMmtgstartdate, RefVar(NILREF));
	SetFrameSlot(proto, RSSYMrepeattemplate, RefVar(NILREF));
	Rprotoinstanceofrepeatingmeeting = proto;
}
