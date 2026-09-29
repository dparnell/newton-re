/*
	File:		frames/ObjectHeap.cpp

	Contains:	TObjectHeap - the NewtonScript object heap's blocks, objects
				and RefHandles - and the functions that turn a Ref into a
				pointer to its object (ObjectPtr and its variants, forwarding,
				magic pointers, locking).  The collector is in GC.cpp, the
				object-level API in Objects.cpp.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	ObjectHeap.h explains the host layout (pointer-sized Refs and header
	words; the ROM's offsets and 4-byte rounding become sizeof-based).
*/

#include "ObjectHeap.h"
#include "NSErrors.h"
#include "OSErrors.h"
#include "NewtonMemory.h"

#include <string.h>

TObjectHeap*	gHeap = nil;
Ref				gCurrentStackPos = 1;
Ref				gCacheObjPtrRef = 0;
ObjHeader*		gCacheObjPtrPtr = nil;
Ref				gCacheLengthObj = 0;
long			gCacheLengthLen = 0;
Ref				gVarFrame = NILREF;
Ref				gFunctionFrame = NILREF;
Ref				gInheritanceFrame = NILREF;
Ref				gStores = NILREF;
Ref				gUnionSoups = NILREF;
Ref				gPackageStores = NILREF;
int				gVerboseGC = 0;
Ref				gROMBuiltinFunctions = NILREF;

// The magic pointer tables (ObjectHeap.h).  In the ROM table 0 is at
// 0x01d80000 in the jump table's diagonal page layout (physically
// gROMMagicPointerTable, 0x003a5000) and the REx tables at 0x01ee0000 +
// n * 0x100000 (InitRExMagicPointerTables, 0x000d218c); here they are plain
// arrays the ROM importer (ROMImport.cpp) and, later, the REx reader fill.
Ref*			gMagicPointerTables[kMagicPointerTables] = { nil };
long			gMagicPointerTableCounts[kMagicPointerTables] = { 0 };


/* -------------------------------------------------------------------------------
	Errors
------------------------------------------------------------------------------- */

// ROM 0x0031cf3c _RINTError__Fl
int
_RINTError(Ref r)
{
	RefVar value(r);
	ThrowBadTypeWithFrameData(kNSErrNotAnInteger, value);
	return 0;
}


// ROM 0x0031d8c8 _RCHARError__Fl
int
_RCHARError(Ref r)
{
	RefVar value(r);
	ThrowBadTypeWithFrameData(kNSErrNotACharacter, value);
	return 0;
}


// ROM 0x0031d3f8 _RPTRError__Fl
static ULong
_RPTRError(Ref r)
{
	RefVar value(r);
	ThrowBadTypeWithFrameData(kNSErrNotAPointer, value);
	return 0;
}


/* -------------------------------------------------------------------------------
	DeclawingRange
------------------------------------------------------------------------------- */

// ROM 0x002e3e40 __ct__14DeclawingRangeFUlT1P14DeclawingRange
DeclawingRange::DeclawingRange(ULong start, ULong end, DeclawingRange* next)
{
	fNext = next;
	fEnd = end;
	fStart = start;
}


// ROM 0x002e3e80 InRange__14DeclawingRangeFl
Boolean
DeclawingRange::InRange(Ref r) const
{
	return (ULong) r >= fStart && (ULong) r < fEnd;
}


// ROM 0x002e3ea8 InAnyRange__14DeclawingRangeFl
Boolean
DeclawingRange::InAnyRange(Ref r) const
{
	for (const DeclawingRange* range = this; range != nil; range = range->fNext)
		if (range->InRange(r))
			return true;
	return false;
}


/* -------------------------------------------------------------------------------
	TObjectHeap
------------------------------------------------------------------------------- */

// ROM 0x0031cafc __ct__11TObjectHeapFlT1
// The whole area is one free block; the RefHandle table takes the top of
// it, its handles chained free (each holding the index of the next as an
// integer, its stack position -1).
TObjectHeap::TObjectHeap(long size, long /*allocateInTempMemory*/)
{
	fMemory = NewPtr(size + kObjAlign);
	fStart = (char*) AlignedSize((ULong) fMemory);
	fEnd = fStart + (size & ~(kObjAlign - 1));
	MakeFreeBlock((ObjHeader*) fStart, fEnd - fStart);
	SplitBlock((ObjHeader*) fStart, ObjSize((ObjHeader*) fStart) - kRefHandleTableSize);
	fRefHandleTable = NextBlock((ObjHeader*) fStart);
	fRefHandleTable->fSizeAndFlags = (kRefHandleTableSize << kObjSizeShift) | kObjSlotted;
	fRefHandleTableSize = kRefHandleTableSize;
	RefHandle* handles = RefHandleTableEntries(fRefHandleTable);
	for (long i = 0; i < kRefHandleTableEntries - 1; i++)
	{
		handles[i].ref = MAKEINT(i + 1);
		handles[i].stackPos = MAKEINT(-1);
	}
	handles[kRefHandleTableEntries - 1].ref = MAKEINT(-1);
	handles[kRefHandleTableEntries - 1].stackPos = MAKEINT(-1);
	fResizeRoot = NILREF;
	fFreeHandleIndex = 0;
	AddGCRoot(fResizeRoot);
	fRover = (ObjHeader*) fStart;
	fInGC = false;
	fDeclawingRanges = nil;
	fDeclawing = false;
}


