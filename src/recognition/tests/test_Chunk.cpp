// The cursive reader's digit reader (recognition/Chunk.h), from the
// bottom up: digits drawn with a synthetic pen, turned into the reader's
// own trace as ChunkProcessor does, the turns marked and the polyline
// fitted to them.

#include "Chunk.h"
#include "ParaGraph.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static bool gVerbose = false;


/*--------------------------------------------------------------------
	A pen: points a pixel apart along straight lines, a pen-up (y -1)
	before each stroke and after the last - the reader's trace, as
	ChunkProcessor copies it out of the PS_point_type points.
--------------------------------------------------------------------*/

static tag_WORD_TRACE	gTrace[4096];
static long				gCount = 0;
static double			gPenX, gPenY;

// a point, in pixels: the trace is in eighths of one
static void
Pt(double x, double y)
{
	gTrace[gCount].x = (short) lround(x * 8);
	gTrace[gCount].y = (short) lround(y * 8);
	gTrace[gCount].fFlags = 0;
	gCount++;
}

static void	TraceStart(void)			{ gCount = 0; gTrace[gCount].x = 0; gTrace[gCount].y = -1; gTrace[gCount].fFlags = 0; gCount++; }
static void	StrokeEnd(void)				{ gTrace[gCount].x = 0; gTrace[gCount].y = -1; gTrace[gCount].fFlags = 0; gCount++; }
static void	MoveTo(double x, double y)	{ gPenX = x; gPenY = y; Pt(x, y); }

// a point every pixel of travel
static void
LineTo(double x, double y)
{
	double dx = x - gPenX, dy = y - gPenY;
	long steps = (long) ceil(sqrt(dx * dx + dy * dy));
	for (long i = 1; i <= steps; i++)
		Pt(gPenX + dx * i / steps, gPenY + dy * i / steps);
	gPenX = x;
	gPenY = y;
}

// an arc about (cx, cy) from angle a to b (degrees, anticlockwise with y up)
static void
ArcTo(double cx, double cy, double r, double a, double b)
{
	const double kRad = 3.14159265358979323846 / 180;
	long steps = (long) ceil(fabs(b - a) * kRad * r);
	for (long i = 1; i <= steps; i++)
	{
		double t = (a + (b - a) * i / steps) * kRad;
		gPenX = cx + r * cos(t);
		gPenY = cy - r * sin(t);
		Pt(gPenX, gPenY);
	}
}

// a 4, 20 pixels high: the slanting stroke and the bar in one, then the upright
static void
DrawFour(double x, double y)
{
	MoveTo(x + 10, y);
	LineTo(x, y + 13);
	LineTo(x + 14, y + 13);
	StrokeEnd();
	MoveTo(x + 10, y);
	LineTo(x + 10, y + 20);
	StrokeEnd();
}

// a 2: the hook over the top, the diagonal down to the left, the foot
static void
DrawTwo(double x, double y)
{
	MoveTo(x + 1, y + 6);
	ArcTo(x + 7, y + 6, 6, 170, -30);
	LineTo(x, y + 20);
	LineTo(x + 14, y + 20);
	StrokeEnd();
}

// an ellipse about (cx, cy) from angle a to b, as ArcTo
static void
EllipseTo(double cx, double cy, double rx, double ry, double a, double b)
{
	const double kRad = 3.14159265358979323846 / 180;
	long steps = (long) ceil(fabs(b - a) * kRad * (rx > ry ? rx : ry));
	for (long i = 1; i <= steps; i++)
	{
		double t = (a + (b - a) * i / steps) * kRad;
		gPenX = cx + rx * cos(t);
		gPenY = cy - ry * sin(t);
		Pt(gPenX, gPenY);
	}
}

// a 0, 20 pixels high: from the top anticlockwise, down the left and back
// up the right to where it started
static void
DrawZero(double x, double y)
{
	MoveTo(x + 7, y);
	EllipseTo(x + 7, y + 10, 7, 10, 90, 450);
	StrokeEnd();
}

// a 1: one stroke straight down
static void
DrawOne(double x, double y)
{
	MoveTo(x + 5, y);
	LineTo(x + 5, y + 20);
	StrokeEnd();
}

