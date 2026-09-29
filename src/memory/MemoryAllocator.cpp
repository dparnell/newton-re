/*
	File:		memory/MemoryAllocator.cpp

	Contains:	THeapAllocator and TNoReuseAllocator (MemoryAllocator.h).
*/

#include "MemoryAllocator.h"
#include "NewtonMemory.h"

#include <new>


/*------------------------------------------------------------------------------
	T H e a p A l l o c a t o r
------------------------------------------------------------------------------*/

// ROM 0x00030bfc Allocate__14THeapAllocatorFUl
// malloc, which in the ROM is NewPtr, with the C++ new handler called when
// it fails.
void*
THeapAllocator::Allocate(ULong size)
{
	if (size == 0)
		size = 1;
	void* block = NewPtr(size);
	if (block == nil)
	{
		std::new_handler handler = std::get_new_handler();
		if (handler != nil)
			handler();
	}
	return block;
}


// ROM 0x00030c04 Deallocate__14THeapAllocatorFPv
// free, which is DisposePtr (the ROM has it inline).
void
THeapAllocator::Deallocate(void* block)
{
	if (block != nil)
		DisposePtr((Ptr) block);
}


// ROM 0x00030c0c GetGlobalAllocator__14THeapAllocatorSFv
// One allocator, made the first time it is asked for (0x0c100884: its
// table pointer, 0x0c100888 the flag).
THeapAllocator*
THeapAllocator::GetGlobalAllocator(void)
{
	static THeapAllocator	gGlobalAllocator;
	return &gGlobalAllocator;
}


/*------------------------------------------------------------------------------
	T N o R e u s e A l l o c a t o r
------------------------------------------------------------------------------*/

// ROM 0x00030bcc Allocate__17TNoReuseAllocatorFUl
// Nothing is rounded: the caller asks for sizes that keep the next block
// aligned.
void*
TNoReuseAllocator::Allocate(ULong size)
{
	void* block = nil;
	if (size <= fRemaining)
	{
		block = fNext;
		fNext += size;
		fRemaining -= size;
	}
	return block;
}


// ROM 0x00030bf8 Deallocate__17TNoReuseAllocatorFPv
void
TNoReuseAllocator::Deallocate(void*)
{
}
