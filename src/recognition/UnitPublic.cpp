/*
	File:		recognition/UnitPublic.cpp

	Contains:	TUnitPublic, the face of a unit the view system sees.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "UnitPublic.h"
#include "EdgeList.h"
#include "Recognizer.h"
#include "RootView.h"
#include "ViewFlags.h"
#include "Rects.h"
#include "StrokeCentral.h"


// ROM 0x0022ced0 __ct__11TUnitPublicFP5TUnitUl
// Nothing made yet: the stroke face, shapes, word list and view hit come
// when asked for; the word base is unknown.
TUnitPublic::TUnitPublic(TUnit* unit, ULong /*unused*/)
{
	fWordInfo = AllocateRefHandle(NILREF);
	fWordInfo->stackPos = 0;
	fCleanShape = nil;
	fUnit = unit;
	fRoughShape = nil;
	fStroke = nil;
	fViewHit = nil;
	fWordList = nil;
	fWordInfo->ref = NILREF;
	fWordBase.top = (short) 0x8000;
	fWordBase.bottom = (short) 0x8000;
}


// ROM 0x0022cf60 __dt__11TUnitPublicFv
// The stroke face, the polygons, the word list and the word info gone
// (the unit itself is the recogniser's).
TUnitPublic::~TUnitPublic()
{
	if (fStroke != nil)
		delete fStroke;
	if (fCleanShape != nil)
		DisposHandle(fCleanShape);		// (the ROM: KillPoly)
	if (fRoughShape != nil)
		DisposHandle(fRoughShape);
	// NOT YET RECONSTRUCTED: TWordList
	DisposeRefHandle(fWordInfo);
}


// ROM 0x0022d678 GetType__11TUnitPublicFv
ULong
TUnitPublic::GetType(void)
{
	return fUnit->fType;
}


// ROM 0x0022d934 StartTime__11TUnitPublicFv
ULong
TUnitPublic::StartTime(void)
{
	return fUnit->fStartTime;
}


// ROM 0x0022da68 EndTime__11TUnitPublicFv
// The end of the last stroke the unit covers.
// NOT YET RECONSTRUCTED: the controller's stroke unit at fMaxStroke (its
// start plus its duration); the unit's own end time serves the host.
ULong
TUnitPublic::EndTime(void)
{
	return fUnit->EndTime();
}


// ROM 0x0022d18c ContextID__11TUnitPublicFv
ULong
TUnitPublic::ContextID(void)
{
	return fUnit->ContextID();
}


// ROM 0x0022dc28 Bounds__11TUnitPublicFP5TRect
// The unit's bounds in pixels, a pixel wider and taller than its box - the
// stroke's box for a recogniser flagged kRecognizerStrokeBounds.  With no
// recogniser for the type (or no stroke) the rect's top and bottom are
// left at -0x8000.
void
TUnitPublic::Bounds(Rect* rect)
{
	TRecognizer* recognizer = gRecognition.fRecognizers->FindRecognizer(GetType());
	rect->top = (short) 0x8000;
	rect->bottom = (short) 0x8000;
	if (recognizer == nil)
		return;
	if (!recognizer->TestFlags(kRecognizerStrokeBounds))
	{
		FRect box;
		fUnit->GetBBox(&box);
		UnfixRect(&box, rect);
	}
	else
	{
		TStrokePublic* stroke = Stroke();
		if (stroke == nil)
			return;
		UnfixRect(&stroke->fStroke->fBBox, rect);
	}
	rect->bottom++;
	rect->right++;
}


// ROM 0x0022db84 IsTap__11TUnitPublicFv
// A tap: the bounds under 6 pixels each way.
Boolean
TUnitPublic::IsTap(void)
{
	Rect bounds;
	Bounds(&bounds);
	return (UShort) (bounds.bottom - bounds.top) < 6 && (UShort) (bounds.right - bounds.left) < 6;
}


// ROM 0x0022dbdc Stroke__11TUnitPublicFv
// The face of the unit's first stroke, made once (the face does not own
// the stroke); nil when the unit has none.
TStrokePublic*
TUnitPublic::Stroke(void)
{
	if (fStroke == nil)
	{
		TStroke* stroke = fUnit->GetStroke(0);
		if (stroke != nil)
			fStroke = TStrokePublic::Make(stroke, false);
	}
	return fStroke;
}


// ROM 0x0022d940 FindView__11TUnitPublicFUl
// The view under the unit with the flags: the one found last is answered
// when it was found with the same flags; otherwise the view under the
// centre of the bounds, and failing that the closest within 10 pixels
// each way.  While the arbiter is arbitrating for the whole screen
// (NOT YET RECONSTRUCTED: gArbiter) the view under the screen's centre
// is taken instead.
TView*
TUnitPublic::FindView(ULong flags)
{
	if (fViewHit != nil && fViewHitFlags == flags)
		return fViewHit;
	Rect bounds;
	Bounds(&bounds);
	TView* view = gRootView->FindView(MidPoint(bounds), flags, nil);
	if (view == nil)
	{
		Point slop = MakePoint(10, 10);
		view = gRootView->FindView(MidPoint(bounds), flags, &slop);
	}
	SetViewHit(view, flags);
	return view;
}


// ROM 0x0022daa0 SetViewHit__11TUnitPublicFP5TViewUl
void
TUnitPublic::SetViewHit(TView* view, ULong flags)
{
	fViewHit = view;
	fViewHitFlags = flags;
}


