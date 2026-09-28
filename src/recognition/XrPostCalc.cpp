/*
	File:		XrPostCalc.cpp

	Contains:	The cursive reader's rule interpreter and the functions
				its rules call.  See XrPost.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	Almost all of this was transcribed from the disassembly: the
	decompiler cannot follow the interpreter's two switches, loses the
	functions' fifth argument (the parameters, passed on the stack), and
	mistakes the ARM's unaligned loads of halfwords.
*/

#include "XrPost.h"
#include "XrDomains.h"
#include "CursiveReader.h"
#include "LowLevel.h"
#include "ParaGraph.h"
#include "WordSegment.h"
#include "toolbox/ByteOrder.h"
#include <string.h>

// an xr's first and last points, and its box (big-endian halfwords)
static inline short	XrBeg(const xrd_el_type* el)		{ return XrGetH(el->begpoint); }
static inline short	XrEnd(const xrd_el_type* el)		{ return XrGetH(el->endpoint); }
static inline short	XrBox(const xrd_el_type* el, long h)	{ return XrGetH(el->box + h); }
static inline xrd_el_type*	XrAt(xrdata_type* xr, long i)	{ return (xrd_el_type*) xr->fElements + i; }
static inline xrd_el_type*	XrEl(intptr_t p)				{ return (xrd_el_type*) p; }

// the ARM's 32-bit division (__rt_sdiv): a/b, rounded towards nought
static inline long	SDiv(long a, long b)	{ return (int32_t) a / (int32_t) b; }


/*------------------------------------------------------------------------------
	T h e   i n t e r p r e t e r
------------------------------------------------------------------------------*/

// ROM 0x00336808 FindXrIndex__FPA13_15RWG_PPD_el_typesi
// The xr the j'th element of the symbol's prototype was read from - when
// the element read one (3) or was skipped over one (1) - or -1.
long
FindXrIndex(RWG_PPD_type* ppd, short sym, long j)
{
	UByte* el = ppd[sym].el[j];
	if (el[1] == 3 || el[1] == 1)
		return (short) el[0];
	return -1;
}


// ROM 0x00335f94 CalculatePow__FlT1Pi
// a to the power b, by multiplying; ROM QUIRK: a negative b multiplies
// too (counting down to it), so 2 to the -2 is 4.
long
CalculatePow(long a, long b, int* err)
{
	long step = b >= 0 ? 1 : -1;
	int32_t result = 1;
	for (long i = 0; i != b; i += step)
		result = (int32_t) ((uint32_t) a * (uint32_t) result);
	*err = 0;
	return result;
}


// ROM 0x00335fd0 CalculateChangeSign__FlPi
long
CalculateChangeSign(long a, int* err)
{
	*err = 0;
	return (int32_t) (0u - (uint32_t) a);
}


// ROM 0x003375dc CalculateLess__FlT1Pi
// How far b is above a, fuzzily: 0 when it is more than 50 below, 20 when
// more than 50 above, and a straight line between (10 when equal).
long
CalculateLess(long a, long b, int* err)
{
	long d = (int32_t) (b - a);
	*err = 0;
	if (d < -50)
		return 0;
	if (d > 50)
		return 20;
	return SDiv(d * 20, 100) + 10;
}


// ROM 0x00338d34 CalculateGreater__FlT1Pi
long
CalculateGreater(long a, long b, int* err)
{
	long d = (int32_t) (a - b);
	*err = 0;
	if (d < -50)
		return 0;
	if (d < 51)
		return SDiv(d * 20, 100) + 10;
	return 20;
}


// ROM 0x00338d44 CalculateEquals__FlT1Pi
// 20 when equal, falling to nought 50 either side.
long
CalculateEquals(long a, long b, int* err)
{
	*err = 0;
	long d = (int32_t) (b - a);
	if (d > -51 && d < 51)
	{
		long v = d * 20;
		if (d >= 0)
			v = d * -20;
		return SDiv(v, 50) + 20;
	}
	return 0;
}


// ROM 0x00338d98 CalculateAdd__FlT1Pi
// DEVIATION: on the host a stack value may be a pointer (an xr), so the
// sum is as wide as one.
intptr_t
CalculateAdd(intptr_t a, intptr_t b, int* err)
{
	*err = 0;
	return a + b;
}


// ROM 0x00338da8 CalculateSubtract__FlT1Pi
intptr_t
CalculateSubtract(intptr_t a, intptr_t b, int* err)
{
	*err = 0;
	return a - b;
}


// ROM 0x00338db8 CalculateMultiply__FlT1Pi
long
CalculateMultiply(long a, long b, int* err)
{
	*err = 0;
	return (int32_t) ((uint32_t) b * (uint32_t) a);
}


// ROM 0x00338dc8 CalculateDivide__FlT1Pi
// a/b; a division by nought answers 10000 with a's sign (nought counting
// as positive).
long
CalculateDivide(long a, long b, int* err)
{
	*err = 0;
	if (b == 0)
		return a >= 0 ? 10000 : -10000;
	return SDiv(a, b);
}


// ROM 0x00336b70 CalculateFunction__FP12_POST_PARAMSP5QUEUEPlPsPi
// The function the current bytecode names (0x0e: its next byte; 0x05 or
// 0x0d: the next two, big-endian) called with as many of the top of the
// stack as it takes, deepest first; the stack is then that many less one
// deep (the caller puts the answer on top).  An unknown function fails
// with 0x1f.  (Every entry of the ROM's table has its range checks off,
// but they are made as the ROM makes them.)
intptr_t
CalculateFunction(POST_PARAMS* pp, QUEUE* queue, intptr_t* stack, short* depth, int* err)
{
	const UByte* at = queue->bytes + queue->index;
	ULong id;
	if (at[0] == 0x0e)
		id = at[1];
	else
		id = at[2] + ((short) at[1] << 8);
	if (id >= (ULong) RD_N_PP_FUNCTIONS[0] || (long) id < 0 || Functions[id * 9 + 1] == 0)
	{
		*err = 0x1f;
		return 0;
	}
	const int* entry = Functions + id * 9;
	if (entry[2] != 0 && stack[*depth - 1] <= entry[4])
	{
		*err = entry[6];
		return 0;
	}
	if (entry[3] != 0 && stack[*depth - 1] >= entry[5])
	{
		*err = entry[7];
		return 0;
	}
	*err = 0;
	long nargs = entry[8];
	// ROM QUIRK: the arguments a function does not take are whatever the
	// registers held; the host passes nought
	intptr_t a = 0, b = 0, c = 0;
	if (nargs > 0)
		a = stack[*depth - nargs];
	if (nargs > 1)
		b = stack[*depth - nargs + 1];
	if (nargs > 2)
		c = stack[*depth - nargs + 2];
	long n;
	PPFunction fn = PPFunctionAt(id, &n);
	if (fn == nil)
	{
		*err = 0x1f;		// (host: a table entry with no function of ours)
		return 0;
	}
	intptr_t result = fn(a, b, c, err, pp);
	*depth = (short) (*depth - (nargs - 1));
	return result;
}


// ROM 0x003359bc CalculateQueueResult__FP12_POST_PARAMSPcPs
// One queue of a rule run: a stack machine over the bytes, each bytecode
// as long as globalSizeArray says (0x10 says its own length):
//	0x03, 0x09	push the next four bytes (big-endian)
//	0x0a		push the next two
//	0x0b		push the next one
//	0x10		push the PDF's constant (section 1) the halfword at +2 names
//	0x07, 0x0f	push a field of an xr of the letter (byte 2 < 5), the letter
//				before (5..9) or after (10..14) - its address, left, bottom,
//				right, top - byte 1 naming which of its prototype's elements
//	0x04, 0x0c	arithmetic on the top two (byte 1: add, subtract, multiply,
//				divide, power, negate, less, greater, equal)
//	0x05, 0x0d, 0x0e	a function of the table
// and anything else fails with 0xf.  A failure (0x10 the stack is full,
// 0x11 no such xr, 0x14..0x16 the letter the field names is not there,
// or a function's own) ends the queue with nought.  ==> the bottom of the
// stack, nought when it is empty.
intptr_t
CalculateQueueResult(POST_PARAMS* pp, const UByte* queue, UByte* error)
{
	xrd_el_type* xrBase = (xrd_el_type*) pp->xr->fElements;
	short cur = pp->cur;
	RWG_PPD_type* ppd = pp->ppd;
	long isPrev = pp->isPrev, isNext = pp->isNext, noPrev = pp->noPrev, noNext = pp->noNext;
	short* prevSyms = pp->prevSyms;
	short* nextSyms = pp->nextSyms;
	const UByte* constants = ((PDFHeader*) pp->pdf)->fSection1;
	intptr_t* stack = pp->stack;
	short depth = 0;
	long index = 0;
	int err = 0;
	error[0] = error[1] = 0;
	if (queue[0] == 0)
		return 0;
	long full = (short) (((ULong) (long) pp->stackBytes) >> 2) - 1;
	// ROM QUIRK: an xr field of a letter that is neither the rule's
	// neighbour nor missing reads the xr the last such field found (the
	// ROM's register); the host starts it at -1
	long found = -1;
	for (;;)
	{
		const UByte* at = queue + index;
		UByte fail = 0;
		intptr_t push = 0;
		Boolean pushing = false;
		switch (at[0])
		{
		case 0x03:
		case 0x09:
			if (full <= depth) { fail = 0x10; break; }
			push = (int32_t) (((ULong) at[1] << 24) + (at[2] << 16) + (at[3] << 8) + at[4]);
			pushing = true;
			break;
		case 0x0a:
			if (full <= depth) { fail = 0x10; break; }
			push = at[2] + (at[1] << 8);
			pushing = true;
			break;
		case 0x0b:
			if (full <= depth) { fail = 0x10; break; }
			push = at[1];
			pushing = true;
			break;
		case 0x10:
			if (full <= depth) { fail = 0x10; break; }
			push = XrGetH(constants + (short) (at[3] + (at[2] << 8)) * 2);
			pushing = true;
			break;
		case 0x07:
		case 0x0f:
		{
			if (full <= depth) { fail = 0x10; break; }
			UByte field = at[2];
			if (field < 5)
				found = FindXrIndex(ppd, cur, at[1]);
			else
			{
				if (isPrev != 0)
				{
					if (field >= 10) { fail = 0x14; break; }
					found = FindXrIndex(ppd, prevSyms[0], at[1]);
				}
				if (isNext != 0)
				{
					if (field >= 5 && field < 10) { fail = 0x14; break; }
					found = FindXrIndex(ppd, nextSyms[0], at[1]);
				}
				if (isPrev == 0 && isNext == 0)
				{
					if (field >= 5 && field < 10 && noPrev != 0) { fail = 0x15; break; }
					if (field >= 10 && noNext == 0) { fail = 0x16; break; }
				}
			}
			if (found == -1) { fail = 0x11; break; }
			xrd_el_type* el = xrBase + found;
			switch (field)
			{
			case 0: case 5: case 10:	push = (intptr_t) el; break;
			case 1: case 6: case 11:	push = XrBox(el, kXrLeft); break;
			case 2: case 7: case 12:	push = XrBox(el, kXrBottom); break;
			case 3: case 8: case 13:	push = XrBox(el, kXrRight); break;
			case 4: case 9: case 14:	push = XrBox(el, kXrTop); break;
			}
			pushing = field <= 14;
			break;
		}
		case 0x04:
		case 0x0c:
		{
			intptr_t* top = stack + depth;
			intptr_t r;
			switch (at[1])
			{
			case 0: r = CalculateAdd(top[-2], top[-1], &err); break;
			case 1: r = CalculateSubtract(top[-2], top[-1], &err); break;
			case 2: r = CalculateMultiply((long) top[-2], (long) top[-1], &err); break;
			case 3: r = CalculateDivide((long) top[-2], (long) top[-1], &err); break;
			case 4: r = CalculatePow((long) top[-2], (long) top[-1], &err); break;
			case 5:
				top[-1] = CalculateChangeSign((long) top[-1], &err);
				goto checked;
			case 6: r = CalculateLess((long) top[-2], (long) top[-1], &err); break;
			case 7: r = CalculateGreater((long) top[-2], (long) top[-1], &err); break;
			case 8: r = CalculateEquals((long) top[-2], (long) top[-1], &err); break;
			default:
				err = 0xf;
				goto failed;
			}
			top[-2] = r;
			depth = (short) (depth - 1);
		checked:
			if (err != 0)
				goto failed;
			break;
		}
		case 0x05:
		case 0x0d:
		case 0x0e:
		{
			QUEUE q = { queue, 0, 0, index };
			intptr_t r = CalculateFunction(pp, &q, stack, &depth, &err);
			stack[depth - 1] = r;
			if (err != 0)
				goto failed;
			break;
		}
		default:
			fail = 0xf;
			break;
		}
		if (fail != 0)
		{
			error[1] = fail;
			error[0] = 0;
			return 0;
		}
		if (pushing)
		{
			stack[depth] = push;
			depth = (short) (depth + 1);
		}
		if (at[0] == 0x10)
			index += at[1];
		else
			index += globalSizeArray[queue[index]];
		if (queue[index] == 0)
			break;
	}
	if (depth != 0)
		return stack[0];
	return 0;

failed:
	error[1] = (UByte) err;
	error[0] = (UByte) (err >> 8);
	return 0;
}


