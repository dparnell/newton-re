/*
	File:		utility/List.cpp

	Contains:	CList (List.h): a CDynamicArray of pointers, searched with
				CItemTesters (identity by a CItemComparer on the pointer).

	Reconstructed from the MP2100 D ROM; each function cites its origin.
	Size 0x18 (the CDynamicArray).
*/

#include "List.h"
#include "ListIterator.h"
#include "ItemComparer.h"
#include "UCErrors.h"
#include "NewtonMemory.h"


// ROM 0x001148ac __ct__5CListFv
CList::CList()
	: CDynamicArray(sizeof(void*), kDefaultChunkSize)
{ }


// ROM 0x001149a0 __dt__5CListFv
CList::~CList()
{ }


// ROM 0x00114610 Make__5CListSFv
CList*
CList::Make()
{
	return new CList;
}


// ROM 0x001103a0 Make__5CListSFl
// (the size is not used)
CList*
CList::Make(ArrayIndex /*size*/)
{
	return new CList;
}


// ROM 0x00114a90 At__5CListFl
// nil outside the list.
void*
CList::At(ArrayIndex index)
{
	void** slot = (void**) SafeElementPtrAt(index);
	return slot != nil ? *slot : nil;
}


// ROM 0x00114b1c InsertAt__5CListFlPv
NewtonErr
CList::InsertAt(ArrayIndex index, void* item)
{
	void* element = item;
	return InsertElementsBefore(index, &element, 1);
}


// ROM 0x00114cb4 InsertUnique__5CListFPv
// Appends the item unless it is already in the list; true if it was added.
Boolean
CList::InsertUnique(void* item)
{
	ArrayIndex index = GetIdentityIndex(item);
	if (index == kEmptyIndex)
		InsertAt(fSize, item);
	return index == kEmptyIndex;
}


// ROM 0x00114b60 Remove__5CListFPv
// (the removal is RemoveElementsAt(index, 1), inlined in the ROM)
NewtonErr
CList::Remove(void* item)
{
	ArrayIndex index = GetIdentityIndex(item);
	if (index == kEmptyIndex)
		return eRangeCheck;
	return RemoveAt(index);
}


// ROM 0x00114cf8 Replace__5CListFPvT1
NewtonErr
CList::Replace(void* oldItem, void* newItem)
{
	ArrayIndex index = GetIdentityIndex(oldItem);
	if (index == kEmptyIndex)
		return eRangeCheck;
	void* element = newItem;
	return ReplaceElementsAt(index, &element, 1);
}


// ROM 0x00114e94 ReplaceAt__5CListFlPv
NewtonErr
CList::ReplaceAt(ArrayIndex index, void* newItem)
{
	void* element = newItem;
	return ReplaceElementsAt(index, &element, 1);
}


// ROM 0x00114644 GetIdentityIndex__5CListFPv
// The index of the item itself (pointer equality), or kEmptyIndex.
ArrayIndex
CList::GetIdentityIndex(void* item)
{
	CItemComparer comparer(item, nil);
	ArrayIndex index;
	Search(&comparer, index);
	return index;
}


// ROM 0x0011467c Search__5CListFP11CItemTesterRl
// The first item the tester finds equal to its criteria, and its index
// (kEmptyIndex when none).
void*
CList::Search(CItemTester* test, ArrayIndex& index)
{
	CListIterator iter(this);
	index = kEmptyIndex;
	for (void* item = iter.FirstItem(); iter.More(); item = iter.NextItem())
	{
		if (test->TestItem(item) == kItemEqualCriteria)
		{
			index = iter.CurrentIndex();
			return item;
		}
	}
	return nil;
}