static void
Dump(const char* what, tag_wapx_type* nodes, long n)
{
	if (!gVerbose)
		return;
	printf("%s: %ld nodes\n", what, n);
	for (long k = 0; k < n; k++)
		printf("  %2ld: point %4d (%3d,%3d) flags %02x first %d dir %04x out %2d\n", k, nodes[k].fIndex, nodes[k].x, nodes[k].y,
			nodes[k].fFlags, nodes[k].fFirst, nodes[k].fDir, nodes[k].fDirOut);
}


/*--------------------------------------------------------------------
	ExtrWordTrace_V: the turns
--------------------------------------------------------------------*/

static void
TestTurns(void)
{
	// a zigzag stroke 40 high: every turn marked, alternately
	TraceStart();
	MoveTo(0, 0);
	LineTo(10, 40);
	LineTo(20, 0);
	LineTo(30, 40);
	LineTo(40, 0);
	StrokeEnd();
	long height = 0;
	EXPECT(ExtrWordTrace_V(gTrace, gCount, 7, &height) == 0);
	EXPECT(height == 320);
	long lows = 0, highs = 0;
	for (long i = 0; i < gCount; i++)
	{
		if (gTrace[i].fFlags & kTraceLow)
		{
			lows++;
			EXPECT(gTrace[i].y == 320);
		}
		if (gTrace[i].fFlags & kTraceHigh)
		{
			highs++;
			EXPECT(gTrace[i].y == 0);
		}
	}
	EXPECT(lows == 2 && highs == 3);		// the start and the end are tops too

	// the height is the one past the middle of the strokes sorted by height
	TraceStart();
	MoveTo(0, 0);	LineTo(0, 10);	StrokeEnd();
	MoveTo(10, 0);	LineTo(10, 20);	StrokeEnd();
	MoveTo(20, 0);	LineTo(20, 30);	StrokeEnd();
	EXPECT(ExtrWordTrace_V(gTrace, gCount, 7, &height) == 0);
	EXPECT(height == 240);				// ROM QUIRK: not 160, the middle one
}


/*--------------------------------------------------------------------
	GetLineApprox: the polyline
--------------------------------------------------------------------*/

static void
TestApprox(void)
{
	long height = 0;
	tag_wapx_type* nodes = nil;

	// a straight upright: two nodes, down both ways
	TraceStart();
	MoveTo(10, 0);
	LineTo(10, 60);
	StrokeEnd();
	EXPECT(ExtrWordTrace_V(gTrace, gCount, 7, &height) == 0);
	long n = GetLineApprox(gTrace, gCount, 10, &nodes);
	Dump("upright", nodes, n);
	EXPECT(n == 2);
	if (n == 2)
	{
		EXPECT(nodes[0].fFlags & kApxStart && nodes[1].fFlags & kApxEnd);
		EXPECT(nodes[0].y == 0 && nodes[1].y == 480);
		EXPECT(nodes[0].fDir == 12 || nodes[0].fDir == 11);
		EXPECT(nodes[1].fDirOut == nodes[0].fDirOut);
	}
	HWRMemoryFree((Ptr) nodes);

	// a 4: the first stroke's corner at the bottom left found, as a corner
	TraceStart();
	DrawFour(0, 0);
	EXPECT(ExtrWordTrace_V(gTrace, gCount, 7, &height) == 0);
	n = GetLineApprox(gTrace, gCount, 10, &nodes);
	Dump("four", nodes, n);
	EXPECT(n >= 5);
	long corners = 0, starts = 0, ends = 0;
	for (long k = 0; k < n; k++)
	{
		if (nodes[k].fFlags & kApxCorner)
		{
			corners++;
			EXPECT(nodes[k].x <= 16 && nodes[k].y >= 100);			// the bottom left
		}
		if (nodes[k].fFlags & kApxStart)
			starts++;
		if (nodes[k].fFlags & kApxEnd)
			ends++;
	}
	EXPECT(corners == 1 && starts == 2 && ends == 2);
	HWRMemoryFree((Ptr) nodes);

	// a 2: the hook comes out as several nodes, the foot's corner found
	TraceStart();
	DrawTwo(0, 5);
	EXPECT(ExtrWordTrace_V(gTrace, gCount, 7, &height) == 0);
	n = GetLineApprox(gTrace, gCount, 10, &nodes);
	Dump("two", nodes, n);
	EXPECT(n >= 5);
	Boolean foot = false;
	for (long k = 0; k < n; k++)
		if ((nodes[k].fFlags & kApxCorner) && nodes[k].x <= 16 && nodes[k].y >= 196)
			foot = true;
	EXPECT(foot);
	HWRMemoryFree((Ptr) nodes);
}


