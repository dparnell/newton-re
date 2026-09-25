/*
	File:		recognition/LELang.cpp

	Contains:	The lexicon walk - see LELang.h.

	Reconstructed from the MP2x00 US ROM; each function cites its
	origin.
*/

#include "LELang.h"
#include "RosStrokes.h"			// kRosettaMemoryTag
#include "NewtonMemory.h"
#include "NewtonExceptions.h"


// ROM 0x0c101090 LELTranCache
ULong*		LELTranCache = nil;
// ROM 0x0c101094 LELTranCacheSize
long		LELTranCacheSize = 0;
// ROM 0x0c101098 LELTranCacheNode
ULong		LELTranCacheNode = 0;
// ROM 0x0c10109c LELTranCacheLang
const void*	LELTranCacheLang = nil;

// How big the cache is: 261 words, which is one per character code and
// a few over.
const long	kLELTranCacheSize	= 0x414;

extern const ExceptionName exOutOfStack;


// ROM 0x000ffd98 LELangNodeNumOut
// Every character that may follow this position in the lexicon, and
// where each of them leads.
//
// A **run** lexicon keeps the letters that may follow together as a
// string somewhere else in the data, and they all lead to the same next
// node; the run is walked until a node says it is the last.  Nothing is
// searched: the answer is already written down.
//
// A **chained** lexicon has one node per character instead, each
// holding an offset to the next.  The offset is one to four bytes wide
// depending on two bits of the node's flags - `AckNodeSizeTab` says
// how many - with the low nibble of the flags as its top four bits.  So
// a short hop costs one byte and a long one costs four, and a lexicon
// of fifty thousand words is not paying four bytes a letter.
long
LELangNodeNumOut(const void* lang, ULong node)
{
	if (LELTranCache == nil)
	{
		// DEVIATION: the ROM's cache is 0x414 bytes of four-byte words;
		// a host ULong is wider, so it is sized by the entry
		LELTranCache = (ULong*) NewNamedPtr((kLELTranCacheSize / 4) * (long) sizeof(ULong),
								kRosettaMemoryTag);
		if (LELTranCache == nil)
			Throw(exOutOfStack, (void*) "", nil);
	}
	// the end of a word: nothing follows it
	if (node == 0x80000000)
		return 0;
	node &= 0x7fffffff;

	const UByte* bytes = (const UByte*) lang;
	const UByte* nodes = bytes + kLELangNodes;
	UByte shape = (UByte) (bytes[kLELangFormat] & 7);

	if (shape == kLELangRun)
	{
		LELTranCacheSize = 0;
		LELTranCacheNode = node;
		LELTranCacheLang = lang;
		while (node != 0)
		{
			// the letters that may follow, as a string
			const char* run = (const char*) (nodes
								+ (ULong) nodes[node] * 0x100
								+ (ULong) nodes[node + 1]);
			for (long i = 0; run[i] != 0; i++)
			{
				LELTranCache[LELTranCacheSize] = node
							| ((ULong) (UByte) run[i] << 24);
				LELTranCacheSize++;
			}
			UByte flags = nodes[node + 2];
			if ((flags & 4) != 0)
				node = 0;			// that was the last run
			else
				node += (ULong) (bytes[kLELangFormat] >> 4)
					+ (((flags & 2) != 0) ? 3 : 5);
		}
		return LELTranCacheSize;
	}

	if (shape != kLELangChainA && shape != kLELangChainB)
		return 0;

	UByte header = bytes[kLELangFormat];
	long at = 0;
	LELTranCacheNode = node;
	LELTranCacheLang = lang;
	for (;;)
	{
		LELTranCacheSize = at + 1;
		LELTranCache[at] = node;

		const UByte* p = nodes + node + 1;
		UByte flags = *p;
		if ((flags & 0xc0) == 0)
			break;					// nothing follows this one

		ULong width = (ULong) AckNodeSizeTab[(flags & 0xc0) >> 6];
		long next = (long) node + (long) width + 2;
		if ((flags & 0x10) != 0)
			next += (long) (header >> 4);

		// the offset: the flags' low nibble on top of `width` bytes
		ULong delta = (ULong) (flags & 0x0f);
		for (ULong k = width; k != 0; k = (k - 1) & 0xff)
		{
			p++;
			delta = (ULong) *p | (delta << 8);
		}
		node = (ULong) next + delta;
		at = LELTranCacheSize;
	}
	return LELTranCacheSize;
}
