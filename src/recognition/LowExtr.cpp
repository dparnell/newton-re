/*
	File:		LowExtr.cpp

	Contains:	The cursive reader's low level: the extrema of each
				stroke in a direction, recorded as special elements.
				See LowLevel.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	A stroke is looked at along a direction (a, b): each point's value
	is its x and y weighed by the direction over |a| + |b|.  A point is a
	maximum when the run of points within `eps` of its value is bounded
	by lower values on both sides (or by the stroke's end on one of
	them), and a minimum likewise; the element made covers the run, with
	ipoint0 the middle of the flat top (or bottom) inside it.  Maxima and
	minima alternate: one is not recorded right after one of its own
	kind.  Each stroke is bracketed by a 0x10 element at its start and a
	0x20 at its end.

	The directions and the kinds of element they make (max, min): 2 up
	and down (3, 1), 1 left and right (0x13, 0x11), 4 the diagonal
	(0x23, 0x21), 8 the other diagonal (0x33, 0x31).
*/

#include "LowLevel.h"
#include "ParaGraph.h"


// A direction to look for extrema in.
struct _ENVIRONS
{
	short		iBeg;			// +00  the stroke's first point
	short		iEnd;			// +02  and its last
	short		eps;			// +04  what counts as flat
	short		a;				// +06  the direction: x's weight
	short		b;				// +08  y's
	short		sum;			// +0a  |a| + |b|
	UByte		maxMark;		// +0c
	UByte		minMark;		// +0d
};

static long	BigExtr(low_type* low, short iBeg, short iEnd, short kind, short eps);
static long	DirectExtr(low_type* low, _ENVIRONS* env, SPEC_TYPE* elem, short i);
static long	Mark(low_type* low, UByte mark, UByte code, UByte attr, UByte other, short iBeg, short iEnd, short ipoint0, short ipoint1);
static long	NoteSpecl(low_type* low, SPEC_TYPE* src, SPEC_TYPE* specl, short* len, short max);
static SPEC_TYPE*	LastElemAnyKindFor(SPEC_TYPE* elem, UByte mark);
static SPEC_TYPE*	FirstElemAnyKindFor(SPEC_TYPE* elem, UByte mark);


// ROM 0x002ba52c Extr__FP8low_typesN52
// Every stroke's extrema: each stroke not already in the index gets its
// 0x10, then the extrema in the directions flags asks for (2: up and
// down, found with a step of `step` over 1, 2, ... up to depth+1 until
// one is found, but never under 2; 1: left and right with eps1; 4: the
// diagonal with eps2; 4 or 8: the other diagonal with eps3), then its
// 0x20.  ==> 0, 1 for no room (the element count put back, f5c the
// stroke it failed on).
long
Extr(low_type* low, short step, short eps1, short eps2, short eps3, short depth, short flags)
{
	POINTS_GROUP* groups = low->fGroups;
	long nGroups = low->fLenGroups;
	short* index = low->fIndex;
	long lenIndex = low->fLenIndex;
	SPEC_TYPE* specl = low->fSpecl;
	short savedLen = low->fLenSpecl;
	long k = 0;
	if (nGroups >= 1)
	{
		long start = 0;
		for ( ; k < nGroups; k++)
		{
			short iBeg = groups[k].iBeg;
			short iEnd = groups[k].iEnd;
			long j;
			for (j = start; j < lenIndex; j++)
			{
				SPEC_TYPE* elem = &specl[index[j]];
				if (elem->iBeg == iBeg && elem->iEnd == iEnd)
					break;
			}
			if (j < lenIndex)
			{
				// done already
				start = j;
				continue;
			}
			if (Mark(low, 0x10, 0, 0, 0, iBeg, iBeg, iBeg, iBeg) == 1)
				goto fail;
			if ((flags & 2) != 0)
			{
				long m = 0;
				long len = low->fLenSpecl;
				while (len == low->fLenSpecl && m <= depth)
				{
					long s = step / (m + 1);
					m++;
					if (s < 2)
					{
						s = 2;
						m = depth + 1;
					}
					if (BigExtr(low, iBeg, iEnd, 2, s) == 1)
						goto fail;
				}
			}
			if ((flags & 1) != 0 && BigExtr(low, iBeg, iEnd, 1, eps1) == 1)
				goto fail;
			if ((flags & 4) != 0 && BigExtr(low, iBeg, iEnd, 4, eps2) == 1)
				goto fail;
			if ((flags & 0xc) != 0 && BigExtr(low, iBeg, iEnd, 8, eps3) == 1)
				goto fail;
			if (Mark(low, 0x20, 0, 0, 0, iEnd, iEnd, iEnd, iEnd) == 1)
				goto fail;
		}
		return 0;
	}
fail:
	low->f5c = k;
	low->fLenSpecl = savedLen;
	return 1;
}


