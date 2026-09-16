/*
	File:		MemObjManager.cpp

	Contains:	MemObjManager, the database it works on, and the info classes.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "MemObjManager.h"
#include "KernelGlobals.h"
#include "Task.h"
#include "OSErrors.h"
#include "os600/TaskGlobals.h"
#include "os600/GenericSWISelectors.h"
#include "UserGlobals.h"
#include "hal/System.h"

#include <string.h>


const DomainTableEntry*	gDomainTable = nil;
MemObjDatabase*			gMemObjDatabase = nil;
void*					gMemObjHeap = nil;

// what the persistent entries' queues hold: items with their TSingleQItem 0x14 in
static const ULong kPersistentQueueItemOffset = 0x14;	// FUN_00381424 (a patch stub returning 0x14)


// ROM 0x00045c84 InitCGlobals +0x374 (the part that picks the domain table)
void
SelectDomainTable(ULong ramSize)
{
	gDomainTable = ramSize <= kOneMegabyte ? g1MegDomainTable : g4MegDomainTable;
}


/* -------------------------------------------------------------------------------
	Info classes
------------------------------------------------------------------------------- */

// ROM 0x0011f594 InitDomainInfo__10DomainInfoFUlN31
void
DomainInfo::InitDomainInfo(ULong name, ULong unknown, ULong base, ULong size)
{
	fName = name;
	fUnknown04 = unknown;
	fBase = base;
	fSize = size;
}


// ROM 0x0011f5a0 InitHeapInfo__10DomainInfoFUlN21
void
DomainInfo::InitHeapInfo(ULong heapSize, ULong handleHeapSize, ULong heapFlags)
{
	fHeapSize = heapSize;
	fHandleHeapSize = handleHeapSize;
	fHeapFlags = heapFlags;
}


// ROM 0x0011f5ac InitGlobalInfo__10DomainInfoFUlN31
void
DomainInfo::InitGlobalInfo(ULong globalBase, ULong globalROMBase, ULong initSize, ULong zeroSize)
{
	fGlobalROMBase = globalROMBase;
	fGlobalBase = globalBase;
	fGlobalInitSize = initSize;
	fGlobalZeroSize = zeroSize;
}


// ROM 0x0011f840 Init__15EnvironmentInfoFUlN41
void
EnvironmentInfo::Init(ULong name, ULong unknown, ULong defaultHeap, ULong heapDomain, ULong stackDomain)
{
	fName = name;
	fUnknown04 = unknown;
	fDefaultHeap = defaultHeap;
	fDefaultHeapDomain = heapDomain;
	fDefaultStackDomain = stackDomain;
}


// ROM 0x0011f858 Domains__15EnvironmentInfoFUlPUlPUcPl
Boolean
EnvironmentInfo::Domains(ULong index, ULong* outName, Boolean* outIsManager, long* outError)
{
	return MemObjManager::GetEnvDomainName(fName, index, outName, outIsManager, outError);
}


// ROM 0x0011f8dc Init__17PersistentDBEntryFUlUcT1
// reset: start the record afresh under `name` for the domain at domainIndex
// (its id from the domain table, -1 if it has none yet); otherwise only the
// low half of the flags is kept.
void
PersistentDBEntry::Init(ULong name, Boolean reset, ULong domainIndex)
{
	if (reset)
	{
		fHeap = nil;
		fName = name;
		fStart = 0;
		fSize = 0;
		fUnknown10 = 0;
		fQueue.Init(kPersistentQueueItemOffset);
		fFlags = (fFlags & 0xFFFF00FF) | kPersistent_Unknown80 | ((domainIndex & 0xFF) << kPersistent_IndexShift) | kPersistent_InUse;
		MemObjEntry domain;
		long err;
		if (!MemObjManager::FindEntryByIndex(kMemObjDomain, domainIndex, &domain, &err))
			domain.fValue = (uintptr_t) -1;
		fDomainId = (TObjectId) domain.fValue;
	}
	fFlags &= 0xFFFF;
}


