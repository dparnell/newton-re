/*
	File:		utility/ListIterator.cpp

	Contains:	CListIterator (ListIterator.h): a CArrayIterator over a CList
				that hands out the items (the pointers) rather than indices.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
	Size 0x1c (the CArrayIterator).
*/

#include "ListIterator.h"
#include "DynamicArray.h"


// ROM 0x001103a8 __ct__13CListIteratorFv
CListIterator::CListIterator()
	: CArrayIterator()
{ }


// ROM 0x001103dc __ct__13CListIteratorFP13CDynamicArray
CListIterator::CListIterator(CDynamicArray* itsList)
	: CArrayIterator(itsList)
{ }


// ROM 0x00110418 __ct__13CListIteratorFP13CDynamicArrayUc
CListIterator::CListIterator(CDynamicArray* itsList, Boolean itsForward)
	: CArrayIterator(itsList, itsForward)
{ }


// ROM 0x0011045c __ct__13CListIteratorFP13CDynamicArraylT2Uc
CListIterator::CListIterator(CDynamicArray* itsList, ArrayIndex itsLowBound, ArrayIndex itsHighBound, Boolean itsForward)
	: CArrayIterator(itsList, itsLowBound, itsHighBound, itsForward)
{ }


// ROM 0x001104bc CurrentItem__13CListIteratorFv
void*
CListIterator::CurrentItem()
{
	if (fDynamicArray == nil)
		return nil;
	void** slot = (void**) fDynamicArray->SafeElementPtrAt(fCurrentIndex);
	return slot != nil ? *slot : nil;
}


// ROM 0x001104d8 FirstItem__13CListIteratorFv
void*
CListIterator::FirstItem()
{
	Reset();
	if (!More())
		return nil;
	void** slot = (void**) fDynamicArray->SafeElementPtrAt(fCurrentIndex);
	return slot != nil ? *slot : nil;
}


// ROM 0x00110508 NextItem__13CListIteratorFv
void*
CListIterator::NextItem()
{
	Advance();
	if (!More())
		return nil;
	void** slot = (void**) fDynamicArray->SafeElementPtrAt(fCurrentIndex);
	return slot != nil ? *slot : nil;
}
