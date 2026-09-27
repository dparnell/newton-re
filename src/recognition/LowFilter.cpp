/*
	File:		LowFilter.cpp

	Contains:	The cursive reader's low level: the engine's integer
				roots, and the filters that thin and resample a trace.
				See LowLevel.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	A filter reads the trace in fX/fY and writes the new one into
	working buffers 0 and 1, keeping in buffer 2 or 3 which old point
	each new point came from (Errorprov starts that map in buffer 3;
	each filter reads the map from one of 2 and 3 and writes the other,
	so that it goes on naming the original points).  Every new trace
	starts and ends with a pen-up, as the old one did.
*/

#include "LowLevel.h"
#include <string.h>

extern const short			sqrtab[128];
extern const unsigned char	SQRTa[256];
extern const int			SQRTb[267];


/*------------------------------------------------------------------------------
	T h e   e n g i n e ' s   r o o t s
------------------------------------------------------------------------------*/

// ROM 0x000e6508 HWRLAbs__Fl
long
HWRLAbs(long x)
{
	if (x < 1)
		x = -x;
	return x;
}


// ROM 0x002e61c0 HWRMathISqrt__Fs
// The square root rounded to the nearest: below 0x100 straight out of
// SQRTa (sixteen times the root, less 7); above it a guess from the top
// byte's entry, walked down SQRTb (the squares) to the root below, then
// up one if the square above is nearer.
long
HWRMathISqrt(short x)
{
	long v = x;
	if (v < 0)
		return 0;
	ULong u;
	if (v < 0x100)
		u = (SQRTa[v] + 7) >> 4;
	else
	{
		long t = SQRTa[(v >> 8) & 0xff] + 3;
		if (SQRTb[t] < v)
			t = SQRTa[(v >> 8) & 0xff] + 6;
		u = (t - 1) & 0xffff;
		long sq = SQRTb[u];
		if (v < sq)
		{
			u = (u - 1) & 0xffff;
			sq = SQRTb[u];
		}
		if (sq > v)
		{
			u = (u - 1) & 0xffff;
			sq = SQRTb[u];
			if (sq > v)
				u = (u - 1) & 0xffff;
		}
		if (SQRTb[u + 1] - v < v - SQRTb[u])
			u = u + 1;
	}
	return (short) u;
}


// ROM 0x002e615c HWRMathILSqrt__Fl
// A long's root: brought under 0x8000 by quarters, its root doubled back
// as many times.  ==> 0x7fff when that overflows a short.
long
HWRMathILSqrt(long x)
{
	if (x < 0)
		return 0;
	short shift = 0;
	for ( ; x > 0x7fff; x >>= 2)
		shift = shift + 1;
	long r = HWRMathISqrt((short) x);
	r = (short) (r << (shift & 0xff));
	if (r < 0)
		r = 0x7fff;
	return r;
}


/*------------------------------------------------------------------------------
	T h e   f i l t e r s
------------------------------------------------------------------------------*/

// ROM 0x002e0f1c Errorprov__FP8low_type
// Every pen-up that follows a pen-up taken out; buffer 3 says which old
// point each one kept was.
void
Errorprov(low_type* low)
{
	short* xs = low->fBuffers[0].ptr;
	short* ys = low->fBuffers[1].ptr;
	short* x = low->fX;
	short* y = low->fY;
	short* map = low->fBuffers[3].ptr;
	short n = low->fII - 2;
	memcpy(xs, x, (n + 3) * 2);
	memcpy(ys, y, (n + 3) * 2);
	short j = 0;
	short i = 0;
	for ( ; i <= n; i++)
	{
		if (!(ys[i] == -1 && ys[i + 1] == -1))
		{
			x[j] = xs[i];
			y[j] = ys[i];
			map[j] = i;
			j++;
		}
	}
	x[j] = xs[i];
	y[j] = ys[i];
	map[j] = i;
	low->fII = j + 1;
}


