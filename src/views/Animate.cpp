/*
	File:		views/Animate.cpp

	Contains:	The view effects.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Animate.h"
#include "Inker.h"			// BusyBoxSend
#include "SoundSettings.h"	// FPlaySound
#include "View.h"
#include "RootView.h"
#include "Rects.h"
#include "Regions.h"
#include "Ports.h"
#include "Draw.h"
#include "Shapes.h"
#include "Polygons.h"
#include "Pictures.h"
#include "Screen.h"
#include "Locale.h"
#include "ObjectHeap.h"
#include "ROMConstants.h"
#include "NewtonTime.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"
#include "objects.h"
#include <string.h>


/*------------------------------------------------------------------------------
	T S a v e S c r e e n B i t s
------------------------------------------------------------------------------*/

// ROM 0x001c60b8 __ct__15TSaveScreenBitsFv
// No bits yet (a stack instance registers its cleanup: the host's
// destructor).
TSaveScreenBits::TSaveScreenBits()
{
	baseAddr = nil;
}


// ROM 0x001c610c __dt__15TSaveScreenBitsFv
TSaveScreenBits::~TSaveScreenBits()
{
	if (baseAddr != nil)
		DisposPtr(baseAddr);
}


// ROM 0x001c6148 AllocateBuffers__15TSaveScreenBitsFP4Rect
// A pointer block for the rectangle's pixels at the screen's depth (nil:
// the whole screen), the map set over it (a pointer map, 72 dpi); ==>
// whether there was memory (the out-of-memory exception caught).
Boolean
TSaveScreenBits::AllocateBuffers(Rect* r)
{
	Boolean allocated = true;
	newton_try
	{
		Rect screen;
		if (r == nil)
		{
			screen = qdGlobals.fScreenBits.bounds;
			r = &screen;
		}
		long depth = PixelMapDepth(&qdGlobals.fScreenBits);
		if (depth == 0)
			depth = PixelMapDepth(&GetCurrentPort()->portBits);		// host: no screen (a test drawing offscreen)
		rowBytes = (short) (depth * (((r->right - r->left) + 31 & ~31) >> 3));
		baseAddr = NewPtr(rowBytes * (r->bottom - r->top));
		if (baseAddr == nil)
			OutOfMemory();
		bounds = *r;
		pixMapFlags = kPixMapPtr + depth;
		deviceRes.h = kDefaultDPI;
		deviceRes.v = kDefaultDPI;
		grayTable = nil;
	}
	newton_catch(exRootException)
	{
		allocated = false;
	}
	end_try;
	return allocated;
}


// ROM 0x001c62c0 SaveScreenBits__15TSaveScreenBitsFv
// The current port's pixels of our bounds copied in.
void
TSaveScreenBits::SaveScreenBits(void)
{
	CopyBits(&GetCurrentPort()->portBits, this, &bounds, &bounds, srcCopy, nil);
}


// ROM 0x001c62f8 RestoreScreenBits__15TSaveScreenBitsFP4RectPP6Region
// Our pixels of the rectangle (cut down to our bounds when it reaches
// outside them) copied back to the current port, through the mask.
void
TSaveScreenBits::RestoreScreenBits(Rect* r, RgnHandle mask)
{
	Point topLeft;
	topLeft.v = r->top;
	topLeft.h = r->left;
	if (!(PtInRect(topLeft, &bounds) && r->bottom <= bounds.bottom && r->right <= bounds.right))
		SectRect(r, &bounds, r);
	CopyBits(this, &GetCurrentPort()->portBits, r, r, srcCopy, mask);
}


/*------------------------------------------------------------------------------
	T A n i m a t e
------------------------------------------------------------------------------*/

// ROM 0x000429ec PlaySound__FRC6RefVarT1
// A sound played for the context: a symbol is looked up as its proto
// variable; nil plays nothing; the sound played if the preferences allow
// (FPlaySound).
void
PlaySound(RefArg context, RefArg sound)
{
	RefVar theSound(sound);
	if (IsSymbol(sound))
		theSound = GetProtoVariable(context, sound, nil);
	if (NOTNIL(theSound))
		FPlaySound(context, theSound);
}


// ROM 0x00043290 TrimRect__FRC5TRectT1P5TRect
// The rows of a not covered by b: a when b is empty, nothing when a is;
// else a's columns from where b ends (when b starts at or above a) down
// to where b starts (when a ends at or above b) - the same rectangle
// answers an inverted, empty one.
void
TrimRect(const Rect& a, const Rect& b, Rect* result)
{
	if (EmptyRect(&b))
	{
		*result = a;
		return;
	}
	if (EmptyRect(&a))
	{
		SetEmptyRect(result);
		return;
	}
	result->top = (b.top <= a.top) ? b.bottom : a.top;
	result->bottom = (a.bottom <= b.bottom) ? b.top : a.bottom;
	result->left = a.left;
	result->right = a.right;
}


