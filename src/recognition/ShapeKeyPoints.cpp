/*
	File:		recognition/ShapeKeyPoints.cpp

	Contains:	FindKeyPoints: a shape's strokes fitted with corners and
				curves, and written out as its outline of GeneralPts.

				Each stroke (and each shape on the page one of the shape's
				free ends met, taken the way round it runs) goes through the
				same four steps:

				  RLineOut		the corners: the run of points is split
								where it strays farthest from its chord,
								over and over (RLineOut2), and the runs of
								tightly bunched samples are noted
								(RSmallDists);
				  Collapser		corners too close together are merged (the
								one that turns less goes), and corners the
								line barely turns at are dropped;
				  FindCubic1	each stretch between two corners becomes a
								cubic (a SplineSeg: its ends and the
								tangents there), a bend being smooth or a
								kink by CheckSmooth;
				  Connect		the stroke's segments joined onto the ones
								before, merged where the join is smooth.

				Then the segments are written out: a straight one as its end,
				a curved one as a control point and its end (DoConic, a conic
				through the two tangents), a curve that changes the way it
				bends as two (FindInflection, DoConicInfl), and a closed shape
				has its last segment joined to its first (MeetEnds).

	Reconstructed from the MP2x00 US ROM (0x0021227c-0x0021534c); each
	function cites its origin.
*/

#include "ShapeGeometry.h"
#include "Stroke.h"
#include "StrokeQueue.h"
#include "EdgeList.h"			// Signum
#include "Angles.h"
#include "FixedMath.h"
#include "NewtonMemory.h"

#include "host/RomBugs.h"
#include <string.h>


// ROM 0x0c104d00 ptZero
const FPoint	ptZero = { 0, 0 };

// ROM 0x0c107020 (unnamed) - the box a shape small enough to be a blob is
// held within (SetGeneralPt); its top is -1000 when there is none
static FRect	gGSClampRect;

// ROM 0x0c104d18 (unnamed) - FindInflection's memory of the way the last
// curve bent, and whether it may be trusted
static long		gGSLastBend;
static Boolean	gGSBendKnown;


/*------------------------------------------------------------------------------
	S m a l l   p i e c e s
------------------------------------------------------------------------------*/

// ROM 0x0021566c NORMD__FPl
// An angle in 16.16 degrees brought into (-180, 180].
void
NORMD(long* angle)
{
	if (*angle > 0xb40000)
	{
		do
			*angle -= 0x1680000;
		while (*angle > 0xb40000);
		return;
	}
	if (*angle > -0xb40000)
		return;
	do
		*angle += 0x1680000;
	while (*angle <= -0xb40000);
}


// ROM 0x00213988 Delta__FlT1
// How far apart two angles are, the short way round.
long
Delta(long a, long b)
{
	long d = a - b;
	if (d < 0)
		d = -d;
	if (d > 0xb40000)
		d = 0x1680000 - d;
	return d;
}


// ROM 0x002142e4 SameAngle__F6FPointT1l
// Whether two directions (vectors) are within `slop` degrees of each other.
Boolean
SameAngle(FPoint a, FPoint b, long slop)
{
	long angleA = PtsToAngle(&a, &ptZero, 0x10000);
	long angleB = PtsToAngle(&b, &ptZero, 0x10000);
	return Delta(angleA, angleB) < slop;
}


// ROM 0x002140ac ScaleToSize__FP6FPointl
// A vector made `size` long (a vector of nothing taken as one pixel).
void
ScaleToSize(FPoint* v, long size)
{
	Fixed length = DistPoint(v, &ptZero);
	if (length == 0)
		length = 0x10000;
	Fixed scale = FixedDivide(size, length);
	v->x = FixedMultiply(v->x, scale);
	v->y = FixedMultiply(v->y, scale);
}


// ROM 0x00214108 Project__F6FPointT1
// `a` projected onto the direction of `b`.
FPoint
Project(FPoint a, FPoint b)
{
	Fixed length = CheapDistPoint(&b, &ptZero);
	Fixed along = FixedDivide(FixedMultiply(a.y, b.y) + FixedMultiply(b.x, a.x), length);
	along = FixedDivide(along, length);
	FPoint p;
	p.x = FixedMultiply(b.x, along);
	p.y = FixedMultiply(b.y, along);
	return p;
}


// ROM 0x002147dc Reflect__FP6FPointT1
// `v` reflected in the line along `axis`.
void
Reflect(FPoint* v, FPoint* axis)
{
	FPoint p = Project(*v, *axis);
	v->x = p.x * 2 - v->x;
	v->y = p.y * 2 - v->y;
}


// ROM 0x002156dc IntersectLine__FP6FPointN41
// Where the line through a1 and a2 meets the line through b1 and b2.  The
// directions are brought down to a sixty-fourth of their size first when
// they are long, so the products cannot overflow.  Parallel lines, or two
// upright ones, leave `pt` alone.
void
IntersectLine(FPoint* pt, FPoint* a1, FPoint* a2, FPoint* b1, FPoint* b2)
{
	Fixed adx = a2->x - a1->x;
	Fixed ady = a2->y - a1->y;
	Fixed bdx = b2->x - b1->x;
	Fixed bdy = b2->y - b1->y;
	long size = ((adx < 0) ? -adx : adx) + ((ady < 0) ? -ady : ady);
	if (size > 0x200000)
	{
		adx = FixedDivide(adx, size / 32);
		ady = FixedDivide(ady, size / 32);
	}
	size = ((bdx < 0) ? -bdx : bdx) + ((bdy < 0) ? -bdy : bdy);
	if (size > 0x200000)
	{
		bdx = FixedDivide(bdx, size / 32);
		bdy = FixedDivide(bdy, size / 32);
	}
	Fixed x, y;
	if (adx == 0)
	{
		if (bdx == 0)
			return;
		x = a1->x;
		Fixed slope = FixedDivide(bdy, bdx);
		y = FixedMultiply(slope, x) + (b1->y - FixedMultiply(slope, b1->x));
	}
	else if (bdx == 0)
	{
		x = b1->x;
		Fixed slope = FixedDivide(ady, adx);
		y = FixedMultiply(slope, x) + (a1->y - FixedMultiply(slope, a1->x));
	}
	else
	{
		Fixed det = FixedMultiply(adx, -bdy) + FixedMultiply(bdx, ady);
		if (det == 0)
			return;
		Fixed t = FixedDivide(FixedMultiply(bdx, b1->y - a1->y) - FixedMultiply(bdy, b1->x - a1->x), det);
		x = a1->x + FixedMultiply(t, adx);
		y = a1->y + FixedMultiply(t, ady);
	}
	pt->x = x;
	pt->y = y;
}


/*------------------------------------------------------------------------------
	T h e   o u t l i n e ' s   p o i n t s
------------------------------------------------------------------------------*/

// ROM 0x00212ed4 InitGeneralPt__FP7TDArrayUl6FPoint
// A plain corner at the index (appended when the index is the count).
void
InitGeneralPt(TDArray* shape, ULong index, FPoint pt)
{
	GeneralPt* entry = (index < (ULong) shape->Count()) ? (GeneralPt*) shape->GetEntry(index)
														  : (GeneralPt*) shape->AddEntry();
	entry->fPt.y = pt.y;
	entry->fPt.x = pt.x;
	entry->fControl = 0;
	entry->f09 = 0;
	entry->f0a = 0;
}


