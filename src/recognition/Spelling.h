/*
	File:		recognition/Spelling.h

	Contains:	The spelling checker: whether a word is spelled right, and
				what it might have been meant to be.

				It is a self-contained thing sitting on top of the
				dictionaries - it knows nothing about the recogniser, and
				the recogniser reaches it only through the seven
				NewtonScript functions below (`SpellDocBegin`,
				`SpellCheck`, `SpellCorrect`, `SpellSkip`, `SpellDocEnd`,
				and the learn/unlearn pair).  That is the ROM's own seam,
				and it is the place a modern checker would go in.

				A *session* is a `spell_state`: the chains of dictionaries
				a word is checked against, a dictionary of the words this
				session has been told to skip, and the table of guesses a
				correction fills in.  A script holds it as a frame
				(`spellFrame`, whose `speller` slot is the block's
				address), which is what `SpellDocBegin` answers and every
				other call is given.

				The words themselves are eight-bit through all of this:
				the checker works in Mac Roman, and the curly right
				single quote a paragraph writes is turned into a plain
				apostrophe on the way in (`FixQuotes`) and back again on
				the way out (`RestoreQuotes`), because the dictionaries
				hold the plain one.

	Reconstructed from the MP2x00 US ROM (0x001f1b74-0x001f6600); each
	function cites its origin.
*/

#ifndef __SPELLING_H
#define __SPELLING_H

#ifndef __OBJECTS_H
#include "objects.h"
#endif
#ifndef __UNICODE_H
#include "Unicode.h"
#endif
#include "NewtonMemory.h"
#include "Dictionaries.h"		// TDictChain

// One guess at what a word was meant to be: how far it is from what was
// written, and the word itself.
struct SpellGuess						// 8 bytes
{
	long	fScore;						// +0x00  kSpellNoGuess until one is put there
	char*	fWord;						// +0x04  0x32 bytes of its own
};

const long	kSpellGuessCount	= 7;		// how many the checker offers
const long	kSpellNoGuess		= 10000;	// the score an empty slot carries
const long	kSpellWordMax		= 0x32;		// the longest word it will look at

// A spelling session.  (The ROM's is 0x54 bytes; here the pointers are
// pointer-sized, so it is not - nothing outside reads it by offset.)
struct spell_state
{
	long		fField00[6];			// +0x00  what the guessing walks with
	SpellGuess*	fGuesses;				// +0x18  kSpellGuessCount of them
	long		fGuessCount;			// +0x1c
	TDictChain*	fChains[kDictChainCount];		// +0x20  the dictionaries a word is checked against
	TDictChain*	fNumberChains[kDictChainCount];	// +0x2c  ... and the ones a number-like word is
	TDictChain*	fChain;					// +0x38  the chain being walked
	long		fField3c;
	Handle		fIgnore;				// +0x40  the words this session was told to skip
	UByte		fQuoted;				// +0x44  the word had a curly quote in it
	long		fField48;
	long		fField4c;
	long		fField50;
};

extern spell_state*	gSpeller;			// ROM 0x0c101b20 gSpeller - the session being worked on

// The chains a session checks against: every dictionary of the machine's
// list whose `domainType` says it holds words (0x1000, and not the 1 bit
// that marks the ones a lookup must not offer), and separately the ones
// that hold numbers, dates and the like (0x1c2000).
void	InitSpellChains(TDictChain** chains);				// ROM 0x001f1b74 InitSpellChains__FPP10TDictChain
void	InitNumberChains(TDictChain** chains);				// ROM 0x001f40a4 InitNumberChains__FPP10TDictChain

// The frame a script holds a session by, and the session behind one.
Ref			MakeSpellFrame(spell_state* speller);			// ROM 0x001f62ac MakeSpellFrame__FP11spell_state
spell_state*	GetSpeller(RefArg frame);					// ROM 0x001f6314 GetSpeller__FRC6RefVar

// The curly right single quote turned into a plain apostrophe and back:
// the dictionaries hold the plain one, and the paragraph writes the
// curly one.  ==> whether there was one.
Boolean	FixQuotes(char* word);								// ROM 0x001f5d50 FixQuotes__FPc
void	RestoreQuotes(char* word, Boolean quoted);			// ROM 0x001f5da0 RestoreQuotes__FPcUc
// The first character of that one in the string; nil for none.
char*	strpos(char* str, char c);							// ROM 0x001f4b98 strpos__FPcc

// Whether every character of the word is a capital Roman letter (an
// empty word is).  The ROM writes this test out twice; the corrector has
// its own copy.
Boolean	SpellAllCapitals(const UniChar* word);				// ROM 0x001f5d10 (unnamed) - AllCapitals

// The session: SpellDocBegin() answers the frame, and every other call
// is given it.  A session holds a dictionary of its own for the words it
// has been told to skip, and DocEnd writes the user dictionary out when
// anything was learnt.
Ref		FSpellDocBegin(RefArg rcvr);						// ROM 0x001f6360 FSpellDocBegin
Ref		FSpellDocEnd(RefArg rcvr, RefArg frame);			// ROM 0x001f63f8 FSpellDocEnd

void	RegisterSpellingNatives(void);

#endif	/* __SPELLING_H */
