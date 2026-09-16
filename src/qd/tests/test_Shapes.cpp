// QuickDraw shapes test: ovals and round rectangles painted and framed
// (the pictures pinned: the ROM's oval rasteriser gives the classic
// QuickDraw shapes, the 8 by 8 circle among them), the oval region's
// format, lines in every direction with pens of several sizes, and the
// pen location LineTo/Line leave, regions recorded from lines and
// polygons.  Runs over a standalone kernel heap.
#include "Shapes.h"
#include "Polygons.h"
#include "Draw.h"
#include "FixedMath.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

const long kSize = 64;
static unsigned char gBits[kSize * kSize];
static PixelMap gMap;
static GrafPort gPort;


static void
Clear()
{
	memset(gBits, 0, sizeof(gBits));
}


// the map's top-left corner as rows of '#' and '.', against the picture
static Boolean
PictureIs(long width, long height, const char* picture, const char* what)
{
	Boolean same = true;
	for (long y = 0; y < height; y++)
		for (long x = 0; x < width; x++)
		{
			Boolean want = picture[y * (width + 1) + x] == '#';
			if ((GetPixel(&gMap, x, y) != 0) != want)
				same = false;
		}
	if (!same)
	{
		fprintf(stderr, "  %s:\n", what);
		for (long y = 0; y < height; y++)
		{
			fprintf(stderr, "    ");
			for (long x = 0; x < width; x++)
				fputc(GetPixel(&gMap, x, y) ? '#' : '.', stderr);
			fprintf(stderr, "   %.*s\n", (int) width, picture + y * (width + 1));
		}
	}
	return same;
}


static void
TestOvals()
{
	Rect r;
	SetRect(&r, 2, 2, 10, 10);
	Clear();
	PaintOval(&r);
	EXPECT(PictureIs(12, 12,
		"............\n"
		"............\n"
		"....####....\n"
		"...######...\n"
		"..########..\n"
		"..########..\n"
		"..########..\n"
		"..########..\n"
		"...######...\n"
		"....####....\n"
		"............\n"
		"............\n", "PaintOval 8x8"));
	Clear();
	FrameOval(&r);
	EXPECT(PictureIs(12, 12,
		"............\n"
		"............\n"
		"....####....\n"
		"...#....#...\n"
		"..#......#..\n"
		"..#......#..\n"
		"..#......#..\n"
		"..#......#..\n"
		"...#....#...\n"
		"....####....\n"
		"............\n"
		"............\n", "FrameOval 8x8"));
	SetRect(&r, 2, 2, 22, 12);
	Clear();
	PaintOval(&r);
	EXPECT(PictureIs(24, 14,
		"........................\n"
		"........................\n"
		"........########........\n"
		".....##############.....\n"
		"...##################...\n"
		"..####################..\n"
		"..####################..\n"
		"..####################..\n"
		"..####################..\n"
		"...##################...\n"
		".....##############.....\n"
		"........########........\n"
		"........................\n"
		"........................\n", "PaintOval 20x10"));
	Clear();
	FrameOval(&r);
	EXPECT(PictureIs(24, 14,
		"........................\n"
		"........................\n"
		"........########........\n"
		".....##..........##.....\n"
		"...##..............##...\n"
		"..##................##..\n"
		"..#..................#..\n"
		"..#..................#..\n"
		"..##................##..\n"
		"...##..............##...\n"
		".....##..........##.....\n"
		"........########........\n"
		"........................\n"
		"........................\n", "FrameOval 20x10"));
	// small ones
	SetRect(&r, 2, 2, 3, 3);
	Clear();
	PaintOval(&r);
	EXPECT(PictureIs(5, 5, ".....\n.....\n..#..\n.....\n.....\n", "PaintOval 1x1"));
	SetRect(&r, 2, 2, 5, 5);
	Clear();
	PaintOval(&r);
	EXPECT(PictureIs(7, 7, ".......\n.......\n..###..\n..###..\n..###..\n.......\n.......\n", "PaintOval 3x3"));
	// the other verbs: erase and invert
	SetRect(&r, 2, 2, 10, 10);
	Clear();
	PaintRect(&gMap.bounds);
	EraseOval(&r);
	EXPECT(GetPixel(&gMap, 5, 5) == 0 && GetPixel(&gMap, 2, 2) != 0 && GetPixel(&gMap, 4, 2) == 0);
	InvertOval(&r);
	EXPECT(GetPixel(&gMap, 5, 5) != 0 && GetPixel(&gMap, 4, 2) != 0);
	FillOval(&r, GetStdPattern(whitePat));
	EXPECT(GetPixel(&gMap, 5, 5) == 0 && gPort.fgPat == stdPatterns[blackPat]);
	// the oval as a region: its box and rows
	RgnHandle rgn = OvalRgn(&r, 8, 8);
	EXPECT((*rgn)->rgnBBox.top == 2 && (*rgn)->rgnBBox.left == 2 && (*rgn)->rgnBBox.bottom == 10 && (*rgn)->rgnBBox.right == 10);
	EXPECT((*rgn)->rgnSize > kRectRgnSize);
	Rect probe;
	SetRect(&probe, 2, 2, 3, 3);
	EXPECT(!RectInRgn(&probe, rgn));
	SetRect(&probe, 5, 2, 6, 3);
	EXPECT(RectInRgn(&probe, rgn));
	DisposeRgn(rgn);
	// a hidden pen draws nothing
	Clear();
	HidePen();
	PaintOval(&r);
	ShowPen();
	EXPECT(GetPixel(&gMap, 5, 5) == 0);
}


