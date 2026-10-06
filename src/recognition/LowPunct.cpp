/*
	File:		LowPunct.cpp

	Contains:	The cursive reader's low level: the tests the base-line
				finder's stroke classifier is made of - whether a stroke
				is a comma or a bracket, leading or trailing punctuation,
				an i's dot, an umlaut, a horizontal bar, a t's stem - and
				the gathering of the extrema into an EXTR array.
				See LowLevel.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	A stroke is found in the element list between a 0x10 element (its
	start: iBeg its first point) and a 0x20 (its end: iEnd its last).
	Most of these tests are handed the 0x20 and walk back to the 0x10.
	`h` in the comments is the engine's idea of the letters' height, the
	halfword at rc +0xe0, which the base-line finder writes.  The box's
	middle is (top >> 1) + (bottom >> 1) of the trace's box (low +0x74).
*/

#include "LowLevel.h"
#include "ParaGraph.h"
#include "XrDomains.h"
#include "host/RomBugs.h"


static inline long
LetterHeight(low_type* low)
{
	return (short) RCGetH(low->rc, 0xe0);
}


static inline long
BoxMiddle(low_type* low)
{
	return (low->fBox.top >> 1) + (low->fBox.bottom >> 1);
}


// ROM 0x001bc434 extract_all_extr__FP8low_typeUcP4EXTRPiT4Ps
// The extrema of one kind (1 the tops, 3 the bottoms) copied into extr in
// the order of the list: those of attr 1 or 5 and those coded 0x6e, but
// not those coded 0x6f; each copied one's code becomes 0x64 unless it
// was 0x6e.  *all counts the kind's extrema of attr 1, 5, 3 or 8.  Each
// stroke (but one whose end is attr 8) is also given a shift: the sum of
// two thirds of each gap between a stroke's box and the one before's
// right edge, kept in the stroke end's attr and in the EXTRs' +8 short;
// the last one is answered in *shift.  ==> 0, 1 for more than fifty.
long
extract_all_extr(low_type* low, UByte kind, EXTR* extr, long* all, long* count, short* shift)
{
	short* x = low->fBuffers[0].ptr;
	short* y = low->fBuffers[1].ptr;
	short* map = low->fBuffers[2].ptr;
	long n = 0;
	long total = 0;
	long gap = 0;
	short last = 0;
	long start = 0;						// (the stroke's first point: set at its 0x10, which comes first)
	_RECT box = { 0, 0, 0, 0 };
	short prevRight = 0;
	*all = 0;
	for (SPEC_TYPE* elem = low->fSpecl; elem != nil; elem = elem->next)
	{
		if (elem->mark == 0x10)
		{
			start = elem->iBeg;
			prevRight = box.right;
		}
		if (elem->mark == kind && (elem->attr == 1 || elem->attr == 5 || elem->attr == 3 || elem->attr == 8))
			*all = *all + 1;
		if (elem->mark == kind && elem->code != 0x6f
		 && (elem->attr == 1 || elem->attr == 5 || elem->code == 0x6e))
		{
			if (n > 0x31)
				return 1;
			long p = elem->ipoint0;
			EXTR* e = &extr[n];
			e->x = x[p];
			e->y = y[p];
			e->i = map[p];
			e->susp = 0;
			e->elem = elem;
			if (elem->code != 0x6e)
				elem->code = 0x64;
			n++;
		}
		if (elem->mark == 0x20 && elem->attr != 8)
		{
			GetTraceBox(x, y, (short) start, elem->iEnd, &box);
			// ROM QUIRK: gap is left as it was when the stroke before has
			// no box (its right edge 0) - the first stroke's is nought.
			if (prevRight != 0)
				gap = ((box.left - prevRight) * 2 + 1) / 3;
			if (0 < gap)
				total += gap;
			elem->attr = (UByte) total;
			long m = n;
			for (SPEC_TYPE* back = elem->prev; back->mark != 0x10; back = back->prev)
			{
				// ROM BUG (fixed): this walk back does not skip the extrema
				// coded 0x6f that the copying above left out, so a stroke
				// with one writes its shift into the EXTR before the
				// stroke's own (or before the array, for the first
				// stroke's).  The fix skips them, as the copying does.
				if (RomBugFixed() && back->code == 0x6f)
					continue;
				if (back->mark == kind && (back->attr == 1 || back->attr == 5 || back->code == 0x6e))
				{
					m--;
					extr[m].shift = (short) total;
				}
			}
		}
		last = (short) total;
		if (elem->next == nil)
			*count = n;
	}
	*shift = last;
	return 0;
}


