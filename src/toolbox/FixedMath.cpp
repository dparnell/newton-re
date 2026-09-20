/*
	File:		toolbox/FixedMath.cpp

	Contains:	16.16 fixed-point multiply and divide (FixedMath.h): the
				ARM assembly routines of the ROM's fplib, which work on
				magnitudes, round to nearest and saturate on overflow.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	FixedASin/FixedACos are declared in FixedMathExtra.h (the DDK omits
	them).  NOT YET RECONSTRUCTED: FixMul32 (0x000b1f14, a
	different fixed format).
*/

#include "FixedMath.h"
#include "CompMath.h"
#include "FixedMathExtra.h"
#include <stdint.h>

const Fixed kFixedMax = 0x7fffffff;
const Fixed kFixedMin = (Fixed) 0x80000000;


// ROM 0x00394688 FixedMultiply
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


// ROM 0x003945a0 FixedDivide
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


// ROM 0x0039467c FractMultiply
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


// ROM 0x00394594 FractDivide
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


// ROM 0x000bdc10 FixedMultiplyDivide
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


// ROM 0x00394714 FractSquareRoot
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


// ROM 0x000bdb28 FixedAtan2
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


// The CORDIC arctangent table (2.30) FractSineCosine rotates by, and its
// one-entry memo cache.  Both live unnamed in the initialised RAM area
// (the table at RAM 0x0c100d38, the cache at 0x0c100d2c), so they are
// written from the ROM's RW-init copy and cited there; the cache's initial
// values are the RW-init image's (angle 0: cosine 1.0, sine 0).
// ROM 0x006f1020 (unnamed) - the RW-init copy of the CORDIC table at RAM 0x0c100d38
static const Fract kCordicTable[30] =
{
	0x35214e63, 0x1c128e81, 0x0e400225, 0x0727154d, 0x03946f2a, 0x01ca5435,
	0x00e52daf, 0x0072974a, 0x00394bb3, 0x001ca5dc, 0x000e52ee, 0x00072977,
	0x000394bc, 0x0001ca5e, 0x0000e52f, 0x00007297, 0x0000394c, 0x00001ca6,
	0x00000e53, 0x00000729, 0x00000395, 0x000001ca, 0x000000e5, 0x00000073,
	0x00000039, 0x0000001d, 0x0000000e, 0x00000007, 0x00000004, 0x00000002,
};

static Fixed sLastSinCosDegrees = 0;			// RAM 0x0c100d2c
static Fract sLastCosine = (Fract) 0x40000000;	// RAM 0x0c100d30 (cos 0 = 1.0)
static Fract sLastSine = 0;						// RAM 0x0c100d34

// The CORDIC gain 1/K in 2.30, the vector FractSineCosine starts from.
const Fract kCordicGain = (Fract) 0x4dba76d4;


// ROM 0x000bedd0 (unnamed)
// Rotate the vector (in[0], 0) by in[1] degrees (16.16) with CORDIC,
// leaving the sine in out[0] and the cosine in out[1] (2.30).  The angle is
// reduced modulo 360 degrees and folded into the first octant; the residual
// (angle - 45 degrees, times 512) drives the rotation over kCordicTable,
// and the quadrant is restored by the two sign tests.  Transcribed verbatim
// from the ROM's integer arithmetic; the i-variables keep its names.
static void
CordicRotate(const int* param_1, int* param_2)
{
	int iVar10 = 1;
	int iVar8 = (param_1[0] + 1) >> 1;
	int iVar4 = param_1[1];
	const Fract* piVar11 = kCordicTable;
	if (0x167ffff < iVar4 || iVar4 < -0x1680000)
		iVar4 = iVar4 % 0x1680000;					// reduce modulo 360 degrees
	if (iVar4 < 0)
		iVar4 = iVar4 + 0x1680000;
	int iVar5 = iVar4;
	if (0xb40000 < iVar4)
		iVar5 = 0x1680000 - iVar4;					// fold 180..360 down to 0..180
	int iVar6 = iVar5;
	if (0x5a0000 < iVar5)
		iVar6 = 0xb40000 - iVar5;					// fold 90..180 down to 0..90
	iVar6 = (iVar6 - 0x2d0000) * 0x200;				// (angle - 45 degrees) * 512
	int uVar3 = 1;
	int iVar9 = iVar8;
	do
	{
		int iVar1 = (iVar9 + iVar10) >> uVar3;
		int iVar2;
		int iVar7;
		if (iVar6 < 1)
		{
			iVar1 = -iVar1;
			iVar2 = (iVar8 + iVar10) >> uVar3;
			iVar7 = *piVar11;
		}
		else
		{
			iVar2 = -((iVar8 + iVar10) >> uVar3);
			iVar7 = -*piVar11;
		}
		iVar8 = iVar1 + iVar8;
		piVar11 = piVar11 + 1;
		iVar9 = iVar9 + iVar2;
		iVar6 = iVar6 + iVar7;
		iVar10 = iVar10 << 1;
		uVar3 = uVar3 + 1;
	}
	while (uVar3 < 0x1f);
	if (0x5a0000 < iVar5)
		iVar9 = -iVar9;
	if (0xb40000 < iVar4)
		iVar8 = -iVar8;
	param_2[1] = iVar8;
	param_2[0] = iVar9;
}


