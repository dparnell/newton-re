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

/*--------------------------------------------------------------------
	Digits' second-look pass (DigitsSecondLooks, ROM 0x0029ce20)
--------------------------------------------------------------------*/

// a digit put in the list over one stroke's nodes
static tag_LOWOBJ*
DigitOverStroke(void* lo, tag_CHUNK_STAFF* staff, long stroke, long value, long extra = 1, long lastStroke = -1)
{
	long from = staff->fChunks[staff->fStrokes[stroke].fFirstChunk].fFrom;
	long to = staff->fChunks[staff->fStrokes[lastStroke < 0 ? stroke : lastStroke].fLastChunk].fTo;
	tag_LOWOBJ* o = nil;
	LO_PickDirectInd(lo, LO_Add(lo, staff->fNodes, 1300, from, to, (ULong) value, extra), &o);
	return o;
}

// the pass run with every variant allowed (unless given), the class-1900
// objects it wrote out read back in order as their values
static long
SecondLooks(void* lo, tag_CHUNK_STAFF* staff, long* values, long max, const UByte* allowed = nil)
{
	UByte all[10];
	memset(all, 0xff, sizeof(all));
	tag_BOX box;
	DefRectForChunks(staff->fChunks, staff->fNodes, 0, staff->fChunkCount - 1, &box);
	long made = DigitsSecondLooks(lo, staff->fChunks, staff->fRealChunks, staff->fStrokes, staff->fStrokeCount,
		staff->fNodes, allowed != nil ? allowed : all, box);
	tag_LOWOBJ* obj = nil;
	long k = 0;
	if (LO_SetWorkClass(lo, 1900) == 1)
		for (long more = LO_PickFirst(lo, &obj); more && k < max; more = LO_PickNext(lo, &obj))
			values[k++] = obj->fValue;
	if (gVerbose)
	{
		printf("  second looks: %ld made:", made);
		for (long i = 0; i < k; i++)
			printf(" %ld", values[i]);
		printf("\n");
	}
	EXPECT(k == made);
	return made;
}

// a stroke straight from one point to another
static void
DrawLine(double x0, double y0, double x1, double y1)
{
	MoveTo(x0, y0);
	LineTo(x1, y1);
	StrokeEnd();
}

// a "<", 8 pixels wide and 12 high
static void
DrawLess(double x, double y)
{
	MoveTo(x + 8, y);
	LineTo(x, y + 6);
	LineTo(x + 8, y + 12);
	StrokeEnd();
}