// ROM 0x00212f2c SetGeneralPt__FP7TDArrayUl6FPointUcN24
// A point of the outline set at the index (appended at the count; an index
// past it is refused): a point off the screen is refused, and noted when
// it is not a control point; a point is held within the blob box when
// there is one.  The first point never carries the third flag.  ==>
// whether it was set.
Boolean
SetGeneralPt(TDArray* shape, ULong index, FPoint pt, UByte control, UByte flag9, UByte flag10)
{
	if (!PointInRectangle(&pt, &gGSScreenRect))
	{
		if (control == 0)
			gGSOffScreen = true;
		return false;
	}
	if (gGSClampRect.top != -1000)
	{
		if (pt.x < gGSClampRect.left)
			pt.x = gGSClampRect.left;
		else if (gGSClampRect.right < pt.x)
			pt.x = gGSClampRect.right;
		Fixed y = gGSClampRect.top;
		if (gGSClampRect.top <= pt.y)
		{
			y = pt.y;
			if (gGSClampRect.bottom < pt.y)
				y = gGSClampRect.bottom;
		}
		pt.y = y;
	}
	GeneralPt* entry;
	long where = Signum((long) shape->Count() - (long) index);
	if (where == 0)
		entry = (GeneralPt*) shape->AddEntry();
	else if (where == 1)
		entry = (GeneralPt*) shape->GetEntry(index);
	else
		return false;
	if (entry == nil)
		return false;
	if (index == 0)
		flag10 = 0;
	entry->fPt.x = pt.x;
	entry->fPt.y = pt.y;
	entry->fControl = control;
	entry->f09 = flag9;
	entry->f0a = flag10;
	return true;
}


/*------------------------------------------------------------------------------
	T h e   c o r n e r s
------------------------------------------------------------------------------*/

// ROM 0x002134a8 PlaceAfter__FPUlUlT2
// A new break put into the list just after `after` (the list is 29 long;
// the last falls off).  ==> whether `after` was there.
Boolean
PlaceAfter(uint32_t* breaks, uint32_t after, uint32_t index)
{
	for (ULong i = 0; i < 29; i++)
	{
		if (breaks[i] == after)
		{
			for (ULong j = 29; i + 1 < j; j--)
				breaks[j] = breaks[j - 1];
			breaks[i + 1] = index;
			return true;
		}
	}
	return false;
}


// ROM 0x00213064 RLineOut2__FP6FPointPcPUllUlT5
// The points from `first` to `last` split at the one that strays farthest
// from the chord between them - above or below it, whichever is farther -
// when the spread either side is more than a ninth of the chord (held
// between the minimum and maximum tolerances) or the path is more than
// 1.38 times the chord; and so on down each half, twenty deep at most.
// Each end of a run that is not split is marked 2 in `marks` (1 for a run
// of under two points).
void
RLineOut2(FPoint* pts, char* marks, uint32_t* breaks, long depth, ULong first, ULong last)
{
	char mark = 1;
	if (last - first >= 2)
	{
		mark = 2;
		if (depth <= 20)
		{
			FPoint a = pts[first];
			FPoint b = pts[last];
			long chord = CheapDistPoint(&a, &b);
			// ROM BUG (fixed): chord is 16.16, so chord * 138 overflows a
			// 32-bit word once the chord passes about 237 pixels (a tall
			// shape's diagonal, on a 320x480 screen) and the limit wraps to
			// rubbish, often negative.  The ARM wraps silently; so does
			// this.  The fix works the product out in 64 bits.
			long pathLimit = RomBugFixed()
				? (long) ((int64_t) chord * 138 / 100)
				: (long) (int32_t) ((uint32_t) chord * 138u) / 100;
			long tolerance = chord / 9;
			if (tolerance < gPixMinRLineOutTolerance)
				tolerance = gPixMinRLineOutTolerance;
			else if (gPixMaxRLineOutTolerance < tolerance)
				tolerance = gPixMaxRLineOutTolerance;
			Fixed dx = a.x - b.x;
			Fixed dy = a.y - b.y;
			Fixed adx = (dx < 0) ? -dx : dx;
			Fixed ady = (dy < 0) ? -dy : dy;
			// the chord as y = slope x + c (or x = slope y + c when it is
			// steep), and each point's offset from it as its own c
			Boolean shallow = adx > ady;
			Fixed slope, c;
			if (shallow)
			{
				slope = FixedDivide(dy, dx);
				c = a.y - FixedMultiply(slope, a.x);
			}
			else
			{
				slope = FixedDivide(dx, dy);
				c = a.x - FixedMultiply(slope, a.y);
			}
			Fixed most = c, least = c;
			ULong mostAt = first, leastAt = first;
			long path = 0;
			for (ULong i = first + 1; i <= last; i++)
			{
				Fixed v = shallow ? pts[i].y - FixedMultiply(slope, pts[i].x)
								  : pts[i].x - FixedMultiply(slope, pts[i].y);
				if (v > most)
				{
					most = v;
					mostAt = i;
				}
				else if (v < least)
				{
					least = v;
					leastAt = i;
				}
				path += CheapDistPoint(&pts[i - 1], &pts[i]);
			}
			if (most - least > tolerance || path > pathLimit)
			{
				ULong at = (most - c > -(least - c)) ? mostAt : leastAt;
				if (first < at && at < last)
				{
					PlaceAfter(breaks, (uint32_t) first, (uint32_t) at);
					RLineOut2(pts, marks, breaks, depth + 1, first, at);
					RLineOut2(pts, marks, breaks, depth + 1, at, last);
					return;
				}
			}
		}
	}
	marks[last] = mark;
	marks[first] = mark;
}


// ROM 0x00213314 RSmallDists__FP6FPointUlP3Run
// The runs of samples closer together than the small distance - at least
// gSmpMinSmallDistRun long - noted, ended by a -1 pair.  ==> the whole
// length of the stroke, which is left in gGSInkLength.
//
// ROM BUG (fixed): the average step is worked out so that a stroke whose
// points are unevenly spaced has its runs thrown away (a -2 at the start of
// the first), and then the -2 is written whatever it came to, so FindCubic1
// never sees a run of small steps.  The fix writes it only when the
// average says so.
long
RSmallDists(FPoint* pts, ULong last, Run* runs)
{
	if (last == 0)
		return 0;
	Boolean inRun = false;
	long count = 0;
	long total = 0;
	for (ULong i = 1; i <= last; i++)
	{
		long step = CheapDistPoint(&pts[i - 1], &pts[i]);
		total += step;
		if (gPixMaxSmallDist < step)
		{
			if (inRun)
			{
				inRun = false;
				if ((ULong) gSmpMinSmallDistRun <= (i - 1) - (ULong) runs[count].fStart)
				{
					runs[count].fEnd = (int32_t) (i - 1);
					count++;
				}
			}
		}
		else if (!inRun && count < 29)
		{
			inRun = true;
			runs[count].fStart = (int32_t) (i - 1);
		}
	}
	if (inRun && (ULong) gSmpMinSmallDistRun <= last - (ULong) runs[count].fStart)
	{
		runs[count].fEnd = (int32_t) last;
		count++;
	}
	runs[count].fEnd = -1;
	runs[count].fStart = -1;
	long average = (long) ((ULong) total / last);
	if (gPixMaxAvgLenForSmallDists < average || average < gPixMinAvgLenForSmallDists)
		runs[0].fStart = -2;
	if (!RomBugFixed())
		runs[0].fStart = -2;
	return total;
}


// ROM 0x00213444 RLineOut__FP6FPointPcPUlP3RunlUlT6
Boolean
RLineOut(FPoint* pts, char* marks, uint32_t* breaks, Run* runs, long depth, ULong first, ULong last)
{
	gGSInkLength = RSmallDists(pts, last, runs);
	RLineOut2(pts, marks, breaks, depth, first, last);
	return true;
}


// ROM 0x00214d7c DeleteStrokes__FUlcPUlP6FPointPcT3
// `count` corners taken out in front of `index` (the points, their marks
// and their breaks moved down).  The moves take two entries more than
// there are, which is why the working block's layout matters.
void
DeleteStrokes(ULong index, char count, uint32_t* n, FPoint* keys, char* kinds, uint32_t* breaks)
{
	long moved = (long) (*n - index) + 2;
	long to = (long) index - (UByte) count;
	MoveBlock(keys + index, keys + to, moved * (long) sizeof(FPoint));
	MoveBlock(kinds + index, kinds + to, moved);
	MoveBlock(breaks + index, breaks + to, moved * (long) sizeof(uint32_t));
	*n -= (UByte) count;
}


