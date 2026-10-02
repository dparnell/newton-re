/*
	File:		views/StyleRuns.cpp

	Contains:	Style runs.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "StyleRuns.h"
#include "RichString.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "NewtonExceptions.h"
#include "ROMConstants.h"

const long kStylesErrReadOnly = -8009;			// the ROM's 0xffffe0b7 (ErrorNotify -8009, then evt.ex)


// ROM 0x0017a894 TotalRunLength__FRC6RefVar
// The characters the runs cover: the sum of the lengths (the even slots).
long
TotalRunLength(RefArg styles)
{
	long total = 0;
	for (long slot = 0, count = Length(styles); slot < count; slot += 2)
		total += RINT(GetArraySlotRef(styles, slot));
	return total;
}


// ROM 0x00128fcc RunsInsert__FRC6RefVarlT2
// Count characters inserted at the offset: the run the offset falls in
// grows by them - the last run when the offset is at the end.
void
RunsInsert(RefArg styles, long offset, long count)
{
	if (count == 0)
		return;
	long length = Length(styles);
	long end = 0;
	long slot = 0;
	for ( ; slot < length; slot += 2)
	{
		Long runLength = RINT(GetArraySlotRef(styles, slot));
		end += runLength;
		if (offset < end)
		{
			SetArraySlotRef(styles, slot, MAKEINT(runLength + count));
			return;
		}
	}
	if (length < 2)
		return;
	Long runLength = RINT(GetArraySlotRef(styles, length - 2));
	SetArraySlotRef(styles, length - 2, MAKEINT(runLength + count));
}


// ROM 0x00128edc RunsDelete__FRC6RefVarlT2
// Count characters deleted from the offset: every run the range touches
// loses the characters of it within the range; a run losing all of its
// characters is removed with its style.
void
RunsDelete(RefArg styles, long offset, long count)
{
	if (count == 0)
		return;
	long length = Length(styles);
	long rangeEnd = offset + count;
	long start = 0;
	for (long slot = 0; slot < length; slot += 2)
	{
		Long runLength = RINT(GetArraySlotRef(styles, slot));
		long end = start + runLength;
		long lost = (end < rangeEnd ? end : rangeEnd) - (start > offset ? start : offset);
		if (lost > 0)
		{
			if (lost == runLength)
			{
				ArrayRemoveCount(styles, slot, 2);
				slot -= 2;
				length -= 2;
			}
			else
				SetArraySlotRef(styles, slot, MAKEINT(runLength - lost));
		}
		start = end;
		if (end >= rangeEnd)
			break;
	}
}


// ROM 0x0017a8fc CorrectAnyBadStyleRuns__F6RefVarl
// The runs made to cover the text: the last run grows to the text's
// length, or the runs past its end are cut; read-only runs cannot be
// corrected (ErrorNotify -8009, evt.ex).
void
CorrectAnyBadStyleRuns(RefArg styles, long textLength)
{
	if (ISNIL(styles) || Length(styles) <= 0)
		return;
	long total = TotalRunLength(styles);
	if (total >= textLength)
	{
		if (total > textLength)
		{
			if (ObjectFlags(styles) & kObjReadOnly)
				Throw((ExceptionName) "evt.ex", (void*) kStylesErrReadOnly, nil);
			RunsDelete(styles, textLength, total - textLength);
		}
		return;
	}
	if (ObjectFlags(styles) & kObjReadOnly)
		Throw((ExceptionName) "evt.ex", (void*) kStylesErrReadOnly, nil);
	RunsInsert(styles, total, textLength - total);
}


// ROM 0x0017a9c4 SaveStylesAndTabStopsArrays__FRC6RefVarT1
// A canonical styles frame: {styles: the runs when there are any, tabs:
// the tab stops when there are any} (a clone of the ROM's canonicalStyles).
Ref
SaveStylesAndTabStopsArrays(RefArg styles, RefArg tabs)
{
	RefVar frame(Clone(Rcanonicalstyles));
	if (NOTNIL(styles) && Length(styles) > 0)
		SetFrameSlot(frame, RSSYMstyles, styles);
	if (NOTNIL(tabs))
		SetFrameSlot(frame, RSSYMtabs, tabs);
	return frame;
}


// ROM 0x0017d8ac GetStyleAtOffset__FRC6RefVarlPlT3
// The style of the character at the offset: a single spec is it; in an
// array the run whose span holds the offset (the last for an offset past
// the runs).  run and offsetInRun say where (0 and the offset for a
// single spec).
Ref
GetStyleAtOffset(RefArg styles, long offset, long* run, long* offsetInRun)
{
	RefVar style;
	long index = 0;
	long inRun = offset;
	if (!IsArray(styles))
		style = styles;
	else
	{
		long count = Length(styles) / 2;
		long covered = 0;
		long runLength = 0;
		long i = 0;
		for ( ; i < count; i++)
		{
			runLength = RINT(GetArraySlotRef(styles, i * 2));
			covered += runLength;
			if (offset < covered)
			{
				style = GetArraySlotRef(styles, i * 2 + 1);
				inRun = runLength - (covered - offset);
				index = i;
				break;
			}
		}
		if (i == count)
		{
			style = GetArraySlotRef(styles, count * 2 - 1);
			index = count - 1;
			inRun = runLength;
		}
	}
	if (run != nil)
		*run = index;
	if (offsetInRun != nil)
		*offsetInRun = inRun;
	return style;
}


// ROM 0x0017dd38 CountStylesForLength__FRC6RefVarlT2
// How many runs from the run given it takes to cover the length (at
// least one; 1 for a single spec).
long
CountStylesForLength(RefArg styles, long run, long length)
{
	if (!IsArray(styles))
		return 1;
	long count = Length(styles) / 2;
	long covered = 0;
	long i = run;
	while (i < count)
	{
		covered += RINT(GetArraySlotRef(styles, i * 2));
		if (covered >= length)
			break;
		i++;
	}
	if (i == count)
		i--;
	return i - run + 1;
}


// ROM 0x0017d6c0 ExtractRichStringFromParaSlots__FRC6RefVarT1UlT3
// A piece of a paragraph as a string of its own: `count` characters from
// `start` of its text, kept rich when any of the styles covering them is
// ink (MakeRichString: each ink word kept with its character).  A start
// or a count past the end of the text is cut back to it.
Ref
ExtractRichStringFromParaSlots(RefArg text, RefArg styles, ULong start, ULong count)
{
	TRichString rich(text);
	ULong length = (ULong) rich.Length();
	if (length < start)
		start = length;
	if (length < start + count)
		count = length - start;
	RefVar result(AllocateBinary(RefVar(RSSYMstring), (long) count * 2 + 2));
	// (both pointers taken after the allocation: the heap may have moved
	//  the text while the string was being made)
	UniChar* from = (UniChar*) BinaryData(text);
	UniChar* to = (UniChar*) BinaryData(result);
	BlockMove(from + start, to, (Size) count * 2);
	to[count] = 0;
	if (!IsArray(styles) || Length(styles) < 1)
		return result;
	RefVar runs(GetStylesOfRange(styles, (long) start, (long) count, false));
	long runCount = Length(runs);
	for (long i = 1; i < runCount; i += 2)
	{
		if (IsInkWord(RefVar(GetArraySlotRef(runs, i))))
			return MakeRichString(result, runs, false);
	}
	return result;
}


// ROM 0x0017da64 GetStylesOfRange__FRC6RefVarlT2Uc
// The style runs covering the range as a new runs array (a single spec
// makes one run of the length; the specs cloned when asked): the first
// run cut to what lies past the offset, the last to what fits.
Ref
GetStylesOfRange(RefArg styles, long offset, long length, Boolean clone)
{
	RefVar result;
	if (!IsArray(styles))
	{
		result = MakeArray(2);
		SetArraySlotRef(result, 0, MAKEINT(length));
		if (clone)
			SetArraySlot(result, 1, RefVar(Clone(styles)));
		else
			SetArraySlot(result, 1, styles);
		return result;
	}
	long firstRun, inRun;
	GetStyleAtOffset(styles, offset, &firstRun, &inRun);
	Long firstLength = RINT(GetArraySlotRef(styles, firstRun * 2)) - inRun;
	if (length <= firstLength)
		firstLength = length;
	long runs = CountStylesForLength(styles, firstRun, length + inRun);
	result = MakeArray(runs * 2);
	long covered = 0;
	for (long i = 0, run = firstRun; i < runs; i++, run++)
	{
		RefVar spec(GetArraySlotRef(styles, run * 2 + 1));
		if (clone)
			SetArraySlot(result, i * 2 + 1, RefVar(Clone(spec)));
		else
			SetArraySlot(result, i * 2 + 1, spec);
		long runLength;
		if (run == firstRun)
		{
			runLength = firstLength;
			covered += firstLength;
		}
		else
		{
			runLength = RINT(GetArraySlotRef(styles, run * 2));
			covered += runLength;
			if (covered > length)
				runLength -= covered - length;
		}
		SetArraySlotRef(result, i * 2, MAKEINT(runLength));
	}
	return result;
}


// ROM 0x00179ae0 SetStyleOfRange__FRC6RefVarT1ClT3
// The characters from start to end given one style: runs wholly inside
// go, a run straddling the start is cut short (and one straddling the
// end split, its tail kept), and a run of the range's length is put in
// their place.  Runs covering nothing get the one run.
void
SetStyleOfRange(RefArg styles, RefArg style, long start, long end)
{
	if (start == end)
		return;
	long count = Length(styles);
	if (TotalRunLength(styles) == 0)
	{
		SetLength(styles, 2);
		SetArraySlotRef(styles, 0, MAKEINT(end - start));
		SetArraySlot(styles, 1, style);
		return;
	}
	long first = -1;			// where the new run goes
	long covered = 0;
	for (long i = 0; i < count; i += 2)
	{
		Long runLength = RINT(GetArraySlotRef(styles, i));
		long runEnd = covered + runLength;
		if (first < 0)
		{
			if (start == covered)
				first = i;
			else if (start < runEnd)
			{
				SetArraySlotRef(styles, i, MAKEINT(start - covered));		// the run cut before the range
				first = i + 2;
			}
		}
		if (end <= runEnd)
		{
			long last;
			if (covered < start)
			{
				// the range lies within this run: its tail after the range
				if (end < runEnd)
				{
					ArrayGrowAt(styles, i + 2, 2);
					SetArraySlotRef(styles, i + 2, MAKEINT(runEnd - end));
					SetArraySlot(styles, i + 3, RefVar(GetArraySlotRef(styles, i + 1)));
				}
			}
			else
			{
				if (end < runEnd)
				{
					SetArraySlotRef(styles, i, MAKEINT(runLength - (end - covered)));	// the run's tail past the range
					last = i - 1;
				}
				else
					last = i + 1;
				if (first <= last)
					ArrayRemoveCount(styles, first, last - first + 1);
			}
			ArrayGrowAt(styles, first, 2);
			SetArraySlotRef(styles, first, MAKEINT(end - start));
			SetArraySlot(styles, first + 1, style);
			return;
		}
		covered = runEnd;
	}
}


// ROM 0x0017ab64 CompactStyleRuns__FRC6RefVar
// Neighbouring runs of the same style (EQRef) - and empty runs - merged.
void
CompactStyleRuns(RefArg styles)
{
	long count = Length(styles);
	for (long i = 0; i < count; )
	{
		Long runLength = RINT(GetArraySlotRef(styles, i));
		Ref spec = GetArraySlotRef(styles, i + 1);
		long merged = 0;
		long next = i + 2;
		long j = next;
		for ( ; j < count; j += 2)
		{
			Long otherLength = RINT(GetArraySlotRef(styles, j));
			if (!EQRef(spec, GetArraySlotRef(styles, j + 1)) && otherLength > 0)
				break;
			merged += otherLength;
		}
		if (next < j)
		{
			SetArraySlotRef(styles, i, MAKEINT(runLength + merged));
			ArrayRemoveCount(styles, next, j - next);
			count -= j - next;
		}
		i = next;
	}
}


// ROM 0x0017aa58 ExtractStylesArray__FRC6RefVar
Ref
ExtractStylesArray(RefArg frame)
{
	if (ISNIL(frame))
		return NILREF;
	if (IsArray(frame))
		return frame;
	return GetFrameSlotRef(frame, RSSYMstyles);
}


// ROM 0x0017aab4 ExtractTabStopsArray__FRC6RefVar
Ref
ExtractTabStopsArray(RefArg frame)
{
	if (ISNIL(frame) || IsArray(frame))
		return NILREF;
	return GetFrameSlotRef(frame, RSSYMtabs);
}


// ROM 0x0017ab0c ExtractCorrectInfo__FRC6RefVar
Ref
ExtractCorrectInfo(RefArg frame)
{
	if (ISNIL(frame) || !IsFrame(frame))
		return NILREF;
	return GetFrameSlotRef(frame, RSSYMcorrectinfo);
}


// ROM 0x0017a1c0 StyleArrayContainsInk__FRC6RefVar
// Whether a runs array has any ink word among its style specs - which is
// how a piece of text says it has writing in it without being opened up.
Boolean
StyleArrayContainsInk(RefArg styles)
{
	if (!IsArray(styles))
		return false;
	long runs = Length(styles) / 2;
	for (long i = 0; i < runs; i++)
		if (IsInkWord(RefVar(GetArraySlotRef(styles, i * 2 + 1))))
			return true;
	return false;
}
