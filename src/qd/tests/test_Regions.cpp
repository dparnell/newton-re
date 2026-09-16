// QuickDraw rectangles and regions test: the rectangle utilities, the
// region format (a union of two rectangles written out row by row), the
// region operations (union, intersection, difference, xor, inset, offset,
// map, trim) checked pixel by pixel against set arithmetic on the
// rectangles they were built from - the regions rasterised through the
// scan-conversion state (SeekRgn) that drawing uses - and the membership
// tests.  Runs over a standalone kernel heap.
#include "Rects.h"
#include "Regions.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

const long kGrid = 64;					// the raster the tests compare on: 0..63 both ways


// The region rasterised over the grid: one byte per pixel, through the
// scan-conversion state a pixel row at a time.
static void
Rasterize(RgnHandle rgn, unsigned char* pixels)
{
	memset(pixels, 0, kGrid * kGrid);
	if (EmptyRgn(rgn))
		return;
	if ((*rgn)->rgnSize == kRectRgnSize)
	{
		const Rect* r = &(*rgn)->rgnBBox;
		for (long y = r->top; y < r->bottom; y++)
			for (long x = r->left; x < r->right; x++)
				if (0 <= x && x < kGrid && 0 <= y && y < kGrid)
					pixels[y * kGrid + x] = 1;
		return;
	}
	ULong32 scan[kGrid / 32 + 2];
	RgnState state;
	InitRgn(*rgn, &state, 0, kGrid, 0, (char*) scan);
	for (long y = 0; y < kGrid; y++)
	{
		SeekRgn(&state, y);
		for (long x = 0; x < kGrid; x++)
			if (scan[x >> 5] & (0x80000000u >> (x & 31)))
				pixels[y * kGrid + x] = 1;
	}
}


static Boolean
InRect(long x, long y, const Rect& r)
{
	return r.left <= x && x < r.right && r.top <= y && y < r.bottom;
}


// the raster compared with a predicate over the grid
typedef Boolean (*Predicate)(long x, long y, const unsigned char* a, const unsigned char* b);

static Boolean
SameAs(RgnHandle rgn, Predicate expected, const unsigned char* a, const unsigned char* b, const char* what)
{
	unsigned char pixels[kGrid * kGrid];
	Rasterize(rgn, pixels);
	long wrong = 0;
	for (long y = 0; y < kGrid; y++)
		for (long x = 0; x < kGrid; x++)
			if ((pixels[y * kGrid + x] != 0) != (expected(x, y, a, b) != 0))
			{
				if (wrong == 0)
					fprintf(stderr, "  %s: first difference at (%ld, %ld): region %d, expected %d\n", what, x, y, pixels[y * kGrid + x], expected(x, y, a, b));
				wrong++;
			}
	return wrong == 0;
}

static Boolean	PredA(long x, long y, const unsigned char* a, const unsigned char*)			{ return a[y * kGrid + x]; }
static Boolean	PredUnion(long x, long y, const unsigned char* a, const unsigned char* b)	{ return a[y * kGrid + x] || b[y * kGrid + x]; }
static Boolean	PredSect(long x, long y, const unsigned char* a, const unsigned char* b)	{ return a[y * kGrid + x] && b[y * kGrid + x]; }
static Boolean	PredDiff(long x, long y, const unsigned char* a, const unsigned char* b)	{ return a[y * kGrid + x] && !b[y * kGrid + x]; }
static Boolean	PredXor(long x, long y, const unsigned char* a, const unsigned char* b)		{ return (a[y * kGrid + x] != 0) != (b[y * kGrid + x] != 0); }