/*--------------------------------------------------------------------
	LO_*: the list of low objects
--------------------------------------------------------------------*/

static void
TestLowObjects(void)
{
	// five nodes in two chunks (1, 1, 1 continuing into 2, 2)
	tag_wapx_type nodes[5];
	memset(nodes, 0, sizeof(nodes));
	const int32_t xs[5] = { 10, 20, 30, 25, 5 }, ys[5] = { 0, 40, 10, 50, 5 }, chunk[5] = { 1, 1, -1, 2, 2 };
	for (long k = 0; k < 5; k++)
	{
		nodes[k].fIndex = (int32_t) (k * 10);
		nodes[k].x = xs[k];
		nodes[k].y = ys[k];
		nodes[k].f18 = chunk[k];
	}
	tag_CHUNK chunks[2];
	memset(chunks, 0, sizeof(chunks));
	chunks[0].fRealIndex = 7;
	chunks[1].fRealIndex = 8;

	void* lo = LO_Create();
	EXPECT(lo != nil);
	if (lo == nil)
		return;
	EXPECT(LO_Clear(lo) == 1);
	EXPECT(LO_Add(lo, nodes, 300, 0, 4, 11, 22) == 0);
	EXPECT(LO_Add(lo, nodes, 300, 1, 2, 12, 23) == 1);
	EXPECT(LO_Add(lo, nodes, 1900, 3, 4, 13, 24) == 2);
	EXPECT(LO_Add(lo, nodes, 999, 3, 4, 13, 24) == -1);		// not a class
	LOBlock* block = (LOBlock*) lo;
	EXPECT(block->fCount == 3 && block->fFree == 297);
	EXPECT(block->fClasses[2].fCount == 2 && block->fClasses[16].fCount == 1);

	tag_LOWOBJ* obj = nil;
	EXPECT(LO_PickFirst(lo, &obj) == 0 && obj == nil);			// no class worked in yet
	EXPECT(LO_SetWorkClass(lo, 300) == 1 && LO_GetWorkClassID(lo) == 300);
	EXPECT(LO_PickFirst(lo, &obj) == 1 && obj != nil);
	if (obj != nil)
	{
		EXPECT(obj->fChunks == 2);								// nodes 0..4: chunks 1 and 2
		EXPECT(obj->fLeft == 5 && obj->fRight == 30 && obj->fTop == 0 && obj->fBottom == 50);
		EXPECT(obj->fFirstPoint == 0 && obj->fLastPoint == 40 && obj->fValue == 11 && obj->fExtra == 22);
		EXPECT(LO_HowManyChunks(lo, obj) == 2);
		EXPECT(LO_GetRealChunkInd(lo, chunks, nodes, obj, 1) == 7);
		EXPECT(LO_GetRealChunkInd(lo, chunks, nodes, obj, 2) == 8);
		EXPECT(LO_GetRealChunkInd(lo, chunks, nodes, obj, 3) == -1);
	}
	EXPECT(LO_PickNext(lo, &obj) == 1 && obj != nil && obj->fFrom == 1 && obj->fChunks == 1);
	EXPECT(LO_PickNext(lo, &obj) == 0 && obj == nil);
	EXPECT(LO_SetWorkClass(lo, 1900) == 1);
	EXPECT(LO_PickFirst(lo, &obj) == 1 && obj != nil && obj->fClass == 1900);
	// a node chunk-less at the start of its span (negative) is not counted
	if (obj != nil)
		EXPECT(obj->fChunks == 1);
	EXPECT(LO_PickDirectInd(lo, 1, &obj) == 1 && obj->fFrom == 1);
	EXPECT(LO_PickDirectInd(lo, 3, &obj) == 0 && obj == nil);
	EXPECT(LO_Destroy(lo) == 1);
}


/*--------------------------------------------------------------------
	ChunkConstruct: the chunks, strokes, brackets and classes
--------------------------------------------------------------------*/

