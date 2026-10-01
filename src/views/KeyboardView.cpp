/*
	File:		views/KeyboardView.cpp

	Contains:	TKeyboardView and its iterators (KeyboardView.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "KeyboardView.h"
#include "Inker.h"			// BusyBoxSend
#include "SoundSettings.h"	// FPlaySound
#include "Rects.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "ViewFlags.h"
#include "Keyboard.h"
#include "Draw.h"
#include "Shapes.h"
#include "Ports.h"
#include "Pictures.h"
#include "Text.h"
#include "REPTranslators.h"
#include "Unicode.h"
#include "RSSymbols.h"
#include "RootView.h"
#include "Application.h"
#include "Commands.h"
#include "Regions.h"
#include "RegionVars.h"
#include "Screen.h"
#include "UnitPublic.h"
#include "Stroke.h"
#include "NewtonExceptions.h"
#include "NewtonTime.h"


/*------------------------------------------------------------------------------
	T R a w K e y I t e r a t o r
------------------------------------------------------------------------------*/

// The slots of a key within its row: two numbers come first, then three
// slots each.
enum { kRowHeaderSlots = 2, kSlotsPerKey = 3 };


// ROM 0x000fac10 __ct__15TRawKeyIteratorFRC6RefVar
// The definition taken, and its keys counted so that a caller can size
// something to it.
TRawKeyIterator::TRawKeyIterator(RefArg keys)
{
	fInfo = 0;
	fKeys = keys;
	fRowCount = Length(fKeys);
	fTotalKeys = 0;
	for (long i = 0; i < fRowCount; i++)
	{
		RefVar row(GetArraySlotRef(fKeys, i));
		fTotalKeys += (Length(row) - kRowHeaderSlots) / kSlotsPerKey;
	}
	Reset();
}


// ROM 0x000fad08 Reset__15TRawKeyIteratorFv
void
TRawKeyIterator::Reset(void)
{
	fKeyIndex = 0;
	fRowIndex = 0;
	LoadRow();
	LoadKey();
}


// ROM 0x000fcfec LoadRow__15TRawKeyIteratorFv
// The row at the index, and how many keys are in it.
void
TRawKeyIterator::LoadRow(void)
{
	fKeyIndex = 0;
	if (Done())
	{
		fRow = NILREF;
		fRowKeys = 0;
		return;
	}
	fRow = GetArraySlotRef(fKeys, fRowIndex);
	fRowKeys = (Length(fRow) - kRowHeaderSlots) / kSlotsPerKey;
}


// ROM 0x000fceb8 LoadKey__15TRawKeyIteratorFv
// The key's three slots.  ==> true when the walk has run out, in which
// case the legend and the result are nil and the info word is left as
// NILREF - which is a Ref where a packed word is wanted, but nothing
// reads it once the walk is over.
Boolean
TRawKeyIterator::LoadKey(void)
{
	if (fKeyIndex < fRowKeys && fRowIndex < fRowCount)
	{
		long slot = fKeyIndex * kSlotsPerKey + kRowHeaderSlots;
		fLegend = GetArraySlotRef(fRow, slot);
		fResult = GetArraySlotRef(fRow, slot + 1);
		fInfo = RINT(GetArraySlotRef(fRow, slot + 2));
		return false;
	}
	fLegend = NILREF;
	fResult = NILREF;
	fInfo = NILREF;
	return true;
}


// ROM 0x000fd064 Next__15TRawKeyIteratorFv
Boolean
TRawKeyIterator::Next(void)
{
	if (++fKeyIndex >= fRowKeys)
	{
		fRowIndex++;
		LoadRow();
	}
	return LoadKey();
}


// ROM 0x000fb7a8 CopyInto__15TRawKeyIteratorFP15TRawKeyIterator
void
TRawKeyIterator::CopyInto(TRawKeyIterator* other)
{
	other->fKeys = (Ref) fKeys;
	other->fLegend = (Ref) fLegend;
	other->fResult = (Ref) fResult;
	other->fInfo = fInfo;
	other->fRow = (Ref) fRow;
	other->fRowIndex = fRowIndex;
	other->fRowCount = fRowCount;
	other->fKeyIndex = fKeyIndex;
	other->fRowKeys = fRowKeys;
	other->fTotalKeys = fTotalKeys;
}


/*------------------------------------------------------------------------------
	T V i s K e y I t e r a t o r
------------------------------------------------------------------------------*/

// A row's measurement in eighths of the cell.  The ROM rounds this one
// towards zero - seven added to a negative product before the shift -
// although a row's numbers are never negative.
static inline long
RowEighths(long units, long of)
{
	long product = units * of;
	if (product < 0)
		product += 7;
	return product >> 3;
}

