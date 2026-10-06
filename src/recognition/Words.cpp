/*
	File:		recognition/Words.cpp

	Contains:	Word validation (Words.h).

	ValidateWord asks the rest of the recognition system two questions:
	the dictionaries whether they have the word or a variant of it
	(LookupWordOrVariant, Dictionaries.h), and the word recogniser in use
	whether it can write its symbols (WRecVerifyWordSymbols).
	StripRecognitionWord's gEnabledLanguage and gWordID are the word
	recogniser's too.
*/

#include "Words.h"
#include "AirusIterator.h"
#include "Stroke.h"
#include "Unit.h"			// AddRect
#include "Ports.h"			// RoundFixed
#include "Frames.h"
#include "NativeFunctions.h"
#include "Airus.h"
#include "Recognizer.h"
#include "Unicode.h"
#include "RSSymbols.h"
#include "ObjectHeap.h"
#include "ROMConstants.h"
#include "Interpreter.h"	// DoBlock
#include "WRecDomain.h"		// WRecVerifyWordSymbols
#include "Controller.h"		// gController
#include "WordRecognizer.h"	// VerifyWordSymbols
#include "Dictionaries.h"	// LookupWordOrVariant
#include "LowLevel.h"		// FindBaseline: low_level
#include "CursiveReader.h"	// xrdata_type
#include "XrDomains.h"		// rc_type, RCGetH
#include "InkGroups.h"		// GetTraceFromStrokes
#include "StrokeQueue.h"	// gTabScale
#include "ParaGraph.h"		// HWRMemoryFree
#include "FixedMath.h"
#include "Rects.h"			// SetPt
#include "WordEngines.h"	// HostWordEngineDomainInUse

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


// ROM 0x002565e0 StripPunctSymbols__FPUs
// The punctuation taken off the front and off the end.
static void
StripPunctSymbols(UniChar* word)
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


// ROM 0x0008ec00 EncodeAttribute__FPUs
// 0x80 when the word starts with a capital.
static ULong
EncodeAttribute(const UniChar* word)
{
	return UToLower(word[0]) != word[0] ? kCapStartsUpper : 0;
}


// ROM 0x0008eb8c StripRecognitionWord__FPUs
// The word made ready to look up: its diacriticals taken off unless the
// language is the one that keeps them (8) or the word recogniser wrote
// it, then the punctuation taken off the front and off the end.
//
void
StripRecognitionWord(UniChar* word)
{
	if (gEnabledLanguage != 8 && ISNIL(RefVar(FWRecIsBeingUsed(RefVar(NILREF)))))
		NoDiacriticsText(word, 0x7fffffff);
	StripPunctSymbols(word);
}


// ROM 0x0008ebfc StripRecognitionWordDiacritsOK__FPUs
// The punctuation taken off both ends and nothing else: for the
// languages whose words are not the same without their diacriticals.
void
StripRecognitionWordDiacritsOK(UniChar* word)
{
	StripPunctSymbols(word);
}


// ROM 0x0008ec9c EncodeRecognitionWord__FPUs
// The word stripped where it lies; ==> 0x80 when what is left starts
// with a capital.
ULong
EncodeRecognitionWord(UniChar* word)
{
	StripRecognitionWord(word);
	return EncodeAttribute(word);
}


