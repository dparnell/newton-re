// Stores and soups test: a THostStore registered as a store frame (its
// root object formatted, its persistent frame, name and signature), a soup
// created with string and int indexes, entries added (fault blocks in the
// soup's cache, _uniqueIDs, mod times, the indexes), read back through
// their fault blocks, changed (the indexes updated), removed, flushed;
// indexes added and removed; the store re-registered over the same bytes
// finding the soup and its entries again; soup renaming, info, removal
// and the store's soup names; the same through NewtonScript sends to the
// prototype frames' methods.  Runs over a standalone kernel heap and
// object heap.

#include "Soups.h"
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
	EXPECT(ISINT(StoreGetSignature(storeObject)) && RINT(StoreGetSignature(storeObject)) != 0);
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
	StoreWriteWholeObject(storeObject, id, data);
	RefVar back(StoreReadObject(storeObject, id));
	EXPECT(Length(back) == 6 && memcmp(BinaryData(back), "abcdef", 6) == 0);
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


int
main()
{
	InitHostStandaloneHeap();
	gObjectHeapSize = 0x200000;
	InitObjects();
	InitQueries();
	newton_try
	{
		TestStoreFrame();
		TestSoups();
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
