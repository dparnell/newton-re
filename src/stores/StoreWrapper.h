/*
	File:		stores/StoreWrapper.h

	Contains:	The frames layer's view of a TStore: TStoreWrapper, which
				keeps the store's symbol table and map table (two
				TStoreHashTables), caches of the maps and symbols read back,
				the B-tree node cache of the store's soup indexes, and the
				store's dirty state; TStoreHashTable, the on-store hash table
				of byte strings both tables are (a 64-bucket table object, each
				bucket an object of [2-byte length][bytes] entries; an entry
				is referred to by bucket << 16 | offset); TCachedReadStore, a
				read cache over one store object; and StoreRootData, the
				store's root object.

	A frame is written to a store with a *map reference* - the reference
	of its sorted tag names in the map table - and its slots in that
	order (FrameToMapReference/ReferenceToMap); a symbol as a *symbol
	reference* into the symbol table (SymbolToReference/ReferenceToSymbol).

	The ROM's layouts: TStoreWrapper 0x98, TStoreHashTable 0x108,
	TStoreHashTableIterator 0x1c, TCachedReadStore 0x41c.  The DDK has no
	headers for them.
*/

#ifndef __STOREWRAPPER_H
#define __STOREWRAPPER_H

#ifndef __STORE_H
#include "Store.h"
#endif
#ifndef __FRAMES_H
#include "Frames.h"
#endif
#ifndef __NODECACHE_H
#include "NodeCache.h"
#endif

class TEphemeralTracker;		// NOT YET RECONSTRUCTED (ephemeral soup entries)


// The store's root object: what MakeStoreObject writes and reads back.
const ULong kStoreRootSignature = 'WALY';		// 0x57414c59
const long	kStoreRootVersion = 4;

struct StoreRootData
{
	ULong32		fSignature;			// +0x00  kStoreRootSignature
	Long32		fVersion;			// +0x04  kStoreRootVersion (older stores: less)
	StorePSSId	fMapTableId;		// +0x08
	StorePSSId	fSymbolTableId;		// +0x0c
	StorePSSId	fRootFrameId;		// +0x10  the store's persistent frame
	StorePSSId	fExtra;				// +0x14  (0 when the root object is only 0x14 long)
};
const long kStoreRootDataSize = 0x14;


/* -------------------------------------------------------------------------------
	TStoreHashTable
------------------------------------------------------------------------------- */

const long kStoreHashTableBuckets = 64;
const long kStoreHashTableSize = kStoreHashTableBuckets * sizeof(StorePSSId);	// 0x100
const long kStoreHashTableMaxEntry = 0x400;		// entries up to this are written in one piece

class TStoreHashTable
{
public:
	static PSSId	Create(TStore* store);			// a new, empty table object
				TStoreHashTable(TStore* store, PSSId id);

	void		Abort(void);							// the buckets re-read after the store aborted
	long		Insert(ULong hash, char* data, long size);	// ==> the entry's reference (found or added)
	Boolean		Get(long reference, char* data, long* size);	// ==> whether it fit; *size the entry's size
	long		TotalSize(void);

	PSSId		fId;								// +0x00
	StorePSSId	fBuckets[kStoreHashTableBuckets];	// +0x04  as on the store
	TStore*		fStore;								// +0x104
};


class TStoreHashTableIterator
{
public:
				TStoreHashTableIterator(TStoreHashTable* table);

	void		Next(void);
	void		GetData(char* data, long* size);		// the current entry (up to *size bytes; *size its length)
	Boolean		Done(void) const					{ return fDone; }
	long		Reference(void) const				{ return (fBucket << 16) | fOffset; }

	TStoreHashTable*	fTable;			// +0x00
	long		fBucket;				// +0x04  -1 before the first
	PSSId		fBucketId;				// +0x08
	long		fBucketSize;			// +0x0c
	long		fOffset;				// +0x10  of the current entry
	long		fEntryLength;			// +0x14  (the ROM reads the 2-byte length into the high half)
	Boolean		fDone;					// +0x18
};


