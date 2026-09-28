/*
	File:		LowDArcs.cpp

	Contains:	The cursive reader's low level: FindDArcs, the last of
				xt_st_zz's element finders - where an upper element and
				the lower one after it are the two arcs of an S or a Z (a
				new element 0x23/0x24 between them) or one side of a d's
				bowl written back on itself (0x25/0x26), and the upright
				sticks either side of them turned into the arcs they
				are.  See LowLevel.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	The pair is described in an SZD_FEATURES block, which the ROM keeps
	on FindDArcs' stack: the two elements, the arrays, the pieces of the
	initial trace each covers, the three points the arcs turn at (i1 the
	top of the first, i0 where they meet, i2 the bottom of the second),
	the point of each arc furthest from its chord, the two curvatures
	CurvNonQuadr measures (the area an arc closes with its chord against
	its length squared, in hundredths, signed by the side it bends to)
	and how far the meeting point is to the side of the line from i1 to
	i2 (dev).

	All of it was read from the disassembly: the decompiler lost most of
	CheckSZArcs' and CheckDArcs' stack, and two of CheckDArcs' box
	measurements go through the unaligned load that takes the halfword
	*before* its address (see CheckDArcs).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "LowLevel.h"
#include "ParaGraph.h"
#include "XrDomains.h"


/*------------------------------------------------------------------------------
	T h e   h e l p e r s
------------------------------------------------------------------------------*/

// ROM 0x003015b8 SkipAnglesAndHMoves__FP9SPEC_TYPE
// The element after e, past the angles (0xe, 0x11) and the horizontal
// moves (0x27..0x29).  ==> nil for none.
SPEC_TYPE*
SkipAnglesAndHMoves(SPEC_TYPE* e)
{
	if (e == nil)
		return nil;
	for (;;)
	{
		e = e->next;
		if (e == nil)
			return nil;
		UByte c = e->code;
		if (c != 0xe && c != 0x11 && c != 0x28 && c != 0x29 && c != 0x27)
			return e;
	}
}


// ROM 0x003015fc CurvNonQuadr__FPsT1iT3
// How much the trace from i to j bends: the area it closes with its chord
// in hundredths of the chord's length squared, at most 1000, signed by the
// side; 0 for a single point or a trace that crosses its chord.
long
CurvNonQuadr(short* x, short* y, long i, long j)
{
	short crossed;
	if (i == j)
		return 0;
	long area = ClosedSquare(x, y, i, j, &crossed);
	if (crossed != 0)
		return 0;
	long sign;
	if (area < 0)
	{
		sign = -1;
		area = -area;
	}
	else
		sign = 1;
	long d2 = DistanceSquare(i, j, x, y);
	long curv;
	if (d2 == 0)
		curv = 1000;
	else
	{
		curv = LAdd(LMul(area, 100), d2 >> 1) / d2;
		if (curv > 1000)
			curv = 1000;
	}
	return (short) LMul(sign, curv);
}


// ROM 0x003071c8 iXmax_right__FPsT1iT3
// Rightwards from i while the trace keeps within dx of the furthest right
// it has been: the middle of the plateau at that furthest point.
long
iXmax_right(short* x, short* y, long i, long dx)
{
	long best = i;
	for (i++; y[i] != -1; i++)
	{
		if (x[i] < x[best] - dx)
			break;
		if (x[i] > x[best])
			best = i;
	}
	return iMidPointPlato(best, 0x7fff, x, y);
}


// ROM 0x0030722c iXmin_right__FPsT1iT3
// The same towards the left.
long
iXmin_right(short* x, short* y, long i, long dx)
{
	long best = i;
	for (i++; y[i] != -1; i++)
	{
		if (x[i] - dx > x[best])
			break;
		if (x[i] < x[best])
			best = i;
	}
	return iMidPointPlato(best, 0x7fff, x, y);
}


// ROM 0x00305930 CurvLikeSZ__FsN21
// Two bends at least t each, the opposite ways round.
Boolean
CurvLikeSZ(short a, short b, short t)
{
	if (a >= t && b <= -t)
		return true;
	if (a <= -t && b >= t)
		return true;
	return false;
}


