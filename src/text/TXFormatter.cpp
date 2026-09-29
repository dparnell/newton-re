/*
	File:		text/TXFormatter.cpp

	Contains:	The formatter: styled text broken into lines (TXFormatter.h).

	Reconstructed from the MP2x00 US ROM (0x00237540-0x002390d4); each
	function cites its origin.  BreakLine, BreakRun, BreakCtrlChar,
	BreakAlignTabChars, FormatRange and ReplaceRange were read from the
	assembly: the decompiler loses the arguments of their virtual calls.
*/

#include "TXFormatter.h"
#include "TXStyledText.h"
#include "TXRulerRange.h"
#include "TXRuler.h"
#include "TXChars.h"
#include "TXStream.h"
#include "ByteOrder.h"
#include "OSErrors.h"


// ROM 0x00237540 __ct__11TXFormatterFv
// (The ROM leaves the rest as the allocator had them; SetHandlers sets
// them.)
TXFormatter::TXFormatter()
	: fText(nil), fRulers(nil), fFrames(nil), fFrameFormatter(nil), fLineEnds(nil), fRuns(nil),
	  fLineWidth(0), fSincePending(0), fLeft(0)
{
	fSuspended = 0;
	fNoWrap = false;
	fRuler = nil;
	fLastLine = -1;
	memset(&fPending, 0, sizeof(fPending));
}


// ROM 0x0023758c __dt__11TXFormatterFv
TXFormatter::~TXFormatter()
{
	if (fLineEnds != nil)
		delete fLineEnds;
}


// ROM 0x002376c8 IsLineFeed__11TXFormatterCFl
Boolean
TXFormatter::IsLineFeed(TXOffset offset) const
{
	UniChar c = fText->fChars->GetChar(offset);
	return c == 0x0a || c == 0x0d;
}


// ROM 0x002375d8 RemoveLines__11TXFormatterFlT1P19TXFormatReflowLines
void
TXFormatter::RemoveLines(long line, long count, TXFormatReflowLines* reflow)
{
	if (count <= 0)
		return;
	fLineEnds->Remove(line, count);
	fFrameFormatter->RemoveLines(count, line, reflow);
	fLastLine = fLastLine - count;
}


// ROM 0x00237640 RemoveFormattedLines__11TXFormatterFlP19TXFormatReflowLines
TXOffset
TXFormatter::RemoveFormattedLines(long line, TXFormatReflowLines* reflow)
{
	TXOffset end = fLineEnds->GetRangeEnd(line);
	long count = 0;
	TXOffset last = 0;
	long next = line + 1;
	for (long i = next; i <= fLastLine; i++)
	{
		TXOffset e = fLineEnds->GetRangeEnd(i);
		if (e > end)
			break;
		count++;
		last = e;
	}
	if (count == 0)
		return -1;
	RemoveLines(next, count, reflow);
	return last;
}


// ROM 0x0023770c BreakVisibleChars__11TXFormatterFPCUslN22PlP5TXRun
// How many of `count` characters from `at` the run fits into `*width`
// (TXRun::LineBreak, over the text from the line's start); `*width`
// comes back as what is left, or nought when the line broke.  A text run
// may always cut the line's only word; a picture may go on only at the
// start of a line.
long
TXFormatter::BreakVisibleChars(const UniChar* text, TXOffset lineStart, TXOffset at, long count, Fixed* width, TXRun* run)
{
	long into = at - lineStart;
	Boolean mayCut;
	if (run->IsTextRun())
	{
		mayCut = true;
		count += into;
	}
	else
		mayCut = (into == 0);
	long length;
	if (run->LineBreak(text, count, into, width, mayCut, &length) != 2)
		*width = 0;
	return length;
}


// ROM 0x00237798 BreakCtrlChar__11TXFormatterFlT1Pl
// The control character at `at`: anything but a tab simply goes on the
// line.  A tab settles the tab before it that was waiting (the ruler's
// CalcPendingTabWidth) and takes the ruler's width - at once for a left
// tab, nothing yet for one waiting on what follows (which fails when
// even its stop is past the room left).  ==> whether it went on the
// line; a left tab past the end of the line goes on only when it starts
// the line.
Boolean
TXFormatter::BreakCtrlChar(TXOffset lineStart, TXOffset at, Fixed* width)
{
	if (gTXParagCtrlChars.GetCurrCtrlChar() != 9)
		return true;
	if (*width <= 0)
		return false;
	if (fPending.fPending)
		*width = *width - fRuler->CalcPendingTabWidth(fPending, fSincePending, *width);
	long tab = fRuler->GetTabWidth((fLineWidth - *width) + fLeft, fLineWidth + fLeft, &fPending);
	if (fPending.fPending)
	{
		if (tab > *width)
		{
			*width = 0;
			return false;
		}
		fSincePending = 0;
		return true;
	}
	*width = *width - tab;
	if (*width < 0)
		return at == lineStart;
	return true;
}