// ROM 0x0031cc50 DisposeMemory__11TObjectHeapFv
void
TObjectHeap::DisposeMemory(void)
{
	if (fMemory != nil)
		DisposPtr((Ptr) fMemory);
	fMemory = nil;
}


// ROM 0x0031cc78 __dt__11TObjectHeapFv
TObjectHeap::~TObjectHeap()
{
	RemoveGCRoot(fResizeRoot);
	DisposeMemory();
}


// ROM 0x0031ccc0 CoalesceFreeBlocks__11TObjectHeapFP9ObjHeaderl
// The size of the free block, after merging the free blocks that follow it
// when it is too small; 0 for a block in use.
ULong
TObjectHeap::CoalesceFreeBlocks(ObjHeader* block, long size)
{
	ULong header = block->fSizeAndFlags;
	if ((header & kObjFree) == 0)
		return 0;
	ULong blockSize = AlignedSize(header >> kObjSizeShift);
	if ((long) blockSize < size)
	{
		for (char* next = (char*) block + blockSize; next < fEnd; next = (char*) block + blockSize)
		{
			ULong nextHeader = ((ObjHeader*) next)->fSizeAndFlags;
			if ((nextHeader & kObjFree) == 0)
				break;
			blockSize += AlignedSize(nextHeader >> kObjSizeShift);
		}
		block->fSizeAndFlags = (header & 0xff) | (blockSize << kObjSizeShift);
		if (block < fRover && (char*) fRover < (char*) block + blockSize)
			fRover = block;
	}
	return blockSize;
}


// ROM 0x0031cd48 FindFreeBlock__11TObjectHeapFl
// Round the heap from the rover for a free block of at least size bytes.
ObjHeader*
TObjectHeap::FindFreeBlock(long size)
{
	ObjHeader* block = fRover;
	do {
		if ((long) CoalesceFreeBlocks(block, size) >= size)
			return block;
		block = NextBlock(block);
		if ((char*) block >= fEnd)
			block = (ObjHeader*) fStart;
	} while (block != fRover);
	return nil;
}


// ROM 0x0031cdb4 SplitBlock__11TObjectHeapFP9ObjHeaderl
// Shrink a block to size bytes; what is left over becomes a free block if
// there is room for a header.
void
TObjectHeap::SplitBlock(ObjHeader* block, long size)
{
	ULong newSize = AlignedSize(size);
	long remainder = (long) ObjAlignedSize(block) - (long) newSize;
	if (remainder >= (long) sizeof(ULong))				// room for a size word (the ROM's > 3)
		MakeFreeBlock((ObjHeader*) ((char*) block + newSize), remainder);
	SetObjSize(block, size);
	if (fRover == block)
	{
		fRover = (ObjHeader*) ((char*) block + AlignedSize(size & kObjMaxSize));
		if ((char*) fRover >= fEnd)
			fRover = (ObjHeader*) fStart;
	}
}


// ROM 0x0031ce3c MakeFreeBlock__11TObjectHeapFP9ObjHeaderl
void
TObjectHeap::MakeFreeBlock(ObjHeader* block, long size)
{
	block->fSizeAndFlags = (AlignedSize(size) << kObjSizeShift) | kObjFree;
	if (size > (long) sizeof(block->fSizeAndFlags))
		block->fGCStuff = 0;
}


// ROM 0x0031ce6c AllocateBlock__11TObjectHeapFlUl
// A block of size bytes with the given flags, its GC word clear; a GC when
// none is free, out of memory when there still is none.
ObjHeader*
TObjectHeap::AllocateBlock(long size, ULong flags)
{
	ObjHeader* block = FindFreeBlock(size);
	if (block == nil)
	{
		GC();
		block = FindFreeBlock(size);
		if (block == nil)
		{
			fResizeRoot = NILREF;
			Throw(exOutOfMemory, (void*) kNSErrOutOfObjectMemory, nil);
		}
	}
	SplitBlock(block, size);
	fRover = NextBlock(block);
	if ((char*) fRover >= fEnd)
		fRover = (ObjHeader*) fStart;
	SetObjFlags(block, flags);
	block->fGCStuff = 0;
	return block;
}


// ROM 0x0031cf28 KillBlock__11TObjectHeapFPc
void
TObjectHeap::KillBlock(ObjHeader* block)
{
	ULong size = ObjAlignedSize(block);
	block->fSizeAndFlags = (size << kObjSizeShift) | kObjFree;
	if (size > sizeof(block->fSizeAndFlags))
		block->fGCStuff = 0;
}


