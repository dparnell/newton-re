/*
	File:		views/MonthView.cpp

	Contains:	TMonthView, the calendar (MonthView.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "MonthView.h"
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
	style.fFontPattern = NILREF;		// (the ROM clears them before asking)
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
	style.fFontPattern = NILREF;
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


// ROM 0x00122174 DrawMonthOverView__10TMonthViewFv
// NOT YET RECONSTRUCTED: the Dates app's month overview, which draws a
// bar across each day that has meetings in it - it walks the meeting,
// repeat, note and repeat-note soups of the context and measures what it
// finds.  Until it is here an overview draws its dates like any other
// month, so the calendar is there to be read and tapped.
void
TMonthView::DrawMonthOverView(void)
{
	DrawDates();
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
	// (the ROM tells the busy box a calendar is being tracked -
	// BusyBoxSend 0x37 - which the views layer cannot reach from here)

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
	CopyRgn(saved, port->clipRgn);
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