static void
TestRects()
{
	Rect r;
	SetRect(&r, 10, 20, 30, 40);
	EXPECT(r.left == 10 && r.top == 20 && r.right == 30 && r.bottom == 40);
	EXPECT(!EmptyRect(&r));
	OffsetRect(&r, 5, -5);
	EXPECT(r.left == 15 && r.top == 15 && r.right == 35 && r.bottom == 35);
	InsetRect(&r, 5, 10);
	EXPECT(r.left == 20 && r.top == 25 && r.right == 30 && r.bottom == 25);
	EXPECT(EmptyRect(&r));
	Rect a, b, c;
	SetRect(&a, 0, 0, 10, 10);
	SetRect(&b, 5, 5, 15, 15);
	EXPECT(SectRect(&a, &b, &c) && c.left == 5 && c.top == 5 && c.right == 10 && c.bottom == 10);
	UnionRect(&a, &b, &c);
	EXPECT(c.left == 0 && c.top == 0 && c.right == 15 && c.bottom == 15);
	JoinRect(&a, &b, &c);
	EXPECT(c.left == 0 && c.top == 0 && c.right == 15 && c.bottom == 15);
	SetRect(&b, 20, 20, 30, 30);
	EXPECT(!SectRect(&a, &b, &c) && EmptyRect(&c));
	SetEmptyRect(&b);
	UnionRect(&a, &b, &c);
	EXPECT(EqualRect(&a, &c));
	UnionRect(&b, &a, &c);
	EXPECT(EqualRect(&a, &c));
	EXPECT(PtInRect(MakePoint(0, 0), &a) && PtInRect(MakePoint(9, 9), &a) && !PtInRect(MakePoint(10, 9), &a) && !PtInRect(MakePoint(-1, 0), &a));
	Pt2Rect(MakePoint(12, 3), MakePoint(2, 8), &c);
	EXPECT(c.left == 2 && c.top == 3 && c.right == 12 && c.bottom == 8);
	SetRect(&a, 0, 0, 10, 10);
	SetRect(&b, 0, 0, 20, 20);
	SetRect(&c, 2, 3, 4, 6);
	MapRect(&c, &a, &b);
	EXPECT(c.left == 4 && c.top == 6 && c.right == 8 && c.bottom == 12);
	EXPECT(MapCoord(5, 0, 10, 100, 30) == 115);				// 5/10 of 30, rounded
	EXPECT(MapCoord(1, 0, 3, 0, 10) == 3 && MapCoord(2, 0, 3, 0, 10) == 7);
	Point p = MakePoint(5, 5);
	MapPt(&p, &a, &b);
	EXPECT(p.h == 10 && p.v == 10);
	Rect d, e, f;
	SetRect(&d, 0, 0, 10, 10);
	SetRect(&e, 2, 2, 12, 12);
	SetRect(&f, 4, 4, 14, 14);
	EXPECT(RSect(&c, 3, &d, &e, &f) && c.left == 4 && c.top == 4 && c.right == 10 && c.bottom == 10);
}


