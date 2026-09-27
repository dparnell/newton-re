/*
	File:		LowClassify.cpp

	Contains:	The cursive reader's low level: the base-line finder's
				stroke classifiers - what each extremum and each stroke
				of a word is, before the line is looked for - and the
				test for a word of figures.  See LowLevel.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	classify_strokes gives every extremum an attr: 1 an ordinary one,
	3 a t's stem crossed by a bar, 2 part of a horizontal bar, 4 part of
	a dot, 5 a top with a dot over it, 6 and 0x3d/0x3e parts of loops,
	7 an umlaut, 8 punctuation, 9/0xa/0xb/0xc the ends of strokes (an
	entry or an exit stroke), and each stroke end (0x20) the stroke's
	kind.  classify_num_strokes does the same for a word of figures,
	giving each stroke end a figure's kind (0xca upright, 0xcb level,
	0xcc crossing the one before - a plus - 0xcd a four's, 0xcf/0xd0 a
	bracket, 0xc9 a low one, 4 a dot).

	These are transcribed from the disassembly: the decompiler lost the
	conditions of every one of them.
*/

#include "LowLevel.h"
#include "ParaGraph.h"
#include "XrDomains.h"


static inline long
LMax(long a, long b)
{
	return (a <= b) ? b : a;
}


// Every top and bottom of the stroke ending at end given an attr.
static void
MarkStroke(SPEC_TYPE* end, UByte attr)
{
	for (SPEC_TYPE* p = end; p->mark != 0x10; p = p->prev)
		if (p->mark == 1 || p->mark == 3)
			p->attr = attr;
}


