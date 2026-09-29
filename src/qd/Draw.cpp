/*
	File:		qd/Draw.cpp

	Contains:	Drawing rectangles and regions, and the blitter.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	The blitter is written a pixel at a time (Draw.h says why); the ROM's
	transfer semantics - the source inverted for the notSrc/notPat modes,
	copy, or (a non-white gray source pixel replacing the destination's),
	xor and bic - are kept, as is the clipping: the destination rectangle
	cut to the map's bounds and the clip regions' boxes, and each pixel
	row masked by the regions' scan lines (RgnState).
*/

#include "Draw.h"
#include "PicRecord.h"

// QuickDraw's per-depth shifts (QDTables.cpp, generated)
extern const unsigned char	kDepthPixelsPerWordShift[33];
extern const unsigned char	kDepthPixelsPerByteShift[17];
extern const unsigned char	kDepthPixelsPerWordMask[33];
#include "Screen.h"
#include "FixedMath.h"
#include "OSErrors.h"
#include <string.h>


/*------------------------------------------------------------------------------
	T h e   b l i t t e r
------------------------------------------------------------------------------*/

// a source pixel's value brought to the destination's depth (a set one-bit
// pixel is black at any depth; a non-white deeper pixel is set at one bit)
static inline long
ConvertDepth(long value, long fromDepth, long toDepth)
{
	if (fromDepth == toDepth)
		return value;
	if (value == 0)
		return 0;
	if (fromDepth == 1)
		return (1 << toDepth) - 1;
	if (toDepth == 1)
		return 1;
	if (fromDepth < toDepth)
		return value << (toDepth - fromDepth);
	return value >> (fromDepth - toDepth);
}


// one pixel transferred under the mode's operation (mode bits 0-1)
static inline long
Transfer(long op, long dst, long src, long maxValue)
{
	switch (op)
	{
	case 0:		return src;											// copy
	case 1:		return (maxValue == 1) ? (dst | src) : (src != 0 ? src : dst);	// or: the ROM's gray "or"
	case 2:		return dst ^ src;									// xor
	default:	return dst & ~src;									// bic
	}
}


// The rows of dstRect (already clipped) drawn from srcRect's pixels (or the
// pattern's when mode has patCopy's bit), each pixel passed by the masks
// (any number of scan states, nil for none); the source read a row ahead
// when it is the destination map, and bottom up when it lies above.
static void
BlitPixels(PixelMap* src, PixelMap* dst, const Rect* srcRect, const Rect* dstRect, const Rect* clipped, long mode, PatternHandle pattern, RgnState** masks, long maskCount, long* row)
{
	long depth = PixelMapDepth(dst);
	long maxValue = (1 << depth) - 1;
	long op = mode & 3;
	Boolean invert = (mode & 4) != 0;
	Boolean usePattern = (mode & 8) != 0;
	long srcDepth = usePattern ? depth : PixelMapDepth(src);
	long dh = srcRect->left - dstRect->left;			// the source pixel for a destination pixel
	long dv = srcRect->top - dstRect->top;
	long width = clipped->right - clipped->left;
	if (width <= 0 || clipped->top >= clipped->bottom || row == nil)
		return;
	Boolean sameBits = !usePattern && GetPixelMapBits(src) == GetPixelMapBits(dst);
	Boolean upward = sameBits && dv < 0;				// the source above: copy the bottom rows first
	long y = upward ? clipped->bottom - 1 : clipped->top;
	long yEnd = upward ? clipped->top - 1 : clipped->bottom;
	long yStep = upward ? -1 : 1;
	for (; y != yEnd; y += yStep)
	{
		// the source row first (it may overlap the destination row)
		for (long i = 0; i < width; i++)
		{
			long x = clipped->left + i;
			long value = usePattern ? PatternPixel(pattern, x, y, depth) : ConvertDepth(GetPixel(src, x + dh, y + dv), srcDepth, depth);
			if (invert)
				value ^= maxValue;
			row[i] = value;
		}
		for (long m = 0; m < maskCount; m++)
			SeekRgn(masks[m], y);
		for (long i = 0; i < width; i++)
		{
			long x = clipped->left + i;
			Boolean visible = true;
			for (long m = 0; m < maskCount && visible; m++)
			{
				long bit = x - masks[m]->fOrigin;
				if (!(masks[m]->fScan[bit >> 5] & (0x80000000u >> (bit & 31))))
					visible = false;
			}
			if (visible)
				SetPixel(dst, x, y, Transfer(op, GetPixel(dst, x, y), row[i], maxValue));
		}
	}
}


