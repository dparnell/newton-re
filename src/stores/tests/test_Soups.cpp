// Stores and soups test: a THostStore registered as a store frame (its
// root object formatted, its persistent frame, name and signature), a soup
// created with string and int indexes, entries added (fault blocks in the
// soup's cache, _uniqueIDs, mod times, the indexes), read back through
// their fault blocks, changed (the indexes updated), removed, flushed;
// indexes added and removed; the store re-registered over the same bytes
// finding the soup and its entries again; soup renaming, info, removal
// and the store's soup names; the same through NewtonScript sends to the
// prototype frames' methods; cursors over the soup's indexes (key
// bounds, tests, moves, counting, following changes and removals, the
// collect cursor).  Runs over a standalone kernel heap and object heap.

#include "Soups.h"
#include "hal/System.h"
#include "Cursors.h"
#include "Tags.h"
#include "Entries.h"
#include "StoreObject.h"
#include "Compiler.h"
#include "host/HostStore.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "RSSymbols.h"
#include "NSErrors.h"
#include "REPTranslators.h"
#include "memory/host/KernelHeap.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"
#include "Unicode.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

extern const ExceptionName exStoreError;

static Ref SYMBOL(const char* name) { return Intern((char*) name); }


static TStore*
NewStore(ULong size = 0x80000)
{
	TStore* store = (TStore*) THostStore::ClassInfo()->New();
	EXPECT(store != nil);
	EXPECT(store->Init(nil, size, 0, 0, kStoreIsInternal, nil) == noErr);
	EXPECT(store->Format() == noErr);
	return store;
}


static Ref
Eval(const char* source)
{
	if (getenv("EVAL_TRACE")) fprintf(stderr, "eval: %s\n", source);
	RefVar fn(ParseString(RefVar(MakeString(source))));
	return InterpretBlock(fn, RefVar(gVarFrame));
}


static Boolean
StringIs(RefArg str, const char* text)
{
	if (!IsString(str))
		return false;
	UniChar* chars = GetCString(str);
	for (long i = 0; ; i++)
	{
		if (chars[i] != (UniChar) (unsigned char) text[i])
			return false;
		if (text[i] == 0)
			return true;
	}
}


// {structure: 'slot, path: path, type: type}
static Ref
IndexSpec(const char* path, const char* type)
{
	RefVar spec(AllocateFrame());
	SetFrameSlot(spec, RSSYMstructure, RSSYMslot);
	SetFrameSlot(spec, RSSYMpath, RefVar(SYMBOL(path)));
	SetFrameSlot(spec, RSSYMtype, RefVar(SYMBOL(type)));
	return spec;
}


static Ref
Person(const char* name, long age)
{
	RefVar frame(AllocateFrame());
	SetFrameSlot(frame, RSSYMname, RefVar(MakeString(name)));
	SetFrameSlot(frame, RefVar(SYMBOL("age")), RefVar(MAKEINT(age)));
	return frame;
}


// the entries of a soup in _uniqueID order
static long
CountEntries(RefArg soup, long* firstUID = nil)
{
	SKey key, data;
	key.Clear();
	data.Clear();
	long count = 0;
	int result = GetSoupIndexObject(soup, 0)->First(&key, &data);
	while (result == kIndexOK)
	{
		if (count == 0 && firstUID != nil)
			*firstUID = (long) key;
		count++;
		result = GetSoupIndexObject(soup, 0)->Next(&key, &data, kIndexNextKey, &key, &data);
	}
	return count;
}


static void
TestStoreFrame()
{
	TStore* store = NewStore();
	EXPECT(Length(gStores) == 0);
	RefVar storeObject(RegisterTStore(store));
	EXPECT(Length(gStores) == 1 && EQRef(GetArraySlotRef(gStores, 0), storeObject));
	EXPECT(IsValidStore(store));
	EXPECT(EQRef(ToObject(store), storeObject));
	EXPECT(StoreIsValid(storeObject) == TRUEREF);
	EXPECT(StoreIsReadOnly(storeObject) == NILREF);
	EXPECT(GetStoreWrapper(storeObject)->Store() == store);
	EXPECT(StringIs(RefVar(StoreGetName(storeObject)), "Untitled"));
	EXPECT(StringIs(RefVar(StoreGetKind(storeObject)), store->StoreKind()));
	// a store that is not the machine's own is signed at random
	EXPECT(ISINT(StoreGetSignature(storeObject)) && RINT(StoreGetSignature(storeObject)) != 0);
	{
		// ... and the internal one with the machine's serial number, its
		// second word, which is what the ROM's boot checks it against
		TStore* internal = (TStore*) THostStore::ClassInfo()->New();
		EXPECT(internal != nil && internal->Init(nil, 0x8000, 0, 0, kStoreIsInternal, nil) == noErr);
		SetInternalStore(internal);
		EXPECT(GetInternalStore() == internal && internal->Format() == noErr);
		RefVar internalObject(MakeStoreObject(internal));
		ULong serialNumber[2];
		EXPECT(GetSystemSerialNumber(serialNumber) == noErr);
		EXPECT(RINT(StoreGetSignature(internalObject)) == (long) serialNumber[1]);
		SetInternalStore(nil);
	}
	EXPECT(RINT(StoreTotalSize(storeObject)) == 0x80000);
	EXPECT(RINT(StoreUsedSize(storeObject)) > 0 && RINT(StoreUsedSize(storeObject)) < 0x2000);
	EXPECT(RINT(StoreOverhead(storeObject)) > 0);
	long version = 0;
	EXPECT(GetStoreVersion(store, &version) == noErr && version == kStoreRootVersion);
	EXPECT(RINT(GetFrameSlotRef(storeObject, RSSYMversion)) == kStoreRootVersion);
	EXPECT(Length(RefVar(StoreGetSoupNames(storeObject))) == 0);
	EXPECT(StoreHasSoup(storeObject, RefVar(MakeString("Names"))) == NILREF);
	EXPECT(StoreGetSoup(storeObject, RefVar(MakeString("Names"))) == NILREF);

	// the persistent frame is a fault block on the root object
	RefVar persistent(GetFrameSlotRef(storeObject, RSSYM_proto));
	EXPECT(IsFaultBlock(persistent));
	StoreSetName(storeObject, RefVar(MakeString("Test store")));
	EXPECT(StringIs(RefVar(StoreGetName(storeObject)), "Test store"));
	StoreSetInfo(storeObject, RefVar(SYMBOL("owner")), RefVar(MakeString("me")));
	EXPECT(StringIs(RefVar(StoreGetInfo(storeObject, RefVar(SYMBOL("owner")))), "me"));
	EXPECT(StoreGetInfo(storeObject, RefVar(SYMBOL("other"))) == NILREF);
	RefVar allInfo(StoreGetAllInfo(storeObject));
	EXPECT(IsFrame(allInfo) && FrameHasSlotRef(allInfo, SYMBOL("owner")));
	EXPECT(StoreLock(storeObject) == TRUEREF && StoreUnlock(storeObject) == NILREF);

	// the frame slot methods through NewtonScript
	SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("theStore")), storeObject);
	EXPECT(StringIs(RefVar(Eval("theStore:GetName()")), "Test store"));
	EXPECT(Eval("theStore:HasSoup(\"Names\")") == NILREF);
	EXPECT(Eval("theStore:IsValid()") == TRUEREF);

	// the low-level object methods
	RefVar id(StoreNewObject(storeObject, RefVar(MAKEINT(6))));
	EXPECT(RINT(StoreGetObjectSize(storeObject, id)) == 6);
	RefVar data(AllocateBinary(RSSYMbinary, 6));
	memcpy(BinaryData(data), "abcdef", 6);
	StoreWriteWholeObject(storeObject, id, data, RefVar(NILREF), RefVar(NILREF));
	// ReadObject says how much it wants and from where
	RefVar back(StoreReadObject(storeObject, id, RefVar(MAKEINT(6)), RefVar(MAKEINT(0))));
	EXPECT(Length(back) == 6 && memcmp(BinaryData(back), "abcdef", 6) == 0);
	RefVar tail(StoreReadObject(storeObject, id, RefVar(MAKEINT(3)), RefVar(MAKEINT(3))));
	EXPECT(Length(tail) == 3 && memcmp(BinaryData(tail), "def", 3) == 0);
	// WriteObject writes a run of bytes at an offset
	RefVar two(AllocateBinary(RSSYMbinary, 2));
	memcpy(BinaryData(two), "XY", 2);
	StoreWriteObject(storeObject, id, two, RefVar(MAKEINT(2)), RefVar(MAKEINT(1)));
	back = StoreReadObject(storeObject, id, RefVar(MAKEINT(6)), RefVar(MAKEINT(0)));
	EXPECT(memcmp(BinaryData(back), "aXYdef", 6) == 0);
	StoreDeleteObject(storeObject, id);

	// killed and gone
	RemoveTStore(store);
	EXPECT(Length(gStores) == 0);
	EXPECT(GetFrameSlotRef(storeObject, RSSYM_proto) == NILREF);
	EXPECT(StoreIsValid(storeObject) == NILREF);
	Boolean threw = false;
	newton_try
	{
		StoreGetName(storeObject);
	}
	newton_catch(exStoreError)
	{
		threw = (long) (Long) _info.exception.data == kNSErrInvalidStore;
	}
	end_try;
	EXPECT(threw);
	store->Delete();
}