static void
TestFormat()
{
	// two rectangles united: the rows say where the spans change
	RgnHandle a = NewRgn();
	RgnHandle b = NewRgn();
	RgnHandle u = NewRgn();
	EXPECT(EmptyRgn(a) && (*a)->rgnSize == kRectRgnSize);
	SetRectRgn(a, 0, 0, 10, 10);
	SetRectRgn(b, 5, 5, 15, 15);
	EXPECT(!EmptyRgn(a) && !EqualRgn(a, b));
	UnionRgn(a, b, u);
	EXPECT((*u)->rgnSize == 46);
	EXPECT((*u)->rgnBBox.top == 0 && (*u)->rgnBBox.left == 0 && (*u)->rgnBBox.bottom == 15 && (*u)->rgnBBox.right == 15);
	const short expected[] = { 0, 0, 10, kRgnEnd, 5, 10, 15, kRgnEnd, 10, 0, 5, kRgnEnd, 15, 5, 15, kRgnEnd, kRgnEnd };
	const short* rows = (const short*) ((char*) *u + kRgnRowsOffset);
	EXPECT(memcmp(rows, expected, sizeof(expected)) == 0);
	// the same union again is equal; a copy too; offset moves rows and box
	RgnHandle u2 = NewRgn();
	UnionRgn(b, a, u2);
	EXPECT(EqualRgn(u, u2));
	CopyRgn(u, u2);
	EXPECT(EqualRgn(u, u2) && (*u2)->rgnSize == 46);
	OffsetRgn(u2, 3, -2);
	rows = (const short*) ((char*) *u + kRgnRowsOffset);		// (handles move when the heap compacts)
	EXPECT(!EqualRgn(u, u2) && (*u2)->rgnBBox.left == 3 && (*u2)->rgnBBox.top == -2 && rows[0] == 0);
	const short* rows2 = (const short*) ((char*) *u2 + kRgnRowsOffset);
	EXPECT(rows2[0] == -2 && rows2[1] == 3 && rows2[2] == 13 && rows2[4] == 3);
	OffsetRgn(u2, -3, 2);
	EXPECT(EqualRgn(u, u2));
	// the intersection of two rectangles is a rectangular region
	RgnHandle s = NewRgn();
	SectRgn(a, b, s);
	EXPECT((*s)->rgnSize == kRectRgnSize && (*s)->rgnBBox.left == 5 && (*s)->rgnBBox.top == 5 && (*s)->rgnBBox.right == 10 && (*s)->rgnBBox.bottom == 10);
	// the union less one of them is the other less the intersection: not a rectangle
	RgnHandle d = NewRgn();
	DiffRgn(u, b, d);
	EXPECT((*d)->rgnSize > kRectRgnSize);
	RgnHandle x = NewRgn();
	XorRgn(a, b, x);
	EXPECT(RectInRgn(&(*s)->rgnBBox, u) && !RectInRgn(&(*s)->rgnBBox, x));
	// membership: the union's pixels - the ROM's PtInRgn answers for the pixel row above the point
	EXPECT(PtInRgn(MakePoint(0, 1), u) && PtInRgn(MakePoint(12, 12), u) && PtInRgn(MakePoint(9, 3), u) && PtInRgn(MakePoint(14, 14), u));
	EXPECT(!PtInRgn(MakePoint(12, 2), u) && !PtInRgn(MakePoint(2, 12), u) && !PtInRgn(MakePoint(15, 15), u) && !PtInRgn(MakePoint(-1, 5), u));
	EXPECT(!PtInRgn(MakePoint(0, 0), u));
	EXPECT(PtInRgn(MakePoint(0, 0), a) && !PtInRgn(MakePoint(10, 0), a));
	// wide open
	SetRectRgn(x, -32767, -32767, 32767, 32767);
	EXPECT(IsWideOpenRgn(x) && IsWideOpenRgn(nil) && !IsWideOpenRgn(a) && !IsWideOpenRgn(u));
	// empty results
	SetRectRgn(x, 20, 20, 30, 30);
	SectRgn(a, x, s);
	EXPECT(EmptyRgn(s) && (*s)->rgnSize == kRectRgnSize);
	DiffRgn(a, a, s);
	EXPECT(EmptyRgn(s));
	XorRgn(u, u, s);
	EXPECT(EmptyRgn(s));
	UnionRgn(u, s, x);
	EXPECT(EqualRgn(u, x));
	DisposeRgn(a); DisposeRgn(b); DisposeRgn(u); DisposeRgn(u2); DisposeRgn(s); DisposeRgn(d); DisposeRgn(x);
}


// a few rectangles' worth of region, and its raster
static RgnHandle
RectsRgn(const Rect* rects, long count, unsigned char* raster)
{
	RgnHandle rgn = NewRgn();
	RgnHandle one = NewRgn();
	memset(raster, 0, kGrid * kGrid);
	for (long i = 0; i < count; i++)
	{
		RectRgn(one, &rects[i]);
		UnionRgn(rgn, one, rgn);
		for (long y = 0; y < kGrid; y++)
			for (long x = 0; x < kGrid; x++)
				if (InRect(x, y, rects[i]))
					raster[y * kGrid + x] = 1;
	}
	DisposeRgn(one);
	return rgn;
}


