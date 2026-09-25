/*
	File:		recognition/LELang.h

	Contains:	The lexicon as the search walks it.

				A kind of word in a grammar names a **lexicon**, and
				`RosettaSetArea` replaces that name with the data
				itself out of `gROMDictionaryData`.  This is how the
				search reads one: given a position in the lexicon it
				answers every character that may come next, and where
				each of them leads.

				The lexicons come in two shapes, which the byte at
				offset 5 says apart in its bottom three bits:

				* **1** - a run of characters.  The node names an
				  offset where a string of the letters that may follow
				  is kept, and every one of them leads to the *same*
				  next node; a flag in the node says whether the run
				  ends there or another follows it.
				* **3 or 7** - a chain of nodes, one per character.
				  Each node holds a character and a variable-length
				  offset to the next, one to four bytes wide depending
				  on two bits of its flags (`AckNodeSizeTab`).

				Both are answered the same way: `LELangNodeNumOut`
				fills `LELTranCache` with one word per way out - the
				node it leads to in the low three bytes, and for a run
				the character in the top byte - and says how many there
				are.  Because it is one global cache and one global
				"which node was this for", only one walk may be in
				hand at a time, which is all the search ever needs.

	Reconstructed from the MP2x00 US ROM (0x000ffd98-0x000fff5c); each
	function cites its origin.
*/

#ifndef __LELANG_H
#define __LELANG_H

#ifndef __NEWTONTYPES_H
#include "NewtonTypes.h"
#endif

// A lexicon's bytes.  Only two things about the header are known: the
// node area starts at offset 4, and the byte at offset 5 carries the
// shape in its bottom three bits and, in its top nibble, how much wider
// a node gets when it is marked.
const long	kLELangNodes	= 4;
const long	kLELangFormat	= 5;
const UByte	kLELangRun		= 1;
const UByte	kLELangChainA	= 3;
const UByte	kLELangChainB	= 7;

// How many bytes of offset a chained node carries, by the top two bits
// of its flags.
extern const UByte	AckNodeSizeTab[4];					// ROM 0x00371e94 AckNodeSizeTab

// Where `LELangNodeNumOut` leaves its answer.  One word per way out:
// the node in the low three bytes and, for a run lexicon, the character
// in the top byte.
extern ULong*		LELTranCache;						// ROM 0x0c101090 LELTranCache
extern long			LELTranCacheSize;					// ROM 0x0c101094 LELTranCacheSize
// Which walk it holds, so a caller can tell whether it is still the
// one it asked for.
extern ULong		LELTranCacheNode;					// ROM 0x0c101098 LELTranCacheNode
extern const void*	LELTranCacheLang;					// ROM 0x0c10109c LELTranCacheLang

// How many characters may follow this position in the lexicon, with
// the ways out left in `LELTranCache`.  A node of 0x80000000 is the
// end of a word and has no ways out.
long	LELangNodeNumOut(const void* lang, ULong node);	// ROM 0x000ffd98 LELangNodeNumOut

#endif	/* __LELANG_H */