// ROM 0x0031cf74 ResizeBlock__11TObjectHeapFP9ObjHeaderl
// In place when shrinking or when the free blocks after it suffice; else a
// new block, the contents moved, the old one freed - unless the object is
// locked, which is an error.  The new block's address is returned.
ObjHeader*
TObjectHeap::ResizeBlock(ObjHeader* block, long size)
{
	ULong header = block->fSizeAndFlags;
	ULong oldSize = AlignedSize(header >> kObjSizeShift);
	long delta = (long) AlignedSize(size) - (long) oldSize;
	if (delta != 0)
	{
		if (delta < 0)
		{
			SplitBlock(block, size);
			return block;
		}
		ObjHeader* next = (ObjHeader*) ((char*) block + oldSize);
		if ((char*) next >= fEnd || (long) CoalesceFreeBlocks(next, size) < delta)
		{
			if ((header & kObjLocked) != 0)
			{
				RefVar value(MAKEPTR(block));
				ThrowExFramesWithBadValue(kNSErrCouldntResizeLockedObject, value);
				return block;
			}
			fResizeRoot = MAKEPTR(block);						// AllocateBlock may collect and move it
			ObjHeader* newBlock = AllocateBlock(size, header & 0xff);
			ObjHeader* oldBlock = PTRVALUE(fResizeRoot);
			fResizeRoot = NILREF;
			long copy = ((long) oldSize < size ? (long) oldSize : size) - (long) sizeof(ObjHeader);
			BlockMove(ObjSlots(oldBlock), ObjSlots(newBlock), copy);
			KillBlock(oldBlock);
			return newBlock;
		}
		SplitBlock(next, delta);								// take what is needed of the free block after
		header = block->fSizeAndFlags;
	}
	block->fSizeAndFlags = (header & 0xff) | ((ULong) size << kObjSizeShift);
	return block;
}


// ROM 0x0031d0b4 BlockStatistics__11TObjectHeapFP9ObjHeaderPUlPUc
// The block after previous (the first for nil), its rounded size and
// whether it is free; nil past the end.
ObjHeader*
TObjectHeap::BlockStatistics(ObjHeader* previous, ULong* size, Boolean* isFree)
{
	ObjHeader* block = (previous == nil) ? (ObjHeader*) fStart : NextBlock(previous);
	if (block == nil || (char*) block >= fEnd)
		return nil;
	*isFree = (block->fSizeAndFlags & kObjFree) != 0;
	*size = ObjAlignedSize(block);
	return block;
}


// ROM 0x0031d11c Statistics__11TObjectHeapFPUlT1
void
TObjectHeap::Statistics(ULong* freeSpace, ULong* largestFreeBlock)
{
	*freeSpace = 0;
	*largestFreeBlock = 0;
	ULong size = 0;
	Boolean isFree = false;
	for (ObjHeader* block = BlockStatistics(nil, &size, &isFree); block != nil; block = BlockStatistics(block, &size, &isFree))
	{
		if (isFree)
		{
			*freeSpace += size;
			if (*largestFreeBlock < size)
				*largestFreeBlock = size;
		}
	}
}


// ROM 0x0031bddc InHeap__11TObjectHeapFl
Boolean
TObjectHeap::InHeap(Ref r) const
{
	ULong p = (RTAG(r) == kTagPointer) ? (ULong) (r - kTagPointer) : _RPTRError(r);
	return p >= (ULong) fStart && p < (ULong) fEnd;
}


// ROM 0x0031d2f4 ClearRefHandles__11TObjectHeapFv
// Free the handles of RefVars deeper on the stack than the current position
// in the current generation (the high 16 bits of gCurrentStackPos).  The
// ROM's loop stops one short of the last handle; so does this.
void
TObjectHeap::ClearRefHandles(void)
{
	Ref current = gCurrentStackPos;
	ULong depth = (ULong) current & 0xffff;
	RefHandle* handles = RefHandleTableEntries(fRefHandleTable);
	long count = RefHandleTableCount(fRefHandleTable) - 1;
	for (long i = 0; i < count; i++)
	{
		Ref pos = RVALUE(handles[i].stackPos);
		if (pos != 0 && ((current ^ pos) >> 16) == 0 && depth < ((ULong) pos & 0xffff))
			DisposeRefHandle(&handles[i]);
	}
}


// ROM 0x0031d37c AllocateObject__11TObjectHeapFlUl
// A block with its class slot NILREF and, for a slotted object, its slots
// NILREF, for a binary its data zero.
Ref
TObjectHeap::AllocateObject(long size, ULong flags)
{
	ObjHeader* obj = AllocateBlock(size, flags);
	ObjClass(obj) = NILREF;
	if ((flags & kObjSlotted) == 0)
	{
		if (size > kObjBodySize)
			memset(ObjData(obj), 0, size - kObjBodySize);
	}
	else
	{
		long slots = (size - (long) sizeof(ObjHeader)) / (long) sizeof(Ref);
		Ref* slot = ObjSlots(obj);
		for (long i = 1; i < slots; i++)
			slot[i] = NILREF;
	}
	return MAKEPTR(obj);
}


