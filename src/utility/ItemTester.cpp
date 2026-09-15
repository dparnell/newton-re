/*
	File:		utility/ItemTester.cpp

	Contains:	CItemTester and CItemComparer (ItemTester.h, ItemComparer.h):
				the predicate objects CList::Search and CSortedList take.  The
				base tester matches nothing; the comparer orders items by
				their pointer value against fItem (so a list of pointers can
				be searched for a given one).

	Reconstructed from the MP2100 D ROM; each function cites its origin (the
	FXU setters the header declares are not in this ROM).
	CItemComparer is 0xc bytes: vptr +0, fItem +4, fKey +8.
*/

#include "ItemComparer.h"


// ROM 0x000fa464 TestItem__11CItemTesterCFPCv
CompareResult
CItemTester::TestItem(const void* /*testItem*/) const
{
	return kItemLessThanCriteria;
}


// ROM 0x000fa3c8 __ct__13CItemComparerFv
CItemComparer::CItemComparer()
{
	fItem = nil;
	fKey = nil;
}


// ROM 0x000fa408 __ct__13CItemComparerFPCvT1
CItemComparer::CItemComparer(const void* testItem, const void* keyValue)
{
	fItem = testItem;
	fKey = keyValue;
}


// ROM 0x000fa448 TestItem__13CItemComparerCFPCv
// fItem against the criteria, as (unsigned) pointer values.
CompareResult
CItemComparer::TestItem(const void* criteria) const
{
	if ((uintptr_t) fItem < (uintptr_t) criteria)
		return kItemLessThanCriteria;
	if (fItem == criteria)
		return kItemEqualCriteria;
	return kItemGreaterThanCriteria;
}
