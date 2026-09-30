/*
	File:		stores/Entries.cpp

	Contains:	Soup entries (Entries.h): fault blocks, the entry cache and
				the entry operations.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Entries.h"
#include "Soups.h"
#include "StoreObject.h"
#include "LargeBinaries.h"
#include "LargeObjects.h"
#include "Frames.h"
#include "Interpreter.h"
#include "RSSymbols.h"
#include "RichString.h"
#include "REPTranslators.h"
#include "Unicode.h"
#include "NSErrors.h"
#include "NewtonTime.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"
#include "protocols/Protocols.h"

extern const ExceptionName exStoreError;
extern const ExceptionName exBadType;


/*------------------------------------------------------------------------------
	F a u l t   b l o c k s
------------------------------------------------------------------------------*/

// ROM 0x002e04d4 MakeFaultBlock__FRC6RefVarP13TStoreWrapperUl
// A fault block for store object id of wrapper's store, handled by
// handler (the soup); the entry is not in memory.
Ref
MakeFaultBlock(RefArg handler, TStoreWrapper* wrapper, PSSId id)
{
	RefVar fb(AllocateArray(RefVar(NILREF), 4));
	SetArraySlotRef(fb, kFaultBlockHandlerSlot, handler);
	SetArraySlotRef(fb, kFaultBlockStoreSlot, (Ref) wrapper);
	SetArraySlotRef(fb, kFaultBlockIdSlot, MAKEINT(id));
	ObjClass(OBJ(fb)) = kFaultBlockClass;
	gCacheObjPtrRef = 0;
	return fb;
}


// ROM 0x002e0584 MakeFaultBlock__FRC6RefVarP13TStoreWrapperUlT1
// The same with the entry frame already in memory.
Ref
MakeFaultBlock(RefArg handler, TStoreWrapper* wrapper, PSSId id, RefArg object)
{
	RefVar fb(AllocateArray(RefVar(NILREF), 4));
	SetArraySlotRef(fb, kFaultBlockHandlerSlot, handler);
	SetArraySlotRef(fb, kFaultBlockStoreSlot, (Ref) wrapper);
	SetArraySlotRef(fb, kFaultBlockIdSlot, MAKEINT(id));
	SetArraySlotRef(fb, kFaultBlockObjectSlot, object);
	ObjClass(OBJ(fb)) = kFaultBlockClass;
	gCacheObjPtrRef = 0;
	return fb;
}


// ROM 0x002db03c NewProxyEntry__FRC6RefVarT1
// A fault block with no store: its handler answers for it.
Ref
NewProxyEntry(RefArg handler, RefArg object)
{
	RefVar fb(AllocateArray(RefVar(NILREF), 4));
	SetArraySlotRef(fb, kFaultBlockHandlerSlot, handler);
	SetArraySlotRef(fb, kFaultBlockStoreSlot, NILREF);
	SetArraySlotRef(fb, kFaultBlockIdSlot, MAKEINT(0));
	SetArraySlotRef(fb, kFaultBlockObjectSlot, object);
	ObjClass(OBJ(fb)) = kFaultBlockClass;
	gCacheObjPtrRef = 0;
	return fb;
}


// ROM 0x002e01d8 FollowFaultBlock__FRC6RefVar
// The entry a fault block stands for: read from its store (and kept in the
// block); a proxy's handler is asked (EntryAccess); the store gone is an
// error.
Ref
StoreFollowFaultBlock(RefArg faultBlock)
{
	Ref* slots = FaultBlockSlots(faultBlock);
	if (slots[kFaultBlockStoreSlot] == 0)
		Throw(exStoreError, (void*) kNSErrEntryStoreGone, nil);
	if (slots[kFaultBlockStoreSlot] == NILREF)
		return ForwardEntryMessage(faultBlock, RSSYMentryaccess);
	TStoreWrapper* wrapper = (TStoreWrapper*) slots[kFaultBlockStoreSlot];
	PSSId id = (PSSId) RINT(slots[kFaultBlockIdSlot]);
	Ref object = LoadPermObject(wrapper, id, nil);
	FaultBlockSlots(faultBlock)[kFaultBlockObjectSlot] = object;
	return object;
}