// ROM 0x00214b88 Collapser__FPUlP6FPointPcT1
// Corners closer together than the collapse size merged - at the ends the
// end is kept, in the middle whichever turns less goes - and then corners
// the line turns less than 15 degrees at dropped (the one before them
// becoming a smooth kind, 1).
void
Collapser(uint32_t* n, FPoint* keys, char* kinds, uint32_t* breaks)
{
	if (*n == 2)
		return;
	for (ULong i = 1; i < *n; )
	{
		ULong next = i;
		FPoint* here = &keys[i];
		FPoint* before = here - 1;
		if ((ULong) CheapDistPoint(here, before) <= (ULong) gPixMaxCollapseSize)
		{
			Boolean dropBefore = false;
			if (i < *n - 1)
			{
				if (i > 1)
				{
					long into = PtsToAngle(before, here - 2, 0x10000);
					long mid = PtsToAngle(here, before, 0x10000);
					long outOf = PtsToAngle(here + 1, here, 0x10000);
					long turnBefore = mid - into;
					long turnAfter = outOf - mid;
					NORMD(&turnBefore);
					NORMD(&turnAfter);
					if (((turnBefore < 0) ? -turnBefore : turnBefore) <= ((turnAfter < 0) ? -turnAfter : turnAfter))
						dropBefore = true;
				}
				if (!dropBefore)
					i++;
			}
			next = i - 1;
			DeleteStrokes(i, 1, n, keys, kinds, breaks);
		}
		i = next + 1;
	}
	long angle = PtsToAngle(&keys[1], &keys[0], 0x10000);
	for (ULong i = 2; i < *n; )
	{
		long nextAngle = PtsToAngle(&keys[i], &keys[i - 1], 0x10000);
		long turn = nextAngle - angle;
		NORMD(&turn);
		ULong next = i;
		if (((turn < 0) ? -turn : turn) < 0xf0000)
		{
			kinds[i - 2] = 1;
			next = i - 1;
			DeleteStrokes(i, 1, n, keys, kinds, breaks);
			nextAngle = PtsToAngle(&keys[next], &keys[next - 1], 0x10000);
		}
		i = next + 1;
		angle = nextAngle;
	}
}


/*------------------------------------------------------------------------------
	T h e   c u r v e s
------------------------------------------------------------------------------*/

// ROM 0x0021418c CheckSmooth__F6FPointN31
// Whether the join between two pieces is a smooth bend rather than a
// kink: the stroke's own directions either side (`before`, `after`)
// within 100 degrees, and either a piece shorter than the kink distance or
// the turn under 26 degrees - or, under 64 degrees, the corners' own
// directions (`chordBefore`, `chordAfter`) within a sixth of it of the
// stroke's.
Boolean
CheckSmooth(FPoint before, FPoint after, FPoint chordBefore, FPoint chordAfter)
{
	long aBefore = PtsToAngle(&before, &ptZero, 0x10000);
	long aAfter = PtsToAngle(&after, &ptZero, 0x10000);
	long aChordBefore = PtsToAngle(&chordBefore, &ptZero, 0x10000);
	long aChordAfter = PtsToAngle(&chordAfter, &ptZero, 0x10000);
	long lenBefore = CheapDistPoint(&chordBefore, &ptZero);
	long lenAfter = CheapDistPoint(&chordAfter, &ptZero);
	long turn = Delta(aBefore, aAfter);
	if (turn > 0x640000)
		return false;
	if (lenBefore < gPixMinKinkDist || lenAfter < gPixMinKinkDist || turn < 0x1a0000)
		return true;
	if (turn <= 0x400000)
	{
		long slop = turn / 6;
		if (slop < Delta(aBefore, aChordBefore))
			return true;
		if (slop < Delta(aAfter, aChordAfter))
			return true;
	}
	return false;
}


// ROM 0x00213f4c TVStrHead__FUlP6FPointP9SplineSeg
// A straight segment's start tangent: along it.
void
TVStrHead(ULong i, FPoint* keys, SplineSeg* segs)
{
	segs[i].fT0.x = keys[i + 1].x - keys[i].x;
	segs[i].fT0.y = keys[i + 1].y - keys[i].y;
}


// ROM 0x00213f78 TVStrTail__FUlP6FPointT2PcPUlP9SplineSegT4l
// The tangents where a straight segment (i-1) meets the next: along the
// straight one, and at a smooth join the next one starts that way too (a
// kink, flag 1); otherwise the next starts the way the stroke goes over
// the first half of it.  `smalls` is what the runs of small steps said
// about the join (2: they cover it, so CheckSmooth decides).
void
TVStrTail(ULong i, FPoint* pts, FPoint* keys, char* kinds, uint32_t* breaks,
		  SplineSeg* segs, char* flags, long smalls)
{
	Fixed dx = keys[i].x - keys[i - 1].x;
	Fixed dy = keys[i].y - keys[i - 1].y;
	char flag = 0;
	if (kinds[i] == 1)
	{
		segs[i - 1].fT1.y = dy;
		segs[i - 1].fT1.x = dx;
	}
	else
	{
		ULong at = breaks[i];
		// ROM BUG (fixed): at the stroke's last corner (FindCubic1 calls this
		// for i = n - 1 after a straight last segment) breaks[i + 1] is past
		// the corners - the 0xffffffff FindKeyPoints fills the table with -
		// so the "middle" is some 2^31 points on.  On the ARM the address
		// pts + mid * 8 wraps at 32 bits and lands on point mid mod 2^29,
		// inside the stroke, and the tangent comes out of a point a little
		// before the start; a 64-bit host would read 16 GB away and fall
		// over (a long rising line on the reMarkable did, in the shape
		// recogniser).  The index is wrapped as the ARM's address is
		// (DEVIATION in form only: the same point is read).  The fix: at
		// the last corner (no corner after it, the fill in breaks[i + 1])
		// there is no segment after to start, so the segment before ends
		// along its chord, as at a straight corner, and the join is no
		// kink (as FindCubic1 marks the last corner).
		if (RomBugFixed() && breaks[i + 1] == 0xffffffff)
		{
			segs[i - 1].fT1.y = dy;
			segs[i - 1].fT1.x = dx;
			flags[i] = 0;
			return;
		}
		ULong32 mid32 = (ULong32) at + (((ULong32) breaks[i + 1] - (ULong32) at) >> 1);
		long mid = (long) (mid32 & 0x1fffffff);
		Fixed sx = pts[mid].x - pts[at].x;
		Fixed sy = pts[mid].y - pts[at].y;
		FPoint chord = { dx, dy };
		FPoint next = { keys[i + 1].x - keys[i].x, keys[i + 1].y - keys[i].y };
		FPoint stroke = { sx, sy };
		if (smalls != 1 && (smalls != 2 || CheckSmooth(chord, stroke, chord, next)))
		{
			segs[i - 1].fT1.y = dy;
			segs[i - 1].fT1.x = dx;
			segs[i].fT0 = segs[i - 1].fT1;
			flag = 1;
		}
		else
		{
			segs[i - 1].fT1.y = dy;
			segs[i - 1].fT1.x = dx;
			segs[i].fT0.y = sy;
			segs[i].fT0.x = sx;
		}
	}
	flags[i] = flag;
}


