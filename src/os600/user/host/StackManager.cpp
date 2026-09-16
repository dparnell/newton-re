/*
	File:		user/host/StackManager.cpp

	Contains:	A host stand-in for the stack manager's paged-memory API
				(VirtualMemory.h: NewStack, NewHeapArea, SetHeapLimits,
				LockHeapRange, UnlockHeapRange, SetRemoveRoutine,
				GetHeapAreaInfo, FreePagedMem).  On the MessagePad TStackManager
				(0x001f408c-) is a fault monitor that pages a task's stack or a
				heap's area in on demand, and these are monitor calls into it
				(selectors 2, 3, 6, 7, 10, 11 through gStackManager's monitor);
				on a host an area is a plain allocation of its whole maximum
				size, limits and locking are no-ops, and release routines are
				never called (no shortage of pages).  This is a stand-in, not a
				reconstruction - TStackManager itself is still to come.
*/

#include "VirtualMemory.h"
#include "OSErrors.h"

#include <stdlib.h>

// The areas handed out, so that an address in one finds its bounds
// (GetHeapAreaInfo) and the allocation behind it (FreePagedMem).
struct HostArea
{
	char*		fBlock;			// what malloc gave
	VAddr		fStart;			// the page-aligned start handed out
	VAddr		fEnd;
	HostArea*	fNext;
};

static HostArea*	gHostAreas = nil;
const ULong			kHostPageSize = 0x1000;


static HostArea*
FindArea(VAddr addr)
{
	for (HostArea* a = gHostAreas; a != nil; a = a->fNext)
	{
		if (addr >= a->fStart && addr <= a->fEnd)
			return a;
	}
	return nil;
}


static long
MakeArea(ULong maxSize, VAddr* returnStart, VAddr* returnEnd)
{
	HostArea* area = (HostArea*) malloc(sizeof(HostArea));
	char* block = (char*) malloc(maxSize + kHostPageSize);
	if (area == nil || block == nil)
	{
		free(area);
		free(block);
		return kError_No_Memory;
	}
	area->fBlock = block;
	area->fStart = ((VAddr) block + kHostPageSize - 1) & ~(VAddr) (kHostPageSize - 1);
	area->fEnd = area->fStart + maxSize;
	area->fNext = gHostAreas;
	gHostAreas = area;
	*returnStart = area->fStart;
	*returnEnd = area->fEnd;
	return noErr;
}


// selector 1 (NewStack): a task's stack
extern "C" long
NewStack(TObjectId /*domainId*/, ULong maxSize, TObjectId /*ownerId*/, VAddr* returnTopOfStack, VAddr* returnBottomOfStack)
{
	VAddr start, end;
	long err = MakeArea(maxSize, &start, &end);
	if (err != noErr)
		return err;
	*returnBottomOfStack = start;
	*returnTopOfStack = end;
	return noErr;
}


// selector 2 (NewHeapArea, 0x001f62b0): a heap's area of address space
extern "C" long
NewHeapArea(TObjectId /*domainId*/, VAddr /*requestedAddr*/, ULong maxSize, ULong /*options*/, VAddr* returnStart, VAddr* returnEnd)
{
	return MakeArea(maxSize, returnStart, returnEnd);
}


// selector 3 (SetHeapLimits, 0x001f62fc): how much of the area is in use
extern "C" long
SetHeapLimits(VAddr start, VAddr end)
{
	HostArea* area = FindArea(start);
	if (area == nil || end > area->fEnd)
		return kError_Bad_Parameters;
	return noErr;
}


// selectors 6 and 7 (LockHeapRange, UnlockHeapRange): pages are always in
extern "C" long
LockHeapRange(VAddr /*start*/, VAddr /*end*/, Boolean /*wire*/)
{
	return noErr;
}

extern "C" long
UnlockHeapRange(VAddr /*start*/, VAddr /*end*/)
{
	return noErr;
}


// selector 10 (SetRemoveRoutine, 0x001f632c): whom to ask when pages are
// short - never, here
extern "C" long
SetRemoveRoutine(VAddr /*addr*/, ReleaseProcPtr /*releaseProc*/, void* /*releaseRefCon*/)
{
	return noErr;
}


// selector 11 (GetHeapAreaInfo, 0x001f635c): the bounds of the area an
// address is in
extern "C" long
GetHeapAreaInfo(VAddr addr, VAddr* returnStart, VAddr* returnEnd)
{
	HostArea* area = FindArea(addr);
	if (area == nil)
		return kError_Bad_Parameters;
	*returnStart = area->fStart;
	*returnEnd = area->fEnd;
	return noErr;
}


// FreePagedMem: the area an address is in goes
extern "C" void
FreePagedMem(VAddr addressInArea)
{
	HostArea** link = &gHostAreas;
	for (HostArea* a = gHostAreas; a != nil; link = &a->fNext, a = a->fNext)
	{
		if (addressInArea >= a->fStart && addressInArea <= a->fEnd)
		{
			*link = a->fNext;
			free(a->fBlock);
			free(a);
			return;
		}
	}
}
