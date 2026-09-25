/*
	File:		recognition/RosList.cpp

	Contains:	The handwriting engine's own linked list - see
				RosList.h.

	Reconstructed from the MP2x00 US ROM; each function cites its
	origin.
*/

#include "RosList.h"
#include "RosStrokes.h"			// RosAllocate
#include "NewtonMemory.h"

#include <stdio.h>


// ROM 0x0c1010d8 (unnamed)
List*		gFreeLists = nil;
// ROM 0x0c1010dc (unnamed)
ListEntry*	gFreeListEntries = nil;
// ROM 0x0c1010e0 (unnamed)
void*		gListBlocks = nil;
// ROM 0x0c1010e4 (unnamed)
void*		gListEntryBlocks = nil;


// The head of a block of cells: the block before it, and how many
// cells follow.
// DEVIATION: pointer-sized on the host, as the cells are.
struct ListBlock
{
	void*	fNext;
	ULong	fCount;
};


// ROM 0x00113208 Listbailout
// Where the ROM stops to be looked at.  It does nothing.
Boolean
Listbailout(void)
{
	return true;
}


// ROM 0x00113274 ListBlockAllocate
// A block of list cells, all but the first strung onto the pool.  Each
// of those carries a count of minus one, so that a list used after it
// has been given back is caught by the checks below rather than
// followed.
List*
ListBlockAllocate(ULong count)
{
	if (count == 0)
		return nil;

	ListBlock* block = (ListBlock*) RosAllocate((long)
					(sizeof(ListBlock) + count * sizeof(List)));
	block->fNext = gListBlocks;
	block->fCount = count;
	gListBlocks = block;

	List* cells = (List*) (block + 1);
	for (ULong i = 1; i < count; i++)
	{
		cells[i].fFirst = (ListEntry*) gFreeLists;
		gFreeLists = &cells[i];
		cells[i].fCount = -1;
	}
	return &cells[0];
}


// ROM 0x0011335c ListEntryBlockAllocate
// ... and a block of entries, the free ones marked `FFlg`.
ListEntry*
ListEntryBlockAllocate(ULong count)
{
	if (count == 0)
		return nil;

	ListBlock* block = (ListBlock*) RosAllocate((long)
					(sizeof(ListBlock) + count * sizeof(ListEntry)));
	block->fNext = gListEntryBlocks;
	block->fCount = count;
	gListEntryBlocks = block;

	ListEntry* cells = (ListEntry*) (block + 1);
	for (ULong i = 1; i < count; i++)
	{
		cells[i].fNext = gFreeListEntries;
		gFreeListEntries = &cells[i];
		cells[i].fValue = (void*) kListEntryFree;
	}
	return &cells[0];
}


// ROM 0x00112f74 ListAllocate
// One list cell.  (The ROM writes the hundred-cell case out again
// here rather than calling `ListBlockAllocate`; the two are the same
// code and the compiler ran the tail of one into the other.)
List*
ListAllocate(void)
{
	if (gFreeLists != nil)
	{
		List* cell = gFreeLists;
		gFreeLists = (List*) cell->fFirst;
		return cell;
	}
	return ListBlockAllocate((ULong) kListBlockCells);
}


// ROM 0x00113210 ListEntryAllocate
ListEntry*
ListEntryAllocate(void)
{
	if (gFreeListEntries != nil)
	{
		ListEntry* cell = gFreeListEntries;
		gFreeListEntries = cell->fNext;
		return cell;
	}
	return ListEntryBlockAllocate((ULong) kListBlockCells);
}


// ROM 0x001134c8 ListCreate
List*
ListCreate(void)
{
	List* list = ListAllocate();
	list->fFirst = nil;
	list->fLast = nil;
	list->fCount = 0;
	return list;
}


