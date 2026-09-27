/*
	File:		LowSide.cpp

	Contains:	The cursive reader's low level: FindSideExtr, the side
				extrema, and the curve measures it is made of.  See
				LowLevel.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	Between a top and the next bottom (or a bottom and the next top) the
	pen goes down or up one side of a letter.  FindSideExtr asks of each
	such side whether it bends out to one side on the way - the bowl of a
	c, the back of an s - and where (SideExtr): the bend is found on the
	filtered trace and then judged on the trace as it was first filled
	(buffer 2 maps a filtered point back to it), where a real corner shows
	as a triangle the path fills (IsTriangledPath).  A side that bends
	moves the top at the start of a stroke, or the bottom at the end of
	one, halfway towards the bend.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "LowLevel.h"
#include "ParaGraph.h"


// ROM 0x00305fd4 brk_right__FPsiT2
// The first pen-up from i to j, or j + 1 for none.
long
brk_right(short* y, long i, long j)
{
	while (i <= j && y[i] != -1)
		i++;
	return i;
}


// ROM 0x00307820 TriangleSquare__FPsT1iN23
// The signed area of the triangle of points a, b and c (in that order
// along the trace, none of them a pen-up); 0 otherwise.
long
TriangleSquare(short* x, short* y, long a, long b, long c)
{
	long ya = y[a];
	long yb;
	long yc;
	if (ya == -1 || (yb = y[b]) == -1 || (yc = y[c]) == -1)
		return 0;
	if (!(a <= b && b <= c))
		return 0;
	long xa = x[a];
	long xb = x[b];
	long xc = x[c];
	long s = LMul(xb - xa, yb + ya);
	s = LAdd(LMul(yc + yb, xc - xb), s);
	s = LAdd(LMul(ya + yc, xa - xc), s);
	return -s / 2;
}


// ROM 0x00307744 ClosedSquare__FPsT1iT3T1
// The signed area the trace from i to j encloses with the chord back from
// j to i (twice the shoelace sum, halved).  flag says why there is none:
// 1 i after j, 2 a pen-up on the way (0x7fff answered in both cases).
long
ClosedSquare(short* x, short* y, long i, long j, short* flag)
{
	*flag = 0;
	if (i > j)
	{
		*flag = 1;
		return 0x7fff;
	}
	long yi = y[i];
	if (yi == -1)
	{
		*flag = 2;
		return 0x7fff;
	}
	if (i == j)
		return 0;
	long s = LMul(x[j] - x[i], y[j] + yi);
	for (long k = i + 1; i < j; i++, k++)
	{
		long yk = y[k];
		if (yk == -1)
		{
			*flag = 2;
			return 0x7fff;
		}
		s = LAdd(s, -LMul(x[k] - x[i], y[i] + yk));
	}
	return s / 2;
}


// ROM 0x00306524 IsTriangledPath__FPsT1iN23
// Whether the trace from i to j, round k (the point furthest from the
// chord when k < 1), is a corner: the triangle i, k, j is big for the
// chord (ten times its length at least) and the area the trace encloses
// is no more than a third bigger than it.
long
IsTriangledPath(short* x, short* y, long i, long j, long k)
{
	if (k < 1)
		k = iMostFarFromChord(x, y, i, j);
	long area = TriangleSquare(x, y, i, k, j);
	if (area < 0)
		area = -area;
	if (area < Distance8(x[i], y[i], x[j], y[j]) * 10)
		return 0;
	short flag;
	long closed = HWRLAbs(ClosedSquare(x, y, i, j, &flag));
	if (closed >= (area + 1) / 3 + area)
		return 0;
	return 1;
}


// ROM 0x00306660 (unnamed) - after IsTriangledPath
// Whether the path from i to j round k is *not* one smooth curve: either
// half is straight, or either half bends the other way from the whole.
static long
HalvesDisagree(short* x, short* y, long i, long j, long k)
{
	long whole = CurvMeasure(x, y, i, j, k);
	long first = CurvMeasure(x, y, i, k, -1);
	long second = CurvMeasure(x, y, k, j, -1);
	if (first == 0 || second == 0)
		return 1;
	if ((first >= 0) != (whole >= 0))
		return 1;
	if ((second >= 0) == (whole >= 0))
		return 0;
	return 1;
}


// ROM 0x0030660c (unnamed) - after IsTriangledPath
// How much a bend is to be believed from the other half's bend c1 against
// the whole's c0, when the side's width a is small against its depth d:
// 0 when a is at least half d, 10 when it is a quarter or less, 5 when the
// other half is straight, 0 when the two bend opposite ways, 10 otherwise.
static long
BendAllowance(long c0, long c1, long a, long d)
{
	if (a >= d >> 1)
		return 0;
	if (a <= (d + 2) >> 2)
		return 10;
	if (c1 == 0)
		return 5;
	if (c0 > 0 && c1 < 0)
		return 0;
	if (c0 < 0 && c1 > 0)
		return 0;
	return 10;
}


// ROM 0x00306c44 iMostCurvedPoint__FPsT1iN23
// The point from i (at least 3) to j - 2 where the trace turns most
// sharply (the cosine between the steps two points either side), no
// pen-up within two of it; with sgn nonzero, only a turn the same way
// as sgn (against the direction from i to j - 2) counts.  The middle of
// a stretch too short to look along.
long
iMostCurvedPoint(short* x, short* y, long i, long j, long sgn)
{
	if (i <= 2)
		i = 3;
	long e = j - 2;
	if (i + 1 >= e)
		return (i + e) >> 1;
	long dxA = x[e] - x[i];
	long dyA = y[e] - y[i];
	long best = -100;
	long bestI = i;
	for (long k = i; k <= e; k++)
	{
		if (y[k] == -1 || y[k + 1] == -1 || y[k + 2] == -1)
		{
			k++;
			continue;
		}
		if (y[k - 1] == -1 || y[k - 2] == -1)
			continue;
		long c = cos_vect(k, k - 2, k, k + 2, x, y);
		if (c <= best)
			continue;
		if (sgn != 0)
		{
			long cm = CurvMeasure(x, y, k - 2, k + 2, k);
			long p = LAdd(LMul(x[k + 2] - x[k - 2], dxA), LMul(y[k + 2] - y[k - 2], dyA));
			Boolean ok;
			if (p >= 0)
				ok = (sgn >= 0) == (cm >= 0);
			else
				ok = (sgn >= 0) != (cm >= 0);
			if (!ok)
				continue;
		}
		best = c;
		bestI = k;
	}
	return bestI;
}


// ROM 0x00306734 SideExtr__FPsT1iN23N31PiUi
// Whether the side from i to j bends out, and which way: 1 or 3 to the
// left on the way (3 the surer), 2 or 4 to the right, 0 not at all; *k is
// the bend.  x and y are the filtered trace, x0 and y0 the trace as first
// filled and map the filtered points' places in it.  A side less than 16
// deep does not bend.  With strict, a bend must be a real corner.
long
SideExtr(short* x, short* y, long i, long j, long slope, short* x0, short* y0, short* map, long* k, ULong strict)
{
	long dyij = y[i] - y[j];
	long mi = map[i];
	long mj = map[j];
	long far = iMostFarFromChord(x, y, i, j);
	long curv = CurvMeasure(x, y, i, j, far);
	long bend = iMostCurvedPoint(x, y, (i + far) >> 1, (far + j) >> 1, curv);
	long kk = (far + bend * 2 + 1) / 3;
	long mk = map[kk];
	*k = kk;
	if (HWRAbs(dyij) < 0x10)
		return 0;
	// ROM QUIRK: only a slant to the left is brought down to a quarter
	if (slope < 0)
		slope = (slope + 2) >> 2;
	long d1 = y[kk] - y[i];
	long d2 = y[kk] - y[j];
	long e1 = SlopeShiftDx((short) d1, slope) + (x[kk] - x[i]);
	long a1 = (short) HWRAbs(e1);
	long e2 = (x[j] - x[kk]) - SlopeShiftDx((short) d2, slope);
	long a2 = (short) HWRAbs(e2);
	if (d1 < 0)
		d1 = -d1;
	if (d2 < 0)
		d2 = -d2;
	long cf = HalvesDisagree(x0, y0, mi, mj, mk);
	long result;
	Boolean toRight;
	if (d2 > (d1 + 2) >> 2 && (strict != 0 || cf == 0 || d2 > d1 >> 1))
	{
		// L948
		if (d2 > d1)
			toRight = false;
		else if (a1 <= (a2 + 4) >> 3)
			toRight = true;
		else if (strict != 0 || cf == 0 || (a2 + 2) / 5 < a1)
			toRight = false;
		else
			toRight = true;
	}
	else
		toRight = true;
	if (toRight)
	{
		if (IsTriangledPath(x0, y0, mi, mj, mk) == 0)
			return 0;
		if ((e1 < 0 && e2 > 0) || !((a2 * 2 + 1) / 3 < a1))
			result = 2;
		else if ((a2 + 1) / 3 + a2 >= a1)
			result = 4;
		else
			return 0;
	}
	else
	{
		if (d1 > (d2 + 2) >> 2 && (strict != 0 || cf == 0 || d1 > d2 >> 1))
			return 0;
		if (IsTriangledPath(x0, y0, mi, mj, mk) == 0)
			return 0;
		if ((e1 >= 0) != (e2 >= 0) || !((a1 * 2 + 1) / 3 < a2))
			result = 1;
		else if ((a1 + 1) / 3 + a1 < a2)
			return 0;
		else
			result = 3;
	}
	long dx = x[i] - x[j];
	long w;
	if (dyij == 0)
		w = 0;
	else
	{
		w = LMul(LMul(dx, dx), 3) / LMul(dyij, dyij);
		if (w > 10)
			w = 10;
	}
	long c0 = CurvMeasure(x0, y0, mi, mj, mk);
	long c1;
	long allowance;
	long a;
	long d;
	if (result == 1)
	{
		c1 = CurvMeasure(x0, y0, mi, mk, -1);
		allowance = BendAllowance(c0, c1, a1, d2);
		a = a1;
		d = d1;
	}
	else
	{
		c1 = CurvMeasure(x0, y0, mk, mj, -1);
		allowance = BendAllowance(c0, c1, a2, d1);
		a = a2;
		d = d2;
	}
	long s = HWRAbs(c0) + w;
	if (s - allowance < 10)
	{
		if (s < 6 || (c0 > 0 && c1 >= 7) || (c0 < 0 && c1 <= -7)
		 || (HWRAbs(c1) >= 4 && a - ((a + 2) >> 2) < d))
			result = 0;
		else if (result == 1)
			result = 3;
		else if (result == 2)
			result = 4;
	}
	return result;
}


// ROM 0x00303584 FindSideExtr__FP8low_type
// Each side between a top and a bottom (either way round) with no pen-up
// in it looked at for a bend (SideExtr); a bend to the left moves a top
// that starts its stroke halfway towards it, one to the right a bottom
// that ends its stroke.  ==> 1 (AnalyzeLowData goes on only when it is
// not nought).
long
FindSideExtr(low_type* low)
{
	short* y = low->fY;
	short* map = low->fBuffers[2].ptr;
	short* x = low->fX;
	SPEC_TYPE* next = low->fSpecl->next;
	if (next == nil)
		return 1;
	for (;;)
	{
		SPEC_TYPE* cur = next;
		next = cur->next;
		if (next == nil)
			return 1;
		if (!((cur->mark == 1 && next->mark == 3) || (cur->mark == 3 && next->mark == 1)))
			continue;
		if (!(cur->iEnd < next->iBeg))
			continue;
		if (!(next->iBeg < brk_right(y, cur->iEnd, next->iBeg)))
			continue;
		long k;
		long r = SideExtr(x, y, (cur->iBeg + cur->iEnd) >> 1, (next->iBeg + next->iEnd) >> 1, 0,
						  low->fXInitial, low->fYInitial, map, &k, 1);
		if (r == 1 || r == 3)
		{
			if (cur->prev->mark == 0x10 && cur->iEnd < k)
				cur->iEnd = (short) ((cur->iEnd + k) >> 1);
		}
		else if (r == 2 || r == 4)
		{
			SPEC_TYPE* q = next->next;
			if ((q == nil || q->mark == 0x20) && k < next->iBeg)
				next->iBeg = (short) ((next->iBeg + k) >> 1);
		}
	}
}
