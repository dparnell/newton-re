/*
	File:		recognition/Spelling.cpp

	Contains:	The spelling checker - Spelling.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Spelling.h"
#include "Airus.h"
#include "Words.h"
#include "Learning.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"		// NSCallGlobalFn
#include "NativeFunctions.h"
#include "RSSymbols.h"
#include "ROMConstants.h"

#include <string.h>
#include <ctype.h>


// ROM 0x0c101b20 gSpeller
spell_state*	gSpeller = nil;


/*------------------------------------------------------------------------------
	T h e   c h a i n s   a   s e s s i o n   c h e c k s   a g a i n s t
------------------------------------------------------------------------------*/

// The two are the same walk over the machine's list of dictionaries with
// different masks, so they are written out once here; the ROM has them
// as two functions side by side.
static void
InitChainsForMask(TDictChain** chains, ULong wanted, ULong refused)
{
	for (long i = 0; i < kDictChainCount; i++)
		chains[i] = nil;
	RefVar list(Dictionaries());
	ULong count = gDictList != nil ? (ULong) gDictList->fCount : 0;
	for (ULong i = 0; i < count; i++)
	{
		dictListEntry* entry = (dictListEntry*) gDictList->GetEntry(i);
		if (entry == nil || entry->fDictionary == nil || entry->fStatus == 0
			|| entry->fDisabled != 0)
			continue;
		RefVar frame(GetArraySlotRef(list, entry->fIndex));
		ULong domain = (ULong) RINT(RefVar(GetProtoVariable(frame, RSSYMdomaintype, nil)));
		if ((domain & wanted) != 0 && (domain & refused) == 0)
			AddToChain(chains, entry);
	}
}


// ROM 0x001f1b74 InitSpellChains__FPP10TDictChain
// The dictionaries a word is checked against: the ones that hold words
// (0x1000), less the ones whose bottom `domainType` bit marks them as
// not to be offered.
void
InitSpellChains(TDictChain** chains)
{
	InitChainsForMask(chains, 0x1000, 1);
}


// ROM 0x001f40a4 InitNumberChains__FPP10TDictChain
// ... and the ones that hold numbers, dates, times and money, which is
// what a word full of digits is checked against instead.
void
InitNumberChains(TDictChain** chains)
{
	InitChainsForMask(chains, 0x1c2000, 0);
}


/*------------------------------------------------------------------------------
	T h e   s e s s i o n
------------------------------------------------------------------------------*/

// ROM 0x001f62ac MakeSpellFrame__FP11spell_state
// The frame a script holds a session by: a clone of the ROM's own
// `spellFrame` with the block's address in its `speller` slot.
Ref
MakeSpellFrame(spell_state* speller)
{
	RefVar frame(Clone(RefVar(Rspellframe)));
	SetFrameSlot(frame, RSSYMspeller, RefVar(AddressToRef(speller)));
	return frame;
}


// ROM 0x001f6314 GetSpeller__FRC6RefVar
spell_state*
GetSpeller(RefArg frame)
{
	return (spell_state*) RefToAddress(GetFrameSlotRef(frame, RSSYMspeller));
}


/*------------------------------------------------------------------------------
	T h e   w o r d   o n   t h e   w a y   i n   a n d   o u t
------------------------------------------------------------------------------*/

// ROM 0x001f5d50 FixQuotes__FPc
// The curly right single quote turned into a plain apostrophe, which is
// what the dictionaries hold.  ==> whether there was one.
Boolean
FixQuotes(char* word)
{
	Boolean found = false;
	long length = (long) strlen(word);
	for (long i = 0; i < length; i++)
		if ((UByte) word[i] == 0xd5)		// Mac Roman's right single quote
		{
			word[i] = '\'';
			found = true;
		}
	return found;
}


// ROM 0x001f5da0 RestoreQuotes__FPcUc
// ... and put back, so a guess is written the way the writer writes it.
void
RestoreQuotes(char* word, Boolean quoted)
{
	if (!quoted)
		return;
	long length = (long) strlen(word);
	for (long i = 0; i < length; i++)
		if (word[i] == '\'')
			word[i] = (char) 0xd5;
}


