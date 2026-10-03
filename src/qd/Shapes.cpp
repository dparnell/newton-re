/*
	File:		qd/Shapes.cpp

	Contains:	Lines, ovals, round rectangles and arcs.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	The ROM's DrawArc (0x002aaaf8) rasterises the shape straight into the
	port's bits row by row (the oval's ends from BumpOval, the pen's inner
	oval for a frame, the angles' slopes for an arc, the clip regions'
	masks); the host builds the shape as a region from the same OvalRec
	rows (PutOval, the ROM's own point writer for an open region) and
	draws it through DrawRgn - the same pixels, one pass more (DEVIATION:
	the code).  Lines are drawn as the pen swept along the line's pixels
	(the ROM's DrawLine 0x002f7fc0 does the same a row at a time).
*/

#include "Shapes.h"
#include "PicRecord.h"
#include "Polygons.h"
#include "CompMath.h"
#include "FixedMath.h"
#include "OSErrors.h"
#include "Rects.h"
#include "Regions.h"
#include "Draw.h"
#include "Angles.h"
#include <string.h>


/*------------------------------------------------------------------------------
	T h e   o v a l   r a s t e r i s e r
------------------------------------------------------------------------------*/

// ROM 0x00320ac4 InitOval__FP4RectP7OvalReclT3
// The state for an oval of the corners' width and height (clamped to the
// rectangle) in the rectangle: the row's ends start at the flat top edge,
// half the oval's width in from each side; the accumulator's differences
// are the squared height-to-width ratio and its double.
void
InitOval(const Rect* r, OvalRec* oval, long ovalWidth, long ovalHeight)
{
	oval->fTop = r->top;
	oval->fBottom = r->bottom;
	if (ovalWidth < 0)
		ovalWidth = 0;
	if (ovalHeight < 0)
		ovalHeight = 0;
	long width = r->right - r->left;
	if (ovalWidth > width)
		ovalWidth = width;
	long height = r->bottom - r->top;
	if (ovalHeight > height)
		ovalHeight = height;
	Fixed halfWidth = (Fixed) (ovalWidth << 15);
	oval->fStep = 0x8000;
	oval->fRight = (Fixed) ((ULong32) r->right << 16) - halfWidth + 0x8000;
	oval->fLeft = (Fixed) ((ULong32) r->left << 16) + halfWidth;
	oval->fRow = 1 - ovalHeight;
	oval->fD = ovalHeight * 2 - 1;
	oval->fA.hi = 0;
	oval->fA.lo = 0;
	Fixed ratio = FixedDivide((Fixed) (ovalHeight << 16), (Fixed) (ovalWidth << 16));
	CompMul(ratio, ratio, &oval->fB);
	Int64 twice = oval->fB;
	CompAdd(&oval->fB, &twice);
	oval->fC = twice;
}


// ROM 0x00320994 BumpOval__FP7OvalRecl
// The ends moved to row y's: widened half a pixel at a time while the
// accumulator is below the decision term, narrowed while above.
void
BumpOval(OvalRec* oval, long y)
{
	if (!(oval->fTop <= y && y < oval->fBottom))
		return;
	long row = oval->fRow;
	oval->fRow = row + 2;
	long d = oval->fD;
	Int64 a = oval->fA;
	Int64 b = oval->fB;
	Int64 c = oval->fC;
	Fixed right = oval->fRight;
	Fixed step = oval->fStep;
	Fixed left = oval->fLeft;
	while ((long) (Long32) a.hi < d)
	{
		right += step;
		left -= step;
		CompAdd(&b, &a);
		CompAdd(&c, &b);
	}
	while (d < (long) (Long32) a.hi)
	{
		right -= step;
		left += step;
		CompSub(&c, &b);
		CompSub(&b, &a);
	}
	oval->fD = d - (row + 1) * 4;
	oval->fA = a;
	oval->fB = b;
	oval->fC = c;
	oval->fLeft = left;
	oval->fRight = right;
}


// a point appended to the buffer, cancelling the point before it when they
// are the same (a transition twice)
static inline short*
EmitPoint(short* out, const short* start, long y, long x)
{
	if (out > start && out[-2] == (short) y && out[-1] == (short) x)
		return out - 2;
	*out++ = (short) y;
	*out++ = (short) x;
	return out;
}