// ROM 0x002e027c WriteFaultBlock__FRC6RefVar
// The entry frame in memory written back to its store object.
void
WriteFaultBlock(RefArg faultBlock)
{
	if (!IsFaultBlock(faultBlock))
		Throw(exStoreError, (void*) kNSErrNotAFaultBlock, nil);
	RefVar object(FaultBlockObject(faultBlock));
	if ((Ref) object != NILREF)
	{
		TStoreWrapper* wrapper = FaultBlockStore(faultBlock);
		PSSId id = FaultBlockId(faultBlock);
		OSErrIf(wrapper->LockStore());
		newton_try
		{
			StorePermObject(object, wrapper, id, nil, nil);
		}
		newton_catch_all
		{
			OSErrIf(wrapper->Abort());
			rethrow;
		}
		end_try;
		OSErrIf(wrapper->UnlockStore());
	}
}


// the ROM's 0x42: the special immediate left where an object was killed
// (the DDK names the other specials but not this one)
static const Ref kKilledObjectRef = MAKEIMMED(kImmedSpecial, 4);


// ROM 0x0031e09c FIsValid
// Whether the object is still usable: an immediate always is, except the
// ROM's 0x42 - the marker a killed object leaves behind; a soup entry is
// when its store is still there (EntryValid), a large binary when its
// pages are still mapped, and anything else simply is.  An
// evt.ex.fr;type.ref exception carrying error -48201 - the object has
// gone - is the answer nil rather than a throw.  An object that itself
// lies in a large object (NoTouchObjectPtr's flag: never on the host,
// see ObjectHeap.cpp) is valid while that is mapped.
Ref
FIsValid(RefArg /*rcvr*/, RefArg obj)
{
	if (!ISPTR(obj))
		return (Ref) obj == kKilledObjectRef ? NILREF : TRUEREF;
	RefVar result(TRUEREF);
	newton_try
	{
		int isLargeObject = 0;
		ObjHeader* o = NoTouchObjectPtr(obj, &isLargeObject);
		if (isLargeObject != 0)
			result = MAKEBOOLEAN(LargeObjectAddressIsValid((ULong) (uintptr_t) o));
		else if (IsSoupEntry(obj))
			result = MAKEBOOLEAN(EntryValid(obj));
		else if (IsLargeBinary(obj))
			result = MAKEBOOLEAN(LargeObjectAddressIsValid(LargeBinaryData(obj)->fAddress));
	}
	newton_catch(exFrames)
	{
		Exception* exception = CurrentException();
		if (Subexception(exception->name, (ExceptionName) "type.ref") && IsFrame(RefVar(*(Ref*) exception->data)))
		{
			RefVar code(GetFrameSlotRef(RefVar(*(Ref*) exception->data), RSSYMerrorcode));
			if (ISINT(code) && RINT(code) == kNSErrBadMagicPointer)
				result = NILREF;
			else
				rethrow;
		}
		else
			rethrow;
	}
	end_try;
	return result;
}


// ROM 0x002e03a0 InvalFaultBlock__FRC6RefVar
// The store is gone: no store, no entry.
void
InvalFaultBlock(RefArg faultBlock)
{
	if (!IsFaultBlock(faultBlock))
		Throw(exStoreError, (void*) kNSErrNotAFaultBlock, nil);
	Ref* slots = FaultBlockSlots(faultBlock);
	slots[kFaultBlockStoreSlot] = 0;
	slots[kFaultBlockObjectSlot] = NILREF;
}


// ROM 0x002e0400 UncacheIfFaultBlock__FRC6RefVar
// The entry frame dropped from memory (it will be read again).
void
UncacheIfFaultBlock(RefArg object)
{
	if (!IsFaultBlock(object))
		return;
	FaultBlockSlots(object)[kFaultBlockObjectSlot] = NILREF;
}


/*------------------------------------------------------------------------------
	T h e   e n t r y   c a c h e
------------------------------------------------------------------------------*/

// ROM 0x002d9b10 MakeEntryCache__Fv
// A weak array of 8 nils: the entries stay only while something else
// refers to them.
Ref
MakeEntryCache(void)
{
	return AllocateArray(RefVar(kWeakArrayClass), kEntryCacheGrowth);
}


// ROM 0x002d9b5c FindEntryInCache__FRC6RefVarUl
// The cached fault block for store object id; nil for none.
Ref
FindEntryInCache(RefArg cache, PSSId id)
{
	long count = Length(cache);
	for (long i = 0; i < count; i++)
	{
		Ref entry = GetArraySlotRef(cache, i);
		if (entry != NILREF && FaultBlockId(entry) == id)
			return entry;
	}
	return NILREF;
}


