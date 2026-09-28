// The cursive recogniser's word descriptors (recognition/WordDescriptors.h)
// and the frame a word is read in (recognition/CursiveReader.h).
//
// The descriptors are a list threaded through a block of eight; the
// checks go from the list itself, through writing the segmenter's words
// into it (a word kept, a word whose strokes moved thrown away, a word
// after a dash joined to it), to two words written with a pen, grouped
// by the segmenter into two descriptors, and each read - which on the
// host ends at the low level, so each is marked as not read (0x200).

#include "WordDescriptors.h"
#include "CursiveReader.h"
#include "InkGroups.h"
#include "XrDomains.h"
#include "ParaGraph.h"
#include "Chunk.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


/*--------------------------------------------------------------------
	A pen writing a trace: points in eighths of a pixel, a pen-up
	(y -1) before each stroke and after the last.
--------------------------------------------------------------------*/

static PS_point_type	gTrace[4096];
static long				gCount = 0;
static bool				gSmooth = true;		// a point every pixel of travel, or only the points given

static void	TraceStart(void)		{ gCount = 0; gTrace[gCount].x = 0; gTrace[gCount].y = -1; gCount++; }
static void	StrokeEnd(void)			{ gTrace[gCount].x = 0; gTrace[gCount].y = -1; gCount++; }
static void
PenTo(double x, double y)
{
	// a point every pixel of travel
	if (gSmooth && gCount > 0 && gTrace[gCount - 1].y >= 0)
	{
		double lx = gTrace[gCount - 1].x / 8.0, ly = gTrace[gCount - 1].y / 8.0;
		double d = sqrt((x - lx) * (x - lx) + (y - ly) * (y - ly));
		int steps = (int) d;
		for (int s = 1; s < steps; s++)
		{
			gTrace[gCount].x = (short) ((lx + (x - lx) * s / steps) * 8.0);
			gTrace[gCount].y = (short) ((ly + (y - ly) * s / steps) * 8.0);
			gCount++;
		}
	}
	gTrace[gCount].x = (short) (x * 8.0);
	gTrace[gCount].y = (short) (y * 8.0);
	gCount++;
}

static void
Arc(double cx, double cy, double rx, double ry, double from, double to, bool first)
{
	for (int i = 0; i <= 30; i++)
	{
		double a = (from + (to - from) * i / 30) * 3.14159265358979 / 180.0;
		if (i == 0 && !first)
			continue;
		PenTo(cx + rx * cos(a), cy - ry * sin(a));
	}
}

static const double h = 14;

// "ton": four strokes (the t's stem and bar, the o, the n)
static double
WriteTon(double x, double base)
{
	PenTo(x + h * 0.35, base - 1.7 * h);	PenTo(x + h * 0.35, base);		StrokeEnd();
	PenTo(x, base - h);						PenTo(x + h * 0.75, base - h);	StrokeEnd();
	x += h * 1.1;
	Arc(x + h * 0.45, base - h / 2, h * 0.45, h / 2, 80, 440, true);		StrokeEnd();
	x += h * 1.15;
	PenTo(x, base - h);	PenTo(x, base);	PenTo(x, base - h * 0.5);
	Arc(x + h * 0.35, base - h * 0.55, h * 0.35, h * 0.45, 180, 0, false);
	PenTo(x + h * 0.7, base);												StrokeEnd();
	return x + h * 0.95;
}


/*--------------------------------------------------------------------
	The list.
--------------------------------------------------------------------*/

static GCWordDescrType*
NewBlock(Handle* h)
{
	*h = GCNewRecSegment();
	EXPECT(*h != nil);
	return (GCWordDescrType*) HWRMemoryLockHandle(*h);
}