// ROM 0x003053f8 LooksLikeSZ__FPsT1iT3
// The trace from i to j, cut at its middle, bends one way and then the
// other (by 5 or more each).
Boolean
LooksLikeSZ(short* x, short* y, long i, long j)
{
	long m = (i + j) >> 1;
	if (j - i < 4)
		return false;
	short a = CurvNonQuadr(x, y, i, m);
	short b = CurvNonQuadr(x, y, m, j);
	return CurvLikeSZ(a, b, 5);
}


// ROM 0x00305984 CurvConsistent__FPsT1iT3T1
// The initial trace from map[i] to map[j] does not turn into an S at its
// middle (bends of 8 or more the opposite ways round).
Boolean
CurvConsistent(short* x, short* y, long i, long j, short* map)
{
	long m = map[(i + j) >> 1];
	long a = map[i];
	long b = map[j];
	if (m == a || m == b)
		return false;
	short c1 = CurvNonQuadr(x, y, a, m);
	short c2 = CurvNonQuadr(x, y, m, b);
	return !CurvLikeSZ(c1, c2, 8);
}


/*------------------------------------------------------------------------------
	T h e   f e a t u r e s
------------------------------------------------------------------------------*/

// ROM 0x0030431c FillBasicFeatures__FP12SZD_FEATURESP8low_type
// The two elements' height bands and the pieces of the initial trace
// they cover (a single point widened by one where the trace goes on).
// ==> 1 when the first ends before the second starts, 0 otherwise.
long
FillBasicFeatures(SZD_FEATURES* f, low_type* low)
{
	short* map = f->map;
	short* yInit = f->yInit;
	f->low = low;
	f->band1 = f->e1->attr & 0xf;
	f->band2 = f->e2->attr & 0xf;
	f->iBeg1 = map[f->e1->iBeg];
	f->iEnd1 = map[f->e1->iEnd];
	if (f->iEnd1 <= f->iBeg1)
	{
		f->iEnd1 = f->iEnd1 + 1;
		if (yInit[f->iEnd1] == -1)
			return 0;
	}
	f->iBeg2 = map[f->e2->iBeg];
	f->iEnd2 = map[f->e2->iEnd];
	if (f->iBeg2 >= f->iEnd2)
	{
		f->iBeg2 = f->iBeg2 - 1;
		if (yInit[f->iBeg2] == -1)
			return 0;
	}
	if (f->iEnd1 < f->iBeg2)
	{
		f->back = 0;
		return 1;
	}
	return 0;
}


// ROM 0x003050dc PairWorthLookingAt__FP12SZD_FEATURES
// An upper element no lower than band 7 followed by a lower one (at least
// four bands below it when both are in the middle of the line), the second
// starting lower than the first ends, with no pen-up between them.
long
PairWorthLookingAt(SZD_FEATURES* f)
{
	SPEC_TYPE* e2 = f->e2;
	if (!IsUpperElem(f->e1) || f->band1 > 7 || !IsLowerElem(e2))
		return 0;
	if (f->band2 > 10 && f->band1 > 4 && f->band2 - f->band1 <= 3)
		return 0;
	short* yInit = f->yInit;
	if (yInit[f->iEnd1] >= yInit[f->iBeg2])
		return 0;
	if (brk_right(yInit, f->iBeg1, f->iEnd2) > f->iEnd2)
		return 1;
	return 0;
}


// ROM 0x003051a4 FillCurvFeatures__FP12SZD_FEATURES
// The point of each arc furthest from its chord, where the arcs meet (the
// middle between those two, or the bottom of a gulf to the right), and
// the two arcs' curvatures on the initial trace.  ==> 0 when the meeting
// point is not between the ends or either arc is an S of its own, 1
// otherwise.
long
FillCurvFeatures(SZD_FEATURES* f)
{
	short* map = f->map;
	short* y = f->y;
	short* x = f->x;
	short* yInit = f->yInit;
	f->far1 = iMostFarFromChord(x, y, f->i1, f->i0);
	f->far2 = iMostFarFromChord(x, y, f->i0, f->i2);
	f->i0 = (f->far1 + f->far2) >> 1;
	long b = f->e2->ipoint0;
	b = (b == -2) ? f->i2 : (b + f->i2) >> 1;
	long a = f->e1->ipoint0;
	a = (a == -2) ? f->i1 : (a + f->i1) >> 1;
	long at;
	if (IsRightGulfLikeIn3(x, y, a, b, &at))
		f->i0 = at;
	long m = map[f->i0];
	long p = map[f->i1];
	long q = map[f->i2];
	if (!(p < m && q > m))
		return 0;
	if (m - p <= 1 && p > 0 && yInit[p - 1] != -1)
		p--;
	if (q - m <= 1 && yInit[q + 1] != -1)
		q++;
	f->curv1 = CurvNonQuadr(f->xInit, yInit, p, m);
	f->curv2 = CurvNonQuadr(f->xInit, yInit, m, q);
	if ((f->curv1 >= 0) == (f->curv2 >= 0))
		return 1;
	if (LooksLikeSZ(x, y, f->i1, f->i0))
		return 0;
	if (LooksLikeSZ(x, y, f->i0, f->i2))
		return 0;
	return 1;
}


