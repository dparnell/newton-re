/*
	File:		memory/SkiaHeap.cpp

	Contains:	The Skia heap (SkiaHeap.h): blocks, free list, master pointers,
				growing, shrinking, compaction.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.  The
	functions the decompiler could not follow (SetBlockSize, TrySetSize,
	FindSmallestSlide, SearchFreeList, JumpBlock) were read from the
	disassembly.  The current heap is the task's (GetCurrentHeap): these
	primitives work on it, as on the ARM, and their callers switch heaps.
*/

#include "SkiaHeap.h"
#include "KernelGlobals.h"
#include "Boot.h"
#include "os600/TaskGlobals.h"
#include "VirtualMemory.h"
#include "OSErrors.h"

#include <string.h>

// the ROM's SetHeapLimits/LockHeapRange/UnlockHeapRange are the stack
// manager's (VirtualMemory.h)

static SkiaHeap*	gFirstHeap = nil;			// 0x0c102624 (unnamed in the symbol table)

static SkiaBlock*	NewBlock(Size size);


static inline SkiaHeap*
Current()
{
	return (SkiaHeap*) GetCurrentHeap();
}


// The task globals hold the task's current heap (TaskGlobals::fCurrentHeap)
static inline TaskGlobals*
Globals()
{
	return (TaskGlobals*) ((char*) gCurrentGlobals - kTaskGlobalsSize);
}


// A size request's physical size: the header, rounded up to kBlockAlign;
// the rounding is the block's delta.
static inline Size
PhysicalSize(Size size, UByte* outDelta)
{
	ULong delta = (~((ULong) size + kBlockHeaderSize - 1)) & (kBlockAlign - 1);
	*outDelta = (UByte) delta;
	return size + kBlockHeaderSize + delta;
}


/* -------------------------------------------------------------------------------
	The current heap
------------------------------------------------------------------------------- */

// ROM 0x00142da0 GetCurrentHeap
// The task's current heap, or the kernel heap before there are tasks.
Heap
GetCurrentHeap(void)
{
	if (gOSIsRunning || gCurrentTaskId != 0)
	{
		Heap heap = Globals()->fCurrentHeap;
		return heap != nil ? heap : gKernelHeap;
	}
	return gKernelHeap;
}


// ROM 0x00142df0 SetCurrentHeap
void
SetCurrentHeap(Heap heap)
{
	if (gOSIsRunning || gCurrentTaskId != 0)
		Globals()->fCurrentHeap = heap;
}


// ROM 0x003108b0 GetFirstHeap
Heap
GetFirstHeap(void)
{
	return gFirstHeap;
}


// ROM 0x003108c0 SetFirstHeap
void
SetFirstHeap(Heap heap)
{
	gFirstHeap = (SkiaHeap*) heap;
}


/* -------------------------------------------------------------------------------
	Heap fields
------------------------------------------------------------------------------- */

// ROM 0x00310c20 IsSkiaHeap
Boolean
IsSkiaHeap(Heap heap)
{
	return ((SkiaHeap*) heap)->fMagic == kSkiaHeapMagic;
}

// ROM 0x003108d0 SetSkiaHeapSemaphore
void SetSkiaHeapSemaphore(Heap heap, TULockingSemaphore* semaphore)	{ ((SkiaHeap*) heap)->fSemaphore = semaphore; }

// ROM 0x00310fe0 GetSkiaHeapSemaphore
TULockingSemaphore*
GetSkiaHeapSemaphore(Heap heap)
{
	if (heap == nil)
		heap = GetCurrentHeap();
	return ((SkiaHeap*) heap)->fSemaphore;
}

// ROM 0x003108d8 GetSkiaHeapRefcon
void*
GetSkiaHeapRefcon(Heap heap)
{
	if (heap == nil)
		heap = GetCurrentHeap();
	return ((SkiaHeap*) heap)->fRefCon;
}

// ROM 0x003108f4 SetSkiaHeapRefcon
void
SetSkiaHeapRefcon(void* refCon, Heap heap)
{
	if (heap == nil)
		heap = GetCurrentHeap();
	((SkiaHeap*) heap)->fRefCon = refCon;
}

// ROM 0x00310ac8 SetHeapIsVMBacked
void SetHeapIsVMBacked(Heap heap)					{ ((SkiaHeap*) heap)->fIsVMBacked = 1; }
// ROM 0x00310ad4 GetHeapEnd
char* GetHeapEnd(Heap heap)							{ return ((SkiaHeap*) heap)->fEnd; }
// ROM 0x00310c98 GetHeapStart
char* GetHeapStart(Heap heap)						{ return ((SkiaHeap*) heap)->fStart; }
// ROM 0x00310c8c GetHeapSize
Size GetHeapSize(Heap heap)							{ return ((SkiaHeap*) heap)->fEnd - ((SkiaHeap*) heap)->fStart; }
// ROM 0x00310c84 GetHeapExtent
Size GetHeapExtent(Heap heap)						{ return ((SkiaHeap*) heap)->fExtent; }
// ROM 0x00310c7c SetHeapExtentUnits
void SetHeapExtentUnits(Heap heap, Size units)		{ ((SkiaHeap*) heap)->fExtentUnits = units; }
// ROM 0x00310c10 GetFixedHeap
Heap GetFixedHeap(Heap heap)						{ return ((SkiaHeap*) heap)->fFixedHeap; }
// ROM 0x00310c18 SetFixedHeap
void SetFixedHeap(Heap heap, Heap fixedHeap)		{ ((SkiaHeap*) heap)->fFixedHeap = (SkiaHeap*) fixedHeap; }
// ROM 0x00310c3c GetMPHeap
Heap GetMPHeap(Heap heap)							{ return ((SkiaHeap*) heap)->fMPHeap; }
// ROM 0x00310c44 SetMPHeap
void SetMPHeap(Heap heap, Heap mpHeap)				{ ((SkiaHeap*) heap)->fMPHeap = (SkiaHeap*) mpHeap; }
// ROM 0x00310c4c GetSPHeap
Heap GetSPHeap(Heap heap)							{ return ((SkiaHeap*) heap)->fSPHeap; }
// ROM 0x00310c54 SetSPHeap
void SetSPHeap(Heap heap, Heap spHeap)				{ ((SkiaHeap*) heap)->fSPHeap = (SkiaHeap*) spHeap; }
// ROM 0x00310c5c GetRelocHeap
Heap GetRelocHeap(Heap heap)						{ return ((SkiaHeap*) heap)->fRelocHeap; }
// ROM 0x00310c64 SetRelocHeap
void SetRelocHeap(Heap heap, Heap relocHeap)		{ ((SkiaHeap*) heap)->fRelocHeap = (SkiaHeap*) relocHeap; }
// ROM 0x00310c6c GetWiredHeap
SWiredHeapDescr* GetWiredHeap(Heap heap)			{ return ((SkiaHeap*) heap)->fWiredHeap; }
// ROM 0x00310c74 SetWiredHeap
void SetWiredHeap(Heap heap, SWiredHeapDescr* wired) { ((SkiaHeap*) heap)->fWiredHeap = wired; }


// ROM 0x00310ca0 GetHeapReleaseable
// What the VM heap holds beyond what it uses.
Size
GetHeapReleaseable(Heap heap)
{
	if (heap == nil)
		heap = GetCurrentHeap();
	SkiaHeap* h = (SkiaHeap*) heap;
	return h->fLimit - h->fExtent;
}


// ROM 0x00310e0c ResurrectSkiaHeap
// A heap found again after a reboot: its pages are neither locked nor asked for.
void
ResurrectSkiaHeap(SkiaHeap* heap)
{
	heap->fRequested = heap->fExtent;
	heap->fLimit = heap->fExtent;
}


// ROM 0x00310e1c GetMinimumHeapSize
Size
GetMinimumHeapSize(void)
{
	return kMinimumHeapSize;
}


// ROM 0x00311298 TotalFreeInHeap
Size
TotalFreeInHeap(Heap heap)
{
	if (heap == nil)
		heap = GetCurrentHeap();
	return ((SkiaHeap*) heap)->fFree;
}


