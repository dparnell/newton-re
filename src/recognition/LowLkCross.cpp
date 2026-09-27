/*
	File:		LowLkCross.cpp

	Contains:	The cursive reader's low level: lk_cross, the pass after
				lk_begin that decides what each crossing is - a stick
				crossed (a t, an f), a loop (an o, an a's bowl, an e's eye,
				the loops of an l or a g), or nothing that matters.  See
				LowLevel.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	A crossing is a pair of elements (mark 6, code 0 until now): where the
	pen first passes the point and where it passes it again.
	analize_sticks looks at the ones Cross found where a stroke comes back
	along itself; analize_circles, for each pair still uncoded, counts the
	extrema the loop between the two passes holds and the ones outside it
	and codes the pair: 4 a loop at the top (an e, an l), 6 at the bottom
	(a g's), 5 a closed letter (an o), 0x1d/0x1e a small loop at the top or
	bottom, 0x1f/0x20 a flat one, 3 or 7 a loop thin enough to be a stick
	(Isgammathin), 0x15 an arc.  del_inside_circles then takes out the
	elements a loop swallowed.

	CrossInfoType is what analize_circles knows of the pair (ROM 0x3c
	bytes, FillCrossInfo): the box of the trace between the two passes,
	the middle of the pair, how long the loop is against its span, and
	where the middle lies in the box.

	All of it was read from the disassembly.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "LowLevel.h"
#include "ParaGraph.h"
#include "XrDomains.h"

#include <string.h>


static inline Boolean
IsBreakCode(UByte c)
{
	return c == 0x12 || c == 1 || c == 0x13 || c == 0x14;
}

static inline long
Mid(SPEC_TYPE* e)
{
	return (e->iBeg + e->iEnd) >> 1;
}


// ROM 0x002ca310 count_cross_box__FP9SPEC_TYPEPsT2P5_RECTN22
// The box of the trace from the middle of a crossing's first pass to the
// middle of its second, and its width and height.
void
count_cross_box(SPEC_TYPE* e, short* x, short* y, _RECT* box, short* dx, short* dy)
{
	SPEC_TYPE* n = e->next;
	GetTraceBox(x, y, Mid(e), Mid(n), box);
	*dx = (short) ((UShort) box->right - (UShort) box->left);
	*dy = (short) ((UShort) box->bottom - (UShort) box->top);
}


// ROM 0x002cda64 FillCrossInfo__FP8low_typeP9SPEC_TYPEP13CrossInfoType
void
FillCrossInfo(low_type* low, SPEC_TYPE* e, CrossInfoType* ci)
{
	short* y = low->fY;
	short* x = low->fX;
	SPEC_TYPE* n = e->next;
	memset(ci, 0, sizeof(CrossInfoType));
	ci->elem = e;
	ci->low = low;
	count_cross_box(e, x, y, &ci->box, &ci->dx, &ci->dy);
	long midX = (x[Mid(e)] + x[Mid(n)]) >> 1;
	ci->midX = midX;
	long midY = (y[Mid(e)] + y[Mid(n)]) >> 1;
	ci->midY = midY;
	ci->loop = (100 + 100 * ((e->iEnd - e->iBeg) + (n->iEnd - n->iBeg) + 1)) / (e->iEnd - n->iBeg + 1);
	ci->boxMidX = (ci->box.left + ci->box.right) >> 1;
	ci->boxMidY = (ci->box.top + ci->box.bottom) >> 1;
	if (ci->dx != 0)
		ci->relX = ((midX - ci->box.left) * 100) / ci->dx;
	if (ci->dy != 0)
		ci->relY = ((midY - ci->box.top) * 100) / ci->dy;
}


// ROM 0x002ca3b0 CheckSmallGamma__FP13CrossInfoType
// A small loop coded by which way it lies from its middle against the
// writing's slant: 0x1d over it or 0x1e under it when nearly upright,
// otherwise 0x20 or 0x1f by the side.  ==> 0.
long
CheckSmallGamma(CrossInfoType* ci)
{
	SPEC_TYPE* e = ci->elem;
	SPEC_TYPE* n = e->next;
	long p = (Mid(e) + Mid(n)) >> 1;
	low_type* low = ci->low;
	short* x = low->fX;
	short* y = low->fY;
	long c = cos_pointvect((short) ci->midX, (short) ci->midY, x[p], y[p],
						   (short) ci->midX, (short) ci->midY, (short) (ci->midX + 100), (short) (ci->midY + low->fSlope));
	UByte code;
	if (ci->midY > y[p])
		code = (HWRAbs(c) < 0x46) ? 0x1d : (c >= 0 ? 0x20 : 0x1f);
	else
		code = (HWRAbs(c) < 0x46) ? 0x1e : (c >= 0 ? 0x20 : 0x1f);
	e->code = code;
	n->code = e->code;
	return 0;
}


// ROM 0x002ca4d4 Isgammathin__FP13CrossInfoTypeP9SPEC_TYPE
// Whether the loop is thin enough to be a stick after all: a top loop (4)
// becomes 3, a bottom one (6) 0x15 when it hangs off to the right below,
// or 7.  ==> whether it was recoded.
long
Isgammathin(CrossInfoType* ci, SPEC_TYPE* ext)
{
	SPEC_TYPE* e = ci->elem;
	long done = 0;
	SPEC_TYPE* n = e->next;
	low_type* low = ci->low;
	short* x = low->fX;
	short* y = low->fY;
	long width = ci->maxDx;
	long dy = ci->dy;
	if (e->code == 4)
	{
		UByte attr = ext->attr;
		Boolean low3 = (attr & 0xf) <= 3;
		Boolean thin = (width <= 0xc && ext->code == 3);
		if (!thin && (attr & 0x30) == 0x10)
		{
			if ((dy < 0x3c && ext->code == 3) || (low3 && width <= 0x1e) || width < 0x15)
				thin = !is_cross(x[n->iBeg], y[n->iBeg], x[n->iEnd], y[n->iEnd],
								 x[e->iBeg], y[e->iBeg], x[e->iEnd], y[e->iEnd]);
		}
		if (thin)
		{
			UByte band = (x[n->iBeg] >= x[e->iEnd]) ? 0x20 : 0x10;
			n->code = 3;
			e->code = 3;
			e->attr = (e->attr & ~0x30) | (band & 0x30);
			n->attr = (n->attr & ~0x30) | (ext->attr & 0x30);
			done = 1;
		}
	}
	if (e->code != 6)
		return done;
	long m = Mid(ext);
	long pe = e->iEnd;
	if (y[m] < y[pe] && x[m] > x[pe])
	{
		// (the heights from the second element's start to the first's end)
		short yMin, yMax;
		yMinMax(n->iBeg, pe, y, &yMin, &yMax);
		e->attr = (HeightInLine(yMin, low) & 0xf) | 0x10;
		n->code = 0x15;
		e->code = 0x15;
		return 1;
	}
	long crossed = is_cross(x[n->iBeg], y[n->iBeg], x[n->iEnd], y[n->iEnd],
							x[e->iBeg], y[e->iBeg], x[pe], y[pe]);
	long a, b;
	if (crossed)
	{
		a = n->iBeg;
		b = pe;
	}
	else
	{
		a = Mid(n);
		b = Mid(e);
	}
	Boolean stick = (width <= 0xc)
				 || (width <= 0x1e && (RCGetH(low->rc, 0x90) & 0x800));
	if (!stick)
	{
		if (crossed || (ext->attr & 0x30) != 0x20)
			return done;
		if (!(dy <= 0x28 && width < 0x1e) && width >= 0x15)
			return done;
	}
	UByte band = (x[a] >= x[b]) ? 0x10 : 0x20;
	if (ext->code == 8 && width > 0xc && !(RCGetH(low->rc, 0x90) & 0x800))
	{
		e->other |= 2;
		return 0;
	}
	n->code = 7;
	e->code = 7;
	e->attr = (e->attr & ~0x30) | band;
	n->attr = (n->attr & ~0x30) | band;
	e->other = 1;
	return 1;
}


// ROM 0x002cd63c GetMaxDxInGamma__FiN21PsT4UcPiT7
// The widest the loop gets across: from its turning point c, level by
// level towards the lower of a's and b's heights (up for a top loop, 4,
// down otherwise), the two points where the trace passes that level on
// either side.  ==> the widest, *left and *right where it is.
long
GetMaxDxInGamma(long a, long b, long c, short* x, short* y, UByte kind, long* left, long* right)
{
	long yc = y[c];
	long best = 0;
	long bestLeft = c, bestRight = c;
	long step = 1;
	if (kind == 4)
	{
		for (;;)
		{
			long lim = (y[a] < y[b]) ? y[a] : y[b];
			if (lim - yc <= step)
				break;
			long level = yc + step;
			long l = c - 1;
			while (y[l] < level)
				l--;
			long r = c + 1;
			while (y[r] < level)
				r++;
			if (HWRAbs(x[r] - x[l]) > best)
			{
				best = HWRAbs(x[r] - x[l]);
				bestRight = r;
				bestLeft = l;
			}
			step++;
		}
	}
	else
	{
		for (;;)
		{
			long lim = (y[a] <= y[b]) ? y[b] : y[a];
			if (yc - lim <= step)
				break;
			long level = yc - step;
			long l = c - 1;
			while (y[l] > level)
				l--;
			long r = c + 1;
			while (y[r] > level)
				r++;
			if (HWRAbs(x[r] - x[l]) > best)
			{
				best = HWRAbs(x[r] - x[l]);
				bestRight = r;
				bestLeft = l;
			}
			step++;
		}
	}
	*left = bestLeft;
	*right = bestRight;
	return best;
}


// ROM 0x002cd9a4 IsEndOfStrokeInsideCross__FP13CrossInfoType
// Whether the end of the stroke the loop is in lies inside the loop.
long
IsEndOfStrokeInsideCross(CrossInfoType* ci)
{
	SPEC_TYPE* e = ci->elem;
	low_type* low = ci->low;
	short* y = low->fY;
	short* x = low->fX;
	long c = Mid(e->next);
	long len = Mid(e) - c + 1;
	long g = GetGroupNumber(low, c);
	long end = low->fGroups[g].iEnd;
	short flag;
	long inside = 0;
	if (IsPointInsideArea(&x[c], &y[c], len, x[end], y[end], &flag) == 0 && flag != 2)
		inside = 1;
	return inside;
}


// ROM 0x002cd860 Decision_GU_or_O___FP13CrossInfoType
// A top loop (4) that is really a closed letter (5, an o) - it starts or
// ends a stroke, or holds its stroke's end, or a lower extremum inside it
// overlaps its second pass, or it is round and tall - or 4 after all.
void
Decision_GU_or_O_(CrossInfoType* ci)
{
	SPEC_TYPE* e = ci->elem;
	SPEC_TYPE* lower = ci->lower;
	SPEC_TYPE* n = e->next;
	long width = ci->maxDx;
	long relY = ci->relY;
	long loop = ci->loop;
	long relX = ci->relX;
	short* y = ci->low->fY;
	Boolean startPen = (y[n->iBeg - 1] == -1);
	Boolean endPen = (y[e->iEnd + 1] == -1);
	UByte code;
	if (startPen)
	{
		if (endPen)
			goto closed;
		if (width > 0x3c)
			goto overlap;
	}
	if (!endPen && !IsEndOfStrokeInsideCross(ci))
		goto shape;
overlap:
	if (lower != nil && lower->code == 8 && n->iEnd >= lower->iBeg && lower->iEnd >= n->iBeg)
		goto closed;
shape:
	if (loop > 0x46 && width > 0x3c)
		goto closed;
	if (relX < 0x21 && relY > 0x26)
	{
		code = 4;
		goto store;
	}
	if (relY < 0x19)
		goto closed;
	if (relY < 0x57 && relX > 0x43)
		goto closed;
	if (loop <= 0x37)
		return;
	if ((e->attr & 0xf) > 3 || width > 0x3c)
		goto closed;
	return;
closed:
	code = 5;
store:
	e->code = code;
}


/*------------------------------------------------------------------------------
	T h e   s h a p e   t e s t s
------------------------------------------------------------------------------*/

