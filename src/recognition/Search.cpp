/*
	File:		recognition/Search.cpp

	Contains:	The lexical search's state and its life - see Search.h.

	Reconstructed from the MP2x00 US ROM; each function cites its
	origin.
*/

#include "Search.h"
#include "RosEngine.h"
#include "RosStrokes.h"			// kRosettaMemoryTag
#include "NewtonMemory.h"
#include "NewtonExceptions.h"

extern const ExceptionName exRosetta;	// ROM 0x003774f8 exRosetta
#include "Segment.h"

#include <string.h>


// ROM 0x0c101a8c MaxBestNodes
// How many readings a column keeps.  There is room for thirty.
long	MaxBestNodes = 27;

// ROM 0x0c101a90 (unnamed)
Ptr		gSearchScratch = nil;
// ROM 0x0c101a94 (unnamed)
UByte	gSearchAllocated = 0;
// ROM 0x0c101a98 (unnamed)
// `MaxBestNodes` pointers into the block below, eight bytes apart.
Ptr*	gSearchBest = nil;
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

		gSearchBestBlock = (Ptr) RosAllocate(MaxBestNodes * 8);
		gSearchBest = (Ptr*) RosAllocate(MaxBestNodes * (long) sizeof(Ptr));
		for (long i = 0; i < MaxBestNodes; i++)
			gSearchBest[i] = gSearchBestBlock + i * 8;

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
	first->fField79 = 0;
	first->fField7a = 0;
	first->fField88 = 0;
	first->fField8c = 0;
	first->fWords = nil;
	for (long i = 0; i < 10; i++)
		first->fClassCounts[i] = 0;

	SearchNode* node = first->fNodes[first->fCount];
	first->fCount = (UByte) (first->fCount + 1);
	node->fSlice = nil;
	node->fField04 = 0;
	node->fScore = 0;
	node->fTail = kWordTailNone;
	node->fField0c = 0;
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
	The search itself.  NOT YET: the Viterbi step that walks the
	lattice, the gathering of the best paths at the end, and the
	scoring that ties the classifier, the grammar and the dictionaries
	together - about 6 KB in seven functions.
--------------------------------------------------------------------*/

// ROM 0x001cebac SearchDoViterbStep
void
SearchDoViterbStep(short* /*fromProbs*/, short* /*fromScratch*/, long /*index*/,
				RosSegment* /*segment*/, Fixed /*confidence*/, Boolean /*endsWord*/,
				short /*total*/)
{
}





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
		if (i <= (long) col->fField79 && col->fCount != 0
			&& (flag == 0 || (long) col->fField79 <= i))
		{
			if ((ULong) col->fField88 < leastCost)
				leastCost = (ULong) col->fField88;
			if ((ULong) col->fField8c < leastOther)
				leastOther = (ULong) col->fField8c;
		}
	}

	for (long i = 0; i < kSearchColumns; i++)
	{
		SearchColumn* col = gSearchColumns[i];
		if (col->fCount == 0)
			continue;
		ULong bias = (ULong) col->fField88 - leastCost;
		if (!(i <= (long) col->fField79
			&& (flag == 0 || (long) col->fField79 <= i)
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
	list->fField0c = gSearchColumns[best[0]]->fNodes[best[1]]->fField0c;

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