// ROM 0x00305474 FillComplexFeatures__FP12SZD_FEATURES
// Where the arcs turn: the meeting point half way along, how far it is to
// the side of the line between the ends (allowing for the slant), the top
// of the first arc a quarter of the way from its start (the middle for a
// short element), the bottom of the second likewise, moved to where the
// second turns left or to the elements' own points; then the curvatures.
// ==> 1 when the arcs are there.
long
FillComplexFeatures(SZD_FEATURES* f)
{
	short* x = f->x;
	short* y = f->y;
	long b1 = f->e1->iBeg;
	long b2 = f->e2->iBeg;
	long en2 = f->e2->iEnd;
	f->i0 = (b1 + en2) >> 1;
	long dx = SlopeShiftDx((short) (y[f->i0] - ((y[b1] + y[en2]) >> 1)), f->low->fSlope);
	long i0 = f->i0;
	f->dev = (short) (dx + ((UShort) x[i0] - ((x[b1] + x[en2]) >> 1)));
	SPEC_TYPE* e1 = f->e1;
	if (e1->code != 4 && e1->mark == 0x10)
		f->i1 = b1 + ((i0 - b1 + 2) >> 2);
	else
		f->i1 = (e1->iBeg + e1->iEnd) >> 1;
	SPEC_TYPE* e2 = f->e2;
	if (e2->code != 6 && e2->mark == 0x20)
		f->i2 = en2 - ((en2 - i0 + 2) >> 2);
	else
		f->i2 = (e2->iBeg + e2->iEnd) >> 1;
	if (e2->code == 0x1c || (e2->code != 6 && x[en2] > x[b2]))
	{
		long k = ixMin(b2, en2, x, y);
		if (f->i2 > k && k > b2)
			f->i2 = k;
	}
	long p = f->e1->ipoint0;
	if (p != -2 && p > f->i1)
		f->i1 = p;
	p = f->e2->ipoint0;
	if (p != -2 && p < f->i2)
		f->i2 = p;
	if (f->i2 < f->i1)
		return 0;
	return FillCurvFeatures(f) != 0;
}


/*------------------------------------------------------------------------------
	T h e   c h e c k s
------------------------------------------------------------------------------*/

// ROM 0x003056a0 CheckBackDArcs__FP12SZD_FEATURES
// The first element's stroke goes right, back left and right again (by 5
// or more) before it comes down: a d's bowl written as a stroke that goes
// back on itself.  The three turns become i1, i0 and i2 and the arcs are
// measured again.  ==> 1 (and back set) when it is one.
long
CheckBackDArcs(SZD_FEATURES* f)
{
	long found = 0;
	short* x = f->x;
	short* y = f->y;
	SPEC_TYPE* e1 = f->e1;
	long i1 = f->i1;
	long i2 = f->i2;
	long r = iXmax_right(x, y, i1, 5);
	if (HWRAbs(f->band1 - f->band2) <= 2)
		return 0;
	if (!(x[r] > x[i1] + 5))
	{
		if (!(e1->code == 2 || e1->code == 9) || (e1->attr & 0x30) != 0x10)
			return found;
	}
	long l = iXmin_right(x, y, r, 5);
	long r2 = iXmax_right(x, y, l, 5);
	long d1 = y[l] - y[r];
	long d2 = y[r2] - y[l];
	if (d1 <= 0 || d2 <= 0)
		return 0;
	if ((d2 + 1) / 3 > d1)
		return 0;
	if ((d1 + 1) / 3 > d2)
		return 0;
	if (iYdown_range(y, f->e2->iBeg, f->e2->iEnd) < r2)
		return 0;
	if (x[r] - x[l] < 5 && x[r2] - x[l] < 5)
		return 0;
	if (f->e2->iEnd - ((i2 - i1 + 4) >> 3) <= r2)
		return found;
	if (iXmin_right(x, y, r2, 5) < i2)
		return found;
	if (!((l - r) > ((r2 - l) >> 1) && (r2 - l) > ((l - r) >> 1)))
		return found;
	f->i1 = r;
	f->i2 = r2;
	f->i0 = l;
	f->dev = (short) ((UShort) x[f->i0] - ((x[f->i1] + x[f->i2]) >> 1));
	FillCurvFeatures(f);
	f->back = 1;
	found = 1;
	return found;
}


