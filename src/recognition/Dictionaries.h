/*
	File:		recognition/Dictionaries.h

	Contains:	The dictionaries the machine knows, and how a word is
				looked up in them.

				`vars.dictionaries` is an array of frames, one per
				dictionary: what kind it is (`dictType`), what it is for
				(`domainType`, a mask of the input kinds it serves),
				which one it is (`dictId`) and which other one it is
				linked to (`linkedDictId`).  `gDictList` runs alongside
				it: one `dictListEntry` per frame, holding the opened
				Airus dictionary itself.

				A lookup does not walk that list.  It walks a *chain* -
				a `TDictChain`, which is an array of the dictionaries
				that matter for the question being asked - and there are
				three of them, picked by the frame's `dictType`:

				    0  the ordinary lexicons
				    1  the ones that are only consulted for particular
				       fields (dates, places, and the like)
				    4  the exceptions, which are chain 2

				`BuildChains` fills the three in from the recognition
				configuration of whatever view is being written on: the
				dictionaries it names by hand first, then every
				dictionary of the list whose `domainType` overlaps the
				view's input mask, and then the symbols dictionary
				unless the configuration says to leave it out.  A chain
				is built for one lookup and thrown away again.

				`LookupWord` is what everything else calls: it builds
				the chains, converts the word to eight-bit characters
				and asks each dictionary of the first chain and then of
				the third.  ==> the id of the dictionary the word was
				found in, or -1.

	Reconstructed from the MP2x00 US ROM (0x0013d460-0x0013fb0c); each
	function cites its origin.
*/

#ifndef __DICTIONARIES_H
#define __DICTIONARIES_H

#include "RecObject.h"
#include "objects.h"
#include "Words.h"			// Dictionaries, FindDictionaryFrame
#include "RecConfig.h"		// CountCustomDictionaries(TView*)

class TView;

// One dictionary of the machine's list.
struct dictListEntry				// 8 bytes
{
	Handle		fDictionary;		// +0x00  the opened Airus dictionary
	UByte		fIndex;				// +0x04  which frame of vars.dictionaries
	UByte		fStatus;			// +0x05  0: nothing opened, leave it out
	UByte		fDisabled;			// +0x06  non-zero: leave it out of the chains
	UByte		fPad;				// +0x07
};


// The dictionaries a lookup is to walk.  It is an array of Handles with
// one thing of its own: which of them the walk has reached.
class TDictChain : public TDArray
{
public:
						TDictChain();							// ROM 0x0020cab0 __ct__10TDictChainFv
	static TDictChain*	Make(ULong count, ULong position);		// ROM 0x0020caf0 Make__10TDictChainSFUlT1
	long				IDictChain(ULong count, ULong position);	// ROM 0x0020cb44 IDictChain__10TDictChainFUlT1

	Handle				PositionToHandle(ULong position);		// ROM 0x0020cbf0 PositionToHandle__10TDictChainFUl
	ULong				HandleToPosition(Handle dictionary);	// ROM 0x0020cc18 HandleToPosition__10TDictChainFPP15AirusAParmBlock
	void				AddDictToChain(Handle dictionary);		// ROM 0x0020cbc8 AddDictToChain__10TDictChainFPP15AirusAParmBlock
	long				RemoveDictFromChain(Handle dictionary);	// ROM 0x0020cb7c RemoveDictFromChain__10TDictChainFPP15AirusAParmBlock

	long				fPosition;		// +0x20  where the walk is (-1: nowhere)
};

// how many chains there are, and what each is for
const long	kDictChainCount		= 3;
const long	kDictChainOrdinary	= 0;
const long	kDictChainSpecial	= 1;
const long	kDictChainException	= 2;

// The four lexicons the locale carries for reading dates, times, phone
// numbers and numbers out of what is written.  They are not in the list:
// InitDictionaries opens each of them from the current locale bundle and
// leaves it here for the lexical analysis to use.
extern Handle	gTimeLexDictionary;		// ROM 0x0c100f8c gTimeLexDictionary
extern Handle	gDateLexDictionary;		// ROM 0x0c100f90 gDateLexDictionary
extern Handle	gPhoneLexDictionary;	// ROM 0x0c100f94 gPhoneLexDictionary
extern Handle	gNumberLexDictionary;	// ROM 0x0c100f98 gNumberLexDictionary

