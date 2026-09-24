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

/*------------------------------------------------------------------------------
	T h e   R O M ' s   o w n   l e x i c o n s
------------------------------------------------------------------------------*/

// A small AL dictionary laid out by hand from the node format, holding
// "at", "an" and "be".  The bytes are written here rather than built by
// a writer of ours, so that what is being checked is the reader against
// the format as it is written down.
//
//   header   'a', then the kind (1) with the attribute size (1) above it
//   a node   the offset of its character set (two bytes, high first),
//            its flags (1 an attribute, 2 no children, 4 the last of its
//            siblings), the offset of its first child (two bytes, absent
//            when it has no children), then the attribute
static const UByte kSmallAL[] = {
	'a', 0x11,							// +0   the header
	0, 26,   0x00, 0, 14,   1,			// +2   'a', has children -> 14, not last
	0, 28,   0x04, 0, 22,   2,			// +8   'b', has children -> 22, last
	0, 30,   0x03,          3,			// +14  't', a leaf with an attribute
	0, 32,   0x07,          4,			// +18  'n', ... and the last of them
	0, 34,   0x07,          5,			// +22  'e', ... under the 'b'
	'a', 0,								// +26  the character sets
	'b', 0,								// +28
	't', 0,								// +30
	'n', 0,								// +32
	'e', 0								// +34
};


// The word looked up, as `VerifyString` would: the characters in the
// block's buffer, the index of the last one to match, and the walk
// started at the root.
static long
LookUpAL(AirusAParmBlock* parms, const char* word)
{
	Astrcpy((char*) parms->fWord, word);
	parms->fIndex = Astrlen(word) - 1;
	parms->fNode = 0;
	parms->fAttribute = 0;
	AL_Verify(parms);
	return parms->fResult;
}


static void
TestALLexicon(void)
{
	UByte bytes[sizeof(kSmallAL)];
	memcpy(bytes, kSmallAL, sizeof(kSmallAL));
	Ptr data = (Ptr) bytes;
	char buffer[64];

	AirusAParmBlock block;
	memset(&block, 0, sizeof(block));
	Handle fake = (Handle) &data;		// the bytes, as a Handle over them
	block.fDataHandle = fake;
	block.fData = data;
	block.fDataEnd = data + sizeof(kSmallAL);
	block.fSize = (long) sizeof(kSmallAL);
	block.fWord = (UByte*) buffer;
	block.fAttributeSize = 1;

	// a whole word, with the attribute that was stored with it
	// ... and the same dictionary opened the way a ROM one is: a block
	// built over bytes that already exist, read where they lie
	{
		Handle opened = BuildDictionaryFromPtr(bytes, (Size) sizeof(kSmallAL));
		EXPECT(opened != nil && airusResult == 0);
		if (opened != nil)
		{
			AirusAParmBlock* p = (AirusAParmBlock*) *opened;
			EXPECT(p->fAttributeSize == 1 && p->fSize == (long) sizeof(kSmallAL));
			EXPECT(p->fDataEnd - p->fData == (long) sizeof(kSmallAL));
			EXPECT(p->fCurrent == opened && p->fNext == nil);
			EXPECT(LookUpAL(p, "an") == kAirusLeaf && p->fAttribute == 4);
			EXPECT(LookUpAL(p, "zz") == kAirusNoMatch);
		}
		// bytes that are not a dictionary are refused
		UByte junk[4] = { 1, 2, 3, 4 };
		EXPECT(BuildDictionaryFromPtr(junk, 4) == nil);
		EXPECT(airusResult == kAirusBadDictionary);
	}

	EXPECT(LookUpAL(&block, "at") == kAirusLeaf);
	EXPECT(block.fAttribute == 3);
	EXPECT(block.fIndex == 2);
	EXPECT(LookUpAL(&block, "an") == kAirusLeaf);
	EXPECT(block.fAttribute == 4);
	EXPECT(LookUpAL(&block, "be") == kAirusLeaf);
	EXPECT(block.fAttribute == 5);

	// the beginning of two words is a prefix and nothing more, and there
	// is no one character it must go on with
	EXPECT(LookUpAL(&block, "a") == kAirusPrefix);
	EXPECT(block.fSymbol == 0xffffffff);

	// the beginning of one word says which character that is, which is
	// what the corrector offers
	EXPECT(LookUpAL(&block, "b") == kAirusPrefix);
	EXPECT(block.fSymbol == (ULong) 'e');

	// nothing begins that way
	EXPECT(LookUpAL(&block, "ax") == kAirusNoMatch);
	EXPECT(LookUpAL(&block, "c") == kAirusNoMatch);
	EXPECT(LookUpAL(&block, "bee") == kAirusNoMatch);

	// an empty dictionary answers nothing to everything
	AirusAParmBlock empty;
	memset(&empty, 0, sizeof(empty));
	UByte header[2] = { 'a', 0x11 };
	Ptr headerData = (Ptr) header;
	empty.fDataHandle = (Handle) &headerData;
	empty.fData = headerData;
	empty.fDataEnd = headerData + 2;
	empty.fWord = (UByte*) buffer;
	empty.fAttributeSize = 1;
	EXPECT(LookUpAL(&empty, "at") == kAirusNoMatch);

	// a character set is each character once
	char set[16];
	Astrcpy(set, "aabbcaz");
	AL_FilterString(set);
	EXPECT(Astrlen(set) == 4 && set[0] == 'a' && set[1] == 'b'
		   && set[2] == 'c' && set[3] == 'z');
	EXPECT(Astrchr(set, 'c') == set + 2 && Astrchr(set, 'q') == nil);
}