// ROM 0x001f4b98 strpos__FPcc
// The first of that character in the string; nil for none.
char*
strpos(char* str, char c)
{
	for (; *str != 0; str++)
		if (*str == c)
			return str;
	return nil;
}


// ROM 0x001f5d10 (unnamed) - AllCapitals
// Whether every character of the word is a capital Roman letter.  An
// empty word is.  (The corrector has its own copy of this test; the ROM
// writes it out twice.)
Boolean
SpellAllCapitals(const UniChar* word)
{
	for (long i = 0; word[i] != 0; i++)
		if (word[i] < 'A' || word[i] > 'Z')
			return false;
	return true;
}


/*------------------------------------------------------------------------------
	B e g i n n i n g   a n d   e n d i n g   a   s e s s i o n
------------------------------------------------------------------------------*/

// ROM 0x001f6360 FSpellDocBegin
// SpellDocBegin(): a session started.  The chains are built from the
// machine's dictionaries as they stand, and an empty dictionary is made
// for the words this session is told to skip - it is given the user
// dictionary's id, so that a word skipped here looks to the rest of the
// checker like a word the writer has added.
//
// ==> the frame a script holds it by, nil when there was no room.
Ref
FSpellDocBegin(RefArg /*rcvr*/)
{
	RefVar frame;
	gSpeller = new spell_state;
	if (gSpeller != nil)
	{
		memset(gSpeller, 0, sizeof(spell_state));
		InitSpellChains(gSpeller->fChains);
		InitNumberChains(gSpeller->fNumberChains);
		gSpeller->fChain = gSpeller->fChains[0];
		gSpeller->fIgnore = NewDictionary(kAirusKindEnumRAM | kAirusLockedBit, 1);
		if (gSpeller->fIgnore != nil)
			((AirusAParmBlock*) *gSpeller->fIgnore)->fDictID = kUserDictionary;
		frame = MakeSpellFrame(gSpeller);
	}
	return frame;
}


// ROM 0x001f63f8 FSpellDocEnd
// SpellDocEnd(frame): the session given back - the chains, the skip
// dictionary and the block itself - and the user dictionary written out
// when anything was learnt during it.
Ref
FSpellDocEnd(RefArg /*rcvr*/, RefArg frame)
{
	if (ISNIL(frame))
		return NILREF;
	gSpeller = GetSpeller(frame);
	if (gSpeller == nil)
		return NILREF;
	DoneChains(gSpeller->fChains);
	DoneChains(gSpeller->fNumberChains);
	if (gSpeller->fIgnore != nil)
		DisposDictionary(&gSpeller->fIgnore);
	if (NOTNIL(RefVar(GetFrameSlotRef(frame, RSSYMsaveuserdict))))
	{
		NSCallGlobalFn(RSSYMsaveuserdictionary);
		SetFrameSlot(frame, RSSYMsaveuserdict, RefVar(NILREF));
	}
	delete gSpeller;
	gSpeller = nil;
	SetFrameSlot(frame, RSSYMspeller, RefVar(NILREF));
	return NILREF;
}


/*------------------------------------------------------------------------------
	I s   t h i s   a   w o r d ?
------------------------------------------------------------------------------*/

// ROM 0x001f4dc8 ValidateWord__FPP15AirusAParmBlockPcPUl
// One dictionary asked about one word.  ==> the dictionary's id when it
// has it - as a word, whether or not other words go on from it - and -1
// when it does not; `attribute` comes back with whatever was stored
// beside it, which for a word dictionary is how it is capitalised.
long
ValidateWord(Handle dictionary, char* word, ULong* attribute)
{
	long found = -1;
	ULong* stored = nil;
	*attribute = 0;
	VerifyString(dictionary, word, nil, &stored, nil);
	if (airusResult == kAirusIsPrefixAndWord || airusResult == kAirusIsWord)
	{
		found = ((AirusAParmBlock*) *dictionary)->fDictID;
		if (stored != nil)
			*attribute = *stored;
	}
	return found;
}


