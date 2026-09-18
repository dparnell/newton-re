/*
	File:		views/ViewDraw.cpp

	Contains:	TView's showing and drawing: Show/Hide, the visibility of
				the root view's children (ViewVisibleChanged, GetFrontMask,
				SetupVisRgn), Draw/Update/DrawChildren, the viewFormat's
				fill, lines and frame (PreDraw, PostDraw), Dirty.  The port
				is the current one; its visRgn is narrowed to what a view may
				draw on and restored after.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "View.h"
#include "RootView.h"
#include "Rects.h"
#include "Ports.h"
#include "Draw.h"
#include "Shapes.h"
#include "Animate.h"
#include "Stroke.h"
#include "StrokeCentral.h"
#include "Screen.h"
#include "Regions.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "NewtonExceptions.h"


// ROM 0x000e37e8 SetPattern__Fl
// The port's pen pattern from a viewFormat pattern index: 1 white, 2
// light gray, 3 gray, 4 dark gray, 5 black (12 black, 13 and 15 gray:
// the hilite and drag frames); 14, the custom pattern, is the caller's.
// ==> whether there is a pattern to draw with.
Boolean
SetPattern(long index)
{
	GetPatSelector which;
	switch (index)
	{
	case 1:		which = whitePat;	break;
	case 2:		which = ltGrayPat;	break;
	case 3:
	case 0xd:
	case 0xf:	which = grayPat;	break;
	case 4:		which = dkGrayPat;	break;
	case 5:
	case 0xc:	which = blackPat;	break;
	case 0xe:	return true;
	default:	return false;
	}
	SetFgPattern(GetStdPattern(which));
	return true;
}


// ROM 0x00328e6c DisposeFgPattern__Fv
// The pen pattern disposed and black put back.
void
DisposeFgPattern(void)
{
	DisposePattern(GetFgPattern());
	GetCurrentPort()->fgPat = GetStdPattern(blackPat);
}


// ROM 0x00197d2c GetPattern__FRC6RefVarPUcPPP8PixelMapUc
// A pattern from its NewtonScript form: an integer is a standard pattern
// index (1-5; a packed RGB with bit 28 NOT YET: a gray), a binary the
// eight rows of a simple pattern.  NOT YET RECONSTRUCTED: the pattern
// frames ({rgb, ...}) and 'grayPattern binaries.  ==> whether pattern was
// set; owned says whether it must be disposed.
Boolean
GetPattern(RefArg spec, Boolean* owned, PatternHandle* pattern, Boolean /*wasOwned*/)
{
	if (ISINT(spec))
	{
		long value = RVALUE(spec);
		if (value <= 0)
			return false;
		if (value & 0x10000000)
			return false;
		if (*owned)
			DisposePattern(*pattern);
		*owned = false;
		*pattern = GetStdPattern((GetPatSelector) (value - 1));
		return *pattern != nil;
	}
	if (IsFrame(spec) || IsArray(spec) || IsSymbol(spec))
		return false;
	if (Length(spec) < 8)
		return false;
	if (*owned)
		DisposePattern(*pattern);
	*pattern = MakeSimplePattern((const char*) BinaryData(spec));
	if (*pattern == nil)
		return false;
	*owned = true;
	return true;
}


// ROM 0x00268240 SetCustomPattern__5TViewFRC6RefVar
// The pen pattern from the view's slot (viewFillPattern, viewLinePattern,
// viewFramePattern); ==> whether one was set.
Boolean
TView::SetCustomPattern(RefArg slot)
{
	RefVar spec(GetVar(slot));
	if (ISNIL(spec))
		return false;
	Boolean owned = false;
	PatternHandle pattern;
	if (!GetPattern(spec, &owned, &pattern, true))
		return false;
	SetFgPattern(pattern);
	return true;
}


/*------------------------------------------------------------------------------
	S h o w i n g
------------------------------------------------------------------------------*/

// ROM 0x00265e80 Show__5TViewFv
// The view shown: the strokes blocked, the view brought to the front, its
// show effect set up, the view made visible (a child of the root view has
// the visibility of the views around it recomputed; any other is
// dirtied), the effect run with the showSound, the strokes unblocked and
// flushed, the viewShowScript run.
void
TView::Show(void)
{
	if (fFlags & vVisible)
		return;
	gStrokeWorld.BlockStrokes();
	BringToFront();
	TAnimate effect;
	effect.SetupPlainEffect(this, true, 0);
	SetFlags(vVisible);
	if (HasVisRgn())
		fParent->ViewVisibleChanged(this, true);
	else
		Dirty(nil);
	effect.DoEffect(RSSYMshowsound);
	gStrokeWorld.UnblockStrokes();
	gStrokeWorld.FlushStrokes();
	RunScript(RSSYMviewshowscript, RefVar(NILREF));
}


