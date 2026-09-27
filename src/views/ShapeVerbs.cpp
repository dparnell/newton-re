/*
	File:		views/ShapeVerbs.cpp

	Contains:	The shape functions a script asks questions of shapes with,
				and makes shapes out of other things with: FindShape (which
				shape of a list is nearest the pen, and which corner of it),
				GetShapeInfo (what a shape is, as a frame), MakeInk (an ink
				shape out of an ink binary), StrokeInPicture (whether a
				stroke ended on a picture's ink), AnimateSimpleStroke (a drawing
				played back as though written).

	Reconstructed from the MP2x00 US ROM (0x000dd160-0x000e2aa4,
	0x0003f544); each function cites its origin.
*/

#include "DrawShape.h"
#include "Ink.h"
#include "Rects.h"
#include "Regions.h"
#include "RegionVars.h"
#include "Polygons.h"
#include "Pictures.h"
#include "Stroke.h"
#include "UnitPublic.h"
#include "FixedGeometry.h"
#include "Frames.h"
#include "RSSymbols.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "ROMConstants.h"
#include "NativeFunctions.h"
#include "NewtonMemory.h"
#include "NewtonTime.h"
#include "Bits.h"
#include "Screen.h"
#include "Shapes.h"
#include "ByteOrder.h"


// ROM 0x000e15b8 PointInShape__FRC6RefVarRC6TPointP10TStyleSave
// Whether the point is inside the shape as it is drawn: the shape drawn
// into a region (from the origin, in the style in force) and the point
// tested against that.  (The ROM forces the QD scaler off round the
// drawing - TQDScaler::ForceScaling - so the shape records at its own
// size; the scaler is NOT YET, so there is nothing to force.)
Boolean
PointInShape(RefArg shape, const Point& pt, TStyleSave* style)
{
	TRegionVar rgn;
	OpenRgn();
	Point origin;
	origin.h = 0;
	origin.v = 0;
	DrawOneShape(shape, origin, style);
	CloseRgn(rgn);
	return PtInRgn(pt, rgn);
}


// ROM 0x000e1b60 DistanceFromRect__FRC5TRectC6TPoint
// How far the point is from the nearest of the rectangle's four sides
// (taken as whole lines: a point far along a side's line is near it).
static long
DistanceFromRect(const Rect& r, Point pt)
{
	long left = pt.h - r.left;
	if (left < 0)
		left = -left;
	long right = pt.h - r.right;
	if (right < 0)
		right = -right;
	if (left < right)
		right = left;
	long top = pt.v - r.top;
	if (top < 0)
		top = -top;
	long bottom = pt.v - r.bottom;
	if (bottom < 0)
		bottom = -bottom;
	if (top < bottom)
		bottom = top;
	return (right < bottom) ? right : bottom;
}


// The distance within which an oval or a wedge's outline is hit: nearer
// the middle than half the longer side, and further from it than half the
// shorter side less twice the tolerance.  (Written out twice in the ROM.)
static long
OutlineDistance(const Point& pt, const Point& mid, long width, long height, long tolerance)
{
	long longer = (width > height) ? width : height;
	long shorter = (width < height) ? width : height;
	long d = CheapDistance(pt, mid);
	if (d < (longer >> 1) && d > (shorter >> 1) - (tolerance << 1))
		return d;
	return -1;
}


