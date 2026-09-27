/*
	File:		LowPict.cpp

	Contains:	The cursive reader's low level: Pict, the first of
				AnalyzeLowData's element finders, and the stroke
				descriptions (`_SDS_TYPE`) and measurements it is made of.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	A stroke description is what `iMostFarDoubleSide` makes of a piece
	of the trace between two points: its box, the slope of its chord,
	the points furthest from the chord on either side and how far they
	are, the longer of the two (the piece's bend, `crook`, in hundredths
	of the chord), and the length along the trace.  Pict describes every
	stroke that way, decides what it is (a stick, a dot, a hatch - the
	bar of a t or the cross of an x - or a letter's body) and marks the
	special elements the later passes turn into xrs.

	The measurements are arithmetic on the trace's shorts; as elsewhere
	in the low level, a product that can overflow a word wraps as the
	ARM's does (`LMul`).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "LowLevel.h"
#include "ParaGraph.h"
#include "XrDomains.h"
#include <string.h>


// ROM 0x00309818 DistanceSquare__FiT1PsT3
// The square of the distance between points i and j.
long
DistanceSquare(long i, long j, short* x, short* y)
{
	long dx = x[i] - x[j];
	long dy = y[i] - y[j];
	return LAdd(LMul(dx, dx), LMul(dy, dy));
}


// ROM 0x00305f90 Distance8__FiN31
// An octagonal distance: the larger of the two sides, or two thirds of
// their sum when that is more.
long
Distance8(long x1, long y1, long x2, long y2)
{
	long dx = x1 - x2;
	if (dx < 0)
		dx = -dx;
	long dy = y1 - y2;
	if (dy < 0)
		dy = -dy;
	long d = ((dx + dy) * 2 + 1) / 3;
	if (dx < dy)
		dx = dy;
	if (d <= dx)
		d = dx;
	return d;
}


// ROM 0x00305df4 CurvMeasure__FPsT1iN23
// How far point k (the point furthest from the chord, when k is not
// given) is from the chord from i to j, in hundredths of the chord's
// length (1000 for a chord of no length or a bend over ten times it),
// negative when the bend is to the chord's left.
long
CurvMeasure(short* x, short* y, long i, long j, long k)
{
	long d2 = DistanceSquare(i, j, x, y);
	long xi = x[i];
	long xj = x[j];
	long yi = y[i];
	long yj = y[j];
	if (d2 == 0)
		return 1000;
	if (k <= 0)
		k = iMostFarFromChord(x, y, i, j);
	long q = QDistFromChord(xi, yi, xj, yj, x[k], y[k]);
	long r;
	if (q / 1000 > d2)
		r = 1000;
	else
		r = LAdd(LMul(q, 100), d2 >> 1) / d2;
	Boolean negate;
	if (xi == xj)
		negate = (yi < yj && x[k] < xi) || (yi > yj && x[k] > xi);
	else
	{
		long dx = xj - xi;
		long dy = yj - yi;
		long v = LAdd(LMul(dx, y[k]) - LMul(dy, x[k]), LMul(dy, xi) - LMul(dx, yi));
		Boolean s1 = (dx >= 0);
		if (!s1)
			v = -v;
		Boolean s2 = (v >= 0);
		negate = (s1 == s2);
	}
	if (negate)
		r = -r;
	return (short) r;
}


// ROM 0x00306e2c HeightInLine__FsP8low_type
// Which of the thirteen bands between the line thresholds a height is
// in (1 above the highest, 13 below the lowest).
long
HeightInLine(short y, low_type* low)
{
	const short* t = low->fThresh;
	if (y <= t[3])
	{
		if (y <= t[0])
			return 1;
		if (y > t[1])
			return (y > t[2]) ? 4 : 3;
		return 2;
	}
	if (y <= t[6])
	{
		if (y > t[4])
			return (y > t[5]) ? 7 : 6;
		return 5;
	}
	if (y > t[8])
	{
		if (y <= t[9])
			return 10;
		if (y > t[10])
			return (y > t[11]) ? 13 : 12;
		return 11;
	}
	return (y > t[7]) ? 9 : 8;
}


// The middle of the run of equal values starting at best, not past
// iEnd (iyMin, iyMax, iYup_range and iYdown_range end the same way).
static long
MiddleOfRun(short* a, long best, long iEnd)
{
	long k = best;
	while (a[k] == a[best] && a[k] != -1)
		k++;
	long mid = (best + k - 1) >> 1;
	if (iEnd < mid)
		mid = iEnd;
	return mid;
}


// ROM 0x0030707c iyMin__FiT1Ps
// The highest point from iBeg to iEnd (the middle of a run of them);
// -1 for none.
long
iyMin(long iBeg, long iEnd, short* y)
{
	long best = -1;
	Boolean found = false;
	for (long i = iBeg; i <= iEnd; i++)
		if (y[i] != -1 && (!found || y[i] < y[best]))
		{
			found = true;
			best = i;
		}
	return found ? MiddleOfRun(y, best, iEnd) : -1;
}


// ROM 0x003070e8 iyMax__FiT1Ps
// The lowest point; -1 for none.
long
iyMax(long iBeg, long iEnd, short* y)
{
	long best = -1;
	Boolean found = false;
	for (long i = iBeg; i <= iEnd; i++)
		if (y[i] != -1 && (!found || y[best] < y[i]))
		{
			found = true;
			best = i;
		}
	return found ? MiddleOfRun(y, best, iEnd) : -1;
}


// ROM 0x00307438 iYup_range__FPsiT2
// The highest point; 0x7fff for none.
long
iYup_range(short* y, long iBeg, long iEnd)
{
	long v = 0x7fff;
	long best = 0;
	for (long i = iBeg; i <= iEnd; i++)
		if (y[i] != -1 && y[i] < v)
		{
			v = y[i];
			best = i;
		}
	return (v != 0x7fff) ? MiddleOfRun(y, best, iEnd) : 0x7fff;
}


// ROM 0x003074a8 iYdown_range__FPsiT2
// The lowest point; 0x7fff for none (a trace wholly at y 0 counting as
// none).
long
iYdown_range(short* y, long iBeg, long iEnd)
{
	long v = 0;
	long best = 0;
	for (long i = iBeg; i <= iEnd; i++)
		if (y[i] != -1 && v < y[i])
		{
			v = y[i];
			best = i;
		}
	return (v != 0) ? MiddleOfRun(y, best, iEnd) : 0x7fff;
}


// ROM 0x00307614 iClosestToXY__FiT1PsT3sT5
// The point from iBeg to iEnd nearest (px, py); the first of equals.
long
iClosestToXY(long iBeg, long iEnd, short* x, short* y, short px, short py)
{
	long dx = x[iBeg] - px;
	long dy = y[iBeg] - py;
	long best = LAdd(LMul(dy, dy), LMul(dx, dx));
	for (long i = iBeg + 1; i <= iEnd; i++)
	{
		dx = x[i] - px;
		dy = y[i] - py;
		long d = LAdd(LMul(dx, dx), LMul(dy, dy));
		if (d < best)
		{
			iBeg = i;
			best = d;
		}
	}
	return iBeg;
}


// ROM 0x00307db0 R_ClosestToLine__FPsT1P13PS_point_typeP12POINTS_GROUPT1
// The point of the stroke nearest p, into *at; ==> how far it is
// (0x7fff when that overflows a short).
long
R_ClosestToLine(short* x, short* y, PS_point_type* p, POINTS_GROUP* group, short* at)
{
	long i = group->iBeg;
	long dx = x[i] - p->x;
	long dy = y[i] - p->y;
	long best = i;
	long d = LAdd(LMul(dy, dy), LMul(dx, dx));
	while (++i <= group->iEnd)
	{
		dx = x[i] - p->x;
		dy = y[i] - p->y;
		long e = LAdd(LMul(dx, dx), LMul(dy, dy));
		if (e < d)
		{
			best = i;
			d = e;
		}
	}
	*at = (short) best;
	if (d < 0)
		return 0;
	long shift = 0;
	for (; 0x7fff < d; d >>= 2)
		shift = (short) (shift + 1);
	d = (short) (HWRMathISqrt((short) d) << (shift & 0xff));
	if (d < 0)
		d = 0x7fff;
	return d;
}


#pragma mark The stroke descriptions

// ROM 0x0032f8bc CreateSDS__FP8low_types
// Room for n stroke descriptions in the array fSDS (which the caller
// provides) controls.  ==> whether there was the memory.
Boolean
CreateSDS(low_type* low, short n)
{
	_SDS_CONTROL_TYPE* control = low->fSDS;
	control->pSDS = (_SDS_TYPE*) HWRMemoryAlloc(n * sizeof(_SDS_TYPE));	// (0x2c each, the same on the host)
	if (control->pSDS == nil)
		return false;
	control->sizeSDS = n;
	control->lenSDS = 0;
	return true;
}


// ROM 0x0032f91c DestroySDS__FP8low_type
// The array given back (the pointer left as it was) and marked destroyed.
void
DestroySDS(low_type* low)
{
	_SDS_CONTROL_TYPE* control = low->fSDS;
	if (control == nil)
		return;
	if (control->pSDS != nil)
		HWRMemoryFree((Ptr) control->pSDS);
	control->sizeSDS = 0;
	control->lenSDS = -2;
}


// ROM 0x0032f5e4 InitElementSDS__FP9_SDS_TYPE
// ROM QUIRK: clears 0x28 bytes of the 0x2c - the last word is left as
// it was (Init_SDS_Element clears the lot).
void
InitElementSDS(_SDS_TYPE* sds)
{
	memset(sds, 0, 0x28);
}


// ROM 0x0032eb24 Init_SDS_Element__FP9_SDS_TYPE
// ==> whether there was one.
Boolean
Init_SDS_Element(_SDS_TYPE* sds)
{
	if (sds != nil)
	{
		memset(sds, 0, sizeof(_SDS_TYPE));
		sds->mark = 0;
		sds->attr = 0;
		sds->iBeg = -2;
		sds->iEnd = -2;
	}
	return sds != nil;
}


// ROM 0x0032f84c NoteSDS__FP17_SDS_CONTROL_TYPEP9_SDS_TYPE
// A copy of sds added to the array; ==> 1, 0 when it is full (the last
// place is never used).
long
NoteSDS(_SDS_CONTROL_TYPE* control, _SDS_TYPE* sds)
{
	if (control->lenSDS < control->sizeSDS - 1)
	{
		control->pSDS[control->lenSDS] = *sds;
		control->lenSDS = (short) (control->lenSDS + 1);
		return 1;
	}
	return 0;
}


// ROM 0x0032f19c HordIntersectDetect__FP9_SDS_TYPEPsT2
// Whether the piece's own chord crosses the piece anywhere but at its
// two ends.
long
HordIntersectDetect(_SDS_TYPE* sds, short* x, short* y)
{
	long i = sds->iBeg;
	long j = sds->iEnd;
	short xa = x[i], ya = y[i], xb = x[j], yb = y[j];
	for (i += 2; i < j - 2; i++)
		if (is_cross(xa, ya, xb, yb, x[i], y[i], x[i + 1], y[i + 1]) != 0)
			return 1;
	return 0;
}


// ROM 0x0032eb80 iMostFarDoubleSide__FPsT1P9_SDS_TYPEN21Ui
// The piece from sds->iBeg to sds->iEnd described: its box, its chord's
// length and slope (hundredths of dy/dx; 0x7fff upright, 0 near level),
// the furthest point on each side of the chord and how far, the further
// of the two and the bend it makes (hundredths of the chord), whether
// the chord crosses the piece (attr 0x80, else 0x81) and, when length
// is asked for, its length along the trace and that in hundredths of
// the chord.  (*px, *py) is the foot on the chord of the further point.
// A piece whose ends are one point is described as nothing much.
// ==> 0
long
iMostFarDoubleSide(short* x, short* y, _SDS_TYPE* sds, short* px, short* py, ULong withLength)
{
	long iBeg = sds->iBeg;
	long iEnd = sds->iEnd;
	long x1 = x[iEnd];
	long y1 = y[iEnd];
	long x0 = x[iBeg];
	long y0 = y[iBeg];
	long dx = (short) (x1 - x0);
	long dy = (short) (y1 - y0);
	if (dx == 0 && dy == 0)
	{
		sds->slope = -2;
		sds->chord = 0;
		sds->iMax = (short) iBeg;
		sds->maxDist = -2;
		sds->crook = -2;
		sds->distA = 0;
		sds->iA = (short) iBeg;
		sds->distB = 0;
		sds->iB = (short) iBeg;
		*px = 0;
		*py = 0;
		return 0;
	}
	yMinMax(iBeg, iEnd, y, &sds->yMin, &sds->yMax);
	xMinMax(iBeg, iEnd, x, y, &sds->xMin, &sds->xMax);
	if (dx == 0)
		sds->slope = 0x7fff;
	else
	{
		sds->slope = (short) (LMul(dy, 100) / dx);
		long a = HWRAbs(sds->slope);
		if (a > 0x5dc)
			sds->slope = 0x7fff;
		else if (HWRAbs(sds->slope) < 4)
			sds->slope = 0;
	}

	// the furthest point on each side: A the negative side, B the other
	long c = LMul(dy, x0) - LMul(dx, y0);
	long iA = iBeg, iB = iBeg;
	long vMin = 0, vMax = 0;
	for (long i = iBeg + 1; i <= iEnd; i++)
	{
		long yi = y[i];
		if (yi == -1)
			continue;
		long v = LAdd(LMul(dx, yi) - LMul(dy, x[i]), c);
		Boolean minSide = (dy != 0) ? (v < 0) : (yi < y1);
		if (minSide)
		{
			if (v < vMin)
			{
				vMin = v;
				iA = i;
			}
		}
		else if (v > vMax)
		{
			vMax = v;
			iB = i;
		}
	}
	sds->distA = (short) HWRMathILSqrt(QDistFromChord(x0, y0, x1, y1, x[iA], y[iA]));
	sds->iA = (short) iA;
	sds->distB = (short) HWRMathILSqrt(QDistFromChord(x0, y0, x1, y1, x[iB], y[iB]));
	sds->iB = (short) iB;
	long len = HWRMathILSqrt(DistanceSquare(iBeg, iEnd, x, y));
	sds->chord = (short) len;

	// the feet of the two points on the chord
	long t = LAdd(LMul(dx, x[iB] - x0), LMul(dy, y[iB] - y0));
	short pxB = (short) (LMul(t, dx) / len / len + x0);
	short pyB = (short) (LMul(t, dy) / len / len + y0);
	t = LAdd(LMul(dx, x[iA] - x0), LMul(dy, y[iA] - y0));
	short pxA = (short) (LMul(t, dx) / len / len + x0);
	short pyA = (short) (LMul(t, dy) / len / len + y0);

	if (sds->distB >= sds->distA)
	{
		sds->iMax = sds->iB;
		sds->maxDist = sds->distB;
		long crook = LMul(sds->distB, 100) / sds->chord;
		sds->crook = (short) crook;
		if (crook >= 0x7fff)
			sds->crook = 0x7fff;
		*px = pxB;
		*py = pyB;
	}
	else
	{
		sds->iMax = sds->iA;
		sds->maxDist = sds->distA;
		long crook = LMul(sds->distA, 100) / sds->chord;
		sds->crook = (crook < 0x7fff) ? (short) crook : 0x7fff;
		*px = pxA;
		*py = pyA;
	}
	sds->attr = (sds->distB != 0 && sds->distA != 0 && HordIntersectDetect(sds, x, y) == 1) ? 0x80 : 0x81;
	sds->mark = 0;
	if (withLength != 0)
	{
		long length = 0;
		for (long i = iBeg; i < iEnd; i++)
			length += HWRMathILSqrt(DistanceSquare(i, i + 1, x, y));
		sds->length = length;
		sds->lengthRatio = (sds->chord == 0) ? 0x7fff : (short) (LMul(length, 100) / sds->chord);
	}
	return 0;
}


// ROM 0x00026338 CrookCalc__FP8low_typePsiT3
// The piece from i to j described; *maxDist is how far its furthest
// point is from the chord.  ==> its bend in hundredths of the chord,
// negative when the further point is on the chord's A side or level
// with it.
long
CrookCalc(low_type* low, short* maxDist, long i, long j)
{
	_SDS_TYPE sds;
	short px, py;
	InitElementSDS(&sds);
	sds.iBeg = (short) i;
	sds.iEnd = (short) j;
	iMostFarDoubleSide(low->fX, low->fY, &sds, &px, &py, 1);
	*maxDist = sds.maxDist;
	long crook = sds.crook;
	if (sds.distA <= sds.distB)
		crook = -crook;
	return crook;
}


#pragma mark The heights

// ROM 0x0032e65c BildHigh__FsT1Ps
// The eleven heights a stroke's top and bottom are placed against: the
// word's top and bottom (clamped to the normal line's) at each end, the
// line's five fixed heights (0x2796 to 0x27e6: the trace rescaled by
// transfrmN) in the middle, and the heights between.  ==> 1
long
BildHigh(short top, short bottom, short* h)
{
	h[0] = (top > 0x2746) ? 0x2746 : top;
	h[10] = (bottom < 0x2836) ? 0x2836 : bottom;
	h[1] = (short) ((h[0] + 0x2796) / 2);
	h[2] = (short) (h[0] / 5 + 0x1fab);
	h[3] = 0x2796;
	h[4] = 0x27a9;
	h[5] = 0x27be;
	h[6] = 0x27d1;
	h[7] = 0x27e6;
	h[8] = (short) (h[10] / 5 + 0x1feb);
	h[9] = (short) ((h[10] + 0x27e6) / 2);
	return 1;
}


// The band a height is in against the eleven heights: 9 at or above
// h[1], down to 0 at or above h[10]; one out of range is clamped (and
// says so).
static long
HeightBand(short* v, short* h, Boolean* inRange)
{
	if (h[0] > *v)
	{
		*inRange = false;
		*v = h[0];
		return 9;
	}
	if (h[1] >= *v)
		return 9;
	for (long k = 2; k <= 9; k++)
		if (h[k] >= *v)
			return 10 - k;
	if (h[10] < *v)
	{
		*inRange = false;
		*v = h[10];
	}
	return 0;
}


// ROM 0x0032e39c RelHigh__FPsiT2N31
// Where a piece of the trace reaches to against BildHigh's heights: its
// top into *topBand, its bottom into *bottomBand (9 highest, 0 lowest).
// ==> whether both were within the heights.
long
RelHigh(short* y, long i, long j, short* h, short* bottomBand, short* topBand)
{
	long lo, hi;
	if (i > j)
	{
		lo = j;
		hi = i;
	}
	else
	{
		lo = i;
		hi = j;
	}
	short yMin, yMax;
	yMinMax(lo, hi, y, &yMin, &yMax);
	Boolean inRange = true;
	*topBand = (short) HeightBand(&yMin, h, &inRange);
	*bottomBand = (short) HeightBand(&yMax, h, &inRange);
	return inRange;
}


#pragma mark A stroke's pieces

// ROM 0x0032f26c RareAngle__FP8low_typeP9SPEC_TYPET2Ps
// The corners of the stroke elem covers noted in list as angles (0x0b):
// a run of points where the trace, seen from each point up to two steps
// either way, turns sharply - a value under 16 (20 once a run has
// begun) on a scale where 100 is straight.  ==> 1, 0 when the list
// already has over 76 elements.
long
RareAngle(low_type* low, SPEC_TYPE* elem, SPEC_TYPE* list, short* count)
{
	short* x = low->fX;
	short* y = low->fY;
	long iBeg = elem->iBeg;
	long iEnd = elem->iEnd;
	Boolean found = false;
	long start = 0, last = 0;
	if (iBeg == iEnd)
		return 1;
	long threshold = 0x10;
	for (long i = iBeg + 1; i <= iEnd - 1; i++)
	{
		long lo = (i - 2 < iBeg) ? iBeg : i - 2;
		long hi = (iEnd < i + 2) ? iEnd : i + 2;
		long xi = x[i];
		long yi = y[i];
		long a = i, b = i;
		do
		{
			if (lo < a)
				a--;
			if (b < hi)
				b++;
			long dx1 = xi - x[a];
			long dy1 = yi - y[a];
			long dx2 = xi - x[b];
			long dy2 = yi - y[b];
			long v;
			if ((dx1 == 0 && dy1 == 0) || (dx2 == 0 && dy2 == 0))
				v = 100;
			else
			{
				long dot = LAdd(LMul(dy2, dy1), LMul(dx2, dx1));
				if (dot < 0)
				{
					long n1 = LAdd(LMul(dy1, dy1), LMul(dx1, dx1));
					long n2 = LAdd(LMul(dy2, dy2), LMul(dx2, dx2));
					long t;
					if (n1 < n2)
						t = LMul(dot, 100) / n1;
					else
					{
						t = LMul(dot, 100) / n2;
						n2 = n1;
					}
					v = (short) (LMul(dot, t) / n2);
				}
				else
					v = 0xf;
			}
			if (v < threshold)
			{
				last = i;
				if (!found)
				{
					found = true;
					threshold = 0x14;
					start = i;
				}
			}
		} while (lo < a || b < hi);
		if (found && last != i)
		{
			if (0x4c < *count)
				return 0;
			SPEC_TYPE angle;
			angle.mark = 0x0b;
			angle.code = 0;
			angle.attr = 0;
			angle.other = 0;
			angle.iBeg = (short) start;
			angle.iEnd = (short) last;
			angle.ipoint0 = (short) ((start + last) >> 1);
			angle.ipoint1 = -2;
			if (NoteSpecl(low, &angle, list, count, 0x50) == 0)
				return 1;
			found = false;
			threshold = 0x10;
		}
	}
	if (found)
	{
		SPEC_TYPE angle;
		angle.mark = 0x0b;
		angle.code = 0;
		angle.attr = 0;
		angle.other = 0;
		angle.iBeg = (short) start;
		angle.iEnd = (short) last;
		angle.ipoint0 = (short) ((start + last) >> 1);
		angle.ipoint1 = -2;
		NoteSpecl(low, &angle, list, count, 0x50);
	}
	return 1;
}


// ROM 0x0032d510 StrElements__FP8low_typeP9SPEC_TYPEPs
// The stroke elem covers described in the stroke descriptions: a head
// (0x10: the stroke's box; its own index at +0x12, the longest piece's
// index + 1 at +0x24, the whole length at +0x20, elem's code and attr
// at +0x26 and +0x28), a description of each piece between its ends and
// its corners (RareAngle) with each piece's share of the length at
// +0x28, and a tail (0x20).  elem's ipoint1 is set to where its
// descriptions start.  ==> 0, 1 for no memory or no room.
long
StrElements(low_type* low, SPEC_TYPE* elem, short* /*heights*/)
{
	_SDS_CONTROL_TYPE* control = low->fSDS;
	_SDS_TYPE* all = control->pSDS;
	long first = control->lenSDS;
	_SDS_TYPE* head = &all[first];
	short* x = low->fX;
	short* y = low->fY;
	long iBeg = elem->iBeg;
	long iEnd = elem->iEnd;
	short code = elem->code;
	short attr = elem->attr;
	long result = 0;
	SPEC_TYPE e;
	_SDS_TYPE sds;
	short count = 0;
	// DEVIATION: room for 80 elements of the host's size (the ROM's 0x640)
	SPEC_TYPE* list = (SPEC_TYPE*) HWRMemoryAlloc(80 * sizeof(SPEC_TYPE));
	if (list == nil)
		return 1;
	control->f02 = control->lenSDS;
	elem->ipoint1 = control->f02;
	InitSpeclElement(&e);
	e.mark = 0x10;
	e.iBeg = e.iEnd = e.ipoint0 = (short) iBeg;
	e.ipoint1 = -2;
	if (NoteSpecl(low, &e, list, &count, 0x50) == 0)
		goto failed;
	RareAngle(low, elem, list, &count);
	InitSpeclElement(&e);
	e.mark = 0x20;
	e.iBeg = e.iEnd = e.ipoint0 = (short) iEnd;
	e.ipoint1 = -2;
	if (NoteSpecl(low, &e, list, &count, 0x50) == 0 || !Init_SDS_Element(&sds))
		goto failed;
	sds.iBeg = (short) iBeg;
	sds.iEnd = (short) iEnd;
	sds.attr = 0x10;
	sds.mark = 0;
	xMinMax(iBeg, iEnd, x, y, &sds.xMin, &sds.xMax);
	yMinMax(iBeg, iEnd, y, &sds.yMin, &sds.yMax);
	if (NoteSDS(control, &sds) == 0 || !Init_SDS_Element(&sds))
		goto failed;
	{
		long best = 0, bestIndex = 0, total = 0;
		for (long k = 0; k < count - 1; k++)
		{
			SPEC_TYPE* p = &list[k];
			SPEC_TYPE* q = &list[k + 1];
			long a = (p->iBeg + 1 < p->iEnd) ? p->ipoint0 : p->iEnd;
			long b = (q->iBeg + 1 < q->iEnd) ? q->ipoint0 : q->iBeg;
			sds.iBeg = (short) a;
			sds.iEnd = (short) b;
			short px, py;
			iMostFarDoubleSide(x, y, &sds, &px, &py, 1);
			long length = 0;
			for (long i = a; i < b; i++)
				length += HWRMathILSqrt(DistanceSquare(i, i + 1, x, y));
			sds.length = length;
			if (best < sds.chord)
			{
				best = sds.chord;
				bestIndex = k;
			}
			sds.lengthRatio = (sds.chord == 0) ? 0x7fff : (short) (LMul(length, 100) / sds.chord);
			total += length;
			if (NoteSDS(control, &sds) == 0)
			{
				result = 1;
				break;
			}
		}
		long n = control->lenSDS;
		for (long j = first + 1; j < n; j++)
			all[j].share = (total != 0) ? (short) (LMul(all[j].length, 100) / total) : 100;
		head->slope = (short) first;
		head->crook = (short) (bestIndex + 1);
		head->length = total;
		head->lengthRatio = code;
		head->share = attr;
	}
	if (!Init_SDS_Element(&sds))
		goto failed;
	sds.iBeg = sds.iEnd = (short) iEnd;
	sds.attr = 0x20;
	sds.mark = 0;
	if (NoteSDS(control, &sds) != 0)
		goto done;
failed:
	result = 1;
done:
	HWRMemoryFree((Ptr) list);
	return result;
}