// ROM 0x0031d430 ResizeObject__11TObjectHeapFRC6RefVarl
// When the object moves, what is left at the old address becomes a
// forwarding object to the new one (references are fixed up at the next GC).
void
TObjectHeap::ResizeObject(RefArg obj, long size)
{
	ObjHeader* o = OBJ(obj);
	if ((ULong) size == ObjSize(o))
		return;
	ObjHeader* moved = ResizeBlock(o, size);
	ObjHeader* old = OBJ(obj);
	if (old == moved)
		return;
	SplitBlock(old, kObjBodySize);
	SetObjFlags(old, kObjForward);
	ObjClass(old) = MAKEPTR(moved);
	if ((Ref) obj == gCacheObjPtrRef)
		gCacheObjPtrPtr = moved;
}


// ROM 0x0031d548 ReplaceObject__11TObjectHeapFlT1
// Make every reference to target refer to replacement: target becomes a
// forwarding object.
void
TObjectHeap::ReplaceObject(Ref target, Ref replacement)
{
	if (!ISPTR(target))
	{
		RefVar value(target);
		ThrowBadTypeWithFrameData(kNSErrNotAPointer, value);
	}
	if (!ISPTR(replacement))
	{
		RefVar value(replacement);
		ThrowBadTypeWithFrameData(kNSErrNotAPointer, value);
	}
	if (target == replacement)
		return;
	ObjHeader* o = NoFaultObjectPtr(target);
	if ((ObjFlags(o) & kObjReadOnly) != 0)
	{
		RefVar value(target);
		ThrowExFramesWithBadValue(kNSErrObjectReadOnly, value);
	}
	SplitBlock(o, kObjBodySize);
	SetObjFlags(o, kObjForward);
	ObjClass(o) = replacement;
	if (target == gCacheObjPtrRef)
		gCacheObjPtrRef = 0;
	if (target == gCacheLengthObj)
		gCacheLengthObj = 0;
	ICacheClear();
}


// ROM 0x0031d65c AllocateBinary__11TObjectHeapFRC6RefVarl
Ref
TObjectHeap::AllocateBinary(RefArg theClass, long length)
{
	if (length < 0)
	{
		RefVar value(MAKEINT(length));
		ThrowExFramesWithBadValue(kNSErrNegativeLength, value);
	}
	if (length > kMaxBinaryLength)
	{
		RefVar value(MAKEINT(length));
		ThrowExFramesWithBadValue(kNSErrOutOfRange, value);
	}
	Ref obj = AllocateObject(BinaryObjSize(length), 0);
	ObjClass(PTRVALUE(obj)) = theClass;
	return obj;
}


// ROM 0x0031d700 AllocateIndirectBinary__11TObjectHeapFRC6RefVarl
// length bytes of body after the procedure-table pointer.
Ref
TObjectHeap::AllocateIndirectBinary(RefArg theClass, long length)
{
	Ref obj = AllocateObject(BinaryObjSize(length) + sizeof(IndirectBinaryProcs*), kObjFrame);
	ObjClass(PTRVALUE(obj)) = theClass;
	return obj;
}


// ROM 0x0031d730 AllocateArray__11TObjectHeapFRC6RefVarl
Ref
TObjectHeap::AllocateArray(RefArg theClass, long length)
{
	if (length < 0)
	{
		RefVar value(MAKEINT(length));
		ThrowExFramesWithBadValue(kNSErrNegativeLength, value);
	}
	if (length > kMaxArrayLength)
	{
		RefVar value(MAKEINT(length));
		ThrowExFramesWithBadValue(kNSErrOutOfRange, value);
	}
	Ref obj = AllocateObject(ArrayObjSize(length), kObjSlotted);
	ObjClass(PTRVALUE(obj)) = theClass;
	return obj;
}


// ROM 0x0031d7d8 AllocateFrame__11TObjectHeapFv
// An empty frame with an empty map of its own.
Ref
TObjectHeap::AllocateFrame(void)
{
	RefVar frame(AllocateObject(kObjBodySize, kObjSlotted | kObjFrame));
	RefVar map(AllocateMap(RefVar(NILREF), 0));
	ObjClass(PTRVALUE((Ref) frame)) = map;
	return frame;
}


// ROM 0x0031d848 AllocateFrameWithMap__11TObjectHeapFRC6RefVar
Ref
TObjectHeap::AllocateFrameWithMap(RefArg map)
{
	long slots = ComputeMapSize(map);
	Ref obj = AllocateObject(ArrayObjSize(slots), kObjSlotted | kObjFrame);
	ObjClass(PTRVALUE(obj)) = map;
	return obj;
}


// ROM 0x0031d88c AllocateMap__11TObjectHeapFRC6RefVarl
// A map for length tags: flags 0, the supermap, then the tags.
Ref
TObjectHeap::AllocateMap(RefArg superMap, long length)
{
	Ref obj = AllocateObject(ArrayObjSize(length + 1), kObjSlotted);
	ObjHeader* map = PTRVALUE(obj);
	MapFlags(map) = 0;
	MapSuperMap(map) = superMap;
	return obj;
}