// ROM 0x00305b0c SkipAnglesAfter__FP9SPEC_TYPE
// The next element that is not an arc.
SPEC_TYPE*
SkipAnglesAfter(SPEC_TYPE* e)
{
	if (e == nil)
		return nil;
	for (;;)
	{
		e = e->next;
		if (e == nil)
			return nil;
		UByte c = e->code;
		if (!(c == 0xe || c == 0x11 || c == 0x28 || c == 0x29))
			return e;
	}
}


// ROM 0x00305b48 SkipAnglesBefore__FP9SPEC_TYPE
SPEC_TYPE*
SkipAnglesBefore(SPEC_TYPE* e)
{
	if (e == nil)
		return nil;
	for (;;)
	{
		e = e->prev;
		if (e == nil)
			return nil;
		UByte c = e->code;
		if (!(c == 0xe || c == 0x11 || c == 0x28 || c == 0x29))
			return e;
	}
}


// ROM 0x00307154 iXYweighted_max_right__FPsT1iN33
// From point i on (to a pen-up), the point furthest along the direction
// (wx, wy); the walk stops once the trace falls back more than lim from
// the best so far.
long
iXYweighted_max_right(short* x, short* y, long i, long lim, long wx, long wy)
{
	long best = i;
	long most = LAdd(LMul(wx, x[i]), LMul(wy, y[i]));
	for (;;)
	{
		i++;
		if (y[i] == -1)
			return best;
		long v = LAdd(LMul(wx, x[i]), LMul(wy, y[i]));
		if (most - lim > v)
			return best;
		if (v > most)
		{
			best = i;
			most = v;
		}
	}
}


// ROM 0x00307c74 cos_normalslope__FiN21PsT4
// The cosine (hundredths) between the trace from point i to point j and
// the writing's slant.
long
cos_normalslope(long i, long j, long slope, short* x, short* y)
{
	return cos_pointvect(x[i], y[i], x[j], y[j], x[i], y[i], x[i] + 100, y[i] + slope);
}


// ROM 0x00308298 IsRightGulfLikeIn3__FPsT1iT3Pi
// Whether the trace from i down to j bulges to the right and back like a
// 3's middle: the furthest points right, left and right again (steps of
// an eighth of its height) with the turns the right way.  ==> 1 and *at
// the left turn; 0 and *at the rightmost point.
long
IsRightGulfLikeIn3(short* x, short* y, long i, long j, long* at)
{
	if (i <= j && y[i] != -1 && y[j] != -1 && y[i] < y[j])
	{
		long step = (y[j] - y[i] + 4) >> 3;
		if (step < 1)
			step = 1;
		long a = iXYweighted_max_right(x, y, i, step, 2, -1);
		if (a > i)
		{
			long b = iXYweighted_max_right(x, y, a, step, -2, 1);
			if (b > a)
			{
				long c = iXYweighted_max_right(x, y, b, step, 2, 1);
				if (c > b && c < j
				 && TriangleSquare(x, y, i, a, b) > 0
				 && TriangleSquare(x, y, a, b, c) < 0
				 && TriangleSquare(x, y, b, c, j) > 0)
				{
					*at = b;
					return 1;
				}
			}
		}
	}
	*at = ixMax(i, j, x, y);
	return 0;
}


// ROM 0x003096fc IsPointOnBorder__FPsT1iT3sT5PUi
// Whether the point (px, py) lies on the edge from point i to point j;
// *cross whether the level ray from x = 1 to it meets the edge.
long
IsPointOnBorder(short* xs, short* ys, long i, long j, short px, short py, ULong* cross)
{
	short ox, oy;
	long r = FindCrossPoint(1, py, px, py, xs[i], ys[i], xs[j], ys[j], &ox, &oy);
	*cross = r;
	if (r == 0)
	{
		if (ox == 0x7fff && oy == 0x7fff && ys[i] == py)
		{
			// the edge lies along the ray
			if (xs[j] <= px && xs[i] >= px)
				return 1;
			if (xs[j] < px || xs[i] > px)
				return 0;
			return 1;
		}
		return 0;
	}
	return (px == ox && py == oy) ? 1 : 0;
}


