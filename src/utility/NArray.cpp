/*
	File:		utility/NArray.cpp

	Contains:	NArray, NSortedArray, NComparator, NBlockComparator and
				NIterator (NArray.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	The ROM's malloc/free/realloc here are NewPtr/DisposPtr/ReallocPtr.
*/

#include "NArray.h"
#include "NewtonMemory.h"
#include "UCErrors.h"

#include <string.h>


/* -------------------------------------------------------------------------------
	NComparator
------------------------------------------------------------------------------- */

// ROM 0x00128648 __ct__11NComparatorFv
NComparator::NComparator()
{ }


// ROM 0x0012867c __dt__11NComparatorFv
NComparator::~NComparator()
{ }


// ROM 0x00128694 KeyOf__11NComparatorCFPCv
const void*
NComparator::KeyOf(const void* element) const
{
	return element;
}


// ROM 0x0012869c CompareKeys__11NComparatorCFPCvT1
// The keys themselves, as unsigned numbers.
int
NComparator::CompareKeys(const void* key1, const void* key2) const
{
	if ((uintptr_t) key1 < (uintptr_t) key2)
		return -1;
	if ((uintptr_t) key1 > (uintptr_t) key2)
		return 1;
	return 0;
}


/* -------------------------------------------------------------------------------
	NBlockComparator
------------------------------------------------------------------------------- */

// ROM 0x001285b0 __ct__16NBlockComparatorFl
NBlockComparator::NBlockComparator(long size)
{
	fSize = size;
}


// ROM 0x001285f4 __dt__16NBlockComparatorFv
NBlockComparator::~NBlockComparator()
{ }


// ROM 0x00128634 CompareKeys__16NBlockComparatorCFPCvT1
int
NBlockComparator::CompareKeys(const void* key1, const void* key2) const
{
	return memcmp(key1, key2, fSize);
}


/* -------------------------------------------------------------------------------
	NArray
------------------------------------------------------------------------------- */

// ROM 0x0012583c __ct__6NArrayFv
NArray::NArray()
{
	fElementSize = 4;
	fChunkSize = 4;
	fCount = 0;
	fPhysicalCount = 0;
	fArray = nil;
	fIterators = nil;
	fShrink = true;
}


// ROM 0x00125898 __dt__6NArrayFv
// The iterators are told their array is gone.
NArray::~NArray()
{
	if (fIterators != nil)
		fIterators->DeleteArray();
	if (fArray != nil)
		DisposPtr((Ptr) fArray);
}


// ROM 0x001259b0 Init__6NArrayFlN21Uc
NewtonErr
NArray::Init(long elementSize, long chunkSize, long physicalCount, Boolean shrink)
{
	if (fElementSize <= 0 || fChunkSize <= 0 || physicalCount <= 0)		// (sic: the members, not the arguments)
		return eRangeCheck;
	fElementSize = elementSize;
	fChunkSize = chunkSize;
	fShrink = shrink;
	return SetPhysicalCount(physicalCount);
}


// ROM 0x001259f8 At__6NArrayCFl
void*
NArray::At(long index) const
{
	if (fCount <= 0 || index < 0 || index >= fCount)
		return nil;
	return (char*) fArray + index * fElementSize;
}


// ROM 0x001258e4 Contains__6NArrayCFPCv
// A linear search comparing whole elements.
long
NArray::Contains(const void* element) const
{
	NBlockComparator comparator(fElementSize);
	long found = -1;
	for (long i = 0; i < fCount; i++)
	{
		if (comparator.CompareKeys(comparator.KeyOf(At(i)), comparator.KeyOf(element)) == 0)
		{
			found = i;
			break;
		}
	}
	return found;
}


// ROM 0x001259a8 Where__6NArrayCFPCv
long
NArray::Where(const void* /*element*/) const
{
	return fCount;
}


// ROM 0x00125a20 InsertElements__6NArrayFlT1PCv
// Inserts count elements (copied from elements) before index, or at the end
// when index is past it; the iterators are told.
NewtonErr
NArray::InsertElements(long index, long count, const void* elements)
{
	if (count == 0)
		return noErr;
	if (index < 0 || count < 0)
		return eRangeCheck;
	if (index > fCount)
		index = fCount;
	NewtonErr err = SetPhysicalCount(fCount + count);
	if (err == noErr)
	{
		char* at = (char*) fArray + index * fElementSize;
		char* end = (char*) fArray + fCount * fElementSize;
		if (fCount > index)
			memmove(at + count * fElementSize, at, end - at);
		memcpy(at, elements, count * fElementSize);
		fCount += count;
		if (fIterators != nil)
			fIterators->InsertElements(index, count);
	}
	return err;
}


