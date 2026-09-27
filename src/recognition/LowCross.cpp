/*
	File:		LowCross.cpp

	Contains:	The cursive reader's low level: Cross, AnalyzeLowData's
				crossing finder.  See LowLevel.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	Where the pen passes over a place it has been before - a t's bar over
	its stem, an x, the loop of an l, a stroke laid over another - the
	trace comes close to itself.  Cross looks for that, one pair of strokes
	(or one stroke and itself) at a time: Grab walks the later part of the
	trace point by point and, for each point, walks back over the earlier
	part looking for a point near it - near meaning within a distance that
	grows with how far apart the two points are along the trace (the eps
	tables: two points three steps apart must be very close to count, two
	thirty apart need only be as close as a letter's stroke is thick).  A
	pair found near is grown by Clash into the two stretches that run
	together, and the crossing is recorded as a pair of elements, the
	earlier stretch and the later: mark 6 an ordinary crossing, 9 where the
	trace comes back along itself (DrawEnds: the stretches overlap end to
	end), 0xa one between a dash or dot and something else.  A new pair
	overlapping the last one found is merged with it (ChkMrgCrs) instead.

	The walk steps faster where the points are far apart, by how much
	further than the limit they are, which is what keeps it from being a
	square of the trace's length.

	Two strokes are only compared when their boxes come within nbcut0 of
	each other; a stroke that is a dot (8) is never compared, and two dashes
	(7) not with each other.  The elements are appended to the list's
	array, so the crossing found last is the array's last two elements,
	which is where ChkMrgCrs and AnyCrosCont look.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "LowLevel.h"
#include "ParaGraph.h"


extern const short	eps0[64];
extern const short	eps1[64];
extern const short	eps2[64];
extern const short	eps3[64];
extern const short	nbcut0[2];

enum
{
	kCrossFound = 1,			// Grab/Clash: a pair was found near on this row
	kCrossGrown = 2,
	kCrossNear = 4,				// two different strokes whose boxes meet
	kCrossFromDash = 0x10,		// the later stroke is a dash (7)
	kCrossToDash = 0x20			// the earlier one is
};


// ROM 0x002c9184 DrawEnds__FP8low_typePsT2
// Whether the crossing from *a back to *b is really the trace coming back
// along itself: moving a down towards b while the points get no further
// from b, and then both ends in towards each other while they stay within
// eps3 of each other, the two meet.  Then *a is where they met (halfway,
// if they passed) and *b the point before it; ==> 9.  ==> 6 an ordinary
// crossing.
static long
DrawEnds(low_type* low, short* pa, short* pb)
{
	short* x = low->fX;
	short* y = low->fY;
	long a = *pa;
	long b = *pb;
	if (a >= b)
	{
		long xb = x[b];
		long yb = y[b];
		long d = LAdd(LMul(x[a] - xb, x[a] - xb), LMul(y[a] - yb, y[a] - yb));
		long idx = a - b;
		if (idx >= 0x40)
			idx = 0x3f;
		long lim = eps3[idx];
		if (d > lim)
			return 6;
		long prev;
		for (;;)
		{
			prev = d;
			a--;
			if (a <= b)
				break;
			d = LAdd(LMul(x[a] - xb, x[a] - xb), LMul(y[a] - yb, y[a] - yb));
			if (!(d <= prev))
				break;
		}
		if (a > b)
		{
			d = prev;
			a++;
			while (d <= prev || d <= lim)
			{
				prev = d;
				a--;
				b++;
				if (a <= b)
					break;
				long dx = x[a] - x[b];
				long dy = y[a] - y[b];
				d = LAdd(LMul(dx, dx), LMul(dy, dy));
				idx = a - b;
				if (idx >= 0x40)
					idx = 0x3f;
				lim = eps3[idx];
			}
		}
	}
	if (b < a)
		return 6;
	if (b > a)
		a = (a + b) / 2;
	*pa = (short) a;
	*pb = (short) (*pa - 1);
	return 9;
}


// ROM 0x002c930c AnyCrosCont__FP8low_typeiT2Pi
// Whether points i and j (i the later) are already inside a crossing found
// before - the pairs at the end of the array, from the last back: *found
// is the start of the earlier stretch of the last such pair (-2 none).
// ==> 1 when either point is off the trace or a pen-up, 0 otherwise.
static long
AnyCrosCont(low_type* low, long i, long j, long* found)
{
	long len = low->fLenSpecl;
	SPEC_TYPE* specl = low->fSpecl;
	long result = 0;
	long at = -2;
	short* y = low->fY;
	long n = low->fII;
	if (i < 0 || n <= i || y[i] == -1 || j < 0 || n <= j || y[j] == -1)
		result = 1;
	else
	{
		for (long k = 0; k < len; k += 2)
		{
			SPEC_TYPE* e = &specl[len - 1 - k];
			UByte m = e->mark;
			if (m != 6 && m != 0xa && m != 9)
				break;
			SPEC_TYPE* later = e - 1;
			if (e->iBeg <= j && j <= e->iEnd && later->iBeg <= i && i <= later->iEnd)
				at = e->iBeg;
		}
	}
	*found = at;
	return result;
}


// ROM 0x002c93f8 ChkMrgCrs__FP8low_typePsP9SPEC_TYPET3
// The new crossing (a the later stretch, b the earlier) merged into the
// last one found when the two overlap: *result 0x80 merged (the last
// pair's stretches widened over the new ones; a 9 met by an ordinary one
// makes the pair a 9), -2 overlapping but of kinds that do not merge, 0 to
// be added as it is (and a's end moved on to the last pair's when both
// are 9s that do not coincide).  A dash's crossing (0xa) only merges with
// another.  ==> 0.
static long
ChkMrgCrs(low_type* low, short* result, SPEC_TYPE* a, SPEC_TYPE* b)
{
	long len = low->fLenSpecl;
	SPEC_TYPE* lb = &low->fSpecl[len - 1];
	SPEC_TYPE* la = &low->fSpecl[len - 2];
	UByte ma = a->mark;
	UByte ml = lb->mark;
	*result = 0;
	if (ml == 0xa)
	{
		if (ma != 0xa)
			return 0;
	}
	else if (ma == 0xa)
		return 0;
	else if (ml != 6 && ml != 9 && ml != 0xa)
		return 0;
	long ai = a->iBeg;
	long ae = a->iEnd;
	long bi = b->iBeg;
	long be = b->iEnd;
	long lai = la->iBeg;
	long lae = la->iEnd;
	long lbi = lb->iBeg;
	long lbe = lb->iEnd;
	*result = -2;
	if (!(lae >= ai && ae >= lai && lbe >= bi && be >= lbi))
	{
		*result = 0;
		return 0;
	}
	if (ml != 9 && ma != 9)
	{
		*result = 0x80;
		lb->iBeg = (short) ((bi >= lbi) ? lbi : bi);
		lb->iEnd = (short) ((be <= lbe) ? lbe : be);
		la->iBeg = (short) ((ai >= lai) ? lai : ai);
		la->iEnd = (short) ((ae > lae) ? ae : lae);
		return 0;
	}
	if (ml != 9)
	{
		// the new one is a 9, the last not: the last pair becomes a 9
		*result = 0x80;
		la->iBeg = (short) ai;
		lb->iEnd = (short) be;
		la->iEnd = (short) ((ae > lae) ? ae : lae);
		lb->iBeg = (short) ((bi >= lbi) ? lbi : bi);
		la->mark = 9;
		lb->mark = 9;
		return 0;
	}
	if (ma != 9 || (ai == lai && be == lbe))
	{
		*result = 0x80;
		la->iEnd = (short) ((ae > lae) ? ae : lae);
		lb->iBeg = (short) ((bi >= lbi) ? lbi : bi);
		return 0;
	}
	*result = 0;
	a->iEnd = (short) ((ae > lae) ? ae : lae);
	return 0;
}


// The limit two points `diff` apart along the trace must be within (as a
// square distance) for Clash to take them as running together.
static inline long
ClashLimit(ULong flags, long diff)
{
	if ((flags & kCrossToDash) != 0 || (flags & kCrossFromDash) != 0)
		return 0x90;
	long idx = (diff >= 0x40) ? 0x3f : diff;
	if ((flags & kCrossNear) != 0)
		return eps2[idx] + ((eps2[idx] + 2) >> 2);
	return eps1[idx];
}


// ROM 0x002c9bf8 Clash__FP8low_typeUsP12POINTS_GROUPP9SPEC_TYPET4
// The crossing found at a->iBeg (later) and b->iBeg (earlier) grown: for
// each point i on from a's start, the earlier trace is walked out both
// ways from the middle of b while the points stay within the limit of i
// (ClashLimit), widening b to the points found; a grows while any are.
// a->ipoint0 is the furthest a may grow (the later stroke's end),
// b->ipoint1 the furthest back b may (the earlier stroke's start); on the
// first row the nearest point found is kept, its square distance in
// a->ipoint1 and where it is in b->ipoint0.  group is the earlier stroke.
static void
Clash(low_type* low, ULong flags, POINTS_GROUP* group, SPEC_TYPE* a, SPEC_TYPE* b)
{
	flags = (UShort) flags;
	short* x = low->fX;
	short* y = low->fY;
	long aBeg = a->iBeg;
	long aEnd = a->iEnd;
	long aLimit = a->ipoint0;
	long best = a->ipoint1;
	long bBeg = b->iBeg;
	long bEnd = b->iEnd;
	long bNear = b->ipoint0;
	long bLimit = b->ipoint1;
	long gEnd = group->iEnd;
	long rowLimit = aEnd + 1;
	long mid = (bBeg + bEnd) >> 1;
	long d0 = HWRMathILSqrt(DistanceSquare(aBeg, bNear, x, y));
	long lim = ClashLimit(flags, aBeg - mid);
	long d = d0;
	if (!(aBeg > rowLimit))
	{
		long c17 = const1[17];
		long c1 = const1[1];
		for (long i = aBeg; ; )
		{
			flags = (UShort) flags & ~kCrossFound;
			long j = mid;
			while (j <= gEnd && (d <= lim || j <= c1 + bEnd))
			{
				long diff = i - j;
				if ((flags & kCrossNear) != 0 || !(c17 >= diff))
				{
					long dx = x[i] - x[j];
					long dy = y[i] - y[j];
					d = LAdd(LMul(dx, dx), LMul(dy, dy));
					lim = ClashLimit(flags, diff);
					if (d <= lim)
					{
						flags = (UShort) flags | kCrossFound;
						if (bEnd <= j)
							bEnd = j;
						if (i == aBeg && d <= best)
						{
							best = d;
							bNear = j;
						}
						j = bEnd;
					}
				}
				j++;
			}
			j = mid;
			d = d0;
			while (j >= bLimit && (d <= lim || bBeg - c1 <= j))
			{
				long diff = i - j;
				if ((flags & kCrossNear) != 0 || !(c17 >= diff))
				{
					long dx = x[i] - x[j];
					long dy = y[i] - y[j];
					d = LAdd(LMul(dx, dx), LMul(dy, dy));
					lim = ClashLimit(flags, diff);
					if (d <= lim)
					{
						flags = (UShort) flags | kCrossFound;
						if (bBeg >= j)
							bBeg = j;
						if (i == aBeg && d <= best)
						{
							best = d;
							bNear = j;
						}
						j = bBeg;
					}
				}
				j--;
			}
			long found = flags & kCrossFound;
			if (found != 0 && i >= aEnd)
			{
				aEnd = i;
				long next = i + 1;
				if (next >= aLimit)
					next = aLimit;
				rowLimit = next;
			}
			if (found == 0 || i == aLimit)
				break;
			i++;
			if (!(i <= rowLimit))
				break;
		}
	}
	a->iBeg = (short) aBeg;
	a->iEnd = (short) aEnd;
	a->ipoint1 = (short) best;
	b->iBeg = (short) bBeg;
	b->iEnd = (short) bEnd;
	b->ipoint0 = (short) bNear;
}


// ROM 0x002c96b4 Grab__FP8low_typeUsP12POINTS_GROUPT3
// The crossings between the stroke g (the later) and h (the earlier, or g
// itself) found and recorded.  For each point i of g, the points k of h
// from j back are tried: within the limit (the eps table by how far apart
// they are, eps0 for a stroke against itself, eps2 for two strokes, a
// flat 0x87 against a dash) is a crossing, unless it is in one found
// already; further off, the walk steps on by how much further (the
// square root's excess, a quarter or ten thirty-seconds of it), and the
// rows step on by the least of those.  A stroke against itself starts
// 9 points in (const1[13] + 1) and compares only points more than that
// far apart.  ==> 0, 1 for no room.
static long
Grab(low_type* low, ULong flags, POINTS_GROUP* g, POINTS_GROUP* h)
{
	flags = (UShort) flags;
	short* x = low->fX;
	short* y = low->fY;
	long i = g->iBeg;
	long gEnd = g->iEnd;
	long hBeg = h->iBeg;
	long j = h->iEnd;
	const short* table = nil;
	long base;
	long span;
	if ((flags & kCrossToDash) != 0 || (flags & kCrossFromDash) != 0)
	{
		base = 0xc;
		span = 0;
	}
	else if ((flags & kCrossNear) != 0)
	{
		table = eps2;
		base = 10;
		span = 10;
	}
	else
	{
		i = i + const1[13] + 1;
		if (i >= gEnd)
			return 0;
		table = eps0;
		base = 0xc;
		span = 0x1e;
	}
	long far;
	if ((flags & kCrossToDash) != 0 || (flags & kCrossFromDash) != 0)
		far = 0x87;
	else
		far = table[63];
	if (i > gEnd)
		return 0;
	SPEC_TYPE e1;		// the later stretch
	SPEC_TYPE e2;		// the earlier
	for (;;)
	{
		// the rows
		flags = (UShort) flags & ~(kCrossFound | kCrossGrown);
		if ((flags & kCrossNear) == 0)
			j = i - const1[13] - 1;
		long k = j;
		long step = 0x7fff;
		if (!(j < hBeg))
		{
			for (;;)
			{
				// the points of the row
				long dx = x[i] - x[k];
				long dy = y[i] - y[k];
				long d2 = LAdd(LMul(dx, dx), LMul(dy, dy));
				long dist = HWRMathILSqrt(d2);
				long t;
				Boolean check = false;
				if (i - k > span)
				{
					if (d2 <= far)
						check = true;
					else
					{
						long v = LMul(dist - base, 10);
						t = (1 <= (v >> 5)) ? (v >> 5) : 1;
					}
				}
				else
				{
					long v = table[i - k];
					if (d2 <= v)
						check = true;
					else
					{
						long r = dist - HWRMathISqrt((short) v);
						t = (1 <= (r >> 2)) ? (r >> 2) : 1;
					}
				}
				if (!check)
				{
					long t1 = t - 1;
					long m = (t1 <= step) ? t1 : step;
					if (m <= 1)
						step = 1;
					else if (t1 <= step)
						step = t1;
					if (!(k - t >= hBeg) && k != hBeg)
						t = k - hBeg;
				}
				else
				{
					long already;
					if (AnyCrosCont(low, i, k, &already) == 1)
						return 1;
					if (already != -2)
					{
						t = 1;
						step = 1;
						k = already;
					}
					else
					{
						ULong f = (UShort) flags | (kCrossFound | kCrossGrown);
						ULong near = f & kCrossNear;
						if (near == 0)
							j = i - const1[17] - 1;		// (written over at once: ROM QUIRK)
						InitSpeclElement(&e1);
						InitSpeclElement(&e2);
						if (near == 0)
							j = i - 1;
						e1.iBeg = (short) i;
						e1.iEnd = (short) i;
						e2.iBeg = (short) k;
						e2.iEnd = (short) k;
						e1.ipoint0 = (short) gEnd;
						e1.ipoint1 = 0x7fff;
						e2.ipoint0 = (short) k;
						e2.ipoint1 = (short) hBeg;
						Clash(low, f, h, &e1, &e2);
						if ((f & kCrossFromDash) != 0 || (f & kCrossToDash) != 0)
							e2.mark = 0xa;
						else if (near != 0)
							e2.mark = 6;
						else
						{
							e2.mark = (UByte) DrawEnds(low, &e1.iBeg, &e2.ipoint0);
							// ROM QUIRK: the copy is an ldr of the word at
							// ipoint1's address (two bytes past a word
							// boundary), and the halfword it leaves in the low
							// half is the one before - ipoint0, where DrawEnds
							// left the point before the meeting
							if (e2.mark == 9)
								e2.iEnd = e2.ipoint0;
						}
						e1.mark = e2.mark;
						short merged;
						ChkMrgCrs(low, &merged, &e1, &e2);
						if (merged == 0 || merged == -2)
						{
							e1.ipoint1 = -2;
							e1.ipoint0 = -2;
							if (MarkSpecl(low, &e1) == 1)
								return 1;
							e2.ipoint0 = -2;
							e2.ipoint1 = -2;
							if (MarkSpecl(low, &e2) == 1)
								return 1;
						}
						flags = (UShort) f & ~kCrossGrown;
						if (gEnd <= e1.iEnd && hBeg >= e2.iBeg)
							return 0;
						t = 1;
						step = 0x7fff;
						if (hBeg < e2.iBeg)
							k = e2.iBeg;
						else
						{
							i++;
							break;		// the next row
						}
					}
				}
				k -= t;
				if (!(k >= hBeg))
				{
					i += step;
					if (!(i <= gEnd))
						return 0;
					break;
				}
			}
			continue;
		}
		i += step;
		if (!(i <= gEnd))
			return 0;
	}
}


// ROM 0x002c8fb4 Cross__FP8low_type
// Every stroke g compared with itself and each stroke before it whose box
// comes within nbcut0 of g's (Grab); dots never, two dashes not with each
// other.  ==> 0, 1 for no room.
long
Cross(low_type* low)
{
	POINTS_GROUP* groups = low->fGroups;
	long n = low->fLenGroups;
	for (long gi = 0; gi < n; gi++)
	{
		ULong flags = 0;
		POINTS_GROUP* g = &groups[gi];
		long gb = g->iBeg;
		if (IsPointCont(low, gb, 8) != -2)
			continue;
		if (IsPointCont(low, gb, 7) != -2)
			flags = kCrossFromDash;
		for (long hi = gi; hi >= 0; hi--)
		{
			flags = (UShort) flags & ~kCrossToDash;
			POINTS_GROUP* h = &groups[hi];
			long hb = h->iBeg;
			if (IsPointCont(low, hb, 8) != -2)
				continue;
			if (IsPointCont(low, hb, 7) != -2)
				flags = (UShort) flags | kCrossToDash;
			if ((flags & kCrossFromDash) != 0 && (flags & kCrossToDash) != 0)
				continue;
			ULong f;
			if (hi == gi)
				f = flags & ~kCrossNear;
			else
			{
				long nb = nbcut0[0];
				if (!(g->box.right >= h->box.left - nb && h->box.right >= g->box.left - nb
				   && g->box.bottom >= h->box.top - nb && h->box.bottom >= g->box.top - nb))
					continue;
				f = flags | kCrossNear;
			}
			flags = (UShort) f;
			if (Grab(low, flags, g, h) == 1)
				return 1;
		}
	}
	return 0;
}
