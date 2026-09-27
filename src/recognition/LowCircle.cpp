/*
	File:		LowCircle.cpp

	Contains:	The cursive reader's low level: Circle, AnalyzeLowData's
				circle finder.  See LowLevel.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	Circle looks at each bottom of the trace (an element marked 3) that
	lies between two tops (marked 1) and looks like the foot of an o
	(look_like_circle).  For each such foot it decides whether the pen,
	going round it, came back far enough to close a loop - an o, an a's
	bowl, a d's, a g's, an e's eye - and if so marks the loop's closing
	as a pair of crossing elements (mark 6, `other` 'c' at the later
	point and 'd' at the earlier, make_circle).

	The loop is judged from three elements: the foot (a), the top before
	it (b) and the top after it (c).  Orient00 first rules out a foot too
	narrow or too flat to be round.  A foot drawn right to left is a
	*back* circle (work_with_back_circle: the o written anticlockwise the
	usual way), one drawn left to right a *forward* one, which must also
	stand alone in its stroke (is_forw_isolate_circle).  Clash_my then
	searches, over a stretch of the trace on the way down into the foot
	and a stretch on the way up out of it, for the pair of points that
	come closest - the place the loop closes - and says whether they are
	close enough.  How close is close enough comes from Ruler0 and
	circle_type, which know the shapes: an e's eye (is_e_circle) and a g's
	bowl (is_g_circle) are small, a loop that doubles back (vozvrat_move)
	is judged by how far it came back.

	The heights are in the rescaled trace's units (the small letters 80
	high between 0x2796 and 0x27e6).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "LowLevel.h"
#include "ParaGraph.h"


// Skip an angle (0x0b) the corner finder put between two extrema.
static inline SPEC_TYPE*
NextSkippingAngle(SPEC_TYPE* e)
{
	SPEC_TYPE* n = e->next;
	if (n->mark == 0x0b)
		n = n->next;
	return n;
}

static inline SPEC_TYPE*
PrevSkippingAngle(SPEC_TYPE* e)
{
	SPEC_TYPE* p = e->prev;
	if (p->mark == 0x0b)
		p = p->prev;
	return p;
}


// ROM 0x00307e58 SlopeShiftDx__Fsi
// How far the writing's slant (hundredths) moves a point dy up or down
// across, rounded to the nearest.
short
SlopeShiftDx(short dy, long slope)
{
	Boolean mismatch = ((dy < 0) ? 0 : 1) != ((slope < 0) ? 0 : 1);
	long p = LMul(slope, dy);
	return (short) ((p + (mismatch ? -0x32 : 0x32)) / 100);
}


// ROM 0x002bc734 find_right_part_beg__FPsT1P9SPEC_TYPEN23s
// Where the search for the loop's closing starts on the way up out of
// the foot a: a quarter of the way along from a's end towards c's start,
// moved on while that point is still deep in the foot.  When the loop is
// wider than k (from b's start to c's end), the way back from c is tried
// first: the first point on it more than a quarter as high above c's
// start as a's end is.
static short
find_right_part_beg(short* x, short* y, SPEC_TYPE* a, SPEC_TYPE* b, SPEC_TYPE* c, short k)
{
	long ae = a->iEnd;
	long cb = c->iBeg;
	long yc = y[cb];
	long dy = (short) ((UShort) y[ae] - yc);
	long ce = c->iEnd;
	long span = cb - a->iEnd;
	long i;
	if (x[ce] - x[b->iBeg] > k)
	{
		for (i = (short) (cb - span / 4); ae < i; i = (short) (i - 1))
			if (dy < ((short) ((UShort) y[i] - yc)) * 4)
				return (short) i;
	}
	i = (short) (ae + span / 4);
	long yce = y[ce];
	long lim = dy * 3;
	while ((short) ((UShort) y[i] - yce) * 5 > lim && cb > i)
		i = (short) (i + 1);
	return (short) i;
}


// ROM 0x002bc874 end_min_has_right_point__FPsT1P9SPEC_TYPET3T1
// Where the search on the way down into the foot a ends, when the top b
// before it runs on to the right past its own end: a sixth or a third of
// the way from b's end to a, or the last point before that still right
// of b's end.  ==> 0 when b turns back left at once (nothing set).
static long
end_min_has_right_point(short* x, short* y, SPEC_TYPE* a, SPEC_TYPE* b, short* out)
{
	long i = b->iEnd;
	if (x[i + 1] < x[i])
		return 0;
	long span = a->iBeg - i;
	short i6 = (short) (span / 6 + i);
	short i3 = (short) (span / 3 + i);
	if (x[i3] < x[b->iBeg])
	{
		if (x[i6] < x[b->iBeg])
		{
			for (;;)
			{
				i = (short) (i + 1);
				if (i6 <= i)
					return 1;
				if (x[i] < x[b->iEnd])
					break;
				*out = (short) i;
			}
			return 1;
		}
		*out = i6;
	}
	else
		*out = i3;
	return 1;
}


// ROM 0x002bc990 is_forw_isolate_circle__FP9SPEC_TYPET1Ps
// Whether the loop through b and c is all its stroke has: the stroke
// starts at b (or one element before it) and ends at c (or one after
// it), and c's end is no further right than b's start.
static long
is_forw_isolate_circle(SPEC_TYPE* b, SPEC_TYPE* c, short* x)
{
	SPEC_TYPE* q = b->prev;
	UByte m = q->mark;
	if (m == 0x10 || m == 0 || q->prev->mark == 0x10 || q->prev->mark == 0)
	{
		// (the ROM reads the mark before it tests the pointer for nil;
		// the result is the same)
		SPEC_TYPE* r = c->next;
		Boolean ok = r == nil || r->mark == 0x20;
		if (!ok)
		{
			r = r->next;
			ok = r == nil || r->mark == 0x20;
		}
		if (ok && x[c->iEnd] <= x[b->iBeg])
			return 1;
	}
	return 0;
}


// ROM 0x002bca14 control_line_high__FPsP9SPEC_TYPEN22
// Whether the foot a's middle is below both tops' middles, the second by
// more than two fifths of the first's depth.
static long
control_line_high(short* y, SPEC_TYPE* a, SPEC_TYPE* b, SPEC_TYPE* c)
{
	long ma = (short) ((y[a->iBeg] + y[a->iEnd]) / 2);
	long d1 = (short) (ma - (short) ((y[b->iBeg] + y[b->iEnd]) / 2));
	long d2 = (short) (ma - (short) ((y[c->iBeg] + y[c->iEnd]) / 2));
	return d1 > 0 && d2 > 0 && d2 * 5 > d1 * 2;
}


// ROM 0x002bdaf0 is_vert_min__FPsT1P9SPEC_TYPE
// Whether the bottom a is really an upright run rather than a rounded
// foot: a short one, or one whose middle points are all at least as low
// as its higher end and which is no wider than it is deep.
static long
is_vert_min(short* x, short* y, SPEC_TYPE* a)
{
	long e = a->iEnd;
	long b = a->iBeg;
	if (e - b < 5)
		return 1;
	long m = (short) ((b + e) / 2);
	long ref = (x[e] < x[b]) ? y[b] : y[e];
	if (y[m] >= ref && y[(short) ((e + m) / 2)] >= ref && y[(short) ((b + m) / 2)] >= ref)
	{
		short w = (short) HWRAbs(x[b] - x[e]);
		short h = (short) HWRAbs(y[a->iBeg] - y[a->iEnd]);
		if (w <= h)
			return 1;
	}
	return 0;
}


// ROM 0x002bdc04 is_narrow_prev__FPsT1P9SPEC_TYPET3
// Whether the top b before the foot a leans over narrowly: from the last
// point of b where it still moves left, compare the slope down to b's
// end with the slope on down to a's end.
static long
is_narrow_prev(short* x, short* y, SPEC_TYPE* b, SPEC_TYPE* a)
{
	long s = b->iBeg;
	long e = b->iEnd;
	long lr = s;
	for (long i = (short) (e - 1); s < i; )
	{
		if (x[i] < x[i + 1])
		{
			lr = (short) (i - 1);
			break;
		}
		i = (short) (i - 1);
	}
	long dx1 = (short) ((UShort) x[lr] - x[e]);
	long dy1 = (short) (y[e] - (UShort) y[lr]);
	long dx2 = (short) (x[e] - (UShort) x[a->iBeg]);
	long dy2 = (short) ((UShort) y[a->iEnd] - y[e]);
	if (dx1 < 1 || dx2 / 10 > dx1)
		return 1;
	if (dx2 == 0)
		return 0;
	return (dy1 * 10) / dx1 >= (dy2 * 10) / dx2;
}


// ROM 0x002bd0d8 is_isolate_circle__FP9SPEC_TYPET1
// Whether the stroke is just the loop: it starts at b (an angle apart at
// most) and ends at c (likewise).
static long
is_isolate_circle(SPEC_TYPE* b, SPEC_TYPE* c)
{
	SPEC_TYPE* q = b->prev;
	if (q->mark != 0x10 && (q->mark != 0x0b || q->prev->mark != 0x10))
		return 0;
	SPEC_TYPE* r = c->next;
	if (r->mark != 0x20 && (r->mark != 0x0b || r->next->mark != 0x20))
		return 0;
	return 1;
}


// ROM 0x002bd12c is_b_circle__FP9SPEC_TYPET1PsT3s
// A b's bowl: the stroke ends after c (at most a bottom between), c is
// drawn right to left and lies well below b.
static long
is_b_circle(SPEC_TYPE* b, SPEC_TYPE* c, short* x, short* y, short dy)
{
	SPEC_TYPE* n = NextSkippingAngle(c);
	UByte m = n->mark;
	if (m != 0x20 && (m != 3 || n->next->mark != 0x20))
		return 0;
	long x1 = x[c->iBeg];
	long x2 = x[c->iEnd];
	if (x1 > x2)
	{
		long yc = y[c->iBeg];
		long yb = y[b->iBeg];
		if (yc > yb && dy / 3 <= yc - yb)
		{
			if (m == 0x20)
				return 1;
			if (x[n->iEnd] <= x2)
				return 1;
		}
	}
	return 0;
}


// ROM 0x002bcae0 is_d_circle__FPsT1P9SPEC_TYPEN23T1sT7
// A d's bowl: c is drawn right to left and followed by a bottom (the
// stem's foot), and c stands well above b.  out is where the search on
// the way up ends (that bottom's start); ==> 1 when the trace between c's
// end and there goes back left of c's start before it has moved a
// quarter of the loop's width from the foot.
static long
is_d_circle(short* x, short* y, SPEC_TYPE* a, SPEC_TYPE* b, SPEC_TYPE* c, short* out, short dy, short dx)
{
	SPEC_TYPE* n = NextSkippingAngle(c);
	if (n->mark != 3 || x[c->iEnd] > x[c->iBeg] || is_vert_min(x, y, b) != 0)
		return 0;
	if (!(y[c->iBeg] < y[b->iBeg]))
		return 0;
	if (dy / 8 < y[b->iBeg] - y[c->iBeg] && x[b->iEnd] <= x[b->iBeg]
	 && HWRAbs(y[n->iBeg] - y[a->iBeg]) * 2 <= dy)
	{
		*out = n->iBeg;
		long i = c->iEnd;
		long r = 0;
		if (n->iBeg <= i)
			return 0;
		for (;;)
		{
			if (x[i] < x[c->iBeg])
				r = 1;
			if (dx / 4 < x[a->iBeg] - x[i])
				break;
			i = (short) (i + 1);
			if (n->iBeg <= i)
				return r;
		}
	}
	return 0;
}


// ROM 0x002bcca4 make_circle__FP8low_typeP9SPEC_TYPEsT3
// The loop's closing marked: a crossing pair (6), 'c' at the later point
// and 'd' at the earlier.  ==> 0, 1 for no room.
static long
make_circle(low_type* low, SPEC_TYPE* /*head*/, short a, short b)
{
	short hi = a;
	short lo = b;
	if (a < b)
	{
		hi = b;
		lo = a;
	}
	if (Mark(low, 6, 0, 0, 'c', hi, hi, hi, -2) != 1
	 && Mark(low, 6, 0, 0, 'd', lo, lo, lo, -2) != 1)
		return 0;
	return 1;
}


