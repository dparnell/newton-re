/*
	File:		memory/SkiaHeap.h

	Contains:	The "Skia" heap - the memory manager's heap behind NewPtr,
				NewHandle and the VM heaps (NewtonMemory.h).  Nothing of it is
				in the DDK; these declarations follow the ROM (0x002eb538-
				0x002ee100, and the API at 0x00143000-0x00145290).

	A heap is one area of memory: a header block holding the SkiaHeap
	record, then blocks, then an end sentinel.  Every block, free or
	allocated, carries a 16-byte (on the ARM) header; the free blocks are on
	a doubly linked list in address order (fFreeHead/fFreeTail), and each
	block's flags say whether the block before it is free, so a freed block
	coalesces with its neighbours.  Allocation is first-fit from a rover
	(fRover); when nothing fits, the free blocks are merged by sliding the
	allocated blocks between them (SlideBlocksDown/Up, FindSmallestSlide -
	a block whose busy count is non-zero does not move), or the heap grows
	(ExtendVMHeap, in fExtentUnits, up to fMaxSize).  A "direct" block is a
	Ptr (busy from birth, so it never moves); an "indirect" block is a
	Handle: its master pointer, allocated in chunks (AllocateMoreMasters),
	is what the client holds, and points at the block wherever it moves.

	Representation on the host: the ARM's free block keeps its size in the
	first word, whose top byte (the flag byte of an allocated block) has bit
	7 clear - a big-endian trick a portable heap cannot use, so here a free
	block has the same header as an allocated one (flags 0, size in fSize).
	Pointers are host-sized, so the header is 32 bytes and the SkiaHeap
	record larger than the ROM's 0xbc; the ROM offsets are noted.  The
	behaviour is the ROM's.
*/

#ifndef __SKIAHEAP_H
#define __SKIAHEAP_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif
#ifndef __NEWTONMEMORY_H
#include "NewtonMemory.h"
#endif

class TULockingSemaphore;
struct SWiredHeapDescr;


// SkiaBlock::fFlags
enum
{
	kBlockFlag_Allocated	= 0x80,		// clear for a free block
	kBlockFlag_Private		= 0x10,		// the heap's own blocks (header, sentinel, master pointers): fType says which
	kBlockFlag_Temporary	= 0x08,		// NewTemporaryBlock's
	kBlockFlag_PrevIsFree	= 0x04,		// the block before this one is free
	kBlockFlag_Indirect		= 0x02,		// a Handle's block: fParent is its master pointer
	kBlockFlag_Direct		= 0x01,		// a Ptr's block: fParent is its heap
	kBlockFlag_KindMask		= 0x03
};

// SkiaBlock::fType of a private block
enum
{
	kBlockType_HeapHeader	= 3,
	kBlockType_EndSentinel	= 4,
	kBlockType_MasterPointers = 5
};

// The block header, before the block's data.  (ROM: 16 bytes; a free block
// overlays fSize/fNext/fPrev on the first three words.)
struct SkiaBlock
{
	UByte		fFlags;			// +0x00  kBlockFlag_*
	UByte		fDelta;			// +0x01  bytes between the requested size and the physical size (rounding to the word, and slop)
	UByte		fBusy;			// +0x02  lock count: a busy block does not move (0xff: private)
	UByte		fType;			// +0x03  kBlockType_* for a private block; the client's type byte otherwise
	ULong		fSize;			// +0x04  physical size, header included (a free block's, in the ROM, at +0x00)
	union
	{
		void*		fParent;	// +0x08  the heap (direct) or the master pointer (indirect)
		SkiaBlock*	fNext;		// +0x04  free block: the next free block (higher address)
	};
	union
	{
		ULong		fOwner;		// +0x0c  the owning task's id, or (top bit set) a name
		SkiaBlock*	fPrev;		// +0x08  free block: the previous free block
	};

	Boolean			IsFree() const			{ return (fFlags & kBlockFlag_Allocated) == 0; }
	void*			Data()					{ return this + 1; }
	SkiaBlock*		Following()				{ return (SkiaBlock*) ((char*) this + fSize); }
	static SkiaBlock*	Of(const void* data)	{ return (SkiaBlock*) data - 1; }
};

const ULong kBlockHeaderSize = sizeof(SkiaBlock);		// 0x10 on the ARM
const ULong kBlockAlign = sizeof(ULong);				// physical sizes are multiples of the word (4 on the ARM)
const ULong kBlockWordMask = ~(kBlockAlign - 1);		// (the ROM's & ~3)
const ULong kBlockSlop = kBlockHeaderSize;				// a remainder no bigger than a header is not split off (the ROM's 0x10)


// A master pointer: what a Handle points at.  Two words (ROM 8 bytes).
struct SkiaMasterPointer
{
	void*				fBlock;			// the block's data, or kMasterPointer_Free
	union
	{
		void*				fHeap;		// the heap the block is in
		SkiaMasterPointer*	fNextFree;	// on the free master list
		ULong				fFakeSize;	// a fake handle (NewFakeHandle): (size << 2) | 1
	};
};