static void
TestList(void)
{
	Handle h;
	GCWordDescrType* words = NewBlock(&h);
	EXPECT(GCGetFirstWordDescriptor(words) == nil);
	GCWordDescrType* a = GCNewWordDescriptor(words);
	GCWordDescrType* b = GCNewWordDescriptor(words);
	GCWordDescrType* c = GCNewWordDescriptor(words);
	EXPECT(a == words && b == words + 1 && c == words + 2);
	EXPECT(GCGetFirstWordDescriptor(words) == a);
	EXPECT(GCGetNextWordDescriptor(words, a) == b);
	EXPECT(GCGetNextWordDescriptor(words, b) == c);
	EXPECT(GCGetNextWordDescriptor(words, c) == nil);
	EXPECT(GCGetLastWordDescriptor(words) == c);
	EXPECT(GCGetPrevWordDescriptor(words, c) == b);

	// b taken out: its slot is free again and the next one made fills it
	EXPECT(GCWordDescriptorDispose(words, b) == 0);
	EXPECT(GCGetNextWordDescriptor(words, a) == c);
	GCWordDescrType* d = GCNewWordDescriptor(words);
	EXPECT(d == words + 1);
	EXPECT(GCGetLastWordDescriptor(words) == d);

	// sorted by first stroke: a 9, c 3, d 5 -> c, d, a
	a->fFirst = 9; c->fFirst = 3; d->fFirst = 5;
	EXPECT(GCSortWordDescByStrokesOrder(words) == 0);
	EXPECT(GCGetFirstWordDescriptor(words) == c);
	EXPECT(GCGetNextWordDescriptor(words, c) == d);
	EXPECT(GCGetNextWordDescriptor(words, d) == a);
	EXPECT(GCGetLastWordDescriptor(words) == a);

	// flags: counted, found, changed for the first n
	a->fFlags = 2; c->fFlags = 2; d->fFlags = 0x82;
	EXPECT(GCCountWordDescWithFlags(words, 2, 0) == 3);
	EXPECT(GCCountWordDescWithFlags(words, 2, 1) == 2);
	EXPECT(GCGetWordDescWithFlags(words, 0x80, 0) == d);
	EXPECT(GCRecSegmentSetGroupFlags(words, 1, 0) == 0);		// three settled, one kept back: the first two to be read
	EXPECT(c->fFlags == 4 && d->fFlags == 0x84 && a->fFlags == 2);

	// the eighth is the last there is room for
	for (int i = 0; i < 5; i++)
		EXPECT(GCNewWordDescriptor(words) != nil);
	EXPECT(GCNewWordDescriptor(words) == nil);
	HWRMemoryUnlockHandle(h);
	HWRMemoryFreeHandle(h);
}


static void
TestContains(void)
{
	GCWordDescrType word;
	memset(&word, 0, sizeof(word));
	word.fFirst = 4; word.fLast = 6; word.fExtra[0] = 9; word.fExtra[1] = 11;
	EXPECT(GCIsWordDescContainsStroke(&word, 4));
	EXPECT(GCIsWordDescContainsStroke(&word, 6));
	EXPECT(GCIsWordDescContainsStroke(&word, 11));
	EXPECT(!GCIsWordDescContainsStroke(&word, 7));
	EXPECT(!GCIsWordDescContainsStroke(&word, 3));

	UByte strokes[32];
	memset(strokes, 0xff, sizeof(strokes));
	EXPECT(GCWDRemoveStrokesFromList(&word, strokes) == 0);
	EXPECT(strokes[0] == 0xf1);			// 4, 5 and 6 gone (bit 7 is stroke 0)
	EXPECT(strokes[1] == 0xaf);			// 9 and 11 (the extras are walked while each is more than its place in the list, which these are)
}


