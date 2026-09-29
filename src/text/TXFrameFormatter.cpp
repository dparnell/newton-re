/*
	File:		text/TXFrameFormatter.cpp

	Contains:	The frame formatters (TXFrameFormatter.h).

	Reconstructed from the MP2x00 US ROM (0x002396e4-0x00239b4c,
	0x002415b0-0x0024282c); each function cites its origin.
*/

#include "TXFrameFormatter.h"
#include "TXStream.h"
#include "ByteOrder.h"
#include "OSErrors.h"


// The ROM's globals from 0x0c104d98: gFramesEditInfo, eight bytes no
// symbol names, and gTXParagCtrlChars at 0x0c104de0.  An edit that
// catches more than two frames writes on from the first into the others
// (TXFramesEditInfo, ROM BUG), so the host keeps them in the same order.
struct TXFramesEditGlobals
{
	TXFramesEditInfo	fFramesEditInfo;	// +0x00
	long				fUnnamed[2];		// +0x40
	TXParagCtrlChars	fParagCtrlChars;	// +0x48
};

static TXFramesEditGlobals	gTXFramesEditGlobals;

TXFramesEditInfo&	gFramesEditInfo = gTXFramesEditGlobals.fFramesEditInfo;
TXParagCtrlChars&	gTXParagCtrlChars = gTXFramesEditGlobals.fParagCtrlChars;


// ROM 0x002399b0 CatchFrame__16TXFramesEditInfoFl
TXFrameEditInfo*
TXFramesEditInfo::CatchFrame(long frame)
{
	long n = fCount;
	if (n == 0)
		fFirst = frame;
	fLast = frame;
	fCount = n + 1;
	TXFrameEditInfo* info = &fInfos[n];
	info->fFrame = frame;
	info->fHeightChange = 0;
	info->fFlags = 0;
	info->fField08 = 0;
	info->fField10 = 0;
	info->fField14 = 0;
	return info;
}


// ROM 0x002399f0 GetEditInfoPtr__16TXFramesEditInfoCFlPP15TXFrameEditInfoi
Boolean
TXFramesEditInfo::GetEditInfoPtr(long frame, TXFrameEditInfo** info, int mask) const
{
	*info = nil;
	if (fCount != 0 && fFirst <= frame && frame <= fLast)
	{
		for (const TXFrameEditInfo* p = fInfos; p < fInfos + fCount; p++)
		{
			if (p->fFrame == frame)
			{
				*info = (TXFrameEditInfo*) p;
				return (p->fFlags & mask) == 0;
			}
		}
	}
	return false;
}


// ROM 0x00239a74 GetNext__16TXFramesEditInfoFv
TXFrameEditInfo*
TXFramesEditInfo::GetNext(void)
{
	long n = fNext;
	if (n != fCount)
	{
		fNext = n + 1;
		return &fInfos[n];
	}
	fNext = 0;
	return nil;
}


// ROM 0x00239aa8 SetEditFlag__16TXFramesEditInfoFilT2
void
TXFramesEditInfo::SetEditFlag(int flag, long frame, long count)
{
	long n = fCount;
	if (n == 0)
		return;
	if (count == 1)
	{
		TXFrameEditInfo* info;
		if (GetEditInfoPtr(frame, &info, 0))
			info->fFlags |= flag;
		return;
	}
	if (count < 0x7fffffff)
		count = frame + count;
	for (TXFrameEditInfo* p = fInfos; p < fInfos + n; p++)
		if (frame <= p->fFrame && p->fFrame < count)
			p->fFlags |= flag;
}


// ROM 0x002396e4 __ct__16TXFrameFormatterFv
TXFrameFormatter::TXFrameFormatter()
	: fLineEnds(nil)
{ }


// ROM 0x00239724 TestFrameOverflow__16TXFrameFormatterFlT1
Boolean
TXFrameFormatter::TestFrameOverflow(long line, long extra)
{
	long frame = LineToFrame(line, false);
	long textHeight = GetFrameTextHeight(frame);
	long lineHeight = GetLinesHeight(line, line);
	long frameHeight = GetFrameHeight(frame);
	return frameHeight < (textHeight - lineHeight) + extra;
}