static void
TestSoups()
{
	TStore* store = NewStore();
	RefVar storeObject(RegisterTStore(store));
	RefVar name(MakeString("Names"));

	// a soup with string and int indexes
	RefVar specs(AllocateArray(RSSYMarray, 2));
	SetArraySlotRef(specs, 0, IndexSpec("name", "string"));
	SetArraySlotRef(specs, 1, IndexSpec("age", "int"));
	RefVar soup(StoreCreateSoup(storeObject, name, specs));
	EXPECT(IsFrame(soup));
	EXPECT(StoreHasSoup(storeObject, name) == TRUEREF);
	EXPECT(EQRef(StoreGetSoup(storeObject, RefVar(MakeString("names"))), soup));		// cached, case-insensitively
	EXPECT(StoreGetSoupId(storeObject, name) != 0);
	EXPECT(StringIs(RefVar(SoupGetName(soup)), "Names"));
	EXPECT(EQRef(SoupGetStore(soup), storeObject));
	EXPECT(RINT(SoupGetNextUID(soup)) == 0);
	EXPECT(Length(RefVar(SoupGetIndexes(soup))) == 2);
	EXPECT(SoupIsValid(soup) == TRUEREF);
	EXPECT(ISINT(SoupGetSignature(soup)));
	RefVar names(StoreGetSoupNames(storeObject));
	EXPECT(Length(names) == 1 && StringIs(RefVar(GetArraySlotRef(names, 0)), "Names"));
	RefVar persistent(GetFrameSlotRef(soup, RSSYM_proto));
	EXPECT(IsFaultBlock(persistent));
	EXPECT(Length(RefVar(GetFrameSlotRef(persistent, RSSYMindexes))) == 3);		// _uniqueID first
	// a second soup of the same name
	Boolean threw = false;
	newton_try
	{
		StoreCreateSoup(storeObject, RefVar(MakeString("NAMES")), RefVar(NILREF));
	}
	newton_catch(exStoreError)
	{
		threw = (long) (Long) _info.exception.data == kNSErrDuplicateSoupName;
	}
	end_try;
	EXPECT(threw);

	// entries
	RefVar entry(SoupAdd(soup, RefVar(Person("Bob", 30))));
	EXPECT(IsFaultBlock(entry));
	EXPECT(EntryIsResident(entry));
	EXPECT(RINT(GetFrameSlotRef(entry, RSSYM_uniqueid)) == 0);
	EXPECT(ISINT(GetFrameSlotRef(entry, RSSYM_modtime)));
	EXPECT(EntryUniqueID(entry) == 0);
	EXPECT(EntryModTime(entry) == RINT(GetFrameSlotRef(entry, RSSYM_modtime)));
	EXPECT(EQRef(EntrySoup(entry), soup));
	EXPECT(EQRef(EntryStore(entry), storeObject));
	EXPECT(IsSoupEntry(entry) && EntryValid(entry) && !IsProxyEntry(entry));
	EXPECT(EntrySize(entry) > 0x10 && EntrySize(entry) == EntrySizeWithoutVBOs(entry));
	EXPECT(RINT(SoupGetNextUID(soup)) == 1);
	RefVar entry2(SoupAdd(soup, RefVar(Person("alice", 25))));
	RefVar entry3(SoupAdd(soup, RefVar(Person("Carol", 35))));
	EXPECT(RINT(GetFrameSlotRef(entry3, RSSYM_uniqueid)) == 2);
	EXPECT(CountEntries(soup) == 3);
	EXPECT(EQRef(GetEntry(soup, FaultBlockId(entry2)), entry2));		// from the cache
	// the name index: collated, so alice < Bob < Carol
	RefVar indexes(GetFrameSlotRef(persistent, RSSYMindexes));
	RefVar nameDesc(IndexPathToIndexDesc(persistent, RefVar(SYMBOL("name")), nil));
	EXPECT((Ref) nameDesc != NILREF);
	TSoupIndex* nameIndex = GetSoupIndexObject(soup, (PSSId) RINT(GetFrameSlotRef(nameDesc, RSSYMindex)));
	EXPECT(nameIndex != nil);
	SKey key, data;
	key.Clear();
	data.Clear();
	EXPECT(nameIndex->First(&key, &data) == kIndexOK);
	EXPECT(StringIs(RefVar(SKeyToKey(key, RSSYMstring, nil)), "alice"));
	EXPECT((PSSId) (long) data == FaultBlockId(entry2));
	EXPECT(nameIndex->Next(&key, &data, kIndexNextKey, &key, &data) == kIndexOK);
	EXPECT(StringIs(RefVar(SKeyToKey(key, RSSYMstring, nil)), "Bob"));
	EXPECT(nameIndex->Next(&key, &data, kIndexNextKey, &key, &data) == kIndexOK);
	EXPECT(StringIs(RefVar(SKeyToKey(key, RSSYMstring, nil)), "Carol"));
	EXPECT(nameIndex->Next(&key, &data, kIndexNextKey, &key, &data) == kIndexEnd);
	// MakeKey
	EXPECT(StringIs(RefVar(PlainSoupMakeKey(soup, RefVar(MakeString("Zed")), RefVar(SYMBOL("name")))), "Zed"));
	EXPECT(RINT(PlainSoupMakeKey(soup, RefVar(MAKEINT(7)), RefVar(SYMBOL("age")))) == 7);
	threw = false;
	newton_try
	{
		PlainSoupMakeKey(soup, RefVar(MakeString("x")), RefVar(SYMBOL("age")));
	}
	newton_catch(exStoreError)
	{
		threw = (long) (Long) _info.exception.data == kNSErrKeyTypeMismatch;
	}
	end_try;
	EXPECT(threw);

	// the entry read back from the store through a fresh fault block
	TStoreWrapper* wrapper = GetStoreWrapper(storeObject);
	PSSId bobId = FaultBlockId(entry);
	RefVar fresh(MakeFaultBlock(soup, wrapper, bobId));
	EXPECT(!EntryIsResident(fresh));
	EXPECT(StringIs(RefVar(GetFrameSlotRef(fresh, RSSYMname)), "Bob"));		// FollowFaultBlock
	EXPECT(EntryIsResident(fresh));
	EXPECT(RINT(GetFrameSlotRef(fresh, SYMBOL("age"))) == 30);

	// a change: the index follows the key
	SetFrameSlot(entry, RefVar(SYMBOL("age")), RefVar(MAKEINT(31)));
	SetFrameSlot(entry, RSSYMname, RefVar(MakeString("Robert")));
	EXPECT(EntryDirty(entry));
	long modTime = EntryModTime(entry);
	EntryChange(entry);
	RefVar stored(LoadPermObject(wrapper, bobId, nil));
	EXPECT(RINT(GetFrameSlotRef(stored, SYMBOL("age"))) == 31);
	EXPECT(StringIs(RefVar(GetFrameSlotRef(stored, RSSYMname)), "Robert"));
	EXPECT(EntryModTime(entry) >= modTime);
	key.Clear();
	KeyToSKey(RefVar(MakeString("Bob")), RSSYMstring, &key, nil, nil);
	EXPECT(nameIndex->Find(&key, &key, &data, true) != kIndexOK);
	KeyToSKey(RefVar(MakeString("Robert")), RSSYMstring, &key, nil, nil);
	EXPECT(nameIndex->Find(&key, &key, &data, true) == kIndexOK && (PSSId) (long) data == bobId);
	// a flush: the entry frame leaves memory
	SetFrameSlot(entry, RefVar(SYMBOL("age")), RefVar(MAKEINT(32)));
	EntryFlush(entry);
	EXPECT(!EntryIsResident(entry));
	EXPECT(RINT(GetFrameSlotRef(entry, SYMBOL("age"))) == 32);
	EXPECT(EntryIsResident(entry));
	// undo: the store's copy stands
	SetFrameSlot(entry, RefVar(SYMBOL("age")), RefVar(MAKEINT(99)));
	EntryUndoChanges(entry);
	EXPECT(RINT(GetFrameSlotRef(entry, SYMBOL("age"))) == 32);
	// the soup's size counts the entries and indexes
	EXPECT(RINT(PlainSoupGetSize(soup)) > 3 * 0x10);
	EXPECT(Length(RefVar(PlainSoupIndexSizes(soup))) == 3);

	// through NewtonScript
	SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("theSoup")), soup);
	SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("theStore")), storeObject);
	EXPECT(StringIs(RefVar(Eval("theSoup:GetName()")), "Names"));
	EXPECT(RINT(Eval("theSoup:GetNextUID()")) == 3);
	RefVar added(Eval("theSoup:Add({name: \"Dave\", age: 40})"));
	EXPECT(IsFaultBlock(added) && RINT(GetFrameSlotRef(added, RSSYM_uniqueid)) == 3);
	EXPECT(RINT(Eval("theStore:GetSoup(\"Names\"):GetNextUID()")) == 4);
	EXPECT(Length(RefVar(Eval("theSoup:GetIndexes()"))) == 2);
	EXPECT(Length(RefVar(Eval("theStore:GetSoupNames()"))) == 1);
	EXPECT(CountEntries(soup) == 4);
	// the NewtonScript functions
	EXPECT(EQRef(Eval("GetStores()[0]"), storeObject));
	SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("dave")), added);
	EXPECT(Eval("IsSoupEntry(dave)") == TRUEREF && Eval("IsSoupEntry(theSoup)") == NILREF);
	EXPECT(RINT(Eval("EntryUniqueID(dave)")) == 3);
	EXPECT(EQRef(Eval("EntrySoup(dave)"), soup) && EQRef(Eval("EntryStore(dave)"), storeObject));
	EXPECT(Eval("EntryValid(dave)") == TRUEREF && Eval("EntryIsResident(dave)") == TRUEREF);
	Eval("dave.age := 41; EntryChange(dave)");
	EXPECT(RINT(GetFrameSlotRef(RefVar(LoadPermObject(wrapper, FaultBlockId(added), nil)), SYMBOL("age"))) == 41);
	Eval("EntryFlush(dave)");
	EXPECT(Eval("EntryIsResident(dave)") == NILREF);
	RefVar alias(Eval("MakeEntryAlias(dave)"));
	EXPECT(Eval("IsEntryAlias(dave)") == NILREF);
	SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("theAlias")), alias);
	EXPECT(Eval("IsEntryAlias(theAlias)") == TRUEREF);
	EXPECT(EQRef(Eval("ResolveEntryAlias(theAlias)"), added));
	EXPECT(Eval("IsSameEntry(theAlias, dave)") == TRUEREF && Eval("IsSameEntry(dave, theAlias)") == TRUEREF);
	EXPECT(Eval("IsSameEntry(theAlias, MakeEntryAlias(dave))") == TRUEREF);
	EXPECT(RINT(Eval("EntrySize(dave)")) > 0x10);
	EXPECT(IsFaultBlock(RefVar(Eval("EntryCopy(dave, theSoup)"))));
	EXPECT(CountEntries(soup) == 5);
	Eval("EntryRemoveFromSoup(dave)");
	EXPECT(CountEntries(soup) == 4 && Eval("IsSoupEntry(dave)") == NILREF);
	EXPECT(Eval("ResolveEntryAlias(theAlias)") == NILREF);
	Eval("theSoup:AddWithUniqueID(dave)");
	EXPECT(CountEntries(soup) == 5 && EQRef(Eval("ResolveEntryAlias(theAlias)"), added));
	Eval("EntryRemoveFromSoup(dave)");

	// removal: the entry becomes a plain frame, out of the indexes
	EntryRemoveFromSoup(entry2);
	EXPECT(!IsFaultBlock(entry2) && IsFrame(entry2));
	EXPECT(StringIs(RefVar(GetFrameSlotRef(entry2, RSSYMname)), "alice"));
	EXPECT(CountEntries(soup) == 3);
	KeyToSKey(RefVar(MakeString("alice")), RSSYMstring, &key, nil, nil);
	EXPECT(nameIndex->Find(&key, &key, &data, true) != kIndexOK);
	EXPECT(RINT(SoupGetNextUID(soup)) == 5);
	// a copy and a move to another soup
	RefVar soup2(StoreCreateSoup(storeObject, RefVar(MakeString("Others")), RefVar(NILREF)));
	RefVar copy(EntryCopy(entry3, soup2));
	EXPECT(IsFaultBlock(copy) && EQRef(EntrySoup(copy), soup2));
	EXPECT(CountEntries(soup2) == 1 && CountEntries(soup) == 3);
	EntryMove(entry, soup2);
	EXPECT(CountEntries(soup2) == 2 && CountEntries(soup) == 2);
	EXPECT(IsFaultBlock(entry) && EQRef(EntrySoup(entry), soup2));		// the block now stands for the moved entry
	EXPECT(StringIs(RefVar(GetFrameSlotRef(entry, RSSYMname)), "Robert"));
	KeyToSKey(RefVar(MakeString("Robert")), RSSYMstring, &key, nil, nil);
	EXPECT(nameIndex->Find(&key, &key, &data, true) != kIndexOK);

	// indexes added and removed
	RefVar ageDesc(IndexPathToIndexDesc(persistent, RefVar(SYMBOL("age")), nil));
	EXPECT((Ref) ageDesc != NILREF);
	SoupRemoveIndex(soup, RefVar(SYMBOL("age")));
	EXPECT(IndexPathToIndexDesc(persistent, RefVar(SYMBOL("age")), nil) == NILREF);
	EXPECT(Length(RefVar(SoupGetIndexes(soup))) == 1);
	threw = false;
	newton_try
	{
		SoupRemoveIndex(soup, RSSYM_uniqueid);
	}
	newton_catch(exStoreError)
	{
		threw = (long) (Long) _info.exception.data == kNSErrCantRemoveUniqueIDIndex;
	}
	end_try;
	EXPECT(threw);
	SoupAdd(soup, RefVar(Person("Eve", 22)));
	SoupAddIndex(soup, RefVar(IndexSpec("age", "int")));
	ageDesc = IndexPathToIndexDesc(persistent, RefVar(SYMBOL("age")), nil);
	EXPECT((Ref) ageDesc != NILREF);
	TSoupIndex* ageIndex = GetSoupIndexObject(soup, (PSSId) RINT(GetFrameSlotRef(ageDesc, RSSYMindex)));
	key = 22L;
	EXPECT(ageIndex->Find(&key, &key, &data, true) == kIndexOK);
	key = 35L;
	EXPECT(ageIndex->Find(&key, &key, &data, true) == kIndexOK);		// Carol was indexed too
	EXPECT(ageIndex->TotalSize() > 0);

	// info and renaming
	SoupSetInfo(soup, RefVar(SYMBOL("version")), RefVar(MAKEINT(2)));
	EXPECT(RINT(SoupGetInfo(soup, RefVar(SYMBOL("version")))) == 2);
	EXPECT(ISINT(SoupGetInfoModTime(soup)) && ISINT(SoupGetIndexesModTime(soup)));
	SoupSetName(soup, RefVar(MakeString("People")));
	EXPECT(StringIs(RefVar(SoupGetName(soup)), "People"));
	EXPECT(StoreHasSoup(storeObject, RefVar(MakeString("Names"))) == NILREF);
	EXPECT(StoreHasSoup(storeObject, RefVar(MakeString("People"))) == TRUEREF);
	names = StoreGetSoupNames(storeObject);
	EXPECT(Length(names) == 2);

	// the store re-registered over the same bytes: everything is still there
	RemoveTStore(store);
	EXPECT(GetFrameSlotRef(soup, RSSYM_proto) == NILREF && SoupIsValid(soup) == NILREF);
	storeObject = RegisterTStore(store);
	EXPECT(StringIs(RefVar(StoreGetName(storeObject)), "Untitled"));
	names = StoreGetSoupNames(storeObject);
	EXPECT(Length(names) == 2);
	soup = StoreGetSoup(storeObject, RefVar(MakeString("people")));
	EXPECT((Ref) soup != NILREF);
	EXPECT(StringIs(RefVar(SoupGetName(soup)), "People"));
	EXPECT(RINT(SoupGetNextUID(soup)) == 6);		// one past the last _uniqueID (Eve's 5)
	EXPECT(Length(RefVar(SoupGetIndexes(soup))) == 2);
	EXPECT(RINT(SoupGetInfo(soup, RefVar(SYMBOL("version")))) == 2);
	long firstUID = -1;
	EXPECT(CountEntries(soup, &firstUID) == 3 && firstUID == 2);		// Carol, the copy of Dave, Eve
	RefVar carol(GetEntry(soup, (PSSId) (long) data));
	key = 35L;
	EXPECT(ageIndex != GetSoupIndexObject(soup, 0));
	ageDesc = IndexPathToIndexDesc(RefVar(GetFrameSlotRef(soup, RSSYM_proto)), RefVar(SYMBOL("age")), nil);
	ageIndex = GetSoupIndexObject(soup, (PSSId) RINT(GetFrameSlotRef(ageDesc, RSSYMindex)));
	EXPECT(ageIndex->Find(&key, &key, &data, true) == kIndexOK);
	carol = GetEntry(soup, (PSSId) (long) data);
	EXPECT(StringIs(RefVar(GetFrameSlotRef(carol, RSSYMname)), "Carol"));
	soup2 = StoreGetSoup(storeObject, RefVar(MakeString("Others")));
	EXPECT(CountEntries(soup2) == 2);

	// the soups emptied and removed
	SoupRemoveAllEntries(soup2);
	EXPECT(CountEntries(soup2) == 0);
	long total, used;
	SoupRemoveFromStore(soup2);
	EXPECT(GetFrameSlotRef(soup2, RSSYM_proto) == NILREF);
	EXPECT(StoreHasSoup(storeObject, RefVar(MakeString("Others"))) == NILREF);
	EXPECT(Length(RefVar(StoreGetSoupNames(storeObject))) == 1);
	SoupRemoveFromStore(soup);
	EXPECT(Length(RefVar(StoreGetSoupNames(storeObject))) == 0);
	store->GetStoreSizes(&total, &used);
	EXPECT(used < 0x1000);

	// erased: a fresh store frame in the same place
	RefVar erased(StoreErase(storeObject));
	EXPECT(Length(gStores) == 1 && EQRef(GetArraySlotRef(gStores, 0), erased));
	EXPECT(StoreIsValid(storeObject) == NILREF && StoreIsValid(erased) == TRUEREF);
	RemoveTStore(store);
	store->Delete();
}


