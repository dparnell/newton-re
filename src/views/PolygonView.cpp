/*
	File:		views/PolygonView.cpp

	Contains:	TPolygonView: a shape or a sketch on a page.  See
				PolygonView.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "PolygonView.h"
#include "Rerecognize.h"
#include "Controller.h"
#include "Areas.h"
#include "Commands.h"
#include "ViewFlags.h"
#include "Rects.h"
#include "Ports.h"
#include "Shapes.h"
#include "RegionVars.h"
#include "Ink.h"
#include "Angles.h"
#include "FixedMath.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "EditView.h"		// AlignToGrid
#include "DrawShape.h"		// MakePolygonForm
#include "Application.h"
#include "RootView.h"
#include "Interpreter.h"	// DoMessage
#include "OSErrors.h"
#include "NewtonExceptions.h"
#include <stdint.h>

TTransform	gEditViewTransform;


/*------------------------------------------------------------------------------
	T h e   s h a p e
------------------------------------------------------------------------------*/

// ROM 0x001911b4 Scale__12PolygonShapeFRC5TRectT1
void
PolygonShape::Scale(const Rect& from, const Rect& to)
{
	TTransform transform;
	transform.Setup(&from, &to, false);
	for (long i = 0; i < fCount; i++)
		::Scale(&fPoints[i], transform);
}

// ROM 0x00191210 Offset__12PolygonShapeFlT1
void
PolygonShape::Offset(long dh, long dv)
{
	for (long i = 0; i < fCount; i++)
	{
		fPoints[i].h = (short) (fPoints[i].h + (short) dh);
		fPoints[i].v = (short) (fPoints[i].v + (short) dv);
	}
}

// ROM 0x0019127c CalcBounds__12PolygonShapeFP5TRect
// The box round the points: started as the ROM's "nothing yet" rectangle
// (0x8000 at the top left, nought at the bottom right), which Union takes
// to mean empty.
void
PolygonShape::CalcBounds(Rect* bounds)
{
	bounds->top = (short) 0x8000;
	bounds->left = (short) 0x8000;
	bounds->bottom = 0;
	bounds->right = 0;
	for (long i = 0; i < fCount; i++)
		UnionPt(bounds, fPoints[i]);
}

// ROM 0x001912e0 IsCurvy__12PolygonShapeFv
// The verbs whose points are not corners: ink, the oval and the arc, and
// the four curved shapes the recogniser flattens into points.
Boolean
PolygonShape::IsCurvy(void)
{
	switch (fVerb)
	{
	case kPolyInk: case kPolyOval: case 1: case 2: case 6: case 7: case kPolyArc:
		return true;
	}
	return false;
}

// ROM 0x00191348 IsOval__12PolygonShapeFv
Boolean
PolygonShape::IsOval(void)
{
	return fVerb == kPolyOval || fVerb == kPolyArc;
}


/*------------------------------------------------------------------------------
	T h e   v i e w
------------------------------------------------------------------------------*/

// ROM 0x0018de14 ClassID__12TPolygonViewCFv
long
TPolygonView::ClassID(void) const
{
	return clPolygonView;
}

// ROM 0x0018e700 DerivedFrom__12TPolygonViewCFl
Boolean
TPolygonView::DerivedFrom(long id) const
{
	return id == clPolygonView || TDataView::DerivedFrom(id);
}

// ROM 0x00191088 Points__12TPolygonViewFv
Ref
TPolygonView::Points(void)
{
	return GetProto(RSSYMpoints);
}

// ROM 0x001910b4 GetPenSize__12TPolygonViewFv
long
TPolygonView::GetPenSize(void)
{
	return (long) ((fViewFormat & 0xf00) >> 8);
}

// ROM 0x0018c684 RealDraw__12TPolygonViewFR5TRect
void
TPolygonView::RealDraw(Rect& /*bounds*/)
{
	RefVar points(Points());
	DrawData((PolygonShape*) BinaryData(points), nil, nil);
}

// ROM 0x0018bfc4 GetArcBounds__12TPolygonViewFR5TRect
void
TPolygonView::GetArcBounds(Rect& bounds)
{
	RefVar arc(GetFrameSlot(RefVar(DataFrame()), RSSYMarcerbounds));
	if (ISNIL(arc))
		bounds = viewBounds;
	else
	{
		FromObject(arc, bounds);
		OffsetRect(&bounds, viewBounds.left, viewBounds.top);
	}
}

// ROM 0x0018c1ac DrawData__12TPolygonViewFP12PolygonShapeP6TPointT2
void
TPolygonView::DrawData(PolygonShape* shape, Point* from, Point* to)
{
	short top = viewBounds.top;
	short left = viewBounds.left;
	long pen = GetPenSize();
	PenSize(pen, pen);

	if (shape->fVerb == kPolyInk)
	{
		RefVar ink(GetProto(RSSYMink));
		GrafPtr port;
		GetPort(&port);
		if ((port->portBits.pixMapFlags & 0xf00) == 0)
		{
			// on the screen: the inker draws it when nothing is being
			// recorded and the view is wholly inside a rectangular
			// visible region
			Boolean useInker = false;
			RgnHandle vis = port->visRgn;
			// (the ROM asks CheckPic 0x00335030 too, which flushes what a
			//  picture being recorded is owed; the host records no
			//  pictures, so with picSave nil it answers false)
			if ((*vis)->rgnSize == 12 && port->picSave == nil)
				useInker = Encloses(&(*vis)->rgnBBox, &viewBounds);
			InkDraw(ink, (ULong) pen, left, top, useInker);
		}
		// NOT YET RECONSTRUCTED: a printer's port, where the ink is made
		// into outlined paths and framed (InkMakePaths, FramePaths)
		return;
	}

	Point* pts = shape->fPoints;
	if (!shape->IsOval())
	{
		TRegionVar saved;
		Boolean clipped = false;
		if (from != nil)
			GetClip(saved);
		long last = shape->fCount - 1;
		for (long i = 0; i <= last; i++)
		{
			Point pt;
			Boolean clip = false;
			if (from == nil)
				pt = pts[i];
			else
			{
				// the ends stand in for by the ones given, and the
				// segments they are on drawn only within the box the
				// old segment filled, widened by the pen
				if (i == 0)
					pt = *from;
				else
					pt = (i == last) ? *to : pts[i];
				clip = (i == last) || (i == 1);
				if (clip)
				{
					Rect box;
					Pt2Rect(pts[i - 1], pts[i], &box);
					OffsetRect(&box, left, top);
					::Scale(&box, gEditViewTransform);
					box.bottom = (short) (box.bottom + (short) pen);
					box.right = (short) (box.right + (short) pen);
					ClipRect(&box);
					clipped = true;
				}
			}
			pt.v = (short) (pt.v + top);
			pt.h = (short) (pt.h + left);
			::Scale(&pt, gEditViewTransform);
			if (i == 0)
				MoveTo(pt.h, pt.v);
			else
			{
				LineTo(pt.h, pt.v);
				if (clipped)
				{
					SetClip(saved);
					clipped = false;
				}
			}
		}
	}
	else
	{
		Rect box;
		GetArcBounds(box);
		long start, arc;
		if (shape->fVerb == kPolyArc)
		{
			Point first = pts[0];
			Point last = pts[shape->fCount - 1];
			first.v = (short) (first.v + top);
			first.h = (short) (first.h + left);
			last.v = (short) (last.v + top);
			last.h = (short) (last.h + left);
			CalcArcAngles(box, first, last, &start, &arc);
		}
		else
		{
			start = 0;
			arc = 360;
		}
		::Scale(&box, gEditViewTransform);
		FrameArc(&box, start, arc);
	}
}


