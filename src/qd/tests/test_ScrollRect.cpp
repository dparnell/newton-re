// ScrollRect test (src/qd/ScrollRect.h): a black square scrolled across
// an offscreen 1-bit port - the bits moved, the uncovered strip filled
// with the background and answered as the update region, what the clip
// region hides left alone, and nothing done with the pen hidden.
#include "ScrollRect.h"
#include "LocalToGlobal.h"
#include "Rects.h"
#include "Regions.h"
#include "Draw.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

const long kWidth = 64;
const long kHeight = 32;
static unsigned char gBits[kWidth * kHeight / 8];
static PixelMap gMap;
static GrafPort gPort;


static Boolean
Pixel(long x, long y)
{
	return (gBits[y * (kWidth / 8) + x / 8] & (0x80 >> (x % 8))) != 0;
}


// the black pixels are exactly the rectangle
static Boolean
OnlyBlack(short left, short top, short right, short bottom)
{
	for (long y = 0; y < kHeight; y++)
		for (long x = 0; x < kWidth; x++)
			if (Pixel(x, y) != (x >= left && x < right && y >= top && y < bottom))
				return false;
	return true;
}


static void
Square(void)
{
	memset(gBits, 0, sizeof(gBits));
	Rect square;
	SetRect(&square, 10, 10, 20, 20);
	PaintRect(&square);
	EXPECT(OnlyBlack(10, 10, 20, 20));
}


int
main()
{
	InitHostStandaloneHeap();
	InitGraf();
	gMap.baseAddr = (Ptr) gBits;
	gMap.rowBytes = kWidth / 8;
	SetRect(&gMap.bounds, 0, 0, kWidth, kHeight);
	gMap.pixMapFlags = kPixMapPtr | 1;
	gMap.deviceRes.v = kDefaultDPI;
	gMap.deviceRes.h = kDefaultDPI;
	gMap.grayTable = nil;
	OpenPort(&gPort);
	SetPortBits(&gMap);
	gPort.portRect = gMap.bounds;
	RectRgn(gPort.visRgn, &gMap.bounds);

	RgnHandle update = NewRgn();
	Rect all = gMap.bounds;

	// down 3 and right 5: the square moves, the strip it uncovered is the update
	Square();
	ScrollRect(&all, 5, 3, update);
	EXPECT(OnlyBlack(15, 13, 25, 23));
	Rect box = (*update)->rgnBBox;
	EXPECT(box.left == 0 && box.top == 0 && box.right == kWidth && box.bottom == kHeight);
	EXPECT(PtInRgn(MakePoint(2, 20), update) && PtInRgn(MakePoint(30, 1), update));
	EXPECT(!PtInRgn(MakePoint(30, 10), update));

	// up and left, off the edge
	Square();
	ScrollRect(&all, -12, -12, update);
	EXPECT(OnlyBlack(0, 0, 8, 8));
	box = (*update)->rgnBBox;
	EXPECT(box.right == kWidth && box.bottom == kHeight && PtInRgn(MakePoint(60, 30), update));

	// only part of the port
	Square();
	Rect part;
	SetRect(&part, 0, 0, 16, kHeight);							// the left 16 columns
	ScrollRect(&part, 0, 4, update);
	EXPECT(Pixel(12, 23) && !Pixel(12, 10) && Pixel(12, 14));	// its part moved down 4
	EXPECT(Pixel(18, 10) && !Pixel(18, 23));					// the rest stayed

	// the clip region hides the right half: it does not move
	Square();
	Rect left;
	SetRect(&left, 0, 0, 15, kHeight);
	RectRgn(gPort.clipRgn, &left);
	ScrollRect(&all, 0, 5, update);
	EXPECT(Pixel(12, 22) && !Pixel(12, 12));
	EXPECT(Pixel(17, 12) && !Pixel(17, 22));
	RectRgn(gPort.clipRgn, &all);

	// nothing to do
	Square();
	ScrollRect(&all, 0, 0, update);
	EXPECT(EmptyRgn(update) && OnlyBlack(10, 10, 20, 20));
	gPort.pnVis = -1;
	ScrollRect(&all, 4, 4, update);
	EXPECT(EmptyRgn(update) && OnlyBlack(10, 10, 20, 20));
	gPort.pnVis = 0;

	// LocalToGlobal: the port's bits' bounds taken off
	Point pt = MakePoint(5, 7);
	LocalToGlobal(&pt);
	EXPECT(pt.h == 5 && pt.v == 7);
	OffsetRect(&gMap.bounds, -20, -30);						// the map's origin 20 across, 30 down the port
	SetPortBits(&gMap);
	pt = MakePoint(5, 7);
	LocalToGlobal(&pt);
	EXPECT(pt.h == 25 && pt.v == 37);
	OffsetRect(&gMap.bounds, 20, 30);
	SetPortBits(&gMap);

	DisposeRgn(update);
	ClosePort(&gPort);
	printf("test_ScrollRect: %s\n", failures == 0 ? "ok" : "FAILED");
	return failures == 0 ? 0 : 1;
}