// ROM 0x0031f9b4 Clone__11TObjectHeapFRC6RefVar
// A shallow copy: non-pointers and symbols are themselves; an indirect
// binary clones through its table; a frame's map becomes shared.
Ref
TObjectHeap::Clone(RefArg obj)
{
	Ref r = obj;
	if (!ISPTR(r))
		return r;
	ObjHeader* o = OBJ(r);
	ULong header = o->fSizeAndFlags;
	if ((header & kObjSlotted) == 0)
	{
		if ((header & kObjFrame) != 0)
		{
			o = OBJ(obj);
			return ObjIndirectProcs(o)->fClone(ObjIndirectData(o), ObjClass(o));
		}
		if (ObjClass(o) == kSymbolClass)
			return obj;
	}
	ObjHeader* copy = AllocateBlock(header >> kObjSizeShift, header & (kObjSlotted | kObjFrame));
	o = OBJ(obj);
	BlockMove(ObjSlots(o), ObjSlots(copy), (header >> kObjSizeShift) - sizeof(ObjHeader));
	if ((header & (kObjSlotted | kObjFrame)) == (kObjSlotted | kObjFrame))
	{
		ObjHeader* map = OBJ(ObjClass(o));
		if ((MapFlags(map) & kMapShared) == 0 && (ObjFlags(map) & kObjReadOnly) == 0)
			MapFlags(map) |= kMapShared;
	}
	return MAKEPTR(copy);
}


// ROM 0x0031e378 SetLength__11TObjectHeapFRC6RefVarl
void
TObjectHeap::SetLength(RefArg obj, long length)
{
	if (IsFrame(obj))
		ThrowBadTypeWithFrameData(kNSErrUnexpectedFrame, obj);
	if (length < 0)
	{
		RefVar value(MAKEINT(length));
		ThrowExFramesWithBadValue(kNSErrNegativeLength, value);
	}
	ObjHeader* o = OBJ(obj);
	ULong flags = ObjFlags(o);
	if ((flags & kObjReadOnly) != 0)
		ThrowExFramesWithBadValue(kNSErrObjectReadOnly, obj);
	if ((flags & (kObjSlotted | kObjFrame)) == kObjFrame)
		ObjIndirectProcs(o)->fSetLength(ObjIndirectData(o), length);
	else if ((flags & kObjSlotted) == 0)
	{
		if (length > kMaxBinaryLength)
		{
			RefVar value(MAKEINT(length));
			ThrowExFramesWithBadValue(kNSErrOutOfRange, value);
		}
		UnsafeSetBinaryLength(obj, length);
	}
	else
	{
		if (length > kMaxArrayLength)
		{
			RefVar value(MAKEINT(length));
			ThrowExFramesWithBadValue(kNSErrOutOfRange, value);
		}
		UnsafeSetArrayLength(obj, length);
	}
}


// ROM 0x0031e4d0 UnsafeSetArrayLength__11TObjectHeapFRC6RefVarl
// New slots are NILREF.
void
TObjectHeap::UnsafeSetArrayLength(RefArg obj, long length)
{
	ObjHeader* o = OBJ(obj);
	ULong oldSize = ObjSize(o);
	ULong newSize = ArrayObjSize(length);
	if (oldSize != newSize)
	{
		long oldLength = ObjArrayLength(OBJ(obj));
		ResizeObject(obj, newSize);
		o = OBJ(obj);
		if (oldSize < newSize)
		{
			Ref* slots = ObjArraySlots(o);
			for (long i = oldLength; i < length; i++)
				slots[i] = NILREF;
		}
		DirtyObject(obj);
	}
	gCacheLengthObj = 0;
}


// ROM 0x0031e590 UnsafeSetBinaryLength__11TObjectHeapFRC6RefVarl
// New bytes are zero.
void
TObjectHeap::UnsafeSetBinaryLength(RefArg obj, long length)
{
	ObjHeader* o = OBJ(obj);
	ULong oldSize = ObjSize(o);
	ULong newSize = BinaryObjSize(length);
	if (oldSize != newSize)
	{
		ResizeObject(obj, newSize);
		o = OBJ(obj);
		if (oldSize < newSize)
			memset((char*) o + oldSize, 0, newSize - oldSize);
		DirtyObject(obj);
	}
	gCacheLengthObj = 0;
}


/* -------------------------------------------------------------------------------
	RefHandles
	The table at the top of the heap is a slotted object whose slots are the
	handles (so the collector marks and updates their refs like any slots);
	free handles chain through their ref field (MAKEINT(next index), -1 at
	the end) with stack position -1.  A RefVar's handle records the stack
	position it was made at, so ClearRefHandles can free the ones an
	exception unwound past; a RefStruct's records 0 (never cleared).
------------------------------------------------------------------------------- */

// ROM 0x0031d1b0 IncrementCurrentStackPos__Fv
void
IncrementCurrentStackPos(void)
{
	gCurrentStackPos++;
}


// ROM 0x0031d1c8 DecrementCurrentStackPos__Fv
void
DecrementCurrentStackPos(void)
{
	gCurrentStackPos--;
}


