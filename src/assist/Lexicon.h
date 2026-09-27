/*
	File:		assist/Lexicon.h

	Contains:	The Assistant's lexicon: the words it knows and what each
				of them stands for.

				Two Airus dictionaries hold the words.  `gTrie` is the
				ROM's own (ROM dictionary 2), each word's attribute an index
				into the array of lexicon entries at magic pointer 248
				(`gDictionaryFrame`) - "call" is an index there, and the
				entry is the list of the things "call" can mean.  `gDynaTrie`
				is the one the machine builds at run time out of the
				`Lexicon` slots of the classes and task templates
				registered with it (MakePhrasalLexEntry): each word's
				attribute is an index into `gDynaDictionaryFrame`, whose
				entries are [the number of registrations of the word, the
				list of frames it stands for].

				MatchString is where a phrase of the sentence is looked up:
				the two tries first (the run-time one's meanings joined to
				the ROM's), then - for a phrase neither knows - the lexical
				dictionaries of the locale (a date, a time, a phone number,
				a number: each made a copy of the Assistant's `lexical`
				frame with the matching class as its `isa`), and last of all
				DSResolveString, which asks the Names file whether it is a
				person or a place.  What comes back is a list of frames, each
				a copy of a meaning with the phrase in its `value`.

	Reconstructed from the MP2x00 US ROM (0x0007ce20-0x0007ebb4); each
	function cites its origin.
*/

#ifndef __LEXICON_H
#define __LEXICON_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif
#include "objects.h"

extern Handle	gTrie;					// ROM 0x0c100b68 gTrie - the ROM's lexicon (ROM dictionary 2)
extern Handle	gDynaTrie;				// ROM 0x0c100b6c gDynaTrie - the words registered at run time
extern Ref		gDateFrame;				// ROM 0x0c100b70 gDateFrame - @8.Date, the class of a date
extern Ref		gTimeFrame;				// ROM 0x0c100b74 gTimeFrame - @8.Time
extern Ref		gPhoneFrame;			// ROM 0x0c100b78 gPhoneFrame - @8.parsed_phone
extern Ref		gNumberFrame;			// ROM 0x0c100b7c gNumberFrame - @8.parsed_number
extern Ref		gDictionaryFrame;		// ROM 0x0c100b84 gDictionaryFrame - @248, gTrie's entries
extern Ref		gDynaDictionaryFrame;	// ROM 0x0c100b88 gDynaDictionaryFrame - gDynaTrie's entries: [count, frames]
extern Ref		gLastLookupString;		// ROM 0x0c100b94 gLastLookupString

// the Assistant's lexicon: set up by InitDarkStar
Ref		InitDSDictionary(RefArg rcvr, RefArg arg);				// ROM 0x0007d204 InitDSDictionary__FRC6RefVarT1
Handle	TrieInit(void);											// ROM 0x0007e8f4 TrieInit__Fv - an empty RAM dictionary with 4-byte attributes

// words in and out of the run-time trie
void	TrieAdd(char* word, Handle dictionary, RefArg frame);	// ROM 0x0007ce20 TrieAdd__FPcPP15AirusAParmBlockRC6RefVar
Ref		DynaTrieLookup(char* word);								// ROM 0x0007d7d8 DynaTrieLookup__FPc - the frames a registered word stands for, or nil
Ref		DynaTrieDelete(RefArg rcvr, RefArg word);				// ROM 0x0007e900 DynaTrieDelete__FRC6RefVarT1
Ref		DynaCompress(ULong index);								// ROM 0x0007d880 DynaCompress__FUl - an entry taken out and the attributes above it renumbered
Ref		GetDictItem(RefArg rcvr, RefArg index);					// ROM 0x0007d378 GetDictItem__FRC6RefVarT1 - gTrie's entry
long	DictAppendItem(RefArg array, RefArg item);				// ROM 0x0007d3d4 DictAppendItem__FRC6RefVarT1 - ==> its index
Ref		DumpDict(RefArg rcvr);									// ROM 0x0007d484 DumpDict__FRC6RefVar
Ref		DSAddLexiconFrame(RefArg rcvr, RefArg frame);			// ROM 0x0007d494 DSAddLexiconFrame__FRC6RefVarT1
void	LoadLexiconFrames(RefArg frames);						// ROM 0x0007d52c LoadLexiconFrames__FRC6RefVar

// the lexical dictionaries of the locale
Ref		LexLookup(Handle dictionary, char* word);				// ROM 0x0007eb64 LexLookup__FPP15AirusAParmBlockPc - TRUE when it is a word of it
Ref		LexDateLookup(RefArg rcvr, RefArg str);					// ROM 0x0007d0d4 LexDateLookup__FRC6RefVarT1
Ref		LexTimeLookup(RefArg rcvr, RefArg str);					// ROM 0x0007d124 LexTimeLookup__FRC6RefVarT1
Ref		LexPhoneLookup(RefArg rcvr, RefArg str);				// ROM 0x0007d174 LexPhoneLookup__FRC6RefVarT1
Ref		cLexDateLookup(RefArg rcvr, char* word);				// ROM 0x0007d1c4 cLexDateLookup__FRC6RefVarPc
Ref		cLexTimeLookup(RefArg rcvr, char* word);				// ROM 0x0007d1d4 cLexTimeLookup__FRC6RefVarPc
Ref		cLexPhoneLookup(RefArg rcvr, char* word);				// ROM 0x0007d1e4 cLexPhoneLookup__FRC6RefVarPc
Ref		cLexNumberLookup(RefArg rcvr, char* word);				// ROM 0x0007d1f4 cLexNumberLookup__FRC6RefVarPc

// a phrase of the sentence looked up
Ref		TagPhraseFrame(RefArg rcvr, RefArg meanings, RefArg phrase);	// ROM 0x0007dca0 TagPhraseFrame__FRC6RefVarN21 - each meaning copied with the phrase as its value
Ref		MatchString(Handle dictionary, char* phrase, RefArg info);	// ROM 0x0007ddd8 MatchString__FPP15AirusAParmBlockPcRC6RefVar
Ref		FastStringLookup(RefArg rcvr, RefArg word);				// ROM 0x000831d8 FastStringLookup__FRC6RefVarT1 - the meanings of one word, both tries

void	RegisterLexiconNatives(void);

#endif	/* __LEXICON_H */