// ROM 0x002ac9c8 BitBlt__FP8PixelMapT1P4RectT3lPP8PixelMap
// The transfer without region clipping: dstRect is drawn as it is (the
// caller has clipped it) from the corresponding pixels of srcRect.
void
BitBlt(PixelMap* src, PixelMap* dst, const Rect* srcRect, const Rect* dstRect, long mode, PatternHandle pattern)
{
	long width = dstRect->right - dstRect->left;
	if (width <= 0)
		return;
	long* row = (long*) QDNewTempPtr(width * sizeof(long));
	BlitPixels(src, dst, srcRect, dstRect, dstRect, mode, pattern, nil, 0, row);
	QDDisposeTempPtr(row);
}


// ROM 0x00343228 RgnBlt__FP8PixelMapT1P4RectT3lPP8PixelMapPP6RegionN27
// The transfer clipped by three regions: the destination rectangle is cut
// to the map's bounds and the regions' boxes (nothing to do when that is
// empty), and, when every region is a rectangle, BitBlt does the rest (a
// single non-rectangular region first trims the rectangle, TrimRect);
// otherwise each region is scan-converted over the rectangle and the
// rows are masked.  A negative mode draws nothing.
void
RgnBlt(PixelMap* src, PixelMap* dst, const Rect* srcRect, const Rect* dstRect, long mode, PatternHandle pattern, RgnHandle clip1, RgnHandle clip2, RgnHandle clip3)
{
	if (mode < 0)
		return;
	Rect clipped;
	if (!RSect(&clipped, 5, &dst->bounds, dstRect, &(*clip1)->rgnBBox, &(*clip2)->rgnBBox, &(*clip3)->rgnBBox))
		return;
	RgnHandle clips[3] = { clip1, clip2, clip3 };
	long which = 0;
	for (long i = 0; i < 3; i++)
		if ((*clips[i])->rgnSize != kRectRgnSize)
			which |= 2 << i;
	if (which == 4)
	{
		long trimmed = TrimRect(clip2, &clipped);
		if (trimmed < 0)
			return;
		if (trimmed == 0)
			which = 0;
	}
	QDStartDrawing(dst, &clipped);					// (the screen: the rectangle drawn goes to the display when the drawing ends)
	if (which == 0)
	{
		Rect source = *srcRect;
		OffsetRect(&source, clipped.left - dstRect->left, clipped.top - dstRect->top);
		BitBlt(src, dst, &source, &clipped, mode, pattern);
		QDStopDrawing(dst, &clipped);
		return;
	}
	long words = ((clipped.right - clipped.left) >> 5) + 2;
	RgnState states[3];
	RgnState* masks[3];
	char* scans[3] = { nil, nil, nil };
	long count = 0;
	// every buffer first: the states point into the regions' blocks, which
	// an allocation may move (the heap compacts handles)
	long* row = (long*) QDNewTempPtr((clipped.right - clipped.left) * sizeof(long));
	for (long i = 0; i < 3; i++)
		if (which & (2 << i))
			scans[i] = (char*) QDNewTempPtr(words * sizeof(ULong32));
	for (long i = 0; i < 3; i++)
	{
		if (!(which & (2 << i)) || scans[i] == nil)
			continue;
		InitRgn(*clips[i], &states[i], clipped.left, clipped.right, clipped.left, scans[i]);
		masks[count++] = &states[i];
	}
	BlitPixels(src, dst, srcRect, dstRect, &clipped, mode, pattern, masks, count, row);
	for (long i = 0; i < 3; i++)
		if (scans[i] != nil)
			QDDisposeTempPtr(scans[i]);
	QDDisposeTempPtr(row);
	QDStopDrawing(dst, &clipped);
}


/*------------------------------------------------------------------------------
	B i t s
------------------------------------------------------------------------------*/