// The table has run out of free handles: ask the collector for a bigger one
// (kRefHandleTableGrowth more; SweepAndCompact moves the table's start down
// into the free space below it) and give up if it could not.
// ROM 0x0031d1e0 ExpandObjectTable__FP9RefHandle
static RefHandle*
ExpandObjectTable(RefHandle* handle)
{
	TObjectHeap* heap = gHeap;
	ULong oldSize = ObjSize(heap->fRefHandleTable);
	heap->fRefHandleTableSize = AlignedSize(oldSize + kRefHandleTableGrowth);
	GC();
	ObjHeader* table = heap->fRefHandleTable;
	if ((ObjSize(table) - oldSize) / sizeof(RefHandle) == 0)
	{
		// DEVIATION: the ROM leaves an index below the table here, which the
		// next AllocateRefHandle would write through; -1 makes it throw instead
		heap->fFreeHandleIndex = -1;
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	}
	return handle;
}


// ROM 0x0031d26c AllocateRefHandle__Fl
RefHandle*
AllocateRefHandle(Ref targetObj)
{
	TObjectHeap* heap = gHeap;
	if (heap->fFreeHandleIndex < 0)						// DEVIATION: see ExpandObjectTable
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	RefHandle* handle = RefHandleTableEntries(heap->fRefHandleTable) + heap->fFreeHandleIndex;
	long next = RVALUE(handle->ref);
	heap->fFreeHandleIndex = next;
	handle->ref = targetObj;
	handle->stackPos = MAKEINT(gCurrentStackPos);
	if (next != -1)
		return handle;
	return ExpandObjectTable(handle);
}


// ROM 0x0031d2b0 DisposeRefHandle__FP9RefHandle
void
DisposeRefHandle(RefHandle* handle)
{
	TObjectHeap* heap = gHeap;
	long next = heap->fFreeHandleIndex;
	if (next < 0)
		next = -1;
	handle->ref = MAKEINT(next);
	handle->stackPos = MAKEINT(-1);
	heap->fFreeHandleIndex = handle - RefHandleTableEntries(heap->fRefHandleTable);
}


// ROM 0x0031be6c ClearRefHandles__Fv
void
ClearRefHandles(void)
{
	gHeap->ClearRefHandles();
}


/* -------------------------------------------------------------------------------
	Refs to objects
------------------------------------------------------------------------------- */

// In the ROM a pointer ref below 0x03800000 or in 0x60000000-0x67ffffff (the
// ROM, its extensions and the packages) is used as it is: only heap objects
// can be forwarding objects or fault blocks.  The host has no such
// addresses: an object outside the heap is used as it is.
static inline Boolean
IsDirectRef(Ref r)
{
	return !gHeap->InHeap((const void*) (r - kTagPointer));
}


// ROM 0x0031d4d4 ForwardReference1__FP16ForwardingObject
// Follow a chain of forwarding objects from one of them; it is then made to
// point at the end, so that the next lookup is short.  What it points at is
// a pointer ref to the final object, or the last non-pointer ref (a magic
// pointer) met on the way.
static void
ForwardReference1(ObjHeader* forwarder)
{
	ObjHeader* o = forwarder;
	Ref result = 0;
	do {
		Ref next = ObjClass(o);
		if (RTAG(next) == kTagPointer)
			o = PTRVALUE(next);
		else
		{
			o = NoFaultObjectPtr(next);
			result = next;
		}
	} while ((ObjFlags(o) & kObjForward) != 0);
	if (result == 0)
		result = MAKEPTR(o);
	ObjClass(forwarder) = result;
}


// ROM 0x0031d524 ForwardReference__Fl
// A ref with forwarding followed (the ROM repeats ForwardReference1's loop).
Ref
ForwardReference(Ref r)
{
	if (RTAG(r) != kTagPointer)
		return r;
	ObjHeader* o = PTRVALUE(r);
	if ((ObjFlags(o) & kObjForward) == 0)
		return r;
	ForwardReference1(o);
	return ObjClass(o);
}


// ROM 0x0031dad4 ResolveMagicPtr__Fl
// A magic pointer names a table (value >> 12) and an entry in it: table 0 is
// the ROM's (its entries are refs to resolve again), table 1 the global
// variables (1) and the built-in functions (2), even tables 2-8 the REx
// export tables and odd tables 3-9 the RAM tables of their imports; those
// entries are pointer refs.
ObjHeader*
ResolveMagicPtr(Ref r)
{
	ULong value = (ULong) RVALUE(r);
	ULong table = value >> 12;
	ULong index = value & 0xfff;
	if (table == 0)
	{
		if ((long) index < gMagicPointerTableCounts[0])
			return OBJ(gMagicPointerTables[0][index]);
	}
	else if (table == 1)
	{
		if (index == 1)
			return OBJ(gVarFrame);
		if (index == 2)
			return OBJ(gROMBuiltinFunctions);
	}
	else if (table < kMagicPointerTables)
	{
		if ((long) index < gMagicPointerTableCounts[table])
			return PTRVALUE(gMagicPointerTables[table][index]);
	}
	RefVar bad(MAKEINT(value));
	ThrowExFramesWithBadValue(kNSErrBadMagicPointer, bad);
	return nil;
}