// ROM 0x000e1be0 DoFindShape__FRC6RefVarRC6TPointR6RefVarP10TStyleSave
// Whether the point finds the shape - is on its outline, or inside it when
// it is filled - within the style's `selection` distance (six when there
// is none).  A list is walked, a style frame in it put in force for the
// shapes after it, and the index of each member found is added to `path`.
// A shape found nearer than the one `path` holds (its first slot is the
// distance) starts `path` again as [distance, corner]: the corner is
// which of the box's four (0 top left, 1 top right, 2 bottom right, 3
// bottom left) the point is in when a selection distance is given - a
// point there finds the shape whatever its outline says, being where a
// selected shape's resize handles are - and nil otherwise.
//
// ROM BUG: the distance a new find is compared with is the path's first
// slot as it stands - the Ref, four times the distance it holds - so a
// shape up to four times further away than the one found so far replaces
// it (and with nothing found yet the limit is 0x200, the Ref of 128).
//
// ROM BUG: a filled oval or wedge asks PointInShape and then takes the
// shape as found whatever it answers, so any point inside its box (grown
// by the tolerance) finds it.
Boolean
DoFindShape(RefArg shape, const Point& pt, RefVar& path, TStyleSave* style)
{
	long found = -1;
	RefVar cls(ClassOf(shape));
	if (ISNIL(shape))
		return false;
	if (EQRef(cls, RSSYMframe))
	{
		style->SetStyle(shape, pt, 2);
		return false;
	}
	if (IsArray(shape))
	{
		RefVar member;
		long count = Length(shape);
		for (long i = 0; i < count; i++)
		{
			member = GetArraySlotRef(shape, i);
			if (DoFindShape(member, pt, path, style))
			{
				found = 0;
				AddArraySlot(path, RefVar(MAKEINT(i)));
			}
		}
		return found >= 0;
	}

	Boolean fill = style->fFill;
	long tolerance = style->fSelection;
	if (tolerance == 0)
		tolerance = 6;
	Rect bounds;
	ShapeBounds(shape, &bounds);
	Rect near = bounds;
	InsetRect(&near, -tolerance, -tolerance);
	if (!PtInRect(pt, &near))
		return false;

	if (EQRef(cls, RSSYMrectangle) || EQRef(cls, RSSYMroundrectangle))
	{
		if (fill)
		{
			if (PtInRect(pt, &bounds))
				found = 0;
			else
				found = DistanceFromRect(bounds, pt);
		}
		else
		{
			// hollow: the band between the box grown and shrunk by the tolerance
			InsetRect(&near, tolerance << 1, tolerance << 1);
			if (!PtInRect(pt, &near))
				found = DistanceFromRect(bounds, pt);
		}
	}
	else if (EQRef(cls, RSSYMline))
	{
		Point ends[2];
		TBinaryDataPtr data(shape);
		BlockMove((char*) data, ends, sizeof(ends));
		long d = DistanceFromLine(pt, ends[0], ends[1]);
		if (d <= tolerance)
			found = d;
	}
	else if (EQRef(cls, RSSYMpolygon))
	{
		if (fill)
			found = PointInShape(shape, pt, style) ? 0 : -1;
		else
		{
			// the sides, each within the tolerance of its line and of the box of its two ends
			RefVar data(GetProtoVariable(shape, RSSYMdata, nil));
			TBinaryDataPtr bits(data);
			Handle fake = NewFakeHandle((char*) bits, Length(data));
			Polygon* poly = *(Polygon**) fake;
			const Point* side = poly->polyPoints;
			long sides = ((poly->polySize - 12) >> 2) - 1;
			for (long i = 0; i < sides; i++, side++)
			{
				long d = DistanceFromLine(pt, side[0], side[1]);
				if (d > tolerance)
					continue;
				long minH = (side[0].h < side[1].h) ? side[0].h : side[1].h;
				long maxH = (side[0].h > side[1].h) ? side[0].h : side[1].h;
				long minV = (side[0].v < side[1].v) ? side[0].v : side[1].v;
				long maxV = (side[0].v > side[1].v) ? side[0].v : side[1].v;
				if (pt.h < maxH + tolerance && pt.h > minH - tolerance
					&& pt.v < maxV + tolerance && pt.v > minV - tolerance)
				{
					found = d;
					break;
				}
			}
			// (ROM: the fake handle is not given back - a leak kept)
		}
	}
	else if (EQRef(cls, RSSYMoval))
	{
		if (fill)
		{
			PointInShape(shape, pt, style);			// (its answer ignored: see above)
			found = 0;
		}
		else
		{
			long width = (short) (near.right - near.left);
			long height = (short) (near.bottom - near.top);
			Point mid = MakePoint((short) (near.left + (width >> 1)), (short) (near.top + (height >> 1)));
			found = OutlineDistance(pt, mid, width, height, tolerance);
		}
	}
	else if (EQRef(cls, RSSYMwedge))
	{
		struct { Rect fRect; short fStart; short fArc; } wedge;
		{
			TBinaryDataPtr data(shape);
			BlockMove((char*) data, &wedge, sizeof(wedge));
		}
		// the quarter of the shape's box the wedge starts in, grown by the tolerance
		WedgeBox(&bounds, wedge.fStart, wedge.fArc);
		InsetRect(&bounds, -tolerance, -tolerance);
		if (PtInRect(pt, &bounds))
		{
			if (fill)
			{
				PointInShape(shape, pt, style);		// (its answer ignored: see above)
				found = 0;
			}
			else
			{
				Point mid = MidPoint(bounds);
				long width = (short) (bounds.right - bounds.left);
				long height = (short) (bounds.bottom - bounds.top);
				found = OutlineDistance(pt, mid, width, height, tolerance);
			}
		}
	}
	else if (EQRef(cls, RSSYMregion))
	{
		RefVar data(GetProtoVariable(shape, RSSYMdata, nil));
		TBinaryDataPtr bits(data);
		Handle fake = NewFakeHandle((char*) bits, Length(data));
		if (PtInRgn(pt, (RgnHandle) fake))
			found = 0;
		DisposHandle(fake);
	}
	else if (EQRef(cls, RSSYMink))
	{
		// any of the strokes' points within the tolerance and the pen's
		// width of the point, measured in the ink's own coordinates
		RefVar data(GetProtoVariable(shape, RSSYMdata, nil));
		RefVar inkBounds(GetFrameSlotRef(shape, RSSYMbounds));
		RefVar inkOriginal(GetFrameSlotRef(shape, RSSYMoriginalbounds));
		TBinaryDataPtr boundsData(inkBounds);
		TBinaryDataPtr originalData(inkOriginal);
		const Rect* to = (const Rect*) (char*) boundsData;
		const Rect* from = (const Rect*) (char*) originalData;
		TStroke** strokes = InkExpand(data, 0, from->left - 2, from->top - 2);
		PenState pen;
		GetPenState(&pen);
		long within = tolerance + pen.pnSize.h;
		Point at = pt;
		MapPt(&at, to, from);
		for (long s = 0; strokes[s] != nil; s++)
		{
			TStroke* stroke = strokes[s];
			stroke->UpdateBBox();
			Rect box;
			UnfixRect(&stroke->fBBox, &box);
			InsetRect(&box, -tolerance, -tolerance);
			if (PtInRect(at, &box))
			{
				for (long i = 0; i < stroke->fCount; i++)
				{
					FPoint fp;
					stroke->GetFPoint(i, &fp);
					long dh = (short) ((fp.x + 0x8000) >> 16) - at.h;
					if (dh < 0)
						dh = -dh;
					long dv = (short) ((fp.y + 0x8000) >> 16) - at.v;
					if (dv < 0)
						dv = -dv;
					long d = (dh >= dv) ? dh + (dv >> 1) : dv + (dh >> 1);
					if (d <= within)
					{
						found = d;
						break;
					}
				}
			}
			if (found >= 0)
				break;
		}
		// ROM BUG: the expanded strokes are never given back (no
		// DisposeTStrokes) - a leak kept
	}
	else if (EQRef(cls, RSSYMbitmap) || EQRef(cls, RSSYMtext) || EQRef(cls, RSSYMpicture))
	{
		if (PtInRect(pt, &bounds))
			found = 0;
		else
			found = DistanceFromRect(bounds, pt);
	}

	// a selected shape's corners: a point in one finds the shape
	long corner = -1;
	if (style->fSelection != 0)
	{
		InsetRect(&bounds, style->fSelection, style->fSelection);
		if (pt.v <= bounds.top)
		{
			if (pt.h <= bounds.left)
				corner = 0;
			else if (pt.h >= bounds.right)
				corner = 1;
		}
		else if (pt.v >= bounds.bottom)
		{
			if (pt.h >= bounds.right)
				corner = 2;
			else if (pt.h <= bounds.left)
				corner = 3;
		}
		if (corner >= 0)
			found = 0;
	}
	if (corner < 0 && found < 0)
		return false;

	Ref nearest = 0x200;
	if (Length(path) != 0)
		nearest = GetArraySlotRef(path, 0);
	if (found > (long) nearest)						// (the Ref, not its value: see above)
		return false;
	path = AllocateArray(RSSYMpathexpr, 0);
	AddArraySlot(path, RefVar(MAKEINT(found)));
	AddArraySlot(path, RefVar(corner < 0 ? NILREF : MAKEINT(corner)));
	return true;
}


