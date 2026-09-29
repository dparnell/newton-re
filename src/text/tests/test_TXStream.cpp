// The text engine's byte streams: the position and the end of the
// stream, the handle stream over a TXArray, the binary stream over a
// NewtonScript binary (and the slack it keeps), the temporary stream
// factory, text moved through a stream by a TXTextDescriptor, and the
// chunked storage's chunk lengths written out and read back.
#include "TXStream.h"
#include "NSErrors.h"
#include "TXChars.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "OSErrors.h"
#include "NewtErrors.h"
#include "ByteOrder.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


static void
TestHandleStream()
{
	TXHandleStream stream;
	long size = -1;
	EXPECT(stream.GetSize(&size) == noErr && size == 0);
	EXPECT(stream.GetPosition() == 0);

	// written at the end, twice: the stream grows and the position follows
	EXPECT(stream.WriteBytes("Newton", 6) == noErr);
	EXPECT(stream.GetPosition() == 6);
	EXPECT(stream.WriteBytes(" MessagePad", 11) == noErr);
	EXPECT(stream.GetSize(&size) == noErr && size == 17);

	// read back from the beginning
	char buffer[32];
	stream.SetPosition(0);
	memset(buffer, 0, sizeof(buffer));
	EXPECT(stream.ReadBytes(buffer, 17) == noErr);
	EXPECT(strcmp(buffer, "Newton MessagePad") == 0);
	EXPECT(stream.GetPosition() == 17);

	// a read past the end gets what there is and says so
	stream.SetPosition(11);
	memset(buffer, 0, sizeof(buffer));
	EXPECT(stream.ReadBytes(buffer, 20) == kTXErrEndOfStream);
	EXPECT(strcmp(buffer, "agePad") == 0);
	EXPECT(stream.GetPosition() == 17);

	// a write inside what is there overwrites without lengthening it
	stream.SetPosition(0);
	EXPECT(stream.WriteBytes("NEWTON", 6) == noErr);
	EXPECT(stream.GetSize(&size) == noErr && size == 17);
	stream.SetPosition(0);
	memset(buffer, 0, sizeof(buffer));
	EXPECT(stream.ReadBytes(buffer, 17) == noErr);
	EXPECT(strcmp(buffer, "NEWTON MessagePad") == 0);

	// and one that starts inside and runs past the end does both
	stream.SetPosition(14);
	EXPECT(stream.WriteBytes("PAD 2100", 8) == noErr);
	EXPECT(stream.GetSize(&size) == noErr && size == 22);
	stream.SetPosition(0);
	memset(buffer, 0, sizeof(buffer));
	EXPECT(stream.ReadBytes(buffer, 22) == noErr);
	EXPECT(strcmp(buffer, "NEWTON MessagePAD 2100") == 0);
}


static void
TestBinaryStream()
{
	// a stream made to write into: the binary it is given is empty, and
	// the stream grows it by the slack it was asked for
	RefVar binary(AllocateBinary(RSSYMbinary, 0));
	{
		TXBinaryStream stream(binary, false, 0x40, true);
		long size = -1;
		EXPECT(stream.GetSize(&size) == noErr && size == 0);
		EXPECT(stream.WriteBytes("hello", 5) == noErr);
		EXPECT(stream.GetSize(&size) == noErr && size == 5);
		// grown to the slack plus what was wanted, not to what was wanted
		EXPECT(Length(binary) == 0x40 + 5);
		// the next few writes fit in the slack and do not grow it again
		EXPECT(stream.WriteBytes(", world", 7) == noErr);
		EXPECT(Length(binary) == 0x40 + 5);
		EXPECT(stream.GetSize(&size) == noErr && size == 12);

		// read it back
		char buffer[32];
		stream.SetPosition(0);
		memset(buffer, 0, sizeof(buffer));
		EXPECT(stream.ReadBytes(buffer, 12) == noErr);
		EXPECT(strcmp(buffer, "hello, world") == 0);
		// and past the end
		EXPECT(stream.ReadBytes(buffer, 4) == kTXErrEndOfStream);

		// a write inside what is there leaves the size alone
		stream.SetPosition(0);
		EXPECT(stream.WriteBytes("HELLO", 5) == noErr);
		EXPECT(stream.GetSize(&size) == noErr && size == 12);
	}
	// the destructor was asked to trim, so the slack has gone
	EXPECT(Length(binary) == 12);
	EXPECT(memcmp(BinaryData(binary), "HELLO, world", 12) == 0);

	// and a stream opened over a binary that already holds something
	// starts at its end, so what is written is added to it
	{
		TXBinaryStream stream(binary, true, 0, false);
		long size = -1;
		EXPECT(stream.GetSize(&size) == noErr && size == 12);
		stream.SetPosition(12);
		EXPECT(stream.WriteBytes("!", 1) == noErr);
		EXPECT(Length(binary) == 13);
	}
	// no trim was asked for, so it is left as it was grown
	EXPECT(Length(binary) == 13);
	EXPECT(memcmp(BinaryData(binary), "HELLO, world!", 13) == 0);
}


