/*
	File:		qd/Pictures.cpp

	Contains:	Drawing bitmap frames: TPixelObj, DrawBitmap, Justify,
				DrawPicture.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "Pictures.h"
#include "Rects.h"
#include "Draw.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"
#include "ByteOrder.h"
#include <string.h>

// the graphics exception (evt.ex.graf) for a picture that is not a bitmap
static const char kGrafException[] = "evt.ex.graf";
const long kGrafErrNotABitmap = -8803;			// the ROM's 0xffffdd9d


/*------------------------------------------------------------------------------
	T P i x e l O b j
------------------------------------------------------------------------------*/

// ROM 0x0003e868 __ct__9TPixelObjFv
TPixelObj::TPixelObj()
{
	memset(&fPixMap, 0, sizeof(fPixMap));
	memset(&fMaskMap, 0, sizeof(fMaskMap));
	fPixels = &fPixMap;
	fMask = nil;
	fGrayTable = nil;
	fDepth = 1;
	fLocked = false;
}


// ROM 0x0003e8bc __dt__9TPixelObjFv
TPixelObj::~TPixelObj()
{
	if (fGrayTable != nil)
		DisposPtr(fGrayTable);
	if (fLocked)
		UnlockRef(fObject);
	if (NOTNIL(fMaskObject))
		UnlockRef(fMaskObject);
}


// ROM 0x00041d40 FramBitMapToPixMap__9TPixelObjFRC10FramBitmapP8PixelMap
// A pixel map over a 'bits binary: its rows, row bytes and bounds (the
// binary is a persistent format: its halfwords big-endian), the depth as
// found, 72 dpi, and the gray table when there is one.
void
TPixelObj::FramBitMapToPixMap(const FramBitmap& bits, PixelMap* map)
{
	map->baseAddr = (Ptr) &bits + kFramBitmapHeaderSize;
	map->rowBytes = (short) GetBigEndianHalf(&bits.fRowBytes);
	map->bounds.top = (short) GetBigEndianHalf(&bits.fBounds.top);
	map->bounds.left = (short) GetBigEndianHalf(&bits.fBounds.left);
	map->bounds.bottom = (short) GetBigEndianHalf(&bits.fBounds.bottom);
	map->bounds.right = (short) GetBigEndianHalf(&bits.fBounds.right);
	map->pixMapFlags = kPixMapPtr | fDepth;
	map->deviceRes.v = kDefaultDPI;
	map->deviceRes.h = kDefaultDPI;
	map->grayTable = (UChar*) fGrayTable;
	if (fGrayTable != nil)
		map->pixMapFlags |= kPixMapGrayTable;
}


// ROM 0x000419a4 GetFramBitmap__9TPixelObjFv
// The bitmap frame's bits for the port's depth: the bits slot when it has
// no colorData; else the colorData's (a frame: its bitDepth and cBits; an
// array: the entry of the port's depth, or the nearest - the deepest not
// deeper than the port, else the shallowest) with the colour table made
// a gray table (NOT YET RECONSTRUCTED: the table; the entry's depth is
// used as it is).
Ref
TPixelObj::GetFramBitmap(void)
{
	RefVar colorData(GetFrameSlotRef(fObject, RSSYMcolordata));
	if (ISNIL(colorData))
		return GetFrameSlotRef(fObject, RSSYMbits);
	long portDepth = PixelMapDepth(&GetCurrentPort()->portBits);
	RefVar entry;
	if (!IsArray(colorData))
		entry = colorData;
	else
	{
		long bestDepth = 0;
		for (long i = 0, count = Length(colorData); i < count; i++)
		{
			RefVar candidate(GetArraySlotRef(colorData, i));
			long depth = RINT(GetFrameSlotRef(candidate, RSSYMbitdepth));
			if (depth == portDepth)
			{
				entry = candidate;
				break;
			}
			Boolean better = ISNIL(entry)
				|| (depth <= portDepth && (bestDepth > portDepth || depth > bestDepth))
				|| (depth > portDepth && bestDepth > portDepth && depth < bestDepth);
			if (better)
			{
				bestDepth = depth;
				entry = candidate;
			}
		}
	}
	if (ISNIL(entry))
		return GetFrameSlotRef(fObject, RSSYMbits);
	fDepth = RINT(GetFrameSlotRef(entry, RSSYMbitdepth));
	return GetFrameSlotRef(entry, RSSYMcbits);
}


// ROM 0x0003f718 Init__9TPixelObjFRC6RefVar
// The picture readied for drawing: a 'picture binary is refused
// (evt.ex.graf), a bitmap frame gives its bits (a frame that is not a
// bitmap: its data), which are locked and made a pixel map (a 'pixels
// binary is a pixel map already).
void
TPixelObj::Init(RefArg picture)
{
	fObject = picture;
	if (IsInstance(picture, RSSYMpicture))
		Throw((ExceptionName) kGrafException, (void*) kGrafErrNotABitmap, nil);
	if (IsFrame(picture))
	{
		if (!IsInstance(picture, RSSYMbitmap))
			fObject = GetFramBitmap();
		else if (ISNIL(GetFrameSlotRef(picture, RSSYMcolordata)))
			fObject = GetFrameSlotRef(picture, RSSYMdata);
		else
			fObject = GetFramBitmap();
	}
	LockRef(fObject);
	fLocked = true;
	if (!IsInstance(fObject, RSSYMpixels))
	{
		FramBitMapToPixMap(*(const FramBitmap*) BinaryData(fObject), &fPixMap);
		fPixels = &fPixMap;
	}
	else
		fPixels = (PixelMap*) BinaryData(fObject);
}


