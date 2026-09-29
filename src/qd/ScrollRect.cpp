/*
	File:		qd/ScrollRect.cpp

	Contains:	ScrollRect (ScrollRect.h).

	Reconstructed from the MP2x00 US ROM (0x00340378, 0x001ccf2c); each
	function cites its origin.
*/

#include "ScrollRect.h"
#include "Rects.h"
#include "Regions.h"
#include "Draw.h"
#include "NewtonMemory.h"


// ROM 0x001ccf2c StartProtectSrcBits__FP8PixelMapP4Rect
void
StartProtectSrcBits(PixelMap* /*map*/, Rect* /*r*/)
{ }


// ROM 0x001ccf30 StopProtectSrcBits__FP8PixelMap
void
StopProtectSrcBits(PixelMap* /*map*/)
{ }


// ROM 0x00340378 ScrollRect__FP4RectlT2PP6Region
// What shows of the rectangle (its region cut by the visRgn and the
// clipRgn) is blitted (dh, dv) along, clipped to where it lands; what it
// uncovered - the region less its moved copy - is the update region, and
// is filled with the background pattern (mode 8, patCopy).
void
ScrollRect(Rect* r, long dh, long dv, RgnHandle updateRgn)
{
	GrafPtr port = GetCurrentPort();
	if (port->pnVis < 0 || (dh == 0 && dv == 0))
	{
		SetEmptyRgn(updateRgn);
		return;
	}
	RgnHandle shown = NewRgn();
	RgnHandle moved = NewRgn();
	RectRgn(shown, r);
	SectRgn(shown, port->visRgn, shown);
	SectRgn(shown, port->clipRgn, shown);
	CopyRgn(shown, moved);
	OffsetRgn(moved, dh, dv);
	Rect source = *r;
	OffsetRect(&source, -dh, -dv);
	DiffRgn(shown, moved, updateRgn);
	PixelMap* bits = &port->portBits;
	StartProtectSrcBits(bits, &source);
	RgnBlt(bits, bits, &source, r, 0, nil, moved, shown, wideHandle);
	RgnBlt(bits, bits, r, r, 8, port->bgPat, updateRgn, wideHandle, wideHandle);
	StopProtectSrcBits(bits);
	DisposHandle((Handle) shown);
	DisposHandle((Handle) moved);
}
