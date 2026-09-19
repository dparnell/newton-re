// Airus test (recognition/Airus.h): the dictionary container - one made
// and what its first two bytes say about it, the block's pointers kept
// right when the Handle moves, the data grown, slid and read.  The
// walkers themselves are NOT YET, so the selector call is only checked
// for the two that clear the error.
#include "Airus.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)



// the word looked up, as a caller of the engine would
static long
Lookup(AirusAParmBlock* d, const char* text)
{
	static UByte buffer[64];
	strcpy((char*) buffer, text);
	d->fWord = buffer;
	d->fNode = 0;
	d->fIndex = (long) strlen(text) - 1;
	return AE8_Verify(d);
}

int
main()
{
	InitHostStandaloneHeap();

	// an empty dictionary of the kind the machine writes into
	// (the type InitDictionaries asks for: the walkers the machine writes
	// with, and the Handle locked while they run)
	Handle dictionary = NewDictionary(kAirusKindEnumRAM | kAirusLockedBit, 1);
	EXPECT(dictionary != nil && airusResult == 0);
	AirusAParmBlock* parms = (AirusAParmBlock*) *dictionary;
	EXPECT(parms->fCurrent == dictionary && parms->fAttributeSize == 1 && parms->fGrowBy == 100);
	EXPECT(parms->fSize == 2 && parms->fDataEnd - parms->fData == 2);
	EXPECT((UByte) parms->fData[0] == 'a');
	// the kind, with the size of each word's attribute above it
	EXPECT((UByte) parms->fData[1] == (kAirusKindEnumRAM | kAirusLockedBit | (1 << 4)));
	// and that byte asks for the Handle to be locked while a walker runs
	EXPECT(((UByte) parms->fData[1] & kAirusLockedBit) != 0);

	// the pointers follow the Handle, and the block becomes the one the
	// walkers work on
	AE_Parms = nil;
	CheckDictPtrs(parms);
	EXPECT(AE_Parms == parms);
	long used = parms->fDataEnd - parms->fData;
	Ptr was = parms->fData;
	parms->fData = was - 100;						// as if it had moved
	parms->fDataEnd = parms->fDataEnd - 100;
	CheckDictPtrs(parms);
	EXPECT(parms->fData == was && parms->fDataEnd - parms->fData == used);

	// grown a hundred at a time until there is room
	EXPECT(ExpandDict(50) == 0 && parms->fSize == 102 && parms->fResult == 0);
	EXPECT(ExpandDict(300) == 0 && parms->fSize >= 302);
	EXPECT(parms->fDataEnd - parms->fData == 2);	// growing does not fill it

	// the bytes slid apart and back together
	parms->fData[0] = 'a';
	parms->fData[1] = (char) 0x1f;
	SlideDown(2, 3);								// three bytes of room at the end
	EXPECT(parms->fDataEnd - parms->fData == 5);
	parms->fData[2] = (char) 0x12;
	parms->fData[3] = (char) 0x34;
	parms->fData[4] = (char) 0x56;
	SlideDown(2, 1);								// one more, before them
	EXPECT(parms->fDataEnd - parms->fData == 6);
	EXPECT((UByte) parms->fData[3] == 0x12 && (UByte) parms->fData[5] == 0x56);
	SlideUp(3, 1);									// and taken out again
	EXPECT(parms->fDataEnd - parms->fData == 5);
	EXPECT((UByte) parms->fData[2] == 0x12 && (UByte) parms->fData[4] == 0x56);

	// read big-endian, as it lies
	EXPECT(GetDictBytes(2, 1) == 0x12);
	EXPECT(GetDictBytes(2, 2) == 0x1234);
	EXPECT(GetDictBytes(2, 3) == 0x123456);
	EXPECT(GetDictBytes(2, 0) == 0);

	// the two selectors that only say nothing has gone wrong
	parms->fResult = 7;
	CallAirusA(dictionary, kAirusStartA);
	EXPECT(((AirusAParmBlock*) *dictionary)->fResult == 0);
	((AirusAParmBlock*) *dictionary)->fResult = 7;
	CallAirusANoLock(dictionary, kAirusExitA);
	EXPECT(((AirusAParmBlock*) *dictionary)->fResult == 0);

	// a dictionary of two words, written out by hand, which is what the
	// node layout looks like:
	//
	//   2: 'a' 0x00      the only letter at the top, with children
	//   4: 'n' 0x60      a leaf, with a sibling 0 bytes further on
	//   6: 't' 0x20      a leaf, with no sibling
	{
		Handle two = NewDictionary(kAirusKindEnumRAM | kAirusLockedBit, 0);
		EXPECT(two != nil);
		AirusAParmBlock* d = (AirusAParmBlock*) *two;
		CheckDictPtrs(d);
		EXPECT(ExpandDict(8) == 0);
		static const UByte kTrie[6] = { 'a', 0x00, 'n', 0x60, 't', 0x20 };
		SlideDown(2, sizeof(kTrie));
		memcpy(d->fData + 2, kTrie, sizeof(kTrie));
		EXPECT(d->fDataEnd - d->fData == 8);

		// the node readers, on the root
		EXPECT(AirusCharSize() == 1);
		EXPECT(RPByteSize(2) == 0 && RPByteSize(4) == 0);
		EXPECT(SkipNode(2) == 4 && FollowLeft(2) == 4);
		EXPECT(GetSymbol(2) == 'a' && GetSymbol(4) == 'n' && GetSymbol(6) == 't');

		// "an" and "at" are both whole words, and end at a node with no
		// children
		d->fWord[0] = 'a';
		d->fWord[1] = 'n';
		d->fNode = 0;
		d->fIndex = 1;					// the last character to match
		EXPECT(AE8_Verify(d) == kAirusLeaf && d->fNode == 4 && d->fIndex == 2);
		d->fWord[1] = 't';
		d->fNode = 0;
		d->fIndex = 1;
		EXPECT(AE8_Verify(d) == kAirusLeaf && d->fNode == 6);	// found along the siblings

		// "a" leads on rather than ending
		d->fNode = 0;
		d->fIndex = 0;
		EXPECT(AE8_Verify(d) == kAirusPrefix && d->fNode == 2);
		// and there is more than one way on, so no single character is named
		EXPECT(d->fSymbol == (ULong) -1);

		// "ax" and "b" are not in it
		d->fWord[1] = 'x';
		d->fNode = 0;
		d->fIndex = 1;
		EXPECT(AE8_Verify(d) == kAirusNoMatch);
		d->fWord[0] = 'b';
		d->fNode = 0;
		d->fIndex = 0;
		EXPECT(AE8_Verify(d) == kAirusNoMatch);

		// one character at a time comes to the same place
		d->fWord[0] = 'a';
		d->fWord[1] = 'n';
		d->fNode = 0;
		d->fIndex = 0;
		EXPECT(AE8_Verify(d) == kAirusPrefix && d->fNode == 2 && d->fIndex == 1);
		d->fIndex = 1;
		EXPECT(AE8_Verify(d) == kAirusLeaf && d->fNode == 4);
	}

	// a dictionary with nothing in it finds nothing
	{
		Handle empty = NewDictionary(kAirusKindEnumRAM | kAirusLockedBit, 0);
		AirusAParmBlock* d = (AirusAParmBlock*) *empty;
		CheckDictPtrs(d);
		d->fWord[0] = 'a';
		d->fNode = 0;
		d->fIndex = 0;
		EXPECT(AE8_Verify(d) == kAirusNoMatch);
	}

	// words put in, and found again: the whole of the engine end to end
	{
		Handle words = NewDictionary(kAirusKindEnumRAM | kAirusLockedBit, 1);
		EXPECT(words != nil);
		UByte word[32];
		static const struct { const char* fText; long fAttribute; } kWords[] = {
			{ "at", 7 }, { "an", 8 }, { "and", 9 },
			{ "be", 10 }, { "a", 11 }, { "ant", 12 }, { nil, 0 }
		};
		for (long i = 0; kWords[i].fText != nil; i++)
		{
			strcpy((char*) word, kWords[i].fText);
			AddWord(words, 0, word, (ULong) kWords[i].fAttribute);
			EXPECT(airusResult == 0);
		}
		AirusAParmBlock* d = (AirusAParmBlock*) *words;
		CheckDictPtrs(d);

		// each of them is there, with the attribute it went in with
		for (long i = 0; kWords[i].fText != nil; i++)
		{
			long found = Lookup(d, kWords[i].fText);
			EXPECT(found == kAirusLeaf || found == kAirusPrefixWithAttr);
			EXPECT((long) d->fAttribute == kWords[i].fAttribute);
		}
		// the ones that end where another word goes on lead on; the rest do not
		EXPECT(Lookup(d, "and") == kAirusLeaf);
		EXPECT(Lookup(d, "an") == kAirusPrefixWithAttr);	// "and" and "ant" go on from it
		EXPECT(Lookup(d, "be") == kAirusLeaf);

		// and what is not in it is not found
		EXPECT(Lookup(d, "ax") == kAirusNoMatch);
		EXPECT(Lookup(d, "b") == kAirusPrefix);			// "be" begins that way, but "b" is not a word
		EXPECT(Lookup(d, "bee") == kAirusNoMatch);
		EXPECT(Lookup(d, "c") == kAirusNoMatch);
		EXPECT(Lookup(d, "ands") == kAirusNoMatch);

		// a word that is already there keeps the attribute it had
		strcpy((char*) word, "and");
		AddWord(words, 0, word, 99);
		EXPECT(airusResult == kAirusAlreadyThere);
		CheckDictPtrs(d);
		EXPECT(Lookup(d, "and") == kAirusLeaf && d->fAttribute == 9);

		// an empty word is refused
		word[0] = 0;
		AddWord(words, 0, word, 1);
		EXPECT(airusResult == kAirusEmptyWord);

		// the way a caller really uses it: the chain put back to the start,
		// then a whole word at once
		{
			void* terminal = (void*) 1;
			ULong* attribute = (ULong*) 1;
			VerifyStart(words);
			EXPECT(airusResult == 0);
			VerifyString(words, "and", &terminal, &attribute, nil);
			EXPECT(airusResult == kAirusIsWord);		// a word, with nothing going on from it
			EXPECT(attribute != nil && *attribute == 9);
			EXPECT(terminal == nil);					// so there is no character after it

			VerifyStart(words);
			VerifyString(words, "an", &terminal, &attribute, nil);
			EXPECT(airusResult == kAirusIsPrefixAndWord);	// a word, and the start of others
			EXPECT(attribute != nil && *attribute == 8);

			VerifyStart(words);
			VerifyString(words, "b", &terminal, &attribute, nil);
			EXPECT(airusResult == kAirusIsPrefix && attribute == nil);
			// only "be" goes on from it, so the next character is named
			EXPECT(terminal != nil && *(UByte*) terminal == 'e');

			VerifyStart(words);
			VerifyString(words, "zoo", &terminal, &attribute, nil);
			EXPECT(airusResult == kAirusNotAWord && attribute == nil);
		}

		// the chain: there is only one dictionary here
		EXPECT(PositionToHandle(words, 0) == words && airusResult == 0);
		EXPECT(PositionToHandle(words, 1) == nil && airusResult == kAirusNoSuchDictionary);
	}

	printf("test_Airus: %d failures\n", failures);
	return failures != 0;
}