// ROM 0x00336840 CalculateGroupResult__FP12_POST_PARAMSP15PDF_RULE_HEADERl
// A rule's queues run in turn, each answer weighed by the weight its
// header names (the PDF's section 2) and no lower than -floor, and added.
// The header: +0 how many queues, +2 each one's length, +0x16 each one's
// weight; the queues from +0x20.
long
CalculateGroupResult(POST_PARAMS* pp, const UByte* rule, long floor)
{
	long sum = 0;
	const UByte* queue = rule + 0x20;
	const UByte* weights = ((PDFHeader*) pp->pdf)->fSection2;
	long n = (UShort) XrGetH(rule);
	for (long i = 0; i < n; i = (short) (i + 1))
	{
		UByte error[2];
		long v = (int32_t) ((uint32_t) (long) CalculateQueueResult(pp, queue, error)
							* (uint32_t) (long) XrGetH(weights + XrGetH(rule + 0x16 + i * 2) * 2));
		if (v < -floor)
			v = -floor;
		sum += v;
		queue += (UShort) XrGetH(rule + 2 + i * 2);
		pp->queues = (short) (pp->queues + 1);
	}
	return sum;
}


/*------------------------------------------------------------------------------
	W h a t   t h e   f u n c t i o n s   n e e d
------------------------------------------------------------------------------*/

// ROM 0x00305c88 X_IsStrongElem__FP11xrd_el_type
// Whether an xr counts: every type but the marks 0x2d-0x30, 0x32-0x34,
// 0x36, 0x39, 0x3c and 0x3d.
Boolean
X_IsStrongElem(xrd_el_type* el)
{
	switch (el->type)
	{
	case 0x2d: case 0x2e: case 0x2f: case 0x30:
	case 0x32: case 0x33: case 0x34:
	case 0x36:
	case 0x39:
	case 0x3c: case 0x3d:
		return false;
	}
	return true;
}


// ROM 0x00307290 iXmax_left__FPsT1iT3
// From i back along its stroke while no point is dx left of the
// rightmost so far: the middle of the rightmost's plateau.
long
iXmax_left(short* x, short* y, long i, long dx)
{
	long best = i;
	for (i--; y[i] != -1; i--)
	{
		if (x[i] < x[best] - dx)
			break;
		if (x[i] >= x[best])
			best = i;
	}
	return iMidPointPlato(best, 0x7fff, x, y);
}


// ROM 0x003072f4 iXmin_left__FPsT1iT3
long
iXmin_left(short* x, short* y, long i, long dx)
{
	long best = i;
	for (i--; y[i] != -1; i--)
	{
		if (x[i] - dx > x[best])
			break;
		if (x[i] <= x[best])
			best = i;
	}
	return iMidPointPlato(best, 0x7fff, x, y);
}


// ROM 0x00308b08 CurveHasSelfCrossing__FPsT1iT3PiT5l
// Whether the trace from i to j crosses itself - two of its segments at
// least two apart - closing (with minSquare above nought) at least that
// much area; the two segments' first point and the second's last.
long
CurveHasSelfCrossing(short* x, short* y, long i, long j, long* pI, long* pJ, long minSquare)
{
	if (i >= j)
		return 0;
	if (y[i] == -1)
	{
		i++;
		if (y[i] == -1)
			return 0;
	}
	if (y[j] == -1)
	{
		j--;
		if (y[j] == -1)
			return 0;
	}
	long last = j - 3;
	if (last < i)
		return 0;
	for (long a = i; a <= last; a++)
	{
		if (y[a] == -1 || y[a + 1] == -1)
			continue;
		short minX, maxX, minY, maxY;
		if (x[a] < x[a + 1]) { minX = x[a]; maxX = x[a + 1]; } else { minX = x[a + 1]; maxX = x[a]; }
		if (y[a] < y[a + 1]) { minY = y[a]; maxY = y[a + 1]; } else { minY = y[a + 1]; maxY = y[a]; }
		for (long b = a + 2; b < j; b++)
		{
			short yb = y[b], yb1 = y[b + 1];
			if (yb == -1 || yb1 == -1)
				continue;
			short xb = x[b], xb1 = x[b + 1];
			if (xb > maxX && xb1 > maxX)
				continue;
			if (xb < minX && xb1 < minX)
				continue;
			if (yb > maxY && yb1 > maxY)
				continue;
			if (yb < minY && yb1 < minY)
				continue;
			if (!is_cross(x[a], y[a], x[a + 1], y[a + 1], xb, yb, xb1, yb1))
				continue;
			if (minSquare > 0)
			{
				short flag;
				long s = ClosedSquare(x, y, a, b + 1, &flag);
				if (flag != 0)
					continue;
				if (s < 0)
					s = -s;
				if (s < minSquare)
					continue;
			}
			if (pI != nil)
				*pI = a;
			if (pJ != nil)
				*pJ = b + 1;
			return 1;
		}
	}
	return 0;
}


// ROM 0x00339198 FindIRangeOfCorrXRs__FPA13_15RWG_PPD_el_typeP11xrdata_typePsUiPiT5
// The xrs the symbols (-1 ending them) were read from: the one that
// starts first and the one that ends last, breaks left out and (without
// withMarks) the marks 0x34, 0x36, 0x3a and 0x3b too.  ==> whether any.
long
FindIRangeOfCorrXRs(RWG_PPD_type* ppd, xrdata_type* xr, short* syms, ULong withMarks, long* iMin, long* iMax)
{
	long minBeg = 0x7fff, maxEnd = 0;
	if (syms[0] == -1)
		return 0;
	for (long s = 0; syms[s] != -1; s++)
	{
		for (long j = 0; ppd[syms[s]].el[j][1] != 0; j++)
		{
			long k = FindXrIndex(ppd, syms[s], j);
			if (k < 0)
				continue;
			xrd_el_type* el = XrAt(xr, k);
			if (X_IsBreak(el))
				continue;
			if (withMarks == 0 && (el->type == 0x36 || el->type == 0x34 || el->type == 0x3a || el->type == 0x3b))
				continue;
			if (XrBeg(el) < minBeg)
			{
				minBeg = XrBeg(el);
				*iMin = k;
			}
			if (XrEnd(el) > maxEnd)
			{
				maxEnd = XrEnd(el);
				*iMax = k;
			}
		}
	}
	return (minBeg != 0x7fff && maxEnd != 0) ? 1 : 0;
}


