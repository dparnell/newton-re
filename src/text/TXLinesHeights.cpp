/*
	File:		text/TXLinesHeights.cpp

	Contains:	The lines' heights and a paragraph's control characters -
				see TXLinesHeights.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "TXLinesHeights.h"
#include "OSErrors.h"


/*------------------------------------------------------------------------------
	T X F o r m a t R e f l o w L i n e s
------------------------------------------------------------------------------*/

// ROM 0x0023962c Reset__19TXFormatReflowLinesFv
void
TXFormatReflowLines::Reset(void)
{
	fFirst = -1;
	fLast = -1;
	fFlag08 = true;
	fFlag09 = false;
}


// ROM 0x0023964c GetFirst__19TXFormatReflowLinesCFPl
Boolean
TXFormatReflowLines::GetFirst(long* line) const
{
	*line = fFirst;
	return fFirst >= 0;
}


// ROM 0x00239668 GetLast__19TXFormatReflowLinesCFPl
Boolean
TXFormatReflowLines::GetLast(long* line) const
{
	*line = fLast;
	return fLast >= 0;
}


/*------------------------------------------------------------------------------
	T X L i n e s H e i g h t s
------------------------------------------------------------------------------*/

// ROM 0x002390d4 __ct__14TXLinesHeightsFv
TXLinesHeights::TXLinesHeights()
	: TXArray(sizeof(TXLineHeightGroup), 0)
{
	fTotalHeight = 0;
	fLastLine = -1;
}


// ROM 0x0023912c FreeData__14TXLinesHeightsFv
void
TXLinesHeights::FreeData(void)
{
	SetCount(0);
	fTotalHeight = 0;
	fLastLine = -1;
}


// ROM 0x00239158 Concat__14TXLinesHeightsFlT1PC17TXLineHeightGroup
Boolean
TXLinesHeights::Concat(long a, long b, const TXLineHeightGroup* group)
{
	if (group == nil)
	{
		if (!EqualGroup(a, b))
			return false;
		TXLineHeightGroup* first = (TXLineHeightGroup*) GetElementPtr(a);
		TXLineHeightGroup* second = (TXLineHeightGroup*) GetElementPtr(b);
		first->fCount += second->fCount;
	}
	else
	{
		long into;
		if (EqualGroup(a, *group))
			into = a;
		else if (EqualGroup(b, *group))
			into = b;
		else
			return false;
		((TXLineHeightGroup*) GetElementPtr(into))->fCount += group->fCount;
	}
	return true;
}


// ROM 0x00239228 SetLineHeightInfo__14TXLinesHeightsFRC16TXLineHeightInfolP19TXFormatReflowLines
// The line's group is changed where it is when the line is all there is
// of it (folding it into an equal neighbour when there is one, and
// joining the two neighbours when they then match); at either end of a
// longer group the line leaves it for an equal neighbour or a group of
// its own; in the middle the group is split in three.
NewtonErr
TXLinesHeights::SetLineHeightInfo(const TXLineHeightInfo& info, long line, TXFormatReflowLines* /*reflow*/)
{
	long inGroup = line;
	long index;
	TXLineHeightGroup* group = LineToHeightGroup(&inGroup, &index);
	if (group->fHeight == info.fHeight && group->fAscent == info.fAscent)
		return noErr;
	TXLineHeightGroup mine = { 1, info.fHeight, info.fAscent };
	fTotalHeight += info.fHeight - group->fHeight;
	long count = group->fCount;
	if (count == 1)
	{
		if (!Concat(index - 1, index + 1, &mine))
			*group = mine;
		else
			Remove(index, Concat(index - 1, index + 1, nil) ? 2 : 1);
		return noErr;
	}
	long at = index + (inGroup != 0 ? 1 : 0);
	if (inGroup != 0 && count - 1 != inGroup)
	{
		// in the middle: split in three
		TXLineHeightGroup rest = *group;
		group->fCount = inGroup;
		TXLineHeightGroup* two = (TXLineHeightGroup*) Insert(nil, 2, at);
		if (two == nil)
			return kError_No_Memory;
		two[0] = mine;
		two[1] = rest;
		two[1].fCount = count - inGroup - 1;
		return noErr;
	}
	// at an end of it
	group->fCount = count - 1;
	if (Concat(at - 1, at, &mine))
		return noErr;
	if (Insert(&mine, 1, at) == nil)
		return kError_No_Memory;
	return noErr;
}


