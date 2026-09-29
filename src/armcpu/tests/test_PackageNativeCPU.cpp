/*
	File:		armcpu/tests/test_PackageNativeCPU.cpp

	Contains:	The package-native fallback (armcpu/PackageNativeCPU.h) run
				over hand-assembled code binaries, from NewtonScript through
				the interpreter's native entry, outside the OS: objects in
				the code binary (a symbol, a string, an array) come back as
				host objects; arguments arrive by reference; a call out
				through the public jump table (Length) is answered on the
				host; an exception thrown out of a host function reaches the
				ARM code's handler with its data, and a ref the ARM code
				throws reaches a NewtonScript handler as the ref; a native
				of another code binary is called from ARM code.
*/

#include "PackageNativeCPU.h"
#include "PackageNatives.h"
#include "Interpreter.h"
#include "Compiler.h"
#include "ObjectHeap.h"
#include "Frames.h"
#include "RSSymbols.h"
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

// a 0x232 function object over the code binary
static Ref
MakeBinaryNative(RefArg code, long numArgs, long offset)
{
	RefVar fn(MakeArray(5));
	SetArraySlotRef(fn, 0, kBinaryNativeFuncClass);
	SetArraySlot(fn, 1, code);
	SetArraySlotRef(fn, 2, MAKEINT(numArgs));
	SetArraySlotRef(fn, 3, NILREF);
	SetArraySlotRef(fn, 4, MAKEINT(offset));
	return fn;
}

static void
Put(RefArg code, unsigned long at, unsigned long word)
{
	unsigned char* p = (unsigned char*) BinaryData(code) + at;
	p[0] = (unsigned char) (word >> 24);
	p[1] = (unsigned char) (word >> 16);
	p[2] = (unsigned char) (word >> 8);
	p[3] = (unsigned char) word;
}

