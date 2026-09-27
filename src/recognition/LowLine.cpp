/*
	File:		LowLine.cpp

	Contains:	The cursive reader's low level: the base-line finder's
				search for gaps and glitches in a line of extrema, and
				what it then makes of them.  See LowLevel.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	A line is an array of EXTRs in order of x - the bottoms (kind 3) or
	the tops (kind 1) - with its strokes' gaps closed up (each EXTR's
	shift).  Walking along it, an extremum is a *gap* when the line
	steps up or down at it by more than a slope and a height allow
	(0x1e a step down to it - y grows downwards, so its y is below its
	neighbour's - 0x14 a step up), and a run of one, two or more
	extrema is a *glitch* when it sticks out of the line on both sides
	(0x1e/0x32/0x46 below, 0x14/0x28/0x3c above, for one, two, more).
	The slopes (in hundredths) and heights (in percent of the letters'
	amplitude) come from the ROM's tables TG1/H1 (gaps) and TG2/H2
	(glitches), indexed by where on the line the extremum is (the start,
	the middle, the end), whether the line is the tops, and which way it
	steps; CS is the cosine under which a bend is not a gap.
	`glitch_to_*` then decide what a glitch is: a descender (0x65), an
	ascender (0x66), part of the line after all (0), or a thing inside
	the letters (0x67); `all_susp_extr` settles a suspicious extremum
	next to an inside one.
*/

#include "LowLevel.h"
#include "ParaGraph.h"
#include "XrDomains.h"


// The EXTR's x with its stroke's gap closed up (a halfword subtraction).
static inline long
ShiftedX(const EXTR& e)
{
	return (short) ((UShort) e.x - (UShort) e.shift);
}


static inline long
Table(const int* table, long row, long top, long which)
{
	return table[row * 4 + top * 2 + which];
}


