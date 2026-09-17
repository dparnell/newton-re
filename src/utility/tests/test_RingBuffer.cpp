// CRingBuffer and CRingPipe test (RingBuffer.h): a byte ring buffer over a
// fixed external buffer - fill and empty, the full/empty edges, wrap-around,
// the bulk CopyIn/CopyOut across the wrap, Peek/Next/Skip and GetnAt - the
// CopyIn that pulls straight from a CPipe, and the pipe over a ring buffer,
// whose Underflow refills it and whose Overflow drains it.  No OS boot: a
// host heap is enough for the allocating parts.

#include "RingBuffer.h"
#include "TestPipe.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"
#include "UCErrors.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

extern const ExceptionName exPipeException;

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



/*------------------------------------------------------------------------------
	C R i n g B u f f e r : : C o p y I n ( C P i p e * )
------------------------------------------------------------------------------*/

// The pipe reads its chunks into the buffer's own free runs, so a fill that
// crosses the wrap is two ReadChunk calls and no intermediate copy.
static void
TestCopyInFromPipe()
{
	UByte storage[8];					// capacity 7
	CRingBuffer rb;
	rb.Init(storage, sizeof(storage), false, 0, 0);
	for (int i = 0; i < 5; i++)			// move both pointers to offset 5
		rb.Put(0xEE);
	for (int i = 0; i < 5; i++)
		rb.Get();
	EXPECT(rb.IsEmpty() && rb.FreeCount() == 7);

	UByte source[7] = { 1, 2, 3, 4, 5, 6, 7 };
	CTestPipe pipe(8);
	pipe.WriteChunk(source, sizeof(source), false);
	pipe.Rewind();

	long count = sizeof(source);
	EXPECT(rb.CopyIn(&pipe, count) == -1);		// -1: the buffer is full now
	EXPECT(count == 0);
	EXPECT(rb.DataCount() == 7 && rb.IsFull());

	UByte out[7];
	EXPECT(rb.Getn(out, 7) == 7);
	EXPECT(memcmp(out, source, sizeof(source)) == 0);	// the wrap did not reorder them
}


/*------------------------------------------------------------------------------
	C R i n g P i p e
------------------------------------------------------------------------------*/

// A ring pipe with somewhere for the bytes to come from and go to: Underflow
// tops the ring up from a source array and says when the source's last bytes
// have gone in; Overflow (and FlushWrite) empty the ring into a sink.  A read
// past the end of the source throws, as a real subclass's does - the ROM's
// ReadChunk loops until the count is met, so an Underflow that supplies
// nothing and merely reports the end would spin for ever.
class CTestRingPipe : public CRingPipe
{
public:
					CTestRingPipe(long size)
						: fSource(nil), fSourceLeft(0), fSinkLen(0), fOverflows(0), fUnderflows(0), fFlushes(0)
						{ Init(size); }

	virtual void	FlushRead(void)		{ }
	virtual void	FlushWrite(void)	{ fFlushes++; Drain(); }

	virtual void	Overflow(void)		{ fOverflows++; Drain(); }

	virtual void	Underflow(long count, Boolean& eof)
	{
		fUnderflows++;
		if (fSourceLeft == 0)
			Throw(exPipeException, (void*) (Long) eNoMemory, nil);
		long room = fBuffer->FreeCount();
		long n = (fSourceLeft < room) ? fSourceLeft : room;
		fBuffer->Putn(fSource, n);
		fSource += n;
		fSourceLeft -= n;
		if (fSourceLeft == 0)
			eof = true;
	}

	void			SetSource(const UByte* data, long len)	{ fSource = data; fSourceLeft = len; }
	void			Drain(void)
	{
		long n = fBuffer->DataCount();
		if (n > 0)
			fSinkLen += fBuffer->Getn(fSink + fSinkLen, n);
	}

	const UByte*	fSource;
	long			fSourceLeft;
	UByte			fSink[64];
	long			fSinkLen;
	long			fOverflows;
	long			fUnderflows;
	long			fFlushes;
};


static void
TestRingPipeRead()
{
	static const UByte source[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
	CTestRingPipe pipe(4);					// a ring that holds four bytes
	pipe.SetSource(source, sizeof(source));

	UByte out[10];
	long count = sizeof(out);
	Boolean eof = false;
	pipe.ReadChunk(out, count, eof);
	EXPECT(count == 10);
	EXPECT(memcmp(out, source, sizeof(source)) == 0);
	EXPECT(eof);							// the source ran out on the last refill
	EXPECT(pipe.fUnderflows == 3);			// 4 + 4 + 2 bytes

	// the flag is answered once and then forgotten: given more source, a read
	// that leaves some of it reports no end
	static const UByte more[8] = { 11, 12, 13, 14, 15, 16, 17, 18 };
	pipe.SetSource(more, sizeof(more));
	count = 2;
	pipe.ReadChunk(out, count, eof);
	EXPECT(count == 2 && out[0] == 11 && out[1] == 12);
	EXPECT(!eof);							// four bytes of the source are still to come

	count = 6;
	pipe.ReadChunk(out, count, eof);
	EXPECT(count == 6 && out[0] == 13 && out[5] == 18);
	EXPECT(eof);

	// and a read past the end of the source is the subclass's to refuse
	Boolean threw = false;
	newton_try
	{
		count = 1;
		pipe.ReadChunk(out, count, eof);
	}
	newton_catch(exPipeException)
	{
		threw = (NewtonErr) (Long) CurrentException()->data == eNoMemory;
	}
	end_try;
	EXPECT(threw);
}


static void
TestRingPipeWrite()
{
	static const UByte source[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
	CTestRingPipe pipe(4);

	pipe.WriteChunk(source, sizeof(source), true);
	EXPECT(pipe.fOverflows == 2);			// the ring filled twice on the way
	EXPECT(pipe.fFlushes == 1);
	EXPECT(pipe.fSinkLen == 10);
	EXPECT(memcmp(pipe.fSink, source, sizeof(source)) == 0);

	// the pipe does not seek, and Reset empties the ring
	EXPECT(pipe.ReadPosition() == 0 && pipe.WritePosition() == 0);
	EXPECT(pipe.ReadSeek(4, kSeekFromBeginning) == 0 && pipe.WriteSeek(4, kSeekFromBeginning) == 0);
	pipe.WriteChunk(source, 2, false);
	EXPECT(pipe.fBuffer->DataCount() == 2);
	pipe.Reset();
	EXPECT(pipe.fBuffer->DataCount() == 0);
}


int main()
{
	InitHostStandaloneHeap();
	TestFillAndDrain();
	TestWrap();
	TestBulk();
	TestPeekNextSkipAt();
	TestCopyInFromPipe();
	TestRingPipeRead();
	TestRingPipeWrite();
	if (failures == 0)
		printf("test_RingBuffer: all passed\n");
	else
		printf("test_RingBuffer: %d failures\n", failures);
	return failures != 0;
}
