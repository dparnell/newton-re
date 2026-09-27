/*
	File:		assist/Lexicon.cpp

	Contains:	The Assistant's lexicon.  See Lexicon.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Lexicon.h"
#include "Assistant.h"
#include "AssistStrings.h"
#include "Heuristics.h"
#include "Airus.h"
#include "ROMDictionaryData.h"
#include "Dictionaries.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "NativeFunctions.h"
#include "NewtonMemory.h"
#include "RSSymbols.h"

#include <string.h>

Handle	gTrie = nil;						// ROM 0x0c100b68 gTrie
Handle	gDynaTrie = nil;					// ROM 0x0c100b6c gDynaTrie
Ref		gDateFrame = NILREF;				// ROM 0x0c100b70 gDateFrame
Ref		gTimeFrame = NILREF;				// ROM 0x0c100b74 gTimeFrame
Ref		gPhoneFrame = NILREF;				// ROM 0x0c100b78 gPhoneFrame
Ref		gNumberFrame = NILREF;				// ROM 0x0c100b7c gNumberFrame
Ref		gDictionaryFrame = NILREF;			// ROM 0x0c100b84 gDictionaryFrame
Ref		gDynaDictionaryFrame = NILREF;		// ROM 0x0c100b88 gDynaDictionaryFrame
Ref		gLastLookupString = NILREF;			// ROM 0x0c100b94 gLastLookupString

// the array of the ROM lexicon's entries, gTrie's attributes indexing it
const Ref	kLexiconEntries = MAKEMAGICPTR(248);
// the words (month and day names, "today" and so on) a date can be, which
// MatchString marks as seen in the info frame's `exception` array
const Ref	kDateWords = MAKEMAGICPTR(113);

// frames/StringNatives.cpp
Ref		SplitString(RefArg rcvr, RefArg str);						// ROM 0x000833e4 SplitString__FRC6RefVarT1
Ref		FFindStringInArray(RefArg rcvr, RefArg array, RefArg str);	// ROM 0x001fe3c8 FFindStringInArray__FRC6RefVarN21


// ROM 0x0007ce20 TrieAdd__FPcPP15AirusAParmBlockRC6RefVar
// A word (lowercased in place) registered as standing for the frame.  A
// word the trie knows already has its count raised and the frame added
// to its list, when it is not there yet; a new one gets an entry of its
// own - or, with no frame, the attribute 2 (the nil ref) and no entry at
// all.
void
TrieAdd(char* word, Handle dictionary, RefArg frame)
{
	RefVar entry;
	RefVar count;
	RefVar frames;
	unsigned char* lower = DownCase((unsigned char*) word);
	ULong attributeBuf;
	ULong* attribute = &attributeBuf;
	VerifyString(dictionary, lower, nil, &attribute, nil);
	if (airusResult == kAirusIsPrefixAndWord || airusResult == kAirusIsWord)
	{
		entry = GetArraySlotRef(gDynaDictionaryFrame, *attribute);
		count = GetArraySlotRef(entry, 0);
		frames = GetArraySlotRef(entry, 1);
		count = MAKEINT(RINT(count) + 1);
		SetArraySlotRef(entry, 0, count);
		if (NOTNIL(frame) && ISNIL(member_p(frames, frame)))
			Append(RefVar(), frames, frame);
		return;
	}
	if (ISNIL(frame))
		attributeBuf = NILREF;
	else
	{
		entry = AllocateArray(RSSYMarray, 2);
		SetArraySlotRef(entry, 0, MAKEINT(1));
		frames = AllocateArray(RSSYMarray, 1);
		SetArraySlotRef(frames, 0, frame);
		SetArraySlotRef(entry, 1, frames);
		attributeBuf = (ULong) DictAppendItem(RefVar(gDynaDictionaryFrame), entry);
	}
	if (*lower != 0)
		AddWord(dictionary, 0, lower, attributeBuf);
}


// ROM 0x0007d0d4 LexDateLookup__FRC6RefVarT1
Ref
LexDateLookup(RefArg /*rcvr*/, RefArg str)
{
	char* word = NewASCIIString(str);
	Ref result = LexLookup(gDateLexDictionary, word);
	DisposPtr(word);
	return result;
}


