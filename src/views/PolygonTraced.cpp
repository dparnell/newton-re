/*
	File:		PolygonTraced.cpp

	Contains:	Part of a shape selected by tracing along it
				(TPolygonView::HiliteTraced) and the segment geometry under
				it.  The pen held still on a shape and then drawn along one
				of its sides selects the stretch of the shape it followed:
				the stroke's rough polygon is walked a step at a time
				(NextHiliteIndex), each step compared with the shape's
				segments just ahead of where the last one matched
				(SegSegTraced: how much of the one lies along the other,
				within the hit slop), and the stretch from the first match
				to the last - with its ends snapped to the start, middle or
				end of their segments, to where the old selection ended, or
				to where another shape on the page crosses (SearchForSnap) -
				made the selection, merged with the one there was
				(AddInterval, ExtractHiliteFromIntervals).

				A position along a shape is a 16.16 number: the segment in
				the high half, how far along it in the low half.  A segment
				is kept as its line's equation (SegParams): a*h + b*v = c,
				(a, b) the unit normal, and its length.

	Reconstructed from the MP2x00 US ROM (0x0018de34-0x0018ffb8); each
	function cites its origin.
*/

#include "PolygonView.h"
#include "Commands.h"
#include "EditView.h"		// gSkipView
#include "Rects.h"
#include "Polygons.h"		// PolyPointCount
#include "FixedMath.h"
#include "FixedMathExtra.h"
#include "Unit.h"
#include "UnitPublic.h"
#include "ShapeDomain.h"	// GetContextUnits
#include "Stroke.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "Hilites.h"
#include "NewtonMemory.h"
#include "host/RomBugs.h"
#include <string.h>

extern const int	kHiliteTracedCurve[17];		// PolygonViewTables.cpp: sqrt(1 - (i/16)^2), 16.16

// ROM 0x0c101768 gHiliteHitSlop
long		gHiliteHitSlop = 6;					// how near the pen has to be to a side, pixels
// ROM 0x0c10176c gHiliteHitSlopF
Fixed		gHiliteHitSlopF = 6 << 16;			// the same, 16.16


// A segment as the traced selection measures it (ROM 0x34 bytes).
struct SegParams
{
	Point		fFrom;			// +0x00
	Point		fTo;			// +0x04
	long		fH;				// +0x08  fFrom.h
	long		fV;				// +0x0c  fFrom.v
	Fixed		fA;				// +0x10  the line: fA * h + fB * v = fC, (fA, fB) its unit normal
	Fixed		fB;				// +0x14
	Fixed		fC;				// +0x18
	Fixed		fLength;		// +0x1c
	Fixed		fSlop;			// +0x20
	Rect		fBounds;		// +0x24  fFrom to fTo
	Rect		fSlopBounds;	// +0x2c  the same grown by the slop
};

// How much of a traced segment lies along a shape's (ROM 0x20 bytes).
struct SegTraced
{
	long		fFrom;			// +0x00  the shape segment's first point
	long		fTo;			// +0x04  and its last
	Fixed		fStart;			// +0x08  the stretch of the shape segment the traced one covers, with the slop
	Fixed		fTraceStart;	// +0x0c  where the traced segment starts along it, kept inside that
	Fixed		fTraceEnd;		// +0x10  where it ends
	Fixed		fEnd;			// +0x14
	Fixed		fDistance;		// +0x18  from the traced segment's end to the shape
	Fixed		fMissed;		// +0x1c  the traced length that was not along it
};


/*------------------------------------------------------------------------------
	T h e   s e g m e n t   g e o m e t r y
------------------------------------------------------------------------------*/