// ROM 0x00042a70 __ct__8TAnimateFv
// The parts constructed, the context nil, no sprite; the kinds enabled
// are all but the noFX preference's bits (a stack instance registers its
// cleanup: the host's destructor).
TAnimate::TAnimate()
{
	fContext = NILREF;
	fHasBits = false;
	RefVar noFX(GetPreference(RSSYMnofx));
	fEnabled = ISNIL(noFX) ? (ULong) -1 : ~(ULong) RINT(noFX);
}


// ROM 0x00042b34 __dt__8TAnimateFv
TAnimate::~TAnimate()
{
}


// the kind not enabled (or the view has no effect): only the view and
// its context are kept, for the sound
void
TAnimate::Disabled(TView* view)
{
	fView = view;
	fContext = view->fContext;
}


// ROM 0x00043254 PreSetup__8TAnimateFP5TViewQ28TAnimate10EffectKind
// The view and the kind noted, the slide offsets none, the limit none,
// the context the view's.
void
TAnimate::PreSetup(TView* view, EffectKind kind)
{
	fView = view;
	fKind = kind;
	fReverse = false;
	fOffsetV = 0;
	fOffsetH = 0;
	fLimit = 0x7ffe;
	fContext = view->fContext;
}


// ROM 0x0004336c PostSetup__8TAnimateFRC5TRectN21
// The sprite made for bounds (the effect's area), from (where the image
// starts: empty when it is drawn from the view) and to (the sprite's
// size): the screen saved under the rows of bounds not covered by from;
// the sprite's bits allocated for to (out of memory throws); the mask
// what is in front of the view and its ancestors below the root, and
// everything off the screen.  With from given and bounds the sprite's
// size, the whole image is drawn from the view later (fDrawBounds);
// else the sprite is copied from the screen (the caret's bits put back
// first when it lies in the way) and the view drawn into it where other
// views cover it, the rows of to outside bounds left to be drawn from
// the view.  The sprite's bounds are then bounds.
void
TAnimate::PostSetup(const Rect& bounds, const Rect& from, const Rect& to)
{
	fStartBounds = from;
	TrimRect(bounds, from, &fSaveBounds);
	fHasBits = !EmptyRect(&bounds);
	if (!fHasBits)
		return;
	fHasBits = TBits::Constructor(to);
	if (!fHasBits)
		OutOfMemory();
	{
		TRegion front(fView->GetFrontMask());
		fMask = front;
	}
	for (TView* parent = fView->fParent; parent != gRootView; parent = parent->fParent)
	{
		TRegion mask(parent->GetFrontMask());
		TRegionVar rgn(mask);
		UnionRgn(fMask, rgn, fMask);
	}
	Rect screen;
	SetRect(&screen, 0, 0, screenWidth, screenHeight);
	{
		TRectangularRegion screenRgn(screen);
		DiffRgn(screenRgn, fMask, fMask);
	}
	{
		Rect everywhere;
		SetRect(&everywhere, -32767, -32767, 32766, 32766);
		TRectangularRegion everywhereRgn(everywhere);
		DiffRgn(everywhereRgn, fMask, fMask);
	}
	if (!EmptyRect(&from) && EqualRect(&bounds, &to))
		fDrawBounds = from;
	else
	{
		TRegionVar covered;
		{
			TRectangularRegion boundsRgn(bounds);
			SectRgn(fMask, boundsRgn, covered);
		}
		Rect onScreen;
		SectRect(&bounds, &screen, &onScreen);
		Rect caret;
		gRootView->GetCaretRect(&caret);
		SectRect(&to, &caret, &caret);
		if (!EmptyRect(&caret))
		{
			gRootView->RestoreBitsUnderCaret();
			gRootView->DirtyCaret();
		}
		CopyFromScreen(onScreen, onScreen, srcCopy, nil);
		if (!EmptyRgn(covered))
		{
			BeginDrawing(MakePoint(to.left, to.top));
			gSkipVisRegions = true;
			fView->Update(covered, nil);
			gSkipVisRegions = false;
			EndDrawing();
		}
		TrimRect(to, bounds, &fDrawBounds);
	}
	fDrawOrigin.v = fDrawBounds.top;
	fDrawOrigin.h = fDrawBounds.left;
	SetBounds(fBounds);
}