// ROM 0x00237894 BreakAlignTabChars__11TXFormatterFPCUslN22PlP5TXRun
// After a decimal tab: the characters up to its alignment character (the
// tab's fill character) placed, and the tab settled.  ==> how many were
// placed; nought, and the tab left waiting, when the character is not
// among these.
long
TXFormatter::BreakAlignTabChars(const UniChar* text, TXOffset lineStart, TXOffset at, long count, Fixed* width, TXRun* run)
{
	long found = fText->fChars->SearchChar(fPending.fTab.fFillChar, at, count);
	if (found < 0)
		return 0;
	long placed;
	if (found == 0)
		placed = 0;
	else
	{
		Fixed before = *width;
		placed = BreakVisibleChars(text, lineStart, at, found, width, run);
		fSincePending = fSincePending + (before - *width);
	}
	*width = *width - fRuler->CalcPendingTabWidth(fPending, fSincePending, *width);
	fPending.fPending = false;
	return placed;
}


// ROM 0x00237970 BreakRun__11TXFormatterFPCUslN22PlP5TXRun
// `count` characters of one run from `at` put on the line: up to each
// control character by BreakVisibleChars (or, after a decimal tab,
// BreakAlignTabChars first), the control character by BreakCtrlChar,
// until they are all on or the room runs out.  ==> how many went on.
long
TXFormatter::BreakRun(const UniChar* text, TXOffset lineStart, TXOffset at, long count, Fixed* width, TXRun* run)
{
	long left = count;
	while (left != 0)
	{
		long placed;
		long ctrl = gTXParagCtrlChars.GetCurrCtrlOffset() - at;
		if (ctrl == 0)
		{
			placed = BreakCtrlChar(lineStart, at, width) ? 1 : 0;
			if (placed != 0)
				gTXParagCtrlChars.fCurrent++;
			else if (*width <= 0)
				break;
		}
		else
		{
			Fixed before = *width;
			if (before <= 0)
				break;
			long n = (ctrl > 0 && left > ctrl) ? ctrl : left;
			if (fPending.fPending && fPending.fTab.fKind == kTXTabDecimalPoint)
			{
				placed = BreakAlignTabChars(text, lineStart, at, n, width, run);
				long rest = n - placed;
				if (rest > 0 && *width > 0)
					placed = BreakVisibleChars(text, lineStart, at + placed, rest, width, run) + placed;
			}
			else
				placed = BreakVisibleChars(text, lineStart, at, n, width, run);
			if (fPending.fPending)
				fSincePending = fSincePending + (before - *width);
		}
		at += placed;
		left -= placed;
	}
	return count - left;
}


// ROM 0x00237b28 CalcRunsHeight__11TXFormatterFlT1P14TXRunsIteratorP16TXLineHeightInfo
// The tallest ascent, descent and leading of the runs over `count`
// characters from `start`.
void
TXFormatter::CalcRunsHeight(TXOffset start, long count, TXRunsIterator* runs, TXLineHeightInfo* info)
{
	int maxAscent = 0, maxDescent = 0, maxLeading = 0;
	long inRun = (runs->fOffset + runs->fLength) - start;
	if (inRun < 1)
	{
		runs->Next();
		inRun = runs->fLength;
	}
	TXRun* run = (TXRun*) runs->fObject;
	while (run != nil)
	{
		int ascent, descent, leading;
		run->GetHeightInfo(&ascent, &descent, &leading);
		if (maxAscent < ascent)
			maxAscent = ascent;
		if (maxDescent < descent)
			maxDescent = descent;
		if (maxLeading < leading)
			maxLeading = leading;
		count -= inRun;
		if (count < 1)
			break;
		runs->Next();
		inRun = runs->fLength;
		run = (TXRun*) runs->fObject;
	}
	info->fHeight = maxAscent + maxDescent + maxLeading;
	info->fAscent = maxAscent;
}