// ROM 0x0007d124 LexTimeLookup__FRC6RefVarT1
Ref
LexTimeLookup(RefArg /*rcvr*/, RefArg str)
{
	char* word = NewASCIIString(str);
	Ref result = LexLookup(gTimeLexDictionary, word);
	DisposPtr(word);
	return result;
}


// ROM 0x0007d174 LexPhoneLookup__FRC6RefVarT1
Ref
LexPhoneLookup(RefArg /*rcvr*/, RefArg str)
{
	char* word = NewASCIIString(str);
	Ref result = LexLookup(gPhoneLexDictionary, word);
	DisposPtr(word);
	return result;
}


// (the four below are LexLookup written out again)
static Ref
LexicalLookup(Handle dictionary, char* word)
{
	ULong attributeBuf;
	ULong* attribute = &attributeBuf;
	VerifyString(dictionary, word, nil, &attribute, nil);
	return (airusResult == kAirusIsPrefixAndWord || airusResult == kAirusIsWord) ? TRUEREF : NILREF;
}


// ROM 0x0007d1c4 cLexDateLookup__FRC6RefVarPc
Ref
cLexDateLookup(RefArg /*rcvr*/, char* word)
{
	return LexicalLookup(gDateLexDictionary, word);
}


// ROM 0x0007d1d4 cLexTimeLookup__FRC6RefVarPc
Ref
cLexTimeLookup(RefArg /*rcvr*/, char* word)
{
	return LexicalLookup(gTimeLexDictionary, word);
}


// ROM 0x0007d1e4 cLexPhoneLookup__FRC6RefVarPc
Ref
cLexPhoneLookup(RefArg /*rcvr*/, char* word)
{
	return LexicalLookup(gPhoneLexDictionary, word);
}


// ROM 0x0007d1f4 cLexNumberLookup__FRC6RefVarPc
Ref
cLexNumberLookup(RefArg /*rcvr*/, char* word)
{
	return LexicalLookup(gNumberLexDictionary, word);
}


// ROM 0x0007d204 InitDSDictionary__FRC6RefVarT1
// The two tries, the classes the lexical dictionaries' words are given,
// and the arrays of entries.
Ref
InitDSDictionary(RefArg /*rcvr*/, RefArg /*arg*/)
{
	gDynaTrie = TrieInit();
	gTrie = GetROMDictionary(2);
	gDateFrame = GetFrameSlotRef(kAssistantFrame, RSSYMdate);
	AddGCRoot(gDateFrame);
	gTimeFrame = GetFrameSlotRef(kAssistantFrame, RSSYMtime);
	AddGCRoot(gTimeFrame);
	gPhoneFrame = GetFrameSlotRef(kAssistantFrame, RSSYMparsed_phone);
	AddGCRoot(gPhoneFrame);
	gNumberFrame = GetFrameSlotRef(kAssistantFrame, RSSYMparsed_number);
	AddGCRoot(gNumberFrame);
	gDictionaryFrame = kLexiconEntries;
	gDynaDictionaryFrame = AllocateArray(RSSYMarray, 0);
	AddGCRoot(gDynaDictionaryFrame);
	gLastLookupString = NILREF;
	AddGCRoot(gLastLookupString);
	return TRUEREF;
}


// ROM 0x0007d378 GetDictItem__FRC6RefVarT1
Ref
GetDictItem(RefArg /*rcvr*/, RefArg index)
{
	return GetArraySlotRef(gDictionaryFrame, RINT(index));
}


// ROM 0x0007d3d4 DictAppendItem__FRC6RefVarT1
long
DictAppendItem(RefArg array, RefArg item)
{
	long length = Length(array);
	if (length == 0)
	{
		SetLength(array, 1);
		length = 0;
	}
	else
		SetLength(array, length + 1);
	SetArraySlotRef(array, length, item);
	return Length(array) - 1;
}


// ROM 0x0007d484 DumpDict__FRC6RefVar
Ref
DumpDict(RefArg /*rcvr*/)
{
	return gDynaDictionaryFrame;
}