static void
TestSecondLookPass(void)
{
	tag_CHUNK_STAFF staff;
	long v[8];

	// sorted by where they start, whatever order they were found in; a gap
	// (class 1200) between them written out in its place, value 0xffff
	TraceStart();
	DrawOne(0, 0);
	DrawFour(20, 0);
	EXPECT(Construct(&staff, "1 4"));
	void* lo = LO_Create();
	DigitOverStroke(lo, &staff, 1, 1404);		// (the 4's first stroke only)
	DigitOverStroke(lo, &staff, 0, 1381);
	LO_Add(lo, staff.fNodes, 1200, 0, 0, 0, -1);
	tag_LOWOBJ* gap = nil;
	LO_SetWorkClass(lo, 1200);
	LO_PickFirst(lo, &gap);
	if (gap != nil)
	{
		gap->fLeft = 80;			// between the 1 (x 40) and the 4 (from x 160)
		gap->fRight = 150;
	}
	EXPECT(SecondLooks(lo, &staff, v, 8) == 3);
	EXPECT(v[0] == 1381 && v[1] == 0xffff && v[2] == 1404);
	LO_Destroy(lo);
	Destruct(&staff);

	// a "1" hanging low between two 4s is a comma; a shorter one a full stop
	TraceStart();
	DrawFour(0, 0);
	DrawLine(17, 19, 17, 27);
	DrawFour(22, 0);
	EXPECT(Construct(&staff, "4 , 4"));
	lo = LO_Create();
	DigitOverStroke(lo, &staff, 0, 1404, 1, 1);
	DigitOverStroke(lo, &staff, 2, 1301);
	DigitOverStroke(lo, &staff, 3, 1404, 1, 4);
	EXPECT(SecondLooks(lo, &staff, v, 8) == 3);
	EXPECT(v[0] == 1404 && v[1] == 1319 && v[2] == 1404);
	LO_Destroy(lo);
	Destruct(&staff);
	TraceStart();
	DrawFour(0, 0);
	DrawLine(17, 19, 17, 24);
	DrawFour(22, 0);
	EXPECT(Construct(&staff, "4 . 4"));
	lo = LO_Create();
	DigitOverStroke(lo, &staff, 0, 1404, 1, 1);
	DigitOverStroke(lo, &staff, 2, 1301);
	DigitOverStroke(lo, &staff, 3, 1404, 1, 4);
	EXPECT(SecondLooks(lo, &staff, v, 8) == 3);
	EXPECT(v[1] == 1314);
	LO_Destroy(lo);
	Destruct(&staff);

	// a "1" slanting and taller than the digits beside it is a solidus
	TraceStart();
	DrawFour(0, 0);
	DrawLine(30, -6, 20, 26);
	EXPECT(Construct(&staff, "4 /"));
	lo = LO_Create();
	DigitOverStroke(lo, &staff, 0, 1404);
	DigitOverStroke(lo, &staff, 2, 1301);
	tag_CHUNK* slash = &staff.fChunks[staff.fStrokes[2].fFirstChunk];
	if (gVerbose)
		printf("  slash: class %d dir %d height %d\n", slash->f74, slash->fDir, slash->fHeight);
	EXPECT(slash->f74 == 300 && slash->fDir >= 7 && slash->fDir <= 11);
	EXPECT(SecondLooks(lo, &staff, v, 8) == 2);
	EXPECT(v[1] == 1316);
	LO_Destroy(lo);
	Destruct(&staff);

	// a "-" high against the upright stroke before it, which is no digit,
	// is its bar: taken out
	TraceStart();
	DrawLine(0, 0, 0, 20);
	DrawLine(1, 1, 8, 1);
	EXPECT(Construct(&staff, "| -"));
	lo = LO_Create();
	DigitOverStroke(lo, &staff, 1, 1313);
	EXPECT(SecondLooks(lo, &staff, v, 8) == 0);
	LO_Destroy(lo);
	Destruct(&staff);

	// two "<"s side by side are one guillemet over both
	TraceStart();
	DrawLess(0, 4);
	DrawLess(10, 4);
	EXPECT(Construct(&staff, "< <"));
	lo = LO_Create();
	tag_LOWOBJ* first = DigitOverStroke(lo, &staff, 0, 1324);
	tag_LOWOBJ* second = DigitOverStroke(lo, &staff, 1, 1324);
	EXPECT(SecondLooks(lo, &staff, v, 8) == 1);
	EXPECT(v[0] == 1325 && first->fValue == 0xffff);
	EXPECT(second->fFrom == first->fFrom && second->fLeft == first->fLeft);
	LO_Destroy(lo);
	Destruct(&staff);

	// a 3 after an upright line through its bowl is a "B", made over both
	TraceStart();
	DrawLine(5, 0, 5, 20);
	DrawThreeFlat(0, 0);
	EXPECT(Construct(&staff, "| 3"));
	lo = LO_Create();
	DigitOverStroke(lo, &staff, 1, 1303);
	EXPECT(SecondLooks(lo, &staff, v, 8) == 1);
	EXPECT(v[0] == 1330);
	LO_Destroy(lo);
	Destruct(&staff);

	// a 4 in two strokes whose upright ends lowest is the letter table's
	// first variant: kept when the field allows it, taken out otherwise
	UByte allowed[10];
	memset(allowed, 0xff, sizeof(allowed));
	for (int pass = 0; pass < 2; pass++)
	{
		allowed[4] = pass == 0 ? 0x01 : 0x02;
		TraceStart();
		DrawFour(0, 0);
		EXPECT(Construct(&staff, "4 variant"));
		lo = LO_Create();
		long from = staff.fChunks[0].fFrom, to = staff.fChunks[staff.fChunkCount - 1].fTo;
		LO_Add(lo, staff.fNodes, 1300, from, to, 1404, 1);
		EXPECT(SecondLooks(lo, &staff, v, 8, allowed) == (pass == 0 ? 1 : 0));
		LO_Destroy(lo);
		Destruct(&staff);
	}
}

/*--------------------------------------------------------------------
	SearchDigit_K: the digits of lines and arcs
--------------------------------------------------------------------*/

// the digits SearchDigit_K found: class 1300, value 1500 + the digit, with
// the extra saying how
static long
SearchK(const char* what, long* digits, long max)
{
	tag_CHUNK_STAFF staff;
	if (!Construct(&staff, what))
		return -1;
	void* lo = LO_Create();
	staff.fLO = lo;
	DefHeightsForNumber(&staff);
	ChunkPutClassesToLO(lo, staff.fNodes, staff.fChunks, staff.fChunkCount);
	GetCircles(&staff);
	SearchDigit_K(&staff);
	tag_LOWOBJ* obj = nil;
	long k = 0;
	if (LO_SetWorkClass(lo, 1300) == 1)
		for (long more = LO_PickFirst(lo, &obj); more && k < max; more = LO_PickNext(lo, &obj))
			if (obj->fValue != 0xffff)
				digits[k++] = (obj->fValue - 1500) + ((obj->fExtra & 0xff) << 8);
	if (gVerbose)
	{
		printf("  %s: %ld digits:", what, k);
		for (long i = 0; i < k; i++)
			printf(" %ld(extra %ld)", digits[i] & 0xff, digits[i] >> 8);
		printf("\n");
	}
	LO_Destroy(lo);
	Destruct(&staff);
	return k;
}