// ROM 0x001bfb18 classify_strokes__FP8low_typeiN22PiT5PUi
// Every extremum and stroke end of the word given an attr (above), from
// its neighbours, the letters' median height `mid`, their greatest `most`
// and how many were measured `n`.  A t's stems found measure the tallest
// in *tall (and in *stem too for one not leaning left).  *simple is 1
// unless a stroke has more than three tops or bottoms.  ==> the number of
// strokes.
long
classify_strokes(low_type* low, long mid, long most, long n, long* stem, long* tall, ULong* simple)
{
	short* x = low->fBuffers[0].ptr;
	short* y = low->fBuffers[1].ptr;
	rc_type* rc = low->rc;
	long h = (short) RCGetH(rc, 0xe0);
	long start = 0, end = 0, prevStart = 0, prevEnd = 0;
	long strokes = 0;
	long bar = 0;						// 2: the stroke before was a horizontal bar
	_RECT box = { 0, 0, 0, 0 };
	_RECT prevBox = { 0, 0, 0, 0 };
	long height = 0;
	*simple = 1;
	long tops = 0, bottoms = 0;			// (set at each stroke's start; the list's head has neither mark)
	for (SPEC_TYPE* p = low->fSpecl; p != nil; p = p->next)
	{
		if (p->mark == 0x10)
		{
			bottoms = 0;
			tops = 0;
		}
		else if (p->mark == 1)
			tops++;
		else if (p->mark == 3)
			bottoms++;
		else if (p->mark == 0x20)
		{
			strokes++;
			if (tops > 3 || bottoms > 3)
				*simple = 0;
		}
	}
	long wide = (most + 4) >> 3;
	long third = most + 1;
	long lean = -h;
	long endLim = (0xc >= wide) ? 0xc : wide;
	long midLim = (mid > 0xc) ? mid : 0xc;
	SPEC_TYPE* elem = low->fSpecl;
	while (elem != nil)
	{
		SPEC_TYPE* next = elem->next;
		SPEC_TYPE* prev = elem->prev;
		UByte mark = elem->mark;
		if (mark == 0x10)
		{
			prevStart = start;
			prevEnd = end;
			start = elem->iBeg;
			elem = next;
			continue;
		}
		if (mark == 1 || mark == 3)
		{
			elem->attr = 1;
			elem->code = 0;
		}
		Boolean noT = (RCGetH(rc, 0x90) & 0x800) != 0;
		if (mark == 1)
		{
			if (!noT && next->mark == 3 && bar == 2)
			{
				if (is_t_min(elem, x, y, prevBox, h, prevStart, prevEnd, 0, &height) == 1)
				{
					elem->attr = 3;
					*tall = LMax(*tall, height);
					if (x[elem->iEnd] - x[elem->iBeg] > lean)
						*stem = LMax(*stem, height);
				}
			}
			if (next->mark == 0x20)
			{
				if (prev->mark == 3 && y[prev->ipoint0] - y[elem->ipoint0] < midLim)
					elem->attr = 0xa;
				long xe = x[elem->iEnd];
				if (next->next == nil || xe <= h + low->fBox.left || xe >= low->fBox.right - h)
				{
					long pi = prev->ipoint0;
					long pe = elem->iEnd;
					long m = iMostFarFromChord(x, y, pi, pe);
					long c = LMul(x[pe] - x[pi], y[m] - y[pi]) + LMul(x[m] - x[pi], y[pi] - y[pe]);
					if (c > 0)
						elem->attr = 0xa;
				}
				if (prev->mark == 3 && x[prev->ipoint0] > x[elem->ipoint0] && x[prev->iBeg] > x[prev->iEnd])
					elem->attr = 6;
			}
			// an entry stroke's top, and a top between two bottoms leaning in
			if (prev->mark == 0x10)
			{
				Boolean entry = false;
				if (next->mark == 3)
				{
					long d = y[next->ipoint0] - y[elem->ipoint0];
					if (d <= (most + 4) >> 3 && d <= mid >> 1)
						entry = true;
				}
				if (!entry && y[next->ipoint0] - y[elem->ipoint0] > 0xc)
				{
					elem = next;
					continue;
				}
				elem->attr = 0xb;
			}
			if (prev->mark == 3 && x[prev->ipoint0] >= x[elem->ipoint0] && x[prev->iEnd] < x[prev->iBeg]
			 && next->mark == 3 && x[next->ipoint0] >= x[elem->ipoint0] && x[next->iEnd] > x[next->iBeg])
			{
				long lowest = LMax(y[next->ipoint0], y[prev->ipoint0]);
				if (lowest - y[elem->ipoint0] < third / 3 && prev->prev->mark == 1)
					elem->attr = 6;
			}
			elem = next;
			continue;
		}
		if (mark == 3)
		{
			Boolean toLoop = false;			// straight to the 0x3e test
			Boolean exit = false;			// the 0x3d test without its first condition
			if (prev->mark == 0x10)
			{
				SPEC_TYPE* nn = next->next;
				if (prev->prev->prev == nil && n > 5 && nn->mark == 3 && y[nn->ipoint0] > y[elem->ipoint0])
					elem->attr = 9;
				else if (next->mark == 1)
				{
					if (y[elem->ipoint0] - y[next->ipoint0] >= midLim)
						toLoop = true;
					else
						elem->attr = 9;
				}
			}
			if (!toLoop)
			{
				if (next->mark == 0x20 && prev->mark == 1)
				{
					if (y[elem->ipoint0] - y[prev->ipoint0] <= endLim)
						elem->attr = 0xc;
					else
						exit = true;
				}
				if (exit || prev->mark == 1)
				{
					SPEC_TYPE* pp = prev->prev;
					if (pp->mark == 3
					 && y[elem->ipoint0] * 5 - y[prev->ipoint0] < y[pp->ipoint0] * 4
					 && x[pp->iBeg] < x[pp->iEnd]
					 && x[prev->iBeg] > x[prev->iEnd]
					 && x[pp->iBeg] < x[elem->ipoint0])
					{
						long e = x[elem->iBeg];
						if (x[pp->iEnd] + h > e && x[prev->iBeg] + h > e)
							elem->attr = 0x3d;
					}
				}
				if (next->mark != 1)
				{
					elem = next;
					continue;
				}
			}
			SPEC_TYPE* nn = next->next;
			if (nn->mark == 3 && y[nn->ipoint0] > y[elem->ipoint0])
			{
				long ex = x[elem->ipoint0];
				if (x[nn->iBeg] < ex && x[nn->iEnd] > ex && x[next->iBeg] > ex && x[next->iEnd] < ex)
					elem->attr = 0x3e;
			}
			elem = next;
			continue;
		}
		if (mark == 0x20)
		{
			bar = 0;
			elem->attr = 0;
			end = elem->iEnd;
			GetTraceBox(x, y, (short) start, end, &box);
			if (is_umlyut(elem, box, start, end, x, y, mid) == 1)
			{
				elem->attr = 7;
				MarkStroke(elem, 7);
			}
			if (hor_stroke(elem, x, y, strokes) == 1)
			{
				elem->attr = 2;
				bar = 2;
				SPEC_TYPE* s = elem;
				while (s->mark != 0x10)
				{
					if (s->mark == 1 || s->mark == 3)
						s->attr = 2;
					s = s->prev;
				}
				if ((RCGetH(rc, 0x90) & 0x800) == 0)
				{
					long found = 0;
					for (SPEC_TYPE* t = s->prev; t != nil; t = t->prev)
					{
						if (t->mark == 1 && is_t_min(t, x, y, box, h, start, end, 0, &height) == 1)
						{
							t->attr = 3;
							found++;
							*tall = LMax(*tall, height);
							if (x[t->iEnd] - x[t->iBeg] > lean)
								*stem = LMax(*stem, height);
						}
					}
					if (found == 0)
					{
						for (SPEC_TYPE* t = s; t != nil; t = t->prev)
						{
							if (t->mark == 1 && is_t_min(t, x, y, box, h, start, end, 1, &height) == 1)
							{
								t->attr = 3;
								*tall = LMax(*tall, height);
								if (x[t->iEnd] - x[t->iBeg] > lean)
									*stem = LMax(*stem, height);
							}
						}
					}
				}
			}
			if (is_i_point(low, elem, box, mid) == 1)
			{
				elem->attr = 4;
				MarkStroke(elem, 4);
			}
			if (elem->prev->mark == 0x10 && box.right - box.left < h)
				elem->attr = 4;
			prevBox = box;
		}
		if (elem->next == nil)
			break;
		elem = elem->next;
	}
	// the word's last stroke and its first as punctuation
	long punct = 0;
	if (strokes > 1)
	{
		punct = end_punct(low, elem, mid);
		if (punct != 0)
		{
			elem->attr = 8;
			while (elem->mark != 0x10)
			{
				if (elem->mark == 1 || elem->mark == 3)
					elem->attr = 8;
				elem = elem->prev;
			}
			if (strokes > 2 && punct == 2)
			{
				SPEC_TYPE* p = elem->prev;
				p->attr = 8;
				while (p->mark != 0x10)
				{
					if (p->mark == 1 || p->mark == 3)
						p->attr = 8;
					p = p->prev;
				}
			}
		}
	}
	long rest = strokes - punct;
	if (rest > 1)
	{
		long lead = lead_punct(low);
		if (lead != 0)
		{
			SPEC_TYPE* p = low->fSpecl;
			while (p->mark != 0x20)
			{
				if (p->mark == 1 || p->mark == 3)
					p->attr = 8;
				p = p->next;
			}
			p->attr = 8;
			if (rest > 2 && lead == 2)
			{
				p = p->next;
				while (p->mark != 0x20)
				{
					if (p->mark == 1 || p->mark == 3)
						p->attr = 8;
					p = p->next;
				}
				p->attr = 8;
			}
		}
	}
	return strokes;
}