// ROM 0x0007d494 DSAddLexiconFrame__FRC6RefVarT1
// AddLexFrame(frame): a frame with a `Lexicon` registered; nil when it has
// none.
Ref
DSAddLexiconFrame(RefArg /*rcvr*/, RefArg frame)
{
	if (!FrameHasSlot(frame, RSSYMlexicon))
		return NILREF;
	RefVar frames(AllocateArray(RSSYMarray, 1));
	SetArraySlotRef(frames, 0, frame);
	LoadLexiconFrames(frames);
	return TRUEREF;
}


// ROM 0x0007d52c LoadLexiconFrames__FRC6RefVar
void
LoadLexiconFrames(RefArg frames)
{
	ULong count = (ULong) Length(frames);
	for (ULong i = 0; i < count; i++)
		MakePhrasalLexEntry(RefVar(), RefVar(GetArraySlotRef(frames, i)));
}


// ROM 0x0007d7d8 DynaTrieLookup__FPc
Ref
DynaTrieLookup(char* word)
{
	ULong attributeBuf;
	ULong* attribute = &attributeBuf;
	VerifyString(gDynaTrie, word, nil, &attribute, nil);
	if (airusResult == kAirusIsPrefixAndWord || airusResult == kAirusIsWord)
	{
		RefVar entry(GetArraySlotRef(gDynaDictionaryFrame, *attribute));
		return GetArraySlotRef(entry, 1);
	}
	return NILREF;
}


// ROM 0x0007d880 DynaCompress__FUl
// The entry at the index taken out of gDynaDictionaryFrame, the ones
// above it moved down, and every word of the trie whose attribute was
// above it given one less.
Ref
DynaCompress(ULong index)
{
	long length = Length(gDynaDictionaryFrame);
	for (ULong slot = index; (long) slot < length - 1; slot++)
		SetArraySlotRef(gDynaDictionaryFrame, slot, GetArraySlotRef(gDynaDictionaryFrame, slot + 1));
	SetLength(RefVar(gDynaDictionaryFrame), length - 1);
	Handle dictionary = gDynaTrie;
	ULong attributeBuf;
	ULong* attribute = &attributeBuf;
	unsigned char last[256];
	unsigned char word[256];
	unsigned char prefix[256];
	prefix[0] = 0;
	word[0] = 0;
	last[0] = 0;
	FirstCompletion(dictionary, prefix, word, &attribute, nil);
	if (airusResult == kAirusEmptyDictionary)
		return TRUEREF;
	Bstrcpy(last, word);
	ULong value = *attribute;
	if (value >= index)
		goto change;
	for (;;)
	{
		do
		{
			Bstrcpy(last, word);
			prefix[0] = 0;
			NextCompletion(dictionary, prefix, word, last, &attribute, nil);
			if (airusResult == kAirusNoMoreWords || airusResult == kAirusNotAWord)
				return TRUEREF;
		}
		while (*attribute < index);
		value = *attribute;
change:
		ChangeAttribute(dictionary, word, value - 1);
	}
}


// ROM 0x0007dca0 TagPhraseFrame__FRC6RefVarN21
// Each meaning (a symbol names its frame) copied with the phrase as its
// `value`.
Ref
TagPhraseFrame(RefArg /*rcvr*/, RefArg meanings, RefArg phrase)
{
	RefVar meaning;
	RefVar tagged(AllocateArray(RSSYMarray, 0));
	ULong count = (ULong) Length(meanings);
	for (ULong i = 0; i < count; i++)
	{
		meaning = GetArraySlotRef(meanings, i);
		if (IsSymbol(meaning))
			meaning = MapSymToFrame(RefVar(), meaning);
		meaning = Clone(meaning);
		SetFrameSlot(meaning, RSSYMvalue, phrase);
		tagged = Append(RefVar(), tagged, meaning);
	}
	return tagged;
}


// A copy of the Assistant's `lexical` frame as a thing of the class.
static Ref
MakeLexical(RefArg isa)
{
	RefVar lexical(Clone(RefVar(GetFrameSlotRef(kAssistantFrame, RSSYMlexical))));
	SetFrameSlot(lexical, RSSYMisa, isa);
	return lexical;
}