// ROM 0x002393f8 InsertLine__14TXLinesHeightsFRC16TXLineHeightInfoP19TXFormatReflowLinesl
// The new line is first counted into the group of the line it goes
// before (or the last line's, at the end), at that group's height, and
// then given its own.
NewtonErr
TXLinesHeights::InsertLine(const TXLineHeightInfo& info, TXFormatReflowLines* reflow, long line)
{
	fLastLine++;
	if (fLastLine == 0)
	{
		TXLineHeightGroup* group = (TXLineHeightGroup*) Insert(nil, 1, 0);
		if (group == nil)
			return kError_No_Memory;
		group->fCount = 1;
		group->fAscent = info.fAscent;
		group->fHeight = info.fHeight;
		fTotalHeight = info.fHeight;
		return noErr;
	}
	if (line < 0)
		line = fLastLine;
	long inGroup = line - (fLastLine == line ? 1 : 0);
	TXLineHeightGroup* group = LineToHeightGroup(&inGroup, nil);
	group->fCount++;
	fTotalHeight += group->fHeight;
	return SetLineHeightInfo(info, line, reflow);
}


// ROM 0x002394d0 InsertLineHeightInfo__14TXLinesHeightsFRC16TXLineHeightInfol
NewtonErr
TXLinesHeights::InsertLineHeightInfo(const TXLineHeightInfo& info, long line)
{
	return InsertLine(info, nil, line);
}


// ROM 0x002394dc RemoveLines__14TXLinesHeightsFlT1P19TXFormatReflowLines
// `count` lines from `line` on taken out of their groups; the groups
// emptied go, and the two either side are joined when they now match.
void
TXLinesHeights::RemoveLines(long count, long line, TXFormatReflowLines* /*reflow*/)
{
	if (count == 0)
		return;
	fLastLine -= count;
	if (fLastLine < 0)
	{
		SetCount(0);
		fTotalHeight = 0;
		return;
	}
	long inGroup = line;
	long index;
	TXLineHeightGroup* group = LineToHeightGroup(&inGroup, &index);
	long taken;
	if (inGroup != 0)
	{
		taken = group->fCount - inGroup;
		if (count < taken)
			taken = count;
		group->fCount -= taken;
		index++;
	}
	else
		taken = 0;
	long emptied = 0;
	while (count != 0)
	{
		if (taken == 0)
		{
			if (count < group->fCount)
			{
				taken = count;
				group->fCount -= count;
			}
			else
			{
				taken = group->fCount;
				emptied++;
			}
		}
		count -= taken;
		fTotalHeight -= taken * group->fHeight;
		group++;
		taken = 0;
	}
	if (emptied != 0)
	{
		if (Concat(index - 1, index + emptied, nil))
			emptied++;
		Remove(index, emptied);
	}
}


// ROM 0x00239684 LineToHeightGroup__14TXLinesHeightsCFPlT1
TXLineHeightGroup*
TXLinesHeights::LineToHeightGroup(long* line, long* index) const
{
	TXLineHeightGroup* first = (TXLineHeightGroup*) GetElementPtr(0);
	TXLineHeightGroup* group = first;
	while (group->fCount <= *line)
	{
		*line -= group->fCount;
		group++;
	}
	if (index != nil)
		*index = (long) (group - first);
	return group;
}


// ROM 0x0023992c HeightToCountLines__14TXLinesHeightsCFRC17TXLineHeightGrouplPl
long
TXLinesHeights::HeightToCountLines(const TXLineHeightGroup& group, long line, long* pixels) const
{
	long lines = group.fCount - line;
	long height = group.fHeight;
	long used = height * lines;
	long have = *pixels;
	if (have < used)
	{
		lines = (have + height - 1) / height;
		used = height * lines;
	}
	*pixels = have - used;
	return lines;
}