/* -------------------------------------------------------------------------------
	Building the database
------------------------------------------------------------------------------- */

static inline Boolean
DomainHasHeap(const DomainTableEntry& d)
{
	return d.fHeapSize != 0 || d.fHandleHeapSize != 0;
}


// ROM 0x0011e784 ComputeMemObjDatabaseSize__FPUl
// Room for the four tables: an entry per domain, per environment and per heap,
// a persistent record per persistent heap plus ten spare ones, and the 0x30
// bytes of table headers.  (0x168 is the ten spares' 0x24 bytes each.)
void
ComputeMemObjDatabaseSize(ULong* outSize)
{
	ULong size = 0;
	for (const DomainTableEntry* d = gDomainTable; d->fName != 0; d++)
		size += sizeof(MemObjEntry);
	for (const DomainTableEntry* d = gDomainTable; d->fName != 0; d++)
		if (DomainHasHeap(*d))
			size += (d->fHeapFlags & kHeapFlag_Persistent) ? sizeof(PersistentDBEntry) : sizeof(MemObjEntry);
	size += 10 * sizeof(PersistentDBEntry);
	for (const EnvTableEntry* e = gEnvTable; e->fName != 0; e++)
		size += sizeof(MemObjEntry);
	*outSize = size + sizeof(MemObjDatabase);
}


// ROM 0x0011e84c BuildMemObjDatabase__Fv
// Lays the tables out in the memory object heap: domains, then heaps (the
// domains with a heap that is not persistent), then persistent heaps (with
// ten free 'emty' records), then environments.  Ids and heap addresses start
// at 0; the kernel registers them as it makes the objects.
void
BuildMemObjDatabase()
{
	MemObjDatabase* db = (MemObjDatabase*) gMemObjHeap;
	gMemObjDatabase = db;
	char* next = (char*) (db + 1);

	MemObjEntry* entry = (MemObjEntry*) next;
	db->fTables[kMemObjDomain].fEntries = entry;
	ULong count = 0;
	for (const DomainTableEntry* d = gDomainTable; d->fName != 0; d++, entry++, count++)
	{
		entry->fValue = 0;
		entry->fName = d->fName;
	}
	db->fTables[kMemObjDomain].fCount = count;
	db->fTables[kMemObjDomain].fEntrySize = sizeof(MemObjEntry);

	db->fTables[kMemObjHeap].fEntries = entry;
	count = 0;
	for (const DomainTableEntry* d = gDomainTable; d->fName != 0; d++)
	{
		if (DomainHasHeap(*d) && (d->fHeapFlags & kHeapFlag_Persistent) == 0)
		{
			entry->fName = d->fName;
			entry->fValue = 0;
			entry++;
			count++;
		}
	}
	db->fTables[kMemObjHeap].fCount = count;
	db->fTables[kMemObjHeap].fEntrySize = sizeof(MemObjEntry);

	PersistentDBEntry* record = (PersistentDBEntry*) entry;
	db->fTables[kMemObjPersistent].fEntries = record;
	count = 0;
	ULong index = 0;
	for (const DomainTableEntry* d = gDomainTable; d->fName != 0; d++, index++)
	{
		if (DomainHasHeap(*d) && (d->fHeapFlags & kHeapFlag_Persistent))
		{
			record->Init(d->fName, true, index);
			record++;
			count++;
		}
	}
	db->fTables[kMemObjPersistent].fCount = count;
	db->fTables[kMemObjPersistent].fEntrySize = sizeof(PersistentDBEntry);
	for (ULong i = 0; i < 10; i++, record++)
	{
		record->Init(kEmptyEntryName, true, (ULong) -1);
		record->fFlags &= ~kPersistent_InUse;
	}
	db->fTables[kMemObjPersistent].fCount += 10;

	entry = (MemObjEntry*) record;
	db->fTables[kMemObjEnvironment].fEntries = entry;
	count = 0;
	for (const EnvTableEntry* e = gEnvTable; e->fName != 0; e++, entry++, count++)
	{
		entry->fName = e->fName;
		entry->fValue = 0;
	}
	db->fTables[kMemObjEnvironment].fCount = count;
	db->fTables[kMemObjEnvironment].fEntrySize = sizeof(MemObjEntry);
}


