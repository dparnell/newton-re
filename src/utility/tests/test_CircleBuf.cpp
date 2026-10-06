// TCircleBuf (CircleBuf.h): bytes in and out round the wrap, the free byte
// that tells full from empty, the end-of-message markers (a framed tool's
// frames kept whole), tentative puts committed or not, the block copies to
// and from memory and a buffer list, and the DMA views.  No OS boot.

#include "CircleBuf.h"
#include "BufferList.h"
#include "memory/host/KernelHeap.h"
#include "host/RomBugs.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


static void
TestBytes()
{
	TCircleBuf buf;
	EXPECT(buf.Allocate(7) == noErr);		// (7 + 4) & ~3: 8 bytes, 7 usable
	EXPECT(buf.fBufferSize == 8);
	EXPECT(buf.BufferCount() == 0 && buf.BufferSpace() == 7);
	UByte b;
	EXPECT(buf.GetNextByte(&b) == kCircleBufEmpty);
	for (int i = 1; i <= 7; i++)
		EXPECT(buf.PutNextByte(i) == kCircleBufOK);
	EXPECT(buf.PutNextByte(99) == kCircleBufFull);
	EXPECT(buf.BufferCount() == 7 && buf.BufferSpace() == 0);
	EXPECT(buf.BufferSpace(1) == kCircleBufFull);
	EXPECT(buf.PeekNextByte(&b) == kCircleBufOK && b == 1);
	for (int i = 1; i <= 5; i++)
		EXPECT(buf.GetNextByte(&b) == kCircleBufOK && b == i);
	// round the wrap
	for (int i = 8; i <= 12; i++)
		EXPECT(buf.PutNextByte(i) == kCircleBufOK);
	for (int i = 6; i <= 12; i++)
		EXPECT(buf.GetNextByte(&b) == kCircleBufOK && b == i);
	EXPECT(buf.GetNextByte(&b) == kCircleBufEmpty);
	buf.Reset();
	EXPECT(buf.fStart == 0 && buf.fEnd == 0);
}


static void
TestMarkers()
{
	TCircleBuf buf;
	EXPECT(buf.Allocate(16, 3, kCircleBufPlain, 0) == noErr);
	EXPECT(buf.fMarkerCount == 4 && buf.MarkerSpace() == 3);
	ULong count = 3;
	EXPECT(buf.CopyIn((UByte*) "abc", &count, true, 0x11) == kCircleBufOK && count == 0);
	count = 2;
	EXPECT(buf.CopyIn((UByte*) "de", &count, true, 0x22) == kCircleBufOK);
	EXPECT(buf.MarkerCount() == 2);
	// a frame at a time
	ULong n;
	EXPECT(buf.BufferCountToNextMarker(&n) && n == 3);
	UByte out[8];
	ULong want = sizeof(out), value = 0;
	EXPECT(buf.CopyOut(out, &want, &value) == kCircleBufEOM);
	EXPECT(memcmp(out, "abc", 3) == 0 && value == 0x11 && want == sizeof(out) - 3);
	UByte b;
	EXPECT(buf.GetNextByte(&b, &value) == kCircleBufOK && b == 'd');
	EXPECT(buf.GetNextByte(&b, &value) == kCircleBufEOM && b == 'e' && value == 0x22);
	EXPECT(buf.MarkerCount() == 0);
	// a tentative put: not committed, then committed as a frame
	buf.PutNextStart();
	EXPECT(buf.PutNextPossible('x') == kCircleBufOK);
	EXPECT(buf.BufferCount() == 0);
	EXPECT(buf.PutFirstPossible('y') == kCircleBufOK);	// (starts again)
	EXPECT(buf.PutNextPossible('z') == kCircleBufOK);
	EXPECT(buf.PutNextEOM(0x33) == kCircleBufOK);
	EXPECT(buf.BufferCount() == 2);
	EXPECT(buf.PeekNextByte(&b, &value) == kCircleBufOK && b == 'y');
	buf.FlushBytes();
	EXPECT(buf.BufferCount() == 0 && buf.MarkerCount() == 0);
	// the marker ring fills
	for (int i = 0; i < 3; i++)
		EXPECT(buf.PutNextByte('m', i) == kCircleBufOK);
	EXPECT(buf.PutNextByte('m', 9) == kCircleBufNoMarkerSpace);
	EXPECT(buf.FlushToNextMarker(&value) == kCircleBufOK && value == 0);
}