static long
WalkCursor(RefArg cursor, long* firstAge, long* lastAge)
{
	TCursor* c = CursorObj(cursor);
	long count = 0;
	RefVar entry(c->Reset());
	long prior = -1;
	while ((Ref) entry != NILREF)
	{
		long age = RINT(GetFrameSlotRef(entry, SYMBOL("age")));
		if (count == 0)
		{
			if (firstAge != nil)
				*firstAge = age;
		}
		else
			EXPECT(age >= prior);
		prior = age;
		count++;
		entry = c->Move(1);
	}
	if (lastAge != nil)
		*lastAge = prior;
	return count;
}


static void
TestCursors()
{
	TStore* store = NewStore();
	RefVar storeObject(RegisterTStore(store));
	RefVar specs(AllocateArray(RSSYMarray, 2));
	SetArraySlotRef(specs, 0, IndexSpec("name", "string"));
	SetArraySlotRef(specs, 1, IndexSpec("age", "int"));
	RefVar soup(StoreCreateSoup(storeObject, RefVar(MakeString("People")), specs));
	static const char* names[] = { "Ann", "Bob", "Cid", "Dee", "Eve", "Fay", "Gus", "Hal", "Ivy", "Jon" };
	for (long i = 0; i < 10; i++)
		SoupAdd(soup, RefVar(Person(names[i], 20 + i * 5)));		// ages 20..65
	SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("theSoup")), soup);

	// the whole soup by age
	RefVar spec(AllocateFrame());
	SetFrameSlot(spec, RSSYMindexpath, RefVar(SYMBOL("age")));
	RefVar cursor(SoupQuery(soup, spec));
	EXPECT(IsFrame(cursor));
	TCursor* c = CursorObj(cursor);
	EXPECT(c != nil && EQRef(c->fSoup, soup));
	RefVar entry(c->Entry());
	EXPECT(IsFaultBlock(entry) && RINT(GetFrameSlotRef(entry, SYMBOL("age"))) == 20);
	EXPECT(RINT(c->EntryKey()) == 20);
	EXPECT(c->IsParked() == NILREF);
	long first, last;
	EXPECT(WalkCursor(cursor, &first, &last) == 10 && first == 20 && last == 65);
	EXPECT(c->IsParked() == RSSYMend);
	EXPECT(c->CountEntries() == 10);
	EXPECT(c->IsParked() == RSSYMend);				// the count leaves the position alone
	entry = c->Move(-1);
	EXPECT(RINT(GetFrameSlotRef(entry, SYMBOL("age"))) == 65);
	entry = c->Move(-3);
	EXPECT(RINT(GetFrameSlotRef(entry, SYMBOL("age"))) == 50);
	entry = c->Move(2);
	EXPECT(RINT(GetFrameSlotRef(entry, SYMBOL("age"))) == 60);
	entry = c->ResetToEnd();
	EXPECT(RINT(GetFrameSlotRef(entry, SYMBOL("age"))) == 65);
	EXPECT(c->Move(1) == NILREF && c->IsParked() == RSSYMend);
	EXPECT(c->Move(1) == NILREF);
	entry = c->Move(-1);
	EXPECT(RINT(GetFrameSlotRef(entry, SYMBOL("age"))) == 65);
	entry = c->GotoKey(RefVar(MAKEINT(40)));
	EXPECT(RINT(GetFrameSlotRef(entry, SYMBOL("age"))) == 40);
	entry = c->GotoKey(RefVar(MAKEINT(42)));		// no such key: the one after
	EXPECT(RINT(GetFrameSlotRef(entry, SYMBOL("age"))) == 45);
	EXPECT(c->GotoKey(RefVar(MAKEINT(99))) == NILREF && c->IsParked() == RSSYMend);
	RefVar bob(GetEntry(soup, FaultBlockId(RefVar(c->GotoKey(RefVar(MAKEINT(25)))))));
	EXPECT(c->GotoEntry(bob) == TRUEREF && EQRef(c->Entry(), bob));
	entry = c->Move(1);
	EXPECT(RINT(GetFrameSlotRef(entry, SYMBOL("age"))) == 30);
	// a clone walks on its own
	RefVar cloned(c->Clone());
	TCursor* c2 = CursorObj(cloned);
	EXPECT(c2 != c && RINT(GetFrameSlotRef(RefVar(c2->Entry()), SYMBOL("age"))) == 30);
	c2->Move(2);
	EXPECT(RINT(GetFrameSlotRef(RefVar(c2->Entry()), SYMBOL("age"))) == 40);
	EXPECT(RINT(GetFrameSlotRef(RefVar(c->Entry()), SYMBOL("age"))) == 30);
	EXPECT(Length(RefVar(GetFrameSlotRef(soup, RSSYMcursors))) >= 2);

	// key bounds
	spec = AllocateFrame();
	SetFrameSlot(spec, RSSYMindexpath, RefVar(SYMBOL("age")));
	SetFrameSlot(spec, RSSYMbeginkey, RefVar(MAKEINT(30)));
	SetFrameSlot(spec, RSSYMendkey, RefVar(MAKEINT(50)));
	cursor = SoupQuery(soup, spec);
	c = CursorObj(cursor);
	EXPECT(WalkCursor(cursor, &first, &last) == 5 && first == 30 && last == 50);
	EXPECT(c->CountEntries() == 5);
	c->ResetToEnd();
	EXPECT(RINT(c->EntryKey()) == 50);
	EXPECT(c->Move(-5) == NILREF && c->IsParked() == RSSYMbegin);
	entry = c->GotoKey(RefVar(MAKEINT(10)));			// before the bounds: reset
	EXPECT(RINT(GetFrameSlotRef(entry, SYMBOL("age"))) == 30);
	entry = c->GotoKey(RefVar(MAKEINT(60)));			// past them: to the end
	EXPECT(RINT(GetFrameSlotRef(entry, SYMBOL("age"))) == 50);
	spec = AllocateFrame();
	SetFrameSlot(spec, RSSYMindexpath, RefVar(SYMBOL("age")));
	SetFrameSlot(spec, RSSYMbeginexclkey, RefVar(MAKEINT(30)));
	SetFrameSlot(spec, RSSYMendexclkey, RefVar(MAKEINT(50)));
	cursor = SoupQuery(soup, spec);
	EXPECT(WalkCursor(cursor, &first, &last) == 3 && first == 35 && last == 45);
	// a start key
	SetFrameSlot(spec, RSSYMstartkey, RefVar(MAKEINT(40)));
	cursor = SoupQuery(soup, spec);
	c = CursorObj(cursor);
	EXPECT(RINT(c->EntryKey()) == 40);
	EXPECT(c->CountEntries() == 2);					// counted from the start key, as the ROM does (40, 45)
	EXPECT(RINT(c->EntryKey()) == 40);
	// string keys, collated
	spec = AllocateFrame();
	SetFrameSlot(spec, RSSYMindexpath, RefVar(SYMBOL("name")));
	SetFrameSlot(spec, RSSYMbeginkey, RefVar(MakeString("d")));
	SetFrameSlot(spec, RSSYMendkey, RefVar(MakeString("g")));
	cursor = SoupQuery(soup, spec);
	c = CursorObj(cursor);
	EXPECT(StringIs(RefVar(GetFrameSlotRef(RefVar(c->Entry()), RSSYMname)), "Dee"));
	EXPECT(StringIs(RefVar(c->EntryKey()), "Dee"));
	EXPECT(c->CountEntries() == 3);					// Dee, Eve, Fay
	entry = c->Move(2);
	EXPECT(StringIs(RefVar(GetFrameSlotRef(entry, RSSYMname)), "Fay"));
	EXPECT(c->Move(1) == NILREF);

	// tests through NewtonScript
	RefVar q(Eval("theSoup:Query({indexPath: 'age, validTest: func(e) e.age mod 10 = 0})"));
	EXPECT(WalkCursor(q, &first, &last) == 5 && first == 20 && last == 60);
	q = Eval("theSoup:Query({indexPath: 'age, indexValidTest: func(k) k < 40})");
	EXPECT(WalkCursor(q, &first, &last) == 4 && first == 20 && last == 35);
	q = Eval("theSoup:Query({indexPath: 'age, endTest: func(e) e.age > 35})");
	EXPECT(WalkCursor(q, &first, &last) == 4 && first == 20 && last == 35);
	EXPECT(RINT(Eval("theSoup:Query({indexPath: 'age, beginKey: 45}):CountEntries()")) == 5);
	q = Eval("theSoup:Query({indexPath: 'name})");
	EXPECT(StringIs(RefVar(Eval("call func(c) begin c:Next(); c:Next(); c:Entry().name end with (theSoup:Query({indexPath: 'name}))")), "Cid"));
	SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("q")), q);
	EXPECT(StringIs(RefVar(Eval("q:Entry().name")), "Ann"));
	EXPECT(StringIs(RefVar(Eval("q:Move(3).name")), "Dee"));
	EXPECT(Eval("q:Prev().name") != NILREF);
	EXPECT(RINT(Eval("q:CountEntries()")) == 10);
	EXPECT(Eval("q:WhichEnd()") == NILREF);
	EXPECT(Eval("q:ResetToEnd().name") != NILREF && Eval("q:Next()") == NILREF && EQRef(Eval("q:WhichEnd()"), RSSYMend));
	EXPECT(EQRef(Eval("q:Soup()"), soup) && EQRef(Eval("q:IndexPath()"), SYMBOL("name")));
	EXPECT(EQRef(Eval("q:Status()"), RSSYMvalid));
	EXPECT(StringIs(RefVar(Eval("q:GotoKey(\"e\").name")), "Eve"));
	EXPECT(StringIs(RefVar(Eval("q:EntryKey()")), "Eve"));
	EXPECT(StringIs(RefVar(Eval("q:Clone():Next().name")), "Fay"));
	EXPECT(StringIs(RefVar(Eval("q:Entry().name")), "Eve"));

	// the cursor follows changes: a key change, a removal
	spec = AllocateFrame();
	SetFrameSlot(spec, RSSYMindexpath, RefVar(SYMBOL("age")));
	cursor = SoupQuery(soup, spec);
	c = CursorObj(cursor);
	entry = c->GotoKey(RefVar(MAKEINT(40)));			// Eve
	SetFrameSlot(entry, RefVar(SYMBOL("age")), RefVar(MAKEINT(70)));
	EntryChange(entry);
	EXPECT(EQRef(c->Entry(), entry) && RINT(c->EntryKey()) == 70);
	EXPECT(c->Move(1) == NILREF);						// now the last
	entry = c->Move(-1);
	EXPECT(RINT(c->EntryKey()) == 70);
	c->Reset();
	entry = c->GotoKey(RefVar(MAKEINT(25)));			// Bob
	EntryRemoveFromSoup(entry);
	EXPECT(EQRef(c->Entry(), RSSYMdeleted));			// stands in for the removed one
	entry = c->Move(1);
	EXPECT(RINT(GetFrameSlotRef(entry, SYMBOL("age"))) == 30);
	EXPECT(c->CountEntries() == 9);
	// the last entry removed while the cursor is on it
	c->ResetToEnd();
	entry = c->Entry();
	EXPECT(RINT(GetFrameSlotRef(entry, SYMBOL("age"))) == 70);
	EntryRemoveFromSoup(entry);
	EXPECT(c->Entry() == NILREF && c->IsParked() == RSSYMend);
	EXPECT(c->CountEntries() == 8);
	// an index removed invalidates the cursor
	SoupRemoveIndex(soup, RefVar(SYMBOL("age")));
	EXPECT(c->fSoupInfo == nil && c->Move(1) == NILREF);
	// a collect cursor
	SoupAddIndex(soup, RefVar(IndexSpec("age", "int")));
	spec = AllocateFrame();
	SetFrameSlot(spec, RSSYMindexpath, RefVar(SYMBOL("age")));
	SetFrameSlot(spec, RSSYMbeginkey, RefVar(MAKEINT(35)));
	cursor = SoupCollect(soup, spec);
	c = CursorObj(cursor);
	EXPECT(c->CountEntries() == 6);					// 35, 45, 50, 55, 60, 65
	EXPECT(RINT(GetFrameSlotRef(RefVar(c->Entry()), SYMBOL("age"))) == 35);	// collected, on the first
	entry = c->Move(1);
	EXPECT(RINT(GetFrameSlotRef(entry, SYMBOL("age"))) == 45);
	entry = c->Move(3);
	EXPECT(RINT(GetFrameSlotRef(entry, SYMBOL("age"))) == 60);
	EXPECT(c->GotoEntry(entry) == TRUEREF);
	EntryRemoveFromSoup(entry);
	EXPECT(c->CountEntries() == 5 && RINT(GetFrameSlotRef(RefVar(c->Entry()), SYMBOL("age"))) == 65);
	entry = c->Move(-10);
	EXPECT((Ref) entry == NILREF && c->IsParked() == RSSYMbegin);
	EXPECT(RINT(Eval("theSoup:collect({indexPath: 'age}):CountEntries()")) == 7);
	// the soup removed: the cursors invalidated
	SoupRemoveFromStore(soup);
	EXPECT(c->Move(1) == NILREF && CursorObj(q)->Move(1) == NILREF);
	RemoveTStore(store);
	store->Delete();
}


