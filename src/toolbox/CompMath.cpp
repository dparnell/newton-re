/*
	File:		toolbox/CompMath.cpp

	Contains:	64-bit integer arithmetic on Int64 {hi, lo} (declared in the
				DDK's CompMath.h).  The kernel keeps time in these.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
	CompFixMul/CompSquareRoot follow when something needs them.

	The value of an Int64 is hi * 2^32 + lo.  On the MessagePad hi and lo
	are 32-bit words; a host build makes ULong pointer-sized, so the word
	arithmetic of the ROM is done in 32 bits explicitly (Word) and every
	result is stored normalised, lo below 2^32 - the words then read as
	they do on the MessagePad.
*/

#include "Newton.h"
#include "CompMath.h"

#include <stdint.h>

typedef uint32_t	Word;

static inline int64_t
Value(const Int64* x)
{
	return (int64_t) (((uint64_t) (uint32_t) x->hi << 32) + (uint64_t) x->lo);
}

static inline void
Store(Int64* x, int64_t value)
{
	x->hi = (SLong) (int32_t) (value >> 32);
	x->lo = (ULong) (uint32_t) value;
}


// ROM 0x0007148c CompAdd
void
CompAdd(const Int64* src, Int64* dst)
{
	Word lo = (Word) dst->lo + (Word) src->lo;
	dst->lo = lo;
	dst->hi = (SLong) (int32_t) ((Word) dst->hi + (Word) src->hi);
	if (lo < (Word) src->lo)			// carry
		dst->hi = (SLong) (int32_t) ((Word) dst->hi + 1);
}


// ROM 0x000714c0 CompSub
void
CompSub(const Int64* src, Int64* dst)
{
	dst->hi = (SLong) (int32_t) ((Word) dst->hi - (Word) src->hi);
	if ((Word) dst->lo < (Word) src->lo)		// borrow
		dst->hi = (SLong) (int32_t) ((Word) dst->hi - 1);
	dst->lo = (Word) dst->lo - (Word) src->lo;
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


// ROM 0x0038b0ec CompMul
// The 64-bit product of two signed words (the ROM multiplies the halves).
void
CompMul(long src1, long src2, Int64* dst)
{
	Store(dst, (int64_t) (int32_t) src1 * (int64_t) (int32_t) src2);
}


// ROM 0x000714f4 CompDiv
// numerator / denominator as a signed word.  Without a remainder pointer the
// quotient is rounded to nearest (half up); with one, truncated, and the
// remainder takes the numerator's sign.  A quotient that does not fit in a
// word saturates (to 0x7fffffff or -0x80000000, the remainder reported as
// -0x80000000).  (The ROM divides bit-serially; the result is the same.)
long
CompDiv(const Int64* numerator, long denominator, long* remainder)
{
	int64_t n = Value(numerator);
	Boolean negativeNumerator = n < 0;
	Boolean negativeResult = negativeNumerator;
	uint64_t un = negativeNumerator ? (uint64_t) 0 - (uint64_t) n : (uint64_t) n;
	uint32_t d = (uint32_t) (int32_t) denominator;
	if ((int32_t) denominator < 0)
	{
		negativeResult = !negativeResult;
		d = (uint32_t) 0 - d;
	}
	if (remainder == nil)
		un += d >> 1;
	if (un >= ((uint64_t) d << 31))
	{
		if (remainder != nil)
			*remainder = (long) (int32_t) 0x80000000;
		return negativeResult ? (long) (int32_t) 0x80000000 : (long) 0x7fffffff;
	}
	uint32_t quotient = (uint32_t) (un / d);
	uint32_t rest = (uint32_t) (un % d);
	if (remainder != nil)
		*remainder = (long) (int32_t) (negativeNumerator ? (uint32_t) 0 - rest : rest);
	return (long) (int32_t) (negativeResult ? (uint32_t) 0 - quotient : quotient);
}


// ROM 0x00071650 CompShift
// A positive shift is to the right, rounding half up on the last bit shifted
// out; a negative one to the left.
void
CompShift(Int64* srcdst, long shift)
{
	int64_t v = Value(srcdst);
	if (shift <= 0)
	{
		if (shift < 0)
			Store(srcdst, shift <= -64 ? 0 : (int64_t) ((uint64_t) v << (-shift)));
		return;
	}
	int64_t result;
	Boolean roundUp;
	if (shift >= 64)
	{
		result = v < 0 ? -1 : 0;
		roundUp = shift == 64 && v < 0;
	}
	else
	{
		result = v >> shift;
		roundUp = ((uint64_t) v >> (shift - 1)) & 1;
	}
	if (roundUp)
		result++;
	Store(srcdst, result);
}
