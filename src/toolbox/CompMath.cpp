/*
	File:		toolbox/CompMath.cpp

	Contains:	64-bit integer arithmetic on Int64 {hi, lo} (declared in the
				DDK's CompMath.h).  The kernel keeps time in these.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
	CompNeg/CompShift/CompMul/CompDiv/CompFixMul/CompSquareRoot follow when
	something needs them.
*/

#include "Newton.h"
#include "CompMath.h"


// ROM 0x0007148c CompAdd
void
CompAdd(const Int64* src, Int64* dst)
{
	ULong lo = dst->lo + src->lo;
	dst->lo = lo;
	dst->hi = dst->hi + src->hi;
	if (lo < src->lo)			// carry
		dst->hi++;
}


// ROM 0x000714c0 CompSub
void
CompSub(const Int64* src, Int64* dst)
{
	dst->hi = dst->hi - src->hi;
	if (dst->lo < src->lo)		// borrow
		dst->hi--;
	dst->lo = dst->lo - src->lo;
}


// ROM 0x00071720 CompCompare
// -1, 0, 1 as a is less than, equal to, greater than b.  (The DDK calls the
// second argument `minusb`; the ROM compares the values as given, hi signed.)
long
CompCompare(const Int64* a, const Int64* b)
{
	if (a->hi < b->hi)
		return -1;
	if (a->hi == b->hi)
	{
		if (a->lo < b->lo)
			return -1;
		if (a->lo == b->lo)
			return 0;
	}
	return 1;
}
