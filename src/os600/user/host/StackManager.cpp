/*
	File:		user/host/StackManager.cpp

	Contains:	A host stand-in for the stack manager's paged-memory API
				(VirtualMemory.h: NewStack, FreePagedMem, LockHeapRange,
				UnlockHeapRange).  On the MessagePad TStackManager (0x001f408c-)
				is a fault monitor that pages a task's stack in on demand and
				these are monitor calls into it; on a host a stack is a plain
				allocation and locking is a no-op.  This is a stand-in, not a
				reconstruction - TStackManager itself is still to come.
*/

#include "VirtualMemory.h"
#include "OSErrors.h"

#include <stdlib.h>

extern "C" long
NewStack(TObjectId /*domainId*/, ULong maxSize, TObjectId /*ownerId*/, VAddr* returnTopOfStack, VAddr* returnBottomOfStack)
{
	char* stack = (char*) malloc(maxSize);
	if (stack == nil)
		return kError_No_Memory;
	*returnBottomOfStack = (VAddr) stack;
	*returnTopOfStack = (VAddr) (stack + maxSize);
	return noErr;
}

extern "C" void
FreePagedMem(VAddr addressInArea)
{
	free((void*) addressInArea);
}

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
