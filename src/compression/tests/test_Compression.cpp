// Compression test: the OS is booted so that the protocol registry exists;
// in the kernel services task InitializeCompression registers the ROM's
// compressors, and each is driven through its interface: round trips of
// texts, runs, random bytes (stored blocks) and multi-block inputs, the
// compressed format's framing, and the callback compressor's blocks.

#include "Compression.h"
#include "LZCompression.h"
#include "Boot.h"
#include "KernelGlobals.h"
#include "UserBoot.h"
#include "NewtonMemory.h"
#include "OSErrors.h"
#include "host/TaskRuntime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const char* kText =
	"The Newton MessagePad is a series of personal digital assistant devices developed by Apple. "
	"The MessagePad was the first series of PDAs; it introduced handwriting recognition, the Newton "
	"operating system and the NewtonScript language. The MessagePad 2100 was the last of the series. "
	"The Newton MessagePad is a series of personal digital assistant devices developed by Apple. ";


// a byte pattern: runs, text and noise, seeded so that it is repeatable
static void
Fill(UByte* p, ULong size, unsigned seed)
{
	srand(seed);
	ULong i = 0;
	while (i < size)
	{
		int kind = rand() % 4;
		ULong n = 1 + rand() % 200;
		if (i + n > size)
			n = size - i;
		if (kind == 0)
			memset(p + i, rand() & 0xff, n);
		else if (kind == 1)
		{
			ULong at = rand() % strlen(kText);
			for (ULong j = 0; j < n; j++)
				p[i + j] = kText[(at + j) % strlen(kText)];
		}
		else if (kind == 2 && i > 300)
		{
			ULong back = 1 + rand() % 300;			// an earlier stretch again
			for (ULong j = 0; j < n; j++)
				p[i + j] = p[i + j - back];
		}
		else
			for (ULong j = 0; j < n; j++)
				p[i + j] = (UByte) rand();
		i += n;
	}
}


static Boolean
RoundTrip(TCompressor* compressor, TDecompressor* decompressor, const UByte* data, ULong size, ULong* outCompressedSize)
{
	ULong capacity = compressor->EstimatedCompressedSize((void*) data, size) + 0x800;
	UByte* compressed = (UByte*) NewPtr(capacity);
	UByte* restored = (UByte*) NewPtr(size + 0x800);
	memset(restored, 0xee, size + 0x800);
	ULong compressedSize = 0, restoredSize = 0;
	Boolean ok = compressor->Compress(&compressedSize, compressed, capacity, (void*) data, size) == noErr;
	if (ok)
	{
		ok = compressedSize >= 4 && *(ULong32*) compressed == compressedSize;
		if (outCompressedSize != nil)
			*outCompressedSize = compressedSize;
	}
	if (ok)
		ok = decompressor->Decompress(&restoredSize, restored, size + 0x800, compressed, compressedSize) == noErr;
	if (ok)
		ok = restoredSize == size && memcmp(restored, data, size) == 0;
	if (!ok)
		fprintf(stderr, "  round trip of %lu bytes failed: compressed %lu, restored %lu\n", (unsigned long) size, (unsigned long) compressedSize, (unsigned long) restoredSize);
	DisposPtr((Ptr) compressed);
	DisposPtr((Ptr) restored);
	return ok;
}


// the callback compressor's blocks, gathered
struct Gathered
{
	UByte*	fData;
	ULong	fSize;
	int		fBlocks;
	int		fLastSeen;
};

static NewtonErr
GatherBlock(void* refCon, void* block, ULong size, Boolean isLast)
{
	Gathered* g = (Gathered*) refCon;
	memcpy(g->fData + g->fSize, block, size);
	g->fSize += (size + 3) & ~3;			// chunks are read as words: keep them aligned
	g->fBlocks++;
	if (isLast)
		g->fLastSeen++;
	return noErr;
}


