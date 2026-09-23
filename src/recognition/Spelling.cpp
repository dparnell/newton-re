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


/*------------------------------------------------------------------------------
	G u e s s i n g

	What a misspelling was meant to be.  The word is put through five
	kinds of change - two letters swapped, one dropped, one inserted, the
	word split in two, and one letter read as another - and whatever
	comes out that the dictionary has is offered, nearest first.

	The insertions and the substitutions do not try one replacement at a
	time: they put a `?` where the change goes and hand the word to
	`DoWord`, which walks the dictionary and the map together.  That is
	the engine, and it is a stack of `word_state`s, one per piece of the
	word matched so far: at each piece it takes the map entries whose
	pattern the word goes on with, and for each of them every spelling
	that entry allows, and follows the ones the trie can actually take.
------------------------------------------------------------------------------*/

// ROM 0x001f5b08 InvalidateListState__FP10word_state
void
InvalidateListState(word_state* state)
{
	state->fList = kSpellStateDone;
}


// ROM 0x001f5c9c GetNextElement__FP10word_state
void
GetNextElement(word_state* state)
{
	state->fElement++;
}


// The tail `FindList` and `GetNextList` share: the map walked from the
// state's entry on, looking for one whose pattern the word goes on with.
// The map is sorted, so a pattern that sorts past the word ends the
// search.  A state that has already spent its six edits stops matching
// altogether and takes the rest of the word as it stands.
static void
SeekList(word_state* state)
{
	const SpellMapEntry* map = gSpeller->fMap;
	if (state->fEdits < kSpellMaxEdits)
	{
		const char* pattern = map[state->fList].fPattern;
		if (pattern == nil)
		{
			state->fList = kSpellStateDone;
			return;
		}
		for (;;)
		{
			long matched = 0;
			const char* w = gSpeller->fWord + state->fFrom;
			Boolean advance = false;
			while (*pattern != 0)
			{
				if (*w == 0)
				{
					advance = true;
					break;
				}
				UByte a = (UByte) *w++;
				UByte b = (UByte) *pattern++;
				if (a != b)
				{
					matched = a < b ? -1 : -2;
					break;
				}
				matched++;
			}
			if (!advance)
			{
				if (matched == -1)
					break;				// the map has sorted past the word
				if (matched > 0)
				{
					state->fTake = (UByte) matched;
					return;
				}
			}
			state->fList++;
			pattern = map[state->fList].fPattern;
			if (pattern == nil)
				break;
		}
	}
	else if (state->fList != kSpellStateRest)
	{
		state->fList = kSpellStateRest;
		state->fTake = (UByte) (gSpeller->fWordLength - state->fFrom);
		return;
	}
	state->fList = kSpellStateDone;
}


// ROM 0x001f5a1c FindList__FP10word_state
void
FindList(word_state* state)
{
	SeekList(state);
}


// ROM 0x001f59d0 BumpListState__FP10word_state
// On to the next entry of the map, with the element and its cost reset.
UByte
BumpListState(word_state* state)
{
	if (state->fList < kSpellStateRest)
	{
		state->fList++;
		state->fElement = 0;
		state->fCost = 0;
		if (gSpeller->fMap[state->fList].fPattern == nil)
			state->fList = kSpellStateDone;
	}
	return state->fList;
}


// ROM 0x001f5950 GetCurrentList__FP10word_state
// ==> the map entry the state stands on, or -1 when it has run out.
long
GetCurrentList(word_state* state)
{
	if (state->fList == kSpellStateStart)
	{
		state->fList = 0;
		state->fElement = 0;
		state->fCost = 0;
		FindList(state);
	}
	long list = state->fList;
	if (list > 0xfc)
		list = -1;
	return list;
}


// ROM 0x001f5994 GetNextList__FP10word_state
void
GetNextList(word_state* state)
{
	if (state->fList > 0xfc)
		return;
	BumpListState(state);
	if (state->fList > 0xfc)
		return;
	SeekList(state);
}