// ROM 0x00213df0 TVSplEnds__FUcUlP6FPointT3PcPUlP9SplineSegT5
// The tangent at a curve's free end, the way the stroke leaves it: from
// the end to the first point more than a third of the segment away (at
// the start), or from the last such point to the end (at the end).  The
// end of a single segment, or of one whose start is a kink, is straight.
void
TVSplEnds(UByte atEnd, ULong i, FPoint* pts, FPoint* keys, char* kinds, uint32_t* breaks,
		  SplineSeg* segs, char* flags)
{
	ULong from = breaks[i];
	ULong to = breaks[i + 1];
	FPoint* key = &keys[i];
	long reach = CheapDistPoint(key + 1, key) / 3;
	FPoint* tangent;
	if (atEnd == 0)
	{
		ULong stop = to - 1;
		to = from;
		do
		{
			to++;
			if (reach < CheapDistPoint(&pts[from], &pts[to]))
				break;
		}
		while (stop != to);
		tangent = &segs[i].fT0;
	}
	else
	{
		if (i == 0 || flags[i] == 0)
		{
			kinds[i] = 1;
			segs[i].fT1.x = key[1].x - keys[i].x;
			segs[i].fT1.y = key[1].y - key->y;
			segs[i].fT0 = segs[i].fT1;
			return;
		}
		ULong stop = from + 1;
		from = to;
		do
		{
			from--;
			if (reach < CheapDistPoint(&pts[from], &pts[to]))
				break;
		}
		while (stop != from);
		tangent = &segs[i].fT1;
	}
	tangent->x = pts[to].x - pts[from].x;
	tangent->y = pts[to].y - pts[from].y;
}


// ROM 0x00213c68 TVSplStr__FUlP6FPointT2PcPUlP9SplineSegT4l
// The tangents where a curve (i-1) meets a straight segment: a kink when
// the runs say so (or CheckSmooth says it is not smooth) - the curve
// arriving the way the stroke does, the straight one leaving along
// itself; otherwise both along the straight one (flag 1).
void
TVSplStr(ULong i, FPoint* pts, FPoint* keys, char* kinds, uint32_t* breaks,
		 SplineSeg* segs, char* flags, long smalls)
{
	ULong at = breaks[i];
	long back = (long) (at - ((at - breaks[i - 1]) >> 1));
	Fixed sx = pts[at].x - pts[back].x;
	Fixed sy = pts[at].y - pts[back].y;
	Fixed nx = keys[i + 1].x - keys[i].x;
	Fixed ny = keys[i + 1].y - keys[i].y;
	char flag;
	Boolean kink = smalls == 1;
	if (!kink && smalls == 2)
	{
		FPoint stroke = { sx, sy };
		FPoint next = { nx, ny };
		FPoint chord = { keys[i].x - keys[i - 1].x, keys[i].y - keys[i - 1].y };
		kink = !CheckSmooth(stroke, next, chord, next);
	}
	if (kink)
	{
		if (i != 1 && flags[i - 1] != 0)
		{
			segs[i - 1].fT1.y = sy;
			segs[i - 1].fT1.x = sx;
		}
		else
		{
			kinds[i - 1] = 1;
			segs[i - 1].fT1.x = keys[i].x - keys[i - 1].x;
			segs[i - 1].fT1.y = keys[i].y - keys[i - 1].y;
			segs[i - 1].fT0 = segs[i - 1].fT1;
		}
		segs[i].fT0.y = ny;
		segs[i].fT0.x = nx;
		flag = 0;
	}
	else
	{
		segs[i - 1].fT1.y = ny;
		segs[i - 1].fT1.x = nx;
		segs[i].fT0 = segs[i - 1].fT1;
		flag = 1;
	}
	flags[i] = flag;
}


// ROM 0x0021399c TVSplSpl__FUlP6FPointT2PcPUlP9SplineSegT4l
// The tangents where two curves meet at corner i.  At a kink each takes
// the way the stroke goes over the half of it nearest the corner.  At a
// smooth join both take the direction the stroke goes through the corner
// (over a quarter of each side, at least a point); and if that turns more
// than 110 degrees from the curve's start, the curve (i-1) is given the
// midpoint of its side and a direction to it instead.
void
TVSplSpl(ULong i, FPoint* pts, FPoint* keys, char* kinds, uint32_t* breaks,
		 SplineSeg* segs, char* flags, long smalls)
{
	ULong at = breaks[i];
	ULong after = breaks[i + 1] - at;
	ULong before = at - breaks[i - 1];
	long back = (long) (at - (before >> 1));
	FPoint* half = &pts[back];
	Fixed bx = pts[at].x - half->x;
	Fixed by = pts[at].y - half->y;
	long ahead = (long) (at + (after >> 1));
	Fixed ax = pts[ahead].x - pts[at].x;
	Fixed ay = pts[ahead].y - pts[at].y;
	Boolean kink = smalls == 1;
	if (!kink && smalls == 2)
	{
		FPoint stroke = { bx, by };
		FPoint strokeAfter = { ax, ay };
		FPoint chord = { keys[i].x - keys[i - 1].x, keys[i].y - keys[i - 1].y };
		FPoint next = { keys[i + 1].x - keys[i].x, keys[i + 1].y - keys[i].y };
		kink = !CheckSmooth(stroke, strokeAfter, chord, next);
	}
	if (kink)
	{
		if (i != 1 && flags[i - 1] != 0)
		{
			segs[i - 1].fT1.y = by;
			segs[i - 1].fT1.x = bx;
		}
		else
		{
			kinds[i - 1] = 1;
			segs[i - 1].fT1.x = keys[i].x - keys[i - 1].x;
			segs[i - 1].fT1.y = keys[i].y - keys[i - 1].y;
			segs[i - 1].fT0 = segs[i - 1].fT1;
		}
		segs[i].fT0.y = ay;
		segs[i].fT0.x = ax;
		flags[i] = 0;
		return;
	}
	FPoint across = { keys[i + 1].x - keys[i - 1].x, keys[i + 1].y - keys[i - 1].y };
	CheapDistPoint(&across, &ptZero);			// (worked out and not used)
	ULong fore = after >> 2;
	ULong aft = before >> 2;
	if (fore < 2)
		fore = 1;
	if (aft < 2)
		aft = 1;
	segs[i - 1].fT1.x = pts[at + fore].x - pts[at - aft].x;
	segs[i - 1].fT1.y = pts[at + fore].y - pts[at - aft].y;
	long startAngle = PtsToAngle(&segs[i - 1].fT0, &ptZero, 0x10000);
	long turn = PtsToAngle(&segs[i - 1].fT1, &ptZero, 0x10000) - startAngle;
	NORMD(&turn);
	if (turn < 0)
		turn = -turn;
	segs[i].fT0 = segs[i - 1].fT1;
	if (turn > 0x6e0000)
	{
		// ROM BUG (fixed): the curve is ended at the stroke's half-way
		// point, and its end *tangent* is given that point too - a
		// position where a direction was meant.  The fix gives it the
		// direction to that point from the curve's start, as the comment
		// above the function describes.
		segs[i - 1].fP1 = *half;
		if (RomBugFixed())
		{
			segs[i - 1].fT1.x = half->x - segs[i - 1].fP0.x;
			segs[i - 1].fT1.y = half->y - segs[i - 1].fP0.y;
		}
		else
			segs[i - 1].fT1 = *half;
	}
	flags[i] = 1;
}


