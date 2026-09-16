// Soup index test: TSoupIndex over a THostStore - long keys added in a
// scrambled order (the tree splits into several levels), found, walked
// forwards and backwards, deleted (the nodes merge and the root shrinks
// away); duplicate keys with many data (into the field, then into dup
// nodes), walked and deleted datum by datum; unique-key indexes refusing
// duplicates; string, character and multi-keys ordered by their compare
// functions; Search with a stop function; the nodes surviving a store
// round trip (compact on the store, expanded in the cache); Destroy
// freeing the nodes.  Runs over a standalone kernel heap and object heap.

#include "SoupIndex.h"
#include "ByteOrder.h"
#include "host/HostStore.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "memory/host/KernelHeap.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

extern const ExceptionName exStoreError;


// a formatted host store
static TStore*
NewStore(ULong size = 0x40000)
{
	TStore* store = (TStore*) THostStore::ClassInfo()->New();
	EXPECT(store != nil);
	EXPECT(store->Init(nil, size, 0, 0, kStoreIsInternal, nil) == noErr);
	EXPECT(store->Format() == noErr);
	return store;
}


static void
MakeIndex(TStoreWrapper* wrapper, TSoupIndex* index, long keyType, long dataType, long dups, PSSId* infoId = nil)
{
	IndexInfo info;
	memset(&info, 0, sizeof(info));
	info.fKeyType = keyType;
	info.fDataType = dataType;
	info.fDuplicates = dups;
	info.fMultiTypes = 0xffffffff;
	info.fMultiAscending = 0xff;
	PSSId id = TSoupIndex::Create(wrapper, &info);
	EXPECT(id != 0);
	index->Init(wrapper, id, nil);
	EXPECT(index->fInfo.fNodeSize == kSoupIndexNodeSize && index->fInfo.fRootNodeId == 0);
	EXPECT(index->fFixedKeySize == (keyType == kKeyTypeLong ? 4 : keyType == kKeyTypeChar ? 2 : keyType == kKeyTypeDouble ? 8 : 0));
	if (infoId != nil)
		*infoId = id;
}


static long
Scramble(long i, long n)
{
	return (i * 7919 + 13) % n;			// 7919 is prime to the sizes used
}


// every entry in order, forwards: ==> the count; keys strictly ascending
static long
WalkForward(TSoupIndex* index, long* firstKey = nil, long* lastKey = nil, Boolean allowDups = false)
{
	SKey key, data;
	long count = 0;
	int result = index->First(&key, &data);
	long prior = 0;
	while (result == kIndexOK)
	{
		long k = (long) key;
		if (count == 0)
		{
			if (firstKey != nil)
				*firstKey = k;
		}
		else
			EXPECT(allowDups ? k >= prior : k > prior);
		prior = k;
		count++;
		result = index->Next(&key, &data, kIndexNextDupOrKey, &key, &data);
	}
	EXPECT(result == kIndexEnd || count == 0);
	if (lastKey != nil)
		*lastKey = prior;
	return count;
}


static long
WalkBackward(TSoupIndex* index)
{
	SKey key, data;
	long count = 0;
	int result = index->Last(&key, &data);
	long prior = 0;
	while (result == kIndexOK)
	{
		long k = (long) key;
		if (count != 0)
			EXPECT(k < prior);
		prior = k;
		count++;
		result = index->Prior(&key, &data, false, &key, &data);
	}
	EXPECT(result == kIndexEnd || count == 0);
	return count;
}


