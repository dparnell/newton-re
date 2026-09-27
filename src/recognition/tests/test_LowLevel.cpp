// The cursive reader's low level (recognition/LowLevel.h): the integer
// roots, the state and its memory, the strokes, the filters and the
// extrema.
//
// The checks build a trace by hand - pen-ups at each end and between
// strokes, as the reader is given it - and look at what each piece makes
// of it: the box and the strokes found, a doubled pen-up taken out, a
// line thinned and resampled to the step asked for, and a zigzag's
// turning points found as alternating maxima and minima.

#include "LowLevel.h"
#include "XrDomains.h"
#include "ParaGraph.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


static PS_point_type	gTrace[4096];
static long				gCount = 0;

static void	TraceStart(void)		{ gCount = 0; gTrace[gCount].x = 0; gTrace[gCount].y = -1; gCount++; }
static void	PenUp(void)				{ gTrace[gCount].x = 0; gTrace[gCount].y = -1; gCount++; }
static void	Pt(long x, long y)	{ gTrace[gCount].x = x; gTrace[gCount].y = y; gCount++; }


// A low_type set up over gTrace as low_level sets it up.
struct LowFixture
{
	low_type	low;
	rc_type		rc;
	short*		block;
	_SDS_CONTROL_TYPE	sds;

	LowFixture()
	{
		memset(&rc, 0, sizeof(rc));
		RCSetH(&rc, 0x96, gCount);
		block = nil;
		long ok = PrepareLowData(&low, gTrace, &rc, &block);
		EXPECT(ok == 1);
		SetXYToInitial(&low);
		FillLowDataTrace(&low, gTrace);
		GetLowDataRect(&low);
	}
	~LowFixture()
	{
		low_dealloc(&block);
		DeallocSpecl(&low.fSpecl);
	}
};


static void
TestRoots(void)
{
	long bad = 0;
	for (long x = 0; x < 0x8000; x++)
	{
		long r = HWRMathISqrt((short) x);
		long d = labs(r * r - x);
		if (labs((r + 1) * (r + 1) - x) < d || (r > 0 && labs((r - 1) * (r - 1) - x) < d))
			bad++;
	}
	EXPECT(bad == 0);
	EXPECT(HWRMathISqrt(-5) == 0);
	EXPECT(HWRMathISqrt(10000) == 100);
	EXPECT(HWRMathILSqrt(1000000) == 1000);
	EXPECT(HWRMathILSqrt(-1) == 0);
	EXPECT(HWRLAbs(-7) == 7 && HWRLAbs(7) == 7 && HWRLAbs(0) == 0);
}


static void
TestStrokes(void)
{
	TraceStart();
	for (long i = 0; i < 5; i++)
		Pt(10 + i, 20 + 2 * i);
	PenUp();
	for (long i = 0; i < 5; i++)
		Pt(40 + i, 5 + i);
	PenUp();
	LowFixture f;
	low_type* low = &f.low;
	EXPECT(low->fII == 13);
	EXPECT(low->fBox.left == 10 && low->fBox.right == 44);
	EXPECT(low->fBox.top == 5 && low->fBox.bottom == 28);
	EXPECT(InitGroupsBorder(low, 1) == 0);
	EXPECT(low->fLenGroups == 2);
	EXPECT(low->fGroups[0].iBeg == 1 && low->fGroups[0].iEnd == 5);
	EXPECT(low->fGroups[1].iBeg == 7 && low->fGroups[1].iEnd == 11);
	EXPECT(low->fGroups[1].box.left == 40 && low->fGroups[1].box.bottom == 9);
	EXPECT(GetGroupNumber(low, 3) == 0);
	EXPECT(GetGroupNumber(low, 9) == 1);
	EXPECT(GetGroupNumber(low, 6) == -2);

	// a trace that does not end with a pen-up is refused
	low->fY[low->fII - 1] = 7;
	EXPECT(InitGroupsBorder(low, 0) == 1);
}


