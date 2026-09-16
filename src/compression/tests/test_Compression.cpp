// Compression test: the OS is booted so that the protocol registry exists;
// in the kernel services task InitializeCompression registers the ROM's
// compressors, and each is driven through its interface: round trips of
// texts, runs, random bytes (stored blocks) and multi-block inputs, the
// compressed format's framing, and the callback compressor's blocks.

#include "Compression.h"
#include "LZCompression.h"
#include "ZippyCompression.h"
#include "ArithmeticCompression.h"
#include "UnicodeCompression.h"
#include "ByteOrder.h"
#include "Boot.h"
#include "KernelGlobals.h"
#include "UserBoot.h"
#include "NewtonMemory.h"
#include "OSErrors.h"
#include "NewtErrors.h"
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
		ok = compressedSize >= 4 && GetBigEndianWord(compressed) == compressedSize;
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
			ULong chunkSize = GetBigEndianWord(gathered.fData + at);
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


// Zippy: pointer-like words, in cache order
static void
TestZippy()
{
	EXPECT(ClassInfoByName("TCompressor", "TZippyCompressor") == TZippyCompressor::ClassInfo());
	EXPECT(ClassInfoByName("TDecompressor", "TZippyDecompressor") == TZippyDecompressor::ClassInfo());
	EXPECT(ClassInfoByName("TCallbackCompressor", "TZippyCallbackCompressor") == TZippyCallbackCompressor::ClassInfo());
	TCompressor* compressor = TCompressor::New("TZippyCompressor");
	TDecompressor* decompressor = TDecompressor::New("TZippyDecompressor");
	EXPECT(compressor != nil && decompressor != nil);

	// a frames-heap-like block: zeros, repeated words, pointers into the
	// same pages, the odd new value
	static UByte words[0x400];
	ULong32 pointers[6] = { 0x0c108000, 0x0c10a004, 0x0011ff08, 0x0011ff10, 0x60000000, 0x0000001a };
	srand(11);
	for (ULong i = 0; i < sizeof(words); i += 4)
	{
		int kind = rand() % 10;
		ULong32 w;
		if (kind < 3)
			w = 0;
		else if (kind < 6)
			w = pointers[rand() % 6];
		else if (kind < 9)
			w = (pointers[rand() % 6] & 0xffffc007) | ((rand() & 0x7ff) << 3);		// same page, other offset
		else
			w = (ULong32) rand() * 3;
		PutBigEndianWord(words + i, w);
	}
	ULong compressedSize = 0;
	EXPECT(RoundTrip(compressor, decompressor, words, sizeof(words), &compressedSize));
	EXPECT(compressedSize < sizeof(words) / 2);
	{
		UByte out[0x500];
		ULong n = 0;
		EXPECT(compressor->Compress(&n, out, sizeof(out), words, sizeof(words)) == noErr);
		EXPECT(n == compressedSize && GetBigEndianWord(out + 4) == kZippyCodedHeader);
		// the first word is 0 or not: the stream starts with its code
		ULong32 first = GetBigEndianWord(words);
		EXPECT(first == 0 ? (out[8] & 0xc0) == 0 : (out[8] & 0xc0) == 0xc0);
	}

	// all zero words: two bits each
	memset(words, 0, sizeof(words));
	EXPECT(RoundTrip(compressor, decompressor, words, sizeof(words), &compressedSize));
	EXPECT(compressedSize == 8 + (0x400 / 4 * 2 + 7) / 8);
	// one word, repeated: 34 bits, then 6 each
	for (ULong i = 0; i < sizeof(words); i += 4)
		PutBigEndianWord(words + i, 0x12345678);
	EXPECT(RoundTrip(compressor, decompressor, words, sizeof(words), &compressedSize));
	EXPECT(compressedSize == 8 + (34 + (0x400 / 4 - 1) * 6 + 7) / 8);
	// noise does not shrink: stored, with the other header
	for (ULong i = 0; i < sizeof(words); i++)
		words[i] = (UByte) rand();
	EXPECT(RoundTrip(compressor, decompressor, words, sizeof(words), &compressedSize));
	EXPECT(compressedSize == sizeof(words) + 8);
	{
		UByte out[0x500];
		ULong n = 0;
		EXPECT(compressor->Compress(&n, out, sizeof(out), words, sizeof(words)) == noErr);
		EXPECT(GetBigEndianWord(out + 4) == kZippyStoredHeader && memcmp(out + 8, words, sizeof(words)) == 0);
	}
	// odd sizes: whole words only are coded (the ROM's)
	{
		UByte out[0x500], back[0x500];
		ULong n = 0, m = 0;
		memset(words, 0, 30);
		EXPECT(compressor->Compress(&n, out, sizeof(out), words, 30) == noErr);
		EXPECT(decompressor->Decompress(&m, back, sizeof(back), out, n) == noErr && m == 28);
		// a destination too small is refused by the chunk (Decompress itself answers 0 regardless, as in the ROM)
		EXPECT(((TZippyDecompressor*) decompressor)->DecompressChunk(&m, back, 16, out, n) == ERRBASE_COMPRESSION - 100);
		EXPECT(decompressor->Decompress(&m, back, 16, out, n) == noErr);
	}

	// the callback form
	TCallbackCompressor* cb = TCallbackCompressor::New("TZippyCallbackCompressor");
	Gathered gathered = { (UByte*) NewPtr(0x2000), 0, 0, 0 };
	cb->fWriteProc = GatherBlock;
	cb->fRefCon = &gathered;
	EXPECT(cb->Init(nil) == noErr);
	for (ULong i = 0; i < sizeof(words); i += 4)
		PutBigEndianWord(words + i, pointers[(i / 4) % 6]);
	EXPECT(cb->WriteChunk(words, 0x400) == noErr && cb->WriteChunk(words, 0x100) == noErr);
	EXPECT(gathered.fBlocks == 1);
	EXPECT(cb->Flush() == noErr && gathered.fBlocks == 2 && gathered.fLastSeen == 1);
	{
		UByte back[0x400];
		ULong m = 0;
		EXPECT(decompressor->Decompress(&m, back, sizeof(back), gathered.fData, GetBigEndianWord(gathered.fData)) == noErr);
		EXPECT(m == 0x400 && memcmp(back, words, 0x400) == 0);
	}
	cb->Delete();
	DisposPtr((Ptr) gathered.fData);
	compressor->Delete();
	decompressor->Delete();
}