// A key's, which the ROM shifts without that: the width and height are a
// byte each and the cell is positive, so there is nothing to round.
static inline long
KeyEighths(long units, long of)
{
	return (long) (((ULong) (units * of)) >> 3);
}


// ROM 0x000fd0ac __ct__15TVisKeyIteratorFRC6RefVarR5TRect6TPoint
// The definition walked as it is laid out, a key at a time, from the
// origin.  `cell` is the unit a key's eighths are of: its bottom is the
// height and its right the width.
TVisKeyIterator::TVisKeyIterator(RefArg keys, const Rect& cell, Point origin)
	: TRawKeyIterator(keys)
{
	fCell = cell;
	fOrigin = origin;
	fX = origin.h;
	fY = origin.v;
	Reset();
}


// ROM 0x000fd124 Reset__15TVisKeyIteratorFv
Boolean
TVisKeyIterator::Reset(void)
{
	TRawKeyIterator::Reset();
	fX = fOrigin.h;
	fY = fOrigin.v;
	LoadRow();
	return LoadKey();
}


// ROM 0x000faf80 LoadRow__15TVisKeyIteratorFv
// The row's geometry: it starts a pitch below the one before it, is as
// wide as its keys add up to and as tall as its second number says, and
// remembers which of its entries are keys rather than gaps (the key
// chain walks between rows through the first and the last of them).
void
TVisKeyIterator::LoadRow(void)
{
	TRawKeyIterator::LoadRow();
	if (Done())
	{
		fRowHeight = 0;
		fRowPitch = 0;
		SetRect(&fRowBounds, 0, 0, 0, 0);
		return;
	}
	if (fRowIndex != 0)
		fY += fRowPitch;			// (the row before this one's)
	fX = fOrigin.h;
	fRowPitch = RowEighths(RINT(GetArraySlotRef(fRow, 0)), fCell.bottom);
	fRowHeight = RowEighths(RINT(GetArraySlotRef(fRow, 1)), fCell.bottom);
	fRowBounds.left = fOrigin.h;
	fRowBounds.top = (short) fY;
	fRowBounds.bottom = (short) (fY + fRowHeight);
	fRowHasKeys = false;
	fFirstKey = 0;
	fLastKey = 0;
	long across = 0;
	for (long i = 0; i < fRowKeys; i++)
	{
		long info = RINT(GetArraySlotRef(fRow, i * kSlotsPerKey + kRowHeaderSlots + 2));
		// (the width and the gap bit are both taken off the word shifted
		//  down by eight, so the bit that is really being read for the gap
		//  is the info word's sign.  Every gap in the ROM's own keyboards
		//  has both set, so the two answer alike; the reconstruction keeps
		//  the ROM's arithmetic rather than the meaning it was after.)
		long shifted = info >> kKeyWidthShift;
		across += shifted & kKeyWidthMask;
		if ((shifted & kKeyIsGap) == 0)
		{
			if (!fRowHasKeys)
			{
				fRowHasKeys = true;
				fFirstKey = i;
			}
			fLastKey = i;
		}
	}
	fRowBounds.right = (short) (fRowBounds.left + RowEighths(across, fCell.right));
	fRowBounds.right++;				// (a pixel over, so the edge is drawn)
	fRowBounds.bottom++;
}


// ROM 0x000fadd8 LoadKey__15TVisKeyIteratorFv
// Where the key goes: the cell scaled to the eighths its info word asks
// for, put down at the pen, and the pen moved to its right edge.  The
// face is the cell with its bottom and right pushed out by the depth and
// the shadow the same cell with its top and left pushed in by it, which
// is what gives a key its raised edge.
Boolean
TVisKeyIterator::LoadKey(void)
{
	Boolean done = TRawKeyIterator::LoadKey();
	if (done)
	{
		SetRect(&fKeyBounds, 0, 0, 0, 0);
		SetRect(&fKeyFace, 0, 0, 0, 0);
		SetRect(&fKeyShadow, 0, 0, 0, 0);
		return done;
	}
	fKeyBounds = fCell;
	fKeyBounds.right = (short) KeyEighths((fInfo >> kKeyWidthShift) & kKeyWidthMask, fKeyBounds.right);
	fKeyBounds.bottom = (short) KeyEighths(fInfo & kKeyHeightMask, fKeyBounds.bottom);
	OffsetRect(&fKeyBounds, fX, fY);
	fX = fKeyBounds.right;
	long inset = (fInfo >> kKeyInsetShift) & kKeyInsetMask;
	if (inset != 0)
		InsetRect(&fKeyBounds, inset, inset);
	fKeyFace = fKeyBounds;
	fKeyShadow = fKeyFace;
	long depth = (fInfo >> kKeyDepthShift) & kKeyDepthMask;
	if (depth != 0)
	{
		fKeyShadow.left = (short) (fKeyShadow.left + depth);
		fKeyShadow.top = (short) (fKeyShadow.top + depth);
		fKeyFace.right = (short) (fKeyFace.right + depth);
		fKeyFace.bottom = (short) (fKeyFace.bottom + depth);
	}
	return done;
}


