/*
	File:		frames/PackageNatives.h

	Contains:	How a package's native functions run on the host.

				NTK compiles a NewtonScript function marked native into ARM
				code, all of a package's into one binary; the function object
				is an array of class 0x232 ([0x232, the code binary, numArgs,
				closure, the offset of its code in the binary, ...]) or a
				binCFunction frame ({code, offset, numArgs, closure}), and the
				ROM calls the code as a C function (TInterpreter::CallCFunction
				0x002f4da8: the receiver and each argument passed by
				reference, the closure - when there is one - as one argument
				more).  A host cannot call ARM code, so (the owner's
				decision):

				1. a function that has been re-expressed as host code runs
				   that: RegisterPackageNative binds a host C function (the
				   NativeFunctions.h signatures: the receiver, then the
				   arguments, each a RefArg) to a function in a package's
				   code, identified by
				     - the code binary's length and a 32-bit FNV-1a hash of
				       its bytes (PackageNativeCodeHash), which does not
				       depend on where the package was loaded or what it is
				       called, and changes if a single byte of the code does
				       (a different build of the package is a different
				       binary, and its functions are not taken for these);
				     - the function's offset in that binary (the object's
				       offset slot);
				2. anything else goes to the fallback, when one is installed
				   (SetPackageNativeFallback: an ARM interpreter), given the
				   code binary, the offset, the receiver and the arguments;
				3. with neither, the call throws kNSErrNativeNotReconstructed
				   (exInterpreter), saying which function it was.

				The hash of a binary is worked out once (cached by the
				binary's address, which a package's objects keep while the
				package is in).

	Host only (the ROM runs the code).
*/

#ifndef __PACKAGENATIVES_H
#define __PACKAGENATIVES_H

#ifndef __OBJECTS_H
#include "objects.h"
#endif

// A function in a package's native code: the code binary's length and
// hash, and the function's offset in it.
struct PackageNativeKey
{
	ULong		fCodeLength;
	ULong		fCodeHash;
	ULong		fOffset;
};

// 32-bit FNV-1a over the bytes.
ULong		PackageNativeCodeHash(const void* code, ULong length);

// A host re-expression of a package native: fn is a NativeFn0..NativeFn6
// (frames/NativeFunctions.h) taking numArgs arguments after the receiver
// (the closure, for a function that has one, counts as the last); name is
// for messages.
void		RegisterPackageNative(ULong codeLength, ULong codeHash, ULong offset, void* fn, long numArgs, const char* name);

// The fallback for package natives nothing is registered for: code is the
// code binary (its bytes are BinaryData(code), big-endian ARM), offset
// where the function starts in it; rcvr and args[0..numArgs-1] are what
// the ROM passes by reference (args[i] is the argument's RefVar; numArgs
// includes the closure, last, when the function has one).  It answers the
// function's result.
typedef Ref	(*PackageNativeFallback)(RefArg code, ULong offset, RefArg rcvr, long numArgs, const RefVar* const* args);
void		SetPackageNativeFallback(PackageNativeFallback fallback);
PackageNativeFallback	GetPackageNativeFallback(void);

// The host function for a package native (nil: none registered), and its
// argument count; the key it was looked up by, when wanted.
void*		FindPackageNative(RefArg code, ULong offset, long* numArgs, PackageNativeKey* key);

#endif
