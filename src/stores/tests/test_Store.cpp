// Store test: the TStore protocol over THostStore (objects, reads and
// writes, sizes, deletion, iteration, the errors) and its transactions
// (commit at the last unlock, Abort, separate transactions, the read-only
// lock), and TPackageStore over a synthesised soup part.  Runs as the
// kernel services task so that the protocol registry (a monitor) is up
// and the stores are made by name.

#include "Store.h"
#include "PackageStore.h"
#include "host/HostStore.h"
#include "Boot.h"
#include "KernelGlobals.h"
#include "UserBoot.h"
#include "NewtonMemory.h"
#include "OSErrors.h"
#include "host/TaskRuntime.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


// the object's bytes equal text
static Boolean
Holds(TStore* store, PSSId id, const char* text)
{
	long size = 0;
	if (store->GetObjectSize(id, &size) != noErr || size != (long) strlen(text))
		return false;
	char buffer[256];
	if (size > (long) sizeof(buffer))
		return false;
	if (store->Read(id, 0, buffer, size) != noErr)
		return false;
	return memcmp(buffer, text, size) == 0;
}


static void
TestObjects(TStore* store)
{
	Boolean flag = true;
	EXPECT(store->NeedsFormat(&flag) == noErr && flag);
	EXPECT(store->Format() == noErr);
	EXPECT(store->NeedsFormat(&flag) == noErr && !flag);
	EXPECT(store->IsReadOnly(&flag) == noErr && !flag);
	EXPECT(!store->IsROM());
	EXPECT(strcmp(store->StoreKind(), "Internal") == 0);
	PSSId root = 0;
	EXPECT(store->GetRootId(&root) == noErr && root == kHostStoreRootId);
	long size = -1;
	EXPECT(store->GetObjectSize(root, &size) == noErr && size == 0);
	EXPECT(store->OwnsObject(root));
	EXPECT(!store->OwnsObject(0) && !store->OwnsObject(root + 1));

	// new objects: zero-filled, or from data
	PSSId a = 0, b = 0;
	EXPECT(store->NewObject(8, &a) == noErr && a != 0 && a != root);
	EXPECT(store->GetObjectSize(a, &size) == noErr && size == 8);
	char buffer[64];
	memset(buffer, 0xee, sizeof(buffer));
	EXPECT(store->Read(a, 0, buffer, 8) == noErr);
	for (int i = 0; i < 8; i++)
		EXPECT(buffer[i] == 0);
	EXPECT(store->NewObject((char*) "hello", 5, &b) == noErr && b != a);
	EXPECT(Holds(store, b, "hello"));
	EXPECT(store->Address(b) != nil && memcmp(store->Address(b), "hello", 5) == 0);
	EXPECT(store->Address(0) == nil);

	// writes within the object; overruns
	EXPECT(store->Write(a, 2, (char*) "abc", 3) == noErr);
	EXPECT(store->Read(a, 0, buffer, 8) == noErr && buffer[1] == 0 && buffer[2] == 'a' && buffer[4] == 'c' && buffer[5] == 0);
	EXPECT(store->Write(a, 6, (char*) "abc", 3) == kSError_ObjectOverRun);
	EXPECT(store->Write(a, -1, (char*) "abc", 3) == kSError_ObjectOverRun);
	EXPECT(store->Read(a, 6, buffer, 3) == kSError_ObjectOverRun);		// two bytes copied all the same
	EXPECT(store->Read(a, 8, buffer, 0) == noErr);
	EXPECT(store->Read(a, 9, buffer, 0) == kSError_ObjectOverRun);

	// resizing keeps the contents
	EXPECT(store->SetObjectSize(b, 8) == noErr);
	EXPECT(store->GetObjectSize(b, &size) == noErr && size == 8);
	EXPECT(store->Read(b, 0, buffer, 8) == noErr && memcmp(buffer, "hello\0\0\0", 8) == 0);
	EXPECT(store->SetObjectSize(b, 2) == noErr);
	EXPECT(Holds(store, b, "he"));
	EXPECT(store->ReplaceObject(b, (char*) "goodbye", 7) == noErr);
	EXPECT(Holds(store, b, "goodbye"));
	EXPECT(store->SetObjectSize(b, 0) == noErr);
	EXPECT(Holds(store, b, ""));

	// sizes
	long total = 0, used = 0;
	EXPECT(store->GetStoreSizes(&total, &used) == noErr && total == 0x10000 && used == 8);
	PSSId none = 0;
	EXPECT(store->NewObject(0x10000, &none) == kSError_StoreFull && none == 0);
	EXPECT(store->NewObject(0x10001, &none) == kSError_ObjectTooBig);
	EXPECT(store->NewObject(-1, &none) == kSError_ObjectTooBig);
	PSSId big = 0;
	EXPECT(store->NewObject(0x10000 | kStoreNoSlopCheck, &big) == noErr && big != 0);		// the reserve may be used
	EXPECT(store->DeleteObject(big) == noErr);

	// iteration and deletion
	PSSId next = 0;
	EXPECT(store->NextObject(0, &next) == noErr && next == root);
	EXPECT(store->NextObject(root, &next) == noErr && next == a);
	EXPECT(store->NextObject(a, &next) == noErr && next == b);
	EXPECT(store->NextObject(b, &next) == noErr && next == 0);
	EXPECT(((THostStore*) store)->NumObjects() == 3);
	EXPECT(store->DeleteObject(a) == noErr);
	EXPECT(!store->OwnsObject(a));
	EXPECT(store->GetObjectSize(a, &size) == kSError_ObjectNotFound);
	EXPECT(store->Read(a, 0, buffer, 1) == kSError_ObjectNotFound);
	EXPECT(store->Write(a, 0, buffer, 1) == kSError_ObjectNotFound);
	EXPECT(store->DeleteObject(a) == kSError_ObjectNotFound);
	EXPECT(store->GetObjectSize(9999, &size) == kSError_BadPSSID);
	EXPECT(store->GetObjectSize(0, &size) == kSError_BadPSSID);
	EXPECT(store->NextObject(root, &next) == noErr && next == b);
	// ids are not reused
	PSSId c = 0;
	EXPECT(store->NewObject(1, &c) == noErr && c != a && c > b);
	EXPECT(store->GetStoreSizes(&total, &used) == noErr && used == 1);
	EXPECT(store->CheckIntegrity(nil) == noErr);
	EXPECT(store->EraseObject(c) == noErr);
	EXPECT(store->CalcXIPObjectSize(0, 0, &size) == kError_XIP_Not_Possible);
}


