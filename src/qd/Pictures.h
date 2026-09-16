/*
	File:		qd/Pictures.h

	Contains:	Drawing NewtonScript pictures: bitmap frames ({bounds, bits,
				mask, colorData}: bits a 'bits binary - a FramBitmap: the
				PixelMap's header without its base address, then the rows -
				colorData a frame {bitDepth, cBits, colorTable} or an array
				of them, one per depth) placed in a rectangle by the
				viewJustify bits (Justify) and copied to the port (DrawBitmap
				through a TPixelObj, the ROM's holder of a picture's pixel
				maps).  DrawPicture is what a picture view and the
				NewtonScript DrawShape draw with.  NOT YET RECONSTRUCTED:
				'picture binaries (QuickDraw pictures), shapes (DrawShape,
				ShapeBounds for them), the colour tables as gray tables.

	Reconstructed from the MP2100 D ROM (0x0003e868-0x0003eb84,
	0x0003f718-0x0003f86c, 0x00041818-0x00041e00, 0x0018b5f0-0x0018bb40);
	each function cites its origin.
*/

#ifndef __PICTURES_H
#define __PICTURES_H

#ifndef __PORTS_H
#include "Ports.h"
#endif
#ifndef __OBJECTS_H
#include "objects.h"
#endif

// a 'bits binary (the ROM's FramBitmap): the rows follow at +0x10
struct FramBitmap
{
	ULong32		fBaseAddr;			// +0x00  unused
	short		fRowBytes;			// +0x04
	short		fPad;				// +0x06
	Rect		fBounds;			// +0x08
};
const long kFramBitmapHeaderSize = 0x10;

// the ROM's holder of a picture's pixel maps (0x50 bytes): the bitmap
// frame (or 'bits binary) locked while it is drawn, the pixel map made
// from its bits for the port's depth, the mask's when asked
class TPixelObj
{
public:
				TPixelObj();										// ROM 0x0003e868 __ct__9TPixelObjFv
				~TPixelObj();										// ROM 0x0003e8bc __dt__9TPixelObjFv
	void		Init(RefArg picture);								// ROM 0x0003f718 Init__9TPixelObjFRC6RefVar
	void		Init(RefArg picture, Boolean withMask);				// ROM 0x00041818 Init__9TPixelObjFRC6RefVarUc
	Ref			GetFramBitmap(void);								// ROM 0x000419a4 GetFramBitmap__9TPixelObjFv
	void		FramBitMapToPixMap(const FramBitmap& bits, PixelMap* map);	// ROM 0x00041d40 FramBitMapToPixMap__9TPixelObjFRC10FramBitmapP8PixelMap

	PixelMap*	Pixels(void)					{ return fPixels; }
	PixelMap*	Mask(void)						{ return fMask; }

	RefStruct	fObject;			// +0x00  the frame, then the bits binary drawn
	PixelMap	fPixMap;			// +0x04  the pixel map over the bits
	PixelMap*	fPixels;			// +0x20  -> fPixMap (or a 'pixels binary's map)
	PixelMap	fMaskMap;			// +0x24
	PixelMap*	fMask;				// +0x40  nil for none
	Ptr			fGrayTable;			// +0x44  from the colour table (NOT YET: nil)
	long		fDepth;				// +0x48  the bits' depth
	Boolean		fLocked;			// +0x4c  the object is locked
	RefStruct	fMaskObject;		// host: the mask's bits, locked while the map is in use
};

void	DrawBitmap(RefArg bitmap, Rect* box, long mode);					// the bitmap copied into the box (sized to the bits when 0 wide)
void	Justify(Rect* r, const Rect& box, ULong justify);					// r placed in the box by the viewJustify bits
Boolean	ShapeBounds(RefArg shape, Rect* bounds);							// a bitmap frame's bounds (NOT YET: shapes)
void	DrawPicture(RefArg picture, const Rect& box, ULong justify, long mode);	// a bitmap frame drawn in the box, justified

#endif	/* __PICTURES_H */
