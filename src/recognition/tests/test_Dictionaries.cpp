// The dictionaries the machine knows, the chains a lookup walks, and the
// lookup itself (recognition/Dictionaries.h).
//
// The chains are checked first over a list made by hand - two frames and
// two small AL dictionaries laid out the way test_Airus lays them out -
// so that what a chain does is visible: which chain a dictionary goes
// into, the links between them, what BuildChains picks out of a
// recognition configuration, and whether a word is found.  Then
// InitDictionaries is run for real and the same questions are put to the
// twenty-seven dictionaries the ROM carries.
//
// The ROM's objects are imported for its list of descriptors and for
// `rcbuildchains`, the starting configuration LookupWord falls back on
// when nothing is being written on; its input mask is 0x1000, which is
// why the hand-made general lexicon below has that as its domain.

#include "Dictionaries.h"
#include "Airus.h"
#include "Words.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "ROMImport.h"
#include "RSSymbols.h"
#include "Unicode.h"
#include "Locale.h"
#include "Learning.h"
#include "View.h"
#include "RootView.h"
#include "Ports.h"
#include "NativeFunctions.h"
#include "ROMConstants.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

// the smallest screen the view system will start over
const long	kTestWidth	= 64;
const long	kTestHeight	= 32;
static PixelMap	gTestMap;
static GrafPort	gTestPort;
static unsigned char	gTestBits[(kTestWidth / 8) * kTestHeight];

// a NewtonScript string compared with a C one
static Boolean
WordIs(Ref word, const char* text)
{
	if (!IsString(RefVar(word)))
		return false;
	UByte bytes[64];
	ConvertFromUnicode(GetCString(RefVar(word)), bytes, kMacRomanEncoding, 0x3f);
	return strcmp((const char*) bytes, text) == 0;
}

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


// Two AL dictionaries written out by hand from the node format: the
// offset of the node's character set, its flags (1 an attribute, 2 no
// children, 4 the last of its siblings), the offset of its first child
// when it has any, and then the attribute.  The header's second byte is
// the kind (1, a byte-character lexicon) with the attribute size (1)
// above it.

// "at" and "an"
static const UByte kWordsAL[] = {
	'a', 0x11,						// +0   the header
	0, 16,   0x04, 0, 8,    1,		// +2   'a', children at 8, the last of its siblings
	0, 18,   0x03,          3,		// +8   't', a leaf with the attribute 3
	0, 20,   0x07,          4,		// +12  'n', a leaf, the last of them
	'a', 0,							// +16  the character sets
	't', 0,							// +18
	'n', 0							// +20
};

// "be", so that the two dictionaries can be told apart by which of them
// answered
static const UByte kNamesAL[] = {
	'a', 0x11,						// +0
	0, 12,   0x04, 0, 8,    1,		// +2   'b', children at 8, the last
	0, 14,   0x07,          9,		// +8   'e', a leaf with the attribute 9
	'b', 0,							// +12
	'e', 0							// +14
};


// the ROM's U.S. locale bundle (nsfunctions.py --object 0x4a4d09)
const ULong kUSABundle = 0x004a4d09;


static UByte gWordsBytes[sizeof(kWordsAL)];
static UByte gNamesBytes[sizeof(kNamesAL)];


// One frame of vars.dictionaries.
static Ref
MakeDictFrame(long id, long type, long domain, Ref linked)
{
	RefVar frame(AllocateFrame());
	SetFrameSlot(frame, RSSYMdictid, RefVar(MAKEINT(id)));
	SetFrameSlot(frame, RSSYMdicttype, RefVar(MAKEINT(type)));
	SetFrameSlot(frame, RSSYMdomaintype, RefVar(MAKEINT(domain)));
	if (NOTNIL(linked))
		SetFrameSlot(frame, RSSYMlinkeddictid, RefVar(linked));
	return frame;
}


// One entry of gDictList, as InitDictionaries would leave it.
static void
AddListEntry(Handle dictionary, UByte index)
{
	dictListEntry* entry = (dictListEntry*) gDictList->GetEntry((ULong) gDictList->Add());
	memset(entry, 0, sizeof(dictListEntry));
	entry->fDictionary = dictionary;
	entry->fIndex = index;
	entry->fStatus = 1;
}


// which dictionary FindDictionaryEntry answers for an id
static Handle
EntryFor(ULong id)
{
	dictListEntry* entry = FindDictionaryEntry(id);
	return entry == nil ? nil : entry->fDictionary;
}


