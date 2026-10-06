// StretchBits onto an eight-bit map (qd/Stretch.cpp, "Eight-bit
// destinations": a host extension - the ROM draws nothing onto one) held to
// what it is defined to be: the pixels the same call puts on a four-bit map,
// widened (each four-bit gray v as v * 17).  Random one-, two- and four-bit
// sources go onto a four-bit map of random pixels and onto its widened
// eight-bit copy - copied, stretched and shrunk, in all eight modes, clipped
// by random regions - and the eight-bit map must be the four-bit one
// widened, byte for byte.  (Copy, the gray "or", xor and bic all commute
// with widening: v * 17 repeats the nibble.)  An eight-bit source onto an
// eight-bit map is checked for its own rule: unscaled it is copied, and
// every pixel drawn is one of the source's.  Runs over a standalone kernel
// heap.
#include "Draw.h"
#include "Ports.h"
#include "memory/host/KernelHeap.h"
#include "host/RomBugs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static unsigned long gSeed = 0x57a8;
static long
Rnd(long n)
{
	gSeed = gSeed * 1103515245u + 12345u;
	return (long) ((gSeed >> 16) & 0x7fff) % n;
}

const long kWidth = 77;
const long kHeight = 53;

struct Map
{
	PixelMap		pm;
	unsigned char*	bits;
	long			size;
};

static void
MakeMap(Map* m, long width, long height, long depth, long left, long top)
{
	long rowBytes = ((width * depth + 31) / 32) * 4;
	m->size = rowBytes * height;
	m->bits = (unsigned char*) calloc(m->size + 64, 1);		// (a little past the end: the row routines read a word ahead)
	m->pm.baseAddr = (Ptr) m->bits;
	m->pm.rowBytes = (short) rowBytes;
	SetRect(&m->pm.bounds, left, top, left + width, top + height);
	m->pm.pixMapFlags = kPixMapPtr | depth;
	m->pm.deviceRes.v = kDefaultDPI;
	m->pm.deviceRes.h = kDefaultDPI;
	m->pm.grayTable = nil;
}

static long
Pixel(const Map* m, long x, long y, long depth)
{
	long bit = x * depth;
	const unsigned char* row = m->bits + y * m->pm.rowBytes;
	if (depth == 8)
		return row[x];
	return (row[bit >> 3] >> (8 - depth - (bit & 7))) & ((1 << depth) - 1);
}

static RgnHandle
RandomRegion(const Rect* in)
{
	RgnHandle rgn = NewRgn();
	RgnHandle piece = NewRgn();
	long n = 1 + Rnd(3);
	for (long i = 0; i < n; i++)
	{
		Rect r;
		long w = in->right - in->left, h = in->bottom - in->top;
		long l = in->left + Rnd(w), t = in->top + Rnd(h);
		SetRect(&r, l, t, l + 1 + Rnd(w), t + 1 + Rnd(h));
		RectRgn(piece, &r);
		if (i > 0 && Rnd(3) == 0)
			DiffRgn(rgn, piece, rgn);
		else
			UnionRgn(rgn, piece, rgn);
	}
	DisposeRgn(piece);
	return rgn;
}