// ROM 0x001f5b14 GetCurrentElement__FP10word_state
// The spelling the state stands on, or the end marker.  A number among
// the entry's spellings is not one: it is the cost of everything after
// it, which is taken as the state passes over it.  A spelling whose
// first character is not one the trie can go on with here is passed over
// too, which is what keeps the walk from trying every letter at every
// step.
const char*
GetCurrentElement(word_state* state)
{
	if (state->fList == kSpellStateDone)
		return nil;
	if (state->fEdits >= kSpellMaxEdits)
		return state->fElement == 0 ? gSpeller->fWord + state->fFrom : nil;

	const SpellItem* items = gSpeller->fMap[state->fList].fItems;
	char at = gSpeller->fWord[state->fFrom];
	Boolean boundary = at == '$' || at == '@';
	for (;;)
	{
		const SpellItem* item = &items[state->fElement];
		if (item->fText == nil)
		{
			if (item->fValue == kSpellEndOfList)
				return nil;
			state->fCost = (UByte) item->fValue;
		}
		else if (boundary || item->fText[0] == 0
				 || strpos(state->fChars, item->fText[0]) != nil)
			return item->fText;
		state->fElement++;
	}
}


// ROM 0x001f5570 GetNextCharacters__FP10word_state
// The set of characters the trie can go on with at the state's node,
// read into the state.
void
GetNextCharacters(word_state* state)
{
	AirusAParmBlock* parms = (AirusAParmBlock*) *gSpeller->fDictionary;
	state->fChars[0] = 0;
	parms->fWord = (UByte*) state->fChars;
	parms->fNode = state->fNode;
	CallAirusANoLock(gSpeller->fDictionary, kAirusNextSet);
}


// ROM 0x001f4d44 ResetWordState__FPcPPvl
// The stack put back to one state standing at the root of the trie, with
// a word to match and a map to match it through.  `cost` is what a guess
// found this way is charged on top of what the map asked.
void
ResetWordState(char* word, const SpellMapEntry* map, long cost)
{
	gSpeller->fWord = word;
	gSpeller->fWordLength = (long) strlen(word);
	gSpeller->fMap = map;
	gSpeller->fBaseCost = cost;
	gSpeller->fDepth = 0;

	word_state* state = (word_state*) gSpeller->fWork;
	state->fList = kSpellStateStart;
	state->fElement = 0;
	// (the ROM leaves fCost as it lies)
	state->fFrom = 0;
	state->fTake = 0;
	state->fAt = 0;
	state->fPut = 0;
	state->fEdits = 0;
	state->fResult = 0;
	state->fNode = 0;
	state->fChars[0] = 0;
	GetNextCharacters(state);
}


// ROM 0x001f6250 ScoreGuess__Fv
// What the walk that reached here cost: the costs the map asked at each
// state, and the caller's own on top when anything was spent at all.
long
ScoreGuess(void)
{
	word_state* states = (word_state*) gSpeller->fWork;
	long score = 0;
	for (long i = 0; i <= gSpeller->fDepth; i++)
		score += states[i].fCost;
	if (score != 0)
		score += gSpeller->fBaseCost;
	return score;
}


