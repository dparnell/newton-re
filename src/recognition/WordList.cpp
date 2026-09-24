/*
	File:		recognition/WordList.cpp

	Contains:	The readings a word unit came to - WordList.h.
*/

#include "WordList.h"
#include "Unicode.h"
#include "NewtonMemory.h"
#include "NativeFunctions.h"
#include "Frames.h"
#include "Objects.h"

#include <stdlib.h>
#include <string.h>

// the tags the handles are made under, which is how they are told apart
// in a heap dump
enum
{
	kWordListHandleName	= 'wlst',		// the packed words of a list
	kWordCopyHandleName	= 'wrdW',		// one word, out of Word
	kWordIthHandleName	= 'wrdI'		// one word, out of Ith
};

// What separates one word from the next inside the packed handle.  A
// word therefore ends at 0xFFFF as well as at NUL, which is the whole
// reason for Wstrlen and Wstrcpy.
const UniChar kWordSeparator = 0xffff;


/*------------------------------------------------------------------------------
	T h e   p a c k e d   s t r i n g s
------------------------------------------------------------------------------*/

// ROM 0x0022ef8c Wstrlen__FPUs
// How long the word at `str` is: it ends at the separator or at NUL.
long
Wstrlen(const UniChar* str)
{
	long length = 0;
	UniChar c;
	while ((c = *str) != 0 && c != kWordSeparator)
	{
		str++;
		length++;
	}
	return length;
}


// ROM 0x0022f190 Wstrcpy__FPUsT1
// The word at `from` copied out, NUL-terminated however it ended.
UniChar*
Wstrcpy(UniChar* to, const UniChar* from)
{
	UniChar c;
	while ((c = *from) != 0 && c != kWordSeparator)
	{
		from++;
		*to++ = c;
	}
	*to = 0;
	return to;
}


/*------------------------------------------------------------------------------
	T h e   p o o l
------------------------------------------------------------------------------*/

// ROM 0x0c107030 gPreallocWordLists
// Twelve lists kept ready, 0x360 bytes of them.  A slot is free when
// its handle is nil, which is what the destructor and operator delete
// leave behind; zeroed storage therefore starts out all free.
//
// (DEVIATION: the ROM's pool is a zero-initialised RAM array of the
//  class; here it is raw storage of the same size, because a static
//  array of TWordList would run the constructor on each one.)
union WordListSlot
{
	double		fAlign;
	UByte		fBytes[sizeof(TWordList)];
};
static WordListSlot		gPreallocWordLists[kPreallocatedWordLists];


// ROM 0x0022edb0 __nw__9TWordListSFUi
// A list out of the pool when one is free, off the heap when they are
// all in use.  The recogniser makes and throws away word lists on every
// stroke, which is what the pool is for.
void*
TWordList::operator new(size_t size)
{
	TWordList* slot = (TWordList*) gPreallocWordLists;
	for (long i = 0; i < kPreallocatedWordLists; i++, slot++)
		if (slot->fWords == nil)
			return slot;
	// (the ROM calls the C++ runtime's new_handler when the heap has
	//  nothing either)
	if (size == 0)
		size = 1;
	return malloc(size);
}


// ROM 0x0022ede0 __dl__9TWordListSFPvUi
// The handle is nilled first, which is what gives a pooled slot back;
// one that came off the heap goes back to it.
void
TWordList::operator delete(void* p, size_t /*size*/)
{
	((TWordList*) p)->fWords = nil;
	if (p >= (void*) gPreallocWordLists
		&& p < (void*) (gPreallocWordLists + kPreallocatedWordLists))
		return;
	free(p);
}


/*------------------------------------------------------------------------------
	T h e   l i s t
------------------------------------------------------------------------------*/

// ROM 0x0022eb28 __ct__9TWordListFv
// Empty: no readings, and a handle holding nothing but its terminator.
TWordList::TWordList()
{
	fCount = 0;
	fWords = NewHandle(sizeof(UniChar));
	if (fWords != nil)
	{
		SetHandleName(fWords, kWordListHandleName);
		*(UniChar*) *fWords = 0;
	}
}


