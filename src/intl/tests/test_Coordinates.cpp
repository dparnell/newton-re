// World map coordinate test: longitudes and latitudes turned into pixels
// of a map and back (they must round-trip, which is the whole point of the
// ROM's wrapping arithmetic), the edges and the middle of the map, and
// CircleDistance between places whose distance is known, in both units.
// Runs over a standalone kernel heap and object heap without ROM objects.
#include "Coordinates.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "memory/host/KernelHeap.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdio.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// A coordinate is 2^28 to a half turn, so this many to a degree.
static const double kPerDegree = 268435456.0 / 180.0;

static long
Coord(double degrees)
{
	return (long) (degrees * kPerDegree);
}

static long
Longitude(long coord, long width)
{
	return RINT(LongitudeToCoordinate(RefVar(NILREF), RefVar(MAKEINT(coord)), RefVar(MAKEINT(width))));
}

static long
Latitude(long coord, long height)
{
	return RINT(LatitudeToCoordinate(RefVar(NILREF), RefVar(MAKEINT(coord)), RefVar(MAKEINT(height))));
}

static long
FromX(long x, long width)
{
	return RINT(CoordinateToLongitude(RefVar(NILREF), RefVar(MAKEINT(x)), RefVar(MAKEINT(width))));
}

static long
FromY(long y, long height)
{
	return RINT(CoordinateToLatitude(RefVar(NILREF), RefVar(MAKEINT(y)), RefVar(MAKEINT(height))));
}


// Greenwich is the middle of the map, the date line each edge, the north
// pole the top and the south pole the bottom.
static void
TestEdges()
{
	EXPECT(Longitude(0, 320) == 160);
	// the date line is the left edge from either side: a coordinate of
	// exactly 2^28 shifted up by three overflows to -2^31, which is the
	// same word as -2^28 shifted up by three, so half a turn east and half
	// a turn west are the same pixel (ROM behaviour, kept)
	EXPECT(Longitude(Coord(180), 320) == 0);
	EXPECT(Longitude(-Coord(180), 320) == 0);
	EXPECT(Longitude(Coord(179), 320) >= 318 && Longitude(Coord(179), 320) <= 320);
	EXPECT(Longitude(Coord(90), 320) == 240);
	EXPECT(Latitude(0, 240) == 120);
	EXPECT(Latitude(Coord(90), 240) == 0);
	EXPECT(Latitude(-Coord(90), 240) == 240);
	EXPECT(Latitude(Coord(45), 240) == 60);
}


// A coordinate taken to a pixel of a map as wide as the coordinate range
// and back must be itself again: the longitude's half turn is added on the
// way out and added again on the way back, wrapping to nothing.
static void
TestRoundTrip()
{
	static const long kWidth = 0x1000000;			// a map wide enough to lose nothing
	for (long degrees = -175; degrees <= 175; degrees += 5)
	{
		long coord = Coord(degrees);
		long back = FromX(Longitude(coord, kWidth), kWidth);
		EXPECT(back > coord - 0x100 && back < coord + 0x100);
	}
	for (long degrees = -85; degrees <= 85; degrees += 5)
	{
		long coord = Coord(degrees);
		long back = FromY(Latitude(coord, kWidth), kWidth);
		EXPECT(back > coord - 0x100 && back < coord + 0x100);
	}
	// and the middle of an ordinary map is Greenwich and the equator
	EXPECT(FromX(160, 320) == 0);
	EXPECT(FromY(120, 240) == 0);
}


// CircleDistance answers whole miles or kilometres, rounded to ten.
static void
TestDistance(double long1, double lat1, double long2, double lat2, double radius, Ref units)
{
	RefVar answer(CircleDistance(RefVar(NILREF),
								 RefVar(MAKEINT(Coord(long1))), RefVar(MAKEINT(Coord(lat1))),
								 RefVar(MAKEINT(Coord(long2))), RefVar(MAKEINT(Coord(lat2))),
								 RefVar(units)));
	double r1 = lat1 * M_PI / 180.0, r2 = lat2 * M_PI / 180.0;
	double d = (long2 - long1) * M_PI / 180.0;
	double expected = acos(sin(r1) * sin(r2) + cos(r1) * cos(r2) * cos(d)) * radius;
	double got = (double) RINT(answer);
	EXPECT(RINT(answer) % 10 == 0);
	EXPECT(got > expected - 20.0 - expected / 100.0 && got < expected + 20.0 + expected / 100.0);
}


static void
TestCircleDistance()
{
	// London to Paris (344 km), London to New York (5570 km), Sydney to
	// Rio (13520 km, almost half the world), and a place to itself
	TestDistance(-0.1278, 51.5074, 2.3522, 48.8566, 6371.0, NILREF);
	TestDistance(-0.1278, 51.5074, -74.0060, 40.7128, 6371.0, NILREF);
	TestDistance(151.2093, -33.8688, -43.1729, -22.9068, 6371.0, NILREF);
	TestDistance(-0.1278, 51.5074, -74.0060, 40.7128, 3959.0, RSSYMmiles);
	TestDistance(139.6917, 35.6895, 139.6917, 35.6895, 6371.0, NILREF);

	// a symbol that is not 'miles is kilometres
	RefVar km(CircleDistance(RefVar(NILREF),
							 RefVar(MAKEINT(Coord(-0.1278))), RefVar(MAKEINT(Coord(51.5074))),
							 RefVar(MAKEINT(Coord(-74.0060))), RefVar(MAKEINT(Coord(40.7128))),
							 RefVar(Intern((char*) "kilometres"))));
	RefVar miles(CircleDistance(RefVar(NILREF),
								RefVar(MAKEINT(Coord(-0.1278))), RefVar(MAKEINT(Coord(51.5074))),
								RefVar(MAKEINT(Coord(-74.0060))), RefVar(MAKEINT(Coord(40.7128))),
								RefVar(RSSYMmiles)));
	EXPECT(RINT(km) > RINT(miles));
}


int
main()
{
	InitHostStandaloneHeap();
	gObjectHeapSize = 0x100000;
	InitObjects();

	TestEdges();
	TestRoundTrip();
	TestCircleDistance();

	printf("test_Coordinates: %s\n", failures == 0 ? "ok" : "FAILED");
	return failures == 0 ? 0 : 1;
}
