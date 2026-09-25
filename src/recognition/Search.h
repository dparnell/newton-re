/*
	File:		recognition/Search.h

	Contains:	The lexical search: how the engine chooses a reading.

				The segment layer hands up a **lattice** - every
				grouping of strokes that might be a letter - and the
				classifier says what each of those groupings might be.
				Neither of them decides anything.  This is what does:
				a Viterbi search that walks the lattice from left to
				right keeping the best few partial readings at each
				point, scored by the classifier, the grammar and the
				dictionaries together.

				The state is 37 **columns**, one per stroke of the word
				plus one to start from, and each column holds up to
				`MaxBestNodes` (27) nodes.  A node is a partial reading
				that reaches this point: what it cost, and a word tail
				(`recognition/WordTails.h`) for the text so far.  Those
				tails are reference counted and shared, which is what
				makes holding twenty-seven of them at every one of
				thirty-six positions affordable.

	NOT YET: the search itself - `SearchProcessSegment`,
	`SearchDoViterbStep`, `SearchDoVStepFromNode`, `SearchFindBest`,
	`SearchBestWords`, `SearchSendWords` and
	`SearchSegwordRememberNBest`, about 6 KB.  What is here is the
	state, its life, and what happens to a word on the way out.

	Reconstructed from the MP2x00 US ROM; each function cites its
	origin.
*/

#ifndef __SEARCH_H
#define __SEARCH_H

#ifndef __WORDTAILS_H
#include "WordTails.h"
#endif

struct BiGrammar;
struct BiGSlice;
struct RosSegment;
struct WordRecog;

// One partial reading that reaches a point in the lattice.  Sixteen
// bytes, and the only one of them that is understood so far is the
// word tail - what has been read to get here.
struct SearchNode
{
	// The kind of word this reading is in - a slice of the grammar.
	// DEVIATION: pointer-sized on the host.
	const BiGSlice*	fSlice;			// +0x00
	// 2 puts the reading in the upper six capitals contexts, and the
	// sign bit is read as well.
	long			fField04;		// +0x04
	short			fScore;			// +0x08  what it has cost to get here
	WordTailRef		fTail;			// +0x0a  ... and what has been read
	// The candidate letter it was grown with.
	// DEVIATION: pointer-sized on the host.
	RosSegment*		fSegment;		// +0x0c
};

// How many columns there are - one per stroke of the longest word the
// engine will read, and one to start from - and how many nodes a
// column has room for.  `MaxBestNodes` is how many it actually uses.
const long	kSearchColumns		= 0x25;		// 37
const long	kSearchNodeSlots	= 30;

// One point in the lattice, and the best readings that reach it.
struct SearchColumn
{
	SearchNode*		fNodes[kSearchNodeSlots];	// +0x00 .. +0x77
	UByte			fCount;			// +0x78  how many of them are in use
	// How far ahead the search may jump from here, and how many
	// letters the candidate that filled this column stood for -
	// copied off the segment by `SearchDoViterbStep`.
	UByte			fJump;			// +0x79
	UByte			fRealCount;		// +0x7a
	// How many of the readings in this column are of each kind of
	// word, one count per lexicon class (`BiGSlice::fField2c`).  The
	// grammar carries a limit for each of them in `fField15`, so that
	// one kind of word cannot crowd the others out of the column -
	// a beam that is kept deliberately varied.
	UByte			fClassCounts[10];	// +0x7b .. +0x84
	UByte			fPad85[3];
	// What it cost to reach this column at all, which is the bias
	// added back before readings from different columns are compared,
	// and the same thing measured the other way.
	long			fCost;			// +0x88
	long			fAltCost;		// +0x8c
	WordList*		fWords;			// +0x90  what was read to get here
};

