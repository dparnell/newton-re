/*
	File:		LowBegin.cpp

	Contains:	The cursive reader's low level: lk_begin, the first of the
				passes that turn the special elements into the codes the
				xrs are written from.  See LowLevel.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	Up to here an element's `mark` said what was found (1 a top, 3 a
	bottom, 5 an arc, 6/9/0xa a crossing, 7 a dash, 8 a dot, 0xb an angle,
	0x10/0x20 a stroke's start and end).  lk_begin gives the elements that
	will become xrs their `code` - the kind of xr - and `attr` its height
	band (HeightInLine, 1..13 between the line thresholds):

	  - init_proc_XT_ST_CROSS tidies the crossings: a loop's crossing
	    ('c', Circle) inside another is dropped, a dash's crossings are
	    moved to follow the dash, a short dash above the line becomes a
	    dot and a long flat dot a dash, and a stroke's last extremum is
	    moved to just before its end;
	  - process_ZZ folds each stroke's first and last extremum into its
	    start and end (code 3 a start or end at a top, 7 at a bottom, 0xd
	    a dash, 0x10 a dot, 0xf/0x27 an arc) and puts a break element
	    (0x44: code 0x12, or 0x14 when the pen-up carries a gap) between
	    strokes - or, when a stroke starts right where the last one ended
	    and both ends are the same kind, joins the two strokes into one;
	  - process_AN keeps an angle only when it is not already covered by
	    the extrema around it (code 0xe or 0x11 by which way it opens);
	  - process_curves codes the tops and bottoms left: 2/3 a top, 8/7 a
	    bottom by how sharp it is (the cosine at its turn), 0xf/0x27 an
	    arc;
	  - DefineWritingStep measures how wide the writing's strokes step
	    across (delta_interval: the runs between turns, the slant allowed
	    for), kept at low +0x70.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "LowLevel.h"
#include "ParaGraph.h"
#include "XrDomains.h"


static inline Boolean
IsCrossingMark(UByte m)
{
	return m == 6 || m == 0xa || m == 9;
}


// ROM 0x00306024 nobrk_left__FPsiT2
// Back from i to j over pen-ups: the first point that is not one.
long
nobrk_left(short* y, long i, long j)
{
	while (j <= i && y[i] == -1)
		i--;
	return i;
}


// ROM 0x0030604c nobrk_right__FPsiT2
long
nobrk_right(short* y, long i, long j)
{
	while (i <= j && y[i] == -1)
		i++;
	return i;
}


// ROM 0x00306f00 MidPointHeight__FP9SPEC_TYPEP8low_type
// The height band of the element's middle point.
long
MidPointHeight(SPEC_TYPE* elem, low_type* low)
{
	return HeightInLine(low->fY[(elem->iBeg + elem->iEnd) >> 1], low);
}


// ROM 0x002f9dd8 extremum__FUcsT2Ps
// The middle of the highest (kind 1) or lowest (kind 3) run of points from
// i to j; 0 for another kind.
short
extremum(UByte kind, short i, short j, short* y)
{
	long v = y[i];
	long best = i;
	long k;
	if (kind == 1)
	{
		for (k = i; k <= j; k = (short) (k + 1))
			if (y[k] < v)
			{
				best = k;
				v = y[k];
			}
	}
	else if (kind == 3)
	{
		for (k = i; k <= j; k = (short) (k + 1))
			if (v < y[k])
			{
				best = k;
				v = y[k];
			}
	}
	else
		return 0;
	long e = best;
	while (e <= j && y[e] == v)
		e = (short) (e + 1);
	return (short) ((best + e) >> 1);
}


// ROM 0x00308de4 delta_interval__FPsT1iN33PlN27Ui
// The steps across and down between the trace's turns from i0 to i1, as
// sums (*sx, *sy) and a count: each run from one turn (a change of
// direction in x or y) to the next that is more than three points long
// and neither k times wider than it is tall nor k times taller than
// wide, its width with the slant allowed for.  With trim, the widest and
// narrowest are left out of the sums when there are four or more.
// ==> whether any run was counted.
long
delta_interval(short* x, short* y, long i0, long i1, long k, long slope, long* sx, long* sy, long* count, ULong trim)
{
	*count = 0;
	*sy = 0;
	*sx = 0;
	long s = nobrk_right(y, i0, i1);
	if (s > i1)
		return 0;
	long e = nobrk_left(y, i1, s);
	if (e < s || e - s < 3)
		return 0;
	long minY = 0x7fff;
	long minX = 0x7fff;
	long maxY = -1;
	long maxX = -1;
	long start = s;
	long prev = s - 1;
	long waiting = 1;
	long lastDy = 0;
	long lastDx = 0;
	for (long i = s; i <= e; prev = i, i++)
	{
		long yi = y[i];
		if (yi == -1 || i == e)
		{
			if (waiting == 0 && prev > start)
			{
				long dy = (short) ((UShort) y[prev] - (UShort) y[start]);
				long dx0 = (short) ((UShort) x[prev] - (UShort) x[start]);
				long ax = (short) (SlopeShiftDx((short) dy, slope) + dx0);
				if (ax < 0)
					ax = (short) -ax;
				if (dy < 0)
					dy = (short) -dy;
				if (LMul(k, ax) > dy && LMul(k, dy) > ax && start + 3 < prev)
				{
					*sx += ax;
					*sy += dy;
					*count += 1;
					if (trim != 0)
					{
						if (ax < minX) minX = ax;
						if (ax > maxX) maxX = ax;
						if (dy < minY) minY = dy;
						if (dy > maxY) maxY = dy;
					}
				}
			}
			waiting = 1;
			continue;
		}
		if (waiting != 0)
		{
			long x0 = x[i];
			for (;;)
			{
				i++;
				if (i >= e)
					return *count != 0;
				if (y[i] == -1)
					break;
				if (x[i] == x0 || y[i] == yi)
					continue;
				lastDx = (short) ((UShort) x[i] - x0);
				lastDy = (short) (y[i] - yi);
				start = i;
				waiting = 0;
				break;
			}
			continue;
		}
		long ddx = (short) ((UShort) x[i] - x[prev]);
		long ddy = (short) (yi - y[prev]);
		if (ddx == 0 || ddy == 0)
			continue;
		if ((ddx >= 0) == (lastDx >= 0) && (ddy >= 0) == (lastDy >= 0))
			continue;
		lastDy = ddy;
		lastDx = ddx;
		long dy = (short) (y[prev] - (UShort) y[start]);
		long dx0 = (short) (x[prev] - (UShort) x[start]);
		long ax = (short) (SlopeShiftDx((short) dy, slope) + dx0);
		if (ax < 0)
			ax = (short) -ax;
		if (dy < 0)
			dy = (short) -dy;
		if (LMul(k, ax) > dy && LMul(k, dy) > ax && start + 3 < prev)
		{
			*sx += ax;
			*sy += dy;
			*count += 1;
			if (trim != 0)
			{
				if (ax < minX) minX = ax;
				if (ax > maxX) maxX = ax;
				if (dy < minY) minY = dy;
				if (dy > maxY) maxY = dy;
			}
		}
		if (start + 3 < prev)
			start = prev;
		if (y[i + 1] != -1)
			i++;
	}
	if (trim != 0 && *count >= 4)
	{
		*sx -= maxX + minX;
		*sy -= maxY + minY;
		*count -= 2;
	}
	return *count != 0;
}


// ROM 0x0030845c DefineWritingStep__FP8low_typePsUi
// How wide the writing steps across (*step: the mean width of the runs
// delta_interval finds, a third over five), the slant counted at half
// above 50.  ==> 0 measured, 2 measured from fewer than eight runs (with
// adjust, the step taken halfway to 40), 1 none (40 with adjust).
long
DefineWritingStep(low_type* low, short* step, ULong adjust)
{
	long result = 0;
	*step = 0;
	long slope = low->fSlope;
	if (slope < 0)
		slope = 0;
	else if (slope > 0x32)
		slope = ((slope - 0x32) >> 1) + 0x32;
	long sx;
	long sy;
	long count;
	if (delta_interval(low->fX, low->fY, 0, low->fII - 1, 4, slope, &sx, &sy, &count, 1) != 0)
	{
		if (count > 3)
			*step = (short) ((sx * 5 + ((count * 3) >> 1)) / (count * 3));
		if (*step == 0)
			goto none;
		if (count < 8)
		{
			if (adjust != 0)
				*step = (short) ((*step + 0x28) >> 1);
			result = 2;
		}
	}
	if (*step != 0)
		return result;
none:
	if (adjust != 0)
		*step = 0x28;
	return 1;
}


// ROM 0x002f8df8 init_proc_XT_ST_CROSS__FP8low_type
// The crossings, dashes and dots tidied before the codes are given (see
// the file's comment).  ==> 0, 1 when a stroke's end has nothing before it.
long
init_proc_XT_ST_CROSS(low_type* low)
{
	SPEC_TYPE* e = low->fSpecl;
	short* y = low->fY;
	short* x = low->fX;
	for ( ; e != nil; e = e->next)
	{
		SPEC_TYPE* prev = e->prev;
		SPEC_TYPE* next = e->next;
		UByte m = e->mark;
		if (m == 6)
		{
			if (e->other == 'c')
			{
				SPEC_TYPE* p = FindMarkLeft(e, 0x10);
				while (p->mark != 0x20)
				{
					if (p != e && p != next && (p->mark == 6 || p->mark == 9))
					{
						if (p->iBeg <= e->iBeg && e->iEnd <= p->iEnd
						 && p->next->iBeg <= next->iBeg && next->iEnd <= p->next->iEnd)
						{
							DelCrossingFromSPECLList(e);
							break;
						}
						p = p->next;
					}
					p = p->next;
				}
			}
			e = e->next;			// (the pair's second, skipped by the loop's step)
		}
		else if (m == 7)
		{
			SPEC_TYPE* p = FindMarkRight(e, 0x10);
			while (p != nil)
			{
				if (p->mark == 0xa && e->iBeg <= p->next->iBeg && p->next->iEnd <= e->iEnd)
				{
					SPEC_TYPE* after = p->next->next;
					MoveCrossing2ndAfter1st(e, p);
					p = after;
				}
				else
					p = p->next;
			}
			if (RCGetH(low->rc, 0x92) != 2 && e->next->mark != 0xa)
			{
				long ym = y[(e->iBeg + e->iEnd) >> 1];
				if (ym < 0x2796 && HWRAbs(x[e->iEnd] - x[e->iBeg]) <= 0x1e)
					e->mark = 8;
			}
		}
		else if (m == 8)
		{
			if (low->fThresh[5] <= y[(e->iBeg + e->iEnd) >> 1]
			 && HWRAbs(x[e->iEnd] - x[e->iBeg]) >= 0x1e
			 && HWRAbs(y[e->iEnd] - y[e->iBeg]) <= 5)
			{
				e->mark = 7;
				e->ipoint1 = -2;
				e->ipoint0 = -2;
			}
		}
		else if (m == 0x20)
		{
			UByte pm = prev->mark;
			if (pm == 6 || pm == 9 || pm == 0xb || pm == 5)
			{
				SPEC_TYPE* q = prev->prev;
				for (;;)
				{
					if (q == nil)
						return 1;
					UByte qm = q->mark;
					if (qm != 0xb && qm != 6 && qm != 0xa && qm != 9 && qm != 5)
						break;
					q = q->prev;
				}
				Move2ndAfter1st(prev, q);
			}
		}
	}
	return 0;
}


// The code a stroke's start or end takes from the extremum folded into it.
// ==> 0 for an element that is not one of those kinds.
static UByte
EndCode(UByte mark, UByte other)
{
	switch (mark)
	{
	case 1:		return 3;
	case 3:		return 7;
	case 5:		return ((other & 1) == 0) ? 0xf : 0x27;
	case 7:		return 0xd;
	case 8:		return 0x10;
	default:	return 0;
	}
}


// ROM 0x002f9100 process_ZZ__FP8low_type
// Each stroke's start and end given the code of the extremum next to it
// (which is folded into it), and a break put between strokes, or two
// strokes joined (see the file's comment).  ==> 0, 1 for a stroke with
// nothing in it.
long
process_ZZ(low_type* low)
{
	SPEC_TYPE* head = low->fSpecl;
	short* y = low->fY;
	short* x = low->fX;
	for (SPEC_TYPE* e = head; e != nil; e = e->next)
	{
		if (e->mark == 0x10)
		{
			SPEC_TYPE* n = e->next;
			UByte code;
			for (;;)
			{
				code = EndCode(n->mark, e->other);
				if (code != 0)
					break;
				e->code = 0;
				while (n != nil && (n->mark == 0xb || n->mark == 6 || n->mark == 0xa || n->mark == 9))
					n = n->next;
				e->next = n;
				if (n == nil)
					return 1;
				n->prev = e;
				if (n->mark == 0x20)
					return 1;
			}
			e->code = code;
			e->iEnd = n->iEnd;
			e->other = n->other;
			e->ipoint0 = n->ipoint0;
			if (n->mark == 1 || n->mark == 3)
				e->attr = (UByte) HeightInLine(y[n->ipoint0], low);
			else
				e->attr = (UByte) MidPointHeight(e, low);
			DelFromSPECLList(n);
			SPEC_TYPE* p = e->prev;
			if (x[e->iBeg - 1] != 0)
			{
				e->other |= 8;
				if (p->code == 3 || p->code == 7)
					p->other |= 8;
			}
			long skipped = 0;
			while (p != nil && (p->code == 0xd || p->code == 0x10 || p->mark == 0xa))
			{
				p = p->prev;
				skipped = 1;
			}
			if (e == head || p == nil || p->mark != 0x20)
				continue;
			SPEC_TYPE* q = e;
			Boolean found = false;
			while (q != nil)
			{
				if (!(q->code == 0xd || q->code == 0x10 || q->mark == 0xa || q->mark == 0x20))
				{
					if (q->mark == 0x10)
						found = true;
					break;
				}
				q = q->next;
				skipped = 1;
			}
			if (!found)
			{
				q = e;
				if (e->code != 0xd && e->code != 0x10)
					skipped = 0;
			}
			// the end p and the start q: a break between them, or one stroke
			long d2 = DistanceSquare(p->iEnd, q->iBeg, x, y);
			long gap = x[p->iEnd + 1];
			ULong flags = RCGetH(low->rc, 0x90);
			if (gap == 0 && d2 <= low->fThresh[15]
			 && (flags & 0x400) == 0 && (flags & 0x800) == 0
			 && (short) RCGetH(low->rc, 4) != 1 && skipped == 0
			 && !((p->code == 3 || p->code == 7) && (e->code == 3 || e->code == 7) && p->code == e->code))
			{
				if (!(p->code == 3 || p->code == 7) || !(e->code == 3 || e->code == 7))
					continue;
				long at = e->iBeg - 1;
				for (SPEC_TYPE* c = e->next; c != nil && c->mark != 0x20; c = c->next)
				{
					if (c->mark == 6 || c->mark == 9)
					{
						if (at < c->iBeg && at > c->next->iEnd)
							DelCrossingFromSPECLList(c);
						c = c->next;
					}
				}
				if (p->code != e->code)
				{
					if (skipped == 0)
					{
						DelThisAndNextFromSPECLList(p);
						continue;
					}
				}
				// (the join below is never reached: codes that match went to
				// the break above, and differing ones either went just now
				// or were skipped - the ROM's code is kept as it is)
				if (skipped != 0)
					continue;
				x[at] = x[e->iBeg];
				y[at] = y[e->iBeg];
				e->iBeg = p->iBeg;
				e->mark = (e->code == 3) ? 1 : 3;
				DelFromSPECLList(p);
				continue;
			}
			// ROM QUIRK: the break is written into the array slot before
			// the stroke's end - the element process_ZZ folded into that
			// end a moment before, now out of the list - and keeps that
			// element's two points
			SPEC_TYPE* brk = p - 1;
			brk->mark = 0x44;
			if (gap != 0)
			{
				brk->code = 0x14;
				brk->other = 8;
			}
			else
				brk->code = 0x12;
			brk->attr = 7;
			brk->iBeg = p->iEnd;
			brk->iEnd = q->iBeg;
			Insert2ndAfter1st(p, brk);
		}
		else if (e->mark == 0x20)
		{
			SPEC_TYPE* p = e->prev;
			UByte code = EndCode(p->mark, e->other);
			if (code == 0)
			{
				e->code = 0;
				DelFromSPECLList(e);
				continue;
			}
			e->code = code;
			e->iBeg = p->iBeg;
			e->ipoint0 = p->ipoint0;
			long h;
			if (p->mark == 1 || p->mark == 3)
				h = HeightInLine(y[p->ipoint0], low);
			else
				h = MidPointHeight(e, low);
			e->attr = (UByte) ((h & 0xf) | (e->attr & 0xf0));
			DelFromSPECLList(p);
		}
	}
	return 0;
}


// ROM 0x002f9690 process_AN__FP8low_type
// Each angle kept, coded 0xe or 0x11 (by which way it opens) and given its
// height, when it is not covered by the extrema either side of it; one
// covered is taken out of the list (and one between two crossings marked
// in `other`: 4 when a crossing either side comes back along itself, 2
// otherwise).  ==> 0.
long
process_AN(low_type* low)
{
	short* x = low->fX;
	short* y = low->fY;
	for (SPEC_TYPE* e = low->fSpecl; e != nil; e = e->next)
	{
		if (e->mark != 0xb)
			continue;
		e->other |= 1;
		Boolean before = false;			// a crossing before it
		Boolean after = false;			// and after
		SPEC_TYPE* p = e->prev;
		SPEC_TYPE* n = e->next;
		SPEC_TYPE* pc = nil;
		SPEC_TYPE* nc = nil;
		for (;;)
		{
			if (p->mark == 6)
			{
				if (p->other != 0)
				{
					p = p->prev;
					continue;
				}
			}
			else if (!IsCrossingMark(p->mark))
				break;
			p = p->prev;
			pc = p;
			before = true;
			while (IsCrossingMark(p->mark))
				p = p->prev;
			break;
		}
		if (IsCrossingMark(n->mark))
		{
			after = true;
			nc = n;
			while (IsCrossingMark(n->mark))
				n = n->next;
		}
		long d = (e->attr != 0) ? 5 : 0;
		short cA = 0;
		short cB = 0;
		if (after)
			cA = (short) (nc->iBeg - d);
		if (before)
			cB = (short) (pc->iEnd + d);
		short nB = (short) (n->iBeg - d);
		short pE = (short) (p->iEnd + d);
		Boolean clear;
		if (after)
			clear = cA > e->iEnd;
		else
			clear = nB > e->iEnd;
		if (clear)
		{
			if (before)
				clear = cB < e->iBeg;
			else
				clear = pE < e->iBeg;
		}
		if (clear)
		{
			e->other &= ~1;
			e->code = ((e->other & 0xf0) == 0x80) ? 0x11 : 0xe;
			e->attr = (UByte) HeightInLine(y[(e->iBeg + e->iEnd) >> 1], low);
			continue;
		}
		long ib = e->iBeg;
		if (ib > pE && nB > e->iEnd)
		{
			Boolean back = (after && nc->mark == 9) || (before && pc->mark == 9);
			DelFromSPECLList(e);
			e->other |= back ? 4 : 2;
			continue;
		}
		Boolean pOK = ib <= pE;			// (r1)
		Boolean nOK = nB <= e->iEnd;	// (r0)
		Boolean keep = false;
		if (e->attr == 0)
			keep = true;
		else
		{
			if (nOK && !pOK)
			{
				if (n->mark == 0x20 && n->code == 3 && x[n->iEnd] > x[n->iBeg])
					keep = true;
				else
					keep = (n->other & 1) != 0;
			}
			else if (!pOK)
				keep = nOK && (n->other & 1) != 0;
			else if (nOK)
				keep = false;
			else
				keep = (p->other & 1) != 0;
		}
		if (!keep)
		{
			DelFromSPECLList(e);
			continue;
		}
		long at = e->ipoint0;
		SPEC_TYPE* q = n;
		if (pOK)
		{
			if (!nOK)
			{
				short m = (short) ((p->iBeg + p->iEnd) >> 1);
				if (p->mark == 0x10 && m > at)
				{
					DelFromSPECLList(e);
					continue;
				}
			}
			if (nOK && (p->mark == 0x10 || n->mark == 0x20))
			{
				DelFromSPECLList(e);
				continue;
			}
			q = p;
		}
		else if (nOK)
		{
			short m = (short) ((n->iBeg + n->iEnd) >> 1);
			if (n->mark == 0x20 && m < at)
			{
				DelFromSPECLList(e);
				continue;
			}
		}
		long qb = q->iBeg;
		long qe = q->iEnd;
		long mid = (short) ((qb + qe) >> 1);
		UByte qm = q->mark;
		long lim;
		Boolean measure;
		if (qm == 0x10 || qm == 0x20)
		{
			lim = 2;
			measure = true;
		}
		else
		{
			lim = 6;
			if (qm == 1)
			{
				mid = (short) ((((at <= mid) ? qe : qb) + at) >> 1);
				measure = x[qe] > x[qb];
			}
			else if (qm == 3 || qm == 5)
				measure = x[qe] > x[qb];
			else
				measure = false;
		}
		if (!measure)
		{
			DelFromSPECLList(e);
			continue;
		}
		if (HWRAbs(mid - at) <= lim)
		{
			DelFromSPECLList(e);
			continue;
		}
		e->other &= ~1;
		e->code = (x[at] >= x[mid]) ? 0x11 : 0xe;
		e->attr = (UByte) HeightInLine(y[at], low);
		if (q == p && q->mark != 0x10 && mid > at)
		{
			DelFromSPECLList(q);
			Insert2ndAfter1st(e, q);
		}
	}
	return 0;
}


// ROM 0x002f9bc8 process_curves__FP8low_type
// The tops and bottoms coded by how sharp their turn is (a top 3 sharp, 2
// round; a bottom 7 sharp, 8 round; the cosine between the arms from the
// extremum's middle, 50 hundredths the divide) with their height and, in
// attr's 0x30 bits, which way they were drawn; the arcs coded 0xf or 0x27
// (the latter's point its middle).  ==> 0.
long
process_curves(low_type* low)
{
	short* y = low->fY;
	short* x = low->fX;
	for (SPEC_TYPE* p = low->fSpecl->next; p != nil; p = p->next)
	{
		UByte m = p->mark;
		if (m == 1)
		{
			p->attr = (UByte) HeightInLine(y[p->ipoint0], low);
			p->attr = (UByte) ((p->attr & 0xcf) | ((x[p->iBeg] < x[p->iEnd]) ? 0x10 : 0x20));
			long k = extremum(p->mark, p->iBeg, p->iEnd, y);
			long c = cos_vect(k, p->iBeg, k, p->iEnd, x, y);
			p->code = (c < 0x32) ? 2 : 3;
		}
		else if (m == 3)
		{
			p->attr = (UByte) HeightInLine(y[p->ipoint0], low);
			p->attr = (UByte) ((p->attr & 0xcf) | ((x[p->iEnd] < x[p->iBeg]) ? 0x10 : 0x20));
			long k = extremum(p->mark, p->iBeg, p->iEnd, y);
			long c = cos_vect(k, p->iBeg, k, p->iEnd, x, y);
			p->code = (c < 0x32) ? 8 : 7;
		}
		else if (m == 5)
		{
			if ((p->other & 1) == 0)
				p->code = 0xf;
			else
			{
				p->code = 0x27;
				p->attr = 7;			// (written over below)
				p->ipoint0 = (short) ((p->iBeg + p->iEnd) >> 1);
			}
			p->attr = (UByte) HeightInLine(y[(p->iBeg + p->iEnd) >> 1], low);
		}
	}
	return 0;
}


// ROM 0x002f8d68 lk_begin__FP8low_type
// The elements sorted again and given their codes (see the file's
// comment); the writing's step measured into low +0x70 and how it was
// measured into +0x72.  ==> 0, 1 for a failure.
long
lk_begin(low_type* low)
{
	SPEC_TYPE* head = low->fSpecl;
	DefLineThresholds(low);
	if (Sort_specl(head, low->fLenSpecl) != 0 || init_proc_XT_ST_CROSS(low) != 0 || process_ZZ(low) != 0)
		return 1;
	process_AN(low);
	process_curves(low);
	low->fStepKind = (short) DefineWritingStep(low, &low->fStep, 1);
	return 0;
}
