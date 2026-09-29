/*
	File:		text/TXFrames.cpp

	Contains:	The frames (TXFrames.h).

	Reconstructed from the MP2x00 US ROM (0x00239e0c-0x0023ac54,
	0x002413e0-0x00241598, 0x00241774, 0x0024282c-0x00242a2c); each
	function cites its origin.
*/

#include "TXFrames.h"
#include "Rects.h"


// ROM 0x0023ab48 __ct__16TXDisplayChangesFv
TXDisplayChanges::TXDisplayChanges()
{
	fFlags = 0;
	fFormatStart = 0x7fffffff;
	fFormatEnd = 0;
}


// ROM 0x0023ab80 GetFormatRange__16TXDisplayChangesCFP12TXOffsetPair
void
TXDisplayChanges::GetFormatRange(TXOffsetPair* range) const
{
	range->fStart = fFormatStart;
	range->fEnd = fFormatEnd;
}


// ROM 0x00239e0c GetNextFrame__12TXSectFramesFv
long
TXSectFrames::GetNextFrame(void)
{
	long current = fCurrent;
	if (current == -2)
		return -1;
	if (fListCount >= 0)
	{
		if (fListCount <= current)
			return -1;
		fCurrent = current + 1;
		return fList[current];
	}
	if (current < 0)
		fCurrent = fFirst;
	else
	{
		fInRow = fInRow + 1;
		if (fInRow == fPerRow)
		{
			fCurrent = (fStride - fPerRow) + current + 1;
			fInRow = 0;
		}
		else
			fCurrent = current + 1;
	}
	if (fLast < fCurrent)
		return -1;
	return fCurrent;
}


// ROM 0x00239eac SetUniform__12TXSectFramesFlN31
void
TXSectFrames::SetUniform(long first, long perRow, long last, long stride)
{
	fFirst = first;
	fPerRow = perRow;
	fLast = last;
	fStride = stride;
	fInRow = 0;
	fCurrent = -1;
	fListCount = -1;
}


// ROM 0x0023ab90 __ct__8TXFramesFv
TXFrames::TXFrames()
{
	fFormatter = nil;
	SetRect(&fMargins, 0, 0, 0, 0);
	fScrollV = 0;
	fScrollH = 0;
	fDrawOriginH = 0;
	fDrawOriginV = 0;
	fFramesOriginH = 0;
	fFramesOriginV = 0;
}


// ROM 0x0023ac08 __dt__8TXFramesFv
TXFrames::~TXFrames()
{
	if (fFormatter != nil)
		delete fFormatter;
}


// ROM 0x00239ecc FreeData__8TXFramesFv
void
TXFrames::FreeData(void)
{
	fFormatter->FreeData();
}


// ROM 0x00239ed8 InvalFramePart__8TXFramesFliT1PP6Region
// Part 1 is the whole frame, its margins included.
void
TXFrames::InvalFramePart(long frame, int parts, long /*unused*/, RgnHandle rgn)
{
	if ((parts & 1) == 0)
		return;
	Rect r;
	GetFrameBounds(frame, &r);
	TXInvalSectRect(&r, rgn);
}


// ROM 0x00239f0c HAbsToDraw__8TXFramesCFl
short
TXFrames::HAbsToDraw(long h) const
{
	return (short) TXClipValue(((h - fScrollH) - fDrawOriginH) + fFramesOriginH, -0x7fff, 0x7fff);
}


// ROM 0x00239f50 VAbsToDraw__8TXFramesCFl
short
TXFrames::VAbsToDraw(long v) const
{
	return (short) TXClipValue(((v - fScrollV) - fDrawOriginV) + fFramesOriginV, -0x7fff, 0x7fff);
}


// ROM 0x00239f94 HDrawToAbs__8TXFramesCFl
long
TXFrames::HDrawToAbs(long h) const
{
	return (fScrollH + h + fDrawOriginH) - fFramesOriginH;
}


// ROM 0x00239fb0 VDrawToAbs__8TXFramesCFl
long
TXFrames::VDrawToAbs(long v) const
{
	return (fScrollV + v + fDrawOriginV) - fFramesOriginV;
}