// ROM 0x0033a0f8 IsAnyExtr__FPsT1iT3
// What the point i is an extremum of: 1 a top, 2 a bottom (each for the
// whole of the stretch dist either side), 3 leftmost, 4 rightmost; at a
// stroke's end only its neighbour is asked.  0 for none.
long
IsAnyExtr(short* x, short* y, long i, long dist)
{
	short x0 = x[i], y0 = y[i];
	if (i <= 0 || y0 == -1)
		return 0;
	short ym = y[i - 1], yp = y[i + 1];
	short t;
	if (ym == -1)
	{
		if (yp < y0)
			return 2;
		if (y0 < yp)
			return 1;
		t = x[i + 1];
	}
	else if (yp != -1)
	{
		if (y0 <= ym && y0 <= yp
		 && iXmin_left(y, y, i, dist) == i && iXmin_right(y, y, i, dist) == i)
			return 1;
		if (ym <= y0 && yp <= y0
		 && iXmax_left(y, y, i, dist) == i && iXmax_right(y, y, i, dist) == i)
			return 2;
		if (x0 <= x[i - 1] && x0 <= x[i + 1]
		 && iXmin_left(x, y, i, dist) == i && iXmin_right(x, y, i, dist) == i)
			return 3;
		if (!(x[i - 1] <= x0 && x[i + 1] <= x0))
			return 0;
		if (iXmax_left(x, y, i, dist) != i)
			return 0;
		if (iXmax_right(x, y, i, dist) == i)
			return 4;
		return 0;
	}
	else
	{
		if (ym < y0)
			return 2;
		if (y0 < ym)
			return 1;
		t = x[i - 1];
	}
	if (t < x0)
		return 4;
	if (x0 < t)
		return 3;
	return 0;
}


// ROM 0x0033a950 CheckChord__FPsT1iN33
// The point between i and j furthest from their chord, when the chord
// is at least 2 dist long, the point at least dist from both ends and
// (for a short stretch) the bend there no straighter than cosLimit; -1
// otherwise.
long
CheckChord(short* x, short* y, long i, long j, long cosLimit, long dist)
{
	long n = j - i;
	if (n <= 2)
		return -1;
	if (Distance8(x[i], y[i], x[j], y[j]) < dist * 2)
		return -1;
	long m = iMostFarFromChord(x, y, i, j);
	if (n < 12 && HWRAbs(cos_vect(i, m, m, j, x, y)) > cosLimit)
		return -1;
	if (m == -1)
		return m;
	if (Distance8(x[i], y[i], x[m], y[m]) < dist)
		return -1;
	if (Distance8(x[j], y[j], x[m], y[m]) >= dist)
		return m;
	return -1;
}


// ROM 0x0033a32c filtr_gid__FiPsN42PiN21
// The n points of a trace thinned to at most *count (all n for none
// asked): the pen-ups, the extrema (IsAnyExtr) at least three points
// from the last kept, and between two kept the furthest from their chord
// when it stands out (CheckChord); the first and last points as a
// stroke's ends.  index gets where each came from.  ==> 0, 1 when there
// was no room for them all.
long
filtr_gid(long n, short* x, short* y, short* xOut, short* yOut, short* index, long* count, long cosLimit, long dist)
{
	if (n <= 2)
	{
		if (n <= 0)
			return 1;
		if (*count > 0 && *count < n)
			return 1;
		for (long i = 0; i < n; i++)
		{
			xOut[i] = x[i];
			yOut[i] = y[i];
			index[i] = (short) i;		// ROM QUIRK: not checked for nil here
		}
		*count = n;
		return 0;
	}
	// the ends marked as pen-ups while the trace is walked, and put back
	short save0 = y[0];
	y[0] = -1;
	short saveN = y[n - 1];
	y[n - 1] = -1;
	long room = *count;
	if (room <= 0)
		room = n;
	*count = 0;
	long i = 0;
	long last = 0;			// the last point kept
	short curX = 0, curY = 0, nextX = 0, nextY;
#define KEEP(px, py, pi)	do { xOut[*count] = (px); yOut[*count] = (py); if (index != nil) index[*count] = (short) (pi); } while (0)
	while (*count < room && i < n)
	{
		if (y[i] == -1)
		{
			KEEP(x[i], -1, i);
			if (++*count >= room)
				goto done;
			i++;
			continue;
		}
		if (*count >= room)
			goto done;
		KEEP(x[i], y[i], i);
		last = i;
		++*count;
		i++;
		if (n - 1 <= i)
			continue;
		curX = x[i];
		curY = y[i];
		for (;;)
		{
			if (n - 1 > i)
			{
				nextX = x[i + 1];
				nextY = y[i + 1];
			}
			else
				nextY = -1;
			if (room - 1 > *count && n - 1 > i)
			{
				if (curY == -1)
					goto next;
				if (nextY != -1)
				{
					if (IsAnyExtr(x, y, i, dist) != 0 && HWRAbs(i - index[*count - 1]) >= 3)
					{
						long m = CheckChord(x, y, last, i, cosLimit, dist);
						if (m > 0)
						{
							if (*count >= room)
								goto closing;
							KEEP(x[m], y[m], m);
							++*count;
						}
						if (*count >= room)
							goto closing;
						KEEP(curX, curY, i);
						++*count;
						last = i;
					}
					i++;
					curX = nextX;
					curY = nextY;
					continue;
				}
			}
		closing:
			// the stretch closes at i: what stands out between it and the
			// last point kept, and i itself
			if (curY == -1)
				goto next;
			{
				long m = CheckChord(x, y, last, i, cosLimit, dist);
				if (m > 0)
				{
					if (*count >= room)
						goto done;
					KEEP(x[m], y[m], m);
					++*count;
				}
				if (*count >= room)
					goto done;
				KEEP(curX, curY, i);
				++*count;
				i++;
			}
			goto next;
		}
	next:
		;
	}
done:
#undef KEEP
	y[0] = save0;
	y[n - 1] = saveN;
	{
		long c = *count;
		if (c >= room && i < n)
			return 1;
		if (c >= 3)
		{
			if (saveN != -1 && yOut[c - 1] == -1)
				*count = --c;
			if (save0 != -1 && yOut[0] == -1)
			{
				c = *count - 1;
				*count = c;
				memmove(xOut, xOut + 1, c * 2);
				memmove(yOut, yOut + 1, *count * 2);
				if (index != nil)
					memmove(index, index + 1, *count * 2);
			}
		}
	}
	return 0;
}


// ROM 0x00339b88 FindBeak__FPsT1iT3Pi
// The sharpest corner of the trace between i and j (thinned by
// filtr_gid, a top's corner counting half), or failing one an end of it
// that is a stroke's end.  ==> 0 and where, 1 for none.
long
FindBeak(short* x, short* y, long i, long j, long* at)
{
	long result = 1;
	long count = 50;
	long hi = j < i ? i : j;
	long lo = i > j ? j : i;
	short idx[50], yOut[50], xOut[50];
	if (filtr_gid(hi - lo + 1, x + lo, y + lo, xOut, yOut, idx, &count, 0x50, 2) != 0)
		return 1;
	long best = -100;
	*at = lo;
	if (count - 1 > 1)
	{
		for (long k = 1; count - 1 > k; k++)
		{
			long a = k - 1, b = k + 1;
			while (a > 0 && HWRAbs(idx[k] - idx[a]) <= 2)
				a--;
			while (count - 1 > b && HWRAbs(idx[b] - idx[k]) <= 2)
				b++;
			if (yOut[k] == -1 || yOut[a] == -1 || yOut[b] == -1)
				continue;
			long c = cos_pointvect(xOut[k], yOut[k], xOut[a], yOut[a], xOut[k], yOut[k], xOut[b], yOut[b]);
			if (yOut[a] >= yOut[k] && yOut[b] >= yOut[k] && c > 0)
				c = c / 2;
			if (c > best)
			{
				best = c;
				*at = idx[k] + lo;
			}
		}
		if (best > 0)
			return 0;
	}
	if (y[j + 1] == -1)
	{
		*at = j;
		return 0;
	}
	if (y[i - 1] != -1)
		return result;
	*at = i;
	return 0;
}


// ROM 0x0033ad08 UpdateBoxWithXr__FP11xrd_el_typePP11xrd_el_typeP5_RECTP12_POST_PARAMSUii
// The box grown by an xr's (breaks never; the marks 0x34, 0x3a, 0x3b
// only on the second pass; a 0x36 only sideways); on the first pass,
// with track, by the trace between the last xr and this one too
// (leftwards).  The xr becomes the last.
void
UpdateBoxWithXr(xrd_el_type* el, xrd_el_type** last, _RECT* box, POST_PARAMS* pp, ULong track, long pass)
{
	if (X_IsBreak(el))
		return;
	if (pass != 1 && (el->type == 0x34 || el->type == 0x3a || el->type == 0x3b))
		return;
	if (el->type != 0x36)
	{
		if (XrBox(el, kXrTop) < box->top)
			box->top = XrBox(el, kXrTop);
		if (XrBox(el, kXrBottom) > box->bottom)
			box->bottom = XrBox(el, kXrBottom);
	}
	if (XrBox(el, kXrRight) > box->right)
		box->right = XrBox(el, kXrRight);
	if (XrBox(el, kXrLeft) < box->left)
		box->left = XrBox(el, kXrLeft);
	if (track != 0 && pass == 0 && *last != nil)
	{
		for (short i = XrEnd(*last); i < XrBeg(el); i = (short) (i + 1))
			if (pp->y[i] != -1 && pp->x[i] < box->left)
				box->left = pp->x[i];
	}
	*last = el;
}


