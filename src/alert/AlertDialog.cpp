/*
	File:		alert/AlertDialog.cpp

	Contains:	The alerts themselves (AlertManager.h): the dialogs and
				their items, the alert font's glyphs, and the drawing on the
				screen's bits.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	The ROM draws a 32-bit word of the screen at a time; the screen's bits
	are big-endian (its pixel order), so the host reads and writes each word
	as the ARM would (GetWordBE/PutWordBE).
*/

#include "AlertManager.h"
#include "Screen.h"
#include "Fonts.h"
#include "TabletBuffer.h"
#include "Unicode.h"
#include "System.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "ObjectHeap.h"

#include <string.h>


Rect	gDisplayRect;							// ROM 0x0c10085c gDisplayRect
Boolean	gLastPollingState;						// ROM 0x0c100864 gLastPollingState

// The glyph cell a character is drawn into before DrawDChar puts it on
// the screen: 16 by 16 pixels, one bit each.
static UByte	gAlertGlyphBuf[0x20];			// ROM 0x0c105f14 gAlertGlyphBuf
static PixelMap	gAlertGlyphPixMap =				// ROM 0x0c100868 gAlertGlyphPixMap
	{ (Ptr) gAlertGlyphBuf, 2, { 0, 0, 16, 16 }, 0 };

extern const unsigned short	gTwoBitTable[256];		// qd/StretchTables.cpp: a byte's pixels at two bits each
extern const unsigned int	gFourBitTable[256];		// ... at four bits each


static inline ULong
GetWordBE(const void* p)
{
	const UByte* b = (const UByte*) p;
	return ((ULong) b[0] << 24) | ((ULong) b[1] << 16) | ((ULong) b[2] << 8) | b[3];
}


static inline void
PutWordBE(void* p, ULong w)
{
	UByte* b = (UByte*) p;
	b[0] = (UByte) (w >> 24);
	b[1] = (UByte) (w >> 16);
	b[2] = (UByte) (w >> 8);
	b[3] = (UByte) w;
}


// An ARM load of a word from an address that may not be word-aligned: the
// aligned word rotated right by the misalignment's bytes.
static inline ULong
LoadWordARM(const UByte* p)
{
	uintptr_t a = (uintptr_t) p;
	ULong w = GetWordBE((const void*) (a & ~(uintptr_t) 3));
	ULong r = (ULong) (a & 3) * 8;
	return r == 0 ? w : ((w >> r) | (w << (32 - r))) & 0xFFFFFFFF;
}


// The ARM's shifts by a register: by 32 or more, nought.
static inline ULong
ArmLSR(ULong value, ULong amount)
{
	amount &= 0xFF;
	return amount >= 32 ? 0 : (value & 0xFFFFFFFF) >> amount;
}


static inline ULong
ArmLSL(ULong value, ULong amount)
{
	amount &= 0xFF;
	return amount >= 32 ? 0 : (value << amount) & 0xFFFFFFFF;
}


static inline Ptr		ScreenBase(void)		{ return gAlertScreenInfo.fScreen.baseAddr; }
static inline long		ScreenRowBytes(void)	{ return gAlertScreenInfo.fScreen.rowBytes; }
static inline ULong		ScreenDepth(void)		{ return gAlertScreenInfo.fScreen.pixMapFlags & 0xFF; }


static void
BlitAlert(Rect* src)
{
	gAlertScreenInfo.fDriver->Blit(&gAlertScreenInfo.fScreen, src, &gDisplayRect, 0);
}


/* -------------------------------------------------------------------------------
	The drawing
------------------------------------------------------------------------------- */

