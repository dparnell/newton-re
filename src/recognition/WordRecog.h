/*
	File:		recognition/WordRecog.h

	Contains:	The handwriting engine's word recogniser: the block of
				state a piece of writing is read in, and its life.

				This is level 3 of the engine (see
				`docs/recognition/README.md`).  Everything above it -
				`TRosRecognizer` and the fifteen `Rosetta*` calls - is
				the Newton's join to ParaGraph's code; everything from
				here down is ParaGraph's.  There is exactly one word
				recogniser, made when the engine wakes and destroyed
				when it sleeps, and it is where a piece of writing
				lives while it is being read: the strokes as they come
				in, the segments they are cut into, the readings that
				come out, and the running measurements of the hand that
				wrote them.

				The state is one flat block of 0x208 bytes, allocated
				with `NewNamedPtr` and tagged 'RoCK' like everything
				else the engine owns, and the layers reach into it at
				fixed byte offsets.  `WordRecog` below gives those
				offsets names as far as the evidence goes; a field
				still called `fFieldNNN` is one nothing reconstructed
				yet reads.

				Three things are worth knowing about it.

				**It is made in two halves.**  `WordRecogNew` allocates
				the block and nils exactly the ten pointers
				`WordRecogDeallocate` gives back - nothing else, so the
				rest is rubbish until `WordRecogCreate2` fills it in.
				`WordRecogAllocate` then hangs the arrays off it, and
				every one of those allocations throws on failure into a
				handler that deallocates the lot.  That is also what
				`WordRecogSuspend` and `WordRecogResume` are: the
				arrays are handed back while the engine is quiet and
				made again when it wakes, without the block itself
				moving, so everything pointing at it stays good.

				**The run.**  `fRun` is twenty-two numbers describing
				the writing as it is being read - how tall it is, how
				far apart the letters are, how much it slopes.
				`fSavedRun` is the copy to go back to:
				`WordRecogInvalRun` puts the run back as it was,
				`WordRecogSaveRun` keeps what has been learnt, and
				`WordRecogReset` fills the saved copy with the engine's
				own starting values, which are all worked out from one
				nominal cap height.

				**The readings.**  `fWords` is the engine's answer -
				`fWordCount` strings with a score each - and
				`fCheckWords` is who they are handed to.  When the
				engine cannot read the writing the first word is
				`FailureString`, "????", which is the same four
				characters a word arrives at the view as.

	NOT YET: `WordRecogAddStroke` and `WordRecogAddStroke2`, which take
	the strokes in; `WordRecogAnalyzeWord` and the net calls, which read
	them; and the segment side.  What is here is the block, its life and
	the handing back.

	Reconstructed from the MP2x00 US ROM (0x00272728-0x002766c0); each
	function cites its origin.
*/

#ifndef __WORDRECOG_H
#define __WORDRECOG_H

#ifndef __ROSSTROKES_H
#include "RosStrokes.h"
#endif

#ifndef __ROSENGINE_H
#include "RosEngine.h"
#endif


// How many readings the engine is asked for, how many strokes and
// segments one word may be made of, and how long the run is.  The
// counts are the sizes `WordRecogAllocate` asks for divided by the
// size of a pointer: 600 and 0xe10 bytes.
const long	kWordRecogMaxStrokes	= 150;
const long	kWordRecogMaxSegments	= 900;
const long	kWordRecogBufferSize	= 0x400;
const long	kWordRecogRunLength		= 22;

// The ROM's block is this big; ours is larger, because a host pointer
// is eight bytes.  The offsets in the comments are the ROM's.
const long	kWordRecogStateSize		= 0x208;

// ROM 0x0037acdc FailureString
// What the engine puts where a reading would go when it could not read
// the writing at all.
extern const char* const	FailureString;


// What the engine hands a word back through: the readings, a score
// each, a word of flags each, how many of the strokes offered the
// readings cover, and how many readings there are.  (The ROM's own
// callback, `RosettaCheckWords`, never looks at the flags.)
typedef void (*WordRecogCheckWordsProc)(char** words, UniChar* scores, long* flags, ULong strokes, ULong count);