// ROM 0x0033ae54 FindXrLetterBox__FiP12_POST_PARAMSP5_RECTUiPi
// The box of the letter (0), the one before (-1) or after (1), or of the
// two or three strong xrs just before (-2) or after (2) the letter's -
// the marks tried again on a second pass when nothing else gave a top.
// ==> 1, 0 with 0xd (no such letter) or 0x11 (no box).
long
FindXrLetterBox(long which, POST_PARAMS* pp, _RECT* box, ULong track, int* err)
{
	xrd_el_type* base = (xrd_el_type*) pp->xr->fElements;
	short* syms = nil;
	long from = 0, to = 0;
	short own[2];
	if (which < -2 || which > 2 || (pp->noPrev != 0 && which < 0) || (pp->noNext != 0 && which > 0))
	{
		*err = 0xd;
		return 0;
	}
	switch (which)
	{
	case -2:
	{
		long j = 0, k = -1;
		if (pp->ppd[pp->cur].el[0][1] == 0)
			goto noLetter;
		do
		{
			k = FindXrIndex(pp->ppd, pp->cur, j);
			if (k >= 0)
				break;
			j++;
		} while (pp->ppd[pp->cur].el[j][1] != 0);
		if (k <= 0)
			goto noLetter;
		to = k - 1;
		long strong = 0;
		from = k - 1;
		for (; from >= 0; from--)
		{
			xrd_el_type* el = base + from;
			if (X_IsStrongElem(el) && !X_IsBreak(el) && ++strong >= 3)
				break;
		}
		if (from < 0)
			from = 0;
		if (strong < 2)
			goto noLetter;
		break;
	}
	case -1:
		syms = pp->prevSyms;
		break;
	case 0:
		own[0] = pp->cur;
		own[1] = -1;
		syms = own;
		break;
	case 1:
		syms = pp->nextSyms;
		break;
	case 2:
	{
		long k = -1;
		long j = 0;
		if (pp->ppd[pp->cur].el[0][1] == 0)
			goto noLetter;
		do
		{
			long f = FindXrIndex(pp->ppd, pp->cur, j);
			if (f >= 0)
				k = f;
			j++;
		} while (pp->ppd[pp->cur].el[j][1] != 0);
		if (k < 0)
			goto noLetter;
		from = to = k + 1;
		long strong = 0;
		if (base[to].type != 0)
		{
			for (;;)
			{
				xrd_el_type* el = base + to;
				if (X_IsStrongElem(el) && !X_IsBreak(el) && ++strong >= 3)
				{
					if (base[to].type == 0)
						to--;
					goto counted;
				}
				to++;
				if (base[to].type == 0)
					break;
			}
		}
		to--;
	counted:
		if (strong < 2)
			goto noLetter;
		break;
	}
	}
	{
		xrd_el_type* last = nil;
		box->left = 0x7fff;
		box->top = 0x7fff;
		box->right = 0;
		box->bottom = 0;
		for (long pass = 0; ; )
		{
			if (syms == nil)
			{
				for (long i = from; i <= to; i++)
					UpdateBoxWithXr(base + i, &last, box, pp, track, pass);
			}
			else
			{
				for (long s = 0; syms[s] != -1; s++)
					for (long j = 0; pp->ppd[syms[s]].el[j][1] != 0; j++)
					{
						long k = FindXrIndex(pp->ppd, syms[s], j);
						if (k >= 0)
							UpdateBoxWithXr(base + k, &last, box, pp, track, pass);
					}
			}
			if (box->top != 0x7fff)
				break;
			if (++pass >= 2)
				goto noBox;
		}
		if (box->top != 0x7fff && box->left != 0x7fff && box->bottom != 0 && box->right != 0)
			return 1;
	}
noBox:
	*err = 0x11;
	return 0;
noLetter:
	*err = 0xd;
	return 0;
}


// ROM 0x0033b894 TooManyStrongElems__FPA13_15RWG_PPD_el_typeP11xrdata_typePs
// Whether the symbols span more than four strong xrs (or none at all).
long
TooManyStrongElems(RWG_PPD_type* ppd, xrdata_type* xr, short* syms)
{
	long iMin, iMax;
	if (!FindIRangeOfCorrXRs(ppd, xr, syms, 0, &iMin, &iMax))
		return 1;
	long strong = 0;
	if (iMin > iMax)
		return 0;
	for (long i = iMin; i <= iMax; i++)
		if (X_IsStrongElem(XrAt(xr, i)))
			strong++;
	return strong > 4 ? 1 : 0;
}


// ROM 0x0033bcc4 GetXrCorr__FP10xrinp_typeUciT3P14dti_descr_type
// How well an xr matches the idx'th element of the variant's prototype
// (the letter table's 0x4c-byte elements, each a nibble per xr type,
// height, shift, orientation and link).  -1 for no such element, nought
// when the type or the height is not allowed at all.
// ROM BUG: the shift's, the orientation's and the link's bytes are added
// whole as well as their nibbles, so the answer is inflated by whatever
// the neighbouring nibble holds.
long
GetXrCorr(xrd_el_type* xr, UByte sym, long var, long idx, DTIHeader* dti)
{
	ULong c = OSToRec(sym);
	if (c < 0x20 || c >= 0xa2)
		return -1;
	UByte* data = nil;
	if (dti->fRAMDTEMainPtr != nil)
	{
		ULong offset = GetBigEndianWord(dti->fRAMDTEMainPtr + c * 4);
		if (offset != 0)
			data = dti->fRAMDTEMainPtr + offset;
	}
	if (data == nil)
	{
		if (dti->fDTEMain == nil)
			return -1;
		ULong offset = GetBigEndianWord(dti->fDTEMain + c * 4);
		if (offset == 0)
			return -1;
		data = dti->fDTEMain + offset;
	}
	if (data == nil || data[0] == 0 || var >= data[0] || data[4 + var] <= idx)
		return -1;
	UByte* el = data + 0x54;
	for (long i = 0; i < var && i < 16; i++)
		el += data[4 + i] * 0x4c;
	el += idx * 0x4c;
	#define NIBBLE(b, n)	(((n) & 1) ? ((b) & 0xf) : ((b) >> 4))
	long v = NIBBLE(el[4 + (xr->type >> 1)], xr->type);
	if (v == 0)
		return 0;
	long h = NIBBLE(el[0x24 + (xr->height >> 1)], xr->height);
	if (h == 0)
		return 0;
	long sum = v + h;
	UByte b = el[0x2c + (xr->shift >> 1)];
	sum += NIBBLE(b, xr->shift) + b;
	b = el[0x3c + (xr->orient >> 1)];
	sum += NIBBLE(b, xr->orient) + b;
	b = el[0x34 + (xr->link >> 1)];
	sum += NIBBLE(b, xr->link) + b;
	#undef NIBBLE
	return sum;
}


// ROM 0x0033be50 PostCompareXrs__FP11xrd_el_typeT1
long
PostCompareXrs(xrd_el_type* a, xrd_el_type* b)
{
	return a->type != b->type ? 1 : 0;
}


// ROM 0x0033c0d8 ResetChangePPDLetterInfo__FP12_POST_PARAMS
void
ResetChangePPDLetterInfo(POST_PARAMS* pp)
{
	pp->changeLetter = 0;
}


// ROM 0x0033c100 ApplyChangePPDLetterInfo__FP12_POST_PARAMS
// A letter a rule asked for written into the graph in place of the one
// read.
void
ApplyChangePPDLetterInfo(POST_PARAMS* pp)
{
	if (pp->changeLetter != 0)
		pp->rws[pp->cur].sym = pp->changeLetter;
}


// ROM 0x0033c328 IsFirstCharAsDefined__FP12_POST_PARAMS
long
IsFirstCharAsDefined(POST_PARAMS* pp)
{
	return pp->defineEnds == 0 ? pp->noPrev : pp->firstChar;
}


// ROM 0x0033c33c IsLastCharAsDefined__FP12_POST_PARAMS
long
IsLastCharAsDefined(POST_PARAMS* pp)
{
	return pp->defineEnds == 0 ? pp->noNext : pp->lastChar;
}


/*------------------------------------------------------------------------------
	T h e   f u n c t i o n s   o f   t h e   t a b l e
------------------------------------------------------------------------------*/

// ROM 0x00338e04 CalculateAbs__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateAbs(intptr_t a, intptr_t, intptr_t, int*, POST_PARAMS*)
{
	long v = (long) a;
	return v < 0 ? -v : v;
}


// the square root of a bend (CurvMeasure's hundredths), with its sign
static long
SignedRootOfBend(long bend)
{
	if (bend >= 0)
		return HWRMathILSqrt(bend * 100);
	return -HWRMathILSqrt(-bend * 100);
}


// ROM 0x00338e10 CalculateCurvature__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
// How much the trace bends between two xrs: from the first's end to the
// second's start, or when they overlap from the middle of one to the
// other's.  ROM BUG: the "middles" are half of each xr's length, not
// points within it.
static intptr_t
CalculateCurvature(intptr_t a, intptr_t b, intptr_t, int* err, POST_PARAMS* pp)
{
	long i = XrEnd(XrEl(a));
	long j = XrBeg(XrEl(b));
	if (j <= i)
	{
		i = (i - XrBeg(XrEl(a))) / 2;
		j = (XrEnd(XrEl(b)) - j) / 2;
		if (j <= i)
		{
			*err = 0x13;
			return 0;
		}
	}
	if (pp->y[i] == -1 || pp->y[j] == -1)
	{
		*err = 0x17;
		return 0;
	}
	return SignedRootOfBend(CurvMeasure(pp->x, pp->y, i, j, -1));
}


// ROM 0x00339850 CalculateDistanceFromXrToLine__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
// How far the first xr's ends are from the line through the other two's
// outer ends (the further, as a distance).
static intptr_t
CalculateDistanceFromXrToLine(intptr_t a, intptr_t b, intptr_t c, int* err, POST_PARAMS* pp)
{
	long beg = XrBeg(XrEl(a)), end = XrEnd(XrEl(a));
	long p = XrBeg(XrEl(b)), q = XrEnd(XrEl(c));
	if (p >= XrBeg(XrEl(c)))
		p = XrBeg(XrEl(c));
	if (XrEnd(XrEl(b)) > q)
		q = XrEnd(XrEl(b));
	short* x = pp->x;
	short* y = pp->y;
	if (y[p] == -1 || y[q] == -1 || y[beg] == -1 || y[end] == -1)
	{
		*err = 0x17;
		return 0x7fff;
	}
	long d1 = QDistFromChord(x[p], y[p], x[q], y[q], x[beg], y[beg]);
	long d2 = QDistFromChord(x[p], y[p], x[q], y[q], x[end], y[end]);
	return HWRMathILSqrt(d1 > d2 ? d1 : d2);
}


// ROM 0x0033b888 CalculateIBeg__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateIBeg(intptr_t a, intptr_t, intptr_t, int*, POST_PARAMS*)
{
	return XrBeg(XrEl(a));
}


// ROM 0x0033acd0 CalculateXBeg__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateXBeg(intptr_t a, intptr_t, intptr_t, int*, POST_PARAMS* pp)
{
	return pp->x[XrBeg(XrEl(a))];
}


// ROM 0x0033b578 CalculateYBeg__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateYBeg(intptr_t a, intptr_t, intptr_t, int*, POST_PARAMS* pp)
{
	return pp->y[XrBeg(XrEl(a))];
}


// ROM 0x00338ef0 CalculateIEnd__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateIEnd(intptr_t a, intptr_t, intptr_t, int*, POST_PARAMS*)
{
	return XrEnd(XrEl(a));
}