// ROM 0x000bdc40 FractSineCosine
// The sine (returned) and cosine (through the out parameter) of an angle in
// degrees (16.16), both 2.30.  The last angle's result is memoised, so a
// repeated angle is answered from the cache.
extern "C" Fract
FractSineCosine(Fixed degrees, Fract* cosine)
{
	if (degrees != sLastSinCosDegrees)
	{
		int in[2];
		int out[2];
		in[0] = kCordicGain;
		in[1] = degrees;
		sLastSinCosDegrees = degrees;
		CordicRotate(in, out);
		sLastCosine = out[0];
		*cosine = out[0];
		sLastSine = out[1];
		return out[1];
	}
	*cosine = sLastCosine;
	return sLastSine;
}


// The degrees in a radian, 16.16 (57.29578).
const Fixed kFixedRadiansToDegrees = 0x00394bc0;


// ROM 0x00038060 xFracSin__Fl
// The sine (2.30) of an angle in 16.16 radians.  FractSineCosine works in
// degrees, so the angle is converted first; the exported FractSin
// (0x0011b850) is a branch straight to this.
Fract
FractSin(Fixed radians)
{
	Fract cosine;
	return FractSineCosine(FixedMultiply(radians, kFixedRadiansToDegrees), &cosine);
}


// ROM 0x00038088 xFracCos__Fl
// The cosine, which is the one FractSineCosine answers through its out
// parameter (FractCos 0x0011b854 branches here).
Fract
FractCos(Fixed radians)
{
	Fract cosine;
	FractSineCosine(FixedMultiply(radians, kFixedRadiansToDegrees), &cosine);
	return cosine;
}


// ROM 0x00255158 FixedASin__Fl
// The arcsine of a Fract (2.30, -1..1), in 16.16 radians: atan2 of x and
// sqrt(1 - x^2) through the arctangent polynomial (asin x = atan(x /
// sqrt(1 - x^2))).  When |x| >= 1 it answers +/- pi/2.
Fixed
FixedASin(Fract x)
{
	Fract x2 = FractMultiply(x, x);
	if (0x40000000 - x2 < 1)					// |x| >= 1
		return x < 1 ? -kFixedHalfPi : kFixedHalfPi;
	Fixed root = FractSquareRoot(0x40000000 - x2);
	Boolean negRoot = false;					// FractSquareRoot is never negative,
	Boolean negX = false;						// so this branch is dead - kept as the ROM has it
	if (root < 0)
	{
		negRoot = true;
		root = -root;
		if (root == (Fixed) 0x80000000)
			root = 0x7fffffff;
	}
	if (x < 0)
	{
		negX = true;
		x = -x;
		if (x == (Fixed) 0x80000000)
			x = 0x7fffffff;
	}
	Fixed hi = root;
	Fixed lo = x;
	if (root < x)
	{
		hi = x;
		lo = root;
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
		angle = ((acc >> 13) & 1) + (acc >> 14);
	}
	if (root < x)
		angle = kFixedHalfPi - angle;
	if (negRoot)
		angle = kFixedPi - angle;
	if (negX)
		angle = -angle;
	return angle;
}


// ROM 0x002551a4 FixedACos__Fl
// The arccosine: pi/2 - arcsine.
Fixed
FixedACos(Fract x)
{
	return kFixedHalfPi - FixedASin(x);
}


// ROM 0x000bd87c FixedLength
// The length of a vector without a square root: the greater of |dx| and
// |dy| plus a fraction of the lesser, the fraction chosen by which of the
// two halves of the octant the vector is in - a shift-and-add
// approximation that over-estimates by up to three per cent.  The shifts are
// the ROM's arithmetic ones, which is safe here because both have been
// made positive first.
extern "C" Fixed
FixedLength(Fixed dx, Fixed dy)
{
	if (dx < 0)
		dx = -dx;
	if (dy < 0)
		dy = -dy;
	if (dy <= dx)
	{
		if (dy < (dx >> 1))
			return dx + (dy >> 2) - (dy >> 7) - (dy >> 8) - (dy >> 9);
		return dx - (dx >> 3) - (dx >> 5) - (dx >> 6) + (dy >> 1) + (dy >> 4) + (dy >> 5);
	}
	if (dx < (dy >> 1))
		return dy + (dx >> 2) - (dx >> 7) - (dx >> 8) - (dx >> 9);
	return dy - (dy >> 3) - (dy >> 5) - (dy >> 6) + (dx >> 1) + (dx >> 4) + (dx >> 5);
}


// ROM 0x000bd904 FixedRoundBy
// A fixed-point value rounded to a multiple of unit: divided by it, the
// fraction of the quotient dropped, multiplied back.  Half the unit is
// added first to round to nearest - less one, so a value exactly half way
// rounds down.
extern "C" Fixed
FixedRoundBy(Fixed value, Fixed unit)
{
	Fixed quotient = FixedDivide(value + (unit >> 1) - 1, unit);
	return FixedMultiply((Fixed) ((ULong) quotient & 0xffff0000), unit);
}
