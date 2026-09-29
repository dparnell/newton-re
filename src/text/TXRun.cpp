/*
	File:		text/TXRun.cpp

	Contains:	The runs and the run ranges - see TXRun.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "TXRun.h"


/*------------------------------------------------------------------------------
	T X R u n
------------------------------------------------------------------------------*/

// ROM 0x00245e64 __ct__5TXRunFv
TXRun::TXRun()
{ }


// ROM 0x00245eac Assign__5TXRunFPC12TXAttrObject
void
TXRun::Assign(const TXAttrObject* /*other*/)
{ }


// ROM 0x00245ea4 FullJustifPortion__5TXRunFRC20TXLineRunDisplayInfo
// How much of the line's slack this run takes when the line is fully
// justified: none, unless a subclass says otherwise.
Fixed
TXRun::FullJustifPortion(const TXLineRunDisplayInfo& /*info*/)
{
	return 0;
}


// ROM 0x00245eb8 VisibleLen__5TXRunFPCUsl
// How many of the characters show: all of them.
long
TXRun::VisibleLen(const UniChar* /*text*/, long count)
{
	return count;
}


// ROM 0x00245eb0 Click__5TXRunFRC17TXRunPositionInfoP16TXPointingDeviceliRC4RectP18TXClickCommandInfo
long
TXRun::Click(const TXRunPositionInfo& /*where*/, TXPointingDevice* /*pen*/, long /*offset*/, int /*clicks*/, const Rect& /*bounds*/, TXClickCommandInfo* /*command*/)
{
	return 0;
}


// ROM 0x00245ec0 SetHilite__5TXRunFcRC17TXRunPositionInfoUc
void
TXRun::SetHilite(char /*on*/, const TXRunPositionInfo& /*where*/, Boolean /*draw*/)
{ }


// ROM 0x00245ec4 DrawHilite__5TXRunFRC17TXRunPositionInfo
void
TXRun::DrawHilite(const TXRunPositionInfo& /*where*/)
{ }


/*------------------------------------------------------------------------------
	T X R u n R a n g e
------------------------------------------------------------------------------*/

// ROM 0x00245cc4 __ct__10TXRunRangeFc
TXRunRange::TXRunRange(char kind)
	: TXObjectRange(kind == 2 ? 10 : 1)
{ }


// ROM 0x00245d18 __dt__10TXRunRangeFv
TXRunRange::~TXRunRange()
{ }


// ROM 0x00245d58 IsTextRun__10TXRunRangeCFl
TXRun*
TXRunRange::IsTextRun(long index) const
{
	TXRun* run = (TXRun*) RangeIndexToObject(index);
	return run->IsTextRun() ? run : nil;
}


// ROM 0x00245d84 CharToTextRun__10TXRunRangeCF8TXOffset
TXRun*
TXRunRange::CharToTextRun(TXOffset offset, Boolean atStart) const
{
	long index = OffsetToRangeIndex(offset, atStart);
	if (index < 0)
		return nil;
	TXRun* run = SearchTextRunBackward(index);
	if (run == nil)
		run = SearchTextRunForward(index + 1);
	return run;
}


// ROM 0x00245dc8 SearchTextRunBackward__10TXRunRangeCFl
TXRun*
TXRunRange::SearchTextRunBackward(long index) const
{
	for ( ; index >= 0; index--)
	{
		TXRun* run = IsTextRun(index);
		if (run != nil)
			return run;
	}
	return nil;
}


// ROM 0x00245e04 SearchTextRunForward__10TXRunRangeCFl
TXRun*
TXRunRange::SearchTextRunForward(long index) const
{
	long count = fCount;
	for ( ; index < count; index++)
	{
		TXRun* run = IsTextRun(index);
		if (run != nil)
			return run;
	}
	return nil;
}


// ROM 0x00245e4c TXIndivisibleCharToPixel__FRC20TXLineRunDisplayInfol
Fixed
TXIndivisibleCharToPixel(const TXLineRunDisplayInfo& info, long offset)
{
	if (offset == 0)
		return 0;
	return info.fWidth + info.fJustifyExtra;
}


// ROM 0x00245c18 TXIndivisiblePixelToChar__FRC20TXLineRunDisplayInfolP13TXOffsetRange
// Left of the margin: in front of it; right of the other margin: after
// it; between them: the whole run.  The margins are a quarter of the
// width when the run's character is a control character, else none.
void
TXIndivisiblePixelToChar(const TXLineRunDisplayInfo& info, Fixed pixel, TXOffsetRange* range)
{
	int width = (short) ((ULong) (info.fWidth + info.fJustifyExtra + 0x8000) >> 16);
	int margin = 0;
	if (info.fText[0] < 0x20)
		margin = width >> 2;
	long x = pixel >> 16;
	if (margin < x)
	{
		if (x < width - margin)
		{
			range->Set(0, info.fLength, false, true);
			return;
		}
		long end = info.fLength;
		range->Set(end, end, end != 0, end != 0);
	}
	else
		range->Set(0, 0, false, false);
}
