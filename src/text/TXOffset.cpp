/*
	File:		text/TXOffset.cpp

	Contains:	A place in the text, and a stretch of it - see TXOffset.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "TXOffset.h"


// ROM 0x00233fd0 __eq__8TXOffsetCFRC8TXOffset
// The same offset, told apart the same way on a boundary.
bool
TXOffsetPos::operator==(const TXOffsetPos& other) const
{
	return fOffset == other.fOffset && fAtStart == other.fAtStart;
}


// ROM 0x00233ff8 __ct__13TXOffsetRangeFRC8TXOffsetT1
TXOffsetRange::TXOffsetRange(const TXOffsetPos& start, const TXOffsetPos& end)
{
	fStart = start;
	fEnd = end;
}


// ROM 0x0023403c __ct__13TXOffsetRangeFlT1UcT3
TXOffsetRange::TXOffsetRange(TXOffset start, TXOffset end, Boolean startAtStart, Boolean endAtStart)
{
	fStart.fOffset = start;
	fStart.fAtStart = startAtStart;
	fEnd.fOffset = end;
	fEnd.fAtStart = endAtStart;
}


// ROM 0x0023408c Set__13TXOffsetRangeFlT1UcT3
void
TXOffsetRange::Set(TXOffset start, TXOffset end, Boolean startAtStart, Boolean endAtStart)
{
	fStart.fOffset = start;
	fStart.fAtStart = startAtStart;
	fEnd.fOffset = end;
	fEnd.fAtStart = endAtStart;
}


// ROM 0x002340a8 __eq__13TXOffsetRangeCFRC13TXOffsetRange
bool
TXOffsetRange::operator==(const TXOffsetRange& other) const
{
	return fStart == other.fStart && fEnd == other.fEnd;
}


// ROM 0x002340ec Offset__13TXOffsetRangeFl
void
TXOffsetRange::Offset(long delta)
{
	fStart.fOffset += delta;
	fEnd.fOffset += delta;
}


// ROM 0x0023411c CheckBounds__13TXOffsetRangeFv
// The ends put in order.  ROM QUIRK kept: only the offsets are swapped -
// each end keeps its own boundary flag, so a range that was the wrong
// way round comes out with its flags crossed.
void
TXOffsetRange::CheckBounds(void)
{
	TXOffset end = fEnd.fOffset;
	if (end < fStart.fOffset)
	{
		fEnd.fOffset = fStart.fOffset;
		fStart.fOffset = end;
	}
}


// ROM 0x00234108 TXSwapLong__FPlT1
void
TXSwapLong(long* a, long* b)
{
	long t = *a;
	*a = *b;
	*b = t;
}
