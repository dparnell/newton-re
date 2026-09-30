/*
	File:		views/MonthView.cpp

	Contains:	TMonthView, the calendar (MonthView.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "MonthView.h"
#include "Inker.h"			// BusyBoxSend
#include "ViewFlags.h"
#include "Commands.h"
#include "RootView.h"
#include "Locale.h"
#include "Text.h"
#include "Fonts.h"
#include "Shapes.h"
#include "Screen.h"
#include "Rects.h"
#include "RegionVars.h"
#include "Unicode.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "UnitPublic.h"
#include "Stroke.h"
#include "Frames.h"
#include "Interpreter.h"
#include "Ports.h"
#include "Pictures.h"
#include "MeetingView.h"
#include "Meetings.h"

#include <string.h>


// ROM 0x00120414 ClassID__10TMonthViewCFv
long
TMonthView::ClassID(void) const
{
	return clMonthView;
}


// ROM 0x0012041c DerivedFrom__10TMonthViewCFl
Boolean
TMonthView::DerivedFrom(long id) const
{
	return id == clMonthView || TView::DerivedFrom(id);
}


// The column the first of the month falls in: how far the first day of
// the month is past the first day of the week.  The ROM works this out
// again wherever it is wanted rather than keeping it.
long
TMonthView::FirstColumn(void) const
{
	long offset = (fDate.fDayOfWeek - fFirstDayOfWeek + 7) % 7;
	return offset;
}


// ROM 0x00120f88 Constructor__10TMonthViewFRC6RefVarP5TView
// The week's first day comes from the view, else the user's preference,
// else the locale; the month from the first of `selectedDates`.
void
TMonthView::Constructor(RefArg context, TView* parent)
{
	TView::Constructor(context, parent);
	fNoSelection = NOTNIL(RefVar(GetVar(RSSYMnoselection)));
	fUnused34 = 2;
	RefVar firstDay(GetVar(RSSYMfirstdayofweek));
	if (ISNIL(firstDay))
	{
		firstDay = GetPreference(RSSYMfirstdayofweek);
		if (ISNIL(firstDay))
		{
			RefVar locale(GetCurrentLocale());
			firstDay = GetProtoVariable(locale, RSSYMfirstdayofweek, nil);
		}
	}
	fFirstDayOfWeek = ISNIL(firstDay) ? 0 : RINT(firstDay);
	fMeetingOverview = NOTNIL(RefVar(GetProto(RSSYMmeetingoverview)));
	RefVar dates(GetVar(RSSYMselecteddates));
	ULong minutes = ISNIL(dates) ? 0 : (ULong) RINT(RefVar(GetArraySlotRef(dates, 0)));
	fDate.InitWithMinutes(minutes);
}


// ROM 0x00120d64 DateRect__10TMonthViewFR5TRectl
// The cell a day of the month is in.  An overview's cells meet, an
// ordinary month's are a pixel apart.
void
TMonthView::DateRect(Rect& rect, long date)
{
	long index = FirstColumn() + date - 1;
	long row = index / 7;
	long column = index % 7;
	rect.left = (short) (fCellWidth * column + fGridRect.left);
	rect.top = (short) (fCellHeight * row + fGridRect.top + 1);
	rect.right = (short) (rect.left + fCellWidth);
	rect.bottom = (short) (rect.top + fCellHeight);
	if (fMeetingOverview == 0)
		rect.right = (short) (rect.right - 1);
}


// ROM 0x00120ea0 PointToDate__10TMonthViewFR6TPoint
// The day a point is over, pinned to the grid: a point past the last
// column or row answers the day at the edge, so a pen dragged off the
// calendar still picks something.
long
TMonthView::PointToDate(Point& pt)
{
	long column = (pt.h - fGridRect.left) / fCellWidth;
	long row = (pt.v - fGridRect.top) / fCellHeight;
	long offset = FirstColumn();
	long lastRow = (ULong) (fDate.DaysInMonth() + offset) < 36 ? 4 : 5;
	if (column < 0)
		column = 0;
	else if (column > 6)
		column = 6;
	if (row < 0)
		row = 0;
	if (row > lastRow)
		row = lastRow;
	return row * 7 + column - offset + 1;
}


// ROM 0x00120e54 InvertSelection__10TMonthViewFv
// The selection turned over: one rounded rectangle, and a second when the
// range runs onto another week.
void
TMonthView::InvertSelection(void)
{
	InvertRoundRect(&fRangeRect, 4, 4);
	if (!EmptyRect(&fRangeRect2))
		InvertRoundRect(&fRangeRect2, 4, 4);
}


// ROM 0x00120b74 UpdateRangeRect__10TMonthViewFlT1
// The rectangles a range of days covers.  A range inside one week is one
// rectangle; a range that runs on is two - the first day's week from that
// day to the end of the row, and the last day's from the start of its row
// to it.  (The rows in between are not covered: that is the ROM's, and
// the ranges it is asked for are a week at most.)
void
TMonthView::UpdateRangeRect(long first, long last)
{
	long firstColumn = (FirstColumn() + first - 1) % 7;
	long lastColumn = (FirstColumn() + last - 1) % 7;
	Rect firstRect, lastRect;
	DateRect(firstRect, first);
	DateRect(lastRect, last);
	SetRect(&fRangeRect2, 0, 0, 0, 0);

	if (last == first)
	{
		fRangeRect = firstRect;
		return;
	}
	if (last > first && lastColumn >= firstColumn)
	{
		// the same row, forwards
		fRangeRect.left = firstRect.left;
		fRangeRect.top = firstRect.top;
		fRangeRect.right = lastRect.right;
		fRangeRect.bottom = lastRect.bottom;
		return;
	}
	if (last < first && lastColumn <= firstColumn)
	{
		// the same row, dragged backwards
		fRangeRect.left = lastRect.left;
		fRangeRect.top = lastRect.top;
		fRangeRect.right = firstRect.right;
		fRangeRect.bottom = firstRect.bottom;
		return;
	}

	// two rows: the first day's row to its end, and the other row
	Rect rowStart, rowEnd;
	DateRect(rowStart, first - firstColumn);
	DateRect(rowEnd, first + (6 - firstColumn));
	Rect* other;
	if (last < first)
	{
		fRangeRect = lastRect;
		other = &firstRect;
	}
	else
	{
		fRangeRect = firstRect;
		other = &lastRect;
	}
	fRangeRect.right = rowEnd.right;
	fRangeRect2 = *other;
	fRangeRect2.left = rowStart.left;
}


// ROM 0x00120a60 UpdateFrame__10TMonthViewFlT1
// The range of days written back as `selectedDates` - the minutes of
// each day in it.  A range that runs over a week's end is taken as the
// block of columns it spans, not as the days between.
void
TMonthView::UpdateFrame(long first, long last)
{
	RefVar dates(MakeArray(0));
	SetVariable(fContext, RSSYMselecteddates, dates);
	if (last < first)
	{
		long swap = first;
		first = last;
		last = swap;
	}
	long rows = (last - first) / 7;
	long columns = (last - first) % 7;
	TDate date;
	date.fYear = fDate.fYear;
	date.fMonth = fDate.fMonth;
	for (long row = 0; row <= rows; row++)
	{
		for (long column = 0; column <= columns; column++)
		{
			date.fDate = row * 7 + first + column;
			AddArraySlot(dates, RefVar(MAKEINT(date.TotalMinutes())));
		}
	}
}


// ROM 0x00121ff8 DrawLabels__10TMonthViewFv
// The letter of each weekday along the top, starting at the week's first
// day, lowercased and centred over its column.  A view with no labelFont
// has no labels.
void
TMonthView::DrawLabels(void)
{
	if (ISNIL(RefVar((Ref) fLabelFont)))
		return;
	TextOptions options;
	memset(&options, 0, sizeof(options));
	options.fAlignment = 0x8000;					// centred
	options.fWidth = ToFixed(fCellWidth);
	StyleRecord style;
	style.fFontPattern = 0;		// (the ROM clears them before asking)
	style.fPattern = nil;
	CreateTextStyleRecord(fLabelFont, &style);
	StyleRecord* styles = &style;

	TDate day;
	day.fDayOfWeek = fFirstDayOfWeek % 7;
	for (long i = 0; i < 7; i++)
	{
		UniChar letter[2];		// (the name is cut to one, and terminated)
		day.DateElementString(2, 4, letter, 1, true);
		LowercaseText(letter, 1);
		FPoint at;
		at.x = ToFixed(i * fCellWidth + fLabelRect.left);
		at.y = ToFixed(fLabelRect.bottom);
		DrawTextOnce(letter, 1, &styles, nil, at, &options, nil);
		day.fDayOfWeek = (day.fDayOfWeek + 1) % 7;
	}
	DisposeStyleRecord(&style);
}


// ROM 0x00120450 DrawDates__10TMonthViewFv
// The number of each day in its cell, centred, on the baseline that sits
// the text half way down the cell.  The tens digit of a day below ten is
// a space rather than a zero, so that every date is two characters wide
// and they line up.
void
TMonthView::DrawDates(void)
{
	TextOptions options;
	memset(&options, 0, sizeof(options));
	options.fAlignment = 0x8000;					// centred
	options.fWidth = ToFixed(fCellWidth);
	options.fTransferMode = srcOr;
	StyleRecord style;
	style.fFontPattern = 0;
	style.fPattern = nil;
	CreateTextStyleRecord(ISNIL(RefVar((Ref) fDatesFont)) ? RefVar(Rfontsystem9) : fDatesFont, &style);
	StyleRecord* styles = &style;
	FontInfo info;
	GetStyleFontInfo(&style, &info);

	long height = fCellHeight;
	long baseline = (short) (info.ascent + (height - info.ascent) / 2);
	if (height < baseline)
		baseline = height;
	baseline = (short) baseline;

	long days = fDate.DaysInMonth();
	long cell = FirstColumn();
	for (long day = 0; day < days; )
	{
		UniChar text[2];
		text[0] = U_CONST_CHAR(day < 9 ? ' ' : (unsigned char) ((day + 1) / 10) + '0');
		day = day + 1;
		text[1] = U_CONST_CHAR((unsigned char) (day % 10) + '0');
		FPoint at;
		at.x = ToFixed(fCellWidth * (cell % 7) + fGridRect.left);
		at.y = ToFixed(fCellHeight * (cell / 7) + fGridRect.top + baseline);
		DrawTextOnce(text, 2, &styles, nil, at, &options, nil);
		cell++;
	}
	DisposeStyleRecord(&style);
}


// ROM 0x00122ba4 DrawMeetingOverviewLine__FlN31RC5TRect
// A meeting as a black bar down a day's box, from where its start falls
// to where its end does (minutes after the day's start), below the top
// margin.  A box tall enough for a pixel to be under half an hour takes
// the day evenly; a smaller one squeezes the night into less room and
// gives the working day, 7 am to 7 pm, what is left: the hours before
// 7 am in the top half of what is not the day's 24 pixels (or of all but
// two when that leaves nothing), the working day in those 24 at half an
// hour a pixel, and the evening under them.  The bar is at least a pixel.
static void
DrawMeetingOverviewLine(long start, long end, long dayStart, long margin, const Rect& box)
{
	long left = box.left;
	long width = box.right - left;
	long height = (short) (box.bottom - box.top - margin - 4);
	long top = (short) (box.top + margin + 2);
	long from = start - dayStart;
	long to = end - dayStart;
	long perPixel = (height / 2 + 1440) / height;		// minutes a pixel, rounded
	Rect bar;
	bar.left = (short) (left + 2);
	bar.right = (short) (box.left + width - 2);
	if (perPixel < 31)
	{
		bar.top = (short) (from / perPixel + top);
		bar.bottom = (short) (to / perPixel + top);
	}
	else
	{
		long dayScale = 30;					// the working day: half an hour a pixel
		long dayPixels = 24;
		long night = (height - 24) / 2;
		if (night < 1)
		{
			night = 1;
			dayPixels = height - 2;
			dayScale = 720 / dayPixels;
		}
		long morningScale = 420 / night;	// midnight to 7 am
		long eveningScale = 300 / night;	// 7 pm to midnight
		long offset = 0;
		long scale = morningScale;
		if (from >= 420)
		{
			if (from >= 1140)
			{
				from -= 1140;
				scale = eveningScale;
				offset = night + dayPixels;
			}
			else
			{
				from -= 420;
				scale = dayScale;
				offset = night;
			}
		}
		bar.top = (short) (from / scale + top + offset);
		offset = 0;
		scale = morningScale;
		if (to >= 420)
		{
			if (to >= 1140)
			{
				to -= 1140;
				scale = eveningScale;
				offset = night + dayPixels;
			}
			else
			{
				to -= 420;
				scale = dayScale;
				offset = night;
			}
		}
		bar.bottom = (short) (to / scale + top + offset);
	}
	if (bar.bottom < bar.top + 1)
		bar.bottom = (short) (bar.top + 1);
	FillRect(&bar, GetStdPattern(blackPat));
}


// ROM 0x00122ddc DrawDayNoteIcon__FRC6RefVarlRC5TRect
// The index'th note of a day as its icon, 23 by 16 a pixel in from the
// box's top left corner and 24 along for each before it - when it fits.
static void
DrawDayNoteIcon(RefArg icon, long index, const Rect& box)
{
	Rect r;
	r.top = (short) (box.top + 1);
	r.left = (short) (box.left + index * 24 + 1);
	r.right = (short) (r.left + 23);
	r.bottom = (short) (r.top + 16);
	if (r.right <= box.right)
		DrawBitmap(icon, &r, 0);
}


// ROM 0x00122e6c DrawDayNoteGlyphs__FlRC5TRectT1
// A small box's notes as a row of little flags along its top, nine
// pixels apart, as many as fit short of the number in the corner.  (The
// margin it is passed is not used.)
static void
DrawDayNoteGlyphs(long count, const Rect& box, long /*margin*/)
{
	RefVar flag(Clone(RefVar(Rsmallflagbitmap)));
	short limit = (short) (box.right - 14);
	Rect r;
	r.left = (short) (box.left + 4);
	r.right = (short) (r.left + 7);
	r.top = (short) (box.top + 2);
	r.bottom = (short) (r.top + 6);
	for (long i = 0; i < count; i++)
	{
		if (limit < r.right)
			break;
		DrawBitmap(flag, &r, 0);
		r.left = (short) (r.left + 9);
		r.right = (short) (r.left + 7);
	}
}