// ROM 0x00239fcc AbsToDraw__8TXFramesCFRC10TXLongRectP4Rect
void
TXFrames::AbsToDraw(const TXLongRect& abs, Rect* draw) const
{
	draw->top = VAbsToDraw(abs.top);
	draw->bottom = VAbsToDraw(abs.bottom);
	draw->left = HAbsToDraw(abs.left);
	draw->right = HAbsToDraw(abs.right);
}


// ROM 0x0023a044 DrawToAbs__8TXFramesCFRC4RectP10TXLongRect
void
TXFrames::DrawToAbs(const Rect& draw, TXLongRect* abs) const
{
	abs->top = VDrawToAbs(draw.top);
	abs->bottom = VDrawToAbs(draw.bottom);
	abs->left = HDrawToAbs(draw.left);
	abs->right = HDrawToAbs(draw.right);
}


// ROM 0x0023a0ac AbsToDraw__8TXFramesCFRC11TXLongPoint
Point
TXFrames::AbsToDraw(const TXLongPoint& abs) const
{
	Point pt;
	pt.h = HAbsToDraw(abs.h);
	pt.v = VAbsToDraw(abs.v);
	return pt;
}


// ROM 0x0023a104 DrawToAbs__8TXFramesCF5PointP11TXLongPoint
void
TXFrames::DrawToAbs(Point draw, TXLongPoint* abs) const
{
	abs->v = VDrawToAbs(draw.v);
	abs->h = HDrawToAbs(draw.h);
}


// ROM 0x0023a140 SetFramesMargins__8TXFramesFRC4RectP16TXDisplayChanges
void
TXFrames::SetFramesMargins(const Rect& margins, TXDisplayChanges* changes)
{
	unsigned long flags;
	if (!EqualRect(&margins, &fMargins))
	{
		fMargins = margins;
		flags = 0x18;
	}
	else
		flags = 0;
	if (changes != nil)
		changes->fFlags |= flags;
}


// ROM 0x0023a18c GetFramesMargins__8TXFramesCFP4Rect
void
TXFrames::GetFramesMargins(Rect* margins) const
{
	*margins = fMargins;
}


// ROM 0x0023a19c GetAbsFrameBounds__8TXFramesCFlP10TXLongRect
void
TXFrames::GetAbsFrameBounds(long frame, TXLongRect* bounds) const
{
	GetAbsTextBounds(frame, bounds);
	Rect margins;
	GetFramesMargins(&margins);
	bounds->top -= margins.top;
	bounds->left -= margins.left;
	bounds->bottom += margins.bottom;
	bounds->right += margins.right;
}


// ROM 0x0023a218 GetAbsTextBounds__8TXFramesCFlP10TXLongRect
// The text's size from the margins' top left.
void
TXFrames::GetAbsTextBounds(long frame, TXLongRect* bounds) const
{
	TXLongPoint size;
	GetTextBoundsSize(&size, frame);
	Rect margins;
	GetFramesMargins(&margins);
	bounds->top = margins.top;
	bounds->left = margins.left;
	bounds->bottom = margins.top + size.v;
	bounds->right = margins.left + size.h;
}


// ROM 0x0023a290 GetTextBounds__8TXFramesCFlP4Rect
void
TXFrames::GetTextBounds(long frame, Rect* bounds) const
{
	TXLongRect abs;
	GetAbsTextBounds(frame, &abs);
	AbsToDraw(abs, bounds);
}


// ROM 0x0023a2cc GetFrameBounds__8TXFramesCFlP4Rect
void
TXFrames::GetFrameBounds(long frame, Rect* bounds) const
{
	TXLongRect abs;
	GetAbsFrameBounds(frame, &abs);
	AbsToDraw(abs, bounds);
}


// ROM 0x0023a300 FramesScrolled__8TXFramesFlT1
void
TXFrames::FramesScrolled(long dh, long dv)
{
	fScrollH = fScrollH - dh;
	fScrollV = fScrollV - dv;
}


// ROM 0x0023a31c SetDrawOrigin__8TXFramesFlT1
void
TXFrames::SetDrawOrigin(long h, long v)
{
	fDrawOriginH = h;
	fDrawOriginV = v;
}


// ROM 0x0023a32c SetFramesOrigin__8TXFramesFlT1
void
TXFrames::SetFramesOrigin(long h, long v)
{
	fFramesOriginH = h;
	fFramesOriginV = v;
}


