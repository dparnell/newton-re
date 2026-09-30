// qd/Tile.h: a fax page (1728 x 1146, 216-byte rows) turned a quarter with
// TTile, each way, every pixel of the result compared with the turn worked
// out a pixel at a time.  The new bitmap is 1152 columns wide (the page's
// height rounded up to 64); a right turn puts the page's row y at column
// 1151 - y, a left turn at column y.  ROM QUIRKS pinned: of the 58 rows
// past the last whole band only whole groups of eight are turned (rows
// 1144 and 1145 are lost), and the left turn's leftover rows go wrong - so
// the left turn is checked with those rows white.  A left turn of a page
// whose last rows are black is where the ROM writes past the end of the
// new bitmap; the host drops those bytes (DEVIATION in Tile.cpp), so the
// bytes after it are checked untouched and the whole bands still right.
// Runs over a standalone heap and object heap without ROM objects.
#include "Tile.h"
#include "Pictures.h"
#include "Ports.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "NewtonMemory.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const long kWidth = 1728, kHeight = 1146, kRowBytes = 216;
static const long kTurnedWidth = 1152, kTurnedRowBytes = 144;


static void
InitMap(PixelMap* pm, UChar* bits, long rowBytes, long bottom, long right)
{
	pm->baseAddr = (Ptr) bits;
	pm->rowBytes = (short) rowBytes;
	SetRect(&pm->bounds, 0, 0, (short) right, (short) bottom);
	pm->pixMapFlags = kPixMapPtr | 1;
	pm->deviceRes.v = 98;
	pm->deviceRes.h = 204;
	pm->grayTable = nil;
}


static inline int
Pixel(const UChar* bits, long rowBytes, long row, long col)
{
	return (bits[row * rowBytes + (col >> 3)] >> (7 - (col & 7))) & 1;
}


static void
Turn(Boolean right, long whiteFrom)
{
	UChar* page = (UChar*) NewPtr(kRowBytes * kHeight);
	UChar* turned = (UChar*) NewPtr(kTurnedRowBytes * kWidth);
	memset(turned, 0, kTurnedRowBytes * kWidth);
	unsigned long seed = 12345;
	for (long i = 0; i < kRowBytes * kHeight; i++)
	{
		seed = seed * 1103515245 + 12345;
		// some white stretches, so the blank-tile paths are taken too
		page[i] = ((i / kRowBytes) % 97 < 20) ? 0 : (UChar) (seed >> 16);
	}
	for (long y = whiteFrom; y < kHeight; y++)
		memset(page + y * kRowBytes, 0, kRowBytes);
	PixelMap from, to;
	InitMap(&from, page, kRowBytes, kHeight, kWidth);
	InitMap(&to, turned, kTurnedRowBytes, kWidth, kTurnedWidth);
	EXPECT(Tilable(&from));
	{
		TTile tile(&from, RefVar(NILREF));
		EXPECT(tile.fTilesAcross == 27 && tile.fTileCount == 27);
		if (right)
			tile.RotateTilesR(&from, &to);
		else
			tile.RotateTilesL(&from, &to);
	}
	long wrong = 0;
	for (long row = 0; row < kWidth; row++)
		for (long col = 0; col < kTurnedWidth; col++)
		{
			long y = right ? kTurnedWidth - 1 - col : col;
			long x = right ? row : kWidth - 1 - row;
			// the turned rows: the whole bands and whole groups of eight after them
			int expected = (y < 1144) ? Pixel(page, kRowBytes, y, x) : 0;
			if (Pixel(turned, kTurnedRowBytes, row, col) != expected)
			{
				if (wrong++ < 5)
					fprintf(stderr, "%s turn: row %ld col %ld is %d, not %d\n", right ? "right" : "left",
						row, col, Pixel(turned, kTurnedRowBytes, row, col), expected);
			}
		}
	EXPECT(wrong == 0);
	printf("test_Tile: a %s turn, %ld pixels wrong\n", right ? "right" : "left", wrong);
	DisposPtr((Ptr) page);
	DisposPtr((Ptr) turned);
}


static void
TurnLeftOverrun()
{
	const long kGuard = 1024;
	UChar* page = (UChar*) NewPtr(kRowBytes * kHeight);
	UChar* turned = (UChar*) NewPtr(kTurnedRowBytes * kWidth + kGuard);
	memset(page, 0xff, kRowBytes * kHeight);
	memset(turned, 0, kTurnedRowBytes * kWidth);
	memset(turned + kTurnedRowBytes * kWidth, 0xa5, kGuard);
	PixelMap from, to;
	InitMap(&from, page, kRowBytes, kHeight, kWidth);
	InitMap(&to, turned, kTurnedRowBytes, kWidth, kTurnedWidth);
	{
		TTile tile(&from, RefVar(NILREF));
		tile.RotateTilesL(&from, &to);
	}
	long touched = 0;
	for (long i = 0; i < kGuard; i++)
		if (turned[kTurnedRowBytes * kWidth + i] != 0xa5)
			touched++;
	EXPECT(touched == 0);
	long wrong = 0;
	for (long row = 0; row < kWidth; row++)
		for (long col = 0; col < 1088; col++)
			if (Pixel(turned, kTurnedRowBytes, row, col) != 1)
				wrong++;
	EXPECT(wrong == 0);
	printf("test_Tile: a black page turned left, %ld bytes past the bitmap written, %ld pixels of the whole bands wrong\n", touched, wrong);
	DisposPtr((Ptr) page);
	DisposPtr((Ptr) turned);
}


int
main()
{
	InitHostStandaloneHeap();
	InitGraf();
	gObjectHeapSize = 0x100000;
	InitObjects();

	Turn(true, kHeight);
	Turn(false, 1088);
	TurnLeftOverrun();

	printf("test_Tile: %s\n", failures == 0 ? "ok" : "FAILED");
	return failures == 0 ? 0 : 1;
}