// ROM 0x002bd8a4 is_e_circle__FP9SPEC_TYPET1PsT3sN25
// An e's eye: the foot a drawn left to right, a bottom before b that the
// trace comes back past, and the narrowest gap between the way down to it
// and the way up from a no more than a quarter of the loop's width.
static long
is_e_circle(SPEC_TYPE* a, SPEC_TYPE* b, short* x, short* y, short k, short dy, short dx)
{
	long ai = a->iBeg;
	if (!(x[ai] <= x[a->iEnd]))
		return 0;
	SPEC_TYPE* p = PrevSkippingAngle(b);
	if (p->mark != 3)
		return 0;
	long pi = p->iBeg;
	long xp = x[pi];
	if (!(xp <= x[a->iEnd]))
		return 0;
	long bb = b->iBeg;
	long be = b->iEnd;
	if (!(x[be] <= x[bb] && xp <= x[bb]))
		return 0;
	long j = ai;
	long bestJ = ai;
	long bestI = pi;
	long best = dx;
	if (p->prev->mark != 0x10)
	{
		long pe = p->prev->iEnd;
		if (x[pe] < xp)
		{
			pi = (short) ((pi + pe) / 2);
			bestI = pi;
		}
	}
	for (long i = pi; be < j && i < bb; i = (short) (i + 1))
	{
		long yi = y[i];
		while (y[j] > yi && j > be)
			j = (short) (j - 1);
		long d = (short) ((UShort) x[i] - (UShort) x[j]);
		if (d >= 0 && d < best)
		{
			bestJ = j;
			bestI = i;
			best = d;
		}
	}
	if (best * 4 <= dx)
	{
		long t = (short) ((UShort) x[(bestI + bb) / 2] - (UShort) x[(bestJ + be) / 2]);
		if (t >= 0 && dx <= t * 5)
		{
			long u = y[b->ipoint0] - k;
			if ((u <= dy / 6 || dx <= t * 4)
			 && (short) ((UShort) y[ai] - (UShort) y[bestJ] + 5) >= 0)
				return 1;
		}
	}
	return 0;
}


