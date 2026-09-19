/*
	File:		stores/SoupNatives.cpp

	Contains:	The NewtonScript functions over stores, soups and entries
				(GetStores, EntryChange, ...), the entry aliases, and the
				binding of every store, soup and entry native to the ROM's
				function objects (RegisterSoupNatives).

	Reconstructed from the MP2x00 US ROM (0x002b5c74-0x002b60b8,
	0x0034e128-0x0034e7d0); each function cites its origin.
*/

#include "Soups.h"
#include "Tags.h"
#include "Frames.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "NSErrors.h"
#include "NewtonExceptions.h"

extern const ExceptionName exStoreError;


/*------------------------------------------------------------------------------
	E n t r y   a l i a s e s
	An alias is an array of class 'alias: [nil, the soup's signature, the
	entry's _uniqueID, the soup's name]; it finds the entry again in any
	store.
------------------------------------------------------------------------------*/

// ROM 0x0034e128 MakeEntryAlias__FRC6RefVar
Ref
MakeEntryAlias(RefArg entry)
{
	RefVar soup(EntrySoup(entry));
	// the ROM notes here whether the internal store's signatures are wanted (gNeedsInternalSignatures): NOT YET
	RefVar alias(AllocateArray(RSSYMalias, 4));
	SetArraySlotRef(alias, 1, SoupGetSignature(soup));
	SetArraySlotRef(alias, 2, MAKEINT(EntryUniqueID(entry)));
	SetArraySlotRef(alias, 3, CommonSoupGetName(soup));
	return alias;
}


// ROM 0x0034e258 IsEntryAlias__FRC6RefVar
Boolean
IsEntryAlias(RefArg object)
{
	return !IsFaultBlock(object) && EQRef(ClassOf(object), RSSYMalias);
}


// ROM 0x0034e2b0 ResolveEntryAliasInStores__FRC6RefVarT1
// The entry the alias names in one of the stores (frames): the soup of
// that name whose signature matches, its entry of that _uniqueID.
Ref
ResolveEntryAliasInStores(RefArg alias, RefArg stores)
{
	if ((Ref) stores == NILREF)
		return NILREF;
	RefVar signature(GetArraySlotRef(alias, 1));
	RefVar soup;
	RefVar name;
	RefVar storeObject;
	for (long i = Length(stores) - 1; i >= 0; i--)
	{
		name = GetArraySlotRef(alias, 3);
		storeObject = GetArraySlotRef(stores, i);
		soup = StoreGetSoup(storeObject, name);
		if ((Ref) soup == NILREF)
			continue;
		if (!EQRef(signature, SoupGetSignature(soup)))
			continue;
		SKey key;
		SKey data;
		key = (long) RINT(GetArraySlotRef(alias, 2));
		if (GetSoupIndexObject(soup, 0)->Find(&key, nil, &data, false) == kIndexOK)
			return GetEntry(soup, (PSSId) (long) data);
		return NILREF;
	}
	return NILREF;
}


// ROM 0x0034e480 ResolveEntryAlias__FRC6RefVar
Ref
ResolveEntryAlias(RefArg alias)
{
	if (!IsEntryAlias(alias))
		return NILREF;
	RefVar entry(ResolveEntryAliasInStores(alias, RefVar(gStores)));
	if ((Ref) entry == NILREF)
		entry = ResolveEntryAliasInStores(alias, RefVar(gPackageStores));
	return entry;
}


// ROM 0x0034e610 CompareAliasAndEntry__FRC6RefVarT1
Boolean
CompareAliasAndEntry(RefArg alias, RefArg entry)
{
	if (!IsSoupEntry(entry))
		return false;
	if (RINT(GetArraySlotRef(alias, 2)) != EntryUniqueID(entry))
		return false;
	RefVar soup(EntrySoup(entry));
	return EQRef(GetArraySlotRef(alias, 1), SoupGetSignature(soup));
}


// ROM 0x0034e6fc IsSameEntry__FRC6RefVarT1
// Two entries, or aliases, or one of each, for the same entry.
Boolean
IsSameEntry(RefArg a, RefArg b)
{
	Boolean aIsAlias = IsEntryAlias(a);
	Boolean bIsAlias = IsEntryAlias(b);
	if (!aIsAlias)
	{
		if (bIsAlias)
			return CompareAliasAndEntry(b, a);
		return EQRef(a, b);
	}
	if (!bIsAlias)
		return CompareAliasAndEntry(a, b);
	return EQRef(GetArraySlotRef(a, 2), GetArraySlotRef(b, 2))
		&& EQRef(GetArraySlotRef(a, 1), GetArraySlotRef(b, 1));
}