static void
TestLongKeys()
{
	TStore* store = NewStore();
	TStoreWrapper wrapper(store);
	TSoupIndex index;
	MakeIndex(&wrapper, &index, kKeyTypeLong, kKeyTypeLong, kIndexDuplicateKeys);

	// empty
	SKey key, data;
	EXPECT(index.First(&key, &data) == kIndexNotFound);
	EXPECT(index.Last(&key, &data) == kIndexNotFound);
	key = 5L;
	EXPECT(index.Find(&key, &key, &data, true) == kIndexEnd);
	EXPECT(WalkForward(&index) == 0);

	// 2000 keys in a scrambled order: several levels of nodes
	const long n = 2000;
	for (long i = 0; i < n; i++)
	{
		long k = Scramble(i, n);
		key = k;
		data = k * 10 + 1;
		EXPECT(index.Add(&key, &data) == kIndexOK);
	}
	EXPECT(index.fInfo.fRootNodeId != 0);
	long first, last;
	EXPECT(WalkForward(&index, &first, &last) == n);
	EXPECT(first == 0 && last == n - 1);
	EXPECT(WalkBackward(&index) == n);
	long size = index.TotalSize();
	EXPECT(size > n * 10 && size < n * 40);

	// every key found with its datum
	for (long k = 0; k < n; k++)
	{
		key = k;
		SKey outKey, outData;
		EXPECT(index.Find(&key, &outKey, &outData, true) == kIndexOK);
		EXPECT((long) outKey == k && (long) outData == k * 10 + 1);
	}
	// a key that is not there: the one after it
	key = -1L;
	EXPECT(index.Find(&key, &key, &data, true) == kIndexNotFound && (long) key == 0);
	key = n + 5;
	EXPECT(index.Find(&key, &key, &data, true) == kIndexEnd);
	// FindPrior
	key = -1L;
	EXPECT(index.FindPrior(&key, &key, &data, true, true) == kIndexNotFound && (long) key == 0);	// nothing before key 0: it stays
	key = n + 5;
	EXPECT(index.FindPrior(&key, &key, &data, true, true) == kIndexEnd && (long) key == n - 1);
	key = 100L;
	EXPECT(index.FindPrior(&key, &key, &data, true, true) == kIndexOK && (long) key == 99);
	key = 100L;
	EXPECT(index.FindPrior(&key, &key, &data, true, false) == kIndexOK && (long) key == 101);

	// Next/Prior from a key that is not there
	key = 1000L; data = 0L;
	EXPECT(index.Next(&key, &data, kIndexNextKey, &key, &data) == kIndexOK && (long) key == 1001);
	key = 1000L; data = 0L;
	EXPECT(index.Prior(&key, &data, true, &key, &data) == kIndexOK && (long) key == 999);
	key = n - 1; data = (n - 1) * 10 + 1;
	EXPECT(index.Next(&key, &data, kIndexNextKey, &key, &data) == kIndexEnd);
	key = 0L; data = 1L;
	EXPECT(index.Prior(&key, &data, true, &key, &data) == kIndexEnd);

	// deleting a datum that is not the key's
	key = 7L; data = 999L;
	Boolean threw = false;
	newton_try
	{
		index.Delete(&key, &data);
	}
	newton_catch(exStoreError)
	{
		threw = (long) (Long) _info.exception.data == kIndexErrNotFound;
	}
	end_try;
	EXPECT(threw);
	// a key that is not there
	key = -7L; data = 1L;
	EXPECT(index.Delete(&key, &data) == kIndexNotFound);

	// every other key deleted, scrambled
	for (long i = 0; i < n; i++)
	{
		long k = Scramble(i, n);
		if ((k & 1) == 0)
		{
			key = k;
			data = k * 10 + 1;
			EXPECT(index.Delete(&key, &data) == kIndexOK);
		}
	}
	EXPECT(WalkForward(&index, &first, &last) == n / 2);
	EXPECT(first == 1 && last == n - 1);
	EXPECT(WalkBackward(&index) == n / 2);
	for (long k = 0; k < n; k++)
	{
		key = k;
		SKey outKey;
		int result = index.Find(&key, &outKey, &data, true);
		if (k & 1)
			EXPECT(result == kIndexOK && (long) outKey == k);
		else
			EXPECT(result == kIndexNotFound && (long) outKey == k + 1);
	}
	// the rest: the tree shrinks to nothing
	for (long i = 0; i < n; i++)
	{
		long k = Scramble(i, n);
		if (k & 1)
		{
			key = k;
			data = k * 10 + 1;
			EXPECT(index.Delete(&key, &data) == kIndexOK);
		}
	}
	EXPECT(WalkForward(&index) == 0);
	EXPECT(index.fInfo.fRootNodeId == 0);
	long total, used;
	store->GetStoreSizes(&total, &used);
	EXPECT(used < 0x1000);

	// added again in order, then Destroy
	for (long k = 0; k < 500; k++)
	{
		key = k;
		data = k;
		EXPECT(index.Add(&key, &data) == kIndexOK);
	}
	EXPECT(WalkForward(&index) == 500);
	index.Destroy();
	EXPECT(index.fInfo.fRootNodeId == 0);
	store->GetStoreSizes(&total, &used);
	EXPECT(used < 0x1000);
	EXPECT(WalkForward(&index) == 0);
	store->Delete();
}