static void
TestWidened(long scenes)
{
	const long depths[3] = { 1, 2, 4 };
	long compared = 0, differing = 0;
	for (long s = 0; s < scenes; s++)
	{
		long srcDepth = depths[Rnd(3)];
		long sw = 1 + Rnd(60), sh = 1 + Rnd(40);
		Map src, d4, d8;
		MakeMap(&src, sw, sh, srcDepth, Rnd(20) - 10, Rnd(20) - 10);
		for (long i = 0; i < src.size; i++)
			src.bits[i] = (unsigned char) Rnd(256);
		long left = Rnd(20) - 10, top = Rnd(20) - 10;
		MakeMap(&d4, kWidth, kHeight, 4, left, top);
		MakeMap(&d8, kWidth, kHeight, 8, left, top);
		for (long y = 0; y < kHeight; y++)
			for (long x = 0; x < kWidth; x += 2)
			{
				unsigned char b = (unsigned char) Rnd(256);
				d4.bits[y * d4.pm.rowBytes + x / 2] = b;
				d8.bits[y * d8.pm.rowBytes + x] = (unsigned char) ((b >> 4) * 17);
				if (x + 1 < kWidth)
					d8.bits[y * d8.pm.rowBytes + x + 1] = (unsigned char) ((b & 15) * 17);
			}
		Rect sr = src.pm.bounds;
		if (Rnd(2))
		{
			// (part of the source)
			long w = sw, h = sh;
			long l = Rnd(w), t = Rnd(h);
			SetRect(&sr, src.pm.bounds.left + l, src.pm.bounds.top + t, src.pm.bounds.left + l + 1 + Rnd(w - l), src.pm.bounds.top + t + 1 + Rnd(h - t));
		}
		Rect dr;
		long kind = Rnd(3);			// unscaled, stretched, shrunk
		long dw = kind == 0 ? sr.right - sr.left : kind == 1 ? (sr.right - sr.left) + 1 + Rnd(40) : 1 + Rnd(sr.right - sr.left);
		long dh = kind == 0 ? sr.bottom - sr.top : kind == 1 ? (sr.bottom - sr.top) + 1 + Rnd(30) : 1 + Rnd(sr.bottom - sr.top);
		long dl = left + Rnd(kWidth) - 10, dt = top + Rnd(kHeight) - 10;
		SetRect(&dr, dl, dt, dl + dw, dt + dh);
		long mode = Rnd(8);
		RgnHandle clip = Rnd(2) ? RandomRegion(&d4.pm.bounds) : nil;
		RgnHandle wide = NewRgn();
		SetRectRgn(wide, -32767, -32767, 32767, 32767);
		// (each map the port's bits while it is drawn on: the regions' masks
		// are made at the port's depth, as they are when the screen is drawn on)
		GrafPort port;
		OpenPort(&port);
		SetPortBits(&d4.pm);
		StretchBits(&src.pm, &d4.pm, &sr, &dr, mode, clip != nil ? clip : wide, wide, wide);
		SetPortBits(&d8.pm);
		StretchBits(&src.pm, &d8.pm, &sr, &dr, mode, clip != nil ? clip : wide, wide, wide);
		ClosePort(&port);
		SetPort(&gGrafPort);
		compared++;
		long bad = 0;
		for (long y = 0; y < kHeight; y++)
			for (long x = 0; x < kWidth; x++)
				if (Pixel(&d8, x, y, 8) != Pixel(&d4, x, y, 4) * 17)
					bad++;
		if (bad != 0)
		{
			differing++;
			if (differing <= 5)
				fprintf(stderr, "  scene %ld: %ld-bit %ldx%ld -> %ldx%ld mode %ld%s: %ld pixels differ\n",
						s, srcDepth, (long) (sr.right - sr.left), (long) (sr.bottom - sr.top), dw, dh, mode, clip ? " clipped" : "", bad);
		}
		if (clip != nil)
			DisposeRgn(clip);
		DisposeRgn(wide);
		free(src.bits);
		free(d4.bits);
		free(d8.bits);
	}
	printf("test_Stretch8: %ld scenes onto four and eight bits, %ld differing\n", compared, differing);
	EXPECT(differing == 0);
}


// an eight-bit source: unscaled it is copied; stretched or shrunk every
// pixel drawn is one of the source's
static void
TestEightToEight(long scenes)
{
	long bad = 0;
	for (long s = 0; s < scenes; s++)
	{
		long sw = 1 + Rnd(50), sh = 1 + Rnd(40);
		Map src, dst;
		MakeMap(&src, sw, sh, 8, 0, 0);
		bool used[256] = { false };
		for (long y = 0; y < sh; y++)
			for (long x = 0; x < sw; x++)
			{
				unsigned char v = (unsigned char) (1 + Rnd(255));
				src.bits[y * src.pm.rowBytes + x] = v;
				used[v] = true;
			}
		MakeMap(&dst, kWidth, kHeight, 8, 0, 0);
		long kind = Rnd(3);
		long dw = kind == 0 ? sw : kind == 1 ? sw + 1 + Rnd(20) : 1 + Rnd(sw);
		long dh = kind == 0 ? sh : kind == 1 ? sh + 1 + Rnd(10) : 1 + Rnd(sh);
		Rect sr = src.pm.bounds, dr;
		SetRect(&dr, 3, 2, 3 + dw, 2 + dh);
		RgnHandle wide = NewRgn();
		SetRectRgn(wide, -32767, -32767, 32767, 32767);
		StretchBits(&src.pm, &dst.pm, &sr, &dr, 0, wide, wide, wide);
		for (long y = 0; y < kHeight; y++)
			for (long x = 0; x < kWidth; x++)
			{
				long v = Pixel(&dst, x, y, 8);
				bool inside = x >= dr.left && x < dr.right && y >= dr.top && y < dr.bottom;
				if (!inside && v != 0)
					bad++;
				if (inside && !used[v])
					bad++;
				if (inside && kind == 0 && v != Pixel(&src, x - dr.left, y - dr.top, 8))
					bad++;
			}
		DisposeRgn(wide);
		free(src.bits);
		free(dst.bits);
	}
	printf("test_Stretch8: %ld eight-bit sources, %ld bad pixels\n", scenes, bad);
	EXPECT(bad == 0);
}