// ROM 0x000e2808 FFindShape
// FindShape(shapes, x, y, style) - the shape nearest the point.  nil when
// none is near enough; true for a single shape that is; for a list a frame
// {vertex: the corner or nil, path: the indexes down to the shape,
// outermost first} (a list of one level gives a path of its one index).
static Ref
FFindShape(RefArg /*rcvr*/, RefArg shapes, RefArg x, RefArg y, RefArg style)
{
	PenState pen;
	GetPenState(&pen);
	Point pt;
	pt.h = (short) RINT(x);
	pt.v = (short) RINT(y);
	RefVar path(AllocateArray(RSSYMpathexpr, 0));
	TStyleSave styleSave;
	SaveLevel level;
	styleSave.BeginLevel(&level);
	styleSave.SetStyle(style, pt, 2);
	Boolean found = DoFindShape(shapes, pt, path, &styleSave);
	SetPenState(&pen);
	long length = Length(path);
	if (length == 0)
		return found ? TRUEREF : NILREF;
	// the path came out innermost first, the distance and the corner in front
	RefVar swap;
	for (long i = 0; i < length / 2; i++)
	{
		long j = length - 1 - i;
		swap = GetArraySlotRef(path, j);
		SetArraySlotRef(path, j, GetArraySlotRef(path, i));
		SetArraySlotRef(path, i, swap);
	}
	SetLength(path, length - 1);					// the distance
	RefVar result(AllocateFrame());
	SetFrameSlot(result, RSSYMvertex, RefVar(GetArraySlotRef(path, length - 2)));
	if (length - 1 == 1)
		SetFrameSlot(result, RSSYMpath, RefVar(TRUEREF));
	else
	{
		SetFrameSlot(result, RSSYMpath, path);
		SetLength(path, length - 2);				// the corner
	}
	return result;
}