// ROM 0x0018de34 MiniSolver__FRC9SegParamsT1PlT3
// Where two lines cross: a1 h + b1 v = c1 and a2 h + b2 v = c2 solved by
// eliminating on the largest coefficient.  ==> false when they are (as
// good as) parallel.
static Boolean
MiniSolver(const SegParams& one, const SegParams& two, long* h, long* v)
{
	Fixed a1 = one.fA;
	Fixed b1 = one.fB;
	Fixed a2 = two.fA;
	Fixed b2 = two.fB;
	Fixed num, den;
	if (a1 == 0)
	{
		if (a2 == 0 || b1 == 0)
			return false;
		*v = FixedDivide(one.fC, b1);
		*h = FixedDivide(two.fC - FixedMultiply(two.fB, *v), a2);
		return true;
	}
	if (b2 == 0)
	{
		if (a2 == 0 || b1 == 0)
			return false;
		*h = FixedDivide(two.fC, a2);
		*v = FixedDivide(one.fC - FixedMultiply(one.fA, *h), b1);
		return true;
	}
	if (b1 == 0)
	{
		*h = FixedDivide(one.fC, a1);
		*v = FixedDivide(two.fC - FixedMultiply(two.fA, *h), b2);
		return true;
	}
	if (a2 == 0)
	{
		num = two.fC;
		den = b2;
	}
	else
	{
		long largest = a1 < 0 ? -a1 : a1;
		long pivot = 1;
		if (largest < (b1 < 0 ? -b1 : b1))
		{
			largest = b1 < 0 ? -b1 : b1;
			pivot = 2;
		}
		if (largest < (a2 < 0 ? -a2 : a2))
		{
			largest = a2 < 0 ? -a2 : a2;
			pivot = 4;
		}
		if (largest < (b2 < 0 ? -b2 : b2))
			pivot = 5;
		if (pivot == 2)
		{
			Fixed k = FixedDivide(FixedMultiply(b2, one.fC), b1);
			den = two.fA - FixedDivide(FixedMultiply(two.fB, one.fA), b1);
			if ((den < 0 ? -den : den) < 11)
				return false;
			*h = FixedDivide(two.fC - k, den);
			*v = FixedDivide(one.fC - FixedMultiply(one.fA, *h), b1);
			return true;
		}
		if (pivot == 4)
		{
			Fixed k = FixedDivide(FixedMultiply(a1, two.fC), two.fA);
			den = one.fB - FixedDivide(FixedMultiply(one.fA, two.fB), two.fA);
			if ((den < 0 ? -den : den) < 11)
				return false;
			*v = FixedDivide(one.fC - k, den);
			*h = FixedDivide(two.fC - FixedMultiply(two.fB, *v), two.fA);
			return true;
		}
		if (pivot == 5)
		{
			Fixed k = FixedDivide(FixedMultiply(b1, two.fC), two.fB);
			den = one.fA - FixedDivide(FixedMultiply(one.fB, two.fA), two.fB);
			if ((den < 0 ? -den : den) < 11)
				return false;
			*h = FixedDivide(one.fC - k, den);
			*v = FixedDivide(two.fC - FixedMultiply(two.fA, *h), two.fB);
			return true;
		}
		// pivot 1
		Fixed k = FixedDivide(FixedMultiply(a2, one.fC), a1);
		den = two.fB - FixedDivide(FixedMultiply(two.fA, one.fB), a1);
		if ((den < 0 ? -den : den) < 11)
			return false;
		num = two.fC - k;
	}
	*v = FixedDivide(num, den);
	*h = FixedDivide(one.fC - FixedMultiply(one.fB, *v), one.fA);
	return true;
}


// ROM 0x0018e128 RectRectOverlaps__FRC5TRectT1
// Whether two rectangles overlap, their edges counting.
static Boolean
RectRectOverlaps(const Rect& a, const Rect& b)
{
	return b.right >= a.left && a.right >= b.left && b.bottom >= a.top && a.bottom >= b.top;
}


// ROM 0x0018e174 CalcSegParameters__FRC6TPointT1P9SegParams
// A segment's line: its unit normal from the direction, its length (the
// average of FixedLength's estimate and what the normalised direction
// gives, which keeps a*a + b*b near one), and the boxes it is hit in.
static void
CalcSegParameters(const Point& from, const Point& to, SegParams* seg)
{
	seg->fFrom = from;
	seg->fTo = to;
	seg->fH = from.h;
	seg->fV = from.v;
	Fixed dh = (Fixed) ((ULong32) (short) (to.h - from.h) << 16);
	Fixed dv = (Fixed) ((ULong32) (short) (to.v - from.v) << 16);
	Fixed c;
	if (dv == 0)
	{
		seg->fA = 0;
		if (dh == 0)
			dh = 0x10000;
		if (dh >= 0)
		{
			seg->fLength = dh;
			seg->fB = 0x10000;
			c = (Fixed) ((ULong32) from.v << 16);
		}
		else
		{
			seg->fLength = -dh;
			seg->fB = (Fixed) 0xffff0000;
			c = -(Fixed) ((ULong32) from.v << 16);
		}
	}
	else if (dh == 0)
	{
		seg->fB = 0;
		if (dv < 0)
		{
			seg->fLength = -dv;
			seg->fA = 0x10000;
			c = (Fixed) ((ULong32) from.h << 16);
		}
		else
		{
			seg->fA = (Fixed) 0xffff0000;
			seg->fLength = dv;
			c = -(Fixed) ((ULong32) from.h << 16);
		}
	}
	else
	{
		Fixed length = FixedLength(dh, dv);
		Fixed na = FixedDivide(-dv, length);
		Fixed nb = FixedDivide(dh, length);
		Long32 sum = length + (FixedMultiply(nb, dh) - FixedMultiply(na, dv));
		seg->fLength = sum / 2;
		seg->fA = FixedDivide(-dv, seg->fLength);
		seg->fB = FixedDivide(dh, seg->fLength);
		c = (Fixed) ((Long32) seg->fA * (Long32) seg->fH + (Long32) seg->fV * (Long32) seg->fB);
	}
	seg->fC = c;
	Pt2Rect(from, to, &seg->fBounds);
	seg->fSlop = gHiliteHitSlopF;
	seg->fSlopBounds = seg->fBounds;
	InsetRect(&seg->fSlopBounds, -gHiliteHitSlop, -gHiliteHitSlop);
}


