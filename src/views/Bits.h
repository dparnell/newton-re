/*
	File:		views/Bits.h

	Contains:	TBits: an offscreen pixel map of the screen's depth over a
				rectangle - the bits under the caret, the dragged image, the
				animation effects' sprites.  A TBits is a PixelMap (its bits
				in a handle it owns, or another map's) with the drawing
				helpers: CopyFromScreen takes the current port's pixels of a
				rectangle, Draw puts them back (or elsewhere), Fill paints
				the bits with a word; BeginDrawing/EndDrawing make the bits
				the current port (a TBitsPort: a GrafPort over the map with
				the origin asked for, the map cleared first unless it was
				drawn from the screen) so a view can be drawn into them.
				The ROM's object is 0x34 bytes: the PixelMap, +0x1c the
				TBitsPort drawn into, +0x20 whether it was drawn from/to
				the screen, +0x21 whether it owns its bits, +0x24 an
				exception cleanup for a stack instance (the host's
				destructor does).

	Reconstructed from the MP2x00 US ROM (0x000422ac-0x000452fc, with
	TAnimate and DragBits); each function cites its origin.
*/

#ifndef __BITS_H
#define __BITS_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif
#include "Ports.h"

class TBits;

// a port over a TBits' map for the time a view is drawn into it; the
// ROM's object is 0xc bytes
class TBitsPort
{
public:
	void		Constructor(TBits* bits, Point origin, Boolean fill);	// ROM 0x00042484 Constructor__9TBitsPortFP5TBits6TPointUc
				~TBitsPort();									// ROM 0x0004256c __dt__9TBitsPortFv

	TBits*		fBits;			// +0x00
	GrafPort*	fPort;			// +0x04  the port over the bits
	GrafPort*	fSavedPort;		// +0x08  the port current before
};

class TBits : public PixelMap
{
public:
				TBits();										// ROM 0x000422b4 __ct__5TBitsFv
				~TBits();										// ROM 0x0004512c __dt__5TBitsFv
	Boolean		Constructor(const Rect& bounds);				// ROM 0x00042fbc Constructor__5TBitsFRC5TRect (bits of the screen's depth for the rectangle)
	void		Constructor(const PixelMap& map);				// ROM 0x0004499c Constructor__5TBitsFRC8PixelMap (over another map's bits)
	void		Cleanup(void);									// ROM 0x0004516c Cleanup__5TBitsFv
	void		SetBounds(const Rect& bounds);					// ROM 0x000451b4 SetBounds__5TBitsFRC5TRect
	void		CopyFromScreen(const Rect& src, const Rect& dst, long mode, RgnHandle mask);	// ROM 0x00042c5c
	void		Draw(const Rect& src, const Rect& dst, long mode, RgnHandle mask);	// ROM 0x00042cac
	void		Draw(const Rect& dst, long mode, RgnHandle mask);	// ROM 0x00042c00: the whole map
	void		CopyIntoBitmap(PixelMap* map, long mode, RgnHandle mask);	// ROM 0x00042c3c
	void		Fill(long pattern);								// ROM 0x00042444 Fill__5TBitsFl
	void		BeginDrawing(Point origin);						// ROM 0x000451c4 BeginDrawing__5TBitsF6TPoint (the bits made the current port)
	void		EndDrawing(void);								// ROM 0x0004527c EndDrawing__5TBitsFv
	void		SetPort(void);									// ROM 0x0004242c SetPort__5TBitsFv (the bits' port current)
	void		RestorePort(void);								// ROM 0x00042438 RestorePort__5TBitsFv (the port before)
	static long	InitBitMap(const Rect& bounds, PixelMap* map);	// ROM 0x000425b4 InitBitMap__5TBitsSFRC5TRectP8PixelMap (==> the bits' size)

	TBitsPort*	fPort;			// +0x1c  the port while drawing into the bits
	Boolean		fDrawn;			// +0x20  drawn from or to the screen
	Boolean		fOwnsBits;		// +0x21  the bits are a handle of ours
};

#endif	/* __BITS_H */