// ROM 0x001c559c com__FP8low_typeP9SPEC_TYPEiN23
// Whether the stroke from i to j is a comma: a short straight one, or a
// curved one.
long
com(low_type* low, SPEC_TYPE* elem, long i, long j, long k)
{
	if (str_com(i, j, low->fBuffers[0].ptr, low->fBuffers[1].ptr, k) != 1
	 && curve_com_or_brkt(low, elem, i, j, k, 0x10) == 0)
		return 0;
	return 1;
}


// ROM 0x001bc6c8 curve_com_or_brkt__FP8low_typeP9SPEC_TYPEiN23Us
// Whether the stroke from i to j (its end element elem) is a curved comma
// (kind 0x10) or a bracket (0x20): more upright than across, made of no
// more than three runs between extrema none of them long sideways, and
// bowed one way throughout - its most distant point from the chord a k-th
// of the chord's length off it, and every piece of it, cut into three,
// four and five, bowed the same way when bowed at all.  ==> the bow's
// sign (1 or -1), times ten when the stroke's shape makes it sure; 0 for
// neither.
long
curve_com_or_brkt(low_type* low, SPEC_TYPE* elem, long i, long j, long k, UShort kind)
{
	short* x = low->fBuffers[0].ptr;
	short* y = low->fBuffers[1].ptr;
	long sure = 1;
	long h = LetterHeight(low);
	long wide = 2, high = 2, mul = 1;
	long s = 0;
	if (kind == 0x20)
	{
		wide = 8;
		high = 5;
		mul = 2;
	}
	long dx = x[j] - x[i];
	long dy = y[j] - y[i];
	if (!(HWRAbs(dx) - ((HWRAbs(dx) + 2) >> 2) <= HWRAbs(dy)))
		return 0;
	SPEC_TYPE* p = elem->prev;
	if (p->mark == 0x10)
		return 0;
	if (!(HWRAbs(x[p->iEnd] - x[p->iBeg]) <= (HWRAbs(dy) * 2 + 1) / 3))
		return 0;
	if (p->mark == 1)
	{
		if (kind == 0x10)
			sure = 10;
		else if (kind == 0x20)
		{
			short from = p->iBeg;
			p = p->prev;
			if (1 < from - p->iEnd)
				return 0;
		}
	}
	long v = HWRAbs(x[p->iBeg] - x[p->iEnd]);
	wide = h * wide;
	if (mul * v > wide)
		sure = 10;
	p = p->prev;
	if (p->mark == 0x10)
		return 0;
	{
		if (!(mul * (x[p->iBeg] - x[p->iEnd]) <= wide))
			return 0;
		if (!(mul * (x[p->iEnd] - x[p->iBeg]) <= h * high))
			return 0;
		p = p->prev;
		if (p->mark != 0x10)
		{
			if (kind == 0x10)
				sure = 10;
			if (p->prev->mark != 0x10)
				return 0;
			if (h + (h >> 1) < HWRAbs(x[p->iBeg] - x[p->iEnd]))
				return 0;
			if (h < HWRAbs(x[p->iEnd] - x[p->next->iBeg]))
				return 0;
			if (h < HWRAbs(y[p->iEnd] - y[p->next->iBeg]))
				return 0;
			if (kind == 0x20 && 1 < p->next->iBeg - p->iEnd)
				return 0;
		}
	}
	long len2 = LAdd(LMul(dy, dy), LMul(dx, dx));
	long m = iMostFarFromChord(x, y, (short) i, (short) j);
	long c = LMul(dx, y[m] - y[i]) + LMul(-dy, x[m] - x[i]);
	if (!(LMul(k, HWRLAbs(c)) > len2))
		return 0;
	if (0 < c)
		s = 1;
	if (c < 0)
		s = -1;
	for (long parts = 3; ; )
	{
		long prev = i;
		for (long t = 1; t <= parts; t++)
		{
			long mid = t * (j - i) / parts + i;
			long far = iMostFarFromChord(x, y, (short) prev, (short) mid);
			long ex = x[mid] - x[prev];
			long ey = y[mid] - y[prev];
			long cc = LMul(ex, y[far] - y[prev]) + LMul(-ey, x[far] - x[prev]);
			if (LAdd(LMul(ey, ey), LMul(ex, ex)) <= LMul(HWRLAbs(cc), 5))
			{
				if (0 < cc && s < 0)
					return 0;
				if (cc < 0 && 0 < s)
					return 0;
			}
			prev = mid;
		}
		parts++;
		if (5 < parts)
			return s * sure;
	}
}


