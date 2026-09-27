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


static void
TestBaselinePieces(void)
{
	// the means and medians the finder measures letters with
	short a[] = { 1, 2, 3, 4, 100 };
	EXPECT(calc_average(a, 5) == 22);
	EXPECT(calc_mediana(a, 5) == 3);
	short b[] = { 7, 7, 7 };
	EXPECT(calc_mediana(b, 3) == 7);
	short c[] = { 10, 20, 30, 40 };
	EXPECT(calc_mediana(c, 4) == 25);
	EXPECT(calc_average(a, 0) == 1 && calc_mediana(a, 0) == 1);
	EXPECT(sign(3, 1) == 1 && sign(1, 3) == -1 && sign(2, 2) == 0);

	// the point furthest from a chord, and whether a stretch is straight
	short x[] = { 0, 0, 5, 10, 15, 20 };
	short y[] = { -1, 0, 5, 0, 0, 0 };
	EXPECT(iMostFarFromChord(x, y, 1, 3) == 2);
	EXPECT(straight_stroke(3, 5, x, y, 4) == 1);
	EXPECT(straight_stroke(1, 3, x, y, 4) == 0);
	_RECT dot = { 10, 10, 11, 11 };
	_RECT bar = { 10, 10, 30, 11 };
	EXPECT(pnt(dot, 8) && !pnt(bar, 8));

	// the extremum arrays: sorted by x, and codes struck out
	EXTR e[6];
	memset(e, 0, sizeof(e));
	short xs[] = { 50, 10, 40, 20, 30 };
	for (long i = 0; i < 5; i++)
	{
		e[i].x = xs[i];
		e[i].susp = (i & 1) ? 0x0d : 0;
	}
	sort_extr(e, 5);
	EXPECT(e[0].x == 10 && e[1].x == 20 && e[2].x == 30 && e[3].x == 40 && e[4].x == 50);
	long n = 5;
	delete_line_extr(e, &n, 0x0d);
	EXPECT(n == 3 && e[0].x == 30 && e[1].x == 40 && e[2].x == 50);

	// the least x, the middle of its flat run
	short px[] = { 9, 5, 3, 3, 3, 8, 3 };
	short py[] = { 0, 0, 0, 0, 0, 0, -1 };
	EXPECT(ixMin(0, 5, px, py) == 3);
	EXPECT(ixMax(0, 5, px, py) == 0);
	EXPECT(iMidPointPlato(2, 3, px, py) == 3);
	short none[] = { -1, -1 };
	EXPECT(ixMin(0, 1, px, none) == -1);

	// a line's ends extended with another array's extrema beyond them
	EXTR line[8], more[4];
	memset(line, 0, sizeof(line));
	memset(more, 0, sizeof(more));
	line[0].x = 50;
	line[1].x = 60;
	for (long i = 0; i < 4; i++)
	{
		more[i].x = 30 + 15 * i;		// 30, 45, 60, 75
		more[i].y = 10;
	}
	long len = 2;
	correct_narrow_ends(line, &len, more, 4, 5, 0x10);
	EXPECT(len == 4 && line[0].x == 30 && line[1].x == 45 && line[2].x == 50);
	EXPECT(line[0].y == 15 && line[0].susp == 0x6e);
	correct_narrow_ends(line, &len, more, 4, 5, 0x20);
	EXPECT(len == 5 && line[4].x == 75);

	// a code put back on the line carries to the neighbours that had it
	EXTR r[4];
	memset(r, 0, sizeof(r));
	r[1].susp = 0x65;
	r[2].susp = 0x65;
	ret_to_line(r, 4, 1, 1);
	EXPECT(r[1].susp == -0x65 && r[2].susp == -0x65 && r[3].susp == 0);
}


static void
TestSmoothLines(void)
{
	// a level stroke with its points out of order in x
	TraceStart();
	short xs[] = { 30, 10, 20, 50, 40, 20 };
	for (long i = 0; i < 6; i++)
		Pt(xs[i], 100);
	PenUp();
	LowFixture f;
	low_type* low = &f.low;
	short order[16];
	long n = fill_i_point(order, low);
	EXPECT(n == 5);				// the second point at x 20 left out
	for (long i = 1; i < n; i++)
		EXPECT(low->fX[order[i]] > low->fX[order[i - 1]]);

	// a line through extrema all at one height is that height everywhere
	EXTR e[3];
	memset(e, 0, sizeof(e));
	for (long i = 0; i < 3; i++)
	{
		e[i].x = 10 + 20 * i;
		e[i].y = 100;
	}
	short line[16];
	smooth_d_bord(e, 3, low, 15, line);
	EXPECT(line[0] == 0 && line[7] == 0);
	for (long i = 1; i <= 6; i++)
		EXPECT(line[i] == 100);

	// with no extrema the lower line is the box's bottom, the upper its top
	smooth_d_bord(e, 0, low, 15, line);
	EXPECT(line[3] == low->fBox.bottom && line[0] == 0);
	short upper[16];
	smooth_u_bord(e, 0, low, 15, upper, line);
	EXPECT(upper[3] == low->fBox.top);

	// one top: the lower line moved up by its height
	e[0].i = 2;
	e[0].y = 60;
	short base[16];
	for (long i = 0; i < 8; i++)
		base[i] = 100 + i;
	smooth_u_bord(e, 1, low, 15, upper, base);
	EXPECT(upper[4] == 104 - (102 - 60));
}


// The list's order, as marks.
static bool
ListIs(SPEC_TYPE* head, const char* marks)
{
	SPEC_TYPE* e = head;
	for ( ; *marks != 0; marks++, e = e->next)
	{
		if (e == nil || e->mark != (UByte) *marks)
			return false;
		if (e->next != nil && e->next->prev != e)
			return false;
	}
	return e == nil;
}


static void
TestListOps(void)
{
	SPEC_TYPE s[6];
	memset(s, 0, sizeof(s));
	for (long i = 0; i < 5; i++)
	{
		s[i].mark = 'a' + i;
		s[i].next = (i < 4) ? &s[i + 1] : nil;
		s[i].prev = (i > 0) ? &s[i - 1] : nil;
	}
	EXPECT(ListIs(&s[0], "abcde"));
	SwapThisAndNext(&s[1]);
	EXPECT(ListIs(&s[0], "acbde"));
	Move2ndAfter1st(&s[0], &s[3]);
	EXPECT(ListIs(&s[0], "adcbe"));
	DelFromSPECLList(&s[2]);
	EXPECT(ListIs(&s[0], "adbe"));
	EXPECT(FindMarkRight(&s[0], 'b') == &s[1] && FindMarkRight(&s[0], 'z') == nil);
	EXPECT(FindMarkLeft(&s[4], 'd') == &s[3]);
	RefreshElem(&s[4], 'e', 3, 0);
	EXPECT(IsUpperElem(&s[4]) && !IsLowerElem(&s[4]));
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
	TestBaselinePieces();
	TestSmoothLines();
	TestListOps();
	if (failures == 0)
		printf("test_LowLevel: all passed\n");
	return failures == 0 ? 0 : 1;
}
