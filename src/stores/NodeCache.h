/*
	File:		stores/NodeCache.h

	Contains:	TNodeCache, the cache of B-tree nodes a store's soup indexes
				(TSoupIndex) read and write: a handle of entries, each with a
				node's id, its buffer (512 bytes, the node size), whether it
				is a duplicate-key node, whether it is dirty, its use stamp
				(the least recently used entry is reused), the index it
				belongs to and whether it is in use.  A TStoreWrapper has one
				for all the indexes on its store.

	The ROM's layout: 0x10 (the entries 0x14 each).
*/

#ifndef __NODECACHE_H
#define __NODECACHE_H

#ifndef __NEWTONMEMORY_H
#include "NewtonMemory.h"
#endif

class TSoupIndex;
struct NodeHeader;

const long kNodeCacheInitialEntries = 3;
const long kNodeCacheShrinkEntries = 8;		// Commit trims the cache back to this many
const long kNodeCacheNodeSize = 0x200;

struct NodeCacheEntry
{
	ULong		fId;			// +0x00  the node's object id (0: free)
	NodeHeader*	fNode;			// +0x04  its buffer
	Boolean		fIsDup;			// +0x08  a duplicate-key node
	Boolean		fIsDirty;		// +0x09
	Boolean		fInUse;			// +0x0a
	long		fStamp;			// +0x0c  fUseCount when last used
	TSoupIndex*	fIndex;			// +0x10
};

class TNodeCache
{
public:
				TNodeCache();
				~TNodeCache();

	NodeHeader*	FindNode(TSoupIndex* index, ULong id);		// nil when not cached; marks it in use
	NodeHeader*	RememberNode(TSoupIndex* index, ULong id, long size, int isDup, int isDirty);
	void		DeleteNode(ULong id);						// forgotten and deleted from the store
	void		ForgetNode(ULong id);
	void		DirtyNode(NodeHeader* node);
	void		Commit(TSoupIndex* index);					// the index's dirty nodes written; the cache trimmed
	void		Reuse(TSoupIndex* index);					// the index's nodes marked in use again
	void		Abort(TSoupIndex* index);					// the index's nodes dropped
	void		Clear(void);

	long		fNumEntries;		// +0x00
	Handle		fEntries;			// +0x04  NodeCacheEntry[fNumEntries]
	long		fUseCount;			// +0x08  the stamp clock
	long		fModCount;			// +0x0c  bumped whenever an entry changes (the indexes' iterators watch it)
};

#endif	/* __NODECACHE_H */
