/*
	File:		utility/ArrayIterator.cpp

	Contains:	CArrayIterator (ArrayIterator.h): an index walking a
				CDynamicArray between bounds, forwards or backwards.  The
				array keeps its live iterators in a doubly-linked ring
				(fIterator is one of them) and, when elements are inserted or
				removed, tells the ring so that every iterator's bounds and
				current index move with the elements.  kEmptyIndex (-1) is the
				index of "nothing".

	Reconstructed from the MP2100 D ROM; each function cites its origin.
	Layout (0x1c bytes): fDynamicArray +0, fCurrentIndex +4, fLowBound +8,
	fHighBound +0xc, fIterateForward +0x10, fPreviousLink +0x14,
	fNextLink +0x18.
*/

#include "ArrayIterator.h"
#include "DynamicArray.h"


// ROM 0x00038750 __ct__14CArrayIteratorFv
CArrayIterator::CArrayIterator()
{
	Init();
}


// ROM 0x0003883c __ct__14CArrayIteratorFP13CDynamicArray
CArrayIterator::CArrayIterator(CDynamicArray* itsDynamicArray)
{
	Init(itsDynamicArray, 0, itsDynamicArray->fSize - 1, kIterateForward);
}


// ROM 0x000387e4 __ct__14CArrayIteratorFP13CDynamicArrayUc
CArrayIterator::CArrayIterator(CDynamicArray* itsDynamicArray, Boolean itsForward)
{
	Init(itsDynamicArray, 0, itsDynamicArray->fSize - 1, itsForward);
}


// ROM 0x00038784 __ct__14CArrayIteratorFP13CDynamicArraylT2Uc
CArrayIterator::CArrayIterator(CDynamicArray* itsDynamicArray, ArrayIndex itsLowBound, ArrayIndex itsHighBound, Boolean itsForward)
{
	Init(itsDynamicArray, itsLowBound, itsHighBound, itsForward);
}


// ROM 0x00038890 __dt__14CArrayIteratorFv
CArrayIterator::~CArrayIterator()
{
	if (fDynamicArray != nil)
		fDynamicArray->fIterator = RemoveFromList();
}


// ROM 0x000388d4 Init__14CArrayIteratorFv
void
CArrayIterator::Init()
{
	fNextLink = this;
	fPreviousLink = this;
	fHighBound = kEmptyIndex;
	fLowBound = kEmptyIndex;
	fCurrentIndex = kEmptyIndex;
	fIterateForward = kIterateForward;
	fDynamicArray = nil;
}


// ROM 0x00038900 Init__14CArrayIteratorFP13CDynamicArraylT2Uc
void
CArrayIterator::Init(CDynamicArray* itsDynamicArray, ArrayIndex itsLowBound, ArrayIndex itsHighBound, Boolean itsForward)
{
	fNextLink = this;
	fPreviousLink = this;
	fDynamicArray = itsDynamicArray;
	fDynamicArray->fIterator = AppendToList(itsDynamicArray->fIterator);
	InitBounds(itsLowBound, itsHighBound, itsForward);
}


// ROM 0x00038488 InitBounds__14CArrayIteratorFlT1Uc
// The bounds are clipped to the array; an empty array gives kEmptyIndex for
// both, and the current index starts at the end the iteration begins from.
void
CArrayIterator::InitBounds(ArrayIndex itsLowBound, ArrayIndex itsHighBound, Boolean itsForward)
{
	ArrayIndex last = fDynamicArray->fSize;
	if (last < 1)
		itsHighBound = kEmptyIndex;
	else
	{
		last--;
		if (itsHighBound < 0)
			itsHighBound = 0;
		if (itsHighBound >= last)
			itsHighBound = last;
	}
	fHighBound = itsHighBound;
	if (itsHighBound < 0)
		itsLowBound = kEmptyIndex;
	else
	{
		if (itsLowBound < 0)
			itsLowBound = 0;
		if (itsLowBound >= itsHighBound)
			itsLowBound = itsHighBound;
	}
	fLowBound = itsLowBound;
	fIterateForward = itsForward;
	fCurrentIndex = fIterateForward ? fLowBound : fHighBound;
}


// ROM 0x000384f4 ResetBounds__14CArrayIteratorFUc
// The whole array, in the given direction.
void
CArrayIterator::ResetBounds(Boolean goForward)
{
	fHighBound = fDynamicArray->fSize < 1 ? kEmptyIndex : fDynamicArray->fSize - 1;
	fLowBound = fHighBound >= 0 ? 0 : kEmptyIndex;
	fIterateForward = goForward;
	fCurrentIndex = fIterateForward ? fLowBound : fHighBound;
}


// ROM 0x00038548 Reset__14CArrayIteratorFv
void
CArrayIterator::Reset()
{
	fCurrentIndex = fIterateForward ? fLowBound : fHighBound;
}


