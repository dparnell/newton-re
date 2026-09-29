/*
	File:		stores/Soups.h

	Contains:	The frames layer's stores and soups: the store frame
				(storePrototype over a TStoreWrapper, its persistent frame
				{name, signature, nameIndex, ephemerals} a fault block on the
				store's root object; gStores holds the registered ones), the
				plain soup frame (plainSoupPrototype: tStore, storeObj,
				theName, cache, cursors, indexObjects, indexNextUID; its
				persistent frame {class, lastUID, signature, indexes, flags,
				indexesModTime, infoModTime, info} a fault block), the index
				descriptions ({structure, path, type, index: the TSoupIndex
				info object; multiSlot ones with arrays of paths and types,
				order}) and their keys, and the methods of both prototypes,
				which are the ROM's native functions the prototype frames
				hold (bound by symbol through NativeFunctions.h).

	The store's soups are found through its name index: a TSoupIndex of
	string keys (the soup names, no terminator) to the ids of the soups'
	persistent frames.  The soup's entries are found through its _uniqueID
	index (index description 0: long keys to store object ids); every
	other index maps keys to _uniqueIDs' store object ids as well.

	Union soups (UnionSoups.cpp): a clone of unionSoupPrototype {class
	'UnionSoup, soupList: the stores' soups of the name, theName, cursors}
	kept in gUnionSoups; GetUnionSoup/GetUnionSoupAlways make them,
	AddToUnionSoup/RemoveFromUnionSoup follow the soups as stores come
	and go.  Their methods are natives here and the ROM's NewtonScript
	methods re-expressed as source.

	NOT YET RECONSTRUCTED here: the sort tables (every sort id is 0),
	passwords, large binaries, the XMit (synchronising) methods, package
	stores' part handler.  The cursors are Cursors.h, the tags Tags.h.

	Reconstructed from the MP2x00 US ROM (0x00347a34-0x0034e570,
	0x0033f698-0x0033ff44, 0x003509d0-0x0035570c); each function cites
	its origin.
*/

#ifndef __SOUPS_H
#define __SOUPS_H

#ifndef __ENTRIES_H
#include "Entries.h"
#endif
#ifndef __SOUPINDEX_H
#include "SoupIndex.h"
#endif

// the registered stores (store frames), the union soups and the package
// stores' frames
extern Ref	gStores;
extern Ref	gUnionSoups;
extern Ref	gPackageStores;

void	InitQueries(void);					// the globals; the package store's part handler is NOT YET
void	InitSoupPrototypes(void);			// host: the prototype frames when no ROM objects are imported
void	InitUnionSoupPrototype(void);		// host: unionSoupPrototype, its NewtonScript methods compiled, the built-ins they call
void	RegisterUnionSoupNatives(void);


/*------------------------------------------------------------------------------
	S t o r e s
------------------------------------------------------------------------------*/

Ref		GetStores(void);
Ref		MakeStoreObject(TStore* store);		// the store frame (the root object formatted when empty)
Ref		RegisterTStore(TStore* store);		// added to gStores (and the union soups)
void	RemoveTStore(TStore* store);
Ref		ToObject(TStore* store);			// the store frame; nil when not registered
Boolean	IsValidStore(const TStore* store);	// registered
TStore*	GetInternalStore(void);				// ROM 0x00154908 GetInternalStore__Fv - the machine's own store (the flash)
void	SetInternalStore(TStore* store);		// DEVIATION: a port names it, TPSSManager being NOT YET
const TClassInfo*	GetStoreClassInfo(const TStore* store);
TStoreWrapper*	GetStoreWrapper(RefArg storeObject);		// throws when the frame has been killed
TStore*	StoreFromWrapper(RefArg storeObject);
void	CheckWriteProtect(TStore* store);	// throws for a ROM or read-only store
void	CheckWriteProtect(RefArg storeObject);
void	KillStoreObject(RefArg storeObject);
Ref		FlushSoupList(RefArg soups);
long	GetRandomSignature(void);
void	AskForFlush(Boolean ask);
NewtonErr	GetStoreVersion(TStore* store, long* version);
const TSortingTable*	StoreGetDirSortTable(RefArg storeObject);	// NOT YET: nil
void	InitNameIndex(TSoupIndex* index, RefArg storeObject);		// the store's soup name index
void	StoreSaveSortTable(RefArg storeObject, long sortId);			// NOT YET
void	StoreRemoveSortTable(RefArg storeObject, long sortId);			// NOT YET
void	LargeBinariesStoreRemoved(TStoreWrapper* wrapper);				// (LargeBinaries.cpp)
void	AbortLargeBinaries(RefArg entry);								// (LargeBinaries.cpp)

