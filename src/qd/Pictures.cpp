/*
	File:		qd/Pictures.cpp

	Contains:	Drawing bitmap frames: TPixelObj, DrawBitmap, Justify,
				DrawPicture, and asking a bitmap about a point
				(PtInPicture).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Pictures.h"
#include "PicPlay.h"
#include "Rects.h"
#include "Draw.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"
#include "NativeFunctions.h"
#include "ByteOrder.h"
#include "ROMConstants.h"
#include "LargeBinaries.h"		// FLBAllocCompressed, FGetBinaryStore...
#include <string.h>

// the graphics exception (evt.ex.graf) for a picture that is not a bitmap
static const char kGrafException[] = "evt.ex.graf";
const long kGrafErrNotABitmap = -8803;			// the ROM's 0xffffdd9d
const long kGrafErrBadRowBytes = -8808;			// MakeBitmap: rowBytes not a multiple of four, or too small
const long kGrafErrBadDepth = -8807;			// ... a depth that is not a power of two
const long kGrafErrBadWidth = -8806;
const long kGrafErrBadHeight = -8805;
const long kGrafErrBadParameters = -8809;


/*------------------------------------------------------------------------------
	T P i x e l O b j
------------------------------------------------------------------------------*/

// ROM 0x0003e7b8 __ct__9TPixelObjFv
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


// ROM 0x0003e80c __dt__9TPixelObjFv
TPixelObj::~TPixelObj()
{
	if (fGrayTable != nil)
		DisposPtr(fGrayTable);
	if (fLocked)
		UnlockRef(fObject);
	if (NOTNIL(fMaskObject))
		UnlockRef(fMaskObject);
}


// ROM 0x00041448 FramBitMapToPixMap__9TPixelObjFRC10FramBitmap
// A pixel map over a 'bits binary: its rows, row bytes and bounds (the
// binary is a persistent format: its halfwords big-endian), the depth as
// found, 72 dpi, and the gray table when there is one.
//
// This is the one place the two ROMs we have differ in their interface
// rather than only in their addresses, and this follows the later of the
// two.  The MP2x00 US build's TPixelObj is 0x34 bytes with a single
// PixelMap in it, and this method fills that one map and answers it
// (FramBitMapToPixMap(FramBitmap const&)), so Init(picture, withMask)
// converts the mask into it, keeps the pointer, and then converts the
// image over the top of it: the mask and the image end up the same map.
// The MP2100 D build's TPixelObj is 0x50 bytes with a second PixelMap for
// the mask, and its method takes the map to fill
// (0x00041d40 FramBitMapToPixMap__9TPixelObjFRC10FramBitmapP8PixelMap),
// which is what the mask drawing needs and what is written here.
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