/*------------------------------------------------------------------------------
	T h e   n a t i v e s
------------------------------------------------------------------------------*/

// ROM 0x002b5cf0 FGetStores
static Ref
FGetStores(RefArg /*rcvr*/)
{
	return gStores;
}


// ROM 0x002b5c74 FQuery
static Ref
FQuery(RefArg /*rcvr*/, RefArg soup, RefArg querySpec)
{
	return SoupQuery(soup, querySpec);
}


// ROM 0x002b5cf4 FIsSoupEntry
static Ref
FIsSoupEntry(RefArg /*rcvr*/, RefArg object)
{
	return MAKEBOOLEAN(IsSoupEntry(object));
}


// ROM 0x002b5d18 FEntryIsResident
static Ref
FEntryIsResident(RefArg /*rcvr*/, RefArg entry)
{
	return MAKEBOOLEAN(EntryIsResident(entry));
}


// ROM 0x002b5d40 FEntryValid
static Ref
FEntryValid(RefArg /*rcvr*/, RefArg entry)
{
	return MAKEBOOLEAN(EntryValid(entry));
}


// ROM 0x002b5d64 FEntrySoup
static Ref
FEntrySoup(RefArg /*rcvr*/, RefArg entry)
{
	return EntrySoup(entry);
}


// ROM 0x002b5d6c FEntryStore
static Ref
FEntryStore(RefArg /*rcvr*/, RefArg entry)
{
	return EntryStore(entry);
}


// ROM 0x002b5d74 FEntrySize
static Ref
FEntrySize(RefArg /*rcvr*/, RefArg entry)
{
	return MAKEINT(EntrySize(entry));
}


// ROM 0x002b5d90 FEntrySizeWithoutVBOs
static Ref
FEntrySizeWithoutVBOs(RefArg /*rcvr*/, RefArg entry)
{
	return MAKEINT(EntrySizeWithoutVBOs(entry));
}


// ROM 0x002b5de4 FEntryTextSize
static Ref
FEntryTextSize(RefArg /*rcvr*/, RefArg entry)
{
	return MAKEINT(EntryTextSize(entry));
}


// ROM 0x002b5e00 FEntryUniqueID
static Ref
FEntryUniqueID(RefArg /*rcvr*/, RefArg entry)
{
	return MAKEINT(EntryUniqueID(entry));
}


// ROM 0x002b5e1c FEntryModTime
static Ref
FEntryModTime(RefArg /*rcvr*/, RefArg entry)
{
	return MAKEINT(EntryModTime(entry));
}


// ROM 0x002b5e38 FEntryChange
static Ref
FEntryChange(RefArg /*rcvr*/, RefArg entry)
{
	EntryChange(entry);
	return NILREF;
}


// ROM 0x002b5e54 FEntryFlush
static Ref
FEntryFlush(RefArg /*rcvr*/, RefArg entry)
{
	EntryFlush(entry);
	return NILREF;
}


// ROM 0x002b5e70 FEntryChangeWithModTime
static Ref
FEntryChangeWithModTime(RefArg /*rcvr*/, RefArg entry)
{
	EntryChangeWithModTime(entry);
	return NILREF;
}


// ROM 0x002b5e8c FEntryChangeVerbatim
static Ref
FEntryChangeVerbatim(RefArg /*rcvr*/, RefArg entry)
{
	EntryChangeVerbatim(entry);
	return NILREF;
}


// ROM 0x002b5ea8 FEntryUndoChanges
static Ref
FEntryUndoChanges(RefArg /*rcvr*/, RefArg entry)
{
	EntryUndoChanges(entry);
	return NILREF;
}


// ROM 0x002b5ec4 FEntryRemoveFromSoup
static Ref
FEntryRemoveFromSoup(RefArg /*rcvr*/, RefArg entry)
{
	EntryRemoveFromSoup(entry);
	return NILREF;
}


// ROM 0x002b5ee0 FEntryReplace
static Ref
FEntryReplace(RefArg /*rcvr*/, RefArg entry, RefArg newEntry)
{
	EntryReplace(entry, newEntry);
	return NILREF;
}


// ROM 0x002b5f50 FEntryReplaceWithModTime
static Ref
FEntryReplaceWithModTime(RefArg /*rcvr*/, RefArg entry, RefArg newEntry)
{
	EntryReplaceWithModTime(entry, newEntry);
	return NILREF;
}


