/*
	File:		recognition/WordTails.cpp

	Contains:	The lexical search's readings - see WordTails.h.

	Reconstructed from the MP2x00 US ROM; each function cites its
	origin.
*/

#include "WordTails.h"
#include "RosStrokes.h"			// kRosettaMemoryTag
#include "NewtonMemory.h"
#include "NewtonExceptions.h"
#include "host/RomBugs.h"

extern const ExceptionName exRosetta;	// ROM 0x003774f8 exRosetta


// ROM 0x0c104fac wordTails
// Up to 128 tables of 32 cells each, made as they are needed.
// DEVIATION: the ROM asks for 0x200 bytes, its 128 four-byte pointers;
// a host pointer is eight.
Ptr*		wordTails = nil;
// ROM 0x0c104fb0 wordTailTables
long		wordTailTables = 0;
// ROM 0x0c104fb4 wordTailFrees
// The head of the free list, which is chained through each cell's
// `fNext`.
WordTailRef	wordTailFrees = kWordTailNone;
// ROM 0x0c104fb8 wordTailTmp
// Where `WordTailBlockAllocate` leaves the reference it is about to
// answer.  The ROM keeps it beside the free head rather than in a
// register.
WordTailRef	wordTailTmp = kWordTailNone;
// ROM 0x0c104fbc wordLists
WordList*	wordLists = nil;
// ROM 0x0c104fc0 freeWordLists
WordList*	freeWordLists = nil;


WordTailCell*
WordTailAt(WordTailRef ref)
{
	WordTailCell* table = (WordTailCell*) wordTails[(ref >> 5) & 0x7f];
	return &table[ref & 0x1f];
}


WordList*
WordListAt(WordTailRef ref)
{
	return &wordLists[ref - kWordTailListBase];
}


// The free-list link, which the ROM writes over the first word of a
// word list while it is free.  (The search takes one off when it keeps a
// finished word's readings - Search.cpp, SearchSegwordRememberNBest.)
// DEVIATION: a host pointer is eight bytes rather than four, so it
// covers `fRefCount`, `fCount` and four bytes of `fField04` - none of
// which mean anything while the entry is on the free list.
static void
WordListSetFreeLink(WordList* list, WordList* next)
{
	*(WordList**) list = next;
}


// ROM 0x00277180 WordTailBlockAllocate
// Another 32 cells.  The tables are made one at a time as the readings
// get longer, which is why a reference carries the table number in it:
// the search never has to move a cell once it has made it.
//
// The new cells are pushed onto the free list in order, so the one that
// comes back is the *last* of them.  A reference must stay under
// `kWordTailListBase`, and there may be no more than 128 tables, so
// running out of either is `evt.ex.Rosetta` - the engine giving up on
// a piece of writing rather than on the machine.
WordTailRef
WordTailBlockAllocate(void)
{
	if (wordTails == nil)
	{
		wordTails = (Ptr*) NewNamedPtr(kWordTailTablesMax * (long) sizeof(Ptr),
								kRosettaMemoryTag);
		if (wordTails == nil)
			Throw(exOutOfStack, (void*) "", nil);
	}
	if (wordTailTables > kWordTailTablesMax - 1)
		Throw(exRosetta, nil, nil);

	ULong base = (ULong) (wordTailTables & 0x7ff) * kWordTailsPerTable;
	if (base + kWordTailsPerTable > kWordTailListBase)
		Throw(exRosetta, nil, nil);

	Ptr table = NewNamedPtr(kWordTailsPerTable * (long) sizeof(WordTailCell),
						kRosettaMemoryTag);
	if (table == nil)
		Throw(exOutOfStack, (void*) "", nil);
	wordTails[wordTailTables] = table;
	wordTailTables++;

	ULong ref = base;
	for (long i = 0; i < kWordTailsPerTable; i++)
	{
		ref = (ref & ~0x1fUL) | (ULong) i;
		WordTailAt((WordTailRef) ref)->fNext = wordTailFrees;
		wordTailFrees = (WordTailRef) ref;
	}

	wordTailTmp = wordTailFrees;
	if (wordTailTmp == kWordTailNone)
		// (cannot happen: 32 were just added)
		return WordTailBlockAllocate();

	WordTailRef taken = wordTailTmp;
	wordTailFrees = WordTailAt(taken)->fNext;
	return taken;
}


