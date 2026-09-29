// Host unit test for the flash store (src/stores/flash/FlashStore.h) on the
// internal flash (Flash.h) over the host's flash chips (hal/host/HostFlash.h):
// formatting, objects made, written, resized, replaced and deleted under
// the store's transactions (committed by the last unlock, undone by
// Abort), separate transactions, enough churn to make the store compact
// its blocks into the spare, and the store read back from the file.
//
// Runs as the kernel services task of a booted OS, as test_Flash does.

#include "FlashStore.h"
#include "MemoryAllocator.h"
#include "HostFlash.h"
#include "Host.h"
#include "Boot.h"
#include "UserBoot.h"
#include "OSErrors.h"
#include "host/TaskRuntime.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const char*	kFlashFile = "test_FlashStore.flash";


static Boolean
Holds(TStore* store, PSSId id, const char* text, long length = -1)
{
	if (length < 0)
		length = (long) strlen(text);
	long size = 0;
	if (store->GetObjectSize(id, &size) != noErr || size != length)
		return false;
	char buffer[256];
	if (size > (long) sizeof(buffer))
		return false;
	if (store->Read(id, 0, buffer, size) != noErr)
		return false;
	return memcmp(buffer, text, size) == 0;
}


static TNewInternalFlash*	gFlash;

static TFlashStore*
OpenStore(void)
{
	alignas(TNewInternalFlash) char memory[sizeof(TNewInternalFlash)];
	TNewInternalFlash::ClassInfo()->MakeAt(memory);
	TNewInternalFlash* boot = (TNewInternalFlash*) memory;
	boot->InitForReservedBlock(THeapAllocator::GetGlobalAllocator(), TNewInternalFlash::kMapWindows);
	boot->CleanUp();

	gFlash = (TNewInternalFlash*) TFlash::New("TNewInternalFlash");
	gFlash->Init(THeapAllocator::GetGlobalAllocator());
	TFlashStore* store = (TFlashStore*) TStore::New("TFlashStore");
	EXPECT(store != nil);
	EXPECT(store->Init(nil, gInternalFlashStoreSize, 0, 0, kFlashStoreUsesTFlash, gFlash) == noErr);
	return store;
}


static void
CloseStore(TFlashStore* store)
{
	store->Delete();
	gFlash->Delete();
}


static void
TestObjects(TFlashStore* store, PSSId* kept)
{
	Boolean flag = false;
	EXPECT(store->NeedsFormat(&flag) == noErr && flag);
	EXPECT(store->Format() == noErr);
	EXPECT(store->NeedsFormat(&flag) == noErr && !flag);
	EXPECT(store->IsReadOnly(&flag) == noErr && !flag);
	EXPECT(strcmp(store->StoreKind(), "Internal") == 0);
	EXPECT(store->fBlockSize == 0x20000 && store->fBlockCount == 30);
	PSSId root = 0;
	long size = -1;
	EXPECT(store->GetRootId(&root) == noErr && root == kFlashRootObjectId);
	EXPECT(store->GetObjectSize(root, &size) == noErr && size == 0);

	// (every change locks the store itself: one made with no lock of the
	// caller's commits at once)
	PSSId a = 0, b = 0;

	// made and committed
	EXPECT(store->LockStore() == noErr);
	EXPECT(store->NewObject((char*) "hello", 5, &a) == noErr && a != 0);
	EXPECT(store->NewObject(8, &b) == noErr && b != a);
	EXPECT(store->InTransaction());
	EXPECT(store->UnlockStore() == noErr);
	EXPECT(!store->InTransaction());
	EXPECT(Holds(store, a, "hello"));
	char buffer[64];
	memset(buffer, 0, sizeof(buffer));
	EXPECT(store->Read(b, 0, buffer, 8) == noErr && (unsigned char) buffer[0] == 0xFF);	// never written: blank flash

	// a change undone by Abort ...
	EXPECT(store->LockStore() == noErr);
	EXPECT(store->Write(a, 0, (char*) "HE", 2) == noErr);
	EXPECT(Holds(store, a, "HEllo"));
	EXPECT(store->Abort() == noErr);
	EXPECT(Holds(store, a, "hello"));
	// ... and kept by the last unlock
	EXPECT(store->LockStore() == noErr);
	EXPECT(store->Write(a, 0, (char*) "HE", 2) == noErr);
	EXPECT(store->Write(a, 3, (char*) "LO", 2) == noErr);
	EXPECT(store->UnlockStore() == noErr);
	EXPECT(Holds(store, a, "HElLO"));

	// overruns
	EXPECT(store->LockStore() == noErr);
	EXPECT(store->Write(a, 4, (char*) "abc", 3) == kSError_ObjectOverRun);
	EXPECT(store->Read(a, 3, buffer, 5) == kSError_ObjectOverRun);
	EXPECT(store->UnlockStore() == noErr);

	// resized, replaced, deleted
	EXPECT(store->LockStore() == noErr);
	EXPECT(store->SetObjectSize(a, 8) == noErr);
	EXPECT(store->GetObjectSize(a, &size) == noErr && size == 8);
	EXPECT(store->SetObjectSize(a, 2) == noErr);
	EXPECT(store->UnlockStore() == noErr);
	EXPECT(Holds(store, a, "HE"));
	EXPECT(store->LockStore() == noErr);
	EXPECT(store->ReplaceObject(a, (char*) "goodbye", 7) == noErr);
	EXPECT(store->UnlockStore() == noErr);
	EXPECT(Holds(store, a, "goodbye"));
	EXPECT(store->LockStore() == noErr);
	EXPECT(store->DeleteObject(b) == noErr);
	EXPECT(store->Abort() == noErr);
	EXPECT(store->GetObjectSize(b, &size) == noErr && size == 8);
	EXPECT(store->LockStore() == noErr);
	EXPECT(store->DeleteObject(b) == noErr);
	EXPECT(store->UnlockStore() == noErr);
	EXPECT(store->GetObjectSize(b, &size) == kSError_ObjectNotFound);
	EXPECT(store->GetObjectSize(0, &size) == kSError_BadPSSID);

	// a separate transaction outlives the store's abort, and its own
	// abort undoes it
	PSSId c = 0;
	EXPECT(store->NewWithinTransaction(4, &c) == noErr && c != 0);
	EXPECT(store->InSeparateTransaction(c));
	EXPECT(store->LockStore() == noErr);
	EXPECT(store->Write(c, 0, (char*) "four", 4) == noErr);
	EXPECT(store->Write(a, 0, (char*) "G", 1) == noErr);
	EXPECT(store->Abort() == noErr);
	EXPECT(Holds(store, a, "goodbye"));
	EXPECT(Holds(store, c, "four"));
	EXPECT(store->SeparatelyAbort(c) == noErr);
	EXPECT(store->GetObjectSize(c, &size) == kSError_ObjectNotFound);
	// or joins the store's
	EXPECT(store->NewWithinTransaction(2, &c) == noErr);
	EXPECT(store->LockStore() == noErr);
	EXPECT(store->Write(c, 0, (char*) "hi", 2) == noErr);
	EXPECT(store->AddToCurrentTransaction(c) == noErr);
	EXPECT(store->UnlockStore() == noErr);
	EXPECT(!store->InSeparateTransaction(c));
	EXPECT(Holds(store, c, "hi"));

	long total = 0, used = 0;
	EXPECT(store->GetStoreSizes(&total, &used) == noErr && total > 0x300000 && used > 0 && used < total);
	kept[0] = a;
	kept[1] = c;
}


