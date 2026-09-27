/*
	File:		LowLevel.cpp

	Contains:	The cursive reader's low level: its state, its memory,
				the strokes and the trace.  See LowLevel.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.
*/

#include "LowLevel.h"
#include "XrDomains.h"
#include "ParaGraph.h"
#include <string.h>


/*------------------------------------------------------------------------------
	T h e   l o w   l e v e l ' s   s t a t e
------------------------------------------------------------------------------*/

// ROM 0x0034e99c PrepareLowData__FP8low_typeP13PS_point_typeP7rc_typePPs
// The state cleared and its memory allocated for the trace rc says it
// has.  ==> 1, 0 for a failure (with what was allocated let go - except
// for a trace too long for the buffers, where the special elements are
// left for the caller, which lets them go in any case).
long
PrepareLowData(low_type* low, PS_point_type* /*trace*/, rc_type* rc, short** block)
{
	memset(low, 0, sizeof(low_type));		// DEVIATION: sizeof (the ROM clears its 0x9c bytes)
	low->rc = rc;
	if (AllocSpecl(&low->fSpecl, kLowSpeclSize))
	{
		low->fNMaxSpecl = kLowSpeclSize;
		short maxPoints = MaxPointsGrown(RCGetH(rc, 0x96));
		if (maxPoints > kLowBufferSize)
			return 0;
		low->fMaxPoints = maxPoints;
		low->fMaxGroups = kLowMaxGroups;
		low->fSizeIndex = kLowIndexSize;
		low->f5c = 0x7fff;
		if (LowAlloc(block, 4, kLowBufferSize, low) == 0)
			return 1;
	}
	low_dealloc(block);
	DeallocSpecl(&low->fSpecl);
	return 0;
}


// ROM 0x0034e8f8 FillLowDataTrace__FP8low_typeP13PS_point_type
// The trace copied into the x and y arrays, ended with a pen-up if it
// was not.
void
FillLowDataTrace(low_type* low, PS_point_type* trace)
{
	short n = RCGetH(low->rc, 0x96);
	low->fII = n;
	trace_to_xy(low->fX, low->fY, n, trace);
	if (n > 1 && low->fY[n - 1] != -1)
		low->fY[n - 1] = -1;
	low->fTrace = trace;
}


// ROM 0x0034e968 GetLowDataRect__FP8low_type
void
GetLowDataRect(low_type* low)
{
	GetTraceBox(low->fX, low->fY, 0, low->fII - 1, &low->fBox);
}


// ROM 0x00305a28 LowAlloc__FPPssT2P8low_type
// One block for the initial x and y arrays, the strokes, the index and
// the working buffers, carved up in that order.  Everything in it is
// shorts, so its size is the ROM's.  ==> 0, 1 for no room.
long
LowAlloc(short** block, short nBuffers, short bufferSize, low_type* low)
{
	short* p = (short*) HWRMemoryAlloc(((long) nBuffers * bufferSize + low->fMaxPoints * 2 + low->fSizeIndex) * 2 + low->fMaxGroups * 0xc);
	*block = p;
	if (p == nil)
		return 1;
	low->fXInitial = p;
	low->fYInitial = p + low->fMaxPoints;
	low->fGroups = (POINTS_GROUP*) (low->fYInitial + low->fMaxPoints);
	low->fIndex = low->fYInitial + low->fMaxPoints + low->fMaxGroups * 6;
	for (long i = 0; i < nBuffers; i++)
	{
		low->fBuffers[i].ptr = low->fIndex + low->fSizeIndex + bufferSize * i;
		low->fBuffers[i].size = bufferSize;
	}
	return 0;
}


// ROM 0x00306f74 low_dealloc__FPPs
void
low_dealloc(short** block)
{
	if (*block != nil)
		HWRMemoryFree((Ptr) *block);
	*block = nil;
}


// ROM 0x00305a14 SetXYToInitial__FP8low_type
void
SetXYToInitial(low_type* low)
{
	low->fX = low->fXInitial;
	low->fY = low->fYInitial;
}


// ROM 0x00307f50 MaxPointsGrown__Fs
short
MaxPointsGrown(short n)
{
	return n + 1;
}


/*------------------------------------------------------------------------------
	T h e   s p e c i a l   e l e m e n t s
------------------------------------------------------------------------------*/

// ROM 0x00306410 AllocSpecl__FPP9SPEC_TYPEs
Boolean
AllocSpecl(SPEC_TYPE** specl, short n)
{
	*specl = (SPEC_TYPE*) HWRMemoryAlloc(n * sizeof(SPEC_TYPE));	// DEVIATION: sizeof (the ROM's 0x14)
	return *specl != nil;
}


// ROM 0x00307984 DeallocSpecl__FPP9SPEC_TYPE
void
DeallocSpecl(SPEC_TYPE** specl)
{
	if (*specl == nil)
		return;
	HWRMemoryFree((Ptr) *specl);
	*specl = nil;
}


