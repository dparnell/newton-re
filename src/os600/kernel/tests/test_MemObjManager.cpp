// Host unit test for the memory object database (src/os600/kernel/MemObjManager.*).
// The tables are the ROM's (MemObjTables.cpp); on the host every call goes
// through GenericSWI with the request in the current task's globals, so a
// task with a globals block is set up first.

#include "MemObjManager.h"
#include "Task.h"
#include "KernelGlobals.h"
#include "ObjectTable.h"
#include "OSErrors.h"
#include "os600/TaskGlobals.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static TObjectTable table;

int main()
{
	table.Init();
	gObjectTable = &table;
	TTask* task = new TTask;
	table.Add(task, kTaskType, 1);
	TaskGlobals globals;
	gCurrentTask = task;
	gCurrentGlobals = &globals + 1;

	// --- the table for the RAM fitted, and the database built from it ---------
	SelectDomainTable(kOneMegabyte);
	EXPECT(gDomainTable == g1MegDomainTable);
	SelectDomainTable(4 * kOneMegabyte);
	EXPECT(gDomainTable == g4MegDomainTable);
	ULong size = 0;
	ComputeMemObjDatabaseSize(&size);
	// 9 domains, 7 environments, 2 heaps ('kstk', 'user'), 2 persistent ('prot', 'rams') + 10 spares
	EXPECT(size == sizeof(MemObjDatabase) + 18 * sizeof(MemObjEntry) + 12 * sizeof(PersistentDBEntry));
	gMemObjHeap = malloc(size);
	BuildMemObjDatabase();
	EXPECT(gMemObjDatabase->fTables[kMemObjDomain].fCount == 9 && gMemObjDatabase->fTables[kMemObjEnvironment].fCount == 7);
	EXPECT(gMemObjDatabase->fTables[kMemObjHeap].fCount == 2 && gMemObjDatabase->fTables[kMemObjPersistent].fCount == 12);

	// --- domains -----------------------------------------------------------------
	DomainInfo info;
	long err = 1;
	EXPECT(MemObjManager::GetDomainInfo(0, &info, &err) && info.Name() == 'krnl' && info.Base() == 0x0C100000 && info.Size() == 0x100000);
	EXPECT(info.HasGlobals() == false && !info.HasHeap());
	EXPECT(MemObjManager::GetDomainInfo(2, &info, &err) && info.Name() == 'user');
	EXPECT(info.HasHeap() && info.HeapSize() == 0x380000 && info.HandleHeapSize() == 0x200000 && info.IsSegregated());
	EXPECT(info.IsCacheable() && !info.IsPersistent() && !info.IsReadOnly() && !info.IsHunkOMemory());
	EXPECT(MemObjManager::GetDomainInfoByName('rams', &info) == noErr && info.IsPersistent() && info.IsHunkOMemory() && info.IsReadOnly());
	EXPECT(MemObjManager::GetDomainInfoByName('none', &info) == kError_Item_Not_Found);
	EXPECT(!MemObjManager::GetDomainInfo(9, &info, &err) && err == noErr);			// past the end: no error

	// --- environments and their domains -----------------------------------------------
	EnvironmentInfo env;
	EXPECT(MemObjManager::GetEnvironmentInfo(1, &env, &err) && env.Name() == 'ksrv' && env.DefaultHeap() == 'kstk');
	EXPECT(env.DefaultHeapDomain() == 'kstk' && env.DefaultStackDomain() == 'kstk');
	ULong name = 0;
	Boolean isManager = true;
	EXPECT(env.Domains(0, &name, &isManager, &err) && name == 'user' && !isManager);
	EXPECT(env.Domains(2, &name, &isManager, &err) && name == 'kstk' && !isManager);
	EXPECT(!env.Domains(3, &name, &isManager, &err) && err == noErr);
	EXPECT(MemObjManager::GetEnvironmentInfo(4, &env, &err) && env.Name() == 'prot');
	EXPECT(env.Domains(6, &name, &isManager, &err) && name == 'prot' && isManager);		// after 6 clients, the one it manages
	EXPECT(!MemObjManager::GetEnvDomainName('none', 0, &name, &isManager, &err) && err == kError_Item_Not_Found);
	EXPECT(!MemObjManager::GetEnvironmentInfo(7, &env, &err) && err == noErr);

	// --- ids and heaps are registered as the kernel makes them --------------------------
	TObjectId id = 0xBAD;
	EXPECT(MemObjManager::FindEnvironmentId('krnl', &id) == noErr && id == 0);
	MemObjManager::RegisterEnvironmentId('krnl', 0x1230 | kEnvironmentType);
	EXPECT(MemObjManager::FindEnvironmentId('krnl', &id) == noErr && id == (0x1230 | kEnvironmentType));
	EXPECT(MemObjManager::FindEnvironmentId('none', &id) == kError_Item_Not_Found);
	MemObjManager::RegisterDomainId('user', 0x1240 | kDomainType);
	EXPECT(MemObjManager::FindDomainId('user', &id) == noErr && id == (0x1240 | kDomainType));
	EXPECT(MemObjManager::FindDomainId('krnl', &id) == noErr && id == 0);
	int heap;
	void* ref = nil;
	MemObjManager::RegisterHeapRef('user', &heap);
	EXPECT(MemObjManager::FindHeapRef('user', &ref) == noErr && ref == &heap);
	EXPECT(MemObjManager::FindHeapRef('krnl', &ref) == kError_Item_Not_Found);		// no heap
	MemObjManager::RegisterHeapRef('krnl', &heap);										// ...so nothing to register
	EXPECT(MemObjManager::FindHeapRef('krnl', &ref) == kError_Item_Not_Found);

	// --- persistent records -----------------------------------------------------------------
	PersistentDBEntry* rams = nil;
	EXPECT(MemObjManager::GetPersistentRef(0, &rams, &err) && rams->fName == 'prot');
	EXPECT(MemObjManager::GetPersistentRef(1, &rams, &err) && rams->fName == 'rams');
	EXPECT((rams->fFlags & kPersistent_InUse) && (rams->fFlags >> kPersistent_IndexShift) == 4);	// 'rams' is domain 4
	EXPECT(rams->fDomainId == 0 && rams->fHeap == nil);
	EXPECT(MemObjManager::FindHeapRef('rams', &ref) == noErr && ref == nil);				// found in the persistent table
	PersistentDBEntry* spare = nil;
	EXPECT(MemObjManager::GetPersistentRef(2, &spare, &err) && spare->fName == kEmptyEntryName && (spare->fFlags & kPersistent_InUse) == 0);
	EXPECT(!MemObjManager::GetPersistentRef(12, &spare, &err) && err == noErr);
	PersistentDBEntry store;
	memset(&store, 0, sizeof(store));
	store.Init('stor', true, 4);
	store.fHeap = &heap;
	EXPECT(MemObjManager::RegisterPersistentNewEntry('stor', &store) == noErr);
	EXPECT(MemObjManager::RegisterPersistentNewEntry('stor', &store) == kError_Duplicate_Object);
	EXPECT(MemObjManager::FindHeapRef('stor', &ref) == noErr && ref == &heap);
	EXPECT(MemObjManager::DeregisterPersistentEntry('stor') == noErr);
	EXPECT(MemObjManager::FindHeapRef('stor', &ref) == kError_Item_Not_Found);
	EXPECT(MemObjManager::DeregisterPersistentEntry('stor') == kError_Item_Not_Found);
	EXPECT(MemObjManager::DeregisterPersistentEntry(kEmptyEntryName) == kError_Item_Not_Found);	// a free slot is not in use

	if (failures == 0)
		printf("test_MemObjManager: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