// the trace as it is drawn, through the turns and the polyline into a staff
static bool
Construct(tag_CHUNK_STAFF* staff, const char* what)
{
	memset(staff, 0, sizeof(*staff));
	long height = 0;
	if (ExtrWordTrace_V(gTrace, gCount, 7, &height) != 0)
		return false;
	staff->fTrace = gTrace;
	staff->fTraceCount = (int32_t) gCount;
	staff->fNodeCount = (int32_t) GetLineApprox(gTrace, gCount, 10, &staff->fNodes);
	if (staff->fNodeCount <= 0)
		return false;
	long chunks = ChunkConstruct(staff);
	if (gVerbose)
	{
		Dump(what, staff->fNodes, staff->fNodeCount);
		printf("  %ld chunks, %d strokes, %d brackets, %d real\n", chunks, staff->fStrokeCount, staff->fBracketCount, staff->fRealCount);
		for (long k = 0; k < staff->fChunkCount; k++)
		{
			tag_CHUNK* c = &staff->fChunks[k];
			printf("  chunk %ld: nodes %d-%d kind %d dir %2d box (%d,%d)-(%d,%d) prev %d next %d real %d stroke %d class %d/%d brackets %d-%d\n",
				k, c->fFrom, c->fTo, c->fKind, c->fDir, c->fLeft, c->fTop, c->fRight, c->fBottom, c->fPrev, c->fNext,
				c->fRealIndex, c->fStroke, c->f74, c->f78, c->fFirstBracket, c->fLastBracket);
		}
		for (long k = 0; k < staff->fBracketCount; k++)
		{
			brack_type* b = &staff->fBrackets[k];
			printf("  bracket %ld: chunk %d nodes %d-%d kind %d sign %2d l2 %d h2 %d\n", k, b->fChunk, b->fFrom, b->fTo, b->fKind, b->fSign, b->fLength2, b->fHeight2);
		}
	}
	return chunks > 0;
}

static void
Destruct(tag_CHUNK_STAFF* staff)
{
	ChunkDestroyData(staff);
	EXPECT(staff->fChunks == nil && staff->fBrackets == nil && staff->fStrokes == nil && staff->fRealChunks == nil);
	HWRMemoryFree((Ptr) staff->fNodes);
}

static void
TestConstruct(void)
{
	tag_CHUNK_STAFF staff;

	// a 4: two strokes; the first is two chunks (down the slant, then
	// the bar), the jump between the strokes a chunk of its own
	TraceStart();
	DrawFour(0, 0);
	EXPECT(Construct(&staff, "four"));
	EXPECT(staff.fStrokeCount == 2);
	long jumps = 0, real = 0;
	for (long k = 0; k < staff.fChunkCount; k++)
		if (staff.fChunks[k].fKind == 3)
			jumps++;
		else
		{
			EXPECT(staff.fChunks[k].fRealIndex == real);
			EXPECT(staff.fRealChunks[real] == k);
			real++;
		}
	EXPECT(jumps == 1 && real == staff.fRealCount);
	// the upright is one straight line going down: class 300
	tag_CHUNK* upright = &staff.fChunks[staff.fChunkCount - 1];
	EXPECT(upright->fKind == 2 && upright->f74 == 300 && upright->fStroke == 1);
	EXPECT(upright->fFirstBracket == upright->fLastBracket);
	// the chunks put in the list of low objects by their classes
	void* lo = LO_Create();
	EXPECT(ChunkPutClassesToLO(lo, staff.fNodes, staff.fChunks, staff.fChunkCount) == 2);
	LOBlock* block = (LOBlock*) lo;
	EXPECT(block->fClasses[2].fCount == 1 && block->fClasses[5].fCount == 1);	// 300 and 600
	EXPECT(staff.fChunks[1].f70 == -1 && upright->f70 == 1);
	tag_LOWOBJ* obj = nil;
	EXPECT(LO_SetWorkClass(lo, 300) == 1 && LO_PickFirst(lo, &obj) == 1);
	if (obj != nil)
	{
		EXPECT(obj->fValue == 301 && obj->fChunks == 1);
		EXPECT(staff.fRealChunks[LO_GetRealChunkInd(lo, staff.fChunks, staff.fNodes, obj, 1)] == staff.fChunkCount - 1);
	}
	LO_Destroy(lo);
	tag_BOX r;
	EXPECT(DefRectForChunks(staff.fChunks, staff.fNodes, 0, staff.fChunkCount - 1, &r) == 1);
	EXPECT(r.left == 0 && r.top == 0 && r.right == 112 && r.bottom == 160);
	Destruct(&staff);

	// a 2: one stroke; the hook over the top an arc
	TraceStart();
	DrawTwo(0, 5);
	EXPECT(Construct(&staff, "two"));
	EXPECT(staff.fStrokeCount == 1);
	Boolean arc = false;
	for (long k = 0; k < staff.fBracketCount; k++)
		if (staff.fBrackets[k].fKind == 2)
			arc = true;
	EXPECT(arc);
	Destruct(&staff);
}


