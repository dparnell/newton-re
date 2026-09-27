/*
	File:		views/SliderView.cpp

	Contains:	TSliderView, a meeting's duration bar.  See SliderView.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	The ROM builds its rectangles on the stack a halfword at a time
	through unaligned loads; each is written here as the fields it comes
	to, read out of the assembly.
*/

#include "SliderView.h"
#include "MeetingView.h"
#include "RootView.h"
#include "Commands.h"
#include "Application.h"
#include "Animate.h"
#include "UnitPublic.h"
#include "Stroke.h"
#include "Rects.h"
#include "Draw.h"
#include "Polygons.h"
#include "RegionVars.h"
#include "Screen.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"
#include "RSSymbols.h"
#include "ROMConstants.h"


// ROM 0x001cbda4 TRectToSliderPoly__FR5TRect
// The rectangle as a bar with slanted ends: from the left side a width
// below the top, up to the top right corner, down the right side to a
// width above the bottom, across to the bottom left, and back up.
PolyHandle
TRectToSliderPoly(Rect& bounds)
{
	long width = (short) ((unsigned short) bounds.right - (unsigned short) bounds.left);
	PolyHandle poly = OpenPoly();
	if (poly != nil)
	{
		long left = bounds.left;
		long right = bounds.right;
		long top = bounds.top;
		long bottom = (short) ((unsigned short) bounds.bottom - 1);
		long start = top + width;
		long capTop = (short) start;
		MoveTo(left, capTop);
		LineTo(left + 1, capTop - 1);
		LineTo(left + 1, capTop - 2);
		LineTo(right - 1, top);
		LineTo(right, top + 3);
		long capBottom = (short) (bottom - width);
		LineTo(right, capBottom);
		LineTo(right - 1, capBottom + 1);
		LineTo(right - 1, capBottom + 2);
		LineTo(left + 1, bottom);
		LineTo(left, bottom);
		LineTo(left, start);
		ClosePoly();
	}
	return poly;
}


/*------------------------------------------------------------------------------
	T S l i d e r V i e w
------------------------------------------------------------------------------*/

// ROM 0x001cbd58 __dt__11TSliderViewFv
// The hilite a click on the meeting put on its context goes with it.
// (Host: a slider with no parent - one whose construction threw - has no
// meeting to ask; the ROM would read at 0x24.)
TSliderView::~TSliderView()
{
	if (fParent != nil)
		DeleteMeetingHilite(RefVar(fParent->fContext));
}


// ROM 0x001c97b8 ClassID__11TSliderViewCFv
long
TSliderView::ClassID(void) const
{
	return clSliderView;
}


// ROM 0x001c97c0 DerivedFrom__11TSliderViewCFl
Boolean
TSliderView::DerivedFrom(long id) const
{
	return id == clSliderView || TDataView::DerivedFrom(id);
}


// ROM 0x001cbd54 Constructor__11TSliderViewFRC6RefVarP5TView
void
TSliderView::Constructor(RefArg context, TView* parent)
{
	TView::Constructor(context, parent);
}


// ROM 0x001c97f4 DrawSlider__11TSliderViewFRC5TRect
// The bar in fSlider painted black (the rectangle given is not looked
// at); nothing when it has no width.
void
TSliderView::DrawSlider(const Rect& /*bounds*/)
{
	if ((short) (fSlider.right - fSlider.left) < 1)
		return;
	PolyHandle poly = TRectToSliderPoly(fSlider);
	if (poly == nil)
		return;
	PatternHandle saved = GetFgPattern();
	SetFgPattern(GetStdPattern(blackPat));
	PaintPoly(poly);
	KillPoly(poly);
	GetCurrentPort()->fgPat = saved;
}


// ROM 0x001c9860 RealDraw__11TSliderViewFR5TRect
// The bar: the view two pixels in from each side - not printed when the
// meeting is in a plain view (the week's overview).
void
TSliderView::RealDraw(Rect& /*bounds*/)
{
	fSlider = viewBounds;
	if (Printing() && RINT(fParent->GetProto(RSSYMviewclass)) == clView)
		return;
	InsetRect(&fSlider, 2, 0);
	if ((short) (fSlider.right - fSlider.left) < 1)
		return;
	PolyHandle poly = TRectToSliderPoly(fSlider);
	if (poly == nil)
		return;
	PatternHandle saved = GetFgPattern();
	SetFgPattern(GetStdPattern(blackPat));
	PaintPoly(poly);
	KillPoly(poly);
	GetCurrentPort()->fgPat = saved;
}


