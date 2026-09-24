/*
	File:		text/TXArray.cpp

	Contains:	The text engine's arrays - see TXArray.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "TXArray.h"
#include "NewtonMemory.h"
#include "OSErrors.h"


// ROM 0x002343d0 __ct__15TXVirtualObjectFv
// ROM 0x00234404 __dt__15TXVirtualObjectFv
// The base of everything in the text engine: a vtable and nothing else.
TXVirtualObject::TXVirtualObject()
{ }

TXVirtualObject::~TXVirtualObject()
{ }


/*------------------------------------------------------------------------------
	T X A r r a y
------------------------------------------------------------------------------*/

// ROM 0x002306c8 __ct__7TXArrayFUci
// An empty array of that element size.  The handle is made with no room
// at all - the first Insert is what sizes it.  A chunk of nothing is
// taken as one, so the array always grows by at least one element and
// CheckUnusedCount always has a slack to compare against.
TXArray::TXArray(unsigned char elementSize, int chunk)
{
	fElementSize = elementSize;
	fData = NewHandle(0);
	fCount = 0;
	fPhysicalCount = 0;
	fLockCount = 0;
	fChunk = chunk;
	if (chunk < 1)
		fChunk = 1;
}


// ROM 0x00230740 __dt__7TXArrayFv
TXArray::~TXArray()
{
	if (fData != nil)
		DisposHandle(fData);
}


// ROM 0x00230f74 GetElementPtr__7TXArrayCFl
// The element's address.  The handle is not locked, so the answer is
// good only until the next allocation - which is why everything that
// walks the array locks it first.
void*
TXArray::GetElementPtr(long index) const
{
	return *fData + fElementSize * index;
}


// ROM 0x00230f8c GetLastElementPtr__7TXArrayCFv
void*
TXArray::GetLastElementPtr(void) const
{
	return *fData + fElementSize * (fCount - 1);
}


// ROM 0x00230f98 Stuff__7TXArrayFlPCvT1
// `count` elements written into the array at the index.
void
TXArray::Stuff(long index, const void* data, long count)
{
	BlockMove(data, GetElementPtr(index), count * fElementSize);
}


// ROM 0x00230fcc CopyTo__7TXArrayCFlT1Pv
void
TXArray::CopyTo(long index, long count, void* out) const
{
	BlockMove(GetElementPtr(index), out, count * fElementSize);
}


// ROM 0x00230874 SetPhysicalCount__7TXArrayFl
// The handle resized to hold exactly that many elements.  The count is
// only believed when the resize worked, so a failure leaves the array as
// it was.
NewtonErr
TXArray::SetPhysicalCount(long count)
{
	if (fPhysicalCount == count)
		return noErr;
	SetHandleSize(fData, count * fElementSize);
	NewtonErr err = MemError();
	if (err == noErr)
		fPhysicalCount = count;
	return err;
}


// ROM 0x002310c8 CheckUnusedCount__7TXArrayFv
// More than a chunk of spare room is given back.
void
TXArray::CheckUnusedCount(void)
{
	if (fPhysicalCount > fCount + fChunk)
		SetPhysicalCount(fCount + fChunk);
}


// ROM 0x00230ffc Insert__7TXArrayFPCvlT2
// `count` elements opened at `at` and `data` copied into them; `at` of
// -1 means the end and a nil `data` leaves them as they were.  The array
// grows by a whole chunk when the chunk is bigger than what is being put
// in, and falls back to exactly what is needed when that will not fit.
// ==> the first of the new elements, or nil when there was no memory.
void*
TXArray::Insert(const void* data, long count, long at)
{
	long was = fCount;
	long wanted = was + count;
	if (fPhysicalCount < wanted)
	{
		long target = fChunk > count ? fChunk + was : wanted;
		if (SetPhysicalCount(target) != noErr)
		{
			if (target == wanted)
				return nil;
			if (SetPhysicalCount(wanted) != noErr)
				return nil;
		}
	}
	if (at < 0)
		at = was;
	fCount = wanted;
	void* p = GetElementPtr(at);
	if (at != was)
		Stuff(at + count, p, was - at);
	if (data != nil)
		Stuff(at, data, count);
	return p;
}