extern TDArray*	gDictList;			// ROM 0x0c10162c gDictList - one dictListEntry per frame

// The list built: every dictionary of the ROM opened and put in
// vars.dictionaries, with gDictList beside it.
void	InitDictionaries(void);								// ROM 0x0013de2c InitDictionaries__Fv

// The frames: vars.dictionaries, and the one with a given id.
// The list entry for an id.  Some ids stand for others, and an id that
// names nothing falls back on the one the list calls 6.
dictListEntry*	FindDictionaryEntry(ULong id);				// ROM 0x0013d4ac FindDictionaryEntry__FUl

// The dictionaries a configuration names by hand.
long	CountCustomDictionaries(RefArg config);				// ROM 0x0013fa44 CountCustomDictionaries__FRC6RefVar
ULong	GetCustomDictionary(RefArg config, ULong index);	// ROM 0x0013fa9c GetCustomDictionary__FRC6RefVarUl

// A dictionary of the list replaced by another: the one the frame's
// `dictId` names is disposed of and a new one opened over the bytes
// given, which go in the frame's `dict` slot.  ==> whether one was.
Boolean	ReplaceDictionary(RefArg frame, RefArg binary);		// ROM 0x0013ec74 ReplaceDictionary__F6RefVarT1
Boolean	ReplaceDictionary(RefArg frame, ULong romDictID, const char* data, ULong size);	// ROM 0x0013f14c ReplaceDictionary__F6RefVarUlPcT2
// ... the one a locale bundle carries in place of this dictionary, if it
// carries one (the frame's `localDictSlot` names the bundle's slot).
Boolean	ReplaceLocalDictionary(RefArg localeBundle, RefArg frame);	// ROM 0x0013e384 ReplaceLocalDictionary__F6RefVarT1
// ... and every dictionary of the list asked that question at once,
// which is how a change of locale changes the words the machine reads.
void	ReadDictPrefs(void);								// ROM 0x0013e4a4 ReadDictPrefs__Fv

// The chains: built for one lookup and thrown away again.
void	AddToChain(TDictChain** chains, dictListEntry* entry);	// ROM 0x0013d628 AddToChain__FPP10TDictChainP13dictListEntry
void	BuildChains(TDictChain** chains, RefArg config);	// ROM 0x0013d808 BuildChains__FPP10TDictChainRC6RefVar
void	BuildChains(TDictChain** chains);					// ROM 0x0013d9dc BuildChains__FPP10TDictChain - from the view the caret is in
void	CompactChains(TDictChain** chains);					// ROM 0x0013db74 CompactChains__FPP10TDictChain
void	DoneChains(TDictChain** chains);					// ROM 0x0013dbac DoneChains__FPP10TDictChain

// The lookups.  ==> the id of the dictionary the word was found in, or
// -1; `attribute` comes back with whatever was stored beside the word.
long	LookupWordInChain(const UByte* word, TDictChain* chain, ULong* attribute);	// ROM 0x0013f430 LookupWordInChain__FPUcP10TDictChainPUl
long	LookupWord(const UniChar* word, ULong* attribute);	// ROM 0x0013f4f4 LookupWord__FPUsPUl
// The same, trying the word's capitalisations in turn; `variant` comes
// back with the one that was found.  `attribute` carries the flags in.
long	LookupWordOrVariant(const UniChar* word, ULong* attribute, UniChar* variant);	// ROM 0x0013f570 LookupWordOrVariant__FPUsPUlT1
// The index'th capitalisation of a word.  ==> whether there was one.
Boolean	BuildCaseVariant(const UniChar* word, ULong flags, ULong index, UniChar* out);	// ROM 0x0013f2fc BuildCaseVariant__FPUsUlT2T1

void	RegisterDictionaryNatives(void);			// result, DumpDict (Dictionaries.cpp)

#endif	/* __DICTIONARIES_H */