static void
TestDuplicates()
{
	TStore* store = NewStore();
	TStoreWrapper wrapper(store);
	TSoupIndex index;
	MakeIndex(&wrapper, &index, kKeyTypeLong, kKeyTypeLong, kIndexDuplicateKeys);

	SKey key, data;
	// a few keys around the one with duplicates
	for (long k = 0; k < 20; k++)
	{
		key = k;
		data = k;
		EXPECT(index.Add(&key, &data) == kIndexOK);
	}
	// 300 data for key 10: the field fills, then dup nodes
	const long dups = 300;
	for (long i = 1; i <= dups; i++)
	{
		key = 10L;
		data = 1000 + i;
		EXPECT(index.Add(&key, &data) == kIndexOK);
	}
	// the same datum again: an error
	key = 10L; data = 1005L;
	Boolean threw = false;
	newton_try
	{
		index.Add(&key, &data);
	}
	newton_catch(exStoreError)
	{
		threw = (long) (Long) _info.exception.data == kIndexErrDuplicateKey;
	}
	end_try;
	EXPECT(threw);

	// Find gives the first datum
	key = 10L;
	EXPECT(index.Find(&key, &key, &data, true) == kIndexOK && (long) data == 10);
	// all the data, in the order added
	key = 10L; data = 10L;
	long count = 1;
	int result;
	while ((result = index.Next(&key, &data, kIndexNextDup, &key, &data)) == kIndexOK)
	{
		EXPECT((long) key == 10 && (long) data == 1000 + count);
		count++;
	}
	EXPECT(result == kIndexEnd && count == dups + 1);
	// and back
	count = 0;
	key = 10L; data = 1000 + dups;
	while ((result = index.Prior(&key, &data, false, &key, &data)) == kIndexOK && (long) key == 10)
		count++;
	EXPECT(count == dups && (long) key == 9);
	// through the whole index: every key once, key 10 dups + 1 times
	key.Clear(); data.Clear();
	count = 0;
	result = index.First(&key, &data);
	while (result == kIndexOK)
	{
		count++;
		result = index.Next(&key, &data, kIndexNextDupOrKey, &key, &data);
	}
	EXPECT(count == 20 + dups);
	// skipping the dups
	key = 10L; data = 10L;
	EXPECT(index.Next(&key, &data, kIndexNextKey, &key, &data) == kIndexOK && (long) key == 11);
	key = 10L; data = 1150L;
	EXPECT(index.Next(&key, &data, kIndexNextKey, &key, &data) == kIndexOK && (long) key == 11);
	key = 11L; data = 11L;
	EXPECT(index.Prior(&key, &data, true, &key, &data) == kIndexOK && (long) key == 10);
	// Last gives the last datum of the last key; Prior into the dups from key 11 the last datum
	key = 11L; data = 11L;
	EXPECT(index.Prior(&key, &data, false, &key, &data) == kIndexOK && (long) key == 10 && (long) data == 1000 + dups);
	// Search with a stop function
	struct Stopper { static int Stop(SKey* k, SKey* d, void* refCon) { (*(long*) refCon)++; return (long) *d == 1200; } };
	long visited = 0;
	key = 10L; data = 1150L;
	EXPECT(index.Search(true, &key, &data, Stopper::Stop, &visited, &key, &data) == kIndexOK);
	EXPECT((long) key == 10 && (long) data == 1200 && visited == 50);
	visited = 0;
	EXPECT(index.Search(true, nil, nil, Stopper::Stop, &visited, &key, &data) == kIndexOK);
	EXPECT((long) data == 1200 && visited == 1 + 10 + 200);
	visited = 0;
	key = 19L; data = 19L;
	EXPECT(index.Search(false, &key, &data, Stopper::Stop, &visited, &key, &data) == kIndexOK);
	EXPECT((long) data == 1200 && visited == 8 + 101);
	struct Never { static int Stop(SKey*, SKey*, void* refCon) { (*(long*) refCon)++; return 0; } };
	visited = 0;
	key = 15L; data = 15L;
	EXPECT(index.Search(true, &key, &data, Never::Stop, &visited, &key, &data) == kIndexEnd && visited == 4);

	// deleting data: from the field, from the dup nodes, the key's own
	key = 10L; data = 1003L;
	EXPECT(index.Delete(&key, &data) == kIndexOK);
	key = 10L; data = 1250L;
	EXPECT(index.Delete(&key, &data) == kIndexOK);
	key = 10L; data = 10L;
	EXPECT(index.Delete(&key, &data) == kIndexOK);
	key = 10L;
	EXPECT(index.Find(&key, &key, &data, true) == kIndexOK && (long) data == 1001);
	count = 1;
	while ((result = index.Next(&key, &data, kIndexNextDup, &key, &data)) == kIndexOK)
	{
		EXPECT((long) data != 1003 && (long) data != 1250);
		count++;
	}
	EXPECT(count == dups - 2);
	// every datum of the key, from the end
	for (long i = dups; i >= 1; i--)
	{
		if (i == 3 || i == 250)
			continue;
		key = 10L;
		data = 1000 + i;
		EXPECT(index.Delete(&key, &data) == kIndexOK);
	}
	key = 10L;
	EXPECT(index.Find(&key, &key, &data, true) == kIndexNotFound && (long) key == 11);
	EXPECT(WalkForward(&index) == 19);
	long total, used;
	store->GetStoreSizes(&total, &used);
	EXPECT(used < 0x1000);

	// data over more than one dup node deleted from the front
	for (long i = 1; i <= dups; i++)
	{
		key = 10L;
		data = 2000 + i;
		EXPECT(index.Add(&key, &data) == kIndexOK);
	}
	for (long i = 1; i <= dups; i++)
	{
		key = 10L;
		data = 2000 + i;
		EXPECT(index.Delete(&key, &data) == kIndexOK);
	}
	EXPECT(WalkForward(&index) == 19);

	// a unique-key index refuses a second datum for a key
	TSoupIndex unique;
	MakeIndex(&wrapper, &unique, kKeyTypeLong, kKeyTypeLong, kIndexUniqueKeys);
	key = 1L; data = 1L;
	EXPECT(unique.Add(&key, &data) == kIndexOK);
	data = 2L;
	threw = false;
	newton_try
	{
		unique.Add(&key, &data);
	}
	newton_catch(exStoreError)
	{
		threw = (long) (Long) _info.exception.data == kIndexErrDuplicateKey;
	}
	end_try;
	EXPECT(threw);
	EXPECT(WalkForward(&unique) == 1);
	store->Delete();
}


