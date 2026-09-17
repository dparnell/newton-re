/*
	File:		views/Bits.cpp

	Contains:	TBits, the offscreen bits.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "Bits.h"
#include "Rects.h"
#include "Draw.h"
#include "Screen.h"
#include "NewtonMemory.h"
#include <string.h>


// ROM 0x00042b84 __ct__5TBitsFv
// No bits yet; a stack instance registers its cleanup (the host's
// destructor runs on a throw).
TBits::TBits()
{
	baseAddr = nil;
	fPort = nil;
	fDrawn = false;
	fOwnsBits = true;
}


// ROM 0x000459fc __dt__5TBitsFv
TBits::~TBits()
{
	Cleanup();
}


// ROM 0x00045a3c Cleanup__5TBitsFv
// The drawing ended and the bits disposed when they are ours.
void
TBits::Cleanup(void)
{
	if (baseAddr == nil)
		return;
	// NOT YET RECONSTRUCTED: EndDrawing (the TBitsPort)
	if (fOwnsBits)
	{
		DisposHandle((Handle) baseAddr);
		fOwnsBits = false;
	}
	baseAddr = nil;
}


// ROM 0x00042e84 InitBitMap__5TBitsSFRC5TRectP8PixelMap
// A handle pixel map of the screen's depth over the rectangle: the row
// bytes the width in bits rounded up to 32, the resolution 72; ==> the
// size of its bits.
long
TBits::InitBitMap(const Rect& bounds, PixelMap* map)
{
	long depth;
	GetGrafInfo(kGrafInfoDepth, &depth);
	if (depth == 0)
		depth = PixelMapDepth(&GetCurrentPort()->portBits);		// host: no screen (a test drawing offscreen)
	long rowBytes = ((depth * (bounds.right - bounds.left) + 31) & ~31) >> 3;
	map->baseAddr = nil;
	map->rowBytes = (short) rowBytes;
	map->bounds = bounds;
	map->pixMapFlags = depth;			// kPixMapHandle is 0
	map->deviceRes.h = kDefaultDPI;
	map->deviceRes.v = kDefaultDPI;
	map->grayTable = nil;
	return rowBytes * (bounds.bottom - bounds.top);
}


// ROM 0x0004388c Constructor__5TBitsFRC5TRect
// Bits for the rectangle; ==> whether there was memory (nothing for an
// empty rectangle).
Boolean
TBits::Constructor(const Rect& bounds)
{
	if (EmptyRect(&bounds))
		return false;
	long size = InitBitMap(bounds, this);
	baseAddr = (Ptr) NewHandle(size);
	fDrawn = false;
	return baseAddr != nil;
}


// ROM 0x0004526c Constructor__5TBitsFRC8PixelMap
// Over another map's bits (not ours to dispose).
void
TBits::Constructor(const PixelMap& map)
{
	*(PixelMap*) this = map;
	fOwnsBits = false;
	fDrawn = false;
}


// ROM 0x00045a84 SetBounds__5TBitsFRC5TRect
void
TBits::SetBounds(const Rect& newBounds)
{
	bounds = newBounds;
}


// ROM 0x00042c5c CopyFromScreen__5TBitsFRC5TRectT1lPP6Region
// The current port's pixels of src copied into dst of ours.
void
TBits::CopyFromScreen(const Rect& src, const Rect& dst, long mode, RgnHandle mask)
{
	GrafPort* port;
	GetPort(&port);
	CopyBits(&port->portBits, this, &src, &dst, mode, mask);
	fDrawn = true;
}


// ROM 0x00042cac Draw__5TBitsFRC5TRectT1lPP6Region
// Our pixels of src copied to dst of the current port.
void
TBits::Draw(const Rect& src, const Rect& dst, long mode, RgnHandle mask)
{
	GrafPort* port;
	GetPort(&port);
	CopyBits(this, &port->portBits, &src, &dst, mode, mask);
	fDrawn = true;
}


// ROM 0x00042c00 Draw__5TBitsFRC5TRectlPP6Region
void
TBits::Draw(const Rect& dst, long mode, RgnHandle mask)
{
	Rect src = bounds;
	Draw(src, dst, mode, mask);
}


// ROM 0x00042c3c CopyIntoBitmap__5TBitsFP8PixelMaplPP6Region
void
TBits::CopyIntoBitmap(PixelMap* map, long mode, RgnHandle mask)
{
	CopyBits(this, map, &bounds, &map->bounds, mode, mask);
}


// ROM 0x00042d14 Fill__5TBitsFl
// Every word of the bits set to the pattern (rowBytes * height bytes).
void
TBits::Fill(long pattern)
{
	Ptr bits = GetPixelMapBits(this);
	long size = rowBytes * (bounds.bottom - bounds.top);
	unsigned char* p = (unsigned char*) bits;
	for (long i = 0; i < size; i++)
		p[i] = (unsigned char) (pattern >> (24 - 8 * (i & 3)));		// the word's bytes as the ROM stores them
}
