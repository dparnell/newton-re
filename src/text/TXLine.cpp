/*
	File:		text/TXLine.cpp

	Contains:	One line of styled text, laid out (TXLine.h).

	Reconstructed from the MP2x00 US ROM (0x0023cba8-0x0023ded4); each
	function cites its origin.
*/

#include "TXLine.h"
#include "TXStyledText.h"
#include "TXRulerRange.h"
#include "TXRuler.h"
#include "TXChars.h"
#include "TXLinesHeights.h"
#include "TXUtilities.h"
#include "FixedMath.h"
#include "Ports.h"


// ROM 0x0023cba8 __ct__6TXLineFP12TXStyledTextP12TXRulerRange
// (The ROM leaves the other fields as the allocator had them.)
TXLine::TXLine(TXStyledText* text, TXRulerRange* rulers)
	: fRuler(nil), fLength(0), fVisibleLength(0), fInvisible(false), fLast(-1), fLeft(0), fWidth(0)
{
	fStart = -1;
	fRulers = rulers;
	fText = text;
	fRunInfos = new TXArray(sizeof(TXLineRunInfo), 0);
}


// ROM 0x0023cc0c __dt__6TXLineFv
TXLine::~TXLine()
{
	if (fRunInfos != nil)
		delete fRunInfos;
}


// ROM 0x0023d7f4 GetRunDisplayInfo__6TXLineFPCUsPC13TXLineRunInfoP20TXLineRunDisplayInfo
void
TXLine::GetRunDisplayInfo(const UniChar* text, const TXLineRunInfo* run, TXLineRunDisplayInfo* info)
{
	info->fText = text + run->fOffset;
	info->fLength = run->fLength;
	info->fWidth = run->fWidth;
	info->fJustifyExtra = run->fExtra;
}


// ROM 0x0023d8c4 RunMeasure__6TXLineFPCUsPC13TXLineRunInfo
Fixed
TXLine::RunMeasure(const UniChar* text, const TXLineRunInfo* run)
{
	TXLineRunDisplayInfo info;
	GetRunDisplayInfo(text, run, &info);
	return run->fRun->MeasureWidth(info);
}


// ROM 0x0023d81c RunCharToPixel__6TXLineFPCUsPC13TXLineRunInfol
// Where the piece's `offset`th character starts: nought and the whole
// width at its ends, else what its run says - a piece that is not text,
// or a run that moves as one, as one thing.
Fixed
TXLine::RunCharToPixel(const UniChar* text, const TXLineRunInfo* run, long offset)
{
	if (offset == 0)
		return 0;
	if (run->fLength == offset)
		return run->fWidth + run->fExtra;
	TXLineRunDisplayInfo info;
	GetRunDisplayInfo(text, run, &info);
	TXRun* r = run->fRun;
	if (run->fKind == 0 && (r->GetObjFlags() & 2) == 0)
		return r->CharToPixel(info, offset);
	return TXIndivisibleCharToPixel(info, offset);
}


// ROM 0x0023dd80 DefineRuns__6TXLineFlT1
// The pieces of [start, start + length): each run's stretch of it, cut at
// the control characters.
void
TXLine::DefineRuns(long start, long length)
{
	fRunInfos->SetCount(0);
	if (length == 0)
		return;
	gTXParagCtrlChars.Define(fText->fChars, start, start + length);
	TXObjectIterator runs(fText->fRuns, start);
	while (runs.fObject != nil)
	{
		long count = runs.fLength;
		if (length < count)
			count = length;
		InsertRun(runs.fOffset, count, (TXRun*) runs.fObject);
		length -= count;
		if (length == 0)
			break;
		runs.Next();
	}
}


// ROM 0x0023dcc8 InsertRun__6TXLineFlT1P5TXRun
// A run's stretch as pieces: up to the next control character, which is
// a piece of one character of its own (its kind the character).
void
TXLine::InsertRun(long at, long length, TXRun* run)
{
	if (length == 0)
		return;
	do
	{
		long ctrl = gTXParagCtrlChars.GetCurrCtrlOffset() - at;
		TXLineRunInfo* info = (TXLineRunInfo*) fRunInfos->Insert(nil, 1, -1);
		info->fRun = run;
		info->fOffset = at - fStart;
		info->fExtra = 0;
		if (ctrl == 0)
		{
			info->fLength = 1;
			info->fKind = (unsigned char) gTXParagCtrlChars.GetCurrCtrlChar();
			gTXParagCtrlChars.fCurrent++;
		}
		else
		{
			info->fLength = (ctrl > 0 && ctrl < length) ? ctrl : length;
			info->fKind = 0;
		}
		at += info->fLength;
		length -= info->fLength;
	} while (length != 0);
}


