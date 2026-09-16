/*
	File:		memory/SafeHeap.cpp

	Contains:	The safe heap (SafeHeap.h): pages, blocks, the wired heap's
				pages.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include <new>				// before the DDK headers

#include "SafeHeap.h"
#include "NewtonMemory.h"
#include "VirtualMemory.h"
#include "Boot.h"
#include "UserSemaphore.h"
#include "OSErrors.h"

static inline ULong
PageOffset(const void* p)
{
	return (ULong) ((uintptr_t) p & (kSafePageSize - 1));
}

// the smallest partial page worth having: the header, a block header and a
// block of the slop size (ROM 0x3c)
const ULong kSafeMinimumPartialPage = kSafePageHeaderSize + kSafeBlockHeaderSize + kSafeBlockSlop;


/* -------------------------------------------------------------------------------
	Blocks
------------------------------------------------------------------------------- */

// ROM 0x001c81ec Next__14SSafeHeapBlockFv
// The block after this one in its page; nil at the sentinel.
SSafeHeapBlock*
SSafeHeapBlock::Next()
{
	SSafeHeapBlock* next = (SSafeHeapBlock*) ((char*) this + Size());
	if (PageOffset(next) >= kSafeSentinelOffset)
		return nil;
	return next;
}


// ROM 0x001c7b6c SafeHeapBlockSize__FPv
// The size the client asked for.
long
SafeHeapBlockSize(const void* data)
{
	if (data == nil)
		return 0;
	const SSafeHeapBlock* b = SSafeHeapBlock::Of(data);
	return b->Size() - (b->fWord >> 24);
}


// ROM 0x001c82d4 SafeHeapEndSentinelFor__FPv
// The last word of the address's page: it holds the page.
void**
SafeHeapEndSentinelFor(const void* addr)
{
	return (void**) (((uintptr_t) addr & ~(uintptr_t) (kSafePageSize - 1)) + kSafeSentinelOffset);
}


/* -------------------------------------------------------------------------------
	SSafeHeapPage
------------------------------------------------------------------------------- */

// ROM 0x001c7ba0 Init__13SSafeHeapPageFUlP5TPhysP13SSafeHeapPage
// Lays the page out (from its address, which need not be a page's start,
// to the sentinel) as one free block, and chains it after the heap's last
// page - every page of the heap then names it as the last.
void
SSafeHeapPage::Init(ULong physId, TPhys* phys, SSafeHeapPage* heap)
{
	if (kSafePageSize - PageOffset(this) < kSafeMinimumPartialPage)
		return;
	fNext = nil;
	fPrev = heap != nil ? heap->fLastPage : nil;
	fMagic = kSafeHeapMagic;
	fLastPage = this;
	fFree = kSafePageCapacity - PageOffset(this);
	SSafeHeapBlock* first = FirstBlock();
	fSemaphore = nil;
	fPhysId = physId;
	fPhys = phys;
	fFreeBlock = first;
	fRefCon = heap != nil ? heap->fRefCon : nil;
	first->fWord = (fFree & 0xffffff) | 0xff000000;
	*SafeHeapEndSentinelFor(this) = this;
	if (heap != nil)
	{
		heap->fLastPage->fNext = this;
		for (SSafeHeapPage* p = heap; p != nil; p = p->fNext)
			p->fLastPage = this;
	}
}


// ROM 0x001c7b88 FirstPage__13SSafeHeapPageFv
SSafeHeapPage*
SSafeHeapPage::FirstPage()
{
	SSafeHeapPage* p = this;
	while (p->fPrev != nil)
		p = p->fPrev;
	return p;
}


// ROM 0x001c7c70 Alloc__13SSafeHeapPageFlT1
// A block of physicalSize bytes (header and rounding included) from this
// page: the remembered free block if it fits, else the first free block
// that does, merging consecutive free blocks on the way.  The block records
// its slop, physicalSize - requestedSize.  nil if the page has no room.
void*
SSafeHeapPage::Alloc(long physicalSize, long requestedSize)
{
	if (fFree < (ULong) physicalSize)
		return nil;
	SSafeHeapBlock* b = fFreeBlock;
	SSafeHeapBlock* prevFree = nil;
	if (b == nil || (long) b->Size() < physicalSize)
	{
		b = FirstBlock();
		for (;;)
		{
			if (b == nil)
				return nil;
			while (b->IsFree())
			{
				if (prevFree == nil)
					goto found;
				prevFree->fWord = (prevFree->fWord & 0xff000000) | ((prevFree->fWord + b->Size()) & 0xffffff);
				fFreeBlock = prevFree;
				b = prevFree;
				prevFree = nil;
			}
			prevFree = nil;
			b = b->Next();
			continue;
		found:
			{
				ULong size = b->Size();
				prevFree = b;
				if ((long) size >= physicalSize)
					break;
			}
			b = b->Next();
		}
	}
	ULong size = b->Size();
	if ((long) size - physicalSize < (long) kSafeBlockSlop)
	{
		b->fWord = size | ((ULong32) (size - requestedSize) << 24);
		fFree -= size;
		if (fFreeBlock == b)
			fFreeBlock = nil;
	}
	else
	{
		SSafeHeapBlock* rest = (SSafeHeapBlock*) ((char*) b + physicalSize);
		rest->fWord = ((size - physicalSize) & 0xffffff) | 0xff000000;
		fFreeBlock = rest;
		b->fWord = (physicalSize & 0xffffff) | ((ULong32) (physicalSize - requestedSize) << 24);
		fFree -= physicalSize;
	}
	return b->Data();
}