// ROM 0x0007ddd8 MatchString__FPP15AirusAParmBlockPcRC6RefVar
// What a phrase (lowercased in place) means: the meanings the two tries
// give it, tagged with the phrase; else a date, a time, a phone number or
// a number, as a `lex` array of one lexical frame - except that a date
// word seen for the first time in the sentence (marked in `info`'s
// `exception` array) goes on to be resolved against the Names file, as
// does anything of up to four words none of which is an action.  ==> nil
// when nothing fits.
Ref
MatchString(Handle dictionary, char* phrase, RefArg info)
{
	char* str = (char*) DownCase((unsigned char*) phrase);
	RefVar dynamic(DynaTrieLookup(str));
	ULong attributeBuf;
	ULong* attribute = &attributeBuf;
	VerifyString(dictionary, str, nil, &attribute, nil);
	RefVar result(dynamic);
	if (airusResult == kAirusIsPrefixAndWord || airusResult == kAirusIsWord)
	{
		RefVar value(MakeString(str));
		RefVar entry(GetArraySlotRef(gDictionaryFrame, *attribute));
		if (NOTNIL(dynamic))
		{
			RefVar meanings(UniqueAppendListGen(RefVar(), dynamic, entry));
			return TagPhraseFrame(RefVar(), meanings, value);
		}
		return TagPhraseFrame(RefVar(), entry, value);
	}
	if (NOTNIL(dynamic))
		return dynamic;

	RefVar lexical;
	RefVar dateLexical;
	RefVar lexArray;
	RefVar dateIndex;
	if (NOTNIL(cLexDateLookup(RefVar(), str)))
	{
		lexical = MakeLexical(RefVar(gDateFrame));
		dateLexical = lexical;
		SetFrameSlot(lexical, RSSYMdate, RefVar(MAKEINT(0)));
	}
	else if (NOTNIL(cLexTimeLookup(RefVar(), str)))
	{
		lexical = MakeLexical(RefVar(gTimeFrame));
		SetFrameSlot(lexical, RSSYMtime, RefVar(MAKEINT(0)));
	}
	else if (NOTNIL(cLexPhoneLookup(RefVar(), str)))
		lexical = MakeLexical(RefVar(gPhoneFrame));
	else if (NOTNIL(cLexNumberLookup(RefVar(), str)))
		lexical = MakeLexical(RefVar(gNumberFrame));
	if (NOTNIL(lexical))
	{
		SetFrameSlot(lexical, RSSYMvalue, RefVar(MakeString(str)));
		lexArray = AllocateArray(RSSYMlex, 1);
		SetArraySlotRef(lexArray, 0, lexical);
	}
	if (NOTNIL(dateLexical))
	{
		dateIndex = FFindStringInArray(RefVar(), RefVar(kDateWords), RefVar(MakeString(str)));
		if (NOTNIL(dateIndex))
		{
			RefVar exceptions(GetFrameSlotRef(info, RSSYMexception));
			Ref seen = GetArraySlotRef(exceptions, RINT(dateIndex));
			if (ISNIL(seen))
				// ROM BUG, kept: the slot written is the one numbered by
				// the nil it found (2), not the word's own
				SetArraySlotRef(exceptions, (ArrayIndex) seen, MAKEINT(0));
			else
				dateIndex = NILREF;
		}
	}
	if (NOTNIL(lexical) && ISNIL(dateIndex))
		return lexArray;
	if (ISNIL(dateIndex))
	{
		RefVar words(SplitString(RefVar(), RefVar(MakeString(str))));
		long count = Length(words);
		if (count > 4)
			return NILREF;
		for (long i = 0; i < count; i++)
		{
			RefVar hits(FastStringLookup(RefVar(), RefVar(GetArraySlotRef(words, i))));
			if (ISNIL(hits))
				continue;
			long n = Length(hits);
			for (long j = 0; j < n; j++)
				if (NOTNIL(ISATest(RefVar(), RefVar(GetArraySlotRef(hits, j)), RSSYMaction)))
					return NILREF;
		}
	}
	return DSResolveString(RefVar(), RefVar(MakeString(str)), info);
}


