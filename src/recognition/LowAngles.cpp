/*
	File:		LowAngles.cpp

	Contains:	The cursive reader's low level: angl, AnalyzeLowData's
				corner finder.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	After Pict has drawn the dashes and dots straight, angl walks the
	trace looking for corners: stretches where the points six either side
	of a point are close together (the trace doubles back) and the turn
	seen from four points either side is sharp.  Each is marked an angle
	(0x0b) at its sharpest point, with the way it opens as `other` (0x10,
	0x20, 0x40 or 0x80) and whether it is a real turn back as `attr`.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "LowLevel.h"
#include <string.h>


// ROM 0x00307ba8 cos_vect__FiN31PsT5
// The cosine (hundredths) between the steps from point a to b and from
// c to d.
long
cos_vect(long a, long b, long c, long d, short* x, short* y)
{
	return cos_pointvect(x[a], y[a], x[b], y[b], x[c], y[c], x[d], y[d]);
}


// ROM 0x002aa418 angle_direction__FsN21
// Which way a corner opens, from where the middle of its two arms lies
// from its point: 0x10, 0x20, 0x40 or 0x80 by the signs of 3.73 dx + dy
// and 3.73 dx - dy.  ROM QUIRK: the writing's slant is passed and never
// used.
long
angle_direction(short dx, short dy, short /*slope*/)
{
	long t = LMul(dx, 373) / 100;
	long a = t + dy;
	long b = t - dy;
	if (a > 0)
		return (b <= 0) ? 0x10 : 0x40;
	return (b <= 0) ? 0x80 : 0x20;
}


// ROM 0x002aa2f0 store_angle__FP8low_typesN42
// The corner at point i (its arms k points either way), running from
// start to end, marked an angle (0x0b) - unless it is a single step and
// opens up or down (0x10, 0x20).  attr is 1 when the turn is back on
// itself (a cosine below nought).  ==> 0, 1 for no room.
long
store_angle(low_type* low, short i, short k, short start, short end, short best)
{
	short* x = low->fX;
	short* y = low->fY;
	if (start == end)
		return 0;
	short mx = (short) ((x[i + k] + x[i - k]) / 2 - x[i]);
	short my = (short) ((y[i + k] + y[i - k]) / 2 - y[i]);
	long dir = angle_direction(mx, my, low->fSlope);
	if (!(start + 1 < end) && (dir == 0x10 || dir == 0x20))
		return 0;
	return Mark(low, 0x0b, 0, (best < 0) ? 1 : 0, (UByte) dir, start, end, i, -2);
}


// ROM 0x002a9f7c angl__FP8low_type
// The trace's corners marked (store_angle).  Buffer 3 is the working
// array: the points of dashes and dots, the first and last 13 and those
// within six of a pen-up are left out (0x7fff); the rest first hold the
// square of the distance between the points six either side and then,
// where that is at most 1000 (the trace doubles back), the cosine of the
// turn four points either side less 100.  A run of such points with a
// cosine of -60 or more is a corner at its highest.  ==> 0, 1 for no room.
long
angl(low_type* low)
{
	long bestIndex = 0;
	long best = 0;
	long n = low->fII;
	if (n < 0x16)
		return 0;
	short* a = low->fBuffers[3].ptr;
	short* x = low->fX;
	short* y = low->fY;
	memset(a, 0, low->fBuffers[3].size << 1);
	for (SPEC_TYPE* p = low->fSpecl; p != nil; p = p->next)
		if (p->mark == 7 || p->mark == 8)
			for (long i = p->iBeg; i <= p->iEnd; i = (short) (i + 1))
				a[i] = 0x7fff;
	for (long k = 0; k <= 12; k++)
		a[n - k] = 0x7fff;
	for (long k = 0; k <= 12; k++)
		a[k] = 0x7fff;
	long last = n - 6;
	for (long i = 6; i <= last; i = (short) (i + 1))
		if (y[i] == -1)
		{
			a[i] = 0x7fff;
			for (long k = 1; k <= 6; k++)
			{
				a[i + k] = 0x7fff;
				a[i - k] = 0x7fff;
			}
		}
	long start = 0;
	Boolean inRun = false;
	Boolean open = false;
	for (long i = 0; i <= last; i = (short) (i + 1))
	{
		long v = a[i];
		if (v != 0x7fff)
		{
			long dx = (short) (x[i + 6] - x[i - 6]);
			long dy = (short) (y[i + 6] - y[i - 6]);
			v = (short) LAdd(LMul(dx, dx), LMul(dy, dy));
			a[i] = (short) v;
		}
		if (!inRun)
		{
			if (v > 1000)
				continue;
			inRun = true;
		}
		if (v > 1000)
		{
			inRun = false;
			long s = start;
			start = 0;
			if (open)
			{
				if (store_angle(low, (short) bestIndex, 4, (short) s, (short) (i - 1), (short) best) != 0)
					return 1;
				open = false;
			}
			continue;
		}
		long c = (short) cos_vect(i, i - 4, i, i + 4, x, y);
		a[i] = (short) (c - 100);
		if (!open)
		{
			if (c < -0x3c)
				continue;
			open = true;
			if (start == 0)
				start = i;
			best = c;
			bestIndex = i;
		}
		else
		{
			if (c > best)
			{
				best = c;
				bestIndex = i;
			}
			if (c < -0x3c)
			{
				open = false;
				if (store_angle(low, (short) bestIndex, 4, (short) start, (short) (i - 1), (short) best) != 0)
					return 1;
				start = 0;
			}
		}
	}
	return 0;
}
