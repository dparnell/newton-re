/*
	File:		qd/Colour.h

	Contains:	The host's colour screen (docs/qd/colour.md): an eight-bit
				screen whose pixel values index a palette of colours
				rather than run from white to black.

				The MessagePad's QuickDraw takes colours everywhere - packed
				RGB patterns, dither patterns, text colours, colour tables,
				direct-colour bitmaps - and makes each a gray through
				RGBtoGray at the screen's depth.  On a colour screen
				RGBtoGray at eight bits answers the palette entry nearest
				the colour instead, and GrayToRGB the entry's colour, so the
				same calls draw in colour.  The palette keeps what the ROM's
				drawing takes for granted of a gray screen:
				  - 0 is white and 255 black (a one-bit pixel widened to
				    eight is 255, a clear one 0);
				  - entry 17 * v is the four-bit gray v (a four-bit pixel
				    widened to eight is v * 17: Stretch.cpp);
				  - entry 255 - i is entry i's complement, so that xor with
				    all ones - how QuickDraw inverts, hilites included -
				    turns every colour into its opposite.
				Besides the sixteen grays it holds a 6 x 6 x 6 colour cube
				(the web's "safe" colours) and 24 grays between the
				sixteen.

				DEVIATION (an extension: the ROM has no colour screen); off
				unless newton is started with --colour, and four-bit screens
				are untouched.
*/

#ifndef __COLOUR_H
#define __COLOUR_H

#include "Newton.h"

void			SetColourScreen(Boolean colour);	// before the screen is made: its pixel values a palette's
Boolean			ColourScreen(void);

// the palette: 256 entries of red, green and blue, 0..255 each
const UChar*	ColourPalette(void);

// the entry nearest a colour of sixteen-bit components, and an entry's
// colour as sixteen-bit components
UChar			ColourToIndex(ULong red, ULong green, ULong blue);
void			IndexToColour(UChar index, ULong* red, ULong* green, ULong* blue);

#endif	/* __COLOUR_H */