// The same again with sixteen-bit characters: "at" and "an", the
// characters and their set terminators two bytes each.
static const UByte kSmallAL16[] = {
	'a', 0x12,							// +0   the header: kind 2, attribute size 1
	0, 16,   0x04, 0, 8,    1,			// +2   'a', has children -> 8, last
	0, 20,   0x03,          2,			// +8   't', a leaf, not the last
	0, 24,   0x07,          3,			// +12  'n', a leaf, the last
	0, 0x61, 0, 0,						// +16  the character sets
	0, 0x74, 0, 0,						// +20
	0, 0x6e, 0, 0						// +24
};


static long
LookUpAL16(AirusAParmBlock* parms, const char* word)
{
	UniChar* chars = (UniChar*) parms->fWord;
	long i = 0;
	for (; word[i] != 0; i++)
		chars[i] = (UniChar) (UByte) word[i];
	chars[i] = 0;
	parms->fIndex = i - 1;
	parms->fNode = 0;
	parms->fAttribute = 0;
	AL16_Verify(parms);
	return parms->fResult;
}


static void
TestAL16Lexicon(void)
{
	UByte bytes[sizeof(kSmallAL16)];
	memcpy(bytes, kSmallAL16, sizeof(kSmallAL16));
	Ptr data = (Ptr) bytes;
	UniChar buffer[64];

	AirusAParmBlock block;
	memset(&block, 0, sizeof(block));
	Handle fake = (Handle) &data;
	block.fDataHandle = fake;
	block.fData = data;
	block.fDataEnd = data + sizeof(kSmallAL16);
	block.fSize = (long) sizeof(kSmallAL16);
	block.fWord = (UByte*) buffer;
	block.fAttributeSize = 1;

	EXPECT(LookUpAL16(&block, "at") == kAirusLeaf);
	EXPECT(block.fAttribute == 2);
	EXPECT(LookUpAL16(&block, "an") == kAirusLeaf);
	EXPECT(block.fAttribute == 3);
	EXPECT(LookUpAL16(&block, "ax") == kAirusNoMatch);
	EXPECT(LookUpAL16(&block, "b") == kAirusNoMatch);

	// "a" is the beginning of two words, so there is no one character it
	// must go on with - and here is the ROM bug this walker has and the
	// eight-bit one does not.  Its tail tests the "last sibling" flag the
	// wrong way round, so it stops at the *first* child and hands that
	// character back as though it were the only one.  The eight-bit
	// walker, given the same shape, answers "no single character".
	EXPECT(LookUpAL16(&block, "a") == kAirusPrefix);
	EXPECT(block.fSymbol == (ULong) 't');		// (and 'n' was just as possible)
}


// the words a walk reaches, in the order it reaches them
static char	gWalked[256];
static long	gWalkedCount;
static long	gWalkStopAfter;

static Boolean
Collect(UByte* word, ULong attribute, UByte terminal, long count, void* context)
{
	if (gWalkedCount != 0)
		strcat(gWalked, " ");
	strcat(gWalked, (const char*) word);
	char tail[32];
	sprintf(tail, "/%lu/%c/%ld", (unsigned long) attribute,
			terminal == 0 ? (char) '-' : (char) terminal, count);
	strcat(gWalked, tail);
	gWalkedCount++;
	EXPECT(context == (void*) &gWalkedCount);
	return gWalkStopAfter == 0 || gWalkedCount < gWalkStopAfter;
}


