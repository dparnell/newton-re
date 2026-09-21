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
#include "Stroke.h"
#include "Unit.h"			// AddRect
#include "Ports.h"			// RoundFixed
#include "Frames.h"
#include "NativeFunctions.h"
#include "Airus.h"
#include "Unicode.h"
#include "RSSymbols.h"
#include "ObjectHeap.h"
#include "ROMConstants.h"

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


// ROM 0x0008ebfc StripRecognitionWordDiacritsOK__FPUs
// The punctuation taken off both ends and nothing else: for the
// languages whose words are not the same without their diacriticals.
void
StripRecognitionWordDiacritsOK(UniChar* word)
{
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


// ROM 0x0008ec9c EncodeRecognitionWord__FPUs
// The word stripped where it lies; ==> 0x80 when what is left starts
// with a capital.
ULong
EncodeRecognitionWord(UniChar* word)
{
	StripRecognitionWord(word);
	return UToLower(word[0]) != word[0] ? kCapStartsUpper : 0;
}


// ROM 0x0008ecbc EncodeRecognitionWordDiacritsOK__FPUs
ULong
EncodeRecognitionWordDiacritsOK(UniChar* word)
{
	StripRecognitionWordDiacritsOK(word);
	return UToLower(word[0]) != word[0] ? kCapStartsUpper : 0;
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


/*------------------------------------------------------------------------------
	T h e   d i c t i o n a r i e s

	The word sources are frames in `vars.dictionaries`, each with a
	`dictID` of its own; a script asks for one by that id.  What is in
	them, and the looking up itself, is NOT YET RECONSTRUCTED
	(InitDictionaries 0x0013de2c and everything under it), so the list is
	empty and every id answers nil - which is what a machine that has
	loaded no dictionary answers too.
------------------------------------------------------------------------------*/

// ROM 0x0c101844 gWordID
// DEVIATION: the word recogniser is NOT YET RECONSTRUCTED, so nothing
// ever sets this and the system is told the recogniser is not reading.
ULong	gWordID = 0;


// ROM 0x0014444c FWRecIsBeingUsed
// WRecIsBeingUsed(): true while the word recogniser is the one reading
// what is written - it puts its own four characters in gWordID.
Ref
FWRecIsBeingUsed(RefArg /*rcvr*/)
{
	return MAKEBOOLEAN(gWordID == 'WREC');
}

// ROM 0x0013de2c InitDictionaries__Fv
// The dictionaries built and put in `vars.dictionaries`: the ROM's own
// (the words, the auxiliary lists, the expansions), then the user's off
// the system soup.
//
// The list itself is the ROM's own (`Rdictionarylist`, cloned), and each
// of its descriptors is wrapped in a clone of `canonicalDictRAMFrame`
// with the descriptor as its `_proto`, which is what gives every
// dictionary the frame a script talks to and the `dictID` it is found
// by.
//
// NOT YET RECONSTRUCTED: the dictionaries themselves - the ROM's word
// data (InitROMDictionaryData, GetROMDictionaryData,
// BuildDictionaryFromPtr), the empty ones the user's words go into
// (NewDictionary), the trie, and gDictList.  Each frame's `dict` slot
// therefore stays nil, which is what it holds for a dictionary the
// machine could not build.
void
InitDictionaries(void)
{
	// DEVIATION: a host that has not imported the ROM's objects has no
	// list to clone; an empty one keeps everything that takes its Length
	// happy, which is what the list is for.
	RefVar list(IsArray(RefVar(Rdictionarylist)) ? Clone(RefVar(Rdictionarylist)) : MakeArray(0));
	SetFrameSlot(RefVar(gVarFrame), RSSYMdictionaries, list);
	long count = Length(list);
	for (long i = 0; i < count; i++)
	{
		RefVar descriptor(GetArraySlotRef(list, i));
		RefVar romDictId(GetProtoVariable(descriptor, RSSYMromdictid, nil));
		RefVar frame(Clone(RefVar(Rcanonicaldictramframe)));
		SetFrameSlot(frame, RSSYM_proto, descriptor);
		SetFrameSlot(frame, RSSYMromdictid, romDictId);
		SetArraySlotRef(list, i, frame);
		long id = RINT(GetProtoVariable(descriptor, RSSYMdictid, nil));
		Handle dictionary = nil;
		if (ISNIL(romDictId))
		{
			// the three a user writes into start empty
			if (id == kUserDictionary || id == kExpandDictionary || id == kAutoAddDictionary)
				dictionary = NewDictionary(kAirusKindEnumRAM | kAirusLockedBit, 1);
			// NOT YET RECONSTRUCTED: gTrie, which is dictionary 32
		}
		// NOT YET RECONSTRUCTED: the ones built out of the ROM's own word
		// data (GetROMDictionaryData 0x0013dd28, BuildDictionaryFromPtr
		// 0x0002d624), and gDictList beside them
		if (dictionary != nil)
		{
			((AirusAParmBlock*) *dictionary)->fDictID = id & 0xffff;
			SetFrameSlot(frame, RSSYMdict, RefVar(AddressToRef(dictionary)));
		}
	}
}

// ROM 0x0013d460 Dictionaries__Fv
Ref
Dictionaries(void)
{
	return GetFrameSlotRef(gVarFrame, RSSYMdictionaries);
}


// ROM 0x0013e558 FindDictionaryFrame__FUl
// The dictionary of that id, or nil when none of them has it.
Ref
FindDictionaryFrame(ULong id)
{
	RefVar dictionaries(Dictionaries());
	long count = Length(dictionaries);
	for (long i = 0; i < count; i++)
	{
		RefVar dictionary(GetArraySlotRef(dictionaries, i));
		if ((ULong) RINT(GetProtoVariable(dictionary, RSSYMdictid, nil)) == id)
			return dictionary;
	}
	return NILREF;
}


// ROM 0x0013e988 FFindDictionaryFrame
// GetDictionary(id)
Ref
FFindDictionaryFrame(RefArg /*rcvr*/, RefArg id)
{
	return FindDictionaryFrame((ULong) RINT(id));
}

// ROM 0x0008ea78 GetScriptDictRef__FRC6RefVar
// The engine's dictionary behind a script's dictionary frame: the `dict`
// slot, which holds its address for one that lives in memory.
//
// NOT YET RECONSTRUCTED: ReadRefDictionary 0x0002d6a0, which builds one
// out of a binary in the slot - how a dictionary that came off a store
// or out of a package is reached.
Handle
GetScriptDictRef(RefArg dictionary)
{
	RefVar dict(GetFrameSlotRef(dictionary, RSSYMdict));
	if (ISNIL(dict))
		ThrowMsg("dict not initialized");
	if (((Ref) dict & 3) == 0)
		return (Handle) RefToAddress(dict);
	return nil;
}


// ROM 0x0008ee98 FAirusNew
// The frame given a dictionary of its own: an empty one of that kind,
// with that many bytes of attribute per word, remembered in its `dict`
// slot.  ==> what the engine made of it (0 when it worked).
Ref
FAirusNew(RefArg rcvr, RefArg type, RefArg attributeSize)
{
	Handle dictionary = NewDictionary((UByte) RINT(type), RINT(attributeSize));
	if (airusResult >= 0)
		SetFrameSlot(rcvr, RSSYMdict, RefVar(AddressToRef(dictionary)));
	return MAKEINT(airusResult);
}


// ROM 0x0008fb28 FAirusLookupWord
// LookupWord(word, result) on a dictionary frame: the word looked up,
// and, when it is one the dictionary has, the frame handed in given its
// `attribute` and the `terminalClass` - the character that would come
// next, when only one would.  ==> what the engine made of it: 1 the word
// is only the beginning of others, 2 it is that and a word too, 3 it is
// a word and nothing goes on from it, -6 nothing begins that way.
Ref
FAirusLookupWord(RefArg rcvr, RefArg word, RefArg result)
{
	Handle dictionary = GetScriptDictRef(rcvr);
	UByte text[64];
	ConvertFromUnicode(GetCString(word), text, kMacRomanEncoding, 0x3f);
	VerifyStart(dictionary);
	void* terminal = nil;
	ULong* attribute = nil;
	VerifyString(dictionary, text, &terminal, &attribute, nil);
	if ((airusResult == kAirusIsPrefixAndWord || airusResult == kAirusIsWord) && NOTNIL(result))
	{
		SetFrameSlot(result, RSSYMattribute,
					 attribute == nil ? RefVar(NILREF) : RefVar(MAKEINT(*attribute)));
		SetFrameSlot(result, RSSYMterminalclass,
					 terminal == nil ? RefVar(NILREF) : RefVar(MAKEINT(*(UByte*) terminal)));
	}
	return MAKEINT(airusResult);
}


// ROM 0x0008fc3c FAirusAddWord
// AddWord(word, attribute) on a dictionary frame.  ==> 0 it went in, 4
// it was there already.
Ref
FAirusAddWord(RefArg rcvr, RefArg word, RefArg attribute)
{
	Handle dictionary = GetScriptDictRef(rcvr);
	UByte text[64];
	ConvertFromUnicode(GetCString(word), text, kMacRomanEncoding, 0x3f);
	AddWord(dictionary, 0, text, (ULong) RINT(attribute));
	return MAKEINT(airusResult);
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


// ROM 0x0008eff4 FStripRecognitionWord
// StripRecognitionWord(word): the string stripped where it lies; ==> the
// capitalisation bit.
Ref
FStripRecognitionWord(RefArg /*rcvr*/, RefArg word)
{
	return MAKEINT(EncodeRecognitionWord((UniChar*) BinaryData(word)));
}


// ROM 0x0008f030 FStripRecognitionWordDiacritsOK
Ref
FStripRecognitionWordDiacritsOK(RefArg /*rcvr*/, RefArg word)
{
	return MAKEINT(EncodeRecognitionWordDiacritsOK((UniChar*) BinaryData(word)));
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
	RegisterNativeFunction("FFindDictionaryFrame", (void*) FFindDictionaryFrame, 1);
	RegisterNativeFunction("FAirusNew", (void*) FAirusNew, 2);
	RegisterNativeFunction("FAirusLookupWord", (void*) FAirusLookupWord, 2);
	RegisterNativeFunction("FAirusAddWord", (void*) FAirusAddWord, 2);
	RegisterNativeFunction("FWRecIsBeingUsed", (void*) FWRecIsBeingUsed, 0);
	RegisterNativeFunction("FStripRecognitionWord", (void*) FStripRecognitionWord, 1);
	RegisterNativeFunction("FStripRecognitionWordDiacritsOK", (void*) FStripRecognitionWordDiacritsOK, 1);
}


/*------------------------------------------------------------------------------
	W h e r e   a   w o r d   s i t s
------------------------------------------------------------------------------*/

// ROM 0x00065b2c FindBaseline__FPP7TStrokeP5Point
// The four corners a word of strokes would be laid out in.
//
// The recogniser is asked first: the strokes are turned into the trace
// its feature extractor works on and `low_level` is run over it, which
// among everything else says where the short letters stand and how far
// up they reach.  When that cannot be done the strokes' own box is used
// instead - the word then sits entirely above its baseline, which is the
// best that can be said without reading it - and the answer is 1 rather
// than 0 to say so.
//
// A box with no width, or none with no height, is given one, because
// nothing downstream divides by nought happily.
//
// NOT YET RECONSTRUCTED: GetTraceFromStrokes and low_level - the CIC
// handwriting library's feature extractor, which is what would read the
// word - so this always takes the second path.
long
FindBaseline(TStroke** strokes, Point* out)
{
	long failed = 1;			// (0 once low_level has been asked and answered)
	long upper = 0;
	long upperRight = 0;
	long base = 0;
	long baseRight = 0;

	FRect box;
	Boolean first = true;
	for (long i = 0; strokes[i] != nil; i++)
	{
		AddRect(&strokes[i]->fBBox, &box, first);
		first = false;
	}
	if (first)
		SetRectangleEmpty(&box);
	long left = RoundFixed(box.left);
	long right = RoundFixed(box.right);
	if (left == right)
		right = right + 1;
	if (base == 0)
	{
		upper = RoundFixed(box.top);
		base = RoundFixed(box.bottom);
		upperRight = upper;
		baseRight = base;
		if (upper == base)
		{
			base = base + 1;
			baseRight = base;
		}
	}
	out[0].h = (short) left;	out[0].v = (short) upper;
	out[1].h = (short) right;	out[1].v = (short) upperRight;
	out[2].h = (short) left;	out[2].v = (short) base;
	out[3].h = (short) right;	out[3].v = (short) baseRight;
	return failed;
}


// ROM 0x001444c4 WRecFindBaseline__FPP7TStrokeP5Point
// The word recogniser's name for it; in the ROM one instruction that
// branches straight to FindBaseline.
long
WRecFindBaseline(TStroke** strokes, Point* out)
{
	return FindBaseline(strokes, out);
}

