/*
	File:		recognition/WordTails.h

	Contains:	How the lexical search remembers what it has read.

				The search walks a lattice of candidate letters looking
				for the likeliest paths through it, and at every step it
				is holding a few dozen partial readings at once.  Those
				readings share nearly all of their text: if the search
				is halfway through "handwriting" it may be holding
				"handw", "hanciw" and "haridw", and all three of them
				end in the same "w" that came from the same piece of
				ink.

				So a reading is not a string.  It is a **word tail**: a
				backwards linked list of single characters, reference
				counted, so that the shared ends are stored once.  A
				tail is named by a 16-bit reference rather than a
				pointer, and the cells live in tables of 32 that are
				made as they are needed - 128 tables at most, which is
				4096 characters of readings in 16 KB.

				A reference is read like this:

				* `kWordTailNone` (0xffff) is the empty tail;
				* anything from `kWordTailListBase` (0xf000) up is a
				  **word list** - a whole set of alternatives at this
				  point, out of a pool of fifty;
				* anything else is a cell: table `(ref >> 5) & 0x7f`,
				  slot `ref & 0x1f`.

				`WordTailSprint` is what turns one back into text, and
				it prints a word list as a bullet, because a set of
				alternatives has no one spelling.

	Reconstructed from the MP2x00 US ROM (0x00276e48-0x00277600); each
	function cites its origin.
*/

#ifndef __WORDTAILS_H
#define __WORDTAILS_H

#ifndef __NEWTONTYPES_H
#include "NewtonTypes.h"
#endif

// What names a tail: a small integer, not a pointer.
typedef UShort	WordTailRef;

// The empty tail, and where the word lists start.
const WordTailRef	kWordTailNone		= 0xffff;
const WordTailRef	kWordTailListBase	= 0xf000;
// How the rest of a reference is cut up.
const long	kWordTailsPerTable	= 0x20;
const long	kWordTailTablesMax	= 0x80;
// The bullet a word list prints as, in Mac Roman.
const UByte	kWordListMark		= 0xa5;

// One character of a reading, and the rest of it.  Four bytes.
struct WordTailCell
{
	UByte		fChar;			// +0x00
	UByte		fRefCount;		// +0x01  0xff: not counted at all
	UShort		fNext;			// +0x02  the reference of the rest
};

// A set of alternatives at one point, out of a pool of fifty.
const long	kWordListCount	= 0x32;
struct WordList
{
	UShort		fRefCount;		// +0x00  (the free-list link while free)
	UByte		fCount;			// +0x02  how many alternatives
	UByte		fPad03;
	UByte		fField04[12];	// +0x04
	WordTailRef	fTails[40];		// +0x10
};

// The tables, and the pool.
extern Ptr*			wordTails;			// ROM 0x0c104fac wordTails
extern long			wordTailTables;		// ROM 0x0c104fb0 wordTailTables
extern WordTailRef	wordTailFrees;		// ROM 0x0c104fb4 wordTailFrees
extern WordTailRef	wordTailTmp;		// ROM 0x0c104fb8 wordTailTmp
extern WordList*	wordLists;			// ROM 0x0c104fbc wordLists
extern WordList*	freeWordLists;		// ROM 0x0c104fc0 freeWordLists

// The cell a reference names.  Only ever called on a reference that is
// neither `kWordTailNone` nor a word list.
WordTailCell*	WordTailAt(WordTailRef ref);
// ... and the word list it names.
WordList*		WordListAt(WordTailRef ref);

// Another table of 32 cells, strung onto the free list, and one of them
// answered.  The first call makes the table of tables as well.
WordTailRef	WordTailBlockAllocate(void);					// ROM 0x00277180 WordTailBlockAllocate
// One more holder of this tail, and one fewer - the last one gives the
// cell, and everything it points at, back to the free list.
void	WordTailAddRef(WordTailRef ref);					// ROM 0x00277458 WordTailAddRef
void	WordTailDeleteRef(WordTailRef ref);					// ROM 0x00276e48 WordTailDeleteRef
// Everything the pool is holding, given back.
void	WordTailDeallocateGlobals(void);					// ROM 0x00277338 WordTailDeallocateGlobals

// A tail written out as a string, oldest character first, stopping at
// `room - 1` characters and always terminating.  ==> how many were
// written.  `WordTailSprint` prints a word list as a bullet and then
// stops; `WordTailSprint2` refuses a word list altogether.
long	WordTailSprint(WordTailRef ref, UByte* out, long room);	// ROM 0x00276f0c WordTailSprint
long	WordTailSprint2(WordTailRef ref, UByte* out, long room);	// ROM 0x00276ff0 WordTailSprint2
// Two tails compared character by character, oldest first.  A word list
// sorts after everything, and the empty tail before it.
long	WordTailCompare(WordTailRef a, WordTailRef b);		// ROM 0x0027709c WordTailCompare

// The pool of fifty word lists, made and put all back on the free list.
void	WordListFreeAll(void);								// ROM 0x002773bc WordListFreeAll
// One fewer holder of a word list; the last one gives it and every tail
// in it back.
void	WordListDeleteRef(WordList* list);					// ROM 0x002774e0 WordListDeleteRef
// Its first alternative written out, with a bullet after it to say
// there were others.
long	WordListSprint(WordList* list, UByte* out, long room);	// ROM 0x00277568 WordListSprint

#endif	/* __WORDTAILS_H */