// ROM 0x001bcc78 lead_punct__FP8low_type
// Whether the word starts with punctuation: the first stroke a comma
// above the box's middle (1), and the second one too (2).  ==> 0 for
// neither.
long
lead_punct(low_type* low)
{
	short* y = low->fBuffers[1].ptr;
	short* x = low->fBuffers[0].ptr;
	SPEC_TYPE* first = low->fSpecl->next;
	long i1 = first->iBeg;
	SPEC_TYPE* end1 = first;
	while (end1->mark != 0x20)
		end1 = end1->next;
	long j1 = end1->iEnd;
	_RECT box1, box2;
	GetTraceBox(x, y, (short) i1, j1, &box1);
	SPEC_TYPE* second = end1->next;
	long i2 = second->iBeg;
	SPEC_TYPE* end2 = second;
	while (end2->mark != 0x20)
		end2 = end2->next;
	long j2 = end2->iEnd;
	GetTraceBox(x, y, (short) i2, j2, &box2);
	if (com(low, end1, i1, j1, 5) == 1 && box1.bottom < BoxMiddle(low))
	{
		if (com(low, end2, i2, j2, 5) == 1 && box2.bottom < BoxMiddle(low))
			return 2;
		return 1;
	}
	return 0;
}


// ROM 0x001c519c end_punct__FP8low_typeP9SPEC_TYPEi
// Whether the stroke ending at end is trailing punctuation, with the
// stroke before it: 2 when the two make one mark (a colon, a semicolon, an
// exclamation or question mark's dot and bar, a dot beside the stroke
// before), 1 when it is a mark of its own (a dot, a comma below the
// middle), 0 when it is not punctuation.  k is what a dot's box must be
// under a third of.
long
end_punct(low_type* low, SPEC_TYPE* end, long k)
{
	long h = LetterHeight(low);
	short* x = low->fBuffers[0].ptr;
	short* y = low->fBuffers[1].ptr;
	long j = end->iEnd;
	SPEC_TYPE* p = end->prev;
	while (p->mark != 0x10)
		p = p->prev;
	long i = p->iBeg;
	_RECT box1, box2;
	GetTraceBox(x, y, i, (short) j, &box1);
	SPEC_TYPE* prevEnd = p->prev;
	long pj = prevEnd->iEnd;
	SPEC_TYPE* q = prevEnd;
	while (q->mark != 0x10)
		q = q->prev;
	long pi = q->iBeg;
	GetTraceBox(x, y, pi, (short) pj, &box2);
	if (pnt(box1, k) == 1)
	{
		if (pnt(box2, k) == 1 && HWRAbs(box1.left - box2.left) < h
		 && (box1.top > box2.bottom || box1.bottom < box2.top))
			return 2;
		if (str_com(pi, pj, x, y, 5) == 1 && box1.top > box2.bottom
		 && (HWRAbs(box1.right - box2.left) < h || HWRAbs(box1.left - box2.right) < h))
			return 2;
		if (box1.top > box2.bottom && box1.right < box2.right && box1.left > box2.left)
			return 2;
		return 1;
	}
	if (com(low, end, i, j, 5) == 1)
	{
		if (pnt(box2, k) == 1 && box1.top > box2.bottom
		 && box1.right + h > box2.right && box1.left - h < box2.right)
			return 2;
		if (pnt(box2, k) == 1 && box2.top > box1.bottom
		 && box1.right + h > box2.right && box1.left - h < box2.right)
			return 2;
	}
	if (com(low, end, i, j, 5) == 1 && box1.bottom < BoxMiddle(low))
	{
		if (com(low, prevEnd, pi, pj, 5) != 1)
			return 1;
		if (box2.bottom < BoxMiddle(low))
			return 2;
		return 1;
	}
	if (com(low, end, i, j, 5) == 1
	 && (low->fBox.top * 2 + 1) / 3 + (low->fBox.bottom + 1) / 3 < box1.top)
		return 1;
	return 0;
}