// ROM 0x00042b90 SetupPlainEffect__8TAnimateFP5TViewUcl
// The plain effect of a view about to be shown (showing) or hidden: the
// area its outer bounds cut to the port's rectangle when that is
// smaller, the effect word given or the view's viewEffect (none: no
// effect), showing keeps the whole area as the start (the image drawn
// from the view), hiding starts from nothing (the image copied from the
// screen).
void
TAnimate::SetupPlainEffect(TView* view, Boolean showing, long effect)
{
	if ((fEnabled & (1 << kPlainEffect)) == 0)
	{
		Disabled(view);
		return;
	}
	view->OuterBounds(&fBounds);
	GrafPort* port;
	GetPort(&port);
	if (port->portRect.bottom - port->portRect.top < fBounds.bottom - fBounds.top
	 || port->portRect.right - port->portRect.left < fBounds.right - fBounds.left)
		SectRect(&port->portRect, &fBounds, &fBounds);
	PreSetup(view, kPlainEffect);
	if (effect == 0)
	{
		RefVar viewEffect(view->GetProto(RSSYMvieweffect));
		fEffect = ISINT(viewEffect) ? RINT(viewEffect) : 0;
	}
	else
		fEffect = effect;
	if (fEffect == 0)
		return;
	fReverse = showing;
	Rect from;
	if (showing)
		from = fBounds;
	else
		SetEmptyRect(&from);
	PostSetup(fBounds, from, fBounds);
}


// ROM 0x00042d30 SetupSlideEffect__8TAnimateFP5TViewRC5TRectlT3
// The contents of bounds slid by distance (down or right when positive):
// direction > 0 slides new contents in from the far edge (the area grown
// by the distance on that side, the sprite ending short of it), 0 slides
// the old contents out (the area grown, the cells no taller than the
// height, the image drawn from the view a distance on), < 0 slides them
// out with the image coming from a distance before.  The effect moves
// the cells vertically, from the bottom when the distance and the
// direction disagree.
void
TAnimate::SetupSlideEffect(TView* view, const Rect& bounds, long distance, long direction)
{
	if ((fEnabled & (1 << kSlideEffect)) == 0)
	{
		Disabled(view);
		return;
	}
	Rect to = bounds;
	fBounds = bounds;
	PreSetup(view, kSlideEffect);
	long height = bounds.bottom - bounds.top;
	fOffsetV = height;
	fReverse = direction >= 0;
	if (direction > 0)
	{
		if (distance <= 0)
		{
			fBounds.top += distance;
			to.bottom -= distance;
		}
		else
		{
			fBounds.bottom += distance;
			to.top -= distance;
		}
	}
	Rect from = fBounds;
	if (direction == 0)
	{
		fLimit = height;
		if (distance <= 0)
		{
			fBounds.top += distance;
			to.bottom -= distance;
		}
		else
		{
			fBounds.bottom += distance;
			to.top -= distance;
		}
	}
	else if (direction < 0)
	{
		fOffsetV -= distance;
		if (distance <= 0)
			from.bottom += distance;
		else
			from.top += distance;
	}
	fEffect = ((distance > 0) == (direction >= 0)) ? 0x20000 : 0x20800;		// fxMoveV, and fxVStartPhase when they disagree
	PostSetup(bounds, from, to);
	if (direction == 0)
	{
		OffsetRect(&fDrawBounds, 0, distance);
		fDrawOrigin.v = fBounds.top;
		fDrawOrigin.h = fBounds.left;
	}
	else
	{
		fDrawBounds = fStartBounds;
		fDrawOrigin.v = fBounds.bottom - (from.bottom - from.top);
		fDrawOrigin.h = fBounds.left;
	}
}


// ROM 0x00043010 SetupTrashEffect__8TAnimateFP5TView
// The view crumpled into the trash: its outer bounds (no lower than the
// screen), the image from the screen; the area saved reaches from at
// most 72 pixels above the view's bottom to the screen's bottom right,
// where the trash is drawn and the ball falls.
void
TAnimate::SetupTrashEffect(TView* view)
{
	if ((fEnabled & (1 << kTrashEffect)) == 0)
	{
		Disabled(view);
		return;
	}
	view->OuterBounds(&fBounds);
	if (fBounds.bottom > screenHeight)
		fBounds.bottom = (short) screenHeight;
	PreSetup(view, kTrashEffect);
	Rect from;
	SetEmptyRect(&from);
	PostSetup(fBounds, from, fBounds);
	if (fSaveBounds.bottom - fSaveBounds.top < 72)
	{
		long top = fSaveBounds.bottom - 72;
		fSaveBounds.top = (short) (top > 0 ? top : 0);
	}
	fSaveBounds.bottom = (short) screenHeight;
	fSaveBounds.right = (short) screenWidth;
}