// ROM 0x00265f84 Hide__5TViewFv
// The view hidden: the strokes blocked, the caret and popup let go of it,
// its hide effect set up (the image taken from the screen), the
// viewHideScript run, the view made invisible and the views around it
// recomputed, or its outer bounds invalidated; the effect run with the
// hideSound, the strokes unblocked and flushed.
void
TView::Hide(void)
{
	if ((fFlags & vVisible) == 0)
		return;
	gStrokeWorld.BlockStrokes();
	if (gRootView->ViewContainsCaretView(this))
		gRootView->CaretViewGone();
	gRootView->SetPopup(this, false);
	TAnimate effect;
	effect.SetupPlainEffect(this, false, 0);
	RunScript(RSSYMviewhidescript, RefVar(NILREF));
	if (HasVisRgn())
	{
		ClearFlags(vVisible);
		fParent->ViewVisibleChanged(this, true);
	}
	else
	{
		Rect bounds;
		OuterBounds(&bounds);
		ClearFlags(vVisible);
		gRootView->SmartInvalidate(bounds);
	}
	effect.DoEffect(RSSYMhidesound);
	gStrokeWorld.UnblockStrokes();
	gStrokeWorld.FlushStrokes();
}


// ROM 0x00266bf4 Drag__5TViewFP13TStrokePublicRC5TRect
// The view dragged with the pen: the caret hidden, the ink off, the root
// view brought up to date and the view's image taken as a sprite
// (TAnimate::SetupDragEffect) - the drag is off when there is no memory
// to save the port's pixels.  Until the stroke ends: nothing until the
// pen has moved more than four pixels from where it went down, then a
// tick waited while it stays put, else the view's outer bounds moved by
// the pen's travel (kept within the limit) and, the first time, the view
// hidden (its parent dirtied, or the visibility recomputed), the screen
// redrawn without it and saved, and the view made visible again (not
// redrawn); the saved pixels put back where the sprite was and the
// sprite drawn at the new place.  When it moved: the pixels put back
// where views in front cover the new place, the view offset to it (its
// viewBounds slot written dejustified and Changed), the visibility
// recomputed, the port's area validated.  The caret shown again.  ==>
// whether the view moved.  NOT YET RECONSTRUCTED: the busy box.
Boolean
TView::Drag(TStrokePublic* stroke, const Rect& limit)
{
	gRootView->HideCaret();
	stroke->InkOff(true);
	Point start = stroke->FirstPoint();
	Point last = start;
	Rect outer;
	OuterBounds(&outer);
	gRootView->Update(nil);
	TAnimate sprite;
	sprite.SetupDragEffect(this);
	Rect drawn = outer;
	if (outer.bottom - outer.top > screenHeight)
		drawn.bottom = (short) (outer.top + screenHeight);
	if (outer.right - outer.left > screenWidth)
		drawn.right = (short) (outer.left + screenWidth);
	GrafPort* port;
	GetPort(&port);
	Rect portRect = port->portRect;
	if (!sprite.SavedBits().AllocateBuffers(&portRect))
		return false;
	Boolean moved = false;
	Boolean hidden = false;
	Rect newBounds = outer;
	while (!stroke->Done())
	{
		Point pt = stroke->FinalPoint();
		if (!moved)
			moved = CheapDistance(pt, last) > 4;
		if (!moved || EqualPt(pt, last))
			Wait(1);
		else
		{
			newBounds = outer;
			OffsetRect(&newBounds, pt.h - start.h, pt.v - start.v);
			long d;
			if ((d = limit.right - newBounds.right) < 0)
				OffsetRect(&newBounds, d, 0);
			if ((d = limit.bottom - newBounds.bottom) < 0)
				OffsetRect(&newBounds, 0, d);
			if ((d = limit.left - newBounds.left) > 0)
				OffsetRect(&newBounds, d, 0);
			if ((d = limit.top - newBounds.top) > 0)
				OffsetRect(&newBounds, 0, d);
			UnionRect(&drawn, &newBounds, &drawn);
			if (hidden)
			{
				StartDrawing(nil, nil);
				sprite.SavedBits().RestoreScreenBits(&drawn, nil);
			}
			else
			{
				hidden = true;
				ClearFlags(vVisible);
				if (HasVisRgn())
					fParent->ViewVisibleChanged(this, true);
				else
					fParent->Dirty(&drawn);
				StartDrawing(nil, nil);
				gRootView->Update(nil);
				sprite.SavedBits().SaveScreenBits();
				SetFlags(vVisible);
				if (HasVisRgn())
					fParent->ViewVisibleChanged(this, false);
			}
			sprite.Draw(newBounds, srcCopy, nil);
			StopDrawing(nil, nil);
			drawn = newBounds;
			last = pt;
		}
	}
	PenNormal();
	if (moved)
	{
		{
			TRectangularRegion newRgn(newBounds);
			TRegionVar covered;
			SectRgn(sprite.Mask(), newRgn, covered);
			if (!EmptyRgn(covered))
				sprite.SavedBits().RestoreScreenBits(&newBounds, covered);
		}
		Point delta;
		delta.v = newBounds.top - outer.top;
		delta.h = newBounds.left - outer.left;
		Offset(delta);
		Rect bounds;
		DejustifyBounds(&bounds);
		SetDataSlot(RSSYMviewbounds, RefVar(ToObject(bounds)));
		Changed(RSSYMviewbounds);
		if (HasVisRgn())
			fParent->ViewVisibleChanged(this, false);
		TRectangularRegion portRgn(portRect);
		gRootView->Validate(portRgn);
	}
	gRootView->ShowCaret();
	return moved;
}


