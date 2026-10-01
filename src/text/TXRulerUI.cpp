/*
	File:		text/TXRulerUI.cpp

	Contains:	The ruler bar (TXRulerUI.h).

	Reconstructed from the MP2x00 US ROM (0x00243240-0x00245880,
	0x0024d900-0x0024d9f4, 0x0024dd1c-0x0024dea4); each function cites
	its origin.
*/

#include "TXRulerUI.h"
#include "TXFrameFormatter.h"
#include "TXFrames.h"
#include "TXUtilities.h"
#include "View.h"
#include "Pictures.h"
#include "Draw.h"
#include "Shapes.h"
#include "Ports.h"
#include "Rects.h"
#include "Regions.h"
#include "Fonts.h"
#include "Text.h"
#include "Unicode.h"
#include "NumberFormat.h"
#include "REPTranslators.h"
#include "objects.h"
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "NewtonExceptions.h"
#include "NewtErrors.h"
#include "NewtonMemory.h"
#include "OSErrors.h"
#include "FixedMath.h"
#include "TXRulerRange.h"

#include <string.h>

TXRulerUIData	gTXRulerUIData;
PixelMap*		gTXRulerBitMaps = nil;
TXRulerPixMaps	gTXRulerPixMaps = { 0, nil };

const long		kTXRulerPictures	= 17;


/*------------------------------------------------------------------------------
	T h e   p i c t u r e s   a n d   t h e   p e n
------------------------------------------------------------------------------*/

// ROM 0x0024dd1c Get__14TXRulerPixMapsFPPA17_8PixelMap
// The first user makes the pixel maps of the ROM's rulerPicts, each moved
// to the origin.  DEVIATION: an array of the host's PixelMap (sizeof), not
// the ROM's 0x1dc bytes.
NewtonErr
TXRulerPixMaps::Get(PixelMap** maps)
{
	if (fUsers++ == 0)
	{
		fMaps = new PixelMap[kTXRulerPictures];
		if (fMaps == nil)
			return kError_No_Memory;
		RefVar pictures(Rrulerpicts);
		TPixelObj pixels;
		for (long i = kTXRulerPictures - 1; i >= 0; i--)
		{
			pixels.Init(RefVar(GetArraySlotRef(pictures, i)));
			PixelMap* map = &fMaps[i];
			*map = *pixels.Pixels();
			OffsetRect(&map->bounds, -map->bounds.left, -map->bounds.top);
		}
	}
	*maps = fMaps;
	return noErr;
}


// ROM 0x0024de70 Release__14TXRulerPixMapsFv
void
TXRulerPixMaps::Release(void)
{
	if (--fUsers != 0)
		return;
	delete[] fMaps;
	fMaps = nil;
}


// ROM 0x00243240 (unnamed)
// Read from the assembly.  The side it is near is only measured across
// (a point beside a marker but far above it still counts), and the top or
// bottom only when a side was near - and not exactly on it.
int
TXRulerPointDistance(Point pt, const Rect* r)
{
	if (PtInRect(pt, r))
		return 0;
	int across = -1;
	int slop = (int) gTXRulerUIData.fHitSlop;
	int d = (short) (pt.h - r->left);
	if (d < 0)
		d = -d;
	d = (short) d;
	if (d <= slop)
		across = d;
	else
	{
		d = (short) (pt.h - r->right);
		if (d < 0)
			d = -d;
		d = (short) d;
		if (d <= slop)
			across = d;
	}
	int down = -1;
	if (across > 0)
	{
		d = (short) (pt.v - r->top);
		if (d < 0)
			d = -d;
		d = (short) d;
		if (d <= slop)
			down = d;
		else
		{
			d = (short) (pt.v - r->bottom);
			if (d < 0)
				d = -d;
			d = (short) d;
			if (d < slop)
				down = d;
		}
	}
	if (across < 0 || down < 0)
		return -1;
	return across < down ? across : down;
}


// ROM 0x00243354 (unnamed)
void
TXRulerPinRect(Rect* r, const Rect* bounds)
{
	long dh = 0;
	if (r->left < bounds->left)
		dh = bounds->left - r->left;
	else if (r->right > bounds->right)
		dh = bounds->right - r->right;
	long dv = 0;
	if (r->top < bounds->top)
		dv = bounds->top - r->top;
	else if (r->bottom > bounds->bottom)
		dv = bounds->bottom - r->bottom;
	OffsetRect(r, dh, dv);
}