// ROM 0x001f4e48 ValidateWord2__FPP15AirusAParmBlockPcPUl
// The same, and then again with the first letter's case turned over - so
// that a word the dictionary holds in lower case is still found when it
// was written with a capital, and the other way round.  The attribute
// answered is not the dictionary's: it is 0x80 when the spelling that
// was found begins with a capital, which is what the caller wants to
// know.  The word is put back as it was found.
long
ValidateWord2(Handle dictionary, char* word, ULong* attribute)
{
	long found = -1;
	// (the ROM reads the C library's character table directly: bit 0x10
	//  is "upper case" and bit 8 "lower case")
	char was = word[0];
	Boolean upper = isupper((UByte) was) != 0;
	Boolean lower = islower((UByte) was) != 0;
	*attribute = 0;
	VerifyString(dictionary, word, nil, nil, nil);
	if (airusResult == kAirusIsPrefixAndWord || airusResult == kAirusIsWord)
	{
		*attribute = upper ? 0x80 : 0;
		return ((AirusAParmBlock*) *dictionary)->fDictID;
	}
	if (upper)
		word[0] = (char) tolower((UByte) was);
	else if (lower)
		word[0] = (char) toupper((UByte) was);
	if (was != word[0])
	{
		*attribute = 0;
		VerifyString(dictionary, word, nil, nil, nil);
		if (airusResult == kAirusIsPrefixAndWord || airusResult == kAirusIsWord)
		{
			found = ((AirusAParmBlock*) *dictionary)->fDictID;
			*attribute = lower ? 0x80 : 0;
		}
		word[0] = was;
	}
	return found;
}


// ROM 0x001f4bcc ValidateWordInChain__FPcPUlUc
// Every dictionary of the session's chain asked in turn, and the words
// this session was told to skip asked first when `skipped` says so.
long
ValidateWordInChain(char* word, ULong* attribute, Boolean skipped)
{
	long found = -1;
	TDictChain* chain = gSpeller->fChain;
	if (skipped && gSpeller->fIgnore != nil)
	{
		found = ValidateWord(gSpeller->fIgnore, word, attribute);
		if (found != -1)
			return found;
	}
	ULong count = (ULong) chain->fCount;
	for (ULong i = 0; i < count; i++)
	{
		found = ValidateWord(*(Handle*) chain->GetEntry(i), word, attribute);
		if (found != -1)
			return found;
	}
	return -1;
}


// ROM 0x001f4c68 ValidateWordInNumberChain__FPc
// ... and the same over the dictionaries that hold numbers, dates and
// money, which are all of the kind that is only consulted for particular
// fields - so the chain they land in is the exceptions one.
long
ValidateWordInNumberChain(char* word)
{
	TDictChain* chain = gSpeller->fNumberChains[kDictChainException];
	ULong count = (ULong) chain->fCount;
	ULong attribute;
	for (ULong i = 0; i < count; i++)
	{
		long found = ValidateWord(*(Handle*) chain->GetEntry(i), word, &attribute);
		if (found != -1)
			return found;
	}
	return -1;
}


// ROM 0x001f4cdc CheckSymbols__FPUs
// Whether the word is made of things the checker can look up at all:
// letters, and the two apostrophes.  Anything else - a digit, a bracket,
// a symbol - and it is left alone.
Boolean
CheckSymbols(const UniChar* word)
{
	long length = Ustrlen(word);
	for (long i = 0; i < length; i++)
		if (!IsAlphabet(word[i]) && word[i] != 0x0027 && word[i] != 0x2019)
			return false;
	return true;
}


// ROM 0x001f54e4 CheckNumbers__FPUs
// Whether a word with a digit anywhere in it is one the number
// dictionaries know - a date, a time, an amount of money.  A word with
// no digit in it at all is not one of theirs.
Boolean
CheckNumbers(const UniChar* word)
{
	long length = Ustrlen(word);
	for (long i = 0; i < length; i++)
		if (word[i] >= '0' && word[i] <= '9')
		{
			char bytes[kSpellWordMax + 2];
			ConvertFromUnicode(word, bytes, 1, kSpellWordMax);
			return ValidateWordInNumberChain(bytes) != -1;
		}
	return false;
}