/* -------------------------------------------------------------------------------
	TCachedReadStore
	One object read once into a buffer (its own 1 KB, or one allocated for a
	larger object); GetDataPtr answers a pointer to a range of it.
------------------------------------------------------------------------------- */

const long kCachedReadStoreBufferSize = 0x400;

class TCachedReadStore
{
public:
				TCachedReadStore();
				TCachedReadStore(TStore* store, PSSId id, long size);
				~TCachedReadStore();

	void		Init(TStore* store, PSSId id, long size);		// size -1: the object's
	NewtonErr	GetDataPtr(long offset, long size, void** data);

	char		fBuffer[kCachedReadStoreBufferSize];	// +0x000
	char*		fData;					// +0x400  fBuffer, or the buffer for a bigger object
	TStore*		fStore;					// +0x404
	PSSId		fId;					// +0x408
	long		fSize;					// +0x40c
	Boolean		fLoaded;				// +0x410
	char*		fBigBuffer;				// +0x414  for a range read past the cached size
	long		fBigBufferSize;			// +0x418
};


/* -------------------------------------------------------------------------------
	TStoreWrapper
------------------------------------------------------------------------------- */

const long kMapCacheSize = 8;
const long kSymbolCacheSize = 16;

// what StartCopyMaps_Symbols keeps while a store's objects are copied to
// another: the references already translated
struct StoreCopyInfo
{
	struct { long fFrom; long fTo; long fCount; }	fMaps[16];		// +0x00  (fFrom -1: unused)
	long		fNextSymbol;										// +0xc0
	struct { long fFrom; long fTo; }				fSymbols[32];	// +0xc4
};

class TStoreWrapper
{
public:
				TStoreWrapper(TStore* store);
				~TStoreWrapper();

	void		Dirty(void);
	void		SparklingClean(void);
	NewtonErr	LockStore(void);
	NewtonErr	UnlockStore(void);
	NewtonErr	Abort(void);
	void		GetStoreSizes(long* totalSize, long* usedSize);

	// maps and symbols
	long		AddMap(SortedMapTag* tags, Boolean skipHiddenSlots, long* count, long* indexes);
	long		FrameToMapReference(RefArg frame, Boolean skipHiddenSlots, long* count, long** indexes);
	Ref			ReferenceToMap(long reference);
	long		SymbolToReference(RefArg symbol);
	Ref			ReferenceToSymbol(long reference);

	// copying to another store
	void		StartCopyMaps_Symbols(void);
	void		EndCopyMaps_Symbols(void);
	long		CopyMap(long reference, TStoreWrapper* from, long* count);
	long		CopySymbol(long reference, TStoreWrapper* from);

	TStore*		Store(void) const					{ return fStore; }

	TStoreHashTable*	fMapTable;					// +0x00
	TStoreHashTable*	fSymbolTable;				// +0x04
	RefStruct	fMapCache;							// +0x08  an array of kMapCacheSize maps
	long		fMapCacheRefs[kMapCacheSize];		// +0x0c  their references (-1: empty)
	long		fMapCacheNext;						// +0x2c
	RefStruct	fSymbolCache;						// +0x30  an array of kSymbolCacheSize symbols
	long		fSymbolCacheRefs[kSymbolCacheSize];	// +0x34
	StoreCopyInfo*	fCopyInfo;						// +0x74
	long		fSymbolCacheNext;					// +0x78
	TStore*		fStore;								// +0x7c
	TNodeCache	fNodeCache;							// +0x80
	Boolean		fIsDirty;							// +0x90
	TEphemeralTracker*	fEphemeralTracker;			// +0x94
};

void	AskForFlush(Boolean ask);
void	ThrowOSErr(NewtonErr err);				// the ROM's _OSErr: exStoreError with the error
inline void	OSErrIf(NewtonErr err)				{ if (err != noErr) ThrowOSErr(err); }
void	ReadStoreRootData(TStore* store, PSSId rootId, StoreRootData* data, long* size);

#endif	/* __STOREWRAPPER_H */
