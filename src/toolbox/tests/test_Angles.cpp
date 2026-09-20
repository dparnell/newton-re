// Angles test (Angles.h): the 16.16 degree arithmetic, the arctangent
// table lookup and the two length/rounding helpers, checked against
// double-precision trigonometry.  Pure functions - no OS boot.

#include "Angles.h"
#include "FixedMathExtra.h"

#include <stdio.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const double kFix = 65536.0;

static Boolean
Near(long got, double want, double tolerance)
{
	double d = (double) got - want;
	return d > -tolerance && d < tolerance;
}

static FPoint
Pt(double x, double y)
{
	FPoint p;
	p.x = (Fixed) (x * kFix);
	p.y = (Fixed) (y * kFix);
	return p;
}


static void
TestMapDegrees()
{
	EXPECT(MapDegrees(0) == 0);
	EXPECT(MapDegrees(kHalfTurnDegrees) == kHalfTurnDegrees);		// 180 stays
	EXPECT(MapDegrees(kHalfTurnDegrees + 1) == kHalfTurnDegrees + 1 - kFullTurnDegrees);
	EXPECT(MapDegrees(-kHalfTurnDegrees) == kHalfTurnDegrees);		// -180 becomes 180
	EXPECT(MapDegrees(-kHalfTurnDegrees + 1) == -kHalfTurnDegrees + 1);
	EXPECT(MapDegrees(5 * kFullTurnDegrees + 0x100000) == 0x100000);
	EXPECT(MapDegrees(-5 * kFullTurnDegrees - 0x100000) == -0x100000);
}


static void
TestAngleArithmetic()
{
	EXPECT(AddAngle(0x5a0000, 0x5a0000) == kHalfTurnDegrees);		// 90 + 90 == 180
	EXPECT(AddAngle(0x5a0000, 0x960000) == 0x5a0000 + 0x960000 - kFullTurnDegrees);	// 90 + 150 wraps
	EXPECT(DeltaAngle(0x0a0000, 0x140000) == 0x0a0000);				// 10 -> 20 is +10
	EXPECT(DeltaAngle(0x140000, 0x0a0000) == -0x0a0000);
	// 170 -> -170 is +20 the short way, not -340
	EXPECT(DeltaAngle(0xaa0000, -0xaa0000) == 0x140000);
	EXPECT(MidAngle(0x0a0000, 0x140000) == 0x0f0000);				// half way from 10 to 20
	EXPECT(MidAngle(0xaa0000, -0xaa0000) == kHalfTurnDegrees);		// ... and across the join
	EXPECT(Near(DegToRad(kHalfTurnDegrees), M_PI * kFix, 2.0));
	EXPECT(Near(DegToRad(0x5a0000), M_PI / 2 * kFix, 2.0));
}


static void
TestNorm()
{
	long r = kPiRadians;
	NORM(&r);
	EXPECT(r == kPiRadians);					// pi stays
	r = -kPiRadians;
	NORM(&r);
	EXPECT(r == kPiRadians);					// -pi becomes pi
	r = 3 * kPiRadians;
	NORM(&r);
	EXPECT(r == kPiRadians);
	r = -3 * kPiRadians + 0x1000;
	NORM(&r);
	EXPECT(r == -kPiRadians + 0x1000);
	r = 0x1000;
	NORM(&r);
	EXPECT(r == 0x1000);
}


// AngleFromSlope answers the arctangent in whole degrees, counted from
// 180 for a positive slope (its callers' y grows downwards).
static void
TestAngleFromSlope()
{
	EXPECT(AngleFromSlope(0) == 180);
	EXPECT(AngleFromSlope(kFix1) == 135);						// slope 1 -> 45 degrees
	EXPECT(AngleFromSlope(-kFix1) == 45);
	for (double t = -60.0; t <= 60.0; t += 0.25)
	{
		double slope = tan(t * M_PI / 180.0);
		ULong got = AngleFromSlope((Fixed) (slope * kFix));
		double want = t >= 0 ? 180.0 - t : -t;
		EXPECT(Near((long) got, want, 1.5));
	}
	// a slope of eight or more is 83 degrees or steeper
	EXPECT(AngleFromSlope(8 * kFix1) >= 180 - 83);
	EXPECT(AngleFromSlope(0x7fffffff) == 180 - 90);
}