// ROM 0x0033c0e4 CalculateXEnd__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateXEnd(intptr_t a, intptr_t, intptr_t, int*, POST_PARAMS* pp)
{
	return pp->x[XrEnd(XrEl(a))];
}


// ROM 0x0033c350 CalculateYEnd__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateYEnd(intptr_t a, intptr_t, intptr_t, int*, POST_PARAMS* pp)
{
	return pp->y[XrEnd(XrEl(a))];
}


// ROM 0x0033908c CalculateCurvatureOnPoints__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateCurvatureOnPoints(intptr_t a, intptr_t b, intptr_t, int* err, POST_PARAMS* pp)
{
	long i = (short) a, j = (short) b;
	if (j <= i)
	{
		*err = 0x13;
		return 0;
	}
	for (long k = i; k <= j; k = (short) (k + 1))
		if (pp->y[k] == -1)
		{
			*err = 0x17;
			return 0;
		}
	return SignedRootOfBend(CurvMeasure(pp->x, pp->y, i, j, -1));
}


// ROM 0x00338efc CalculateXi__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateXi(intptr_t a, intptr_t, intptr_t, int*, POST_PARAMS* pp)
{
	return pp->x[(short) a];
}


// ROM 0x00338f18 CalculateYi__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateYi(intptr_t a, intptr_t, intptr_t, int*, POST_PARAMS* pp)
{
	return pp->y[(short) a];
}


// ROM 0x00338f34 CalculateILeftPointXr__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateILeftPointXr(intptr_t a, intptr_t, intptr_t, int*, POST_PARAMS* pp)
{
	return ixMin(XrBeg(XrEl(a)), XrEnd(XrEl(a)), pp->x, pp->y);
}


// ROM 0x00338f54 CalculateIRightPointXr__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateIRightPointXr(intptr_t a, intptr_t, intptr_t, int*, POST_PARAMS* pp)
{
	return ixMax(XrBeg(XrEl(a)), XrEnd(XrEl(a)), pp->x, pp->y);
}


// ROM 0x00338f74 CalculateIUpPointXr__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateIUpPointXr(intptr_t a, intptr_t, intptr_t, int*, POST_PARAMS* pp)
{
	return iyMin(XrBeg(XrEl(a)), XrEnd(XrEl(a)), pp->y);
}


// ROM 0x00338f90 CalculateIDownPointXr__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateIDownPointXr(intptr_t a, intptr_t, intptr_t, int*, POST_PARAMS* pp)
{
	return iyMax(XrBeg(XrEl(a)), XrEnd(XrEl(a)), pp->y);
}


// whether the points i..j may be searched: in order, and not one pen-up
static Boolean
TrackOK(long i, long j, int* err, POST_PARAMS* pp)
{
	if (i > j || (i == j && pp->y[i] == -1))
	{
		*err = 0x18;
		return false;
	}
	return true;
}


// ROM 0x00338fac CalculateILeftPointOnTrack__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateILeftPointOnTrack(intptr_t a, intptr_t b, intptr_t, int* err, POST_PARAMS* pp)
{
	if (!TrackOK((short) a, (short) b, err, pp))
		return 0x7fff;
	return ixMin((short) a, (short) b, pp->x, pp->y);
}


// ROM 0x00338ff8 CalculateIRightPointOnTrack__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateIRightPointOnTrack(intptr_t a, intptr_t b, intptr_t, int* err, POST_PARAMS* pp)
{
	if (!TrackOK((short) a, (short) b, err, pp))
		return 0x7fff;
	return ixMax((short) a, (short) b, pp->x, pp->y);
}


// ROM 0x00339044 CalculateIUpPointOnTrack__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateIUpPointOnTrack(intptr_t a, intptr_t b, intptr_t, int* err, POST_PARAMS* pp)
{
	if (!TrackOK((short) a, (short) b, err, pp))
		return 0x7fff;
	return iyMin((short) a, (short) b, pp->y);
}


// ROM 0x00339150 CalculateIDownPointOnTrack__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateIDownPointOnTrack(intptr_t a, intptr_t b, intptr_t, int* err, POST_PARAMS* pp)
{
	if (!TrackOK((short) a, (short) b, err, pp))
		return 0x7fff;
	return iyMax((short) a, (short) b, pp->y);
}


// ROM 0x003392ec Calculate1stPointIndex__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
// The letter's first point: its first xr's start, moved back to just
// after a pen-up between it and the end of the letter before (0 for the
// word's first letter).  ROM QUIRK: the point is only moved when a
// pen-up is found; otherwise it stays where it was.
static intptr_t
Calculate1stPointIndex(intptr_t, intptr_t, intptr_t, int* err, POST_PARAMS* pp)
{
	short syms[2] = { pp->cur, -1 };
	long iMin, iMax, pMin, pMax;
	if (!FindIRangeOfCorrXRs(pp->ppd, pp->xr, syms, 0, &iMin, &iMax))
	{
		*err = 0x1b;
		return 1;
	}
	long first = XrBeg(XrAt(pp->xr, iMin));
	long prevEnd;
	if (IsFirstCharAsDefined(pp) != 0)
		prevEnd = 0;
	else if (pp->noPrev == 0 && FindIRangeOfCorrXRs(pp->ppd, pp->xr, pp->prevSyms, 0, &pMin, &pMax))
		prevEnd = XrEnd(XrAt(pp->xr, pMax));
	else
		prevEnd = first;
	if (prevEnd < first)
	{
		long i = first;
		for (;;)
		{
			if (i <= prevEnd)
			{
				if (pp->y[i] != -1)
					break;
				first = i + 1;
				break;
			}
			if (pp->y[i] == -1)
			{
				first = i + 1;
				break;
			}
			i--;
		}
	}
	if (pp->y[first] == -1)
		*err = 0x17;
	return first;
}


// ROM 0x0033945c CalculateLastPointIndex__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
// The letter's last point: its last xr's end, moved on towards the start
// of the letter after while the pen stays down, less one (the trace's
// last point for the word's last letter).
static intptr_t
CalculateLastPointIndex(intptr_t, intptr_t, intptr_t, int* err, POST_PARAMS* pp)
{
	short syms[2] = { pp->cur, -1 };
	long iMin, iMax, nMin, nMax;
	if (!FindIRangeOfCorrXRs(pp->ppd, pp->xr, syms, 0, &iMin, &iMax))
	{
		*err = 0x1b;
		return 1;
	}
	long last = XrEnd(XrAt(pp->xr, iMax));
	long nextBeg;
	if (IsLastCharAsDefined(pp) != 0)
		nextBeg = pp->nPoints - 1;
	else if (pp->noNext == 0 && FindIRangeOfCorrXRs(pp->ppd, pp->xr, pp->nextSyms, 0, &nMin, &nMax))
		nextBeg = XrBeg(XrAt(pp->xr, nMin));
	else
		nextBeg = last;
	if (last < nextBeg)
	{
		while (last < nextBeg && pp->y[last] != -1)
			last++;
		last--;
	}
	if (pp->y[last] == -1)
		*err = 0x17;
	return last;
}


// ROM 0x003395c8 vy3__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t	vy3(intptr_t a, intptr_t, intptr_t, int*, POST_PARAMS*)	{ return (short) a; }
// ROM 0x003395d4 vy4__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t	vy4(intptr_t a, intptr_t, intptr_t, int*, POST_PARAMS*)	{ return (short) a; }
// ROM 0x003395e0 vy5__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t	vy5(intptr_t a, intptr_t, intptr_t, int*, POST_PARAMS*)	{ return (short) a; }
// ROM 0x003395ec vy6__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t	vy6(intptr_t a, intptr_t, intptr_t, int*, POST_PARAMS*)	{ return (short) a; }


// ROM 0x003395f8 RetOneIfLettersCompeteForXRs__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
// How much of the letter's stretch of xrs another letter of the graph -
// one of the (up to three) characters named - claims: the largest share,
// in tenths, of the two stretches' union that their overlap is (by xrs
// times points).  10 when switched off.
static intptr_t
RetOneIfLettersCompeteForXRs(intptr_t a, intptr_t b, intptr_t c, int*, POST_PARAMS* pp)
{
	if (pp->competeOn == 0)
		return 10;
	// ROM QUIRK: the fourth byte of the list is never set; the loop stops at three
	UByte letters[4] = { (UByte) a, (UByte) b, (UByte) c, 0 };
	short syms[2] = { pp->cur, -1 };
	long iMin, iMax;
	if (!FindIRangeOfCorrXRs(pp->ppd, pp->xr, syms, 0, &iMin, &iMax))
		return 0;
	long best = 0;
	for (short s = 0; s < pp->rwg->size; s = (short) (s + 1))
	{
		RWS_type* e = &pp->rws[s];
		if (e->type != 1)
			continue;
		for (short k = 0; k < 3 && letters[k] != 0; k = (short) (k + 1))
		{
			if (OSToRec(e->realSym) != letters[k])
				continue;
			syms[0] = s;
			long oMin, oMax;
			if (!FindIRangeOfCorrXRs(pp->ppd, pp->xr, syms, 0, &oMin, &oMax))
				continue;
			if (oMin > iMax)
				continue;
			long lo, loU;		// the overlap's start, the union's
			if (oMin <= iMin) { lo = iMin; loU = oMin; } else { lo = oMin; loU = iMin; }
			if (oMax < iMin)
				continue;
			long hi, hiU;
			if (oMax >= iMax) { hi = iMax; hiU = oMax; } else { hi = oMax; hiU = iMax; }
			long inner = (hi - lo + 1) * (XrEnd(XrAt(pp->xr, hi)) - XrBeg(XrAt(pp->xr, lo)) + 1);
			long outer = (hiU - loU + 1) * (XrEnd(XrAt(pp->xr, hiU)) - XrBeg(XrAt(pp->xr, loU)) + 1);
			long share = SDiv(inner * 10, outer);
			if (best < share)
				best = share;
		}
	}
	return best;
}


// ROM 0x0033981c SameAsFrom_but_Fatal__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
SameAsFrom_but_Fatal(intptr_t a, intptr_t b, intptr_t c, int* err, POST_PARAMS* pp)
{
	intptr_t r = RetOneIfLettersCompeteForXRs(a, b, c, err, pp);
	if (r == 0)
		*err = 0x1e;
	return r;
}