// ROM 0x00243840 (unnamed)
// Read from the assembly.  The icons follow the pen, xor-drawn where they
// are, each kept within its limit; with `snap`, an icon above its limit's
// top half is put on the limit's top line and below it back where it
// started, measured by the tabs bar's height.  Only the icons up to the
// first that did not move are moved each time.  ==> whether the first
// ended on its limit's top (a drop into the ruler); if not, the last
// drawing is taken away again.
Boolean
TXDragRulerBitMaps(TXPointingDevice* pen, TXRulerDragItem* items, int count, Boolean drawn, Boolean snap)
{
	Point last = pen->FirstLocation();
	Boolean moved = false;
	GrafPtr port;
	GetPort(&port);
	for (int i = count - 1; i >= 0; i--)
	{
		TXRulerDragItem* item = &items[i];
		item->fCurrent = item->fStart;
		item->fDelta.v = 0;
		item->fDelta.h = 0;
		if (snap)
			item->fSnap = item->fCurrent;
	}
	while (pen->IsStillDown())
	{
		Point pt = pen->CurrentLocation();
		if (EqualPt(pt, last))
			continue;
		for (int i = 0; i < count; i++)
		{
			TXRulerDragItem* item = &items[i];
			if (drawn)
				CopyBits(item->fMap, &port->portBits, &item->fMap->bounds, &item->fCurrent, srcXor, nil);
			long dv = pt.v - last.v;
			OffsetRect(&item->fCurrent, pt.h - last.h, dv);
			TXRulerPinRect(&item->fCurrent, &item->fLimit);
			if (snap)
			{
				OffsetRect(&item->fSnap, 0, dv);
				if (item->fSnap.top < item->fLimit.top || item->fSnap.bottom > item->fLimit.bottom)
					item->fSnap = item->fCurrent;
				long below = item->fSnap.top - item->fLimit.top;
				long d;
				if (below > gTXRulerUIData.fTabsBarHeight / 2)
					d = item->fSnap.top - item->fCurrent.top;
				else
					d = item->fLimit.top - item->fCurrent.top;
				OffsetRect(&item->fCurrent, 0, d);
			}
			CopyBits(item->fMap, &port->portBits, &item->fMap->bounds, &item->fCurrent, srcXor, nil);
			item->fDelta.h = item->fCurrent.left - item->fStart.left;
			item->fDelta.v = item->fCurrent.top - item->fStart.top;
			if (item->fDelta.h == 0 && item->fDelta.v == 0)
				break;
		}
		last = pt;
		drawn = true;
		moved = true;
	}
	if (items[0].fCurrent.top == items[0].fLimit.top)
		return true;
	if (moved)
		CopyBits(items[0].fMap, &port->portBits, &items[0].fMap->bounds, &items[0].fCurrent, srcXor, nil);
	return false;
}


/*------------------------------------------------------------------------------
	T X R u l e r B i t M a p C l u s t e r
------------------------------------------------------------------------------*/

// ROM 0x00245110 __ct__20TXRulerBitMapClusterFv
TXRulerBitMapCluster::TXRulerBitMapCluster()
{ }


// ROM 0x00245144 IRulerBitMapCluster__20TXRulerBitMapClusterFiN31
// Each icon the size of the first picture.
void
TXRulerBitMapCluster::IRulerBitMapCluster(int first, int bitmap, int count, int spacing)
{
	fFirst = first;
	fCount = count;
	fBitMap = bitmap;
	const Rect& bounds = gTXRulerBitMaps[bitmap].bounds;
	fWidth = bounds.right - bounds.left;
	fHeight = bounds.bottom - bounds.top;
	fSpacing = spacing;
}


// ROM 0x002451a0 SetTopLeft__20TXRulerBitMapClusterFiT1
void
TXRulerBitMapCluster::SetTopLeft(int top, int left)
{
	fTop = top;
	fLeft = left;
}


// ROM 0x002451a8 CalcDimensions__20TXRulerBitMapClusterCFPiT1
void
TXRulerBitMapCluster::CalcDimensions(int* width, int* height) const
{
	*width = fWidth * fCount + (fCount - 1) * fSpacing;
	*height = fHeight;
}


// ROM 0x002451d4 CalcBitMapRect__20TXRulerBitMapClusterCFiP4Rect
void
TXRulerBitMapCluster::CalcBitMapRect(int index, Rect* r) const
{
	r->top = (short) fTop;
	r->left = (short) ((index - fFirst) * (fWidth + fSpacing) + fLeft);
	r->bottom = (short) (fHeight + r->top);
	r->right = (short) (r->left + fWidth);
}


// ROM 0x0024524c CalcDragBitMapRect__20TXRulerBitMapClusterCFiP4Rect
void
TXRulerBitMapCluster::CalcDragBitMapRect(int index, Rect* r) const
{
	CalcBitMapRect(index, r);
	const Rect& bounds = gTXRulerBitMaps[index].bounds;
	int height = bounds.bottom - bounds.top;
	int width = bounds.right - bounds.left;
	r->top = (short) (r->top + ((r->bottom - r->top) - height) / 2);
	r->left = (short) (r->left + ((r->right - r->left) - width) / 2);
	r->bottom = (short) (r->top + height);
	r->right = (short) (r->left + width);
}


// ROM 0x00245320 Draw__20TXRulerBitMapClusterFPC7TXRuler
void
TXRulerBitMapCluster::Draw(const TXRuler* /*ruler*/)
{
	int end = fBitMap + fCount;
	for (int i = fBitMap; i < end; i++)
	{
		Rect r;
		CalcBitMapRect((i - fBitMap) + fFirst, &r);
		PixelMap* map = &gTXRulerBitMaps[i];
		GrafPtr port;
		GetPort(&port);
		CopyBits(map, &port->portBits, &map->bounds, &r, srcCopy, nil);
	}
}


// ROM 0x002453b8 InvertBitMap__20TXRulerBitMapClusterCFi
void
TXRulerBitMapCluster::InvertBitMap(int index) const
{
	Rect r;
	CalcBitMapRect(index, &r);
	InsetRect(&r, 2, 2);
	InvertRect(&r);
}


// ROM 0x002453ec PointToBitMapIndex__20TXRulerBitMapClusterCF5Point
int
TXRulerBitMapCluster::PointToBitMapIndex(Point pt) const
{
	int nearest = 0x7fff;
	int found = -1;
	int end = fFirst + fCount;
	for (int i = fFirst; i < end; i++)
	{
		Rect r;
		CalcBitMapRect(i, &r);
		int d = TXRulerPointDistance(pt, &r);
		if (d == 0)
			return i;
		if (d > 0 && d < nearest)
		{
			found = i;
			nearest = d;
		}
	}
	return found;
}