/*------------------------------------------------------------------------------
	Union soups: the soup of a name across the stores, a cursor over it
	following stores as they come and go, the NewtonScript methods and the
	registry.
------------------------------------------------------------------------------*/

static void
TestUnionSoups()
{
	TStore* store1 = NewStore();
	RefVar storeObject1(RegisterTStore(store1));
	RefVar specs(AllocateArray(RSSYMarray, 2));
	SetArraySlotRef(specs, 0, IndexSpec("name", "string"));
	SetArraySlotRef(specs, 1, IndexSpec("age", "int"));
	RefVar soup1(StoreCreateSoup(storeObject1, RefVar(MakeString("People")), specs));
	SoupAdd(soup1, RefVar(Person("Ann", 20)));
	SoupAdd(soup1, RefVar(Person("Cid", 40)));
	SoupAdd(soup1, RefVar(Person("Eve", 60)));

	// no store has it: nil, or an empty union soup when it must exist
	EXPECT(GetUnionSoup(RefVar(MakeString("Nothing"))) == NILREF);
	RefVar empty(GetUnionSoupAlways(RefVar(MakeString("Nothing"))));
	EXPECT(IsFrame(empty) && Length(RefVar(GetFrameSlotRef(empty, RSSYMsouplist))) == 0);
	EXPECT(EQRef(GetUnionSoup(RefVar(MakeString("Nothing"))), empty));		// cached now
	EXPECT(StringIs(RefVar(SoupGetName(empty)), "Nothing"));

	// the union of one soup; cached, by name case-insensitively
	RefVar unionSoup(GetUnionSoup(RefVar(MakeString("People"))));
	EXPECT(IsFrame(unionSoup) && EQRef(GetFrameSlotRef(unionSoup, RSSYMclass), SYMBOL("UnionSoup")));
	RefVar soupList(GetFrameSlotRef(unionSoup, RSSYMsouplist));
	EXPECT(Length(soupList) == 1 && EQRef(GetArraySlotRef(soupList, 0), soup1));
	EXPECT(EQRef(GetUnionSoup(RefVar(MakeString("people"))), unionSoup));
	EXPECT(StringIs(RefVar(SoupGetName(unionSoup)), "People"));
	EXPECT(GetFrameSlotRef(unionSoup, RSSYMerrorcode) == NILREF);

	// a cursor over the union; a second store's soup joins it
	RefVar spec(AllocateFrame());
	SetFrameSlot(spec, RSSYMindexpath, RefVar(SYMBOL("age")));
	RefVar cursor(SoupQuery(unionSoup, spec));
	long first, last;
	EXPECT(WalkCursor(cursor, &first, &last) == 3 && first == 20 && last == 60);
	TStore* store2 = NewStore();
	RefVar storeObject2(RegisterTStore(store2));
	RefVar soup2(StoreCreateSoup(storeObject2, RefVar(MakeString("People")), specs));
	EXPECT(Length(soupList) == 2 && EQRef(GetArraySlotRef(soupList, 1), soup2));
	SoupAdd(soup2, RefVar(Person("Bob", 30)));
	SoupAdd(soup2, RefVar(Person("Dee", 50)));
	SoupAdd(soup2, RefVar(Person("Fay", 70)));
	TCursor* c = CursorObj(cursor);
	c->Reset();
	EXPECT(WalkCursor(cursor, &first, &last) == 6 && first == 20 && last == 70);	// merged in key order
	EXPECT(c->CountEntries() == 6);
	RefVar entry(c->Move(-2));
	EXPECT(RINT(GetFrameSlotRef(entry, SYMBOL("age"))) == 60);
	entry = c->Move(-1);
	EXPECT(RINT(GetFrameSlotRef(entry, SYMBOL("age"))) == 50 && EQRef(FaultBlockHandler(entry), soup2));
	entry = c->GotoKey(RefVar(MAKEINT(30)));
	EXPECT(RINT(GetFrameSlotRef(entry, SYMBOL("age"))) == 30);
	// the cursor's soup is the union soup; the entry's soup its member
	EXPECT(EQRef(c->fSoup, unionSoup));
	// a fresh cursor sees both soups from the start
	RefVar cursor2(SoupQuery(unionSoup, spec));
	EXPECT(WalkCursor(cursor2, &first, &last) == 6);
	EXPECT(RINT(Eval("GetUnionSoup(\"People\"):Query({indexPath: 'age, beginKey: 45}):CountEntries()")) == 3);

	// the union soup's methods
	RefVar unionSize(UnionSoupGetSize(unionSoup));
	EXPECT(RINT(unionSize) == RINT(PlainSoupGetSize(soup1)) + RINT(PlainSoupGetSize(soup2)));
	UnionSoupAddIndex(unionSoup, RefVar(IndexSpec("shoe", "int")));
	EXPECT(Length(RefVar(SoupGetIndexes(soup1))) == 3 && Length(RefVar(SoupGetIndexes(soup2))) == 3);	// (GetIndexes leaves out _uniqueID)
	UnionSoupRemoveIndex(unionSoup, RefVar(SYMBOL("shoe")));
	EXPECT(Length(RefVar(SoupGetIndexes(soup1))) == 2 && Length(RefVar(SoupGetIndexes(soup2))) == 2);
	EXPECT(UnionSoupFlush(unionSoup) == NILREF);

	// the NewtonScript methods: GetSoupList, GetMember, AddToStore, AddToDefaultStore
	SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("u")), unionSoup);
	SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("store1")), storeObject1);
	SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("store2")), storeObject2);
	SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("userConfiguration")), RefVar(AllocateFrame()));
	RefVar list(Eval("u:GetSoupList()"));
	EXPECT(IsArray(list) && Length(list) == 2 && !EQRef(list, soupList));		// a clone
	EXPECT(EQRef(Eval("u:GetMember(store2)"), soup2));
	EXPECT(EQRef(Eval("u:GetName()"), GetFrameSlotRef(unionSoup, RSSYMthename)));
	entry = Eval("u:AddToStore({name: \"Gus\", age: 80}, store2)");
	EXPECT(IsFaultBlock(entry) && EQRef(FaultBlockHandler(entry), soup2));
	entry = Eval("u:AddToDefaultStore({name: \"Hal\", age: 10})");		// no defaultStoreSig: the first store
	EXPECT(IsFaultBlock(entry) && EQRef(FaultBlockHandler(entry), soup1));
	EXPECT(RINT(Eval("u:Query({indexPath: 'age}):CountEntries()")) == 8);
	EXPECT(RINT(Eval("GetDefaultStore():GetSignature()")) == RINT(StoreGetSignature(storeObject1)));

	// the registry: a soupDef creates the member soup on a store that lacks it
	EXPECT(Eval("GetSoupDef(\"Pets\")") == NILREF);
	RefVar pets(Eval("RegUnionSoup('testApp, {name: \"Pets\", indexes: [{structure: 'slot, path: 'kind, type: 'string}], initHook: func(soup, def) soup:SetInfo('hooked, def.name)})"));
	EXPECT(IsFrame(pets) && Length(RefVar(GetFrameSlotRef(pets, RSSYMsouplist))) == 0);
	EXPECT(Length(RefVar(Eval("UnionSoupRegistry"))) == 1);
	EXPECT(StringIs(RefVar(Eval("GetSoupDef(\"Pets\").name")), "Pets"));
	EXPECT(Length(RefVar(Eval("GetSoupIndexesFromSoupDef(\"Pets\")"))) == 1);
	SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("pets")), pets);
	RefVar petSoup(Eval("pets:GetMember(store1)"));
	EXPECT(IsFrame(petSoup) && StoreHasSoup(storeObject1, RefVar(MakeString("Pets"))) != NILREF);
	EXPECT(Length(RefVar(GetFrameSlotRef(pets, RSSYMsouplist))) == 1);			// joined the union as it was created
	EXPECT(StringIs(RefVar(Eval("pets:GetMember(store1):GetInfo('soupDef).name")), "Pets"));
	EXPECT(StringIs(RefVar(Eval("pets:GetMember(store1):GetInfo('hooked)")), "Pets"));	// the initHook ran
	EXPECT(Length(RefVar(SoupGetIndexes(petSoup))) == 1);
	entry = Eval("pets:AddToStore({kind: \"cat\"}, store2)");
	EXPECT(Length(RefVar(GetFrameSlotRef(pets, RSSYMsouplist))) == 2);
	EXPECT(RINT(Eval("pets:Query({indexPath: 'kind}):CountEntries()")) == 1);
	EXPECT(StringIs(RefVar(Eval("GetSoupDef(\"Pets\").name")), "Pets"));
	Eval("UnRegUnionSoup(\"Pets\", 'testApp)");
	EXPECT(Length(RefVar(Eval("UnionSoupRegistry"))) == 0);
	EXPECT(StringIs(RefVar(Eval("GetSoupDef(\"Pets\").name")), "Pets"));	// still: from the soups' soupDef info
	// no soupDef at all: CreateUSoupMember throws
	Boolean threw = false;
	newton_try
	{
		Eval("GetUnionSoupAlways(\"Ghosts\"):GetMember(store1)");
	}
	newton_catch("evt.ex.nosoupdef")
	{
		threw = true;
	}
	end_try;
	EXPECT(threw);

	// a store removed: its soup leaves the union; the cursor follows
	c->Reset();
	RemoveTStore(store2);
	store2->Delete();
	EXPECT(Length(soupList) == 1 && EQRef(GetArraySlotRef(soupList, 0), soup1));
	EXPECT(WalkCursor(cursor, &first, &last) == 4 && first == 10 && last == 60);
	// the last soup gone too: the union soup stays, empty
	SoupRemoveFromStore(soup1);
	EXPECT(Length(soupList) == 0);
	EXPECT(c->Move(1) == NILREF);
	EXPECT(EQRef(GetUnionSoup(RefVar(MakeString("People"))), unionSoup));
	RemoveTStore(store1);
	store1->Delete();
}