// ROM 0x003206e4 PutOval__FP4RectlT2PPcPlT5
// The shape's change points appended to the point buffer (grown as
// needed): the top row's ends, then, for each row of the curved parts
// (the top and bottom halves of the corner oval), the new and old end
// wherever an end moves, and the bottom row's ends.
void
PutOval(const Rect* r, long ovalWidth, long ovalHeight, Handle points, long* offset, long* limit)
{
	OvalRec oval;
	InitOval(r, &oval, ovalWidth, ovalHeight);
	long left = oval.fLeft >> 16;
	long right = oval.fRight >> 16;
	long curveEnd = oval.fTop + (ovalHeight >> 1);
	long straightEnd = curveEnd + (r->bottom - r->top) - ovalHeight;
	long outAt = *offset;
	long outLimit = *limit;
	if (outLimit <= outAt + 32)
	{
		outLimit += 0x200;
		if (SetHandleSize(points, outLimit) != noErr)
			return;
	}
	short* start = (short*) *points;
	short* out = (short*) (*points + outAt);
	out[0] = (short) oval.fTop;
	out[1] = (short) left;
	if (right != left)
	{
		out[2] = (short) oval.fTop;
		out[3] = (short) right;
		out += 4;
	}
	for (long y = oval.fTop; y < oval.fBottom; y++)
	{
		if (y < curveEnd || y >= straightEnd)
		{
			if (outLimit <= ((char*) out - (char*) start) + 24)
			{
				long at = (char*) out - (char*) start;
				outLimit += 0x200;
				if (SetHandleSize(points, outLimit) != noErr)
					return;
				start = (short*) *points;
				out = (short*) (*points + at);
			}
			BumpOval(&oval, y);
			long newLeft = oval.fLeft >> 16;
			if (newLeft != left)
			{
				out = EmitPoint(out, start, y, newLeft);
				out = EmitPoint(out, start, y, left);
				left = newLeft;
			}
			long newRight = oval.fRight >> 16;
			if (newRight != right)
			{
				out = EmitPoint(out, start, y, newRight);
				out = EmitPoint(out, start, y, right);
				right = newRight;
			}
		}
	}
	out = EmitPoint(out, start, oval.fBottom, left);
	out = EmitPoint(out, start, oval.fBottom, right);
	*offset = (char*) out - (char*) start;
	*limit = outLimit;
}


// Host: the shape as a region: its points sorted, culled and packed.
RgnHandle
OvalRgn(const Rect* r, long ovalWidth, long ovalHeight)
{
	RgnHandle rgn = NewRgn();
	if (rgn == nil)
		return nil;
	if (EmptyRect(r))
		return rgn;
	long limit = 0x200;
	Handle points = NewHandle(limit);
	if (points == nil)
		return rgn;
	long offset = 0;
	PutOval(r, ovalWidth, ovalHeight, points, &offset, &limit);
	long count = offset / 4;
	SortPoints((Point*) *points, count);
	CullPoints((Point*) *points, &count);
	PackRgn(points, count, rgn);
	DisposHandle(points);
	return rgn;
}


/*------------------------------------------------------------------------------
	A r c s ,   o v a l s ,   r o u n d   r e c t a n g l e s
------------------------------------------------------------------------------*/

// (host) One of DrawArc's slabs: row y from left up to right, cut to the
// clip rectangle as the ROM's DrawSlab (0x00347784) cuts it, drawn through
// the port's regions (the ROM masks the row with them itself).
static void
ArcSlab(long y, long left, long right, const Rect* clip, long mode, PatternHandle pattern)
{
	if (left < clip->left)
		left = clip->left;
	if (right > clip->right)
		right = clip->right;
	if (right <= left)
		return;
	Rect slab;
	SetRect(&slab, left, y, right, y + 1);
	DrawRect(&slab, mode, pattern);
}