// ROM 0x0018e36c SegHitRatio__FRC6TPointRC9SegParamsPl
// Whether a point is within the slop of a segment: ==> how far along it,
// 16.16, 0 and 1.0 for the ends (reached within the slop of them), or
// 0x80000000 for not near it.  `distance`, when asked, is how far off
// the line the point is - or, for a point by an end, from that end.
static Fixed
SegHitRatio(const Point& pt, const SegParams& seg, long* distance)
{
	Fixed ratio = (Fixed) 0x80000000;
	long dh = pt.h - seg.fH;
	long dv = pt.v - seg.fV;
	Long32 off = (Long32) dh * seg.fA + (Long32) dv * seg.fB;
	Long32 along = 0;
	if (off <= seg.fSlop && off >= -seg.fSlop)
	{
		along = (Long32) dh * seg.fB - (Long32) dv * seg.fA;
		if (along >= -seg.fSlop && seg.fLength + seg.fSlop >= along)
		{
			if (along <= 0x8000)
			{
				off = FixedLength((Fixed) ((ULong32) dh << 16), (Fixed) ((ULong32) dv << 16));
				if (seg.fSlop >= off)
					ratio = 0;
			}
			else if (seg.fLength - 0x8000 > along)
				ratio = FixedDivide(along, seg.fLength);
			else
			{
				off = FixedLength((Fixed) ((ULong32) (pt.h - seg.fTo.h) << 16), (Fixed) ((ULong32) (pt.v - seg.fTo.v) << 16));
				if (seg.fSlop >= off)
					ratio = 0x10000;
			}
		}
	}
	if (distance != nil)
		*distance = off < 0 ? -off : off;
	return ratio;
}


// ROM 0x0018e484 BuildPts__FPC6TPointRC6TPointlP6TPointT4P6FPointT4
// The segment a position is on (a position at the very start of a segment
// being taken as the end of the one before), its two ends moved by the
// offset, and the point the position is at - rounded, and 16.16.
// ==> the segment's first point.
static long
BuildPts(const Point* points, const Point& offset, long position, Point* from, Point* at, FPoint* atF, Point* to)
{
	long index = position >> 16;
	long part = position & 0xffff;
	if (part == 0 && index > 0)
	{
		index--;
		part = 0x10000;
	}
	Point a, b;
	a.h = (short) (points[index].h + offset.h);
	a.v = (short) (points[index].v + offset.v);
	*from = a;
	b.h = (short) (points[index + 1].h + offset.h);
	b.v = (short) (points[index + 1].v + offset.v);
	*to = b;
	if (part >= 0xffff)
	{
		*at = b;
		atF->x = (Fixed) ((ULong32) b.h << 16);
		atF->y = (Fixed) ((ULong32) b.v << 16);
	}
	else
	{
		atF->x = (Fixed) ((ULong32) a.h << 16);
		atF->y = (Fixed) ((ULong32) a.v << 16);
		if (part > 0)
		{
			atF->x = (Fixed) (part * (b.h - a.h) + atF->x);
			atF->y = (Fixed) (part * (b.v - a.v) + atF->y);
			at->h = (short) ((atF->x + 0x8000) >> 16);
			at->v = (short) ((atF->y + 0x8000) >> 16);
		}
		else
			*at = a;
	}
	return index;
}


// ROM 0x0018e644 IsClosed__Fl
// Whether a shape of the verb closes on itself.
Boolean
IsClosed(long verb)
{
	switch (verb)
	{
	case 0: case 1: case 4: case 6: case 9: case 10: case 11: case 12:
		return true;
	default:
		return false;
	}
}


// ROM 0x0018e6bc TrySnap__FRC9SegParamslT2PlT4Pc
// Whether a snap position is the nearest yet: the distance along the
// segment from the position to it, in pixels 16.16.  (The name is only
// there for a debugging print.)
static void
TrySnap(const SegParams& seg, long part, long snapTo, long* best, long* bestPart, const char* /*name*/)
{
	long distance = FixedMultiply(part - snapTo, seg.fLength);
	if (distance < 0)
		distance = -distance;
	if (distance < *best)
	{
		*best = distance;
		*bestPart = snapTo;
	}
}


