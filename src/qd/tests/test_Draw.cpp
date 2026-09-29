// QuickDraw drawing test: a port over an offscreen one-bit map - rectangles
// painted, framed (pen sizes), erased, inverted and filled with patterns,
// regions painted and framed, clipping by the clip region and by a
// complex region, the pen state, the origin; CopyBits between maps in the
// source modes and between depths; a four-bit gray map and the ROM's gray
// "or"; and the inker's line (InkerLine) in every direction.  Every result is checked pixel by pixel.  Runs over a standalone
// kernel heap.
#include "Draw.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

const long kSize = 64;


// an offscreen map of the size, depth and origin, cleared
static PixelMap
MakeMap(unsigned char* bits, long depth, long left = 0, long top = 0)
{
	PixelMap pm;
	pm.baseAddr = (Ptr) bits;
	pm.rowBytes = (short) (kSize * depth / 8);
	SetRect(&pm.bounds, left, top, left + kSize, top + kSize);
	pm.pixMapFlags = kPixMapPtr | depth;
	pm.deviceRes.v = kDefaultDPI;
	pm.deviceRes.h = kDefaultDPI;
	pm.grayTable = nil;
	memset(bits, 0, kSize * kSize);
	return pm;
}


// every pixel of the map against a predicate; the first difference reported
typedef long (*Expected)(long x, long y);

static Boolean
MapIs(const PixelMap* pm, Expected expected, const char* what)
{
	long wrong = 0;
	for (long y = pm->bounds.top; y < pm->bounds.bottom; y++)
		for (long x = pm->bounds.left; x < pm->bounds.right; x++)
		{
			long value = GetPixel(pm, x, y);
			long want = expected(x, y);
			if (value != want)
			{
				if (wrong == 0)
					fprintf(stderr, "  %s: first difference at (%ld, %ld): pixel %ld, expected %ld\n", what, x, y, value, want);
				wrong++;
			}
		}
	return wrong == 0;
}

static Boolean In(long x, long y, long l, long t, long r, long b) { return l <= x && x < r && t <= y && y < b; }

static long	ExpRect(long x, long y)			{ return In(x, y, 10, 10, 30, 20); }
static long	ExpFrame1(long x, long y)		{ return In(x, y, 10, 10, 30, 20) && !In(x, y, 11, 11, 29, 19); }
static long	ExpFrame21(long x, long y)		{ return In(x, y, 10, 10, 30, 20) && !In(x, y, 12, 11, 28, 19); }
static long	ExpFrameThin(long x, long y)	{ return In(x, y, 10, 10, 14, 20); }
static long	ExpErased(long x, long y)		{ return In(x, y, 10, 10, 30, 20) && !In(x, y, 15, 12, 20, 18); }
static long	ExpInverted(long x, long y)		{ return In(x, y, 10, 10, 30, 20) != In(x, y, 20, 15, 40, 25); }
static long	ExpGray(long x, long y)			{ return In(x, y, 0, 0, 16, 16) ? ((x + y) & 1) == 0 : 0; }
static long	ExpClipped(long x, long y)		{ return In(x, y, 0, 0, 64, 64) && In(x, y, 8, 8, 24, 24); }
static long	ExpTwoRects(long x, long y)		{ return In(x, y, 4, 4, 20, 20) || In(x, y, 12, 12, 40, 30); }
static long	ExpRgnFrame(long x, long y)		{ return ExpTwoRects(x, y) && !(In(x, y, 5, 5, 19, 19) || In(x, y, 13, 13, 39, 29)); }
static long	ExpRgnClipped(long x, long y)	{ return ExpTwoRects(x, y) && In(x, y, 0, 0, 64, 16); }
static long	ExpOrigin(long x, long y)		{ return In(x, y, 100, 200, 110, 205); }