// ROM 0x00265c80 ViewVisibleChanged__5TViewFP5TViewUc
// A child of the root view appeared, vanished or moved in the order: the
// views from the front down to it have their visible regions recomputed
// against what is in front (everything outside the root view's bounds to
// start with); when asked (Show, Hide), the child's region less what is
// in front of it is invalidated, with the child as the filler when it is
// visible, else the root view.
void
TView::ViewVisibleChanged(TView* child, Boolean invalidate)
{
	TRegionVar inFront;
	Rect everything;
	SetRect(&everything, -32767, -32767, 32766, 32766);
	RectRgn(inFront, &everything);
	Rect outer;
	OuterBounds(&outer);
	TRectangularRegion outerRgn(outer);
	DiffRgn(inFront, outerRgn, inFront);
	Boolean passed = false;
	TBackwardViewListLoop loop(fChildren);
	for (TView* view = loop.Next(); view != nil; view = loop.Next())
	{
		if (!passed && view == child)
		{
			passed = true;
			if (invalidate)
			{
				TClipper* clipper = child->Clipper();
				TRegionVar dirty;
				TRectangularRegion full((*clipper->fFullRgn)->rgnBBox);
				DiffRgn(full, inFront, dirty);
				gRootView->Invalidate(dirty, (view->fFlags & vVisible) ? view : this);
			}
		}
		if (view->fFlags & vVisible)
		{
			TClipper* clipper = view->Clipper();
			if (clipper != nil)
			{
				if (passed)
					clipper->RecalcVisible(inFront);
				UnionRgn(inFront, clipper->fFullRgn, inFront);
			}
		}
	}
}


// ROM 0x00265a84 GetFrontMask__5TViewCFv
// What is in front of the view among its siblings: the regions of the
// visible siblings in front that have a clipper or a fill, or the outer
// bounds of their filled children.
TRegion
TView::GetFrontMask(void) const
{
	TRegionVar mask;
	SetEmptyRgn(mask);
	TBackwardViewListLoop loop(fParent->fChildren);
	for (TView* view = loop.Next(); view != nil && view != this; view = loop.Next())
	{
		if ((view->fFlags & vVisible) == 0)
			continue;
		if ((view->fViewFormat & vfFillMask) != 0 || view->HasVisRgn())
		{
			TClipper* clipper = view->Clipper();
			if (clipper != nil)
				UnionRgn(mask, clipper->fFullRgn, mask);
			else
			{
				Rect bounds;
				view->OuterBounds(&bounds);
				TRectangularRegion r(bounds);
				UnionRgn(mask, r, mask);
			}
		}
		else if (view->fChildren->GetArraySize() != 0)
		{
			TViewLoop children(view->fChildren);
			for (TView* grandchild = children.Next(); grandchild != nil; grandchild = children.Next())
			{
				if ((grandchild->fFlags & vVisible) && (grandchild->fViewFormat & vfFillMask) != 0)
				{
					Rect bounds;
					grandchild->OuterBounds(&bounds);
					TRectangularRegion r(bounds);
					UnionRgn(mask, r, mask);
				}
			}
		}
	}
	return TRegion(mask);
}