// a string key's text: big-endian UniChars from ASCII, no terminator
static void
SetStringKey(SKey* key, const char* text)
{
	long len = strlen(text);
	UniChar chars[32];
	for (long i = 0; i < len; i++)
		chars[i] = (UniChar) text[i];
	SwapUniChars(chars, len);
	key->Set(len * sizeof(UniChar), chars);
}


static void
GetStringKey(const SKey* key, char* text)
{
	long len = key->Size() / sizeof(UniChar);
	UniChar chars[32];
	memcpy(chars, key->Data(), key->Size());
	SwapUniChars(chars, len);
	for (long i = 0; i < len; i++)
		text[i] = (char) chars[i];
	text[len] = 0;
}


static void
TestOtherKeyTypes()
{
	TStore* store = NewStore();
	TStoreWrapper wrapper(store);
	SKey key, data;
	char text[32];

	// strings: collated, case folded
	TSoupIndex strings;
	MakeIndex(&wrapper, &strings, kKeyTypeString, kKeyTypeLong, kIndexDuplicateKeys);
	const char* words[] = { "pear", "Apple", "fig", "banana", "cherry", "apple pie", "date" };
	for (long i = 0; i < 7; i++)
	{
		SetStringKey(&key, words[i]);
		data = i;
		EXPECT(strings.Add(&key, &data) == kIndexOK);
	}
	const char* sorted[] = { "Apple", "apple pie", "banana", "cherry", "date", "fig", "pear" };
	int result = strings.First(&key, &data);
	for (long i = 0; i < 7; i++)
	{
		EXPECT(result == kIndexOK);
		GetStringKey(&key, text);
		EXPECT(strcmp(text, sorted[i]) == 0);
		result = strings.Next(&key, &data, kIndexNextDupOrKey, &key, &data);
	}
	EXPECT(result == kIndexEnd);
	SetStringKey(&key, "CHERRY");
	EXPECT(strings.Find(&key, &key, &data, false) == kIndexOK && (long) data == 4);
	SetStringKey(&key, "coconut");
	EXPECT(strings.Find(&key, &key, &data, false) == kIndexNotFound);
	GetStringKey(&key, text);
	EXPECT(strcmp(text, "date") == 0);
	// a key and datum too long for a field
	TSoupIndex rawraw;
	MakeIndex(&wrapper, &rawraw, kKeyTypeRaw, kKeyTypeRaw, kIndexDuplicateKeys);
	char longText[80];
	memset(longText, 'z', sizeof(longText));
	key.Set(sizeof(longText), longText);
	EXPECT(key.Size() == kSKeyDataSize);				// clipped by Set
	data.Set(0x30, longText);
	Boolean threw = false;
	newton_try
	{
		rawraw.Add(&key, &data);
	}
	newton_catch(exStoreError)
	{
		threw = (long) (Long) _info.exception.data == -48022;
	}
	end_try;
	EXPECT(threw);
	data.Set(0x0e, longText);
	EXPECT(rawraw.Add(&key, &data) == kIndexOK);		// (2 + 78) + (2 + 14) + 4 = 100 fits
	EXPECT(rawraw.Find(&key, &key, &data, true) == kIndexOK && data.Size() == 0x0e);

	// characters
	TSoupIndex chars;
	MakeIndex(&wrapper, &chars, kKeyTypeChar, kKeyTypeLong, kIndexDuplicateKeys);
	for (unsigned short c = 'z'; c >= 'a'; c--)
	{
		key = c;
		data = (long) c;
		EXPECT(chars.Add(&key, &data) == kIndexOK);
	}
	result = chars.First(&key, &data);
	for (unsigned short c = 'a'; c <= 'z'; c++)
	{
		EXPECT(result == kIndexOK && (unsigned short) key == c);
		result = chars.Next(&key, &data, kIndexNextDupOrKey, &key, &data);
	}
	EXPECT(result == kIndexEnd);

	// doubles, descending
	TSoupIndex reals;
	MakeIndex(&wrapper, &reals, kKeyTypeDouble, kKeyTypeLong, kIndexDuplicateKeys);
	reals.fInfo.fDescending = 1;
	double values[] = { 1.5, -2.25, 100.0, 0.0, 3.14159 };
	for (long i = 0; i < 5; i++)
	{
		key = values[i];
		data = i;
		EXPECT(reals.Add(&key, &data) == kIndexOK);
	}
	double expected[] = { 100.0, 3.14159, 1.5, 0.0, -2.25 };
	result = reals.First(&key, &data);
	for (long i = 0; i < 5; i++)
	{
		EXPECT(result == kIndexOK && (double) key == expected[i]);
		result = reals.Next(&key, &data, kIndexNextDupOrKey, &key, &data);
	}
	EXPECT(result == kIndexEnd);

	// ASCII and raw
	TSoupIndex ascii;
	MakeIndex(&wrapper, &ascii, kKeyTypeASCII, kKeyTypeLong, kIndexDuplicateKeys);
	key.Set(3, "Zed"); data = 1L; EXPECT(ascii.Add(&key, &data) == kIndexOK);
	key.Set(5, "apple"); data = 2L; EXPECT(ascii.Add(&key, &data) == kIndexOK);
	key.Set(3, "app"); data = 3L; EXPECT(ascii.Add(&key, &data) == kIndexOK);
	EXPECT(ascii.First(&key, &data) == kIndexOK && (long) data == 3);
	EXPECT(ascii.Next(&key, &data, kIndexNextKey, &key, &data) == kIndexOK && (long) data == 2);
	EXPECT(ascii.Next(&key, &data, kIndexNextKey, &key, &data) == kIndexOK && (long) data == 1);
	TSoupIndex raw;
	MakeIndex(&wrapper, &raw, kKeyTypeRaw, kKeyTypeLong, kIndexDuplicateKeys);
	key.Set(3, "Zed"); data = 1L; EXPECT(raw.Add(&key, &data) == kIndexOK);
	key.Set(5, "apple"); data = 2L; EXPECT(raw.Add(&key, &data) == kIndexOK);
	key.Set(3, "app"); data = 3L; EXPECT(raw.Add(&key, &data) == kIndexOK);
	EXPECT(raw.First(&key, &data) == kIndexOK && (long) data == 1);
	EXPECT(raw.Next(&key, &data, kIndexNextKey, &key, &data) == kIndexOK && (long) data == 3);
	EXPECT(raw.Next(&key, &data, kIndexNextKey, &key, &data) == kIndexOK && (long) data == 2);

	// a multi-key of two longs, the second descending; a missing sub-key sorts first
	TSoupIndex multi;
	MakeIndex(&wrapper, &multi, kKeyTypeMulti, kKeyTypeLong, kIndexDuplicateKeys);
	multi.fInfo.fMultiTypes = 0xffffff11;
	multi.fInfo.fMultiAscending = 0xfd;
	struct { long a, b; } pairs[] = { {2, 1}, {1, 5}, {2, 9}, {1, 2}, {3, 0} };
	for (long i = 0; i < 5; i++)
	{
		key.Clear();
		PutBigEndianWord(key.Data(), (unsigned int) pairs[i].a);
		PutBigEndianWord(key.Data() + 4, (unsigned int) pairs[i].b);
		key.SetSize(8);
		data = i;
		EXPECT(multi.Add(&key, &data) == kIndexOK);
	}
	key.Clear();
	PutBigEndianWord(key.Data(), 2);
	key.SetSize(4);
	key.SetMissingKey(1);
	data = 5L;
	EXPECT(multi.Add(&key, &data) == kIndexOK);
	long order[] = { 1, 3, 5, 2, 0, 4 };			// (1,5) (1,2) (2,-) (2,9) (2,1) (3,0)
	result = multi.First(&key, &data);
	for (long i = 0; i < 6; i++)
	{
		EXPECT(result == kIndexOK && (long) data == order[i]);
		result = multi.Next(&key, &data, kIndexNextDupOrKey, &key, &data);
	}
	EXPECT(result == kIndexEnd);
	store->Delete();
}


