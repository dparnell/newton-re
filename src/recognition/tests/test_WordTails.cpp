// How the lexical search remembers what it has read
// (recognition/WordTails.h): a reading is a backwards linked list of
// single characters, reference counted, so that the dozens of partial
// readings the search holds at once share their ends instead of each
// keeping a string.
#include "WordTails.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


// A character put on the front of a reading: a cell taken off the free
// list - another table asked for only when it is empty - pointed at
// what was there, and the old tail's count raised.  (The search's own
// `SearchDoVStepFromNode` does this; it is NOT YET, so the test does it
// by hand.)
//
// Note that the empty tail is *not* given a reference: `WordTailAddRef`
// would take it down the cell path and look in table 127.  That is the
// bug noted in `WordTails.cpp`, and this is how the engine avoids it.
static WordTailRef
Push(WordTailRef tail, UByte c)
{
	WordTailRef ref;
	if (wordTailFrees == kWordTailNone)
		ref = WordTailBlockAllocate();
	else
	{
		ref = wordTailFrees;
		wordTailFrees = WordTailAt(ref)->fNext;
	}
	WordTailCell* cell = WordTailAt(ref);
	cell->fChar = c;
	cell->fRefCount = 1;
	cell->fNext = tail;
	if (tail != kWordTailNone)
		WordTailAddRef(tail);
	return ref;
}

static const char*
Text(WordTailRef ref, UByte* buffer)
{
	WordTailSprint(ref, buffer, 64);
	return (const char*) buffer;
}