// ROM 0x001bd7d0 find_gaps_in_line__FP4EXTRiN22UcN22PsT8UiUi
// The gaps in a line of n extrema of a kind: each extremum whose step
// from its neighbour (from the next one at the start, from the one
// before at the end, from both in the middle) is steeper than the slope
// and higher than the height the tables allow, is marked 0x1e (a step
// down) or 0x14 (up).  The first and last extrema are measured against
// the next or last, and also against half the slope and twice the
// height (the tops only get the first of those at the start).  In the
// middle the tables' middle row is used, and a bend sharper than CS's
// cosine is not a gap.  The height is the letters' amplitude there
// (the depth below `line` for the tops, calc_ampl for the bottoms), no
// more than lim.  n of 2 uses 35/40; fast (1) 30/20; wide (1) takes the
// height half again.  xStart/xEnd: an extremum before or after them is
// at the line's start or end.
void
find_gaps_in_line(EXTR* extr, long n, long mode, long lim, UByte kind, long xStart, long xEnd, short* line, short* y, ULong fast, ULong wide)
{
	if (n < 2)
		return;
	long last = n - 1;
	long top = (kind != 1) ? 1 : 0;
	long dx = (ShiftedX(extr[1])) - (ShiftedX(extr[0]));
	long dy = extr[0].y - extr[1].y;
	long up = (dy >= 0) ? 1 : 0;
	long row = (extr[0].x > xStart) ? 1 : 0;
	long tg = Table(TG1, row, top, up);
	long ht = Table(H1, row, top, up);
	if (n == 2)
	{
		tg = 0x23;
		ht = 0x28;
	}
	if (mode == 2 && kind == 3 && up == 0)
	{
		tg = 0x1e;
		ht = 10;
	}
	if (fast == 1)
	{
		tg = 0x1e;
		ht = 0x14;
	}
	if (wide == 1)
		ht = (ht * 3) / 2;
	long amp;
	if (kind == 1)
	{
		long a = line[extr[0].i] - extr[0].y;
		long b = line[extr[1].i] - extr[1].y;
		amp = (a >= b) ? b : a;
	}
	else
	{
		long a = calc_ampl(extr[0], y, 3);
		long b = calc_ampl(extr[1], y, 3);
		amp = (a < b) ? calc_ampl(extr[0], y, 3) : calc_ampl(extr[1], y, 3);
	}
	if (amp >= lim)
		amp = lim;
	long s = LMul(dx, tg);
	long d100 = LMul(dy, 100);
	if (s <= d100 && LMul(amp, ht) <= d100)
		extr[0].susp = 0x1e;
	if (LMul(-tg, dx) >= d100 && LMul(-ht, amp) >= d100)
		extr[0].susp = 0x14;
	if (d100 >= (s >> 1) && d100 >= LMul(amp, ht) * 2 && kind == 1)
		extr[0].susp = 0x1e;
	if (-(s >> 1) >= d100 && -(LMul(amp, ht) * 2) >= d100)
		extr[0].susp = 0x14;

	for (long t = 1; t < last; t++)
	{
		EXTR* e = &extr[t];
		long cosine = cos_pointvect((short) ShiftedX(e[0]), e[0].y, (short) ShiftedX(e[-1]), e[-1].y,
									(short) ShiftedX(e[0]), e[0].y, (short) ShiftedX(e[1]), e[1].y);
		long dyp = e[0].y - e[-1].y;
		long dyn = e[0].y - e[1].y;
		if (!((dyp < 0 || dyn > 0) && (dyp > 0 || dyn < 0)))
			continue;
		long h = (fast != 0) ? 0x14 : Table(H1, 1, top, (dyp >= 0) ? 1 : 0);
		if (wide == 1)
			h = (h * 3) / 2;
		long ap, an;
		if (kind == 1)
		{
			long here = line[e[0].i] - e[0].y;
			long before = line[e[-1].i] - e[-1].y;
			ap = (before > here) ? here : before;
			if (ap >= lim)
				ap = lim;
			long after = line[e[1].i] - e[1].y;
			an = (after <= here) ? after : here;
		}
		else
		{
			long here = calc_ampl(e[0], y, 3);
			long before = calc_ampl(e[-1], y, 3);
			ap = (before > here) ? here : calc_ampl(e[-1], y, 3);
			if (ap >= lim)
				ap = lim;
			long after = calc_ampl(e[1], y, 3);
			an = (after > here) ? here : calc_ampl(e[1], y, 3);
		}
		if (an >= lim)
			an = lim;
		if (cosine < -CS[0])
			continue;
		if (LMul(ap, h) <= LMul(dyp, 100) && LMul(an, h) <= LMul(dyn, 100))
			e->susp = 0x1e;
		if (LMul(-h, ap) >= LMul(dyp, 100) && LMul(-h, an) >= LMul(dyn, 100))
			e->susp = 0x14;
	}

	EXTR* e = &extr[last];
	dy = e[0].y - e[-1].y;
	dx = ShiftedX(e[0]) - ShiftedX(e[-1]);
	up = (dy >= 0) ? 1 : 0;
	row = (e[0].x >= xEnd) ? 2 : 1;
	tg = Table(TG1, row, top, up);
	long hl = Table(H1, row, top, up);
	if (n == 2)
	{
		tg = 0x23;
		hl = 0x28;
	}
	if (fast == 1)
	{
		tg = 0x1e;
		hl = 0x14;
	}
	if (wide == 1)
		hl = (hl * 3) / 2;
	if (kind == 1)
	{
		long a = line[e[0].i] - e[0].y;
		long b = line[e[-1].i] - e[-1].y;
		amp = (a < b) ? a : b;
	}
	else
	{
		long a = calc_ampl(e[0], y, 3);
		long b = calc_ampl(e[-1], y, 3);
		amp = (a >= b) ? calc_ampl(e[-1], y, 3) : calc_ampl(e[0], y, 3);
	}
	if (amp >= lim)
		amp = lim;
	s = LMul(dx, tg);
	d100 = LMul(dy, 100);
	if (s <= d100 && LMul(amp, hl) <= d100)
		e->susp = 0x1e;
	if (LMul(-tg, dx) >= d100 && LMul(-hl, amp) >= d100)
		e->susp = 0x14;
	if (d100 >= (s >> 1) && d100 >= LMul(amp, hl) * 2)
		e->susp = 0x1e;
	if (-(s >> 1) >= d100 && -(LMul(amp, hl) * 2) >= d100)
		e->susp = 0x14;
}