/*--------------------------------------------------------------------
	Digits' first passes: the line, the circles
--------------------------------------------------------------------*/

static long
CountClass(void* lo, ULong cls)
{
	tag_LOWOBJ* obj = nil;
	long n = 0;
	if (LO_SetWorkClass(lo, cls) == 1)
		for (long more = LO_PickFirst(lo, &obj); more; more = LO_PickNext(lo, &obj))
			n++;
	return n;
}

// a 5: down the left, round the bowl, then the bar as a stroke of its own
static void
DrawFive(double x, double y)
{
	MoveTo(x + 2, y);
	LineTo(x + 1, y + 9);
	ArcTo(x + 7, y + 14, 6, 120, -150);
	StrokeEnd();
	MoveTo(x + 2, y);
	LineTo(x + 13, y);
	StrokeEnd();
}

// a 9: the loop anticlockwise from the right, then the tail down
static void
DrawNine(double x, double y)
{
	MoveTo(x + 12, y + 3);
	EllipseTo(x + 7, y + 6, 6, 6, 30, 390);
	LineTo(x + 12, y + 20);
	StrokeEnd();
}

// the digits the searchers found: class 1300, value 1400 + the digit
static long
Digits1300(void* lo, long* digits, long max)
{
	tag_LOWOBJ* obj = nil;
	long k = 0;
	if (LO_SetWorkClass(lo, 1300) == 1)
		for (long more = LO_PickFirst(lo, &obj); more && k < max; more = LO_PickNext(lo, &obj))
			digits[k++] = obj->fValue - 1400 + (obj->fExtra << 8);
	return k;
}

static long
SearchL(const char* what, long* digits, long max)
{
	tag_CHUNK_STAFF staff;
	long found = 0;
	if (!Construct(&staff, what))
		return -1;
	void* lo = LO_Create();
	staff.fLO = lo;
	DefHeightsForNumber(&staff);
	ChunkPutClassesToLO(lo, staff.fNodes, staff.fChunks, staff.fChunkCount);
	GetCircles(&staff);
	SearchDigit_L(&staff);
	found = Digits1300(lo, digits, max);
	if (gVerbose)
	{
		printf("  %s: %ld digits:", what, found);
		for (long k = 0; k < found; k++)
			printf(" %ld(kind %ld)", digits[k] & 0xff, digits[k] >> 8);
		printf("\n");
	}
	LO_Destroy(lo);
	Destruct(&staff);
	return found;
}

// a 2 started at its top left, so its first chunk goes down all the way
static void
DrawTwoFlat(double x, double y)
{
	MoveTo(x + 1, y + 3);
	ArcTo(x + 7, y + 6, 6, 155, -30);
	LineTo(x, y + 20);
	LineTo(x + 14, y + 20);
	StrokeEnd();
}

// a 3 started at its top left, one stroke down round both bowls
static void
DrawThreeFlat(double x, double y)
{
	MoveTo(x + 1, y + 2);
	ArcTo(x + 7, y + 5, 5, 150, -90);
	ArcTo(x + 7, y + 15, 5, 90, -160);
	StrokeEnd();
}

// a 5 in one stroke: the bar right to left, down, round the bowl
static void
DrawFiveOne(double x, double y)
{
	MoveTo(x + 13, y);
	LineTo(x + 2, y);
	LineTo(x + 1, y + 9);
	ArcTo(x + 7, y + 14, 6, 120, -150);
	StrokeEnd();
}

// a 7 whose bar and stem are one curve down
static void
DrawSevenOne(double x, double y)
{
	MoveTo(x, y + 1);
	LineTo(x + 13, y);
	LineTo(x + 9, y + 8);
	LineTo(x + 5, y + 20);
	StrokeEnd();
}