// ROM 0x003112b4 LargestFreeInHeap
// The largest block's data size.
Size
LargestFreeInHeap(Heap heap)
{
	if (heap == nil)
		heap = GetCurrentHeap();
	Size largest = 0;
	for (SkiaBlock* f = ((SkiaHeap*) heap)->fFreeHead; f != nil; f = f->fNext)
	{
		if ((Size) f->fSize > largest)
			largest = f->fSize;
	}
	return largest != 0 ? largest - kBlockHeaderSize : 0;
}


// ROM 0x00311304 CountFreeBlocks
unsigned long
CountFreeBlocks(Heap heap)
{
	if (heap == nil)
		heap = GetCurrentHeap();
	unsigned long n = 0;
	for (SkiaBlock* f = ((SkiaHeap*) heap)->fFreeHead; f != nil; f = f->fNext)
		n++;
	return n;
}


// ROM 0x00311340 TotalUsedInHeap
Size
TotalUsedInHeap(Heap heap)
{
	if (heap == nil)
		heap = GetCurrentHeap();
	SkiaHeap* h = (SkiaHeap*) heap;
	return h->fExtent - h->fFree;
}


// ROM 0x00311364 MaxHeapSize
Size
MaxHeapSize(Heap heap)
{
	if (heap == nil)
		heap = GetCurrentHeap();
	return ((SkiaHeap*) heap)->fMaxSize;
}


// The validation hooks: nothing in this ROM.
long GetHeapValidation(void)	{ return 0; }	// ROM 0x0031088c GetHeapValidation
void SetHeapValidation(void)	{ }				// ROM 0x00310894 SetHeapValidation
void ValidateHeap(void)			{ }				// ROM 0x00310898 ValidateHeap
void ValidateDirectBlock(void)	{ }				// ROM 0x0031089c ValidateDirectBlock
void ValidateIndirectBlock(void) { }			// ROM 0x003108a0 ValidateIndirectBlock
void ValidateMasterPointer(void) { }			// ROM 0x003108a4 ValidateMasterPointer
void ValidateBlockRange(void)	{ }				// ROM 0x003108a8 ValidateBlockRange
void UnscrambleMaster(void)		{ }				// ROM 0x003108ac UnscrambleMaster


/* -------------------------------------------------------------------------------
	Block fields
------------------------------------------------------------------------------- */

UByte GetBlockType(const void* data)			{ return SkiaBlock::Of(data)->fType; }		// ROM 0x00311114 GetBlockType
UByte GetBlockFlags(const void* data)			{ return SkiaBlock::Of(data)->fFlags; }		// ROM 0x0031111c GetBlockFlags
UByte GetBlockBusy(const void* data)			{ return SkiaBlock::Of(data)->fBusy; }		// ROM 0x00311124 GetBlockBusy
void* GetBlockParent(const void* data)			{ return SkiaBlock::Of(data)->fParent; }	// ROM 0x0031112c GetBlockParent
void SetBlockType(void* data, UByte type)		{ SkiaBlock::Of(data)->fType = type; }		// ROM 0x00311134 SetBlockType
void SetBlockFlags(void* data, UByte flags)		{ SkiaBlock::Of(data)->fFlags = flags; }	// ROM 0x0031113c SetBlockFlags
void xSetBlockBusy(void* data, UByte busy)		{ SkiaBlock::Of(data)->fBusy = busy; }		// ROM 0x00311144 xSetBlockBusy
void SetBlockParent(void* data, void* parent)	{ SkiaBlock::Of(data)->fParent = parent; }	// ROM 0x0031114c SetBlockParent
UByte GetBlockDelta(const void* data)			{ return SkiaBlock::Of(data)->fDelta; }		// ROM 0x00311154 GetBlockDelta
void IncrementBlockBusy(void* data)				{ SkiaBlock::Of(data)->fBusy++; }			// ROM 0x00311174 IncrementBlockBusy
void DecrementBlockBusy(void* data)				{ SkiaBlock::Of(data)->fBusy--; }			// ROM 0x00311184 DecrementBlockBusy

// ROM 0x0031115c GetBlockPhysicalSize
Size
GetBlockPhysicalSize(const void* data)
{
	return SkiaBlock::Of(data)->fSize;
}


// ROM 0x00312b54 GetDirectBlockSize
// The size the client asked for.
Size
GetDirectBlockSize(const void* data)
{
	const SkiaBlock* b = SkiaBlock::Of(data);
	return b->fSize - kBlockHeaderSize - b->fDelta;
}


/* -------------------------------------------------------------------------------
	The free list
------------------------------------------------------------------------------- */

// ROM 0x0031084c SetFreeChain
// Links a free block between prev and next (nil: an end of the list).
static void
SetFreeChain(SkiaBlock* block, SkiaBlock* prev, SkiaBlock* next)
{
	SkiaHeap* heap = Current();
	block->fPrev = prev;
	if (prev == nil)
		heap->fFreeHead = block;
	else
		prev->fNext = block;
	block->fNext = next;
	if (next == nil)
		heap->fFreeTail = block;
	else
		next->fPrev = block;
}


// ROM 0x00311d58 RemoveFreeBlock
// Unlinks a free block (the rover moves to the head if it was it); the
// block after it is no longer preceded by free space.
static void
RemoveFreeBlock(SkiaBlock* block)
{
	SkiaHeap* heap = Current();
	SkiaBlock* prev = block->fPrev;
	SkiaBlock* next = block->fNext;
	if (prev == nil)
		heap->fFreeHead = next;
	else
		prev->fNext = next;
	if (next == nil)
		heap->fFreeTail = prev;
	else
		next->fPrev = prev;
	if (heap->fRover == block)
		heap->fRover = heap->fFreeHead;
	block->Following()->fFlags &= ~kBlockFlag_PrevIsFree;
}


// ROM 0x003121ac MoveFreeBlock
// The free block gives up `by` bytes at its start: its header moves up.
static void
MoveFreeBlock(SkiaBlock* block, Size by)
{
	SkiaBlock* prev = block->fPrev;
	Size size = block->fSize;
	SkiaBlock* next = block->fNext;
	SkiaHeap* heap = Current();
	SkiaBlock* rover = heap->fRover;
	SkiaBlock* moved = (SkiaBlock*) ((char*) block + by);
	moved->fFlags = 0;
	moved->fSize = size - by;
	SetFreeChain(moved, prev, next);
	if (rover == block)
		heap->fRover = moved;
}


// ROM 0x00312e68 LockedBlock
// The first busy block between two blocks (from the end of `from`, which may
// be free, up to `upTo`), or nil.
static SkiaBlock*
LockedBlock(SkiaBlock* from, SkiaBlock* upTo)
{
	SkiaBlock* b = from->Following();
	if (b == upTo)
		return nil;
	do
	{
		if (!b->IsFree() && b->fBusy != 0)
			return b;
		b = b->Following();
	} while (b < upTo);
	return nil;
}


/* -------------------------------------------------------------------------------
	Sliding
------------------------------------------------------------------------------- */

// ROM 0x00312ba0 SlideBlocksDown
// Moves the allocated blocks after the free block `f` down over it, until
// `upTo` (the next free block, which merges) is reached; returns the free
// block, now above them.  Only a handle's master pointer is told (a direct
// block is not moved by the callers unless unlocked).
static SkiaBlock*
SlideBlocksDown(SkiaBlock* f, SkiaBlock* upTo)
{
	SkiaHeap* heap = Current();
	SkiaBlock* src = f->Following();
	SkiaBlock* prev = f->fPrev;
	SkiaBlock* next = f->fNext;
	Boolean roverMoved = heap->fRover == f;
	do
	{
		Size byteCount = src->fSize;
		Size fsize = f->fSize;
		if (src->fFlags & kBlockFlag_Indirect)
			((SkiaMasterPointer*) src->fParent)->fBlock = f->Data();
		src->fFlags &= ~kBlockFlag_PrevIsFree;
		MoveBytes(src, f, byteCount);
		f = (SkiaBlock*) ((char*) f + byteCount);
		src = (SkiaBlock*) ((char*) src + byteCount);
		f->fFlags = 0;
		f->fSize = fsize;
		if (src == next)
		{
			src = src->Following();
			f->fSize = fsize + next->fSize;
			if (heap->fRover == next)
				roverMoved = true;
			next = next->fNext;
		}
		else if ((char*) src < heap->fEnd)
			src->fFlags |= kBlockFlag_PrevIsFree;
		SetFreeChain(f, prev, next);
	} while (src < upTo);
	if (roverMoved || heap->fRover->fSize < f->fSize)
		heap->fRover = f;
	return f;
}


