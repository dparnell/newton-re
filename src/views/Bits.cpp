/*
	File:		views/Bits.cpp

	Contains:	TBits, the offscreen bits.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Bits.h"
#include "Rects.h"
#include "Draw.h"
#include "Screen.h"
#include "NewtonMemory.h"
#include "objects.h"
#include "View.h"
#include "RootView.h"
#include "ViewFlags.h"
#include "OSErrors.h"
#include "Regions.h"
#include <string.h>


// ROM 0x000422b4 __ct__5TBitsFv
// No bits yet; a stack instance registers its cleanup (the host's
// destructor runs on a throw).
TBits::TBits()
{
	baseAddr = nil;
	fPort = nil;
	fDrawn = false;
	fOwnsBits = true;
}


// ROM 0x0004512c __dt__5TBitsFv
TBits::~TBits()
{
	Cleanup();
}


// ROM 0x0004516c Cleanup__5TBitsFv
// The drawing ended and the bits disposed when they are ours.
void
TBits::Cleanup(void)
{
	if (baseAddr == nil)
		return;
	EndDrawing();
	if (fOwnsBits)
	{
		DisposHandle((Handle) baseAddr);
		fOwnsBits = false;
	}
	baseAddr = nil;
}


// ROM 0x000425b4 InitBitMap__5TBitsSFRC5TRectP8PixelMap
// A handle pixel map of the screen's depth over the rectangle: the row
// bytes the width in bits rounded up to 32, the resolution 72; ==> the
// size of its bits.
long
TBits::InitBitMap(const Rect& bounds, PixelMap* map)
{
	long depth;
	GetGrafInfo(kGrafInfoDepth, &depth);
	if (depth == 0)
		depth = PixelMapDepth(&GetCurrentPort()->portBits);		// host: no screen (a test drawing offscreen)
	long rowBytes = ((depth * (bounds.right - bounds.left) + 31) & ~31) >> 3;
	map->baseAddr = nil;
	map->rowBytes = (short) rowBytes;
	map->bounds = bounds;
	map->pixMapFlags = depth;			// kPixMapHandle is 0
	map->deviceRes.h = kDefaultDPI;
	map->deviceRes.v = kDefaultDPI;
	map->grayTable = nil;
	return rowBytes * (bounds.bottom - bounds.top);
}


// ROM 0x00042fbc Constructor__5TBitsFRC5TRect
// Bits for the rectangle; ==> whether there was memory (nothing for an
// empty rectangle).
Boolean
TBits::Constructor(const Rect& bounds)
{
	if (EmptyRect(&bounds))
		return false;
	long size = InitBitMap(bounds, this);
	baseAddr = (Ptr) NewHandle(size);
	fDrawn = false;
	return baseAddr != nil;
}


// ROM 0x0004499c Constructor__5TBitsFRC8PixelMap
// Over another map's bits (not ours to dispose).
void
TBits::Constructor(const PixelMap& map)
{
	*(PixelMap*) this = map;
	fOwnsBits = false;
	fDrawn = false;
}


// ROM 0x000451b4 SetBounds__5TBitsFRC5TRect
void
TBits::SetBounds(const Rect& newBounds)
{
	bounds = newBounds;
}


// ROM 0x0004238c CopyFromScreen__5TBitsFRC5TRectT1lPP6Region
// The current port's pixels of src copied into dst of ours.
void
TBits::CopyFromScreen(const Rect& src, const Rect& dst, long mode, RgnHandle mask)
{
	GrafPort* port;
	GetPort(&port);
	CopyBits(&port->portBits, this, &src, &dst, mode, mask);
	fDrawn = true;
}


// ROM 0x000423dc Draw__5TBitsFRC5TRectT1lPP6Region
// Our pixels of src copied to dst of the current port.
void
TBits::Draw(const Rect& src, const Rect& dst, long mode, RgnHandle mask)
{
	GrafPort* port;
	GetPort(&port);
	CopyBits(this, &port->portBits, &src, &dst, mode, mask);
	fDrawn = true;
}


// ROM 0x00042330 Draw__5TBitsFRC5TRectlPP6Region
void
TBits::Draw(const Rect& dst, long mode, RgnHandle mask)
{
	Rect src = bounds;
	Draw(src, dst, mode, mask);
}


// ROM 0x0004236c CopyIntoBitmap__5TBitsFP8PixelMaplPP6Region
void
TBits::CopyIntoBitmap(PixelMap* map, long mode, RgnHandle mask)
{
	CopyBits(this, map, &bounds, &map->bounds, mode, mask);
}


// ROM 0x00042444 Fill__5TBitsFl
// Every word of the bits set to the pattern (rowBytes * height bytes).
void
TBits::Fill(long pattern)
{
	Ptr bits = GetPixelMapBits(this);
	long size = rowBytes * (bounds.bottom - bounds.top);
	unsigned char* p = (unsigned char*) bits;
	for (long i = 0; i < size; i++)
		p[i] = (unsigned char) (pattern >> (24 - 8 * (i & 3)));		// the word's bytes as the ROM stores them
}


/*------------------------------------------------------------------------------
	D r a w i n g   i n t o   t h e   b i t s
------------------------------------------------------------------------------*/

