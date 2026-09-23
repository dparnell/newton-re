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


void
RegisterSpellingNatives(void)
{
	RegisterNativeFunction("FSpellDocBegin", (void*) FSpellDocBegin, 0);
	RegisterNativeFunction("FSpellDocEnd", (void*) FSpellDocEnd, 1);
}