// ROM 0x0004312c SetupPoofEffect__8TAnimateFP5TViewRC5TRect
// A cloud of at least 89 x 54 centred on the bounds when they are smaller.
void
TAnimate::SetupPoofEffect(TView* view, const Rect& bounds)
{
	if ((fEnabled & (1 << kPoofEffect)) == 0)
	{
		Disabled(view);
		return;
	}
	fBounds = bounds;
	if (bounds.right - bounds.left < 89 || bounds.bottom - bounds.top < 54)
	{
		fBounds.left = (short) ((bounds.left + bounds.right) / 2 - 44);
		fBounds.right = (short) (fBounds.left + 89);
		fBounds.top = (short) ((bounds.top + bounds.bottom) / 2 - 27);
		fBounds.bottom = (short) (fBounds.top + 54);
	}
	PreSetup(view, kPoofEffect);
	Rect from;
	SetEmptyRect(&from);
	PostSetup(fBounds, from, fBounds);
}


// ROM 0x00045118 SetupDragEffect__8TAnimateFP5TView
// The sprite of a view about to be dragged: every kind enabled, the
// plain setup of a view being hidden (the image from the screen) with
// an effect word of its own (fxMoveV + fxVStartPhase; the drag never
// runs it).
void
TAnimate::SetupDragEffect(TView* view)
{
	fEnabled = (ULong) -1;
	SetupPlainEffect(view, false, 0x20800);
}


// ROM 0x000436dc DoEffect__8TAnimateFRC6RefVar
// The effect run, the view having changed: with the kind disabled the
// view's bounds are invalidated and the root view updated (no effect);
// without a sprite, or no memory for the screen bits, only the sound is
// played.  Else, drawing: when there is an area to save, the view's
// bounds are validated when the image starts somewhere (the view is not
// to be drawn by the update), the root view updated (the screen without
// the view), the caret's bits put back when it lies in the area, and the
// screen saved; the caret is dirtied when it lies in the effect's area;
// then the effect of the kind (the plain and slide effects MultiEffect,
// the trash CrumpleEffect, the poof PoofEffect).  In slow motion
// (gSlowMotion) the screen is not held for the drawing and the effect
// itself is left out - the view simply appears or goes.
void
TAnimate::DoEffect(RefArg sound)
{
	if ((fEnabled & (1 << fKind)) == 0 && fView != nil)
	{
		gRootView->SmartInvalidate(fView->viewBounds);
		gRootView->Update(nil);
	}
	Boolean saving = !EmptyRect(&fSaveBounds);
	if (!fHasBits || (saving && !fSavedBits.AllocateBuffers(&fSaveBounds)))
	{
		if (NOTNIL(fContext))
			PlaySound(fContext, sound);
		return;
	}
	if (gSlowMotion == 0)
		StartDrawing(nil, nil);
	Rect caret;
	gRootView->GetCaretRect(&caret);
	if (saving)
	{
		if (!EmptyRect(&fStartBounds))
		{
			TRectangularRegion viewRgn(fView->viewBounds);
			gRootView->Validate(viewRgn);
		}
		gRootView->Update(nil);
		Rect caretInArea;
		SectRect(&fSaveBounds, &caret, &caretInArea);
		if (!EmptyRect(&caretInArea))
			gRootView->RestoreBitsUnderCaret();
		fSavedBits.SaveScreenBits();
	}
	SectRect(&fBounds, &caret, &caret);
	if (!EmptyRect(&caret))
		gRootView->DirtyCaret();
	if (gSlowMotion != 0)
		return;
	switch (fKind)
	{
	case kPlainEffect:
	case kSlideEffect:
		MultiEffect(sound);
		break;
	case kTrashEffect:
		CrumpleEffect();
		break;
	case kPoofEffect:
		PoofEffect();
		break;
	}
	StopDrawing(nil, nil);
}