// ROM 0x00245544 CalcDimensions__20TXLineSpacingClusterCFPiT1
// The arrows and 36 pixels for the spacing.
void
TXLineSpacingCluster::CalcDimensions(int* width, int* height) const
{
	TXRulerBitMapCluster::CalcDimensions(width, height);
	*width = *width + 0x24;
}


// ROM 0x00245568 GetLineSpacingStringBounds__20TXLineSpacingClusterFP4Rect
void
TXLineSpacingCluster::GetLineSpacingStringBounds(Rect* r)
{
	int left = fCount * fWidth + fLeft;
	SetRect(r, left - 2, fTop, left + 0x24, fTop + fHeight);
}


// ROM 0x002455b0 DrawLineSpacingString__20TXLineSpacingClusterFPC7TXRuler
// The spacing as the locale's lineSpacingFmtStr says, centred in its box
// in System 10 bold.
void
TXLineSpacingCluster::DrawLineSpacingString(const TXRuler* ruler)
{
	Rect r;
	GetLineSpacingStringBounds(&r);
	InsetRect(&r, 2, 2);
	EraseRect(&r);
	unsigned char spacing;
	ruler->GetAttributeValue(kTXAttrLineSpacing, &spacing);
	UniChar number[10];
	IntegerString(spacing, number);
	RefVar format(Rlinespacingfmtstr);
	UniChar text[10];
	ParamString(text, 10, (const UniChar*) BinaryData(format), number);
	StyleRecord style;
	CreateTextStyleRecord(RefVar(Rfontsystem10bold), &style);
	StyleRecord* styles = &style;
	TextOptions options;
	options.fJustification = 0;
	options.fAlignment = 0x8000;
	options.fWidth = 0x240000;
	options.fReserved = 0;
	options.fTransferMode = 1;
	options.fFittedWidth = 0;
	options.fScanner = nil;
	FPoint where;
	where.x = (Fixed) r.left << 16;
	where.y = (Fixed) (fTop + fHeight - 3) << 16;
	DrawTextOnce(text, Ustrlen(text), &styles, nil, where, &options, nil);
	DisposeStyleRecord(&style);
}


// ROM 0x0024571c Draw__20TXLineSpacingClusterFPC7TXRuler
void
TXLineSpacingCluster::Draw(const TXRuler* ruler)
{
	DrawLineSpacingString(ruler);
	Rect r;
	GetLineSpacingStringBounds(&r);
	PenState saved;
	GetPenState(&saved);
	PenNormal();
	PenSize(2, 2);
	SetFgPattern(GetStdPattern(2));
	FrameRoundRect(&r, 5, 5);
	SetPenState(&saved);
	TXRulerBitMapCluster::Draw(ruler);
}


/*------------------------------------------------------------------------------
	T X R u l e r B a r
------------------------------------------------------------------------------*/

// ROM 0x00243bb4 __ct__10TXRulerBarFv
TXRulerBar::TXRulerBar()
{ }


// ROM 0x00243be8 IRulerBar__10TXRulerBarFP10TextensionP7TXRuler
void
TXRulerBar::IRulerBar(Textension* text, TXRuler* ruler)
{
	fText = text;
	fRuler = ruler;
	SetRect(&fBounds, 0, 0, 0, 0);
}


// ROM 0x00243c14 SetBounds__10TXRulerBarFRC4Rect
void
TXRulerBar::SetBounds(const Rect& r)
{
	fBounds = r;
}


// ROM 0x00243c24 GetBounds__10TXRulerBarCFP4Rect
void
TXRulerBar::GetBounds(Rect* r) const
{
	*r = fBounds;
}


// ROM 0x00243c34 Activate__10TXRulerBarFUc
void
TXRulerBar::Activate(Boolean /*on*/)
{ }


/*------------------------------------------------------------------------------
	T X R u l e r T a b s B a r
------------------------------------------------------------------------------*/

// ROM 0x0024447c __ct__14TXRulerTabsBarFv
TXRulerTabsBar::TXRulerTabsBar()
{
	fMeasure = -1;
}


// ROM 0x002444c4 IRulerTabsBar__14TXRulerTabsBarFP10TextensionP7TXRuleri
void
TXRulerTabsBar::IRulerTabsBar(Textension* text, TXRuler* ruler, int measure)
{
	SetRulerMeasure(measure);
	fText = text;
	fRuler = ruler;
	SetRect(&fBounds, 0, 0, 0, 0);
}


// ROM 0x002444f8 SetRulerMeasure__14TXRulerTabsBarFi
Boolean
TXRulerTabsBar::SetRulerMeasure(int measure)
{
	Boolean changed = fMeasure != measure;
	if (changed)
		fMeasure = measure;
	return changed;
}


// ROM 0x00244600 GetTabBitMapIndex__14TXRulerTabsBarCF5TXTab
int
TXRulerTabsBar::GetTabBitMapIndex(TXTab tab) const
{
	char kind = (char) tab.fKind;
	if (kind == 0)
		return 4;
	if (kind == 1)
		return 5;
	if (kind == -1)
		return 6;
	return 7;
}


// ROM 0x00244638 GetTabRect__14TXRulerTabsBarCF5TXTabP4Rect
// The tab's marker centred on its position, on the measure's line.
void
TXRulerTabsBar::GetTabRect(TXTab tab, Rect* r) const
{
	*r = gTXRulerBitMaps[GetTabBitMapIndex(tab)].bounds;
	short dv = (short) (gTXRulerUIData.fMeasureBase + fBounds.top);
	short dh = (short) ((tab.fPosition + fBounds.left) - (r->right - r->left) / 2);
	r->top = (short) (r->top + dv);
	r->left = (short) (r->left + dh);
	r->bottom = (short) (r->bottom + dv);
	r->right = (short) (r->right + dh);
}


