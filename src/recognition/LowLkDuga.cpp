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

	lk_duga runs them in this order: prevent_arcs (only when rc +0x92 is
	2), arcs_processing, conv_sticks_to_arcs (a level stick at a stroke's
	start or end made an arc), del_before_after_circles (the elements
	either side of each loop recoded or taken out - see the comment over
	them), delete_CROSS_elements, check_IUb_IDf_small and
	delete_UD_before_DDL.

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


// ROM 0x002fd3a8 prevent_arcs__FP8low_type
// (With rc +0x92 = 2.)  A narrow stroke end (3/7, mark 0x10 or 0x20), a
// top 2 marked 1 or a bottom 8 marked 3 - under 20 across - is kept a
// stick: other set to 1 (nothing else in it survives), a 2 made 3 and an
// 8 made 7, so arcs_processing will not fold it into an arc.
void
prevent_arcs(low_type* low)
{
	short* x = low->fX;
	for (SPEC_TYPE* e = low->fSpecl; e != nil; e = e->next)
	{
		UByte mark = e->mark;
		UByte code = e->code;
		if (!((mark == 0x10 || mark == 0x20) && (code == 3 || code == 7)))
		{
			if (code == 2)
			{
				if (mark != 1)
					continue;
			}
			else if (!(code == 8 && mark == 3))
				continue;
		}
		if (HWRAbs(x[e->iBeg] - x[e->iEnd]) >= 0x14)
			continue;
		e->other = 1;
		if (e->code == 2)
			e->code = 3;
		else if (e->code == 8)
			e->code = 7;
	}
}


// The point of the element beside a stick that CurvMeasure bends it
// towards: its own point, or its middle when it has none (0 counts as
// none).
static inline long
PointOrMid(SPEC_TYPE* n)
{
	long i = n->ipoint0;
	if (i == 0 || i == -2)
		i = (n->iBeg + n->iEnd) >> 1;
	return i;
}


// ROM 0x002fcbb8 conv_sticks_to_arcs__FP8low_type
// A stroke's start or end that is a stick (3 or 7, other neither 0x10 nor
// 1) made an arc when it is at least five points long, runs level (a
// cosine of at least 0.85 with the horizontal) and is either longer than
// nine points or bent (by more than 20 hundredths towards the element
// beside it): the extremum across it (ixMin or ixMax, by which way it
// runs) becomes its point and the end it starts or finishes at, its code
// 9..0xc and its band by the way it curls.  A start that is a top going
// right by 15 or more is made a 0xa straight away.  ==> 0.
//
// ROM QUIRK: each test is also skipped when |dx| is at most 12 under a
// flag (r8) that is nought and never set - a switched-off option.
long
conv_sticks_to_arcs(low_type* low)
{
	short* x = low->fX;
	short* y = low->fY;
	for (SPEC_TYPE* e = low->fSpecl; e != nil; e = e->next)
	{
		UByte mark = e->mark;
		UByte other = e->other;
		if ((mark != 0x10 && mark != 0x20) || (other & 0x10) || (other & 1))
			continue;
		UByte code = e->code;
		if (code != 3 && code != 7)
			continue;
		long iBeg = e->iBeg;
		long iEnd = e->iEnd;
		long len = iEnd - iBeg;
		long i;
		long dx;
		if (mark == 0x10)
		{
			i = (other & 2) ? (iBeg + iEnd) >> 1 : iBeg;
			dx = x[i] - x[iEnd];
			if (code == 3 && dx >= 0)
			{
				// a start going right: an arc over the top if far enough
				if (dx < 0xf)
					continue;
				long r = ixMax(iBeg, iEnd, x, y);
				if (r != -1)
					e->ipoint0 = e->iBeg = (short) r;
				e->code = 0xa;
				e->attr = (MidPointHeight(e, low) & 0xf) | 0x20;
				continue;
			}
		}
		else
		{
			i = (other & 2) ? (iBeg + iEnd) >> 1 : iEnd;
			dx = x[iBeg] - x[i];
		}
		if (len < 5)
			continue;
		Boolean bent = len > 9;
		if (!bent)
		{
			long c = (mark == 0x10) ? CurvMeasure(x, y, i, PointOrMid(e->next), -1)
									: CurvMeasure(x, y, PointOrMid(e->prev), i, -1);
			if (HWRAbs(c) > 0x14)
				bent = true;
		}
		if (!bent)
			continue;
		if (HWRAbs((short) cos_horizline(iBeg, iEnd, x, y)) < 0x55)
			continue;
		long r;
		UByte arc;
		UByte band;
		if (mark == 0x10)
		{
			if (code == 7)
			{
				if (dx >= 0)
				{
					r = ixMax(iBeg, iEnd, x, y);
					arc = 0xc;
					band = 0x10;
				}
				else
				{
					r = ixMin(iBeg, iEnd, x, y);
					arc = 0xb;
					band = 0x20;
				}
			}
			else
			{
				r = ixMin(iBeg, iEnd, x, y);
				arc = 9;
				band = 0x10;
			}
			if (r != -1)
				e->ipoint0 = e->iBeg = (short) r;
		}
		else
		{
			if (code == 7)
			{
				if (dx >= 0)
				{
					r = ixMin(iBeg, iEnd, x, y);
					arc = 0xb;
					band = 0x10;
				}
				else
				{
					r = ixMax(iBeg, iEnd, x, y);
					arc = 0xc;
					band = 0x20;
				}
			}
			else
			{
				if (dx >= 0)
				{
					r = ixMin(iBeg, iEnd, x, y);
					arc = 9;
					band = 0x20;
				}
				else
				{
					r = ixMax(iBeg, iEnd, x, y);
					arc = 0xa;
					band = 0x10;
				}
			}
			if (r != -1)
				e->ipoint0 = e->iEnd = (short) r;
		}
		e->code = arc;
		e->attr = (MidPointHeight(e, low) & 0xf) | band;
	}
	return 0;
}