// ROM 0x000263d0 DownStepOK__FP8low_typeP9SPEC_TYPET2
// Whether b's point is more than 7 below where a ends.
Boolean
DownStepOK(low_type* low, SPEC_TYPE* a, SPEC_TYPE* b)
{
	short* y = low->fY;
	return 7 < y[b->ipoint0] - y[a->iEnd];
}


// ROM 0x0002620c ArcTurnsOK__FP8low_type9_ARC_TYPEiT3
// Whether the piece from i to j (or one a point longer at either end,
// where there is one) bends by more than 7 hundredths the way kind says
// (6 one way, 7 the other).
Boolean
ArcTurnsOK(low_type* low, long kind, long i, long j)
{
	short* y = low->fY;
	long sign = (kind == 6) ? -1 : ((kind == 7) ? 1 : 0);
	short maxDist;
	long v = LMul(CrookCalc(low, &maxDist, i, j), sign);
	long best = (-0x7fff < v) ? v : -0x7fff;
	if (y[i - 1] != -1)
	{
		v = LMul(CrookCalc(low, &maxDist, i - 1, j), sign);
		if (v > best)
			best = v;
	}
	if (y[j + 1] != -1)
	{
		v = LMul(CrookCalc(low, &maxDist, i, j + 1), sign);
		if (v > best)
			best = v;
	}
	if (y[i - 1] != -1 && y[j + 1] != -1)
	{
		v = LMul(CrookCalc(low, &maxDist, i - 1, j + 1), sign);
		if (v > best)
			best = v;
	}
	return 7 < best;
}