// ROM 0x0008ecbc EncodeRecognitionWordDiacritsOK__FPUs
ULong
EncodeRecognitionWordDiacritsOK(UniChar* word)
{
	StripRecognitionWordDiacritsOK(word);
	return EncodeAttribute(word);
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
	them, and the looking up itself, is Dictionaries.h (InitDictionaries
	and everything under it).
------------------------------------------------------------------------------*/

// ROM 0x0c101848 gEnabledLanguage
long	gEnabledLanguage = 1;


// ROM 0x0c101844 gWordID
// The word recogniser's unit type while one is installed (Recognizer.cpp),
// 0 while none is.
ULong	gWordID = 0;


// ROM 0x0014444c FWRecIsBeingUsed
// WRecIsBeingUsed(): true while the word recogniser is the one reading
// what is written - it puts its own four characters in gWordID.
Ref
FWRecIsBeingUsed(RefArg /*rcvr*/)
{
	return MAKEBOOLEAN(gWordID == 'WREC');
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
// slot, which holds its address for one that lives in memory, and the
// words themselves for one that came off a store or out of a package - a
// dictionary is then opened over them each time it is asked for.
Handle
GetScriptDictRef(RefArg dictionary)
{
	RefVar dict(GetFrameSlotRef(dictionary, RSSYMdict));
	if (ISNIL(dict))
		ThrowMsg("dict not initialized");
	if (((Ref) dict & 3) == 0)
		return (Handle) RefToAddress(dict);
	return ReadRefDictionary(dict);
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


// What a script asks for when it walks a dictionary: the function to
// call and the array of four its arguments are written into.
struct ScriptWalkContext
{
	RefStruct	fFunction;
	RefStruct	fArgs;
};


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


// ROM 0x0008fcb4 FAirusDeleteWord
// PrivateDeleteWord(word) on a dictionary frame.  ==> airusResult.
Ref
FAirusDeleteWord(RefArg rcvr, RefArg word)
{
	Handle dictionary = GetScriptDictRef(rcvr);
	UByte text[64];
	ConvertFromUnicode(GetCString(word), text, kMacRomanEncoding, 0x3f);
	DeleteWord(dictionary, text);
	return MAKEINT(airusResult);
}


// ROM 0x0008fd08 FAirusDeletePrefix
// DeletePrefix(word): the word and everything that goes on from it.
Ref
FAirusDeletePrefix(RefArg rcvr, RefArg word)
{
	Handle dictionary = GetScriptDictRef(rcvr);
	UByte text[64];
	ConvertFromUnicode(GetCString(word), text, kMacRomanEncoding, 0x3f);
	DeletePrefix(dictionary, text);
	return MAKEINT(airusResult);
}


// ROM 0x0008f62c (unnamed)
// The cursor behind a protoDictionaryCursor frame: its `cursor` slot,
// which holds the TAirusIterator by address.
static TAirusIterator*
GetScriptCursorRef(RefArg frame)
{
	RefVar cursor(GetFrameSlotRef(frame, RSSYMcursor));
	if (ISNIL(cursor))
		ThrowMsg("cursor ref missing");
	return (TAirusIterator*) RefToAddress(cursor);
}


// ROM 0x0008f4e8 FAirusIteratorMake
// AllocateCursor() on a dictionary frame: a protoDictionaryCursor frame
// with a cursor of its own, remembered in the dictionary's `cursors`
// array so that everything using the dictionary can be found again.
Ref
FAirusIteratorMake(RefArg rcvr)
{
	RefVar cursor(AllocateFrame());
	SetFrameSlot(cursor, RSSYM_proto,
				 RefVar(GetProtoVariable(rcvr, RSSYMprotodictionarycursor, nil)));
	SetFrameSlot(cursor, RSSYMdict, rcvr);

	RefVar cursors(GetFrameSlotRef(rcvr, RSSYMcursors));
	if (ISNIL(cursors))
	{
		cursors = AllocateArray(RSSYMarray, 0);
		SetFrameSlot(rcvr, RSSYMcursors, cursors);
	}
	AddArraySlot(cursors, cursor);

	TAirusIterator* iterator = new TAirusIterator(GetScriptDictRef(rcvr));
	SetFrameSlot(cursor, RSSYMcursor, RefVar(AddressToRef(iterator)));
	return cursor;
}


// ROM 0x0008f680 FAirusIteratorClone
// PrivateClone() on a cursor frame.
//
// BUG (the ROM's), kept: it makes a copy of the iterator and then never
// uses it - the new frame's `cursor` slot is set from the *original's*
// slot, so the two frames share one iterator and the copy is leaked -
// and it adds the original rather than the copy to the dictionary's
// `cursors` array.  (Which is also why the copy constructor's own
// muddle, which would leave the copy with no state stack, never shows.)
Ref
FAirusIteratorClone(RefArg rcvr)
{
	RefVar copy(Clone(rcvr));
	new TAirusIterator(*GetScriptCursorRef(rcvr));
	SetFrameSlot(copy, RSSYMcursor, RefVar(GetFrameSlotRef(rcvr, RSSYMcursor)));
	RefVar cursors(GetFrameSlotRef(RefVar(GetFrameSlotRef(rcvr, RSSYMdict)), RSSYMcursors));
	AddArraySlot(cursors, rcvr);
	return copy;
}


// ROM 0x0008f788 FAirusIteratorReset
// PrivateReset(word, exact, which) on a cursor frame: the cursor put at
// a word.  `exact` non-nil starts it *at* that word rather than walking
// up to where it would be, and `which` is 'first or 'last - which way
// the first step goes.  ==> whether it is standing on a word.
Ref
FAirusIteratorReset(RefArg rcvr, RefArg word, RefArg exact, RefArg which)
{
	TAirusIterator* iterator = GetScriptCursorRef(rcvr);
	UByte text[64];
	ConvertFromUnicode(GetCString(word), text, kMacRomanEncoding, 0x3f);
	Boolean atPrefix = NOTNIL(exact);
	Boolean backwards = false;
	if (!EQRef(which, RSSYMfirst) && EQRef(which, RSSYMlast))
		backwards = true;
	return iterator->Reset(text, atPrefix, backwards) ? TRUEREF : NILREF;
}


// ROM 0x0008f888 FAirusIteratorThisWord
// PrivateEntry(frame) on a cursor frame: the frame handed in given the
// `word` the cursor stands on and, when it is asked to, the `attribute`
// stored with it and the `terminalClass` that could follow it.  ==> the
// word, or nil when the cursor stands on nothing.
Ref
FAirusIteratorThisWord(RefArg rcvr, RefArg result)
{
	TAirusIterator* iterator = GetScriptCursorRef(rcvr);
	UByte text[64];
	ULong attribute = 0;
	UByte terminal = 0;
	if (!iterator->ThisWord(text, attribute, terminal))
		return NILREF;

	UniChar unicode[64];
	ConvertToUnicode(text, unicode, kMacRomanEncoding, 0x7fffffff);
	RefVar word(MakeString(unicode));
	if (NOTNIL(result))
	{
		SetFrameSlot(result, RSSYMword, word);
		SetFrameSlot(result, RSSYMattribute, RefVar(MAKEINT((long) attribute)));
		SetFrameSlot(result, RSSYMterminalclass, RefVar(MAKEINT(terminal)));
	}
	return word;
}


// ROM 0x0008f998 FAirusIteratorNextWord
Ref
FAirusIteratorNextWord(RefArg rcvr)
{
	return GetScriptCursorRef(rcvr)->NextWord() ? TRUEREF : NILREF;
}


// ROM 0x0008f9bc FAirusIteratorPreviousWord
Ref
FAirusIteratorPreviousWord(RefArg rcvr)
{
	return GetScriptCursorRef(rcvr)->PreviousWord() ? TRUEREF : NILREF;
}


// ROM 0x0008f9e0 FAirusIteratorDispose
// PrivateDispose() on a cursor frame: the iterator given back and the
// slot emptied.  ==> nil, always.  (The frame stays in the dictionary's
// `cursors` array.)
Ref
FAirusIteratorDispose(RefArg rcvr)
{
	TAirusIterator* iterator = GetScriptCursorRef(rcvr);
	if (iterator == nil)
		return NILREF;
	SetFrameSlot(rcvr, RSSYMcursor, RefVar(NILREF));
	delete iterator;
	return NILREF;
}


// ROM 0x0008eb18 FAirusChangeAttribute
// ChangeAttribute(word, attribute) on a dictionary frame: the attribute
// of a word already there written over where it lies.  ==> airusResult:
// 0 it was changed, -7 the dictionary carries no attributes, -6 the word
// is not in it.
Ref
FAirusChangeAttribute(RefArg rcvr, RefArg word, RefArg attribute)
{
	Handle dictionary = GetScriptDictRef(rcvr);
	UByte text[64];
	ConvertFromUnicode(GetCString(word), text, kMacRomanEncoding, 0x3f);
	ChangeAttribute(dictionary, text, (ULong) RINT(attribute));
	return MAKEINT(airusResult);
}


// ROM 0x0008fae0 FAirusDictionaryType
// type() on a dictionary frame: the second byte of the dictionary, the
// kind and the "lock the Handle" bit together (the attribute size, which
// is the rest of that byte, is AttributeSize's business).
Ref
FAirusDictionaryType(RefArg rcvr)
{
	Handle dictionary = GetScriptDictRef(rcvr);
	AirusAParmBlock* parms = (AirusAParmBlock*) *dictionary;
	return MAKEINT((UByte) (*parms->fDataHandle)[1] & 0x0f);
}


// ROM 0x0008fb0c FAirusAttributeSize
// AttributeSize() on a dictionary frame.
Ref
FAirusAttributeSize(RefArg rcvr)
{
	return MAKEINT(AttributeLength(GetScriptDictRef(rcvr)));
}


// ROM 0x0008f84c FAirusDispose
// Dispose() on a dictionary frame: the dictionary given back and the
// frame's `dict` slot taken away, so the frame no longer has one.  ==>
// true, always.
Ref
FAirusDispose(RefArg rcvr)
{
	Handle dictionary = GetScriptDictRef(rcvr);
	DisposDictionary(&dictionary);
	RemoveSlot(rcvr, RSSYMdict);
	return TRUEREF;
}


// ROM 0x0008f2e4 (unnamed) - ScriptWalkProc
// What a script's walk function is called through: the four things the
// walk knows are written into one array, which is passed to the function
// each time rather than a fresh one being made - so a script that wants
// to keep a word has to copy it.  ==> whether to go on, which is
// whatever the function answered.
static Boolean
ScriptWalkProc(UByte* word, ULong attribute, UByte terminal, long count, void* context)
{
	ScriptWalkContext* walk = (ScriptWalkContext*) context;
	UniChar text[64];
	ConvertToUnicode(word, text, kMacRomanEncoding, 0x7fffffff);
	RefVar args(walk->fArgs);
	SetArraySlot(args, 0, RefVar(MakeString(text)));
	SetArraySlot(args, 1, RefVar(MAKEINT((long) attribute)));
	SetArraySlot(args, 2, RefVar(MAKEINT(terminal)));
	SetArraySlot(args, 3, RefVar(MAKEINT(count)));
	return NOTNIL(RefVar(DoBlock(RefVar(walk->fFunction), args)));
}


// ROM 0x0008f44c FAirusWalkDictionary
// Walk(prefix, fn) on a dictionary frame: every word of it that begins
// with the prefix handed to the function, which is given the word, its
// attribute, the one character that could follow it and how many words
// have come so far, and answers whether to go on.  A nil function walks
// it all the same and only counts.  ==> how many words were reached.
Ref
FAirusWalkDictionary(RefArg rcvr, RefArg prefix, RefArg fn)
{
	Handle dictionary = GetScriptDictRef(rcvr);
	UByte text[64];
	ConvertFromUnicode(GetCString(prefix), text, kMacRomanEncoding, 0x3f);
	ScriptWalkContext walk;
	walk.fFunction = fn;
	walk.fArgs = MakeArray(4);
	Boolean counting = ISNIL(fn);
	long count = WalkDictionary(dictionary, text,
								counting ? (DictWalkProc) nil : ScriptWalkProc,
								counting ? nil : &walk);
	return MAKEINT(count);
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
	// the word and its variants looked up in the dictionaries: which
	// variant was found (-1 for none), and the attribute it was found with
	// joins the variant's own capitalisation
	ULong variantCaps = caps;
	UniChar variant[64];
	long found = LookupWordOrVariant(text, &variantCaps, variant);
	variantCaps = CheckCapAttributes(variant) | variantCaps;
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
	if (!WRecVerifyWordSymbols(text))
		flags |= kWordHasBadSymbols;
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
	RegisterNativeFunction("FAirusDeleteWord", (void*) FAirusDeleteWord, 1);
	RegisterNativeFunction("FAirusDeletePrefix", (void*) FAirusDeletePrefix, 1);
	RegisterNativeFunction("FAirusWalkDictionary", (void*) FAirusWalkDictionary, 2);
	RegisterNativeFunction("FAirusChangeAttribute", (void*) FAirusChangeAttribute, 2);
	RegisterNativeFunction("FAirusDictionaryType", (void*) FAirusDictionaryType, 0);
	RegisterNativeFunction("FAirusAttributeSize", (void*) FAirusAttributeSize, 0);
	RegisterNativeFunction("FAirusDispose", (void*) FAirusDispose, 0);
	RegisterNativeFunction("FAirusIteratorMake", (void*) FAirusIteratorMake, 0);
	RegisterNativeFunction("FAirusIteratorClone", (void*) FAirusIteratorClone, 0);
	RegisterNativeFunction("FAirusIteratorReset", (void*) FAirusIteratorReset, 3);
	RegisterNativeFunction("FAirusIteratorThisWord", (void*) FAirusIteratorThisWord, 1);
	RegisterNativeFunction("FAirusIteratorNextWord", (void*) FAirusIteratorNextWord, 0);
	RegisterNativeFunction("FAirusIteratorPreviousWord", (void*) FAirusIteratorPreviousWord, 0);
	RegisterNativeFunction("FAirusIteratorDispose", (void*) FAirusIteratorDispose, 0);
	RegisterNativeFunction("FWRecIsBeingUsed", (void*) FWRecIsBeingUsed, 0);
	RegisterNativeFunction("FUseWRec", (void*) FUseWRec, 1);
	RegisterNativeFunction("FStripRecognitionWord", (void*) FStripRecognitionWord, 1);
	RegisterNativeFunction("FStripRecognitionWordDiacritsOK", (void*) FStripRecognitionWordDiacritsOK, 1);
}


/*------------------------------------------------------------------------------
	W h e r e   a   w o r d   s i t s
------------------------------------------------------------------------------*/

// ROM 0x00065b2c FindBaseline__FPP7TStrokeP5Point
// The four corners a word of strokes would be laid out in: the top-left,
// top-right, bottom-left and bottom-right, the bottom being the baseline.
//
// The recogniser is asked first: the strokes are turned into the trace
// ParaGraph's low level works on (GetTraceFromStrokes) and `low_level`
// is run over it in its base-line-only mode (rc +0x90 = 0x72, +0x92 = 1),
// which says where the line is.  The two heights it leaves at rc +0xec
// (the base line) and +0xea (from it to the top, negative) are in the
// tablet's units, so they are brought to pixels by gTabScale.y.  When
// that cannot be done - no trace, or low_level fails - the answer is 1
// rather than 0, and so is the box's: the strokes' own box is used
// instead, the word sitting entirely above its baseline.
//
// ROM QUIRK kept: the box is used whenever the base line low_level gave
// comes to nought, not only when it failed (the test is on the value).  Both
// heights are read through unaligned loads, which take the halfword
// *before* the one named - rc +0xec and +0xea, not +0xee and +0xec - and
// the trace's point count goes in rc +0x96 the same way (the fourth of
// GetTraceFromStrokes' answers).
//
// A box with no width, or none with no height, is given one, because
// nothing downstream divides by nought happily.
long
FindBaseline(TStroke** strokes, Point* out)
{
	long failed = 0;
	short base = 0;				// r6: the base line (the box's bottom when nought)
	short top = 0;				// r8
	short baseRight = 0;		// r9
	short topRight = 0;			// r10
	PS_point_type* trace = nil;
	short nStrokes = 0;
	short nPoints = 0;
	GetTraceFromStrokes(strokes, &trace, &nStrokes, &nPoints);
	long done = 1;
	if (trace != nil)
	{
		rc_type rc;
		memset(&rc, 0, sizeof(rc));
		RCSetH(&rc, 0x96, (UShort) nPoints);
		rc.fTrace = trace;
		RCSetH(&rc, 0x90, 0x72);
		RCSetH(&rc, 0x92, 1);
		xrdata_type xr;
		memset(&xr, 0, sizeof(xr));
		done = low_level(trace, &xr, &rc);
		if (done == 0)
		{
			Fixed scale = gTabScale.y;
			long lineY = (short) RCGetH(&rc, 0xec);
			long toTop = (short) RCGetH(&rc, 0xea);
			base = (short) ((FixedDivide((Fixed) ((uint32_t) lineY << 16), scale) + 0x8000) >> 16);
			top = (short) ((FixedDivide((Fixed) ((uint32_t) (lineY + toTop) << 16), scale) + 0x8000) >> 16);
			baseRight = base;
			topRight = top;
		}
	}
	if (done != 0)
		failed = 1;
	if (trace != nil)
		HWRMemoryFree((Ptr) trace);

	FRect box;
	SetRectangleEmpty(&box);		// DEVIATION: with no strokes the ROM reads what is on its stack
	for (long i = 0; strokes[i] != nil; i++)
		AddRect(&strokes[i]->fBBox, &box, i == 0);
	short left = (short) ((box.left + 0x8000) >> 16);
	short right = (short) ((box.right + 0x8000) >> 16);
	if (left == right)
		right = (short) (right + 1);
	if (base == 0)
	{
		top = (short) ((box.top + 0x8000) >> 16);
		base = (short) ((box.bottom + 0x8000) >> 16);
		if (top == base)
			base = (short) (base + 1);
		topRight = top;
		baseRight = base;
	}
	SetPt(&out[0], left, top);
	SetPt(&out[1], right, topRight);
	SetPt(&out[2], left, base);
	SetPt(&out[3], right, baseRight);
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


// ROM 0x00144470 (unnamed) - WRecDomainInUse
// The 'WREC' recogniser's domain while that recogniser is the one reading
// (gWordID), nil otherwise - Rosetta's, when the letter set is printed.
//
// DEVIATION (host): or the domain of the host's own engine in use
// (WordEngines.h), which is a word domain of the same kind and is asked
// the same questions.
static TDomain*
WRecDomainInUse(void)
{
	if (gRecognition.fRecognizers == nil)		// DEVIATION: a host test with no recognition system
		return nil;
	TDomain* hostEngine = HostWordEngineDomainInUse();
	if (hostEngine != nil)
		return hostEngine;
	TRecognizer* recognizer = gRecognition.fRecognizers->FindRecognizer('WREC');
	if (recognizer != nil && gWordID == 'WREC')
		return recognizer->Domain();
	return nil;
}


// ROM 0x001444c8 WRecVerifyWordSymbols__FPUs
// Whether the word recogniser in use can write every symbol of the word:
// Rosetta's domain is asked when it is the one reading, ParaGraph's
// (the free VerifyWordSymbols, over the 'XRWR' domain) otherwise.
//
// DEVIATION: the ROM always has a recognition system to ask; a host test
// that validates words without one is answered "nothing to object to".
Boolean
WRecVerifyWordSymbols(UniChar* word)
{
	TDomain* domain = WRecDomainInUse();
	if (domain == nil)
	{
		if (gController == nil)
			return true;
		return VerifyWordSymbols(word);
	}
	return ((TWRecDomain*) domain)->VerifyWordSymbols(word);
}
// ROM 0x0c101864 gSaveWordTrainingData
// Set out of the "learning enabled" preference (Recognizer.cpp).
Boolean	gSaveWordTrainingData = false;