// ROM 0x001be17c find_glitches_in_line__FP4EXTRiT2UcN22PsN27T2UiUi
// The glitches in a line of n extrema: every run of `width` extrema, one
// to maxWidth long, that the line steps into and out of the same way -
// both steps steep and high by TG2/H2 - and whose own spread of heights
// is small against the smaller step (55/100ths of it, 55 when the run
// already has a gap mark in it and it is not at the line's start - such
// a run is skipped - 57 when fast).  A run below the line is marked
// 0x1e/0x32/0x46 for one, two, more; above, 0x14/0x28/0x3c.  The height
// the steps are held to is the letters' amplitude (the depth below
// `line` for the tops), no more than lim.  A single top just after a
// bottom coded 0x64/0x65 has its step taken less its slant (x/y).
void
find_glitches_in_line(EXTR* extr, long n, long lim, UByte kind, long xStart, long xEnd, short* line, short* x, short* y, long maxWidth, ULong fast, ULong wide)
{
	long ampLeft = lim;
	long ampRight = lim;
	long top = (kind != 1) ? 1 : 0;
	long last = n - 1;
	for (long width = 1; width <= maxWidth; width++)
	{
		if (last <= width)
			return;
		for (long t = width; t < n; t++)
		{
			long marked = 0;
			for (long k = 0; k <= width; k++)
			{
				long c = extr[t - k].susp;
				if (c == 0x14 || c == 0x28 || c == 0x3c || c == 0x1e || c == 0x32 || c == 0x46)
					marked = 1;
			}
			if ((t > width || kind == 3) && marked == 1)
				continue;
			long dxl, dyl;
			if (t > width)
			{
				EXTR* e = &extr[t - width];
				dxl = ShiftedX(e[0]) - ShiftedX(e[-1]);
				dyl = e[0].y - e[-1].y;
			}
			else
			{
				dxl = 0;
				dyl = lim * 2;
			}
			long sl = (t > width) ? sign(dyl, 0) : 0;
			long adyl = HWRAbs(dyl);
			long dxr, dyr;
			if (last > t)
			{
				dxr = ShiftedX(extr[t + 1]) - ShiftedX(extr[t]);
				dyr = extr[t].y - extr[t + 1].y;
			}
			else
			{
				dxr = 0;
				dyr = lim * 2;
			}
			long sr = (last > t) ? sign(dyr, 0) : 0;
			long adyr = HWRAbs(dyr);
			if (LMul(sr, sl) < 0)
				continue;
			long down = (sl >= 0 && sr >= 0) ? 1 : 0;
			long step = (adyl >= adyr) ? adyr : adyl;
			long ymax = 0, ymin = 0x7fff;
			for (long k = 0; k <= width; k++)
			{
				long v = extr[t - k].y;
				if (v >= ymax)
					ymax = v;
				if (v <= ymin)
					ymin = v;
			}
			long spread = ymax - ymin;
			if (kind == 1)
			{
				EXTR* f = &extr[t - width];
				long a = line[f->i] - f->y;
				long b = line[extr[t].i] - extr[t].y;
				if (t > width)
				{
					long c = line[f[-1].i] - f[-1].y;
					if (c <= a)
						a = c;
				}
				if (last > t)
				{
					long d = line[extr[t + 1].i] - extr[t + 1].y;
					if (d <= b)
						b = d;
				}
				ampLeft = (a >= lim) ? lim : a;
				ampRight = (b >= lim) ? lim : b;
			}
			long row = 1;
			if (t == width && xStart >= extr[0].x)
				row = 0;
			if (last == t && xEnd <= extr[n - 1].x)
				row = 2;
			long tg = Table(TG2, row, top, down);
			if (t == width)
				row = 0;
			if (last == t)
				row = 2;
			long ht = Table(H2, row, top, down);
			long flat = 100;
			if (marked == 1)
				flat = 0x37;
			if (fast == 1)
			{
				tg = 0x28;
				ht = 0x1e;
				flat = 0x39;
			}
			if (wide == 1)
				ht = (ht * 3) / 2;
			if (x != nil && y != nil && kind == 1 && down == 0 && row > 0 && width == 1)
			{
				SPEC_TYPE* el = extr[t - 1].elem;
				SPEC_TYPE* nx = el->next;
				if (nx->mark == 3 && (nx->code == 0x64 || nx->code == 0x65))
				{
					long a = y[nx->ipoint0] - y[el->ipoint0];
					long b = x[el->ipoint0] - x[nx->ipoint0];
					// DEVIATION: an upright stroke (a of nought) is a divide
					// by zero the ROM would trap on; the step is left as it is.
					if (b > 0 && a != 0)
						dxl = HWRAbs(dxl - LMul(b, adyl) / a);
				}
			}
			if (!(LMul(tg, dxl) <= LMul(adyl, 100) && LMul(ampLeft, ht) <= LMul(adyl, 100)))
				continue;
			if (!(LMul(tg, dxr) <= LMul(adyr, 100) && LMul(ampRight, ht) <= LMul(adyr, 100)))
				continue;
			if (!(LMul(step, flat) > LMul(spread, 100)))
				continue;
			if (down == 1)
			{
				if (width == 0)
					extr[t].susp = 0x1e;
				else if (width == 1)
				{
					extr[t - 1].susp = 0x32;
					extr[t].susp = 0x32;
				}
				else
					for (long k = 0; k <= width; k++)
						extr[t - k].susp = 0x46;
			}
			else
			{
				if (width == 0)
					extr[t].susp = 0x14;
				else if (width == 1)
				{
					extr[t - 1].susp = 0x28;
					extr[t].susp = 0x28;
				}
				else
					for (long k = 0; k <= width; k++)
						extr[t - k].susp = 0x3c;
			}
		}
	}
}