// (host) An arc of less than a full turn, as the ROM's DrawArc draws it: a
// row at a time down the oval (and, framed, the oval inset by the pen),
// each row cut by the two lines from the centre at the start and end
// angles.  An angle is QuickDraw's - nought straight up, clockwise - and a
// line's slope is SlopeFromAngle's scaled by the rectangle's aspect; each
// line's x is followed down the rows from the top.  Which side of a line
// a row keeps depends on whether its ray points up (its "q" is negative:
// the angle is within 90 of the top); at the centre row the two lines
// change places (the lower half is walked the other way round), and an arc
// that lies wholly in one half has the other half left out.
static void
DrawPartArc(const Rect* r, Boolean framed, long ovalWidth, long ovalHeight, long mode, PatternHandle pattern, long startAngle, long arcAngle, const Rect* clip)
{
	GrafPort* port = GetCurrentPort();
	long start = startAngle % 360;
	if (start < 0)
		start += 360;
	long end = start + arcAngle;
	if (end > 359)
		end -= 360;
	long cy = (r->bottom + r->top) >> 1;
	long cx = (r->left + r->right) >> 1;
	long height = r->bottom - r->top;
	Fixed aspect = FixedDivide((Fixed) ((ULong32) (r->right - r->left) << 16), (Fixed) ((ULong32) height << 16));
	Fixed slopeStart = FixedMultiply(SlopeFromAngle(start), aspect);
	Fixed slopeEnd = FixedMultiply(SlopeFromAngle(end), aspect);
	Fixed xStart = (Fixed) (((ULong32) cx << 16) - (ULong32) (height >> 1) * (ULong32) slopeStart);
	Fixed xEnd = (Fixed) (((ULong32) cx << 16) - (ULong32) (height >> 1) * (ULong32) slopeEnd);
	long qStart = start < 180 ? start - 90 : 270 - start;
	long qEnd = end < 180 ? end - 90 : 270 - end;
	Boolean skip = false;
	if (arcAngle == 180)
	{
		if (start == 90)
			skip = true;							// the lower half only
	}
	else if (arcAngle < 180 && (qStart | qEnd) >= 0)
		skip = true;								// both rays point down

	OvalRec outer, inner;
	memset(&inner, 0, sizeof(inner));
	InitOval(r, &outer, ovalWidth, ovalHeight);
	long curveEnd = outer.fTop + (ovalHeight >> 1);
	long straightEnd = curveEnd + (r->bottom - r->top) - ovalHeight;
	inner.fTop = 0x7fff;
	if (framed)
	{
		long pnh = port->pnSize.h;
		Rect in;
		in.left = (short) (r->left + pnh);
		in.right = (short) (r->right - pnh);
		if (in.left < in.right)
		{
			long pnv = port->pnSize.v;
			in.top = (short) (r->top + pnv);
			in.bottom = (short) (r->bottom - pnv);
			if (in.top < in.bottom)
				InitOval(&in, &inner, ovalWidth - pnh * 2, ovalHeight - pnv * 2);
		}
	}
	long y = outer.fTop;
	do
	{
		if (y < curveEnd || y >= straightEnd)
		{
			BumpOval(&outer, y);
			BumpOval(&inner, y);
		}
		if (y == cy)
		{
			long q = -qEnd;
			skip = false;
			if (arcAngle == 180)
			{
				if (start == 270)
					break;							// the upper half only
			}
			else if (arcAngle < 180 && (-qStart | q) >= 0)
				break;								// both rays point up
			qEnd = -qStart;
			qStart = q;
			Fixed x = xEnd;
			xEnd = xStart;
			xStart = x;
			Fixed slope = slopeEnd;
			slopeEnd = slopeStart;
			slopeStart = slope;
		}
		if (y >= clip->top && !skip)
		{
			long outerLeft = outer.fLeft >> 16;
			long outerRight = outer.fRight >> 16;
			long a = outerLeft;
			if (qStart < 0 && a < (xStart >> 16))
				a = xStart >> 16;
			long b = outerRight;
			if (qEnd < 0 && b > (xEnd >> 16))
				b = xEnd >> 16;
			Boolean bothUp = (qStart & qEnd) < 0;
			if (y >= inner.fTop && inner.fBottom > y)
			{
				long innerLeft = inner.fLeft >> 16;
				long innerRight = inner.fRight >> 16;
				long c = innerLeft;
				if (qEnd < 0 && c > (xEnd >> 16))
					c = xEnd >> 16;
				long d = innerRight;
				if (qStart < 0 && d < (xStart >> 16))
					d = xStart >> 16;
				if (a < b)
				{
					ArcSlab(y, a, c, clip, mode, pattern);
					ArcSlab(y, d, b, clip, mode, pattern);
				}
				else if (bothUp && arcAngle > 180)
				{
					if (c == b)
						ArcSlab(y, a, innerLeft, clip, mode, pattern);
					else if (d == a)
						ArcSlab(y, innerRight, b, clip, mode, pattern);
					ArcSlab(y, outerLeft, c, clip, mode, pattern);
					ArcSlab(y, d, outerRight, clip, mode, pattern);
				}
			}
			else if (a < b)
				ArcSlab(y, a, b, clip, mode, pattern);
			else if (bothUp && arcAngle > 180)
			{
				ArcSlab(y, outerLeft, b, clip, mode, pattern);
				ArcSlab(y, a, outerRight, clip, mode, pattern);
			}
		}
		xStart = AddFixed(xStart, slopeStart);
		xEnd = AddFixed(xEnd, slopeEnd);
		y++;
	} while (y < clip->bottom);
}


