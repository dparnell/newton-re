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


// ROM 0x0018ffbc RealDoCommand__12TPolygonViewFRC6RefVar
// Command 0x19 (Rerecognize.h): a shape with ink reads it again - brought
// to the front, the arrow drawn over it when asked, its recognition flag
// 0x1000 cleared, and its strokes read in an area made of the command's
// configuration, with the controller's state put aside meanwhile; a
// shape with no ink leaves the command untaken.  NOT YET RECONSTRUCTED:
// the shape's other commands (the points moved and scaled - 0x43, 0x44 -,
// the double tap's reading of its ink - 0x32 -, the pen size of the
// hilited shapes - 0x4b); they go to TView's as before.
Boolean
TPolygonView::RealDoCommand(RefArg cmd)
{
	long id = CommandID(cmd);
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