// a $: an S from its top right down round to its bottom left, then the
// upright through it
static void
DrawDollar(double x, double y)
{
	MoveTo(x + 12, y + 4);
	ArcTo(x + 7, y + 6, 5, 25, 270);
	ArcTo(x + 7, y + 16, 5, 90, -155);
	StrokeEnd();
	MoveTo(x + 7, y - 2);
	LineTo(x + 7, y + 23);
	StrokeEnd();
}

// a 5 whose bar is written first and the pen brought back along it
static void
DrawFiveBarBack(double x, double y)
{
	MoveTo(x + 2, y);
	LineTo(x + 13, y);
	LineTo(x + 2, y + 1);
	LineTo(x + 1, y + 9);
	ArcTo(x + 7, y + 14, 6, 120, -150);
	StrokeEnd();
}

// a 6: from the top right down the left, round the bottom and up into the
// loop
static void
DrawSix(double x, double y)
{
	MoveTo(x + 12, y + 3);
	ArcTo(x + 7, y + 6, 5, 35, 180);
	LineTo(x + 2, y + 15);
	ArcTo(x + 7, y + 15, 5, 180, 450);
	StrokeEnd();
}

// a digit put in the list by hand over the whole of the writing
static tag_LOWOBJ*
DigitOver(void* lo, tag_CHUNK_STAFF* staff, long value)
{
	long obj = LO_Add(lo, staff->fNodes, 1300, 0, staff->fNodeCount - 1, (ULong) value, 1);
	tag_LOWOBJ* o = nil;
	LO_PickDirectInd(lo, obj, &o);
	return o;
}

static void
TestSearchL(void)
{
	// SearchDigit_L starts from a chunk that is a curve down and round
	// (class 500, value 501): the 5, the $ - whose S is one
	long d[8];
	// a 5 with its bar a stroke of its own: found with the bar (kind 2)
	TraceStart();
	DrawFive(0, 0);
	EXPECT(SearchL("5", d, 8) == 1 && d[0] == (5 | 2 << 8));
	// a 5 in one stroke: read from its curve, the bar its start (kind 3)
	TraceStart();
	DrawFiveOne(0, 0);
	EXPECT(SearchL("5 one", d, 8) == 1 && d[0] == (5 | 3 << 8));
	// a $: the S with an upright through it, found whole (0x15, kind 1)
	TraceStart();
	DrawDollar(0, 0);
	EXPECT(SearchL("$", d, 8) == 1 && d[0] == (0x15 | 1 << 8));
	// a 2 whose body turns the other way (value 502), a 3 of two arcs
	// (class 700), a 4, a 7 and a 9 are other searchers' work
	TraceStart(); DrawTwoFlat(0, 0); EXPECT(SearchL("2 flat", d, 8) == 0);
	TraceStart(); DrawThreeFlat(0, 0); EXPECT(SearchL("3 flat", d, 8) == 0);
	TraceStart(); DrawFour(0, 0); EXPECT(SearchL("4", d, 8) == 0);
	TraceStart(); DrawSevenOne(0, 0); EXPECT(SearchL("7 one", d, 8) == 0);
	TraceStart(); DrawNine(0, 0); EXPECT(SearchL("9", d, 8) == 0);
	TraceStart(); DrawZero(0, 0); EXPECT(SearchL("0", d, 8) == 0);
}