static void
TestOperations()
{
	static unsigned char rasterA[kGrid * kGrid], rasterB[kGrid * kGrid];
	Rect rectsA[3], rectsB[3];
	SetRect(&rectsA[0], 2, 2, 20, 12);
	SetRect(&rectsA[1], 10, 8, 30, 30);
	SetRect(&rectsA[2], 40, 5, 60, 25);
	SetRect(&rectsB[0], 5, 10, 45, 15);
	SetRect(&rectsB[1], 25, 20, 50, 50);
	SetRect(&rectsB[2], 0, 40, 10, 63);
	RgnHandle a = RectsRgn(rectsA, 3, rasterA);
	RgnHandle b = RectsRgn(rectsB, 3, rasterB);
	EXPECT(SameAs(a, PredA, rasterA, nil, "a"));
	EXPECT(SameAs(b, PredA, rasterB, nil, "b"));
	RgnHandle r = NewRgn();
	UnionRgn(a, b, r);
	EXPECT(SameAs(r, PredUnion, rasterA, rasterB, "union"));
	SectRgn(a, b, r);
	EXPECT(SameAs(r, PredSect, rasterA, rasterB, "sect"));
	DiffRgn(a, b, r);
	EXPECT(SameAs(r, PredDiff, rasterA, rasterB, "diff"));
	DiffRgn(b, a, r);
	EXPECT(SameAs(r, PredDiff, rasterB, rasterA, "diff reversed"));
	XorRgn(a, b, r);
	EXPECT(SameAs(r, PredXor, rasterA, rasterB, "xor"));
	DoRgnOp(kRgnOpXor, a, b, r);
	EXPECT(SameAs(r, PredXor, rasterA, rasterB, "DoRgnOp xor"));
	DoRgnOp(kRgnOpSect, a, b, r);
	EXPECT(SameAs(r, PredSect, rasterA, rasterB, "DoRgnOp sect"));
	// an operation into one of its operands
	CopyRgn(a, r);
	UnionRgn(r, b, r);
	EXPECT(SameAs(r, PredUnion, rasterA, rasterB, "union in place"));
	// a - (a - b) is a and b
	DiffRgn(a, b, r);
	DiffRgn(a, r, r);
	EXPECT(SameAs(r, PredSect, rasterA, rasterB, "a - (a - b)"));
	// the membership tests agree with the raster: PtInRgn for the pixel row above (within the box)
	Boolean ptOK = true;
	for (long y = (*a)->rgnBBox.top + 1; y < (*a)->rgnBBox.bottom && ptOK; y++)
		for (long x = 0; x < kGrid; x++)
			if ((PtInRgn(MakePoint(x, y), a) != 0) != (rasterA[(y - 1) * kGrid + x] != 0))
			{
				fprintf(stderr, "  PtInRgn differs at (%ld, %ld)\n", x, y);
				ptOK = false;
				break;
			}
	EXPECT(ptOK);
	Rect probe;
	SetRect(&probe, 30, 26, 40, 34);					// between a's second and third rectangles
	EXPECT(!RectInRgn(&probe, a));
	SetRect(&probe, 30, 26, 41, 34);
	EXPECT(!RectInRgn(&probe, a));
	SetRect(&probe, 29, 26, 41, 34);
	EXPECT(RectInRgn(&probe, a));
	SetRect(&probe, 0, 0, 64, 64);
	EXPECT(RectInRgn(&probe, a) && !RectInRgn(&probe, r) == EmptyRgn(r));
	// TrimRect: a rectangle within one of a's rectangles stays, one across two is complex, one outside is empty
	SetRect(&probe, 42, 6, 50, 20);
	EXPECT(TrimRect(a, &probe) == 0 && probe.left == 42 && probe.top == 6 && probe.right == 50 && probe.bottom == 20);
	SetRect(&probe, 35, 0, 50, 20);
	EXPECT(TrimRect(a, &probe) == 0 && probe.left == 40 && probe.top == 5 && probe.right == 50 && probe.bottom == 20);
	SetRect(&probe, 0, 0, 64, 64);
	EXPECT(TrimRect(a, &probe) > 0 && probe.left == 0);
	SetRect(&probe, 31, 26, 39, 34);
	EXPECT(TrimRect(a, &probe) < 0);
	DisposeRgn(a); DisposeRgn(b); DisposeRgn(r);
}


// the raster eroded (inset > 0) or dilated (inset < 0) by a rectangle of
// half-widths dh and dv - what an inset means
static void
InsetRaster(const unsigned char* in, unsigned char* out, long dh, long dv)
{
	for (long y = 0; y < kGrid; y++)
		for (long x = 0; x < kGrid; x++)
		{
			Boolean erode = dh >= 0;
			long rh = dh < 0 ? -dh : dh, rv = dv < 0 ? -dv : dv;
			Boolean result = erode;
			for (long yy = y - rv; yy <= y + rv; yy++)
				for (long xx = x - rh; xx <= x + rh; xx++)
				{
					Boolean p = (0 <= xx && xx < kGrid && 0 <= yy && yy < kGrid) ? in[yy * kGrid + xx] != 0 : false;
					if (erode && !p)
						result = false;
					if (!erode && p)
						result = true;
				}
			out[y * kGrid + x] = result;
		}
}