// ROM 0x002ada5c StretchBits__FP8PixelMapT1P4RectT3lPP6RegionN26
// Bits copied between maps under the mode, clipped by two regions and a
// mask.  NOT YET RECONSTRUCTED: the ROM's stretching and depth conversion
// tables; rectangles of different sizes are sampled nearest-neighbour
// through a temporary map (DEVIATION).
void
StretchBits(PixelMap* src, PixelMap* dst, const Rect* srcRect, const Rect* dstRect, long mode, RgnHandle clip1, RgnHandle clip2, RgnHandle mask)
{
	long srcWidth = srcRect->right - srcRect->left, srcHeight = srcRect->bottom - srcRect->top;
	long dstWidth = dstRect->right - dstRect->left, dstHeight = dstRect->bottom - dstRect->top;
	if (srcWidth == dstWidth && srcHeight == dstHeight)
	{
		RgnBlt(src, dst, srcRect, dstRect, mode, nil, clip1, clip2, mask);
		return;
	}
	if (dstWidth <= 0 || dstHeight <= 0 || srcWidth <= 0 || srcHeight <= 0)
		return;
	PixelMap scaled;
	scaled.rowBytes = (short) (((dstWidth * PixelMapDepth(src) + 15) / 16) * 2);
	SetRect(&scaled.bounds, 0, 0, dstWidth, dstHeight);
	scaled.pixMapFlags = kPixMapPtr | PixelMapDepth(src);
	scaled.deviceRes = src->deviceRes;
	scaled.grayTable = nil;
	scaled.baseAddr = (Ptr) QDNewTempPtr(scaled.rowBytes * dstHeight);
	if (scaled.baseAddr == nil)
		return;
	memset(scaled.baseAddr, 0, scaled.rowBytes * dstHeight);
	for (long y = 0; y < dstHeight; y++)
		for (long x = 0; x < dstWidth; x++)
			SetPixel(&scaled, x, y, GetPixel(src, srcRect->left + x * srcWidth / dstWidth, srcRect->top + y * srcHeight / dstHeight));
	RgnBlt(&scaled, dst, &scaled.bounds, dstRect, mode, nil, clip1, clip2, mask);
	QDDisposeTempPtr(scaled.baseAddr);
}


// ROM 0x002ad664 StdBits
// The standard bits proc: recorded into an open picture, then drawn into
// the current port (unless the pen is hidden), clipped by its visible and
// clip regions and the mask.
//
// What is recorded is the part of the pixels the source rectangle takes:
// a copy of the pixel map's bounds cut down to it - the top and bottom as
// they are, the left moved to a whole byte, the right to eight pixels past
// the left - its row bytes worked out from its width (in halfwords, the
// per-depth tables), and BitsRect (0x90) or BitsRgn (0x91, with the mask)
// written, 8 more when the rows are to be packed (eight row bytes or
// more): a bitmap's row bytes and bounds or a pixel map and its gray
// table, the two rectangles, the mode, the mask, then the rows as they
// are or packed, a count byte in front of each.
//
// ROM BUG, kept: a packed row's count is always one byte, where Apple's
// format (and GetPicBits) has a word from 251 row bytes up - a row that
// packs to more than 255 bytes is recorded with its count cut short.
void
StdBits(PixelMap* src, Rect* srcRect, Rect* dstRect, long mode, RgnHandle mask)
{
	GrafPort* port = GetCurrentPort();
	if (CheckPic())
	{
		long srcRowBytes = src->rowBytes;
		const char* bits = (const char*) GetPixelMapBits(src);
		long depth = src->pixMapFlags & 0xff;
		PixelMap part;
		part.bounds = src->bounds;
		long skip = srcRect->top - part.bounds.top;
		if (skip > 0)
		{
			part.bounds.top = (short) (part.bounds.top + skip);
			bits += srcRowBytes * skip;
		}
		if (srcRect->bottom < part.bounds.bottom)
			part.bounds.bottom = srcRect->bottom;
		skip = srcRect->left - part.bounds.left;
		if (skip > 0)
		{
			long shift = kDepthPixelsPerByteShift[depth];
			skip >>= shift;
			bits += skip;
			part.bounds.left = (short) (part.bounds.left + (skip << shift));
		}
		long right = ((srcRect->right - part.bounds.left + 7) & ~7) + part.bounds.left;
		if (right < part.bounds.right)
			part.bounds.right = (short) right;
		long halfwords = (part.bounds.right - part.bounds.left + (kDepthPixelsPerWordMask[depth] >> 1))
					   >> (kDepthPixelsPerWordShift[depth] - 1);
		if (halfwords > 0)
		{
			long rowBytes = halfwords * 2;
			part.rowBytes = (short) rowBytes;
			long opcode = mask != nil ? 0x91 : 0x90;
			if (rowBytes >= 8)
				opcode += 8;
			PutPicOpcode(opcode);
			part.deviceRes = src->deviceRes;
			part.pixMapFlags = src->pixMapFlags;
			part.grayTable = src->grayTable;
			if (depth > 1)
			{
				PutPixMap(&part);
				PutGrayTable(&part);
			}
			else
			{
				PutPicWord(part.rowBytes);
				PutPicWord(part.bounds.top);
				PutPicWord(part.bounds.left);
				PutPicWord(part.bounds.bottom);
				PutPicWord(part.bounds.right);
			}
			PutPicWord(srcRect->top);
			PutPicWord(srcRect->left);
			PutPicWord(srcRect->bottom);
			PutPicWord(srcRect->right);
			PutPicWord(dstRect->top);
			PutPicWord(dstRect->left);
			PutPicWord(dstRect->bottom);
			PutPicWord(dstRect->right);
			PutPicWord(mode);
			if (mask != nil)
				PutPicRgn(mask);
			long rows = part.bounds.bottom - part.bounds.top;
			if (rowBytes < 8)
			{
				for ( ; rows > 0; rows--)
				{
					PutPicData(bits, rowBytes & 0xff);
					bits += srcRowBytes;
				}
			}
			else
			{
				char packed[0x100];
				for ( ; rows > 0; rows--)
				{
					char* from = (char*) bits;
					char* to = packed;
					PackBits(&from, &to, rowBytes);
					long count = (to - packed) & 0xff;
					PutPicByte(count);
					PutPicData(packed, count);
					bits += srcRowBytes;
				}
			}
		}
	}
	if (port->pnVis >= 0)
		StretchBits(src, &port->portBits, srcRect, dstRect, mode, port->visRgn, port->clipRgn, mask != nil ? mask : wideHandle);
}


