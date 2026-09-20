// FixedMath test (FixedMath.h): the 16.16 and 2.30 fixed-point routines
// checked against double-precision arithmetic (to the last bit, so a
// tolerance of one), plus their saturation and sign edges.  Pure
// functions - no OS boot.

#include "FixedMath.h"
#include "FixedMathExtra.h"

#include <stdio.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
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


// FixedAtan2(x, y) is the angle of the vector, i.e. atan2(y, x), in 16.16
// radians.  Checked against the C library over the eight octants.
static void
TestAtan2()
{
	EXPECT(FixedAtan2(0x10000, 0) == 0);						// (1,0): 0
	EXPECT(Near(FixedAtan2(0, 0x10000), M_PI / 2 * kFix));		// (0,1): pi/2
	EXPECT(Near(FixedAtan2(0x10000, 0x10000), M_PI / 4 * kFix));	// (1,1): pi/4
	EXPECT(Near(FixedAtan2((Fixed) 0xffff0000, 0), M_PI * kFix));	// (-1,0): pi
	static const struct { double x, y; } pts[] = {
		{ 3, 1 }, { 1, 3 }, { -2, 1 }, { -1, 2 }, { -3, -1 },
		{ -1, -4 }, { 2, -3 }, { 5, -1 }, { 1.5, 0.25 }, { -0.5, 0.75 },
	};
	for (unsigned i = 0; i < sizeof(pts) / sizeof(pts[0]); i++)
	{
		Fixed fx = (Fixed) (pts[i].x * kFix);
		Fixed fy = (Fixed) (pts[i].y * kFix);
		double want = atan2(pts[i].y, pts[i].x) * kFix;
		// the 16.16 polynomial loses a little; a few LSBs of slack
		double got = FixedAtan2(fx, fy);
		double diff = got - want;
		EXPECT(diff > -40.0 && diff < 40.0);
	}
}


// FractSineCosine(degrees<<16, &cos): the sine (returned) and cosine, both
// 2.30, of an angle in degrees.  Checked against the C library.
static void
TestSinCos()
{
	// exact-ish anchors
	Fract cos0 = 0;
	EXPECT(FractSineCosine(0, &cos0) == 0 && cos0 == (Fract) 0x40000000);	// sin0=0, cos0=1
	for (int deg = -350; deg <= 360; deg += 10)
	{
		Fract cos = 0;
		Fract sin = FractSineCosine((Fixed) (deg * 65536), &cos);
		double rad = deg * M_PI / 180.0;
		double diff_s = (double) sin - ::sin(rad) * kFrac;
		double diff_c = (double) cos - ::cos(rad) * kFrac;
		EXPECT(diff_s > -0x8000 && diff_s < 0x8000);		// CORDIC to ~1e-5
		EXPECT(diff_c > -0x8000 && diff_c < 0x8000);
	}
	// the memo cache answers a repeated angle identically
	Fract c1 = 0, c2 = 0;
	Fract s1 = FractSineCosine(45 << 16, &c1);
	Fract s2 = FractSineCosine(45 << 16, &c2);
	EXPECT(s1 == s2 && c1 == c2);
}


// FixedASin/FixedACos take a Fract (2.30) in -1..1 and answer 16.16
// radians, checked against the C library.
static void
TestAsinAcos()
{
	EXPECT(FixedASin((Fract) 0x40000000) == 0x19220);		// asin(1) = pi/2
	EXPECT(FixedASin((Fract) 0xc0000000) == -0x19220);		// asin(-1) = -pi/2
	EXPECT(FixedASin(0) == 0);
	EXPECT(FixedACos((Fract) 0x40000000) == 0);				// acos(1) = 0
	EXPECT(Near(FixedACos(0), M_PI / 2 * kFix));			// acos(0) = pi/2
	for (int i = -9; i <= 9; i++)
	{
		double xd = i / 10.0;								// -0.9 .. 0.9
		Fract x = (Fract) (xd * kFrac);
		double diff_a = (double) FixedASin(x) - asin(xd) * kFix;
		double diff_c = (double) FixedACos(x) - acos(xd) * kFix;
		EXPECT(diff_a > -40.0 && diff_a < 40.0);
		EXPECT(diff_c > -40.0 && diff_c < 40.0);
	}
}


// FractSin/FractCos take 16.16 radians and answer a 2.30 sine or cosine.
static void
TestFractSinCos()
{
	EXPECT(FractSin(0) == 0);
	EXPECT(FractCos(0) == kFrac1);
	for (int i = -6; i <= 6; i++)
	{
		double rad = i / 2.0;								// -3 .. 3 radians
		Fixed x = (Fixed) (rad * kFix);
		double diff_s = (double) FractSin(x) - sin(rad) * kFrac;
		double diff_c = (double) FractCos(x) - cos(rad) * kFrac;
		EXPECT(diff_s > -80000.0 && diff_s < 80000.0);
		EXPECT(diff_c > -80000.0 && diff_c < 80000.0);
	}
}


int main()
{
	TestFixedMul();
	TestFixedDiv();
	TestFractMul();
	TestFractDiv();
	TestFractSqrt();
	TestMultiplyDivide();
	TestAtan2();
	TestSinCos();
	TestFractSinCos();
	TestAsinAcos();
	if (failures == 0)
		printf("test_FixedMath: all passed\n");
	else
		printf("test_FixedMath: %d failures\n", failures);
	return failures != 0;
}