static void
TestSearchK(void)
{
	long d[8];
	// a 4: the slant and bar in one stroke, the upright a stroke of its own
	TraceStart();
	DrawFour(0, 0);
	EXPECT(SearchK("4", d, 8) == 1 && d[0] == (4 | 1 << 8));		// a 4, the letter table's first shape
	// an x: two strokes crossing
	TraceStart();
	DrawLine(0, 0, 14, 20);
	DrawLine(14, 0, 0, 20);
	EXPECT(SearchK("x", d, 8) == 1 && d[0] == (69 | 9 << 8));
	// a 7 with a sharp corner, its bar rising a little as a hand makes it
	TraceStart();
	MoveTo(0, 5);
	LineTo(13, 0);
	LineTo(9, 20);
	StrokeEnd();
	EXPECT(SearchK("7", d, 8) == 1 && d[0] == (7 | 6 << 8));
	// a #
	TraceStart();
	DrawLine(4, 0, 3, 20);
	DrawLine(11, 0, 10, 20);
	DrawLine(0, 6, 15, 6);
	DrawLine(0, 14, 15, 14);
	EXPECT(SearchK("#", d, 8) == 1 && d[0] == (71 | 0xff << 8));		// (extra -1)
	// an 8 in one stroke: down round the top loop to the right, across and
	// round the bottom loop, back up to the start
	TraceStart();
	MoveTo(12, 3);
	ArcTo(7, 5, 5, 25, 180);
	LineTo(12, 15);
	ArcTo(7, 15, 5, 0, -180);
	LineTo(12, 3);
	StrokeEnd();
	EXPECT(SearchK("8", d, 8) == 1 && d[0] == (8 | 5 << 8));
	// a per cent sign: a small ring, the slash, a small ring
	TraceStart();
	MoveTo(3, 1);
	EllipseTo(3, 4, 3, 3, 90, 450);
	StrokeEnd();
	DrawLine(13, 0, 3, 20);
	MoveTo(13, 16);
	EllipseTo(13, 19, 3, 3, 90, 450);
	StrokeEnd();
	EXPECT(SearchK("%", d, 8) == 1 && d[0] == (17 | 7 << 8));
	// a 1 on its own is not this searcher's
	TraceStart();
	DrawOne(0, 0);
	EXPECT(SearchK("1", d, 8) == 0);
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

	// Check_4: a "4" (value 0x605) kept while it stands alone, taken out
	// once another digit shares its chunks
	TraceStart();
	DrawFour(0, 0);
	DrawTwo(20, 0);
	EXPECT(Construct(&staff, "4 2 for Check_4"));
	lo = LO_Create();
	staff.fLO = lo;
	tag_LOWOBJ* four = nil;
	LO_PickDirectInd(lo, LO_Add(lo, staff.fNodes, 1300, 0, 4, 0x605, 1), &four);
	LO_Add(lo, staff.fNodes, 1300, 5, staff.fNodeCount - 1, 1402, 1);
	Check_4(&staff);
	EXPECT(four != nil && four->fValue == 0x605);
	LO_Add(lo, staff.fNodes, 1300, 0, 4, 1407, 1);
	Check_4(&staff);
	EXPECT(four != nil && four->fValue == 0xffff);
	LO_Destroy(lo);
	Destruct(&staff);
}


/*--------------------------------------------------------------------
	New_SearchDigit_V: the chunks asked one by one what they start.
--------------------------------------------------------------------*/

// the digits V found (class 1300, value 1300 + the digit or the sign's
// code, the extra saying which test) and the grey ones (class 2200), run
// as Digits runs it after the other searchers' preparation - with the
// writing's box and, for the height Digits is given, the line's
static long
SearchV(const char* what, long* digits, long max, long* grey = nil)
{
	tag_CHUNK_STAFF staff;
	if (!Construct(&staff, what))
		return -1;
	void* lo = LO_Create();
	staff.fLO = lo;
	DefHeightsForNumber(&staff);
	ChunkPutClassesToLO(lo, staff.fNodes, staff.fChunks, staff.fChunkCount);
	GetCircles(&staff);
	tag_BOX box = { 0x7fff, 0x7fff, -0x7fff, -0x7fff };
	for (long k = 0; k < staff.fNodeCount; k++)
	{
		tag_wapx_type* nd = &staff.fNodes[k];
		if (nd->x < box.left) box.left = nd->x;
		if (nd->x > box.right) box.right = nd->x;
		if (nd->y < box.top) box.top = nd->y;
		if (nd->y > box.bottom) box.bottom = nd->y;
	}
	New_SearchDigit_V(lo, staff.fTrace, staff.fTraceCount, staff.fNodes, staff.fChunks, staff.fBrackets,
					  staff.fRealChunks, staff.fChunkCount, staff.fRealCount, box, staff.fStrokes, staff.fStrokeCount, staff.fHeight);
	tag_LOWOBJ* obj = nil;
	long k = 0;
	if (LO_SetWorkClass(lo, 1300) == 1)
		for (long more = LO_PickFirst(lo, &obj); more && k < max; more = LO_PickNext(lo, &obj))
			digits[k++] = (obj->fValue - 1300) + ((obj->fExtra & 0xff) << 8);
	long g = 0;
	if (LO_SetWorkClass(lo, 2200) == 1)
		for (long more = LO_PickFirst(lo, &obj); more; more = LO_PickNext(lo, &obj))
			g++;
	if (grey != nil)
		*grey = g;
	if (gVerbose)
	{
		printf("  %s: %ld found:", what, k);
		for (long i = 0; i < k; i++)
			printf(" %ld(test %ld)", digits[i] & 0xff, digits[i] >> 8);
		printf("  grey %ld\n", g);
	}
	LO_Destroy(lo);
	Destruct(&staff);
	return k;
}