static void
TestWriteGroupResults(void)
{
	Handle h;
	GCWordDescrType* words = NewBlock(&h);
	ws_word_info_type info;
	memset(&info, 0, sizeof(info));
	info.fStrokes[0] = 2; info.fSure[0] = 7;

	// a word of strokes 0-2, settled
	EXPECT(GCWordDescWriteGroupResults(words, 0, 2, nil, 10, 20, 80, 100, 0, 2, &info) == 0);
	GCWordDescrType* w = GCGetFirstWordDescriptor(words);
	EXPECT(w != nil && w->fFirst == 0 && w->fLast == 2 && w->fFlags == 2);
	EXPECT(w->fLineHeight == 80 && w->fBaseLine == 100 && w->fInfo.fSure[0] == 7);

	// the same strokes again: the descriptor is kept, its flags changed
	EXPECT(GCWordDescWriteGroupResults(words, 0, 2, nil, 10, 20, 80, 100, 0, 4, &info) == 0);
	EXPECT(GCGetFirstWordDescriptor(words) == w && GCGetNextWordDescriptor(words, w) == nil);
	EXPECT(w->fFlags == 4);

	// strokes 2-4: the old word's stroke 2 has moved, so it goes
	EXPECT(GCWordDescWriteGroupResults(words, 2, 4, nil, 10, 20, 80, 100, 0, 2, &info) == 0);
	w = GCGetFirstWordDescriptor(words);
	EXPECT(w != nil && w->fFirst == 2 && w->fLast == 4 && GCGetNextWordDescriptor(words, w) == nil);

	// a word ending in a dash (5-6), and the word after it (7-8) joined
	// to it
	EXPECT(GCWordDescWriteGroupResults(words, 5, 6, nil, 300, 100, 80, 100, 0, 0x82, &info) == 0);
	GCWordDescrType* dashed = GCGetWordDescWithFlags(words, 0x80, 0);
	EXPECT(dashed != nil && dashed->fJoinX == 300 && dashed->fJoinY == 100);
	EXPECT(GCWordDescWriteGroupResults(words, 7, 8, nil, 20, 300, 80, 280, 1, 2, &info) == 0);
	GCWordDescrType* joined = GCGetNextWordDescriptor(words, GCGetFirstWordDescriptor(words));
	EXPECT(joined != nil && joined->fFirst == 5 && joined->fLast == 8);
	EXPECT(joined->fMerged == 2);						// two strokes of the first part
	EXPECT(joined->fJoinX == 280 && joined->fJoinY == -200);	// where the second line is, relative
	EXPECT(joined->fInfo.fStrokes[1] == 4);				// the second part's info renumbered after the first's
	EXPECT(GCGetNextWordDescriptor(words, joined) == nil);
	EXPECT(GCGetWordDescWithFlags(words, 0x80, 0) == nil);
	HWRMemoryUnlockHandle(h);
	HWRMemoryFreeHandle(h);
}


static void
TestTraces(void)
{
	// three strokes; a word of the first and the third
	gSmooth = false;
	TraceStart();
	PenTo(10, 10); PenTo(12, 10); PenTo(14, 10);	StrokeEnd();
	PenTo(40, 10); PenTo(42, 10);					StrokeEnd();
	PenTo(20, 10); PenTo(22, 10); PenTo(24, 12);	StrokeEnd();
	UByte strokes[32];
	memset(strokes, 0, sizeof(strokes));
	strokes[0] = 0xe0;								// strokes 0, 1 and 2 are the unit's
	GCWordDescrType word;
	memset(&word, 0, sizeof(word));
	word.fFirst = 0; word.fLast = 0; word.fExtra[0] = 2;
	PS_point_type* wordTrace;
	short n;
	ULong copied;
	EXPECT(GCWDGetTrace(gTrace, &word, strokes, &wordTrace, &n, &copied) == 0);
	EXPECT(copied == 1);
	EXPECT(n == 1 + 3 + 1 + 3 + 1);					// the pen-up between them shared
	EXPECT(wordTrace[0].y == -1 && wordTrace[1].x == 80 && wordTrace[4].y == -1 && wordTrace[5].x == 160 && wordTrace[8].y == -1);
	HWRMemoryFree((Ptr) wordTrace);

	// a run is the unit's own trace
	word.fExtra[0] = 0; word.fLast = 1;
	EXPECT(GCWDGetTrace(gTrace, &word, strokes, &wordTrace, &n, &copied) == 0);
	EXPECT(copied == 0 && wordTrace == gTrace && n == 8);

	// two lines joined after a dash: the dash (the stroke reaching
	// furthest right of the first two) goes, the second line moves up
	TraceStart();
	PenTo(10, 10); PenTo(20, 10);					StrokeEnd();	// the word
	PenTo(30, 10); PenTo(34, 10);					StrokeEnd();	// the dash
	PenTo(5, 40);  PenTo(15, 40);					StrokeEnd();	// the next line
	short count = (short) gCount;
	EXPECT(GCMergeLinesAndRemoveDash(gTrace, &count, 200, -240, 2, 99) == 1);
	EXPECT(count == gCount - 3);
	EXPECT(gTrace[3].y == -1);						// the word's end, then straight on to the next line
	EXPECT(gTrace[4].x == 40 + 200 && gTrace[4].y == 320 - 240);
	EXPECT(gTrace[6].y == -1);
	gSmooth = true;
}


