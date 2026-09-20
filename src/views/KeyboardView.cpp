/*
	File:		views/KeyboardView.cpp

	Contains:	TKeyboardView and its iterators (KeyboardView.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "KeyboardView.h"
#include "Rects.h"
#include "Frames.h"
#include "ObjectHeap.h"


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
