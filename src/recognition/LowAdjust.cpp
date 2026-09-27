/*
	File:		LowAdjust.cpp

	Contains:	The cursive reader's low level: Adjust_I_U, which decides
				whether the bottom between two tops is an i's sharp turn or a
				u's round one.  See LowLevel.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	process_curves coded a bottom by the cosine at its turn alone.  For a
	narrow bottom between two tops (at most 20 across, and at most a
	quarter of the distance between the tops when over 15), whose height
	band differs from both tops' and with no pen-up on at least one side,
	Adjust_I_U looks again at how the trace bends going into it, coming out
	of it, and across it (CurvMeasure over three chords): bends that agree
	make it round (8, a u), bends that disagree - or a strong bend across it
	when the sides are straight - make it sharp (7, an i), unless it is much
	wider than it is deep.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "LowLevel.h"
#include "ParaGraph.h"


static inline Boolean
IsTopCode(UByte c)
{
	return c == 2 || c == 3 || c == 9 || c == 0xa;
}

static inline Boolean
IsBottomCode(UByte c)
{
	return c == 8 || c == 7 || c == 0xb || c == 0xc;
}


// ROM 0x00303038 Adjust_I_U__FP8low_type
// Each bottom between two tops recoded 8 (round) or 7 (sharp) - see the
// file's comment.  ROM QUIRK: the kind it keeps (r10 in the ROM) is only
// ever 0 or 2, so the two tests for a kind of 1 - a narrower width limit,
// and storing the verdict as the code itself - are never taken.
void
Adjust_I_U(low_type* low)
{
	short* x = low->fX;
	short* y = low->fY;
	for (SPEC_TYPE* e = low->fSpecl->next; e != nil; e = e->next)
	{
		SPEC_TYPE* n = e->next;
		if (n == nil)
			return;
		SPEC_TYPE* p = e->prev;
		UByte pc = p->code;
		if (pc == 0 || e->mark == 0x10 || e->mark == 0x20)
			continue;
		long pe = p->iEnd;
		long eb = e->iBeg;
		if (pe >= eb)
			continue;
		long nb = n->iBeg;
		long ee = e->iEnd;
		if (nb <= ee)
			continue;
		long kind = 0;
		UByte ec = e->code;
		if (IsTopCode(ec))
		{
			if (IsBottomCode(pc) && IsBottomCode(n->code))
				continue;
		}
		else if (IsBottomCode(ec))
		{
			if (IsTopCode(pc) && IsTopCode(n->code))
				kind = 2;
		}
		long w = (short) ((UShort) x[ee] - (UShort) x[eb]);
		if (w < 0)
			w = (short) -w;
		long d = (short) ((UShort) x[(nb + n->iEnd) >> 1] - (UShort) x[(pe + p->iBeg) >> 1]);
		if (d < 0)
			d = (short) -d;
		if (w > 0x14)
			continue;
		if (w > 0xf && w > (d + 2) >> 2)
			continue;
		if (kind == 0)
			continue;
		long band = e->attr & 0xf;
		if ((p->attr & 0xf) == band || (n->attr & 0xf) == band)
			continue;
		if (brk_right(y, pe, eb) < e->iBeg && brk_right(y, e->iEnd, n->iBeg) < n->iBeg)
			continue;
		long em = (e->iBeg + e->iEnd) >> 1;
		long pm = (p->iBeg + p->iEnd) >> 1;
		long nm = (n->iBeg + n->iEnd) >> 1;
		long i1 = (em + pm * 2 + 1) / 3;
		long i2 = (em + nm * 2 + 1) / 3;
		long c1 = CurvMeasure(x, y, pm, em, i1);
		long c2 = CurvMeasure(x, y, em, nm, i2);
		long c3 = CurvMeasure(x, y, p->iEnd, n->iBeg, em);
		Boolean b1 = HWRAbs(c1) >= 1;
		Boolean b2 = HWRAbs(c2) >= 1;
		long verdict = 0;
		Boolean measure = false;
		if (b1)
		{
			if (!b2)
			{
				if ((c1 >= 0) == (c3 >= 0))
					continue;
				verdict = 3;
				measure = true;
			}
			else
			{
				if ((c1 >= 0) != (c2 >= 0))
					continue;
				if ((c1 >= 0) == (c3 >= 0))
					verdict = 2;
				else
				{
					verdict = 3;
					measure = true;
				}
			}
		}
		else if (b2)
		{
			if ((c2 >= 0) == (c3 >= 0))
				continue;
			verdict = 3;
			measure = true;
		}
		else
		{
			if (HWRAbs(CurvMeasure(x, y, e->iBeg, e->iEnd, -1)) >= 0x11)
				verdict = 3;
			measure = verdict == 3;
		}
		if (measure)
		{
			long j1 = (p->iEnd + e->iBeg * 2 + 1) / 3;
			long j2 = (n->iBeg + e->iEnd * 2 + 1) / 3;
			if (e->iBeg < j1)
				j1 = e->iBeg;
			if (e->iEnd > j2)
				j2 = e->iEnd;
			long h = (short) ((UShort) y[em] - ((y[j1] + y[j2]) >> 1));
			if (h < 0)
				h = (short) -h;
			long wd = (short) ((UShort) x[j2] - (UShort) x[j1]);
			if (wd < 0)
				wd = (short) -wd;
			if ((kind == 1 && wd > h) || wd > h * 2)
				verdict = 0;
		}
		if (verdict == 0)
			continue;
		if (kind == 1)
			e->code = (UByte) verdict;
		else
			e->code = (verdict == 2) ? 8 : 7;
	}
}