// ROM 0x00312d10 SlideBlocksUp
// The mirror: the allocated blocks between `from` (an allocated block, not
// moved) and the free block `free` move up over it, so the free space ends
// up just after `from`; a free block met on the way merges.
static void
SlideBlocksUp(SkiaBlock* from, SkiaBlock* free)
{
	SkiaHeap* heap = Current();
	SkiaBlock* next = free->fNext;
	SkiaBlock* prev = free->fPrev;
	Size fsize = free->fSize;
	Boolean roverMoved = heap->fRover == free;
	SkiaBlock* stop = from->Following();
	if (stop == free)
		return;
	SkiaBlock* src;
	do
	{
		// the last allocated block before `free`
		SkiaBlock* p = stop;
		if (stop <= prev)
			p = prev->Following();
		Size byteCount;
		do
		{
			src = p;
			byteCount = src->fSize;
			p = src->Following();
		} while (p < free);
		SkiaBlock* dst = (SkiaBlock*) ((char*) src + fsize);
		if ((src->fFlags & kBlockFlag_Direct) == 0)
			((SkiaMasterPointer*) src->fParent)->fBlock = dst->Data();
		else if (heap->fMoveHook != nil)
			heap->fMoveHook(src->Data(), dst->Data());
		MoveBytes(src, dst, byteCount);
		SkiaBlock* after = dst->Following();
		dst->fFlags |= kBlockFlag_PrevIsFree;
		if ((char*) after < heap->fEnd)
			after->fFlags &= ~kBlockFlag_PrevIsFree;
		if (src->fFlags & kBlockFlag_PrevIsFree)
		{
			// the free block before it (prev) joins
			if (heap->fRover == after)
				roverMoved = true;
			fsize += prev->fSize;
			src = prev;
			prev = prev->fPrev;
		}
		src->fFlags = 0;
		src->fSize = fsize;
		SetFreeChain(src, prev, next);
		free = src;
	} while (stop < src);
	if (roverMoved || heap->fRover->fSize < src->fSize)
		heap->fRover = src;
}


// ROM 0x00313368 JumpBlock
// The allocated blocks in [from, upTo) each jump to a new block elsewhere
// in the heap, freeing their space (the free blocks among them merge into
// it).
static void
JumpBlock(SkiaBlock* from, SkiaBlock* upTo)
{
	SkiaHeap* heap = Current();
	SkiaBlock* cur = from;
	if (cur->IsFree())
		cur = cur->Following();
	do
	{
		Size size = cur->fSize;
		Size logical = size - kBlockHeaderSize - cur->fDelta;
		SkiaBlock* nb = (SkiaBlock*) NewBlock(logical);
		nb->fFlags = cur->fFlags & ~kBlockFlag_PrevIsFree;
		nb->fBusy = cur->fBusy;
		nb->fType = cur->fType;
		if (cur->fFlags & kBlockFlag_Direct)
		{
			if (heap->fMoveHook != nil)
				heap->fMoveHook(cur->Data(), nb->Data());
		}
		else
			((SkiaMasterPointer*) cur->fParent)->fBlock = nb->Data();
		MoveBytes(&cur->fParent, &nb->fParent, logical + (kBlockHeaderSize - offsetof(SkiaBlock, fParent)));
		SkiaBlock* after = cur->Following();
		if ((char*) after < heap->fEnd && after->IsFree() && after->fSize > 0)
			size += after->fSize;
		KillBlock(cur->Data());
		cur = (SkiaBlock*) ((char*) cur + size);
		heap->fRover = nb->Following();
	} while (cur < upTo);
	if (heap->fRover->fSize < from->fSize)
		heap->fRover = from;
}


// ROM 0x00312ec4 FindSmallestSlide
// Finds the cheapest way to make `size` bytes of free space by merging two
// adjacent free blocks (sliding the allocated blocks between them, which
// must not be busy), does it, and returns the merged free block; nil if
// there is no way.  `blockData` names a block being resized: it may move,
// the new address is written back, and free space that ends up next to it
// needs only `size2` (it grows in place).
static SkiaBlock*
FindSmallestSlide(void** blockData, Size size, Size size2)
{
	SkiaBlock* block = blockData != nil ? SkiaBlock::Of(*blockData) : nil;
	Size best = 0x7fffffff;
	SkiaBlock* bestLow = nil;
	SkiaBlock* bestHigh = nil;
	Size blockSize = 0;
	SkiaHeap* heap = Current();
	SkiaBlock* head = heap->fFreeHead;
	SkiaBlock* next = head->fNext;

	if (next == nil)
	{
		// one free block: everything between the block (or the heap's start) and it slides
		if (block < head)
		{
			if (block == nil)
				block = (SkiaBlock*) heap->fStart;
			if (LockedBlock(block, head) != nil)
				return nil;
			bestLow = block;
			bestHigh = head;
		}
		else
		{
			if (LockedBlock(head, block) != nil)
				return nil;
			blockSize = block->fSize;
			bestHigh = block;
			bestLow = head;
		}
	}
	else
	{
		// a window [head, next] of consecutive free blocks whose sizes add
		// up to what is needed; slid closed at the cheapest place
		Size sum = head->fSize + next->fSize;
		Boolean between = head < block && block < next;
		Size target = between ? size2 : size;
		for (;;)
		{
			if (head == nil)
				return nil;
			Size headSize = head->fSize;
			if (sum < target)
			{
				// widen
				do
				{
					next = next->fNext;
					if (next == nil)
						goto done;
					between = head < block && block < next;
					target = between ? size2 : size;
					sum += next->fSize;
				} while (sum < target);
			}
			Size rest = sum - headSize;
			if (rest > target)
			{
				// narrow from below
				do
				{
					SkiaBlock* h = head->fNext;
					target = (h < block && block < next) ? size2 : size;
					if (rest < target)
						break;
					between = block > head && between;
					head = h;
					headSize = head->fSize;
					rest -= headSize;
				} while (rest > target);
			}
			sum = rest + headSize;
			SkiaBlock* locked = nil;
			if (between)
			{
				// the block lies in the window: the free space may gather
				// below it (it slides down) or above it
				blockSize = block->fSize;
				Size below = size2;
				for (SkiaBlock* f = head; f < block; f = f->fNext)
					below -= f->fSize;
				if (below <= 0)
				{
					Size cost = ((char*) block - (char*) head) - headSize + blockSize;
					if (cost < best)
					{
						locked = LockedBlock(head, block);
						if (locked != nil)
							goto skipLocked;
						bestHigh = block;
						bestLow = head;
						best = cost;
					}
				}
				if (below + sum - size2 >= size2)
				{
					Size cost = ((char*) next - (char*) block) - blockSize;
					if (cost < best)
					{
						locked = LockedBlock(block, next);
						if (locked != nil)
							goto skipLocked;
						bestLow = block;
						bestHigh = next;
						best = cost;
					}
				}
				if (head == next)
					goto advance;
			}
			{
				Size cost = ((char*) next - (char*) head) - headSize;
				if (cost < best)
				{
					locked = LockedBlock(head, next);
					if (locked != nil)
						goto skipLocked;
					bestLow = head;
					bestHigh = next;
					best = cost;
				}
			}
		advance:
			sum -= headSize;
			head = head->fNext;
			if (next == nil)
				break;
			continue;
		skipLocked:
			// past the locked block
			do
			{
				sum -= headSize;
				head = head->fNext;
				if (head == nil)
					return nil;
				headSize = head->fSize;
			} while (head < locked);
			if (next == nil)
				break;
		}
	done:
		if (best >= 0x7fffffff)
			return nil;
	}

	// apply
	if (block < bestLow || block > bestHigh)
		return SlideBlocksDown(bestLow, bestHigh);
	if (bestHigh > block)
		SlideBlocksUp(block, bestHigh);
	if (bestLow >= block)
		return block->Following();
	char* blockEnd = (char*) block + blockSize;
	SkiaBlock* result = SlideBlocksDown(bestLow, (SkiaBlock*) blockEnd);
	// the named block moved down by as much as the slide did; the ROM adds
	// the two addresses to the pointer, which on a host is arithmetic on a
	// pointer that is briefly nowhere, so the distance is worked out first
	*blockData = (char*) *blockData + ((char*) result - blockEnd);
	return result;
}


