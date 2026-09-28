// Large objects test (src/stores/LargeObjects.h): with the companders
// registered (so the test runs as the kernel services task), large objects
// are made on a THostStore through each compander and taken through the
// ROM domain manager's requests - the host's (HostLargeObjects.cpp): made
// empty and complete ('paok'), mapped, written, flushed, unmapped and
// mapped again (the data read back through the compander), grown at the
// end and cut in the middle, committed (the chunk array grown to match and
// the root's size brought up to date), a change aborted (the object
// unmapped and the committed data what comes back), its storage size
// counted, filled from a pipe as it is made, and deleted; an object that
// is not complete, and a compander nobody knows, refused.

#include "LargeObjects.h"
#include "StoreCompander.h"
#include "Store.h"
#include "Compression.h"
#include "ByteOrder.h"
#include "Protocols.h"
#include "Boot.h"
#include "KernelGlobals.h"
#include "UserBoot.h"
#include "NewtonMemory.h"
#include "OSErrors.h"
#include "host/TaskRuntime.h"
#include "../../utility/tests/TestPipe.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


static UByte
Pattern(long i)
{
	return (UByte) ((i * 7 + (i >> 8)) & 0xff);
}


static long
ChunkCount(TStore* store, PSSId id)
{
	UByte root[kLargeObjectRootSize];
	if (store->Read(id, 0, (char*) root, kLargeObjectRootSize) != noErr)
		return -1;
	long size = 0;
	store->GetObjectSize(GetBigEndianWord(root + kLORootChunkArray), &size);
	return size >> 2;
}


static long
RootSize(TStore* store, PSSId id)
{
	UByte root[kLargeObjectRootSize];
	if (store->Read(id, 0, (char*) root, kLargeObjectRootSize) != noErr)
		return -1;
	return (long) GetBigEndianWord(root + kLORootSize);
}