static void
TestRoundRects()
{
	Rect r;
	SetRect(&r, 2, 2, 22, 14);
	Clear();
	PaintRoundRect(&r, 8, 8);
	EXPECT(PictureIs(24, 16,
		"........................\n"
		"........................\n"
		"....################....\n"
		"...##################...\n"
		"..####################..\n"
		"..####################..\n"
		"..####################..\n"
		"..####################..\n"
		"..####################..\n"
		"..####################..\n"
		"..####################..\n"
		"..####################..\n"
		"...##################...\n"
		"....################....\n"
		"........................\n"
		"........................\n", "PaintRoundRect 20x12 corners 8"));
	Clear();
	FrameRoundRect(&r, 8, 8);
	EXPECT(PictureIs(24, 16,
		"........................\n"
		"........................\n"
		"....################....\n"
		"...#................#...\n"
		"..#..................#..\n"
		"..#..................#..\n"
		"..#..................#..\n"
		"..#..................#..\n"
		"..#..................#..\n"
		"..#..................#..\n"
		"..#..................#..\n"
		"..#..................#..\n"
		"...#................#...\n"
		"....################....\n"
		"........................\n"
		"........................\n", "FrameRoundRect 20x12 corners 8"));
	Clear();
	PenSize(2, 2);
	FrameRoundRect(&r, 6, 6);
	PenNormal();
	EXPECT(PictureIs(24, 16,
		"........................\n"
		"........................\n"
		"...##################...\n"
		"..####################..\n"
		"..##................##..\n"
		"..##................##..\n"
		"..##................##..\n"
		"..##................##..\n"
		"..##................##..\n"
		"..##................##..\n"
		"..##................##..\n"
		"..##................##..\n"
		"..####################..\n"
		"...##################...\n"
		"........................\n"
		"........................\n", "FrameRoundRect 20x12 corners 6, pen 2"));
	// square corners are a rectangle
	Clear();
	PaintRoundRect(&r, 0, 0);
	EXPECT(GetPixel(&gMap, 2, 2) != 0 && GetPixel(&gMap, 21, 13) != 0 && GetPixel(&gMap, 22, 2) == 0);
	Clear();
	EraseRect(&gMap.bounds);
	PaintRect(&gMap.bounds);
	EraseRoundRect(&r, 8, 8);
	EXPECT(GetPixel(&gMap, 2, 2) != 0 && GetPixel(&gMap, 10, 7) == 0);
	InvertRoundRect(&r, 8, 8);
	EXPECT(GetPixel(&gMap, 10, 7) != 0);
	FillRoundRect(&r, 8, 8, GetStdPattern(whitePat));
	EXPECT(GetPixel(&gMap, 10, 7) == 0);
	// arcs of a full turn are ovals; lesser arcs are not yet drawn
	SetRect(&r, 2, 2, 10, 10);
	Clear();
	PaintArc(&r, 0, 360);
	EXPECT(GetPixel(&gMap, 5, 5) != 0 && GetPixel(&gMap, 2, 2) == 0);
	Clear();
	PaintArc(&r, 0, 90);
	EXPECT(GetPixel(&gMap, 5, 5) == 0);
}