// ROM 0x00277458 WordTailAddRef
// One more holder.  A count of 0xff means the cell is not counted at
// all and is never given back.
//
// **ROM BUG (fixed).**  `WordTailDeleteRef` begins by answering
// straight away for `kWordTailNone`; this does not, so the empty tail
// takes the cell path and looks in table 127, which is almost never
// made.  Nothing in the engine adds a reference to nothing, so it has
// never mattered.  The fix answers straight away for it too.
void
WordTailAddRef(WordTailRef ref)
{
	if (RomBugFixed() && ref == kWordTailNone)
		return;
	if (ref == kWordTailNone || ref < kWordTailListBase)
	{
		WordTailCell* cell = WordTailAt(ref);
		if (cell->fRefCount == 0xff)
			return;
		cell->fRefCount = (UByte) (cell->fRefCount + 1);
		return;
	}
	WordList* list = WordListAt(ref);
	if (list->fRefCount > 0xfe)
		return;
	list->fRefCount = (UShort) (list->fRefCount + 1);
}


// ROM 0x00276e48 WordTailDeleteRef
// One fewer holder, and the last one gives the cell back - along with
// everything it points at, which is where the sharing pays for itself:
// dropping a reading costs only the characters no other reading is
// still using.
void
WordTailDeleteRef(WordTailRef ref)
{
	if (ref == kWordTailNone)
		return;

	if (ref < kWordTailListBase)
	{
		WordTailCell* cell = WordTailAt(ref);
		UByte was = cell->fRefCount;
		if (was == 0xff)
			return;
		cell->fRefCount = (UByte) (was - 1);
		if (was != 1)
			return;
		// the last holder: the rest of the tail goes too
		WordTailDeleteRef(WordTailAt(ref)->fNext);
		WordTailAt(ref)->fNext = wordTailFrees;
		wordTailFrees = ref;
		return;
	}

	WordList* list = WordListAt(ref);
	if (list->fRefCount == 0xff)
		return;
	UShort left = (UShort) (list->fRefCount - 1);
	list->fRefCount = left;
	if (left != 0)
		return;
	for (long i = 0; i < list->fCount; i++)
		WordTailDeleteRef(list->fTails[i]);
	WordListSetFreeLink(list, freeWordLists);
	freeWordLists = list;
}


// ROM 0x00277338 WordTailDeallocateGlobals
void
WordTailDeallocateGlobals(void)
{
	if (wordTails != nil)
	{
		for (long i = 0; i < wordTailTables; i++)
			DisposPtr((Ptr) wordTails[i]);
		DisposPtr((Ptr) wordTails);
		wordTails = nil;
	}
	wordTailTables = 0;
	wordTailFrees = kWordTailNone;
	if (wordLists == nil)
		return;
	DisposPtr((Ptr) wordLists);
	wordLists = nil;
}


// ROM 0x00276f0c WordTailSprint
// A tail written out as text.  It recurses to the far end first and
// writes on the way back, because a tail runs backwards.
//
// A word list is a set of alternatives with no one spelling, so it
// prints as its own first alternative followed by a bullet.
long
WordTailSprint(WordTailRef ref, UByte* out, long room)
{
	if (ref != kWordTailNone)
	{
		if (ref >= kWordTailListBase)
		{
			WordList* list = WordListAt(ref);
			if (list == nil || list->fCount == 0)
			{
				if (room > 0 && out != nil)
					out[0] = 0;
				return 0;
			}
			if (list->fTails[0] == kWordTailNone)
				return 0;
			long at = WordTailSprint(list->fTails[0], out, room);
			if (at < room - 1)
			{
				if (out != nil)
				{
					out[at] = kWordListMark;
					out[at + 1] = 0;
				}
				return at + 1;
			}
			return at;
		}

		WordTailCell* cell = WordTailAt(ref);
		long at = WordTailSprint(cell->fNext, out, room);
		if (room - 1 <= at)
			return at;
		if (out != nil)
		{
			out[at] = WordTailAt(ref)->fChar;
			out[at + 1] = 0;
		}
		return at + 1;
	}

	if (room > 0 && out != nil)
		out[0] = 0;
	return 0;
}