// ROM 0x0023078c Remove__7TXArrayFlT1
// `count` elements taken out at `at`, what follows moved down; ==> the
// count that is left.
long
TXArray::Remove(long at, long count)
{
	long after = fCount - (at + count);
	if (after > 0)
		Stuff(at, GetElementPtr(at + count), after);
	fCount -= count;
	CheckUnusedCount();
	return fCount;
}


// ROM 0x002307f0 Replace__7TXArrayFlT1PCvT1
// `count` elements at `at` replaced by `newCount` of `data`: the array
// is opened or closed by the difference and the new elements written in.
NewtonErr
TXArray::Replace(long at, long count, const void* data, long newCount)
{
	long difference = newCount - count;
	if (difference > 0)
	{
		if (Insert(nil, difference, at) == nil)
			return kError_No_Memory;
	}
	else if (difference < 0)
		Remove(at, -difference);
	if (data != nil)
		Stuff(at, data, newCount);
	return noErr;
}


// ROM 0x002308ec SetCount__7TXArrayFl
// The element count set.  Growing past what the handle holds goes
// through Insert, so it grows by chunks like everything else; shrinking
// gives the spare room back.
NewtonErr
TXArray::SetCount(long count)
{
	if (fPhysicalCount < count)
	{
		if (Insert(nil, count - fCount, -1) == nil)
			return kError_No_Memory;
		return noErr;
	}
	fCount = count;
	CheckUnusedCount();
	return noErr;
}


// ROM 0x002308b8 Reserve__7TXArrayFl
// Room made for that many more elements without the count changing.
NewtonErr
TXArray::Reserve(long count)
{
	if (count <= 0)
		return noErr;
	long was = fCount;
	NewtonErr err = SetCount(was + count);
	fCount = was;
	return err;
}


// ROM 0x0023093c Compact__7TXArrayFv
// Every spare element given back.
NewtonErr
TXArray::Compact(void)
{
	if (fPhysicalCount == fCount)
		return noErr;
	SetHandleSize(fData, fCount * fElementSize);
	NewtonErr err = MemError();
	if (err == noErr)
		fPhysicalCount = fCount;
	return err;
}


// ROM 0x00230ae0 Lock__7TXArrayFUc
// The handle locked while the elements are walked.  Locks nest: only
// the first one touches the handle, and `moveHigh` asks for it to be
// moved out of the way of the heap first.  ==> the elements.
void*
TXArray::Lock(Boolean moveHigh)
{
	char was = fLockCount;
	fLockCount = (char) (was + 1);
	if (was == 0)
	{
		if (moveHigh)
			MoveHHi(fData);
		HLock(fData);
	}
	return *fData;
}


// ROM 0x00230d60 Unlock__7TXArrayFv
void
TXArray::Unlock(void)
{
	if (fLockCount == 0)
		return;
	fLockCount--;
	if (fLockCount == 0)
		HUnlock(fData);
}


/*------------------------------------------------------------------------------
	T X L o n g T a g A r r a y
------------------------------------------------------------------------------*/

// ROM 0x00230944 __ct__14TXLongTagArrayFUci
TXLongTagArray::TXLongTagArray(unsigned char elementSize, int chunk)
	: TXArray(elementSize, chunk)
{ }


// ROM 0x00230994 __dt__14TXLongTagArrayFv
TXLongTagArray::~TXLongTagArray()
{ }