// ROM 0x001c7dcc Free__13SSafeHeapPageFPv
// Frees a block of this page, merging it with the remembered free block if
// they touch (the larger of the two is remembered otherwise); a whole page
// left empty leaves the heap and goes back where it came from.
void
SSafeHeapPage::Free(void* data)
{
	SSafeHeapBlock* b = SSafeHeapBlock::Of(data);
	ULong32 word = b->fWord;
	b->fWord = word | 0xff000000;
	fFree += word & 0xffffff;
	if (fFreeBlock != nil)
	{
		if (b->Next() == fFreeBlock)
			b->fWord = (b->fWord & 0xff000000) | ((b->fWord + fFreeBlock->Size()) & 0xffffff);
		else if (fFreeBlock->Next() == b)
		{
			fFreeBlock->fWord = (fFreeBlock->fWord & 0xff000000) | ((fFreeBlock->fWord + b->Size()) & 0xffffff);
			goto done;
		}
		else if (b->Size() <= fFreeBlock->Size())
			goto done;
	}
	fFreeBlock = b;
done:
	if (fFree != kSafePageCapacity || fPrev == nil || PageOffset(this) != 0)
		return;
	fPrev->fNext = fNext;
	if (fNext == nil)
	{
		for (SSafeHeapPage* p = FirstPage(); p != nil; p = p->fNext)
			p->fLastPage = fPrev;
	}
	else
		fNext->fPrev = fPrev;
	FreePage();
}


// ROM 0x001c7f18 GetPage__13SSafeHeapPageFv
// A page from the page manager, chained onto the heap.  The heap's
// semaphore is let go meanwhile.
SSafeHeapPage*
SSafeHeapPage::GetPage()
{
	if (fSemaphore != nil)
		fSemaphore->Release();
	void* area;
	ULong physId;
	TPhys* phys;
	long err = GetNewPageFromPageMgr(&area, &physId, &phys);
	if (fSemaphore != nil)
		fSemaphore->Acquire(kWaitOnBlock);
	if (err != noErr)
		return nil;
	SSafeHeapPage* page = area != nil ? new (area) SSafeHeapPage : nil;
	page->Init(physId, phys, this);
	return page;
}


// ROM 0x001c7f9c FreePage__13SSafeHeapPageFv
void
SSafeHeapPage::FreePage()
{
	// NOT YET RECONSTRUCTED: the page goes back to the page manager - a page
	// with a physical page object (fPhysId) is forgotten by the kernel domain
	// manager (TUDomainManager::Forget) and its TPhys put back in the page
	// tracker (or the external page tracker for a card's); one without is
	// queued in the page tracker directly
}


// ROM 0x001c7a6c GetNewPageFromPageMgr__FPPvPUlPP5TPhys
long
GetNewPageFromPageMgr(void** /*outPage*/, ULong* /*outPhysId*/, TPhys** /*outPhys*/)
{
	// NOT YET RECONSTRUCTED: a page from gThePageManager mapped into the
	// kernel domain (the page manager monitor)
	return kError_Call_Not_Implemented;
}


/* -------------------------------------------------------------------------------
	The safe heap
------------------------------------------------------------------------------- */

// ROM 0x001c80a0 InitSafeHeap__FPP13SSafeHeapPage
// The kernel heap: its first page.
long
InitSafeHeap(SSafeHeapPage** outHeap)
{
	void* area;
	ULong physId;
	TPhys* phys;
	long err = GetNewPageFromPageMgr(&area, &physId, &phys);
	if (err == noErr)
	{
		SSafeHeapPage* page = area != nil ? new (area) SSafeHeapPage : nil;
		page->Init(physId, phys, nil);
		*outHeap = page;
	}
	return err;
}


// ROM 0x001c82e8 AddPartialPageToSafeHeap__FPvP13SSafeHeapPage
// The rest of a page (what the kernel's globals left of one) joins the heap.
void
AddPartialPageToSafeHeap(void* area, SSafeHeapPage* heap)
{
	if (area == nil)
		return;
	if (kSafePageSize - PageOffset(area) < kSafePageHeaderSize)
		return;
	SSafeHeapPage* page = new (area) SSafeHeapPage;
	page->Init(0, nil, heap);
}


// ROM 0x001c8320 IsSafeHeap__FPv
Boolean
IsSafeHeap(const void* heap)
{
	return ((const SSafeHeapPage*) heap)->fMagic == kSafeHeapMagic;
}