// ROM 0x002bd204 is_g_circle__FP9SPEC_TYPEN21PsT4sT6
// A g's (or a q's) bowl: c drawn right to left and then either the stroke
// ends in a hook back left at the right height, or a bottom follows (the
// tail) not far below.
static long
is_g_circle(SPEC_TYPE* a, SPEC_TYPE* b, SPEC_TYPE* c, short* x, short* y, short dy, short dx)
{
	if (is_vert_min(x, y, b) != 0 || !(x[b->iEnd] <= x[b->iBeg]))
		return 0;
	SPEC_TYPE* n = c->next;
	long ya = y[a->iBeg];
	long lo = (short) (ya - (dy * 2) / 3);
	long hi = (short) (ya - dy / 4);
	if (n->mark == 0x20)
	{
		if (is_vert_min(x, y, c) == 0)
		{
			long ce = c->iEnd;
			long xce = x[ce];
			if (x[c->iBeg] <= xce)
			{
				long i = ce;
				while (a->iEnd < i && x[i - 1] <= x[i])
					i = (short) (i - 1);
				if (a->iEnd != i)
				{
					long xi = x[i];
					long yi;
					if (xi - x[b->iBeg] <= dx / 6 && lo <= (yi = y[i]) && yi <= hi
					 && (short) HWRAbs(y[ce] - yi) <= (short) (xce - xi))
						return 1;
				}
			}
		}
	}
	else
	{
		if (n->mark == 0x0b)
			n = n->next;
		if (n->mark == 3)
		{
			long xcb = x[c->iBeg];
			long ce = c->iEnd;
			long xce = x[ce];
			if (xce <= xcb)
			{
				long yce = y[ce];
				if (yce >= lo && yce <= hi
				 && y[n->iBeg] < hi + dy / 8
				 && dx <= (short) (xcb - xce) * 3)
					return 1;
			}
		}
	}
	return 0;
}


