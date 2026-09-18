/*
	File:		utility/List.cpp

	Contains:	CList (List.h): a CDynamicArray of pointers, searched with
				CItemTesters (identity by a CItemComparer on the pointer).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	Size 0x18 (the CDynamicArray).
*/

#include "List.h"
#include "ListIterator.h"
#include "ItemComparer.h"
#include "UCErrors.h"
#include "NewtonMemory.h"


// ROM 0x00113238 __ct__5CListFv
CList::CList()
	: CDynamicArray(sizeof(void*), kDefaultChunkSize)
{ }


// ROM 0x0011332c __dt__5CListFv
CList::~CList()
{ }


// ROM 0x00112f9c Make__5CListSFv
CList*
CList::Make()
{
	return new CList;
}


// ROM 0x0010ed2c Make__5CListSFl
// (the size is not used)
CList*
CList::Make(ArrayIndex /*size*/)
{
	return new CList;
}


// ROM 0x0011341c At__5CListFl
// nil outside the list.
void*
CList::At(ArrayIndex index)
{
	void** slot = (void**) SafeElementPtrAt(index);
	return slot != nil ? *slot : nil;
}


// ROM 0x001134a8 InsertAt__5CListFlPv
NewtonErr
CList::InsertAt(ArrayIndex index, void* item)
{
	void* element = item;
	return InsertElementsBefore(index, &element, 1);
}


// ROM 0x00113640 InsertUnique__5CListFPv
// Appends the item unless it is already in the list; true if it was added.
Boolean
CList::InsertUnique(void* item)
{
	ArrayIndex index = GetIdentityIndex(item);
	if (index == kEmptyIndex)
		InsertAt(fSize, item);
	return index == kEmptyIndex;
}


// ROM 0x001134ec Remove__5CListFPv
// (the removal is RemoveElementsAt(index, 1), inlined in the ROM)
NewtonErr
CList::Remove(void* item)
{
	ArrayIndex index = GetIdentityIndex(item);
	if (index == kEmptyIndex)
		return eRangeCheck;
	return RemoveAt(index);
}


// ROM 0x00113684 Replace__5CListFPvT1
NewtonErr
CList::Replace(void* oldItem, void* newItem)
{
	ArrayIndex index = GetIdentityIndex(oldItem);
	if (index == kEmptyIndex)
		return eRangeCheck;
	void* element = newItem;
	return ReplaceElementsAt(index, &element, 1);
}


// ROM 0x00113820 ReplaceAt__5CListFlPv
NewtonErr
CList::ReplaceAt(ArrayIndex index, void* newItem)
{
	void* element = newItem;
	return ReplaceElementsAt(index, &element, 1);
}


// ROM 0x00112fd0 GetIdentityIndex__5CListFPv
// The index of the item itself (pointer equality), or kEmptyIndex.
ArrayIndex
CList::GetIdentityIndex(void* item)
{
	CItemComparer comparer(item, nil);
	ArrayIndex index;
	Search(&comparer, index);
	return index;
}


// ROM 0x00113008 Search__5CListFP11CItemTesterRl
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
