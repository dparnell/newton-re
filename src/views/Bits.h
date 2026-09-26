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
#include "RegionVars.h"

class TBits;
class TView;

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

// A 1-bit (or any depth's) pixel map over the rectangle whose bits are a
// new handle; ==> whether there was the memory.
Boolean	InitBitMap(PixelMap* map, const Rect& bounds, long depth, long hRes, long vRes);	// ROM 0x000414e0 InitBitMap__FP8PixelMapRC5TRectlN23


// What a drag moves about the screen: the data being dragged drawn into
// bits of its own (the source view's DrawDragData) and, beside it, the
// screen as it is under the rectangle - as it would be without the data,
// when the drag is a move, the view being drawn without its selection and
// the data taken out of the picture.  The clip of the port it was made in
// is kept and put back when the bits go, a Throw included.  The ROM's
// object is 0x80 bytes.
class DragBits
{
public:
				DragBits();										// ROM 0x000429a0 __ct__8DragBitsFv
				DragBits(TView* view, const Rect* bounds, Boolean copy);	// ROM 0x000428d4 __ct__8DragBitsFP5TViewPC5TRectUc
				~DragBits();									// ROM 0x00042938 __dt__8DragBitsFv
	void		Constructor(TView* view, const Rect* bounds, Boolean copy);	// ROM 0x0004266c Constructor__8DragBitsFP5TViewPC5TRectUc

	TRegionStruct	fSavedClip;		// +0x00  the port's clip when the bits were made
	TBits			fDataBits;		// +0x04  the dragged data
	TBits			fBackground;	// +0x38  the screen under it
	ExceptionCleanup	fCleanup;	// +0x6c  DisposeDragBits when a Throw unwinds
	Boolean			fConstructed;	// +0x7c  Constructor has run (the clip to put back)
};

void	DisposeDragBits(void* bits);							// ROM 0x000425ac DisposeDragBits__FPv

#endif	/* __BITS_H */
