// The text engine's small helpers (TXUtilities.h): long rectangles, the
// clamping and array arithmetic, the pool of scratch references, and
// the clipping helpers over a QuickDraw port.
#include "TXUtilities.h"
#include "Draw.h"
#include "Rects.h"
#include "Ports.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


static void
TestLongRects()
{
	TXLongRect a = { 10, 20, 50, 80 };
	TXLongRect b = { 30, 60, 100, 200 };
	TXLongRect s;
	EXPECT(a.Sect(b, &s));
	EXPECT(s.top == 30 && s.left == 60 && s.bottom == 50 && s.right == 80);
	TXLongRect c = { 60, 0, 70, 10 };
	EXPECT(!a.Sect(c, &s));									// apart: the (empty) overlap still written
	EXPECT(s.top == 60 && s.bottom == 50);
	a.Offset(5, -10);
	EXPECT(a.top == 0 && a.left == 25 && a.bottom == 40 && a.right == 85);

	TXLongPoint in = { 40, 85 };							// on the bottom and right edges
	EXPECT(a.IsPointInside(in));							// both edges inside (the ROM's)
	TXLongPoint out = { 41, 85 };
	EXPECT(!a.IsPointInside(out));

	EXPECT(TXClipValue(5, 0, 10) == 5);
	EXPECT(TXClipValue(-5, 0, 10) == 0);
	EXPECT(TXClipValue(15, 0, 10) == 10);

	long longs[4] = { 1, 2, 3, 4 };
	TXAddToLongArray(10, longs, 3);
	EXPECT(longs[0] == 11 && longs[2] == 13 && longs[3] == 4);
	struct Elem { long first; long other; } elems[3] = { { 1, 7 }, { 2, 7 }, { 3, 7 } };
	TXAddToArrayElements(100, (char*) elems, 3, sizeof(Elem));
	EXPECT(elems[0].first == 101 && elems[2].first == 103 && elems[1].other == 7);
}


// A pool that counts what it makes and frees.
class CountingReferences : public TXTempReferences
{
public:
			CountingReferences() : fMade(0), fFreed(0)	{ }
	virtual void*	CreateNewReference(void)		{ return (void*) (long) (0x1000 + ++fMade); }
	virtual void	FreeReference(void*)			{ fFreed++; }
	long	fMade;
	long	fFreed;
};


static void
TestTempReferences()
{
	CountingReferences pool;
	void* got[6];
	for (long i = 0; i < 6; i++)
		got[i] = pool.Get();
	EXPECT(pool.fMade == 6);								// five slots filled, one made outside the pool
	pool.Done(got[5]);										// not the pool's: freed
	EXPECT(pool.fFreed == 1);
	pool.Done(got[2]);										// the pool's: kept for the next Get
	EXPECT(pool.fFreed == 1);
	EXPECT(pool.Get() == got[2]);
	EXPECT(pool.fMade == 6);								// ... without making another
}


static void
TestClipping()
{
	const long kSize = 64;
	static unsigned char bits[kSize * kSize];
	PixelMap pm;
	pm.baseAddr = (Ptr) bits;
	pm.rowBytes = (short) (kSize / 8);
	SetRect(&pm.bounds, 0, 0, kSize, kSize);
	pm.pixMapFlags = kPixMapPtr | 1;
	pm.deviceRes.v = kDefaultDPI;
	pm.deviceRes.h = kDefaultDPI;
	pm.grayTable = nil;
	GrafPort port;
	OpenPort(&port);
	SetPortBits(&pm);
	port.portRect = pm.bounds;
	RectRgn(port.visRgn, &pm.bounds);

	gTXTempRegions = new TXTempRegions;

	// a rectangular clip
	Rect clip;
	SetRect(&clip, 10, 10, 40, 40);
	ClipRect(&clip);
	Rect r;
	SetRect(&r, 30, 30, 60, 60);
	EXPECT(TXCalcClipRect(&r));
	EXPECT(r.left == 30 && r.top == 30 && r.right == 40 && r.bottom == 40);
	SetRect(&r, 50, 50, 60, 60);
	EXPECT(!TXCalcClipRect(&r));

	RgnHandle saved = NewRgn();
	SetRect(&r, 20, 20, 50, 50);
	EXPECT(TXClipFurther(&r, saved));
	EXPECT(r.right == 40 && r.bottom == 40);
	EXPECT((*port.clipRgn)->rgnBBox.left == 20 && (*port.clipRgn)->rgnBBox.right == 40);
	EXPECT((*saved)->rgnBBox.left == 10);					// the old clip saved
	SetClip(saved);

	// a clip of two rectangles
	RgnHandle two = NewRgn();
	RgnHandle other = NewRgn();
	SetRectRgn(two, 0, 0, 20, 20);
	SetRectRgn(other, 30, 30, 50, 50);
	UnionRgn(two, other, two);
	SetClip(two);
	SetRect(&r, 10, 10, 60, 60);
	EXPECT(TXCalcClipRect(&r));
	EXPECT(r.left == 10 && r.top == 10 && r.right == 50 && r.bottom == 50);
	SetRect(&r, 22, 22, 28, 28);							// in the bounding box, between the two
	EXPECT(!TXClipFurther(&r, saved));
	EXPECT((*port.clipRgn)->rgnBBox.right == 50);			// the clip put back
	SetRect(&r, 5, 5, 15, 15);
	EXPECT(TXClipFurther(&r, saved));
	EXPECT(r.left == 5 && r.right == 15);

	DisposeRgn(two);
	DisposeRgn(other);

	// scrolling: a black line moved down 4, the strip above it the update
	ClipRect(&pm.bounds);
	memset(bits, 0, sizeof(bits));
	Rect line;
	SetRect(&line, 0, 20, kSize, 21);
	PaintRect(&line);
	RgnHandle update = NewRgn();
	SetRect(&r, 0, 10, kSize, 40);
	EXPECT(TXScrollRect(r, 0, 4, update, false));
	EXPECT(bits[24 * (kSize / 8)] == 0xff && bits[20 * (kSize / 8)] == 0);
	EXPECT((*update)->rgnBBox.top == 10 && (*update)->rgnBBox.bottom == 14);
	// widened by the scroll: [10, 20) scrolled up 8 is [2, 20) moved
	memset(bits, 0, sizeof(bits));
	SetRect(&line, 0, 15, kSize, 16);
	PaintRect(&line);
	SetRect(&r, 0, 10, kSize, 20);
	EXPECT(TXScrollRect(r, 0, -8, update, true));
	EXPECT(bits[7 * (kSize / 8)] == 0xff && bits[15 * (kSize / 8)] == 0);
	EXPECT((*update)->rgnBBox.top == 12 && (*update)->rgnBBox.bottom == 20);
	EXPECT(!TXScrollRect(r, 0, 0, update, false));		// nothing to scroll
	DisposeRgn(update);
	DisposeRgn(saved);
}


int
main()
{
	InitHostStandaloneHeap();
	InitGraf();
	TestLongRects();
	TestTempReferences();
	TestClipping();
	if (failures == 0)
		printf("test_TXUtilities: all passed\n");
	return failures != 0;
}
