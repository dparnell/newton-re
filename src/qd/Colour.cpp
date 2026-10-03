/*
	File:		qd/Colour.cpp

	Contains:	The host's colour screen's palette (qd/Colour.h,
				docs/qd/colour.md).  Not a reconstruction: the ROM has no
				colour screen.
*/

#include "Colour.h"

#include <string.h>

static Boolean	gColourScreen = false;
static UChar	gPalette[256][3];
static Boolean	gPaletteMade = false;
// the nearest entry for each colour of five bits a component, made on
// first use (32 x 32 x 32)
static UChar	gNearest[32 * 32 * 32];
static Boolean	gNearestMade = false;


void
SetColourScreen(Boolean colour)
{
	gColourScreen = colour;
}


Boolean
ColourScreen(void)
{
	return gColourScreen;
}


// One entry and its complement: entry i the colour, entry 255 - i its
// opposite.
static void
SetPair(long i, long r, long g, long b)
{
	gPalette[i][0] = (UChar) r;
	gPalette[i][1] = (UChar) g;
	gPalette[i][2] = (UChar) b;
	gPalette[255 - i][0] = (UChar) (255 - r);
	gPalette[255 - i][1] = (UChar) (255 - g);
	gPalette[255 - i][2] = (UChar) (255 - b);
}


static void
MakePalette(void)
{
	// the sixteen four-bit grays at 17 * v: 0 white, 255 black (each its
	// complement's partner already: 255 - 17v = 17(15 - v))
	for (long v = 0; v < 8; v++)
	{
		long level = 255 - 17 * v;
		SetPair(17 * v, level, level, level);
	}
	// the other 120 pairs: the indices below 128 that are no gray's
	long free[120];
	long count = 0;
	for (long i = 1; i < 128 && count < 120; i++)
		if (i % 17 != 0)
			free[count++] = i;
	long next = 0;
	// the colour cube's 108 complementary pairs, each by the member that
	// comes first (5 - c is the complement of level c; levels 0, 51 ... 255)
	for (long r = 0; r < 6; r++)
		for (long g = 0; g < 6; g++)
			for (long b = 0; b < 6; b++)
			{
				long rc = 5 - r, gc = 5 - g, bc = 5 - b;
				if (r * 36 + g * 6 + b < rc * 36 + gc * 6 + bc)
					SetPair(free[next++], r * 51, g * 51, b * 51);
			}
	// twelve pairs of grays between the sixteen (the lighter member given)
	static const long kGrays[12] = { 247, 230, 213, 196, 179, 162, 145, 128, 251, 234, 217, 200 };
	for (long k = 0; k < 12 && next < count; k++)
		SetPair(free[next++], kGrays[k], kGrays[k], kGrays[k]);
	gPaletteMade = true;
}


const UChar*
ColourPalette(void)
{
	if (!gPaletteMade)
		MakePalette();
	return &gPalette[0][0];
}


static void
MakeNearest(void)
{
	const UChar* palette = ColourPalette();
	for (long r = 0; r < 32; r++)
		for (long g = 0; g < 32; g++)
			for (long b = 0; b < 32; b++)
			{
				long R = (r << 3) | (r >> 2), G = (g << 3) | (g >> 2), B = (b << 3) | (b >> 2);
				long best = 0, bestDistance = 0x7fffffff;
				for (long i = 0; i < 256; i++)
				{
					long dr = R - palette[i * 3], dg = G - palette[i * 3 + 1], db = B - palette[i * 3 + 2];
					long distance = 2 * dr * dr + 4 * dg * dg + 3 * db * db;		// (the eye's weights, roughly)
					if (distance < bestDistance)
					{
						bestDistance = distance;
						best = i;
					}
				}
				gNearest[(r << 10) | (g << 5) | b] = (UChar) best;
			}
	gNearestMade = true;
}


UChar
ColourToIndex(ULong red, ULong green, ULong blue)
{
	if (!gNearestMade)
		MakeNearest();
	return gNearest[(((red >> 11) & 31) << 10) | (((green >> 11) & 31) << 5) | ((blue >> 11) & 31)];
}


void
IndexToColour(UChar index, ULong* red, ULong* green, ULong* blue)
{
	const UChar* entry = ColourPalette() + index * 3;
	*red = entry[0] * 0x101UL;
	*green = entry[1] * 0x101UL;
	*blue = entry[2] * 0x101UL;
}