// ROM 0x002e1064 Filt__FP8low_typesT2
// The trace resampled: a point nearer than the root of dist2 to the one
// kept before it is dropped (unless a pen-up follows it), and a gap wider
// than that is filled with points a step apart, the step being the root
// of dist2 (at least 2).  The new trace is left in buffers 0 and 1, and
// fX/fY point at them.  mode 1: the map read from buffer 2 into buffer 3,
// copied back to 2, and the special elements moved to the new points
// (PSProc); otherwise read from 3 into 2.  ==> 0.
long
Filt(low_type* low, short dist2, short mode)
{
	short* xs = low->fBuffers[0].ptr;
	short* ys = low->fBuffers[1].ptr;
	long limit = (short) (low->fBuffers[0].size - 7);
	short* x = low->fX;
	short* y = low->fY;
	short* mapIn;
	short* mapOut;
	if (mode == 1)
	{
		mapIn = low->fBuffers[2].ptr;
		mapOut = low->fBuffers[3].ptr;
	}
	else
	{
		mapIn = low->fBuffers[3].ptr;
		mapOut = low->fBuffers[2].ptr;
	}
	long step = HWRMathISqrt(dist2);
	if (step < 2)
		step = 2;
	short j = 0;
	short i = 0;
	xs[0] = 0;
	ys[0] = -1;
	if (mapIn != nil)
		mapOut[0] = mapIn[0];
	long n = (short) (low->fII - 2);
	while (!(n < i || limit <= j))
	{
		i++;
		long yi = y[i];
		if (yi == -1)
		{
			j++;
			xs[j] = x[i];
			ys[j] = -1;
		}
		else if (ys[j] == -1)
		{
			j++;
			xs[j] = x[i];
			ys[j] = y[i];
		}
		else
		{
			long dx = (short) (x[i] - xs[j]);
			long dy = (short) (yi - ys[j]);
			long d2 = dy * dy + dx * dx;
			if (dist2 < d2)
			{
				long d = (d2 < 0x80) ? sqrtab[d2] : (short) HWRMathILSqrt(d2);
				long bx = xs[j];
				long by = ys[j];
				for (long k = step; k < d && j < limit; k = (short) (k + step))
				{
					j++;
					xs[j] = k * dx / d + bx;
					ys[j] = k * dy / d + by;
					if (mapIn != nil)
					{
						if (k < d >> 1)
							mapOut[j] = mapOut[j - 1];
						else
							mapOut[j] = mapIn[i];
					}
				}
				j++;
				xs[j] = x[i];
				ys[j] = y[i];
			}
			else if (i <= n && y[i + 1] == -1)
			{
				// the last point before a pen-up is kept, over the one
				// before it when that is very near
				if (((dist2 + 2) >> 2) < d2)
					j++;
				xs[j] = x[i];
				ys[j] = y[i];
			}
			else
				continue;
		}
		if (mapIn != nil)
			mapOut[j] = mapIn[i];
	}
	if (ys[j] == -1)
		xs[j] = 0;
	else
	{
		j++;
		xs[j] = 0;
		ys[j] = -1;
		if (mapIn != nil)
			mapOut[j] = mapIn[i];
	}
	low->fII = j + 1;
	low->fX = xs;
	low->fY = ys;
	if (mode == 1)
	{
		memcpy(mapIn, mapOut, low->fII << 1);
		PSProc(low, j);
	}
	low->fX[low->fII] = 0;
	low->fY[low->fII] = 0;
	return 0;
}


// ROM 0x002e15f4 PreFilt__FsP8low_type
// The points nearer than the root of dist2 to the one kept before them
// dropped (but the last before a pen-up), the new trace copied back into
// fX/fY and the map read from buffer 3 into buffer 2.  ==> 0.
long
PreFilt(short dist2, low_type* low)
{
	short* xs = low->fBuffers[0].ptr;
	short* ys = low->fBuffers[1].ptr;
	short* mapIn = low->fBuffers[3].ptr;
	short* mapOut = low->fBuffers[2].ptr;
	long limit = (short) (low->fBuffers[0].size - 7);
	short* x = low->fX;
	short* y = low->fY;
	short j = 0;
	short i = 0;
	xs[0] = 0;
	ys[0] = -1;
	if (mapIn != nil)
		mapOut[0] = mapIn[0];
	long n = (short) (low->fII - 2);
	while (!(n < i || limit <= j))
	{
		i++;
		long yi = y[i];
		if (yi == -1)
		{
			j++;
			xs[j] = 0;
			ys[j] = -1;
		}
		else if (ys[j] == -1)
		{
			j++;
			xs[j] = x[i];
			ys[j] = y[i];
		}
		else
		{
			long dx = (short) (x[i] - xs[j]);
			long dy = (short) (yi - ys[j]);
			long d2 = dx * dx + dy * dy;
			if (dist2 < d2)
			{
				j++;
				xs[j] = x[i];
				ys[j] = y[i];
			}
			else if (i <= n && y[i + 1] == -1)
			{
				if (((dist2 + 2) >> 2) < d2)
					j++;
				xs[j] = x[i];
				ys[j] = y[i];
			}
			else
				continue;
		}
		if (mapIn != nil)
			mapOut[j] = mapIn[i];
	}
	if (ys[j] == -1)
		xs[j] = 0;
	else
	{
		j++;
		xs[j] = 0;
		ys[j] = -1;
		if (mapIn != nil)
			mapOut[j] = mapIn[i];
	}
	low->fII = j + 1;
	memcpy(x, xs, low->fII << 1);
	memcpy(y, ys, low->fII << 1);
	x[low->fII] = 0;
	y[low->fII] = 0;
	return 0;
}


