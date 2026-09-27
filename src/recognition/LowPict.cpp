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