static void
TestFactory()
{
	EXPECT(TXGetTempStreamFactory() == nil);
	TXNewtStreamFactory factory;
	TXSetTempStreamFactory(&factory);
	EXPECT(TXGetTempStreamFactory() == &factory);

	// anything that will fit in the heap is a handle stream
	TXStream* stream = nil;
	EXPECT(factory.Create(&stream, 0x100) == noErr && stream != nil);
	EXPECT(stream->WriteBytes("x", 1) == noErr);
	delete stream;

	// a big one wants a large binary on the first store; with no store
	// mounted here the factory answers the error the attempt threw - the
	// first of no stores is out of bounds (the large-binary arm itself is
	// exercised by src/host/demo/txview.ns)
	stream = nil;
	EXPECT(factory.Create(&stream, 0x4000) == kNSErrOutOfBounds && stream == nil);

	TXSetTempStreamFactory(nil);
}


// A chunked storage whose chunks are plain heap blocks, small enough
// that a sentence is spread over several of them.
class TestChars : public TXChunkedChars
{
public:
			TestChars() : TXChunkedChars(8), fBlockCount(0)
			{
				memset(fBlocks, 0, sizeof(fBlocks));
			}
	virtual	~TestChars()
			{
				for (long i = 0; i < fBlockCount; i++)
					delete[] fBlocks[i];
			}

	virtual UniChar* GetChunkPtr(long chunk, Boolean, Boolean)	{ return fBlocks[chunk]; }
	virtual NewtonErr AllocateChunks(long at, long count)
			{
				for (long i = fBlockCount - 1; i >= at; i--)
					fBlocks[i + count] = fBlocks[i];
				for (long i = 0; i < count; i++)
					fBlocks[at + i] = new UniChar[fChunkSize];
				fBlockCount += count;
				return noErr;
			}
	virtual void RemoveChunks(long at, long count)
			{
				for (long i = 0; i < count; i++)
					delete[] fBlocks[at + i];
				for (long i = at; i + count < fBlockCount; i++)
					fBlocks[i] = fBlocks[i + count];
				for (long i = fBlockCount - count; i < fBlockCount; i++)
					fBlocks[i] = nil;
				fBlockCount -= count;
			}

	UniChar*	fBlocks[64];
	long		fBlockCount;
};


static void
SetText(TXTextDescriptor& source, UniChar* buffer, const char* text)
{
	long n = (long) strlen(text);
	for (long i = 0; i < n; i++)
		buffer[i] = (UniChar) text[i];
	source.Set(buffer, n);
}


static Boolean
CharsAre(TXChars& chars, const char* text)
{
	long n = (long) strlen(text);
	if (chars.Count() != n)
		return false;
	for (long i = 0; i < n; i++)
		if (chars.GetChar(i) != (UniChar) text[i])
			return false;
	return true;
}


static void
TestTextThroughAStream()
{
	const char* kText = "The Newton MessagePad 2100, a machine of 1997.";
	long length = (long) strlen(kText);

	// a descriptor over a buffer poured into a stream...
	TXHandleStream stream;
	UniChar buffer[64];
	TXTextDescriptor source;
	SetText(source, buffer, kText);
	TXTextDescriptor sink;
	sink.Set(&stream, length);
	EXPECT(source.CopyTo(&sink, length) == noErr);

	long size = -1;
	EXPECT(stream.GetSize(&size) == noErr && size == length * (long) sizeof(UniChar));
	// the characters in it are halfwords, most significant byte first,
	// whatever the host's own order is
	unsigned char first[2];
	stream.SetPosition(0);
	EXPECT(stream.ReadBytes(first, 2) == noErr);
	EXPECT(GetBigEndianHalf(first) == (unsigned short) 'T');

	// ...and read back out of it into a chunked storage
	TestChars chars;
	stream.SetPosition(0);
	TXTextDescriptor back;
	back.Set(&stream, length);
	TXTextDescriptor into;
	into.Set((TXChars*) &chars, 0, length);
	EXPECT(back.CopyTo(&into, length) == noErr);
	EXPECT(CharsAre(chars, kText));

	// the chunk lengths written out: a count, the chunks that are not
	// full, and a nought
	TXHandleStream ranges;
	EXPECT(chars.WriteChunksRanges(&ranges) == noErr);
	long chunkCount = chars.fChunks->GetCount();
	EXPECT(chunkCount > 1);
	ranges.SetPosition(0);
	unsigned char half[2];
	EXPECT(ranges.ReadBytes(half, 2) == noErr && (long) GetBigEndianHalf(half) == chunkCount);

	// and read back into a storage that has been given the same chunks
	TestChars other;
	EXPECT(other.AllocateChunks(0, chunkCount) == noErr);
	ranges.SetPosition(0);
	EXPECT(other.ReadChunksRanges(&ranges) == noErr);
	EXPECT(other.fChunks->GetCount() == chunkCount);
	EXPECT(other.Count() == chars.Count());
	for (long i = 0; i < chunkCount; i++)
		EXPECT(other.fChunks->GetRangeEnd(i) == chars.fChunks->GetRangeEnd(i));
}


int
main()
{
	InitHostStandaloneHeap();
	gObjectHeapSize = 0x80000;
	InitObjects();
	TestHandleStream();
	TestBinaryStream();
	TestFactory();
	TestTextThroughAStream();
	printf("test_TXStream: %s\n", failures == 0 ? "ok" : "FAILED");
	return failures == 0 ? 0 : 1;
}