// ROM 0x002da774 PutEntryIntoCache__FRC6RefVarT1
// The cache closed up (its nils dropped) and the entry put after the last;
// grown by 8 when full, shrunk to 8 when few.
void
PutEntryIntoCache(RefArg cache, RefArg entry)
{
	long count = Length(cache);
	long used = 0;
	for (long i = 0; i < count; i++)
	{
		Ref item = GetArraySlotRef(cache, i);
		if (item != NILREF)
		{
			if (i != used)
			{
				SetArraySlotRef(cache, used, item);
				SetArraySlotRef(cache, i, NILREF);
			}
			used++;
		}
	}
	if (used == count)
		SetLength(cache, count + kEntryCacheGrowth);
	else if (used < kEntryCacheGrowth)
		SetLength(cache, kEntryCacheGrowth);
	SetArraySlotRef(cache, used, entry);
}


// ROM 0x002dae6c DeleteEntryFromCache__FRC6RefVarT1
void
DeleteEntryFromCache(RefArg cache, RefArg entry)
{
	long count = Length(cache);
	for (long i = 0; i < count; i++)
		if (EQRef(entry, GetArraySlotRef(cache, i)))
			SetArraySlotRef(cache, i, NILREF);
}


// ROM 0x002db04c InvalidateCacheEntries__FRC6RefVar
// Every cached entry invalidated (its store is gone) and the cache emptied.
void
InvalidateCacheEntries(RefArg cache)
{
	RefVar entry;
	for (long i = Length(cache) - 1; i >= 0; i--)
	{
		entry = GetArraySlotRef(cache, i);
		if ((Ref) entry != NILREF)
		{
			InvalFaultBlock(entry);
			SetArraySlotRef(cache, i, NILREF);
		}
	}
	SetLength(cache, kEntryCacheGrowth);
}


// ROM 0x002db0e0 FindSoupInCache__FRC6RefVarT1
// The soup of this name (case-insensitively) in a soup cache; nil for none.
Ref
FindSoupInCache(RefArg cache, RefArg name)
{
	RefVar soup;
	RefVar soupName;
	long count = Length(cache);
	for (long i = 0; i < count; i++)
	{
		soup = GetArraySlotRef(cache, i);
		if ((Ref) soup != NILREF)
		{
			soupName = GetFrameSlotRef(soup, RSSYMthename);
			if (CompareStringNoCase(GetCString(name), GetCString(soupName)) == 0)
				return soup;
		}
	}
	return NILREF;
}


/*------------------------------------------------------------------------------
	E n t r i e s
------------------------------------------------------------------------------*/

// The check every entry operation starts with.
static inline void
CheckEntry(RefArg entry)
{
	if (!IsFaultBlock(entry))
		Throw(exStoreError, (void*) kNSErrNotASoupEntry, nil);
}


// ROM 0x002d9c04 EntryStore__FRC6RefVar
// The store frame the entry's soup is on (a proxy's handler is asked).
Ref
EntryStore(RefArg entry)
{
	if (IsProxyEntry(entry))
		return ForwardEntryMessage(entry, RSSYMentrystore);
	RefVar soup(EntrySoup(entry));
	return GetFrameSlot(soup, RSSYMstoreobj);
}


// ROM 0x002d9c50 EnsureEntryInternal__FRC6RefVar
// The entry frame made all internal objects (EnsureInternal) - with its
// _proto (the soup's persistent frame) taken off for the duration so
// that it is not copied too.
Ref
EnsureEntryInternal(RefArg entry)
{
	RefVar proto(GetFrameSlotRef(entry, RSSYM_proto));
	if ((Ref) proto == NILREF)
		return EnsureInternal(entry);
	RefVar internal;
	SetFrameSlot(entry, RSSYM_proto, RefVar(NILREF));
	newton_try
	{
		internal = EnsureInternal(entry);
		SetFrameSlot(internal, RSSYM_proto, proto);
		SetFrameSlot(entry, RSSYM_proto, proto);
	}
	newton_catch_all
	{
		SetFrameSlot(entry, RSSYM_proto, proto);
		rethrow;
	}
	end_try;
	return internal;
}


