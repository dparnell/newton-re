/*
	File:		views/Hilites.cpp

	Contains:	THilite, TParagraphHilite and HiliteLoop (Hilites.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Hilites.h"
#include "Frames.h"
#include "Unicode.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"
#include "RSSymbols.h"


/*------------------------------------------------------------------------------
	T H i l i t e
------------------------------------------------------------------------------*/

// ROM 0x00262b14 __ct__7THiliteFv
// The ROM leaves the bounds alone; the host empties them, an uninitialised
// rectangle being indeterminate rather than merely stale.
THilite::THilite()
{
	SetEmptyRect(&fBounds);
}


// ROM 0x00262b48 __dt__7THiliteFv
THilite::~THilite()
{ }


// ROM 0x00262b60 Area__7THiliteFv
void
THilite::Area(RgnHandle rgn)
{
	RectRgn(rgn, &fBounds);
}


// ROM 0x00262ba8 Clone__7THiliteFv
THilite*
THilite::Clone(void)
{
	THilite* copy = new THilite;
	copy->CopyFrom(this);
	return copy;
}


// ROM 0x00262bd4 CopyFrom__7THiliteFP7THilite
void
THilite::CopyFrom(THilite* other)
{
	fBounds = other->fBounds;
}


// ROM 0x00262be8 UpdateBounds__7THiliteFv
void
THilite::UpdateBounds(void)
{ }


// ROM 0x00262bec Overlaps__7THiliteFRC5TRect
// A rectangle with no width or height would intersect nothing, so each is
// given a pixel first - a hilite of an empty line still overlaps the line.
static void
WidenIfEmpty(Rect* r)
{
	if (r->left == r->right)
		r->right++;
	if (r->top == r->bottom)
		r->bottom++;
}

Boolean
THilite::Overlaps(const Rect& r)
{
	Rect mine = fBounds;
	Rect theirs = r;
	WidenIfEmpty(&mine);
	WidenIfEmpty(&theirs);
	Rect ignored;
	return SectRect(&mine, &theirs, &ignored);
}


// ROM 0x00262bf4 Encloses__7THiliteFRC6TPoint
Boolean
THilite::Encloses(const Point& pt)
{
	return fBounds.top <= pt.v && pt.v < fBounds.bottom
		&& fBounds.left <= pt.h && pt.h < fBounds.right;
}


/*------------------------------------------------------------------------------
	T P a r a g r a p h H i l i t e
------------------------------------------------------------------------------*/

// ROM 0x00180e38 __ct__16TParagraphHiliteFl
// Room made for the selected text.  The ROM's takes the text's length and
// leaves the range for its caller to set; the reconstruction takes the
// range, whose length is the text's.
TParagraphHilite::TParagraphHilite(long start, long end)
{
	fStart = start;
	fEnd = end;
	fText = new UniChar[end - start + 1];
	if (fText == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	fText[0] = 0;
	fArea = NewRgn();
	if (fArea != nil)
		SetEmptyRgn(fArea);
}


// ROM 0x00180ec8 __dt__16TParagraphHiliteFv
TParagraphHilite::~TParagraphHilite()
{
	if (fText != nil)
		delete[] fText;
	if (fArea != nil)
		DisposeRgn(fArea);
}


// ROM 0x00181094 Area__16TParagraphHiliteFv
// Whatever the paragraph worked out; empty until it has (SetupArea).
void
TParagraphHilite::Area(RgnHandle rgn)
{
	if (fArea != nil)
		CopyRgn(fArea, rgn);
	else
		SetEmptyRgn(rgn);
}


// host: the region the paragraph laid out, and the bounding box that goes
// with it (the ROM's TParagraphView::SetupArea 0x0016c774 does both).
void
TParagraphHilite::SetArea(RgnHandle rgn)
{
	if (fArea == nil)
		return;
	CopyRgn(rgn, fArea);
	fBounds = (*fArea)->rgnBBox;
}


// ROM 0x00180f20 Clone__16TParagraphHiliteFv
// Room for the text as long as the copy's is (Ustrlen), then CopyFrom.
THilite*
TParagraphHilite::Clone(void)
{
	TParagraphHilite* copy = new TParagraphHilite(0, Ustrlen(fText));
	if (copy == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	copy->CopyFrom(this);
	return copy;
}


// ROM 0x00180f7c CopyFrom__16TParagraphHiliteFP7THilite
// The range, the selected text and the laid-out region.  ROM QUIRK: not
// the bounds - THilite::CopyFrom is not called - so a clone's bounds stay
// empty until its area is worked out again.
void
TParagraphHilite::CopyFrom(THilite* other)
{
	TParagraphHilite* from = (TParagraphHilite*) other;
	fStart = from->fStart;
	fEnd = from->fEnd;
	Ustrcpy(fText, from->fText);
	if (fArea != nil && from->fArea != nil)
		CopyRgn(from->fArea, fArea);
}


// ROM 0x00180fbc Overlaps__16TParagraphHiliteFRC5TRect
// The region, once there is one - a selection that runs over several lines
// is not the rectangle around them.
Boolean
TParagraphHilite::Overlaps(const Rect& r)
{
	if (!HasArea())
		return THilite::Overlaps(r);
	return RectInRgn(&r, fArea);
}


// ROM 0x00181040 Encloses__16TParagraphHiliteFRC6TPoint
Boolean
TParagraphHilite::Encloses(const Point& pt)
{
	if (!HasArea())
		return THilite::Encloses(pt);
	return PtInRgn(pt, fArea);
}


/*------------------------------------------------------------------------------
	H i l i t e L o o p
------------------------------------------------------------------------------*/

// ROM 0x00262e90 __ct__10HiliteLoopFP5TView
// The length is read once: a caller that removes what it is handed steps
// fIndex and fCount back itself, as TView::RemoveAllHilites does.
HiliteLoop::HiliteLoop(TView* view)
{
	fHilites = GetFrameSlotRef(view->fContext, RSSYMhilites);
	fIndex = 0;
	fCount = ISNIL(fHilites) ? 0 : Length(fHilites);
	fCurrent = nil;
}


// ROM 0x00262f30 __dt__10HiliteLoopFv
HiliteLoop::~HiliteLoop()
{ }


// ROM 0x00262f68 Next__10HiliteLoopFv
Boolean
HiliteLoop::Next(void)
{
	fHilite = RefVar(NILREF);
	if (fIndex < fCount && NOTNIL(fHilites) && Length(fHilites) > 0)
	{
		fHilite = GetArraySlotRef(fHilites, fIndex++);
		fCurrent = (THilite*) RefToAddress(fHilite);
		return true;
	}
	fCurrent = nil;
	return false;
}