static void
TestRects()
{
	static unsigned char bits[kSize * kSize];
	PixelMap pm = MakeMap(bits, 1);
	GrafPort port;
	OpenPort(&port);
	SetPortBits(&pm);
	port.portRect = pm.bounds;
	RectRgn(port.visRgn, &pm.bounds);
	Rect r;
	SetRect(&r, 10, 10, 30, 20);
	PaintRect(&r);
	EXPECT(MapIs(&pm, ExpRect, "PaintRect"));
	EXPECT(PtInPixelMap(&pm, 10, 10) && !PtInPixelMap(&pm, 9, 10) && !PtInPixelMap(&pm, 70, 10));
	// erased inside, inverted across the edge
	Rect hole;
	SetRect(&hole, 15, 12, 20, 18);
	EraseRect(&hole);
	EXPECT(MapIs(&pm, ExpErased, "EraseRect"));
	PaintRect(&hole);
	Rect across;
	SetRect(&across, 20, 15, 40, 25);
	InvertRect(&across);
	EXPECT(MapIs(&pm, ExpInverted, "InvertRect"));
	InvertRect(&across);
	EXPECT(MapIs(&pm, ExpRect, "InvertRect back"));
	// frames: a one-pixel pen, a 2 by 1 pen, and a pen too fat for the width
	EraseRect(&pm.bounds);
	FrameRect(&r);
	EXPECT(MapIs(&pm, ExpFrame1, "FrameRect"));
	EraseRect(&pm.bounds);
	PenSize(2, 1);
	FrameRect(&r);
	EXPECT(MapIs(&pm, ExpFrame21, "FrameRect 2x1"));
	EraseRect(&pm.bounds);
	PenSize(2, 1);
	Rect thin;
	SetRect(&thin, 10, 10, 14, 20);
	FrameRect(&thin);
	EXPECT(MapIs(&pm, ExpFrameThin, "FrameRect thin"));
	PenNormal();
	EXPECT(port.pnSize.h == 1 && port.pnSize.v == 1 && port.pnMode == patCopy && port.fgPat == stdPatterns[blackPat]);
	// a pattern: the gray checkerboard (pattern row y: 0xaa on even rows)
	EraseRect(&pm.bounds);
	Rect square;
	SetRect(&square, 0, 0, 16, 16);
	FillRect(&square, GetStdPattern(grayPat));
	EXPECT(MapIs(&pm, ExpGray, "FillRect gray"));
	EXPECT(port.fgPat == stdPatterns[blackPat]);
	// clipping
	EraseRect(&pm.bounds);
	Rect clip;
	SetRect(&clip, 8, 8, 24, 24);
	ClipRect(&clip);
	PaintRect(&pm.bounds);
	EXPECT(MapIs(&pm, ExpClipped, "ClipRect"));
	RgnHandle wide = NewRgn();
	CopyRgn(wideHandle, wide);
	SetClip(wide);
	RgnHandle got = NewRgn();
	GetClip(got);
	EXPECT(EqualRgn(got, wideHandle));
	// the pen hidden draws nothing
	EraseRect(&pm.bounds);
	HidePen();
	PaintRect(&r);
	ShowPen();
	Boolean blank = true;
	for (long i = 0; i < kSize * kSize; i++)
		if (bits[i] != 0)
			blank = false;
	EXPECT(blank);
	// the pen
	MoveTo(5, 7);
	Move(2, -3);
	Point pen;
	GetPen(&pen);
	EXPECT(pen.h == 7 && pen.v == 4);
	PenState state;
	GetPenState(&state);
	PenSize(3, 3);
	PenMode(patXor);
	SetPenState(&state);
	EXPECT(port.pnSize.h == 1 && port.pnMode == patCopy);
	DisposeRgn(wide);
	DisposeRgn(got);
	ClosePort(&port);
}


