/*
	File:		text/TXFrameFormatter.cpp

	Contains:	The frame formatters (TXFrameFormatter.h).

	Reconstructed from the MP2x00 US ROM (0x002396e4-0x00239b4c); each
	function cites its origin.
*/

#include "TXFrameFormatter.h"


TXFramesEditInfo	gFramesEditInfo;


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