// ROM 0x000410ac GetFramBitmap__9TPixelObjFv
// The bitmap frame's bits for the port's depth: the bits slot when it has
// no colorData; else the colorData's - a frame: its bitDepth and cBits;
// an array: the entry of the port's depth, else the shallowest of those
// deeper than the port, else the deepest of those shallower (a deeper
// entry, once seen, is always preferred to a shallower one).  An entry
// deeper than one bit with a colorTable gets a gray table of 2^depth
// bytes: every entry the foreground pattern's first byte, then the
// colour table's entries (eight bytes each: a value and red, green, blue
// halfwords) made grays (RGBtoGray at the entry's depth).
//
// ROM BUG, kept: the table is allocated afresh on every call, a table
// already made left behind.
Ref
TPixelObj::GetFramBitmap(void)
{
	RefVar bits;
	RefVar colorTable;
	RefVar colorData(GetFrameSlotRef(fObject, RSSYMcolordata));
	if (ISNIL(colorData))
		return GetFrameSlotRef(fObject, RSSYMbits);
	long portDepth = GetCurrentPort()->portBits.pixMapFlags & 0xff;
	if (!IsArray(colorData))
	{
		fDepth = RINT(GetFrameSlotRef(colorData, RSSYMbitdepth));
		bits = GetFrameSlotRef(colorData, RSSYMcbits);
		colorTable = GetFrameSlotRef(colorData, RSSYMcolortable);
	}
	else
	{
		long above = 0x7fff;				// the shallowest deeper than the port so far
		long below = 0;						// the deepest shallower
		for (long i = 0, count = Length(colorData); i < count; i++)
		{
			RefVar entry(GetArraySlotRef(colorData, i));
			long depth = RINT(GetFrameSlotRef(entry, RSSYMbitdepth));
			if (depth == portDepth)
			{
				fDepth = depth;
				bits = GetFrameSlotRef(entry, RSSYMcbits);
				colorTable = GetFrameSlotRef(entry, RSSYMcolortable);
				break;
			}
			Boolean take = false;
			if (portDepth < depth && depth < above)
			{
				above = depth;
				take = true;
			}
			else if (above == 0x7fff && depth < portDepth && below != depth && depth >= below)
			{
				below = depth;
				take = true;
			}
			if (take)
			{
				fDepth = depth;
				bits = GetFrameSlotRef(entry, RSSYMcbits);
				colorTable = GetFrameSlotRef(entry, RSSYMcolortable);
			}
		}
	}
	if (fDepth > 1 && NOTNIL(colorTable))
	{
		LockRef(colorTable);
		long entries = Length(colorTable) / 8;
		ULong size = (ULong) 1 << (fDepth & 0xff);
		fGrayTable = NewPtr(size);
		if (fGrayTable != nil)
		{
			if ((ULong) entries > size)
				entries = size;
			UChar fill = *(const UChar*) GetPixelMapBits(*GetFgPattern());
			for (ULong i = 0; i < size; i++)
				fGrayTable[i] = fill;
			const UChar* spec = (const UChar*) BinaryData(colorTable);
			for (long i = 0; i < entries; i++, spec += 8)
				fGrayTable[i] = (char) RGBtoGray(GetBigEndianHalf(spec + 2), GetBigEndianHalf(spec + 4), GetBigEndianHalf(spec + 6), fDepth, fDepth);
		}
		UnlockRef(colorTable);
	}
	return bits;
}


// ROM 0x0003f614 Init__9TPixelObjFRC6RefVar
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
	{
		// (the ROM points fPixels at the binary's own header; the host's
		// PixelMap is not the binary's layout, so it is made of it)
		PixelsToPixMap(BinaryData(fObject), &fPixMap);
		fPixels = &fPixMap;
	}
}


// ROM 0x00040f28 Init__9TPixelObjFRC6RefVarUc
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
		PixelsToPixMap(BinaryData(data), &fPixMap);	// (as above)
		fPixels = &fPixMap;
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

// ROM 0x0003e9b8 DrawBitmap__FRC6RefVarP5TRectl
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


// ROM 0x0003f3f0 PtInPicture__FRC6RefVarN21Uc
// Whether the point (x, y) - taken from the bitmap's own origin - is in
// the picture, or, when `wantsPixel`, what the pixel there is.
//
// A bitmap with a mask is the shape the mask draws, so that is what the
// point is tried against; one without is its own shape, and a pixel that
// is not white is inside it.  Asked for the pixel, the mask only says
// whether there is one: outside it the answer is -1, and inside it the
// value comes from the bits.
//
// The exception handler is the ROM's: it is what gives the TPixelObj back
// when Init throws for a picture that is not a bitmap, since a Throw is a
// longjmp and skips the destructor.
Ref
PtInPicture(RefArg x, RefArg y, RefArg picture, Boolean wantsPixel)
{
	RefVar result;
	TPixelObj obj;
	newton_try
	{
		obj.Init(picture, wantsPixel);
		PixelMap* mask = obj.Mask();
		PixelMap* pixels = obj.Pixels();
		long px = RINT(x);
		long py = RINT(y);
		if (!wantsPixel)
			result = MAKEBOOLEAN(PtInPixelMap(mask != nil ? mask : pixels, px, py));
		else if (mask != nil && PtInMask(mask, px, py) == -1)
			result = MAKEINT(-1);
		else
			result = MAKEINT(PtInCPixelMap(pixels, px, py));
	}
	cleanup
	{
		obj.~TPixelObj();
	}
	end_try;
	return result;
}


// ROM 0x0003f3c0 FPtInPicture__FRC6RefVarN31
// PtInPicture(x, y, bitmap)
Ref
FPtInPicture(RefArg /*rcvr*/, RefArg x, RefArg y, RefArg picture)
{
	return PtInPicture(x, y, picture, false);
}