// ROM 0x000de2d0 FGetShapeInfo
// GetShapeInfo(shape) - a frame describing the shape: a bitmap's is what
// GetBitmapInfo says; any other's is canonicalShapeInfo with the bounds
// (a frame with bits or colour data giving its own bounds slot, anything
// else what ShapeBounds says), plus a text's string, a line's two ends as
// {x, y} frames, and a wedge's box as bitsBounds.
static Ref
FGetShapeInfo(RefArg /*rcvr*/, RefArg shape)
{
	RefVar cls(ClassOf(shape));
	RefVar info;
	if (EQRef(cls, RSSYMbitmap))
		info = NSCallGlobalFn(RSSYMgetbitmapinfo, shape);
	else
	{
		RefVar bounds;
		info = Clone(RefVar(Rcanonicalshapeinfo));
		if (EQRef(cls, RSSYMframe) && (FrameHasSlot(shape, RSSYMbits) || FrameHasSlot(shape, RSSYMcolordata)))
			bounds = GetProtoVariable(shape, RSSYMbounds, nil);
		else
			bounds = NSCallGlobalFn(RSSYMshapebounds, shape);
		SetFrameSlot(info, RSSYMbounds, bounds);
	}
	if (EQRef(cls, RSSYMtext))
	{
		RefVar data(GetProtoVariable(shape, RSSYMdata, nil));
		cls = Clone(data);
		SetClass(cls, RSSYMstring);
		SetFrameSlot(info, RSSYMtext, cls);
	}
	else if (EQRef(cls, RSSYMline))
	{
		Point ends[2];
		{
			TBinaryDataPtr data(shape);
			BlockMove((char*) data, ends, sizeof(ends));
		}
		RefVar end(AllocateFrame());
		SetFrameSlot(end, RSSYMx, RefVar(MAKEINT(ends[0].h)));
		SetFrameSlot(end, RSSYMy, RefVar(MAKEINT(ends[0].v)));
		SetFrameSlot(info, RSSYMstart, end);
		end = Clone(end);
		SetFrameSlot(end, RSSYMx, RefVar(MAKEINT(ends[1].h)));
		SetFrameSlot(end, RSSYMy, RefVar(MAKEINT(ends[1].v)));
		SetFrameSlot(info, RSSYMstop, end);
	}
	else if (EQRef(cls, RSSYMwedge))
	{
		struct { Rect fRect; short fStart; short fArc; } wedge;
		{
			TBinaryDataPtr data(shape);
			BlockMove((char*) data, &wedge, sizeof(wedge));
		}
		Rect box;
		ShapeBounds(shape, &box);
		WedgeBox(&box, wedge.fStart, wedge.fArc);
		SetFrameSlot(info, RSSYMbitsbounds, RefVar(ToObject(box)));
	}
	return info;
}


