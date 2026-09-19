/*
	File:		stores/Soups.cpp

	Contains:	The store and soup frames (Soups.h): the store frame and its
				methods, the plain soup and its methods, index descriptions
				and keys, adding entries and keeping the indexes.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Soups.h"
#include "hal/System.h"
#include "Cursors.h"
#include "Tags.h"
#include "StoreObject.h"
#include "Frames.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "RichString.h"
#include "REPTranslators.h"
#include "Unicode.h"
#include "NSErrors.h"
#include "NewtonTime.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"
#include "PackageStore.h"
#include "protocols/Protocols.h"
#include <string.h>
#include <new>

extern const ExceptionName exStoreError;
extern const ExceptionName exBadType;
extern const ExceptionName exFramesWithFrameData;	// "evt.ex.fr;type.ref.frame"

const long kMaxSoupNameLength = 39;					// UniChars (0x27)

// gStores, gUnionSoups and gPackageStores are the frames layer's (ObjectHeap.cpp)


/*------------------------------------------------------------------------------
	I n i t i a l i s a t i o n
------------------------------------------------------------------------------*/

// ROM 0x0033fc6c InitQueries__Fv
// The store, union soup and package store lists; the package store's
// class registered (its part handler, which makes a store of a package's
// soup part: NOT YET RECONSTRUCTED).
void
InitQueries(void)
{
	// InitObjects (frames/Objects.cpp) has made gStores and gPackageStores
	// and registered the three as GC roots
	gStores = AllocateArray(RSSYMarray, 0);
	gUnionSoups = MakeEntryCache();
	gPackageStores = AllocateArray(RSSYMarray, 0);
	InitEntries();
	RegisterSoupNatives();
	RegisterCursorNatives();
	RegisterUnionSoupNatives();
	InstallHostNatives();						// host: into the function frame when there are no ROM objects
	InitSoupPrototypes();
	InitCursorPrototype();
	InitUnionSoupPrototype();
}


// Host: without the ROM's objects the prototype frames (Rstoreprototype,
// Rstorepersistent, Rplainsoupprototype, Rplainsouppersistent,
// Rindexdescprototype) are made here, their methods native function
// objects of the host implementations - the shape the ROM's have (see
// docs/stores/README.md).
struct PrototypeMethod
{
	const char*	fName;
	void*		fFunction;
	long		fNumArgs;
};

static const PrototypeMethod gStoreMethods[] = {
	{ "GetName", (void*) StoreGetName, 0 },
	{ "SetName", (void*) StoreSetName, 1 },
	{ "GetKind", (void*) StoreGetKind, 0 },
	{ "GetSignature", (void*) StoreGetSignature, 0 },
	{ "SetSignature", (void*) StoreSetSignature, 1 },
	{ "GetInfo", (void*) StoreGetInfo, 1 },
	{ "SetInfo", (void*) StoreSetInfo, 2 },
	{ "GetAllInfo", (void*) StoreGetAllInfo, 0 },
	{ "SetAllInfo", (void*) StoreSetAllInfo, 1 },
	{ "GetSoup", (void*) StoreGetSoup, 1 },
	{ "HasSoup", (void*) StoreHasSoup, 1 },
	{ "CreateSoup", (void*) StoreCreateSoup, 2 },
	{ "GetSoupNames", (void*) StoreGetSoupNames, 0 },
	{ "TotalSize", (void*) StoreTotalSize, 0 },
	{ "UsedSize", (void*) StoreUsedSize, 0 },
	{ "IsReadOnly", (void*) StoreIsReadOnly, 0 },
	{ "IsValid", (void*) StoreIsValid, 0 },
	{ "Lock", (void*) StoreLock, 0 },
	{ "Unlock", (void*) StoreUnlock, 0 },
	{ "Abort", (void*) StoreAbort, 0 },
	{ "Erase", (void*) StoreErase, 0 },
	{ "CheckWriteProtect", (void*) StoreCheckWriteProtect, 0 },
	{ "ReadObject", (void*) StoreReadObject, 1 },
	{ "WriteObject", (void*) StoreWriteObject, 3 },
	{ "WriteWholeObject", (void*) StoreWriteWholeObject, 2 },
	{ "NewObject", (void*) StoreNewObject, 1 },
	{ "DeleteObject", (void*) StoreDeleteObject, 1 },
	{ "SetObjectSize", (void*) StoreSetObjectSize, 2 },
	{ "GetObjectSize", (void*) StoreGetObjectSize, 1 },
	{ nil, nil, 0 }
};

static const PrototypeMethod gPlainSoupMethods[] = {
	{ "GetName", (void*) CommonSoupGetName, 0 },
	{ "SetName", (void*) PlainSoupSetName, 1 },
	{ "GetStore", (void*) PlainSoupGetStore, 0 },
	{ "Add", (void*) PlainSoupAdd, 1 },
	{ "AddFlushed", (void*) SoupAddFlushed, 1 },
	{ "AddWithUniqueID", (void*) PlainSoupAddWithUniqueID, 1 },
	{ "AddIndex", (void*) PlainSoupAddIndex, 1 },
	{ "RemoveIndex", (void*) PlainSoupRemoveIndex, 1 },
	{ "GetIndexes", (void*) PlainSoupGetIndexes, 0 },
	{ "IndexSizes", (void*) PlainSoupIndexSizes, 0 },
	{ "GetSignature", (void*) PlainSoupGetSignature, 0 },
	{ "SetSignature", (void*) PlainSoupSetSignature, 1 },
	{ "GetNextUID", (void*) PlainSoupGetNextUID, 0 },
	{ "GetInfo", (void*) PlainSoupGetInfo, 1 },
	{ "SetInfo", (void*) PlainSoupSetInfo, 2 },
	{ "GetAllInfo", (void*) PlainSoupGetAllInfo, 0 },
	{ "SetAllInfo", (void*) PlainSoupSetAllInfo, 1 },
	{ "GetFlags", (void*) SoupGetFlags, 0 },
	{ "SetFlags", (void*) SoupSetFlags, 1 },
	{ "RemoveAllEntries", (void*) PlainSoupRemoveAllEntries, 0 },
	{ "RemoveFromStore", (void*) PlainSoupRemoveFromStore, 0 },
	{ "GetSize", (void*) PlainSoupGetSize, 0 },
	{ "CopyEntries", (void*) PlainSoupCopyEntries, 1 },
	{ "CopyEntriesWithCallback", (void*) PlainSoupCopyEntriesWithCallBack, 3 },
	{ "MakeKey", (void*) PlainSoupMakeKey, 2 },
	{ "IsValid", (void*) SoupIsValid, 0 },
	{ "GetIndexesModTime", (void*) SoupGetIndexesModTime, 0 },
	{ "GetInfoModTime", (void*) SoupGetInfoModTime, 0 },
	{ "flush", (void*) PlainSoupFlush, 0 },
	{ "AddTags", (void*) PlainSoupAddTags, 1 },
	{ "RemoveTags", (void*) PlainSoupRemoveTags, 1 },
	{ "ModifyTag", (void*) PlainSoupModifyTag, 2 },
	{ "HasTags", (void*) PlainSoupHasTags, 0 },
	{ "GetTags", (void*) PlainSoupGetTags, 0 },
	{ "Query", (void*) CommonSoupQuery, 1 },
	{ "collect", (void*) SoupCollect, 1 },
	{ nil, nil, 0 }
};


static Ref
MakeMethodsFrame(const PrototypeMethod* methods)
{
	RefVar frame(AllocateFrame());
	RefVar fn;
	for (const PrototypeMethod* m = methods; m->fName != nil; m++)
	{
		fn = MakeCFunction(m->fFunction, m->fNumArgs, nil);
		SetFrameSlot(frame, RefVar(Intern((char*) m->fName)), fn);
	}
	return frame;
}


static void
SetNilSlots(RefArg frame, const Ref* tags, long count)
{
	for (long i = 0; i < count; i++)
		SetFrameSlot(frame, RefVar(tags[i]), RefVar(NILREF));
}


void
InitSoupPrototypes(void)
{
	if (Rstoreprototype != NILREF)
		return;										// the ROM's
	AddGCRoot(Rstoreprototype);
	AddGCRoot(Rstorepersistent);
	AddGCRoot(Rplainsoupprototype);
	AddGCRoot(Rplainsouppersistent);
	AddGCRoot(Rindexdescprototype);

	RefVar frame(AllocateFrame());
	Ref storePersistentTags[] = { RSSYMnameindex, RSSYMname, RSSYMsignature, RSSYMephemerals };
	SetNilSlots(frame, storePersistentTags, 4);
	Rstorepersistent = frame;

	frame = AllocateFrame();
	SetFrameSlot(frame, RSSYM_parent, RefVar(MakeMethodsFrame(gStoreMethods)));
	Ref storeTags[] = { RSSYM_proto, RSSYMstore, RSSYMsoups, RSSYMversion };
	SetNilSlots(frame, storeTags, 4);
	Rstoreprototype = frame;

	frame = AllocateFrame();
	SetFrameSlot(frame, RSSYMclass, RSSYMdisksoup);
	Ref soupPersistentTags[] = { RSSYMlastuid, RSSYMsignature, RSSYMindexes, RSSYMflags, RSSYMindexesmodtime, RSSYMinfomodtime };
	SetNilSlots(frame, soupPersistentTags, 6);
	Rplainsouppersistent = frame;

	frame = AllocateFrame();
	SetFrameSlot(frame, RSSYM_parent, RefVar(MakeMethodsFrame(gPlainSoupMethods)));
	SetFrameSlot(frame, RSSYM_proto, RefVar(NILREF));
	SetFrameSlot(frame, RSSYMclass, RefVar(Intern((char*) "PlainSoup")));
	Ref soupTags[] = { RSSYMtstore, RSSYMstoreobj, RSSYMthename, RSSYMcache, RSSYMindexobjects, RSSYMindexnextuid, RSSYMcursors };
	SetNilSlots(frame, soupTags, 7);
	Rplainsoupprototype = frame;

	frame = AllocateFrame();
	SetFrameSlot(frame, RSSYMstructure, RSSYMslot);
	SetFrameSlot(frame, RSSYMpath, RSSYM_uniqueid);
	SetFrameSlot(frame, RSSYMtype, RSSYMint);
	SetFrameSlot(frame, RSSYMindex, RefVar(NILREF));
	Rindexdescprototype = frame;
}


/*------------------------------------------------------------------------------
	S t o r e s
------------------------------------------------------------------------------*/

// ROM 0x003509d0 GetStores__Fv
Ref
GetStores(void)
{
	return gStores;
}


// ROM 0x0012329c GetStoreClassInfo__FPC6TStore
const TClassInfo*
GetStoreClassInfo(const TStore* store)
{
	return store->ClassInfo();
}


// ROM 0x00154908 GetInternalStore__Fv
// The store the machine boots with - the flash the PSS manager formats
// and mounts.
//
// DEVIATION: TPSSManager is NOT YET RECONSTRUCTED, so nothing here knows
// which store is the flash; a port says so with SetInternalStore before
// it registers the store (the host does it in HostMountStores).  Nil
// until one does, as it was before.
static TStore*	gInternalStore = nil;

TStore*
GetInternalStore(void)
{
	return gInternalStore;
}


void
SetInternalStore(TStore* store)
{
	gInternalStore = store;
}


// ROM 0x001559d4 IsValidStore__FPC6TStore
// A registered store (the ROM asks TPSSManager; here the store frames).
Boolean
IsValidStore(const TStore* store)
{
	if (gStores == NILREF)
		return false;
	long count = Length(gStores);
	for (long i = 0; i < count; i++)
	{
		Ref storeObject = GetArraySlotRef(gStores, i);
		TStoreWrapper* wrapper = (TStoreWrapper*) GetFrameSlotRef(storeObject, RSSYMstore);
		if (wrapper->Store() == store)
			return true;
	}
	return false;
}


// ROM 0x003509e0 CheckWriteProtect__FP6TStore
void
CheckWriteProtect(TStore* store)
{
	if (store->IsROM())
		Throw(exStoreError, (void*) kNSErrStoreIsROM, nil);
	Boolean readOnly;
	OSErrIf(store->IsReadOnly(&readOnly));
	if (readOnly)
		Throw(exStoreError, (void*) kSError_WriteProtected, nil);
}


// ROM 0x00351108 CheckWriteProtect__FRC6RefVar
void
CheckWriteProtect(RefArg storeObject)
{
	TStoreWrapper* wrapper = (TStoreWrapper*) GetFrameSlotRef(storeObject, RSSYMstore);
	CheckWriteProtect(wrapper->Store());
}


// ROM 0x00351e54 GetStoreWrapper__FRC6RefVar
// The store frame's wrapper; a killed frame (no _proto) is an error.
TStoreWrapper*
GetStoreWrapper(RefArg storeObject)
{
	if (GetFrameSlotRef(storeObject, RSSYM_proto) == NILREF)
		Throw(exStoreError, (void*) kNSErrInvalidStore, nil);
	return (TStoreWrapper*) GetFrameSlotRef(storeObject, RSSYMstore);
}


// ROM 0x00355528 StoreFromWrapper__FRC6RefVar
TStore*
StoreFromWrapper(RefArg storeObject)
{
	if (GetFrameSlotRef(storeObject, RSSYM_proto) == NILREF)
		Throw(exStoreError, (void*) kNSErrInvalidStore, nil);
	return ((TStoreWrapper*) GetFrameSlotRef(storeObject, RSSYMstore))->Store();
}