// ROM 0x0021358c FindCubic1__FPUlP6FPointT2PcT1P3RunP9SplineSegT4
// The corners made into segments, and each given its tangents by what is
// either side of it: a straight segment (kind 1) or a curve, meeting a
// straight one or a curve.  The runs of small steps say, for each join,
// whether the stroke slowed there (1: a kink) or they cannot be trusted
// (2: CheckSmooth decides).  A smooth join between two curves that did
// not come out where it was expected is given a corner of its own, half
// way between (so a curve that turns too far is cut in two).
void
FindCubic1(uint32_t* n, FPoint* pts, FPoint* keys, char* kinds, uint32_t* breaks,
		   Run* runs, SplineSeg* segs, char* flags)
{
	flags[*n - 1] = 0;
	flags[0] = 0;
	long run = 0;
	long trusted = runs[0].fStart;
	for (ULong i = 0; i < *n - 1; i++)
	{
		SplineSeg* seg = &segs[i];
		FPoint* key = &keys[i];
		seg->fP0 = key[0];
		seg->fP1 = key[1];
		long smalls;
		if (trusted == -2)
			smalls = 2;
		else
		{
			smalls = 0;
			while (runs[run].fEnd >= 0 && (uint32_t) runs[run].fEnd < breaks[i + 1])
				run++;
			if (runs[run].fEnd >= 0 && (uint32_t) runs[run].fStart <= breaks[i + 1])
			{
				smalls = 1;
				run++;
			}
		}
		if (kinds[i] == 1)
		{
			if (i == 0 || kinds[i - 1] == 1)
				TVStrHead(i, keys, segs);
			TVStrTail(i + 1, pts, keys, kinds, breaks, segs, flags, smalls);
			continue;
		}
		if (i == 0)
			TVSplEnds(0, 0, pts, keys, kinds, breaks, segs, flags);
		if (*n - 2 == i)
		{
			TVSplEnds(1, i, pts, keys, kinds, breaks, segs, flags);
			continue;
		}
		ULong next = i + 1;
		if (kinds[next] == 1)
		{
			TVSplStr(next, pts, keys, kinds, breaks, segs, flags, smalls);
			continue;
		}
		TVSplSpl(next, pts, keys, kinds, breaks, segs, flags, smalls);
		if (flags[next] == 0)
			continue;
		// a smooth join whose tangents did not come out the same
		if (seg[1].fT0.x == seg->fT1.x && seg[1].fT0.y == seg->fT1.y)
			continue;
		if (*n < 29)
		{
			// room for a corner half way between: everything after moves up
			for (ULong j = *n; next < j; j--)
			{
				keys[j] = keys[j - 1];
				kinds[j] = kinds[j - 1];
				breaks[j] = breaks[j - 1];
				segs[j] = segs[j - 1];
				flags[j] = flags[j - 1];
			}
			kinds[next] = 2;
			breaks[next] = (breaks[next + 1] + breaks[i]) >> 1;
			seg[1].fP0 = seg->fP1;
			key[1] = seg[1].fP0;
			seg[1].fP1 = key[2];
			seg->fT1.x = seg[1].fP1.x - seg->fP0.x;
			seg->fT1.y = seg[1].fP1.y - seg->fP0.y;
			seg[1].fT0 = seg->fT1;
			seg[1].fT1 = seg[2].fT0;
			flags[next] = 1;
			(*n)++;
			i = next;
		}
		else
		{
			seg->fT1 = seg[1].fT0;
			seg->fP1 = seg[1].fP0;
		}
	}
}


/*------------------------------------------------------------------------------
	J o i n i n g   t h e   s t r o k e s
------------------------------------------------------------------------------*/

// ROM 0x00214dec Connect__FUllT2P9SplineSegPcT5PUlT4N25
// One stroke's `n` corners (as segments, `segs`) joined onto the shape's
// running list (`shape`, `count` of them).  `mode` says what the stroke
// is: 0 one of the shape's own, 1 a shape on the page its first end met,
// 2 one its last end met, 3 the first stroke of all.  Where the two meet
// at the same angle (within 35 degrees) the join is smoothed - the
// shape's last segment and the stroke's first become one where both are
// straight, or the stroke's first segments that bend the other way are
// passed over - and otherwise the stroke's start is put at the shape's
// end.
void
Connect(ULong n, long mode, long connections, SplineSeg* segs, char* segFlags, char* kinds,
		uint32_t* count, SplineSeg* shape, char* shapeFlags, char* shapeKinds)
{
	ULong skipped = 0;
	if (*count != 0)
	{
		long last = (long) *count - 2;
		FPoint* lastEnd = &shape[last].fP1;
		FPoint end = *lastEnd;
		FPoint start = segs[0].fP0;
		FPoint* lastTangent = &shape[last].fT1;
		FPoint endTangent = *lastTangent;
		FPoint* firstTangent = &segs[0].fT0;
		FPoint startTangent = *firstTangent;
		if (!SameAngle(*lastTangent, *firstTangent, 0x230000))
		{
			if (mode == 0 || n > 2 || connections < 2)
				segs[0].fP0 = *lastEnd;
			else if (mode == 2)
				return;
		}
		else
		{
			Boolean bothStraight = false;
			if (mode == 0)
			{
				shapeFlags[*count - 1] = 1;
				bothStraight = shapeKinds[*count - 2] == 1;
			}
			if (bothStraight && kinds[0] == 1 && mode == 0)
			{
				// two straight pieces in line: the shape's last runs on to
				// the stroke's first end
				*lastEnd = segs[0].fP1;
				Fixed tx = segs[0].fP1.x - shape[last].fP0.x;
				shape[last].fT1.x = tx;
				shape[last].fT0.x = tx;
				Fixed ty = segs[0].fP1.y - shape[last].fP0.y;
				shape[last].fT1.y = ty;
				shape[last].fT0.y = ty;
				if (last != 0 && shapeFlags[last] != 0)
					shape[last - 1].fT1 = shape[last].fT0;
				if (n > 2 && segFlags[1] != 0)
					segs[1].fT0 = shape[last].fT0;
				n--;
				skipped = 1;
			}
			else if (mode == 2)
			{
				// the shape on the page the last end met: the shape's own
				// last segments that bend the other way are dropped
				long k = last;
				while (k >= 0 && segFlags[k] != 0)
				{
					FPoint p0 = shape[k].fP0;
					FPoint t0 = shape[k].fT0;
					long a = PtsToAngle(&t0, &ptZero, 0x10000);
					long b = PtsToAngle(&startTangent, &ptZero, 0x10000);
					long chord = PtsToAngle(&start, &p0, 0x10000);
					long d1 = a - chord;
					long d2 = b - chord;
					NORMD(&d1);
					NORMD(&d2);
					long s1 = Signum(d1);
					long s2 = Signum(d2);
					if (s1 == 0 || s2 == 0 || s1 != s2)
					{
						*count -= (uint32_t) (last - k);
						last = k;
						break;
					}
					k--;
				}
				shape[last].fP1 = segs[0].fP0;
				shape[last].fT1 = *firstTangent;
				return;
			}
			else
			{
				// the stroke's own first segments that bend the other way
				// are passed over
				ULong k = 0;
				ULong stop = n - 2;
				while (k <= stop && segFlags[k + 1] != 0)
				{
					FPoint p1 = segs[k].fP1;
					FPoint t1 = segs[k].fT1;
					long a = PtsToAngle(&endTangent, &ptZero, 0x10000);
					long b = PtsToAngle(&t1, &ptZero, 0x10000);
					long chord = PtsToAngle(&p1, &end, 0x10000);
					long d1 = a - chord;
					long d2 = b - chord;
					NORMD(&d1);
					NORMD(&d2);
					long s1 = Signum(d1);
					long s2 = Signum(d2);
					if (s1 == 0 || s2 == 0 || s1 != s2)
					{
						n -= k;
						skipped = k;
						break;
					}
					k++;
				}
				segs[skipped].fP0 = *lastEnd;
				segs[skipped].fT0 = *lastTangent;
			}
		}
	}
	Boolean fresh = *count == 0 || mode == 3;
	if (fresh)
	{
		shapeFlags[0] = 0;
		*count = 0;
	}
	long base = (long) fresh - 2;
	for (ULong i = 1; i < n; i++)
	{
		long from = (long) (i + skipped);
		shape[(long) *count + base + (long) i] = segs[from - 1];
		shapeFlags[(long) *count + base + (long) i + 1] = segFlags[from];
		shapeKinds[(long) *count + base + (long) i] = kinds[from - 1];
	}
	if (mode == 0 || mode == 3)
	{
		if (*count != 0)
			(*count)--;
		uint32_t was = *count;
		*count = was + (uint32_t) n;
		shapeFlags[was + n - 1] = 0;
	}
}