static void
TestLines()
{
	Clear();
	MoveTo(2, 2);
	LineTo(9, 2);
	EXPECT(PictureIs(12, 4, "............\n............\n..########..\n............\n", "horizontal line"));
	EXPECT(gPort.pnLoc.h == 9 && gPort.pnLoc.v == 2);
	Clear();
	MoveTo(2, 2);
	LineTo(2, 6);
	EXPECT(PictureIs(4, 8, "....\n....\n..#.\n..#.\n..#.\n..#.\n..#.\n....\n", "vertical line"));
	Clear();
	MoveTo(2, 2);
	Line(5, 5);
	EXPECT(PictureIs(9, 9,
		".........\n"
		".........\n"
		"..#......\n"
		"...#.....\n"
		"....#....\n"
		".....#...\n"
		"......#..\n"
		".......#.\n"
		".........\n", "diagonal line"));
	EXPECT(gPort.pnLoc.h == 7 && gPort.pnLoc.v == 7);
	Clear();
	MoveTo(7, 2);
	LineTo(2, 7);
	EXPECT(PictureIs(9, 9,
		".........\n"
		".........\n"
		".......#.\n"
		"......#..\n"
		".....#...\n"
		"....#....\n"
		"...#.....\n"
		"..#......\n"
		".........\n", "diagonal line back"));
	// a shallow line: every column once, rows stepping
	Clear();
	MoveTo(0, 0);
	LineTo(8, 2);
	long count = 0;
	for (long y = 0; y < 3; y++)
		for (long x = 0; x < 9; x++)
			count += GetPixel(&gMap, x, y);
	EXPECT(count == 9 && GetPixel(&gMap, 0, 0) && GetPixel(&gMap, 8, 2) && GetPixel(&gMap, 4, 1));
	// a fat pen hangs below and right of the point
	Clear();
	PenSize(2, 3);
	MoveTo(2, 2);
	LineTo(5, 2);
	PenNormal();
	EXPECT(PictureIs(8, 6, "........\n........\n..#####.\n..#####.\n..#####.\n........\n", "line with a 2x3 pen"));
	// the pen's mode and pattern
	Clear();
	PaintRect(&gMap.bounds);
	PenMode(patBic);
	MoveTo(2, 2);
	LineTo(6, 2);
	PenNormal();
	EXPECT(GetPixel(&gMap, 4, 2) == 0 && GetPixel(&gMap, 4, 3) != 0 && GetPixel(&gMap, 7, 2) != 0);
	// clipped
	Clear();
	Rect clip;
	SetRect(&clip, 0, 0, 5, 5);
	ClipRect(&clip);
	MoveTo(0, 2);
	LineTo(20, 2);
	EXPECT(GetPixel(&gMap, 4, 2) != 0 && GetPixel(&gMap, 5, 2) == 0);
	ClipRect(&gMap.bounds);
	// a hidden pen only moves
	Clear();
	HidePen();
	MoveTo(2, 2);
	LineTo(6, 2);
	ShowPen();
	EXPECT(GetPixel(&gMap, 4, 2) == 0 && gPort.pnLoc.h == 6);
}