// ROM 0x002ad604 CallBits__FP8PixelMapP4RectT2lPP6Region
void
CallBits(PixelMap* src, const Rect* srcRect, const Rect* dstRect, long mode, RgnHandle mask)
{
	GrafPort* port = GetCurrentPort();
	BitsProcPtr proc = (port->grafProcs != nil && port->grafProcs->bitsProc != nil) ? port->grafProcs->bitsProc : StdBits;
	proc(src, (Rect*) srcRect, (Rect*) dstRect, mode, mask);
}


// ROM 0x002ae440 CopyBits__FP8PixelMapT1P4RectT3lPP6Region
// Bits copied into a map: through the port's bits proc when the map is
// the current port's (so that its regions clip), else straight in.
void
CopyBits(PixelMap* src, PixelMap* dst, const Rect* srcRect, const Rect* dstRect, long mode, RgnHandle mask)
{
	GrafPort* port = GetCurrentPort();
	if (port != nil && GetPixelMapBits(&port->portBits) == GetPixelMapBits(dst)
	 && port->portBits.bounds.left == dst->bounds.left && port->portBits.bounds.top == dst->bounds.top)
	{
		CallBits(src, srcRect, dstRect, mode, mask);
		return;
	}
	if (mask == nil)
		mask = wideHandle;
	StretchBits(src, dst, srcRect, dstRect, mode, wideHandle, wideHandle, mask);
}


/*------------------------------------------------------------------------------
	V e r b s
------------------------------------------------------------------------------*/

// ROM 0x00340c44 PushVerb__FUcPlPPP8PixelMap
// The mode and pattern a verb draws with.
void
PushVerb(GrafVerb verb, long* mode, PatternHandle* pattern)
{
	GrafPort* port = GetCurrentPort();
	switch (verb)
	{
	case erase:
		*mode = patCopy;
		*pattern = port->bgPat;
		break;
	case invert:
		*mode = patXor;
		*pattern = stdPatterns[blackPat];
		break;
	case fill:
		*mode = patCopy;
		*pattern = port->fgPat;
		break;
	default:				// frame, paint
		*mode = port->pnMode;
		*pattern = port->fgPat;
		break;
	}
}


