/*
	File:		frames/GC.cpp

	Contains:	The object heap's garbage collector: marking by pointer
				reversal, weak arrays, sweeping and compaction (locked objects
				stay, the RefHandle table grows into the space below it),
				declawing of refs into packages that have gone, and the
				registration of GC roots and hooks.

	Reconstructed from the MP2x00 US ROM (0x002e3280-0x002e4388); each
	function cites its origin.
*/

#include "ObjectHeap.h"
#include "NSErrors.h"
#include "OSErrors.h"
#include "NewtonMemory.h"
#include "REPTranslators.h"		// gREPout, where the verbose report goes

#include <stdio.h>

Handle	gGCRoots = nil;
Handle	gDIYGCEntries = nil;
Handle	gGCProcEntries = nil;

struct DIYGCEntry
{
	void*			fRefCon;
	DIYGCFuncPtr	fMark;
	DIYGCFuncPtr	fUpdate;
};

struct GCProcEntry
{
	void*			fRefCon;
	GCProcPtr		fProc;
};


/* -------------------------------------------------------------------------------
	Marking
------------------------------------------------------------------------------- */

// ROM 0x002e3318 Mark__11TObjectHeapFl
// Mark everything reachable from r without a stack (Deutsch-Schorr-Waite):
// on the way down each object's slot being followed holds the ref to its
// parent and its GC word the slot's index; on the way back up the slot is
// restored and the next one taken.  A weak array is marked but not
// followed: it is chained (through its class slot) for CleanUpWeakChain.
void
TObjectHeap::Mark(Ref r)
{
	if (RTAG(r) != kTagPointer || !InHeap(PTRVALUE(r)))
		return;
	Ref parent = NILREF;
	Ref current = r;
	for (;;)
	{
		ObjHeader* o = PTRVALUE(current);
		if (RTAG(current) == kTagPointer && InHeap(o) && (o->fSizeAndFlags & kObjMarked) == 0)
		{
			o->fSizeAndFlags |= kObjMarked;
			if (ObjClass(o) != kWeakArrayClass)
			{
				if (ObjIsIndirect(o))
					ObjIndirectProcs(o)->fMark(ObjIndirectData(o));
				o->fGCStuff &= kObjLockMask;						// slot 0 is being followed
				Ref child = ObjClass(o);
				ObjClass(o) = parent;
				parent = current;
				current = child;
				continue;
			}
			ObjClass(o) = (Ref) fWeakChain;
			fWeakChain = o;
		}
		// back up: current is done (or is not an object)
		for (;;)
		{
			if (parent == NILREF)
				return;
			ObjHeader* p = PTRVALUE(parent);
			ULong index = p->fGCStuff & kObjGCIndexMask;
			Ref* slots = ObjSlots(p);
			Ref grandparent = slots[index];
			slots[index] = current;
			if ((p->fSizeAndFlags & kObjSlotted) != 0 && (long) index + 1 < (long) ((ObjSize(p) - sizeof(ObjHeader)) / sizeof(Ref)))
			{
				p->fGCStuff = (index + 1) | (p->fGCStuff & kObjLockMask);
				current = slots[index + 1];
				slots[index + 1] = grandparent;
				break;												// down into the next slot
			}
			current = parent;										// the parent is done too
			parent = grandparent;
		}
	}
}


// ROM 0x002e3460 CleanUpWeakChain__11TObjectHeapFv
// The slots of the weak arrays reached: a ref to an unmarked heap object is
// replaced by NILREF; forwarding is followed to find the object (the ref
// itself is updated later by SweepAndCompact).
void
TObjectHeap::CleanUpWeakChain(void)
{
	for (ObjHeader* weak = fWeakChain; weak != nil; )
	{
		long count = (long) ((ObjSize(weak) - sizeof(ObjHeader)) / sizeof(Ref));
		Ref* slots = ObjSlots(weak);
		for (long i = 1; i < count; i++)
		{
			Ref r = slots[i];
			while (RTAG(r) == kTagPointer && (ObjFlags(PTRVALUE(r)) & kObjForward) != 0)
				r = ObjClass(PTRVALUE(r));
			if (RTAG(r) == kTagPointer && InHeap(PTRVALUE(r)) && (ObjFlags(PTRVALUE(r)) & kObjMarked) == 0)
				slots[i] = NILREF;
		}
		ObjHeader* next = (ObjHeader*) ObjClass(weak);
		ObjClass(weak) = kWeakArrayClass;
		weak = next;
	}
}


