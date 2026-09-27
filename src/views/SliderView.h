/*
	File:		views/SliderView.h

	Contains:	TSliderView (class 96), the bar down the left of a meeting in
				the Dates day view that says how long the meeting is.

				The bar is the view's bounds, two pixels in from each side,
				drawn black as a polygon with slanted ends
				(TRectToSliderPoly).  The pen dragged down it moves the end
				of the meeting (the bar growing and shrinking as it goes,
				the screen under it kept and put back) and the meeting's
				SetMeetingBounds is told the new box and the old; a scrub
				over it deletes the meeting.

	Reconstructed from the MP2x00 US ROM (0x001c97b8-0x001ca128,
	0x001cbd54-0x001cbed0); each function cites its origin.
*/

#ifndef __SLIDERVIEW_H
#define __SLIDERVIEW_H

#include "DataView.h"

class TSliderView : public TDataView
{
public:
	virtual			~TSliderView();							// ROM 0x001cbd58 __dt__11TSliderViewFv
	virtual long	ClassID(void) const;					// ROM 0x001c97b8 ClassID__11TSliderViewCFv
	virtual Boolean	DerivedFrom(long id) const;				// ROM 0x001c97c0 DerivedFrom__11TSliderViewCFl
	virtual void	Constructor(RefArg context, TView* parent);	// ROM 0x001cbd54 Constructor__11TSliderViewFRC6RefVarP5TView - TView's
	virtual Boolean	RealDoCommand(RefArg cmd);				// ROM 0x001c98e0 RealDoCommand__11TSliderViewFRC6RefVar
	virtual void	RealDraw(Rect& bounds);					// ROM 0x001c9860 RealDraw__11TSliderViewFR5TRect
	virtual void	DrawHilitedData(void);					// ROM 0x001c9b60 DrawHilitedData__11TSliderViewFv - the meeting drawn

	void			DrawSlider(const Rect& bounds);			// ROM 0x001c97f4 DrawSlider__11TSliderViewFRC5TRect - the bar in fSlider
	long			HandleClick(RefArg cmd);				// ROM 0x001c9b94 HandleClick__11TSliderViewFRC6RefVar - the end of the meeting dragged

	Rect			fSlider;			// +0x3c  the bar
};

// The bar's outline: the rectangle with its top and bottom ends slanted
// by its width, as a polygon (nil when there was no memory for one).
PolyHandle	TRectToSliderPoly(Rect& bounds);					// ROM 0x001cbda4 TRectToSliderPoly__FR5TRect

#endif	/* __SLIDERVIEW_H */