// The body ObjectPtr, NoFaultObjectPtr and FaultCheckObjectPtr share
// (the ROM's ObjectPtr1, 0x002f8950, whose tail they jump into): a pointer
// ref is followed through forwarding objects, and a fault block stands for
// the soup entry it holds - in memory already (its object slot) or to be
// read (FollowFaultBlock; nil instead when faultCheck).  A magic pointer
// resolves through its table; anything else is not an object.
// ROM 0x0031dc54 ObjectPtr1__FlT1i
static ObjHeader*
ObjectPtr1(Ref obj, Boolean faultCheck)
{
	for (;;)
	{
		ULong tag = RTAG(obj);
		if (tag == kTagPointer)
		{
			if (IsDirectRef(obj))
				return PTRVALUE(obj);
			if (gCacheObjPtrRef == obj)
				return gCacheObjPtrPtr;
		}
		else if (tag == kTagMagicPtr)
			return ResolveMagicPtr(obj);
		else
		{
			if (obj == kDeclawedRef)
				Throw(exFrames, (void*) kNSErrBadPackageRef, nil);
			RefVar value(obj);
			ThrowExFramesWithBadValue(kNSErrObjectPointerOfNonPtr, value);
			return nil;
		}
		gCacheObjPtrRef = obj;
		Ref forwarded = ForwardReference(obj);
		ObjHeader* o = PTRVALUE(forwarded);
		if ((ObjFlags(o) & kObjSlotted) == 0 || ObjClass(o) != kFaultBlockClass)
		{
			gCacheObjPtrPtr = o;
			return o;
		}
		gCacheObjPtrRef = 0;
		obj = ObjArraySlots(o)[kFaultBlockObjectSlot];
		if (obj == NILREF)
		{
			if (faultCheck)
				return nil;
			RefVar faultBlock(forwarded);
			return OBJ(FollowFaultBlock(faultBlock));
		}
	}
}


// ROM 0x0031dd54 ObjectPtr__Fl
Ptr
ObjectPtr(Ref obj)
{
	return (Ptr) ObjectPtr1(obj, false);
}


// ROM 0x0031ddac NoFaultObjectPtr__Fl
ObjHeader*
NoFaultObjectPtr(Ref obj)
{
	if (RTAG(obj) == kTagPointer)
		return PTRVALUE(ForwardReference(obj));
	return ObjectPtr1(obj, false);
}


// ROM 0x0031dde8 FaultCheckObjectPtr__Fl
ObjHeader*
FaultCheckObjectPtr(Ref obj)
{
	return ObjectPtr1(obj, true);
}


// ROM 0x0031de40 NoTouchObjectPtr__FlPi
// ObjectPtr for FIsValid: the pointer without reading anything from a
// store.  DEVIATION: the ROM also asks whether the pointer lies in the ROM
// domain's large-object space (ROMDomainBase/Size and the large object
// address test); the host imports the packages' objects into areas of
// its own, so nothing is ever there and *isLargeObject is always 0.
ObjHeader*
NoTouchObjectPtr(Ref obj, int* isLargeObject)
{
	*isLargeObject = 0;
	return NoFaultObjectPtr(obj);
}


// ROM 0x0031c9a8 IsFaultBlock__Fl
Boolean
IsFaultBlock(Ref r)
{
	return ISPTR(r) && ObjClass(NoFaultObjectPtr(r)) == kFaultBlockClass;
}


// ROM 0x002e01d8 FollowFaultBlock__FRC6RefVar
// Read the entry a fault block stands for: from its store when it has one,
// else by sending its handler EntryAccess.  The stores layer (which has
// LoadPermObject and the entries) does it through gFollowFaultBlockProc
// (stores/Entries.cpp); without it, the store error the ROM throws for a
// missing store.
Ref (*gFollowFaultBlockProc)(RefArg faultBlock) = nil;

Ref
FollowFaultBlock(RefArg faultBlock)
{
	if (gFollowFaultBlockProc != nil)
		return gFollowFaultBlockProc(faultBlock);
	Throw(exStoreError, (void*) kNSErrEntryStoreGone, nil);
	return NILREF;
}


// ROM 0x0031b0bc LockRef__Fl
// A locked object does not move at a GC; the count is the high byte of the
// GC word (0xff: locked for ever).  Read-only objects never move anyway.
void
LockRef(Ref obj)
{
	ObjHeader* o = OBJ(obj);
	if ((ObjFlags(o) & kObjReadOnly) != 0)
		return;
	ULong count = o->fGCStuff >> kObjLockShift;
	if (count != 0xff)
	{
		o->fGCStuff = (o->fGCStuff & kObjGCIndexMask) | ((count + 1) << kObjLockShift);
		if (count == 0)
			o->fSizeAndFlags |= kObjLocked;
	}
}


// ROM 0x0031b108 UnlockRef__Fl
void
UnlockRef(Ref obj)
{
	ObjHeader* o = OBJ(obj);
	if ((ObjFlags(o) & kObjReadOnly) != 0)
		return;
	ULong count = o->fGCStuff >> kObjLockShift;
	if (count != 0xff)
	{
		count = (count - 1) & 0xff;
		o->fGCStuff = (o->fGCStuff & kObjGCIndexMask) | (count << kObjLockShift);
		if (count == 0)
			o->fSizeAndFlags &= ~(ULong) kObjLocked;
	}
}


