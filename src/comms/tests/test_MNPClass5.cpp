// MNP class 5 compression (comms/MNPClass5.cpp): text, runs (of more than
// the 250 a count holds) and every byte value compressed through the
// compressor's hook and flushed, then decompressed through the
// decompressor's a byte at a time - the same bytes come back, and text
// and runs come out smaller.  No OS boot.

#include "MNP.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

struct Sink
{
	UByte	buf[20000];
	ULong	count;
};

static void
Collect(void* refCon, UByte byte)
{
	Sink* s = (Sink*) refCon;
	if (s->count < sizeof(s->buf))
		s->buf[s->count++] = byte;
}


static void
RoundTrip(const UByte* data, ULong n, const char* what, Boolean shouldShrink)
{
	TMNPClass5Vars* tx;
	TMNPClass5Vars* rx;
	EXPECT(MNPC5Open(&tx) == noErr && MNPC5Open(&rx) == noErr);
	static Sink packed, unpacked;
	packed.count = 0;
	unpacked.count = 0;
	MNPC5Init(tx, Collect, Collect, &packed);
	MNPC5Init(rx, Collect, Collect, &unpacked);
	for (ULong i = 0; i < n; i++)
		MNPC5CompressHook(tx, data[i]);
	MNPC5FlushHook(tx, 0);
	for (ULong i = 0; i < packed.count; i++)
		MNPC5DecompressHook(rx, packed.buf[i]);
	printf("%s: %lu -> %lu -> %lu\n", what, (unsigned long) n, (unsigned long) packed.count, (unsigned long) unpacked.count);
	EXPECT(unpacked.count == n && memcmp(unpacked.buf, data, n) == 0);
	if (shouldShrink)
		EXPECT(packed.count < n);
	MNPC5Close(tx);
	MNPC5Close(rx);
}


int
main()
{
	InitHostStandaloneHeap();
	static UByte data[8000];
	const char* text = "The quick brown fox jumps over the lazy dog. Newton docks with MNP class 5. ";
	ULong n = 0;
	while (n + strlen(text) < 4000)
	{
		memcpy(data + n, text, strlen(text));
		n += strlen(text);
	}
	RoundTrip(data, n, "text", true);

	n = 0;
	for (int i = 0; i < 600; i++)
		data[n++] = 'x';
	for (int i = 0; i < 3; i++)
		data[n++] = 'y';
	for (int i = 0; i < 251; i++)
		data[n++] = 0;
	data[n++] = 'z';
	RoundTrip(data, n, "runs", true);

	n = 0;
	for (int r = 0; r < 3; r++)
		for (int i = 0; i < 256; i++)
			data[n++] = (UByte) (i * 37 + r);
	RoundTrip(data, n, "every byte", false);

	printf("test_MNPClass5: %s\n", failures == 0 ? "all passed" : "FAILED");
	return failures != 0;
}