// ROM 0x0002f4dc AlertFastLine__F5PointT1l
// A line on the screen's bits in the mode: across when the two points are
// on one row, otherwise down - one pixel wide at the leftmost point's
// column, whatever the slant (the alerts only ever draw straight lines).
void
AlertFastLine(Point from, Point to, long mode)
{
	long width = 1;
	long left = gAlertScreenInfo.fScreen.bounds.left;
	ULong x = (ULong) ((from.h < to.h ? from.h : to.h) - left);
	ULong depth = ScreenDepth();
	int shift = 3;
	ULong mask = 7;
	if (depth == 1)
	{
		shift = 5;
		mask = 0x1F;
	}
	else if (depth == 2)
	{
		shift = 4;
		mask = 0xF;
	}
	long word = (long) x >> shift;
	long wordsPerRow = ScreenRowBytes() >> 2;
	Point start = from.v < to.v ? from : to;
	UByte* p = (UByte*) ScreenBase() + ScreenRowBytes() * (start.v - gAlertScreenInfo.fScreen.bounds.top) + word * 4;
	if (from.v == to.v)
	{
		long rightmost = from.h < to.h ? to.h : from.h;
		ULong end = (ULong) (rightmost - left) + 1;
		ULong startMask = ArmLSR(0xFFFFFFFF, depth * (x & mask));
		ULong endMask = ArmLSL(0xFFFFFFFF, 32 - depth * (end & mask));
		long words = ((long) end >> shift) - word;
		ULong m = words == 0 ? startMask & endMask : startMask;
		UByte* q = p;
		long k = words - 1;
		if (mode == kAlertModeSet)
		{
			PutWordBE(p, GetWordBE(p) | m);
			if (words - 1 >= 0)
			{
				for (q += 4; k > 0; k--, q += 4)
					PutWordBE(q, 0xFFFFFFFF);
				PutWordBE(q, GetWordBE(q) | endMask);
			}
		}
		else if (mode == kAlertModeInvert)
		{
			PutWordBE(p, GetWordBE(p) ^ m);
			if (words - 1 >= 0)
			{
				for (q += 4; k > 0; k--, q += 4)
					PutWordBE(q, ~GetWordBE(q));
				PutWordBE(q, GetWordBE(q) ^ endMask);
			}
		}
		else
		{
			PutWordBE(p, GetWordBE(p) & ~m);
			if (words - 1 >= 0)
			{
				for (q += 4; k > 0; k--, q += 4)
					PutWordBE(q, 0);
				PutWordBE(q, GetWordBE(q) & ~endMask);
			}
		}
	}
	else
	{
		long top = from.v <= to.v ? from.v : to.v;
		long bottom = from.v <= to.v ? to.v : from.v;
		long rows = (bottom - top) + 1;
		do
		{
			ULong m;
			if ((((x ^ (x + width - 1)) & 0xFFFFFFFF) >> shift) == 0)
			{
				ULong endBits = (x + width) & mask;
				m = ArmLSR(0xFFFFFFFF, depth * (x & mask));
				if (endBits != 0)
					m &= ArmLSL(0xFFFFFFFF, 32 - depth * endBits);
				width = 0;
			}
			else
			{
				// (ROM: shifted by the whole column, not its place in the word)
				m = ArmLSR(0xFFFFFFFF, depth * x);
				width -= (long) ((mask + 1) - (x & mask));
				x = (x + mask) & ~mask;
			}
			UByte* q = p;
			long n = rows;
			if (mode < 2)
			{
				if (mode == kAlertModeInvert)
					do { PutWordBE(q, GetWordBE(q) ^ m); q += wordsPerRow * 4; } while (--n > 0);
				else
					do { PutWordBE(q, GetWordBE(q) | m); q += wordsPerRow * 4; } while (--n > 0);
			}
			else
				do { PutWordBE(q, GetWordBE(q) & ~m); q += wordsPerRow * 4; } while (--n > 0);
			p += 4;
		} while (width != 0);
	}
}


// ROM 0x0002ef30 DrawDLine__FsN31
void
DrawDLine(short fromV, short fromH, short toV, short toH)
{
	Point from = { fromV, fromH };
	Point to = { toV, toH };
	AlertFastLine(from, to, kAlertModeSet);
}


