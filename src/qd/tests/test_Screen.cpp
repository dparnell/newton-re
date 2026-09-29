// Screen test: the host display driver behind QuickDraw's screen pixel
// map - InitScreen sizing the screen from the driver, the drawing
// brackets carrying what was drawn to the display (through RgnBlt's
// QDStartDrawing/QDStopDrawing), the deferral under StartDrawing/
// StopDrawing, the orientation, GetGrafInfo, and the image written out;
// and the driver's Blit against a pixel-at-a-time reference.
// Runs over a standalone kernel heap.
#include "Screen.h"
#include "HostScreen.h"
#include "Rects.h"
#include "Draw.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


// The display driver's Blit (which reads a whole byte of pixels at a time)
// against a pixel-at-a-time reference: maps of depths 1, 2, 4 and 8 at odd
// origins, random pixels, random rectangles partly off the map and the
// display, srcCopy and the inker's srcOr - every gray compared.
static unsigned long gSeed = 12345;
static long Rnd(long n) { gSeed = gSeed * 1103515245u + 12345u; return (long) ((gSeed >> 16) & 0x7fff) % n; }

static void
TestBlitAgainstReference(void)
{
	THostScreenDriver* display = new THostScreenDriver;
	display->New();
	display->Configure(61, 37, 1, 100);
	display->ScreenSetup();
	long width = display->Width(), height = display->Height();
	unsigned char* want = new unsigned char[width * height];
	long wrong = 0;
	for (long round = 0; round < 400; round++)
	{
		long depth = 1 << Rnd(4);
		long maxValue = (1 << depth) - 1;
		PixelMap map;
		long mw = 20 + Rnd(50), mh = 10 + Rnd(40);
		map.rowBytes = (short) (((mw * depth + 31) / 32) * 4);
		unsigned char* bits = new unsigned char[map.rowBytes * mh];
		for (long i = 0; i < map.rowBytes * mh; i++)
			bits[i] = (unsigned char) Rnd(256);
		map.baseAddr = (Ptr) bits;
		long left = Rnd(21) - 10, top = Rnd(21) - 10;
		SetRect(&map.bounds, left, top, left + mw, top + mh);
		map.pixMapFlags = kPixMapPtr | depth;
		Rect src;
		long l = left + Rnd(mw + 10) - 5, t = top + Rnd(mh + 10) - 5;
		SetRect(&src, l, t, l + Rnd(mw), t + Rnd(mh));
		Rect dst = src;
		OffsetRect(&dst, Rnd(21) - 10, Rnd(21) - 10);
		long mode = Rnd(2) ? srcCopy : srcOr;
		// the reference: what is there now, then each pixel alone
		memcpy(want, display->Pixels(), width * height);
		for (long y = src.top; y < src.bottom; y++)
		{
			long dy = dst.top + (y - src.top);
			if (dy < 0 || dy >= height || y < map.bounds.top || y >= map.bounds.bottom)
				continue;
			for (long x = src.left; x < src.right; x++)
			{
				long dx = dst.left + (x - src.left);
				if (dx < 0 || dx >= width || x < map.bounds.left || x >= map.bounds.right)
					continue;
				long bit = (x - map.bounds.left) * depth;
				const unsigned char* row = bits + (y - map.bounds.top) * map.rowBytes;
				long value = depth == 8 ? row[bit >> 3] : (row[bit >> 3] >> (8 - depth - (bit & 7))) & maxValue;
				if (mode == srcOr && value == 0)
					continue;
				want[dy * width + dx] = (unsigned char) ((value * 255) / maxValue);
			}
		}
		display->Blit(&map, &src, &dst, mode);
		if (memcmp(want, display->Pixels(), width * height) != 0)
			wrong++;
		delete[] bits;
	}
	EXPECT(wrong == 0);
	delete[] want;
}