// ROM 0x003092cc IsPointInsideArea__FPsT1isT4T1
// Whether the point (px, py) is inside the polygon of n points, by the
// crossings of a level ray, the ends of level runs counted once: *where is
// 0 on the border, 1 inside, 2 outside.  ==> 0, 1 (with *where untouched)
// for a polygon of fewer than three points.
long
IsPointInsideArea(short* xs, short* ys, long n, short px, short py, short* where)
{
	long k;
	long odd = 0;
	Boolean fresh = true;
	long side = 0;
	// (the ROM leaves the side before a level run unset on paths that
	// never read it)
	long before = 0;
	ULong cross = 0;
	if (xs == nil || ys == nil || n < 3)
		return 1;
	for (k = 0; k < n && xs[k] > px; k++)
		;
	if (k == n)
		goto outside;
	if (k != 0)
		fresh = false;
	else
	{
		k = 1;
		long d = ys[0] - py;
		if (d == 0)
		{
			side = 0;
			before = 0;
		}
		else
			side = (d >= 0) ? 1 : -1;
	}
	for (long last = n - 1; k <= last; k++)
	{
		long xk = xs[k];
		if (xk == px && xs[k - 1] == px)
		{
			long yk = ys[k];
			if (!(yk < py || py < ys[k - 1]) || !(yk > py || py > ys[k - 1]))
				goto border;
		}
		if (xk > px)
		{
			if (!fresh)
				continue;
			fresh = false;
			if (IsPointOnBorder(xs, ys, k - 1, k, px, py, &cross))
				goto border;
			long d = ys[k - 1] - py;
			if (d == 0)
			{
				if (side != 0)
					before = side;
				side = 0;
				goto level;
			}
			if (cross)
				odd = !odd;
			side = (d >= 0) ? 1 : -1;
			continue;
		}
		if (!fresh)
		{
			fresh = true;
			if (IsPointOnBorder(xs, ys, k - 1, k, px, py, &cross))
				goto border;
			long d = ys[k] - py;
			if (d == 0)
			{
				side = 0;
				before = (ys[k - 1] - py >= 0) ? 1 : -1;
				continue;
			}
			if (cross)
				odd = !odd;
			side = (d >= 0) ? 1 : -1;
			continue;
		}
	level:
		{
			long yk = ys[k];
			if (yk == ys[k - 1])
				continue;
			long d = yk - py;
			if ((d < 0 && side < 0) || (d > 0 && side > 0))
				continue;
			if (d == 0)
			{
				before = side;
				side = 0;
				continue;
			}
			if (side != 0)
			{
				side = -side;
				odd = !odd;
				continue;
			}
			side = (d >= 0) ? 1 : -1;
			if (before == 0 || side == before)
				continue;
			odd = !odd;
		}
	}
	if (IsPointOnBorder(xs, ys, n - 1, 0, px, py, &cross))
		goto border;
	if (cross)
	{
		long y0 = ys[0];
		long yl = ys[n - 1];
		if (py != y0 && py != yl)
			odd = !odd;
		else
		{
			long s = (y0 - yl >= 0) ? 1 : -1;
			if (yl != py)
			{
				long m = 1;
				while (m < n - 1 && ys[m] == y0)
					m++;
				if (m != n - 1 && ((y0 - ys[m] >= 0) ? 1 : -1) != s)
					odd = !odd;
			}
			else if (before != s)
				odd = !odd;
		}
	}
	if (odd)
	{
		*where = 1;
		return 0;
	}
outside:
	*where = 2;
	return 0;
border:
	*where = 0;
	return 0;
}


// ROM 0x002cbbe0 IsShapeDUR__FP9SPEC_TYPEN31P8low_type
// Whether the trace between q and r swings further left than q's end,
// the stick's left side and p's end: the bowl of a d.
long
IsShapeDUR(SPEC_TYPE* p, SPEC_TYPE* q, SPEC_TYPE* r, SPEC_TYPE* stick, low_type* low)
{
	short* x = low->fX;
	short* y = low->fY;
	short left = (short) ((x[stick->iBeg] < x[stick->iEnd]) ? x[stick->iBeg] : x[stick->iEnd]);
	short xMin, xMax;
	xMinMax(q->iEnd + 1, r->iBeg - 1, x, y, &xMin, &xMax);
	if (x[q->iEnd] > xMin && left > xMin && x[p->iEnd] > xMin)
		return 1;
	return 0;
}


static inline Boolean
IsPassOver(SPEC_TYPE* e)
{
	UByte m = e->mark;
	UByte c = e->code;
	return m == 6 || m == 0xa || m == 9 || c == 0xe || c == 0x11 || c == 0x28 || c == 0x29;
}


// ROM 0x002cb58c IsDUR__FP9SPEC_TYPEN21P8low_type
// Whether a crossing is where a d's stem comes back up over its bowl: the
// extremum the bowl would have is taken out and the crossing coded an arc
// (0x15).  ==> whether it was.
long
IsDUR(SPEC_TYPE* e, SPEC_TYPE* a, SPEC_TYPE* b, low_type* low)
{
	short* y = low->fY;
	SPEC_TYPE* partner = e->next;
	long found = 0;
	if (e->mark == 9 && HeightInLine(y[e->iBeg], low) <= 9)
	{
		if (a != nil && b != nil)
		{
			Boolean after = a->iBeg < b->iBeg;
			UByte band = a->attr & 0x30;
			Boolean ok = after ? (band == 0x10) : (band == 0x20 && a->code != 3);
			if (ok)
			{
				SPEC_TYPE* r = after ? SkipAnglesAfter(b) : SkipAnglesBefore(b);
				while (r != nil && IsPassOver(r))
					r = after ? r->next : r->prev;
				if (r != nil && (r->code == 2 || (r->code == 3 && (b->attr & 0xf) < 9)))
				{
					UByte rb = r->attr & 0x30;
					if ((after ? rb == 0x20 : rb == 0x10)
					 && HWRAbs(y[a->ipoint0] - y[r->ipoint0]) < 0x28)
					{
						DelFromSPECLList(r);
						found = 1;
					}
				}
			}
		}
		else if (a != nil)
			return 0;
		else if (b == nil)
		{
			SPEC_TYPE* t = SkipAnglesBefore(e);
			if (t != nil && t->code == 2 && (t->attr & 0x30) == 0x10
			 && t->iEnd >= partner->iBeg && partner->iEnd >= t->iBeg)
				found = 1;
		}
		else
		{
			SPEC_TYPE* p = SkipAnglesBefore(b);
			if (p != nil && p->code == 2 && (p->attr & 0x30) == 0x10)
			{
				SPEC_TYPE* q = SkipAnglesAfter(b);
				while (q != nil && IsPassOver(q))
					q = q->next;
				if (q != nil && (q->code == 3 || q->code == 2) && (q->attr & 0x30) == 0x20)
				{
					long yb = y[b->ipoint0];
					if (yb - y[p->ipoint0] < 0x50 && yb - y[q->ipoint0] < 0x50)
					{
						SPEC_TYPE* r = q->next;
						while (r != nil && IsPassOver(r))
							r = r->next;
						if (r != nil && r->code == 8 && (r->attr & 0x30) == 0x20
						 && IsShapeDUR(p, q, r, b, low))
						{
							DelFromSPECLList(q);
							found = 1;
						}
					}
				}
			}
		}
	}
	else if (e->mark == 6)
	{
		SPEC_TYPE* p = b->prev;
		while (p != nil && IsPassOver(p))
			p = p->prev;
		if (p != nil && (p->code == 3 || p->code == 2))
		{
			SPEC_TYPE* q = b->next;
			while (q != nil && IsPassOver(q))
				q = q->next;
			if (q != nil && (q->code == 3 || q->code == 2)
			 && (p->attr & 0x30) == 0x10 && (q->attr & 0x30) == 0x20
			 && HWRAbs(y[p->ipoint0] - y[q->ipoint0]) < 0x28)
			{
				SPEC_TYPE* r = q->next;
				while (r != nil && IsPassOver(r))
					r = r->next;
				if (r != nil && r->code == 8 && (r->attr & 0x30) == 0x20)
				{
					long yr = y[r->ipoint0];
					if (yr - y[p->ipoint0] > 0x1b && yr - y[q->ipoint0] > 0x1b && yr - y[b->ipoint0] > 0x14
					 && IsShapeDUR(p, q, r, b, low))
					{
						DelFromSPECLList(p);
						DelFromSPECLList(q);
						found = 1;
					}
				}
			}
		}
	}
	else
		return 0;
	if (!found)
		return 0;
	e->code = 0x15;
	short yMin, yMax;
	yMinMax(partner->iBeg, e->iEnd, y, &yMin, &yMax);
	e->attr = (HeightInLine(yMin, low) & 0xf) | 0x10;
	return 1;
}