/*------------------------------------------------------------------------------
	A r c s   a n d   g r i d s
------------------------------------------------------------------------------*/

// ROM 0x0018c148 CalcArcAngles__FRC5TRect6TPointT2PlT4
void
CalcArcAngles(const Rect& box, Point from, Point to, long* start, long* arc)
{
	long end;
	PtToAngle(&box, from, start);
	PtToAngle(&box, to, &end);
	end -= *start;
	*arc = end;
	if (end > 0)
		*arc = end - 360;
}

// ROM 0x002aa5b8 PtToAngle__FP4Rect5PointPl
void
PtToAngle(const Rect* box, Point pt, long* angle)
{
	long dv = pt.v - ((box->bottom + box->top) >> 1);
	long dh = pt.h - ((box->left + box->right) >> 1);
	long result;
	if (dh == 0)
		result = (dv > 0) ? 180 : 0;
	else
	{
		Fixed slope = FixedDivide(ToFixed(dh), ToFixed(dv));
		Fixed aspect = FixedDivide(ToFixed(box->bottom - box->top), ToFixed(box->right - box->left));
		slope = FixedMultiply(slope, aspect);
		result = (long) AngleFromSlope(slope);
		if (dh < 0)
		{
			result += 180;
			if (result == 360)
				result = 0;
		}
	}
	*angle = result;
}

// ROM 0x002628f8 AlignPtToGrid__FP6TPointR6TPoint
void
AlignPtToGrid(Point* pt, Point& spacing)
{
	pt->h = (short) AlignToGrid(pt->h, spacing.h);
	pt->v = (short) AlignToGrid(pt->v, spacing.v);
}

// ROM 0x00262988 AlignRectToGrid__FP5TRectR6TPoint
void
AlignRectToGrid(Rect* rect, Point& spacing)
{
	rect->left = (short) AlignToGrid(rect->left, spacing.h);
	rect->top = (short) AlignToGrid(rect->top, spacing.v);
	rect->right = (short) AlignToGrid(rect->right, spacing.h);
	rect->bottom = (short) AlignToGrid(rect->bottom, spacing.v);
}


/*------------------------------------------------------------------------------
	T h e   s e l e c t i o n
------------------------------------------------------------------------------*/