// ROM 0x002fc70c delete_UD_before_DDL__FP8low_type
// A high bottom (8, band 0x10) just before a 0x1c taken out.  ==> 0.
long
delete_UD_before_DDL(low_type* low)
{
	SPEC_TYPE* e = low->fSpecl;
	if (e->next == nil)
		return 0;
	do
	{
		if (e->code == 8 && (e->attr & 0x30) == 0x10 && e->next->code == 0x1c)
			DelFromSPECLList(e);
		e = e->next;
	}
	while (e->next != nil);
	return 0;
}


/*------------------------------------------------------------------------------
	The elements either side of a loop.

	del_before_after_circles walks the loops - an element marked 6 (a
	crossing's first half) whose code is not a stick (3, 7) or a small loop
	(0x16, 0x17) - and hands the passes below a NxtPrvCircle_type: the
	loop, the elements before and after it (skipping the ones marked other
	0x40 that sit inside it), its height band and its band.  Each pass may
	move the before or after element along, recode it, or take it out; the
	loop's height band is kept in the caller's byte through pHeight.
------------------------------------------------------------------------------*/

// ROM 0x002fc764 make_CDL_in_O_GU_f__FP9SPEC_TYPET1Uc
// After a loop high up whose element before is a 0x21, the element after
// made a small top loop (0x1b) when it is an arc 0xb, or a stroke end 7
// marked 0x20 that overlaps the loop.
void
make_CDL_in_O_GU_f(SPEC_TYPE* e, SPEC_TYPE* n, UByte band)
{
	if (!(e->prev->code == 0x21 && band == 0x10))
		return;
	if (n->code != 0xb)
	{
		if (!(n->code == 7 && n->mark == 0x20))
			return;
		if (!(e->iEnd >= n->iBeg && n->iEnd >= e->iBeg))
			return;
	}
	n->code = 0x1b;
}


// ROM 0x002fb3e0 del_prv_and_shift__FP9SPEC_TYPE
// e taken out.  ==> the element before it.
SPEC_TYPE*
del_prv_and_shift(SPEC_TYPE* e)
{
	DelFromSPECLList(e);
	return e->prev;
}


// ROM 0x002fd7ac check_inside_circle__FP9SPEC_TYPET1P8low_type
// Whether t lies inside loop e: for a closed loop (5) whether t's middle
// point is inside or on the trace of the loop (from the middle of the
// loop's partner - the element after it in the array - to the middle of
// its ipoint1 and end); otherwise whether t's box is within e's across and
// down, e reaching below it.
long
check_inside_circle(SPEC_TYPE* e, SPEC_TYPE* t, low_type* low)
{
	short* x = low->fX;
	short* y = low->fY;
	if (e == nil || t == nil)
		return 0;
	if (e->code != 5)
	{
		_RECT eb, tb;
		GetTraceBox(x, y, e->iBeg, e->iEnd, &eb);
		GetTraceBox(x, y, t->iBeg, t->iEnd, &tb);
		if (eb.left > tb.left || eb.right < tb.right || eb.top > tb.top || eb.bottom <= tb.bottom)
			return 0;
		return 1;
	}
	SPEC_TYPE* partner = e + 1;
	long a = (partner->iBeg + partner->iEnd) >> 1;
	long n = ((e->ipoint1 + e->iEnd) >> 1) - a + 1;
	long m = (t->iBeg + t->iEnd) >> 1;
	short where;
	if (IsPointInsideArea(&x[a], &y[a], n, x[m], y[m], &where) == 1)
		return 0;
	return (where == 0 || where == 1) ? 1 : 0;
}