// ROM 0x00340cc0 DrawRect__FP4RectlPP8PixelMap
// The rectangle filled in the current port under the mode and pattern,
// clipped by the port's regions; nothing while the pen is hidden.
void
DrawRect(const Rect* r, long mode, PatternHandle pattern)
{
	GrafPort* port = GetCurrentPort();
	if (port->pnVis < 0)
		return;
	RgnBlt(&port->portBits, &port->portBits, r, r, mode, pattern, port->visRgn, port->clipRgn, wideHandle);
}


// ROM 0x003409c8 FrRect__FP4Rect
// The rectangle's frame, the pen's width and height thick, in the pen's
// mode and pattern: the whole rectangle when the pen fills it, else its
// four sides.
void
FrRect(const Rect* r)
{
	GrafPort* port = GetCurrentPort();
	if (port->pnVis < 0)
		return;
	long mode = port->pnMode;
	PatternHandle pattern = port->fgPat;
	long innerLeft = r->left + port->pnSize.h;
	long innerRight = r->right - port->pnSize.h;
	long innerTop = r->top + port->pnSize.v;
	long innerBottom = r->bottom - port->pnSize.v;
	Rect side;
	if (innerLeft < innerRight && innerTop < innerBottom)
	{
		SetRect(&side, r->left, r->top, innerLeft, r->bottom);				// left
		RgnBlt(&port->portBits, &port->portBits, &side, &side, mode, pattern, port->visRgn, port->clipRgn, wideHandle);
		SetRect(&side, innerRight, r->top, r->right, r->bottom);			// right
		RgnBlt(&port->portBits, &port->portBits, &side, &side, mode, pattern, port->visRgn, port->clipRgn, wideHandle);
		SetRect(&side, innerLeft, r->top, innerRight, innerTop);			// top
		RgnBlt(&port->portBits, &port->portBits, &side, &side, mode, pattern, port->visRgn, port->clipRgn, wideHandle);
		SetRect(&side, innerLeft, innerBottom, innerRight, r->bottom);		// bottom
		RgnBlt(&port->portBits, &port->portBits, &side, &side, mode, pattern, port->visRgn, port->clipRgn, wideHandle);
		return;
	}
	side = *r;
	RgnBlt(&port->portBits, &port->portBits, &side, &side, mode, pattern, port->visRgn, port->clipRgn, wideHandle);
}


// ROM 0x003400b8 StdRect
// The standard rect proc: recorded into an open picture (the verb's pen
// state, then the rectangle: 0x30 + the verb); frame records the
// rectangle into an open region (PutRect) and draws the frame, the other
// verbs fill.
void
StdRect(GrafVerb verb, Rect* r)
{
	if (CheckPic())
	{
		PutPicVerb(verb);
		PutPicRect(0x30 + verb, r);
	}
	if (verb == frame)
	{
		if (GetCurrentPort()->rgnSave != nil)
			PutRect(r, qdGlobals.fRgnHandle, &qdGlobals.fRgnOffset, &qdGlobals.fRgnSize);
		FrRect(r);
		return;
	}
	long mode;
	PatternHandle pattern;
	PushVerb(verb, &mode, &pattern);
	DrawRect(r, mode, pattern);
}


// ROM 0x0034078c CallRect__FUcP4Rect
void
CallRect(GrafVerb verb, const Rect* r)
{
	GrafPort* port = GetCurrentPort();
	RectProcPtr proc = (port->grafProcs != nil && port->grafProcs->rectProc != nil) ? port->grafProcs->rectProc : StdRect;
	proc(verb, (Rect*) r);
}


// ROM 0x00340fec FrameRect__FP4Rect
void
FrameRect(const Rect* r)
{
	CallRect(frame, r);
}


// ROM 0x0034005c PaintRect__FP4Rect
void
PaintRect(const Rect* r)
{
	CallRect(paint, r);
}


// ROM 0x00340068 EraseRect__FP4Rect
void
EraseRect(const Rect* r)
{
	CallRect(erase, r);
}


// ROM 0x00340074 InvertRect__FP4Rect
void
InvertRect(const Rect* r)
{
	CallRect(invert, r);
}


// ROM 0x00340080 FillRect__FP4RectPP8PixelMap
// The pattern installed as the port's for the call.
void
FillRect(const Rect* r, PatternHandle pattern)
{
	GrafPort* port = GetCurrentPort();
	PatternHandle saved = port->fgPat;
	port->fgPat = pattern;
	CallRect(fill, r);
	port->fgPat = saved;
}