// ROM 0x0023dc34 CalcVisibleLength__6TXLineFPCUslPl
// The length less what does not show at the end (the spaces and the line
// end), and how many whole pieces at the end show nothing at all.
long
TXLine::CalcVisibleLength(const UniChar* text, long length, long* trailing)
{
	TXLineRunInfo* first = (TXLineRunInfo*) fRunInfos->GetElementPtr(0);
	TXLineRunInfo* info = (TXLineRunInfo*) fRunInfos->GetLastElementPtr();
	*trailing = 0;
	do
	{
		long visible = info->fRun->VisibleLen(text + info->fOffset, info->fLength);
		length -= info->fLength - visible;
		if (visible != 0)
			return length;
		(*trailing)++;
		info--;
	} while (first <= info);
	return length;
}


// ROM 0x0023de28 CalcAlignTabWidth__6TXLineFPCUsP12TXPendingTablRC13TXLineRunInfo
// A decimal tab is settled by the first piece that has its character (the
// tab's fill character): the tab's width is what is left once the text up
// to that character is placed.  ==> nought until then.
Fixed
TXLine::CalcAlignTabWidth(const UniChar* text, TXPendingTab* pending, Fixed since, const TXLineRunInfo& run)
{
	TXChars* chars = fText->fChars;
	long found = chars->SearchChar(pending->fTab.fFillChar, fStart + run.fOffset, run.fLength);
	if (found < 0)
		return 0;
	pending->fPending = false;
	Fixed width = pending->fWidth - since;
	if (found != 0)
	{
		TXLineRunInfo before = run;
		before.fLength = found;
		width -= RunMeasure(text, &before);
	}
	if (width < 0)
		width = 0;
	return width;
}


// ROM 0x0023cc58 DefineRunWidths__6TXLineFPCUslUc
// Every piece measured: text by its run, a tab by the ruler (a tab whose
// width waits on the text after it is settled when the next tab or the
// end of the line is reached, or - a decimal tab - its character), a line
// end nothing.  ==> the width it all takes.
Fixed
TXLine::DefineRunWidths(const UniChar* text, Fixed available, Boolean firstLine)
{
	if (fLast < 0)
		return 0;
	TXLineRunInfo* info = (TXLineRunInfo*) fRunInfos->GetElementPtr(0);
	Fixed total = 0;
	TXPendingTab pending;
	pending.fPending = false;
	TXLineRunInfo* pendingRun = nil;
	Fixed since = 0;
	Fixed left = fRuler->GetLineLeftBlanks(firstLine);
	long last = fLast;
	for (long i = 0; i <= last; i++, info++)
	{
		if (info->fKind == 0)
		{
			info->fWidth = RunMeasure(text, info);
			if (pending.fPending)
			{
				if (pending.fTab.fKind == kTXTabDecimalPoint)
				{
					Fixed width = CalcAlignTabWidth(text, &pending, since, *info);
					pendingRun->fWidth = width;
					total += width;
				}
				since += info->fWidth;
			}
			total += info->fWidth;
		}
		else if (info->fKind == 9)
		{
			if (pending.fPending)
			{
				Fixed width = fRuler->CalcPendingTabWidth(pending, since, available - total);
				pendingRun->fWidth = width;
				total += width;
			}
			info->fWidth = fRuler->GetTabWidth(total + left, available, &pending);
			if (pending.fPending)
			{
				since = 0;
				info->fWidth = 0;
				pendingRun = info;
			}
			total += info->fWidth;
		}
		else
			info->fWidth = 0;
	}
	if (pending.fPending)
	{
		Fixed width = fRuler->CalcPendingTabWidth(pending, since, available - total);
		pendingRun->fWidth = width;
		total += width;
	}
	return total;
}