/* -------------------------------------------------------------------------------
	Sweeping
------------------------------------------------------------------------------- */

// ROM 0x002e3514 UpdateRef__11TObjectHeapFl
// What a ref becomes after this GC: forwarding followed; NILREF for a heap
// object that was not marked; the object's new address (its GC word) unless
// it is locked.  While declawing, a ref in a registered range becomes
// kDeclawedRef instead.
Ref
TObjectHeap::UpdateRef(Ref r)
{
	if (fDeclawing)
	{
		if (fDeclawingRanges->InAnyRange(r))
			return kDeclawedRef;
		return r;
	}
	if (RTAG(r) != kTagPointer || !InHeap(PTRVALUE(r)))
		return r;
	ObjHeader* o = PTRVALUE(r);
	while ((o->fSizeAndFlags & kObjForward) != 0)
	{
		Ref next = ObjClass(o);
		if (RTAG(next) != kTagPointer)
			return next;
		o = PTRVALUE(next);
	}
	Ref found = MAKEPTR(o);
	Boolean inHeap = InHeap(o);
	if ((o->fSizeAndFlags & kObjMarked) == 0 && inHeap)
		return NILREF;
	if ((o->fSizeAndFlags & kObjLocked) != 0)
		return found;
	if (inHeap)
		return (Ref) o->fGCStuff;
	return found;
}


