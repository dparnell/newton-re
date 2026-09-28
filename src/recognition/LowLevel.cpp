/*
	File:		LowLevel.cpp

	Contains:	The cursive reader's low level: its state, its memory,
				the strokes and the trace.  See LowLevel.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.
*/

#include "LowLevel.h"
#include "XrDomains.h"
#include "ParaGraph.h"
#include "CursiveReader.h"
#include <string.h>


/*------------------------------------------------------------------------------
	T h e   l o w   l e v e l
------------------------------------------------------------------------------*/

// ROM 0x0034ea74 low_level__FP13PS_point_typeP11xrdata_typeP7rc_type
// One word's trace (pen-ups at each end and between its strokes, rc +0x96
// points) cut into xrs: its state made, the base line found and the trace
// rescaled to it (BaselineAndScale), and - unless rc +0x90 bit 6 asks for
// the base line alone - the special elements found (AnalyzeLowData) and
// written out as the xrs (exchange).  The writing's slant is taken from rc
// +0xac and written back there.  ==> 0, 1 for a failure (or a trace of
// fewer than three points, for which nothing is done).
long
low_level(PS_point_type* trace, xrdata_type* xr, rc_type* rc)
{
	long result = 1;
	short* block = nil;
	low_type low;
	_SDS_CONTROL_TYPE sds;					// (the ROM keeps both on its stack)
	if ((short) RCGetH(rc, 0x96) < 3)
		return 1;
	xr->fLength = 0;
	if (PrepareLowData(&low, trace, rc, &block))
	{
		SetXYToInitial(&low);
		FillLowDataTrace(&low, trace);
		GetLowDataRect(&low);
		low.fSlope = RCGetH(low.rc, 0xac);
		if (BaselineAndScale(&low) == 0)
		{
			Boolean done = false;
			if ((RCGetH(rc, 0x90) & 0x40) != 0)
				done = true;
			else
			{
				low.fSDS = &sds;
				if (CreateSDS(&low, 200)
				 && AnalyzeLowData(&low, trace) == 0
				 && exchange(&low, xr) == 0)
					done = true;
			}
			if (done)
			{
				RCSetH(low.rc, 0xac, low.fSlope);
				result = 0;
			}
		}
	}
	DestroySDS(&low);
	low_dealloc(&block);
	DeallocSpecl(&low.fSpecl);
	return result;
}


// ROM 0x0034ed54 AnalyzeLowData__FP8low_typeP13PS_point_type
// The special elements found in the rescaled trace: the trace filtered
// again, its extrema (Extr), the strokes described and the sticks, dots
// and hatches found (Pict), refiltered at the reader's step and its
// extrema found again; the slant measured (none for the numbers field or
// when rc +0x90 bit 0 says so); the loops (Circle), the corners (angl),
// the bends at the sides (FindSideExtr) and the crossings (Cross); then
// every element given its code (lk_begin) and the passes over the
// crossings, the arcs, the i's and u's and the strokes written out of order
// (lk_cross, lk_duga, Adjust_I_U, xt_st_zz), the colons (RestoreColons)
// and the bends again (PostFindSideExtr).  ==> 0, 1 for a failure.
long
AnalyzeLowData(low_type* low, PS_point_type* trace)
{
	GetLowDataRect(low);
	Errorprov(low);
	if (PreFilt(const1[0], low) != 0 || InitGroupsBorder(low, 1) != 0)
		return 1;
	DefLineThresholds(low);
	InitSpecl(low, 400);
	Extr(low, const1[5], const1[6], const1[6], const1[5] >> 1, 0, 7);
	OperateSpeclArray(low);
	if (Sort_specl(low->fSpecl, low->fLenSpecl) != 0
	 || InitGroupsBorder(low, 1) != 0
	 || Pict(low) != 0)
		return 1;
	Surgeon(low);
	if (Filt(low, const1[0], 1) != 0 || InitGroupsBorder(low, 1) != 0)
		return 1;
	trace_to_xy(low->fXInitial, low->fYInitial, (short) RCGetH(low->rc, 0x96), trace);
	if (Extr(low, const1[5], -2, -2, -2, 5, 2) != 0)
		return 1;
	if ((RCGetH(low->rc, 0x90) & 1) != 0 || RCGetH(low->rc, 0x92) == 2)
		low->fSlope = 0;
	else
		low->fSlope = (short) measure_slope(low);
	if (Circle(low) != 0
	 || angl(low) != 0
	 || FindSideExtr(low) == 0
	 || Cross(low) != 0
	 || Clear_specl(low->fSpecl, low->fLenSpecl) != 0
	 || lk_begin(low) != 0)
		return 1;
	lk_cross(low);
	lk_duga(low);
	Adjust_I_U(low);
	if (xt_st_zz(low) != 0 || RestoreColons(low) != 0 || PostFindSideExtr(low) == 0)
		return 1;
	return 0;
}


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


