/*
	File:		LowBorders.cpp

	Contains:	The cursive reader's low level: the two lines the
				base-line finder settles on - an upper and a lower
				border under every point of the trace - and what it
				reports of them: the medians of their heights and of
				the letters' height between them, and the engine's
				record of them at nine places along the word.
				See LowLevel.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	The borders are arrays of a y for every point of the trace (nought
	at a pen-up), indexed as the trace is; `order` (fill_i_point) lists
	the points in order of x, which is how they are walked across the
	word.  The engine's parameters (rc) say how sure the caller is of a
	line it already has: +0xe2 its letters' height, +0xe4 its base,
	+0xe6 and +0xe8 how sure (out of 100) of each; 50 and over is sure.
*/

#include "LowLevel.h"
#include "ParaGraph.h"
#include "XrDomains.h"


// ROM 0x001c4ce0 SpecBord__FP8low_typePsT2PiN34UiP4EXTRi
// Level borders, for a word too short or too odd to find its own: the
// lower at `lower`, the upper at `upper`, from what the caller is sure
// of - both (its base and its height), or its height alone (about the
// middle of the box when the writing is smaller than it, else about the
// median of the n bottoms extr), or neither: the median of the bottoms
// (the box's bottom for fewer than two) with the upper two thirds of the
// box above it (no higher than the box's top), or, for `wide`, the box
// widened by its own height (at least twelve) each way.  *lowerY and
// *upperY are the two, *height their difference, *count what
// fill_i_point answers (into buffer 3).  The bottoms' y are gathered in
// dLine on the way, which is then filled.
void
SpecBord(low_type* low, short* dLine, short* uLine, long* lowerY, long* upperY, long* height, long* count, ULong wide, EXTR* extr, long n)
{
	short* order = low->fBuffers[3].ptr;
	rc_type* rc = low->rc;
	long lo = 0, up = 0;
	long sureH = (short) RCGetH(rc, 0xe6);
	if (sureH >= 0x32 && (short) RCGetH(rc, 0xe8) >= 0x32)
	{
		lo = (short) RCGetH(rc, 0xe4);
		up = lo - (short) RCGetH(rc, 0xe2);
	}
	if (sureH >= 0x32 && (short) RCGetH(rc, 0xe8) < 0x32)
	{
		long bottom = low->fBox.bottom;
		long top = low->fBox.top;
		long h = (short) RCGetH(rc, 0xe2);
		if (bottom - top < h)
		{
			long mid = (bottom >> 1) + (top >> 1);
			lo = mid + (h >> 1);
			up = mid - (h >> 1);
		}
		else
		{
			if (n > 1)
			{
				for (long i = 0; i < n; i++)
					dLine[i] = extr[i].y;
				lo = calc_mediana(dLine, n);
			}
			else
				lo = bottom;
			up = lo - (short) RCGetH(rc, 0xe2);
		}
	}
	if ((short) RCGetH(rc, 0xe6) < 0x32)
	{
		if (wide == 0)
		{
			if (n > 1)
			{
				for (long i = 0; i < n; i++)
					dLine[i] = extr[i].y;
				lo = calc_mediana(dLine, n);
			}
			else
				lo = low->fBox.bottom;
			long top = low->fBox.top;
			up = lo - ((low->fBox.bottom - top) * 2 + 1) / 3;
			if (up < top)
				up = top;
		}
		else
		{
			long bottom = low->fBox.bottom;
			long top = low->fBox.top;
			long d = bottom - top;
			if (d <= 0xc)
				d = 0xc;
			lo = bottom + d;
			up = top - d;
		}
	}
	for (long i = 0; i < low->fII; i++)
	{
		dLine[i] = (low->fY[i] != -1) ? (short) lo : 0;
		uLine[i] = (low->fY[i] != -1) ? (short) up : 0;
	}
	*upperY = up;
	*lowerY = lo;
	*height = lo - up;
	*count = fill_i_point(order, low);
}


// ROM 0x001c3660 calc_med_heights__FP8low_typeP4EXTRT2PsN24iN27PiPiPi
// The medians of the letters' height (lower - upper), of the lower border
// and of the upper, across the stretch of the word the two lines of
// extrema (a, na; b, nb) span - the points in order of x from the first
// at the leftmost extremum's x to the one at the rightmost's - or across
// all `total` points when either line has fewer than two.  Over fifty
// points, fifty are sampled at even steps of x (each the point nearest
// its step).  ==> 0, 1 for no memory.
long
calc_med_heights(low_type* low, EXTR* a, EXTR* b, short* upper, short* lower, short* order, long na, long nb, long total,
				 long* height, long* upperMed, long* lowerMed)
{
	short* x = low->fX;
	long first, last, n;
	if (na < 2 || nb < 2)
	{
		first = 0;
		last = total - 1;
		n = total;
	}
	else
	{
		long xmin = a[0].x;
		if (xmin >= b[0].x)
			xmin = b[0].x;
		long xmax = a[na - 1].x;
		if (xmax <= b[nb - 1].x)
			xmax = b[nb - 1].x;
		last = 0;
		while (x[order[last]] < xmin)
			last++;
		first = last;
		while (x[order[last]] < xmax)
			last++;
		n = last - first + 1;
	}
	long x0 = x[order[first]];
	long x1 = x[order[last]];
	short* values = (short*) HWRMemoryAlloc(((n <= 0x32) ? 0x32 : n) * 2);
	if (values == nil)
		return 1;
	long m = (n <= 0x32) ? n : 0x32;
	for (long pass = 0; pass < 3; pass++)
	{
		if (n < 0x32)
		{
			for (long k = 0; k < n; k++)
			{
				long p = order[first + k];
				if (pass == 0)
					values[k] = (short) ((UShort) lower[p] - (UShort) upper[p]);
				else if (pass == 1)
					values[k] = lower[p];
				else
					values[k] = upper[p];
			}
		}
		else
		{
			long at = first;
			for (long s = 0; s < 0x32; s++)
			{
				long target = s * (x1 - x0) / 0x32 + x0;
				long j = at;
				while (j <= last && x[order[j]] < target)
					j++;
				if (j > first && x[order[j]] - target > target - x[order[j - 1]])
					j--;
				at = j;
				long p = order[j];
				if (pass == 0)
					values[s] = (short) ((UShort) lower[p] - (UShort) upper[p]);
				else if (pass == 1)
					values[s] = lower[p];
				else
					values[s] = upper[p];
			}
		}
		long med = calc_mediana(values, m);
		if (pass == 0)
			*height = med;
		else if (pass == 1)
			*lowerMed = med;
		else
			*upperMed = med;
	}
	HWRMemoryFree((Ptr) values);
	return 0;
}