// ROM 0x002446ac GetBitMapRect__14TXRulerTabsBarCFPC7TXRuleriP4Rect
// The left margin marker at the bottom of the bar, the indent and the
// right margin at its top.
void
TXRulerTabsBar::GetBitMapRect(const TXRuler* ruler, int index, Rect* r) const
{
	*r = gTXRulerBitMaps[index].bounds;
	long value;
	long dh, dv;
	if (index == 0xc)
	{
		ruler->GetAttributeValue(kTXAttrLeftMargin, &value);
		dv = fBounds.bottom - r->bottom;
		dh = fBounds.left + value;
	}
	else if (index == 0xd)
	{
		ruler->GetAttributeValue(kTXAttrIndent, &value);
		dv = fBounds.top;
		dh = fBounds.left + value;
	}
	else
	{
		ruler->GetAttributeValue(kTXAttrRightMargin, &value);
		dh = (fBounds.right - value) - (r->right - r->left);
		dv = fBounds.top;
	}
	OffsetRect(r, dh, dv);
}


// ROM 0x002447c4 TabRectToTabValue__14TXRulerTabsBarCFRC4Rect
int
TXRulerTabsBar::TabRectToTabValue(const Rect& r) const
{
	return (r.left - fBounds.left) + (r.right - r.left) / 2;
}


// ROM 0x002447e8 DrawRuler__14TXRulerTabsBarFPC7TXRuler
void
TXRulerTabsBar::DrawRuler(const TXRuler* ruler)
{
	GrafPtr port;
	GetPort(&port);
	EraseRect(&fBounds);
	DrawRulerMeasure();
	TXTabsArray* tabs;
	ruler->GetAttributeValue(kTXAttrTabs, &tabs);
	if (tabs != nil)
	{
		for (long i = tabs->GetCount() - 1; i >= 0; i--)
		{
			TXTab tab = tabs->GetIndTab(i);
			PixelMap* map = &gTXRulerBitMaps[GetTabBitMapIndex(tab)];
			Rect r;
			GetTabRect(tab, &r);
			CopyBits(map, &port->portBits, &map->bounds, &r, srcCopy, nil);
		}
	}
	for (int i = 0xc; i < 0xf; i++)
	{
		Rect r;
		GetBitMapRect(ruler, i, &r);
		PixelMap* map = &gTXRulerBitMaps[i];
		CopyBits(map, &port->portBits, &map->bounds, &r, srcCopy, nil);
	}
}


// ROM 0x00244934 DrawRulerMeasure__14TXRulerTabsBarFv
// Read from the assembly.  The measure's line and its ticks - an inch in
// eighths, or a centimetre in halves, each tick as long as the times its
// number halves evenly - with the whole units numbered in System 9.
void
TXRulerTabsBar::DrawRulerMeasure(void)
{
	Fixed unit;
	long divisions;
	if (fMeasure == 0)
	{
		unit = 0x10000;
		divisions = 8;
	}
	else if (fMeasure == 1)
	{
		unit = 0x64c9;
		divisions = 2;
	}
	else
		return;
	StyleRecord style;
	CreateTextStyleRecord(RefVar(Rfontsystem9), &style);
	StyleRecord* styles = &style;
	Fixed tick = FixedDivide(FixedMultiply(0x480000, unit), divisions << 16);
	PenState saved;
	GetPenState(&saved);
	PenNormal();
	long left = fBounds.left;
	long line = gTXRulerUIData.fMeasureBase + fBounds.top - 1;
	MoveTo(left, line);
	Line(fBounds.right - fBounds.left, 0);
	long n = 1;
	if (fBounds.right > 0)
	{
		Fixed labelV = (Fixed) (line - 2) << 16;
		long x;
		do
		{
			x = left + (FixedMultiply(n << 16, tick) >> 16);
			long evenly = 0;
			for (long m = n; (m & 1) == 0; m >>= 1)
				evenly++;
			MoveTo(x, line);
			Line(0, -(evenly + 2));
			if (n % divisions == 0)
			{
				UniChar number[10];
				IntegerString(n / divisions, number);
				FPoint where;
				where.x = (Fixed) (x + 2) << 16;
				where.y = labelV;
				DrawTextOnce(number, Ustrlen(number), &styles, nil, where, nil, nil);
			}
			n++;
		} while (x < fBounds.right);
	}
	SetPenState(&saved);
	DisposeStyleRecord(&style);
}


// ROM 0x00244b44 Draw__14TXRulerTabsBarFv
// (the ROM has DrawRuler's body inlined here, over the bar's ruler)
void
TXRulerTabsBar::Draw(void)
{
	DrawRuler(fRuler);
}


// ROM 0x0024510c CheckUpdate__14TXRulerTabsBarFPC7TXRuler
void
TXRulerTabsBar::CheckUpdate(const TXRuler* ruler)
{
	DrawRuler(ruler);
}


