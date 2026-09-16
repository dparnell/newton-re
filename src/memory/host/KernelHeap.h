/*
	File:		memory/host/KernelHeap.h

	Contains:	The host's kernel heap.  On the MessagePad the kernel heap
				(gKernelHeap) is a safe heap over pages from the page manager
				(VMemInit, called from InitCGlobals's persistent recovery before
				OsBoot); the page manager is not reconstructed yet, so the host
				gives the kernel a Skia heap over a plain allocation.  A
				stand-in, not a reconstruction.
*/

#ifndef __HOSTKERNELHEAP_H
#define __HOSTKERNELHEAP_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

void	InitHostKernelHeap(void);		// makes gKernelHeap; before the first task exists

// For host tests that use the memory manager without booting the kernel:
// the memory manager keeps the current heap and MemError in the task
// globals, so a test stands as a task with globals of its own, the OS
// marked running, and the kernel heap current.
void	InitHostStandaloneHeap(void);

#endif	/* __HOSTKERNELHEAP_H */