// ROM 0x002d9d7c EntryChangeCommon__FRC6RefVari
// The entry (in memory) written back to its store object, the soup's
// indexes updated from the old to the new keys, the soup's cursors told
// (TCursor::EntryChanged: one on the entry finds it again when its keys
// changed, or tests it again when its tags did) - by flags: _modTime set,
// a changed _uniqueID reinstated, the tags index updated, the frame
// written verbatim and dropped after.
void
EntryChangeCommon(RefArg entry, int flags)
{
	CheckEntry(entry);
	if (flags & kEntryChangeSetModTime)
		SetFrameSlot(entry, RSSYM_modtime, RefVar(MAKEINT(RealClock() & 0x1fffffff)));
	RefVar soup(FaultBlockHandler(entry));
	RefVar object(FaultBlockObject(entry));
	TStoreWrapper* wrapper = FaultBlockStore(entry);
	PSSId id = FaultBlockId(entry);
	CheckWriteProtect(wrapper->Store());
	Boolean tagsChanged = false;
	RefVar soupPersistent(GetFrameSlotRef(soup, RSSYM_proto));
	if ((Ref) soupPersistent == NILREF)
		Throw(exStoreError, (void*) kNSErrSoupGone, nil);
	if ((Ref) object == NILREF)
		return;
	if ((flags & kEntryChangeVerbatim) == 0)
		object = EnsureEntryInternal(object);
	CDynamicArray* largeBinaries = nil;
	RefVar oldObject(LoadPermObject(wrapper, id, &largeBinaries));
	if (flags & kEntryChangeKeepUniqueID)
	{
		if (!EQRef(GetFrameSlotRef(object, RSSYM_uniqueid), GetFrameSlotRef(oldObject, RSSYM_uniqueid)))
			SetFrameSlot(object, RSSYM_uniqueid, RefVar(GetFrameSlotRef(oldObject, RSSYM_uniqueid)));
	}
	wrapper->LockStore();
	Boolean duplicated = false;
	Boolean indexesChanged;
	newton_try
	{
		StorePermObject(object, wrapper, id, largeBinaries, &duplicated);
		if (duplicated)
			flags |= kEntryChangeVerbatim;
		tagsChanged = (flags & kEntryChangeUpdateTags) != 0;
		indexesChanged = UpdateIndexes(soup, object, oldObject, id, &tagsChanged);
		SoupChanged(soupPersistent, true);
	}
	newton_catch_all
	{
		wrapper->Abort();
		AbortSoupIndexes(soup);
		rethrow;
	}
	end_try;
	wrapper->UnlockStore();
	if (indexesChanged || tagsChanged)
		EachSoupCursorEntryChanged(soup, entry, indexesChanged, tagsChanged);
	if (flags & kEntryChangeVerbatim)
		FaultBlockSlots(entry)[kFaultBlockObjectSlot] = NILREF;
}


// ROM 0x002da1c4 EntryChange__FRC6RefVar
Ref
EntryChange(RefArg entry)
{
	if (IsProxyEntry(entry))
		return ForwardEntryMessage(entry, RSSYMentrychange);
	EntryChangeCommon(entry, kEntryChangeKeepUniqueID | kEntryChangeSetModTime | kEntryChangeUpdateTags);
	return NILREF;
}


// ROM 0x002da154 EntryChangeWithModTime__FRC6RefVar
// The frame's own _modTime kept.
Ref
EntryChangeWithModTime(RefArg entry)
{
	if (IsProxyEntry(entry))
		return ForwardEntryMessage(entry, RSSYMentrychangewithmodtime);
	EntryChangeCommon(entry, kEntryChangeKeepUniqueID | kEntryChangeUpdateTags);
	return NILREF;
}


// ROM 0x002da18c EntryChangeVerbatim__FRC6RefVar
// _modTime and _uniqueID kept as they are in the frame.
Ref
EntryChangeVerbatim(RefArg entry)
{
	if (IsProxyEntry(entry))
		return ForwardEntryMessage(entry, RSSYMentrychangeverbatim);
	EntryChangeCommon(entry, kEntryChangeUpdateTags);
	return NILREF;
}


// ROM 0x002da1fc EntryFlush__FRC6RefVar
// EntryChange, the frame dropped from memory after.
Ref
EntryFlush(RefArg entry)
{
	if (IsProxyEntry(entry))
		return ForwardEntryMessage(entry, RSSYMentrychange);
	EntryChangeCommon(entry, kEntryChangeKeepUniqueID | kEntryChangeSetModTime | kEntryChangeUpdateTags | kEntryChangeVerbatim);
	return NILREF;
}


// ROM 0x002da234 EntryFlushWithModTime__FRC6RefVar
Ref
EntryFlushWithModTime(RefArg entry)
{
	if (IsProxyEntry(entry))
		return ForwardEntryMessage(entry, RSSYMentrychange);
	EntryChangeCommon(entry, kEntryChangeKeepUniqueID | kEntryChangeUpdateTags | kEntryChangeVerbatim);
	return NILREF;
}