// ROM 0x002b5f70 FEntryCopy
static Ref
FEntryCopy(RefArg /*rcvr*/, RefArg entry, RefArg soup)
{
	return EntryCopy(entry, soup);
}


// ROM 0x002b5f7c FEntryMove
static Ref
FEntryMove(RefArg /*rcvr*/, RefArg entry, RefArg soup)
{
	EntryMove(entry, soup);
	return NILREF;
}


// ROM 0x002b5f9c FNewProxyEntry
static Ref
FNewProxyEntry(RefArg /*rcvr*/, RefArg handler, RefArg object)
{
	return NewProxyEntry(handler, object);
}


// ROM 0x002b5fa8 FIsProxyEntry
static Ref
FIsProxyEntry(RefArg /*rcvr*/, RefArg entry)
{
	return MAKEBOOLEAN(IsFaultBlock(entry) && IsProxyEntry(entry));
}


// ROM 0x002b5fcc FEntrySetHandler
static Ref
FEntrySetHandler(RefArg /*rcvr*/, RefArg entry, RefArg handler)
{
	EntrySetHandler(entry, handler);
	return NILREF;
}


// ROM 0x002b5fec FEntryHandler
static Ref
FEntryHandler(RefArg /*rcvr*/, RefArg entry)
{
	return EntryHandler(entry);
}


// ROM 0x002b5ff4 FEntrySetCachedObject
static Ref
FEntrySetCachedObject(RefArg /*rcvr*/, RefArg entry, RefArg object)
{
	EntrySetCachedObject(entry, object);
	return NILREF;
}


// ROM 0x002b6014 FEntryCachedObject
static Ref
FEntryCachedObject(RefArg /*rcvr*/, RefArg entry)
{
	return EntryCachedObject(entry);
}


// ROM 0x002b601c FMakeEntryAlias
static Ref
FMakeEntryAlias(RefArg /*rcvr*/, RefArg entry)
{
	return MakeEntryAlias(entry);
}


// ROM 0x002b6064 FIsEntryAlias
static Ref
FIsEntryAlias(RefArg /*rcvr*/, RefArg object)
{
	return MAKEBOOLEAN(IsEntryAlias(object));
}


// ROM 0x002b6088 FResolveEntryAlias
static Ref
FResolveEntryAlias(RefArg /*rcvr*/, RefArg alias)
{
	return ResolveEntryAlias(alias);
}


// ROM 0x002b6090 FIsSameEntry
static Ref
FIsSameEntry(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	return MAKEBOOLEAN(IsSameEntry(a, b));
}


// The host implementations bound to the ROM's function objects: the
// built-ins by their F symbols, the store and soup prototypes' methods by
// theirs (ROMNatives.cpp lists both).
// ROM 0x0016059c FGetPackageStore
// GetPackageStore(name): the package store of that name (the stores a
// package's soup part is read through, gPackageStores), compared
// without case; nil when there is none.
static Ref
FGetPackageStore(RefArg /*rcvr*/, RefArg name)
{
	RefVar stores(gPackageStores);
	long count = Length(stores);
	for (long i = 0; i < count; i++)
	{
		RefVar store(GetArraySlotRef(stores, i));
		RefVar storeName(StoreGetName(store));
		if (CompareStringNoCase(GetCString(name), GetCString(storeName)) == 0)
			return store;
	}
	return NILREF;
}