// ROM 0x000438e4 MultiEffect__8TAnimateFRC6RefVar
// The cells of the effect word drawn step by step (the word's fields
// are described in Animate.h).  The image is first completed from the
// view (the part in fDrawBounds).  The clip is the view's region (its
// clipper's, when not a plain rectangle) or the effect's area, less the
// mask.  Each step: the cells drawn last are put back from the saved
// screen; then, unless this is the last step of a plain hide (the screen
// is right already), every cell - a row's height and a column's width,
// or, when the cells move, a part that shrinks (hiding) or grows
// (showing) with the step towards the slide offset, no taller than the
// limit (the rest shifted) - is drawn from the sprite: from its own
// place, or from the opposite edge of the cell when the phase says so
// and the cell moves, so the image slides; a plain effect draws the
// source on the other phase from the destination (a wipe from the
// image's edge); the reveal line is drawn along the moving edge, the
// frame's pen wide.  The phases alternate along a row and from row to
// row as the word asks.  Between steps the screen is released for the
// step's ticks.  The clip restored and the pen put back even on a throw;
// at the end the mask within the sprite is validated in the root view.
// The busy box is held off while each step is drawn (0x35) and let go
// after it (0x36).
void
TAnimate::MultiEffect(RefArg sound)
{
	long effect = fEffect;
	long columns = (effect & 0x1f) + 1;
	long rows = ((effect & 0x3e0) >> 5) + 1;
	long hPhase = (effect >> 10) & 1;
	long vPhase = (effect >> 11) & 1;
	long hAlternate = (effect >> 12) & 1;
	long vAlternate = (effect >> 13) & 1;
	long hAlternateRow = (effect >> 14) & 1;
	long vAlternateRow = (effect >> 15) & 1;
	Boolean moveH = ((effect >> 16) & 1) != 0;
	Boolean moveV = ((effect >> 17) & 1) != 0;
	Boolean revealLine = ((effect >> 18) & 1) != 0;
	long reverse = (effect >> 19) & 1;
	long steps = (effect & 0x1e00000) >> 21;
	steps = (steps == 0) ? 3 : steps + 1;
	long stepTicks = (effect & 0x1e000000) >> 25;
	if (stepTicks == 0)
		stepTicks = 3;
	Boolean saved = fSavedBits.baseAddr != nil;

	if (!EmptyRect(&fDrawBounds))
	{
		BeginDrawing(fDrawOrigin);
		gSkipVisRegions = true;
		TRectangularRegion drawRgn(fDrawBounds);
		fView->Update(drawRgn, nil);
		gSkipVisRegions = false;
		EndDrawing();
	}
	long width = fBounds.right - fBounds.left;
	long height = fBounds.bottom - fBounds.top;
	if (reverse)
	{
		hPhase ^= 1;
		vPhase ^= 1;
	}
	PenState pen;
	GetPenState(&pen);
	PenNormal();
	long lineWidth = 0;
	if (revealLine)
	{
		lineWidth = (fView->fViewFormat & vfPenMask) >> vfPenShift;
		PenSize(lineWidth, lineWidth);
	}
	Boolean clipped = false;
	if (fView->HasVisRgn())
	{
		TClipper* clipper = fView->Clipper();
		if ((*(RgnHandle) clipper->fFullRgn)->rgnSize != kRectRgnSize)
		{
			clipped = true;
			DiffRgn(clipper->fFullRgn, fMask, fMask);
		}
	}
	if (!clipped)
	{
		TRectangularRegion boundsRgn(fBounds);
		DiffRgn(boundsRgn, fMask, fMask);
	}
	SetClip(fMask);
	unwind_protect
	{
		PlaySound(fContext, sound);
		Rect drawn;
		SetEmptyRect(&drawn);
		for (long step = 1; step <= steps; step++)
		{
			ULong deadline = Ticks() + stepTicks;
			if (saved)
			{
				if (step > 1 && SectRect(&drawn, &fSavedBits.bounds, &drawn))
					fSavedBits.RestoreScreenBits(&drawn, nil);
				SetEmptyRect(&drawn);
			}
			if (!(!fReverse && fOffsetV == 0 && fOffsetH == 0) || step < steps)
			{
				BusyBoxSend(0x35);
				long prevV = fBounds.top;
				for (long row = 1; row <= rows; row++)
				{
					long hp = hPhase, vp = vPhase;
					long rowBottom = fBounds.top + row * height / rows;
					long rowH = rowBottom - prevV;
					long vShift = 0;
					if (moveV)
					{
						long t = step * (rowH - fOffsetV) / steps;
						rowH = fReverse ? fOffsetV + t : rowH - t;
						if (rowH > fLimit)
						{
							vShift = fLimit - rowH;
							rowH = fLimit;
						}
					}
					long prevH = fBounds.left;
					for (long col = 1; col <= columns; col++)
					{
						long colRight = fBounds.left + col * width / columns;
						long colW = colRight - prevH;
						if (moveH)
						{
							long t = step * (colW - fOffsetH) / steps;
							colW = fReverse ? fOffsetH + t : colW - t;
						}
						Rect src, dst;
						for (long i = 0; i < 2; i++)
						{
							Rect* r = (i == 0) ? &src : &dst;
							if (hp == 0 && moveH)
							{
								r->left = (short) (colRight - colW);
								r->right = (short) colRight;
							}
							else
							{
								r->left = (short) prevH;
								r->right = (short) (prevH + colW);
							}
							long shift;
							if (vp == 0 && moveV)
							{
								r->top = (short) (rowBottom - rowH);
								r->bottom = (short) rowBottom;
								shift = vShift;
							}
							else
							{
								r->top = (short) prevV;
								r->bottom = (short) (prevV + rowH);
								shift = -vShift;
							}
							if (i == 0 && vShift != 0)
								OffsetRect(r, 0, shift);
							if (reverse == 0)
							{
								hp ^= 1;
								vp ^= 1;
							}
						}
						Draw(src, dst, srcCopy, nil);
						if (saved)
							UnionRect(&drawn, &dst, &drawn);
						if (revealLine && step < steps)
						{
							if (moveH)
							{
								long x = ((prevH == dst.left) == (reverse != 0)) ? dst.right - lineWidth : dst.left;
								MoveTo(x, dst.top);
								Line(0, rowH - lineWidth);
							}
							if (moveV)
							{
								long y = ((prevV == dst.top) == (reverse != 0)) ? dst.bottom - lineWidth : dst.top;
								MoveTo(dst.left, y);
								Line(colW - lineWidth, 0);
							}
						}
						hp ^= hAlternate;
						vp ^= vAlternate;
						prevH = colRight;
					}
					prevV = rowBottom;
					hPhase ^= hAlternateRow;
					vPhase ^= vAlternateRow;
				}
				if (step < steps)
				{
					StopDrawing(nil, nil);
					SleepTillTicks(deadline);
					StartDrawing(nil, nil);
				}
				BusyBoxSend(0x36);
			}
		}
		TRectangularRegion spriteRgn(bounds);
		SectRgn(fMask, spriteRgn, fMask);
		gRootView->Validate(fMask);
	}
	on_unwind
	{
		Rect everywhere;
		SetRect(&everywhere, -32767, -32767, 32766, 32766);
		ClipRect(&everywhere);
		SetPenState(&pen);
	}
	end_unwind;
}


