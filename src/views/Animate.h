/*
	File:		views/Animate.h

	Contains:	The view effects: TAnimate, which shows or hides a view
				(or slides, trashes, poofs one) with the animation its
				viewEffect asks for, and TSaveScreenBits, the screen's
				pixels under an effect kept to be put back between its
				steps.

				An effect is set up before the view changes (Setup...Effect:
				the sprite - a TBits of the view's image - is made from the
				screen and the view drawn into it where other views cover
				it) and run after (DoEffect: the screen redrawn without the
				view, its pixels saved, then the sprite drawn step by step
				over them - MultiEffect for the plain effects and the
				slides, CrumpleEffect for the trash, PoofEffect for the
				cloud - and the area validated).  The noFX preference is a
				bit mask of the kinds not animated.

				The viewEffect word, as MultiEffect reads it:
				  bits 0-4    the columns less one (NTK fxColumnsMask)
				  bits 5-9    the rows less one (fxRowsMask)
				  bit 10, 11  the horizontal/vertical start phase: the
				              cells drawn from their right/bottom edge
				              (fxHStartPhase, fxVStartPhase)
				  bit 12, 13  the phase alternates from cell to cell along
				              a row (fxHAlternatePhase, fxVAlternatePhase)
				  bit 14, 15  the phase alternates from row to row
				  bit 16, 17  the cells move horizontally/vertically
				              (fxMoveH, fxMoveV): the image slides in or
				              out instead of being wiped
				  bit 18      a line drawn along the moving edge, the
				              view's frame pen wide (fxRevealLine)
				  bit 19      the effect run the other way (the phases
				              flipped, the image moved rather than wiped)
				  bits 21-24  the steps (0: 3, else n + 1; fxSteps)
				  bits 25-28  the ticks a step takes (0: 3; fxStepTime)
				The ROM's TAnimate is 0xbc bytes: the TBits sprite, the
				saved screen bits at +0x34, the mask region +0x60, the
				bounds +0x64, the start bounds +0x6c, the saved area +0x74,
				the part drawn from the view +0x7c and its origin +0x84,
				the view +0x88, the kind +0x8c, the slide offsets +0x90/
				+0x94, the cell limit +0x98, the effect word +0x9c, the
				context +0xa0, the reverse and has-bits flags +0xa4/+0xa5,
				the enabled kinds +0xa8 and an exception cleanup +0xac (the
				host's destructor).  NOT YET RECONSTRUCTED: gSlowMotion.

	Reconstructed from the MP2x00 US ROM (0x000429ec-0x0004512c,
	0x001c60b0-0x001c6384); each function cites its origin.
*/

#ifndef __ANIMATE_H
#define __ANIMATE_H

#ifndef __BITS_H
#include "Bits.h"
#endif
#ifndef __REGIONVARS_H
#include "RegionVars.h"
#endif
#ifndef __FRAMES_H
#include "Frames.h"
#endif

class TView;

// the screen's pixels of a rectangle kept in a pointer block; the ROM's
// object is 0x2c bytes: the PixelMap (baseAddr the block) and an
// exception cleanup (the host's destructor)
class TSaveScreenBits : public PixelMap
{
public:
				TSaveScreenBits();								// ROM 0x001c60b8 __ct__15TSaveScreenBitsFv
				~TSaveScreenBits();								// ROM 0x001c610c __dt__15TSaveScreenBitsFv
	Boolean		AllocateBuffers(Rect* bounds);					// ROM 0x001c6148 AllocateBuffers__15TSaveScreenBitsFP4Rect (nil: the screen; ==> whether there was memory)
	void		SaveScreenBits(void);							// ROM 0x001c62c0 SaveScreenBits__15TSaveScreenBitsFv
	void		RestoreScreenBits(Rect* bounds, RgnHandle mask);	// ROM 0x001c62f8 RestoreScreenBits__15TSaveScreenBitsFP4RectPP6Region
};


class TAnimate : public TBits
{
public:
	enum EffectKind
	{
		kPlainEffect = 0,		// Show/Hide, :Effect(): the viewEffect's cells
		kTrashEffect = 1,		// :Delete(): the crumple into the trash
		kSlideEffect = 2,		// SyncScroll, :SlideEffect(), :RevealEffect()
		kPoofEffect = 3			// a scrub: the cloud
	};