static void
TestBaseLine(void)
{
	gSmooth = false;
	TraceStart();
	PenTo(10, 20); PenTo(30, 20); PenTo(30, 40);	StrokeEnd();
	UByte box[8];
	EXPECT(GetInkBox(gTrace, gCount, box) == 0);
	EXPECT(box[0] == 0 && box[1] == 80 && box[3] == 160 && box[5] == 240 && box[6] == 1 && box[7] == 64);
	EXPECT(GetAvePos(gTrace, gCount) > 160 && GetAvePos(gTrace, gCount) < 320);

	rc_type rc;
	memset(&rc, 0, sizeof(rc));
	RCSetH(&rc, 0x96, (UShort) gCount);
	// the segmenter's line, no new line: both halves at 50
	EXPECT(GCFillBaseLineParameters(160, 300, 0, 0, 0, 0, 0, &rc, gTrace) == 0);
	EXPECT(RCGetH(&rc, 0xe2) == 160 && RCGetH(&rc, 0xe4) == 300 && RCGetH(&rc, 0xe6) == 50 && RCGetH(&rc, 0xe8) == 50);
	// a new line: the middle not trusted
	EXPECT(GCFillBaseLineParameters(160, 300, 1, 0, 0, 0, 0, &rc, gTrace) == 0);
	EXPECT(RCGetH(&rc, 0xe8) == 0);
	// the word before's line, its middle far from this word's ink
	EXPECT(GCFillBaseLineParameters(0, 0, 0, 20, 2000, 70, 70, &rc, gTrace) == 0);
	EXPECT(RCGetH(&rc, 0xe2) == 20 && RCGetH(&rc, 0xe6) == 70 && RCGetH(&rc, 0xe8) == 0);
	gSmooth = true;
}


/*--------------------------------------------------------------------
	Two words through the segmenter and the reader.
--------------------------------------------------------------------*/

static void
TestGroupAndRead(void)
{
	TraceStart();
	double x = WriteTon(20, 60);
	WriteTon(x + 60, 60);
	GCGroupParmStruct parm;
	memset(&parm, 0, sizeof(parm));
	parm.fSpacing = 4;
	parm.fSureLevel = 0;
	for (int s = 0; s < 8; s++)
		parm.fStrokes[s >> 3] |= (UByte) (0x80 >> (s & 7));
	Handle h;
	GCWordDescrType* words = NewBlock(&h);
	EXPECT(GCGroupStrokes(words, gTrace, (short) gCount, 1, 0, &parm) == 0);
	GCWordDescrType* first = GCGetFirstWordDescriptor(words);
	GCWordDescrType* second = GCGetNextWordDescriptor(words, first);
	EXPECT(first != nil && second != nil && GCGetNextWordDescriptor(words, second) == nil);
	if (first != nil && second != nil)
	{
		fprintf(stderr, "  words: %d-%d (%#lx), %d-%d (%#lx)\n", first->fFirst, first->fLast, (unsigned long) first->fFlags, second->fFirst, second->fLast, (unsigned long) second->fFlags);
		EXPECT(first->fFirst == 0 && first->fLast == 3);
		EXPECT(second->fFirst == 4 && second->fLast == 7);
		EXPECT((first->fFlags & 4) != 0 && (second->fFlags & 4) != 0);	// the end of the writing: both to be read

		// read: the frame runs and the low level cuts the word into xrs;
		// on the host the reader stops at xrw_algs (NOT YET), -9
		rc_type rc;
		memset(&rc, 0, sizeof(rc));
		EXPECT(GCTryToRecognize(gTrace, first, &rc, &parm) == -9);
		EXPECT((first->fFlags & 0x400) != 0);			// (0x200 is the low level failing, 0x400 xrw_algs)
		EXPECT(rc.fWordInfo == nil);
		EXPECT(RCGetH(&rc, 0x96) > 2);				// the word's points handed to the engine
		EXPECT(RCGetH(&rc, 0xd8 + 4) > RCGetH(&rc, 0xd8));	// its ink box
		// read again: a descriptor that has readings is not
		first->fRecResults = (Handle) 1;
		EXPECT(GCTryToRecognize(gTrace, first, &rc, &parm) == -6);
		first->fRecResults = nil;
	}
	HWRMemoryUnlockHandle(h);
	GCDisposeGResHandle(&parm.fGRes);
	HWRMemoryFreeHandle(h);
}