// ROM 0x000dd160 FMakeInk
// MakeInk(ink, left, top, right, bottom) - an ink shape: canonicalInkShape
// with the ink as its data, drawn into the rectangle, which it was also
// made in (originalBounds a copy of bounds).
static Ref
FMakeInk(RefArg /*rcvr*/, RefArg ink, RefArg left, RefArg top, RefArg right, RefArg bottom)
{
	RefVar shape(Clone(RefVar(Rcanonicalinkshape)));
	RefVar bounds(MakeRectShape(RSSYMboundsrect, left, top, right, bottom));
	SetFrameSlot(shape, RSSYMbounds, bounds);
	SetFrameSlot(shape, RSSYMoriginalbounds, RefVar(Clone(bounds)));
	SetFrameSlot(shape, RSSYMdata, ink);
	return shape;
}


// ROM 0x0003f544 FStrokeInPicture__FRC6RefVarN21
// view:StrokeInPicture(unit, picture) - whether the stroke ended on the
// picture's ink: its last point, made relative to the view, asked of
// PtInPicture.  nil when the view is not open.
static Ref
FStrokeInPicture(RefArg rcvr, RefArg unit, RefArg picture)
{
	RefVar result;
	TView* view = GetView(rcvr);
	if (view != nil)
	{
		Point last = StrokeFromRef(unit)->FinalPoint();
		RefVar y(MAKEINT(last.v - view->viewBounds.top));
		RefVar x(MAKEINT(last.h - view->viewBounds.left));
		result = FPtInPicture(rcvr, x, y, picture);
	}
	return result;
}


// ROM 0x001f12ac FAnimateSimpleStroke__FRC6RefVarN31
// AnimateSimpleStroke(strokes, dest, withPen) - a drawing played back as
// though written: the binary is the rectangle it was drawn in (eight
// bytes), then strokes, each a count, a starting point and that many
// bytes of moves (the high nibble down, the low nibble across, each -8..7),
// padded to a word; every point is mapped from the rectangle into `dest`
// and a line drawn to it.  With a pen, each step draws the stylus picture
// (the third of gtPens) with its tip at the point, a tick at a time, and
// puts back what was under it.
//
// No ROM script calls it, so its bytes are read as the ROM reads them:
// big-endian, whatever the host.
//
// ROM QUIRK: the stylus picture's bounds are offset by their own top left
// - doubled, not taken back to the origin; a picture whose bounds start at
// 0, 0 (as the ROM's does) is not moved.
static Ref
FAnimateSimpleStroke(RefArg /*rcvr*/, RefArg strokes, RefArg dest, RefArg withPen)
{
	TBinaryDataPtr data(strokes);
	RefVar pen(GetArraySlotRef(RefVar(Rgtpens), 2));
	RefVar penBounds(GetFrameSlotRef(pen, RSSYMbounds));
	Rect penBox;
	if (!FromObject(penBounds, penBox))
		ThrowMsg((char*) "bad pict frame");
	OffsetRect(&penBox, penBox.left, penBox.top);
	const UByte* p = (const UByte*) (char*) data;
	const UByte* end = p + Length(strokes);
	Rect from;
	from.top = (short) GetBigEndianHalf(p);
	from.left = (short) GetBigEndianHalf(p + 2);
	from.bottom = (short) GetBigEndianHalf(p + 4);
	from.right = (short) GetBigEndianHalf(p + 6);
	p += 8;
	TBits under;
	Rect screen;
	SetRect(&screen, 0, 0, (short) screenWidth, (short) screenHeight);
	under.Constructor(screen);
	Rect to;
	if (!FromObject(dest, to))
		ThrowMsg((char*) "bad stroke dest");
	while (p < end)
	{
		long count = (long) GetBigEndianWord(p);
		p += 4;
		long pad = (4 - count) & 3;
		Point at;
		at.v = (short) GetBigEndianHalf(p);
		at.h = (short) GetBigEndianHalf(p + 2);
		p += 4;
		Point mapped = at;
		MapPt(&mapped, &from, &to);
		MoveTo(mapped.h, mapped.v);
		while (count-- != 0)
		{
			long dv = *p >> 4;
			if (dv > 7)
				dv -= 16;
			long dh = *p & 0x0f;
			if (dh > 7)
				dh -= 16;
			p++;
			at.v = (short) (at.v + dv);
			at.h = (short) (at.h + dh);
			mapped = at;
			MapPt(&mapped, &from, &to);
			if (ISNIL(withPen))
				LineTo(mapped.h, mapped.v);
			else
			{
				StartDrawing(nil, nil);
				LineTo(mapped.h, mapped.v);
				Rect box = penBox;
				OffsetRect(&box, mapped.h, mapped.v - (short) (penBox.bottom - penBox.top));
				ULong next = Ticks() + 1;
				under.CopyFromScreen(box, box, 0, nil);
				DrawPicture(pen, box, 0, 8);
				StopDrawing(nil, nil);
				SleepTillTicks(next);
				under.Draw(box, box, 0, nil);
			}
		}
		p += pad;
	}
	return NILREF;
}