// ROM 0x002397c0 CharToFrame__16TXFrameFormatterCF8TXOffset
long
TXFrameFormatter::CharToFrame(TXOffset offset, Boolean atStart) const
{
	long line = fLineEnds->OffsetToRangeIndex(offset, atStart);
	return LineToFrame(line, false);
}


// ROM 0x00239808 CharRangeChanged__16TXFrameFormatterFP7TXCharslN22Ul
void
TXFrameFormatter::CharRangeChanged(TXChars* /*chars*/, long /*start*/, long /*oldLength*/, long /*newLength*/, unsigned long /*flags*/)
{ }


// ROM 0x0023980c Format__16TXFrameFormatterFv
NewtonErr
TXFrameFormatter::Format(void)
{
	return noErr;
}


// ROM 0x00239814 BeginEdit__16TXFrameFormatterFv
void
TXFrameFormatter::BeginEdit(void)
{
	gFramesEditInfo.fCount = 0;
	gFramesEditInfo.fNext = 0;
}


// ROM 0x0023982c EndEdit__16TXFrameFormatterFv
void
TXFrameFormatter::EndEdit(void)
{
	gFramesEditInfo.fCount = 0;
}


// ROM 0x00239840 CatchFrame__16TXFrameFormatterFl
TXFrameEditInfo*
TXFrameFormatter::CatchFrame(long frame)
{
	return gFramesEditInfo.CatchFrame(frame);
}


// ROM 0x0023984c GetNextFrameEditInfo__16TXFrameFormatterFv
TXFrameEditInfo*
TXFrameFormatter::GetNextFrameEditInfo(void)
{
	return gFramesEditInfo.GetNext();
}


// ROM 0x00239858 WriteToStream__16TXFrameFormatterFP8TXStream
NewtonErr
TXFrameFormatter::WriteToStream(TXStream* /*stream*/)
{
	return noErr;
}


// ROM 0x00239860 ReadFromStream__16TXFrameFormatterFP8TXStream
NewtonErr
TXFrameFormatter::ReadFromStream(TXStream* /*stream*/)
{
	return noErr;
}


// ROM 0x00239868 __ct__20TXMonoFrameFormatterFv
TXMonoFrameFormatter::TXMonoFrameFormatter()
{ }


// ROM 0x002398a8 CatchFrame__20TXMonoFrameFormatterFl
TXFrameEditInfo*
TXMonoFrameFormatter::CatchFrame(long frame)
{
	long height = fTotalHeight;
	TXFrameEditInfo* info = gFramesEditInfo.CatchFrame(frame);
	info->fHeightChange = height;
	return info;
}


// ROM 0x002398cc GetNextFrameEditInfo__20TXMonoFrameFormatterFv
TXFrameEditInfo*
TXMonoFrameFormatter::GetNextFrameEditInfo(void)
{
	TXFrameEditInfo* info = gFramesEditInfo.GetNext();
	if (info != nil)
		info->fHeightChange = fTotalHeight - info->fHeightChange;
	return info;
}


// ROM 0x00239904 SetFrameHeight__20TXMonoFrameFormatterFlT1
void
TXMonoFrameFormatter::SetFrameHeight(long /*frame*/, long /*height*/)
{ }


// ROM 0x00239908 GetFrameHeight__20TXMonoFrameFormatterCFl
long
TXMonoFrameFormatter::GetFrameHeight(long /*frame*/) const
{
	return 0x7fff;
}


// ROM 0x00239914 GetFrameTextHeight__20TXMonoFrameFormatterCFl
long
TXMonoFrameFormatter::GetFrameTextHeight(long /*frame*/) const
{
	return fTotalHeight;
}


// ROM 0x0023991c ForceOverflow__20TXMonoFrameFormatterFl
NewtonErr
TXMonoFrameFormatter::ForceOverflow(long /*line*/)
{
	return noErr;
}


// ROM 0x00239924 GetCountFrames__20TXMonoFrameFormatterCFv
long
TXMonoFrameFormatter::GetCountFrames(void) const
{
	return 1;
}


// ROM 0x00239978 GetFrameLineRange__20TXMonoFrameFormatterCFlP12TXOffsetPair
Boolean
TXMonoFrameFormatter::GetFrameLineRange(long /*frame*/, TXOffsetPair* lines) const
{
	lines->fStart = 0;
	long last = fLastLine;
	lines->fEnd = last;
	return last >= 0;
}