// ROM 0x00237bf8 BreakLine__11TXFormatterFlT1P14TXRunsIteratorP10TXLineInfo
// Starting a paragraph, its control characters are found and its ruler
// taken.  With no wrapping the line is the paragraph.  Otherwise the runs
// are put on the line one after another (BreakRun) until the room is
// gone, the paragraph ends, or the line has 128 characters; the line's
// height is the tallest of the runs that went on it.
//
// ROM QUIRK, kept: the no-wrap line is measured from where the previous
// paragraph's control characters ended, not from `start`.
void
TXFormatter::BreakLine(TXOffset start, long width, TXRunsIterator* runs, TXLineInfo* line)
{
	if (width <= 0)
		width = 0x7fff;
	TXOffset paraEnd = gTXParagCtrlChars.fEnd;
	TXOffset from = paraEnd;
	if (start >= paraEnd)
	{
		gTXParagCtrlChars.Define(fText->fChars, start, fText->fChars->Count());
		paraEnd = gTXParagCtrlChars.fEnd;
		fRuler = (TXRuler*) fRulers->OffsetToObject(start, false);
	}
	if (fNoWrap)
	{
		line->fEnd = paraEnd;
		CalcRunsHeight(from, paraEnd - from, runs, &line->fHeight);
		return;
	}
	Boolean firstLine = (start == 0 || IsLineFeed(start - 1));
	Fixed left = fRuler->GetLineLeftBlanks(firstLine);
	Fixed right = fRuler->GetLineRightBlanks();
	fLineWidth = (Fixed) ((ULong) width << 16) - left - right;
	fLeft = left;
	long maxAscent = 0;
	long maxDescent = 0, maxLeading = 0, total = 0;
	fPending.fPending = false;
	long inRun = (runs->fOffset + runs->fLength) - start;
	if (inRun <= 0)
	{
		runs->Next();
		inRun = runs->fLength;
	}
	long chunk;
	UniChar* text = fText->fChars->GetLineChars(start, paraEnd - start, &chunk);
	long room = 0x80;
	Fixed widthLeft = fLineWidth;
	TXOffset at = start;
	TXRun* run = (TXRun*) runs->fObject;
	while (run != nil)
	{
		long n = paraEnd - at;
		if (inRun <= n)
			n = inRun;
		if (n > room)
			n = room;
		long placed = BreakRun(text, start, at, n, &widthLeft, run);
		if (placed == 0)
			break;
		total += placed;
		if (placed < 0)
		{
			runs->SetOffset(start);
			CalcRunsHeight(start, total, runs, &line->fHeight);
			maxAscent = -1;
			break;
		}
		int ascent, descent, leading;
		run->GetHeightInfo(&ascent, &descent, &leading);
		if (ascent > maxAscent)
			maxAscent = ascent;
		if (descent > maxDescent)
			maxDescent = descent;
		if (leading > maxLeading)
			maxLeading = leading;
		at += placed;
		room -= placed;
		if (widthLeft <= 0 || at == paraEnd || room == 0)
			break;
		runs->Next();
		inRun = runs->fLength;
		run = (TXRun*) runs->fObject;
	}
	fText->fChars->ReleaseCharChunk(chunk);
	if (maxAscent >= 0)
	{
		line->fHeight.fHeight = maxAscent + maxDescent + maxLeading;
		line->fHeight.fAscent = maxAscent;
	}
	line->fEnd = start + total;
}


// ROM 0x00237f78 AppendEmptyLine__11TXFormatterFv
// After a text ending in a line break, an empty last line - as tall as
// the text run at the end, or 12 with an ascent of 9.  ==> whether one
// was added.
Boolean
TXFormatter::AppendEmptyLine(void)
{
	TXLineInfo line;
	line.fEnd = -1;
	line.fHeight.fHeight = -1;
	line.fHeight.fAscent = 0;
	if (fLastLine < 0)
		line.fEnd = 0;
	else
	{
		if (fLineEnds->GetRangeLen(fLastLine) != 0)
		{
			line.fEnd = fLineEnds->GetRangeEnd(fLastLine);
			if (!IsLineFeed(line.fEnd - 1))
				return false;
			TXRun* run = fRuns->CharToTextRun(fText->fChars->Count(), false);
			if (run != nil)
			{
				int ascent, descent, leading;
				run->GetHeightInfo(&ascent, &descent, &leading);
				line.fHeight.fAscent = ascent;
				line.fHeight.fHeight = ascent + descent + leading;
			}
		}
		if (line.fEnd < 0)
			return false;
	}
	TXFormattingInfo info = { -1, 0, 0 };
	if (line.fHeight.fHeight < 0)
	{
		line.fHeight.fHeight = 12;
		line.fHeight.fAscent = 9;
	}
	return InsertLine(&line, &info) == noErr;
}