// ROM 0x002da26c EntryRemoveFromSoup__FRC6RefVar
// The entry taken out of its soup: its keys out of the indexes, the
// block out of the cache and replaced by the plain frame, the store
// object deleted; the soup's lastUID updated when it was the last.
void
EntryRemoveFromSoup(RefArg entry)
{
	CheckEntry(entry);
	if (IsProxyEntry(entry))
	{
		ForwardEntryMessage(entry, RSSYMentryremovefromsoup);
		return;
	}
	RefVar soup(FaultBlockHandler(entry));
	TStoreWrapper* wrapper = FaultBlockStore(entry);
	if (wrapper == nil)
		Throw(exStoreError, (void*) kNSErrSoupGone, nil);
	PSSId id = FaultBlockId(entry);
	RefVar cached(FaultBlockObject(entry));
	CheckWriteProtect(wrapper->Store());
	EachSoupCursorDo(soup, kSoupCursorEntryRemoved, entry);
	RefVar soupPersistent(GetFrameSlotRef(soup, RSSYM_proto));
	RefVar object;
	if ((Ref) cached == NILREF || EntryDirty(entry))
		object = LoadPermObject(wrapper, id, nil);
	else
		object = cached;
	wrapper->LockStore();
	newton_try
	{
		AlterIndexes(false, soup, object, id);
		RefVar cache(GetFrameSlotRef(soup, RSSYMcache));
		DeleteEntryFromCache(cache, entry);
		ReplaceObjectRef(entry, (Ref) cached == NILREF ? (Ref) object : (Ref) cached);
		DeletePermObject(wrapper, id);
		RefVar nextUID(GetFrameSlotRef(soup, RSSYMindexnextuid));
		if (RINT(GetFrameSlotRef(entry, RSSYM_uniqueid)) + 1 == RINT(nextUID))
		{
			SetFrameSlot(soupPersistent, RSSYMlastuid, nextUID);
			WriteFaultBlock(soupPersistent);
		}
		SoupChanged(soupPersistent, true);
	}
	newton_catch_all
	{
		wrapper->Abort();
		AbortSoupIndexes(soup);
		rethrow;
	}
	end_try;
	wrapper->UnlockStore();
}


// ROM 0x002da588 EntryReplaceCommon__FRC6RefVarT1i
// The entry's frame replaced by newEntry's (a plain frame is cloned, a
// fault block gives up its frame and leaves its cache), written, and
// newEntry made a forwarding object for entry.
Ref
EntryReplaceCommon(RefArg entry, RefArg newEntry, int withModTime)
{
	CheckEntry(entry);
	if (IsProxyEntry(entry))
		return ForwardEntryMessage(entry, withModTime ? RSSYMentryreplacewithmodtime : RSSYMentryreplace, newEntry);
	if (!ISPTR(newEntry) || (ObjectFlags(newEntry) & kObjFrame) == 0)
		Throw(exBadType, (void*) kNSErrNotAFrame, nil);
	if (EQRef(entry, newEntry))
		return NILREF;
	CheckWriteProtect(FaultBlockStore(entry)->Store());
	if (!IsFaultBlock(newEntry))
		FaultBlockSlots(entry)[kFaultBlockObjectSlot] = Clone(newEntry);
	else
	{
		ObjectPtr(newEntry);					// read
		RefVar otherSoup(FaultBlockHandler(newEntry));
		RefVar object(FaultBlockObject(newEntry));
		RefVar cache(GetFrameSlotRef(otherSoup, RSSYMcache));
		DeleteEntryFromCache(cache, newEntry);
		FaultBlockSlots(entry)[kFaultBlockObjectSlot] = object;
	}
	if (withModTime)
		EntryChangeWithModTime(entry);
	else
		EntryChange(entry);
	ReplaceObjectRef(newEntry, entry);
	return NILREF;
}


// ROM 0x002da860 EntryReplace__FRC6RefVarT1
Ref
EntryReplace(RefArg entry, RefArg newEntry)
{
	return EntryReplaceCommon(entry, newEntry, 0);
}


// ROM 0x002da868 EntryReplaceWithModTime__FRC6RefVarT1
Ref
EntryReplaceWithModTime(RefArg entry, RefArg newEntry)
{
	return EntryReplaceCommon(entry, newEntry, 1);
}


// ROM 0x002da870 EntryUndoChanges__FRC6RefVar
// The frame in memory dropped: the store's copy stands.
Ref
EntryUndoChanges(RefArg entry)
{
	CheckEntry(entry);
	if (IsProxyEntry(entry))
		return ForwardEntryMessage(entry, RSSYMentryundochanges);
	AbortLargeBinaries(entry);
	if (IsFaultBlock(entry))
		FaultBlockSlots(entry)[kFaultBlockObjectSlot] = NILREF;
	return NILREF;
}