// ROM 0x00042484 Constructor__9TBitsPortFP5TBits6TPointUc
// A new port over the bits made current: its portRect and visRgn the
// map's bounds, the bits cleared when asked, the origin set.
void
TBitsPort::Constructor(TBits* bits, Point origin, Boolean fill)
{
	fBits = bits;
	GetPort(&fSavedPort);
	fPort = new GrafPort;
	if (fPort == nil)
		OutOfMemory();
	OpenPort(fPort);
	::SetPort(fPort);
	SetPortBits(bits);
	fPort->portRect = bits->bounds;
	GrafPort* port;
	GetPort(&port);
	RectRgn(port->visRgn, &bits->bounds);
	if (fill)
		bits->Fill(0);
	SetOrigin(origin.h, origin.v);
}


// ROM 0x0004256c __dt__9TBitsPortFv
// The port before made current again, ours closed.
TBitsPort::~TBitsPort()
{
	::SetPort(fSavedPort);
	ClosePort(fPort);
	delete fPort;
}


// ROM 0x000451c4 BeginDrawing__5TBitsF6TPoint
// The bits made the current port at the origin: a TBitsPort the first
// time (the bits cleared unless they came from the screen), else the port
// made current again.  In slow motion (gSlowMotion, ViewAutopsy) the
// port current before - the screen - is made current again instead, so
// the drawing is seen as it happens; EndDrawing copies it into the bits.
void
TBits::BeginDrawing(Point origin)
{
	if (fPort == nil)
	{
		fPort = new TBitsPort;
		if (fPort == nil)
			OutOfMemory();
		fPort->Constructor(this, origin, !fDrawn);
	}
	else
	{
		SetPort();
		SetOrigin(origin.h, origin.v);
	}
	if (gSlowMotion != 0)
		::SetPort(fPort->fSavedPort);		// (the ROM sets the newt globals' current port)
}


// ROM 0x0004527c EndDrawing__5TBitsFv
// The port disposed (the port before made current); in slow motion what
// was drawn on the screen copied into the bits first.
void
TBits::EndDrawing(void)
{
	if (gSlowMotion != 0)
	{
		Rect r = bounds;
		CopyFromScreen(r, r, 0, nil);
	}
	if (fPort != nil)
		delete fPort;
	fPort = nil;
}


// ROM 0x0004242c SetPort__5TBitsFv
void
TBits::SetPort(void)
{
	::SetPort(fPort->fPort);
}


// ROM 0x00042438 RestorePort__5TBitsFv
void
TBits::RestorePort(void)
{
	::SetPort(fPort->fSavedPort);
}