// ROM 0x002e35f0 SweepAndCompact__11TObjectHeapFv
// Three passes over the heap.  The first assigns each marked movable object
// its new address: objects slide down over the dead ones, first-fit into
// the gaps left below locked objects (up to 32 remembered; a gap that
// cannot be remembered is noted in its locked object's GC word to be freed
// later).  The RefHandle table at the top grows down into the last gap if
// ExpandObjectTable asked for it.  The second pass updates every slot, root
// and DIY entry through UpdateRef; the third moves the objects, frees dead
// indirect binaries' data and rebuilds the free blocks.
void
TObjectHeap::SweepAndCompact(void)
{
	struct Gap { char* fStart; ULong fSize; };
	const long kMaxGaps = 32;
	Gap gaps[kMaxGaps];
	long gapCount = 1;
	gaps[0].fStart = fStart;
	gaps[0].fSize = 0;

	// where everything goes
	for (ObjHeader* block = (ObjHeader*) fStart; block < fRefHandleTable; block = NextBlock(block))
	{
		ULong header = block->fSizeAndFlags;
		ULong size = AlignedSize(header >> kObjSizeShift);
		if ((header & kObjMarked) == 0 || (header & kObjForward) != 0)
			gaps[0].fSize += size;										// dead: part of the gap
		else if ((header & kObjLocked) == 0)
		{
			gaps[0].fSize += size;
			Gap* gap = &gaps[0];
			for (long i = 1; i < gapCount; i++)
				if (size <= gaps[i].fSize)
				{
					gap = &gaps[i];
					break;
				}
			gap->fSize -= size;
			block->fGCStuff = MAKEPTR(gap->fStart);
			gap->fStart += size;
		}
		else
		{
			block->fGCStuff &= kObjLockMask;
			if (gapCount < kMaxGaps)
				gaps[gapCount++] = gaps[0];
			else
			{
				long i;
				for (i = 1; i < gapCount; i++)
					if (gaps[i].fSize == 0)
					{
						gaps[i] = gaps[0];
						break;
					}
				if (i == gapCount)
					block->fGCStuff = gaps[0].fSize | (block->fGCStuff & kObjLockMask);	// freed when the objects move
			}
			gaps[0].fStart = (char*) block + size;
			gaps[0].fSize = 0;
		}
	}

	// the RefHandle table's growth, out of the gap below it
	ULong tableSize = ObjAlignedSize(fRefHandleTable);
	ULong growth = fRefHandleTableSize - tableSize;
	if (growth != 0)
	{
		if (gaps[0].fSize < growth)
		{
			fRefHandleTableSize = gaps[0].fSize + tableSize;
			growth = gaps[0].fSize;
			gaps[0].fSize = 0;
		}
		else
			gaps[0].fSize -= growth;
		if (growth < sizeof(RefHandle))
		{
			// DEVIATION: the ROM would chain zero new handles and write the
			// chain's end over the table's header; no growth is no growth
			gaps[0].fSize += growth;
			growth = 0;
			fRefHandleTableSize = tableSize;
		}
	}
	fRefHandleTable->fGCStuff = MAKEPTR(fRefHandleTable);

	// update every ref
	for (ObjHeader* block = (ObjHeader*) fStart; (char*) block < fEnd; block = NextBlock(block))
	{
		ULong header = block->fSizeAndFlags;
		if ((header & kObjMarked) != 0 && (header & kObjForward) == 0)
		{
			long count = ((header & kObjSlotted) == 0) ? 1 : (long) ((ObjSize(block) - sizeof(ObjHeader)) / sizeof(Ref));
			Ref* slots = ObjSlots(block);
			for (long i = 0; i < count; i++)
				slots[i] = UpdateRef(slots[i]);
			if ((header & (kObjSlotted | kObjFrame)) == kObjFrame)
				ObjIndirectProcs(block)->fUpdate(ObjIndirectData(block));
		}
	}
	if (gGCRoots != nil)
	{
		Ref** roots = (Ref**) *gGCRoots;
		long count = GetHandleSize(gGCRoots) / sizeof(Ref*);
		for (long i = 0; i < count; i++)
			*roots[i] = UpdateRef(*roots[i]);
	}
	if (gDIYGCEntries != nil)
	{
		DIYGCEntry* entries = (DIYGCEntry*) *gDIYGCEntries;
		long count = GetHandleSize(gDIYGCEntries) / sizeof(DIYGCEntry);
		for (long i = 0; i < count; i++)
			entries[i].fUpdate(entries[i].fRefCon);
	}

	// move the objects
	for (ObjHeader* block = (ObjHeader*) fStart; (char*) block < fEnd; )
	{
		ULong header = block->fSizeAndFlags;
		ObjHeader* next = NextBlock(block);
		if ((header & kObjMarked) == 0 || (header & kObjForward) != 0)
		{
			if ((header & (kObjSlotted | kObjFrame)) == kObjFrame)
				ObjIndirectProcs(block)->fDelete(ObjIndirectData(block));
		}
		else
		{
			block->fSizeAndFlags = header & ~(ULong) kObjMarked;
			if ((header & kObjLocked) == 0)
			{
				ObjHeader* dest = PTRVALUE((Ref) block->fGCStuff);
				if (block != dest)
					BlockMove(block, dest, header >> kObjSizeShift);
				dest->fGCStuff = 0;
			}
			else
			{
				ULong gap = block->fGCStuff & kObjGCIndexMask;
				if (gap != 0)
					MakeFreeBlock((ObjHeader*) ((char*) block - gap), gap);
			}
		}
		block = next;
	}

	// the grown RefHandle table: the new handles go before the old ones
	if (growth != 0)
	{
		fRefHandleTable = (ObjHeader*) ((char*) fRefHandleTable - growth);
		fRefHandleTable->fSizeAndFlags = (fRefHandleTableSize << kObjSizeShift) | kObjSlotted;
		fRefHandleTable->fGCStuff = 0;
		long count = growth / sizeof(RefHandle);
		RefHandle* handles = RefHandleTableEntries(fRefHandleTable);
		for (long i = 0; i < count; i++)
		{
			handles[i].ref = MAKEINT(i + 1);
			handles[i].stackPos = MAKEINT(-1);
		}
		handles[count - 1].ref = MAKEINT(-1);
		fFreeHandleIndex = 0;
	}

	for (long i = 0; i < gapCount; i++)
		if ((long) gaps[i].fSize > 0)
			MakeFreeBlock((ObjHeader*) gaps[i].fStart, gaps[i].fSize);
	fRover = (ObjHeader*) gaps[0].fStart;
}


/* -------------------------------------------------------------------------------
	GC
------------------------------------------------------------------------------- */

