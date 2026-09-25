/*
	File:		recognition/Search.cpp

	Contains:	The lexical search's state and its life - see Search.h.

	Reconstructed from the MP2x00 US ROM; each function cites its
	origin.
*/

#include "Search.h"
#include "LELang.h"
#include "GeoContext.h"
#include "RosEngine.h"
#include "RosStrokes.h"			// kRosettaMemoryTag
#include "NewtonMemory.h"
#include "NewtonExceptions.h"

extern const ExceptionName exRosetta;	// ROM 0x003774f8 exRosetta
#include "Segment.h"
#include "FixedMath.h"

#include <string.h>


// ROM 0x0c101a8c MaxBestNodes
// How many readings a column keeps.  There is room for thirty.
long	MaxBestNodes = 27;

// ROM 0x0c101a90 (unnamed)
// Which of the two cases of each character code is reachable here.
Ptr		gSearchScratch = nil;

// ROM 0x0c106ec8 (unnamed)
// The pseudo-node a finished word is grown on from.
SearchNode	gSearchWordListNode = { nil, 0, 0, kWordTailNone, nil };
// ROM 0x0c101a94 (unnamed)
UByte	gSearchAllocated = 0;
// ROM 0x0c101a98 (unnamed)
// `MaxBestNodes` pointers into the block below, eight bytes apart.
SearchBestEntry**	gSearchBest = nil;
// ROM 0x0c101a9c (unnamed)
SearchColumn**	gSearchColumns = nil;
// ROM 0x0c101aa0 (unnamed)
const BiGrammar*	gSearchGrammar = nil;
// ROM 0x0c101aa4 (unnamed)
// Room for the readings on the way back out: pointers into one block of
// 0x24-byte entries.  It is grown when a caller wants more and never
// shrunk.
Ptr*	gSearchReturnCache = nil;
// ROM 0x0c101aa8 (unnamed)
long	gSearchReturnCacheSize = 0;
// ROM 0x0c101aac (unnamed)
Ptr		gSearchBestBlock = nil;
// ROM 0x0c101ab0 (unnamed)
SearchColumn*	gSearchColumnBlock = nil;
// ROM 0x0c101ab4 (unnamed)
// One block of nodes per column.
Ptr*	gSearchNodeBlocks = nil;
// ROM 0x0c101ab8 (unnamed)
Ptr		gSearchScratchA = nil;
// ROM 0x0c101abc (unnamed)
Ptr		gSearchScratchB = nil;