// ROM 0x0022daac InputMask__11TUnitPublicFv
// The recognition bits of the view under the unit (found with the
// recogniser's required mask); 0 for no view.
ULong
TUnitPublic::InputMask(void)
{
	TView* view = FindView(RequiredMask());
	return view != nil ? view->fFlags & vRecognitionAllowed : 0;
}


// ROM 0x0022db28 RequiredMask__11TUnitPublicFv
// The services the unit's recogniser has enabled, as view flags; a stroke
// recogniser's take the shape and word bits along, a gesture recogniser's
// the clicks.
ULong
TUnitPublic::RequiredMask(void)
{
	ULong mask = 0;
	TRecognizer* recognizer = gRecognition.fRecognizers->FindRecognizer(fUnit->fType);
	if (recognizer != nil)
		mask = recognizer->ServicesEnabled();
	if (mask & vStrokesAllowed)
		mask |= 0x17ef000;
	if (mask & vGesturesAllowed)
		mask |= vClickable;
	return mask;
}


// ROM 0x0022dae8 Cleanup__11TUnitPublicFv
// A click's ink taken off the screen once it is handled, and the stroke
// world's current stroke forgotten.
void
TUnitPublic::Cleanup(void)
{
	if (fUnit->fType != kClickUnit)
		return;
	Stroke()->InkOff(true);
	gStrokeWorld.InvalidateCurrentStroke();
}


// ROM 0x0022dcfc Invalidate__11TUnitPublicFv
// What the unit's strokes inked given to the root view to redraw: with no
// stroke face yet, the bounds (let out for the ink) and every stroke's
// rect - the strokes not drawn are marked to draw no ink and their rects
// go to the screen-dirty region, the drawn ones' rects are invalidated;
// with a face, its inked rect unless the stroke draws no ink.
void
TUnitPublic::Invalidate(void)
{
	if (fStroke == nil)
	{
		Rect bounds;
		Bounds(&bounds);
		AdjustForInk(&bounds);
		gRootView->SmartInvalidate(bounds);
		Rect drawn, notDrawn;
		SetEmptyRect(&drawn);
		SetEmptyRect(&notDrawn);
		ULong count = fUnit->CountStrokes();
		for (ULong i = 0; i < count; i++)
		{
			TStroke* stroke = fUnit->GetStroke(i);
			Rect rect;
			GetStrokeRect(stroke, &rect);
			AdjustForInk(&rect);
			if (!stroke->TestFlags(kStrokeDrawn))
			{
				UnionRect(&notDrawn, &rect, &notDrawn);
				stroke->SetFlags(kStrokeNoInk);
			}
			else
				UnionRect(&drawn, &rect, &drawn);
		}
		if (!EmptyRect(&drawn))
			gRootView->SmartInvalidate(drawn);
		if (!EmptyRect(&notDrawn))
			gRootView->SmartScreenDirty(notDrawn);
	}
	else if (!fStroke->fStroke->TestFlags(kStrokeNoInk))
	{
		Rect inked;
		fStroke->GetInkedRect(&inked);
		gRootView->SmartInvalidate(inked);
	}
}


// ROM 0x0022cfc8 CaretType__11TUnitPublicFv
// The first interpretation's label when it is one of the caret gestures
// (2, 3, 5, 6); else 0.
long
TUnitPublic::CaretType(void)
{
	long label = ((TSIUnit*) fUnit)->GetInterpretation(0)->label;
	if (label != 2 && label != 3 && label != 5 && label != 6)
		label = 0;
	return label;
}


// ROM 0x0022d0a8 CountGesturePoints__FP11TUnitPublic
// The corners of the unit's polyline (a gesture unit's interpretation),
// which is what the gesture was recognised from.
long
CountGesturePoints(TUnitPublic* unit)
{
	return ((TEdgeListUnit*) unit->fUnit)->GetCorners()->fCount;
}


// ROM 0x0022d0b8 GestureAngle__11TUnitPublicFv
// The first interpretation's angle (Fixed, rounded to degrees), snapped to
// 0, 90, -90, 180 or 135 when within the tolerance - 30 degrees for label
// 5, 20 for the rest; otherwise as it is.
long
TUnitPublic::GestureAngle(void)
{
	UnitInterpretation* interp = ((TSIUnit*) fUnit)->GetInterpretation(0);
	long angle = (short) ((interp->angle + 0x8000) >> 16);
	long tolerance = interp->label == 5 ? 30 : 20;
	long diff = angle < 0 ? -angle : angle;
	if (diff <= tolerance)
		return 0;
	diff = angle - 90 < 0 ? 90 - angle : angle - 90;
	if (diff <= tolerance)
		return 90;
	diff = angle + 90 < 0 ? -(angle + 90) : angle + 90;
	if (diff <= tolerance)
		return -90;
	diff = angle - 180 < 0 ? 180 - angle : angle - 180;
	if (diff <= tolerance)
		return 180;
	diff = angle - 135 < 0 ? 135 - angle : angle - 135;
	if (diff <= tolerance)
		return 135;
	diff = angle + 180 < 0 ? -(angle + 180) : angle + 180;
	if (diff <= tolerance)
		return 180;
	return angle;
}


// ROM 0x0022d870 Strokes__11TUnitPublicFv
// The word's strokes as a stroke bundle: the word info frame's strokes
// slot.  NOT YET RECONSTRUCTED: WordInfo (MakeWordInfo); nil.
Ref
TUnitPublic::Strokes(void)
{
	return NILREF;
}