/*------------------------------------------------------------------------------
	T h e   o u t l i n e
------------------------------------------------------------------------------*/

// ROM 0x00214834 DoConic__FP9SplineSeg
// The control point of the conic through a segment: where its two
// tangents meet - unless they meet behind the start, or further from
// either end than the segment is long, in which case the start's tangent
// is made that long and its end is the control point.
FPoint
DoConic(SplineSeg* seg)
{
	SameAngle(seg->fT0, seg->fT1, 0xa0000);		// (worked out and not used)
	FPoint along;
	along.x = seg->fT0.x + seg->fP0.x;
	along.y = seg->fP0.y + seg->fT0.y;
	FPoint back;
	back.x = seg->fP1.x + seg->fT1.x;
	back.y = seg->fP1.y + seg->fT1.y;
	FPoint meet;
	IntersectLine(&meet, &seg->fP0, &along, &seg->fP1, &back);
	Boolean behind = Signum(seg->fT0.x) != Signum(meet.x - seg->fP0.x)
				  || Signum(seg->fT0.y) != Signum(meet.y - seg->fP0.y);
	long length = CheapDistPoint(&seg->fP1, &seg->fP0);
	if (!behind)
	{
		long fromStart = CheapDistPoint(&meet, &seg->fP0);
		long fromEnd = CheapDistPoint(&meet, &seg->fP1);
		long limit = ShiftLeft(length, 1) >> 1;
		if (!(fromStart > limit && fromEnd > limit))
			return meet;
	}
	ScaleToSize(&seg->fT0, length);
	meet.x = seg->fT0.x + seg->fP0.x;
	meet.y = seg->fP0.y + seg->fT0.y;
	return meet;
}


// ROM 0x002149c8 DoConicInfl__FUcP9SplineSegT2
// A control point for one half of a curve cut at its inflection `split`
// (its point and its tangent there): the first half runs from the
// segment's start to the split, the second from the split to its end.
// As DoConic, but a meeting behind or too far away gives the half's start
// itself.
FPoint
DoConicInfl(UByte first, SplineSeg* seg, SplineSeg* split)
{
	FPoint from, to, t0, t1;
	if (first == 0)
	{
		from = split->fP0;
		to = seg->fP1;
		t0 = split->fT0;
		t1 = seg->fT1;
	}
	else
	{
		from = seg->fP0;
		to = split->fP0;
		t0 = seg->fT0;
		t1 = split->fT0;
	}
	SameAngle(t0, t1, 0xa0000);					// (worked out and not used)
	FPoint along = { from.x + t0.x, from.y + t0.y };
	FPoint back = { to.x + t1.x, to.y + t1.y };
	FPoint meet;
	IntersectLine(&meet, &from, &along, &to, &back);
	Boolean behind = Signum(t0.x) != Signum(meet.x - from.x)
				  || Signum(t0.y) != Signum(meet.y - from.y);
	long length = CheapDistPoint(&from, &to);
	if (!behind)
	{
		long fromStart = CheapDistPoint(&meet, &from);
		long fromEnd = CheapDistPoint(&meet, &to);
		long limit = ShiftLeft(length, 1) >> 1;
		if (!(fromStart > limit && fromEnd > limit))
			return meet;
	}
	return from;
}


// ROM 0x00214344 FindInflection__FUcP9SplineSegUlPcT4T2
// What segment i's curve does: whether its two tangents bend the same way
// about its chord (a plain conic, 0 or 1), the other way (an inflection:
// -1, with `split` given its point and tangent - or 3 or 4 when the
// inflection is so near an end that the tangent there is moved instead),
// whether it is straight (2, or -2 when it is straight but bends against
// the one before), or has no tangent at all (10); 5 is a conic that bends
// against the last one.  The way the last curve bent is remembered.
long
FindInflection(UByte cut, SplineSeg* segs, ULong i, char* flags, char* kinds, SplineSeg* split)
{
	char* flag = &flags[i];
	SplineSeg* seg = &segs[i];
	if (kinds[i] != 1)
	{
		FPoint p0 = seg->fP0;
		FPoint p1 = seg->fP1;
		FPoint t0 = seg->fT0;
		FPoint t1 = seg->fT1;
		if (t0.x == 0 && t0.y == 0)
			return 10;
		if (t1.x == 0 && t1.y == 0)
			return 10;
		long a0 = PtsToAngle(&t0, &ptZero, 0x10000);
		long a1 = PtsToAngle(&t1, &ptZero, 0x10000);
		long chord = PtsToAngle(&p1, &p0, 0x10000);
		long d0 = a0 - chord;
		long d1 = a1 - chord;
		NORMD(&d0);
		NORMD(&d1);
		long s0 = Signum(d0);
		long s1 = Signum(d1);
		if (s0 != 0 && s1 != 0)
		{
			if (s0 != s1)
			{
				// one conic: does it bend the same way as the last?
				if (i != 0 && flags[i] != 0 && s0 != gGSLastBend && !gGSBendKnown)
				{
					gGSLastBend = s0;
					gGSBendKnown = false;
					return 0;
				}
				gGSLastBend = s0;
				gGSBendKnown = false;
				return 5;
			}
			// an inflection
			gGSBendKnown = true;
			FPoint half;
			half.x = (p1.x - p0.x) / 2;
			half.y = (p1.y - p0.y) / 2;
			gGSLastBend = s0;
			long length = CheapDistPoint(&half, &ptZero);
			Fixed ratio = FixedDivide(d0, d1);
			if (cut != 0)
			{
				ScaleToSize(&t0, length);
				ScaleToSize(&t1, length);
				FPoint mean;
				mean.x = (t0.x + t1.x) / 2;
				mean.y = (t0.y + t1.y) / 2;
				Reflect(&mean, &half);
				split->fT0.x = (FixedMultiply(0x18000, half.x) + mean.x) / 2;
				split->fT0.y = (FixedMultiply(0x18000, half.y) + mean.y) / 2;
				Fixed y;
				if (ratio < 0x10000)
				{
					if (flags[i] == 0 && SameAngle(t0, split->fT0, 0xa0000))
					{
						seg->fT0 = split->fT0;
						gGSBendKnown = false;
						return 3;
					}
					Fixed f = (ratio > 0x1000) ? ratio : 0x1000;
					split->fP0.x = FixedMultiply(f, half.x) + p0.x;
					y = FixedMultiply(f, half.y) + p0.y;
				}
				else
				{
					ratio = FixedDivide(0x10000, ratio);
					if (flag[1] == 0 && SameAngle(t1, split->fT0, 0xa0000))
					{
						seg->fT1 = split->fT0;
						gGSBendKnown = false;
						return 4;
					}
					Fixed f = (ratio > 0x1000) ? ratio : 0x1000;
					split->fP0.x = p1.x - FixedMultiply(f, half.x);
					y = p1.y - FixedMultiply(f, half.y);
				}
				split->fP0.y = y;
				return -1;
			}
			if (flag[1] != 0 && (flags[i] == 0 || ratio > 0x10000))
				return 1;
			gGSLastBend = -s1;
			gGSBendKnown = false;
			return 0;
		}
	}
	// straight (or a tangent along its chord)
	gGSBendKnown = true;
	if (flags[i] != 0 && flag[1] != 0)
	{
		if (seg[1].fT0.x == 0 && seg[1].fT0.y == 0)
		{
			gGSBendKnown = true;
			return 10;
		}
		long a = PtsToAngle(&seg[1].fT0, &ptZero, 0x10000);
		long d = a - PtsToAngle(&seg[1].fP1, &seg[1].fP0, 0x10000);
		NORMD(&d);
		if (Signum(d) != gGSLastBend)
			return -2;
	}
	return 2;
}


