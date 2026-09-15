/*
	File:		toolbox/host/Memory.cpp

	Contains:	A host stand-in for the pointer part of the Newton memory
				manager (NewtonMemory.h: NewPtr, NewPtrClear, NewNamedPtr,
				DisposPtr, GetPtrSize, ReallocPtr, MemError, BlockMove).  On
				the MessagePad these run over the heap manager (NewPtr is at
				0x0014467c, in the direct-block heap code); on a host a block
				comes from malloc, with its size kept in front of it so that
				GetPtrSize and ReallocPtr work.  A stand-in, not a
				reconstruction - the heap manager itself is still to come.
*/

#include "Newton.h"
#include "NewtonMemory.h"
#include "OSErrors.h"

#include <stdlib.h>
#include <string.h>

// the block header: the size, padded so the block stays 16-byte aligned
struct HostPtrHeader
{
	Size		size;
	char		pad[16 - sizeof(Size)];
};

static NewtonErr	gHostMemError = noErr;		// the ROM keeps this per task (in the task globals)


extern "C" NewtonErr
MemError(void)
{
	return gHostMemError;
}


extern "C" Ptr
NewPtr(Size size)
{
	HostPtrHeader* header = (HostPtrHeader*) malloc(sizeof(HostPtrHeader) + (size_t) size);
	if (header == nil)
	{
		gHostMemError = kError_No_Memory;
		return nil;
	}
	header->size = size;
	gHostMemError = noErr;
	return (Ptr) (header + 1);
}


extern "C" Ptr
NewNamedPtr(Size size, ULong /*name*/)
{
	return NewPtr(size);
}


extern "C" Ptr
NewPtrClear(Size byteCount)
{
	Ptr p = NewPtr(byteCount);
	if (p != nil)
		memset(p, 0, (size_t) byteCount);
	return p;
}


extern "C" void
DisposPtr(Ptr p)
{
	gHostMemError = noErr;
	if (p != nil)
		free((HostPtrHeader*) p - 1);
}


extern "C" Size
GetPtrSize(Ptr p)
{
	gHostMemError = noErr;
	return p != nil ? ((HostPtrHeader*) p - 1)->size : 0;
}


extern "C" Ptr
ReallocPtr(Ptr p, Size size)
{
	if (p == nil)
		return NewPtr(size);
	HostPtrHeader* header = (HostPtrHeader*) realloc((HostPtrHeader*) p - 1, sizeof(HostPtrHeader) + (size_t) size);
	if (header == nil)
	{
		gHostMemError = kError_No_Memory;
		return nil;
	}
	header->size = size;
	gHostMemError = noErr;
	return (Ptr) (header + 1);
}


extern "C" void
BlockMove(const void* srcPtr, void* destPtr, Size byteCount)
{
	memmove(destPtr, srcPtr, (size_t) byteCount);
}