// ROM 0x002e3af4 GC__11TObjectHeapFv
// Mark from the RefHandle table, the roots and the DIY markers; the symbol
// table root is marked last, after GCTWA has dropped the symbols nothing
// else reached.  NOT YET RECONSTRUCTED: the frames function profiler's
// hooks around the collection (gFramesFunctionProfilingEnabled).  The
// verbose report (gVerboseGC) goes to the REP's out translator.
void
TObjectHeap::GC(void)
{
	if (fInGC)
		Throw(exFrames, (void*) kNSErrGCDuringGC, nil);
	fInGC = true;
	gCacheObjPtrRef = 0;
	gCacheLengthObj = 0;
	FindOffsetCacheClear();
	ULong freeSpace, largestFree;
	if (gVerboseGC)
	{
		Statistics(&freeSpace, &largestFree);
		gREPout->Print("[ GC! start %ld/%ld...", (long) freeSpace, (long) largestFree);
	}
	Boolean symbolTableIsRoot = false;
	fWeakChain = nil;
	Mark(MAKEPTR(fRefHandleTable));
	if (gGCRoots != nil)
	{
		Ref** roots = (Ref**) *gGCRoots;
		long count = GetHandleSize(gGCRoots) / sizeof(Ref*);
		for (long i = 0; i < count; i++)
		{
			if (roots[i] == &gSymbolTable)
				symbolTableIsRoot = true;
			else if (roots[i] != nil)
				Mark(*roots[i]);
		}
	}
	if (gDIYGCEntries != nil)
	{
		DIYGCEntry* entries = (DIYGCEntry*) *gDIYGCEntries;
		long count = GetHandleSize(gDIYGCEntries) / sizeof(DIYGCEntry);
		for (long i = 0; i < count; i++)
			entries[i].fMark(entries[i].fRefCon);
	}
	if (symbolTableIsRoot)
	{
		GCTWA();
		Mark(gSymbolTable);
	}
	CleanUpWeakChain();
	SweepAndCompact();
	DeclawRefsInRegisteredRanges();
	fInGC = false;
	// ValidateHeap(GetCurrentHeap(), -1) - the ROM's ValidateHeap (0x002eb584) does nothing
	if (gGCProcEntries != nil)
	{
		GCProcEntry* entries = (GCProcEntry*) *gGCProcEntries;
		long count = GetHandleSize(gGCProcEntries) / sizeof(GCProcEntry);
		for (long i = 0; i < count; i++)
			entries[i].fProc(entries[i].fRefCon);
	}
	if (gVerboseGC)
	{
		Statistics(&freeSpace, &largestFree);
		gREPout->Print("finish %ld/%ld ]\r", (long) freeSpace, (long) largestFree);
		Uriah();
	}
}


// ROM 0x0031bfe4 GC__Fv
void
GC(void)
{
	gHeap->GC();
}


/* -------------------------------------------------------------------------------
	Declawing
	When a package goes, refs into its address range must not be followed
	again: the range is registered and the next GC replaces every such ref
	(in objects, roots and DIY entries) by kDeclawedRef, which ObjectPtr
	reports as kNSErrBadPackageRef.
------------------------------------------------------------------------------- */

// ROM 0x002e3ee4 RegisterRangeForDeclawing__11TObjectHeapFUlT1
Boolean
TObjectHeap::RegisterRangeForDeclawing(ULong start, ULong end)
{
	DeclawingRange* range = new DeclawingRange(start, end, fDeclawingRanges);
	if (range != nil)
		fDeclawingRanges = range;
	return range != nil;
}


// ROM 0x002e3f14 DeclawRefsInRegisteredRanges__11TObjectHeapFv
void
TObjectHeap::DeclawRefsInRegisteredRanges(void)
{
	if (fDeclawingRanges == nil)
		return;
	fDeclawing = true;
	for (ObjHeader* block = (ObjHeader*) fStart; (char*) block < fEnd; block = NextBlock(block))
	{
		ULong header = block->fSizeAndFlags;
		long count = ((header & kObjSlotted) == 0) ? 1 : (long) ((ObjSize(block) - sizeof(ObjHeader)) / sizeof(Ref));
		Ref* slots = ObjSlots(block);
		for (long i = 0; i < count; i++)
			slots[i] = UpdateRef(slots[i]);
		if ((header & (kObjSlotted | kObjFrame)) == kObjFrame)
			ObjIndirectProcs(block)->fUpdate(ObjIndirectData(block));
	}
	if (gGCRoots != nil)
	{
		Ref** roots = (Ref**) *gGCRoots;
		long count = GetHandleSize(gGCRoots) / sizeof(Ref*);
		for (long i = 0; i < count; i++)
			*roots[i] = UpdateRef(*roots[i]);
	}
	if (gDIYGCEntries != nil)
	{
		DIYGCEntry* entries = (DIYGCEntry*) *gDIYGCEntries;
		long count = GetHandleSize(gDIYGCEntries) / sizeof(DIYGCEntry);
		for (long i = 0; i < count; i++)
			entries[i].fUpdate(entries[i].fRefCon);
	}
	while (fDeclawingRanges != nil)
	{
		DeclawingRange* next = fDeclawingRanges->Next();
		delete fDeclawingRanges;
		fDeclawingRanges = next;
	}
	fDeclawing = false;
}