// ROM 0x001bcdec hor_stroke__FP9SPEC_TYPEPsT2i
// Whether the stroke ending at end is a horizontal bar - straight and
// much wider than high - or has one in it: of the three pieces its two
// furthest-from-chord points cut it into (either side of the middle),
// one straight, twice as wide as high, and the stroke's other points
// near its ends.  A word of more than one stroke: only a stroke of up to
// three elements; of one: up to five.
long
hor_stroke(SPEC_TYPE* end, short* x, short* y, long nStrokes)
{
	long j = end->iEnd;
	long elems = 0;
	SPEC_TYPE* p = end->prev;
	while (p->mark != 0x10)
	{
		elems++;
		p = p->prev;
	}
	if (nStrokes > 1 && elems > 3)
		return 0;
	if (nStrokes == 1 && elems > 5)
		return 0;
	long i = p->iBeg;
	long w = HWRAbs(x[j] - x[i]);
	long hgt = HWRAbs(y[j] - y[i]);
	if (straight_stroke(i, j, x, y, 5) == 1 && hgt * 18 < w * 10)
		return 1;
	if (straight_stroke(i, j, x, y, 4) == 1 && hgt * 30 < w * 10)
		return 1;
	long mid = (short) ((i + j) >> 1);
	long a = iMostFarFromChord(x, y, (short) i, mid);
	long b = iMostFarFromChord(x, y, mid, (short) j);
	long abx = HWRAbs(x[b] - x[a]);
	long aby = HWRAbs(y[b] - y[a]);
	long iax = HWRAbs(x[i] - x[a]);
	long iay = HWRAbs(y[i] - y[a]);
	long jbx = HWRAbs(x[j] - x[b]);
	long jby = HWRAbs(y[j] - y[b]);
	long ajx = HWRAbs(x[a] - x[j]);
	long ajy = HWRAbs(y[a] - y[j]);
	long ibx = HWRAbs(x[i] - x[b]);
	long iby = HWRAbs(y[i] - y[b]);
	if (straight_stroke(a, b, x, y, 5) == 1 && aby * 20 < abx * 10)
	{
		long q = (abx + 2) >> 2;
		if (iax < q && iay < q && jbx < q && jby < q)
			return 1;
	}
	if (straight_stroke(a, j, x, y, 5) == 1 && ajy * 20 < ajx * 10)
	{
		long q = (ajx + 2) >> 2;
		if (iax < q && iay < q)
			return 1;
	}
	if (straight_stroke(i, b, x, y, 5) == 1 && iby * 20 < ibx * 10)
	{
		long q = (ibx + 2) >> 2;
		if (jbx < q && jby < q)
			return 1;
	}
	return 0;
}