static void
TestSearchV(void)
{
	long d[8];
	// ComposeTrace: the nodes between pen-ups, their flags untouched
	tag_wapx_type nodes[4] = { };
	for (long k = 0; k < 4; k++)
	{
		nodes[k].x = (int32_t) (10 * k);
		nodes[k].y = (int32_t) (100 + k);
	}
	tag_WORD_TRACE t[6];
	for (long k = 0; k < 6; k++)
	{
		t[k].x = 7; t[k].y = 7; t[k].fFlags = 0x55;
	}
	EXPECT(ComposeTrace(nodes, 1, 2, t) == 1);
	EXPECT(t[0].x == -1 && t[0].y == -1 && t[1].x == 10 && t[1].y == 101 && t[2].x == 20 && t[2].y == 102
		&& t[3].x == -1 && t[3].y == -1 && t[1].fFlags == 0x55 && t[4].x == 7);

	// (d[k] is the value less 1300 - the digit or the sign's code - with
	// the test that found it in the second byte)
	TraceStart(); DrawOne(0, 0);
	EXPECT(SearchV("1", d, 8) == 1 && d[0] == (1 | 26 << 8));			// a 1 on its own
	TraceStart(); DrawZero(0, 0);
	EXPECT(SearchV("0", d, 8) == 1 && d[0] == (0 | 25 << 8));			// from its circle
	TraceStart(); DrawTwo(0, 0);
	EXPECT(SearchV("2", d, 8) == 1 && d[0] == (2 | 11 << 8));			// a curve down with its foot
	TraceStart(); DrawTwoFlat(0, 0);
	EXPECT(SearchV("2 flat", d, 8) == 1 && d[0] == (2 | 11 << 8));
	TraceStart(); DrawThreeFlat(0, 0);
	EXPECT(SearchV("3", d, 8) == 1 && d[0] == (3 | 3 << 8));			// an S
	TraceStart(); MoveTo(2, 2); ArcTo(7, 5, 5, 150, -90); ArcTo(7, 15, 5, 90, -180); StrokeEnd();
	EXPECT(SearchV("3b", d, 8) == 1 && d[0] == (3 | 3 << 8));
	TraceStart(); DrawNine(0, 0);
	EXPECT(SearchV("9", d, 8) == 1 && d[0] == (9 | 30 << 8));			// the horseshoe
	TraceStart(); MoveTo(0, 5); LineTo(13, 0); LineTo(9, 20); StrokeEnd();
	EXPECT(SearchV("7", d, 8) == 1 && d[0] == (7 | 28 << 8));			// the bar a hook before the upright
	TraceStart(); MoveTo(1, 0); LineTo(13, 0); LineTo(4, 20); StrokeEnd();
	EXPECT(SearchV("7b", d, 8) == 1 && d[0] == (7 | 20 << 8));			// an arc on its own
	TraceStart(); MoveTo(1, 5); LineTo(6, 0); LineTo(6, 20); StrokeEnd();
	EXPECT(SearchV("1 flag", d, 8) == 1 && d[0] == (7 | 28 << 8));		// a flag steep enough to be a 7's bar
	TraceStart(); MoveTo(10, 0); LineTo(2, 12); ArcTo(7, 15, 5, 180, 420); StrokeEnd();
	EXPECT(SearchV("6", d, 8) == 1 && d[0] == (6 | 12 << 8));			// an arc down closed by an arc up
	TraceStart(); MoveTo(10, 0); ArcTo(10, 13, 8, 90, 180); ArcTo(7, 15, 5, 180, 430); StrokeEnd();
	EXPECT(SearchV("6b", d, 8) == 1 && d[0] == (6 | 12 << 8));			// a curve down, then the same
	// the signs of two sections
	TraceStart(); MoveTo(14, 0); LineTo(0, 8); LineTo(14, 16); StrokeEnd();
	EXPECT(SearchV("<", d, 8) == 1 && d[0] == 24);
	TraceStart(); MoveTo(0, 0); LineTo(14, 8); LineTo(0, 16); StrokeEnd();
	EXPECT(SearchV(">", d, 8) == 2 && d[0] == 23 && d[1] == (7 | 20 << 8));	// (and a 7 of it)
	// a # of a zigzag and two lines (and a 1 of each line)
	TraceStart(); DrawLine(4, 0, 3, 20); DrawLine(11, 0, 10, 20); MoveTo(0, 6); LineTo(15, 6); LineTo(0, 14); LineTo(15, 14); StrokeEnd();
	EXPECT(SearchV("#", d, 8) == 3 && d[0] == (1 | 26 << 8) && d[1] == (1 | 26 << 8) && d[2] == 71);
	// what the other searchers read: a 4 is K's (V sees only its upright),
	// a 5 is L's, an 8 is K's, a line across is nobody's here
	TraceStart(); DrawFour(0, 0);
	EXPECT(SearchV("4", d, 8) == 1 && d[0] == (1 | 26 << 8));
	TraceStart(); DrawFive(0, 0);
	EXPECT(SearchV("5", d, 8) == 0);
	TraceStart(); DrawFiveOne(0, 0);
	EXPECT(SearchV("5 one", d, 8) == 0);
	TraceStart(); MoveTo(12, 3); ArcTo(7, 5, 5, 25, 180); LineTo(12, 15); ArcTo(7, 15, 5, 0, -180); LineTo(12, 3); StrokeEnd();
	EXPECT(SearchV("8", d, 8) == 0);
	TraceStart(); DrawLine(0, 10, 14, 10);
	EXPECT(SearchV("-", d, 8) == 0);
}


/*--------------------------------------------------------------------
	SearchNumber: the verdict, after the searchers Digits runs (those
	reconstructed: L, K, V, Check_4) and its second looks.
--------------------------------------------------------------------*/

