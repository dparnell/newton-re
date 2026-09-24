# The memory manager

Reverse-engineering notes for the Newton memory manager: the `NewtonMemory.h`
API (`NewPtr`, `NewHandle`, heaps, heap semaphores), the relocating "Skia"
heap beneath it, and the page-based safe heap used for the kernel heap and
for wired memory. Reconstructed in `src/memory/` with the host test
`src/memory/tests/test_SkiaHeap.cpp`. Everything below was established by
reading the ROM (decompiler plus disassembly where the decompiler stopped)
at the addresses given; the code cites each function.

## The map the machine starts from

`docs/memory/mmu-map.md` is the boot MMU map, decoded from
`g8MegContinuousTableStart` (ROM 0x100) by
`tools/newton-rom/analysis/mmumap.py`.  It is worth knowing about for a
reason that has nothing to do with the memory manager: **the ROM is
mapped more than once**, cached at 0x00100000 and *uncached* at
0x03500000, and ROM code picks the mapping it wants.

So when an address in ROM code looks out of range, it is usually a
second mapping rather than a mistake:

```
analysis/mmumap.py build/MP2x00US --where 0x038948f0
  0x038948f0 is in g8MegContinuousTableStart: virtual 0x03500000 + 0x3948f0
      -> physical 0x003948f0   (section, UNCACHED, domain 0, AP 2)
```

That one is the handwriting engine's classifier reading its ninety-one
kilobytes of trained weights through the uncached alias, so that
streaming them once does not flush the StrongARM's sixteen-kilobyte
data cache (`docs/recognition/bpnet.md`).  The *flags* on the entry are
usually what says why the alias was chosen.

## Where it is in the ROM

| Part | ROM range | Reconstruction |
|------|-----------|----------------|
| `NewtonMemory.h` API: `NewPtr` ... `SetHandleName`, heaps (`NewHeapAt`, `NewVMHeap`, `NewPersistentVMHeap`, `NewSegregatedVMHeap`, `DestroyVMHeap`, `ZapHeap`, `ResurrectVMHeap`, `ShrinkHeapLeaving`), heap walking (`HeapSeed`, `NextHeapBlock`, `CountHeapBlocks`, `CheckHeap`), the memory-manager breaks, `MemError`/`GetHeap`/`SetHeap` | 0x00143000-0x00145290 (plus `ClearMemory` 0x0037fc84, `ReportMemMgrTrap`/`ReportSmashedHeap` near 0x00120d90) | `MemoryManager.cpp` |
| The Skia heap primitives (`NewHeap`, block allocation, compaction, master pointers, VM growth, `RelocateHeap`, statistics, the byte utilities) | 0x002eb538-0x002ee100 | `SkiaHeap.h/.cpp` |
| The safe heap (`SSafeHeapPage`, `InitSafeHeap`, `SafeHeapAlloc/Realloc/Free`, `SWiredHeapDescr`, `SWiredHeapPage`) | 0x001c7a6c-0x001c84a0 | `SafeHeap.h/.cpp` |

None of these types are in the DDK (only the API is, in `NewtonMemory.h`),
so the structures are ours, laid out after the ROM with the ROM offsets
noted in the headers.

## The Skia heap

A heap is one contiguous area: a **header block** whose data is the
`SkiaHeap` record (0xbc bytes on the ARM, so the first client block is at
+0xcc), then blocks, then an **end sentinel** block at `fEnd - 0x10`. The
record is found from the header block's data, so `Heap` (the API's opaque
type) *is* the `SkiaHeap*`; `IsSkiaHeap` checks the `'skia'` magic at
+0x08.

### Blocks

Every block, free or allocated, has a 16-byte header:

