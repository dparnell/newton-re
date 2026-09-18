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

struct NativeBinding
{
	ULong	fFuncPtr;			// the address a ROM function object holds
	void*	fFunction;
	long	fNumArgs;
};

static NativeBinding*	gNativeBindings = nil;
static long				gNativeBindingCount = 0;
static long				gNativeBindingCapacity = 0;


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
		Boolean bound = false;
		for (long j = 0; j < gNativeBindingCount; j++)
			if (gNativeBindings[j].fFuncPtr == entry.fFuncPtr)
			{
				gNativeBindings[j].fFunction = fn;
				gNativeBindings[j].fNumArgs = numArgs;
				bound = true;
			}
		if (bound)
			continue;
		if (gNativeBindingCount == gNativeBindingCapacity)
		{
			long capacity = gNativeBindingCapacity == 0 ? 64 : gNativeBindingCapacity * 2;
			NativeBinding* grown = new NativeBinding[capacity];
			for (long j = 0; j < gNativeBindingCount; j++)
				grown[j] = gNativeBindings[j];
			delete[] gNativeBindings;
			gNativeBindings = grown;
			gNativeBindingCapacity = capacity;
		}
		gNativeBindings[gNativeBindingCount].fFuncPtr = entry.fFuncPtr;
		gNativeBindings[gNativeBindingCount].fFunction = fn;
		gNativeBindings[gNativeBindingCount].fNumArgs = numArgs;
		gNativeBindingCount++;
	}
}


// Host: the ROM's own name for the native at an address, so that a boot
// which asks for one we have not written yet says which.
const ROMNativeEntry*
ROMNativeAt(unsigned int funcPtr)
{
	for (long i = 0; i < gROMNativeCount; i++)
		if (gROMNativeEntries[i].fFuncPtr == funcPtr)
			return &gROMNativeEntries[i];
	return nil;
}


void*
ResolveNativeFunction(ULong funcPtr, long* numArgs)
{
	for (long i = 0; i < gNativeBindingCount; i++)
		if (gNativeBindings[i].fFuncPtr == funcPtr)
		{
			*numArgs = gNativeBindings[i].fNumArgs;
			return gNativeBindings[i].fFunction;
		}
	return nil;
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