// ROM 0x001f55ac DoWord__Fv
// The dictionary and the map walked together.  The word is bracketed
// with `$` and `@` first, so that a map entry can say what may stand at
// the beginning and the end of a word; each state takes a piece of the
// word and writes a piece of the guess, and a state that reaches the end
// of the word on a node that is a word reports what it built.
void
DoWord(void)
{
	char* original = gSpeller->fWord;
	word_state* states = (word_state*) gSpeller->fWork;
	long length = gSpeller->fWordLength;
	char* bracketed = new char[length + 3];
	bracketed[0] = '$';
	strcpy(bracketed + 1, original);
	bracketed[length + 1] = '@';
	bracketed[length + 2] = 0;
	gSpeller->fWord = bracketed;
	gSpeller->fWordLength = (long) strlen(bracketed);

	word_state* state = states;
	for (;;)
	{
		long list = GetCurrentList(state);
		while (list >= 0)
		{
			word_state* next = state;
			const char* element = GetCurrentElement(state);
			while (element != nil)
			{
				next = state;
				state->fPut = (UByte) strlen(element);
				Boolean tooFar = (long) state->fAt + state->fPut >= kSpellWordMax - 1
								 || gSpeller->fCost >= kSpellNoGuess;
				if (tooFar)
					InvalidateListState(state);
				else
				{
					AirusAParmBlock* parms = (AirusAParmBlock*) *gSpeller->fDictionary;
					parms->fWord = (UByte*) gSpeller->fScratch;
					parms->fIndex = state->fAt;
					parms->fNode = state->fNode;
					char* out = gSpeller->fScratch + state->fAt;
					airusResult = state->fResult;
					char ch = *element;
					*out = ch;
					while (ch != 0)
					{
						element++;
						out++;
						gSpeller->fCost++;
						*out = 0;
						CallAirusANoLock(gSpeller->fDictionary, kAirusVerify);
						airusResult = parms->fResult;
						if (airusResult == kAirusNoMatch)
							break;
						ch = *element;
						*out = ch;
					}
					if (airusResult != kAirusNoMatch)
					{
						// more of the word to match, or a node that is only the
						// beginning of words: go on.  Only a node that is a word
						// itself, with the whole word matched, is a guess.
						Boolean more = (long) state->fFrom + state->fTake < gSpeller->fWordLength;
						if (more || airusResult == kAirusPrefix)
						{
							if (gSpeller->fDepth > kSpellStateMax)
							{
								InvalidateListState(state);
								goto nextElement;
							}
							long depth = ++gSpeller->fDepth;
							next = &states[depth];
							next->fList = kSpellStateStart;
							next->fElement = 0;
							next->fFrom = (UByte) (state->fFrom + state->fTake);
							next->fTake = 0;
							next->fAt = (UByte) (state->fAt + state->fPut);
							next->fPut = 0;
							next->fEdits = state->fEdits;
							// what it wrote is not what it took: an edit
							if (state->fTake != state->fPut
								|| strncmp(gSpeller->fWord + state->fFrom,
										   gSpeller->fScratch + state->fAt,
										   state->fTake) != 0)
								next->fEdits++;
							next->fNode = parms->fNode;
							next->fResult = (UByte) parms->fResult;
							GetNextCharacters(next);
							break;			// carry on at the new state
						}
						else
							ReportGuess(gSpeller->fScratch, parms->fAttribute, ScoreGuess());
					}
				}
			nextElement:
				GetNextElement(state);
				element = GetCurrentElement(state);
			}
			GetNextList(next);
			list = GetCurrentList(next);
			state = next;
		}
		if (GetCurrentList(state) < 0)
		{
			long depth = --gSpeller->fDepth;
			if (depth < 0)
				break;
			state = &states[depth];
			GetNextElement(state);
		}
	}

	delete[] bracketed;
	gSpeller->fWord = original;
	gSpeller->fWordLength = (long) strlen(original);
}


/*------------------------------------------------------------------------------
	T h e   g u e s s e s   t h e m s e l v e s
------------------------------------------------------------------------------*/

// ROM 0x001f5cac FixCapitalization__FPcUl
// The guess given the capitalisation the dictionary said the word has.
void
FixCapitalization(char* word, ULong attribute)
{
	if ((attribute & 0x80) != 0)
		word[0] = (char) toupper((UByte) word[0]);
	if ((attribute & 0x40) == 0)
		return;
	long length = (long) strlen(word);
	for (long i = 0; i < length; i++)
		word[i] = (char) toupper((UByte) word[i]);
}