// ROM 0x001bd24c is_i_point__FP8low_typeP9SPEC_TYPE5_RECTi
// Whether the stroke ending at end, whose box is box, is the dot of an i
// or a j: small (under a third of k + 1 both ways), in the upper part of
// the word, and over a top of an earlier stroke that nothing closes over
// (extrs_open), not already marked (attr 3, 6, 0x3f) and not a stroke's
// last element - the nearest such top in x, within the larger of 2h and
// half the dot's width.  That top is marked attr 5.
long
is_i_point(low_type* low, SPEC_TYPE* end, _RECT box, long k)
{
	long best = 0x7fff;
	SPEC_TYPE* found = nil;
	short* x = low->fBuffers[0].ptr;
	short* y = low->fBuffers[1].ptr;
	long third = (k + 1) / 3;
	if (box.bottom < (low->fBox.bottom * 2 + 1) / 3 + (low->fBox.top + 1) / 3
	 && box.bottom - box.top < third
	 && box.right - box.left < third)
	{
		long lim = LetterHeight(low) * 2;
		long half = (box.right - box.left) >> 1;
		if (lim <= half)
			lim = half;
		SPEC_TYPE* p = end;
		while (p->mark != 0x10)
			p = p->prev;
		if (p != nil)
		{
			while ((p = p->prev) != nil)
			{
				if (p->mark == 1 && p->next->mark != 0x20 && box.bottom < y[p->ipoint0]
				 && p->attr != 3 && p->attr != 6 && p->attr != 0x3f
				 && extrs_open(low, p, 1, 1) != 0)
				{
					long d = HWRAbs(x[p->ipoint0] - ((box.left >> 1) + (box.right >> 1)));
					if (d < best)
					{
						found = p;
						best = d;
					}
				}
			}
		}
		if (found != nil && best <= lim)
		{
			found->attr = 5;
			return 1;
		}
	}
	return 0;
}


// ROM 0x001bd42c is_umlyut__FP9SPEC_TYPE5_RECTiT3PsT5T3
// Whether the stroke ending at end (from point i to j, its box box) is an
// umlaut's dots: a stroke of no more than two tops and two bottoms, over
// a top of an earlier letter that lies within its width and below it -
// or, walking back to the first top left of it, between that top and the
// next (the dots over the gap of a u), or made of one dot beside a
// straight stroke.  ==> 1 or 0.
long
is_umlyut(SPEC_TYPE* end, _RECT box, long i, long j, short* x, short* y, long k)
{
	long tops = 0, bottoms = 0;
	SPEC_TYPE* p = end;
	SPEC_TYPE* found = nil;
	SPEC_TYPE* before = nil;
	SPEC_TYPE* after = nil;
	if (p->mark != 0x10)
	{
		do
		{
			if (p->mark == 1)
				tops++;
			else if (p->mark == 3)
				bottoms++;
			p = p->prev;
		} while (p->mark != 0x10);
		if (tops > 2 || bottoms > 2)
			return 0;
	}
	if (p->prev->prev == nil)
		return 0;
	// walk back from the stroke's start over the earlier tops
	for (p = p->prev; p != nil; )
	{
		if (p->mark != 1 || (p->attr != 1 && p->attr != 5))
		{
			p = p->prev;
			continue;
		}
		long px = x[p->ipoint0];
		if (px > box.right)
		{
			p = p->prev;
			continue;
		}
		if (px >= box.left)
		{
			if (y[p->ipoint0] > box.bottom && p->next->mark != 0x20)
				return 1;
			p = p->prev;
			continue;
		}
		// the first top left of the dots, and the tops either side of it
		found = p;
		for (p = p->prev; p != nil && p->mark != 1; p = p->prev)
			;
		before = p;
		for (p = found->next; p != nil && p->mark != 1; p = p->next)
			;
		after = p;
		break;
	}
	if (found != nil)
	{
		long fy = y[found->ipoint0];
		if (fy > box.bottom && found->next->mark != 0x20 && after != nil
		 && y[after->ipoint0] > box.bottom && x[after->ipoint0] > box.right)
			return 1;
		if (fy > box.bottom && before != nil && y[before->ipoint0] > box.bottom
		 && before->next->mark != 0x20)
		{
			if (straight_stroke(i, j, x, y, 5) == 1)
				return 1;
			if (box.bottom - box.top < (k >> 1))
				return 1;
		}
		return 0;
	}
	// ROM QUIRK: p is nil here when the walk went off the list's head;
	// the ROM then reads its elements from address nought.  The head is
	// never a top, so the walk ends on it, not past it.
	p = end;
	while (p->mark != 0x20 && p->mark != 1)
		p = p->next;
	long xmin = x[p->iEnd];
	if (x[p->iBeg] < xmin)
		xmin = x[p->iBeg];
	if (p->mark == 1 && p->attr == 1 && y[p->ipoint0] > box.bottom && box.right > xmin
	 && box.right - box.left < (x[p->ipoint0] - xmin) * 2)
		return 1;
	return 0;
}


