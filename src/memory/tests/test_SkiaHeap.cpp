// Host unit test for the memory manager (src/memory): the Skia heap through
// NewtonMemory.h's API - pointers, handles, resizing, coalescing, compaction
// (handles slide, locked handles do not), heap growth and shrinking, the
// statistics and the heap walk - plus a randomised stress run checked
// against a shadow of every block's contents and a heap consistency check.
// The heaps are host allocations; no task runs, so the kernel heap is the
// current heap and MemError is not kept (it lives in the task globals).

#include "SkiaHeap.h"
#include "memory/host/KernelHeap.h"
#include "Boot.h"
#include "KernelGlobals.h"
#include "os600/TaskGlobals.h"
#include "NewtonMemory.h"
#include "OSErrors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


// Walks the heap: every block accounted for, the free list in address order
// and matching the free blocks met, the "previous is free" flags right,
// the free byte count right, the sentinel where the end is.
static Boolean
Consistent(SkiaHeap* heap, const char* what)
{
	Boolean ok = true;
	SkiaBlock* b = (SkiaBlock*) heap->fStart;
	SkiaBlock* prevFree = nil;
	Boolean prevWasFree = false;
	Size freeBytes = 0;
	int freeCount = 0;
	SkiaBlock* f = heap->fFreeHead;
	while ((char*) b < heap->fEnd)
	{
		if (b->fSize < kBlockHeaderSize || (char*) b + b->fSize > heap->fEnd)
		{
			printf("  %s: block %p has size %lu\n", what, (void*) b, (unsigned long) b->fSize);
			return false;
		}
		// (the sentinel's flag is not kept up: ExtendVMHeap moves it without
		// looking, and nothing reads it - the ROM's way)
		Boolean flagged = (b->fFlags & kBlockFlag_PrevIsFree) != 0;
		if (!b->IsFree() && flagged != prevWasFree && b != heap->Sentinel())
		{
			printf("  %s: block %p prev-is-free flag %d, previous block free %d\n", what, (void*) b, flagged, prevWasFree);
			ok = false;
		}
		if (b->IsFree())
		{
			if (prevWasFree)
			{
				printf("  %s: two free blocks in a row at %p\n", what, (void*) b);
				ok = false;
			}
			if (f != b)
			{
				printf("  %s: free block %p is not next on the free list (%p)\n", what, (void*) b, (void*) f);
				return false;
			}
			if (b->fPrev != prevFree)
			{
				printf("  %s: free block %p has prev %p, expected %p\n", what, (void*) b, (void*) b->fPrev, (void*) prevFree);
				ok = false;
			}
			prevFree = b;
			f = b->fNext;
			freeBytes += b->fSize;
			freeCount++;
		}
		prevWasFree = b->IsFree();
		b = b->Following();
	}
	if ((char*) b != heap->fEnd)
	{
		printf("  %s: the blocks do not end at the heap's end\n", what);
		ok = false;
	}
	SkiaBlock* sentinel = heap->Sentinel();
	if (sentinel->fType != kBlockType_EndSentinel || sentinel->fSize != kBlockHeaderSize)
	{
		printf("  %s: no sentinel at the end\n", what);
		ok = false;
	}
	if (f != nil || heap->fFreeTail != prevFree)
	{
		printf("  %s: the free list has blocks beyond the heap (tail %p, last met %p)\n", what, (void*) heap->fFreeTail, (void*) prevFree);
		ok = false;
	}
	if (freeBytes != heap->fFree)
	{
		printf("  %s: %ld free bytes counted, heap says %ld\n", what, (long) freeBytes, (long) heap->fFree);
		ok = false;
	}
	if ((int) CountFreeBlocks(heap) != freeCount)
	{
		printf("  %s: %d free blocks counted, heap says %lu\n", what, freeCount, CountFreeBlocks(heap));
		ok = false;
	}
	if (heap->fRover != nil && !heap->fRover->IsFree())
	{
		printf("  %s: the rover %p is not a free block\n", what, (void*) heap->fRover);
		ok = false;
	}
	return ok;
}


static void
Dump(SkiaHeap* heap, const char* what)
{
	printf("  heap %s: start %p end %p free %ld rover %p head %p tail %p\n", what, (void*) heap->fStart, (void*) heap->fEnd, (long) heap->fFree, (void*) heap->fRover, (void*) heap->fFreeHead, (void*) heap->fFreeTail);
	for (SkiaBlock* b = (SkiaBlock*) heap->fStart; (char*) b < heap->fEnd; b = b->Following())
		printf("    %p size %6lu flags %02x busy %3d delta %d%s\n", (void*) b, (unsigned long) b->fSize, b->fFlags, b->fBusy, b->fDelta, b->IsFree() ? "  FREE" : "");
}