// ROM 0x002aaaf8 DrawArc__FP4RectUclN23PP8PixelMapN23
// The shape - an oval of the corners' size within the rectangle, or, from
// startAngle through arcAngle, the wedge of it - drawn under the mode (a
// pattern mode; anything else draws nothing) and pattern: the whole of it,
// or, framed, without the same shape inset by the pen (the pen hidden:
// nothing).  The ROM draws a row at a time within the rectangle the clip
// and visible regions' boxes, the port and the shape have in common; a
// full turn is drawn here as a region (DEVIATION, the same pixels), less
// than one row by row as the ROM does (DrawPartArc).
void
DrawArc(const Rect* r, Boolean framed, long ovalWidth, long ovalHeight, long mode, PatternHandle pattern, long startAngle, long arcAngle)
{
	GrafPort* port = GetCurrentPort();
	if (port->pnVis < 0 || (mode & ~7) != 8)
		return;
	Rect clip;
	if (!RSect(&clip, 4, &(*port->clipRgn)->rgnBBox, &(*port->visRgn)->rgnBBox, &port->portBits.bounds, r))
		return;
	if (arcAngle == 0)
		return;
	if (arcAngle < 0)
	{
		startAngle += arcAngle;
		arcAngle = -arcAngle;
	}
	if (arcAngle < 360)
	{
		DrawPartArc(r, framed, ovalWidth, ovalHeight, mode, pattern, startAngle, arcAngle, &clip);
		return;
	}
	RgnHandle shape = OvalRgn(r, ovalWidth, ovalHeight);
	if (shape == nil)
		return;
	if (framed)
	{
		Rect inner = *r;
		InsetRect(&inner, port->pnSize.h, port->pnSize.v);
		if (inner.left < inner.right && inner.top < inner.bottom)
		{
			RgnHandle hole = OvalRgn(&inner, ovalWidth - 2 * port->pnSize.h, ovalHeight - 2 * port->pnSize.v);
			if (hole != nil)
			{
				DiffRgn(shape, hole, shape);
				DisposeRgn(hole);
			}
		}
	}
	DrawRgn(shape, mode, pattern);
	DisposeRgn(shape);
}


// ROM 0x003205b8 StdOval
// The standard oval proc: recorded into an open picture (0x50 + the
// verb); the oval fills its rectangle; frame records it into an open
// region (PutOval).
void
StdOval(GrafVerb verb, Rect* r)
{
	if (CheckPic())
	{
		PutPicVerb(verb);
		PutPicRect(0x50 + verb, r);
	}
	if (verb == frame && GetCurrentPort()->rgnSave != nil)
		PutOval(r, r->right - r->left, r->bottom - r->top, qdGlobals.fRgnHandle, &qdGlobals.fRgnOffset, &qdGlobals.fRgnSize);
	long mode;
	PatternHandle pattern;
	PushVerb(verb, &mode, &pattern);
	DrawArc(r, verb == frame, r->right - r->left, r->bottom - r->top, mode, pattern, 0, 360);
}


