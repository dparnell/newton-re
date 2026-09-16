/*
	File:		memory/SafeHeap.h

	Contains:	The safe heap: the memory manager's page-based allocator for
				memory that must stay put and paged in - the kernel heap
				(gKernelHeap, before and beneath the VM heaps; InitSafeHeap in
				VMemInit) and a VM heap's wired sub-heap (NewWiredPtr).  Not in
				the DDK; these declarations follow the ROM (0x001c7a6c-
				0x001c84a0).

	A safe heap is a chain of 4 KB pages, each an SSafeHeapPage: a header,
	blocks of a one-word header (the size in the low 24 bits, the top byte
	0xff for a free block or the block's slop otherwise) to the last word of
	the page, which points back at the page (SafeHeapEndSentinelFor).  A
	block is found by rounding its address down to its page.  Every page
	knows the newest page (fLastPage, where allocation starts); a page that
	empties is given back.  Where pages come from is virtual: the kernel
	heap's from the page manager (GetNewPageFromPageMgr - NOT YET
	RECONSTRUCTED), a wired heap's (SWiredHeapPage) from its own locked area.

	Host representation: pointers are host-sized, so the page header is
	bigger than the ROM's 0x2c bytes and the sentinel is a pointer wide; the
	constants derive from the structures.  The kernel heap on the host is a
	Skia heap (memory/host/KernelHeap.cpp) until the page manager exists.
*/

#ifndef __SAFEHEAP_H
#define __SAFEHEAP_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

class TULockingSemaphore;
class TPhys;

const ULong kSafeHeapMagic = 'safe';
const ULong kSafePageSize = 0x1000;

typedef unsigned int	ULong32;			// the ROM's word, whatever ULong is here


// A block's header word: size (with the header) in the low 24 bits, the top
// byte the slop (size - request) of an allocated block, 0xff when free.
struct SSafeHeapBlock
{
	ULong32			fWord;

	ULong			Size() const			{ return fWord & 0xffffff; }
	Boolean			IsFree() const			{ return (fWord >> 24) == 0xff; }
	SSafeHeapBlock*	Next();
	void*			Data()					{ return this + 1; }
	static SSafeHeapBlock*	Of(const void* data)	{ return (SSafeHeapBlock*) data - 1; }
};


// A page of a safe heap.  (ROM 0x2c bytes: vtable +0, fNext +4, fPrev +8,
// fMagic +0xc, fLastPage +0x10, fFreeBlock +0x14, fFree +0x18, fPhysId
// +0x1c, fPhys +0x20, fSemaphore +0x24, fRefCon +0x28; blocks from +0x2c;
// the sentinel at +0xffc.)
class SSafeHeapPage
{
public:
	virtual SSafeHeapPage*	GetPage(void);		// a fresh page for the heap (chained after this one)
	virtual void			FreePage(void);		// this page, empty, goes back

	void			Init(ULong physId, TPhys* phys, SSafeHeapPage* heap);
	void*			Alloc(long physicalSize, long requestedSize);
	void			Free(void* data);
	SSafeHeapPage*	FirstPage(void);
	SSafeHeapBlock*	FirstBlock()			{ return (SSafeHeapBlock*) (this + 1); }

	SSafeHeapPage*	fNext;				// the page after this one (newer)
	SSafeHeapPage*	fPrev;				// the page before (older); nil in the first
	ULong			fMagic;				// kSafeHeapMagic
	SSafeHeapPage*	fLastPage;			// the newest page: allocation starts there (kept in every page)
	SSafeHeapBlock*	fFreeBlock;			// the free block to try first (nil: search)
	ULong			fFree;				// bytes free in this page
	ULong			fPhysId;			// the page's physical page object (kernel heap)
	TPhys*			fPhys;
	TULockingSemaphore*	fSemaphore;		// the heap's (in every page)
	void*			fRefCon;			// a wired heap's SWiredHeapDescr
};

const ULong kSafePageHeaderSize = sizeof(SSafeHeapPage);				// ROM 0x2c
const ULong kSafeSentinelOffset = kSafePageSize - sizeof(void*);		// ROM 0xffc
const ULong kSafePageCapacity = kSafeSentinelOffset - kSafePageHeaderSize;	// ROM 0xfd0: the bytes for blocks
const ULong kSafeBlockHeaderSize = sizeof(SSafeHeapBlock);			// 4
const ULong kSafeBlockSlop = 0xc;										// a remainder smaller than this is not split off


// The wired heap: a VM heap's sub-heap of pages locked in memory, grown a
// page at a time within an area of the stack manager's.  (ROM 0x10 bytes.)
struct SWiredHeapDescr
{
	VAddr			fStart;				// the area, page aligned
	VAddr			fEnd;
	SSafeHeapPage*	fPage;				// its safe heap
	ULong			fSize;				// bytes locked so far

	long			GrowByOnePage(void);
	void			ShrinkByOnePage(void);
};

class SWiredHeapPage : public SSafeHeapPage
{
public:
	static SSafeHeapPage*	New(SWiredHeapDescr* descr);
	long			Destroy(void);
	virtual SSafeHeapPage*	GetPage(void);
	virtual void			FreePage(void);
};


// the safe heap over its pages
long			InitSafeHeap(SSafeHeapPage** outHeap);
void			AddPartialPageToSafeHeap(void* area, SSafeHeapPage* heap);
Boolean			IsSafeHeap(const void* heap);
Boolean			SafeHeapIsEmpty(SSafeHeapPage* heap);
void*			SafeHeapAlloc(long size, SSafeHeapPage* heap);
void*			SafeHeapRealloc(void* data, long size);
void			SafeHeapFree(void* data);
long			SafeHeapBlockSize(const void* data);
void**			SafeHeapEndSentinelFor(const void* addr);

// the page source (NOT YET RECONSTRUCTED: the page manager)
long			GetNewPageFromPageMgr(void** outPage, ULong* outPhysId, TPhys** outPhys);

#endif	/* __SAFEHEAP_H */