// ROM 0x0021534c MeetEnds__FUlT1P9SplineSegP7TDArray
// A closed shape's last point joined to its first.  When the last segment
// arrives at the same angle as the first leaves (within 35 degrees), the
// outline's last points that are curves bending the other way are taken
// back off, the last segment is made to end at the start, and either a
// control point is put in for it (DoConic) or the start is marked as a
// smooth join; either way the start is written again at the end.
Boolean
MeetEnds(ULong n, ULong last, SplineSeg* segs, TDArray* shape)
{
	FPoint start = segs[0].fP0;
	FPoint end = segs[n - 2].fP1;
	FPoint startTangent = segs[0].fT0;
	FPoint endTangent = segs[n - 2].fT1;
	UByte smooth = 0;
	UByte flip = 0;
	long lastSeg = (long) n - 2;
	long k = lastSeg;
	ULong at = last;
	if (SameAngle(startTangent, endTangent, 0x230000))
	{
		smooth = 1;
		FPoint p1 = segs[0].fP1;
		long a0 = PtsToAngle(&startTangent, &ptZero, 0x10000);
		long d = a0 - PtsToAngle(&p1, &start, 0x10000);
		NORMD(&d);
		long sign0 = Signum(d);
		long state = 1;
		for (;;)
		{
			at--;
			GeneralPt* entry = (GeneralPt*) shape->GetEntry(at);
			if (entry->fControl == 0)
				break;
			at--;
			entry = (GeneralPt*) shape->GetEntry(at);
			end = entry->fPt;
			endTangent = segs[k--].fT0;
			long aT = PtsToAngle(&endTangent, &ptZero, 0x10000);
			long aS = PtsToAngle(&start, &end, 0x10000);
			long sEnd = aT - aS;
			long sStart = a0 - aS;
			NORMD(&sStart);
			NORMD(&sEnd);
			long gA = Signum(sStart);
			long gB = Signum(sEnd);
			if (gA == 0)
			{
				if (gB == 0)
					break;
			}
			else if (gB != 0 && gA != gB)
			{
				flip = (sign0 != gB) ? 1 : 0;
				state = 0;
				break;
			}
			if (entry->f09 == 0)
			{
				state = 2;
				break;
			}
		}
		ULong next = at + 1;
		shape->CutToIndex(next);
		if (lastSeg != k)
			k++;
		segs[k].fP1 = start;
		segs[k].fT1 = startTangent;
		if (state != 0)
		{
			if (sign0 == 0)
			{
				InitGeneralPt(shape, 0, segs[k].fP0);
				return true;
			}
			smooth = 0;
		}
		else
		{
			FPoint control = DoConic(&segs[k]);
			if (!SetGeneralPt(shape, next, control, 1, 1, 0))
			{
				at++;
				goto write;
			}
			at = next;
		}
		GeneralPt* first = (GeneralPt*) shape->GetEntry(0);
		first->f09 = 1;
		first->f0a = flip;
		at++;
	}
write:
	SetGeneralPt(shape, at, start, 0, smooth, flip);
	return true;
}


/*------------------------------------------------------------------------------
	F i n d K e y P o i n t s
------------------------------------------------------------------------------*/

// The working block FindKeyPoints keeps everything in (the ROM's 0xa54-byte
// 'FKPA' handle).  Its layout is the ROM's to the byte, because the moves
// DeleteStrokes makes run two entries past the end of each array.
struct KeyPointWork
{
	FPoint		fKeys[30];			// +0x000  one stroke's corners
	char		fKinds[30];			// +0x0f0  what each is (1 straight on, 2 a corner)
	UByte		fPad10e[2];
	uint32_t	fBreaks[30];		// +0x110  where each is in the stroke's points
	Run			fRuns[30];			// +0x188  the stroke's runs of small steps
	SplineSeg	fSegs[30];			// +0x278  one stroke's segments
	char		fSegFlags[30];		// +0x638  whether each join is smooth
	UByte		fPad656[2];
	SplineSeg	fShape[30];			// +0x658  the whole shape's segments
	char		fShapeFlags[30];	// +0xa18
	char		fShapeKinds[30];	// +0xa36
};