// ROM 0x0030a668 NewSPECLElem__FP8low_type
// The next element of the array, cleared and counted.  ==> nil when full.
SPEC_TYPE*
NewSPECLElem(low_type* low)
{
	if (low->fLenSpecl < low->fNMaxSpecl)
	{
		SPEC_TYPE* elem = &low->fSpecl[low->fLenSpecl];
		memset(elem, 0, sizeof(SPEC_TYPE));				// DEVIATION: sizeof
		low->fLenSpecl = low->fLenSpecl + 1;
		return elem;
	}
	return nil;
}


// ROM 0x0030a6d0 DelFromSPECLList__FP9SPEC_TYPE
// Unlinked (its own links left as they were).
void
DelFromSPECLList(SPEC_TYPE* elem)
{
	elem->prev->next = elem->next;
	if (elem->next == nil)
		return;
	elem->next->prev = elem->prev;
}


// ROM 0x0030a6f4 FindMarkRight__FP9SPEC_TYPEUc
// From elem forwards to the first of the kind.  ==> nil for none.
SPEC_TYPE*
FindMarkRight(SPEC_TYPE* elem, UByte mark)
{
	while (elem != nil && elem->mark != mark)
		elem = elem->next;
	return elem;
}


// ROM 0x0030a714 FindMarkLeft__FP9SPEC_TYPEUc
SPEC_TYPE*
FindMarkLeft(SPEC_TYPE* elem, UByte mark)
{
	while (elem != nil && elem->mark != mark)
		elem = elem->prev;
	return elem;
}


// ROM 0x0030a734 DelThisAndNextFromSPECLList__FP9SPEC_TYPE
// elem and the one after it unlinked, still linked to each other.
void
DelThisAndNextFromSPECLList(SPEC_TYPE* elem)
{
	SPEC_TYPE* next = elem->next;
	DelFromSPECLList(next);
	DelFromSPECLList(elem);
	elem->next = next;
}


// ROM 0x0030a760 DelCrossingFromSPECLList__FP9SPEC_TYPE
// (a crossing is two elements; the ROM's entry is a branch to the above)
void
DelCrossingFromSPECLList(SPEC_TYPE* elem)
{
	DelThisAndNextFromSPECLList(elem);
}


// ROM 0x0030a764 SwapThisAndNext__FP9SPEC_TYPE
void
SwapThisAndNext(SPEC_TYPE* elem)
{
	SPEC_TYPE* next = elem->next;
	if (next == nil)
		return;
	DelFromSPECLList(elem);
	SPEC_TYPE* after = next->next;
	next->next = elem;
	if (elem != nil)
		elem->prev = next;
	elem->next = after;
	if (after != nil)
		after->prev = elem;
}


// ROM 0x0030a798 Insert2ndAfter1st__FP9SPEC_TYPET1
void
Insert2ndAfter1st(SPEC_TYPE* first, SPEC_TYPE* second)
{
	SPEC_TYPE* after = first->next;
	first->next = second;
	if (second != nil)
		second->prev = first;
	second->next = after;			// (written whether or not second is nil)
	if (after != nil)
		after->prev = second;
}


// ROM 0x0030a7b8 InsertCrossing2ndAfter1st__FP9SPEC_TYPET1
// A crossing (second and the element after it) put after first.
void
InsertCrossing2ndAfter1st(SPEC_TYPE* first, SPEC_TYPE* second)
{
	Insert2ndAfter1st(first, second->next);
	Insert2ndAfter1st(first, second);
}


// ROM 0x0030a7e4 Move2ndAfter1st__FP9SPEC_TYPET1
void
Move2ndAfter1st(SPEC_TYPE* first, SPEC_TYPE* second)
{
	DelFromSPECLList(second);
	Insert2ndAfter1st(first, second);
}


// ROM 0x0030a810 MoveCrossing2ndAfter1st__FP9SPEC_TYPET1
void
MoveCrossing2ndAfter1st(SPEC_TYPE* first, SPEC_TYPE* second)
{
	DelCrossingFromSPECLList(second);
	Insert2ndAfter1st(first, second->next);
	Insert2ndAfter1st(first, second);
}


// ROM 0x0030a83c RefreshElem__FP9SPEC_TYPEUcN22
void
RefreshElem(SPEC_TYPE* elem, UByte mark, UByte code, UByte attr)
{
	elem->mark = mark;
	elem->code = code;
	elem->attr = attr;
}