#pragma mark - turning and flipping

// ROM 0x000de6d8 RotatePointR__FP5PointsT2
// A quarter turn to the right about (cx, cy).
void
RotatePointR(Point* pt, short cx, short cy)
{
	short h = pt->h;
	short v = pt->v;
	pt->v = (short) ((h - cx) + cy);
	pt->h = (short) ((cy - v) + cx);
}


// ROM 0x000de72c RotatePointL__FP5PointsT2
void
RotatePointL(Point* pt, short cx, short cy)
{
	short h = pt->h;
	short v = pt->v;
	pt->v = (short) ((cx - h) + cy);
	pt->h = (short) ((v - cy) + cx);
}


// ROM 0x000de780 FlipHPoint__FP5PointsT2
void
FlipHPoint(Point* pt, short cx, short /*cy*/)
{
	pt->h = (short) (cx * 2 - pt->h);
}


// ROM 0x000de7a4 FlipVPoint__FP5PointsT2
void
FlipVPoint(Point* pt, short /*cx*/, short cy)
{
	pt->v = (short) (cy * 2 - pt->v);
}


// ROM 0x000de7c8 RotateRectR__FP4RectsT2
void
RotateRectR(Rect* r, short cx, short cy)
{
	short top = r->top, left = r->left, bottom = r->bottom, right = r->right;
	r->top = (short) ((left - cx) + cy);
	r->bottom = (short) ((right - cx) + cy);
	r->left = (short) ((cy - bottom) + cx);
	r->right = (short) ((cy - top) + cx);
}


// ROM 0x000de98c RotateRectL__FP4RectsT2
void
RotateRectL(Rect* r, short cx, short cy)
{
	short top = r->top, left = r->left, bottom = r->bottom, right = r->right;
	r->top = (short) ((cx - right) + cy);
	r->bottom = (short) ((cx - left) + cy);
	r->left = (short) ((top - cy) + cx);
	r->right = (short) ((bottom - cy) + cx);
}


// ROM 0x000dea24 FlipRectV__FP4RectsT2
void
FlipRectV(Rect* r, short /*cx*/, short cy)
{
	short top = r->top;
	r->top = (short) (cy * 2 - r->bottom);
	r->bottom = (short) (cy * 2 - top);
}


// ROM 0x000dea60 FlipRectH__FP4RectsT2
void
FlipRectH(Rect* r, short cx, short /*cy*/)
{
	short left = r->left;
	r->left = (short) (cx * 2 - r->right);
	r->right = (short) (cx * 2 - left);
}


