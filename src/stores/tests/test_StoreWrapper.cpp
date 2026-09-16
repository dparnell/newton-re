// Store wrapper test: TStoreHashTable (entries by hash, references, Get,
// the iterator, TotalSize, Abort re-reading the buckets), TCachedReadStore,
// and TStoreWrapper's symbol and map tables over a THostStore: symbols to
// references and back (the cache), frames to map references (the sorted
// tag order, _proto and the hidden slots left out, functions and argFrames
// unsorted) and back to maps, copying maps and symbols between two
// stores, and the root data.  Runs over a standalone kernel heap and
// object heap (no boot: the stores are made from their class info).

#include "StoreWrapper.h"
#include "StoreObject.h"
#include "Compiler.h"
#include "ByteOrder.h"
#include "REPTranslators.h"
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
	WriteStoreRootData(store, kHostStoreRootId, &root);
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


// a wrapper with fresh tables over a new store (the caller deletes both)
static TStoreWrapper*
NewWrapper(TStore** storeOut)
{
	TStore* store = NewStore();
	TStoreWrapper* wrapper = new TStoreWrapper(store);
	wrapper->fMapTable = new TStoreHashTable(store, TStoreHashTable::Create(store));
	wrapper->fSymbolTable = new TStoreHashTable(store, TStoreHashTable::Create(store));
	*storeOut = store;
	return wrapper;
}


// the same object, structurally: immediates and symbols EQ, binaries the
// same class and bytes, arrays and frames the same class, length and parts
static Boolean
DeepEqual(RefArg a, RefArg b)
{
	Ref ra = a, rb = b;
	if (ra == rb)
		return true;
	if (!ISPTR(ra) || !ISPTR(rb))
		return false;
	if (IsSymbol(ra) || IsSymbol(rb))
		return false;
	if (!EQ(ClassOf(a), ClassOf(b)) || Length(a) != Length(b))
		return false;
	ULong flags = ObjectFlags(ra);
	if ((ObjectFlags(rb) & 3) != (flags & 3))
		return false;
	if ((flags & kObjSlotted) == 0)
		return memcmp(BinaryData(ra), BinaryData(rb), Length(a)) == 0;
	if ((flags & kObjFrame) == 0)
	{
		for (long i = 0; i < Length(a); i++)
			if (!DeepEqual(RefVar(GetArraySlot(a, i)), RefVar(GetArraySlot(b, i))))
				return false;
		return true;
	}
	TObjectIterator iter(a);
	for (; !iter.Done(); iter.Next())
	{
		if (!FrameHasSlot(b, iter.fTag))
			return false;
		if (!DeepEqual(iter.fValue, RefVar(GetFrameSlot(b, iter.fTag))))
			return false;
	}
	return true;
}


static Ref
Eval(const char* source)
{
	RefVar fn(ParseString(RefVar(MakeString(source))));
	return InterpretBlock(fn, RefVar(gVarFrame));
}


