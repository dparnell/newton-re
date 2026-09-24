/*
	File:		recognition/Dictionaries.cpp

	Contains:	The dictionaries the machine knows, and how a word is
				looked up in them - Dictionaries.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Dictionaries.h"
#include "NativeFunctions.h"
#include "Airus.h"
#include "ROMDictionaryData.h"
#include "Locale.h"			// IntlResources
#include "RecConfig.h"
#include "View.h"
#include "RootView.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"	// GetVariable
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "Unicode.h"

#include <string.h>


// ROM 0x0c10162c gDictList
TDArray*	gDictList = nil;

// ROM 0x0c100f8c-0x0c100f98 - the lexicons the locale carries
Handle		gTimeLexDictionary = nil;
Handle		gDateLexDictionary = nil;
Handle		gPhoneLexDictionary = nil;
Handle		gNumberLexDictionary = nil;


/*------------------------------------------------------------------------------
	T h e   l i s t
------------------------------------------------------------------------------*/

// ROM 0x0013d4ac FindDictionaryEntry__FUl
// The list entry for a dictionary id.
//
// A dozen ids stand for others - a lexicon that was folded into another
// one, or a name the outside world uses for a dictionary the list keeps
// under a different number - and the substitution is written out here as
// a switch rather than kept in the frames.  An id that names nothing at
// all falls back on whatever the list calls 6, which is the general
// lexicon: asking for a dictionary that is not there gets you the
// ordinary words rather than nothing.
dictListEntry*
FindDictionaryEntry(ULong id)
{
	switch (id)
	{
	case 13:
	case 0x29:	id = 0x18;	break;
	case 1:
	case 7:
	case 9:
	case 0x14:
	case 0x2a:	id = 6;		break;
	case 2:		id = 3;		break;
	case 10:	id = 0x2d;	break;
	case 0x2b:
	case 0x2c:	id = 0x1a;	break;
	default:				break;
	}

	RefVar list(Dictionaries());
	dictListEntry* fallback = nil;
	ULong count = (ULong) gDictList->fCount;
	for (ULong i = 0; i < count; i++)
	{
		dictListEntry* entry = (dictListEntry*) gDictList->GetEntry(i);
		RefVar frame(GetArraySlotRef(list, entry->fIndex));
		ULong was = (ULong) RINT(RefVar(GetProtoVariable(frame, RSSYMdictid, nil)));
		if (was == id)
			return entry;
		if (was == 6)
			fallback = entry;
	}
	return fallback;
}


/*------------------------------------------------------------------------------
	B u i l d i n g   t h e   l i s t
------------------------------------------------------------------------------*/

// One of the four lexicons the locale carries, opened over the binary in
// the named slot of the current locale bundle.  Written out four times in
// the ROM, once per lexicon.
static void
OpenLocaleLexicon(RefArg slot, Handle* where)
{
	RefVar words(GetLocaleSlot(slot));
	if (ISNIL(words))
		return;
	Handle dictionary = ReadRefDictionary(words);
	if (dictionary != nil && *dictionary != nil)
	{
		((AirusAParmBlock*) *dictionary)->fField4c = 1;
		*where = dictionary;
	}
}