// What the Viterbi step carries about the candidate letter it is
// growing readings with.  The ROM builds it as a block of locals in
// `SearchDoViterbStep` and hands its address down to
// `SearchDoVStepFromNode` and `RegisterNewPath`; only the fields those
// two read are known so far.  NOT YET, but mapped.
struct SearchStep
{
	// The reading being grown and the letter being added to it, which
	// go into the column's backtrace.
	SearchNode*		fFrom;			// +0x00
	RosSegment*		fSegment;		// +0x04
	const BiGSlice*	fSlice;			// +0x08  the kind of word being tried
	long			fChar;			// +0x0c
	SearchColumn*	fColumn;		// +0x10  the column being filled
	short			fBest;			// +0x14  the cheapest reading in it so far
	UByte			fEndsWord;		// +0x16
	UByte			fPad17;
	long			fLimit;			// +0x18  the index being grown, plus two
	long*			fMaxBestNodes;	// +0x1c  &MaxBestNodes, used as a base
};

// How many readings a column keeps.  A variable, not a constant: the
// ROM leaves room for thirty and uses twenty-seven.
extern long		MaxBestNodes;						// ROM 0x0c101a8c MaxBestNodes

// The state.  None of these has a symbol in the ROM.
extern UByte	gSearchAllocated;					// ROM 0x0c101a94 (unnamed)
struct SearchBestEntry;
extern SearchBestEntry**	gSearchBest;		// ROM 0x0c101a98 (unnamed)
extern SearchColumn**	gSearchColumns;				// ROM 0x0c101a9c (unnamed)
extern const BiGrammar*	gSearchGrammar;				// ROM 0x0c101aa0 (unnamed)
// The two arrays of 256 scores `SearchProcessSegment` leaves for the
// Viterbi step: what the classifier said, and what the search charges
// for the letter itself.
// Room for the readings on the way back out: the strings a caller is
// handed are these, so they have to stay valid until it asks again.
extern Ptr*		gSearchReturnCache;					// ROM 0x0c101aa4 (unnamed)
// A `SearchNode` standing for a whole finished word, so that the
// search can grow a reading straight on from one: its tail is the
// word list on the column the candidate starts from, and its score
// is what the gap before it cost.
extern SearchNode	gSearchWordListNode;			// ROM 0x0c106ec8 (unnamed)
// Which of the two cases of each character code is reachable here:
// 0 neither, 1 the lower, 2 the upper.  256 bytes.
extern Ptr		gSearchScratch;						// ROM 0x0c101a90 (unnamed)
extern Ptr		gSearchScratchA;					// ROM 0x0c101ab8 (unnamed)
extern Ptr		gSearchScratchB;					// ROM 0x0c101abc (unnamed)

// The state made and given back.  `SearchBeginWord` makes it on its
// first call and it is kept for the life of the engine.
void	SearchAllocateGlobals(void);					// ROM 0x001d0014 SearchAllocateGlobals
void	SearchDeallocateGlobals(void);					// ROM 0x001d02ec SearchDeallocateGlobals
// Room for `count` readings on the way back out, grown but never shrunk.
void	SearchAllocateReturnCache(long count);			// ROM 0x001cea78 SearchAllocateReturnCache

// A word about to be read: every column emptied and one node put in
// the first of them, with nothing read yet.
void	SearchBeginWord(const BiGrammar* grammar);		// ROM 0x001ce008 SearchBeginWord
// ... and a word finished: the best readings gathered and handed to
// `proc`, and then everything the search was holding given back.
typedef void (*SearchEndWordProc)(WordRecog* wr, char** words, UniChar* scores,
				long* flags, long strokes, long count);
void	SearchEndWord(const BiGrammar* grammar, long strokes, SearchEndWordProc proc,
				WordRecog* wr, char** words, UniChar* scores, long* flags,
				long count);							// ROM 0x001d0660 SearchEndWord
// One column's readings given back.
void	GCBestNodes(SearchColumn* column);				// ROM 0x001d0738 GCBestNodes

// A reading on its way out, looked at one last time.  See the comment
// on the definition.
void	SearchCheckHashHit(char** word);				// ROM 0x001d0d90 SearchCheckHashHit
// The eight words it knows, and what it answers instead
// (`SearchEasterEgg.cpp`, generated by `analysis/romtable.py`).
extern const char	kSearchEasterWords[8][9];			// ROM 0x0037757c (unnamed)
extern const char	kSearchEasterReplies[8][23];		// ROM 0x003775c4 (unnamed)