/* -------------------------------------------------------------------------------
	Kernel-mode primitives
------------------------------------------------------------------------------- */

// ROM 0x0011eac8 PrimGetDomainInfo__13MemObjManagerSFUlP10DomainInfo
// The index'th domain of the domain table, with its globals from the
// data-area table if it has an entry there.
NewtonErr
MemObjManager::PrimGetDomainInfo(ULong index, DomainInfo* outInfo)
{
	const DomainTableEntry* d = gDomainTable;
	for (ULong i = 0; d->fName != 0 && i != index; i++)
		d++;
	if (d->fName == 0)
		return kError_Item_Not_Found;
	const DataAreaEntry* area = DataAreaTable;
	while (area->fName != 0 && area->fName != d->fName)
		area++;
	outInfo->InitDomainInfo(d->fName, 0, d->fBase, d->fSize);
	outInfo->InitHeapInfo(d->fHeapSize, d->fHandleHeapSize, d->fHeapFlags);
	if (area->fName == 0)
		outInfo->InitGlobalInfo(0, 0, 0, 0);
	else
		outInfo->InitGlobalInfo(area->fGlobalBase, area->fGlobalROMBase, area->fGlobalInitSize, area->fGlobalZeroSize);
	return noErr;
}


// ROM 0x0011ebb8 PrimGetDomainInfoByName__13MemObjManagerSFUlP10DomainInfo
NewtonErr
MemObjManager::PrimGetDomainInfoByName(ULong name, DomainInfo* outInfo)
{
	DomainInfo info;
	for (ULong i = 0; PrimGetDomainInfo(i, &info) == noErr; i++)
	{
		if (info.Name() == name)
		{
			*outInfo = info;
			return noErr;
		}
	}
	return kError_Item_Not_Found;
}


// ROM 0x0011ec38 PrimGetEnvironmentInfo__13MemObjManagerSFUlP15EnvironmentInfo
NewtonErr
MemObjManager::PrimGetEnvironmentInfo(ULong index, EnvironmentInfo* outInfo)
{
	const EnvTableEntry* e = gEnvTable;
	for (ULong i = 0; e->fName != 0 && i != index; i++)
		e++;
	if (e->fName == 0)
		return kError_Item_Not_Found;
	outInfo->Init(e->fName, 0, e->fDefaultHeap, e->fHeapDomain, e->fStackDomain);
	return noErr;
}


// ROM 0x0011ecbc PrimGetEnvDomainName__13MemObjManagerSFUlT1PUlPUcT4
// The index'th domain of the environment: its client domains first, then
// the ones it manages.  outFound false past the end.
NewtonErr
MemObjManager::PrimGetEnvDomainName(ULong envName, ULong index, ULong* outName, Boolean* outIsManager, Boolean* outFound)
{
	const EnvTableEntry* e = gEnvTable;
	while (e->fName != 0 && e->fName != envName)
		e++;
	if (e->fName == 0)
	{
		*outFound = false;
		return kError_Item_Not_Found;
	}
	ULong i = 0;
	if (e->fClientDomains != nil)
	{
		for (const ULong* name = e->fClientDomains; *name != 0; name++, i++)
		{
			if (i == index)
			{
				*outName = *name;
				*outIsManager = false;
				*outFound = true;
				return noErr;
			}
		}
	}
	if (e->fManagerDomains != nil)
	{
		for (const ULong* name = e->fManagerDomains; *name != 0; name++, i++)
		{
			if (i == index)
			{
				*outName = *name;
				*outIsManager = true;
				*outFound = true;
				return noErr;
			}
		}
	}
	*outFound = false;
	return noErr;
}