// ROM 0x002fb148 change_circle_before__FP17NxtPrvCircle_typeUc
// A loop's start moved back to the end of a loop-like element before it
// (h its height band: a top loop 4 low down, with the loop at the bottom;
// a bottom loop 6 high up) that finishes inside it, and its height band
// worked out again; a top loop before it reaching band 12 makes it a 6 at
// band 12.
long
change_circle_before(NxtPrvCircle_type* s, UByte h)
{
	SPEC_TYPE* e = s->e;
	low_type* low = s->low;
	SPEC_TYPE* p = *s->pPrev;
	UByte height = *s->pHeight;
	UByte band = s->band;
	short* y = low->fY;
	if (p->code == 4)
	{
		if (h > 5 || band != 0x20)
			goto done;
	}
	else if (p->code != 6 || h < 9)
		goto done;
	if (p->iEnd < e->iBeg || p->iEnd >= e->iEnd - 0x14)
		goto done;
	{
		short yMin, yMax;
		yMinMax(p->iEnd, e->iEnd, y, &yMin, &yMax);
		if (p->code == 4 && HeightInLine(yMax, low) >= 0xc)
		{
			e->iBeg = p->iEnd;
			e->code = 6;
			e->attr = (band & 0x30) | 0xc;
			height = 0xc;
			goto done;
		}
		if (h <= 3)
			e->iBeg = p->iEnd;
		height = (UByte) HeightInLine((short) ((yMin + yMax) >> 1), low);
		e->attr = (height & 0xf) | (band & 0x30);
	}
done:
	*s->pHeight = height;
	return 0;
}


// ROM 0x002fb70c Is_8__FPsT1P9SPEC_TYPET3
// Whether a loop 0x1d before b is not the top of an 8 standing on it: its
// middle half not overlapping b down.  A loop that is not, before a low
// b, becomes a high 0x21.  ==> 1 when it is not (or was made so); 0 when
// it is an 8's top.
long
Is_8(short* x, short* y, SPEC_TYPE* a, SPEC_TYPE* b)
{
	if (a->code != 0x1d)
		return 0;
	long q = (a->iEnd - a->iBeg + 2) >> 2;
	_RECT ab, bb;
	GetTraceBox(x, y, a->iBeg + q, a->iEnd - q, &ab);
	GetTraceBox(x, y, b->iBeg, b->iEnd, &bb);
	if (yHardOverlapRect(&ab, &bb, 0) != 0)
		return 0;
	if ((b->attr & 0x30) == 0x20)
	{
		a->code = 0x21;
		a->attr = (a->attr & ~0x30) | 0x10;
	}
	return 1;
}


// ROM 0x002fb3fc UpElemBeforeCircle__FP17NxtPrvCircle_typeUc
// Whether the element before a loop is an upward stroke that belongs to
// it (h is its height band): a top loop over it high enough, a closed
// loop overlapping it, a low loop, a crossing arc, a stick or an arc.
long
UpElemBeforeCircle(NxtPrvCircle_type* s, UByte h)
{
	SPEC_TYPE* p = *s->pPrev;
	SPEC_TYPE* e = s->e;
	UByte band = s->band;
	low_type* low = s->low;
	short* y = low->fY;
	short* x = low->fX;
	Boolean highLoop;
	if (e->code == 6 && (e->attr & 0x30) == 0x10)
		highLoop = true;
	else
	{
		SPEC_TYPE* n = p->next;
		highLoop = n->code == 0x22 && (n->attr & 0x30) == 0x10 && (n->attr & 0xf) >= 0xd;
	}
	UByte c = p->code;
	long stick = 0;
	if (c == 3 && !highLoop)
	{
		if (p->mark == 9 || p->mark == 6)
			stick = h > 4 ? 1 : 0;
		else
			stick = 1;
	}
	long arc9 = (c == 9 && !highLoop) ? 1 : 0;
	long loop = 0;
	long overlap = 0;
	if (c == 5)
	{
		if ((p->attr & 0x30) == band)
			overlap = 1;
		else
		{
			SPEC_TYPE* nn = e->next->next;
			if (nn != nil && !IsBreakCode(nn->code))
				overlap = 1;
		}
	}
	if (c == 9 && highLoop)
		p->other |= 4;
	short yMin, yMax;
	yMinMax(p->iEnd, e->iEnd, y, &yMin, &yMax);
	if (yMin != 0x7fff)
	{
		loop = 0;
		if (p->code == 4)
		{
			Boolean tryLow = true;
			if (h >= 5)
			{
				if (HeightInLine(yMin, low) > 8 || yMin - y[p->ipoint0] > 0x1e)
				{
					loop = 1;
					tryLow = false;
				}
			}
			if (tryLow && (h == 3 || h == 4))
			{
				long yp = y[p->ipoint0];
				if (yp - 0x2780 >= 2 && yMin - yp <= 0x14)
					loop = 1;
			}
		}
	}
	if (overlap)
	{
		_RECT pb, eb;
		GetTraceBox(x, y, p->iBeg, p->iEnd, &pb);
		GetTraceBox(x, y, e->iBeg, e->iEnd, &eb);
		overlap = HardOverlapRect(&pb, &eb, 0);
	}
	if (h >= 3 && h <= 7)
	{
		if (loop || overlap)
			return 1;
		UByte pc = p->code;
		if ((pc == 0x1d || pc == 0x1e || pc == 0x1f || pc == 0x20) && Is_8(x, y, p, e) == 0)
			return 1;
		if (p->code == 0x15 || arc9)
			return 1;
	}
	if (h > 9)
		return 0;
	if (p->code == 0xa || stick)
		return 1;
	return 0;
}