static void
Fill(void* p, Size size, unsigned seed)
{
	unsigned char* b = (unsigned char*) p;
	for (Size i = 0; i < size; i++)
		b[i] = (unsigned char) (seed + i * 7);
}

static Boolean
Check(const void* p, Size size, unsigned seed)
{
	const unsigned char* b = (const unsigned char*) p;
	for (Size i = 0; i < size; i++)
		if (b[i] != (unsigned char) (seed + i * 7))
			return false;
	return true;
}


static void TestPointers()
{
	SkiaHeap* heap = (SkiaHeap*) gKernelHeap;
	Size freeBefore = TotalFreeInHeap(nil);
	Size usedBefore = TotalUsedInHeap(nil);
	Ptr a = NewPtr(100);
	Ptr b = NewPtr(200);
	Ptr c = NewPtr(300);
	EXPECT(a != nil && b != nil && c != nil);
	EXPECT(GetPtrSize(a) == 100 && GetPtrSize(b) == 200 && GetPtrSize(c) == 300);
	EXPECT(PtrToHeap(a) == gKernelHeap);
	EXPECT(TotalFreeInHeap(nil) < freeBefore);
	EXPECT(Consistent(heap, "three pointers"));
	Fill(a, 100, 1);
	Fill(b, 200, 2);
	Fill(c, 300, 3);

	// a pointer is busy from birth: it does not move
	EXPECT(GetBlockBusy(a) == 1);
	SetPtrName(b, 'name');
	EXPECT(GetPtrName(b) == 'name' && GetPtrOwner(b) == 0);
	SetPtrType(b, 7);
	EXPECT(GetPtrType(b) == 7);

	// freeing the middle one leaves a hole; the neighbours are intact
	DisposPtr(b);
	EXPECT(Consistent(heap, "hole"));
	EXPECT(Check(a, 100, 1) && Check(c, 300, 3));
	// the hole is reused
	Ptr d = NewPtr(150);
	EXPECT(d != nil && d > a && d < c);
	EXPECT(Consistent(heap, "hole reused"));

	// realloc: shrink in place, grow in place (into the rest of the hole), grow by moving
	Ptr d2 = ReallocPtr(d, 50);
	EXPECT(d2 == d && GetPtrSize(d) == 50);
	EXPECT(Consistent(heap, "shrunk"));
	Fill(d, 50, 4);
	Ptr d3 = ReallocPtr(d, 190);
	EXPECT(d3 == d && GetPtrSize(d) == 190 && Check(d, 50, 4));
	EXPECT(Consistent(heap, "grown in place"));
	Ptr d4 = ReallocPtr(d, 5000);
	EXPECT(d4 != nil && d4 != d && GetPtrSize(d4) == 5000 && Check(d4, 50, 4));
	EXPECT(Consistent(heap, "grown by moving"));
	DisposPtr(d4);
	DisposPtr(a);
	DisposPtr(c);
	EXPECT(Consistent(heap, "all freed"));
	EXPECT(TotalUsedInHeap(nil) == usedBefore);		// (the heap may have grown and shrunk meanwhile)
	EXPECT(CountFreeBlocks(nil) == 1);

	Ptr z = NewPtrClear(64);
	Boolean clear = true;
	for (int i = 0; i < 64; i++)
		if (z[i] != 0)
			clear = false;
	EXPECT(clear);
	DisposPtr(z);
	DisposPtr(nil);					// harmless
	EXPECT(ReallocPtr(nil, 10) != nil);
}


