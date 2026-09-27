/*
	File:		LowGeometry.cpp

	Contains:	The cursive reader's low level: the plane geometry its
				tests are made of - how far a point is from a chord,
				whether two segments cross and where, the cosine
				between two vectors.  See LowLevel.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	The engine's arithmetic is 32-bit and wraps: a product of two
	coordinates' differences is kept in a word, and sums of squares of
	long segments overflow on the machine without trapping.  LMul/LAdd
	(LowLevel.h) do the same on the host.
*/

#include "LowLevel.h"
#include "ParaGraph.h"


// ROM 0x00305cec QDistFromChord__FiN51
// The square of how far P (px, py) is from the line through A (ax, ay)
// and B (bx, by), worked out without a division that could lose it: the
// projection's quotient and remainder by |AB|^2 are kept apart, and the
// remainder's square scaled down first when it would overflow.  A and B
// the same: the square of |AP|.
long
QDistFromChord(long ax, long ay, long bx, long by, long px, long py)
{
	long dpx = px - ax;
	long dpy = py - ay;
	long dbx = bx - ax;
	long dby = by - ay;
	if (ax == bx && ay == by)
		return LAdd(LMul(dpx, dpx), LMul(dpy, dpy));
	long dot = LAdd(LMul(dbx, dpx), LMul(dby, dpy));
	long len2 = LAdd(LMul(dbx, dbx), LMul(dby, dby));
	long q = dot / len2;				// (__rt_sdiv: the quotient and the remainder)
	long rem = dot % len2;
	long r;
	long a = HWRLAbs(rem);
	if (a <= 0x7fff)
		r = -LMul(rem, rem) / len2;
	else
	{
		long d = len2;
		while (a >= 0x7fff && d > 0x40)
		{
			a >>= 1;
			d = (d + 2) >> 2;
		}
		if (d > 0x40)
			r = -LMul(a, a) / d;
		else
			r = LMul(a, -(a + (d >> 1)) / d);
		if (rem < 0)
			r = -r;
	}
	r = LAdd(LMul(dpy, dpy), r);
	r = LAdd(r, -LMul(q, dot));
	r = LAdd(LMul(dpx, dpx), r);
	return LAdd(r, -LMul(rem, q));
}


// ROM 0x003060c8 is_cross__FsN71
// Whether segment A-B (xa, ya)-(xb, yb) crosses segment C-D: the two
// parameters of the crossing point, as numerator over a common
// denominator, both between nought and one.  Parallel: no.
long
is_cross(short xa, short ya, short xb, short yb, short xc, short yc, short xd, short yd)
{
	long abx = xb - xa;
	long aby = yb - ya;
	long cdx = xd - xc;
	long cdy = yd - yc;
	long cax = xa - xc;
	long cay = ya - yc;
	long num = LMul(cay, cdx) - LMul(cax, cdy);
	long den = LMul(abx, cdy) - LMul(aby, cdx);
	if (den == 0)
		return 0;
	if ((num > 0 && den < 0) || (num < 0 && den > 0))
		return 0;
	if (HWRLAbs(num) > HWRLAbs(den))
		return 0;
	num = LMul(cax, aby) - LMul(cay, abx);
	den = -den;
	if ((num > 0 && den < 0) || (num < 0 && den > 0))
		return 0;
	if (HWRLAbs(num) <= HWRLAbs(den))
		return 1;
	return 0;
}


// ROM 0x003061dc FindCrossPoint__FsN71PsT9
// Where the line through A-B meets the line through C-D, into (*px,
// *py), rounded; 0x7fff both for parallel lines.  ==> 1 when the point
// is on both segments, else 0.  The numerator and denominator are halved
// together while either is past 0xfffe (and the denominator past 0x20),
// so the product stays in a word; a denominator still under 0x20 then
// divides first and multiplies after.  ROM QUIRK: each coordinate is
// rounded by adding half the denominator before a division that
// truncates towards nought, so a step that is negative comes out half a
// point too far up - the crossing of (0, 0)-(10, 10) and (0, 10)-(10, 0)
// is answered as (5, 6).
long
FindCrossPoint(short xa, short ya, short xb, short yb, short xc, short yc, short xd, short yd, short* px, short* py)
{
	long result = 1;
	long abx = xb - xa;
	long aby = yb - ya;
	long cdx = xd - xc;
	long cdy = yd - yc;
	long cax = xa - xc;
	long cay = ya - yc;
	long num = LMul(cay, cdx) - LMul(cax, cdy);
	long den = LMul(abx, cdy) - LMul(aby, cdx);
	if (den == 0)
	{
		*py = 0x7fff;
		*px = 0x7fff;
		return 0;
	}
	if ((num > 0 && den < 0) || (num < 0 && den > 0) || HWRLAbs(num) > HWRLAbs(den))
		result = 0;
	long t = LMul(cax, aby) - LMul(cay, abx);
	den = -den;
	if ((t > 0 && den < 0) || (t < 0 && den > 0) || HWRLAbs(t) > HWRLAbs(den))
		result = 0;
	while ((t > 0xfffe || den > 0xfffe) && den > 0x20)
	{
		t >>= 1;
		den >>= 1;
	}
	if (t > 0xfffe && den < 0x20)
	{
		long q = (t + (den >> 1)) / den;
		*px = (short) (LMul(cdx, q) + xc);
		*py = (short) (LMul(cdy, q) + yc);
	}
	else
	{
		*px = (short) ((LMul(cdx, t) + (den >> 1)) / den + xc);
		*py = (short) ((LMul(cdy, t) + (den >> 1)) / den + yc);
	}
	return result;
}


// ROM 0x00307ad8 cos_pointvect__FiN71
// The cosine between vector A-B and vector C-D, in hundredths; 0 when
// either has no length.  The lengths' product is the root of the product
// of the squares when that fits in a word, else the product of the roots.
long
cos_pointvect(long xa, long ya, long xb, long yb, long xc, long yc, long xd, long yd)
{
	long dx1 = xb - xa;
	long dy1 = yb - ya;
	long dx2 = xd - xc;
	long dy2 = yd - yc;
	long dot = LAdd(LMul(dy2, dy1), LMul(dx2, dx1));
	long l1 = LAdd(LMul(dx1, dx1), LMul(dy1, dy1));
	long l2 = LAdd(LMul(dx2, dx2), LMul(dy2, dy2));
	long len;
	if ((l1 < 0x7fff || l2 < 0x7fff)
	 && LMul((l1 - 1 + 0x8000) >> 16, l2) < 0x3fff
	 && LMul((l2 - 1 + 0x8000) >> 16, l1) < 0x3fff)
		len = HWRMathILSqrt(LMul(l2, l1));
	else
		len = LMul(HWRMathILSqrt(l1), HWRMathILSqrt(l2));
	if (len > 0)
		return LMul(dot, 100) / len;
	return 0;
}