// ROM 0x0002ecb8 DrawDChar__FlT1PUc
// A glyph cell (16 rows of two bytes, one bit a pixel) ORed onto the
// screen's bits at the row and column.
void
DrawDChar(long v, long h, UByte* bits)
{
	long rowBytes = ScreenRowBytes();
	ULong depth = ScreenDepth();
	if (depth == 1)
	{
		ULong s = h & 7;
		UByte* row = (UByte*) ScreenBase() + rowBytes * v + (h >> 3);
		for (int r = 0; r < 16; r++, row += rowBytes)
		{
			UByte* p = row;
			for (int i = 0; i < 2; i++)
			{
				UByte b = *bits++;
				p[0] |= (UByte) (b >> s);
				p++;
				p[0] |= (UByte) ArmLSL(b, 8 - s);
			}
		}
	}
	else if (depth == 2)
	{
		// ROM BUG: each expanded halfword's bytes land one to a halfword -
		// the ROM ORs a byte into the low half of what an unaligned load
		// finds there - so the glyph is drawn at half its width, in every
		// other byte
		ULong s = (h & 3) * 2;
		UByte* row = (UByte*) ScreenBase() + rowBytes * v + (h >> 2);
		for (int r = 0; r < 16; r++, row += rowBytes)
		{
			UByte* p = row;
			for (int i = 0; i < 2; i++)
			{
				long t = (short) gTwoBitTable[*bits++];
				long hi = (short) (t >> s);
				long lo = (short) ArmLSL((ULong) t, 16 - s);
				ULong v1 = ((hi >> 8) & 0xFF) | (LoadWordARM(p) >> 16);
				p[1] = (UByte) v1;
				p[0] = (UByte) (v1 >> 8);
				p += 2;
				ULong v2 = (hi & 0xFF) | (LoadWordARM(p) >> 16);
				p[1] = (UByte) v2;
				p[0] = (UByte) (v2 >> 8);
				p += 2;
				ULong v3 = ((lo >> 8) & 0xFF) | (LoadWordARM(p) >> 16);
				p[1] = (UByte) v3;
				p[0] = (UByte) (v3 >> 8);
				ULong v4 = (LoadWordARM(p + 2) >> 16) | (lo & 0xFF);
				p[3] = (UByte) v4;
				p[2] = (UByte) (v4 >> 8);
			}
		}
	}
	else if (depth == 4)
	{
		ULong s = (h & 1) * 4;
		UByte* row = (UByte*) ScreenBase() + rowBytes * v + (h >> 1);
		for (int r = 0; r < 16; r++, row += rowBytes)
		{
			UByte* p = row;
			for (int i = 0; i < 2; i++)
			{
				ULong t = gFourBitTable[*bits++];
				ULong hi = ArmLSR(t, s);
				ULong lo = ArmLSL(t, 32 - s);
				p[0] |= (UByte) (hi >> 24);
				p[1] |= (UByte) (hi >> 16);
				p[2] |= (UByte) (hi >> 8);
				p[3] |= (UByte) hi;
				p[4] |= (UByte) (lo >> 24);
				p[5] |= (UByte) (lo >> 16);
				p[6] |= (UByte) (lo >> 8);
				p[7] |= (UByte) lo;
				p += 4;
			}
		}
	}
}


// ROM 0x0002efa4 PtInDRect__FlT1P4Rect
// (strictly inside: the edges are not in)
Boolean
PtInDRect(long v, long h, Rect* r)
{
	return r->top < v && v < r->bottom && r->left < h && h < r->right;
}


// ROM 0x0002efe4 InsetDRect__FP4RectsT2
void
InsetDRect(Rect* r, short dv, short dh)
{
	r->top += dv;
	r->bottom -= dv;
	r->left += dh;
	r->right -= dh;
}


// ROM 0x0002f058 PaintDRect__FP4Rect6TDMode
void
PaintDRect(Rect* r, long mode)
{
	Point from = { 0, r->left };
	Point to = { 0, (short) (r->right - 1) };
	for (long row = r->bottom - 1; row >= r->top; row--)
	{
		from.v = to.v = (short) row;
		AlertFastLine(from, to, mode);
	}
}


// ROM 0x0002f0f4 EraseDRect__FP4Rect
void
EraseDRect(Rect* r)
{
	PaintDRect(r, kAlertModeClear);
}


// ROM 0x0002f0fc InvertDRect__FP4Rect
void
InvertDRect(Rect* r)
{
	PaintDRect(r, kAlertModeInvert);
}


