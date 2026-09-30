/*
	File:		qd/GrayShrink.h

	Contains:	The antialiasing shrink of a one-bit picture into grays:
				the TPixelMapAntialias protocol and TGrayShrink, its one
				implementation, which InitGraf registers.  StretchBits
				hands it a one-bit map flagged 0x1000000 being made
				smaller on a four-bit one (view:GrayShrink - the fax
				viewer, protoImageView, zoomed out): each cell of source
				pixels that makes one destination pixel is counted, and the
				count looked up in a table of sixteen grays
				(MakeGrayTable: four quartiles, their widths from the
				grayLevels preference or worked out from the cell size).

	Reconstructed from the MP2x00 US ROM (0x000e402c-0x000e4f40,
	0x00388b34-0x00388bb8); each function cites its origin.
*/

#ifndef __GRAYSHRINK_H
#define __GRAYSHRINK_H

#include "Protocols.h"
#include "Regions.h"

PROTOCOL TPixelMapAntialias : public TProtocol
{
public:
	static TPixelMapAntialias*	New(const char* implementation);
	void			Delete();

	VIRTUAL void	GrayShrink(PixelMap* src, PixelMap* dst, Rect* srcRect, Rect* dstRect,
							   RgnHandle clip1, RgnHandle clip2, RgnHandle mask) ENDVIRTUAL;	// ROM 0x00388b34 GrayShrink__18TPixelMapAntialiasFP8PixelMapT1P4RectT3PP6RegionN25
};

PROTOCOL TGrayShrink : public TPixelMapAntialias
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TGrayShrink);	// (Sizeof 0x000e4714, ClassInfo 0x00388b7c)

	// (the ROM's dispatch table has no New or Delete: slot 0 is empty)
	TGrayShrink*	New();
	void			Delete();

	void			GrayShrink(PixelMap* src, PixelMap* dst, Rect* srcRect, Rect* dstRect,
							   RgnHandle clip1, RgnHandle clip2, RgnHandle mask);	// ROM 0x000e471c GrayShrink__11TGrayShrinkFP8PixelMapT1P4RectT3PP6RegionN25
};

void	RegisterGrayShrink(void);		// (InitGraf's registration of TGrayShrink)

#endif	/* __GRAYSHRINK_H */