// ROM 0x0022eb8c __dt__9TWordListFv
TWordList::~TWordList()
{
	if (fWords != nil)
		DisposHandle(fWords);
	fWords = nil;
}


// ROM 0x0022f2b0 Count__9TWordListFv
long
TWordList::Count(void)
{
	return fCount;
}


// ROM 0x0022ed60 ScanTo__9TWordListFl
// The n-th word within the packed handle; nil when the text ran out
// first.
//
// (BUG, kept: the nil is only noticed on the way round the loop, so an
//  index more than one past the last word carries on reading from it.
//  Every caller asks for an index below the count, so it does not
//  happen in practice.)
UniChar*
TWordList::ScanTo(long index)
{
	UniChar* at = (UniChar*) *fWords;
	long remaining = index - 1;
	if (index == 0)
		return at;
	for (;;)
	{
		UniChar c;
		while ((c = *at) != 0 && c != kWordSeparator)
			at++;
		if (c == 0)
			at = nil;
		else
			at++;					// past the separator
		if (remaining-- == 0)
			return at;
	}
}


// ROM 0x0022f258 Word__9TWordListFl
// The n-th word copied out into a handle of its own.  The list is
// scanned twice because making the handle may move it.
//
// (BUG, kept: the new handle is not checked, so with no memory the copy
//  writes through nil.)
Handle
TWordList::Word(long index)
{
	long length = Wstrlen(ScanTo(index));
	Handle word = NewHandle((length + 1) * (long) sizeof(UniChar));
	SetHandleName(word, kWordCopyHandleName);
	Wstrcpy((UniChar*) *word, ScanTo(index));
	return word;
}


// ROM 0x0022ebc4 Ith__9TWordListFlPlT2
// The n-th word and what came with it.
Handle
TWordList::Ith(long index, long* score, long* label)
{
	Handle word = Word(index);
	SetHandleName(word, kWordIthHandleName);
	if (score != nil)
		*score = fScores[index];
	if (label != nil)
		*label = fLabels[index];
	return word;
}


// ROM 0x0022ec20 Score__9TWordListFl
long
TWordList::Score(long index)
{
	return fScores[index];
}


// ROM 0x0022ec2c Label__9TWordListFl
long
TWordList::Label(long index)
{
	return fLabels[index];
}


// ROM 0x0022ec90 InsertLast__9TWordListFPPUslT2
// Another reading on the end.  The one before it had a NUL after it;
// that NUL becomes the separator and the new word is written where it
// stood.  A list already holding sixteen readings quietly drops the
// rest, there being no more room in the two arrays.
void
TWordList::InsertLast(UniChar** word, long score, long label)
{
	if (fCount > kMaxWordListEntries - 1)
		return;
	long length = Ustrlen(*word);
	long at = 0;
	if (fCount != 0)
	{
		// (Ustrlen, not Wstrlen: this is the length of everything
		//  packed in so far, the separators counting as characters)
		at = Ustrlen((UniChar*) *fWords) + 1;
		((UniChar*) *fWords)[at - 1] = kWordSeparator;
	}
	SetHandleSize(fWords, (at + length + 1 + 1) * (long) sizeof(UniChar));
	BlockMove(*word, (UniChar*) *fWords + at, (length + 1) * (long) sizeof(UniChar));
	fScores[fCount] = (UShort) score;
	fLabels[fCount] = (UShort) label;
	fCount++;
}


// ROM 0x0022f1cc Find__9TWordListFPPUs
// Which reading this is, -1 for none of them.
//
// (BUG, kept: a reading is compared only as far as it goes, so a word
//  longer than the one in the list matches it - "01" finds the reading
//  "0".  The callers all look for single characters, where it only
//  matters for an empty reading, which cannot be inserted.)
long
TWordList::Find(UniChar** word)
{
	UniChar* at = (UniChar*) *fWords;
	if (*at == 0)
		return -1;
	long index = 0;
	for (;;)
	{
		const UniChar* wanted = *word;
		Boolean same = true;
		UniChar c;
		while ((c = *at) != 0 && c != kWordSeparator)
		{
			at++;
			if (c != *wanted)
				same = false;
			wanted++;
		}
		if (same)
			return index;
		if (*at == kWordSeparator)
		{
			index++;
			at++;
		}
		if (*at == 0)
			return -1;
	}
}


