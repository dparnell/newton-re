/*
	File:		views/StyleRuns.cpp

	Contains:	Style runs.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "StyleRuns.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "NewtonExceptions.h"
#include "ROMConstants.h"

const long kStylesErrReadOnly = -8009;			// the ROM's 0xffffe0b7 (ErrorNotify -8009, then evt.ex)


// ROM 0x0017c8c4 TotalRunLength__FRC6RefVar
// The characters the runs cover: the sum of the lengths (the even slots).
long
TotalRunLength(RefArg styles)
{
	long total = 0;
	for (long slot = 0, count = Length(styles); slot < count; slot += 2)
		total += RINT(GetArraySlotRef(styles, slot));
	return total;
}


// ROM 0x0012aa28 RunsInsert__FRC6RefVarlT2
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
		long runLength = RINT(GetArraySlotRef(styles, slot));
		end += runLength;
		if (offset < end)
		{
			SetArraySlotRef(styles, slot, MAKEINT(runLength + count));
			return;
		}
	}
	if (length < 2)
		return;
	long runLength = RINT(GetArraySlotRef(styles, length - 2));
	SetArraySlotRef(styles, length - 2, MAKEINT(runLength + count));
}


// ROM 0x0012a938 RunsDelete__FRC6RefVarlT2
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
		long runLength = RINT(GetArraySlotRef(styles, slot));
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


// ROM 0x0017c92c CorrectAnyBadStyleRuns__F6RefVarl
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


// ROM 0x0017c9f4 SaveStylesAndTabStopsArrays__FRC6RefVarT1
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