static void
TestPtsToAngle()
{
	// GetSlope is a bearing from a to b with 0 straight up the screen and
	// the degrees growing clockwise: right is 90, down is 180, left is -90.
	FPoint origin = Pt(0, 0);
	FPoint up = Pt(0, -10);
	FPoint down = Pt(0, 10);
	FPoint right = Pt(10, 0);
	FPoint left = Pt(-10, 0);
	EXPECT(GetSlope(&origin, &up) == 0);
	EXPECT(GetSlope(&origin, &down) == kHalfTurnDegrees);
	EXPECT(GetSlope(&origin, &right) == 0x5a0000);
	EXPECT(GetSlope(&origin, &left) == -0x5a0000);
	FPoint downRight = Pt(10, 10);
	FPoint upRight = Pt(10, -10);
	EXPECT(GetSlope(&origin, &downRight) == 0x870000);		// 135
	EXPECT(GetSlope(&origin, &upRight) == 0x2d0000);		// 45
	// the reverse bearing is half a turn away
	EXPECT(GetSlope(&downRight, &origin) == -0x2d0000);
	// rounded to the unit asked for: 135 is already a multiple of 45
	EXPECT(PtsToAngle(&origin, &downRight, 0x2d0000) == 0x870000);
	FPoint shallow = Pt(100, 10);							// 90 + atan(1/10): about 96 degrees
	EXPECT(Near(GetSlope(&origin, &shallow), 96.0 * kFix, 1.5 * kFix));
	EXPECT(PtsToAngle(&origin, &shallow, 0x2d0000) == 0x5a0000);	// to the nearest 45: 90
	EXPECT(PtsToAngle(nil, &downRight, kFix1) == 0);
}


static void
TestPtsToAngleR()
{
	FPoint a = Pt(0, 0);
	FPoint b = Pt(0, 0);
	EXPECT(PtsToAngleR(&a, &b) == 0);
	b = Pt(0, -10);						// b above a: -pi
	EXPECT(PtsToAngleR(&a, &b) == -kPiRadians);
	b = Pt(0, 10);
	EXPECT(PtsToAngleR(&a, &b) == 0);
	// FixedAtan2's first argument is its x: the angle is atan2(-dy, dx)
	a = Pt(0, 0);
	b = Pt(10, 10);
	EXPECT(Near(PtsToAngleR(&a, &b), atan2(-10.0, 10.0) * kFix, 0.01 * kFix));
}


static void
TestLengthAndRounding()
{
	EXPECT(FixedLength(0, 0) == 0);
	EXPECT(FixedLength(kFix1, 0) == kFix1);
	EXPECT(FixedLength(0, -kFix1) == kFix1);
	// it over-estimates by up to three per cent (the coefficient on the
	// greater of the two is exactly one, so the error is all one way)
	for (int i = 0; i <= 16; i++)
	{
		double angle = i * M_PI / 32.0;
		double dx = 100.0 * cos(angle);
		double dy = 100.0 * sin(angle);
		Fixed got = FixedLength((Fixed) (dx * kFix), (Fixed) (dy * kFix));
		EXPECT(Near(got, 100.0 * kFix, 0.03 * 100.0 * kFix));
	}
	// half way rounds down, because FixedRoundBy takes one off after adding half the unit
	EXPECT(FixedRoundBy(0x00018000, kFix1) == kFix1);			// 1.5 -> 1
	EXPECT(FixedRoundBy(0x00018001, kFix1) == 0x00020000);		// just over 1.5 -> 2
	EXPECT(FixedRoundBy(0x00028000, kFix1) == 0x00020000);		// 2.5 -> 2
	EXPECT(FixedRoundBy(0x00028000, 0x00008000) == 0x00028000);	// already a multiple of a half
}


int
main(void)
{
	TestMapDegrees();
	TestAngleArithmetic();
	TestNorm();
	TestAngleFromSlope();
	TestPtsToAngle();
	TestPtsToAngleR();
	TestLengthAndRounding();
	if (failures != 0)
	{
		fprintf(stderr, "%d failure(s)\n", failures);
		return 1;
	}
	printf("Angles: all tests passed\n");
	return 0;
}