// The ipoints of an element moved, when it has both.
static void
PSProcPoints(SPEC_TYPE* elem, short* map, short* y, short ii)
{
	if (elem->ipoint0 != -2 && elem->ipoint1 != -2)
	{
		elem->ipoint0 = NewIndex(map, y, elem->ipoint0, ii, 1);
		elem->ipoint1 = NewIndex(map, y, elem->ipoint1, ii, 1);
	}
}


// ROM 0x002e19a0 PSProc__FP8low_types
// The special elements moved from the old points to the new ones (buffer
// 2 being the map); an element whose start went is dropped with those
// after it, one whose end went is cut at the new trace's end.  Kinds 7
// and 8 have the elements either side of them marking their ends, which
// move with them.  n is the new trace's last index.  ==> 1 when the
// filter stopped short of the old trace's end (n not its last point).
long
PSProc(low_type* low, short n)
{
	short ii = low->fII;
	SPEC_TYPE* specl = low->fSpecl;
	short* y = low->fY;
	short* map = low->fBuffers[2].ptr;
	long last = ii - 1;
	long result = 0;
	long nn = n;
	if (last != nn)
	{
		result = 1;
		if (last < nn)
			nn = (short) last;
	}
	short end = nn - 1;
	for (short k = 0; k < low->fLenSpecl; k++)
	{
		SPEC_TYPE* elem = &specl[k];
		UByte mark = elem->mark;
		if (mark == 0x10 || mark == 0x20 || mark == 0)
			continue;
		long b = NewIndex(map, y, elem->iBeg, ii, 0);
		long e = NewIndex(map, y, elem->iEnd, ii, 2);
		if (end < e)
			e = -2;
		if (b == -2)
		{
			// the element and everything after it gone
			SPEC_TYPE* cleared;
			if (mark == 5)
			{
				low->fLenSpecl = k;
				low->fLastSpecl = k - 1;
				specl[k - 1].next = nil;
				cleared = elem;
			}
			else
			{
				low->fLenSpecl = k - 1;
				low->fLastSpecl = k - 2;
				specl[k - 2].next = nil;
				cleared = &specl[k - 1];
			}
			InitSpeclElement(cleared);
			InitSpeclElement(elem);
			return result;
		}
		if (e == -2)
		{
			// cut at the end of the new trace, and the list ended there
			if (mark == 5)
			{
				elem->iBeg = b;
				elem->iEnd = end;
				PSProcPoints(elem, map, y, ii);
				low->fLenSpecl = k + 1;
				low->fLastSpecl = k;
				elem->next = nil;
				InitSpeclElement(&specl[k + 1]);
				return result;
			}
			if (mark == 8 || mark == 7)
			{
				elem->iBeg = b;
				elem->iEnd = end;
				specl[k - 1].iBeg = b;
				specl[k - 1].iEnd = b;
				specl[k + 1].iBeg = end;
				specl[k + 1].iEnd = end;
				if (mark == 7)
					PSProcPoints(elem, map, y, ii);
				low->fLenSpecl = k + 2;
				low->fLastSpecl = k + 1;
				specl[k + 1].next = nil;
				InitSpeclElement(&specl[k + 2]);
				return result;
			}
		}
		else if (mark == 5)
		{
			elem->iBeg = NewIndex(map, y, elem->iBeg, ii, 1);
			elem->iEnd = NewIndex(map, y, elem->iEnd, ii, 1);
			PSProcPoints(elem, map, y, ii);
		}
		else if (mark == 8 || mark == 7)
		{
			elem->iBeg = b;
			elem->iEnd = e;
			specl[k - 1].iBeg = b;
			specl[k - 1].iEnd = b;
			specl[k + 1].iBeg = e;
			specl[k + 1].iEnd = e;
			if (mark == 7)
				PSProcPoints(elem, map, y, ii);
		}
	}
	return result;
}