// ROM 0x001c4a4c FillRCNB__FPsiP8low_typeN21
// The engine's record of the borders (rc +0x98, ten pairs of bytes): at
// the word's first point in x and then at nine steps of a ninth of its
// width, the first point (in order of x, from where the last was found)
// at or past the step that is not a pen-up and has a border, its upper
// and lower border's height in the box, each clamped to one pixel and
// the box's height and scaled to 255.  ==> 0, 1 for no points.
long
FillRCNB(short* order, long n, low_type* low, short* upper, short* lower)
{
	long left = low->fBox.left;
	long top = low->fBox.top;
	long w = low->fBox.right - left;
	if (w == 0)
		w = 1;
	long h = low->fBox.bottom - top;
	if (h == 0)
		h = 1;
	UByte* nb = RCByte(low->rc, 0x98);
	if (order == nil || n <= 0)
		return 1;
	long p = order[0];
	long u = upper[p] - top;
	if (u < 1)
		u = 1;
	if (u > h)
		u = h;
	long d = lower[p] - top;
	if (d < 1)
		d = 1;
	if (d > h)
		d = h;
	nb[0] = (UByte) (u * 0xff / h);
	nb[1] = (UByte) (d * 0xff / h);
	long from = 0;
	for (long k = 1; k < 10; k++)
	{
		long at = k * w / 9 + left;
		for (long j = from; j < n; j++)
		{
			p = order[j];
			if (low->fTrace[p].y != -1 && low->fTrace[p].x >= at && upper[p] != 0)
			{
				u = upper[p] - top;
				if (u < 1)
					u = 1;
				if (u > h)
					u = h;
				d = lower[p] - top;
				if (d < 1)
					d = 1;
				if (d > h)
					d = h;
				nb[k * 2] = (UByte) (u * 0xff / h);
				nb[k * 2 + 1] = (UByte) (d * 0xff / h);
				from = j;
				break;
			}
		}
	}
	return 0;
}


// Whether the caller is sure enough of its line (rc +0xe8 and +0xe6) that
// a count of n asks for a correction: n up to 3 when both are 90 or more,
// up to 2 at 75, up to 1 at 55.
static Boolean
SureEnough(long sureA, long sureB, long n)
{
	if (sureA >= 0x5a && sureB >= 0x5a && n <= 3)
		return true;
	if (sureA >= 0x4b && sureB >= 0x4b && n <= 2)
		return true;
	if (sureA >= 0x37 && sureB >= 0x37 && n <= 1)
		return true;
	return false;
}


// The first extremum of a mark coded 0x64 before elem (back) or after it:
// how far it lies from v, in the direction s; 0 for none.
static long
NearestLine(SPEC_TYPE* elem, UByte mark, short* yy, long v, long s, Boolean back)
{
	for (SPEC_TYPE* p = back ? elem->prev : elem->next; p != nil; p = back ? p->prev : p->next)
		if (p->mark == mark && p->code == 0x64)
			return s * (v - yy[p->ipoint0]);
	return 0;
}


// The walk back from an extremum put back on the line: the first of its
// kind with a code before it (or the stroke's start) is coded 0x6e too.
static void
MarkBack(SPEC_TYPE* elem, UByte mark)
{
	SPEC_TYPE* p = elem->prev;
	while (p->mark != 0x10 && (p->mark != mark || p->code == 0))
		p = p->prev;
	p->code = 0x6e;
}


