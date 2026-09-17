/*
	File:		toolbox/FixedMath.cpp

	Contains:	16.16 fixed-point multiply and divide (FixedMath.h): the
				ARM assembly routines of the ROM's fplib, which work on
				magnitudes, round to nearest and saturate on overflow.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
	NOT YET RECONSTRUCTED: FractSineCosine (a CORDIC over a RAM-area table),
	FractSin/FractCos (xFracSin/xFracCos), FixedASin/FixedACos (the same
	arctangent polynomial as FixedAtan2, but undeclared in the DDK), and
	FixMul32 (0x000b310c, a different fixed format).
*/

#include "FixedMath.h"
#include "CompMath.h"
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


const Fract kFractMax = 0x7fffffff;
const Fract kFractMin = (Fract) 0x80000000;


// ROM 0x0038affc FractMultiply
// The product of the magnitudes in 2.30, shifted down by thirty with
// rounding (half up), the sign restored; saturated when it does not fit.
// The same routine as FixedMultiply with a 2.30 rather than a 16.16 shift.
extern "C" Fixed
FractMultiply(Fixed a, Fixed b)
{
	Boolean negative = (a < 0) != (b < 0);
	uint64_t ma = (a < 0) ? (uint64_t) -(int64_t) a : (uint64_t) a;
	uint64_t mb = (b < 0) ? (uint64_t) -(int64_t) b : (uint64_t) b;
	uint64_t product = (ma * mb + 0x20000000) >> 30;
	if (product > 0x7fffffff)
		return negative ? kFractMin : kFractMax;
	return negative ? -(Fract) product : (Fract) product;
}


// ROM 0x0038af14 FractDivide
// a / b in 2.30: the quotient of the magnitudes to thirty-one bits,
// rounded half up, the sign restored; the largest value of the sign when
// b is 0 or the quotient does not fit.  Unlike FixedDivide it has no
// a == b / a == -b / b == 1 shortcuts.
extern "C" Fixed
FractDivide(Fixed a, Fixed b)
{
	Boolean negative = (a < 0) != (b < 0);
	if (b == 0)
		return negative ? kFractMin : kFractMax;
	if (a == 0)
		return 0;
	uint64_t ma = (a < 0) ? (uint64_t) -(int64_t) a : (uint64_t) a;
	uint64_t mb = (b < 0) ? (uint64_t) -(int64_t) b : (uint64_t) b;
	uint64_t quotient = (ma << 31) / mb;
	quotient = (quotient + 1) >> 1;
	if (quotient > 0x7fffffff)
		return negative ? kFractMin : kFractMax;
	return negative ? -(Fract) quotient : (Fract) quotient;
}


// ROM 0x000bed34 FixedMultiplyDivide
// multiplier * dividend / divisor, carried through the 64-bit comp
// arithmetic (CompMath.h) exactly as the ROM does: the full product, then
// the truncating comp divide.
extern "C" Fixed
FixedMultiplyDivide(Fixed multiplier, Fixed dividend, Fixed divisor)
{
	Int64 product;
	CompMul(multiplier, dividend, &product);
	return CompDiv(&product, divisor, nil);
}


// ROM 0x0038b094 FractSquareRoot
// The square root of a Fract, by the ROM's non-restoring digit-by-digit
// method (two bits an iteration, thirty-two iterations).  Transcribed
// verbatim from the ROM's unsigned arithmetic so the rounding matches to
// the last bit; the u-variables keep the ROM's names.
extern "C" Fract
FractSquareRoot(Fract param_1)
{
	uint32_t p = (uint32_t) param_1;
	uint32_t uVar4 = 0;
	uint32_t uVar2 = 0;
	int iVar3 = 0x20;
	do
	{
		Boolean bVar1 = (uVar4 - uVar2) < (uint32_t) (0x3fffffffU < p);
		uint32_t iVar5 = uVar4 - (uVar2 + (uint32_t) (0x3fffffffU >= p));
		uint32_t uVar6 = p + 0xc0000000U;
		if (uVar2 >= uVar4 && !bVar1)
		{
			iVar5 = iVar5 + uVar2 + (uint32_t) (0xbfffffffU < (uint32_t) (p + 0xc0000000U));
			uVar6 = p;
		}
		uVar2 = uVar2 * 2 + (uint32_t) (uVar2 < uVar4 || bVar1);
		uVar4 = (iVar5 << 2) | (uVar6 >> 0x1e);
		p = uVar6 << 2;
		iVar3 = iVar3 + -1;
	}
	while (iVar3 != 0);
	return (Fract) ((uVar2 & 1) + (uVar2 >> 1));
}


// The arctangent minimax polynomial's coefficients (2.30): atan(r) is
// r * (c0 + c1*r^2 + c2*r^4 + ... + c5*r^10), evaluated by Horner.  They
// live in the initialised RAM area at 0x0c100db8 and have no symbol, so
// they are written here from the ROM's RW-init copy (cited at that copy's
// address) rather than through romtable.py.
// ROM 0x006f10a0 (unnamed) - the RW-init copy of RAM 0x0c100db8
static const Fract kAtanCoeffs[6] =
{
	(Fract) 0x3fffa073, (Fract) 0xeab64ebe, (Fract) 0x0c62f72c,
	(Fract) 0xf88c77f2, (Fract) 0x035e92fe, (Fract) 0xff3ffe62
};

const Fixed kFixedHalfPi = 0x19220;			// pi/2 in 16.16 (radians)
const Fixed kFixedPi     = 0x32440;			// pi   in 16.16


// ROM 0x000bec4c FixedAtan2
// The angle (16.16 radians, -pi..pi) of the vector (x, y): atan2(y, x).
// It works on the magnitudes, uses the smaller-over-larger ratio through
// the arctangent polynomial (so the argument stays in 0..1), then folds
// the octant and quadrant back with the sign flags.
extern "C" Fixed
FixedAtan2(Fixed x, Fixed y)
{
	Boolean negX = false;
	Boolean negY = false;
	if (x < 0)
	{
		negX = true;
		x = -x;
		if (x == (Fixed) 0x80000000)
			x = 0x7fffffff;
	}
	if (y < 0)
	{
		negY = true;
		y = -y;
		if (y == (Fixed) 0x80000000)
			y = 0x7fffffff;
	}
	Fixed hi = x;
	Fixed lo = y;
	if (x < y)
	{
		hi = y;
		lo = x;
	}
	Fixed angle = 0;
	if (hi != 0)
	{
		Fract ratio = FractDivide(lo, hi);
		Fract r2 = FractMultiply(ratio, ratio);
		Fract acc = kAtanCoeffs[5];
		for (int i = 4; i >= 0; i--)
		{
			acc = FractMultiply(acc, r2);
			acc = kAtanCoeffs[i] + acc;
		}
		acc = FractMultiply(acc, ratio);
		angle = ((acc >> 13) & 1) + (acc >> 14);	// 2.30 Fract -> 16.16 Fixed, rounded
	}
	if (x < y)
		angle = kFixedHalfPi - angle;		// > 45 degrees: the complement
	if (negX)
		angle = kFixedPi - angle;			// x < 0: reflect across the y axis
	if (negY)
		angle = -angle;						// y < 0: below the x axis
	return angle;
}