// ==> SearchNumber's answer, *digits the digits the second looks wrote out
static long
IsNumber(const char* what, long* digits = nil, long* count = nil)
{
	tag_CHUNK_STAFF staff;
	if (!Construct(&staff, what))
		return -1;
	void* lo = LO_Create();
	staff.fLO = lo;
	memset(staff.fDigits, 0xff, sizeof(staff.fDigits));
	DefHeightsForNumber(&staff);
	ChunkPutClassesToLO(lo, staff.fNodes, staff.fChunks, staff.fChunkCount);
	GetCircles(&staff);
	SearchDigit_L(&staff);
	SearchDigit_K(&staff);
	tag_BOX box = { 0x7fff, 0x7fff, -0x7fff, -0x7fff };
	for (long k = 0; k < staff.fNodeCount; k++)
	{
		tag_wapx_type* nd = &staff.fNodes[k];
		if (nd->x < box.left) box.left = nd->x;
		if (nd->x > box.right) box.right = nd->x;
		if (nd->y < box.top) box.top = nd->y;
		if (nd->y > box.bottom) box.bottom = nd->y;
	}
	New_SearchDigit_V(lo, staff.fTrace, staff.fTraceCount, staff.fNodes, staff.fChunks, staff.fBrackets,
					  staff.fRealChunks, staff.fChunkCount, staff.fRealCount, box, staff.fStrokes, staff.fStrokeCount, staff.fHeight);
	// (SearchDigit_S and FindPound come here in Digits, and after Check_4
	// the statics that take the doubtful digits out - a 0 both V and S read
	// would otherwise be written out twice; this harness is SearchNumber's
	// own, the pieces before it in their order - TestDigits runs the whole)
	Check_4(&staff);
	DigitsSecondLooks(lo, staff.fChunks, staff.fRealChunks, staff.fStrokes, staff.fStrokeCount, staff.fNodes, staff.fDigits, box);
	long answer = SearchNumber(&staff);
	tag_LOWOBJ* obj = nil;
	long k = 0;
	if (LO_SetWorkClass(lo, 1900) == 1)
		for (long more = LO_PickFirst(lo, &obj); more; more = LO_PickNext(lo, &obj))
			if (obj->fValue != 0xffff && (ULong) obj->fValue % 100 <= 9)
			{
				if (digits != nil && k < 8)
					digits[k] = (long) ((ULong) obj->fValue % 100);
				k++;
			}
	if (count != nil)
		*count = k;
	if (gVerbose)
		printf("  %s: %ld digits, a number: %ld\n", what, k, answer);
	LO_Destroy(lo);
	Destruct(&staff);
	return answer;
}

static void
TestSearchNumber(void)
{
	long d[8], n = 0;
	// 42: K reads the 4, V the 2
	TraceStart(); DrawFour(0, 0); DrawTwo(22, 0);
	EXPECT(IsNumber("42", d, &n) == 1 && n == 2 && d[0] == 4 && d[1] == 2);
	// 10: V reads both
	TraceStart(); DrawOne(0, 0); DrawZero(14, 0);
	EXPECT(IsNumber("10", d, &n) == 1 && n == 2 && d[0] == 1 && d[1] == 0);
	// 2 1 7
	TraceStart(); DrawTwo(0, 0); DrawOne(18, 0); MoveTo(30, 5); LineTo(43, 0); LineTo(39, 20); StrokeEnd();
	EXPECT(IsNumber("217", d, &n) == 1 && n == 3 && d[0] == 2 && d[1] == 1 && d[2] == 7);
	// one digit alone is not judged a number
	TraceStart(); DrawTwo(0, 0);
	EXPECT(IsNumber("2", d, &n) == 0 && n == 1);
	// a 1 and a 0 whose bottoms step by more than half their height: the
	// step only marks it irregular (2), which a 1 and a 0 survive...
	TraceStart(); DrawOne(0, 0); DrawZero(14, 14);
	EXPECT(IsNumber("1 0 stepped", d, &n) == 1);
	// ...but a number of nothing but 1s does not
	TraceStart(); DrawOne(0, 0); DrawOne(14, 14);
	EXPECT(IsNumber("1 1 stepped", d, &n) == 0 && n == 2);
	TraceStart(); DrawOne(0, 0); DrawOne(14, 0);
	EXPECT(IsNumber("11", d, &n) == 1 && n == 2);
}


/*--------------------------------------------------------------------
	FindPound: a bar (a sign coded 13, as SearchDigit_S reads it) and
	the stroke before it.
--------------------------------------------------------------------*/

// ==> how many pound signs FindPound put in (value 1570)
static long
Pounds(const char* what)
{
	tag_CHUNK_STAFF staff;
	if (!Construct(&staff, what))
		return -1;
	void* lo = LO_Create();
	staff.fLO = lo;
	DefHeightsForNumber(&staff);
	ChunkPutClassesToLO(lo, staff.fNodes, staff.fChunks, staff.fChunkCount);
	GetCircles(&staff);
	SearchDigit_S(&staff);
	// the bar is the minus S read: class 1300, value 1613
	long bars = 0;
	tag_LOWOBJ* obj = nil;
	if (LO_SetWorkClass(lo, 1300) == 1)
		for (long more = LO_PickFirst(lo, &obj); more; more = LO_PickNext(lo, &obj))
			if (obj->fValue == 1613)
				bars++;
	EXPECT(bars == 1);
	FindPound(&staff);
	long k = 0;
	if (LO_SetWorkClass(lo, 1300) == 1)
		for (long more = LO_PickFirst(lo, &obj); more; more = LO_PickNext(lo, &obj))
			if (obj->fValue == 1570)
				k++;
	if (gVerbose)
		printf("  %s: %ld pound signs\n", what, k);
	LO_Destroy(lo);
	Destruct(&staff);
	return k;
}