/*------------------------------------------------------------------------------
	Tags: a tags index, entries' tags encoded into it, tagSpec queries,
	the tag methods and a cursor following tag changes.
------------------------------------------------------------------------------*/

static Ref
Note(const char* name, RefArg labels)
{
	RefVar frame(AllocateFrame());
	SetFrameSlot(frame, RSSYMname, RefVar(MakeString(name)));
	if ((Ref) labels != NILREF)
		SetFrameSlot(frame, RefVar(SYMBOL("labels")), labels);
	return frame;
}


static Ref
Symbols(const char* a, const char* b = nil)
{
	RefVar array(AllocateArray(RSSYMarray, 0));
	AddArraySlot(array, RefVar(SYMBOL(a)));
	if (b != nil)
		AddArraySlot(array, RefVar(SYMBOL(b)));
	return array;
}


static long
CountTagged(RefArg soup, const char* mode, RefArg tags)
{
	RefVar tagSpec(AllocateFrame());
	SetFrameSlot(tagSpec, RefVar(SYMBOL(mode)), tags);
	RefVar spec(AllocateFrame());
	SetFrameSlot(spec, RSSYMtagspec, tagSpec);
	return CursorObj(RefVar(SoupQuery(soup, spec)))->CountEntries();
}


static void
TestTags()
{
	TStore* store = NewStore();
	RefVar storeObject(RegisterTStore(store));
	RefVar tagsSpec(IndexSpec("labels", "tags"));
	SetFrameSlot(tagsSpec, RSSYMtags, RefVar(Symbols("home", "work")));
	RefVar specs(AllocateArray(RSSYMarray, 2));
	SetArraySlotRef(specs, 0, IndexSpec("name", "string"));
	SetArraySlotRef(specs, 1, tagsSpec);
	RefVar soup(StoreCreateSoup(storeObject, RefVar(MakeString("Notes")), specs));
	EXPECT(PlainSoupHasTags(soup) == TRUEREF);
	RefVar tags(PlainSoupGetTags(soup));
	EXPECT(Length(tags) == 2 && EQRef(GetArraySlotRef(tags, 0), SYMBOL("home")));

	// entries with tags; an unknown tag joins the soup's
	RefVar home(SoupAdd(soup, RefVar(Note("chores", RefVar(SYMBOL("home"))))));
	RefVar both(SoupAdd(soup, RefVar(Note("taxes", RefVar(Symbols("home", "work"))))));
	RefVar work(SoupAdd(soup, RefVar(Note("report", RefVar(SYMBOL("work"))))));
	RefVar none(SoupAdd(soup, RefVar(Note("idea", RefVar(NILREF)))));
	RefVar fun(SoupAdd(soup, RefVar(Note("party", RefVar(Symbols("fun"))))));
	tags = PlainSoupGetTags(soup);
	EXPECT(Length(tags) == 3 && EQRef(GetArraySlotRef(tags, 2), SYMBOL("fun")));
	// the tags index holds each entry's bits
	TSoupIndex* tagsIndex = GetSoupIndexObject(soup, (PSSId) RINT(GetFrameSlotRef(RefVar(GetTagsIndexDesc(RefVar(GetFrameSlotRef(soup, RSSYM_proto)))), RSSYMindex)));
	SKey key;
	key.Clear();
	key = (long) FaultBlockId(both);
	TagsBits bits;
	bits.Clear();
	EXPECT(tagsIndex->Find(&key, &key, &bits, true) == kIndexOK && bits.Size() == 1 && bits.Data()[0] == 3);
	key = (long) FaultBlockId(fun);
	EXPECT(tagsIndex->Find(&key, &key, &bits, true) == kIndexOK && bits.Data()[0] == 4);
	key = (long) FaultBlockId(none);
	EXPECT(tagsIndex->Find(&key, &key, &bits, true) != kIndexOK);

	// the tagSpec modes
	EXPECT(CountTagged(soup, "any", RefVar(SYMBOL("home"))) == 2);
	EXPECT(CountTagged(soup, "any", RefVar(Symbols("home", "fun"))) == 3);
	EXPECT(CountTagged(soup, "all", RefVar(Symbols("home", "work"))) == 1);
	EXPECT(CountTagged(soup, "all", RefVar(SYMBOL("work"))) == 2);
	EXPECT(CountTagged(soup, "equal", RefVar(Symbols("home", "work"))) == 1);
	EXPECT(CountTagged(soup, "equal", RefVar(SYMBOL("home"))) == 1);
	EXPECT(CountTagged(soup, "none", RefVar(SYMBOL("home"))) == 3);			// work, idea, party
	EXPECT(CountTagged(soup, "none", RefVar(Symbols("home", "work"))) == 2);	// idea, party
	EXPECT(CountTagged(soup, "any", RefVar(SYMBOL("nothing"))) == 0);		// no such tag
	EXPECT(CountTagged(soup, "all", RefVar(Symbols("home", "nothing"))) == 0);
	SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("notes")), soup);
	EXPECT(RINT(Eval("notes:Query({tagSpec: {any: 'work}}):CountEntries()")) == 2);
	EXPECT(RINT(Eval("notes:Query({tagSpec: {any: 'work, none: 'home}}):CountEntries()")) == 1);
	EXPECT(RINT(Eval("notes:Query({indexPath: 'name, tagSpec: {none: ['home, 'work]}}):CountEntries()")) == 2);
	EXPECT(StringIs(RefVar(Eval("notes:Query({indexPath: 'name, tagSpec: {all: ['home, 'work]}}):Entry().name")), "taxes"));
	Boolean threw = false;
	newton_try
	{
		Eval("notes:Query({tagSpec: {}})");			// no mode at all
	}
	newton_catch(exStoreError)
	{
		threw = true;
	}
	end_try;
	EXPECT(threw);

	// a cursor follows an entry's tags changing
	RefVar tagSpec(AllocateFrame());
	SetFrameSlot(tagSpec, RSSYMany, RefVar(SYMBOL("home")));
	RefVar spec(AllocateFrame());
	SetFrameSlot(spec, RSSYMtagspec, tagSpec);
	RefVar cursor(SoupQuery(soup, spec));
	TCursor* c = CursorObj(cursor);
	EXPECT(EQRef(c->Entry(), home));
	SetFrameSlot(work, RefVar(SYMBOL("labels")), RefVar(Symbols("work", "home")));
	EntryChange(work);
	EXPECT(c->CountEntries() == 3);
	SetFrameSlot(home, RefVar(SYMBOL("labels")), RefVar(SYMBOL("fun")));
	EntryChange(home);									// the cursor's entry no longer matches
	EXPECT(c->CountEntries() == 2 && !EQRef(c->Entry(), home));
	EXPECT(CountTagged(soup, "any", RefVar(SYMBOL("fun"))) == 2);

	// the tag methods
	PlainSoupAddTags(soup, RefVar(Symbols("urgent", "fun")));
	tags = PlainSoupGetTags(soup);
	EXPECT(Length(tags) == 4 && EQRef(GetArraySlotRef(tags, 3), SYMBOL("urgent")));
	PlainSoupRemoveTags(soup, RefVar(Symbols("home")));
	tags = PlainSoupGetTags(soup);
	EXPECT(Length(tags) == 3 && EQRef(GetArraySlotRef(tags, 0), SYMBOL("work")));
	RefVar labels(GetFrameSlotRef(work, RefVar(SYMBOL("labels"))));
	EXPECT(IsArray(labels) && Length(labels) == 1 && EQRef(GetArraySlotRef(labels, 0), SYMBOL("work")));
	EXPECT(CountTagged(soup, "any", RefVar(SYMBOL("work"))) == 2);		// taxes, report
	EXPECT(CountTagged(soup, "any", RefVar(SYMBOL("home"))) == 0);
	PlainSoupModifyTag(soup, RefVar(SYMBOL("work")), RefVar(SYMBOL("office")));
	tags = PlainSoupGetTags(soup);
	EXPECT(EQRef(GetArraySlotRef(tags, 0), SYMBOL("office")));
	labels = GetFrameSlotRef(work, RefVar(SYMBOL("labels")));
	EXPECT(EQRef(GetArraySlotRef(labels, 0), SYMBOL("office")));
	EXPECT(CountTagged(soup, "any", RefVar(SYMBOL("office"))) == 2);
	threw = false;
	newton_try
	{
		PlainSoupModifyTag(soup, RefVar(SYMBOL("fun")), RefVar(SYMBOL("office")));	// exists already
	}
	newton_catch(exStoreError)
	{
		threw = true;
	}
	end_try;
	EXPECT(threw);
	EXPECT(RINT(Eval("Length(notes:GetTags())")) == 3);
	Eval("notes:AddTags('later)");
	EXPECT(RINT(Eval("Length(notes:GetTags())")) == 4);

	// a union soup's tags
	RefVar unionSoup(GetUnionSoup(RefVar(MakeString("Notes"))));
	EXPECT(UnionSoupHasTags(unionSoup) == TRUEREF);
	EXPECT(Length(RefVar(UnionSoupGetTags(unionSoup))) == 4);
	UnionSoupAddTags(unionSoup, RefVar(SYMBOL("shared")));
	EXPECT(Length(RefVar(PlainSoupGetTags(soup))) == 5);
	EXPECT(CountTagged(unionSoup, "any", RefVar(SYMBOL("office"))) == 2);

	// the tags index removed: no tags
	SoupRemoveIndex(soup, RefVar(SYMBOL("labels")));
	EXPECT(PlainSoupHasTags(soup) == NILREF && PlainSoupGetTags(soup) == NILREF);
	threw = false;
	newton_try
	{
		CountTagged(soup, "any", RefVar(SYMBOL("office")));
	}
	newton_catch(exStoreError)
	{
		threw = true;
	}
	end_try;
	EXPECT(threw);
	RemoveTStore(store);
	store->Delete();
}