// ROM 0x0023999c LineToFrame__20TXMonoFrameFormatterCF8TXOffset
long
TXMonoFrameFormatter::LineToFrame(TXOffset /*line*/, Boolean /*atStart*/) const
{
	return 0;
}


// ROM 0x002415b0 __ct__21TXMultiFrameFormatterFv
TXMultiFrameFormatter::TXMultiFrameFormatter()
{
	fFrames = new TXRanges(sizeof(TXFrameLines), 1);
	fPageBreaks = nil;
	fReflowAll = false;
}


// ROM 0x00241610 __dt__21TXMultiFrameFormatterFv
TXMultiFrameFormatter::~TXMultiFrameFormatter()
{
	if (fFrames != nil)
		delete fFrames;
	if (fPageBreaks != nil)
		delete fPageBreaks;
}


// ROM 0x00241680 FreeData__21TXMultiFrameFormatterFv
void
TXMultiFrameFormatter::FreeData(void)
{
	TXLinesHeights::FreeData();
	fFrames->FreeData(true);
	if (fPageBreaks == nil)
		return;
	delete fPageBreaks;
	fPageBreaks = nil;
}


// ROM 0x002416cc Compact__21TXMultiFrameFormatterFv
// (The page breaks' TXArray::Compact is written out in line in the ROM.)
NewtonErr
TXMultiFrameFormatter::Compact(void)
{
	TXArray::Compact();
	fFrames->Compact();
	if (fPageBreaks == nil)
		return noErr;
	return fPageBreaks->Compact();
}


// ROM 0x002416fc VariableSizeFrames__21TXMultiFrameFormatterCFv
Boolean
TXMultiFrameFormatter::VariableSizeFrames(void) const
{
	return false;
}


// ROM 0x00241704 GetCountFrames__21TXMultiFrameFormatterCFv
long
TXMultiFrameFormatter::GetCountFrames(void) const
{
	return fFrames->fCount;
}


// ROM 0x00241710 GetFrameLineRange__21TXMultiFrameFormatterCFlP12TXOffsetPair
Boolean
TXMultiFrameFormatter::GetFrameLineRange(long frame, TXOffsetPair* lines) const
{
	if (frame < GetCountFrames())
	{
		lines->fStart = fFrames->GetRangeStart(frame);
		lines->fEnd = fFrames->GetRangeEnd(frame) - 1;
		return true;
	}
	return false;
}


// ROM 0x0024176c LineToFrame__21TXMultiFrameFormatterCF8TXOffset
long
TXMultiFrameFormatter::LineToFrame(TXOffset line, Boolean atStart) const
{
	return fFrames->OffsetToRangeIndex(line, atStart);
}


// ROM 0x0024177c GetFrameTextHeight__21TXMultiFrameFormatterCFl
long
TXMultiFrameFormatter::GetFrameTextHeight(long frame) const
{
	return ((TXFrameLines*) fFrames->GetElementPtr(frame))->fHeight;
}


// ROM 0x00241798 MeasureFrame__21TXMultiFrameFormatterFlPlT2P12TXOffsetPair
// The frame filled from its first line: as many lines as its height
// holds - one at least, counted as filling it - or up to a page break
// before that.
Boolean
TXMultiFrameFormatter::MeasureFrame(long frame, long* shift, long* end, TXOffsetPair* lines)
{
	long height = GetFrameHeight(frame);
	long first = fFrames->GetRangeStart(frame);
	*end = PixelToLine(&height, first, nil, nil);
	if (*end == first)
	{
		height = GetFrameHeight(frame);
		*end = *end + 1;
	}
	if (fPageBreaks != nil)
		CheckFrameBreaks(first, end, &height);
	TXFrameLines* e = (TXFrameLines*) fFrames->GetElementPtr(frame);
	*shift = e->fHeight - height;
	if (*shift == 0)
		return false;
	long last;
	if (*end <= e->fEnd)
	{
		lines->fStart = *end;
		last = e->fEnd;
	}
	else
	{
		lines->fStart = e->fEnd;
		last = *end;
	}
	lines->fEnd = last - 1;
	return true;
}


