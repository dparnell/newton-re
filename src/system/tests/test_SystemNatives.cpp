// The machine's own NewtonScript functions: GetHeapStats, which walks the
// pointer and handle heaps a block at a time and asks the object heap for
// its free space.  Runs over a standalone kernel heap and object heap
// without ROM objects.  (The serial number's native has no name in the
// ROM's table - a script reaches it through its function object - so it
// is not called from here.)
#include "SystemNatives.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Compiler.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "memory/host/KernelHeap.h"

#include "NewtonExceptions.h"
#include "REPTranslators.h"

#include <stdio.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


static Ref
Eval(const char* source)
{
	RefVar fn(ParseString(RefVar(MakeString(source))));
	return InterpretBlock(fn, RefVar(gVarFrame));
}


static long
Slot(RefArg stats, const char* name)
{
	RefVar value(GetFrameSlotRef(stats, RefVar(Intern((char*) name))));
	EXPECT(ISINT(value));
	return ISINT(value) ? RINT(value) : -1;
}


int
main()
{
	InitHostStandaloneHeap();
	gObjectHeapSize = 0x100000;
	InitObjects();
	RegisterSystemNatives();
	InstallHostNatives();

	newton_try
	{
	RefVar stats(Eval("GetHeapStats(nil)"));
	EXPECT(IsFrame(stats));

	// the three heaps: each has a start, a size and free space within it
	EXPECT(Slot(stats, "ptrHeapStart") != 0);
	EXPECT(Slot(stats, "ptrHeapSize") > 0);
	EXPECT(Slot(stats, "ptrFreeSize") >= 0 && Slot(stats, "ptrFreeSize") <= Slot(stats, "ptrHeapSize"));
	EXPECT(Slot(stats, "handleHeapSize") > 0);
	EXPECT(Slot(stats, "handleFreeSize") >= 0);
	EXPECT(Slot(stats, "framesHeapStart") != 0);
	EXPECT(Slot(stats, "framesHeapSize") > 0 && Slot(stats, "framesHeapSize") <= gObjectHeapSize);
	EXPECT(Slot(stats, "framesFreeSize") > 0 && Slot(stats, "framesFreeSize") <= Slot(stats, "framesHeapSize"));

	// the machine has memory left over besides its heaps - which is the
	// point of the host's TotalSystemFree (MemoryManager.cpp): answering
	// nought told everything that asked that it had none
	EXPECT(Slot(stats, "systemFreeSize") > 0);

	// a collection first: the object heap cannot come back with less free
	// than it had, and a heapful of rubbish comes back
	long wasFree = Slot(stats, "framesFreeSize");
	Eval("local junk := nil; for i := 1 to 400 do junk := {a: i, b: [i, i, i], c: \"some text\"}");
	RefVar collected(Eval("GetHeapStats({garbageCollectFrames: true})"));
	EXPECT(IsFrame(collected));
	long nowFree = Slot(collected, "framesFreeSize");
	EXPECT(nowFree > wasFree - 0x20000);

	}
	newton_catch_all
	{
		failures++;
		fprintf(stderr, "FAIL: unhandled exception %s\n", _info.exception.name);
		HostInitREP(stderr, nil);
		PrintObject(*(RefStruct*) _info.exception.data, 0);
		fprintf(stderr, "\n");
	}
	end_try;

	printf("test_SystemNatives: %s\n", failures == 0 ? "ok" : "FAILED");
	return failures == 0 ? 0 : 1;
}