/*------------------------------------------------------------------------------
	CopyEntries: the fast path (an empty target with the same indexes: the
	store objects copied as they lie) and the slow one, with a callback.
------------------------------------------------------------------------------*/

static void
TestCopyEntries()
{
	TStore* store1 = NewStore();
	RefVar storeObject1(RegisterTStore(store1));
	TStore* store2 = NewStore();
	RefVar storeObject2(RegisterTStore(store2));
	RefVar tagsSpec(IndexSpec("labels", "tags"));
	SetFrameSlot(tagsSpec, RSSYMtags, RefVar(Symbols("home", "work")));
	RefVar specs(AllocateArray(RSSYMarray, 3));
	SetArraySlotRef(specs, 0, IndexSpec("name", "string"));
	SetArraySlotRef(specs, 1, IndexSpec("age", "int"));
	SetArraySlotRef(specs, 2, tagsSpec);
	RefVar soup1(StoreCreateSoup(storeObject1, RefVar(MakeString("People")), specs));
	static const char* names[] = { "Ann", "Bob", "Cid", "Dee", "Eve" };
	RefVar entry;
	for (long i = 0; i < 5; i++)
	{
		entry = Person(names[i], 20 + i * 10);
		SetFrameSlot(entry, RefVar(SYMBOL("labels")), RefVar(SYMBOL(i % 2 == 0 ? "home" : "work")));
		SetFrameSlot(entry, RefVar(SYMBOL("notes")), RefVar(MakeString("some text about this person")));
		SoupAdd(soup1, entry);
	}
	EntryRemoveFromSoup(RefVar(GetEntry(soup1, FaultBlockId(RefVar(CursorObj(RefVar(SoupQuery(soup1, RefVar(AllocateFrame()))))->GotoKey(RefVar(MAKEINT(1))))))));	// Bob (uid 1) removed: a gap in the ids

	// the fast path: the same indexes, an empty soup
	RefVar soup2(StoreCreateSoup(storeObject2, RefVar(MakeString("People")), specs));
	EXPECT(PlainSoupCopyEntries(soup1, soup2) == NILREF);
	RefVar spec(AllocateFrame());
	SetFrameSlot(spec, RSSYMindexpath, RefVar(SYMBOL("age")));
	RefVar cursor(SoupQuery(soup2, spec));
	long first, last;
	EXPECT(WalkCursor(cursor, &first, &last) == 4 && first == 20 && last == 60);
	entry = CursorObj(cursor)->GotoKey(RefVar(MAKEINT(40)));
	EXPECT(StringIs(RefVar(GetFrameSlotRef(entry, RSSYMname)), "Cid") && RINT(GetFrameSlotRef(entry, RSSYM_uniqueid)) == 2);
	EXPECT(StringIs(RefVar(GetFrameSlotRef(entry, SYMBOL("notes"))), "some text about this person"));
	EXPECT(EQRef(GetFrameSlotRef(entry, SYMBOL("labels")), SYMBOL("home")));
	EXPECT(RINT(GetFrameSlotRef(soup2, RSSYMindexnextuid)) == 5);
	SetFrameSlot(spec, RSSYMindexpath, RefVar(SYMBOL("name")));
	cursor = SoupQuery(soup2, spec);
	EXPECT(CursorObj(cursor)->CountEntries() == 4 && StringIs(RefVar(CursorObj(cursor)->EntryKey()), "Ann"));
	EXPECT(CountTagged(soup2, "any", RefVar(SYMBOL("home"))) == 3 && CountTagged(soup2, "any", RefVar(SYMBOL("work"))) == 1);
	entry = SoupAdd(soup2, RefVar(Person("Fay", 70)));
	EXPECT(RINT(GetFrameSlotRef(entry, RSSYM_uniqueid)) == 5);		// the ids carry on past the copied ones

	// the slow path: different indexes, a callback
	RefVar specs2(AllocateArray(RSSYMarray, 1));
	SetArraySlotRef(specs2, 0, IndexSpec("age", "int"));
	RefVar soup3(StoreCreateSoup(storeObject2, RefVar(MakeString("Others")), specs2));
	SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("calls")), RefVar(MAKEINT(0)));
	RefVar callback(Eval("func() calls := calls + 1"));
	EXPECT(PlainSoupCopyEntriesWithCallBack(soup1, soup3, callback, RefVar(MAKEINT(1))) == NILREF);	// every millisecond: at most once per entry
	EXPECT(RINT(GetFrameSlotRef(RefVar(gVarFrame), SYMBOL("calls"))) <= 4);
	SetFrameSlot(spec, RSSYMindexpath, RefVar(SYMBOL("age")));
	cursor = SoupQuery(soup3, spec);
	EXPECT(WalkCursor(cursor, &first, &last) == 4 && first == 20 && last == 60);
	entry = CursorObj(cursor)->GotoKey(RefVar(MAKEINT(60)));
	EXPECT(StringIs(RefVar(GetFrameSlotRef(entry, RSSYMname)), "Eve") && RINT(GetFrameSlotRef(entry, RSSYM_uniqueid)) == 4);
	EXPECT(RINT(GetFrameSlotRef(soup3, RSSYMindexnextuid)) == 5);
	entry = SoupAdd(soup3, RefVar(Person("Yan", 98)));
	EXPECT(RINT(GetFrameSlotRef(entry, RSSYM_uniqueid)) == 5);
	// the _uniqueIDs are kept: copying into a soup that has them collides
	Boolean threw = false;
	newton_try
	{
		PlainSoupCopyEntries(soup1, soup3);
	}
	newton_catch(exStoreError)
	{
		threw = true;
	}
	end_try;
	EXPECT(threw);
	// through NewtonScript, and not to a union soup
	SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("soup1")), soup1);
	SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("soup4")), RefVar(StoreCreateSoup(storeObject2, RefVar(MakeString("Fourth")), specs2)));
	Eval("soup1:CopyEntries(soup4)");
	EXPECT(RINT(Eval("soup4:Query({indexPath: 'age}):CountEntries()")) == 4);
	threw = false;
	newton_try
	{
		Eval("soup1:CopyEntries(GetUnionSoup(\"People\"))");
	}
	newton_catch(exStoreError)
	{
		threw = true;
	}
	end_try;
	EXPECT(threw);
	RemoveTStore(store2);
	store2->Delete();
	RemoveTStore(store1);
	store1->Delete();
}


int
main()
{
	InitHostStandaloneHeap();
	gObjectHeapSize = 0x200000;
	InitObjects();
	InitQueries();
	// the globals the REP defines (REPInit) that the NewtonScript built-ins use
	SetFrameSlot(RefVar(gVarFrame), RSSYMvars, RefVar(gVarFrame));
	SetFrameSlot(RefVar(gVarFrame), RSSYMfunctions, RefVar(gFunctionFrame));
	newton_try
	{
		TestStoreFrame();
		TestSoups();
		TestCursors();
		TestUnionSoups();
		TestTags();
		TestCopyEntries();
	}
	newton_catch_all
	{
		failures++;
		fprintf(stderr, "FAIL: unhandled exception %s (%ld)\n", _info.exception.name, (long) (Long) _info.exception.data);
	}
	end_try;
	if (failures == 0)
		printf("test_Soups: all passed\n");
	else
		printf("test_Soups: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