// a byte stream gathered from a callback compressor, unpadded
static NewtonErr
GatherBytes(void* refCon, void* block, ULong size, Boolean isLast)
{
	Gathered* g = (Gathered*) refCon;
	memcpy(g->fData + g->fSize, block, size);
	g->fSize += size;
	g->fBlocks++;
	if (isLast)
		g->fLastSeen++;
	return noErr;
}


// a source for a callback decompressor: bytes handed out as asked
struct Source
{
	const UByte*	fData;
	ULong			fSize;
	ULong			fAt;
	int				fCalls;
};

static NewtonErr
ReadSource(void* refCon, void* into, long* size, Boolean* underflow)
{
	Source* s = (Source*) refCon;
	ULong n = s->fSize - s->fAt;
	if ((long) n > *size)
		n = *size;
	memcpy(into, s->fData + s->fAt, n);
	s->fAt += n;
	*size = n;
	*underflow = s->fAt >= s->fSize;
	s->fCalls++;
	return noErr;
}


// the arithmetic coder: a stream in (in odd-sized pieces), the same stream
// out (in pieces of readSize), through the callbacks
static Boolean
ArithmeticRoundTrip(const UByte* data, ULong size, ULong* outCompressedSize, ULong readSize)
{
	TCallbackCompressor* c = TCallbackCompressor::New("TArithmeticCompressor");
	TCallbackDecompressor* d = TCallbackDecompressor::New("TArithmeticDecompressor");
	if (c == nil || d == nil)
		return false;
	Gathered gathered = { (UByte*) NewPtr(size + 0x400), 0, 0, 0 };
	c->fWriteProc = GatherBytes;
	c->fRefCon = &gathered;
	Boolean ok = c->Init(nil) == noErr;
	ULong fed = 0;
	while (ok && fed < size)
	{
		ULong n = 1 + (fed * 13) % 97;
		if (fed + n > size)
			n = size - fed;
		ok = c->WriteChunk((void*) (data + fed), n) == noErr;
		fed += n;
	}
	if (ok)
		ok = c->Flush() == noErr && gathered.fLastSeen == 1;
	if (outCompressedSize != nil)
		*outCompressedSize = gathered.fSize;
	ULong got = 0;
	if (ok)
	{
		Source source = { gathered.fData, gathered.fSize, 0, 0 };
		d->fReadProc = ReadSource;
		d->fRefCon = &source;
		ok = d->Init(nil) == noErr;
		UByte* restored = (UByte*) NewPtr(size + 0x400);
		Boolean underflow = false;
		while (ok && !underflow)
		{
			long n = readSize;
			ok = d->ReadChunk(restored + got, &n, &underflow) == noErr;
			got += n;
			if (got > size + 0x200)
				ok = false;
		}
		if (ok)
			ok = got == size && memcmp(restored, data, size) == 0;
		DisposPtr((Ptr) restored);
	}
	if (!ok)
		fprintf(stderr, "  arithmetic round trip of %lu bytes failed: compressed %lu, restored %lu\n", (unsigned long) size, (unsigned long) gathered.fSize, (unsigned long) got);
	DisposPtr((Ptr) gathered.fData);
	c->Delete();
	d->Delete();
	return ok;
}