// Whether a bottom's neighbour top (prev or next) is more than lim above
// it.
static inline Boolean
DeepFrom(SPEC_TYPE* elem, SPEC_TYPE* other, short* y, long lim)
{
	return other->mark == 1 && y[elem->ipoint0] - y[other->ipoint0] > lim;
}


// ROM 0x001be7e0 glitch_to_sub_max__FP8low_typeP4EXTRiT3Ui
// The glitches below the line of bottoms (0x1e, one; 0x32, two) made
// descenders (0x65) when they are deep - a real dip (non_sub says no), and
// a top beside it, or the line beside it, more than lim above - or when
// sure (1); else put back on the line (0).  One already put back (0x6e)
// is left alone.
void
glitch_to_sub_max(low_type* low, EXTR* extr, long n, long lim, ULong sure)
{
	short* x = low->fBuffers[0].ptr;
	short* y = low->fBuffers[1].ptr;
	long h = (short) RCGetH(low->rc, 0xe0);
	for (long t = 0; t < n; t++)
	{
		EXTR* e = &extr[t];
		if (e->susp == 0x1e)
		{
			SPEC_TYPE* el = e->elem;
			if (el->code == 0x6e)
				continue;
			SPEC_TYPE* nx = el->next;
			SPEC_TYPE* pv = el->prev;
			Boolean deep = false;
			if (non_sub(el, x, y, h) == 0)
			{
				if (DeepFrom(el, pv, y, lim) || DeepFrom(el, nx, y, lim)
				 || (t > 0 && e[0].y - e[-1].y > lim)
				 || (t + 1 < n && e[0].y - e[1].y > lim))
					deep = true;
			}
			e->susp = (deep || sure == 1) ? 0x65 : 0;
		}
		if (e->susp == 0x32 && t < n - 1)
		{
			SPEC_TYPE* el = e->elem;
			SPEC_TYPE* el2 = e[1].elem;
			Boolean deep = false;
			if (non_sub(el, x, y, h) == 0 && el->code != 0x6e
			 && (DeepFrom(el, el->prev, y, lim) || DeepFrom(el, el->next, y, lim)
			  || (t > 0 && e[0].y - e[-1].y > lim))
			 && non_sub(el2, x, y, h) == 0 && el2->code != 0x6e
			 && (DeepFrom(el2, el2->prev, y, lim) || DeepFrom(el2, el2->next, y, lim)
			  || (t + 2 < n && e[1].y - e[2].y > lim)))
				deep = true;
			short c = (deep || sure == 1) ? 0x65 : 0;
			e[1].susp = c;
			e[0].susp = c;
			t++;
		}
	}
}


