/*
	File:		LowXtSt.cpp

	Contains:	The cursive reader's low level: xt_st_zz - the strokes
				written out of order put where they belong (a t's bar, an
				i's dot, an umlaut, a crossed-out x, quotes and the like),
				the breaks between the letters worked out, and the last of
				the elements tidied before exchange writes them.  See
				LowLevel.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	The element codes met here besides lk_begin's: 0xd a stroke written
	late (a bar, a dot) and 0x10 a dot, 0x12 a break between strokes, 0x13
	and 0x14 the breaks round a stroke moved in among the others (marked
	0x44), and mark 0xa for the halves of a crossing that belongs to a
	late stroke.

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

// A break, a late stroke or a dot: what is skipped over looking for the
// letters either side.
static inline Boolean
IsBreakOrLate(UByte c)
{
	return IsBreakCode(c) || c == 0xd || c == 0x10;
}

static inline Boolean
Overlaps(SPEC_TYPE* a, SPEC_TYPE* b)
{
	return a->iEnd >= b->iBeg && b->iEnd >= a->iBeg;
}


// ROM 0x002af89c conv_top_elem_to_ST__FP8low_type
// A lone short stroke high up - a stick (3 or 7, height band at most 4)
// that is a whole stroke after a break - made a dot (8/0x10, band 2); and
// a stroke of two sticks, both high, whose box is at least twice as tall
// as it is wide, made one element of it (8/0x10 at its middle's band),
// unless a dot high up (0x10, band 7 or more) follows.  ==> 0.
long
conv_top_elem_to_ST(low_type* low)
{
	short* y = low->fY;
	short* x = low->fX;
	for (SPEC_TYPE* e = low->fSpecl; e != nil; e = e->next)
	{
		SPEC_TYPE* n = e->next;
		SPEC_TYPE* p = e->prev;
		long h = e->attr & 0xf;
		long hn = 0;
		if (n != nil)
			hn = n->attr & 0xf;
		if ((e->code == 3 || e->code == 7) && h <= 4 && p->code == 0x12
		 && (n == nil || IsBreakCode(n->code)))
		{
			RefreshElem(e, 8, 0x10, 2);
			e->other = 0;
		}
		if (n == nil)
			continue;
		if (e->code == 3)
		{
			if (n->code != 7)
				continue;
		}
		else if (!(e->code == 7 && n->code == 3))
			continue;
		if (h > 4 || hn > 4)
			continue;
		if (p->code != 0x12)
			continue;
		SPEC_TYPE* nn = n->next;
		if (nn != nil && !IsBreakCode(nn->code))
			continue;
		_RECT box;
		GetTraceBox(x, y, e->iBeg, n->iEnd, &box);
		// (the ROM takes the two differences out of two unaligned words:
		// the low halves are the width and the height)
		short w = (short) (box.right - box.left);
		short ht = (short) (box.bottom - box.top);
		if (nn != nil)
		{
			SPEC_TYPE* nnn = nn->next;
			// DEVIATION: at the end of the list the ROM reads the code of
			// the element at address 0; the host takes it as not a dot
			if (nnn != nil && nnn->code == 0x10 && (nnn->attr & 0xf) >= 7)
				continue;
		}
		if (ht < w * 2)
			continue;
		e->iEnd = n->iEnd;
		RefreshElem(e, 8, 0x10, (UByte) MidPointHeight(e, low));
		e->other = 0;
		DelFromSPECLList(n);
	}
	return 0;
}


// ROM 0x002afa84 Placement_XT_CUTTED__FP9SPEC_TYPEP8low_type
// A late stroke e marked other 4 and moved to just before the nearest
// upper element before it whose start lies, in the stroke of the point
// its crossing partner (the element after it in the array) names, no
// further on than that point - or, failing one, the nearest at or after
// it.  ==> 0.
long
Placement_XT_CUTTED(SPEC_TYPE* e, low_type* low)
{
	long at = (e + 1)->ipoint0;
	e->other |= 4;
	long g = GetGroupNumber(low, at);
	long gBeg = low->fGroups[g].iBeg;
	long gEnd = low->fGroups[g].iEnd;
	SPEC_TYPE* head = low->fSpecl;
	SPEC_TYPE* r = e;
	if (head != r)
	{
		do
		{
			r = r->prev;
			if (IsUpperElem(r) && r->iBeg <= at && r->iBeg >= gBeg && r->iBeg <= gEnd)
				break;
		}
		while (head != r);
	}
	if (head == r)
	{
		r = e;
		if (head != e)
		{
			do
			{
				r = r->prev;
				if (IsUpperElem(r) && r->iBeg >= at && r->iBeg >= gBeg && r->iBeg <= gEnd)
					break;
			}
			while (head != r);
		}
		if (head == r)
			return 0;
	}
	Move2ndAfter1st(r->prev, e);
	return 0;
}


// ROM 0x002afb9c SortXT_ST__FP8low_type
// A run of late strokes and dots (0xd, 0x10) with more than one of them:
// the dots moved ahead of the first late stroke, and when there is more
// than one late stroke, those sorted by height band.  ==> 0.
long
SortXT_ST(low_type* low)
{
	for (SPEC_TYPE* e = low->fSpecl; e != nil; e = e->next)
	{
		if (e->code != 0xd)
			continue;
		short count = 0;
		short late = 0;
		SPEC_TYPE* r = e;
		while (r != nil && (r->code == 0xd || r->code == 0x10))
		{
			count++;
			if (r->code == 0xd)
				late++;
			r = r->next;
		}
		if (count <= 1)
			continue;
		for (SPEC_TYPE* q = e->next; q != r; q = q->next)
			if (q->code == 0x10)
				Move2ndAfter1st(e->prev, q);
		SPEC_TYPE* before = e->prev;
		if (late <= 1)
			continue;
		SPEC_TYPE* last;
		Boolean sorted;
		do
		{
			sorted = true;
			SPEC_TYPE* q = before->next;
			SPEC_TYPE* t = q->next;
			last = t;
			while (t != nil && t->code == 0xd)
			{
				last = t;
				t = t->next;
			}
			while (q != nil && q != last && q->code == 0xd)
			{
				if ((q->attr & 0xf) > (q->next->attr & 0xf))
				{
					SwapThisAndNext(q);
					sorted = false;
				}
				q = q->next;
			}
		}
		while (!sorted);
		e = last;
	}
	return 0;
}


// ROM 0x002afce4 GetStrokeWhichBelongsToRestricted__FP9SPEC_TYPEP8low_typePsT3
// The first and last points of the stroke of the letter before e (the
// breaks, late strokes and dots skipped); both 0 when there is none.
// ==> 0.
long
GetStrokeWhichBelongsToRestricted(SPEC_TYPE* e, low_type* low, short* beg, short* end)
{
	SPEC_TYPE* r = e->prev;
	*beg = 0;
	*end = 0;
	while (r != nil && IsBreakOrLate(r->code))
		r = r->prev;
	if (r == nil)
		return 0;
	short g = (short) GetGroupNumber(low, r->iEnd);
	*beg = low->fGroups[g].iBeg;
	*end = low->fGroups[g].iEnd;
	return 0;
}


// ROM 0x002afda4 FindDelayedStroke__FP8low_type
// A stroke after the first, made only of low-ish elements (band 7 at
// most, or angles and the like), that lies to the left of everything
// written before it (its right plus the writing's step): it was written
// late - a t's bar, say - and becomes one late element 0xd over the whole
// stroke (the elements inside it dropped from the list).  A crossing
// found within it (find_CROSS) that does not overlap it is marked as the
// late stroke's (0xa) and moved after it.  ==> 0.
long
FindDelayedStroke(low_type* low)
{
	short* y = low->fY;
	short* x = low->fX;
	for (SPEC_TYPE* e = low->fSpecl; e != nil; e = e->next)
	{
		if (e->mark != 0x10 || e->iBeg == 1)
			continue;
		SPEC_TYPE* end = nil;
		for (SPEC_TYPE* r = e; r != nil; )
		{
			UByte c = r->code;
			if (IsBreakCode(c))
				break;
			Boolean skip = c == 0x24 || c == 0x23 || c == 0x26 || c == 0x25 || c == 0xe || c == 0x11
						 || c == 0x28 || c == 0x29 || c == 0xf;
			if (!skip)
			{
				if ((r->attr & 0xf) > 7)
					break;
				if (!(c == 3 || c == 7 || c == 0xa || c == 9 || c == 0xc || c == 0xb || c == 2 || c == 8))
					break;
			}
			if (r->mark == 0x20)
			{
				end = r;
				break;
			}
			r = r->next;
		}
		if (end == nil)
			continue;
		_RECT box, before;
		GetTraceBox(x, y, e->iBeg, end->iEnd, &box);
		GetTraceBox(x, y, 0, e->iBeg - 1, &before);
		if (low->fStep + box.right >= before.right)
			continue;
		e->code = 0xd;
		e->attr = (UByte) HeightInLine((short) ((box.top + box.bottom) >> 1), low);
		e->other = 0;
		e->iEnd = end->iEnd;
		e->next = end->next;
		if (end->next != nil)
			end->next->prev = e;
		SPEC_TYPE* cross;
		if (find_CROSS(low, e->iBeg, e->iEnd, &cross) == 0)
			continue;
		SPEC_TYPE* q = cross->next;
		if (e->iEnd >= q->iBeg && q->iEnd >= e->iBeg)
			continue;
		q->mark = 0xa;
		cross->mark = 0xa;
		InsertCrossing2ndAfter1st(e, cross);
	}
	return 0;
}


// ROM 0x002affdc CheckStrokesForDxTimeMatch__FP8low_type
// The last stroke of the word, when there is more than one and the step
// was measured: if its rightmost point (the slant taken out) is further
// left than the rightmost point of what came before by more than twice
// the step (three times for a late stroke high up), it was written late
// and is not part of the word - it is taken out, with the break before it
// when it ends the list.  ==> 0.
long
CheckStrokesForDxTimeMatch(low_type* low)
{
	short* y = low->fY;
	short* x = low->fX;
	SPEC_TYPE* head = low->fSpecl;
	long slope = low->fSlope;
	Boolean late = false;
	SPEC_TYPE* last = head->next;
	if (low->fLenGroups == 1 || low->fStepKind == 1)
		return 0;
	while (last->next != nil)
		last = last->next;
	while (IsBreakCode(last->code))
		last = last->prev;
	long g = GetGroupNumber(low, last->iBeg);
	long gBeg = low->fGroups[g].iBeg;
	SPEC_TYPE* r = last;
	while (r != head && gBeg <= r->iBeg)
		r = r->prev;
	SPEC_TYPE* first = r->next;
	if (first != last)
	{
		while (first != nil && IsBreakOrLate(first->code))
			first = first->next;
		if (first == nil)
			return 0;
	}
	else
	{
		if (first->code == 0xd)
			late = true;
		else if (first->code == 0x10)
		{
			if (first->other & 0xc)
				return 0;
			late = true;
		}
	}
	SPEC_TYPE* before = first->prev;
	while (before != head && IsBreakOrLate(before->code))
		before = before->prev;
	if (before == head)
		return 0;
	long i = ixMax(first->iBeg, last->iEnd, x, y);
	long right = x[i] - SlopeShiftDx((short) (0x27be - y[i]), slope);
	long j = ixMax(0, before->iEnd, x, y);
	long rightBefore = x[j] - SlopeShiftDx((short) (0x27be - y[j]), slope);
	long margin;
	if (late && (first->attr & 0xf) >= 7)
		margin = 3 * low->fStep;
	else
		margin = 2 * low->fStep;
	if (right + margin >= rightBefore)
		return 0;
	if (late)
		DelFromSPECLList(first);
	else
	{
		SPEC_TYPE* q = first;
		for (; q != last; q = q->next)
			DelFromSPECLList(q);
		DelFromSPECLList(q);
	}
	SPEC_TYPE* b = first->prev;
	if (IsBreakCode(b->code) && (last->next == nil || IsBreakCode(last->next->code)))
		DelFromSPECLList(b);
	return 0;
}


// ROM 0x002b0298 placement_X__FP8low_type
// The last stroke, when it is an up-stroke and a down-stroke (a start 3,
// or an arc, low; an end 7 or arc, from band 7) lying wholly more than 50
// to the left of the rightmost point before it - the second half of an x
// crossed out after the word - and a crossing inside it shows where it
// crosses: it is moved, between two breaks (0x13 before, a new 0x14 marked
// 0x44 after), to after the low bottom (8, band 7 up, at the bottom)
// nearest that crossing, unless the crossing is in the letter just
// before it.  ==> 0.
long
placement_X(low_type* low)
{
	SPEC_TYPE* head = low->fSpecl;
	SPEC_TYPE* best = head;
	short* x = low->fX;
	short* y = low->fY;
	SPEC_TYPE* e = head;
	while (e->next != nil)
		e = e->next;
	SPEC_TYPE* end = FindMarkLeft(e, 0x20);
	if (end == nil)
		return 0;
	SPEC_TYPE* start = end->prev;
	UByte c = start->code;
	if (c == 9)
	{
		if ((start->attr & 0x30) != 0x10)
			return 0;
	}
	else if (c == 0xa)
	{
		if ((start->attr & 0x30) != 0x20)
			return 0;
	}
	else if (c != 3)
		return 0;
	if (start->mark != 0x10 || (start->attr & 0xf) > 7)
		return 0;
	c = end->code;
	if (c == 0xb)
	{
		if ((end->attr & 0x30) != 0x10)
			return 0;
	}
	else if (c == 0xc)
	{
		if ((end->attr & 0x30) != 0x20)
			return 0;
	}
	else if (c != 7)
		return 0;
	if ((end->attr & 0xf) < 7)
		return 0;
	SPEC_TYPE* r = start->prev;
	while (r != nil && (IsBreakOrLate(r->code) || r->mark == 0xa))
		r = r->prev;
	if (r == nil)
		return 0;
	SPEC_TYPE* after = r->next;
	short xMin, xMax;
	xMinMax(0, r->iEnd, x, y, &xMin, &xMax);
	long lim = xMax - 0x32;
	long a = start->iBeg;
	long b = end->iEnd;
	if (!(x[a] < lim && x[b] < lim))
		return 0;
	if (x[(a + b) / 2] >= lim)
		return 0;
	SPEC_TYPE* cross;
	if (find_CROSS(low, a, b, &cross) == 0)
		return 0;
	long at = cross->next->iEnd;
	if (at >= r->prev->iBeg && at <= r->iEnd)
		return 0;
	long nearest = 0x7fff;
	for (; r != nil; r = r->prev)
	{
		if (r->code == 8 && (r->attr & 0xf) >= 7 && (r->attr & 0x30) == 0x20)
		{
			long d = HWRAbs(r->iBeg - at);
			if (d < nearest)
			{
				nearest = d;
				best = r;
			}
		}
	}
	if (best == head)
		return 0;
	SPEC_TYPE* brk = NewSPECLElem(low);
	if (brk == nil)
		return 0;
	after->code = 0x13;
	Move2ndAfter1st(best, after);
	Move2ndAfter1st(after, start);
	Move2ndAfter1st(start, end);
	Insert2ndAfter1st(end, brk);
	brk->code = 0x14;
	brk->mark = 0x44;
	brk->attr = 7;
	brk->other = 1;
	brk->iBeg = end->iEnd;
	brk->iEnd = end->iEnd;
	return 0;
}


// ROM 0x002b05a8 FindMisplacedParentheses__FP8low_type
// A stroke of a start going up and an end coming down low (a "(" - start
// 3/0xa below band 7 after a break, a late stroke or a dot; end 7/0xc
// past band 7 closing the stroke) that lies left of everything before it
// by twice the step and starts further left: written after the word, it
// is moved to the front, the break before it (made a 0x14) after it.
// Only the first is moved.  ==> 0.
long
FindMisplacedParentheses(low_type* low)
{
	SPEC_TYPE* head = low->fSpecl;
	short* y = low->fY;
	short* x = low->fX;
	for (SPEC_TYPE* e = head->next; e != nil; e = e->next)
	{
		SPEC_TYPE* p = e->prev;
		if (e->mark != 0x10 || (e->attr & 0xf) >= 7)
			continue;
		if (e->code != 3 && e->code != 0xa)
			continue;
		if (!(IsBreakOrLate(p->code) || p->mark == 0xa))
			continue;
		SPEC_TYPE* n = e->next;
		if (n == nil || n->mark != 0x20)
			continue;
		if (n->next != nil && !IsBreakCode(n->next->code))
			continue;
		if (n->code != 7 && n->code != 0xc)
			continue;
		if ((n->attr & 0xf) <= 7)
			continue;
		_RECT box, before;
		GetTraceBox(x, y, e->iBeg, n->iEnd, &box);
		GetTraceBox(x, y, 0, e->iBeg - 1, &before);
		if (!(box.right + 2 * low->fStep < before.right && box.left < before.left))
			continue;
		SPEC_TYPE* q = e->prev;
		while (q != nil && !IsBreakCode(q->code))
			q = q->prev;
		if (q != nil)
		{
			q->code = 0x14;
			Move2ndAfter1st(head, q);
			Move2ndAfter1st(head, e);
			Move2ndAfter1st(e, n);
		}
		return 0;
	}
	return 0;
}


// ROM 0x002b0780 find_CROSS__FP8low_typesT2PP9SPEC_TYPE
// The first crossing (mark 6, the first of each pair in the array) inside
// iBeg..iEnd whose partner - the element after it in the array - lies
// wholly outside it.  ==> 1 and *cross, or 0.
long
find_CROSS(low_type* low, short iBeg, short iEnd, SPEC_TYPE** cross)
{
	SPEC_TYPE* specl = low->fSpecl;
	long n = low->fLenSpecl;
	long first = 1;
	for (short i = 0; i < n; i++)
	{
		SPEC_TYPE* e = &specl[i];
		if (e->mark != 6)
			continue;
		if (first == 1)
		{
			if (iBeg <= e->iBeg && iEnd >= e->iEnd)
			{
				SPEC_TYPE* partner = e + 1;
				if (iEnd < partner->iBeg || iBeg > partner->iEnd)
				{
					*cross = e;
					return 1;
				}
			}
			first = 2;
		}
		else
			first = 1;
	}
	return 0;
}


// ROM 0x002b0b24 IsPartOfTrajectoryInside__FP8low_typeP9SPEC_TYPET2
// Whether some letter element (not a break, dot or late stroke) before a
// or after b lies within the stroke a..b across and reaches less far down.
long
IsPartOfTrajectoryInside(low_type* low, SPEC_TYPE* a, SPEC_TYPE* b)
{
	_RECT box0, box;
	GetTraceBox(low->fX, low->fY, a->iBeg, b->iEnd, &box0);
	for (SPEC_TYPE* r = low->fSpecl->next; r != nil && r != a; r = r->next)
	{
		if (IsBreakOrLate(r->code))
			continue;
		GetTraceBox(low->fX, low->fY, r->iBeg, r->iEnd, &box);
		if (box0.left <= box.left && box0.right >= box.right && box0.bottom > box.bottom)
			return 1;
	}
	for (SPEC_TYPE* r = b->next; r != nil; r = r->next)
	{
		if (IsBreakOrLate(r->code))
			continue;
		GetTraceBox(low->fX, low->fY, r->iBeg, r->iEnd, &box);
		if (box0.left <= box.left && box0.right >= box.right && box0.bottom > box.bottom)
			return 1;
	}
	return 0;
}


// ROM 0x002b083c find_umlaut__FP8low_type
// A stroke after the first made of a few small elements (at most five
// that are not angles, half of them or fewer above band 3... every
// element band 5 or lower) that crosses nothing, has nothing of the word
// under it, and is not an ! or ? - two dots, an umlaut's: made one dot
// element (0x10, other 2) over the whole stroke; when it is the first
// stroke after a break, not if it is the second of two strokes of which
// the first is higher (SecondHigherFirst).  ==> 0.
long
find_umlaut(low_type* low)
{
	short* y = low->fY;
	for (SPEC_TYPE* e = low->fSpecl; e != nil; e = e->next)
	{
		if (e->mark != 0x10 || e->iBeg == 1)
			continue;
		short count = 0;
		short high = 0;
		SPEC_TYPE* end = nil;
		for (SPEC_TYPE* r = e; r != nil; )
		{
			UByte c = r->code;
			if (IsBreakCode(c))
				break;
			Boolean angle = c == 0x23 || c == 0x24 || c == 0x27 || c == 0xe || c == 0x11 || c == 0x28 || c == 0x29;
			if (!angle)
			{
				if ((r->attr & 0xf) > 5)
					break;
				if (!(c == 3 || c == 7 || c == 0xa || c == 9 || c == 0xc || c == 0xb || c == 2 || c == 8))
					break;
				count++;
			}
			if ((r->attr & 0xf) <= 3)
				high++;
			if (r->mark == 0x20)
			{
				end = r;
				break;
			}
			r = r->next;
		}
		if (end == nil)
			continue;
		short k = (short) ((short) brk_left(y, e->iBeg - 2, 0) + 1);
		SPEC_TYPE* p = e->prev;
		SPEC_TYPE* pp = p->prev;
		if (count > 5)
			continue;
		count /= 2;
		if (count > high)
			continue;
		SPEC_TYPE* cross;
		if (find_CROSS(low, e->iBeg, end->iEnd, &cross) != 0)
			continue;
		if (IsPartOfTrajectoryInside(low, e, end) != 0)
			continue;
		if (IsExclamationOrQuestionSign(low, e, end) != 0)
			continue;
		if (k == 1)
		{
			if (!IsBreakCode(p->code))
				continue;
			if (low->fSpecl == pp)
				continue;
			if (SecondHigherFirst(low, p, pp, e, k, p->iBeg, e->iBeg, end->iEnd) != 0)
				continue;
		}
		short yMin, yMax;
		yMinMax(e->iBeg, end->iEnd, y, &yMin, &yMax);
		e->attr = (UByte) HeightInLine((short) ((yMin + yMax) >> 1), low);
		e->code = 0x10;
		e->other = 2;
		e->iEnd = end->iEnd;
		e->next = end->next;
		if (end->next != nil)
			end->next->prev = e;
	}
	return 0;
}


// ROM 0x0030857c CalcDistBetwXr__FPsT1iN33T1
// The distance (Distance8) between two pieces of the trace, a0..a1 and
// b0..b1, each taken at five points - its ends and 1/5, 2/5 and 3/5 of the
// way along (a pen-up there taken as the start) - as the least from any
// point of the first to any of the second.  0x7fff and *flag 1 when a
// point of the first, or the second's point of the same number, is a
// pen-up.
long
CalcDistBetwXr(short* x, short* y, long a0, long a1, long b0, long b1, short* flag)
{
	struct { short x, y; } p[5], q[5];
	*flag = 0;
	p[0].x = x[a0]; p[0].y = y[a0];
	p[4].x = x[a1]; p[4].y = y[a1];
	for (long k = 1; k < 4; k++)
	{
		long i = LMul(k, a1 - a0) / 5 + a0;
		if (y[i] == -1)
			p[k] = p[0];
		else
		{
			p[k].x = x[i];
			p[k].y = y[i];
		}
	}
	q[0].x = x[b0]; q[0].y = y[b0];
	q[4].x = x[b1]; q[4].y = y[b1];
	for (long k = 1; k < 4; k++)
	{
		long i = LMul(k, b1 - b0) / 5 + b0;
		if (y[i] == -1)
			q[k] = q[0];
		else
		{
			q[k].x = x[i];
			q[k].y = y[i];
		}
	}
	long best = 0x7fff;
	for (long i = 0; i < 5; i++)
	{
		if (p[i].y == -1 || q[i].y == -1)
		{
			*flag = 1;
			return 0x7fff;
		}
		for (long j = 0; j < 5; j++)
		{
			long d = Distance8(p[i].x, p[i].y, q[j].x, q[j].y);
			if (d < best)
				best = d;
		}
	}
	return best;
}


// ROM 0x002b0ca0 del_close_MAX_MIN__FP8low_type
// (For printed writing, rc +0x94 = 0x10, when rc +4 is not 1.)  A top and
// the next top that are really one - a stroke's start (3) and a top (3 or
// 2, mark 1 or 0x10) just after it across a break or a low bottom, closer
// than 15 (25 when either is high) and within a third of how far apart
// the bottoms between them are - made one: the first taken out and the
// break between them with it, the second made a plain top.  Likewise a
// stroke's end (7) and the next end (7) across a break or a top: the
// second taken out (or the first, when the stroke before is not a
// start).  ==> 0.
long
del_close_MAX_MIN(low_type* low)
{
	short* x = low->fX;
	short* y = low->fY;
	SPEC_TYPE* e = low->fSpecl;
	if (e->next == nil)
		return 0;
	do
	{
		Boolean brk = false;
		SPEC_TYPE* r = e->next;
		SPEC_TYPE* first = r;
		UByte c = e->code;
		if (c == 3)
		{
			UByte mark = e->mark;
			if (mark != 0x10 && mark != 0x20)
				goto next;
			if ((e->other & 8) || (e->other & 0x20))
				goto next;
			while (r != nil && r->code != 3 && r->code != 2)
			{
				UByte rc = r->code;
				if (IsBreakCode(rc))
					brk = true;
				else
				{
					if (!(rc == 7 || rc == 0xd || rc == 0x10))
						goto next;
					if ((r->attr & 0xf) > 9)
						goto next;
				}
				r = r->next;
			}
			if (mark == 0x20)
			{
				SPEC_TYPE* p = e->prev;
				if (p->mark != 0x10)
					goto next;
				if (!(p->code == 7 || p->code == 0xc || p->code == 0xb))
					goto next;
			}
			if (r == nil || !(r->mark == 1 || r->mark == 0x10))
				goto next;
			SPEC_TYPE* after = r->next;
			if (after == nil)
				goto next;
			long lowGap = 0x7fff;
			Boolean bottom7 = first->code == 7;
			UByte rc = r->code;
			if (rc == 3 && (r->other & 0x20))
				goto next;
			if ((after->attr & 0xf) > 0xa)
				goto next;
			if (brk || (rc == 2 && (r->attr & 0x30) == 0x10))
			{
				if (bottom7 && (first->attr & 0xf) <= 7)
					goto next;
			}
			if (rc == 2 && (r->attr & 0x30) == 0x10 && x[r->iEnd] - x[r->iBeg] > 0xa)
				goto next;
			if (brk)
			{
				if (rc == 2)
					goto next;
				if (after->code != 7 && after->code != 8)
					goto next;
				long a = ixMin(e->iBeg, r->iBeg - 1, x, y);
				long b = ixMin(r->iBeg, after->iEnd, x, y);
				if (a == -1 || b == -1)
					goto next;
				if (x[b] < x[a])
				{
					if (after->mark == 0x20 && HWRAbs(CurvMeasure(x, y, r->iBeg, after->iEnd, -1)) > 5)
						goto next;
					SPEC_TYPE* nn;
					if (after->mark != 0x20 && (nn = after->next) != nil
					 && x[(r->iBeg + after->iBeg) >> 1] < x[(e->iBeg + first->iBeg) >> 1]
					 && HWRAbs(CurvMeasure(x, y, r->iBeg, nn->iEnd, -1)) > 5)
						goto next;
				}
				if (bottom7)
					lowGap = Distance8(x[first->ipoint0], y[first->ipoint0], x[after->ipoint0], y[after->ipoint0]);
			}
			long lim = ((e->attr & 0xf) > 2 && (r->attr & 0xf) > 2) ? 0xf : 0x19;
			long b0, b1;
			if (r->code != 2)
			{
				b0 = r->iBeg;
				b1 = r->iEnd;
			}
			else
				b0 = b1 = r->ipoint0;
			short flag;
			long d = CalcDistBetwXr(x, y, e->iBeg, e->iEnd, b0, b1, &flag);
			if (d >= lim)
				goto next;
			if ((lowGap + 1) / 3 <= d)
				goto next;
			SPEC_TYPE* q = r->prev;
			while (q->code == 0xd || q->code == 0x10)
				q = q->prev;
			SPEC_TYPE* s = e->prev;
			DelFromSPECLList(e);
			while (s->code == 0xd || s->code == 0x10)
			{
				s = s->prev;
				SwapThisAndNext(s->next);
			}
			if (IsBreakCode(q->code))
			{
				SPEC_TYPE* n = e->next;
				if (n->mark == 0x20)
					n->mark = 0x10;
				DelFromSPECLList(q);
				r->mark = 1;
				r->attr = (r->attr & ~0x30) | 0x10;
			}
		}
		else if (c == 7 && e->mark == 0x20 && e->prev->mark == 0x10 && !(e->other & 8) && !(e->other & 0x20))
		{
			SPEC_TYPE* start = e->prev;
			while (r != nil && r->code != 7)
			{
				UByte rc = r->code;
				if (!(IsBreakCode(rc) || rc == 3))
					goto next;
				r = r->next;
			}
			if (r == nil || r->mark != 0x20)
				goto next;
			if (r->other & 0x20)
				goto next;
			long gap = Distance8(x[start->ipoint0], y[start->ipoint0], x[r->prev->ipoint0], y[r->prev->ipoint0]);
			short flag;
			long d = CalcDistBetwXr(x, y, e->iBeg, e->iEnd, r->iBeg, r->iEnd, &flag);
			if (d >= 0xf)
				goto next;
			if ((gap + 1) / 3 <= d)
				goto next;
			SPEC_TYPE* p = r->prev;
			if (p->mark == 0x10)
			{
				p->mark = 0x20;
				DelFromSPECLList(r);
				SPEC_TYPE* n = e->next;
				if (IsBreakCode(n->code))
				{
					DelFromSPECLList(n);
					e->mark = 3;
					e->attr = (e->attr & ~0x30) | 0x20;
				}
			}
			else
				DelFromSPECLList(e);
		}
	next:
		e = e->next;
	}
	while (e->next != nil);
	return 0;
}


// ROM 0x002b12dc IsExclamationOrQuestionSign__FP8low_typeP9SPEC_TYPET2
// Whether the stroke a..b is the stem of an ! or ? - followed, as the last
// thing but one, by a break and a dot or late stroke - or lies wholly to
// one side of everything written before it.
long
IsExclamationOrQuestionSign(low_type* low, SPEC_TYPE* a, SPEC_TYPE* b)
{
	SPEC_TYPE* n = b->next;
	if (n != nil && IsBreakCode(n->code))
	{
		SPEC_TYPE* nn = n->next;
		if (nn != nil && (nn->code == 0xd || nn->code == 0x10) && nn->next == nil)
			return 1;
	}
	short xMin, xMax, xMinBefore, xMaxBefore;
	xMinMax(a->iBeg, b->iEnd, low->fX, low->fY, &xMin, &xMax);
	xMinMax(0, a->iBeg - 1, low->fX, low->fY, &xMinBefore, &xMaxBefore);
	if (xMinBefore > xMax || xMaxBefore < xMin)
		return 1;
	return 0;
}


// ROM 0x002b13dc del_ZZ_HATCH__FP9SPEC_TYPE
// The crossings marked as a late stroke's (0xa) taken out, and of each
// break marked 0x44 with a break after it, one of the two: the 0x44 when
// it is a 0x13 or the next is a 0x14, otherwise the next.  ==> 0.
long
del_ZZ_HATCH(SPEC_TYPE* head)
{
	for (SPEC_TYPE* e = head; e != nil; e = e->next)
	{
		if (e->mark == 0xa)
		{
			DelCrossingFromSPECLList(e);
			e = e->next;
			// DEVIATION: a crossing at the very end leaves the ROM reading
			// the links of the element at address 0; the host stops
			if (e == nil)
				break;
		}
	}
	for (SPEC_TYPE* e = head; e != nil && e->next != nil; e = e->next)
	{
		SPEC_TYPE* n = e->next;
		if (e->mark != 0x44 || !IsBreakCode(n->code))
			continue;
		if (e->code == 0x13)
			DelFromSPECLList(e);
		else if (e->code == 0x14)
			DelFromSPECLList(n);
		else if (n->code != 0x14)
			DelFromSPECLList(n);
		else
			DelFromSPECLList(e);
	}
	return 0;
}


// ROM 0x002b19a8 insert_drop__FP9SPEC_TYPEP8low_type
// A break put after e: the element after it made one (0x14, other 1)
// when it is one already; otherwise a new 0x14 marked 0x44 inserted, two
// points long (less at the end of the trace).
//
// ROM QUIRK: the new break is not taken from the free elements
// (NewSPECLElem) but is simply the element after e in the array,
// whatever that was.
void
insert_drop(SPEC_TYPE* e, low_type* low)
{
	SPEC_TYPE* n = e->next;
	if (n == nil)
		return;
	if (IsBreakCode(n->code))
	{
		n->code = 0x14;
		e->next->other = 1;
		return;
	}
	SPEC_TYPE* t = e + 1;
	t->mark = 0x44;
	t->code = 0x14;
	t->attr = 7;
	t->other = 1;
	t->iBeg = e->iEnd;
	short end = e->iEnd;
	t->iEnd = (end + 2 >= low->fII) ? end : (short) (end + 2);
	Insert2ndAfter1st(e, t);
}


// ROM 0x002b1494 punctuation__FP8low_typeP9SPEC_TYPET2
// Whether a late stroke or dot e (placed after start) is punctuation of
// its own - a full stop, a comma, a dash, a leading or trailing mark -
// rather than part of a letter; a break is put after one that is
// (insert_drop).  An apostrophe is first offered to RestoreApostroph.
// ==> 1 punctuation, 0 not.
//
// ROM QUIRKS: a late stroke at the very end compares its whole attr byte,
// band bits and all, with 5; and the last test works out a slant shift
// and the trace's right edge only to throw both away.
long
punctuation(low_type* low, SPEC_TYPE* start, SPEC_TYPE* e)
{
	rc_type* rc = low->rc;
	short* x = low->fX;
	short* y = low->fY;
	long iBeg = e->iBeg;
	long iEnd = e->iEnd;
	UByte code = e->code;
	if (code == 0x10 && (e->other & 2))
		return 0;
	UShort flags = RCGetH(rc, 0x90);
	if (flags & 1)
		return 0;
	UShort kind = RCGetH(rc, 0x94);
	if (!(flags & 0x8000))
	{
		if (kind != 0x20)
		{
			if (!(RCGetH(rc, 6) & 2) && code == 0x10 && e->ipoint1 == 0)
				return RestoreApostroph(low, e);
			return 0;
		}
	}
	else if (kind != 0x20)
	{
		if (!(RCGetH(rc, 6) & 2) && code == 0x10 && e->ipoint1 == 0
		 && RestoreApostroph(low, e) != 0)
			return 1;
		goto measure;
	}
	{
		short count = 0;
		for (SPEC_TYPE* r = low->fSpecl; r != nil; r = r->next)
			count++;
		if (count < 4)
			return 0;
	}
measure:
	SPEC_TYPE* p = e->prev;
	SPEC_TYPE* n = e->next;
	short xMin, xMax;
	xMinMax(0, low->fII - 1, x, y, &xMin, &xMax);
	if ((x[iBeg] == xMin || x[iEnd] == xMin) && p->prev == nil && n != nil)
	{
		// the first thing written, at the left edge
		if (e->code == 0x10)
		{
			if (n->code == 0x10)
				return 1;
			if (n->code != 0xd)
			{
				insert_drop(e, low);
				return 1;
			}
			if (n->next == nil)
				return 1;
			xMinMax(n->iEnd + 1, low->fII - 1, x, y, &xMin, &xMax);
			if (x[n->iEnd] <= xMin)
				return 1;
			insert_drop(e, low);
			return 1;
		}
		if (e->code == 0xd && n->mark != 0xa && (e->attr & 0xf) <= 6)
		{
			xMinMax(iEnd + 1, low->fII - 1, x, y, &xMin, &xMax);
			if (x[(iBeg + 2 * iEnd) / 3] <= xMin && (e->attr & 0xf) > 5)
			{
				insert_drop(e, low);
				return 1;
			}
		}
	}
	_RECT box;
	GetTraceBox(x, y, iBeg, iEnd, &box);
	long midX = (box.right + box.left) >> 1;
	long h = e->attr & 0xf;
	UByte attr = e->attr;
	if (e->code == 0x10)
	{
		if (h >= 7)
		{
			insert_drop(e, low);
			return 1;
		}
		if (h > 4)
			goto last;
		if (n != nil && !(n->code == 0x10 && n->next == nil))
			goto last;
		if (e->prev == nil || iBeg == 1)
			return 1;
		xMinMax(0, iBeg - 1, x, y, &xMin, &xMax);
		if (x[iBeg] > xMax && x[iEnd] > xMax)
			return 1;
		return 0;
	}
	if (e->code == 0xd)
	{
		if (n != nil)
			goto notLast;
		if (x[iEnd] == xMax || x[iBeg] == xMax)
		{
			if (attr > 5 && HWRAbs(x[iBeg] - x[iEnd]) <= 0x1e)
			{
				e->code = 0x10;
				return 1;
			}
			if (h > 5 && h <= 9)
			{
				if (e->prev == nil || iBeg == 1)
					return 1;
				xMinMax(0, iBeg - 1, x, y, &xMin, &xMax);
				if (midX - 0xa > xMax)
					return 1;
			}
			if (e->code == 0xd)
				goto notLast;
			goto crossed;
		}
		goto crossing;
	}
	goto crossed;
notLast:
	if (n != nil && n->mark == 0xa)
		goto crossedLate;
crossing:
	{
		UByte flag = 0;
		if (is_X_crossing_XT(e, low, &flag) == 0)
		{
			if (flag == 0)
				return 1;
			insert_drop(e, low);
			return 1;
		}
	}
crossed:
	if (e->code != 0xd)
		goto last;
crossedLate:
	if (n != nil && n->mark == 0xa)
		return 0;
last:
	(void) SlopeShiftDx((short) (0x27be - ((box.top + box.bottom) >> 1)), low->fSlope);
	return low->fSpecl == start ? 1 : 0;
}


// ROM 0x002b1fcc IsStick__FP9SPEC_TYPET1
// Whether a..b is a whole stroke of two sticks or arcs: a start then an
// end.
long
IsStick(SPEC_TYPE* a, SPEC_TYPE* b)
{
	if (a == nil || a->mark != 0x10)
		return 0;
	UByte c = a->code;
	if (!(c == 3 || c == 7 || c == 0xa || c == 9 || c == 0xc || c == 0xb))
		return 0;
	if (b == nil || b->mark != 0x20)
		return 0;
	c = b->code;
	return (c == 3 || c == 7 || c == 0xa || c == 9 || c == 0xc || c == 0xb) ? 1 : 0;
}


// ROM 0x002b1eb0 PutLeadingQuotes__FP8low_typeP9SPEC_TYPET2
// The two strokes of an opening quote made dots marked other 9, moved to
// the front and a break put after them.
void
PutLeadingQuotes(low_type* low, SPEC_TYPE* a, SPEC_TYPE* b)
{
	SPEC_TYPE* head = low->fSpecl;
	a->other |= 9;
	b->other |= 9;
	b->code = 0x10;
	a->code = 0x10;
	Move2ndAfter1st(head, a);
	Move2ndAfter1st(a, b);
	insert_drop(b, low);
}


// ROM 0x002b1f14 PutTrailingQuotes__FP8low_typeP9SPEC_TYPE
// The two strokes of a closing quote (a and the one after it) made dots
// marked other 9 and moved to the end after a break; a break left at the
// end before them taken out.
void
PutTrailingQuotes(low_type* low, SPEC_TYPE* a)
{
	SPEC_TYPE* last = low->fSpecl;
	SPEC_TYPE* b = a->next;
	while (last->next != nil)
		last = last->next;
	a->other |= 9;
	b->other |= 9;
	b->code = 0x10;
	a->code = 0x10;
	if (last != b)
	{
		Move2ndAfter1st(last, a);
		Move2ndAfter1st(a, b);
	}
	else
		last = a->prev;
	insert_drop(a, low);
	Move2ndAfter1st(last, a->next);
	if (IsBreakCode(last->code))
		DelFromSPECLList(last);
}


// Whether the stroke from a point to another lies wholly to the left (1)
// or wholly to the right (2) of what is written before and after it, or
// neither (0): the three spans FindQuotes compares.
static long
QuoteSide(low_type* low, long iBeg, long iEnd)
{
	short* x = low->fX;
	short* y = low->fY;
	short qMin, qMax, aMin, aMax, bMin, bMax;
	xMinMax(iBeg, iEnd, x, y, &qMin, &qMax);
	xMinMax(iEnd + 1, low->fII - 1, x, y, &aMin, &aMax);
	xMinMax(1, iBeg - 1, x, y, &bMin, &bMax);
	if (qMax < bMin && qMax < aMin)
		return 1;
	if (qMin > bMax && qMin > aMax)
		return 2;
	return 0;
}


// ROM 0x002b1a48 FindQuotes__FP9SPEC_TYPEP8low_type
// Whether a small late stroke or dot e (band 6 at most) is one half of a
// quote: with a small late stroke or dot after it, with a small stick
// stroke after it (made one element), or after a small stick stroke and
// a break before it (the two made one), when the pair lies wholly left of
// everything else (an opening quote: PutLeadingQuotes) or wholly right of
// it (a closing one: PutTrailingQuotes).  ==> 1 when it was.
long
FindQuotes(SPEC_TYPE* e, low_type* low)
{
	if ((e->attr & 0xf) > 6)
		return 0;
	SPEC_TYPE* n = e->next;
	if (n != nil)
	{
		if ((n->code == 0xd || n->code == 0x10) && (n->attr & 0xf) <= 6 && !(n->other & 1)
		 && !(n->next != nil && n->next->mark == 0xa))
		{
			long side = QuoteSide(low, e->iBeg, n->iEnd);
			if (side == 1)
			{
				PutLeadingQuotes(low, e, n);
				return 1;
			}
			if (side == 2)
			{
				PutTrailingQuotes(low, e);
				return 1;
			}
		}
		SPEC_TYPE* nn = n->next;
		if (IsStick(n, nn) && (n->attr & 0xf) <= 6 && (nn->attr & 0xf) <= 6)
		{
			long side = QuoteSide(low, e->iBeg, nn->iEnd);
			if (side != 0)
			{
				n->iEnd = nn->iEnd;
				n->other = 0;
				DelFromSPECLList(nn);
				if (side == 1)
					PutLeadingQuotes(low, e, n);
				else
					PutTrailingQuotes(low, e);
				return 1;
			}
		}
	}
	SPEC_TYPE* p = e->prev;
	if (!IsBreakCode(p->code))
		return 0;
	SPEC_TYPE* end = p->prev;
	SPEC_TYPE* start = end->prev;
	if (!IsStick(start, end))
		return 0;
	if ((start->attr & 0xf) > 6 || (end->attr & 0xf) > 6)
		return 0;
	long side = QuoteSide(low, start->iBeg, e->iEnd);
	if (side == 0)
		return 0;
	start->iEnd = end->iEnd;
	start->other = 0;
	DelThisAndNextFromSPECLList(end);
	if (side == 1)
		PutLeadingQuotes(low, e, start);
	else
	{
		SwapThisAndNext(start);
		PutTrailingQuotes(low, e);
	}
	return 1;
}


// ROM 0x002b2040 is_X_crossing_XT__FP9SPEC_TYPEP8low_typePUc
// Whether a late stroke e crosses letters either side of it far enough -
// more than a quarter of its own width into what was written before it
// (from before a late stroke in front of it) or after it (past one after
// it) - to be a t's bar or an x's stroke rather than punctuation; it
// always is when it is high in the line (rc +0x90 bit 10, not for rc
// +0x94 = 0x20, band 2 or less), low down (under 0x2796), or followed by
// a crossing of its own.  *next is 1 when nothing late follows it.
long
is_X_crossing_XT(SPEC_TYPE* e, low_type* low, UByte* next)
{
	short* y = low->fY;
	short* x = low->fX;
	long a = x[e->iBeg];
	long b = x[e->iEnd];
	long before = 0, after = 0;
	short leftOver = 0, rightOver = 0;
	rc_type* rc = low->rc;
	if ((RCGetH(rc, 0x90) & 0x400) && RCGetH(rc, 0x94) != 0x20 && (e->attr & 0xf) <= 2)
		return 1;
	long xl = (a <= b) ? a : b;
	long xr = (a <= b) ? b : a;
	SPEC_TYPE* p = e->prev;
	short i;
	if (p->code == 0xd)
	{
		i = (short) ((UShort) p->iBeg - 1);
		p = p->prev;
	}
	else
		i = (short) (e->iBeg - 1);
	short xMin, xMax;
	if (p->prev != nil)
	{
		xMinMax(0, i, x, y, &xMin, &xMax);
		if (xl < xMax)
		{
			before = 1;
			leftOver = (short) (xMax - xl);
		}
	}
	*next = 0;
	SPEC_TYPE* n = e->next;
	if (n == nil)
		return before ? 1 : 0;
	short j;
	SPEC_TYPE* r;
	if (n->code == 0xd)
	{
		r = n->next;
		if (r != nil && r->mark == 0xa)
			return 1;
		j = (short) ((UShort) n->iEnd + 1);
	}
	else
	{
		j = (short) ((UShort) e->iEnd + 1);
		*next = 1;
		r = n;
	}
	if (r != nil)
	{
		xMinMax(j, low->fII - 1, x, y, &xMin, &xMax);
		if (xr > xMin)
		{
			after = 1;
			rightOver = (short) (xr - xMin);
		}
	}
	if (!before && !after)
		return 0;
	short yMin, yMax;
	yMinMax(e->iBeg, e->iEnd, y, &yMin, &yMax);
	if (yMin < 0x2796)
		return 1;
	long q = (short) (xr - xl);
	q = (q + 2) >> 2;
	if (before && !after)
		return leftOver > q ? 1 : 0;
	if (after && !before)
		return rightOver > q ? 1 : 0;
	return (leftOver <= q && rightOver <= q) ? 0 : 1;
}


// ROM 0x002b230c change_last_IU_height__FP8low_type
// In a word of more than ten elements, a last stroke end that is a low
// top (3, band under 5 and no band bits) after a bottom at the bottom,
// right of everything before it: given height band 5.
void
change_last_IU_height(low_type* low)
{
	short* x = low->fX;
	short* y = low->fY;
	SPEC_TYPE* e = low->fSpecl;
	short count = 0;
	while (e->next != nil)
	{
		count++;
		e = e->next;
	}
	if (e->mark == 0x44)
		e = e->prev;
	SPEC_TYPE* p = e->prev;
	if (count <= 10)
		return;
	if (!(e->mark == 0x20 && e->code == 3))
		return;
	UByte attr = e->attr;
	if ((attr & 0xf) >= 5)
		return;
	if (!(attr == (attr & 0xf) && p->code == 8 && (p->attr & 0x30) == 0x20))
		return;
	short xMin, xMax;
	xMinMax(0, e->iBeg - 1, x, y, &xMin, &xMax);
	if (x[e->iBeg] >= xMax && x[e->iEnd] >= xMax)
		e->attr = (e->attr & ~0xf) | 5;
}


// ROM 0x002b241c placement_XT_ST__FP8low_type
// Every late stroke and dot put where it belongs.  First each is readied:
// a late stroke's crossings (the 0xa pairs after it) that lie beyond it
// (or, for printed writing, outside the stroke of the letter before it)
// taken out, its points cleared and other kept only as 2 (cut); a dot's
// other kept as 2 or 0x40.  Then each not yet placed (other 1 marks the
// placed) is placed by the first of: Placement_XT_CUTTED (a cut late
// stroke), DoubleXT, Placement_XT_With_HATCH (a late stroke with its
// crossings), FindQuotes, Placement_XT_WO_HATCH_AND_ST.  The head is
// given band 6 and x[0] 0x7fff meanwhile as sentinels.  ==> 0.
long
placement_XT_ST(low_type* low)
{
	SPEC_TYPE* head = low->fSpecl;
	short* x = low->fX;
	head->attr = 6;
	x[0] = 0x7fff;
	for (SPEC_TYPE* e = head; e != nil; e = e->next)
	{
		UByte c = e->code;
		if ((c == 0xd || c == 0x10) && (e->other & 1))
			continue;
		if (c == 0xd)
		{
			SPEC_TYPE* n;
			if ((short) RCGetH(low->rc, 4) == 1)
			{
				short beg, end;
				n = e->next;
				GetStrokeWhichBelongsToRestricted(e, low, &beg, &end);
				while (n != nil && n->mark == 0xa)
				{
					if (n->iBeg > e->iEnd)
					{
						DelThisAndNextFromSPECLList(n);
						n = n->next->next;
					}
					else
					{
						n = n->next;
						if (n->iEnd < beg || n->iBeg > end)
							DelThisAndNextFromSPECLList(n->prev);
						n = n->next;
					}
				}
			}
			if (e->other & 2)
			{
				n = e->next;
				e->other = 2;
				while (n != nil && n->mark == 0xa)
				{
					if (n->iBeg > e->iEnd)
						DelThisAndNextFromSPECLList(n);
					n = n->next->next;
				}
			}
			else
				e->other = 0;
			e->ipoint0 = 0;
			e->ipoint1 = 0;
		}
		else if (c == 0x10)
		{
			if (e->other & 2)
				e->other = 2;
			else if (e->other & 0x40)
				e->other = 0x40;
			else
				e->other = 0;
			e->ipoint1 = 0;
		}
	}
	SPEC_TYPE* e = head;
	while (e != nil)
	{
		SPEC_TYPE* n = e->next;
		UByte c = e->code;
		if ((c == 0xd || c == 0x10) && !(e->other & 1))
		{
			e->other |= 1;
			Boolean placed = false;
			if (e->code == 0xd)
			{
				if (e->other & 2)
				{
					Placement_XT_CUTTED(e, low);
					placed = true;
				}
				else if (DoubleXT(e, low) != 0)
					placed = true;
			}
			if (!placed)
			{
				if (e->code == 0xd && n != nil && n->mark == 0xa)
					Placement_XT_With_HATCH(e, n, low);
				else
				{
					SPEC_TYPE* p = e->prev;
					if (FindQuotes(e, low) != 0)
						n = p->next;
					else
						Placement_XT_WO_HATCH_AND_ST(e, low);
				}
			}
		}
		e = n;
	}
	head->attr = 0;
	x[0] = 0;
	return 0;
}


// Whether a late stroke's crossing (its partner in the array holds the
// two points) is a late stroke that makes the break at e run through a
// letter: a late stroke (0xd, not cut or marked 8) whose crossing points
// are both set.
static inline Boolean
IsCrossingLate(SPEC_TYPE* r)
{
	return r->code == 0xd && !(r->other & 2) && !(r->other & 8)
		&& (r + 1)->ipoint0 != -2 && (r + 1)->ipoint1 != -2;
}


// ROM 0x002b26b8 make_different_breaks__FP8low_type
// Each break between strokes (0x12) weighed: its gap (GetDxBetweenStrokes,
// the pen-up's two strokes measured across) kept in its ipoint0; a break
// that a late stroke's crossing spans, or too narrow for printed writing,
// made no break at all (1); a break before a stroke that sits higher than
// the first (SecondHigherFirst, from the first stroke) made 0x13; a gap
// over 5/12 of the step a space (0x14).  A dot ending the word decides
// whether the break before it is a space and stops the pass.  Then the
// breaks are compared with the others: a 0x12 under a quarter of the mean
// gap is no break, a space not wide enough against the other spaces (or
// the step) goes back to 0x12.  ==> 0.
long
make_different_breaks(low_type* low)
{
	SPEC_TYPE* head = low->fSpecl;
	short* x = low->fX;
	short* y = low->fY;
	long nBreaks = 0, sumDx = 0, sum14 = 0, n14 = 0;
	SPEC_TYPE* e;
	SPEC_TYPE* late = nil;
	if (low->fStep == 0 || (e = head->next) == nil)
		return 0;
	while (e != nil)
	{
		if (e->code != 0x12)
			goto next;
		{
			Boolean haveLate = false;
			e->ipoint0 = 0x7fff;
			SPEC_TYPE* before = e->prev;
			while (before->code == 0xd || before->code == 0x10)
			{
				if (IsCrossingLate(before))
				{
					haveLate = true;
					late = before;
				}
				before = before->prev;
			}
			if (before->code == 0)
				goto next;
			SPEC_TYPE* following = e->next;
			SPEC_TYPE* after = following;
			while (after != nil && (after->code == 0xd || after->code == 0x10))
			{
				if (after->code == 0xd)
				{
					if (IsCrossingLate(after))
					{
						haveLate = true;
						late = after;
					}
				}
				else if (after->next == nil)
					break;
				after = after->next;
			}
			if (after == nil)
			{
				e = following;
				continue;
			}
			if (after->code == 0x10 && after->next == nil)
			{
				// a dot ending the word: a space when it is low, beside the
				// last low point before it, and further down than across
				if ((after->attr & 0xf) <= 7)
					break;
				long i = (IsBreakCode(before->code) || before == head) ? e->iBeg : before->iEnd;
				long k = brk_left(y, i, 0) + 1;
				long j = iYdown_range(y, k, i);
				if (j == 0x7fff)
					break;
				long dx = x[after->iBeg] - x[j];
				long step = low->fStep <= 0x14 ? 0x14 : low->fStep;
				if (dx >= step)
					break;
				if (y[after->iBeg] <= y[j])
					break;
				long dy = y[after->iBeg] - y[j];
				if (dy + (dy + 1) / 3 > dx)
					e->code = 1;
				break;
			}
			if (!haveLate)
			{
				SPEC_TYPE* q = before;
				while (q != nil && !(IsBreakCode(q->code) || q->code == 0xd))
					q = q->prev;
				if (q != nil && !IsBreakCode(q->code) && IsCrossingLate(q))
				{
					haveLate = true;
					late = q;
				}
			}
			long a1 = before->iEnd;
			long a0 = a1;
			do
				a0--;
			while (y[a0] != -1);
			a0++;
			long b0 = after->iBeg;
			long b1 = b0;
			do
				b1++;
			while (y[b1] != -1);
			b1--;
			long from = b0;
			if ((RCGetH(low->rc, 0x90) & 0x400) && (after->code == 0xb || after->code == 7) && (after->attr & 0xf) > 7)
			{
				SPEC_TYPE* q = after->next;
				while (q != nil && (q->code == 0xd || q->code == 0x10))
					q = q->next;
				if (q != nil && !IsBreakCode(q->code) && q->mark != 0x20)
				{
					from = after->iEnd;
					while (y[from] != -1 && y[from] > 0x2796 && from <= q->iBeg)
						from++;
					from--;
				}
			}
			long dx = GetDxBetweenStrokes(low, a0, a1, from, b1);
			if (haveLate)
			{
				long p = (late + 1)->ipoint0;
				long q = (late + 1)->ipoint1;
				Boolean inA0 = p <= a1 && p >= a0;
				Boolean inB0 = p <= b1 && p >= b0;
				Boolean inA1 = q <= a1 && q >= a0;
				Boolean inB1 = q <= b1 && q >= b0;
				if ((inA0 && inB1) || (inB0 && inA1))
				{
					e->code = 1;
					goto next;
				}
			}
			if (a0 == 1 && SecondHigherFirst(low, e, before, after, a0, a1, b0, b1) != 0)
			{
				e->code = 0x13;
				e->other = 0x10;
				goto next;
			}
			if ((5 * low->fStep + 6) / 0xc < dx)
				e->code = 0x14;
			if ((RCGetH(low->rc, 0x90) & 0x400) && (low->fStep + 3) / 7 >= dx)
			{
				e->code = 1;
				e->other = 4;
				goto next;
			}
			UByte c = e->code;
			if (c != 0x12 && c != 0x14)
				goto next;
			e->ipoint0 = (short) dx;
			sumDx += dx;
			if (c == 0x14)
			{
				sum14 += dx;
				n14++;
			}
			nBreaks++;
		}
	next:
		e = e->next;
	}
	if (nBreaks < 1)
		return 0;
	for (e = head->next; e != nil; e = e->next)
	{
		long v = e->ipoint0;
		if (e->code == 0x12)
		{
			if (v == 0x7fff || nBreaks <= 1)
				continue;
			long m = (sumDx - v) / (nBreaks - 1);
			if (v <= (m + 2) >> 2)
			{
				e->code = 1;
				e->other = 4;
			}
		}
		else if (e->code == 0x14)
		{
			if (v == 0x7fff)
				continue;
			if ((e->other & 1) || (e->other & 8) || (e->other & 2))
				continue;
			if (n14 == 1)
			{
				if (v > (low->fStep >> 1))
					continue;
			}
			else if (n14 < 1)
				continue;
			else
			{
				long m = (sum14 - v) / (n14 - 1);
				if (v > (m >> 1))
				{
					if ((2 * m + 1) / 3 < v)
						continue;
					if (v > (low->fStep >> 1))
						continue;
				}
			}
			e->code = 0x12;
		}
	}
	return 0;
}


// ROM 0x00308050 GetTraceBoxInsideYZone__FPsT1iT3sT5P5_RECTN41
// The box of the points from iBeg to iEnd whose y lies from yTop to
// yBottom, and the points at its right, left, bottom and top (each moved
// to the middle of a run of equal values).  ==> 1; 0 (the points -1)
// when no point lies in the zone.
long
GetTraceBoxInsideYZone(short* x, short* y, long iBeg, long iEnd, short yTop, short yBottom, _RECT* box,
					   short* iRight, short* iLeft, short* iBottom, short* iTop)
{
	long xMax = 0, xMin = 0x7fff, yMax = 0, yMin = 0x7fff;
	*iTop = -1;
	*iBottom = -1;
	*iLeft = -1;
	*iRight = -1;
	for (long i = iBeg; i <= iEnd; i++)
	{
		long yi = y[i];
		if (yi == -1 || yi < yTop || yi > yBottom)
			continue;
		if (x[i] > xMax)
		{
			xMax = x[i];
			*iRight = (short) i;
		}
		if (x[i] < xMin)
		{
			xMin = x[i];
			*iLeft = (short) i;
		}
		if (y[i] > yMax)
		{
			yMax = y[i];
			*iBottom = (short) i;
		}
		if (y[i] < yMin)
		{
			yMin = y[i];
			*iTop = (short) i;
		}
	}
	box->left = (short) xMin;
	box->right = (short) xMax;
	box->top = (short) yMin;
	box->bottom = (short) yMax;
	if (xMin == 0x7fff || xMax == 0 || yMin == 0x7fff || yMax == 0)
		return 0;
	*iRight = (short) iMidPointPlato(*iRight, iEnd, x, y);
	*iLeft = (short) iMidPointPlato(*iLeft, iEnd, x, y);
	*iBottom = (short) iMidPointPlato(*iBottom, iEnd, y, y);
	*iTop = (short) iMidPointPlato(*iTop, iEnd, y, y);
	return 1;
}


// The three bands of the line GetDxBetweenStrokes measures the two strokes
// in (the rescaled trace's tops, middles and bottoms).
static const short kDxZones[4] = { 0x2796, 0x27b1, 0x27cb, 0x27e6 };

// A stroke's right (right) or left edge within a band, the slant taken
// out; false when none of it is in the band.
static Boolean
EdgeInZone(low_type* low, long i, long j, long zone, Boolean right, long* edge)
{
	short* x = low->fX;
	short* y = low->fY;
	_RECT box;
	short iRight, iLeft, iBottom, iTop;
	short top = kDxZones[zone];
	short bottom = kDxZones[zone + 1];
	if (GetTraceBoxInsideYZone(x, y, i, j, top, bottom, &box, &iRight, &iLeft, &iBottom, &iTop) == 0)
		return false;
	short k = right ? iRight : iLeft;
	long shift = SlopeShiftDx((short) (0x27be - y[k]), low->fSlope);
	*edge = (right ? box.right : box.left) - shift;
	return true;
}


// ROM 0x002b2df0 GetDxBetweenStrokes__FP8low_typeiN32
// How far apart two strokes are across - the one from a0 to a1 on the
// left, the one from b0 to b1 on the right - with the slant taken out: the
// left one's right edge and the right one's left edge are each measured in
// the three bands of the line (and the two furthest kept), and the gap is
// the one between the bands that face each other, or an average of the
// nearest two when the strokes reach into different bands.
long
GetDxBetweenStrokes(low_type* low, long a0, long a1, long b0, long b1)
{
	short* x = low->fX;
	short* y = low->fY;
	long slope = low->fSlope;
	long r;
	// the left stroke: its furthest right (r6) and the next (r8)
	long right1 = 0, right2 = 0;
	long leftLow = 0, leftHigh = 0, leftOne = 0;
	if (EdgeInZone(low, a0, a1, 0, true, &r))
	{
		right1 = r;
		leftLow = 0;
		leftHigh = 1;
	}
	if (EdgeInZone(low, a0, a1, 1, true, &r))
	{
		if (r <= right1)
			right2 = r;
		else
		{
			right2 = right1;
			right1 = r;
			leftLow = 0;
			leftHigh = 0;
		}
	}
	if (EdgeInZone(low, a0, a1, 2, true, &r))
	{
		if (r > right1)
		{
			right2 = right1;
			right1 = r;
			leftLow = 1;
			leftHigh = 0;
		}
		else if (r > right2)
			right2 = r;
	}
	if (right1 == 0)
	{
		leftOne = 1;
		if (y[a0] - 0x2780 >= 0x16 && y[a0] - 0x2780 > 0x16)
		{
			leftHigh = 0;
			leftLow = 1;
		}
		else
		{
			leftHigh = 1;
			leftLow = 0;
		}
		long i = (short) ixMax(a0, a1, x, y);
		if (i != -1)
			right1 = x[i] - SlopeShiftDx((short) (0x27be - y[i]), slope);
		else
		{
			_RECT box;
			GetTraceBox(x, y, a0, a1, &box);
			right1 = box.right;
		}
	}
	else
		leftOne = (right2 == 0) ? 1 : 0;
	// the right stroke: its furthest left (r5) and the next (r10)
	long left1 = 0x7fff, left2 = 0x7fff;
	long rightLow = 0, rightHigh = 0, rightOne = 0;
	if (EdgeInZone(low, b0, b1, 0, false, &r))
	{
		left1 = r;
		rightLow = 0;
		rightHigh = 1;
	}
	if (EdgeInZone(low, b0, b1, 1, false, &r))
	{
		if (r >= left1)
			left2 = r;
		else
		{
			left2 = left1;
			left1 = r;
			rightLow = 0;
			rightHigh = 0;
		}
	}
	if (EdgeInZone(low, b0, b1, 2, false, &r))
	{
		if (r < left1)
		{
			left2 = left1;
			left1 = r;
			rightLow = 1;
			rightHigh = 0;
		}
		else if (r < left2)
			left2 = r;
	}
	if (left1 == 0x7fff)
	{
		rightOne = 1;
		if (y[b0] - 0x2780 >= 0x16 && y[b0] - 0x2780 > 0x16)
		{
			rightHigh = 0;
			rightLow = 1;
		}
		else
		{
			rightHigh = 1;
			rightLow = 0;
		}
		long i;
		if (low->fII - 2 == b1 && rightLow)
			i = b0;
		else
			i = ixMin(b0, b1, x, y);
		i = (short) i;
		if (i != -1)
			left1 = x[i] - SlopeShiftDx((short) (0x27be - y[i]), slope);
		else
		{
			_RECT box;
			GetTraceBox(x, y, b0, b1, &box);
			left1 = box.left;
		}
	}
	else
		rightOne = (left2 == 0x7fff) ? 1 : 0;
	if (leftOne && rightOne)
		return left1 - right1;
	if (leftHigh)
	{
		if (rightLow && leftOne)
			return left2 - right1;
		if (rightLow && rightOne)
			return left1 - right2;
		if (rightLow)
		{
			long m = (left1 - right2 < left2 - right1) ? left1 - right2 : left2 - right1;
			return (m + (left1 - right1)) >> 1;
		}
	}
	if (!leftLow)
		return left1 - right1;
	if (rightHigh && leftOne)
		return left2 - right1;
	if (rightHigh && rightOne)
		return left1 - right2;
	if (rightHigh)
	{
		long m = (left1 - right2 < left2 - right1) ? left1 - right2 : left2 - right1;
		return (m + (left1 - right1)) >> 1;
	}
	return left1 - right1;
}


// ROM 0x002b348c SecondHigherFirst__FP8low_typeP9SPEC_TYPEN22iN35
// Whether the stroke b0..b1 (whose elements run on to b) is a high
// stroke - a t's bar or the like, written across the top of the fairly
// straight stroke a0..a1 (whose elements run from a to the break brk) to
// its left and reaching past it on the right - that belongs with it: some
// point of it above the first stroke's top shares an x with it.
//
// DEVIATION: when no point of the second stroke is as high as the first's
// top, the ROM compares an unset stack word with a register left over
// from its caller; the host answers no.
long
SecondHigherFirst(low_type* low, SPEC_TYPE* brk, SPEC_TYPE* a, SPEC_TYPE* b, long a0, long a1, long b0, long b1)
{
	short* x = low->fX;
	short* y = low->fY;
	_RECT boxA, boxB;
	GetTraceBox(x, y, a0, a1, &boxA);
	GetTraceBox(x, y, b0, b1, &boxB);
	if (HeightInLine(boxB.top, low) > 4)
		return 0;
	if (boxA.right < boxB.left || boxB.right <= boxA.right || boxB.top > boxA.top)
		return 0;
	SPEC_TYPE* head = low->fSpecl;
	while (a != head && a->mark != 0x10)
		a = a->prev;
	if (a == head)
		return 0;
	while (b != nil && b->mark != 0x20)
		b = b->next;
	if (b == nil)
		return 0;
	long count = 0;
	if (a != brk)
	{
		do
		{
			UByte c = a->code;
			if (!(c == 3 || c == 7 || c == 9 || c == 2 || c == 0xb || c == 8))
				return 0;
			count++;
			a = a->next;
		}
		while (a != brk);
		if (count > 4)
			return 0;
	}
	count = 0;
	SPEC_TYPE* r = brk->next;
	SPEC_TYPE* stop = b->next;
	if (r != stop)
	{
		do
		{
			UByte c = r->code;
			if (c == 3 || c == 7 || c == 9 || c == 2 || c == 0xb || c == 8)
				count++;
			else if (!(c == 0x25 || c == 0x26 || c == 0xe || c == 0x11 || c == 0x28 || c == 0x29))
				return 0;
			r = r->next;
		}
		while (r != stop);
		if (count > 6)
			return 0;
	}
	if (HWRAbs(CurvMeasure(x, y, a0, a1, -1)) >= 5)
		return 0;
	if (HWRAbs(CurvMeasure(x, y, b0, b1, -1)) < 10)
		return 0;
	long first = 0, last = 0;
	Boolean found = false;
	for (long i = b0; i <= b1; i++)
	{
		if (y[i] <= boxA.top)
		{
			if (!found)
			{
				first = i;
				found = true;
			}
			last = i;
		}
	}
	if (!found || first > last)
		return 0;
	for (long i = first; i <= last; i++)
		for (long j = a0; j <= a1; j++)
			if (x[j] == x[i])
				return 1;
	return 0;
}


// ROM 0x002b377c AdjustZZ_BegEnd__FP8low_type
// Each break between two letters stretched over the pen-up it stands for:
// from the end of the stroke before it (past the late strokes and dots in
// front of it) to the start of the stroke after it.  ==> 0.
long
AdjustZZ_BegEnd(low_type* low)
{
	short* y = low->fY;
	SPEC_TYPE* head = low->fSpecl;
	for (SPEC_TYPE* e = head->next; e != nil; e = e->next)
	{
		SPEC_TYPE* p = e->prev;
		SPEC_TYPE* n = e->next;
		if (p == head || n == nil || !IsBreakCode(e->code))
			continue;
		while (p != nil && p != head && (p->code == 0xd || p->code == 0x10))
		{
			SPEC_TYPE* q = p->prev;
			if (q == nil || q == head || IsBreakCode(q->code))
				break;
			p = q;
		}
		while (n->code == 0xd || n->code == 0x10)
		{
			SPEC_TYPE* q = n->next;
			if (q == nil || IsBreakCode(q->code))
				break;
			n = q;
		}
		long beg = e->iBeg;
		if (p != nil && p != head)
		{
			beg = p->iEnd;
			while (beg + 1 < low->fII && y[beg + 1] != -1)
				beg = (short) (beg + 1);
		}
		long end = n->iBeg;
		while (end > 0 && y[end - 1] != -1)
			end = (short) (end - 1);
		e->iBeg = (short) beg;
		e->iEnd = (short) end;
	}
	return 0;
}


// ROM 0x002b3900 redirect_sticks__FP8low_type
// (Not for rc +4 = 1.)  A stroke of a start going down (7) and an end
// going up (3) - a tick drawn upwards - that is steep (or barely bent) has
// its two codes and height bands exchanged, both marked other 0x20.
void
redirect_sticks(low_type* low)
{
	short* x = low->fX;
	short* y = low->fY;
	SPEC_TYPE* e = low->fSpecl;
	if (e->next == nil)
		return;
	do
	{
		Boolean a = e->mark == 0x10 && e->code == 7 && !(e->other & 1) && !(e->other & 8) && e->prev->mark != 0x10;
		SPEC_TYPE* n = e->next;
		Boolean b = n->mark == 0x20 && n->code == 3 && !(n->other & 8)
				 && (n->next == nil || n->next->mark != 0x20);
		if (a && b)
		{
			Boolean turn = HWRAbs(CurvMeasure(x, y, e->ipoint0, n->ipoint0, -1)) <= 2;
			if (!turn)
			{
				long dx = HWRAbs(x[n->ipoint0] - x[e->ipoint0]);
				long dy = HWRAbs(y[e->ipoint0] - y[n->ipoint0]);
				turn = dx < (dy + 1) / 3;
			}
			if (turn)
			{
				UByte h = e->attr & 0xf;
				e->code = 3;
				e->attr = (e->attr & ~0xf) | (n->attr & 0xf);
				n->code = 7;
				n->attr = (n->attr & ~0xf) | h;
				e->other |= 0x20;
				n->other |= 0x20;
			}
		}
		e = e->next;
	}
	while (e->next != nil);
}


// ROM 0x002b3ac0 find_angstrem__FP8low_type
// (With rc +6 bit 3.)  A small stroke high above the letter before it - its
// middle above 0x2796 and above that letter's top, its bottom above
// 0x27b1, no further right than that letter's right plus two thirds of
// the step, smaller than three steps across and down together - that
// crosses nothing and is no ! or ?: an angstrom's ring, made one dot
// element (0x10, other 0x42) over its whole stroke, the breaks marked 0x44
// beside it made plain breaks.  ==> 0.
long
find_angstrem(low_type* low)
{
	short* x = low->fX;
	short* y = low->fY;
	long step = low->fStep;
	long near = 2 * step + 1;
	long size = 3 * step;
	SPEC_TYPE* e = low->fSpecl->next;
	while (e != nil)
	{
		if (!(e->mark == 0x10 || e->prev->mark == 0x44) || e->code == 0x10)
		{
			e = e->next;
			continue;
		}
		SPEC_TYPE* r = e;
		while (r != nil && (r->code == 0xd || r->mark == 0xa || r->mark == 0x44))
			r = r->next;
		if (r == nil)
			return 0;
		SPEC_TYPE* start = r;
		short k = (short) (brk_left(y, r->iBeg, 0) + 1);
		SPEC_TYPE* last = r;
		while (r != nil && r->mark != 0x20 && r->mark != 0x44)
		{
			last = r;
			r = r->next;
		}
		if (r == nil || r->mark != 0x20)
			r = last;
		short end = (short) (brk_right(y, r->iEnd, low->fII - 1) - 1);
		SPEC_TYPE* p = e->prev;
		while (p->code == 0x10 || p->code == 0xd || p->mark == 0x44)
			p = p->prev;
		short kb = (short) (brk_left(y, p->iBeg - 2, 0) + 1);
		short ke = (short) ((UShort) p->iBeg - 2);
		if (kb >= 1)
		{
			_RECT before, box;
			GetTraceBox(x, y, kb, ke, &before);
			GetTraceBox(x, y, k, end, &box);
			short midX = (short) ((box.right + box.left) / 2);
			short midY = (short) ((box.bottom + box.top) / 2);
			SPEC_TYPE* cross;
			if (midY < 0x2796 && box.bottom < 0x27b1 && midY < before.top
			 && before.right + near / 3 > midX
			 && (box.bottom - box.top) + (box.right - box.left) < size
			 && IsExclamationOrQuestionSign(low, start, r) == 0
			 && find_CROSS(low, start->iBeg, r->iEnd, &cross) == 0)
			{
				start->code = 0x10;
				start->attr = (UByte) HeightInLine((short) ((box.top + box.bottom) >> 1), low);
				start->other = 0x42;
				start->mark = 0x10;
				start->iBeg = k;
				start->iEnd = end;
				start->next = r->next;
				if (r->next != nil)
					r->next->prev = start;
				if (e->prev->mark == 0x44)
					e->prev->code = 0x12;
				SPEC_TYPE* n = start->next;
				if (n != nil && n->mark == 0x44)
					n->code = 0x12;
				r = start;
			}
		}
		e = r->next;
	}
	return 0;
}


// ROM 0x002b3e4c CheckSequenceOfElements__FP8low_type
// Two elements of a kind side by side made one: of two sticks or ends in
// a crossing (3/3, 7/7) the longer kept; of two overlapping loops (4/4,
// 6/6) the first; a top (2 or 0x21) overlapping a loop 4 or 0x17 after it
// taken out; of two small loops 0x16 the second kept, 0x17 the first; a
// high loop 0x22 overlapping a small top loop 0x1b/0x1c kept, the other
// taken out.  ==> 0.
long
CheckSequenceOfElements(low_type* low)
{
	SPEC_TYPE* e = low->fSpecl;
	SPEC_TYPE* n;
	while (e != nil && (n = e->next) != nil)
	{
		Boolean overlap = e->iEnd >= n->iBeg && n->iEnd >= e->iBeg;
		int del = 0;					// 1 the first, 2 the second
		switch (e->code)
		{
		case 7:
		case 3:
			if (e->mark == 6 && n->code == e->code)
				del = (e->iEnd - e->iBeg <= n->iEnd - n->iBeg) ? 2 : 1;
			break;
		case 2:
		case 0x21:
			if ((n->code == 4 || n->code == 0x17) && overlap)
				del = 1;
			break;
		case 4:
			if (n->code == 4 && overlap)
				del = 2;
			break;
		case 6:
			if (n->code == 6 && overlap)
				del = 2;
			break;
		case 0x16:
			if (n->code == 0x16)
				del = 1;
			break;
		case 0x17:
			if (n->code == 0x17)
				del = 2;
			break;
		case 0x22:
			if ((e->attr & 0x30) == 0x10 && (n->code == 0x1c || n->code == 0x1b) && overlap)
				del = 2;
			break;
		}
		if (del == 1)
			DelFromSPECLList(e);
		else if (del == 2)
		{
			DelFromSPECLList(n);
			e = n;
		}
		e = e->next;
	}
	return 0;
}


// ROM 0x0030769c iClosestToY__FPsiT2s
// The point from i to j (neither end a pen-up) whose y is nearest v, the
// first of equals; -1 for none.
long
iClosestToY(short* y, long i, long j, short v)
{
	if (i > j || y[i] == -1 || y[j] == -1)
		return -1;
	short best = (short) HWRAbs(y[i] - v);
	long k = i;
	for (long m = i + 1; m <= j; m++)
	{
		if (y[m] == -1)
			continue;
		short d = (short) HWRAbs(y[m] - v);
		if (d < best)
		{
			best = d;
			k = m;
		}
	}
	return k;
}


// ROM 0x002b4fbc Put_XT_ST__FP8low_typeP9SPEC_TYPET2Ui
// The late stroke or dot e put after the element found for it, unless it
// is punctuation of its own.  ==> 0.
long
Put_XT_ST(low_type* low, SPEC_TYPE* best, SPEC_TYPE* e, ULong found)
{
	if (best == nil)
		return 0;
	if (punctuation(low, best, e) != 0)
		return 0;
	if (low->fSpecl != best && found != 0 && best->prev != e)
		Move2ndAfter1st(best->prev, e);
	return 0;
}


// ROM 0x002b4634 FindClosestUpperElement__FP9SPEC_TYPEs
// The nearest upper element that starts at or before point i: back from
// the first letter element that starts after it.  ==> head for none.
SPEC_TYPE*
FindClosestUpperElement(SPEC_TYPE* head, short i)
{
	SPEC_TYPE* r = head->next;
	while (r != nil && (r->code == 0xd || r->code == 0x10 || r->mark == 0xa || i >= r->iBeg))
		r = r->next;
	if (r == nil)
		return head;
	while (r != head)
	{
		r = r->prev;
		if (IsUpperElem(r) && i >= r->iBeg)
			return r;
	}
	return r;
}


// ROM 0x002b44e8 DoubleXT__FP9SPEC_TYPEP8low_type
// A late stroke that crosses two letters (its crossing partner's two
// points both set) - a bar across two t's - is doubled: the copy (the
// element two after it in the array, marked other 8) goes before the
// letter it crosses further off, the stroke itself before the nearer one.
// ==> 1 when it was.
//
// DEVIATION: the ROM copies the 0x14 bytes of its SPEC_TYPE; the host
// copies its own (bigger) one.
long
DoubleXT(SPEC_TYPE* e, low_type* low)
{
	SPEC_TYPE* partner = e + 1;
	short* x = low->fX;
	if (partner->ipoint0 == -2 || partner->ipoint1 == -2)
		return 0;
	SPEC_TYPE* head = low->fSpecl;
	long mid = (e->iBeg + e->iEnd) >> 1;
	long d0 = HWRAbs(x[mid] - x[partner->ipoint0]);
	long d1 = HWRAbs(x[mid] - x[partner->ipoint1]);
	short nearPt, farPt;
	if (d0 <= d1)
	{
		nearPt = partner->ipoint0;
		farPt = partner->ipoint1;
	}
	else
	{
		nearPt = partner->ipoint1;
		farPt = partner->ipoint0;
	}
	SPEC_TYPE* a = FindClosestUpperElement(head, nearPt);
	if (a == head)
		return 0;
	SPEC_TYPE* b = FindClosestUpperElement(head, farPt);
	if (b == head)
		return 0;
	SPEC_TYPE* copy = e + 2;
	e->other |= 4;
	memcpy(copy, e, sizeof(SPEC_TYPE));
	Insert2ndAfter1st(e, copy);
	Move2ndAfter1st(a->prev, e);
	Move2ndAfter1st(b->prev, copy);
	copy->other |= 8;
	return 1;
}


// ROM 0x002b4060 Placement_XT_With_HATCH__FP9SPEC_TYPET1P8low_type
// A late stroke with crossings of its own (the 0xa pairs from r): each
// crossing's half on the letter it crosses (its first two recorded as the
// stroke's points) is looked for among the letter elements of its stroke
// - the nearest before and after it - and an upper one chosen; the best
// over all the crossings (the one whose height band suits the stroke's
// and whose crossing is nearest the stroke's middle across) is where the
// stroke goes (Put_XT_ST).  ==> 0.
long
Placement_XT_With_HATCH(SPEC_TYPE* e, SPEC_TYPE* r, low_type* low)
{
	SPEC_TYPE* head = low->fSpecl;
	short* x = low->fX;
	short* y = low->fY;
	ULong found = 0;
	short count = 0;
	e->other |= 4;
	SPEC_TYPE* best = head;
	SPEC_TYPE* bestCross = head;
	while (r != nil && r->mark == 0xa)
	{
		Boolean second;
		if (r->iBeg <= e->iEnd)
		{
			r = r->next;
			second = true;
		}
		else
			second = false;
		count++;
		if (count == 1)
			e->ipoint0 = (short) ((r->iBeg + r->iEnd) >> 1);
		else if (count == 2)
			e->ipoint1 = (short) ((r->iBeg + r->iEnd) >> 1);
		else
		{
			e->ipoint1 = 0;
			e->ipoint0 = 0;
		}
		long s0 = r->iBeg;
		do
			s0--;
		while (y[s0] != -1);
		long s1 = r->iEnd;
		do
			s1++;
		while (y[s1] != -1);
		SPEC_TYPE* before = head;
		long nearest = 0x7fff;
		for (SPEC_TYPE* q = head; q != nil; q = q->next)
		{
			if (q->iBeg <= s0 || r->iBeg < q->iBeg)
				continue;
			if (IsBreakOrLate(q->code) || q->mark == 0xa || q->mark == 0xb || q->mark == 5)
				continue;
			long d = HWRAbs(r->iBeg - q->iBeg);
			if (d <= nearest)
			{
				before = q;
				nearest = d;
			}
			found = 1;
		}
		SPEC_TYPE* after = head;
		nearest = 0x7fff;
		for (SPEC_TYPE* q = head; q != nil; q = q->next)
		{
			if (!(q->iBeg < s1 && r->iBeg < q->iBeg))
				continue;
			if (IsBreakOrLate(q->code) || q->mark == 0xa || q->mark == 0xb || q->mark == 5)
				continue;
			long d = HWRAbs(r->iBeg - q->iBeg);
			if (d <= nearest)
			{
				after = q;
				nearest = d;
			}
			found = 1;
		}
		SPEC_TYPE* pick = head;
		if (IsUpperElem(before))
			pick = before;
		else if (IsLowerElem(before) || before == head)
		{
			if (IsUpperElem(after))
				pick = after;
			else if (after == head)
				pick = before->prev;
			else if (IsLowerElem(after) && after->next != nil)
				pick = after->next;
			else if (after->code == 0x1f || after->code == 0x20)
				pick = after;
		}
		else if (before->code == 0x1f || before->code == 0x20)
		{
			if (IsUpperElem(after))
				pick = after;
			else if (IsLowerElem(after) || after == head)
				pick = before;
			else if (after->code == 0x1f || after->code == 0x20)
				pick = ((before->attr & 0xf) >= (after->attr & 0xf)) ? after : before;
		}
		Boolean take;
		if (best != head && best != nil)
		{
			if (pick == nil)
				break;
			long xe = x[(e->iBeg + e->iEnd) >> 1];
			long hp = pick->attr & 0xf;
			long hb = best->attr & 0xf;
			if (hb >= 4 && hp <= 2)
				take = true;
			else if ((hb <= 3 && hp <= 3) || (hb >= 3 && hb <= 7 && hp >= 3 && hp <= 7))
			{
				long db = HWRAbs(x[(bestCross->iBeg + bestCross->iEnd) >> 1] - xe);
				long dr = HWRAbs(x[(r->iBeg + r->iEnd) >> 1] - xe);
				take = db >= dr;
			}
			else
				take = false;
		}
		else
			take = true;
		if (take)
		{
			bestCross = r;
			best = pick;
		}
		if (!second)
			r = r->next;
		r = r->next;
	}
	Put_XT_ST(low, best, e, found);
	return 0;
}


// ROM 0x002b46cc Placement_XT_WO_HATCH_AND_ST__FP9SPEC_TYPEP8low_type
// A late stroke or dot with no crossings of its own: put after the letter
// element nearest it across (the slant taken out for a dot), of the kinds
// that take one - for a dot an upper element (a stick with an arc) below
// it, for a bar one of the tops and loops it crosses, for printed writing
// (rc +6) only one it lies past - charged for lying lower than a long bar;
// a dot low beside an upper element whose side bends back further left
// than it is taken as belonging to nothing.  Two dots side by side (rc +6
// bits 1 and 3) are first spread apart by the step.  ==> 0.
//
// ROM QUIRK: one flag (the one at +0xc in its frame) is nought and never
// set, so the tests on it always go the same way and the move after the
// search never happens.
long
Placement_XT_WO_HATCH_AND_ST(SPEC_TYPE* e, low_type* low)
{
	SPEC_TYPE* head = low->fSpecl;
	short* y = low->fY;
	short* x = low->fX;
	rc_type* rc = low->rc;
	ULong found = 0;
	long slope = low->fSlope;
	const long never = 0;
	long mid = (e->iBeg + e->iEnd) >> 1;
	long xe = x[mid];
	if (e->code == 0x10)
	{
		if (RCGetH(rc, 6) & 0xa)
		{
			SPEC_TYPE* n = e->next;
			if (n != nil && n->code == 0x10 && !(n->other & 1))
			{
				long step = low->fStep;
				short dx = (short) (xe - (UShort) x[(n->iBeg + n->iEnd) >> 1]);
				short sign = dx < 0 ? -1 : 1;
				long shift;
				if (HWRAbs(dx) <= step >> 1)
					shift = -(dx >> 1);
				else if (HWRAbs(dx) >= step + (step >> 1))
					shift = 0;
				else if (HWRAbs(dx) <= step - ((step + 2) >> 2))
					shift = LMul(step >> 1, -sign);
				else
					shift = LMul((HWRAbs(dx) + 1) / 3 - (step >> 1), sign);
				shift = (short) shift;
				e->ipoint1 = (short) ((UShort) e->ipoint1 + shift);
				xe += e->ipoint1;
				n->ipoint1 = (short) -shift;
			}
			else
				xe += e->ipoint1;
		}
		xe -= SlopeShiftDx((short) (0x27be - y[mid]), slope);
	}
	SPEC_TYPE* best = head;
	long bestScore = 0x7fff;
	SPEC_TYPE* r = head->next;
	short beg = 0, end = 0;
	if ((short) RCGetH(rc, 4) == 1)
		GetStrokeWhichBelongsToRestricted(e, low, &beg, &end);
	if (r->next == nil)
		goto put;
	do
	{
		long flagA = 0, flagB = 0;
		long h = r->attr & 0xf;
		UByte c = r->code;
		if (IsBreakOrLate(c) || r->mark == 0xa)
			goto next;
		if (never == 0 && IsBreakCode(r->next->code))
		{
			if (h > 5 || (h == 5 && e->code == 0x10))
				goto next;
		}
		if ((short) RCGetH(rc, 4) == 1 && (r->iEnd < beg || r->iBeg > end))
			goto next;
		{
			long m;
			if (c == 3 && (r->other & 0x20))
				m = (r->next->iBeg + r->next->iEnd) >> 1;
			else
				m = (r->iBeg + r->iEnd) >> 1;
			long xm = x[m];
			if (e->code == 0x10)
				xm -= SlopeShiftDx((short) (0x27be - y[m]), slope);
			if (e->code == 0x10 && never == 0)
			{
				if (y[mid] < y[m])
					flagA = 1;
				else if (h <= 5 && r->prev != head && xm > xe)
				{
					if (c == 4)
						flagB = 1;
					else if (c == 3)
					{
						if (r->mark == 9)
						{
							if ((r->attr & 0x30) == 0x10)
								flagB = 1;
						}
						else if (r->mark == 0x10)
							flagB = 1;
					}
				}
			}
			short rc6 = (short) RCGetH(rc, 6);
			Boolean up = c == 4 || c == 3 || c == 0x1d || c == 2 || c == 0xa || c == 9 || c == 0x15 || c == 0x21 || c == 0x16
					  || ((rc6 & 0xe) && (c == 0x17 || c == 0x18));
			Boolean upForBar = c == 0xa || c == 2 || c == 9 || c == 4 || c == 3 || c == 0x21 || c == 0x1f || c == 0x1d
					  || ((rc6 & 0xe) && (c == 0x16 || c == 0x15 || c == 0x17 || c == 0x18))
					  || ((c == 0x20 || c == 0x15) && h < (e->attr & 0xf));
			Boolean down = c == 8 || c == 0xc || c == 0x22 || (c == 7 && r->mark == 0x20);
			Boolean take = false;
			if (e->code == 0x10)
			{
				if ((flagA || flagB) && up && h <= 9)
				{
					if (rc6 == 1 || ((rc6 & 0xe) && r->iEnd < e->iBeg))
						take = true;
				}
			}
			else if (e->code == 0xd)
			{
				if (upForBar && h <= 6)
				{
					if (rc6 == 1 || ((rc6 & 0xe) && r->iEnd < e->iBeg))
						take = true;
				}
			}
			if (!take && never != 0 && down && h >= 8 && r->iEnd < e->iBeg)
				take = true;
			if (take)
			{
				long penalty = 0;
				if (e->code == 0xd && HWRAbs(x[e->iBeg] - x[e->iEnd]) > 0x50)
				{
					short eMin, eMax, rMin, rMax;
					yMinMax(e->iBeg, e->iEnd, y, &eMin, &eMax);
					yMinMax(r->iBeg, r->iEnd, y, &rMin, &rMax);
					long d = rMin - eMax;
					if (d > 0)
						penalty = d;
				}
				long score = penalty + 2 * HWRAbs(xm - xe);
				if (score <= bestScore)
				{
					bestScore = score;
					SPEC_TYPE* p = r->prev;
					if (never == 0 && flagB && (p->code == 7 || p->code == 0xb) && p->prev == e)
						best = p;
					else
						best = r;
				}
				found = 1;
			}
		}
		if (e->code == 0x10 && never == 0 && flagA == 0 && IsUpperElem(r))
		{
			SPEC_TYPE* n = r->next;
			Boolean nextLow = n != nil && IsLowerElem(n) && (n->attr & 0xf) >= 7;
			Boolean prevLow = r->prev != head && IsLowerElem(r->prev) && (r->prev->attr & 0xf) >= 7;
			if (nextLow || prevLow)
			{
				SPEC_TYPE* q = nextLow ? r->next : r->prev;
				if (q != nil)
				{
					long i0 = nextLow ? (r->iBeg + r->iEnd) >> 1 : (r->prev->iBeg + r->prev->iEnd) >> 1;
					long i1 = nextLow ? (r->next->iBeg + r->next->iEnd) >> 1 : (r->iBeg + r->iEnd) >> 1;
					long k = iClosestToY(y, i0, i1, y[mid]);
					if (k != -1)
					{
						if (x[k] >= x[mid])
							goto put;
						bestScore = 0x7fff;
						best = head;
					}
				}
			}
		}
	next:
		r = r->next;
	}
	while (r->next != nil);
	if (never != 0 && best != nil)
	{
		Move2ndAfter1st(best, e);
		SPEC_TYPE* n = e->next;
		if (best->code == 8 && n != nil
		 && !(n->code == 3 && (n->next == nil || IsBreakCode(n->next->code))))
			insert_drop(e, low);
		return 0;
	}
put:
	Put_XT_ST(low, best, e, found);
	return 0;
}


// ROM 0x002d89b0 (unnamed) - after TEWorldClient::DispatchPacket
// Whether an element may stand beside an apostrophe: an upper element
// (not a stroke's end low down, band 7 or more), or a loop 0x1f/0x20 high
// up (band under 7).
static long
ApostropheNeighbour(SPEC_TYPE* e)
{
	if (IsUpperElem(e))
	{
		if (e->mark != 0x20)
			return 1;
		return (e->attr & 0xf) <= 6 ? 1 : 0;
	}
	if ((e->code == 0x20 || e->code == 0x1f) && (e->attr & 0xf) < 7)
		return 1;
	return 0;
}


// ROM 0x002d8ab4 (unnamed) - after TEWorldClient::DispatchPacket
// The first such neighbour after e; nil for none.
static SPEC_TYPE*
NextApostropheNeighbour(SPEC_TYPE* e)
{
	if (e->next == nil)
		return nil;
	do
	{
		e = e->next;
		if (ApostropheNeighbour(e))
			return e;
	}
	while (e->next != nil);
	return nil;
}


// ROM 0x002d8afc (unnamed) - after TEWorldClient::DispatchPacket
// The first such neighbour from e back; nil for none.
static SPEC_TYPE*
PrevApostropheNeighbour(SPEC_TYPE* e)
{
	for (; e != nil; e = e->prev)
		if (ApostropheNeighbour(e))
			return e;
	return nil;
}


// ROM 0x002d8a1c (unnamed) - after TEWorldClient::DispatchPacket
// e about to be moved out from between two breaks (or a break and an
// element): a break that ended where e began made to end where the
// element after e begins, one that began where e ended made to begin
// where the element before e ends.
static void
CloseUpAround(SPEC_TYPE* e)
{
	SPEC_TYPE* p = e->prev;
	SPEC_TYPE* n = e->next;
	if (p == nil || n == nil)
		return;
	if (IsBreakCode(p->code) && p->iEnd == e->iBeg)
		p->iEnd = n->iBeg;
	if (IsBreakCode(n->code) && n->iBeg == e->iEnd)
		n->iBeg = p->iEnd;
}


// ROM 0x002d94dc (unnamed) - in RestoreApostroph
// The letter element nearest a dot e across (its end, start or middle),
// then the place after the letter it belongs to - the last neighbour to
// the left of the dot, back to the break before, or on to the one after
// (*nearBreak 0 when a break was met first).  With move, the dot is put
// there between two breaks (0x14 marked 0x44, other 2), made where they
// are not.  ==> the element it goes after; nil for none.
static SPEC_TYPE*
PlaceApostrophe(low_type* low, SPEC_TYPE* e, long move, short* nearBreak)
{
	short* x = low->fX;
	short* y = low->fY;
	long xe = x[(e->iBeg + e->iEnd) >> 1];
	SPEC_TYPE* best = nil;
	long bestDist = 0x7fff;
	Boolean right = false;
	for (SPEC_TYPE* r = low->fSpecl->next; r != nil; r = r->next)
	{
		if (r == e || !ApostropheNeighbour(r))
			continue;
		long xi;
		if (r->prev == nil)
		{
			if (r->next == nil || y[r->iEnd] == -1)
				return nil;
			xi = x[r->iEnd];
		}
		else if (r->next == nil)
		{
			if (y[r->iBeg] == -1)
				return nil;
			xi = x[r->iBeg];
		}
		else
		{
			if (y[r->iBeg] == -1 || y[r->iEnd] == -1)
				return nil;
			if (IsBreakCode(r->code) && x[r->iBeg] >= x[r->iEnd])
				xi = x[r->iBeg];
			else
				xi = x[(r->iBeg + r->iEnd) >> 1];
		}
		short d = (short) (xi - xe);
		Boolean isRight = d > 0;
		if (d < 0)
			d = (short) -d;
		if (d < bestDist)
		{
			best = r;
			bestDist = d;
			right = isRight;
		}
	}
	if (best == nil)
		return nil;
	SPEC_TYPE* r = best;
	if (right && r->prev != nil)
		r = r->prev;
	*nearBreak = 1;
	for (;;)
	{
		if (ApostropheNeighbour(r) && x[(r->iBeg + r->iEnd) >> 1] < xe)
			break;
		if (r->prev != nil)
			r = r->prev;
		if (low->fSpecl == r || IsBreakCode(r->code))
		{
			*nearBreak = 0;
			goto found;
		}
		if (r == nil)
			goto found;
	}
	if (r->next != nil)
	{
		for (;;)
		{
			SPEC_TYPE* n = r->next;
			if (ApostropheNeighbour(n) && x[(n->iBeg + n->iEnd) >> 1] > xe)
				break;
			n = r->next;
			if (IsBreakCode(n->code))
			{
				*nearBreak = 0;
				break;
			}
			r = n;
			if (n->next == nil)
				break;
		}
	}
found:
	if (move == 0)
		return r;
	if (!IsBreakCode(r->code))
	{
		SPEC_TYPE* b = NewSPECLElem(low);
		if (b == nil)
			return nil;
		Insert2ndAfter1st(r, b);
		b->iEnd = r->iEnd;
		b->iBeg = r->iEnd;
		r = b;
	}
	r->code = 0x14;
	r->mark = 0x44;
	r->other = 2;
	r->attr = (r->attr & ~0xf) | 7;
	CloseUpAround(e);
	Move2ndAfter1st(r, e);
	if (e->iBeg > r->iBeg)
		r->iEnd = e->iBeg;
	SPEC_TYPE* n = e->next;
	if (n == nil)
		return r;
	if (!IsBreakCode(n->code))
	{
		SPEC_TYPE* b = NewSPECLElem(low);
		if (b == nil)
			return nil;
		Insert2ndAfter1st(e, b);
	}
	n = e->next;
	n->code = 0x14;
	n->mark = 0x44;
	n->other = 2;
	n->attr = (n->attr & ~0xf) | 7;
	n->iEnd = e->iEnd;
	n->iBeg = e->iEnd;
	return r;
}


// ROM 0x002d98c8 IsNearI__FP9SPEC_TYPE
// Whether e is the top of an i: a stick up (3, a top, start or crossing
// top) or an arc after a 0x13 break, followed (angles skipped) by a
// stroke's end coming down.
long
IsNearI(SPEC_TYPE* e)
{
	if (e == nil)
		return 0;
	if (e->code == 3)
	{
		if (!(e->mark == 1 || e->mark == 0x10 || e->mark == 9))
			return 0;
	}
	else if (e->code == 9)
	{
		if (e->prev == nil || e->prev->code != 0x13)
			return 0;
	}
	else
		return 0;
	SPEC_TYPE* r = SkipAnglesAfter(e);
	if (r != nil && (r->code == 7 || r->code == 0xc) && r->mark == 0x20)
		return 1;
	return 0;
}


// ROM 0x002d8b38 RestoreApostroph__FP8low_typeP9SPEC_TYPE
// Whether a dot e high in the line (at or above 0x27b1) is an apostrophe
// rather than an i's dot: the letters either side of it (the nearest
// neighbours left and right, PlaceApostrophe) found, then its length and
// slope weighed against how high it sits, whether it stands clear over
// the letters, whether it slants like an apostrophe, and whether an i
// under it would want it; an apostrophe is moved between two breaks after
// the letter to its left.  ==> 1 when it was.
//
// ROM QUIRKS: the squared length is kept in a short, so a long stroke
// wraps; two tests also want a flag that is nought and never set, so they
// never pass.
long
RestoreApostroph(low_type* low, SPEC_TYPE* e)
{
	short* x = low->fX;
	short* y = low->fY;
	short* xInit = low->fXInitial;
	short* map = low->fBuffers[2].ptr;
	const long never = 0;			// (the ROM's frame +0: set to 0 and never again)
	long quotes = 0;				// +0x14
	long leftClear = 1;				// +0x8: nothing of the letters under it on the left
	long rightClear = 1;			// +0x4
	if (e->code != 0x10)
		return 0;
	long iBeg = e->iBeg;
	long iEnd = e->iEnd;
	long mid = (iBeg + iEnd) >> 1;
	long xe = x[mid];
	if (y[mid] - 0x2780 > 0x31)
		return 0;
	short bottom = (short) (y[iBeg] > y[iEnd] ? y[iBeg] : y[iEnd]);
	short nearBreak = 0;
	SPEC_TYPE* start = PlaceApostrophe(low, e, 0, &nearBreak);
	if (start == nil)
		return 0;
	SPEC_TYPE* rightN = NextApostropheNeighbour(start);
	SPEC_TYPE* leftN = PrevApostropheNeighbour(start);
	for (short k = -10; ; )
	{
		if (rightN == nil)
			return 0;
		if (leftN == nil)
			break;
		long xl = x[(leftN->iBeg + leftN->iEnd) >> 1];
		long xr = x[(rightN->iBeg + rightN->iEnd) >> 1];
		if (xl <= xr)
			break;
		short dl = (short) (xl - xe);
		short dr = (short) (xr - xe);
		short al = dl < 0 ? (short) -dl : dl;
		short ar = dr < 0 ? (short) -dr : dr;
		short nearer = (al >= ar) ? dr : dl;
		if (nearer < 0)
			rightN = NextApostropheNeighbour(rightN);
		else
			leftN = PrevApostropheNeighbour(leftN->prev);
		k = (short) (k + 1);
		if (k == 0)
			break;
	}
	if (rightN == nil || leftN == nil)
		return 0;
	_RECT box;
	GetTraceBox(x, y, iBeg, iEnd, &box);
	long half = (box.right - box.left) / 2;
	short reach = (short) (half + 5);
	short cx = (short) (half + box.left);
	long lo = cx - reach;
	long hi = cx + reach;
	for (short i = leftN->iBeg; rightN->iEnd > i; i++)
	{
		long xi = x[i];
		if (xi < lo || xi > hi)
			continue;
		if (i >= iBeg && i <= iEnd)
			continue;
		if (xi > cx)
			rightClear = 0;
		else
			leftClear = 0;
	}
	long a = rightN->iBeg, b = rightN->iEnd;
	GetTraceBox(x, y, a < b ? a : b, a < b ? b : a, &box);
	long belowRight = bottom > box.top ? 1 : 0;			// +0x10
	a = leftN->iBeg; b = leftN->iEnd;
	GetTraceBox(x, y, a < b ? a : b, a < b ? b : a, &box);
	SPEC_TYPE* p = leftN->prev;
	if (p != nil && p->prev != nil)
	{
		SPEC_TYPE* pp = p->prev;
		quotes = 0;
		if (p->code == 0xd && pp->code == 0xd && !(pp->prev != nil && pp->prev->code == 0xd))
		{
			long hp = p->attr & 0xf;
			long hpp = pp->attr & 0xf;
			if (!(hp > 5 && hp < 9) && !(hpp > 5 && hpp < 9))
				quotes = 1;
		}
	}
	long belowLeft = (bottom > box.top || quotes) ? 1 : 0;	// +0xc
	if (!belowRight && !belowLeft && !leftClear && !rightClear)
		return 0;
	long between = (belowRight && belowLeft) ? 1 : 0;		// +0x20
	if (!between && nearBreak != 0)
		return 0;
	short dx = (short) ((UShort) x[iBeg] - (UShort) x[iEnd]);
	if (dx < 0)
		dx = (short) -dx;
	long y0 = y[iBeg];
	long y1 = y[iEnd];
	short dy = (short) (y0 - y1);
	if (dy < 0)
		dy = (short) -dy;
	short len2 = (short) LAdd(LMul(dx, dx), LMul(dy, dy));
	long v = 0x27e6 - bottom;
	long q = (v + 2) / 4;
	long tall = (LMul(q, q) < len2) ? 1 : 0;				// +0x18
	long xb = xInit[map[iBeg]];
	long xn = xInit[map[iEnd]];
	long slant = (y0 < y1) ? (xb < xn ? 1 : 0) : (xb > xn ? 1 : 0);	// +0x1c
	if (between || (leftClear && rightClear))
	{
		long w = (v + 8) / 16;
		if (LMul(w, w) > len2)
			return 0;
		if (between)
			goto wide;
	}
	else
	{
		long div = quotes ? 0xa : 6;
		long t = (v + (div >> 1)) / div;
		if (LMul(t, t) > len2)
			return 0;
	}
	if (tall || (leftClear && rightClear))
	{
	wide:
		if ((15 + 10 * dx) / 30 > dy)
			return 0;
	}
	else
	{
		if ((7 + 10 * dx) / 15 > dy)
			return 0;
	}
	if (slant != 0 || between == 0)
	{
		if (leftClear && (11 * dx + 5) / 10 > dy)
			return 0;
	}
	if (!between)
	{
		Boolean nearI = (!belowLeft && IsNearI(leftN)) || (!belowRight && IsNearI(rightN));
		if (nearI)
		{
			long ok = 1;
			Boolean skip = false;
			if (!belowLeft && IsNearI(leftN) && leftN->prev != nil && leftN->prev->code == 0xd)
			{
				ok = 0;
				skip = true;
			}
			if (!skip && !belowRight && IsNearI(rightN) && rightN->prev->code == 0xd)
				ok = 0;
			if (skip || !belowLeft)
			{
				if (IsNearI(leftN) && leftClear && never)
					ok = 0;
			}
			if (belowRight || !(IsNearI(rightN) && rightClear && never))
			{
				for (SPEC_TYPE* r = low->fSpecl->next; r != nil; r = r->next)
				{
					if (r->code != 0x10 || r == e)
						continue;
					long from, to;
					if (!belowLeft && IsNearI(leftN))
					{
						long xl = x[leftN->iBeg];
						from = xl - (xe - xl);
						to = xe;
					}
					else
					{
						long xr = x[rightN->iBeg];
						from = xe;
						to = xr + (xr - xe);
					}
					long xq = x[(r->iBeg + r->iEnd) >> 1];
					if (xq > from && xq < to)
						ok = 0;
				}
				if (ok)
					return 0;
			}
		}
	}
	GetTraceBox(x, y, iBeg, iEnd, &box);
	box.left -= 2;
	box.right += 2;
	for (short i = leftN->iBeg; rightN->iEnd > i; i++)
	{
		long xi = x[i];
		if (xi < box.left || xi > box.right)
			continue;
		if (i >= iBeg && i <= iEnd)
			continue;
		if (y[i] < box.bottom)
			return 0;
	}
	PlaceApostrophe(low, e, 1, &nearBreak);
	return 1;
}