// A point's value along a direction whose weights are 0 or 1, as
// BigExtr works it out: each weight contributes the coordinate with its
// sign rather than multiplied by it (the same for the directions it is
// given).
static inline long
BigValue(short* x, short* y, long a, long b, long sum, long i)
{
	long tx = (a == 0) ? 0 : (a > 0 ? x[i] : -x[i]);
	long ty = (b == 0) ? 0 : (b > 0 ? y[i] : -y[i]);
	return (tx + ty) / sum;
}


// ROM 0x002ba858 BigExtr__FP8low_typesN32
// The extrema of the stroke from iBeg to iEnd in direction `kind`,
// recorded; then the stroke's first and last extremum stretched to its
// ends.  ==> 0, 1 for an unknown kind or no room.
static long
BigExtr(low_type* low, short iBeg, short iEnd, short kind, short eps)
{
	SPEC_TYPE* specl = low->fSpecl;
	_ENVIRONS env;
	env.iBeg = iBeg;
	env.iEnd = iEnd;
	env.eps = eps;
	SPEC_TYPE elem;
	if (kind == 8)
	{
		// the other diagonal, looked for one point at a time by DirectExtr
		// with x weighed twice when once finds nothing
		env.maxMark = 0x33;
		env.minMark = 0x31;
		env.b = -1;
		for (long i = iBeg; i <= iEnd; i++)
		{
			env.a = 1;
			env.sum = HWRAbs(env.a) + HWRAbs(env.b);
			DirectExtr(low, &env, &elem, i);
			if (elem.mark == 0)
			{
				env.a = 2;
				env.sum = HWRAbs(env.a) + HWRAbs(env.b);
				DirectExtr(low, &env, &elem, i);
			}
			if (elem.mark != 0 && i <= elem.iEnd)
			{
				if (MarkSpecl(low, &elem) == 1)
					return 1;
				if (i <= elem.iEnd)
					i = elem.iEnd;
			}
		}
	}
	else
	{
		if (kind == 2)
		{
			env.maxMark = 3;
			env.a = 0;
			env.minMark = 1;
			env.b = 1;
		}
		else if (kind == 1)
		{
			env.maxMark = 0x13;
			env.a = 1;
			env.minMark = 0x11;
			env.b = 0;
		}
		else if (kind == 4)
		{
			env.maxMark = 0x23;
			env.a = 1;
			env.minMark = 0x21;
			env.b = 1;
		}
		else
			return 1;
		short* x = low->fX;
		short* y = low->fY;
		long a = env.a, b = env.b;
		env.sum = HWRAbs(env.a) + HWRAbs(env.b);
		if (env.sum == 0)
			return 1;
		long sum = env.sum;
		InitSpeclElement(&elem);
		UByte lastMark = low->fSpecl[low->fLenSpecl - 1].mark;
		for (long i = iBeg; i <= iEnd; i++)
		{
#define V(p) BigValue(x, y, a, b, sum, (p))
			// a candidate: at least as high as both neighbours, at least
			// as low as both, or the stroke's end
			if (!(V(i) < V(i + 1)) && !(V(i) < V(i - 1)))
				;
			else if (V(i) <= V(i + 1) && V(i) <= V(i - 1))
				;
			else if (!(i == iBeg || i == iEnd))
				continue;
			long vi = V(i);
			// the run of points within eps of it
			long j = i;
			while (!(eps <= HWRAbs(vi - V(j)) || j < iBeg))
				j--;
			j++;
			long k = i;
			while (!(eps <= HWRAbs(vi - V(k)) || iEnd < k))
				k++;
			k--;
			Boolean isMax = (j != iBeg && V(j - 1) < vi && (k == iEnd || V(k + 1) < vi))
						 || (k != iEnd && V(k + 1) < vi && (V(j - 1) < vi || j == iBeg));
			if (isMax && lastMark != env.maxMark)
			{
				long vmax = vi;
				long best = i;
				for (long t = j; t <= k; t++)
				{
					if (V(t) > vmax)
					{
						vmax = V(t);
						best = t;
					}
				}
				long t = best;
				while (V(t) == vmax && t <= iEnd)
					t++;
				long mid = (best + t - 1) >> 1;
				if (mid != i)
				{
					long u = mid;
					while (!(eps <= vmax - V(u) || u < iBeg))
						u--;
					j = u + 1;
					long w = mid;
					while (!(eps <= vmax - V(w) || iEnd < w))
						w++;
					k = w - 1;
				}
				InitSpeclElement(&elem);
				elem.iBeg = j;
				elem.iEnd = k;
				elem.ipoint0 = mid;
				elem.ipoint1 = -2;
				elem.mark = env.maxMark;
				lastMark = elem.mark;
			}
			else
			{
				Boolean isMin = (j != iBeg && vi < V(j - 1) && (k == iEnd || vi < V(k + 1)))
							 || (k != iEnd && vi < V(k + 1) && (vi < V(j - 1) || j == iBeg));
				if (isMin && lastMark != env.minMark)
				{
					long vmin = vi;
					long best = i;
					for (long t = j; t <= k; t++)
					{
						if (V(t) < vmin)
						{
							best = t;
							vmin = V(t);
						}
					}
					long t = best;
					while (V(t) == vmin && t <= iEnd)
						t++;
					long mid = (best + t - 1) >> 1;
					if (mid != i)
					{
						long w = mid;
						while (V(w) - vmin < eps && w <= iEnd)
							w++;
						k = w - 1;
						long u = mid;
						while (V(u) - vmin < eps && u >= iBeg)
							u--;
						j = u + 1;
					}
					InitSpeclElement(&elem);
					elem.iBeg = j;
					elem.iEnd = k;
					elem.ipoint0 = mid;
					elem.ipoint1 = -2;
					elem.mark = env.minMark;
					lastMark = elem.mark;
				}
			}
#undef V
			if (elem.mark != 0)
			{
				if (MarkSpecl(low, &elem) == 1)
					return 1;
				i = elem.iEnd;
				InitSpeclElement(&elem);
			}
		}
	}

	// the first and the last extremum of either kind stretched to the
	// stroke's ends (whichever of the two is further in)
	SPEC_TYPE* last = &specl[low->fLenSpecl - 1];
	SPEC_TYPE* lastMax = LastElemAnyKindFor(last, env.maxMark);
	SPEC_TYPE* lastMin = LastElemAnyKindFor(last, env.minMark);
	if (lastMax != nil && lastMax->iEnd < iEnd && lastMin != nil && lastMin->iEnd < iEnd)
	{
		if (lastMin->iEnd < lastMax->iEnd)
			lastMax->iEnd = iEnd;
		else
			lastMin->iEnd = iEnd;
	}
	SPEC_TYPE* firstMax = FirstElemAnyKindFor(last, env.maxMark);
	SPEC_TYPE* firstMin = FirstElemAnyKindFor(last, env.minMark);
	if (firstMax != nil && iBeg < firstMax->iBeg && firstMin != nil && iBeg < firstMin->iBeg)
	{
		if (firstMax->iBeg < firstMin->iBeg)
			firstMax->iBeg = iBeg;
		else
			firstMin->iBeg = iBeg;
	}
	return 0;
}


