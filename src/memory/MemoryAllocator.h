/*
	File:		memory/MemoryAllocator.h

	Contains:	TMemoryAllocator, the interface the flash code (and the
				reserved block's accessor) takes its memory through, so that
				the same code can run at boot, before there is a heap, out of
				a page handed over for the purpose (TNoReuseAllocator), and
				later out of the ordinary heap (THeapAllocator).

	Not in the DDK; the interface is Allocate then Deallocate, the two
	virtuals every caller reaches at +0 and +4 of the allocator's table.
*/

#ifndef __MEMORYALLOCATOR_H
#define __MEMORYALLOCATOR_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

class TMemoryAllocator
{
public:
	virtual void*	Allocate(ULong size) = 0;
	virtual void	Deallocate(void* block) = 0;
};


// the heap: NewPtr and DisposePtr (the ROM's malloc and free)
class THeapAllocator : public TMemoryAllocator
{
public:
	void*			Allocate(ULong size);							// ROM 0x00030bfc Allocate__14THeapAllocatorFUl
	void			Deallocate(void* block);						// ROM 0x00030c04 Deallocate__14THeapAllocatorFPv

	static THeapAllocator*	GetGlobalAllocator(void);				// ROM 0x00030c0c GetGlobalAllocator__14THeapAllocatorSFv
};


// A block of memory handed out front to back and never taken back: what the
// boot allocates from before the heap exists (InitCGlobals, out of the
// early-boot page).
class TNoReuseAllocator : public TMemoryAllocator
{
public:
					TNoReuseAllocator(void* block, ULong size) : fNext((char*) block), fRemaining(size) { }

	void*			Allocate(ULong size);							// ROM 0x00030bcc Allocate__17TNoReuseAllocatorFUl
	void			Deallocate(void* block);						// ROM 0x00030bf8 Deallocate__17TNoReuseAllocatorFPv

	char*			fNext;			// +0x04
	ULong			fRemaining;		// +0x08
};

#endif	/* __MEMORYALLOCATOR_H */