// ROM 0x000fad38 Next__15TVisKeyIteratorFv
Boolean
TVisKeyIterator::Next(void)
{
	if (++fKeyIndex >= fRowKeys)
	{
		fRowIndex++;
		LoadRow();
	}
	return LoadKey();
}


// ROM 0x000fad80 SkipToStartOfNextRow__15TVisKeyIteratorFv
// On to the next row that has a key in it, so that a keyboard's rows of
// nothing but gaps are stepped over rather than walked.
Boolean
TVisKeyIterator::SkipToStartOfNextRow(void)
{
	if (Done())
		return true;
	do
	{
		fRowIndex++;
		LoadRow();
	}
	while (fRowIndex <= fRowCount && !fRowHasKeys);
	return LoadKey();
}


// ROM 0x000fb194 FindEnclosingKey__15TVisKeyIteratorF6TPoint
// The walk left standing on the key the point is in, from the beginning
// when it is not there already.  Gaps are not keys.  ==> whether one was
// found.
Boolean
TVisKeyIterator::FindEnclosingKey(Point pt)
{
	if (fRowIndex != 0 || fKeyIndex != 0)
		Reset();
	Boolean done = false;
	Boolean found = false;
	do
	{
		found = PtInRect(pt, &fKeyFace) && (fInfo & kKeyIsGap) == 0;
		if (!found)
			done = Next();
	}
	while (!done && !found);
	return found;
}


// ROM 0x000fd164 CopyInto__15TVisKeyIteratorFP15TVisKeyIterator
void
TVisKeyIterator::CopyInto(TVisKeyIterator* other)
{
	TRawKeyIterator::CopyInto(other);
	other->fKeyBounds = fKeyBounds;
	other->fKeyFace = fKeyFace;
	other->fKeyShadow = fKeyShadow;
	other->fRowBounds = fRowBounds;
	other->fCell = fCell;
	other->fOrigin = fOrigin;
	other->fX = fX;
	other->fY = fY;
	other->fRowPitch = fRowPitch;
	other->fRowHeight = fRowHeight;
}


/*------------------------------------------------------------------------------
	T K e y b o a r d V i e w
------------------------------------------------------------------------------*/

// How coarsely the keyboard's cell has to be rounded for every key's
// width (or height) in eighths to come out a whole number of pixels,
// indexed by the low three bits of all the keys' eighths ORed together.
// The mask is this with 0xfff8 over it: widths that are all multiples of
// eight will divide anything, one that is a multiple of four needs an
// even cell, and anything else needs a multiple of eight.
//
// (ROM 0x00371a44, in qdConstants' neighbourhood; eight bytes.)
static const unsigned char kCellRounding[8] = { 7, 0, 4, 0, 6, 0, 4, 0 };


// ROM 0x000fb220 ClassID__13TKeyboardViewCFv
long
TKeyboardView::ClassID(void) const
{
	return clKeyboardView;
}


// ROM 0x000fb228 DerivedFrom__13TKeyboardViewCFl
Boolean
TKeyboardView::DerivedFrom(long id) const
{
	return id == clKeyboardView || TView::DerivedFrom(id);
}