// ROM 0x0013de2c InitDictionaries__Fv
// The dictionaries built and put in `vars.dictionaries`, with `gDictList`
// beside them.
//
// The list itself is the ROM's own (`Rdictionarylist`, cloned), and each
// of its descriptors is wrapped in a clone of `canonicalDictRAMFrame`
// with the descriptor as its `_proto`, which is what gives every
// dictionary the frame a script talks to and the `dictID` it is found by.
//
// A descriptor with no `romDictID` names one of the dictionaries the
// machine makes for itself: 31 the user dictionary, 35 the expand
// dictionary and 36 the auto-add one start empty, and 32 is the trie.
// Every other descriptor names a slot of the ROM's own word data, which
// is opened where it lies - but only for the kinds of dictionary a lookup
// walks (`dictType` under 2, or 4, the exceptions); the rest are
// described but never built.
//
// NOT YET RECONSTRUCTED: `gTrie`, which would be dictionary 32 if that
// descriptor had no `romDictID` - it has one, so this ROM never takes
// that path.
void
InitDictionaries(void)
{
	InitROMDictionaryData();
	// DEVIATION: a host that has not imported the ROM's objects has no
	// list to clone; an empty one keeps everything that takes its Length
	// happy, which is what the list is for.
	RefVar list(IsArray(RefVar(Rdictionarylist)) ? Clone(RefVar(Rdictionarylist)) : MakeArray(0));
	SetFrameSlot(RefVar(gVarFrame), RSSYMdictionaries, list);
	gDictList = TDArray::Make(sizeof(dictListEntry), 0);
	long count = Length(list);
	for (long slot = 0; slot < count; slot++)
	{
		RefVar descriptor(GetArraySlotRef(list, slot));
		long id = RINT(RefVar(GetProtoVariable(descriptor, RSSYMdictid, nil)));
		RefVar romDictId(GetProtoVariable(descriptor, RSSYMromdictid, nil));
		RefVar frame(Clone(RefVar(Rcanonicaldictramframe)));
		SetFrameSlot(frame, RSSYM_proto, descriptor);
		SetFrameSlot(frame, RSSYMromdictid, romDictId);
		SetArraySlotRef(list, slot, frame);

		Handle dictionary = nil;
		if (ISNIL(romDictId))
		{
			if (id == kUserDictionary || id == kExpandDictionary || id == kAutoAddDictionary)
				dictionary = NewDictionary(kAirusKindEnumRAM | kAirusLockedBit, 1);
			// NOT YET RECONSTRUCTED: gTrie, which is dictionary 32
		}
		else
		{
			ULong size = 0;
			const void* data = GetROMDictionaryData((ULong) RINT(romDictId), &size);
			long type = RINT(RefVar(GetProtoVariable(frame, RSSYMdicttype, nil)));
			if (type < 2 || type == 4)
			{
				dictionary = BuildDictionaryFromPtr((void*) data, (Size) size);
				if (dictionary != nil)
					((AirusAParmBlock*) *dictionary)->fField4c = 1;
			}
		}

		dictListEntry entry;
		memset(&entry, 0, sizeof(entry));
		entry.fDictionary = dictionary;
		entry.fIndex = (UByte) slot;
		entry.fStatus = (UByte) RINT(RefVar(GetProtoVariable(frame, RSSYMstatus, nil)));
		entry.fDisabled = 0;

		Ref dict = NILREF;
		if (dictionary != nil)
		{
			((AirusAParmBlock*) *dictionary)->fDictID = id & 0xffff;
			dict = AddressToRef(dictionary);
		}
		SetFrameSlot(frame, RSSYMdict, RefVar(dict));
		// DEVIATION: the ROM copies the eight bytes of the entry into the
		// new slot; here a Handle is pointer-sized, so the whole entry is
		// assigned instead.
		*(dictListEntry*) gDictList->AddEntry() = entry;
	}
	gDictList->Compact();

	// the four the locale carries, which are not in the list at all
	OpenLocaleLexicon(RSSYMtimedictionary, &gTimeLexDictionary);
	OpenLocaleLexicon(RSSYMdatedictionary, &gDateLexDictionary);
	OpenLocaleLexicon(RSSYMphonedictionary, &gPhoneLexDictionary);
	OpenLocaleLexicon(RSSYMnumberdictionary, &gNumberLexDictionary);
}


/*------------------------------------------------------------------------------
	R e p l a c i n g   o n e
------------------------------------------------------------------------------*/

// ROM 0x0013ec74 ReplaceDictionary__F6RefVarT1
// The dictionary the frame names replaced by one over the bytes of a
// binary object.  The old one is disposed of first, so a dictionary that
// was open on the ROM's own words is closed before the locale's take
// their place.
//
// A frame of a kind no lookup walks is a mistake rather than a thing to
// ignore, and is thrown on.
Boolean
ReplaceDictionary(RefArg frame, RefArg binary)
{
	long id = RINT(RefVar(GetProtoVariable(frame, RSSYMdictid, nil)));
	long type = RINT(RefVar(GetProtoVariable(frame, RSSYMdicttype, nil)));
	Handle dictionary = nil;
	Boolean replaced = false;
	if (type < 2 || type == 4)
	{
		dictionary = FindDictionaryEntry((ULong) id)->fDictionary;
		if (dictionary != nil)
			DisposDictionary(&dictionary);
		dictionary = ReadRefDictionary(binary);
		((AirusAParmBlock*) *dictionary)->fDictID = id;
		replaced = true;
		((AirusAParmBlock*) *dictionary)->fField4c = 1;
	}
	else
		ThrowMsg("unknown dictionary type");

	FindDictionaryEntry((ULong) id)->fDictionary = dictionary;
	SetFrameSlot(frame, RSSYMdict,
				 RefVar(dictionary == nil ? NILREF : AddressToRef(dictionary)));
	return replaced;
}