// ROM 0x00244b4c PointToBitMapIndex__14TXRulerTabsBarCF5PointP5TXTab
// The margin markers first, then the tabs from the last.
// ROM BUG: a tab that was only near leaves in `*tab` the nearest tab seen
// - or, when no tab was near at all, whatever was on the stack; the host
// clears it.
int
TXRulerTabsBar::PointToBitMapIndex(Point pt, TXTab* tab) const
{
	int nearest = 0x7fff;
	int found = -1;
	for (int i = 0xc; i < 0xf; i++)
	{
		Rect r;
		GetBitMapRect(fRuler, i, &r);
		int d = TXRulerPointDistance(pt, &r);
		if (d == 0)
			return i;
		if (d > 0 && d < nearest)
		{
			nearest = d;
			found = i;
		}
	}
	TXTabsArray* tabs;
	fRuler->GetAttributeValue(kTXAttrTabs, &tabs);
	if (tabs != nil)
	{
		TXTab nearestTab;
		memset(&nearestTab, 0, sizeof(nearestTab));
		for (long n = tabs->GetCount() - 1; n >= 0; n--)
		{
			*tab = tabs->GetIndTab(n);
			Rect r;
			GetTabRect(*tab, &r);
			int d = TXRulerPointDistance(pt, &r);
			int index = GetTabBitMapIndex(*tab);
			if (d == 0)
				return index;
			if (d > 0 && d < nearest)
			{
				nearestTab = *tab;
				nearest = d;
				found = index;
			}
		}
		*tab = nearestTab;
	}
	return found;
}


// ROM 0x00244c78 HitTest__14TXRulerTabsBarF5Point
Boolean
TXRulerTabsBar::HitTest(Point pt)
{
	TXTab tab;
	return PointToBitMapIndex(pt, &tab) >= 0;
}


// ROM 0x00244ca4 Click__14TXRulerTabsBarFP16TXPointingDevicelP12TXAttrValuesPl
// Read from the assembly.  A tab dragged along the ruler moves (how 4), or
// off it is taken away (how 1); a margin marker dragged changes its margin
// - the left margin taking the indent with it unless modifier bit 3 is
// held - each kept 50 pixels short of the other side.
Boolean
TXRulerTabsBar::Click(TXPointingDevice* pen, long modifiers, TXAttrValues* values, long* how)
{
	*how = 0;
	Point pt = pen->FirstLocation();
	TXTab tab;
	int index = PointToBitMapIndex(pt, &tab);
	if (index < 0)
		return false;
	TXRulerDragItem items[2];
	items[0].fMap = &gTXRulerBitMaps[index];
	if (index >= 4 && index <= 7)
	{
		GetTabRect(tab, &items[0].fStart);
		items[0].fLimit = fBounds;
		items[0].fLimit.top = (short) (items[0].fLimit.top + gTXRulerUIData.fMeasureBase);
		items[0].fLimit.bottom = (short) (items[0].fLimit.bottom + gTXRulerUIData.fIconsBarHeight);
		TXTabUpdate update;
		if (!TXDragRulerBitMaps(pen, items, 1, false, true))
		{
			*how = 1;
			update.fOld = tab;
			memset(&update.fNew, 0, sizeof(update.fNew));	// (the ROM leaves what the stack held)
		}
		else
		{
			if (items[0].fDelta.h == 0)
				return false;
			*how = 4;
			update.fOld = tab;
			update.fNew = tab;
			update.fNew.fPosition = update.fNew.fPosition + items[0].fDelta.h;
		}
		update.fRuler = (TXAdvancedRuler*) fRuler;
		values->Add(kTXAttrTabs, &update, sizeof(update), false);
		return true;
	}
	GetBitMapRect(fRuler, index, &items[0].fStart);
	items[0].fLimit = items[0].fStart;
	items[0].fLimit.left = fBounds.left;
	items[0].fLimit.right = fBounds.right;
	TXAttrTag tag;
	if (index == 0xe)
	{
		tag = kTXAttrRightMargin;
		long left, indent;
		fRuler->GetAttributeValue(kTXAttrLeftMargin, &left);
		fRuler->GetAttributeValue(kTXAttrIndent, &indent);
		if (left > indent)
			indent = left;
		items[0].fLimit.left = (short) (items[0].fLimit.left + indent + 0x32);
	}
	else
	{
		tag = (index == 0xc) ? kTXAttrLeftMargin : kTXAttrIndent;
		long right;
		fRuler->GetAttributeValue(kTXAttrRightMargin, &right);
		items[0].fLimit.right = (short) (items[0].fLimit.right - (right + 0x32));
	}
	int count = 1;
	if (index == 0xc && (modifiers & 8) == 0)
	{
		items[1].fMap = &gTXRulerBitMaps[0xd];
		GetBitMapRect(fRuler, 0xd, &items[1].fStart);
		items[1].fLimit = items[1].fStart;
		items[1].fLimit.left = items[0].fLimit.left;
		items[1].fLimit.right = items[0].fLimit.right;
		count = 2;
	}
	TXDragRulerBitMaps(pen, items, count, false, false);
	if (items[0].fDelta.h == 0)
		return false;
	if (index == 0xe)
		items[0].fDelta.h = -items[0].fDelta.h;
	long value;
	fRuler->GetAttributeValue(tag, &value);
	value = value + items[0].fDelta.h;
	values->Add(tag, &value, sizeof(value), false);
	if (count == 2)
	{
		long indent;
		fRuler->GetAttributeValue(kTXAttrIndent, &indent);
		indent = indent + items[1].fDelta.h;
		values->Add(kTXAttrIndent, &indent, sizeof(indent), false);
	}
	return true;
}


/*------------------------------------------------------------------------------
	T X R u l e r I c o n s B a r
------------------------------------------------------------------------------*/

// ROM 0x00243c38 __ct__15TXRulerIconsBarFv
TXRulerIconsBar::TXRulerIconsBar()
{ }