// ROM 0x00041818 Init__9TPixelObjFRC6RefVarUc
// The same for a frame with a data slot ('pixels or bits), and the mask
// made a pixel map too (always when asked, else only... the ROM makes the
// mask's map when there is a mask, and the bits' map when asked or there
// is no mask).
void
TPixelObj::Init(RefArg picture, Boolean withBits)
{
	fObject = picture;
	fMask = nil;
	if (IsInstance(picture, RSSYMpicture))
		Throw((ExceptionName) kGrafException, (void*) kGrafErrNotABitmap, nil);
	RefVar data(GetFrameSlotRef(picture, RSSYMdata));
	Boolean pixels = NOTNIL(data) && IsInstance(data, RSSYMpixels);
	LockRef(fObject);
	fLocked = true;
	if (pixels)
	{
		fPixels = (PixelMap*) BinaryData(data);
		return;
	}
	fMaskObject = GetFrameSlotRef(picture, RSSYMmask);
	if (NOTNIL(fMaskObject))
	{
		LockRef(fMaskObject);		// (the ROM locks it only while the map is made; the host keeps it locked - the heap moves objects)
		FramBitMapToPixMap(*(const FramBitmap*) BinaryData(fMaskObject), &fMaskMap);
		fMask = &fMaskMap;
	}
	if (withBits || fMask == nil)
	{
		fObject = GetFramBitmap();
		LockRef(fObject);
		FramBitMapToPixMap(*(const FramBitmap*) BinaryData(fObject), &fPixMap);
		fPixels = &fPixMap;
	}
}


/*------------------------------------------------------------------------------
	D r a w i n g
------------------------------------------------------------------------------*/

// ROM 0x0003ea68 DrawBitmap__FRC6RefVarP5TRectl
// The bitmap's pixels copied into the box in the mode (a box of no width
// takes the bits' size).
void
DrawBitmap(RefArg bitmap, Rect* box, long mode)
{
	TPixelObj pixels;
	pixels.Init(bitmap);
	PixelMap* map = pixels.Pixels();
	if (box->left == box->right)
	{
		box->right = (short) (box->left + (map->bounds.right - map->bounds.left));
		box->bottom = (short) (box->top + (map->bounds.bottom - map->bounds.top));
	}
	GrafPort* port;
	GetPort(&port);
	CopyBits(map, &port->portBits, &map->bounds, box, mode, nil);
}


// ROM 0x0018b5f0 Justify__FP5TRectRC5TRectUl
// The rectangle placed in the box by the viewJustify bits: a box of no
// size takes the rectangle's; vertically at the top, centred (never
// above the top), at the bottom, or the box's full height; horizontally
// at the left, right, centred, or the full width.
void
Justify(Rect* r, const Rect& box, ULong justify)
{
	long top = box.top;
	long left = box.left;
	long bottom = box.bottom;
	long right = box.right;
	if (right - left == 0 && bottom - top == 0)
	{
		right = left + (r->right - r->left);
		bottom = top + (r->bottom - r->top);
	}
	long boxHeight = bottom - top;
	long boxWidth = right - left;
	long dy = 0;
	switch (justify & 0xc)
	{
	case 4:
		dy = (boxHeight - (r->bottom - r->top)) / 2;
		if (dy < 0)
			dy = 0;
		break;
	case 0xc:
		r->bottom = (short) (r->top + boxHeight);
		// (falls through: the bottom of the box, which is now the top)
	case 8:
		dy = boxHeight - (r->bottom - r->top);
		break;
	}
	long dx = 0;
	switch (justify & 3)
	{
	case 1:
		dx = boxWidth - (r->right - r->left);
		break;
	case 3:
		r->right = (short) (r->left + boxWidth);
		// (falls through)
	case 2:
		dx = (boxWidth - (r->right - r->left)) / 2;
		if (dx < 0)
			dx = 0;
		break;
	}
	OffsetRect(r, (left + dx) - r->left, (top + dy) - r->top);
}


// ROM 0x0018b82c DrawPicture__FRC6RefVarRC5TRectUll
// A bitmap frame (one with bits or colorData) drawn in the box: its
// bounds justified into the box ("bad pictBounds frame" without proper
// bounds); mode 8 (patCopy) draws the mask first in srcBic and the bits
// in srcOr - a masked copy; a negative mode draws the mask itself in the
// mode negated.  NOT YET RECONSTRUCTED: 'picture binaries (QuickDraw
// pictures, DrawPicture 0x0030e270) and shapes (DrawShape) - nothing is
// drawn for them.
void
DrawPicture(RefArg picture, const Rect& box, ULong justify, long mode)
{
	if (IsBinary(picture))
		return;
	if (!IsFrame(picture))
		return;
	if (!IsInstance(picture, RSSYMbitmap) && !FrameHasSlotRef(picture, RSSYMbits) && !FrameHasSlotRef(picture, RSSYMcolordata))
		return;
	Rect bounds;
	RefVar boundsFrame(GetFrameSlotRef(picture, RSSYMbounds));
	if (ISNIL(boundsFrame) || !FromObject(boundsFrame, bounds))
		ThrowMsg((char*) "bad pictBounds frame");
	Justify(&bounds, box, justify);
	RefVar bitmap(picture);
	if (mode == patCopy)
	{
		RefVar mask(GetFrameSlotRef(picture, RSSYMmask));
		if (NOTNIL(mask))
			DrawBitmap(mask, &bounds, srcBic);
		mode = srcOr;
	}
	else if (mode < 0)
	{
		mode = -mode;
		bitmap = GetFrameSlotRef(picture, RSSYMmask);
	}
	DrawBitmap(bitmap, &bounds, mode);
}