// ROM 0x003016b8 CheckSZArcs__FP12SZD_FEATURES
// The pair bent the opposite ways round, well below one another: an S or
// a Z.  When the bends are real ones (consistent, strong enough, with the
// far points out to the side) a new element (0x24 an S's top arc to the
// right, 0x23 to the left) is put between them, band 7; and an upper stick
// or a lower one that is really wide is turned into the arc it is (9/0xa,
// 0xc/0xb).  ==> 0 when it is not an S or a Z, 1 otherwise.
long
CheckSZArcs(SZD_FEATURES* f)
{
	short* x = f->x;
	short* y = f->y;
	short* xInit = f->xInit;
	short* yInit = f->yInit;
	short* map = f->map;
	SPEC_TYPE* e1 = f->e1;
	SPEC_TYPE* e2 = f->e2;
	long i0 = f->i0;
	long i1 = f->i1;
	long i2 = f->i2;
	Boolean ok = true;

	if (!(HWRAbs(f->band1 - f->band2) > 2 && y[i2] > y[i1]))
		return 0;
	if (f->curv1 < 0)
	{
		long xi1 = x[i1];
		if (x[i2] > xi1)
		{
			long dx = (short) (x[i2] - xi1);
			long dy = (short) (y[i2] - y[i1]);
			if (dx > (dy >> 1))
				return 0;
			if ((dy + 1) / 3 < dx)
				ok = false;
			else if (dx > ((dy + 2) >> 2) && x[i0] < xi1)
				ok = false;
		}
	}
	long cos = cos_vect(i0, i1, i0, i2, x, y);
	if (cos >= -0x3c)
	{
		long a = HWRAbs(x[i0] - x[i1]);
		long b = HWRAbs(y[i0] - y[i1]);
		long c = HWRAbs(x[i0] - x[i2]);
		long d = HWRAbs(y[i0] - y[i2]);
		if ((a * 2 + 1) / 3 > b || (c * 2 + 1) / 3 > d)
		{
			if (cos > -0x1e)
				return 0;
			ok = false;
		}
	}
	if (!(f->curv1 > 0 && f->curv2 < 0))
	{
		if (f->curv1 >= 0)
			return 0;
		if (f->curv2 <= 0)
			return 0;
	}
	if (ok)
		ok = CurvLikeSZ(f->curv1, f->curv2, 8)
			&& CurvConsistent(xInit, yInit, i1, i0, map)
			&& CurvConsistent(xInit, yInit, i0, i2, map);
	long lo = (short) HWRAbs(f->curv1);
	long hi = (short) HWRAbs(f->curv2);
	if (hi < lo)
	{
		long t = lo;
		lo = hi;
		hi = t;
	}
	if (hi >= lo * 8)
		return 0;
	if (ok)
		ok = hi * 15 > 100;

	long mx1 = (short) ((x[i1] + x[i0]) >> 1);
	long my1 = (short) ((y[i1] + y[i0]) >> 1);
	long mx2 = (short) ((x[i2] + x[i0]) >> 1);
	long my2 = (short) ((y[i2] + y[i0]) >> 1);
	Boolean make = false;
	// (the ROM measures |dx| twice where it may have meant to use |dy|
	// once: a far point is out to the side when one and a half times its
	// dx from the chord's middle reaches its dy)
	if (ok
	 && HWRAbs(x[f->far1] - mx1) + (HWRAbs(x[f->far1] - mx1) >> 1) >= HWRAbs(y[f->far1] - my1)
	 && HWRAbs(x[f->far2] - mx2) + (HWRAbs(x[f->far2] - mx2) >> 1) >= HWRAbs(y[f->far2] - my2)
	 && HWRAbs(x[f->far1] - x[f->far2]) >= (HWRAbs(f->dev) + 1) / 3)
		make = true;
	if (make)
	{
		if (f->curv1 > 0 && e1->mark != 0x10)
		{
			SPEC_TYPE* p = SkipAnglesBefore(e1);
			if (p != nil && IsLowerElem(p) && p->iBeg < i1)
			{
				long mid = (p->iBeg + p->iEnd) >> 1;
				long c1 = iClosestToY(y, mid, i1, y[f->far1]);
				long c2 = iClosestToY(y, mid, i1, y[f->far2]);
				long c0 = iClosestToY(y, mid, i1, y[i0]);
				SPEC_TYPE* q = SkipAnglesBefore(p);
				if (q != nil && IsUpperElem(q) && q->iBeg < mid)
				{
					long t = iClosestToY(y, (q->iBeg + q->iEnd) >> 1, mid, y[f->far2]);
					if (x[t] > x[c2])
						c2 = t;
				}
				long a = HWRAbs(x[f->far1] - x[c1]);
				long b = HWRAbs(x[f->far2] - x[c2]);
				b = b + (HWRAbs(x[f->far2] - x[c2]) + 1) / 3;
				// (not the absolute value of the second: a far point to
				// the left of where the writing came from compares
				// negative)
				if (a < b && HWRAbs(x[i0] - x[c0]) > x[f->far1] - x[c1])
					make = false;
			}
		}
		if (make)
		{
			SPEC_TYPE* n = NewSPECLElem(f->low);
			f->fNew = n;
			if (n == nil)
				return 0;
			n->attr = (n->attr & 0xf0) | 7;
			n->iBeg = i1;
			n->iEnd = i2;
			n->code = (f->curv1 <= 0) ? 0x23 : 0x24;
			Insert2ndAfter1st(e1, f->fNew);
			f->fNew->other = 0;
			if (lo > 0xc)
				f->fNew->other = (hi / lo <= 3) ? 4 : 2;
		}
	}

	Boolean right1 = x[e1->iBeg] < x[e1->iEnd];
	Boolean right2 = x[e2->iBeg] < x[e2->iEnd];
	_RECT box;
	long j = map[f->far1];
	if (f->iEnd1 > j)
		j = f->iEnd1;
	GetTraceBox(xInit, yInit, f->iBeg1, j, &box);
	long w = box.right - box.left;
	long h = box.bottom - box.top;
	Boolean wide1 = ((ok || HWRAbs(f->curv1) > 8) && w > (h >> 1))
				 || (h + (h >> 1) < w && HWRAbs(f->curv1) > 3);
	j = f->iBeg2;
	if (j >= map[f->far2])
		j = map[f->far2];
	GetTraceBox(xInit, yInit, j, f->iEnd2, &box);
	w = box.right - box.left;
	h = box.bottom - box.top;
	Boolean wide2 = ((ok || HWRAbs(f->curv2) > 8) && w > (h >> 1))
				 || (h + (h >> 1) < w && HWRAbs(f->curv2) > 3);

	if (f->curv1 > 0)
	{
		if (e1->code == 3 && e1->mark == 0x10 && wide1 && right1)
		{
			e1->code = 9;
			e1->attr = (e1->attr & ~0x30) | 0x10;
		}
		if (e2->code == 7 && e2->mark == 0x20 && wide2 && right2)
		{
			e2->code = 0xc;
			e2->attr = (e2->attr & ~0x30) | 0x20;
		}
	}
	else
	{
		if (e1->code == 3 && e1->mark == 0x10 && wide1 && !right1)
		{
			e1->code = 0xa;
			e1->attr = (e1->attr & ~0x30) | 0x20;
		}
		if (e2->code == 7 && e2->mark == 0x20 && wide2 && !right2)
		{
			e2->code = 0xb;
			e2->attr = (e2->attr & ~0x30) | 0x10;
		}
	}
	return 1;
}