// ROM 0x002cb25c is_DDL__FP9SPEC_TYPET1P8low_type
// Whether a crossing is a d's loop at the bottom left (0x1c): a lower
// extremum at the bottom just after it (or before it), an upper extremum
// before it low and at the left, and the lower extremum's height close to
// it.  ==> whether it was coded so.
long
is_DDL(SPEC_TYPE* e, SPEC_TYPE* upper, low_type* low)
{
	short* x = low->fX;
	short* y = low->fY;
	SPEC_TYPE* partner = e->next;
	SPEC_TYPE* after = partner->next;
	while (after != nil && (after->mark == 6 || after->mark == 5
		|| after->code == 0xe || after->code == 0x11 || after->code == 0x28 || after->code == 0x29))
		after = after->next;
	Boolean near = false;
	// DEVIATION: an element list ends with a break, so `after` is never
	// nil; the ROM would read its code from address 1
	if (after != nil)
	{
		if (after->code == 8)
			near = (after->attr & 0x30) == 0x20;
		else if (after->code == 7 && after->mark == 0x20 && x[after->iBeg] < x[after->iEnd])
			near = true;
		if (near && !(e->iEnd + 0xf >= after->iBeg))
			near = false;
	}
	if (!near)
	{
		after = e->prev;
		if (!(after->code == 8 && (after->attr & 0x30) == 0x20))
			return 0;
	}
	long right = 1;
	long hook = 0;
	SPEC_TYPE* p = upper->prev;
	while (p->mark == 6 || p->code == 0xe || p->code == 0x11 || p->code == 0x28 || p->code == 0x29)
		p = p->prev;
	long foot = (p->code == 8 && (p->attr & 0x30) == 0x10 && partner->iBeg - 0xf <= p->iEnd) ? 1 : 0;
	long yp = y[Mid(p)];
	if (foot && upper->code == 2 && (upper->attr & 0x30) == 0x20 && (upper->attr & 0xf) < 7
	 && HWRAbs(y[Mid(upper)] - yp) > 0x35)
	{
		SPEC_TYPE* q = p->prev;
		if (q != nil && q->mark == 0x10 && q->code == 3)
			hook = 1;
	}
	if (RCGetH(low->rc, 0x92) != 2)
		right = (x[Mid(p)] > x[Mid(upper)]) ? 1 : 0;
	if (foot && right && HWRAbs(y[Mid(after)] - yp) < 0x35 && hook == 0)
	{
		e->code = 0x1c;
		short yMin, yMax;
		yMinMax(partner->iBeg, e->iEnd, y, &yMin, &yMax);
		// (the lowest point, where IsDUR takes the highest)
		e->attr = (HeightInLine(yMax, low) & 0xf) | 0x10;
		return 1;
	}
	return 0;
}


// ROM 0x002cbcbc check_IU_ID_in_crossing__FPP9SPEC_TYPEPsT2
// A crossing coded 3 or 7 at a stroke's end marked as that end (0x10 or
// 0x20) when what follows it (or goes before it) is not the kind that
// continues a letter; otherwise its height band set by the neighbour, or
// by which way the stick leans.
void
check_IU_ID_in_crossing(SPEC_TYPE** pe, short* x, short* y)
{
	SPEC_TYPE* e = *pe;
	SPEC_TYPE* n = e->next;
	for ( ; ; n = n->next)
	{
		if (n == nil)
			return;
		if (n->mark != 6)
			break;
	}
	SPEC_TYPE* p = e->prev;
	for ( ; ; p = p->prev)
	{
		if (p == nil)
			return;
		if (p->mark != 6)
			break;
	}
	SPEC_TYPE* by;
	if (y[e->iBeg - 1] == -1)
	{
		UByte c = n->code;
		if (!(c == 2 || c == 8 || c == 0x22 || c == 0x21 || ((c == 3 || c == 7) && n->mark != 0x20)))
		{
			e->mark = 0x10;
			e->other |= 1;
			return;
		}
		by = n;
	}
	else if (y[e->iEnd + 1] == -1)
	{
		UByte c = p->code;
		if (!(c == 2 || c == 8 || c == 0x22 || c == 0x21 || ((c == 3 || c == 7) && p->mark != 0x10)))
		{
			e->mark = 0x20;
			e->other |= 1;
			return;
		}
		by = p;
	}
	else
	{
		if (e->other != 0)
			return;
		Boolean low = (x[e->iBeg] < x[e->iEnd]) ? (e->code == 3) : (e->code != 3);
		e->attr = (e->attr & ~0x30) | (low ? 0x10 : 0x20);
		return;
	}
	e->attr = (e->attr & ~0x30) | (((by->attr & 0x30) == 0x10) ? 0x20 : 0x10);
}


// ROM 0x002cac98 Restore_AN__FP8low_typeP9SPEC_TYPEUcs
// An angle (mark 0xb) lk_begin left out of the list, of the kind asked
// for and overlapping the element, put back after it (or after the one
// after it, mode 2) as an arc (0xe).
void
Restore_AN(low_type* low, SPEC_TYPE* e, UByte kind, short mode)
{
	SPEC_TYPE* specl = low->fSpecl;
	short* y = low->fY;
	short n = low->fLenSpecl;
	SPEC_TYPE* after = e;
	for (short i = 0; i < n; i = (short) (i + 1))
	{
		SPEC_TYPE* a = &specl[i];
		if (a->mark != 0xb || (a->other & kind) != kind)
			continue;
		if (e->iEnd < a->iBeg || e->iBeg > a->iEnd || (a->other & 0xf0) != 0x40)
			continue;
		a->code = 0xe;
		a->attr = (UByte) HeightInLine(y[a->ipoint0], low);
		a->other ^= 1;
		if (e->code != 0)
			a->other |= 0x40;
		if (mode == 2)
			after = e->next;
		Insert2ndAfter1st(after, a);
		return;
	}
}


// ROM 0x002cada0 IsOutsideOfCrossing__FP9SPEC_TYPEN21P8low_typePP9SPEC_TYPET5PUi
// Whether an element before a loop's second pass is really outside the
// loop (it ends before the loop begins, by the loop's kind): it is marked
// 0x80 and moved after the second pass.  ==> 1 and *prev (where the walk
// goes on from), *moved and *flag set; 0.
long
IsOutsideOfCrossing(SPEC_TYPE* e, SPEC_TYPE* r, SPEC_TYPE* partner, low_type* low, SPEC_TYPE** prev, SPEC_TYPE** moved, ULong* flag)
{
	UByte c = e->code;
	Boolean closed = (c == 6 || c == 5);
	Boolean endLow = false;
	if (r->mark == 1)
	{
		SPEC_TYPE* n = partner->next;
		if (n == nil)
			endLow = true;
		else if ((n->code == 7 || n->code == 8) && MidPointHeight(n, low) >= 8)
			endLow = true;
	}
	Boolean firm = (c == 3 || c == 7 || c == 0x1d || c == 0x1e || c == 0x1f || c == 0x20 || c == 4 || c == 6)
				&& (r->mark == 3 || r->mark == 1 || r->mark == 9 || r->mark == 5);
	long lim;
	if (c == 3 || c == 7 || c == 4 || c == 6)
		lim = e->iBeg;
	else if (c == 5)
		lim = ((e->attr & 0x30) == 0x20) ? e->iBeg : e->iEnd;
	else if (c == 0x1d)
		lim = (r->code == 8) ? e->iBeg : e->iEnd;
	else if (c == 0x1e && r->code == 2)
		lim = e->iBeg;
	else
		lim = e->iEnd;
	if (lim <= r->iEnd)
	{
		if (firm || (c == 5 && r->mark == 5) || (closed && (r->mark == 9 || endLow)))
			goto outside;
	}
	if (r->code != 0x27)
		return 0;
	if (c == 4)
	{
		if ((e->attr & 0x30) != 0x20)
			return 0;
	}
	else if (!(c == 0x1d || c == 0x1f))
		return 0;
outside:
	r->other |= 0x80;
	*prev = r->prev;
	Move2ndAfter1st(partner, r);
	*moved = r;
	*flag = 1;
	return 1;
}


