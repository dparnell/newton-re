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
#include "XrDomains.h"


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


// ROM 0x001c0ee0 point_of_smooth_bord__FiT1P4EXTRP8low_typeT1
// The line's height at point i: the extrema within w either side of it
// (at most nine, the window's ends interpolated between the extrema that
// straddle them), runs at the same x evened out, and the area under the
// polyline divided by the window's width.
long
point_of_smooth_bord(long i, long n, EXTR* extr, low_type* low, long w)
{
	long P[11];				// the polyline's x, from index 1
	long Q[11];				// and its y (ROM: ten words, so the last lands on P[0], which is never read)
	long xi = low->fX[i];
	long lo = xi - w;
	long hi = xi + w;
	long k = 0;
	while (k < n && extr[k].x <= lo)
		k++;
	long first = k - 1;
	while (k < n && extr[k].x < hi)
		k++;
	long cnt = k - first;
	if (10 < cnt + 1)
		cnt = 9;
	P[1] = low->fBox.left;
	if (low->fBox.left <= lo)
		P[1] = lo;
	if (first == -1)
		Q[1] = extr[0].y;
	if (n - 1 == first)
		Q[1] = extr[n - 1].y;
	if (first != -1 && n - 1 != first)
	{
		long y0 = extr[first].y;
		long dx = extr[first + 1].x - extr[first].x;
		if (dx < 1)
			dx = 1;
		Q[1] = y0 + (short) ((extr[first + 1].y - y0) * (lo - extr[first].x) / dx);
	}
	for (long m = 1; m < cnt; m++)
	{
		P[m + 1] = extr[first + m].x;
		Q[m + 1] = extr[first + m].y;
	}
	long lastX = hi;
	if (low->fBox.right < hi)
		lastX = low->fBox.right;
	P[cnt + 1] = lastX;
	if (k == 0)
		Q[cnt + 1] = extr[0].y;
	if (k == n)
		Q[cnt + 1] = extr[n - 1].y;
	if (k != 0 && k != n)
	{
		long y1 = extr[k - 1].y;
		long dx = extr[k].x - extr[k - 1].x;
		if (dx < 1)
			dx = 1;
		Q[cnt + 1] = y1 + (short) ((extr[k].y - y1) * (hi - extr[k - 1].x) / dx);
	}
	// a run of points at one x: its two ends given the run's mean
	long a = 0;
	long b = a;
	if (0 < cnt)
	{
		do
		{
			long c;
			for ( ; ; )
			{
				c = b;
				if (c + 1 <= cnt)
				{
					b = c + 1;
					if (P[c + 2] == P[a + 1])
						continue;
				}
				break;
			}
			if (a < c)
			{
				long sum = 0;
				long len = c - a;
				for (long m = 0; m <= len; m++)
					sum = Q[a + m + 1] + sum;
				long mean = sum / (len + 1);
				Q[c + 1] = mean;
				Q[a + 1] = mean;
				a = c;
			}
			a++;
			b = a;
		} while (a < cnt);
	}
	long width = lastX - P[1];
	long area = Q[1];
	if (width != 0)
	{
		long acc = 0;
		for (long m = 1; m <= cnt; m++)
			acc = (P[m + 1] - P[m]) * (Q[m + 1] + Q[m]) + acc;
		area = acc >> 1;
	}
	if (width < 1)
		width = 1;
	return (short) (area / width);
}


// ROM 0x001c11ec smooth_d_bord__FP4EXTRiP8low_typeT2Ps
// The lower line under every point (0 at a pen-up) smoothed over w
// from the bottoms; with none, the box's bottom (or rc's own line, once
// it has been worked out).
void
smooth_d_bord(EXTR* extr, long n, low_type* low, long w, short* line)
{
	for (long i = 0; i < low->fII; i++)
	{
		if (low->fY[i] == -1)
			line[i] = 0;
		else if (n >= 1)
			line[i] = point_of_smooth_bord(i, n, extr, low, w);
		else if ((short) RCGetH(low->rc, 0xe8) < 0x32)
			line[i] = low->fBox.bottom;
		else
			line[i] = RCGetH(low->rc, 0xe4);
	}
}