// ROM 0x0023ce20 CalcFullJustifPortions__6TXLineFPCUsPlT2
// What each text piece after the last tab can be stretched by, from the
// line's end backwards, and how many of them there are; ==> the sum.
Fixed
TXLine::CalcFullJustifPortions(const UniChar* text, long* portions, long* count)
{
	fRunInfos->Lock(false);
	long last = fLast;
	TXLineRunInfo* info = (TXLineRunInfo*) fRunInfos->GetElementPtr(last);
	Fixed total = 0;
	*count = 0;
	for (long i = 0; i <= last; i++, info--, portions++)
	{
		if (info->fKind == 0)
		{
			TXRun* run = info->fRun;
			(*count)++;
			TXLineRunDisplayInfo display;
			GetRunDisplayInfo(text, info, &display);
			Fixed portion = run->FullJustifPortion(display);
			*portions = portion;
			total += portion;
		}
		else if (info->fKind == 9)
			break;
		else
			*portions = 0;
	}
	fRunInfos->Unlock();
	return total;
}


// ROM 0x0023cf00 DefineRunsExtraWidths__6TXLineFPCUsl
// The room left over shared out between the text pieces after the last
// tab in proportion to their portions.  A line of more than a hundred
// pieces is left as it is.
//
// ROM BUG, kept: the shares are handed to the pieces from the end
// backwards, one per *text* piece counted, but the portions include a
// nought for each line end among them - so a line end before the last
// text piece shifts every share after it one piece along.
void
TXLine::DefineRunsExtraWidths(const UniChar* text, Fixed extra)
{
	if (extra == 0)
		return;
	long last = fLast;
	if (last > 99 || last < 0)
		return;
	if (last == 0)
	{
		GetRunInfo(0)->fExtra = extra;
		return;
	}
	long portions[100];
	long count;
	Fixed total = CalcFullJustifPortions(text, portions, &count);
	if (total != 0)
	{
		TXLineRunInfo* info = GetRunInfo(fLast);
		for (long i = 0; i < count; i++, info--)
			info->fExtra = FixedMultiply(FixedDivide(portions[i], total), extra);
	}
}


// ROM 0x0023d8fc DoLineLayout__6TXLineFlN21
void
TXLine::DoLineLayout(long start, long length, long width)
{
	if (fStart == start)
		return;
	TXChars* chars = fText->fChars;
	long chunk;
	UniChar* text = chars->GetLineChars(start, length, &chunk);
	fWidth = (Fixed) ((ULong) width << 16);
	fStart = start;
	fLength = length;
	fRuler = (TXRuler*) fRulers->OffsetToObject(start, false);
	DefineRuns(start, length);
	fLast = fRunInfos->GetCount() - 1;
	fRunInfos->Lock(false);
	long trailing = 0;
	long visible = (length == 0) ? 0 : CalcVisibleLength(text, length, &trailing);
	fInvisible = (visible == 0);
	char justification;
	fRuler->GetAttributeValue(kTXAttrJustification, &justification);
	Boolean trim;
	if (justification == kTXJustifyFullAll)
	{
		if (visible != 0)
		{
			justification = kTXJustifyFull;
			trim = true;
		}
		else
		{
			justification = kTXJustifyLeft;
			trim = false;
		}
	}
	else
	{
		// a paragraph's last line is not stretched
		if (justification == kTXJustifyFull)
		{
			long end = start + length - 1;
			UniChar c;
			if (visible == 0
			 || (c = fText->fChars->GetChar(end), c == 0x0a || c == 0x0d)
			 || fText->fChars->Count() - 1 == end)
				justification = kTXJustifyLeft;
		}
		trim = (justification == kTXJustifyFull || justification == kTXJustifyRight);
	}
	if (trim)
	{
		fVisibleLength = visible;
		if (trailing != 0)
		{
			fLast -= trailing;
			fRunInfos->SetCount(fLast + 1);
		}
		if (fLast >= 0)
		{
			TXLineRunInfo* info = GetRunInfo(fLast);
			info->fLength = visible - info->fOffset;
		}
	}
	else
		fVisibleLength = length;
	UniChar before;
	Boolean firstLine = (start == 0
					  || (before = fText->fChars->GetChar(start - 1), before == 0x0a || before == 0x0d));
	fLeft = fRuler->GetLineLeftBlanks(firstLine);
	Fixed room = (fWidth - fLeft) - fRuler->GetLineRightBlanks();
	Fixed used = DefineRunWidths(text, room, firstLine);
	room -= used;
	if (justification == kTXJustifyFull)
		DefineRunsExtraWidths(text, room);
	else if (justification == kTXJustifyRight)
		fLeft = fLeft + room;
	else if (justification == kTXJustifyCenter)
		fLeft = FixedDivide(room, 0x20000) + fLeft;
	fRunInfos->Unlock();
	fText->fChars->ReleaseCharChunk(chunk);
}


