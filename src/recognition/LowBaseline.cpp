/*
	File:		LowBaseline.cpp

	Contains:	The cursive reader's low level: the pieces the base-line
				finder (transfrmN) is made of.  See LowLevel.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	The base-line finder works over two arrays of EXTRs, the upper
	extrema (the maxima of y: kind 3, the bottoms of the letters, since
	y grows downwards) and the lower ones (kind 1, their tops), each a
	copy of an extremum's place with a suspicion code: 0 on the line,
	0x65/0x66 an extremum that sticks out below or above it (a descender,
	an ascender), 0x67 one near it, 0x6e one put back on the line, 0x0d a
	tail struck off, the tens (0x14 ... 0x3c) the kinds of gap and glitch
	found in the line.  The element each came from has the same codes in
	its `code` byte.
*/

#include "LowLevel.h"
#include "ParaGraph.h"


// ROM 0x001bd73c sort_extr__FP4EXTRi
// Into order of x (a selection sort).
void
sort_extr(EXTR* extr, long n)
{
	for (long i = 0; i < n - 1; i++)
	{
		long m = i;
		long mx = extr[i].x;
		for (long j = i + 1; j < n; j++)
		{
			if (extr[j].x < mx)
			{
				m = j;
				mx = extr[j].x;
			}
		}
		EXTR t = extr[i];
		extr[i] = extr[m];
		extr[m] = t;
	}
}


// ROM 0x001c0ea4 calc_average__FPsi
// ==> the mean, 1 for none.
long
calc_average(short* a, long n)
{
	long sum = 0;
	if (n <= 0)
		return 1;
	for (long i = 0; i < n; i++)
		sum += a[i];
	return sum / n;
}


// ROM 0x001c5068 calc_mediana__FPsi
// A median, found by walking from the mean towards the side with more
// values, a distinct value at a time, until the two sides balance; the
// last step taken back (or half of it) when it overshot.  ==> 1 for none.
long
calc_mediana(short* a, long n)
{
	long cnt = 0;
	long step = 0;			// (set before it is read: the loop below runs at least once)
	if (n < 1)
		return 1;
	long m = calc_average(a, n);
	long lo = 0, hi = 0;
	for (long i = 0; i < n; i++)
	{
		if (a[i] < m)
			lo++;
		if (m < a[i])
			hi++;
	}
	if (hi == lo)
		return m;
	long dir = n;
	if (hi <= lo)
		dir = -1;
	if (lo <= hi)
		dir = 1;
	while (dir * (hi - lo) > 0)
	{
		long zeros = 0;
		step = 0x7fff;
		for (long i = 0; i < n; i++)
		{
			long d = dir * (a[i] - m);
			if (d == step)
				cnt++;
			if (0 < d && d < step)
			{
				cnt = 1;
				step = d;
			}
			if (d == 0)
				zeros++;
		}
		if (step == 0x7fff)
			return m;
		m = step * dir + m;
		if (dir == 1)
		{
			lo += zeros;
			hi -= cnt;
		}
		else
		{
			lo -= cnt;
			hi += zeros;
		}
	}
	long t = dir * (lo - hi);
	if (cnt <= t)
	{
		if (t != cnt)
			m = m - step * dir;
		if (t == cnt)
			m = m - ((step * dir) >> 1);
	}
	return m;
}


// ROM 0x001c4f74 extract_ampl__FP8low_typePsPi
// The heights of the letters: for every top (kind 1 with attr 1, 5 or
// 0xca) how far the bottoms either side of it lie below it.  ==> 0, 1 for
// more than a hundred.
long
extract_ampl(low_type* low, short* ampl, long* count)
{
	long n = 0;
	short* y = low->fBuffers[1].ptr;
	for (SPEC_TYPE* elem = low->fSpecl; elem != nil; elem = elem->next)
	{
		if (elem->mark == 1 && (elem->attr == 1 || elem->attr == 5 || elem->attr == 0xca))
		{
			long v = y[elem->ipoint0];
			SPEC_TYPE* next = elem->next;
			if (elem->prev->mark == 3)
			{
				if (n > 99)
					return 1;
				ampl[n++] = (UShort) y[elem->prev->ipoint0] - v;
			}
			if (next->mark == 3)
			{
				if (n > 99)
					return 1;
				ampl[n++] = (UShort) y[next->ipoint0] - v;
			}
		}
		if (elem->next == nil)
			*count = n;
	}
	return 0;
}