// ROM 0x000dea9c DoMungeShape__FRC6RefVarN21sT4
// A shape turned ('rotateLeft, 'rotateRight) or flipped ('flipHorizontal,
// 'flipVertical) about (cx, cy), in place where it can be: a list member
// by member (a style frame in it applying to the shapes after it); a
// line's points, a rectangle's (oval's, ...) box, a polygon's points and
// box moved; an ink shape's strokes each turned or scaled and packed
// again, its boxes turned (and kept from going negative); and a bitmap
// turned with MungeBitmap - a region, picture or text first drawn into
// a bitmap of its own, which is what comes back.  ROM QUIRKS: the shape
// drawn into that bitmap is left moved to the origin; the ink's stroke
// list is not given back.  ==> the shape (or the bitmap made).
Ref
DoMungeShape(RefArg shape, RefArg operation, RefArg style, short cx, short cy)
{
	typedef void (*PointProc)(Point*, short, short);
	typedef void (*RectProc)(Rect*, short, short);
	RefVar cls(ClassOf(shape));
	RefVar result;
	PointProc pointProc = nil;
	RectProc rectProc = nil;
	if (EQ(operation, RSSYMrotateright))
	{
		pointProc = RotatePointR;
		rectProc = RotateRectR;
	}
	else if (EQ(operation, RSSYMrotateleft))
	{
		pointProc = RotatePointL;
		rectProc = RotateRectL;
	}
	else if (EQ(operation, RSSYMfliphorizontal))
	{
		pointProc = FlipHPoint;
		rectProc = FlipRectH;
	}
	else if (EQ(operation, RSSYMflipvertical))
	{
		pointProc = FlipVPoint;
		rectProc = FlipRectV;
	}
	else
		Throw((ExceptionName) "evt.ex.graf", (void*) -8802, nil);

	if (IsArray(shape))
	{
		long i = 0;
		RefVar currentStyle(style);
		for (TObjectIterator iter(shape); !iter.Done(); iter.Next(), i++)
		{
			RefVar item(iter.Value());
			if (ISNIL(item))
				continue;
			if (!EQ(RefVar(ClassOf(item)), RSSYMframe))
			{
				RefVar munged(DoMungeShape(item, operation, currentStyle, cx, cy));
				SetArraySlot(shape, i, munged);
			}
			else
			{
				currentStyle = item;
				SetArraySlot(shape, i, item);
			}
		}
		return shape;
	}

	Boolean isRegion = EQ(cls, RSSYMregion);
	Boolean isBitmap = false;
	if (!isRegion)
	{
		isBitmap = EQ(cls, RSSYMbitmap);
		if (!isBitmap && !EQ(cls, RSSYMpicture) && !EQ(cls, RSSYMtext))
		{
			if (EQ(cls, RSSYMink))
			{
				// ink: the strokes turned or scaled, and packed again
				RefVar boundsRef(GetProtoVariable(shape, RSSYMbounds, nil));
				RefVar originalRef(GetProtoVariable(shape, RSSYMoriginalbounds, nil));
				Rect* boundsData = (Rect*) BinaryData(boundsRef);
				Rect* originalData = (Rect*) BinaryData(originalRef);
				Rect bounds = *boundsData;
				Rect original = *originalData;
				rectProc(&bounds, cx, cy);
				rectProc(&original, cx, cy);
				long dx = bounds.left < 0 ? -bounds.left : 0;
				long dy = bounds.top < 0 ? -bounds.top : 0;
				if (dx != 0 || dy != 0)
					OffsetRect(&bounds, dx, dy);
				dx = original.left < 0 ? -original.left : 0;
				dy = original.top < 0 ? -original.top : 0;
				if (dx != 0 || dy != 0)
					OffsetRect(&original, dx, dy);
				RefVar data(GetProtoVariable(shape, RSSYMdata, nil));
				TStroke** strokes = InkExpand(data, 0, boundsData->left - 2, boundsData->top - 2);
				Rect box;
				for (long k = 0; strokes[k] != nil; k++)
				{
					TStroke* stroke = strokes[k];
					stroke->UpdateBBox();
					UnfixRect(&stroke->fBBox, &box);
					rectProc(&box, cx, cy);
					dx = box.left < 0 ? -box.left : 0;
					dy = box.top < 0 ? -box.top : 0;
					if (dx != 0 || dy != 0)
					{
						stroke->Offset(dx << 16, dy << 16);
						stroke->UpdateBBox();
					}
					if (EQ(operation, RSSYMrotateleft))
						stroke->Rotate(0x5a0000);
					else if (EQ(operation, RSSYMrotateright))
						stroke->Rotate((long) (int32_t) 0xffa60000);
					else if (EQ(operation, RSSYMflipvertical))
						stroke->Scale(0x10000, (long) (int32_t) 0xffff0000);
					else
						stroke->Scale(-0x10000, 0x10000);
					stroke->UpdateBBox();
				}
				RefVar ink(TStrokesToInk(strokes, &box));
				SetFrameSlot(shape, RSSYMdata, ink);
				*(Rect*) BinaryData(boundsRef) = bounds;
				*(Rect*) BinaryData(originalRef) = original;
			}
			else if (EQ(cls, RSSYMpolygon))
			{
				RefVar data(GetProtoVariable(shape, RSSYMdata, nil));
				Polygon* poly = (Polygon*) BinaryData(data);
				long n = ((ULong) (poly->polySize - 0xc)) >> 2;
				for (long k = 0; k < n; k++)
					pointProc(&poly->polyPoints[k], cx, cy);
				rectProc(&poly->polyBBox, cx, cy);
			}
			else if (EQ(cls, RSSYMline))
			{
				Point* ends = (Point*) BinaryData(shape);
				pointProc(&ends[0], cx, cy);
				pointProc(&ends[1], cx, cy);
			}
			else
				rectProc((Rect*) BinaryData(shape), cx, cy);
			return shape;
		}
	}

	// a region, bitmap, picture or text: turned as a bitmap
	Rect* where;
	RefVar boundsRef;
	if (!isRegion)
	{
		boundsRef = GetProtoVariable(shape, RSSYMbounds, nil);
		where = (Rect*) BinaryData(boundsRef);
	}
	else
		where = &((Region*) BinaryData(shape))->rgnBBox;
	Rect box = *where;
	rectProc(&box, cx, cy);
	long top = where->top, left = where->left, bottom = where->bottom, right = where->right;
	if (!isBitmap)
	{
		result = FMakeBitmap(RefVar(NILREF), RefVar(MAKEINT((short) (right - left))), RefVar(MAKEINT((short) (bottom - top))), RefVar(NILREF));
		FOffsetShape(RefVar(NILREF), shape, RefVar(MAKEINT(-left)), RefVar(MAKEINT(-top)));
		FDrawIntoBitmap(RefVar(NILREF), shape, style, result);
		FOffsetShape(RefVar(NILREF), result, RefVar(MAKEINT(left)), RefVar(MAKEINT(top)));
	}
	else
		result = shape;
	FMungeBitmap(RefVar(NILREF), result, operation, RefVar(NILREF));
	Rect turned = *(Rect*) BinaryData(RefVar(GetFrameSlot(result, RSSYMbounds)));
	FOffsetShape(RefVar(NILREF), result, RefVar(MAKEINT(box.left - turned.left)), RefVar(MAKEINT(box.top - turned.top)));
	return result;
}


// ROM 0x000df718 FMungeShape
// MungeShape(shape, operation, style): the shape turned or flipped about
// the middle of its box.
Ref
FMungeShape(RefArg /*rcvr*/, RefArg shape, RefArg operation, RefArg style)
{
	Rect bounds;
	ShapeBounds(shape, &bounds);
	short cx = (short) (bounds.left + (bounds.right - bounds.left) / 2);
	short cy = (short) (bounds.top + (bounds.bottom - bounds.top) / 2);
	return DoMungeShape(shape, operation, style, cx, cy);
}


void
RegisterShapeVerbNatives(void)
{
	RegisterNativeFunction("FFindShape", (void*) FFindShape, 4);
	RegisterNativeFunction("FMungeShape", (void*) FMungeShape, 3);
	RegisterNativeFunction("FGetShapeInfo", (void*) FGetShapeInfo, 1);
	RegisterNativeFunction("FMakeInk", (void*) FMakeInk, 5);
	RegisterNativeFunction("FStrokeInPicture__FRC6RefVarN21", (void*) FStrokeInPicture, 2);
	RegisterNativeFunction("FAnimateSimpleStroke__FRC6RefVarN31", (void*) FAnimateSimpleStroke, 3);
}