// ROM 0x0023a33c Draw__8TXFramesCFl
void
TXFrames::Draw(long /*frame*/) const
{ }


// ROM 0x0023a340 PointToLine__8TXFramesCF5PointPUcT2
long
TXFrames::PointToLine(Point pt, unsigned char* outside, unsigned char* past) const
{
	TXLongPoint abs;
	DrawToAbs(pt, &abs);
	long frame = PointToFrame(abs, outside);
	*past = *outside;
	TXOffsetPair lines;
	if (frame < 0 || !fFormatter->GetFrameLineRange(frame, &lines))
		return -1;
	TXLongRect text;
	GetAbsTextBounds(frame, &text);
	long pixels = abs.v - text.top;
	if (pixels < 0)
	{
		// above the frame: the last line of the frame before
		if (frame != 0)
		{
			*past = 1;
			fFormatter->GetFrameLineRange(frame - 1, &lines);
			return lines.fEnd;
		}
		pixels = 0;
	}
	long line = fFormatter->PixelToLine(&pixels, lines.fStart, nil, nil);
	if (lines.fEnd < line)
	{
		*past = 1;
		line = lines.fEnd;
	}
	return line;
}


// ROM 0x0023a454 GetLineBounds__8TXFramesCFlP10TXLongRect
// The line's band across its frame's text.
Boolean
TXFrames::GetLineBounds(long line, TXLongRect* bounds) const
{
	long frame = fFormatter->LineToFrame(line, false);
	if (frame < 0)
		return false;
	GetAbsTextBounds(frame, bounds);
	TXOffsetPair lines;
	fFormatter->GetFrameLineRange(frame, &lines);
	if (lines.fStart < line)
		bounds->top = fFormatter->GetLinesHeight(lines.fStart, line - 1) + bounds->top;
	bounds->bottom = fFormatter->GetLinesHeight(line, line) + bounds->top;
	return true;
}


// ROM 0x0023a524 GetLineBounds__8TXFramesCFlP4Rect
Boolean
TXFrames::GetLineBounds(long line, Rect* bounds) const
{
	TXLongRect abs;
	Boolean found = GetLineBounds(line, &abs);
	if (found)
		AbsToDraw(abs, bounds);
	return found;
}


// ROM 0x0023a564 PointToFrame__8TXFramesCFRC11TXLongPointPUc
long
TXFrames::PointToFrame(const TXLongPoint& pt, unsigned char* outside) const
{
	long frame = PointToNearestFrame(pt);
	if (frame < 0)
		*outside = 1;
	else
	{
		TXLongRect text;
		GetAbsTextBounds(frame, &text);
		*outside = !text.IsPointInside(pt);
	}
	return frame;
}


// ROM 0x0023a5e0 PointToFrame__8TXFramesCF5PointPUc
long
TXFrames::PointToFrame(Point pt, unsigned char* outside) const
{
	TXLongPoint abs;
	DrawToAbs(pt, &abs);
	return PointToFrame(abs, outside);
}


// ROM 0x0023a614 SectLines__8TXFramesCFP4RectlPlT3P7TXArray
// Read from the assembly: the band's rectangle is built in a Rect on the
// stack and moved on with unaligned word loads, which on the ARM rotate
// the halfwords - so only the low halfword of each sum is the one meant
// (the bottom is the top plus one line's height, the next top the top
// plus the band's height).
Boolean
TXFrames::SectLines(Rect* r, long frame, long* first, long* count, TXArray* lines) const
{
	TXLongRect text;
	GetAbsTextBounds(frame, &text);
	TXLongRect abs;
	DrawToAbs(*r, &abs);
	if (!text.Sect(abs, &abs))
		return false;
	*count = 0;
	lines->SetCount(0);
	AbsToDraw(abs, r);
	TXOffsetPair range;
	if (!fFormatter->GetFrameLineRange(frame, &range))
	{
		TXSectLine* band = (TXSectLine*) lines->Insert(nil, 1, -1);
		band->fCount = 0;
		band->fRect = *r;
		return true;
	}
	long pixels = abs.top - text.top;
	TXLineHeightGroup* group;
	long inGroup;
	long line = fFormatter->PixelToLine(&pixels, range.fStart, &group, &inGroup);
	*first = line;
	fFormatter->Lock(false);
	Rect band;
	band.left = HAbsToDraw(text.left);
	band.right = HAbsToDraw(text.right);
	long top = text.top + pixels;
	band.top = VAbsToDraw(top);
	band.bottom = 0;
	long remaining = abs.bottom - top;
	do
	{
		TXSectLine* entry = (TXSectLine*) lines->Insert(nil, 1, -1);
		if (range.fEnd < line)
		{
			entry->fCount = 0;
			band.bottom = r->bottom;
			entry->fRect = band;
			break;
		}
		long last = fFormatter->HeightToCountLines(*group, inGroup, &remaining) + line - 1;
		if (range.fEnd < last)
		{
			remaining = group->fHeight + remaining;
			last = range.fEnd;
		}
		entry->fCount = (last - line) + 1;
		band.bottom = (short) (group->fHeight + band.top);
		entry->fRect = band;
		entry->fLine = line;
		entry->fAscent = group->fAscent;
		band.top = (short) (group->fHeight * entry->fCount + band.top);
		line = entry->fCount + line;
		*count = entry->fCount + *count;
		group++;
		inGroup = 0;
	} while (remaining > 0);
	fFormatter->Unlock();
	return true;
}


