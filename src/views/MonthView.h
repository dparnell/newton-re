/*
	File:		views/MonthView.h

	Contains:	TMonthView (clMonthView, 80): the calendar - a month laid out
				as seven columns of days under a row of weekday letters, with
				the selected days inverted.  It is the grid in the Dates app,
				the one the Setup assistant asks today's date on, and the one
				behind every date picker.

				The month it shows is worked out from `selectedDates` (the
				minutes of each selected day) each time it draws, and written
				back onto the context as `month` and `year` - which is what
				the pickers around it read.  `firstDayOfWeek` (the view's,
				the user's preference, or the locale's) turns the days into
				columns; `labelFont` and `datesFont` draw them.  The pen
				picks a day or drags a range out (`HandleClick`), the range
				is turned back into `selectedDates` (`UpdateFrame`) and the
				`monthChangedScript` is run.

				A view whose `meetingOverview` proto slot is there is the
				Dates app's month overview, which draws a bar for each day
				that has meetings instead of the date itself.

				NOT YET RECONSTRUCTED: `DrawMonthOverView` (0x00122174),
				which needs the meeting and repeat soups of `intl/Meetings.h`
				- an overview draws its dates like any other month until it
				is here.

	Reconstructed from the MP2x00 US ROM (0x00120414-0x00122174); each
	function cites its origin.
*/

#ifndef __MONTHVIEW_H
#define __MONTHVIEW_H

#include "View.h"
#include "Dates.h"

class TStrokePublic;

class TMonthView : public TView
{
public:
	virtual long	ClassID(void) const;								// ROM 0x00120414 ClassID__10TMonthViewCFv
	virtual Boolean	DerivedFrom(long id) const;							// ROM 0x0012041c DerivedFrom__10TMonthViewCFl
	virtual void	Constructor(RefArg context, TView* parent);			// ROM 0x00120f88 Constructor__10TMonthViewFRC6RefVarP5TView
	virtual Boolean	RealDoCommand(RefArg cmd);							// ROM 0x00120648 RealDoCommand__10TMonthViewFRC6RefVar
	virtual void	RealDraw(Rect& bounds);								// ROM 0x00121834 RealDraw__10TMonthViewFR5TRect

	void			DrawLabels(void);									// ROM 0x00121ff8 DrawLabels__10TMonthViewFv - the weekday letters
	void			DrawDates(void);									// ROM 0x00120450 DrawDates__10TMonthViewFv
	void			DrawMonthOverView(void);							// ROM 0x00122174 DrawMonthOverView__10TMonthViewFv (NOT YET)
	void			DateRect(Rect& rect, long date);					// ROM 0x00120d64 DateRect__10TMonthViewFR5TRectl - the cell a day of the month is in
	long			PointToDate(Point& pt);								// ROM 0x00120ea0 PointToDate__10TMonthViewFR6TPoint - the day a point is over
	void			InvertSelection(void);								// ROM 0x00120e54 InvertSelection__10TMonthViewFv
	void			UpdateRangeRect(long first, long last);				// ROM 0x00120b74 UpdateRangeRect__10TMonthViewFlT1 - the one or two rectangles a range of days covers
	void			UpdateFrame(long first, long last);					// ROM 0x00120a60 UpdateFrame__10TMonthViewFlT1 - the range written back as selectedDates
	Boolean			HandleClick(TStrokePublic* stroke);					// ROM 0x001206b4 HandleClick__10TMonthViewFP13TStrokePublic

	long			FirstColumn(void) const;							// the column the first of the month falls in (inline in the ROM)

	short			fCellHeight;		// +0x30  a day's box
	short			fCellWidth;			// +0x32
	short			fUnused34;			// +0x34  (the ROM sets it to 2 and never reads it)
	long			fMeetingOverview;	// +0x38  the meetingOverview proto slot is there
	Boolean			fNoSelection;		// +0x3c  the noSelection variable is there: nothing is inverted
	Rect			fLabelRect;			// +0x40  where the weekday letters go
	Rect			fGridRect;			// +0x48  where the days go
	Rect			fRangeRect;			// +0x50  the selection, as one or two rectangles
	Rect			fRangeRect2;		// +0x58  (empty when one is enough)
	TDate			fDate;				// +0x60  the first of the month shown
	RefStruct		fLabelFont;			// +0x88
	RefStruct		fDatesFont;			// +0x8c
	long			fFirstDayOfWeek;	// +0x90  0 Sunday
};

#endif	/* __MONTHVIEW_H */
