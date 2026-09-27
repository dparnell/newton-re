/*
	File:		host/HostHeapCheck.h

	Contains:	A host debugging aid: the task's heap walked after every
				allocation, so the first damaged block is caught by the
				code that damaged it rather than long afterwards.

				With NEWTON_HEAPCHECK set in the environment (to a number
				N: every Nth allocation; empty or 1: every one),
				HostHeapCheckInstall puts a walker on the current task's
				heap (SkiaHeap::fAfterAllocHook) and on DisposPtr
				(gHostDisposeCheck).  The walker checks every block's
				size and alignment, that a direct block's parent is the
				heap, and that the free list runs through the free blocks
				and only them, in address order, with its back links
				right, and that a handle's block is the one its master
				pointer names.  (The hook runs before the allocator sets
				the new block's flags, so after an allocation one
				flagless block off the list is the new one, and allowed.)  A damaged heap prints what was wrong
				and the C stack as image offsets (tools/host/whichfunction.py
				names them), then stops the program.  A pointer given back
				twice is caught the same way.  NEWTON_HEAPDUMP as well lists
				the free list and every block at each walk (a lot of
				output).  Not the ROM's; nothing is installed without the
				variable.
*/

#ifndef __HOSTHEAPCHECK_H
#define __HOSTHEAPCHECK_H

void	HostHeapCheckInstall(void);		// on the current task's heap, when NEWTON_HEAPCHECK is set
void	HostHeapCheckNow(const char* where);	// one walk, now (a no-op unless installed)

#endif	/* __HOSTHEAPCHECK_H */
