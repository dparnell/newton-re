/*
	File:		text/TXRulerRange.cpp

	Contains:	Which paragraph is laid out against which ruler - see
				TXRulerRange.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "TXRulerRange.h"


static inline Boolean
IsLineBreak(UniChar c)
{
	return c == 0x0a || c == 0x0d;
}


// ROM 0x00242b7c TXGetParagStartOffset__FP7TXCharsl
long
TXGetParagStartOffset(TXChars* chars, long at)
{
	long limit = at > 0x7fff ? 0x7fff : at;
	long back = chars->SearchCharBack(0x0c, at, limit);
	return back > 0 ? back - 1 : limit;
}


// ROM 0x00242bc8 TXGetParagEndOffset__FP7TXCharsl
long
TXGetParagEndOffset(TXChars* chars, long at)
{
	long limit = chars->Count() - at;
	if (limit > 0x7fff)
		limit = 0x7fff;
	long on = chars->SearchChar(0x0c, at, limit);
	return on >= 0 ? on + 1 : limit;
}


// ROM 0x00242c68 __ct__12TXRulerRangeFP7TXCharsP7TXRuler
TXRulerRange::TXRulerRange(TXChars* chars, TXRuler* defaultRuler)
	: TXObjectRange(1)
{
	fDefaultRuler = defaultRuler;
	fChars = chars;
	fPendingInvalid = true;
}


// ROM 0x00242cc4 __dt__12TXRulerRangeFv
// The pending ruler given back.
TXRulerRange::~TXRulerRange()
{
	fDefaultRuler->Free();
}


// ROM 0x00242d14 CharRangeToParagRange__12TXRulerRangeCFP8TXOffsetT1
void
TXRulerRange::CharRangeToParagRange(TXOffsetPos* start, TXOffsetPos* end) const
{
	start->fOffset -= TXGetParagStartOffset(fChars, start->fOffset);
	if (!end->fAtStart || !IsLineBreak(fChars->GetChar(end->fOffset - 1)))
		end->fOffset += TXGetParagEndOffset(fChars, end->fOffset);
	start->fAtStart = false;
	end->fAtStart = true;
}


// ROM 0x00242dac GetReplaceExtraChars__12TXRulerRangeFlT1PP12TXAttrObject
// When the stretch crosses from one ruler's paragraphs into another's,
// the rest of the paragraph it ends in goes with it.
long
TXRulerRange::GetReplaceExtraChars(TXOffset start, TXOffset end, TXAttrObject** pending)
{
	long length = end - start;
	*pending = GetPendingRuler(start, length);
	if (length != 0 && fCount > 1)
	{
		TXSectRanges sect;
		SectRanges(start, length, &sect);
		if (sect.fEndRemainder == 0)
			sect.fLastIndex++;
		if (sect.fLastIndex == sect.fFirstIndex)
			return 0;
		return TXGetParagEndOffset(fChars, end);
	}
	return 0;
}


// ROM 0x00242e48 FreeData__12TXRulerRangeFUc
// The pending ruler made a copy of the first paragraph's before the
// ranges go.
NewtonErr
TXRulerRange::FreeData(Boolean compact)
{
	if (fCount != 0)
		fDefaultRuler->Assign(TXObjectRange::OffsetToObject(0, false));	// (the base's, not ours)
	return TXObjectRange::FreeData(compact);
}


// ROM 0x00242eac GetPendingRuler__12TXRulerRangeFlT1
// A place with nothing selected in an empty text, or at the very end of
// a text that ends in a line break, is in the paragraph not yet typed.
// The first time it is asked for since it was invalidated, the pending
// ruler becomes a fresh default one (empty text) or a copy of the ruler
// before it.
TXRuler*
TXRulerRange::GetPendingRuler(TXOffset at, long length)
{
	if (length != 0)
		return nil;
	long count = fChars->Count();
	if (count != 0)
	{
		if (at != count || !IsLineBreak(fChars->GetChar(at - 1)))
			return nil;
	}
	if (fPendingInvalid)
	{
		fPendingInvalid = false;
		if (count == 0)
		{
			TXRuler* old = fDefaultRuler;
			fDefaultRuler = (TXRuler*) old->CreateNew();
			old->Free();
		}
		else
			fDefaultRuler->Assign(TXObjectRange::OffsetToObject(at, false));
	}
	return fDefaultRuler;
}


// ROM 0x00242fac InvalidatePendingRuler__12TXRulerRangeFlT1
// An edit that reaches the end of the text leaves the pending ruler to
// be worked out again - unless the end is now the paragraph not yet
// typed (asking brings it up to date there and then).
void
TXRulerRange::InvalidatePendingRuler(TXOffset at, long length)
{
	if (GetLastRangeEnd() == at + length)
	{
		if (GetPendingRuler(at + length, 0) == nil)
			fPendingInvalid = true;
	}
}


// ROM 0x00242fec NukePendingRuler__12TXRulerRangeFv
void
TXRulerRange::NukePendingRuler(void)
{
	fPendingInvalid = true;
}


// ROM 0x00242ff8 OffsetToObject__12TXRulerRangeF8TXOffset
TXAttrObject*
TXRulerRange::OffsetToObject(TXOffset offset, Boolean atStart)
{
	TXRuler* pending = GetPendingRuler(offset, 0);
	if (pending != nil)
		return pending;
	return TXObjectRange::OffsetToObject(offset, atStart);
}


// ROM 0x0024302c UpdateRangeObjects__12TXRulerRangeFlT1PC12TXAttrValuesT1
// A change to the paragraph not yet typed goes to the pending ruler and
// nowhere else.
unsigned long
TXRulerRange::UpdateRangeObjects(TXOffset at, long length, const TXAttrValues* values, long how)
{
	TXRuler* pending = GetPendingRuler(at, length);
	if (pending != nil)
	{
		pending->Update(values, how);
		return 0;
	}
	InvalidatePendingRuler(at, length);
	return TXObjectRange::UpdateRangeObjects(at, length, values, how);
}


// ROM 0x0024309c ValidateRuler__12TXRulerRangeFl
// A range that does not start just after a line break is run into the
// range before it (the ruler of a paragraph is the one its first
// character points at).
Boolean
TXRulerRange::ValidateRuler(long index)
{
	Boolean valid = true;
	if (index != 0)
	{
		TXOffset start = GetRangeStart(index);
		if (!IsLineBreak(fChars->GetChar(start - 1)))
		{
			valid = false;
			long length = GetRangeLen(index);
			ReplaceRange(start - 1, length + 1, length + 1, nil, true);
		}
	}
	return valid;
}


// ROM 0x00243138 ValidateRulerRange__12TXRulerRangeFlT1
// The ranges the two ends of an edited stretch fall in validated;
// ==> how much of the paragraph after the stretch was run into it (0
// when nothing was).
//
// ROM QUIRK kept: when the first range had to be merged it calls itself
// again over the same stretch and throws the answer away, then goes on
// to the last range with the indices worked out before the merge.
long
TXRulerRange::ValidateRulerRange(TXOffset at, long length)
{
	long count = fCount;
	if (count <= 1)
		return 0;
	TXSectRanges sect;
	SectRanges(at, length, &sect);
	long extra = 0;
	if (sect.fEndRemainder == 0)
	{
		sect.fLastIndex++;
		if (sect.fLastIndex < count)
			extra = GetRangeLen(sect.fLastIndex);
	}
	else
		extra = GetRangeLen(sect.fLastIndex) - sect.fLastLen;
	if (!ValidateRuler(sect.fFirstIndex))
	{
		if (sect.fLastIndex == sect.fFirstIndex)
			return 0;
		(void) ValidateRulerRange(at, length);
	}
	if (sect.fLastIndex == sect.fFirstIndex || fCount <= sect.fLastIndex || ValidateRuler(sect.fLastIndex))
		extra = 0;
	return extra;
}