// the store frame's methods (the receiver is the store frame)
Ref		StoreGetName(RefArg rcvr);
Ref		StoreSetName(RefArg rcvr, RefArg name);
Ref		StoreGetKind(RefArg rcvr);
Ref		StoreGetSignature(RefArg rcvr);
Ref		StoreSetSignature(RefArg rcvr, RefArg signature);
Ref		StoreGetInfo(RefArg rcvr, RefArg tag);
Ref		StoreSetInfo(RefArg rcvr, RefArg tag, RefArg value);
Ref		StoreGetAllInfo(RefArg rcvr);
Ref		StoreSetAllInfo(RefArg rcvr, RefArg info);
Ref		StoreGetSoup(RefArg rcvr, RefArg name);
Ref		StoreHasSoup(RefArg rcvr, RefArg name);
PSSId	StoreGetSoupId(RefArg rcvr, RefArg name);
Ref		StoreCreateSoup(RefArg rcvr, RefArg name, RefArg indexes);
Ref		StoreGetSoupNames(RefArg rcvr);
Ref		StoreTotalSize(RefArg rcvr);
Ref		StoreUsedSize(RefArg rcvr);
Ref		StoreOverhead(RefArg rcvr);
Ref		StoreIsReadOnly(RefArg rcvr);
Ref		StoreGetPasswordKey(RefArg rcvr);
Ref		StoreGetPasswordKey(TStore* store);
Boolean	CheckStorePassword(TStore* store, RefArg password);		// ROM 0x0035268c CheckStorePassword__FP6TStoreRC6RefVar
Ref		StoreHasPassword(RefArg rcvr);							// ROM 0x003527d4 StoreHasPassword
Ref		StoreSetPassword(RefArg rcvr, RefArg oldPassword, RefArg newPassword);	// ROM 0x003527f4 StoreSetPassword
Ref		StoreIsValid(RefArg rcvr);
Ref		StoreLock(RefArg rcvr);
Ref		StoreUnlock(RefArg rcvr);
Ref		StoreAbort(RefArg rcvr);
Ref		StoreDirty(RefArg rcvr);
Ref		StoreFlush(RefArg rcvr);
Ref		StoreErase(RefArg rcvr);
Ref		StoreCheckWriteProtect(RefArg rcvr);
Ref		StoreReadObject(RefArg rcvr, RefArg id, RefArg size, RefArg offset);	// FReadStoreObject
Ref		StoreWriteObject(RefArg rcvr, RefArg id, RefArg data, RefArg length, RefArg offset);	// FWriteStoreObject
Ref		StoreWriteWholeObject(RefArg rcvr, RefArg id, RefArg data, RefArg, RefArg);	// FWriteEntireStoreObject
Ref		StoreNewObject(RefArg rcvr, RefArg size);								// FNewStoreObject
Ref		StoreDeleteObject(RefArg rcvr, RefArg id);								// FDeleteStoreObject
Ref		StoreSetObjectSize(RefArg rcvr, RefArg id, RefArg size);				// FSetStoreObjectSize
Ref		StoreGetObjectSize(RefArg rcvr, RefArg id);								// FGetStoreObjectSize

// The PSS manager's record of a store (0x50 bytes in the ROM; only the two
// words the store frame's methods read are named).
struct StorePSSInfo
{
	UByte		fUnknown00[0x18];
	ULong32		fSocket;			// +0x18  the socket the card is in
	UByte		fUnknown1c[0x14];
	ULong32		fCardType;			// +0x30  the card's type, four characters
};
const StorePSSInfo*	GetStorePSSInfo(const TStore* store);	// ROM 0x001559bc GetStorePSSInfo__FPC6TStore (DEVIATION: always nil - TPSSManager is NOT YET)
long	GetCardSlotStores(int socket, TStore** stores);								// ROM 0x00155a20 GetCardSlotStores__FiPP6TStore
Ref		FGetCardSlotStores(RefArg rcvr, RefArg socket);								// ROM 0x0035561c FGetCardSlotStores
Ref		StoreGetCardSlot(RefArg rcvr);											// FGetStoreCardSlot
Ref		StoreGetCardType(RefArg rcvr);											// FGetStoreCardType


/*------------------------------------------------------------------------------
	S o u p s
------------------------------------------------------------------------------*/