// A point's value along a direction, as DirectExtr works it out: the
// weights multiplied in.
static inline long
DirectValue(short* x, short* y, _ENVIRONS* env, long i)
{
	return (env->b * y[i] + env->a * x[i]) / env->sum;
}


// ROM 0x002bb950 DirectExtr__FP8low_typeP9_ENVIRONSP9SPEC_TYPEs
// Whether point i of the stroke is an extremum along env's direction;
// elem is cleared, and filled in when it is.  ==> 0.
static long
DirectExtr(low_type* low, _ENVIRONS* env, SPEC_TYPE* elem, short i)
{
	short* y = low->fY;
	short* x = low->fX;
	long iBeg = env->iBeg;
	long iEnd = env->iEnd;
	long eps = env->eps;
	UByte lastMark = low->fSpecl[low->fLenSpecl - 1].mark;
	InitSpeclElement(elem);
#define V(p) DirectValue(x, y, env, (p))
	long v = V(i);
	long vn = V(i + 1);
	if ((v < vn || v < V(i - 1)) && (vn < v || V(i - 1) < v))
	{
		// neither as high nor as low as both neighbours
		if (i != iBeg && i != iEnd)
			return 0;
	}
	long j = i;
	while (!(eps <= HWRAbs(v - V(j)) || j < iBeg))
		j--;
	j++;
	long k = i;
	while (!(eps <= HWRAbs(v - V(k)) || iEnd < k))
		k++;
	k--;
	long mid;
	UByte mark;
	Boolean notMax = (j == iBeg || v <= V(j - 1) || (v <= V(k + 1) && k != iEnd))
				  && (k == iEnd || v <= V(k + 1) || (v <= V(j - 1) && j != iBeg));
	if (notMax || lastMark == env->maxMark)
	{
		if (j == iBeg || V(j - 1) <= v || (V(k + 1) <= v && k != iEnd))
		{
			if (k == iEnd)
				return 0;
			if (V(k + 1) <= v)
				return 0;
			if (V(j - 1) <= v && j != iBeg)
				return 0;
		}
		if (lastMark == env->minMark)
			return 0;
		long best = i;
		long vmin = v;
		for (long t = j; t <= k; t++)
		{
			long vt = V(t);
			if (vt < vmin)
			{
				best = t;
				vmin = vt;
			}
		}
		long t = best;
		while (V(t) == vmin && t <= iEnd)
			t++;
		mid = (best + t - 1) >> 1;
		if (mid != i)
		{
			long w = mid;
			while (V(w) - vmin < eps && w <= iEnd)
				w++;
			k = w - 1;
			long u = mid;
			while (V(u) - vmin < eps && iBeg <= u)
				u--;
			j = u + 1;
		}
		mark = env->minMark;
	}
	else
	{
		long best = i;
		long vmax = v;
		for (long t = j; t <= k; t++)
		{
			long vt = V(t);
			if (vmax < vt)
			{
				best = t;
				vmax = vt;
			}
		}
		long t = best;
		while (V(t) == vmax && t <= iEnd)
			t++;
		mid = (best + t - 1) >> 1;
		if (mid != i)
		{
			long u = mid;
			while (vmax - V(u) < eps && iBeg <= u)
				u--;
			j = u + 1;
			long w = mid;
			while (vmax - V(w) < eps && w <= iEnd)
				w++;
			k = w - 1;
		}
		mark = env->maxMark;
	}
#undef V
	elem->iBeg = j;
	elem->iEnd = k;
	elem->ipoint0 = mid;
	elem->ipoint1 = -2;
	elem->mark = mark;
	return 0;
}