// ROM 0x002caf8c CheckInsideCrossing__FP9SPEC_TYPET1Ps
// An element inside a loop: taken out when the loop's kind says it
// cannot be part of the letter, marked 0x40 when it can (up to two
// extrema the same height band as a closed loop recoded 0x21/0x22).
void
CheckInsideCrossing(SPEC_TYPE* e, SPEC_TYPE* r, short* count)
{
	UByte c = e->code;
	UByte rc;
	switch (c)
	{
	case 3: case 0x1d:
		goto tops;
	case 7: case 0x1e:
		goto bottoms;
	case 0x1f: case 0x20: case 0x1c: case 0x15:
		goto remove;
	}
	if (c == 4)
	{
		if ((e->attr & 0x30) == 0x20)
			goto tops;
	}
	else if (c == 6 && (e->attr & 0x30) == 0x10)
		goto bottoms;
	rc = r->code;
	if (!(rc == 0xe || rc == 0x11 || rc == 0x28 || rc == 0x29 || rc == 7))
	{
		if ((rc == 2 || rc == 8) && *count < 2
		 && !(r->iEnd >= e->iBeg && e->iEnd >= r->iBeg)
		 && (r->attr & 0x30) == (e->attr & 0x30))
		{
			*count = (short) (*count + 1);
			r->code = (rc == 2) ? 0x21 : 0x22;
		}
		goto keep;
	}
	if (!(c == 4 || c == 3 || c == 0x1d))
		goto notTops;
tops:
	rc = r->code;
	if (rc == 7 || rc == 8 || rc == 0x22)
		goto keep;
notTops:
	if (!(c == 6 || c == 7 || c == 0x1e))
		goto closedLoop;
bottoms:
	rc = r->code;
	if (rc == 3 || rc == 2 || rc == 0x21)
		goto keep;
closedLoop:
	if (c == 5 && r->code == 7)
	{
		SPEC_TYPE* n = e->next;
		if (r->iEnd >= n->iBeg && n->iEnd >= r->iBeg)
			return;
	}
remove:
	DelFromSPECLList(r);
	return;
keep:
	r->other |= 0x40;
}


// ROM 0x002cb138 IsInnerAngle__FPsT1P9SPEC_TYPEN23
// Whether an arc restored inside a top loop (4 or 5, low band) is the
// inner corner of a 3-like bulge between the loop's passes: marked 0x48.
long
IsInnerAngle(short* x, short* y, SPEC_TYPE* partner, SPEC_TYPE* e, SPEC_TYPE* r)
{
	if (!(r->code == 0xe && (r->other & 0xf0) == 0x40))
		return 0;
	if (!(e->code == 4 || e->code == 5) || (e->attr & 0x30) != 0x10)
		return 0;
	if (e->iEnd >= r->iBeg && r->iEnd >= e->iBeg)
		return 0;
	long pe = partner->iEnd;
	if (pe >= r->iBeg && r->iEnd >= partner->iBeg)
		return 0;
	long eb = e->iBeg;
	long top = iyMin(pe, eb, y);
	long bottom = iyMax(pe, eb, y);
	if (top == -1 || bottom == -1 || top >= bottom)
		return 0;
	long at;
	if (!IsRightGulfLikeIn3(x, y, top, bottom, &at))
		return 0;
	r->other |= 0x48;
	return 1;
}


// ROM 0x002ca8a0 del_inside_circles__FP8low_type
// The crossings still uncoded taken out (a break before them recoded by
// which way the stroke goes, and an angle restored); for a coded one the
// elements its loop swallowed are taken out or moved outside, and the
// pair made one element spanning both passes.  ==> 0.
long
del_inside_circles(low_type* low)
{
	short* x = low->fX;
	short* y = low->fY;
	SPEC_TYPE* head = low->fSpecl;
	for (SPEC_TYPE* e = head; e != nil; e = e->next)
	{
		if (e->mark != 6)
			continue;
		if (e->code == 0)
		{
			SPEC_TYPE* partner = e->next;
			SPEC_TYPE* dot = FindMarkLeft(e->prev, 0x10);
			if (dot != nil)
			{
				SPEC_TYPE* b = dot->prev;
				while (b != nil && (b->code == 0xd || b->code == 0x10 || b->mark == 0xa))
					b = b->prev;
				if (b != nil && (b->code == 0x12 || b->code == 1 || b->code == 0x13)
				 && partner->iBeg <= b->iBeg)
				{
					if (b->code == 0x12)
						b->code = (x[e->iBeg] - x[dot->iBeg] < 0) ? 1 : 0x13;
					Restore_AN(low, e, 3, 2);
				}
			}
			e = e->prev;
			DelCrossingFromSPECLList(e->next);
			continue;
		}
		short count = 0;
		SPEC_TYPE* partner = e->next;
		SPEC_TYPE* r = e->prev;
		SPEC_TYPE* resume = nil;
		SPEC_TYPE* moved = partner;
		ULong flag = 0;
		if (r->mark == 6)
			r = r->prev;
		// back from the second pass to the first, stopping at a dot, a
		// crossing already coded or the list's head
		for (;;)
		{
			if (r->iBeg < partner->iBeg || r->mark == 0x10 || r == head)
				break;
			if (r->mark == 6 && r->code != 0)
				break;
			if (IsOutsideOfCrossing(e, r, partner, low, &resume, &moved, &flag) == 0
			 && IsInnerAngle(x, y, partner, e, r) == 0)
				CheckInsideCrossing(e, r, &count);
			r = (flag == 0) ? r->prev : resume;
			flag = 0;
			if (r->mark == 6)
				r = r->prev;
		}
		SPEC_TYPE* p = e->prev;
		SPEC_TYPE* pn = partner->next;
		Boolean restore;
		if (p->code == 0xe && p->iEnd >= e->iBeg && e->iEnd >= p->iBeg)
			restore = false;
		else if (pn->code == 0xe && pn->iEnd >= e->iBeg && e->iEnd >= pn->iBeg)
			restore = false;
		else if (e->code == 5)
		{
			if ((e->attr & 0x30) != 0x20)
				restore = false;
			else if (p->code == 2 || p->code == 0x21 || p->code == 3)
				restore = (p->attr & 0x30) != 0x20;
			else
				restore = true;
		}
		else
		{
			Boolean check = false;
			if (e->code == 4)
				check = true;
			else if (e->code == 0x1d)
			{
				if ((e->other & 4) == 0)
					goto skip;
				check = true;
			}
			if (check && (e->attr & 0x30) == 0x10)
			{
				if (!(p->code == 8 || p->code == 0x22 || p->code == 7) || (p->attr & 0x30) != 0x10)
				{
					restore = true;
					goto decided;
				}
			}
			restore = (e->code == 3 && (e->attr & 0x30) == 0x10 && (moved->attr & 0x30) == 0x10);
		}
	decided:
		if (restore)
			Restore_AN(low, e, 3, 2);
	skip:
		if (e->code != 0x15 && e->code != 0x1c)
			e->ipoint1 = e->iBeg;
		e->iBeg = partner->iBeg;
		DelFromSPECLList(partner);
		if (e->code == 3 || e->code == 7)
			check_IU_ID_in_crossing(&e, x, y);
	}
	return 0;
}


/*------------------------------------------------------------------------------
	T h e   s t i c k s
------------------------------------------------------------------------------*/

// ROM 0x002cca28 cross_little__FP9SPEC_TYPE
// A crossing pair taken out.  ==> the element after the pair's first,
// which is the pair's second (its links are left as they were).
SPEC_TYPE*
cross_little(SPEC_TYPE* e)
{
	DelCrossingFromSPECLList(e);
	return e->next;
}


static inline Boolean
IsArcCode(UByte c)
{
	return c == 0xe || c == 0x11 || c == 0x28 || c == 0x29;
}

