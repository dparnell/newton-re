/*
	File:		intl/Meetings.h

	Contains:	The repeating meetings: the Dates application's engine for
				meetings that repeat (a repeat template entry holds the
				first meeting's mtgStartDate, its mtgDuration, a repeatType
				and the mtgInfo word that encodes the days, weeks, dates or
				period it repeats on, the mtgStopDate and an exceptions
				array of [date, entry] pairs - a replaced or deleted
				instance), stepping a TDate to the next or previous instance
				and collecting the instances in a range together with the
				plain meetings of a soup (the NewtonScript GetAllMeetings,
				NextMeeting, PrevMeeting, GetNextMeetingTime).

	The ROM keeps these with TDate (0x0008bc98-0x0008d910); each function
	cites its origin.
*/

#ifndef __MEETINGS_H
#define __MEETINGS_H

#ifndef __DATES_H
#include "Dates.h"
#endif

// repeatType
enum
{
	kDayOfWeek = 0,			// mtgInfo: the day-of-week bits and the week-in-month bits
	kWeekInMonth = 1,		// the same
	kDateInMonth = 2,		// mtgInfo: the date, in the low 6 bits
	kDateInYear = 3,		// mtgInfo: (month << 8) | date
	kPeriod = 4,			// mtgInfo: (the first instance's day number << 8) | the period in days
	kNever = 5,
	kWeekInYear = 7			// mtgInfo: (month << 12) | the day-of-week and week bits
};

// mtgInfo bits
const ULong kSunday = 0x800;
const ULong kMonday = 0x400;
const ULong kTuesday = 0x200;
const ULong kWednesday = 0x100;
const ULong kThursday = 0x80;
const ULong kFriday = 0x40;
const ULong kSaturday = 0x20;
const ULong kEveryDay = 0xfe0;
const ULong kFirstWeek = 0x10;
const ULong kSecondWeek = 0x08;
const ULong kThirdWeek = 0x04;
const ULong kFourthWeek = 0x02;
const ULong kLastWeek = 0x01;
const ULong kEveryWeek = 0x1f;

const long kForeverStopDate = 0x1fffffff;			// the mtgStopDate of a meeting that never stops
const long kNoMeetingTime = 0x1ffffffe;				// NextMeeting's time past the stop date

// stepping a date to the next instance on or after it (the time of day untouched)
void	NextDayOfWeek(TDate* date, ULong mtgInfo);
void	NextDateOfMonth(TDate* date, ULong mtgInfo);
void	NextDateOfYear(TDate* date, ULong mtgInfo);
void	NextDateByWeekInYear(TDate* date, ULong mtgInfo);
void	NextPeriod(TDate* date, ULong unused, ULong mtgInfo);
void	NextMeeting(TDate* date, ULong unused, ULong mtgInfo, ULong repeatType);
void	simplePrevMeeting(TDate* date, ULong mtgInfo, ULong repeatType);

// the instances
Ref		MakeMeetingFrame(ULong minutes, RefArg repeatTemplate);
void	FixupRepeatFrame(RefArg entry);								// missing integer slots defaulted, bad exceptions dropped, the entry changed
long	FindExceptionMeetingInRange(RefArg exceptions, long start, long end);	// -1 none, 0 the meeting at end is an exception, else the earliest exception meeting's time in the range
void	GetInstanceMeetings(RefArg meetings, RefArg cursor);
void	GetRepeatingMeetings(RefArg meetings, RefArg cursor, ULong start, ULong end, Boolean unique);
Ref		GetAllMeetings(RefArg meetingSoup, RefArg repeatSoup, long start, long end, Boolean unique);

void	RegisterMeetingNatives(void);		// (a separate library over the stores: the host's boot registers them after InitQueries)
void	InitMeetingPrototypes(void);		// host: the query specs and the instance prototype when no ROM objects are imported

#endif	/* __MEETINGS_H */