// ROM 0x0003f3d8 FGetBitmapPixel__FRC6RefVarN31
// GetBitmapPixel(x, y, bitmap)
Ref
FGetBitmapPixel(RefArg /*rcvr*/, RefArg x, RefArg y, RefArg picture)
{
	return PtInPicture(x, y, picture, true);
}


void
RegisterPictureNatives(void)
{
	RegisterNativeFunction("FPtInPicture__FRC6RefVarN31", (void*) FPtInPicture, 3);
	RegisterNativeFunction("FGetBitmapPixel__FRC6RefVarN31", (void*) FGetBitmapPixel, 3);
	RegisterNativeFunction("FGetBitmapInfo", (void*) FGetBitmapInfo, 1);
}


// ROM 0x001895c0 Justify__FP5TRectRC5TRectUl
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


// the shapes' drawing (views/DrawShape.cpp sets them: the view system's,
// which the qd library does not link)
void	(*gPictureShapeDrawer)(RefArg shape, RefArg style, const Point& origin) = nil;
void	(*gPictureShapeBounds)(RefArg shape, Rect* bounds) = nil;


// ROM 0x001897fc DrawPicture__FRC6RefVarRC5TRectUll
// A bitmap frame (one with bits or colorData) drawn in the box: its
// bounds justified into the box ("bad pictBounds frame" without proper
// bounds); mode 8 (patCopy) draws the mask first in srcBic and the bits
// in srcOr - a masked copy; a negative mode draws the mask itself in the
// mode negated.
//
// Anything else - not a frame, a 'bitmap-class frame, a frame without bits
// - is a shape, drawn by DrawShape in the transfer mode.
//
// A 'picture binary is a QuickDraw picture: its frame (big-endian, at +2)
// justified into the box and the picture played there (qd/PicPlay.h's
// DrawPicture; the mode is not looked at).  The ROM's own pictures include
// the world map the Time Zones application draws (Rworldmapbitmap, a
// version 1 picture whose one opcode is a PackBitsRect of a 360x179
// bitmap).
void
DrawPicture(RefArg picture, const Rect& box, ULong justify, long mode)
{
	if (IsBinary(picture))
	{
		if (!EQRef(ClassOf(picture), RSSYMpicture))
			return;
		LockRef(picture);
		Ptr data = (Ptr) BinaryData(picture);
		const unsigned char* frame = (const unsigned char*) data + 2;
		Rect bounds;
		bounds.top = (short) GetBigEndianHalf(frame);
		bounds.left = (short) GetBigEndianHalf(frame + 2);
		bounds.bottom = (short) GetBigEndianHalf(frame + 4);
		bounds.right = (short) GetBigEndianHalf(frame + 6);
		Justify(&bounds, box, justify);
		newton_try
		{
			DrawPicture((PicHandle) &data, &bounds, false);
		}
		newton_catch_all
		{
			UnlockRef(picture);
			rethrow;
		}
		end_try;
		UnlockRef(picture);
		return;
	}
	if (!IsFrame(picture) || IsInstance(picture, RSSYMbitmap)
	 || (!FrameHasSlotRef(picture, RSSYMbits) && !FrameHasSlotRef(picture, RSSYMcolordata)))
	{
		// anything else is a shape (a picture shape among them - what
		// MakePict makes): drawn in a style of the transfer mode alone, at
		// the box's corner, or with its bounds justified into the box
		// (ROM QUIRK, kept: a frame of class 'bitmap goes this way too)
		if (gPictureShapeDrawer == nil)
			return;								// (host: no view system linked)
		Rect where = box;
		if (justify != 0)
		{
			gPictureShapeBounds(picture, &where);
			Justify(&where, box, justify);
		}
		RefVar style(AllocateFrame());
		SetFrameSlot(style, RSSYMtransfermode, RefVar(MAKEINT(mode)));
		Point origin;
		origin.h = where.left;
		origin.v = where.top;
		gPictureShapeDrawer(picture, style, origin);
		return;
	}
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


/*------------------------------------------------------------------------------
	M a k i n g   a   b i t m a p

	A script can make an offscreen bitmap and draw into it: the shape it
	gets back is a `canonicalBitmapShape` whose `data` is a `'pixels`
	binary - a PixelMap header with the rows after it, the map's baseAddr
	being the offset from the header to them (kPixMapOffset).
------------------------------------------------------------------------------*/

// ROM 0x000415a4 MakePixelsObject__FR5TRectlN32RC6RefVarN26
// The binary itself: the header written over the front of it and the
// rows left as they were allocated (nought).  With a store it is a large
// binary instead, compressed by the named compander - TPixelMapCompander
// (stores/PixelMapCompander.cpp: LZ over pages whose rows are each XORed
// with the one above) when none is named.
Ref
MakePixelsObject(const Rect& bounds, long depth, long rowBytes,
				 long hRes, long vRes, RefArg store, RefArg compander, RefArg companderData)
{
	long size = kPixelsHeaderSize + rowBytes * (bounds.bottom - bounds.top);
	RefVar object;
	if (NOTNIL(store))
	{
		RefVar companderName(compander);
		if (ISNIL(companderName))
			companderName = MakeString("TPixelMapCompander");
		object = FLBAllocCompressed(store, RSSYMpixels, RefVar(MAKEINT(size)), companderName, companderData);
	}
	else
		object = AllocateBinary(RSSYMpixels, size);
	UByte* header = (UByte*) BinaryData(object);
	PutBigEndianWord(header, (ULong32) kPixelsHeaderSize);
	PutBigEndianHalf(header + 4, (UShort) rowBytes);
	PutBigEndianHalf(header + 8, (UShort) bounds.top);
	PutBigEndianHalf(header + 10, (UShort) bounds.left);
	PutBigEndianHalf(header + 12, (UShort) bounds.bottom);
	PutBigEndianHalf(header + 14, (UShort) bounds.right);
	PutBigEndianWord(header + 0x10, (ULong32) (kPixMapOffset | kPixMapVersion2 | (ULong) depth));
	PutBigEndianHalf(header + 0x14, (UShort) vRes);
	PutBigEndianHalf(header + 0x16, (UShort) hRes);
	PutBigEndianWord(header + 0x18, 0);
	return object;
}


// (host) The 'pixels header read into a host PixelMap: the same fields,
// baseAddr made a pointer to the rows (the header's offset added to the
// binary's address) so the map can be drawn from anywhere.
void
PixelsToPixMap(const void* pixels, PixelMap* map)
{
	const UByte* header = (const UByte*) pixels;
	ULong flags = GetBigEndianWord(header + 0x10);
	ULong offset = GetBigEndianWord(header);
	map->baseAddr = (flags & kPixMapStorage) == kPixMapOffset ? (Ptr) header + offset : nil;
	map->rowBytes = (short) GetBigEndianHalf(header + 4);
	map->bounds.top = (short) GetBigEndianHalf(header + 8);
	map->bounds.left = (short) GetBigEndianHalf(header + 10);
	map->bounds.bottom = (short) GetBigEndianHalf(header + 12);
	map->bounds.right = (short) GetBigEndianHalf(header + 14);
	map->pixMapFlags = (flags & ~kPixMapStorage) | kPixMapPtr;
	map->deviceRes.v = (short) GetBigEndianHalf(header + 0x14);
	map->deviceRes.h = (short) GetBigEndianHalf(header + 0x16);
	map->grayTable = nil;
}


// (host) A host PixelMap's fields written as a 'pixels header, the rows
// taken to follow it.
void
PixMapToPixels(const PixelMap* map, void* pixels)
{
	UByte* header = (UByte*) pixels;
	PutBigEndianWord(header, (ULong32) kPixelsHeaderSize);
	PutBigEndianHalf(header + 4, (UShort) map->rowBytes);
	PutBigEndianHalf(header + 6, 0);
	PutBigEndianHalf(header + 8, (UShort) map->bounds.top);
	PutBigEndianHalf(header + 10, (UShort) map->bounds.left);
	PutBigEndianHalf(header + 12, (UShort) map->bounds.bottom);
	PutBigEndianHalf(header + 14, (UShort) map->bounds.right);
	PutBigEndianWord(header + 0x10, (ULong32) ((map->pixMapFlags & ~kPixMapStorage) | kPixMapOffset));
	PutBigEndianHalf(header + 0x14, (UShort) map->deviceRes.v);
	PutBigEndianHalf(header + 0x16, (UShort) map->deviceRes.h);
	PutBigEndianWord(header + 0x18, 0);
}


// ROM 0x00041d94 FGetBitmapInfo
// GetBitmapInfo(bitmap): what a bitmap shape is made of, in a clone of
// canonicalBitmapInfo - the store and compander of its bits (when it has
// any), the bits themselves, the bits' bounds, row bytes, depth, the
// offset to the first row (a 'pixels binary's baseAddr, sixteen for the
// old 'bits form) and the resolution [h, v]; then the shape's bounds (a
// 'boundsRect binary, as MakeBitmap makes it), and every slot of the
// shape but class, bounds and data copied across.
Ref
FGetBitmapInfo(RefArg /*rcvr*/, RefArg bitmap)
{
	RefVar shape(Clone(bitmap));
	RefVar info(Clone(RefVar(Rcanonicalbitmapinfo)));
	RefVar data(GetFrameSlotRef(shape, RSSYMdata));
	Boolean isPixels = false;
	if (NOTNIL(data))
	{
		isPixels = IsInstance(data, RSSYMpixels);
		SetFrameSlot(info, RSSYMstore, RefVar(FGetBinaryStore(RefVar(NILREF), data)));
		SetFrameSlot(info, RSSYMcompandername, RefVar(FGetBinaryCompander(RefVar(NILREF), data)));
		SetFrameSlot(info, RSSYMcompanderdata, RefVar(FGetBinaryCompanderData(RefVar(NILREF), data)));
	}
	TPixelObj pixels;
	newton_try
	{
		pixels.Init(bitmap);
		PixelMap* pm = pixels.fPixels;
		SetFrameSlot(info, RSSYMbits, RefVar(GetFrameSlotRef(shape, RSSYMdata)));
		long scanOffset = isPixels ? (long) GetBigEndianWord(BinaryData(data)) : 0x10;	// (the header's baseAddr; the host's map holds a pointer)
		SetFrameSlot(info, RSSYMbitsbounds, RefVar(ToObject(pm->bounds)));
		SetFrameSlot(info, RSSYMrowbytes, RefVar(MAKEINT(pm->rowBytes)));
		SetFrameSlot(info, RSSYMdepth, RefVar(MAKEINT(pm->pixMapFlags & 0xff)));
		SetFrameSlot(info, RSSYMscanoffset, RefVar(MAKEINT(scanOffset)));
		RefVar resolution(AllocateArray(RSSYMresolution, 2));
		SetArraySlotRef(resolution, 0, MAKEINT(pm->deviceRes.h));
		SetArraySlotRef(resolution, 1, MAKEINT(pm->deviceRes.v));
		SetFrameSlot(info, RSSYMresolution, resolution);
	}
	cleanup
	{
		pixels.~TPixelObj();
	}
	end_try;
	RefVar bounds(GetFrameSlotRef(shape, RSSYMbounds));
	SetFrameSlot(info, RSSYMbounds, RefVar(ToObject(*(Rect*) BinaryData(bounds))));
	RemoveSlot(shape, RSSYMclass);
	RemoveSlot(shape, RSSYMbounds);
	RemoveSlot(shape, RSSYMdata);
	if (NOTNIL(shape))
	{
		TObjectIterator* iter = NewTObjectIterator(shape);
		for (; !iter->Done(); iter->Next())
		{
			RefVar tag(iter->Tag());
			SetFrameSlot(info, tag, RefVar(iter->Value()));
			RemoveSlot(shape, tag);
		}
		DeleteTObjectIterator(iter);
	}
	return info;
}


// ROM 0x0004173c FMakeBitmap
// MakeBitmap(width, height, options): a bitmap shape of that size.  The
// options frame may say the `depth` (a power of two; the row bytes are
// multiplied by it), the `rowBytes` outright (a multiple of four, and no
// less than the width needs), the `resolution` (one number for both, or
// an array of the horizontal and the vertical) and, for a bitmap kept on
// a store, its `store`, `companderName` and `companderData`.  Every other
// slot of the options frame is copied into the shape.
//
// The ROM works out what the width and height would be at 72 dpi when
// the resolution is something else - and throws both answers away.  Kept
// as it is, since it makes no difference to what comes out.
Ref
FMakeBitmap(RefArg /*rcvr*/, RefArg width, RefArg height, RefArg options)
{
	long theHeight = RINT(height);
	if (theHeight < 1)
		Throw((ExceptionName) kGrafException, (void*) kGrafErrBadHeight, nil);
	long theWidth = RINT(width);
	if (theWidth < 1)
		Throw((ExceptionName) kGrafException, (void*) kGrafErrBadWidth, nil);
	long rowBytes = ((theWidth + 31) & ~31) >> 3;
	long hRes = kDefaultDPI;
	long vRes = kDefaultDPI;
	long depth = 1;
	RefVar rest;
	RefVar store;
	RefVar compander;
	RefVar companderData;

	if (NOTNIL(options))
	{
		rest = Clone(options);
		if (FrameHasSlot(options, RSSYMdepth))
		{
			long asked = RINT(RefVar(GetFrameSlotRef(options, RSSYMdepth)));
			long power = asked;
			while (power > 1 && (power & 1) == 0)
				power >>= 1;
			if (power != 1)
				Throw((ExceptionName) kGrafException, (void*) kGrafErrBadDepth, nil);
			depth = asked;
			if (asked != 1)
				rowBytes = asked * rowBytes;
			RemoveSlot(rest, RSSYMdepth);
		}
		if (FrameHasSlot(options, RSSYMrowbytes))
		{
			long asked = RINT(RefVar(GetFrameSlotRef(options, RSSYMrowbytes)));
			if ((asked & 3) != 0 || asked < rowBytes)
				Throw((ExceptionName) kGrafException, (void*) kGrafErrBadRowBytes, nil);
			rowBytes = asked;
			RemoveSlot(rest, RSSYMrowbytes);
		}
		if (FrameHasSlot(options, RSSYMresolution))
		{
			RefVar asked(GetFrameSlotRef(options, RSSYMresolution));
			if (!IsArray(asked))
			{
				hRes = RINT(asked);
				vRes = hRes;
			}
			else
			{
				hRes = RINT(RefVar(GetArraySlotRef(asked, 0)));
				vRes = RINT(RefVar(GetArraySlotRef(asked, 1)));
			}
			RemoveSlot(rest, RSSYMresolution);
		}
		if (FrameHasSlot(options, RSSYMstore))
		{
			store = GetFrameSlotRef(options, RSSYMstore);
			RemoveSlot(rest, RSSYMstore);
			// (the ROM reads companderName only when there is a store, and
			//  companderData only when there is a companderName)
		}
		if (FrameHasSlot(options, RSSYMcompandername))
		{
			compander = GetFrameSlotRef(options, RSSYMcompandername);
			RemoveSlot(rest, RSSYMcompandername);
			if (FrameHasSlot(options, RSSYMcompanderdata))
			{
				companderData = GetFrameSlotRef(options, RSSYMcompanderdata);
				RemoveSlot(rest, RSSYMcompanderdata);
			}
		}
	}

	Rect bounds;
	SetRect(&bounds, 0, 0, (short) theWidth, (short) theHeight);
	RefVar pixels(MakePixelsObject(bounds, depth, rowBytes, hRes, vRes,
								   store, compander, companderData));

	RefVar boundsBinary(AllocateBinary(RSSYMboundsrect, sizeof(Rect)));
	BlockMove(&bounds, BinaryData(boundsBinary), sizeof(Rect));
	RefVar shape(Clone(RefVar(Rcanonicalbitmapshape)));
	SetFrameSlot(shape, RSSYMbounds, boundsBinary);
	SetFrameSlot(shape, RSSYMdata, pixels);
	if (NOTNIL(rest))
	{
		TObjectIterator* iter = NewTObjectIterator(rest);
		for (; !iter->Done(); iter->Next())
			SetFrameSlot(shape, RefVar(iter->Tag()), RefVar(iter->Value()));
		DeleteTObjectIterator(iter);
	}
	return shape;
}


void
RegisterBitmapNatives(void)
{
	RegisterNativeFunction("FMakeBitmap", (void*) FMakeBitmap, 3);
	RegisterMungeBitmapNatives();
}
