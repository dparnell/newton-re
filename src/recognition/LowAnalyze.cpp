/*
	File:		LowAnalyze.cpp

	Contains:	The cursive reader's low level: the first of
				AnalyzeLowData's passes over the rescaled trace - the
				heights they compare with, the element list tidied,
				sorted and checked, the writing's slant, and the first
				test of the circle finder.  See LowLevel.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	After BaselineAndScale the trace's y runs 0x2796 at the upper border
	to 0x27e6 at the lower (80 units the small letters' height), and x
	is in the same units.  AnalyzeLowData then filters it again, finds
	its extrema afresh and works the list of special elements over in
	some twenty passes before exchange turns it into xrs.

	An element whose mark is 6, 9 or 0xa is a *crossing*, and comes as a
	pair: the list operations move the two together.
*/

#include "LowLevel.h"
#include "ParaGraph.h"
#include "XrDomains.h"


static inline Boolean
IsCrossing(UByte mark)
{
	return mark == 6 || mark == 0xa || mark == 9;
}


// ROM 0x002f8b0c DefLineThresholds__FP8low_type
// The heights the passes compare with (low->fThresh), from the box of the
// rescaled trace:
//   [0]  halfway between the box's top and 0x2746 (0x2746 when the top is
//        below it); d = 0x2796 - [0]
//   [1]  0x2796 - (2d + 1) / 3, [2] 0x2796 - (d + 1) / 3, [3] 0x2796 -
//        (d + 3) / 6: two thirds, a third and a sixth of the way back up
//        from the upper border
//   [4]  0x27a8, [5] [4] + 0x12, [6] [5] + 9, [7] [6] + 0x12: steps
//        down from just below the upper border
//   [11] halfway between the box's bottom and 0x2836 (0x2836 when the
//        bottom is above it); d2 = [11] - 0x27e6
//   [10] 0x27e6 + (2 d2 + 1) / 3, [9] 0x27e6 + (d2 + 1) / 3, [8] 0x27e6 +
//        (d2 + 3) / 6: the same below the lower border
//   [12] 0x7fff, [14] 40, [15] 400 (for figures, rc +0x94 = 0x20: 27 and
//        200).  [13] is left as it was.
void
DefLineThresholds(low_type* low)
{
	short* t = low->fThresh;
	long top = low->fBox.top;
	t[0] = (top < 0x2746) ? (short) ((top + 0x2746) >> 1) : 0x2746;
	long d = (short) (0x2796 - (UShort) t[0]);
	t[1] = (short) (0x2796 - (d * 2 + 1) / 3);
	t[2] = (short) (0x2796 - (d + 1) / 3);
	t[3] = (short) (0x2796 - (d + 3) / 6);
	t[4] = 0x27a8;
	t[5] = (short) ((UShort) t[4] + 0x12);
	t[6] = (short) ((UShort) t[5] + 9);
	t[12] = 0x7fff;
	long bottom = low->fBox.bottom;
	t[11] = (bottom > 0x2836) ? (short) ((bottom + 0x2836) >> 1) : 0x2836;
	long d2 = (short) ((UShort) t[11] - 0x27e6);
	t[10] = (short) ((d2 * 2 + 1) / 3 + 0x27e6);
	t[9] = (short) ((d2 + 1) / 3 + 0x27e6);
	t[8] = (short) ((d2 + 3) / 6 + 0x27e6);
	t[7] = (short) ((UShort) t[6] + 0x12);
	t[14] = 0x28;
	if (RCGetH(low->rc, 0x94) == 0x20)
		t[14] = (short) ((t[14] * 2 + 1) / 3);
	t[15] = 0x190;
	if (RCGetH(low->rc, 0x94) == 0x20)
		t[15] = (short) (t[15] >> 1);
}


// ROM 0x0032f5f0 OperateSpeclArray__FP8low_type
// The empty strokes (a start straight followed by its end) taken out of
// the element array, the rest moved down over them, and the list relinked
// in array order when any went.
void
OperateSpeclArray(low_type* low)
{
	SPEC_TYPE* s = low->fSpecl;
	long n = low->fLenSpecl;
	long i = 1;
	SPEC_TYPE* last = &s[1];
	if (n > 1)
	{
		do
		{
			if (s[i].mark == 0x10 && s[i + 1].mark == 0x20)
			{
				memmove(&s[i], &s[i + 2], (n - i - 2) * sizeof(SPEC_TYPE));	// DEVIATION: sizeof
				n -= 2;
			}
			else
				i++;
		} while (i < n);
	}
	if (n >= low->fLenSpecl)
		return;
	low->fLenSpecl = (short) n;
	low->fLastSpecl = (short) (n - 1);
	s[0].next = &s[1];
	for (long k = 1; k < n; k++)
	{
		last = &s[k];
		last->prev = &s[k - 1];
		last->next = &s[k + 1];
	}
	last->next = nil;
}


