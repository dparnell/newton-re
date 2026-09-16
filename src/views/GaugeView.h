/*
	File:		views/GaugeView.h

	Contains:	TGaugeView (clGaugeView, 92): protoGauge and protoSlider - a
				horizontal bar filled from the left in proportion to
				viewValue between minValue and maxValue (100 when there is
				none); an editable one (vReadOnly clear) has a hollow diamond
				knob at the value's place, and gaugeDrawLimits fills the rest
				of the bar in light gray.  The ROM's object is 0x38 bytes:
				TView, the maximum and the minimum.  NOT YET RECONSTRUCTED:
				tracking the pen (TrackSetValue: the aeClick command, the
				strokes), the gray pattern of deeper ports.

	Reconstructed from the MP2100 D ROM (0x0018ada4-0x0018b400); each
	function cites its origin.
*/

#ifndef __GAUGEVIEW_H
#define __GAUGEVIEW_H

#ifndef __VIEW_H
#include "View.h"
#endif

class TGaugeView : public TView
{
public:
	virtual long	ClassID(void) const;								// ROM 0x0018ada4 ClassID__10TGaugeViewCFv
	virtual Boolean	DerivedFrom(long id) const;							// ROM 0x0018adac DerivedFrom__10TGaugeViewCFl
	virtual void	Constructor(RefArg context, TView* parent);			// ROM 0x0018ade0 Constructor__10TGaugeViewFRC6RefVarP5TView
	virtual void	SetValue(RefArg slot, RefArg value);				// ROM 0x0018aed4 SetValue__10TGaugeViewFRC6RefVarT1
	virtual void	RealDraw(Rect& bounds);								// ROM 0x0018af84 RealDraw__10TGaugeViewFR5TRect
	virtual Boolean	RealDoCommand(RefArg cmd);							// ROM 0x0018b2d0 RealDoCommand__10TGaugeViewFRC6RefVar

	long		fMaxValue;			// +0x30  maxValue (100 when none)
	long		fMinValue;			// +0x34  minValue (0 when none)
};

#endif	/* __GAUGEVIEW_H */
