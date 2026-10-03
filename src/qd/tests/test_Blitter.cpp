// The blitter's two ways against each other (qd/Draw.h: SetQDSlowBlitter).
// The slow one - a pixel at a time through GetPixel/SetPixel, as the
// blitter was first written - is the oracle; the fast one must draw the
// same pixels bit for bit.  Scenes of random drawing, made from a seed, are
// drawn twice over the same starting pixels, once each way, and every byte
// of the maps compared: rectangles, regions, ovals, round rectangles,
// arcs, polygons and lines (straight across and down among them: DrawLine
// draws a run as one rectangle, except under the slow blitter) in every
// verb and all sixteen pen modes, with the standard patterns, one of our
// own and one wider than the blitter makes a row of at once, pens of
// several sizes, the pattern alignment moved; CopyBits in the eight source
// modes between maps of depths 1, 2, 4 and 8 (a gray source onto a one-bit
// map, and back), with and without a mask region, and within one map in
// both directions (the scroll's overlap); RgnBlt itself between maps of
// different depths with the port's bits of a third (its masks at a depth
// not the destination's); ScrollRect; all clipped by a complex clip region
// and a visible region, the port's origin moved so that the maps' edges
// fall anywhere in a byte.  Every path of BlitPixelsFast is reached: an
// error planted in any one of them shows in tens of scenes or more.  Runs
// over a standalone kernel heap.
#include "Draw.h"
#include "Shapes.h"
#include "Polygons.h"
#include "ScrollRect.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// a small generator of our own, so that the scene is the same both ways
static unsigned long gSeed;
static long
Rnd(long n)
{
	gSeed = gSeed * 1103515245u + 12345u;
	return (long) ((gSeed >> 16) & 0x7fff) % n;
}

const long kWidth = 83;			// (odd: the rows end part way through a byte at every depth)
const long kHeight = 61;

struct Map
{
	PixelMap		pm;
	unsigned char*	bits;
	long			size;
};

static void
MakeMap(Map* m, long depth, long left, long top)
{
	long rowBytes = ((kWidth * depth + 31) / 32) * 4;
	m->size = rowBytes * kHeight;
	m->bits = (unsigned char*) malloc(m->size);
	m->pm.baseAddr = (Ptr) m->bits;
	m->pm.rowBytes = (short) rowBytes;
	SetRect(&m->pm.bounds, left, top, left + kWidth, top + kHeight);
	m->pm.pixMapFlags = kPixMapPtr | depth;
	m->pm.deviceRes.v = kDefaultDPI;
	m->pm.deviceRes.h = kDefaultDPI;
	m->pm.grayTable = nil;
}

static void
RandomRect(Rect* r, const Rect* in)
{
	long w = in->right - in->left, h = in->bottom - in->top;
	long l = in->left + Rnd(w + 10) - 5, t = in->top + Rnd(h + 10) - 5;
	SetRect(r, l, t, l + Rnd(w) + 1, t + Rnd(h) + 1);
}

// A rectangle wholly inside `in`: what a copy's source must be - QuickDraw
// reads the source rectangle without clipping it to the source map, so a
// source partly outside it reads whatever memory lies there (as the ROM's
// does), which would make the two ways differ for no fault of theirs.
static void
RandomInside(Rect* r, const Rect* in)
{
	long w = in->right - in->left, h = in->bottom - in->top;
	long l = in->left + Rnd(w), t = in->top + Rnd(h);
	SetRect(r, l, t, l + 1 + Rnd(in->right - l), t + 1 + Rnd(in->bottom - t));
}

static PatternHandle gOwnPattern;
static PatternHandle gWidePattern;

static PatternHandle
RandomPattern()
{
	long k = Rnd(7);
	return k == 6 ? gWidePattern : k == 5 ? gOwnPattern : GetStdPattern(k);
}