// ROM 0x001136c0 ListAddEntry
// A value put in after the entry the cursor names, and the cursor left
// on the new entry.  A nil cursor means the front of the list.
Boolean
ListAddEntry(List* list, ListEntry** cursor, void* value)
{
	const char* complaint;
	if (list == nil)
		complaint = "Error: NULL list passed to %s\r";
	else if (list->fCount < 0)
		complaint = "Error: FREED list passed to %s\r";
	else if (*cursor != nil && (ULong) (*cursor)->fValue == kListEntryFree)
		complaint = "Error: FREED item indexed in %s\r";
	else
	{
		ListEntry* entry = ListEntryAllocate();
		entry->fValue = value;
		if (*cursor == nil)
		{
			entry->fNext = list->fFirst;
			entry->fPrev = nil;
			list->fFirst = entry;
		}
		else
		{
			entry->fNext = (*cursor)->fNext;
			entry->fPrev = *cursor;
			(*cursor)->fNext = entry;
		}
		if (entry->fNext != nil)
			entry->fNext->fPrev = entry;
		if (list->fLast == *cursor)
			list->fLast = entry;
		*cursor = entry;
		list->fCount++;
		return true;
	}
	fprintf(stderr, complaint, "ListAddEntry");
	Listbailout();
	return false;
}


// ROM 0x00112fa4 ListAppendEntry
// ... at the end, which is the same thing with the cursor on the last
// entry.  (ROM BUG: it answers true whatever `ListAddEntry` said, so a
// caller cannot tell that the list was nil or had been given back -
// only the complaint on the standard error shows it.)
Boolean
ListAppendEntry(List* list, void* value)
{
	ListEntry* cursor = list->fLast;
	ListAddEntry(list, &cursor, value);
	return true;
}


// ROM 0x001130a8 ListRemoveEntry
// The entry the cursor names taken out, the cursor moved on to the next
// and the value answered.  Nought means there was nothing there, which
// is how `ListDestroy` knows when to stop.
void*
ListRemoveEntry(List* list, ListEntry** cursor)
{
	const char* complaint;
	if (list == nil)
		complaint = "Error: NULL list passed to %s\r";
	else if (list->fCount < 0)
		complaint = "Error: FREED list passed to %s\r";
	else
	{
		ListEntry* entry = *cursor;
		if (entry == nil)
			return nil;
		void* value = entry->fValue;
		if ((ULong) value != kListEntryFree)
		{
			if (entry->fPrev == nil)
				list->fFirst = entry->fNext;
			else
				entry->fPrev->fNext = entry->fNext;
			*cursor = entry->fNext;
			if (entry->fNext == nil)
				list->fLast = entry->fPrev;
			else
				entry->fNext->fPrev = entry->fPrev;
			entry->fNext = gFreeListEntries;
			gFreeListEntries = entry;
			entry->fValue = (void*) kListEntryFree;
			list->fCount--;
			return value;
		}
		complaint = "Error: FREED item indexed in %s\r";
	}
	fprintf(stderr, complaint, "ListRemoveEntry");
	Listbailout();
	return nil;
}


// ROM 0x00113524 ListDestroy
// Every entry taken out - with `proc` called on each value first when
// one is handed over - and the list cell put back on its pool with a
// count of minus one.
Boolean
ListDestroy(List* list, ListDisposeProc proc)
{
	const char* complaint;
	if (list == nil)
		complaint = "Error: NULL list passed to %s\r";
	else if (list->fCount < 0)
		complaint = "Error: FREED list passed to %s\r";
	else
	{
		ListEntry* cursor = list->fFirst;
		if (proc == nil)
			while (ListRemoveEntry(list, &cursor) != nil)
				;
		else
		{
			void* value = ListRemoveEntry(list, &cursor);
			while (value != nil)
			{
				proc(value);
				value = ListRemoveEntry(list, &cursor);
			}
		}
		list->fFirst = (ListEntry*) gFreeLists;
		gFreeLists = list;
		list->fCount = -1;
		return true;
	}
	fprintf(stderr, complaint, "ListDestroy");
	Listbailout();
	return false;
}


// ROM 0x0011343c ListZap
// Every block given back and both pools emptied, which is what the
// engine does when it is put to sleep.  Nothing that still holds a
// list or an entry may run after this.
void
ListZap(void)
{
	void* block = gListBlocks;
	gListBlocks = nil;
	while (block != nil)
	{
		void* next = ((ListBlock*) block)->fNext;
		DisposPtr((Ptr) block);
		block = next;
	}

	block = gListEntryBlocks;
	gListEntryBlocks = nil;
	while (block != nil)
	{
		void* next = ((ListBlock*) block)->fNext;
		DisposPtr((Ptr) block);
		block = next;
	}

	gFreeLists = nil;
	gFreeListEntries = nil;
}
