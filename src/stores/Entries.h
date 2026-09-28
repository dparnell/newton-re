/*
	File:		stores/Entries.h

	Contains:	Soup entries: the fault block (a frame of class
				kFaultBlockClass whose slots are the entry's handler - its
				soup - the store wrapper, the store object id and, once
				read, the entry frame itself) that stands for an entry in
				memory, the entry cache a soup keeps of the entries it has
				handed out, and the operations on entries (change, flush,
				remove, replace, copy, move, sizes, ids).  A proxy entry is
				a fault block with no store: its operations are messages to
				its handler.  The persistent frames of stores and soups are
				fault blocks too (their handler nil).

	Reconstructed from the MP2x00 US ROM (0x002d9b10-0x002dbd8c,
	0x002e01d8-0x002e0b88); each function cites its origin.
*/

#ifndef __ENTRIES_H
#define __ENTRIES_H

#ifndef __STOREWRAPPER_H
#include "StoreWrapper.h"
#endif
#ifndef __OBJECTHEAP_H
#include "ObjectHeap.h"
#endif

class CDynamicArray;


/*------------------------------------------------------------------------------
	F a u l t   b l o c k s
------------------------------------------------------------------------------*/

// the slots (ObjectHeap.h: kFaultBlockHandlerSlot, kFaultBlockStoreSlot,
// kFaultBlockIdSlot, kFaultBlockObjectSlot)
inline Ref*				FaultBlockSlots(Ref fb)			{ return ObjArraySlots(NoFaultObjectPtr(fb)); }
inline Ref				FaultBlockHandler(Ref fb)		{ return FaultBlockSlots(fb)[kFaultBlockHandlerSlot]; }
inline TStoreWrapper*	FaultBlockStore(Ref fb)			{ return (TStoreWrapper*) FaultBlockSlots(fb)[kFaultBlockStoreSlot]; }
inline PSSId			FaultBlockId(Ref fb)			{ return (PSSId) RINT(FaultBlockSlots(fb)[kFaultBlockIdSlot]); }
inline Ref				FaultBlockObject(Ref fb)		{ return FaultBlockSlots(fb)[kFaultBlockObjectSlot]; }

Ref		MakeFaultBlock(RefArg handler, TStoreWrapper* wrapper, PSSId id);
Ref		MakeFaultBlock(RefArg handler, TStoreWrapper* wrapper, PSSId id, RefArg object);
Ref		StoreFollowFaultBlock(RefArg faultBlock);		// FollowFaultBlock's body (installed in gFollowFaultBlockProc)
void	WriteFaultBlock(RefArg faultBlock);
void	InvalFaultBlock(RefArg faultBlock);
void	UncacheIfFaultBlock(RefArg object);
Ref		NewProxyEntry(RefArg handler, RefArg object);


/*------------------------------------------------------------------------------
	T h e   e n t r y   c a c h e
	An array of fault blocks (nil where one was taken out), grown by 8.
------------------------------------------------------------------------------*/

const long kEntryCacheGrowth = 8;

Ref		MakeEntryCache(void);
Ref		FindEntryInCache(RefArg cache, PSSId id);
void	PutEntryIntoCache(RefArg cache, RefArg entry);
void	DeleteEntryFromCache(RefArg cache, RefArg entry);
void	InvalidateCacheEntries(RefArg cache);
Ref		FindSoupInCache(RefArg cache, RefArg name);			// a soup by name, case-insensitively


/*------------------------------------------------------------------------------
	E n t r i e s
------------------------------------------------------------------------------*/

// EntryChangeCommon's flags
enum
{
	kEntryChangeKeepUniqueID = 1,		// the store's _uniqueID is reinstated if the frame's differs
	kEntryChangeSetModTime = 2,			// _modTime is set to now
	kEntryChangeUpdateTags = 4,			// the tags index is updated
	kEntryChangeVerbatim = 8			// the frame is written as it is (not made internal) and dropped from memory after
};

Ref		EntryStore(RefArg entry);				// the store frame the entry is on
Ref		EnsureEntryInternal(RefArg entry);
void	EntryChangeCommon(RefArg entry, int flags);
Ref		EntryChange(RefArg entry);
Ref		EntryChangeWithModTime(RefArg entry);
Ref		EntryChangeVerbatim(RefArg entry);
Ref		EntryFlush(RefArg entry);
Ref		EntryFlushWithModTime(RefArg entry);
void	EntryRemoveFromSoup(RefArg entry);
Ref		EntryReplaceCommon(RefArg entry, RefArg newEntry, int withModTime);
Ref		EntryReplace(RefArg entry, RefArg newEntry);
Ref		EntryReplaceWithModTime(RefArg entry, RefArg newEntry);
Ref		EntryUndoChanges(RefArg entry);
Ref		EntryCopy(RefArg entry, RefArg soup);
void	EntryMove(RefArg entry, RefArg soup);
Boolean	EntryDirty(Ref entry);					// the entry (or an object it holds) has been written to
Boolean	EntryIsResident(Ref entry);				// the entry frame is in memory
Boolean	EntryValid(RefArg entry);				// its store is still here
Ref		GetEntry(RefArg soup, PSSId id);		// the soup's entry (from its cache, else a new fault block)
void	CheckProxyEntry(RefArg entry);			// throws unless a proxy
Boolean	IsProxyEntry(RefArg entry);
Ref		EntryCachedObject(RefArg entry);
void	EntrySetCachedObject(RefArg entry, RefArg object);
Ref		EntryHandler(RefArg entry);
void	EntrySetHandler(RefArg entry, RefArg handler);
Ref		ForwardEntryMessage(RefArg entry, RefArg message);
Ref		ForwardEntryMessage(RefArg entry, RefArg message, RefArg arg);
Boolean	IsSoupEntry(RefArg object);
Ref		FIsValid(RefArg rcvr, RefArg obj);			// ROM 0x0031e09c FIsValid - whether the object is still usable
Ref		EntrySoup(RefArg entry);
long	EntrySize(PSSId id, TStoreWrapper* wrapper, Boolean withLargeBinaries);
long	EntrySize(RefArg entry);
long	EntrySizeWithoutVBOs(RefArg entry);
long	EntryTextSize(RefArg entry);
long	EntryUniqueID(RefArg entry);
long	EntryModTime(RefArg entry);


/*------------------------------------------------------------------------------
	E p h e m e r a l s
	Large objects created and not yet committed: the store's
	TEphemeralTracker (Ephemerals.h; a store without the large-object
	capability has none).
------------------------------------------------------------------------------*/

NewtonErr	SetupEphemeralTracker(RefArg storeObject, PSSId rootFrameId);
Boolean		StoreWritable(TStore* store);
Boolean		CanCreateLargeObjectsOnStore(TStore* store);

void	InitEntries(void);		// installs the fault block reader

#endif	/* __ENTRIES_H */