#define kMasterPointer_Free		((void*) (uintptr_t) 0xc0000000)
const ULong kFakeMasterMark = 1;		// low bits of fFakeSize


// The hooks a heap may carry (the NewtonScript object heap uses them)
typedef void	(*SkiaMoveHookProc)(void* oldData, void* newData);	// a direct block moved
typedef long	(*SkiaOutOfMemoryHookProc)(Size needed);			// nothing fits: non-zero if something was freed
typedef void	(*SkiaAfterAllocHookProc)(void);					// after every allocation or resize
typedef long	(*SkiaReleaseHookProc)(void* heap, Size shortfall);	// before the heap grows: non-zero if room was made


// The heap record, in the heap's header block.  (ROM: 0xbc bytes at the
// heap pointer, which is the header block's data; the offsets given.)
struct SkiaHeap
{
	char*				fStart;				// +0x00  the area
	char*				fEnd;				// +0x04  fStart + fExtent: the sentinel is the block before it
	ULong				fMagic;				// +0x08  kSkiaHeapMagic
	SkiaHeap*			fFixedHeap;			// +0x0c  where Ptrs go (self, or the fixed heap of a segregated set)
	SkiaHeap*			fRelocHeap;			// +0x10  where Handles go
	SWiredHeapDescr*	fWiredHeap;			// +0x14  the wired (never paged) sub-heap, made on demand
	void*				fRefCon;			// +0x18
	Size				fFree;				// +0x1c  bytes in free blocks
	SkiaBlock*			fFreeHead;			// +0x20  free list, in address order
	SkiaBlock*			fFreeTail;			// +0x24
	Size				fMaxSize;			// +0x28  the area's size: the heap may grow to it
	Size				fExtent;			// +0x2c  the heap's current size
	Size				fLimit;				// +0x30  VM: pages are locked up to here
	Size				fRequested;			// +0x34  VM: the size asked of the stack manager
	Size				fExtentUnits;		// +0x38  the heap grows and shrinks in these (a page)
	ULong				fIsVMBacked;		// +0x3c
	ULong				fMastersPerChunk;	// +0x40  master pointers per master block (0x40)
	SkiaMasterPointer*	fFreeMasters;		// +0x44
	SkiaBlock*			fRover;				// +0x48  where the free-list search starts
	SkiaMoveHookProc	fMoveHook;			// +0x4c  set: direct blocks may be moved (the hook is told)
	SkiaOutOfMemoryHookProc fOutOfMemoryHook;	// +0x50
	SkiaAfterAllocHookProc fAfterAllocHook;	// +0x54
	SkiaReleaseHookProc	fReleaseHook;		// +0x58
	ULong				fAllocFlags;		// +0x5c  kAllocFlag_*, during an allocation
	long				fSeed;				// +0x60  HeapSeed
	TULockingSemaphore*	fSemaphore;			// +0x64
	ULong				fDeltaSum;			// +0x68  sum of the blocks' fDelta
	ULong				fDeltaWordsSum;		// +0x6c  sum of fDelta & ~3
	ULong				fUnknown70[4];		// +0x70
	ULong				fUnknown80;			// +0x80  0
	ULong				fUnknown84[3];		// +0x84
	SkiaHeap*			fNextHeap;			// +0x90  the heap list (GetHeaps, FindHeap)
	SkiaHeap*			fChildHeap;			// +0x94  heaps within this heap's range
	SkiaHeap*			fMPHeap;			// +0x98  where master pointers go
	SkiaHeap*			fSPHeap;			// +0x9c
	ULong				fUnknownA0[5];		// +0xa0
	ULong				fFreeMasterCount;	// +0xb4
	ULong				fUnknownB8;			// +0xb8

	SkiaBlock*			HeaderBlock()		{ return (SkiaBlock*) fStart; }
	SkiaBlock*			Sentinel()			{ return (SkiaBlock*) fEnd - 1; }
};

const ULong kSkiaHeapMagic = 'skia';

// SkiaHeap::fAllocFlags
enum
{
	kAllocFlag_NoSlide		= 0x01,		// no compaction unless direct blocks may move
	kAllocFlag_Weak			= 0x02		// NewWeakBlock: no compaction at all
};

const ULong kHeapHeaderSize = kBlockHeaderSize + sizeof(SkiaHeap);	// 0xcc on the ARM: the header block
const ULong kFirstBlockOffset = kHeapHeaderSize;						// the first block (ROM 0xcc)
const ULong kHeapOverhead = kHeapHeaderSize + kBlockHeaderSize;		// header + sentinel (ROM 0xdc)
const Size kMinimumHeapSize = kHeapOverhead + 0x84;					// GetMinimumHeapSize: ROM 0x160
const ULong kMastersPerChunk = 0x40;


