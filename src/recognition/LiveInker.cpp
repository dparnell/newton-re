/*
	File:		recognition/LiveInker.cpp

	Contains:	TLiveInker, the inker's drawing of the pen's trail
				(recognition/LiveInker.h).

				Reconstructed from the MP2x00 US ROM (0x00113840-0x00113cac);
				each function cites its origin.
*/

#include "LiveInker.h"
#include "Rects.h"
#include "Draw.h"
#include "Screen.h"
#include "Ports.h"
#include "NewtonMemory.h"
#include "OSErrors.h"

extern const unsigned char	qdDepthShift[9];		// LiveInkerTables.cpp: qdConstants+0x65, log2 of the pixels in a byte by depth


// ROM 0x00113840 __ct__10TLiveInkerFv
TLiveInker::TLiveInker()
{
	fBuffer = nil;
}


// ROM 0x00113870 __dt__10TLiveInkerFv
TLiveInker::~TLiveInker()
{
	DisposPtr(fBuffer);
}


// ROM 0x001138a0 Init__10TLiveInkerFv
// The tile's buffer for the screen's depth: 64 rows of 64 pixels, and the
// tile a pointer map of that depth over it.
long
TLiveInker::Init(void)
{
	ULong depth = qdGlobals.fScreenBits.pixMapFlags & 0xff;
	fDepthShift = qdDepthShift[depth];
	fBufferSize = (0x40 >> fDepthShift) << 6;
	fBuffer = NewPtr(fBufferSize);
	if (fBuffer == nil)
		return kError_No_Memory;
	fMap.baseAddr = fBuffer;
	fMap.pixMapFlags = depth + kPixMapPtr;
	fMap.deviceRes.v = kDefaultDPI;
	fMap.deviceRes.h = kDefaultDPI;
	fMap.grayTable = nil;
	return noErr;
}


// ROM 0x00113928 ResetAccumulator__10TLiveInkerFv
// Nothing added yet: the extent inside out, so the first point is not
// inside it; and the screen driver's alignment read afresh.
void
TLiveInker::ResetAccumulator(void)
{
	SetRect(&fExtent, 0x7fff, 0x7fff, (short) 0x8000, (short) 0x8000);
	fCount = 0;
	ScreenInfo info;
	GetGrafInfo(7, &info);
	fAlignV = info.fAlignV;
	fAlignH = info.fAlignH;
}


// ROM 0x00113980 AddPoint__10TLiveInkerFC5PointT1
// A point (and the pen's size below and to the right of it) added to the
// extent.  A point already inside it is simply counted; otherwise, from
// the third point on, the grown extent must still fit the buffer once
// aligned, and if it does not the point is not added.  ==> whether it was.
Boolean
TLiveInker::AddPoint(const Point pt, const Point pen)
{
	long top = pt.v, left = pt.h;
	long bottom = top + pen.v, right = left + pen.h;
	if (fExtent.top <= top && fExtent.left <= left && fExtent.bottom >= bottom && fExtent.right >= right)
	{
		fCount++;
		return true;
	}
	Rect grown;
	grown.top = (short) (fExtent.top < top ? fExtent.top : top);
	grown.left = (short) (fExtent.left < left ? fExtent.left : left);
	grown.bottom = (short) (fExtent.bottom > bottom ? fExtent.bottom : bottom);
	grown.right = (short) (fExtent.right > right ? fExtent.right : right);
	Boolean fits = true;
	if (fCount > 1)
	{
		fits = MapLCDExtent(&grown, nil);
		if (!fits)
			return false;
	}
	fExtent = grown;
	fCount++;
	return fits;
}


// ROM 0x00113aac MapLCDExtent__10TLiveInkerFPC4RectP4Rect
// The extent widened to the screen driver's alignment (the top and left
// down, the bottom and right up); if the tile that makes fits the buffer
// it is answered in aligned, otherwise aligned is a 64-pixel square from
// the aligned top left.  ==> whether it fits.
Boolean
TLiveInker::MapLCDExtent(const Rect* extent, Rect* aligned)
{
	Long32 top = (Long32) extent->top & -(Long32) fAlignV;
	Long32 bottom = ((Long32) fAlignV + extent->bottom - 1) & -(Long32) fAlignV;
	Long32 left = (Long32) extent->left & -(Long32) fAlignH;
	Long32 right = ((Long32) fAlignH + extent->right - 1) & -(Long32) fAlignH;
	Long32 bytes = (bottom - top) * ((right - left) >> qdDepthShift[fMap.pixMapFlags & 0xff]);
	if (bytes <= fBufferSize)
	{
		if (aligned != nil)
		{
			aligned->top = (short) top;
			aligned->left = (short) left;
			aligned->bottom = (short) bottom;
			aligned->right = (short) right;
		}
		return true;
	}
	if (aligned != nil)
		SetRect(aligned, (short) left, (short) top, (short) (left + 0x40), (short) (top + 0x40));
	return false;
}


// ROM 0x00113b9c StartLiveInk__10TLiveInkerFv
// The tile placed over the aligned extent, its row bytes worked out from
// that width, then cut to the screen and cleared.  (ROM: the row bytes
// stay those of the uncut width, so a tile cut at the screen's left or
// right edge keeps rows wider than it needs - harmless.)
void
TLiveInker::StartLiveInk(void)
{
	MapLCDExtent(&fExtent, &fMap.bounds);
	fMap.rowBytes = (short) ((fMap.bounds.right - fMap.bounds.left) >> qdDepthShift[fMap.pixMapFlags & 0xff]);
	if (!RSect(&fMap.bounds, 2, &fMap.bounds, &qdGlobals.fScreenBits.bounds))
		return;
	ZeroBytes(fBuffer, fMap.rowBytes * (fMap.bounds.bottom - fMap.bounds.top));
}


// ROM 0x00113c3c InkLine__10TLiveInkerFC5PointN21
void
TLiveInker::InkLine(const Point from, const Point to, const Point pen)
{
	Rect damaged;
	InkerLine(from, to, &damaged, pen, &fMap);
}


// ROM 0x00113c74 StopLiveInk__10TLiveInkerFv
// The tile ORed onto the screen, the LCD's own activity held off meanwhile.
void
TLiveInker::StopLiveInk(void)
{
	BlockLCDActivity(true);
	BlitToScreens(&fMap, &fMap.bounds, &fMap.bounds, srcOr);
	BlockLCDActivity(false);
}