// ROM 0x00276ff0 WordTailSprint2
// The same, except that a word list is nothing at all rather than a
// bullet - which is what the caller wants when it is going to print the
// alternatives itself.
long
WordTailSprint2(WordTailRef ref, UByte* out, long room)
{
	if (ref >= kWordTailListBase)
	{
		if (room > 0 && out != nil)
			out[0] = 0;
		return 0;
	}
	WordTailCell* cell = WordTailAt(ref);
	long at = WordTailSprint2(cell->fNext, out, room);
	if (at < room - 1)
	{
		if (out != nil)
		{
			out[at] = WordTailAt(ref)->fChar;
			out[at + 1] = 0;
		}
		return at + 1;
	}
	return at;
}


// ROM 0x0027709c WordTailCompare
// Two readings compared, oldest character first.  Answers -1, 0 or 1.
//
// Two tails that are the same reference are the same reading without
// looking at anything, which is the other half of what the sharing
// buys.  A word list on either side is not compared at all - it simply
// sorts by its reference - and the empty tail sorts before everything.
long
WordTailCompare(WordTailRef a, WordTailRef b)
{
	for (;;)
	{
		if (a == b)
			return 0;
		if ((a != kWordTailNone && a >= kWordTailListBase)
			|| (b != kWordTailNone && b >= kWordTailListBase))
			return (a > b) ? 1 : -1;
		if (a == kWordTailNone)
			return -1;
		if (b == kWordTailNone)
			return 1;

		UByte ca = WordTailAt(a)->fChar;
		UByte cb = WordTailAt(b)->fChar;
		if (ca != cb)
			return ((long) ca - (long) cb >= 0) ? 1 : -1;
		b = WordTailAt(b)->fNext;
		a = WordTailAt(a)->fNext;
	}
}


// ROM 0x002773bc WordListFreeAll
// The pool of fifty, made if it is not there and every one of them put
// back on the free list.  They are chained through their first word,
// which is the reference count while the entry is in use.
void
WordListFreeAll(void)
{
	if (wordLists == nil)
	{
		wordLists = (WordList*) NewNamedPtr(kWordListCount * (long) sizeof(WordList),
									kRosettaMemoryTag);
		if (wordLists == nil)
			Throw(exOutOfStack, (void*) "", nil);
	}
	freeWordLists = nil;
	for (long i = 0; i < kWordListCount; i++)
	{
		WordListSetFreeLink(&wordLists[i], freeWordLists);
		freeWordLists = &wordLists[i];
	}
}


// ROM 0x002774e0 WordListDeleteRef
void
WordListDeleteRef(WordList* list)
{
	if (list->fRefCount == 0xff)
		return;
	UShort left = (UShort) (list->fRefCount - 1);
	list->fRefCount = left;
	if (left != 0)
		return;
	for (long i = 0; i < list->fCount; i++)
		WordTailDeleteRef(list->fTails[i]);
	WordListSetFreeLink(list, freeWordLists);
	freeWordLists = list;
}


// ROM 0x00277568 WordListSprint
// Its first alternative, with a bullet after it to say there were
// others.
long
WordListSprint(WordList* list, UByte* out, long room)
{
	if (list == nil || list->fCount == 0)
	{
		if (room > 0 && out != nil)
			out[0] = 0;
		return 0;
	}
	WordTailRef first = list->fTails[0];
	if (first == kWordTailNone)
		return 0;
	long at = WordTailSprint(first, out, room);
	if (at < room - 1)
	{
		if (out != nil)
		{
			out[at] = kWordListMark;
			out[at + 1] = 0;
		}
		return at + 1;
	}
	return at;
}