// ROM 0x0011f40c EntryLocByIndex__13MemObjManagerSF10MemObjTypeUl
void*
MemObjManager::EntryLocByIndex(MemObjType type, ULong index)
{
	if (type > kMemObjPersistent)
		return nil;
	MemObjTable& table = gMemObjDatabase->fTables[type];
	if (index >= table.fCount)
		return nil;
	return (char*) table.fEntries + index * table.fEntrySize;
}


// ROM 0x0011f5fc EntryLocByName__13MemObjManagerSF10MemObjTypeUl
void*
MemObjManager::EntryLocByName(MemObjType type, ULong name)
{
	if (type > kMemObjPersistent)
		return nil;
	MemObjTable& table = gMemObjDatabase->fTables[type];
	char* entry = (char*) table.fEntries;
	for (ULong i = 0; i < table.fCount; i++, entry += table.fEntrySize)
		if (*(ULong*) entry == name)
			return entry;
	return nil;
}


// ROM 0x0011ef78 CopyObject__13MemObjManagerSF10MemObjTypePvT2
void
MemObjManager::CopyObject(MemObjType type, void* to, void* from)
{
	if (type == kMemObjDomain || type == kMemObjEnvironment || type == kMemObjHeap)
		memcpy(to, from, sizeof(MemObjEntry));
	else if (type == kMemObjPersistent)
		memcpy(to, from, sizeof(PersistentDBEntry));
}


// ROM 0x0011f780 PrimGetEntryByIndex__13MemObjManagerSF10MemObjTypeUlPv
NewtonErr
MemObjManager::PrimGetEntryByIndex(MemObjType type, ULong index, void* outEntry)
{
	void* entry = EntryLocByIndex(type, index);
	if (entry == nil)
		return kError_Item_Not_Found;
	CopyObject(type, outEntry, entry);
	return noErr;
}


// ROM 0x0011f8a0 PrimSetEntryByIndex__13MemObjManagerSF10MemObjTypeUlPv
NewtonErr
MemObjManager::PrimSetEntryByIndex(MemObjType type, ULong index, void* newEntry)
{
	void* entry = EntryLocByIndex(type, index);
	if (entry == nil)
		return kError_Item_Not_Found;
	CopyObject(type, entry, newEntry);
	return noErr;
}


// ROM 0x0011fb98 PrimGetEntryByName__13MemObjManagerSF10MemObjTypeUlPv
NewtonErr
MemObjManager::PrimGetEntryByName(MemObjType type, ULong name, void* outEntry)
{
	void* entry = EntryLocByName(type, name);
	if (entry == nil)
		return kError_Item_Not_Found;
	CopyObject(type, outEntry, entry);
	return noErr;
}


// ROM 0x0011fbd8 PrimSetEntryByName__13MemObjManagerSF10MemObjTypeUlPv
NewtonErr
MemObjManager::PrimSetEntryByName(MemObjType type, ULong name, void* newEntry)
{
	void* entry = EntryLocByName(type, name);
	if (entry == nil)
		return kError_Item_Not_Found;
	CopyObject(type, entry, newEntry);
	return noErr;
}


// The request block of the calling task (the start of its globals).
static inline MemObjRequest*
Request()
{
	return (MemObjRequest*) ((char*) gCurrentGlobals - kTaskGlobalsSize);
}


