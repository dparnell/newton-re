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

	Reconstructed from the MP2x00 US ROM (0x0003e7b8-0x0003ead4,
	0x0003f614-0x0003f86c, 0x00040f28-0x00041530, 0x001895c0-0x00189b10);
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
				TPixelObj();										// ROM 0x0003e7b8 __ct__9TPixelObjFv
				~TPixelObj();										// ROM 0x0003e80c __dt__9TPixelObjFv
	void		Init(RefArg picture);								// ROM 0x0003f614 Init__9TPixelObjFRC6RefVar
	void		Init(RefArg picture, Boolean withMask);				// ROM 0x00040f28 Init__9TPixelObjFRC6RefVarUc
	Ref			GetFramBitmap(void);								// ROM 0x000410ac GetFramBitmap__9TPixelObjFv
	void		FramBitMapToPixMap(const FramBitmap& bits, PixelMap* map);	// ROM 0x00041448 FramBitMapToPixMap__9TPixelObjFRC10FramBitmap (see Pictures.cpp: the two ROMs differ)

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
void	DrawPicture(RefArg picture, const Rect& box, ULong justify, long mode);	// a bitmap frame drawn in the box, justified

// Asking a bitmap about a point, from its own origin: whether it is in
// the picture, or (wantsPixel) the pixel's value, -1 outside the mask.
Ref		PtInPicture(RefArg x, RefArg y, RefArg picture, Boolean wantsPixel);	// ROM 0x0003f3f0 PtInPicture__FRC6RefVarN21Uc

Ref		FPtInPicture(RefArg rcvr, RefArg x, RefArg y, RefArg picture);		// ROM 0x0003f3c0 FPtInPicture__FRC6RefVarN31
Ref		FGetBitmapPixel(RefArg rcvr, RefArg x, RefArg y, RefArg picture);	// ROM 0x0003f3d8 FGetBitmapPixel__FRC6RefVarN31

void	RegisterPictureNatives(void);

// A 'pixels binary of that size: a PixelMap header with the rows after
// it, the map's baseAddr being the offset to them.
Ref		MakePixelsObject(const Rect& bounds, long depth, long rowBytes,
						 long hRes, long vRes, RefArg store, RefArg compander,
						 RefArg companderData);				// ROM 0x000415a4 MakePixelsObject__FR5TRectlN32RC6RefVarN26

void	RegisterBitmapNatives(void);					// MakeBitmap (Pictures.cpp)

// MungeBitmap (MungeBitmap.cpp): a bitmap turned or flipped in place
Boolean	Tilable(PixelMap* pm);									// ROM 0x00040ee0 Tilable__FP8PixelMap - the size of the whole screen
Ref		RotBitmap180(RefArg bitmap, RefArg options);			// ROM 0x0003f93c RotBitmap180__FRC6RefVarT1
Ref		FlipBitmapH(RefArg bitmap);								// ROM 0x0003fbf8 FlipBitmapH__FRC6RefVar
Ref		FlipBitmapV(RefArg bitmap);								// ROM 0x0003fdc0 FlipBitmapV__FRC6RefVar
Ref		RotBitmapL(RefArg bitmap, RefArg options);				// ROM 0x0003fec0 RotBitmapL__FRC6RefVarT1
Ref		RotBitmapR(RefArg bitmap, RefArg options);				// ROM 0x0004056c RotBitmapR__FRC6RefVarT1
Ref		FMungeBitmap(RefArg rcvr, RefArg bitmap, RefArg operation, RefArg options);	// ROM 0x0003f764 FMungeBitmap
void	RegisterMungeBitmapNatives(void);
Ref		FMakeBitmap(RefArg rcvr, RefArg width, RefArg height, RefArg options);	// ROM 0x0004173c FMakeBitmap

#endif	/* __PICTURES_H */