static void
TestFindPound(void)
{
	// a pound sign: over the top from the right and down, a turn left at
	// the foot and out along a wavy base (the foot must lie below the
	// base's crest); then the bar across its middle
	TraceStart();
	MoveTo(13, 4); ArcTo(9, 5, 4, 20, 180); LineTo(5, 16); LineTo(1, 20); LineTo(8, 17); LineTo(15, 20); StrokeEnd();
	DrawLine(1, 11, 10, 11);
	EXPECT(Pounds("pound") == 1);
	// the same body without the turn at its foot is no pound sign
	TraceStart();
	MoveTo(13, 4); ArcTo(9, 5, 4, 20, 180); LineTo(5, 20); StrokeEnd();
	DrawLine(1, 11, 10, 11);
	EXPECT(Pounds("no foot") == 0);
	// nor with the bar across its top
	TraceStart();
	MoveTo(13, 4); ArcTo(9, 5, 4, 20, 180); LineTo(5, 16); LineTo(1, 20); LineTo(15, 20); StrokeEnd();
	DrawLine(1, 2, 10, 2);
	EXPECT(Pounds("bar at the top") == 0);
}


/*--------------------------------------------------------------------
	SearchDigit_S: the signs, the small marks and the digits of arcs.
--------------------------------------------------------------------*/

// What SearchDigit_S put in, still standing, of classes 1300, 1600 and
// 2200: { class, value, extra }.  ==> how many.
struct SFound { long cls, value, extra; };

static long
SearchS(const char* what, SFound* found, long max)
{
	tag_CHUNK_STAFF staff;
	if (!Construct(&staff, what))
		return -1;
	void* lo = LO_Create();
	staff.fLO = lo;
	DefHeightsForNumber(&staff);
	ChunkPutClassesToLO(lo, staff.fNodes, staff.fChunks, staff.fChunkCount);
	GetCircles(&staff);
	EXPECT(SearchDigit_S(&staff) == 0);
	long k = 0;
	static const ULong kClasses[] = { 1300, 1600, 2200 };
	for (long c = 0; c < 3; c++)
	{
		tag_LOWOBJ* obj = nil;
		if (LO_SetWorkClass(lo, kClasses[c]) == 1)
			for (long more = LO_PickFirst(lo, &obj); more && k < max; more = LO_PickNext(lo, &obj))
				if (obj->fValue != 0xffff)
				{
					found[k].cls = (long) kClasses[c];
					found[k].value = obj->fValue;
					found[k].extra = obj->fExtra;
					k++;
				}
	}
	if (gVerbose)
	{
		printf("  S %s: %ld:", what, k);
		for (long i = 0; i < k; i++)
			printf(" %ld/%ld(extra %#lx)", found[i].cls, found[i].value, found[i].extra & 0xffff);
		printf("\n");
	}
	LO_Destroy(lo);
	Destruct(&staff);
	return k;
}

// SearchS found exactly one thing, class 1300 value 1600 + code, this extra
static bool
ReadsAs(const char* what, long code, long extra)
{
	SFound f[8];
	long k = SearchS(what, f, 8);
	return k == 1 && f[0].cls == 1300 && f[0].value == 1600 + code && f[0].extra == extra;
}