static void
TestArithmetic()
{
	EXPECT(ClassInfoByName("TCallbackCompressor", "TArithmeticCompressor") == TArithmeticCompressor::ClassInfo());
	EXPECT(ClassInfoByName("TCallbackDecompressor", "TArithmeticDecompressor") == TArithmeticDecompressor::ClassInfo());
	ULong compressedSize = 0;
	ULong textSize = strlen(kText);
	EXPECT(ArithmeticRoundTrip((const UByte*) kText, textSize, &compressedSize, 50));
	EXPECT(compressedSize < textSize * 3 / 4);				// a short English text, learnt from scratch
	UByte same[2000];
	memset(same, 'z', sizeof(same));
	EXPECT(ArithmeticRoundTrip(same, sizeof(same), &compressedSize, 333));
	EXPECT(compressedSize < 200);								// (the 5-bit quotient wastes some range)
	EXPECT(ArithmeticRoundTrip(same, 0, &compressedSize, 10));
	EXPECT(compressedSize >= 1 && compressedSize <= 5);
	EXPECT(ArithmeticRoundTrip(same, 1, nil, 1));
	static UByte data[0x2800];
	Fill(data, sizeof(data), 3);
	EXPECT(ArithmeticRoundTrip(data, sizeof(data), &compressedSize, 0x1000));
	EXPECT(compressedSize < sizeof(data));
	srand(9);
	for (ULong i = 0; i < sizeof(data); i++)
		data[i] = (UByte) rand();
	EXPECT(ArithmeticRoundTrip(data, sizeof(data), &compressedSize, 7));
	EXPECT(compressedSize > sizeof(data) && compressedSize < sizeof(data) + 200);	// noise costs a little

	// a fixed, non-adaptive model shared by both ends: one coder's tables,
	// tilted a little, lent to two others
	TArithmeticCompressor* m = (TArithmeticCompressor*) TCallbackCompressor::New("TArithmeticCompressor");
	TArithmeticCompressor* c = (TArithmeticCompressor*) TCallbackCompressor::New("TArithmeticCompressor");
	TArithmeticDecompressor* d = (TArithmeticDecompressor*) TCallbackDecompressor::New("TArithmeticDecompressor");
	EXPECT(m->Init(nil) == noErr);
	m->UpdateModel(m->fCharToIndex[(UByte) 'e']);
	m->UpdateModel(m->fCharToIndex[(UByte) 'e']);
	ArithmeticModel model = { m->fCumFreq, m->fCharToIndex, m->fIndexToChar, m->fFreq, false };
	long total = model.fCumFreq[0];
	Gathered gathered = { (UByte*) NewPtr(0x800), 0, 0, 0 };
	c->fWriteProc = GatherBytes;
	c->fRefCon = &gathered;
	EXPECT(c->Init(&model) == noErr && !c->fAdaptive && !c->fOwnsTables);
	EXPECT(c->WriteChunk((void*) kText, textSize) == noErr && c->Flush() == noErr);
	EXPECT(model.fCumFreq[0] == total);						// untouched
	Source source = { gathered.fData, gathered.fSize, 0, 0 };
	d->fReadProc = ReadSource;
	d->fRefCon = &source;
	EXPECT(d->Init(&model) == noErr && !d->fAdaptive);
	UByte restored[512];
	long n = sizeof(restored);
	Boolean underflow = false;
	EXPECT(d->ReadChunk(restored, &n, &underflow) == noErr && underflow && (ULong) n == textSize && memcmp(restored, kText, textSize) == 0);
	// junk that never ends: zeros are made up for four bytes, then the end of data
	TArithmeticDecompressor* e = (TArithmeticDecompressor*) TCallbackDecompressor::New("TArithmeticDecompressor");
	UByte junk[8] = { 1, 0, 0, 0, 0, 0, 0, 0 };
	Source junkSource = { junk, sizeof(junk), 0, 0 };
	e->fReadProc = ReadSource;
	e->fRefCon = &junkSource;
	EXPECT(e->Init(nil) == noErr);
	n = 512;
	NewtonErr err = e->ReadChunk(restored, &n, &underflow);
	EXPECT(err == kArithmeticErr_EndOfData || (err == noErr && underflow));
	e->Delete();
	d->Init(nil);											// own tables again: Delete frees only those
	d->Delete();
	c->Delete();
	m->Cleanup();											// (a compressor's Delete leaves its tables)
	m->Delete();
	DisposPtr((Ptr) gathered.fData);
}