/*------------------------------------------------------------------------------
	T h e   t r a s h
------------------------------------------------------------------------------*/

// ROM 0x00044204 CrumplePt__FlN21P5Point
// A point jittered by up to spread around (h, v).
static void
CrumplePt(long h, long v, long spread, Point* pt)
{
	pt->h = (short) (Rand(spread) + h - (spread >> 1));
	pt->v = (short) (Rand(spread) + v - (spread >> 1));
}


// ROM 0x0004425c CrumpleRect__F11TBaseRegionP4Rectl
// The rectangle as a region with crumpled edges: its corners jittered by
// half the spread and the middles of its sides by the spread, joined by
// lines.
static void
CrumpleRect(RgnHandle rgn, Rect* r, long spread)
{
	OpenRgn();
	long half = spread >> 1;
	Point pt, first;
	CrumplePt(r->left, r->top, half, &pt);
	MoveTo(pt.h, pt.v);
	first = pt;
	CrumplePt(r->left, (r->top + r->bottom) >> 1, spread, &pt);
	LineTo(pt.h, pt.v);
	CrumplePt(r->left, r->bottom, half, &pt);
	LineTo(pt.h, pt.v);
	CrumplePt((r->left + r->right) >> 1, r->bottom, spread, &pt);
	LineTo(pt.h, pt.v);
	CrumplePt(r->right, r->bottom, half, &pt);
	LineTo(pt.h, pt.v);
	CrumplePt(r->right, (r->top + r->bottom) >> 1, spread, &pt);
	LineTo(pt.h, pt.v);
	CrumplePt(r->right, r->top, half, &pt);
	LineTo(pt.h, pt.v);
	CrumplePt((r->left + r->right) >> 1, r->top, spread, &pt);
	LineTo(pt.h, pt.v);
	CrumplePt(r->left, r->top, half, &pt);
	LineTo(pt.h, pt.v);
	LineTo(first.h, first.v);
	CloseRgn(rgn);
}