int
main()
{
	InitHostStandaloneHeap();
	UByte buffer[64];

	// ---- one reading ----
	{
		EXPECT(wordTails == nil && wordTailTables == 0);
		WordTailRef ref = WordTailBlockAllocate();
		// the first call makes the table of tables and one table of 32
		EXPECT(wordTails != nil);
		EXPECT(wordTailTables == 1);
		// the 32 new cells go onto the free list in order, so the one
		// that comes back is the last of them and the next is below it
		EXPECT(ref == 31);
		EXPECT(wordTailFrees == 30);
		// asking again makes a whole new table - the search pops cells
		// off the free list itself and only calls this when it is empty
		EXPECT(WordTailBlockAllocate() == 0x3f);	// table 1, slot 31
		EXPECT(wordTailTables == 2);

		// a reading built one character at a time, and read back
		// forwards although it is stored backwards
		WordTailRef word = kWordTailNone;
		const char* letters = "hand";
		for (long i = 0; letters[i] != 0; i++)
			word = Push(word, (UByte) letters[i]);
		EXPECT(strcmp(Text(word, buffer), "hand") == 0);
		// the last character pushed is the cell the reference names
		EXPECT(WordTailAt(word)->fChar == 'd');
		// and its tail is the reading without it
		EXPECT(strcmp(Text(WordTailAt(word)->fNext, buffer), "han") == 0);
	}

	// ---- two readings sharing an end ----
	{
		// "hand" and "hanc" share three characters, and the sharing is
		// the whole point: the search holds dozens of partial readings
		// at once and they nearly all end the same way
		WordTailRef stem = kWordTailNone;
		const char* letters = "han";
		for (long i = 0; letters[i] != 0; i++)
			stem = Push(stem, (UByte) letters[i]);

		long before = wordTailTables;
		WordTailRef a = Push(stem, 'd');
		WordTailRef b = Push(stem, 'c');
		EXPECT(strcmp(Text(a, buffer), "hand") == 0);
		EXPECT(strcmp(Text(b, buffer), "hanc") == 0);
		// two readings of four characters cost two cells, not eight
		EXPECT(WordTailAt(a)->fNext == stem);
		EXPECT(WordTailAt(b)->fNext == stem);
		EXPECT(WordTailAt(stem)->fRefCount == 3);	// its own, and the two
		EXPECT(wordTailTables == before);

		// dropping one reading costs only the character no other
		// reading is still using
		WordTailRef wasFree = wordTailFrees;
		WordTailDeleteRef(b);
		EXPECT(WordTailAt(stem)->fRefCount == 2);
		EXPECT(wordTailFrees == b);					// the 'c' came back
		EXPECT(WordTailAt(b)->fNext == wasFree);
		EXPECT(strcmp(Text(a, buffer), "hand") == 0);	// and the other is untouched

		// dropping the last one takes the shared end with it
		WordTailDeleteRef(a);
		WordTailDeleteRef(stem);					// the stem's own reference
		EXPECT(WordTailAt(stem)->fRefCount == 0);
	}

	// ---- how a reference is read ----
	{
		// table and slot are cut straight out of the number, which is
		// why a cell never has to move once it is made
		WordTailRef ref = WordTailBlockAllocate();
		EXPECT(WordTailAt(ref) ==
			&((WordTailCell*) wordTails[(ref >> 5) & 0x7f])[ref & 0x1f]);
		// a count of 0xff means the cell is never given back
		WordTailAt(ref)->fRefCount = 0xff;
		WordTailAddRef(ref);
		EXPECT(WordTailAt(ref)->fRefCount == 0xff);
		WordTailDeleteRef(ref);
		EXPECT(WordTailAt(ref)->fRefCount == 0xff);
		// and nothing at all is nothing at all
		WordTailDeleteRef(kWordTailNone);
		EXPECT(WordTailSprint(kWordTailNone, buffer, 64) == 0);
		EXPECT(buffer[0] == 0);
	}

	// ---- readings compared ----
	{
		WordTailRef han = kWordTailNone;
		for (const char* p = "han"; *p != 0; p++)
			han = Push(han, (UByte) *p);
		WordTailRef hand = Push(han, 'd');
		WordTailRef hanc = Push(han, 'c');
		WordTailRef also = kWordTailNone;
		for (const char* p = "hand"; *p != 0; p++)
			also = Push(also, (UByte) *p);

		// the same reference is the same reading without looking
		EXPECT(WordTailCompare(hand, hand) == 0);
		// ... and so is the same text spelled out separately
		EXPECT(WordTailCompare(hand, also) == 0);
		EXPECT(hand != also);
		// oldest character first, so 'c' sorts before 'd'
		EXPECT(WordTailCompare(hanc, hand) == -1);
		EXPECT(WordTailCompare(hand, hanc) == 1);
		// the empty reading sorts before everything
		EXPECT(WordTailCompare(kWordTailNone, hand) == -1);
		EXPECT(WordTailCompare(hand, kWordTailNone) == 1);
	}

	// ---- how much room there is ----
	{
		// a reading written into too little room stops and still
		// terminates
		WordTailRef word = kWordTailNone;
		for (const char* p = "writing"; *p != 0; p++)
			word = Push(word, (UByte) *p);
		EXPECT(WordTailSprint(word, buffer, 64) == 7);
		EXPECT(strcmp((const char*) buffer, "writing") == 0);
		memset(buffer, 0xee, sizeof(buffer));
		EXPECT(WordTailSprint(word, buffer, 4) == 3);
		EXPECT(strcmp((const char*) buffer, "wri") == 0);
		// with nowhere to put it, only the count comes back
		EXPECT(WordTailSprint(word, nil, 64) == 7);
	}

	// ---- a set of alternatives ----
	{
		// a reference from 0xf000 up is a whole word list rather than
		// one character, which is how the search carries "it was one
		// of these" through the lattice
		WordListFreeAll();
		EXPECT(wordLists != nil && freeWordLists != nil);

		WordList* list = &wordLists[0];
		WordTailRef ref = (WordTailRef) (kWordTailListBase + 0);
		EXPECT(WordListAt(ref) == list);

		WordTailRef one = kWordTailNone;
		for (const char* p = "dog"; *p != 0; p++)
			one = Push(one, (UByte) *p);
		WordTailRef two = kWordTailNone;
		for (const char* p = "dug"; *p != 0; p++)
			two = Push(two, (UByte) *p);
		list->fRefCount = 1;
		list->fCount = 2;
		list->fTails[0] = one;
		list->fTails[1] = two;

		// it prints as its first alternative and a bullet, because a
		// set of alternatives has no one spelling
		EXPECT(WordListSprint(list, buffer, 64) == 4);
		EXPECT(buffer[0] == 'd' && buffer[3] == kWordListMark && buffer[4] == 0);
		// and so does the reference, through the same path
		EXPECT(WordTailSprint(ref, buffer, 64) == 4);
		EXPECT(buffer[3] == kWordListMark);
		// `Sprint2` refuses it altogether
		EXPECT(WordTailSprint2(ref, buffer, 64) == 0);
		EXPECT(buffer[0] == 0);

		// letting it go gives its alternatives back as well
		WordList* wasFree = freeWordLists;
		WordListDeleteRef(list);
		EXPECT(freeWordLists == list);
		EXPECT(*(WordList**) list == wasFree);
		EXPECT(WordTailAt(one)->fRefCount == 0);
		EXPECT(WordTailAt(two)->fRefCount == 0);
	}

	// ---- everything given back ----
	{
		long tables = wordTailTables;
		EXPECT(tables > 0);
		WordTailDeallocateGlobals();
		EXPECT(wordTails == nil && wordTailTables == 0);
		EXPECT(wordLists == nil);
		EXPECT(wordTailFrees == kWordTailNone);
		// twice is no trouble
		WordTailDeallocateGlobals();
	}

	if (failures == 0)
		printf("test_WordTails: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