// ROM 0x0030222c CheckDArcs__FP12SZD_FEATURES
// The pair bent the same way round, or found going back on itself: the
// sides of a d's (or an a's, a g's) bowl.  A new element (0x25 bowed to
// the right, 0x26 to the left, band 7) goes between them when the whole
// bends consistently with them, and an upper or lower stick whose piece of
// the trace is really an arc - wider than it is tall, or near a late stroke
// - is turned into that arc (9/0xa, 0xb/0xc).  ==> 0 for no room or no
// such thing, 1 otherwise.
long
CheckDArcs(SZD_FEATURES* f)
{
	if (f->back == 0)
	{
		// the arcs bent the opposite ways round are an S's, not a d's
		if (!(f->curv1 >= 0 && f->curv2 >= 0))
		{
			if (f->curv1 > 0 || (f->curv1 <= 0 && f->curv2 > 0))
				return 1;
		}
	}
	short* map = f->map;
	short* y = f->y;
	short* x = f->x;
	SPEC_TYPE* e2 = f->e2;
	SPEC_TYPE* e1 = f->e1;
	long b1 = e1->iBeg;
	long en1 = e1->iEnd;
	long b2 = e2->iBeg;
	long en2 = e2->iEnd;
	long dev = (short) HWRAbs(f->dev);
	long dband = f->band2 - f->band1;
	long i1 = f->i1;
	long i2 = f->i2;
	Boolean isNum = RCGetH(f->low->rc, 0x92) == 2;
	long lo = (short) HWRAbs(f->curv1);
	long hi = (short) HWRAbs(f->curv2);
	if (hi < lo)
	{
		long t = lo;
		lo = hi;
		hi = t;
	}
	long a = (e1->code == 3 || e1->code == 2) ? b1 : i1;
	long b = (e2->code == 7 || e2->code == 8) ? en2 : i2;
	long adx = (short) HWRAbs(x[a] - x[b]);
	long ady = (short) HWRAbs(y[a] - y[b]);
	long q = (short) ((ady + 2) >> 2);

	// whether the pair is tall and narrow enough to be a bowl's sides,
	// or wide and deep enough to be one written back
	Boolean look = false;
	if (dev > q)
	{
		if ((ady + 1) / 3 > adx)
			look = true;
		else if (adx < ady && (x[b1] > x[en2] || lo > 3))
			look = true;
	}
	if (!look)
	{
		Boolean check = dband >= 6
			|| (f->curv1 > 0 && f->curv2 > 0)
			|| RCGetH(f->low->rc, 0x94) == 0x20;
		if (check)
		{
			if (q >= dev * 2)
				;
			else if (dev > q)
				look = true;
			else if (dev > (q >> 1) && y[en2] > 0x27e6)
				look = true;
			else if ((q + 1) / 3 >= dev)
				;
			else if (f->dev > 0)
				look = true;
		}
		if (!look && f->back == 0)
			return 1;
	}

	long back = f->back;
	if (back == 0 && !(e1->mark == 0x10 && e2->mark == 0x20) && lo < 3)
		return 0;
	Boolean bowl = (dband >= 6 && (hi + 1) / 3 < lo)
		|| (lo > 0 && ((e1->code != 3 && e1->code != 7) || (e2->code != 3 && e2->code != 7)));
	if (bowl || back != 0)
	{
		short cv = CurvNonQuadr(f->xInit, f->yInit, map[i1], map[i2]);
		short dyA = (short) (y[f->i0] - (UShort) y[i1]);
		short dyB = (short) ((UShort) y[i2] - y[f->i0]);
		if (cv != 0 && HWRAbs(cv) >= (lo >> 1) && dyA > 0 && dyB > 0)
		{
			Boolean put;
			if (f->back != 0)
				put = true;
			else
				put = (f->curv1 >= 0) != (cv >= 0)
				   && (f->curv2 >= 0) != (cv >= 0)
				   && dyA > (dyB >> 1) && dyB > (dyA >> 1);
			if (put)
			{
				SPEC_TYPE* n = NewSPECLElem(f->low);
				f->fNew = n;
				if (n == nil)
					return 0;
				n->iBeg = i1;
				n->iEnd = i2;
				n->attr = (n->attr & 0xf0) | 7;
				n->code = (f->dev <= 0) ? 0x26 : 0x25;
				Insert2ndAfter1st(e1, f->fNew);
			}
		}
		if (f->back != 0)
			return 1;
	}

	// the sticks either side: arcs when their pieces are wide
	if (e1->mark != 0x10 && e2->mark != 0x20)
		return 1;
	Boolean lenient = !(!isNum && dband >= 6 && f->band1 < 5 && f->band2 > 9
						&& e1->mark == 0x10 && e2->mark == 0x20 && dev >= (q >> 1));
	Boolean nearLate = false;
	Boolean ok1 = true;
	Boolean ok2 = true;
	Boolean right1 = x[b1] < x[en1];
	Boolean right2 = x[b2] < x[en2];
	SPEC_TYPE* p = e1->prev;
	while (p != nil && (p->code == 0x12 || p->code == 1 || p->code == 0x13 || p->code == 0x14))
		p = p->prev;
	// (the list's head, which is never a break, stops the walk before p
	// can be nil)
	UByte pc = (p != nil) ? p->code : 0;
	if (pc == 0xd || pc == 0x10)
		nearLate = true;
	if (nearLate || lenient)
	{
		long far = iMostFarFromChord(x, y, i1, i2);
		long h1 = (short) HWRAbs(y[far] - y[i1]);
		long h2 = (short) HWRAbs(y[i2] - y[far]);
		long t1, t2;
		if (isNum || dband >= 6)
		{
			t1 = (short) (h1 >> 1);
			t2 = (short) (h2 >> 1);
		}
		else
		{
			t1 = (short) ((h1 + 1) / 3);
			t2 = (short) ((h2 + 1) / 3);
		}
		_RECT box1, box2;
		GetTraceBox(x, y, b1, (en1 > i1) ? en1 : i1, &box1);
		// ROM: these two go through word loads two bytes past the word
		// boundary, which take the halfword before the one named - the
		// right edge less the left, where the offsets name the bottom and
		// the top (what the reads mean is the widths)
		long w1 = (short) (box1.right - box1.left);
		GetTraceBox(x, y, (b2 >= i2) ? i2 : b2, en2, &box2);
		long w2 = (short) (box2.right - box2.left);

		if (e2->mark != 0x20 && w1 < (w2 >> 1))
			ok1 = false;
		else if (t1 > h2)
			ok1 = false;
		else
		{
			long ht1 = (short) (box1.bottom - box1.top);
			if (nearLate)
				ok1 = ht1 < w1;
			else
				ok1 = w1 * 3 >= ht1 * 2 || (dband > 3 && ht1 <= w1 * 2);
		}
		if ((e1->mark != 0x10 && (w1 + 1) / 3 > w2) || t2 > h1)
			ok2 = false;
		else
		{
			long ht2 = (short) (box2.bottom - box2.top);
			if (nearLate)
				ok2 = ht2 < w2;
			else
				ok2 = w2 + (w2 >> 1) >= ht2 || (ht2 <= w2 * 2 && f->band2 > 9);
		}
	}

	if (!(f->curv1 > 0 || f->curv2 > 0) && f->curv1 >= -3)
	{
		SPEC_TYPE* s;
		if (e2->mark == 0x20)
			s = e2;
		else
		{
			s = SkipRealAnglesAndPointsAfter(e2);
			if (s == nil || s->code == 0x12 || s->code == 1 || s->code == 0x13 || s->code == 0x14)
				s = e2;
		}
		long k = iyMax(s->iBeg, s->iEnd, y);
		if (k < 0)
			return 0;
		long at;
		long side = SideExtr(x, y, i1, (k + s->iEnd) >> 1, 0, f->xInit, f->yInit, map, &at, 0);
		if (side == 2 || side == 4)
			ok1 = false;
	}

	ULong flags = RCGetH(f->low->rc, 0x90);
	if (f->dev > 0)
	{
		if (e1->code == 3 && ok1 && right1 && e1->mark == 0x10 && f->curv1 > 0 && (flags & 0x800) == 0)
		{
			e1->code = 9;
			e1->attr = (e1->attr & ~0x30) | 0x10;
		}
		if (e2->code == 7 && ok2 && !right2 && e2->mark == 0x20 && f->curv2 > 0)
		{
			e2->code = 0xb;
			e2->attr = (e2->attr & ~0x30) | 0x10;
		}
	}
	else
	{
		if (e1->code == 3 && ok1 && !right1 && e1->mark == 0x10 && f->curv1 < 0 && (flags & 0x800) == 0)
		{
			e1->code = 0xa;
			e1->attr = (e1->attr & ~0x30) | 0x20;
		}
		if (e2->code == 7 && ok2 && right2 && e2->mark == 0x20 && f->curv2 < 0)
		{
			e2->code = 0xc;
			e2->attr = (e2->attr & ~0x30) | 0x20;
		}
	}
	return 1;
}