// whether a meeting of a GetAllMeetings list is one the overview draws: a
// meeting, a repeating one or an exception to one (not an event)
static Boolean
IsOverviewMeeting(RefArg meeting)
{
	RefVar stationery(GetFrameSlotRef(meeting, RSSYMviewstationery));
	return EQRef(stationery, RSSYMmeeting) || EQRef(stationery, RSSYMrepeatingmeeting)
		|| EQRef(stationery, RSSYMexceptionmeeting);
}


// ROM 0x00122174 DrawMonthOverView__10TMonthViewFv
// The Dates app's month overview.  The meetings and the notes of the whole
// month are asked for once (GetAllMeetings over the context's MeetingSoup
// and RepeatSoup, and its Notes and RepeatNotes); then each day in turn is
// a gray-framed box a pixel bigger than its cell, with a black bar for
// each meeting that starts in it (the lists are in time order, so a
// meeting is drawn once its start is in the day, and struck off), the
// day's notes - as their icons when the boxes are more than 47 pixels
// tall, as little flags otherwise - and its number right-aligned at the
// top, nine pixels down.  The bars start 9 pixels down in a small box and
// 16 in a big one.
//
// When asking for the month throws, each day is asked for its own.  ROM
// bug kept: that path draws every meeting of the day with the start and
// end of the *month's* first meeting - which it never found, the asking
// having failed - so the bars all fall past the bottom of the box.
void
TMonthView::DrawMonthOverView(void)
{
	TextOptions options;
	memset(&options, 0, sizeof(options));
	options.fAlignment = ToFixed(1);				// right-aligned
	options.fWidth = ToFixed(fCellWidth);
	options.fTransferMode = srcOr;
	StyleRecord style;
	style.fFontPattern = 0;
	style.fPattern = nil;
	CreateTextStyleRecord(ISNIL(RefVar((Ref) fDatesFont)) ? RefVar(Rfontsystem9) : fDatesFont, &style);
	StyleRecord* styles = &style;

	long days = fDate.DaysInMonth();
	long cell = FirstColumn();
	long dayStart = fDate.TotalMinutes();
	long dayEnd = dayStart + 1440;
	RefVar meetingSoup(GetVariable(fContext, RSSYMmeetingsoup, nil, 0));
	RefVar repeatSoup(GetVariable(fContext, RSSYMrepeatsoup, nil, 0));
	RefVar notesSoup(GetVariable(fContext, RSSYMnotes, nil, 0));
	RefVar repeatNotesSoup(GetVariable(fContext, RSSYMrepeatnotes, nil, 0));
	RefVar item;
	long meetingCount = 0;
	long meetingIndex = 0;
	long noteCount = 0;
	long nextStart = 0x1fffffff;			// the next meeting to draw: its start and end
	long nextEnd = 0;
	long noteIndex = 0;
	long nextNote = 0x1fffffff;			// the next note's start
	Boolean big = fCellHeight > 47;
	long margin = big ? 16 : 9;
	RefVar meetings;
	RefVar notes;
	Boolean whole = true;					// the month's lists were had in one go
	newton_try
	{
		long monthEnd = dayStart + days * 1440;
		meetings = GetAllMeetings(meetingSoup, repeatSoup, dayStart, monthEnd, false);
		notes = GetAllMeetings(notesSoup, repeatNotesSoup, dayStart, monthEnd, false);
	}
	newton_catch_all
	{
		whole = false;
		meetings = NILREF;
		notes = NILREF;
	}
	end_try;
	EraseRect(&fGridRect);
	if (NOTNIL(meetings))
	{
		meetingCount = Length(meetings);
		for (meetingIndex = 0; meetingIndex < meetingCount; meetingIndex++)
		{
			item = GetArraySlotRef(meetings, meetingIndex);
			if (IsOverviewMeeting(item))
			{
				nextStart = RINT(GetMeetingSlot(item, RSSYMmtgstartdate));
				nextEnd = RINT(GetMeetingSlot(item, RSSYMmtgduration)) + nextStart;
				break;
			}
		}
	}
	if (NOTNIL(notes))
	{
		noteCount = Length(notes);
		nextNote = RINT(GetMeetingSlot(RefVar(GetArraySlotRef(notes, 0)), RSSYMmtgstartdate));
	}
	for (long day = 0; day < days; )
	{
		Rect box;
		box.left = (short) (fCellWidth * (cell % 7) + fGridRect.left);
		box.top = (short) (fCellHeight * (cell / 7) + fGridRect.top);
		box.right = (short) (box.left + fCellWidth + 1);
		box.bottom = (short) (fCellHeight + box.top + 1);
		PatternHandle was = GetFgPattern();
		SetFgPattern(GetStdPattern(grayPat));
		FrameRect(&box);
		SetFgPattern(was);
		cell++;
		long drawn = 0;						// the day's notes
		if (!whole)
		{
			RefVar dayMeetings(GetAllMeetings(meetingSoup, repeatSoup, dayStart, dayEnd, false));
			long count = ISNIL(dayMeetings) ? 0 : Length(dayMeetings);
			for (long i = 0; i < count; i++)
			{
				item = GetArraySlotRef(dayMeetings, i);
				if (IsOverviewMeeting(item))
					DrawMeetingOverviewLine(nextStart, nextEnd, dayStart, margin, box);		// (ROM bug: see above)
			}
			notes = GetAllMeetings(notesSoup, repeatNotesSoup, dayStart, dayEnd, false);
			noteCount = ISNIL(notes) ? 0 : Length(notes);
			for (drawn = 0; drawn < noteCount; drawn++)
				if (big)
				{
					item = GetArraySlotRef(notes, drawn);
					DrawDayNoteIcon(RefVar(FGetMeetingIcon(RefVar(NILREF), item)), drawn, box);
				}
		}
		else
		{
			while (nextStart < dayEnd)
			{
				DrawMeetingOverviewLine(nextStart, nextEnd, dayStart, margin, box);
				SetArraySlotRef(meetings, meetingIndex, NILREF);
				nextStart = 0x1fffffff;
				for (meetingIndex = meetingIndex + 1; meetingIndex < meetingCount; meetingIndex++)
				{
					item = GetArraySlotRef(meetings, meetingIndex);
					if (IsOverviewMeeting(item))
					{
						nextStart = RINT(GetMeetingSlot(item, RSSYMmtgstartdate));
						nextEnd = RINT(GetMeetingSlot(item, RSSYMmtgduration)) + nextStart;
						break;
					}
				}
			}
			item = NILREF;
			while (nextNote < dayEnd)
			{
				if (big)
				{
					if (ISNIL(item))
						item = GetArraySlotRef(notes, noteIndex);
					DrawDayNoteIcon(RefVar(FGetMeetingIcon(RefVar(NILREF), item)), drawn, box);
				}
				drawn++;
				noteIndex++;
				if (noteIndex < noteCount)
				{
					item = GetArraySlotRef(notes, noteIndex);
					nextNote = RINT(GetMeetingSlot(item, RSSYMmtgstartdate));
				}
				else
					nextNote = 0x1fffffff;
			}
		}
		if (!big && drawn > 0)
			DrawDayNoteGlyphs(drawn, box, margin);
		dayStart += 1440;
		dayEnd += 1440;
		UniChar text[2];
		text[0] = U_CONST_CHAR(day < 9 ? ' ' : (unsigned char) ((day + 1) / 10) + '0');
		day = day + 1;
		text[1] = U_CONST_CHAR((unsigned char) (day % 10) + '0');
		FPoint at;
		at.x = ToFixed(box.left);
		at.y = ToFixed(box.top + 9);
		DrawTextOnce(text, 2, &styles, nil, at, &options, nil);
	}
	DisposeStyleRecord(&style);
}


