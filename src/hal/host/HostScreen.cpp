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
	fOrientation = 0;			// portrait (1 and 3 are the landscape orientations)
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
	unsigned char gray[256];
	for (long v = 0; v <= maxValue && v < 256; v++)
		gray[v] = (unsigned char) ((v * 255) / maxValue);
	// a whole byte of pixels at a time where the row allows: each byte's
	// grays from a table (the live inker's srcOr, which leaves white
	// pixels alone, goes pixel by pixel)
	long perByte = 8 / depth;
	unsigned char expand[256][8];
	Boolean wholeBytes = mode != srcOr && depth < 8;
	if (wholeBytes)
		for (long b = 0; b < 256; b++)
			for (long k = 0; k < perByte; k++)
				expand[b][k] = gray[(b >> (8 - depth * (k + 1))) & maxValue];
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
		unsigned char* out = fPixels + dy * width + dst->left + (xFrom - src->left);
		long bit = (xFrom - map->bounds.left) * depth;
		long x = xFrom;
		if (wholeBytes)
		{
			for (; x < xTo && (bit & 7) != 0; x++, bit += depth, out++)
				*out = gray[(row[bit >> 3] >> (8 - depth - (bit & 7))) & maxValue];
			for (; x + perByte <= xTo; x += perByte, bit += 8, out += perByte)
				memcpy(out, expand[row[bit >> 3]], perByte);
		}
		for (; x < xTo; x++, bit += depth, out++)
		{
			long value = (depth == 8) ? row[bit >> 3] : (row[bit >> 3] >> (8 - depth - (bit & 7))) & maxValue;
			if (mode == srcOr && value == 0)
				continue;			// the live inker's: only its ink put on the display
			*out = gray[value];
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
