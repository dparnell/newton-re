/*
	File:		frames/NativeFunctions.cpp

	Contains:	The registry that binds the ROM's native built-in functions
				(by the symbol of the C function their funcPtr reaches) to
				host implementations (NativeFunctions.h).  A host stand-in
				for the ARM calling funcPtr.
*/

#include "NativeFunctions.h"
#include "Interpreter.h"
#include "ObjectHeap.h"

#include <string.h>
#include <stdio.h>

struct NativeBinding
{
	ULong	fFuncPtr;			// the address a ROM function object holds
	void*	fFunction;
	long	fNumArgs;
};

static NativeBinding*	gNativeBindings = nil;
static long				gNativeBindingCount = 0;
static long				gNativeBindingCapacity = 0;
// Host: the bindings found by funcPtr through an open-addressed hash table
// (each slot a binding's index + 1, 0 empty; twice the bindings' capacity,
// a power of two), rather than by looking at each - the interpreter asks
// at every call of a native (CallCFuncPtr), and the bindings are some
// thousands
static long*			gNativeIndex = nil;
static long				gNativeIndexSize = 0;


static inline unsigned long
NativeIndexSlot(ULong funcPtr)
{
	return (unsigned long) (((funcPtr >> 2) * 2654435761u) & (ULong32) (gNativeIndexSize - 1));
}


// ==> the binding of funcPtr, or nil
static NativeBinding*
FindNativeBinding(ULong funcPtr)
{
	if (gNativeIndexSize == 0)
		return nil;
	for (unsigned long slot = NativeIndexSlot(funcPtr); gNativeIndex[slot] != 0; slot = (slot + 1) & (gNativeIndexSize - 1))
		if (gNativeBindings[gNativeIndex[slot] - 1].fFuncPtr == funcPtr)
			return &gNativeBindings[gNativeIndex[slot] - 1];
	return nil;
}


static void
IndexNativeBinding(long index)
{
	unsigned long slot = NativeIndexSlot(gNativeBindings[index].fFuncPtr);
	while (gNativeIndex[slot] != 0)
		slot = (slot + 1) & (gNativeIndexSize - 1);
	gNativeIndex[slot] = index + 1;
}


// Bind the host implementation of the ROM function named symbol to the
// funcPtr of every built-in (or prototype method) that reaches it.
void
RegisterNativeFunction(const char* symbol, void* fn, long numArgs)
{
	for (long i = 0; i < gROMNativeCount + gROMMethodCount; i++)
	{
		const ROMNativeEntry& entry = i < gROMNativeCount ? gROMNativeEntries[i] : gROMMethodEntries[i - gROMNativeCount];
		if (strcmp(entry.fSymbol, symbol) != 0)
			continue;
		// The ROM's own function object says how many arguments it is called
		// with, and the interpreter passes that many; a host implementation
		// that expects a different number reads one argument past the end of
		// the stack, which is a crash a long way from here.  Say so instead.
		if (entry.fNumArgs != numArgs)
		{
			fprintf(stderr, "[frames] %s is registered for %ld arguments, the ROM calls it with %ld\n",
					symbol, numArgs, entry.fNumArgs);
			fflush(stderr);
		}
		NativeBinding* bound = FindNativeBinding(entry.fFuncPtr);
		if (bound != nil)
		{
			bound->fFunction = fn;
			bound->fNumArgs = numArgs;
			continue;
		}
		if (gNativeBindingCount == gNativeBindingCapacity)
		{
			long capacity = gNativeBindingCapacity == 0 ? 64 : gNativeBindingCapacity * 2;
			NativeBinding* grown = new NativeBinding[capacity];
			for (long j = 0; j < gNativeBindingCount; j++)
				grown[j] = gNativeBindings[j];
			delete[] gNativeBindings;
			gNativeBindings = grown;
			gNativeBindingCapacity = capacity;
			delete[] gNativeIndex;
			gNativeIndexSize = capacity * 2;
			gNativeIndex = new long[gNativeIndexSize];
			memset(gNativeIndex, 0, gNativeIndexSize * sizeof(long));
			for (long j = 0; j < gNativeBindingCount; j++)
				IndexNativeBinding(j);
		}
		gNativeBindings[gNativeBindingCount].fFuncPtr = entry.fFuncPtr;
		gNativeBindings[gNativeBindingCount].fFunction = fn;
		gNativeBindings[gNativeBindingCount].fNumArgs = numArgs;
		IndexNativeBinding(gNativeBindingCount);
		gNativeBindingCount++;
	}
}


// Host: the ROM's own name for the native at an address, so that a boot
// which asks for one we have not written yet says which.  Both tables are
// searched: a native reached as a method of one of the ROM's prototype
// frames is in the second, and looking only in the first left the boot
// reporting a bare jump-table address for those.
const ROMNativeEntry*
ROMNativeAt(unsigned int funcPtr)
{
	for (long i = 0; i < gROMNativeCount; i++)
		if (gROMNativeEntries[i].fFuncPtr == funcPtr)
			return &gROMNativeEntries[i];
	for (long i = 0; i < gROMMethodCount; i++)
		if (gROMMethodEntries[i].fFuncPtr == funcPtr)
			return &gROMMethodEntries[i];
	return nil;
}


void*
ResolveNativeFunction(ULong funcPtr, long* numArgs)
{
	NativeBinding* binding = FindNativeBinding(funcPtr);
	if (binding == nil)
		return nil;
	*numArgs = binding->fNumArgs;
	return binding->fFunction;
}


// Host: with no ROM objects imported there is no built-in functions frame
// for GlobalFunctionLookup to fall back on, so each bound native gets a
// function object (MakeCFunction, class 0x132 with the host pointer) in
// gFunctionFrame under the name the ROM's frame gives it.
void
InstallHostNatives(void)
{
	if (gROMBuiltinFunctions != NILREF)
		return;
	RefVar fn;
	for (long i = 0; i < gROMNativeCount; i++)
	{
		const ROMNativeEntry& entry = gROMNativeEntries[i];
		long numArgs;
		void* host = ResolveNativeFunction(entry.fFuncPtr, &numArgs);
		if (host == nil)
			continue;
		fn = MakeCFunction(host, numArgs, nil);
		SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) entry.fName)), fn);
	}
}