/*--------------------------------------------------------------------
	SetStrXrRC: one word of a configuration's strxrCommands.
--------------------------------------------------------------------*/

static ULong
Command(ULong op, ULong which, ULong low)
{
	return (op << 25) | (which << 16) | (low & 0xffff);
}


static void
TestSetStrXrRC(void)
{
	STRXRPARAM p;
	memset(&p, 0, sizeof(p));
	p.fControl = 0x000a0005;		// wait for 5 words, spacing 5
	SetStrXrRC(Command(0, 0x02, 4), &p);			// set the letter style
	EXPECT(p.fLetterStyle == 4);
	SetStrXrRC(Command(4, 0x02, 3), &p);			// add
	EXPECT(p.fLetterStyle == 7);
	SetStrXrRC(Command(1, 0x17, 0x8000), &p);		// or into the flags
	EXPECT(p.fFlags == 0x8000);
	SetStrXrRC(Command(0, 0x1f, 0xfffe), &p);		// the base line, a long set as a signed short
	EXPECT(p.fGeom[2] == -2);
	SetStrXrRC(Command(0, 0x40, 7), &p);			// the letter spacing: the low half
	EXPECT(p.fControl == 0x000a0007);
	SetStrXrRC(Command(0, 0x42, 2), &p);			// the words to wait for, from bit 17
	EXPECT(p.fControl == 0x00040007);
	SetStrXrRC(Command(0, 0x41, 1), &p);			// read only at the end
	EXPECT(p.fControl == 0x00050007);
	SetStrXrRC(Command(0, 0x41, 0), &p);
	EXPECT(p.fControl == 0x00040007);
	SetStrXrRC(Command(0, 0x46, 9), &p);			// ROM QUIRK: 0x46 changes +0x54 too
	EXPECT(p.fPrevBase[2] == 9 && p.fPrevBase[3] == 0);
	// a byte by its ROM offset: +0x0f is fControl's lowest byte
	SetStrXrRC((0 << 25) | (1 << 24) | (0x33 << 16) | 0x0f, &p);
	EXPECT(p.fControl == 0x00040033);
	// +0x40 is fLetterStyle's high byte
	SetStrXrRC((1 << 25) | (1 << 24) | (0x01 << 16) | 0x40, &p);
	EXPECT(p.fLetterStyle == 0x0107);
	// an offset of 0x58 or more names no byte: the low half is then the operand of field 0 (none)
	STRXRPARAM before = p;
	SetStrXrRC((0 << 25) | (1 << 24) | (0x33 << 16) | 0x58, &p);
	EXPECT(memcmp(&before, &p, sizeof(p)) == 0);
}


/*--------------------------------------------------------------------
	The digit reader's context and the configuration it narrows while
	a number is read (Chunk.h).
--------------------------------------------------------------------*/

static void
TestChunkContext(void)
{
	rc_type rc;
	memset(&rc, 0, sizeof(rc));
	RCSetH(&rc, 0x00, 0x1111);
	RCSetH(&rc, 0x02, 0x2222);
	RCSetH(&rc, 0x08, 0x00ff);
	RCSetH(&rc, 0x0a, 0x4444);
	RCSetH(&rc, 0x90, 0x5555);
	void* chunk = nil;
	ChunkAllocCtx(&chunk, &rc);
	EXPECT(chunk != nil && IsChunkNumbers(chunk) == 0);
	ChunkModifyRC(chunk, &rc);						// no numbers found: nothing changes
	EXPECT(RCGetH(&rc, 0x02) == 0x2222);
	((ChunkCtx*) chunk)->fNumbers = 1;				// as the processor would
	ChunkModifyRC(chunk, &rc);
	EXPECT(RCGetH(&rc, 0x02) == 0x3f && RCGetH(&rc, 0x90) == 0x422 && RCGetH(&rc, 0x08) == 0x00fa && RCGetH(&rc, 0x0a) == 2);
	ChunkRestoreRC(chunk, &rc);
	EXPECT(RCGetH(&rc, 0x00) == 0x1111 && RCGetH(&rc, 0x02) == 0x2222 && RCGetH(&rc, 0x08) == 0x00ff
		&& RCGetH(&rc, 0x0a) == 0x4444 && RCGetH(&rc, 0x90) == 0x5555);
	// numbers alone: +0x90 0x62 and +0x92 one - which is not put back (ROM bug)
	((ChunkCtx*) chunk)->fNumbersOnly = 1;
	ChunkModifyRC(chunk, &rc);
	EXPECT(RCGetH(&rc, 0x90) == 0x62 && RCGetH(&rc, 0x92) == 1 && RCGetH(&rc, 0x08) == 0x00ff);
	ChunkRestoreRC(chunk, &rc);
	EXPECT(RCGetH(&rc, 0x90) == 0x5555 && RCGetH(&rc, 0x92) == 1);
	rec_w_type readings[1];
	EXPECT(ChunkWriteParamCtx(chunk, &rc, nil, readings) == &((ChunkCtx*) chunk)->fReadings);
	EXPECT(ChunkWriteParamCtx(nil, &rc, nil, readings) == nil);
	ChunkCleanUp(&chunk);
	EXPECT(chunk == nil);
}