// Whether an element's neighbour of the other kind is within k of it in
// height.
static inline Boolean
NearOther(SPEC_TYPE* elem, SPEC_TYPE* other, UByte otherKind, short* y, long k)
{
	return other->mark == otherKind && HWRAbs(y[elem->ipoint0] - y[other->ipoint0]) < k;
}


// ROM 0x001bed40 glitch_to_inside__FP4EXTRiUcPsN32
// The glitches of one and two extrema that are really inside the letters
// (0x67): an extremum within k in height of its neighbour of the other
// kind, at its stroke's start or end, or more than k from the line's
// extremum beside it.  An i's dot's top (attr 5) and one put back (0x6e)
// are taken out of the glitch (0).  For the tops, a two-extremum glitch
// at the line's start before xStart or at its end past xEnd (the last
// two, the second's next a bottom) is taken out too.
void
glitch_to_inside(EXTR* extr, long n, UByte kind, short* y, long k, long xStart, long xEnd)
{
	short one = -1, two = -1;		// (the ROM leaves these unset for a kind other than 1 or 3, which it is never given)
	UByte other = 0;
	if (kind == 3)
	{
		one = 0x14;
		two = 0x28;
		other = 1;
	}
	else if (kind == 1)
	{
		one = 0x1e;
		two = 0x32;
		other = 3;
	}
	for (long t = 0; t < n; t++)
	{
		EXTR* e = &extr[t];
		if (e->susp == one && e->elem->code != 0x6e)
		{
			SPEC_TYPE* el = e->elem;
			SPEC_TYPE* pv = el->prev;
			SPEC_TYPE* nx = el->next;
			if (el->attr == 5)
			{
				e->susp = 0;
				continue;
			}
			if (NearOther(el, pv, other, y, k) || NearOther(el, nx, other, y, k)
			 || pv->mark == 0x10 || nx->mark == 0x20
			 || (t > 0 && HWRAbs(e[0].y - e[-1].y) > k)
			 || (t + 1 < n && HWRAbs(e[0].y - e[1].y) > k))
				e->susp = 0x67;
		}
		if (e->susp == two && n - 1 > t)
		{
			SPEC_TYPE* el = e->elem;
			SPEC_TYPE* el2 = e[1].elem;
			if (el->attr == 5 || el2->attr == 5 || el->code == 0x6e || el2->code == 0x6e
			 || (kind == 1 && t == n - 2 && el2->next->mark == 3 && xEnd <= e[1].x)
			 || (kind == 1 && t == 0 && xStart >= extr[0].x))
			{
				e[1].susp = 0;
				e[0].susp = 0;
			}
			else if ((NearOther(el, el->prev, other, y, k) || NearOther(el, el->next, other, y, k)
					  || el->next->mark == 0x20 || el->prev->mark == 0x10
					  || (t > 0 && HWRAbs(e[0].y - e[-1].y) > k))
				  && (NearOther(el2, el2->prev, other, y, k) || NearOther(el2, el2->next, other, y, k)
					  || el2->next->mark == 0x20 || el2->prev->mark == 0x10
					  || (t + 2 < n && HWRAbs(e[1].y - e[2].y) > k)))
			{
				e[1].susp = 0x67;
				e[0].susp = 0x67;
			}
			t++;
		}
	}
}


// Whether the top at extr[k] reaches high enough above the line to be an
// ascender: more than 55% of lim above `line`.
static inline Boolean
HighAbove(EXTR* e, short* line, long lim)
{
	return !(lim * 0x37 > (line[e->i] - e->y) * 100);
}