// ROM 0x00302d74 ArrangeAnglesNearNew__FP12SZD_FEATURES
// The angles and horizontal moves just after a new element, in its middle
// half: one in its first quarter (or, for an S's element, at or before
// where the arcs meet, or not a stroke's end) is moved before it; an end
// of a stroke (mark 0xb) at a d's element, or at an S's whose turn is not
// on the way to it, is taken out.
void
ArrangeAnglesNearNew(SZD_FEATURES* f)
{
	SPEC_TYPE* n = f->fNew;
	if (n == nil)
		return;
	long lo = (n->iBeg * 3 + n->iEnd + 2) >> 2;
	long hi = (n->iEnd * 3 + n->iBeg + 2) >> 2;
	for (SPEC_TYPE* p = n->next; p != nil; p = p->next)
	{
		UByte c = p->code;
		if (c != 0xe && c != 0x11 && c != 0x28 && c != 0x29)
			return;
		if (hi < p->iEnd)
			continue;
		SPEC_TYPE* resume = p;
		Boolean move = false;
		if (p->iBeg <= lo)
			move = true;
		else
		{
			UByte nc = n->code;
			Boolean take;
			if (nc == 0x24 || nc == 0x23)
			{
				if (p->iBeg <= f->i0 || p->mark != 0xb)
					move = true;
				take = !move;
			}
			else
				take = p->mark == 0xb;
			if (take)
			{
				Boolean del;
				if (nc == 0x25 || nc == 0x26)
					del = true;
				else
				{
					long ip = p->ipoint0;
					long to = (ip <= f->i0) ? n->iBeg : n->iEnd;
					long cos = cos_vect(ip, f->i0, ip, to, f->x, f->y);
					if (cos >= 0x3c)
					{
						del = false;
						if (!(p->iBeg > f->i0))
							move = true;
					}
					else
						del = true;
				}
				if (del)
				{
					resume = p->prev;
					DelFromSPECLList(p);
				}
			}
		}
		if (move)
		{
			resume = p->prev;
			Move2ndAfter1st(n->prev, p);
		}
		p = resume;
	}
}