int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_ROM_IMAGE) != noErr)
	{
		printf("test_PackageNativeCPU: cannot import %s\n", NEWTON_ROM_IMAGE);
		return 1;
	}
	gObjectHeapSize = 0x100000;
	InitObjects();
	InstallPackageNativeCPU();

	RefVar code(AllocateBinary(RSSYMbinary, 0x280));
	memset(BinaryData(code), 0, 0x280);
	// +0x00: return the string object at +0x40 (a pointer ref: its address + 1)
	Put(code, 0x00, 0xe28f0039);		// add r0,pc,#0x39    (0x08 + 0x39 = 0x41)
	Put(code, 0x04, 0xe1a0f00e);		// mov pc,lr
	// +0x08: return the array at +0x60
	Put(code, 0x08, 0xe28f0051);		// add r0,pc,#0x51    (0x10 + 0x51 = 0x61)
	Put(code, 0x0c, 0xe1a0f00e);
	// +0x10: the first argument's Length, through the public jump table:
	// stmfd sp!,{lr}; ldr r0,[r1]; ldr r0,[r0]; bl stub; mov r0,r0,lsl #2
	// (MAKEINT); ldmfd sp!,{pc}
	Put(code, 0x10, 0xe92d4000);
	Put(code, 0x14, 0xe5910000);		// ldr r0,[r1]       (the RefVar's handle)
	Put(code, 0x18, 0xe5900000);		// ldr r0,[r0]       (its ref)
	Put(code, 0x1c, 0xeb000001);		// bl 0x28 (the stub)
	Put(code, 0x20, 0xe1a00100);		// mov r0,r0,lsl #2
	Put(code, 0x24, 0xe8bd8000);		// ldmfd sp!,{pc}
	Put(code, 0x28, 0xe51ff004);		// the stub: ldr pc,[pc,#-4]
	Put(code, 0x2c, 0x01800978);		//   Length__Fl (public jump table +0x978)
	// (a ref inside an object in the binary is its ARM address: the binary
	//  is at 0x20000000 in the ARM world)
	// +0x40: a string, "hi": header (18 bytes, flags 0x40), the GC's word,
	// its class (the symbol 'string at +0x80), big-endian UniChars
	Put(code, 0x40, 0x00001240);
	Put(code, 0x48, 0x20000081);
	Put(code, 0x4c, 0x00680069);
	Put(code, 0x50, 0x00000000);
	// +0x60: an array [1, 2, 'string]: header (24 bytes, slotted), class nil
	Put(code, 0x60, 0x00001841);
	Put(code, 0x68, 0x00000002);
	Put(code, 0x6c, 0x00000004);
	Put(code, 0x70, 0x00000008);
	Put(code, 0x74, 0x20000081);
	// +0x80: the symbol 'string: header, the GC's word, the symbol class,
	// the hash, the name
	Put(code, 0x80, 0x00001740);
	Put(code, 0x88, 0x00055552);
	memcpy((char*) BinaryData(code) + 0x90, "string", 7);

	// +0xc0: catch what GetArraySlotRef throws and answer its data - a ref
	// exception's data comes into the ARM world as a RefVar
	Put(code, 0xc0, 0xe92d4010);		// stmfd sp!,{r4,lr}
	Put(code, 0xc4, 0xe24dd070);		// sub sp,sp,#0x70     (an ExceptionHandler at sp)
	Put(code, 0xc8, 0xe1a04001);		// mov r4,r1           (the argument's RefVar)
	Put(code, 0xcc, 0xe28d0008);		// add r0,sp,#8        (its jmp_buf)
	Put(code, 0xd0, 0xeb00001a);		// bl setjmp
	Put(code, 0xd4, 0xe3500000);		// cmp r0,#0
	Put(code, 0xd8, 0x1a000007);		// bne caught
	Put(code, 0xdc, 0xe1a0000d);		// mov r0,sp
	Put(code, 0xe0, 0xeb000018);		// bl AddExceptionHandler
	Put(code, 0xe4, 0xe5940000);		// ldr r0,[r4]
	Put(code, 0xe8, 0xe5900000);		// ldr r0,[r0]
	Put(code, 0xec, 0xe3a01000);		// mov r1,#0
	Put(code, 0xf0, 0xeb000016);		// bl GetArraySlotRef  (throws on a non-array)
	Put(code, 0xf4, 0xe3a00002);		// mov r0,#2 (nil: not reached)
	Put(code, 0xf8, 0xea000002);		// b done
	Put(code, 0xfc, 0xe59d0064);		// caught: ldr r0,[sp,#0x64]  (the Exception's data: a RefVar)
	Put(code, 0x100, 0xe5900000);		// ldr r0,[r0]        (a RefStruct: its RefHandle)
	Put(code, 0x104, 0xe5900000);		// ldr r0,[r0]        (the ref)
	Put(code, 0x108, 0xe28dd070);		// done: add sp,sp,#0x70
	Put(code, 0x10c, 0xe8bd8010);		// ldmfd sp!,{r4,pc}
	Put(code, 0x140, 0xe51ff004);		// the stubs
	Put(code, 0x144, 0x01800040);		// setjmp
	Put(code, 0x148, 0xe51ff004);
	Put(code, 0x14c, 0x01801748);		// AddExceptionHandler
	Put(code, 0x150, 0xe51ff004);
	Put(code, 0x154, 0x01800920);		// GetArraySlotRef__FlT1

	// +0x180: throw a ref exception with the argument as its data, for the
	// host to catch
	Put(code, 0x180, 0xe1a00001);		// mov r1,r1 (the argument's RefVar)
	Put(code, 0x184, 0xe28f0034);		// add r0,pc,#0x34     (the name at +0x1c0)
	Put(code, 0x188, 0xea000006);		// b ThrowRefException (the stub at +0x1a8)
	Put(code, 0x1a8, 0xe51ff004);
	Put(code, 0x1ac, 0x01800b28);		// ThrowRefException__FPcRC6RefVar
	memcpy((char*) BinaryData(code) + 0x1c0, "evt.ex.fr.intrp;type.ref.frame", 31);

	// +0x1e0: call the function object in the first argument (a native
	// of another code binary) with the second, through NativeEntry
	Put(code, 0x1e0, 0xe92d4030);		// stmfd sp!,{r4,r5,lr}
	Put(code, 0x1e4, 0xe1a04002);		// mov r4,r2           (the second argument's RefVar)
	Put(code, 0x1e8, 0xe1a00001);		// mov r0,r1           (the first: the function)
	Put(code, 0x1ec, 0xe3a01001);		// mov r1,#1           (one argument)
	Put(code, 0x1f0, 0xe3a02000);		// mov r2,#0           (no closure asked for)
	Put(code, 0x1f4, 0xeb000005);		// bl NativeEntry
	Put(code, 0x1f8, 0xe1a05000);		// mov r5,r0
	Put(code, 0x1fc, 0xe3a00000);		// mov r0,#0           (no receiver)
	Put(code, 0x200, 0xe1a01004);		// mov r1,r4
	Put(code, 0x204, 0xe1a0e00f);		// mov lr,pc
	Put(code, 0x208, 0xe1a0f005);		// mov pc,r5
	Put(code, 0x20c, 0xe8bd8030);		// ldmfd sp!,{r4,r5,pc}
	Put(code, 0x210, 0xe51ff004);
	Put(code, 0x214, 0x01802750);		// NativeEntry__FRC6RefVarlPP9RefHandle
	// another code binary: +0x00 answers its argument plus one
	RefVar other(AllocateBinary(RSSYMbinary, 0x10));
	Put(other, 0x00, 0xe5910000);		// ldr r0,[r1]
	Put(other, 0x04, 0xe5900000);		// ldr r0,[r0]
	Put(other, 0x08, 0xe2800004);		// add r0,r0,#4       (an integer ref plus one)
	Put(other, 0x0c, 0xe1a0f00e);		// mov pc,lr

	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "nativeString")), RefVar(MakeBinaryNative(code, 0, 0x00)));
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "nativeArray")), RefVar(MakeBinaryNative(code, 0, 0x08)));
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "nativeLength")), RefVar(MakeBinaryNative(code, 1, 0x10)));
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "nativeCatch")), RefVar(MakeBinaryNative(code, 1, 0xc0)));
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "nativeThrow")), RefVar(MakeBinaryNative(code, 1, 0x180)));
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "nativeCallOther")), RefVar(MakeBinaryNative(code, 2, 0x1e0)));
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "otherPlusOne")), RefVar(MakeBinaryNative(other, 1, 0x00)));

	// a string in the code binary: its UniChars in the host's order, of class 'string
	RefVar s(Eval("call nativeString with ()"));
	EXPECT(IsString(s));
	EXPECT(Length(s) == 6 && ((UniChar*) BinaryData(s))[0] == 'h' && ((UniChar*) BinaryData(s))[1] == 'i');
	// an array: its slots translated, the symbol interned
	RefVar a(Eval("call nativeArray with ()"));
	EXPECT(IsArray(a) && Length(a) == 3);
	EXPECT(RINT(GetArraySlotRef(a, 0)) == 1 && RINT(GetArraySlotRef(a, 1)) == 2);
	EXPECT(EQRef(GetArraySlotRef(a, 2), RSSYMstring));
	// the same object each time within a call; a new one each call
	EXPECT(!EQRef(a, Eval("call nativeArray with ()")));
	// an argument by reference, and a call out: Length([1, 2, 3, 4, 5])
	EXPECT(RINT(Eval("call nativeLength with ([1, 2, 3, 4, 5])")) == 5);
	EXPECT(RINT(Eval("call nativeLength with (\"abc\")")) == 8);		// (bytes: three UniChars and the nul)
	// a host exception's data: the frame of the bad value, {errorCode, value}
	RefVar caught(Eval("call nativeCatch with (42)"));
	EXPECT(IsFrame(caught));
	if (IsFrame(caught))
	{
		EXPECT(RINT(GetFrameSlot(caught, RefVar(Intern((char*) "errorCode")))) == kNSErrObjectPointerOfNonPtr);		// (42 is no pointer)
		EXPECT(RINT(GetFrameSlot(caught, RefVar(Intern((char*) "value")))) == 42);
	}
	// an ARM throw's data out to the host: the ref comes back as the ref
	RefVar thrown(Eval("try call nativeThrow with ({a: 7}) onexception |evt.ex| do CurrentException().data.a"));
	EXPECT(RINT(thrown) == 7);
	// a native of another code binary, called from ARM code
	EXPECT(RINT(Eval("call nativeCallOther with (otherPlusOne, 41)")) == 42);
	EXPECT(PackageNativeCPUAnswers("Length__Fl") && PackageNativeCPUEntryCount() > 50);

	if (failures == 0)
		printf("test_PackageNativeCPU: all passed\n");
	else
		printf("test_PackageNativeCPU: %d failures\n", failures);
	return failures != 0;
}