// Unicode text: runs of one block's characters as high byte, count, lows
static void
TestUnicode()
{
	EXPECT(ClassInfoByName("TCallbackCompressor", "TUnicodeCompressor") == TUnicodeCompressor::ClassInfo());
	EXPECT(ClassInfoByName("TCallbackDecompressor", "TUnicodeDecompressor") == TUnicodeDecompressor::ClassInfo());
	TCallbackCompressor* c = TCallbackCompressor::New("TUnicodeCompressor");
	TCallbackDecompressor* d = TCallbackDecompressor::New("TUnicodeDecompressor");
	EXPECT(c != nil && d != nil);

	// a text: Latin, a Greek stretch, a symbol block that is not run-coded
	static UByte text[2 * 700];
	ULong n = 0;
	for (ULong i = 0; i < 300; i++)
	{
		text[n++] = 0x00;
		text[n++] = (UByte) kText[i % strlen(kText)];
	}
	for (ULong i = 0; i < 100; i++)
	{
		text[n++] = 0x03;
		text[n++] = (UByte) (0xb1 + i % 24);					// alpha..
	}
	for (ULong i = 0; i < 20; i++)
	{
		text[n++] = 0x26;										// dingbats: sent as they are
		text[n++] = (UByte) (0x60 + i);
	}
	for (ULong i = 0; i < 280; i++)
	{
		text[n++] = 0x00;
		text[n++] = (UByte) ('a' + i % 26);
	}
	Gathered gathered = { (UByte*) NewPtr(sizeof(text) + 0x100), 0, 0, 0 };
	c->fWriteProc = GatherBytes;
	c->fRefCon = &gathered;
	EXPECT(c->Init(nil) == noErr);
	EXPECT(c->WriteChunk(text, 1) == kError_Bad_Parameters);		// whole characters only
	ULong fed = 0;
	while (fed < n)
	{
		ULong k = 2 * (1 + (fed / 2 * 7) % 50);
		if (fed + k > n)
			k = n - fed;
		EXPECT(c->WriteChunk(text + fed, k) == noErr);
		fed += k;
	}
	EXPECT(c->Flush() == noErr && gathered.fLastSeen == 1);
	// 300 Latin: runs of 255 and 45 (2 + 255 + 2 + 45); 100 Greek: 2 + 100;
	// 20 dingbats raw: 40; 280 Latin: 2 + 255 + 2 + 25
	EXPECT(gathered.fSize == 304 + 102 + 40 + 284);
	EXPECT(gathered.fData[0] == 0 && gathered.fData[1] == 255 && gathered.fData[2] == (UByte) kText[0]);

	Source source = { gathered.fData, gathered.fSize, 0, 0 };
	d->fReadProc = ReadSource;
	d->fRefCon = &source;
	EXPECT(d->Init(nil) == noErr);
	static UByte restored[sizeof(text) + 0x100];
	long got = 0;
	Boolean underflow = false;
	long odd = 3;
	EXPECT(d->ReadChunk(restored, &odd, &underflow) == kError_Bad_Parameters);
	while (!underflow)
	{
		long k = 2 * (1 + (got / 2 * 3) % 40);
		EXPECT(d->ReadChunk(restored + got, &k, &underflow) == noErr);
		got += k;
		if (got > (long) sizeof(text))
			break;
	}
	EXPECT(got == (long) n && memcmp(restored, text, n) == 0);
	long k = 10;
	EXPECT(d->ReadChunk(restored, &k, &underflow) == noErr && k == 0 && underflow);
	c->Delete();
	d->Delete();
	DisposPtr((Ptr) gathered.fData);
}


static void
CompressionScenario()
{
	InitializeCompression();
	TestLZ();
	TestZippy();
	TestArithmetic();
	TestUnicode();
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