// what EachSoupCursorDo tells a soup's cursors (Cursors.cpp)
enum
{
	kSoupCursorSoupAdded = 0,		// the soup joined a union soup
	kSoupCursorSoupRemoved = 1,		// the soup itself
	kSoupCursorEntryRemoved = 2,	// the entry
	kSoupCursorSetSoup = 3,			// the cursor's soup
	kSoupCursorTagsChanged = 4,
	kSoupCursorIndexesChanged = 5,
	kSoupCursorEntryMoved = 6,		// the entry, the entry frame it became
	kSoupCursorEntryReadded = 7,	// the entry, the fault block it was
	kSoupCursorIndexRemoved = 8		// the index description
};

void	EachSoupCursorDo(RefArg soup, int op);
void	EachSoupCursorDo(RefArg soup, int op, RefArg arg);
void	EachSoupCursorDo(RefArg soup, int op, RefArg arg1, RefArg arg2);
void	EachSoupCursorEntryChanged(RefArg soup, RefArg entry, Boolean keysChanged, Boolean tagsChanged);

// union soups (UnionSoups.cpp)
Ref		GetUnionSoup(RefArg name);							// nil when no store has the soup
Ref		GetUnionSoupAlways(RefArg name);
void	AddToUnionSoup(RefArg nameOrUnionSoup, RefArg soup);
void	RemoveFromUnionSoup(RefArg nameOrUnionSoup, RefArg soup);
void	CheckStoresWriteProtect(RefArg unionSoup);
Ref		CheckSoupsSortTables(RefArg soupPersistent1, RefArg soupPersistent2);
Boolean	StoreHasSortTables(RefArg storeObject);
Ref		StoreCheckUnion(RefArg rcvr);
Ref		StoreConvertSoupSortTables(RefArg rcvr, RefArg name);
Ref		UnionSoupAdd(RefArg rcvr, RefArg entry);
Ref		UnionSoupAddIndex(RefArg rcvr, RefArg indexSpec);
Ref		UnionSoupRemoveIndex(RefArg rcvr, RefArg path);
Ref		UnionSoupFlush(RefArg rcvr);
Ref		UnionSoupGetSize(RefArg rcvr);

// the messages a soup (plain or union) answers
Ref		SoupQuery(RefArg soup, RefArg querySpec);
Ref		Query(RefArg soup, RefArg querySpec);		// ROM 0x0033fbd8 Query__FRC6RefVarT1 - the soup sent Query
Ref		SoupGetName(RefArg soup);
Ref		SoupGetSignature(RefArg soup);
Ref		SoupSetName(RefArg soup, RefArg name);
Ref		SoupSetSignature(RefArg soup, long signature);
Ref		SoupGetInfo(RefArg soup, RefArg tag);
Ref		SoupSetInfo(RefArg soup, RefArg tag, RefArg value);
Ref		SoupGetAllInfo(RefArg soup);
Ref		SoupSetAllInfo(RefArg soup, RefArg info);
Ref		SoupCopyEntries(RefArg soup, RefArg toSoup);
Ref		SoupRemoveAllEntries(RefArg soup);
Ref		SoupRemoveFromStore(RefArg soup);
Ref		SoupFlush(RefArg soup);
Ref		SoupGetStore(RefArg soup);
Ref		SoupAddIndex(RefArg soup, RefArg indexSpec);
Ref		SoupRemoveIndex(RefArg soup, RefArg path);
Ref		SoupGetIndexes(RefArg soup);
Ref		SoupGetNextUID(RefArg soup);
Ref		SoupAdd(RefArg soup, RefArg entry);
Ref		SoupAddWithUniqueID(RefArg soup, RefArg entry);
Boolean	PathsEqual(RefArg a, RefArg b);

// entry aliases ([nil, soup signature, _uniqueID, soup name] of class 'alias)
Ref		MakeEntryAlias(RefArg entry);
Boolean	IsEntryAlias(RefArg object);
Ref		ResolveEntryAliasInStores(RefArg alias, RefArg stores);
Ref		ResolveEntryAlias(RefArg alias);
Boolean	CompareAliasAndEntry(RefArg alias, RefArg entry);
Boolean	IsSameEntry(RefArg a, RefArg b);
void	RegisterSoupNatives(void);				// the NewtonScript functions and the prototypes' methods bound

// the plain soup's persistent frame
Ref		SoupPersistent(RefArg soup);			// throws kNSErrSoupRemoved when nil
void	SoupChanged(RefArg soupPersistent, Boolean write);	// its flags say changed (bits 0 and 1); written when asked
Ref		GetTagsIndexDesc(RefArg soupPersistent);