// ROM 0x0013f14c ReplaceDictionary__F6RefVarUlPcT2
// The same over a slot of the ROM's own word data, which is what a locale
// that names one of the ROM's lexicons rather than carrying its own gets.
// A dictionary already open on those very bytes is left alone.
Boolean
ReplaceDictionary(RefArg frame, ULong romDictID, const char* data, ULong size)
{
	long id = RINT(RefVar(GetProtoVariable(frame, RSSYMdictid, nil)));
	long type = RINT(RefVar(GetProtoVariable(frame, RSSYMdicttype, nil)));
	Handle dictionary = nil;
	Boolean replaced = false;
	if (type < 2 || type == 4)
	{
		dictionary = FindDictionaryEntry((ULong) id)->fDictionary;
		if (dictionary != nil)
		{
			if (*((AirusAParmBlock*) *dictionary)->fDataHandle == (Ptr) data)
				return false;
			DisposDictionary(&dictionary);
		}
		dictionary = BuildDictionaryFromPtr((void*) data, (Size) size);
		((AirusAParmBlock*) *dictionary)->fDictID = id & 0xffff;
		replaced = true;
		((AirusAParmBlock*) *dictionary)->fField4c = 1;

		FindDictionaryEntry((ULong) id)->fDictionary = dictionary;
		SetFrameSlot(frame, RSSYMdict,
					 RefVar(dictionary == nil ? NILREF : AddressToRef(dictionary)));
		SetFrameSlot(frame, RSSYMromdictid, RefVar(MAKEINT((long) romDictID)));
	}
	else
		ThrowMsg("unknown dictionary type");
	return replaced;
}


// ROM 0x0013e384 ReplaceLocalDictionary__F6RefVarT1
// What the locale has to say about one dictionary.  The frame's
// `localDictSlot` is the name of the slot of the locale bundle that
// carries this dictionary's words: an integer there is a slot of the
// ROM's own word data, and anything else is a binary of words the locale
// brought with it.  A frame with no `localDictSlot`, or a bundle with
// nothing in that slot, is left as it is.
Boolean
ReplaceLocalDictionary(RefArg localeBundle, RefArg frame)
{
	Boolean replaced = false;
	RefVar slot(GetProtoVariable(frame, RSSYMlocaldictslot, nil));
	RefVar words(ISNIL(slot) ? NILREF : GetProtoVariable(localeBundle, slot, nil));
	if (ISINT(words))
	{
		ULong size = 0;
		const void* data = GetROMDictionaryData((ULong) RINT(words), &size);
		replaced = ReplaceDictionary(frame, (ULong) RINT(words), (const char*) data, size);
	}
	else if (NOTNIL(words))
		replaced = ReplaceDictionary(frame, words);
	return replaced;
}


// ROM 0x0013e4a4 ReadDictPrefs__Fv
// Every dictionary of the list asked what the current locale has to say
// about it.  That is how a machine set to another language reads other
// words: the list of dictionaries is the ROM's either way, and the locale
// bundle replaces the data behind the ones it has its own words for.
//
// BUG (kept): the list is not checked.  `TRecognitionManager::Init`
// builds it only above level 1, but calls `InitRecognizers` - and so
// `ReadDomainOptions` and this - at every level, so a machine started at
// level 1 throws here on a `vars.dictionaries` that was never made.  The
// MP2x00 always starts at level 2, so nobody ever saw it.
void
ReadDictPrefs(void)
{
	RefVar list(Dictionaries());
	RefVar intl(IntlResources());
	RefVar bundle(GetProtoVariable(intl, RSSYMcurrentlocalebundle, nil));
	long count = Length(list);
	for (long i = 0; i < count; i++)
		ReplaceLocalDictionary(bundle, RefVar(GetArraySlotRef(list, i)));
}


/*------------------------------------------------------------------------------
	T h e   c h a i n
------------------------------------------------------------------------------*/

// ROM 0x0020cab0 __ct__10TDictChainFv
TDictChain::TDictChain()
{
}


// ROM 0x0020cb44 IDictChain__10TDictChainFUlT1
long
TDictChain::IDictChain(ULong count, ULong position)
{
	fData = nil;
	long err = IDArray(sizeof(Handle), count);
	if (err == 0)
		fPosition = (long) position;
	return err;
}