static inline Boolean
IsCrossingMark(UByte m)
{
	return m == 6 || m == 0xa || m == 9;
}


// ROM 0x002ca0a0 EndIUIDNearStick__FP9SPEC_TYPET1PsT3
// A stroke that comes back up along itself (a stick, mark 9) and ends
// just after: the stick's pair coded by where it bends, from the middle
// of the trace before it - 0x18/0x15 when the stick goes down to its
// start, 0x1c/0x19 when up, the larger by the bend's side.  ==> whether
// it was.
long
EndIUIDNearStick(SPEC_TYPE* end, SPEC_TYPE* partner, short* x, short* y)
{
	if (partner == nil || partner->mark != 9 || end->iBeg < partner->iBeg)
		return 0;
	SPEC_TYPE* e = partner->prev;
	long b = e->iBeg;
	long pb = e->next->iBeg;
	long ee = e->iEnd;
	if (ee <= ((end->iBeg + end->iEnd) >> 1))
		return 0;
	if (HWRAbs(y[ee] - y[b]) >= (HWRAbs(x[ee] - x[b]) >> 1))
		return 0;
	SPEC_TYPE* r = e->prev;
	if (r == nil)
		return 0;
	for (;;)
	{
		if (!IsCrossingMark(r->mark)
		 && !(IsArcCode(r->code) && HWRAbs(y[r->iEnd] - y[pb]) < 10)
		 && pb > r->iBeg)
			break;
		r = r->prev;
		if (r == nil)
			return 0;
	}
	if (r->prev == nil)
		return 0;
	long mid = (pb + Mid(r)) >> 1;
	if (r->iEnd > pb)
	{
		for (r = r->prev; r != nil; r = r->prev)
		{
			if (!IsCrossingMark(r->mark)
			 && !(IsArcCode(r->code) && HWRAbs(y[r->iEnd] - y[pb]) < 10)
			 && pb >= r->iEnd)
			{
				if (r->prev != nil)
					mid = (pb + Mid(r)) >> 1;
				break;
			}
		}
	}
	long k = iMostFarFromChord(x, y, mid, b);
	if (x[k] == x[b])
		k = (mid + k) >> 1;
	UByte code;
	if (y[mid] > y[b])
		code = (x[k] >= x[b]) ? 0x18 : 0x15;
	else
		code = (x[k] >= x[b]) ? 0x1c : 0x19;
	e->code = code;
	return 1;
}


// ROM 0x002cbe54 analize_sticks__FP8low_type
// Each stick (a crossing where the pen comes back along itself, mark 9):
// the extrema the pen passes between going and coming back are counted -
// uppers and lowers, and among them the ones coded as the stroke's own
// turn (3, 7) - and the stick coded by them: 3 (a stroke up and down), 7,
// 0x15/0x18/0x19/0x1c (a hook one way or another), 0x1f/0x20 (flat), or
// taken out altogether when the pen barely moved; the extrema it covers
// are then taken out and the pair made one element.  ==> 0.
long
analize_sticks(low_type* low)
{
	SPEC_TYPE* head = low->fSpecl;
	short* x = low->fX;
	short* y = low->fY;
	long slope = low->fSlope;
	for (SPEC_TYPE* e = head; e != nil; e = e->next)
	{
		if (e->mark != 9)
			continue;
		SPEC_TYPE* partner = e->next;
		SPEC_TYPE* r = e->prev;
		short uppers = 0, turnsUp = 0, lowers = 0, turnsDown = 0;
		// (the ROM leaves these unset until an extremum is counted, and
		// reads them only when one was)
		SPEC_TYPE* up = nil;
		SPEC_TYPE* lo = nil;
		UByte band = 0;
		long eb = e->iBeg;
		SPEC_TYPE* end = FindMarkRight(e, 0x20);
		if (end != nil)
		{
			if ((end->code == 3 || end->code == 7) && EndIUIDNearStick(end, partner, x, y))
			{
				e->attr = HeightInLine(y[e->iBeg], low) & 0xf;
				goto finish;
			}
			if (end->iEnd == e->iEnd && DistanceSquare(eb, end->iEnd, x, y) < 0x320)
			{
				e = cross_little(e);
				end->other |= 2;
				continue;
			}
		}
		if (r->mark == 6)
			r = r->prev;
		{
			long pb = partner->iBeg;
			while (!(pb > r->iBeg) && r != head)
			{
				UByte m = r->mark;
				if (m == 1)
				{
					up = r;
					if (r->code != 3)
						uppers++;
					else
						turnsUp++;
				}
				else if (m == 3)
				{
					lo = r;
					if (r->code != 7)
						lowers++;
					else
						turnsDown++;
				}
				else if (m == 0x10)
				{
					// the stroke starts at the stick: a tiny one is no stick
					if (pb != r->iBeg || DistanceSquare(partner->iEnd, r->iBeg, x, y) >= 0x320)
						break;
					e = cross_little(e);
					if (turnsDown == 1 || lowers == 1)
						lo->other |= 2;
					if (turnsUp == 1 || uppers == 1)
						up->other |= 2;
					if (turnsUp == 0 && uppers == 0 && turnsDown == 0 && lowers == 0
					 && (r->code == 3 || r->code == 7))
						r->other |= 2;
					goto next;
				}
				r = r->prev;
				if (r->mark == 6)
					r = r->prev;
			}
		}

		if (turnsUp == 1)
		{
			if (turnsDown != 1)
				goto oneUpper;
			if (uppers != 0)
				goto mixed;
			if (lowers != 0)
				goto flat;
			if (IsDUR(e, up, lo, low))
				goto finish;
			up->attr = (up->attr & ~0x30) | 0x10;
			lo->attr = (lo->attr & ~0x30) | 0x20;
			e = cross_little(e);
			continue;
		}
		if (turnsUp == 0)
		{
			if (turnsDown != 0 || uppers != 0)
				goto oneUpperMaybe;
			if (lowers != 0)
				goto oneLower;
			if (IsDUR(e, nil, nil, low))
				goto finish;
			e->code = (x[e->iBeg] <= x[e->iEnd]) ? 0x1f : 0x20;
			e->attr = (UByte) HeightInLine(y[e->iBeg], low);
			goto finish;
		}
	oneUpperMaybe:
		if (uppers != 1)
			goto oneLowerMaybe;
	oneUpper:
		if (turnsDown != 0)
			goto oneLowerMaybe;
		if (lowers != 0)
			goto oneLower;
		if (is_DDL(e, up, low))
			goto finish;
		{
			long ext = extremum(up->mark, up->iBeg, up->iEnd, y);
			long c = cos_normalslope(partner->iBeg, partner->iBeg + (partner->iEnd - partner->iBeg + 1) / 3, slope, x, y);
			long lim = ((up->attr & 0xf) <= 4) ? 0x55 : 0x4b;
			if (HWRAbs((short) c) < lim)
			{
				band = up->attr & 0x30;
				e->code = 3;
			}
			else
			{
				band = 0;
				if (uppers == 1 && up->iEnd > e->iEnd)
					e->code = 0x18;
				else
					e->code = (x[ext] < x[partner->iBeg]) ? 0x18 : 0x15;
			}
			goto setHeight;
		}
	oneLowerMaybe:
		if (turnsDown == 1)
			goto oneLowerChecked;
	oneLower:
		if (lowers != 1)
			goto mixed;
	oneLowerChecked:
		if (turnsUp != 0 || uppers != 0)
			goto mixed;
		if (IsDUR(e, nil, lo, low))
			goto finish;
		{
			long c = cos_normalslope(partner->iBeg, partner->iBeg + (partner->iEnd - partner->iBeg + 1) / 3, slope, x, y);
			if (HWRAbs((short) c) < 0x46)
			{
				band = lo->attr & 0x30;
				e->code = 7;
			}
			else
			{
				band = 0;
				e->code = (x[e->iBeg] > x[partner->iBeg]) ? 0x19 : 0x1c;
			}
			goto setHeight;
		}
	mixed:
		if (uppers == 1)
		{
			if (lowers == 1)
			{
				long du = HWRAbs(x[up->iBeg] - x[up->iEnd]);
				long dl = HWRAbs(x[lo->iBeg] - x[lo->iEnd]);
				if (du > dl && IsDUR(e, up, lo, low))
					goto finish;
				if (du < dl)
					is_DDL(e, up, low);
				goto finish;
			}
			if (turnsDown != 1)
				goto flat;
		}
		else if (uppers == 2)
		{
			if (turnsDown + lowers != 1)
				goto flat;
		}
		else
			goto flat;
		// an upper and a lower turn of the stroke's own: a stick down to
		// the line (7), or a hook (0x18)
		if (IsDUR(e, up, lo, low))
			goto finish;
		if ((up->attr & 0x30) == 0x10)
			goto finish;
		if (HWRAbs(y[Mid(up)] - y[Mid(lo)]) > 0x3c)
		{
			e->code = 7;
			e->attr = (HeightInLine(y[lo->ipoint0], low) & 0xf) | (lo->attr & 0x30);
			SPEC_TYPE* pn = partner->next;
			if (pn->code == 2 && (up->attr & 0x30) == (pn->attr & 0x30))
				up->code = 3;
			goto finish;
		}
		e->code = 0x18;
		e->attr = HeightInLine(y[up->ipoint0], low) & 0xf;
		goto finish;
	flat:
		if (lowers == 1 && turnsUp == 1)
		{
			if (is_DDL(e, up, low))
				goto finish;
			long c = cos_normalslope(partner->iBeg, partner->iEnd, slope, x, y);
			if (HWRAbs(c) <= 0x46)
				goto finish;
			e->code = (x[e->iEnd] <= x[e->iBeg]) ? 0x20 : 0x1f;
			e->attr = up->attr & 0xf;
		}
		goto finish;
	setHeight:
		e->attr = (HeightInLine(y[e->iBeg], low) & 0xf) | (band & 0x30);
	finish:
		{
			SPEC_TYPE* p = e->prev;
			if (e->code == 0)
			{
				DelCrossingFromSPECLList(e);
				e = p;
				continue;
			}
			// the extrema the stick covers taken out (bar the ones of its
			// own kind), and for a hook or a d's loop the element at its
			// foot
			if (p->mark == 6)
				p = p->prev;
			while (!(p->iBeg < partner->iBeg) && !(p->mark == 0x10 || p == nil))
			{
				if (p->mark != 6)
				{
					Boolean keep;
					if (e->code == 3)
						keep = (p->code == 7 || p->code == 8);
					else if (e->code == 7)
						keep = (p->code == 3 || p->code == 2);
					else
						keep = false;
					if (!keep)
						DelFromSPECLList(p);
				}
				p = p->prev;
				if (p->mark == 6)
					p = p->prev;
			}
			SPEC_TYPE* after = p->next;
			if (e->code == 0x15)
			{
				if (p->code == 2 && p->iEnd >= partner->iBeg)
					p = p->prev;
				if (p->mark == 0x10 && p->code == 7 && p->iEnd >= partner->iBeg)
					p = p->prev;
				p->next = after;
				if (after != nil)
					after->prev = p;
			}
			if (e->code == 0x1c)
			{
				if (p->code != 8 || partner->iBeg - 2 > p->iEnd)
					goto join;
				SPEC_TYPE* q = p->prev;
				q->next = after;
				if (after != nil)
					after->prev = q;
			}
			if (e->code == 3 || e->code == 0x15)
				Restore_AN(low, e, 5, 2);
		}
	join:
		{
			SPEC_TYPE* pt = e->next;
			e->ipoint0 = e->iBeg;
			UByte c = e->code;
			if (c == 0x15 || c == 0x18 || c == 0x1c)
				e->ipoint1 = pt->iEnd;
			else if (c == 3)
			{
				if (turnsUp == 1 && turnsDown == 0 && uppers == 0 && lowers == 0)
					e->ipoint0 = up->ipoint0;
			}
			else if (c == 7 && turnsDown == 1 && turnsUp == 0 && uppers == 0 && lowers == 0)
				e->ipoint0 = lo->ipoint0;
			e->iBeg = pt->iBeg;
			SPEC_TYPE* n = pt->next;
			e->next = n;
			if (n != nil)
				n->prev = e;
			if (e->code == 0x15)
			{
				if (n->code != 2 || !(e->iEnd >= n->iBeg && n->iEnd >= e->iBeg))
					continue;
				n = n->next;
				e->next = n;
				if (n != nil)
					n->prev = e;
			}
			if (e->code == 0x1c)
			{
				if (!((n->code == 3 || n->code == 7) && n->mark == 0x20))
					continue;
				if (!(e->iEnd >= n->iBeg && n->iEnd >= e->iBeg && e->iEnd >= n->iEnd))
					continue;
				DelFromSPECLList(n);
			}
			if (e->code == 3 || e->code == 7)
				check_IU_ID_in_crossing(&e, x, y);
		}
	next:
		;
	}
	return 0;
}