// The word recogniser's state.  The ROM's is 0x208 flat bytes; the
// offsets below are its, and a field named `fFieldNNN` is one nothing
// reconstructed so far reads.
struct WordRecog
{
	void*			fField00;			// +0x000  RosettaAwaken passes nil
	void*			fField04;			// +0x004  ... and nil
	WordRecogCheckWordsProc	fCheckWords;	// +0x008  where the readings go
	long			fWordCount;			// +0x00c  how many readings to make (ten)
	char**			fWords;				// +0x010  fWordCount of them
	UniChar*		fScores;			// +0x014  one score each
	long*			fWordFlags;			// +0x018  one word each
	char*			fField1c;			// +0x01c  a string, emptied on a clear
	short			fStrokeCount;		// +0x020
	short			fField22;			// +0x022
	RosStroke**		fStrokes;			// +0x024  kWordRecogMaxStrokes of them
	short			fResX;				// +0x028  the tablet's resolution, whole
	short			fResY;				// +0x02a
	Fixed			fField2c;			// +0x02c  1.0 at a reset
	Fixed			fField30;			// +0x030  1.0 at a reset
	Fixed			fField34;			// +0x034  the nominal height times the ratio
	short			fOwnsStrokes;		// +0x038  the strokes are the engine's to free
	short			fReturnedStrokes;	// +0x03a  how many have been handed back
	short			fSegmentCount;		// +0x03c
	short			fField3e;			// +0x03e
	RosSegment**	fSegments;			// +0x040  kWordRecogMaxSegments of them
	UByte			fField44;			// +0x044
	UByte			fPad45[3];
	void*			fBuffer48;			// +0x048  kWordRecogBufferSize bytes
	void*			fBuffer4c;			// +0x04c  kWordRecogBufferSize bytes
	UByte			fSuspended;			// +0x050  the arrays have been given back
	UByte			fPad51[3];
	void*			fNet;				// +0x054  the classifier (RosettaAwaken's)
	RosNetPatternizer*	fPatternizer;	// +0x058
	RosNetPattern*	fPattern;			// +0x05c
	Fixed			fField60;			// +0x060  fRun[0] as the word started
	Fixed			fField64;			// +0x064
	Fixed			fField68;			// +0x068
	Fixed			fMeanCharHeight;	// +0x06c  CharGetAvgBoxBHW's, per character
	Fixed			fRun[22];			// +0x070  the hand, as it is being measured
	Fixed			fSavedRun[22];		// +0x0c8  the copy to go back to
	Fixed			fField120[16];		// +0x120  eight pairs; Create2 sets the second of each
	UByte			fPad160[0x38];		// +0x160
	RosGrammars*	fGrammars;			// +0x198
	long			fContextIndex;		// +0x19c  < 0: fContext is ours to destroy
	RosGrammarContext*	fContext;		// +0x1a0
	long			fField1a4;			// +0x1a4
	RosStroke*		fPendingStroke;		// +0x1a8  the stroke not yet taken in
	long			fField1ac;			// +0x1ac
	long			fField1b0[6];		// +0x1b0  cleared when the engine wakes
	void*			fCallBack;			// +0x1c8  the Newton's own (gRosCallBack)
	FRect			fBaseline;			// +0x1cc  the word's box, its bottom the baseline
	long			fField1dc;			// +0x1dc
	long			fField1e0;			// +0x1e0  -1 when the engine wakes
	long			fField1e4;			// +0x1e4
	long			fField1e8;			// +0x1e8
	void*			fCharBox;			// +0x1ec  the boxed-character recogniser
	ULong			fClassifyMode;		// +0x1f0  kRosettaClassifyNormally and friends
	ULong			fFlags1f4;			// +0x1f4
	long			fField1f8;			// +0x1f8
	long			fField1fc;			// +0x1fc
	UByte			fField200;
	UByte			fField201;
	UByte			fField202;			// +0x202
	UByte			fField203;			// +0x203
	long			fField204;			// +0x204
};


// What the calls answer.  The engine's codes have no names in the
// symbol table; these are ours, from what the callers make of them.
const long	kWordRecogOk			= 0;
const long	kWordRecogNothingToDo	= 2;	// already awake, or already asleep
const long	kWordRecogNoRecognizer	= 3;


/*--------------------------------------------------------------------
	Making it, and its life.
--------------------------------------------------------------------*/

