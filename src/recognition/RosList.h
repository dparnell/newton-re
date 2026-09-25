/*
	File:		recognition/RosList.h

	Contains:	The handwriting engine's own linked list.

				A doubly-linked list of `void*` with a cursor, and
				underneath it a pair of free lists so that making and
				unmaking lists costs nothing once the machine has been
				running a while.  Both a list and an entry are three
				words, and both come out of blocks of a hundred cells
				carved from one allocation; a cell that is free is
				linked into its pool and marked - a list with a count of
				minus one, an entry with the four characters `FFlg` -
				so that using one after it has been given back is caught
				rather than followed.

				`ListZap` gives every block back, which is what the
				engine does when it is put to sleep.

				The ligature fragmenter is what uses this: a stroke
				being cut into letters gathers its candidate break
				points here.

	Reconstructed from the MP2x00 US ROM (0x00112f74-0x00113750); each
	function cites its origin.
*/

#ifndef __ROSLIST_H
#define __ROSLIST_H

#ifndef __NEWTONTYPES_H
#include "NewtonTypes.h"
#endif

// One thing in a list.  A free entry carries `kListEntryFree` where its
// value would be and is linked into the pool by `fNext`.
// DEVIATION: pointer-sized on the host, so a cell is twenty-four bytes
// where the ROM's is twelve; every block below is sized by `sizeof`.
struct ListEntry
{
	void*		fValue;			// +0x00
	ListEntry*	fNext;			// +0x04
	ListEntry*	fPrev;			// +0x08
};

// ... and the list itself.  A free list carries a count of minus one
// and is linked into its pool by `fFirst`.
struct List
{
	ListEntry*	fFirst;			// +0x00
	ListEntry*	fLast;			// +0x04
	long		fCount;			// +0x08
};

// The four characters a free entry carries, and how many cells a block
// holds.
const ULong	kListEntryFree	= 0x46466c67;		// 'FFlg'
const long	kListBlockCells	= 100;

// What `ListDestroy` does with each value on the way out.
typedef void (*ListDisposeProc)(void* value);

// The two pools - the cells that are free, and the blocks they were
// carved from.  (They have no symbols of their own; they sit in the
// ROM's data just past `slimewonderbug`.)
extern List*		gFreeLists;			// ROM 0x0c1010d8 (unnamed)
extern ListEntry*	gFreeListEntries;	// ROM 0x0c1010dc (unnamed)
extern void*		gListBlocks;		// ROM 0x0c1010e0 (unnamed)
extern void*		gListEntryBlocks;	// ROM 0x0c1010e4 (unnamed)

// A block of `count` cells, all but the first put on the pool; the
// first is answered.  Nought answers nothing.
List*		ListBlockAllocate(ULong count);			// ROM 0x00113274 ListBlockAllocate
ListEntry*	ListEntryBlockAllocate(ULong count);	// ROM 0x0011335c ListEntryBlockAllocate

// One cell off the pool, a fresh block of a hundred if it is empty.
List*		ListAllocate(void);						// ROM 0x00112f74 ListAllocate
ListEntry*	ListEntryAllocate(void);				// ROM 0x00113210 ListEntryAllocate

// An empty list, and a list given back - with `proc` called on each of
// its values first when one is handed over.  `ListDestroy` answers
// false, having complained on the standard error, when the list is nil
// or has been given back already.
List*		ListCreate(void);						// ROM 0x001134c8 ListCreate
Boolean		ListDestroy(List* list, ListDisposeProc proc);	// ROM 0x00113524 ListDestroy

// A value put in after the entry the cursor names - at the front when
// the cursor is nil - and the cursor left on it.
Boolean		ListAddEntry(List* list, ListEntry** cursor, void* value);	// ROM 0x001136c0 ListAddEntry
// ... and at the end, which is the same thing with the cursor on the
// last entry.
Boolean		ListAppendEntry(List* list, void* value);	// ROM 0x00112fa4 ListAppendEntry

// The entry the cursor names taken out, the cursor moved on to the next
// and the value answered; nothing left answers nought.
void*		ListRemoveEntry(List* list, ListEntry** cursor);	// ROM 0x001130a8 ListRemoveEntry

// Every block given back and both pools emptied.
void		ListZap(void);							// ROM 0x0011343c ListZap

// Where the ROM stops to be looked at when one of the checks above
// fails.  It does nothing and answers true.
Boolean		Listbailout(void);						// ROM 0x00113208 Listbailout

#endif	/* __ROSLIST_H */
