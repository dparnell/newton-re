// Spelling test (recognition/Spelling.h): a session over the machine's
// own dictionaries.
//
// The ROM's objects are imported and InitDictionaries run for real, so
// the chains a session builds are the ones a Newton builds: the
// dictionaries whose domainType says they hold words, and separately the
// ones that hold numbers and dates.

#include "Spelling.h"
#include "Dictionaries.h"
#include "Airus.h"
#include "Words.h"
#include "Learning.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "ROMImport.h"
#include "RSSymbols.h"
#include "Unicode.h"
#include "Locale.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


static void
Uni(UniChar* out, const char* text)
{
	long i = 0;
	for (; text[i] != 0; i++)
		out[i] = (UniChar) (UByte) text[i];
	out[i] = 0;
}


// the guesses a correction answers, as one string
static void
Guesses(RefArg frame, const char* word, char* out)
{
	UniChar text[64];
	Uni(text, word);
	RefVar list(FSpellCorrect(RefVar(NILREF), frame, RefVar(MakeString(text))));
	out[0] = 0;
	if (ISNIL(list))
		return;
	long count = Length(list);
	for (long i = 0; i < count; i++)
	{
		if (i != 0)
			strcat(out, " ");
		RefVar one(GetArraySlotRef(list, i));
		const UniChar* chars = CString(one);
		long at = (long) strlen(out);
		for (long k = 0; chars[k] != 0; k++)
			out[at + k] = (char) chars[k];
		out[at + Ustrlen(chars)] = 0;
	}
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_OBJECTS) != noErr)
	{
		printf("test_Spelling: cannot import %s\n", NEWTON_OBJECTS);
		return 1;
	}
	gObjectHeapSize = 0x200000;
	InitObjects();
	SetFrameSlot(RefVar(gVarFrame), RSSYMvars, RefVar(gVarFrame));
	// the locale, which InitDictionaries asks for the four lexicons a
	// bundle carries
	RefVar intl(AllocateFrame());
	SetFrameSlot(intl, RSSYMcurrentlocalebundle, RefVar(TranslateROMRef(0x004a4d09)));
	SetFrameSlot(RefVar(gVarFrame), RSSYMinternational, intl);
	InitDictionaries();
	// ... and what the locale has to say about them, which is what gives
	// the date, time, phone and money dictionaries their words - without
	// it the number chains are empty
	ReadDictPrefs();

	// ---- the chains ----
	// the words chain holds the general lexicons; the numbers chain holds
	// the ones the machine reads dates, times and money against, and they
	// are not the same dictionaries
	{
		TDictChain* words[kDictChainCount];
		TDictChain* numbers[kDictChainCount];
		InitSpellChains(words);
		InitNumberChains(numbers);
		EXPECT(words[kDictChainOrdinary] != nil);
		EXPECT(words[kDictChainOrdinary]->Count() > 0);
		// the number dictionaries are all of the kind that is only
		// consulted for particular fields (dictType 4), so they land in
		// the exception chain rather than the ordinary one
		EXPECT(numbers[kDictChainOrdinary] == nil);
		EXPECT(numbers[kDictChainException] != nil);
		if (numbers[kDictChainException] != nil)
			EXPECT(numbers[kDictChainException]->Count() > 0);
		DoneChains(words);
		DoneChains(numbers);
		EXPECT(words[kDictChainOrdinary] == nil && numbers[kDictChainException] == nil);
	}

	// ---- a session ----
	{
		RefVar frame(FSpellDocBegin(RefVar(NILREF)));
		EXPECT(NOTNIL(frame));
		spell_state* speller = GetSpeller(frame);
		EXPECT(speller != nil && speller == gSpeller);
		if (speller != nil)
		{
			// the chains are built and the first of the word ones is the
			// chain a check walks
			EXPECT(speller->fChains[kDictChainOrdinary] != nil);
			EXPECT(speller->fChain == speller->fChains[kDictChainOrdinary]);
			EXPECT(speller->fNumberChains[kDictChainException] != nil);
			// and the session has a dictionary of its own for the words
			// it is told to skip, answering to the user dictionary's id
			EXPECT(speller->fIgnore != nil);
			if (speller->fIgnore != nil)
				EXPECT(((AirusAParmBlock*) *speller->fIgnore)->fDictID == kUserDictionary);
			EXPECT(speller->fGuessCount == 0);
		}
		FSpellDocEnd(RefVar(NILREF), frame);
		EXPECT(gSpeller == nil);
		EXPECT(ISNIL(RefVar(GetFrameSlotRef(frame, RSSYMspeller))));
	}

	// ---- the word on the way in and out ----
	{
		char word[32];
		strcpy(word, "don\xd5t");				// a curly right single quote
		EXPECT(FixQuotes(word));
		EXPECT(strcmp(word, "don't") == 0);
		RestoreQuotes(word, true);
		EXPECT((UByte) word[3] == 0xd5);
		// ... and a word with none is left alone
		strcpy(word, "dont");
		EXPECT(!FixQuotes(word));
		RestoreQuotes(word, false);
		EXPECT(strcmp(word, "dont") == 0);

		EXPECT(strpos(word, 'n') == word + 2);
		EXPECT(strpos(word, 'z') == nil);

		UniChar text[32];
		Uni(text, "HELLO");
		EXPECT(SpellAllCapitals(text));
		Uni(text, "Hello");
		EXPECT(!SpellAllCapitals(text));
		Uni(text, "");
		EXPECT(SpellAllCapitals(text));
	}

	// ---- the contraction taken off a word ----
	{
		UniChar text[32];
		UniChar* leading;
		UniChar* trailing;
		Uni(text, "dog's");
		CollectContractions(text, &leading, &trailing);
		EXPECT(text[0] == 'd' && text[3] == 0);
		// (BUG, kept: the leading run is answered nil whatever the word
		//  is - the ROM never writes it)
		EXPECT(leading == nil);
		EXPECT(trailing != nil && trailing[0] == '\'' && trailing[1] == 's'
			   && trailing[2] == 0);
		if (trailing != nil)
			DisposePtr((Ptr) trailing);

		// a word that does not end in a contraction keeps all of itself
		Uni(text, "dogs");
		CollectContractions(text, &leading, &trailing);
		EXPECT(text[3] == 's' && text[4] == 0);
		EXPECT(leading == nil && trailing == nil);
	}

	// ---- is this a word? ----
	// The checker over the ROM's own lexicons: a word spelled right
	// answers nil, one the dictionaries do not have answers true, and
	// one they only hold capitalised answers how it should have been.
	{
		RefVar frame(FSpellDocBegin(RefVar(NILREF)));
		UniChar text[64];

		Uni(text, "notebook");
		EXPECT(ISNIL(RefVar(FSpellCheck(RefVar(NILREF), frame, RefVar(MakeString(text))))));
		Uni(text, "hello");
		EXPECT(ISNIL(RefVar(FSpellCheck(RefVar(NILREF), frame, RefVar(MakeString(text))))));
		// a word nothing has heard of
		Uni(text, "qqxyzzy");
		EXPECT(EQRef(FSpellCheck(RefVar(NILREF), frame, RefVar(MakeString(text))), TRUEREF));
		// one character is too little to judge
		Uni(text, "q");
		EXPECT(ISNIL(RefVar(FSpellCheck(RefVar(NILREF), frame, RefVar(MakeString(text))))));
		// and a word with a bracket in it is not the checker's business
		Uni(text, "qq(zz");
		EXPECT(EQRef(FSpellCheck(RefVar(NILREF), frame, RefVar(MakeString(text))), TRUEREF));

		// the punctuation round a word does not count against it
		Uni(text, "(hello),");
		EXPECT(ISNIL(RefVar(FSpellCheck(RefVar(NILREF), frame, RefVar(MakeString(text))))));
		// nor does a possessive
		Uni(text, "notebook's");
		EXPECT(ISNIL(RefVar(FSpellCheck(RefVar(NILREF), frame, RefVar(MakeString(text))))));

		// a name the lexicon holds capitalised, written in lower case:
		// 0x80 says it should start with a capital
		Uni(text, "newton");
		RefVar answer(FSpellCheck(RefVar(NILREF), frame, RefVar(MakeString(text))));
		EXPECT(ISINT(answer) && RINT(answer) == 0x80);
		Uni(text, "Newton");
		EXPECT(ISNIL(RefVar(FSpellCheck(RefVar(NILREF), frame, RefVar(MakeString(text))))));
		// ... and a misspelling of a real word is just as unknown
		Uni(text, "recieve");
		EXPECT(EQRef(FSpellCheck(RefVar(NILREF), frame, RefVar(MakeString(text))), TRUEREF));

		FSpellDocEnd(RefVar(NILREF), frame);
	}

	// ---- what it might have been meant to be ----
	// The five kinds of change a misspelling is put through, over the
	// ROM's own lexicons: the right word comes out of each of them.
	{
		RefVar frame(FSpellDocBegin(RefVar(NILREF)));
		char guesses[512];

		// two letters the wrong way round
		Guesses(frame, "wrod", guesses);
		EXPECT(strstr(guesses, "word") != nil);
		// one letter too many
		Guesses(frame, "helllo", guesses);
		EXPECT(strstr(guesses, "hello") != nil);
		// one too few
		Guesses(frame, "notebok", guesses);
		EXPECT(strstr(guesses, "notebook") != nil);
		// one letter read as another
		Guesses(frame, "recieve", guesses);
		EXPECT(strstr(guesses, "receive") != nil);
		Guesses(frame, "seperate", guesses);
		EXPECT(strstr(guesses, "separate") != nil);

		// there are never more than seven, and they come nearest first
		Guesses(frame, "teh", guesses);
		EXPECT(strstr(guesses, "the") != nil);
		long count = 1;
		for (const char* p = guesses; *p != 0; p++)
			if (*p == ' ')
				count++;
		EXPECT(count <= kSpellGuessCount);

		// what was written round the word is put back on the guesses
		Guesses(frame, "(wrod),", guesses);
		EXPECT(strstr(guesses, "(word),") != nil);
		// ... and so is the capitalisation it was written with
		Guesses(frame, "Wrod", guesses);
		EXPECT(strstr(guesses, "Word") != nil);

		FSpellDocEnd(RefVar(NILREF), frame);
	}

	// ---- the pieces the guessing is built of ----
	{
		char word[32];
		strcpy(word, "abcd");
		SwapTwo(word, 0, 1);
		EXPECT(strcmp(word, "bacd") == 0);

		// the same word is no distance at all; a different one of the
		// same letters costs the five a difference is charged
		EXPECT(MeasureDistance("word", 4, "word", 4) == 0);
		EXPECT(MeasureDistance("wrod", 4, "word", 4) == 5);
		// ... and the brackets the walk puts round a word are not counted
		EXPECT(MeasureDistance("$word@", 6, "word", 4) == 0);

		// the capitalisation the dictionary says the word has
		strcpy(word, "newton");
		FixCapitalization(word, 0x80);
		EXPECT(strcmp(word, "Newton") == 0);
		strcpy(word, "newton");
		FixCapitalization(word, 0x40);
		EXPECT(strcmp(word, "NEWTON") == 0);
	}

	// ---- a word the session is told to skip ----
	// It goes into the session's own dictionary, so the checker stops
	// complaining about it - until the session ends, when it goes too.
	{
		RefVar frame(FSpellDocBegin(RefVar(NILREF)));
		UniChar text[64];
		Uni(text, "qqxyzzy");
		EXPECT(EQRef(FSpellCheck(RefVar(NILREF), frame, RefVar(MakeString(text))), TRUEREF));
		FSpellSkip(RefVar(NILREF), frame, RefVar(MakeString(text)));
		EXPECT(ISNIL(RefVar(FSpellCheck(RefVar(NILREF), frame, RefVar(MakeString(text))))));
		FSpellDocEnd(RefVar(NILREF), frame);

		// a new session has never heard of it
		RefVar again(FSpellDocBegin(RefVar(NILREF)));
		EXPECT(EQRef(FSpellCheck(RefVar(NILREF), again, RefVar(MakeString(text))), TRUEREF));
		FSpellDocEnd(RefVar(NILREF), again);
	}

	if (failures == 0)
		printf("test_Spelling: all passed\n");
	else
		printf("test_Spelling: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