// ROM 0x000fb25c Constructor__13TKeyboardViewFRC6RefVarP5TView
// The keyboard read out of the context, and its cell worked out from the
// bounds it has been given: the cell is as wide as the view divided by
// the widest row and as tall as the view divided by all the rows' pitches
// - so a keyboard fills whatever it is put in - and then rounded down so
// that no key's eighths land between pixels.
void
TKeyboardView::Constructor(RefArg context, TView* parent)
{
	TView::Constructor(context, parent);
	long exists = 0;
	fKeyDefinitions = GetProtoVariable(fContext, RSSYMkeydefinitions, nil);
	Ref value = GetProtoVariable(fContext, RSSYMkeyarrayindex, &exists);
	fKeyArrayIndex = exists ? RINT(value) : 0;
	value = GetProtoVariable(fContext, RSSYMkeyresultsarekeycodes, &exists);
	fResultsAreKeycodes = exists ? NOTNIL(value) : false;
	value = GetProtoVariable(fContext, RSSYMkeysound, &exists);
	fHasKeySound = exists ? NOTNIL(value) : false;

	CreateTextStyleRecord(RefVar(GetVariable(fContext, RSSYMviewfont, nil, 0)), &fStyle);
	fStylePtr = &fStyle;
	FontInfo info;
	GetStyleFontInfo(&fStyle, &info);
	fAscent = info.ascent;
	fDescent = info.descent;
	fTextOptions.fAlignment = 0x8000;		// centred
	fTextOptions.fJustification = 0;
	fTextOptions.fWidth = 0;
	fTextOptions.fFittedWidth = 0;
	fTextOptions.fTransferMode = 1;
	fTextOptions.fReserved = 0;
	fTextOptions.fScanner = nil;

	fKeyReceiverView = GetProtoVariable(fContext, RSSYMkeyreceiverview, &exists);
	if (!exists)
		fKeyReceiverView = RSSYMviewfrontkey;

	// the widest row, the rows' pitches, and the eighths every key asks for
	long widest = 0;
	long down = 0;
	long widthBits = 0;
	long heightBits = 0;
	long tallestKey = 0;
	{
		TRawKeyIterator iter(fKeyDefinitions);
		long across = 0;
		heightBits = RINT(GetArraySlotRef(iter.fRow, 0));
		while (!iter.Done())
		{
			if (iter.fKeyIndex <= 0)
			{
				across = 0;
				tallestKey = 0;
				down += RINT(GetArraySlotRef(iter.fRow, 0)) & 0xff;
			}
			long wide = (iter.fInfo >> kKeyWidthShift) & kKeyWidthMask;
			across += wide;
			long high = iter.fInfo & kKeyHeightMask;
			if ((ULong) high > (ULong) tallestKey)
				tallestKey = high;			// (worked out and not used)
			widthBits |= wide;
			heightBits |= high;
			if (iter.fKeyIndex + 1 >= iter.fRowKeys && across > widest)
				widest = across;
			if (iter.Next())
				break;
		}
	}
	long cellWidth = ((viewBounds.right - viewBounds.left) * 8) / widest;
	cellWidth &= 0xfff8 | kCellRounding[widthBits & 7];
	long cellHeight = ((viewBounds.bottom - viewBounds.top) * 8) / down;
	cellHeight &= 0xfff8 | kCellRounding[heightBits & 7];
	SetRect(&fCell, 0, 0, cellWidth, cellHeight);
}


// One of a key's two slots, taken through the same two turns: an array
// is this keyboard's entry of it, and a function is called on the view
// and its answer taken (and taken through the array turn again).
static Ref
KeySlot(TKeyboardView* view, RefArg slot, long index)
{
	RefVar it(slot);
	if (EQRef(ClassOf(it), RSSYMarray))
		it = GetArraySlotRef(it, index);
	else if (IsFunction(it))
	{
		it = DoScript(view->fContext, it, RefVar(NILREF));
		if (EQRef(ClassOf(it), RSSYMarray))
			it = GetArraySlotRef(it, index);
	}
	return it;
}


// ROM 0x000fb660 GetLegendRef__13TKeyboardViewFR15TRawKeyIterator
// What is drawn on the key: its legend, or its result when it has no
// legend of its own.
Ref
TKeyboardView::GetLegendRef(TRawKeyIterator& iter)
{
	RefVar legend(iter.fLegend);
	if (ISNIL(legend))
		legend = (Ref) iter.fResult;
	return KeySlot(this, legend, fKeyArrayIndex);
}


// ROM 0x000fb81c GetResultRef__13TKeyboardViewFR15TRawKeyIterator
// What the key produces.
Ref
TKeyboardView::GetResultRef(TRawKeyIterator& iter)
{
	return KeySlot(this, RefVar(iter.fResult), fKeyArrayIndex);
}


// ROM 0x000fbde0 DrawKeyFrame__13TKeyboardViewFR15TVisKeyIteratorUcT2
// The key's outline, rounded by as much as its info word asks for and
// drawn with a pen as thick as its 3-D depth.  `fill` paints it solid,
// which is what a pressed or hilited key gets.
//
// The four bits at 0x10000 are how a key is made to have square corners
// on the sides where it meets the edge of the keyboard: the rectangle is
// pushed a hundred pixels out on those sides, so its rounded corner is
// far away, and the drawing is clipped back to where the key really is.
void
TKeyboardView::DrawKeyFrame(TVisKeyIterator& iter, Boolean fill, Boolean keepBackground)
{
	Rect face = iter.fKeyFace;
	long round = 2 + 2 * ((iter.fInfo >> 20) & 7);
	long squared = (iter.fInfo >> 16) & 0xf;
	long depth = (iter.fInfo >> kKeyDepthShift) & kKeyDepthMask;
	Rect clip = face;
	if (squared != 0)
	{
		if (iter.fInfo & 0x80000)	face.left = (short) (face.left - 100);
		if (iter.fInfo & 0x20000)	face.top = (short) (face.top - 100);
		if (iter.fInfo & 0x10000)	face.right = (short) (face.right + 100);
		if (iter.fInfo & 0x40000)	face.bottom = (short) (face.bottom + 100);
	}
	if (fill)
	{
		if (squared != 0)
			ClipRect(&clip);
		// (the ROM has a PaintRect here for a rounding of nothing, which
		//  cannot happen: the rounding is two plus twice a field)
		if (round != 0)
			PaintRoundRect(&face, round, round);
		else
			PaintRect(&face);
	}
	else if (depth != 0)
	{
		PenSize(depth, depth);
		if (squared != 0)
			ClipRect(&clip);
		Boolean erase = keepBackground || (fViewFormat & 0xf) != 1;
		if (round != 0)
		{
			if (erase)
				EraseRoundRect(&face, round, round);
			FrameRoundRect(&face, round, round);
		}
		else
		{
			if (erase)
				EraseRect(&face);
			FrameRect(&face);
		}
		PenNormal();
	}
	if (squared != 0)
	{
		Rect wide;
		SetRect(&wide, -32767, -32767, 32766, 32766);
		ClipRect(&wide);
	}
}