// ROM 0x0026590c NarrowVisByIntersectingObscuringSiblingsAndUncles__5TViewFP5TViewP5TRect
// The port's visRgn less the filled or windowed views in front of this
// one and of each ancestor up to upTo (the root view for nil) - only
// those whose outer bounds meet the rectangle, when one is given.
void
TView::NarrowVisByIntersectingObscuringSiblingsAndUncles(TView* upTo, Rect* bounds)
{
	if (upTo == nil)
		upTo = gRootView;
	GrafPort* port;
	GetPort(&port);
	RgnHandle vis = port->visRgn;
	for (TView* view = this; view != upTo; view = view->fParent)
	{
		TBackwardViewListLoop loop(view->fParent->fChildren);
		for (TView* sibling = loop.Next(); sibling != nil && sibling != view; sibling = loop.Next())
		{
			if (bounds != nil)
			{
				Rect outer, met;
				sibling->OuterBounds(&outer);
				if (!SectRect(&outer, bounds, &met))
					continue;
			}
			if ((sibling->fViewFormat & vfFillMask) != 0 || sibling->HasVisRgn())
			{
				TClipper* clipper = sibling->Clipper();
				if (clipper != nil)
					DiffRgn(vis, clipper->fFullRgn, vis);
				else
				{
					Rect bounds;
					sibling->OuterBounds(&bounds);
					TRectangularRegion r(bounds);
					DiffRgn(vis, r, vis);
				}
			}
		}
	}
}


// ROM 0x00267974 SetupVisRgn__5TViewCFv
// The port's visRgn narrowed to what the view may draw on: for each
// ancestor from the view up to the root view, the clipper's visible
// region, the bounds of a vClipping view, and less the front mask.  ==>
// the visRgn as it was, for the caller to put back.
TRegion
TView::SetupVisRgn(void) const
{
	TRegionVar saved;
	GrafPort* port;
	GetPort(&port);
	RgnHandle vis = port->visRgn;
	CopyRgn(vis, saved);
	for (const TView* view = this; view != gRootView; view = view->fParent)
	{
		TClipper* clipper = view->Clipper();
		if (clipper != nil)
			SectRgn(vis, clipper->fVisRgn, vis);
		if (view->fFlags & vClipping)
		{
			TRectangularRegion bounds(view->viewBounds);
			SectRgn(vis, bounds, vis);
		}
		TRegion mask(view->GetFrontMask());
		DiffRgn(vis, mask, vis);
	}
	return TRegion(saved);
}


/*------------------------------------------------------------------------------
	D r a w i n g
------------------------------------------------------------------------------*/

// ROM 0x00267a8c Draw__5TViewFRC5TRectUc
void
TView::Draw(const Rect& bounds, Boolean force)
{
	TRectangularRegion rgn(bounds);
	Draw(rgn, force);
}