// ROM 0x002fb7f0 DnElemBeforeCircle__FP17NxtPrvCircle_typeUc
// Whether the element before a loop (h the loop's height band, 5..9) is a
// downward stroke into it: a stroke end 7 not coming down far from a top
// that starts a letter, or a bottom loop 6.  The loop is then started
// where that stroke starts.
long
DnElemBeforeCircle(NxtPrvCircle_type* s, UByte h)
{
	SPEC_TYPE* p = *s->pPrev;
	low_type* low = s->low;
	SPEC_TYPE* head = low->fSpecl;
	SPEC_TYPE* e = s->e;
	Boolean down = p->code == 7;
	if (down)
	{
		SPEC_TYPE* pp = p->prev;
		short* y = low->fY;
		short yMin, yMax;
		yMinMax(e->iBeg, e->iEnd, y, &yMin, &yMax);
		UByte c = pp->code;
		Boolean done = false;
		if ((c == 3 || c == 0xa || c == 9) && y[pp->ipoint0] < yMin && y[p->ipoint0] - y[pp->ipoint0] >= 0x28)
		{
			SPEC_TYPE* q = pp->prev;
			if (q == head || IsBreakCode(q->code) || q->code == 0xd || q->code == 0x10 || q->mark == 0xa)
			{
				down = false;
				done = true;
			}
		}
		if (!done && (c == 2 || (c == 3 && pp->mark == 1)))
		{
			if ((pp->attr & 0xf) <= 3 && h > 7)
				down = false;
		}
	}
	if (!(h >= 5 && h <= 9 && (down || p->code == 6)))
		return 0;
	if (p->iBeg < e->iBeg)
		e->iBeg = p->iBeg;
	return 1;
}


// ROM 0x002fb29c change_and_del_before_circle__FP17NxtPrvCircle_typeUc
// The element before a loop (the loop's height band 3..9) that overlaps it
// or lies inside it, and goes up into it (or down, with the loop at the
// bottom): a closed loop, or one followed by a 0x21, taken out; anything
// else made a small loop (0x16, or 0x17 when a high one that is not an
// arc 0xa follows).  A stick end 7 marked 9 inside the loop before that
// goes too.
long
change_and_del_before_circle(NxtPrvCircle_type* s, UByte h)
{
	SPEC_TYPE* e = s->e;
	SPEC_TYPE* p = *s->pPrev;
	UByte height = *s->pHeight;
	UByte band = s->band;
	low_type* low = s->low;
	if (height < 3 || height > 9)
		goto done;
	if (!(p->iEnd >= e->iBeg && e->iEnd >= p->iBeg) && check_inside_circle(e, p, low) == 0)
		goto done;
	if (UpElemBeforeCircle(s, h) == 0)
	{
		if (DnElemBeforeCircle(s, h) == 0 || band != 0x20)
			goto done;
	}
	{
		UByte c = p->code;
		SPEC_TYPE* n = nil;
		if (c == 5 || (n = p->next)->code == 0x21)
			p = del_prv_and_shift(p);
		else
		{
			if ((n->attr & 0x30) == 0x10 && c != 0xa)
				p->code = 0x17;
			else
				p->code = 0x16;
			p = p->prev;
		}
		if (p->code == 7 && p->mark == 9 && check_inside_circle(e, p, low) != 0)
			p = del_prv_and_shift(p);
	}
done:
	*s->pPrev = p;
	return 0;
}


// ROM 0x002fb108 check_before_circle__FP17NxtPrvCircle_type
long
check_before_circle(NxtPrvCircle_type* s)
{
	UByte h = (*s->pPrev)->attr & 0xf;
	change_circle_before(s, h);
	change_and_del_before_circle(s, h);
	return 0;
}


