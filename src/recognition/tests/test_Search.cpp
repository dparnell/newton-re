// The lexical search's state and its life (recognition/Search.h): the
// thirty-seven columns the Viterbi walks, the readings each of them
// keeps, and what happens to a word on the way out.
#include "Search.h"
#include "RosEngine.h"
#include "Segment.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


// A character put on the front of a reading (the search's own
// `SearchDoVStepFromNode` does this; it is NOT YET).
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

	// ---- the columns are a ring ----
	{
		SearchAllocateGlobals();
		// nothing is ever copied: the pointers rotate, so a column
		// keeps its nodes wherever it ends up in the word
		SearchColumn* was[kSearchColumns];
		for (long i = 0; i < kSearchColumns; i++)
		{
			was[i] = gSearchColumns[i];
			gSearchColumns[i]->fCount = 0;
			gSearchColumns[i]->fWords = nil;
		}
		ShiftNetValues();
		// the last becomes the first and everything else moves back
		EXPECT(gSearchColumns[0] == was[kSearchColumns - 1]);
		for (long i = 1; i < kSearchColumns; i++)
			EXPECT(gSearchColumns[i] == was[i - 1]);
		// ... and the new first column is emptied on the way
		EXPECT(gSearchColumns[0]->fWords == nil);

		// all the way round brings it back
		for (long i = 1; i < kSearchColumns; i++)
			ShiftNetValues();
		for (long i = 0; i < kSearchColumns; i++)
			EXPECT(gSearchColumns[i] == was[i]);
	}

	// ---- one candidate letter offered to the search ----
	{
		CharInitialize(0);
		SearchBeginWord(ROMGrammar.fContexts[0]);

		// the classifier's probabilities, and what CharModifyProbs
		// would have made of them
		static Fixed probs[256];
		static Fixed scratch[256];
		for (long i = 0; i < 256; i++)
		{
			probs[i] = 0;
			scratch[i] = 0;
		}
		probs['t'] = 0xe500;			// as the classifier really answers
		probs['+'] = 0xf100;
		scratch['t'] = 0xe500;
		scratch['+'] = 0xf100;

		RosSegment* seg = SegmentCreate();
		seg->fFirstStroke = 0;
		seg->fCount = 1;
		seg->fRealCount = 1;

		SearchProcessSegment(ROMGrammar.fContexts[0], probs, scratch, 0, seg,
						0x8000, false, nil);

		// the two score arrays the Viterbi step reads.  A code the
		// classifier gave nothing costs never...
		const short* fromProbs = (const short*) gSearchScratchA;
		const short* fromScratch = (const short*) gSearchScratchB;
		EXPECT(fromProbs['x'] == (short) kArProbNever);
		// ... and one it believed in costs the score scaled by the
		// four fifths `rosCI` says the classifier is worth
		EXPECT(RosCI->fNetScoreWeight == 0xcccc);
		EXPECT(fromProbs['t']
			== (short) ((RosCI->fNetScoreWeight * (long) ArProbEncode(0xe500)) >> 16));
		// the more likely letter costs less
		EXPECT(fromProbs['+'] < fromProbs['t']);
		EXPECT(fromProbs['+'] > 0);
		// the other array is the same score quartered
		EXPECT(fromScratch['t'] == (short) (ArProbEncode(0xe500) >> 2));
		EXPECT(fromScratch['x'] == (short) (kArProbNever >> 2));

		// and a score really is a logarithm, scaled by five hundred: a
		// letter the classifier gave half the probability of another
		// costs -ln(0.5) x 500 = 346 more
		EXPECT(ArProbEncode(0x10000) == 0);
		EXPECT(ArProbEncode(0x8000) == 346);
		EXPECT(ArProbEncode(0x0080) == 3119);		// one in 512

		SegmentDestroy(seg);
	}

	// ---- what a reading looks like from outside ----
	{
		// `CapHackDetermineContext` is the other half of the capitals
		// hack: `CharModifyProbs` leans a letter towards its capital
		// by height, and this says what having written one *means* for
		// whatever comes next.
		SearchColumn* first = gSearchColumns[0];
		SearchNode* node = first->fNodes[0];
		node->fField04 = 0;

		// a reading that has come to a set of alternatives has no
		// context at all
		node->fTail = (WordTailRef) (kWordTailListBase + 1);
		EXPECT(CapHackDetermineContext(node) == 0);

		// lower case
		node->fTail = Push(kWordTailNone, 'a');
		EXPECT(CapHackDetermineContext(node) == 1);
		// one capital, and two in a row - `Mc` is a name and `MC` is
		// an abbreviation, and the grammar charges differently for
		// what may follow them
		WordTailRef cap = Push(kWordTailNone, 'M');
		node->fTail = cap;
		EXPECT(CapHackDetermineContext(node) == 3);
		node->fTail = Push(cap, 'C');
		EXPECT(CapHackDetermineContext(node) == 2);
		node->fTail = Push(cap, 'c');
		EXPECT(CapHackDetermineContext(node) == 1);

		// an apostrophe after a lower-case letter is inside a word,
		// not after one, so `don't` reads as one word
		WordTailRef inWord = Push(Push(kWordTailNone, 'n'), '\'');
		node->fTail = inWord;
		EXPECT(CapHackDetermineContext(node) == 1);
		// ... but one after anything else is not
		WordTailRef after = Push(Push(kWordTailNone, '5'), '\'');
		node->fTail = after;
		EXPECT(CapHackDetermineContext(node) != 1);

		// and the twelve are the same six twice over
		node->fTail = Push(kWordTailNone, 'a');
		node->fField04 = 2;
		EXPECT(CapHackDetermineContext(node) == 7);
		node->fField04 = 0;
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