// ROM 0x00125aec RemoveElements__6NArrayFlT1
NewtonErr
NArray::RemoveElements(long index, long count)
{
	if (count == 0)
		return noErr;
	if (index < 0 || count < 0 || index >= fCount || index + count > fCount)
		return eRangeCheck;
	char* at = (char*) fArray + index * fElementSize;
	char* from = (char*) fArray + (index + count) * fElementSize;
	char* end = (char*) fArray + fCount * fElementSize;
	if (from < end)
		memmove(at, from, end - from);
	NewtonErr err = SetCount(fCount - count);
	if (err == noErr && fIterators != nil)
		fIterators->RemoveElements(index, count);
	return err;
}


// ROM 0x00125b9c SetCount__6NArrayFl
NewtonErr
NArray::SetCount(long count)
{
	if (count == fCount)
		return noErr;
	NewtonErr err = SetPhysicalCount(count);
	if (err == noErr)
		fCount = count;
	return err;
}


// ROM 0x00125bd8 SetPhysicalCount__6NArrayFl
// Makes the block hold count elements, rounded up to whole chunks; it only
// shrinks when fShrink is set and a whole chunk would be freed.
NewtonErr
NArray::SetPhysicalCount(long count)
{
	NewtonErr err = noErr;
	if (count == 0)
	{
		if (fShrink)
		{
			DisposPtr((Ptr) fArray);
			err = MemError();
			fArray = nil;
			fPhysicalCount = 0;
		}
	}
	else if (fPhysicalCount < count || (fShrink && fPhysicalCount - count >= fChunkSize))
	{
		long rounded = count + fChunkSize - 1;
		rounded -= rounded % fChunkSize;
		if (fPhysicalCount < rounded || (fShrink && fPhysicalCount > rounded))
		{
			void* block = ReallocPtr((Ptr) fArray, rounded * fElementSize);
			if (block == nil)
				err = MemError();
			else
			{
				fPhysicalCount = rounded;
				fArray = block;
			}
		}
	}
	return err;
}


/* -------------------------------------------------------------------------------
	NSortedArray
------------------------------------------------------------------------------- */

// ROM 0x00129ca8 __ct__12NSortedArrayFv
NSortedArray::NSortedArray()
{
	fComparator = nil;
}


// ROM 0x00129cf0 __dt__12NSortedArrayFv
NSortedArray::~NSortedArray()
{ }


// ROM 0x00129d30 Init__12NSortedArrayFP11NComparatorlN22Uc
NewtonErr
NSortedArray::Init(NComparator* comparator, long elementSize, long chunkSize, long physicalCount, Boolean shrink)
{
	if (comparator == nil)
		return -1;
	fComparator = comparator;
	return NArray::Init(elementSize, chunkSize, physicalCount, shrink);
}


// ROM 0x00129e0c Where__12NSortedArrayCFPCv
// Binary search: the index after the last element whose key is not greater
// than the element's - where an equal element goes, after those it equals.
long
NSortedArray::Where(const void* element) const
{
	long low = 0;
	long high = fCount - 1;
	while (high >= low)
	{
		long mid = (low + high) / 2;
		const void* midKey = fComparator->KeyOf(At(mid));
		if (fComparator->CompareKeys(fComparator->KeyOf(element), midKey) >= 0)
			low = mid + 1;
		else
			high = mid - 1;
	}
	return high + 1;
}


// ROM 0x00129d74 Contains__12NSortedArrayCFPCv
long
NSortedArray::Contains(const void* element) const
{
	long index = Where(element) - 1;
	if (index != -1)
	{
		const void* key = fComparator->KeyOf(At(index));
		if (fComparator->CompareKeys(fComparator->KeyOf(element), key) != 0)
			index = -1;
	}
	return index;
}


/* -------------------------------------------------------------------------------
	NIterator
	The iterators of an array form a ring through fNext; each notification
	runs round it until it is back at the array's first iterator.
------------------------------------------------------------------------------- */

// ROM 0x00128b24 InsertElements__9NIteratorFlT1
void
NIterator::InsertElements(long index, long count)
{
	if (fLow >= index)
		fLow += count;
	if (fHigh >= index)
		fHigh += count;
	if (fReverse ? fCurrent >= index : fCurrent > index)
		fCurrent += count;
	if (fArray != nil && fNext != fArray->fIterators)
		fNext->InsertElements(index, count);
}


// ROM 0x00128ab8 RemoveElements__9NIteratorFlT1
void
NIterator::RemoveElements(long index, long count)
{
	if (fLow > index)
		fLow -= count;
	if (fHigh >= index)
		fHigh -= count;
	if (fReverse ? fCurrent >= index : fCurrent > index)
		fCurrent -= count;
	if (fArray != nil && fNext != fArray->fIterators)
		fNext->RemoveElements(index, count);
}


// ROM 0x00128b90 DeleteArray__9NIteratorFv
void
NIterator::DeleteArray()
{
	if (fNext != fArray->fIterators)
		fNext->DeleteArray();
	fArray = nil;
}
