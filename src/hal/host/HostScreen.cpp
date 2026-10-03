/*
	File:		hal/host/HostScreen.cpp

	Contains:	THostScreenDriver: the display as gray bytes.
*/

#include "Colour.h"
#include "HostScreen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PROTOCOL_IMPL_SOURCE_MACRO(THostScreenDriver)
PROTOCOL_CLASSINFO(THostScreenDriver, "TScreenDriver", "", 0, 0, nil)


THostScreenDriver*
THostScreenDriver::New()
{
	fWidth = 320;
	fHeight = 480;
	fDepth = 4;
	fDPI = 100;
	fLandscape = false;
	fContrast = 0;
	fBacklight = 0;
	fOrientation = 0;			// portrait (1 and 3 are the landscape orientations)
	fPowered = false;
	fBlanked = false;
	fPixels = nil;
	fPanel = nil;
	fPixelBytes = 0;
	fBlits = 0;
	memset(&fLastBlit, 0, sizeof(fLastBlit));
	return this;
}


void
THostScreenDriver::Delete()
{
	if (fPixels != nil)
		free(fPixels);
	if (fPanel != nil)
		free(fPanel);
	fPixels = nil;
	fPanel = nil;
	fPixelBytes = 0;
}


// The display's size as the window has it: a portrait panel (the ROM's
// SetOrientation takes portrait to be the taller way round, as the
// MessagePad's is), turned to landscape from the start when the window is
// wider than it is tall - so the orientation the OS starts in shows the
// display the way round it was asked for.
void
THostScreenDriver::Configure(long width, long height, long depth, long dpi)
{
	fWidth = width < height ? width : height;
	fHeight = width < height ? height : width;
	fOrientation = width > height ? 1 : 0;
	fLandscape = width > height;
	fDepth = depth;
	fDPI = dpi;
}


// The gray buffer made (white) for the size.  The buffer is kept and
// cleared where it is already the right size - which is every time after
// the first, the size being the portrait one whichever way round the
// screen is turned - because the host's window holds the pointer for as
// long as it runs (host/HostWindow.h: the display comes as its size and
// its bytes).  Freeing it here and allocating again left the window
// reading a block that had been given back: on Windows the allocator
// handed the same one straight back and nothing came of it, but a buffer
// this size is mapped on its own by glibc, so there the window read an
// unmapped page as soon as the screen was set up.
void
THostScreenDriver::ScreenSetup(void)
{
	long bytes = fWidth * fHeight;
	if (fPixels != nil && fPixelBytes == bytes)
	{
		memset(fPixels, 0, (size_t) bytes);		// white
		memset(fPanel, 0, (size_t) bytes);
		return;
	}
	if (fPixels != nil)
		free(fPixels);
	if (fPanel != nil)
		free(fPanel);
	fPixels = (unsigned char*) calloc((size_t) bytes, 1);
	fPanel = (unsigned char*) calloc((size_t) bytes, 1);
	if (fPixels == nil || fPanel == nil)
	{
		free(fPixels);
		free(fPanel);
		fPixels = fPanel = nil;
	}
	fPixelBytes = fPixels != nil ? bytes : 0;
}


void
THostScreenDriver::GetScreenInfo(ScreenInfo* info)
{
	memset(info, 0, sizeof(ScreenInfo));
	info->fHeight = Height();
	info->fWidth = Width();
	info->fDepth = fDepth;
	info->fResolutionH = (short) fDPI;
	info->fResolutionV = (short) fDPI;
	// the live ink's tile (recognition/LiveInker.h) on any row, and on a
	// byte of the 1-bit display's columns (so on a byte at every depth)
	info->fAlignV = 1;
	info->fAlignH = 8;
}


void	THostScreenDriver::PowerInit(void)			{ }
void	THostScreenDriver::PowerOn(void)			{ fPowered = true; SetShown(false, fBacklight); }
void	THostScreenDriver::PowerOff(void)			{ fPowered = false; SetShown(true, fBacklight); }


// The panel's grays in a rectangle of the display shown as the power and
// the backlight have them: nothing when the panel is off, the ink a
// quarter lighter when it is lit.
void
THostScreenDriver::Render(long left, long top, long right, long bottom)
{
	long width = Width();
	long height = Height();
	if (left < 0) left = 0;
	if (top < 0) top = 0;
	if (right > width) right = width;
	if (bottom > height) bottom = height;
	for (long y = top; y < bottom; y++)
	{
		const unsigned char* in = fPanel + y * width;
		unsigned char* out = fPixels + y * width;
		for (long x = left; x < right; x++)
			out[x] = fBlanked ? 0 : ColourScreen() ? in[x] : (unsigned char) (in[x] - in[x] / 4);	// (a colour screen's values are not grays to lighten)
	}
}


