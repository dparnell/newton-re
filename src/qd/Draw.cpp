/*
	File:		qd/Draw.cpp

	Contains:	Drawing rectangles and regions, and the blitter.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
	The blitter is written a pixel at a time (Draw.h says why); the ROM's
	transfer semantics - the source inverted for the notSrc/notPat modes,
	copy, or (a non-white gray source pixel replacing the destination's),
	xor and bic - are kept, as is the clipping: the destination rectangle
	cut to the map's bounds and the clip regions' boxes, and each pixel
	row masked by the regions' scan lines (RgnState).
*/

#include "Draw.h"
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
BlitPixels(PixelMap* src, PixelMap* dst, const Rect* srcRect, const Rect* dstRect, const Rect* clipped, long mode, PatternHandle pattern, RgnState** masks, long maskCount)
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
	if (width <= 0 || clipped->top >= clipped->bottom)
		return;
	long* row = (long*) QDNewTempPtr(width * sizeof(long));
	if (row == nil)
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
	QDDisposeTempPtr(row);
}


// ROM 0x00287e20 BitBlt__FP8PixelMapT1P4RectT3lPP8PixelMap
// The transfer without region clipping: dstRect is drawn as it is (the
// caller has clipped it) from the corresponding pixels of srcRect.
void
BitBlt(PixelMap* src, PixelMap* dst, const Rect* srcRect, const Rect* dstRect, long mode, PatternHandle pattern)
{
	BlitPixels(src, dst, srcRect, dstRect, dstRect, mode, pattern, nil, 0);
}


// ROM 0x003172e0 RgnBlt__FP8PixelMapT1P4RectT3lPP8PixelMapPP6RegionN27
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
	if (which == 0)
	{
		Rect source = *srcRect;
		OffsetRect(&source, clipped.left - dstRect->left, clipped.top - dstRect->top);
		BitBlt(src, dst, &source, &clipped, mode, pattern);
		return;
	}
	long words = ((clipped.right - clipped.left) >> 5) + 2;
	RgnState states[3];
	RgnState* masks[3];
	char* scans[3] = { nil, nil, nil };
	long count = 0;
	for (long i = 0; i < 3; i++)
	{
		if (!(which & (2 << i)))
			continue;
		scans[i] = (char*) QDNewTempPtr(words * sizeof(ULong32));
		if (scans[i] == nil)
			break;
		InitRgn(*clips[i], &states[i], clipped.left, clipped.right, clipped.left, scans[i]);
		masks[count++] = &states[i];
	}
	BlitPixels(src, dst, srcRect, dstRect, &clipped, mode, pattern, masks, count);
	for (long i = 0; i < 3; i++)
		if (scans[i] != nil)
			QDDisposeTempPtr(scans[i]);
}


/*------------------------------------------------------------------------------
	B i t s
------------------------------------------------------------------------------*/

// ROM 0x00288eb4 StretchBits__FP8PixelMapT1P4RectT3lPP6RegionN26
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


// ROM 0x00288abc StdBits
// The standard bits proc: into the current port, clipped by its visible
// and clip regions and the mask.  NOT YET RECONSTRUCTED: recording into
// an open picture.
void
StdBits(PixelMap* src, Rect* srcRect, Rect* dstRect, long mode, RgnHandle mask)
{
	GrafPort* port = GetCurrentPort();
	StretchBits(src, &port->portBits, srcRect, dstRect, mode, port->visRgn, port->clipRgn, mask != nil ? mask : wideHandle);
}


// ROM 0x00288a5c CallBits__FP8PixelMapP4RectT2lPP6Region
void
CallBits(PixelMap* src, const Rect* srcRect, const Rect* dstRect, long mode, RgnHandle mask)
{
	GrafPort* port = GetCurrentPort();
	BitsProcPtr proc = (port->grafProcs != nil && port->grafProcs->bitsProc != nil) ? port->grafProcs->bitsProc : StdBits;
	proc(src, (Rect*) srcRect, (Rect*) dstRect, mode, mask);
}