// ROM 0x00243c9c IRulerIconsBar__15TXRulerIconsBarFP10TextensionP7TXRulerP14TXRulerTabsBar
void
TXRulerIconsBar::IRulerIconsBar(Textension* text, TXRuler* ruler, TXRulerTabsBar* tabsBar)
{
	IRulerBar(text, ruler);
	fJust.IRulerBitMapCluster(0, 0, 4, (int) gTXRulerUIData.fIconSpacing);
	fTabsBar = tabsBar;
	fTabs.IRulerBitMapCluster(4, 8, 4, (int) gTXRulerUIData.fIconSpacing);
	fLineSpacing.IRulerBitMapCluster(0xf, 0xf, 2, 0);
}


// ROM 0x00243d20 SetBounds__15TXRulerIconsBarFRC4Rect
// The tab icons at the left, the justifications at the right and the line
// spacing in the middle, with a third of what is left over either side.
void
TXRulerIconsBar::SetBounds(const Rect& r)
{
	TXRulerBar::SetBounds(r);
	int tabsWidth, tabsHeight, justWidth, justHeight, lineWidth, lineHeight;
	fTabs.CalcDimensions(&tabsWidth, &tabsHeight);
	fJust.CalcDimensions(&justWidth, &justHeight);
	fLineSpacing.CalcDimensions(&lineWidth, &lineHeight);
	int left = fBounds.left;
	int gap = (((fBounds.right - left) - tabsWidth) - justWidth - lineWidth) / 3;
	if (gap < 0)
		gap = 0;
	int barHeight = (int) gTXRulerUIData.fIconsBarHeight;
	fTabs.SetTopLeft(fBounds.top + (barHeight - tabsHeight) / 2, left + gap);
	fJust.SetTopLeft(fBounds.top + (barHeight - justHeight) / 2, (fBounds.right - gap) - justWidth);
	left = fBounds.left;
	fLineSpacing.SetTopLeft(fBounds.top + (barHeight - lineHeight) / 2, left + ((fBounds.right - left) - lineWidth) / 2);
}


// ROM 0x00243e84 Draw__15TXRulerIconsBarFv
// A line a quarter of the way down, the icons, the justification in use
// inverted.
void
TXRulerIconsBar::Draw(void)
{
	PenState saved;
	GetPenState(&saved);
	PenNormal();
	PenSize(2, 2);
	long top = fBounds.top;
	long y = top + (fBounds.bottom - top) / 4;
	MoveTo(fBounds.left, y);
	LineTo(fBounds.right, y);
	SetPenState(&saved);
	fJust.Draw(fRuler);
	char just;
	fRuler->GetAttributeValue(kTXAttrJustification, &just);
	fJust.InvertBitMap(JustValueToBitMapIndex(just));
	fTabs.Draw(fRuler);
	fLineSpacing.Draw(fRuler);
}


// ROM 0x00244450 JustValueToBitMapIndex__15TXRulerIconsBarCFc
int
TXRulerIconsBar::JustValueToBitMapIndex(char just) const
{
	if (just == 1)
		return 0;
	if (just == 2)
		return 2;
	if (just == 4)
		return 1;
	return 3;
}


// ROM 0x00243fa4 DoJustClick__15TXRulerIconsBarFiP12TXAttrValuesPl
Boolean
TXRulerIconsBar::DoJustClick(int index, TXAttrValues* values, long* how)
{
	*how = 0;
	char just;
	if (index == 0)
		just = 1;
	else if (index == 1)
		just = 4;
	else if (index == 2)
		just = 2;
	else
		just = 8;
	values->Add(kTXAttrJustification, &just, 1, false);
	return true;
}


// ROM 0x00244010 DoTabsClick__15TXRulerIconsBarFP16TXPointingDeviceiP12TXAttrValuesPl
// Read from the assembly.  A tab icon dragged up into the ruler is a new
// tab there (how 2).
Boolean
TXRulerIconsBar::DoTabsClick(TXPointingDevice* pen, int index, TXAttrValues* values, long* how)
{
	TXRulerDragItem item;
	item.fMap = &gTXRulerBitMaps[index];
	char kind;
	if (index == 4)
		kind = 0;
	else if (index == 5)
		kind = 1;
	else if (index == 6)
		kind = -1;
	else
		kind = 2;
	fTabsBar->GetBounds(&item.fLimit);
	item.fLimit.top = (short) (item.fLimit.top + gTXRulerUIData.fMeasureBase);
	item.fLimit.bottom = fBounds.bottom;
	fTabs.CalcDragBitMapRect(index, &item.fStart);
	if (!TXDragRulerBitMaps(pen, &item, 1, false, true))
		return false;
	OffsetRect(&item.fStart, item.fDelta.h, item.fDelta.v);
	int position = fTabsBar->TabRectToTabValue(item.fStart);
	*how = 2;
	TXTabUpdate update;
	memset(&update.fOld, 0, sizeof(update.fOld));		// (the ROM leaves what the stack held)
	update.fNew.Set(position, kind, 0);
	update.fRuler = (TXAdvancedRuler*) fRuler;
	values->Add(kTXAttrTabs, &update, sizeof(update), false);
	return true;
}


// ROM 0x00244170 DoLineSpaceClick__15TXRulerIconsBarFiP12TXAttrValuesPl
// The arrow flashed; the spacing one step more (to 20) or less (to 1).
Boolean
TXRulerIconsBar::DoLineSpaceClick(int index, TXAttrValues* values, long* how)
{
	Boolean changed = false;
	fLineSpacing.InvertBitMap(index);
	unsigned char spacing;
	fRuler->GetAttributeValue(kTXAttrLineSpacing, &spacing);
	Boolean change = false;
	if (index == 0x10)
	{
		if (spacing < 0x14)
		{
			spacing = spacing + 1;
			change = true;
		}
	}
	else if (spacing > 1)
	{
		spacing = spacing - 1;
		change = true;
	}
	if (change)
	{
		changed = true;
		*how = 0;
		values->Add(kTXAttrLineSpacing, &spacing, 1, false);
	}
	fLineSpacing.InvertBitMap(index);
	return changed;
}