// ROM 0x002309d4 Search__14TXLongTagArrayCFlPl
// The element whose long is `tag`, by binary search; when there is none,
// the first element past it.  `found` comes back as the long that was
// landed on, so the caller can tell an exact hit from a near miss.
//
// The two ends are tried first: a tag at or below the first element
// answers 0, and one past the last answers the count (one past the end).
long
TXLongTagArray::Search(long tag, long* found) const
{
	long low = 0;
	long high = fCount - 1;
	if (high < 0)
	{
		*found = -1;
		return 0;
	}
	long value = *(long*) GetElementPtr(0);
	*found = value;
	if (value >= tag)
		return 0;
	value = *(long*) GetLastElementPtr();
	*found = value;
	if (value < tag)
		return fCount;
	long answer = 0;
	do
	{
		long middle = (low + high) >> 1;
		long here = *(long*) GetElementPtr(middle);
		if (here == tag)
		{
			*found = tag;
			return middle;
		}
		if (here - tag < 1)
			low = middle + 1;
		else
		{
			high = middle - 1;
			*found = here;
			answer = middle;
		}
	}
	while (low <= high);
	return answer;
}


// ROM 0x00230a94 SearchBigger__14TXLongTagArrayCFl
// The first element whose long is greater than `tag`, bounded by the
// last element.
long
TXLongTagArray::SearchBigger(long tag) const
{
	long landed = 0;
	long index = Search(tag, &landed);
	long last = fCount - 1;
	if (index > last)
		return last;
	if (landed <= tag && index < last)
		return index + 1;
	return index;
}


// ROM 0x00230b2c AddToElements__14TXLongTagArrayFlN21
// `delta` added to the long of `count` elements from `at`; a count of -1
// means the rest of the array.  (The ROM has a copy of the loop for
// four-byte elements, where the walk is a pointer increment.)
void
TXLongTagArray::AddToElements(long at, long delta, long count)
{
	if (delta == 0)
		return;
	if (count < 0)
	{
		count = fCount - at;
		if (count < 1)
			return;
	}
	char* p = (char*) GetElementPtr(at);
	for (long i = 0; i < count; i++)
	{
		*(long*) p += delta;
		p += fElementSize;
	}
}


/*------------------------------------------------------------------------------
	T X R a n g e s
------------------------------------------------------------------------------*/

// ROM 0x00230b8c __ct__8TXRangesFUci
TXRanges::TXRanges(unsigned char elementSize, int chunk)
	: TXLongTagArray(elementSize, chunk)
{ }


// ROM 0x00230bdc FreeData__8TXRangesFUc
// Every range dropped; `compact` gives the memory back as well.
NewtonErr
TXRanges::FreeData(Boolean compact)
{
	NewtonErr err = SetCount(0);
	if (!compact)
		return err;
	return Compact();
}


// ROM 0x00230c0c GetRangeEnd__8TXRangesCFl
// Where the range ends - which is the element itself.
TXOffset
TXRanges::GetRangeEnd(long index) const
{
	if (index < 0)
		return 0;
	return *(TXOffset*) GetElementPtr(index);
}


// ROM 0x00230c30 GetRangeStart__8TXRangesCFl
// Where it starts - which is where the one before it ends, the first
// range starting at 0.
TXOffset
TXRanges::GetRangeStart(long index) const
{
	if (index <= 0)
		return 0;
	return *(TXOffset*) GetElementPtr(index - 1);
}


// ROM 0x00230c58 GetRangeLen__8TXRangesCFl
long
TXRanges::GetRangeLen(long index) const
{
	char* p = (char*) GetElementPtr(index);
	long end = *(long*) p;
	if (index == 0)
		return end;
	return end - *(long*) (p - fElementSize);
}


// ROM 0x00230c8c GetRangeBounds__8TXRangesCFlP12TXOffsetPair
void
TXRanges::GetRangeBounds(long index, TXOffsetPair* bounds) const
{
	char* p = (char*) GetElementPtr(index);
	bounds->fEnd = *(long*) p;
	bounds->fStart = index == 0 ? 0 : *(long*) (p - fElementSize);
}


// ROM 0x00230cc8 SetRangeEnd__8TXRangesFlT1
void
TXRanges::SetRangeEnd(long index, TXOffset end)
{
	*(TXOffset*) GetElementPtr(index) = end;
}


