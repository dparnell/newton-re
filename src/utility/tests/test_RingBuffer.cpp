// CRingBuffer test (RingBuffer.h): a byte ring buffer over a fixed
// external buffer - fill and empty, the full/empty edges, wrap-around, the
// bulk CopyIn/CopyOut across the wrap, Peek/Next/Skip and GetnAt.  No OS
// boot: Init(buffer, ...) takes a caller's buffer, so no allocation.

#include "RingBuffer.h"
#include "OSErrors.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


static void
TestFillAndDrain()
{
	UByte storage[8];					// capacity 7 (one slot kept free)
	CRingBuffer rb;
	rb.Init(storage, sizeof(storage), false, 0, 0);

	EXPECT(rb.GetSize() == 7);
	EXPECT(rb.IsEmpty() && !rb.IsFull());
	EXPECT(rb.DataCount() == 0 && rb.FreeCount() == 7);
	EXPECT(rb.Get() == -1 && rb.Peek() == -1);

	for (int i = 1; i <= 7; i++)
		EXPECT(rb.Put(i) == i);
	EXPECT(rb.IsFull() && !rb.IsEmpty());
	EXPECT(rb.DataCount() == 7 && rb.FreeCount() == 0);
	EXPECT(rb.Put(99) == -1);			// full: refused

	EXPECT(rb.Peek() == 1);				// peek does not consume
	EXPECT(rb.Peek() == 1);
	for (int i = 1; i <= 7; i++)
		EXPECT(rb.Get() == i);
	EXPECT(rb.IsEmpty() && rb.Get() == -1);
}


static void
TestWrap()
{
	UByte storage[8];
	CRingBuffer rb;
	rb.Init(storage, sizeof(storage), false, 0, 0);

	// advance the pointers near the end, then wrap
	for (int i = 0; i < 5; i++)
		rb.Put(0xA0 + i);
	for (int i = 0; i < 5; i++)
		EXPECT(rb.Get() == 0xA0 + i);		// fGet now at 5
	// now put 6 bytes: 3 to the end, 3 wrapped to the front
	for (int i = 0; i < 6; i++)
		EXPECT(rb.Put(0xB0 + i) == 0xB0 + i);
	EXPECT(rb.DataCount() == 6);
	for (int i = 0; i < 6; i++)
		EXPECT(rb.Get() == 0xB0 + i);		// order preserved across the wrap
	EXPECT(rb.IsEmpty());
}


static void
TestBulk()
{
	UByte storage[8];
	CRingBuffer rb;
	rb.Init(storage, sizeof(storage), false, 0, 0);

	// Putn more than fits: only capacity bytes go in
	UByte in[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
	EXPECT(rb.Putn(in, 10) == 7);
	EXPECT(rb.IsFull());
	UByte out[10];
	memset(out, 0, sizeof(out));
	EXPECT(rb.Getn(out, 10) == 7);			// only 7 available
	EXPECT(memcmp(out, in, 7) == 0);
	EXPECT(rb.IsEmpty());

	// a bulk write/read that straddles the wrap
	rb.Reset();
	for (int i = 0; i < 5; i++) { rb.Put(i); rb.Get(); }	// move to offset 5
	UByte in2[6] = { 20, 21, 22, 23, 24, 25 };
	EXPECT(rb.Putn(in2, 6) == 6);			// 3 + 3 across the wrap
	UByte out2[6];
	EXPECT(rb.Getn(out2, 6) == 6);
	EXPECT(memcmp(out2, in2, 6) == 0);
}


static void
TestPeekNextSkipAt()
{
	UByte storage[8];
	CRingBuffer rb;
	rb.Init(storage, sizeof(storage), false, 0, 0);
	for (int i = 1; i <= 5; i++)
		rb.Put(i * 10);

	// GetnAt reads at an offset without consuming
	UByte peek[3];
	EXPECT(rb.GetnAt(1, peek, 3) == 3);
	EXPECT(peek[0] == 20 && peek[1] == 30 && peek[2] == 40);
	EXPECT(rb.DataCount() == 5);				// nothing consumed
	EXPECT(rb.Peek() == 10);

	// Next advances then returns the following byte; Skip advances only
	EXPECT(rb.Next() == 20);					// consumed 10, peeks 20
	EXPECT(rb.Skip() == noErr);					// consumed 20
	EXPECT(rb.Peek() == 30);
	EXPECT(rb.DataCount() == 3);

	rb.Reset();
	EXPECT(rb.IsEmpty() && rb.DataCount() == 0);
	EXPECT(rb.Skip() == -1);
}


int main()
{
	TestFillAndDrain();
	TestWrap();
	TestBulk();
	TestPeekNextSkipAt();
	if (failures == 0)
		printf("test_RingBuffer: all passed\n");
	else
		printf("test_RingBuffer: %d failures\n", failures);
	return failures != 0;
}
