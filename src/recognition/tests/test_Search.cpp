// The lexical search's state and its life (recognition/Search.h): the
// thirty-seven columns the Viterbi walks, the readings each of them
// keeps, and what happens to a word on the way out.
#include "Search.h"
#include "RosEngine.h"
#include "Segment.h"
#include "ROMDictionaryData.h"
#include "ROMImport.h"
#include "NewtErrors.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static Fixed	F(long n)		{ return (Fixed) (int) ((unsigned int) n << 16); }


// The ROM's General grammar with its lexicons found, which is the state
// `RosettaSetArea` leaves a grammar in and the only one the search may
// be run in: until then a kind of word names its lexicon by its place in
// `gROMDictionaryData` rather than pointing at it.
static const BiGrammar*
Resolved(void)
{
	static BiGrammar* cached = nil;
	if (cached == nil)
	{
		cached = BiGrammarClone(ROMGrammar.fContexts[0]);
		for (long i = 0; i < cached->fCount; i++)
		{
			BiGSlice* slice = (BiGSlice*) cached->fSlices[i];
			slice->fDictionary = (ULong) gROMDictionaryData[slice->fDictionary];
		}
	}
	return cached;
}

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


// A character put on the front of a reading, the way
// `SearchDoVStepFromNode` does it through `RegisterNewPath`.
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
	// the lexicons are in the ROM's own bytes
	if (ImportROMObjectsFromFile(NEWTON_ROM_BIN) != noErr)
	{
		printf("test_Search: cannot import %s\n", NEWTON_ROM_BIN);
		return 1;
	}
	InitROMDictionaryData();

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
		EXPECT(first->fNodes[0]->fSlice == nil);
		EXPECT(first->fNodes[0]->fSegment == nil);
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
		SearchBeginWord(Resolved());

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
		FPoint pts[2];
		pts[0].x = F(0);	pts[0].y = F(0);
		pts[1].x = F(4);	pts[1].y = F(20);
		RosStroke* ink = StrokeCreate(2, pts);
		SegmentStrokeData(ink, 0, 0, 0);
		RosStroke* one[1];
		one[0] = ink;
		SegmentSetStrokes(seg, 1, one);
		seg->fFirstStroke = 0;
		seg->fCount = 1;
		seg->fRealCount = 1;
		SegmentBoundsDotsEtc(seg);

		SearchProcessSegment(Resolved(), probs, scratch, 0, seg,
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

		// ... and the step really read something: the column at the
		// front now holds readings, each one a single letter the
		// lexicons allow a word to begin with and the classifier can
		// see in the stroke
		SearchColumn* filled = gSearchColumns[0];
		EXPECT(filled->fCount > 0);
		EXPECT(filled->fCost < 0x7ffe);
		// ... but none of them is a whole word yet, because no lexicon
		// has a word of one letter that the stroke could be, so
		// `StoreFinalPaths` had nothing to hand out
		EXPECT(filled->fWords == nil);
		// `GetBestPath` is the try string the Newton shows while you
		// are still writing: one letter, and one of the two the
		// classifier gave anything to
		char tried[64];
		GetBestPath(tried, 0);
		EXPECT(strlen(tried) == 1);
		EXPECT(tried[0] == 't' || tried[0] == 'T' || tried[0] == '+');

		SegmentDestroy(seg);
		StrokeDestroy(ink);
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

	// ---- the best readings gathered out of the columns ----
	{
		// the columns are filled by hand here, so that what
		// `SearchBestWords` makes of them can be said exactly - a
		// reading is a word tail and what it cost
		SearchBeginWord(Resolved());
		const BiGSlice* slice = Resolved()->fSlices[0];

		// three readings in one column: `cat` cheapest, then `cot`,
		// then `car`
		SearchColumn* col = gSearchColumns[0];
		col->fCount = 0;
		col->fJump = 0;
		col->fCost = 0;
		col->fAltCost = 0;
		static const char* const kWords[3] = { "cot", "cat", "car" };
		static const short kCosts[3] = { 900, 400, 1500 };
		for (long i = 0; i < 3; i++)
		{
			SearchNode* node = col->fNodes[col->fCount++];
			WordTailRef tail = kWordTailNone;
			for (const char* p = kWords[i]; *p != 0; p++)
				tail = Push(tail, (UByte) *p);
			node->fTail = tail;
			node->fScore = kCosts[i];
			node->fSlice = slice;
			node->fField04 = (long) 0x80000000;	// so nothing is added for the kind
			node->fSegment = nil;
		}

		long best[30];
		Fixed a = 0, b = 0;
		long found = SearchFindBest(best, &a, &b, 10, 0, 0);
		EXPECT(found == 3);
		// best first, and the best comes out at nothing with the rest
		// priced against it
		EXPECT(best[2] == 0);
		EXPECT(best[5] == 900 - 400);
		EXPECT(best[8] == 1500 - 400);
		// ... and they are the readings we put in, in order
		UByte text[64];
		WordTailSprint(col->fNodes[best[1]]->fTail, text, 64);
		EXPECT(strcmp((const char*) text, "cat") == 0);
		WordTailSprint(col->fNodes[best[4]]->fTail, text, 64);
		EXPECT(strcmp((const char*) text, "cot") == 0);
		WordTailSprint(col->fNodes[best[7]]->fTail, text, 64);
		EXPECT(strcmp((const char*) text, "car") == 0);
		// what the cheapest reading cost altogether
		EXPECT(b == 400);

		// **the same text found twice is one reading.**  Two paths
		// through the lattice can spell the same word - `cl` and `d`
		// written the same way - and the cheaper spelling wins rather
		// than the word appearing twice in the list.
		SearchNode* again = col->fNodes[col->fCount++];
		WordTailRef same = kWordTailNone;
		for (const char* p = "cat"; *p != 0; p++)
			same = Push(same, (UByte) *p);
		again->fTail = same;
		again->fScore = 100;					// cheaper than the first `cat`
		again->fSlice = slice;
		again->fField04 = (long) 0x80000000;
		again->fSegment = nil;
		EXPECT(again->fTail != col->fNodes[1]->fTail);	// spelled out separately

		found = SearchFindBest(best, &a, &b, 10, 0, 0);
		EXPECT(found == 3);						// still three, not four
		// and it is the cheaper of the two that is kept
		EXPECT(best[1] == 3);
		WordTailSprint(col->fNodes[best[1]]->fTail, text, 64);
		EXPECT(strcmp((const char*) text, "cat") == 0);
		EXPECT(b == 100);

		// only as many as were asked for
		found = SearchFindBest(best, &a, &b, 2, 0, 0);
		EXPECT(found == 2);

		// ---- and put on the column as a set of alternatives ----
		WordListFreeAll();
		SearchSegwordRememberNBest(col, 3, 0x10000);
		WordList* list = col->fWords;
		EXPECT(list != nil);
		EXPECT(list->fCount == 3);
		EXPECT(list->fRefCount == 1);
		EXPECT(list->fStrokes == 3);
		// each alternative keeps its text and what it cost
		EXPECT(list->fScores[0] == 0);
		EXPECT(list->fScores[1] > 0);
		WordTailSprint(list->fTails[0], text, 64);
		EXPECT(strcmp((const char*) text, "cat") == 0);
		// ... and holds a reference to it, so the text survives the
		// columns moving on
		EXPECT(WordTailAt(list->fTails[0])->fRefCount >= 2);
		// which is what makes the whole list print as a bullet
		EXPECT(WordListSprint(list, text, 64) == 4);
		EXPECT(text[3] == kWordListMark);

		// ---- and written out as text ----
		char* gotWords[10];
		UniChar gotScores[10];
		long gotFlags[10];
		for (long i = 0; i < 10; i++)
			gotWords[i] = nil;
		long said = SearchBestWords(gotWords, gotScores, gotFlags, 10, 0);
		EXPECT(said == 3);
		EXPECT(gotWords[0] != nil);
		EXPECT(strcmp(gotWords[0], "cat") == 0);
		EXPECT(strcmp(gotWords[1], "cot") == 0);
		EXPECT(strcmp(gotWords[2], "car") == 0);
		// best first, and each reading says which lexicon it came from
		EXPECT(gotScores[0] <= gotScores[1]);
		EXPECT(gotScores[1] <= gotScores[2]);
		EXPECT(gotFlags[0] == (long) slice->fDictionary);
		// the strings are the engine's own, out of the return cache
		EXPECT(gotWords[0] == (char*) gSearchReturnCache[0]);
		// asking for fewer gets fewer
		EXPECT(SearchBestWords(gotWords, gotScores, gotFlags, 2, 0) == 2);
		// and `GetBestPath` is the same thing into a buffer - the try
		// string the Newton shows while you are still writing
		char tryString[64];
		GetBestPath(tryString, 0);
		EXPECT(strcmp(tryString, "cat") == 0);

		col->fWords = nil;
		col->fCount = 0;
	}

	// ---- a reading put back into a column ----
	{
		// `RegisterNewPath` is where the beam is kept varied.  Two
		// kinds of word are set up here: one the grammar limits to two
		// readings a column, and one it does not limit at all.
		SearchBeginWord(Resolved());
		SearchColumn* col = gSearchColumns[0];
		col->fCount = 0;
		for (long i = 0; i < 10; i++)
			col->fClassCounts[i] = 0;

		// (the grammar is the ROM's own, so its limits are read rather
		//  than invented: class 0 is what `LexicalSymbols` carries)
		BiGSlice limited;
		BiGSlice open;
		memset(&limited, 0, sizeof(limited));
		memset(&open, 0, sizeof(open));
		limited.fField10 = 0x10;			// counted against a class
		limited.fField2c = 0;
		open.fField10 = 0;					// not counted at all
		open.fField2c = 0;
		long limit = (long) gSearchGrammar->fClassLimits[0];

		SearchStep step;
		memset(&step, 0, sizeof(step));
		step.fColumn = col;
		step.fBest = 0x7ffe;
		step.fSlice = &open;

		// while there is room it is a plain insertion, cheapest first
		EXPECT(RegisterNewPath(&step, 500, 0) != nil);
		EXPECT(RegisterNewPath(&step, 100, 0) != nil);
		EXPECT(RegisterNewPath(&step, 300, 0) != nil);
		EXPECT(col->fCount == 3);
		EXPECT(col->fNodes[0]->fScore == 100);
		EXPECT(col->fNodes[1]->fScore == 300);
		EXPECT(col->fNodes[2]->fScore == 500);
		// ... and the column remembers its cheapest
		EXPECT(step.fBest == 100);
		// an unlimited kind is not counted against any class
		EXPECT(col->fClassCounts[0] == 0);

		// a limited kind is counted
		step.fSlice = &limited;
		EXPECT(RegisterNewPath(&step, 400, 0) != nil);
		EXPECT(col->fClassCounts[0] == 1);
		EXPECT(col->fNodes[1]->fScore == 300 && col->fNodes[2]->fScore == 400);

		// fill the column right up with the unlimited kind
		step.fSlice = &open;
		while ((long) col->fCount < MaxBestNodes)
			EXPECT(RegisterNewPath(&step, 1000, 0) != nil);
		EXPECT((long) col->fCount == MaxBestNodes);

		// now it is full.  A reading of the unlimited kind that is
		// dearer than everything in the column is refused...
		EXPECT(RegisterNewPath(&step, 0x7ffd, 0) == nil);

		// ... but one of the *limited* kind, which still has room
		// under its quota, is taken however dear it is.  That is the
		// whole point: a column that has room for another date takes
		// one rather than keeping a twenty-eighth word.
		EXPECT((long) col->fClassCounts[0] < limit);
		step.fSlice = &limited;
		SearchNode* got = RegisterNewPath(&step, 0x7ffd, 0);
		EXPECT(got != nil);
		EXPECT(got->fScore == 0x7ffd);
		EXPECT(col->fClassCounts[0] == 2);
		EXPECT((long) col->fCount == MaxBestNodes);		// still full

		// and once that kind is at its quota it has to compete on
		// price like everything else
		while ((long) col->fClassCounts[0] < limit)
			RegisterNewPath(&step, 1200, 0);
		EXPECT((long) col->fClassCounts[0] >= limit);
		EXPECT(RegisterNewPath(&step, 0x7ffd, 0) == nil);
		EXPECT(RegisterNewPath(&step, 50, 0) != nil);
		EXPECT(col->fNodes[0]->fScore == 50);			// straight to the front

		col->fCount = 0;
		for (long i = 0; i < 10; i++)
			col->fClassCounts[i] = 0;
	}

	// ---- the readings finished off ----
	{
		// During a step a reading in the new column is only a
		// backtrace: the node it grew from and the letter that was
		// added.  `StoreFinalPaths` turns those into real word tails
		// at the end - so a reading dropped part way through never
		// costs a cell at all.
		SearchBeginWord(Resolved());
		SearchColumn* from = gSearchColumns[1];
		SearchColumn* col = gSearchColumns[0];

		// something already read: "ca"
		from->fCount = 1;
		SearchNode* older = from->fNodes[0];
		older->fTail = Push(Push(kWordTailNone, 'c'), 'a');
		UByte text[64];
		EXPECT(strcmp(Text(older->fTail, text), "ca") == 0);
		UByte was = WordTailAt(older->fTail)->fRefCount;

		// two readings grown from it, by `t` and by `r`
		col->fCount = 2;
		col->fNodes[0]->fScore = 700;
		col->fNodes[1]->fScore = 900;
		gSearchBest[0]->fFrom = older;
		gSearchBest[0]->fChar = 't';
		gSearchBest[1]->fFrom = older;
		gSearchBest[1]->fChar = 'r';

		StoreFinalPaths(col, 700);
		// the scores are measured from the column's own cheapest
		EXPECT(col->fNodes[0]->fScore == 0);
		EXPECT(col->fNodes[1]->fScore == 200);
		// ... and each backtrace has become a real reading
		EXPECT(strcmp(Text(col->fNodes[0]->fTail, text), "cat") == 0);
		EXPECT(strcmp(Text(col->fNodes[1]->fTail, text), "car") == 0);
		// both of which share the "ca" rather than copying it
		EXPECT(WordTailAt(col->fNodes[0]->fTail)->fNext == older->fTail);
		EXPECT(WordTailAt(col->fNodes[1]->fTail)->fNext == older->fTail);
		EXPECT(WordTailAt(older->fTail)->fRefCount == was + 2);

		col->fCount = 0;
		from->fCount = 0;
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
