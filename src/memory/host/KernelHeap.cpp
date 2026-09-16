/*
	File:		memory/host/KernelHeap.cpp

	Contains:	The host's kernel heap (KernelHeap.h): a Skia heap of up to
				kHostKernelHeapSize over a host allocation, standing in for the
				safe heap VMemInit builds over the page manager's pages.
*/

#include "memory/host/KernelHeap.h"
#include "SkiaHeap.h"
#include "Boot.h"
#include "KernelGlobals.h"
#include "os600/TaskGlobals.h"

#include <stdlib.h>
#include <string.h>

const Size kHostKernelHeapSize = 4 * 1024 * 1024;		// what the kernel and the services allocate with NewPtr


void
InitHostKernelHeap(void)
{
	void* area = malloc(kHostKernelHeapSize);
	gKernelHeap = NewHeap(area, kHostKernelHeapSize, 0x1000);
}


void
InitHostStandaloneHeap(void)
{
	static TaskGlobals globals;
	memset(&globals, 0, sizeof(globals));
	gCurrentGlobals = (char*) &globals + kTaskGlobalsSize;
	gOSIsRunning = true;
	InitHostKernelHeap();
	SetHeap(gKernelHeap);
}