// ROM 0x0018e734 SearchForSnap__FP6TPointR6TPointlT3P9TUnitListPl
// An end of a selection snapped along its segment: to the segment's start
// (near the first tenth), middle (between .45 and .55) or end (past .9),
// or to where the old selection began or ended when that is on the same
// segment - whichever is nearest, within the slop.  Failing all those
// (nothing within a pixel), to where a stroke of another shape on the
// page crosses the segment near the point.  A position at the start of a
// segment is left alone.
static void
SearchForSnap(Point* points, Point& offset, long oldFirst, long oldLast, TUnitList* units, long* position)
{
	long part = *position & 0xffff;
	if (part == 0)
		return;
	long base = *position - part;
	Point from, at, to;
	FPoint atF;
	BuildPts(points, offset, *position, &from, &at, &atF, &to);
	SegParams seg;
	CalcSegParameters(from, to, &seg);
	long best = gHiliteHitSlopF + 1;
	long bestPart = (long) 0x80000000;
	if (part < 0x7334)
	{
		if (part <= 0x1999)
			TrySnap(seg, part, 0, &best, &bestPart, "Start");
	}
	else if (part <= 0x8ccc)
		TrySnap(seg, part, 0x8000, &best, &bestPart, "Mid");
	else if (part >= 0xe665)
		TrySnap(seg, part, 0x10000, &best, &bestPart, "End");
	if (oldFirst != (long) 0x80000000)
	{
		long d = oldFirst - base;
		if (d >= 0 && d <= 0x10000)
			TrySnap(seg, part, d, &best, &bestPart, "OldFirst");
		d = oldLast - base;
		if (d >= 0 && d <= 0x10000)
			TrySnap(seg, part, d, &best, &bestPart, "OldLast");
	}
	if (best >= 0x10000)
	{
		TArrayIterator iter;
		char* entry;
		if (units == nil)
		{
			entry = nil;
			iter.fCount = 0;
		}
		else
			entry = units->GetIterator(&iter);
		for (long i = 0; iter.fCount > i && best > 0 && entry != nil; i++, entry = iter.GetNext())
		{
			TSIUnit* unit = *(TSIUnit**) entry;
			long label = unit->GetLabel(0);
			if (label != 4 && label != 5 && label != 8 && label != 9 && label != 10 && label != 11 && label != 12)
				continue;
			TStroke* stroke = ((TStrokeUnit*) unit->GetSub(0))->fStroke;
			long count = stroke->fCount;
			FPoint fpt;
			stroke->GetFPoint(0, &fpt);
			Point cur;
			cur.h = (short) ((fpt.x + 0x8000) >> 16);
			cur.v = (short) ((fpt.y + 0x8000) >> 16);
			for (long j = 1; j < count; j++)
			{
				Point prev = cur;
				stroke->GetFPoint(j, &fpt);
				cur.h = (short) ((fpt.x + 0x8000) >> 16);
				cur.v = (short) ((fpt.y + 0x8000) >> 16);
				SegParams other;
				CalcSegParameters(prev, cur, &other);
				long h, v;
				if (RectRectOverlaps(other.fBounds, seg.fBounds)
				 && SegHitRatio(at, other, nil) != (Fixed) 0x80000000
				 && MiniSolver(other, seg, &h, &v))
				{
					long distance = FixedLength(h - atF.x, v - atF.y);
					if (distance < best)
					{
						Point cross;
						cross.h = (short) ((h + 0x8000) >> 16);
						cross.v = (short) ((v + 0x8000) >> 16);
						Fixed ratio = SegHitRatio(cross, seg, nil);
						if (ratio != (Fixed) 0x80000000)
						{
							bestPart = ratio;
							best = distance;
							if (distance == 0)
								break;
						}
					}
				}
			}
		}
	}
	if (bestPart != (long) 0x80000000)
		*position = base + bestPart;
}


// ROM 0x0018eb78 IntervalIndex__FlPlT1
// Where a position falls among the intervals' ends (sorted pairs): the
// index of the first end not below it - an odd index is inside an
// interval - a position on an interval's start counting as inside it.
static long
IntervalIndex(long position, long* ends, long count)
{
	long i = 0;
	while (i < count && ends[i] < position)
		i++;
	if (i < count && ends[i] == position && (i & 1) == 0)
		i++;
	return i;
}


// ROM 0x0018ebb8 AddInterval__FlT1PlT3
// An interval added to the (at most five) kept in order: one that falls
// wholly between two is put in there, one that overlaps is merged.
// ROM BUG (fixed): an interval that runs over more than one of the others
// is only merged with the first of them - the length of the stretch to
// close up comes out negative (the first index less the last) and nothing
// is moved - so the ones it covers stay as they were.  The fix closes up
// the ends between the merged interval's start and its end, so the ones it
// covers go.
static void
AddInterval(long from, long to, long* ends, long* count)
{
	long n = *count * 2;
	if (from == to)
		return;
	if (to < from)
	{
		long t = from;
		from = to;
		to = t;
	}
	long first = IntervalIndex(from, ends, n);
	long last = IntervalIndex(to, ends, n);
	if (first == last)
	{
		if ((first & 1) != 0)
			return;							// inside one already
		if (n >= 11)
			return;							// no room
		if (first < n)
			memmove(ends + first + 2, ends + first, (n - first) * sizeof(long));
		ends[first] = from;
		ends[first + 1] = to;
		*count = *count + 1;
		return;
	}
	if ((first & 1) == 0)
		ends[first] = from;
	else
		first--;
	if ((last & 1) == 0)
	{
		last--;
		ends[last] = to;
	}
	if (RomBugFixed())
	{
		long gone = last - (first + 1);	// the ends inside the merged interval
		if (gone > 0)
		{
			memmove(ends + first + 1, ends + last, (n - last) * sizeof(long));
			*count = *count - gone / 2;
		}
		return;
	}
	long move = (first + 1) - last;
	if (move <= 0)
		return;
	memmove(ends + first + 1, ends + last, move * sizeof(long));
	*count = *count - move / 2;
}