// a UniChar string of an ASCII one
static void
Uni(UniChar* out, const char* text)
{
	long i = 0;
	for (; text[i] != 0; i++)
		out[i] = (UniChar) (UByte) text[i];
	out[i] = 0;
}


static Boolean
Is(const UniChar* text, const char* expected)
{
	long i = 0;
	for (; expected[i] != 0; i++)
		if (text[i] != (UniChar) (UByte) expected[i])
			return false;
	return text[i] == 0;
}


static long
LookUp(const char* word, ULong* attribute)
{
	UniChar chars[64];
	long i = 0;
	for (; word[i] != 0; i++)
		chars[i] = (UniChar) (UByte) word[i];
	chars[i] = 0;
	return LookupWord(chars, attribute);
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_ROM_BIN) != noErr)
	{
		printf("test_Dictionaries: cannot import %s\n", NEWTON_ROM_BIN);
		return 1;
	}
	gObjectHeapSize = 0x200000;
	InitObjects();
	SetFrameSlot(RefVar(gVarFrame), RSSYMvars, RefVar(gVarFrame));

	memcpy(gWordsBytes, kWordsAL, sizeof(kWordsAL));
	memcpy(gNamesBytes, kNamesAL, sizeof(kNamesAL));

	// the two dictionaries, opened over bytes that already exist the way
	// the ROM's own are
	Handle words = BuildDictionaryFromPtr(gWordsBytes, (Size) sizeof(kWordsAL));
	Handle names = BuildDictionaryFromPtr(gNamesBytes, (Size) sizeof(kNamesAL));
	EXPECT(words != nil && names != nil);
	if (words == nil || names == nil)
		return 1;
	((AirusAParmBlock*) *words)->fDictID = 6;		// the general lexicon
	((AirusAParmBlock*) *names)->fDictID = 0x18;	// the names one

	// the list: the general lexicon, linked to the names one
	RefVar list(MakeArray(2));
	SetArraySlot(list, 0, RefVar(MakeDictFrame(6, 0, 0x1000, MAKEINT(0x18))));
	SetArraySlot(list, 1, RefVar(MakeDictFrame(0x18, 0, 0x2000, NILREF)));
	SetFrameSlot(RefVar(gVarFrame), RSSYMdictionaries, list);
	gDictList = TDArray::Make(sizeof(dictListEntry), 0);
	EXPECT(gDictList != nil);
	AddListEntry(words, 0);
	AddListEntry(names, 1);

	// ---- the list, and the ids that stand for other ids ----
	EXPECT(EntryFor(6) == words);
	EXPECT(EntryFor(0x18) == names);
	EXPECT(EntryFor(13) == names && EntryFor(0x29) == names);
	EXPECT(EntryFor(1) == words && EntryFor(9) == words && EntryFor(0x2a) == words);
	// an id the list does not have falls back on the general lexicon
	// rather than on nothing
	EXPECT(EntryFor(0x77) == words);

	// ---- a chain built from a configuration ----
	// its mask picks the general lexicon, and the names one comes with it
	// because the two are linked
	{
		TDictChain* chains[kDictChainCount];
		RefVar config(AllocateFrame());
		SetFrameSlot(config, RSSYMinputmask, RefVar(MAKEINT(0x1000)));
		SetFrameSlot(config, RSSYMinhibitsymbolsdictionary, RefVar(TRUEREF));
		BuildChains(chains, config);
		EXPECT(chains[kDictChainOrdinary] != nil);
		EXPECT(chains[kDictChainSpecial] == nil && chains[kDictChainException] == nil);
		if (chains[kDictChainOrdinary] != nil)
		{
			TDictChain* chain = chains[kDictChainOrdinary];
			EXPECT(chain->Count() == 2);
			EXPECT(chain->PositionToHandle(0) == words);
			EXPECT(chain->PositionToHandle(1) == names);
			EXPECT(chain->HandleToPosition(names) == 1);
			EXPECT(chain->fPosition == -1);

			// the words are found, with what was stored beside them
			ULong attribute = 0;
			EXPECT(LookupWordInChain((const UByte*) "at", chain, &attribute) == 6);
			EXPECT(attribute == 3);
			EXPECT(LookupWordInChain((const UByte*) "an", chain, &attribute) == 6);
			EXPECT(attribute == 4);
			// this one only the second dictionary of the chain has
			EXPECT(LookupWordInChain((const UByte*) "be", chain, &attribute) == 0x18);
			EXPECT(attribute == 9);
			// these are in neither
			EXPECT(LookupWordInChain((const UByte*) "ax", chain, &attribute) == -1);
			EXPECT(LookupWordInChain((const UByte*) "zoo", chain, &attribute) == -1);
			// and "a" begins two words without being one itself, which is
			// not an answer to the question being asked
			EXPECT(LookupWordInChain((const UByte*) "a", chain, &attribute) == -1);

			// a dictionary taken out of the chain is not asked again
			EXPECT(chain->RemoveDictFromChain(names) == 0);
			EXPECT(chain->Count() == 1);
			EXPECT(LookupWordInChain((const UByte*) "be", chain, &attribute) == -1);
		}
		DoneChains(chains);
		EXPECT(chains[kDictChainOrdinary] == nil);
	}

	// a mask that picks neither of them leaves the chains empty
	{
		TDictChain* chains[kDictChainCount];
		RefVar config(AllocateFrame());
		SetFrameSlot(config, RSSYMinputmask, RefVar(MAKEINT(0x800000)));
		SetFrameSlot(config, RSSYMinhibitsymbolsdictionary, RefVar(TRUEREF));
		BuildChains(chains, config);
		EXPECT(chains[kDictChainOrdinary] == nil);
		// and a lookup down a chain that is not there is simply a no
		ULong attribute = 0;
		EXPECT(LookupWordInChain((const UByte*) "at", chains[kDictChainOrdinary],
								 &attribute) == -1);
		DoneChains(chains);
	}

	// a configuration that names a dictionary by hand gets it whatever
	// the mask says
	{
		TDictChain* chains[kDictChainCount];
		RefVar config(AllocateFrame());
		SetFrameSlot(config, RSSYMinputmask, RefVar(MAKEINT(0x800000)));
		SetFrameSlot(config, RSSYMinhibitsymbolsdictionary, RefVar(TRUEREF));
		SetFrameSlot(config, RSSYMdictionaries, RefVar(MAKEINT(0x18)));
		EXPECT(CountCustomDictionaries(config) == 1);
		EXPECT(GetCustomDictionary(config, 0) == 0x18);
		BuildChains(chains, config);
		EXPECT(chains[kDictChainOrdinary] != nil);
		if (chains[kDictChainOrdinary] != nil)
		{
			EXPECT(chains[kDictChainOrdinary]->Count() == 1);
			EXPECT(chains[kDictChainOrdinary]->PositionToHandle(0) == names);
		}
		DoneChains(chains);
	}

	// a dictionary of another kind goes into another chain, and one of a
	// kind that is no chain at all stops the walk along the links
	{
		RefVar frames(Dictionaries());
		TDictChain* chains[kDictChainCount];
		RefVar config(AllocateFrame());
		SetFrameSlot(config, RSSYMinputmask, RefVar(MAKEINT(0x1000)));
		SetFrameSlot(config, RSSYMinhibitsymbolsdictionary, RefVar(TRUEREF));

		SetFrameSlot(RefVar(GetArraySlotRef(frames, 1)), RSSYMdicttype, RefVar(MAKEINT(4)));
		BuildChains(chains, config);
		EXPECT(chains[kDictChainOrdinary] != nil
			   && chains[kDictChainOrdinary]->Count() == 1);
		EXPECT(chains[kDictChainException] != nil
			   && chains[kDictChainException]->PositionToHandle(0) == names);
		ULong attribute = 0;
		EXPECT(LookupWordInChain((const UByte*) "be", chains[kDictChainException],
								 &attribute) == 0x18);
		DoneChains(chains);

		SetFrameSlot(RefVar(GetArraySlotRef(frames, 1)), RSSYMdicttype, RefVar(MAKEINT(9)));
		BuildChains(chains, config);
		EXPECT(chains[kDictChainOrdinary] != nil
			   && chains[kDictChainOrdinary]->Count() == 1);
		EXPECT(chains[kDictChainException] == nil);
		DoneChains(chains);

		SetFrameSlot(RefVar(GetArraySlotRef(frames, 1)), RSSYMdicttype, RefVar(MAKEINT(0)));
	}

	// ---- the lookup as everything else calls it ----
	// With nothing being written on, the chains are built from
	// `rcbuildchains`, whose mask is 0x1000 - the general lexicon's
	// domain - so the words come back.
	{
		ULong attribute = 0;
		EXPECT(LookUp("at", &attribute) == 6 && attribute == 3);
		EXPECT(LookUp("be", &attribute) == 0x18 && attribute == 9);
		EXPECT(LookUp("zoo", &attribute) == -1);
	}

	// ---- the capitalisations a lookup tries ----
	{
		UniChar out[32];
		UniChar word[8];
		word[0] = 'H'; word[1] = 'E'; word[2] = 'L'; word[3] = 'L';
		word[4] = 'O'; word[5] = 0;

		// nought is the word as it was written, and each one after that is
		// built on the one before it
		EXPECT(BuildCaseVariant(word, 0x40, 0, out));
		EXPECT(Ustrcmp(out, word) == 0);
		EXPECT(BuildCaseVariant(word, 0x40, 1, out));
		EXPECT(out[0] == 'h' && out[4] == 'o');
		EXPECT(BuildCaseVariant(word, 0x40, 2, out));
		EXPECT(out[0] == 'H' && out[1] == 'e');
		// ... and the third is "hEllo", which is not a capitalisation
		// anyone would write; the ROM looks it up like any other
		EXPECT(BuildCaseVariant(word, 0x40, 3, out));
		EXPECT(out[0] == 'h' && out[1] == 'E' && out[2] == 'l');
		// sixteen is as far as it goes
		EXPECT(!BuildCaseVariant(word, 0x40, 17, out));
		// and with no flags there is nothing but the word itself
		EXPECT(BuildCaseVariant(word, 0, 0, out));
		EXPECT(!BuildCaseVariant(word, 0, 1, out));

		// a word found by one of its capitalisations answers which
		// dictionary had it, and `variant` is left holding the spelling
		// that was found
		UniChar at[8];
		at[0] = 'A'; at[1] = 'T'; at[2] = 0;
		ULong attribute = 0x40;
		EXPECT(LookupWordOrVariant(at, &attribute, out) == 6);
		EXPECT(out[0] == 'a' && out[1] == 't' && attribute == 3);
	}

	// ---- the dictionaries the ROM itself carries ----
	// InitDictionaries clones the ROM's own list of descriptors, opens a
	// dictionary over the word data each of them names, and rebuilds
	// gDictList; the words looked up below are the ROM's, out of its own
	// lexicons, read where they lie in the image.
	{
		// what the boot script makes: vars.international with the U.S.
		// locale bundle, which is where the four lexicons the locale
		// carries and the words that replace the ROM's come from
		RefVar intl(AllocateFrame());
		SetFrameSlot(intl, RSSYMcurrentlocalebundle, RefVar(TranslateROMRef(kUSABundle)));
		SetFrameSlot(RefVar(gVarFrame), RSSYMinternational, intl);

		InitDictionaries();
		RefVar list(Dictionaries());
		EXPECT(Length(list) == 27);
		EXPECT(gDictList != nil && gDictList->Count() == Length(list));

		// the general lexicon is there, and it is a dictionary of the
		// ROM's own bytes
		dictListEntry* entry = FindDictionaryEntry(0);
		EXPECT(entry != nil && entry->fDictionary != nil);
		if (entry != nil && entry->fDictionary != nil)
			EXPECT(((AirusAParmBlock*) *entry->fDictionary)->fDictID == 0);

		// and the words of the language are in it
		ULong attribute = 0;
		EXPECT(LookUp("hello", &attribute) != -1);
		EXPECT(LookUp("notebook", &attribute) != -1);
		EXPECT(LookUp("the", &attribute) != -1);
		EXPECT(LookUp("qqxyzzy", &attribute) == -1);

		// a word written in capitals is not in the lexicon as it stands,
		// but one of its capitalisations is
		UniChar word[16];
		UniChar variant[16];
		word[0] = 'H'; word[1] = 'E'; word[2] = 'L'; word[3] = 'L';
		word[4] = 'O'; word[5] = 0;
		attribute = 0;
		EXPECT(LookupWord(word, &attribute) == -1);
		attribute = 0x40;
		EXPECT(LookupWordOrVariant(word, &attribute, variant) != -1);
		EXPECT(variant[0] == 'h' && variant[4] == 'o');

		// the four the locale carries, which are binaries in the bundle
		// rather than slots of the ROM's word data
		EXPECT(gTimeLexDictionary != nil && gDateLexDictionary != nil);
		EXPECT(gPhoneLexDictionary != nil && gNumberLexDictionary != nil);

		// ... and the ones it names by slot.  Dictionary 111 reads times;
		// the U.S. bundle says its words are slot 16 of the ROM's data, so
		// ReadDictPrefs opens that in place of whatever the descriptor
		// named and writes the slot into the frame.
		RefVar times(FindDictionaryFrame(111));
		EXPECT(NOTNIL(times));
		ReadDictPrefs();
		EXPECT(RINT(RefVar(GetProtoVariable(times, RSSYMromdictid, nil))) == 16);
		dictListEntry* timeEntry = FindDictionaryEntry(111);
		EXPECT(timeEntry != nil && timeEntry->fDictionary != nil);
		// ... and doing it twice changes nothing, because the dictionary is
		// already open on those very bytes
		ReadDictPrefs();
		EXPECT(FindDictionaryEntry(111)->fDictionary == timeEntry->fDictionary);

		// the words are still there afterwards
		attribute = 0;
		EXPECT(LookUp("hello", &attribute) != -1);
	}

	// ---- the words the machine keeps of its own ----
	// The three dictionaries the writer fills start empty; what is
	// checked here is the counting, the expansion, and the one-word
	// memory the auto-add dictionary keeps.
	{
		// an expansion put in by hand: the word goes in the dictionary
		// with the index of its expansion as its attribute, and the
		// expansion itself goes in the frame's `list`
		RefVar frame(FindDictionaryFrame(kExpandDictionary));
		EXPECT(NOTNIL(frame));
		RefVar expansions(MakeArray(1));
		UniChar full[64];
		Uni(full, "as soon as possible");
		SetArraySlot(expansions, 0, RefVar(MakeString(full)));
		SetFrameSlot(frame, RSSYMlist, expansions);
		EXPECT(RINT(RefVar(GetProtoVariable(frame, RSSYMcount, nil))) == 0);
		EXPECT(AddWordWithCount(kExpandDictionary, (UByte*) "asap", 0) == 0);
		EXPECT(airusResult == 0);
		EXPECT(RINT(RefVar(GetProtoVariable(frame, RSSYMcount, nil))) == 1);

		// the word written out in full
		UniChar word[64];
		Uni(word, "asap");
		Handle out = ExpandWord(word);
		EXPECT(out != nil && Is((const UniChar*) *out, "as soon as possible"));
		if (out != nil)
			DisposeHandle(out);
		// ... with what was written around it put back
		Uni(word, "(asap),");
		out = ExpandWord(word);
		EXPECT(out != nil && Is((const UniChar*) *out, "(as soon as possible),"));
		if (out != nil)
			DisposeHandle(out);
		// ... and a capital carried over
		Uni(word, "Asap");
		out = ExpandWord(word);
		EXPECT(out != nil && Is((const UniChar*) *out, "As soon as possible"));
		if (out != nil)
			DisposeHandle(out);
		// a word the dictionary has never heard of expands into nothing
		Uni(word, "qqxyzzy");
		EXPECT(ExpandWord(word) == nil);

		// the limit: a dictionary as full as its frame says is refused
		SetFrameSlot(frame, RSSYMlimit, RefVar(MAKEINT(1)));
		EXPECT(AddWordWithCount(kExpandDictionary, (UByte*) "btw", 0) == 1);
		EXPECT(airusResult == kAirusDictionaryFull);
		EXPECT(RINT(RefVar(GetProtoVariable(frame, RSSYMcount, nil))) == 1);

		// the punctuation at the ends, on its own
		Uni(word, "\"quoted.\"");
		UniChar* leading;
		UniChar* trailing;
		CollectPunctSymbols(word, &leading, &trailing);
		EXPECT(Is(word, "quoted"));
		EXPECT(leading != nil && Is(leading, "\""));
		EXPECT(trailing != nil && Is(trailing, ".\""));
		if (leading != nil)
			DisposePtr((Ptr) leading);
		if (trailing != nil)
			DisposePtr((Ptr) trailing);

		// a capital is noticed without the word being changed
		Uni(word, "Hello");
		EXPECT(Capitalized(word) && Is(word, "Hello"));
		Uni(word, "hello");
		EXPECT(!Capitalized(word) && Is(word, "hello"));

		// the auto-add dictionary remembers the last word it was offered
		Uni(word, "notebook");
		EXPECT(!LastWordSame(RefVar(MakeString(word))));
		EXPECT(LastWordSame(RefVar(MakeString(word))));
		Uni(word, "elsewhere");
		EXPECT(!LastWordSame(RefVar(MakeString(word))));
		Uni(word, "notebook");
		EXPECT(!LastWordSame(RefVar(MakeString(word))));
		// ---- the words the machine adds on the writer's behalf ----
		// Only when the writer has asked for it, and only for a word the
		// handwriting recogniser read.
		RefVar prefs(AllocateFrame());
		SetFrameSlot(RefVar(gVarFrame), RSSYMuserconfiguration, prefs);
		UniChar learnt[32];
		Uni(learnt, "zorblat");
		ULong where = 0;
		EXPECT(LookUp("zorblat", &where) == -1);

		// with the preference off, nothing is learnt
		gWordID = 'WREC';
		EXPECT(!AddAutoAdd(learnt));
		// ... and with it on but the word typed rather than written
		SetFrameSlot(prefs, RSSYMdoautoadd, RefVar(TRUEREF));
		gWordID = 0;
		EXPECT(!AddAutoAdd(learnt));

		// written, and asked for: the word goes into the user dictionary
		// and into the machine's own record of what it added
		gWordID = 'WREC';
		EXPECT(AddAutoAdd(learnt));
		EXPECT(LookUp("zorblat", &where) == kUserDictionary);
		EXPECT(RINT(RefVar(GetProtoVariable(RefVar(FindDictionaryFrame(kAutoAddDictionary)),
											  RSSYMcount, nil))) == 1);
		// the same word again is the one it was offered last time, so it
		// is left alone; and it is in the dictionaries now anyway
		EXPECT(!AddAutoAdd(learnt));

		// and taken back out of both when the entry it was learnt from goes
		RemoveAutoAdd(learnt);
		EXPECT(LookUp("zorblat", &where) == -1);
		EXPECT(RINT(RefVar(GetProtoVariable(RefVar(FindDictionaryFrame(kAutoAddDictionary)),
											  RSSYMcount, nil))) == 0);
	}

	// ---- what a script does with a dictionary of its own ----
	// (the natives want a root view, because registering one tells the
	// Assistant's line to look again)
	InitGraf();
	gTestMap.baseAddr = (Ptr) gTestBits;
	gTestMap.rowBytes = kTestWidth / 8;
	SetRect(&gTestMap.bounds, 0, 0, kTestWidth, kTestHeight);
	gTestMap.pixMapFlags = kPixMapPtr | 1;
	gTestMap.deviceRes.v = kDefaultDPI;
	gTestMap.deviceRes.h = kDefaultDPI;
	OpenPort(&gTestPort);
	SetPortBits(&gTestMap);
	gTestPort.portRect = gTestMap.bounds;
	RectRgn(gTestPort.visRgn, &gTestMap.bounds);
	InitViewSystem();
	EXPECT(gRootView != nil);
	// the slot DictionariesChanged looks for; on a real machine the
	// Assistant's view has put itself there
	SetFrameSlot(RefVar(gRootView->fContext), RSSYMassistant, RefVar(AllocateFrame()));

	{
		// a frame with a dictionary of its own, as protoDictionary's
		// New() makes one
		RefVar frame(AllocateFrame());
		EXPECT(RINT(RefVar(FAirusNew(frame, RefVar(MAKEINT(kAirusKindEnumRAM | kAirusLockedBit)),
									 RefVar(MAKEINT(1))))) == 0);
		// its second byte, and the size of the attribute each word carries
		EXPECT(RINT(RefVar(FAirusDictionaryType(frame))) == (kAirusKindEnumRAM | kAirusLockedBit));
		EXPECT(RINT(RefVar(FAirusAttributeSize(frame))) == 1);

		RefVar word(MakeString("badger"));
		EXPECT(RINT(RefVar(FAirusAddWord(frame, word, RefVar(MAKEINT(3))))) == 0);
		RefVar found(AllocateFrame());
		EXPECT(RINT(RefVar(FAirusLookupWord(frame, word, found))) == kAirusIsWord);
		EXPECT(RINT(RefVar(GetFrameSlotRef(found, RSSYMattribute))) == 3);

		// the attribute changed where it lies
		EXPECT(RINT(RefVar(FAirusChangeAttribute(frame, word, RefVar(MAKEINT(9))))) == 0);
		EXPECT(RINT(RefVar(FAirusLookupWord(frame, word, found))) == kAirusIsWord);
		EXPECT(RINT(RefVar(GetFrameSlotRef(found, RSSYMattribute))) == 9);
		// a word it does not have
		EXPECT(RINT(RefVar(FAirusChangeAttribute(frame, RefVar(MakeString("stoat")),
												 RefVar(MAKEINT(1))))) == kAirusNotAWord);

		// registered: it joins vars.dictionaries and gDictList, and gets
		// an id of its own
		long before = gDictList->Count();
		long wasNext = gNextCustomDictionaryID;
		RefVar id(FAirusRegisterDictionary(frame));
		EXPECT(RINT(id) == wasNext && gNextCustomDictionaryID == wasNext + 1);
		EXPECT(gDictList->Count() == before + 1);
		EXPECT(RINT(RefVar(GetFrameSlotRef(frame, RSSYMdictid))) == RINT(id));
		EXPECT(EQRef(GetFrameSlotRef(frame, RSSYM_proto), Rprotodictionary));
		EXPECT(FindDictionaryEntry((ULong) RINT(id))->fDictionary == GetScriptDictRef(frame));
		// (it is in the list, but not in any chain: a chain is built
		// from the dictionaries whose `domainType` overlaps what is
		// being written on, and this frame names none)
		EXPECT(EQRef(GetArraySlotRef(RefVar(Dictionaries()),
									 Length(RefVar(Dictionaries())) - 1), frame));

		// a cursor of its own, walked from the script side
		{
			EXPECT(RINT(RefVar(FAirusAddWord(frame, RefVar(MakeString("badge")),
											 RefVar(MAKEINT(1))))) == 0);
			RefVar cursor(FAirusIteratorMake(frame));
			EXPECT(IsFrame(cursor));
			EXPECT(EQRef(GetFrameSlotRef(cursor, RSSYMdict), frame));
			EXPECT(Length(RefVar(GetFrameSlotRef(frame, RSSYMcursors))) == 1);

			// from the beginning: "badge" comes before "badger"
			EXPECT(NOTNIL(RefVar(FAirusIteratorReset(cursor, RefVar(MakeString("")),
													 RefVar(NILREF), RSSYMfirst))));
			RefVar entry(AllocateFrame());
			EXPECT(NOTNIL(RefVar(FAirusIteratorThisWord(cursor, entry))));
			EXPECT(WordIs(GetFrameSlotRef(entry, RSSYMword), "badge"));
			EXPECT(NOTNIL(RefVar(FAirusIteratorNextWord(cursor))));
			EXPECT(NOTNIL(RefVar(FAirusIteratorThisWord(cursor, entry))));
			EXPECT(WordIs(GetFrameSlotRef(entry, RSSYMword), "badger"));
			EXPECT(RINT(RefVar(GetFrameSlotRef(entry, RSSYMattribute))) == 9);
			// and no further
			EXPECT(ISNIL(RefVar(FAirusIteratorNextWord(cursor))));
			// back again
			EXPECT(NOTNIL(RefVar(FAirusIteratorReset(cursor, RefVar(MakeString("")),
													 RefVar(NILREF), RSSYMlast))));
			EXPECT(NOTNIL(RefVar(FAirusIteratorThisWord(cursor, entry))));
			EXPECT(WordIs(GetFrameSlotRef(entry, RSSYMword), "badger"));

			// the clone shares the original's cursor, which is the ROM's
			// own muddle rather than ours
			RefVar copy(FAirusIteratorClone(cursor));
			EXPECT(IsFrame(copy));
			EXPECT(EQRef(GetFrameSlotRef(copy, RSSYMcursor),
						 GetFrameSlotRef(cursor, RSSYMcursor)));

			EXPECT(ISNIL(RefVar(FAirusIteratorDispose(cursor))));
			EXPECT(ISNIL(RefVar(GetFrameSlotRef(cursor, RSSYMcursor))));
		}

		// and taken out again
		FAirusUnregisterDictionary(frame);
		EXPECT(gDictList->Count() == before);
		EXPECT(ISNIL(RefVar(GetFrameSlotRef(frame, RSSYMdictid))));

		// the dictionary given back: the frame no longer has one
		EXPECT(NOTNIL(RefVar(FAirusDispose(frame))));
		EXPECT(ISNIL(RefVar(GetFrameSlotRef(frame, RSSYMdict))));
	}

	if (failures == 0)
		printf("test_Dictionaries: all passed\n");
	else
		printf("test_Dictionaries: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