// (one source drawn unscaled or shrunk onto a fresh four-bit map)
static void
DrawOnto4(Map* src, Map* d4, long dw, long dh)
{
	MakeMap(d4, dw, dh, 4, 0, 0);
	Rect sr = src->pm.bounds, dr;
	SetRect(&dr, 0, 0, dw, dh);
	RgnHandle wide = NewRgn();
	SetRectRgn(wide, -32767, -32767, 32767, 32767);
	GrafPort port;
	OpenPort(&port);
	SetPortBits(&d4->pm);
	StretchBits(&src->pm, &d4->pm, &sr, &dr, srcCopy, wide, wide, wide);
	ClosePort(&port);
	SetPort(&gGrafPort);
	DisposeRgn(wide);
}


// The ROM's bugs in StretchBits' row routines, and their fixes: two bits
// into four at the same width took the one-to-two routine (a gray 1 came
// out 3, not 5); and a further row of 32-bit pixels was folded in a pixel
// off, its colours taken as eight bits where RGBtoGray wants sixteen, and
// ORed into the nibble.
static void
TestRomBugFixes()
{
	Map src, d4;
	MakeMap(&src, 8, 1, 2, 0, 0);
	src.bits[0] = src.bits[1] = 0x55;				// every pixel 1
	SetRomBugFixed(false);
	DrawOnto4(&src, &d4, 8, 1);
	EXPECT(Pixel(&d4, 0, 0, 4) == 3 && Pixel(&d4, 7, 0, 4) == 3);
	free(d4.bits);
	SetRomBugFixed(true);
	DrawOnto4(&src, &d4, 8, 1);
	EXPECT(Pixel(&d4, 0, 0, 4) == 5 && Pixel(&d4, 7, 0, 4) == 5);
	free(d4.bits);
	free(src.bits);

	// two rows of 32-bit pixels shrunk into one: the first all white, the
	// second black only at its first pixel - the darker kept, in place
	MakeMap(&src, 4, 2, 32, 0, 0);
	memset(src.bits, 0xff, 16);
	for (long i = 0; i < 4; i++)
		src.bits[i * 4] = 0;						// the pad bytes
	memset(src.bits + 16, 0xff, 16);
	memset(src.bits + 16, 0, 4);					// (0, 0) black
	for (long i = 1; i < 4; i++)
		src.bits[16 + i * 4] = 0;
	DrawOnto4(&src, &d4, 4, 1);
	EXPECT((ULong) Pixel(&d4, 0, 0, 4) == RGBtoGray(0, 0, 0, 8, 4));
	EXPECT((ULong) Pixel(&d4, 1, 0, 4) == RGBtoGray(0xff00, 0xff00, 0xff00, 8, 4));
	EXPECT((ULong) Pixel(&d4, 2, 0, 4) == RGBtoGray(0xff00, 0xff00, 0xff00, 8, 4));
	free(d4.bits);
	free(src.bits);
}


int
main(int argc, char** argv)
{
	InitHostStandaloneHeap();
	InitGraf();
	TestWidened(argc > 1 ? atol(argv[1]) : 600);
	TestEightToEight(200);
	TestRomBugFixes();
	if (failures == 0)
		printf("test_Stretch8: all passed\n");
	else
		printf("test_Stretch8: %d failures\n", failures);
	return failures != 0;
}