// ROM 0x001c1308 smooth_u_bord__FP4EXTRiP8low_typeT2PsT5
// The upper line over every point, smoothed over w from the tops; from
// one top, the lower line moved up by that top's height; from none, the
// box's top - or, for a word being read as a number, a third of the way
// from the box's top to the lower line's highest point (or rc's own).
void
smooth_u_bord(EXTR* extr, long n, low_type* low, long w, short* line, short* base)
{
	short ii = low->fII;
	if (n > 1)
	{
		for (long i = 0; i < low->fII; i++)
			line[i] = (low->fY[i] == -1) ? 0 : point_of_smooth_bord(i, n, extr, low, w);
	}
	if (n == 1)
	{
		for (long i = 0; i < low->fII; i++)
		{
			if (low->fY[i] == -1)
				line[i] = 0;
			else
				line[i] = (UShort) base[i] - ((UShort) base[extr[0].i] - (UShort) extr[0].y);
		}
	}
	if (n != 0)
		return;
	if (RCGetH(low->rc, 0x94) != 0x10)
	{
		for (long i = 0; i < low->fII; i++)
			line[i] = (low->fY[i] == -1) ? 0 : low->fBox.top;
		return;
	}
	long least = 0x7fff;
	for (long i = 0; i < ii; i++)
	{
		if (low->fY[i] != -1 && base[i] < least)
			least = base[i];
	}
	for (long i = 0; i < low->fII; i++)
	{
		if (low->fY[i] == -1)
			line[i] = 0;
		else
		{
			long e6 = (short) RCGetH(low->rc, 0xe6);
			if (0x31 < e6)
				e6 = (short) RCGetH(low->rc, 0xe8);
			if (e6 < 0x32)
				line[i] = (short) ((low->fBox.top * 2 + 1) / 3) + (short) ((least + 1) / 3);
			else
				line[i] = (short) RCGetH(low->rc, 0xe4) - (short) RCGetH(low->rc, 0xe2);
		}
	}
}


// ROM 0x001bf8ac neibour_susp_extr__FP4EXTRiUcPsT2
// Two neighbours that are both suspect - one sticking out (0x65 bottoms,
// 0x66 tops) beside one near the line (0x67): the one further from the
// average of the unsuspected ones is left suspect and the other put back
// (for the tops, measured as heights over base, and a height of at least
// lim decides it outright).  ==> 0, 1 for fewer than two unsuspected.
long
neibour_susp_extr(EXTR* extr, long n, UByte kind, short* base, long lim)
{
	long tag = 0;			// ROM BUG: unset for a kind other than 1 or 3 (never given one)
	if (kind == 3)
		tag = 0x65;
	else if (kind == 1)
		tag = 0x66;
	long plain = 0;
	long sum = 0;
	long avg = 0;
	long vCur = 0;			// (a register the ROM carries from one element to the next)
	long vPrev = 0;
	if (n <= 0)
		return 1;
	for (long i = 0; i < n; i++)
		if (extr[i].susp != tag && extr[i].susp != 0x67)
			plain++;
	if (plain > 1)
	{
		for (long i = 0; i < n; i++)
		{
			if (extr[i].susp == tag || extr[i].susp == 0x67)
				continue;
			if (kind == 3)
				vCur = extr[i].y;
			else if (kind == 1)
				vCur = base[extr[i].i] - extr[i].y;
			sum += vCur;
		}
		avg = sum / plain;
	}
	if (plain <= 1)
		return 1;
	for (long i = 1; i < n; i++)
	{
		EXTR* cur = &extr[i];
		EXTR* prev = &extr[i - 1];
		if (!((cur->susp == tag && prev->susp == 0x67) || (cur->susp == 0x67 && prev->susp == tag)))
			continue;
		long j;
		if (kind == 3)
		{
			vCur = cur->y;
			vPrev = prev->y;
		}
		else if (kind == 1)
		{
			vCur = base[cur->i] - cur->y;
			vPrev = base[prev->i] - prev->y;
			if (lim > 0 && cur->susp == tag && vCur >= lim)
			{
				ret_to_line(extr, n, i, i - 1);
				continue;
			}
			if (lim > 0 && prev->susp == tag && vPrev >= lim)
			{
				ret_to_line(extr, n, i, i);
				continue;
			}
		}
		if (HWRAbs(vCur - avg) >= HWRAbs(vPrev - avg))
			j = i - 1;
		else
			j = i;
		ret_to_line(extr, n, i, j);
	}
	return 0;
}