// ROM 0x00267ac8 Draw__5TViewF11TBaseRegionUc
// The view drawn where the region meets it: an invisible view (or one
// being deleted) only when forced; nothing when its outer bounds miss the
// region (or its clipper's visible region does).  The port's visRgn is
// narrowed to the view's (SetupVisRgn, unless gSkipVisRegions); PreDraw,
// then, for a vClipping view, the visRgn cut to the bounds while RealDraw,
// the viewDrawScript (its evt.ex errors dropped) and the children draw;
// PostDraw.  gOutlineViews frames every view in light gray.
void
TView::Draw(RgnHandle rgn, Boolean force)
{
	if (((fFlags & vVisible) == 0 || (fFlags & vIsInSetup2)) && !force)
		return;
	Rect outer;
	OuterBounds(&outer);
	Rect rgnBounds = (*rgn)->rgnBBox;
	Rect overlap;
	if (SectRect(&outer, &rgnBounds, &overlap))
	{
		Boolean intersects;
		TClipper* clipper = Clipper();
		if (clipper != nil && !gSkipVisRegions)
		{
			TRegionVar meet;
			SectRgn(clipper->fVisRgn, rgn, meet);
			intersects = !EmptyRgn(meet);
		}
		else
			intersects = RectInRgn(&outer, rgn);
		if (intersects)
		{
			GrafPort* port;
			GetPort(&port);
			RgnHandle savedVis = nil;
			RgnHandle savedClip = nil;
			if (!gSkipVisRegions && HasVisRgn())
			{
				TRegion saved(SetupVisRgn());
				savedVis = saved.StealRegion();
			}
			newton_try
			{
				if (savedVis == nil || !EmptyRgn(port->visRgn))
				{
					PreDraw(rgnBounds);
					if (fFlags & vClipping)
					{
						savedClip = NewCachedRgn();
						CopyRgn(port->visRgn, savedClip);
						TRectangularRegion bounds(viewBounds);
						SectRgn(port->visRgn, bounds, port->visRgn);
					}
					RealDraw(rgnBounds);
					if (fSlotCache & (1UL << kIndexViewDrawScript))
					{
						newton_try
						{
							RunCacheScript(kIndexViewDrawScript, RefVar(NILREF));
						}
						newton_catch(exRootException)
						{ }
						end_try;
					}
					DrawChildren(rgn, nil);
					if (fFlags & vClipping)
					{
						CopyRgn(savedClip, port->visRgn);
						DisposeCachedRgn(savedClip);
						savedClip = nil;
					}
					PostDraw(rgnBounds);
				}
			}
			newton_catch_all
			{
				if (savedClip != nil)
				{
					CopyRgn(savedClip, port->visRgn);
					DisposeCachedRgn(savedClip);
				}
				if (savedVis != nil)
				{
					CopyRgn(savedVis, port->visRgn);
					DisposeCachedRgn(savedVis);
				}
				NextHandler(&_info);
			}
			end_try;
			if (savedClip != nil)
			{
				CopyRgn(savedClip, port->visRgn);
				DisposeCachedRgn(savedClip);
			}
			if (savedVis != nil)
			{
				CopyRgn(savedVis, port->visRgn);
				DisposeCachedRgn(savedVis);
			}
		}
	}
	if (gOutlineViews)
	{
		PenState state;
		GetPenState(&state);
		SetFgPattern(GetStdPattern(ltGrayPat));
		FrameRect(&viewBounds);
		SetPenState(&state);
	}
}


// ROM 0x00267f88 Update__5TViewF11TBaseRegionP5TView
// An update: the port's visRgn cut to the region; the background erased
// when the filler (this view when none) has no fill; the filler's
// parent's children in front of the filler drawn and its parent's
// PostDraw; then the view drawn.  The visRgn is put back after.
void
TView::Update(RgnHandle rgn, TView* filler)
{
	TRegionVar saved;
	GrafPort* port;
	GetPort(&port);
	RgnHandle vis = port->visRgn;
	CopyRgn(vis, saved);
	newton_try
	{
		SectRgn(vis, rgn, vis);
		Rect bounds = (*rgn)->rgnBBox;
		TView* background = filler != nil ? filler : this;
		if ((background->fViewFormat & vfFillMask) == 0)
			EraseRgn(vis);
		if (filler != nil && filler != gRootView)
		{
			TView* parent = filler->fParent;
			parent->DrawChildren(rgn, filler);
			parent->PostDraw(bounds);
		}
		Draw(rgn, false);
	}
	newton_catch(exRootException)
	{ }
	end_try;
	GetPort(&port);
	CopyRgn(saved, port->visRgn);
}


// ROM 0x00268138 DrawChildren__5TViewFRC5TRectP5TView
void
TView::DrawChildren(const Rect& bounds, TView* after)
{
	TRectangularRegion rgn(bounds);
	DrawChildren(rgn, after);
}


// ROM 0x00268174 DrawChildren__5TViewF11TBaseRegionP5TView
// The children drawn back to front - those after the given one (in front
// of it) when one is given.  An evt.ex from a child ends the drawing.
void
TView::DrawChildren(RgnHandle rgn, TView* after)
{
	if (fChildren->GetArraySize() == 0)
		return;
	newton_try
	{
		TViewLoop loop(fChildren);
		for (TView* child = loop.Next(); child != nil; child = loop.Next())
		{
			if (after == nil)
				child->Draw(rgn, false);
			else if (after == child)
				after = nil;
		}
	}
	newton_catch(exRootException)
	{ }
	end_try;
}