// The heap primitives (SkiaHeap.cpp); the current heap is the task's
// (GetCurrentHeap), as on the ARM.
Heap			NewHeap(void* area, Size maxSize, Size initialSize);
Boolean			IsSkiaHeap(Heap heap);
Boolean			ExtendVMHeap(SkiaHeap* heap, Size needed);
NewtonErr		ShrinkSkiaHeapLeaving(SkiaHeap* heap, Size amountLeftFree);
Boolean			HeapReleaseRequestHandler(SkiaHeap* heap, VAddr* outStart, VAddr* outEnd, Boolean shrink);
void			RelocateHeap(SkiaHeap* heap, char* newBase);
void			ResurrectSkiaHeap(SkiaHeap* heap);
Size			GetMinimumHeapSize(void);
Heap			GetCurrentHeap(void);
void			SetCurrentHeap(Heap heap);
Heap			GetFirstHeap(void);
void			SetFirstHeap(Heap heap);
SkiaHeap*		FindHeap(const void* addr);
SkiaBlock*		FindBlock(const void* addr);
int				GetHeaps(SkiaHeap* from, int skip, int count, SkiaHeap** outHeaps);

// heap fields
void			SetSkiaHeapSemaphore(Heap heap, TULockingSemaphore* semaphore);
TULockingSemaphore* GetSkiaHeapSemaphore(Heap heap);
void*			GetSkiaHeapRefcon(Heap heap);
void			SetSkiaHeapRefcon(void* refCon, Heap heap);
void			SetHeapIsVMBacked(Heap heap);
char*			GetHeapEnd(Heap heap);
char*			GetHeapStart(Heap heap);
Size			GetHeapSize(Heap heap);
Size			GetHeapExtent(Heap heap);
void			SetHeapExtentUnits(Heap heap, Size units);
Heap			GetFixedHeap(Heap heap);
void			SetFixedHeap(Heap heap, Heap fixedHeap);
Heap			GetMPHeap(Heap heap);
void			SetMPHeap(Heap heap, Heap mpHeap);
Heap			GetSPHeap(Heap heap);
void			SetSPHeap(Heap heap, Heap spHeap);
Heap			GetRelocHeap(Heap heap);
void			SetRelocHeap(Heap heap, Heap relocHeap);
SWiredHeapDescr* GetWiredHeap(Heap heap);
void			SetWiredHeap(Heap heap, SWiredHeapDescr* wired);

// blocks
void*			NewDirectBlock(Size size);
void*			NewWeakBlock(Size size);
void*			NewTemporaryBlock(Size size);
void			DisposeTemporaryBlock(void* data);
void			KillBlock(void* data);
void			DisposeDirectBlock(void* data);
void*			SetBlockSize(void* data, Size newSize);
void*			SetDirectBlockSize(void* data, Size newSize);
Size			GetDirectBlockSize(const void* data);
SkiaMasterPointer* NewIndirectBlock(Size size);
SkiaMasterPointer* NewFakeIndirectBlock(void* address, Size size);
Boolean			IsFakeIndirectBlock(const SkiaMasterPointer* master);
Size			GetFakeIndirectBlockSize(const SkiaMasterPointer* master);
void			DisposeIndirectBlock(SkiaMasterPointer* master);
void*			SetIndirectBlockSize(SkiaMasterPointer* master, Size newSize);
Size			GetIndirectBlockSize(const SkiaMasterPointer* master);
Size			GetBlockPhysicalSize(const void* data);
UByte			GetBlockType(const void* data);
void			SetBlockType(void* data, UByte type);
UByte			GetBlockFlags(const void* data);
void			SetBlockFlags(void* data, UByte flags);
UByte			GetBlockBusy(const void* data);
void			xSetBlockBusy(void* data, UByte busy);
void			IncrementBlockBusy(void* data);
void			DecrementBlockBusy(void* data);
void*			GetBlockParent(const void* data);
void			SetBlockParent(void* data, void* parent);
UByte			GetBlockDelta(const void* data);
void*			CompactHeap(SkiaHeap* heap, void* keepData);

// statistics
Size			TotalFreeInHeap(Heap heap);
Size			LargestFreeInHeap(Heap heap);
unsigned long	CountFreeBlocks(Heap heap);
Size			TotalUsedInHeap(Heap heap);
Size			MaxHeapSize(Heap heap);
Size			GetHeapReleaseable(Heap heap);

// the validation hooks are empty in this ROM
long			GetHeapValidation(void);
void			SetHeapValidation(void);
void			ValidateHeap(void);
void			ValidateDirectBlock(void);
void			ValidateIndirectBlock(void);
void			ValidateMasterPointer(void);
void			ValidateBlockRange(void);
void			UnscrambleMaster(void);

// byte utilities (NewtonMemory.h declares BlockMove & co.; MoveBits and
// FillBits, the bit blitters next to them in the ROM, belong to QuickDraw)
void			MoveBytes(const void* srcPtr, void* destPtr, Size byteCount);
void			CopyBytes(const void* srcPtr, void* destPtr, Size byteCount);

#endif	/* __SKIAHEAP_H */