// ROM 0x001c17fc fill_i_point__FPsP8low_type
// The points (not pen-ups) in order of x, by insertion: a point whose x
// is already there is left out.  ==> how many.
long
fill_i_point(short* order, low_type* low)
{
	short* x = low->fX;
	long cnt = 0;
	long maxX = -1;
	for (long i = 0; i < low->fII; i++)
	{
		if (low->fY[i] == -1)
			continue;
		long xv = x[i];
		if (maxX < xv)
		{
			order[cnt] = i;
			maxX = x[i];
			cnt++;
			continue;
		}
		for (long j = cnt - 1; j >= 0; j--)
		{
			long v = x[order[j]];
			if (xv == v)
				break;
			if (xv > v)
			{
				for (long k = cnt; j + 1 < k; k--)
					order[k] = order[k - 1];
				order[j + 1] = i;
				cnt++;
				break;
			}
		}
		if (x[i] < x[order[0]])
		{
			for (long k = cnt; 0 < k; k--)
				order[k] = order[k - 1];
			order[0] = i;
			cnt++;
		}
	}
	return cnt;
}


// ROM 0x001c3b94 correct_narrow_ends__FP4EXTRPiT1iT4Uc
// The extrema of src beyond the ends of extr (before its first, for
// 0x10; after its last, for 0x20) copied in, moved down by dy and marked
// 0x6e, so that a line found over the middle of the word reaches its
// ends.  ==> 1.
long
correct_narrow_ends(EXTR* extr, long* n, EXTR* src, long m, long dy, UByte which)
{
	long i = 0;
	if (which == 0x10)
	{
		while (i < m && src[i].x < extr[0].x)
			i++;
		for (long k = i - 1; k >= 0; k--)
		{
			for (long j = *n; j > 0; j--)
				extr[j] = extr[j - 1];
			extr[0].x = src[k].x;
			extr[0].y = (UShort) src[k].y + dy;
			extr[0].i = src[k].i;
			extr[0].susp = 0x6e;
			extr[0].elem = nil;
			*n = *n + 1;
		}
	}
	if (which == 0x20)
	{
		long len = *n;
		while (i < m && extr[len - 1].x < src[(m - 1) - i].x)
			i++;
		for (long k = m - i; k < m; k++)
		{
			EXTR* e = &extr[len];
			e->x = src[k].x;
			e->y = (UShort) src[k].y + dy;
			e->i = src[k].i;
			e->susp = 0x6e;
			len++;
			e->elem = nil;
		}
		*n = len;
	}
	return 1;
}


// ROM 0x001bf578 non_super__FP4EXTRiPsN23
// Whether the top at extr[k] is not a real ascender: one already put back
// (0x6e), or - past the word's first few - a shallow arch between two
// bottoms that both lie below a line a third of the way from the top to
// the upper line.
long
non_super(EXTR* extr, long k, short* x, short* y, short* upper)
{
	SPEC_TYPE* elem = extr[k].elem;
	SPEC_TYPE* prev = elem->prev;
	SPEC_TYPE* next = elem->next;
	if (elem->code == 0x6e)
		return 1;
	if (3 < k && prev->mark == 3 && next->mark == 3)
	{
		long a = y[elem->iBeg];
		long b = y[elem->iEnd];
		long hi = (b < a) ? a : b;
		if (hi - y[elem->ipoint0] * 3 + hi * 2 < x[elem->iEnd] - x[elem->iBeg])
		{
			long line = (extr[k].y + 1) / 3 + (upper[extr[k].i] * 2 + 1) / 3;
			if (line < y[next->ipoint0] && line < y[prev->ipoint0])
				return 1;
		}
	}
	return 0;
}


// ROM 0x001beb90 non_sub__FP9SPEC_TYPEPsT2i
// Whether the bottom elem is not a real descender: a shallow dip (no
// deeper than two fifths of its width) between two tops, where the
// writing goes on to the right within eps of the dip's end.
long
non_sub(SPEC_TYPE* elem, short* x, short* y, long eps)
{
	long e = elem->iEnd;
	long low = y[e];
	if (y[elem->iBeg] < y[e])
		low = y[elem->iBeg];
	if ((y[elem->ipoint0] - low) * 5 <= (x[e] - x[elem->iBeg]) * 2)
	{
		SPEC_TYPE* next = elem->next;
		if (elem->prev->mark == 1 && next->mark == 1
		 && (x[next->ipoint0] >= x[next->iBeg] || y[elem->prev->ipoint0] >= y[next->ipoint0]))
		{
			long m = ixMin(e, next->iEnd, x, y);
			e = elem->iEnd;
			long lo = y[e] - eps;
			long hi = x[e] + eps;
			long v = y[m];
			if (v >= lo || x[m] >= hi)
			{
				if (lo <= v)
				{
					for (long p = e; p < next->iEnd; p++)
					{
						if (y[p] < lo || hi < x[p])
						{
							long m2 = ixMin((short) p, next->iEnd, x, y);
							return (hi <= x[m2]) ? 1 : 0;
						}
					}
				}
				return 1;
			}
		}
	}
	return 0;
}
