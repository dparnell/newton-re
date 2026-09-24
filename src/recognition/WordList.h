/*
	File:		recognition/WordList.h

	Contains:	`TWordList`, the readings a word unit came to, in the form
				the rest of the system asks for them.

				A recogniser keeps its readings on the unit, one per
				interpretation, each with a score and a label; a word
				list is the same thing flattened out, and it is what
				`TUnitPublic::Words` hands over and what the word info
				frame is built from.

				The words themselves all live in *one* handle, packed
				end to end and separated by 0xFFFF, with a NUL after the
				last - so a list of sixteen readings is one allocation
				rather than seventeen.  That is why the area has its own
				pair of string functions, `Wstrlen` and `Wstrcpy`, which
				stop at 0xFFFF as well as at NUL: within the handle a
				word is a string that ends at either.  `ScanTo` walks to
				the n-th of them, `Word` copies one out into a handle of
				its own, and `Find` looks one up.  The scores and labels
				are two arrays of sixteen halfwords at the front of the
				object, so the list holds at most sixteen readings and
				`InsertLast` quietly drops the rest.

				`TWordList::operator new` keeps a pool of twelve
				preallocated lists and only falls back on the heap when
				they are all in use; a list is in use when its handle is
				not nil, which is what `operator delete` clears.  The
				recogniser makes and throws away word lists on every
				stroke, so this keeps the pointer heap from churning.

				`Reorder` is where the list stops being the recogniser's
				opinion alone.  A recogniser cannot tell '0' from 'O' or
				'1' from 'l' by the writing, so the list remembers what
				the writer has lately been choosing by hand - the *try
				string* - and moves the round and straight guesses up or
				down accordingly.

	Reconstructed from the MP2x00 US ROM (0x0022eb28-0x0022f2b8); each
	function cites its origin.
*/

#ifndef __WORDLIST_H
#define __WORDLIST_H

#include "RecObject.h"

// How many readings a list can hold, which is how many halfwords the
// two arrays at the front of it have room for.
enum { kMaxWordListEntries = 16 };

// how many lists are kept ready (TWordList::operator new)
enum { kPreallocatedWordLists = 12 };

// A character test, as BubbleGuess takes one: does this character
// belong to the class being looked for.
typedef UChar (*WordCharTestProc)(UniChar c);


// The word list's own string functions: within the packed handle a word
// ends at 0xFFFF as well as at NUL.
long		Wstrlen(const UniChar* str);					// ROM 0x0022ef8c Wstrlen__FPUs
UniChar*	Wstrcpy(UniChar* to, const UniChar* from);		// ROM 0x0022f190 Wstrcpy__FPUsT1 - NUL-terminated


// The character classes the reordering asks about: the round guesses
// ('0', 'O', 'o') and the straight ones ('1', 'I', 'l', 'i', '|'), which
// are the pairs a recogniser cannot tell apart from the writing alone.
UChar	IsSlash(UniChar c);								// ROM 0x0022ef20 IsSlash__FUs
UChar	IsCircular(UniChar c);							// ROM 0x0022ef3c IsCircular__FUs
UChar	IsLinear(UniChar c);							// ROM 0x0022ef60 IsLinear__FUs


// The try string: the characters the writer last chose by hand out of a
// list of guesses, which is how the recogniser remembers whether it is
// being written digits or letters at the moment.  It means to be a ring
// of two, and a character that is already in it clears it first -
// choosing the same character twice says the writer has settled on it
// rather than that they are alternating.
//
// ROM 0x0c104d64 gTryString / 0x0c104d6c gTryIndex
extern UniChar	gTryString[4];
extern long		gTryIndex;

void	ClearTryString(void);							// ROM 0x0022ee14 ClearTryString__Fv
void	AddTryString(UniChar c);						// ROM 0x0022ee38 AddTryString__FUs
UChar	InTryString(UniChar c);							// ROM 0x0022eed8 InTryString__FUs
long	TryStringLength(void);							// ROM 0x0022ee08 TryStringLength__Fv

void	RegisterWordListNatives(void);					// the four a script uses (WordList.cpp)


class TWordList
{
public:
	static void*		operator new(size_t size);			// ROM 0x0022edb0 __nw__9TWordListSFUi - out of the pool when one is free
	static void			operator delete(void* p, size_t size);	// ROM 0x0022ede0 __dl__9TWordListSFPvUi

						TWordList();						// ROM 0x0022eb28 __ct__9TWordListFv
						~TWordList();						// ROM 0x0022eb8c __dt__9TWordListFv

	long				Count(void);						// ROM 0x0022f2b0 Count__9TWordListFv
	Handle				Word(long index);					// ROM 0x0022f258 Word__9TWordListFl - a handle of its own ('wrdW')
	Handle				Ith(long index, long* score, long* label);	// ROM 0x0022ebc4 Ith__9TWordListFlPlT2 - the word ('wrdI') and what came with it
	long				Score(long index);					// ROM 0x0022ec20 Score__9TWordListFl
	long				Label(long index);					// ROM 0x0022ec2c Label__9TWordListFl

	void				InsertLast(UniChar** word, long score, long label);	// ROM 0x0022ec90 InsertLast__9TWordListFPPUslT2
	UniChar*			ScanTo(long index);					// ROM 0x0022ed60 ScanTo__9TWordListFl - the n-th word in the handle; nil past the end
	long				Find(UniChar** word);				// ROM 0x0022f1cc Find__9TWordListFPPUs - its index, -1 for not there
	void				SwapSingleCharacterGuesses(long a, long b);	// ROM 0x0022ec3c SwapSingleCharacterGuesses__9TWordListFlT1
	void				Reorder(void);						// ROM 0x0022f0a8 Reorder__9TWordListFv - the single-character guesses moved by what has been written lately
	void				BubbleGuess(UniChar c, WordCharTestProc test, long towardsFront);	// ROM 0x0022efbc BubbleGuess__9TWordListFUsPFUs_Ucl

	UShort				fScores[kMaxWordListEntries];	// +0x00
	UShort				fLabels[kMaxWordListEntries];	// +0x20
	UByte				fCount;							// +0x40
	Handle				fWords;							// +0x44  all of them, packed
};													// 0x48 bytes

// a word list as the array of strings a script reads (the words are
// disposed of as they are copied)
Ref		MakeStringArray(TWordList* list);				// ROM 0x001a0958 MakeStringArray__FP9TWordList

#endif	/* __WORDLIST_H */
