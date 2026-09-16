/*
	File:		qd/Shapes.cpp

	Contains:	Lines, ovals, round rectangles and arcs.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
	The ROM's DrawArc (0x00285f50) rasterises the shape straight into the
	port's bits row by row (the oval's ends from BumpOval, the pen's inner
	oval for a frame, the angles' slopes for an arc, the clip regions'
	masks); the host builds the shape as a region from the same OvalRec
	rows (PutOval, the ROM's own point writer for an open region) and
	draws it through DrawRgn - the same pixels, one pass more (DEVIATION:
	the code).  Lines are drawn as the pen swept along the line's pixels
	(the ROM's DrawLine 0x002d277c does the same a row at a time).
*/

#include "Shapes.h"
#include "Polygons.h"
#include "CompMath.h"
#include "FixedMath.h"
#include "OSErrors.h"


/*------------------------------------------------------------------------------
	T h e   o v a l   r a s t e r i s e r
------------------------------------------------------------------------------*/

// ROM 0x002fb7c8 InitOval__FP4RectP7OvalReclT3
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


// ROM 0x002fb698 BumpOval__FP7OvalRecl
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


// ROM 0x002fb3e8 PutOval__FP4RectlT2PPcPlT5
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

// ROM 0x00285f50 DrawArc__FP4RectUclN23PP8PixelMapN23
// The shape - an oval of the corners' size within the rectangle, or, from
// startAngle through arcAngle, the wedge of it - drawn under the mode and
// pattern: the whole of it, or, framed, without the same shape inset by
// the pen (the pen hidden: nothing).  NOT YET RECONSTRUCTED: arcs of less
// than a full turn (the ROM clips the rows by the angles' slopes, the
// centre and the ends' quadrants; nothing is drawn here).
void
DrawArc(const Rect* r, Boolean framed, long ovalWidth, long ovalHeight, long mode, PatternHandle pattern, long startAngle, long arcAngle)
{
	GrafPort* port = GetCurrentPort();
	if (port->pnVis < 0 || arcAngle == 0)
		return;
	if (arcAngle < 0)
	{
		startAngle += arcAngle;
		arcAngle = -arcAngle;
	}
	if (arcAngle < 360)
		return;
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


// ROM 0x002fb2bc StdOval
// The standard oval proc: the oval fills its rectangle; frame records it
// into an open region (PutOval).  NOT YET RECONSTRUCTED: recording into
// an open picture.
void
StdOval(GrafVerb verb, Rect* r)
{
	if (verb == frame && GetCurrentPort()->rgnSave != nil)
		PutOval(r, r->right - r->left, r->bottom - r->top, qdGlobals.fRgnHandle, &qdGlobals.fRgnOffset, &qdGlobals.fRgnSize);
	long mode;
	PatternHandle pattern;
	PushVerb(verb, &mode, &pattern);
	DrawArc(r, verb == frame, r->right - r->left, r->bottom - r->top, mode, pattern, 0, 360);
}


// ROM 0x002fb3a8 CallOval__FUcP4Rect
void
CallOval(GrafVerb verb, const Rect* r)
{
	GrafPort* port = GetCurrentPort();
	OvalProcPtr proc = (port->grafProcs != nil && port->grafProcs->ovalProc != nil) ? port->grafProcs->ovalProc : StdOval;
	proc(verb, (Rect*) r);
}


// ROM 0x002fb254 FrameOval__FP4Rect
void	FrameOval(const Rect* r)		{ CallOval(frame, r); }
// ROM 0x002fb260 PaintOval__FP4Rect
void	PaintOval(const Rect* r)		{ CallOval(paint, r); }
// ROM 0x002fb26c EraseOval__FP4Rect
void	EraseOval(const Rect* r)		{ CallOval(erase, r); }
// ROM 0x002fb278 InvertOval__FP4Rect
void	InvertOval(const Rect* r)		{ CallOval(invert, r); }


// ROM 0x002fb284 FillOval__FP4RectPP8PixelMap
void
FillOval(const Rect* r, PatternHandle pattern)
{
	GrafPort* port = GetCurrentPort();
	PatternHandle saved = port->fgPat;
	port->fgPat = pattern;
	CallOval(fill, r);
	port->fgPat = saved;
}


// ROM 0x00318eac StdRRect
// The standard round-rectangle proc; frame records the shape into an
// open region.  NOT YET RECONSTRUCTED: recording into an open picture.
void
StdRRect(GrafVerb verb, Rect* r, long ovalWidth, long ovalHeight)
{
	if (verb == frame && GetCurrentPort()->rgnSave != nil)
		PutOval(r, ovalWidth, ovalHeight, qdGlobals.fRgnHandle, &qdGlobals.fRgnOffset, &qdGlobals.fRgnSize);
	long mode;
	PatternHandle pattern;
	PushVerb(verb, &mode, &pattern);
	DrawArc(r, verb == frame, ovalWidth, ovalHeight, mode, pattern, 0, 360);
}


// ROM 0x00318fcc CallRRect__FUcP4RectlT3
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


// ROM 0x00318e14 FrameRoundRect__FP4RectlT2
void	FrameRoundRect(const Rect* r, long ovalWidth, long ovalHeight)		{ CallRRect(frame, r, ovalWidth, ovalHeight); }
// ROM 0x00318e28 PaintRoundRect__FP4RectlT2
void	PaintRoundRect(const Rect* r, long ovalWidth, long ovalHeight)		{ CallRRect(paint, r, ovalWidth, ovalHeight); }
// ROM 0x00318e3c EraseRoundRect__FP4RectlT2
void	EraseRoundRect(const Rect* r, long ovalWidth, long ovalHeight)		{ CallRRect(erase, r, ovalWidth, ovalHeight); }
// ROM 0x00318e50 InvertRoundRect__FP4RectlT2
void	InvertRoundRect(const Rect* r, long ovalWidth, long ovalHeight)		{ CallRRect(invert, r, ovalWidth, ovalHeight); }


// ROM 0x00318e64 FillRoundRect__FP4RectlT2PP8PixelMap
void
FillRoundRect(const Rect* r, long ovalWidth, long ovalHeight, PatternHandle pattern)
{
	GrafPort* port = GetCurrentPort();
	PatternHandle saved = port->fgPat;
	port->fgPat = pattern;
	CallRRect(fill, r, ovalWidth, ovalHeight);
	port->fgPat = saved;
}


// ROM 0x00285df8 StdArc
// The standard arc proc: the wedge of the oval that fills the rectangle.
// NOT YET RECONSTRUCTED: recording into an open picture or region.
void
StdArc(GrafVerb verb, Rect* r, long startAngle, long arcAngle)
{
	long mode;
	PatternHandle pattern;
	PushVerb(verb, &mode, &pattern);
	DrawArc(r, verb == frame, r->right - r->left, r->bottom - r->top, mode, pattern, startAngle, arcAngle);
}


// ROM 0x00285f00 CallArc__FUcP4RectlT3
void
CallArc(GrafVerb verb, const Rect* r, long startAngle, long arcAngle)
{
	GrafPort* port = GetCurrentPort();
	ArcProcPtr proc = (port->grafProcs != nil && port->grafProcs->arcProc != nil) ? port->grafProcs->arcProc : StdArc;
	proc(verb, (Rect*) r, startAngle, arcAngle);
}


// ROM 0x00285d60 FrameArc__FP4RectlT2
void	FrameArc(const Rect* r, long startAngle, long arcAngle)		{ CallArc(frame, r, startAngle, arcAngle); }
// ROM 0x00285d74 PaintArc__FP4RectlT2
void	PaintArc(const Rect* r, long startAngle, long arcAngle)		{ CallArc(paint, r, startAngle, arcAngle); }
// ROM 0x00285d88 EraseArc__FP4RectlT2
void	EraseArc(const Rect* r, long startAngle, long arcAngle)		{ CallArc(erase, r, startAngle, arcAngle); }
// ROM 0x00285d9c InvertArc__FP4RectlT2
void	InvertArc(const Rect* r, long startAngle, long arcAngle)	{ CallArc(invert, r, startAngle, arcAngle); }


// ROM 0x00285db0 FillArc__FP4RectlT2PP8PixelMap
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

// ROM 0x002d277c DrawLine__F5PointT1
// The line from one point to the other, the pen (its size, mode and
// pattern) stamped at every pixel along it - hanging below and to the
// right of the point - clipped by the port's regions; nothing while the
// pen is hidden.  The ROM draws it a row at a time straight into the bits
// (FastLine for a one-pixel black or white pen) from a fixed-point slope;
// this Bresenham walk may place the odd diagonal pixel differently
// (DEVIATION).
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
	Rect pen;
	for (long i = 0; i <= steps; i++)
	{
		SetRect(&pen, x, y, x + port->pnSize.h, y + port->pnSize.v);
		RgnBlt(&port->portBits, &port->portBits, &pen, &pen, port->pnMode, port->fgPat, port->visRgn, port->clipRgn, wideHandle);
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


// ROM 0x002d1eac StdLine
// The standard line proc: the line from the pen's location to the point,
// which becomes the pen's location - recorded into an open polygon or
// region (DoLine).  NOT YET RECONSTRUCTED: recording into an open picture.
void
StdLine(Point to)
{
	DoLine(to);
}


// ROM 0x002d1e20 LineTo__FlT1
void
LineTo(long h, long v)
{
	GrafPort* port = GetCurrentPort();
	LineProcPtr proc = (port->grafProcs != nil && port->grafProcs->lineProc != nil) ? port->grafProcs->lineProc : StdLine;
	proc(MakePoint(h, v));
}


// ROM 0x002d1e7c Line__FlT1
void
Line(long dh, long dv)
{
	GrafPort* port = GetCurrentPort();
	Point to = MakePoint(port->pnLoc.h + dh, port->pnLoc.v + dv);
	LineProcPtr proc = (port->grafProcs != nil && port->grafProcs->lineProc != nil) ? port->grafProcs->lineProc : StdLine;
	proc(to);
}