// ROM 0x0023cfb8 Draw__6TXLineFRC4Recti
// Each text piece drawn by its run, from the line's left edge plus where
// the text starts; the pen is moved to each piece's start first.
void
TXLine::Draw(const Rect& line, int baseline)
{
	if (fInvisible)
		return;
	Fixed x = fLeft + (Fixed) ((ULong) line.left << 16);
	TXLineRunInfo* info = (TXLineRunInfo*) fRunInfos->Lock(false);
	TXChars* chars = fText->fChars;
	long chunk;
	UniChar* text = chars->GetLineChars(fStart, fLength, &chunk);
	long y = baseline + line.top;
	long last = fLast;
	for (long i = 0; i <= last; i++, info++)
	{
		if (info->fKind == 0)
		{
			TXRun* run = info->fRun;
			MoveTo((short) ((ULong) (x + 0x8000) >> 16), y);
			TXLineRunDisplayInfo display;
			GetRunDisplayInfo(text, info, &display);
			run->Draw(display, x, line, baseline);
		}
		x = info->fWidth + info->fExtra + x;
	}
	chars->ReleaseCharChunk(chunk);
	fRunInfos->Unlock();
}


// ROM 0x0023d0f4 CharToRun__6TXLineF8TXOffsetPl
// The piece holding the character at the offset (the one before it when
// the offset is the start of what follows), no further than the last
// visible character; failing that the last piece, and its start is not
// counted in.
long
TXLine::CharToRun(TXOffset offset, Boolean atStart, Fixed* pixel)
{
	long last = fLast;
	if (last < 0)
		return -1;
	long at = offset - fStart;
	Fixed x = fLeft;
	if (atStart && at != 0)
		at = at - 1;
	if (fVisibleLength - 1 <= at)
		at = fVisibleLength - 1;
	TXLineRunInfo* info = GetRunInfo(0);
	long i = 0;
	for ( ; i < last; i++, info++)
	{
		if (info->fOffset <= at && at < info->fOffset + info->fLength)
			break;
		x = info->fWidth + info->fExtra + x;
	}
	if (pixel != nil)
		*pixel = x;
	return i;
}


// ROM 0x0023d1c8 CharacterToPixel__6TXLineF8TXOffset
short
TXLine::CharacterToPixel(TXOffset offset, Boolean atStart)
{
	Fixed x;
	long index = CharToRun(offset, atStart, &x);
	if (index < 0)
		return (short) ((ULong) (fLeft + 0x8000) >> 16);
	fRunInfos->Lock(false);
	TXLineRunInfo* info = GetRunInfo(index);
	long lineStart = fStart;
	long runOffset = info->fOffset;
	TXChars* chars = fText->fChars;
	long chunk;
	UniChar* text = chars->GetLineChars(fStart, fLength, &chunk);
	Fixed within = RunCharToPixel(text, info, offset - (lineStart + runOffset));
	chars->ReleaseCharChunk(chunk);
	fRunInfos->Unlock();
	return (short) ((ULong) (within + x + 0x8000) >> 16);
}


// ROM 0x0023d5e0 PixelToRun__6TXLineFPl
long
TXLine::PixelToRun(Fixed* pixel)
{
	*pixel = *pixel - fLeft;
	if (*pixel < 0)
		*pixel = 0;
	TXLineRunInfo* info = GetRunInfo(0);
	Fixed width = 0;
	long last = fLast;
	for (long i = 0; i <= last; i++, info++)
	{
		width = info->fWidth + info->fExtra;
		if (*pixel <= width)
			return i;
		*pixel = *pixel - width;
	}
	*pixel = width;
	return fLast;
}