// ROM 0x000fb944 DrawKey__13TKeyboardViewFR15TVisKeyIteratorUcT2
// A key drawn: its outline, and then its legend centred in it.  A legend
// is a string, a character, a bitmap frame, or a number - which is a key
// code to be labelled when the keyboard's results are key codes, and a
// number to be written out when they are not.  A keyboard of key codes
// also draws a key that is being held down as though it were hilited,
// which is what keeps shift and the option keys looking pressed.
void
TKeyboardView::DrawKey(TVisKeyIterator& iter, Boolean hilited, Boolean keepBackground)
{
	RefVar legend(GetLegendRef(iter));
	long code = -1;
	Boolean hasText = false;
	Boolean isBitmap = false;
	UniChar oneChar[2];
	UniChar number[32];
	const UniChar* text = nil;
	long length = 0;
	Point bitmapSize;
	bitmapSize.h = 0;
	bitmapSize.v = 0;

	if (EQRef(ClassOf(legend), RSSYMstring))
	{
		hasText = true;
		text = (const UniChar*) BinaryData(legend);
		length = (Length(legend) - sizeof(UniChar)) / sizeof(UniChar);
	}
	else if (ISCHAR((Ref) legend))
	{
		hasText = true;
		oneChar[0] = RCHAR(legend);
		text = oneChar;
		length = 1;
	}
	else if (ISINT((Ref) legend) && fResultsAreKeycodes)
	{
		hasText = true;
		code = RINT(legend);
		oneChar[0] = KeyLabel((ULong) code, false);
		text = oneChar;
		length = 1;
	}
	else if (ISINT((Ref) legend))
	{
		hasText = true;
		IntegerString(RINT(legend), number);
		text = number;
		length = Ustrlen(number);
	}
	else if (EQRef(ClassOf(legend), RSSYMframe) && FrameHasSlotRef(legend, RSSYMbits))
	{
		isBitmap = true;
		Rect bounds;
		FromObject(RefVar(GetFrameSlotRef(legend, RSSYMbounds)), bounds);
		bitmapSize.h = (short) (bounds.right - bounds.left);
		bitmapSize.v = (short) (bounds.bottom - bounds.top);
	}
	else
		return;					// nothing to draw it with

	if (fResultsAreKeycodes)
	{
		if (code == -1)
		{
			RefVar result(GetResultRef(iter));
			if (ISINT((Ref) result))
				code = RINT(result);
		}
		if (code != -1 && KeyDown((ULong) code, false))
			hilited = true;
	}
	DrawKeyFrame(iter, hilited, keepBackground);

	if (isBitmap)
	{
		// centred in the key's shadow rectangle, which is the face
		// without its raised edge
		Rect at;
		at.left = (short) (((iter.fKeyShadow.left + iter.fKeyShadow.right) - bitmapSize.h) / 2);
		at.right = (short) (at.left + bitmapSize.h);
		at.top = (short) (((iter.fKeyShadow.top + iter.fKeyShadow.bottom) - bitmapSize.v) / 2);
		at.bottom = (short) (at.top + bitmapSize.v);
		DrawBitmap(legend, &at, hilited ? 3 : 1);
	}
	else if (hasText)
	{
		fTextOptions.fTransferMode = hilited ? 3 : 1;
		fTextOptions.fWidth = ToFixed(iter.fKeyShadow.right - iter.fKeyShadow.left + 6);
		FPoint where;
		where.x = ToFixed(iter.fKeyShadow.left - 3);
		long spare = (iter.fKeyShadow.top + iter.fKeyShadow.bottom) - (fAscent + fDescent);
		where.y = ToFixed(fAscent + (spare + ((ULong) spare >> 31)) / 2);
		DrawTextOnce(text, length, &fStylePtr, nil, where, &fTextOptions, nil);
	}
}