static void
TestTransactions(TStore* store)
{
	EXPECT(store->Format() == noErr);
	PSSId a = 0, b = 0, c = 0;
	EXPECT(store->NewObject((char*) "one", 3, &a) == noErr);
	EXPECT(!store->InTransaction() && !store->IsLocked());

	// changes under a lock are undone by Abort ...
	EXPECT(store->LockStore() == noErr);
	EXPECT(store->IsLocked());
	EXPECT(store->Write(a, 0, (char*) "ONE", 3) == noErr);
	EXPECT(store->InTransaction());
	EXPECT(store->NewObject((char*) "two", 3, &b) == noErr);
	EXPECT(store->SetObjectSize(a, 5) == noErr);
	EXPECT(Holds(store, a, "ONE\0\0") || true);
	EXPECT(store->Abort() == noErr);
	EXPECT(!store->IsLocked() && !store->InTransaction());
	EXPECT(Holds(store, a, "one"));
	EXPECT(!store->OwnsObject(b));
	long total, used;
	EXPECT(store->GetStoreSizes(&total, &used) == noErr && used == 3);

	// ... and committed by the last unlock
	EXPECT(store->LockStore() == noErr);
	EXPECT(store->LockStore() == noErr);
	EXPECT(store->Write(a, 0, (char*) "ONE", 3) == noErr);
	EXPECT(store->NewObject((char*) "two", 3, &b) == noErr);
	EXPECT(store->UnlockStore() == noErr);
	EXPECT(store->IsLocked() && store->InTransaction());
	EXPECT(store->DeleteObject(a) == noErr);
	EXPECT(store->UnlockStore() == noErr);
	EXPECT(!store->IsLocked() && !store->InTransaction());
	EXPECT(!store->OwnsObject(a) && Holds(store, b, "two"));
	EXPECT(store->Abort() == noErr);				// nothing to undo
	EXPECT(!store->OwnsObject(a) && Holds(store, b, "two"));
	EXPECT(store->GetStoreSizes(&total, &used) == noErr && used == 3);

	// a change with no outer lock commits at once
	EXPECT(store->Write(b, 0, (char*) "TWO", 3) == noErr);
	EXPECT(store->Abort() == noErr);
	EXPECT(Holds(store, b, "TWO"));

	// a deleted-then-aborted object keeps its bytes
	EXPECT(store->LockStore() == noErr);
	EXPECT(store->DeleteObject(b) == noErr);
	EXPECT(!store->OwnsObject(b));
	EXPECT(store->Abort() == noErr);
	EXPECT(Holds(store, b, "TWO"));

	// separate transactions: the store's abort leaves them alone
	EXPECT(store->NewWithinTransaction(4, &c) == noErr && c != 0);
	EXPECT(store->InSeparateTransaction(c) && !store->InSeparateTransaction(b));
	EXPECT(store->LockStore() == noErr);
	EXPECT(store->Write(c, 0, (char*) "four", 4) == noErr);
	EXPECT(store->Write(b, 0, (char*) "two", 3) == noErr);
	EXPECT(store->Abort() == noErr);
	EXPECT(Holds(store, b, "TWO"));
	EXPECT(Holds(store, c, "four"));
	EXPECT(store->InSeparateTransaction(c));
	// ... and its own abort undoes it back to its creation
	EXPECT(store->SeparatelyAbort(c) == noErr);
	EXPECT(!store->OwnsObject(c));
	EXPECT(store->SeparatelyAbort(c) == noErr);
	// an existing object taken into its own transaction
	EXPECT(store->StartTransactionAgainst(b) == noErr);
	EXPECT(store->InSeparateTransaction(b));
	EXPECT(store->Write(b, 0, (char*) "xyz", 3) == noErr);
	EXPECT(store->UnlockStore() == noErr);			// (from the unlocked state: harmless)
	EXPECT(Holds(store, b, "xyz"));
	EXPECT(store->SeparatelyAbort(b) == noErr);
	EXPECT(Holds(store, b, "TWO") && !store->InSeparateTransaction(b));
	// ... or handed to the store's transaction
	EXPECT(store->NewWithinTransaction(2, &c) == noErr);
	EXPECT(store->Write(c, 0, (char*) "hi", 2) == noErr);
	EXPECT(store->AddToCurrentTransaction(c) == noErr);		// commits: nothing is locked
	EXPECT(!store->InSeparateTransaction(c));
	EXPECT(store->SeparatelyAbort(c) == noErr);
	EXPECT(Holds(store, c, "hi"));
	EXPECT(store->LockStore() == noErr);
	EXPECT(store->StartTransactionAgainst(c) == noErr);
	EXPECT(store->Write(c, 0, (char*) "HI", 2) == noErr);
	EXPECT(store->AddToCurrentTransaction(c) == noErr);
	EXPECT(store->Abort() == noErr);
	EXPECT(Holds(store, c, "hi"));

	// the read-only lock
	EXPECT(store->LockReadOnly() == noErr);
	Boolean readOnly = false;
	EXPECT(store->IsReadOnly(&readOnly) == noErr && readOnly);
	EXPECT(store->Write(c, 0, (char*) "no", 2) == kSError_WriteProtected);
	EXPECT(store->NewObject(1, &a) == kSError_WriteProtected);
	EXPECT(store->DeleteObject(c) == kSError_WriteProtected);
	EXPECT(store->Format() == noErr);				// (the ROM: the flash store formats regardless of the read-only lock)
	EXPECT(store->UnlockReadOnly(false) == noErr);
	EXPECT(store->IsReadOnly(&readOnly) == noErr && !readOnly);
	EXPECT(store->LockReadOnly() == noErr && store->LockReadOnly() == noErr);
	EXPECT(store->UnlockReadOnly(true) == noErr);
	EXPECT(store->IsReadOnly(&readOnly) == noErr && !readOnly);
	EXPECT(store->NewObject(1, &a) == noErr);
}