// ROM 0x002da8e4 EntryCopy__FRC6RefVarT1
// A copy of the entry added to soup.
Ref
EntryCopy(RefArg entry, RefArg soup)
{
	if (IsProxyEntry(entry))
		return ForwardEntryMessage(entry, RSSYMentrycopy, soup);
	RefVar copy(Clone(entry));
	RefVar added(SoupAdd(soup, copy));
	UncacheIfFaultBlock(added);
	return added;
}


// ROM 0x002da968 EntryMove__FRC6RefVarT1
// The entry added to soup and removed from its own; the block becomes a
// forwarder to the new entry.
void
EntryMove(RefArg entry, RefArg soup)
{
	CheckEntry(entry);
	if (IsProxyEntry(entry))
	{
		ForwardEntryMessage(entry, RSSYMentrymove, soup);
		return;
	}
	RefVar toStore(PlainSoupGetStore(soup));
	CheckWriteProtect(toStore);
	ObjectPtr(entry);							// read
	RefVar fromSoup(FaultBlockHandler(entry));
	TStoreWrapper* wrapper = FaultBlockStore(entry);
	if (wrapper == nil)
		Throw(exStoreError, (void*) kNSErrSoupGone, nil);
	CheckWriteProtect(wrapper->Store());
	RefVar object(FaultBlockObject(entry));
	FaultBlockSlots(entry)[kFaultBlockObjectSlot] = NILREF;
	if (EQRef(fromSoup, soup))
		return;
	PlainSoupAdd(soup, object);
	RefVar name(GetFrameSlotRef(fromSoup, RSSYMthename));
	RefVar unionSoup(FindSoupInCache(RefVar(gUnionSoups), name));
	if ((Ref) unionSoup != NILREF)
		EachSoupCursorDo(unionSoup, kSoupCursorEntryMoved, entry, object);
	EntryRemoveFromSoup(entry);
	ReplaceObjectRef(entry, object);
}


// ROM 0x002dab6c EntryDirty1__FP6ObjectP14EntryDirtyLink
// Whether the object or any object it holds (not one already on the
// chain of objects being looked at) is dirty.
struct EntryDirtyLink
{
	ObjHeader*		fObject;
	EntryDirtyLink*	fNext;
};

static Boolean
EntryDirty1(ObjHeader* o, EntryDirtyLink* chain)
{
	for (EntryDirtyLink* link = chain; link != nil; link = link->fNext)
		if (link->fObject == o)
			return false;
	if (ObjFlags(o) & kObjDirty)
		return true;
	if (ObjFlags(o) & kObjSlotted)
	{
		EntryDirtyLink here = { o, chain };
		long count = ObjArrayLength(o);
		Ref* slots = ObjArraySlots(o);
		for (long i = 0; i < count; i++)
		{
			if (ISPTR(slots[i]) && EntryDirty1(OBJ(slots[i]), &here))
				return true;
		}
	}
	return false;
}


// ROM 0x002dac24 EntryDirty__Fl
// Whether the entry frame in memory has been written to.
Boolean
EntryDirty(Ref entry)
{
	if (!ISPTR(entry))
		return false;
	if (IsFaultBlock(entry) && FaultBlockObject(entry) == NILREF)
		return false;
	return EntryDirty1(OBJ(entry), nil);
}


// ROM 0x002dac84 EntryIsResident__Fl
Boolean
EntryIsResident(Ref entry)
{
	CheckEntry(RefVar(entry));
	return FaultBlockObject(entry) != NILREF;
}


// ROM 0x002dacd4 EntryValid__FRC6RefVar
// Whether the entry's store is still here (a package store counts).
Boolean
EntryValid(RefArg entry)
{
	if (!IsFaultBlock(entry))
		return false;
	Ref storeSlot = FaultBlockSlots(entry)[kFaultBlockStoreSlot];
	if (storeSlot == NILREF)
		return ForwardEntryMessage(entry, RSSYMentryvalid) != NILREF;
	if (storeSlot == 0)
		return false;
	TStore* store = ((TStoreWrapper*) storeSlot)->Store();
	if (IsValidStore(store))
		return true;
	for (long i = Length(gPackageStores) - 1; i >= 0; i--)
	{
		Ref packageStore = GetArraySlotRef(gPackageStores, i);
		if (((TStoreWrapper*) GetFrameSlotRef(packageStore, RSSYMstore))->Store() == store)
			return true;
	}
	return false;
}


// ROM 0x002dadb4 GetEntry__FRC6RefVarUl
// The soup's entry for store object id: from its cache, else a new fault
// block put there.
Ref
GetEntry(RefArg soup, PSSId id)
{
	RefVar cache(GetFrameSlotRef(soup, RSSYMcache));
	RefVar entry(FindEntryInCache(cache, id));
	if ((Ref) entry == NILREF)
	{
		TStoreWrapper* wrapper = (TStoreWrapper*) GetFrameSlotRef(soup, RSSYMtstore);
		entry = MakeFaultBlock(soup, wrapper, id);
		PutEntryIntoCache(cache, entry);
	}
	return entry;
}


