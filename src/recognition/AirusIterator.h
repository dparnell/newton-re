/*
	File:		recognition/AirusIterator.h

	Contains:	`TAirusIterator`, the cursor a script walks a dictionary
				with.

				`WalkDictionary` (recognition/Airus.h) runs through a
				dictionary from beginning to end and calls back for each
				word; an iterator does the opposite - it *stands* on one
				word and is asked for the next or the previous, so a
				script can stop, look and carry on.  That is harder than
				it sounds, because a dictionary is a trie: there is no
				"next word" to step to, only a shape to walk, and the
				walk has to be kept somewhere between calls.

				That somewhere is a stack of `charState`s, one per
				character of the word the cursor stands on.  Each of them
				holds the trie nodes reached at that character - up to
				four of them, because a lookup may be running down a
				*chain* of dictionaries at once - and, once it is asked
				for, the sorted set of characters that may come next.
				Stepping forward means taking the next of those
				characters, pushing a state for it and asking the engine
				to verify the word so far; stepping back means taking the
				previous one, and running off either end pops the state
				and carries on in the one below.

				A word is found when one of the parallel positions
				answers 1 (a prefix that carries an attribute) or 2 (a
				leaf): `ConstructResult` then walks the stack back down,
				picking the characters of the path that answered, and
				verifies the whole word once to read its attribute.

	Reconstructed from the MP2x00 US ROM (0x0002d994-0x0002e6e0); each
	function cites its origin.
*/

#ifndef __AIRUSITERATOR_H
#define __AIRUSITERATOR_H

#ifndef __AIRUS_H
#include "Airus.h"
#endif


// Two characters compared the way the dictionaries are ordered: through
// the Unicode collation, ignoring case - so `A` and `a` sort the same
// and the walk treats them as one character with two branches.
int		SortOrder(UByte a, UByte b);							// ROM 0x0002de9c SortOrder__FUcT1


// One of the trie positions a state stands on: which of the state
// below's next-characters led here, the node reached, and what the
// engine made of the word so far.
struct TAirusPosition
{
	long		fWhich;			// +0x00
	long		fNode;			// +0x04
	long		fResult;		// +0x08
};

// The most parallel positions one character may be at - the chain of
// dictionaries a lookup runs down is at most this long.
const long	kAirusMaxPositions	= 4;
// ... and the most characters that may follow one.  (The ROM's state is
// 0x444 bytes and its next-character entries are four each from +0x44,
// which is where this comes from.)
const long	kAirusMaxNextChars	= (0x440 - 0x44) / 4;

// One character of the walk.  The ROM lays the header out as position
// zero of the array - the index, the count and the cursor are the three
// words a position would have used - so `fPositions[n]` is at offset
// (n+1)*12, which is what the ROM's arithmetic computes.
struct charState
{
	long			fIndex;			// +0x00  the index of the character this state stands on (-1: none)
	long			fCount;			// +0x04  how many positions there are
	long			fCursor;		// +0x08  which of them the walk is on
	TAirusPosition	fPositions[kAirusMaxPositions];		// +0x0c
	long			fCharCount;		// +0x3c  how many characters may come next (-1: not asked yet)
	long			fCharCursor;	// +0x40  which of them the walk is on
	// each entry is the character and which position it came from
	UByte			fChars[kAirusMaxNextChars][4];		// +0x44
	charState*		fNext;			// +0x440  the state of the character before
};


class TAirusIterator
{
public:
					TAirusIterator(Handle dictionary);				// ROM 0x0002e260 __ct__14TAirusIteratorFPP15AirusAParmBlock
					TAirusIterator(const TAirusIterator& other);	// ROM 0x0002e2a8 __ct__14TAirusIteratorFRC14TAirusIterator
					~TAirusIterator();								// ROM 0x0002e360 __dt__14TAirusIteratorFv

	// The cursor put at a word: `atPrefix` starts it *at* the prefix
	// rather than walking up to where the prefix would be, and
	// `backwards` asks for the word before rather than the word after.
	// ==> whether there is a word there.
	Boolean			Reset(UByte* prefix, Boolean atPrefix, Boolean backwards);	// ROM 0x0002e38c Reset__14TAirusIteratorFPUcUcT2
	// The word it stands on, with what is stored beside it and the one
	// character that could follow it; ==> whether it stands on one.
	Boolean			ThisWord(UByte* word, ULong& attribute, UByte& terminal);	// ROM 0x0002e424 ThisWord__14TAirusIteratorFPUcRUlRUc
	Boolean			NextWord(void);									// ROM 0x0002e470 NextWord__14TAirusIteratorFv
	Boolean			PreviousWord(void);								// ROM 0x0002d994 PreviousWord__14TAirusIteratorFv

	// One character on, or one back: a state pushed for it and the word
	// so far verified.  ==> whether there was one.
	Boolean			VerifyNextChar(void);							// ROM 0x0002e594 VerifyNextChar__14TAirusIteratorFv
	Boolean			VerifyPrevChar(void);							// ROM 0x0002daac VerifyPrevChar__14TAirusIteratorFv
	// The word a position answered built out of the stack, and verified
	// once more to read its attribute.
	void			ConstructResult(TAirusPosition* position);		// ROM 0x0002dbe4 ConstructResult__14TAirusIteratorFP9charState
	// The stack started at a prefix already walked, or walked up to one.
	long			BuildStateAtPrefix(ULong length);				// ROM 0x0002dcdc BuildStateAtPrefix__14TAirusIteratorFUl
	void			BuildStateUpToPrefix(UByte* prefix, ULong length);	// ROM 0x0002dd78 BuildStateUpToPrefix__14TAirusIteratorFPUcUl
	// The characters that may follow the state's positions, gathered and
	// sorted into it.
	void			GetNextChars(void);								// ROM 0x0002def0 GetNextChars__14TAirusIteratorFv
	void			InsertNewNextChar(UByte c, int which);			// ROM 0x0002dfbc InsertNewNextChar__14TAirusIteratorFUci

	void			UnwindStateStack(void);							// ROM 0x0002e054 UnwindStateStack__14TAirusIteratorFv
	void			PushState(ULong index);							// ROM 0x0002e090 PushState__14TAirusIteratorFUl
	// The engine's current node and result kept as another position of
	// the state; ==> 1 when there was no room for one.
	long			AddParallelState(ULong which);					// ROM 0x0002e188 AddParallelState__14TAirusIteratorFUl
	Boolean			PopState(void);									// ROM 0x0002e1f0 PopState__14TAirusIteratorFv
	// ... and the other way: a position put back into the engine's block.
	void			RefreshState(TAirusPosition* position);			// ROM 0x0002e228 RefreshState__14TAirusIteratorFP9charState
	// The engine's block of this cursor's dictionary, read afresh each
	// time as the ROM does (the Handle may have moved).  The cursor
	// never goes through AE_Parms: that is only the block of whichever
	// dictionary the engine was last called on, which may since have
	// been disposed of.
	AirusAParmBlock*	Block(void) const		{ return (AirusAParmBlock*) *fDictionary; }

	Handle			fDictionary;	// +0x00
	UByte			fWorking[0x40];	// +0x04  the word the walk is building
	UByte			fWord[0x40];	// +0x44  the word it is standing on
	ULong			fAttribute;		// +0x84
	UByte			fTerminal;		// +0x88
	charState*		fStates;		// +0x8c
};

#endif	/* __AIRUSITERATOR_H */