static void
TestSearchS(void)
{
	SFound f[8];
	// a dot low between two 1s: a small mark (class 1600, 1614) settled
	// as a full stop by the dots pass
	TraceStart(); DrawOne(0, 0); DrawLine(12, 19, 13, 20); DrawOne(18, 0);
	EXPECT(ReadsAs("1.1", 14, 1));
	// a bar between two 1s: a bar for later (class 1600, 1613), judged a
	// minus (extra 3) by the bars pass
	TraceStart(); DrawOne(0, 0); DrawLine(12, 10, 20, 10); DrawOne(24, 0);
	EXPECT(ReadsAs("1-1", 13, 3));
	// two dots one above the other: a colon
	TraceStart(); DrawOne(0, 0); DrawLine(12, 6, 13, 7); DrawLine(12, 17, 13, 18); DrawOne(18, 0);
	EXPECT(ReadsAs("1:1", 15, 1));
	// an upright and a bar across it: a + when the upright is the first
	// thing written...
	TraceStart(); DrawLine(7, 3, 7, 17); DrawLine(0, 10, 14, 10);
	EXPECT(ReadsAs("+", 12, 0x23));
	// ...otherwise only the bar, as a minus (S_Bar's quirk)
	TraceStart(); DrawOne(-14, 0); DrawLine(7, 3, 7, 17); DrawLine(0, 10, 14, 10);
	EXPECT(ReadsAs("1+", 13, 0x23));
	// a 5 whose bar is a stroke of its own
	TraceStart(); DrawFive(0, 0);
	EXPECT(ReadsAs("5", 5, 0x21));
	// a 7 crossed in the middle (its stem an arc after the top)
	TraceStart(); MoveTo(0, 5); LineTo(13, 0); LineTo(9, 20); StrokeEnd(); DrawLine(4, 11, 16, 11);
	EXPECT(ReadsAs("7 crossed", 7, 0x23));
	// a 7's bar at its top is not a crossbar (the stem's foot must be 20
	// to 70 per cent below the bar's end): it is read as a minus
	TraceStart(); MoveTo(10, 0); LineTo(13, 1); LineTo(5, 20); StrokeEnd(); DrawLine(0, 0, 13, 0);
	EXPECT(ReadsAs("7 barred at the top", 13, 3));
	// brackets taller than the line: a ( (there must be more than three
	// real chunks) and a ) after a stroke of its height
	TraceStart(); DrawOne(0, 0); MoveTo(20, -3); ArcTo(30, 10, 16, 125, 235); StrokeEnd(); DrawOne(24, 0); DrawOne(34, 0);
	EXPECT(ReadsAs("1(11", 10, 0x20));
	TraceStart(); DrawOne(0, 0); MoveTo(10, -3); ArcTo(0, 10, 16, 55, -55); StrokeEnd();
	EXPECT(ReadsAs("1)", 11, 0x20));
	// a bracket no taller than the line is not one
	TraceStart(); DrawOne(0, 0); MoveTo(10, 0); ArcTo(2, 10, 10, 60, -60); StrokeEnd();
	EXPECT(SearchS("1) short", f, 8) == 0);
	// a solidus, taller by a quarter than the 1s either side
	TraceStart(); DrawOne(0, 0); DrawLine(20, -3, 10, 23); DrawOne(24, 0);
	EXPECT(ReadsAs("1/1", 16, 0x1c));
	// a stroke as tall as they are is not
	TraceStart(); DrawOne(0, 0); DrawLine(20, 0, 10, 20); DrawOne(24, 0);
	EXPECT(SearchS("1/1 short", f, 8) == 0);
	// a comma: a small stroke low, reaching below the line
	TraceStart(); DrawOne(0, 0); DrawLine(12, 18, 10, 24); DrawOne(18, 0);
	EXPECT(ReadsAs("1,1", 19, 0x19));
	// the arcs pass: a 0, a 6 (one arc down the left, then the bowl), a 3
	TraceStart(); DrawZero(0, 0);
	EXPECT(ReadsAs("0", 0, 1));
	TraceStart(); MoveTo(11.1, 2.2); ArcTo(13, 13, 11, 100, 260); EllipseTo(13, 17.5, 5, 5.5, 270, 540); StrokeEnd();
	EXPECT(ReadsAs("6", 6, 1));
	TraceStart(); MoveTo(1, 2); ArcTo(7, 5, 5, 150, -90); ArcTo(7, 15, 5, 90, -160); StrokeEnd();
	EXPECT(ReadsAs("3", 3, 3));		// (S_PutChunks: the extra is the digit again)
	// a per cent sign: ring, slash, ring - the first ring a 0 and the
	// second, with the slash and the first, the sign
	TraceStart(); MoveTo(3, 1); EllipseTo(3, 4, 3, 3, 90, 450); StrokeEnd(); DrawLine(13, 0, 3, 20); MoveTo(13, 16); EllipseTo(13, 19, 3, 3, 90, 450); StrokeEnd();
	EXPECT(SearchS("%", f, 8) == 2 && f[0].value == 1600 && f[1].value == 1617);
	// an @: a small ring and a big one round it in one stroke
	TraceStart(); MoveTo(12, 8); EllipseTo(9, 10, 3, 3, 0, 360); LineTo(13, 13); ArcTo(10, 10, 9, -30, 330); StrokeEnd();
	EXPECT(ReadsAs("@", 20, 20));
	// a 9 is not this searcher's (a loop and a straight tail: the other
	// searchers read it), nor a 1
	TraceStart(); DrawNine(0, 0);
	EXPECT(SearchS("9", f, 8) == 0);
	TraceStart(); DrawOne(0, 0);
	EXPECT(SearchS("1", f, 8) == 0);
}


/*--------------------------------------------------------------------
	Digits: the whole of it - the searchers, the doubtful digits taken
	out, the cells, the second looks and the verdict.
--------------------------------------------------------------------*/

// ==> Digits' answer; *text the characters it handed back, *runs how many
// runs of other strokes
static long
ReadNumber(const char* what, char* text = nil, long* runs = nil, bool show = false)
{
	tag_CHUNK_STAFF staff;
	if (!Construct(&staff, what))
		return -1;
	void* lo = LO_Create();
	staff.fLO = lo;
	memset(staff.fDigits, 0xff, sizeof(staff.fDigits));
	staff.f5C = 0x18;
	tag_BOX box = { 0x7fff, 0x7fff, -0x7fff, -0x7fff };
	for (long k = 0; k < staff.fNodeCount; k++)
	{
		tag_wapx_type* nd = &staff.fNodes[k];
		if (nd->x < box.left) box.left = nd->x;
		if (nd->x > box.right) box.right = nd->x;
		if (nd->y < box.top) box.top = nd->y;
		if (nd->y > box.bottom) box.bottom = nd->y;
	}
	tagNumBox numbox[0x19];
	memset(numbox, 0, sizeof(numbox));
	int32_t* pairs = nil;
	int32_t count = 0;
	long answer = Digits(&staff, box, staff.fHeight, numbox, &pairs, &count);
	char buf[0x20];
	long k = 0;
	for (; k < 0x18 && numbox[k].fChar != 0; k++)
		buf[k] = (char) numbox[k].fChar;
	buf[k] = 0;
	if (text != nil)
		strcpy(text, buf);
	if (runs != nil)
		*runs = count;
	if (gVerbose || show)
		printf("  Digits %s: answer %ld, \"%s\", %d runs\n", what, answer, buf, count);
	if (pairs != nil)
		HWRMemoryFree((Ptr) pairs);
	LO_Destroy(lo);
	Destruct(&staff);
	return answer;
}