// ROM 0x0022ec3c SwapSingleCharacterGuesses__9TWordListFlT1
// The first characters of two readings exchanged, which is all that is
// needed when both are one character long.
void
TWordList::SwapSingleCharacterGuesses(long a, long b)
{
	UniChar* first = ScanTo(a);
	UniChar* second = ScanTo(b);
	UniChar c = *first;
	*first = *second;
	*second = c;
}


/*------------------------------------------------------------------------------
	W h a t   t h e   w r i t e r   h a s   b e e n   w r i t i n g
------------------------------------------------------------------------------*/

// The characters a recogniser cannot tell apart from the writing alone.
// A '0' and an 'O' are the same shape; so are '1', 'I', 'l', 'i' and
// '|'.  All the recogniser can do is offer both and let the list decide
// which to put first.

// ROM 0x0022ef20 IsSlash__FUs
UChar
IsSlash(UniChar c)
{
	return (UChar) (c == '/');
}


// ROM 0x0022ef3c IsCircular__FUs
UChar
IsCircular(UniChar c)
{
	return (UChar) (c == 'o' || c == 'O' || c == '0');
}


// ROM 0x0022ef60 IsLinear__FUs
UChar
IsLinear(UniChar c)
{
	return (UChar) (c == '1' || c == 'I' || c == 'l' || c == 'i' || c == '|');
}


// ROM 0x0c104d64 gTryString / 0x0c104d6c gTryIndex
UniChar		gTryString[4];
long		gTryIndex;


// ROM 0x0022ee08 TryStringLength__Fv
long
TryStringLength(void)
{
	return Ustrlen(gTryString);
}


// ROM 0x0022ee14 ClearTryString__Fv
void
ClearTryString(void)
{
	gTryString[0] = 0;
	gTryIndex = 0;
}


// ROM 0x0022eed8 InTryString__FUs
// Whether the writer has lately chosen this character by hand.
UChar
InTryString(UniChar c)
{
	for (const UniChar* at = gTryString; ; at++)
	{
		UniChar d = *at;
		if (d == 0)
			return 0;
		if (d == c)
			return 1;
	}
}


// ROM 0x0022ee38 AddTryString__FUs
// A character the writer picked out of a list of guesses.  A character
// that is already there clears the string first: choosing the same one
// twice says the writer has settled on it, not that they are
// alternating between two.
//
// (BUG, kept: this means to be a ring of two, and the index does cycle
//  0, 1, 0, 1 - but the wrap happens *after* the write, so the third
//  character lands on the terminator and the string becomes three long.
//  The third character then stays there for ever, because nothing
//  writes position 2 again.  Nothing notices: the only thing ever asked
//  of the string is its first character and whether a character is in
//  it.)
void
AddTryString(UniChar c)
{
	if (InTryString(c))
		ClearTryString();
	long length = Ustrlen(gTryString);
	long index = gTryIndex;
	gTryIndex = index + 1;
	if (length >= 2)
	{
		gTryString[index] = c;
		if (gTryIndex >= 2)
			gTryIndex = 0;
	}
	else
	{
		gTryString[index] = c;
		gTryString[gTryIndex] = 0;
	}
}