// ROM 0x0020caf0 Make__10TDictChainSFUlT1
TDictChain*
TDictChain::Make(ULong count, ULong position)
{
	TDictChain* chain = new TDictChain;
	if (chain != nil && chain->IDictChain(count, position) != 0)
	{
		chain->Dispose();
		chain = nil;
	}
	return chain;
}


// ROM 0x0020cbf0 PositionToHandle__10TDictChainFUl
Handle
TDictChain::PositionToHandle(ULong position)
{
	Handle* entry = (Handle*) GetEntry(position);
	return entry == nil ? nil : *entry;
}


// ROM 0x0020cc18 HandleToPosition__10TDictChainFPP15AirusAParmBlock
ULong
TDictChain::HandleToPosition(Handle dictionary)
{
	Lock();
	ULong at = 0;
	for (; at < (ULong) fCount; at++)
		if (*(Handle*) GetEntry(at) == dictionary)
			break;
	Unlock();
	return at;
}


// ROM 0x0020cbc8 AddDictToChain__10TDictChainFPP15AirusAParmBlock
void
TDictChain::AddDictToChain(Handle dictionary)
{
	InsertEntry((ULong) fCount, (const char*) &dictionary);
}


// ROM 0x0020cb7c RemoveDictFromChain__10TDictChainFPP15AirusAParmBlock
long
TDictChain::RemoveDictFromChain(Handle dictionary)
{
	ULong at = HandleToPosition(dictionary);
	Delete(at);
	if (fPosition == (long) at)
		fPosition = -1;
	return 0;
}


// ROM 0x0013d628 AddToChain__FPP10TDictChainP13dictListEntry
// A dictionary put into whichever of the three chains its frame says it
// belongs in - and then the dictionary that one is linked to, and the
// one after that, for up to a hundred links or until the ring closes.
//
// A dictionary already in the chain is not added twice.  Which chain a
// dictionary goes in comes out of its `dictType`: 0 the ordinary ones,
// 1 the ones only consulted for particular fields, 4 the exceptions; a
// frame that says anything else stops the walk.
void
AddToChain(TDictChain** chains, dictListEntry* entry)
{
	UByte first = entry->fIndex;
	long links = 0;
	for (;;)
	{
		// DEVIATION: the ROM copies the seven bytes of the entry that
		// matter, because the next FindDictionaryEntry may move the list
		// out from under it; here a Handle is pointer-sized, so the whole
		// entry is copied instead of the ROM's seven bytes.
		dictListEntry copy = *entry;
		RefVar frame(GetArraySlotRef(RefVar(Dictionaries()), copy.fIndex));
		long which;
		switch (RINT(RefVar(GetProtoVariable(frame, RSSYMdicttype, nil))))
		{
		case 0:		which = kDictChainOrdinary;		break;
		case 1:		which = kDictChainSpecial;		break;
		case 4:		which = kDictChainException;	break;
		default:	return;
		}

		if (copy.fDictionary != nil)
		{
			TDictChain* chain = chains[which];
			Boolean already = false;
			if (chain == nil)
			{
				chain = TDictChain::Make(0, 0xffffffff);
				if (chain == nil)
					return;
				chains[which] = chain;
			}
			else
			{
				for (ULong i = 0; i < (ULong) chain->fCount; i++)
					if (*(Handle*) chain->GetEntry(i) == copy.fDictionary)
					{
						already = true;
						break;
					}
			}
			if (!already)
				*(Handle*) chain->GetEntry((ULong) chain->Add()) = copy.fDictionary;
		}

		RefVar linked(GetProtoVariable(frame, RSSYMlinkeddictid, nil));
		if (ISNIL(linked))
			break;
		entry = FindDictionaryEntry((ULong) RINT(linked));
		if (entry == nil || entry->fIndex == first)
			break;
		if (++links >= 100)
			break;
	}
}


// ROM 0x0013fa44 CountCustomDictionaries__FRC6RefVar
// How many dictionaries a configuration names by hand.  One that names a
// single dictionary rather than an array of them counts as one.
long
CountCustomDictionaries(RefArg config)
{
	RefVar named(GetProtoVariable(config, RSSYMdictionaries, nil));
	if (ISNIL(named))
		return 0;
	return ISINT(named) ? 1 : Length(named);
}


// ROM 0x0013fa9c GetCustomDictionary__FRC6RefVarUl
ULong
GetCustomDictionary(RefArg config, ULong index)
{
	RefVar named(GetProtoVariable(config, RSSYMdictionaries, nil));
	if (ISINT(named))
		return (ULong) RINT(named);
	return (ULong) RINT(RefVar(GetArraySlotRef(named, (long) index)));
}