// ROM 0x000fbfd4 RealDraw__13TKeyboardViewFR5TRect
// The keys drawn, from the top left of the view.  A row whose rectangle
// is nowhere near what has to be redrawn is stepped over whole rather
// than key by key, which is what makes a keyboard cheap to update when a
// single key changes.  keyHighlightKeys names the results whose keys are
// to be drawn pressed.
void
TKeyboardView::RealDraw(Rect& bounds)
{
	RefVar highlight(GetProtoVariable(fContext, RSSYMkeyhighlightkeys, nil));
	long highlights = ISNIL(highlight) ? 0 : Length(highlight);
	RefVar index(GetProtoVariable(fContext, RSSYMkeyarrayindex, nil));
	fKeyArrayIndex = ISNIL(index) ? 0 : RINT(index);
	PenNormal();
	Point at;
	at.v = viewBounds.top;
	at.h = viewBounds.left;
	TVisKeyIterator iter(fKeyDefinitions, fCell, at);
	Boolean done;
	do
	{
		if (iter.fRowHasKeys && iter.fKeyIndex == iter.fFirstKey
			&& !Intersects(&iter.fRowBounds, &bounds))
		{
			done = iter.SkipToStartOfNextRow();
			continue;
		}
		if (Intersects(&iter.fKeyFace, &bounds) && (iter.fInfo & kKeyIsGap) == 0)
		{
			Boolean hilited = false;
			if (highlights > 0)
			{
				RefVar result(GetResultRef(iter));
				for (long i = 0; i < highlights; i++)
					if (EQRef(GetArraySlotRef(highlight, i), result))
					{
						hilited = true;
						break;
					}
			}
			DrawKey(iter, hilited, false);
		}
		done = iter.Next();
	}
	while (!done);
	PenNormal();
}


// The auto-repeat, in the units the Newton keeps time in (1/3686400 of a
// second): a key held down for three fifths of a second starts repeating,
// and repeats every fifth of a second after that.
enum
{
	kKeyRepeatDelay		= 0x0021bf10,
	kKeyRepeatInterval	= 0x000b3fb0
};

// The bit of a key's info word that says it changes when it is pressed.
// A key without it - the space bar on some keyboards - is left alone.
enum { kKeyHilites = 0x10000000 };


// ROM 0x000fc1f8 GetKeyReceiver__FRC6RefVarT1
TView*
GetKeyReceiver(RefArg context, RefArg name)
{
	TView* view = GetView(context, name);
	if (view == nil)
		view = gRootView->fCaretView;
	return view;
}


// ROM 0x000fc220 PostKeypressCommands__13TKeyboardViewFRC6RefVar
// A key whose result is a string: every character of it posted to the
// receiver as a key down and a key up, one after another.  The receiver
// is asked for again between characters, because handling one of them may
// have moved the caret somewhere else.
void
TKeyboardView::PostKeypressCommands(RefArg text)
{
	UniChar chars[0x40];
	long length = 0;
	StringObject(text, chars, length, 0x3f);
	for (long i = 0; chars[i] != 0; i++)
	{
		TView* receiver = GetKeyReceiver(fContext, fKeyReceiverView);
		if (receiver == nil)
			break;
		RefVar cmd(MakeCommand(aeKeyDown, receiver, chars[i]));
		gApplication->DispatchCommand(cmd);
		cmd = MakeCommand(aeKeyUp, receiver, chars[i]);
		gApplication->DispatchCommand(cmd);
	}
}