// ROM 0x0011f4bc PrimGetMemObjInfo__Fv
// GenericSWI kGeneric_GetMemObjInfo: the user-mode side of every call above,
// with the request in the caller's task globals.
long
PrimGetMemObjInfo()
{
	MemObjRequest* req = Request();
	switch (req->fSelector)
	{
	case kMemObjReq_DomainInfo:
		return MemObjManager::PrimGetDomainInfo(req->fArg2, &req->fDomainInfo);
	case kMemObjReq_DomainInfoByName:
		return MemObjManager::PrimGetDomainInfoByName(req->fArg1, &req->fDomainInfo);
	case kMemObjReq_EnvironmentInfo:
		return MemObjManager::PrimGetEnvironmentInfo(req->fArg2, &req->fEnvironmentInfo);
	case kMemObjReq_EnvDomainName:
		return MemObjManager::PrimGetEnvDomainName(req->fArg1, req->fArg2, &req->fArg1, &req->fDomainName.fIsManager, &req->fDomainName.fFound);
	case kMemObjReq_GetEntryByIndex:
		return MemObjManager::PrimGetEntryByIndex((MemObjType) req->fArg1, req->fArg2, &req->fEntry);
	case kMemObjReq_GetEntryByName:
		return MemObjManager::PrimGetEntryByName((MemObjType) req->fArg1, req->fArg2, &req->fEntry);
	case kMemObjReq_SetEntryByIndex:
		return MemObjManager::PrimSetEntryByIndex((MemObjType) req->fArg1, req->fArg2, &req->fEntry);
	case kMemObjReq_SetEntryByName:
		return MemObjManager::PrimSetEntryByName((MemObjType) req->fArg1, req->fArg2, &req->fEntry);
	default:
		return kError_Bad_Parameters;
	}
}


/* -------------------------------------------------------------------------------
	The calls either side makes
------------------------------------------------------------------------------- */

// ROM 0x0011f060 GetDomainInfo__13MemObjManagerSFUlP10DomainInfoPl
// True with the info; false at the end of the table (outError 0) or on an
// error (outError set).
Boolean
MemObjManager::GetDomainInfo(ULong index, DomainInfo* outInfo, long* outError)
{
	long err;
	if (!IsSuperMode())
	{
		MemObjRequest* req = Request();
		req->fArg2 = index;
		req->fSelector = kMemObjReq_DomainInfo;
		err = GenericSWI(kGeneric_GetMemObjInfo);
		if (err == noErr)
			memcpy(outInfo, &req->fDomainInfo, sizeof(DomainInfo));
	}
	else
		err = PrimGetDomainInfo(index, outInfo);
	if (err == kError_Item_Not_Found)
		err = noErr;
	else if (err == noErr)
		return true;
	*outError = err;
	return false;
}


// ROM 0x0011f104 GetDomainInfoByName__13MemObjManagerSFUlP10DomainInfo
NewtonErr
MemObjManager::GetDomainInfoByName(ULong name, DomainInfo* outInfo)
{
	if (IsSuperMode())
		return PrimGetDomainInfoByName(name, outInfo);
	MemObjRequest* req = Request();
	req->fSelector = kMemObjReq_DomainInfoByName;
	req->fArg1 = name;
	long err = GenericSWI(kGeneric_GetMemObjInfo);
	if (err == noErr)
		memcpy(outInfo, &req->fDomainInfo, sizeof(DomainInfo));
	return err;
}


// ROM 0x0011f17c GetEnvironmentInfo__13MemObjManagerSFUlP15EnvironmentInfoPl
Boolean
MemObjManager::GetEnvironmentInfo(ULong index, EnvironmentInfo* outInfo, long* outError)
{
	long err;
	if (!IsSuperMode())
	{
		MemObjRequest* req = Request();
		req->fSelector = kMemObjReq_EnvironmentInfo;
		req->fArg2 = index;
		err = GenericSWI(kGeneric_GetMemObjInfo);
		if (err == noErr)
			memcpy(outInfo, &req->fEnvironmentInfo, sizeof(EnvironmentInfo));
	}
	else
		err = PrimGetEnvironmentInfo(index, outInfo);
	if (err == kError_Item_Not_Found)
		err = noErr;
	else if (err == noErr)
		return true;
	*outError = err;
	return false;
}