static void TestHandles()
{
	SkiaHeap* heap = (SkiaHeap*) gKernelHeap;
	Size freeBefore = TotalFreeInHeap(nil);
	Handle h1 = NewHandle(1000);
	Handle h2 = NewHandle(2000);
	Handle h3 = NewHandle(3000);
	EXPECT(h1 != nil && h2 != nil && h3 != nil);
	EXPECT(GetHandleSize(h1) == 1000 && GetHandleSize(h2) == 2000 && GetHandleSize(h3) == 3000);
	EXPECT(HandleToHeap(h1) == gKernelHeap);
	EXPECT(!IsFakeHandle(h1));
	EXPECT(Consistent(heap, "three handles"));
	Fill(*h1, 1000, 11);
	Fill(*h2, 2000, 12);
	Fill(*h3, 3000, 13);

	// a handle's block moves when the heap compacts: free the middle one and
	// ask for more than any single free block holds
	void* h3Before = *h3;
	DisposHandle(h2);
	EXPECT(Consistent(heap, "handle hole"));
	Size largest = LargestFreeInHeap(nil);
	Handle h4 = NewHandle(largest + 500);
	EXPECT(h4 != nil && GetHandleSize(h4) == largest + 500);
	EXPECT(*h3 != h3Before);						// slid down into the hole
	EXPECT(Check(*h1, 1000, 11) && Check(*h3, 3000, 13));
	EXPECT(Consistent(heap, "compacted"));

	// a locked handle stays put
	HLock(h3);
	EXPECT(HGetState(h3) == 1);
	void* h3Locked = *h3;
	Handle h5 = NewHandle(100);
	EXPECT(Consistent(heap, "h5"));
	DisposHandle(h1);								// a hole below the locked h3
	EXPECT(Consistent(heap, "h1 gone"));
	Handle h6 = NewHandle(LargestFreeInHeap(nil) + 100);
	EXPECT(*h3 == h3Locked);
	EXPECT(h6 == nil || Consistent(heap, "locked"));
	HUnlock(h3);
	EXPECT(HGetState(h3) == 0);
	EXPECT(Check(*h3, 3000, 13));

	// resizing
	Fill(*h5, 100, 15);
	EXPECT(SetHandleSize(h5, 50) == noErr && GetHandleSize(h5) == 50 && Check(*h5, 50, 15));
	EXPECT(SetHandleSize(h5, 4000) == noErr && GetHandleSize(h5) == 4000 && Check(*h5, 50, 15));
	EXPECT(Consistent(heap, "handle resized"));

	// copies
	Handle copy = CopyHandle(h5);
	EXPECT(copy != nil && GetHandleSize(copy) == 4000 && Check(*copy, 50, 15));
	Handle again = h5;
	EXPECT(HandToHand(&again) == noErr && again != h5 && Check(*again, 50, 15));

	// a fake handle
	char outside[32];
	Handle fake = NewFakeHandle(outside, sizeof(outside));
	EXPECT(fake != nil && IsFakeHandle(fake) && *fake == outside && GetHandleSize(fake) == sizeof(outside));
	EXPECT(SetHandleSize(fake, 10) == -1);
	DisposHandle(fake);

	DisposHandle(h3);
	DisposHandle(h4);
	DisposHandle(h5);
	if (h6 != nil)
		DisposHandle(h6);
	DisposHandle(copy);
	DisposHandle(again);
	EXPECT(Consistent(heap, "handles freed"));
	// the master pointers' block stays
	EXPECT(TotalFreeInHeap(nil) == freeBefore - (kMastersPerChunk * sizeof(SkiaMasterPointer) + kBlockHeaderSize));
	EXPECT(heap->fFreeMasterCount == kMastersPerChunk);
}


static void TestGrowthAndWalk()
{
	// a small heap of its own, allowed to grow to 64 KB in 4 KB units
	const Size kArea = 64 * 1024;
	void* area = malloc(kArea);
	SkiaHeap* heap = (SkiaHeap*) NewHeap(area, kArea, 0x1000);
	EXPECT(heap != nil && heap->fExtent == 0x1000 && heap->fMaxSize == kArea && IsSkiaHeap(heap));
	EXPECT(Consistent(heap, "new heap"));
	EXPECT(MaxHeapSize(heap) == kArea && TotalUsedInHeap(heap) == (Size) kHeapOverhead);
	Heap saved = GetHeap();
	SetHeap(heap);
	Ptr big = NewPtr(10000);						// more than the first page: the heap grows
	EXPECT(big != nil && heap->fExtent >= 10000 + (Size) kHeapOverhead && heap->fExtent % 0x1000 == 0);
	EXPECT(Consistent(heap, "grown"));
	Ptr tooBig = NewPtr(100000);					// beyond the area
	EXPECT(tooBig == nil);
	EXPECT(Consistent(heap, "refused"));

	// the walk
	long seed = HeapSeed(heap);
	void* block = nil;
	int types[16];
	int n = 0;
	int type;
	while ((type = NextHeapBlock(heap, seed, block, &block, nil, nil, nil, nil, nil)) != kMM_HeapEndBlock && n < 16)
		types[n++] = type;
	EXPECT(n == 2 && types[0] == kMM_HeapPtrBlock && types[1] == kMM_HeapFreeBlock);
	// ROM bug fixed: asked for the block after the end, the sentinel again
	// (the ROM reads the sentinel's "data" as a header)
	void* after = nil;
	EXPECT(NextHeapBlock(heap, seed, block, &after, nil, nil, nil, nil, nil) == kMM_HeapEndBlock && after == block);
	Size total;
	ULong count;
	CountHeapBlocks(&total, &count, heap, kMM_HeapPtrBlock, 0, 0);
	EXPECT(count == 1 && total == GetBlockPhysicalSize(big));

	// freeing shrinks the heap back (ShrinkHeapLeaving keeps a unit's worth)
	DisposPtr(big);
	EXPECT(Consistent(heap, "shrunk"));
	EXPECT(heap->fExtent <= 0x2000);
	SetHeap(saved);
	free(area);
}