static void
TestPersistence()
{
	TStore* store = NewStore();
	TStoreWrapper wrapper(store);
	PSSId infoId;
	{
		TSoupIndex index;
		MakeIndex(&wrapper, &index, kKeyTypeLong, kKeyTypeLong, kIndexDuplicateKeys, &infoId);
		SKey key, data;
		for (long i = 0; i < 1000; i++)
		{
			key = Scramble(i, 1000);
			data = Scramble(i, 1000);
			EXPECT(index.AddInTransaction(&key, &data) == kIndexOK);
		}
		for (long i = 0; i < 40; i++)
		{
			key = 500L;
			data = 5000 + i;
			EXPECT(index.AddInTransaction(&key, &data) == kIndexOK);
		}
		wrapper.fNodeCache.Commit(&index);
	}
	// the info object holds the root, big-endian
	UByte bytes[kIndexInfoSize];
	EXPECT(store->Read(infoId, 0, (char*) bytes, kIndexInfoSize) == noErr);
	EXPECT(GetBigEndianWord(bytes) != 0 && GetBigEndianWord(bytes + 4) == kSoupIndexNodeSize && GetBigEndianWord(bytes + 8) == kKeyTypeLong);
	// a root node on the store is compact: its size is what it holds
	long rootSize;
	EXPECT(store->GetObjectSize(GetBigEndianWord(bytes), &rootSize) == noErr);
	EXPECT(rootSize > 0x10 && rootSize < kSoupIndexNodeSize);

	// a fresh index over the same info, the cache cleared, reads it all back
	wrapper.fNodeCache.Clear();
	TSoupIndex again;
	again.Init(&wrapper, infoId, nil);
	EXPECT(again.fInfo.fRootNodeId == GetBigEndianWord(bytes));
	long first, last;
	EXPECT(WalkForward(&again, &first, &last, true) == 1000 + 40);
	EXPECT(first == 0 && last == 999);
	SKey key, data;
	key = 500L;
	EXPECT(again.Find(&key, &key, &data, true) == kIndexOK && (long) data == 500);
	long count = 0;
	while (again.Next(&key, &data, kIndexNextDup, &key, &data) == kIndexOK)
		count++;
	EXPECT(count == 40);
	// StoreAborted re-reads the info
	again.fInfo.fRootNodeId = 0;
	again.StoreAborted();
	EXPECT(again.fInfo.fRootNodeId == GetBigEndianWord(bytes));
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
		TestLongKeys();
		TestDuplicates();
		TestOtherKeyTypes();
		TestPersistence();
	}
	newton_catch_all
	{
		failures++;
		fprintf(stderr, "FAIL: unhandled exception %s (%ld)\n", _info.exception.name, (long) (Long) _info.exception.data);
	}
	end_try;
	if (failures == 0)
		printf("test_SoupIndex: all passed\n");
	else
		printf("test_SoupIndex: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
