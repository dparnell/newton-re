/*
	File:		views/GaugeView.cpp

	Contains:	TGaugeView: a bar filled to its value, with a knob when editable.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "GaugeView.h"
#include "SoundSettings.h"	// FPlaySound
#include "Rects.h"
#include "Draw.h"
#include "Shapes.h"
#include "Polygons.h"
#include "RegionVars.h"
#include "ObjectHeap.h"
#include "RootView.h"
#include "Commands.h"
#include "UnitPublic.h"
#include "NewtonTime.h"


// ROM 0x00188d74 ClassID__10TGaugeViewCFv
long
TGaugeView::ClassID(void) const
{
	return clGaugeView;
}


// ROM 0x00188d7c DerivedFrom__10TGaugeViewCFl
Boolean
TGaugeView::DerivedFrom(long id) const
{
	return id == clGaugeView || TView::DerivedFrom(id);
}


// ROM 0x00188db0 Constructor__10TGaugeViewFRC6RefVarP5TView
// The maximum from maxValue (100 without one), the minimum from minValue
// (0).
void
TGaugeView::Constructor(RefArg context, TView* parent)
{
	TView::Constructor(context, parent);
	fMaxValue = 100;
	RefVar value(GetValue(RSSYMmaxvalue, RefVar(NILREF)));
	if (NOTNIL(value))
		fMaxValue = RINT(value);
	fMinValue = 0;
	value = GetValue(RSSYMminvalue, RefVar(NILREF));
	if (NOTNIL(value))
		fMinValue = RINT(value);
}


// ROM 0x00188ea4 SetValue__10TGaugeViewFRC6RefVarT1
// maxValue and minValue set the limits; then as TView::SetValue (whose
// body the ROM repeats here).
void
TGaugeView::SetValue(RefArg slot, RefArg value)
{
	if (EQRef(slot, RSSYMmaxvalue))
		fMaxValue = RINT(value);
	else if (EQRef(slot, RSSYMminvalue))
		fMinValue = RINT(value);
	TView::SetValue(slot, value);
}


// ROM 0x00188f54 RealDraw__10TGaugeViewFR5TRect
// The bar: viewValue pinned to the limits; the bounds made an odd height
// (a pixel off the bottom); an editable gauge keeps a knob's width (the
// height) out of the range; the filled part runs from the left to half
// the knob plus the value's share of the range, inset two from the top
// and bottom, painted black.  With gaugeDrawLimits the rest of the bar
// (a pixel narrower top and bottom) is painted light gray (NOT YET
// RECONSTRUCTED: the ROM's solid gray pattern on a port deeper than a
// bit).  The knob: a diamond the height wide, two pixels taller than the
// bar at each end, centred on the filled part's right, painted and its
// inside (a pixel in) erased.  The pen pattern is left black.
void
TGaugeView::RealDraw(Rect& /*bounds*/)
{
	long value = RINT(GetValue(RSSYMviewvalue, RefVar(NILREF)));
	if (value <= fMinValue)
		value = fMinValue;
	if (value > fMaxValue)
		value = fMaxValue;
	Rect r = viewBounds;
	long height = r.bottom - r.top;
	if ((height & 1) == 0)
	{
		height--;
		r.bottom--;
	}
	Boolean editable = (fFlags & vReadOnly) == 0;
	long knob = editable ? height : 0;
	long range = (r.right - r.left) - knob;
	long pos = ((value - fMinValue) * range) / (fMaxValue - fMinValue);
	r.right = (short) (r.left + knob / 2 + pos);
	InsetRect(&r, 0, 2);
	PenNormal();
	PaintRect(&r);
	if (NOTNIL(GetProto(RSSYMgaugedrawlimits)))
	{
		SetPattern(2);
		Rect limits;
		limits.top = (short) (r.top + 1);
		limits.left = r.right;
		limits.bottom = (short) (r.bottom - 1);
		limits.right = viewBounds.right;
		PaintRect(&limits);
	}
	PenNormal();
	if (editable)
	{
		Rect knobBox;
		knobBox.top = (short) (r.top - 2);
		knobBox.bottom = (short) (r.bottom + 2);
		knobBox.left = (short) (r.right - height / 2);
		knobBox.right = (short) (knobBox.left + height);
		Point mid = MidPoint(knobBox);
		TRegionVar diamond;
		OpenRgn();
		MoveTo(mid.h, knobBox.top);
		LineTo(knobBox.right, mid.v);
		LineTo(mid.h, knobBox.bottom);
		LineTo(knobBox.left - 1, mid.v);
		LineTo(mid.h, knobBox.top);
		CloseRgn(diamond);
		PaintRgn(diamond);
		InsetRgn(diamond, 1, 1);
		EraseRgn(diamond);
	}
	SetPattern(5);
}


// ROM 0x001892a0 RealDoCommand__10TGaugeViewFRC6RefVar
// aeClick on an editable gauge (vReadOnly clear) tracks the pen to set
// the value (TrackSetValue), the command's result its answer; then as
// TView.
Boolean
TGaugeView::RealDoCommand(RefArg cmd)
{
	if (CommandID(cmd) == aeClick && (fFlags & vReadOnly) == 0)
	{
		Boolean result = TrackSetValue((TUnitPublic*) CommandParameter(cmd));
		CommandSetResult(cmd, result);
		if (result)
			return result;
	}
	return TView::RealDoCommand(cmd);
}


// ROM 0x00189314 TrackSetValue__10TGaugeViewFP11TUnitPublic
// The pen tracked, its ink off: each turn the value under the stroke's
// last point - the point's distance from the left, half a step on, as a
// fraction of the width in the range, clamped to it - set when it
// changed (the _sound proto variable played, the root view updated), a
// tick waited when it did not, until the stroke is done; the
// viewFinalChangeScript is run with [old, new] when the value changed.
// ==> true.  NOT YET RECONSTRUCTED: BusyBoxSend.
Boolean
TGaugeView::TrackSetValue(TUnitPublic* unit)
{
	TStrokePublic* stroke = unit->Stroke();
	stroke->InkOff(true);
	long original = RINT(GetValue(RSSYMviewvalue, RefVar(NILREF)));
	RefVar sound(GetProto(RSSYM_sound));
	long width = viewBounds.right - viewBounds.left;
	long range = fMaxValue - fMinValue;
	long halfStep = (width / range) / 2;
	long value = original;
	do
	{
		Point pt = stroke->FinalPoint();
		long newValue = (range * ((pt.h - viewBounds.left) + halfStep)) / width + fMinValue;
		if (newValue < fMinValue)
			newValue = fMinValue;
		else if (newValue > fMaxValue)
			newValue = fMaxValue;
		if (newValue == value)
			Wait(1);
		else
		{
			if (NOTNIL(sound))
				FPlaySound(RefVar(fContext), sound);
			SetValue(RSSYMviewvalue, MAKEINT(newValue));
			gRootView->Update(nil);
			value = newValue;
		}
	} while (!stroke->Done());
	if (value != original)
	{
		RefVar args(MakeArray(2));
		SetArraySlot(args, 0, MAKEINT(original));
		SetArraySlot(args, 1, MAKEINT(value));
		RunScript(RSSYMviewfinalchangescript, args);
	}
	return true;
}