// ROM 0x00244238 HitTest__15TXRulerIconsBarF5Point
Boolean
TXRulerIconsBar::HitTest(Point pt)
{
	return fTabs.PointToBitMapIndex(pt) >= 0
		|| fJust.PointToBitMapIndex(pt) >= 0
		|| fLineSpacing.PointToBitMapIndex(pt) >= 0;
}


// ROM 0x00244298 Click__15TXRulerIconsBarFP16TXPointingDevicelP12TXAttrValuesPl
Boolean
TXRulerIconsBar::Click(TXPointingDevice* pen, long /*modifiers*/, TXAttrValues* values, long* how)
{
	Point pt = pen->FirstLocation();
	int index = fTabs.PointToBitMapIndex(pt);
	if (index >= 0)
		return DoTabsClick(pen, index, values, how);
	index = fJust.PointToBitMapIndex(pt);
	if (index >= 0)
		return DoJustClick(index, values, how);
	index = fLineSpacing.PointToBitMapIndex(pt);
	if (index < 0)
		return false;
	return DoLineSpaceClick(index, values, how);
}


// ROM 0x00244358 CheckUpdate__15TXRulerIconsBarFPC7TXRuler
// What differs from the ruler shown redrawn: the justification's
// inversion moved, the line spacing written again.
void
TXRulerIconsBar::CheckUpdate(const TXRuler* ruler)
{
	char newJust, oldJust;
	ruler->GetAttributeValue(kTXAttrJustification, &newJust);
	fRuler->GetAttributeValue(kTXAttrJustification, &oldJust);
	if (newJust != oldJust)
	{
		fJust.InvertBitMap(JustValueToBitMapIndex(oldJust));
		fJust.InvertBitMap(JustValueToBitMapIndex(newJust));
	}
	unsigned char newSpacing, oldSpacing;
	ruler->GetAttributeValue(kTXAttrLineSpacing, &newSpacing);
	fRuler->GetAttributeValue(kTXAttrLineSpacing, &oldSpacing);
	if (newSpacing != oldSpacing)
		fLineSpacing.DrawLineSpacingString(ruler);
}


/*------------------------------------------------------------------------------
	T X R u l e r U I
------------------------------------------------------------------------------*/

// ROM 0x00243f70 Start__9TXRulerUISFRC13TXRulerUIData
void
TXRulerUI::Start(const TXRulerUIData& data)
{
	gTXRulerUIData = data;
}


// ROM 0x00244510 __ct__9TXRulerUIFP10TextensionP8PixelMapRC6RefVar
// A ruler object of its own to show, in the measure the info's type asks
// for.
TXRulerUI::TXRulerUI(Textension* text, PixelMap* maps, RefArg info)
{
	gTXRulerBitMaps = maps;
	fText = text;
	SetRect(&fBounds, 0, 0, 0, 0);
	fTextBounds = fBounds;
	fRuler = (TXRuler*) Textension::GetNewRulerObject();
	int measure = ISNIL(info) ? 0 : GetRulerType(info);
	fTabsBar.IRulerTabsBar(text, fRuler, measure);
	fIconsBar.IRulerIconsBar(text, fRuler, &fTabsBar);
}


// ROM 0x002450c8 __dt__9TXRulerUIFv
TXRulerUI::~TXRulerUI()
{
	fRuler->Free();
}


// ROM 0x002433d4 Focus__9TXRulerUIFP18TXRulerUIFocusInfo
void
TXRulerUI::Focus(TXRulerUIFocusInfo* info)
{
	GetPort(&info->fPort);
	SetPort(fText->GetTextPort());
	info->fClip = (RgnHandle) gTXTempRegions->Get();
	GetClip(info->fClip);
	Rect bounds;
	GetBounds(&bounds);
	ClipRect(&bounds);
}


// ROM 0x00243430 Unfocus__9TXRulerUIFRC18TXRulerUIFocusInfo
void
TXRulerUI::Unfocus(const TXRulerUIFocusInfo& info)
{
	SetClip(info.fClip);
	gTXTempRegions->Done(info.fClip);
	SetPort(info.fPort);
}


// ROM 0x00243468 CalcCurrentRulerObject__9TXRulerUICFv
TXAttrObject*
TXRulerUI::CalcCurrentRulerObject(void) const
{
	TXOffsetRange selection;
	fText->fHilite->GetHiliteRange(&selection);
	return fText->fRulers->OffsetToObject(selection.fStart.fOffset, selection.fStart.fAtStart);
}


// ROM 0x002434ac GetCurrFrameTextBounds__9TXRulerUICFP4Rect
void
TXRulerUI::GetCurrFrameTextBounds(Rect* r) const
{
	TXOffsetRange selection;
	fText->fHilite->GetHiliteRange(&selection);
	long frame = fText->fFrameFormatter->CharToFrame(selection.fStart.fOffset, selection.fStart.fAtStart);
	fText->fDisplay->fFrames->GetTextBounds(frame, r);
}


