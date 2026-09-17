// FixedMath test (FixedMath.h): the 16.16 and 2.30 fixed-point routines
// checked against double-precision arithmetic (to the last bit, so a
// tolerance of one), plus their saturation and sign edges.  Pure
// functions - no OS boot.

#include "FixedMath.h"

#include <stdio.h>
#include <math.h>
#include <stdint.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// close to within one LSB (fixed-point rounding)
static Boolean
Near(long got, double want)
{
	double d = (double) got - want;
	return d > -1.5 && d < 1.5;
}

static const double kFix = 65536.0;			// 16.16
static const double kFrac = 1073741824.0;		// 2.30 (2^30)


static void
TestFixedMul()
{
	EXPECT(FixedMultiply(kFix1, kFix1) == kFix1);				// 1 * 1 == 1
	EXPECT(FixedMultiply(kFix1, 0) == 0);
	EXPECT(FixedMultiply(0x30000, 0x28000) == 0x78000);			// 3 * 2.5 == 7.5
	EXPECT(FixedMultiply(-0x20000, 0x18000) == -0x30000);		// -2 * 1.5 == -3
	// rounding and a spot value
	EXPECT(Near(FixedMultiply(0x14444, 0x38ccc), ((double) 0x14444 / kFix) * ((double) 0x38ccc / kFix) * kFix));
	// saturation
	EXPECT(FixedMultiply(0x40000000, 0x40000000) == 0x7fffffff);
	EXPECT(FixedMultiply(0x40000000, (Fixed) 0x80000000) == (Fixed) 0x80000000);
}


static void
TestFixedDiv()
{
	EXPECT(FixedDivide(kFix1, kFix1) == kFix1);
	EXPECT(FixedDivide(0x78000, 0x30000) == 0x28000);			// 7.5 / 3 == 2.5
	EXPECT(FixedDivide(-0x30000, 0x18000) == -0x20000);			// -3 / 1.5 == -2
	EXPECT(FixedDivide(kFix1, 0) == 0x7fffffff);				// by zero: +inf
	EXPECT(FixedDivide(-kFix1, 0) == (Fixed) 0x80000000);
	EXPECT(Near(FixedDivide(0x38ccc, 0x14444), ((double) 0x38ccc / (double) 0x14444) * kFix));
	EXPECT(FixedDivide(0x400000, 0x100) == 0x40000000);			// 64 / (1/256) == 16384
}


static void
TestFractMul()
{
	EXPECT(FractMultiply(kFrac1, kFrac1) == kFrac1);			// 1 * 1 == 1
	EXPECT(FractMultiply(kFracHalf, kFracHalf) == 0x10000000);	// 0.5 * 0.5 == 0.25
	EXPECT(FractMultiply(kFrac1, kFracHalf) == kFracHalf);
	EXPECT(FractMultiply(-kFracHalf, kFracHalf) == -0x10000000);
	EXPECT(Near(FractMultiply(0x2b851eb8, 0x0ccccccd), ((double) 0x2b851eb8 / kFrac) * ((double) 0x0ccccccd / kFrac) * kFrac));
	EXPECT(FractMultiply(0x7fffffff, 0x7fffffff) == 0x7fffffff);	// ~2 * ~2 saturates to the 2.30 max
}


static void
TestFractDiv()
{
	EXPECT(FractDivide(kFracHalf, kFrac1) == kFracHalf);		// 0.5 / 1 == 0.5
	EXPECT(FractDivide(kFrac1, 0) == 0x7fffffff);
	EXPECT(FractDivide(0x10000000, kFracHalf) == kFracHalf);	// 0.25 / 0.5 == 0.5
	EXPECT(Near(FractDivide(0x0ccccccd, 0x2b851eb8), ((double) 0x0ccccccd / (double) 0x2b851eb8) * kFrac));
	EXPECT(FractDivide(kFrac1, kFracHalf) == 0x7fffffff);		// 1 / 0.5 == 2, saturates (2.30 max < 2)
}


static void
TestFractSqrt()
{
	EXPECT(FractSquareRoot(kFrac1) == kFrac1);					// sqrt(1) == 1
	EXPECT(FractSquareRoot(0x10000000) == kFracHalf);			// sqrt(0.25) == 0.5
	EXPECT(FractSquareRoot(0) == 0);
	for (int i = 1; i <= 8; i++)
	{
		Fract x = (Fract) ((int64_t) i * kFrac1 / 8);			// i/8 in 2.30
		double want = sqrt((double) x * kFrac);					// sqrt(x/2^30) * 2^30
		EXPECT(Near(FractSquareRoot(x), want));
	}
}


static void
TestMultiplyDivide()
{
	// multiplier * dividend / divisor, all Fixed
	EXPECT(FixedMultiplyDivide(0x30000, 0x40000, 0x20000) == 0x60000);	// 3 * 4 / 2 == 6
	EXPECT(FixedMultiplyDivide(kFix1, 0x50000, 0x50000) == kFix1);		// x / x == 1
	EXPECT((int64_t) FixedMultiplyDivide(0x28000, 0x30000, 0x18000) ==
		((int64_t) 0x28000 * 0x30000 / 0x18000));						// matches the 64-bit form
}


int main()
{
	TestFixedMul();
	TestFixedDiv();
	TestFractMul();
	TestFractDiv();
	TestFractSqrt();
	TestMultiplyDivide();
	if (failures == 0)
		printf("test_FixedMath: all passed\n");
	else
		printf("test_FixedMath: %d failures\n", failures);
	return failures != 0;
}