				TAnimate();										// ROM 0x00042a70 __ct__8TAnimateFv
				~TAnimate();									// ROM 0x00042b34 __dt__8TAnimateFv

	void		SetupPlainEffect(TView* view, Boolean showing, long effect);				// ROM 0x00042b90 SetupPlainEffect__8TAnimateFP5TViewUcl
	void		SetupSlideEffect(TView* view, const Rect& bounds, long distance, long direction);	// ROM 0x00042d30 SetupSlideEffect__8TAnimateFP5TViewRC5TRectlT3
	void		SetupTrashEffect(TView* view);					// ROM 0x00043010 SetupTrashEffect__8TAnimateFP5TView
	void		SetupPoofEffect(TView* view, const Rect& bounds);	// ROM 0x0004312c SetupPoofEffect__8TAnimateFP5TViewRC5TRect
	void		SetupDragEffect(TView* view);					// ROM 0x00045118 SetupDragEffect__8TAnimateFP5TView
	void		DoEffect(RefArg sound);							// ROM 0x000436dc DoEffect__8TAnimateFRC6RefVar

	// (the drag draws the sprite itself)
	TSaveScreenBits&	SavedBits(void)			{ return fSavedBits; }
	RgnHandle			Mask(void)				{ return fMask; }
	const Rect&			Bounds(void) const		{ return fBounds; }

private:
	void		PreSetup(TView* view, EffectKind kind);			// ROM 0x00043254 PreSetup__8TAnimateFP5TViewQ28TAnimate10EffectKind
	void		PostSetup(const Rect& bounds, const Rect& from, const Rect& to);	// ROM 0x0004336c PostSetup__8TAnimateFRC5TRectN21
	void		MultiEffect(RefArg sound);						// ROM 0x000438e4 MultiEffect__8TAnimateFRC6RefVar
	void		CrumpleSprite(Rect* crumpleBox, Rect* spriteBox);	// ROM 0x00044488 CrumpleSprite__8TAnimateFP5TRectT1
	void		CrumpleEffect(void);							// ROM 0x000449c8 CrumpleEffect__8TAnimateFv
	void		PoofEffect(void);								// ROM 0x00044ffc PoofEffect__8TAnimateFv
	void		Disabled(TView* view);							// the kind not animated: the view and its context noted, nothing else

	TSaveScreenBits	fSavedBits;		// +0x34  the screen under fSaveBounds
	TRegionStruct	fMask;			// +0x60  what is in front of the view and off the screen; MultiEffect turns it into what it may draw on
	Rect		fBounds;			// +0x64  the effect's area: the view's bounds (the sprite's bounds), within the port
	Rect		fStartBounds;		// +0x6c  where the image starts (empty: it comes from the view)
	Rect		fSaveBounds;		// +0x74  the screen saved and put back between steps (empty: nothing)
	Rect		fDrawBounds;		// +0x7c  the part of the sprite drawn from the view itself (empty: all from the screen)
	Point		fDrawOrigin;		// +0x84  the origin the view is drawn at into the sprite
	TView*		fView;				// +0x88
	EffectKind	fKind;				// +0x8c
	long		fOffsetV;			// +0x90  a slide: the height the cells end at
	long		fOffsetH;			// +0x94  a slide: the width the cells end at
	long		fLimit;				// +0x98  the tallest a moving cell may be (0x7ffe: no limit)
	long		fEffect;			// +0x9c  the viewEffect word
	RefStruct	fContext;			// +0xa0  the view's context (the sounds are its proto variables)
	Boolean		fReverse;			// +0xa4  the cells grow (showing) rather than shrink (hiding)
	Boolean		fHasBits;			// +0xa5  the sprite was made
	ULong		fEnabled;			// +0xa8  a bit per EffectKind: ~ the noFX preference
};

void	PlaySound(RefArg context, RefArg sound);				// ROM 0x000429ec PlaySound__FRC6RefVarT1 - a symbol looked up in the context and played
void	TrimRect(const Rect& a, const Rect& b, Rect* result);	// ROM 0x00043290 TrimRect__FRC5TRectT1P5TRect - a less the rows of b (b empty: a)

#endif	/* __ANIMATE_H */