| Offset | Field | Meaning |
|--------|-------|---------|
| +0 | flags | 0x80 allocated; 0x10 private (the heap's own: type 3 header, 4 sentinel, 5 master-pointer chunk); 0x08 temporary (`NewTemporaryBlock`); 0x04 the block before this one is free; 0x02 indirect (a Handle's block, parent = master pointer); 0x01 direct (a Ptr's block, parent = heap) |
| +1 | delta | physical size minus requested size (word rounding plus any slop not split off) |
| +2 | busy | lock count; a busy block never moves. Direct blocks are born busy (`NewPtr` increments it); 0xff marks a private block |
| +3 | type | the client's type byte (`SetPtrType`), or the private type |
| +4 | size | physical size including the header |
| +8 | parent | heap (direct) or master pointer (indirect) |
| +12 | owner | owning task id, or a name with the top bit set (`NewNamedPtr`) |

A free block overlays the same 16 bytes: the size in the first word (whose
top byte, being the flags of an allocated block, has bit 7 clear - a
big-endian trick), then the next and previous free block. The free list is
doubly linked in address order (`fFreeHead`/`fFreeTail`); the "previous is
free" flag lets a freed block merge with both neighbours in constant time.
Sizes are rounded to the word; a remainder no larger than a header (0x10)
is not split off but left in the block's `delta`.

Host representation: pointers are 64-bit, so the header is 32 bytes and a
free block has the same header as an allocated one (flags 0, size in the
size field) instead of the overlay; `kBlockAlign` is `sizeof(ULong)`. The
sentinel's "previous is free" flag is not kept up to date by the ROM
either, so the checker in the test ignores it.

### Allocation and compaction

`NewBlock` is first-fit from the rover (`fRover`) round the free list. When
nothing fits, `SearchFreeList` compacts: `FindSmallestSlide` looks for the
cheapest run of allocated blocks to slide (`SlideBlocksDown`/`SlideBlocksUp`
move the blocks and fix their master pointers, calling the heap's
`fMoveHook` for direct blocks when the heap allows them to move; `JumpBlock`
moves a single block over a free one; a block with a non-zero busy count
pins the run). If compaction cannot help, the heap's out-of-memory hook
(+0x50) is asked, then the heap grows (`ExtendVMHeap`, in `fExtentUnits`
up to `fMaxSize`, after the release hook (+0x58) has had a chance to make
room). `fAllocFlags` during an allocation: 1 = no sliding unless direct
blocks may move, 2 = weak (`NewWeakBlock`: no compaction at all).

`SetBlockSize` (behind `ReallocPtr`/`SetHandleSize`) tries in order: absorb
the change in the block's `delta`; grow in place into a following free
block (moving that block's header up) or shrink leaving a free block;
otherwise allocate anew and copy (`TrySetSize` reports which case applied:
1 in place, 3 shrink, 4 no room). Growing in place by less than a header
overlaps the old and new free-block headers, so the old fields must be read
before the new header is written - the reconstruction's first version got
this wrong and the stress test caught it.

### Handles

A Handle is a pointer to a **master pointer**: two words, the block's data
(or 0xc0000000 when the master is free) and the block's heap (or, on the
free master list, the next free master; for a fake handle, `(size << 2) |
1`). Masters are allocated 0x40 at a time in a private block of type 5
(`AllocateMoreMasters`) in the heap's `fMPHeap`; the free ones chain
through the second word. `HLock` increments the block's busy count.

### Heaps, VM heaps, segregated heaps

`NewHeap(area, maxSize, initialSize)` builds a heap of `initialSize` within
an area of `maxSize`; VM heaps (`NewVMHeap`) get their area from the stack
manager (`NewHeapArea`), lock pages up to `fLimit` as they grow
(`LockHeapRange`) and give them back when they shrink
(`ShrinkSkiaHeapLeaving`, `UnlockHeapRange`). A segregated heap
(`NewSegregatedVMHeap`) is two heaps: `fFixedHeap` takes the Ptrs,
`fRelocHeap` the Handles, so relocatable blocks are never pinned by fixed
ones. Heaps are on a list (`fNextHeap`, `GetHeaps`, `FindHeap`), with
`fChildHeap` for heaps that live inside another's range. `RelocateHeap`
rebases every pointer in a heap by a delta (persistent heaps found at a new
address after a reboot; `ResurrectVMHeap`).

The current heap and `MemError` live in the task globals (+0x44 and +0x48
from the end); `NewPtr` allocates in a safe heap when the current heap is
one (the kernel heap) and in a Skia heap otherwise. The ROM also tests
`gOSIsRunning` there, which on the MessagePad comes to the same thing; on
the host the kernel heap is a Skia heap (see below), so only the heap's
kind is tested.

## The safe heap

For memory that must stay put and paged in - the kernel heap and a VM
heap's wired sub-heap (`NewWiredPtr`) - the memory manager has a simpler
page-based allocator. A safe heap is a chain of 4 KB pages, each an
`SSafeHeapPage` (0x2c bytes: vtable, `fNext`, `fPrev`, `'safe'` magic,
`fLastPage`, `fFreeBlock`, `fFree`, `fPhysId`, `fPhys`, `fSemaphore`,
`fRefCon`), followed by blocks of a one-word header (size in the low 24
bits, top byte 0xff for free or the slop of an allocated block) up to the
page's last word, which points back at the page. A block finds its page by
rounding its address down; allocation starts at the newest page and takes a
new page when none has room; a page that empties is returned. Where pages
come from is virtual (`GetPage`/`FreePage`): the kernel heap's come from
the page manager, a wired heap's (`SWiredHeapPage`) from its own locked
range.

## Host stand-ins and gaps

* The page manager is not reconstructed, so `GetNewPageFromPageMgr`,
  `SSafeHeapPage::FreePage` and `NewWiredPtr`/`DisposeWiredPtr` are `NOT
  YET RECONSTRUCTED`. The host kernel heap
  (`memory/host/KernelHeap.cpp`) is a Skia heap over a plain allocation,
  made at the end of `InitMemObjDatabase` where the ROM's `VMemInit`
  would have built the safe heap; host tests that use the memory manager
  without booting call `InitHostStandaloneHeap()`.
* `DEVIATION` (`TotalSystemFree`, `SystemRAMSize`): the ROM asks the page
  manager how much RAM nobody has spoken for, and how much there is
  altogether. With no page manager, and one heap where the machine has a
  domain each, the host answers from that heap instead - the bytes in its
  free blocks plus the room it may still grow into, and the size of the
  area it was made over. Both of them used to answer **0** while the page
  manager was NOT YET, which told everything that asked that the machine
  was out of memory; `GetHeapStats` (below) is what showed it up.
* The stack manager on the host (`os600/user/host/StackManager.cpp`) hands
  out page-aligned areas from the host allocator; `LockHeapRange`/
  `UnlockHeapRange` are no-ops there.
* `HashCallChain` (the caller hash the breaks use) needs the ARM stack
  frame and is `NOT YET RECONSTRUCTED`; `VetHeap` (behind `CheckHeap`)
  likewise.
* `DEVIATION` (SkiaHeap.cpp, `RelocateHeap`): the ROM adds the delta to the
  free master list's terminating nil as well, leaving the list ending in a
  bad pointer; the reconstruction stops at nil.

## What a script sees (`system/SystemNatives.h`)

`GetHeapStats(options)` 0x00202ff4 is the ROM's own report on all of it,
and the way to see what the reconstruction is doing with memory:

    build/host/host/newton --rom build/MP2x00US/rom.bin --headless 3 \
        --script <a script calling GetHeapStats({garbageCollectFrames: true})>

It answers a frame of `ptrHeapStart`/`ptrHeapSize`/`ptrFreeSize`,
`handleHeapStart`/`handleHeapSize`/`handleFreeSize`,
`framesHeapStart`/`framesHeapSize`/`framesFreeSize` and
`systemFreeSize`. The first two triples come from `GetActualHeapInfo`
0x00202f08, which walks a heap a block at a time (`HeapSeed`/
`NextHeapBlock`) and counts what it finds; a walk is only good while the
heap holds still, so when `NextHeapBlock` reports that the seed has
changed under it the whole walk starts again from nothing. The frames
triple is `HeapBounds` and `TObjectHeap::Statistics`, with a collection
first when `options.garbageCollectFrames` asks for one.

The starts are addresses with their low two bits masked off, which makes
them integer Refs of the address divided by four - a number to print
rather than a pointer to use.

`options.includeSystemReleasable` is `NOT YET RECONSTRUCTED`
(`GetSystemReleasable` 0x0014312c, `ROMDomainManagerFreePageCount`
0x0027f594), so the system's free space is what it has this moment rather
than what it could release.