// ROM 0x0021227c FindKeyPoints__FP17TGeneralShapeUnitP6GSTypePUl
// The shape's strokes fitted and its outline written (see the top of the
// file).  A shape of one point is a dot (kNone); a single stroke smaller
// than the low blob size is a curve drawn as it was written; one smaller
// than the high blob size keeps its points within half its size again of
// its box.  A shape that goes off the screen, or any failure, is nothing
// (15, scored 10000).
void
FindKeyPoints(TGeneralShapeUnit* unit, long* type, ULong* score)
{
	TDArray* shape = nil;
	Boolean failed = false;
	gGSOffScreen = false;
	gGSClampRect.top = -1000;
	long subs = unit->SubCount();
	Boolean closed = *type == kShapeClosedCurve;
	gGSClosed = closed;
	TStroke* strokes[8];
	uint32_t counts[8];
	uint32_t most = 0;
	for (long i = 0; i < subs; i = (i + 1) & 0xff)
	{
		TStrokeUnit* sub = (TStrokeUnit*) unit->GetSub(unit->fGroupInfo->fOrder[i]);
		strokes[i] = sub->fStroke;
		counts[i] = (uint32_t) strokes[i]->Count();
		if (most < counts[i])
			most = counts[i];
	}
	Handle ptsHandle = MakeHandle((long) most * (long) sizeof(FPoint));
	NameHandle(ptsHandle, 'PTS ');
	Handle marksHandle = MakeHandle((long) most);
	NameHandle(marksHandle, 'RIGD');
	Handle workHandle = MakeHandle(sizeof(KeyPointWork));
	NameHandle(workHandle, 'FKPA');
	uint32_t segCount = 0;			// sp+0x78
	uint32_t keyCount = 0;			// sp+0x7c
	if (ptsHandle == nil || marksHandle == nil || workHandle == nil)
		failed = true;
	else
	{
		KeyPointWork* work = (KeyPointWork*) *workHandle;
		for (ULong i = 0; i < 30; i++)
			work->fShapeFlags[i] = (char) 0xff;
		long mode = 0;				// r6: 0 the shape's own, 1 the page's at the start, 2 at the end, 3 after the start
		ULong sub = 0;				// r8
		TGeneralShapeUnit* source = unit;
		if (subs > 0)
		{
			long lastSub = subs - 1;
			Boolean checkStart = true;
			for (;;)
			{
				TStroke* stroke;
				uint32_t count;
				SamplePt* samples;
				if (checkStart && unit->fGroupInfo->fEnds[0].fKind == 2)
				{
					source = (TGeneralShapeUnit*) unit->fGroupInfo->fEnds[0].fUnit;
					mode = 1;
					sub = 0xff;
				}
				checkStart = false;
				if (mode == 1 || mode == 2)
				{
					stroke = ((TStrokeUnit*) source->GetSub(0))->fStroke;
					count = (uint32_t) stroke->Count();
					if (most < count)
					{
						most = count;
						if (ResizeHandle(ptsHandle, (long) most * (long) sizeof(FPoint)) != 0
						 || ResizeHandle(marksHandle, (long) most) != 0)
						{
							failed = true;
							break;
						}
					}
					samples = stroke->GetPoint(0);
				}
				else
				{
					source = unit;
					count = counts[sub];
					samples = strokes[sub]->GetPoint(0);
				}
				work = (KeyPointWork*) *workHandle;
				for (ULong i = 0; i < 30; i++)
				{
					work->fBreaks[i] = 0xffffffff;
					work->fRuns[i].fStart = -1;
					work->fRuns[i].fEnd = -1;
					work->fSegFlags[i] = (char) 0xff;
					work->fKinds[i] = (char) 0xff;
				}
				work->fBreaks[0] = 0;
				ULong lastPt = count - 1;
				work->fBreaks[1] = (uint32_t) lastPt;
				if (count >= 0x9c0 && count - 0x9c0 >= 4)
				{
					failed = true;
					break;
				}
				UByte order = (mode == 1 || mode == 2) ? source->fGroupInfo->fOrder[0]
													   : source->fGroupInfo->fOrder[sub];
				UByte reversed = source->fGroupInfo->fReversed[order];
				FPoint* pts = (FPoint*) *ptsHandle;
				char* marks = (char*) *marksHandle;
				for (ULong i = 0; i < count; i++)
				{
					marks[i] = 0;
					GetPoint(&samples[reversed == 0 ? i : lastPt - i], &pts[i]);
				}
				RLineOut(pts, marks, work->fBreaks, work->fRuns, 0, 0, lastPt);
				ULong k = 0;
				for (ULong i = 0; i < count; i++)
				{
					if (marks[i] == 0)
						continue;
					if (k >= 29)
					{
						failed = true;
						break;
					}
					work->fKeys[k] = pts[i];
					work->fKinds[k] = marks[i];
					if (mode == 1)
						work->fShapeKinds[k] = marks[i];
					k++;
				}
				if (failed)
					break;
				keyCount = (uint32_t) k;
				if (segCount + k > 30)
				{
					failed = true;
					break;
				}
				Collapser(&keyCount, work->fKeys, work->fKinds, work->fBreaks);
				if (mode == 1)
				{
					// the shape on the page the first end met starts the list
					FindCubic1(&keyCount, pts, work->fKeys, work->fShapeKinds, work->fBreaks,
							   work->fRuns, work->fShape, work->fShapeFlags);
					mode = 3;
					segCount = keyCount;
				}
				else
				{
					if (keyCount > 1)
					{
						FindCubic1(&keyCount, pts, work->fKeys, work->fKinds, work->fBreaks,
								   work->fRuns, work->fSegs, work->fSegFlags);
						if (mode != 2)
						{
							// a single stroke drawn small: a blob
							FRect box;
							((TUnit*) unit->GetSub(unit->fGroupInfo->fOrder[sub]))->GetBBox(&box);
							long height = box.bottom - box.top;
							long width = box.right - box.left;
							long size = (height > width) ? height : width;
							Fixed wiggle = FixedDivide(gGSInkLength, height + width);
							for (ULong i = 0; i < keyCount; i++)
							{
								if (work->fSegFlags[i] == 0)
									continue;
								if (wiggle > 0x30000)
								{
									failed = true;
									break;
								}
								if (subs == 1 && size < gPixHighBlobThreshold)
								{
									if (size < gPixLowBlobThreshold)
										*type = kShapeCurve;
									else
									{
										gGSClampRect.top = box.top - (height >> 1);
										gGSClampRect.bottom = box.bottom + (height >> 1);
										gGSClampRect.left = box.left - (width >> 1);
										gGSClampRect.right = box.right + (width >> 1);
									}
								}
								break;
							}
							if (failed)
								break;
						}
					}
					Connect(keyCount, mode, unit->fGroupInfo->fConnections, work->fSegs,
							work->fSegFlags, work->fKinds, &segCount, work->fShape,
							work->fShapeFlags, work->fShapeKinds);
					if (mode == 2)
						break;
					mode = 0;
					if (lastSub == (long) sub && unit->fGroupInfo->fEnds[1].fKind == 2)
					{
						// and the shape on the page the last end met ends it
						source = (TGeneralShapeUnit*) unit->fGroupInfo->fEnds[1].fUnit;
						mode = 2;
						sub = (sub - 1) & 0xff;
					}
				}
				sub = (sub + 1) & 0xff;
				if ((long) sub >= subs)
					break;
				if (mode == 0 && sub == 0)
					checkStart = true;
			}
		}
		if (!failed)
		{
			shape = TDArray::Make(sizeof(GeneralPt), 0);
			if (shape == nil)
				failed = true;
			else
			{
				unit->SetGeneralShape(shape);
				*score = 1000;
				work = (KeyPointWork*) *workHandle;
				if (segCount == 1)
				{
					InitGeneralPt(shape, 0, work->fKeys[0]);
					*type = kShapeNone;
				}
				else
				{
					if (segCount == 2)
						closed = false;
					if (*type == kShapeCurve)
					{
						// a blob: the first stroke's own points
						for (ULong i = 0; i < counts[0]; i++)
						{
							FPoint pt;
							strokes[0]->GetFPoint((long) i, &pt);
							InitGeneralPt(shape, i, pt);
						}
					}
					else
					{
						DeleteHandle(marksHandle);
						marksHandle = nil;
						DeleteHandle(ptsHandle);
						ptsHandle = nil;
						work = (KeyPointWork*) *workHandle;
						ULong j = 0;
						if (!gCurveFlag)
						{
							// corners only
							for (; j < segCount - 1; j++)
								InitGeneralPt(shape, j, work->fShape[j].fP0);
							InitGeneralPt(shape, j, work->fShape[j - 1].fP1);
							if (closed)
								MeetEnds(segCount, segCount - 1, work->fShape, shape);
						}
						else if (!SetGeneralPt(shape, 0, work->fShape[0].fP0, 0, work->fShapeFlags[0], 0))
							failed = true;
						else
						{
							for (ULong i = 0; !failed && i < segCount - 1; i++)
							{
								SplineSeg split;
								work = (KeyPointWork*) *workHandle;
								SplineSeg* seg = &work->fShape[i];
								long what = FindInflection(1, work->fShape, i, work->fShapeFlags,
														   work->fShapeKinds, &split);
								switch (what)
								{
								case -2:
									j++;
									if (!SetGeneralPt(shape, j, seg->fP1, 0, work->fShapeFlags[i + 1], 1))
										failed = true;
									break;
								case -1:
								{
									j++;
									FPoint c = DoConicInfl(1, seg, &split);
									if (!SetGeneralPt(shape, j, c, 1, 1, 0))
										j--;
									j++;
									if (!SetGeneralPt(shape, j, split.fP0, 0, 1, 1))
									{
										failed = true;
										break;
									}
									j++;
									c = DoConicInfl(0, seg, &split);
									if (!SetGeneralPt(shape, j, c, 1, 1, 0))
										j--;
									j++;
									if (!SetGeneralPt(shape, j, seg->fP1, 0, work->fShapeFlags[i + 1], 0))
										failed = true;
									break;
								}
								case 2:
									j++;
									if (!SetGeneralPt(shape, j, seg->fP1, 0, work->fShapeFlags[i + 1], 0))
										failed = true;
									break;
								case 0:
									if (!SetGeneralPt(shape, j, seg->fP0, 0, work->fShapeFlags[i], 1))
									{
										failed = true;
										break;
									}
									// and on, as for a plain conic
								case 1: case 3: case 4: case 5:
								{
									j++;
									FPoint c = DoConic(seg);
									if (!SetGeneralPt(shape, j, c, 1, 1, 0))
										j--;
									j++;
									if (!SetGeneralPt(shape, j, seg->fP1, 0, work->fShapeFlags[i + 1], what == 1))
										failed = true;
									break;
								}
								default:
									failed = true;
									break;
								}
							}
							if (!failed && closed)
								MeetEnds(segCount, j, work->fShape, shape);
						}
					}
				}
			}
		}
	}
	if (shape != nil)
		shape->Compact();
	if (failed || gGSOffScreen)
	{
		*type = kShapeNothing;
		*score = 10000;
	}
	if (ptsHandle != nil)
		DeleteHandle(ptsHandle);
	if (marksHandle != nil)
		DeleteHandle(marksHandle);
	if (workHandle != nil)
		DeleteHandle(workHandle);
}