// ROM 0x001c3d40 classify_num_strokes__FP8low_typePi
// Every stroke end of a word of figures given its kind (above): a stroke
// with no extrema narrower than h is a dot; a straight stroke (or one
// with a bar) is upright (0xca) or level (0xcb), a plus when it crosses
// the upright or level stroke before (0xcc), or - upright, after a
// stroke of kind 1 or a bracket - a four's when it crosses or comes near
// the stroke before's leftmost point at the right height (0xcd: the
// stroke before is then kind 1 and this one's end coded 0xcd as well); a
// stroke that is not straight is a bracket (0xcf, 0xd0 the other way) or,
// low beside the stroke before, 0xc9.  *height (when asked) is the mean
// height of the strokes of kind 1 and 0xca, or of all of them.  ==> the
// number of strokes.
long
classify_num_strokes(low_type* low, long* height)
{
	short* x = low->fBuffers[0].ptr;
	short* y = low->fBuffers[1].ptr;
	long h = (short) RCGetH(low->rc, 0xe0);
	long strokes = 0, good = 0, sumGood = 0, sumAll = 0;
	long start = 0, end = 0, prevStart = 0, prevEnd = 0;
	UByte kind = 0, prevKind = 0;
	_RECT box = { 0, 0, 0, 0 };
	_RECT prevBox = { 0, 0, 0, 0 };
	SPEC_TYPE* before = nil;				// the element before the stroke's start: the stroke before's end
	long xs = 0, ys = 0;
	for (SPEC_TYPE* elem = low->fSpecl; elem != nil; elem = elem->next)
	{
		if (elem->mark == 0x10)
		{
			prevStart = start;
			prevEnd = end;
			if (end != 0)
				prevBox = box;
			before = elem->prev;
			start = elem->iBeg;
			xs = x[start];
			ys = y[start];
			prevKind = kind;
			kind = 1;
			continue;
		}
		if (elem->mark != 0x20)
			continue;
		end = elem->iEnd;
		GetTraceBox(x, y, (short) start, end, &box);
		strokes++;
		sumAll += box.bottom - box.top;
		if (elem->prev->mark == 0x10 && box.right - box.left < h)
		{
			kind = 4;
			elem->attr = 4;
			continue;
		}
		long xe = x[end];
		long ye = y[end];
		long straight = straight_stroke(start, end, x, y, 7);
		if (straight == 1 || hor_stroke(elem, x, y, 1) == 1)
		{
			kind = 0xca;
			long dx = xe - xs;
			if (HWRAbs(ye - ys) <= HWRAbs(dx))
				kind = 0xcb;
			if ((prevKind == 0xca || prevKind == 0xcb)
			 && is_cross(xs, ys, xe, ye, x[prevStart], y[prevStart], x[prevEnd], y[prevEnd]) == 1)
				kind = 0xcc;
			else if (kind == 0xca && (prevKind == 1 || prevKind == 0xcf))
			{
				long i = ixMin((short) prevStart, (short) prevEnd, x, y);
				i = (short) i;
				short xi = x[i], yi = y[i];
				short xpe = x[prevEnd], ype = y[prevEnd];
				long hi = (ys <= ye) ? ye : ys;
				long lo = (ys >= ye) ? ye : ys;
				short px = 0, py = 0;
				if (FindCrossPoint(xs, ys, xe, ye, xi, yi, xpe, ype, &px, &py) == 1)
				{
					if (hi - ((hi + 2) >> 2) + ((lo + 2) >> 2) >= py
					 && lo - ((lo + 2) >> 2) + ((hi + 2) >> 2) <= py)
						kind = 0xcd;
				}
				else if (!(QDistFromChord(xs, ys, xe, ye, xpe, ype) > LMul(h, h) * 3))
				{
					long count = 0;
					SPEC_TYPE* p = before;
					while (p->prev->mark != 0x10)
					{
						if (p->mark == 1 || p->mark == 3)
							count++;
						p = p->prev;
					}
					if (count + 1 <= 3 && p->mark == 1
					 && HWRAbs(x[p->iBeg] - x[p->iEnd]) <= HWRAbs(y[p->iBeg] - y[p->iEnd])
					 && hi - ((hi + 2) >> 2) + ((lo + 2) >> 2) >= ype
					 && lo - ((lo + 2) >> 2) + ((hi + 2) >> 2) <= ype)
						kind = 0xcd;
				}
			}
		}
		if (straight == 0)
		{
			long c = curve_com_or_brkt(low, elem, start, end, 7, 0x20);
			if (c == 1)
				kind = 0xcf;
			else if (c == -1 || c < 0)
			{
				if (c == -1)
					kind = 0xd0;
				if (prevKind != 0 && (prevBox.top * 2 + 1) / 3 + (prevBox.bottom + 1) / 3 < ys)
					kind = 0xc9;
			}
		}
		if (kind == 0xcd)
			elem->attr = 1;
		else
			elem->attr = kind;
		if (kind == 0xcc || kind == 0xcd)
		{
			SPEC_TYPE* p = elem->prev;
			while (p->mark != 0x20)
				p = p->prev;
			p->attr = kind;
		}
		if (elem->attr == 1 || elem->attr == 0xca)
		{
			good++;
			sumGood += box.bottom - box.top;
		}
	}
	if (height != nil)
	{
		// DEVIATION: a word with no strokes is a divide by zero the ROM
		// would trap on; low_level never hands this one.
		if (good != 0)
			*height = sumGood / good;
		else if (strokes != 0)
			*height = sumAll / strokes;
	}
	return strokes;
}


