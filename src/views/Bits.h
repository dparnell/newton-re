/*
	File:		views/Bits.h

	Contains:	TBits: an offscreen pixel map of the screen's depth over a
				rectangle - the bits under the caret, the dragged image, the
				animation effects' sprites.  A TBits is a PixelMap (its bits
				in a handle it owns, or another map's) with the drawing
				helpers: CopyFromScreen takes the current port's pixels of a
				rectangle, Draw puts them back (or elsewhere), Fill paints
				the bits with a word.  The ROM's object is 0x34 bytes: the
				PixelMap, +0x1c the TBitsPort drawn into (BeginDrawing/
				EndDrawing, NOT YET RECONSTRUCTED), +0x20 whether it was
				drawn from/to the screen, +0x21 whether it owns its bits,
				+0x24 an exception cleanup for a stack instance (the host's
				destructor does).

	Reconstructed from the MP2100 D ROM (0x00042b7c-0x00045bcc, with
	TAnimate and DragBits); each function cites its origin.
*/

#ifndef __BITS_H
#define __BITS_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif
#include "Ports.h"

class TBits : public PixelMap
{
public:
				TBits();										// ROM 0x00042b84 __ct__5TBitsFv
				~TBits();										// ROM 0x000459fc __dt__5TBitsFv
	Boolean		Constructor(const Rect& bounds);				// ROM 0x0004388c Constructor__5TBitsFRC5TRect (bits of the screen's depth for the rectangle)
	void		Constructor(const PixelMap& map);				// ROM 0x0004526c Constructor__5TBitsFRC8PixelMap (over another map's bits)
	void		Cleanup(void);									// ROM 0x00045a3c Cleanup__5TBitsFv
	void		SetBounds(const Rect& bounds);					// ROM 0x00045a84 SetBounds__5TBitsFRC5TRect
	void		CopyFromScreen(const Rect& src, const Rect& dst, long mode, RgnHandle mask);	// ROM 0x00042c5c
	void		Draw(const Rect& src, const Rect& dst, long mode, RgnHandle mask);	// ROM 0x00042cac
	void		Draw(const Rect& dst, long mode, RgnHandle mask);	// ROM 0x00042c00: the whole map
	void		CopyIntoBitmap(PixelMap* map, long mode, RgnHandle mask);	// ROM 0x00042c3c
	void		Fill(long pattern);								// ROM 0x00042d14 Fill__5TBitsFl
	static long	InitBitMap(const Rect& bounds, PixelMap* map);	// ROM 0x00042e84 InitBitMap__5TBitsSFRC5TRectP8PixelMap (==> the bits' size)

	void*		fPort;			// +0x1c  the TBitsPort (NOT YET)
	Boolean		fDrawn;			// +0x20  drawn from or to the screen
	Boolean		fOwnsBits;		// +0x21  the bits are a handle of ours
};

#endif	/* __BITS_H */