// ROM 0x00025ef0 SlashArcs__FP8low_typeiT2
// Within the stroke from iBeg to iEnd, a low (0x31) followed by a high
// (0x33) that make one arc between the stroke's start (1) and its end
// (3) - the end more than 7 below the low, the two over 10 apart and
// each bending the right way - is marked as an arc (5) from the low's
// point to the high's, with its size in `other` (1, and 2 a small one
// high up, 4 a middling one, 8 a big one).  ==> 0, 1 for no room.
long
SlashArcs(low_type* low, long iBeg, long iEnd)
{
	SPEC_TYPE* specl = low->fSpecl;
	short* x = low->fX;
	short* y = low->fY;
	long n = low->fLenSpecl;
	for (long i = 0; i < n; i++)
	{
		SPEC_TYPE* lowElem = &specl[i];
		SPEC_TYPE* highElem;
		SPEC_TYPE* start;
		SPEC_TYPE* end;
		SPEC_TYPE* p;
		SPEC_TYPE arc;
		short maxDist;
		long crook, band;
		if (iEnd < lowElem->iBeg)
			return 0;
		if (iBeg > lowElem->iEnd)
			continue;
		if (!(lowElem->mark == 0x31 && specl[i + 1].mark == 0x33))
			continue;
		// back to the stroke's start, not past a 3 or the stroke's 0x10
		for (start = lowElem; ; start = start->prev)
		{
			UByte m = start->mark;
			if (m == 3 || m == 0x10)
				goto next;
			if (m == 1)
				break;
		}
		highElem = &specl[i + 1];
		// a high before the low, within the stroke
		for (p = lowElem; ; p = p->prev)
		{
			if (p->mark == 0x33)
				break;
			if (p->mark == 0x10)
				goto next;
		}
		// on to the stroke's end, not past a 1 or 0x20
		for (end = highElem; ; end = end->next)
		{
			UByte m = end->mark;
			if (m == 3)
				break;
			if (m == 1 || m == 0x20)
				goto next;
		}
		// the first end after the start must be that one
		for (p = start; p->mark != 3 && p != end; p = p->next)
			;
		if (p != end)
			continue;
		if (lowElem->ipoint0 >= highElem->ipoint0)
			continue;
		if (!DownStepOK(low, lowElem, end))
			continue;
		if (HWRMathILSqrt(DistanceSquare(lowElem->ipoint0, highElem->ipoint0, x, y)) <= 10)
			continue;
		if (!ArcTurnsOK(low, 6, lowElem->iBeg, lowElem->iEnd) || !ArcTurnsOK(low, 7, highElem->iBeg, highElem->iEnd))
			continue;
		// ROM QUIRK: the arc's code and attr are whatever was on the
		// stack; the host has them nought (DEVIATION)
		InitSpeclElement(&arc);
		arc.other = 0;
		crook = HWRAbs(CrookCalc(low, &maxDist, start->ipoint0, end->ipoint0));
		band = HeightInLine(y[end->ipoint0], low);
		if (crook < 10 && maxDist < 10)
			continue;
		if (crook < 14 && maxDist < 21)
			arc.other |= (band < 11) ? 2 : 4;
		else if (crook > 18 && maxDist > 18)
			arc.other |= 8;
		else
			arc.other |= 4;
		arc.mark = 5;
		arc.iBeg = lowElem->ipoint0;
		arc.iEnd = highElem->ipoint0;
		arc.ipoint0 = start->ipoint0;
		arc.ipoint1 = end->ipoint0;
		arc.other |= 1;
		if (MarkSpecl(low, &arc) == 1)
			return 1;
	next:
		;
	}
	return 0;
}


// ROM 0x0032dd54 FieldSt__FP9_SDS_TYPEsN22PsN25
// The limits a stick is judged against, for a stroke reaching from
// band top down to band bottom, out of the trained tables, then
// adjusted by how upright and how bent piece k is and by its share of
// the stroke: the largest bend (*maxA), the largest bend in hundredths
// of the chord (*maxCR) and the shortest length (*minL).  ==> 1
long
FieldSt(_SDS_TYPE* sds, short bottom, short top, short k, short* maxA, short* maxCR, short* minL)
{
	long col = bottom;
	long row = top;
	_SDS_TYPE* piece = &sds[k];
	short slope = (short) HWRAbs(piece->slope);
	long crook = piece->crook;
	long a = maxA_H_end[row * 10 + col];
	long cr = maxCR_H_end[row * 10 + col];
	long l = minL_H_end[row * 10 + col];
	if (cr >= 0)
	{
		if (slope < 10)
		{
			l = (short) (l * 85 / 100);
			cr = (short) (cr * 125 / 100);
		}
		else if (slope < 20)
		{
			l = (short) (l * 95 / 100);
			cr = (short) (cr * 115 / 100);
		}
		else if (slope > 40 && row < 7 && col <= 6)
			cr = (short) (cr * 85 / 100);
		if (row > 6 && col > 6)
		{
			if (crook < 5)
			{
				a = (short) (a * 115 / 100);
				cr = (short) (cr * 130 / 100);
			}
			else if (crook < 10)
			{
				a = (short) (a * 110 / 100);
				cr = (short) (cr * 120 / 100);
			}
			else if (crook < 15)
			{
				a = (short) (a * 110 / 100);
				cr = (short) (cr * 110 / 100);
			}
		}
		if (col < 6)
		{
			long share = piece->share;
			if (share < 25)
				cr = (short) (cr * 40 / 100);
			else if (share < 35)
				cr = (short) (cr * 45 / 100);
			else if (share < 50)
				cr = (short) (cr * 50 / 100);
			else if (share < 60)
				cr = (short) (cr * 80 / 100);
			if (a > 0x54)
				a = 0x54;
		}
	}
	*maxA = (short) a;
	*maxCR = (short) cr;
	*minL = (short) l;
	return 1;
}


// ROM 0x0032dfd4 Dot__FP8low_typeP9SPEC_TYPEP9_SDS_TYPE
// Whether the stroke elem covers is a dot: one point, or a box smaller
// than the trained limits for where it lies (a little larger for the
// word's last stroke, smaller in a field of figures) that is not an
// upright stick.  A dot is marked 8 at its middle.  ==> 8 a dot, 0 not.
long
Dot(low_type* low, SPEC_TYPE* elem, _SDS_TYPE* head)
{
	long iBeg = elem->iBeg;
	long iEnd = elem->iEnd;
	long code = (short) elem->code;
	long attr = (short) elem->attr;
	if (iBeg != iEnd)
	{
		long best = head->crook;
		long width = (short) (head->xMax - head->xMin);
		long height = (short) (head->yMax - head->yMin);
		long maxX = maxX_H_end[code * 10 + attr];
		long maxY = maxY_H_end[code * 10 + attr];
		long last = low->fII - 2;
		if (last == iEnd)
		{
			if (code <= 4)
			{
				maxX = (short) (maxX * 120 / 100);
				maxY = (short) (maxY * 125 / 100);
			}
			else if (code <= 6)
			{
				maxX = (short) (maxX * 110 / 100);
				maxY = (short) (maxY * 115 / 100);
			}
		}
		if (RCGetH(low->rc, 0x92) == 2 && attr >= 4)
		{
			maxY = (short) (maxY * 80 / 100);
			maxX = (short) (maxX * 80 / 100);
		}
		if (!(height < maxY && width < maxX))
			return 0;
		if (iEnd < last)
		{
			// the longest piece: a steep one with most of the length (or
			// in the first stroke's descriptions) and tall enough is a stick
			_SDS_TYPE* piece = &head[best];
			static const short kShare[4] = { 90, 80, 70, 50 };
			static const short kSlope[4] = { 160, 170, 200, 500 };
			static const short kHeight[4] = { 60, 60, 60, 70 };
			for (long t = 0; t < 4; t++)
				if (code <= 5
				 && (head->slope == 1 || piece->share > kShare[t])
				 && HWRAbs(piece->slope) > kSlope[t]
				 && maxY * kHeight[t] / 100 <= height)
					return 0;
		}
	}
	elem->mark = 8;
	elem->ipoint0 = elem->ipoint1 = (short) ((iBeg + iEnd) >> 1);
	return 8;
}


#pragma mark Pieces of the trace against each other

// The boxes of two pieces of the trace overlap.
static Boolean
BoxesMeet(short aXMin, short aXMax, short aYMin, short aYMax, short bXMin, short bXMax, short bYMin, short bYMax)
{
	return aXMin <= bXMax && aXMax >= bXMin && aYMax >= bYMin && aYMin <= bYMax;
}


// ROM 0x0032cdb4 Close_To__FP8low_typeP12POINTS_GROUPT2
// Whether a's box meets b's; if it does, a is narrowed by halves to a
// piece of three points or fewer that still meets it (stopping when
// both halves or neither do), else a is made -2, -2.  ==> 1 they meet,
// 0 not.
long
Close_To(low_type* low, POINTS_GROUP* a, POINTS_GROUP* b)
{
	short* x = low->fX;
	short* y = low->fY;
	long iBeg = a->iBeg;
	long iEnd = a->iEnd;
	short aYMin, aYMax, bYMin, bYMax, aXMin, aXMax, bXMin, bXMax;
	long result;
	yMinMax(iBeg, iEnd, y, &aYMin, &aYMax);
	yMinMax(b->iBeg, b->iEnd, y, &bYMin, &bYMax);
	xMinMax(iBeg, iEnd, x, y, &aXMin, &aXMax);
	xMinMax(b->iBeg, b->iEnd, x, y, &bXMin, &bXMax);
	if (BoxesMeet(aXMin, aXMax, aYMin, aYMax, bXMin, bXMax, bYMin, bYMax))
	{
		result = 1;
		while (2 < iEnd - iBeg)
		{
			long mid = (short) ((iBeg + iEnd) >> 1);
			yMinMax(iBeg, mid, y, &aYMin, &aYMax);
			xMinMax(iBeg, mid, x, y, &aXMin, &aXMax);
			Boolean left = BoxesMeet(aXMin, aXMax, aYMin, aYMax, bXMin, bXMax, bYMin, bYMax);
			yMinMax(mid, iEnd, y, &aYMin, &aYMax);
			xMinMax(mid, iEnd, x, y, &aXMin, &aXMax);
			Boolean right = BoxesMeet(aXMin, aXMax, aYMin, aYMax, bXMin, bXMax, bYMin, bYMax);
			if (left)
			{
				if (right)
					break;
				iEnd = mid;
			}
			else
			{
				if (!right)
					break;
				iBeg = mid;
			}
		}
	}
	else
	{
		iEnd = -2;
		iBeg = -2;
		result = 0;
	}
	a->iBeg = (short) iBeg;
	a->iEnd = (short) iEnd;
	return result;
}