// ROM 0x001c15ac is_t_min__FP9SPEC_TYPEPsT25_RECTiN25UcPi
// Whether the bottom elem, with the extremum after it (or before it, when
// the next is not a top), is a t's stem that the bar from point i to j
// crosses: the stem narrower than k and the bar crossing it, or - with
// flag 1 - the bar's box's bottom between the two extrema's heights and
// the stem within k of the box sideways.  *height is then the stem's
// height.  A stem going left crossed by the bar and low enough also
// counts, without the height.
long
is_t_min(SPEC_TYPE* elem, short* x, short* y, _RECT box, long k, long i, long j, UByte flag, long* height)
{
	long p0 = elem->ipoint0;
	SPEC_TYPE* other = (elem->next->mark == 3) ? elem->next : elem->prev;
	long p1 = other->ipoint0;
	short xEnd = x[elem->iEnd];
	short y0 = y[p0];
	short xOther = x[other->iBeg];
	short y1 = y[p1];
	short xi = x[i], yi = y[i], xj = x[j], yj = y[j];
	// ROM QUIRK: the stem is the segment from the element's last point's x
	// at its extremum's y to the other element's first point's x at its
	// extremum's y.
	if (HWRAbs(xEnd - x[elem->iBeg]) < k)
	{
		if (is_cross(xEnd, y0, xOther, y1, xi, yi, xj, yj) == 1
		 || (flag == 1 && y[p0] < box.bottom && y[p1] > box.bottom
		  && x[p0] < k + box.right && x[p0] > box.left - k))
		{
			*height = y1 - y0;
			return 1;
		}
	}
	if (x[elem->iEnd] - x[elem->iBeg] >= 0)
		return 0;
	long v = y[p0] - ((y[p0] + 2) >> 2) + ((y[p1] + 2) >> 2);
	if (v >= (box.bottom + box.top) >> 1)
		return 0;
	if (is_cross(xEnd, y0, xOther, y1, xi, yi, xj, yj) == 1)
		return 1;
	return 0;
}


// ROM 0x001c347c extrs_open__FP8low_typeP9SPEC_TYPEUci
// Whether the extremum elem (of kind 1 or 3) is open: no point of its
// stroke - from the extremum of that kind before it (with n > 1, the one
// before that that has a code) to the one after it - outside the two
// extrema's own runs lies beyond either of them (above a top, below a
// bottom) within h/2 of it sideways.  ==> 1 open, 0 closed over.
long
extrs_open(low_type* low, SPEC_TYPE* elem, UByte kind, long n)
{
	long h = LetterHeight(low);
	short* x = low->fBuffers[0].ptr;
	short* y = low->fBuffers[1].ptr;
	SPEC_TYPE* p = elem->next;
	while (p->mark != 0x20)
	{
		if (p->mark == kind)
		{
			p = p->next;
			break;
		}
		p = p->next;
	}
	long last = p->iEnd;
	SPEC_TYPE* other = elem;
	p = elem->prev;
	if (n > 1)
	{
		while (p->mark != 0x10 && (p->mark != kind || p->code == 0))
			p = p->prev;
		other = p;
		p = p->prev;
	}
	while (p->mark != 0x10)
	{
		if (p->mark == kind)
		{
			p = p->prev;
			break;
		}
		p = p->prev;
	}
	long first = p->iBeg;
	long ey = y[elem->ipoint0];
	long ex = x[elem->ipoint0];
	long oy = y[other->ipoint0];
	long ox = x[other->ipoint0];
	long s = (kind == 1) ? 1 : -1;
	for (long t = first; t < last; t++)
	{
		if ((t < elem->iBeg || t > elem->iEnd) && s * (ey - y[t]) > 0 && HWRAbs(ex - x[t]) < (h >> 1))
			return 0;
		if ((t < other->iBeg || t > other->iEnd) && s * (oy - y[t]) > 0 && HWRAbs(ox - x[t]) < (h >> 1))
			return 0;
	}
	return 1;
}