// ROM 0x001c4368 numbers_in_text__FP8low_typePsT2
// Whether the word is figures: classify_num_strokes' kinds must make
// sense of it - no plus at the box's top, no bracket reaching below the
// box's lower fifth, no upright figure wider than it is tall by 14:10
// or whose borders (upper, lower) at its leftmost and rightmost points
// are more than 10/11 of its height apart - and every figure must reach
// above and below the others alike (a descender or an ascender of its
// own - an extremum coded 0x65/0x66, or a t's stem or a struck-out or
// put-back top above the upper border - or a stroke beside it that sticks
// out), with the heights so measured within 15:10 of one another.  A
// single figure among other strokes must have a descender under every
// bottom of its own.  ==> 1 figures, 0 not.
long
numbers_in_text(low_type* low, short* upper, short* lower)
{
	long strokes = 0, figures = 0, dots = 0;
	long most = 0, least = 0x7fff;
	short* map = low->fBuffers[2].ptr;
	short* x = low->fBuffers[0].ptr;
	short* y = low->fBuffers[1].ptr;
	long start = 0;
	_RECT box = { 0, 0, 0, 0 };
	classify_num_strokes(low, nil);
	long allDown = 1, allUp = 1;
	SPEC_TYPE* elem = low->fSpecl;
	for (; elem != nil; elem = elem->next)
	{
		if (elem->mark == 0x10)
			start = elem->iBeg;
		else if (elem->mark == 0x20)
		{
			strokes++;
			long end = elem->iEnd;
			if (elem->attr == 4)
				dots++;
			else
			{
				if (elem->attr == 0xcc)
				{
					GetTraceBox(x, y, (short) start, end, &box);
					if (box.top == low->fBox.top)
						return 0;
				}
				if (elem->attr == 0xcf || elem->attr == 0xd0)
				{
					GetTraceBox(x, y, (short) start, (short) end, &box);
					if (low->fBox.top + low->fBox.bottom * 4 > box.bottom * 5)
						return 0;
				}
				if (elem->attr == 1 || elem->attr == 0xca || elem->attr == 0xcf)
				{
					figures++;
					long i0 = (short) start, i1 = (short) end;
					GetTraceBox(x, y, i0, i1, &box);
					long hgt = box.bottom - box.top;
					if (hgt * 14 < (box.right - box.left) * 10)
						return 0;
					long l = ixMin(i0, i1, x, y);
					long r = ixMax(i0, i1, x, y);
					long a = lower[map[l]] - upper[map[l]];
					long b = lower[map[r]] - upper[map[r]];
					long m = (a > b) ? a : b;
					if (m * 11 > hgt * 10)
						return 0;
					long down = 0, up = 0;
					for (SPEC_TYPE* p = elem->prev; p->mark != 0x10; p = p->prev)
					{
						if (p->mark == 3)
						{
							if (p->code == 0x65)
								down = 1;
						}
						else if (p->mark == 1)
						{
							if (p->code == 0x66)
								up = 1;
							else if ((p->attr == 3 || p->code == 0x0d || p->code == 0x6e)
								  && y[p->iEnd] < upper[map[p->iEnd]])
								up = 1;
						}
					}
					if ((allDown == 1 && down == 0) || (allUp == 1 && up == 0))
					{
						SPEC_TYPE* nx = elem->next;
						if (nx != nil)
						{
							long ni = nx->iBeg;
							SPEC_TYPE* ne = nx;
							while (ne->mark != 0x20)
								ne = ne->next;
							_RECT b2;
							GetTraceBox(x, y, (short) ni, ne->iEnd, &b2);
							if (b2.top > box.bottom && box.right < b2.left)
							{
								down = 1;
								hgt = b2.bottom - box.top;
							}
							if (b2.bottom < box.top && box.right < b2.left)
							{
								up = 1;
								hgt = box.bottom - b2.top;
							}
						}
						// ROM BUG: meant to be the stroke before, this is the
						// figure's own last extremum and its own start, so the
						// box it measures is the figure's own and neither test
						// below can pass
						SPEC_TYPE* pv = elem->prev;
						if (pv != nil)
						{
							long pe = pv->iEnd;
							SPEC_TYPE* ps = pv;
							while (ps->mark != 0x10)
								ps = ps->prev;
							_RECT b2;
							GetTraceBox(x, y, ps->iBeg, (short) pe, &b2);
							if (b2.top > box.bottom && b2.right > box.left)
							{
								down = 1;
								hgt = b2.bottom - box.top;
							}
							if (b2.bottom < box.top && b2.right > box.left)
							{
								up = 1;
								hgt = box.bottom - b2.top;
							}
						}
					}
					if (down == 0)
						allDown = 0;
					if (up == 0)
						allUp = 0;
					most = (most <= hgt) ? hgt : most;
					least = (least >= hgt) ? hgt : least;
				}
			}
		}
		if (elem->next == nil)
			break;
	}
	if (figures > 1 && allDown == 0 && allUp == 0)
		return 0;
	if (least * 15 < most * 10)
		return 0;
	if (figures == 0)
		return 0;
	if (figures == 1 && strokes > 3)
		return 0;
	if (strokes == 2 && dots == 1)
		return 0;
	if (figures != 1)
		return 1;
	if (allDown == 0)
		return 0;
	// the single figure: every bottom of its own over a descender
	SPEC_TYPE* p = elem;
	for (;; p = p->prev)
	{
		if (p->attr == 1)
			break;
		if (p->attr == 0xca)
			return 0;
	}
	SPEC_TYPE* fig = p;
	for (; p->mark != 0x10; p = p->prev)
	{
		if (p->mark == 3 && p->code == 0x64)
		{
			long a = x[p->iBeg], b = x[p->iEnd];
			SPEC_TYPE* q = fig;
			if (q->mark == 0x10)
				return 0;
			Boolean over = false;
			for (; q->mark != 0x10; q = q->prev)
			{
				if (q->mark == 3 && q->code == 0x65)
				{
					long c = x[q->iBeg], d = x[q->iEnd];
					long hiAB = (a <= b) ? b : a;
					long loCD = (c >= d) ? d : c;
					if (hiAB < loCD)
						continue;
					long hiCD = (c > d) ? c : d;
					long loAB = (a >= b) ? b : a;
					if (hiCD >= loAB)
					{
						over = true;
						break;
					}
				}
			}
			if (!over)
				return 0;
		}
	}
	return 1;
}