// ROM 0x0032cc9c Box_Cover__FP8low_typeP12POINTS_GROUPT2
// Whether a's box holds b's.
long
Box_Cover(low_type* low, POINTS_GROUP* a, POINTS_GROUP* b)
{
	short* x = low->fX;
	short* y = low->fY;
	short aYMin, aYMax, bYMin, bYMax, aXMin, aXMax, bXMin, bXMax;
	yMinMax(a->iBeg, a->iEnd, y, &aYMin, &aYMax);
	yMinMax(b->iBeg, b->iEnd, y, &bYMin, &bYMax);
	xMinMax(a->iBeg, a->iEnd, x, y, &aXMin, &aXMax);
	xMinMax(b->iBeg, b->iEnd, x, y, &bXMin, &bXMax);
	return aXMin <= bXMin && aXMax >= bXMax && aYMax >= bYMax && aYMin <= bYMin;
}


// ROM 0x0032cad4 Find_Cross__FP8low_typeP13PS_point_typeP12POINTS_GROUPT3
// Where pieces a and b of the trace cross: each is first narrowed to
// the part near the other (Close_To), then the first pair of steps that
// cross makes a and b those two steps and *p the crossing.  ==> 1 they
// cross, 0 not.
long
Find_Cross(low_type* low, PS_point_type* p, POINTS_GROUP* a, POINTS_GROUP* b)
{
	short* y = low->fY;
	short* x = low->fX;
	if (Close_To(low, a, b) == 0 || Close_To(low, b, a) == 0)
		return 0;
	long aEnd = a->iEnd;
	long bBeg = b->iBeg;
	long bEnd = b->iEnd;
	for (long i = a->iBeg; i < aEnd; i = (short) (i + 1))
	{
		short xi = x[i], yi = y[i], xi1 = x[i + 1], yi1 = y[i + 1];
		for (long j = bBeg; j < bEnd; j = (short) (j + 1))
		{
			short px, py;
			if (FindCrossPoint(xi, yi, xi1, yi1, x[j], y[j], x[j + 1], y[j + 1], &px, &py) == 1)
			{
				a->iBeg = (short) i;
				a->iEnd = (short) (i + 1);
				b->iBeg = (short) j;
				b->iEnd = (short) (j + 1);
				p->x = px;
				p->y = py;
				return 1;
			}
		}
	}
	return 0;
}


// ROM 0x0032d08c IsAnythingShift__FP8low_typeP12POINTS_GROUPT2sT4
// Whether a's left (aSide 0) or right (1) edge is not left of b's left
// (bSide 0) or right (1) edge.  ==> 0 it is left of it, 1 otherwise (and
// for any other sides).
long
IsAnythingShift(low_type* low, POINTS_GROUP* a, POINTS_GROUP* b, short aSide, short bSide)
{
	short* x = low->fX;
	short* y = low->fY;
	short aXMin, aXMax, bXMin, bXMax;
	xMinMax(a->iBeg, a->iEnd, x, y, &aXMin, &aXMax);
	xMinMax(b->iBeg, b->iEnd, x, y, &bXMin, &bXMax);
	if (aSide == 0)
	{
		if (bSide == 0)
			return (aXMin >= bXMin) ? 1 : 0;
		if (bSide == 1)
			return (aXMin < bXMax) ? 0 : 1;
		return 1;
	}
	if (aSide == 1)
	{
		if (bSide == 1)
			return (aXMax < bXMax) ? 0 : 1;
		if (bSide == 0)
			return (aXMax < bXMin) ? 0 : 1;
		return 1;
	}
	return 1;
}


// ROM 0x0032dcdc BoxSmallOK__FsT1PsT3
// Whether the trace from i to j fits in a box under 27 each way.
Boolean
BoxSmallOK(short i, short j, short* x, short* y)
{
	_RECT box;
	GetTraceBox(x, y, i, j, &box);
	return box.right - box.left < 0x1b && box.bottom - box.top < 0x1b;
}


#pragma mark The upright sticks

// Forward from `from` to `to` (inclusive) for an element of either mark;
// nil for none.
static SPEC_TYPE*
FindForward(SPEC_TYPE* from, SPEC_TYPE* to, UByte m1, UByte m2)
{
	for (SPEC_TYPE* p = from; ; p = p->next)
	{
		if (p->mark == m1 || p->mark == m2)
			return p;
		if (p == to)
			return nil;
	}
}


// Back from `from` to `to` (inclusive) for an element of either mark.
static SPEC_TYPE*
FindBackward(SPEC_TYPE* from, SPEC_TYPE* to, UByte m1, UByte m2)
{
	for (SPEC_TYPE* p = from; ; p = p->prev)
	{
		if (p->mark == m1 || p->mark == m2)
			return p;
		if (p == to)
			return nil;
	}
}


// ROM 0x0032d1c0 VertStickBorders__FP8low_typeP9SPEC_TYPEP12POINTS_GROUP
// Whether the piece between extremum elem and the next one (both tops,
// 1, or bottoms, 3) is an upright stick - under 12 hundredths bent and
// over 10 long - or, failing that, whether the part of it between the
// middles of the half-turns (0x21/0x33 or 0x23/0x31) at its two ends is
// one (under 35 bent).  *group is set to the piece either way.
// ==> 1 a stick, 0 not.
long
VertStickBorders(low_type* low, SPEC_TYPE* elem, POINTS_GROUP* group)
{
	SPEC_TYPE* next = elem + 1;
	short* x = low->fX;
	short* y = low->fY;
	_SDS_TYPE sds;
	short px, py;
	if (next->mark != 1 && next->mark != 3)
		return 0;
	if (!Init_SDS_Element(&sds))
		return 0;
	sds.iBeg = elem->ipoint0;
	sds.iEnd = next->ipoint0;
	iMostFarDoubleSide(x, y, &sds, &px, &py, 1);
	if (HWRAbs(sds.slope) > 0x5a && sds.crook < 0xc && sds.chord > 10)
	{
		group->iBeg = elem->ipoint0;
		group->iEnd = next->ipoint0;
		return 1;
	}
	SPEC_TYPE* p;
	long v;
	if (elem->mark == 1)
	{
		p = FindForward(elem->next, next, 0x21, 0x33);
		v = (p != nil) ? (p->iEnd + p->ipoint0) >> 1 : 0;
		group->iBeg = (p != nil && elem->iEnd <= v) ? (short) v : elem->iEnd;
		p = FindBackward(next->prev, elem, 0x23, 0x31);
	}
	else if (elem->mark == 3)
	{
		p = FindForward(elem->next, next, 0x23, 0x31);
		v = (p != nil) ? (p->iEnd + p->ipoint0) >> 1 : 0;
		group->iBeg = (p != nil && elem->iEnd <= v) ? (short) v : elem->iEnd;
		p = FindBackward(next->prev, elem, 0x21, 0x33);
	}
	else
		return 0;
	v = (p != nil) ? (p->iBeg + p->ipoint0 + 1) >> 1 : 0;
	group->iEnd = (p != nil && !(next->iBeg < v)) ? (short) v : next->iBeg;
	if (group->iBeg >= group->iEnd)
		return 0;
	sds.iBeg = group->iBeg;
	sds.iEnd = group->iEnd;
	iMostFarDoubleSide(x, y, &sds, &px, &py, 1);
	return (HWRAbs(sds.slope) > 0x5a && sds.crook < 0x23 && sds.chord > 10) ? 1 : 0;
}


// ROM 0x0032da64 VertSticksSelector__FP8low_type
// The upright sticks between the extrema (up to 80) into fBars.
void
VertSticksSelector(low_type* low)
{
	SPEC_TYPE* specl = low->fSpecl;
	POINTS_GROUP* bars = low->fBars;
	long n = low->fLenSpecl;
	long count = 0;
	for (long i = 2; i < n; i = (short) (i + 1))
	{
		SPEC_TYPE* elem = &specl[i];
		if (elem->mark == 1 || elem->mark == 3)
		{
			if (0x4f < count)
				break;
			// (the box is never set: only the ends are read)
			POINTS_GROUP group = { 0, 0, { 0, 0, 0, 0 } };
			if (VertStickBorders(low, elem, &group) == 1)
			{
				bars[count] = group;
				count = (short) (count + 1);
			}
		}
	}
	low->fLenBars = (short) count;
}


// ROM 0x0032bacc YFilter__FP8low_typeP9_SDS_TYPEP9SPEC_TYPE
// Whether a gently sloping stroke of the middle band (the bar of a t or
// an f?) crosses a stick of the stroke before or after it well away
// from the stick's middle - the crossing at least two and a half times
// as far from one end of the piece as from the other.  ==> 1 it does,
// 0 not.
long
YFilter(low_type* low, _SDS_TYPE* piece, SPEC_TYPE* elem)
{
	short* y = low->fY;
	short* x = low->fX;
	long code = (short) elem->code;
	long attr = (short) elem->attr;
	if (piece->slope > 0x30 || piece->slope < 0xf)
		return 0;
	if (piece->crook > 0x12 || code > 6)
		return 0;
	if (code < 5 || attr > 4 || attr < 3)
		return 0;
	long g = (short) GetGroupNumber(low, elem->iBeg);
	for (long k = (short) (low->fLenBars - 1); k >= 0; k = (short) (k - 1))
	{
		POINTS_GROUP bar = low->fBars[k];
		long gb = (short) GetGroupNumber(low, bar.iBeg);
		if (g - 1 > gb)
			return 0;
		if (g + 1 != gb && g - 1 != gb)
			continue;
		short yMin, yMax;
		yMinMax(bar.iBeg, bar.iEnd, y, &yMin, &yMax);
		if (yMax - 0x27c0 < 0x26)
			return 0;
		POINTS_GROUP part = bar;
		POINTS_GROUP mine = { piece->iBeg, piece->iEnd, { 0, 0, 0, 0 } };
		PS_point_type cross;
		if (Find_Cross(low, &cross, &mine, &part) != 1)
			continue;
		long i1 = (short) ixMin(piece->iBeg, piece->iEnd, x, y);
		long i2 = (short) ixMax(piece->iBeg, piece->iEnd, x, y);
		long dx = (short) (cross.x - x[i1]);
		long dy = (short) (cross.y - y[i1]);
		long d1 = HWRMathILSqrt(LAdd(LMul(dx, dx), LMul(dy, dy)));
		dx = (short) (cross.x - x[i2]);
		dy = (short) (cross.y - y[i2]);
		long d2 = HWRMathILSqrt(LAdd(LMul(dx, dx), LMul(dy, dy)));
		if (d2 != 0)
			return (LMul(d1, 100) / d2 < 0xfa) ? 0 : 1;
		return (d1 <= 0xf) ? 0 : 1;
	}
	return 0;
}


// ROM 0x0032f960 SPDClass__FP8low_typesP9SPEC_TYPEP9_SDS_TYPE
// Whether the stroke elem covers is a stick: its longest piece long
// enough, straight enough and upright enough for where it lies
// (FieldSt, the limits a fifth looser for kind 1), and every point of
// the stroke within the allowed distance of that piece's line.  A stick
// that is not the bar across another stick (YFilter, asked for kinds 1
// and 2) is marked 7.  ==> 7 a stick, 0 not.
long
SPDClass(low_type* low, short kind, SPEC_TYPE* elem, _SDS_TYPE* head)
{
	short* x = low->fX;
	short* y = low->fY;
	long iBeg = elem->iBeg;
	long iEnd = elem->iEnd;
	short code = elem->code;
	short attr = elem->attr;
	long best = head->crook;
	elem->mark = 0;
	short maxA, maxCR, minL;
	FieldSt(head, attr, code, (short) best, &maxA, &maxCR, &minL);
	if (kind == 1)
		maxA = (short) (maxA * 122 / 100);
	_SDS_TYPE* piece = &head[best];
	if (piece->chord <= minL)
		return 0;
	if (piece->crook >= maxCR)
		return 0;
	if (HWRAbs(piece->slope) >= maxA)
		return 0;
	if (piece->lengthRatio >= 0x82 && attr <= 5 && piece->crook >= 8)
		return 0;
	long tol = LMul(maxCR, piece->chord) / 100;
	if (kind == 1)
		tol = tol * 122 / 100;
	if (piece->slope == 0x7fff)
	{
		long mx = (x[piece->iBeg] + x[piece->iEnd]) >> 1;
		if (iEnd <= iBeg)
			goto judged;
		for (long i = iBeg; i < iEnd; i++)
			if (HWRAbs(mx - x[i]) > tol)
				return 0;
	}
	if (piece->slope == 0)
	{
		long my = (y[piece->iBeg] + y[piece->iEnd]) >> 1;
		if (iEnd <= iBeg)
			goto judged;
		for (long i = iBeg; i < iEnd; i++)
			if (HWRAbs(my - y[i]) > tol)
				return 0;
	}
	{
		long s = piece->slope;
		if (s != 0x7fff && s != 0)
		{
			// every point's distance from the line through the piece's
			// start, in coordinates 10000 below the trace's
			long c = (y[piece->iBeg] - 10000) - LMul(s, x[piece->iBeg]) / 100;
			long k = LMul(s, s) / 100 + 100;
			long tol2 = LMul(tol, tol);
			for (long i = iBeg; i < iEnd; i++)
			{
				long a = LMul(s, x[i]) / 100;
				long b = y[i] - 10000;
				long u = LMul(LAdd(a - b, c), 100) / k;
				long v = LMul(s, (b - a) - c) / k;
				if (LAdd(LMul(v, v), LMul(u, u)) > tol2)
					return 0;
			}
		}
	}
judged:
	if ((kind == 1 || kind == 2) && YFilter(low, piece, elem) != 0)
		return 0;
	elem->mark = 7;
	return 7;
}


