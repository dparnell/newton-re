/*
	File:		qd/Rects.cpp

	Contains:	QuickDraw's rectangles and points.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Rects.h"
#include "NewtonExceptions.h"
#include <stdarg.h>


// ROM 0x0033525c SetPt__FP5PointlT2
void
SetPt(Point* pt, long h, long v)
{
	pt->h = (short) h;
	pt->v = (short) v;
}


// ROM 0x0033ffb0 SetRect__FP4RectlN32
void
SetRect(Rect* r, long left, long top, long right, long bottom)
{
	r->left = (short) left;
	r->top = (short) top;
	r->right = (short) right;
	r->bottom = (short) bottom;
}


// ROM 0x00340fc4 SetEmptyRect__FP4Rect
void
SetEmptyRect(Rect* r)
{
	r->left = 0;
	r->top = 0;
	r->right = 0;
	r->bottom = 0;
}


// ROM 0x0033ffe8 OffsetRect__FP4RectlT2
void
OffsetRect(Rect* r, long dh, long dv)
{
	r->top = (short) (r->top + (short) dv);
	r->left = (short) (r->left + (short) dh);
	r->bottom = (short) (r->bottom + (short) dv);
	r->right = (short) (r->right + (short) dh);
}


// ROM 0x00340718 InsetRect__FP4RectlT2
void
InsetRect(Rect* r, long dh, long dv)
{
	r->top = (short) (r->top + (short) dv);
	r->left = (short) (r->left + (short) dh);
	r->bottom = (short) (r->bottom - (short) dv);
	r->right = (short) (r->right - (short) dh);
}


// ROM 0x00340154 Pt2Rect__F5PointT1P4Rect
// The rectangle with the two points as opposite corners.
void
Pt2Rect(Point a, Point b, Rect* r)
{
	if (a.h < b.h)
	{
		r->left = a.h;
		r->right = b.h;
	}
	else
	{
		r->left = b.h;
		r->right = a.h;
	}
	if (a.v < b.v)
	{
		r->top = a.v;
		r->bottom = b.v;
	}
	else
	{
		r->top = b.v;
		r->bottom = a.v;
	}
}


// ROM 0x003401fc PtInRect__F5PointP4Rect
// The pixel below and to the right of the point is in the rectangle.
Boolean
PtInRect(Point pt, const Rect* r)
{
	return r->top <= pt.v && pt.v < r->bottom && r->left <= pt.h && pt.h < r->right;
}


// ROM 0x00340f88 EmptyRect__FP4Rect
Boolean
EmptyRect(const Rect* r)
{
	return !(r->top < r->bottom && r->left < r->right);
}


// ROM 0x00340f60 EqualRect__FP4RectT1
Boolean
EqualRect(const Rect* a, const Rect* b)
{
	return a->top == b->top && a->left == b->left && a->bottom == b->bottom && a->right == b->right;
}


// ROM 0x00340d70 SectRect__FP4RectN21
// (RSect with two rectangles.)
Boolean
SectRect(const Rect* a, const Rect* b, Rect* result)
{
	return RSect(result, 2, a, b);
}


// ROM 0x003408d0 RSect__FP4RectlT1e
// The intersection of count rectangles; empty (and false) when any is
// empty or they do not all overlap.
Boolean
RSect(Rect* result, long count, const Rect* first, ...)
{
	if (count != 0)
	{
		long top = first->top, left = first->left, bottom = first->bottom, right = first->right;
		if (top < bottom && left < right)
		{
			va_list ap;
			va_start(ap, first);
			for (;;)
			{
				if (--count == 0)
				{
					va_end(ap);
					SetRect(result, left, top, right, bottom);
					return true;
				}
				const Rect* r = va_arg(ap, const Rect*);
				if (top < r->top)
					top = r->top;
				if (left < r->left)
					left = r->left;
				if (r->bottom < bottom)
					bottom = r->bottom;
				if (r->right < right)
					right = r->right;
				if (!(top < bottom && left < right))
					break;
			}
			va_end(ap);
		}
	}
	SetEmptyRect(result);
	return false;
}


// ROM 0x001975ec Union__5TRectF6TPoint
// The point taken into the rectangle.  A top of -0x8000 is the ROM's
// "nothing yet" mark: the rectangle then becomes the point alone, which
// is how MakeShape grows the bounds of a run of points.  Note the
// horizontal test is an either/or while the vertical one is not, so a
// point left of the left edge never moves the right edge - which is what
// the ROM does and is right for a rectangle that already holds a point.
void
UnionPt(Rect* r, Point pt)
{
	if (r->top == (short) 0x8000)
	{
		r->top = pt.v;
		r->left = pt.h;
		r->bottom = pt.v;
		r->right = pt.h;
		return;
	}
	if (pt.h <= r->left)
		r->left = pt.h;
	else if (pt.h >= r->right)
		r->right = pt.h;
	if (pt.v <= r->top)
	{
		r->top = pt.v;
		return;
	}
	if (pt.v < r->bottom)
		return;
	r->bottom = pt.v;
}

// ROM 0x00340d88 UnionRect__FP4RectN21
// The smallest rectangle holding both; an empty one contributes nothing.
void
UnionRect(const Rect* a, const Rect* b, Rect* result)
{
	if (EmptyRect(a))
	{
		*result = *b;
		return;
	}
	if (EmptyRect(b))
	{
		*result = *a;
		return;
	}
	Rect r;
	r.top = (a->top < b->top) ? a->top : b->top;
	r.left = (a->left < b->left) ? a->left : b->left;
	r.bottom = (a->bottom > b->bottom) ? a->bottom : b->bottom;
	r.right = (a->right > b->right) ? a->right : b->right;
	*result = r;
}


// ROM 0x00340e60 JoinRect__FP4RectN21
// The same, spelt out for two non-empty rectangles (the ROM has both).
void
JoinRect(const Rect* a, const Rect* b, Rect* result)
{
	if (a->top < a->bottom && a->left < a->right)
	{
		if (b->top < b->bottom && b->left < b->right)
		{
			Rect r;
			r.left = (a->left < b->left) ? a->left : b->left;
			r.top = (a->top < b->top) ? a->top : b->top;
			r.right = (b->right < a->right) ? a->right : b->right;
			r.bottom = (b->bottom < a->bottom) ? a->bottom : b->bottom;
			*result = r;
			return;
		}
		*result = *a;
		return;
	}
	*result = *b;
}


/*------------------------------------------------------------------------------
	M a p p i n g
------------------------------------------------------------------------------*/