// ROM 0x0031ca1c LockRefArg__FRC6RefVar
void
LockRefArg(RefArg obj)
{
	LockRef(obj);
}


// ROM 0x0031ca28 UnlockRefArg__FRC6RefVar
void
UnlockRefArg(RefArg obj)
{
	UnlockRef(obj);
}


// ROM 0x0031ca34 DirtyObject__Fl
void
DirtyObject(Ref obj)
{
	ObjHeader* o = OBJ(obj);
	if ((ObjFlags(o) & kObjReadOnly) == 0)
		o->fSizeAndFlags |= kObjDirty;
}


// ROM 0x0031cad8 UndirtyObject__Fl
void
UndirtyObject(Ref obj)
{
	ObjHeader* o = OBJ(obj);
	if ((ObjFlags(o) & kObjReadOnly) == 0)
		o->fSizeAndFlags &= ~(ULong) kObjDirty;
}


/* -------------------------------------------------------------------------------
	TFramesObjectPtr, TBinaryDataPtr
	The DDK's names for the ROM's TObjectPtr and DataPtr: a RefStruct that
	keeps its object locked so that the pointer to it stays good.
------------------------------------------------------------------------------- */

// ROM 0x0031e05c __ct__10TObjectPtrFv
TFramesObjectPtr::TFramesObjectPtr()
{ }


// ROM 0x0031e80c __ct__10TObjectPtrFl
TFramesObjectPtr::TFramesObjectPtr(Ref r)
	: fRef(r)
{
	if (r == NILREF)
		Throw(exFrames, (void*) kNSErrFramesObjectPtrOfNil, nil);
	LockRef(fRef);
}


// ROM 0x0031f1c4 __ct__10TObjectPtrFRC9RefStruct
TFramesObjectPtr::TFramesObjectPtr(const RefStruct& r)
	: fRef(r)
{
	if ((Ref) r == NILREF)
		Throw(exFrames, (void*) kNSErrFramesObjectPtrOfNil, nil);
	LockRef(fRef);
}


// ROM 0x0031fe10 __ct__10TObjectPtrFRC6RefVar
TFramesObjectPtr::TFramesObjectPtr(const RefVar& r)
	: fRef(r)
{
	if ((Ref) r == NILREF)
		Throw(exFrames, (void*) kNSErrFramesObjectPtrOfNil, nil);
	LockRef(fRef);
}


// ROM 0x0031ada4 __ct__10TObjectPtrFRC10TObjectPtr
TFramesObjectPtr::TFramesObjectPtr(const TFramesObjectPtr& p)
{
	fRef = p.fRef;
	if ((Ref) fRef != NILREF)
		LockRef(fRef);
}


// ROM 0x0031a15c __dt__10TObjectPtrFv
TFramesObjectPtr::~TFramesObjectPtr()
{
	if ((Ref) fRef != NILREF)
		UnlockRef(fRef);
}


// ROM 0x0031be28 __as__10TObjectPtrFRC10TObjectPtr
const TFramesObjectPtr&
TFramesObjectPtr::operator=(const TFramesObjectPtr& p)
{
	if ((Ref) fRef != NILREF)
		UnlockRef(fRef);
	fRef = p.fRef;
	if ((Ref) fRef != NILREF)
		LockRef(fRef);
	return *this;
}


// ROM 0x0031bf8c __as__10TObjectPtrFl
const TFramesObjectPtr&
TFramesObjectPtr::operator=(Ref r)
{
	if ((Ref) fRef != NILREF)
		UnlockRef(fRef);
	fRef = r;
	if (r != NILREF)
		LockRef(r);
	return *this;
}


// ROM 0x0031c3ac __opPc__10TObjectPtrCFv
TFramesObjectPtr::operator char*() const
{
	if ((Ref) fRef == NILREF)
		Throw(exFrames, (void*) kNSErrUnassignedFramesObjectPtr, nil);
	return ObjectPtr(fRef);
}


// ROM 0x0031c5a8 __as__7DataPtrFRC7DataPtr
const TBinaryDataPtr&
TBinaryDataPtr::operator=(const TBinaryDataPtr& p)
{
	TFramesObjectPtr::operator=(p);
	return *this;
}


// ROM 0x0031c71c __as__7DataPtrFl
const TBinaryDataPtr&
TBinaryDataPtr::operator=(Ref r)
{
	TFramesObjectPtr::operator=(r);
	return *this;
}


// ROM 0x0031c88c __opPc__7DataPtrCFv
// The binary's data: through the table of an indirect binary.
TBinaryDataPtr::operator char*() const
{
	if ((Ref) fRef == NILREF)
		Throw(exFrames, (void*) kNSErrUnassignedFramesObjectPtr, nil);
	ObjHeader* o = OBJ(fRef);
	if (ObjIsIndirect(o))
		return ObjIndirectProcs(o)->fDataPtr(ObjIndirectData(o));
	return ObjData(o);
}
