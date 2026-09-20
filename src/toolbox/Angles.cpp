/*
	File:		toolbox/Angles.cpp

	Contains:	The angle arithmetic of Angles.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	The group at 0x00030c3c-0x00030e24 is plain C in the debug table (no
	mangled names), so the citations carry the bare names.
*/

#include "Angles.h"
#include "FixedMathExtra.h"

#include <stdint.h>

extern const unsigned char	kDegreesOfFraction[64];		// AngleTables.cpp
extern const unsigned int	kTangentBelowOne[46];
extern const unsigned char	kDegreesOfWhole[64];
extern const unsigned int	kTangentAboveOne[46];


// ROM 0x00030c3c MapDegrees
// An angle brought into (-180, 180] a whole turn at a time.
long
MapDegrees(long degrees)
{
	while (degrees > kHalfTurnDegrees)
		degrees -= kFullTurnDegrees;
	while (degrees <= -kHalfTurnDegrees)
		degrees += kFullTurnDegrees;
	return degrees;
}


// ROM 0x00030c68 DegToRad
Fixed
DegToRad(long degrees)
{
	return FixedMultiplyDivide(MapDegrees(degrees), kPiRadians, kHalfTurnDegrees);
}


// ROM 0x00030c8c DeltaAngle
// How far it is from one angle to another, the short way round.
long
DeltaAngle(long from, long to)
{
	return MapDegrees(to - from);
}


// ROM 0x00030c94 AddAngle
long
AddAngle(long a, long b)
{
	return MapDegrees(a + b);
}


// ROM 0x00030c9c MidAngle
long
MidAngle(long a, long b)
{
	return AddAngle(DeltaAngle(a, b) >> 1, a);
}


// ROM 0x002aa530 AngleFromSlope__Fl
// The arctangent of a 16.16 slope, in whole degrees, by table lookup: the
// tangents of the half degrees (kTangentBelowOne up to 45, kTangentAboveOne
// from 45 on) say which degree a slope falls in, and two byte tables start
// the search close enough that at most a step or two of walking is left.
// A slope of 8 or more starts at 83 degrees.
//
// The answer is 180 minus the degree for a positive slope, because the
// points it is used on are screen points, whose y grows downwards.
//
// (The two tables of tangents are not one array: kTangentBelowOne ends
// with 0xffff, a value no fraction can exceed, and the ROM indexes
// kTangentAboveOne from 45, so its first entry is the tangent of 45.5
// degrees.  The last eight bytes of kTangentBelowOne are also the first
// eight of kDegreesOfWhole, which are never read as bytes - the whole
// part of a slope in this branch is at least 1, so the index is at least
// 8.  See docs/curiosities.md.)
ULong
AngleFromSlope(Fixed slope)
{
	// (uint32_t, not ULong: ULong is pointer-sized here and the ROM's
	// registers are 32 bits, so a negative slope must not sign-extend)
	Boolean negative = slope < 0;
	uint32_t magnitude = (uint32_t) slope;
	if (negative)
		magnitude = (uint32_t) (0 - magnitude);		// (wrapping, as the ARM's rsb does)
	uint32_t whole = magnitude >> 16;
	ULong degrees;
	if (whole == 0)
	{
		uint32_t fraction = magnitude & 0xffff;
		degrees = kDegreesOfFraction[fraction >> 10];
		if (kTangentBelowOne[degrees] < fraction)
			degrees++;
	}
	else
	{
		if (whole >= 8)
			degrees = 83;
		else
			degrees = kDegreesOfWhole[magnitude >> 13];
		while (kTangentAboveOne[degrees - 45] < magnitude)
			degrees++;
	}
	return negative ? degrees : 180 - degrees;
}


// ROM 0x00030cc0 PtsToAngle
// The angle of the line from a to b, rounded to a multiple of unit.  The
// two are compared by x first: the slope handed to AngleFromSlope is
// dx/dy, so the vertical case (the same x) is the one it cannot answer and
// is taken here - 0 when a is at or below b, 180 when it is above.
long
PtsToAngle(const FPoint* a, const FPoint* b, Fixed unit)
{
	if (a == nil || b == nil)
		return 0;
	long degrees;
	if (a->x < b->x)
		degrees = (long) AngleFromSlope(FixedDivide(a->x - b->x, a->y - b->y)) << 16;
	else if (a->x > b->x)
		degrees = -((long) AngleFromSlope(FixedDivide(b->x - a->x, a->y - b->y)) << 16);
	else
		degrees = a->y >= b->y ? 0 : kHalfTurnDegrees;
	return FixedRoundBy(MapDegrees(degrees), unit);
}


// ROM 0x00030d50 GetSlope
long
GetSlope(const FPoint* a, const FPoint* b)
{
	return PtsToAngle(a, b, kFix1);
}


// ROM 0x00030d58 PtsToAngleR__FP6FPointT1
// The angle of the line from b to a in 16.16 radians, straight out of
// FixedAtan2 - and 0 for an answer outside +/-4 radians, which no angle
// is: the arctangent has gone wrong (it overflowed) and nothing is
// better than nonsense.  The two points being in the same place is 0 as
// well; the same x is straight up or straight down.
Fixed
PtsToAngleR(const FPoint* a, const FPoint* b)
{
	Fixed dx = a->x - b->x;
	Fixed dy = b->y - a->y;
	if (dx == 0)
	{
		if (dy == 0)
			return 0;
		return dy >= 0 ? 0 : -kPiRadians;
	}
	Fixed radians = FixedAtan2(dy, dx);
	if (radians < -0x40000 || radians > 0x40000)
		return 0;
	return radians;
}


// ROM 0x00030dbc NORM__FPl
// An angle in radians brought into (-pi, pi] a turn at a time.
void
NORM(long* radians)
{
	if (*radians > kPiRadians)
	{
		do
			*radians -= 2 * kPiRadians;
		while (*radians > kPiRadians);
		return;
	}
	if (*radians > -kPiRadians)
		return;
	do
		*radians += 2 * kPiRadians;
	while (*radians <= -kPiRadians);
}