// ROM 0x00335474 MapCoord__FlN41
// x's position in [srcStart, srcStart + srcSize) scaled into the
// destination range, rounded (away from zero at the half).
long
MapCoord(long x, long srcStart, long srcSize, long dstStart, long dstSize)
{
	long offset = x - srcStart;
	if (srcSize != dstSize)
	{
		Boolean negative = offset < 0;
		if (negative)
			offset = -offset;
		offset = (dstSize * offset + (srcSize >> 1)) / srcSize;
		if (negative)
			offset = -offset;
	}
	return offset + dstStart;
}


// ROM 0x00335308 MapPt__FP5PointP4RectT2
void
MapPt(Point* pt, const Rect* src, const Rect* dst)
{
	pt->v = (short) MapCoord(pt->v, src->top, src->bottom - src->top, dst->top, dst->bottom - dst->top);
	pt->h = (short) MapCoord(pt->h, src->left, src->right - src->left, dst->left, dst->right - dst->left);
}


// ROM 0x00340264 MapRect__FP4RectN21
void
MapRect(Rect* r, const Rect* src, const Rect* dst)
{
	long srcHeight = src->bottom - src->top, srcWidth = src->right - src->left;
	long dstHeight = dst->bottom - dst->top, dstWidth = dst->right - dst->left;
	r->top = (short) MapCoord(r->top, src->top, srcHeight, dst->top, dstHeight);
	r->left = (short) MapCoord(r->left, src->left, srcWidth, dst->left, dstWidth);
	r->bottom = (short) MapCoord(r->bottom, src->top, srcHeight, dst->top, dstHeight);
	r->right = (short) MapCoord(r->right, src->left, srcWidth, dst->left, dstWidth);
}


// ROM 0x003353b4 ScalePt__FP5PointP4RectT2
// A size scaled by the rectangles' proportions, never below 1.
void
ScalePt(Point* pt, const Rect* src, const Rect* dst)
{
	long v = MapCoord(pt->v, 0, src->bottom - src->top, 0, dst->bottom - dst->top);
	if (v < 1)
		v = 1;
	pt->v = (short) v;
	long h = MapCoord(pt->h, 0, src->right - src->left, 0, dst->right - dst->left);
	if (h < 1)
		h = 1;
	pt->h = (short) h;
}


// ROM 0x001991c4 CheapDistance__FRC6TPointT1
// An approximation of the distance between two points: the longer of the
// two axes' differences plus half the shorter.
long
CheapDistance(const Point& a, const Point& b)
{
	long dh = a.h - b.h;
	if (dh < 0)
		dh = -dh;
	long dv = a.v - b.v;
	if (dv < 0)
		dv = -dv;
	return (dh < dv) ? dv + (dh >> 1) : dh + (dv >> 1);
}

// ROM 0x00198f0c DistanceFromLine__6TPointCFRC6TPointT1
// How far the point is from the line through a and b: the cross product of
// a->pt and a->b over the line's length - measured the cheap way, so the
// answer is only roughly the perpendicular distance.  A line of no length
// divides by nought, which the ROM's __rt_sdiv throws as evt.ex.div0 (its
// data the return address, nil here).
long
DistanceFromLine(const Point& pt, const Point& a, const Point& b)
{
	long length = CheapDistance(a, b);
	long cross = (pt.v - a.v) * (b.h - a.h) - (b.v - a.v) * (pt.h - a.h);
	if (length == 0)
		Throw(exDivideByZero, nil, nil);
	long d = cross / length;
	return d < 0 ? -d : d;
}