// The block, with exactly the pointers `WordRecogDeallocate` gives
// back nilled and nothing else touched.  Throws `evt.ex.abt.stack`.
WordRecog*	WordRecogNew(void);								// ROM 0x00274970 WordRecogNew
// Everything, including the block.
void		WordRecogDestroy(WordRecog* wr);				// ROM 0x00274a08 WordRecogDestroy
// The arrays hung off a block that has its `fWordCount`; every failure
// throws, and the handler gives back whatever was got.
void		WordRecogAllocate(WordRecog* wr);				// ROM 0x00274a38 WordRecogAllocate
// ... and all of them given back and nilled, so it is safe twice over.
void		WordRecogDeallocate(WordRecog* wr);				// ROM 0x00274c08 WordRecogDeallocate

// A whole recogniser: the block, its arrays, its grammar and its
// starting values.  `checkWords` is where the readings will go and
// `wordCount` how many are wanted - with no callback, or fewer than
// one wanted, it makes one.  Throws `evt.ex.Rosetta` when there is no
// grammar to read against.
WordRecog*	WordRecogCreate2(void* field00, void* field04,
							WordRecogCheckWordsProc checkWords, long wordCount,
							RosGrammars* grammars, void* net, short ownsStrokes);	// ROM 0x00275940 WordRecogCreate2

// The arrays given back while the engine is quiet, and made again when
// it wakes.  The block itself does not move.
void		WordRecogSuspend(WordRecog* wr);				// ROM 0x00274cbc WordRecogSuspend
long		WordRecogResume(WordRecog* wr);					// ROM 0x002758d0 WordRecogResume

// Everything put back as the engine starts: the resolution unknown,
// the saved run filled with the engine's own numbers, the grammar's
// first context, "General" looked for by name, and then a clear.
void		WordRecogReset(WordRecog* wr);					// ROM 0x00275b14 WordRecogReset
// The writing forgotten.  `invalRun` also puts the run back as it was,
// which is what tells "a new word" from "the same word again".
void		WordRecogClear(WordRecog* wr, Boolean invalRun);	// ROM 0x00275d28 WordRecogClear
// The strokes and segments alone.  A stroke is only given back if the
// recogniser owns them all or the stroke is one the engine made
// itself; the others belong to whoever handed them over.
void		WordRecogClearStrokes(WordRecog* wr);			// ROM 0x00275f04 WordRecogClearStrokes

// The run put back as it was saved, and the run saved.
void		WordRecogInvalRun(WordRecog* wr);				// ROM 0x00275d94 WordRecogInvalRun
void		WordRecogSaveRun(WordRecog* wr);				// ROM 0x00275e48 WordRecogSaveRun

// The grammar context picked by name; answers kWordRecogOk when there
// is one of that name and 1 when there is not.
long		WordRecogSetContext(WordRecog* wr, const char* name);	// ROM 0x00276050 WordRecogSetContext


/*--------------------------------------------------------------------
	Measuring, and handing the readings back.
--------------------------------------------------------------------*/

// The tallest stroke of the word, plus one pixel; with no strokes at
// all, the height the run says a word has.
Fixed		WordRecogDetermineMaxHeight(WordRecog* wr);		// ROM 0x002760d0 WordRecogDetermineMaxHeight
// The cap height nudged towards what the word just read implies.
void		WordRecogComputeCapHeight(WordRecog* wr);		// ROM 0x00274818 WordRecogComputeCapHeight
// True if any dot in the run of strokes sits above the line.  `range`
// is a pair of shorts: the first stroke and how many.
Boolean		WordRecogDotIsHigh(WordRecog* wr, const short* range, Fixed y, Fixed height);	// ROM 0x00274910 WordRecogDotIsHigh

// The word finished: the cap height learnt from it, and the readings
// handed to whoever asked for them.
void		WordRecogEndWord(WordRecog* wr, char** words, UniChar* scores, long* flags,
							long strokes, long count);		// ROM 0x002746f0 WordRecogEndWord
void		WordRecogReturnWords(WordRecog* wr, char** words, UniChar* scores, long* flags,
							long strokes, long count);		// ROM 0x00274744 WordRecogReturnWords


/*--------------------------------------------------------------------
	Strokes in.
--------------------------------------------------------------------*/