// ROM 0x002f9ec0 Sort_specl__FP9SPEC_TYPEs
// The element list sorted into the order of the trace: first by a bubble
// of up to n passes on the first point each covers (a crossing moving as
// its pair), then each stroke's start put before everything that begins
// where it does (and, after it, the shortest element beginning there),
// and each stroke's end after everything that ends where it does.  More
// than n^2 moves in either part is a failure.  ==> 0, 1 for a failure
// (or a stroke start or crossing with nothing before it).
long
Sort_specl(SPEC_TYPE* head, short n)
{
	SPEC_TYPE* best = nil;
	if (n < 3)
		return 0;
	long limit = n * n;
	long moves = 0;
	for (short pass = 1; pass <= n; pass = (short) (pass + 1))
	{
		SPEC_TYPE* p = head->next;
		while (p->next != nil)
		{
			SPEC_TYPE* after = p->next;
			SPEC_TYPE* q = after;
			Boolean pair = IsCrossing(p->mark);
			if (pair)
			{
				q = q->next;
				if (q == nil)
					break;
			}
			if (p->iBeg < q->iBeg)
			{
				if (pair)
					p = after;
				p = p->next;
				continue;
			}
			if (pair)
				DelCrossingFromSPECLList(p);
			else
				DelFromSPECLList(p);
			if (IsCrossing(q->mark))
				q = q->next;
			if (pair)
				InsertCrossing2ndAfter1st(q, p);
			else
				Insert2ndAfter1st(q, p);
			if (++moves > limit)
				return 1;
		}
	}
	moves = 0;
	SPEC_TYPE* p = head->next;
	if (p == nil)
		return 0;
	for (;;)
	{
		if (p->mark == 0x10)
		{
			SPEC_TYPE* before = p->prev;
			SPEC_TYPE* q = before;
			if (IsCrossing(q->mark))
				q = q->prev;
			for (;;)
			{
				if (q == nil)
					return 1;
				if (q->iBeg != p->iBeg)
					break;
				q = q->prev;
				// (the ROM reads the mark of a nil element here - the ROM's
				// first byte, which is no crossing - before it finds the nil)
				if (q != nil && IsCrossing(q->mark))
					q = q->prev;
			}
			if (IsCrossing(q->mark))
				q = q->next;
			if (before != q)
			{
				Move2ndAfter1st(q, p);
				if (++moves > limit)
					return 1;
			}
			short shortest = 0x7fff;
			SPEC_TYPE* next = p->next;
			if (next == nil)
				return 0;
			for (SPEC_TYPE* r = next; r != nil && r->iBeg == p->iBeg; r = r->next)
			{
				UByte m = r->mark;
				if (m == 1 || m == 2 || m == 3 || m == 4 || m == 0x11 || m == 0x13 || m == 0x21 || m == 0x23
				 || m == 0x31 || m == 0x33 || m == 7 || m == 8)
				{
					long d = r->iEnd - r->iBeg;
					if (d < shortest)
					{
						shortest = (short) d;
						best = r;
					}
				}
				if (IsCrossing(m))
					r = r->next;
			}
			if (best == nil)
				return 1;
			if (next != best)
			{
				Move2ndAfter1st(p, best);
				if (++moves > limit)
					return 1;
			}
		}
		if (p->mark == 0x20)
		{
			SPEC_TYPE* next = p->next;
			if (next == nil)
				return 0;
			SPEC_TYPE* r = next;
			SPEC_TYPE* last = nil;			// (set by the loop, which runs at least once when r ends where p does)
			while (r != nil && r->iEnd == p->iEnd)
			{
				if (IsCrossing(r->mark))
					r = r->next;
				last = r;
				r = r->next;
			}
			if (!(r != nil && next == r))
			{
				Move2ndAfter1st(last, p);
				if (++moves > limit)
					return 1;
				if (r != nil)
				{
					p->next = r;
					r->prev = p;
				}
				p = p->prev->prev;
			}
		}
		p = p->next;
		if (p == nil)
			return 0;
	}
}


// ROM 0x002fa20c Clear_specl__FP9SPEC_TYPEs
// The empty strokes taken out of the list, then the list checked: every
// stroke's start matched by an end before the next start.  ==> 0, 1 for
// a list with fewer than four elements, an end with no start, a start
// inside a stroke, or no stroke at all.
long
Clear_specl(SPEC_TYPE* head, short n)
{
	if (n < 4)
		return 1;
	for (SPEC_TYPE* p = head->next; p != nil && p->next != nil; p = p->next)
	{
		if (p->mark == 0x10 && p->next->mark == 0x20)
		{
			// (the two taken out keep their own links, which the walk
			// follows on past them)
			DelThisAndNextFromSPECLList(p);
			p = p->next;
		}
	}
	Boolean inStroke = false, whole = false, ended = false;
	for (SPEC_TYPE* p = head->next; p != nil; p = p->next)
	{
		if (p->mark == 0x10)
		{
			if (inStroke)
				return 1;
			inStroke = true;
			whole = false;
		}
		else
		{
			if (p->mark == 0x20)
			{
				if (!inStroke)
					return 1;
				ended = true;
			}
			if (!inStroke)
				continue;
		}
		if (ended)
		{
			whole = true;
			inStroke = false;
			ended = false;
		}
	}
	return whole ? 0 : 1;
}


