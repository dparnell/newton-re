/*
	File:		LowXrFeatures.cpp

	Contains:	The cursive reader's low level: FillXrFeatures, the last
				thing exchange does - each xr's shift, its height class and
				its direction.  See LowLevel.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	What the reader compares an xr against a letter's prototype by, beyond
	its type: `xr_type_merits` says of each type whether it is an upper or
	a lower extremum, a stroke's end, a crossing or a link, and from that
	FillSHR finds each xr's four neighbours that bracket it - the upper and
	lower extrema before and after - and records how tall the piece it is
	in is against its neighbour's (the height class, +3, over
	kSHRRatioLimits) and how far it is shifted across once the writing's
	slant is taken out (+4, over kSHRShiftLimits), the shifts rescaled
	afterwards when the word's are on average large; FillOrients records
	the direction (+5, one of 32) the trace leaves or reaches it in over
	about a third of the letters' height, GetAngle turning a slope into one
	of eight steps a quadrant.

	All of it was read from the disassembly.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "LowLevel.h"
#include "ParaGraph.h"
#include "XrDomains.h"
#include "CursiveReader.h"

#include <string.h>


static inline xrd_el_type*
Xr(xrdata_type* xr, long i)
{
	return (xrd_el_type*) xr->fElements + i;
}


// ROM 0x0027e508 GetXrHT__FP11xrd_el_type
long
GetXrHT(xrd_el_type* xr)
{
	long m = xr_type_merits[xr->type];
	long h = 0;
	if (m & 1)
		h = 2;
	if (m & 2)
		h = 1;
	if (m & 0x10)
		h = 4;
	return h;
}


// ROM 0x0027e540 GetXrMovable__FP11xrd_el_type
long
GetXrMovable(xrd_el_type* xr)
{
	return ((UShort) xr_type_merits[xr->type] & 0x20) != 0;
}


// ROM 0x0027e840 IsXrLink__FP11xrd_el_type
Boolean
IsXrLink(xrd_el_type* xr)
{
	return ((UShort) xr_type_merits[xr->type] & 0x40) != 0;
}


// ROM 0x0027e868 GetXrMetrics__FP11xrd_el_type
long
GetXrMetrics(xrd_el_type* xr)
{
	return xr_type_merits[xr->type];
}


// ROM 0x0027e568 GetAngle__FiT1
// A direction as one of 32: the slope's step (ratio_to_angle) within its
// quadrant, the quadrant by the signs; anything past 0x1f is 0.
long
GetAngle(long dx, long dy)
{
	long quadrant = ((dx < 0) ? 1 : 0) + ((dy < 0) ? 2 : 0);
	long ratio;
	if (dx == 0)
		ratio = (dy != 0) ? 32000 : 0;
	else
		ratio = (HWRAbs(dy) * 100) / HWRAbs(dx);
	long k = 0;
	while (k < 8 && ratio_to_angle[k] <= ratio)
		k++;
	long a;
	switch (quadrant)
	{
	case 0:		a = k; break;
	case 1:		a = 0x10 - k; break;
	case 2:		a = 0x20 - k; break;
	default:	a = k + 0x10; break;
	}
	if (a > 0x1f)
		a = 0;
	return a;
}


// ROM 0x0027e638 GetVect__FiP9vect_typeP13PS_point_typeN21
// The vector from point a to the first point from b on (forwards or
// backwards) further than dist from it, or to blp: the nearer of it and
// the point before when that is closer to dist.  With no such point a
// short level vector.  Backwards, the vector is turned round.  ==> 0.
long
GetVect(long forward, vect_type* v, PS_point_type* trace, long n, long dist)
{
	long step = forward ? 1 : -1;
	long x0 = trace[v->a].x;
	long y0 = trace[v->a].y;
	long i = v->b;
	long before = 0;
	long d = 0;
	Boolean found = false;
	while (i < n && i > 0)
	{
		long y = trace[i].y;
		if (y < 0)
			before = 0;
		else
		{
			long dx = x0 - trace[i].x;
			long dy = y0 - y;
			d = HWRMathILSqrt(LAdd(LMul(dx, dx), LMul(dy, dy)));
			if (d > dist || v->blp == i)
			{
				found = true;
				break;
			}
			before = d;
		}
		i += step;
	}
	if (before != 0 && dist - before < d - dist)
		i -= step;
	if (!found)
	{
		v->x1 = x0;
		v->x2 = x0 + 10;
		v->y1 = y0;
		v->y2 = y0;
	}
	else
	{
		v->y1 = y0;
		v->x1 = x0;
		v->x2 = trace[i].x;
		v->y2 = trace[i].y;
	}
	if (!forward)
	{
		long t = v->x1;
		v->x1 = v->x2;
		v->x2 = t;
		t = v->y1;
		v->y1 = v->y2;
		v->y2 = t;
	}
	return 0;
}


// ROM 0x0027e7a8 GetBlp__FiP9vect_typeT1P11xrdata_type
// The point of the nearest extremum or stroke end past xr i (forwards or
// backwards): the vector from i must stop there.  ==> 0.
long
GetBlp(long forward, vect_type* v, long i, xrdata_type* xr)
{
	v->blp = 0;
	long step = forward ? 1 : -1;
	for (long k = i + step; k > 0 && k < xr->fLength; k += step)
	{
		xrd_el_type* e = Xr(xr, k);
		if (GetXrHT(e) != 0)
		{
			long p = XrGetH(e->hotpoint);
			if (p == 0)
				p = (XrGetH(e->begpoint) + XrGetH(e->endpoint)) / 2;
			v->blp = p;
			return 0;
		}
	}
	return 0;
}


// ROM 0x0027e8fc GetCurSlope__FiP13PS_point_type
// The writing's slant in hundredths: the steps along the trace between
// points more than ten apart, those steeper than half a right angle each
// way, summed (a downward one turned round and weighed eight times) -
// sideways over up, the up starting at 300.  0 for a trace under ten
// points.
long
GetCurSlope(long n, PS_point_type* trace)
{
	if (n < 10)
		return 0;
	long sumX = 0;
	long sumY = 300;
	long last = 0;
	for (long i = 0; i < n; i++)
	{
		long x = trace[i].x;
		long y = trace[i].y;
		if (y < 0)
		{
			last = i + 1;
			continue;
		}
		long dx = x - trace[last].x;
		long adx = HWRAbs(dx);
		long dy = -(y - trace[last].y);
		if (HWRAbs(dy) + adx <= 10)
			continue;
		last = i;
		if (dy == 0)
			continue;
		if ((adx * 100) / HWRAbs(dy) > 200)
			continue;
		if (dy < 0)
		{
			dy = -(dy * 8);
			dx = -(dx * 8);
		}
		sumX += dx;
		sumY += dy;
	}
	return (sumX * 100) / sumY;
}


// Where FillSHR's searches look: the xrs' merits, and whether one of
// them carries a bit.
static inline long
BackTo(const UByte* m, long from, UByte bits)
{
	for (long k = from; k > 0; k--)
		if (m[k] & bits)
			return k;
	return 0;
}


// ROM 0x0027ea0c FillSHR__FiP11xrdata_typeP8low_type
// Each xr's height class (+3) and shift class (+4) against the four xrs
// that bracket it - see the file's comment.  ==> 0, 1 for fewer than three
// xrs.
long
FillSHR(long slope, xrdata_type* xr, low_type* low)
{
	// the shift classes then the merits, one after the other as the ROM
	// keeps them on its stack: a stroke end first in the list would read
	// the merits' byte before the first, which is the last shift's
	UByte frame[2 * 120 + 4];
	memset(frame, 0, sizeof(frame));
	signed char* shr = (signed char*) frame;
	UByte* m = frame + 120;
	PS_point_type* trace = low->fTrace;
	long n = xr->fLength;
	if (n < 3)
		return 1;
	for (long i = 0; i < n; i++)
		m[i] = (UByte) GetXrMetrics(Xr(xr, i));
	for (long i = 0; i < n; i++)
	{
		// the four xrs that bracket this one, the ROM's [sp+0x194],
		// [sp+0x198], [sp+0x19c] and [sp+0x1a0] in that order (the fill
		// below reads them as a word array from 0x194): the first pair
		// measures the height before, the second pair the height after,
		// and the shift is the second pair's run across less the first's
		// - an extremum is { the one before, it, it, the one after }
		long idx[4] = { 0, 0, 0, 0 };
		UByte mi = m[i];
		long h, s;
		if ((mi & 0x10) && (m[i + 1] & 0x40))
		{
			// a stroke end before a link: taken as the extremum the
			// nearest firm xr either side (not across a link) is not
			long back = 0, fwd = 0;
			for (long k = i - 1; k > 0; k--)
			{
				if (m[k] & 0x40)
					break;
				if (m[k] & 3)
				{
					back = k;
					break;
				}
			}
			if (back != 0)
				mi |= (m[back] & 2) ? 1 : 2;
			else
			{
				for (long k = i + 1; k < n; k++)
					if (m[k] & 3)
					{
						fwd = k;
						break;
					}
				if (fwd != 0)
					mi |= (m[fwd] & 2) ? 1 : 2;
			}
		}
		if (mi & 0x40)
		{
			long a = 0, b = 0, c = 0, d = 0;
			long k = i - 1;
			for ( ; k > 0; k--)
				if (m[k] & 0x11)
				{
					b = k;
					break;
				}
			for (k = k - 1; k > 0; k--)
				if (m[k] & 0x12)
				{
					a = k;
					break;
				}
			k = i + 1;
			for ( ; k < n; k++)
				if (m[k] & 0x12)
				{
					c = k;
					break;
				}
			for (k = k + 1; k < n; k++)
				if (m[k] & 0x11)
				{
					d = k;
					break;
				}
			if (a > 0 && b > 0 && c > 0 && d > 0)
			{
				idx[2] = d;
				idx[0] = a;
				idx[1] = b;
				idx[3] = c;
			}
		}
		else if (mi & 2)
		{
			long b = BackTo(m, i - 1, 0x11), f = 0;
			for (long k = i + 1; k < n; k++)
				if (m[k] & 0x11)
				{
					f = k;
					break;
				}
			if (b > 0 && f > 0)
			{
				idx[0] = b;
				idx[2] = i;
				idx[3] = f;
				idx[1] = i;
			}
		}
		else if (mi & 1)
		{
			long b = BackTo(m, i - 1, 0x12), f = 0;
			for (long k = i + 1; k < n; k++)
				if (m[k] & 0x12)
				{
					f = k;
					break;
				}
			if (b > 0 && f > 0)
			{
				idx[0] = b;
				idx[2] = i;
				idx[1] = i;
				idx[3] = f;
			}
		}
		else if (mi & 0x20)
		{
			// a crossing: the upper and lower extrema after it, or failing
			// them before it.  ROM QUIRK: the backward search starts where
			// the forward one stopped and keeps what it had found
			long up = 0, low2 = 0;
			long k = i + 1;
			for ( ; k < n; k++)
				if (m[k] & 0x12)
				{
					up = k;
					break;
				}
			for (k = k + 1; k < n; k++)
				if (m[k] & 0x11)
				{
					low2 = k;
					break;
				}
			Boolean got = (low2 > 0 && up > 0);
			if (!got)
			{
				for (k = k - 1; k > 0; k--)
					if (m[k] & 0x11)
					{
						low2 = k;
						break;
					}
				for (k = k - 1; k > 0; k--)
					if (m[k] & 0x12)
					{
						up = k;
						break;
					}
				got = (low2 > 0 && up > 0);
			}
			if (got)
			{
				idx[0] = up;
				idx[2] = low2;
				idx[3] = i;
				idx[1] = low2;
			}
			if (idx[3] > 0 && low2 > 0
			 && XrGetH(Xr(xr, idx[3])->box + kXrTop) > XrGetH(Xr(xr, low2)->box + kXrTop))
				idx[3] = idx[2];
		}
		else if (mi & 0x10)
		{
			// a stroke end: the firm xrs either side, or across a link the
			// extrema of the kind the far side starts with
			long b = 0, f = 0;
			long back = i - 1;
			for (long k = i - 1; k > 0; k--)
				if (m[k] & 0x13)
				{
					b = k;
					break;
				}
			long fwd = i + 1;
			for (long k = i + 1; k < n; k++)
				if (m[k] & 0x13)
				{
					f = k;
					break;
				}
			if (m[i - 1] & 0x40)
			{
				UByte t = m[f];
				if (t & 2)
					for (long k = back; k > 0; k--)
						if (m[k] & 2)
						{
							b = k;
							break;
						}
				if (t & 1)
					for ( ; back > 0; back--)
						if (m[back] & 1)
						{
							b = back;
							break;
						}
			}
			if (m[i + 1] & 0x40)
			{
				UByte t = m[b];
				if (t & 2)
					for (long k = fwd; k < n; k++)
						if (m[k] & 2)
						{
							f = k;
							break;
						}
				if (t & 1)
					for ( ; fwd < n; fwd++)
						if (m[fwd] & 1)
						{
							f = fwd;
							break;
						}
			}
			if (b > 0 && f > 0)
			{
				idx[0] = b;
				idx[2] = i;
				idx[1] = i;
				idx[3] = f;
			}
		}
		else
		{
			long b = BackTo(m, i - 1, 0x12), f = 0;
			for (long k = i + 1; k < n; k++)
				if (m[k] & 0x11)
				{
					f = k;
					break;
				}
			if (b > 0 && f > 0)
			{
				idx[2] = f;
				idx[0] = b;
				idx[1] = f;
				idx[3] = i;
			}
		}

		if (idx[0] != 0 && idx[1] != 0 && idx[3] != 0 && idx[2] != 0)
		{
			long ys[4], xs[4];
			for (long k = 0; k < 4; k++)
			{
				xrd_el_type* e = Xr(xr, idx[k]);
				long p = XrGetH(e->hotpoint);
				if (p != 0)
				{
					ys[k] = trace[p].y;
					xs[k] = trace[p].x;
				}
				else
				{
					ys[k] = (XrGetH(e->box + kXrTop) + XrGetH(e->box + kXrBottom)) / 2;
					xs[k] = (XrGetH(e->box + kXrLeft) + XrGetH(e->box + kXrRight)) / 2;
				}
				UByte t = m[idx[k]];
				if (t & 2)
					ys[k] = XrGetH(e->box + kXrTop);
				if (t & 1)
					ys[k] = XrGetH(e->box + kXrBottom);
				if (t & 4)
					xs[k] = XrGetH(e->box + kXrLeft);
				if (t & 8)
					xs[k] = XrGetH(e->box + kXrRight);
			}
			long h1 = HWRAbs(ys[0] - ys[1]);
			long h2 = HWRAbs(ys[2] - ys[3]);
			if (h2 <= 0)
				h2 = 1;
			if (h1 <= 0)
				h1 = 1;
			long ratio = (h2 * 100) / h1;
			long k;
			h = 0;
			for (k = 1; k < 16; k++)
				if (kSHRRatioLimits[k] > ratio)
				{
					h = k;
					break;
				}
			if (k == 16)
				h = 15;
			long a, b;
			if (mi & 0x40)
			{
				a = (xs[3] * 100 - slope * (ys[2] - ys[3])) - (xs[0] * 100 - slope * (ys[1] - ys[0]));
				b = 0;
			}
			else
			{
				a = (xs[3] - xs[2]) * 100 - slope * (ys[2] - ys[3]);
				b = (xs[0] - xs[1]) * 100 - slope * (ys[1] - ys[0]);
			}
			long shift = (a - b) / ((h2 + h1) / 2);
			s = 0;
			for (k = 1; k < 16; k++)
				if (kSHRShiftLimits[k] > shift)
				{
					s = k;
					break;
				}
			if (k == 16)
				s = 15;
			if (shift < -0x1fe)
				shift = -0x1fe;
			else if (shift > 0x1fe)
				shift = 0x1fe;
			else if (shift == 0)
				shift = 1;
			shr[i] = (signed char) (shift / 4);
		}
		else
		{
			h = 0;
			s = 0;
		}
		Xr(xr, i)->height = (UByte) h;
		Xr(xr, i)->shift = (UByte) s;
	}

	// when the shifts are on average large (but not absurdly), they are
	// rescaled so the average comes to about 5.75
	if (n > 10)
	{
		long sum = 0, count = 0;
		for (long i = 0; i < n; i++)
			if (shr[i] != 0)
			{
				sum += shr[i];
				count++;
			}
		if (count > 8)
		{
			long avg = (sum * 100) / count;
			if (avg > 500 && avg < 80000)
			{
				long scale = (230000 / avg * 3 + 100) / 4;
				for (long i = 0; scale > 0 && i < n; i++)
				{
					if (shr[i] == 0)
						continue;
					long v = (scale * shr[i]) / 100;
					long s = 0;
					long k;
					for (k = 1; k < 16; k++)
						if (v < kSHRShiftLimits[k] / 4)
						{
							s = k;
							break;
						}
					if (k == 16)
						s = 15;
					Xr(xr, i)->shift = (UByte) s;
				}
			}
		}
	}
	return 0;
}


// ROM 0x0027f58c FillOrients__FiP11xrdata_typeP8low_type
// Each xr's direction (+5): of the trace leaving it forwards (from its
// point, or from the middle of an arc), or of the trace reaching a link
// backwards, measured over a third of the mean height between the upper
// and lower extrema (the base line's height when there are too few), the
// slant taken out.  ==> 0.
long
FillOrients(long slope, xrdata_type* xr, low_type* low)
{
	PS_point_type* trace = low->fTrace;
	long nPoints = (short) RCGetH(low->rc, 0x96);
	long state = 0, last = 0, sum = 0, count = 0;
	long n = xr->fLength;
	long height;
	if (n > 1)
	{
		for (long i = 1; i < n; i++)
		{
			xrd_el_type* e = Xr(xr, i);
			long m = GetXrMetrics(e);
			if ((state == 0 || state == 1) && (m & 2))
			{
				if (state != 0)
					sum += HWRAbs(last - XrGetH(e->box + kXrTop));
				state = 2;
				last = XrGetH(e->box + kXrTop);
				count++;
			}
			else if (state != 2)
				continue;
			if (m & 1)
			{
				sum += HWRAbs(last - XrGetH(e->box + kXrBottom));
				state = 1;
				last = XrGetH(e->box + kXrBottom);
				count += 1;
			}
		}
	}
	if (n > 1 && count > 1)
		height = sum / count;
	else
		height = (short) RCGetH(low->rc, 0xde) - (short) RCGetH(low->rc, 0xda);
	long dist = height / 3;
	for (long i = 1; i < n; i++)
	{
		xrd_el_type* e = Xr(xr, i);
		long m = GetXrMetrics(e);
		vect_type v;
		if (m & 0x40)
		{
			xrd_el_type* p = e - 1;
			long a;
			if (GetXrMetrics(p) & 0x80)
				a = XrGetH(p->endpoint);
			else
			{
				a = XrGetH(p->hotpoint);
				if (a == 0)
					a = (XrGetH(p->begpoint) + XrGetH(p->endpoint)) / 2;
			}
			v.b = a;
			v.a = a;
			GetBlp(0, &v, i - 1, xr);
			GetVect(0, &v, trace, nPoints, dist);
		}
		else
		{
			long a, b;
			Boolean measured = false;
			if (m & 0x20)
			{
				a = XrGetH(e->begpoint);
				b = XrGetH(e->endpoint);
			}
			else
			{
				a = XrGetH(e->hotpoint);
				if (a == 0)
					a = (XrGetH(e->begpoint) + XrGetH(e->endpoint)) / 2;
				v.a = a;
				if (IsXrLink(e + 1))
				{
					a = XrGetH(e->endpoint);
					b = XrGetH(e[1].endpoint);
				}
				else
				{
					b = (m & 0x100) ? (a + XrGetH(e->endpoint)) / 2 : a;
					v.b = b;
					GetBlp(1, &v, i, xr);
					GetVect(1, &v, trace, nPoints, dist);
					measured = true;
				}
			}
			if (!measured)
			{
				v.a = a;
				v.b = b;
				v.x1 = trace[a].x;
				v.y1 = trace[a].y;
				v.x2 = trace[b].x;
				v.y2 = trace[b].y;
			}
		}
		long dx = v.x2 - v.x1;
		long dy = v.y1 - v.y2;
		e->orient = (UByte) GetAngle(dx - (dy * slope) / 100, dy);
	}
	return 0;
}


// ROM 0x0027e880 FillXrFeatures__FP11xrdata_typeP8low_type
// The writing's slant (held within a right angle's hundredths, halved for
// a short word), then the shift and height classes and the directions.
// ==> what the two answered, added.
long
FillXrFeatures(xrdata_type* xr, low_type* low)
{
	long slope = GetCurSlope((short) RCGetH(low->rc, 0x96), low->fTrace);
	if (slope < -100)
		slope = -100;
	else if (slope > 100)
		slope = 100;
	if (xr->fLength < 7)
		slope = slope / 2;
	long r = FillSHR(slope, xr, low);
	return FillOrients(slope, xr, low) + r;
}