// ROM 0x000fc300 HandleKeyPress__13TKeyboardViewFR15TVisKeyIteratorRC6RefVar
// The key turned into key events.  A keyboard of key codes goes the long
// way round: the code is put into the key map (KeyIn) as a press and a
// release, and the modifiers that were held down are released with it -
// which is what makes shift on the soft keyboard a *sticky* shift, on for
// exactly one key.  Shift and option are toggled rather than pressed, and
// the keyboard redraws itself when anything of that changed, so the keys
// show their new legends.
//
// The character KeyIn answers is then posted to the receiver as a key
// down and a key up, with the key code and the modifiers packed into the
// command's parameter.  With no receiver at all the root view beeps.
//
// A keyboard whose results are not key codes has nothing to translate:
// the result goes straight out as characters (PostKeypressCommands).
void
TKeyboardView::HandleKeyPress(TVisKeyIterator& /*iter*/, RefArg result)
{
	ULong modifiers = Modifiers(false);
	long ch = 0;
	if (ISINT((Ref) result) && fResultsAreKeycodes)
	{
		Boolean optionDown = KeyDown(kOptionKey, false);
		Boolean commandDown = KeyDown(kCommandKey, false);
		Boolean controlDown = KeyDown(kControlKey, false);
		Boolean shiftDown = KeyDown(kShiftKey, false);
		Boolean capsDown = KeyDown(kCapsLockKey, false);
		long code = RINT(result);
		ULong deadWas = gSoftKeyDeadState;
		Boolean changed = false;
		switch (code)
		{
		case kCommandKey:
			KeyIn(kCommandKey, !commandDown, this);
			changed = true;
			break;
		case kShiftKey:
			KeyIn(kShiftKey, !shiftDown, this);
			if (capsDown)
				KeyIn(kCapsLockKey, false, this);
			changed = true;
			break;
		case kCapsLockKey:
			KeyIn(kCapsLockKey, true, this);
			KeyIn(kCapsLockKey, false, this);
			if (shiftDown)
				KeyIn(kShiftKey, false, this);
			changed = true;
			break;
		case kOptionKey:
			KeyIn(kOptionKey, !optionDown, this);
			changed = true;
			break;
		case kControlKey:
			KeyIn(kControlKey, !controlDown, this);
			changed = true;
			break;
		default:
			ch = KeyIn((ULong) code, true, this);
			KeyIn((ULong) code, false, this);
			// the modifiers let go with the key they modified
			if (shiftDown)		{ KeyIn(kShiftKey, false, this); changed = true; }
			if (optionDown)		{ KeyIn(kOptionKey, false, this); changed = true; }
			if (commandDown)	{ KeyIn(kCommandKey, false, this); changed = true; }
			if (controlDown)	{ KeyIn(kControlKey, false, this); changed = true; }
			break;
		}
		if (gSoftKeyDeadState != deadWas || changed)
		{
			Dirty(nil);					// the legends have changed
			gRootView->Update(nil);
		}
		if (ch != 0)
		{
			TView* receiver = GetKeyReceiver(fContext, fKeyReceiverView);
			if (receiver == nil)
				gRootView->RunScript(RSSYMsysbeep, RefVar(MakeArray(0)), false, nil);
			else
			{
				// the keyboard's keySound, when its context has one
				if (fHasKeySound)
					FPlaySound(RefVar(fContext), RefVar(GetProtoVariable(RefVar(fContext), RSSYMkeysound, nil)));
				ULong parameter = (ULong) ch | ((ULong) code << 16)
								| (modifiers << 25) | 0x1000000;
				RefVar cmd(MakeCommand(aeKeyDown, receiver, (Long) parameter));
				gApplication->DispatchCommand(cmd);
				// (the view may have gone, and the caret may have moved,
				//  while the key was being handled)
				if (GetView(fContext) != nil)
				{
					receiver = GetKeyReceiver(fContext, fKeyReceiverView);
					if (receiver != nil)
					{
						cmd = MakeCommand(aeKeyUp, receiver, (Long) parameter);
						gApplication->DispatchCommand(cmd);
					}
				}
			}
		}
	}
	else
	{
		// (the keySound again)
		if (fHasKeySound)
			FPlaySound(RefVar(fContext), RefVar(GetProtoVariable(RefVar(fContext), RSSYMkeysound, nil)));
		PostKeypressCommands(result);
	}
	gRootView->Update(nil);
}


// ROM 0x000fc7ec DoKey__13TKeyboardViewFR15TVisKeyIterator
// One key done.  The view's keyPressScript is given the result first and
// may take it; otherwise it goes to HandleKeyPress.  ==> whether the key
// was a modifier, which is what tells TrackStroke to leave it drawn
// pressed.
Boolean
TKeyboardView::DoKey(TVisKeyIterator& iter)
{
	RefVar result(GetResultRef(iter));
	RefVar args(AllocateArray(RSSYMarray, 1));
	SetArraySlot(args, 0, result);
	RefVar context(fContext);
	Boolean ran = false;
	RefVar answer(RunCacheScript(kIndexKeyPressScript, args, true, &ran));
	if (ran && NOTNIL(GetProtoVariable(context, RSSYMnewt_feature, nil)))
		ran = NOTNIL(answer);		// a newer script says so by what it answers
	if (!ran)
		HandleKeyPress(iter, result);
	if (ISINT((Ref) result) && fResultsAreKeycodes)
		return IsModifierKeyCode((ULong) RINT(result));
	return false;
}


// ROM 0x000fc760 InsideView__13TKeyboardViewFR6TPoint
// A keyboard is only where its keys are: a tap in one of the gaps between
// them goes to whatever is underneath.
Boolean
TKeyboardView::InsideView(Point& pt)
{
	if (!PtInRect(pt, &viewBounds))
		return false;
	Point at;
	at.v = viewBounds.top;
	at.h = viewBounds.left;
	TVisKeyIterator iter(fKeyDefinitions, fCell, at);
	return iter.FindEnclosingKey(pt);
}