// ROM 0x0032f6dc Surgeon__FP8low_type
// Everything in the element array before the first element marked 5 (or
// before the one before a 7 or 8) cut off, the rest moved down to follow
// the head, the list relinked in array order and the elements marked 5,
// 7 and 8 indexed (low->fIndex).  An array with no such element (or where
// it is the last) is emptied instead.  ==> 0.
long
Surgeon(low_type* low)
{
	SPEC_TYPE* s = low->fSpecl;
	long n = low->fLenSpecl;
	short* index = low->fIndex;
	long from = 0;
	short m = 0;
	if (n >= 0)
	{
		for (long i = 0; i <= n; i++)
		{
			m = s[i].mark;
			if (m == 7 || m == 8)
			{
				from = i - 1;
				break;
			}
			from = i;
			if (m == 5)
				break;
		}
	}
	if (m == 0 || from == n)
	{
		InitSpecl(low, kLowSpeclSize);
		return 0;
	}
	long count = n - from;
	memmove(&s[1], &s[from], count * sizeof(SPEC_TYPE));		// DEVIATION: sizeof
	low->fLenSpecl = (short) (count + 1);
	low->fLastSpecl = (short) (low->fLenSpecl - 1);
	long k = 0;
	for (long i = 0; i < low->fLenSpecl; i++)
	{
		s[i].prev = &s[i - 1];
		s[i].next = &s[i + 1];
		UByte mk = s[i].mark;
		if (mk == 5 || mk == 8 || mk == 7)
			index[k++] = (short) i;
	}
	s[0].prev = nil;
	InitSpeclElement(&s[low->fLenSpecl]);
	s[low->fLenSpecl - 1].next = nil;
	low->fLenIndex = (short) k;
	return 0;
}


// ROM 0x00320bd0 measure_slope__FP8low_type
// The writing's slant, in hundredths: over every top (mark 1 or 2)
// followed by a bottom (3 or 4), the downstroke's horizontal drift
// against its depth, counting only the downstrokes more than `ratio`
// times deeper than they drift - ratio 2 at first, down towards 0 while
// those counted are less than three quarters of the rest.  ==> 0 for
// none.
long
measure_slope(low_type* low)
{
	short* x = low->fX;
	short* y = low->fY;
	SPEC_TYPE* head = low->fSpecl;
	long ratio = 2;
	short drift, steep, flat;
	for (;;)
	{
		drift = 0;
		steep = 0;
		flat = 0;
		SPEC_TYPE* p = head->next;
		if (p == nil)
			return 0;
		for (SPEC_TYPE* q = p->next; q != nil; p = q, q = q->next)
		{
			if ((p->mark == 1 || p->mark == 2) && (q->mark == 3 || q->mark == 4))
			{
				short dx = (short) ((UShort) x[p->iEnd] - (UShort) x[q->iBeg]);
				short dy = (short) ((UShort) y[q->iBeg] - (UShort) y[p->iEnd]);
				if (dy > 0 && ratio * HWRAbs(dx) < dy)
				{
					steep = (short) (steep + dy);
					drift = (short) (drift + dx);
				}
				else
					flat = (short) (flat + dy);
			}
		}
		if (ratio > 0 && steep * 3 < flat * 4)
		{
			ratio = (short) (ratio - 1);
			continue;
		}
		break;
	}
	if (steep == 0)
		return 0;
	return (short) ((drift * 100) / steep);
}


// ROM 0x002bc6a0 look_like_circle__FP9SPEC_TYPEN21Ps
// Whether the bottom elem, between the tops prev and next, is the foot of
// an o: no higher than either top, below the upper border, and either
// below the lower border or nearer it than the upper.
long
look_like_circle(SPEC_TYPE* elem, SPEC_TYPE* prev, SPEC_TYPE* next, short* y)
{
	if (next->mark == 1 && prev->mark == 1)
	{
		long v = y[elem->iBeg];
		if (v >= y[prev->iBeg] && v >= y[next->iBeg] && v >= 0x2796)
		{
			if (v > 0x27e6)
				return 1;
			if (v - 0x2796 >= 0x27e6 - v)
				return 1;
		}
	}
	return 0;
}
