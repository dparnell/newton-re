// Host unit test for the buffers and pipes (src/utility/BufferSegment.h,
// Pipes.h): CBufferSegment over its own and a given block (get and put,
// bulk copies, seeking, hiding), and CBufferPipe reading and writing
// through a concrete pipe whose Overflow grows the write segment and
// whose Underflow reports the end (CTestPipe, TestPipe.h), with the
// big-endian scalar operators and the eof reporting of ReadChunk.

#include "TestPipe.h"
#include "NewtonExceptions.h"
#include "UCErrors.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

extern const ExceptionName exPipeException;




static void
TestSegment()
{
	CBufferSegment seg;
	EXPECT(seg.Init(8) == noErr);
	EXPECT(seg.GetSize() == 8 && seg.Position() == 0 && !seg.AtEOF() && seg.GetPhysicalSize() == 8);
	for (int i = 0; i < 8; i++)
		EXPECT(seg.Put(i * 10) == i * 10);
	EXPECT(seg.Put(99) == -1 && seg.AtEOF());
	seg.Seek(0, kSeekFromBeginning);
	EXPECT(seg.Peek() == 0 && seg.Get() == 0 && seg.Peek() == 10);
	EXPECT(seg.Next() == 20);				// moves on, then answers the byte there
	EXPECT(seg.Skip() != -1 && seg.Get() == 30);
	UByte out[8];
	EXPECT(seg.Getn(out, 8) == 4 && out[0] == 40 && out[3] == 70);	// only four left
	EXPECT(seg.Get() == -1 && seg.Next() == -1 && seg.Skip() == -1);
	// seeking
	EXPECT(seg.Seek(2, kSeekFromBeginning) == 2 && seg.Peek() == 20);
	EXPECT(seg.Seek(3, kSeekFromHere) == 5 && seg.Peek() == 50);
	EXPECT(seg.Seek(1, kSeekFromEnd) == 7 && seg.Peek() == 70);
	EXPECT(seg.Seek(100, kSeekFromBeginning) == 8 && seg.AtEOF());
	EXPECT(seg.Seek(-100, kSeekFromHere) == 0);
	// bulk copies
	seg.Reset();
	UByte in[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
	Size n = 10;
	EXPECT(seg.CopyIn(in, n) == -1 && n == 2);		// full, two left over
	seg.Seek(0, kSeekFromBeginning);
	UByte got[10];
	n = 5;
	EXPECT(seg.CopyOut(got, n) == 0 && n == 0 && got[4] == 5);
	n = 5;
	EXPECT(seg.CopyOut(got, n) == -1 && n == 2 && got[0] == 6);	// exhausted: two not copied
	n = 5;
	EXPECT(seg.CopyOut(got, n) == -1 && n == 5);
	// hiding
	seg.Reset();
	EXPECT(seg.Hide(2, kSeekFromBeginning) == 2 && seg.GetSize() == 6 && seg.Peek() == 3 && seg.Position() == 0);
	EXPECT(seg.Hide(3, kSeekFromEnd) == 3 && seg.GetSize() == 3 && seg.AtEOF());
	EXPECT(seg.Hide(-2, kSeekFromBeginning) == -2 && seg.GetSize() == 5 && seg.Peek() == 1);
	EXPECT(seg.Hide(-10, kSeekFromEnd) == -3 && seg.GetSize() == 8);	// clipped to the block
	EXPECT(seg.Hide(0, kSeekFromEnd) == 0);
	// resized: the position kept
	seg.Seek(3, kSeekFromBeginning);
	EXPECT(seg.SetPhysicalSize(16) == noErr && seg.GetPhysicalSize() == 16 && seg.GetSize() == 16 && seg.Position() == 3 && seg.Peek() == 4);
	// over a given block: the valid range
	static UByte block[12] = { 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l' };
	CBufferSegment given;
	EXPECT(given.Init(block, 12, false, 2, 5) == noErr);
	EXPECT(given.GetSize() == 5 && given.Get() == 'c' && given.Seek(0, kSeekFromEnd) == 5 && given.AtEOF());
	given.Reset();
	EXPECT(given.GetSize() == 12 && given.Get() == 'a');
	EXPECT(given.Init(block, 12, false, 10, 100) == noErr && given.GetSize() == 2);	// a count past the end: to the end
}


static void
TestPipe()
{
	CTestPipe pipe(8);
	EXPECT(pipe.WritePosition() == 0);
	pipe << (long) 0x01020304 << (unsigned short) 0xabcd << (char) 'x' << (unsigned char) 0xff;
	EXPECT(pipe.WritePosition() == 8 && pipe.fOverflows == 0);
	pipe << (long) -2 << (short) -3;				// past the segment: grown
	EXPECT(pipe.WritePosition() == 14 && pipe.fOverflows >= 1);
	static const char text[] = "hello, pipes";
	pipe.WriteChunk(text, 12, true);
	EXPECT(pipe.WritePosition() == 26);
	// the bytes are big-endian
	UByte* bytes = pipe.fWriteBuffer->fBuffer;
	EXPECT(bytes[0] == 1 && bytes[3] == 4 && bytes[4] == 0xab && bytes[5] == 0xcd && bytes[6] == 'x' && bytes[7] == 0xff);
	EXPECT(bytes[8] == 0xff && bytes[11] == 0xfe && bytes[12] == 0xff && bytes[13] == 0xfd);

	pipe.Rewind();
	long l;
	unsigned short us;
	char c;
	unsigned char uc;
	short s;
	pipe >> l >> us >> c >> uc;
	EXPECT(l == 0x01020304 && us == 0xabcd && c == 'x' && uc == 0xff);
	pipe >> l >> s;
	EXPECT(l == -2 && s == -3 && pipe.ReadPosition() == 14);
	char in[32];
	long count = 12;
	Boolean eof = false;
	pipe.ReadChunk(in, count, eof);
	EXPECT(count == 12 && memcmp(in, text, 12) == 0);
	EXPECT(!eof && pipe.fUnderflows == 0);
	// the end: Underflow asked, nothing more, eof reported with what there was
	count = 4;
	pipe.ReadChunk(in, count, eof);
	EXPECT(count == 0 && eof && pipe.fUnderflows == 1);
	pipe.Rewind();
	count = 30;
	pipe.ReadChunk(in, count, eof);				// more than there is
	EXPECT(count == 26 && eof);
	// seeking
	EXPECT(pipe.ReadSeek(4, kSeekFromBeginningPos) == 4);
	pipe >> us;
	EXPECT(us == 0xabcd);
	EXPECT(pipe.ReadSeek(-2, kSeekFromCurrentPos) == 4);
	// a pipe without a read segment
	CTestPipe writeOnly(4);
	Boolean threw = false;
	newton_try
	{
		writeOnly >> l;
	}
	newton_catch(exPipeException)
	{
		threw = (long) (Long) _info.exception.data == eNotInitialized;
	}
	end_try;
	EXPECT(threw);
	// the peek/get shortcuts on the read segment
	pipe.Rewind();
	EXPECT(pipe.Peek(false) == 1 && pipe.Get() == 1 && pipe.Next() == 3 && pipe.Skip() != -1 && pipe.Get() == 4);
	pipe.Reset();
	EXPECT(pipe.ReadPosition() == 0 && pipe.WritePosition() == 0);
}


int
main()
{
	InitHostStandaloneHeap();
	TestSegment();
	TestPipe();
	if (failures == 0)
		printf("test_Pipes: all passed\n");
	return failures != 0;
}