// ROM 0x002bc1e8 Mark__FP8low_typeUcN32sN36
// An element made of the fields given and added to the list (and, for
// kinds 5, 7 and 8, to the index).  ==> 0, 1 for no room.
static long
Mark(low_type* low, UByte mark, UByte code, UByte attr, UByte other, short iBeg, short iEnd, short ipoint0, short ipoint1)
{
	SPEC_TYPE local;
	local.mark = mark;
	local.code = code;
	local.attr = attr;
	local.other = other;
	local.iBeg = iBeg;
	local.iEnd = iEnd;
	local.ipoint0 = ipoint0;
	local.ipoint1 = ipoint1;
	return MarkSpecl(low, &local);
}


// ROM 0x002bc36c MarkSpecl__FP8low_typeP9SPEC_TYPE
// A copy of elem added to the end of the list (and to the index for kinds
// 5, 7 and 8).  ==> 0, 1 for no room.
long
MarkSpecl(low_type* low, SPEC_TYPE* elem)
{
	short len = low->fLenSpecl;
	SPEC_TYPE* specl = low->fSpecl;
	SPEC_TYPE* added = &specl[len];
	if (NoteSpecl(low, elem, specl, &low->fLenSpecl, kLowSpeclSize) != 0)
	{
		added->prev = &specl[low->fLastSpecl];
		added->next = nil;
		specl[low->fLastSpecl].next = added;
		low->fLastSpecl = len;
		UByte mark = elem->mark;
		if (mark != 5 && mark != 7 && mark != 8)
			return 0;
		if (low->fLenIndex < low->fSizeIndex - 1)
		{
			low->fIndex[low->fLenIndex] = len;
			low->fLenIndex = low->fLenIndex + 1;
			return 0;
		}
	}
	return 1;
}