// ROM 0x002380b0 SetHandlers__11TXFormatterFP12TXStyledTextP8TXFramesP12TXRulerRangec
void
TXFormatter::SetHandlers(TXStyledText* text, TXFrames* frames, TXRulerRange* rulers, char kind)
{
	TXRanges* ends = new TXRanges(sizeof(TXOffset), (kind == 2) ? 0x14 : 5);
	fLineEnds = ends;
	fText = text;
	fRuns = text->fRuns;
	fFrames = frames;
	fRulers = rulers;
	fFrameFormatter = frames->fFormatter;
	fFrameFormatter->fLineEnds = ends;
	AppendEmptyLine();
}


// ROM 0x0023811c FormatAll__11TXFormatterFv
NewtonErr
TXFormatter::FormatAll(void)
{
	TXFormatReflowLines reflow;
	reflow.Reset();
	fFrameFormatter->RemoveLines(fLastLine + 1, 0, &reflow);
	FreeData(false);
	TXLineInfo line;
	line.fEnd = 0;
	line.fHeight.fHeight = 0;
	line.fHeight.fAscent = 0;
	long count = fText->fChars->Count();
	TXFormattingInfo info = { 0, 0, 0 };
	TXRunsIterator runs(fRuns, 0);
	NewtonErr err;
	for ( ; ; )
	{
		if (line.fEnd == count)
		{
			AppendEmptyLine();
			Compact();
			return noErr;
		}
		long width = fFrames->GetLineFormatWidth(info.fLine);
		BreakLine(line.fEnd, width, &runs, &line);
		err = InsertLine(&line, &info);
		if (err != noErr)
			break;
		info.fLine++;
	}
	if (fLastLine >= 0)
		fLineEnds->SetRangeEnd(fLastLine, count);
	return err;
}


// ROM 0x0023825c CheckRulerSettings__11TXFormatterFv
NewtonErr
TXFormatter::CheckRulerSettings(void)
{
	long count = fText->fChars->Count();
	TXOffset at = 0;
	long width = fFrames->GetLineFormatWidth(0);
	fRulers->NukePendingRuler();
	while (at < count)
	{
		long length;
		TXAttrObject* ruler = fRulers->GetNextObjectRange(at, &length);
		long indent, leftMargin, rightMargin;
		TXTabsArray* tabs;
		ruler->GetAttributeValue(kTXAttrIndent, &indent);
		ruler->GetAttributeValue(kTXAttrLeftMargin, &leftMargin);
		ruler->GetAttributeValue(kTXAttrRightMargin, &rightMargin);
		ruler->GetAttributeValue(kTXAttrTabs, &tabs);
		if ((width - leftMargin) - rightMargin < 50)
		{
			rightMargin = 0;
			ruler->SetAttributeValue(kTXAttrRightMargin, &rightMargin);
		}
		long oldLeft = leftMargin;
		Boolean tooWide = (width - leftMargin) - rightMargin < 50;
		if (tooWide)
		{
			leftMargin = 0;
			ruler->SetAttributeValue(kTXAttrLeftMargin, &leftMargin);
		}
		if ((width - indent) - rightMargin < 50 || (tooWide && oldLeft == indent))
		{
			indent = 0;
			ruler->SetAttributeValue(kTXAttrIndent, &indent);
		}
		if (tabs != nil)
		{
			for (long i = tabs->GetCount() - 1; i >= 0; i--)
			{
				TXTab tab = tabs->GetIndTab(i);
				if (width < tab.fPosition)
					tabs->RemoveTab(i);
			}
			if (tabs->GetCount() == 0)
			{
				tabs = nil;
				ruler->SetAttributeValue(kTXAttrTabs, &tabs);
			}
		}
		at += length;
	}
	return noErr;
}