// ROM 0x001f612c MeasureDistance__FPclT1T2
// How far one spelling is from another: the characters of each that the
// other has none of, the greater of the two counts, the difference in
// length on top, and five more when they are not the same word.  It is
// not an edit distance - it counts sets, not order - which is why the
// generators add their own cost for the kind of change they made.
long
MeasureDistance(const char* a, long aLength, const char* b, long bLength)
{
	if (aLength != 0)
	{
		if (a[0] == '$')
		{
			a++;
			aLength--;
		}
		if (aLength != 0 && a[aLength - 1] == '@')
			aLength--;
	}
	long difference = aLength - bLength;
	if (difference < 0)
		difference = -difference;

	long missingFromB = 0;
	for (long i = 0; i < aLength; i++)
	{
		Boolean found = false;
		for (long j = 0; j < bLength; j++)
			if (a[i] == b[j])
				found = true;
		if (!found)
			missingFromB++;
	}
	long missingFromA = 0;
	for (long i = 0; i < bLength; i++)
	{
		Boolean found = false;
		for (long j = 0; j < aLength; j++)
			if (b[i] == a[j])
				found = true;
		if (!found)
			missingFromA++;
	}
	long distance = missingFromB > missingFromA ? missingFromB : missingFromA;
	distance += difference;
	if (aLength != bLength || strncmp(a, b, (size_t) aLength) != 0)
		distance += 5;
	return distance;
}


// ROM 0x001f5e68 FindGuess__FPc
// Where that spelling is among the guesses; kSpellGuessCount for nowhere.
long
FindGuess(const char* word)
{
	for (long i = 0; i < kSpellGuessCount; i++)
		if (strcmp(gSpeller->fGuesses[i].fWord, word) == 0)
			return i;
	return kSpellGuessCount;
}


// ROM 0x001f5ebc DeleteGuess__Fl
// One taken out, the rest moved down, and its buffer put back on the end
// with nothing in it.
void
DeleteGuess(long at)
{
	long count = gSpeller->fGuessCount;
	if (at >= count)
		return;
	char* buffer = gSpeller->fGuesses[at].fWord;
	for (long i = at + 1; i < count; i++)
		gSpeller->fGuesses[i - 1] = gSpeller->fGuesses[i];
	gSpeller->fGuessCount = --count;
	gSpeller->fGuesses[count].fWord = buffer;
	gSpeller->fGuesses[count].fScore = kSpellNoGuess;
}


// ROM 0x001f5f54 InsertGuess__FPcl
// A guess put in its place among the others, nearest first.  A spelling
// that is there already keeps the better of the two scores.  There is
// room for seven, and the worst falls off the end.
Boolean
InsertGuess(const char* word, long score)
{
	long at = FindGuess(word);
	if (at < kSpellGuessCount)
	{
		if (gSpeller->fGuesses[at].fScore <= score)
			return true;			// the one that is there is nearer
		DeleteGuess(at);
	}
	long i = 0;
	for (; i < kSpellGuessCount; i++)
		if (score < gSpeller->fGuesses[i].fScore)
			break;
	if (i < kSpellGuessCount)
	{
		long count = gSpeller->fGuessCount;
		char* buffer = gSpeller->fGuesses[count].fWord;
		for (; i < count; count--)
			gSpeller->fGuesses[count] = gSpeller->fGuesses[count - 1];
		strcpy(buffer, word);
		gSpeller->fGuesses[i].fWord = buffer;
		gSpeller->fGuesses[i].fScore = score;
		if (gSpeller->fGuessCount < kSpellGuessCount - 1)
			gSpeller->fGuessCount++;
	}
	return true;
}


// ROM 0x001f5df0 ReportGuess__FPcUll
// A spelling the walk reached offered as a guess, with the dictionary's
// capitalisation put back on it and the writer's own quote put back in.
void
ReportGuess(const char* word, ULong attribute, long score)
{
	char* copy = new char[strlen(word) + 1];
	if (copy == nil)
		return;
	strcpy(copy, word);
	FixCapitalization(copy, attribute);
	RestoreQuotes(copy, gSpeller->fQuoted != 0);
	InsertGuess(copy, score);
	delete[] copy;
}


/*------------------------------------------------------------------------------
	T h e   f i v e   k i n d s   o f   c h a n g e
------------------------------------------------------------------------------*/

// ROM 0x001f4f7c SwapTwo__FPclT2
void
SwapTwo(char* word, long a, long b)
{
	char was = word[a];
	word[a] = word[b];
	word[b] = was;
}