// ROM 0x0002f3d4 FrameDRect__FP4Rect6TDMode
void
FrameDRect(Rect* r, long mode)
{
	Point topLeft = { r->top, r->left };
	Point topRight = { r->top, (short) (r->right - 1) };
	Point bottomRight = { (short) (r->bottom - 1), (short) (r->right - 1) };
	Point bottomLeft = { (short) (r->bottom - 1), r->left };
	AlertFastLine(topLeft, topRight, mode);
	AlertFastLine(topRight, bottomRight, mode);
	AlertFastLine(bottomRight, bottomLeft, mode);
	AlertFastLine(topLeft, bottomLeft, mode);
}


// ROM 0x0002f830 AlertGetPoint__FPl
// The pen polled until it is down (a sample, pressed hard enough) or up:
// ==> whether it is up; the point (v, h) where it is down.
UChar
AlertGetPoint(long* point)
{
	long x, y;
	ULong pressure;
	Boolean penUp = false;
	while (PollTablet(&x, &y, &pressure, &penUp) != 0 || pressure > 7)
	{
		if (penUp)
			return penUp;
	}
	point[0] = (short) ((ULong) (y + 0x8000) >> 16);
	point[1] = (short) ((ULong) (x + 0x8000) >> 16);
	return penUp;
}


/* -------------------------------------------------------------------------------
	TAlertItem
------------------------------------------------------------------------------- */

// ROM 0x0002e870 __ct__10TAlertItemFv
TAlertItem::TAlertItem()
	:	fText(nil)
{
	fBounds.top = fBounds.left = fBounds.bottom = fBounds.right = 0;
}


// ROM 0x0002f104 DrawText__10TAlertItemFUc
// The item's text drawn in the alert font: wrapped into its bounds at
// spaces (and returns), as many lines as fit; or, centred, one line in
// the middle of them (a button's).
void
TAlertItem::DrawText(UChar centred)
{
	if (fText == nil)
		return;
	TAlertGlyph glyph;
	UniChar* s = fText;
	long v = fBounds.top;
	long h0 = fBounds.left;
	ULong width = (ULong) (fBounds.right - h0);
	long lineHeight = (long) glyph.GetAlertHeight();
	ULong remaining = (ULong) Ustrlen(s);
	ULong used = 0;
	if (!centred)
	{
		while ((used = 0, *s != 0) && (ULong) (v + lineHeight) <= (ULong) (long) fBounds.bottom)
		{
			ULong n = 0;
			while (n < remaining && used < width)
			{
				used += glyph.GetAlertGlyphWidth(s[n]);
				n++;
			}
			ULong k = n;
			if (width <= used)
			{
				k = n - 1;
				ULong c = s[k];
				n = n - 1;
				if (c != 0 && c != ' ' && c != 0x0D)
					while (k != 0 && (c = s[k], c != 0 && c != ' ' && c != 0x0D))
						k--;
			}
			if (k != 0)
				n = k;
			if (*s == 0x0D)
				n = 1;
			long h = h0;
			for (ULong i = 0; i < n; i++)
			{
				long advance = glyph.GetAlertGlyph(s[i], &gAlertGlyphPixMap);
				DrawDChar(v, h, gAlertGlyphBuf);
				h += advance;
			}
			s += n;
			remaining -= n;
			v += lineHeight + 2;
			while (*s == ' ')
			{
				remaining--;
				s++;
			}
		}
	}
	else
	{
		ULong vOffset = (ULong) ((fBounds.bottom - fBounds.top) - lineHeight) >> 1;
		ULong n = 0;
		while (n < remaining && used < width)
		{
			used += glyph.GetAlertGlyphWidth(s[n]);
			n++;
		}
		// ROM BUG: when the text fits, every character is drawn (the
		// count is the string's length), not the ones measured
		if (width < used)
		{
			remaining = n - 1;
			used -= glyph.GetAlertGlyphWidth(s[remaining]);
		}
		long h = h0 + (long) ((width - used) >> 1);
		for (ULong i = 0; i < remaining; i++)
		{
			long advance = glyph.GetAlertGlyph(s[i], &gAlertGlyphPixMap);
			DrawDChar(v + (long) vOffset, h, gAlertGlyphBuf);
			h += advance;
		}
	}
}