// ROM 0x0013db74 CompactChains__FPP10TDictChain
void
CompactChains(TDictChain** chains)
{
	for (long i = 0; i < kDictChainCount; i++)
		if (chains[i] != nil)
			chains[i]->Compact();
}


// ROM 0x0013dbac DoneChains__FPP10TDictChain
void
DoneChains(TDictChain** chains)
{
	for (long i = 0; i < kDictChainCount; i++)
		if (chains[i] != nil)
		{
			chains[i]->Dispose();
			chains[i] = nil;
		}
}


// ROM 0x0013d808 BuildChains__FPP10TDictChainRC6RefVar
// The three chains filled in for one lookup: the dictionaries the
// configuration names by hand first, so that a field's own dictionary is
// asked before the general ones; then every dictionary of the list whose
// `domainType` overlaps the configuration's input mask; and then the
// symbols dictionary, unless the configuration says to leave it out.
void
BuildChains(TDictChain** chains, RefArg config)
{
	ULong mask = (ULong) RINT(RefVar(GetVariable(config, RSSYMinputmask, nil, 0)));
	for (long i = 0; i < kDictChainCount; i++)
		chains[i] = nil;

	long custom = CountCustomDictionaries(config);
	for (long i = 0; i < custom; i++)
	{
		dictListEntry* entry = FindDictionaryEntry(GetCustomDictionary(config, (ULong) i));
		if (entry != nil)
			AddToChain(chains, entry);
	}

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
		if ((domain & 0x1fff000 & mask) != 0)
			AddToChain(chains, entry);
	}

	if (ISNIL(RefVar(GetProtoVariable(config, RSSYMinhibitsymbolsdictionary, nil))))
	{
		dictListEntry* entry = FindDictionaryEntry(0x28);
		if (entry != nil)
			AddToChain(chains, entry);
	}
	CompactChains(chains);
}


// ROM 0x0013d9dc BuildChains__FPP10TDictChain
// The same for whatever is being written on now: the recognition
// configuration of the view the caret is in, or - when there is none, or
// it takes ink rather than words - the starter `rcBuildChains` with the
// view's input mask and its own dictionaries written into it.
void
BuildChains(TDictChain** chains)
{
	RefVar config;
	ULong mask = 0;
	TView* view = nil;
	if (gRootView != nil && gRootView->fCaretView != nil)
		view = GetRecognitionView(gRootView->fCaretView);
	if (view != nil)
	{
		mask = view->fFlags & 0x1ffff00;
		config = GetProtoVariable(RefVar(view->fContext), RSSYMrecconfig, nil);
		if (NOTNIL(config))
		{
			if (InkTextEnabled(view, mask, config))
				config = NILREF;
			else
			{
				config = PrepRecConfig(view, config);
				SetFrameSlot(config, RSSYMinhibitsymbolsdictionary, RefVar(TRUEREF));
			}
		}
	}
	if (ISNIL(config))
	{
		config = Clone(RefVar(Rrcbuildchains));
		if (view != nil && mask != 0)
		{
			SetFrameSlot(config, RSSYMinputmask, RefVar(MAKEINT(mask)));
			SetFrameSlot(config, RSSYMdictionaries,
						 RefVar(view->GetVar(RSSYMdictionaries)));
		}
	}
	BuildChains(chains, config);
}


/*------------------------------------------------------------------------------
	L o o k i n g   a   w o r d   u p
------------------------------------------------------------------------------*/

// ROM 0x0013f430 LookupWordInChain__FPUcP10TDictChainPUl
// Each dictionary of the chain asked in turn, and the first that says
// anything better than "no" wins.  "The beginning of other words" is not
// an answer: what is being asked is whether the word is a word.
//
// ==> the id of the dictionary it was found in, -1 for none.
long
LookupWordInChain(const UByte* word, TDictChain* chain, ULong* attribute)
{
	if (chain == nil)
		return -1;
	ULong count = (ULong) chain->fCount;
	for (ULong i = 0; i < count; i++)
	{
		Handle dictionary = *(Handle*) chain->GetEntry(i);
		void* terminal = nil;
		ULong* found = nil;
		VerifyString(dictionary, word, &terminal, &found, nil);
		if (airusResult != kAirusNotAWord && airusResult != kAirusIsPrefix)
		{
			long id = ((AirusAParmBlock*) *dictionary)->fDictID;
			if (found != nil)
				*attribute = *found;
			return id;
		}
	}
	return -1;
}