// ROM 0x0023849c Format__11TXFormatterFlT1PlT3
NewtonErr
TXFormatter::Format(TXOffset start, TXOffset end, long* first, long* last)
{
	long unused;
	if (first == nil)
	{
		last = &unused;
		first = &unused;
	}
	if (fSuspended < 0)
	{
		if (fLastLine < 0)
			AppendEmptyLine();
		*last = -1;
		*first = -1;
		return noErr;
	}
	long count = fText->fChars->Count();
	if (end < 0)
		end = count;
	gTXParagCtrlChars.Invalid();
	fRuler = nil;
	if (start == 0 && end == count)
	{
		NewtonErr err = FormatAll();
		*first = 0;
		*last = fLastLine;
		return err;
	}
	return FormatRange(start, end, first, last);
}


// ROM 0x00238574 FormatRange__11TXFormatterFlT1PlT3
// From the line `start` is in - or the one before, when that one does not
// end a paragraph - each line broken again: a line that now ends no
// further than the line before it used to is a new one, inserted; the
// others are set again (and any lines they now swallow removed).  It
// stops at the text's end, or at a line that ends where it used to, past
// `end`.
NewtonErr
TXFormatter::FormatRange(TXOffset start, TXOffset end, long* first, long* last)
{
	long count = fText->fChars->Count();
	TXFormattingInfo info;
	info.fLine = fLineEnds->OffsetToRangeIndex(start, false);
	info.fEnd = end;
	if (start >= count && fLineEnds->GetRangeLen(info.fLine) == 0)
	{
		*last = *first = fLastLine;
		return noErr;
	}
	Boolean joined;
	if (info.fLine != 0 && !IsLineFeed(fLineEnds->GetRangeEnd(info.fLine - 1) - 1))
	{
		info.fLine--;
		joined = true;
	}
	else
		joined = false;
	*first = -1;
	info.fOldEnd = fLineEnds->GetRangeStart(info.fLine);
	TXLineInfo line;
	line.fEnd = info.fOldEnd;
	line.fHeight.fHeight = 0;
	line.fHeight.fAscent = 0;
	info.fLine--;
	NewtonErr err = noErr;
	TXRunsIterator runs(fRuns, line.fEnd);
	Boolean done;
	do
	{
		info.fLine++;
		long width = fFrames->GetLineFormatWidth(info.fLine);
		BreakLine(line.fEnd, width, &runs, &line);
		if (fLastLine < info.fLine || info.fOldEnd >= line.fEnd)
		{
			err = InsertLine(&line, &info);
			if (err != noErr)
			{
				fLineEnds->SetRangeEnd(fLastLine, count);
				break;
			}
		}
		else
		{
			Boolean same = joined && fLineEnds->GetRangeEnd(info.fLine) == line.fEnd;
			if (!same)
				SetLineInfo(&line, &info, true);
			if (*first < 0)
			{
				*first = info.fLine;
				if (same)
					*first = info.fLine + 1;
			}
			else if (info.fLine < *first)
				*first = info.fLine;
		}
		done = line.fEnd == count
			|| (line.fEnd == info.fOldEnd && info.fEnd <= line.fEnd && !joined);
		joined = false;
	} while (!done);
	Boolean atLast = fLastLine == info.fLine;
	if (AppendEmptyLine() && atLast)
		info.fLine++;
	*last = info.fLine;
	return err;
}


