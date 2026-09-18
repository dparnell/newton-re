/*
	File:		utility/DynamicArray.cpp

	Contains:	CDynamicArray (DynamicArray.h): a growable array of fixed-size
				elements over a memory-manager pointer, grown and shrunk in
				chunks, with a ring of CArrayIterators told about every
				insertion and removal so that iterations in progress stay
				valid.  CList, CSortedList and the event/timer queues sit on
				it.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	Layout (0x18 bytes): fSize +0, fElementSize +4, fChunkSize +8,
	fAllocatedSize +0xc, fArrayBlock +0x10, fIterator +0x14.
*/

#include "DynamicArray.h"
#include "ArrayIterator.h"
#include "UCErrors.h"
#include "NewtonMemory.h"


// ROM 0x000a1668 __ct__13CDynamicArrayFv
CDynamicArray::CDynamicArray()
{
	fArrayBlock = nil;
	fAllocatedSize = 0;
	fChunkSize = kDefaultChunkSize;
	fSize = 0;
	fElementSize = kDefaultElementSize;
	fIterator = nil;
}


// ROM 0x000a16ac __ct__13CDynamicArrayFlT1
CDynamicArray::CDynamicArray(Size elementSize, ArrayIndex chunkSize)
{
	fArrayBlock = nil;
	fAllocatedSize = 0;
	fChunkSize = chunkSize;
	fElementSize = elementSize;
	fSize = 0;
	fIterator = nil;
}


// ROM 0x000a171c __dt__13CDynamicArrayFv
// Iterators still on the array are cut loose (they answer "no more").
CDynamicArray::~CDynamicArray()
{
	if (fIterator != nil)
		fIterator->DeleteArray();
	if (fArrayBlock != nil)
		DisposPtr((Ptr) fArrayBlock);
}


// ROM 0x000a16f8 SetElementCount__13CDynamicArrayFl
NewtonErr
CDynamicArray::SetElementCount(ArrayIndex theSize)
{
	NewtonErr err = SetArraySize(theSize);
	if (err == noErr)
		fSize = theSize;
	return err;
}


// ROM 0x000a175c SafeElementPtrAt__13CDynamicArrayFl
// nil outside [0, fSize).
void*
CDynamicArray::SafeElementPtrAt(ArrayIndex index)
{
	if (fSize == 0 || index == kEmptyIndex || index < 0 || index >= fSize)
		return nil;
	return ElementPtrAt(index);
}


// ROM 0x000a19b0 SetArraySize__13CDynamicArrayFl
// The physical size follows the logical one in chunks: nothing changes
// while the request fits and the slack is under a chunk; otherwise the
// block is resized to the next chunk boundary above the request (a request
// on a boundary still gets a further chunk).  Zero frees the block.
NewtonErr
CDynamicArray::SetArraySize(ArrayIndex theSize)
{
	NewtonErr err = noErr;
	if (theSize == 0)
	{
		if (fArrayBlock != nil)
		{
			DisposPtr((Ptr) fArrayBlock);
			err = MemError();
			fArrayBlock = nil;
			fAllocatedSize = 0;
		}
	}
	else if (fAllocatedSize < theSize || fAllocatedSize - theSize >= fChunkSize)
	{
		ArrayIndex newSize = theSize;
		if (fChunkSize != 0)
			newSize = (theSize + fChunkSize) - (theSize + fChunkSize) % fChunkSize;
		if (fAllocatedSize != newSize)
		{
			void* block = ReallocPtr((Ptr) fArrayBlock, ComputeByteCount(newSize));
			err = MemError();
			if (err == noErr)
			{
				fAllocatedSize = newSize;
				fArrayBlock = block;
			}
		}
	}
	return err;
}


// ROM 0x000a182c GetElementsAt__13CDynamicArrayFlPvT1
NewtonErr
CDynamicArray::GetElementsAt(ArrayIndex index, void* elemPtr, ArrayIndex count)
{
	if (count > 0)
		BlockMove(ElementPtrAt(index), elemPtr, ComputeByteCount(count));
	return noErr;
}


// ROM 0x000a1864 InsertElementsBefore__13CDynamicArrayFlPvT1
// An index past the end appends.
NewtonErr
CDynamicArray::InsertElementsBefore(ArrayIndex startHere, void* elemPtr, ArrayIndex count)
{
	NewtonErr err = noErr;
	if (startHere > fSize)
		startHere = fSize;
	if (count > 0 && (err = SetArraySize(fSize + count)) == noErr)
	{
		void* slot = ElementPtrAt(startHere);
		if (startHere < fSize)
			BlockMove(slot, ElementPtrAt(startHere + count), (char*) ElementPtrAt(fSize) - (char*) slot);
		BlockMove(elemPtr, slot, ComputeByteCount(count));
		fSize += count;
		if (fIterator != nil)
			fIterator->InsertElementsBefore(startHere, count);
	}
	return err;
}


// ROM 0x000a1920 ReplaceElementsAt__13CDynamicArrayFlPvT1
NewtonErr
CDynamicArray::ReplaceElementsAt(ArrayIndex index, void* elemPtr, ArrayIndex count)
{
	if (count > 0)
		BlockMove(elemPtr, ElementPtrAt(index), ComputeByteCount(count));
	return noErr;
}


// ROM 0x000a178c RemoveElementsAt__13CDynamicArrayFlT1
NewtonErr
CDynamicArray::RemoveElementsAt(ArrayIndex index, ArrayIndex count)
{
	NewtonErr err = noErr;
	if (fSize == 0)
		return noErr;
	if (count > 0)
	{
		char* from = (char*) ElementPtrAt(index + count);
		char* end = (char*) ElementPtrAt(fSize);
		if (from < end)
			BlockMove(from, ElementPtrAt(index), end - from);
		err = SetArraySize(fSize - count);
		if (err == noErr)
		{
			fSize -= count;
			if (fIterator != nil)
				fIterator->RemoveElementsAt(index, count);
		}
	}
	return err;
}


// ROM 0x000a1958 Merge__13CDynamicArrayFP13CDynamicArray
// Appends the other array's elements; the element sizes must agree.
NewtonErr
CDynamicArray::Merge(CDynamicArray* aDynamicArray)
{
	if (fElementSize != aDynamicArray->fElementSize)
		return eElementSizeMismatch;
	if (aDynamicArray->fSize <= 0)
		return noErr;
	return InsertElementsBefore(fSize, aDynamicArray->fArrayBlock, aDynamicArray->fSize);
}