// index descriptions and keys
Boolean	IndexPathsEqual(RefArg a, RefArg b);
Ref		IndexPathToIndexDesc(RefArg soupPersistent, RefArg path, long* index);
const TSortingTable*	GetIndexSortTable(RefArg indexDesc);
void	IndexDescToIndexInfo(RefArg indexDesc, IndexInfo* info);
Ref		NewIndexDesc(RefArg soupPersistent, RefArg storeObject, RefArg indexSpec);
Ref		AddNewSoupIndexes(RefArg soupPersistent, RefArg storeObject, RefArg indexSpecs);
void	CreateSoupIndexObjects(RefArg soup);
TSoupIndex*	GetSoupIndexObject(RefArg soup, PSSId infoId);		// 0: the _uniqueID index
void	IndexEntries(RefArg soup, RefArg indexDesc);
void	KeyToSKey(RefArg key, RefArg type, SKey* outKey, short* outSize, Boolean* outIsVariable);
Ref		SKeyToKey(const SKey& key, RefArg type, short* outSize);
void	MultiKeyToSKey(RefArg key, RefArg types, SKey* outKey);
void	RichStringToSKey(RefArg string, SKey* outKey);
Ref		GetEntryKey(RefArg entry, RefArg path);
Boolean	GetEntrySKey(RefArg entry, RefArg indexDesc, SKey* outKey, Boolean* outIsVariable);
void	AlterIndexes(Boolean add, RefArg soup, RefArg entry, PSSId id);
Boolean	UpdateIndexes(RefArg soup, RefArg newEntry, RefArg oldEntry, PSSId id, Boolean* tagsChanged);
void	AbortSoupIndexes(RefArg soup);

// adding entries (flags: SafeEntryAdd)
enum
{
	kSoupAddVerbatim = 1,		// the frame as it is (not made internal)
	kSoupAddSetModTime = 2,
	kSoupAddSetUniqueID = 4		// the soup's next _uniqueID given to it
};
Ref		SafeEntryAdd(RefArg soup, RefArg entry, RefArg uniqueId, int flags);
Ref		CommonSoupAddEntry(RefArg soup, RefArg entry, int flags, Boolean unused);

// the plain soup's methods (the receiver is the soup frame)
Ref		PlainSoupGetStore(RefArg rcvr);
Ref		PlainSoupAdd(RefArg rcvr, RefArg entry);
Ref		SoupAddFlushed(RefArg rcvr, RefArg entry);
Ref		PlainSoupAddWithUniqueID(RefArg rcvr, RefArg entry);
Ref		SoupAddFlushedWithUniqueId(RefArg rcvr, RefArg entry);
Ref		PlainSoupAddIndex(RefArg rcvr, RefArg indexSpec);
Ref		PlainSoupRemoveIndex(RefArg rcvr, RefArg path);
Ref		PlainSoupSetName(RefArg rcvr, RefArg name);
Ref		PlainSoupGetSignature(RefArg rcvr);
Ref		PlainSoupSetSignature(RefArg rcvr, RefArg signature);
Ref		PlainSoupGetNextUID(RefArg rcvr);
Ref		PlainSoupGetInfo(RefArg rcvr, RefArg tag);
Ref		PlainSoupSetInfo(RefArg rcvr, RefArg tag, RefArg value);
Ref		PlainSoupGetAllInfo(RefArg rcvr);
Ref		PlainSoupSetAllInfo(RefArg rcvr, RefArg info);
Ref		SoupGetFlags(RefArg rcvr);
Ref		SoupSetFlags(RefArg rcvr, RefArg flags);
Ref		PlainSoupRemoveAllEntries(RefArg rcvr);
Ref		PlainSoupRemoveFromStore(RefArg rcvr);
Ref		PlainSoupDirty(RefArg rcvr);
Ref		PlainSoupFlush(RefArg rcvr);
Ref		PlainSoupGetSize(RefArg rcvr);
Ref		PlainSoupCopyEntries(RefArg rcvr, RefArg toSoup);							// CopyEntries.cpp
Ref		PlainSoupCopyEntriesWithCallBack(RefArg rcvr, RefArg toSoup, RefArg callback, RefArg interval);
Ref		SlowCopyEntries(RefArg fromSoup, RefArg toSoup, RefArg callback, ULong interval);
Boolean	CompareSoupIndexes(RefArg soupPersistent1, RefArg soupPersistent2);
Ref		PlainSoupIndexSizes(RefArg rcvr);
Ref		PlainSoupGetIndexes(RefArg rcvr);
Ref		PlainSoupMakeKey(RefArg rcvr, RefArg key, RefArg path);
Ref		SoupIsValid(RefArg rcvr);
Ref		SoupGetIndexesModTime(RefArg rcvr);
Ref		SoupGetInfoModTime(RefArg rcvr);
Ref		CommonSoupGetName(RefArg rcvr);
void	SoupCacheRemoveAllEntries(RefArg soup);

#endif	/* __SOUPS_H */