// ROM 0x00241898 BreakFrame__21TXMultiFrameFormatterFlPlP12TXOffsetPairP19TXFormatReflowLines
// (The reflow lines are not looked at.)
Boolean
TXMultiFrameFormatter::BreakFrame(long frame, long* shift, TXOffsetPair* lines, TXFormatReflowLines* /*reflow*/)
{
	long end;
	if (!MeasureFrame(frame, shift, &end, lines))
		return false;
	TXFrameLines* e = (TXFrameLines*) fFrames->GetElementPtr(frame);
	e->fHeight = e->fHeight - *shift;
	e->fEnd = end;
	return true;
}


// ROM 0x00241900 CheckReflow__21TXMultiFrameFormatterFlP19TXFormatReflowLinesUcT1PC12TXOffsetPair
// Read from the assembly: the decompiler folds the loop's two exits
// together.  The lines each break moves (`moved`) are written and never
// read - in the ROM too.  A frame that now takes more than the one after
// it holds empties that one (and those after it that it also covers),
// which are removed; what is left over at the end becomes a new frame.
NewtonErr
TXMultiFrameFormatter::CheckReflow(long frame, TXFormatReflowLines* reflow, Boolean back, long shift, const TXOffsetPair* lines)
{
	fReflowAll = false;
	long count = fFrames->fCount;
	if (count == 0 || frame >= count || !reflow->fFlag08)
		return noErr;
	long f = frame;
	if (back)
	{
		f = frame - 1;
		if (f <= 0)
			f = 0;
	}
	fFrames->Lock(false);
	TXFrameLines* e = (TXFrameLines*) fFrames->GetElementPtr(f);
	TXOffsetPair moved;
	if (lines != nil)
		moved = *lines;
	for (;;)
	{
		TXFrameEditInfo* info;
		long carried;
		if (shift == 0)
			BreakFrame(f, &shift, &moved, reflow);
		if (shift != 0 && gFramesEditInfo.GetEditInfoPtr(f, &info, 0x101))
		{
			info->fField14 += shift;
			carried = shift;
		}
		else
			carried = 0;
		f++;
		if (f == count)
			break;
		e++;
		if (shift == 0)
		{
			if (f > frame)
				break;
			continue;
		}
		if (shift < 0 && e->fHeight <= -shift)
		{
			carried = 0;
			shift = -shift;
			count -= RemoveFormattedFrames(f, &shift, &moved);
			if (f == count)
				break;
			shift = -shift;
		}
		if (shift != 0 && gFramesEditInfo.GetEditInfoPtr(f, &info, 0x101))
		{
			if (carried == 0)
				carried = shift;
			info->fField10 += carried;
		}
		e->fHeight += shift;
		shift = 0;
	}
	fFrames->Unlock();
	if (shift != 0)
		AppendFrame(shift, -1);
	return noErr;
}


// ROM 0x00241b44 InsertLine__21TXMultiFrameFormatterFRC16TXLineHeightInfoP19TXFormatReflowLinesl
NewtonErr
TXMultiFrameFormatter::InsertLine(const TXLineHeightInfo& info, TXFormatReflowLines* reflow, long line)
{
	NewtonErr err = TXLinesHeights::InsertLine(info, reflow, line);
	if (err != noErr)
		return err;
	long frame;
	if (line < 0)
		frame = fFrames->fCount - 1;
	else
		frame = LineToFrame(line, true);
	if (frame < 0)
		return AppendFrame(info.fHeight, 1);
	TXFrameEditInfo* edit;
	if (gFramesEditInfo.fCount != 0 && gFramesEditInfo.GetEditInfoPtr(frame, &edit, 0x101))
	{
		TXLineHeightInfo height;
		GetLineHeightInfo(line, &height);
		edit->fHeightChange += height.fHeight;
	}
	fFrames->AddToElements(frame, 1, -1);
	TXFrameLines* e = (TXFrameLines*) fFrames->GetElementPtr(frame);
	e->fHeight = e->fHeight + info.fHeight;
	if (fReflowAll || fPageBreaks != nil || GetFrameHeight(frame) < e->fHeight)
		return CheckReflow(frame, reflow, false, 0, nil);
	return noErr;
}


