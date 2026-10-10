/*
	File:		host/HostFontRaster.h

	Contains:	The host system's fonts as the platform draws them - the
				calls host/HostFonts.cpp makes the Newton's host font
				provider of (qd/HostFonts.h, docs/qd/host-fonts.md).

				One implementation per host answers them, built into the
				host window's library: host/win32/HostFontRaster.cpp over
				GDI.  Where there is none (its #else) HostFontRasterOpen
				answers false and the Newton draws no host fonts.

	Characters are UTF-16 code units, which is what a Newton UniChar is.
*/

#ifndef __HOSTFONTRASTER_H
#define __HOSTFONTRASTER_H

#include <stdint.h>

// (no Newton headers here: the Windows headers and they do not mix)

bool	HostFontRasterOpen(void);											// ==> whether the host draws fonts
int		HostFontRasterCount(void);											// its families
bool	HostFontRasterName(int index, uint16_t* name, int size);			// size in units, the nought included
int		HostFontRasterFaces(const uint16_t* family);						// bit (1 << face) for each of plain, bold, italic, bold italic it has
const char* const*	HostFontRasterDefaults(void);							// families to offer when none are chosen (nil-terminated)

struct HostFontRasterMetrics
{
	int		ascent, descent, leading, widMax;	// pixels; descent positive below the baseline
	int		top, bottom, left;					// the furthest any glyph goes above, below (negative) and left of the pen
};

struct HostFontRasterGlyph
{
	int		width, height;				// the image (0 x 0 for nothing to draw)
	int		bearingX, bearingY;			// the pen to its left column; the baseline up to its top row
	int32_t	advance;					// 16.16
	int		depth;						// 1: a bit a pixel, top bit leftmost; 8: coverage 0-255 a pixel
	int		rowBytes;
	const unsigned char* bits;			// the face's, good until its next glyph
};

void*	HostFontRasterOpenFace(const uint16_t* family, int face, int pixelsX, int pixelsY);	// face: 0-3 as above, one the family has; nil none
void	HostFontRasterCloseFace(void* face);
void	HostFontRasterGetMetrics(void* face, HostFontRasterMetrics* metrics);
bool	HostFontRasterGetGlyph(void* face, uint16_t ch, int depth, HostFontRasterGlyph* glyph);	// ==> false: no glyph for it

#endif	/* __HOSTFONTRASTER_H */
