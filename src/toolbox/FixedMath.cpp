/*
	File:		toolbox/FixedMath.cpp

	Contains:	16.16 fixed-point multiply and divide (FixedMath.h): the
				ARM assembly routines of the ROM's fplib, which work on
				magnitudes, round to nearest and saturate on overflow.

	Reconstructed from the MP2100 D ROM (0x0038af20-0x0038b060); each
	function cites its origin.  NOT YET RECONSTRUCTED: FractMultiply
	(the same with a 2.30 shift), FixedMultiplyDivide, FractSquareRoot,
	FractSineCosine, FixedAtan2.
*/

#include "FixedMath.h"
#include <stdint.h>

const Fixed kFixedMax = 0x7fffffff;
const Fixed kFixedMin = (Fixed) 0x80000000;


// ROM 0x0038b008 FixedMultiply
// The product of the magnitudes, shifted down by sixteen with rounding
// (half up), the sign restored; saturated when it does not fit.
extern "C" Fixed
FixedMultiply(Fixed a, Fixed b)
{
	Boolean negative = (a < 0) != (b < 0);
	uint64_t ma = (a < 0) ? (uint64_t) -(int64_t) a : (uint64_t) a;
	uint64_t mb = (b < 0) ? (uint64_t) -(int64_t) b : (uint64_t) b;
	uint64_t product = (ma * mb + 0x8000) >> 16;
	if (product > 0x7fffffff)
		return negative ? kFixedMin : kFixedMax;
	return negative ? -(Fixed) product : (Fixed) product;
}


// ROM 0x0038af20 FixedDivide
// a / b in 16.16: the quotient of the magnitudes to seventeen extra bits,
// rounded half up, the sign restored; the largest value of the sign when
// b is 0 or the quotient does not fit.  a == b, a == -b, a == 0 and
// b == 1.0 are answered directly.
extern "C" Fixed
FixedDivide(Fixed a, Fixed b)
{
	Boolean negative = (a < 0) != (b < 0);
	if (b == 0)
		return negative ? kFixedMin : kFixedMax;
	if (a == b)
		return 0x10000;
	if (a == -b)
		return (Fixed) 0xffff0000;
	if (a == 0)
		return 0;
	if (b == 0x10000)
		return a;
	uint64_t ma = (a < 0) ? (uint64_t) -(int64_t) a : (uint64_t) a;
	uint64_t mb = (b < 0) ? (uint64_t) -(int64_t) b : (uint64_t) b;
	uint64_t quotient = (ma << 17) / mb;
	quotient = (quotient + 1) >> 1;
	if (quotient > 0x7fffffff)
		return negative ? kFixedMin : kFixedMax;
	return negative ? -(Fixed) quotient : (Fixed) quotient;
}