// ROM 0x0002ffe4 DrawButton__10TAlertItemFv
// A round-cornered double frame, the label in the middle.
void
TAlertItem::DrawButton(void)
{
	if (fText[0] == 0)
		return;
	DrawDLine(fBounds.top, (short) (fBounds.left + 2), fBounds.top, (short) (fBounds.right - 2));
	DrawDLine((short) (fBounds.top + 2), fBounds.left, (short) (fBounds.bottom - 2), fBounds.left);
	DrawDLine((short) (fBounds.top + 2), fBounds.right, (short) (fBounds.bottom - 2), fBounds.right);
	DrawDLine(fBounds.bottom, (short) (fBounds.left + 2), fBounds.bottom, (short) (fBounds.right - 2));
	Rect inner = { (short) (fBounds.top + 1), (short) (fBounds.left + 1), fBounds.bottom, fBounds.right };
	FrameDRect(&inner, kAlertModeSet);
	DrawText(1);
}


/* -------------------------------------------------------------------------------
	TAlertDialog
------------------------------------------------------------------------------- */

// ROM 0x0003011c __ct__12TAlertDialogFv
TAlertDialog::TAlertDialog()
	:	fTextCount(0), fButtonCount(0), fTextOffset(0), fButtonOffset(0), fSize(0),
		fFilterProc(nil), fFilterRefCon(nil), fFilterData(nil)
{ }


// ROM 0x00030168 SetFilterProc__12TAlertDialogFPFPvUlT1_UcPv
void
TAlertDialog::SetFilterProc(AlertFilterProcPtr proc, void* refCon)
{
	fFilterProc = proc;
	fFilterRefCon = refCon;
}


// ROM 0x00030174 SetFilterData__12TAlertDialogFPv
void
TAlertDialog::SetFilterData(void* data)
{
	fFilterData = data;
}


// ROM 0x0002e8c0 CheckAlertDone__12TAlertDialogFPUl
// Done when a button has been tapped - or, with a filter proc, when it
// says so (given the button, -1 for none).
UChar
TAlertDialog::CheckAlertDone(ULong* button)
{
	ULong b = CheckButton();
	*button = b;
	if (fFilterProc == nil)
		return b != 0xFFFFFFFF;
	return fFilterProc(fFilterRefCon, b, fFilterData);
}


// Where a button is inverted while the pen is on it
static void
InvertButton(TAlertDialog* dialog, TAlertItem* button)
{
	Rect r = { (short) (button->fBounds.top + 2), (short) (button->fBounds.left + 2),
			   (short) (button->fBounds.bottom - 1), (short) (button->fBounds.right - 1) };
	InvertDRect(&r);
	BlitAlert(&dialog->fBounds);
}


// ROM 0x0002eaa4 CheckButton__12TAlertDialogFv
// The pen tracked while it is down on the alert: a button it is on is
// inverted, and the one it is lifted on is the answer.  ==> the button,
// or -1 (none - no buttons, or the pen is up).
ULong
TAlertDialog::CheckButton(void)
{
	ULong hit = 0xFFFFFFFF;
	if (fButtonCount == 0)
		return 0xFFFFFFFF;
	TAlertItem* buttons = Buttons();
	long point[2];
	if (AlertGetPoint(point) != 0)
		return 0xFFFFFFFF;
search:
	for (ULong i = 0; i < fButtonCount; i++)
	{
		if (PtInDRect(point[0], point[1], &buttons[i].fHitRect))
		{
			InvertButton(this, &buttons[i]);
			hit = i;
			break;
		}
	}
	for ( ; ; )
	{
		do
		{
			if (AlertGetPoint(point) != 0)
				return hit;
			if (hit == 0xFFFFFFFF)
				goto search;
		} while (PtInDRect(point[0], point[1], &buttons[hit].fHitRect));
		InvertButton(this, &buttons[hit]);
		hit = 0xFFFFFFFF;
	}
}