// ROM 0x001bf1c8 glitch_to_super_min__FP4EXTRiPsT2N23Ui
// The glitches above the line of tops (0x14, 0x28, 0x3c: one, two, three)
// made ascenders (0x66) when each is a real top of its letter (attr 1 or
// 5), a real ascender (non_super says no) and more than 55% of lim above
// `line` - or when sure (1) and each is a real top; else put back on the
// line (0).
void
glitch_to_super_min(EXTR* extr, long n, short* line, long lim, short* x, short* y, ULong sure)
{
	for (long t = 0; t < n; t++)
	{
		EXTR* e = &extr[t];
		if (e->susp == 0x14)
		{
			short c = 0;
			if (e->elem->attr == 1 || e->elem->attr == 5)
			{
				if (sure == 1 || (non_super(extr, t, x, y, line) == 0 && HighAbove(e, line, lim)))
					c = 0x66;
			}
			e->susp = c;
		}
		if (e->susp == 0x28)
		{
			if (n - 1 <= t)
				continue;
			short c = 0;
			if ((e[0].elem->attr == 1 || e[0].elem->attr == 5) && (e[1].elem->attr == 1 || e[1].elem->attr == 5))
			{
				if (sure == 1
				 || (non_super(extr, t, x, y, line) == 0 && non_super(extr, t + 1, x, y, line) == 0
				  && HighAbove(&e[0], line, lim) && HighAbove(&e[1], line, lim)))
					c = 0x66;
			}
			e[1].susp = c;
			e[0].susp = c;
			t++;
			e = &extr[t];
		}
		if (e->susp == 0x3c)
		{
			if (n - 2 <= t)
				continue;
			short c = 0;
			if ((e[0].elem->attr == 1 || e[0].elem->attr == 5) && (e[1].elem->attr == 1 || e[1].elem->attr == 5)
			 && (e[2].elem->attr == 1 || e[2].elem->attr == 5))
			{
				if (sure == 1
				 || (non_super(extr, t, x, y, line) == 0 && non_super(extr, t + 1, x, y, line) == 0
				  && non_super(extr, t + 2, x, y, line) == 0
				  && HighAbove(&e[0], line, lim) && HighAbove(&e[1], line, lim) && HighAbove(&e[2], line, lim)))
					c = 0x66;
			}
			e[2].susp = c;
			e[1].susp = c;
			e[0].susp = c;
			t += 2;
		}
	}
}


// ROM 0x001c0844 all_susp_extr__FP4EXTRiT2UcPsN32T5T2
// A suspicious extremum (0x66 a top above the line, 0x65 a bottom below
// it) next to one inside the letters (0x67): one of the two is put back
// on the line (ret_to_line), the one whose amplitude is the less like
// `mid` - unless one of them is under an eighth of `small`, which then
// goes, or (the tops) past `big` below `line`, which then stays; a bottom
// glitch at a stroke's start goes when the other's amplitude is under
// half as much again as mid.  The amplitudes of the bottoms are calc_ampl
// (the last one kept for the next pair), of the tops the depth below
// `line`.
void
all_susp_extr(EXTR* extr, long n, long unused, UByte kind, short* y, long mid, long unused2, long small, short* line, long big)
{
	const long kEighth = 8;
	short susp = 0;
	long a = 0;					// the amplitude before (callee-saved r9 in the ROM: set before it is read for kinds 1 and 3)
	long b = 0;					// and here
	if (kind == 1)
		susp = 0x66;
	else if (kind == 3)
		susp = 0x65;
	if (n <= 1)
		return;
	long most = mid + (mid >> 1);
	for (long t = 1; t < n; t++)
	{
		short c = extr[t].susp;
		long g, s;
		if (c == susp && extr[t - 1].susp == 0x67)
		{
			g = t - 1;
			s = t;
		}
		else if (c == 0x67 && extr[t - 1].susp == susp)
		{
			g = t;
			s = t - 1;
		}
		else
		{
			b = 0;
			continue;
		}
		SPEC_TYPE* el = extr[g].elem;
		long keep;				// the index passed to ret_to_line
		if (kind == 3)
		{
			if (el->prev->mark == 0x10 && most > calc_ampl(extr[s], y, kind))
			{
				ret_to_line(extr, n, t, s);
				continue;
			}
			a = (b != 0) ? b : calc_ampl(extr[t - 1], y, kind);
			b = calc_ampl(extr[t], y, kind);
		}
		else if (kind == 1)
		{
			a = line[extr[t - 1].i] - extr[t - 1].y;
			b = line[extr[t].i] - extr[t].y;
			if (big > 0 && c == susp && b >= big)
			{
				ret_to_line(extr, n, t, t - 1);
				continue;
			}
			if (big > 0 && extr[t - 1].susp == susp && a >= big)
			{
				ret_to_line(extr, n, t, t);
				continue;
			}
		}
		if (LMul(a, kEighth) <= small)
			keep = t;
		else if (LMul(b, kEighth) <= small)
			keep = t - 1;
		else if (HWRAbs(b - mid) >= HWRAbs(a - mid))
			keep = t - 1;
		else
			keep = t;
		ret_to_line(extr, n, t, keep);
	}
}