// ROM 0x00243500 CheckTextBounds__9TXRulerUIFv
Boolean
TXRulerUI::CheckTextBounds(void)
{
	Rect r;
	GetCurrFrameTextBounds(&r);
	if (fTextBounds.left == r.left && fTextBounds.right == r.right)
		return false;
	fTextBounds.left = r.left;
	fTextBounds.right = r.right;
	fTabsBar.SetBounds(fTextBounds);
	return true;
}


// ROM 0x00243580 Draw__9TXRulerUIFv
void
TXRulerUI::Draw(void)
{
	TXRulerUIFocusInfo info;
	Focus(&info);
	EraseRect(&fBounds);
	fTabsBar.Draw();
	fIconsBar.Draw();
	Unfocus(info);
}


// ROM 0x002435e4 HitTest__9TXRulerUIF5Point
Boolean
TXRulerUI::HitTest(Point pt)
{
	return PtInRect(pt, &fBounds) && (fIconsBar.HitTest(pt) || fTabsBar.HitTest(pt));
}


// ROM 0x00243660 Click__9TXRulerUIFP16TXPointingDevicelP12TXAttrValuesPl
Boolean
TXRulerUI::Click(TXPointingDevice* pen, long modifiers, TXAttrValues* values, long* how)
{
	Point pt = pen->FirstLocation();
	if (!PtInRect(pt, &fBounds))
		return false;
	TXRulerUIFocusInfo info;
	Focus(&info);
	Boolean changed = false;
	TXRulerBar* bar = &fIconsBar;
	Rect bounds;
	bar->GetBounds(&bounds);
	if (!PtInRect(pt, &bounds))
	{
		bar = &fTabsBar;
		bar->GetBounds(&bounds);
		if (!PtInRect(pt, &bounds))
			bar = nil;
	}
	if (bar != nil)
		changed = bar->Click(pen, modifiers, values, how);
	Unfocus(info);
	return changed;
}


// ROM 0x0024375c CheckUpdate__9TXRulerUIFUc
void
TXRulerUI::CheckUpdate(Boolean redraw)
{
	TXAttrObject* ruler = CalcCurrentRulerObject();
	Boolean moved = CheckTextBounds();
	if (ruler->IsEqual(fRuler) && !moved)
		return;
	if (!redraw)
	{
		fRuler->Assign(ruler);
		return;
	}
	TXRulerUIFocusInfo info;
	Focus(&info);
	fIconsBar.CheckUpdate((const TXRuler*) ruler);
	fTabsBar.CheckUpdate((const TXRuler*) ruler);
	fRuler->Assign(ruler);
	Unfocus(info);
}


// ROM 0x00243b58 Scrolled__9TXRulerUIFv
void
TXRulerUI::Scrolled(void)
{
	if (!CheckTextBounds())
		return;
	TXRulerUIFocusInfo info;
	Focus(&info);
	fTabsBar.Draw();
	Unfocus(info);
}


// ROM 0x00245474 SetBounds__9TXRulerUIFRC4Rect
// Read from the assembly.  The icons bar below the tabs bar, the tabs bar
// over the text's frame.
void
TXRulerUI::SetBounds(const Rect& r)
{
	fBounds = r;
	Rect icons = r;
	icons.top = (short) (r.top + gTXRulerUIData.fTabsBarHeight);
	icons.bottom = (short) (icons.top + gTXRulerUIData.fIconsBarHeight);
	fIconsBar.SetBounds(icons);
	GetCurrFrameTextBounds(&fTextBounds);
	fTextBounds.bottom = icons.top;
	fTextBounds.top = (short) ((fTextBounds.bottom - gTXRulerUIData.fTabsBarHeight) + 1);
	fTabsBar.SetBounds(fTextBounds);
}


// ROM 0x0024586c GetBounds__9TXRulerUICFP4Rect
void
TXRulerUI::GetBounds(Rect* r) const
{
	*r = fBounds;
}


// ROM 0x00245794 UpdateRulerInfo__9TXRulerUIFRC6RefVar
void
TXRulerUI::UpdateRulerInfo(RefArg info)
{
	if (!fTabsBar.SetRulerMeasure(GetRulerType(info)))
		return;
	TXRulerUIFocusInfo focus;
	Focus(&focus);
	fTabsBar.Draw();
	Unfocus(focus);
}


// ROM 0x00245800 GetRulerType__9TXRulerUIFRC6RefVar
int
TXRulerUI::GetRulerType(RefArg info)
{
	RefVar type(GetFrameSlotRef(info, RSSYMtype));
	return EQRef(type, RSSYMmetric) ? 1 : 0;
}


/*------------------------------------------------------------------------------
	T X N e w t R u l e r U I
------------------------------------------------------------------------------*/

// ROM 0x0024d900 __ct__13TXNewtRulerUIFP5TViewP10TextensionP8PixelMapRC6RefVar
TXNewtRulerUI::TXNewtRulerUI(TView* view, Textension* text, PixelMap* maps, RefArg info)
	: TXRulerUI(text, maps, info)
{
	fView = view;
}


// ROM 0x0024d968 Focus__13TXNewtRulerUIFP18TXRulerUIFocusInfo
void
TXNewtRulerUI::Focus(TXRulerUIFocusInfo* info)
{
	TRegion saved(fView->SetupVisRgn());
	fSavedVis = saved;
	TXRulerUI::Focus(info);
}


// ROM 0x0024d9b4 Unfocus__13TXNewtRulerUIFRC18TXRulerUIFocusInfo
void
TXNewtRulerUI::Unfocus(const TXRulerUIFocusInfo& info)
{
	TXRulerUI::Unfocus(info);
	GrafPtr port;
	GetPort(&port);
	CopyRgn(fSavedVis, port->visRgn);
}