// ROM 0x0002e908 DrawAlert__12TAlertDialogFv
// The alert drawn into the screen's bits at its own bounds - a double
// frame, the texts, the buttons (each with where it is on the display,
// for the pen) - and blitted onto the display at gDisplayRect.
void
TAlertDialog::DrawAlert(void)
{
	Rect r = fBounds;
	if (fSize == 0)
		return;
	EraseDRect(&r);
	FrameDRect(&r, kAlertModeSet);
	InsetDRect(&r, 1, 1);
	FrameDRect(&r, kAlertModeSet);
	if (fTextCount != 0 && fTextOffset != 0)
		for (ULong i = 0; i < fTextCount; i++)
			TextItems()[i].DrawText(0);
	if (fButtonCount != 0 && fButtonOffset != 0)
	{
		for (ULong i = 0; i < fButtonCount; i++)
		{
			TAlertItem* button = &Buttons()[i];
			button->DrawButton();
			// (the ROM's offsets: two down, one to the left, of where the
			// button is on the display)
			button->fHitRect.top = (short) (button->fBounds.top + gDisplayRect.top + 2);
			button->fHitRect.bottom = (short) (button->fBounds.bottom + gDisplayRect.top + 2);
			button->fHitRect.left = (short) (button->fBounds.left + gDisplayRect.left - 1);
			button->fHitRect.right = (short) (button->fBounds.right + gDisplayRect.left - 1);
		}
	}
	BlitAlert(&fBounds);
}


// ROM 0x0003017c Alert__12TAlertDialogFPUl
// The alert put up and waited on until it is done, then taken down.
long
TAlertDialog::Alert(ULong* button)
{
	long err = DisplayAlert();
	if (err != noErr)
		return err;
	while (!CheckAlertDone(button))
		;
	EraseDRect(&fBounds);
	BlitAlert(&fBounds);
	if (!IsSuperMode())
	{
		BlockLCDActivity(false);
		StopDrawing(&gAlertScreenInfo.fScreen, &gDisplayRect);
	}
	SetTabletPolling(gLastPollingState);
	return noErr;
}


// ROM 0x000301bc DisplayAlert__12TAlertDialogFv
// The alert put up: the pen polled for it rather than inked, its place on
// the display worked out (a quarter of the way down, in whole eight-row
// bands, and across the middle in 32-pixel steps), the screen kept from
// anybody else, the alert drawn.
long
TAlertDialog::DisplayAlert(void)
{
	gLastPollingState = TBCGetTabletPolling();
	SetTabletPolling(true);
	gDisplayRect.top = (short) ((gAlertScreenInfo.fScreen.bounds.bottom >> 2) & ~7);
	gDisplayRect.bottom = (short) ((((fBounds.bottom - fBounds.top) + 7) & ~7) + gDisplayRect.top);
	short width = (short) (((fBounds.right - fBounds.left) + 0x1F) & 0xFFE0);
	gDisplayRect.left = (short) ((gAlertScreenInfo.fScreen.bounds.right - width) >> 1);
	gDisplayRect.right = (short) (gDisplayRect.left + width);
	if (!IsSuperMode())
	{
		BlockLCDActivity(true);
		StartDrawing(&gAlertScreenInfo.fScreen, &gDisplayRect);
	}
	DrawAlert();
	return noErr;
}


// ROM 0x000302bc RemoveAlert__12TAlertDialogFv
// The alert taken down: its place in the screen's bits cleared, and the
// display given back (StopDrawing shows the screen there again).
long
TAlertDialog::RemoveAlert(void)
{
	EraseDRect(&fBounds);
	BlitAlert(&fBounds);
	if (!IsSuperMode())
	{
		BlockLCDActivity(false);
		StopDrawing(&gAlertScreenInfo.fScreen, &gDisplayRect);
	}
	SetTabletPolling(gLastPollingState);
	return noErr;
}


/* -------------------------------------------------------------------------------
	The ROM's own alerts
------------------------------------------------------------------------------- */

static short
Coordinate(Ref frame, Ref slot)
{
	return (short) RINT(GetFrameSlotRef(frame, slot));
}


static void
BoundsFrom(Ref frame, Rect* r)
{
	r->top = Coordinate(frame, RSSYMtop);
	r->left = Coordinate(frame, RSSYMleft);
	r->bottom = Coordinate(frame, RSSYMbottom);
	r->right = Coordinate(frame, RSSYMright);
}


