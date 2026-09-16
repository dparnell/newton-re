// Store wrapper test: TStoreHashTable (entries by hash, references, Get,
// the iterator, TotalSize, Abort re-reading the buckets), TCachedReadStore,
// and TStoreWrapper's symbol and map tables over a THostStore: symbols to
// references and back (the cache), frames to map references (the sorted
// tag order, _proto and the hidden slots left out, functions and argFrames
// unsorted) and back to maps, copying maps and symbols between two
// stores, and the root data.  Runs over a standalone kernel heap and
// object heap (no boot: the stores are made from their class info).

#include "StoreWrapper.h"
#include "host/HostStore.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "memory/host/KernelHeap.h"
#include "NewtonMemory.h"
#include "OSErrors.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static Ref SYMBOL(const char* name) { return Intern((char*) name); }


// a formatted host store of 64 KB
static TStore*
NewStore()
{
	TStore* store = (TStore*) THostStore::ClassInfo()->New();
	EXPECT(store != nil);
	EXPECT(store->Init(nil, 0x10000, 0, 0, kStoreIsInternal, nil) == noErr);
	EXPECT(store->Format() == noErr);
	return store;
}


static void
TestHashTable()
{
	TStore* store = NewStore();
	PSSId id = TStoreHashTable::Create(store);
	EXPECT(id != 0);
	long size = 0;
	EXPECT(store->GetObjectSize(id, &size) == noErr && size == kStoreHashTableSize);
	TStoreHashTable table(store, id);
	EXPECT(table.TotalSize() == kStoreHashTableSize);

	// entries: the same bytes get the same reference, others a new one
	long a = table.Insert(5, (char*) "alpha", 5);
	long b = table.Insert(5, (char*) "beta", 4);				// the same bucket
	long c = table.Insert(6, (char*) "gamma", 5);
	EXPECT(a == (5 << 16) && b == (5 << 16) + 7 && c == (6 << 16));
	EXPECT(table.Insert(5, (char*) "alpha", 5) == a);
	EXPECT(table.Insert(5, (char*) "beta", 4) == b);
	EXPECT(table.Insert(5 + 64, (char*) "delta", 5) == (5 << 16) + 13);		// hash & 63
	char buffer[16];
	long room = sizeof(buffer);
	EXPECT(table.Get(b, buffer, &room) && room == 4 && memcmp(buffer, "beta", 4) == 0);
	room = 3;
	EXPECT(!table.Get(a, buffer, &room) && room == 5);
	EXPECT(table.TotalSize() == kStoreHashTableSize + 7 + 6 + 7 + 7);
	// a long entry (written in two pieces)
	char big[0x500];
	memset(big, 'x', sizeof(big));
	long e = table.Insert(7, big, sizeof(big));
	char* back = new char[sizeof(big)];
	room = sizeof(big);
	EXPECT(table.Get(e, back, &room) && room == (long) sizeof(big) && memcmp(back, big, sizeof(big)) == 0);
	delete[] back;

	// the iterator visits every entry, bucket by bucket
	long count = 0;
	Boolean sawBeta = false;
	for (TStoreHashTableIterator iter(&table); !iter.Done(); iter.Next())
	{
		count++;
		char data[8];
		long got = sizeof(data);
		iter.GetData(data, &got);
		if (iter.Reference() == b)
			sawBeta = got == 4 && memcmp(data, "beta", 4) == 0;
	}
	EXPECT(count == 5 && sawBeta);

	// a second table object over the same store sees the entries
	TStoreHashTable again(store, id);
	room = sizeof(buffer);
	EXPECT(again.Get(c, buffer, &room) && room == 5 && memcmp(buffer, "gamma", 5) == 0);

	// the buckets re-read after an abort
	store->LockStore();
	long f = table.Insert(9, (char*) "zeta", 4);
	EXPECT(table.fBuckets[9] != 0);
	store->Abort();
	table.Abort();
	EXPECT(table.fBuckets[9] == 0);
	(void) f;

	// the cached reader
	{
		TCachedReadStore reader(store, table.fBuckets[5], -1);
		void* p;
		EXPECT(reader.GetDataPtr(2, 5, &p) == noErr && memcmp(p, "alpha", 5) == 0);
		EXPECT(reader.GetDataPtr(9, 4, &p) == noErr && memcmp(p, "beta", 4) == 0);
		EXPECT(reader.fLoaded);
		EXPECT(reader.GetDataPtr(15, 5, &p) == noErr && memcmp(p, "delta", 5) == 0);
		// a range past the cached size is read on its own
		EXPECT(reader.GetDataPtr(18, 10, &p) != noErr);
	}
	store->Delete();
}