// The columns moved along by one: they are a ring, so nothing is
// copied and the column that falls off the end is emptied and becomes
// the new first one.
void	ShiftNetValues(void);							// ROM 0x001d06fc ShiftNetValues
// The best reading so far - the try string the Newton shows while you
// are still writing.
void	GetBestPath(char* out, UByte how);				// ROM 0x001d0798 GetBestPath

// One candidate letter offered to every reading the search is holding:
// the step.
void	SearchDoViterbStep(short* fromProbs, short* fromScratch, long index,
				RosSegment* segment, Fixed confidence, Boolean endsWord,
				short total);							// ROM 0x001cebac SearchDoViterbStep
// ... and one reading tried against every character code.  NOT YET, so
// no reading is ever grown.
void	SearchDoVStepFromNode(SearchStep* step, SearchNode* node, short* fromProbs,
				short* fromScratch, Fixed confidence, ULong bias,
				long which);							// ROM 0x001cf0d8 SearchDoVStepFromNode
// The best readings written out as text, with a score and the
// dictionary each came from.  The strings are the engine's own, out
// of the return cache.
long	SearchBestWords(char** words, UniChar* scores, long* flags,
				long count, UByte how);					// ROM 0x001d0c48 SearchBestWords

// The **backtrace** for one reading in the column being filled: the
// node it grew from and the letter that was added.  `StoreFinalPaths`
// turns these into word tails when the step is over, so a reading that
// is dropped part way through never costs a cell.
// DEVIATION: `fFrom` is pointer-sized on the host, so the entry is
// bigger than the ROM's eight bytes.
struct SearchBestEntry
{
	SearchNode*		fFrom;			// +0x00
	UByte			fChar;			// +0x04
	UByte			fPad05[3];
};

// The readings in a column finished off once the step has filled it:
// each backtrace turned into a real word tail, and the scores measured
// from the column's own cheapest.
void	StoreFinalPaths(SearchColumn* column, ULong base);	// ROM 0x001cfaa8 StoreFinalPaths

// A reading grown by one letter, put back into the column it reaches.
// The column holds its readings in score order and, when it is full,
// prefers to recycle one whose *kind of word* is over quota rather than
// simply the worst.
SearchNode*	RegisterNewPath(SearchStep* step, ULong score, long flags);	// ROM 0x001cfc70 RegisterNewPath

// What a partial reading looks like from the outside, in twelve
// classes, so that the grammar can charge differently for what may
// follow it.  `Mc` and `MC` are not the same context, and an
// apostrophe inside a word is not the end of one.
long	CapHackDetermineContext(const SearchNode* node);	// ROM 0x001cff18 CapHackDetermineContext

// NOT YET: the search itself.
void	SearchProcessSegment(const BiGrammar* grammar, Fixed* probs, Fixed* scratch,
				long index, RosSegment* segment, Fixed confidence,
				Boolean endsWord, char* tryString);		// ROM 0x001ce830 SearchProcessSegment
// The best readings the search is holding, gathered out of the columns
// best first: a triple per reading in `out` - which column, which node,
// what it cost - with the best brought down to nothing.  The same text
// found twice is one reading.
long	SearchFindBest(long* out, Fixed* outA, Fixed* outB, long count,
				UByte flag, Fixed weight);				// ROM 0x001d07f4 SearchFindBest
void	SearchSegwordRememberNBest(SearchColumn* column, long strokes, Fixed weight);	// ROM 0x001cf920 SearchSegwordRememberNBest
void	SearchSendWords(WordList* list, long strokes, SearchEndWordProc proc,
				WordRecog* wr, char** words, UniChar* scores, long* flags,
				long count);							// ROM 0x001d03f8 SearchSendWords

#endif	/* __SEARCH_H */
