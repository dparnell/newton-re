/*
	File:		frames/NativeFunctions.h

	Contains:	The binding of native (C) NewtonScript functions to the
				interpreter.  A native function object holds the address of
				its C function (MakeCFunction: [class 0x132, funcPtr,
				numArgs]); the ROM's built-in functions hold jump-table
				addresses of the ROM's F... functions (ROMNatives.cpp, the
				table nsfunctions.py generates).  On the host a function
				object made here holds a host function pointer, and a ROM
				function's address is looked up in a registry of the host
				implementations of the ROM's functions, bound by the symbol
				name they had in the ROM (RegisterNativeFunction: "FLength"
				-> FLength).  Calling one whose implementation is not
				reconstructed yet throws kNSErrNativeNotReconstructed.

	Host re-expression: in the ROM funcPtr is simply called.
*/

#ifndef __NATIVEFUNCTIONS_H
#define __NATIVEFUNCTIONS_H

#ifndef __OBJECTS_H
#include "objects.h"
#endif

// native functions take the receiver then their arguments, and answer a Ref
typedef Ref (*NativeFn0)(RefArg rcvr);
typedef Ref (*NativeFn1)(RefArg rcvr, RefArg a1);
typedef Ref (*NativeFn2)(RefArg rcvr, RefArg a1, RefArg a2);
typedef Ref (*NativeFn3)(RefArg rcvr, RefArg a1, RefArg a2, RefArg a3);
typedef Ref (*NativeFn4)(RefArg rcvr, RefArg a1, RefArg a2, RefArg a3, RefArg a4);
typedef Ref (*NativeFn5)(RefArg rcvr, RefArg a1, RefArg a2, RefArg a3, RefArg a4, RefArg a5);
typedef Ref (*NativeFn6)(RefArg rcvr, RefArg a1, RefArg a2, RefArg a3, RefArg a4, RefArg a5, RefArg a6);

// the ROM's native built-in functions (ROMNatives.cpp)
struct ROMNativeEntry
{
	const char*		fName;			// its name in the built-in functions frame
	unsigned int	fFuncPtr;		// the funcPtr the ROM's function object holds (a jump-table address)
	unsigned int	fTarget;		// the ROM function it reaches
	long			fNumArgs;
	const char*		fSymbol;		// that function's symbol ("FLength")
};
extern const ROMNativeEntry	gROMNativeEntries[];
extern const long			gROMNativeCount;

// addresses below this are the ROM's (its code and jump table), looked up;
// above it, host function pointers, called
const ULong	kROMCodeLimit = 0x02000000;

// a host implementation of the ROM function of this symbol
void	RegisterNativeFunction(const char* symbol, void* fn, long numArgs);
// what a funcPtr calls: nil when nothing is bound to a ROM address
void*	ResolveNativeFunction(ULong funcPtr, long* numArgs);
// the host implementations of the core built-ins (Builtins.cpp), registered
void	RegisterBuiltinNatives(void);
// without the ROM's built-in functions frame: a native function object in
// gFunctionFrame for every bound native, under its NewtonScript name
void	InstallHostNatives(void);

#endif	/* __NATIVEFUNCTIONS_H */