// The digit reader's chord measures (Chunk.h).
static void
TestChunkChords(void)
{
	EXPECT(v_QDistFromChord(0, 0, 10, 0, 5, 3) == 9);
	EXPECT(v_QDistFromChord(0, 0, 10, 0, 5, -4) == 16);
	EXPECT(v_QDistFromChord(0, 0, 0, 10, 7, 2) == 49);
	EXPECT(v_QDistFromChord(1, 1, 1, 1, 4, 5) == 25);			// a chord of no length
	EXPECT(v_QDistFromChord(0, 0, 3, 4, 3, 4) == 0);			// on the line

	tag_WORD_TRACE arch[5] = { {0, 0, 0}, {5, 2, 0}, {10, 5, 0}, {15, 2, 0}, {20, 0, 0} };
	EXPECT(v_MostFarFromChord(arch, 0, 4) == 2);
	// a flat top: the middle of the run
	tag_WORD_TRACE flat[5] = { {0, 0, 0}, {5, 5, 0}, {10, 5, 0}, {15, 5, 0}, {20, 0, 0} };
	EXPECT(v_MostFarFromChord(flat, 0, 4) == 2);
	// a pen-up breaks the run: the first of the flat points stays
	tag_WORD_TRACE broken[6] = { {0, 0, 0}, {5, 5, 0}, {0, -1, 0}, {10, 5, 0}, {15, 5, 0}, {20, 0, 0} };
	EXPECT(v_MostFarFromChord(broken, 0, 5) == 1);
	// nothing off the line: the points at nought count as a flat run from
	// the chord's start (its middle answered, one point on)
	tag_WORD_TRACE line[3] = { {0, 0, 0}, {5, 0, 0}, {10, 0, 0} };
	EXPECT(v_MostFarFromChord(line, 0, 2) == 1);

	// directions, y growing downwards
	EXPECT(GetDirection(0, 0, 0, -10) == 0);		// up (just left of it)
	EXPECT(GetDirection(0, 0, 1, -10) == 23);		// up, a little right
	EXPECT(GetDirection(0, 0, -10, 0) == 6);		// left (straight left is in the octant below it)
	EXPECT(GetDirection(0, 0, -10, 1) == 6);		// left, a little down
	EXPECT(GetDirection(0, 0, 0, 10) == 12);		// down (in the octant to its right)
	EXPECT(GetDirection(0, 0, 10, 0) == 18);		// right (in the octant above it)
	EXPECT(GetDirection(0, 0, 10, -1) == 18);		// right, a little up
	EXPECT(GetDirection(0, 0, 10, -10) == 21);		// 45 degrees up and right: octant 2's last slice
	EXPECT(GetDirection(0, 0, 10, -4) == 19);		// 22 degrees: the middle slice
	EXPECT(GetDirection(3, 3, 3, 3) == 17);			// no step at all
}


int
main()
{
	InitHostStandaloneHeap();
	TestList();
	TestContains();
	TestWriteGroupResults();
	TestTraces();
	TestBaseLine();
	TestGroupAndRead();
	TestSetStrXrRC();
	TestChunkContext();
	TestChunkChords();
	if (failures == 0)
		printf("test_WordDescriptors: all passed\n");
	return failures == 0 ? 0 : 1;
}