// ROM 0x00241cc4 SetLineHeightInfo__21TXMultiFrameFormatterFRC16TXLineHeightInfolP19TXFormatReflowLines
// The frame is told how much the line's height changed and reflowed when
// the line grew past its room or shrank (lines may come back), or on
// every change when page breaks are involved.  A line whose height does
// not change reflows its frame too, from the one before, when there are
// page breaks or the frames are of different sizes.
NewtonErr
TXMultiFrameFormatter::SetLineHeightInfo(const TXLineHeightInfo& info, long line, TXFormatReflowLines* reflow)
{
	TXLineHeightInfo old;
	GetLineHeightInfo(line, &old);
	if (old.fHeight == info.fHeight && old.fAscent == info.fAscent)
	{
		if (fReflowAll || fPageBreaks != nil || VariableSizeFrames())
			CheckReflow(fFrames->OffsetToRangeIndex(line, false), reflow, true, 0, nil);
		return noErr;
	}
	long frame = fFrames->OffsetToRangeIndex(line, false);
	if (frame < 0)
		return TXLinesHeights::SetLineHeightInfo(info, line, reflow);
	// (GetEditInfoPtr's answer is not looked at: an entry that has the
	// flags is added to all the same.)
	TXFrameEditInfo* edit;
	if (gFramesEditInfo.fCount != 0)
		gFramesEditInfo.GetEditInfoPtr(frame, &edit, 0x101);
	else
		edit = nil;
	NewtonErr err = TXLinesHeights::SetLineHeightInfo(info, line, reflow);
	if (err != noErr)
		return err;
	long delta = info.fHeight - old.fHeight;
	if (edit != nil)
		edit->fHeightChange += delta;
	TXFrameLines* e = (TXFrameLines*) fFrames->GetElementPtr(frame);
	e->fHeight = e->fHeight + delta;
	Boolean back = delta < 0;
	if (back || fReflowAll || fPageBreaks != nil || GetFrameHeight(frame) < e->fHeight)
		err = CheckReflow(frame, reflow, back, 0, nil);
	return err;
}


// ROM 0x00241ec0 RemoveLines__21TXMultiFrameFormatterFlT1P19TXFormatReflowLines
// The lines' heights taken off the frames they were in (the first and
// last frames only partly emptied, the ones between removed), the frames
// after them brought forward, and the frames reflowed from the first.
void
TXMultiFrameFormatter::RemoveLines(long count, long line, TXFormatReflowLines* reflow)
{
	TXSectRanges sect;
	long n = fFrames->SectRanges(line, count, &sect);
	if (n == 0)
	{
		TXLinesHeights::RemoveLines(count, line, reflow);
		return;
	}
	if (sect.fEndRemainder != 0 && (n > 1 || sect.fStartOffset == 0))
	{
		long start = fFrames->GetRangeStart(sect.fLastIndex);
		RemoveFrameLines(sect.fLastIndex, start, sect.fLastLen + start - 1);
	}
	if (sect.fStartOffset != 0)
	{
		long first = fFrames->GetRangeStart(sect.fFirstIndex) + sect.fStartOffset;
		RemoveFrameLines(sect.fFirstIndex, first, sect.fFirstLen + first - 1);
		if (sect.fLastIndex != sect.fFirstIndex)
			fFrames->SetRangeEnd(sect.fFirstIndex, first);
	}
	if (sect.fEndRemainder == 0 && sect.fLastIndex != sect.fFirstIndex)
		sect.fLastIndex++;
	fFrames->AddToElements(sect.fLastIndex, -count, -1);
	if (sect.fWholeCount != 0)
		RemoveFrames(sect.fWholeCount, sect.fWholeIndex);
	TXLinesHeights::RemoveLines(count, line, reflow);
	if (fLastLine != -1)
		CheckReflow(sect.fFirstIndex, reflow, true, 0, nil);
}


// ROM 0x00242054 RemoveFrames__21TXMultiFrameFormatterFlT1
// (SetEditFlag is written out in line in the ROM.)
void
TXMultiFrameFormatter::RemoveFrames(long count, long frame)
{
	fFrames->Remove(frame, count);
	gFramesEditInfo.SetEditFlag(0x1fe, frame, 0x7fffffff);
}