// ROM 0x00305b84 IsUpperElem__FP9SPEC_TYPE
// Whether an element's code is one of the upper kinds.
Boolean
IsUpperElem(SPEC_TYPE* elem)
{
	UByte c = elem->code;
	return c == 3 || c == 2 || c == 9 || c == 0xa || c == 4 || c == 0x1d || c == 0x21
		|| c == 0x17 || c == 0x16 || c == 0x18 || c == 0x15;
}


// ROM 0x00305bc0 IsLowerElem__FP9SPEC_TYPE
Boolean
IsLowerElem(SPEC_TYPE* elem)
{
	UByte c = elem->code;
	return c == 7 || c == 8 || c == 0xb || c == 0xc || c == 6 || c == 0x1e || c == 0x22
		|| c == 0x1b || c == 0x1a || c == 0x1c || c == 0x19;
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


// ROM 0x00306f24 iMidPointPlato__FiT1PsT3
// The middle of the run of points from i on with the same value in a
// (not past a pen-up), no further than iEnd.
long
iMidPointPlato(long i, long iEnd, short* a, short* y)
{
	long v = a[i];
	long k = i;
	while (a[k] == v && y[k] != -1)
		k++;
	long r = (i + k - 1) >> 1;
	if (r > iEnd)
		r = iEnd;
	return r;
}


// ROM 0x00306f9c ixMin__FiT1PsT3
// The point from iBeg to iEnd (not a pen-up) with the least x - the
// middle of its run of equals.  ==> -1 for none.
long
ixMin(long iBeg, long iEnd, short* x, short* y)
{
	long best = -1;
	Boolean found = false;
	for (long i = iBeg; i <= iEnd; i++)
	{
		if (y[i] != -1 && (!found || x[i] < x[best]))
		{
			best = i;
			found = true;
		}
	}
	if (found)
		return iMidPointPlato(best, iEnd, x, y);
	return -1;
}


// ROM 0x0030700c ixMax__FiT1PsT3
// The same with the greatest x.
long
ixMax(long iBeg, long iEnd, short* x, short* y)
{
	long best = -1;
	Boolean found = false;
	for (long i = iBeg; i <= iEnd; i++)
	{
		if (y[i] != -1 && (!found || x[i] > x[best]))
		{
			best = i;
			found = true;
		}
	}
	if (found)
		return iMidPointPlato(best, iEnd, x, y);
	return -1;
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


// ROM 0x0034eba0 BaselineAndScale__FP8low_type
// The trace's preprocessing and its base line: a doubled pen-up taken
// out, the trace resampled to a step of a sixteenth of the box's height
// (at least two), then - unless the caller gives its own line (rc +0x90
// bit 0, which makes it sure of both: +0xe6 and +0xe8 100) - the strokes
// found and their extrema up and down at one and a half times four
// fifths of that step (7 for 8), and the step recorded (rc +0xe0, the
// engine's `h`: const1's 8 when the extrema are not looked for).  The
// trace (x, y and its count, rc +0x96) is then the one first given, and
// transfrmN finds the borders and rescales it.  ==> 0, 1 for a failure.
long
BaselineAndScale(low_type* low)
{
	rc_type* rc = low->rc;
	RCSetH(rc, 0x94, 0);
	long step = (short) (((low->fBox.bottom - low->fBox.top) * 10) / 0xa0);
	if (step < 2)
		step = 2;
	Errorprov(low);
	if (Filt(low, (short) ((const1[0] * step) / 10), 0) != 0)
		return 1;
	RCSetH(rc, 0xe0, (UShort) const1[5]);
	if ((RCGetH(rc, 0x90) & 1) != 0)
	{
		RCSetH(rc, 0xe6, 100);
		RCSetH(rc, 0xe8, 100);
	}
	if ((RCGetH(rc, 0x90) & 1) == 0)
	{
		long h = const1[5];
		long s = (short) ((step * h) / 10);
		step = (short) (s + (s >> 1));
		if (step < 2)
			step = 2;
		if (h == step)
			step = (short) (h - 1);
		if (InitGroupsBorder(low, 0) != 0)
			return 1;
		InitSpecl(low, kLowSpeclSize);
		if (Extr(low, (short) step, -2, -2, -2, 0, 2) != 0)
			return 1;
		RCSetH(rc, 0xe0, (UShort) step);
	}
	SetXYToInitial(low);
	low->fII = (short) RCGetH(rc, 0x96);
	if (transfrmN(low) != 0)
		return 1;
	return 0;
}