// ROM 0x0c101ac0 (unnamed)
// How many times each of the eight has been written in a row.
UByte	gSearchEasterCounts[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
// ROM 0x0c101ac8 (unnamed)
long	gSearchEasterMatch = -1;

extern const ExceptionName exOutOfStack;


// ROM 0x001d0014 SearchAllocateGlobals
// The whole of the search's memory, made once and kept for the life of
// the engine: thirty-seven columns, a block of nodes for each of them,
// and the scratch the Viterbi step works in.
//
// Everything is carved out of a handful of big blocks with arrays of
// pointers into them, which is how the ROM avoids thirty-seven separate
// allocations in the middle of reading a word.
//
// DEVIATION: the ROM's pointers are four bytes and the host's are
// eight, so the arrays and the column block are sized from `sizeof`
// rather than from the ROM's 0x94 and 0x1564.
void
SearchAllocateGlobals(void)
{
	if (gSearchAllocated != 0)
		return;

	newton_try
	{
		gSearchAllocated = 1;

		gSearchNodeBlocks = (Ptr*) NewPtrClear(kSearchColumns * (long) sizeof(Ptr));
		if (gSearchNodeBlocks == nil)
			Throw(exOutOfStack, (void*) "", nil);
		SetPtrName((Ptr) gSearchNodeBlocks, kRosettaMemoryTag);

		gSearchBestBlock = (Ptr) RosAllocate(
							MaxBestNodes * (long) sizeof(SearchBestEntry));
		gSearchBest = (SearchBestEntry**) RosAllocate(
							MaxBestNodes * (long) sizeof(SearchBestEntry*));
		for (long i = 0; i < MaxBestNodes; i++)
			gSearchBest[i] = &((SearchBestEntry*) gSearchBestBlock)[i];

		gSearchColumnBlock = (SearchColumn*) RosAllocate(
							kSearchColumns * (long) sizeof(SearchColumn));
		gSearchColumns = (SearchColumn**) RosAllocate(
							kSearchColumns * (long) sizeof(SearchColumn*));
		for (long i = 0; i < kSearchColumns; i++)
		{
			gSearchColumns[i] = &gSearchColumnBlock[i];
			SearchNode* nodes = (SearchNode*) RosAllocate(
								MaxBestNodes * (long) sizeof(SearchNode));
			gSearchNodeBlocks[i] = (Ptr) nodes;
			for (long j = 0; j < MaxBestNodes; j++)
				gSearchColumns[i]->fNodes[j] = &nodes[j];
		}

		gSearchScratchA = (Ptr) RosAllocate(0x200);
		gSearchScratchB = (Ptr) RosAllocate(0x200);
		gSearchScratch = (Ptr) RosAllocate(0x100);
	}
	cleanup
	{
		SearchDeallocateGlobals();
	}
	end_try;
}


// ROM 0x001d02ec SearchDeallocateGlobals
void
SearchDeallocateGlobals(void)
{
	if (gSearchAllocated == 0)
		return;
	gSearchAllocated = 0;

	if (gSearchNodeBlocks != nil)
	{
		for (long i = 0; i < kSearchColumns; i++)
			if (gSearchNodeBlocks[i] != nil)
			{
				DisposPtr(gSearchNodeBlocks[i]);
				gSearchNodeBlocks[i] = nil;
			}
		DisposPtr((Ptr) gSearchNodeBlocks);
		gSearchNodeBlocks = nil;
	}

	if (gSearchColumns != nil)
		DisposPtr((Ptr) gSearchColumns);
	if (gSearchColumnBlock != nil)
		DisposPtr((Ptr) gSearchColumnBlock);
	if (gSearchBest != nil)
		DisposPtr((Ptr) gSearchBest);
	if (gSearchBestBlock != nil)
		DisposPtr(gSearchBestBlock);
	gSearchColumns = nil;
	gSearchColumnBlock = nil;
	gSearchBest = nil;
	gSearchBestBlock = nil;

	if (gSearchScratchA != nil)
		DisposPtr(gSearchScratchA);
	if (gSearchScratchB != nil)
		DisposPtr(gSearchScratchB);
	gSearchScratchA = nil;
	gSearchScratchB = nil;
	if (gSearchScratch != nil)
		DisposPtr(gSearchScratch);
	gSearchScratch = nil;

	if (gSearchReturnCache != nil)
	{
		DisposPtr(gSearchReturnCache[0]);
		DisposPtr((Ptr) gSearchReturnCache);
		gSearchReturnCache = nil;
	}
	gSearchReturnCacheSize = 0;

	// (the ROM has `WordTailDeallocateGlobals` inlined here)
	WordTailDeallocateGlobals();
}


// ROM 0x001cea78 SearchAllocateReturnCache
// Room for `count` readings on the way back out.  It only ever grows:
// a word that wanted thirty leaves the room there for the next one.
void
SearchAllocateReturnCache(long count)
{
	if (count <= gSearchReturnCacheSize)
		return;
	if (gSearchReturnCache != nil)
	{
		DisposPtr(gSearchReturnCache[0]);
		DisposPtr((Ptr) gSearchReturnCache);
	}

	gSearchReturnCache = (Ptr*) NewNamedPtr(count * (long) sizeof(Ptr),
									kRosettaMemoryTag);
	if (gSearchReturnCache == nil)
		Throw(exOutOfStack, (void*) "", nil);

	Ptr block = nil;
	newton_try
	{
		block = (Ptr) RosAllocate(count * 0x24);
	}
	cleanup
	{
		DisposPtr((Ptr) gSearchReturnCache);
		gSearchReturnCache = nil;
	}
	end_try;

	for (long i = 0; i < count; i++)
		gSearchReturnCache[i] = block + i * 0x24;
	gSearchReturnCacheSize = count;
}


// ROM 0x001ce008 SearchBeginWord
// A word about to be read.  Every column is emptied, and the first of
// them is given one node with nothing read yet - the empty reading
// that every path through the lattice grows from.
void
SearchBeginWord(const BiGrammar* grammar)
{
	gSearchGrammar = grammar;
	if (gSearchAllocated == 0)
		SearchAllocateGlobals();
	WordListFreeAll();

	for (long i = 1; i < kSearchColumns; i++)
	{
		gSearchColumns[i]->fCount = 0;
		gSearchColumns[i]->fWords = nil;
	}

	SearchColumn* first = gSearchColumns[0];
	first->fCount = 0;
	first->fJump = 0;
	first->fRealCount = 0;
	first->fCost = 0;
	first->fAltCost = 0;
	first->fWords = nil;
	for (long i = 0; i < 10; i++)
		first->fClassCounts[i] = 0;

	SearchNode* node = first->fNodes[first->fCount];
	first->fCount = (UByte) (first->fCount + 1);
	node->fSlice = nil;
	node->fField04 = 0;
	node->fScore = 0;
	node->fTail = kWordTailNone;
	node->fSegment = nil;
}


// ROM 0x001d0738 GCBestNodes
// One column's readings given back: the word list it ended up with,
// and the tail each of its nodes was holding.
void
GCBestNodes(SearchColumn* column)
{
	if (column->fWords != nil)
	{
		WordListDeleteRef(column->fWords);
		column->fWords = nil;
	}
	if (column->fCount == 0)
		return;
	for (long i = 0; i < column->fCount; i++)
		WordTailDeleteRef(column->fNodes[i]->fTail);
}


// ROM 0x001d0660 SearchEndWord
// A word finished: the best readings that reach the end are gathered
// into the first column's word list and handed to `proc`, and then
// everything the search was holding goes back.
void
SearchEndWord(const BiGrammar* /*grammar*/, long strokes, SearchEndWordProc proc,
			WordRecog* wr, char** words, UniChar* scores, long* flags,
			long count)
{
	SearchSegwordRememberNBest(gSearchColumns[0], strokes, 0x00010000);
	SearchSendWords(gSearchColumns[0]->fWords, strokes, proc, wr, words, scores,
				flags, count);
	for (long i = 0; i < kSearchColumns; i++)
		GCBestNodes(gSearchColumns[i]);
	GeoContextClearCache();
	// (the ROM has `WordTailDeallocateGlobals` inlined here too)
	WordTailDeallocateGlobals();
}


// ROM 0x001d0d90 SearchCheckHashHit
// **An easter egg**, and the only thing in the engine that is not
// about reading handwriting.
//
// Every reading on its way out is compared against eight words.  Write
// one of them three times in a row and the engine answers something
// else instead - the addresses and names of the people who built the
// Newton's handwriting recognition, and one restaurant.  `Rosetta!`
// answers "Hey, that's me!".
//
// The counts are kept per word and every one but the matching word is
// cleared on each reading, so the three have to be consecutive.  Note
// that the whole `kSearchEasterWords` table is walked even after a
// match, and the counter is bumped for *every* entry that compares
// equal - which is harmless, because the eight are distinct.
void
SearchCheckHashHit(char** word)
{
	if (word == nil)
		return;
	if (*word == nil)
		return;

	gSearchEasterMatch = -1;
	for (long i = 0; i < 8; i++)
		if (strcmp(kSearchEasterWords[i], *word) == 0)
		{
			gSearchEasterCounts[i] = (UByte) (gSearchEasterCounts[i] + 1);
			gSearchEasterMatch = i;
		}

	if (gSearchEasterMatch != -1 && gSearchEasterCounts[gSearchEasterMatch] > 2)
	{
		*word = (char*) kSearchEasterReplies[gSearchEasterMatch];
		gSearchEasterMatch = -1;
	}

	// everything but the word just seen starts again
	long keep = gSearchEasterMatch;
	for (long i = 0; i < 8; i++)
		if (i != keep)
			gSearchEasterCounts[i] = 0;
}


// ROM 0x001cff18 CapHackDetermineContext
// What a partial reading looks like from the outside, in twelve
// classes, so that the grammar can charge differently for what may
// follow it.
//
// It is the **capitals hack** again, and this is the other half of it:
// `CharModifyProbs` leans a letter towards its capital by height, and
// this says what having written a capital *means* for the next one.
// One capital is a different context from two in a row - `Mc` is a
// name and `MC` is an abbreviation, and what may follow them differs -
// and an apostrophe after a lower-case letter is read as part of the
// word rather than the end of it, so that `don't` is one word.
//
// The twelve are six classes twice over: a node whose `fField04` is 2
// takes the upper six.  A reading that has come to a word list rather
// than a tail has no context at all.
long
CapHackDetermineContext(const SearchNode* node)
{
	WordTailRef tail = node->fTail;
	if (tail >= kWordTailListBase)
		return 0;

	long base = (node->fField04 == 2) ? 6 : 0;
	UByte last = WordTailAt(tail)->fChar;
	const UByte* flags = RosCI->fCapCaseFlags;

	if ((flags[last] & 1) != 0)
	{
		// a capital - and two in a row is a different thing again
		WordTailRef before = WordTailAt(tail)->fNext;
		if (before < kWordTailListBase
			&& (flags[WordTailAt(before)->fChar] & 1) != 0)
			return base + 2;
		return base + 3;
	}

	if ((flags[last] & 2) == 0)
	{
		if ((flags[last] & 4) != 0)
			return base + 5;
		// an apostrophe with a lower-case letter in front of it is
		// inside a word, not after one
		WordTailRef before = WordTailAt(tail)->fNext;
		if (last != '\''
			|| before >= kWordTailListBase
			|| (flags[WordTailAt(before)->fChar] & 2) == 0)
			return base + 4;
	}
	return base + 1;
}

#pragma mark -
/*--------------------------------------------------------------------
	The search itself: the Viterbi step that walks the lattice, the
	gathering of the best paths at the end, and the scoring that ties
	the classifier, the grammar and the dictionaries together - about
	6 KB in seven functions.
--------------------------------------------------------------------*/






// ROM 0x001d06fc ShiftNetValues
// The columns moved along by one.  They are a **ring**: nothing is
// copied, the pointers are rotated so that the last column becomes the
// first, and the first is then emptied.  Column 0 is always "here",
// column 1 is one stroke back, and so on to thirty-six - which is why
// the search can look back over a whole word without ever moving a
// node.
void
ShiftNetValues(void)
{
	SearchColumn* last = gSearchColumns[kSearchColumns - 1];
	for (long i = kSearchColumns - 1; i > 0; i--)
		gSearchColumns[i] = gSearchColumns[i - 1];
	gSearchColumns[0] = last;

	// (the ROM has `GCBestNodes` inlined here)
	if (last->fWords != nil)
	{
		WordListDeleteRef(last->fWords);
		last->fWords = nil;
	}
	if (last->fCount == 0)
		return;
	for (long i = 0; i < last->fCount; i++)
		WordTailDeleteRef(last->fNodes[i]->fTail);
}


// ROM 0x001d0798 GetBestPath
// The best reading so far, copied into the caller's buffer.  This is
// the **try string** - what the Newton shows you while you are still
// writing, before the word is finished.
void
GetBestPath(char* out, UByte how)
{
	if (out == nil)
		return;
	char* best = nil;
	if (SearchBestWords(&best, 0, 0, 1, how) < 1)
		out[0] = 0;
	else
		strcpy(out, best);
}


// ROM 0x001ce830 SearchProcessSegment
// One candidate letter offered to the search.
//
// The classifier left a probability for each of the 256 character
// codes in `probs`, and `CharModifyProbs` left what it made of them in
// `scratch`.  Both are turned into **scores** here - negative
// logarithms, which is the currency everything above this works in -
// into two arrays the Viterbi step then reads:
//
// * from `scratch`, the score quartered, which is what the search
//   charges for the letter itself;
// * from `probs`, the score scaled by `rosCI`'s 0.8, with nought
//   meaning never.
//
// While it is about it, the probabilities of the second are added up,
// by turning each stored score back into a probability again.  That
// total is how much the classifier believes in this piece of writing
// at all, and it goes to the Viterbi step as one more score.
//
// Then the columns move along (`ShiftNetValues`), the step runs, and -
// if the caller wants one - the best reading so far is copied out as
// the try string.
void
SearchProcessSegment(const BiGrammar* /*grammar*/, Fixed* probs, Fixed* scratch,
				long index, RosSegment* segment, Fixed confidence,
				Boolean endsWord, char* tryString)
{
	short* fromProbs = (short*) gSearchScratchA;
	short* fromScratch = (short*) gSearchScratchB;
	ULong ends = (ULong) endsWord & 0xff;

	Fixed total = 0;
	for (long code = 0; code < 256; code++)
	{
		// what the search charges for the letter itself
		fromScratch[code] = (short) ((ArProbEncode(scratch[code]) << 14) >> 16);

		Fixed p = probs[code];
		if (p == 0)
		{
			fromProbs[code] = (short) kArProbNever;
			continue;
		}
		fromProbs[code] = (short) ((RosCI->fNetScoreWeight * (long) ArProbEncode(p)) >> 16);

		// ... read back and turned into a probability again, so that
		// the total says how much the classifier believes in this
		// piece of writing at all
		ULong stored = (ULong) (UShort) fromProbs[code];
		ULong at = stored * 4;
		Fixed back;
		if (at < (ULong) kArProbMaxScore)
			back = (stored == 0) ? 0x00010000 : (Fixed) ArProbDecodeLu[at >> 3];
		else
			back = 0;
		total += back;
	}
	if (total > 0x00010000)
		total = 0x00010000;

	// a piece that starts partway through the word and stands for
	// exactly one letter is a place a reading may end
	if (segment->fFirstStroke > 0 && segment->fRealCount == 1)
		SearchSegwordRememberNBest(gSearchColumns[0], segment->fFirstStroke,
							segment->fSeparation);

	ShiftNetValues();
	SearchDoViterbStep(fromProbs, fromScratch, index, segment, confidence,
					(Boolean) ends, ArProbEncode(total));

	if (tryString != nil)
		GetBestPath(tryString, 0);
}


// ROM 0x001d07f4 SearchFindBest
// The best readings the search is holding, gathered out of the
// columns, best first.  Answers how many it found and leaves a triple
// per reading in `out`: which column, which node in it, and what it
// cost - with the cost of the best subtracted from all of them, so the
// best comes out at nothing.
//
// Readings from different columns are not directly comparable, because
// a column that starts further into the word has had fewer chances to
// spend; each column's `fField88` is what it has cost to reach at all,
// and the least of those is added back as a bias.
//
// The interesting part is that **the same text found twice is one
// reading**.  Two paths through the lattice can spell the same word -
// `cl` and `d` written identically, say - and before inserting, the
// list is searched for a reading whose word tail compares equal.  If
// one is there, the cheaper of the two wins and moves up the list
// rather than appearing twice.  That comparison is free whenever the
// two paths happen to share their tail, which is most of the time.
//
// (The sixth argument is passed by both callers - `1.0` or nought -
//  and never read.  A vestige of the training build.)
long
SearchFindBest(long* out, Fixed* outA, Fixed* outB, long count, UByte flag,
			Fixed /*weight*/)
{
	long found = 0;
	long room = (MaxBestNodes < count) ? MaxBestNodes : count;
	ULong best = 0x7ffe;
	ULong leastCost = 0x7ffe;
	ULong leastOther = 0x7ffe;

	// what the cheapest column has cost to reach
	for (long i = 0; i < kSearchColumns; i++)
	{
		SearchColumn* col = gSearchColumns[i];
		if (i <= (long) col->fJump && col->fCount != 0
			&& (flag == 0 || (long) col->fJump <= i))
		{
			if ((ULong) col->fCost < leastCost)
				leastCost = (ULong) col->fCost;
			if ((ULong) col->fAltCost < leastOther)
				leastOther = (ULong) col->fAltCost;
		}
	}

	for (long i = 0; i < kSearchColumns; i++)
	{
		SearchColumn* col = gSearchColumns[i];
		if (col->fCount == 0)
			continue;
		ULong bias = (ULong) col->fCost - leastCost;
		if (!(i <= (long) col->fJump
			&& (flag == 0 || (long) col->fJump <= i)
			&& bias < 0x7ffe))
			continue;

		for (long j = 0; j < (long) col->fCount; j++)
		{
			SearchNode* node = col->fNodes[j];
			if ((ULong) (UShort) node->fScore >= 0x7ffe - bias)
				continue;
			ULong cost = (ULong) (UShort) node->fScore + bias;

			if (flag != 0)
			{
				// what the kind of word itself costs to end on
				ULong extra = (ULong) (UShort) node->fSlice->fField0a;
				if ((node->fField04 & 0x80000000UL) == 0)
					extra += 0x5d9;
				if (0x7ffe - cost < extra)
					extra = 0x7ffe - cost;
				cost += extra;
			}

			// the list is full and this is dearer than the worst of it
			if (found == room && (ULong) out[(found - 1) * 3 + 2] <= cost)
				break;
			if (cost < best)
				best = cost;

			if (found < 1)
			{
				out[0] = i;
				out[1] = j;
				out[2] = (long) cost;
				found = 1;
				continue;
			}

			// is this same text already in the list?
			WordTailRef tail = node->fTail;
			long same = -1;
			for (long k = 0; k < found; k++)
			{
				SearchColumn* other = gSearchColumns[out[k * 3]];
				if (WordTailCompare(tail, other->fNodes[out[k * 3 + 1]]->fTail) == 0)
				{
					same = k;
					break;
				}
			}

			long at;
			if (same < 0)
			{
				// somewhere new
				long k = found;
				do
				{
					at = k;
					k = at - 1;
					if (k < 0)
						break;
				}
				while (cost <= (ULong) out[k * 3 + 2]);
				if (found < room)
					found++;
				for (long m = found - 2; m >= at; m--)
				{
					out[(m + 1) * 3] = out[m * 3];
					out[(m + 1) * 3 + 1] = out[m * 3 + 1];
					out[(m + 1) * 3 + 2] = out[m * 3 + 2];
				}
			}
			else
			{
				// the same reading is already there, and stays unless
				// this way of spelling it is cheaper
				if ((ULong) out[same * 3 + 2] <= cost)
					continue;
				long e = same - 1;
				at = e;
				while (at >= 0 && cost <= (ULong) out[at * 3 + 2])
					at--;
				at++;
				for (; at <= e; e--)
				{
					out[(e + 1) * 3] = out[e * 3];
					out[(e + 1) * 3 + 1] = out[e * 3 + 1];
					out[(e + 1) * 3 + 2] = out[e * 3 + 2];
				}
			}
			out[at * 3] = i;
			out[at * 3 + 1] = j;
			out[at * 3 + 2] = (long) cost;
		}
	}

	if (room < found)
		found = room;
	// the best reading comes out at nothing and the rest are priced
	// against it
	for (long i = 0; i < found; i++)
		out[i * 3 + 2] = (long) ((ULong) out[i * 3 + 2] - best);
	if (outA != nil)
		*outA = (Fixed) leastOther;
	if (outB != nil)
		*outB = (found < 1) ? 0x7ffe : (Fixed) (leastCost + best);
	return found;
}


// ROM 0x001cf920 SearchSegwordRememberNBest
// The best readings so far taken off the columns and put on a word
// list, which the column then holds.  That is how a point in the
// lattice comes to stand for "one of these ten things", and how the
// engine can hand back a set of alternatives rather than one answer.
//
// Each reading keeps a reference to its word tail, so the text is
// still there after the columns have moved on.
void
SearchSegwordRememberNBest(SearchColumn* column, long strokes, Fixed weight)
{
	long best[kWordListMax * 3];
	Fixed other = 0;
	Fixed cost = 0;
	long found = SearchFindBest(best, &other, &cost, kWordListMax, 1, weight);
	if (found < 1)
	{
		column->fWords = nil;
		return;
	}

	if (freeWordLists == nil)
		Throw(exRosetta, (void*) 1, nil);
	WordList* list = freeWordLists;
	freeWordLists = *(WordList**) freeWordLists;

	list->fStrokes = (UByte) strokes;
	list->fRefCount = 1;
	list->fCost = cost;
	list->fScoreBase = other;
	list->fCount = (UByte) found;
	list->fSegment = gSearchColumns[best[0]]->fNodes[best[1]]->fSegment;

	for (long i = 0; i < found; i++)
	{
		SearchNode* node = gSearchColumns[best[i * 3]]->fNodes[best[i * 3 + 1]];
		list->fFlags[i] = node->fField04;
		ULong score = (ULong) best[i * 3 + 2];
		if (score >= 0x7ffe)
			score = 0x7ffe;
		list->fScores[i] = (short) score;
		list->fTails[i] = node->fTail;
		if (list->fTails[i] != kWordTailNone)
			WordTailAddRef(list->fTails[i]);
	}
	column->fWords = list;
}


// ROM 0x001d0c48 SearchBestWords
// The best readings written out as text, with a score and the
// dictionary each came from.  This is what `GetBestPath` asks for
// while the writing is still going on, and it is the shape the
// readings are finally handed back in.
//
// Each word goes into the return cache, which is why that only ever
// grows: the strings the caller is given are the engine's own, and
// they have to stay valid until it asks again.
long
SearchBestWords(char** words, UniChar* scores, long* flags, long count, UByte how)
{
	long room = (MaxBestNodes < count) ? MaxBestNodes : count;
	SearchAllocateReturnCache(room);

	long triples[kWordListMax * 3 * 3];
	Fixed base = 0;
	long found = SearchFindBest(triples, &base, nil, room, how,
						(how != 0) ? 0x00010000 : 0);

	for (long i = 0; i < found; i++)
	{
		SearchNode* node = gSearchColumns[triples[i * 3]]->fNodes[triples[i * 3 + 1]];
		if (words != nil)
		{
			WordTailSprint(node->fTail, (UByte*) gSearchReturnCache[i], 0x24);
			words[i] = (char*) gSearchReturnCache[i];
		}
		if (scores != nil)
		{
			ULong score = (ULong) (triples[i * 3 + 2] + base);
			if (score >= 0x7ffe)
				score = 0x7ffe;
			scores[i] = (UniChar) score;
		}
		if (flags != nil)
			flags[i] = (long) node->fSlice->fDictionary;
	}

	if (found != 0 && how != 0)
		SearchCheckHashHit(words);
	return found;
}


// ROM 0x001d03f8 SearchSendWords
// The readings handed back, one **word** at a time.
//
// A word list's readings may run into another word list - that is what
// a reference of 0xf000 or more at the far end of a tail means - and
// when they do, the earlier list is sent first.  So a piece of writing
// read as several words comes back as several calls to `proc`, each
// with its own alternatives, its own scores and its own count of
// strokes.  The stroke count and the score base are taken off as the
// recursion goes in, so each call is told only about its own part.
//
// Only the alternatives that end where the first one does are sent:
// the rest belong to a different word.
void
SearchSendWords(WordList* list, long strokes, SearchEndWordProc proc,
			WordRecog* wr, char** words, UniChar* scores, long* flags,
			long count)
{
	if (list == nil || list->fCount == 0)
	{
		ULong reached = (list != nil) ? (ULong) list->fStrokes : (ULong) strokes;
		proc(wr, words, scores, flags, (long) reached, 0);
		return;
	}

	// where the first alternative's reading really ends
	WordTailRef end = list->fTails[0];
	while (end < kWordTailListBase)
		end = WordTailAt(end)->fNext;

	ULong reached = (ULong) list->fStrokes;
	long base = list->fScoreBase;
	if (end != kWordTailNone && end >= kWordTailListBase)
	{
		// it runs into an earlier word: send that one first
		WordList* earlier = WordListAt(end);
		reached -= (ULong) earlier->fStrokes;
		base -= earlier->fScoreBase;
		SearchSendWords(earlier, strokes, proc, wr, words, scores, flags, count);
	}

	if (MaxBestNodes < count)
		count = MaxBestNodes;
	SearchAllocateReturnCache(count);

	long out = 0;
	for (long i = 0; i < (long) list->fCount && out < count; i++)
	{
		WordTailRef tail = list->fTails[i];
		WordTailRef far = tail;
		while (far < kWordTailListBase)
			far = WordTailAt(far)->fNext;
		if (far != end)
			continue;			// this one belongs to a different word

		if (words != nil)
		{
			words[out] = (char*) gSearchReturnCache[out];
			WordTailSprint2(tail, (UByte*) gSearchReturnCache[out], 0x24);
		}
		if (scores != nil)
		{
			ULong score = (ULong) (base + list->fScores[i]);
			if (score >= 0x7ffe)
				score = 0x7ffe;
			scores[out] = (UniChar) score;
		}
		if (flags != nil)
			flags[out] = list->fFlags[i];
		out++;
	}

	if (out != 0)
		SearchCheckHashHit(words);
	proc(wr, words, scores, flags, (long) reached, out);
}


// The limit on how many readings of one kind of word a column may
// hold, out of the grammar.
static long
SearchClassLimit(long cls)
{
	return (long) gSearchGrammar->fClassLimits[cls];
}

// Whether a kind of word is one of the limited ones, and which class
// it counts against.
static Boolean
SearchIsLimited(const BiGSlice* slice)
{
	return (slice->fField10 & 0x10) != 0;
}

static long
SearchClassOf(const BiGSlice* slice)
{
	return (long) (signed char) slice->fField2c;
}


// ROM 0x001cfc70 RegisterNewPath
// A reading grown by one letter, put back into the column it now
// reaches.  Answers the node it went into, or nil if there was no room
// worth giving it.
//
// A column holds `MaxBestNodes` readings in score order, cheapest
// first, and while there is a free slot this is just an insertion.
// When the column is full is where it gets interesting, because the
// search does **not** simply drop the worst reading.
//
// Every kind of word carries a class (`BiGSlice::fField2c`), the column
// counts how many of its readings are of each (`fClassCounts`), and the
// grammar says how many it will allow (`fClassLimits`).  So:
//
// * if the new reading's own kind is **under** its limit, the search
//   walks back from the worst end for a reading that is unlimited or
//   already over quota, and recycles that one - **without looking at
//   the score at all**.  A column that has room for another date will
//   take one however dear it is, rather than keeping a twenty-eighth
//   word;
// * otherwise it walks back for one that is unlimited, of the same
//   class as the newcomer, or over quota, and takes it only if the
//   newcomer is actually cheaper.
//
// That is what keeps the twenty-seven readings a column holds varied,
// so one kind of word cannot crowd the others out however well the
// classifier happens to like it.
//
// (The two paths are not symmetrical about the counts: the second
//  counts the recycled reading's class down and the first does not.)
SearchNode*
RegisterNewPath(SearchStep* step, ULong score, long flags)
{
	ULong cost = score & 0xffff;
	SearchColumn* col = step->fColumn;
	long at;
	SearchNode* node;

	if ((long) col->fCount < MaxBestNodes)
	{
		// a free slot
		at = (long) col->fCount;
		col->fCount = (UByte) (at + 1);
		node = col->fNodes[at];
	}
	else
	{
		long worst = MaxBestNodes - 1;
		at = worst;
		node = col->fNodes[at];
		const BiGSlice* mine = step->fSlice;

		Boolean roomForMine = false;
		if (SearchIsLimited(mine))
		{
			long cls = SearchClassOf(mine);
			roomForMine = ((long) col->fClassCounts[cls] < SearchClassLimit(cls));
		}

		if (roomForMine)
		{
			// take any reading that is not holding a place of its own
			while (SearchIsLimited(node->fSlice)
				&& (long) col->fClassCounts[SearchClassOf(node->fSlice)]
					<= SearchClassLimit(SearchClassOf(node->fSlice))
				&& at > 0)
			{
				at--;
				node = col->fNodes[at];
			}
			// ... and move it to the worst end, closing the gap
			SearchBestEntry* best = gSearchBest[at];
			if (worst > at)
			{
				for (; at < MaxBestNodes - 1; at++)
				{
					col->fNodes[at] = col->fNodes[at + 1];
					gSearchBest[at] = gSearchBest[at + 1];
				}
			}
			col->fNodes[at] = node;
			gSearchBest[at] = best;
		}
		else
		{
			// the newcomer has no place of its own to claim, so it has
			// to be worth more than what it displaces
			while (SearchIsLimited(node->fSlice)
				&& SearchClassOf(node->fSlice) != SearchClassOf(mine)
				&& (long) col->fClassCounts[SearchClassOf(node->fSlice)]
					<= SearchClassLimit(SearchClassOf(node->fSlice))
				&& at > 0)
			{
				at--;
				node = col->fNodes[at];
			}
			if (cost >= (ULong) (UShort) node->fScore)
				return nil;
			if (SearchIsLimited(node->fSlice))
				col->fClassCounts[SearchClassOf(node->fSlice)]--;
		}
	}

	if (SearchIsLimited(step->fSlice))
		step->fColumn->fClassCounts[SearchClassOf(step->fSlice)]++;

	SearchBestEntry* best = gSearchBest[at];
	node->fField04 = flags;
	node->fSlice = step->fSlice;
	node->fScore = (short) cost;
	node->fSegment = step->fSegment;
	best->fFrom = step->fFrom;
	best->fChar = (UByte) step->fChar;

	// back into score order, cheapest first
	long k = at - 1;
	while (k >= 0 && cost < (ULong) (UShort) col->fNodes[k]->fScore)
		k--;
	k++;
	if (k < at)
	{
		for (long m = at; m > k; m--)
		{
			col->fNodes[m] = col->fNodes[m - 1];
			gSearchBest[m] = gSearchBest[m - 1];
		}
		col->fNodes[k] = node;
		gSearchBest[k] = best;
	}

	// ... and the column remembers its cheapest
	if ((ULong) (UShort) node->fScore < (ULong) (UShort) step->fBest)
		step->fBest = (short) (UShort) node->fScore;
	return node;
}


// ROM 0x001cfaa8 StoreFinalPaths
// The readings in a column finished off, once the step that filled it
// has run.
//
// Until now a reading in the new column is only a **backtrace**: the
// node it grew from and the letter that was added, in the matching
// `gSearchBest` entry.  This is what turns each of those into a real
// word tail - a cell holding the letter, pointing at the tail the
// reading grew from, with a reference taken on it so the older text
// stays alive.
//
// Keeping it until the end of the step is what makes the whole thing
// affordable: a reading that is dropped during the step never costs a
// cell at all.
void
StoreFinalPaths(SearchColumn* column, ULong base)
{
	if (column->fCount == 0)
		return;
	for (long i = 0; i < (long) column->fCount; i++)
	{
		SearchNode* node = column->fNodes[i];
		SearchBestEntry* grew = gSearchBest[i];

		// scores in the new column are measured from its own cheapest
		node->fScore = (short) ((ULong) (UShort) node->fScore - (base & 0xffff));

		WordTailRef was = grew->fFrom->fTail;
		// a cell for the letter that was added
		WordTailRef ref;
		if (wordTailFrees == kWordTailNone)
			ref = WordTailBlockAllocate();
		else
		{
			ref = wordTailFrees;
			wordTailFrees = WordTailAt(ref)->fNext;
		}
		WordTailCell* cell = WordTailAt(ref);
		cell->fRefCount = 1;
		cell->fChar = grew->fChar;
		cell->fNext = was;

		// ... and the text it grew from is held on to.  (The ROM has
		//  `WordTailAddRef` inlined here, and unlike that function it
		//  does answer straight away for the empty tail.)
		if (was != kWordTailNone)
		{
			if (was < kWordTailListBase)
			{
				WordTailCell* older = WordTailAt(was);
				if (older->fRefCount != 0xff)
					older->fRefCount = (UByte) (older->fRefCount + 1);
			}
			else
			{
				WordList* list = WordListAt(was);
				if (list->fRefCount < 0xff)
					list->fRefCount = (UShort) (list->fRefCount + 1);
			}
		}
		node->fTail = ref;
	}
}


// ROM 0x001cebac SearchDoViterbStep
// One candidate letter offered to every reading the search is holding.
//
// This is the step.  The column at the front is emptied, and then every
// reading in every column that this candidate could follow is grown by
// it - `SearchDoVStepFromNode` tries each of the 256 character codes
// against one reading - and whatever survives `RegisterNewPath` is left
// in the new column.  `StoreFinalPaths` turns the survivors' backtraces
// into text at the end.
//
// Readings out of different columns are not comparable as they stand,
// because a column further into the word has had fewer chances to
// spend, so each one is offered with its own **bias**: what it cost to
// reach, less the cheapest, plus what continuing a word costs here.
//
// The gap before this candidate is read both ways round.  Its
// separation is the probability that a new word starts here, so
// `ArProbEncode` of it is what starting one costs and `ArProbEncode` of
// its complement is what *not* starting one costs.  And if the column
// this candidate begins at already holds a finished word, the search
// grows readings straight on from that word through
// `gSearchWordListNode` - a node whose tail *is* the word list.
void
SearchDoViterbStep(short* fromProbs, short* fromScratch, long index,
				RosSegment* segment, Fixed confidence, Boolean endsWord,
				short total)
{
	SearchColumn* here = gSearchColumns[0];
	here->fCount = 0;
	for (long i = 0; i < 10; i++)
		here->fClassCounts[i] = 0;

	SearchStep step;
	step.fBest = 0x7ffe;
	step.fEndsWord = endsWord;
	step.fLimit = index + 2;
	step.fSegment = segment;
	step.fColumn = here;

	// what the cheapest column this candidate could follow has cost
	ULong minCost = 0;
	ULong minAlt = 0;
	long from = segment->fRealCount;
	if (index != 0)
	{
		minCost = 0x7ffe;
		minAlt = 0x7ffe;
		for (long i = from; i < step.fLimit && i < kSearchColumns; i++)
		{
			SearchColumn* col = gSearchColumns[i];
			if ((ULong) col->fJump + (ULong) from == (ULong) i)
			{
				if ((ULong) col->fCost < minCost)
					minCost = (ULong) col->fCost;
				if ((ULong) col->fAltCost < minAlt)
					minAlt = (ULong) col->fAltCost;
			}
		}
	}
	here->fAltCost = (long) (minAlt + (ULong) (UShort) total);

	// is anything readable here at all?
	long code;
	for (code = 0; code < 256; code++)
		if ((ULong) (UShort) fromProbs[code] < 0x7ffe)
			break;
	if (code >= 256)
	{
		here->fJump = (UByte) segment->fField04;
		here->fRealCount = (UByte) segment->fRealCount;
		here->fCost = 0x7ffe;
		StoreFinalPaths(here, (ULong) (UShort) step.fBest);
		return;
	}

	// what one stroke of this candidate costs: nothing when its
	// strokes lie on each other well, up to 322 when they do not
	ULong perStroke;
	if (confidence > RosCI->fStrokeCostGate)
		perStroke = RosCI->fStrokeCost >> 16;
	else
	{
		Fixed f = FixedMultiply(confidence, RosCI->fStrokeCostScale);
		ULong most = RosCI->fStrokeCost & 0xffff;
		perStroke = most - (ULong) (((long) (most - (RosCI->fStrokeCost >> 16)) * f) >> 16);
	}
	ULong strokeCost = ((ULong) (segment->fStrokes->fCount - 1) * perStroke) & 0xffff;

	// which case of each character code is reachable here
	UByte* caseOf = (UByte*) gSearchScratch;
	for (long c = 0; c < 256; c++)
	{
		caseOf[c] = 0;
		UByte flags = RosCI->fCapCaseFlags[c];
		ULong one = (ULong) (UShort) fromProbs[RosCI->fCapAltCase1[c]];
		if (one > 0x7ffd
			&& (ULong) (UShort) fromProbs[RosCI->fCapAltCase2[c]] >= 0x7ffe)
			continue;				// neither case can be read
		if ((flags & 2) != 0)
			caseOf[c] = 1;
		if ((flags & 1) != 0)
			caseOf[c] = 2;
	}

	// a word already finished on the column this candidate begins at
	ULong keepGoing = 0;
	SearchColumn* startCol = gSearchColumns[from];
	if (startCol->fWords != nil && segment->fSeparation >= 1)
	{
		ULong wordCost = (ULong) startCol->fWords->fCost;
		if (wordCost < minCost)
			minCost = wordCost;

		ULong newWord = (ULong) (UShort) ArProbEncode(segment->fSeparation);
		keepGoing = (ULong) (UShort) ArProbEncode(0x00010000 - segment->fSeparation);
		if (newWord > 0x7ffe)
			newWord = 0x7ffe;
		if (keepGoing > 0x7ffe)
			keepGoing = 0x7ffe;

		// a node whose tail is that whole word, so a reading can grow
		// straight on from it
		gSearchWordListNode.fScore = (short) newWord;
		gSearchWordListNode.fSlice = nil;
		gSearchWordListNode.fField04 = 0;
		gSearchWordListNode.fSegment = startCol->fWords->fSegment;
		gSearchWordListNode.fTail = (WordTailRef)
					(kWordTailListBase + (startCol->fWords - wordLists));

		SearchDoVStepFromNode(&step, &gSearchWordListNode, fromProbs, fromScratch,
						confidence, wordCost - minCost);
		GeoContextClearCache();
	}

	// ... and every reading in every column this candidate can follow
	for (long i = from; i < step.fLimit && i < kSearchColumns; i++)
	{
		SearchColumn* col = gSearchColumns[i];
		ULong bias = ((ULong) col->fCost - minCost) + keepGoing;
		if ((ULong) col->fJump + (ULong) from == (ULong) i
			&& bias < 0x7ffe && col->fCount != 0)
			for (long j = 0; j < (long) col->fCount; j++)
				SearchDoVStepFromNode(&step, col->fNodes[j], fromProbs, fromScratch,
								confidence, bias);
	}

	here->fJump = (UByte) segment->fField04;
	here->fRealCount = (UByte) segment->fRealCount;
	if (here->fCount == 0)
		here->fCost = 0x7ffe;
	else
		here->fCost = (long) (minCost + (ULong) (UShort) step.fBest + strokeCost);
	StoreFinalPaths(here, (ULong) (UShort) step.fBest);
}


// Where a way out of the lexicon leads: the node the search stands at
// once this character has been taken.  `LELangNodeNumOut` walks a
// node's *siblings* - the letters that may follow - and this goes the
// other way, down into the word.  The sign bit says the word may end
// here, which is how the search knows a reading is a whole word.
static ULong
SearchNextLangNode(const UByte* langBytes, ULong entry, const UByte* at)
{
	UByte header = langBytes[kLELangFormat];
	if ((header & 7) == kLELangRun)
	{
		// a run's node carries the next one two bytes on, big-endian
		UByte flags = at[6];
		ULong next = ((flags & 2) != 0) ? 0x80000000
					: (((ULong) at[7] << 8) | (ULong) at[8]);
		if ((flags & 1) != 0)
			next |= 0x80000000;
		return next;
	}

	UByte flags = langBytes[kLELangNodes + entry + 1];
	if ((flags & 0x20) != 0)
		return 0x80000000;
	ULong next = ((flags & 0x10) != 0) ? (ULong) (header >> 4) : 0;
	next += (ULong) AckNodeSizeTab[flags >> 6] + entry + 2;
	if ((flags & 0x10) != 0)
		next |= 0x80000000;
	return next;
}


// ROM 0x001cf0d8 SearchDoVStepFromNode
// One reading grown by one letter, every way it can be.
//
// This is the innermost thing the engine does, and the rest of the
// search exists to feed it.  Given one reading and one candidate piece
// of ink, it tries every character the lexicon will allow next, adds up
// what each would cost, and offers the result to `RegisterNewPath`.
//
// There are three loops, one inside the other.
//
// **The kind of word.**  A reading may stay in the kind of word it is
// in, or move to one the grammar allows after it - and the pseudo-node
// that stands for a word already finished may start any kind at all.
// The transition's own weight is added to the reading's score.  The ROM
// writes the candidate kind into the node itself and puts the old one
// back at the top of each round, which is what `wasSlice`, `wasScore`
// and `wasFlags` are for.
//
// **The letter.**  `LELangNodeNumOut` says which characters the lexicon
// allows from here.  Each one is charged four things: what the
// classifier thought of it, plus more of the same the more loosely the
// strokes were written; what a character of this kind of word costs;
// what its *case* costs in the context the reading is in
// (`CapHackDetermineContext` and the common info's three twelve-entry
// tables, which is where `Mc` and `MC` part company); and what the
// geometry between it and the letter before it costs - at a quarter
// weight when the letter before is in another word, because across a
// word boundary two shapes have much less to say about each other.
//
// **The case.**  Having tried the character the lexicon named, it tries
// the other case of it, and then a third form - but only the cases the
// kind of word allows, which `gSearchScratch` and the slice's own flags
// say between them.  That is why writing a word in the wrong case still
// reads.
void
SearchDoVStepFromNode(SearchStep* step, SearchNode* node, short* fromProbs,
				short* fromScratch, Fixed confidence, ULong bias)
{
	short wasScore = node->fScore;
	const BiGSlice* wasSlice = node->fSlice;
	long wasFlags = node->fField04;

	// how many other kinds of word this reading may move to
	long transitions = 0;
	if (wasSlice == nil)
	{
		if (gSearchGrammar != nil)
			transitions = gSearchGrammar->fCount;
	}
	else if ((wasFlags & 0x80000000) != 0)
		transitions = wasSlice->fNextCount;
	if (transitions < 0)
		return;

	step->fFrom = node;
	// how loosely this was written, which is what makes a doubtful
	// letter cost more the more strokes it took
	Fixed loose = 0x00010000 - confidence * 2;

	for (long t = 0; t <= transitions; t++)
	{
		node->fScore = wasScore;
		if (t < transitions)
		{
			if (wasSlice == nil)
			{
				const BiGSlice* next = gSearchGrammar->fSlices[t];
				ULong own = (ULong) (UShort) next->fScore;
				if (own >= 0x7ffe || next->fDictionary == 0)
					continue;
				node->fScore = (short) (own + (ULong) (UShort) wasScore);
				node->fSlice = next;
			}
			else
			{
				if (wasSlice->fNext[t]->fDictionary == 0)
					continue;
				node->fScore = (short) ((ULong) (UShort) wasScore
							+ (ULong) (UShort) wasSlice->fWeights[t]);
				node->fSlice = wasSlice->fNext[t];
			}
			node->fField04 = 2;
		}
		else
		{
			// ... or stay where it is
			node->fSlice = wasSlice;
			node->fField04 = wasFlags;
			if (wasSlice == nil)
				continue;
		}

		if ((ULong) (UShort) node->fScore >= 0x7ffe - bias)
			continue;
		ULong base = (ULong) (UShort) node->fScore + bias;

		long context = CapHackDetermineContext(node);
		// the upper six of the twelve capitals contexts
		ULong upperHalf = (context == 0 || context > 5) ? 1 : 0;

		ULong at04 = (ULong) node->fField04;
		step->fSlice = node->fSlice;
		const void* lang = (const void*) step->fSlice->fDictionary;
		long ways = LELangNodeNumOut(lang, at04);
		const UByte* langBytes = (const UByte*) lang;

		for (long w = 0; w < ways; w++)
		{
			ULong entry = LELTranCache[w];
			ULong asked;
			if ((langBytes[kLELangFormat] & 7) == kLELangRun)
				asked = entry >> 24;
			else
				asked = (ULong) langBytes[kLELangNodes + entry];

			step->fChar = (long) asked;
			ULong kindFlags = (ULong) step->fSlice->fField10;
			long which = 0;
			ULong allowsCase = kindFlags & 8;
			ULong allowsBoth = kindFlags & 0x20;
			const UByte* at = langBytes + (entry & 0xffffff);
			ULong newFlags = 0;

			for (;;)
			{
				ULong cost = base;
				Boolean readable = true;
				if (fromProbs != nil)
				{
					// what the classifier thought of this character
					ULong said = (ULong) (UShort) fromProbs[step->fChar];
					if (said >= 0x7ffe)
						readable = false;
					else
					{
						cost += said;
						// ... and again, the more strokes it took and
						// the less they lay on each other
						Fixed f = loose;
						if (f < 1)
							f = 0;
						else if (f > 0x00010000)
							f = 0x00010000;
						f = FixedMultiply(f, 0x00010000);
						cost += (ULong) (((long) said
									* (step->fSegment->fStrokes->fCount - 1) * f) >> 16);
					}
				}

				if (readable)
				{
					const BiGSlice* slice = step->fSlice;
					// what a character of this kind of word costs
					cost += (ULong) (UShort) slice->fCharCost;

					// what this letter's *case* costs in this context
					if ((kindFlags & 4) != 0)
					{
						UByte cf = RosCI->fCapCaseFlags[step->fChar];
						ULong add = 0;
						Boolean charge = true;
						if ((cf & 2) != 0)			// a lower-case letter
						{
							if (which != 0 && allowsCase != 0)
							{
								if (upperHalf != 0 && allowsBoth == 0)
									charge = false;
								else
									add = (ULong) (UShort) slice->fCapCostLower;
							}
							else
								add = (ULong) RosCI->fCapCostLower[context];
						}
						else if ((cf & 1) != 0)		// a capital
						{
							if (which == 0 && allowsCase != 0)
							{
								if (upperHalf != 0 && allowsBoth == 0)
									charge = false;
								else
									add = (ULong) (UShort) slice->fCapCostUpper;
							}
							else
								add = (ULong) RosCI->fCapCostUpper[context];
						}
						else if ((cf & 4) != 0)		// neither
							add = (ULong) RosCI->fCapCostOther[context];
						else
							charge = false;
						if (charge)
							cost += add;
					}

					// ... and what it costs on top in the upper six
					if (upperHalf != 0)
					{
						ULong gate = (allowsCase != 0) ? allowsBoth : upperHalf;
						if (allowsCase == 0 || gate == 0 || which != 0)
						{
							UByte cf = RosCI->fCapCaseFlags[step->fChar];
							if ((cf & 2) != 0)
								cost += (ULong) (UShort) slice->fCapExtraLower;
							else if ((cf & 1) != 0)
								cost += (ULong) (UShort) slice->fCapExtraUpper;
						}
					}

					// how it sits against the letter before it
					WordTailRef tail = node->fTail;
					long geo;
					if (tail == kWordTailNone)
						// nothing before it: a quarter of the charge
						geo = GeoContextPenalty(0, nil, (UByte) step->fChar,
									step->fSegment, 0) >> 2;
					else if (tail >= kWordTailListBase)
					{
						// a whole word before it: also a quarter, and
						// the letter is that word's best reading's last
						WordList* list = WordListAt(tail);
						UByte before = WordTailAt(list->fTails[0])->fChar;
						geo = GeoContextPenalty(before, node->fSegment,
									(UByte) step->fChar, step->fSegment, 1) >> 2;
					}
					else
						// within a word: the whole charge
						geo = GeoContextPenalty(WordTailAt(tail)->fChar, node->fSegment,
									(UByte) step->fChar, step->fSegment, 0);
					cost += (ULong) geo;

					// a reading picking up after a whole word pays the
					// second score array as well
					if (node->fTail >= kWordTailListBase)
						cost += (ULong) (UShort) fromScratch[step->fChar];

					if (cost < 0x7ffe)
					{
						if (newFlags == 0)
							newFlags = SearchNextLangNode(langBytes, entry, at);
						RegisterNewPath(step, (short) (cost & 0xffff), (long) newFlags);
					}
				}

				// the same letter in another case, if this kind of word
				// will have it
				Boolean again = false;
				for (;;)
				{
					which++;
					if (which > 2)
						break;
					step->fChar = (long) asked;
					if ((((UByte*) gSearchScratch)[asked] & kindFlags) == 0)
						break;
					if (which != 1)
					{
						if ((step->fSlice->fField10 & 0x10) != 0)
							break;
						step->fChar = (long) RosCI->fCapAltCase2[step->fChar];
						if (step->fChar == 0)
							break;
						again = true;
						break;
					}
					step->fChar = (long) RosCI->fCapAltCase1[asked];
					if (step->fChar != 0)
					{
						again = true;
						break;
					}
				}
				if (!again)
					break;
			}
		}
	}
}
