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

Boolean	HasSpaces(const UniChar* word);				// ROM 0x00256460 HasSpaces__FPUs
Boolean	HasChars(const UniChar* word);				// ROM 0x002564d4 HasChars__FPUs - a Roman letter anywhere in it
Boolean	IsPunctSymbol(const UniChar* word, long index);	// ROM 0x00256524 IsPunctSymbol__FPUsl
void	StripRecognitionWord(UniChar* word);		// ROM 0x0008eb8c StripRecognitionWord__FPUs - the punctuation taken off both ends
ULong	CheckCapAttributes(const UniChar* word);	// ROM 0x0008ec34 CheckCapAttributes__FPUs

Ref		FValidateWord(RefArg rcvr, RefArg word, RefArg options);	// ROM 0x0008ed50 FValidateWord
Ref		FLookupWord(RefArg rcvr, RefArg word);					// ROM 0x0008ef38 FLookupWord__FRC6RefVarT1

void	RegisterWordNatives(void);

#endif	/* __WORDS_H */