// ROM 0x00121834 RealDraw__10TMonthViewFR5TRect
// The month worked out, then drawn.
//
// The bounds give two rectangles: the labels along the top nine pixels,
// and the grid under them.  The month itself comes from `selectedDates`:
// the first and last of them, and when they are in different months the
// context's own `month` (and `year`) says which of the two to show, the
// other end being folded into it by a month's worth of days.  The month
// and year are then written back onto the context, which is what the
// pickers around the calendar read.  The cells are what is left divided
// seven ways across and five or six down, depending on whether the month
// fits in five weeks.
void
TMonthView::RealDraw(Rect& /*bounds*/)
{
	fLabelRect.top = (short) (viewBounds.top + 1);
	fLabelRect.left = viewBounds.left;
	fLabelRect.bottom = (short) (viewBounds.top + 10);
	fLabelRect.right = (short) (viewBounds.right - 1);
	fGridRect.top = (short) (viewBounds.top + 13);
	fGridRect.left = (short) (viewBounds.left + 1);
	fGridRect.bottom = (short) (viewBounds.bottom - 1);
	fGridRect.right = (short) (viewBounds.right - 1);
	fCellWidth = (short) ((fGridRect.right - fGridRect.left + 2) / 7);

	RefVar dates(GetVariable(fContext, RSSYMselecteddates, nil, 0));
	long count = Length(dates);
	TDate first, last;
	first.InitWithMinutes((ULong) RINT(RefVar(GetArraySlotRef(dates, 0))));
	long year = first.fYear;
	long month = first.fMonth;
	if (count == 1)
		last = first;
	else
	{
		last.InitWithMinutes((ULong) RINT(RefVar(GetArraySlotRef(dates, count - 1))));
		if (last.fMonth != first.fMonth)
		{
			// the two ends are in different months: the context says which
			// one is on show, and the other end is counted into it
			RefVar shown(GetVariable(fContext, RSSYMmonth, nil, 0));
			month = ISNIL(shown) ? first.fMonth : RINT(shown);
			if (last.fMonth == month)
			{
				first.fDate -= first.DaysInMonth();
				year = last.fYear;
			}
			else if (first.fMonth == month)
			{
				last.fDate += first.DaysInMonth();
				year = first.fYear;
			}
			else
			{
				RefVar shownYear(GetVariable(fContext, RSSYMyear, nil, 0));
				year = ISNIL(shownYear) ? first.fYear : RINT(shownYear);
				first.fDate -= first.DaysInMonth();
				last.fMonth = month;
				last.fDate += last.DaysInMonth();
			}
		}
	}
	fDate.fDate = 1;
	fDate.fMonth = month;
	fDate.fYear = year;
	fDate.CleanUpFields();
	SetFrameSlot(fContext, RSSYMyear, RefVar(MAKEINT(fDate.fYear)));
	SetFrameSlot(fContext, RSSYMmonth, RefVar(MAKEINT(fDate.fMonth)));

	long rows = (ULong) (fDate.DaysInMonth() + FirstColumn()) < 36 ? 5 : 6;
	fCellHeight = (short) ((fGridRect.bottom - fGridRect.top) / rows);
	UpdateRangeRect(first.fDate, last.fDate);

	fDatesFont = GetVar(RSSYMdatesfont);
	fLabelFont = GetVar(RSSYMlabelfont);
	DrawLabels();
	if (fMeetingOverview == 0)
		DrawDates();
	else
		DrawMonthOverView();
	if (!fNoSelection)
		InvertSelection();
}