static void
TestPolygons()
{
	// a region from lines: a diamond
	Clear();
	RgnHandle rgn = NewRgn();
	OpenRgn();
	MoveTo(4, 0);
	LineTo(8, 4);
	LineTo(4, 8);
	LineTo(0, 4);
	LineTo(4, 0);
	CloseRgn(rgn);
	EXPECT(gPort.rgnSave == nil && gPort.pnVis == 0);
	EXPECT(PictureIs(9, 9, ".........\n.........\n.........\n.........\n.........\n.........\n.........\n.........\n.........\n", "recording draws nothing"));
	EXPECT((*rgn)->rgnBBox.top == 0 && (*rgn)->rgnBBox.left == 1 && (*rgn)->rgnBBox.bottom == 8 && (*rgn)->rgnBBox.right == 8);
	PaintRgn(rgn);
	// (the rows the ROM's PutLine gives: the pixels whose centres lie inside the outline, the classic QuickDraw shape)
	EXPECT(PictureIs(9, 9,
		"....#....\n"
		"...###...\n"
		"..#####..\n"
		".#######.\n"
		".#######.\n"
		"..#####..\n"
		"...###...\n"
		"....#....\n"
		".........\n", "diamond region"));
	// a rectangle from lines is the rectangle
	Clear();
	OpenRgn();
	MoveTo(1, 1);
	LineTo(6, 1);
	LineTo(6, 4);
	LineTo(1, 4);
	LineTo(1, 1);
	CloseRgn(rgn);
	Rect box;
	SetRect(&box, 1, 1, 6, 4);
	RgnHandle rect = NewRgn();
	RectRgn(rect, &box);
	EXPECT(EqualRgn(rgn, rect));
	DisposeRgn(rect);
	DisposeRgn(rgn);

	// a polygon: recorded, bounded, painted; framed as its lines
	Clear();
	PolyHandle poly = OpenPoly();
	MoveTo(2, 1);
	LineTo(8, 1);
	LineTo(5, 7);
	ClosePoly();
	EXPECT(gPort.polySave == nil && gPort.pnVis == 0);
	EXPECT(PolyPointCount(*poly) == 3 && (*poly)->polyPoints[0].h == 2 && (*poly)->polyPoints[2].v == 7);
	EXPECT((*poly)->polyBBox.left == 2 && (*poly)->polyBBox.top == 1 && (*poly)->polyBBox.right == 8 && (*poly)->polyBBox.bottom == 7);
	EXPECT(PictureIs(9, 9, ".........\n.........\n.........\n.........\n.........\n.........\n.........\n.........\n.........\n", "recording the polygon draws nothing"));
	PaintPoly(poly);
	EXPECT(PictureIs(10, 8,
		"..........\n"
		"...#####..\n"
		"...####...\n"
		"....###...\n"
		"....##....\n"
		".....#....\n"
		"..........\n"
		"..........\n", "painted triangle"));
	Clear();
	FramePoly(poly);
	EXPECT(GetPixel(&gMap, 2, 1) && GetPixel(&gMap, 8, 1) && GetPixel(&gMap, 5, 7) && !GetPixel(&gMap, 5, 3) && !GetPixel(&gMap, 5, 4));
	Clear();
	ErasePoly(poly);
	EXPECT(PictureIs(10, 8, "..........\n..........\n..........\n..........\n..........\n..........\n..........\n..........\n", "erased triangle"));
	OffsetPoly(poly, 10, 0);
	EXPECT((*poly)->polyBBox.left == 12 && (*poly)->polyPoints[1].h == 18);
	Rect from, to;
	SetRect(&from, 12, 1, 18, 7);
	SetRect(&to, 12, 1, 24, 13);
	MapPoly(poly, &from, &to);
	EXPECT((*poly)->polyBBox.right == 24 && (*poly)->polyPoints[2].v == 13 && (*poly)->polyPoints[1].h == 24);
	KillPoly(poly);
}

int
main()
{
	InitHostStandaloneHeap();
	InitGraf();
	gMap.baseAddr = (Ptr) gBits;
	gMap.rowBytes = kSize / 8;
	SetRect(&gMap.bounds, 0, 0, kSize, kSize);
	gMap.pixMapFlags = kPixMapPtr | 1;
	gMap.deviceRes.v = kDefaultDPI;
	gMap.deviceRes.h = kDefaultDPI;
	gMap.grayTable = nil;
	OpenPort(&gPort);
	SetPortBits(&gMap);
	gPort.portRect = gMap.bounds;
	RectRgn(gPort.visRgn, &gMap.bounds);
	EXPECT(FixedMultiply(0x18000, 0x20000) == 0x30000 && FixedMultiply(-0x10000, 0x8000) == -0x8000);
	EXPECT(FixedDivide(0x30000, 0x20000) == 0x18000 && FixedDivide(0x10000, 0x30000) == 0x5555 && FixedDivide(1, 0) == 0x7fffffff);
	TestOvals();
	TestRoundRects();
	TestLines();
	TestPolygons();
	ClosePort(&gPort);
	if (failures == 0)
		printf("test_Shapes: all passed\n");
	else
		printf("test_Shapes: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