// ROM 0x00339978 ReturnOneIfLanguagePresent__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
// Whether the field's language (rc +6) has any of a's bits; a negative a
// that has none fails with 0x1d.
static intptr_t
ReturnOneIfLanguagePresent(intptr_t a, intptr_t, intptr_t, int* err, POST_PARAMS* pp)
{
	long bits = (short) HWRLAbs((long) a);
	if (((short) RCGetH(pp->rc, 6) & bits) != 0)
		return 1;
	if ((long) a < 0)
		*err = 0x1d;
	return 0;
}


// ROM 0x003399c4 CalculateDFromPointToLine__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
// The distance of point p from the line through points a and b, negative
// on its left (going from a to b).
static intptr_t
CalculateDFromPointToLine(intptr_t p, intptr_t a, intptr_t b, int* err, POST_PARAMS* pp)
{
	short i = (short) p, j = (short) a, k = (short) b;
	short* x = pp->x;
	short* y = pp->y;
	long xj = x[j], xk = x[k], yj = y[j], yk = y[k], xi = x[i], yi = y[i];
	if (yj == -1 || yk == -1 || yi == -1)
	{
		*err = 0x17;
		return 0x7fff;
	}
	long d = HWRMathILSqrt(QDistFromChord(xj, yj, xk, yk, xi, yi));
	if (xj == xk)
	{
		if (yj < yk && xi < xj)
			return -d;
		if (yj > yk && xi > xj)
			return -d;
		return d;
	}
	long dx = xk - xj, dy = yk - yj;
	long side = (int32_t) (dx * yi - dy * xi + (dy * xj - dx * yj));
	long up;
	if (dx < 0)
	{
		side = -side;
		up = 0;
	}
	else
		up = 1;
	long neg = side < 0 ? 0 : 1;
	if (up != neg)
		return d;
	return -d;
}


// ROM 0x00339ae0 CalculateIBeakOnXR__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateIBeakOnXR(intptr_t a, intptr_t, intptr_t, int*, POST_PARAMS* pp)
{
	long beg = XrBeg(XrEl(a));
	long at;
	if (FindBeak(pp->x, pp->y, beg, XrEnd(XrEl(a)), &at) == 1)
		return beg;
	return at;
}


// ROM 0x00339b34 CalculateIBeakBetweenPoints__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateIBeakBetweenPoints(intptr_t a, intptr_t b, intptr_t, int* err, POST_PARAMS* pp)
{
	long at;
	if (FindBeak(pp->x, pp->y, (long) a, (long) b, &at) == 1)
	{
		*err = 0x11;
		return 0;
	}
	return at;
}


// ROM 0x00339dfc CalculateSimplicityLine__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
// How much the trace between two points bends, thinned to its corners
// (filtr_gid, the chords a twelfth of the box): each corner's
// (cosine + 100) weighed by how long its shorter side is against the
// mean; in hundredths.
static intptr_t
CalculateSimplicityLine(intptr_t a, intptr_t b, intptr_t, int* err, POST_PARAMS* pp)
{
	long lo = (long) a, hi = (long) b;
	if (lo > hi)
	{
		long t = lo;
		lo = hi;
		hi = t;
	}
	long count = 50;
	_RECT box;
	GetTraceBox(pp->x, pp->y, lo, hi, &box);
	long size = box.bottom - box.top;
	if (size < box.right - box.left)
		size = box.right - box.left;
	long dist = SDiv(size + 6, 12);
	if (dist < 1)
		dist = 1;
	short idx[50], yOut[50], xOut[50];
	if (filtr_gid(hi - lo + 1, pp->x + lo, pp->y + lo, xOut, yOut, idx, &count, 0x50, dist) != 0)
	{
		*err = 0x11;
		return 0x7fff;
	}
	long n = count - 1;
	if (n <= 1)
		return 0;
	long total = 0;
	for (long k = 0; k < n; k++)
		total += Distance8(xOut[k], yOut[k], xOut[k + 1], yOut[k + 1]);
	long half = SDiv(total + (n >> 1), n) >> 1;
	long sum = 0;
	long prev = Distance8(xOut[0], yOut[0], xOut[1], yOut[1]);
	for (long k = 1; k < n; k++)
	{
		long c = cos_pointvect(xOut[k], yOut[k], xOut[k - 1], yOut[k - 1], xOut[k], yOut[k], xOut[k + 1], yOut[k + 1]);
		long before = prev;
		prev = Distance8(xOut[k], yOut[k], xOut[k + 1], yOut[k + 1]);
		long m = before >= prev ? prev : before;
		long w;
		if (m < half)
			w = SDiv(m * SDiv(m * 100, half), half);
		else
			w = 100;
		sum += w * HWRLAbs(c + 100);
	}
	return SDiv(sum + 50, 100);
}


// ROM 0x0033a0b8 CalculateIfFirstChar__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateIfFirstChar(intptr_t, intptr_t, intptr_t, int*, POST_PARAMS* pp)
{
	return IsFirstCharAsDefined(pp) != 0 ? 1 : 0;
}


// ROM 0x0033a0d8 CalculateIfLastChar__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateIfLastChar(intptr_t, intptr_t, intptr_t, int*, POST_PARAMS* pp)
{
	return IsLastCharAsDefined(pp) != 0 ? 1 : 0;
}


// ROM 0x0033aa70 CalculateMin__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateMin(intptr_t a, intptr_t b, intptr_t, int*, POST_PARAMS*)
{
	return a >= b ? b : a;
}


// ROM 0x0033aa7c CalculateMax__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateMax(intptr_t a, intptr_t b, intptr_t, int*, POST_PARAMS*)
{
	return a <= b ? b : a;
}


// ROM 0x0033aa88 CalculateNXrInterXr__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
// How many xrs lie between two.
static intptr_t
CalculateNXrInterXr(intptr_t a, intptr_t b, intptr_t, int*, POST_PARAMS*)
{
	uintptr_t d = (uintptr_t) a <= (uintptr_t) b ? (uintptr_t) b - (uintptr_t) a : (uintptr_t) a - (uintptr_t) b;
	return SDiv((long) d, sizeof(xrd_el_type)) - 1;
}


// ROM 0x0033aab0 CalculateNStrongXrInterXr__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateNStrongXrInterXr(intptr_t a, intptr_t b, intptr_t, int*, POST_PARAMS*)
{
	xrd_el_type* lo = XrEl(a);
	xrd_el_type* hi = XrEl(b);
	if ((uintptr_t) a > (uintptr_t) b)
	{
		hi = XrEl(a);
		lo = XrEl(b);
	}
	long n = 0;
	for (xrd_el_type* el = lo + 1; el < hi; el++)
		if (X_IsStrongElem(el))
			n++;
	return n;
}


// ROM 0x0033ab04 CheckAfterNextBreak__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
// Whether a break follows the xr, or one after (the next being the last
// and marked next to a break counts).
static intptr_t
CheckAfterNextBreak(intptr_t a, intptr_t, intptr_t, int*, POST_PARAMS*)
{
	xrd_el_type* el = XrEl(a);
	if (el->type == 0 || el[1].type == 0)
		return 0;
	xrd_el_type* next = el + 1;
	if (X_IsBreak(next))
		return 1;
	if (next[1].type == 0 && (next->attrib & 0x80) != 0)
		return 1;
	return X_IsBreak(next + 1) ? 1 : 0;
}


// ROM 0x0033ab70 CheckBeforeNextBreak__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CheckBeforeNextBreak(intptr_t a, intptr_t, intptr_t, int*, POST_PARAMS* pp)
{
	xrd_el_type* el = XrEl(a);
	xrd_el_type* base = (xrd_el_type*) pp->xr->fElements;
	if (el->type == 0)
		return 0;
	xrd_el_type* prev = el - 1;
	if (prev < base)
		return 0;
	if (X_IsBreak(prev))
		return 1;
	xrd_el_type* before = prev - 1;
	if (before <= base && (prev->attrib & 0x80) != 0)
		return 1;
	if (before <= base)
		return 0;
	return X_IsBreak(before) ? 1 : 0;
}


// ROM 0x0033a948 CalculateHeightXR__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateHeightXR(intptr_t a, intptr_t, intptr_t, int*, POST_PARAMS*)
{
	return XrEl(a)->height;
}


// ROM 0x0033abf4 CalculateBoolLessThan__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateBoolLessThan(intptr_t a, intptr_t b, intptr_t, int*, POST_PARAMS*)
{
	return a < b ? 1 : 0;
}


// ROM 0x0033ac04 CalculateDistBetweenXr__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateDistBetweenXr(intptr_t a, intptr_t b, intptr_t, int* err, POST_PARAMS* pp)
{
	short flag = 0;
	long d = CalcDistBetwXr(pp->x, pp->y, XrBeg(XrEl(a)), XrEnd(XrEl(a)), XrBeg(XrEl(b)), XrEnd(XrEl(b)), &flag);
	if (flag != 0)
		*err = 0x17;
	return d;
}


// ROM 0x0033ac74 CalculateDist8BetwIPts__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateDist8BetwIPts(intptr_t a, intptr_t b, intptr_t, int* err, POST_PARAMS* pp)
{
	long i = (long) a, j = (long) b;
	if (pp->y[i] == -1 || pp->y[j] == -1)
	{
		*err = 0x17;
		return 0x7fff;
	}
	return Distance8(pp->x[i], pp->y[i], pp->x[j], pp->y[j]);
}


// ROM 0x0033acec CalculateIfStrongElem__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateIfStrongElem(intptr_t a, intptr_t, intptr_t, int*, POST_PARAMS*)
{
	return X_IsStrongElem(XrEl(a)) ? 1 : 0;
}


// the letter's box, one side of it (which: 0 the letter; track: the
// trace between its xrs counted leftwards)
static intptr_t
LetterBoxSide(intptr_t which, ULong track, long side, int* err, POST_PARAMS* pp)
{
	_RECT box;
	if (FindXrLetterBox((long) which, pp, &box, track, err) == 0)
		return 0x7fff;
	switch (side)
	{
	case kXrLeft:	return box.left;
	case kXrTop:	return box.top;
	case kXrRight:	return box.right;
	}
	return box.bottom;
}