// ROM 0x002e3280 RegisterRangeForDeclawing__FUlT1
Boolean
RegisterRangeForDeclawing(ULong start, ULong end)
{
	return gHeap->RegisterRangeForDeclawing(start, end);
}


// ROM 0x002e3298 DeclawRefsInRegisteredRanges__Fv
void
DeclawRefsInRegisteredRanges(void)
{
	gHeap->DeclawRefsInRegisteredRanges();
}


/* -------------------------------------------------------------------------------
	Roots and hooks
	Each list is a Handle of fixed-size entries.  A DIY entry marks and
	updates refs of its own (DIYGCMark/DIYGCUpdate); a GC proc is called
	after each collection.
------------------------------------------------------------------------------- */

// ROM 0x002e41f4 CommonGCRegister__FPPPcl
// Room for one more entry, which is returned.
Ptr
CommonGCRegister(Handle* h, long entrySize)
{
	if (*h == nil)
	{
		*h = NewHandle(entrySize);
		if (*h == nil)
			Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
		return **h;
	}
	Size size = GetHandleSize(*h);
	SetHandleSize(*h, size + entrySize);
	if (MemError() != noErr)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	return **h + size;
}


// ROM 0x002e4290 CommonGCUnregister__FPPclPv
// Remove the entry whose first word is refCon.
void
CommonGCUnregister(Handle h, long entrySize, void* refCon)
{
	if (h == nil)
		return;
	Size size = GetHandleSize(h);
	char* entry = *h;
	char* end = entry + size - entrySize;
	for ( ; entry <= end; entry += entrySize)
	{
		if (*(void**) entry == refCon)
		{
			char* after = entry + entrySize;
			BlockMove(after, entry, size - (after - *h));
			SetHandleSize(h, size - entrySize);
			return;
		}
	}
}


// ROM 0x002e430c CommonGCClearHooks__FPPPc
void
CommonGCClearHooks(Handle* h)
{
	if (*h == nil)
		return;
	DisposHandle(*h);
	*h = nil;
}


// ROM 0x002e40b4 AddGCRoot__FRl
void
AddGCRoot(Ref& root)
{
	*(Ref**) CommonGCRegister(&gGCRoots, sizeof(Ref*)) = &root;
}


// ROM 0x002e4160 RemoveGCRoot__FRl
void
RemoveGCRoot(Ref& root)
{
	CommonGCUnregister(gGCRoots, sizeof(Ref*), &root);
}


// ROM 0x002e4338 DIYGCRegister__FPvPFPv_vT2
void
DIYGCRegister(void* refCon, DIYGCFuncPtr markFunction, DIYGCFuncPtr updateFunction)
{
	DIYGCEntry* entry = (DIYGCEntry*) CommonGCRegister(&gDIYGCEntries, sizeof(DIYGCEntry));
	entry->fUpdate = updateFunction;
	entry->fMark = markFunction;
	entry->fRefCon = refCon;
}


// ROM 0x002e4370 DIYGCUnregister__FPv
void
DIYGCUnregister(void* refCon)
{
	CommonGCUnregister(gDIYGCEntries, sizeof(DIYGCEntry), refCon);
}


// ROM 0x002e32a8 DIYGCMark__Fl
// For a DIY marker: mark from a ref of its own.
void
DIYGCMark(Ref r)
{
	gHeap->Mark(r);
}


// ROM 0x002e32bc DIYGCUpdate__Fl
// For a DIY updater: what a ref of its own becomes.
Ref
DIYGCUpdate(Ref r)
{
	return gHeap->UpdateRef(r);
}


// ROM 0x002e32d0 GCRegister__FPvPFPv_v
void
GCRegister(void* refCon, GCProcPtr proc)
{
	GCProcEntry* entry = (GCProcEntry*) CommonGCRegister(&gGCProcEntries, sizeof(GCProcEntry));
	entry->fProc = proc;
	entry->fRefCon = refCon;
}


// ROM 0x002e3300 GCUnregister__FPv
void
GCUnregister(void* refCon)
{
	CommonGCUnregister(gGCProcEntries, sizeof(GCProcEntry), refCon);
}


// The ROM's ClearGCRoots and ClearGCHooks (objects.h) are CommonGCClearHooks
// on the lists; neither has a symbol of its own in the MP2x00 US ROM.
void
ClearGCRoots(void)
{
	CommonGCClearHooks(&gGCRoots);
}


void
ClearGCHooks(void)
{
	CommonGCClearHooks(&gDIYGCEntries);
	CommonGCClearHooks(&gGCProcEntries);
}