// ROM 0x0002f8bc __ct__19TOSErrorAlertDialogFv
TOSErrorAlertDialog::TOSErrorAlertDialog()
{
	fTextCount = 1;
	fButtonCount = 1;
	fTextOffset = (long) ((char*) fText - (char*) this);
	fButtonOffset = (long) ((char*) fButton - (char*) this);
	fSize = sizeof(TOSErrorAlertDialog);		// (the ROM: 0x50)
	BoundsFrom(Rkoserroralertbounds, &fBounds);
	BoundsFrom(Rkoserroralerttextbounds, &fText[0].fBounds);
	BoundsFrom(Rkoserroralertbutton0bounds, &fButton[0].fBounds);
}


// ROM 0x0002fbc8 Init__19TOSErrorAlertDialogFPUsT1
void
TOSErrorAlertDialog::Init(UniChar* text, UniChar* button)
{
	fText[0].fText = text;
	fButton[0].fText = button;
}


// ROM 0x0002fbd4 __ct__25TErasePersistentDataAlertFv
TErasePersistentDataAlert::TErasePersistentDataAlert()
{
	fTextCount = 1;
	fButtonCount = 2;
	fTextOffset = (long) ((char*) fText - (char*) this);
	fButtonOffset = (long) ((char*) fButtons - (char*) this);
	fSize = sizeof(TErasePersistentDataAlert);	// (the ROM: 100)
	BoundsFrom(Rkoserroralertbounds, &fBounds);
	BoundsFrom(Rkoserroralerttextbounds, &fText[0].fBounds);
	BoundsFrom(Rkoserroralertbutton0bounds, &fButtons[0].fBounds);
	BoundsFrom(Rkoserroralertbutton1bounds, &fButtons[1].fBounds);
}


// ROM 0x0002ffac Init__25TErasePersistentDataAlertFPUsN21
void
TErasePersistentDataAlert::Init(UniChar* text, UniChar* button0, UniChar* button1)
{
	fText[0].fText = text;
	fButtons[0].fText = button0;
	fButtons[1].fText = button1;
}


// ROM 0x0002ffbc Init__25TErasePersistentDataAlertFP4RectPUsN22
void
TErasePersistentDataAlert::Init(Rect* textBounds, UniChar* text, UniChar* button0, UniChar* button1)
{
	fText[0].fBounds = *textBounds;
	fText[0].fText = text;
	fButtons[0].fText = button0;
	fButtons[1].fText = button1;
}


/* -------------------------------------------------------------------------------
	TAlertGlyph
------------------------------------------------------------------------------- */

static inline ULong	Get8(const char* p)		{ return (UByte) p[0]; }
static inline ULong	Get16(const char* p)	{ return (Get8(p) << 8) | Get8(p + 1); }
static inline ULong	Get32(const char* p)	{ return (Get16(p) << 16) | Get16(p + 2); }


// ROM 0x0003033c __ct__11TAlertGlyphFv
// The alert font's, at 9 - which the ROM passes to LocateEntry as it is,
// where it takes a 16.16 size: so the strike it gets is the one nearest a
// size of nought, the smallest.
TAlertGlyph::TAlertGlyph()
{
	InitGlyph(RefVar(Ralertfont), 9);
}


// ROM 0x0003037c __ct__11TAlertGlyphFRC6RefVarl
TAlertGlyph::TAlertGlyph(RefArg font, long size)
{
	InitGlyph(font, size);
}


// ROM 0x000303c0 InitGlyph__11TAlertGlyphFRC6RefVarl
// The font's cmap subtable and its mapping, and the strike for the size:
// its glyph range, ascender and descender, its index subtables, the
// bitmap data.
void
TAlertGlyph::InitGlyph(RefArg font, long size)
{
	const char* sfnt = (const char*) BinaryData(font);
	const char* cmap = FindFontTable(sfnt, 'cmap');
	fCmap = cmap + Get32(cmap + 8);
	switch (Get16(fCmap))
	{
	case 0:		fMap = MapFormat0;	break;
	case 2:		fMap = MapFormat2;	break;
	case 4:		fMap = MapFormat4;	break;
	case 6:		fMap = MapFormat6;	break;
	}
	const char* bloc = FindFontTable(sfnt, 'bloc');
	const char* entry = LocateEntry(size, bloc);
	fFirstGlyph = Get16(entry + 0x28);
	fLastGlyph = Get16(entry + 0x2a);
	fAscender = (SChar) entry[0x18];
	fDescender = (SChar) entry[0x19];
	fIndexArray = bloc + Get32(entry);
	fBdat = FindFontTable(sfnt, 'bdat');
}