// ROM 0x001c24bc line_pos_mist__FP8low_typeiN42P4EXTRPiT8PsPsUc
// How badly the two borders (upper, lower: the lines under every point)
// fit the word, as a penalty.  Every top and bottom is checked against
// them: one coded 0x6f (struck out) looked at again (a top to 0x66 above
// or 0x67 inside, a bottom to 0x65 or 0x67), one put back (0x6e) recoded
// 0x64, a run of extrema inside the letters opened out (0x6e) where
// nothing closes over them, and a line extremum (0x64/0x6e) beyond the
// other border struck out (0x6f, four points) or, too near it, struck
// out when a neighbour on the line is far from it (0x6f, one).  A stroke
// whose tops or bottoms run inside has its highest top and lowest bottom
// put back on the line (a point each), or - a comma or a straight stroke -
// is noted when it hangs below the lower border (a hundred points at the
// end if nothing else sticks out).  Then the counts of tops above the
// upper border and bottoms below the lower, against the ascenders nUp and
// descenders nDown expected, and the caller's own line (rc +0xe2..+0xe8)
// against these, set *upShift/*downShift to 1 or -1 and add to the
// penalty.  upperY/lowerY/height are the borders' medians and the
// letters' height, `bottoms` the line of bottoms (its nDown-th's x
// consulted), `wide` a word with more than one kind of stroke.
// ==> the penalty.
long
line_pos_mist(low_type* low, long upperY, long lowerY, long height, long nUp, long nDown, EXTR* bottoms,
			  long* upShift, long* downShift, short* upper, short* lower, UByte wide)
{
	long above = 0, inside = 0, belowL = 0, aboveL = 0;
	long strokes = 0, counted = 0, tops = 0, nBottoms = 0;
	short* x = low->fBuffers[0].ptr;
	short* map = low->fBuffers[2].ptr;
	short* y = low->fBuffers[1].ptr;
	rc_type* rc = low->rc;
	long h = (short) RCGetH(rc, 0xe0);
	long base = (short) RCGetH(rc, 0xe4);
	long sureBase = (short) RCGetH(rc, 0xe8);
	long hgt = (short) RCGetH(rc, 0xe2);
	long sureHgt = (short) RCGetH(rc, 0xe6);
	long penalty = 0;
	Boolean hangs = false;
	long deviation = 0;
	for (long i = 0; i < low->fII; i++)
	{
		long v = low->fY[i];
		if (lower[i] - v > deviation)
			deviation = lower[i] - v;
		if (v - upper[i] > deviation)
			deviation = v - upper[i];
	}
	if (height * 8 <= deviation)
		penalty += 5;
	long start = 0;
	long downRun = 0, upRun = 0;			// the runs inside the letters (-1: broken by one on the line)
	long highest = 0x7fff, lowest = 0;		// the stroke's highest top and lowest bottom
	UByte openTop = 0, openBottom = 0;
	const long near = (height * 5) * 2;		// a neighbour is far from the line past a tenth of the height (x100)
	for (SPEC_TYPE* elem = low->fSpecl; elem != nil; elem = elem->next)
	{
		if (elem->mark == 0x10)
		{
			counted++;
			start = elem->iBeg;
			downRun = 0;
			highest = 0x7fff;
			lowest = 0;
			upRun = 0;
			strokes++;
			openTop = 0;
			openBottom = 0;
			continue;
		}
		if (elem->mark == 1)
		{
			tops++;
			long v = y[elem->ipoint0];
			long m = map[elem->ipoint0];
			if ((elem->attr == 1 || elem->attr == 5) && highest >= v)
				highest = v;
			Boolean count = false;
			UByte c = elem->code;
			if (c == 0x6e)
				elem->code = 0x64;
			else if (c == 0x6f)
			{
				if (upper[m] < v)
				{
					elem->code = 0x67;
					count = true;
				}
				else if (upper[m] > v)
				{
					elem->code = 0x66;
					count = true;
				}
			}
			else if (c == 0x66 || c == 0x67)
				count = true;
			if (count || elem->attr == 3)
			{
				if (upper[m] > v)
					above++;
				if (upper[m] < v)
					inside++;
			}
			if (elem->code == 0x67)
			{
				if (lower[m] > v)
				{
					if (openTop > 0 && extrs_open(low, elem, 1, 2) == 1)
					{
						elem->code = 0x6e;
						MarkBack(elem, 1);
						penalty++;
						continue;
					}
					openTop = (UByte) (openTop + 1);
				}
				if (upRun >= 0)
					upRun++;
			}
			c = elem->code;
			if (c == 0x64 || c == 0x66 || c == 0x6e || (c == 0 && v <= upperY))
			{
				openTop = 0;
				upRun = -1;
			}
			if ((RCGetH(rc, 0x90) & 0x800) != 0 && c == 0x66)
			{
				if (counted <= 2)
					continue;
				SPEC_TYPE* end = elem;
				while (end->mark != 0x20)
					end = end->next;
				_RECT box;
				GetTraceBox(x, y, (short) start, end->iEnd, &box);
				if (low->fBox.right - h > box.right)
				{
					penalty++;
					elem->code = 0x6e;
				}
			}
			if (elem->code != 0x64 && elem->code != 0x6e)
				continue;
			long l = lower[m];
			if (l < v)
			{
				elem->code = 0x6f;
				penalty += 4;
				continue;
			}
			long d = l - v;
			if (d * 8 < deviation || (d * 2 < height && elem->attr != 5))
			{
				if (NearestLine(elem, 1, y, v, 1, true) * 100 >= near
				 || NearestLine(elem, 1, y, v, 1, false) * 100 >= near)
				{
					elem->code = 0x6f;
					penalty++;
				}
			}
			continue;
		}
		if (elem->mark == 3)
		{
			nBottoms++;
			long v = y[elem->ipoint0];
			long m = map[elem->ipoint0];
			if (elem->attr == 1 && lowest <= v)
				lowest = v;
			UByte c = elem->code;
			Boolean reset = false;
			if (c == 0x6e)
			{
				elem->code = 0x64;
				reset = true;
			}
			else
			{
				Boolean count = false;
				if (c == 0x6f)
				{
					if (lower[m] > v)
						elem->code = 0x67;
					else if (lower[m] == v)
						continue;
					else
						elem->code = 0x65;
					count = true;
				}
				else if (c == 0x65 || c == 0x67)
					count = true;
				if (count)
				{
					if (lower[m] < v)
						belowL++;
					if (lower[m] > v)
						aboveL++;
				}
				if (elem->code == 0x67)
				{
					if (upper[m] < v)
					{
						if (openBottom > 1 && extrs_open(low, elem, 3, 2) == 1)
						{
							elem->code = 0x6e;
							MarkBack(elem, 3);
							penalty++;
							if (openBottom == 2)
								penalty++;
							continue;
						}
						openBottom = (UByte) (openBottom + 1);
					}
					if (downRun >= 0)
						downRun++;
				}
				c = elem->code;
				if (c == 0x64 || c == 0x65 || c == 0x6e)
					reset = true;
				else if (c == 0)
				{
					if (v < lowerY)
						continue;
					reset = true;
				}
			}
			if (reset)
			{
				openBottom = 0;
				downRun = -1;
			}
			if (elem->code != 0x64 && elem->code != 0x6e)
				continue;
			long u = upper[m];
			if (u > v)
			{
				elem->code = 0x6f;
				penalty += 4;
				continue;
			}
			long d = v - u;
			if (d * 8 >= deviation && d * 2 >= height)
				continue;
			if (NearestLine(elem, 3, y, v, -1, true) * 100 >= near
			 || NearestLine(elem, 3, y, v, -1, false) * 100 >= near)
			{
				elem->code = 0x6f;
				penalty++;
			}
			continue;
		}
		if (elem->mark != 0x20 || elem->prev->mark == 0x10 || (upRun <= 0 && downRun <= 0))
			continue;
		long end = elem->iEnd;
		_RECT box;
		GetTraceBox(x, y, (short) start, end, &box);
		long bh = box.bottom - box.top;
		if (low->fBox.right - h < box.right && bh < (height >> 1) && bh + (bh >> 1) > box.right - box.left)
			continue;
		if (straight_stroke(start, end, x, y, 5) == 1 || curve_com_or_brkt(low, elem, start, end, 5, 0x10) != 0)
		{
			if (downRun > 0 && bh >= box.right - box.left)
			{
				SPEC_TYPE* pv = elem->prev;
				if (pv->mark == 3 && lower[map[pv->ipoint0]] - y[pv->ipoint0] > (height + 2) >> 2)
					hangs = true;
			}
			continue;
		}
		long nUpPut = 0, nDownPut = 0;
		long spread = lowest - highest;
		long quarter = (lowerY - upperY + 2) >> 2;
		for (SPEC_TYPE* p = elem->prev; p->mark != 0x10; p = p->prev)
		{
			if (upRun > 0 && p->mark == 1 && y[p->ipoint0] == highest
			 && (upRun > 2 || (wide > 0 && spread > quarter) || (lowerY * 4) / 5 + upperY / 5 > highest))
			{
				p->code = 0x6e;
				nUpPut++;
			}
			if (downRun > 0 && p->mark == 3 && y[p->ipoint0] == lowest
			 && (downRun > 2 || (wide > 0 && spread > quarter)
			  || ((upperY * 3) / 4 + lowerY / 4 < lowest && highest > upperY)))
			{
				p->code = 0x6e;
				nDownPut++;
			}
		}
		penalty += nUpPut + nDownPut;
	}
	if (above > 5 && nUp + 1 < above)
	{
		*upShift = -1;
		penalty = above - nUp + penalty - 1;
	}
	if (low->fBox.right - low->fBox.left > low->fBox.bottom - low->fBox.top || strokes > 1)
	{
		if (aboveL > nDown && nDown + 1 >= belowL)
		{
			*downShift = 1;
			penalty += aboveL - nDown;
		}
		if (inside > nUp && nUp + 1 >= above)
		{
			*upShift = 1;
			penalty += inside - nUp;
		}
		if (nDown + 1 < belowL && aboveL <= nDown)
		{
			*downShift = -1;
			penalty = belowL - nDown + penalty - 1;
		}
		if (nBottoms >= 5 && tops >= 5 && aboveL >= 2
		 && low->fBox.right - bottoms[nDown - 1].x > (low->fBox.right - low->fBox.left) >> 1)
			penalty += 2;
	}
	if (base - hgt > lowerY && SureEnough(sureBase, sureHgt, nDown))
	{
		penalty += 3;
		*downShift = -1;
	}
	if ((hgt * 2 + 2) / 5 > height && *downShift != -1 && SureEnough(sureBase, sureHgt, nUp))
	{
		penalty += 3;
		*upShift = -1;
	}
	if (base + hgt < lowerY && SureEnough(sureBase, sureHgt, nDown))
	{
		penalty += 3;
		*downShift = 1;
	}
	// (the ROM asks nDown here, where the test before this pair asked nUp)
	if (hgt * 2 + (hgt >> 1) < height && SureEnough(sureBase, sureHgt, nDown))
	{
		penalty += 3;
		*upShift = 1;
	}
	if (upperY > base
	 && ((sureBase >= 0x5a && (nDown <= 3 || nUp <= 3))
	  || (sureBase >= 0x4b && (nDown <= 2 || nUp <= 2))
	  || (sureBase >= 0x37 && (nDown <= 1 || nUp <= 1))))
	{
		penalty += 5;
		*upShift = -1;
	}
	if (hangs && nUp < 6 && nDown < 6 && above == 0 && belowL == 0)
		penalty += 100;
	return penalty;
}