// the bounds the format's fill and frame are drawn in: the bounds less
// the frame's pen and the inset
static void
FormatBounds(TView* view, Rect* bounds)
{
	*bounds = view->viewBounds;
	ULong format = view->fViewFormat;
	long pen = (format & vfFrameMask) != 0 ? (format & vfPenMask) >> vfPenShift : 0;
	long grow = pen + ((format & vfInsetMask) >> vfInsetShift);
	InsetRect(bounds, -grow, -grow);
}


// ROM 0x002682a8 PreDraw__5TViewFR5TRect
// The format's fill (a round rectangle when the corners are rounded) in
// its pattern (the custom one from viewFillPattern) and the lines
// (viewLineSpacing apart, from the top less the scroll origin, in patOr;
// a 'squareGrid viewGrid draws every other line and columns too) where
// the bounds meet the drawing.
void
TView::PreDraw(Rect& drawBounds)
{
	if ((fViewFormat & (vfFillMask | vfLinesMask)) == 0)
		return;
	Rect bounds;
	FormatBounds(this, &bounds);
	long round = ((fViewFormat & vfRoundMask) >> vfRoundShift) * 2;
	long fill = fViewFormat & vfFillMask;
	if (SetPattern(fill))
	{
		if (fill == vfFillCustom)
			SetCustomPattern(RSSYMviewfillpattern);
		PaintRoundRect(&bounds, round, round);
		if (fill == vfFillCustom)
			DisposeFgPattern();
	}
	long lines = (fViewFormat & vfLinesMask) >> vfLinesShift;
	if (SetPattern(lines))
	{
		RefVar spacingRef(GetProto(RSSYMviewlinespacing));
		if (NOTNIL(spacingRef))
		{
			long spacing = RINT(spacingRef);
			if (spacing > 0)
			{
				if (lines == vfLinesCustom >> vfLinesShift)
					SetCustomPattern(RSSYMviewlinepattern);
				Point origin;
				GetChildOrigin(&origin);
				long y = spacing + viewBounds.top;
				if (origin.v != 0)
					y -= origin.v % spacing;
				PenSize(1, 1);
				PenMode(patOr);
				Rect clip;
				SectRect(&drawBounds, &viewBounds, &clip);
				if (!EQRef(GetProto(RSSYMviewgrid), RSSYMsquaregrid))
				{
					for ( ; y <= clip.bottom; y += spacing)
						if (y >= clip.top)
						{
							MoveTo(viewBounds.left, y - 1);
							LineTo(viewBounds.right - 1, y - 1);
						}
				}
				else
				{
					for (y += spacing; y <= clip.bottom; y += spacing * 2)
						if (y >= clip.top)
						{
							MoveTo(viewBounds.left, y + 1);
							LineTo(viewBounds.right - 1, y + 1);
						}
					for (long x = spacing + viewBounds.left; x <= clip.right; x += spacing * 2)
						if (x >= clip.left)
						{
							MoveTo(x, viewBounds.top);
							LineTo(x, viewBounds.bottom - 1);
						}
				}
				if (lines == vfLinesCustom >> vfLinesShift)
					DisposeFgPattern();
			}
		}
	}
	PenNormal();
}


// ROM 0x002685f4 RealDraw__5TViewFR5TRect
// A plain view has no content of its own.
void
TView::RealDraw(Rect& /*bounds*/)
{ }