// ROM 0x0033b2cc CalculateLetterXRight__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateLetterXRight(intptr_t a, intptr_t, intptr_t, int* err, POST_PARAMS* pp)
{
	return LetterBoxSide(a, 1, kXrRight, err, pp);
}


// ROM 0x0033b30c CalculateLetterXLeft__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateLetterXLeft(intptr_t a, intptr_t, intptr_t, int* err, POST_PARAMS* pp)
{
	return LetterBoxSide(a, 1, kXrLeft, err, pp);
}


// ROM 0x0033b34c CalculateLetterYTop__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateLetterYTop(intptr_t a, intptr_t, intptr_t, int* err, POST_PARAMS* pp)
{
	return LetterBoxSide(a, 0, kXrTop, err, pp);
}


// ROM 0x0033b38c CalculateLetterYBottom__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateLetterYBottom(intptr_t a, intptr_t, intptr_t, int* err, POST_PARAMS* pp)
{
	return LetterBoxSide(a, 0, kXrBottom, err, pp);
}


// ROM 0x0033b3cc CalculateXrToXrSlope__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
// The slope from the first xr's middle point to the second's: fifty
// times rise over run, within +-200.
static intptr_t
CalculateXrToXrSlope(intptr_t a, intptr_t b, intptr_t, int* err, POST_PARAMS* pp)
{
	long i = (XrBeg(XrEl(a)) + XrEnd(XrEl(a))) >> 1;
	long j = (XrBeg(XrEl(b)) + XrEnd(XrEl(b))) >> 1;
	if (pp->y[i] == -1 || pp->y[j] == -1)
	{
		*err = 0x17;
		return 0;
	}
	long dx = pp->x[j] - pp->x[i];
	if (dx < 0)
		dx = -dx;
	long dy = pp->y[i] - pp->y[j];
	if (dx == 0)
		return dy <= 0 ? -200 : 200;
	long s = SDiv(dy * 50, dx);
	if (s > 200)
		return 200;
	if (s >= -200)
		return s;
	return -200;
}


// ROM 0x0033b498 CalculateClosedSquare__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateClosedSquare(intptr_t a, intptr_t b, intptr_t, int* err, POST_PARAMS* pp)
{
	short flag;
	long s = ClosedSquare(pp->x, pp->y, (long) a, (long) b, &flag);
	if (flag != 0)
		*err = flag == 1 ? 0x18 : flag == 2 ? 0x17 : 0xf;
	return s;
}


// ROM 0x0033b4fc CalculateTriangleSquare__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateTriangleSquare(intptr_t a, intptr_t b, intptr_t c, int* err, POST_PARAMS* pp)
{
	short i = (short) a, j = (short) b, k = (short) c;
	if (pp->y[i] == -1 || pp->y[j] == -1 || pp->y[k] == -1)
	{
		*err = 0x17;
		return 0;
	}
	return TriangleSquare(pp->x, pp->y, i, j, k);
}


// an extremum of the xr's stretch, and the other coordinate there
static intptr_t
XrExtremum(long i, Boolean wantX, int* err, POST_PARAMS* pp)
{
	if (i >= 0 && i < pp->nPoints)
		return wantX ? pp->x[i] : pp->y[i];
	*err = 0x18;
	return 0;
}


// ROM 0x0033b594 CalculateXofXrTop__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateXofXrTop(intptr_t a, intptr_t, intptr_t, int* err, POST_PARAMS* pp)
{
	return XrExtremum(iyMin(XrBeg(XrEl(a)), XrEnd(XrEl(a)), pp->y), true, err, pp);
}


// ROM 0x0033b5f0 CalculateXofXrBottom__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateXofXrBottom(intptr_t a, intptr_t, intptr_t, int* err, POST_PARAMS* pp)
{
	return XrExtremum(iyMax(XrBeg(XrEl(a)), XrEnd(XrEl(a)), pp->y), true, err, pp);
}


// ROM 0x0033b64c CalculateYofXrLeft__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateYofXrLeft(intptr_t a, intptr_t, intptr_t, int* err, POST_PARAMS* pp)
{
	return XrExtremum(ixMin(XrBeg(XrEl(a)), XrEnd(XrEl(a)), pp->x, pp->y), false, err, pp);
}


// ROM 0x0033b6ac CalculateYofXrRight__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateYofXrRight(intptr_t a, intptr_t, intptr_t, int* err, POST_PARAMS* pp)
{
	return XrExtremum(ixMax(XrBeg(XrEl(a)), XrEnd(XrEl(a)), pp->x, pp->y), false, err, pp);
}


// ROM 0x0033b70c CalculateIfXrIsPresent__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateIfXrIsPresent(intptr_t a, intptr_t, intptr_t, int*, POST_PARAMS* pp)
{
	return FindXrIndex(pp->ppd, pp->cur, (long) a & 0xff) < 0 ? 0 : 1;
}


// ROM 0x0033b740 SetInternalVariable__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
SetInternalVariable(intptr_t a, intptr_t b, intptr_t, int* err, POST_PARAMS* pp)
{
	if (a >= 0 && a < 10)
	{
		pp->vars[a] = b;
		return b;
	}
	*err = 0x1c;
	return 0;
}


// ROM 0x0033b770 GetInternalVariable__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
GetInternalVariable(intptr_t a, intptr_t, intptr_t, int* err, POST_PARAMS* pp)
{
	if (a >= 0 && a < 10)
		return pp->vars[a];
	*err = 0x1c;
	return 0;
}


// ROM 0x0033b79c CalculateMostFarPt__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateMostFarPt(intptr_t a, intptr_t b, intptr_t, int* err, POST_PARAMS* pp)
{
	short lo = (short) (a >= b ? b : a);
	short hi = (short) (a > b ? a : b);
	for (short i = lo; i <= hi; i = (short) (i + 1))
		if (pp->y[i] == -1)
		{
			*err = 0x17;
			return 0;
		}
	return iMostFarFromChord(pp->x, pp->y, lo, hi);
}


// ROM 0x0033b820 CalculateIfXrIDEqual__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateIfXrIDEqual(intptr_t a, intptr_t b, intptr_t, int*, POST_PARAMS*)
{
	if (a == 0)
		return 0;
	return XrEl(a)->type == ((long) b & 0xff) ? 1 : 0;
}


// ROM 0x0033b840 CalculateBDShapeTip__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateBDShapeTip(intptr_t a, intptr_t b, intptr_t, int*, POST_PARAMS* pp)
{
	long lo = (long) (a >= b ? b : a);
	long hi = (long) (a <= b ? b : a);
	long at;
	IsRightGulfLikeIn3(pp->x, pp->y, lo, hi, &at);
	return at;
}


// ROM 0x0033bb6c ReturnZeroIfDoubleSkip__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
// Whether the letter's prototype element that read an xr of this one's
// type skipped it (0) or read it with a correlation (1).
// ROM BUG: the element is looked for by giving the xr the next type up
// for a moment and asking PostCompareXrs whether the two differ, which
// they then always do, so nothing is found and the function always fails
// with 0x1b.
static intptr_t
ReturnZeroIfDoubleSkip(intptr_t a, intptr_t, intptr_t, int* err, POST_PARAMS* pp)
{
	xrd_el_type* el = XrEl(a);
	RWG_PPD_type* ppd = pp->ppd;
	short cur = pp->cur;
	xrd_el_type* base = (xrd_el_type*) pp->xr->fElements;
	long matched = 0;			// (never set: the ROM's stack word)
	*err = 0;
	short j = 0;
	if (ppd[cur].el[0][1] == 0)
		goto fail;
	for (;;)
	{
		long k = FindXrIndex(ppd, cur, j);
		if (k != -1 && el->type == base[k].type)
		{
			UByte type = el->type;
			el->type = (UByte) (type + 1);
			long differ = PostCompareXrs(el, base + k);
			el->type = type;
			if (differ == 0)
				break;
		}
		j = (short) (j + 1);
		if (ppd[cur].el[j][1] == 0)
		{
			if (matched != 0)
				break;
			goto fail;
		}
	}
	{
		UByte how = ppd[cur].el[j][1];
		if (how == 1)
			return 0;
		if (how != 3)
			goto fail;
		return GetXrCorr(el, OSToRec(pp->rws[cur].realSym), pp->rws[cur].var, j, (DTIHeader*) pp->rc->fDTI) != 0 ? 1 : 0;
	}
fail:
	*err = 0x1b;
	return 0;
}


// ROM 0x0033be68 CalculateClosestByY__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateClosestByY(intptr_t a, intptr_t b, intptr_t c, int* err, POST_PARAMS* pp)
{
	short v = (short) a, i = (short) b, j = (short) c;
	if (i > j)
	{
		short t = i;
		i = j;
		j = t;
	}
	long r = iClosestToY(pp->y, i, j, v);
	if (r < 0)
	{
		*err = 0xd;
		return 0;
	}
	return r;
}


// ROM 0x0033bec0 CalculateIfIsXrIDInLetter__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
// Whether an xr of either type lies within the letter's stretch (which:
// 0 up to the next letter's start, -1 from the one before's end, 1 as far
// as the next one's start) - a mark type's stretch starting one earlier.
static intptr_t
CalculateIfIsXrIDInLetter(intptr_t a, intptr_t b, intptr_t which, int* err, POST_PARAMS* pp)
{
	UByte t1 = (UByte) a, t2 = (UByte) b;
	xrd_el_type* base = (xrd_el_type*) pp->xr->fElements;
	short syms[2] = { pp->cur, -1 };
	long iMin, iMax, oMin, oMax;
	long from, to;
	if (!FindIRangeOfCorrXRs(pp->ppd, pp->xr, syms, 1, &iMin, &iMax))
	{
		*err = 0x1b;
		return 0;
	}
	if (which == -1)
	{
		// from the letter before's first xr to its last or to just before
		// this one's first, whichever is later
		if (pp->noPrev != 0 || !FindIRangeOfCorrXRs(pp->ppd, pp->xr, pp->prevSyms, 1, &oMin, &oMax))
			return 0;
		from = oMin;
		to = (iMin - 1 >= oMax) ? iMin - 1 : oMax;
	}
	else if (which == 0)
	{
		// from this one's first to its last or to just before the next
		// one's first, whichever is later
		from = iMin;
		if (pp->noNext != 0 || !FindIRangeOfCorrXRs(pp->ppd, pp->xr, pp->nextSyms, 1, &oMin, &oMax))
			to = iMax;
		else
			to = (oMin - 1 >= iMax) ? oMin - 1 : iMax;
	}
	else if (which == 1)
	{
		if (pp->noNext != 0 || !FindIRangeOfCorrXRs(pp->ppd, pp->xr, pp->nextSyms, 1, &oMin, &oMax))
			return 0;
		from = oMin;
		to = oMax;
	}
	else
	{
		*err = 0xd;
		return 0;
	}
	if (t1 == 0x36 || t1 == 0x34 || t1 == 0x3a || t1 == 0x3b || t2 == 0x36 || t2 == 0x34 || t2 == 0x3a || t2 == 0x3b)
		if (from > 0)
			from--;
	for (long i = from; i <= to; i++)
		if (base[i].type == t1 || base[i].type == t2)
			return 1;
	return 0;
}