// ROM 0x0023d66c PixelToCharacter__6TXLineFlP13TXOffsetRange
// A line end answers its start; a piece that is not text, or a run that
// moves as one, is hit as one thing.
TXRun*
TXLine::PixelToCharacter(Fixed pixel, TXOffsetRange* range)
{
	if (fLast < 0)
	{
		range->Set(fStart, fStart, false, false);
		return nil;
	}
	long index = PixelToRun(&pixel);
	TXChars* chars = fText->fChars;
	long chunk = 0;
	UniChar* text = chars->GetLineChars(fStart, fLength, &chunk);
	fRunInfos->Lock(false);
	TXLineRunInfo* info = GetRunInfo(index);
	TXRun* run = info->fRun;
	TXLineRunDisplayInfo display;
	GetRunDisplayInfo(text, info, &display);
	if (info->fKind == 0 && (run->GetObjFlags() & 2) == 0)
		run->PixelToChar(display, pixel, range);
	else if (info->fKind == 0x0a || info->fKind == 0x0d)
		range->Set(0, 0, false, false);
	else
		TXIndivisiblePixelToChar(display, pixel, range);
	range->Offset(fStart + info->fOffset);
	chars->ReleaseCharChunk(chunk);
	fRunInfos->Unlock();
	return info->fRun;
}


// ROM 0x0023d2ac CalcRunHilite__6TXLineFPCUslN22PlN25Uc
// The part of piece `index` that [from, to) covers (offsets in the line):
// its left edge and width, `*x` being where the piece starts and moved on
// past it.  `wholeLine` takes a range from the line's start to the left
// edge (nought) and one to its end to the line's right edge.  ==> how
// many characters of the piece it covers.
long
TXLine::CalcRunHilite(const UniChar* text, long from, long to, long index, Fixed* left, Fixed* width, Fixed* x, Boolean wholeLine)
{
	TXLineRunInfo* info = GetRunInfo(index);
	Fixed runWidth = info->fWidth + info->fExtra;
	Fixed start = *x;
	*x = start + runWidth;
	long a = from - info->fOffset;
	long b = to - info->fOffset;
	if (b < 0 || info->fLength < a)
		return 0;
	if (a < 0)
		a = 0;
	if (info->fLength < b)
		b = info->fLength;
	if (b == a)
		return 0;
	*left = start;
	if (a == 0)
	{
		if (index == 0 && wholeLine)
			*left = 0;
	}
	else
		*left = RunCharToPixel(text, info, a) + *left;
	Fixed right;
	if (info->fLength == b)
	{
		if (fLast == index && wholeLine)
			right = fWidth;
		else
			right = start + runWidth;
	}
	else
		right = RunCharToPixel(text, info, b) + start;
	if (right < *left)
		TXSwapLong((long*) left, (long*) &right);
	*width = right - *left;
	return b - a;
}


// ROM 0x0023d424 GetLineHilite__6TXLineF13TXOffsetRangeP12TXLineHiliteUc
void
TXLine::GetLineHilite(TXOffsetRange range, TXLineHilite* hilite, Boolean wholeLine)
{
	if (range.fEnd.fOffset == range.fStart.fOffset)
	{
		// a caret: a pixel, no further right than the line's last
		long x = CharacterToPixel(range.fStart.fOffset, range.fStart.fAtStart);
		long limit = (fWidth >> 16) - 1;
		if (x < limit)
			limit = x;
		if (limit < 1)
			limit = 0;
		hilite->fLeft = (Fixed) ((ULong) limit << 16);
		hilite->fWidth = 0x10000;
		return;
	}
	range.Offset(-fStart);
	if (range.fStart.fOffset < 0)
		range.fStart.fOffset = 0;
	if (fVisibleLength < range.fEnd.fOffset)
		range.fEnd.fOffset = fVisibleLength;
	long remaining = range.fEnd.fOffset - range.fStart.fOffset;
	Fixed x = fLeft;
	TXChars* chars = fText->fChars;
	long chunk;
	UniChar* text = chars->GetLineChars(fStart, fLength, &chunk);
	fRunInfos->Lock(false);
	hilite->fWidth = 0;
	long last = fLast;
	for (long i = 0; i <= last && remaining != 0; i++)
	{
		Fixed left, width;
		long covered = CalcRunHilite(text, range.fStart.fOffset, range.fEnd.fOffset, i, &left, &width, &x, wholeLine);
		if (covered != 0)
		{
			if (hilite->fWidth == 0)
				hilite->fLeft = left;
			remaining -= covered;
			hilite->fWidth += width;
		}
	}
	chars->ReleaseCharChunk(chunk);
	fRunInfos->Unlock();
}