// ROM 0x0011f220 GetEnvDomainName__13MemObjManagerSFUlT1PUlPUcPl
// True with the index'th domain's name and whether the environment manages
// it; false past the end, or on error with outError set.
Boolean
MemObjManager::GetEnvDomainName(ULong envName, ULong index, ULong* outName, Boolean* outIsManager, long* outError)
{
	Boolean found;
	long err;
	if (IsSuperMode())
	{
		err = PrimGetEnvDomainName(envName, index, outName, outIsManager, &found);
		if (!found)
			*outError = err;
		return found;
	}
	MemObjRequest* req = Request();
	req->fSelector = kMemObjReq_EnvDomainName;
	req->fArg1 = envName;
	req->fArg2 = index;
	err = GenericSWI(kGeneric_GetMemObjInfo);
	found = req->fDomainName.fFound;
	if (err == noErr && found)
	{
		*outName = req->fArg1;
		*outIsManager = req->fDomainName.fIsManager;
	}
	if (!found)
		*outError = err;
	return found;
}


// ROM 0x0011f2e0 FindEntryByIndex__13MemObjManagerSF10MemObjTypeUlPvPl
Boolean
MemObjManager::FindEntryByIndex(MemObjType type, ULong index, void* outEntry, long* outError)
{
	long err;
	if (!IsSuperMode())
	{
		MemObjRequest* req = Request();
		req->fSelector = kMemObjReq_GetEntryByIndex;
		req->fArg1 = type;
		req->fArg2 = index;
		err = GenericSWI(kGeneric_GetMemObjInfo);
		if (err == noErr)
		{
			CopyObject(type, outEntry, &req->fEntry);
			return true;
		}
		if (err == kError_Item_Not_Found)
		{
			*outError = noErr;
			return false;
		}
	}
	else
	{
		err = PrimGetEntryByIndex(type, index, outEntry);
		if (err == noErr)
			return true;
	}
	*outError = err;
	return false;
}


// ROM 0x0011f390 FindEntryByName__13MemObjManagerSF10MemObjTypeUlPv
NewtonErr
MemObjManager::FindEntryByName(MemObjType type, ULong name, void* outEntry)
{
	if (IsSuperMode())
		return PrimGetEntryByName(type, name, outEntry);
	MemObjRequest* req = Request();
	req->fSelector = kMemObjReq_GetEntryByName;
	req->fArg1 = type;
	req->fArg2 = name;
	long err = GenericSWI(kGeneric_GetMemObjInfo);
	if (err == noErr)
		CopyObject(type, outEntry, &req->fEntry);
	return err;
}


// ROM 0x0011f450 RegisterEntryByName__13MemObjManagerSF10MemObjTypeUlPv
NewtonErr
MemObjManager::RegisterEntryByName(MemObjType type, ULong name, void* entry)
{
	if (!IsSuperMode())
	{
		MemObjRequest* req = Request();
		req->fArg2 = name;
		req->fArg1 = type;
		req->fSelector = kMemObjReq_SetEntryByName;
		CopyObject(type, &req->fEntry, entry);
		return GenericSWI(kGeneric_GetMemObjInfo);
	}
	void* loc = EntryLocByName(type, name);
	if (loc == nil)
		return kError_Item_Not_Found;
	CopyObject(type, loc, entry);
	return noErr;
}


// ROM 0x0011eeb8 FindEnvironmentId__13MemObjManagerSFUlPUl
// (The ROM's is void; callers read FindEntryByName's result left in r0.)
NewtonErr
MemObjManager::FindEnvironmentId(ULong name, TObjectId* outId)
{
	MemObjEntry entry;
	NewtonErr err = FindEntryByName(kMemObjEnvironment, name, &entry);
	if (err == noErr)
		*outId = (TObjectId) entry.fValue;
	return err;
}


// ROM 0x0011eee4 FindDomainId__13MemObjManagerSFUlPUl
NewtonErr
MemObjManager::FindDomainId(ULong name, TObjectId* outId)
{
	MemObjEntry entry;
	NewtonErr err = FindEntryByName(kMemObjDomain, name, &entry);
	if (err == noErr)
		*outId = (TObjectId) entry.fValue;
	return err;
}