// ROM 0x00044488 CrumpleSprite__8TAnimateFP5TRectT1
// The view's image crumpled in six passes, eight ticks each: the sprite
// is cut into eight vertical strips which are squeezed towards the
// middle pass by pass (the left four moved right and the right four
// left, each by a share of the pass's inset, each losing rows at the
// top and bottom as it goes in), the screen under the sprite put back,
// and the sprite drawn clipped to a crumpled region of the shrinking box
// (its edges jittered more each pass) and framed two pixels wide, with a
// crumple sound.  ==> crumpleBox the box the crumple ended in, spriteBox
// the region's bounds.
void
TAnimate::CrumpleSprite(Rect* crumpleBox, Rect* spriteBox)
{
	Rect strips[8];
	strips[0] = fBounds;
	*spriteBox = fBounds;
	*crumpleBox = fBounds;
	long width = fBounds.right - fBounds.left;
	long height = fBounds.bottom - fBounds.top;
	long stripWidth = width / 8;
	strips[0].right = (short) (strips[0].left + stripWidth);
	for (long i = 1; i < 8; i++)
	{
		strips[i] = strips[i - 1];
		OffsetRect(&strips[i], stripWidth, 0);
	}
	strips[7].right = fBounds.right;
	long insetH = ((width - 16) << 15) / 6;			// 16.16: half the width to lose, over the passes
	long insetV = ((height - 16) << 15) / 6;
	long fractionH = 0, fractionV = 0;
	TRegionVar rgn;
	InsetRect(crumpleBox, 2, 2);
	PenState pen;
	GetPenState(&pen);
	PenNormal();
	PenSize(2, 2);
	PlaySound(fContext, RefVar(Rcrumple));
	for (long pass = 0; pass < 6; pass++)
	{
		ULong deadline = Ticks() + 8;
		CrumpleRect(rgn, crumpleBox, pass * 2 + 5);
		long nextH = fractionH + insetH;
		long nextV = fractionV + insetV;
		if (pass > 0)
		{
			long prevShift = 0;
			for (long i = 3; i >= 0; i--)
			{
				Rect src = strips[i];
				long shift = ((insetH / (i + 1)) + 0x8000) >> 16;
				if (i < 3)
				{
					strips[i + 1].left = (short) (strips[i + 1].left + (shift - prevShift));
					strips[i].right = (short) (strips[i].right + shift);
				}
				else
					src.right = (short) (src.right - shift);
				strips[i].left = (short) (strips[i].left + shift);
				src.bottom = (short) (src.bottom - i);
				strips[i].top = (short) (strips[i].top + i);
				CopyBits(this, this, &src, &strips[i], srcCopy, nil);
				prevShift = shift;
			}
			prevShift = 0;
			for (long i = 4; i < 8; i++)
			{
				Rect src = strips[i];
				long shift = ((insetH / (8 - i)) + 0x8000) >> 16;
				if (i > 4)
				{
					strips[i - 1].right = (short) (strips[i - 1].right - (shift - prevShift));
					strips[i].left = (short) (strips[i].left - shift);
				}
				else
					src.left = (short) (src.left + shift);
				strips[i].right = (short) (strips[i].right - shift);
				src.top = (short) (src.top + i);
				strips[i].bottom = (short) (strips[i].bottom - i);
				CopyBits(this, this, &src, &strips[i], srcCopy, nil);
				prevShift = shift;
			}
			StartDrawing(nil, nil);
			fSavedBits.RestoreScreenBits(spriteBox, nil);
		}
		EraseRgn(rgn);
		SetClip(rgn);
		Draw(*spriteBox, *spriteBox, srcCopy, nil);
		Rect everywhere;
		SetRect(&everywhere, -32767, -32767, 32766, 32766);
		ClipRect(&everywhere);
		FrameRgn(rgn);
		StopDrawing(nil, nil);
		SleepTillTicks(deadline);
		InsetRect(crumpleBox, nextH >> 16, nextV >> 16);
		fractionH = nextH & 0xffff;
		fractionV = nextV & 0xffff;
		*spriteBox = (*(RgnHandle) rgn)->rgnBBox;
		SetPenState(&pen);
	}
}