// ROM 0x003206a4 CallOval__FUcP4Rect
void
CallOval(GrafVerb verb, const Rect* r)
{
	GrafPort* port = GetCurrentPort();
	OvalProcPtr proc = (port->grafProcs != nil && port->grafProcs->ovalProc != nil) ? port->grafProcs->ovalProc : StdOval;
	proc(verb, (Rect*) r);
}


// ROM 0x00320550 FrameOval__FP4Rect
void	FrameOval(const Rect* r)		{ CallOval(frame, r); }
// ROM 0x0032055c PaintOval__FP4Rect
void	PaintOval(const Rect* r)		{ CallOval(paint, r); }
// ROM 0x00320568 EraseOval__FP4Rect
void	EraseOval(const Rect* r)		{ CallOval(erase, r); }
// ROM 0x00320574 InvertOval__FP4Rect
void	InvertOval(const Rect* r)		{ CallOval(invert, r); }


// ROM 0x00320580 FillOval__FP4RectPP8PixelMap
void
FillOval(const Rect* r, PatternHandle pattern)
{
	GrafPort* port = GetCurrentPort();
	PatternHandle saved = port->fgPat;
	port->fgPat = pattern;
	CallOval(fill, r);
	port->fgPat = saved;
}


// ROM 0x00344df4 StdRRect
// The standard round-rectangle proc: recorded into an open picture (the
// corners' size, OvSize 0x0b, when it has changed, then 0x40 + the verb);
// frame records the shape into an open region.
void
StdRRect(GrafVerb verb, Rect* r, long ovalWidth, long ovalHeight)
{
	GrafPort* port = GetCurrentPort();
	if (CheckPic())
	{
		PicSave* ps = (PicSave*) *port->picSave;
		PutPicVerb(verb);
		Point corners;
		SetPt(&corners, ovalWidth, ovalHeight);
		if (ps->fOvalSize.v != corners.v || ps->fOvalSize.h != corners.h)
		{
			PutPicOpcode(0x0b);
			PutPicPoint(corners);
			ps->fOvalSize = corners;
		}
		PutPicRect(0x40 + verb, r);
	}
	if (verb == frame && port->rgnSave != nil)
		PutOval(r, ovalWidth, ovalHeight, qdGlobals.fRgnHandle, &qdGlobals.fRgnOffset, &qdGlobals.fRgnSize);
	long mode;
	PatternHandle pattern;
	PushVerb(verb, &mode, &pattern);
	DrawArc(r, verb == frame, ovalWidth, ovalHeight, mode, pattern, 0, 360);
}


// ROM 0x00344f14 CallRRect__FUcP4RectlT3
// Square corners make it a rectangle.
void
CallRRect(GrafVerb verb, const Rect* r, long ovalWidth, long ovalHeight)
{
	GrafPort* port = GetCurrentPort();
	if (ovalWidth != 0 || ovalHeight != 0)
	{
		RRectProcPtr proc = (port->grafProcs != nil && port->grafProcs->rRectProc != nil) ? port->grafProcs->rRectProc : StdRRect;
		proc(verb, (Rect*) r, ovalWidth, ovalHeight);
		return;
	}
	CallRect(verb, r);
}


// ROM 0x00344d5c FrameRoundRect__FP4RectlT2
void	FrameRoundRect(const Rect* r, long ovalWidth, long ovalHeight)		{ CallRRect(frame, r, ovalWidth, ovalHeight); }
// ROM 0x00344d70 PaintRoundRect__FP4RectlT2
void	PaintRoundRect(const Rect* r, long ovalWidth, long ovalHeight)		{ CallRRect(paint, r, ovalWidth, ovalHeight); }
// ROM 0x00344d84 EraseRoundRect__FP4RectlT2
void	EraseRoundRect(const Rect* r, long ovalWidth, long ovalHeight)		{ CallRRect(erase, r, ovalWidth, ovalHeight); }
// ROM 0x00344d98 InvertRoundRect__FP4RectlT2
void	InvertRoundRect(const Rect* r, long ovalWidth, long ovalHeight)		{ CallRRect(invert, r, ovalWidth, ovalHeight); }


