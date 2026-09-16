/*
	File:		stores/NodeCache.cpp

	Contains:	TNodeCache (NodeCache.h), the soup indexes' B-tree node cache.
				Commit and DeleteNode, which write through the index, are in
				SoupIndex.cpp.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "NodeCache.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"

extern const ExceptionName exOutOfMemory;		// "evt.ex.outofmem"


static inline NodeCacheEntry*
Entries(Handle h)
{
	return (NodeCacheEntry*) *h;
}


// ROM 0x002c3ad8 __ct__10TNodeCacheFv
// Three entries, each with a node-sized buffer.
TNodeCache::TNodeCache()
{
	fNumEntries = kNodeCacheInitialEntries;
	fEntries = NewHandle(kNodeCacheInitialEntries * sizeof(NodeCacheEntry));
	if (fEntries == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	HLock(fEntries);
	NodeCacheEntry* entry = Entries(fEntries);
	for (long i = 0; i < fNumEntries; i++, entry++)
	{
		entry->fId = 0;
		entry->fStamp = 0;
		entry->fIndex = nil;
		entry->fInUse = false;
		entry->fNode = (NodeHeader*) NewPtr(kNodeCacheNodeSize);
		if (entry->fNode == nil)
			Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	}
	HUnlock(fEntries);
	fUseCount = 0;
	fModCount = 0;
}


// ROM 0x002c3c00 __dt__10TNodeCacheFv
TNodeCache::~TNodeCache()
{
	HLock(fEntries);
	NodeCacheEntry* entry = Entries(fEntries);
	for (long i = 0; i < fNumEntries; i++)
		DisposPtr((Ptr) entry[i].fNode);
	HUnlock(fEntries);
	DisposHandle(fEntries);
}


// ROM 0x002c3c78 FindNode__10TNodeCacheFP10TSoupIndexUl
// The cached node with this id, marked in use by index; nil for none.
NodeHeader*
TNodeCache::FindNode(TSoupIndex* index, ULong id)
{
	NodeCacheEntry* entry = Entries(fEntries);
	NodeCacheEntry* end = entry + fNumEntries;
	for (; entry < end; entry++)
		if (entry->fId == id)
		{
			entry->fIndex = index;
			entry->fInUse = true;
			entry->fStamp = ++fUseCount;
			return entry->fNode;
		}
	return nil;
}


// ROM 0x002c3ce0 RememberNode__10TNodeCacheFP10TSoupIndexUlliT4
// An entry for a node of size bytes: a free one, else the least recently
// used one not in use, else a new one; ==> its buffer.
NodeHeader*
TNodeCache::RememberNode(TSoupIndex* index, ULong id, long size, int isDup, int isDirty)
{
	NodeCacheEntry* entries = Entries(fEntries);
	long chosen = -1;
	long oldest = fUseCount + 1;
	Boolean reusing = true;
	for (long i = 0; i < fNumEntries; i++)
	{
		if (entries[i].fId == 0)
		{
			reusing = false;
			chosen = i;
		}
		if (!entries[i].fInUse && entries[i].fStamp < oldest)
		{
			chosen = i;
			oldest = entries[i].fStamp;
		}
	}
	if (chosen == -1)
	{
		// every entry is in use: one more
		SetHandleSize(fEntries, (fNumEntries + 1) * sizeof(NodeCacheEntry));
		if (MemError() != noErr)
			Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
		NodeHeader* node = (NodeHeader*) NewPtr(size);
		if (node == nil)
		{
			SetHandleSize(fEntries, fNumEntries * sizeof(NodeCacheEntry));
			Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
		}
		chosen = fNumEntries++;
		entries = Entries(fEntries);
		entries[chosen].fNode = node;
	}
	else if (reusing)
		fModCount++;
	NodeCacheEntry* entry = &entries[chosen];
	entry->fId = id;
	entry->fIsDup = (Boolean) isDup;
	entry->fIsDirty = (Boolean) isDirty;
	entry->fStamp = ++fUseCount;
	entry->fIndex = index;
	entry->fInUse = true;
	return entry->fNode;
}


// ROM 0x002c3f20 ForgetNode__10TNodeCacheFUl
void
TNodeCache::ForgetNode(ULong id)
{
	NodeCacheEntry* entry = Entries(fEntries);
	NodeCacheEntry* end = entry + fNumEntries;
	for (; entry < end; entry++)
		if (entry->fId == id)
		{
			entry->fId = 0;
			entry->fStamp = 0;
			entry->fIndex = nil;
			entry->fInUse = false;
			fModCount++;
		}
}


// ROM 0x002c3f7c DirtyNode__10TNodeCacheFP10NodeHeader
void
TNodeCache::DirtyNode(NodeHeader* node)
{
	NodeCacheEntry* entry = Entries(fEntries);
	NodeCacheEntry* end = entry + fNumEntries;
	for (; entry < end; entry++)
		if (entry->fNode == node)
		{
			entry->fIsDirty = true;
			fModCount++;
		}
}


// ROM 0x002c40dc Reuse__10TNodeCacheFP10TSoupIndex
void
TNodeCache::Reuse(TSoupIndex* index)
{
	NodeCacheEntry* entry = Entries(fEntries);
	NodeCacheEntry* end = entry + fNumEntries;
	for (; entry < end; entry++)
		if (entry->fIndex == index)
			entry->fInUse = true;
}


// ROM 0x002c4118 Abort__10TNodeCacheFP10TSoupIndex
void
TNodeCache::Abort(TSoupIndex* index)
{
	NodeCacheEntry* entry = Entries(fEntries);
	NodeCacheEntry* end = entry + fNumEntries;
	for (; entry < end; entry++)
		if (entry->fIndex == index)
		{
			entry->fIndex = nil;
			entry->fInUse = false;
			entry->fId = 0;
			entry->fIsDirty = false;
		}
	fModCount++;
}


// ROM 0x002c4174 Clear__10TNodeCacheFv
void
TNodeCache::Clear(void)
{
	NodeCacheEntry* entry = Entries(fEntries);
	NodeCacheEntry* end = entry + fNumEntries;
	for (; entry < end; entry++)
	{
		entry->fIndex = nil;
		entry->fInUse = false;
		entry->fId = 0;
		entry->fStamp = 0;
		entry->fIsDirty = false;
	}
	fModCount++;
}