// ROM 0x00191364 __ct__14TPolygonHiliteFl
// Room for so many points behind a verb and a count; the count is set, the
// verb left for the maker.
TPolygonHilite::TPolygonHilite(long count)
{
	fShape = (PolygonShape*) NewPtr(count * (long) sizeof(Point) + 4);
	if (fShape == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	fShape->fCount = (short) count;
}

// ROM 0x001914ac __dt__14TPolygonHiliteFv
TPolygonHilite::~TPolygonHilite()
{
	DisposPtr((Ptr) fShape);
}

// ROM 0x001913f0 Clone__14TPolygonHiliteFv
THilite*
TPolygonHilite::Clone(void)
{
	TPolygonHilite* copy = new TPolygonHilite(fShape->fCount);
	if (copy == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	copy->CopyFrom(this);
	return copy;
}

// ROM 0x0019144c CopyFrom__14TPolygonHiliteFP7THilite
// Everything but the points block itself, whose contents are copied into
// this one's (which the caller made big enough).
void
TPolygonHilite::CopyFrom(THilite* other)
{
	TPolygonHilite* from = (TPolygonHilite*) other;
	fBounds = from->fBounds;
	fFirst = from->fFirst;
	fLast = from->fLast;
	fFirstPart = from->fFirstPart;
	fLastPart = from->fLastPart;
	fPenSize = from->fPenSize;
	BlockMove(from->fShape, fShape, from->fShape->fCount * (long) sizeof(Point) + 4);
}

// ROM 0x001914f4 UpdateBounds__14TPolygonHiliteFv
// The points' box grown by four all round and by four more at the bottom
// and right; ink keeps the bounds it was given.
void
TPolygonHilite::UpdateBounds(void)
{
	if (fShape->fVerb == kPolyInk)
		return;
	fShape->CalcBounds(&fBounds);
	InsetRect(&fBounds, -4, -4);
	fBounds.bottom = (short) (fBounds.bottom + 4);
	fBounds.right = (short) (fBounds.right + 4);
}

// ROM 0x00191568 Overlaps__14TPolygonHiliteFRC5TRect
Boolean
TPolygonHilite::Overlaps(const Rect& r)
{
	return THilite::Overlaps(r);
}

// ROM 0x0019156c Encloses__14TPolygonHiliteFRC6TPoint
// A shape of straight sides is picked up by a side, within sixteen pixels
// of it; a curved one anywhere in its box.
Boolean
TPolygonHilite::Encloses(const Point& pt)
{
	if (!fShape->IsCurvy())
	{
		for (long i = 1; i < fShape->fCount; i++)
		{
			Fixed along = LineHitRatio(pt, fShape->fPoints[i - 1], fShape->fPoints[i], 16);
			if (along != (Fixed) 0x80000000 && along < 0x10000)
				return true;
		}
		return false;
	}
	return pt.v >= fBounds.top && pt.v < fBounds.bottom
		&& pt.h >= fBounds.left && pt.h < fBounds.right;
}


// ROM 0x00198f74 LineHitRatio__6TPointCFRC6TPointT1l
// Measured along the segment's longer axis: the pen has to be within the
// segment's extent along it (widened by the slop at either end) and within
// the slop of the line across it; the answer is how far along that axis
// the pen is, as a fraction of the segment's length along it.
Fixed
LineHitRatio(const Point& pt, const Point& a, const Point& b, long slop)
{
	long dh = b.h - a.h;
	long dv = b.v - a.v;
	long adh = dh < 0 ? -dh : dh;
	long adv = dv < 0 ? -dv : dv;
	long along, length;
	if (adh > adv)
	{
		// mostly horizontal
		long h = pt.h;
		if (h < a.h - slop || h > b.h + slop)
		{
			if (h > a.h + slop || h < b.h - slop)
				return (Fixed) 0x80000000;
		}
		if (dh == 0)
		{
			// (unreachable: adh > adv >= 0)
			if (dv == 0)
				return (Fixed) 0x80000000;
			long off = a.v - pt.v;
			if (off < 0) off = -off;
			if (off >= slop)
				return (Fixed) 0x80000000;
			along = h - a.h;
			length = adh;
		}
		else
		{
			Fixed slope = FixedDivide(ToFixed(dv), ToFixed(dh));
			long v = (short) ((int32_t) ((h - a.h) * slope + 0x8000) >> 16) + a.v;
			long off = v - pt.v;
			if (off < 0) off = -off;
			if (off >= slop)
				return (Fixed) 0x80000000;
			along = h - a.h;
			length = b.h - a.h;
			if (length < 0) length = -length;
		}
	}
	else
	{
		// mostly vertical
		long v = pt.v;
		if (v < a.v - slop || v > b.v + slop)
		{
			if (v > a.v + slop || v < b.v - slop)
				return (Fixed) 0x80000000;
		}
		if (dv == 0)
		{
			if (dh == 0)
				return (Fixed) 0x80000000;
			long off = a.h - pt.h;
			if (off < 0) off = -off;
			if (off >= slop)
				return (Fixed) 0x80000000;
			along = v - a.v;
			length = adv;
		}
		else
		{
			Fixed slope = FixedDivide(ToFixed(dh), ToFixed(dv));
			long h = (short) ((int32_t) ((v - a.v) * slope + 0x8000) >> 16) + a.h;
			long off = h - pt.h;
			if (off < 0) off = -off;
			if (off >= slop)
				return (Fixed) 0x80000000;
			along = v - a.v;
			length = b.v - a.v;
			if (length < 0) length = -length;
		}
	}
	if (along < 0)
		along = -along;
	return FixedDivide(ToFixed(along), ToFixed(length));
}


// ROM 0x0018ccf0 LessOrEq__FlN31
Boolean
LessOrEq(long first, long startPart, long last, long endPart)
{
	return first < last - 1 || (first == last - 1 && startPart <= endPart);
}


// ROM 0x0018dd44 UpdatePenSizePalette__Fv
// The style palette, when it is open, told to show the selection's pen.
void
UpdatePenSizePalette(void)
{
	RefVar palette(gRootView->GetVar(RSSYMstylepalette));
	TView* view = GetView(palette);
	if (view != nil && (view->fFlags & vVisible) != 0)
		DoMessage(palette, RSSYMsyncpensize, RefVar(NILREF));
}


// ROM 0x0018bee0 CalcHiliteBounds__12TPolygonViewFP12PolygonShapeP5TRect
// The box a selection of these points is drawn in: the points' own box, or
// the whole view for ink, grown by four all round and four more at the
// bottom and right.
void
TPolygonView::CalcHiliteBounds(PolygonShape* shape, Rect* bounds)
{
	if (shape->fVerb == kPolyInk)
		SetRect(bounds, 0, 0, viewBounds.right - viewBounds.left, viewBounds.bottom - viewBounds.top);
	else
		shape->CalcBounds(bounds);
	InsetRect(bounds, -4, -4);
	bounds->bottom = (short) (bounds->bottom + 4);
	bounds->right = (short) (bounds->right + 4);
}


// ROM 0x001909a4 MakeHilite__12TPolygonViewFlN31
// The shape selected from point `first`, `startPart` of the way along the
// segment after it, to point `last`, `endPart` of the way along the one
// before it.  A start at the very end of its segment is taken to be the
// next point, an end at the very start of its segment the point before
// (round a closed shape either way); a range that runs past the last
// point wraps round to the first.  The hilite keeps a copy of the points
// it covers, the ends moved along their segments to where the range
// starts and ends, with the shape's verb when the whole of it is selected
// and an open polyline's, curve's or arc's when only part is.  Whatever
// was selected before is not any more.
void
TPolygonView::MakeHilite(long first, long startPart, long last, long endPart)
{
	RefVar points(Points());
	PolygonShape* shape = (PolygonShape*) BinaryData(points);
	RefVar old(FirstHilite());
	if (NOTNIL(old))
		RemoveHilite(old);
	long count = shape->fCount;

	if (startPart < 1)
		startPart = 0;
	else if (startPart >= 0xffff)
	{
		startPart = 0;
		first++;
		if (first > count - 2)
			first = 0;
	}
	if (endPart < 1)
	{
		endPart = 0x10000;
		last--;
		if (last < 0)
			last = count - 2;
	}
	else if (endPart >= 0xffff)
		endPart = 0x10000;

	long n = last - first + 1;
	Boolean inOrder = LessOrEq(first, startPart, last, endPart);
	if (!inOrder)
		n += count - 1;
	TPolygonHilite* hilite = new TPolygonHilite(n);
	if (hilite == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	hilite->fFirst = first;
	hilite->fLast = last;
	hilite->fFirstPart = startPart;
	hilite->fLastPart = endPart;
	hilite->fShape->fVerb = shape->fVerb;
	hilite->fPenSize = GetPenSize();
	Point* to = hilite->fShape->fPoints;
	if (!inOrder)
	{
		// round the end of a closed shape: from `first` to the last point,
		// then on from the second (the first being the last again)
		BlockMove(&shape->fPoints[first], to, (count - first) * (long) sizeof(Point));
		BlockMove(&shape->fPoints[1], to + (count - first), last * (long) sizeof(Point));
	}
	else
		BlockMove(&shape->fPoints[first], to, n * (long) sizeof(Point));

	if (!(hilite->fFirst == 0 && count - 1 <= hilite->fLast
		  && hilite->fFirstPart == 0 && hilite->fLastPart >= 0xffff))
	{
		// only part of it: an open line of the same kind
		if (shape->IsOval())
			hilite->fShape->fVerb = kPolyArc;
		else if (shape->IsCurvy())
			hilite->fShape->fVerb = 7;
		else
			hilite->fShape->fVerb = 5;
	}

	// the ends moved along their segments (the start worked out first,
	// from the points as copied, and put in last)
	Point start;
	if (startPart != 0)
	{
		Point p0 = to[0], p1 = to[1];
		start.h = (short) (p0.h + ((int32_t) (startPart * (p1.h - p0.h)) >> 16));
		start.v = (short) (p0.v + ((int32_t) (startPart * (p1.v - p0.v)) >> 16));
	}
	if (endPart < 0xffff)
	{
		Point* end = &to[n - 1];
		Point prev = to[n - 2];
		end->h = (short) (end->h - ((int32_t) ((0xffff - endPart) * (end->h - prev.h)) >> 16));
		end->v = (short) (end->v - ((int32_t) ((0xffff - endPart) * (end->v - prev.v)) >> 16));
	}
	if (startPart != 0)
		to[0] = start;

	CalcHiliteBounds(hilite->fShape, &hilite->fBounds);
	AddHilite(hilite);
}


// ROM 0x00190d70 MakeInkHilite__12TPolygonViewFv
// Ink is selected whole, with no points of its own.
void
TPolygonView::MakeInkHilite(void)
{
	RefVar old(FirstHilite());
	if (NOTNIL(old))
		RemoveHilite(old);
	RefVar points(Points());
	PolygonShape* shape = (PolygonShape*) BinaryData(points);
	TPolygonHilite* hilite = new TPolygonHilite(0);
	if (hilite == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	hilite->fFirst = 0;
	hilite->fLast = shape->fCount - 1;
	hilite->fFirstPart = 0;
	hilite->fLastPart = 0xffff;
	hilite->fShape->fVerb = shape->fVerb;
	hilite->fPenSize = GetPenSize();
	CalcHiliteBounds(hilite->fShape, &hilite->fBounds);
	AddHilite(hilite);
}


// ROM 0x0018dcc8 AddHilite__12TPolygonViewFP14TPolygonHilite
// Through the undoable aeAddHilite, and the palette told.
void
TPolygonView::AddHilite(TPolygonHilite* hilite)
{
	RefVar cmd(MakeCommand(aeAddHilite, this, 0x8000000));
	CommandSetFrameParameter(cmd, RefVar(AddressToRef(hilite)));
	gApplication->DispatchCommand(cmd);
	PolygonHiliteChanged();
}


// ROM 0x0018ddc8 PolygonHiliteChanged__12TPolygonViewFv
// vars.lastPolyHiliteChanged says which shape it was, for the palette.
void
TPolygonView::PolygonHiliteChanged(void)
{
	SetFrameSlot(RefVar(gVarFrame), RSSYMlastpolyhilitechanged, fContext);
	UpdatePenSizePalette();
}


// ROM 0x0018de1c RemoveHilite__12TPolygonViewFRC6RefVar
void
TPolygonView::RemoveHilite(RefArg hilite)
{
	TView::RemoveHilite(hilite);
	UpdatePenSizePalette();
}


// ROM 0x0019184c HiliteAll__12TPolygonViewFv
void
TPolygonView::HiliteAll(void)
{
	RefVar points(Points());
	PolygonShape* shape = (PolygonShape*) BinaryData(points);
	if (shape->fVerb == kPolyInk)
		MakeInkHilite();
	else
		MakeHilite(0, 0, shape->fCount - 1, 0xffff);
}


// ROM 0x001912ec HandleHilite__12TPolygonViewFP11TUnitPubliclUc
// A stroke traced along the shape selects the part of it traced (kind 6,
// offered when the editor has no claim yet or has taken a trace);
// otherwise a stroke over most of it selects the whole of it, as for any
// view (the ROM has TView's test inline here).
long
TPolygonView::HandleHilite(TUnitPublic* unit, long kind, Boolean reallyDoIt)
{
	if ((kind == 6 || kind == -1) && HiliteTraced(unit, reallyDoIt))
		return 6;
	return TView::HandleHilite(unit, kind, reallyDoIt);
}


// ROM 0x0018b5c8 IsCompletelyHilited__12TPolygonViewFRC6RefVar
Boolean
TPolygonView::IsCompletelyHilited(RefArg hiliteRef)
{
	RefVar points(Points());
	PolygonShape* shape = (PolygonShape*) BinaryData(points);
	TPolygonHilite* hilite = (TPolygonHilite*) RefToAddress(hiliteRef);
	return hilite->fFirst == 0 && shape->fCount - 1 <= hilite->fLast
		&& hilite->fFirstPart == 0 && hilite->fLastPart >= 0xffff;
}


// ROM 0x0018b554 ClickOptions__12TPolygonViewFv
// 1 (it may be dragged), 2 as well when the whole shape is selected (it may
// be resized), and 4 when its corners are points that may be dragged -
// a shape of straight sides.
long
TPolygonView::ClickOptions(void)
{
	long options = 1;
	RefVar hilite(FirstHilite());
	if (NOTNIL(hilite) && IsCompletelyHilited(hilite))
		options = 3;
	RefVar points(Points());
	if (!((PolygonShape*) BinaryData(points))->IsCurvy())
		options += 4;
	return options;
}


// ROM 0x0018b54c GlobalHiliteResizeBounds__12TPolygonViewFP5TRect
// A shape is resized by the box round what is selected of it, not by the
// view's bounds.
void
TPolygonView::GlobalHiliteResizeBounds(Rect* bounds)
{
	GlobalHiliteBounds(bounds);
}


// ROM 0x000a31d8 DrawHilitedData__9TDataViewFv (in DataView.cpp)

// ROM 0x0018c6ec DrawHilitedData__12TPolygonViewFv
// What is selected, drawn: the hilite's points, the ends of a partial
// selection standing in for the points they are part of the way along.
void
TPolygonView::DrawHilitedData(void)
{
	RefVar hiliteRef(FirstHilite());
	if (ISNIL(hiliteRef))
	{
		TDataView::DrawHilitedData();
		return;
	}
	RefVar points(Points());
	PolygonShape* shape = (PolygonShape*) BinaryData(points);
	TPolygonHilite* hilite = (TPolygonHilite*) RefToAddress(hiliteRef);
	Point* from = nil;
	Point* to = nil;
	if ((hilite->fFirstPart > 0 && hilite->fFirstPart < 0x10000)
	 || (hilite->fLastPart > 0 && hilite->fLastPart < 0x10000))
	{
		from = &shape->fPoints[hilite->fFirst];
		to = &shape->fPoints[hilite->fLast];
	}
	DrawData(hilite->fShape, from, to);
}


// ROM 0x0018c7d4 DrawHilites__12TPolygonViewFUc
// The selection shown in two passes (TEditView::DrawHiliting): first
// (`on` false) a thick black line over what is selected - a round
// rectangle round ink, an eight-pixel arc over an oval or arc, a hilite
// line along each side - and then (`on` true) a white dot on each corner
// of a shape of straight sides, the handles TrackDistort drags.
void
TPolygonView::DrawHilites(Boolean on)
{
	RefVar hiliteRef(FirstHilite());
	if (ISNIL(hiliteRef))
		return;
	TPolygonHilite* hilite = (TPolygonHilite*) RefToAddress(hiliteRef);
	PolygonShape* shape = hilite->fShape;
	long pen = GetPenSize();
	short top = viewBounds.top;
	short left = viewBounds.left;
	if (!on)
	{
		if (shape->fVerb == kPolyInk)
		{
			Rect r = viewBounds;
			InsetRect(&r, -4, -4);
			r.bottom = (short) (r.bottom + (short) pen);
			r.right = (short) (r.right + (short) pen);
			::Scale(&r, gEditViewTransform);
			FillRoundRect(&r, 8, 8, GetStdPattern(blackPat));
		}
		else if (!shape->IsOval())
		{
			Point prev;
			for (long i = 0; i < shape->fCount; i++)
			{
				Point pt;
				pt.v = (short) (shape->fPoints[i].v + top);
				pt.h = (short) (shape->fPoints[i].h + left);
				::Scale(&pt, gEditViewTransform);
				if (i != 0)
					DrawHiliteLine(prev, pt, GetStdPattern(blackPat), false);
				prev = pt;
			}
		}
		else
		{
			Rect box;
			GetArcBounds(box);
			long start, arc;
			if (shape->fVerb == kPolyArc)
			{
				Point a = shape->fPoints[0];
				Point b = shape->fPoints[shape->fCount - 1];
				a.v = (short) (a.v + top);  a.h = (short) (a.h + left);
				b.v = (short) (b.v + top);  b.h = (short) (b.h + left);
				CalcArcAngles(box, a, b, &start, &arc);
			}
			else
			{
				start = 0;
				arc = 360;
			}
			::Scale(&box, gEditViewTransform);
			short lo = (short) ((short) (pen - 8) / 2);
			short hi = (short) ((short) (pen + 8) / 2);
			box.top = (short) (box.top + lo);
			box.left = (short) (box.left + lo);
			box.bottom = (short) (box.bottom + hi);
			box.right = (short) (box.right + hi);
			PenSize(8, 8);
			SetFgPattern(GetStdPattern(blackPat));
			FrameArc(&box, start, arc);
		}
	}
	else if (!shape->IsCurvy())
	{
		for (long i = 0; i < shape->fCount; i++)
		{
			Point pt;
			pt.v = (short) (shape->fPoints[i].v + top);
			pt.h = (short) (shape->fPoints[i].h + left);
			::Scale(&pt, gEditViewTransform);
			Rect dot;
			SetRect(&dot, pt.h - 3, pt.v - 3, pt.h + 3, pt.v + 3);
			FillOval(&dot, GetStdPattern(whitePat));
		}
	}
}


// ROM 0x0018be20 DeleteHilited__12TPolygonViewFRC6RefVar
// What is selected taken out of the shape; when that is all of it the
// parent removes the view (aeRemoveData), otherwise only the selection
// goes.  A read-only or write-protected shape keeps its points.
void
TPolygonView::DeleteHilited(RefArg hiliteRef)
{
	if (ISNIL(hiliteRef))
		return;
	if ((fFlags & (vReadOnly | vWriteProtected)) == 0)
	{
		TPolygonHilite* hilite = (TPolygonHilite*) RefToAddress(hiliteRef);
		PolygonShape* shape = hilite->fShape;
		if (RemovePoints(&shape->fPoints[0], &shape->fPoints[shape->fCount - 1],
						 hilite->fFirst, hilite->fLast, hilite->fFirstPart, hilite->fLastPart))
		{
			RefVar cmd(MakeCommand(aeRemoveData, fParent, fId));
			gApplication->DispatchCommand(cmd);
			return;
		}
	}
	RemoveHilite(hiliteRef);
}


// ROM 0x0018b67c OuterBounds__12TPolygonViewFP5TRect
// The shape's line is drawn with the pen's top left on each point, so it
// reaches the pen's width beyond the points' box at the bottom and the
// right (a level line's box has no height at all); a selection's thick
// line and dots reach four further all round.
void
TPolygonView::OuterBounds(Rect* bounds)
{
	TView::OuterBounds(bounds);
	short pen = (short) GetPenSize();
	bounds->right = (short) (bounds->right + pen);
	bounds->bottom = (short) (bounds->bottom + pen);
	if (Hilited())
		InsetRect(bounds, -4, -4);
}


// ROM 0x0018d0d4 MakePointsCommand__12TPolygonViewFUll
Ref
TPolygonView::MakePointsCommand(ULong id, long count)
{
	RefVar cmd(MakeCommand(id, this, 0x8000000));
	RefVar points(AllocateBinary(RSSYMpolygonshape, count * 4));
	CommandSetPoints(cmd, points);
	return cmd;
}


// ROM 0x0018bfb4 CommandSetPoints__FRC6RefVarT1
void
CommandSetPoints(RefArg cmd, RefArg points)
{
	SetFrameSlot(cmd, RSSYMpoints, points);
}


// ROM 0x0018cd1c CommandPoints__FRC6RefVar
Ref
CommandPoints(RefArg cmd)
{
	return GetFrameSlotRef(cmd, RSSYMpoints);
}


// ROM 0x0018b2c8 ValidatePoly__F7DataPtrRC5TRect
// ROM bug kept (to no effect): the points' box, moved to the rectangle's
// place, is compared with the rectangle and EmptyRect asked of it when
// they differ, but the flag that was meant to say so is never set - so a
// closed verb of any number of points besides 0, 3, 8, 9 and 10-12 is
// never turned into the plain polygon.
void
ValidatePoly(RefArg points, const Rect& bounds)
{
	PolygonShape* shape = (PolygonShape*) BinaryData(points);
	if (shape->fCount < 0)
	{
		shape->fCount = 0;
		return;
	}
	if (shape->fCount == 0)
	{
		if (shape->fVerb != 15 && shape->fVerb != kPolyInk)
			shape->fVerb = 15;
		return;
	}
	Boolean badBounds = false;
	Rect box;
	shape->CalcBounds(&box);
	OffsetRect(&box, bounds.left, bounds.top);
	if (!EqualRect(&bounds, &box))
		(void) EmptyRect(&bounds);				// (the answer goes nowhere)
	long count = shape->fCount;
	Boolean closes = *(ULong32*) &shape->fPoints[0] == *(ULong32*) &shape->fPoints[count - 1];
	Boolean fix;
	switch (shape->fVerb)
	{
	case 0:							fix = count != 0x19; break;
	case 3:							fix = count != 1; break;
	case 8:							fix = count != 2; break;
	case 9:							fix = count != 4; break;
	case 10: case 11: case 12:		fix = count != 5; break;
	default:						fix = badBounds; break;
	}
	if (fix)
		shape->fVerb = closes ? 4 : 5;
	if (count == 1)
	{
		if (shape->fVerb != 3)
			shape->fVerb = 3;
	}
	else if (count == 2)
	{
		if (shape->fVerb != 8 && shape->fVerb != kPolyArc)
			shape->fVerb = 8;
	}
	else if (count == 4)
	{
		if (shape->fVerb != 9 && closes)
			shape->fVerb = 9;
	}
	if (IsClosed(shape->fVerb) && !closes)
		shape->fVerb = shape->fVerb == 6 ? 7 : 5;
}


// ROM 0x0018d144 RemovePoints__12TPolygonViewFP6TPointT1lN33
// `from` and `to` are the selection's own first and last points (where
// the cut ends go).  Ink, or a range from the very start to the very end,
// is the whole shape: ==> true, and the caller removes the view.  An
// empty range removes nothing.  Otherwise what follows the range becomes
// a shape of its own - an open one (an oval's piece an arc), starting at
// `to` - added to the page through aeAddData; on a closed shape the piece
// before the range goes round into it too (ending at `from`), and the
// shape itself is then wholly replaced (==> true).  What precedes the
// range on an open shape stays, cut back to end at `from` through the
// undoable points command 0x44 (==> false: the view stays).
Boolean
TPolygonView::RemovePoints(Point* from, Point* to, long first, long last, long startPart, long endPart)
{
	RefVar points(Points());
	PolygonShape* shape = (PolygonShape*) BinaryData(points);
	if (startPart > 0xfffe)
		startPart = 0x10000;
	if (endPart > 0xfffe)
		endPart = 0x10000;
	long start = startPart + first * 0x10000;
	long end = endPart + (last - 1) * 0x10000;
	long lastPoint = shape->fCount - 1;
	Boolean before = start > 0;
	Boolean after = end < lastPoint * 0x10000;
	long keep = (start + 0xffff) >> 16;			// the points before the range: 0 to keep-1
	long resume = end >> 16;					// the point the piece after it goes on from
	if (shape->fVerb == kPolyInk || (start == 0 && end == lastPoint * 0x10000))
		return true;
	if (start == end)
		return false;
	Rect bounds = viewBounds;
	Rect arcBounds;
	if (shape->IsOval())
		GetArcBounds(arcBounds);
	if (after)
	{
		Point origin = fParent->ContentsOrigin();
		OffsetRect(&bounds, (short) -origin.h, (short) -origin.v);
		long verb = shape->IsOval() ? kPolyArc : shape->IsCurvy() ? 7 : 5;
		Boolean wraps = false;
		Boolean closesWith = false;
		long endIndex;
		if (before && IsClosed(shape->fVerb))
		{
			before = false;
			closesWith = true;
			endIndex = keep;
			if (LessOrEq(first, startPart, last, endPart))
				wraps = true;
		}
		else
			endIndex = lastPoint;
		long count = (wraps ? lastPoint - resume + keep : endIndex - resume) + 1;
		RefVar cmd(MakeCommand(aeAddData, fParent, 0x8000000));
		RefVar form(MakePolygonForm(shape->fPoints, count, verb, bounds, GetPenSize()));
		CommandSetFrameParameter(cmd, form);
		RefVar newPoints(GetFrameSlotRef(RefVar(CommandFrameParameter(cmd)), RSSYMpoints));
		PolygonShape* newShape = (PolygonShape*) BinaryData(newPoints);
		shape = (PolygonShape*) BinaryData(points);
		if (!wraps)
			memmove(newShape->fPoints, &shape->fPoints[resume], count * sizeof(Point));
		else
		{
			long k = lastPoint - resume + 1;
			memmove(newShape->fPoints, &shape->fPoints[resume], k * sizeof(Point));
			memmove(&newShape->fPoints[k], &shape->fPoints[1], keep * sizeof(Point));
		}
		newShape->fPoints[0] = *to;
		if (closesWith)
			newShape->fPoints[count - 1] = *from;
		Rect box;
		newShape->CalcBounds(&box);
		Rect placed = box;
		OffsetRect(&placed, viewBounds.left, viewBounds.top);
		ValidatePoly(newPoints, placed);
		newShape = (PolygonShape*) BinaryData(newPoints);
		newShape->Offset(-box.left, -box.top);
		Point local = LocalOrigin();
		OffsetRect(&box, local.h, local.v);
		RefVar frame(CommandFrameParameter(cmd));
		SetFrameSlot(frame, RSSYMviewbounds, RefVar(ToObject(box)));
		gApplication->DispatchCommand(cmd);
		if (verb == kPolyArc)
		{
			TPolygonView* view = (TPolygonView*) CommandParameter(cmd);
			if (view != nil)
				view->SetArcBounds(arcBounds);
		}
	}
	if (before)
	{
		shape = (PolygonShape*) BinaryData(points);
		RefVar cmd(MakePointsCommand(0x44, 1));
		CommandSetIndexParameter(cmd, 0, keep);
		CommandSetIndexParameter(cmd, 1, shape->fCount - keep);
		CommandSetIndexParameter(cmd, 2, 1);
		CommandSetIndexParameter(cmd, 3, 0);
		RefVar cmdPoints(CommandPoints(cmd));
		*(Point*) BinaryData(cmdPoints) = *from;
		gApplication->DispatchCommand(cmd);
		// (the ROM hands UpdateBounds the points it had before the command)
		points = Points();
		UpdateBounds((PolygonShape*) BinaryData(points));
		shape = (PolygonShape*) BinaryData(points);
		if (shape->IsOval())
			SetArcBounds(arcBounds);
		Rect r = viewBounds;
		ValidatePoly(RefVar(Points()), r);
	}
	return !before;
}


// ROM 0x0018b700 AddHilited__12TPolygonViewFRC6RefVarP9TEditView
// The selected points made a shape of their own on the page, where they
// are, with the selection's verb and pen; an oval's or arc's box is kept
// for it.  The new shape is selected whole.
TView*
TPolygonView::AddHilited(RefArg hiliteRef, TEditView* editor)
{
	Boolean keepArc = false;
	TPolygonHilite* hilite = (TPolygonHilite*) RefToAddress(hiliteRef);
	Point origin = LocalOrigin();
	Rect box;
	box.top = origin.v;
	box.left = origin.h;
	box.bottom = (short) (viewBounds.bottom - viewBounds.top + origin.v);
	box.right = (short) (viewBounds.right - viewBounds.left + origin.h);
	PolygonShape* sel = hilite->fShape;
	RefVar form(MakePolygonForm(sel->fPoints, sel->fCount, sel->fVerb, box, hilite->fPenSize));
	RefVar points(Points());
	PolygonShape* shape = (PolygonShape*) BinaryData(points);
	Rect arcBounds;
	if (shape->IsOval() && (shape->fVerb == kPolyArc || !IsCompletelyHilited(hiliteRef)))
	{
		keepArc = true;
		GetArcBounds(arcBounds);
	}
	TPolygonView* view = (TPolygonView*) editor->AddForm(form);
	points = view->Points();
	view->UpdateBounds((PolygonShape*) BinaryData(points));
	if (keepArc)
		view->SetArcBounds(arcBounds);
	view->HiliteAll();
	return view;
}


// ROM 0x00190ea0 UpdateBounds__12TPolygonViewFP12PolygonShape
// A view that works its bounds out from its data (vCalculateBounds) fitted
// round its points again: the points moved so the nearest to the top left
// is at nought (the selection's with them), and the view's bounds the box
// round them where the view was - one pixel across at least.
void
TPolygonView::UpdateBounds(PolygonShape* shape)
{
	if ((fFlags & vCalculateBounds) == 0)
		return;
	RefVar hiliteRef(FirstHilite());
	Rect r;
	shape->CalcBounds(&r);
	short dh = (short) -r.left;
	short dv = (short) -r.top;
	if (dh != 0 || dv != 0)
	{
		RefVar points(Points());
		((PolygonShape*) BinaryData(points))->Offset(dh, dv);
		if (NOTNIL(hiliteRef))
			((TPolygonHilite*) RefToAddress(hiliteRef))->fShape->Offset(dh, dv);
	}
	Point origin = LocalOrigin();
	OffsetRect(&r, origin.h, origin.v);
	if (EmptyRect(&r))
	{
		if (r.bottom == r.top)
			r.bottom = (short) (r.bottom + 1);
		if (r.right == r.left)
			r.right = (short) (r.right + 1);
	}
	WriteBounds(r);
	if (NOTNIL(hiliteRef))
	{
		TPolygonHilite* hilite = (TPolygonHilite*) RefToAddress(hiliteRef);
		CalcHiliteBounds(hilite->fShape, &hilite->fBounds);
	}
}


// ROM 0x0018c070 SetArcBounds__12TPolygonViewFRC5TRect
// The data frame's arcerBounds, in the view's own coordinates.
void
TPolygonView::SetArcBounds(const Rect& bounds)
{
	Rect r = bounds;
	OffsetRect(&r, -viewBounds.left, -viewBounds.top);
	RefVar arc(ToObject(r));
	SetFrameSlot(RefVar(DataFrame()), RSSYMarcerbounds, arc);
}


// ROM 0x001910c4 SetPenSize__12TPolygonViewFl
// Into the pen nibble of the data frame's viewFormat and the view's own,
// and the selection's.
void
TPolygonView::SetPenSize(long pen)
{
	RefVar format(GetFrameSlotRef(RefVar(DataFrame()), RSSYMviewformat));
	ULong value = ISNIL(format) ? 0 : (ULong) RINT(format);
	value = (value & 0xfffff0ff) | (ULong) (pen << 8);
	fViewFormat = value;
	SetDataSlot(RSSYMviewformat, RefVar(MAKEINT(value)));
	RefVar hiliteRef(FirstHilite());
	if (NOTNIL(hiliteRef))
		((TPolygonHilite*) RefToAddress(hiliteRef))->fPenSize = pen;
	Changed(RSSYMviewformat);
}


// ROM 0x0018ffbc RealDoCommand__12TPolygonViewFRC6RefVar
// Command 0x19 (Rerecognize.h): a shape with ink reads it again - brought
// to the front, the arrow drawn over it when asked, its recognition flag
// 0x1000 cleared, and its strokes read in an area made of the command's
// configuration, with the controller's state put aside meanwhile; a
// shape with no ink leaves the command untaken.
// Command 0x43: point param0 of the shape moved to param1 (a point on
// the page, v in the high half), the selection's copy with it; a
// rectangle, square or diamond whose corner has moved is a plain closed
// polygon (verb 4) from then on.  There is no undo.  An index past the
// points leaves the command untaken.
// Command 0x4b: the selection's pen size (param), cut out into a shape of
// its own first when only part is selected; undoable when it was the
// whole shape.  Taken whatever.
// Command 0x44: points replaced - param1 points at param0 taken out and
// the command's own 'points put in instead (param2 of them), the verb
// then param3 - 1, or when param3 is nought the open verb the shape's
// kind has; its undo is the same command putting back what was taken
// out (moved as far as the view's bounds moved).  RemovePoints cuts a
// shape back with it.
// NOT YET RECONSTRUCTED: 0x32 (the double tap's reading of ink); it goes
// to TView's as before.
Boolean
TPolygonView::RealDoCommand(RefArg cmd)
{
	long id = CommandID(cmd);
	if (id == 0x44)
	{
		RefVar points(Points());
		if (ISNIL(points))
			return false;
		long at = CommandIndexParameter(cmd, 0);
		long removed = CommandIndexParameter(cmd, 1);
		long inserted = CommandIndexParameter(cmd, 2);
		long verb = CommandIndexParameter(cmd, 3);
		PolygonShape* shape = (PolygonShape*) BinaryData(points);
		short oldTop = viewBounds.top;
		short oldLeft = viewBounds.left;
		Rect arcBounds;
		if (shape->IsOval())
			GetArcBounds(arcBounds);
		RefVar newPoints(CommandPoints(cmd));
		RefVar undo(MakePointsCommand(0x44, removed));
		CommandSetIndexParameter(undo, 0, at);
		CommandSetIndexParameter(undo, 1, inserted);
		CommandSetIndexParameter(undo, 2, removed);
		shape = (PolygonShape*) BinaryData(points);
		CommandSetIndexParameter(undo, 3, shape->fVerb + 1);
		RefVar undoPoints(CommandPoints(undo));
		shape = (PolygonShape*) BinaryData(points);
		if (removed > 0)
			memmove(BinaryData(undoPoints), &shape->fPoints[at], removed * sizeof(Point));
		// (the host copies the points put in aside: Munger may move the heap)
		Point* data = nil;
		if (NOTNIL(newPoints) && inserted > 0)
		{
			data = (Point*) NewPtr(inserted * (long) sizeof(Point));
			memmove(data, BinaryData(newPoints), inserted * sizeof(Point));
		}
		RefVar munged(Munger(points, 4 + at * 4, removed * 4, data, inserted * 4));
		if (data != nil)
			DisposPtr((Ptr) data);
		shape = (PolygonShape*) BinaryData(munged);
		shape->fCount = (short) (shape->fCount + (inserted - removed));
		UpdateBounds(shape);
		Point* saved = (Point*) BinaryData(undoPoints);
		short dv = (short) (oldTop - viewBounds.top);
		short dh = (short) (oldLeft - viewBounds.left);
		for (long i = 0; i < removed; i++)
		{
			saved[i].v = (short) (saved[i].v + dv);
			saved[i].h = (short) (saved[i].h + dh);
		}
		shape = (PolygonShape*) BinaryData(munged);
		if (shape->IsOval())
			SetArcBounds(arcBounds);
		shape = (PolygonShape*) BinaryData(munged);
		if (verb == 0)
			shape->fVerb = shape->IsOval() ? kPolyArc : shape->IsCurvy() ? 7 : 5;
		else
			shape->fVerb = (short) (verb - 1);
		SetValue(RSSYMpoints, munged);
		gApplication->PostUndoCommand(undo);
		fParent->Dirty(nil);
		return true;
	}
	if (id == 0x43)
	{
		long index = CommandIndexParameter(cmd, 0);
		RefVar points(Points());
		PolygonShape* shape = (PolygonShape*) BinaryData(points);
		if (index >= shape->fCount)
			return false;
		ULong where = (ULong) CommandIndexParameter(cmd, 1);
		Point pt;
		pt.v = (short) ((short) (where >> 16) - viewBounds.top);
		pt.h = (short) ((short) where - viewBounds.left);
		shape->fPoints[index] = pt;
		if (shape->fVerb == 10 || shape->fVerb == 11 || shape->fVerb == 12)
			shape->fVerb = 4;
		RefVar hiliteRef(FirstHilite());
		if (NOTNIL(hiliteRef))
		{
			TPolygonHilite* hilite = (TPolygonHilite*) RefToAddress(hiliteRef);
			if (index < hilite->fShape->fCount)
			{
				hilite->fShape->fPoints[index] = pt;
				hilite->fShape->fVerb = shape->fVerb;
			}
		}
		UpdateBounds(shape);
		fParent->Dirty(nil);
		return true;
	}
	if (id == 0x4b)
	{
		RefVar hiliteRef(FirstHilite());
		if (NOTNIL(hiliteRef))
		{
			long pen = CommandParameter(cmd);
			long oldPen = GetPenSize();
			Boolean whole = IsCompletelyHilited(hiliteRef);
			TView* target = this;
			if (!whole)
			{
				Point none;
				none.h = none.v = 0;
				target = DiceHilited(hiliteRef, (TEditView*) fParent, none, false);
			}
			if (target != nil)
			{
				((TPolygonView*) target)->SetPenSize(pen);
				target->Dirty(nil);
				if (whole)
				{
					RefVar undo(MakeCommand(0x4b, this, 0x8000000));
					CommandSetParameter(undo, oldPen);
					gApplication->PostUndoCommand(undo);
				}
			}
		}
		return true;
	}
	if (id == aeRecognizeInk)
	{
		if (ISNIL(GetProto(RSSYMink)))
			return false;
		UChar failed = false;
		ControllerState* state = SaveRecognitionState(gController, &failed);
		if (!failed)
		{
			BringToFront();
			if (NOTNIL(GetFrameSlotRef(cmd, RSSYMdohilite)))
			{
				Rect bounds = viewBounds;
				DrawCheckmark(bounds);
			}
			ClearFlags(0x1000);
			RefVar config(GetFrameSlotRef(cmd, RSSYMrecconfig));
			TRecArea* area = MakeRerecognizeArea(gController, config);
			RerecognizeWord(this, area);
			if (area != nil)
				area->Dispose();
		}
		RestoreRecognitionState(gController, state);
		CommandSetResult(cmd, 1);
		return true;
	}
	return TView::RealDoCommand(cmd);
}