// ROM 0x0018ecb4 ExtractHiliteFromIntervals__FPlT1lN23UcN41
// The old selection (first to last, when there was one) added to the new
// intervals - round the join of a closed shape when it wrapped - and the
// selection read out of them: one interval, or two on a closed shape that
// meet round its join (the second then taken as the start).  ==> false for
// any other number.
static Boolean
ExtractHiliteFromIntervals(long* ends, long* count, long oldFirst, long oldLast, long last, Boolean closed,
						   long* firstIndex, long* firstFrac, long* lastIndex, long* lastFrac)
{
	if (oldFirst >= 0 && oldFirst <= last && oldLast >= 0 && oldLast <= last)
	{
		long n = *count * 2;
		long i1 = IntervalIndex(oldFirst, ends, n);
		long i2 = IntervalIndex(oldLast, ends, n);
		if (oldFirst > oldLast)
		{
			if (i2 > 0 || i1 < n)
			{
				AddInterval(oldFirst, last, ends, count);
				AddInterval(0, oldLast, ends, count);
			}
		}
		else if (i1 < i2)
			AddInterval(oldFirst, oldLast, ends, count);
		else if (closed)
		{
			if ((oldFirst == 0 && ends[n - 1] == last) || (oldLast == last && ends[0] == 0))
				AddInterval(oldFirst, oldLast, ends, count);
		}
	}
	long n = *count * 2;
	if (n < 4)
	{
		if (n < 2)
			return false;
	}
	else
	{
		if (n > 4)
			return false;
		if (!closed)
			return false;
		if (ends[0] != 0)
			return false;
		if (ends[n - 1] < last - 1)
			return false;
	}
	*firstIndex = ends[n - 2] >> 16;
	*firstFrac = ends[n - 2] & 0xffff;
	*lastIndex = (ends[1] >> 16) + 1;
	*lastFrac = ends[1] & 0xffff;
	return true;
}


// ROM 0x0018ee60 NextPolySegHit__FP12PolygonShapeR6TPointPlN23
// The segment after `*from` (a segment is numbered by its last point)
// that the point is nearest, within the slop - the walk stops at the
// first one missed once one has been hit.  ==> false for none; `*from`
// moved on to where the walk stopped, for the next call.
static Boolean
NextPolySegHit(PolygonShape* shape, Point& pt, long* from, long* segment, long* distance)
{
	long best = gHiliteHitSlopF + 1;
	long found = 0;
	long i = *from + 1;
	for ( ; i < shape->fCount; i++)
	{
		SegParams seg;
		CalcSegParameters(shape->fPoints[i - 1], shape->fPoints[i], &seg);
		seg.fSlop = gHiliteHitSlopF;
		long d;
		if (SegHitRatio(pt, seg, &d) != (Fixed) 0x80000000)
		{
			if (d < 0)
				d = -d;
			if (best > d)
			{
				best = d;
				found = i;
			}
		}
		else if (best <= gHiliteHitSlopF)
			break;
	}
	if (best > gHiliteHitSlopF)
		return false;
	*segment = found;
	*distance = best;
	long last = shape->fCount - 1;
	*from = i < last ? i : last;
	return true;
}


// ROM 0x0018ef60 NextPSegBegin__FP12PolygonShapeUclT3
// The next point to start a segment from, going `step` from `index` - off
// either end a closed shape carries on from the other, an open one gives
// -1 - past any repeated points.  -1 when the walk comes all the way round.
static long
NextPSegBegin(PolygonShape* shape, Boolean closed, long index, long step)
{
	long start = index;
	for (;;)
	{
		long next = index + step;
		if (next < 0)
			index = closed ? shape->fCount - 1 : -1;
		else if (next >= shape->fCount)
			index = closed ? 0 : -1;
		if (index < 0)
			return index;
		next = index + step;
		if (*(ULong32*) &shape->fPoints[next] != *(ULong32*) &shape->fPoints[index])
			return index;
		if (next == start)
			return -1;
		index = next;
	}
}


// ROM 0x0018efec NextHiliteIndex__FP6TPointlN22
// The next point of the stroke more than `tolerance` from `index` in
// either direction - halving the tolerance until one is when none is.
// -1 at the end.
static long
NextHiliteIndex(Point* points, long index, long count, long tolerance)
{
	Point at = points[index];
	for (long i = index + 1; i < count; i++)
	{
		long d = at.h - points[i].h;
		if (d < 0)
			d = -d;
		if (d > tolerance)
			return i;
		d = at.v - points[i].v;
		if (d < 0)
			d = -d;
		if (d > tolerance)
			return i;
	}
	if (index + 1 < count && tolerance > 0)
		return NextHiliteIndex(points, index, count, tolerance / 2);
	return -1;
}