// ROM 0x0032fd24 InStr__FP8low_typeP9_SDS_TYPEP9SPEC_TYPEPs
// The strokes crossed within themselves: a piece of the stroke going
// right, low in the heights and nearly level, that crosses a steep,
// straight piece two before it (not overhanging the piece between by
// more than 5), is marked as an arc (5) over the level piece, pointing
// at the steep piece's points either side of the crossing.
// ==> 0, 1 for no room.
long
InStr(low_type* low, _SDS_TYPE* head, SPEC_TYPE* elem, short* heights)
{
	short* x = low->fX;
	short* y = low->fY;
	long slopeLimit = 0x28;
	long crookLimit = 0x14;
	if ((head[1].mark == 0 && head[1].attr == 0x20))
		return 0;
	for (long k = 2; !(head[k].mark == 0 && head[k].attr == 0x20); k++)
	{
		if (k < 3)
			continue;
		_SDS_TYPE* piece = &head[k];
		long iBeg = piece->iBeg;
		long iEnd = piece->iEnd;
		if (piece->chord <= 0xf)
			continue;
		if (x[iBeg] >= x[iEnd])
			continue;
		short bottom, top;
		RelHigh(y, iBeg, iEnd, heights, &bottom, &top);
		if (bottom <= 3)
			continue;
		// ROM QUIRK: the limit is narrowed for good, once for each such piece
		if (bottom <= 4)
			slopeLimit = slopeLimit * 75 / 100;
		if (!(HWRAbs(piece->slope) <= slopeLimit && piece->crook <= crookLimit))
			continue;
		_SDS_TYPE* steep = &head[k - 2];
		_SDS_TYPE* between = &head[k - 1];
		if (HWRAbs(steep->slope) < 0xfa)
			continue;
		if (crookLimit <= steep->crook)
			continue;
		if (steep->chord <= 0xf)
			continue;
		short cx, cy;
		if (FindCrossPoint(x[iBeg], y[iBeg], x[iEnd], y[iEnd], x[steep->iBeg], y[steep->iBeg], x[steep->iEnd], y[steep->iEnd], &cx, &cy) == 0)
			continue;
		long top1 = iYup_range(y, piece->iBeg, piece->iEnd);
		long top2 = iYup_range(y, between->iBeg, between->iEnd);
		if (y[top1] - 5 > y[top2])
			continue;
		long i = iClosestToXY(steep->iBeg, steep->iEnd, x, y, cx, cy);
		long a = (y[i - 1] != -1) ? i - 1 : i;
		long b = (y[i + 1] != -1) ? i + 1 : i;
		elem->mark = 5;
		elem->iBeg = (short) iBeg;
		elem->iEnd = (short) iEnd;
		elem->ipoint0 = (short) a;
		elem->ipoint1 = (short) b;
		if (MarkSpecl(low, elem) == 1)
			return 1;
	}
	return 0;
}


#pragma mark The hatches

// ROM 0x0032db0c SpcElemFirstOccArr__FP8low_typePsP12POINTS_GROUPUc
// The first element (from the array's second) of the mark given that
// meets the piece of trace in group, and how it meets it in *flags:
// 0x40 it covers the piece (with 8 when its point is inside), 0x20 it is
// inside it; and with flags bit 1 asked for, 0x10 it runs past the
// piece's end and 4 past its start (8 again for a point inside).  With
// bit 0 asked for, a stroke's end (0x20) inside the piece stops the
// search.  ==> its index, -2 for none (and at an element that covers
// no points).
long
SpcElemFirstOccArr(low_type* low, short* flags, POINTS_GROUP* group, UByte mark)
{
	SPEC_TYPE* specl = low->fSpecl;
	long n = low->fLenSpecl;
	long a = group->iBeg;
	long b = group->iEnd;
	if (a > b)
		return -2;
	if (n <= 1)
		return -2;
	for (long i = 1; i < n; i = (short) (i + 1))
	{
		SPEC_TYPE* e = &specl[i];
		long iBeg = e->iBeg;
		if (iBeg <= 0)
			return -2;
		long iEnd = e->iEnd;
		if (iEnd <= 0)
			return -2;
		UByte m = e->mark;
		if (m == mark && iBeg <= a && iEnd >= b)
		{
			*flags = (short) (*flags | 0x40);
			if (e->ipoint0 < a || e->ipoint0 > b)
				return i;
			*flags = (short) (*flags | 8);
			return i;
		}
		if (m == mark && ((a <= iBeg && iBeg <= b) || (a <= iEnd && iEnd <= b)))
		{
			if (iBeg >= a && iEnd <= b)
			{
				*flags = (short) (*flags | 0x20);
				return i;
			}
			if ((*flags & 2) != 0)
			{
				if (iBeg >= a)
				{
					*flags = (short) (*flags | 0x10);
					if (b >= e->ipoint0)
						*flags = (short) (*flags | 8);
					return i;
				}
				if (iEnd <= b)
				{
					*flags = (short) (*flags | 4);
					if (a <= e->ipoint0)
						*flags = (short) (*flags | 8);
					return i;
				}
			}
		}
		if ((*flags & 1) != 0 && m == 0x20 && iBeg >= a && iEnd <= b)
			return -2;
	}
	return -2;
}


// ROM 0x0032c8a4 ApprHorStroke__FP8low_type
// Which piece of the stroke being described might be a hatch: the first
// piece that is not within a 27-point box of the stroke's start, or the
// one after it, whichever is long (over 24), not too bent, going right
// and within 45 degrees of level - the second when it reaches further
// right.  ==> its index from the stroke's head, -2 for none.
long
ApprHorStroke(low_type* low)
{
	_SDS_CONTROL_TYPE* control = low->fSDS;
	_SDS_TYPE* head = &control->pSDS[control->f02];
	short* x = low->fX;
	short* y = low->fY;
	if (!(head->mark == 0 && head->attr == 0x10))
		return -2;
	long k = 1;
	while (BoxSmallOK(head->iBeg, head[k].iEnd, x, y) && !(head[k].mark == 0 && head[k].attr == 0x20))
		k = (short) (k + 1);
	_SDS_TYPE* a = &head[k];
	_SDS_TYPE* b = &head[k + 1];
	Boolean aTail = (a->mark == 0 && a->attr == 0x20);
	Boolean bTail = (b->mark == 0 && b->attr == 0x20);
	long aOK = (!aTail && 0x18 < a->chord && a->crook < 0x41 && x[a->iBeg] < x[a->iEnd] && HWRAbs(a->slope) < 100) ? 1 : 0;
	long bOK = (!aTail && !bTail && 0x18 < b->chord && b->crook < 0x14 && x[b->iBeg] < x[b->iEnd] && HWRAbs(b->slope) < 100) ? 1 : 0;
	if (aOK == 1)
	{
		if (bOK == 0)
			return k;
		if (a->xMin + 10 < b->xMin)
			return k;
		if (a->xMax + 10 >= b->xMax)
			return k;
		return (short) (k + 1);
	}
	if (bOK == 1 && b->xMax > a->xMax)
		return (short) (k + 1);
	return -2;
}


// ROM 0x0032bd80 InvTanDel__FP8low_typesT2
// Whether two slopes (hundredths) are far apart as angles: the tangent
// of the angle between them over 60 hundredths (40 when rc says so), or
// the lines near perpendicular.  ==> 1 far apart, 0 not.
long
InvTanDel(low_type* low, short a, short b)
{
	if (b >= 0x7fff)
		return 0;
	if (a >= 0x7fff)
		return 1;
	long d = a - b;
	long t = LAdd(LMul(b, a), 10000) / 100;
	if (HWRLAbs(t) < 100)
		return 1;
	long r = HWRLAbs(LMul(d, 100) / t);
	long limit = ((RCGetH(low->rc, 0x90) & 0x800) == 0) ? 0x3c : 0x28;
	return (r > limit) ? 1 : 0;
}


// ROM 0x0032be28 Oracle__FP8low_typeP13PS_point_type15_HAT_DENOM_TYPE
// Whether both of a hatch's two measurements are over the limit for its
// kind (6 for 3, 16 for 1, 22 for 2).
long
Oracle(low_type* /*low*/, PS_point_type* measures, long kind)
{
	long limit = (kind == 3) ? 6 : ((kind == 1) ? 0x10 : ((kind == 2) ? 0x16 : 0x7fff));
	return (measures->y > limit && measures->x > limit) ? 1 : 0;
}


// ROM 0x0032ad1c SCutFiltr__FP8low_typePsP9SPEC_TYPEP13PS_point_typeT2
// How far the crossing p is from the leftmost point of the piece in
// elem, into *dist.  ==> 0 a low piece crossed near its start (under
// 30), 1 otherwise.
long
SCutFiltr(low_type* low, short* heights, SPEC_TYPE* elem, PS_point_type* p, short* dist)
{
	short* x = low->fX;
	short* y = low->fY;
	long iBeg = elem->iBeg;
	long iEnd = elem->iEnd;
	short bottom, top;
	RelHigh(y, iBeg, iEnd, heights, &bottom, &top);
	long i = ixMin(iBeg, iEnd, x, y);
	long dx = (short) (p->x - x[i]);
	long dy = (short) (p->y - y[i]);
	long d = HWRMathILSqrt(LAdd(LMul(dx, dx), LMul(dy, dy)));
	*dist = (short) d;
	return (top <= 3 && d < 0x1e) ? 0 : 1;
}


// ROM 0x0032ae00 RDFiltr__FP8low_typeP13PS_point_typeP9SPEC_TYPET2
// Whether the second of measures is at least two fifths of the way from
// the crossing p to the rightmost point of the piece in elem (and two
// points on).
long
RDFiltr(low_type* low, PS_point_type* measures, SPEC_TYPE* elem, PS_point_type* p)
{
	short* x = low->fX;
	short* y = low->fY;
	long i = ixMax(elem->iBeg, elem->iEnd + 2, x, y);
	long dx = p->x - x[i];
	long dy = p->y - y[i];
	long d = HWRMathILSqrt(LAdd(LMul(dx, dx), LMul(dy, dy)));
	return (measures->y >= (d * 2 + 2) / 5) ? 1 : 0;
}


// ROM 0x0032b494 LeFiltr__FP8low_typeP9SPEC_TYPEs
// Whether point s is within a turn (0x13) of the rest of the stroke
// after elem, when elem goes down.  ==> 1 it is, 0 not.
long
LeFiltr(low_type* low, SPEC_TYPE* elem, short s)
{
	short* y = low->fY;
	SPEC_TYPE* specl = low->fSpecl;
	POINTS_GROUP* stroke = &low->fGroups[elem->other];
	if (s == -2)
		return 0;
	if (y[elem->iEnd] <= y[elem->iBeg])
		return 0;
	POINTS_GROUP g = { (short) (elem->iEnd - 1), stroke->iEnd, { 0, 0, 0, 0 } };
	short flags = 2;
	long k = SpcElemFirstOccArr(low, &flags, &g, 0x13);
	if (k == -2)
		return 0;
	SPEC_TYPE* e = &specl[k];
	return (s <= e->iEnd && s >= e->iBeg) ? 1 : 0;
}