// ROM 0x002bd530 vozvrat_move__FPsT1P9SPEC_TYPEN23sN26
// How the loop came back ("vozvrat": return) after the top c: 4 or 0x2c
// when the stroke ends there (0x2c when b leans over narrowly), 3 when b
// is not high enough above k, 0xd a closed bowl with a hook, 2 a narrow
// return, 1 c well below b, 0 none of these.
static long
vozvrat_move(short* x, short* y, SPEC_TYPE* a, SPEC_TYPE* b, SPEC_TYPE* c, short k, short dy, short dx)
{
	SPEC_TYPE* n = NextSkippingAngle(c);
	if (n->mark == 0x20 || n->iBeg - 1 <= c->iEnd)
	{
		if (b->prev->mark != 0x10 && is_narrow_prev(x, y, b, a) != 0)
			return 0x2c;
		return 4;
	}
	SPEC_TYPE* nn = NextSkippingAngle(n);
	long yb0 = y[b->ipoint0];
	long d6 = dy / 6;
	if (yb0 <= k || yb0 - k < d6)
		return 3;
	long yn = y[n->iBeg];
	long yc = y[c->iBeg];
	if (yn < yc)
		return 0;
	if (y[a->iBeg] - yn > dy / 2)
		return 4;
	if (y[b->iBeg] - yc > d6)
		return 0;
	long d4 = dy / 4;
	if (nn->mark == 1)
	{
		long xne = x[n->iEnd];
		if (xne > x[n->iBeg])
		{
			long xnne = x[nn->iEnd];
			if (x[nn->iBeg] > xnne && xnne <= xne
			 && HWRAbs(y[n->iEnd] - y[b->iEnd]) < d4)
				return 0xd;
		}
	}
	long ai = a->iBeg;
	if (y[ai] - y[n->iBeg] > d4)
		return 0;
	long xce = x[c->iEnd];
	long xa = x[ai];
	if (xce < xa)
		return 0;
	if (xce - xa < xce - x[a->iEnd])
		return 0;
	long t = (short) HWRAbs(xce - x[c->iBeg]);
	if (t * 5 < dx * 2)
	{
		if (is_narrow_prev(x, y, b, a) != 0 && nn->mark != 0x20)
			return 2;
	}
	else
	{
		long xcb = x[c->iBeg];
		if (!(x[c->iEnd] > xcb && xcb > x[c->iBeg + 1]))
			return 2;
	}
	if (y[c->iBeg] - y[b->iBeg] > dy / 3 + 5)
		return 1;
	return 0;
}