// ROM 0x00230ce4 AddToRangeEnd__8TXRangesFlT1
void
TXRanges::AddToRangeEnd(long index, long delta)
{
	*(TXOffset*) GetElementPtr(index) += delta;
}


// ROM 0x00230d08 IsRangeStart__8TXRangesCFlT1
// Whether the offset is exactly where the range begins.  An index of -1
// asks about whichever range the offset falls in, which can only be true
// when the offset is a boundary.
Boolean
TXRanges::IsRangeStart(TXOffset offset, long index) const
{
	if (index < 0)
		index = OffsetToRangeIndex(offset, false);
	return GetRangeStart(index) == offset;
}


// ROM 0x00230d84 GetLastRangeEnd__8TXRangesCFv
// What the whole array covers.
TXOffset
TXRanges::GetLastRangeEnd(void) const
{
	if (fCount == 0)
		return 0;
	return *(TXOffset*) GetElementPtr(fCount - 1);
}


// ROM 0x00230db0 OffsetToRangeIndex__8TXRangesCF8TXOffset
// The range the offset falls in.  An offset that is exactly a boundary
// belongs to the range that *starts* there; `atStart` asks for the one
// that ends there instead, which is what a caret at the end of a run
// wants.  An array of one range or none answers its only index.
long
TXRanges::OffsetToRangeIndex(TXOffset offset, Boolean atStart) const
{
	long last = fCount - 1;
	if (last < 1)
		return last;
	long index = SearchBigger(offset);
	if (atStart && index > 0 && IsRangeStart(offset, index))
		index--;
	return index;
}


// ROM 0x00230e14 SectRanges__8TXRangesCFlT1P12TXSectRanges
// What a stretch of text covers: the range it starts in and how far into
// it, the range it ends in and how much of that range is left over, and
// the run of ranges between them that are covered end to end - which is
// what an edit needs to know to replace whole runs and trim the two at
// the edges.
long
TXRanges::SectRanges(TXOffset start, long length, TXSectRanges* sect) const
{
	TXOffset end = start + length;
	long index = OffsetToRangeIndex(start, false);
	sect->fFirstIndex = index;
	if (index < 0)
	{
		sect->fFirstIndex = 0;
		sect->fStartOffset = 0;
		sect->fFirstLen = 0;
		sect->fWholeIndex = 0;
		sect->fWholeCount = 0;
		sect->fLastIndex = 0;
		sect->fEndRemainder = 0;
		sect->fLastLen = 0;
		return 0;
	}
	sect->fStartOffset = start - GetRangeStart(index);
	TXOffset firstEnd = GetRangeEnd(sect->fFirstIndex);
	sect->fFirstLen = (firstEnd < end ? firstEnd : end) - start;
	if (firstEnd < end)
		sect->fLastIndex = OffsetToRangeIndex(end, true);
	else
		sect->fLastIndex = sect->fFirstIndex;
	sect->fEndRemainder = GetRangeEnd(sect->fLastIndex) - end;
	sect->fLastLen = GetRangeLen(sect->fLastIndex) - sect->fEndRemainder;
	long first = sect->fFirstIndex;
	long spanned = (sect->fLastIndex - first) + 1;
	sect->fWholeCount = spanned;
	sect->fWholeIndex = first;
	// a stretch that does not start on a boundary, or does not run to
	// the end of the range it starts in, leaves that range partly
	// covered - so the whole ones begin at the next
	if (sect->fStartOffset != 0 || firstEnd > end)
	{
		sect->fWholeCount = spanned - 1;
		sect->fWholeIndex = first + 1;
	}
	if (sect->fEndRemainder != 0)
		sect->fWholeCount--;
	if (sect->fWholeCount < 0)
		sect->fWholeCount = 0;
	// the ROM answers the ranges the stretch *spans*, before either of
	// those two corrections - not the count it has just worked out
	return spanned;
}