// ROM 0x002ba430 InitSpecl__FP8low_types
// The elements and the index cleared; element 0 is the list's head, with
// the one after it next.
long
InitSpecl(low_type* low, short n)
{
	SPEC_TYPE* specl = low->fSpecl;
	memset(specl, 0, n * sizeof(SPEC_TYPE));			// DEVIATION: sizeof
	memset(low->fIndex, 0, low->fSizeIndex << 1);
	low->fLenSpecl = 1;
	low->fLenIndex = 0;
	specl[0].prev = nil;
	specl[0].next = &specl[1];
	specl[0].mark = 0;
	specl[0].ipoint0 = -2;
	specl[0].ipoint1 = -2;
	low->fLastSpecl = 0;
	return 0;
}


// ROM 0x002ba4cc InitSpeclElement__FP9SPEC_TYPE
Boolean
InitSpeclElement(SPEC_TYPE* elem)
{
	if (elem != nil)
	{
		memset(elem, 0, sizeof(SPEC_TYPE));			// DEVIATION: sizeof
		elem->prev = nil;
		elem->next = nil;
		elem->mark = 0;
		elem->ipoint0 = -2;
		elem->ipoint1 = -2;
	}
	return elem == nil;
}


/*------------------------------------------------------------------------------
	T h e   s t r o k e s
------------------------------------------------------------------------------*/

// ROM 0x003087e0 InitGroupsBorder__FP8low_types
// A group per stroke - the points between two pen-ups - with its box when
// asked.  ==> 0, 1 for a trace that does not start and end with a pen-up
// or that has more strokes than there is room for.
long
InitGroupsBorder(low_type* low, short withBoxes)
{
	short* x = low->fX;
	short* y = low->fY;
	POINTS_GROUP* groups = low->fGroups;
	short maxGroups = low->fMaxGroups;
	long ii = low->fII;
	ClearGroupsBorder(low);
	if (y[0] == -1)
	{
		groups[0].iBeg = 1;
		long k = 1;
		for (long i = 1; i < ii - 1; i++)
		{
			if (y[i] == -1)
			{
				groups[k - 1].iEnd = i - 1;
				// ROM BUG: the next stroke's start is written before the room
				// is checked, so with every group used it lands one past the
				// array - on the index that follows it in the block
				groups[k].iBeg = i + 1;
				if (withBoxes == 1)
					GetTraceBox(x, y, groups[k - 1].iBeg, groups[k - 1].iEnd, &groups[k - 1].box);
				if (maxGroups <= k)
					return 1;
				k++;
			}
		}
		groups[k - 1].iEnd = ii - 2;
		if (withBoxes == 1)
			GetTraceBox(x, y, groups[k - 1].iBeg, groups[k - 1].iEnd, &groups[k - 1].box);
		if (y[ii - 1] == -1)
		{
			low->fLenGroups = k;
			return 0;
		}
	}
	return 1;
}


// ROM 0x0030897c ClearGroupsBorder__FP8low_type
// ==> whether the groups had been full.
Boolean
ClearGroupsBorder(low_type* low)
{
	short len = low->fLenGroups;
	short max = low->fMaxGroups;
	memset(low->fGroups, 0, max * 0xc);
	low->fLenGroups = 0;
	return max <= len;
}


// ROM 0x003089cc GetGroupNumber__FP8low_typei
// The stroke point i is in.  ==> its number, -2 for a pen-up.
long
GetGroupNumber(low_type* low, long i)
{
	POINTS_GROUP* groups = low->fGroups;
	long n = low->fLenGroups;
	long k = 0;
	// ROM BUG: when no stroke holds the point the answer is left as the
	// low_type's own address (the register it was passed in), which the
	// callers never see because every point that is not a pen-up is in a
	// stroke
	long found = (long) (ULong) low;
	for ( ; k < n; k++)
	{
		if (groups[k].iBeg <= i && i <= groups[k].iEnd)
		{
			found = k;
			break;
		}
	}
	if ((n - 1 != k || i <= groups[n - 1].iEnd) && low->fY[i] != -1)
		return found;
	return -2;
}


// ROM 0x00308a58 IsPointCont__FP8low_typeiUc
// Where point i lies against the special elements of the kind given.
// ==> 5 inside one, 3 at its start, 4 at its end, -2 in none (or a
// pen-up, or out of the trace).
long
IsPointCont(low_type* low, long i, UByte mark)
{
	if (i < 0 || low->fII <= i || low->fY[i] == -1)
		return -2;
	for (long k = 0; k < low->fLenSpecl; k++)
	{
		SPEC_TYPE* elem = &low->fSpecl[k];
		if (elem->mark == mark)
		{
			if (elem->iBeg < i && i < elem->iEnd)
				return 5;
			if (elem->iBeg == i)
				return 3;
			if (i == elem->iEnd)
				return 4;
		}
	}
	return -2;
}