// ROM 0x001f41bc FSpellCheck
// SpellCheck(frame, word): what is wrong with the spelling of a word.
//
// ==> nil when nothing is - which includes a word too short to judge
// (one character), one the number dictionaries know, and one made of
// characters the checker does not look at; true when the dictionaries do
// not have it at all; 128 when they have it but only with a capital
// first letter, and 192 when they have it only in capitals.
//
// The word is taken apart first - the punctuation off both ends, a
// possessive off the end, the capitalisation noted and taken off - and
// then looked up three ways: as it stands, with a capital first letter,
// and in capitals.  The first that answers decides, and the answer says
// how what was written differs from what was found.
Ref
FSpellCheck(RefArg /*rcvr*/, RefArg frame, RefArg word)
{
	if (ISNIL(frame))
		return NILREF;
	gSpeller = GetSpeller(frame);

	RefVar copy(Clone(word));
	UniChar* text = CString(copy);
	UniChar* leading;
	UniChar* trailing;
	UniChar* contractionLeading;
	UniChar* contraction;
	CollectPunctSymbols(text, &leading, &trailing);
	CollectContractions(text, &contractionLeading, &contraction);

	Boolean capitalized = Capitalized(text);
	Boolean allCapitals = SpellAllCapitals(text);
	if (capitalized || allCapitals)
		LowercaseText(text, allCapitals ? Ustrlen(text) : 1);
	ULong written = 0;
	if (capitalized)
		written = 0x80;
	if (allCapitals)
		written |= 0x40;

	Ref answer = NILREF;
	if (Ustrlen(text) >= 2 && !CheckNumbers(text))
	{
		if (!CheckSymbols(text))
			answer = TRUEREF;			// nothing the checker can judge
		else if (Ustrlen(text) < kSpellWordMax && Ustrlen(text) != 0)
		{
			Boolean skipped = ISNIL(RefVar(GetFrameSlotRef(frame,
														   RSSYMcountskippedasmisspelled)));
			char bytes[kSpellWordMax + 2];
			ULong found = 0;
			ConvertFromUnicode(text, bytes, 1, kSpellWordMax);
			FixQuotes(bytes);
			long where = ValidateWordInChain(bytes, &found, skipped);
			if (where != -1)
			{
				// it is a word: the only question left is whether it was
				// written with the capitalisation the dictionary holds
				if (written != found)
				{
					if ((found & 0x40) != 0 && (written & 0x40) == 0)
						answer = MAKEINT(0xc0);
					else if ((found & 0x80) != 0 && (written & 0x80) == 0)
						answer = MAKEINT(0x80);
				}
			}
			else
			{
				// ... with a capital first letter?
				UppercaseText(text, 1);
				ConvertFromUnicode(text, bytes, 1, kSpellWordMax);
				FixQuotes(bytes);
				where = ValidateWordInChain(bytes, &found, skipped);
				if (where != -1)
				{
					if (written == 0)
						answer = MAKEINT(0x80);
				}
				else
				{
					// ... or in capitals?
					UppercaseText(text, Ustrlen(text));
					ConvertFromUnicode(text, bytes, 1, kSpellWordMax);
					FixQuotes(bytes);
					where = ValidateWordInChain(bytes, &found, skipped);
					if (where != -1)
					{
						if (written != 0xc0)
							answer = MAKEINT(0xc0);
					}
					else
						answer = TRUEREF;		// no spelling of it is a word
				}
			}
		}
	}

	if (leading != nil)
		DisposePtr((Ptr) leading);
	if (trailing != nil)
		DisposePtr((Ptr) trailing);
	if (contraction != nil)
		DisposePtr((Ptr) contraction);
	return answer;
}


void
RegisterSpellingNatives(void)
{
	RegisterNativeFunction("FSpellDocBegin", (void*) FSpellDocBegin, 0);
	RegisterNativeFunction("FSpellDocEnd", (void*) FSpellDocEnd, 1);
	RegisterNativeFunction("FSpellCheck", (void*) FSpellCheck, 2);
}