// A pattern wider than the blitter makes a row of at once (72 x 5, two
// bits a pixel, its rows not a power of two)
static PatternHandle
MakeWidePattern(void)
{
	const long width = 72, height = 5, depth = 2, rowBytes = width * depth / 8;
	PatternHandle pattern = (PatternHandle) NewHandle(kPatternPixelsOffset + rowBytes * height);
	PixelMap* pm = *pattern;
	pm->baseAddr = (Ptr) (intptr_t) kPatternPixelsOffset;
	pm->rowBytes = rowBytes;
	SetRect(&pm->bounds, 0, 0, width, height);
	pm->pixMapFlags = kPixMapOffset | depth;
	pm->deviceRes.v = kDefaultDPI;
	pm->deviceRes.h = kDefaultDPI;
	pm->grayTable = nil;
	unsigned char* bits = (unsigned char*) pm + kPatternPixelsOffset;
	for (long i = 0; i < rowBytes * height; i++)
		bits[i] = (unsigned char) (i * 37 + (i >> 3) * 11);
	return pattern;
}

// A region of a few rectangles, unioned, with a hole
static RgnHandle
RandomRegion(const Rect* in)
{
	RgnHandle rgn = NewRgn();
	RgnHandle piece = NewRgn();
	long n = 1 + Rnd(4);
	for (long i = 0; i < n; i++)
	{
		Rect r;
		RandomRect(&r, in);
		RectRgn(piece, &r);
		if (i > 0 && Rnd(3) == 0)
			DiffRgn(rgn, piece, rgn);
		else
			UnionRgn(rgn, piece, rgn);
	}
	if (Rnd(2) == 0)
	{
		Rect r;
		RandomRect(&r, in);
		RgnHandle oval = OvalRgn(&r, 0, 0);
		if (oval != nil)
		{
			XorRgn(rgn, oval, rgn);
			DisposeRgn(oval);
		}
	}
	DisposeRgn(piece);
	return rgn;
}