// ROM 0x00341860 DrawRgn__FPP6RegionlPP8PixelMap
// The region filled in the current port: the port's bits blitted onto
// themselves under the mode and pattern, clipped by the port's regions
// and the region itself.
void
DrawRgn(RgnHandle rgn, long mode, PatternHandle pattern)
{
	GrafPort* port = GetCurrentPort();
	if (port->pnVis < 0)
		return;
	RgnBlt(&port->portBits, &port->portBits, &port->portBits.bounds, &port->portBits.bounds, mode, pattern, port->visRgn, port->clipRgn, rgn);
}


// ROM 0x00341e28 FrRgn__FPP6RegionlPP8PixelMap
// The region's frame: a rectangular region's is FrRect's; otherwise the
// region less itself inset by the pen is drawn.
void
FrRgn(RgnHandle rgn, long mode, PatternHandle pattern)
{
	GrafPort* port = GetCurrentPort();
	if (port->pnVis < 0)
		return;
	if ((*rgn)->rgnSize == kRectRgnSize)
	{
		Rect r = (*rgn)->rgnBBox;
		FrRect(&r);
		return;
	}
	RgnHandle inner = NewRgn();
	CopyRgn(rgn, inner);
	InsetRgn(inner, port->pnSize.h, port->pnSize.v);
	DiffRgn(rgn, inner, inner);
	DrawRgn(inner, mode, pattern);
	DisposeRgn(inner);
}


// ROM 0x003415c4 StdRgn
// The standard region proc: recorded into an open picture (0x80 + the
// verb and the region); frame records the region into an open region
// (PutRgn) and draws its outline, the other verbs fill it.
void
StdRgn(GrafVerb verb, RgnHandle rgn)
{
	GrafPort* port = GetCurrentPort();
	if (CheckPic())
	{
		PutPicVerb(verb);
		PutPicOpcode(0x80 + verb);
		PutPicRgn(rgn);
	}
	long mode;
	PatternHandle pattern;
	PushVerb(verb, &mode, &pattern);
	if (verb == frame)
	{
		if (port->rgnSave != nil)
			PutRgn(rgn, qdGlobals.fRgnHandle, &qdGlobals.fRgnOffset, &qdGlobals.fRgnSize);
		FrRgn(rgn, port->pnMode, port->fgPat);
	}
	else
		DrawRgn(rgn, mode, pattern);
}


// ROM 0x00341774 CallRgn__FUcPP6Region
void
CallRgn(GrafVerb verb, RgnHandle rgn)
{
	GrafPort* port = GetCurrentPort();
	RgnProcPtr proc = (port->grafProcs != nil && port->grafProcs->rgnProc != nil) ? port->grafProcs->rgnProc : StdRgn;
	proc(verb, rgn);
}


// ROM 0x00341414 FrameRgn__FPP6Region
void
FrameRgn(RgnHandle rgn)
{
	CallRgn(frame, rgn);
}


// ROM 0x00341420 PaintRgn__FPP6Region
void
PaintRgn(RgnHandle rgn)
{
	CallRgn(paint, rgn);
}


// ROM 0x003414b4 EraseRgn__FPP6Region
void
EraseRgn(RgnHandle rgn)
{
	CallRgn(erase, rgn);
}


// ROM 0x003414c0 InvertRgn__FPP6Region
void
InvertRgn(RgnHandle rgn)
{
	CallRgn(invert, rgn);
}


// ROM 0x003414cc FillRgn__FPP6RegionPP8PixelMap
void
FillRgn(RgnHandle rgn, PatternHandle pattern)
{
	GrafPort* port = GetCurrentPort();
	PatternHandle saved = port->fgPat;
	port->fgPat = pattern;
	CallRgn(fill, rgn);
	port->fgPat = saved;
}


/*------------------------------------------------------------------------------
	T h e   i n k e r ' s   l i n e
------------------------------------------------------------------------------*/

// ROM 0x002f7c3c InkerLine__FC5PointT1P4RectT1
// The screen's map when none is named.
void
InkerLine(const Point from, const Point to, Rect* damaged, const Point pen)
{
	InkerLine(from, to, damaged, pen, &qdGlobals.fScreenBits);
}