// ROM 0x0023a8e4 __ct__16TXMonoSizeFramesFv
TXMonoSizeFrames::TXMonoSizeFrames()
{
	fSize.h = 0;
	fSize.v = 0;
}


// ROM 0x0023a934 SetTextBoundsSize__16TXMonoSizeFramesFRC11TXLongPointP16TXDisplayChangesl
void
TXMonoSizeFrames::SetTextBoundsSize(const TXLongPoint& size, TXDisplayChanges* changes, long frame)
{
	if (fSize.h == size.h && fSize.v == size.v)
		return;
	fFormatter->SetFrameHeight(frame, size.v);
	if (changes != nil)
		changes->fFlags |= (fSize.h == size.h) ? 2 : 1;
	fSize = size;
}


// ROM 0x0023a9bc GetTextBoundsSize__16TXMonoSizeFramesCFP11TXLongPointl
void
TXMonoSizeFrames::GetTextBoundsSize(TXLongPoint* size, long /*frame*/) const
{
	*size = fSize;
}


// ROM 0x0023a9cc GetLineFormatWidth__16TXMonoSizeFramesCFl
long
TXMonoSizeFrames::GetLineFormatWidth(long /*line*/) const
{
	return fSize.h;
}


// ROM 0x0023a9d4 GetLineMaxWidth__16TXMonoSizeFramesCFl
long
TXMonoSizeFrames::GetLineMaxWidth(long /*line*/) const
{
	return fSize.h;
}


// ROM 0x0023a9dc __ct__11TXMonoFrameFv
TXMonoFrame::TXMonoFrame()
{
	fFormatter = new TXMonoFrameFormatter;
}


// ROM 0x0023aa28 PointToNearestFrame__11TXMonoFrameCFRC11TXLongPoint
long
TXMonoFrame::PointToNearestFrame(const TXLongPoint& /*pt*/) const
{
	return 0;
}


// ROM 0x0023aa30 SectFrames__11TXMonoFrameCFRC4RectP12TXSectFrames
void
TXMonoFrame::SectFrames(const Rect& /*r*/, TXSectFrames* frames) const
{
	frames->SetUniform(0, 1, 0, 1);
}


// ROM 0x0023aa5c SetTextBoundsSize__11TXMonoFrameFRC11TXLongPointP16TXDisplayChangesl
void
TXMonoFrame::SetTextBoundsSize(const TXLongPoint& size, TXDisplayChanges* changes, long frame)
{
	TXLongPoint s = size;
	if (size.v == 0)
		s.v = 0x40000000;
	TXMonoSizeFrames::SetTextBoundsSize(s, changes, frame);
}


// ROM 0x0023aaa4 GetTotalHeight__11TXMonoFrameCFv
long
TXMonoFrame::GetTotalHeight(void) const
{
	Rect margins;
	GetFramesMargins(&margins);
	long lines = fFormatter->fTotalHeight + margins.bottom + margins.top;
	TXLongRect frame;
	GetAbsFrameBounds(0, &frame);
	long height = frame.bottom - frame.top;
	if (height > 0x3fffffff || height < lines)
		height = lines;
	return height;
}