// ROM 0x0032aa38 LowStFiltr__FP8low_typePsP9SPEC_TYPEP13PS_point_typeT3
// Whether the hatch bar goes on too far to be one: the part of the
// stroke from where it turns before the bar to where it turns after it
// reaches up high, or it starts well below a low top that the crossing
// measures say is near.  ==> 1 it does (not a hatch), 0 not.
long
LowStFiltr(low_type* low, short* heights, SPEC_TYPE* bar, PS_point_type* /*p*/, SPEC_TYPE* measures)
{
	SPEC_TYPE* specl = low->fSpecl;
	short* y = low->fY;
	long grp = bar->other;
	long iBeg = bar->iBeg;
	long iEnd = bar->iEnd;
	long near = measures->ipoint0;
	long flagged = measures->attr;
	short bottom, top;
	RelHigh(y, iBeg, iEnd, heights, &bottom, &top);
	if (grp == 1)
	{
		if (bottom > 3)
			return 0;
	}
	else if (bottom > 4)
		return 0;
	POINTS_GROUP g = { (short) iBeg, low->fGroups[grp].iEnd, { 0, 0, 0, 0 } };
	short flags;
	long start, end;
	long v = y[iBeg];
	if (y[iEnd] > v)
	{
		flags = 2;
		long k = SpcElemFirstOccArr(low, &flags, &g, 3);
		if (k == -2)
			return 1;
		end = specl[k].ipoint0;
	}
	else if (y[iEnd] < v)
	{
		for (end = iBeg; y[end - 1] != -1 && y[end - 1] >= v; end--)
			;
	}
	else
		return 0;
	v = y[iBeg];
	if (y[iEnd] < v)
	{
		flags = 2;
		long k = SpcElemFirstOccArr(low, &flags, &g, 1);
		// ROM BUG: no top found reads the element two before the list's
		// start; the host cannot, and takes it as the case above does
		// (DEVIATION)
		if (k == -2)
			return 1;
		start = specl[k].ipoint0;
	}
	else if (y[iEnd] > v)
	{
		for (start = iBeg; y[start - 1] != -1 && y[start - 1] <= v; start--)
			;
	}
	else
		return 0;
	RelHigh(y, start, end, heights, &bottom, &top);
	if (top >= 8)
		return 1;
	if (grp == 1 && bottom < 2)
		return 0;
	long i3 = measures->iEnd;
	if (y[i3 + 1] != -1)
		i3++;
	long t = iyMin(measures->iBeg, i3, y);
	if (!(y[t] < y[start] + 0x14 && near < 0x1e))
		return 1;
	return (flagged != 0) ? 1 : 0;
}


// ROM 0x0032bfa0 HatDenAnal__FP8low_typeP9SPEC_TYPET2
// Where the hatch bar ends: the stroke's first right-hand turn after it
// (0x33 or 0x23) more than 10 right of its start.  ==> 2 the bar's end
// moved there, 1 none found.
long
HatDenAnal(low_type* low, SPEC_TYPE* bar, SPEC_TYPE* /*stroke*/)
{
	short* x = low->fX;
	SPEC_TYPE* specl = low->fSpecl;
	POINTS_GROUP g = { bar->ipoint1, bar->iEnd, { 0, 0, 0, 0 } };
	long ends[2];
	static const UByte kMarks[2] = { 0x33, 0x23 };
	for (long t = 0; t < 2; t++)
	{
		short flags = 2;
		long k = SpcElemFirstOccArr(low, &flags, &g, kMarks[t]);
		long r = -2;
		SPEC_TYPE* e = (k != -2) ? &specl[k] : nil;
		if (e != nil && x[e->iEnd] > x[bar->iBeg] + 10)
		{
			if ((flags & 0x20) != 0)
				r = e->ipoint0;
			else if ((flags & 4) != 0)
				r = e->iEnd;
			else if ((flags & 0x10) != 0)
				r = e->iBeg;
		}
		ends[t] = r;
	}
	long r = ends[1];
	if (ends[0] != -2)
	{
		if (r == -2 || ends[0] < r)
			r = ends[0];
	}
	else if (r == -2)
		return 1;
	bar->iEnd = (short) r;
	return 2;
}


// ROM 0x0032c484 ShiftsAnalyse__FP8low_typeP9SPEC_TYPEN22
// Whether the stroke has moved left of the hatch: the part before the
// stick not left of the part after it, or where the bar turns (or its
// higher end) not left of it.  ==> 1 it has, 0 not.
long
ShiftsAnalyse(low_type* low, SPEC_TYPE* bar, SPEC_TYPE* stick, SPEC_TYPE* stroke)
{
	short* y = low->fY;
	SPEC_TYPE* specl = low->fSpecl;
	short flags = 0;
	POINTS_GROUP before = { stick->iBeg, stick->ipoint1, { 0, 0, 0, 0 } };
	POINTS_GROUP after = { stick->iEnd, stroke->iEnd, { 0, 0, 0, 0 } };
	if (IsAnythingShift(low, &before, &after, 0, 0) == 1)
		return 1;
	long grp = (short) GetGroupNumber(low, bar->iBeg);
	Boolean down = (y[bar->iEnd] >= y[bar->iBeg]);
	if (down)
	{
		before.iBeg = bar->ipoint1;
		before.iEnd = low->fGroups[grp].iEnd;
	}
	else
	{
		before.iBeg = low->fGroups[grp].iBeg;
		before.iEnd = bar->ipoint0;
	}
	flags = (short) (flags | 2);
	long k = SpcElemFirstOccArr(low, &flags, &before, 3);
	if (k != -2)
		before.iBeg = before.iEnd = specl[k].ipoint0;
	else if (down)
		before.iBeg = before.iEnd = bar->iEnd;
	else
		before.iBeg = before.iEnd = bar->iBeg;
	return IsAnythingShift(low, &before, &after, 1, 0);
}


// ROM 0x0032c68c DrawCross__FP8low_typePsP13PS_point_typeP9SPEC_TYPET4
// Whether an upright line down from the bar's higher end (from 10 below
// it to 20 above - 50 when rc says so) crosses the piece in measures,
// its left end moved 5 (30) further left: a t's cross drawn as two
// strokes.  If it does, the bar is pointed at that end and measures at
// the points either side of the crossing; *p is the crossing either
// way.  ==> 1 it crosses, 0 not.
long
DrawCross(low_type* low, short* heights, PS_point_type* p, SPEC_TYPE* bar, SPEC_TYPE* measures)
{
	short* y = low->fY;
	short* x = low->fX;
	long top;
	if (y[bar->iEnd] > y[bar->iBeg])
		top = bar->iBeg;
	else if (y[bar->iEnd] < y[bar->iBeg])
		top = bar->iEnd;
	else
		return 0;
	short bottomBand, topBand;
	RelHigh(y, top, top, heights, &bottomBand, &topBand);
	if (bottomBand < 5)
		return 0;
	Boolean wide = (RCGetH(low->rc, 0x90) & 0x800) != 0;
	long d = wide ? 0x1e : 5;
	long up = wide ? 0x32 : 0x14;
	long i1 = (short) ixMin(measures->iBeg, measures->iEnd, x, y);
	long i2 = (short) ixMax(measures->iBeg, measures->iEnd, x, y);
	long left = x[i1] - d;
	if (left <= 0)
		left = 0;
	// (FindCrossPoint answers where the lines meet whether or not the
	// pieces do; the host starts them nought)
	short px = 0, py = 0;
	long result = FindCrossPoint(x[top], (short) (y[top] + 10), x[top], (short) (y[top] - up), (short) left, y[i1], x[i2], y[i2], &px, &py);
	if (result == 1)
	{
		bar->ipoint1 = (short) top;
		bar->ipoint0 = (short) top;
		measures->ipoint0 = (short) iClosestToXY(measures->iBeg, measures->iEnd, x, y, px, py);
		long p0 = measures->ipoint0;
		measures->ipoint1 = (short) ((p0 + 1 > measures->iEnd) ? p0 : p0 + 1);
	}
	p->x = px;
	p->y = py;
	return result;
}


// ROM 0x0032c184 InsertBreakAfter__FP8low_typesT2P13PS_point_type
// A pen-up put into the trace after point at (the point after it
// becomes the break, its x the marker given; the point after that is
// moved a fifth of the way from p - or from the point replaced when p's
// x is -2 - towards where it was), then the strokes found again and the
// sticks that ran across the break cut at it (or dropped).  ==> 1, 0 when
// the trace has a pen-up too near.
long
InsertBreakAfter(low_type* low, short marker, short at, PS_point_type* p)
{
	POINTS_GROUP* bars = low->fBars;
	long nBars = low->fLenBars;
	short* y = low->fY;
	short* x = low->fX;
	long result = 1;
	long b1 = (short) (at + 1);
	long b2 = (short) (at + 2);
	if (y[at] == -1 || y[b2] == -1 || y[at + 3] == -1)
		return 0;
	long y2 = y[b2];
	if (y[at + 1] == -1)
		return 1;
	long v;
	if (p->x == -2)
	{
		v = y[b1];
		y[b2] = (short) (v - (v + 2) / 5 + (y2 + 2) / 5);
		v = x[b1];
	}
	else
	{
		v = p->y;
		y[b2] = (short) (v - (v + 2) / 5 + (y2 + 2) / 5);
		v = p->x;
	}
	x[b2] = (short) (v - (v + 2) / 5 + (x[b2] + 2) / 5);
	y[b1] = -1;
	x[b1] = marker;
	if (InitGroupsBorder(low, 1) == 1)
		return 0;
	// ROM QUIRK: a stick taken out moves the rest down one, but the walk
	// goes on by index over the old count - the one moved into its place
	// is skipped, and the last is looked at twice
	for (long k = 0; k < nBars; k++)
	{
		POINTS_GROUP* bar = &bars[k];
		if (bar->iBeg > b1 || bar->iEnd < b1)
			continue;
		if (bar->iEnd > b2)
		{
			_SDS_TYPE sds;
			short px, py;
			memset(&sds, 0, sizeof(sds));		// (the ROM's is whatever was on the stack; every field read is set)
			sds.iBeg = (short) b2;
			sds.iEnd = bar->iEnd;
			iMostFarDoubleSide(x, y, &sds, &px, &py, 1);
			if (HWRAbs(sds.slope) > 0x5a && sds.crook < 0xc && sds.chord > 10)
			{
				bar->iBeg = (short) b2;
				return result;
			}
		}
		memmove(bar, bar + 1, (nBars - k - 1) * sizeof(POINTS_GROUP));
		low->fLenBars = (short) (low->fLenBars - 1);
	}
	return result;
}


// ROM 0x0032b57c StrokeAnalyse__FP8low_typePsP9SPEC_TYPEN23Ui
// What the stroke is once its hatch is taken off.  When enough of the
// stroke goes on past the bar, low down, and the part up to the bar is
// a fairly straight upright one, the rest is judged on its own: a dot
// (the bar then running to the stroke's end) or a stick.  Otherwise the
// bar and what follows it are judged as a stick (SPDClass, kind 1 when
// nothing goes on past the bar) if the bar and the piece in measures
// are far apart as angles.  ==> 7 a stick, 2 a hatch, 1 no room.
long
StrokeAnalyse(low_type* low, short* heights, SPEC_TYPE* bar, SPEC_TYPE* stroke, SPEC_TYPE* measures, ULong strict)
{
	short* y = low->fY;
	short* x = low->fX;
	long sBeg = stroke->iBeg;
	long sEnd = stroke->iEnd;
	long bBeg = bar->iBeg;
	long bEnd = bar->iEnd;
	long after = (short) (bEnd + 2);
	_SDS_TYPE a[3];
	_SDS_TYPE b[3];
	SPEC_TYPE e;
	short bottom, top, bottom2, top2, px, py;
	long kind;
	long r;
	if (after >= sEnd)
	{
		bEnd = sEnd;
		kind = 1;
		goto judgeBar;
	}
	kind = 0;
	RelHigh(y, after, sEnd, heights, &bottom, &top);
	if (bottom > 4)
		return 2;
	RelHigh(y, bEnd, bEnd, heights, &bottom2, &top2);
	if (bottom >= bottom2)
		return 2;
	if (!Init_SDS_Element(&a[0]) || !Init_SDS_Element(&a[1]) || !Init_SDS_Element(&a[2]))
		return 1;
	a[1].iBeg = (short) sBeg;
	a[1].iEnd = (short) after;
	iMostFarDoubleSide(x, y, &a[1], &px, &py, 1);
	if (!(HWRAbs(a[1].slope) <= 100 && a[1].crook <= 0x2b))
		return 2;
	a[1].iBeg = (short) after;
	a[1].iEnd = (short) sEnd;
	iMostFarDoubleSide(x, y, &a[1], &px, &py, 1);
	xMinMax(after, sEnd, x, y, &a[1].xMin, &a[1].xMax);
	yMinMax(after, sEnd, y, &a[1].yMin, &a[1].yMax);
	a[0].crook = 1;
	a[0].share = 100;
	a[0].xMin = a[1].xMin;
	a[0].xMax = a[1].xMax;
	a[0].yMin = a[1].yMin;
	a[0].yMax = a[1].yMax;
	InitSpeclElement(&e);
	e.iBeg = (short) after;
	e.iEnd = (short) sEnd;
	e.code = (UByte) top;
	e.attr = (UByte) bottom;
	if (Dot(low, &e, a) == 8)
	{
		bar->iEnd = stroke->iEnd;
		return 2;
	}
	if (SPDClass(low, 0, &e, a) == 7)
		return 2;
judgeBar:
	if (!Init_SDS_Element(&a[0]) || !Init_SDS_Element(&a[1]) || !Init_SDS_Element(&a[2]))
		return 1;
	a[1].iBeg = (short) bBeg;
	a[1].iEnd = (short) bEnd;
	iMostFarDoubleSide(x, y, &a[1], &px, &py, 1);
	xMinMax(sBeg, sEnd, x, y, &a[0].xMin, &a[0].xMax);
	yMinMax(sBeg, sEnd, y, &a[0].yMin, &a[0].yMax);
	a[0].crook = 1;
	a[1].share = 100;
	if (!Init_SDS_Element(&b[0]) || !Init_SDS_Element(&b[1]) || !Init_SDS_Element(&b[2]))
		return 1;
	b[1].iBeg = measures->iBeg;
	b[1].iEnd = measures->iEnd;
	iMostFarDoubleSide(x, y, &b[1], &px, &py, 1);
	if (InvTanDel(low, b[1].slope, a[1].slope) == 1)
	{
		RelHigh(y, bBeg, bEnd, heights, &bottom, &top);
		InitSpeclElement(&e);
		e.iBeg = (short) sBeg;
		e.iEnd = (short) bEnd;
		e.code = (UByte) top;
		e.attr = (UByte) bottom;
		r = SPDClass(low, (short) kind, &e, a);
		if (r != 7 && a[1].slope > 0x41)
			r = 2;
	}
	else
		r = 2;
	if (stroke->iEnd == bEnd)
	{
		if (r == 7)
			bar->iEnd = stroke->iEnd;
		else
			r = 2;
	}
	if (strict == 1 && r != 7)
		r = 2;
	return r;
}