// ROM 0x00038404 SwitchArray__14CArrayIteratorFP13CDynamicArrayUc
void
CArrayIterator::SwitchArray(CDynamicArray* newArray, Boolean itsForward)
{
	if (fDynamicArray != nil)
	{
		fDynamicArray->fIterator = RemoveFromList();
		fDynamicArray = nil;
	}
	Init(newArray, 0, newArray->fSize - 1, itsForward);
}


// ROM 0x00038528 More__14CArrayIteratorFv
Boolean
CArrayIterator::More()
{
	return fDynamicArray != nil && fCurrentIndex != kEmptyIndex;
}


// ROM 0x00038590 Advance__14CArrayIteratorFv
// One step in the iteration's direction; kEmptyIndex past the bound.
void
CArrayIterator::Advance()
{
	if (fIterateForward)
	{
		if (fCurrentIndex < fHighBound)
		{
			fCurrentIndex++;
			return;
		}
	}
	else if (fCurrentIndex > fLowBound)
	{
		fCurrentIndex--;
		return;
	}
	fCurrentIndex = kEmptyIndex;
}


// ROM 0x000386b0 CurrentIndex__14CArrayIteratorFv
ArrayIndex
CArrayIterator::CurrentIndex()
{
	return fDynamicArray != nil ? fCurrentIndex : kEmptyIndex;
}


// ROM 0x000386c4 FirstIndex__14CArrayIteratorFv
ArrayIndex
CArrayIterator::FirstIndex()
{
	Reset();
	return More() ? fCurrentIndex : kEmptyIndex;
}


// ROM 0x00038724 NextIndex__14CArrayIteratorFv
ArrayIndex
CArrayIterator::NextIndex()
{
	Advance();
	return More() ? fCurrentIndex : kEmptyIndex;
}


// ROM 0x000385d8 RemoveElementsAt__14CArrayIteratorFlT1
// The array removed elements: every iterator in the ring (from this one
// round to the array's head) pulls its bounds and, if it has passed the
// place, its current index back by the count.
void
CArrayIterator::RemoveElementsAt(ArrayIndex theIndex, ArrayIndex theCount)
{
	CArrayIterator* i = this;
	for (;;)
	{
		if (theIndex < i->fLowBound)
			i->fLowBound -= theCount;
		if (theIndex <= i->fHighBound)
			i->fHighBound -= theCount;
		if (i->fIterateForward ? theIndex <= i->fCurrentIndex : theIndex < i->fCurrentIndex)
			i->fCurrentIndex -= theCount;
		if (i->fDynamicArray == nil)
			return;
		i = i->fNextLink;
		if (i == i->fDynamicArray->fIterator)
			return;
	}
}


// ROM 0x00038644 InsertElementsBefore__14CArrayIteratorFlT1
// The counterpart for an insertion.
void
CArrayIterator::InsertElementsBefore(ArrayIndex theIndex, ArrayIndex theCount)
{
	CArrayIterator* i = this;
	for (;;)
	{
		if (theIndex <= i->fLowBound)
			i->fLowBound += theCount;
		if (theIndex <= i->fHighBound)
			i->fHighBound += theCount;
		if (i->fIterateForward ? theIndex <= i->fCurrentIndex : theIndex < i->fCurrentIndex)
			i->fCurrentIndex += theCount;
		if (i->fDynamicArray == nil)
			return;
		i = i->fNextLink;
		if (i == i->fDynamicArray->fIterator)
			return;
	}
}


// ROM 0x00038560 DeleteArray__14CArrayIteratorFv
// The array is going away: every iterator in the ring forgets it.
void
CArrayIterator::DeleteArray()
{
	if (fNextLink != fDynamicArray->fIterator)
		fNextLink->DeleteArray();
	fDynamicArray = nil;
}


// ROM 0x00038464 AppendToList__14CArrayIteratorFP14CArrayIterator
// Links this iterator after toList (nil: it is the ring alone); returns the
// ring's head.
CArrayIterator*
CArrayIterator::AppendToList(CArrayIterator* toList)
{
	if (toList == nil)
		return this;
	fPreviousLink = toList;
	fNextLink = toList->fNextLink;
	toList->fNextLink->fPreviousLink = this;
	toList->fNextLink = this;
	return this;
}


// ROM 0x000386f0 RemoveFromList__14CArrayIteratorFv
// Unlinks this iterator; returns the ring's new head (nil if it was alone).
CArrayIterator*
CArrayIterator::RemoveFromList()
{
	CArrayIterator* next = fNextLink;
	CArrayIterator* head = next != this ? next : nil;
	next->fPreviousLink = fPreviousLink;
	fPreviousLink->fNextLink = fNextLink;
	fNextLink = this;
	fPreviousLink = this;
	return head;
}