// ROM 0x002daef0 CheckProxyEntry__FRC6RefVar
void
CheckProxyEntry(RefArg entry)
{
	CheckEntry(entry);
	if (!IsProxyEntry(entry))
		Throw(exStoreError, (void*) kNSErrNotAProxyEntry, nil);
}


// ROM 0x002daf58 IsProxyEntry__FRC6RefVar
Boolean
IsProxyEntry(RefArg entry)
{
	return FaultBlockSlots(entry)[kFaultBlockStoreSlot] == NILREF;
}


// ROM 0x002daf84 EntryCachedObject__FRC6RefVar
Ref
EntryCachedObject(RefArg entry)
{
	CheckProxyEntry(entry);
	return FaultBlockObject(entry);
}


// ROM 0x002dafac EntrySetCachedObject__FRC6RefVarT1
void
EntrySetCachedObject(RefArg entry, RefArg object)
{
	CheckProxyEntry(entry);
	FaultBlockSlots(entry)[kFaultBlockObjectSlot] = object;
}


// ROM 0x002dafe0 EntryHandler__FRC6RefVar
Ref
EntryHandler(RefArg entry)
{
	CheckProxyEntry(entry);
	return FaultBlockHandler(entry);
}


// ROM 0x002db008 EntrySetHandler__FRC6RefVarT1
void
EntrySetHandler(RefArg entry, RefArg handler)
{
	CheckProxyEntry(entry);
	FaultBlockSlots(entry)[kFaultBlockHandlerSlot] = handler;
}


// ROM 0x002db1c4 ForwardEntryMessage__FRC6RefVarT1
// The message sent to the entry's handler with the entry as argument.
Ref
ForwardEntryMessage(RefArg entry, RefArg message)
{
	RefVar args(AllocateArray(RSSYMarray, 1));
	SetArraySlotRef(args, 0, entry);
	RefVar handler(FaultBlockHandler(entry));
	return DoMessage(handler, message, args);
}


// ROM 0x002db254 ForwardEntryMessage__FRC6RefVarN21
Ref
ForwardEntryMessage(RefArg entry, RefArg message, RefArg arg)
{
	RefVar args(AllocateArray(RSSYMarray, 2));
	SetArraySlotRef(args, 0, entry);
	SetArraySlotRef(args, 1, arg);
	RefVar handler(FaultBlockHandler(entry));
	return DoMessage(handler, message, args);
}


// ROM 0x002db304 IsSoupEntry__FRC6RefVar
Boolean
IsSoupEntry(RefArg object)
{
	return ISPTR(object) && ObjClass(NoFaultObjectPtr(object)) == kFaultBlockClass;
}


// ROM 0x002db310 EntrySoup__FRC6RefVar
Ref
EntrySoup(RefArg entry)
{
	CheckEntry(entry);
	if (IsProxyEntry(entry))
		return ForwardEntryMessage(entry, RSSYMentrysoup);
	return FaultBlockHandler(entry);
}


/*------------------------------------------------------------------------------
	S i z e s   a n d   i d s
	Read from the store object's header (StoreObject.h).
------------------------------------------------------------------------------*/

// ROM 0x002e064c GetLargeObjectSize__FP13TStoreWrapperUllPv
// EachLargeObjectDo's callback for EntrySize: what the large binary takes
// up on the store added to the size.
static Boolean
GetLargeObjectSize(TStoreWrapper* wrapper, PSSId id, long /*arg*/, void* size)
{
	*(long*) size += StorageSizeOfLargeObject(wrapper->Store(), id);
	return false;
}


// The store object's header.
static void
ReadEntryHeader(TStoreWrapper* wrapper, PSSId id, StoreObjectHeader* header)
{
	UByte bytes[kStoreObjectHeaderSize];
	OSErrIf(wrapper->Store()->Read(id, 0, (char*) bytes, kStoreObjectHeaderSize));
	header->ReadFrom(bytes);
}