// ROM 0x0018f090 SegSegTraced__FRC9SegParamsT1P9SegTraced
// How a traced segment lies along a shape's segment: both its ends within
// the slop of the line, the two going the same way, and it overlapping
// the shape segment along its length.  The stretch of the shape segment
// it accounts for (fStart to fEnd) is where its ends are along it - pushed
// out by the slop, or, for an end off the line, by how far the slop's
// circle reaches at that height (kHiliteTracedCurve) - kept within the
// segment; fTraceStart/fTraceEnd are the traced ends kept inside that, and
// fDistance how far the traced segment's end is from the shape.  ==> false
// when it is not along it (fDistance then large, fMissed its length).
static Boolean
SegSegTraced(const SegParams& trace, const SegParams& shape, SegTraced* traced)
{
	Boolean along = false;
	traced->fMissed = 0;
	Fixed a = shape.fA;
	Fixed b = shape.fB;
	Long32 d1 = (Long32) trace.fFrom.h * a + (Long32) trace.fFrom.v * b - shape.fC;
	Long32 d2 = (Long32) trace.fTo.h * a + (Long32) trace.fTo.v * b - shape.fC;
	Long32 t0 = (Long32) shape.fH * b - (Long32) shape.fV * a;
	Long32 t1 = ((Long32) trace.fFrom.h * b - (Long32) trace.fFrom.v * a) - t0;
	Long32 t2 = ((Long32) trace.fTo.h * b - (Long32) trace.fTo.v * a) - t0;
	Fixed cosine = FixedMultiply(a, trace.fA) + FixedMultiply(shape.fB, trace.fB);
	long startKind, endKind;
	Long32 lo, hi;
	long otherKind;
	if (d1 > d2)
	{
		startKind = 1;
		lo = d2;
		hi = d1;
		otherKind = 0;
	}
	else
	{
		startKind = 0;
		lo = d1;
		hi = d2;
		otherKind = 1;
	}
	Fixed slop = shape.fSlop;
	if (slop < lo || -slop > hi || t2 < t1 || -slop > t2
	 || shape.fLength + slop < t1 || cosine < 0)
	{
		traced->fStart = 0;
		traced->fTraceStart = 0;
		traced->fTraceEnd = 0;
		traced->fEnd = 0;
		traced->fDistance = 0x3e70000;
		traced->fMissed = trace.fLength;
		return false;
	}
	along = true;
	Long32 abs1 = d1 < 0 ? -d1 : d1;
	Long32 abs2 = d2 < 0 ? -d2 : d2;
	Fixed reach = FixedMultiply(slop, cosine);
	if (lo > reach)
		endKind = startKind;
	else if (hi < -reach)
	{
		startKind = otherKind;
		endKind = otherKind;
	}
	else
	{
		startKind = abs1 > reach ? 2 : 0;
		endKind = abs2 > reach ? 2 : 1;
	}
	if (startKind == 2 || endKind == 2)
	{
		// an end crosses the slop band: where the traced line meets its edge
		Fixed band = slop;
		Long32 at = t1;
		if (cosine != 0)
		{
			Fixed q = FixedDivide(t2 - t1, hi - lo);
			band = FixedMultiplyDivide(band, q, cosine);
			Fixed shift = FixedMultiply(abs1, q);
			if ((d1 > 0) == (d1 < d2))
				shift = -shift;
			at = t1 + shift;
		}
		if (startKind == 2)
			traced->fStart = at - band;
		if (endKind == 2)
			traced->fEnd = at + band;
	}
	if (!(startKind == 2 && endKind == 2))
	{
		// (the ROM works both out; the one not used may be for an end
		// beyond the slop, off the end of the table, so the host only
		// looks up the one it uses)
#define REACH(d)	FixedMultiply(slop, kHiliteTracedCurve[FixedDivide(d, slop) >> 12])
		if (startKind == 0)
			traced->fStart = t1 - REACH(abs1);
		else if (startKind == 1)
			traced->fStart = t2 - REACH(abs2);
		if (endKind == 1)
			traced->fEnd = t2 + REACH(abs2);
		else if (endKind == 0)
			traced->fEnd = t1 + REACH(abs1);
#undef REACH
	}
	if (traced->fStart < 0)
		traced->fStart = 0;
	else if (traced->fStart > shape.fLength)
		traced->fStart = shape.fLength;
	if (traced->fEnd > shape.fLength)
		traced->fEnd = shape.fLength;
	else if (traced->fEnd < 0)
		traced->fEnd = 0;
	if (traced->fStart > t1)
		traced->fTraceStart = traced->fStart;
	else if (traced->fEnd >= t1)
		traced->fTraceStart = t1;
	else
		traced->fTraceStart = traced->fEnd;
	if (traced->fStart > t2)
		traced->fTraceEnd = traced->fStart;
	else if (traced->fEnd < t2)
		traced->fTraceEnd = traced->fEnd;
	else
		traced->fTraceEnd = t2;
	Fixed distance;
	if (traced->fTraceEnd == t2)
		distance = abs2;
	else
	{
		Fixed h = FixedMultiply(shape.fB, traced->fTraceEnd) + (Fixed) ((ULong32) (unsigned short) shape.fFrom.h << 16);
		Fixed v = (Fixed) ((ULong32) (unsigned short) shape.fFrom.v << 16) - FixedMultiply(shape.fA, traced->fTraceEnd);
		distance = FixedLength((Fixed) ((ULong32) (unsigned short) trace.fTo.h << 16) - h,
							   (Fixed) ((ULong32) (unsigned short) trace.fTo.v << 16) - v);
	}
	traced->fDistance = distance;
	return along;
}