static void
TestSecondLooks(void)
{
	tag_CHUNK_STAFF staff;
	// crossing segments
	EXPECT(CheckQIntersecXY(0, 0, 10, 10, 0, 10, 10, 0) == 1);
	EXPECT(CheckQIntersecXY(0, 0, 10, 0, 0, 5, 10, 5) == 0);
	EXPECT(CheckQIntersecXY(0, 0, 10, 10, 5, 5, 20, 0) == 1);		// touching counts

	TraceStart();
	DrawFiveBarBack(0, 0);
	EXPECT(Construct(&staff, "5 bar back"));
	void* lo = LO_Create();
	tag_LOWOBJ* obj = DigitOver(lo, &staff, 1403);
	EXPECT(obj != nil);
	if (obj != nil)
	{
		EXPECT(ThreeToFive(lo, staff.fChunks, staff.fNodes, staff.fRealChunks, &obj, 1) == 1);
		EXPECT(obj->fValue == 0x519);		// the 3 is a 5 whose bar was not lifted
		if (gVerbose)
			printf("  5 bar back as a 3: value %d\n", obj->fValue);
	}
	EXPECT(ThreeToFive(lo, staff.fChunks, staff.fNodes, staff.fRealChunks, &obj, 0) == 0);
	LO_Destroy(lo);
	Destruct(&staff);

	TraceStart();
	DrawSix(0, 0);
	EXPECT(Construct(&staff, "6"));
	lo = LO_Create();
	// its first chunk is a plain arc down: RecognizeZCCW leaves it be
	obj = DigitOver(lo, &staff, 1402);
	EXPECT(RecognizeZCCW(lo, staff.fChunks, staff.fNodes, staff.fRealChunks, &obj, 1) == 1 && obj->fValue == 1402);
	// taken as a curve that turns the other way (value 502), a 2 over it
	// narrower than the arc up is tall becomes a 6; a 3 stays
	EXPECT(staff.fChunkCount == 2 && staff.fChunks[0].f78 == 402 && staff.fChunks[1].f78 == 402);
	staff.fChunks[0].f78 = 502;
	tag_LOWOBJ* three = DigitOver(lo, &staff, 1403);
	EXPECT(RecognizeZCCW(lo, staff.fChunks, staff.fNodes, staff.fRealChunks, &three, 1) == 1 && three->fValue == 1403);
	if (obj != nil)
	{
		EXPECT(RecognizeZCCW(lo, staff.fChunks, staff.fNodes, staff.fRealChunks, &obj, 1) == 1);
		EXPECT(obj->fValue == 0x51a);
		if (gVerbose)
			printf("  6 as a 2: value %d\n", obj->fValue);
	}
	LO_Destroy(lo);
	Destruct(&staff);
}

static void
TestLineAndCircles(void)
{
	tag_CHUNK_STAFF staff;

	// a 4 and a 2 side by side, both 20 pixels (160 eighths) high from y 0
	TraceStart();
	DrawFour(0, 0);
	DrawTwo(20, 0);
	EXPECT(Construct(&staff, "four two"));
	DefHeightsForNumber(&staff);
	if (gVerbose)
		printf("  line: top %d bottom %d height %d\n", staff.fTopLine, staff.fBottomLine, staff.fHeight);
	EXPECT(staff.fTopLine >= 0 && staff.fTopLine <= 8);
	EXPECT(staff.fBottomLine >= 152 && staff.fBottomLine <= 160);
	EXPECT(staff.fHeight == staff.fBottomLine - staff.fTopLine);
	// the 4's upright starts at the top of the line and ends at its foot
	tag_CHUNK* upright = &staff.fChunks[2];
	EXPECT(upright->fKind == 2 && upright->fPrev == -1);
	EXPECT(upright->fZoneStart == 60 && upright->fZoneEnd == 30);
	Destruct(&staff);

	// a 0 is a circle, class 200, from the chunk going down
	TraceStart();
	DrawZero(0, 0);
	EXPECT(Construct(&staff, "zero"));
	void* lo = LO_Create();
	staff.fLO = lo;
	DefHeightsForNumber(&staff);
	EXPECT(ChunkPutClassesToLO(lo, staff.fNodes, staff.fChunks, staff.fChunkCount) > 0);
	EXPECT(GetCircles(&staff) == 1);
	EXPECT(CountClass(lo, 200) == 1);
	long circles = 0;
	for (long k = 0; k < staff.fChunkCount; k++)
		if (staff.fChunks[k].f7C >= 0)
			circles++;
	EXPECT(circles == 1);
	LO_Destroy(lo);
	Destruct(&staff);

	// neither a 1 nor a 4 has one
	TraceStart();
	DrawOne(0, 0);
	DrawFour(12, 0);
	EXPECT(Construct(&staff, "one four"));
	lo = LO_Create();
	staff.fLO = lo;
	DefHeightsForNumber(&staff);
	ChunkPutClassesToLO(lo, staff.fNodes, staff.fChunks, staff.fChunkCount);
	GetCircles(&staff);
	EXPECT(CountClass(lo, 200) == 0);
	LO_Destroy(lo);
	Destruct(&staff);
}


int
main(int argc, char** argv)
{
	gVerbose = argc > 1;
	InitHostStandaloneHeap();
	TestTurns();
	TestApprox();
	TestLowObjects();
	TestConstruct();
	TestLineAndCircles();
	TestSearchL();
	TestSecondLooks();
	if (failures == 0)
		printf("test_Chunk: all passed\n");
	return failures == 0 ? 0 : 1;
}