// ROM 0x00289898 CopyBits__FP8PixelMapT1P4RectT3lPP6Region
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

// ROM 0x00314cfc PushVerb__FUcPlPPP8PixelMap
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


// ROM 0x00314d78 DrawRect__FP4RectlPP8PixelMap
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


// ROM 0x00314a80 FrRect__FP4Rect
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


// ROM 0x00314170 StdRect
// The standard rect proc: frame draws the frame (and records into an open
// region, NOT YET), the other verbs fill.  NOT YET RECONSTRUCTED:
// recording into an open picture.
void
StdRect(GrafVerb verb, Rect* r)
{
	if (verb == frame)
	{
		FrRect(r);
		return;
	}
	long mode;
	PatternHandle pattern;
	PushVerb(verb, &mode, &pattern);
	DrawRect(r, mode, pattern);
}


// ROM 0x00314844 CallRect__FUcP4Rect
void
CallRect(GrafVerb verb, const Rect* r)
{
	GrafPort* port = GetCurrentPort();
	RectProcPtr proc = (port->grafProcs != nil && port->grafProcs->rectProc != nil) ? port->grafProcs->rectProc : StdRect;
	proc(verb, (Rect*) r);
}


// ROM 0x003150a4 FrameRect__FP4Rect
void
FrameRect(const Rect* r)
{
	CallRect(frame, r);
}


// ROM 0x00314114 PaintRect__FP4Rect
void
PaintRect(const Rect* r)
{
	CallRect(paint, r);
}


// ROM 0x00314120 EraseRect__FP4Rect
void
EraseRect(const Rect* r)
{
	CallRect(erase, r);
}


// ROM 0x0031412c InvertRect__FP4Rect
void
InvertRect(const Rect* r)
{
	CallRect(invert, r);
}


// ROM 0x00314138 FillRect__FP4RectPP8PixelMap
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


// ROM 0x00315918 DrawRgn__FPP6RegionlPP8PixelMap
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


// ROM 0x00315ee0 FrRgn__FPP6RegionlPP8PixelMap
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


// ROM 0x0031567c StdRgn
// The standard region proc.  NOT YET RECONSTRUCTED: recording into an
// open picture or region.
void
StdRgn(GrafVerb verb, RgnHandle rgn)
{
	GrafPort* port = GetCurrentPort();
	long mode;
	PatternHandle pattern;
	PushVerb(verb, &mode, &pattern);
	if (verb == frame)
		FrRgn(rgn, port->pnMode, port->fgPat);
	else
		DrawRgn(rgn, mode, pattern);
}


// ROM 0x0031582c CallRgn__FUcPP6Region
void
CallRgn(GrafVerb verb, RgnHandle rgn)
{
	GrafPort* port = GetCurrentPort();
	RgnProcPtr proc = (port->grafProcs != nil && port->grafProcs->rgnProc != nil) ? port->grafProcs->rgnProc : StdRgn;
	proc(verb, rgn);
}


// ROM 0x003154cc FrameRgn__FPP6Region
void
FrameRgn(RgnHandle rgn)
{
	CallRgn(frame, rgn);
}


// ROM 0x003154d8 PaintRgn__FPP6Region
void
PaintRgn(RgnHandle rgn)
{
	CallRgn(paint, rgn);
}


// ROM 0x0031556c EraseRgn__FPP6Region
void
EraseRgn(RgnHandle rgn)
{
	CallRgn(erase, rgn);
}


// ROM 0x00315578 InvertRgn__FPP6Region
void
InvertRgn(RgnHandle rgn)
{
	CallRgn(invert, rgn);
}


// ROM 0x00315584 FillRgn__FPP6RegionPP8PixelMap
void
FillRgn(RgnHandle rgn, PatternHandle pattern)
{
	GrafPort* port = GetCurrentPort();
	PatternHandle saved = port->fgPat;
	port->fgPat = pattern;
	CallRgn(fill, rgn);
	port->fgPat = saved;
}