void
RegisterSoupNatives(void)
{
	RegisterNativeFunction("FIsValid", (void*) FIsValid, 1);
	RegisterNativeFunction("FGetStores", (void*) FGetStores, 0);
	RegisterNativeFunction("FGetPackageStore", (void*) FGetPackageStore, 1);
	RegisterNativeFunction("FQuery", (void*) FQuery, 2);
	RegisterNativeFunction("FIsSoupEntry", (void*) FIsSoupEntry, 1);
	RegisterNativeFunction("FEntryIsResident", (void*) FEntryIsResident, 1);
	RegisterNativeFunction("FEntryValid", (void*) FEntryValid, 1);
	RegisterNativeFunction("FEntrySoup", (void*) FEntrySoup, 1);
	RegisterNativeFunction("FEntryStore", (void*) FEntryStore, 1);
	RegisterNativeFunction("FEntrySize", (void*) FEntrySize, 1);
	RegisterNativeFunction("FEntrySizeWithoutVBOs", (void*) FEntrySizeWithoutVBOs, 1);
	RegisterNativeFunction("FEntryTextSize", (void*) FEntryTextSize, 1);
	RegisterNativeFunction("FEntryUniqueID", (void*) FEntryUniqueID, 1);
	RegisterNativeFunction("FEntryModTime", (void*) FEntryModTime, 1);
	RegisterNativeFunction("FEntryChange", (void*) FEntryChange, 1);
	RegisterNativeFunction("FEntryFlush", (void*) FEntryFlush, 1);
	RegisterNativeFunction("FEntryChangeWithModTime", (void*) FEntryChangeWithModTime, 1);
	RegisterNativeFunction("FEntryChangeVerbatim", (void*) FEntryChangeVerbatim, 1);
	RegisterNativeFunction("FEntryUndoChanges", (void*) FEntryUndoChanges, 1);
	RegisterNativeFunction("FEntryRemoveFromSoup", (void*) FEntryRemoveFromSoup, 1);
	RegisterNativeFunction("FEntryReplace", (void*) FEntryReplace, 2);
	RegisterNativeFunction("FEntryReplaceWithModTime", (void*) FEntryReplaceWithModTime, 2);
	RegisterNativeFunction("FEntryCopy", (void*) FEntryCopy, 2);
	RegisterNativeFunction("FEntryMove", (void*) FEntryMove, 2);
	RegisterNativeFunction("FNewProxyEntry", (void*) FNewProxyEntry, 2);
	RegisterNativeFunction("FIsProxyEntry", (void*) FIsProxyEntry, 1);
	RegisterNativeFunction("FEntrySetHandler", (void*) FEntrySetHandler, 2);
	RegisterNativeFunction("FEntryHandler", (void*) FEntryHandler, 1);
	RegisterNativeFunction("FEntrySetCachedObject", (void*) FEntrySetCachedObject, 2);
	RegisterNativeFunction("FEntryCachedObject", (void*) FEntryCachedObject, 1);
	RegisterNativeFunction("FMakeEntryAlias", (void*) FMakeEntryAlias, 1);
	RegisterNativeFunction("FIsEntryAlias", (void*) FIsEntryAlias, 1);
	RegisterNativeFunction("FResolveEntryAlias", (void*) FResolveEntryAlias, 1);
	RegisterNativeFunction("FIsSameEntry", (void*) FIsSameEntry, 2);

	// the store prototype's methods
	RegisterNativeFunction("StoreGetName", (void*) StoreGetName, 0);
	RegisterNativeFunction("StoreSetName", (void*) StoreSetName, 1);
	RegisterNativeFunction("StoreGetKind", (void*) StoreGetKind, 0);
	RegisterNativeFunction("StoreGetSignature", (void*) StoreGetSignature, 0);
	RegisterNativeFunction("StoreSetSignature", (void*) StoreSetSignature, 1);
	RegisterNativeFunction("StoreGetInfo", (void*) StoreGetInfo, 1);
	RegisterNativeFunction("StoreSetInfo", (void*) StoreSetInfo, 2);
	RegisterNativeFunction("StoreGetAllInfo", (void*) StoreGetAllInfo, 0);
	RegisterNativeFunction("StoreSetAllInfo", (void*) StoreSetAllInfo, 1);
	RegisterNativeFunction("StoreGetSoup", (void*) StoreGetSoup, 1);
	RegisterNativeFunction("StoreHasSoup", (void*) StoreHasSoup, 1);
	RegisterNativeFunction("StoreCreateSoup", (void*) StoreCreateSoup, 2);
	RegisterNativeFunction("StoreGetSoupNames", (void*) StoreGetSoupNames, 0);
	RegisterNativeFunction("StoreTotalSize", (void*) StoreTotalSize, 0);
	RegisterNativeFunction("StoreUsedSize", (void*) StoreUsedSize, 0);
	RegisterNativeFunction("StoreIsReadOnly", (void*) StoreIsReadOnly, 0);
	RegisterNativeFunction("StoreIsValid", (void*) StoreIsValid, 0);
	RegisterNativeFunction("StoreLock", (void*) StoreLock, 0);
	RegisterNativeFunction("StoreUnlock", (void*) StoreUnlock, 0);
	RegisterNativeFunction("FStoreAbort", (void*) StoreAbort, 0);
	RegisterNativeFunction("StoreErase", (void*) StoreErase, 0);
	RegisterNativeFunction("StoreCheckWriteProtect", (void*) StoreCheckWriteProtect, 0);
	RegisterNativeFunction("FReadStoreObject", (void*) StoreReadObject, 1);
	RegisterNativeFunction("FWriteStoreObject", (void*) StoreWriteObject, 3);
	RegisterNativeFunction("FWriteEntireStoreObject", (void*) StoreWriteWholeObject, 2);
	RegisterNativeFunction("FNewStoreObject", (void*) StoreNewObject, 1);
	RegisterNativeFunction("FDeleteStoreObject", (void*) StoreDeleteObject, 1);
	RegisterNativeFunction("FSetStoreObjectSize", (void*) StoreSetObjectSize, 2);
	RegisterNativeFunction("FGetStoreObjectSize", (void*) StoreGetObjectSize, 1);

	// the plain soup prototype's methods
	RegisterNativeFunction("CommonSoupGetName", (void*) CommonSoupGetName, 0);
	RegisterNativeFunction("PlainSoupSetName", (void*) PlainSoupSetName, 1);
	RegisterNativeFunction("PlainSoupGetStore", (void*) PlainSoupGetStore, 0);
	RegisterNativeFunction("PlainSoupAdd", (void*) PlainSoupAdd, 1);
	RegisterNativeFunction("SoupAddFlushed", (void*) SoupAddFlushed, 1);
	RegisterNativeFunction("PlainSoupAddWithUniqueID", (void*) PlainSoupAddWithUniqueID, 1);
	RegisterNativeFunction("SoupAddFlushedWithUniqueId", (void*) SoupAddFlushedWithUniqueId, 1);
	RegisterNativeFunction("PlainSoupAddIndex", (void*) PlainSoupAddIndex, 1);
	RegisterNativeFunction("PlainSoupRemoveIndex", (void*) PlainSoupRemoveIndex, 1);
	RegisterNativeFunction("PlainSoupGetIndexes", (void*) PlainSoupGetIndexes, 0);
	RegisterNativeFunction("PlainSoupIndexSizes", (void*) PlainSoupIndexSizes, 0);
	RegisterNativeFunction("PlainSoupGetSignature", (void*) PlainSoupGetSignature, 0);
	RegisterNativeFunction("PlainSoupSetSignature", (void*) PlainSoupSetSignature, 1);
	RegisterNativeFunction("PlainSoupGetNextUID", (void*) PlainSoupGetNextUID, 0);
	RegisterNativeFunction("PlainSoupGetInfo", (void*) PlainSoupGetInfo, 1);
	RegisterNativeFunction("PlainSoupSetInfo", (void*) PlainSoupSetInfo, 2);
	RegisterNativeFunction("PlainSoupGetAllInfo", (void*) PlainSoupGetAllInfo, 0);
	RegisterNativeFunction("PlainSoupSetAllInfo", (void*) PlainSoupSetAllInfo, 1);
	RegisterNativeFunction("SoupGetFlags", (void*) SoupGetFlags, 0);
	RegisterNativeFunction("SoupSetFlags", (void*) SoupSetFlags, 1);
	RegisterNativeFunction("PlainSoupRemoveAllEntries", (void*) PlainSoupRemoveAllEntries, 0);
	RegisterNativeFunction("PlainSoupRemoveFromStore", (void*) PlainSoupRemoveFromStore, 0);
	RegisterNativeFunction("PlainSoupGetSize", (void*) PlainSoupGetSize, 0);
	RegisterNativeFunction("PlainSoupCopyEntries", (void*) PlainSoupCopyEntries, 1);
	RegisterNativeFunction("PlainSoupCopyEntriesWithCallBack", (void*) PlainSoupCopyEntriesWithCallBack, 3);
	RegisterNativeFunction("PlainSoupMakeKey", (void*) PlainSoupMakeKey, 2);
	RegisterNativeFunction("SoupIsValid", (void*) SoupIsValid, 0);
	RegisterNativeFunction("SoupGetIndexesModTime", (void*) SoupGetIndexesModTime, 0);
	RegisterNativeFunction("SoupGetInfoModTime", (void*) SoupGetInfoModTime, 0);
	RegisterNativeFunction("PlainSoupFlush", (void*) PlainSoupFlush, 0);
	RegisterNativeFunction("PlainSoupAddTags", (void*) PlainSoupAddTags, 1);
	RegisterNativeFunction("PlainSoupRemoveTags", (void*) PlainSoupRemoveTags, 1);
	RegisterNativeFunction("PlainSoupModifyTag", (void*) PlainSoupModifyTag, 2);
	RegisterNativeFunction("PlainSoupHasTags", (void*) PlainSoupHasTags, 0);
	RegisterNativeFunction("PlainSoupGetTags", (void*) PlainSoupGetTags, 0);
}