// ROM 0x003132d8 SearchFreeList
// A free block of at least `size` bytes: round the free list from the
// rover, then, unless this allocation may not compact, by sliding.
static SkiaBlock*
SearchFreeList(Size size)
{
	SkiaHeap* heap = Current();
	if (heap->fFree < size)
		return nil;
	SkiaBlock* start = heap->fRover;
	SkiaBlock* i = start;
	for (;;)
	{
		i = i->fNext;
		if (i == nil)
			i = heap->fFreeHead;
		if (i == start)
			break;
		if ((Size) i->fSize >= size)
		{
			heap->fRover = i;
			return i;
		}
	}
	if (heap->fAllocFlags & kAllocFlag_Weak)
		return nil;
	if ((heap->fAllocFlags & kAllocFlag_NoSlide) && heap->fMoveHook == nil)
		return nil;
	return FindSmallestSlide(nil, size, 0);
}


// ROM 0x0031322c CompactHeap
// Slides every movable block down, merging the free blocks; `keepData`, a
// block's data in the heap, is followed and its new address returned.
void*
CompactHeap(SkiaHeap* heap, void* keepData)
{
	SkiaBlock* f = heap->fFreeHead;
	char* keep = keepData != nil ? (char*) keepData - kBlockHeaderSize : nil;
	Heap saved = GetCurrentHeap();
	SetCurrentHeap(heap);
	SkiaBlock* prev = nil;
	SkiaBlock* cur = nil;
	for (;;)
	{
		prev = cur;
		cur = f;
		SkiaBlock* next = cur != nil ? cur->fNext : nil;
		if (cur == nil || next == nil)
			break;
		if (LockedBlock(cur, next) == nil)
		{
			if (keep != nil && (char*) cur < keep && keep < (char*) next)
				keep -= cur->fSize;
			SlideBlocksDown(cur, next);
			f = prev == nil ? heap->fFreeHead : prev->fNext;
			cur = prev;
		}
		else
			f = next;
	}
	SetCurrentHeap(saved);
	return keep != nil ? keep + kBlockHeaderSize : nil;
}


/* -------------------------------------------------------------------------------
	Growing and shrinking
------------------------------------------------------------------------------- */

// ROM 0x0031091c ExtendVMHeap
// Grows the heap by `needed` bytes rounded to its extent units, moving the
// sentinel up: the last free block grows, or the new space is a free block.
// A VM-backed heap first has the stack manager extend its area and locks
// the new pages in (then unlocks them: they exist now).  False if the heap
// is at its maximum or the pages cannot be had.
Boolean
ExtendVMHeap(SkiaHeap* heap, Size needed)
{
	SkiaBlock* tail = heap->fFreeTail;
	Boolean tailAtEnd = tail != nil && (char*) tail->Following() + kBlockHeaderSize == heap->fEnd;
	Size grow = (heap->fExtentUnits + needed - 1) & ~(heap->fExtentUnits - 1);
	Size newExtent = heap->fExtent + grow;
	if (newExtent > heap->fMaxSize)
		return false;
	if (heap->fIsVMBacked)
	{
		heap->fRequested = newExtent;
		if (newExtent > heap->fLimit)
		{
			if (SetHeapLimits((VAddr) heap->fStart, (VAddr) (heap->fStart + newExtent)) != noErr)
			{
				heap->fRequested = heap->fExtent;
				return false;
			}
			if (LockHeapRange((VAddr) (heap->fStart + heap->fLimit), (VAddr) (heap->fStart + heap->fRequested), (Boolean) heap->fLimit) != noErr)
			{
				SetHeapLimits((VAddr) heap->fStart, (VAddr) (heap->fStart + heap->fLimit));
				heap->fRequested = heap->fExtent;
				return false;
			}
			if (UnlockHeapRange((VAddr) (heap->fStart + heap->fLimit), (VAddr) (heap->fStart + heap->fRequested)) != noErr)
			{
				heap->fRequested = heap->fExtent;
				return false;
			}
			heap->fLimit = heap->fRequested;
		}
	}
	BlockMove(heap->fEnd - kBlockHeaderSize, heap->fEnd + grow - kBlockHeaderSize, kBlockHeaderSize);
	heap->fExtent += grow;
	heap->fFree += grow;
	heap->fEnd = heap->fStart + heap->fExtent;
	if (tailAtEnd)
		tail->fSize += grow;
	else
	{
		SkiaBlock* f = (SkiaBlock*) (heap->fEnd - grow - kBlockHeaderSize);
		f->fFlags = 0;
		f->fSize = grow;
		f->fPrev = heap->fFreeTail;
		f->fNext = nil;
		if (heap->fFreeHead == nil)
			heap->fFreeHead = f;
		if (heap->fFreeTail != nil)
			heap->fFreeTail->fNext = f;
		heap->fFreeTail = f;
		heap->fRover = f;
	}
	return true;
}


// ROM 0x00310adc ShrinkSkiaHeapLeaving
// If the last free block reaches the end, the heap shrinks to leave
// amountLeftFree of it (in extent units); the pages are released later, on
// the stack manager's request (HeapReleaseRequestHandler).
NewtonErr
ShrinkSkiaHeapLeaving(SkiaHeap* heap, Size amountLeftFree)
{
	if (amountLeftFree < (Size) kBlockHeaderSize)
		amountLeftFree = kBlockHeaderSize;
	SkiaBlock* tail = heap->fFreeTail;
	if (tail != nil)
	{
		Size tailSize = tail->fSize;
		char* end = heap->fEnd;
		if ((char*) tail->Following() + kBlockHeaderSize == end)
		{
			char* newEnd = (char*) (((uintptr_t) tail + heap->fExtentUnits + amountLeftFree + kBlockHeaderSize - 1) & ~(uintptr_t) (heap->fExtentUnits - 1));
			if (newEnd < end)
			{
				Size newTailSize = (newEnd - (char*) tail) - kBlockHeaderSize;
				BlockMove(end - kBlockHeaderSize, newEnd - kBlockHeaderSize, kBlockHeaderSize);
				if (newTailSize == 0)
				{
					heap->fFreeHead = nil;			// (sic: the ROM assumes it was the only free block)
					heap->fFreeTail = nil;
				}
				else
					tail->fSize = newTailSize;
				heap->fFree -= tailSize - newTailSize;
				heap->fEnd = newEnd;
				heap->fExtent = newEnd - heap->fStart;
				heap->fRequested = newEnd - heap->fStart;
			}
		}
	}
	return noErr;
}


// ROM 0x00310b98 HeapReleaseRequestHandler
// The stack manager asks what a VM heap can give back: the pages between
// what the heap uses (fRequested) and what it holds (fLimit); with `shrink`
// the heap lets them go.
Boolean
HeapReleaseRequestHandler(SkiaHeap* heap, VAddr* outStart, VAddr* outEnd, Boolean shrink)
{
	if (!IsSkiaHeap(heap))
		return false;
	Boolean releaseable = false;
	if (heap->fRequested < heap->fLimit)
	{
		if (shrink)
			heap->fLimit = heap->fRequested;
		releaseable = true;
		*outEnd = (VAddr) (heap->fStart + heap->fRequested);
	}
	else
		*outEnd = (VAddr) (heap->fStart + heap->fLimit);
	*outStart = (VAddr) heap->fStart;
	return releaseable;
}