static void
TestRegions()
{
	static unsigned char bits[kSize * kSize];
	PixelMap pm = MakeMap(bits, 1);
	GrafPort port;
	OpenPort(&port);
	SetPortBits(&pm);
	port.portRect = pm.bounds;
	RectRgn(port.visRgn, &pm.bounds);
	RgnHandle rgn = NewRgn();
	RgnHandle other = NewRgn();
	SetRectRgn(rgn, 4, 4, 20, 20);
	SetRectRgn(other, 12, 12, 40, 30);
	UnionRgn(rgn, other, rgn);
	PaintRgn(rgn);
	EXPECT(MapIs(&pm, ExpTwoRects, "PaintRgn"));
	EraseRgn(rgn);
	Boolean blank = true;
	for (long i = 0; i < kSize * kSize; i++)
		if (bits[i] != 0)
			blank = false;
	EXPECT(blank);
	FrameRgn(rgn);
	EXPECT(MapIs(&pm, ExpRgnFrame, "FrameRgn"));
	EraseRect(&pm.bounds);
	InvertRgn(rgn);
	EXPECT(MapIs(&pm, ExpTwoRects, "InvertRgn"));
	// a rectangle painted through a complex clip region
	EraseRect(&pm.bounds);
	SetClip(rgn);
	Rect top;
	SetRect(&top, 0, 0, 64, 16);
	PaintRect(&top);
	EXPECT(MapIs(&pm, ExpRgnClipped, "PaintRect clipped by a region"));
	// a region filled through a rectangular clip
	EraseRect(&pm.bounds);
	ClipRect(&top);
	FillRgn(rgn, stdPatterns[blackPat]);
	EXPECT(MapIs(&pm, ExpRgnClipped, "FillRgn clipped"));
	DisposeRgn(rgn);
	DisposeRgn(other);
	ClosePort(&port);
}


static long	ExpCopied(long x, long y)		{ return In(x, y, 30, 30, 50, 40); }
static long	ExpOred(long x, long y)			{ return In(x, y, 30, 30, 50, 40) || In(x, y, 40, 35, 60, 45); }
static long	ExpXored(long x, long y)		{ return In(x, y, 30, 30, 50, 40) != In(x, y, 40, 35, 60, 45); }
static long	ExpBic(long x, long y)			{ return In(x, y, 40, 35, 60, 45) && !In(x, y, 30, 30, 50, 40); }
static long	ExpNotCopied(long x, long y)	{ return In(x, y, 30, 30, 50, 40) ? 0 : (In(x, y, 30, 30, 60, 45) ? 1 : 0); }
static long	ExpScrolled(long x, long y)		{ return In(x, y, 11, 11, 31, 21) ? (((x + y) & 1) == 0) : (In(x, y, 10, 10, 30, 20) ? (((x + y) & 1) == 0) : 0); }
static long	ExpScrolledBack(long x, long y)	{ return In(x, y, 10, 10, 30, 20) ? (((x + y) & 1) == 0) : (In(x, y, 11, 11, 31, 21) ? (((x + y) & 1) == 0) : 0); }
static long	ExpStretched(long x, long y)	{ return In(x, y, 0, 0, 40, 20); }