// ROM 0x002e0678 EntrySize__FUlP13TStoreWrapperUc
// The store object's size with its text object's (and its large
// binaries' when asked).
long
EntrySize(PSSId id, TStoreWrapper* wrapper, Boolean withLargeBinaries)
{
	long size;
	OSErrIf(wrapper->Store()->GetObjectSize(id, &size));
	StoreObjectHeader header;
	ReadEntryHeader(wrapper, id, &header);
	if (header.fTextBlockId != 0)
	{
		long textSize = 0;
		OSErrIf(wrapper->Store()->GetObjectSize(header.fTextBlockId, &textSize));
		size += textSize;
	}
	if (withLargeBinaries && (header.fFlags & kSOFlagsHasLargeBinaries))
	{
		TStoreObjectReader reader(wrapper, id, nil);
		reader.EachLargeObjectDo(GetLargeObjectSize, &size);
	}
	return size;
}


// ROM 0x002e07b4 EntrySize__FRC6RefVar
long
EntrySize(RefArg entry)
{
	CheckEntry(entry);
	if (!IsProxyEntry(entry))
		return EntrySize(FaultBlockId(entry), FaultBlockStore(entry), true);
	RefVar result(ForwardEntryMessage(entry, RSSYMentrysize));
	if (!ISINT(result))
		ThrowBadTypeWithFrameData(kNSErrNotAnInteger, result);
	return RINT(result);
}


// ROM 0x002e0858 EntrySizeWithoutVBOs__FRC6RefVar
long
EntrySizeWithoutVBOs(RefArg entry)
{
	CheckEntry(entry);
	if (!IsProxyEntry(entry))
		return EntrySize(FaultBlockId(entry), FaultBlockStore(entry), false);
	RefVar result(ForwardEntryMessage(entry, RSSYMentrysize));
	if (!ISINT(result))
		ThrowBadTypeWithFrameData(kNSErrNotAnInteger, result);
	return RINT(result);
}


// ROM 0x002e08fc EntryTextSize__FRC6RefVar
// The size of the entry's (compressed) text object.
long
EntryTextSize(RefArg entry)
{
	CheckEntry(entry);
	if (!IsProxyEntry(entry))
	{
		TStoreWrapper* wrapper = FaultBlockStore(entry);
		StoreObjectHeader header;
		ReadEntryHeader(wrapper, FaultBlockId(entry), &header);
		long size = 0;
		if (header.fTextBlockId != 0)
			OSErrIf(wrapper->Store()->GetObjectSize(header.fTextBlockId, &size));
		return size;
	}
	RefVar result(ForwardEntryMessage(entry, RSSYMentrytextsize));
	if (!ISINT(result))
		ThrowBadTypeWithFrameData(kNSErrNotAnInteger, result);
	return RINT(result);
}


// ROM 0x002e09f0 EntryUniqueID__FRC6RefVar
// The _uniqueID as the store object's header has it (no need to read the
// frame).
long
EntryUniqueID(RefArg entry)
{
	CheckEntry(entry);
	if (!IsProxyEntry(entry))
	{
		StoreObjectHeader header;
		ReadEntryHeader(FaultBlockStore(entry), FaultBlockId(entry), &header);
		return header.fUniqueId;
	}
	RefVar result(ForwardEntryMessage(entry, RSSYMentryuniqueid));
	if (!ISINT(result))
		ThrowBadTypeWithFrameData(kNSErrNotAnInteger, result);
	return RINT(result);
}


// ROM 0x002e0ab8 EntryModTime__FRC6RefVar
long
EntryModTime(RefArg entry)
{
	CheckEntry(entry);
	if (!IsProxyEntry(entry))
	{
		StoreObjectHeader header;
		ReadEntryHeader(FaultBlockStore(entry), FaultBlockId(entry), &header);
		return header.fModTime == 0xffffffff ? 0 : (long) header.fModTime;
	}
	RefVar result(ForwardEntryMessage(entry, RSSYMentrymodtime));
	if (!ISINT(result))
		ThrowBadTypeWithFrameData(kNSErrNotAnInteger, result);
	return RINT(result);
}


/*------------------------------------------------------------------------------
	E p h e m e r a l s
------------------------------------------------------------------------------*/

// ROM 0x002dbaa4 StoreWritable__FP6TStore
Boolean
StoreWritable(TStore* store)
{
	if (store->IsROM())
		return false;
	Boolean readOnly = false;
	store->IsReadOnly(&readOnly);
	return !readOnly;
}


// ROM 0x002dba10 CanCreateLargeObjectsOnStore__FP6TStore
// The store's implementation has the 'LOBJ' capability.
Boolean
CanCreateLargeObjectsOnStore(TStore* store)
{
	const TClassInfo* info = GetStoreClassInfo(store);
	return info != nil && info->GetCapability("LOBJ") != nil;
}


// (SetupEphemeralTracker and the tracker itself: Ephemerals.cpp)


void
InitEntries(void)
{
	gFollowFaultBlockProc = StoreFollowFaultBlock;
}