// ROM 0x002fbb48 check_next_for_circle__FP17NxtPrvCircle_type
// A closed loop just after a low loop, overlapping it and in the same
// band, merged into it (and the 0x22 before whichever of the two is the
// flatter taken out); then a low top loop after it that is more than four
// fifths its size both ways made a 0x21 ahead of it - or taken out when a
// 0x21 is before it already.
//
// ROM QUIRK: the second test compares the band of the element first found
// after the loop, not of the one it is now looking at; and it measures
// that element's box to its ipoint1, not its end.
long
check_next_for_circle(NxtPrvCircle_type* s)
{
	SPEC_TYPE* e = s->e;
	SPEC_TYPE* n = *s->pNext;
	UByte band = s->band;
	low_type* low = s->low;
	short* x = low->fX;
	short* y = low->fY;
	UByte nBand = n->attr & 0x30;
	if (n->code == 5)
	{
		if (nBand != band)
			goto done;
		if (!(e->iEnd >= n->iBeg && n->iEnd >= e->iBeg))
			goto done;
		DelFromSPECLList(n);
		if (n->prev != e)
		{
			short yMin, yMax;
			yMinMax(e->iBeg, e->iEnd, y, &yMin, &yMax);
			long he = yMax - yMin;
			yMinMax(n->iBeg, n->iEnd, y, &yMin, &yMax);
			long hn = yMax - yMin;
			if (hn < he && n->prev->code == 0x22)
				DelFromSPECLList(n->prev);
		}
		else if (s->n22 == 2 && e->prev->code == 0x22)
			DelFromSPECLList(e->prev);
		e->iEnd = n->iEnd;
		n = e->next;
	}
	if (n->code == 4 && (n->attr & 0x30) == 0x20 && nBand == band)
	{
		_RECT eb, nb;
		GetTraceBox(x, y, e->iBeg, e->iEnd, &eb);
		GetTraceBox(x, y, n->iBeg, n->ipoint1, &nb);
		if (eb.right < nb.right || eb.top > nb.top)
			goto done;
		long w = eb.right - eb.left;
		long hgt = eb.bottom - eb.top;
		// DEVIATION: a loop of no width or height is a divide by zero the
		// ROM's __rt_sdiv traps on; the host takes it as not the same size
		if (w == 0 || hgt == 0)
			goto done;
		if ((nb.right - nb.left) * 100 / w <= 0x50)
			goto done;
		if ((nb.bottom - nb.top) * 100 / hgt <= 0x50)
			goto done;
		if (e->prev->code == 0x21)
			DelFromSPECLList(n);
		else
		{
			n->code = 0x21;
			n->other |= 0x40;
			SwapThisAndNext(e);
		}
		n = e->next;
	}
done:
	*s->pNext = n;
	return 0;
}


// ROM 0x002fbfb4 change_circle_after__FP17NxtPrvCircle_typeUcT2
// A loop low in the line (height band at most 6) ended where a top loop
// after it (in the loop's band, its own height band nh at most 3) that
// it overlaps starts, and its height band worked out again.
long
change_circle_after(NxtPrvCircle_type* s, UByte nBand, UByte nh)
{
	SPEC_TYPE* e = s->e;
	SPEC_TYPE* n = *s->pNext;
	UByte height = *s->pHeight;
	UByte band = s->band;
	low_type* low = s->low;
	short* y = low->fY;
	if (n->code == 4 && nh <= 3 && height <= 6 && nBand == band
	 && e->iEnd >= n->iBeg && n->iEnd >= e->iBeg)
	{
		e->iEnd = n->iBeg;
		short yMin, yMax;
		yMinMax(e->iBeg, e->iEnd, y, &yMin, &yMax);
		height = (UByte) HeightInLine((short) ((yMin + yMax) >> 1), low);
		e->attr = (height & 0xf) | (band & 0x30);
	}
	*s->pHeight = height;
	return 0;
}


