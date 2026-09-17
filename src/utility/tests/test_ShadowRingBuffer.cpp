// CShadowRingBuffer test (utility/RingBuffer.h): the ring buffer whose bytes
// live in a shared-memory object, so that every one of them goes through
// TUSharedMem::CopyToShared / CopyFromShared and what the buffer keeps are
// offsets into the block.  The shared-memory object is a real one, so the
// test runs as the kernel services task of a booted OS.
//
// Covered: the same fill/drain, full/empty and wrap behaviour as CRingBuffer,
// the bulk CopyIn/CopyOut across the wrap, the Compute*Vectors/Update*Vector
// runs, and the speculative read the class adds - TempGetn reads ahead
// without consuming, TempReset puts the position back.

#include "RingBuffer.h"
#include "UserSharedMem.h"
#include "SharedTypes.h"
#include "Boot.h"
#include "UserBoot.h"
#include "OSErrors.h"
#include "host/TaskRuntime.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


// A shared-memory object over storage, and a shadow ring buffer over it.
// The block is the buffer, so a block of eight bytes holds seven (one slot
// is kept free to tell full from empty).
//
// kSMemNoSizeChangeOnCopyTo matters here: without it a CopyToShared sets the
// block's size in use to what it just wrote (offset + size), and a ring
// buffer writes at a lower offset than the last one every time it wraps - so
// the block would shrink under the buffer and the reads past the new end
// would answer nothing.  The initial size is what a ring buffer wants.
struct Shadow
{
	UByte				fStorage[8];
	TUSharedMem			fMem;
	CShadowRingBuffer	fBuffer;

	NewtonErr Init(void)
	{
		memset(fStorage, 0, sizeof(fStorage));
		NewtonErr err = fMem.Init();
		if (err == noErr)
			err = fMem.SetBuffer(fStorage, sizeof(fStorage), kSMemReadWrite | kSMemNoSizeChangeOnCopyTo);
		if (err == noErr)
			fBuffer.Init(fMem.fId, 0, 0);
		return err;
	}
};


static void
TestFillAndDrain()
{
	Shadow s;
	EXPECT(s.Init() == noErr);
	CShadowRingBuffer& rb = s.fBuffer;

	EXPECT(rb.GetSize() == 7);
	EXPECT(rb.IsEmpty() && !rb.IsFull());
	EXPECT(rb.DataCount() == 0 && rb.FreeCount() == 7);
	EXPECT(rb.Get() == -1 && rb.Peek() == -1);

	for (int i = 1; i <= 7; i++)
		EXPECT(rb.Put(i) == i);
	EXPECT(rb.IsFull() && !rb.IsEmpty() && rb.AtEOF());
	EXPECT(rb.DataCount() == 7 && rb.FreeCount() == 0);
	EXPECT(rb.Put(99) == -1);				// full: refused
	EXPECT(s.fStorage[0] == 1 && s.fStorage[6] == 7);	// the bytes really went in

	EXPECT(rb.Peek() == 1 && rb.Peek() == 1);			// peek does not consume
	for (int i = 1; i <= 7; i++)
		EXPECT(rb.Get() == i);
	EXPECT(rb.IsEmpty() && rb.Get() == -1);
}


static void
TestWrapAndBulk()
{
	Shadow s;
	EXPECT(s.Init() == noErr);
	CShadowRingBuffer& rb = s.fBuffer;

	// move both offsets to 5, so that the free room is split by the wrap
	for (int i = 0; i < 5; i++)
		rb.Put(0xEE);
	for (int i = 0; i < 5; i++)
		rb.Get();
	EXPECT(rb.IsEmpty() && rb.FreeCount() == 7);

	UByte* p1; long n1; UByte* p2; long n2;
	rb.ComputePutVectors(p1, n1, p2, n2);
	EXPECT((ULong) p2 == 5 && n2 == 3);		// offset 5 to the end of the block
	EXPECT((ULong) p1 == 0 && n1 == 4);		// and offset 0 up to the slot before 5

	static const UByte source[7] = { 1, 2, 3, 4, 5, 6, 7 };
	long count = sizeof(source);
	EXPECT(rb.CopyIn(source, count) == -1);	// -1: the buffer is full now
	EXPECT(count == 0 && rb.DataCount() == 7 && rb.IsFull());
	EXPECT(s.fStorage[5] == 1 && s.fStorage[7] == 3 && s.fStorage[0] == 4);

	UByte out[7];
	count = sizeof(out);
	EXPECT(rb.CopyOut(out, count) == -1);	// -1: the buffer is empty now
	EXPECT(count == 0);
	EXPECT(memcmp(out, source, sizeof(source)) == 0);	// the wrap did not reorder them

	// the vector runs, without copying anything
	rb.Putn(source, 7);
	EXPECT(rb.DataCount() == 7);
	EXPECT(rb.UpdateGetVector(5) == 0);		// five consumed
	EXPECT(rb.DataCount() == 2);
	EXPECT(rb.UpdateGetVector(9) == 7);		// only two were there to consume
	EXPECT(rb.IsEmpty());
}


static void
TestTempRead()
{
	Shadow s;
	EXPECT(s.Init() == noErr);
	CShadowRingBuffer& rb = s.fBuffer;

	static const UByte source[6] = { 10, 20, 30, 40, 50, 60 };
	rb.Putn(source, sizeof(source));
	EXPECT(rb.DataCount() == 6 && rb.TempDataCount() == 6);

	// the speculative read moves only the temp position
	UByte ahead[3];
	EXPECT(rb.TempGetn(ahead, 3) == 3);
	EXPECT(ahead[0] == 10 && ahead[1] == 20 && ahead[2] == 30);
	EXPECT(rb.DataCount() == 6);			// nothing consumed
	EXPECT(rb.TempDataCount() == 3);
	EXPECT(rb.Peek() == 10);

	// reading on continues where the last one stopped
	EXPECT(rb.TempGetn(ahead, 3) == 3);
	EXPECT(ahead[0] == 40 && ahead[2] == 60);
	EXPECT(rb.TempDataCount() == 0);
	EXPECT(rb.TempGetn(ahead, 1) == 0);		// nothing left to read ahead

	// and TempReset puts it back to where the real reader is
	rb.TempReset();
	EXPECT(rb.TempDataCount() == 6);
	EXPECT(rb.TempGetn(ahead, 2) == 2 && ahead[0] == 10 && ahead[1] == 20);

	// consuming for real, then looking ahead from there
	EXPECT(rb.Getn(ahead, 2) == 2 && ahead[0] == 10 && ahead[1] == 20);
	rb.TempReset();
	EXPECT(rb.TempDataCount() == 4);
	EXPECT(rb.TempGetn(ahead, 1) == 1 && ahead[0] == 30);

	// Next and Skip walk the real read position
	EXPECT(rb.Next() == 40);				// consumed 30, peeks 40
	EXPECT(rb.Skip() == noErr);				// consumed 40
	EXPECT(rb.Peek() == 50 && rb.DataCount() == 2);

	rb.Reset();
	EXPECT(rb.IsEmpty() && rb.DataCount() == 0);
}


static void
ShadowScenario(void)
{
	TestFillAndDrain();
	TestWrapAndBulk();
	TestTempRead();
	HostStopTasks();
}


int
main()
{
	gHostKernelServicesTask = ShadowScenario;
	OsBoot();
	if (failures == 0)
		printf("test_ShadowRingBuffer: all passed\n");
	else
		printf("test_ShadowRingBuffer: %d failures\n", failures);
	return failures != 0;
}
