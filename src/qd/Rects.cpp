/*
	File:		qd/Rects.cpp

	Contains:	QuickDraw's rectangles and points.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "Rects.h"
#include <stdarg.h>


// ROM 0x00314068 SetRect__FP4RectlN32
void
SetRect(Rect* r, long left, long top, long right, long bottom)
{
	r->left = (short) left;
	r->top = (short) top;
	r->right = (short) right;
	r->bottom = (short) bottom;
}


// ROM 0x0031507c SetEmptyRect__FP4Rect
void
SetEmptyRect(Rect* r)
{
	r->left = 0;
	r->top = 0;
	r->right = 0;
	r->bottom = 0;
}


// ROM 0x003140a0 OffsetRect__FP4RectlT2
void
OffsetRect(Rect* r, long dh, long dv)
{
	r->top = (short) (r->top + (short) dv);
	r->left = (short) (r->left + (short) dh);
	r->bottom = (short) (r->bottom + (short) dv);
	r->right = (short) (r->right + (short) dh);
}


// ROM 0x003147d0 InsetRect__FP4RectlT2
void
InsetRect(Rect* r, long dh, long dv)
{
	r->top = (short) (r->top + (short) dv);
	r->left = (short) (r->left + (short) dh);
	r->bottom = (short) (r->bottom - (short) dv);
	r->right = (short) (r->right - (short) dh);
}


// ROM 0x0031420c Pt2Rect__F5PointT1P4Rect
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


// ROM 0x003142b4 PtInRect__F5PointP4Rect
// The pixel below and to the right of the point is in the rectangle.
Boolean
PtInRect(Point pt, const Rect* r)
{
	return r->top <= pt.v && pt.v < r->bottom && r->left <= pt.h && pt.h < r->right;
}


// ROM 0x00315040 EmptyRect__FP4Rect
Boolean
EmptyRect(const Rect* r)
{
	return !(r->top < r->bottom && r->left < r->right);
}


// ROM 0x00315018 EqualRect__FP4RectT1
Boolean
EqualRect(const Rect* a, const Rect* b)
{
	return a->top == b->top && a->left == b->left && a->bottom == b->bottom && a->right == b->right;
}


// ROM 0x00314e28 SectRect__FP4RectN21
// (RSect with two rectangles.)
Boolean
SectRect(const Rect* a, const Rect* b, Rect* result)
{
	return RSect(result, 2, a, b);
}


// ROM 0x00314988 RSect__FP4RectlT1e
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


// ROM 0x00314e40 UnionRect__FP4RectN21
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


// ROM 0x00314f18 JoinRect__FP4RectN21
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

// ROM 0x0030fedc MapCoord__FlN41
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


// ROM 0x0030fd70 MapPt__FP5PointP4RectT2
void
MapPt(Point* pt, const Rect* src, const Rect* dst)
{
	pt->v = (short) MapCoord(pt->v, src->top, src->bottom - src->top, dst->top, dst->bottom - dst->top);
	pt->h = (short) MapCoord(pt->h, src->left, src->right - src->left, dst->left, dst->right - dst->left);
}


// ROM 0x0031431c MapRect__FP4RectN21
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


// ROM 0x0030fe1c ScalePt__FP5PointP4RectT2
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


// ROM 0x0019b824 CheapDistance__FRC6TPointT1
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

// ROM 0x00199d24 CoveredBy__5TRectCFRC5TRect
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