// ROM 0x002fbdec check_next_for_common__FP17NxtPrvCircle_type
// A loop after the loop, ending above the middle of the line (height band
// under 8) and overlapping it, whose top is level with the loop's (within
// const1[14] + 3): the second half of one letter - taken out after a 0x21,
// otherwise made a small loop 0x17.  Anything else is change_circle_after's.
long
check_next_for_common(NxtPrvCircle_type* s)
{
	SPEC_TYPE* e = s->e;
	SPEC_TYPE* n = *s->pNext;
	UByte band = s->band;
	low_type* low = s->low;
	short* y = low->fY;
	UByte nBand = n->attr & 0x30;
	UByte nh = n->attr & 0xf;
	short nMin, nMax, eMin, eMax;
	yMinMax(n->iBeg, n->iEnd, y, &nMin, &nMax);
	yMinMax(e->iBeg, e->iEnd, y, &eMin, &eMax);
	short hEnd = (short) HeightInLine(y[n->iEnd], low);
	UByte c = n->code;
	Boolean common = false;
	if ((c == 0x1d || c == 0x1e || c == 0x1f || c == 0x20) && hEnd < 8 && nh >= 4)
		common = true;
	else if (c == 4 && hEnd < 8 && nBand == band)
	{
		if (nh >= 5)
			common = true;
		else if ((nh == 3 || nh == 4) && nMin - 0x2780 >= 2)
			common = true;
	}
	if (common && e->iEnd >= n->iBeg && n->iEnd >= e->iBeg
	 && HWRAbs(nMin - eMin) <= const1[14] + 3)
	{
		if (e->prev->code == 0x21)
			DelFromSPECLList(n);
		else
			n->code = 0x17;
		n = n->next;
	}
	else
		change_circle_after(s, nBand, nh);
	*s->pNext = n;
	return 0;
}


// ROM 0x002fc0a4 check_next_for_special__FP17NxtPrvCircle_type
// The element after a loop at the bottom (band 0x20, height band 5 up):
// an arc or 0x1c overlapping it folded into the 0x21 before it; a low top
// 2 overlapping it, or a stick 3 just after it before a low stroke, made a
// small loop 0x17.  Then, after a 0x22 loop, a stick or arc ending the
// stroke is either made a small loop (0x17, or 0x16 for a high loop) or
// marked other 4; and make_CDL_in_O_GU_f.
//
// ROM QUIRK: for a high loop it reads the byte after the loop's element in
// the array (its partner's other) as though it were a field of the loop.
long
check_next_for_special(NxtPrvCircle_type* s)
{
	SPEC_TYPE* e = s->e;
	SPEC_TYPE* n = *s->pNext;
	UByte height = *s->pHeight;
	UByte band = s->band;
	low_type* low = s->low;
	short* x = low->fX;
	UByte nh = n->attr & 0xf;
	if (height >= 5 && band == 0x20)
	{
		Boolean overlap = e->iEnd >= n->iBeg && n->iEnd >= e->iBeg;
		Boolean small = false;
		if (overlap && (n->code == 0xb || n->code == 0x1c))
		{
			SPEC_TYPE* p = e->prev;
			if (p->code == 2)
				p->code = 0x21;
			else if (!(p->code == 0x17 || p->code == 0x21))
				small = true;
			if (!small)
			{
				e->prev->iEnd = n->iEnd;
				DelFromSPECLList(n);
				n = n->next;
				goto special;
			}
		}
		else if (overlap && n->code == 2)
		{
			if ((n->attr & 0x30) != 0x20)
				goto special;
			SPEC_TYPE* nn = n->next;
			if (nn == nil || nn->code == 0xc)
				goto special;
			if (nn->code == 8 && (nn->attr & 0xf) > nh)
				goto special;
			small = true;
		}
		else if (n->code == 3 && e->iEnd >= n->iBeg - 5 && nh >= 5)
		{
			SPEC_TYPE* nn = n->next;
			if (nn == nil || !(nn->code == 7 || nn->code == 8))
				goto special;
			if (MidPointHeight(nn, low) >= 9)
				goto special;
			small = true;
		}
		if (small)
		{
			n->code = 0x17;
			n = n->next;
		}
	}
special:
	if (n == nil)
		goto done;
	if ((n->code == 3 || n->code == 9) && (e->attr & 0x30) == 0x20 && e->prev->code == 0x22
	 && (n->next == nil || IsBreakCode(n->next->code)))
	{
		if (!(e->other & 8) && !(e->other & 2))
		{
			short yMin, yMax;
			yMinMax(e->iBeg, e->iEnd, low->fY, &yMin, &yMax);
			if (HeightInLine(yMin, low) - nh < 2)
				n->code = 0x17;
		}
		else
			n->other |= 4;
	}
	if ((n->code == 3 || n->code == 0xa || n->code == 9) && (e->attr & 0x30) == 0x10 && e->prev->code == 0x22
	 && (n->next == nil || IsBreakCode(n->next->code)))
	{
		SPEC_TYPE* p = e->prev;
		long hn = n->attr & 0xf;
		if ((e + 1)->other == 0x64)
			n->code = 0x16;
		else if (hn < 6 && !(e->other & 8))
		{
			if (e->iEnd >= n->iBeg && n->iEnd >= e->iBeg)
				n->code = 0x16;
			else if (((n->iBeg + n->iEnd) >> 1) - e->iEnd >= (e->iEnd - ((p->iBeg + p->iEnd) >> 1) + 2) >> 2)
				n->other |= 4;
			else
				n->code = 0x16;
		}
		else if (hn >= 6 && hn <= 7 && x[n->iEnd] > x[p->iEnd])
			n->other |= 4;
	}
	make_CDL_in_O_GU_f(e, n, band);
done:
	*s->pNext = n;
	return 0;
}


