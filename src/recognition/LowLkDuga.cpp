/*
	File:		LowLkDuga.cpp

	Contains:	The cursive reader's low level: lk_duga's passes (duga is
				Russian for an arc) - an extremum that is really the tip
				of an arc folded into its neighbour, the loops that are
				not letters taken out, and a stick's height band set by
				which way it leans.  See LowLevel.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	arcs_processing: a top (2, 3) or bottom (7, 8) that is only the tip of
	a stroke's start or end beside it - close in height (DyLimit, over the
	nearest firm extremum of the other kind), narrower across
	(IsDx_Dy_in_arcs_OK, IsDx_Dy_in_tips_OK) - is folded into it: the
	stroke end takes the extremum's code (9..0xc, an arc) or the extremum
	takes the end's mark.  delete_CROSS_elements takes out the closed
	loops that are too short to be letters, keeping as a small loop (0x1b
	at the top, 0x17 at the bottom) one tall enough that has no loop or
	arc beside it (ins_third_elem_in_circle).  check_IUb_IDf_small sets a
	stick's (3 or 7) height band from its neighbours, or from which way it
	leans across the points about it.

	NOT YET: lk_duga itself and its other passes (prevent_arcs,
	conv_sticks_to_arcs, del_before_after_circles and the circle
	neighbours, delete_UD_before_DDL).

	All of it was read from the disassembly.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "LowLevel.h"
#include "ParaGraph.h"

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


// ROM 0x002fb96c IsDx_Dy_in_arcs_OK__FP9SPEC_TYPET1iPsT4
// Whether t is within lim of e's height and narrower across than e.
long
IsDx_Dy_in_arcs_OK(SPEC_TYPE* e, SPEC_TYPE* t, long lim, short* x, short* y)
{
	long dy = y[Mid(t)] - y[Mid(e)];
	long wt = x[t->iEnd] - x[t->iBeg];
	long we = x[e->iEnd] - x[e->iBeg];
	if (HWRAbs(dy) > lim)
		return 0;
	return HWRAbs(wt) < HWRAbs(we) ? 1 : 0;
}


// ROM 0x002fc7c8 IsDx_Dy_in_tips_OK__FP9SPEC_TYPET1iPsT4
// Whether t is close enough to e to be its tip: within lim down (half as
// much again when either is high up), within 15 across (22 when high up,
// ten more when they overlap or e is marked 2), and narrow (under 20).
long
IsDx_Dy_in_tips_OK(SPEC_TYPE* e, SPEC_TYPE* t, long lim, short* x, short* y)
{
	long tb = t->iBeg;
	long te = t->iEnd;
	long tm = (tb + te) >> 1;
	long eb = e->iBeg;
	long ee = e->iEnd;
	long em = (eb + ee) >> 1;
	long dy = y[tm] - y[em];
	long dx = x[tm] - x[em];
	long w = x[te] - x[tb];
	long across = 0xf;
	if (!((e->attr & 0xf) > 2 && (t->attr & 0xf) > 2))
	{
		lim += lim >> 1;
		across = 0x16;
	}
	if ((ee >= tb && te >= eb) || (e->other & 2))
		across += 0xa;
	if (HWRAbs(dy) < lim && HWRAbs(dx) < across && HWRAbs(w) < 0x14)
		return 1;
	return 0;
}


// ROM 0x002fc8d8 IsTipOK__FP9SPEC_TYPET1Ps
// Whether t may be folded into e as its tip: not when e is low and t
// wide (20 or more across), nor when t was marked 0x10 and e is high.
long
IsTipOK(SPEC_TYPE* e, SPEC_TYPE* t, short* x)
{
	UByte band = e->attr & 0x30;
	Boolean wide = (band == 0x20) && (x[t->iEnd] - x[t->iBeg] >= 0x14);
	Boolean marked = (t->other & 0x10) && band == 0x10;
	return (wide || marked) ? 0 : 1;
}


// ROM 0x002fc954 DyLimit__FP8low_typeP9SPEC_TYPEN32i
// How far down a tip may be from e: a quarter of the height to the
// nearest extremum of the other kind (back from b, or on from c), at
// least k; -1 when e is a low top whose nearby bottom is only just below
// it and the tip a is close on the far side (it is then no tip).
long
DyLimit(low_type* low, SPEC_TYPE* e, SPEC_TYPE* a, SPEC_TYPE* b, SPEC_TYPE* c, long k)
{
	long limit = k;
	short* y = low->fY;
	long ye = y[e->ipoint0];
	long ya = y[a->ipoint0];
	long band = e->attr & 0xf;
	Boolean top = (e->code == 2 || e->code == 3);
	SPEC_TYPE* r;
	if (b != nil)
	{
		for (r = b; ; r = r->prev)
		{
			if (r == nil)
				return limit;
			if (IsBreakCode(r->code))
				break;
			if (top ? IsLowerElem(r) : IsUpperElem(r))
				break;
		}
	}
	else
	{
		if (c == nil)
			return limit;
		for (r = c; ; r = r->next)
		{
			if (r == nil)
				return limit;
			if (IsBreakCode(r->code))
				break;
			if (top ? IsLowerElem(r) : IsUpperElem(r))
				break;
		}
	}
	if (IsBreakCode(r->code))
		return limit;
	long m = Mid(r);
	long yr = y[m];
	long rb = r->attr & 0xf;
	if (HWRAbs(band - rb) > 3 || rb >= 0xc || rb <= 2)
	{
		limit = (HWRAbs(ye - yr) + 2) >> 2;
		if (limit < k)
			limit = k;
		return limit;
	}
	if ((e->code == 2 || e->code == 3) && (e->attr & 0x30) == 0x10
	 && (r->code == 8 || r->code == 0x22) && (r->attr & 0x30) == 0x20 && a->mark == 0x20
	 && band >= 7 && band <= 9
	 && yr - ye <= 2 * (ya - ye))
	{
		long da = a->ipoint0 - e->ipoint0;
		if (e->ipoint0 - m <= 2 * da)
			limit = -1;
	}
	return limit;
}


// The two ways a stroke end beside an extremum is folded into it.
static void
FoldEndIntoExtremum(SPEC_TYPE* e, SPEC_TYPE* end, Boolean before)
{
	DelFromSPECLList(end);
	e->mark = end->mark;
	if (e->ipoint0 != -2)
	{
		if (before)
			e->iBeg = e->ipoint0;
		else
			e->iEnd = e->ipoint0;
	}
	e->other = (e->other | end->other | 0x10) & ~0x40;
}

static void
FoldExtremumIntoEnd(SPEC_TYPE* e, SPEC_TYPE* end, UByte code, Boolean before)
{
	DelFromSPECLList(e);
	end->attr = e->attr;
	end->code = code;
	if (before)
		end->iEnd = e->iEnd;
	else
		end->iBeg = e->iBeg;
	end->ipoint0 = e->ipoint0;
}


// ROM 0x002fa358 arcs_processing__FP8low_type
// See the file's comment.  ==> 0.
long
arcs_processing(low_type* low)
{
	const long k = 0x1b;
	short* y = low->fY;
	short* x = low->fX;
	for (SPEC_TYPE* e = low->fSpecl; e != nil; e = e->next)
	{
		SPEC_TYPE* next = e->next;
		SPEC_TYPE* prev = e->prev;
		UByte code = e->code;
		if (code == 0x15)
		{
			if (e->ipoint1 != -2)
				e->iBeg = e->ipoint1;
			continue;
		}
		if (code == 0x18 || code == 0x1c)
		{
			if (e->ipoint1 != -2)
				e->iEnd = e->ipoint1;
			continue;
		}
		if (code == 2 || code == 8)
		{
			// a top (2) or bottom (8) that is the tip of a stroke's end
			// of the other kind: the end becomes an arc
			UByte endCode = (code == 2) ? 7 : 3;
			long d;
			if (prev->mark == 0x10 && prev->code == endCode
			 && (d = DyLimit(low, e, prev, nil, next, k)) != -1
			 && IsDx_Dy_in_arcs_OK(e, prev, d, x, y)
			 && (code != 2 || IsTipOK(e, prev, x)))
			{
				UByte arc;
				if (code == 2)
					arc = ((e->attr & 0x30) == 0x10) ? 9 : 0xa;
				else
					arc = ((e->attr & 0x30) == 0x20) ? 0xb : 0xc;
				FoldExtremumIntoEnd(e, prev, arc, true);
				continue;
			}
			if (next != nil && next->mark == 0x20 && next->code == endCode
			 && (d = DyLimit(low, e, next, prev, nil, k)) != -1
			 && IsDx_Dy_in_arcs_OK(e, next, d, x, y))
			{
				UByte arc;
				if (code == 2)
					arc = ((e->attr & 0x30) == 0x20) ? 9 : 0xa;
				else
					arc = ((e->attr & 0x30) == 0x10) ? 0xb : 0xc;
				FoldExtremumIntoEnd(e, next, arc, false);
			}
			continue;
		}
		if (code == 3 || code == 7)
		{
			// a firm top (3) or bottom (7) beside a stroke's start or end
			// of its own kind: the end folded into it
			Boolean before = (prev->mark == 0x10 && prev->code == code);
			Boolean after = (next != nil && next->mark == 0x20 && next->code == code);
			long d;
			if (before)
			{
				Boolean fold = false;
				Boolean tryIt = true;
				if (after && e->mark == 0x10)
				{
					long dy = (code == 7) ? y[e->ipoint0] - y[prev->ipoint0] : y[prev->ipoint0] - y[e->ipoint0];
					if (dy < 0x28)
					{
						fold = true;
						tryIt = false;
					}
				}
				else if (e->mark == 0x20)
					tryIt = false;
				if (tryIt && (d = DyLimit(low, e, prev, nil, next, k)) != -1
				 && IsDx_Dy_in_tips_OK(e, prev, d, x, y))
					fold = true;
				if (fold)
				{
					FoldEndIntoExtremum(e, prev, true);
					continue;
				}
			}
			if (!after)
				continue;
			{
				Boolean fold = false;
				Boolean tryIt = true;
				if (before && e->mark == 0x20)
				{
					long dy = (code == 7) ? y[e->ipoint0] - y[next->ipoint0] : y[next->ipoint0] - y[e->ipoint0];
					if (dy < 0x28)
					{
						fold = true;
						tryIt = false;
					}
				}
				else if (e->mark == 0x10)
					tryIt = false;
				if (tryIt && (d = DyLimit(low, e, next, prev, nil, k)) != -1
				 && IsDx_Dy_in_tips_OK(e, next, d, x, y))
					fold = true;
				if (fold)
					FoldEndIntoExtremum(e, next, false);
			}
		}
	}
	return 0;
}


static inline Boolean
IsLoopOrArcCode(UByte c, const UByte* set, long n)
{
	for (long i = 0; i < n; i++)
		if (c == set[i])
			return true;
	return false;
}


// ROM 0x002fab84 ins_third_elem_in_circle__FP9SPEC_TYPEP8low_type
// A loop taller than 60 with no loop or arc beside it kept as a small one
// at its end: 0x1b for a top loop, 0x17 (its height band at most 7) for a
// closed or bottom one.  ==> whether it was.
long
ins_third_elem_in_circle(SPEC_TYPE* e, low_type* low)
{
	short* y = low->fY;
	SPEC_TYPE* next = e->next;
	SPEC_TYPE* prev = e->prev;
	short yMin, yMax;
	yMinMax(e->iBeg, e->iEnd, y, &yMin, &yMax);
	if (yMax - yMin <= 0x3c)
		return 0;
	if (e->code == 4)
	{
		static const UByte kBeside[] = { 0xb, 0x1c, 0x22, 0x1b, 0xe, 0x1e, 0x1f, 0x1d };
		if (next->code == 4)
			next = next->next;
		SPEC_TYPE* both[2] = { prev, next };
		for (long i = 0; i < 2; i++)
		{
			SPEC_TYPE* n = both[i];
			if (n == nil)
				continue;
			if (IsLoopOrArcCode(n->code, kBeside, 8))
				return 0;
			if (n->code == 8)
			{
				if ((n->attr & 0x30) == 0x10)
					return 0;
			}
			else if (n->code == 7 && n->mark == 0x20)
				return 0;
		}
		e->code = 0x1b;
		e->attr = (e->attr & ~0xf) | (HeightInLine(y[e->iEnd], low) & 0xf);
		e->iBeg = (short) ((UShort) e->iEnd - 1);
		e->ipoint0 = -2;
		return 1;
	}
	if (e->code == 5)
	{
		if (!((e->attr & 0x30) == 0x20 && (e->other & 1) == 0 && (e->other & 2) == 0))
			return 0;
	}
	else if (e->code != 6)
		return 0;
	static const UByte kNear[] = { 9, 0x18, 0x21, 0x17, 0xe, 0x1d, 3, 4, 0x1f };
	SPEC_TYPE* both[2] = { prev, next };
	for (long i = 0; i < 2; i++)
	{
		SPEC_TYPE* n = both[i];
		if (n == nil)
			continue;
		if ((n->attr & 0xf) <= 4)
			return 0;
		if (IsLoopOrArcCode(n->code, kNear, 9))
			return 0;
		if (n->code == 2 && (n->attr & 0x30) == 0x20)
			return 0;
	}
	long h = HeightInLine(y[e->iEnd], low);
	if (h == 8 || h == 9)
		h = 7;
	e->code = 0x17;
	e->attr = (e->attr & ~0xf) | (h & 0xf);
	e->iBeg = (short) ((UShort) e->iEnd - 1);
	e->ipoint0 = -2;
	if (prev->code == 3 || prev->code == 2)
		SwapThisAndNext(prev);
	return 1;
}


// ROM 0x002fab00 delete_CROSS_elements__FP8low_type
// The closed loops (5), the top loops low down and the bottom loops high
// up taken out, unless kept as a small loop.  ==> 0.
long
delete_CROSS_elements(low_type* low)
{
	for (SPEC_TYPE* e = low->fSpecl; e != nil; e = e->next)
	{
		UByte c = e->code;
		UByte band = e->attr & 0x30;
		if (c == 5 || (c == 4 && band == 0x10) || (c == 6 && band == 0x20))
		{
			if (ins_third_elem_in_circle(e, low) == 0)
				DelFromSPECLList(e);
		}
	}
	return 0;
}


static inline Boolean
IsLowSide(SPEC_TYPE* n)
{
	UByte c = n->code;
	return (c == 8 || c == 0x22 || c == 0xb || c == 0xc) && (n->attr & 0x30) == 0x20;
}

static inline Boolean
IsHighSide(SPEC_TYPE* n)
{
	UByte c = n->code;
	return (c == 2 || c == 0x21 || c == 9 || c == 0xa) && (n->attr & 0x30) == 0x10;
}


// ROM 0x002fae80 check_IUb_IDf_small__FP8low_type
// A low stick (3, band 0x20) or a high one (7, band 0x10) given the other
// band when its neighbours say so, otherwise when it leans back across
// the ten points either side of it (to the pen-ups).  ==> 0.
long
check_IUb_IDf_small(low_type* low)
{
	short* x = low->fX;
	short* y = low->fY;
	SPEC_TYPE* e = low->fSpecl->next;
	if (e->next == nil)
		return 0;
	for (;;)
	{
		SPEC_TYPE* p = e->prev;
		SPEC_TYPE* n = e->next;
		if (p->code == 0x17)
			p = p->prev;
		UByte c = e->code;
		UByte attr = e->attr;
		long flip = -1;					// the band to set, or none
		if (c == 3)
		{
			if ((attr & 0x30) != 0x20)
				goto next;
			if (e->mark == 9)
			{
				if (IsLowSide(p) || IsLowSide(n))
					flip = 0x10;
			}
			else if (e->mark == 1)
			{
				if (IsLowSide(p) && IsLowSide(n))
					flip = 0x10;
			}
		}
		else if (c == 7 && (attr & 0x30) == 0x10)
		{
			if (e->mark == 9 && (IsHighSide(p) || IsHighSide(n)))
				flip = 0x20;
		}
		else
			goto next;
		if (flip < 0)
		{
			long i = e->iBeg;
			for (short k = 0; y[i] != -1 && k < 10; k++)
				i = (short) (i - 1);
			long j = e->iEnd;
			for (short k = 0; y[j] != -1 && k < 10; k++)
				j = (short) (j + 1);
			if (y[i] == -1)
				i = (short) (i + 1);
			if (y[j] == -1)
				j = (short) (j - 1);
			if (x[i] >= x[j])
				goto next;
			flip = (c == 3) ? 0x10 : 0x20;
		}
		e->attr = (attr & ~0x30) | (UByte) flip;
	next:
		e = n;
		if (e->next == nil)
			return 0;
	}
}