// ROM 0x00344dac FillRoundRect__FP4RectlT2PP8PixelMap
void
FillRoundRect(const Rect* r, long ovalWidth, long ovalHeight, PatternHandle pattern)
{
	GrafPort* port = GetCurrentPort();
	PatternHandle saved = port->fgPat;
	port->fgPat = pattern;
	CallRRect(fill, r, ovalWidth, ovalHeight);
	port->fgPat = saved;
}


// ROM 0x002aa9a0 StdArc
// The standard arc proc: recorded into an open picture (0x60 + the verb,
// the rectangle and the two angles); frame records the whole oval into an
// open region (PutOval, as the ROM does - not the arc); the wedge of the
// oval that fills the rectangle drawn.
void
StdArc(GrafVerb verb, Rect* r, long startAngle, long arcAngle)
{
	if (CheckPic())
	{
		PutPicVerb(verb);
		PutPicRect(0x60 + verb, r);
		PutPicWord(startAngle);
		PutPicWord(arcAngle);
	}
	if (verb == frame && GetCurrentPort()->rgnSave != nil)
		PutOval(r, r->right - r->left, r->bottom - r->top, qdGlobals.fRgnHandle, &qdGlobals.fRgnOffset, &qdGlobals.fRgnSize);
	long mode;
	PatternHandle pattern;
	PushVerb(verb, &mode, &pattern);
	DrawArc(r, verb == frame, r->right - r->left, r->bottom - r->top, mode, pattern, startAngle, arcAngle);
}


// ROM 0x002aaaa8 CallArc__FUcP4RectlT3
void
CallArc(GrafVerb verb, const Rect* r, long startAngle, long arcAngle)
{
	GrafPort* port = GetCurrentPort();
	ArcProcPtr proc = (port->grafProcs != nil && port->grafProcs->arcProc != nil) ? port->grafProcs->arcProc : StdArc;
	proc(verb, (Rect*) r, startAngle, arcAngle);
}


// ROM 0x002aa908 FrameArc__FP4RectlT2
void	FrameArc(const Rect* r, long startAngle, long arcAngle)		{ CallArc(frame, r, startAngle, arcAngle); }
// ROM 0x002aa91c PaintArc__FP4RectlT2
void	PaintArc(const Rect* r, long startAngle, long arcAngle)		{ CallArc(paint, r, startAngle, arcAngle); }
// ROM 0x002aa930 EraseArc__FP4RectlT2
void	EraseArc(const Rect* r, long startAngle, long arcAngle)		{ CallArc(erase, r, startAngle, arcAngle); }
// ROM 0x002aa944 InvertArc__FP4RectlT2
void	InvertArc(const Rect* r, long startAngle, long arcAngle)	{ CallArc(invert, r, startAngle, arcAngle); }


// ROM 0x002aa958 FillArc__FP4RectlT2PP8PixelMap
void
FillArc(const Rect* r, long startAngle, long arcAngle, PatternHandle pattern)
{
	GrafPort* port = GetCurrentPort();
	PatternHandle saved = port->fgPat;
	port->fgPat = pattern;
	CallArc(fill, r, startAngle, arcAngle);
	port->fgPat = saved;
}


/*------------------------------------------------------------------------------
	L i n e s
------------------------------------------------------------------------------*/