// how many cells CutNumberInDigits cuts the writing into
static long
Cells(const char* what)
{
	tag_CHUNK_STAFF staff;
	if (!Construct(&staff, what))
		return -1;
	void* lo = LO_Create();
	staff.fLO = lo;
	DefHeightsForNumber(&staff);
	long cells = CutNumberInDigits(&staff);
	EXPECT(CountClass(lo, 1200) == cells);
	LO_Destroy(lo);
	Destruct(&staff);
	return cells;
}

static void
TestDigits(void)
{
	char t[0x20];
	long runs = -1;
	// numbers: 3 is a number with nothing else written about it
	TraceStart(); DrawFour(0, 0); DrawTwo(22, 0);
	EXPECT(ReadNumber("42", t, &runs) == 3 && strcmp(t, "42") == 0 && runs == 0);
	// the 0 V and S both read is written out once (the overlapping digit
	// taken out)
	TraceStart(); DrawOne(0, 0); DrawZero(14, 0);
	EXPECT(ReadNumber("10", t) == 3 && strcmp(t, "10") == 0);
	TraceStart(); DrawTwo(0, 0); DrawOne(18, 0); MoveTo(30, 5); LineTo(43, 0); LineTo(39, 20); StrokeEnd();
	EXPECT(ReadNumber("217", t) == 3 && strcmp(t, "217") == 0);
	TraceStart(); DrawOne(0, 0); DrawOne(14, 0);
	EXPECT(ReadNumber("11", t) == 3 && strcmp(t, "11") == 0);
	// a lone digit written its usual way (a 2 in one stroke, a 5 in two)
	TraceStart(); DrawTwo(0, 0);
	EXPECT(ReadNumber("2", t) == 3 && strcmp(t, "2") == 0);
	TraceStart(); DrawFive(0, 0);
	EXPECT(ReadNumber("5", t) == 3 && strcmp(t, "5") == 0);
	TraceStart(); DrawOne(0, 0); DrawLine(12, 19, 13, 20); DrawOne(18, 0);
	EXPECT(ReadNumber("1.1", t) == 3 && strcmp(t, "1.1") == 0);
	// an area code in brackets
	TraceStart(); MoveTo(10, -3); ArcTo(20, 10, 16, 125, 235); StrokeEnd(); DrawFour(12, 0); DrawTwo(30, 0); MoveTo(46, -3); ArcTo(36, 10, 16, 55, -55); StrokeEnd();
	EXPECT(ReadNumber("(42)", t) == 3 && strcmp(t, "(42)") == 0);
	// a colon among the digits: not a number
	TraceStart(); DrawOne(0, 0); DrawLine(12, 6, 13, 7); DrawLine(12, 17, 13, 18); DrawOne(18, 0);
	EXPECT(ReadNumber("1:1", t) == 0 && strcmp(t, "1:1") == 0);
	// a 1 and a 5 is a number when the 5's bar is a stroke of its own...
	TraceStart(); DrawOne(0, 0); DrawFive(12, 0);
	EXPECT(ReadNumber("15", t) == 3 && strcmp(t, "15") == 0);
	// ...and the word "is" when the 5 is written in one stroke
	TraceStart(); DrawOne(0, 0); DrawFiveOne(12, 0);
	EXPECT(ReadNumber("1 5-in-one", t) == 0 && strcmp(t, "15") == 0);
	// a stroke that is no digit, after the number or before it: not a
	// number, and the stroke handed back as a run
	TraceStart(); DrawFour(0, 0); DrawTwo(22, 0); MoveTo(40, 8); LineTo(43, 20); LineTo(46, 10); LineTo(49, 20); LineTo(52, 8); StrokeEnd();
	EXPECT(ReadNumber("42w", t, &runs) == 0 && strcmp(t, "42") == 0 && runs == 1);
	TraceStart(); MoveTo(0, 8); LineTo(3, 20); LineTo(6, 10); LineTo(9, 20); LineTo(12, 8); StrokeEnd(); DrawFour(20, 0); DrawTwo(42, 0);
	EXPECT(ReadNumber("w42", t, &runs) == 0 && strcmp(t, "42") == 0 && runs == 1);

	// the cells: the 4's two strokes one cell (the upright overlaps the
	// slant), the 2 another
	TraceStart(); DrawFour(0, 0); DrawTwo(22, 0);
	EXPECT(Cells("42") == 2);
	TraceStart(); DrawOne(0, 0); DrawOne(14, 0); DrawOne(28, 0);
	EXPECT(Cells("111") == 3);
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
	TestSecondLookPass();
	TestSearchK();
	TestSearchV();
	TestSearchNumber();
	TestFindPound();
	TestSearchS();
	TestDigits();
	if (failures == 0)
		printf("test_Chunk: all passed\n");
	return failures == 0 ? 0 : 1;
}