// ROM 0x0013f4f4 LookupWord__FPUsPUl
// Is this a word?  The chains are built for whatever is being written on,
// the word is brought down to eight-bit characters, and the ordinary
// chain is asked and then the exceptions.
long
LookupWord(const UniChar* word, ULong* attribute)
{
	TDictChain* chains[kDictChainCount];
	UByte bytes[64];
	*attribute = 0;
	BuildChains(chains);
	ConvertFromUnicode(word, bytes, 1, 0x3f);
	long found = LookupWordInChain(bytes, chains[kDictChainOrdinary], attribute);
	if (found == -1)
		found = LookupWordInChain(bytes, chains[kDictChainException], attribute);
	DoneChains(chains);
	return found;
}


// ROM 0x0013f2fc BuildCaseVariant__FPUsUlT2T1
// The index'th capitalisation of a word, built on top of the one before
// it - so the caller walks the index up until this answers no.
//
// Nought is the word as it was written.  After that, what is tried
// depends on the flags: with 0x40, everything up to and including the
// first letter that was already lower case is lowered and that letter
// raised; with 0x80, and only once, the letters are lowered one at a
// time until one of them changes.  Sixteen is as far as it goes.
//
// (The 0x40 variant of a word like "Hello" comes out as "hEllo", which
//  is not a capitalisation anyone would write.  It is what the ROM
//  builds, and it is looked up like any other.)
Boolean
BuildCaseVariant(const UniChar* word, ULong flags, ULong index, UniChar* out)
{
	if (index > 0x10)
		return false;
	if (index == 0)
	{
		Ustrcpy(out, word);
		return true;
	}
	if ((flags & 0x40) != 0)
	{
		for (long i = 0; out[i] != 0; i++)
		{
			UniChar was = out[i];
			if (!IsAlphabet(was))
				continue;
			LowercaseText(out + i, 1);
			if (out[i] == was)
			{
				UppercaseText(out + i, 1);
				if (out[i] != was)
					break;
			}
		}
		return Ustrcmp(word, out) != 0;
	}
	if ((flags & 0x80) != 0 && index == 1)
	{
		for (long i = 0; out[i] != 0; i++)
		{
			LowercaseText(out + i, 1);
			if (out[i] != word[i])
				return true;
		}
	}
	return false;
}


// ROM 0x0013f570 LookupWordOrVariant__FPUsPUlT1
// The word looked up, and then its capitalisations one at a time until
// one of them is a word or there are no more.  `attribute` carries the
// flags in and the answer out; `variant` comes back with the spelling
// that was found.
long
LookupWordOrVariant(const UniChar* word, ULong* attribute, UniChar* variant)
{
	TDictChain* chains[kDictChainCount];
	UByte bytes[64];
	ULong flags = *attribute;
	long found = -1;
	*attribute = 0;
	BuildChains(chains);
	for (ULong index = 0; ; index++)
	{
		if (!BuildCaseVariant(word, flags, index, variant))
			break;
		ConvertFromUnicode(variant, bytes, 1, 0x3f);
		found = LookupWordInChain(bytes, chains[kDictChainOrdinary], attribute);
		if (found == -1)
			found = LookupWordInChain(bytes, chains[kDictChainException], attribute);
		if (found != -1)
			break;
	}
	DoneChains(chains);
	return found;
}


/*------------------------------------------------------------------------------
	W h a t   a   s c r i p t   a s k s   o f   t h e   d i c t i o n a r i e s
------------------------------------------------------------------------------*/

// ROM 0x0008eb04 FAirusResult
// dict:result() - what the last call into the dictionary engine left
// behind (Airus.h's airusResult): 0 for done, -15 for a dictionary as
// full as its limit, and so on.
static Ref
FAirusResult(RefArg /*rcvr*/)
{
	return MAKEINT(airusResult);
}


// ROM 0x0007d484 DumpDict__FRC6RefVar
// DumpDict(): the Assistant's dynamic dictionary frame, whatever it
// holds.  The ROM's function ignores its argument and answers that one
// global.
//
// NOT YET RECONSTRUCTED: gDynaDictionaryFrame, which the Assistant's
// lexical side fills in; nil until there is one.
static Ref
FDumpDict(RefArg /*rcvr*/)
{
	return NILREF;
}


void
RegisterDictionaryNatives(void)
{
	RegisterNativeFunction("FAirusResult", (void*) FAirusResult, 0);
	RegisterNativeFunction("DumpDict__FRC6RefVar", (void*) FDumpDict, 0);
}