// ROM 0x001976b0 Intersects__5TRectCFRC5TRect
// Whether the two rectangles have anything in common.
Boolean
Intersects(const Rect* r, const Rect* other)
{
	Rect sect;
	return SectRect(other, r, &sect);
}


// ROM 0x00197564 Encloses__5TRectCFRC5TRect
// Whether the other rectangle lies wholly inside this one; touching
// edges count as inside.
Boolean
Encloses(const Rect* r, const Rect* other)
{
	return other->top >= r->top && other->bottom <= r->bottom
		&& other->left >= r->left && other->right <= r->right;
}


// ROM 0x001991fc Overlaps__5TRectCFRC5TRect
// Intersects, but a rectangle with no width or height is given a pixel
// of it first, so that a caret - which is a line - still overlaps what it
// stands on.
Boolean
Overlaps(const Rect* r, const Rect* other)
{
	Rect mine = *r;
	Rect theirs = *other;
	if (mine.left == mine.right)
		mine.right++;
	if (mine.top == mine.bottom)
		mine.bottom++;
	if (theirs.left == theirs.right)
		theirs.right++;
	if (theirs.top == theirs.bottom)
		theirs.bottom++;
	return Intersects(&mine, &theirs);
}

// ROM 0x001976d8 CoveredBy__5TRectCFRC5TRect
// How much of r, as a percentage, the intersection with other covers.  A
// rectangle with no width or height would intersect nothing, so each is
// given a pixel first - the same widening THilite::Overlaps does.
long
CoveredBy(const Rect* r, const Rect* other)
{
	Rect mine = *r;
	Rect theirs = *other;
	if (mine.left == mine.right)
		mine.right++;
	if (mine.top == mine.bottom)
		mine.bottom++;
	if (theirs.left == theirs.right)
		theirs.right++;
	if (theirs.top == theirs.bottom)
		theirs.bottom++;
	if (!SectRect(&mine, &theirs, &theirs))
		return 0;
	long covered = (long) (theirs.bottom - theirs.top) * (theirs.right - theirs.left);
	long whole = (long) (mine.bottom - mine.top) * (mine.right - mine.left);
	return covered * 100 / whole;
}


// ROM 0x00198df0 Aligned__6TPointCFRC6TPointT1
// Where the point stands on the line from `a` to `b`.  The cross product
// of (b - a) and (pt - a) is how far off the line it is, times the line's
// length; rather than divide, the ROM compares it with ten times the sum
// of the line's width and height, which is a tolerance that grows with
// the line.  ==> 0 off the line, 1 past the `a` end, 2 past the `b` end,
// 3 between them.
long
Aligned(Point pt, const Point& a, const Point& b)
{
	long dh = pt.h - a.h;
	long dv = b.v - a.v;
	long acrossLine = dv * dh;
	long width = b.h - a.h;
	long down = pt.v - a.v;
	long cross = acrossLine - down * width;
	if (cross < 0)
		cross = -cross;
	long tolerance = (width < 0 ? -width : width) + (dv < 0 ? -dv : dv);
	tolerance = tolerance + tolerance * 4;		// five times, then doubled below
	if (cross >= tolerance * 2)
		return 0;
	// past the `a` end
	if (b.h < a.h && a.h < pt.h)	return 1;
	if (b.v < a.v && a.v < pt.v)	return 1;
	if (pt.h < a.h && a.h < b.h)	return 1;
	if (pt.v < a.v && a.v < b.v)	return 1;
	// past the `b` end
	if (a.h < b.h && b.h < pt.h)	return 2;
	if (a.v < b.v && b.v < pt.v)	return 2;
	if (pt.h < b.h && b.h < a.h)	return 2;
	if (pt.v < b.v && b.v < a.v)	return 2;
	return 3;
}


// ROM 0x001978f0 PinTo__6TPointFRC5TRect
void
PinTo(Point* pt, const Rect* r)
{
	if (r->left > pt->h)
		pt->h = r->left;
	if (r->right < pt->h)
		pt->h = r->right;
	if (r->top > pt->v)
		pt->v = r->top;
	if (r->bottom < pt->v)
		pt->v = r->bottom;
}


// ROM 0x00197820 Flip__5TRectFv
void
Flip(Rect* r)
{
	if (r->right < r->left)
	{
		short left = r->left;
		r->left = r->right;
		r->right = left;
	}
	if (r->bottom < r->top)
	{
		short top = r->top;
		r->top = r->bottom;
		r->bottom = top;
	}
}


// ROM 0x001975c0 Union__5TRectFRC5TRect
void
Union(Rect* r, const Rect* other)
{
	if (r->top == -32768)
	{
		*r = *other;
		return;
	}
	if (EmptyRect(other))
		return;
	if (EmptyRect(r))
	{
		*r = *other;
		return;
	}
	if (other->top < r->top)
		r->top = other->top;
	if (other->left < r->left)
		r->left = other->left;
	if (other->bottom > r->bottom)
		r->bottom = other->bottom;
	if (other->right > r->right)
		r->right = other->right;
}