// ROM 0x001c8340 SafeHeapIsEmpty__FP13SSafeHeapPage
Boolean
SafeHeapIsEmpty(SSafeHeapPage* heap)
{
	return heap->fNext == nil && heap->fFree == kSafePageCapacity;
}


// ROM 0x001c8360 SafeHeapAlloc__FlP13SSafeHeapPage
// From the last page, then any page, then a new page.
void*
SafeHeapAlloc(long size, SSafeHeapPage* heap)
{
	long physical = ((size + 3) & ~3) + kSafeBlockHeaderSize;
	if (physical > (long) kSafePageCapacity)
		return nil;
	for (;;)
	{
		void* data = heap->fLastPage->Alloc(physical, size);
		if (data != nil)
			return data;
		for (SSafeHeapPage* p = heap->fLastPage; p != nil; p = p->fPrev)
		{
			data = p->Alloc(physical, size);
			if (data != nil)
				return data;
		}
		if (heap->GetPage() == nil)
			return nil;
	}
}


// ROM 0x001c83f0 SafeHeapRealloc__FPvl
// (always a new block, in the kernel heap)
void*
SafeHeapRealloc(void* data, long size)
{
	void* moved = SafeHeapAlloc(size, (SSafeHeapPage*) gKernelHeap);
	if (data != nil && moved != nil)
	{
		long count = SafeHeapBlockSize(data);
		if (size < count)
			count = size;
		BlockMove(data, moved, count);
		SafeHeapFree(data);
	}
	return moved;
}


// ROM 0x001c8458 SafeHeapFree__FPv
void
SafeHeapFree(void* data)
{
	if (data == nil)
		return;
	SSafeHeapPage* page = (SSafeHeapPage*) *SafeHeapEndSentinelFor(data);
	page->Free(data);
}


/* -------------------------------------------------------------------------------
	The wired heap
------------------------------------------------------------------------------- */

// ROM 0x001c8210 GrowByOnePage__15SWiredHeapDescrFv
// One more page of the area, wired.
long
SWiredHeapDescr::GrowByOnePage()
{
	long err = SetHeapLimits(fStart, fStart + fSize + kSafePageSize);
	if (err == noErr)
	{
		err = LockHeapRange(fStart + fSize, fStart + fSize + kSafePageSize, true);
		if (err == noErr)
			fSize += kSafePageSize;
		else
			SetHeapLimits(fStart, fStart + fSize - kSafePageSize);
	}
	return err;
}


// ROM 0x001c8284 ShrinkByOnePage__15SWiredHeapDescrFv
void
SWiredHeapDescr::ShrinkByOnePage()
{
	if (UnlockHeapRange(fStart + fSize - kSafePageSize, fStart + fSize) != noErr)
		return;
	SetHeapLimits(fStart, fStart + fSize - kSafePageSize);
	fSize -= kSafePageSize;
}


// ROM 0x001c8000 New__14SWiredHeapPageSFP15SWiredHeapDescr
// The wired heap's first page, at the start of the descriptor's area.
SSafeHeapPage*
SWiredHeapPage::New(SWiredHeapDescr* descr)
{
	descr->fStart = (descr->fStart + kSafePageSize - 1) & ~(VAddr) (kSafePageSize - 1);
	descr->fPage = nil;
	descr->fSize = 0;
	if (descr->GrowByOnePage() != noErr)
		return nil;
	SWiredHeapPage* page = (SWiredHeapPage*) descr->fStart;
	if (page != nil)
		page = new ((void*) descr->fStart) SWiredHeapPage;
	page->Init(0, nil, nil);
	page->fRefCon = descr;
	return page;
}


// ROM 0x001c8080 Destroy__14SWiredHeapPageFv
long
SWiredHeapPage::Destroy()
{
	FreePagedMem(((SWiredHeapDescr*) fRefCon)->fStart);
	return noErr;
}


// ROM 0x001c8108 GetPage__14SWiredHeapPageFv
// The next page of the area.
SSafeHeapPage*
SWiredHeapPage::GetPage()
{
	SWiredHeapDescr* descr = (SWiredHeapDescr*) fRefCon;
	if (descr->GrowByOnePage() != noErr)
		return nil;
	void* area = (void*) (descr->fStart + descr->fSize - kSafePageSize);
	SWiredHeapPage* page = area != nil ? new (area) SWiredHeapPage : nil;
	page->Init(0, nil, this);
	return page;
}


// ROM 0x001c816c FreePage__14SWiredHeapPageFv
// The pages above the highest still in use are given back.
void
SWiredHeapPage::FreePage()
{
	SSafeHeapPage* first = FirstPage();
	VAddr highest = 0;
	for (SSafeHeapPage* p = first; p != nil; p = p->fNext)
	{
		if (highest < (VAddr) p + kSafePageSize)
			highest = (VAddr) p + kSafePageSize;
	}
	SWiredHeapDescr* descr = (SWiredHeapDescr*) first->fRefCon;
	while (highest < descr->fStart + descr->fSize)
		descr->ShrinkByOnePage();
}