static void
TestBufferLists()
{
	TCircleBuf buf;
	EXPECT(buf.Allocate(12) == noErr);				// 16 bytes, 15 usable
	for (int i = 0; i < 10; i++)
		buf.PutNextByte('.');
	UByte b;
	for (int i = 0; i < 10; i++)
		buf.GetNextByte(&b);						// start and end at 10
	CBufferList in;
	EXPECT(in.Init() == noErr);
	UByte text[] = "the quick brown fox";			// 19: more than there is room for
	CBufferSegment* seg = CBufferSegment::New();
	EXPECT(seg != nil && seg->Init(text, 19) == noErr);
	in.Insert(seg);
	in.Seek(0, kSeekFromBeginning);
	ULong count = 19;
	EXPECT(buf.CopyIn(&in, &count) == kCircleBufOK);
	EXPECT(count == 4 && buf.BufferCount() == 15);	// round the wrap, the free byte kept
	UByte back[16];
	ULong want = 15, value;
	EXPECT(buf.CopyOut(back, &want, &value) == kCircleBufOK);		// (exhausted only when fewer were asked for than there are)
	EXPECT(want == 0 && memcmp(back, "the quick brown", 15) == 0);

	// DMA views
	EXPECT(buf.DMABufInfo(nil, nil, nil, nil) == buf.fBuffer);
	ULong start;
	EXPECT(buf.DMAGetInfo(&start) == buf.fEnd && start == buf.fStart);
	buf.PutNextStart();
	ULong end, putNext;
	EXPECT(buf.DMAPutInfo(&end, &putNext) == buf.fStart && end == putNext);
}


static void
TestGetBytes()
{
	SetRomBugFixed(true);
	TCircleBuf a, b;
	EXPECT(a.Allocate(8) == noErr && b.Allocate(4) == noErr);	// b: 7 usable
	ULong count = 5;
	a.CopyIn((UByte*) "12345", &count, false, 0);
	EXPECT(b.GetBytes(&a) == kCircleBufEmpty);
	EXPECT(b.BufferCount() == 5 && a.BufferCount() == 0);
	count = 5;
	a.CopyIn((UByte*) "67890", &count, false, 0);
	// full: the last byte written is the free one, and (fixed) the rest of a
	// stays in a, from that byte on
	EXPECT(b.GetBytes(&a) == kCircleBufFull);
	EXPECT(b.BufferCount() == 7 && a.BufferCount() == 3);
	UByte rest[4];
	count = 3;
	a.CopyOut(rest, &count, nil);
	EXPECT(count == 0 && memcmp(rest, "890", 3) == 0);		// (count: what was not copied)
}


// the ROM's GetBytes drops what did not fit (a ROM bug)
static void
TestGetBytesRomBug()
{
	SetRomBugFixed(false);
	TCircleBuf a, b;
	EXPECT(a.Allocate(8) == noErr && b.Allocate(4) == noErr);
	ULong count = 5;
	a.CopyIn((UByte*) "12345", &count, false, 0);
	EXPECT(b.GetBytes(&a) == kCircleBufEmpty);
	count = 5;
	a.CopyIn((UByte*) "67890", &count, false, 0);
	EXPECT(b.GetBytes(&a) == kCircleBufFull);
	EXPECT(b.BufferCount() == 7 && a.BufferCount() == 0);
	SetRomBugFixed(true);
}


int
main()
{
	InitHostStandaloneHeap();
	TestBytes();
	TestMarkers();
	TestBufferLists();
	TestGetBytes();
	TestGetBytesRomBug();
	printf("test_CircleBuf: %s\n", failures == 0 ? "all passed" : "FAILED");
	return failures != 0;
}