// ROM 0x000304c4 GetAlertHeight__11TAlertGlyphFv
ULong
TAlertGlyph::GetAlertHeight(void)
{
	return (ULong) (fAscender - fDescender) & 0xFF;
}


// ROM 0x000304d8 GetAlertGlyphWidth__11TAlertGlyphFl
// The character's glyph found - its bitmap and metrics - and its advance.
UChar
TAlertGlyph::GetAlertGlyphWidth(long ch)
{
	ULong glyph = (ULong) fMap(ch, fCmap);
	// ROM BUG: && where || was meant - a glyph outside the strike is never
	// the missing glyph
	if (glyph < fFirstGlyph && fLastGlyph < glyph)
		glyph = 0;
	const char* entry = fIndexArray;
	while (Get16(entry + 2) < glyph)
		entry += 8;
	if (glyph < Get16(entry))
		glyph = 0;
	long index = (long) glyph - (long) Get16(entry);
	const char* sub = fIndexArray + Get32(entry + 4);
	ULong offset;
	ULong indexFormat = Get16(sub);
	if (indexFormat == 1)
		offset = Get32(sub + 8 + index * 4);
	else if (indexFormat == 3)
		offset = Get16(sub + 8 + index * 2);
	else
		offset = 0;
	const UByte* p = (const UByte*) fBdat + offset + Get32(sub + 4);
	fBits = p;
	ULong imageFormat = Get16(sub + 2);
	if (imageFormat == 1 || imageFormat == 6)
	{
		fHeight = p[0];
		fWidth = p[1];
		fBearingX = (SChar) p[2];
		fBearingY = (SChar) p[3];
		fAdvance = p[4];
		fBits = p + (imageFormat == 1 ? 5 : 8);
	}
	if (fBearingX < 0)
		fBearingX = 0;
	fRowBytes = (fWidth + 7) >> 3;
	fTop = (UChar) ((SChar) fAscender - fBearingY);
	long below = ((long) fHeight - (long) fBearingY) + (SChar) fDescender;
	if (below > 0)
		fHeight = (UChar) (fHeight - below);
	return fAdvance;
}


// ROM 0x00030680 GetAlertGlyph__11TAlertGlyphFlP8PixelMap
// The glyph drawn into the cell (cleared first).  ==> its advance.
UChar
TAlertGlyph::GetAlertGlyph(long ch, PixelMap* map)
{
	UChar advance = GetAlertGlyphWidth(ch);
	fCellHeight = (UChar) (map->bounds.bottom - map->bounds.top);
	fCellWidth = (UChar) (map->bounds.right - map->bounds.left);
	memset(map->baseAddr, 0, map->rowBytes * fCellHeight);
	Portrait(map);
	return advance;
}


// ROM 0x00030708 Portrait__11TAlertGlyphFP8PixelMap
// The glyph's bitmap ORed into the cell at its row and bearing, clipped to
// the cell.
void
TAlertGlyph::Portrait(PixelMap* map)
{
	ULong top = fTop;
	if ((ULong) fCellHeight < fHeight + top)
		fHeight = (UChar) (fCellHeight - fTop);
	long bearing = fBearingX;
	if ((long) fCellWidth < (long) (fWidth + bearing))
		fRowBytes = ((long) fCellWidth - bearing) >> 3;
	UByte* row = (UByte*) map->baseAddr;
	if (top != 0)
		row += top * map->rowBytes;
	row += bearing >> 3;
	const UByte* src = fBits;
	for (ULong n = fHeight; n != 0; n--, row += map->rowBytes)
	{
		UByte* p = row;
		for (long i = fRowBytes; i != 0; i--)
		{
			p[0] |= (UByte) (*src >> (bearing & 7));
			p[1] |= (UByte) ArmLSL(*src, 8 - (bearing & 7));
			p++;
			src++;
		}
	}
}