// ROM 0x002f7fc0 DrawLine__F5PointT1
// The line from one point to the other, the pen (its size, mode and
// pattern) stamped at every pixel along it - hanging below and to the
// right of the point - clipped by the port's regions; nothing while the
// pen is hidden.  The ROM draws it a row at a time straight into the bits
// (FastLine for a one-pixel black or white pen) from a fixed-point slope;
// this Bresenham walk may place the odd diagonal pixel differently
// (DEVIATION).
//
// Host: the pens stamped along one row (one column of a steep line) are
// drawn as the one rectangle they cover, as the ROM draws a row at once
// (DEVIATION, performance: the same pixels).  That holds wherever drawing a
// pixel twice is drawing it once - every mode but xor - and for xor when the
// pen is one pixel, whose stamps never overlap; xor with a bigger pen, and
// the slow blitter (the oracle, qd/tests/test_Blitter.cpp), stamp each pen.
void
DrawLine(Point from, Point to)
{
	GrafPort* port = GetCurrentPort();
	if (port->pnVis < 0)
		return;
	long dx = to.h - from.h;
	long dy = to.v - from.v;
	long stepX = dx < 0 ? -1 : 1;
	long stepY = dy < 0 ? -1 : 1;
	if (dx < 0)
		dx = -dx;
	if (dy < 0)
		dy = -dy;
	long x = from.h;
	long y = from.v;
	long error = (dx > dy ? dx : dy) / 2;
	long steps = dx > dy ? dx : dy;
	long penH = port->pnSize.h, penV = port->pnSize.v;
	Boolean runs = penH > 0 && penV > 0 && !QDSlowBlitter()
				&& ((port->pnMode & 3) != 2 || (penH == 1 && penV == 1));
	Rect pen;
	long runX = x, runY = y;						// (where the run being gathered began)
	for (long i = 0; i <= steps; i++)
	{
		if (!runs)
		{
			SetRect(&pen, x, y, x + penH, y + penV);
			RgnBlt(&port->portBits, &port->portBits, &pen, &pen, port->pnMode, port->fgPat, port->visRgn, port->clipRgn, wideHandle);
		}
		else
		{
			// the next pen leaves the row (column) or there is none: the run drawn
			long nextX = x, nextY = y;
			if (i < steps)
			{
				if (dx > dy)
				{
					nextX += stepX;
					if (error - dy < 0)
						nextY += stepY;
				}
				else
				{
					nextY += stepY;
					if (error - dx < 0)
						nextX += stepX;
				}
			}
			if (i == steps || (dx > dy ? nextY != y : nextX != x))
			{
				SetRect(&pen, runX < x ? runX : x, runY < y ? runY : y, (runX > x ? runX : x) + penH, (runY > y ? runY : y) + penV);
				RgnBlt(&port->portBits, &port->portBits, &pen, &pen, port->pnMode, port->fgPat, port->visRgn, port->clipRgn, wideHandle);
				runX = nextX;
				runY = nextY;
			}
		}
		if (dx > dy)
		{
			x += stepX;
			error -= dy;
			if (error < 0)
			{
				y += stepY;
				error += dx;
			}
		}
		else
		{
			y += stepY;
			error -= dx;
			if (error < 0)
			{
				x += stepX;
				error += dy;
			}
		}
	}
}


// ROM 0x002f76f0 StdLine
// The standard line proc: the line from the pen's location to the point,
// which becomes the pen's location - recorded into an open picture (Line
// 0x20, or LineFrom 0x21 when it starts where the picture's last line
// ended; + 2, ShortLine, when the step fits in a signed byte each way)
// and into an open polygon or region (DoLine).
void
StdLine(Point to)
{
	GrafPort* port = GetCurrentPort();
	if (CheckPic())
	{
		PicSave* ps = (PicSave*) *port->picSave;
		Point from = port->pnLoc;
		long dh = to.h - from.h;
		long dv = to.v - from.v;
		PutPicVerb(frame);
		long opcode = 0x20;
		if (from.v == ps->fPnLoc.v && from.h == ps->fPnLoc.h)
			opcode = 0x21;
		if (dh < 0x80 && dh >= -0x80 && dv < 0x80 && dv >= -0x80)
			opcode += 2;
		PutPicOpcode(opcode);
		if ((opcode & 1) == 0)
			PutPicPoint(from);
		if ((opcode & 2) == 0)
			PutPicPoint(to);
		else
		{
			PutPicByte(dh);
			PutPicByte(dv);
		}
		((PicSave*) *port->picSave)->fPnLoc = to;
	}
	DoLine(to);
}


// ROM 0x002f7664 LineTo__FlT1
void
LineTo(long h, long v)
{
	GrafPort* port = GetCurrentPort();
	LineProcPtr proc = (port->grafProcs != nil && port->grafProcs->lineProc != nil) ? port->grafProcs->lineProc : StdLine;
	proc(MakePoint(h, v));
}


// ROM 0x002f76c0 Line__FlT1
void
Line(long dh, long dv)
{
	GrafPort* port = GetCurrentPort();
	Point to = MakePoint(port->pnLoc.h + dh, port->pnLoc.v + dv);
	LineProcPtr proc = (port->grafProcs != nil && port->grafProcs->lineProc != nil) ? port->grafProcs->lineProc : StdLine;
	proc(to);
}