// ROM 0x00239b4c PixelToLine__14TXLinesHeightsCFPllPP17TXLineHeightGroupT1
long
TXLinesHeights::PixelToLine(long* pixels, long line, TXLineHeightGroup** groupOut, long* lineInGroupOut) const
{
	long inGroup = line;
	TXLineHeightGroup* group = LineToHeightGroup(&inGroup, nil);
	long left = *pixels;
	long place;
	if (left == 0)
		place = inGroup;
	else
	{
		group--;
		do
		{
			group++;
			long lines = HeightToCountLines(*group, inGroup, &left);
			line += lines;
			place = lines + inGroup;
			inGroup = 0;
		}
		while (left > 0 && fLastLine >= line);
		if (left < 0)
		{
			// overshot: into the last line counted
			*pixels -= group->fHeight + left;
			place--;
			line--;
		}
		else if (left == 0)
		{
			if (place == group->fCount)
			{
				place = 0;
				group++;
			}
		}
		else if (fLastLine < line)
			*pixels -= left;
	}
	if (groupOut != nil)
	{
		*groupOut = group;
		*lineInGroupOut = place;
	}
	return line;
}


// ROM 0x00239c4c GetLinesHeight__14TXLinesHeightsCFlT1
long
TXLinesHeights::GetLinesHeight(long first, long last) const
{
	if (fLastLine < 0)
		return 0;
	if (first == 0 && fLastLine == last)
		return fTotalHeight;
	Boolean one = (first == last);
	long a = first;
	TXLineHeightGroup* g1 = LineToHeightGroup(&a, nil);
	long h1 = g1->fHeight;
	if (one)
		return h1;
	long b = last;
	TXLineHeightGroup* g2 = LineToHeightGroup(&b, nil);
	if (g1 == g2)
		return (b - a + 1) * h1;
	long total = h1 * (g1->fCount - a) + g2->fHeight * (b + 1);
	for (TXLineHeightGroup* g = g1 + 1; g < g2; g++)
		total += g->fCount * g->fHeight;
	return total;
}


// ROM 0x00239d30 GetLineHeightInfo__14TXLinesHeightsCFlP16TXLineHeightInfo
void
TXLinesHeights::GetLineHeightInfo(long line, TXLineHeightInfo* info) const
{
	TXLineHeightGroup* group = LineToHeightGroup(&line, nil);
	info->fHeight = group->fHeight;
	info->fAscent = group->fAscent;
}


// ROM 0x00239d68 EqualGroup__14TXLinesHeightsCFlRC17TXLineHeightGroup
Boolean
TXLinesHeights::EqualGroup(long index, const TXLineHeightGroup& group) const
{
	if (index < 0 || index >= fCount)
		return false;
	const TXLineHeightGroup* here = (const TXLineHeightGroup*) GetElementPtr(index);
	return group.fHeight == here->fHeight && group.fAscent == here->fAscent;
}


// ROM 0x00239dc0 EqualGroup__14TXLinesHeightsCFlT1
Boolean
TXLinesHeights::EqualGroup(long a, long b) const
{
	if (b < 0 || fCount <= b)
		return false;
	return EqualGroup(a, *(const TXLineHeightGroup*) GetElementPtr(b));
}


/*------------------------------------------------------------------------------
	T X P a r a g C t r l C h a r s
------------------------------------------------------------------------------*/

// ROM 0x00242a2c Define__16TXParagCtrlCharsFP7TXCharslT2
void
TXParagCtrlChars::Define(TXChars* chars, long start, long end)
{
	fStart = start;
	fCount = 0;
	fCurrent = 0;
	long left = end - start;
	if (left > 0x7fff)
		left = 0x7fff;
	long scanned = 0;
	while (left != 0)
	{
		UniChar found;
		long at = chars->GetCtrlCharOffset(start + scanned, left, &found);
		if (at < 0)
		{
			scanned += left;
			break;
		}
		long offset = scanned + at;
		scanned = offset + 1;
		fOffsets[fCount] = (short) offset;
		fChars[fCount] = found;
		fCount++;
		if (fCount == kTXParagCtrlCharsMax || found == 0x0a || found == 0x0d)
			break;
		left -= at + 1;
	}
	fEnd = start + scanned;
}




// ROM 0x00242b1c GetCurrCtrlOffset__16TXParagCtrlCharsFv
long
TXParagCtrlChars::GetCurrCtrlOffset(void)
{
	if (fCurrent >= fCount)
		return -1;
	return fStart + fOffsets[fCurrent];
}


// ROM 0x00242b44 GetCurrCtrlChar__16TXParagCtrlCharsFv
UniChar
TXParagCtrlChars::GetCurrCtrlChar(void)
{
	return fCurrent < fCount ? fChars[fCurrent] : 0;
}


// ROM 0x00242b6c Invalid__16TXParagCtrlCharsFv
void
TXParagCtrlChars::Invalid(void)
{
	fEnd = 0;
	fCurrent = 0;
}