// ROM 0x00238844 ReplaceRange__11TXFormatterFlN21UlPlT5
// The line ends after the edit moved by what it added or took away; when
// text went, the lines wholly inside it removed; then the lines round it
// formatted again.
NewtonErr
TXFormatter::ReplaceRange(TXOffset start, long oldLength, long newLength, unsigned long flags, long* first, long* last)
{
	fFrameFormatter->CharRangeChanged(fText->fChars, start, oldLength, newLength, flags);
	long delta = newLength - oldLength;
	TXOffset end = start + newLength;
	if (oldLength != 0 && fText->fChars->Count() == end && fLineEnds->GetRangeLen(fLastLine) == 0)
	{
		// what was deleted ran to the end: the empty last line goes
		TXFormatReflowLines reflow;
		reflow.Reset();
		RemoveLines(fLastLine, 1, &reflow);
	}
	if (delta > 0)
	{
		long line = fLineEnds->OffsetToRangeIndex(start, false);
		fLineEnds->GetRangeStart(line);
		fLineEnds->AddToElements(line, delta, -1);
		return Format(start, end, first, last);
	}
	TXSectRanges sect;
	if (fLineEnds->SectRanges(start, oldLength, &sect) > 1)
	{
		if (sect.fStartOffset != 0)
			fLineEnds->SetRangeEnd(sect.fFirstIndex, start);
		if (sect.fEndRemainder == 0)
			sect.fLastIndex = sect.fLastIndex + 1;
	}
	fLineEnds->AddToElements(sect.fLastIndex, delta, -1);
	TXFormatReflowLines reflow;
	reflow.Reset();
	if (sect.fWholeCount != 0)
		RemoveLines(sect.fWholeIndex, sect.fWholeCount, &reflow);
	if (reflow.fFlag09)
	{
		long line;
		if (reflow.GetFirst(&line))
		{
			TXOffset s = fLineEnds->GetRangeStart(line);
			if (start < s)
				s = start;
			start = s;
		}
		if (reflow.GetLast(&line))
		{
			TXOffset e = fLineEnds->GetRangeEnd(line);
			if (end > e)
				e = end;
			end = e;
		}
	}
	NewtonErr err = Format(start, end, first, last);
	if (*first >= sect.fFirstIndex)
		*first = sect.fFirstIndex;
	return err;
}


// ROM 0x00238a90 WriteToStream__11TXFormatterFP8TXStream
// The count of lines, then each line's length in a byte (a line is never
// more than 128 characters), then the frame formatter's.  DEVIATION: the
// count is written big-endian, as the ROM's memory has it.
NewtonErr
TXFormatter::WriteToStream(TXStream* stream)
{
	long count = fLastLine + 1;
	unsigned char word[4];
	PutBigEndianWord(word, (unsigned int) count);
	NewtonErr err = stream->WriteBytes(word, 4);
	if (err != noErr)
		return err;
	unsigned char previous = 0;
	for (long i = 0; i < count; i++)
	{
		unsigned char end = (unsigned char) fLineEnds->GetRangeEnd(i);
		unsigned char length = (unsigned char) (end - previous);
		err = stream->WriteBytes(&length, 1);
		if (err != noErr)
			return err;
		previous = end;
	}
	return fFrameFormatter->WriteToStream(stream);
}


// ROM 0x00238b38 ReadFromStream__11TXFormatterFP8TXStream
NewtonErr
TXFormatter::ReadFromStream(TXStream* stream)
{
	unsigned char word[4];
	NewtonErr err = stream->ReadBytes(word, 4);
	if (err != noErr)
		return err;
	long count = (long) (int) GetBigEndianWord(word);
	err = fLineEnds->SetCount(count);
	if (err != noErr)
		return err;
	TXOffset end = 0;
	for (long i = 0; i < count; i++)
	{
		unsigned char length;
		err = stream->ReadBytes(&length, 1);
		if (err != noErr)
			return err;
		end += length;
		fLineEnds->SetRangeEnd(i, end);
	}
	fFrameFormatter->FreeData();
	err = fFrameFormatter->ReadFromStream(stream);
	if (err == noErr)
	{
		fLastLine = count - 1;
		CalcLinesHeights();
	}
	return err;
}


// ROM 0x00238c1c CalcLinesHeights__11TXFormatterFv
// Every line's height worked out again from its runs and its ruler.
void
TXFormatter::CalcLinesHeights(void)
{
	TXOffset at = 0;
	TXRunsIterator runs(fRuns, 0);
	long last = fLastLine;
	for (long line = 0; line <= last; line++)
	{
		TXLineHeightInfo info;
		long length = fLineEnds->GetRangeLen(line);
		CalcRunsHeight(at, length, &runs, &info);
		TXRuler* ruler = (TXRuler*) fRulers->OffsetToObject(at, false);
		ruler->AdjustLineHeight(&info);
		if (fFrameFormatter->InsertLineHeightInfo(info, line) != noErr)
			return;
		at += length;
	}
	fFrameFormatter->Format();
}


// ROM 0x00238cfc FreeData__11TXFormatterFUc
long
TXFormatter::FreeData(Boolean appendEmpty)
{
	fLineEnds->FreeData(true);
	fLastLine = -1;
	if (appendEmpty)
		return AppendEmptyLine();
	return -1;
}


// ROM 0x00238d40 Compact__11TXFormatterFv
// (The ROM has the lines' heights' TXArray::Compact inline.)
NewtonErr
TXFormatter::Compact(void)
{
	fLineEnds->Compact();
	return fFrameFormatter->TXArray::Compact();
}