static void
TestLZ()
{
	EXPECT(ClassInfoByName("TCompressor", "TLZCompressor") == TLZCompressor::ClassInfo());
	EXPECT(ClassInfoByName("TDecompressor", "TLZDecompressor") == TLZDecompressor::ClassInfo());
	EXPECT(ClassInfoByName("TCallbackCompressor", "TLZCallbackCompressor") == TLZCallbackCompressor::ClassInfo());
	EXPECT(TLZCompressor::ClassInfo()->Size() == sizeof(TLZCompressor));
	EXPECT(TLZCompressor::ClassInfo()->Size() > 0x438);				// (pointers are wider here)

	TCompressor* compressor = TCompressor::New("TLZCompressor");
	TDecompressor* decompressor = TDecompressor::New("TLZDecompressor");
	EXPECT(compressor != nil && decompressor != nil);
	EXPECT(compressor->Init(nil) == noErr && decompressor->Init(nil) == noErr);
	EXPECT(compressor->EstimatedCompressedSize(nil, 100) == 108);

	// the framing of a small text: total length, one coded block
	ULong compressedSize = 0;
	ULong textSize = strlen(kText);
	EXPECT(RoundTrip(compressor, decompressor, (const UByte*) kText, textSize, &compressedSize));
	EXPECT(compressedSize < textSize * 3 / 4);							// the repeats pay
	{
		UByte out[1024];
		ULong n = 0;
		EXPECT(compressor->Compress(&n, out, sizeof(out), (void*) kText, textSize) == noErr);
		EXPECT(n == compressedSize && out[4] == 0 && out[5] == 1 && out[6] == 0 && out[7] == 0);	// a coded block's header
	}

	// runs: 3-byte minimum copies, 64-byte maximum ones
	UByte run[3000];
	memset(run, 'a', sizeof(run));
	EXPECT(RoundTrip(compressor, decompressor, run, sizeof(run), &compressedSize));
	EXPECT(compressedSize < 200);
	for (ULong i = 0; i < sizeof(run); i++)
		run[i] = (UByte) (i % 7 == 0 ? 'x' : 'a' + i % 3);
	EXPECT(RoundTrip(compressor, decompressor, run, sizeof(run), nil));

	// random bytes do not shrink: stored blocks, 4 bytes over per block
	UByte noise[0x1000];
	srand(7);
	for (ULong i = 0; i < sizeof(noise); i++)
		noise[i] = (UByte) rand();
	EXPECT(RoundTrip(compressor, decompressor, noise, sizeof(noise), &compressedSize));
	EXPECT(compressedSize == sizeof(noise) + 4 + 4 * 4);
	{
		UByte out[0x1100];
		ULong n = 0;
		EXPECT(compressor->Compress(&n, out, sizeof(out), noise, 0x400) == noErr);
		EXPECT(n == 0x408 && out[4] == 1 && memcmp(out + 8, noise, 0x400) == 0);
	}

	// every size around the block boundaries, and the tiny ones
	static UByte data[0x2800];
	Fill(data, sizeof(data), 42);
	const ULong sizes[] = { 0, 1, 2, 3, 4, 5, 63, 64, 65, 127, 0x3fe, 0x3ff, 0x400, 0x401, 0x402, 0x7ff, 0x800, 0x801, 0x1000, 0x2800 };
	for (ULong i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
		EXPECT(RoundTrip(compressor, decompressor, data, sizes[i], nil));
	for (unsigned seed = 1; seed < 12; seed++)
	{
		Fill(data, sizeof(data), seed);
		EXPECT(RoundTrip(compressor, decompressor, data, sizeof(data), &compressedSize));
		EXPECT(compressedSize < sizeof(data));
	}
	// a literal run longer than 63 bytes, then copies far back within the block
	for (ULong i = 0; i < 0x400; i++)
		data[i] = (UByte) (i < 200 ? (i * 37 + 11) : data[i - 199]);
	EXPECT(RoundTrip(compressor, decompressor, data, 0x400, nil));

	// the Handle form
	Handle h = NewHandle(textSize);
	EXPECT(h != nil);
	memcpy(*h, kText, textSize);
	EXPECT(compressor->EstimatedCompressedSize(h) == textSize + 8);
	EXPECT(compressor->Compress(h) == noErr);
	EXPECT(GetHandleSize(h) == compressedSize || GetHandleSize(h) < textSize);
	{
		UByte restored[1024];
		ULong n = 0;
		EXPECT(decompressor->Decompress(&n, restored, sizeof(restored), *h, GetHandleSize(h)) == noErr);
		EXPECT(n == textSize && memcmp(restored, kText, textSize) == 0);
	}
	DisposHandle(h);

	// the callback compressor: whole blocks of kLZBlockSize, each a chunk
	TCallbackCompressor* cb = TCallbackCompressor::New("TLZCallbackCompressor");
	EXPECT(cb != nil);
	Gathered gathered = { (UByte*) NewPtr(0x4000), 0, 0, 0 };
	cb->fWriteProc = GatherBlock;
	cb->fRefCon = &gathered;
	EXPECT(cb->Init(nil) == noErr);
	Fill(data, sizeof(data), 5);
	ULong fed = 0;
	while (fed < 0x2500)
	{
		ULong n = 1 + (fed * 7) % 300;
		if (fed + n > 0x2500)
			n = 0x2500 - fed;
		EXPECT(cb->WriteChunk(data + fed, n) == noErr);
		fed += n;
	}
	EXPECT(gathered.fBlocks == 9 && gathered.fLastSeen == 0);
	EXPECT(cb->Flush() == noErr);
	EXPECT(gathered.fBlocks == 10 && gathered.fLastSeen == 1);
	EXPECT(cb->Reset() == noErr && cb->Flush() == noErr && gathered.fBlocks == 10);	// nothing left after a reset (Flush alone does not empty the buffer)
	{
		static UByte restored[0x2800];
		ULong at = 0, total = 0;
		int blocks = 0;
		while (at < gathered.fSize)
		{
			ULong chunkSize = *(ULong32*) (gathered.fData + at);
			ULong n = 0;
			EXPECT(decompressor->Decompress(&n, restored + total, sizeof(restored) - total, gathered.fData + at, chunkSize) == noErr);
			total += n;
			at += (chunkSize + 3) & ~3;
			blocks++;
		}
		EXPECT(blocks == 10 && total == 0x2500 && memcmp(restored, data, 0x2500) == 0);
	}
	EXPECT(cb->Reset() == noErr);
	cb->Delete();
	DisposPtr((Ptr) gathered.fData);

	compressor->Delete();
	decompressor->Delete();
	long count = -1;
	EXPECT(!TLZCompressor::ClassInfo()->HasInstances(&count) && count == 0);
}


static void
CompressionScenario()
{
	InitializeCompression();
	TestLZ();
	HostStopTasks();
}


int main()
{
	gHostKernelServicesTask = CompressionScenario;
	OsBoot();
	if (failures == 0)
		printf("test_Compression: all passed\n");
	return failures != 0;
}