// ROM 0x0022f0a8 Reorder__9TWordListFv
// The single-character guesses moved by what the writer has lately been
// choosing: writing letters pushes '0', '1' and '|' towards the back of
// the list, and writing digits or a slash pulls '0' and '1' towards the
// front.  With one guess there is nothing to reorder, and with a first
// try character that is neither a letter nor a digit nor a slash there
// is nothing to go on.
//
// (The two sides are not mirror images: the letters case moves three
//  guesses and the digits case only two - '|' is pushed away from
//  letters but never pulled towards digits.)
void
TWordList::Reorder(void)
{
	if (Count() <= 1)
		return;
	UniChar tried = gTryString[0];
	Boolean alphabetic = IsAlphabet(tried) != 0;
	Boolean numeric = (IsDigit(tried) != 0 || IsSlash(tried) != 0);
	if (alphabetic)
	{
		BubbleGuess('0', IsCircular, 0);
		BubbleGuess('1', IsLinear, 0);
		BubbleGuess('|', IsLinear, 0);
	}
	else
	{
		if (!numeric)
			return;
		BubbleGuess('0', IsCircular, 1);
		BubbleGuess('1', IsLinear, 1);
	}
}


// ROM 0x0022efbc BubbleGuess__9TWordListFUsPFUs_Ucl
// The one-character reading `c` moved along the list, past every
// neighbour that is also a single character of the class `test` asks
// for: towards the front when `towardsFront`, towards the back
// otherwise.  It stops at the first neighbour that is not - the order
// of the rest of the list is the recogniser's and is left alone.
void
TWordList::BubbleGuess(UniChar c, WordCharTestProc test, long towardsFront)
{
	UniChar wanted[2];
	UniChar* wantedPtr = wanted;
	wanted[0] = c;
	wanted[1] = 0;

	long count = Count();
	long at = Find(&wantedPtr);
	if (at < 0)
		return;

	long step;
	long stop;
	if (towardsFront == 0)
	{
		step = 1;
		stop = count;
	}
	else
	{
		step = -1;
		stop = -1;
	}
	at += step;
	if (at < 0 || at >= count)
		return;
	for (; at != stop; at += step)
	{
		UniChar* word = ScanTo(at);
		if (Wstrlen(word) != 1)
			return;
		if (!test(*word))
			return;
		SwapSingleCharacterGuesses(at - step, at);
	}
}


// ROM 0x001a0958 MakeStringArray__FP9TWordList
// A word list as an array of strings, which is the form a script reads
// the alternatives in.  Each word comes out of the list as a handle of
// its own, so it is disposed of once it has been copied.
Ref
MakeStringArray(TWordList* list)
{
	long count = list->Count();
	RefVar result(MakeArray(count));
	for (long i = 0; i < count; i++)
	{
		Handle word = list->Word(i);
		SetArraySlot(result, i, RefVar(MakeString(*(UniChar**) word)));
		DisposHandle(word);
	}
	return result;
}


/*------------------------------------------------------------------------------
	T h e   t r y   s t r i n g ,   f r o m   a   s c r i p t
------------------------------------------------------------------------------*/

// ROM 0x001a14e8 FClearTryString
// ClearTryString(): the last few characters the writer picked by hand,
// forgotten.
static Ref
FClearTryString(RefArg /*rcvr*/)
{
	ClearTryString();
	return NILREF;
}


// ROM 0x001a1518 FAddTryString
// AddTryString(char): one remembered.
static Ref
FAddTryString(RefArg /*rcvr*/, RefArg c)
{
	AddTryString(RCHAR(c));
	return NILREF;
}


// ROM 0x001a156c FInTryString
// InTryString(char): whether it is one of them.
static Ref
FInTryString(RefArg /*rcvr*/, RefArg c)
{
	return MAKEBOOLEAN(InTryString(RCHAR(c)));
}


// ROM 0x001a1500 FTryStringLength
// TryStringLength(): how many are remembered.
static Ref
FTryStringLength(RefArg /*rcvr*/)
{
	return MAKEINT(TryStringLength());
}


void
RegisterWordListNatives(void)
{
	RegisterNativeFunction("FClearTryString", (void*) FClearTryString, 0);
	RegisterNativeFunction("FAddTryString", (void*) FAddTryString, 1);
	RegisterNativeFunction("FInTryString", (void*) FInTryString, 1);
	RegisterNativeFunction("FTryStringLength", (void*) FTryStringLength, 0);
}
