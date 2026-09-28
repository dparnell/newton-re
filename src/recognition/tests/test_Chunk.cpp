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


int
main(int argc, char** argv)
{
	gVerbose = argc > 1;
	InitHostStandaloneHeap();
	TestTurns();
	TestApprox();
	TestLowObjects();
	TestConstruct();
	if (failures == 0)
		printf("test_Chunk: all passed\n");
	return failures == 0 ? 0 : 1;
}
