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
	if (failures == 0)
		printf("test_WordDescriptors: all passed\n");
	return failures == 0 ? 0 : 1;
}