static void
TestSymbolsAndMaps()
{
	TStore* store = NewStore();
	TStoreWrapper wrapper(store);
	wrapper.fMapTable = new TStoreHashTable(store, TStoreHashTable::Create(store));
	wrapper.fSymbolTable = new TStoreHashTable(store, TStoreHashTable::Create(store));

	// symbols
	long fooRef = wrapper.SymbolToReference(RefVar(SYMBOL("foo")));
	long barRef = wrapper.SymbolToReference(RefVar(SYMBOL("bar")));
	EXPECT(fooRef != barRef);
	EXPECT(wrapper.SymbolToReference(RefVar(SYMBOL("foo"))) == fooRef);
	EXPECT(wrapper.SymbolToReference(RefVar(SYMBOL("FOO"))) == fooRef);		// symbols are case-insensitive
	EXPECT(wrapper.ReferenceToSymbol(fooRef) == SYMBOL("foo"));
	EXPECT(wrapper.ReferenceToSymbol(barRef) == SYMBOL("bar"));
	EXPECT(wrapper.ReferenceToSymbol(fooRef) == SYMBOL("foo"));			// from the cache
	// more than the cache holds
	long refs[20];
	for (int i = 0; i < 20; i++)
	{
		char name[8];
		sprintf(name, "s%d", i);
		refs[i] = wrapper.SymbolToReference(RefVar(SYMBOL(name)));
	}
	for (int i = 0; i < 20; i++)
	{
		char name[8];
		sprintf(name, "s%d", i);
		EXPECT(wrapper.ReferenceToSymbol(refs[i]) == SYMBOL(name));
	}

	// a frame's map: sorted by symbol, _proto left out
	RefVar frame(AllocateFrame());
	SetFrameSlot(frame, RefVar(SYMBOL("zebra")), RefVar(MAKEINT(1)));
	SetFrameSlot(frame, RSSYM_proto, RefVar(MAKEINT(2)));
	SetFrameSlot(frame, RefVar(SYMBOL("apple")), RefVar(MAKEINT(3)));
	SetFrameSlot(frame, RSSYM_uniqueid, RefVar(MAKEINT(4)));
	SetFrameSlot(frame, RefVar(SYMBOL("mango")), RefVar(MAKEINT(5)));
	long count = 0;
	long* indexes = nil;
	long mapRef = wrapper.FrameToMapReference(frame, false, &count, &indexes);
	EXPECT(count == 4 && indexes != nil);
	RefVar map(wrapper.ReferenceToMap(mapRef));
	EXPECT(IsArray(map) && Length(map) == 5);				// supermap + 4 tags
	// the stored order is the sorted tags'; indexes[i] is the frame slot of stored slot i
	RefVar expected(AllocateArray(RSSYMarray, 0));
	for (long i = 0; i < count; i++)
	{
		Ref tag = GetArraySlot(map, i + 1);
		EXPECT(IsSymbol(tag));
		Ref value = GetArraySlot(frame, indexes[i]);
		EXPECT(GetFrameSlot(frame, RefVar(tag)) == value);
		EXPECT(!EQ(tag, RSSYM_proto));
	}
	Boolean sortedRight = true;
	for (long i = 1; i < count; i++)
		if (SymbolCompare(GetArraySlot(map, i), GetArraySlot(map, i + 1)) >= 0)
			sortedRight = false;
	EXPECT(sortedRight);
	delete[] indexes;
	// the same frame shape gets the same reference; another shape another
	RefVar frame2(AllocateFrame());
	SetFrameSlot(frame2, RefVar(SYMBOL("mango")), RefVar(NILREF));
	SetFrameSlot(frame2, RefVar(SYMBOL("apple")), RefVar(NILREF));
	SetFrameSlot(frame2, RefVar(SYMBOL("zebra")), RefVar(NILREF));
	SetFrameSlot(frame2, RSSYM_uniqueid, RefVar(NILREF));
	EXPECT(wrapper.FrameToMapReference(frame2, false, &count, &indexes) == mapRef);
	delete[] indexes;
	long hiddenRef = wrapper.FrameToMapReference(frame2, true, &count, &indexes);		// _uniqueID dropped
	EXPECT(hiddenRef != mapRef && count == 3);
	delete[] indexes;
	EXPECT(Length(RefVar(wrapper.ReferenceToMap(hiddenRef))) == 4);
	EXPECT(wrapper.ReferenceToMap(mapRef) == map);			// cached
	// a function keeps its slot order
	RefVar fn(AllocateArray(RefVar(kFuncClass), 5));
	RefVar fnFrame(AllocateFrame());
	SetFrameSlot(fnFrame, RSSYMclass, RefVar(kFuncClass));
	SetFrameSlot(fnFrame, RSSYMinstructions, RefVar(NILREF));
	SetFrameSlot(fnFrame, RSSYMliterals, RefVar(NILREF));
	SetFrameSlot(fnFrame, RefVar(SYMBOL("argFrame")), RefVar(NILREF));
	SetFrameSlot(fnFrame, RSSYMnumargs, RefVar(MAKEINT(0)));
	EXPECT(IsFunction(fnFrame));
	long fnRef = wrapper.FrameToMapReference(fnFrame, false, &count, &indexes);
	EXPECT(count == 5);
	for (long i = 0; i < count; i++)
		EXPECT(indexes[i] == i);
	delete[] indexes;
	RefVar fnMap(wrapper.ReferenceToMap(fnRef));
	EXPECT(EQ(GetArraySlot(fnMap, 1), RSSYMclass) && EQ(GetArraySlot(fnMap, 5), RSSYMnumargs));
	// a large frame (more tags than the stack buffer)
	RefVar wide(AllocateFrame());
	for (int i = 0; i < 40; i++)
	{
		char name[8];
		sprintf(name, "w%02d", i);
		SetFrameSlot(wide, RefVar(SYMBOL(name)), RefVar(MAKEINT(i)));
	}
	long wideRef = wrapper.FrameToMapReference(wide, false, &count, &indexes);
	EXPECT(count == 40);
	delete[] indexes;
	EXPECT(Length(RefVar(wrapper.ReferenceToMap(wideRef))) == 41);

	// copying to another store
	TStore* store2 = NewStore();
	TStoreWrapper wrapper2(store2);
	wrapper2.fMapTable = new TStoreHashTable(store2, TStoreHashTable::Create(store2));
	wrapper2.fSymbolTable = new TStoreHashTable(store2, TStoreHashTable::Create(store2));
	wrapper2.StartCopyMaps_Symbols();
	long copiedFoo = wrapper2.CopySymbol(fooRef, &wrapper);
	EXPECT(wrapper2.ReferenceToSymbol(copiedFoo) == SYMBOL("foo"));
	EXPECT(wrapper2.CopySymbol(fooRef, &wrapper) == copiedFoo);
	long copiedCount = 0;
	long copiedMap = wrapper2.CopyMap(mapRef, &wrapper, &copiedCount);
	EXPECT(copiedCount == 4);
	EXPECT(wrapper2.CopyMap(mapRef, &wrapper, &copiedCount) == copiedMap && copiedCount == 4);
	RefVar map2(wrapper2.ReferenceToMap(copiedMap));
	EXPECT(Length(map2) == 5);
	for (long i = 1; i <= 4; i++)
		EXPECT(EQ(GetArraySlot(map2, i), GetArraySlot(map, i)));
	// the same names hash to the same bucket in both stores
	EXPECT((copiedMap >> 16) == (mapRef >> 16));
	wrapper2.EndCopyMaps_Symbols();

	// the wrapper's transaction: locks nest, abort drops the tables' new buckets
	wrapper.LockStore();
	wrapper.Dirty();
	long tempRef = wrapper.SymbolToReference(RefVar(SYMBOL("temporary")));
	EXPECT(store->IsLocked() && wrapper.fIsDirty);
	wrapper.SparklingClean();
	EXPECT(store->IsLocked() && !wrapper.fIsDirty);
	wrapper.Abort();
	EXPECT(!store->IsLocked());
	// the symbol went with the abort; its bucket may have been new
	EXPECT(wrapper.SymbolToReference(RefVar(SYMBOL("temporary"))) == tempRef);	// re-added at the same place
	long total, used;
	wrapper.GetStoreSizes(&total, &used);
	EXPECT(total == 0x10000 && used > 0);

	// the root data
	StoreRootData root;
	long size = 0;
	ReadStoreRootData(store, 0, &root, &size);
	EXPECT(size == 0);					// the host store's root is empty until MakeStoreObject writes it
	root.fSignature = kStoreRootSignature;
	root.fVersion = kStoreRootVersion;
	root.fMapTableId = wrapper.fMapTable->fId;
	root.fSymbolTableId = wrapper.fSymbolTable->fId;
	root.fRootFrameId = 0;
	EXPECT(store->ReplaceObject(kHostStoreRootId, (char*) &root, kStoreRootDataSize) == noErr);
	StoreRootData back;
	back.fExtra = 99;
	ReadStoreRootData(store, 0, &back, &size);
	EXPECT(size == kStoreRootDataSize);
	EXPECT(back.fSignature == kStoreRootSignature);
	EXPECT(back.fVersion == 4);
	EXPECT(back.fMapTableId == wrapper.fMapTable->fId);
	EXPECT(back.fExtra == 0);
	ReadStoreRootData(store, kHostStoreRootId, &back, nil);
	EXPECT(back.fSymbolTableId == wrapper.fSymbolTable->fId);

	store2->Delete();
	store->Delete();
}