// A stroke taken in, and the word closed.  `separation` says how sure
// the caller is that this stroke begins something new: under 0.4 the
// gap in front of it is a gap inside a letter, over 0.6 it is a gap
// between letters, and between the two it is not counted at all.
// `endWord` non-nought first closes what has been written - the
// baseline worked out, the strokes sorted, cut into characters and
// read - and the stroke may then be nil.
void	WordRecogAddStroke2(WordRecog* wr, RosStroke* stroke, Fixed advance, Fixed field04,
						long endWord, short how, Fixed separation);	// ROM 0x00274cf0 WordRecogAddStroke2

// The word cut into characters and read.  NOT YET.
void	WordRecogAnalyzeWord(WordRecog* wr);					// ROM 0x002766c0 WordRecogAnalyzeWord

// Whether the strokes of a word are sorted as groups (the pieces of a
// cut stroke kept together) rather than singly.  The ROM's initialised
// data has it set; `SetUpRosetta` turns it off when the writer has
// asked for no fragmentation.
extern ULong	FragmentLigatures;					// ROM 0x0c104f84 FragmentLigatures

// Where the last stroke taken in reached, so that the gap in front of
// the next one can be measured.  (They have no symbols of their own;
// they sit in the ROM's data just past `SegOnly`.)
extern Fixed	gLastStrokeRight;					// ROM 0x0c104fa0 (unnamed)
extern Fixed	gLastStrokeAdvance;					// ROM 0x0c104fa4 (unnamed)
extern UByte	gLastStrokeWasCut;					// ROM 0x0c104fa8 (unnamed)


/*--------------------------------------------------------------------
	What the recogniser makes of one stroke.
--------------------------------------------------------------------*/

// What `WordRecogStrokeType` answers.  A stroke is only said to go one
// way or the other if it is at least `SegmentMinStrokeSize` across its
// longer side: under that it has no shape worth talking about.
const long	kWordRecogStrokeNeither		= 0;
const long	kWordRecogStrokeHorizontal	= 1;
const long	kWordRecogStrokeVertical	= 2;

// Vertical if it is more than four times as tall as it is wide;
// horizontal if it is more than four times as wide as it is tall *and*
// no taller than a quarter of the engine's own small height.
long	WordRecogStrokeType(WordRecog* wr, const RosStroke* stroke);	// ROM 0x002765ac WordRecogStrokeType
// Wider than `multiple` of what a letter of this hand should be, at
// the scale the writing has turned out to be - and not a piece the
// engine cut for itself, because those are not cut again.
Boolean	WordRecogIsStrokeTooWide(WordRecog* wr, RosStroke* stroke, Fixed multiple);	// ROM 0x00276618 WordRecogIsStrokeTooWide
// True if two of the strokes already in hand have a vertical end that
// runs through this one: the test that tells a long horizontal stroke
// crossing two letters from a letter of its own.
Boolean	WordRecogStrokeIntersectsTwoVerticalStrokes(WordRecog* wr, const RosStroke* stroke);	// ROM 0x00276374 WordRecogStrokeIntersectsTwoVerticalStrokes
// Whether the stroke is to be cut in two before it is read.
Boolean	WordRecogStrokeNeedsFragmenting(WordRecog* wr, RosStroke* stroke);	// ROM 0x002762f4 WordRecogStrokeNeedsFragmenting

// How wide a stroke has to be, as a fraction of a letter of the hand
// being read, before it is looked at for cutting.  (0.45; the ROM's
// initialised data has it, and nothing writes it.)
extern Fixed	MinFragmentWidthMultiple;			// ROM 0x0c104f88 MinFragmentWidthMultiple


/*--------------------------------------------------------------------
	The gap cache.
--------------------------------------------------------------------*/

// Two strokes and the middle of the horizontal range each of them had:
// `WRSegWordXGap` measures the gap between two strokes and asks these
// first, `WordRecogAddStroke` shifts a new one in, and
// `WordRecogClearStrokes` forgets whichever entry names a stroke it is
// about to destroy.  (They have no symbols of their own; they sit in
// the ROM's data just past `MinFragmentWidthMultiple`.)
extern RosStroke*	gXGapStroke;		// ROM 0x0c104f94 (unnamed)
extern Fixed		gXGapMidX;			// ROM 0x0c104f8c (unnamed)
extern RosStroke*	gPrevXGapStroke;	// ROM 0x0c104f98 (unnamed)
extern Fixed		gPrevXGapMidX;		// ROM 0x0c104f90 (unnamed)

#endif	/* __WORDRECOG_H */