// One scene drawn into `dst` (the others the sources), from the seed
static void
DrawScene(unsigned long seed, Map* dst, Map* sources, long sourceCount)
{
	gSeed = seed;
	GrafPort port;
	OpenPort(&port);
	SetPortBits(&dst->pm);
	port.portRect = dst->pm.bounds;
	Rect bounds = dst->pm.bounds;
	// the visible region: the map, or less of it
	if (Rnd(3) == 0)
	{
		RgnHandle vis = RandomRegion(&bounds);
		CopyRgn(vis, port.visRgn);
		DisposeRgn(vis);
	}
	else
		RectRgn(port.visRgn, &bounds);
	long steps = 30 + Rnd(30);
	for (long s = 0; s < steps; s++)
	{
		// now and then a new clip, a pen, a mode, a pattern alignment
		if (Rnd(5) == 0)
		{
			if (Rnd(3) == 0)
			{
				RgnHandle clip = NewRgn();
				SetRectRgn(clip, -32767, -32767, 32767, 32767);
				SetClip(clip);
				DisposeRgn(clip);
			}
			else
			{
				RgnHandle clip = RandomRegion(&bounds);
				SetClip(clip);
				DisposeRgn(clip);
			}
		}
		PenSize(1 + Rnd(4), 1 + Rnd(4));
		PenMode(8 + Rnd(8));
		SetFgPattern(RandomPattern());
		SetBgPattern(RandomPattern());
		port.patAlign.h = (short) Rnd(16);
		port.patAlign.v = (short) Rnd(16);
		Rect r;
		RandomRect(&r, &bounds);
		long what = Rnd(16);
		if (getenv("TRACE_BLITTER"))
			fprintf(stderr, "step %ld: %ld\n", s, what);
		// (TEST_BLITTER_ONLY=n: only that kind of step drawn - finding which one misbehaves)
		if (getenv("TEST_BLITTER_ONLY") && atol(getenv("TEST_BLITTER_ONLY")) != what)
			continue;
		switch (what)
		{
		case 0:		PaintRect(&r); break;
		case 1:		FrameRect(&r); break;
		case 2:		EraseRect(&r); break;
		case 3:		InvertRect(&r); break;
		case 4:		FillRect(&r, RandomPattern()); break;
		case 5:
			{
				RgnHandle rgn = RandomRegion(&bounds);
				switch (Rnd(5))
				{
				case 0: PaintRgn(rgn); break;
				case 1: FrameRgn(rgn); break;
				case 2: EraseRgn(rgn); break;
				case 3: InvertRgn(rgn); break;
				default: FillRgn(rgn, RandomPattern()); break;
				}
				DisposeRgn(rgn);
			}
			break;
		case 6:		if (Rnd(2)) PaintOval(&r); else FrameOval(&r); break;
		case 7:
			if (Rnd(2))
			{
				if (getenv("TRACE_BLITTER")) fprintf(stderr, "invert %d %d %d %d\n", r.left, r.top, r.right, r.bottom);
				InvertOval(&r);
			}
			else
			{
				PatternHandle p = RandomPattern();
				if (getenv("TRACE_BLITTER")) fprintf(stderr, "fill %d %d %d %d with %p\n", r.left, r.top, r.right, r.bottom, (void*) p);
				FillOval(&r, p);
			}
			break;
		case 8:		if (Rnd(2)) FrameRoundRect(&r, Rnd(20), Rnd(20)); else PaintRoundRect(&r, Rnd(20), Rnd(20)); break;
		case 9:		if (Rnd(2)) PaintArc(&r, Rnd(360), Rnd(360) - 180); else FrameArc(&r, Rnd(360), Rnd(360) - 180); break;
		case 10:
			MoveTo(bounds.left + Rnd(kWidth), bounds.top + Rnd(kHeight));
			for (long k = 0; k < 4; k++)
				LineTo(bounds.left + Rnd(kWidth + 20) - 10, bounds.top + Rnd(kHeight + 20) - 10);
			// (and straight across and down: a row or column drawn as one run)
			Line(Rnd(kWidth + 20) - 10, 0);
			Line(0, Rnd(kHeight + 20) - 10);
			break;
		case 11:
			{
				PolyHandle poly = OpenPoly();
				MoveTo(bounds.left + Rnd(kWidth), bounds.top + Rnd(kHeight));
				for (long k = 0; k < 5; k++)
					LineTo(bounds.left + Rnd(kWidth), bounds.top + Rnd(kHeight));
				ClosePoly();
				if (poly != nil)
				{
					if (Rnd(2)) PaintPoly(poly); else FramePoly(poly);
					KillPoly(poly);
				}
			}
			break;
		case 12:
			{
				// CopyBits from another map (any depth) or from this one (overlapping)
				Map* from = Rnd(3) == 0 ? dst : &sources[Rnd(sourceCount)];
				Rect sr, dr;
				RandomInside(&sr, &from->pm.bounds);
				dr = sr;
				if (from == dst)
					OffsetRect(&dr, Rnd(21) - 10, Rnd(21) - 10);
				else
					OffsetRect(&dr, bounds.left - from->pm.bounds.left + Rnd(21) - 10, bounds.top - from->pm.bounds.top + Rnd(21) - 10);
				RgnHandle mask = nil;
				if (Rnd(3) == 0)
					mask = RandomRegion(&bounds);
				CopyBits(&from->pm, &dst->pm, &sr, &dr, Rnd(8), mask);
				if (mask != nil)
					DisposeRgn(mask);
			}
			break;
		case 14:
			{
				// CopyBits between rectangles of different sizes (StretchBits:
				// what text and scaled pictures come to), from another map
				Map* from = &sources[Rnd(sourceCount)];
				Rect sr, dr;
				RandomInside(&sr, &from->pm.bounds);
				RandomRect(&dr, &bounds);
				CopyBits(&from->pm, &dst->pm, &sr, &dr, Rnd(8), nil);
			}
			break;
		case 15:
			{
				// RgnBlt itself from a map of any depth, in any of the sixteen
				// modes, the port's bits a map of any depth meanwhile - the
				// regions' masks come at the port's depth, not the destination's
				Map* from = &sources[Rnd(sourceCount)];
				Map* portMap = &sources[Rnd(sourceCount)];
				Rect sr, dr;
				RandomInside(&sr, &from->pm.bounds);
				dr = sr;
				OffsetRect(&dr, bounds.left - from->pm.bounds.left + Rnd(21) - 10, bounds.top - from->pm.bounds.top + Rnd(21) - 10);
				RgnHandle clip = RandomRegion(&bounds);
				SetPortBits(&portMap->pm);
				RgnBlt(&from->pm, &dst->pm, &sr, &dr, Rnd(16), port.fgPat, port.visRgn, port.clipRgn, clip);
				SetPortBits(&dst->pm);
				DisposeRgn(clip);
			}
			break;
		default:
			{
				RgnHandle update = NewRgn();
				Rect inside;
				RandomInside(&inside, &bounds);
				ScrollRect(&inside, Rnd(21) - 10, Rnd(21) - 10, update);
				DisposeRgn(update);
			}
			break;
		}
	}
	// (ClosePort disposes the port's patterns: ours is shared between scenes)
	SetFgPattern(GetStdPattern(blackPat));
	SetBgPattern(GetStdPattern(whitePat));
	ClosePort(&port);
	SetPort(&gGrafPort);
}