// ROM 0x001f4f90 DoTranspositions__FPc
// Each neighbouring pair swapped.
void
DoTranspositions(const char* word)
{
	long length = (long) strlen(word);
	if (length <= 2)
		return;
	char* copy = new char[length + 1];
	if (copy == nil)
		return;
	for (long i = 1; i < length; i++)
	{
		strcpy(copy, word);
		SwapTwo(copy, i - 1, i);
		ULong attribute;
		if (ValidateWord2(gSpeller->fDictionary, copy, &attribute) != -1)
		{
			FixCapitalization(copy, attribute);
			ReportGuess(copy, attribute,
						MeasureDistance(word, length, copy, length) + 2);
		}
	}
	delete[] copy;
}


// ROM 0x001f5068 DoTwoTranspositions__FPc
// ... and two pairs at once, which is only tried when nothing else was
// found at all.
void
DoTwoTranspositions(const char* word)
{
	long length = (long) strlen(word);
	if (length <= 3)
		return;
	char* copy = new char[length + 1];
	if (copy == nil)
		return;
	for (long i = 1; i < length; i++)
		for (long j = 1; j < length; j++)
		{
			if (i == j)
				continue;
			strcpy(copy, word);
			SwapTwo(copy, i - 1, i);
			SwapTwo(copy, j - 1, j);
			ULong attribute;
			if (ValidateWord2(gSpeller->fDictionary, copy, &attribute) != -1)
			{
				FixCapitalization(copy, attribute);
				ReportGuess(copy, attribute,
							MeasureDistance(word, length, copy, length) + 10);
			}
		}
	delete[] copy;
}


// ROM 0x001f5170 DoDeletions__FPc
// Each character dropped in turn.
void
DoDeletions(const char* word)
{
	long length = (long) strlen(word);
	if (length <= 2)
		return;
	char* copy = new char[length + 1];
	if (copy == nil)
		return;
	for (long i = 0; i < length; i++)
	{
		strcpy(copy, word);
		strcpy(copy + i, word + i + 1);
		ULong attribute;
		if (ValidateWord2(gSpeller->fDictionary, copy, &attribute) != -1)
		{
			FixCapitalization(copy, attribute);
			ReportGuess(copy, attribute,
						MeasureDistance(word, length, copy, length) + 3);
		}
	}
	delete[] copy;
}


// ROM 0x001f5248 DoInsertions__FPc
// A character put in at each place - not one letter at a time, but a `?`
// the walk expands into whatever the trie will take there.
void
DoInsertions(const char* word)
{
	long length = (long) strlen(word);
	if (length <= 2)
		return;
	char* copy = new char[length + 2];
	if (copy == nil)
		return;
	for (long i = length < 4 ? 1 : 0; i <= length; i++)
	{
		strcpy(copy, word);
		copy[i] = '?';
		strcpy(copy + i + 1, word + i);
		ResetWordState(copy, gwc_map, 0);
		DoWord();
	}
	delete[] copy;
}


// ROM 0x001f546c DoSubstitutions__FPc
// Each character read as another, the same way.
void
DoSubstitutions(char* word)
{
	long length = (long) strlen(word);
	if (length <= 2)
		return;
	for (long i = length < 4 ? 1 : 0; i < length; i++)
	{
		char was = word[i];
		word[i] = '?';
		ResetWordState(word, gwc_map, 1);
		DoWord();
		word[i] = was;
	}
}


// ROM 0x001f52f0 DoWordSplits__FPc
// The word cut in two at each place, when both halves are words.
void
DoWordSplits(const char* word)
{
	long length = (long) strlen(word);
	if (length <= 3)
		return;
	char* copy = new char[length + 2];
	if (copy == nil)
		return;
	for (long i = 1; i < length; i++)
	{
		strcpy(copy, word);
		copy[i] = 0;
		const char* tail = word + i;
		char* second = copy + i + 1;
		strcpy(second, tail);
		ULong attribute;
		if (ValidateWord2(gSpeller->fDictionary, copy, &attribute) == -1)
			continue;
		FixCapitalization(copy, attribute);
		TDictChain* chain = gSpeller->fChain;
		ULong count = (ULong) chain->fCount;
		for (ULong k = 0; k < count; k++)
		{
			if (ValidateWord2(*(Handle*) chain->GetEntry(k), second, &attribute) == -1)
				continue;
			FixCapitalization(second, attribute);
			copy[i] = ' ';
			ReportGuess(copy, 0, MeasureDistance(word, length, copy, length + 1));
			strcpy(second, tail);
		}
	}
	delete[] copy;
}