static void
TestBits()
{
	static unsigned char srcBits[kSize * kSize], dstBits[kSize * kSize];
	PixelMap src = MakeMap(srcBits, 1);
	PixelMap dst = MakeMap(dstBits, 1);
	GrafPort port;
	OpenPort(&port);
	SetPortBits(&dst);
	port.portRect = dst.bounds;
	RectRgn(port.visRgn, &dst.bounds);
	// a 20x10 block in the source at (10, 10), copied to (30, 30)
	Rect block;
	SetRect(&block, 10, 10, 30, 20);
	for (long y = 10; y < 20; y++)
		for (long x = 10; x < 30; x++)
			SetPixel(&src, x, y, 1);
	Rect to;
	SetRect(&to, 30, 30, 50, 40);
	CopyBits(&src, &dst, &block, &to, srcCopy, nil);
	EXPECT(MapIs(&dst, ExpCopied, "CopyBits srcCopy"));
	// the modes over a painted rectangle
	Rect painted;
	SetRect(&painted, 40, 35, 60, 45);
	EraseRect(&dst.bounds);
	PaintRect(&painted);
	CopyBits(&src, &dst, &block, &to, srcOr, nil);
	EXPECT(MapIs(&dst, ExpOred, "CopyBits srcOr"));
	EraseRect(&dst.bounds);
	PaintRect(&painted);
	CopyBits(&src, &dst, &block, &to, srcXor, nil);
	EXPECT(MapIs(&dst, ExpXored, "CopyBits srcXor"));
	EraseRect(&dst.bounds);
	PaintRect(&painted);
	CopyBits(&src, &dst, &block, &to, srcBic, nil);
	EXPECT(MapIs(&dst, ExpBic, "CopyBits srcBic"));
	Rect wider;
	SetRect(&wider, 30, 30, 60, 45);
	EraseRect(&dst.bounds);
	PaintRect(&wider);
	CopyBits(&src, &dst, &block, &to, notSrcCopy, nil);
	EXPECT(MapIs(&dst, ExpNotCopied, "CopyBits notSrcCopy"));
	// through a mask region
	EraseRect(&dst.bounds);
	RgnHandle mask = NewRgn();
	SetRectRgn(mask, 40, 35, 60, 45);
	CopyBits(&src, &dst, &block, &to, srcCopy, mask);
	Rect both;
	EXPECT(SectRect(&to, &(*mask)->rgnBBox, &both) && MapIs(&dst, [](long x, long y) -> long { return In(x, y, 40, 35, 50, 40); }, "CopyBits masked"));
	DisposeRgn(mask);
	// within one map, overlapping: a checkerboard block moved right by one
	// and down by one keeps its phase (a copy in the wrong direction smears)
	EraseRect(&dst.bounds);
	Rect from;
	SetRect(&from, 10, 10, 30, 20);
	FillRect(&from, GetStdPattern(grayPat));
	Rect moved = from;
	OffsetRect(&moved, 1, 1);
	CopyBits(&dst, &dst, &from, &moved, srcCopy, nil);
	EXPECT(MapIs(&dst, ExpScrolled, "CopyBits overlapping"));
	CopyBits(&dst, &dst, &moved, &from, srcCopy, nil);
	EXPECT(MapIs(&dst, ExpScrolledBack, "CopyBits overlapping back"));
	// stretched to twice the size (nearest neighbour)
	EraseRect(&dst.bounds);
	Rect twice;
	SetRect(&twice, 0, 0, 40, 20);
	CopyBits(&src, &dst, &block, &twice, srcCopy, nil);
	EXPECT(MapIs(&dst, ExpStretched, "CopyBits stretched"));

	// StretchBits' routines: a row of alternating pixels, doubled - each
	// pixel twice across and down - then shrunk to half, where each pair
	// of source pixels is ORed into one (so all of it comes out set)
	memset(srcBits, 0, sizeof(srcBits));
	for (long x = 0; x < 16; x += 2)
		SetPixel(&src, x, 0, 1);
	Rect row;
	SetRect(&row, 0, 0, 16, 1);
	EraseRect(&dst.bounds);
	SetRect(&twice, 0, 0, 32, 2);
	CopyBits(&src, &dst, &row, &twice, srcCopy, nil);
	EXPECT(MapIs(&dst, [](long x, long y) -> long { return In(x, y, 0, 0, 32, 2) && ((x >> 1) & 1) == 0; }, "stretched twice"));
	EraseRect(&dst.bounds);
	Rect half;
	SetRect(&half, 0, 0, 8, 1);
	CopyBits(&src, &dst, &row, &half, srcCopy, nil);
	EXPECT(MapIs(&dst, [](long x, long y) -> long { return In(x, y, 0, 0, 8, 1); }, "shrunk to half"));
	// half again (1.5 times): the fraction 2/3 stepped from a third - the
	// first source pixel is written once (the sum reaches one at once), the
	// second twice, and so on: 1 0 0 1 0 0 ... (the set pixels a third)
	EraseRect(&dst.bounds);
	SetRect(&twice, 0, 0, 24, 1);
	CopyBits(&src, &dst, &row, &twice, srcCopy, nil);
	EXPECT(MapIs(&dst, [](long x, long y) -> long { return y == 0 && x < 24 && x % 3 == 0; }, "stretched by half again"));
	ClosePort(&port);

	// one bit into four: a set pixel is 15
	static unsigned char grayBits[kSize * kSize];
	PixelMap gray = MakeMap(grayBits, 4);
	OpenPort(&port);
	SetPortBits(&gray);
	port.portRect = gray.bounds;
	RectRgn(port.visRgn, &gray.bounds);
	SetRect(&twice, 0, 0, 16, 1);
	CopyBits(&src, &gray, &row, &twice, srcCopy, nil);
	EXPECT(GetPixel(&gray, 0, 0) == 15 && GetPixel(&gray, 1, 0) == 0 && GetPixel(&gray, 14, 0) == 15);
	SetRect(&twice, 0, 2, 32, 3);
	CopyBits(&src, &gray, &row, &twice, srcCopy, nil);
	EXPECT(GetPixel(&gray, 0, 2) == 15 && GetPixel(&gray, 1, 2) == 15 && GetPixel(&gray, 2, 2) == 0);
	ClosePort(&port);
}