// ROM 0x001c17e4 sign__FiT1
long
sign(long a, long b)
{
	long r = b < a;
	if (a < b)
		r = -1;
	return r;
}


// ROM 0x001bcc2c pnt__F5_RECTi
// Whether a box is a point: under a third of k + 1 both wide and high.
Boolean
pnt(_RECT box, long k)
{
	long d = (k + 1) / 3;
	long w = box.right - box.left;
	if (w < d)
		return box.bottom - box.top < d;
	return false;
}


// ROM 0x001bd198 straight_stroke__FiT1PsT3T1
// Whether the trace from i to j is straight: the point furthest from the
// chord no further from it than a k-th of the chord's length (in the
// ROM's squared measure).  ==> 0 for a chord of no length.
long
straight_stroke(long i, long j, short* x, short* y, long k)
{
	long m = iMostFarFromChord(x, y, (short) i, (short) j);
	long dy = y[i] - y[j];
	long dx = x[j] - x[i];
	long len2 = dx * dx + dy * dy;
	long c = dx * (y[m] - y[i]) + dy * (x[m] - x[i]);
	if (c < 0)
		c = -c;
	if (c * k <= len2 && len2 != 0)
		return 1;
	return 0;
}


// ROM 0x001bcb90 str_com__FiT1PsT3T1
// Whether the trace from i to j is a straight stroke at least three
// quarters as high as it is wide - a comma.
long
str_com(long i, long j, short* x, short* y, long k)
{
	long w = HWRAbs(x[j] - x[i]);
	long h = HWRAbs(y[j] - y[i]);
	if (straight_stroke(i, j, x, y, k) == 1 && w - ((w + 2) >> 2) <= h)
		return 1;
	return 0;
}


// ROM 0x001c0c34 ret_to_line__FP4EXTRiN22
// The extremum at i (when j is i) or at i-1 (when j is i-1) put back on
// the line: its code negated, and the neighbours further out that had the
// same code given the new one too.
void
ret_to_line(EXTR* extr, long n, long i, long j)
{
	if (j == i)
	{
		short now = -extr[i].susp;
		extr[i].susp = now;
		if (i + 1 < n && -now == extr[i + 1].susp)
		{
			extr[i + 1].susp = now;
			if (i + 2 < n && -now == extr[i + 2].susp)
				extr[i + 2].susp = now;
		}
	}
	if (i - 1 != j)
		return;
	short now = -extr[i - 1].susp;
	extr[i - 1].susp = now;
	if (i - 2 < 0)
		return;
	if (-now == extr[i - 2].susp)
	{
		extr[i - 2].susp = now;
		if (i - 3 >= 0 && -now == extr[i - 3].susp)
			extr[i - 3].susp = now;
	}
}


// ROM 0x001bf7ec spec_neibour_extr__FP4EXTRiUcT2
// An extremum that sticks out (0x65 for the bottoms, 0x66 for the tops)
// next to one near the line (0x67): one of the two put back on the line
// - the left one unless dir says otherwise.
void
spec_neibour_extr(EXTR* extr, long n, UByte kind, long dir)
{
	long tag = 0;			// ROM BUG: a register never set for a kind other than 1 or 3 (it is only ever given those)
	if (kind == 3)
		tag = 0x65;
	else if (kind == 1)
		tag = 0x66;
	for (long i = 1; i < n; i++)
	{
		if (extr[i].susp == tag && extr[i - 1].susp == 0x67)
			ret_to_line(extr, n, i, (dir != -1) ? i - 1 : i);
		if (extr[i].susp == 0x67 && extr[i - 1].susp == tag)
			ret_to_line(extr, n, i, (dir != 1) ? i - 1 : i);
	}
}