// ROM 0x00302ed0 KillHAtNewElem__FP12SZD_FEATURES
// A horizontal move (0x27) right after the new element taken out.
void
KillHAtNewElem(SZD_FEATURES* f)
{
	SPEC_TYPE* p = SkipAnglesAfter(f->fNew);
	if (p == nil)
		return;
	if (p->code == 0x27)
		DelFromSPECLList(p);
}


/*------------------------------------------------------------------------------
	F i n d D A r c s
------------------------------------------------------------------------------*/

// ROM 0x00302f00 FindDArcs__FP8low_type
// Every element and the next one past the angles and horizontal moves,
// where the two do not overlap, looked at as a pair of arcs.  ==> 0.
long
FindDArcs(low_type* low)
{
	SZD_FEATURES f;
	f.x = low->fX;
	f.y = low->fY;
	f.xInit = low->fXInitial;
	f.yInit = low->fYInitial;
	f.map = low->fBuffers[2].ptr;
	for (f.e1 = low->fSpecl->next; f.e1 != nil; f.e1 = f.e1->next)
	{
		f.fNew = nil;
		f.e2 = SkipAnglesAndHMoves(f.e1);
		if (f.e2 == nil)
			continue;
		if (!(f.e1->iEnd < f.e2->iBeg || f.e2->iEnd < f.e1->iBeg))
			continue;
		if (!FillBasicFeatures(&f, low))
			continue;
		if (PairWorthLookingAt(&f) && FillComplexFeatures(&f))
		{
			if (CheckBackDArcs(&f) || !CheckSZArcs(&f))
				CheckDArcs(&f);
		}
		if (f.fNew != nil)
		{
			KillHAtNewElem(&f);
			ArrangeAnglesNearNew(&f);
		}
	}
	return 0;
}
