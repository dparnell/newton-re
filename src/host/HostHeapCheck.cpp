/*
	File:		host/HostHeapCheck.cpp

	Contains:	The host's heap walker (NEWTON_HEAPCHECK).  See
				HostHeapCheck.h.
*/

#include "HostHeapCheck.h"
#include "SkiaHeap.h"
#include "NewtonMemory.h"
#include <stdio.h>
#include <stdlib.h>

// the C stack, without <windows.h> (whose names clash with the Newton's)
#ifdef _WIN32
extern "C" {
__declspec(dllimport) unsigned short __stdcall RtlCaptureStackBackTrace(unsigned long skip, unsigned long count, void** trace, unsigned long* hash);
__declspec(dllimport) void* __stdcall GetModuleHandleA(const char* name);
}
#endif

static SkiaHeap*	gCheckedHeap = nil;
static long			gEvery = 1;
static long			gCount = 0;
static Boolean		gInCheck = false;


static void
HeapDamaged(SkiaHeap* heap, const char* what, SkiaBlock* b, const char* where)
{
	fprintf(stderr, "[heapcheck] heap %p damaged%s%s after %ld allocations: %s at block %p (flags %02x size %lx)\n",
			(void*) heap, where ? " in " : "", where ? where : "", gCount, what,
			(void*) b, b ? (unsigned) b->fFlags : 0u, b ? (unsigned long) b->fSize : 0ul);
	fprintf(stderr, "[heapcheck]   the heap runs %p-%p; free list head %p tail %p; the block's links: next %p prev %p\n",
			(void*) heap->fStart, (void*) heap->fEnd, (void*) heap->fFreeHead, (void*) heap->fFreeTail,
			b ? (void*) b->fNext : nil, b ? (void*) b->fPrev : nil);
#ifdef _WIN32
	void* trace[48];
	unsigned short n = RtlCaptureStackBackTrace(1, 48, trace, nil);
	char* base = (char*) GetModuleHandleA(nil);
	fprintf(stderr, "[heapcheck] the C stack, as image offsets (tools/host/whichfunction.py):\n");
	for (unsigned short i = 0; i < n; i++)
		fprintf(stderr, "[heapcheck]   %#lx\n", (unsigned long) ((char*) trace[i] - base));
#endif
	fflush(stderr);
	abort();
}


static void
WalkHeap(SkiaHeap* heap, const char* where, Boolean justAllocated)
{
	SkiaBlock* b = heap->HeaderBlock();
	SkiaBlock* end = heap->Sentinel();
	if (getenv("NEWTON_HEAPDUMP"))
		for (SkiaBlock* f = heap->fFreeHead; f != nil; f = f->fNext)
			fprintf(stderr, "  free %p flags %02x size %lx next %p prev %p\n", (void*) f, f->fFlags, (unsigned long) f->fSize, (void*) f->fNext, (void*) f->fPrev);
	SkiaBlock* expectFree = heap->fFreeHead;
	SkiaBlock* prevFree = nil;
	while (b < end)
	{
		if (getenv("NEWTON_HEAPDUMP"))
			fprintf(stderr, "  block %p flags %02x busy %02x type %02x size %lx parent %p owner %lx\n", (void*) b, b->fFlags, b->fBusy, b->fType, (unsigned long) b->fSize, b->fParent, (unsigned long) b->fOwner);
		if (b->fSize < kBlockHeaderSize || (b->fSize & (kBlockAlign - 1)) != 0 || (char*) b + b->fSize > heap->fEnd)
			HeapDamaged(heap, "a bad block size", b, where);
		if (b->IsFree() && b != expectFree && justAllocated && b->fFlags == 0 && b->fBusy == 0)
			justAllocated = false;		// the block being allocated: the hook runs before its flags are set
		else if (b->IsFree())
		{
			if (b != expectFree)
				HeapDamaged(heap, "a free block the free list skips (or the list names another)", b, where);
			if (b->fPrev != prevFree)
				HeapDamaged(heap, "a free block whose back link is wrong", b, where);
			prevFree = b;
			expectFree = b->fNext;
		}
		else if ((b->fFlags & kBlockFlag_Private) == 0)
		{
			if ((b->fFlags & kBlockFlag_Direct) != 0 && b->fParent != (void*) heap)
				HeapDamaged(heap, "a direct block whose parent is not the heap", b, where);
			if ((b->fFlags & kBlockFlag_Indirect) != 0
			&&  (b->fParent == nil || ((SkiaMasterPointer*) b->fParent)->fBlock != b->Data()))
				HeapDamaged(heap, "a handle's block whose master pointer does not point at it", b, where);
		}
		b = b->Following();
	}
	if (b != end)
		HeapDamaged(heap, "the blocks overrun the sentinel", b, where);
	if (expectFree != nil)
		HeapDamaged(heap, "the free list names a block that is not a free block", expectFree, where);
	if (heap->fFreeTail != prevFree)
		HeapDamaged(heap, "the free list's tail is wrong", heap->fFreeTail, where);
}


static void
AfterAlloc(void)
{
	if (gInCheck || gCheckedHeap == nil)
		return;
	if (++gCount % gEvery != 0)
		return;
	gInCheck = true;
	WalkHeap(gCheckedHeap, nil, true);
	gInCheck = false;
}


static void
BeforeDispose(void* p)
{
	if (p == nil || gCheckedHeap == nil)
		return;
	SkiaBlock* block = SkiaBlock::Of(p);
	if ((char*) block >= gCheckedHeap->fStart && (char*) block < gCheckedHeap->fEnd)
	{
		if (block->IsFree())
			HeapDamaged(gCheckedHeap, "a pointer given back twice", block, "DisposPtr");
		gInCheck = true;
		WalkHeap(gCheckedHeap, "DisposPtr", false);
		gInCheck = false;
	}
}


void
HostHeapCheckInstall(void)
{
	const char* setting = getenv("NEWTON_HEAPCHECK");
	if (setting == nil)
		return;
	gEvery = atol(setting);
	if (gEvery < 1)
		gEvery = 1;
	gCheckedHeap = (SkiaHeap*) GetCurrentHeap();
	gCheckedHeap->fAfterAllocHook = AfterAlloc;
	gHostDisposeCheck = BeforeDispose;
	fprintf(stderr, "[heapcheck] heap %p walked every %ld allocations\n", (void*) gCheckedHeap, gEvery);
	WalkHeap(gCheckedHeap, "install", false);
}


void
HostHeapCheckNow(const char* where)
{
	if (gCheckedHeap != nil && !gInCheck)
		WalkHeap(gCheckedHeap, where, false);
}
