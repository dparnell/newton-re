/*
	File:		intl/Coordinates.h

	Contains:	The world map's coordinate maths: a place's longitude and
				latitude turned into a position on a picture of the world
				and back, and the distance between two places.

				A coordinate is an integer of 2^28 to a half turn - so
				-2^28 to 2^28 across the map's width for longitude, and
				-2^27 to 2^27 for latitude - which is what a location frame
				in the Time Zones application keeps.  LongitudeToCoordinate
				and LatitudeToCoordinate scale one into a pixel of a map
				that many pixels wide or tall; CoordinateToLongitude and
				CoordinateToLatitude are their inverses, and the ROM relies
				on the arithmetic wrapping for the longitude to come back.

				CircleDistance is the spherical law of cosines over the
				same units, answering whole miles or kilometres rounded to
				the nearest ten.

	Reconstructed from the MP2x00 US ROM (0x002551c0-0x002554b0); each
	function cites its origin.
*/

#ifndef __COORDINATES_H
#define __COORDINATES_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif
#include "objects.h"


// LongitudeToCoordinate(longitude, width) - the pixel across a map that
// many pixels wide (ROM 0x002551c0)
Ref		LongitudeToCoordinate(RefArg rcvr, RefArg longitude, RefArg width);

// LatitudeToCoordinate(latitude, height) - the pixel down a map that many
// pixels tall (ROM 0x00255220)
Ref		LatitudeToCoordinate(RefArg rcvr, RefArg latitude, RefArg height);

// CoordinateToLongitude(x, width) (ROM 0x00255280)
Ref		CoordinateToLongitude(RefArg rcvr, RefArg x, RefArg width);

// CoordinateToLatitude(y, height) (ROM 0x002552e4)
Ref		CoordinateToLatitude(RefArg rcvr, RefArg y, RefArg height);

// CircleDistance(longitude1, latitude1, longitude2, latitude2, units) -
// the great-circle distance, in 'miles or else kilometres (ROM 0x00255348)
Ref		CircleDistance(RefArg rcvr, RefArg longitude1, RefArg latitude1,
					   RefArg longitude2, RefArg latitude2, RefArg units);

void	RegisterCoordinateNatives(void);

#endif	/* __COORDINATES_H */