/*------------------------------------------------------------------------------
	T h e   t r a c e
------------------------------------------------------------------------------*/

// ROM 0x00306074 trace_to_xy__FPsT1iP13PS_point_type
void
trace_to_xy(short* x, short* y, long n, PS_point_type* trace)
{
	for (long i = 0; i < n; i++)
	{
		x[i] = trace[i].x;
		y[i] = trace[i].y;
	}
}


// ROM 0x00307510 GetBoxFromTrace__FP13PS_point_typeiT2P5_RECT
// The box of the points from iBeg to iEnd that are not pen-ups.  ==> 1.
long
GetBoxFromTrace(PS_point_type* trace, long iBeg, long iEnd, _RECT* box)
{
	long top = 0x7fff, left = 0x7fff, bottom = 0, right = 0;
	for ( ; iBeg <= iEnd; iBeg++)
	{
		long y = trace[iBeg].y;
		if (y != -1)
		{
			long x = trace[iBeg].x;
			if (right < x)
				right = x;
			if (x < left)
				left = x;
			if (bottom < y)
				bottom = y;
			if (y < top)
				top = y;
		}
	}
	box->left = left;
	box->right = right;
	box->top = top;
	box->bottom = bottom;
	return 1;
}


// ROM 0x003075b4 GetTraceBox__FPsT1iT3P5_RECT
void
GetTraceBox(short* x, short* y, long iBeg, long iEnd, _RECT* box)
{
	xMinMax(iBeg, iEnd, x, y, &box->left, &box->right);
	yMinMax(iBeg, iEnd, y, &box->top, &box->bottom);
}


// ROM 0x00307358 xMinMax__FiT1PsN33
long
xMinMax(long iBeg, long iEnd, short* x, short* y, short* xMin, short* xMax)
{
	long lo = 0x7fff, hi = 0;
	for ( ; iBeg <= iEnd; iBeg++)
	{
		if (y[iBeg] != -1)
		{
			long v = x[iBeg];
			if (hi < v)
				hi = v;
			if (v < lo)
				lo = v;
		}
	}
	*xMax = hi;
	*xMin = lo;
	return 1;
}


// ROM 0x003073cc yMinMax__FiT1PsN23
long
yMinMax(long iBeg, long iEnd, short* y, short* yMin, short* yMax)
{
	long lo = 0x7fff, hi = 0;
	for ( ; iBeg <= iEnd; iBeg++)
	{
		long v = y[iBeg];
		if (v != -1)
		{
			if (hi < v)
				hi = v;
			if (v < lo)
				lo = v;
		}
	}
	*yMax = hi;
	*yMin = lo;
	return 1;
}


// ROM 0x00306448 iMostFarFromChord__FPsT1iT3
// The point between i and j (not a pen-up) furthest from the chord i-j,
// by the cross product; along a run of points equally far the answer
// moves on by one for every two of them, so it ends near the run's
// middle.  ==> i when none is off the chord.
long
iMostFarFromChord(short* x, short* y, long i, long j)
{
	long dx = x[j] - x[i];
	long dy = y[j] - y[i];
	Boolean inRun = true;
	Boolean half = false;
	long best = 0;
	long found = i;
	for (long k = i + 1; k <= j; k++)
	{
		if (y[k] != -1)
		{
			long d = (dx * y[k] - dy * x[k]) + (dy * x[i] - dx * y[i]);
			if (d < 0)
				d = -d;
			if (best < d)
			{
				half = false;
				inRun = true;
				best = d;
				found = k;
				continue;
			}
			if (inRun && d == best)
			{
				if (half)
				{
					found++;
					half = false;
				}
				else
					half = true;
				continue;
			}
		}
		inRun = false;
	}
	return found;
}


// ROM 0x00307cd0 NewIndex__FPsT1sN23
// Where an old point went after a filter: index[j] is the old point the
// new point j came from.  mode 0: the first new point from it or after it
// (the one before when that is a pen-up); 2: the last new point from it
// or before it; 1: half way between those two.  ==> -2 for none.
short
NewIndex(short* index, short* y, short i, short n, short mode)
{
	long first = n, last = i;		// (the ROM's registers keep these when a mode skips its search)
	long result = -2;
	if ((UShort) mode < 2)
	{
		first = 0;
		while (first < n && index[first] < i)
			first++;
		if (first < n)
		{
			result = first;
			if (y[first] == -1)
				result = first - 1;
		}
	}
	if (mode == 2 || mode == 1)
	{
		last = 0;
		while (last < n && !(i < index[last]))
			last++;
		// (for an empty trace this reads the short before the index)
		if (last < n || index[last - 1] == i)
		{
			last--;
			result = last;
		}
	}
	if (mode == 1 && result != -2)
		result = (first + last) >> 1;
	return result;
}