// ROM 0x001bf75c super_min_to_line__FP4EXTRiPsN22Pi
// The tops that stick out above the line (0x66) by no more than about
// three quarters of a plus a quarter of b above base put back as 0x6e,
// and the count of them brought down.
void
super_min_to_line(EXTR* extr, long n, short* base, long a, long b, long* count)
{
	for (long i = 0; i < n; i++)
	{
		EXTR* e = &extr[i];
		if (e->susp == 0x66 && base[e->i] - e->y <= (a - ((a + 2) >> 2)) + ((b + 2) >> 2))
		{
			e->susp = 0x6e;
			*count = *count - 1;
		}
	}
}


// ROM 0x001c0d14 delete_line_extr__FP4EXTRPii
// Every extremum with the code taken out.  ==> 1.
long
delete_line_extr(EXTR* extr, long* n, long code)
{
	long len = *n;
	for (long i = 0; i < len; i++)
	{
		if (extr[i].susp == code)
		{
			// (the last move reads the element after the last)
			for (long j = i; j < len; j++)
				extr[j] = extr[j + 1];
			len--;
			i--;
		}
	}
	*n = len;
	return 1;
}


// ROM 0x001c0d88 insert_line_extr__FP8low_typeP9SPEC_TYPEP4EXTRPi
// The extremum at elem put into the array in order of x, marked 0x6e
// (put back on the line), with its stroke's ending attr.  ==> 1.
long
insert_line_extr(low_type* low, SPEC_TYPE* elem, EXTR* extr, long* n)
{
	short* x = low->fBuffers[0].ptr;
	short* y = low->fBuffers[1].ptr;
	short* map = low->fBuffers[2].ptr;
	long len = *n;
	SPEC_TYPE* end = elem->next;
	while (end->mark != 0x20)
		end = end->next;
	UByte endAttr = end->attr;
	long i = 0;
	while (i < len && extr[i].x < x[elem->ipoint0])
		i++;
	for (long j = len - 1; i <= j; j--)
		extr[j + 1] = extr[j];
	long p = elem->ipoint0;
	EXTR* e = &extr[i];
	e->x = x[p];
	e->y = y[p];
	e->i = map[p];
	e->susp = 0x6e;
	e->attr = endAttr;
	e->f8 = 0;
	e->elem = elem;
	*n = *n + 1;
	return 1;
}


// ROM 0x001bf6a4 sub_max_to_line__FP8low_typeP4EXTRPiPsi
// The bottoms marked as sticking out below the line (code 0x65) by less
// than 35% of lim put back into the array of bottoms, as 0x6e.  ==> what
// the last insert_line_extr answered, 0 for none.
long
sub_max_to_line(low_type* low, EXTR* extr, long* n, short* base, long lim)
{
	long result = 0;
	short* map = low->fBuffers[2].ptr;
	short* y = low->fBuffers[1].ptr;
	for (SPEC_TYPE* elem = low->fSpecl; elem != nil; elem = elem->next)
	{
		if (elem->mark == 3 && elem->code == 0x65)
		{
			long d = y[elem->ipoint0] - base[map[elem->ipoint0]];
			if (lim * 0x23 - d * 100 != 0 && d * 100 <= lim * 0x23)
			{
				result = insert_line_extr(low, elem, extr, n);
				elem->code = 0x6e;
			}
		}
	}
	return result;
}


// ROM 0x001c0ab0 calc_ampl__F4EXTRPsUc
// How high a letter is at an extremum: how far the extrema of the other
// kind either side of it are (the mean of the two when both are real
// extrema inside the stroke, else the larger); failing those, three
// quarters of the larger distance to any neighbour of the other kind.
long
calc_ampl(EXTR e, short* y, UByte kind)
{
	long s;
	UByte other;
	if (kind == 1)
	{
		s = -1;
		other = 3;
	}
	else
	{
		s = 1;
		other = 1;
	}
	SPEC_TYPE* prev = e.elem->prev;
	SPEC_TYPE* next = e.elem->next;
	long v = e.y;
	long a = (prev->mark == other && (prev->attr == 1 || prev->attr == 5)) ? s * (v - y[prev->ipoint0]) : 0;
	long b = (next->mark == other && (next->attr == 1 || next->attr == 5)) ? s * (v - y[next->ipoint0]) : 0;
	long r;
	if (a != 0 && b != 0 && next->next->mark != 0x20 && prev->prev->mark != 0x10)
		r = (a + b) >> 1;
	else
		r = (b < a) ? a : b;
	if (r != 0)
		return r;
	a = (prev->mark == other) ? s * (v - y[prev->ipoint0]) : 0;
	b = (next->mark == other) ? s * (v - y[next->ipoint0]) : 0;
	if (b < a)
		b = a;
	return b - ((b + 2) >> 2);
}


