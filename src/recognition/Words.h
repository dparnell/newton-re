/*
	File:		recognition/Words.h

	Contains:	What the system asks about a written or typed word before it
				keeps it: whether it is a word at all, how it is capitalised,
				and whether the dictionaries know it.  The recogniser uses it
				for what it reads; the Setup assistant uses it on the name
				that is typed in.

	Reconstructed from the MP2x00 US ROM (0x0008eb8c-0x0008efc0,
	0x00256460-0x002565d0); each function cites its origin.
*/

#ifndef __WORDS_H
#define __WORDS_H

#ifndef __OBJECTS_H
#include "objects.h"
#endif
#ifndef __UNICODE_H
#include "Unicode.h"
#endif
#ifndef __NEWTONMEMORY_H
#include "NewtonMemory.h"
#endif

// what ValidateWord answers (the bits it sets in its result)
enum
{
	kWordTooLong			= 0x0001,	// longer than 63 characters (it is cut down)
	kWordTooShort			= 0x0002,	// fewer than two characters
	kWordHasSpaces			= 0x0004,
	kWordHasNoLetters		= 0x0008,
	kWordHasBadSymbols		= 0x0010,	// a character the word recogniser does not write
	kWordIsNotAWord			= 0x003f,	// any of the above: the word is refused
	kWordNotFound			= 0x0040,	// the dictionaries do not have it
	kWordKnownOrBad			= 0x0080,	// the dictionaries have it, or it is not a word, or it is known by another capitalisation - LookupWord answers nil for it
	kWordWasStripped		= 0x0100,	// punctuation was taken off the ends
	kWordFoundOtherCase		= 0x1000,	// found, but capitalised differently
	kWordFound				= 0x2000,	// found, capitalised the same way
	kWordStartsUpper		= 0x0400,	// its first letter is a capital
	kWordIsAllCaps			= 0x0800
};

// what CheckCapAttributes answers
enum
{
	kCapStartsUpper			= 0x80,	// its first letter is a capital (it changes when lowered)
	kCapAllUpper			= 0x40
};

// Where a word of strokes sits: the four corners of the box the
// recogniser would lay it out in - the two ends of the line its short
// letters reach up to, and the two ends of the line they stand on.
//
//	out[0]	the left of the upper line		out[1]	its right
//	out[2]	the left of the baseline		out[3]	its right
//
// ==> 0 when the recogniser worked them out, 1 when it could not and the
// strokes' own box was used instead.
class TStroke;
long	FindBaseline(TStroke** strokes, Point* out);	// ROM 0x00065b2c FindBaseline__FPP7TStrokeP5Point
long	WRecFindBaseline(TStroke** strokes, Point* out);	// ROM 0x001444c4 WRecFindBaseline__FPP7TStrokeP5Point
Boolean	WRecVerifyWordSymbols(UniChar* word);				// ROM 0x001444c8 WRecVerifyWordSymbols__FPUs - whether the recogniser in use can write the word's symbols


Boolean	HasSpaces(const UniChar* word);				// ROM 0x00256460 HasSpaces__FPUs
Boolean	HasChars(const UniChar* word);				// ROM 0x002564d4 HasChars__FPUs - a Roman letter anywhere in it
Boolean	IsPunctSymbol(const UniChar* word, long index);	// ROM 0x00256524 IsPunctSymbol__FPUsl
void	StripRecognitionWord(UniChar* word);		// ROM 0x0008eb8c StripRecognitionWord__FPUs - the punctuation taken off both ends
void	StripRecognitionWordDiacritsOK(UniChar* word);	// ROM 0x0008ebfc StripRecognitionWordDiacritsOK__FPUs - the same, the diacriticals left on
ULong	EncodeRecognitionWord(UniChar* word);			// ROM 0x0008ec9c EncodeRecognitionWord__FPUs - stripped; ==> 0x80 when it starts with a capital
ULong	EncodeRecognitionWordDiacritsOK(UniChar* word);	// ROM 0x0008ecbc EncodeRecognitionWordDiacritsOK__FPUs
ULong	CheckCapAttributes(const UniChar* word);	// ROM 0x0008ec34 CheckCapAttributes__FPUs

// the dictionaries the words are looked up in: vars.dictionaries, a
// frame each, told apart by their dictID
// the word recogniser's own id, 'WREC when it is the one reading; 0
// while none is
// The dictionaries, as the word list asks them: LookupWord answers
// where the word was found, -1 for nowhere, and ExpandWord answers
// the word as it would be written out in full (a handle, the
// caller's to dispose), nil when there is nothing to expand
// (Dictionaries.cpp, Learning.cpp).
class TRecArea;
class TDictChain;
void	BuildChains(TDictChain** chains, RefArg config);		// ROM 0x0013d808 BuildChains__FPP10TDictChainRC6RefVar (Dictionaries.cpp)