// ROM 0x002fbaec check_after_circle__FP17NxtPrvCircle_type
long
check_after_circle(NxtPrvCircle_type* s)
{
	if (*s->pHeight >= 3 && s->band == 0x20 && !(s->e->other & 8))
	{
		check_next_for_circle(s);
		check_next_for_common(s);
	}
	check_next_for_special(s);
	return 0;
}


// ROM 0x002fc414 check_before_after_GU__FP17NxtPrvCircle_type
// A top loop high in the line (band 0x10, height band 7..9) after an upper
// element or a closed loop made a 0x1f at height band 9 (the high 0x21
// before it taken out); otherwise a bottom loop after it overlapping it,
// high and at band 8..9, made a 0x1f with everything between them taken
// out; and make_CDL_in_O_GU_f.
long
check_before_after_GU(NxtPrvCircle_type* s)
{
	SPEC_TYPE* e = s->e;
	SPEC_TYPE* n = *s->pNext;
	SPEC_TYPE* p = *s->pPrev;
	UByte height = *s->pHeight;
	UByte band = s->band;
	UByte nBand = n->attr & 0x30;
	UByte nh = n->attr & 0xf;
	if (band == 0x10)
	{
		if (height >= 7 && height <= 9 && (IsUpperElem(p) || p->code == 5))
		{
			SPEC_TYPE* q = e->prev;
			if (q->code == 0x21 && (q->attr & 0x30) == 0x10)
				DelFromSPECLList(q);
			e->code = 0x1f;
			e->attr = 9;
			return 0;
		}
		if ((n->code == 5 || n->code == 6) && (nh == 8 || nh == 9) && nBand == 0x10
		 && e->iEnd >= n->iBeg && n->iEnd >= e->iBeg)
		{
			for (SPEC_TYPE* q = n->prev; q != e; q = q->prev)
				DelFromSPECLList(q);
			n->code = 0x1f;
		}
	}
	make_CDL_in_O_GU_f(e, n, band);
	return 0;
}


// ROM 0x002fc538 O_GU_To3Elements__FP17NxtPrvCircle_type
// A loop at the bottom (band 0x20) - a top loop 4, or any with a single
// 0x21/0x22 inside it - with a stick or arc starting a stroke overlapping
// it before, and one ending a stroke at its end after (or nothing after):
// the three made one letter - the one before a small top loop 0x1b, the
// loop a 0x21 (a closed loop taken out), the one after a 0x1a, with the
// bottoms 8 beside them taken out.  With nothing after, the element after
// the loop in the array is made the 0x1a, at the loop's end.  ==> 1 when
// it was done.
long
O_GU_To3Elements(NxtPrvCircle_type* s)
{
	SPEC_TYPE* e = s->e;
	SPEC_TYPE* n = *s->pNext;
	SPEC_TYPE* p = *s->pPrev;
	low_type* low = s->low;
	short* y = low->fY;
	if ((e->attr & 0x30) != 0x20)
		return 0;
	if (e->code != 4 && s->n21 + s->n22 != 1)
		return 0;
	Boolean before = false;
	if (p->iEnd >= e->iBeg && e->iEnd >= p->iBeg)
	{
		UByte c = p->code;
		if ((c == 3 || c == 7) && p->mark == 0x10)
			before = true;
		else if (c == 0xa || c == 9 || c == 0xc || c == 0xb)
			before = true;
	}
	Boolean after = false;
	if (n == nil)
		after = true;
	else if (e->iEnd == n->iEnd)
	{
		UByte c = n->code;
		if ((c == 3 || c == 7) && n->mark == 0x20)
			after = true;
		else if (c == 0xa || c == 9 || c == 0xc || c == 0xb)
			after = true;
	}
	if (!before || !after)
		return 0;
	p->code = 0x1b;
	if (p->next->code == 8)
		DelFromSPECLList(p->next);
	if (e->code == 5)
		DelFromSPECLList(e);
	else
		e->code = 0x21;
	if (n != nil)
	{
		n->code = 0x1a;
		if (e->code == 5 && n->prev->code == 8)
			DelFromSPECLList(n->prev);
		return 1;
	}
	SPEC_TYPE* t = e + 1;
	t->iEnd = e->iEnd;
	t->iBeg = e->iEnd;
	t->code = 0x1a;
	t->attr = (UByte) HeightInLine(y[t->iEnd], low);
	Insert2ndAfter1st(e, t);
	return 1;
}


