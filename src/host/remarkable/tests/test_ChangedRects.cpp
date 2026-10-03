// What changed on the display as a few rectangles (host/remarkable/
// ChangedRects.h): nothing changed gives none; two small changes far apart
// give two small rectangles, not the screen; a whole new screen gives one;
// and over random scenes - a few scattered changes, many, blocks and
// single pixels at the edges, on displays of odd sizes - every changed
// pixel lies in a rectangle, no two rectangles overlap, there are no more
// than asked for, and shown ends up equal to the display.
#include "../ChangedRects.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static unsigned long gSeed = 12345;
static long
Rnd(long n)
{
	gSeed = gSeed * 1103515245u + 12345u;
	return (long) ((gSeed >> 16) & 0x7fff) % n;
}


// the rectangles' properties against the display before (was) and after
static void
Check(const unsigned char* was, const unsigned char* pixels, const unsigned char* shown, long width, long height,
	  const ChangedRect* rects, long count, long max)
{
	EXPECT(count <= max);
	EXPECT(memcmp(shown, pixels, (size_t) (width * height)) == 0);
	for (long i = 0; i < count; i++)
	{
		EXPECT(rects[i].left >= 0 && rects[i].top >= 0 && rects[i].right <= width && rects[i].bottom <= height);
		EXPECT(rects[i].left < rects[i].right && rects[i].top < rects[i].bottom);
		for (long j = i + 1; j < count; j++)
			EXPECT(!ChangedOverlap(rects[i], rects[j]));
	}
	long uncovered = 0;
	for (long y = 0; y < height; y++)
		for (long x = 0; x < width; x++)
		{
			if (was[y * width + x] == pixels[y * width + x])
				continue;
			bool in = false;
			for (long i = 0; i < count && !in; i++)
				in = x >= rects[i].left && x < rects[i].right && y >= rects[i].top && y < rects[i].bottom;
			if (!in)
				uncovered++;
		}
	EXPECT(uncovered == 0);
}


static void
Fill(unsigned char* p, long width, long left, long top, long right, long bottom, unsigned char value)
{
	for (long y = top; y < bottom; y++)
		memset(p + y * width + left, value, (size_t) (right - left));
}


static void
TestCases(void)
{
	const long w = 810, h = 1080;
	unsigned char* pixels = (unsigned char*) calloc(w * h, 1);
	unsigned char* shown = (unsigned char*) calloc(w * h, 1);
	unsigned char* was = (unsigned char*) calloc(w * h, 1);
	ChangedRect rects[8];

	// nothing
	EXPECT(FindChangedRects(pixels, shown, w, h, rects, 8) == 0);

	// the clock at the top and a button at the bottom: two, small
	Fill(pixels, w, 700, 4, 780, 20, 200);
	Fill(pixels, w, 20, 1040, 120, 1070, 255);
	memcpy(was, shown, w * h);
	long n = FindChangedRects(pixels, shown, w, h, rects, 8);
	Check(was, pixels, shown, w, h, rects, n, 8);
	EXPECT(n == 2);
	long area = 0;
	for (long i = 0; i < n; i++)
		area += rects[i].Area();
	EXPECT(area == 80 * 16 + 100 * 30);
	printf("test_ChangedRects: two far apart: %ld rectangles, %ld pixels (one bounding rectangle: %ld)\n", n, area, 760L * 1066);

	// asked for one: the bounding rectangle
	Fill(pixels, w, 700, 4, 780, 20, 100);
	Fill(pixels, w, 20, 1040, 120, 1070, 100);
	memcpy(was, shown, w * h);
	n = FindChangedRects(pixels, shown, w, h, rects, 1);
	Check(was, pixels, shown, w, h, rects, n, 1);
	EXPECT(n == 1 && rects[0].left == 20 && rects[0].top == 4 && rects[0].right == 780 && rects[0].bottom == 1070);

	// a line of text: one rectangle round it, not one a band
	Fill(pixels, w, 30, 300, 600, 340, 77);
	memcpy(was, shown, w * h);
	n = FindChangedRects(pixels, shown, w, h, rects, 8);
	Check(was, pixels, shown, w, h, rects, n, 8);
	EXPECT(n == 1 && rects[0].Area() == 570 * 40);

	// a whole new screen: one
	for (long i = 0; i < w * h; i++)
		pixels[i] = (unsigned char) (i * 7 + 1);
	memcpy(was, shown, w * h);
	n = FindChangedRects(pixels, shown, w, h, rects, 8);
	Check(was, pixels, shown, w, h, rects, n, 8);
	EXPECT(n == 1 && rects[0].Area() == w * h);
	free(pixels);
	free(shown);
	free(was);
}


static void
TestRandom(long scenes)
{
	for (long s = 0; s < scenes; s++)
	{
		long w = 1 + Rnd(700), h = 1 + Rnd(500);
		long max = 1 + Rnd(16);
		unsigned char* pixels = (unsigned char*) malloc(w * h);
		unsigned char* shown = (unsigned char*) malloc(w * h);
		unsigned char* was = (unsigned char*) malloc(w * h);
		for (long i = 0; i < w * h; i++)
			pixels[i] = (unsigned char) Rnd(4);
		memcpy(shown, pixels, w * h);
		long changes = Rnd(4) == 0 ? Rnd(2000) : Rnd(12);
		for (long c = 0; c < changes; c++)
		{
			if (Rnd(3) == 0)
				pixels[Rnd(w * h)] ^= 0x80;			// a pixel
			else
			{
				long l = Rnd(w), t = Rnd(h);
				long r = l + 1 + Rnd(w - l), b = t + 1 + Rnd(h - t < 40 ? h - t : 40);
				Fill(pixels, w, l, t, r, b, (unsigned char) (100 + Rnd(100)));
			}
		}
		if (Rnd(5) == 0)
			pixels[w * h - 1] ^= 1;						// the last pixel
		memcpy(was, shown, w * h);
		ChangedRect rects[16];
		long n = FindChangedRects(pixels, shown, w, h, rects, max);
		Check(was, pixels, shown, w, h, rects, n, max);
		EXPECT((n == 0) == (memcmp(was, pixels, w * h) == 0));
		free(pixels);
		free(shown);
		free(was);
	}
	printf("test_ChangedRects: %ld random scenes\n", scenes);
}


// wider than the blocks are counted for: the bounding rectangle
static void
TestWide(void)
{
	const long w = 5000, h = 20;
	unsigned char* pixels = (unsigned char*) calloc(w * h, 1);
	unsigned char* shown = (unsigned char*) calloc(w * h, 1);
	unsigned char* was = (unsigned char*) calloc(w * h, 1);
	pixels[3 * w + 10] = 1;
	pixels[17 * w + 4990] = 1;
	ChangedRect rects[4];
	long n = FindChangedRects(pixels, shown, w, h, rects, 4);
	Check(was, pixels, shown, w, h, rects, n, 4);
	EXPECT(n == 1 && rects[0].left == 10 && rects[0].right == 4991);
	free(pixels);
	free(shown);
	free(was);
}


int
main(int argc, char** argv)
{
	TestCases();
	TestRandom(argc > 1 ? atol(argv[1]) : 400);
	TestWide();
	if (failures == 0)
		printf("test_ChangedRects: all passed\n");
	else
		printf("test_ChangedRects: %d failures\n", failures);
	return failures != 0;
}