// Objects made and deleted until the store has been round every block and
// had to compact some: the ones kept all along must still read back.
static void
TestCompaction(TFlashStore* store, PSSId* kept)
{
	static char data[0x1000];
	for (int i = 0; i < (int) sizeof(data); i++)
		data[i] = (char) (i * 7);
	ULong erasesBefore = gHostFlashErases;
	PSSId survivors[16];
	int count = 0;
	for (int round = 0; round < 1200; round++)
	{
		EXPECT(store->LockStore() == noErr);
		PSSId id = 0;
		NewtonErr err = store->NewObject(data, sizeof(data), &id);
		EXPECT(err == noErr);
		EXPECT(store->UnlockStore() == noErr);
		if (err != noErr)
			break;
		if (round % 100 == 0 && count < 16)
			survivors[count++] = id;
		else
		{
			EXPECT(store->LockStore() == noErr);
			EXPECT(store->DeleteObject(id) == noErr);
			EXPECT(store->UnlockStore() == noErr);
		}
	}
	EXPECT(gHostFlashErases > erasesBefore);		// it compacted
	for (int i = 0; i < count; i++)
	{
		long size = 0;
		EXPECT(store->GetObjectSize(survivors[i], &size) == noErr && size == (long) sizeof(data));
		char back[0x100];
		EXPECT(store->Read(survivors[i], 0x800, back, sizeof(back)) == noErr && memcmp(back, data + 0x800, sizeof(back)) == 0);
	}
	EXPECT(Holds(store, kept[0], "goodbye"));
	EXPECT(Holds(store, kept[1], "hi"));
	kept[2] = count > 0 ? survivors[count - 1] : 0;
}


static void
FlashStoreScenario(void)
{
	TNewInternalFlash::ClassInfo()->Register();
	TFlashStore::ClassInfo()->Register();
	remove(kFlashFile);
	EXPECT(HostFlashOpen(kFlashFile) == noErr);
	HostClearSections();

	PSSId kept[3] = { 0, 0, 0 };
	TFlashStore* store = OpenStore();
	TestObjects(store, kept);
	TestCompaction(store, kept);
	CloseStore(store);
	HostFlashClose();

	// the store as the file left it
	EXPECT(HostFlashOpen(kFlashFile) == noErr);
	store = OpenStore();
	Boolean needsFormat = true;
	EXPECT(store->NeedsFormat(&needsFormat) == noErr && !needsFormat);
	EXPECT(Holds(store, kept[0], "goodbye"));
	EXPECT(Holds(store, kept[1], "hi"));
	long size = 0;
	EXPECT(kept[2] == 0 || (store->GetObjectSize(kept[2], &size) == noErr && size == 0x1000));
	// a transaction left under way when the "power went" is undone at the next start
	EXPECT(store->LockStore() == noErr);
	EXPECT(store->Write(kept[0], 0, (char*) "G", 1) == noErr);
	CloseStore(store);
	HostFlashClose();
	EXPECT(HostFlashOpen(kFlashFile) == noErr);
	store = OpenStore();
	EXPECT(Holds(store, kept[0], "goodbye"));
	CloseStore(store);
	HostFlashClose();
	remove(kFlashFile);
	HostStopTasks();
}


int
main()
{
	gHostKernelServicesTask = FlashStoreScenario;
	OsBoot();
	if (failures == 0)
		printf("test_FlashStore: all passed\n");
	else
		printf("test_FlashStore: %d failures\n", failures);
	return failures != 0;
}
