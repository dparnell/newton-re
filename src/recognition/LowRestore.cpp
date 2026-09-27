/*
	File:		LowRestore.cpp

	Contains:	The cursive reader's low level: the last two of
				AnalyzeLowData's passes - RestoreColons, which finds a colon
				written as two dots and puts it where it belongs, and
				PostFindSideExtr, which adds the side bends FindSideExtr could
				not see before the codes were given.  See LowLevel.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	RestoreColons: of the dots and dashes (0x10, and 0xd not marked 4),
	up to eight kept that stand clear of their neighbours; any two next to
	each other in the trace (one ends two points before the other starts),
	no more than 80 apart across and 20 to 160 apart down, their heights
	not overlapping, and not an i's dot (LooksLikeIAndPoint) near the
	writing's right edge, are a colon: PutColonAtItsPlace moves the pair to
	the break (between strokes) nearest their middle across, making one
	when the nearest element is not a break, and joins the breaks either
	side of it.

	PostFindSideExtr: between an upper and a lower extremum (or a lower
	and an upper, or a lower and a crossing's upper part) whose heights
	are ordinary, the trace from the first's point to the second's is
	asked for a side bend (SideExtr, strictly this time); one that bends
	the right way, is closed off (ClosedSquare) and has nothing in between
	becomes an arc element (0x28 going down, 0x29 going up), or an angle
	(0xe) where the pen doubles back sharply in a letter going down.

	All of it was read from the disassembly.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "LowLevel.h"
#include "ParaGraph.h"
#include "XrDomains.h"


static inline Boolean
IsBreakCode(UByte c)
{
	return c == 0x12 || c == 1 || c == 0x13 || c == 0x14;
}

static inline Boolean
IsLoneDot(SPEC_TYPE* e)
{
	return e->code == 0x10 || (e->code == 0xd && (e->other & 4) == 0);
}


// ROM 0x00305044 AdjustBegEndWithoutPoint__FP9SPEC_TYPE
// An element about to be taken out from between two breaks: the breaks'
// ends that met it moved to meet each other.
void
AdjustBegEndWithoutPoint(SPEC_TYPE* e)
{
	SPEC_TYPE* prev = e->prev;
	SPEC_TYPE* next;
	if (prev == nil || (next = e->next) == nil)
		return;
	if (IsBreakCode(prev->code) && prev->iEnd == e->iBeg)
		prev->iEnd = next->iBeg;
	if (!IsBreakCode(next->code))
		return;
	if (next->iBeg != e->iEnd)
		return;
	next->iBeg = prev->iEnd;
}


// ROM 0x00304964 LooksLikeIAndPoint__FP9SPEC_TYPEisPsT4
// Whether a dot (at point p) belongs to the upper extremum after it, as an
// i's dot: left of its top, or above it and less than dx to its right.
Boolean
LooksLikeIAndPoint(SPEC_TYPE* dot, long p, short dx, short* x, short* y)
{
	SPEC_TYPE* e = dot->next;
	if (e == nil || !IsUpperElem(e))
		return false;
	long iBeg = e->iBeg;
	long iEnd = e->iEnd;
	long top = (iBeg + iEnd) >> 1;
	if (y[iBeg] < y[top])
		top = iBeg;
	if (y[iEnd] < y[top])
		top = iEnd;
	if (x[p] < x[top])
		return true;
	if (y[p] < y[top] && x[p] - x[top] < dx)
		return true;
	return false;
}


// ROM 0x00304a34 PutColonAtItsPlace__FP8low_typeP9SPEC_TYPET2
// The two dots of a colon moved to the break nearest their middle across
// (the first or last firm element when every break lies on one side of
// them), one made when the nearest is not a break, and neighbouring breaks
// joined.  ==> 1, 0 when a stroke that should be there is not.
long
PutColonAtItsPlace(low_type* low, SPEC_TYPE* a, SPEC_TYPE* b)
{
	short* x = low->fX;
	short* y = low->fY;
	short mx = (short) ((x[(a->iBeg + a->iEnd) >> 1] + x[(b->iBeg + b->iEnd) >> 1]) >> 1);
	SPEC_TYPE* first = nil;
	SPEC_TYPE* last = nil;
	for (SPEC_TYPE* e = low->fSpecl->next; e != nil; e = e->next)
	{
		if (e->code == 0x10 || e->code == 0xd)
			continue;
		if (first != nil)
			last = e;
		else
			first = e;
	}
	if (last == nil)
		return 1;
	long best = 0x7fff;
	SPEC_TYPE* at = nil;
	Boolean right = false, left = false;
	if (first == nil)
		return 0;
	for (SPEC_TYPE* e = first; e != nil; e = e->next)
	{
		UByte c = e->code;
		if (!(IsBreakCode(c) || e == first || e == last))
			continue;
		SPEC_TYPE* prev = e->prev;
		if (prev == a && e->next == b)
			continue;
		long ex;
		if (prev == nil)
		{
			if (e->next == nil || y[e->iEnd] == -1)
				return 0;
			ex = low->fBox.left;
		}
		else if (e->next == nil)
		{
			if (y[e->iBeg] == -1)
				return 0;
			ex = low->fBox.right;
		}
		else
		{
			long ib = e->iBeg;
			long ie = e->iEnd;
			if (y[ib] == -1 || y[ie] == -1)
				return 0;
			if (!IsBreakCode(c))
				ex = (short) ((x[ib] + x[ie]) >> 1);
			else
			{
				SPEC_TYPE* an = a->next;
				SPEC_TYPE* bn;
				if (e->next == a && an != nil && IsBreakCode(an->code) && an->next == b
				 && (bn = b->next) != nil)
				{
					// a break with the colon's two dots and a break after it:
					// the middle of the strokes either side
					long far;
					if (IsBreakCode(bn->code) && bn->next != nil)
						far = bn->iEnd;
					else
						far = bn->iBeg;
					ex = (short) ((x[ib] + x[far]) >> 1);
				}
				else
				{
					ex = x[ib];
					if (ex < x[ie])
						ex = (short) ((ex + x[ie]) >> 1);
				}
			}
		}
		short d = (short) (ex - mx);
		if (d < 0)
		{
			left = true;
			d = (short) -d;
		}
		else
			right = true;
		if (d < best)
		{
			at = e;
			best = d;
		}
	}
	if (at == nil)
		return 0;
	if (right && !left)
		at = first;
	else if (left && !right)
		at = last;

	if (!IsBreakCode(at->code))
	{
		SPEC_TYPE* br = NewSPECLElem(low);
		if (br == nil)
			return 0;
		br->code = 0x12;
		br->attr = (br->attr & ~0xf) | 7;
		if (at == last)
		{
			br->iEnd = at->iEnd;
			br->iBeg = at->iEnd;
			Insert2ndAfter1st(at, br);
		}
		else
		{
			br->iEnd = at->iBeg;
			br->iBeg = at->iBeg;
			Insert2ndAfter1st(at->prev, br);
		}
		at = br;
	}
	else if (a->next == b)
	{
		SPEC_TYPE* p = a->prev;
		SPEC_TYPE* n = b->next;
		if ((p == nil || IsBreakCode(p->code)) && (n == nil || IsBreakCode(n->code))
		 && (p == at || n == at))
			return 1;
	}
	AdjustBegEndWithoutPoint(a);
	AdjustBegEndWithoutPoint(b);
	Move2ndAfter1st(at, a);
	Move2ndAfter1st(a, b);
	if ((a->attr & 0xf) > (b->attr & 0xf))
	{
		SwapThisAndNext(a);
		SPEC_TYPE* t = a;
		a = b;
		b = t;
	}
	if (a->iBeg > at->iBeg)
		at->iEnd = a->iBeg;
	SPEC_TYPE* n = b->next;
	if (n != nil)
	{
		if (IsBreakCode(n->code))
		{
			if (b->iEnd < n->iEnd)
				n->iBeg = b->iEnd;
		}
		else
		{
			SPEC_TYPE* br = NewSPECLElem(low);
			if (br != nil)
			{
				Insert2ndAfter1st(b, br);
				br->code = 0x12;
				br->attr = (br->attr & ~0xf) | 7;
				SPEC_TYPE* after = br->next;
				if (b->iEnd < after->iBeg)
				{
					br->iBeg = b->iEnd;
					br->iEnd = after->iBeg;
				}
				else
				{
					br->iBeg = after->iEnd;
					br->iEnd = b->iBeg;
				}
			}
		}
	}
	// breaks that now meet are made one (0x14 when either was)
	for (SPEC_TYPE* e = low->fSpecl->next; e != nil; e = e->next)
	{
		SPEC_TYPE* nx;
		if (!IsBreakCode(e->code) || (nx = e->next) == nil || !IsBreakCode(nx->code))
			continue;
		e->code = (e->code == 0x14 || nx->code == 0x14) ? 0x14 : 0x12;
		if (nx->iBeg < e->iBeg)
			e->iBeg = nx->iBeg;
		if (e->iEnd <= nx->iEnd)
			e->iEnd = nx->iEnd;
		DelFromSPECLList(nx);
		e = e->prev;
	}
	return 1;
}


// ROM 0x003044d8 RestoreColons__FP8low_type
// See the file's comment.  ==> 0.
long
RestoreColons(low_type* low)
{
	struct Dot
	{
		SPEC_TYPE*	elem;
		long		gap;			// across to the next firm element (0x7fff at a stroke's end)
	};
	Dot dots[8];
	short* x = low->fX;
	short* y = low->fY;
	long n = 0;
	for (SPEC_TYPE* e = low->fSpecl->next; e != nil; e = e->next)
	{
		if (!IsLoneDot(e))
			continue;
		dots[n].elem = e;
		SPEC_TYPE* nx = e->next;
		while (nx != nil && IsLoneDot(nx))
			nx = nx->next;
		if (nx == nil || IsBreakCode(nx->code))
			dots[n].gap = 0x7fff;
		else
			dots[n].gap = HWRAbs(x[(e->iBeg + e->iEnd) >> 1] - x[(nx->iBeg + nx->iEnd) >> 1]);
		if (dots[n].elem->code == 0xd)
		{
			// a dash is kept only when it is further from its neighbour
			// than it is long
			short xMin, xMax;
			if (dots[n].gap < 0x28)
				continue;
			if (xMinMax(e->iBeg, e->iEnd, x, y, &xMin, &xMax) == 0)
				continue;
			if (dots[n].gap < xMax - xMin)
				continue;
		}
		if (++n >= 8)
			break;
	}
	if (n < 2)
		return 0;
	for (long i = 0; i < n - 1; i++)
	{
		SPEC_TYPE* a = dots[i].elem;
		if (a == nil)
			continue;
		long ma = (a->iBeg + a->iEnd) >> 1;
		for (long j = i + 1; j < n; j++)
		{
			SPEC_TYPE* b = dots[j].elem;
			if (b == nil)
				continue;
			if ((a->attr & 0xf) <= 7 && (b->attr & 0xf) <= 7)
				continue;
			if (b->iEnd != a->iBeg - 2 && b->iBeg - 2 != a->iEnd)
				continue;
			long mb = (b->iEnd + b->iBeg) >> 1;
			long xa = x[ma];
			long xb = x[mb];
			short dx = (short) (xa - xb);
			if (dx < 0)
				dx = (short) -dx;
			if (dx > 0x50)
				continue;
			short dy = (short) ((UShort) y[ma] - (UShort) y[mb]);
			if (dy < 0)
				dy = (short) -dy;
			if (dy < 0x14 || dy > 0xa0)
				continue;
			if (dx + (dx >> 1) > dy
			 && !(dy > dx && dots[i].gap > dx && dots[j].gap > dx))
				continue;
			long edge = low->fBox.right - 0x28;
			if (!(xa <= edge && xb <= edge))
			{
				if (LooksLikeIAndPoint(a, ma, dx, x, y))
					continue;
				if (LooksLikeIAndPoint(b, mb, dx, x, y))
					continue;
			}
			short aMin, aMax, bMin, bMax;
			yMinMax(a->iBeg, a->iEnd, y, &aMin, &aMax);
			yMinMax(b->iBeg, b->iEnd, y, &bMin, &bMax);
			if (aMin < bMax && bMin < aMax)
				continue;
			PutColonAtItsPlace(low, a, b);
			dots[j].elem = nil;
			dots[i].elem = nil;
			break;
		}
	}
	return 0;
}


// ROM 0x003042a4 SkipRealAnglesAndPointsAfter__FP9SPEC_TYPE
// The next element that is not an arc (0xe, 0x11) or a dot or dash.
SPEC_TYPE*
SkipRealAnglesAndPointsAfter(SPEC_TYPE* e)
{
	if (e == nil)
		return nil;
	for (;;)
	{
		e = e->next;
		if (e == nil)
			return nil;
		UByte c = e->code;
		if (!(c == 0xe || c == 0x11 || c == 0xd || c == 0x10))
			return e;
	}
}


// ROM 0x003042e0 SkipRealAnglesAndPointsBefore__FP9SPEC_TYPE
SPEC_TYPE*
SkipRealAnglesAndPointsBefore(SPEC_TYPE* e)
{
	if (e == nil)
		return nil;
	for (;;)
	{
		e = e->prev;
		if (e == nil)
			return nil;
		UByte c = e->code;
		if (!(c == 0xe || c == 0x11 || c == 0xd || c == 0x10))
			return e;
	}
}


// ROM 0x0030446c IsSmthRelevant_InBetween__FP9SPEC_TYPET1iT3
// Whether anything firm lies between a and b other than dots, dashes,
// movements and arcs overlapping the points lo..hi.
Boolean
IsSmthRelevant_InBetween(SPEC_TYPE* a, SPEC_TYPE* b, long lo, long hi)
{
	if (a == nil)
		return false;
	for (SPEC_TYPE* e = a->next; e != nil && e != b; e = e->next)
	{
		UByte c = e->code;
		if (c == 0xd || c == 0x10 || c == 0x27)
			continue;
		if (c != 0xe && c != 0x11)
			return true;
		if (hi < e->iBeg)
			return true;
		if (lo > e->iEnd)
			return true;
	}
	return false;
}


static inline Boolean
IsEndAtCode(SPEC_TYPE* e, UByte c1, UByte c2)
{
	UByte c = e->code;
	return c == 4 || c == 6 || c == 0x1d || c == c1 || c == c2
		|| (c == 3 && (e->mark == 9 || e->mark == 6));
}


// Where the side bend is looked for from an extremum: its point when it
// stands still, otherwise its highest (or lowest) point unless that is
// the middle of a flat cap wider than it is tall either side.
static long
BendEnd(SPEC_TYPE* e, short* x, short* y, long found, Boolean before)
{
	long iBeg = e->iBeg;
	long iEnd = e->iEnd;
	long mid = (iBeg + iEnd) >> 1;
	if (before ? found <= mid : found >= mid)
		return mid;
	UByte c = e->code;
	if (c == 2 || c == 9 || c == 0xa)
	{
		long w = x[iEnd] - x[iBeg];
		if (w < 0)
			w = -w;
		long h1 = y[found] - y[iBeg];
		if (h1 < 0)
			h1 = -h1;
		long h2 = y[found] - y[iEnd];
		if (h2 < 0)
			h2 = -h2;
		if (w >= (h1 >> 1) && w >= (h2 >> 1))
			return mid;
	}
	return found;
}


// ROM 0x00303718 PostFindSideExtr__FP8low_type
// See the file's comment.  ==> 1.
long
PostFindSideExtr(low_type* low)
{
	SPEC_TYPE* head = low->fSpecl;
	short* x = low->fX;
	short* y = low->fY;
	short* map = low->fBuffers[2].ptr;
	SPEC_TYPE* made = nil;
	SPEC_TYPE* e = head->next;
	if (e == nil)
		return 1;
	for (SPEC_TYPE* next; e != nil; e = next)
	{
		next = SkipRealAnglesAndPointsAfter(e);
		if (next == nil)
			return 1;
		long down;
		if (IsUpperElem(e) && IsLowerElem(next))
		{
			if (e->code == 0x15 || e->code == 0x16)
				continue;
			if ((e->attr & 0xf) > 9 || (next->attr & 0xf) < 7)
				continue;
			down = 1;
		}
		else
		{
			if (!IsLowerElem(e))
				continue;
			if (IsUpperElem(next))
			{
				if (next->code == 0x18 || next->code == 0x17)
					continue;
			}
			else if (next->code != 0x20)
				continue;
			if ((next->attr & 0xf) > 9 || (e->attr & 0xf) < 7)
				continue;
			down = 0;
		}
		long i, j;
		if (IsEndAtCode(e, 0x18, 0x1c))
			i = e->iEnd;
		else
			i = BendEnd(e, x, y, down ? iyMin(e->iBeg, e->iEnd, y) : iyMax(e->iBeg, e->iEnd, y), true);
		if (IsEndAtCode(next, 0x15, 0x19))
			j = next->iBeg;
		else
			j = BendEnd(next, x, y, down ? iyMax(next->iBeg, next->iEnd, y) : iyMin(next->iBeg, next->iEnd, y), false);
		if (j <= i)
			continue;
		long k = 0;
		long side = SideExtr(x, y, i, j, low->fSlope, low->fXInitial, low->fYInitial, map, &k, 0);
		if (!down)
		{
			if (side == 0
			 && (e->code == 7 || e->code == 8 || e->code == 0xb || e->code == 0x22)
			 && (e->attr & 0x30) == 0x20)
			{
				if (next->code == 0x21)
					continue;
				if (next->next == nil || next->next->code != 0x1b)
				{
					// the pen doubling back sharply: a bend after all
					long d1 = Distance8(x[i], y[i], x[k], y[k]);
					long d2 = Distance8(x[j], y[j], x[k], y[k]);
					if ((d2 + 1) / 3 <= d1 && (d1 + 1) / 3 <= d2
					 && CurvMeasure(x, y, i, j, k) > 0)
					{
						long m = (i + k) >> 1;
						long d3 = Distance8(x[m], y[m], x[k], y[k]);
						long from = (d3 <= ((d2 + 2) >> 2)) ? i : m;
						if (cos_vect(k, from, k, j, x, y) >= -0x3c)
							side = 4;
					}
				}
			}
			if (next->code == 0x16 && (side == 2 || side == 4))
			{
				SPEC_TYPE* after = SkipRealAnglesAndPointsAfter(next);
				if (after != nil)
				{
					long p = ixMin(next->iEnd, after->iEnd, x, y);
					if (p > 0 && x[k] > x[p])
						side = 0;
					// the element after it in the array: a crossing's partner
					if ((next->mark == 6 || next->mark == 9) && k > (next + 1)->iBeg)
						side = 0;
				}
			}
		}
		if (side == 0)
			continue;
		long band = HeightInLine(y[(side == 1 || side == 3) ? i : j], low);
		SPEC_TYPE* elem;
		if (down)
		{
			if (side == 1 || side == 3)
			{
				SPEC_TYPE* before = SkipRealAnglesAndPointsBefore(e);
				if (made != nil && made == before)
					continue;
				long beg = (i + 2 * k + 1) / 3;
				long end = k;
				if (IsSmthRelevant_InBetween(e, next, beg, end))
					continue;
				if (IsLowerElem(before))
				{
					long v = y[iYdown_range(y, before->iBeg, before->iEnd)];
					short d = (short) (v - (UShort) y[iYup_range(y, e->iBeg, e->iEnd)]);
					if (HWRAbs(d) < 0x1a)
						continue;
				}
				short flag;
				if (ClosedSquare(x, y, i, j, &flag) <= 0 || flag != 0)
					continue;
				if ((elem = made = NewSPECLElem(low)) == nil)
					continue;
				elem->code = 0;
				elem->iBeg = (short) beg;
				elem->iEnd = (short) end;
				Insert2ndAfter1st(e, elem);
				if (elem->next->code == 0x27)
					SwapThisAndNext(elem);
				SPEC_TYPE* p = e->prev;
				if (p->code == 0xd || p->code == 0x10)
					SwapThisAndNext(p);
			}
			else
			{
				if (!(side == 2 || side == 4))
					continue;
				if (x[j] <= x[k])
					continue;
				if (e->mark != 0x10)
				{
					SPEC_TYPE* t = SkipRealAnglesAndPointsBefore(e);
					if (t != nil && t != head && t->mark != 0x10)
						continue;
				}
				if (next->mark != 0x20)
				{
					SPEC_TYPE* t = SkipRealAnglesAndPointsAfter(next);
					if (t != nil && t->mark != 0x10)
						continue;
				}
				if ((elem = made = NewSPECLElem(low)) == nil)
					continue;
				elem->code = 0xe;
				elem->iBeg = (short) ((k - 1 < i) ? i : k - 1);
				elem->iEnd = (short) ((k + 1 > j) ? j : k + 1);
				Insert2ndAfter1st(e, elem);
				SPEC_TYPE* n;
				while ((n = elem->next) != nil
					&& (n->code == 0xe || n->code == 0x11 || n->code == 0x28 || n->code == 0x29)
					&& n->iBeg < elem->iBeg)
					SwapThisAndNext(elem);
			}
		}
		else
		{
			if (!(side == 2 || side == 4))
				continue;
			if (y[i] <= y[k])
				continue;
			SPEC_TYPE* after = SkipRealAnglesAndPointsAfter(next);
			long beg = k;
			long end = (j + 2 * k + 1) / 3;
			if (IsSmthRelevant_InBetween(e, next, beg, end))
				continue;
			if (after != nil && IsLowerElem(after))
			{
				long v = y[iYdown_range(y, after->iBeg, after->iEnd)];
				short d = (short) (v - (UShort) y[iYup_range(y, next->iBeg, next->iEnd)]);
				if (HWRAbs(d) < 0x1a)
					continue;
			}
			short flag;
			if (ClosedSquare(x, y, i, j, &flag) <= 0 || flag != 0)
				continue;
			if ((elem = made = NewSPECLElem(low)) == nil)
				continue;
			elem->code = 0;
			elem->iBeg = (short) beg;
			elem->iEnd = (short) end;
			Insert2ndAfter1st(next->prev, elem);
			SPEC_TYPE* p = elem->prev;
			if (p->code == 0xd || p->code == 0x10)
				SwapThisAndNext(p);
		}

		elem->attr = (elem->attr & ~0xf) | (band & 0xf);
		elem->other = 0;
		if (elem->code == 0)
		{
			elem->code = down ? 0x28 : 0x29;
			elem->other = (side == 1 || side == 2) ? 6 : 2;
		}
		else if (elem->code == 0x11 || elem->code == 0xe)
			elem->ipoint1 = 0;
		else
			elem->other = (side == 1 || side == 2) ? 6 : 2;
		elem->ipoint0 = (short) k;
		// an arc of the same kind already there over the same points gives
		// way to it
		for (SPEC_TYPE* o = e->next; o != nil && o != next; o = o->next)
		{
			if (o == elem)
				continue;
			UByte oc = o->code;
			UByte ec = elem->code;
			if (!(oc == ec || (oc == 0x11 && ec == 0x28) || (oc == 0xe && ec == 0x29)))
				continue;
			if (elem->iEnd >= o->iBeg && o->iEnd >= elem->iBeg)
			{
				DelFromSPECLList(o);
				break;
			}
		}
	}
	return 1;
}