// ROM 0x00353a44 GetRandomSignature__Fv
// A store's or soup's signature: a random number (never 0; the ROM keeps
// the C library's random state for it).
long
GetRandomSignature(void)
{
	static ULong seed = 0x5eed1234;
	long value;
	do {
		seed = seed * 1103515245 + 12345;
		value = (long) ((seed >> 4) & 0x0fffffff);
	} while (value == 0);
	return value;
}


// ROM 0x0035262c GetStoreVersion__FP6TStorePl
// The version in a store's root data (an empty root: this ROM's).
NewtonErr
GetStoreVersion(TStore* store, long* version)
{
	StoreRootData rootData;
	long size;
	PSSId rootId;
	NewtonErr err = store->GetRootId(&rootId);
	if (err == noErr)
	{
		ReadStoreRootData(store, rootId, &rootData, &size);
		*version = size == 0 ? kStoreRootVersion : rootData.fVersion;
	}
	return err;
}


// ROM 0x00352a40 StoreGetDirSortTable__FRC6RefVar
// The sorting table the store's soup names are ordered by: the store's
// dirSortId, looked up among the registered tables.  NOT YET
// RECONSTRUCTED: a table the store itself carries (StoreSaveSortTable
// writes one into the store's root, and this reads it back when it is not
// one of the registered ones).
const TSortingTable*
StoreGetDirSortTable(RefArg storeObject)
{
	RefVar persistent(GetFrameSlotRef(storeObject, RSSYM_proto));
	RefVar sortId(GetFrameSlotRef(persistent, RSSYMdirsortid));
	if (!ISINT(sortId))
		return nil;
	return gSortTables.GetSortTable(RINT(sortId), nil);
}


// ROM 0x00352b2c StoreSaveSortTable__FRC6RefVarl
// NOT YET RECONSTRUCTED: the sorting tables are not kept on the store.
void
StoreSaveSortTable(RefArg /*storeObject*/, long /*sortId*/)
{ }


// ROM 0x00352dac StoreRemoveSortTable__FRC6RefVarl
void
StoreRemoveSortTable(RefArg /*storeObject*/, long /*sortId*/)
{ }


// ROM 0x00100a24 LargeBinariesStoreRemoved__FP13TStoreWrapper
// NOT YET RECONSTRUCTED: large binaries.
void
LargeBinariesStoreRemoved(TStoreWrapper* /*wrapper*/)
{ }


// ROM 0x001008a4 AbortLargeBinaries__FRC6RefVar
void
AbortLargeBinaries(RefArg /*entry*/)
{ }


// The soup name index of a store: a TSoupIndex over the persistent
// frame's nameIndex info object, with the directory sorting table.
void
InitNameIndex(TSoupIndex* index, RefArg storeObject)
{
	RefVar persistent(GetFrameSlotRef(storeObject, RSSYM_proto));
	if ((Ref) persistent == NILREF)
		Throw(exStoreError, (void*) kNSErrInvalidStore, nil);
	TStoreWrapper* wrapper = (TStoreWrapper*) GetFrameSlotRef(storeObject, RSSYMstore);
	PSSId infoId = (PSSId) RINT(GetFrameSlotRef(persistent, RSSYMnameindex));
	index->Init(wrapper, infoId, StoreGetDirSortTable(storeObject));
}


// A soup name as a key of the name index: its UniChars (no terminator).
static void
SoupNameKey(RefArg name, SKey* key)
{
	UniChar* text = GetCString(name);
	long length = Ustrlen(text);
	if (HostIsBigEndian())
		key->Set(length * sizeof(UniChar), text);
	else
	{
		UniChar swapped[kSKeyDataSize / sizeof(UniChar)];
		if (length > (long) (kSKeyDataSize / sizeof(UniChar)))
			length = kSKeyDataSize / sizeof(UniChar);
		memcpy(swapped, text, length * sizeof(UniChar));
		SwapUniChars(swapped, length);
		key->Set(length * sizeof(UniChar), swapped);
	}
}


// A soup name checked: a plain string of at most 39 characters.
static void
CheckSoupName(RefArg name)
{
	if (!IsString(name))
		ThrowBadTypeWithFrameData(kNSErrNotAString, name);
	if (IsRichString(name))
		ThrowBadTypeWithFrameData(kNSErrNotAPlainString, name);
	if (Length(name) / (long) sizeof(UniChar) - 1 > kMaxSoupNameLength)
	{
		RefVar frame(AllocateFrame());
		SetFrameSlot(frame, RSSYMerrorcode, RefVar(MAKEINT(kNSErrSoupNameTooLong)));
		SetFrameSlot(frame, RSSYMvalue, name);
		ThrowRefException(exFramesWithFrameData, frame);
	}
}


// The index error result raised: an index result over 0 is the key size
// error, a negative one the store error it is.
static void
ThrowIndexError(int result)
{
	if (result != kIndexOK)
		Throw(exStoreError, (void*) (Long) (result > 0 ? kNSErrKeySizeTooBig : result), nil);
}