// ROM 0x0011ef10 FindHeapRef__13MemObjManagerSFUlPPv
// A heap by name: from the heap table, else from the persistent records.
NewtonErr
MemObjManager::FindHeapRef(ULong name, void** outHeap)
{
	MemObjEntry entry;
	if (FindEntryByName(kMemObjHeap, name, &entry) == noErr)
	{
		*outHeap = (void*) entry.fValue;
		return noErr;
	}
	PersistentDBEntry record;
	NewtonErr err = FindEntryByName(kMemObjPersistent, name, &record);
	if (err == noErr)
		*outHeap = record.fHeap;
	return err;
}


// ROM 0x0011efac RegisterEnvironmentId__13MemObjManagerSFUlT1
void
MemObjManager::RegisterEnvironmentId(ULong name, TObjectId id)
{
	MemObjEntry entry;
	if (FindEntryByName(kMemObjEnvironment, name, &entry) == noErr)
	{
		entry.fValue = id;
		RegisterEntryByName(kMemObjEnvironment, name, &entry);
	}
}


// ROM 0x0011efe8 RegisterDomainId__13MemObjManagerSFUlT1
void
MemObjManager::RegisterDomainId(ULong name, TObjectId id)
{
	MemObjEntry entry;
	if (FindEntryByName(kMemObjDomain, name, &entry) == noErr)
	{
		entry.fValue = id;
		RegisterEntryByName(kMemObjDomain, name, &entry);
	}
}


// ROM 0x0011f024 RegisterHeapRef__13MemObjManagerSFUlPv
void
MemObjManager::RegisterHeapRef(ULong name, void* heap)
{
	MemObjEntry entry;
	if (FindEntryByName(kMemObjHeap, name, &entry) == noErr)
	{
		entry.fValue = (uintptr_t) heap;
		RegisterEntryByName(kMemObjHeap, name, &entry);
	}
}


// ROM 0x0011ea90 GetPersistentRef__13MemObjManagerSFUlPP17PersistentDBEntryPl
// The index'th persistent record itself (in the database, not a copy).
Boolean
MemObjManager::GetPersistentRef(ULong index, PersistentDBEntry** outEntry, long* outError)
{
	PersistentDBEntry* entry = (PersistentDBEntry*) EntryLocByIndex(kMemObjPersistent, index);
	if (entry == nil)
		*outError = noErr;
	else
		*outEntry = entry;
	return entry != nil;
}


// ROM 0x0011edb8 RegisterPersistentNewEntry__13MemObjManagerSFUlP17PersistentDBEntry
// A new persistent heap takes the first free ('emty') record; a name already
// in use is refused.
NewtonErr
MemObjManager::RegisterPersistentNewEntry(ULong name, PersistentDBEntry* entry)
{
	PersistentDBEntry existing;
	if (FindEntryByName(kMemObjPersistent, name, &existing) == noErr)
		return kError_Duplicate_Object;
	void* slot = EntryLocByName(kMemObjPersistent, kEmptyEntryName);
	if (slot == nil)
		return kError_Could_Not_Create_Object;
	CopyObject(kMemObjPersistent, slot, entry);
	return noErr;
}


// ROM 0x0011ee30 DeregisterPersistentEntry__13MemObjManagerSFUl
// Frees the record: it becomes an 'emty' slot.  A record not in use is not found.
NewtonErr
MemObjManager::DeregisterPersistentEntry(ULong name)
{
	PersistentDBEntry empty;
	empty.Init(kEmptyEntryName, true, (ULong) -1);
	empty.fFlags &= ~kPersistent_InUse;
	PersistentDBEntry* entry = (PersistentDBEntry*) EntryLocByName(kMemObjPersistent, name);
	if (entry == nil || (entry->fFlags & kPersistent_InUse) == 0)
		return kError_Item_Not_Found;
	CopyObject(kMemObjPersistent, entry, &empty);
	return noErr;
}
