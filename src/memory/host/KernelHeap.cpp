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

const Size kHostKernelHeapSize = 32 * 1024 * 1024;		// what the kernel and the services allocate with NewPtr - and, the host having one heap where the ROM has one per domain, the NewtonScript world's object heap (TObjectHeap: 4 MB) and everything else


void
InitHostKernelHeap(void)
{
	void* area = malloc(kHostKernelHeapSize);
	gKernelHeap = NewHeap(area, kHostKernelHeapSize, 0x1000);
}


void
InitHostStandaloneHeap(Size kernelHeapSize)
{
	static TaskGlobals globals;
	memset(&globals, 0, sizeof(globals));
	gCurrentGlobals = (char*) &globals + kTaskGlobalsSize;
	gOSIsRunning = true;
	if (kernelHeapSize == 0)
		InitHostKernelHeap();
	else
	{
		void* area = malloc(kernelHeapSize);
		gKernelHeap = NewHeap(area, kernelHeapSize, 0x1000);
	}
	SetHeap(gKernelHeap);
}