// ROM 0x001f4aac CheckWord__FPc
// One dictionary put through all five kinds of change, and then the word
// walked through the substitution map as it stands - which is the one
// that catches "ph" for "f" and the like.  The two transpositions are
// only tried when nothing else was found.
void
CheckWord(char* word)
{
	gSpeller->fScratch = new char[kSpellWordMax];
	if (gSpeller->fScratch == nil)
		return;
	gSpeller->fScratch[0] = 0;
	gSpeller->fField14 = 0;
	gSpeller->fWork = new UByte[sizeof(word_state) * (kSpellStateMax + 2)];
	if (gSpeller->fWork != nil)
	{
		long cost = gSpeller->fCost;
		DoTranspositions(word);
		DoDeletions(word);
		gSpeller->fCost = 0;
		DoInsertions(word);
		DoWordSplits(word);
		gSpeller->fCost = 0;
		DoSubstitutions(word);
		gSpeller->fCost = cost;
		ResetWordState(word, gsubstitution_map, 0);
		DoWord();
		if (gSpeller->fGuessCount == 0)
			DoTwoTranspositions(word);
		delete[] gSpeller->fScratch;
		gSpeller->fScratch = nil;
	}
	delete[] (UByte*) gSpeller->fWork;
	gSpeller->fWork = nil;
}


// ROM 0x001f49ac CorrectWordInChain__FPc
// Every dictionary of the session's chain asked for guesses in turn.
void
CorrectWordInChain(char* word)
{
	TDictChain* chain = gSpeller->fChain;
	ULong count = (ULong) chain->fCount;
	for (ULong i = 0; i < count; i++)
	{
		gSpeller->fDictionary = *(Handle*) chain->GetEntry(i);
		CheckWord(word);
	}
}


// ROM 0x001f4a14 LockChain__FP10TDictChain
// The dictionaries of a chain pinned while the guessing runs: the engine
// is given pointers into them and the heap compacts handles.
void
LockChain(TDictChain* chain)
{
	ULong count = (ULong) chain->fCount;
	for (ULong i = 0; i < count; i++)
		HLock(*(Handle*) chain->GetEntry(i));
}


// ROM 0x001f4a60 UnlockChain__FP10TDictChain
void
UnlockChain(TDictChain* chain)
{
	ULong count = (ULong) chain->fCount;
	for (ULong i = 0; i < count; i++)
		HUnlock(*(Handle*) chain->GetEntry(i));
}


// ROM 0x001f4828 RestorePunctSymbols__FRC6RefVarPUsT2UcT4
// What was taken off the word put back on the guess: the two runs round
// it, and the capitalisation it was written with.
Ref
RestorePunctSymbols(RefArg word, const UniChar* leading, const UniChar* trailing,
					Boolean capitalized, Boolean allCapitals)
{
	RefVar out(word);
	long leadLength = leading != nil ? Ustrlen(leading) : 0;
	long trailLength = trailing != nil ? Ustrlen(trailing) : 0;
	if (leadLength != 0 || trailLength != 0)
	{
		const UniChar* text = CString(word);
		long length = Ustrlen(text);
		if (length > 0)
		{
			long total = leadLength + length + trailLength;
			UniChar* built = new UniChar[total + 1];
			if (built != nil)
			{
				if (leading != nil)
					BlockMove(leading, built, leadLength * sizeof(UniChar));
				BlockMove(text, built + leadLength, length * sizeof(UniChar));
				if (trailing != nil)
					BlockMove(trailing, built + leadLength + length,
							  trailLength * sizeof(UniChar));
				built[total] = 0;
				out = MakeString(built);
				delete[] built;
			}
		}
	}
	UniChar* text = CString(out);
	long count = Ustrlen(text) - (leadLength + trailLength);
	if (!allCapitals)
	{
		if (!capitalized)
			return out;
		count = 1;
	}
	UppercaseText(text + leadLength, count);
	return out;
}