// ROM 0x001c1960 num_bord_correction__FP4EXTRPiiUcT3PsT6
// The line of a word of figures: the extrema of a figure's own arcs
// (attr 0xce) taken out first; then, on a fast pass, a gap or one- or
// two-extremum glitch (below for the bottoms, above for the tops) at an
// extremum of attr 0xca taken out as well - its neighbours of the other
// kind marked 0xce - and on a second pass (gaps and glitches found again)
// the glitches the other way taken out as inside the figures (0x67).
// ==> what the second delete_line_extr answered.
long
num_bord_correction(EXTR* extr, long* n, long mode, UByte kind, long lim, short* line, short* y)
{
	short out = 0;
	UByte other = 0;
	if (kind == 3)
	{
		out = 0x65;
		other = 1;
	}
	else if (kind == 1)
	{
		out = 0x66;
		other = 3;
	}
	for (long t = 0; t < *n; t++)
		extr[t].susp = (extr[t].elem->attr == 0xce) ? out : 0;
	delete_line_extr(extr, n, out);
	find_gaps_in_line(extr, *n, mode, lim, kind, 0, 0x7fff, line, y, 1, 0);
	find_glitches_in_line(extr, *n, lim, kind, 0, 0x7fff, line, nil, nil, 1, 1, 0);
	for (long t = 0; t < *n; t++)
	{
		Boolean one = (kind == 3 && extr[t].susp == 0x1e) || (kind == 1 && extr[t].susp == 0x14);
		if (one)
		{
			SPEC_TYPE* el = extr[t].elem;
			if (el->attr == 0xca)
			{
				extr[t].susp = out;
				if (el->prev->mark == other)
					el->prev->attr = 0xce;
				if (el->next->mark == other)
					el->next->attr = 0xce;
			}
		}
		Boolean two = (kind == 3 && extr[t].susp == 0x32) || (kind == 1 && extr[t].susp == 0x28);
		if (two && t < *n - 1)
		{
			if (extr[t].elem->attr == 0xca && extr[t + 1].elem->attr == 0xca)
			{
				for (long k = t; k <= t + 1; k++)
				{
					extr[k].susp = out;
					SPEC_TYPE* el = extr[k].elem;
					if (el->prev->mark == other)
						el->prev->attr = 0xce;
					if (el->next->mark == other)
						el->next->attr = 0xce;
				}
			}
			t++;
		}
	}
	long result = delete_line_extr(extr, n, out);
	for (long t = 0; t < *n; t++)
		extr[t].susp = 0;
	find_gaps_in_line(extr, *n, mode, lim, kind, 0, 0x7fff, line, y, 0, 0);
	find_glitches_in_line(extr, *n, lim, kind, 0, 0x7fff, line, nil, nil, 1, 0, 0);
	for (long t = 0; t < *n; t++)
	{
		if ((kind == 3 && extr[t].susp == 0x14) || (kind == 1 && extr[t].susp == 0x1e))
			extr[t].susp = 0x67;
	}
	delete_line_extr(extr, n, 0x67);
	return result;
}