// ROM 0x001206b4 HandleClick__10TMonthViewFP13TStrokePublic
// The pen on the calendar.  In the grid it picks a day and follows the
// pen, inverting as it goes: with `singleDay` each new day replaces the
// last, and without it the days between are taken as a range (kept inside
// one week, the pen wrapping round rather than running on).  On the
// labels - and only when the view has labels and is not single-day -
// tapping a weekday letter picks that column down the month, four or five
// weeks of it.
//
// When the pen lets go the range becomes `selectedDates` and the
// `monthChangedScript` is run.  ==> whether anything was picked.
Boolean
TMonthView::HandleClick(TStrokePublic* stroke)
{
	if (fCellHeight == 0)
		return false;
	long offset = FirstColumn();
	Boolean singleDay = NOTNIL(RefVar(GetVar(RSSYMsingleday)));
	stroke->InkOff(true);
	BusyBoxSend(0x37);

	Point pt;
	pt = stroke->FirstPoint();
	long first = PointToDate(pt);
	long last = first;
	Boolean picked = false;

	TRegion visRgn(SetupVisRgn());
	TRegionVar saved(visRgn);
	if (PtInRect(pt, &fGridRect))
	{
		if (!fNoSelection)
			InvertSelection();
		DateRect(fRangeRect, first);
		SetRect(&fRangeRect2, 0, 0, 0, 0);
		InvertSelection();
		while (!stroke->Done())
		{
			pt = stroke->FinalPoint();
			long date = PointToDate(pt);
			if (!singleDay)
			{
				// a range stays inside the week the first day is in: a
				// pen that has run past either end of the row comes back
				// round to it
				long firstRow = (offset + first - 1) / 7;
				long row = (offset + date - 1) / 7;
				if (row != firstRow)
				{
					if ((first < date) ? (row >= firstRow) : (firstRow >= row))
					{
						if (date - first >= 7)
							date = date - ((date - first) / 7) * 7;
						else if (first - date >= 7)
							date = date + ((first - date) / 7) * 7;
					}
					else
						date = (firstRow - row) + first;
				}
			}
			if (date != last)
			{
				StartDrawing(nil, nil);
				InvertSelection();
				UpdateRangeRect(singleDay ? date : first, date);
				InvertSelection();
				StopDrawing(nil, nil);
				last = date;
			}
			Wait(1);
		}
		picked = true;
	}
	else if (PtInRect(pt, &fLabelRect))
	{
		if (!singleDay && NOTNIL(RefVar((Ref) fLabelFont)))
		{
			if (!fNoSelection)
				InvertSelection();
			long column = (pt.h - fLabelRect.left) / fCellWidth;
			long day = (column - offset + 7) % 7;
			first = day + 1;
			last = day + 29;
			if ((ULong) fDate.DaysInMonth() < (ULong) last)
				last = day + 22;
			UpdateRangeRect(first, last);
			InvertSelection();
			picked = true;
		}
	}

	GrafPort* port;
	GetPort(&port);
	CopyRgn(saved, port->visRgn);		// (the ROM's [port,#0x24] at 0x001209d4: the visRgn SetupVisRgn narrowed)
	if (picked)
	{
		UpdateFrame(singleDay ? last : first, last);
		RunScript(RSSYMmonthchangedscript, RefVar(NILREF), true, nil);
	}
	return picked;
}


// ROM 0x00120648 RealDoCommand__10TMonthViewFRC6RefVar
// A click is the calendar's own; anything else is the view's.
Boolean
TMonthView::RealDoCommand(RefArg cmd)
{
	if (CommandID(cmd) == aeClick)
	{
		TUnitPublic* unit = (TUnitPublic*) CommandParameter(cmd);
		Boolean handled = HandleClick(unit->Stroke());
		CommandSetResult(cmd, 1);
		if (handled)
			return handled;
	}
	return TView::RealDoCommand(cmd);
}