// ROM 0x0033c0b0 CalculateBaselineMedian__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateBaselineMedian(intptr_t, intptr_t, intptr_t, int*, POST_PARAMS* pp)
{
	return (short) RCGetH(pp->rc, 0xea);
}


// ROM 0x0033c0c4 CalculateBaselineDown__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
CalculateBaselineDown(intptr_t, intptr_t, intptr_t, int*, POST_PARAMS* pp)
{
	return (short) RCGetH(pp->rc, 0xec);
}


// ROM 0x0033c120 ChangePPDLetter__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
// The letter to be read as another (a), unless one surer (b) was asked
// for already.
static intptr_t
ChangePPDLetter(intptr_t a, intptr_t b, intptr_t, int*, POST_PARAMS* pp)
{
	if (pp->changeLetter == 0 || pp->changePriority < (long) b)
	{
		pp->changeLetter = (UByte) a;
		pp->changePriority = (long) b;
	}
	return 0;
}


// ROM 0x0033c150 XrIfItExists_NULL_Otherwise__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
XrIfItExists_NULL_Otherwise(intptr_t a, intptr_t, intptr_t, int*, POST_PARAMS* pp)
{
	long k = FindXrIndex(pp->ppd, pp->cur, (long) a & 0xff);
	if (k < 0)
		return 0;
	return (intptr_t) XrAt(pp->xr, k);
}


// ROM 0x0033c194 SelfCrossingOfMinSquare__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
SelfCrossingOfMinSquare(intptr_t a, intptr_t b, intptr_t c, int*, POST_PARAMS* pp)
{
	return CurveHasSelfCrossing(pp->x, pp->y, (long) a, (long) b, nil, nil, (long) c) != 0 ? 1 : 0;
}


// ROM 0x0033c1e0 IsNextLetterOneOfThese__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
IsNextLetterOneOfThese(intptr_t a, intptr_t b, intptr_t c, int*, POST_PARAMS* pp)
{
	UByte n = pp->nextChar;
	return (n == (UByte) a || n == (UByte) b || n == (UByte) c) ? 1 : 0;
}


// ROM 0x0033c20c IsPrevLetterOneOfThese__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
static intptr_t
IsPrevLetterOneOfThese(intptr_t a, intptr_t b, intptr_t c, int*, POST_PARAMS* pp)
{
	UByte p = pp->prevChar;
	return (p == (UByte) a || p == (UByte) b || p == (UByte) c) ? 1 : 0;
}


// ROM 0x0033c238 MaxWidthInFork__FlN21PiP13PS_point_typesPsT7P8ppd_typeUiP11xrdata_type
// The widest the fork from point a opens between points b and c: for
// each point after b, the point before it (from a) at the nearest
// height, and how far apart they are across; the pair kept in the
// variables 8 and 9.
static intptr_t
MaxWidthInFork(intptr_t a, intptr_t b, intptr_t c, int* err, POST_PARAMS* pp)
{
	short* y = pp->y;
	short* x = pp->x;
	long i = (short) a, j = (short) b, k = (short) c;
	pp->vars[8] = i;
	pp->vars[9] = k;
	if (!(i < j && j < k))
	{
		*err = 0xd;
		return 0;
	}
	long widest = 0;
	for (long p = j + 1; p <= k; p++)
	{
		long q = iClosestToY(y, i, j - 1, y[p]);
		if (q < 0)
		{
			*err = 0x17;
			return 0;
		}
		long w = x[p] - x[q];
		if (w < 0)
			w = -w;
		if (w > widest)
		{
			widest = w;
			pp->vars[8] = q;
			pp->vars[9] = p;
		}
	}
	return widest;
}


/*------------------------------------------------------------------------------
	T h e   t a b l e
------------------------------------------------------------------------------*/

// The host's functions for the ROM's, in the order of the Functions table
// (generated); each with the word the table holds for it - the address of
// the function's entry in the ROM's own list of them (0x0016877c-),
// which branches on to its body - that PPFunctionAt checks the table
// against.
static const struct { ULong entry; PPFunction fn; } kPPFunctions[74] =
{
	{ 0x01b28470, CalculateAbs },
	{ 0x01b29480, CalculateCurvature },
	{ 0x01b29490, CalculateDistanceFromXrToLine },
	{ 0x01b294b0, CalculateIBeg },
	{ 0x01b2a518, CalculateXBeg },
	{ 0x01b2a524, CalculateYBeg },
	{ 0x01b294bc, CalculateIEnd },
	{ 0x01b2a51c, CalculateXEnd },
	{ 0x01b2a528, CalculateYEnd },
	{ 0x01b29484, CalculateCurvatureOnPoints },
	{ 0x01b2a520, CalculateXi },
	{ 0x01b2a52c, CalculateYi },
	{ 0x01b294d0, CalculateILeftPointXr },
	{ 0x01b2b5b4, Calculate1stPointIndex },
	{ 0x01b2b5e4, CalculateLastPointIndex },
	{ 0x01b2b588, vy3 },
	{ 0x01b2b58c, vy4 },
	{ 0x01b2b590, vy5 },
	{ 0x01b2b594, vy6 },
	{ 0x01b3083c, RetOneIfLettersCompeteForXRs },
	{ 0x01b30840, ReturnOneIfLanguagePresent },
	{ 0x01b294d8, CalculateIRightPointXr },
	{ 0x01b294e0, CalculateIUpPointXr },
	{ 0x01b294b8, CalculateIDownPointXr },
	{ 0x01b29488, CalculateDFromPointToLine },
	{ 0x01b294a8, CalculateIBeakBetweenPoints },
	{ 0x01b294ac, CalculateIBeakOnXR },
	{ 0x01b2a510, CalculateSimplicityLine },
	{ 0x01b294c0, CalculateIfFirstChar },
	{ 0x01b294c4, CalculateIfLastChar },
	{ 0x01b294fc, CalculateMin },
	{ 0x01b294f8, CalculateMax },
	{ 0x01b2a508, CalculateNXrInterXr },
	{ 0x01b2a530, CheckAfterNextBreak },
	{ 0x01b2a534, CheckBeforeNextBreak },
	{ 0x01b2a504, CalculateNStrongXrInterXr },
	{ 0x01b294cc, CalculateILeftPointOnTrack },
	{ 0x01b294d4, CalculateIRightPointOnTrack },
	{ 0x01b294dc, CalculateIUpPointOnTrack },
	{ 0x01b294b4, CalculateIDownPointOnTrack },
	{ 0x01b294a4, CalculateHeightXR },
	{ 0x01b28478, CalculateBoolLessThan },
	{ 0x01b29494, CalculateDistBetweenXr },
	{ 0x01b2948c, CalculateDist8BetwIPts },
	{ 0x01b294c8, CalculateIfStrongElem },
	{ 0x01b294e8, CalculateLetterXLeft },
	{ 0x01b294ec, CalculateLetterXRight },
	{ 0x01b294f4, CalculateLetterYTop },
	{ 0x01b294f0, CalculateLetterYBottom },
	{ 0x01b32960, CalculateXrToXrSlope },
	{ 0x01b2b5c8, CalculateClosedSquare },
	{ 0x01b2b5f0, CalculateTriangleSquare },
	{ 0x01b2b5f8, CalculateXofXrTop },
	{ 0x01b2b5f4, CalculateXofXrBottom },
	{ 0x01b2b5fc, CalculateYofXrLeft },
	{ 0x01b2c600, CalculateYofXrRight },
	{ 0x01b2b5e0, CalculateIfXrIsPresent },
	{ 0x01b31884, SetInternalVariable },
	{ 0x01b2e730, GetInternalVariable },
	{ 0x01b2b5e8, CalculateMostFarPt },
	{ 0x01b2b5dc, CalculateIfXrIDEqual },
	{ 0x01b2b5b8, CalculateBDShapeTip },
	{ 0x01b30844, ReturnZeroIfDoubleSkip },
	{ 0x01b2b5cc, CalculateClosestByY },
	{ 0x01b2b5d8, CalculateIfIsXrIDInLetter },
	{ 0x01b2b5c0, CalculateBaselineMedian },
	{ 0x01b2c604, ChangePPDLetter },
	{ 0x01b2b5bc, CalculateBaselineDown },
	{ 0x01b30850, SameAsFrom_but_Fatal },
	{ 0x01b318d8, XrIfItExists_NULL_Otherwise },
	{ 0x01b3086c, SelfCrossingOfMinSquare },
	{ 0x01b339e4, IsNextLetterOneOfThese },
	{ 0x01b339e8, IsPrevLetterOneOfThese },
	{ 0x01b339f8, MaxWidthInFork },
};


// host: the Functions table's entry id's function and how many arguments
// it takes; nil for none.
PPFunction
PPFunctionAt(long id, long* nargs)
{
	if (id < 0 || id >= RD_N_PP_FUNCTIONS[0] || id >= 74)
		return nil;
	*nargs = Functions[id * 9 + 8];
	if ((ULong) Functions[id * 9 + 1] != kPPFunctions[id].entry)
		return nil;
	return kPPFunctions[id].fn;
}