// the node cache without an index: entries remembered, found, forgotten
static void
TestNodeCache()
{
	TNodeCache cache;
	EXPECT(cache.fNumEntries == kNodeCacheInitialEntries);
	NodeHeader* a = cache.RememberNode(nil, 100, kNodeCacheNodeSize, 0, 0);
	NodeHeader* b = cache.RememberNode(nil, 200, kNodeCacheNodeSize, 1, 1);
	EXPECT(a != nil && b != nil && a != b);
	EXPECT(cache.FindNode(nil, 100) == a && cache.FindNode(nil, 200) == b && cache.FindNode(nil, 300) == nil);
	EXPECT(cache.fNumEntries == 3);
	// every entry in use: the cache grows
	NodeHeader* c = cache.RememberNode(nil, 300, kNodeCacheNodeSize, 0, 0);
	NodeHeader* d = cache.RememberNode(nil, 400, kNodeCacheNodeSize, 0, 0);
	EXPECT(c != nil && d != nil && cache.fNumEntries == 4);
	// not in use: the least recently used is reused
	cache.Abort(nil);					// all of (index nil) dropped
	EXPECT(cache.FindNode(nil, 100) == nil);
	NodeHeader* e = cache.RememberNode(nil, 500, kNodeCacheNodeSize, 0, 0);
	EXPECT(e != nil && cache.fNumEntries == 4);
	long mods = cache.fModCount;
	cache.DirtyNode(e);
	EXPECT(cache.fModCount == mods + 1);
	cache.ForgetNode(500);
	EXPECT(cache.FindNode(nil, 500) == nil);
	cache.Clear();
	EXPECT(cache.fModCount > mods);
}


int
main()
{
	InitHostStandaloneHeap();
	gObjectHeapSize = 0x100000;
	InitObjects();
	newton_try
	{
		TestHashTable();
		TestSymbolsAndMaps();
		TestNodeCache();
	}
	newton_catch_all
	{
		failures++;
		fprintf(stderr, "FAIL: unhandled exception %s (%ld)\n", _info.exception.name, (long) (Long) _info.exception.data);
	}
	end_try;
	if (failures == 0)
		printf("test_StoreWrapper: all passed\n");
	else
		printf("test_StoreWrapper: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