// ROM 0x002fba20 IsTipBefore__FP17NxtPrvCircle_type
// Whether the element before a loop is only the tip of the stroke that
// starts it (the element before that a break, a start or nothing, and it
// not marked other 4): a stick end or arc coming down into it, or a stick
// 3 before a loop.
long
IsTipBefore(NxtPrvCircle_type* s)
{
	SPEC_TYPE* p = *s->pPrev;
	SPEC_TYPE* e = s->e;
	UByte height = *s->pHeight;
	UByte band = s->band;
	UByte c = p->code;
	Boolean down = false;
	if (c == 7 || c == 0xc)
		down = !(e->code == 4 && band == 0x10);
	Boolean tip = false;
	if (c == 3)
	{
		if (e->code == 6)
			tip = height < 9 || band != 0x10;
		else
			tip = e->code != 7;
	}
	if (!tip && !down && !(c == 0xa || c == 9 || c == 0xb))
		return 0;
	UByte pc = p->prev->code;
	if (IsBreakCode(pc) || pc == 0 || pc == 0xd || pc == 0x10)
		return (p->other & 4) ? 0 : 1;
	return 0;
}


// ROM 0x002fd474 del_before_after_circles__FP8low_type
// See the comment above.  After the passes, the element before the loop
// taken out when it is only the tip that starts it and lies in it (or
// inside it), and the one after when it is a stick or arc ending the
// stroke inside the loop's end.  ==> 0.
long
del_before_after_circles(low_type* low)
{
	NxtPrvCircle_type s;
	memset(&s, 0, sizeof(s));
	for (SPEC_TYPE* e = low->fSpecl; e != nil; e = e->next)
	{
		if (e->mark != 6)
			continue;
		UByte c = e->code;
		if (c == 3 || c == 7 || c == 0x16 || c == 0x17)
			continue;
		SPEC_TYPE* prev = e->prev;
		SPEC_TYPE* next = e->next;
		s.n21 = 0;
		s.n22 = 0;
		while (prev->other & 0x40)
		{
			if (prev->code == 0x21)
				s.n21++;
			else if (prev->code == 0x22)
				s.n22++;
			prev = prev->prev;
		}
		if ((e->code == 5 || e->code == 6) && (e->attr & 0x30) == 0x20)
		{
			SPEC_TYPE* q = prev->next;
			if (q->code == 3 || q->code == 2)
				prev = q;
		}
		while (next != nil && (next->other & 0x40) && next->iBeg < e->iEnd)
			next = next->next;
		UByte height = e->attr & 0xf;
		s.e = e;
		s.pNext = &next;
		s.pPrev = &prev;
		s.low = low;
		s.pHeight = &height;
		s.band = e->attr & 0x30;
		c = e->code;
		if (c < 0x1d)
		{
			if (c == 4)
			{
				if (O_GU_To3Elements(&s) != 0)
					continue;
				check_before_after_GU(&s);
			}
			else if (c == 5 || c == 6)
			{
				if ((e->other & 1) && O_GU_To3Elements(&s) != 0)
					continue;
				check_before_circle(&s);
				check_after_circle(&s);
			}
		}
		if (IsTipBefore(&s) != 0)
		{
			if (e->iBeg <= (prev->iBeg + prev->iEnd) >> 1 || check_inside_circle(e, prev, low) != 0)
				DelFromSPECLList(prev);
		}
		if (next == nil)
			continue;
		long mid = (next->iBeg + next->iEnd) >> 1;
		c = e->code;
		if (c == 4 || c == 0x1d || c == 0x1e || c == 0x1f || c == 0x20)
			mid = (next->iBeg + 3 * next->iEnd) / 4;
		UByte nc = next->code;
		if (!(nc == 3 || nc == 7 || nc == 0xa || nc == 9 || nc == 0xc || nc == 0xb))
			continue;
		if (next->other & 4)
			continue;
		if (!(next->next == nil || IsBreakCode(next->next->code)))
			continue;
		if (c == 4 || c == 6)
			continue;
		if (mid <= e->iEnd || check_inside_circle(e, next, low) != 0)
			DelFromSPECLList(next);
	}
	return 0;
}


// ROM 0x002fa2f8 lk_duga__FP8low_type
// The arc passes, in order.  ==> 0.
long
lk_duga(low_type* low)
{
	if (RCGetH(low->rc, 0x92) == 2)
		prevent_arcs(low);
	arcs_processing(low);
	conv_sticks_to_arcs(low);
	del_before_after_circles(low);
	delete_CROSS_elements(low);
	check_IUb_IDf_small(low);
	delete_UD_before_DDL(low);
	return 0;
}