// ROM 0x00238d64 ReserveLines__11TXFormatterFl
NewtonErr
TXFormatter::ReserveLines(long count)
{
	if (count <= 0)
		return noErr;
	long had = fLineEnds->fCount;
	NewtonErr err = fLineEnds->SetCount(had + count);
	fLineEnds->fCount = had;
	return err;
}


// ROM 0x00238d6c CheckFramesReflow__11TXFormatterFRC19TXFormatReflowLinesP16TXFormattingInfoPl
// When the frames' formatter says lines moved between frames: the
// formatting is carried on at least to the last of them, and the first
// is broken again - set as it is (fFlag09 1), or, when it would now
// overflow its frame, pushed into the next.
void
TXFormatter::CheckFramesReflow(const TXFormatReflowLines& reflow, TXFormattingInfo* info, TXOffset* lineEnd)
{
	long line;
	if (reflow.GetLast(&line))
	{
		if (fLastLine <= line)
			line = fLastLine;
		TXOffset end = fLineEnds->GetRangeEnd(line);
		if (end < info->fEnd)
			end = info->fEnd;
		info->fEnd = end;
	}
	if (reflow.GetFirst(&line) && line <= info->fLine)
	{
		TXOffset start = fLineEnds->GetRangeStart(line);
		TXRunsIterator runs(fRuns, start);
		TXLineInfo again;
		long width = fFrames->GetLineFormatWidth(line);
		BreakLine(start, width, &runs, &again);
		if (reflow.fFlag09 == 1)
		{
			*lineEnd = again.fEnd;
			info->fLine = line;
			SetLineInfo(&again, info, false);
		}
		else if (!fFrameFormatter->TestFrameOverflow(line, again.fHeight.fHeight))
		{
			info->fLine = line;
			*lineEnd = again.fEnd;
			SetLineInfo(&again, info, true);
		}
		else
			fFrameFormatter->ForceOverflow(line);
	}
}


// ROM 0x00238f00 GetLineRange__11TXFormatterCFlP13TXOffsetRange
void
TXFormatter::GetLineRange(long line, TXOffsetRange* range) const
{
	TXOffsetPair bounds;
	fLineEnds->GetRangeBounds(line, &bounds);
	range->Set(bounds.fStart, bounds.fEnd, false, true);
}


// ROM 0x00238f3c InsertLine__11TXFormatterFP10TXLineInfoP16TXFormattingInfo
// ==> kError_No_Memory when the line ends could not grow.
NewtonErr
TXFormatter::InsertLine(TXLineInfo* line, TXFormattingInfo* info)
{
	if (fLineEnds->Insert(&line->fEnd, 1, info->fLine) == nil)
		return kError_No_Memory;
	if (fRuler != nil)
		fRuler->AdjustLineHeight(&line->fHeight);
	TXFormatReflowLines reflow;
	reflow.Reset();
	fFrameFormatter->InsertLine(line->fHeight, &reflow, info->fLine);
	fLastLine = fLastLine + 1;
	if (reflow.fFlag09)
		CheckFramesReflow(reflow, info, &line->fEnd);
	return noErr;
}


// ROM 0x00238ff8 SetLineInfo__11TXFormatterFP10TXLineInfoP16TXFormattingInfoUc
// Line `info->fLine` set to end where `line` does, with its height; the
// end it had goes into `info->fOldEnd` - or, when lines after it are now
// inside it and removed, where the last of those ended.
void
TXFormatter::SetLineInfo(TXLineInfo* line, TXFormattingInfo* info, Boolean flag)
{
	info->fOldEnd = fLineEnds->GetRangeEnd(info->fLine);
	fLineEnds->SetRangeEnd(info->fLine, line->fEnd);
	if (fRuler != nil)
		fRuler->AdjustLineHeight(&line->fHeight);
	TXFormatReflowLines reflow;
	reflow.Reset();
	reflow.fFlag08 = flag;
	fFrameFormatter->SetLineHeightInfo(line->fHeight, info->fLine, &reflow);
	if (info->fOldEnd != line->fEnd)
	{
		TXOffset removed = RemoveFormattedLines(info->fLine, &reflow);
		if (removed >= 0)
			info->fOldEnd = removed;
	}
	if (reflow.fFlag09)
		CheckFramesReflow(reflow, info, &line->fEnd);
}
