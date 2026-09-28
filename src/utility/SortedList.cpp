/*
	File:		utility/SortedList.cpp

	Contains:	CSortedList (SortedList.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "SortedList.h"
#include "ListIterator.h"
#include "ItemComparer.h"


// ROM 0x001e34c0 __ct__11CSortedListFP13CItemComparer
CSortedList::CSortedList(CItemComparer* comparer)
	: CList()
{
	fComparer = comparer;
}


// ROM 0x001e34fc __dt__11CSortedListFv
CSortedList::~CSortedList()
{ }


// ROM 0x001e352c Insert__11CSortedListFPv
// ==> InsertAt's error.
NewtonErr
CSortedList::Insert(void* item)
{
	fComparer->SetTestItem(item);
	ArrayIndex index;
	void* existing = Search(fComparer, index);
	if (existing == nil)
		return InsertAt(index, item);
	return InsertDuplicate(index, existing, item);
}


// ROM 0x001e358c InsertUnique__11CSortedListFPv
// true if the item went in (no equal item was there).
Boolean
CSortedList::InsertUnique(void* item)
{
	fComparer->SetTestItem(item);
	ArrayIndex index;
	void* existing = Search(fComparer, index);
	if (existing == nil)
		InsertAt(index, item);
	return existing == nil;
}


// ROM 0x001e35f0 InsertDuplicate__11CSortedListFlPvT2
// An equal item is already there: the new one goes in front of it.
NewtonErr
CSortedList::InsertDuplicate(ArrayIndex index, void* /*existingItem*/, void* newItem)
{
	return InsertAt(index, newItem);
}


// ROM 0x001e35f8 Search__11CSortedListFP11CItemTesterRl
// Bisection over the list, with the iterator's bounds as the interval.
// The tester says how its item compares with the list's: less means look
// below, greater above.  Without a match the index is the insertion point
// (one past the last item found less); kEmptyIndex if that is out of range.
void*
CSortedList::Search(CItemTester* test, ArrayIndex& index)
{
	if (fSize == 0)
	{
		index = 0;
		return nil;
	}
	CListIterator iter(this);
	void* item;
	CompareResult result;
	do
	{
		iter.fCurrentIndex = (iter.fLowBound + iter.fHighBound) >> 1;
		item = At(iter.fCurrentIndex);
		result = test->TestItem(item);
		if (result <= kItemEqualCriteria)
			iter.fHighBound = iter.fCurrentIndex - 1;
		else
			iter.fLowBound = iter.fCurrentIndex + 1;
	} while (result != kItemEqualCriteria && iter.fLowBound <= iter.fHighBound);
	if (result != kItemEqualCriteria)
	{
		item = nil;
		if (result > kItemEqualCriteria)
			iter.fCurrentIndex++;
	}
	index = (iter.fCurrentIndex < 0 || iter.fCurrentIndex > fSize) ? kEmptyIndex : iter.fCurrentIndex;
	return item;
}