static void TestStress()
{
	const Size kArea = 256 * 1024;
	void* area = malloc(kArea);
	SkiaHeap* heap = (SkiaHeap*) NewHeap(area, kArea, 0x1000);
	Heap saved = GetHeap();
	SetHeap(heap);
	enum { kSlots = 64 };
	struct Slot { Ptr p; Handle h; Size size; unsigned seed; Boolean locked; } slots[kSlots];
	memset(slots, 0, sizeof(slots));
	srand(1234);
	int ops = 0, moved = 0;
	for (int round = 0; round < 4000; round++)
	{
		Slot& s = slots[rand() % kSlots];
		int op = rand() % 10;
		if (s.p == nil && s.h == nil)
		{
			Size size = (rand() % 3 == 0) ? rand() % 3000 : rand() % 200;
			s.seed = (unsigned) rand();
			s.size = size;
			if (op < 5)
			{
				s.p = NewPtr(size);
				if (s.p != nil)
					Fill(s.p, size, s.seed);
			}
			else
			{
				s.h = NewHandle(size);
				if (s.h != nil)
					Fill(*s.h, size, s.seed);
			}
		}
		else if (s.p != nil)
		{
			if (!Check(s.p, s.size, s.seed))
			{
				printf("  stress: pointer contents wrong at op %d\n", ops);
				failures++;
				break;
			}
			if (op < 6)
			{
				DisposPtr(s.p);
				s.p = nil;
			}
			else
			{
				Size size = rand() % 2500;
				Ptr q = ReallocPtr(s.p, size);
				if (q != nil)
				{
					Size keep = size < s.size ? size : s.size;
					if (!Check(q, keep, s.seed))
					{
						printf("  stress: realloc lost contents at op %d\n", ops);
						failures++;
						break;
					}
					s.p = q;
					s.size = size;
					Fill(s.p, size, s.seed);
				}
			}
		}
		else
		{
			void* was = *s.h;
			if (!Check(*s.h, s.size, s.seed))
			{
				printf("  stress: handle contents wrong at op %d\n", ops);
				failures++;
				break;
			}
			if (op < 4)
			{
				DisposHandle(s.h);
				s.h = nil;
			}
			else if (op < 7)
			{
				Size size = rand() % 2500;
				if (SetHandleSize(s.h, size) == noErr)
				{
					Size keep = size < s.size ? size : s.size;
					if (!Check(*s.h, keep, s.seed))
					{
						printf("  stress: handle resize lost contents at op %d\n", ops);
						failures++;
						break;
					}
					s.size = size;
					Fill(*s.h, size, s.seed);
				}
			}
			else if (op == 7)
			{
				if (s.locked)
					HUnlock(s.h);
				else
					HLock(s.h);
				s.locked = !s.locked;
			}
			if (s.h != nil && *s.h != was)
				moved++;
		}
		ops++;
		if (!Consistent(heap, "stress"))
		{
			printf("  (at op %d: slot %d op %d, p %p h %p size %ld)\n", ops, (int) (&s - slots), op, (void*) s.p, (void*) s.h, (long) s.size);
			Dump(heap, "after the failing operation");
			failures++;
			break;
		}
	}
	// every locked handle stayed where it was is implied by the contents; check the end state
	for (int i = 0; i < kSlots; i++)
	{
		if (slots[i].p != nil)
		{
			EXPECT(Check(slots[i].p, slots[i].size, slots[i].seed));
			DisposPtr(slots[i].p);
		}
		if (slots[i].h != nil)
		{
			EXPECT(Check(*slots[i].h, slots[i].size, slots[i].seed));
			if (slots[i].locked)
				HUnlock(slots[i].h);
			DisposHandle(slots[i].h);
		}
	}
	EXPECT(Consistent(heap, "stress end"));
	EXPECT(TotalUsedInHeap(heap) == (Size) (kHeapOverhead + heap->fFreeMasterCount / kMastersPerChunk * (kMastersPerChunk * sizeof(SkiaMasterPointer) + kBlockHeaderSize)));
	printf("  stress: %d operations, handles moved %d times\n", ops, moved);
	SetHeap(saved);
	free(area);
}


int main()
{
	InitHostStandaloneHeap();
	EXPECT(gKernelHeap != nil && IsSkiaHeap(gKernelHeap) && GetHeap() == gKernelHeap);
	EXPECT(Consistent((SkiaHeap*) gKernelHeap, "kernel heap"));
	EXPECT(GetMinimumHeapSize() == kMinimumHeapSize);
	TestPointers();
	TestHandles();
	TestGrowthAndWalk();
	TestStress();
	if (failures == 0)
		printf("test_SkiaHeap: all passed\n");
	return failures != 0;
}