// ROM 0x002420a0 RemoveFrameLines__21TXMultiFrameFormatterFlN21
void
TXMultiFrameFormatter::RemoveFrameLines(long frame, long first, long last)
{
	long height = GetLinesHeight(first, last);
	TXFrameLines* e = (TXFrameLines*) fFrames->GetElementPtr(frame);
	e->fHeight = e->fHeight - height;
	TXFrameEditInfo* edit;
	if (gFramesEditInfo.GetEditInfoPtr(frame, &edit, 0x101))
		edit->fHeightChange -= height;
}


// ROM 0x00242118 AppendFrame__21TXMultiFrameFormatterFlT1
NewtonErr
TXMultiFrameFormatter::AppendFrame(long height, long end)
{
	TXFrameLines* e = (TXFrameLines*) fFrames->Insert(nil, 1, -1);
	if (e == nil)
		return kError_No_Memory;
	if (end < 0)
		end = fLastLine + 1;
	e->fEnd = end;
	e->fHeight = height;
	return noErr;
}


// ROM 0x00242170 ForceOverflow__21TXMultiFrameFormatterFl
NewtonErr
TXMultiFrameFormatter::ForceOverflow(long line)
{
	long frame = LineToFrame(line, false);
	TXFrameLines* e = (TXFrameLines*) fFrames->GetElementPtr(frame);
	TXOffsetPair moved;
	moved.fEnd = e->fEnd - 1;
	long height = GetLinesHeight(line, moved.fEnd);
	e->fHeight = e->fHeight - height;
	e->fEnd = line;
	if (GetCountFrames() - 1 == frame)
		return AppendFrame(height, -1);
	moved.fStart = line;
	TXFormatReflowLines reflow;
	reflow.Reset();
	return CheckReflow(frame, &reflow, false, height, &moved);
}


// ROM 0x00242258 RemoveFormattedFrames__21TXMultiFrameFormatterFlPlP12TXOffsetPair
// (Nothing stops the walk at the last frame: the caller's shift is never
// more than the frames after `frame` hold.)
long
TXMultiFrameFormatter::RemoveFormattedFrames(long frame, long* shift, TXOffsetPair* lines)
{
	TXFrameLines* e = (TXFrameLines*) fFrames->GetElementPtr(frame);
	long n = 0;
	long left = *shift;
	for ( ; left != 0 && e->fHeight <= left; e++)
	{
		left -= e->fHeight;
		n++;
		lines->fStart = e->fEnd;
	}
	RemoveFrames(n, frame);
	*shift = left;
	return n;
}


// ROM 0x002422c8 CharRangeChanged__21TXMultiFrameFormatterFP7TXCharslN22Ul
// A page break inside what was taken out is dropped; when that leaves
// none, the table goes and every later change reflows (fReflowAll).
void
TXMultiFrameFormatter::CharRangeChanged(TXChars* chars, long start, long oldLength, long newLength, unsigned long flags)
{
	long index;
	if (fPageBreaks == nil)
		index = 0;
	else
	{
		long found;
		index = fPageBreaks->Search(start, &found);
		if (oldLength != 0)
		{
			long n = 0;
			long count = fPageBreaks->fCount;
			if (index < count)
			{
				long end = start + oldLength;
				long i = index;
				do
				{
					long at = *(long*) fPageBreaks->GetElementPtr(i);
					i++;
					if (at < start || end <= at)
						break;
					n++;
				} while (i < count);
				if (n != 0 && fPageBreaks->Remove(index, n) == 0)
				{
					fReflowAll = true;
					if (fPageBreaks != nil)
						delete fPageBreaks;
					fPageBreaks = nil;
				}
			}
		}
		if (fPageBreaks != nil)
			fPageBreaks->AddToElements(index, newLength - oldLength, -1);
	}
	if (flags & 4)
	{
		while (newLength > 0)
		{
			long at = chars->SearchChar(10, start, newLength);
			if (at < 0)
				return;
			at += start;
			if (fPageBreaks == nil)
			{
				fPageBreaks = new TXLongTagArray(sizeof(long), 0);
				if (fPageBreaks == nil)
					return;
			}
			fPageBreaks->Insert(&at, 1, index);
			at++;
			index++;
			newLength -= at - start;
			start = at;
		}
	}
}