// ROM 0x0018f4cc HiliteTracedFrom__FP12PolygonShapelT2P6TPointT2RC6TPointPlT7
// The stroke's points (moved by the offset) followed along the shape from
// segment `from` in the direction `step`: each step of the stroke is
// matched against the shape segments just ahead (up to eight, until they
// are longer than the slop between them) and the best of them taken, the
// ones before it passed; the gaps and the stretches of the stroke along
// nothing are totted up and more than twice the slop of either ends it.
// ==> true when at least eight pixels were traced, with the selection's
// two ends (the whole shape when the trace came back round to where it
// began on a closed one).
static Boolean
HiliteTracedFrom(PolygonShape* shape, long from, long step, Point* points, long count, const Point& offset,
				 long* first, long* last)
{
	SegParams segs[8];
	SegTraced traced[8];
	SegParams trace;
	long firstSeg = -1;			// [0x30]
	long lastSeg = -1;			// [0x2c]
	Fixed firstFrac = 0;		// [0x28]
	Fixed lastFrac = 0;			// [0x24]
	long index = 0;				// [0x1c] the stroke point reached
	long gaps = 0;				// r8: the shape not covered between matches
	long missed = 0;			// [0x20]: the stroke along nothing
	long covered = 0;			// r7: the shape traced
	Fixed prevStart = 0;		// [0x0c]
	Fixed prevEnd = 0;			// [0x10]
	Boolean leftFirst = false;	// [0x08] the trace went on past the first segment
	Boolean wholeShape = false;	// [0x04] and came back to it
	Point here;
	here.h = (short) (points[0].h + offset.h);
	here.v = (short) (points[0].v + offset.v);
	long nSegs = 0;				// r4
	Fixed ahead = 0;			// r6: the length of the segments ahead
	Boolean closed = IsClosed(shape->fVerb);
	for (;;)
	{
		if (gaps > gHiliteHitSlopF * 2 || missed > gHiliteHitSlopF * 2)
			return false;
		index = NextHiliteIndex(points, index, count, 2);
		if (index < 0)
			break;
		Point prev = here;
		here.h = (short) (points[index].h + offset.h);
		here.v = (short) (points[index].v + offset.v);
		CalcSegParameters(prev, here, &trace);
		while (nSegs < 8 && ahead <= gHiliteHitSlopF)
		{
			long begin = NextPSegBegin(shape, closed, from, step);
			if (begin < 0)
				break;
			from = begin + step;
			CalcSegParameters(shape->fPoints[begin], shape->fPoints[from], &segs[nSegs]);
			ahead += segs[nSegs].fLength;
			traced[nSegs].fFrom = begin;
			traced[nSegs].fTo = from;
			nSegs++;
		}
		Fixed best = 0x7fffffff;
		long match = nSegs;
		for (long i = 0; i < nSegs; i++)
			if (SegSegTraced(trace, segs[i], &traced[i]) && traced[i].fDistance <= best)
			{
				match = i;
				best = traced[i].fDistance;
			}
		if (match == nSegs)
		{
			missed += traced[0].fMissed;
			continue;
		}
		if (firstSeg == -1)
		{
			firstSeg = traced[0].fFrom;
			firstFrac = FixedDivide(traced[0].fTraceStart, segs[0].fLength);
			prevEnd = traced[0].fEnd;
			prevStart = traced[0].fTraceStart;
		}
		lastSeg = traced[match].fFrom;
		lastFrac = FixedDivide(traced[match].fTraceEnd, segs[match].fLength);
		if (traced[0].fEnd < prevEnd)
			traced[0].fEnd = prevEnd;
		if (traced[0].fStart > prevEnd)
			traced[0].fStart = traced[0].fStart - prevEnd;
		else
			traced[0].fStart = 0;
		for (long i = 0; i <= match; i++)
		{
			if (i < match)
				gaps += segs[i].fLength - (traced[i].fEnd - traced[i].fStart);
			else
				gaps += traced[i].fStart;
			if (i > 0)
				covered += traced[i].fTraceEnd - traced[i].fTraceStart;
			else if (traced[0].fTraceEnd > prevStart)
			{
				Fixed start = traced[0].fTraceStart;
				if (prevStart > start)
					start = prevStart;
				covered += traced[0].fTraceEnd - start;
			}
			if (traced[i].fFrom != firstSeg)
				leftFirst = true;
			else if (leftFirst && (i != match || lastFrac >= firstFrac))
				wholeShape = true;
		}
		if (match > 0)
		{
			nSegs -= match;
			BlockMove(&segs[match], &segs[0], nSegs * sizeof(SegParams));
			BlockMove(&traced[match], &traced[0], nSegs * sizeof(SegTraced));
		}
		prevEnd = traced[0].fEnd;
		prevStart = traced[0].fTraceEnd;
		ahead = -prevEnd;
		for (long i = 0; i < nSegs; i++)
			ahead += segs[i].fLength;
	}
	if (covered < 0x80000)
		return false;
	if (wholeShape)
	{
		*first = 0;
		*last = (shape->fCount - 1) << 16;
	}
	else if (step > 0)
	{
		*first = firstFrac + (firstSeg << 16);
		*last = lastFrac + (lastSeg << 16);
	}
	else
	{
		*first = (lastSeg << 16) - lastFrac;
		*last = (firstSeg << 16) - firstFrac;
	}
	return true;
}