static void
TestStoreObjects()
{
	TStore* store;
	TStoreWrapper* wrapper = NewWrapper(&store);

	// a frame with every kind of object goes to the store and comes back the same
	RefVar obj(Eval("local b := MakeBinary(5, 'bytes); StuffByte(b, 2, 0x55); {name: \"Hello, world\", count: 42, neg: -7, big: 100000, flag: true, none: nil, "
		"ch: $a, uch: $\\u263A, sym: 'foo, real: 1.5, arr: [1, \"two\", 'three], "
		"plain: [4, 5], typed: [foo: 1], inner: {x: 1, y: \"why\"}, "
		"rect: {top: 1, left: 2, bottom: 3, right: 4}, bigrect: {top: 1, left: 2, bottom: 300, right: 4}, "
		"empty: \"\", data: b}"));
	PSSId id = (PSSId) -1;
	StorePermObject(obj, wrapper, id, nil, nil);
	EXPECT(id != 0 && id != (PSSId) -1);
	StoreObjectHeader header;
	char headerBytes[kStoreObjectHeaderSize];
	EXPECT(store->Read(id, 0, headerBytes, kStoreObjectHeaderSize) == noErr);
	header.ReadFrom(headerBytes);
	EXPECT(header.fUniqueId == -1 && header.fModTime == (ULong32) -1 && header.fNumHints == 0);
	EXPECT(header.fTextBlockId != 0 && header.TextSize() == (long) (13 + 4 + 4 + 1) * 2);		// the strings' bytes
	RefVar back(LoadPermObject(wrapper, id, nil));
	EXPECT(IsFrame(back));
	EXPECT(DeepEqual(obj, back));
	EXPECT(!FrameHasSlot(back, RSSYM_uniqueid) && !FrameHasSlot(back, RSSYM_modtime));
	// the shape survives: strings are strings, the symbol is EQ, the real is a real
	EXPECT(IsString(RefVar(GetFrameSlot(back, SYMBOL("name")))));
	EXPECT(EQ(GetFrameSlot(back, SYMBOL("sym")), SYMBOL("foo")));
	EXPECT(ISREAL(GetFrameSlot(back, SYMBOL("real"))) && CDouble(RefVar(GetFrameSlot(back, SYMBOL("real")))) == 1.5);
	EXPECT(EQ(ClassOf(RefVar(GetFrameSlot(back, SYMBOL("typed")))), SYMBOL("foo")));
	EXPECT(GetFrameSlot(back, SYMBOL("uch")) == MAKECHAR(0x263a));
	EXPECT(GetFrameSlot(back, SYMBOL("big")) == MAKEINT(100000));
	EXPECT(GetFrameSlot(back, SYMBOL("neg")) == MAKEINT(-7));
	EXPECT(Length(RefVar(GetFrameSlot(back, SYMBOL("rect")))) == 4 && RINT(GetFrameSlot(RefVar(GetFrameSlot(back, SYMBOL("rect"))), RSSYMbottom)) == 3);
	EXPECT(RINT(GetFrameSlot(RefVar(GetFrameSlot(back, SYMBOL("bigrect"))), RSSYMbottom)) == 300);

	// shared references come back shared (precedents)
	RefVar shared(Eval("local s := \"same\"; local f := {a: 1}; {p: s, q: s, r: f, t: f, u: [s, f]}"));
	id = (PSSId) -1;
	StorePermObject(shared, wrapper, id, nil, nil);
	back = LoadPermObject(wrapper, id, nil);
	EXPECT(DeepEqual(shared, back));
	EXPECT(GetFrameSlot(back, SYMBOL("p")) == GetFrameSlot(back, SYMBOL("q")));
	EXPECT(GetFrameSlot(back, SYMBOL("r")) == GetFrameSlot(back, SYMBOL("t")));
	EXPECT(GetArraySlot(RefVar(GetFrameSlot(back, SYMBOL("u"))), 1) == GetFrameSlot(back, SYMBOL("r")));

	// an entry: _uniqueID and _modTime travel in the header, not the map
	RefVar entry(Eval("{_uniqueID: 17, _modTime: 12345, title: \"note\", _proto: {fromProto: true}}"));
	id = (PSSId) -1;
	StorePermObject(entry, wrapper, id, nil, nil);
	EXPECT(store->Read(id, 0, headerBytes, kStoreObjectHeaderSize) == noErr);
	header.ReadFrom(headerBytes);
	EXPECT(header.fUniqueId == 17 && header.fModTime == 12345);
	back = LoadPermObject(wrapper, id, nil);
	EXPECT(RINT(GetFrameSlot(back, RSSYM_uniqueid)) == 17 && RINT(GetFrameSlot(back, RSSYM_modtime)) == 12345);
	EXPECT(Length(back) == 3);						// title, _uniqueID, _modTime: no _proto
	EXPECT(!FrameHasSlot(back, RSSYM_proto));

	// rewriting in place keeps the id; the text object is reused
	long before, after;
	store->GetStoreSizes(&before, &after);
	SetFrameSlot(entry, RefVar(SYMBOL("title")), RefVar(MakeString("a longer note than before")));
	PSSId same = id;
	StorePermObject(entry, wrapper, same, nil, nil);
	EXPECT(same == id);
	back = LoadPermObject(wrapper, id, nil);
	EXPECT(DeepEqual(RefVar(GetFrameSlot(entry, SYMBOL("title"))), RefVar(GetFrameSlot(back, SYMBOL("title")))));
	StoreObjectHeader header2;
	EXPECT(store->Read(id, 0, headerBytes, kStoreObjectHeaderSize) == noErr);
	header2.ReadFrom(headerBytes);
	EXPECT(header2.fTextBlockId != 0);
	// ... and to one without text
	RefVar plain(Eval("{n: 5}"));
	StorePermObject(plain, wrapper, same, nil, nil);
	EXPECT(store->Read(id, 0, headerBytes, kStoreObjectHeaderSize) == noErr);
	header2.ReadFrom(headerBytes);
	EXPECT(header2.fTextBlockId == 0);
	EXPECT(!store->OwnsObject(header.fTextBlockId));		// the old text object is gone
	EXPECT(DeepEqual(plain, RefVar(LoadPermObject(wrapper, id, nil))));

	// a large object (more than the pipes' buffers)
	RefVar wide(Eval("local a := []; for i := 0 to 300 do AddArraySlot(a, {index: i, text: \"item \" & NumberStr(i), data: MakeBinary(i, 'raw)}); a"));
	id = (PSSId) -1;
	StorePermObject(wide, wrapper, id, nil, nil);
	back = LoadPermObject(wrapper, id, nil);
	EXPECT(Length(back) == 301);
	EXPECT(DeepEqual(RefVar(GetArraySlot(wide, 300)), RefVar(GetArraySlot(back, 300))));
	EXPECT(DeepEqual(RefVar(GetArraySlot(wide, 7)), RefVar(GetArraySlot(back, 7))));
	EXPECT(store->Read(id, 0, headerBytes, kStoreObjectHeaderSize) == noErr);
	header.ReadFrom(headerBytes);
	EXPECT(header.TextSize() > 0x200 && header.fTextBlockId != 0);
	long textSize;
	EXPECT(store->GetObjectSize(header.fTextBlockId, &textSize) == noErr && textSize < header.TextSize() * 3 / 4);		// compressed
	// a string wider than a byte-length long
	RefVar longString(Eval("local s := \"\"; for i := 1 to 60 do s := s & \"abcde\"; {s: s}"));
	id = (PSSId) -1;
	StorePermObject(longString, wrapper, id, nil, nil);
	EXPECT(DeepEqual(longString, RefVar(LoadPermObject(wrapper, id, nil))));

	// deletion takes the text object too
	EXPECT(store->Read(id, 0, headerBytes, kStoreObjectHeaderSize) == noErr);
	header.ReadFrom(headerBytes);
	DeletePermObject(wrapper, id);
	EXPECT(!store->OwnsObject(id) && !store->OwnsObject(header.fTextBlockId));

	// a store that has objects written and read back after an abort of its transaction
	store->LockStore();
	id = (PSSId) -1;
	StorePermObject(plain, wrapper, id, nil, nil);
	wrapper->Abort();
	EXPECT(!store->OwnsObject(id));

	// errors
	{
		Boolean threw = false;
		PSSId none = (PSSId) -1;
		newton_try { StorePermObject(plain, nil, none, nil, nil); } newton_catch(exStoreError) { threw = (long) (Long) _info.exception.data == kNSErrEntryStoreGone; } end_try;
		EXPECT(threw);
		threw = false;
		store->LockReadOnly();
		newton_try { StorePermObject(plain, wrapper, none, nil, nil); } newton_catch(exStoreError) { threw = (long) (Long) _info.exception.data == kSError_WriteProtected; } end_try;
		EXPECT(threw);
		store->UnlockReadOnly(true);
		// a corrupt stream
		threw = false;
		char bad[kStoreObjectHeaderSize + 1];
		memset(bad, 0, sizeof(bad));
		bad[kStoreObjectHeaderSize] = 0x40;
		PSSId badId;
		EXPECT(store->NewObject(bad, sizeof(bad), &badId) == noErr);
		newton_try { LoadPermObject(wrapper, badId, nil); } newton_catch(exStoreError) { threw = (long) (Long) _info.exception.data == kNSErrBadStoreObject; } end_try;
		EXPECT(threw);
	}

	// the pipes and small rects on their own
	{
		long packed = 0;
		EXPECT(PackSmallRect(GetFrameSlot(obj, SYMBOL("rect")), &packed) && packed == 0x01020304);
		EXPECT(!PackSmallRect(GetFrameSlot(obj, SYMBOL("bigrect")), &packed));
		EXPECT(!PackSmallRect(GetFrameSlot(obj, SYMBOL("inner")), &packed));
		RefVar rect(UnpackSmallRect(0x0a0b0c0d));
		EXPECT(RINT(GetFrameSlot(rect, RSSYMtop)) == 10 && RINT(GetFrameSlot(rect, RSSYMright)) == 13);
		char bytes[16] = { 0, 5, (char) 0xff, 0, 1, 0, 0, 7, 'x' };
		TStoreReadPipe pipe(bytes, 9);
		UByte b; long l;
		pipe >> b >> l;
		EXPECT(b == 0 && l == 5);
		pipe >> l;
		EXPECT(l == 0x10000);
		pipe >> l >> b;
		EXPECT(l == 7 && b == 'x');
	}

	delete wrapper;
	store->Delete();
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
		TestStoreObjects();
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