// every scene at every depth, drawn both ways and compared
static void
TestScenes(long sceneCount)
{
	const long depths[4] = { 1, 2, 4, 8 };
	long compared = 0, differing = 0;
	for (long d = 0; d < 4; d++)
	{
		for (long scene = 0; scene < sceneCount; scene++)
		{
			unsigned long seed = 0x5eed0000u + (unsigned long) (d * 1000 + scene);
			gSeed = seed ^ 0x9e3779b9u;
			// the maps' origins anywhere, so that their edges fall mid-byte
			long left = Rnd(40) - 20, top = Rnd(40) - 20;
			Map dst[2], sources[2][4];
			for (long way = 0; way < 2; way++)
			{
				MakeMap(&dst[way], depths[d], left, top);
				for (long s = 0; s < 4; s++)
					MakeMap(&sources[way][s], depths[s], Rnd(40) - 20, Rnd(40) - 20);
			}
			// the same starting pixels both ways
			for (long i = 0; i < dst[0].size; i++)
				dst[0].bits[i] = (unsigned char) Rnd(256);
			memcpy(dst[1].bits, dst[0].bits, dst[0].size);
			for (long s = 0; s < 4; s++)
			{
				for (long i = 0; i < sources[0][s].size; i++)
					sources[0][s].bits[i] = (unsigned char) Rnd(256);
				memcpy(sources[1][s].bits, sources[0][s].bits, sources[0][s].size);
			}
			for (long way = 0; way < 2; way++)
			{
				SetQDSlowBlitter(way == 0 || getenv("TEST_BLITTER_BOTH_SLOW") != nil);
				DrawScene(seed, &dst[way], sources[way], 4);
			}
			compared++;
			if (memcmp(dst[0].bits, dst[1].bits, dst[0].size) != 0)
			{
				differing++;
				if (differing <= 5)
				{
					long first = 0;
					while (dst[0].bits[first] == dst[1].bits[first])
						first++;
					fprintf(stderr, "  depth %ld scene %ld: byte %ld (row %ld) differs: %02x slow, %02x fast\n",
							depths[d], scene, first, first / dst[0].pm.rowBytes, dst[0].bits[first], dst[1].bits[first]);
				}
			}
			for (long way = 0; way < 2; way++)
			{
				free(dst[way].bits);
				for (long s = 0; s < 4; s++)
					free(sources[way][s].bits);
			}
		}
	}
	SetQDSlowBlitter(false);
	printf("test_Blitter: %ld scenes drawn both ways, %ld differing\n", compared, differing);
	EXPECT(differing == 0);
}


#ifdef _WIN32
// a crash says where it happened (as newton.cpp's does; tools/host/whichfunction.py names it)
extern "C" {
struct TestExceptionRecord { unsigned long fCode; unsigned long fFlags; void* fNext; void* fAddress; };
struct TestExceptionPointers { TestExceptionRecord* fRecord; void* fContext; };
typedef long (__stdcall *TestExceptionFilter)(TestExceptionPointers*);
__declspec(dllimport) TestExceptionFilter __stdcall SetUnhandledExceptionFilter(TestExceptionFilter filter);
__declspec(dllimport) void* __stdcall GetModuleHandleA(const char* name);
}
static long __stdcall
Crashed(TestExceptionPointers* info)
{
	fprintf(stderr, "test_Blitter: crashed (%#lx) at image + %#lx\n", info->fRecord->fCode,
			(unsigned long) ((char*) info->fRecord->fAddress - (char*) GetModuleHandleA(nil)));
	fflush(stderr);
	_exit(139);
	return 0;
}
#endif


int
main(int argc, char** argv)
{
#ifdef _WIN32
	SetUnhandledExceptionFilter(Crashed);
#endif
	InitHostStandaloneHeap();
	InitGraf();
	gOwnPattern = MakeSimplePattern(0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81);
	gWidePattern = MakeWidePattern();
	TestScenes(argc > 1 ? atol(argv[1]) : 60);
	if (failures == 0)
		printf("test_Blitter: all passed\n");
	else
		printf("test_Blitter: %d failures\n", failures);
	return failures != 0;
}