static void
TestInsetAndMap()
{
	static unsigned char raster[kGrid * kGrid], expected[kGrid * kGrid];
	Rect rects[3];
	SetRect(&rects[0], 4, 4, 24, 14);
	SetRect(&rects[1], 12, 10, 32, 32);
	SetRect(&rects[2], 40, 8, 60, 28);
	RgnHandle a = RectsRgn(rects, 3, raster);
	RgnHandle r = NewRgn();
	CopyRgn(a, r);
	InsetRgn(r, 2, 1);
	InsetRaster(raster, expected, 2, 1);
	EXPECT(SameAs(r, PredA, expected, nil, "inset 2,1"));
	CopyRgn(a, r);
	InsetRgn(r, -2, -3);
	InsetRaster(raster, expected, -2, -3);
	EXPECT(SameAs(r, PredA, expected, nil, "inset -2,-3"));
	CopyRgn(a, r);
	InsetRgn(r, 0, 0);
	EXPECT(EqualRgn(a, r));
	// a rectangle inset to nothing is empty
	SetRectRgn(r, 10, 10, 20, 20);
	InsetRgn(r, 5, 2);
	EXPECT(EmptyRgn(r));
	// mapped to twice the size: pixel (x, y) is in it when (x/2, y/2) was
	Rect from, to;
	SetRect(&from, 0, 0, 32, 32);
	SetRect(&to, 0, 0, 64, 64);
	CopyRgn(a, r);
	MapRgn(r, &from, &to);
	for (long y = 0; y < kGrid; y++)
		for (long x = 0; x < kGrid; x++)
			expected[y * kGrid + x] = raster[(y / 2) * kGrid + x / 2];
	EXPECT(SameAs(r, PredA, expected, nil, "map x2"));
	SetRectRgn(r, 2, 4, 6, 8);
	MapRgn(r, &from, &to);
	EXPECT((*r)->rgnSize == kRectRgnSize && (*r)->rgnBBox.left == 4 && (*r)->rgnBBox.top == 8 && (*r)->rgnBBox.right == 12 && (*r)->rgnBBox.bottom == 16);
	DisposeRgn(a); DisposeRgn(r);
}


// many random rectangles, the operations against the rasters
static ULong gSeed = 12345;
static long
Random(long n)
{
	gSeed = gSeed * 1103515245 + 12345;
	return (long) ((gSeed >> 16) & 0x7fff) % n;
}


static void
TestRandom()
{
	static unsigned char rasterA[kGrid * kGrid], rasterB[kGrid * kGrid];
	for (long round = 0; round < 20; round++)
	{
		Rect rectsA[8], rectsB[8];
		for (long i = 0; i < 8; i++)
		{
			long x = Random(56), y = Random(56);
			SetRect(&rectsA[i], x, y, x + 1 + Random(kGrid - x - 1), y + 1 + Random(kGrid - y - 1));
			x = Random(56); y = Random(56);
			SetRect(&rectsB[i], x, y, x + 1 + Random(kGrid - x - 1), y + 1 + Random(kGrid - y - 1));
		}
		RgnHandle a = RectsRgn(rectsA, 8, rasterA);
		RgnHandle b = RectsRgn(rectsB, 8, rasterB);
		RgnHandle r = NewRgn();
		EXPECT(SameAs(a, PredA, rasterA, nil, "random a"));
		UnionRgn(a, b, r);
		EXPECT(SameAs(r, PredUnion, rasterA, rasterB, "random union"));
		SectRgn(a, b, r);
		EXPECT(SameAs(r, PredSect, rasterA, rasterB, "random sect"));
		DiffRgn(a, b, r);
		EXPECT(SameAs(r, PredDiff, rasterA, rasterB, "random diff"));
		XorRgn(a, b, r);
		EXPECT(SameAs(r, PredXor, rasterA, rasterB, "random xor"));
		DisposeRgn(a); DisposeRgn(b); DisposeRgn(r);
	}
}


int
main()
{
	InitHostStandaloneHeap();
	TestRects();
	TestFormat();
	TestOperations();
	TestInsetAndMap();
	TestRandom();
	if (failures == 0)
		printf("test_Regions: all passed\n");
	else
		printf("test_Regions: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
