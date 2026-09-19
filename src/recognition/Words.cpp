/*
	File:		recognition/Words.cpp

	Contains:	Word validation (Words.h).

	NOT YET RECONSTRUCTED: the dictionaries themselves
	(LookupWordOrVariant 0x0008f098 and everything under it), so no word is
	ever found and ValidateWord answers "the dictionaries do not have it"
	for all of them - which is what a machine whose user dictionary is
	empty says about a person's name anyway, and which is the answer that
	lets LookupWord hand the word back.  WRecVerifyWordSymbols 0x001444c8
	asks the word recogniser's domain, which is NOT YET: with no domain the
	ROM lets the word through, and so does this.  StripRecognitionWord's
	gEnabledLanguage and gWordID are the word recogniser's too.
*/

#include "Words.h"
#include "Frames.h"
#include "NativeFunctions.h"

#include <string.h>

const long kMaxWordLength = 63;		// what ValidateWord cuts a word down to


// ROM 0x00256460 HasSpaces__FPUs
Boolean
HasSpaces(const UniChar* word)
{
	for (; *word != 0; word++)
		if (*word == ' ')
			return true;
	return false;
}


// ROM 0x002564d4 HasChars__FPUs
// Whether there is a Roman letter anywhere in it: the ROM tests the two
// ASCII ranges, so a word of nothing but accented letters has none.
Boolean
HasChars(const UniChar* word)
{
	for (; *word != 0; word++)
		if ((*word > 'a' - 1 && *word < 'z' + 1)
		 || (*word > 'A' - 1 && *word < 'Z' + 1))
			return true;
	return false;
}


// ROM 0x00256524 IsPunctSymbol__FPUsl
// Whether the character at that index is punctuation to the recogniser.
// A closing bracket or a right single quote - the curly one, not the
// plain apostrophe - is not, when the character before it is an s, so
// that a possessive the recogniser wrote keeps its quote and "(s)" keeps
// its bracket.  The set is in order and the search gives up once it has
// gone past.
Boolean
IsPunctSymbol(const UniChar* word, long index)
{
	static const UniChar kPunctuation[] = {
		0x0021, 0x0022, 0x0027, 0x0028, 0x0029, 0x002c, 0x002e,	// ! " ' ( ) , .
		0x003a, 0x003b, 0x003f,									// : ; ?
		0x2018, 0x2019, 0x201c, 0x201d, 0						// the curly quotes
	};
	if (index != 0)
	{
		UniChar c = word[index];
		if ((c == 0x0029 || c == 0x2019) && (word[index - 1] == 's' || word[index - 1] == 'S'))
			return false;
	}
	long count = Ustrlen(kPunctuation);
	for (long i = 0; i < count; i++)
	{
		if (word[index] == kPunctuation[i])
			return true;
		if (kPunctuation[i] > word[index])
			break;
	}
	return false;
}


// ROM 0x0008eb8c StripRecognitionWord__FPUs
// The word made ready to look up: its diacriticals taken off unless the
// language is the one that keeps them (8) or the word recogniser wrote
// it, then the punctuation taken off the front and off the end.
//
// DEVIATION: gEnabledLanguage and gWordID belong to the word recogniser,
// which is NOT YET RECONSTRUCTED; with neither set the diacriticals
// always come off, which is what a machine writing English does.
void
StripRecognitionWord(UniChar* word)
{
	NoDiacriticsText(word, 0x7fffffff);
	long length = Ustrlen(word);
	long start = 0;
	while (start < length && IsPunctSymbol(word, start))
		start++;
	if (start != 0)
		memmove(word, word + start, (length - start + 1) * sizeof(UniChar));
	for (long i = Ustrlen(word) - 1; i >= 1; i--)
	{
		if (!IsPunctSymbol(word, i))
			break;
		word[i] = 0;
	}
}