// ROM 0x0007e8f4 TrieInit__Fv
Handle
TrieInit(void)
{
	return NewDictionary(0xf, 4);
}


// ROM 0x0007e900 DynaTrieDelete__FRC6RefVarT1
// One registration of a word taken away: the count goes down and the
// frame being unregistered (gDynaDeleteSYM or gDynaDeleteFrame, which
// RemovePhrasalLexEntry sets) comes off its list, and the word leaves the
// trie - its entry with it - when that was the last.  ==> nil when the
// word was not registered.
Ref
DynaTrieDelete(RefArg /*rcvr*/, RefArg word)
{
	unsigned char* ascii = (unsigned char*) NewASCIIString(RefVar(Clone(word)));
	RefVar entry;
	RefVar frames;
	RefVar count;
	ULong attributeBuf;
	ULong* attribute = &attributeBuf;
	unsigned char* lower = DownCase(ascii);
	VerifyString(gDynaTrie, lower, nil, &attribute, nil);
	if (airusResult == kAirusIsPrefixAndWord || airusResult == kAirusIsWord)
	{
		ULong index = *attribute;
		entry = GetArraySlotRef(gDynaDictionaryFrame, index);
		count = GetArraySlotRef(entry, 0);
		frames = GetArraySlotRef(entry, 1);
		count = MAKEINT(RINT(count) - 1);
		SetArraySlotRef(entry, 0, count);
		if (RINT(count) != 0)
		{
			if (ISNIL(gDynaDeleteSym))
				FSetRemove(RefVar(), frames, RefVar(gDynaDeleteFrame));
			else
				FSetRemove(RefVar(), frames, RefVar(gDynaDeleteSym));
			DisposPtr((Ptr) lower);
			return TRUEREF;
		}
		DeleteWord(gDynaTrie, lower);
		DynaCompress(*attribute);
		DisposPtr((Ptr) lower);
		return TRUEREF;
	}
	DisposPtr((Ptr) lower);
	return NILREF;
}


// ROM 0x0007eb64 LexLookup__FPP15AirusAParmBlockPc
Ref
LexLookup(Handle dictionary, char* word)
{
	return LexicalLookup(dictionary, word);
}


// ROM 0x000831d8 FastStringLookup__FRC6RefVarT1
// FastStrLookup(word): what the word (lowercased) means to either trie,
// the run-time registrations first; nil when neither knows it.
Ref
FastStringLookup(RefArg /*rcvr*/, RefArg word)
{
	char* str = (char*) DownCase((unsigned char*) NewASCIIString(RefVar(Clone(word))));
	RefVar dynamic(DynaTrieLookup(str));
	ULong attributeBuf;
	ULong* attribute = &attributeBuf;
	VerifyString(gTrie, str, nil, &attribute, nil);
	if (airusResult == kAirusIsPrefixAndWord || airusResult == kAirusIsWord)
	{
		DisposPtr(str);
		RefVar entry(GetArraySlotRef(gDictionaryFrame, *attribute));
		if (ISNIL(dynamic))
			return entry;
		return UniqueAppendListGen(RefVar(), dynamic, entry);
	}
	DisposPtr(str);
	return dynamic;
}


void
RegisterLexiconNatives(void)
{
	RegisterNativeFunction("LexDateLookup__FRC6RefVarT1", (void*) LexDateLookup, 1);
	RegisterNativeFunction("LexTimeLookup__FRC6RefVarT1", (void*) LexTimeLookup, 1);
	RegisterNativeFunction("LexPhoneLookup__FRC6RefVarT1", (void*) LexPhoneLookup, 1);
	RegisterNativeFunction("GetDictItem__FRC6RefVarT1", (void*) GetDictItem, 1);
	RegisterNativeFunction("DumpDict__FRC6RefVar", (void*) DumpDict, 0);
	RegisterNativeFunction("DSAddLexiconFrame__FRC6RefVarT1", (void*) DSAddLexiconFrame, 1);
	RegisterNativeFunction("FastStringLookup__FRC6RefVarT1", (void*) FastStringLookup, 1);
}