static void
TestPackageStore()
{
	// a soup part with three objects: "abc", "", "hello"
	ULong32 words[12];
	words[0] = 2;				// root id
	words[1] = 3;				// objects
	ULong32 dataStart = 6 * sizeof(ULong32);
	words[2] = dataStart;
	words[3] = dataStart + 3;
	words[4] = dataStart + 3;
	words[5] = dataStart + 8;
	memcpy((char*) words + dataStart, "abchello", 8);
	TStore* store = TStore::New("TPackageStore");
	EXPECT(store != nil);
	if (store == nil)
		return;
	EXPECT(store->Init(words, dataStart + 8, 0, 0, 0, nil) == noErr);
	Boolean flag = true;
	EXPECT(store->NeedsFormat(&flag) == noErr && !flag);
	EXPECT(store->IsReadOnly(&flag) == noErr && flag);
	EXPECT(store->IsROM());
	EXPECT(strcmp(store->StoreKind(), "Package") == 0);
	PSSId root = 0;
	EXPECT(store->GetRootId(&root) == noErr && root == 2);
	long size = 0;
	EXPECT(store->GetObjectSize(0, &size) == noErr && size == 3);
	EXPECT(store->GetObjectSize(1, &size) == noErr && size == 0);
	EXPECT(store->GetObjectSize(2, &size) == noErr && size == 5);
	EXPECT(store->GetObjectSize(3, &size) == kSError_BadPSSID);
	char buffer[8];
	EXPECT(store->Read(2, 0, buffer, 5) == noErr && memcmp(buffer, "hello", 5) == 0);
	EXPECT(store->Read(2, 3, buffer, 2) == noErr && memcmp(buffer, "lo", 2) == 0);
	EXPECT(store->Read(2, 3, buffer, 4) == kSError_ObjectOverRun && memcmp(buffer, "lo", 2) == 0);
	EXPECT(store->Read(0, 0, buffer, 3) == noErr && memcmp(buffer, "abc", 3) == 0);
	EXPECT(store->Read(5, 0, buffer, 1) == kSError_BadPSSID);
	EXPECT(store->Write(0, 0, buffer, 1) == kSError_WriteProtected);
	EXPECT(store->NewObject(4, &root) == kSError_WriteProtected);
	EXPECT(store->Format() == kSError_WriteProtected);
	long total = 0, used = 0;
	EXPECT(store->GetStoreSizes(&total, &used) == noErr && total == (long) (dataStart + 8) && used == total);
	EXPECT(store->LockStore() == noErr && store->IsLocked() && store->InTransaction());
	EXPECT(store->UnlockStore() == noErr && !store->IsLocked());
	EXPECT(store->LockStore() == noErr && store->Abort() == noErr && !store->IsLocked());
	PSSId next = 1;
	EXPECT(store->NextObject(0, &next) == noErr && next == 0);
	EXPECT(store->OwnsObject(7) && store->Address(0) == nil && !store->InSeparateTransaction(0));
	store->Delete();
}


static void
StoreScenario()
{
	RegisterStoreImplementations();
	TStore* store = TStore::New("THostStore");
	EXPECT(store != nil);
	if (store != nil)
	{
		EXPECT(store->Init(nil, 0x10000, 0, 0, kStoreIsInternal, nil) == noErr);
		TestObjects(store);
		TestTransactions(store);
		store->Delete();
	}
	// a write-protected card
	store = TStore::New("THostStore");
	if (store != nil)
	{
		EXPECT(store->Init(nil, 0x1000, 0, 1, kStoreIsCard, nil) == noErr);
		Boolean readOnly = false;
		EXPECT(store->IsReadOnly(&readOnly) == noErr && readOnly);
		EXPECT(store->Format() == kSError_WriteProtected);
		store->Delete();
	}
	TestPackageStore();
	HostStopTasks();
}


int main()
{
	gHostKernelServicesTask = StoreScenario;
	OsBoot();
	if (failures == 0)
		printf("test_Store: all passed\n");
	else
		printf("test_Store: %d failures\n", failures);
	return failures != 0;
}