static void
TestErrorprov(void)
{
	TraceStart();
	Pt(1, 1);
	Pt(2, 2);
	PenUp();
	PenUp();
	Pt(3, 3);
	PenUp();
	LowFixture f;
	low_type* low = &f.low;
	Errorprov(low);
	EXPECT(low->fII == 6);
	EXPECT(low->fY[3] == -1 && low->fX[4] == 3 && low->fY[4] == 3);
	short* map = low->fBuffers[3].ptr;
	EXPECT(map[3] == 4 && map[4] == 5 && map[5] == 6);
}


static void
TestFilters(void)
{
	// a level line a pixel a point, thinned to points more than the root
	// of 10 apart
	TraceStart();
	for (long x = 0; x <= 40; x++)
		Pt(x, 10);
	PenUp();
	{
		LowFixture f;
		low_type* low = &f.low;
		Errorprov(low);
		EXPECT(PreFilt(10, low) == 0);
		EXPECT(low->fY[0] == -1 && low->fY[low->fII - 1] == -1);
		long n = low->fII - 2;
		EXPECT(n > 5 && n < 15);
		for (long i = 2; i < low->fII - 2; i++)
			EXPECT(low->fX[i] - low->fX[i - 1] == 4);
		EXPECT(low->fX[low->fII - 2] == 40);
	}

	// two points far apart resampled a step of 10 apart
	TraceStart();
	Pt(0, 0);
	Pt(100, 0);
	PenUp();
	{
		LowFixture f;
		low_type* low = &f.low;
		Errorprov(low);
		EXPECT(Filt(low, 100, 0) == 0);
		EXPECT(low->fX == low->fBuffers[0].ptr);
		EXPECT(low->fII == 13);
		for (long i = 1; i <= 11; i++)
			EXPECT(low->fX[i] == (i - 1) * 10 && low->fY[i] == 0);
		// the map points each new point at the old one it came from: the
		// first half at the start, the rest at the end
		short* map = low->fBuffers[2].ptr;
		EXPECT(map[1] == 1 && map[5] == 1 && map[6] == 2 && map[11] == 2);
	}
}


static void
TestExtrema(void)
{
	// a zigzag down and up three times: y 0..40 over 10 pixels of x each way
	TraceStart();
	long x = 0;
	for (long leg = 0; leg < 4; leg++)
	{
		for (long s = 0; s < 10; s++, x += 2)
			Pt(x, (leg & 1) ? 40 - 4 * s : 4 * s);
	}
	Pt(x, 0);
	PenUp();
	LowFixture f;
	low_type* low = &f.low;
	EXPECT(InitGroupsBorder(low, 0) == 0);
	InitSpecl(low, kLowSpeclSize);
	EXPECT(Extr(low, 8, -2, -2, -2, 0, 2) == 0);
	// the head, the stroke's start, then alternating extrema up and down,
	// then its end
	SPEC_TYPE* specl = low->fSpecl;
	EXPECT(specl[1].mark == 0x10);
	long n = low->fLenSpecl;
	EXPECT(specl[n - 1].mark == 0x20);
	long extrema = 0;
	UByte last = 0;
	for (long i = 2; i < n - 1; i++)
	{
		EXPECT(specl[i].mark == 1 || specl[i].mark == 3);
		EXPECT(specl[i].mark != last);
		last = specl[i].mark;
		extrema++;
		if (specl[i].ipoint0 != -2 && specl[i].mark == 3)
			EXPECT(low->fY[specl[i].ipoint0] >= 36);			// a bottom (y grows downwards)
		if (specl[i].ipoint0 != -2 && specl[i].mark == 1)
			EXPECT(low->fY[specl[i].ipoint0] <= 4);
		EXPECT(specl[i].prev == &specl[i - 1] && specl[i - 1].next == &specl[i]);
	}
	EXPECT(extrema == 5);
}


int
main()
{
	InitHostStandaloneHeap();
	TestRoots();
	TestStrokes();
	TestErrorprov();
	TestFilters();
	TestExtrema();
	if (failures == 0)
		printf("test_LowLevel: all passed\n");
	return failures == 0 ? 0 : 1;
}
