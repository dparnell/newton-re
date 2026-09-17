/*
	File:		hal/host/HostScreen.cpp

	Contains:	THostScreenDriver: the display as gray bytes.
*/

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
	fOrientation = 1;
	fPowered = false;
	fPixels = nil;
	fBlits = 0;
	memset(&fLastBlit, 0, sizeof(fLastBlit));
	return this;
}


void
THostScreenDriver::Delete()
{
	if (fPixels != nil)
		free(fPixels);
	fPixels = nil;
}


void
THostScreenDriver::Configure(long width, long height, long depth, long dpi)
{
	fWidth = width;
	fHeight = height;
	fDepth = depth;
	fDPI = dpi;
}


// the gray buffer made (white) for the size
void
THostScreenDriver::ScreenSetup(void)
{
	if (fPixels != nil)
		free(fPixels);
	fPixels = (unsigned char*) calloc((size_t) (fWidth * fHeight), 1);
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
}


void	THostScreenDriver::PowerInit(void)			{ }
void	THostScreenDriver::PowerOn(void)			{ fPowered = true; }
void	THostScreenDriver::PowerOff(void)			{ fPowered = false; }
void	THostScreenDriver::AutoAdjustFeatures(void)	{ }
void	THostScreenDriver::EnterIdleMode(void)		{ }
void	THostScreenDriver::ExitIdleMode(void)		{ }


// a pixel of a map: its rows big-endian, 1, 2, 4 or 8 bits deep (as
// qd/Ports.cpp's GetPixel reads them; not linked from here)
static long
MapPixel(const PixelMap* map, long x, long y)
{
	long depth = map->pixMapFlags & kPixMapDepth;
	const unsigned char* bits;
	ULong storage = map->pixMapFlags & kPixMapStorage;
	if (storage == kPixMapPtr)
		bits = (const unsigned char*) map->baseAddr;
	else if (storage == kPixMapHandle)
		bits = (const unsigned char*) *(Handle) map->baseAddr;
	else
		bits = (const unsigned char*) map + (intptr_t) map->baseAddr;
	const unsigned char* row = bits + (y - map->bounds.top) * map->rowBytes;
	long bit = (x - map->bounds.left) * depth;
	if (depth == 8)
		return row[bit >> 3];
	long shift = 8 - depth - (bit & 7);
	return (row[bit >> 3] >> shift) & ((1 << depth) - 1);
}


// The rectangle of the map shown: each pixel's value scaled to a gray
// (all ones black), at the same place on the display (dst names where;
// src and dst are the same rectangle in the ROM's use).
void
THostScreenDriver::Blit(PixelMap* map, Rect* src, Rect* dst, long /*mode*/)
{
	if (fPixels == nil || map == nil)
		return;
	fBlits++;
	fLastBlit = *dst;
	long depth = map->pixMapFlags & kPixMapDepth;
	long maxValue = (1 << depth) - 1;
	long width = Width();
	long height = Height();
	for (long y = src->top; y < src->bottom; y++)
	{
		long dy = dst->top + (y - src->top);
		if (dy < 0 || dy >= height || y < map->bounds.top || y >= map->bounds.bottom)
			continue;
		for (long x = src->left; x < src->right; x++)
		{
			long dx = dst->left + (x - src->left);
			if (dx < 0 || dx >= width || x < map->bounds.left || x >= map->bounds.right)
				continue;
			long value = MapPixel(map, x, y);
			fPixels[dy * width + dx] = (unsigned char) ((value * 255) / maxValue);
		}
	}
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
	case kScreenFeatureBacklight:	fBacklight = value;		break;
	case kScreenFeatureOrientation:
		fOrientation = value;
		fLandscape = value == 1 || value == 3;
		ScreenSetup();
		break;
	default:
		break;
	}
}


unsigned char
THostScreenDriver::Gray(long x, long y) const
{
	if (fPixels == nil || x < 0 || y < 0 || x >= Width() || y >= Height())
		return 0;
	return fPixels[y * Width() + x];
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
		fputc(255 - fPixels[i], f);
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
			if (fPixels[y * width + x] >= 128)
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
