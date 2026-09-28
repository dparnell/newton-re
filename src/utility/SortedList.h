/*
	File:		utility/SortedList.h

	Contains:	CSortedList: a CList kept in the order of a CItemComparer,
				searched by bisection.  The DDK has no header for it (the
				ArrayIterator header only names it as a friend); this
				declaration follows the ROM.

	Layout (0x1c bytes): the CList (0x18), fComparer +0x18.
*/

#ifndef __SORTEDLIST_H
#define __SORTEDLIST_H

#ifndef __LIST_H
#include "List.h"
#endif

class CItemComparer;

class CSortedList : public CList
{
public:
					CSortedList(CItemComparer* comparer);
					~CSortedList();

	// insertion keeps the order; the comparer's test item is set to the
	// item being inserted
	NewtonErr		Insert(void* item);
	Boolean			InsertUnique(void* item);

	// searching: bisection with a tester that orders (the comparer);
	// index is where a matching item is, or where one would go
	void*			Search(CItemTester* test, ArrayIndex& index);

protected:
	NewtonErr		InsertDuplicate(ArrayIndex index, void* existingItem, void* newItem);

	CItemComparer*	fComparer;
};

#endif	/* __SORTEDLIST_H */