static long	ExpGrayPaint(long x, long y)	{ return In(x, y, 10, 10, 30, 20) ? 15 : 0; }
static long	ExpGrayPattern(long x, long y)	{ return In(x, y, 0, 0, 16, 16) ? (((x + y) & 1) == 0 ? 15 : 0) : 0; }
static long	ExpGrayOr(long x, long y)		{ return In(x, y, 10, 10, 30, 20) ? (In(x, y, 20, 10, 40, 20) ? 5 : 15) : (In(x, y, 20, 10, 40, 20) ? 5 : 0); }

static void
TestGray()
{
	static unsigned char bits[kSize * kSize];
	PixelMap pm = MakeMap(bits, 4);
	GrafPort port;
	OpenPort(&port);
	SetPortBits(&pm);
	port.portRect = pm.bounds;
	RectRgn(port.visRgn, &pm.bounds);
	Rect r;
	SetRect(&r, 10, 10, 30, 20);
	PaintRect(&r);
	EXPECT(MapIs(&pm, ExpGrayPaint, "PaintRect gray"));
	EXPECT(GetPixel(&pm, 10, 10) == 15 && bits[10 * 32 + 5] == 0xff);
	Rect square;
	SetRect(&square, 0, 0, 16, 16);
	EraseRect(&pm.bounds);
	FillRect(&square, GetStdPattern(grayPat));
	EXPECT(MapIs(&pm, ExpGrayPattern, "FillRect gray pattern on a gray map"));
	// the ROM's "or": a source pixel of 5 replaces 15, white source pixels leave the destination
	static unsigned char srcBits[kSize * kSize];
	PixelMap src = MakeMap(srcBits, 4);
	for (long y = 10; y < 20; y++)
		for (long x = 20; x < 40; x++)
			SetPixel(&src, x, y, 5);
	EraseRect(&pm.bounds);
	PaintRect(&r);
	Rect strip;
	SetRect(&strip, 20, 10, 40, 20);
	CopyBits(&src, &pm, &strip, &strip, srcOr, nil);
	EXPECT(MapIs(&pm, ExpGrayOr, "CopyBits srcOr gray"));
	// a one-bit source is black on the gray map
	static unsigned char oneBits[kSize * kSize];
	PixelMap one = MakeMap(oneBits, 1);
	for (long y = 10; y < 20; y++)
		for (long x = 10; x < 30; x++)
			SetPixel(&one, x, y, 1);
	EraseRect(&pm.bounds);
	CopyBits(&one, &pm, &r, &r, srcCopy, nil);
	EXPECT(MapIs(&pm, ExpGrayPaint, "CopyBits one bit to gray"));
	ClosePort(&port);
}