// ROM 0x000449c8 CrumpleEffect__8TAnimateFv
// The crumple, then the ball's flight into the trash: the trash bitmap
// placed at the bottom right of the application area (vars.displayParams'
// appAreaGlobalLeft/Top/Width/Height), the ball's box the size of the
// first crumple bitmap at the crumple's top left; a path of up to sixty
// points - the ball rises for four (16, 9, 4, 1 pixels) and falls with
// gathering speed until it reaches the trash's top, landing at its
// bottom, the horizontal spread evenly from the crumple's middle to the
// trash's; the trash drawn (its mask, then its bits) and the port's clip
// cut so nothing is drawn over the trash or right of it; then, four ticks
// a frame at every other point of the path, the screen put back under
// the ball and a crumple bitmap (the six cycled every other frame) drawn
// at the point, until the path is done; a plunk, the clip opened, half a
// second's pause, the screen put back under the trash.
void
TAnimate::CrumpleEffect(void)
{
	Rect crumpleBox, spriteBox;
	CrumpleSprite(&crumpleBox, &spriteBox);
	RefVar trashBitmap(Rtrashbitmap);
	Rect trashBox;
	if (!FromObject(RefVar(GetFrameSlotRef(trashBitmap, RSSYMbounds)), trashBox))
		ThrowMsg((char*) "bad trash bounds");
	RefVar displayParams(GetFrameSlotRef(gVarFrame, RSSYMdisplayparams));
	Long appLeft = RINT(GetProtoVariable(displayParams, RSSYMappareagloballeft, nil));
	Long appWidth = RINT(GetProtoVariable(displayParams, RSSYMappareawidth, nil));
	Long appTop = RINT(GetProtoVariable(displayParams, RSSYMappareaglobaltop, nil));
	Long appHeight = RINT(GetProtoVariable(displayParams, RSSYMappareaheight, nil));
	OffsetRect(&trashBox, appLeft + appWidth - trashBox.right, appTop + appHeight - trashBox.bottom);
	RefVar bitmaps(Rcrumplebitmaps);
	RefVar ball(GetArraySlotRef(bitmaps, 0));
	Rect ballBounds;
	if (!FromObject(RefVar(GetFrameSlotRef(ball, RSSYMbounds)), ballBounds))
		ThrowMsg((char*) "bad pictBounds frame");
	crumpleBox.right = (short) (crumpleBox.left + (ballBounds.right - ballBounds.left));
	crumpleBox.bottom = (short) (crumpleBox.top + (ballBounds.bottom - ballBounds.top));

	Point path[61];		// (the ROM's array is sixty: a fall of sixty points writes one past it)
	path[0].v = crumpleBox.top;
	path[0].h = crumpleBox.left;
	for (long i = 1; i < 5; i++)
		path[i].v = (short) (path[i - 1].v - (5 - i) * (5 - i));
	long last = 5;
	for ( ; ; )
	{
		path[last].v = (short) (path[last - 1].v + ((last - 5) * (last - 5)) / 16);
		if (path[last].v >= trashBox.top)
			break;
		if (++last >= 60)
			break;
	}
	long count = last + 1;
	path[last].v = trashBox.bottom;
	long travel = (trashBox.left + trashBox.right) / 2 - (fBounds.left + fBounds.right) / 2;
	for (long i = 1; i < count; i++)
		path[i].h = (short) (crumpleBox.left + i * travel / count);

	DrawPicture(trashBitmap, trashBox, 0, -srcBic);
	DrawPicture(trashBitmap, trashBox, 0, srcOr);
	Rect trashArea = trashBox;
	trashArea.left = (short) (trashBox.left - 25);
	trashArea.right = (short) screenWidth;
	GrafPort* port;
	GetPort(&port);
	{
		TRectangularRegion trashRgn(trashArea);
		DiffRgn(port->clipRgn, trashRgn, port->clipRgn);
	}
	long frame = 0;
	Boolean odd = true;
	Boolean skipped = true;
	for (long i = 0; i <= count; i++)
	{
		skipped = !skipped;		// every other point is drawn
		if (skipped)
			continue;
		ULong deadline = Ticks();
		if (i < count)
		{
			OffsetRect(&crumpleBox, path[i].h - crumpleBox.left, path[i].v - crumpleBox.top);
			ball = GetArraySlotRef(bitmaps, frame);
		}
		StartDrawing(nil, nil);
		fSavedBits.RestoreScreenBits(&spriteBox, nil);
		if (i < count)
		{
			DrawPicture(ball, crumpleBox, 0, -srcBic);
			DrawPicture(ball, crumpleBox, 0, srcOr);
		}
		StopDrawing(nil, nil);
		SleepTillTicks(deadline + 4);
		odd = !odd;
		if (odd)
			frame = (frame + 1) % 6;
		spriteBox = crumpleBox;
	}
	PlaySound(fContext, RefVar(Rplunk));
	StartDrawing(nil, nil);
	Rect everywhere;
	SetRect(&everywhere, -32767, -32767, 32766, 32766);
	ClipRect(&everywhere);
	SleepTillTicks(Ticks() + 30);
	fSavedBits.RestoreScreenBits(&trashBox, nil);
}


// ROM 0x00044ffc PoofEffect__8TAnimateFv
// The poof sound; three clouds drawn in turn filling the area (each
// masked; the ROM's clouds are 178 x 109, drawn at half size), two
// ticks, two ticks and a tick apart, the screen put back between them;
// then the view's saved area dirtied.
void
TAnimate::PoofEffect(void)
{
	PlaySound(fContext, RefVar(Rpoof));
	DrawPicture(RefVar(Rcloud1), fBounds, vjFullH + vjFullV, patCopy);
	StopDrawing(nil, nil);
	Wait(2);
	StartDrawing(nil, nil);
	fSavedBits.RestoreScreenBits(&fSaveBounds, nil);
	DrawPicture(RefVar(Rcloud2), fBounds, vjFullH + vjFullV, patCopy);
	StopDrawing(nil, nil);
	Wait(2);
	StartDrawing(nil, nil);
	fSavedBits.RestoreScreenBits(&fSaveBounds, nil);
	DrawPicture(RefVar(Rcloud3), fBounds, vjFullH + vjFullV, patCopy);
	StopDrawing(nil, nil);
	Wait(1);
	StartDrawing(nil, nil);
	fSavedBits.RestoreScreenBits(&fSaveBounds, nil);
	fView->Dirty(&fSaveBounds);
}