// ROM 0x00310cc4 RelocateHeap
// The heap's memory is at a new address: every pointer in it is adjusted.
void
RelocateHeap(SkiaHeap* heap, char* newBase)
{
	char* b = heap->fStart;
	intptr_t delta = newBase - b;
	char* end = heap->fEnd;
	while (b < end)
	{
		SkiaBlock* block = (SkiaBlock*) b;
		if (block->IsFree())
		{
			block->fNext = block->fNext != nil ? (SkiaBlock*) ((char*) block->fNext + delta) : nil;
			block->fPrev = block->fPrev != nil ? (SkiaBlock*) ((char*) block->fPrev + delta) : nil;
		}
		else
		{
			if ((block->fFlags & kBlockFlag_KindMask) != kBlockFlag_Direct)
			{
				SkiaMasterPointer* master = (SkiaMasterPointer*) block->fParent;
				if (((uintptr_t) master->fHeap & 3) != kFakeMasterMark)
				{
					master->fHeap = (char*) master->fHeap + delta;
					master->fBlock = (char*) master->fBlock + delta;
				}
			}
			block->fParent = (char*) block->fParent + delta;
		}
		b += block->fSize;
	}
	for (SkiaMasterPointer* m = heap->fFreeMasters; m != nil; )
	{
		// DEVIATION: the ROM adds the delta to the last master's nil link too,
		// leaving the free master list ending in a bad pointer
		SkiaMasterPointer* next = m->fNextFree;
		m->fNextFree = next != nil ? (SkiaMasterPointer*) ((char*) next + delta) : nil;
		m = next;
	}
	heap->fStart += delta;
	heap->fEnd += delta;
	heap->fFixedHeap = (SkiaHeap*) ((char*) heap->fFixedHeap + delta);
	heap->fFreeHead = heap->fFreeHead != nil ? (SkiaBlock*) ((char*) heap->fFreeHead + delta) : nil;
	heap->fFreeTail = heap->fFreeTail != nil ? (SkiaBlock*) ((char*) heap->fFreeTail + delta) : nil;
	heap->fFreeMasters = heap->fFreeMasters != nil ? (SkiaMasterPointer*) ((char*) heap->fFreeMasters + delta) : nil;
	heap->fRover = heap->fRover != nil ? (SkiaBlock*) ((char*) heap->fRover + delta) : nil;
	heap->fSemaphore = nil;
}


/* -------------------------------------------------------------------------------
	Heaps
------------------------------------------------------------------------------- */

// ROM 0x00311f30 CreatePrivateBlock
// A block of the heap's own: never moved, never freed.
static void
CreatePrivateBlock(SkiaBlock* block, UByte type)
{
	block->fFlags = kBlockFlag_Allocated | kBlockFlag_Private | kBlockFlag_Direct;
	block->fDelta = 0;
	block->fBusy = 0xff;
	block->fType = type;
	block->fParent = GetCurrentHeap();
}


// ROM 0x00310e24 NewHeap
// Lays a heap out in `area`: the header block (this record), one free block,
// the sentinel.  initialSize is what the heap starts as, and its extent
// unit; maxSize is what it may grow to.  Returns the heap (the record).
Heap
NewHeap(void* area, Size maxSize, Size initialSize)
{
	Heap saved = GetCurrentHeap();
	char* base = (char*) area;
	SkiaHeap* heap = (SkiaHeap*) (base + kBlockHeaderSize);
	FillBytes(area, initialSize, 0);
	SetCurrentHeap(heap);
	CreatePrivateBlock((SkiaBlock*) base, kBlockType_HeapHeader);
	((SkiaBlock*) base)->fSize = kHeapHeaderSize;
	SkiaBlock* first = (SkiaBlock*) (base + kFirstBlockOffset);
	heap->fStart = base;
	heap->fMagic = kSkiaHeapMagic;
	heap->fMaxSize = maxSize;
	heap->fExtentUnits = initialSize;
	heap->fExtent = initialSize;
	heap->fLimit = initialSize;
	heap->fRequested = initialSize;
	heap->fIsVMBacked = 0;
	heap->fRefCon = nil;
	heap->fFreeHead = first;
	heap->fEnd = base + heap->fExtent;
	heap->fFreeTail = first;
	heap->fRover = first;
	heap->fFixedHeap = heap;
	heap->fMPHeap = heap;
	heap->fSPHeap = heap;
	heap->fRelocHeap = heap;
	Size firstSize = heap->fExtent - kHeapOverhead;
	heap->fFree = firstSize;
	heap->fWiredHeap = nil;
	first->fFlags = 0;
	first->fSize = firstSize;
	first->fNext = nil;
	first->fPrev = nil;
	first->fOwner = 0;
	SkiaBlock* sentinel = first->Following();
	CreatePrivateBlock(sentinel, kBlockType_EndSentinel);
	sentinel->fFlags |= kBlockFlag_PrevIsFree;
	sentinel->fSize = kBlockHeaderSize;
	sentinel->fParent = heap;
	heap->fMastersPerChunk = kMastersPerChunk;
	heap->fUnknown80 = 0;
	heap->fSemaphore = nil;
	heap->fFreeMasters = nil;
	SetCurrentHeap(saved);
	return heap;
}


// ROM 0x00310f38 GetHeaps
// Lists heaps: `count` of them (all, -1) after skipping `skip`, from the
// children of `from` (nil: the first heap); returns how many.
int
GetHeaps(SkiaHeap* from, int skip, int count, SkiaHeap** outHeaps)
{
	SkiaHeap* h = from == nil ? (SkiaHeap*) GetFirstHeap() : from->fChildHeap;
	if (skip == 1 && count == -1 && h == nil)
		return 0;
	for (skip--; skip != 0 && h != nil; skip--)
		h = h->fNextHeap;
	int left = count;
	for (; left != 0 && h != nil; left--)
	{
		if (outHeaps != nil)
			*outHeaps++ = h;
		h = h->fNextHeap;
	}
	if (count == -1)
		count = -1 - left;
	return count;
}


// ROM 0x00311194 FindHeap
// The innermost heap whose range holds the address, or nil.
SkiaHeap*
FindHeap(const void* addr)
{
	SkiaHeap* h = (SkiaHeap*) GetFirstHeap();
	SkiaHeap* found = nil;
	while (h != nil)
	{
		if ((const char*) addr < h->fStart)
			return found;
		if ((const char*) addr < h->fEnd)
		{
			found = h;
			h = h->fChildHeap;
		}
		else
			h = h->fNextHeap;
	}
	return found;
}


// ROM 0x003111e0 FindBlock
// The block of the current heap holding the address: the last free block
// at or below it, then along the blocks from there (the sentinel for the
// heap's end).
SkiaBlock*
FindBlock(const void* addr)
{
	SkiaHeap* heap = Current();
	if (heap == nil)
		return nil;
	SkiaBlock* f = heap->fFreeHead;
	for (;;)
	{
		if (f == nil)
		{
			if ((const char*) addr == heap->fEnd)
				return heap->Sentinel();
			f = heap->fFreeTail;
			break;
		}
		if (addr < f)
		{
			f = f->fPrev;
			break;
		}
		f = f->fNext;
	}
	SkiaBlock* b;
	if (f == nil)
		b = (SkiaBlock*) heap->fStart;
	else
	{
		if (f <= addr && addr < f->Following())
			return f;
		b = f->Following();
	}
	if (addr <= b)
		return b;
	for (;;)
	{
		SkiaBlock* next = b->Following();
		if (addr < next)
			return b;
		b = next;
		if (next >= addr)
			return next;
	}
}


/* -------------------------------------------------------------------------------
	Blocks
------------------------------------------------------------------------------- */