// ROM 0x002bcd58 circle_type__FPsT1P9SPEC_TYPEN23sN26N41
// What kind of loop the foot a between b and c closes, as the limits
// Ruler0 works the closeness out with: rx and ry the widths across and
// down in percent, m and n the scale of how far a narrower loop may reach.
// ==> nought when it is not a loop at all, otherwise a code (1, or what
// vozvrat_move answered, or control_line_high's answer).
static long
circle_type(short* x, short* y, SPEC_TYPE* a, SPEC_TYPE* b, SPEC_TYPE* c, short k, short dy, short dx,
			short* rx, short* ry, short* m, short* n)
{
	*rx = 0x28;
	*ry = 0x28;
	if (dx * 5 < dy)
	{
		if (control_line_high(y, a, b, c) == 0)
			return 0;
		if (is_isolate_circle(b, c) == 0 || x[b->iBeg] <= x[b->iEnd] || x[c->iBeg] <= x[c->iEnd])
			*rx = 0x14;
		else
			*rx = 0x1e;
		*ry = 0x14;
		*m = 1;
		*n = 2;
		return 1;
	}
	if (is_e_circle(a, b, x, y, k, dy, dx) != 0)
	{
		if (control_line_high(y, a, b, c) == 0)
			return 0;
	}
	else if (is_g_circle(a, b, c, x, y, dy, dx) == 0)
	{
		if (is_isolate_circle(b, c) != 0 && x[b->iEnd] < x[b->iBeg] && x[c->iEnd] < x[c->iBeg])
			*rx = 0x3c;
		long r = control_line_high(y, a, b, c);
		long v = vozvrat_move(x, y, a, b, c, k, dy, dx);
		// ROM QUIRK: the 0x3c an isolated loop was just given is written
		// over at once.
		*rx = 0x23;
		switch (v)
		{
		case 1:
			*ry = 0x46;
			*rx = 0x14;
			return v;
		case 2:
		case 3:
			*ry = 0x28;
			*rx = 0x14;
			return v;
		case 4:
			*rx = 0x28;
			return r;
		case 0x2c:
			*rx = 0x1e;
			return r;
		case 0xd:
			*rx = 10;
			*ry = 0x14;
			return 0xd;
		default:
			return r;
		}
	}
	*rx = 0x14;
	*ry = 0x14;
	*m = 1;
	*n = 2;
	return 1;
}