// The number of EXTRs in each of transfrmN's four arrays (ROM 0x320 bytes
// of 0x10-byte EXTRs).
const long kTransfrmExtr = 50;


// Every point's border moved: the lower up by at most q, the upper down
// by as much, in proportion to how far apart they are over boxH.
static void
NarrowBorders(low_type* low, short* lower, short* upper, long q, long boxH)
{
	long half = boxH / 2;
	for (long i = 0; i < low->fII; i++)
	{
		if (low->fY[i] == -1)
			continue;
		long lo = lower[i];
		long d = ((lo - upper[i]) * q + half) / boxH;
		if (d > q)
			d = q;
		lower[i] = (short) (lo - d);
		upper[i] = (short) ((UShort) upper[i] + d);
	}
}


// ROM 0x001baaf8 transfrmN__FP8low_type
// The base-line finder: two borders under every point of the trace -
// the lower (the letters' feet) and the upper (the top of the small
// letters) - found from the word's bottoms and tops, and the trace's y
// (and x) then rescaled against them, so that 0x2796 is the upper border
// and 0x27e6 the lower (80 units the height between), x 80 units to that
// height from the box's left (+0x50).  The engine's parameters are told
// the height (rc +0xea), the lower border's median (+0xec) and how sure
// the finder is of each (+0xee, +0xf0: 45 to 90 by how many extrema it
// had and how little it had to put right), and the borders at ten places
// (FillRCNB).  The way there:
//
//   - the strokes classified (classify_strokes, or for figures -
//     rc +0x94 = 0x20 - classify_num_strokes) and the letters' median and
//     mean height measured;
//   - the bottoms and the tops gathered into two lines (extract_all_extr)
//     and cleaned (bord_correction: the descenders, ascenders and things
//     inside the letters taken out), the lower border smoothed under the
//     bottoms and the upper under the tops, and the medians taken;
//   - line_pos_mist's penalty for how badly they fit; a word with gaps or
//     glitches, or any penalty, goes round again on a second pair of
//     arrays with the first pass's findings, and the better of the two is
//     kept;
//   - a word that turns out to be figures goes round again as figures;
//   - a word that will not do (no extrema, a penalty of a hundred, the
//     borders crossing, a height under twelve or unlike the caller's) gets
//     level borders from SpecBord instead;
//   - borders too far apart for a short word brought together, too near
//     pushed apart, and the ends of the lines extended where the borders
//     are narrow there (correct_narrow_ends).
//
// ==> 0, 1 for a failure (no memory, too many extrema, no points).
long
transfrmN(low_type* low)
{
	long result = 1;
	long penalty = 0;
	rc_type* rc = low->rc;
	short* x = low->fBuffers[0].ptr;
	short* y = low->fBuffers[1].ptr;
	short* map = low->fBuffers[2].ptr;
	short* order = low->fBuffers[3].ptr;
	long boxH = low->fBox.bottom - low->fBox.top;
	long stem = 0, tall = 0, strokes = 0;
	ULong simple = 0;
	long mid = -1;
	long superLim = 0;
	long upShift = 0, downShift = 0;
	UByte pass = 0;
	ULong superSure = 0, subSure = 0;
	long amplMed = 0, amplMean = 0, amplMax = 0;
	long nTops = 0, nBottoms = 0, allTops = 0, allBottoms = 0;
	short shift = 0;
	long xStartB = 0, xEndB = 0, xStartT = 0, xEndT = 0;
	long height = 0, upperMed = 0, lowerMed = 0, total = 0;
	long width = 0;							// the smoothing window
	long bordResult = 0, subResult = 0;
	long mist = 0, firstMist = 0;
	EXTR* savedB = nil;
	EXTR* savedT = nil;
	long savedNT = 0, savedNB = 0;
	long wideArg = 0, nArg = 0;
	EXTR* extrArg = nil;
	UByte text = ((short) RCGetH(rc, 0x92) != 2 && (RCGetH(rc, 0x90) & 0x10) != 0) ? 0x10 : 0x20;
	RCSetH(rc, 0x94, text);
	// DEVIATION: the ROM's arrays are 0x320 bytes of 0x10-byte EXTRs; the
	// host's EXTR holds a pointer, so each is kTransfrmExtr of the host's.
	const ULong arrayBytes = kTransfrmExtr * sizeof(EXTR);
	EXTR* block = nil;
	EXTR* bottoms;
	EXTR* tops;
	EXTR* bottoms2;
	EXTR* tops2;
	short* lower;
	short* upper;
	if (!((short) RCGetH(rc, 0xe6) == 100 && (short) RCGetH(rc, 0xe8) == 100) && RCGetH(rc, 0x94) == 0x10)
	{
		short* ampl = (short*) HWRMemoryAlloc(200);
		if (ampl == nil)
			return result;
		for (SPEC_TYPE* p = low->fSpecl; p != nil; p = p->next)
			if (p->mark == 3 || p->mark == 1)
			{
				p->attr = 1;
				p->code = 0;
			}
		long nAmpl = 0;
		if (extract_ampl(low, ampl, &nAmpl) == 1)
			goto done;
		amplMed = (nAmpl > 0) ? calc_mediana(ampl, nAmpl) : boxH;
		amplMax = 0;
		for (long i = 0; i < nAmpl; i++)
			if (ampl[i] > amplMax)
				amplMax = ampl[i];
		strokes = classify_strokes(low, amplMed, amplMax, nAmpl, &stem, &tall, &simple);
		if (extract_ampl(low, ampl, &nAmpl) == 1)
			goto done;
		amplMed = (nAmpl > 0) ? calc_mediana(ampl, nAmpl) : boxH;
		amplMean = (nAmpl > 0) ? calc_average(ampl, nAmpl) : boxH;
		HWRMemoryFree((Ptr) ampl);
	}
	block = (EXTR*) HWRMemoryAlloc(arrayBytes * 4 + low->fII * 4);
	if (block == nil)
		goto done;
	bottoms = block;
	tops = (EXTR*) ((char*) block + arrayBytes);
	bottoms2 = (EXTR*) ((char*) block + arrayBytes * 2);
	tops2 = (EXTR*) ((char*) block + arrayBytes * 3);
	lower = (short*) ((char*) block + arrayBytes * 4);
	upper = lower + low->fII;
	if ((short) RCGetH(rc, 0xe8) == 100 && (short) RCGetH(rc, 0xe6) == 100)
	{
		penalty += 100;
		goto specBordNone;
	}

again:
	memset(bottoms, 0, arrayBytes * 2);
	memset(lower, 0, low->fII * 4);
	if (RCGetH(rc, 0x94) == 0x20)
	{
		strokes = classify_num_strokes(low, &amplMed);
		if (extract_num_extr(low, 3, bottoms, &allBottoms) == 1)
			goto done;
		nBottoms = allBottoms;
		if (extract_num_extr(low, 1, tops, &allTops) == 1)
			goto done;
		nTops = allTops;
		if (nTops == 0 || nBottoms == 0)
			goto defis;
		sort_extr(bottoms, nBottoms);
		sort_extr(tops, nTops);
		num_bord_correction(bottoms, &nBottoms, allBottoms, 3, amplMed, lower, y);
	}
	else
	{
		if (extract_all_extr(low, 3, bottoms, &allBottoms, &nBottoms, &shift) == 1)
			goto done;
		if (extract_all_extr(low, 1, tops, &allTops, &nTops, &shift) == 1)
			goto done;
		if (nTops == 0 || nBottoms == 0)
			goto defis;
		sort_extr(bottoms, nBottoms);
		xStartB = bottoms[0].x;
		xEndB = bottoms[nBottoms - 1].x;
		sort_extr(tops, nTops);
		xStartT = tops[0].x;
		xEndT = tops[nTops - 1].x;
		long n = nBottoms;
		if (n == 0)
			n = 1;
		if (strokes > 3 && simple == 1)
		{
			stem = 0;
			tall = 0;
		}
		long span = (n <= 1) ? low->fBox.right - low->fBox.left : bottoms[n - 1].x - bottoms[0].x;
		long tenths = n * 10;
		mid = (span * 14) / tenths;
		if (span < boxH)
		{
			if ((short) RCGetH(rc, 0xe6) >= 0x32)
				mid = (short) RCGetH(rc, 0xe2);
			else
				mid = ((low->fBox.right - low->fBox.left) * 14) / tenths;
		}
		if ((short) RCGetH(rc, 0xe6) >= 0x5a)
			mid = (short) RCGetH(rc, 0xe2);
		bordResult = bord_correction(low, bottoms, &nBottoms, allBottoms, 3, mid, amplMed, amplMean, amplMax,
									 xStartB, xEndB, downShift, pass, lower, superLim, tall, superSure, subSure);
	}
	{
		long m = (allBottoms <= allTops) ? allTops : allBottoms;
		if (m == 0)
			m = 1;
		width = (low->fBox.right - low->fBox.left) / (m * 2);
	}
	smooth_d_bord(bottoms, nBottoms, low, width, lower);
	if (RCGetH(rc, 0x94) == 0x20)
	{
		bordResult = num_bord_correction(tops, &nTops, allTops, 1, amplMed, lower, y);
		if (bordResult == 1)
		{
			bordResult = num_bord_correction(bottoms, &nBottoms, allBottoms, 3, amplMed, lower, y);
			if (bordResult == 1)
				smooth_d_bord(bottoms, nBottoms, low, width, lower);
		}
	}
	else
	{
		if (bordResult == 1)
		{
			long deepest = 0;
			for (SPEC_TYPE* p = low->fSpecl; p != nil; p = p->next)
				if (p->mark == 3 && p->code == 0x65 && y[p->ipoint0] - lower[map[p->ipoint0]] > deepest)
					deepest = y[p->ipoint0] - lower[map[p->ipoint0]];
			subResult = sub_max_to_line(low, bottoms, &nBottoms, lower, deepest);
			bordResult = bord_correction(low, bottoms, &nBottoms, allBottoms, 3, mid, amplMed, amplMean, amplMax,
										 xStartB, xEndB, downShift, pass, lower, superLim, tall, superSure, subSure);
			if (bordResult == 1 || subResult == 1)
				smooth_d_bord(bottoms, nBottoms, low, width, lower);
			if (bordResult == 1)
			{
				deepest = 0;
				for (SPEC_TYPE* p = low->fSpecl; p != nil; p = p->next)
					if (p->mark == 3 && p->code == 0x65 && y[p->ipoint0] - lower[map[p->ipoint0]] > deepest)
						deepest = y[p->ipoint0] - lower[map[p->ipoint0]];
				if (sub_max_to_line(low, bottoms, &nBottoms, lower, deepest) == 1)
					smooth_d_bord(bottoms, nBottoms, low, width, lower);
			}
		}
		superLim = 0;
		for (long t = 0; t < nTops; t++)
			if (lower[tops[t].i] - tops[t].y > superLim)
				superLim = lower[tops[t].i] - tops[t].y;
		del_tail_min(tops, &nTops, y, lower, pass);
		bordResult = bord_correction(low, tops, &nTops, allTops, 1, mid, amplMed, amplMean, amplMax,
									 xStartT, xEndT, upShift, pass, lower, superLim, tall, superSure, subSure);
		if (bordResult == 1)
			bord_correction(low, tops, &nTops, allTops, 1, mid, amplMed, amplMean, amplMax,
							xStartT, xEndT, upShift, pass, lower, superLim, tall, superSure, subSure);
	}
	smooth_u_bord(tops, nTops, low, width, upper, lower);
	total = fill_i_point(order, low);
	if (calc_med_heights(low, tops, bottoms, upper, lower, order, nTops, nBottoms, total, &height, &upperMed, &lowerMed) != 0)
		goto done;
	if (RCGetH(rc, 0x94) == 0x20)
		goto adjust;
	mist = line_pos_mist(low, upperMed, lowerMed, height, nTops, nBottoms, bottoms, &upShift, &downShift, upper, lower, pass);
	if (mist >= 100)
		goto bad;
	subSure = 0;
	superSure = 0;
	for (long t = 0; t < nTops; t++)
		tops[t].susp = 0;
	for (long t = 0; t < nBottoms; t++)
		bottoms[t].susp = 0;
	find_gaps_in_line(bottoms, nBottoms, allBottoms, amplMed, 3, xStartB, xEndB, lower, y, 0, 1);
	find_glitches_in_line(bottoms, nBottoms, amplMed, 3, xStartB, xEndB, lower, x, y, 2, 0, 1);
	find_gaps_in_line(tops, nTops, allTops, amplMed, 1, xStartT, xEndT, lower, y, 0, 1);
	find_glitches_in_line(tops, nTops, amplMed, 1, xStartT, xEndT, lower, x, y, 2, 0, 1);
	for (long t = 0; t < nTops; t++)
	{
		short c = tops[t].susp;
		if (c == 0x14 || c == 0x28 || c == 0x3c)
			superSure = 1;
	}
	for (long t = 0; t < nBottoms; t++)
	{
		short c = bottoms[t].susp;
		if (c == 0x1e || c == 0x32)
			subSure = 1;
	}
	if (superSure == 1 || subSure == 1)
		mist++;
	if (pass == 0)
	{
		if (mist <= 0)
			goto figures;
		// round again, on the second pair of arrays, keeping these
		firstMist = mist;
		savedB = bottoms;
		savedT = tops;
		savedNT = nTops;
		savedNB = nBottoms;
		bottoms = bottoms2;
		tops = tops2;
		for (SPEC_TYPE* p = low->fSpecl; p != nil; p = p->next)
			if (p->mark == 1 || p->mark == 3)
				p->other = p->code;
		pass = (UByte) (pass + 1);
		goto again;
	}
	if (pass == 1)
	{
		if (mist >= 3 && firstMist >= 3 && !(nTops > 1 && nBottoms > 1) && !(savedNT > 1 && savedNB > 1))
			goto bad;
		if (mist >= firstMist)
		{
			// the first pass was as good: back to it
			if (mist == firstMist)
				penalty += 10;
			bottoms = savedB;
			tops = savedT;
			nTops = savedNT;
			nBottoms = savedNB;
			for (SPEC_TYPE* p = low->fSpecl; p != nil; p = p->next)
				if (p->mark == 1 || p->mark == 3)
					p->code = p->other;
			smooth_u_bord(tops, nTops, low, width, upper, lower);
			smooth_d_bord(bottoms, nBottoms, low, width, lower);
			if (calc_med_heights(low, tops, bottoms, upper, lower, order, nTops, nBottoms, total, &height, &upperMed, &lowerMed) != 0)
				goto done;
		}
	}

figures:
	if ((RCByte(rc, 0xb6)[0] == 0 && RCGetH(rc, 0x94) == 0x10 && simple == 1) || (RCGetH(rc, 0x90) & 0x800) != 0)
	{
		if (numbers_in_text(low, upper, lower) == 1)
		{
			penalty += 10;
			if ((short) RCGetH(rc, 0xe6) < 0x5a || (short) RCGetH(rc, 0xe8) < 0x5a)
			{
				RCSetH(rc, 0x94, 0x20);
				goto again;
			}
			penalty += 0x5a;
			goto specBordNone;
		}
	}
	{
		long topsDone = 0, bottomsDone = 0;
		long p = order[0];
		if (!((lower[p] - upper[p]) * 2 > height))
		{
			penalty += 5;
			if (nTops < 1 || nBottoms < 1)
				goto adjust;
			if (tops[0].x > bottoms[0].x)
				topsDone = correct_narrow_ends(tops, &nTops, bottoms, nBottoms, tops[0].y - lower[tops[0].i], 0x10);
			else
				bottomsDone = correct_narrow_ends(bottoms, &nBottoms, tops, nTops, bottoms[0].y - upper[bottoms[0].i], 0x10);
		}
		p = order[total - 1];
		if (!((lower[p] - upper[p]) * 2 > height))
		{
			penalty += 5;
			if (nTops < 1 || nBottoms < 1)
				goto adjust;
			EXTR* lt = &tops[nTops - 1];
			EXTR* lb = &bottoms[nBottoms - 1];
			if (lt->x <= lb->x)
				topsDone = correct_narrow_ends(tops, &nTops, bottoms, nBottoms, lt->y - lower[lt->i], 0x20);
			else
				bottomsDone = correct_narrow_ends(bottoms, &nBottoms, tops, nTops, lb->y - upper[lb->i], 0x20);
		}
		if (topsDone == 1)
			smooth_u_bord(tops, nTops, low, width, upper, lower);
		if (bottomsDone == 1)
			smooth_d_bord(bottoms, nBottoms, low, width, lower);
		if ((topsDone == 1 || bottomsDone == 1)
		 && calc_med_heights(low, tops, bottoms, upper, lower, order, nTops, nBottoms, total, &height, &upperMed, &lowerMed) != 0)
			goto done;
	}
	{
		Boolean crossed = false;
		for (long i = 0; i < low->fII; i++)
			if (low->fY[i] != -1 && lower[i] - upper[i] <= 0)
				crossed = true;
		if (crossed)
			goto bad;
	}
	{
		long hgt = (short) RCGetH(rc, 0xe2);
		long sure = (short) RCGetH(rc, 0xe6);
		if (height < 0xc)
			goto bad;
		if (hgt * 3 < height && sure >= 0x4b)
			goto bad;
		if ((hgt + 1) / 3 > height)
		{
			if (sure >= 0x5a && (nBottoms <= 3 || nTops <= 3))
				goto bad;
			if (sure >= 0x4b && (nBottoms <= 2 || nTops <= 2))
				goto bad;
		}
		if (height < (hgt >> 1) && sure >= 0x37 && (nBottoms <= 1 || nTops <= 1))
			goto bad;
	}
	goto adjust;

bad:
	penalty += 100;
	wideArg = 0;
	extrArg = bottoms;
	nArg = nBottoms;
	goto specBord;
defis:
	penalty += 100;
	wideArg = is_defis(low, strokes);
	extrArg = bottoms;
	nArg = 0;
	goto specBord;
specBordNone:
	wideArg = 0;
	extrArg = bottoms;
	nArg = 0;
specBord:
	SpecBord(low, lower, upper, &lowerMed, &upperMed, &height, &total, wideArg, extrArg, nArg);

adjust:
	if (allBottoms + allTops <= 6 && boxH - ((boxH + 2) >> 2) < height && boxH > 0)
	{
		long q = (height + 3) / 6;
		height -= q * 2;
		upperMed += q;
		lowerMed -= q;
		NarrowBorders(low, lower, upper, q, boxH);
	}
	if (height < 0xc)
	{
		penalty += 0x14;
		for (long i = 0; i < low->fII; i++)
		{
			if (low->fY[i] == -1)
				continue;
			lower[i] = (short) ((UShort) lower[i] + 6);
			upper[i] = (short) ((UShort) upper[i] - 6);
		}
		height += 0xc;
		upperMed -= 6;
		lowerMed += 6;
	}
	if (RCByte(rc, 0xae)[0] != 0)
	{
		for (long i = 0; i < low->fII; i++)
			if (lower[i] - upper[i] > (height + 2) >> 2)
				upper[i] = (short) (upper[i] + ((height + 4) >> 3));
	}
	// the trace rescaled against the borders
	for (long i = 0; i < low->fII; i++)
	{
		long v = low->fY[i];
		if (v == -1)
			continue;
		long up = upper[i], lo = lower[i];
		long r;
		if (RCGetH(rc, 0x94) == 0x20)
		{
			long d = lo - up;
			if (d < 1)
				d = 1;
			r = ((v - up) * 0x50) / d - 0x6a + 0x2800;
		}
		else if (v <= lo && v >= up)
		{
			long d = lo - up;
			if (d < 1)
				d = 1;
			r = ((v - up) * 0x50) / d - 0x6a + 0x2800;
		}
		else if (v > lo)
			r = ((v - lo) * 0x50) / ((height < 1) ? 1 : height) - 0x1a + 0x2800;
		else
			r = ((v - up) * 0x50) / ((height < 1) ? 1 : height) - 0x6a + 0x2800;
		low->fY[i] = (short) r;
	}
	for (long i = 0; i < low->fII; i++)
	{
		if (low->fY[i] == -1)
			continue;
		long d = (height < 1) ? 1 : height;
		low->fX[i] = (short) (((low->fX[i] - low->fBox.left) * 0x50) / d + 0x50);
	}
	RCSetH(rc, 0xea, (UShort) height);
	RCSetH(rc, 0xec, (UShort) lowerMed);
	RCSetH(rc, 0xf0, 0);
	RCSetH(rc, 0xee, 0);
	if (penalty < 100 && nTops >= 1 && nBottoms >= 1)
		RCSetH(rc, 0xee, 0x2d);
	if (penalty < 100 && nTops >= 2 && nBottoms >= 2)
		RCSetH(rc, 0xee, 0x32);
	if (penalty < 10 && nTops >= 3 && nBottoms >= 3)
		RCSetH(rc, 0xee, 0x37);
	if (penalty < 10 && nTops >= 4 && nBottoms >= 4)
		RCSetH(rc, 0xee, 0x4b);
	if (penalty == 0 && nTops >= 5 && nBottoms >= 5)
		RCSetH(rc, 0xee, 0x5a);
	if (penalty < 100 && nBottoms >= 1)
		RCSetH(rc, 0xf0, 0x2d);
	if (penalty < 100 && nBottoms >= 2)
		RCSetH(rc, 0xf0, 0x32);
	if (penalty < 10 && nBottoms >= 3)
		RCSetH(rc, 0xf0, 0x37);
	if (penalty < 10 && nBottoms >= 4)
		RCSetH(rc, 0xf0, 0x4b);
	if (penalty == 0 && nBottoms >= 5)
		RCSetH(rc, 0xf0, 0x5a);
	{
		long sum = 0, count = 0;
		for (long i = 0; i < low->fII; i++)
		{
			if (low->fY[i] == -1)
				continue;
			sum += HWRAbs(lower[i] - lowerMed);
			count++;
		}
		// DEVIATION: a trace of nothing but pen-ups is a divide by zero the
		// ROM would trap on; low_level never hands one.
		long mean = (count != 0) ? sum / count : 0;
		if ((height * 2 + 2) / 5 <= mean && (short) RCGetH(rc, 0xf0) >= 0x32)
			RCSetH(rc, 0xf0, 0x32);
	}
	if ((short) RCGetH(rc, 0xe8) == 100 && (short) RCGetH(rc, 0xe6) == 100)
	{
		RCSetH(rc, 0xf0, 0);
		RCSetH(rc, 0xee, 0);
	}
	if (FillRCNB(order, total, low, upper, lower) != 1)
		result = 0;

done:
	if (block != nil)
		HWRMemoryFree((Ptr) block);
	return result;
}