// ROM 0x00311db8 NewBlock  (0x002ecaa0 NewBlockLow is the same entry)
// A block for `size` bytes from the current heap: the first free block from
// the rover that holds it (taking it whole if what would be left is
// smaller than a header), else what SearchFreeList finds (sliding), else
// the heap grows.  The block is returned with its flags clear.
static SkiaBlock*
NewBlock(Size size)
{
	SkiaHeap* heap = Current();
	UByte delta;
	Size physical = PhysicalSize(size, &delta);
	heap->fRover = heap->fFreeHead;
	for (;;)
	{
		SkiaBlock* f = heap->fRover;
		if (f != nil && (Size) f->fSize >= physical)
		{
			Size taken = f->fSize;
			if (taken - physical <= (Size) kBlockSlop)
			{
				delta = (UByte) (delta + (taken - physical));
				RemoveFreeBlock(f);
			}
			else
			{
				MoveFreeBlock(f, physical);
				taken = physical;
			}
			heap->fFree -= taken;
			f->fFlags = 0;
			f->fDelta = delta;
			heap->fDeltaSum += delta;
			heap->fDeltaWordsSum += delta & kBlockWordMask;
			f->fSize = taken;
			f->fBusy = 0;
			f->fOwner = gCurrentTaskId;
			if (heap->fMoveHook == nil)
				heap->fRover = heap->fFreeHead;
			if (heap->fAfterAllocHook != nil)
				heap->fAfterAllocHook();
			return f;
		}
		f = SearchFreeList(physical);
		if (f != nil && (Size) f->fSize >= physical)
			continue;
		if (!ExtendVMHeap(heap, physical))
			return nil;
	}
}


// ROM 0x00311ee4 NewDirectBlock
// A Ptr's block.
void*
NewDirectBlock(Size size)
{
	SkiaHeap* heap = Current();
	SkiaBlock* b = NewBlock(size);
	void* data = nil;
	if (b != nil)
	{
		b->fFlags = kBlockFlag_Allocated | kBlockFlag_Direct;
		b->fParent = GetCurrentHeap();
		data = b->Data();
	}
	heap->fAllocFlags = 0;
	return data;
}


// ROM 0x00312ce4 NewWeakBlock
// A direct block that is not worth compacting for.
void*
NewWeakBlock(Size size)
{
	SkiaHeap* heap = Current();
	heap->fAllocFlags |= kAllocFlag_Weak;
	SkiaBlock* b = NewBlock(size);
	void* data = nil;
	if (b != nil)
	{
		b->fFlags = kBlockFlag_Allocated | kBlockFlag_Direct;
		b->fParent = GetCurrentHeap();
		data = b->Data();
	}
	heap->fAllocFlags = 0;
	return data;
}


// ROM 0x002ebce8 (unnamed) - static: a weak block from any heap of a list, depth first
static void*
NewWeakBlockInHeaps(SkiaHeap* heap, Size size)
{
	for (;;)
	{
		SetCurrentHeap(heap);
		void* data = NewWeakBlock(size);
		if (data != nil)
			return data;
		if (heap->fChildHeap != nil && (data = NewWeakBlockInHeaps(heap->fChildHeap, size)) != nil)
			return data;
		heap = heap->fNextHeap;
		if (heap == nil)
			return nil;
	}
}


// ROM 0x00311058 NewTemporaryBlock
// A locked direct block, from the current heap or any other.
void*
NewTemporaryBlock(Size size)
{
	Heap saved = nil;
	void* data = NewWeakBlock(size);
	if (data == nil)
	{
		saved = GetCurrentHeap();
		data = NewWeakBlockInHeaps((SkiaHeap*) GetFirstHeap(), size);
	}
	SkiaBlock* b = SkiaBlock::Of(data);
	b->fFlags |= kBlockFlag_Temporary;
	b->fBusy++;
	if (saved != nil)
		SetCurrentHeap(saved);
	return data;
}


// ROM 0x003110bc DisposeTemporaryBlock
void
DisposeTemporaryBlock(void* data)
{
	Heap saved = GetCurrentHeap();
	SetCurrentHeap(FindHeap(data));
	SkiaBlock::Of(data)->fBusy--;
	DisposeDirectBlock(data);
	SetCurrentHeap(saved);
}


// ROM 0x00312220 KillBlock  (0x002ed0c4 DisposeDirectBlock is the same entry)
// Frees a block: it joins the free block before it and/or after it, or
// becomes one, in its place in the free list; the rover falls back to it
// if it is lower.
void
KillBlock(void* data)
{
	SkiaHeap* heap = Current();
	SkiaBlock* b = SkiaBlock::Of(data);
	SkiaBlock* prevFree = nil;
	Size size = b->fSize;
	SkiaBlock* after = b->Following();
	Boolean prevIsFree = (b->fFlags & kBlockFlag_PrevIsFree) != 0;
	Boolean afterIsEnd = (char*) after >= heap->fEnd;
	Boolean afterIsFree = !afterIsEnd && after->IsFree();
	heap->fDeltaSum -= b->fDelta;
	heap->fDeltaWordsSum -= b->fDelta & kBlockWordMask;
	if (!afterIsFree)
	{
		if (!afterIsEnd)
			after->fFlags |= kBlockFlag_PrevIsFree;
		// the free blocks around it
		SkiaBlock* f = heap->fFreeHead;
		while (f != nil && f < b)
		{
			prevFree = f;
			f = f->fNext;
		}
		after = f;
	}
	heap->fFree += size;
	SkiaBlock* freed;
	if (!prevIsFree)
	{
		b->fOwner = 0;
		b->fFlags = 0;
		b->fSize = size;
		if (afterIsFree)
		{
			SetFreeChain(b, after->fPrev, after->fNext);
			b->fSize += after->fSize;
		}
		else
			SetFreeChain(b, prevFree, after);
		freed = b;
	}
	else
	{
		freed = prevFree;
		if (afterIsFree)
		{
			Size afterSize = after->fSize;
			freed = after->fPrev;
			freed->fSize += afterSize;
			RemoveFreeBlock(after);
			SkiaBlock* beyond = (SkiaBlock*) ((char*) after + afterSize);
			if ((char*) beyond < heap->fEnd)
				beyond->fFlags |= kBlockFlag_PrevIsFree;
		}
		freed->fSize += size;
	}
	if (heap->fRover == nil || heap->fRover > freed)
		heap->fRover = freed;
}


void
DisposeDirectBlock(void* data)
{
	KillBlock(data);
}


// ROM 0x003123dc TrySetSize
// How a block might grow by `grow` bytes (to `newPhysical`): 1 in place
// (the free block after it holds the growth - if need be after the blocks
// in the way jumped elsewhere, or after sliding), 3 by sliding down into
// the free block before it, 4 by moving to a fresh block, 0 not at all.
// `blockPtr` names the block header and is updated if the block slid.
static int
TrySetSize(SkiaBlock** blockPtr, Size grow, Size newPhysical)
{
	SkiaBlock* b = *blockPtr;
	Size bsize = b->fSize;
	SkiaBlock* after = b->Following();
	SkiaHeap* heap = Current();
	if (after <= heap->fFreeTail && after->IsFree() && (Size) after->fSize >= grow)
		return 1;
	if ((char*) after < heap->fEnd && bsize > grow)
	{
		// the blocks after it: could the allocated ones jump and leave the room?
		Size allocated = 0, freed = 0;
		SkiaBlock* p = after;
		Boolean locked = false;
		do
		{
			if (!p->IsFree())
			{
				if (p->fBusy != 0)
				{
					locked = true;
					break;
				}
				allocated += p->fSize;
			}
			else
				freed += p->fSize;
			p = p->Following();
		} while (allocated + freed < grow && (char*) p < heap->fEnd);
		if (!locked && heap->fMoveHook != nil && allocated + freed >= grow && allocated < bsize
			&& (Size) heap->fRover->fSize >= allocated && (heap->fRover < b || heap->fRover >= p))
		{
			JumpBlock(after, p);
			return 1;
		}
	}
	if (heap->fRover != nil && (Size) heap->fRover->fSize >= newPhysical)
		return 4;
	// the free blocks below it (the rover takes the largest met)
	Size below = 0;
	Boolean prevIsFree = (b->fFlags & kBlockFlag_PrevIsFree) != 0;
	SkiaBlock* last = nil;
	SkiaBlock* f = heap->fFreeHead;
	while (f != nil && f < b)
	{
		below += f->fSize;
		if (heap->fRover->fSize < f->fSize)
			heap->fRover = f;
		last = f;
		f = f->fNext;
	}
	if (prevIsFree && (Size) last->fSize >= grow)
		return 3;
	if (heap->fRover != nil && (Size) heap->fRover->fSize >= newPhysical)
		return 4;
	if (heap->fFree - below > newPhysical)
	{
		for (; f != nil; f = f->fNext)
		{
			if (heap->fRover->fSize < f->fSize)
				heap->fRover = f;
		}
		if (heap->fRover != nil && (Size) heap->fRover->fSize >= newPhysical)
			return 4;
	}
	// sliding: not in the fixed or master-pointer heap of a segregated set
	if ((heap->fFixedHeap == heap || heap->fMPHeap == heap) && heap->fRelocHeap != heap)
		return 0;
	void* data = b->Data();
	SkiaBlock* room = FindSmallestSlide(&data, newPhysical, grow);
	if (room == nil)
		return 0;
	*blockPtr = SkiaBlock::Of(data);
	if ((Size) room->fSize >= grow && (SkiaBlock*) ((char*) *blockPtr + bsize) == room)
		return 1;
	if ((Size) room->fSize >= newPhysical)
	{
		heap->fRover = room;
		return 4;
	}
	return 0;
}