static void
Scenario(const char* compander)
{
	TStore* store = TStore::New("THostStore");
	EXPECT(store != nil);
	if (store == nil)
		return;
	EXPECT(store->Init(nil, 0x80000, 0, 0, kStoreIsInternal, nil) == noErr);
	store->Format();

	// made empty: complete, not a package, its compander named
	ULong id = 0;
	EXPECT(CreateLargeObject(&id, store, 3000, (char*) compander, nil, 0) == noErr && id != 0);
	EXPECT(PackageAllocationOk(store, id) && !IsOnStoreAsPackage(store, id));
	EXPECT(ChunkCount(store, id) == 3 && RootSize(store, id) == 3000);
	char* name = nil;
	EXPECT(LOCompanderName(&name, store, id) == noErr && name != nil && strcmp(name, compander) == 0);
	free(name);

	// mapped, written, flushed, unmapped, mapped again
	ULong address = 0;
	EXPECT(MapLargeObject(&address, store, id, false) == noErr && address != 0);
	EXPECT(ObjectSize(address) == 3000 && LargeObjectIsDirty(address) && !LargeObjectIsReadOnly(address));
	UByte* data = (UByte*) address;
	Boolean zero = true;
	for (long i = 0; i < 3000; i++)
		zero = zero && data[i] == 0;
	EXPECT(zero);
	for (long i = 0; i < 3000; i++)
		data[i] = Pattern(i);
	ULong again = 0;
	EXPECT(MapLargeObject(&again, store, id, false) == noErr && again == address);		// (mapped already: the same)
	TStore* whose = nil;
	ULong whoseId = 0, base = 0, at = 0;
	EXPECT(VAddrToStore(&whose, &whoseId, address) == noErr && whose == store && whoseId == id);
	EXPECT(StoreToVAddr(&at, store, id) == noErr && at == address);
	EXPECT(VAddrToBase(&base, address + 100) == noErr && base == address);
	EXPECT(FlushLargeObject(store, id) == noErr);
	EXPECT(UnmapLargeObject(address) == noErr && StoreToVAddr(&at, store, id) != noErr);
	EXPECT(MapLargeObject(&address, store, id, false) == noErr);
	data = (UByte*) address;
	Boolean same = true;
	for (long i = 0; i < 3000; i++)
		same = same && data[i] == Pattern(i);
	EXPECT(same);

	// grown at the end, committed: the chunk array and the root follow
	ULong grown = 0;
	EXPECT(ResizeLargeObject(&grown, address, 5000, -1) == noErr && ObjectSize(grown) == 5000);
	data = (UByte*) grown;
	same = true;
	for (long i = 0; i < 3000; i++)
		same = same && data[i] == Pattern(i);
	EXPECT(same && data[4999] == 0);
	for (long i = 3000; i < 5000; i++)
		data[i] = Pattern(i);
	EXPECT(CommitObject(grown) == noErr);
	EXPECT(ChunkCount(store, id) == 5 && RootSize(store, id) == 5000);

	// cut in the middle: 1000 bytes taken out at 100
	ULong cut = 0;
	EXPECT(ResizeLargeObject(&cut, grown, 4000, 100) == noErr && ObjectSize(cut) == 4000);
	data = (UByte*) cut;
	EXPECT(data[99] == Pattern(99) && data[100] == Pattern(1100) && data[3999] == Pattern(4999));
	EXPECT(CommitObject(cut) == noErr && RootSize(store, id) == 4000);

	// a change aborted: unmapped, and the committed data comes back
	data[0] = (UByte) ~Pattern(0);
	EXPECT(AbortObject(store, id) == noErr && StoreToVAddr(&at, store, id) != noErr);
	EXPECT(MapLargeObject(&address, store, id, false) == noErr);
	data = (UByte*) address;
	EXPECT(ObjectSize(address) == 4000 && data[0] == Pattern(0) && data[100] == Pattern(1100));
	EXPECT(StorageSizeOfLargeObject(store, id) > 0);

	// deleted: unmapped
	EXPECT(DeleteLargeObject(store, id) == noErr && StoreToVAddr(&at, store, id) != noErr);

	// filled from a pipe as it is made
	const long kPiped = 2500;
	CTestPipe pipe(kPiped);
	UByte bytes[kPiped];
	for (long i = 0; i < kPiped; i++)
		bytes[i] = Pattern(i + 17);
	pipe.WriteChunk(bytes, kPiped, false);
	pipe.Rewind();
	ULong piped = 0;
	EXPECT(LODefaultCreate(&piped, store, &pipe, kPiped, false, (char*) compander, nil, 0, nil) == noErr);
	EXPECT(MapLargeObject(&address, store, piped, true) == noErr && ObjectSize(address) == kPiped);
	EXPECT(LargeObjectIsReadOnly(address) && !LargeObjectIsDirty(address));
	EXPECT(memcmp((void*) address, bytes, kPiped) == 0);
	EXPECT(UnmapLargeObject(address) == noErr);

	// refused: an object that is not complete, a compander nobody knows
	UByte notDone[kLargeObjectRootSize];
	memset(notDone, 0, sizeof(notDone));
	PSSId notDoneId = 0;
	EXPECT(store->NewObject((char*) notDone, sizeof(notDone), &notDoneId) == noErr);
	EXPECT(MapLargeObject(&address, store, notDoneId, false) == kError_Bad_Object);
	ULong unknown = 0;
	EXPECT(CreateLargeObject(&unknown, store, 100, (char*) "TNobodysCompander", nil, 0) == kError_Bad_Parameters);

	store->Delete();
}


static void
LargeObjectScenario()
{
	RegisterStoreImplementations();
	InitializeCompression();
	InitializeStoreCompanders();
	Scenario("TSimpleStoreCompander");
	Scenario("TLZStoreCompander");
	HostStopTasks();
}


int main()
{
	gHostKernelServicesTask = LargeObjectScenario;
	OsBoot();
	if (failures == 0)
		printf("test_LargeObjects: all passed\n");
	else
		printf("test_LargeObjects: %d failures\n", failures);
	return failures != 0;
}