// ROM 0x0032ae94 RMinCalc__FP8low_typePsP9SPEC_TYPEN33
// How near the rest of the stroke, after the hatch bar, comes back to
// the piece in measures: the kind of turn found after the bar into
// out->iBeg (2 a loop back, 3 a turn, 4 none), the sideways distance of
// the nearest approach into out->iEnd and the point in out->ipoint0.
// ==> the nearest distance (0x7fff for none; 0 for a loop back in the
// first stroke).
long
RMinCalc(low_type* low, short* /*heights*/, SPEC_TYPE* bar, SPEC_TYPE* measures, SPEC_TYPE* stroke, SPEC_TYPE* out)
{
	short* x = low->fX;
	short* y = low->fY;
	SPEC_TYPE* specl = low->fSpecl;
	long grp = measures->other;
	POINTS_GROUP* sgrp = &low->fGroups[grp];
	short flags;
	POINTS_GROUP g = { 0, 0, { 0, 0, 0, 0 } };
	long kind, dist, dx;
	// DEVIATION: the ROM leaves the point unset on one path below (it is
	// then whatever was on the stack); the host has it -2, none
	short point = -2;
	long k, k3 = -2;
	SPEC_TYPE* e3;
	SPEC_TYPE* e11;
	SPEC_TYPE* turn;
	if (bar->iEnd + 1 >= stroke->iEnd)
		goto none;
	g.iBeg = (short) (bar->iEnd + 1);
	g.iEnd = stroke->iEnd;
	flags = 2;
	k = SpcElemFirstOccArr(low, &flags, &g, 3);
	if (k == -2)
		goto none;
	e3 = &specl[k];
	g.iBeg = bar->ipoint1;
	g.iEnd = e3->ipoint0;
	flags = 2;
	k = SpcElemFirstOccArr(low, &flags, &g, 1);
	g.iBeg = (short) ((k != -2 && specl[k].iEnd + 1 > bar->iEnd + 1) ? specl[k].iEnd + 1 : bar->iEnd + 1);
	g.iEnd = stroke->iEnd;
	flags = 2;
	k = SpcElemFirstOccArr(low, &flags, &g, 0x11);
	e11 = (k != -2) ? &specl[k] : nil;
	if (e11 == nil || (flags & 0x20) == 0 || (e11->iEnd > e3->iEnd && x[e11->ipoint0] > x[e3->iEnd]))
	{
		flags = 2;
		g.iBeg = measures->iEnd;
		g.iEnd = sgrp->iEnd;
		k = SpcElemFirstOccArr(low, &flags, &g, 0x13);
		point = (k == -2) ? -2 : specl[k].ipoint0;
		dist = dx = 0x7fff;
		kind = 4;
		goto done;
	}
	turn = e11;
	if (e11->iEnd + 1 < stroke->iEnd)
	{
		g.iBeg = (short) (e11->iEnd + 1);
		g.iEnd = stroke->iEnd;
		flags = 2;
		k3 = SpcElemFirstOccArr(low, &flags, &g, 0x11);
		if (k3 == -2)
			kind = 3;
		else
		{
			turn = &specl[k3];
			if (x[e3->iBeg] < x[e3->iEnd])
				kind = 3;
			else
				kind = (x[turn->ipoint0] <= x[e3->iEnd]) ? 2 : 3;
		}
	}
	else
		kind = 3;
	{
		PS_point_type pt;
		short at, at2;
		long t = iyMax(measures->ipoint0, sgrp->iEnd, y);
		if (y[t] > y[turn->ipoint0])
		{
			pt.x = x[turn->ipoint0];
			pt.y = y[turn->ipoint0];
			if (y[measures->iEnd] < y[measures->iBeg])
			{
				g.iBeg = sgrp->iBeg;
				g.iEnd = measures->ipoint0;
			}
			else
			{
				g.iBeg = measures->ipoint0;
				g.iEnd = sgrp->iEnd;
			}
		}
		else
		{
			pt.x = x[t];
			pt.y = y[t];
			g.iBeg = (short) (bar->iEnd + 1);
			g.iEnd = stroke->iEnd;
		}
		dist = R_ClosestToLine(x, y, &pt, &g, &at);
		dx = HWRAbs(pt.x - x[at]);
		if (k3 != -2)
		{
			pt.x = x[e11->ipoint0];
			pt.y = y[e11->ipoint0];
			g.iBeg = measures->ipoint0;
			g.iEnd = sgrp->iEnd;
			long d2 = R_ClosestToLine(x, y, &pt, &g, &at2);
			long dx2 = HWRAbs(pt.x - x[at2]);
			point = at2;
			if (dist > d2)
			{
				dist = d2;
				dx = dx2;
				at = at2;
			}
		}
	}
	goto done;
none:
	dist = dx = 0x7fff;
	kind = 4;
	point = -2;
done:
	out->ipoint0 = point;
	out->iBeg = (short) kind;
	out->iEnd = (short) dx;
	if (grp == 0 && kind == 2)
		dist = 0;
	return dist;
}


// ROM 0x0032a14c HatchureS__FP8low_typeP9SPEC_TYPEPs
// Whether the stroke elem covers has a hatch - the bar of a t or an f
// drawn in one stroke with it, or a cross drawn over a stick of the
// strokes just before (the upright sticks, fBars, from the last back, up
// to five of them).  The piece ApprHorStroke picks as the bar must cross
// a stick of this stroke or of the two before it (or reach it,
// DrawCross), not start to its right, and be no part of a bigger shape
// (the filters and Oracle); what is left of the stroke is then judged by
// StrokeAnalyse.  A stick found this way marks the stroke 7 (pointing
// at the stick); a hatch has its stroke cut after the bar (a pen-up put
// in, InsertBreakAfter) and marked other = 2.  ==> 7 a stick, 2 a hatch
// cut off, 0 neither, 1 no room.
long
HatchureS(low_type* low, SPEC_TYPE* elem, short* heights)
{
	POINTS_GROUP* bars = low->fBars;
	long nBars = low->fLenBars;
	_SDS_TYPE* head = &low->fSDS->pSDS[low->fSDS->f02];
	short* x = low->fX;
	short* y = low->fY;
	long iBeg = elem->iBeg;
	long iEnd = elem->iEnd;
	long grp = elem->ipoint0;
	long gBeg = low->fGroups[grp].iBeg;
	long gEnd = low->fGroups[grp].iEnd;
	long result = 0;
	long found = 0;				// how the bar was found: 1 it crosses, 3 it crosses the rest of the stroke too
	long end = -2;				// where the bar ends
	long maxEnd = -2;			// the furthest the part of the stroke crossed reaches
	long drawn = 0;				// DrawCross found the crossing (the last stick looked at)
	long cut = 0;				// SCutFiltr: the bar crossed away from its start
	long hp;
	long hpBeg, hpEnd;
	long count;
	SPEC_TYPE bar;				// the hatch bar (M)
	SPEC_TYPE stick;			// the stick it crosses (S)
	PS_point_type p1, p2;
	if (iBeg <= 2)
		return 0;
	if (gBeg == -2 || gEnd == -2)
		return 0;
	hp = ApprHorStroke(low);
	if (hp == -2)
		return 0;
	hpBeg = head[hp].iBeg;
	hpEnd = head[hp].iEnd;
	if (InitSpeclElement(&bar) == 1)
		return 1;
	memset(&stick, 0, sizeof(stick));
	p1.x = p1.y = p2.x = p2.y = 0;
	count = 0;
	for (long k = nBars - 1; k >= 0 && count < 5; k--)
	{
		drawn = 0;
		POINTS_GROUP* s = &bars[k];
		long sBeg = s->iBeg;
		long sEnd = s->iEnd;
		if (!(sEnd < hpBeg && sBeg < sEnd))
			continue;
		long g = (short) GetGroupNumber(low, sBeg);
		if (!(g <= grp && grp - 2 <= g))
			continue;
		if (InitSpeclElement(&stick) == 1)
			return 1;
		POINTS_GROUP pg1 = { (short) sBeg, (short) sEnd, { 0, 0, 0, 0 } };
		POINTS_GROUP pg2 = { (short) hpBeg, (short) hpEnd, { 0, 0, 0, 0 } };
		bar.iBeg = (short) hpBeg;
		bar.iEnd = (short) hpEnd;
		stick.iBeg = (short) sBeg;
		stick.iEnd = (short) sEnd;
		stick.other = (UByte) g;
		UByte how;
		if (Find_Cross(low, &p1, &pg2, &pg1) == 0)
		{
			drawn = DrawCross(low, heights, &p1, &stick, &bar);
			if (drawn == 0)
				continue;
		}
		found = 1;
		if (drawn == 0)
		{
			stick.ipoint0 = pg1.iBeg;
			stick.ipoint1 = pg1.iEnd;
			bar.ipoint0 = pg2.iBeg;
			bar.ipoint1 = pg2.iEnd;
			how = 0;
		}
		else
		{
			pg1.iBeg = stick.ipoint0;
			pg1.iEnd = stick.ipoint1;
			pg2.iBeg = bar.ipoint0;
			pg2.iEnd = bar.ipoint1;
			how = 1;
		}
		stick.attr = how;
		bar.other = how;
		short dist;
		cut = SCutFiltr(low, heights, &bar, &p1, &dist);
		SPEC_TYPE r;			// what the filters are told: the piece of the bar crossed, how far the crossing is from its start, LeFiltr's answer
		memset(&r, 0, sizeof(r));
		r.ipoint0 = dist;
		if (ShiftsAnalyse(low, &stick, &bar, elem) == 1)
			return 0;
		if (g < grp)
		{
			POINTS_GROUP whole = { (short) iBeg, (short) iEnd, { 0, 0, 0, 0 } };
			pg1.iBeg = (short) gBeg;
			pg1.iEnd = (short) gEnd;
			if (Box_Cover(low, &whole, &pg1) == 1)
				continue;
		}
		found = HatDenAnal(low, &bar, elem);
		hpEnd = bar.iEnd;
		if (bar.iEnd > end)
			end = bar.iEnd;
		if (maxEnd < pg2.iEnd)
			maxEnd = pg2.iEnd;
		if (end < iEnd)
		{
			pg2.iBeg = (short) hpBeg;
			pg2.iEnd = (short) end;
			pg1.iBeg = (short) (end + 1);
			pg1.iEnd = (short) iEnd;
		}
		r.iBeg = pg2.iBeg;
		r.iEnd = pg2.iEnd;
		count++;
		if (end == -2)
			continue;
		if (end + 2 >= iEnd)
			break;
		pg2.iBeg = (short) (maxEnd - 1);
		pg2.iEnd = (short) hpEnd;
		pg1.iBeg = (short) (end + 1);
		pg1.iEnd = (short) iEnd;
		if (Find_Cross(low, &p2, &pg2, &pg1) == 1)
		{
			found = 3;
			if (bar.iBeg >= bar.iEnd)
				return 0;
			if (bar.iBeg + 1 == bar.iEnd)
				end = bar.iEnd;
			else if (maxEnd == pg2.iEnd)
			{
				x[pg2.iBeg] = (short) ((p1.x + p2.x) >> 1);
				y[pg2.iBeg] = (short) ((p1.y + p2.y) >> 1);
				end = pg2.iBeg;
			}
			else
				end = pg2.iEnd - 1;
			bar.iEnd = (short) end;
		}
		else
		{
			p2.x = -2;
			p2.y = -2;
		}
		if (end >= iEnd)
			continue;
		// the rest of the stroke must not cross back over the stroke
		pg2.iBeg = (short) (end + 1);
		pg2.iEnd = (short) iEnd;
		pg1.iBeg = (short) gBeg;
		pg1.iEnd = (short) gEnd;
		PS_point_type p3;
		if (Find_Cross(low, &p3, &pg2, &pg1) != 0)
			return 0;
		SPEC_TYPE out;
		memset(&out, 0, sizeof(out));
		PS_point_type q;
		q.y = (short) RMinCalc(low, heights, &bar, &stick, elem, &out);
		q.x = out.iEnd;
		long le = LeFiltr(low, &stick, out.ipoint0);
		r.attr = (UByte) le;
		if (LowStFiltr(low, heights, &stick, &p1, &r) == 0)
			return 0;
		if (cut == 0 && le == 0)
			return 0;
		if (out.iBeg == 3 && le == 0 && found != 3 && RDFiltr(low, &q, &bar, &p1) == 0)
			return 0;
		if (Oracle(low, &q, found) == 0)
			return 0;
		break;
	}
	if (end == -2 || found == 0)
		return result;
	result = StrokeAnalyse(low, heights, &bar, elem, &stick, drawn);
	if (result == 1)
		return 1;
	if (result == 2)
		return 0;
	if (result == 7)
	{
		if (elem->iEnd == bar.iEnd)
			end = elem->iEnd;
		elem->mark = 7;
		elem->ipoint0 = stick.ipoint0;
		elem->ipoint1 = stick.ipoint1;
	}
	if (bar.iEnd != elem->iEnd)
	{
		if (InsertBreakAfter(low, -4, (short) end, &p2) == 0)
			return 1;
	}
	elem->other = 2;
	elem->iEnd = (short) end;
	return result;
}