// ROM 0x002be55c Ruler0__FPsT1sP9SPEC_TYPEN24T3N61T3
// The yardsticks a loop's closeness is measured with.  dx is the foot's
// width (from a's start, or from a quarter of the way back to b when the
// trace doubles back there), dy the depth from b's start to a's end.
// limX is how far across two points may be (rx percent of dx, plus a
// tenth or twelfth of dy for a wide limit), limY how far apart down (ry
// percent of dy); m and n scale how far a closer pair may be apart down.
// A forward loop (forward != 0) is always measured as an ordinary one.
// ==> 0 when circle_type says it is no loop.
static long
Ruler0(short* x, short* y, short /*slope*/, SPEC_TYPE* a, SPEC_TYPE* b, SPEC_TYPE* c, short k,
	   short* limX, short* limY, short* outDy, short* outDx, short* m, short* n, short forward)
{
	long dx = (short) HWRAbs(x[a->iEnd] - x[a->iBeg]);
	long ai = a->iBeg;
	long span = ai - b->iEnd;
	if (x[(short) (ai - span / 2)] < x[b->iBeg])
	{
		long xq = x[(short) (ai - span / 4)];
		long xa = x[ai];
		long xe = x[a->iEnd];
		if ((xq < xa && xa < xe) || (xa < xq && xe < xa))
			dx = (short) HWRAbs(xe - xq);
	}
	short dy = (short) HWRAbs(y[a->iEnd] - y[b->iBeg]);
	short rx;
	short ry;
	*m = 7;
	*n = 4;
	if (forward == 0)
	{
		if (circle_type(x, y, a, b, c, k, dy, (short) dx, &rx, &ry, m, n) == 0)
			return 0;
	}
	else
	{
		rx = 0x28;
		ry = 0x14;
		*n = 7;
	}
	short lx;
	if (rx < 0x24)
	{
		if (rx < 0x1f)
			lx = (short) ((rx * dx) / 100);
		else
			lx = (short) ((rx * dx) / 100 + dy / 12);
	}
	else
		lx = (short) ((rx * dx) / 100 + dy / 10);
	*limX = lx;
	*limY = (short) ((ry * dy) / 100);
	*outDy = dy;
	*outDx = (short) dx;
	return 1;
}


// ROM 0x002be83c is_min_right_side__FPsP9SPEC_TYPET2sT1
// Where the search on the way up ends when a bottom follows c well above
// the foot a: that bottom's end, or halfway between it and c's end when
// it is nearly as low as a.  ==> 0 when there is no such bottom.
static long
is_min_right_side(short* y, SPEC_TYPE* a, SPEC_TYPE* c, short dy, short* out)
{
	SPEC_TYPE* n = NextSkippingAngle(c);
	if (n->mark == 3)
	{
		long ya = y[a->iBeg];
		long yn = y[n->iBeg];
		if (yn <= ya && dy / 3 <= ya - yn)
		{
			long ne = n->iEnd;
			if (y[a->iEnd] - y[ne] < dy / 2)
				*out = (short) ((ne + c->iEnd) / 2);
			else
				*out = (short) ne;
			return 1;
		}
	}
	return 0;
}


// ROM 0x002be920 is_min_in_left_side__FPsT1P9SPEC_TYPET3T1sT6
// Where the search on the way down into the foot a starts.  out is first
// b's start; a bottom q before b may move it to q or halfway to it when q
// is to the left of b and not far above it, and otherwise, when b still
// moves right at its start, to a sixth or a third of the way back to q,
// or the earliest point before b's start still right of it.  ==> 0 when
// there is no bottom before b, or b's start already turns left.
static long
is_min_in_left_side(short* x, short* y, SPEC_TYPE* a, SPEC_TYPE* b, short* out, short dy, short dx)
{
	*out = b->iBeg;
	SPEC_TYPE* q = PrevSkippingAngle(b);
	if (q->mark != 3)
		return 0;
	long qi = q->iBeg;
	long xq = x[qi];
	long ai = a->iBeg;
	long xa = x[ai];
	Boolean nearQ = false;
	if (xq >= xa && dx * 3 > (xq - x[b->iBeg]) * 2)
		nearQ = true;
	else if (xq < xa && xq > x[b->iBeg])
		nearQ = true;
	else
	{
		long bi = b->iBeg;
		long xb = x[bi];
		if (xq < xb)
		{
			if (xb - xq < dx / 3)
				nearQ = true;
			else if (y[qi] - y[bi] < dy / 3)
				nearQ = true;
		}
	}
	if (nearQ)
	{
		long yq = y[qi];
		if (yq < y[ai])
		{
			long bb = b->iBeg;
			if (yq - y[bb] > (dy * 2) / 5 || !(xq >= x[bb]))
				*out = (short) ((qi + bb) / 2);
			else
				*out = (short) qi;
			return 1;
		}
	}
	long bi = b->iBeg;
	long xbi = x[bi];
	if (xbi > x[bi - 1])
		return 0;
	long span = bi - q->iEnd;
	short i6 = (short) (bi - span / 6);
	short i3 = (short) (bi - span / 3);
	if (x[i3] >= xbi)
		*out = i3;
	else if (x[i6] >= xbi)
		*out = i6;
	else
	{
		for (long i = (short) (bi - 1); i > i6; i = (short) (i - 1))
		{
			if (x[i] < x[b->iBeg])
				break;
			*out = (short) i;
		}
	}
	return 1;
}