// ROM 0x001c98e0 RealDoCommand__11TSliderViewFRC6RefVar
// A click is the drag (HandleClick).  A scrub over the bar deletes the
// meeting (an aeRemoveData to the page for it) with a poof.  aeScaleData
// is how a drag is undone: the meeting's bounds now become the undo's,
// and the command's two words (a rectangle) the meeting's viewBounds.
Boolean
TSliderView::RealDoCommand(RefArg cmd)
{
	long result = 0;
	long id = CommandID(cmd);
	if (id == aeClick)
	{
		result = HandleClick(cmd);
		CommandSetResult(cmd, result);
	}
	else if (id == aeScrub)
	{
		TUnitPublic* unit = (TUnitPublic*) CommandParameter(cmd);
		Rect scrub;
		unit->Bounds(&scrub);
		if (Intersects(&viewBounds, &scrub))
		{
			TView* meeting = fParent;
			TView* page = meeting->fParent;
			unit->Stroke()->InkOff(false);
			gApplication->DispatchCommand(RefVar(MakeCommand(aeRemoveData, page, meeting->fId)));
			TAnimate effect;
			effect.SetupPoofEffect(page, scrub);
			effect.DoEffect(RefVar(Rpoof));
			CommandSetResult(cmd, 1);
			result = 1;
		}
	}
	else if (id == aeScaleData)
	{
		TView* meeting = fParent;
		Point origin = meeting->fParent->ContentsOrigin();
		Rect was = meeting->viewBounds;
		OffsetRect(&was, -origin.h, -origin.v);
		RefVar undo(MakeCommand(aeScaleData, this, 0x8000000));
		CommandSetIndexRect(undo, 0, was);
		gApplication->PostUndoCommand(undo);
		Rect bounds;
		CommandIndexRect(cmd, 0, &bounds);
		meeting->SetValue(RSSYMviewbounds, RefVar(ToObject(bounds)));
		return true;
	}
	if (result != 0)
		return result;
	return TView::RealDoCommand(cmd);
}


// ROM 0x001c9b60 DrawHilitedData__11TSliderViewFv
void
TSliderView::DrawHilitedData(void)
{
	Rect bounds = fParent->viewBounds;
	fParent->Draw(bounds, false);
}


// ROM 0x001c9b94 HandleClick__11TSliderViewFRC6RefVar
// The pen down on the bar: the bar (inset a little) inverted while the
// pen stays put.  Moved more than three pixels, the pen drags the end
// of the meeting: the screen below the meeting's top is kept, the bar
// redrawn longer as the pen goes down and the screen put back under it
// as the pen comes up (never shorter than twelve pixels), all clipped to
// the page's view; when the pen comes up the meeting's
// SetMeetingBounds(new, old) is told its new box and its old one, in the
// page's coordinates - unless the meeting has no start date.  Lifted
// without moving, the bar is put back.  ==> 1 (0: not on the bar).
//
// ROM BUG, kept: the bar's polygon is never killed - every press on a
// meeting's bar leaks one.
long
TSliderView::HandleClick(RefArg cmd)
{
	fSlider = viewBounds;
	InsetRect(&fSlider, 2, 0);
	TStrokePublic* stroke = ((TUnitPublic*) CommandParameter(cmd))->Stroke();
	stroke->InkOff(true);
	Point first = stroke->FirstPoint();
	Point pt = first;
	long dragged = 0;
	TView* meeting = fParent;
	TView* page = meeting->fParent;
	if (!PtInRect(first, &viewBounds))
		return 0;
	TRegionVar savedClip;
	GetClip(savedClip);
	Rect clip = page->fParent->viewBounds;
	ClipRect(&clip);
	Rect bar = fSlider;
	InsetRect(&bar, 2, 4);
	PolyHandle poly = TRectToSliderPoly(bar);
	if (poly == nil)
		return 1;
	newton_try
	{
		InvertPoly(poly);
		if (!stroke->Done())
		{
			do
			{
				pt = stroke->FinalPoint();
				if (CheapDistance(pt, first) <= 3)
					Wait(1);
				else
				{
					InvertPoly(poly);
					dragged = 1;
					// the saved area and the bar's reach: the view down to
					// the foot of the screen
					Rect saveArea = viewBounds;
					saveArea.bottom = gRootView->viewBounds.bottom;
					Rect reach = viewBounds;
					reach.bottom = gRootView->viewBounds.bottom;
					long shortest = (short) (fSlider.top + 12);
					StartDrawing(nil, nil);
					SetFlags(vIsInSetup2);
					Dirty(nil);
					gRootView->Update(nil);
					TSaveScreenBits saved;
					if (!saved.AllocateBuffers(&saveArea))
						Throw(exOutOfMemory, (void*) (long) kError_No_Memory, nil);
					saved.SaveScreenBits();
					ClearFlags(vIsInSetup2);
					RealDraw(reach);
					StopDrawing(nil, nil);
					long last = pt.v;
					long y = last;
					while (!stroke->Done())
					{
						pt = stroke->FinalPoint();
						y = pt.v;
						if (y < shortest)
							y = shortest;
						y = (short) y;
						if (y == last)
							Wait(1);
						else
						{
							if (y < last)
							{
								reach.top = (short) y;
								TRectangularRegion below(reach);
								saved.RestoreScreenBits(&saveArea, below);
							}
							else
							{
								fSlider.bottom = (short) y;
								DrawSlider(reach);
							}
							last = y;
						}
					}
					RefVar entry(meeting->DataFrame());
					if (RINT(GetMeetingSlot(entry, RSSYMmtgstartdate)) != 0)
					{
						Rect old = meeting->viewBounds;
						Rect now = old;
						now.bottom = (short) last;
						Point origin = page->ContentsOrigin();
						OffsetRect(&now, -origin.h, -origin.v);
						OffsetRect(&old, -origin.h, -origin.v);
						RefVar args(MakeArray(2));
						SetArraySlotRef(args, 0, ToObject(now));
						SetArraySlotRef(args, 1, ToObject(old));
						meeting->RunScript(RSSYMsetmeetingbounds, args, false);
					}
				}
			}
			while (!stroke->Done());
			if (dragged == 0)
				InvertPoly(poly);
		}
		else
			InvertPoly(poly);
	}
	newton_catch_all
	{
		SetClip(savedClip);
		rethrow;
	}
	end_try;
	SetClip(savedClip);
	return 1;
}