// The power or the light changed: while the display is shown as drawn the
// drawing goes straight to it and the panel's copy is not kept up, so it
// is taken from the display when that stops, and given back when it
// starts again.
void
THostScreenDriver::SetShown(Boolean blanked, long backlight)
{
	if (fPixels == nil)
	{
		fBlanked = blanked;
		fBacklight = backlight;
		return;
	}
	Boolean was = Rendered();
	if (!was && (blanked || backlight != 0))
		memcpy(fPanel, fPixels, (size_t) fPixelBytes);
	fBlanked = blanked;
	fBacklight = backlight;
	if (Rendered())
		Render(0, 0, Width(), Height());
	else if (was)
		memcpy(fPixels, fPanel, (size_t) fPixelBytes);
}
void	THostScreenDriver::AutoAdjustFeatures(void)	{ }
void	THostScreenDriver::EnterIdleMode(void)		{ }
void	THostScreenDriver::ExitIdleMode(void)		{ }


// The rectangle of the map shown: each pixel's value scaled to a gray
// (all ones black), at the same place on the display (dst names where;
// src and dst are the same rectangle in the ROM's use).
void
THostScreenDriver::Blit(PixelMap* map, Rect* src, Rect* dst, long mode)
{
	if (fPixels == nil || map == nil)
		return;
	fBlits++;
	fLastBlit = *dst;
	long depth = map->pixMapFlags & kPixMapDepth;
	long maxValue = (1 << depth) - 1;
	long width = Width();
	long height = Height();
	// (the rows and the run of each row that lie on both the map and the
	//  display worked out once, and each pixel read along the row and
	//  turned to its gray through a table, rather than looked up alone -
	//  the display is updated after every drawing, so this is on the path
	//  of everything drawn)
	long xFrom = src->left, xTo = src->right;
	if (xFrom < map->bounds.left)
		xFrom = map->bounds.left;
	if (xTo > map->bounds.right)
		xTo = map->bounds.right;
	if (dst->left + (xFrom - src->left) < 0)
		xFrom = src->left - dst->left;
	if (dst->left + (xTo - src->left) > width)
		xTo = width - dst->left + src->left;
	if (xFrom >= xTo)
		return;
	// a whole byte of pixels at a time where the row allows: each byte's
	// grays from a table, made once for each depth (the live inker's srcOr,
	// which leaves white pixels alone, goes pixel by pixel)
	static unsigned char sGray[9][256];
	static unsigned char sExpand[9][256][8];
	static Boolean sMade[9];
	if (depth < 1 || depth > 8)
		return;
	if (!sMade[depth])
	{
		for (long v = 0; v <= maxValue && v < 256; v++)
			sGray[depth][v] = (unsigned char) ((v * 255) / maxValue);
		if (depth < 8)
			for (long b = 0; b < 256; b++)
				for (long k = 0; k < 8 / depth; k++)
					sExpand[depth][b][k] = sGray[depth][(b >> (8 - depth * (k + 1))) & maxValue];
		sMade[depth] = true;
	}
	const unsigned char* gray = sGray[depth];
	unsigned char (*expand)[8] = sExpand[depth];
	long perByte = 8 / depth;
	Boolean wholeBytes = mode != srcOr && depth < 8;
	const unsigned char* bits;
	ULong storage = map->pixMapFlags & kPixMapStorage;
	if (storage == kPixMapPtr)
		bits = (const unsigned char*) map->baseAddr;
	else if (storage == kPixMapHandle)
		bits = (const unsigned char*) *(Handle) map->baseAddr;
	else
		bits = (const unsigned char*) map + (intptr_t) map->baseAddr;
	for (long y = src->top; y < src->bottom; y++)
	{
		long dy = dst->top + (y - src->top);
		if (dy < 0 || dy >= height || y < map->bounds.top || y >= map->bounds.bottom)
			continue;
		const unsigned char* row = bits + (y - map->bounds.top) * map->rowBytes;
		unsigned char* out = (Rendered() ? fPanel : fPixels) + dy * width + dst->left + (xFrom - src->left);
		long bit = (xFrom - map->bounds.left) * depth;
		long x = xFrom;
		if (wholeBytes)
		{
			for (; x < xTo && (bit & 7) != 0; x++, bit += depth, out++)
				*out = gray[(row[bit >> 3] >> (8 - depth - (bit & 7))) & maxValue];
			// (the copy's size a constant for each depth, so that it is a
			//  single store rather than a call)
			const unsigned char* in = row + (bit >> 3);
			long bytes = (xTo - x) / perByte;
			switch (depth)
			{
			case 1:		for (long b = 0; b < bytes; b++, out += 8) memcpy(out, expand[in[b]], 8); break;
			case 2:		for (long b = 0; b < bytes; b++, out += 4) memcpy(out, expand[in[b]], 4); break;
			default:	for (long b = 0; b < bytes; b++, out += 2) memcpy(out, expand[in[b]], 2); break;
			}
			x += bytes * perByte;
			bit += bytes * 8;
		}
		for (; x < xTo; x++, bit += depth, out++)
		{
			long value = (depth == 8) ? row[bit >> 3] : (row[bit >> 3] >> (8 - depth - (bit & 7))) & maxValue;
			if (mode == srcOr && value == 0)
				continue;			// the live inker's: only its ink put on the display
			*out = gray[value];
		}
	}
	if (Rendered())
		Render(dst->left + (xFrom - src->left), dst->top, dst->left + (xTo - src->left), dst->top + (src->bottom - src->top));
}