// ROM 0x002bc460 NoteSpecl__FP8low_typeP9SPEC_TYPET2Pss
// src's fields copied into specl[*len] (the links left alone), and *len
// counted; kinds 5, 7 and 8 have their points taken through the map in
// buffer 2.  ==> 1, 0 when the list is full.
static long
NoteSpecl(low_type* low, SPEC_TYPE* src, SPEC_TYPE* specl, short* len, short max)
{
	short* map = low->fBuffers[2].ptr;
	long n = *len;
	SPEC_TYPE* dst = &specl[n];
	UByte mark = src->mark;
	short p0 = src->ipoint0;
	short p1 = src->ipoint1;
	if (max - 1 <= n)
		return 0;
	dst->mark = mark;
	dst->code = src->code;
	dst->attr = src->attr;
	dst->other = src->other;
	if (mark == 5 || mark == 8 || mark == 7)
	{
		dst->iBeg = map[src->iBeg];
		dst->iEnd = map[src->iEnd];
		dst->ipoint0 = (p0 == -2) ? -2 : map[p0];
		dst->ipoint1 = (p1 == -2) ? -2 : map[p1];
	}
	else
	{
		dst->iBeg = src->iBeg;
		dst->iEnd = src->iEnd;
		dst->ipoint0 = p0;
		dst->ipoint1 = p1;
	}
	*len = *len + 1;
	return 1;
}


// ROM 0x002bc180 LastElemAnyKindFor__FP9SPEC_TYPEUc
// Back from elem (itself included) to the nearest element of the kind,
// within the stroke.  ==> nil for none before the stroke's 0x10.
static SPEC_TYPE*
LastElemAnyKindFor(SPEC_TYPE* elem, UByte mark)
{
	if (elem == nil)
		return nil;
	for ( ; ; )
	{
		UByte m = elem->mark;
		if (m == 0x10)
			return nil;
		if (m == mark)
			return elem;
		elem = elem->prev;
		if (elem == nil)
			return nil;
	}
}


// ROM 0x002bc1b0 FirstElemAnyKindFor__FP9SPEC_TYPEUc
// The first element of the kind in the stroke elem is in (walking back
// from elem to the stroke's 0x10).  ==> nil for none.
static SPEC_TYPE*
FirstElemAnyKindFor(SPEC_TYPE* elem, UByte mark)
{
	SPEC_TYPE* found = nil;
	if (elem->mark == 0x10)
		return nil;
	do
	{
		if (elem->mark == mark)
			found = elem;
		elem = elem->prev;
	} while (elem->mark != 0x10);
	return found;
}