// ROM 0x0018fa3c HiliteTraced__12TPolygonViewFP11TUnitPublicUc
// Whether the hilite stroke was traced along the shape: its first point
// near a side (NextPolySegHit, each side in turn), the stroke then
// followed from there one way or the other (HiliteTracedFrom), the
// nearest side that works taken.  With `reallyDoIt` the part traced is
// selected - its ends snapped (SearchForSnap, to the shapes about it
// that the recognition context gives, the stroke's unit made a 'GSHP'
// covering it for the asking), merged with what was selected before, and
// made the hilite.  Ink is never traced.
Boolean
TPolygonView::HiliteTraced(TUnitPublic* unit, Boolean reallyDoIt)
{
	RefVar points(Points());
	PolygonShape* shape = (PolygonShape*) BinaryData(points);
	if (shape->fVerb == kPolyInk)
		return false;
	Rect bounds = viewBounds;
	Handle rough = unit->RoughShape();
	if (rough == nil)
		return false;
	Polygon* poly = (Polygon*) *rough;
	Point* strokePoints = poly->polyPoints;
	long count = PolyPointCount(poly);
	Rect first;
	Pt2Rect(strokePoints[0], strokePoints[0], &first);
	OffsetRect(&first, poly->polyBBox.left, poly->polyBBox.top);
	InsetRect(&first, -gHiliteHitSlop, -gHiliteHitSlop);
	if (!RectRectOverlaps(bounds, first))
		return false;
	// the stroke's points are relative to its box: this puts them in the view
	Point offset;
	offset.v = (short) (poly->polyBBox.top - viewBounds.top);
	offset.h = (short) (poly->polyBBox.left - viewBounds.left);
	Point start;
	start.h = (short) (strokePoints[0].h + offset.h);
	start.v = (short) (strokePoints[0].v + offset.v);
	Boolean traced = false;
	long best = gHiliteHitSlopF + 1;
	long from = 0;
	long segment, distance;
	long hiliteFirst = 0, hiliteLast = 0;
	while (NextPolySegHit(shape, start, &from, &segment, &distance))
	{
		if (distance < best)
		{
			long a, b;
			if (HiliteTracedFrom(shape, segment - 1, 1, strokePoints, count, offset, &a, &b)
			 || HiliteTracedFrom(shape, segment, -1, strokePoints, count, offset, &a, &b))
			{
				traced = true;
				best = distance;
				hiliteFirst = a;
				hiliteLast = b;
			}
		}
		// (the shape may have moved while the stroke was followed)
		shape = (PolygonShape*) BinaryData(points);
	}
	if (!traced)
		return false;
	if (!reallyDoIt)
		return true;
	long oldFirst = (long) 0x80000000;
	long oldLast = (long) 0x80000000;
	RefVar hiliteRef(FirstHilite());
	if (NOTNIL(hiliteRef))
	{
		TPolygonHilite* old = (TPolygonHilite*) RefToAddress(hiliteRef);
		oldFirst = old->fFirstPart + (old->fFirst << 16);
		oldLast = old->fLastPart + ((old->fLast - 1) << 16);
	}
	Rect strokeBounds;
	unit->Bounds(&strokeBounds);
	InsetRect(&strokeBounds, -gHiliteHitSlop, -gHiliteHitSlop);
	FRect box;
	box.left = (Fixed) ((ULong32) strokeBounds.left << 16);
	box.top = (Fixed) ((ULong32) strokeBounds.top << 16);
	box.right = (Fixed) ((ULong32) strokeBounds.right << 16);
	box.bottom = (Fixed) ((ULong32) strokeBounds.bottom << 16);
	TUnit* shapeUnit = unit->fUnit;
	shapeUnit->fType = 'GSHP';
	shapeUnit->SetBBox(&box);
	gSkipView = this;
	TUnitList* units = GetContextUnits(shapeUnit, 0);
	gSkipView = nil;
	Point where;
	where.v = viewBounds.top;
	where.h = viewBounds.left;
	Boolean closed = IsClosed(shape->fVerb);
	// the ROM has room for eight ends, AddInterval taking up to twelve, and
	// ExtractHiliteFromIntervals reads the word before them when there
	// are none: the host has room for all of it
	long endsBuffer[13];
	long* ends = endsBuffer + 1;
	endsBuffer[0] = 0;
	long intervals = 0;
	shape = (PolygonShape*) BinaryData(points);
	SearchForSnap(shape->fPoints, where, oldFirst, oldLast, units, &hiliteFirst);
	shape = (PolygonShape*) BinaryData(points);
	SearchForSnap(shape->fPoints, where, oldFirst, oldLast, units, &hiliteLast);
	if (units != nil)
		DisposeContextUnits(units);
	shape = (PolygonShape*) BinaryData(points);
	long lastPosition = (shape->fCount - 1) << 16;
	if (hiliteFirst <= hiliteLast)
		AddInterval(hiliteFirst, hiliteLast, ends, &intervals);
	else if (closed)
	{
		AddInterval(0, hiliteLast, ends, &intervals);
		AddInterval(hiliteFirst, lastPosition, ends, &intervals);
	}
	long firstIndex, firstFrac, lastIndex, lastFrac;
	if (ExtractHiliteFromIntervals(ends, &intervals, oldFirst, oldLast, lastPosition, closed,
								   &firstIndex, &firstFrac, &lastIndex, &lastFrac))
		MakeHilite(firstIndex, firstFrac, lastIndex, lastFrac);
	return true;
}