// ROM 0x000fc958 TrackStroke__13TKeyboardViewFP13TStrokePublicP15TVisKeyIterator
// The pen followed over the keyboard until it is lifted.  The key under
// it is drawn pressed, and redrawn as the pen slides from one key to the
// next, so a finger can be run along the keys and land on the right one.
// The key is done when the pen comes up - and, when it has been held
// still on one key for three fifths of a second, over and over until it
// moves or is lifted.
//
// While that is going on the view's visible region is taken over so that
// the pressed keys can be drawn without the view system's help; it is put
// back at the end.
//
// ==> whether the stroke was on a key at all; a stroke that was not is
// left for whatever is underneath.
Boolean
TKeyboardView::TrackStroke(TStrokePublic* stroke, TVisKeyIterator* /*unused*/)
{
	Point origin;
	origin.v = viewBounds.top;
	origin.h = viewBounds.left;
	TVisKeyIterator last(fKeyDefinitions, fCell, origin);
	TVisKeyIterator iter(fKeyDefinitions, fCell, origin);
	// the busy box held off while the key is tracked (and let go again
	// at the end - but not when the pen is on no key, as the ROM's)
	BusyBoxSend(0x35);
	stroke->InkOff(true);
	gRootView->Update(nil);
	if (!iter.FindEnclosingKey(stroke->FirstPoint()))
		return false;
	PenNormal();
	TTime repeatAt = GetGlobalTime() + TTime(kKeyRepeatDelay);
	Boolean noRepeat = NOTNIL(GetProto(RSSYM_norepeat));
	RgnHandle saved = nil;
	Boolean pressed = false;
	Boolean repeated = false;
	Boolean found = false;
	RefVar context(fContext);
	newton_try
	{
		for (;;)
		{
			found = iter.FindEnclosingKey(stroke->FinalPoint());
			if (found != pressed || iter.fRowIndex != last.fRowIndex
				|| iter.fKeyIndex != last.fKeyIndex)
			{
				if (saved == nil)
					saved = SetupVisRgn().StealRegion();
				StartDrawing(nil, nil);
				if (pressed && (last.fInfo & kKeyHilites) != 0)
					DrawKey(last, false, true);
				if (found && (iter.fInfo & kKeyHilites) != 0)
					DrawKey(iter, true, true);
				StopDrawing(nil, nil);
				pressed = found;
				repeatAt = GetGlobalTime() + TTime(kKeyRepeatDelay);
				repeated = false;
				iter.CopyInto(&last);
			}
			else
				Wait(1);
			if (GetView(context) == nil)
				break;					// the keyboard has gone
			if (stroke->Done() || noRepeat)
				break;
			if (!found)
				continue;
			if (GetGlobalTime() > repeatAt)
			{
				// held still on a key: it repeats
				if (saved != nil)
				{
					GrafPort* port;
					GetPort(&port);
					CopyRgn(saved, port->visRgn);
					DisposeCachedRgn(saved);
					saved = nil;
				}
				noRepeat = DoKey(iter);
				repeatAt = GetGlobalTime() + TTime(kKeyRepeatInterval);
				repeated = true;
			}
		}
		if (GetView(context) != nil && found)
		{
			StartDrawing(nil, nil);
			if ((last.fInfo & kKeyHilites) != 0)
			{
				if (saved == nil)
					saved = SetupVisRgn().StealRegion();
				DrawKey(last, false, true);
			}
			if (!repeated)
			{
				if (saved != nil)
				{
					GrafPort* port;
					GetPort(&port);
					CopyRgn(saved, port->visRgn);
					DisposeCachedRgn(saved);
					saved = nil;
				}
				DoKey(last);
			}
			StopDrawing(nil, nil);
		}
	}
	newton_catch_all
	{
		BusyBoxSend(0x36);
		if (saved != nil)
		{
			GrafPort* port;
			GetPort(&port);
			CopyRgn(saved, port->visRgn);
			DisposeCachedRgn(saved);
		}
		rethrow;
	}
	end_try;
	BusyBoxSend(0x36);
	if (saved != nil)
	{
		GrafPort* port;
		GetPort(&port);
		CopyRgn(saved, port->visRgn);
		DisposeCachedRgn(saved);
	}
	return true;
}


// ROM 0x000fcf80 RealDoCommand__13TKeyboardViewFRC6RefVar
// A click on the keyboard is tracked; anything the keyboard did not take
// goes on to TView.
Boolean
TKeyboardView::RealDoCommand(RefArg cmd)
{
	if (CommandID(cmd) == aeClick)
	{
		TUnitPublic* unit = (TUnitPublic*) CommandParameter(cmd);
		if (TrackStroke(unit->Stroke(), nil))
		{
			CommandSetResult(cmd, 1);
			return true;
		}
	}
	return TView::RealDoCommand(cmd);
}