// ROM 0x001c4c4c is_defis__FP8low_typei
// Whether a word of one or two strokes is a dash: each stroke ending in
// an element marked 2.
long
is_defis(low_type* low, long nStrokes)
{
	if (nStrokes < 3 && 0 < nStrokes)
	{
		SPEC_TYPE* elem;
		for (elem = low->fSpecl; elem != nil; elem = elem->next)
		{
			if (elem->mark == 0x20)
			{
				if (elem->prev->attr != 2)
					return 0;
				break;
			}
		}
		if (nStrokes == 1)
			return 1;
		for (elem = elem->next; elem != nil; elem = elem->next)
		{
			if (elem->mark == 0x20)
				return (elem->prev->attr == 2) ? 1 : 0;
		}
	}
	return 0;
}


// ROM 0x001c239c del_tail_min__FP4EXTRPiPsT3Uc
// The tops that are only a tail: each neighbouring bottom (or the stroke's
// end) near the top compared with how far the line lies below it (a fifth
// of it, a tenth when flag is set) - marked 0x0d and taken out.  ==> 1.
long
del_tail_min(EXTR* extr, long* n, short* y, short* base, UByte flag)
{
	long len = *n;
	long f = (flag == 0) ? 5 : 10;
	for (long i = 0; i < len; i++)
	{
		EXTR* e = &extr[i];
		SPEC_TYPE* elem = e->elem;
		long depth = (base[e->i] - e->y) * 2;
		UByte p = elem->prev->mark;
		if (p == 0x10 || (p == 3 && f * (y[elem->prev->ipoint0] - e->y) < depth))
		{
			UByte q = elem->next->mark;
			if (q == 0x20 || (q == 3 && f * (y[elem->next->ipoint0] - e->y) < depth))
			{
				e->susp = 0x0d;
				elem->code = 0x0d;
			}
		}
	}
	return delete_line_extr(extr, n, 0x0d);
}


// ROM 0x001bdff0 extract_num_extr__FP8low_typeUcP4EXTRPi
// For a number: in each stroke that ends plainly (attr 1 or 0xca, or 0xcd
// for the tops), the last extremum of the kind at the stroke's top (for
// kind 1) or bottom, copied into the array and marked 100.  ==> 0, 1 for
// more than fifty.
long
extract_num_extr(low_type* low, UByte kind, EXTR* extr, long* count)
{
	short* x = low->fBuffers[0].ptr;
	short* y = low->fBuffers[1].ptr;
	short* map = low->fBuffers[2].ptr;
	long n = 0;
	long start = 0;			// (the ROM's register is set by the 0x10 every stroke starts with)
	for (SPEC_TYPE* elem = low->fSpecl; elem != nil; elem = elem->next)
	{
		if (elem->mark == 0x10)
			start = elem->iBeg;
		else if (elem->mark == 0x20 && (elem->attr == 1 || elem->attr == 0xca || (elem->attr == 0xcd && kind == 1)))
		{
			_RECT box;
			GetTraceBox(x, y, (short) start, elem->iEnd, &box);
			long target = (kind == 1) ? box.top : box.bottom;
			for (SPEC_TYPE* p = elem; p->mark != 0x10; p = p->prev)
			{
				if (p->mark == kind && y[p->ipoint0] == target)
				{
					if (n > 0x31)
						return 1;
					p->attr = elem->attr;
					long i = p->ipoint0;
					EXTR* e = &extr[n];
					e->x = x[i];
					e->y = y[i];
					e->i = map[i];
					e->susp = 0;
					e->elem = p;
					p->code = 100;
					n++;
					break;
				}
			}
		}
		if (elem->next == nil)
			*count = n;
	}
	return 0;
}
