// The lexical search's state and its life (recognition/Search.h): the
// thirty-seven columns the Viterbi walks, the readings each of them
// keeps, and what happens to a word on the way out.
#include "Search.h"
#include "RosEngine.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


int
main()
{
	InitHostStandaloneHeap();

	// ---- the state ----
	{
		EXPECT(gSearchAllocated == 0);
		// one column per stroke of the longest word the engine reads,
		// and one to start from
		EXPECT(kSearchColumns == 37);
		// there is room for thirty readings in a column and the ROM
		// uses twenty-seven
		EXPECT(MaxBestNodes == 27);
		EXPECT(MaxBestNodes <= kSearchNodeSlots);

		SearchAllocateGlobals();
		EXPECT(gSearchAllocated == 1);
		EXPECT(gSearchColumns != nil);
		// every column has its nodes carved out of one block of its
		// own, which is how the ROM avoids allocating in the middle of
		// reading a word
		for (long i = 0; i < kSearchColumns; i++)
		{
			EXPECT(gSearchColumns[i] != nil);
			for (long j = 0; j < MaxBestNodes; j++)
				EXPECT(gSearchColumns[i]->fNodes[j] != nil);
			// consecutive, and only the used slots are set
			for (long j = 1; j < MaxBestNodes; j++)
				EXPECT(gSearchColumns[i]->fNodes[j]
					== gSearchColumns[i]->fNodes[j - 1] + 1);
		}
		// twice is no trouble
		SearchAllocateGlobals();
		EXPECT(gSearchAllocated == 1);
	}

	// ---- a word about to be read ----
	{
		const BiGrammar* grammar = ROMGrammar.fContexts[0];
		// leave something behind in a column to be cleared
		gSearchColumns[5]->fCount = 9;
		gSearchColumns[0]->fCount = 9;

		SearchBeginWord(grammar);
		EXPECT(gSearchGrammar == grammar);
		// every column but the first is empty
		for (long i = 1; i < kSearchColumns; i++)
		{
			EXPECT(gSearchColumns[i]->fCount == 0);
			EXPECT(gSearchColumns[i]->fWords == nil);
		}
		// ... and the first holds the one reading every path grows
		// from: nothing read yet
		SearchColumn* first = gSearchColumns[0];
		EXPECT(first->fCount == 1);
		EXPECT(first->fWords == nil);
		EXPECT(first->fNodes[0]->fTail == kWordTailNone);
		EXPECT(first->fNodes[0]->fField00 == 0);
		EXPECT(first->fNodes[0]->fField0c == 0);
		// the word-list pool was made for it
		EXPECT(wordLists != nil);

		// a column given back lets go of whatever its nodes held
		GCBestNodes(first);
		EXPECT(first->fWords == nil);
	}

	// ---- room for the readings on the way out ----
	{
		SearchAllocateReturnCache(10);
		SearchAllocateReturnCache(4);		// smaller: nothing happens
		SearchAllocateReturnCache(30);		// bigger: made again
		// it only ever grows, so the next word finds the room there
		SearchAllocateReturnCache(30);
	}

	// ---- write it three times ----
	{
		// The one thing in the engine that is not about reading
		// handwriting.  Eight words, and writing one of them three
		// times in a row gets something else back.
		EXPECT(strcmp(kSearchEasterWords[0], "larryy") == 0);
		EXPECT(strcmp(kSearchEasterReplies[0], "The Doctor is on.") == 0);
		EXPECT(strcmp(kSearchEasterWords[4], "Rosetta!") == 0);
		EXPECT(strcmp(kSearchEasterReplies[4], "Hey, that's me!") == 0);

		char plain[] = "handwriting";
		char* word = plain;
		SearchCheckHashHit(&word);
		EXPECT(word == plain);				// an ordinary reading is untouched

		// once and twice is nothing
		char secret[] = "Rosetta!";
		word = secret;
		SearchCheckHashHit(&word);
		EXPECT(word == secret);
		word = secret;
		SearchCheckHashHit(&word);
		EXPECT(word == secret);
		// the third time it answers for itself
		word = secret;
		SearchCheckHashHit(&word);
		EXPECT(strcmp(word, "Hey, that's me!") == 0);

		// and the count starts again afterwards
		word = secret;
		SearchCheckHashHit(&word);
		EXPECT(word == secret);

		// the three have to be consecutive: anything else in between
		// clears the count
		char other[] = "larryy";
		word = secret;	SearchCheckHashHit(&word);
		word = other;	SearchCheckHashHit(&word);
		word = secret;	SearchCheckHashHit(&word);
		word = secret;	SearchCheckHashHit(&word);
		EXPECT(word == secret);				// only two in a row
		word = secret;	SearchCheckHashHit(&word);
		EXPECT(strcmp(word, "Hey, that's me!") == 0);

		// nothing at all is no trouble
		SearchCheckHashHit(nil);
		word = nil;
		SearchCheckHashHit(&word);
		EXPECT(word == nil);
	}

	// ---- everything given back ----
	{
		SearchDeallocateGlobals();
		EXPECT(gSearchAllocated == 0);
		EXPECT(gSearchColumns == nil);
		EXPECT(wordTails == nil && wordLists == nil);
		// twice is no trouble
		SearchDeallocateGlobals();
	}

	if (failures == 0)
		printf("test_Search: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