// ROM 0x002424a4 CheckFrameBreaks__21TXMultiFrameFormatterFlPlT2
// (TXLongTagArray::Search's `found` is the break at or after the frame's
// first character; the line that ends just past it is the page's last.)
void
TXMultiFrameFormatter::CheckFrameBreaks(long first, long* end, long* height)
{
	TXOffset start = fLineEnds->GetRangeStart(first);
	long brk;
	fPageBreaks->Search(start, &brk);
	brk++;
	if (brk <= start)
		return;
	if (fLineEnds->GetRangeEnd(*end - 1) <= brk)
		return;
	for (long line = *end - 1; line >= first; line--)
	{
		if (fLineEnds->GetRangeEnd(line) == brk)
		{
			*end = line + 1;
			break;
		}
	}
	*height = GetLinesHeight(first, *end - 1);
}


// ROM 0x00242568 WriteToStream__21TXMultiFrameFormatterFP8TXStream
// The count (at most 0xffff) in a halfword, then the breaks.  DEVIATION:
// the words are written big-endian, as the ROM's memory has them.
NewtonErr
TXMultiFrameFormatter::WriteToStream(TXStream* stream)
{
	long count;
	if (fPageBreaks == nil)
		count = 0;
	else
	{
		count = fPageBreaks->fCount;
		if (count >= 0xffff)
			count = 0xffff;
	}
	unsigned char half[2];
	PutBigEndianHalf(half, (unsigned short) count);
	NewtonErr err = stream->WriteBytes(half, 2);
	if (err != noErr)
		return err;
	for (long i = 0; i < count; i++)
	{
		unsigned char word[4];
		PutBigEndianWord(word, (unsigned int) *(long*) fPageBreaks->GetElementPtr(i));
		err = stream->WriteBytes(word, 4);
		if (err != noErr)
			return err;
	}
	return noErr;
}


// ROM 0x0024262c ReadFromStream__21TXMultiFrameFormatterFP8TXStream
// (A table that cannot be made is not noticed: SetCount is sent to nil.)
NewtonErr
TXMultiFrameFormatter::ReadFromStream(TXStream* stream)
{
	unsigned char half[2];
	NewtonErr err = stream->ReadBytes(half, 2);
	if (err != noErr)
		return err;
	long count = GetBigEndianHalf(half);
	if (count == 0)
		return noErr;
	if (fPageBreaks == nil)
		fPageBreaks = new TXLongTagArray(sizeof(long), 0);
	err = fPageBreaks->SetCount(count);
	if (err != noErr)
		return err;
	for (long i = 0; i < count; i++)
	{
		unsigned char word[4];
		err = stream->ReadBytes(word, 4);
		if (err != noErr)
			return err;
		*(long*) fPageBreaks->GetElementPtr(i) = (long) (int) GetBigEndianWord(word);
	}
	return err;
}


// ROM 0x00242704 __ct__15TXPageFormatterFv
TXPageFormatter::TXPageFormatter()
	: fPageHeight(0)
{ }


// ROM 0x0024274c Format__15TXPageFormatterFv
NewtonErr
TXPageFormatter::Format(void)
{
	fFrames->FreeData(true);
	long count = fLastLine + 1;
	long line = 0;
	if (count > 0)
	{
		long end;
		do
		{
			long height = fPageHeight;
			end = PixelToLine(&height, line, nil, nil);
			if (end == line)
			{
				height = fPageHeight;
				end = end + 1;
			}
			if (fPageBreaks != nil)
				CheckFrameBreaks(line, &end, &height);
			NewtonErr err = AppendFrame(height, end);
			if (err != noErr)
				return err;
			line = end;
		} while (end < count);
	}
	return noErr;
}


// ROM 0x0024281c SetFrameHeight__15TXPageFormatterFlT1
void
TXPageFormatter::SetFrameHeight(long /*frame*/, long height)
{
	fPageHeight = height;
}


// ROM 0x00242824 GetFrameHeight__15TXPageFormatterCFl
long
TXPageFormatter::GetFrameHeight(long /*frame*/) const
{
	return fPageHeight;
}