// ROM 0x0031266c SetBlockSize
// Resizes a block to newSize bytes; returns its data, wherever it is now,
// or nil if the heap cannot hold it (having asked the heap's hooks and
// tried to grow).  A shrink gives the tail back (a small one is absorbed
// in the delta); a growth goes as TrySetSize says.
void*
SetBlockSize(void* data, Size newSize)
{
	SkiaBlock* b = SkiaBlock::Of(data);
	Boolean askHook = true;			// the out-of-memory hook is asked until it says it did nothing
	Size oldPhysical = b->fSize;
	SkiaHeap* heap = Current();
	UByte delta;
	Size newPhysical = PhysicalSize(newSize, &delta);
	Size diff = newPhysical - oldPhysical;
	int how;
	if (diff == 0)
	{
		heap->fDeltaSum -= b->fDelta - delta;
		heap->fDeltaWordsSum -= b->fDelta & kBlockWordMask;
		b->fDelta = delta;
		goto finish;
	}
	if (diff < 0)
	{
		heap->fDeltaSum -= b->fDelta;
		heap->fDeltaWordsSum -= b->fDelta & kBlockWordMask;
		Size shrink = -diff;
		if (shrink < (Size) kBlockSlop)
		{
			delta = (UByte) (delta + shrink);
			newPhysical += shrink & kBlockWordMask;
		}
		else
		{
			// the tail becomes a block of its own and is freed.  (The ROM sets
			// only its flag byte; its delta, which KillBlock subtracts from the
			// heap's sum, is whatever the bytes held - here it is zero.)
			SkiaBlock* tail = (SkiaBlock*) ((char*) b + newPhysical);
			tail->fFlags = 0;
			tail->fDelta = 0;
			tail->fBusy = 0;
			tail->fType = 0;
			tail->fSize = shrink;
			KillBlock(tail->Data());
		}
		goto addDelta;
	}

checkRoom:
	if (heap->fFree < diff)
		goto outOfRoom;
tryResize:
	how = TrySetSize(&b, diff, newPhysical);
	if (how == 3)
	{
		// slide down into the free block before it
		SkiaBlock* prev = nil;
		for (SkiaBlock* f = heap->fFreeHead; f != nil && f < b; f = f->fNext)
			prev = f;
		Size fsize = prev->fSize;
		SkiaBlock* fnext = prev->fNext;
		SkiaBlock* fprev = prev->fPrev;
		if (b->fFlags & kBlockFlag_Direct)
		{
			if (heap->fMoveHook != nil)
				heap->fMoveHook(b->Data(), prev->Data());
		}
		else
			((SkiaMasterPointer*) b->fParent)->fBlock = prev->Data();
		MoveBytes(b, prev, oldPhysical);
		SkiaBlock* oldPrev = prev;
		b = prev;
		b->fFlags &= ~kBlockFlag_PrevIsFree;
		SkiaBlock* freed = (SkiaBlock*) ((char*) prev + oldPhysical);
		freed->fFlags = 0;
		freed->fSize = fsize;
		SetFreeChain(freed, fprev, fnext);
		if (heap->fRover == oldPrev)
			heap->fRover = freed;
		SkiaBlock* after = freed->Following();
		if ((char*) after < heap->fEnd)
		{
			if (!after->IsFree())
				after->fFlags |= kBlockFlag_PrevIsFree;
			else
			{
				SkiaBlock* joined = after->fPrev;
				joined->fSize += after->fSize;
				SetFreeChain(joined, joined->fPrev, after->fNext);
			}
		}
		how = 1;
	}
	if (how == 1)
	{
		// grow in place into the free block after it
		SkiaBlock* nextFree = (SkiaBlock*) ((char*) b + oldPhysical);
		heap->fDeltaSum -= b->fDelta;
		heap->fDeltaWordsSum -= b->fDelta & kBlockWordMask;
		if ((Size) nextFree->fSize < diff + (Size) kBlockSlop)
		{
			Size extra = nextFree->fSize - diff;
			RemoveFreeBlock(nextFree);
			diff += extra;
			delta = (UByte) (delta + extra);
			newPhysical += extra;
		}
		else
		{
			// the free block's header moves up by diff (which may be less than
			// a header: its fields are read before any is written)
			SkiaBlock* moved = (SkiaBlock*) ((char*) nextFree + diff);
			SkiaBlock* fprev = nextFree->fPrev;
			SkiaBlock* fnext = nextFree->fNext;
			Size fsize = nextFree->fSize;
			moved->fFlags = 0;
			moved->fPrev = fprev;
			if (fprev == nil)
				heap->fFreeHead = moved;
			else
				fprev->fNext = moved;
			moved->fNext = fnext;
			if (fnext == nil)
				heap->fFreeTail = moved;
			else
				fnext->fPrev = moved;
			if (heap->fRover == nextFree)
				heap->fRover = moved;
			moved->fSize = fsize - diff;
		}
		heap->fFree -= diff;
		goto addDelta;
	}
	if (how == 4)
	{
		// a fresh block
		SkiaBlock* nb = NewBlock(newSize);
		nb->fFlags = b->fFlags & ~kBlockFlag_PrevIsFree;
		nb->fBusy = b->fBusy;
		nb->fType = b->fType;
		CopyBytes(&b->fParent, &nb->fParent, oldPhysical - offsetof(SkiaBlock, fParent));
		if (b->fFlags & kBlockFlag_Indirect)
			((SkiaMasterPointer*) b->fParent)->fBlock = nb->Data();
		KillBlock(data);
		b = nb;
		goto finish;
	}
	if (how == 5)
		goto setSize;

outOfRoom:
	{
		b->fBusy += 2;			// not to be moved by what the hooks do
		Boolean tryAgain = false;
		if (heap->fOutOfMemoryHook != nil && askHook)
		{
			askHook = heap->fOutOfMemoryHook(newPhysical) != 0;
			tryAgain = true;
		}
		else if (heap->fFree < newSize)
		{
			if (heap->fReleaseHook != nil && heap->fReleaseHook(heap, newPhysical - heap->fFree) != 0)
				tryAgain = true;
			else if (ExtendVMHeap(heap, newPhysical))
				tryAgain = true;
			else
			{
				b->fBusy -= 2;
				if (heap->fAfterAllocHook != nil)
					heap->fAfterAllocHook();
				return nil;
			}
		}
		b->fBusy -= 2;
		if (tryAgain)
			goto tryResize;
		if (!ExtendVMHeap(heap, newPhysical))
			return nil;
		goto checkRoom;
	}

addDelta:
	heap->fDeltaSum += delta;
	heap->fDeltaWordsSum += delta & kBlockWordMask;
setSize:
	b->fSize = newPhysical;
	b->fDelta = delta;
finish:
	if (heap->fMoveHook == nil)
		heap->fRover = heap->fFreeHead;
	if (heap->fAfterAllocHook != nil)
		heap->fAfterAllocHook();
	return b->Data();
}