// ROM 0x001f44c8 FSpellCorrect
// SpellCorrect(frame, word): what the word might have been meant to be,
// as an array of spellings, nearest first.  It is what the corrector
// offers under the readings the recogniser proposed.
Ref
FSpellCorrect(RefArg /*rcvr*/, RefArg frame, RefArg word)
{
	if (ISNIL(frame))
		return NILREF;
	gSpeller = GetSpeller(frame);
	gSpeller->fCost = 0;
	gSpeller->fGuesses = new SpellGuess[kSpellGuessCount];
	if (gSpeller->fGuesses == nil)
		return NILREF;
	gSpeller->fGuessCount = 0;
	for (long i = 0; i < kSpellGuessCount; i++)
		gSpeller->fGuesses[i].fWord = nil;
	Boolean room = true;
	for (long i = 0; i < kSpellGuessCount && room; i++)
	{
		gSpeller->fGuesses[i].fScore = kSpellNoGuess;
		gSpeller->fGuesses[i].fWord = new char[kSpellWordMax];
		if (gSpeller->fGuesses[i].fWord == nil)
			room = false;
		else
			gSpeller->fGuesses[i].fWord[0] = 0;
	}

	RefVar result;
	UniChar* leading = nil;
	UniChar* trailing = nil;
	UniChar* contractionLeading = nil;
	UniChar* contraction = nil;
	if (room)
	{
		RefVar copy(Clone(word));
		UniChar* text = CString(copy);
		CollectPunctSymbols(text, &leading, &trailing);
		CollectContractions(text, &contractionLeading, &contraction);
		Boolean capitalized = Capitalized(text);
		Boolean allCapitals = SpellAllCapitals(text);
		if (capitalized || allCapitals)
			LowercaseText(text, allCapitals ? Ustrlen(text) : 1);

		char bytes[kSpellWordMax + 2];
		ConvertFromUnicode(text, bytes, 1, kSpellWordMax);
		gSpeller->fQuoted = (UByte) FixQuotes(bytes);
		LockChain(gSpeller->fChain);
		CorrectWordInChain(bytes);
		UnlockChain(gSpeller->fChain);

		long count = 0;
		for (long i = 0; i < kSpellGuessCount; i++)
			if (strlen(gSpeller->fGuesses[i].fWord) != 0)
				count++;
		result = MakeArray(count);
		long at = 0;
		for (long i = 0; i < kSpellGuessCount; i++)
		{
			const char* guess = gSpeller->fGuesses[i].fWord;
			if (strlen(guess) == 0)
				continue;
			UniChar chars[kSpellWordMax + 2];
			ConvertToUnicode(guess, chars, 1, 0x7fffffff);
			RefVar spelling(MakeString(chars));
			spelling = RestorePunctSymbols(spelling, contractionLeading, contraction,
										   capitalized, allCapitals);
			spelling = RestorePunctSymbols(spelling, leading, trailing, false, false);
			if (NOTNIL(spelling))
				SetArraySlot(result, at++, spelling);
		}
	}

	for (long i = 0; i < kSpellGuessCount; i++)
		delete[] gSpeller->fGuesses[i].fWord;
	delete[] gSpeller->fGuesses;
	gSpeller->fGuesses = nil;
	if (leading != nil)
		DisposePtr((Ptr) leading);
	if (trailing != nil)
		DisposePtr((Ptr) trailing);
	if (contraction != nil)
		DisposePtr((Ptr) contraction);
	return result;
}


void
RegisterSpellingNatives(void)
{
	RegisterNativeFunction("FSpellDocBegin", (void*) FSpellDocBegin, 0);
	RegisterNativeFunction("FSpellDocEnd", (void*) FSpellDocEnd, 1);
	RegisterNativeFunction("FSpellCheck", (void*) FSpellCheck, 2);
	RegisterNativeFunction("FSpellCorrect", (void*) FSpellCorrect, 2);
}