static void
TestOrigin()
{
	static unsigned char bits[kSize * kSize];
	PixelMap pm = MakeMap(bits, 1);
	GrafPort port;
	OpenPort(&port);
	SetPortBits(&pm);
	port.portRect = pm.bounds;
	RectRgn(port.visRgn, &pm.bounds);
	SetOrigin(100, 200);
	EXPECT(port.portRect.left == 100 && port.portRect.top == 200 && port.portBits.bounds.left == 100 && (*port.visRgn)->rgnBBox.top == 200);
	Rect r;
	SetRect(&r, 100, 200, 110, 205);
	PaintRect(&r);
	EXPECT(MapIs(&port.portBits, ExpOrigin, "PaintRect after SetOrigin"));
	EXPECT(bits[0] == 0xff && bits[1] == 0xc0 && bits[5 * 8] == 0);
	SetOrigin(0, 0);
	EXPECT(port.portRect.left == 0 && port.portBits.bounds.top == 0);
	ClosePort(&port);
}


// The inker's line.  What is checked is the shape of what it draws - the
// ink stays inside the rectangle it says it damaged, every row of that
// rectangle gets something, and a segment drawn backwards leaves exactly
// the same mark as one drawn forwards - and, as much as anything, that it
// survives being asked: a line drawn leftwards or upwards subtracts its
// coordinates the other way round, and a nearly horizontal one gets a
// slope back from FixedDivide that has saturated.  Those are what made the
// first cut of it trap on the host while the ARM would have shrugged.
static long
InkedPixels(const PixelMap* pm, Rect* bounds)
{
	long count = 0;
	SetRect(bounds, 0x7fff, 0x7fff, -0x8000, -0x8000);
	for (long y = pm->bounds.top; y < pm->bounds.bottom; y++)
		for (long x = pm->bounds.left; x < pm->bounds.right; x++)
			if (GetPixel(pm, x, y) != 0)
			{
				count++;
				if (x < bounds->left)		bounds->left = (short) x;
				if (x + 1 > bounds->right)	bounds->right = (short) (x + 1);
				if (y < bounds->top)		bounds->top = (short) y;
				if (y + 1 > bounds->bottom)	bounds->bottom = (short) (y + 1);
			}
	return count;
}

static Point
Pt(long h, long v)
{
	Point pt;
	pt.h = (short) h;
	pt.v = (short) v;
	return pt;
}