// ROM 0x000414e0 InitBitMap__FP8PixelMapRC5TRectlN23
// The map's bits a new handle, the rows rounded up to 32 bits; the
// resolution as given.  ==> whether the handle could be had.
Boolean
InitBitMap(PixelMap* map, const Rect& bounds, long depth, long hRes, long vRes)
{
	long rowBits = (depth * (short) (bounds.right - bounds.left) + 31) & ~31;
	if (rowBits < 0)
		rowBits += 7;
	long rowBytes = rowBits >> 3;
	Handle bits = NewHandle(rowBytes * (short) (bounds.bottom - bounds.top));
	map->baseAddr = (Ptr) bits;
	map->rowBytes = (short) rowBytes;
	map->bounds = bounds;
	map->pixMapFlags = depth;			// kPixMapHandle is 0
	map->deviceRes.h = (short) hRes;
	map->deviceRes.v = (short) vRes;
	map->grayTable = nil;
	return bits != nil;
}


/*------------------------------------------------------------------------------
	D r a g B i t s
------------------------------------------------------------------------------*/

// ROM 0x000425ac DisposeDragBits__FPv
// The cleanup a Throw runs: the clip put back and the bits given up.
void
DisposeDragBits(void* object)
{
	DragBits* bits = (DragBits*) object;
	if (bits->fConstructed)
	{
		RemoveExceptionHandler((CatchHeader*) &bits->fCleanup);
		SetClip(bits->fSavedClip);
		bits->fConstructed = false;
	}
	bits->fBackground.Cleanup();
	bits->fDataBits.Cleanup();
}


// ROM 0x000429a0 __ct__8DragBitsFv
DragBits::DragBits()
{
	fConstructed = false;
}


// ROM 0x000428d4 __ct__8DragBitsFP5TViewPC5TRectUc
DragBits::DragBits(TView* view, const Rect* bounds, Boolean copy)
{
	fConstructed = false;
	Constructor(view, bounds, copy);
}


// ROM 0x00042938 __dt__8DragBitsFv
DragBits::~DragBits()
{
	if (fConstructed)
	{
		RemoveExceptionHandler((CatchHeader*) &fCleanup);
		SetClip(fSavedClip);
	}
}


// ROM 0x0004266c Constructor__8DragBitsFP5TViewPC5TRectUc
// The bits of a drag of the view's data from the rectangle (the port's
// whole rectangle when there is none).  The data is drawn by the view's
// DrawDragData; the background is taken from the screen and, unless the
// view is a clipping, drawn again - by the view's DrawDragBackground,
// and failing that by the root view without the selection showing, the
// data then taken back out of it (exclusive-or) when the drag is a move.
// A heap too full for either throws exOutOfMemory.
void
DragBits::Constructor(TView* view, const Rect* inBounds, Boolean copy)
{
	GrafPort* port;
	GetPort(&port);
	GetClip(fSavedClip);
	ClipRect(&port->portRect);
	fCleanup.header.catchType = kExceptionCleanup;
	fCleanup.function = DisposeDragBits;
	fCleanup.object = this;
	AddExceptionHandler((CatchHeader*) &fCleanup);
	fConstructed = true;
	const Rect* bounds = inBounds != nil ? inBounds : &port->portRect;
	Rect r = *bounds;
	if (!fDataBits.Constructor(r))
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	Point topLeft;
	topLeft.v = r.top;
	topLeft.h = r.left;
	fDataBits.BeginDrawing(topLeft);
	view->DrawDragData(r);
	fDataBits.RestorePort();
	if (!fBackground.Constructor(*bounds))
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	topLeft.v = bounds->top;
	topLeft.h = bounds->left;
	fBackground.BeginDrawing(topLeft);
	fBackground.RestorePort();
	fBackground.CopyFromScreen(*bounds, *bounds, srcCopy, nil);
	if ((view->fFlags & vClipboard) == 0)
	{
		fBackground.SetPort();
		TRegionVar clip;
		GetClip(clip);
		ClipRect(&r);
		if (!view->DrawDragBackground(r, copy))
		{
			gDontDrawHilites = true;
			gRootView->Draw(r, false);
			gDontDrawHilites = false;
			if (!copy)
			{
				TRegion visRgn(view->SetupVisRgn());
				TRegionVar vis(visRgn);
				fDataBits.Draw(r, r, srcXor, nil);
				GrafPort* bitsPort;
				GetPort(&bitsPort);
				CopyRgn(vis, bitsPort->visRgn);
			}
		}
		SetClip(clip);
		fBackground.RestorePort();
	}
}
