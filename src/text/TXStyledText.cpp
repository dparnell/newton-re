/*
	File:		text/TXStyledText.cpp

	Contains:	Styled text (TXStyledText.h).

	Reconstructed from the MP2x00 US ROM (0x002461bc-0x0024659c); each
	function cites its origin.
*/

#include "TXStyledText.h"
#include "TXChars.h"
#include "Frames.h"
#include "RSSymbols.h"
#include "Ports.h"
#include "Text.h"
#include "Locale.h"
#include "Unicode.h"


// ROM 0x002461bc __ct__12TXStyledTextFv
// (The ROM leaves the fields as the allocator had them; IStyledText sets
// them.)
TXStyledText::TXStyledText()
	: fPort(nil), fChars(nil), fRuns(nil)
{ }


// ROM 0x00246224 __dt__12TXStyledTextFv
TXStyledText::~TXStyledText()
{
	if (fRuns != nil)
		delete fRuns;
	if (fChars != nil)
		delete fChars;
}


// ROM 0x002461f0 IStyledText__12TXStyledTextFP8GrafPortP7TXCharsc
void
TXStyledText::IStyledText(GrafPort* port, TXChars* chars, char kind)
{
	TXRunRange* runs = new TXRunRange(kind);
	fPort = port;
	fChars = chars;
	fRuns = runs;
}


// ROM 0x00246288 SetTextPort__12TXStyledTextFP8GrafPort
void
TXStyledText::SetTextPort(GrafPort* port)
{
	fPort = port;
}


// ROM 0x00246290 GetTextPort__12TXStyledTextCFv
GrafPort*
TXStyledText::GetTextPort(void) const
{
	if (fPort != nil)
		return fPort;
	GrafPtr port;
	GetPort(&port);
	return port;
}


// ROM 0x002462bc IsWordSpace__12TXStyledTextCFUs
Boolean
TXStyledText::IsWordSpace(UniChar c) const
{
	return IsSpace(c) || IsTab(c);
}


// ROM 0x00246304 CharToWord__12TXStyledTextF8TXOffsetP13TXOffsetRangec
// The word round the offset, found by FindWordBreaks over the characters
// 64 either side of it (forward from the offset, or back from it when it
// is the start of what follows).  Unless the flags say otherwise, a word
// takes the spaces after it, and spaces are taken with the word before
// them.  The range starts after its first character and ends before its
// last (atStart false, then true).
//
// (The ROM releases the chunk even when GetLineChars answered none, with
// whatever the chunk word held; the host's starts at nought.)
Boolean
TXStyledText::CharToWord(TXOffset offset, Boolean atStart, TXOffsetRange* range, char flags)
{
	long end = fChars->Count();
	long start = offset - 0x40;
	if (start < 0)
		start = 0;
	if (offset + 0x40 <= end)
		end = offset + 0x40;
	ULong length = end - start;
	RefVar table(GetLocaleSlot((flags & kTXWordLineBreaks) ? RSSYMlinebreaktable : RSSYMwordbreaktable));
	long chunk = 0;
	UniChar* chars = fChars->GetLineChars(start, length, &chunk);
	Boolean found = false;
	if (chars != nil)
	{
		ULong wordStart, wordEnd;
		FindWordBreaks(chars, length, offset - start, !atStart, table, &wordStart, &wordEnd);
		if (wordStart != wordEnd)
		{
			if ((flags & kTXWordNoSpaces) == 0)
			{
				if (!IsWordSpace(chars[wordStart]))
				{
					while (wordEnd < length && IsWordSpace(chars[wordEnd]))
						wordEnd++;
				}
				else if (wordStart != 0)
				{
					ULong unused;
					FindWordBreaks(chars, length, wordStart, false, table, &wordStart, &unused);
				}
			}
			range->Set(wordStart + start, wordEnd + start, false, true);
			found = true;
		}
	}
	fChars->ReleaseCharChunk(chunk);
	return found;
}


// ROM 0x002464f4 AdvanceOffset__12TXStyledTextFlUc
// One character, or all of a run whose object says it moves as one (its
// flags' bit 2: a graphics run).
long
TXStyledText::AdvanceOffset(long offset, Boolean forward)
{
	if (!forward)
	{
		offset = offset - 1;
		if (offset < 0)
			return 0;
	}
	else if (offset >= fChars->Count())
		return 0;
	long index = fRuns->OffsetToRangeIndex(offset, !forward);
	TXAttrObject* object = fRuns->RangeIndexToObject(index);
	if ((object->GetObjFlags() & 2) == 0)
		return 1;
	return fRuns->GetRangeLen(index);
}