long	LookupWord(const UniChar* word, ULong* where);		// ROM 0x0013f4f4 LookupWord__FPUsPUl (recognition/Dictionaries.cpp)

// Whether the writer has asked for the recogniser to be taught by
// what they write (the "learning" preference).
extern Boolean	gSaveWordTrainingData;		// ROM 0x0c101864 gSaveWordTrainingData

// ROM 0x0c101848 gEnabledLanguage
// Which language the dictionaries are read in: 8 when the locale
// names one - and a language that names itself keeps its
// diacriticals - 1 when it does not.
extern long		gEnabledLanguage;

extern ULong	gWordID;						// ROM 0x0c101844 gWordID

// the dictionaries a user writes into, which start empty
const long	kUserDictionary		= 31;
const long	kExpandDictionary	= 35;
const long	kAutoAddDictionary	= 36;

Ref		Dictionaries(void);						// ROM 0x0013d460 Dictionaries__Fv
Ref		FindDictionaryFrame(ULong id);			// ROM 0x0013e558 FindDictionaryFrame__FUl

Ref		FValidateWord(RefArg rcvr, RefArg word, RefArg options);	// ROM 0x0008ed50 FValidateWord
Ref		FLookupWord(RefArg rcvr, RefArg word);					// ROM 0x0008ef38 FLookupWord__FRC6RefVarT1
Ref		FFindDictionaryFrame(RefArg rcvr, RefArg id);			// ROM 0x0013e988 FFindDictionaryFrame
Ref		FWRecIsBeingUsed(RefArg rcvr);							// ROM 0x0014444c FWRecIsBeingUsed
Handle	GetScriptDictRef(RefArg dictionary);					// ROM 0x0008ea78 GetScriptDictRef__FRC6RefVar
Ref		FAirusNew(RefArg rcvr, RefArg type, RefArg attributeSize);	// ROM 0x0008ee98 FAirusNew
Ref		FAirusLookupWord(RefArg rcvr, RefArg word, RefArg result);	// ROM 0x0008fb28 FAirusLookupWord
Ref		FAirusAddWord(RefArg rcvr, RefArg word, RefArg attribute);	// ROM 0x0008fc3c FAirusAddWord
Ref		FAirusDeleteWord(RefArg rcvr, RefArg word);				// ROM 0x0008fcb4 FAirusDeleteWord
Ref		FAirusDeletePrefix(RefArg rcvr, RefArg word);			// ROM 0x0008fd08 FAirusDeletePrefix
Ref		FAirusWalkDictionary(RefArg rcvr, RefArg prefix, RefArg fn);	// ROM 0x0008f44c FAirusWalkDictionary
Ref		FAirusChangeAttribute(RefArg rcvr, RefArg word, RefArg attribute);	// ROM 0x0008eb18 FAirusChangeAttribute
Ref		FAirusDictionaryType(RefArg rcvr);						// ROM 0x0008fae0 FAirusDictionaryType
Ref		FAirusAttributeSize(RefArg rcvr);						// ROM 0x0008fb0c FAirusAttributeSize
Ref		FAirusDispose(RefArg rcvr);								// ROM 0x0008f84c FAirusDispose
// The cursor a script walks a dictionary with (recognition/AirusIterator.h).
Ref		FAirusIteratorMake(RefArg rcvr);						// ROM 0x0008f4e8 FAirusIteratorMake
Ref		FAirusIteratorClone(RefArg rcvr);						// ROM 0x0008f680 FAirusIteratorClone
Ref		FAirusIteratorReset(RefArg rcvr, RefArg word, RefArg exact, RefArg which);	// ROM 0x0008f788 FAirusIteratorReset
Ref		FAirusIteratorThisWord(RefArg rcvr, RefArg result);		// ROM 0x0008f888 FAirusIteratorThisWord
Ref		FAirusIteratorNextWord(RefArg rcvr);					// ROM 0x0008f998 FAirusIteratorNextWord
Ref		FAirusIteratorPreviousWord(RefArg rcvr);				// ROM 0x0008f9bc FAirusIteratorPreviousWord
Ref		FAirusIteratorDispose(RefArg rcvr);						// ROM 0x0008f9e0 FAirusIteratorDispose
Ref		FStripRecognitionWord(RefArg rcvr, RefArg word);			// ROM 0x0008eff4 FStripRecognitionWord
Ref		FStripRecognitionWordDiacritsOK(RefArg rcvr, RefArg word);	// ROM 0x0008f030 FStripRecognitionWordDiacritsOK

void	RegisterWordNatives(void);

#endif	/* __WORDS_H */