// One pass of bord_correction's: the gaps and glitches found and made
// descenders, ascenders or inside, and the suspicious ones next to inside
// ones settled.
static void
BordPass(low_type* low, EXTR* extr, long n, long mode, UByte kind, long mid, long lim, long subLim, long small,
		 long xStart, long xEnd, long neighbour, UByte useNeighbour, short* line, long superLim, long big,
		 ULong superSure, ULong subSure, long maxWidth, short* x, short* y, long h, long e6)
{
	find_gaps_in_line(extr, n, mode, lim, kind, xStart, xEnd, line, y, 0, 0);
	find_glitches_in_line(extr, n, lim, kind, xStart, xEnd, line, x, y, maxWidth, 0, 0);
	if (kind == 3)
		glitch_to_sub_max(low, extr, n, subLim, subSure);
	else if (kind == 1)
	{
		glitch_to_super_min(extr, n, line, superLim, x, y, superSure);
		long count = 0, most = 0, sum = 0;
		for (long t = 0; t < n; t++)
		{
			if (extr[t].susp == 0x66)
				count++;
			else
			{
				long d = line[extr[t].i] - extr[t].y;
				if (d >= most)
					most = d;
				sum += d;
			}
		}
		if (count < n)
			sum = sum / (n - count);
		if (count > 0)
			super_min_to_line(extr, n, line, most, sum, &count);
	}
	glitch_to_inside(extr, n, kind, y, subLim, xStart, xEnd);
	if (useNeighbour == 1 && neighbour != 0)
		spec_neibour_extr(extr, n, kind, neighbour);
	else
	{
		long d = big - h;
		if (neibour_susp_extr(extr, n, kind, line, d) == 1)
			all_susp_extr(extr, n, mode, kind, y, mid, e6, small, line, d);
	}
}


// ROM 0x001c1d04 bord_correction__FP8low_typeP4EXTRPiiUcN74T5PsN24UiUi
// A line of extrema (the bottoms, kind 3, or the tops, kind 1) cleaned of
// what is not on it: its gaps and glitches found, the glitches made
// descenders (0x65) or ascenders (0x66) - those taken out of the array,
// their elements coded so - or things inside the letters (0x67, taken out
// the same way), and the whole done twice when anything was taken out
// the first time.  ==> what the last delete_line_extr answered (0 for
// fewer than two extrema or nothing taken out).
long
bord_correction(low_type* low, EXTR* extr, long* nPtr, long mode, UByte kind, long mid, long lim, long subLim,
				long small, long xStart, long xEnd, long neighbour, UByte useNeighbour, short* line,
				long superLim, long big, ULong superSure, ULong subSure)
{
	long result = 0;
	short* x = low->fBuffers[0].ptr;
	short* y = low->fBuffers[1].ptr;
	long n = *nPtr;
	long h = (short) RCGetH(low->rc, 0xe0);
	long e6 = (short) RCGetH(low->rc, 0xe6);
	short out = 0;
	long maxWidth = 0;			// (the ROM leaves these unset for a kind other than 1 or 3, which it is never given)
	if (kind == 3)
	{
		out = 0x65;
		maxWidth = 1;
	}
	else if (kind == 1)
	{
		out = 0x66;
		maxWidth = 2;
	}
	for (long t = 0; t < n; t++)
		extr[t].susp = 0;
	if (n < 2)
		return 0;
	BordPass(low, extr, n, mode, kind, mid, lim, subLim, small, xStart, xEnd, neighbour, useNeighbour, line,
			 superLim, big, superSure, subSure, maxWidth, x, y, h, e6);
	long count = 0;
	for (long t = 0; t < n; t++)
		if (extr[t].susp == out)
			count++;
	if (count > 0)
	{
		for (long t = 0; t < n; t++)
		{
			if (extr[t].susp != out)
				extr[t].susp = 0;
			else
				extr[t].elem->code = (UByte) out;
		}
		result = delete_line_extr(extr, nPtr, out);
		n = *nPtr;
		BordPass(low, extr, n, mode, kind, mid, lim, subLim, small, xStart, xEnd, neighbour, useNeighbour, line,
				 superLim, big, superSure, subSure, maxWidth, x, y, h, e6);
	}
	long inside = 0;
	for (long t = 0; t < n; t++)
		if (extr[t].susp == 0x67)
			inside++;
	if (inside > 0)
	{
		for (long t = 0; t < n; t++)
		{
			if (extr[t].susp != 0x67)
				extr[t].susp = 0;
			else
				extr[t].elem->code = 0x67;
		}
		result = delete_line_extr(extr, nPtr, 0x67);
	}
	return result;
}