// ROM 0x0023ab1c GetTotalWidth__11TXMonoFrameCFv
long
TXMonoFrame::GetTotalWidth(void) const
{
	TXLongRect frame;
	GetAbsFrameBounds(0, &frame);
	return frame.right - frame.left;
}


// ROM 0x002413e0 __ct__12TXPageFramesFv
TXPageFrames::TXPageFrames()
{
	fColumns = 1;
	fFormatter = new TXPageFormatter;
}


// ROM 0x00241434 GetCountPages__12TXPageFramesCFv
long
TXPageFrames::GetCountPages(void) const
{
	return fFormatter->GetCountFrames();
}


// ROM 0x00241440 PointToNearestFrame__12TXPageFramesCFRC11TXLongPoint
long
TXPageFrames::PointToNearestFrame(const TXLongPoint& pt) const
{
	long column;
	if (fColumns < 2)
		column = 0;
	else
	{
		column = pt.h / (GetPageWidth() + GetPageGutter());
		if (column >= fColumns - 1)
			column = fColumns - 1;
	}
	long row = pt.v / (GetPageHeight() + GetPageGutter());
	long page = row * fColumns + column;
	long last = GetCountPages() - 1;
	if (page < last)
		last = page;
	return last;
}


// ROM 0x002414f8 SectFrames__12TXPageFramesCFRC4RectP12TXSectFrames
void
TXPageFrames::SectFrames(const Rect& r, TXSectFrames* frames) const
{
	TXLongRect abs;
	DrawToAbs(r, &abs);
	TXLongPoint pt;
	pt.v = abs.top;
	pt.h = abs.left;
	long first = PointToNearestFrame(pt);
	pt.v = abs.top;
	pt.h = abs.right;
	long topRight = PointToNearestFrame(pt);
	pt.v = abs.bottom;
	pt.h = abs.right;
	long last = PointToNearestFrame(pt);
	frames->SetUniform(first, (topRight - first) + 1, last, fColumns);
}


// ROM 0x00241774 GetPageGutter__12TXPageFramesCFv
long
TXPageFrames::GetPageGutter(void) const
{
	return 5;
}


// ROM 0x0024282c GetPageHeight__12TXPageFramesCFv
long
TXPageFrames::GetPageHeight(void) const
{
	Rect margins;
	GetFramesMargins(&margins);
	return fSize.v + margins.top + margins.bottom;
}


// ROM 0x00242868 GetPageWidth__12TXPageFramesCFv
long
TXPageFrames::GetPageWidth(void) const
{
	Rect margins;
	GetFramesMargins(&margins);
	return fSize.h + margins.left + margins.right;
}


// ROM 0x002428a4 GetTotalHeight__12TXPageFramesCFv
long
TXPageFrames::GetTotalHeight(void) const
{
	long rows = (GetCountPages() + fColumns - 1) / fColumns;
	long gutter = GetPageGutter();
	return rows * (GetPageHeight() + gutter) - gutter;
}


// ROM 0x00242904 GetTotalWidth__12TXPageFramesCFv
long
TXPageFrames::GetTotalWidth(void) const
{
	long columns = GetCountPages();
	if (fColumns < columns)
		columns = fColumns;
	long gutter = GetPageGutter();
	return columns * (GetPageWidth() + gutter) - gutter;
}


// ROM 0x0024295c PageNoToCell__12TXPageFramesCFlP10TXPageCell
void
TXPageFrames::PageNoToCell(long page, TXPageCell* cell) const
{
	long column = 0;
	if (page == 0)
		cell->fRow = 0;
	else if (fColumns == 1)
		cell->fRow = page;
	else
	{
		cell->fRow = page / fColumns;
		column = page % fColumns;
	}
	cell->fColumn = column;
}


// ROM 0x002429b0 GetAbsTextBounds__12TXPageFramesCFlP10TXLongRect
void
TXPageFrames::GetAbsTextBounds(long frame, TXLongRect* bounds) const
{
	TXFrames::GetAbsTextBounds(frame, bounds);
	TXPageCell cell;
	PageNoToCell(frame, &cell);
	long gutter = GetPageGutter();
	long width = GetPageWidth();
	long height = GetPageHeight();
	bounds->Offset(cell.fColumn * (width + gutter), cell.fRow * (height + gutter));
}