#pragma mark Pict

// ROM 0x00329cc4 FillCross__FP8low_typeP9SPEC_TYPE
// Where the stick elem is crossed by the upright sticks of other strokes
// (fBars): its ipoint0 and ipoint1 are the first points of the two steps
// of the stick that are crossed (one crossing per stroke, the longer
// stick of a stroke counting), or -2 when there are none, more than two
// or two of very different heights.  A stick that is only a short part
// of its stroke's length, bent, or not reaching over the stick, does not
// count.
void
FillCross(low_type* low, SPEC_TYPE* elem)
{
	POINTS_GROUP* bars = low->fBars;
	long nBars = low->fLenBars;
	long lastGrp = -2;
	short* y = low->fY;
	short* x = low->fX;
	POINTS_GROUP last = { 0, 0, { 0, 0, 0, 0 } };
	long count = 0;
	if (elem->other == 2 || elem->mark == 8)
		return;
	elem->ipoint1 = -2;
	elem->ipoint0 = -2;
	for (long k = 0; k < nBars; k++)
	{
		POINTS_GROUP pgE = { elem->iBeg, elem->iEnd, { 0, 0, 0, 0 } };
		POINTS_GROUP b = bars[k];
		if (!(b.iBeg < pgE.iBeg || b.iEnd > pgE.iEnd))
			continue;
		long sg = (short) GetGroupNumber(low, b.iBeg);
		long gBeg = low->fGroups[sg].iBeg;
		long gEnd = low->fGroups[sg].iEnd;
		if (b.iEnd - b.iBeg + 1 < (gEnd - gBeg + 2) / 3)
			continue;
		if (HWRAbs(y[b.iEnd] - y[b.iBeg]) < 0x3c)
		{
			long d = (short) Distance8(x[b.iBeg], y[b.iBeg], x[b.iEnd], y[b.iEnd]);
			long sum = 0;
			for (long i = (short) gBeg; i < gEnd; i = (short) (i + 1))
				sum = (short) (sum + Distance8(x[i], y[i], x[i + 1], y[i + 1]));
			if (d < (sum >> 1))
				continue;
		}
		if (HWRAbs(CurvMeasure(x, y, b.iBeg, b.iEnd, -1)) > 5)
			continue;
		PS_point_type p;
		if (Find_Cross(low, &p, &pgE, &b) != 1)
			continue;
		long kBeg = bars[k].iBeg;
		long kEnd = bars[k].iEnd;
		long g2 = (short) GetGroupNumber(low, kBeg);
		if (g2 == lastGrp)
		{
			if (kEnd - kBeg < last.iEnd - last.iBeg)
				continue;
			last = bars[k];
			count--;
		}
		else
		{
			if (lastGrp != -2)
			{
				short lastYMin, lastYMax, yMin, yMax;
				yMinMax(low->fGroups[lastGrp].iBeg, low->fGroups[lastGrp].iEnd, y, &lastYMin, &lastYMax);
				yMinMax(gBeg, gEnd, y, &yMin, &yMax);
				long h1 = lastYMax - lastYMin;
				long h2 = yMax - yMin;
				if (!(h1 >= (h2 >> 1) && h2 >= (h1 >> 1)))
				{
					elem->ipoint0 = -2;
					elem->ipoint1 = -2;
					return;
				}
			}
			lastGrp = g2;
			last = bars[k];
		}
		if (count == 0)
			elem->ipoint0 = b.iBeg;
		else if (count == 1)
			elem->ipoint1 = b.iBeg;
		else
		{
			elem->ipoint0 = -2;
			elem->ipoint1 = -2;
			return;
		}
		count++;
	}
}


// ROM 0x0032e78c FantomSt__FPsN21P9BUF_DESCRT4sT6Uc
// The points of a stick (7) or a dot (8) from iBeg to iEnd replaced in
// the trace by a straight line: a stick's from its leftmost point to its
// rightmost (its highest to its lowest when those are level), a dot's
// from the top right of its box to the bottom left (which are written
// into its two ends first).  The trace is rebuilt through the two
// working buffers; *count is how many points it has.  ==> 0
long
FantomSt(short* count, short* x, short* y, low_buffer* bufX, low_buffer* bufY, short iBeg, short iEnd, UByte mark)
{
	long iB = iBeg;
	long iE = iEnd;
	short* nx = bufX->ptr;
	short* ny = bufY->ptr;
	if (iE - iB + 1 < 3)
		return 0;
	long n = *count;
	long from = ixMin(iB, iE, x, y);
	long xFrom = x[from];
	long to = ixMax(iB, iE, x, y);
	long lo, hi;
	if (mark == 7)
	{
		if (x[to] == xFrom)
		{
			from = iYup_range(y, iB, iE);
			to = iYdown_range(y, iB, iE);
		}
		if (from < to)
		{
			lo = from;
			hi = to;
		}
		else
		{
			hi = from;
			lo = to;
		}
	}
	else
	{
		xMinMax(iB, iE, x, y, &x[iE], &x[iB]);
		yMinMax(iB, iE, y, &y[iB], &y[iE]);
		hi = iE;
		lo = iB;
	}
	memset(nx, 0, bufX->size << 1);
	memset(ny, 0, bufY->size << 1);
	memcpy(nx, x, iB << 1);
	memcpy(ny, y, iB << 1);
	nx[iB] = x[lo];
	ny[iB] = y[lo];
	long xe = x[hi];
	long ye = y[hi];
	long dx = xe - nx[iB];
	long dy = ye - ny[iB];
	long len = HWRMathILSqrt(LAdd(LMul(dy, dy), LMul(dx, dx)));
	long step = len / (iE - iB);
	long x0 = nx[iB];
	long y0 = ny[iB];
	long t = step;
	for (long i = iB; i < iE - 1; )
	{
		// DEVIATION: a line of no length is a division by nought, which
		// the ROM's __rt_sdiv traps on; the host leaves the points at the
		// start
		long vx = (len != 0) ? LMul(t, dx) / len : 0;
		i++;
		nx[i] = (short) (vx + x0);
		long vy = (len != 0) ? LMul(t, dy) / len : 0;
		ny[i] = (short) (vy + y0);
		t += step;
	}
	nx[iE] = (short) xe;
	ny[iE] = (short) ye;
	long rest = (n - iE) * 2;
	memcpy(&nx[iE + 1], &x[iE + 1], rest);
	memcpy(&ny[iE + 1], &y[iE + 1], rest);
	long all = (n + 1) * 2;
	memcpy(x, nx, all);
	memcpy(y, ny, all);
	return 0;
}


// ROM 0x0032be74 Recount__FP8low_type
// The stroke descriptions' points taken back through the filter's map
// (buffer 2) to the trace as it was given; a piece ending more than one
// point before the next begins is taken to end halfway to it, and the
// next to begin there.  ==> 0, 1 for no descriptions.
long
Recount(low_type* low)
{
	short* map = low->fBuffers[2].ptr;
	_SDS_CONTROL_TYPE* control = low->fSDS;
	_SDS_TYPE* all = control->pSDS;
	long n = control->lenSDS;
	long mid = -2;
	Boolean carried = false;
	if (all == nil)
		return 1;
	for (long k = 0; k < n; k++)
	{
		_SDS_TYPE* s = &all[k];
		long a = s->iBeg;
		long b = s->iEnd;
		Boolean headOrTail = (s->mark == 0 && (s->attr == 0x10 || s->attr == 0x20));
		if (!headOrTail)
		{
			long nextBeg = s[1].iBeg;
			if (carried)
				a = mid;
			if (nextBeg - b > 1)
			{
				mid = (b + nextBeg) >> 1;
				b = mid;
				carried = true;
			}
			else
				carried = false;
		}
		s->iBeg = map[a];
		s->iEnd = map[b];
		s->iA = map[s->iA];
		s->iB = map[s->iB];
		s->iMax = map[s->iMax];
	}
	return 0;
}


// ROM 0x003298d8 Pict__FP8low_type
// The strokes looked at one by one against the line's heights: each is
// described (StrElements) and judged a stick (7), a dot (8), a stick
// with a hatch cut off (HatchureS: the rest of the stroke is judged as a
// stroke of its own) or a letter's body with a crossing in it (InStr);
// a stroke's arcs are found (SlashArcs), and a stick or a dot is drawn
// straight in the trace (FantomSt) and put in the list between a stroke
// start and end of its own, with where other strokes cross it
// (FillCross).  The stroke descriptions are then taken back to the
// trace as it was given (Recount).  ==> 0, 1 for no room.
long
Pict(low_type* low)
{
	short* x = low->fX;
	short* y = low->fY;
	POINTS_GROUP* groups = low->fGroups;
	long nGroups = low->fLenGroups;
	short heights[11];
	SPEC_TYPE e;
	long failed = 0;
	short savedII = low->fII;
	long f5c = low->f5c;
	InitSpeclElement(&e);
	BildHigh(low->fBox.top, low->fBox.bottom, heights);
	// DEVIATION: room for 80 sticks of the host's size (the ROM's 0x3c0)
	low->fBars = (POINTS_GROUP*) HWRMemoryAlloc(80 * sizeof(POINTS_GROUP));
	if (low->fBars == nil)
		failed = 1;
	else
		VertSticksSelector(low);
	for (long i = 0; failed == 0 && i < nGroups; i++)
	{
		_SDS_TYPE* head = &low->fSDS->pSDS[low->fSDS->lenSDS];
		long prev;
		long cut;
		if (e.other == 2)
		{
			prev = -2;
			cut = 1;
		}
		else
		{
			cut = 0;
			prev = (i <= 0) ? -2 : i - 1;
		}
		long gBeg = groups[i].iBeg;
		long gEnd = groups[i].iEnd;
		short bottom, top;
		long r;
		RelHigh(y, gBeg, gEnd, heights, &bottom, &top);
		InitSpeclElement(&e);
		e.iBeg = (short) gBeg;
		e.iEnd = (short) gEnd;
		e.ipoint0 = (short) prev;
		e.ipoint1 = -2;
		e.code = (UByte) top;
		e.attr = (UByte) bottom;
		e.mark = 0;
		if (StrElements(low, &e, heights) == 1)
			goto fail;
		r = SPDClass(low, 2, &e, head);
		if (r == 1)
			goto fail;
		if (r != 7)
		{
			r = Dot(low, &e, head);
			if (r == 1)
				goto fail;
			if (r != 8 && cut != 1)
			{
				if (HatchureS(low, &e, heights) == 1)
					goto fail;
				if (e.other == 2)
				{
					if (InitGroupsBorder(low, 1) != 0)
						goto fail;
					nGroups = low->fLenGroups;
				}
				else if (InStr(low, head, &e, heights) == 1)
					goto fail;
			}
		}
		{
			long m = e.mark;
			if ((m == 0 || m == 5) && i < f5c && e.other != 2)
				SlashArcs(low, gBeg, gEnd);
			else if (m == 7 || m == 8)
			{
				FantomSt(&savedII, x, y, &low->fBuffers[0], &low->fBuffers[1], e.iBeg, e.iEnd, (UByte) m);
				short at = e.iBeg;
				if (Mark(low, 0x10, 0, 0, 0, at, at, at, at) == 1)
					goto fail;
				FillCross(low, &e);
				if (MarkSpecl(low, &e) == 1)
					goto fail;
				at = e.iEnd;
				if (Mark(low, 0x20, 0, 0, 0, at, at, at, at) == 1)
					goto fail;
			}
		}
		continue;
	fail:
		failed = 1;
		break;
	}
	if (low->fBars != nil)
		HWRMemoryFree((Ptr) low->fBars);
	low->fII = savedII;
	if (failed == 0)
		Recount(low);
	return failed;
}