// ROM 0x002685f8 PostDraw__5TViewFR5TRect
// A selected view is hilited.  The frame in its pattern and pen (the
// custom one from viewFramePattern; the hilite and drag-shadow frames a
// gray frame with a black one of pen 2 - 4 for the caret slip - inside;
// NOT YET: the drag picture), round when the corners are; the drop shadow
// at the right and bottom, pen wide; the default button's marks: a
// double line above and below.
void
TView::PostDraw(Rect& /*drawBounds*/)
{
	if (fFlags & vSelected)
		Hilite(true);
	Rect bounds = viewBounds;
	long round = 0;
	if (fViewFormat & (vfFrameMask | vfShadowMask))
	{
		FormatBounds(this, &bounds);
		round = ((fViewFormat & vfRoundMask) >> vfRoundShift) * 2;
		long frame = (fViewFormat & vfFrameMask) >> 4;
		PatternHandle savedPattern = GetFgPattern();
		if (SetPattern(frame))
		{
			long pen = (fViewFormat & vfPenMask) >> vfPenShift;
			PenSize(pen, pen);
			Boolean grayed = false;
			if (frame == vfFrameCustom >> 4)
				SetCustomPattern(RSSYMviewframepattern);
			else if ((frame == vfFrameHilite >> 4 || frame == vfFrameDragShadow >> 4) && PixelMapDepth(&GetCurrentPort()->portBits) > 1)
			{
				grayed = true;
				SetFgPattern(GetStdPattern(grayPat));		// the ROM: a 60% gray of the depth (GetStdGrayPattern 0x9999)
			}
			if (round == 0)
				FrameRect(&bounds);
			else
				FrameRoundRect(&bounds, round, round);
			if (frame == vfFrameHilite >> 4 || frame == vfFrameDragShadow >> 4)
			{
				long inner = gRootView->fCaretSlip == this ? 4 : 2;
				PenSize(inner, inner);
				if (grayed)
					DisposeFgPattern();
				SetPattern(vfFillBlack);
				if (round == 0)
					FrameRect(&bounds);
				else
					FrameRoundRect(&bounds, round, round);
				// NOT YET RECONSTRUCTED: the drag-shadow frame's picture (RSbindi) at the top
			}
			else if (frame == vfFrameCustom >> 4)
				DisposeFgPattern();
			SetFgPattern(savedPattern);
		}
		long shadow = (fViewFormat & vfShadowMask) >> vfShadowShift;
		if (shadow != 0)
		{
			PenNormal();
			PenSize(shadow, shadow);
			MoveTo(bounds.right, bounds.top + shadow + round);
			LineTo(bounds.right, bounds.bottom);
			MoveTo(bounds.left + shadow + round, bounds.bottom);
			LineTo(bounds.right, bounds.bottom);
		}
	}
	if (gRootView->fDefaultButton == this)
	{
		Rect marks = bounds;
		RefVar buttonBounds(GetProto(RSSYM_defaultbuttonbounds));
		if (NOTNIL(buttonBounds))
		{
			FromObject(buttonBounds, marks);
			OffsetRect(&marks, viewBounds.left, viewBounds.top);
		}
		InsetRect(&marks, 2, 0);
		SetPattern(vfFillBlack);
		PenSize(1, 1);
		MoveTo(marks.left, marks.top - 2);
		LineTo(marks.right - 1, marks.top - 2);
		MoveTo(marks.left + 1, marks.top - 3);
		LineTo(marks.right - 2, marks.top - 3);
		MoveTo(marks.left, marks.bottom + 1);
		LineTo(marks.right - 1, marks.bottom + 1);
		MoveTo(marks.left + 1, marks.bottom + 2);
		LineTo(marks.right - 2, marks.bottom + 2);
	}
}


// ROM 0x0026ae20 Dirty__5TViewFPC5TRect
// The view (or the part of it in the rect) needs redrawing: its outer
// bounds, cut to each vClipping ancestor's bounds up to the window view,
// meet the window's visible region and go to the root view's update
// region with the first filled ancestor (or the window) as the filler.
// Nothing for a view that is not visible all the way up.
void
TView::Dirty(const Rect* rect)
{
	if (!VisibleDeep())
		return;
	Rect bounds;
	OuterBounds(&bounds);
	if (rect != nil)
		SectRect(rect, &bounds, &bounds);
	if (EmptyRect(&bounds))
		return;
	TView* filler = nil;
	TView* view = this;
	if (!view->HasVisRgn())
	{
		do
		{
			if (filler == nil && (view->fViewFormat & vfFillMask) != 0)
				filler = view;
			view = view->fParent;
			if (view->fFlags & vClipping)
				SectRect(&view->viewBounds, &bounds, &bounds);
			if (EmptyRect(&bounds))
				return;
		} while (!view->HasVisRgn());
	}
	TClipper* clipper = view->Clipper();
	TRegionVar dirty;
	TRectangularRegion r(bounds);
	SectRgn(r, clipper->fVisRgn, dirty);
	if (!EmptyRgn(dirty))
		gRootView->Invalidate(dirty, filler != nil ? filler : view);
}