// ROM 0x002bde8c Orient00__FP9SPEC_TYPET1PsT3
// Whether the foot a is too narrow (under 7), or too tall or too flat
// for its width, to be the bottom of a loop.
static long
Orient00(SPEC_TYPE* a, SPEC_TYPE* b, short* x, short* y)
{
	long w = (short) HWRAbs(x[a->iEnd] - x[a->iBeg]);
	long h = (short) HWRAbs(y[a->iEnd] - y[b->iBeg]);
	if (w * 13 < h)
		return 1;
	if (w < 7)
		return 1;
	return h * 2 < w;
}


// ROM 0x002bdf2c Clash_my__FPsT1sT3P9SPEC_TYPEN25N21T3
// The place the loop round the foot a closes: every point i from a
// stretch on the way down (from `from` to `to`) is paired with every
// point j of a stretch on the way up (from `start` to `end`), and the pair
// nearest together (the slant allowed for across) is where it closes;
// out1 and out2 are i and j.  For a d's bowl the nearest pair before the
// d's stem is taken instead.  ==> 1 when the pair that is best by the
// yardsticks (close enough across, and closer down than the scale allows)
// is close enough, 0 when not (or no loop).
static long
Clash_my(short* x, short* y, short slope, short k, SPEC_TYPE* a, SPEC_TYPE* b, SPEC_TYPE* c,
		 short* out1, short* out2, short forward)
{
	long useD = 1;
	long dLimit = 0;
	long dI = 0;
	long dJ = 0;
	short limX;			// ROM sp+0x54
	short limY;			// sp+0x50
	short dy;			// sp+0x0c
	short dx;			// sp+0x08
	short m;			// sp+0x04
	short n;			// sp+0x00
	if (Ruler0(x, y, slope, a, b, c, k, &limX, &limY, &dy, &dx, &m, &n, forward) == 0)
		return 0;
	short to = 0;		// sp+0x48
	short from;			// sp+0x4c
	short end;			// sp+0x40
	if (forward != 0)
	{
		from = b->iBeg;
		end = c->iEnd;
		useD = 0;
	}
	else
	{
		is_min_in_left_side(x, y, a, b, &from, dy, dx);
		if (is_d_circle(x, y, a, b, c, &end, dy, dx) != 0)
		{
			dLimit = c->iEnd;
			limX = (short) (dx / 3);
		}
		else
		{
			useD = 0;
			if (is_min_right_side(y, a, c, dy, &end) == 0)
				end = c->iEnd;
		}
		if (is_b_circle(b, c, x, y, dy) != 0)
		{
			to = a->iBeg;
			from = (short) ((b->iEnd + a->iBeg) / 2);
			limX = (short) ((dx * 2) / 5);
		}
	}
	if (to == 0 && (forward != 0 || end_min_has_right_point(x, y, a, b, &to) == 0))
		to = b->iEnd;
	long start = find_right_part_beg(x, y, a, b, c, dx);
	// (the ROM tests whether c's start is well above b's and, whether it
	// is or not, goes on the same way - the compiler's divide-by-three
	// check left behind; nothing depends on it)
	long bestI = to;
	long bestJ = start;
	long best = 0x7fff;
	long minI = to;
	long minJ = end;
	long min2 = 0x7fff;
	long dMin = 0x7fff;
	for (long i = from; i <= to; i = (short) (i + 1))
	{
		for (long j = start; j <= end; j = (short) (j + 1))
		{
			long ddx = (short) ((UShort) x[j] - (UShort) x[i]);
			long ddy = (short) ((UShort) y[j] - (UShort) y[i]);
			long sh = (slope > 0) ? SlopeShiftDx((short) ddy, slope) : 0;
			long adx = (short) HWRAbs(ddx + sh);
			long ady = (short) HWRAbs(ddy);
			long d2 = LAdd(LMul(ddx, ddx), LMul(ady, ady));
			if (useD != 0 && j < dLimit && d2 < dMin)
			{
				dMin = d2;
				dJ = j;
				dI = i;
			}
			if (d2 < min2)
			{
				min2 = d2;
				minJ = j;
				minI = i;
			}
			if (adx < limX)
			{
				long v = (short) ((m * (limX - adx)) / n + limY);
				long cap = (dy * 9) / 10;
				if (cap <= v)
					v = (short) cap;
				if (ady < v && d2 < best)
				{
					best = d2;
					bestI = i;
					bestJ = j;
				}
			}
		}
	}
	long ddx = (short) ((UShort) x[bestI] - (UShort) x[bestJ]);
	long ddy = (short) ((UShort) y[bestI] - (UShort) y[bestJ]);
	long sh = (slope > 0) ? SlopeShiftDx((short) ddy, slope) : 0;
	long adx = (short) HWRAbs(ddx + sh);
	long ady = (short) HWRAbs(ddy);
	*out1 = (short) minI;
	*out2 = (short) minJ;
	if (useD != 0)
	{
		if (dI != 0)
			*out1 = (short) dI;
		if (dJ != 0)
			*out2 = (short) dJ;
	}
	if (adx < limX && (m * (limX - adx)) / n + limY > ady)
		return 1;
	return 0;
}


