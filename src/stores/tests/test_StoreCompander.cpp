// Store-compander test: the compression layer a store keeps its data
// behind (StoreCompander.h).  The OS is booted so the protocol registry
// exists; in the kernel services task the compression implementations and
// the companders are registered, a THostStore is formatted and given a
// chunk table of empty block objects, and each compander is driven through
// its interface: 0x400-byte blocks of text, runs and noise written and
// read back (a round trip), an unwritten block reading back as zeroes,
// partial blocks, and BlockSize/IsReadOnly.

#include "StoreCompander.h"
#include "Compression.h"
#include "Store.h"
#include "host/HostStore.h"
#include "Boot.h"
#include "KernelGlobals.h"
#include "UserBoot.h"
#include "NewtonMemory.h"
#include "OSErrors.h"
#include "host/TaskRuntime.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const int kNumBlocks = 4;


// A repeatable pattern for one block: runs, text and noise, so the coder
// meets both compressible and incompressible data.
static void
Fill(UByte* p, long size, unsigned seed)
{
	srand(seed);
	long i = 0;
	while (i < size)
	{
		int kind = rand() % 3;
		long n = 1 + rand() % 90;
		if (i + n > size)
			n = size - i;
		if (kind == 0)
			memset(p + i, rand() & 0xff, n);
		else if (kind == 1 && i > 40)
		{
			long back = 1 + rand() % 40;
			for (long j = 0; j < n; j++)
				p[i + j] = p[i + j - back];
		}
		else
			for (long j = 0; j < n; j++)
				p[i + j] = (UByte) rand();
		i += n;
	}
}


// Format a store and give it a chunk table of kNumBlocks empty block
// objects; answer the root object whose first word names the chunk table.
static PSSId
MakeCompressedStore(TStore* store)
{
	store->Format();
	StorePSSId table[kNumBlocks];
	for (int i = 0; i < kNumBlocks; i++)
	{
		PSSId blockId = 0;
		EXPECT(store->NewObject((long) 0, &blockId) == noErr);
		table[i] = blockId;
	}
	PSSId chunkTableId = 0;
	EXPECT(store->NewObject((char*) table, sizeof(table), &chunkTableId) == noErr);
	PackageRoot root;
	root.fChunkTableId = chunkTableId;
	PSSId rootId = 0;
	EXPECT(store->NewObject((char*) &root, sizeof(PackageRoot), &rootId) == noErr);
	return rootId;
}


// Write each block, read it back and check it survived; an unwritten block
// reads back as zeroes.
static void
RoundTrip(const char* implementation)
{
	TStore* store = TStore::New("THostStore");
	EXPECT(store != nil);
	if (store == nil)
		return;
	EXPECT(store->Init(nil, 0x40000, 0, 0, kStoreIsInternal, nil) == noErr);
	PSSId rootId = MakeCompressedStore(store);

	TStoreCompander* compander = TStoreCompander::New(implementation);
	EXPECT(compander != nil);
	if (compander != nil)
	{
		EXPECT(compander->Init(store, rootId, 0, 0, 0) == noErr);
		EXPECT(compander->BlockSize() == (ULong) kCompanderBlockSize);
		EXPECT(!compander->IsReadOnly());

		UByte* source = (UByte*) NewPtr(kCompanderBlockSize);
		UByte* restored = (UByte*) NewPtr(kCompanderBlockSize);

		// blocks 0..2 written full; block 3 left untouched (reads as zero)
		for (int i = 0; i < kNumBlocks - 1; i++)
		{
			long len = (i == kNumBlocks - 2) ? 250 : kCompanderBlockSize;	// a partial last block
			Fill(source, len, 0x1000 + i);
			EXPECT(compander->Write((ULong) i << 10, (char*) source, len, 0) == noErr);
			memset(restored, 0xee, len);
			EXPECT(compander->Read((ULong) i << 10, (char*) restored, len, 0) == noErr);
			Fill(source, len, 0x1000 + i);
			EXPECT(memcmp(source, restored, len) == 0);
		}

		// the untouched block reads back as zeroes
		memset(restored, 0xee, kCompanderBlockSize);
		EXPECT(compander->Read((ULong) (kNumBlocks - 1) << 10, (char*) restored, kCompanderBlockSize, 0) == noErr);
		Boolean allZero = true;
		for (long j = 0; j < kCompanderBlockSize; j++)
			if (restored[j] != 0)
				allZero = false;
		EXPECT(allZero);

		// rewriting a block replaces it
		Fill(source, kCompanderBlockSize, 0x99);
		EXPECT(compander->Write(0, (char*) source, kCompanderBlockSize, 0) == noErr);
		memset(restored, 0, kCompanderBlockSize);
		EXPECT(compander->Read(0, (char*) restored, kCompanderBlockSize, 0) == noErr);
		Fill(source, kCompanderBlockSize, 0x99);
		EXPECT(memcmp(source, restored, kCompanderBlockSize) == 0);

		DisposePtr((Ptr) source);
		DisposePtr((Ptr) restored);
		compander->Delete();
	}
	store->Delete();
}


static void
CompanderScenario()
{
	RegisterStoreImplementations();
	InitializeCompression();
	InitializeStoreCompanders();

	// the companders are registered under the TStoreCompander interface
	EXPECT(ClassInfoByName("TStoreCompander", "TSimpleStoreCompander") == TSimpleStoreCompander::ClassInfo());
	EXPECT(ClassInfoByName("TStoreCompander", "TLZStoreCompander") == TLZStoreCompander::ClassInfo());
	EXPECT(TSimpleStoreCompander::ClassInfo()->Size() == sizeof(TSimpleStoreCompander));
	EXPECT(TLZStoreCompander::ClassInfo()->Size() == sizeof(TLZStoreCompander));

	RoundTrip("TSimpleStoreCompander");
	RoundTrip("TLZStoreCompander");

	HostStopTasks();
}


int main()
{
	gHostKernelServicesTask = CompanderScenario;
	OsBoot();
	if (failures == 0)
		printf("test_StoreCompander: all passed\n");
	else
		printf("test_StoreCompander: %d failures\n", failures);
	return failures != 0;
}