// ROM 0x00312b50 SetDirectBlockSize
void*
SetDirectBlockSize(void* data, Size newSize)
{
	return SetBlockSize(data, newSize);
}


/* -------------------------------------------------------------------------------
	Master pointers and indirect blocks
------------------------------------------------------------------------------- */

// ROM 0x00311f68 AllocateMoreMasters
// A private block of fMastersPerChunk master pointers (in the master
// pointer heap, if that is another), chained onto the free master list.
static void
AllocateMoreMasters(void)
{
	SkiaHeap* heap = Current();
	SkiaHeap* mpHeap = heap->fMPHeap;
	if (mpHeap != heap)
		SetCurrentHeap(mpHeap);
	SkiaBlock* block = NewBlock(heap->fMastersPerChunk * sizeof(SkiaMasterPointer));
	if (mpHeap != heap)
		SetCurrentHeap(heap);
	if (block == nil)
		return;
	heap->fFreeMasterCount += heap->fMastersPerChunk;
	CreatePrivateBlock(block, kBlockType_MasterPointers);
	SkiaMasterPointer* masters = (SkiaMasterPointer*) block->Data();
	SkiaMasterPointer* chain = nil;
	for (ULong i = 0; i < heap->fMastersPerChunk; i++)
	{
		masters[i].fNextFree = chain;
		masters[i].fBlock = kMasterPointer_Free;
		chain = &masters[i];
	}
	SkiaMasterPointer* last = heap->fFreeMasters;
	if (last != nil)
	{
		while (last->fNextFree != nil)
			last = last->fNextFree;
		last->fNextFree = chain;
	}
	else
		heap->fFreeMasters = chain;
}


// ROM 0x00312040 AllocateMasterPointer
static SkiaMasterPointer*
AllocateMasterPointer(SkiaHeap* heap)
{
	SkiaMasterPointer* m = heap->fFreeMasters;
	if (m == nil)
	{
		AllocateMoreMasters();
		m = heap->fFreeMasters;
	}
	if (m == nil)
		return nil;
	heap->fFreeMasterCount--;
	heap->fFreeMasters = m->fNextFree;
	m->fNextFree = nil;
	return m;
}


// ROM 0x0031208c FreeMasterPointer
// (to the relocatable heap's free list, where handles live)
static void
FreeMasterPointer(SkiaHeap* heap, SkiaMasterPointer* m)
{
	m->fNextFree = heap->fRelocHeap->fFreeMasters;
	m->fBlock = kMasterPointer_Free;
	heap->fRelocHeap->fFreeMasters = m;
	heap->fRelocHeap->fFreeMasterCount++;
}


// ROM 0x003120bc NewIndirectBlock
// A Handle's block: a master pointer, and a block (after compacting the
// heap if the free list has more than one block).
SkiaMasterPointer*
NewIndirectBlock(Size size)
{
	SkiaHeap* heap = Current();
	SkiaBlock* tail = heap->fFreeTail;
	if (tail != nil && tail->fPrev != nil)
		CompactHeap(heap, nil);
	SkiaMasterPointer* m = AllocateMasterPointer(heap);
	if (m == nil)
		return nil;
	if (heap->fFreeTail != nil)
		heap->fRover = heap->fFreeTail;
	SkiaBlock* b = NewBlock(size);
	if (b == nil)
	{
		FreeMasterPointer(heap, m);
		return nil;
	}
	m->fBlock = b->Data();
	m->fHeap = heap;
	b->fFlags = kBlockFlag_Allocated | kBlockFlag_Indirect;
	b->fParent = m;
	return m;
}


// ROM 0x0031214c NewFakeIndirectBlock
// A handle on memory that is not a block (NewFakeHandle): a master pointer
// with the size where the heap would be.
SkiaMasterPointer*
NewFakeIndirectBlock(void* address, Size size)
{
	SkiaHeap* heap = Current();
	SkiaMasterPointer* m = AllocateMasterPointer(heap);
	if (m != nil)
	{
		m->fFakeSize = ((ULong) size << 2) | kFakeMasterMark;
		m->fBlock = address;
	}
	return m;
}


// ROM 0x00312188 IsFakeIndirectBlock
Boolean
IsFakeIndirectBlock(const SkiaMasterPointer* m)
{
	return m != nil && (m->fFakeSize & 3) == kFakeMasterMark;
}


// ROM 0x00312208 GetFakeIndirectBlockSize
Size
GetFakeIndirectBlockSize(const SkiaMasterPointer* m)
{
	return m != nil ? (Size) (m->fFakeSize >> 2) : 0;
}


// ROM 0x003123a8 DisposeIndirectBlock
void
DisposeIndirectBlock(SkiaMasterPointer* m)
{
	if (!IsFakeIndirectBlock(m))
		KillBlock(m->fBlock);
	FreeMasterPointer(Current(), m);
}


// ROM 0x00312b68 SetIndirectBlockSize
// Returns the block's data (its new address), nil if it could not be resized
// (the ROM's callers read that from r0; a fake block gives its test result).
void*
SetIndirectBlockSize(SkiaMasterPointer* m, Size newSize)
{
	if (IsFakeIndirectBlock(m))
		return nil;
	void* data = SetBlockSize(m->fBlock, newSize);
	if (data != nil)
		m->fBlock = data;
	return data;
}


// ROM 0x00312cac GetIndirectBlockSize
Size
GetIndirectBlockSize(const SkiaMasterPointer* m)
{
	if (IsFakeIndirectBlock(m))
		return 0;
	return GetDirectBlockSize(m->fBlock);
}


/* -------------------------------------------------------------------------------
	Bytes
------------------------------------------------------------------------------- */

// ROM 0x00311100 MoveBytes
void
MoveBytes(const void* srcPtr, void* destPtr, Size byteCount)
{
	memmove(destPtr, srcPtr, (size_t) byteCount);
}


// ROM 0x00311104 BlockMove
extern "C" void
BlockMove(const void* srcPtr, void* destPtr, Size byteCount)
{
	memmove(destPtr, srcPtr, (size_t) byteCount);
}


// ROM 0x00311170 CopyBytes
// (the ROM's copies forward; the ranges of its callers do not overlap the wrong way)
void
CopyBytes(const void* srcPtr, void* destPtr, Size byteCount)
{
	memmove(destPtr, srcPtr, (size_t) byteCount);
}


// ROM 0x0031139c ZeroBytes
extern "C" void
ZeroBytes(void* p, Size length)
{
	memset(p, 0, (size_t) length);
}


// ROM 0x00311488 FillBytes
extern "C" void
FillBytes(void* p, Size length, UChar pattern)
{
	memset(p, pattern, (size_t) length);
}


// ROM 0x003114c4 FillLongs
// `length` bytes of the pattern, a word at a time (the tail bytes take the
// pattern's leading bytes).
extern "C" void
FillLongs(void* p, Size length, ULong pattern)
{
	uint32_t word = (uint32_t) pattern;
	char* dst = (char*) p;
	for (; length >= 4; length -= 4, dst += 4)
		memcpy(dst, &word, 4);
	if (length > 0)
		memcpy(dst, &word, (size_t) length);
}


// ROM 0x00311594 EqualBytes
extern "C" int
EqualBytes(const void* a, const void* b, Size length)
{
	return memcmp(a, b, (size_t) length) == 0;
}


// ROM 0x00311634 XORBytes
extern "C" void
XORBytes(const void* src1, const void* src2, void* dest, Size size)
{
	const UChar* a = (const UChar*) src1;
	const UChar* b = (const UChar*) src2;
	UChar* d = (UChar*) dest;
	for (Size i = 0; i < size; i++)
		d[i] = a[i] ^ b[i];
}
