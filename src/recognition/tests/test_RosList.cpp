// The handwriting engine's own linked list (recognition/RosList.h):
// a doubly-linked list of pointers with a cursor, over two free lists
// carved from blocks of a hundred cells.

#include "RosList.h"
#include "memory/host/KernelHeap.h"
#include "host/RomBugs.h"

#include <stdio.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static long	gDisposed = 0;
static void	CountDisposed(void* /*value*/)	{ gDisposed++; }

static void*	V(long n)	{ return (void*) (long) (n + 1); }


// How many entries the list really has, walked from each end.
static long
Walk(List* list, Boolean forwards)
{
	long n = 0;
	if (forwards)
		for (ListEntry* e = list->fFirst; e != nil; e = e->fNext)
			n++;
	else
		for (ListEntry* e = list->fLast; e != nil; e = e->fPrev)
			n++;
	return n;
}


int
main()
{
	InitHostStandaloneHeap();

	// ---- a list made and given back ----
	{
		EXPECT(gFreeLists == nil && gListBlocks == nil);
		List* list = ListCreate();
		EXPECT(list != nil);
		EXPECT(list->fFirst == nil && list->fLast == nil && list->fCount == 0);
		// the first list carved a block of a hundred, so ninety-nine
		// are waiting
		EXPECT(gListBlocks != nil);
		EXPECT(gFreeLists != nil);

		EXPECT(ListDestroy(list, nil));
		// ... and it went back on the pool, marked as given back
		EXPECT(list->fCount == -1);
		EXPECT(gFreeLists == list);
		// which the checks catch rather than follow
		EXPECT(ListDestroy(list, nil) == false);
		// (`ListAppendEntry` answers true whatever happens - it does
		//  not look at what `ListAddEntry` said - so it is the
		//  complaint on the standard error that shows the check fired:
		//  ROM BUG (fixed), the fix answers false)
		SetRomBugFixed(false);
		EXPECT(ListAppendEntry(list, V(0)));
		SetRomBugFixed(true);
		EXPECT(ListAppendEntry(list, V(0)) == false);
		EXPECT(ListAppendEntry(nil, V(0)) == false);
		EXPECT(ListRemoveEntry(list, &list->fFirst) == nil);
		// a nil list likewise
		EXPECT(ListDestroy(nil, nil) == false);

		// the next list is the one just given back
		List* again = ListCreate();
		EXPECT(again == list);
		ListDestroy(again, nil);
	}

	// ---- things put in and taken out ----
	{
		List* list = ListCreate();
		for (long i = 0; i < 5; i++)
			EXPECT(ListAppendEntry(list, V(i)));
		EXPECT(list->fCount == 5);
		EXPECT(Walk(list, true) == 5);
		EXPECT(Walk(list, false) == 5);
		// appended, so they are in the order they were put in
		long i = 0;
		for (ListEntry* e = list->fFirst; e != nil; e = e->fNext, i++)
			EXPECT(e->fValue == V(i));
		EXPECT(list->fLast->fValue == V(4));
		EXPECT(list->fFirst->fPrev == nil && list->fLast->fNext == nil);

		// a nil cursor puts one at the front, and leaves the cursor on
		// it
		ListEntry* cursor = nil;
		EXPECT(ListAddEntry(list, &cursor, V(99)));
		EXPECT(cursor == list->fFirst);
		EXPECT(list->fFirst->fValue == V(99));
		EXPECT(list->fCount == 6);
		EXPECT(Walk(list, false) == 6);
		// ... and a cursor puts one *after* what it names
		EXPECT(ListAddEntry(list, &cursor, V(98)));
		EXPECT(list->fFirst->fNext == cursor);
		EXPECT(cursor->fValue == V(98));

		// taking one out answers its value and moves the cursor on
		cursor = list->fFirst;
		EXPECT(ListRemoveEntry(list, &cursor) == V(99));
		EXPECT(cursor == list->fFirst);
		EXPECT(cursor->fValue == V(98));
		EXPECT(list->fCount == 6);
		// the entry that was taken out went back on the pool, marked
		EXPECT(gFreeListEntries != nil);
		EXPECT((ULong) gFreeListEntries->fValue == kListEntryFree);

		// the last one out leaves the list empty and the cursor nil
		cursor = list->fFirst;
		while (ListRemoveEntry(list, &cursor) != nil)
			;
		EXPECT(list->fCount == 0);
		EXPECT(list->fFirst == nil && list->fLast == nil);
		EXPECT(cursor == nil);
		ListDestroy(list, nil);
	}

	// ---- given back with something to do to each value ----
	{
		List* list = ListCreate();
		for (long i = 0; i < 7; i++)
			ListAppendEntry(list, V(i));
		gDisposed = 0;
		EXPECT(ListDestroy(list, CountDisposed));
		EXPECT(gDisposed == 7);
	}

	// ---- a block of cells asked for by hand ----
	{
		List* first = ListBlockAllocate(4);
		EXPECT(first != nil);
		// three of the four went on the pool
		long free = 0;
		for (List* p = gFreeLists; p != nil; p = (List*) p->fFirst)
			free++;
		EXPECT(free >= 3);
		EXPECT(ListBlockAllocate(0) == nil);
		EXPECT(ListEntryBlockAllocate(0) == nil);
		ListEntry* e = ListEntryBlockAllocate(4);
		EXPECT(e != nil);
	}

	// ---- everything given back ----
	{
		ListZap();
		EXPECT(gListBlocks == nil && gListEntryBlocks == nil);
		EXPECT(gFreeLists == nil && gFreeListEntries == nil);
		// ... and the next list starts a fresh block
		List* list = ListCreate();
		EXPECT(list != nil && gListBlocks != nil);
		ListDestroy(list, nil);
		ListZap();
	}

	if (failures != 0)
	{
		fprintf(stderr, "test_RosList: %d failure(s)\n", failures);
		return 1;
	}
	printf("test_RosList: all passed\n");
	return 0;
}