static void
TestInker()
{
	static unsigned char bits[kSize * kSize];
	static unsigned char other[kSize * kSize];
	PixelMap pm = MakeMap(bits, 1);
	PixelMap pm2 = MakeMap(other, 1);
	Point pen = Pt(2, 2);

	// the eight directions, and the degenerate ones
	static const short kLines[][4] =
	{
		{  8,  8, 40, 40 },		// right and down
		{ 40, 40,  8,  8 },		// left and up
		{ 40,  8,  8, 40 },		// left and down
		{  8, 40, 40,  8 },		// right and up
		{  8, 20, 40, 20 },		// horizontal, rightwards
		{ 40, 20,  8, 20 },		// horizontal, leftwards
		{ 20,  8, 20, 40 },		// vertical, downwards
		{ 20, 40, 20,  8 },		// vertical, upwards
		{  8, 30, 56, 31 },		// nearly horizontal: the slope saturates
		{ 56, 31,  8, 30 },		// and back
		{ 20, 20, 20, 20 },		// a point
	};
	for (long i = 0; i < (long) (sizeof(kLines) / sizeof(kLines[0])); i++)
	{
		memset(bits, 0, sizeof(bits));
		Point from = Pt(kLines[i][0], kLines[i][1]);
		Point to = Pt(kLines[i][2], kLines[i][3]);
		Rect damaged;
		InkerLine(from, to, &damaged, pen, &pm);
		Rect inked;
		long count = InkedPixels(&pm, &inked);
		EXPECT(count > 0);
		// nothing is drawn outside what it said it damaged
		EXPECT(inked.left >= damaged.left && inked.right <= damaged.right
			&& inked.top >= damaged.top && inked.bottom <= damaged.bottom);
		// and the damage reaches the ends of the segment
		EXPECT(damaged.left <= (from.h < to.h ? from.h : to.h)
			&& damaged.top <= (from.v < to.v ? from.v : to.v));
	}

	// a segment and its reverse leave the same mark
	for (long i = 0; i < 4; i++)
	{
		Point from = Pt(kLines[i * 2][0], kLines[i * 2][1]);
		Point to = Pt(kLines[i * 2][2], kLines[i * 2][3]);
		Rect damaged;
		memset(bits, 0, sizeof(bits));
		InkerLine(from, to, &damaged, pen, &pm);
		memset(other, 0, sizeof(other));
		InkerLine(to, from, &damaged, pen, &pm2);
		EXPECT(memcmp(bits, other, sizeof(bits)) == 0);
	}

	// a 45 degree line puts something on every row it covers
	memset(bits, 0, sizeof(bits));
	Rect damaged;
	InkerLine(Pt(8, 8), Pt(40, 40), &damaged, pen, &pm);
	for (long y = 8; y < 40; y++)
	{
		long on = 0;
		for (long x = 0; x < kSize; x++)
			on += GetPixel(&pm, x, y) != 0;
		EXPECT(on > 0);
	}

	// off the map altogether: nothing drawn, and the damage is empty
	memset(bits, 0, sizeof(bits));
	InkerLine(Pt(200, 200), Pt(220, 230), &damaged, pen, &pm);
	Rect inked;
	EXPECT(InkedPixels(&pm, &inked) == 0);

	// the ink is ORed in, so drawing a segment twice is the segment
	memset(bits, 0, sizeof(bits));
	InkerLine(Pt(10, 12), Pt(50, 30), &damaged, pen, &pm);
	memcpy(other, bits, sizeof(bits));
	InkerLine(Pt(10, 12), Pt(50, 30), &damaged, pen, &pm);
	EXPECT(memcmp(bits, other, sizeof(bits)) == 0);

	// a map a few pixels wide clips what is drawn rather than running off it
	PixelMap narrow = MakeMap(bits, 1);
	SetRect(&narrow.bounds, 0, 0, 16, 16);
	narrow.rowBytes = 2;
	memset(bits, 0, sizeof(bits));
	InkerLine(Pt(-20, -20), Pt(60, 60), &damaged, pen, &narrow);
	EXPECT(damaged.left >= 0 && damaged.top >= 0 && damaged.right <= 16 && damaged.bottom <= 16);
	EXPECT(InkedPixels(&narrow, &inked) > 0);
}


int
main()
{
	InitHostStandaloneHeap();
	InitGraf();
	EXPECT(gQDRunning && GetCurrentPort() == &gGrafPort && IsWideOpenRgn(wideHandle));
	EXPECT((*stdPatterns[grayPat])->rowBytes == 1 && ((unsigned char*) GetPixelMapBits(*stdPatterns[grayPat]))[0] == 0xaa);
	EXPECT(GetStdPattern(9) == stdPatterns[blackPat]);
	PatternHandle mine = MakeSimplePattern(1, 2, 3, 4, 5, 6, 7, 8);
	EXPECT(((unsigned char*) GetPixelMapBits(*mine))[7] == 8 && GetPixelMapSize(*mine) == 0x18);		// (no version bits: the size before the gray table)
	// the rows lie inside the handle, after the host's (bigger) PixelMap
	EXPECT(GetHandleSize((Handle) mine) >= (long) sizeof(PixelMap) + 8
		   && (char*) GetPixelMapBits(*mine) + 8 <= (char*) *mine + GetHandleSize((Handle) mine));
	DisposePattern(mine);
	DisposePattern(stdPatterns[whitePat]);
	EXPECT(stdPatterns[whitePat] != nil && (*stdPatterns[whitePat])->rowBytes == 1);
	TestRects();
	TestRegions();
	TestBits();
	TestGray();
	TestOrigin();
	TestInker();
	if (failures == 0)
		printf("test_Draw: all passed\n");
	else
		printf("test_Draw: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