// ROM 0x002bdd40 work_with_back_circle__FP8low_typesP9SPEC_TYPEN33
static long
work_with_back_circle(low_type* low, short k, SPEC_TYPE* head, SPEC_TYPE* a, SPEC_TYPE* b, SPEC_TYPE* c)
{
	short out1;
	short out2;
	long r = Clash_my(low->fX, low->fY, low->fSlope, k, a, b, c, &out1, &out2, 0);
	if (r != 0)
		r = make_circle(low, head, out1, out2);
	return r;
}


// ROM 0x002bddd4 work_with_forw_circle__FP8low_typesP9SPEC_TYPEN33
static long
work_with_forw_circle(low_type* low, short k, SPEC_TYPE* head, SPEC_TYPE* a, SPEC_TYPE* b, SPEC_TYPE* c)
{
	short* x = low->fX;
	short* y = low->fY;
	short out1;
	short out2;
	if (is_forw_isolate_circle(b, c, x) != 0
	 && Clash_my(x, y, low->fSlope, k, a, b, c, &out1, &out2, 1) != 0)
		return make_circle(low, head, out1, out2);
	return 0;
}


// ROM 0x002bd494 work_with_circle__FP8low_typesP9SPEC_TYPEN33
// The foot a between the tops b and c tried as a loop, back or forward
// by which way it was drawn.  ==> 0, 1 for no room.
static long
work_with_circle(low_type* low, short k, SPEC_TYPE* head, SPEC_TYPE* a, SPEC_TYPE* b, SPEC_TYPE* c)
{
	short* x = low->fX;
	if (Orient00(a, b, x, low->fY) != 0)
		return 0;
	if (x[a->iBeg] < x[a->iEnd])
		return work_with_back_circle(low, k, head, a, b, c);
	return work_with_forw_circle(low, k, head, a, b, c);
}


// ROM 0x002bc5b8 Circle__FP8low_type
// Every foot that looks like the bottom of a loop (look_like_circle)
// tried (work_with_circle); k is halfway between the upper border and the
// trace's top (no higher than 0x2746).  ==> 0, 1 for no room.
long
Circle(low_type* low)
{
	SPEC_TYPE* head = low->fSpecl;
	short* y = low->fY;
	long top = low->fBox.top;
	if (top > 0x273f)
		top = 0x2746;
	short k = (short) (0x2796 - (0x2796 - top) / 2);
	for (SPEC_TYPE* e = head->next; e != nil; e = e->next)
	{
		if (e->mark != 3)
			continue;
		SPEC_TYPE* b = PrevSkippingAngle(e);
		SPEC_TYPE* c = NextSkippingAngle(e);
		if (look_like_circle(e, b, c, y) != 0)
		{
			long r = work_with_circle(low, k, head, e, b, c);
			if (r != 0)
				return r;
		}
	}
	return 0;
}
