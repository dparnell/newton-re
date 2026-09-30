// Package natives (frames/PackageNatives.h): a function whose code is in a
// binary - as NTK's native compiler makes them - called from NewtonScript
// runs the host re-expression registered for its code's hash and offset,
// then the fallback, then throws; its closure goes as an argument more.

#include "PackageNatives.h"
#include "Interpreter.h"
#include "Compiler.h"
#include "ObjectHeap.h"
#include "Frames.h"
#include "RSSymbols.h"
#include "NSErrors.h"
#include "ROMImport.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static Ref
Eval(const char* source)
{
	RefVar fn(ParseString(RefVar(MakeString(source))));
	return InterpretBlock(fn, RefVar(gVarFrame));
}

// 0x232 function objects over one "code" binary
static Ref
MakeBinaryNative(RefArg code, long numArgs, RefArg closure, long offset)
{
	RefVar fn(MakeArray(5));
	SetArraySlotRef(fn, 0, kBinaryNativeFuncClass);
	SetArraySlot(fn, 1, code);
	SetArraySlotRef(fn, 2, MAKEINT(numArgs));
	SetArraySlot(fn, 3, closure);
	SetArraySlotRef(fn, 4, MAKEINT(offset));
	return fn;
}

static Ref
HostAdd(RefArg rcvr, RefArg a, RefArg b)
{
	return MAKEINT(RINT(a) + RINT(b));
}

static Ref
HostWithClosure(RefArg rcvr, RefArg a, RefArg closure)
{
	return MAKEINT(RINT(a) * 100 + RINT(closure));
}

static long sFallbackOffset = -1;
static long sFallbackArgs = -1;
static Ref
Fallback(RefArg code, ULong offset, RefArg rcvr, long numArgs, const RefVar* const* args)
{
	sFallbackOffset = offset;
	sFallbackArgs = numArgs;
	return MAKEINT(RINT(*args[0]) - 1);
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_OBJECTS) != noErr)
	{
		printf("test_PackageNatives: cannot import %s\n", NEWTON_OBJECTS);
		return 1;
	}
	gObjectHeapSize = 0x100000;
	InitObjects();

	RefVar code(AllocateBinary(RSSYMbinary, 64));
	memset(BinaryData(code), 0xe5, 64);
	ULong hash = PackageNativeCodeHash(BinaryData(code), 64);
	RegisterPackageNative(64, hash, 8, (void*) HostAdd, 2, "add");
	RegisterPackageNative(64, hash, 16, (void*) HostWithClosure, 2, "closure");

	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern("pkgAdd")), RefVar(MakeBinaryNative(code, 2, RefVar(), 8)));
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern("pkgClosure")), RefVar(MakeBinaryNative(code, 1, RefVar(MAKEINT(7)), 16)));
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern("pkgOther")), RefVar(MakeBinaryNative(code, 1, RefVar(), 24)));

	// the host re-expression, with and without a closure
	Ref r = Eval("call pkgAdd with (40, 2)");
	EXPECT(ISINT(r) && RINT(r) == 42);
	r = Eval("call pkgClosure with (3)");
	EXPECT(ISINT(r) && RINT(r) == 307);

	// wrong argument count
	long thrown = 0;
	newton_try
	{
		Eval("call pkgAdd with (1)");
	}
	newton_catch_all
	{
		thrown = (long) (Long) CurrentException()->data;
	}
	end_try;
	EXPECT(thrown == kNSErrWrongNumberOfArgs);

	// nothing registered, no fallback: the error
	thrown = 0;
	newton_try
	{
		Eval("call pkgOther with (5)");
	}
	newton_catch_all
	{
		thrown = (long) (Long) CurrentException()->data;
	}
	end_try;
	EXPECT(thrown == kNSErrNativeNotReconstructed);

	// the fallback
	SetPackageNativeFallback(Fallback);
	r = Eval("call pkgOther with (5)");
	EXPECT(ISINT(r) && RINT(r) == 4 && sFallbackOffset == 24 && sFallbackArgs == 1);

	// the same code elsewhere, or a byte of it changed
	RefVar other(AllocateBinary(RSSYMbinary, 64));
	memset(BinaryData(other), 0xe5, 64);
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern("pkgCopy")), RefVar(MakeBinaryNative(other, 2, RefVar(), 8)));
	r = Eval("call pkgCopy with (1, 1)");
	EXPECT(ISINT(r) && RINT(r) == 2);
	((unsigned char*) BinaryData(other))[0] = 0;
	RefVar changed(AllocateBinary(RSSYMbinary, 64));
	memcpy(BinaryData(changed), BinaryData(other), 64);
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern("pkgChanged")), RefVar(MakeBinaryNative(changed, 1, RefVar(), 8)));
	sFallbackOffset = -1;
	r = Eval("call pkgChanged with (9)");
	EXPECT(ISINT(r) && RINT(r) == 8 && sFallbackOffset == 8);

	printf("test_PackageNatives: %s\n", failures == 0 ? "all passed" : "FAILED");
	return failures != 0;
}