static long
Walk(Handle dictionary, const char* prefix)
{
	gWalked[0] = 0;
	gWalkedCount = 0;
	return WalkDictionary(dictionary, (const UByte*) prefix, Collect, &gWalkedCount);
}


int
main()
{
	InitHostStandaloneHeap();

	TestALLexicon();
	TestAL16Lexicon();

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

		// ---- words taken out again ----
		// "and" is a leaf, and "ant" goes on from the same "an", so
		// deleting it takes its node out and leaves everything else
		strcpy((char*) word, "and");
		DeleteWord(words, word);
		EXPECT(airusResult == 0);
		CheckDictPtrs(d);
		EXPECT(Lookup(d, "and") == kAirusNoMatch);
		EXPECT(Lookup(d, "ant") == kAirusLeaf && d->fAttribute == 12);
		EXPECT(Lookup(d, "an") == kAirusPrefixWithAttr && d->fAttribute == 8);
		EXPECT(Lookup(d, "at") == kAirusLeaf && d->fAttribute == 7);
		EXPECT(Lookup(d, "be") == kAirusLeaf && d->fAttribute == 10);

		// "an" is a word that other words go on from: it loses its
		// attribute and keeps its place
		strcpy((char*) word, "an");
		DeleteWord(words, word);
		EXPECT(airusResult == 0);
		CheckDictPtrs(d);
		EXPECT(Lookup(d, "an") == kAirusPrefix);
		EXPECT(Lookup(d, "ant") == kAirusLeaf && d->fAttribute == 12);

		// the last word of its row goes, and its row with it
		strcpy((char*) word, "be");
		DeleteWord(words, word);
		EXPECT(airusResult == 0);
		CheckDictPtrs(d);
		EXPECT(Lookup(d, "be") == kAirusNoMatch);
		EXPECT(Lookup(d, "b") == kAirusNoMatch);
		EXPECT(Lookup(d, "at") == kAirusLeaf && d->fAttribute == 7);
		EXPECT(Lookup(d, "ant") == kAirusLeaf && d->fAttribute == 12);
		EXPECT(Lookup(d, "a") == kAirusPrefixWithAttr && d->fAttribute == 11);

		// a word put back in afterwards is found again, so the trie is
		// still whole
		strcpy((char*) word, "and");
		AddWord(words, 0, word, 21);
		EXPECT(airusResult == 0);
		CheckDictPtrs(d);
		EXPECT(Lookup(d, "and") == kAirusLeaf && d->fAttribute == 21);
		EXPECT(Lookup(d, "ant") == kAirusLeaf && d->fAttribute == 12);
		EXPECT(Lookup(d, "at") == kAirusLeaf && d->fAttribute == 7);
		EXPECT(Lookup(d, "a") == kAirusPrefixWithAttr && d->fAttribute == 11);

		// an empty word is refused, and one that was never there is
		// reported as gone (the ROM bug written down in
		// AEnum_DeleteWord)
		word[0] = 0;
		DeleteWord(words, word);
		EXPECT(airusResult == kAirusEmptyWord);
		strcpy((char*) word, "zoo");
		DeleteWord(words, word);
		EXPECT(airusResult == kAirusAlreadyThere);	// ... "not there"
		strcpy((char*) word, "an");					// a path that is not a word
		DeleteWord(words, word);
		EXPECT(airusResult == 0);					// ... and the bug says it went

		// ---- what may come next, and walking the whole thing ----
		// The dictionary now holds "a", "and", "ant", "at" (and "an" is
		// a path that is not a word, after the deletions above).
		{
			// the characters that may follow a prefix, in order
			UByte set[64];
			AirusAParmBlock* p = (AirusAParmBlock*) *words;
			CheckDictPtrs(p);
			p->fWord = set;
			p->fNode = 0;						// the top row
			AEnum_NextSet(p);
			EXPECT(p->fResult == 0 && strcmp((const char*) set, "a") == 0);

			// under "a": "n" and "t"
			strcpy((char*) word, "a");
			p->fWord = word;
			p->fIndex = 0;
			p->fNode = 0;
			AEnum_Verify(p);
			long aNode = p->fNode;
			p->fWord = set;
			p->fNode = aNode;
			AEnum_NextSet(p);
			EXPECT(p->fResult == 0 && strcmp((const char*) set, "nt") == 0);

			// a node nothing goes on from has no set at all
			strcpy((char*) word, "at");
			p->fWord = word;
			p->fIndex = 1;
			p->fNode = 0;
			AEnum_Verify(p);
			long atNode = p->fNode;
			p->fWord = set;
			p->fNode = atNode;
			AEnum_NextSet(p);
			EXPECT(p->fResult == 1 && set[0] == 0);

			// the whole dictionary, in order, with what is stored beside
			// each word, the one character that could follow it, and the
			// running count
			gWalkStopAfter = 0;
			EXPECT(Walk(words, "") == 4);
			EXPECT(strcmp(gWalked, "a/11/-/1 and/21/-/2 ant/12/-/3 at/7/-/4") == 0);

			// ... and only the words under a prefix
			EXPECT(Walk(words, "an") == 2);
			EXPECT(strcmp(gWalked, "and/21/-/1 ant/12/-/2") == 0);
			EXPECT(Walk(words, "at") == 1);
			EXPECT(strcmp(gWalked, "at/7/-/1") == 0);
			EXPECT(Walk(words, "z") == 0 && gWalked[0] == 0);

			// a callback that says to stop is not called again
			gWalkStopAfter = 2;
			EXPECT(Walk(words, "") == 2);
			EXPECT(strcmp(gWalked, "a/11/-/1 and/21/-/2") == 0);
			gWalkStopAfter = 0;

			// no callback at all: the walk still counts
			EXPECT(WalkDictionary(words, (const UByte*) "", nil, nil) == 4);
			EXPECT(WalkDictionary(words, nil, nil, nil) == 4);

			// a prefix taken out takes everything under it
			strcpy((char*) word, "an");
			DeletePrefix(words, word);
			EXPECT(airusResult == 0);
			CheckDictPtrs(d);
			EXPECT(Walk(words, "") == 2);
			EXPECT(strcmp(gWalked, "a/11/-/1 at/7/-/2") == 0);
			EXPECT(Lookup(d, "and") == kAirusNoMatch);
			EXPECT(Lookup(d, "ant") == kAirusNoMatch);
			EXPECT(Lookup(d, "at") == kAirusLeaf && d->fAttribute == 7);
		}

		// the chain: there is only one dictionary here
		EXPECT(PositionToHandle(words, 0) == words && airusResult == 0);
		EXPECT(PositionToHandle(words, 1) == nil && airusResult == kAirusNoSuchDictionary);

		// an attribute changed where it lies: nothing moves, and the
		// word is still there with its new one
		EXPECT(AttributeLength(words) == 1);
		long size = ((AirusAParmBlock*) *words)->fDataEnd - ((AirusAParmBlock*) *words)->fData;
		strcpy((char*) word, "at");
		ChangeAttribute(words, word, 77);
		EXPECT(airusResult == 0);
		d = (AirusAParmBlock*) *words;
		CheckDictPtrs(d);
		EXPECT(d->fDataEnd - d->fData == size);
		EXPECT(Lookup(d, "at") == kAirusLeaf && d->fAttribute == 77);
		EXPECT(Lookup(d, "a") == kAirusPrefixWithAttr && d->fAttribute == 11);

		// a word that is not there
		strcpy((char*) word, "zebra");
		ChangeAttribute(words, word, 1);
		EXPECT(airusResult == kAirusNotAWord);
		// ... and one that is only a prefix of others carries no
		// attribute of its own to change
		strcpy((char*) word, "a");
		ChangeAttribute(words, word, 5);
		EXPECT(airusResult == 0);			// "a" is a word here
	}

	// a dictionary that carries no attributes says so
	{
		Handle plain = NewDictionary(kAirusKindEnumRAM | kAirusLockedBit, 0);
		UByte word[32];
		strcpy((char*) word, "one");
		AddWord(plain, 0, word, 0);
		EXPECT(airusResult == 0);
		EXPECT(AttributeLength(plain) == 0);
		ChangeAttribute(plain, word, 3);
		EXPECT(airusResult == -7);
		DisposDictionary(&plain);
	}

	// ... but the oldest writable kind carries one without declaring it
	{
		Handle old = NewDictionary(kAirusKindEnum, 0);
		EXPECT(AttributeLength(old) == 1);
		DisposDictionary(&old);
	}

	printf("test_Airus: %d failures\n", failures);
	return failures != 0;
}