/*------------------------------------------------------------------------------
	T h e   l o o p s
------------------------------------------------------------------------------*/

// ROM 0x002cca44 analize_circles__FP8low_type
// See the file's comment.  ==> 0.
long
analize_circles(low_type* low)
{
	SPEC_TYPE* head = low->fSpecl;
	short* x = low->fX;
	short* y = low->fY;
	for (SPEC_TYPE* e = head; e != nil; e = e->next)
	{
		if (!(e->mark == 6 && e->code == 0))
			continue;
		SPEC_TYPE* partner = e->next;
		SPEC_TYPE* lowerIn = nil;		// the last lower extremum inside the loop
		SPEC_TYPE* lowerOut = nil;		// outside it
		SPEC_TYPE* upperOut = nil;
		long uppers = 0, turnsUp = 0, lowers = 0, turnsDown = 0;		// outside
		long inUppers = 0, inTurnsUp = 0, inLowers = 0, inTurnsDown = 0;
		UByte band = 0;
		long stick = 0;					// a stick (9, coded 3) inside
		CrossInfoType ci;
		e->other = 0;
		SPEC_TYPE* r = e->prev;
		long pb = partner->iBeg;
		// back to the element before the loop, past the ones inside it
		if (r->mark == 6)
		{
			if (r->code != 0 && !(pb > r->iEnd) && e->iEnd >= r->prev->iEnd)
				goto skip;
			r = r->prev;
		}
		else
		{
			if (r->mark == 9)
			{
				do
				{
					if (!(pb > r->iBeg) && e->iEnd >= r->iEnd)
						goto skip;
					r = r->prev;
				}
				while (r->mark == 9);
			}
			if (r->mark == 6)
			{
				if (r->code != 0 && !(pb > r->iEnd) && e->iEnd >= r->prev->iEnd)
					goto skip;
				r = r->prev;
			}
		}
		while (!(r->iBeg < pb) && r != head)
		{
			UByte m = r->mark;
			long rb = r->iBeg;
			if (m == 1)
			{
				if (r->iEnd < e->iBeg && rb > partner->iEnd)
				{
					upperOut = r;
					if (r->code == 3)
						turnsUp++;
					else
					{
						uppers++;
						if (band != 0 && (r->attr & 0x30) != band)
							goto skip;
						band = r->attr & 0x30;
					}
				}
				else if (r->code == 3)
					inTurnsUp++;
				else
					inUppers++;
			}
			else if (m == 3)
			{
				if (r->iEnd < e->iBeg && rb > partner->iEnd)
				{
					lowerOut = r;
					if (r->code == 7)
						turnsDown++;
					else
					{
						lowers++;
						if (band != 0 && (r->attr & 0x30) != band)
							goto skip;
						band = r->attr & 0x30;
					}
				}
				else
				{
					if (r->code == 7)
						inTurnsDown++;
					else
						inLowers++;
					lowerIn = r;
				}
			}
			else if (m == 0x10)
			{
				// (the mark of the element after it in the array)
				if ((r + 1)->mark != 9)
				{
					SPEC_TYPE* q = r;
					do
						q = q->prev;
					while (q->mark == 0xa || q->mark == 0x10);
					UByte c = q->code;
					if ((IsBreakCode(c) || c == 0xd || c == 0x10) && pb <= q->iBeg)
						goto skip;
					break;
				}
			}
			else if (m == 0x44)
				goto skip;
			r = r->prev;
			m = r->mark;
			if (m == 6 && r->code == 0)
				r = r->prev;
			else
			{
				if (m == 6 || m == 9)
				{
					if (!(pb > r->iBeg))
					{
						if (!(m == 9 && r->code == 3))
							goto skip;
						stick = 1;
					}
					r = r->prev;
				}
				if (r->mark == 6 && r->code == 0)
					r = r->prev;
			}
		}

		if (uppers == 0 && lowers == 0 && turnsUp == 0 && turnsDown == 0)
		{
			// nothing outside the loop
			if (stick != 0)
				goto skip;
			if (inUppers == 0 && inLowers == 0 && inTurnsUp == 0 && inTurnsDown == 0)
				goto skip;
			if (partner->other == 0x64)
				goto skip;
			FillCrossInfo(low, e, &ci);
			long dx = ci.dx;
			long dy = ci.dy;
			e->attr = (UByte) HeightInLine((short) ((ci.box.top + ci.box.bottom) >> 1), low);
			long small = low->fThresh[14];
			if ((small < dx && dx < 2 * dy) || small < dy)
			{
				long n = inUppers + inLowers;
				if (n == 0)
				{
					if (inTurnsUp == 1)
					{
						partner->code = e->code = 0x1d;
						e->attr = (UByte) HeightInLine(ci.box.top, low);
					}
					if (inTurnsDown == 1)
					{
						partner->code = e->code = 0x1e;
						e->attr = (UByte) HeightInLine(ci.box.bottom, low);
					}
				}
				else if (n <= 3)
				{
					if (lowerIn != nil && IsDUR(e, nil, lowerIn, low))
						goto skip;
					SPEC_TYPE* t = e->prev;
					while (t != nil && !(t->code == 2 || t->code == 8))
						t = t->prev;
					if (t != nil && (n == 2 || dy > dx || (dy >= 0x3c && dx >= 0x3c)))
					{
						partner->code = e->code = 5;
						e->attr = (e->attr & ~0x30) | (t->attr & 0x30);
						goto skip;
					}
				}
			}
			if (e->code == 0)
				CheckSmallGamma(&ci);
			goto skip;
		}

		if (stick != 0 && (band == 0 || band == 0x10))
			goto skip;
		{
			Boolean lowOne = (uppers == 1 && lowers == 1 && (upperOut->attr & 0xf) <= 5);
			if (turnsUp + uppers == 1 && !lowOne && is_DDL(e, upperOut, low))
				goto coded;
			SPEC_TYPE* candidate = nil;
			Boolean tryIt = false;
			if ((turnsDown == 1 && inTurnsDown == 0) || (lowers == 1 && inLowers == 0))
			{
				candidate = lowerOut;
				tryIt = true;
			}
			else if ((turnsDown == 0 && inTurnsDown == 1) || (lowers == 0 && inLowers == 1))
			{
				candidate = lowerIn;
				tryIt = true;
			}
			if (tryIt && candidate != nil && (candidate->attr & 0xf) <= 9
			 && IsDUR(e, upperOut, candidate, low))
				goto coded;
		}
		{
			long ul = uppers + lowers;
			long tt = turnsUp + turnsDown;
			if (!((ul == 1 && tt == 0) || (ul == 0 && tt == 1)))
			{
				if (!(uppers == 1 && lowers == 1 && tt == 0))
					goto skip;
				partner->code = e->code = 5;
				FillCrossInfo(low, e, &ci);
				e->attr = (HeightInLine((short) ((ci.box.top + ci.box.bottom) >> 1), low) & 0xf) | (band & 0x30);
				goto shape;
			}
		}
		if (lowers == 1 || (turnsDown == 1 && stick == 0))
			e->code = 6;
		else if (stick != 0)
			goto skip;
		if (uppers == 1 || turnsUp == 1)
			e->code = 4;
		{
			long left, right, width;
			long closed;
			long a = e->iEnd;
			if (a - e->iBeg != 1)
				a = (e->iBeg + a) >> 1;
			long b = partner->iBeg;
			if (partner->iEnd - b != 1)
				b = (b + partner->iEnd) >> 1;
			FillCrossInfo(low, e, &ci);
			ci.lower = lowerIn;
			long c = cos_vect(e->iBeg, a, b, partner->iEnd, x, y);
			ci.cosine = c;
			SPEC_TYPE* ext = (e->code == 4) ? upperOut : lowerOut;
			e->ipoint0 = ext->ipoint0;
			long m = Mid(ext);
			e->attr = (UByte) HeightInLine(y[m], low);
			if (stick != 0 && (e->attr & 0xf) > 9)
				goto uncode;
			width = GetMaxDxInGamma(Mid(partner), Mid(e), m, x, y, e->code, &left, &right);
			ci.maxDx = width;
			partner->ipoint0 = (short) ((left - (UShort) partner->iBeg) + (((UShort) e->iEnd - right) << 8));
			partner->ipoint1 = (short) width;
			if (Isgammathin(&ci, ext))
			{
				if (stick == 0)
					goto skip;
				goto uncode;
			}
			if (e->code == 4)
			{
				if (upperOut->code != 3)
					goto open;
				e->other |= 8;
				upperOut->code = 2;
				band = (x[right] <= x[left]) ? 0x20 : 0x10;
				upperOut->attr = (upperOut->attr & ~0x30) | (band & 0x30);
			}
			else if (e->code == 6)
			{
				if (lowerOut->code != 7)
					goto bottomLoop;
				e->other |= 8;
				lowerOut->code = 8;
				band = (x[right] <= x[left]) ? 0x10 : 0x20;
				lowerOut->attr = (lowerOut->attr & ~0x30) | (band & 0x30);
			}
			if (e->code != 6)
				goto open;
		bottomLoop:
			if ((e->attr & 0xf) >= 0xc)
				goto open;
			if (band == 0x10)
				closed = (c > -1);
			else if (band == 0x20)
			{
				if ((e->attr & 0xf) > 7 && (e->other & 8) == 0)
					closed = 1;
				else
					closed = (width > 0x1e);
			}
			else
				closed = 0;
			goto decided;
		open:
			closed = 0;
		decided:
			{
				long o = 0;
				if (e->code == 4)
				{
					UByte ea = ext->attr;
					if ((ea & 0x30) == 0x20 && (ea & 0xf) < 8 && (ea & 0xf) > 2
					 && (e->other & 8) == 0 && width > 0x15)
					{
						Decision_GU_or_O_(&ci);
						o = (e->code == 5);
					}
				}
				else
					o = (e->code == 5);
				if (o)
					e->other |= 1;
				if (closed || o)
				{
					e->code = 5;
					e->attr = (HeightInLine((short) ((ci.box.top + ci.box.bottom) >> 1), low) & 0xf) | (band & 0x30);
				}
			}
			goto shape;
		uncode:
			partner->code = 0;
			e->code = 0;
			goto skip;
		}
	shape:
		{
			// a flat loop across the middle, or a small one at the top or
			// the bottom
			long small = low->fThresh[14];
			long dx = ci.dx;
			long dy = ci.dy;
			if (small < dx && dx >= 2 * dy && small > dy)
			{
				long m = (e->iBeg + partner->iEnd) / 2;
				e->code = (x[m] >= x[e->iEnd]) ? 0x20 : 0x1f;
				goto coded;
			}
			if (small > dx && small > dy)
			{
				short at;
				if (e->code == 4)
				{
					e->code = 0x1d;
					at = ci.box.top;
				}
				else if (e->code == 6)
				{
					e->code = 0x1e;
					at = ci.box.bottom;
				}
				else
					goto coded;
				e->attr = (HeightInLine(at, low) & 0xf) | (band & 0x30);
				e->other |= 4;
				goto coded;
			}
			short at;
			if (e->code == 4)
				at = ci.box.top;
			else if (e->code == 6)
				at = ci.box.bottom;
			else
				goto coded;
			e->attr = (HeightInLine(at, low) & 0xf) | (band & 0x30);
		}
	coded:
		partner->code = e->code;
	skip:
		e = e->next;
	}
	return 0;
}


// ROM 0x002ca074 lk_cross__FP8low_type
// See the file's comment.  ==> 0.
long
lk_cross(low_type* low)
{
	analize_sticks(low);
	analize_circles(low);
	del_inside_circles(low);
	return 0;
}