// ROM 0x00354178 MakeStoreObject__FP6TStore
// The store frame over a new TStoreWrapper: an empty root object is
// formatted (the persistent frame {nameIndex: a new TSoupIndex, name:
// "Untitled", signature: the serial number for the internal store, a
// random number otherwise}, the map and symbol tables, the root data
// written); otherwise the root data is read and checked (the 'WALY'
// signature, a version this ROM knows) and the persistent frame loaded.
// The frame: storePrototype with _proto the persistent frame's fault
// block, store the wrapper, soups an entry cache, version.
Ref
MakeStoreObject(TStore* store)
{
	TStoreWrapper* wrapper = new TStoreWrapper(store);
	if (wrapper == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	RefVar storeObject;
	newton_try
	{
		RefVar persistent;
		long version = kStoreRootVersion;
		Boolean formatted = false;
		PSSId rootId;
		OSErrIf(store->GetRootId(&rootId));
		long rootSize;
		OSErrIf(store->GetObjectSize(rootId, &rootSize));
		StoreRootData rootData;
		PSSId rootFrameId = (PSSId) -1;
		if (rootSize == 0)
		{
			formatted = true;
			CheckWriteProtect(store);
			OSErrIf(wrapper->LockStore());
			newton_try
			{
				rootData.fSignature = kStoreRootSignature;
				rootData.fVersion = kStoreRootVersion;
				persistent = Clone(Rstorepersistent);
				IndexInfo info;
				memset(&info, 0, sizeof(info));
				info.fDataType = kKeyTypeLong;
				info.fDuplicates = kIndexUniqueKeys;
				info.fMultiTypes = 0xffffffff;
				info.fMultiAscending = 0xff;
				SetFrameSlot(persistent, RSSYMnameindex, RefVar(MAKEINT(TSoupIndex::Create(wrapper, &info))));
				if (gSortTables.fDefaultId != 0)
					SetFrameSlot(persistent, RSSYMdirsortid, RefVar(MAKEINT(gSortTables.fDefaultId)));
				SetFrameSlot(persistent, RSSYMname, RefVar(MakeString("Untitled")));
				// the internal store is signed with the machine's own serial
				// number - its second word - so that the system can tell its
				// flash from a card's store; any other store gets a random
				// signature.  A store whose signature does not match is the one
				// the ROM notifies about ("The internal store's signature has
				// been altered"), which on the machine a hard reset puts right
				// by formatting the flash again - which is this branch.
				long signature;
				if (GetInternalStore() == store)
				{
					ULong serialNumber[2];
					signature = 0;
					if (GetSystemSerialNumber(serialNumber) == noErr)
						signature = (long) serialNumber[1];
				}
				else
					signature = GetRandomSignature();
				SetFrameSlot(persistent, RSSYMsignature, RefVar(MAKEINT(signature)));
				rootData.fMapTableId = TStoreHashTable::Create(store);
				wrapper->fMapTable = new TStoreHashTable(store, rootData.fMapTableId);
				if (wrapper->fMapTable == nil)
					Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
				rootData.fSymbolTableId = TStoreHashTable::Create(store);
				wrapper->fSymbolTable = new TStoreHashTable(store, rootData.fSymbolTableId);
				if (wrapper->fSymbolTable == nil)
					Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
				StorePermObject(persistent, wrapper, rootFrameId, nil, nil);
				rootData.fRootFrameId = rootFrameId;
				rootData.fExtra = 0;
				WriteStoreRootData(store, rootId, &rootData);
			}
			newton_catch_all
			{
				OSErrIf(wrapper->Abort());
				rethrow;
			}
			end_try;
			OSErrIf(wrapper->UnlockStore());
		}
		else
		{
			ReadStoreRootData(store, rootId, &rootData, nil);
			if (rootData.fSignature != kStoreRootSignature)
				Throw(exStoreError, (void*) kNSErrUnknownStoreVersion, nil);
			if (rootData.fVersion > kStoreRootVersion)
				Throw(exStoreError, (void*) kNSErrNewerStoreVersion, nil);
			version = rootData.fVersion;
			wrapper->fMapTable = new TStoreHashTable(store, rootData.fMapTableId);
			if (wrapper->fMapTable == nil)
				Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
			wrapper->fSymbolTable = new TStoreHashTable(store, rootData.fSymbolTableId);
			if (wrapper->fSymbolTable == nil)
				Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
			rootFrameId = rootData.fRootFrameId;
			persistent = LoadPermObject(wrapper, rootFrameId, nil);
		}
		persistent = MakeFaultBlock(RefVar(NILREF), wrapper, rootFrameId, persistent);
		storeObject = Clone(Rstoreprototype);
		SetFrameSlot(storeObject, RSSYM_proto, persistent);
		SetFrameSlot(storeObject, RSSYMstore, RefVar((Ref) wrapper));
		SetFrameSlot(storeObject, RSSYMsoups, RefVar(MakeEntryCache()));
		SetFrameSlot(storeObject, RSSYMversion, RefVar(MAKEINT(version)));
		if (formatted)
			StoreSaveSortTable(storeObject, gSortTables.fDefaultId);
		// else the ROM loads the store's sort tables (0x00327e3c): NOT YET
		SetupEphemeralTracker(storeObject, rootFrameId);
	}
	newton_catch_all
	{
		delete wrapper;
		rethrow;
	}
	end_try;
	return storeObject;
}


// ROM 0x00354c34 RegisterTStore__FP6TStore
// The store's frame made and added to gStores; each union soup gets the
// store's soup of its name.
Ref
RegisterTStore(TStore* store)
{
	RefVar storeObject(MakeStoreObject(store));
	RefVar stores(gStores);
	AddArraySlot(stores, storeObject);
	newton_try
	{
		RefVar unionSoup;
		RefVar name;
		RefVar soup;
		for (long i = Length(gUnionSoups) - 1; i >= 0; i--)
		{
			unionSoup = GetArraySlotRef(gUnionSoups, i);
			if ((Ref) unionSoup != NILREF)
			{
				name = GetFrameSlotRef(unionSoup, RSSYMthename);
				if ((Ref) name != NILREF)
				{
					soup = StoreGetSoup(storeObject, name);
					if ((Ref) soup != NILREF)
						AddToUnionSoup(unionSoup, soup);
				}
			}
		}
	}
	newton_catch_all
	{
		RemoveTStore(store);
		rethrow;
	}
	end_try;
	return storeObject;
}


// ROM 0x00350a54 RemoveTStore__FP6TStore
// The store's frame taken out of gStores and killed, its wrapper deleted.
void
RemoveTStore(TStore* store)
{
	RefVar storeObject;
	TStoreWrapper* wrapper = nil;
	long count = Length(gStores);
	long i;
	for (i = 0; i < count; i++)
	{
		storeObject = GetArraySlotRef(gStores, i);
		wrapper = (TStoreWrapper*) GetFrameSlotRef(storeObject, RSSYMstore);
		if (wrapper->Store() == store)
			break;
	}
	if (i == count)
		Throw(exStoreError, (void*) kNSErrStoreNotRegistered, nil);
	RefVar stores(gStores);
	RefVar none;
	ArrayMunger(stores, i, 1, none, 0, 0);
	// the ROM forgets the store's sort tables here (0x00327fe8): NOT YET
	KillStoreObject(storeObject);
	if (wrapper != nil)
		delete wrapper;
}


// ROM 0x00350b74 ToObject__FP6TStore
Ref
ToObject(TStore* store)
{
	long count = Length(gStores);
	for (long i = 0; i < count; i++)
	{
		Ref storeObject = GetArraySlotRef(gStores, i);
		if (((TStoreWrapper*) GetFrameSlotRef(storeObject, RSSYMstore))->Store() == store)
			return storeObject;
	}
	return NILREF;
}


// ROM 0x00355360 KillStoreObject__FRC6RefVar
// The store frame and its soups cut off from the store: _protos nilled,
// the soups out of the union soups, their entries invalidated, their
// cursors told (NOT YET), the large binaries told (NOT YET).
void
KillStoreObject(RefArg storeObject)
{
	SetFrameSlot(storeObject, RSSYM_proto, RefVar(NILREF));
	RefVar soups(GetFrameSlotRef(storeObject, RSSYMsoups));
	RefVar soup;
	RefVar name;
	RefVar cache;
	long count = Length(soups);
	for (long i = 0; i < count; i++)
	{
		soup = GetArraySlotRef(soups, i);
		if ((Ref) soup != NILREF)
		{
			name = GetFrameSlotRef(soup, RSSYMthename);
			RemoveFromUnionSoup(name, soup);
			SetFrameSlot(soup, RSSYM_proto, RefVar(NILREF));
			cache = GetFrameSlotRef(soup, RSSYMcache);
			InvalidateCacheEntries(cache);
			EachSoupCursorDo(soup, kSoupCursorSoupRemoved, soup);
			SetArraySlotRef(soups, i, NILREF);
		}
	}
	LargeBinariesStoreRemoved((TStoreWrapper*) GetFrameSlotRef(storeObject, RSSYMstore));
}


// ROM 0x003522c8 FlushSoupList__FRC6RefVar
// Every soup flushed; ==> whether any had something to flush.
Ref
FlushSoupList(RefArg soups)
{
	Ref result = NILREF;
	RefVar soup;
	for (long i = Length(soups) - 1; i >= 0; i--)
	{
		soup = GetArraySlotRef(soups, i);
		if ((Ref) soup != NILREF && PlainSoupFlush(soup) != NILREF)
			result = TRUEREF;
	}
	return result;
}


/*------------------------------------------------------------------------------
	T h e   s t o r e   f r a m e ' s   m e t h o d s
------------------------------------------------------------------------------*/

// The store frame's persistent frame; a killed frame is an error.
static Ref
StorePersistent(RefArg rcvr)
{
	Ref persistent = GetFrameSlotRef(rcvr, RSSYM_proto);
	if (persistent == NILREF)
		Throw(exStoreError, (void*) kNSErrInvalidStore, nil);
	return persistent;
}


// ROM 0x00350e04 StoreGetName
Ref
StoreGetName(RefArg rcvr)
{
	RefVar persistent(StorePersistent(rcvr));
	return GetFrameSlotRef(persistent, RSSYMname);
}


// ROM 0x00350e88 StoreSetName
Ref
StoreSetName(RefArg rcvr, RefArg name)
{
	RefVar persistent(StorePersistent(rcvr));
	CheckWriteProtect(rcvr);
	SetFrameSlot(persistent, RSSYMname, name);
	WriteFaultBlock(persistent);
	return name;
}


// ROM 0x00350de8 StoreGetKind
// The store implementation's kind as a string.
Ref
StoreGetKind(RefArg rcvr)
{
	TStore* store = StoreFromWrapper(rcvr);
	const char* kind = store->StoreKind();
	long length = strlen(kind);
	RefVar str(AllocateBinary(RSSYMstring, (length + 1) * sizeof(UniChar)));
	ConvertToUnicode(kind, (UniChar*) BinaryData(str), kMacRomanEncoding, 0x7fffffff);
	return str;
}


// ROM 0x00350f28 StoreGetSignature
Ref
StoreGetSignature(RefArg rcvr)
{
	RefVar persistent(StorePersistent(rcvr));
	return GetFrameSlotRef(persistent, RSSYMsignature);
}


// ROM 0x00350fac StoreSetSignature
Ref
StoreSetSignature(RefArg rcvr, RefArg signature)
{
	RefVar persistent(StorePersistent(rcvr));
	CheckWriteProtect(rcvr);
	SetFrameSlot(persistent, RSSYMsignature, signature);
	WriteFaultBlock(persistent);
	return signature;
}


// ROM 0x0035104c StoreGetInfo
// The persistent frame's info frame's slot tag.
Ref
StoreGetInfo(RefArg rcvr, RefArg tag)
{
	RefVar persistent(StorePersistent(rcvr));
	if (!FrameHasSlotRef(persistent, RSSYMinfo))
		return NILREF;
	return GetFrameSlotRef(GetFrameSlotRef(persistent, RSSYMinfo), tag);
}


// ROM 0x0035113c StoreSetInfo
Ref
StoreSetInfo(RefArg rcvr, RefArg tag, RefArg value)
{
	RefVar persistent(StorePersistent(rcvr));
	CheckWriteProtect(rcvr);
	RefVar theValue(TotalClone(value));
	RefVar theTag(EnsureInternal(tag));
	if (!FrameHasSlotRef(persistent, RSSYMinfo))
	{
		RefVar info(AllocateFrame());
		SetFrameSlot(info, theTag, theValue);
		SetFrameSlot(persistent, RSSYMinfo, info);
	}
	else
	{
		RefVar info(GetFrameSlotRef(persistent, RSSYMinfo));
		SetFrameSlot(info, theTag, theValue);
	}
	WriteFaultBlock(persistent);
	return NILREF;
}


// ROM 0x00351290 StoreGetAllInfo
Ref
StoreGetAllInfo(RefArg rcvr)
{
	RefVar persistent(StorePersistent(rcvr));
	RefVar info(GetFrameSlotRef(persistent, RSSYMinfo));
	return Clone(info);
}


// ROM 0x00351330 StoreSetAllInfo
Ref
StoreSetAllInfo(RefArg rcvr, RefArg info)
{
	if (!IsFrame(info))
		Throw(exBadType, (void*) kNSErrNotAFrame, nil);
	RefVar persistent(StorePersistent(rcvr));
	CheckWriteProtect(rcvr);
	SetFrameSlot(persistent, RSSYMinfo, RefVar(TotalClone(info)));
	WriteFaultBlock(persistent);
	return NILREF;
}


// ROM 0x00351844 StoreGetSoupId__FRC6RefVarT1
// The id of the persistent frame of the soup named, from the name index;
// 0 for none.
PSSId
StoreGetSoupId(RefArg rcvr, RefArg name)
{
	StorePersistent(rcvr);
	TSoupIndex index;
	InitNameIndex(&index, rcvr);
	SKey key;
	SKey data;
	SoupNameKey(name, &key);
	if (index.Find(&key, nil, &data, false) != kIndexOK)
		return 0;
	return (PSSId) (long) data;
}


// ROM 0x003519c0 StoreHasSoup
Ref
StoreHasSoup(RefArg rcvr, RefArg name)
{
	return MAKEBOOLEAN(StoreGetSoupId(rcvr, name) != 0);
}


// ROM 0x00351410 StoreGetSoup
// The soup named: the store's cached soup frame, else one made over the
// persistent frame the name index gives (a plainSoupPrototype with its
// fault block, tStore, storeObj, theName, entry and cursor caches, its
// index objects and next _uniqueID) and cached; nil for none.
Ref
StoreGetSoup(RefArg rcvr, RefArg name)
{
	StorePersistent(rcvr);
	RefVar soups(GetFrameSlotRef(rcvr, RSSYMsoups));
	RefVar soup(FindSoupInCache(soups, name));
	if ((Ref) soup != NILREF)
		return soup;
	TStoreWrapper* wrapper = (TStoreWrapper*) GetFrameSlotRef(rcvr, RSSYMstore);
	TSoupIndex index;
	InitNameIndex(&index, rcvr);
	SKey key;
	SKey data;
	SoupNameKey(name, &key);
	if (index.Find(&key, &key, &data, false) != kIndexOK)
		return NILREF;
	PSSId id = (PSSId) (long) data;
	RefVar persistent(LoadPermObject(wrapper, id, nil));
	persistent = MakeFaultBlock(RefVar(NILREF), wrapper, id, persistent);
	soup = Clone(Rplainsoupprototype);
	SetFrameSlot(soup, RSSYM_proto, persistent);
	SetFrameSlot(soup, RSSYMtstore, RefVar((Ref) wrapper));
	SetFrameSlot(soup, RSSYMstoreobj, rcvr);
	RefVar theName(SKeyToKey(key, RSSYMstring, nil));
	SetClass(theName, RSSYMstring_2Enohint);
	SetFrameSlot(soup, RSSYMthename, theName);
	SetFrameSlot(soup, RSSYMcache, RefVar(MakeEntryCache()));
	SetFrameSlot(soup, RSSYMcursors, RefVar(MakeEntryCache()));
	CreateSoupIndexObjects(soup);
	RefVar lastUID(GetFrameSlotRef(persistent, RSSYMlastuid));
	if ((Ref) lastUID == NILREF)
	{
		// an older soup: the next _uniqueID is one past the last key of the _uniqueID index
		SKey lastKey;
		lastKey = 0L;
		if (GetSoupIndexObject(soup, 0)->Last(&lastKey, nil) == kIndexOK)
			lastUID = MAKEINT((long) lastKey + 1);
		else
			lastUID = MAKEINT(0);
	}
	SetFrameSlot(soup, RSSYMindexnextuid, lastUID);
	PutEntryIntoCache(soups, soup);
	return soup;
}


// ROM 0x003519e0 StoreCreateSoup
// A new soup: its persistent frame (plainSoupPersistent with class
// 'DiskSoup, lastUID 0, a random signature, the mod times, the
// _uniqueID index and the indexes asked for) stored and its name put in
// the name index; then the soup frame as StoreGetSoup makes it.
Ref
StoreCreateSoup(RefArg rcvr, RefArg name, RefArg indexes)
{
	CheckSoupName(name);
	if (StoreHasSoup(rcvr, name) != NILREF)
		Throw(exStoreError, (void*) kNSErrDuplicateSoupName, nil);
	StorePersistent(rcvr);
	TStoreWrapper* wrapper = (TStoreWrapper*) GetFrameSlotRef(rcvr, RSSYMstore);
	CheckWriteProtect(wrapper->Store());
	RefVar persistent(Clone(Rplainsouppersistent));
	SetFrameSlot(persistent, RSSYMclass, RSSYMdisksoup);
	SetFrameSlot(persistent, RSSYMlastuid, RefVar(MAKEINT(0)));
	SetFrameSlot(persistent, RSSYMsignature, RefVar(MAKEINT(GetRandomSignature())));
	Ref now = MAKEINT(RealClock() & 0x1fffffff);
	SetFrameSlot(persistent, RSSYMindexesmodtime, RefVar(now));
	SetFrameSlot(persistent, RSSYMinfomodtime, RefVar(now));
	wrapper->LockStore();
	RefVar soup;
	newton_try
	{
		AddNewSoupIndexes(persistent, rcvr, indexes);
		PSSId id = (PSSId) -1;
		StorePermObject(persistent, wrapper, id, nil, nil);
		TSoupIndex index;
		InitNameIndex(&index, rcvr);
		SKey key;
		SKey data;
		SoupNameKey(name, &key);
		data = (long) id;
		ThrowIndexError(index.Add(&key, &data));
		soup = StoreGetSoup(rcvr, name);
		AddToUnionSoup(name, soup);
	}
	newton_catch_all
	{
		wrapper->Abort();
		RefVar soups(GetFrameSlotRef(rcvr, RSSYMsoups));
		DeleteEntryFromCache(soups, soup);
		rethrow;
	}
	end_try;
	wrapper->UnlockStore();
	return soup;
}


// ROM 0x00350c28 StoreGetSoupNames
// The names in the name index, in its order.
Ref
StoreGetSoupNames(RefArg rcvr)
{
	StorePersistent(rcvr);
	TSoupIndex index;
	InitNameIndex(&index, rcvr);
	RefVar names(AllocateArray(RSSYMarray, 0));
	SKey key;
	SKey data;
	key.Clear();
	data.Clear();
	int result = index.First(&key, &data);
	while (result == kIndexOK)
	{
		AddArraySlot(names, RefVar(SKeyToKey(key, RSSYMstring, nil)));
		result = index.Next(&key, &data, kIndexNextKey, &key, &data);
	}
	return names;
}


// ROM 0x00351ee0 StoreTotalSize
Ref
StoreTotalSize(RefArg rcvr)
{
	long total, used;
	GetStoreWrapper(rcvr)->GetStoreSizes(&total, &used);
	return MAKEINT(total);
}


// ROM 0x00351f14 StoreUsedSize
Ref
StoreUsedSize(RefArg rcvr)
{
	long total, used;
	GetStoreWrapper(rcvr)->GetStoreSizes(&total, &used);
	return MAKEINT(used);
}


// ROM 0x003520a8 StoreOverhead
// The name index's and the map and symbol tables' sizes.
Ref
StoreOverhead(RefArg rcvr)
{
	StorePersistent(rcvr);
	TStoreWrapper* wrapper = (TStoreWrapper*) GetFrameSlotRef(rcvr, RSSYMstore);
	TSoupIndex index;
	InitNameIndex(&index, rcvr);
	long size = index.TotalSize() + wrapper->fMapTable->TotalSize() + wrapper->fSymbolTable->TotalSize();
	return MAKEINT(size);
}


// ROM 0x00351ffc StoreIsReadOnly
Ref
StoreIsReadOnly(RefArg rcvr)
{
	Boolean readOnly;
	OSErrIf(GetStoreWrapper(rcvr)->Store()->IsReadOnly(&readOnly));
	return MAKEBOOLEAN(readOnly);
}


// ROM 0x003521cc StoreIsValid
// Registered (or a package store) and not killed.
Ref
StoreIsValid(RefArg rcvr)
{
	if (GetFrameSlotRef(rcvr, RSSYM_proto) == NILREF)
		return NILREF;
	TStoreWrapper* wrapper = (TStoreWrapper*) GetFrameSlotRef(rcvr, RSSYMstore);
	if (IsValidStore(wrapper->Store()))
		return TRUEREF;
	for (long i = Length(gPackageStores) - 1; i >= 0; i--)
		if (EQRef(GetArraySlotRef(gPackageStores, i), rcvr))
			return TRUEREF;
	return NILREF;
}


// ROM 0x00352038 StoreLock
// The store's transaction lock taken; ==> whether it is now locked.
Ref
StoreLock(RefArg rcvr)
{
	TStoreWrapper* wrapper = GetStoreWrapper(rcvr);
	OSErrIf(wrapper->LockStore());
	return MAKEBOOLEAN(wrapper->Store()->IsLocked());
}


// ROM 0x00352070 StoreUnlock
Ref
StoreUnlock(RefArg rcvr)
{
	TStoreWrapper* wrapper = GetStoreWrapper(rcvr);
	OSErrIf(wrapper->UnlockStore());
	return MAKEBOOLEAN(wrapper->Store()->IsLocked());
}


// ROM 0x00355504 FStoreAbort
// The store's transaction aborted.
Ref
StoreAbort(RefArg rcvr)
{
	OSErrIf(StoreFromWrapper(rcvr)->Abort());
	return NILREF;
}


// ROM 0x00352360 StoreDirty__FRC6RefVar
Ref
StoreDirty(RefArg rcvr)
{
	GetStoreWrapper(rcvr)->Dirty();
	return NILREF;
}


// ROM 0x0035237c StoreFlush
// Every soup's dirty entries written; the store clean.
Ref
StoreFlush(RefArg rcvr)
{
	RefVar soups(GetFrameSlotRef(rcvr, RSSYMsoups));
	RefVar result(FlushSoupList(soups));
	GetStoreWrapper(rcvr)->SparklingClean();
	return result;
}


// ROM 0x003523ec StoreErase
// The store formatted afresh and registered again in the same place.
Ref
StoreErase(RefArg rcvr)
{
	TStoreWrapper* wrapper = GetStoreWrapper(rcvr);
	CheckWriteProtect(rcvr);
	RefVar stores(gStores);
	RefVar none;
	long slot = ArrayPosition(stores, rcvr, 0, none);
	TStore* store = wrapper->Store();
	RemoveTStore(store);
	NewtonErr err = store->Format();
	if (err != noErr)
		Throw(exStoreError, (void*) (Long) err, nil);
	RefVar storeObject(RegisterTStore(store));
	if (slot != -1)
	{
		stores = gStores;
		long newSlot = ArrayPosition(stores, storeObject, 0, none);
		if (newSlot != -1 && newSlot != slot)
		{
			Ref other = GetArraySlotRef(gStores, slot);
			SetArraySlotRef(gStores, newSlot, other);
			SetArraySlotRef(gStores, slot, storeObject);
		}
	}
	return storeObject;
}


// ROM 0x00350c10 StoreCheckWriteProtect
Ref
StoreCheckWriteProtect(RefArg rcvr)
{
	CheckWriteProtect(rcvr);
	return NILREF;
}


// ROM 0x00354f74 FReadStoreObject
// A store object's bytes as a binary.
Ref
StoreReadObject(RefArg rcvr, RefArg id)
{
	TStore* store = StoreFromWrapper(rcvr);
	long size;
	OSErrIf(store->GetObjectSize((PSSId) RINT(id), &size));
	RefVar data(AllocateBinary(RSSYMbinary, size));
	OSErrIf(store->Read((PSSId) RINT(id), 0, BinaryData(data), size));
	return data;
}


// ROM 0x0035512c FWriteStoreObject
Ref
StoreWriteObject(RefArg rcvr, RefArg id, RefArg offset, RefArg data)
{
	TStore* store = StoreFromWrapper(rcvr);
	CheckWriteProtect(store);
	OSErrIf(store->Write((PSSId) RINT(id), RINT(offset), BinaryData(data), Length(data)));
	return NILREF;
}


// ROM 0x00355090 FWriteEntireStoreObject
Ref
StoreWriteWholeObject(RefArg rcvr, RefArg id, RefArg data)
{
	TStore* store = StoreFromWrapper(rcvr);
	CheckWriteProtect(store);
	OSErrIf(store->ReplaceObject((PSSId) RINT(id), BinaryData(data), Length(data)));
	return NILREF;
}


// ROM 0x003551f4 FNewStoreObject
Ref
StoreNewObject(RefArg rcvr, RefArg size)
{
	TStore* store = StoreFromWrapper(rcvr);
	CheckWriteProtect(store);
	PSSId id;
	OSErrIf(store->NewObject(RINT(size), &id));
	return MAKEINT(id);
}


// ROM 0x0035524c FDeleteStoreObject
Ref
StoreDeleteObject(RefArg rcvr, RefArg id)
{
	TStore* store = StoreFromWrapper(rcvr);
	CheckWriteProtect(store);
	OSErrIf(store->DeleteObject((PSSId) RINT(id)));
	return NILREF;
}


// ROM 0x00355298 FSetStoreObjectSize
Ref
StoreSetObjectSize(RefArg rcvr, RefArg id, RefArg size)
{
	TStore* store = StoreFromWrapper(rcvr);
	CheckWriteProtect(store);
	OSErrIf(store->SetObjectSize((PSSId) RINT(id), RINT(size)));
	return NILREF;
}


// ROM 0x00355308 FGetStoreObjectSize
Ref
StoreGetObjectSize(RefArg rcvr, RefArg id)
{
	TStore* store = StoreFromWrapper(rcvr);
	long size;
	OSErrIf(store->GetObjectSize((PSSId) RINT(id), &size));
	return MAKEINT(size);
}


/*------------------------------------------------------------------------------
	T h e   s o u p   m e s s a g e s
	What C++ sends a soup, plain or union: the soup frame's method.
------------------------------------------------------------------------------*/

static Ref
SendSoup(RefArg soup, RefArg message)
{
	RefVar args(NILREF);
	return DoMessage(soup, message, args);
}


static Ref
SendSoup(RefArg soup, RefArg message, RefArg arg)
{
	RefVar args(AllocateArray(RSSYMarray, 1));
	SetArraySlotRef(args, 0, arg);
	return DoMessage(soup, message, args);
}


static Ref
SendSoup(RefArg soup, RefArg message, RefArg arg1, RefArg arg2)
{
	RefVar args(AllocateArray(RSSYMarray, 2));
	SetArraySlotRef(args, 0, arg1);
	SetArraySlotRef(args, 1, arg2);
	return DoMessage(soup, message, args);
}


// ROM 0x0033f698 PathsEqual__FRC6RefVarT1
// The same path: the same object, or arrays of the same class whose
// elements are EQ.
Boolean
PathsEqual(RefArg a, RefArg b)
{
	if (EQRef(a, b))
		return true;
	ULong aFlags = ObjectFlags(a);
	ULong bFlags = ObjectFlags(b);
	if ((aFlags & (kObjSlotted | kObjFrame)) != (bFlags & (kObjSlotted | kObjFrame)) || (aFlags & kObjSlotted) == 0)
		return false;
	long count = Length(a);
	if (Length(b) != count)
		return false;
	for (long i = 0; i < count; i++)
		if (!EQRef(GetArraySlotRef(a, i), GetArraySlotRef(b, i)))
			return false;
	return EQRef(ClassOf(a), ClassOf(b));
}


// ROM 0x0033f7ac SoupQuery__FRC6RefVarT1
Ref
SoupQuery(RefArg soup, RefArg querySpec)
{
	return SendSoup(soup, RSSYMquery, querySpec);
}


// ROM 0x0033f818 SoupGetName__FRC6RefVar
Ref
SoupGetName(RefArg soup)
{
	return SendSoup(soup, RSSYMgetname);
}


// ROM 0x0033f860 SoupGetSignature__FRC6RefVar
Ref
SoupGetSignature(RefArg soup)
{
	return SendSoup(soup, RSSYMgetsignature);
}


// ROM 0x0033f8a8 SoupSetName__FRC6RefVarT1
Ref
SoupSetName(RefArg soup, RefArg name)
{
	return SendSoup(soup, RSSYMsetname, name);
}


// ROM 0x0033f914 SoupSetSignature__FRC6RefVarl
Ref
SoupSetSignature(RefArg soup, long signature)
{
	return SendSoup(soup, RSSYMsetsignature, RefVar(MAKEINT(signature)));
}


// ROM 0x0033f97c SoupGetInfo__FRC6RefVarT1
Ref
SoupGetInfo(RefArg soup, RefArg tag)
{
	return SendSoup(soup, RSSYMgetinfo, tag);
}


// ROM 0x0033f9e8 SoupSetInfo__FRC6RefVarN21
Ref
SoupSetInfo(RefArg soup, RefArg tag, RefArg value)
{
	return SendSoup(soup, RSSYMsetinfo, tag, value);
}


// ROM 0x0033fa70 SoupGetAllInfo__FRC6RefVar
Ref
SoupGetAllInfo(RefArg soup)
{
	return SendSoup(soup, RSSYMgetallinfo);
}


// ROM 0x0033fab8 SoupSetAllInfo__FRC6RefVarT1
Ref
SoupSetAllInfo(RefArg soup, RefArg info)
{
	return SendSoup(soup, RSSYMsetallinfo, info);
}


// ROM 0x0033fb24 SoupCopyEntries__FRC6RefVarT1
Ref
SoupCopyEntries(RefArg soup, RefArg toSoup)
{
	return SendSoup(soup, RSSYMcopyentries, toSoup);
}


// ROM 0x0033fb90 SoupRemoveAllEntries__FRC6RefVar
Ref
SoupRemoveAllEntries(RefArg soup)
{
	return SendSoup(soup, RSSYMremoveallentries);
}


// ROM 0x0033fbdc SoupRemoveFromStore__FRC6RefVar
Ref
SoupRemoveFromStore(RefArg soup)
{
	return SendSoup(soup, RSSYMremovefromstore);
}


// ROM 0x0033fc24 SoupFlush__FRC6RefVar
Ref
SoupFlush(RefArg soup)
{
	return SendSoup(soup, RSSYMflush);
}


// ROM 0x0033fcbc SoupGetStore__FRC6RefVar
Ref
SoupGetStore(RefArg soup)
{
	return SendSoup(soup, RSSYMgetstore);
}


// ROM 0x0033fd04 SoupAddIndex__FRC6RefVarT1
Ref
SoupAddIndex(RefArg soup, RefArg indexSpec)
{
	return SendSoup(soup, RSSYMaddindex, indexSpec);
}


// ROM 0x0033fd70 SoupRemoveIndex__FRC6RefVarT1
Ref
SoupRemoveIndex(RefArg soup, RefArg path)
{
	return SendSoup(soup, RSSYMremoveindex, path);
}


// ROM 0x0033fddc SoupGetIndexes__FRC6RefVar
Ref
SoupGetIndexes(RefArg soup)
{
	return SendSoup(soup, RSSYMgetindexes);
}


// ROM 0x0033fe24 SoupGetNextUID__FRC6RefVar
Ref
SoupGetNextUID(RefArg soup)
{
	return SendSoup(soup, RSSYMgetnextuid);
}


// ROM 0x0033fe6c SoupAdd__FRC6RefVarT1
Ref
SoupAdd(RefArg soup, RefArg entry)
{
	return SendSoup(soup, RSSYMadd, entry);
}


// ROM 0x0033fed8 SoupAddWithUniqueID__FRC6RefVarT1
Ref
SoupAddWithUniqueID(RefArg soup, RefArg entry)
{
	return SendSoup(soup, RSSYMaddwithuniqueid, entry);
}


/*------------------------------------------------------------------------------
	T h e   p l a i n   s o u p
------------------------------------------------------------------------------*/

// The soup frame's persistent frame (its _proto); a removed soup has
// none.
Ref
SoupPersistent(RefArg soup)
{
	Ref persistent = GetFrameSlotRef(soup, RSSYM_proto);
	if (persistent == NILREF)
		Throw(exStoreError, (void*) kNSErrSoupRemoved, nil);
	return persistent;
}


// ROM 0x00347b08 SoupChanged__FRC6RefVarUc
// The persistent frame's flags say the soup has changed (bits 0 and 1),
// written when asked - nothing when they say so already.
void
SoupChanged(RefArg soupPersistent, Boolean write)
{
	Ref flags = GetFrameSlotRef(soupPersistent, RSSYMflags);
	long value = flags == NILREF ? 0 : RINT(flags);
	if ((value & 3) == 3)
		return;
	SetFrameSlot(soupPersistent, RSSYMflags, RefVar(MAKEINT(value | 3)));
	if (write)
		WriteFaultBlock(soupPersistent);
}


// ROM 0x00347a34 GetTagsIndexDesc__FRC6RefVar
// The soup's tags index description; nil for none.
Ref
GetTagsIndexDesc(RefArg soupPersistent)
{
	RefVar indexes(GetFrameSlotRef(soupPersistent, RSSYMindexes));
	if ((Ref) indexes != NILREF)
	{
		RefVar indexDesc;
		for (long i = Length(indexes) - 1; i >= 0; i--)
		{
			indexDesc = GetArraySlotRef(indexes, i);
			if (EQRef(GetFrameSlotRef(indexDesc, RSSYMtype), RSSYMtags))
				return indexDesc;
		}
	}
	return NILREF;
}


// ROM 0x00347ddc IndexPathsEqual__FRC6RefVarT1
// Two index paths the same: both arrays of paths of the same length with
// equal paths, or neither an array and equal paths.
Boolean
IndexPathsEqual(RefArg a, RefArg b)
{
	long aCount = EQRef(ClassOf(a), RSSYMarray) ? Length(a) : 0;
	if (!EQRef(ClassOf(b), RSSYMarray))
		return aCount == 0 && PathsEqual(a, b);
	if (Length(b) != aCount)
		return false;
	RefVar aPath;
	RefVar bPath;
	for (long i = aCount - 1; i >= 0; i--)
	{
		aPath = GetArraySlotRef(a, i);
		bPath = GetArraySlotRef(b, i);
		if (!PathsEqual(bPath, aPath))
			return false;
	}
	return true;
}


// ROM 0x00347f08 IndexPathToIndexDesc__FRC6RefVarT1Pl
// The soup's index description on path (and its index in the indexes
// array); nil for none.
Ref
IndexPathToIndexDesc(RefArg soupPersistent, RefArg path, long* index)
{
	RefVar indexes(GetFrameSlotRef(soupPersistent, RSSYMindexes));
	RefVar indexDesc;
	RefVar indexPath;
	for (long i = Length(indexes) - 1; i >= 0; i--)
	{
		indexDesc = GetArraySlotRef(indexes, i);
		indexPath = GetFrameSlotRef(indexDesc, RSSYMpath);
		if (IndexPathsEqual(path, indexPath))
		{
			if (index != nil)
				*index = i;
			return indexDesc;
		}
	}
	return NILREF;
}


// ROM 0x00347ffc GetIndexSortTable__FRC6RefVar
// The sorting table an index description's sortId names; no sortId slot
// means no table at all (the folding compare).
const TSortingTable*
GetIndexSortTable(RefArg indexDesc)
{
	RefVar sortId(GetFrameSlotRef(indexDesc, RSSYMsortid));
	if (!ISINT(sortId))
		return nil;
	return gSortTables.GetSortTable(RINT(sortId), nil);
}


// ROM 0x00348e3c IndexDescToIndexInfo__FRC6RefVarP9IndexInfo
// An index description as the B-tree's IndexInfo: the data type is long
// (a store object id), the key type from type - a multiSlot's from its
// array of types, 4 bits each, with a bit per sub-key for ascending; a
// 'tags index has raw keys; the _uniqueID index takes no duplicate
// keys; the index is descending when order is given and is not
// 'ascending.
void
IndexDescToIndexInfo(RefArg indexDesc, IndexInfo* info)
{
	info->fDataType = kKeyTypeLong;
	info->fDuplicates = EQRef(GetFrameSlotRef(indexDesc, RSSYMpath), RSSYM_uniqueid) ? kIndexUniqueKeys : kIndexDuplicateKeys;
	info->fMultiTypes = 0xffffffff;
	info->fMultiAscending = 0xff;
	info->fDescending = 0;
	info->fUnused[0] = 0;
	info->fUnused[1] = 0;
	RefVar type(GetFrameSlotRef(indexDesc, RSSYMtype));
	Boolean isMulti = IsArray(type);
	RefVar order(GetFrameSlotRef(indexDesc, RSSYMorder));
	long count;
	if (!isMulti)
	{
		count = 1;
		if ((Ref) order != NILREF && !EQRef(order, RSSYMascending))
			info->fDescending = 1;
	}
	else
	{
		count = Length(type);
		info->fKeyType = kKeyTypeMulti;
	}
	RefVar subType;
	for (long i = count - 1; i >= 0; i--)
	{
		subType = isMulti ? GetArraySlotRef(type, i) : (Ref) type;
		long keyType;
		if (EQRef(subType, RSSYMstring))
			keyType = kKeyTypeString;
		else if (EQRef(subType, RSSYMint))
			keyType = kKeyTypeLong;
		else if (EQRef(subType, RSSYMreal))
			keyType = kKeyTypeDouble;
		else if (EQRef(subType, RSSYMchar))
			keyType = kKeyTypeChar;
		else if (EQRef(subType, RSSYMsymbol))
			keyType = kKeyTypeASCII;
		else if (!isMulti && EQRef(type, RSSYMtags))
		{
			info->fKeyType = kKeyTypeLong;
			info->fDataType = kKeyTypeRaw;
			info->fDuplicates = kIndexUniqueKeys;
			continue;
		}
		else
		{
			Throw(exStoreError, (void*) kNSErrBadIndexType, nil);
			keyType = 0;
		}
		if (!isMulti)
			info->fKeyType = keyType;
		else
		{
			info->fMultiTypes = (info->fMultiTypes << 4) | keyType;
			if ((Ref) order != NILREF)
			{
				info->fMultiAscending <<= 1;
				if (EQRef(GetArraySlotRef(order, i), RSSYMascending))
					info->fMultiAscending |= 1;
			}
		}
	}
}


// ROM 0x00349610 NewIndexDesc__FRC6RefVarN21
// A new index description from the spec (a total clone of it, checked:
// no index on the path already; structure 'slot or 'multiSlot - the
// latter with arrays of at most six paths and as many types; a 'tags
// index only once, with a tags array; a string index with the default
// sort table's id: NOT YET) with its B-tree created on the store.
Ref
NewIndexDesc(RefArg soupPersistent, RefArg storeObject, RefArg indexSpec)
{
	RefVar path(GetFrameSlotRef(indexSpec, RSSYMpath));
	if (IndexPathToIndexDesc(soupPersistent, path, nil) != NILREF)
		Throw(exStoreError, (void*) kNSErrIndexExists, nil);
	RefVar type(GetFrameSlotRef(indexSpec, RSSYMtype));
	RefVar structure(GetFrameSlotRef(indexSpec, RSSYMstructure));
	Boolean isMulti = false;
	if (EQRef(structure, RSSYMmultislot))
	{
		isMulti = true;
		if (!EQRef(ClassOf(path), RSSYMarray) || !IsArray(type))
			Throw(exStoreError, (void*) kNSErrBadMultiSlotIndex, nil);
		long count = Length(path);
		if (Length(type) != count || count > 6)
			Throw(exStoreError, (void*) kNSErrBadMultiSlotIndex, nil);
	}
	else if (!EQRef(structure, RSSYMslot))
		Throw(exStoreError, (void*) kNSErrBadIndexStructure, nil);
	RefVar indexDesc(TotalClone(indexSpec));
	if (EQRef(type, RSSYMtags))
	{
		if (GetTagsIndexDesc(soupPersistent) != NILREF)
			Throw(exStoreError, (void*) kNSErrIndexExists, nil);
		if (GetFrameSlotRef(indexDesc, RSSYMtags) == NILREF)
			SetFrameSlot(indexDesc, RSSYMtags, RefVar(AllocateArray(RSSYMarray, 0)));
	}
	Boolean hasString = EQRef(type, RSSYMstring);
	if (!hasString && isMulti)
	{
		RefVar none;
		hasString = FSetContains(none, type, RSSYMstring) != NILREF;
	}
	if (hasString)
	{
		RefVar sortId(GetFrameSlotRef(indexDesc, RSSYMsortid));
		long id = gSortTables.fDefaultId;
		if ((Ref) sortId == NILREF)
		{
			if (gSortTables.fDefaultId != 0)
				SetFrameSlot(indexDesc, RSSYMsortid, RefVar(MAKEINT(gSortTables.fDefaultId)));
		}
		else
		{
			id = RINT(sortId);
			if (id == 0)
				RemoveSlot(indexDesc, RSSYMsortid);
		}
		if (id != 0)
			StoreSaveSortTable(storeObject, id);
	}
	IndexInfo info;
	memset(&info, 0, sizeof(info));
	IndexDescToIndexInfo(indexDesc, &info);
	TStoreWrapper* wrapper = (TStoreWrapper*) GetFrameSlotRef(storeObject, RSSYMstore);
	SetFrameSlot(indexDesc, RSSYMindex, RefVar(MAKEINT(TSoupIndex::Create(wrapper, &info))));
	return indexDesc;
}


// ROM 0x00349cd8 AddNewSoupIndexes__FRC6RefVarN21
// A new soup's indexes: the _uniqueID index (indexDescPrototype) first,
// then one for each spec.
Ref
AddNewSoupIndexes(RefArg soupPersistent, RefArg storeObject, RefArg indexSpecs)
{
	RefVar indexes(AllocateArray(RSSYMarray, 0));
	SetFrameSlot(soupPersistent, RSSYMindexes, indexes);
	RefVar indexDesc(NewIndexDesc(soupPersistent, storeObject, Rindexdescprototype));
	AddArraySlot(indexes, indexDesc);
	if ((Ref) indexSpecs != NILREF)
	{
		RefVar spec;
		long count = Length(indexSpecs);
		for (long i = 0; i < count; i++)
		{
			spec = GetArraySlotRef(indexSpecs, i);
			indexDesc = NewIndexDesc(soupPersistent, storeObject, spec);
			AddArraySlot(indexes, indexDesc);
		}
	}
	return indexes;
}


// ROM 0x003493d0 GCDeleteIndexObjects__FPv
// Nothing to do when the soup's index objects are collected.
static void
GCDeleteIndexObjects(void* /*indexObjects*/)
{ }


// ROM 0x003493d4 CreateSoupIndexObjects__FRC6RefVar
// The soup's TSoupIndex objects, one per index description, in a C
// object binary in its indexObjects slot; the cursors told (NOT YET).
void
CreateSoupIndexObjects(RefArg soup)
{
	RefVar persistent(GetFrameSlotRef(soup, RSSYM_proto));
	RefVar indexes(GetFrameSlotRef(persistent, RSSYMindexes));
	long count = Length(indexes);
	RefVar indexObjects(AllocateFramesCObject(count * sizeof(TSoupIndex), GCDeleteIndexObjects, nil, nil));
	TSoupIndex* objects = (TSoupIndex*) BinaryData(indexObjects);
	for (long i = 0; i < count; i++)
		new (&objects[i]) TSoupIndex;
	SetFrameSlot(soup, RSSYMindexobjects, indexObjects);
	TStoreWrapper* wrapper = (TStoreWrapper*) GetFrameSlotRef(soup, RSSYMtstore);
	RefVar indexDesc;
	for (long i = 0; i < count; i++)
	{
		indexDesc = GetArraySlotRef(indexes, i);
		const TSortingTable* sortTable = GetIndexSortTable(indexDesc);
		PSSId infoId = (PSSId) RINT(GetFrameSlotRef(indexDesc, RSSYMindex));
		objects[i].Init(wrapper, infoId, sortTable);
	}
	EachSoupCursorDo(soup, kSoupCursorIndexesChanged);
}


// ROM 0x00349564 GetSoupIndexObject__FRC6RefVarUl
// The soup's TSoupIndex over info object infoId (0: the first, the
// _uniqueID index); nil for none.
TSoupIndex*
GetSoupIndexObject(RefArg soup, PSSId infoId)
{
	TSoupIndex* objects = (TSoupIndex*) BinaryData(GetFrameSlotRef(soup, RSSYMindexobjects));
	if (infoId == 0)
		return objects;
	RefVar persistent(GetFrameSlotRef(soup, RSSYM_proto));
	long count = Length(GetFrameSlotRef(persistent, RSSYMindexes));
	for (long i = 0; i < count; i++)
		if (objects[i].fInfoId == infoId)
			return &objects[i];
	return nil;
}


// ROM 0x00349128 IndexEntries__FRC6RefVarT1
// Every entry of the soup (walked through the _uniqueID index, read
// through one fault block re-pointed at each) put into a new index in
// one transaction (a tags index: each entry's tags in its own).
void
IndexEntries(RefArg soup, RefArg indexDesc)
{
	TStoreWrapper* wrapper = (TStoreWrapper*) GetFrameSlotRef(soup, RSSYMtstore);
	Boolean isTags = EQRef(GetFrameSlotRef(indexDesc, RSSYMtype), RSSYMtags);
	TSoupIndex* uniqueIdIndex = GetSoupIndexObject(soup, 0);
	TSoupIndex* index = GetSoupIndexObject(soup, (PSSId) RINT(GetFrameSlotRef(indexDesc, RSSYMindex)));
	SKey key;
	SKey data;
	key.Clear();
	data.Clear();
	if (uniqueIdIndex->First(&key, &data) != kIndexOK)
		return;
	RefVar tags;
	RefVar path;
	if (isTags)
	{
		tags = GetFrameSlotRef(indexDesc, RSSYMtags);
		path = GetFrameSlotRef(indexDesc, RSSYMpath);
	}
	RefVar entry(MakeFaultBlock(soup, wrapper, 0));
	RefVar entryTags;
	do {
		Ref* slots = FaultBlockSlots(entry);
		slots[kFaultBlockIdSlot] = MAKEINT((long) data);
		slots[kFaultBlockObjectSlot] = NILREF;
		if (isTags)
		{
			entryTags = GetEntryKey(entry, path);
			if ((Ref) entryTags != NILREF)
				AlterTagsIndex(true, *index, (PSSId) (long) data, entryTags, soup, tags);
		}
		else
		{
			SKey entryKey;
			memset(&entryKey, 0, sizeof(entryKey));
			if (GetEntrySKey(entry, indexDesc, &entryKey, nil))
				ThrowIndexError(index->AddInTransaction(&entryKey, &data));
		}
	} while (uniqueIdIndex->Next(&key, &data, kIndexNextDupOrKey, &key, &data) == kIndexOK);
	if (!isTags)
		index->fNodeCache->Commit(index);
}


// ROM 0x00349a04 RichStringToSKey__FRC6RefVarP4SKey
// The plain characters of a rich string (its ink characters as 0xf702)
// as a string key, at most 39 of them.
void
RichStringToSKey(RefArg string, SKey* outKey)
{
	const UniChar* text = (const UniChar*) BinaryData(string);
	UByte* data = outKey->Data();
	long size = 0;
	while (size < kSKeyDataSize)
	{
		UniChar ch = *text++;
		if (ch == 0)
			break;
		if (ch == 0xf700)
			ch = 0xf702;
		PutBigEndianHalf(data + size, ch);
		size += sizeof(UniChar);
	}
	outKey->SetSize((short) size);
}


// ROM 0x00348c38 MultiKeyToSKey__FRC6RefVarT1P4SKey
// A multiSlot key: the sub-keys (an array of them, or one) each as an
// SKey of its type, one after the other, padded even; a nil sub-key is
// missing; a variable-size sub-key that would run past the end is cut
// down or left out.
void
MultiKeyToSKey(RefArg key, RefArg types, SKey* outKey)
{
	outKey->SetFlags(0);
	Boolean isArray = IsArray(key);
	long count = isArray ? Length(key) : 1;
	long size = 0;
	RefVar subKey;
	RefVar subType;
	for (long i = 0; i < count; i++)
	{
		subKey = isArray ? GetArraySlotRef(key, i) : (Ref) key;
		if ((Ref) subKey == NILREF)
		{
			outKey->SetMissingKey(i);
			continue;
		}
		SKey sub;
		memset(&sub, 0, sizeof(sub));
		short subSize;
		Boolean isVariable;
		subType = GetArraySlotRef(types, i);
		KeyToSKey(subKey, subType, &sub, &subSize, &isVariable);
		long padded = (subSize + 1) & ~1;
		if (size + padded > kSKeyDataSize)
		{
			if (!isVariable)
				break;
			long room = kSKeyDataSize - size;
			if (room < 3)
				break;
			subSize = (short) room;
			sub.SetSize((short) (room - 2));
			padded = room;
		}
		memcpy(outKey->Data() + size, &sub, subSize);
		if (subSize & 1)
			outKey->Data()[size + subSize] = 0;
		size += padded;
	}
	outKey->SetSize((short) size);
}


// ROM 0x0034ad4c KeyToSKey__FRC6RefVarT1P4SKeyPsPUc
// A key as an SKey of an index type: a string's characters (no
// terminator; a rich string's plain characters), an int as a long, a
// real as a double, a char as a short, a symbol's name bytes, an array
// of types a multi-key.  outSize is the SKey's size in the key field
// (the fixed size, or header + data), outIsVariable whether the type is
// variable-size.
void
KeyToSKey(RefArg key, RefArg type, SKey* outKey, short* outSize, Boolean* outIsVariable)
{
	Boolean isVariable = false;
	if (IsArray(type))
	{
		MultiKeyToSKey(key, type, outKey);
		isVariable = true;
	}
	else
	{
		if ((ISPTR(key) || !EQRef(type, RSSYMreal)) && !IsInstance(key, type))
			Throw(exStoreError, (void*) kNSErrKeyTypeMismatch, nil);
		if (EQRef(type, RSSYMstring))
		{
			if (IsLargeBinary(key))
				Throw(exStoreError, (void*) kNSErrLargeBinaryAsKey, nil);
			if (IsRichString(key))
				RichStringToSKey(key, outKey);
			else
			{
				UniChar* text = (UniChar*) BinaryData(key);
				long length = Ustrlen(text);
				if (length > (long) (kSKeyDataSize / sizeof(UniChar)))
					length = kSKeyDataSize / sizeof(UniChar);
				UByte* data = outKey->Data();
				for (long i = 0; i < length; i++)
					PutBigEndianHalf(data + i * sizeof(UniChar), text[i]);
				outKey->SetSize((short) (length * sizeof(UniChar)));
			}
			isVariable = true;
		}
		else if (EQRef(type, RSSYMint))
		{
			*outKey = (long) RINT(key);
			if (outSize != nil)
				*outSize = 4;
		}
		else if (EQRef(type, RSSYMreal))
		{
			*outKey = CoerceToDouble(key);
			if (outSize != nil)
				*outSize = 8;
		}
		else if (EQRef(type, RSSYMchar))
		{
			*outKey = (unsigned short) RCHAR(key);
			if (outSize != nil)
				*outSize = 2;
		}
		else if (EQRef(type, RSSYMsymbol))
		{
			const char* name = SymbolName(key);
			outKey->Set(strlen(name), name);
			isVariable = true;
		}
		else
			Throw(exStoreError, (void*) kNSErrBadIndexType, nil);
	}
	if (outIsVariable != nil)
		*outIsVariable = isVariable;
	if (isVariable && outSize != nil)
		*outSize = (short) (outKey->Size() + 2);
}


// ROM 0x0034bbd8 SKeyToKey__FRC4SKeyRC6RefVarPs
// An SKey back as an object of the index type (outSize as KeyToSKey's).
Ref
SKeyToKey(const SKey& key, RefArg type, short* outSize)
{
	short size;
	if (outSize == nil)
		outSize = &size;
	if (EQRef(type, RSSYMstring))
	{
		long length = key.Size();
		RefVar str(AllocateBinary(RSSYMstring, length + sizeof(UniChar)));
		UniChar* text = (UniChar*) BinaryData(str);
		for (long i = 0; i < length / (long) sizeof(UniChar); i++)
			text[i] = GetBigEndianHalf(key.Data() + i * sizeof(UniChar));
		text[length / sizeof(UniChar)] = 0;
		*outSize = (short) (length + 2);
		return str;
	}
	if (EQRef(type, RSSYMint))
	{
		*outSize = 4;
		return MAKEINT((long) key);
	}
	if (EQRef(type, RSSYMreal))
	{
		*outSize = 8;
		return MakeReal((double) key);
	}
	if (EQRef(type, RSSYMchar))
	{
		*outSize = 2;
		return MAKECHAR((unsigned short) key);
	}
	if (EQRef(type, RSSYMsymbol))
	{
		long length = key.Size();
		char* name = new char[length + 1];
		memcpy(name, key.Data(), length);
		name[length] = 0;
		RefVar sym(Intern(name));
		delete[] name;
		*outSize = (short) (length + 2);
		return sym;
	}
	if (IsArray(type))
	{
		unsigned int missing = key.Flags();
		const UByte* p = key.Data();
		const UByte* end = p + key.Size();
		long count = Length(type);
		RefVar keys(AllocateArray(RSSYMarray, count));
		RefVar subType;
		for (long i = 0; i < count && p != end; i++)
		{
			if ((missing & 1) == 0)
			{
				subType = GetArraySlotRef(type, i);
				short subSize;
				SetArraySlotRef(keys, i, SKeyToKey(*(const SKey*) p, subType, &subSize));
				p += (subSize + 1) & ~1;
			}
			missing >>= 1;
		}
		return keys;
	}
	Throw(exStoreError, (void*) kNSErrBadIndexType, nil);
	return NILREF;
}


// ROM 0x0034cc58 GetEntryKey__FRC6RefVarT1
// The entry's value on an index path: an array of paths gives an array
// of values (nil when every one is nil); nil when the entry has no such
// path.
Ref
GetEntryKey(RefArg entry, RefArg path)
{
	if (EQRef(ClassOf(path), RSSYMarray))
	{
		long count = Length(path);
		RefVar keys(AllocateArray(RSSYMarray, count));
		RefVar subPath;
		RefVar key;
		Boolean any = false;
		for (long i = 0; i < count; i++)
		{
			subPath = GetArraySlotRef(path, i);
			key = GetEntryKey(entry, subPath);
			if ((Ref) key != NILREF)
			{
				any = true;
				SetArraySlotRef(keys, i, key);
			}
		}
		return any ? (Ref) keys : NILREF;
	}
	if (!FrameHasPath(entry, path))
		return NILREF;
	return GetFramePath(entry, path);
}


// ROM 0x0034dcf4 GetEntrySKey__FRC6RefVarT1P4SKeyPUc
// The entry's key for an index description; ==> whether it has one.
Boolean
GetEntrySKey(RefArg entry, RefArg indexDesc, SKey* outKey, Boolean* outIsVariable)
{
	RefVar path(GetFrameSlotRef(indexDesc, RSSYMpath));
	RefVar key(GetEntryKey(entry, path));
	if ((Ref) key == NILREF)
		return false;
	RefVar type(GetFrameSlotRef(indexDesc, RSSYMtype));
	short size;
	KeyToSKey(key, type, outKey, &size, outIsVariable);
	return true;
}


// ROM 0x00347ba0 AlterIndexes__FUcRC6RefVarT2Ul
// The entry's keys added to (or deleted from) every index of the soup
// with store object id as their datum (the tags index: the entry's tags
// as bits under the id).
void
AlterIndexes(Boolean add, RefArg soup, RefArg entry, PSSId id)
{
	RefVar persistent(GetFrameSlotRef(soup, RSSYM_proto));
	RefVar indexes(GetFrameSlotRef(persistent, RSSYMindexes));
	RefVar indexDesc;
	for (long i = Length(indexes) - 1; i >= 0; i--)
	{
		indexDesc = GetArraySlotRef(indexes, i);
		TSoupIndex* index = GetSoupIndexObject(soup, (PSSId) RINT(GetFrameSlotRef(indexDesc, RSSYMindex)));
		if (EQRef(GetFrameSlotRef(indexDesc, RSSYMtype), RSSYMtags))
		{
			RefVar entryTags(GetEntryKey(entry, RefVar(GetFrameSlotRef(indexDesc, RSSYMpath))));
			if ((Ref) entryTags != NILREF)
				AlterTagsIndex(add, *index, id, entryTags, soup, RefVar(GetFrameSlotRef(indexDesc, RSSYMtags)));
			continue;
		}
		SKey key;
		memset(&key, 0, sizeof(key));
		if (GetEntrySKey(entry, indexDesc, &key, nil))
		{
			SKey data;
			data = (long) id;
			ThrowIndexError(add ? index->Add(&key, &data) : index->Delete(&key, &data));
		}
	}
}


// ROM 0x003482e8 UpdateIndexes__FRC6RefVarN21UlPUc
// The indexes brought from oldEntry's keys to newEntry's: a key that has
// changed is deleted and added (equal ints/reals/keys are left alone);
// tagsChanged asks for the tags index too and says whether it changed.
// ==> whether any other index changed.
Boolean
UpdateIndexes(RefArg soup, RefArg newEntry, RefArg oldEntry, PSSId id, Boolean* tagsChanged)
{
	RefVar persistent(GetFrameSlotRef(soup, RSSYM_proto));
	RefVar indexes(GetFrameSlotRef(persistent, RSSYMindexes));
	RefVar indexDesc;
	RefVar type;
	Boolean changed = false;
	SKey data;
	data = (long) id;
	for (long i = Length(indexes) - 1; i >= 0; i--)
	{
		indexDesc = GetArraySlotRef(indexes, i);
		type = GetFrameSlotRef(indexDesc, RSSYMtype);
		if (EQRef(type, RSSYMtags))
		{
			if (*tagsChanged)
				*tagsChanged = UpdateTagsIndex(soup, indexDesc, oldEntry, newEntry, id);
			continue;
		}
		SKey newKey;
		SKey oldKey;
		memset(&newKey, 0, sizeof(newKey));
		memset(&oldKey, 0, sizeof(oldKey));
		Boolean hasNew = GetEntrySKey(newEntry, indexDesc, &newKey, nil);
		Boolean hasOld = GetEntrySKey(oldEntry, indexDesc, &oldKey, nil);
		if (!hasNew && !hasOld)
			continue;
		if (hasNew && hasOld)
		{
			if (EQRef(type, RSSYMreal))
			{
				if ((double) newKey == (double) oldKey)
					continue;
			}
			else if (EQRef(type, RSSYMint))
			{
				if ((long) newKey == (long) oldKey)
					continue;
			}
			else if (newKey.Equals(oldKey))
				continue;
		}
		changed = true;
		TSoupIndex* index = GetSoupIndexObject(soup, (PSSId) RINT(GetFrameSlotRef(indexDesc, RSSYMindex)));
		if (hasOld)
			ThrowIndexError(index->Delete(&oldKey, &data));
		if (hasNew)
			ThrowIndexError(index->Add(&newKey, &data));
	}
	return changed;
}


// ROM 0x003485a4 AbortSoupIndexes__FRC6RefVar
// Every index told its store transaction was aborted.
void
AbortSoupIndexes(RefArg soup)
{
	RefVar persistent(GetFrameSlotRef(soup, RSSYM_proto));
	RefVar indexes(GetFrameSlotRef(persistent, RSSYMindexes));
	for (long i = Length(indexes) - 1; i >= 0; i--)
	{
		Ref indexDesc = GetArraySlotRef(indexes, i);
		TSoupIndex* index = GetSoupIndexObject(soup, (PSSId) RINT(GetFrameSlotRef(indexDesc, RSSYMindex)));
		index->StoreAborted();
	}
}


// ROM 0x00348664 PlainSoupGetStore
Ref
PlainSoupGetStore(RefArg rcvr)
{
	return GetFrameSlot(rcvr, RSSYMstoreobj);
}


// ROM 0x00348680 SafeEntryAdd__FRC6RefVarN21Uc
// The entry frame stored (made internal unless verbatim; its _modTime
// set, its _uniqueID given, as the flags say) and put in the indexes;
// a plain frame becomes a fault block (in memory unless verbatim) in
// the soup's cache - one that was an entry of another soup is re-pointed
// at this one, and its old soup keeps a fault block for the old object.
Ref
SafeEntryAdd(RefArg soup, RefArg entry, RefArg uniqueId, int flags)
{
	Boolean verbatim = (flags & kSoupAddVerbatim) != 0;
	RefVar object;
	if (verbatim)
		object = entry;
	else
		object = EnsureEntryInternal(entry);
	if (flags & kSoupAddSetModTime)
		SetFrameSlot(object, RSSYM_modtime, RefVar(MAKEINT(RealClock() & 0x1fffffff)));
	if (flags & kSoupAddSetUniqueID)
		SetFrameSlot(object, RSSYM_uniqueid, uniqueId);
	TStoreWrapper* wrapper = (TStoreWrapper*) GetFrameSlotRef(soup, RSSYMtstore);
	PSSId id = (PSSId) -1;
	Boolean duplicated = false;
	StorePermObject(object, wrapper, id, nil, &duplicated);
	if (duplicated)
		verbatim = true;
	AlterIndexes(true, soup, object, id);
	if (!IsFaultBlock(object))
	{
		RefVar cached(verbatim ? NILREF : Clone(object));
		RefVar fb(MakeFaultBlock(soup, wrapper, id, cached));
		ReplaceObjectRef(object, fb);
		RefVar cache(GetFrameSlotRef(soup, RSSYMcache));
		PutEntryIntoCache(cache, object);
	}
	else
	{
		Ref* slots = FaultBlockSlots(object);
		RefVar oldSoup(slots[kFaultBlockHandlerSlot]);
		TStoreWrapper* oldWrapper = (TStoreWrapper*) slots[kFaultBlockStoreSlot];
		PSSId oldId = (PSSId) RINT(slots[kFaultBlockIdSlot]);
		slots[kFaultBlockHandlerSlot] = soup;
		slots[kFaultBlockStoreSlot] = (Ref) wrapper;
		slots[kFaultBlockIdSlot] = MAKEINT(id);
		if (verbatim)
			slots[kFaultBlockObjectSlot] = NILREF;
		RefVar oldCache(GetFrameSlotRef(oldSoup, RSSYMcache));
		if (!EQRef(oldSoup, soup))
		{
			DeleteEntryFromCache(oldCache, object);
			RefVar cache(GetFrameSlotRef(soup, RSSYMcache));
			PutEntryIntoCache(cache, object);
		}
		RefVar oldFb(MakeFaultBlock(oldSoup, oldWrapper, oldId));
		PutEntryIntoCache(oldCache, oldFb);
		EachSoupCursorDo(oldSoup, kSoupCursorEntryReadded, object, oldFb);
	}
	return object;
}


// ROM 0x00348990 CommonSoupAddEntry__FRC6RefVarT1UcT3
// A frame (not read-only) added to the soup in a store transaction; the
// soup's next _uniqueID and its persistent frame's lastUID kept up.
Ref
CommonSoupAddEntry(RefArg soup, RefArg entry, int flags, Boolean /*unused*/)
{
	ULong entryFlags = ObjectFlags(entry);
	if ((entryFlags & kObjFrame) == 0)
		Throw(exBadType, (void*) kNSErrNotAFrame, nil);
	if (entryFlags & kObjReadOnly)
		ThrowExFramesWithBadValue(kNSErrObjectReadOnly, entry);
	RefVar persistent(SoupPersistent(soup));
	TStoreWrapper* wrapper = (TStoreWrapper*) GetFrameSlotRef(soup, RSSYMtstore);
	CheckWriteProtect(wrapper->Store());
	RefVar added;
	RefVar nextUID(GetFrameSlotRef(soup, RSSYMindexnextuid));
	wrapper->LockStore();
	newton_try
	{
		added = SafeEntryAdd(soup, entry, nextUID, flags);
		long uid = RINT(nextUID);
		Boolean bump = (flags & kSoupAddSetUniqueID) != 0;
		if (!bump)
		{
			long entryUID = RINT(GetFrameSlotRef(entry, RSSYM_uniqueid));
			if (entryUID >= uid)
			{
				bump = true;
				uid = entryUID;
			}
		}
		if (bump)
			SetFrameSlot(soup, RSSYMindexnextuid, RefVar(MAKEINT(uid + 1)));
		if (GetFrameSlotRef(persistent, RSSYMlastuid) == NILREF)
			SoupChanged(persistent, true);
		else
		{
			SetFrameSlot(persistent, RSSYMlastuid, RefVar(NILREF));
			SoupChanged(persistent, false);
			WriteFaultBlock(persistent);
		}
	}
	newton_catch_all
	{
		wrapper->Abort();
		AbortSoupIndexes(soup);
		rethrow;
	}
	end_try;
	wrapper->UnlockStore();
	return added;
}


// ROM 0x00348e0c PlainSoupAdd
// The entry added with its _modTime set and the soup's next _uniqueID.
Ref
PlainSoupAdd(RefArg rcvr, RefArg entry)
{
	return CommonSoupAddEntry(rcvr, entry, kSoupAddSetModTime | kSoupAddSetUniqueID, false);
}


// ROM 0x00348e18 SoupAddFlushed
// The same, the frame written as it is.
Ref
SoupAddFlushed(RefArg rcvr, RefArg entry)
{
	return CommonSoupAddEntry(rcvr, entry, kSoupAddVerbatim | kSoupAddSetModTime | kSoupAddSetUniqueID, false);
}


// ROM 0x00348e24 PlainSoupAddWithUniqueID
// The entry added with the _uniqueID it has.
Ref
PlainSoupAddWithUniqueID(RefArg rcvr, RefArg entry)
{
	return CommonSoupAddEntry(rcvr, entry, 0, false);
}


// ROM 0x00348e30 SoupAddFlushedWithUniqueId
Ref
SoupAddFlushedWithUniqueId(RefArg rcvr, RefArg entry)
{
	return CommonSoupAddEntry(rcvr, entry, kSoupAddVerbatim, false);
}


// ROM 0x00349a78 PlainSoupAddIndex
// An index added: its description made and appended to the soup's, the
// index objects remade and every entry put in.
Ref
PlainSoupAddIndex(RefArg rcvr, RefArg indexSpec)
{
	RefVar persistent(SoupPersistent(rcvr));
	RefVar path(GetFrameSlotRef(indexSpec, RSSYMpath));
	if (IndexPathToIndexDesc(persistent, path, nil) != NILREF)
		return NILREF;
	TStoreWrapper* wrapper = (TStoreWrapper*) GetFrameSlotRef(rcvr, RSSYMtstore);
	CheckWriteProtect(wrapper->Store());
	RefVar indexes(GetFrameSlotRef(persistent, RSSYMindexes));
	long count = Length(indexes);
	wrapper->LockStore();
	newton_try
	{
		RefVar storeObject(GetFrameSlotRef(rcvr, RSSYMstoreobj));
		RefVar indexDesc(NewIndexDesc(persistent, storeObject, indexSpec));
		AddArraySlot(indexes, indexDesc);
		SetFrameSlot(persistent, RSSYMindexesmodtime, RefVar(MAKEINT(RealClock() & 0x1fffffff)));
		SoupChanged(persistent, false);
		WriteFaultBlock(persistent);
		CreateSoupIndexObjects(rcvr);
		IndexEntries(rcvr, indexDesc);
	}
	newton_catch_all
	{
		if (Length(indexes) != count)
			SetLength(indexes, count);
		wrapper->Abort();
		rethrow;
	}
	end_try;
	wrapper->UnlockStore();
	return NILREF;
}


// ROM 0x00349de8 PlainSoupRemoveIndex
// The index on path destroyed and its description taken out (never the
// _uniqueID index's).
Ref
PlainSoupRemoveIndex(RefArg rcvr, RefArg path)
{
	RefVar persistent(SoupPersistent(rcvr));
	if (EQRef(path, RSSYM_uniqueid))
		Throw(exStoreError, (void*) kNSErrCantRemoveUniqueIDIndex, nil);
	long position;
	RefVar indexDesc(IndexPathToIndexDesc(persistent, path, &position));
	if ((Ref) indexDesc == NILREF)
		Throw(exStoreError, (void*) kNSErrIndexNotFound, nil);
	TStoreWrapper* wrapper = (TStoreWrapper*) GetFrameSlotRef(rcvr, RSSYMtstore);
	CheckWriteProtect(wrapper->Store());
	PSSId infoId = (PSSId) RINT(GetFrameSlotRef(indexDesc, RSSYMindex));
	wrapper->LockStore();
	TSoupIndex* index = GetSoupIndexObject(rcvr, infoId);
	newton_try
	{
		index->Destroy();
		wrapper->Store()->DeleteObject(infoId);
		RefVar indexes(GetFrameSlotRef(persistent, RSSYMindexes));
		RefVar none;
		ArrayMunger(indexes, position, 1, none, 0, 0);
		SetFrameSlot(persistent, RSSYMindexesmodtime, RefVar(MAKEINT(RealClock() & 0x1fffffff)));
		SoupChanged(persistent, false);
		WriteFaultBlock(persistent);
		RefVar sortId(GetFrameSlotRef(indexDesc, RSSYMsortid));
		if ((Ref) sortId != NILREF)
			StoreRemoveSortTable(RefVar(GetFrameSlotRef(rcvr, RSSYMstoreobj)), RINT(sortId));
	}
	newton_catch_all
	{
		wrapper->Abort();
		index->StoreAborted();
		rethrow;
	}
	end_try;
	wrapper->UnlockStore();
	EachSoupCursorDo(rcvr, kSoupCursorIndexRemoved, indexDesc);
	CreateSoupIndexObjects(rcvr);
	return NILREF;
}


// ROM 0x0034b164 PlainSoupSetName
// The soup renamed: its name index entry replaced, its union soup
// membership moved (NOT YET), theName set.
Ref
PlainSoupSetName(RefArg rcvr, RefArg name)
{
	CheckSoupName(name);
	RefVar oldName(GetFrameSlotRef(rcvr, RSSYMthename));
	if (CompareStringNoCase(GetCString(name), GetCString(oldName)) == 0)
		return name;
	RefVar storeObject(PlainSoupGetStore(rcvr));
	if (StoreHasSoup(storeObject, name) != NILREF)
		Throw(exStoreError, (void*) kNSErrDuplicateSoupName, nil);
	RefVar persistent(GetFrameSlotRef(rcvr, RSSYM_proto));
	TStoreWrapper* wrapper = FaultBlockStore(persistent);
	PSSId id = FaultBlockId(persistent);
	CheckWriteProtect(wrapper->Store());
	OSErrIf(wrapper->LockStore());
	newton_try
	{
		TSoupIndex index;
		InitNameIndex(&index, storeObject);
		SKey key;
		SKey data;
		SoupNameKey(name, &key);
		data = (long) id;
		ThrowIndexError(index.Add(&key, &data));
		SoupNameKey(oldName, &key);
		data = (long) id;
		ThrowIndexError(index.Delete(&key, &data));
	}
	newton_catch_all
	{
		OSErrIf(wrapper->Abort());
		rethrow;
	}
	end_try;
	OSErrIf(wrapper->UnlockStore());
	RemoveFromUnionSoup(oldName, rcvr);
	RefVar theName(TotalClone(name));
	SetClass(theName, RSSYMstring_2Enohint);
	SetFrameSlot(rcvr, RSSYMthename, theName);
	AddToUnionSoup(name, rcvr);
	return name;
}


// ROM 0x0034b594 PlainSoupGetSignature
Ref
PlainSoupGetSignature(RefArg rcvr)
{
	RefVar persistent(SoupPersistent(rcvr));
	return GetFrameSlotRef(persistent, RSSYMsignature);
}


// ROM 0x0034b618 PlainSoupSetSignature
Ref
PlainSoupSetSignature(RefArg rcvr, RefArg signature)
{
	RefVar persistent(SoupPersistent(rcvr));
	CheckWriteProtect(RefVar(PlainSoupGetStore(rcvr)));
	SetFrameSlot(persistent, RSSYMsignature, signature);
	WriteFaultBlock(persistent);
	return signature;
}


// ROM 0x0034b6d4 PlainSoupGetNextUID
Ref
PlainSoupGetNextUID(RefArg rcvr)
{
	return GetFrameSlot(rcvr, RSSYMindexnextuid);
}


// ROM 0x0034b6f0 PlainSoupGetInfo
Ref
PlainSoupGetInfo(RefArg rcvr, RefArg tag)
{
	RefVar persistent(SoupPersistent(rcvr));
	if (!FrameHasSlotRef(persistent, RSSYMinfo))
		return NILREF;
	return GetFrameSlotRef(GetFrameSlotRef(persistent, RSSYMinfo), tag);
}


// ROM 0x0034b7ac PlainSoupSetInfo
// (setting NCKLastBackupTime leaves the info mod time alone)
Ref
PlainSoupSetInfo(RefArg rcvr, RefArg tag, RefArg value)
{
	RefVar persistent(SoupPersistent(rcvr));
	CheckWriteProtect(RefVar(PlainSoupGetStore(rcvr)));
	RefVar theValue(TotalClone(value));
	RefVar theTag(EnsureInternal(tag));
	if (!FrameHasSlotRef(persistent, RSSYMinfo))
	{
		RefVar info(AllocateFrame());
		SetFrameSlot(info, theTag, theValue);
		SetFrameSlot(persistent, RSSYMinfo, info);
	}
	else
	{
		RefVar info(GetFrameSlotRef(persistent, RSSYMinfo));
		SetFrameSlot(info, theTag, theValue);
	}
	if (!EQRef(tag, RSSYMncklastbackuptime))
		SetFrameSlot(persistent, RSSYMinfomodtime, RefVar(MAKEINT(RealClock() & 0x1fffffff)));
	SoupChanged(persistent, false);
	WriteFaultBlock(persistent);
	return NILREF;
}


// ROM 0x0034b980 PlainSoupGetAllInfo
Ref
PlainSoupGetAllInfo(RefArg rcvr)
{
	RefVar persistent(SoupPersistent(rcvr));
	RefVar info(GetFrameSlotRef(persistent, RSSYMinfo));
	return Clone(info);
}


// ROM 0x0034ba20 PlainSoupSetAllInfo
Ref
PlainSoupSetAllInfo(RefArg rcvr, RefArg info)
{
	if (!IsFrame(info))
		Throw(exBadType, (void*) kNSErrNotAFrame, nil);
	RefVar persistent(SoupPersistent(rcvr));
	CheckWriteProtect(RefVar(PlainSoupGetStore(rcvr)));
	SetFrameSlot(persistent, RSSYMinfo, RefVar(TotalClone(info)));
	SetFrameSlot(persistent, RSSYMinfomodtime, RefVar(MAKEINT(RealClock() & 0x1fffffff)));
	SoupChanged(persistent, false);
	WriteFaultBlock(persistent);
	return NILREF;
}


// ROM 0x0034bb54 SoupGetFlags
Ref
SoupGetFlags(RefArg rcvr)
{
	RefVar persistent(SoupPersistent(rcvr));
	return GetFrameSlotRef(persistent, RSSYMflags);
}


// ROM 0x0034bf18 SoupSetFlags
Ref
SoupSetFlags(RefArg rcvr, RefArg flags)
{
	RefVar persistent(SoupPersistent(rcvr));
	CheckWriteProtect(RefVar(PlainSoupGetStore(rcvr)));
	SetFrameSlot(persistent, RSSYMflags, flags);
	WriteFaultBlock(persistent);
	return NILREF;
}


// ROM 0x0034bfcc SoupCacheRemoveAllEntries__FRC6RefVar
// Every cached entry becomes its plain frame (read for the purpose when
// not in memory) and leaves the cache.
void
SoupCacheRemoveAllEntries(RefArg soup)
{
	RefVar cache(GetFrameSlotRef(soup, RSSYMcache));
	TStoreWrapper* wrapper = (TStoreWrapper*) GetFrameSlotRef(GetFrameSlotRef(soup, RSSYMstoreobj), RSSYMstore);
	RefVar entry;
	RefVar object;
	for (long i = Length(cache) - 1; i >= 0; i--)
	{
		entry = GetArraySlotRef(cache, i);
		if ((Ref) entry != NILREF)
		{
			PSSId id = FaultBlockId(entry);
			object = FaultBlockObject(entry);
			SetArraySlotRef(cache, i, NILREF);
			if ((Ref) object == NILREF)
				object = LoadPermObject(wrapper, id, nil);
			ReplaceObjectRef(entry, object);
		}
	}
}


// ROM 0x0034c11c RemoveEntryStopFn__FP4SKeyT1Pv
static int
RemoveEntryStopFn(SKey* /*key*/, SKey* data, void* refCon)
{
	DeletePermObject((TStoreWrapper*) refCon, (PSSId) (long) *data);
	return 0;
}


// ROM 0x0034c148 PlainSoupRemoveAllEntries
// Every entry's store object deleted (walking the _uniqueID index) and
// every index destroyed; the cached entries become plain frames.
Ref
PlainSoupRemoveAllEntries(RefArg rcvr)
{
	RefVar persistent(SoupPersistent(rcvr));
	TStoreWrapper* wrapper = (TStoreWrapper*) GetFrameSlotRef(GetFrameSlotRef(rcvr, RSSYMstoreobj), RSSYMstore);
	CheckWriteProtect(wrapper->Store());
	RefVar name(GetFrameSlotRef(rcvr, RSSYMthename));
	RemoveFromUnionSoup(name, rcvr);
	SoupCacheRemoveAllEntries(rcvr);
	wrapper->LockStore();
	newton_try
	{
		TSoupIndex* uniqueIdIndex = GetSoupIndexObject(rcvr, 0);
		int result = uniqueIdIndex->Search(true, nil, nil, RemoveEntryStopFn, wrapper, nil, nil);
		if (result < 0)
			Throw(exStoreError, (void*) (Long) result, nil);
		RefVar indexes(GetFrameSlotRef(persistent, RSSYMindexes));
		long count = Length(indexes);
		for (long i = 0; i < count; i++)
		{
			Ref indexDesc = GetArraySlotRef(indexes, i);
			GetSoupIndexObject(rcvr, (PSSId) RINT(GetFrameSlotRef(indexDesc, RSSYMindex)))->Destroy();
		}
		AddToUnionSoup(name, rcvr);
		SetFrameSlot(persistent, RSSYMlastuid, RefVar(GetFrameSlotRef(rcvr, RSSYMindexnextuid)));
		SoupChanged(persistent, false);
		WriteFaultBlock(persistent);
	}
	newton_catch_all
	{
		wrapper->Abort();
		AbortSoupIndexes(rcvr);
		rethrow;
	}
	end_try;
	wrapper->UnlockStore();
	return NILREF;
}


// ROM 0x0034c3f0 PlainSoupRemoveFromStore
// The soup gone from its store: its entries, its indexes' info objects,
// its name index entry and its persistent frame; the soup frame killed.
Ref
PlainSoupRemoveFromStore(RefArg rcvr)
{
	RefVar persistent(SoupPersistent(rcvr));
	TStoreWrapper* wrapper = FaultBlockStore(persistent);
	PSSId id = FaultBlockId(persistent);
	CheckWriteProtect(wrapper->Store());
	RefVar name(GetFrameSlotRef(rcvr, RSSYMthename));
	PlainSoupRemoveAllEntries(rcvr);
	RemoveFromUnionSoup(name, rcvr);
	EachSoupCursorDo(rcvr, kSoupCursorSoupRemoved, rcvr);
	OSErrIf(wrapper->LockStore());
	newton_try
	{
		RefVar storeObject(GetFrameSlotRef(rcvr, RSSYMstoreobj));
		RefVar indexes(GetFrameSlotRef(persistent, RSSYMindexes));
		RefVar indexDesc;
		RefVar sortId;
		long count = Length(indexes);
		for (long i = 0; i < count; i++)
		{
			indexDesc = GetArraySlotRef(indexes, i);
			wrapper->Store()->DeleteObject((PSSId) RINT(GetFrameSlotRef(indexDesc, RSSYMindex)));
			sortId = GetFrameSlotRef(indexDesc, RSSYMsortid);
			if ((Ref) sortId != NILREF)
				StoreRemoveSortTable(storeObject, RINT(sortId));
		}
		TSoupIndex index;
		InitNameIndex(&index, storeObject);
		SKey key;
		SKey data;
		SoupNameKey(name, &key);
		data = (long) id;
		ThrowIndexError(index.Delete(&key, &data));
		SetFrameSlot(rcvr, RSSYM_proto, RefVar(NILREF));
		RefVar soups(GetFrameSlotRef(storeObject, RSSYMsoups));
		DeleteEntryFromCache(soups, rcvr);
		DeletePermObject(wrapper, id);
	}
	newton_catch_all
	{
		OSErrIf(wrapper->Abort());
		rethrow;
	}
	end_try;
	OSErrIf(wrapper->UnlockStore());
	return NILREF;
}


// ROM 0x0034c7f8 PlainSoupDirty__FRC6RefVar
Ref
PlainSoupDirty(RefArg rcvr)
{
	SetFrameSlot(rcvr, RSSYMdirty, RefVar(TRUEREF));
	((TStoreWrapper*) GetFrameSlotRef(rcvr, RSSYMtstore))->Dirty();
	return NILREF;
}


// ROM 0x0034c85c PlainSoupFlush
// A dirty soup's dirty cached entries written; ==> whether any was.
Ref
PlainSoupFlush(RefArg rcvr)
{
	Ref result = NILREF;
	if (GetFrameSlotRef(rcvr, RSSYMdirty) != NILREF)
	{
		RefVar cache(GetFrameSlotRef(rcvr, RSSYMcache));
		RefVar entry;
		long count = Length(cache);
		for (long i = 0; i < count; i++)
		{
			entry = GetArraySlotRef(cache, i);
			if ((Ref) entry != NILREF && EntryDirty(entry))
			{
				EntryChange(entry);
				result = TRUEREF;
			}
		}
		SetFrameSlot(rcvr, RSSYMdirty, RefVar(NILREF));
	}
	return result;
}


// ROM 0x0034c980 GetSizeStopFn__FP4SKeyT1Pv
struct SoupSizeInfo
{
	TStoreWrapper*	fWrapper;
	long			fSize;
};

static int
GetSizeStopFn(SKey* /*key*/, SKey* data, void* refCon)
{
	SoupSizeInfo* info = (SoupSizeInfo*) refCon;
	info->fSize += EntrySize((PSSId) (long) *data, info->fWrapper, true);
	return 0;
}


// ROM 0x0034c9b8 PlainSoupGetSize
// The entries' store objects and the indexes' sizes.
Ref
PlainSoupGetSize(RefArg rcvr)
{
	SoupPersistent(rcvr);
	SoupSizeInfo info;
	info.fWrapper = (TStoreWrapper*) GetFrameSlotRef(rcvr, RSSYMtstore);
	info.fSize = 0;
	int result = GetSoupIndexObject(rcvr, 0)->Search(true, nil, nil, GetSizeStopFn, &info, nil, nil);
	if (result < 0)
		Throw(exStoreError, (void*) (Long) result, nil);
	RefVar sizes(PlainSoupIndexSizes(rcvr));
	for (long i = Length(sizes) - 1; i >= 0; i--)
		info.fSize += RINT(GetArraySlotRef(sizes, i));
	return MAKEINT(info.fSize);
}


// ROM 0x0034cb00 PlainSoupIndexSizes
Ref
PlainSoupIndexSizes(RefArg rcvr)
{
	RefVar persistent(SoupPersistent(rcvr));
	RefVar indexes(GetFrameSlotRef(persistent, RSSYMindexes));
	long count = Length(indexes);
	RefVar sizes(AllocateArray(RSSYMarray, count));
	for (long i = 0; i < count; i++)
	{
		Ref indexDesc = GetArraySlotRef(indexes, i);
		TSoupIndex* index = GetSoupIndexObject(rcvr, (PSSId) RINT(GetFrameSlotRef(indexDesc, RSSYMindex)));
		SetArraySlotRef(sizes, i, MAKEINT(index->TotalSize()));
	}
	return sizes;
}


// ROM 0x0034db08 PlainSoupGetIndexes
// Clones of the index descriptions but the _uniqueID index's.
Ref
PlainSoupGetIndexes(RefArg rcvr)
{
	RefVar persistent(SoupPersistent(rcvr));
	RefVar indexes(GetFrameSlotRef(persistent, RSSYMindexes));
	long count = Length(indexes);
	RefVar result(AllocateArray(RSSYMarray, count - 1));
	RefVar indexDesc;
	long n = 0;
	for (long i = 0; i < count; i++)
	{
		indexDesc = GetArraySlotRef(indexes, i);
		if (!EQRef(GetFrameSlotRef(indexDesc, RSSYMpath), RSSYM_uniqueid))
		{
			indexDesc = Clone(indexDesc);
			SetArraySlotRef(result, n++, indexDesc);
		}
	}
	return result;
}


// ROM 0x0034e514 PlainSoupMakeKey
// A key as the index on path would keep it.
Ref
PlainSoupMakeKey(RefArg rcvr, RefArg key, RefArg path)
{
	RefVar persistent(SoupPersistent(rcvr));
	RefVar indexDesc(IndexPathToIndexDesc(persistent, path, nil));
	if ((Ref) indexDesc == NILREF)
		Throw(exStoreError, (void*) kNSErrIndexNotFound, nil);
	RefVar type(GetFrameSlotRef(indexDesc, RSSYMtype));
	SKey skey;
	memset(&skey, 0, sizeof(skey));
	KeyToSKey(key, type, &skey, nil, nil);
	return SKeyToKey(skey, type, nil);
}


// ROM 0x0034dc68 SoupIsValid
Ref
SoupIsValid(RefArg rcvr)
{
	if (GetFrameSlotRef(rcvr, RSSYM_proto) == NILREF)
		return NILREF;
	RefVar storeObject(GetFrameSlotRef(rcvr, RSSYMstoreobj));
	return StoreIsValid(storeObject);
}


// ROM 0x0034ddd8 SoupGetIndexesModTime
Ref
SoupGetIndexesModTime(RefArg rcvr)
{
	RefVar persistent(SoupPersistent(rcvr));
	return GetFrameSlotRef(persistent, RSSYMindexesmodtime);
}


// ROM 0x0034de5c SoupGetInfoModTime
Ref
SoupGetInfoModTime(RefArg rcvr)
{
	RefVar persistent(SoupPersistent(rcvr));
	return GetFrameSlotRef(persistent, RSSYMinfomodtime);
}


// ROM 0x0034dee0 CommonSoupGetName
Ref
CommonSoupGetName(RefArg rcvr)
{
	return GetFrameSlot(rcvr, RSSYMthename);
}