void
THostScreenDriver::DoubleBlit(PixelMap* map, PixelMap* /*map2*/, Rect* src, Rect* dst, long mode)
{
	Blit(map, src, dst, mode);
}


long
THostScreenDriver::GetFeature(long feature)
{
	switch (feature)
	{
	case kScreenFeatureContrast:	return fContrast;
	case kScreenFeatureBacklight:	return fBacklight;
	case kScreenFeatureOrientation:	return fOrientation;
	default:						return 0;
	}
}


// The orientation feature turns the display: 1 and 3 landscape.
void
THostScreenDriver::SetFeature(long feature, long value)
{
	switch (feature)
	{
	case kScreenFeatureContrast:	fContrast = value;		break;
	case kScreenFeatureBacklight:	SetShown(fBlanked, value);	break;
	case kScreenFeatureOrientation:
		fOrientation = value;
		fLandscape = value == 1 || value == 3;
		ScreenSetup();
		break;
	default:
		break;
	}
}


// (host: a colour screen's values are its palette's - qd/Colour.h; a
// value's gray is its colour's luminance, 0 white .. 255 black)
static unsigned char
ValueGray(unsigned char value)
{
	if (!ColourScreen())
		return value;
	const UChar* rgb = ColourPalette() + value * 3;
	return (unsigned char) (255 - (rgb[0] * 77 + rgb[1] * 150 + rgb[2] * 29) / 256);
}


unsigned char
THostScreenDriver::Gray(long x, long y) const
{
	if (fPixels == nil || x < 0 || y < 0 || x >= Width() || y >= Height())
		return 0;
	return ValueGray(fPixels[y * Width() + x]);
}


unsigned long
THostScreenDriver::Colour(long x, long y) const
{
	if (fPixels == nil || x < 0 || y < 0 || x >= Width() || y >= Height())
		return 0xffffff;
	unsigned char value = fPixels[y * Width() + x];
	if (!ColourScreen())
	{
		unsigned long level = 255 - value;
		return (level << 16) | (level << 8) | level;
	}
	const UChar* rgb = ColourPalette() + value * 3;
	return ((unsigned long) rgb[0] << 16) | ((unsigned long) rgb[1] << 8) | rgb[2];
}


Boolean
THostScreenDriver::WritePPM(const char* path) const
{
	if (fPixels == nil)
		return false;
	FILE* f = fopen(path, "wb");
	if (f == nil)
		return false;
	long width = Width();
	long height = Height();
	fprintf(f, "P6\n%ld %ld\n255\n", width, height);
	for (long y = 0; y < height; y++)
		for (long x = 0; x < width; x++)
		{
			unsigned long c = Colour(x, y);
			fputc((int) ((c >> 16) & 0xff), f);
			fputc((int) ((c >> 8) & 0xff), f);
			fputc((int) (c & 0xff), f);
		}
	fclose(f);
	return true;
}


Boolean
THostScreenDriver::WritePGM(const char* path) const
{
	if (fPixels == nil)
		return false;
	FILE* f = fopen(path, "wb");
	if (f == nil)
		return false;
	long width = Width();
	long height = Height();
	fprintf(f, "P5\n%ld %ld\n255\n", width, height);
	for (long i = 0; i < width * height; i++)
		fputc(255 - ValueGray(fPixels[i]), f);
	fclose(f);
	return true;
}


Boolean
THostScreenDriver::WritePBM(const char* path) const
{
	if (fPixels == nil)
		return false;
	FILE* f = fopen(path, "wb");
	if (f == nil)
		return false;
	long width = Width();
	long height = Height();
	fprintf(f, "P4\n%ld %ld\n", width, height);
	for (long y = 0; y < height; y++)
	{
		unsigned char byte = 0;
		for (long x = 0; x < width; x++)
		{
			if (ValueGray(fPixels[y * width + x]) >= 128)
				byte |= (unsigned char) (0x80 >> (x & 7));
			if ((x & 7) == 7 || x == width - 1)
			{
				fputc(byte, f);
				byte = 0;
			}
		}
	}
	fclose(f);
	return true;
}