// ROM 0x002f7c64 InkerLine__FC5PointT1P4RectT1PC8PixelMap
// The segment between two pen samples, inked into the map.  There is no
// port, no pen state and no clipping region: the inker runs at interrupt
// time behind the view system's back, so it walks the pixels itself and
// stops at the map's own edge.
//
// The nib is a rectangle `pen` wide and tall whose top left follows the
// line, so what is drawn is the parallelogram the nib sweeps out.  That
// is why there are two x accumulators rather than one: `left` follows the
// leading edge and `right` the trailing one, both stepped by the same
// dx/dy each row, and the two are pulled apart at the start by the pen's
// width - which way round depends on the sign of the slope, because a
// line going right has its left edge at the bottom of the nib and one
// going left has it at the top.
//
// A horizontal segment has no rows to walk, so it is drawn as a single
// row from the leftmost of the two points to the clipped right edge.
//
// `damaged` comes back as the part of the map drawn on - the line's box
// grown by the nib, cut to the map - and is empty (and nothing drawn)
// when the line is off the map altogether.
//
// (The ROM fills each row a word at a time through three tables of
// qdConstants indexed by the map's depth - a mask for the first word, one
// for the last and all-ones between.  The reconstruction sets the pixels
// one at a time, as the rest of the blitter does; the result is the same
// run of pixels ORed to black.)
void
InkerLine(const Point from, const Point to, Rect* damaged, const Point pen, const PixelMap* map)
{
	Rect box;
	Pt2Rect(from, to, &box);
	box.right = (short) (box.right + pen.h);
	box.bottom = (short) (box.bottom + pen.v);
	if (!RSect(damaged, 2, &map->bounds, &box))
		return;
	Rect clip = *damaged;

	// The arithmetic below goes through Ports.h's ToFixed/AddFixed/
	// ScaleFixed rather than the plain operators: every one of these
	// numbers can be negative (a line drawn leftwards or upwards, a point
	// off the left of the screen) and the slope of a nearly horizontal
	// line comes back from FixedDivide saturated to 0x7fffffff, so the
	// shifts, the additions and the multiply all overflow in the ordinary
	// course of inking.  The ARM wraps; C++ has nothing to say about it.
	Fixed slope;				// dx/dy, 16.16
	Fixed left, right;			// where the nib's two edges are on the row, 16.16
	if (from.v == to.v)
	{
		slope = 0;
		left = AddFixed(0x8000, ToFixed(from.h >= to.h ? to.h : from.h));
		right = AddFixed(0x8000, ToFixed(clip.right));
	}
	else
	{
		Point upper = from;
		Point lower = to;
		if (from.v > to.v)
		{
			upper = to;
			lower = from;
		}
		left = AddFixed(0x8000, ToFixed(upper.h));
		right = AddFixed(left, ToFixed(pen.h));
		slope = FixedDivide(ToFixed(lower.h - upper.h), ToFixed(lower.v - upper.v));
		Fixed lean = ScaleFixed(slope, pen.v);	// how far the nib leans over its own height
		left = AddFixed(left, slope >> 1);
		right = AddFixed(right, slope >> 1);
		if (slope >= 0)
		{
			left = AddFixed(left, -lean);
			if (slope >= 0x10000)
				right = AddFixed(right, -0x10000);
			else
				left = AddFixed(left, slope);
		}
		else
		{
			right = AddFixed(right, -lean);
			if (slope < -0x10000)
				left = AddFixed(left, 0x10000);
			else
				right = AddFixed(right, slope);
		}
		if (box.top != clip.top)
		{
			// the rows above the clip stepped over
			Fixed skip = ScaleFixed(slope, clip.top - box.top);
			left = AddFixed(left, skip);
			right = AddFixed(right, skip);
		}
	}

	long depth = PixelMapDepth(map);
	long black = (1L << depth) - 1;
	QDStartDrawing((PixelMap*) map, damaged);
	for (long y = clip.top; y < clip.bottom; y++)
	{
		long a = left >> 16;
		if (a < clip.left)
			a = clip.left;
		long b = right >> 16;
		if (b > clip.right)
			b = clip.right;
		for (long x = a; x < b; x++)
			SetPixel((PixelMap*) map, x, y, GetPixel(map, x, y) | black);
		left = AddFixed(left, slope);
		right = AddFixed(right, slope);
	}
	QDStopDrawing((PixelMap*) map, damaged);
}