int
main(int argc, char** argv)
{
	InitHostStandaloneHeap();
	InitGraf();
	THostScreenDriver* display = new THostScreenDriver;
	display->New();
	display->Configure(64, 48, 1, 100);
	InitScreen(display);
	EXPECT(gTheScreen == display && screenWidth == 64 && screenHeight == 48);
	PixelMap* screen = &qdGlobals.fScreenBits;
	EXPECT(screen->baseAddr != nil && screen->rowBytes == 8 && screen->bounds.right == 64 && screen->bounds.bottom == 48);
	EXPECT((screen->pixMapFlags & kPixMapDepth) == 1 && (screen->pixMapFlags & kPixMapStorage) == kPixMapPtr && screen->deviceRes.h == 100);
	GrafPort* port = GetCurrentPort();
	EXPECT(GetPixelMapBits(&port->portBits) == screen->baseAddr && port->portRect.right == 64);
	long depth;
	GetGrafInfo(kGrafInfoDepth, &depth);
	EXPECT(depth == 1);
	Point res;
	GetGrafInfo(kGrafInfoResolution, &res);
	EXPECT(res.h == 100 && res.v == 100);
	PixelMap copy;
	GetGrafInfo(kGrafInfoScreenPixelMap, &copy);
	EXPECT(copy.baseAddr == screen->baseAddr && copy.rowBytes == 8);

	// a rectangle painted: the blitter's bracket puts it on the display at once
	Rect r;
	SetRect(&r, 10, 10, 20, 15);
	PaintRect(&r);
	EXPECT(display->fBlits == 1 && display->fLastBlit.left == 10 && display->fLastBlit.right == 20 && display->fLastBlit.top == 10 && display->fLastBlit.bottom == 15);
	EXPECT(display->Gray(10, 10) == 255 && display->Gray(19, 14) == 255 && display->Gray(20, 10) == 0 && display->Gray(9, 10) == 0);
	EXPECT(EmptyRect(&gScreenDirtyRect));
	// under StartDrawing/StopDrawing the display waits for the end, and gets everything at once
	StartDrawing(nil, nil);
	SetRect(&r, 30, 30, 40, 40);
	PaintRect(&r);
	EXPECT(display->fBlits == 1 && display->Gray(35, 35) == 0 && !EmptyRect(&gScreenDirtyRect));
	SetRect(&r, 0, 0, 5, 5);
	PaintRect(&r);
	StopDrawing(nil, nil);
	EXPECT(display->fBlits == 2 && display->Gray(35, 35) == 255 && display->Gray(2, 2) == 255);
	EXPECT(display->fLastBlit.left == 0 && display->fLastBlit.top == 0 && display->fLastBlit.right == 40 && display->fLastBlit.bottom == 40);
	EXPECT(EmptyRect(&gScreenDirtyRect));
	// a map that is not the screen is not the display's business
	unsigned char bits[8 * 8];
	memset(bits, 0, sizeof(bits));
	PixelMap other;
	other.baseAddr = (Ptr) bits;
	other.rowBytes = 1;
	SetRect(&other.bounds, 0, 0, 8, 8);
	other.pixMapFlags = kPixMapPtr | 1;
	other.deviceRes.h = other.deviceRes.v = kDefaultDPI;
	other.grayTable = nil;
	EXPECT(!QDStartDrawing(&other, nil));
	QDStopDrawing(&other, &r);
	EXPECT(display->fBlits == 2);
	// the image written and read back
	const char* path = argc > 1 ? argv[1] : "test_Screen.pgm";
	EXPECT(display->WritePGM(path));
	FILE* f = fopen(path, "rb");
	EXPECT(f != nil);
	if (f != nil)
	{
		char magic[3] = { 0, 0, 0 };
		long w = 0, h = 0, maxv = 0;
		fscanf(f, "%2s %ld %ld %ld", magic, &w, &h, &maxv);
		EXPECT(strcmp(magic, "P5") == 0 && w == 64 && h == 48 && maxv == 255);
		fgetc(f);
		unsigned char pixels[64 * 48];
		EXPECT(fread(pixels, 1, sizeof(pixels), f) == sizeof(pixels));
		EXPECT(pixels[10 * 64 + 10] == 0 && pixels[10 * 64 + 25] == 255);		// black ink is 0 in a graymap
		fclose(f);
		remove(path);
	}
	// the orientation: landscape swaps the sides, the screen re-made (blank)
	SetOrientation(1);
	EXPECT(screenWidth == 64 && screenHeight == 48 && screen->bounds.right == 48 && screen->bounds.bottom == 64 && screen->rowBytes == 8);
	EXPECT(display->Width() == 48 && display->Height() == 64 && GetPixel(screen, 10, 10) == 0);
	long orientation;
	GetGrafInfo(kGrafInfoOrientation, &orientation);
	EXPECT(orientation == 1);
	SetOrientation(0);
	EXPECT(screen->bounds.right == 64 && screen->bounds.bottom == 48 && screenWidth == 48 && screenHeight == 64);
	TestBlitAgainstReference();
	if (failures == 0)
		printf("test_Screen: all passed\n");
	else
		printf("test_Screen: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
