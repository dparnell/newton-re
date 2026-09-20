/*
	File:		intl/Coordinates.cpp

	Contains:	The world map's coordinate maths (Coordinates.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Coordinates.h"

#include "ObjectHeap.h"
#include "Frames.h"
#include "NativeFunctions.h"
#include "RSSymbols.h"
#include "FixedMath.h"
#include "FixedMathExtra.h"

#include <stdint.h>


// pi/2 in 16.16 radians, which is what a quarter turn of coordinate - a
// Fract of 0.25 - is worth: the scale the angles are worked in.
static const Fixed kHalfPi = 0x00019220;

// The whole turn CircleDistance folds a negative arccosine by.  It is one
// less than the pi the rest of the fixed-point library uses (0x32440,
// FixedMath.cpp): the two were written from different constants.
static const Fixed kCircleDistancePi = 0x0003243f;

// The Earth's radius, whole units, as CircleDistance keeps it.
static const long kEarthRadiusMiles = 3959;
static const long kEarthRadiusKilometres = 6371;


// ROM 0x002551c0 LongitudeToCoordinate
// The pixel of a map `width` wide that a longitude falls on.  The
// coordinate is doubled - taken to the 2.30 fraction of a turn that a
// Fract is - and put half a map to the right, so that zero is the middle
// of the picture; the map's width is then scaled by it.
//
// The doubling is `(x << 3) / 4` rounded towards zero, which is what the
// compiler makes of `x * 8 / 4`: the rounding can never apply, since
// the multiple of eight always divides by four exactly.
//
// A coordinate of exactly 2^28 - half a turn east, the date line - shifts
// up by three into -2^31, which is the same word half a turn west gives,
// so both ends of the map answer the left edge.  Kept as the ROM has it.
Ref
LongitudeToCoordinate(RefArg /*rcvr*/, RefArg longitude, RefArg width)
{
	long scaled = (long) (int32_t) ((uint32_t) RINT(longitude) << 3);
	if (scaled < 0)
		scaled += 3;
	Fract fraction = (Fract) (int32_t) ((uint32_t) (scaled >> 2) + 0x20000000u);
	return MAKEINT(FractMultiply(RINT(width), fraction));
}


// ROM 0x00255220 LatitudeToCoordinate
// The same down the map: a latitude is a quarter turn at the poles, so it
// is taken four times over and measured down from the top.
Ref
LatitudeToCoordinate(RefArg /*rcvr*/, RefArg latitude, RefArg height)
{
	uint32_t scaled = (uint32_t) RINT(latitude) << 3;
	scaled += scaled >> 31;											// towards zero
	Fract fraction = (Fract) (int32_t) (0x20000000u - (uint32_t) ((int32_t) scaled >> 1));
	return MAKEINT(FractMultiply(RINT(height), fraction));
}


// ROM 0x00255280 CoordinateToLongitude
// The longitude a pixel across a map `width` wide stands for - the
// inverse of LongitudeToCoordinate, which it undoes by adding half a turn
// rather than subtracting it: the addition overflows to the same place,
// so `x * 4 + 0x80000000` divided by eight is the original longitude
// again.  (The ROM is written for an ARM, where that simply wraps.)
Ref
CoordinateToLongitude(RefArg /*rcvr*/, RefArg x, RefArg width)
{
	Fract fraction = FractDivide(RINT(x), RINT(width));
	long value = (long) (int32_t) (0x80000000u + ((uint32_t) fraction << 2));
	if (value < 0)
		value += 7;
	return MAKEINT(value >> 3);
}


// ROM 0x002552e4 CoordinateToLatitude
// The latitude a pixel down the map stands for.
Ref
CoordinateToLatitude(RefArg /*rcvr*/, RefArg y, RefArg height)
{
	Fract fraction = FractDivide(RINT(y), RINT(height));
	long value = (long) (int32_t) ((0x20000000u - (uint32_t) fraction) << 1);
	if (value < 0)
		value += 7;
	return MAKEINT(value >> 3);
}


// ROM 0x00255348 CircleDistance
// The great-circle distance between two places by the spherical law of
// cosines - cos d = sin(lat1) sin(lat2) + cos(lat1) cos(lat2) cos(long1 -
// long2) - in whole miles when asked for 'miles and whole kilometres
// otherwise, rounded to the nearest ten.
//
// The two longitudes are subtracted as they stand, without being unpacked
// first: the difference of two integer Refs is the Ref of the difference,
// tag and all, so one RINT does for both.
//
// The rounding is `(d + 6) / 10 * 10`, which is a rounding to the nearest
// ten that tips at .4 rather than .5 - six is not half of ten.  And an
// arccosine of less than 0x60 (a sixty-fourth of a degree) is called zero,
// so two places a few hundred yards apart are no distance at all.
Ref
CircleDistance(RefArg /*rcvr*/, RefArg longitude1, RefArg latitude1,
			   RefArg longitude2, RefArg latitude2, RefArg units)
{
	Fixed lat1 = FractMultiply((Fract) (int32_t) ((uint32_t) RINT(latitude1) << 3), kHalfPi);
	Fixed lat2 = FractMultiply((Fract) (int32_t) ((uint32_t) RINT(latitude2) << 3), kHalfPi);
	Ref difference = (Ref) ((ULong) (Ref) longitude1 - (ULong) (Ref) longitude2);	// (a Ref is pointer-sized here, which is what ULong is)
	Fixed dLong = FractMultiply((Fract) (int32_t) ((uint32_t) RINT(difference) << 3), kHalfPi);

	Fract sines = FractMultiply(FractSin(lat1), FractSin(lat2));
	Fract cosines = FractMultiply(FractCos(lat1), FractCos(lat2));
	cosines = FractMultiply(FractCos(dLong), cosines);
	Fixed angle = FixedACos((Fract) (int32_t) ((uint32_t) sines + (uint32_t) cosines));
	if (angle < 0)
		angle += kCircleDistancePi;
	if (angle < 0x60)
		angle = 0;

	long radius = EQRef(units, RSSYMmiles) ? kEarthRadiusMiles : kEarthRadiusKilometres;
	long distance = FixedMultiply(angle, radius);
	return MAKEINT((distance + 6) / 10 * 10);
}


void
RegisterCoordinateNatives(void)
{
	RegisterNativeFunction("LongitudeToCoordinate", (void*) LongitudeToCoordinate, 2);
	RegisterNativeFunction("LatitudeToCoordinate", (void*) LatitudeToCoordinate, 2);
	RegisterNativeFunction("CoordinateToLongitude", (void*) CoordinateToLongitude, 2);
	RegisterNativeFunction("CoordinateToLatitude", (void*) CoordinateToLatitude, 2);
	RegisterNativeFunction("CircleDistance", (void*) CircleDistance, 5);
}