// ROM 0x0008ec34 CheckCapAttributes__FPUs
// How the word is capitalised: 0x80 when its first letter is a
// a capital, 0x40 when the whole word is capitals.
ULong
CheckCapAttributes(const UniChar* word)
{
	ULong attributes = 0;
	if (UToLower(word[0]) != word[0])
		attributes = kCapStartsUpper;
	UniChar upper[kMaxWordLength + 1];
	Ustrncpy(upper, word, kMaxWordLength);
	UppercaseText(upper, 0x7fffffff);
	if (Ustrcmp(word, upper) == 0)
		attributes |= kCapAllUpper;
	return attributes;
}


// ROM 0x0008ed50 FValidateWord
// ValidateWord(word, options): the word looked over and looked up, its
// string changed where it lies (the punctuation stripped off), and a
// word of bits answered saying what was found.  kWordKnownOrBad is the
// one the caller usually wants: it is set when the dictionaries already
// have the word, when they have it under another capitalisation, and
// when it is not a word at all - too short, with spaces, without
// letters, or with symbols the recogniser does not write.
Ref
FValidateWord(RefArg /*rcvr*/, RefArg word, RefArg /*options*/)
{
	UniChar* text = (UniChar*) BinaryData(word);
	ULong flags = 0;
	long length = Ustrlen(text);
	if (length > kMaxWordLength)
	{
		text[kMaxWordLength] = 0;
		length = kMaxWordLength;
		flags = kWordTooLong;
	}
	StripRecognitionWord(text);
	if (Ustrlen(text) != length)
		flags |= kWordWasStripped;
	ULong caps = CheckCapAttributes(text);
	if ((caps & kCapStartsUpper) != 0)
		flags |= kWordStartsUpper;
	if ((caps & kCapAllUpper) != 0)
		flags |= kWordIsAllCaps;
	// NOT YET RECONSTRUCTED: LookupWordOrVariant 0x0008f098, the word and
	// the variants of it looked up in the dictionaries - it answers which
	// variant it found (-1 for none) and the variant's capitalisation
	// joins the word's.  With no dictionaries nothing is ever found.
	long found = -1;
	ULong variantCaps = caps;
	if (found != -1)
		flags |= kWordKnownOrBad;
	if (found == -1)
		flags |= kWordNotFound;
	else if ((variantCaps & 0xc0) == (caps & 0xc0))
		flags |= kWordFound;
	else
		flags |= kWordFoundOtherCase;
	if (Ustrlen(text) < 2)
		flags |= kWordTooShort;
	if (HasSpaces(text))
		flags |= kWordHasSpaces;
	if (!HasChars(text))
		flags |= kWordHasNoLetters;
	// NOT YET RECONSTRUCTED: WRecVerifyWordSymbols 0x001444c8 - with no
	// word recogniser domain the ROM finds nothing to object to
	if ((flags & kWordIsNotAWord) != 0)
		flags |= kWordKnownOrBad;
	if ((flags & kWordFoundOtherCase) != 0)
		flags |= kWordKnownOrBad;
	return MAKEINT(flags);
}


// ROM 0x0008ef38 FLookupWord__FRC6RefVarT1
// LookupWord(word): a copy of the word as ValidateWord leaves it -
// stripped of its punctuation - when it is a well-formed word the
// dictionaries do not already have, and nil otherwise.  It is the
// question "is this a new word?", which is what the Setup assistant asks
// of each part of a name that is typed in.
Ref
FLookupWord(RefArg /*rcvr*/, RefArg word)
{
	RefVar copy(Clone(word));
	RefVar flags(FValidateWord(RefVar(NILREF), copy, RefVar(NILREF)));
	if ((RINT(flags) & kWordKnownOrBad) != 0)
		return NILREF;
	return copy;
}


void
RegisterWordNatives(void)
{
	RegisterNativeFunction("FValidateWord", (void*) FValidateWord, 2);
	RegisterNativeFunction("FLookupWord__FRC6RefVarT1", (void*) FLookupWord, 1);
}
