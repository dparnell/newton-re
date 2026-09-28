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
#include "CursiveReader.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

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


static void
TestGeometry(void)
{
	// the square of a distance from a line, and from a point
	EXPECT(QDistFromChord(0, 0, 10, 0, 0, 5) == 25);
	EXPECT(QDistFromChord(0, 0, 10, 0, 7, -3) == 9);
	EXPECT(QDistFromChord(4, 4, 4, 4, 7, 8) == 25);
	// two segments crossing, missing, parallel
	EXPECT(is_cross(0, 0, 10, 10, 0, 10, 10, 0) == 1);
	EXPECT(is_cross(0, 0, 4, 4, 0, 10, 10, 0) == 0);
	EXPECT(is_cross(0, 0, 10, 0, 0, 5, 10, 5) == 0);
	short px = 0, py = 0;
	EXPECT(FindCrossPoint(0, 0, 10, 10, 0, 10, 10, 0, &px, &py) == 1);
	EXPECT(px == 5 && py == 6);			// (ROM QUIRK: a negative step is rounded the wrong way)
	EXPECT(FindCrossPoint(0, 0, 2, 2, 0, 10, 10, 0, &px, &py) == 0);	// the lines meet, the segments do not
	EXPECT(px == 5 && py == 6);
	EXPECT(FindCrossPoint(0, 0, 10, 0, 0, 5, 10, 5, &px, &py) == 0);
	EXPECT(px == 0x7fff && py == 0x7fff);
	// cosines in hundredths
	EXPECT(cos_pointvect(0, 0, 10, 0, 0, 0, 20, 0) == 100);
	EXPECT(cos_pointvect(0, 0, 10, 0, 0, 0, 0, 10) == 0);
	EXPECT(cos_pointvect(0, 0, 10, 0, 0, 0, -30, 0) == -100);
	EXPECT(cos_pointvect(0, 0, 0, 0, 0, 0, 10, 0) == 0);
}


static void
TestLineGlitches(void)
{
	// a line of tops level but for two that stand up out of it: they are
	// a two-extremum glitch above the line (0x28); the first two and the
	// last two, with nothing beyond them - taken as a step of twice lim -
	// step down off it (0x32)
	EXTR e[6];
	memset(e, 0, sizeof(e));
	short line[6];
	short ys[] = { 60, 60, 20, 20, 60, 60 };
	for (long i = 0; i < 6; i++)
	{
		e[i].x = 10 * i;
		e[i].y = ys[i];
		e[i].i = i;
		line[i] = 100;
	}
	find_glitches_in_line(e, 6, 100, 1, -1, 0x7fff, line, nil, nil, 2, 0, 0);
	EXPECT(e[0].susp == 0x32 && e[1].susp == 0x32);
	EXPECT(e[2].susp == 0x28 && e[3].susp == 0x28);
	EXPECT(e[4].susp == 0x32 && e[5].susp == 0x32);

	// a single top standing up out of the line is a gap each way
	EXTR g[5];
	memset(g, 0, sizeof(g));
	short gy[] = { 60, 60, 20, 60, 60 };
	for (long i = 0; i < 5; i++)
	{
		g[i].x = 10 * i;
		g[i].y = gy[i];
		g[i].i = i;
	}
	find_gaps_in_line(g, 5, 0, 100, 1, -1, 0x7fff, line, nil, 0, 0);
	EXPECT(g[1].susp == 0 && g[3].susp == 0);
	EXPECT(g[2].susp == 0x14);
}


// A cursive word of n arches - a run of u's - on the line y = base, its
// small letters `height` tall, drawn as one stroke a point every two
// pixels across.
static void
Arches(long n, long base, long height, long x0)
{
	TraceStart();
	long x = x0;
	for (long a = 0; a < n; a++)
	{
		for (long s = 0; s < 10; s++, x += 2)
			Pt(x, base - height + (height * s) / 10);
		for (long s = 0; s < 10; s++, x += 2)
			Pt(x, base - (height * s) / 10);
	}
	Pt(x, base - height);
	PenUp();
}


static void
TestBaseline(void)
{
	// eight arches 40 high on the line y = 200: the finder's borders are
	// the line and 40 above it, and the trace is rescaled to them - the
	// feet of the letters at 0x27e6 and their tops at 0x2796, 80 apart
	Arches(8, 200, 40, 100);
	LowFixture f;
	low_type* low = &f.low;
	RCSetH(low->rc, 0x90, 0x10);			// text, the engine to find its own line
	long n = gCount;
	EXPECT(BaselineAndScale(low) == 0);
	EXPECT(RCGetH(low->rc, 0x94) == 0x10);
	long height = (short) RCGetH(low->rc, 0xea);
	long lower = (short) RCGetH(low->rc, 0xec);
	fprintf(stderr, "baseline: height %ld lower %ld sure %d %d penalty-free\n", height, lower,
			(short) RCGetH(low->rc, 0xee), (short) RCGetH(low->rc, 0xf0));
	EXPECT(height >= 36 && height <= 44);
	EXPECT(lower >= 196 && lower <= 204);
	EXPECT((short) RCGetH(low->rc, 0xee) >= 0x4b && (short) RCGetH(low->rc, 0xf0) >= 0x4b);
	// the feet and the tops of the trace, rescaled
	long feet = 0, heads = 0;
	for (long i = 0; i < n; i++)
	{
		if (low->fY[i] == -1)
			continue;
		if (gTrace[i].y == 200)
		{
			EXPECT(low->fY[i] >= 0x27e6 - 8 && low->fY[i] <= 0x27e6 + 8);
			feet++;
		}
		if (gTrace[i].y == 160)
		{
			EXPECT(low->fY[i] >= 0x2796 - 8 && low->fY[i] <= 0x2796 + 8);
			heads++;
		}
	}
	EXPECT(feet == 8 && heads == 9);

	// the same with an ascender (a top at 100) and a descender (a foot at
	// 260): they are taken out of the lines, which stay where they were
	TraceStart();
	long x = 100;
	for (long a = 0; a < 8; a++)
	{
		long top = (a == 3) ? 100 : 160;
		long foot = (a == 5) ? 260 : 200;
		for (long s = 0; s < 10; s++, x += 2)
			Pt(x, top + ((foot - top) * s) / 10);
		for (long s = 0; s < 10; s++, x += 2)
			Pt(x, foot - ((foot - 160) * s) / 10);
	}
	Pt(x, 160);
	PenUp();
	LowFixture g;
	low = &g.low;
	RCSetH(low->rc, 0x90, 0x10);
	EXPECT(BaselineAndScale(low) == 0);
	height = (short) RCGetH(low->rc, 0xea);
	lower = (short) RCGetH(low->rc, 0xec);
	fprintf(stderr, "baseline with ascender and descender: height %ld lower %ld sure %d %d\n", height, lower,
			(short) RCGetH(low->rc, 0xee), (short) RCGetH(low->rc, 0xf0));
	EXPECT(height >= 36 && height <= 44);
	EXPECT(lower >= 196 && lower <= 204);
	long up = 0, down = 0;
	for (SPEC_TYPE* p = low->fSpecl; p != nil; p = p->next)
	{
		if (p->mark == 1 && p->code == 0x66)
			up++;
		if (p->mark == 3 && p->code == 0x65)
			down++;
	}
	EXPECT(up == 1 && down == 1);
}


// A list of elements in s[0..n) (s[0] the head), linked in array order,
// each given by a mark and the points it covers.
static void
MakeList(SPEC_TYPE* s, long n, const UByte* marks, const short* begs, const short* ends)
{
	memset(s, 0, n * sizeof(SPEC_TYPE));
	for (long i = 0; i < n; i++)
	{
		s[i].mark = marks[i];
		s[i].iBeg = begs[i];
		s[i].iEnd = ends[i];
		s[i].prev = (i > 0) ? &s[i - 1] : nil;
		s[i].next = (i < n - 1) ? &s[i + 1] : nil;
	}
}


static void
TestAnalyzePieces(void)
{
	// the thresholds, from a box a little above and within the borders
	low_type low;
	rc_type rc;
	memset(&low, 0, sizeof(low));
	memset(&rc, 0, sizeof(rc));
	low.rc = &rc;
	RCSetH(&rc, 0x94, 0x10);
	low.fBox.top = 0x2700;
	low.fBox.bottom = 0x2800;
	DefLineThresholds(&low);
	EXPECT(low.fThresh[0] == 0x2723 && low.fThresh[1] == 0x2749 && low.fThresh[2] == 0x2770 && low.fThresh[3] == 0x2783);
	EXPECT(low.fThresh[4] == 0x27a8 && low.fThresh[5] == 0x27ba && low.fThresh[6] == 0x27c3 && low.fThresh[7] == 0x27d5);
	EXPECT(low.fThresh[8] == 0x27f3 && low.fThresh[9] == 0x2801 && low.fThresh[10] == 0x281b && low.fThresh[11] == 0x2836);
	EXPECT(low.fThresh[12] == 0x7fff && low.fThresh[14] == 40 && low.fThresh[15] == 400);
	RCSetH(&rc, 0x94, 0x20);
	DefLineThresholds(&low);
	EXPECT(low.fThresh[14] == 27 && low.fThresh[15] == 200);

	// the list sorted into the trace's order: a stroke's start first
	SPEC_TYPE s[8];
	const UByte m1[] = { 0, 0x10, 3, 1, 0x20 };
	const short b1[] = { 0, 1, 10, 1, 20 };
	const short e1[] = { 0, 1, 11, 6, 20 };
	MakeList(s, 5, m1, b1, e1);
	EXPECT(Sort_specl(&s[0], 5) == 0);
	EXPECT(s[0].next->mark == 0x10 && s[0].next->next->mark == 1 && s[0].next->next->next->mark == 3
		&& s[0].next->next->next->next->mark == 0x20);

	// an empty stroke taken out, and the list checked
	const UByte m2[] = { 0, 0x10, 0x20, 0x10, 1, 0x20 };
	const short b2[] = { 0, 1, 1, 3, 3, 9 };
	MakeList(s, 6, m2, b2, b2);
	EXPECT(Clear_specl(&s[0], 6) == 0);
	EXPECT(s[0].next == &s[3]);
	const UByte m3[] = { 0, 0x20, 0x10, 1, 0x20 };
	MakeList(s, 5, m3, b2, b2);
	EXPECT(Clear_specl(&s[0], 5) == 1);

	// the empty strokes squeezed out of the array
	low.fSpecl = s;
	MakeList(s, 6, m2, b2, b2);
	low.fLenSpecl = 6;
	OperateSpeclArray(&low);
	EXPECT(low.fLenSpecl == 4 && s[1].mark == 0x10 && s[2].mark == 1 && s[3].mark == 0x20 && s[3].next == nil);

	// the slant: two downstrokes 40 deep drifting 4 to the right each
	short xs[] = { 0, 10, 14, 30, 34, 0 };
	short ys[] = { -1, 0x2796, 0x27be, 0x2796, 0x27be, -1 };
	const UByte m4[] = { 0, 0x10, 1, 3, 1, 3, 0x20 };
	const short b4[] = { 0, 1, 1, 2, 3, 4, 4 };
	MakeList(s, 7, m4, b4, b4);
	low.fX = xs;
	low.fY = ys;
	EXPECT(measure_slope(&low) == -10);

	// an o's foot: below both tops and nearer the lower border
	short oy[] = { 0x27a0, 0x27e0, 0x27a0 };
	SPEC_TYPE t1, t2, b;
	memset(&t1, 0, sizeof(t1));
	memset(&t2, 0, sizeof(t2));
	memset(&b, 0, sizeof(b));
	t1.mark = t2.mark = 1;
	t1.iBeg = 0;
	b.iBeg = 1;
	t2.iBeg = 2;
	EXPECT(look_like_circle(&b, &t1, &t2, oy) == 1);
	oy[1] = 0x27b0;
	EXPECT(look_like_circle(&b, &t1, &t2, oy) == 0);
}


// Pict's measurements (LowPict.cpp): the heights a stroke is placed
// against, a piece of trace described, and pieces crossing.
static void
TestPictPieces(void)
{
	short h[11];
	EXPECT(BildHigh(0x2700, 0x2900, h) == 1);
	EXPECT(h[0] == 0x2700 && h[1] == 0x274b && h[2] == 0x2777 && h[3] == 0x2796 && h[7] == 0x27e6);
	EXPECT(h[8] == 0x281e && h[9] == 0x2873 && h[10] == 0x2900);
	BildHigh(0x2000, 0x2000, h);
	EXPECT(h[0] == 0x2000 && h[10] == 0x2836);		// the ends clamped to the normal line
	BildHigh(0x2700, 0x2900, h);
	short ry[] = { 0x27a0, 0x27e6 };
	short bottom, top;
	EXPECT(RelHigh(ry, 1, 0, h, &bottom, &top) == 1);
	EXPECT(top == 6 && bottom == 3);
	ry[0] = 0x2600;
	EXPECT(RelHigh(ry, 0, 1, h, &bottom, &top) == 0);	// above the heights
	EXPECT(top == 9);

	EXPECT(Distance8(0, 0, 3, 4) == 5);
	EXPECT(Distance8(0, 0, 0, 7) == 7);

	// an arch: the chord level, the top 5 above its middle
	short ax[] = { 0, 5, 10 };
	short ay[] = { 0, 5, 0 };
	_SDS_TYPE sds;
	Init_SDS_Element(&sds);
	EXPECT(sds.iBeg == -2 && sds.iEnd == -2);
	sds.iBeg = 0;
	sds.iEnd = 2;
	short px, py;
	EXPECT(iMostFarDoubleSide(ax, ay, &sds, &px, &py, 1) == 0);
	EXPECT(sds.chord == 10 && sds.slope == 0 && sds.iMax == 1 && sds.maxDist == 5 && sds.crook == 50);
	EXPECT(sds.distA == 0 && sds.attr == 0x81 && px == 5 && py == 0);
	EXPECT(sds.xMin == 0 && sds.xMax == 10 && sds.yMin == 0 && sds.yMax == 5);
	EXPECT(sds.length == 14 && sds.lengthRatio == 140);
	EXPECT(CurvMeasure(ax, ay, 0, 2, 1) == -25);
	EXPECT(CurvMeasure(ax, ay, 0, 0, 1) == 1000);
	low_type low;
	memset(&low, 0, sizeof(low));
	low.fX = ax;
	low.fY = ay;
	short maxDist;
	EXPECT(CrookCalc(&low, &maxDist, 0, 2) == -50 && maxDist == 5);
	// one point: nothing much
	sds.iBeg = sds.iEnd = 1;
	iMostFarDoubleSide(ax, ay, &sds, &px, &py, 1);
	EXPECT(sds.slope == -2 && sds.chord == 0 && sds.maxDist == -2 && px == 0);

	// two strokes crossing at (5, 5) - found at (5, 6), FindCrossPoint's rounding
	short cx[] = { 0, 10, 0, 10 };
	short cy[] = { 0, 10, 10, 0 };
	low.fX = cx;
	low.fY = cy;
	POINTS_GROUP a = { 0, 1, { 0, 0, 0, 0 } };
	POINTS_GROUP b = { 2, 3, { 0, 0, 0, 0 } };
	PS_point_type cross;
	EXPECT(Find_Cross(&low, &cross, &a, &b) == 1);
	EXPECT(cross.x == 5 && cross.y == 6 && a.iBeg == 0 && a.iEnd == 1 && b.iBeg == 2 && b.iEnd == 3);
	EXPECT(Box_Cover(&low, &a, &b) == 1);
	EXPECT(IsAnythingShift(&low, &a, &b, 0, 0) == 1);
	short fx[] = { 0, 10, 20, 30 };
	low.fX = fx;
	a.iBeg = 0; a.iEnd = 1;
	b.iBeg = 2; b.iEnd = 3;
	EXPECT(Close_To(&low, &a, &b) == 0 && a.iBeg == -2 && a.iEnd == -2);
	a.iBeg = 0; a.iEnd = 1;
	EXPECT(IsAnythingShift(&low, &a, &b, 1, 0) == 0);		// a's right edge left of b's left
	EXPECT(BoxSmallOK(0, 1, fx, cy) == 1 && BoxSmallOK(0, 3, fx, cy) == 0);

	// a single point is a dot, marked at itself
	SPEC_TYPE dot;
	memset(&dot, 0, sizeof(dot));
	dot.iBeg = dot.iEnd = 7;
	EXPECT(Dot(&low, &dot, &sds) == 8 && dot.mark == 8 && dot.ipoint0 == 7 && dot.ipoint1 == 7);

	// a V described: a head, the two arms either side of its corner, a tail
	short vx[] = { 0, 5, 10, 15, 20 };
	short vy[] = { 0, 10, 20, 10, 0 };
	_SDS_TYPE all[20];
	_SDS_CONTROL_TYPE control;
	memset(&control, 0, sizeof(control));
	control.sizeSDS = 20;
	control.pSDS = all;
	SPEC_TYPE v;
	memset(&v, 0, sizeof(v));
	v.iBeg = 0;
	v.iEnd = 4;
	v.code = 3;
	v.attr = 2;
	low.fX = vx;
	low.fY = vy;
	low.fSDS = &control;
	EXPECT(StrElements(&low, &v, h) == 0);
	EXPECT(control.lenSDS == 4 && v.ipoint1 == 0);
	EXPECT(all[0].attr == 0x10 && all[0].crook == 1 && all[0].length == 44 && all[0].lengthRatio == 3 && all[0].share == 2);
	EXPECT(all[1].iBeg == 0 && all[1].iEnd == 2 && all[1].slope == 200 && all[1].share == 50);
	EXPECT(all[2].iBeg == 2 && all[2].iEnd == 4 && all[2].slope == -200 && all[2].share == 50);
	EXPECT(all[3].attr == 0x20 && all[3].iBeg == 4);
}


// A word taken through BaselineAndScale and AnalyzeLowData as far as Pict
// (the rest of AnalyzeLowData being NOT YET): three arches, a dash and a
// dot above.  Pict marks the dash 7 (ParaGraph's straight stroke: a
// level one - the trained tables allow nothing steep, and an upright
// stroke from the word's top to the line never) and the dot 8, each put
// in the list between a stroke start (0x10) and end (0x20) of its own,
// and describes every stroke (a head, its pieces, a tail).
static void
TestPict(void)
{
	TraceStart();
	long x = 100;
	for (long a = 0; a < 3; a++)
	{
		for (long s = 0; s < 10; s++, x += 2)
			Pt(x, 160 + (40 * s) / 10);
		for (long s = 0; s < 10; s++, x += 2)
			Pt(x, 200 - (40 * s) / 10);
	}
	Pt(x, 160);
	PenUp();
	for (long s = 0; s <= 20; s++)			// the dash, 40 long halfway up
		Pt(230 + 2 * s, 180);
	PenUp();
	Pt(280, 120);							// the dot
	Pt(281, 121);
	PenUp();
	LowFixture f;
	low_type* low = &f.low;
	RCSetH(low->rc, 0x90, 0x10);
	EXPECT(BaselineAndScale(low) == 0);
	_SDS_CONTROL_TYPE control;
	memset(&control, 0, sizeof(control));
	low->fSDS = &control;
	EXPECT(CreateSDS(low, 200));
	// AnalyzeLowData's first steps
	GetLowDataRect(low);
	Errorprov(low);
	EXPECT(PreFilt(10, low) == 0);
	EXPECT(InitGroupsBorder(low, 1) == 0);
	DefLineThresholds(low);
	InitSpecl(low, 400);
	Extr(low, 8, 10, 10, 4, 0, 7);
	OperateSpeclArray(low);
	EXPECT(Sort_specl(low->fSpecl, low->fLenSpecl) == 0);
	EXPECT(InitGroupsBorder(low, 1) == 0);
	long nGroups = low->fLenGroups;
	EXPECT(nGroups == 3);
	EXPECT(Pict(low) == 0);
	long sticks = 0, dots = 0, heads = 0, tails = 0;
	for (SPEC_TYPE* p = low->fSpecl; p != nil; p = p->next)
	{
		fprintf(stderr, "pict: mark %#x code %d attr %d other %d points %d..%d (%d, %d)\n",
				p->mark, p->code, p->attr, p->other, p->iBeg, p->iEnd, p->ipoint0, p->ipoint1);
		if (p->mark == 7)
			sticks++;
		if (p->mark == 8)
			dots++;
	}
	for (long k = 0; k < control.lenSDS; k++)
	{
		if (control.pSDS[k].mark == 0 && control.pSDS[k].attr == 0x10)
			heads++;
		if (control.pSDS[k].mark == 0 && control.pSDS[k].attr == 0x20)
			tails++;
	}
	fprintf(stderr, "pict: %ld sticks, %ld dots, %ld descriptions (%ld heads, %ld tails)\n",
			sticks, dots, (long) control.lenSDS, heads, tails);
	EXPECT(heads == nGroups && tails == nGroups);
	EXPECT(sticks == 1 && dots == 1);
	DestroySDS(low);
	EXPECT(control.lenSDS == -2);
}


// angl (LowAngles.cpp): a hairpin - up 20 points and straight back down
// beside itself - is one corner, marked at its apex, opening downwards.
static void
TestAngles(void)
{
	EXPECT(angle_direction(2, 20, 0) == 0x10 && angle_direction(20, 0, 0) == 0x40);
	EXPECT(angle_direction(-10, 5, 0) == 0x80 && angle_direction(2, -20, 0) == 0x20);
	TraceStart();
	for (long s = 0; s < 10; s++)					// a lead-in
		Pt(40 + 5 * s, 200);
	for (long s = 0; s <= 20; s++)
		Pt(100, 200 - 5 * s);
	for (long s = 1; s <= 20; s++)
		Pt(100 + s, 100 + 5 * s);
	for (long s = 1; s <= 10; s++)					// and a lead-out
		Pt(120 + 5 * s, 200);
	PenUp();
	long apex = 1 + 10 + 20;
	LowFixture f;
	low_type* low = &f.low;
	InitSpecl(low, 400);
	EXPECT(angl(low) == 0);
	long corners = 0;
	for (SPEC_TYPE* p = low->fSpecl; p != nil; p = p->next)
		if (p->mark == 0x0b)
		{
			corners++;
			fprintf(stderr, "angle: %d..%d at %d attr %d opening %#x\n", p->iBeg, p->iEnd, p->ipoint0, p->attr, p->other);
			EXPECT(p->ipoint0 >= apex - 1 && p->ipoint0 <= apex + 1);
			EXPECT(p->other == 0x10 && p->attr == 0);
		}
	EXPECT(corners == 1);
}


// AnalyzeLowData's steps (LowLevel.cpp) run by hand as far as `upTo`:
// 1 Circle, 2 angl, 3 FindSideExtr, 4 Cross, 5 Clear_specl, 6 lk_begin, 7 lk_cross.  ==> false when a step
// failed.
static bool
AnalyzeSteps(low_type* low, long upTo)
{
	GetLowDataRect(low);
	Errorprov(low);
	if (PreFilt(10, low) != 0 || InitGroupsBorder(low, 1) != 0)
		return false;
	DefLineThresholds(low);
	InitSpecl(low, 400);
	Extr(low, 8, 10, 10, 4, 0, 7);
	OperateSpeclArray(low);
	if (Sort_specl(low->fSpecl, low->fLenSpecl) != 0 || InitGroupsBorder(low, 1) != 0 || Pict(low) != 0)
		return false;
	Surgeon(low);
	if (Filt(low, 10, 1) != 0 || InitGroupsBorder(low, 1) != 0)
		return false;
	trace_to_xy(low->fXInitial, low->fYInitial, (short) RCGetH(low->rc, 0x96), low->fTrace);
	if (Extr(low, 8, -2, -2, -2, 5, 2) != 0)
		return false;
	low->fSlope = (short) measure_slope(low);
	if (upTo >= 1 && Circle(low) != 0)
		return false;
	if (upTo >= 2 && angl(low) != 0)
		return false;
	if (upTo >= 3 && FindSideExtr(low) == 0)
		return false;
	if (upTo >= 4 && Cross(low) != 0)
		return false;
	if (upTo >= 5 && Clear_specl(low->fSpecl, low->fLenSpecl) != 0)
		return false;
	if (upTo >= 6 && lk_begin(low) != 0)
		return false;
	if (upTo >= 7 && lk_cross(low) != 0)
		return false;
	if (upTo >= 8 && lk_duga(low) != 0)
		return false;
	return true;
}


// A cursive "uou": two arches, an o drawn anticlockwise from its top
// round and back to the top (a little past it, so it closes), and an arch
// to finish, one stroke on the line y = 200 with the small letters 40
// tall.
static void
Uou(void)
{
	TraceStart();
	long x = 100;
	for (long a = 0; a < 2; a++)
	{
		for (long s = 0; s < 10; s++, x += 2)
			Pt(x, 160 + (40 * s) / 10);
		for (long s = 0; s < 10; s++, x += 2)
			Pt(x, 200 - (40 * s) / 10);
	}
	// the o: centre (x + 20, 180), radius 20, from its top anticlockwise
	long cx = x + 20;
	for (long s = 0; s <= 44; s++)
	{
		double t = 2 * 3.14159265358979 * s / 40;
		Pt(cx - (long) lround(20 * sin(t)), 180 - (long) lround(20 * cos(t)));
	}
	x = cx + 12;
	for (long s = 1; s < 10; s++, x += 2)
		Pt(x, 162 + (38 * s) / 10);
	for (long s = 0; s < 10; s++, x += 2)
		Pt(x, 200 - (40 * s) / 10);
	Pt(x, 160);
	PenUp();
}


// Circle (LowCircle.cpp): the o's foot, between its two tops, is found to
// close a loop, marked as a crossing pair - 'c' on the way up and 'd' on
// the way down - near the o's top.  The u's feet, open at the top, are
// not.
static void
TestCircle(void)
{
	EXPECT(SlopeShiftDx(100, 30) == 30 && SlopeShiftDx(-100, 30) == -30);
	EXPECT(SlopeShiftDx(5, 30) == 2 && SlopeShiftDx(-5, 30) == -2);
	Uou();
	LowFixture f;
	low_type* low = &f.low;
	RCSetH(low->rc, 0x90, 0x10);
	EXPECT(BaselineAndScale(low) == 0);
	_SDS_CONTROL_TYPE control;
	memset(&control, 0, sizeof(control));
	low->fSDS = &control;
	EXPECT(CreateSDS(low, 200));
	EXPECT(AnalyzeSteps(low, 1));
	long cs = 0, ds = 0;
	short at[2] = { 0, 0 };
	for (SPEC_TYPE* p = low->fSpecl; p != nil; p = p->next)
	{
		fprintf(stderr, "circle: mark %#x code %d attr %d other %d points %d..%d (%d, %d) y %d\n",
				p->mark, p->code, p->attr, p->other, p->iBeg, p->iEnd, p->ipoint0, p->ipoint1,
				(p->iBeg >= 0) ? low->fY[p->iBeg] : 0);
		if (p->mark == 6 && p->other == 'c')
		{
			cs++;
			at[0] = p->iBeg;
		}
		if (p->mark == 6 && p->other == 'd')
		{
			ds++;
			at[1] = p->iBeg;
		}
	}
	EXPECT(cs == 1 && ds == 1);
	if (cs == 1 && ds == 1)
	{
		// both near the o's top, the 'd' before the 'c' and close to it
		EXPECT(at[1] < at[0]);
		long dx = low->fX[at[0]] - low->fX[at[1]];
		long dy = low->fY[at[0]] - low->fY[at[1]];
		fprintf(stderr, "circle: closes between %d and %d, %ld across and %ld down\n", at[1], at[0], dx, dy);
		EXPECT(labs(dx) < 40 && labs(dy) < 40);
		EXPECT(low->fY[at[0]] < 0x27b0 && low->fY[at[1]] < 0x27b0);
	}
	DestroySDS(low);
}


// The side extrema (LowSide.cpp): a triangle's and a closed path's
// areas, and a side that bows out to the left like a "(" found bending
// left near its middle, where a straight one does not bend.
static void
TestSides(void)
{
	short tx[4] = { 0, 10, 20, 0 };
	short ty[4] = { 0, 10, 0, 0 };
	EXPECT(TriangleSquare(tx, ty, 0, 1, 2) == 100 || TriangleSquare(tx, ty, 0, 1, 2) == -100);
	EXPECT(TriangleSquare(tx, ty, 1, 0, 2) == 0);
	short flag;
	long closed = ClosedSquare(tx, ty, 0, 2, &flag);
	EXPECT(flag == 0 && (closed == 100 || closed == -100));
	EXPECT(ClosedSquare(tx, ty, 2, 0, &flag) == 0x7fff && flag == 1);
	// a hook: from (200, 100) curving out to the left to (160, 115), then
	// straight down to the line at 200 - with `strict` a side's bend must be
	// near one end, a top that starts its stroke hooked
	short sx[64], sy[64], map[64];
	long n = 0;
	for (long s = 0; s <= 10; s++, n++)
	{
		double t = 1.5707963 * s / 10;
		sx[n] = (short) (160 + lround(40 * cos(t)));
		sy[n] = (short) (115 - lround(15 * cos(t)));
		map[n] = (short) n;
	}
	for (long s = 1; s <= 30; s++, n++)
	{
		sx[n] = 160;
		sy[n] = (short) (115 + (85 * s) / 30);
		map[n] = (short) n;
	}
	long k = -1;
	long r = SideExtr(sx, sy, 0, n - 1, 0, sx, sy, map, &k, 1);
	fprintf(stderr, "side: a hook to the left: %ld at %ld\n", r, k);
	EXPECT(r == 1 || r == 3);
	EXPECT(k >= 6 && k <= 16);
	// a symmetric bow is not a hook when strict
	for (long s = 0; s < n; s++)
	{
		double t = (double) s / (n - 1);
		sx[s] = (short) (200 - lround(40 * sin(3.14159265358979 * t)));
		sy[s] = (short) (100 + lround(100 * t));
	}
	EXPECT(SideExtr(sx, sy, 0, n - 1, 0, sx, sy, map, &k, 1) == 0);
	for (long s = 0; s < n; s++)
		sx[s] = 200;
	r = SideExtr(sx, sy, 0, n - 1, 0, sx, sy, map, &k, 1);
	EXPECT(r == 0);
	EXPECT(brk_right(sy, 3, 10) == 11);
	sy[7] = -1;
	EXPECT(brk_right(sy, 3, 10) == 7);
}


// Line from (x0, y0) to (x1, y1), a point every two pixels or so (the
// first point left out when `skipFirst`).
static void
Line(long x0, long y0, long x1, long y1, bool skipFirst)
{
	long dx = x1 - x0, dy = y1 - y0;
	long n = (labs(dx) > labs(dy) ? labs(dx) : labs(dy)) / 2;
	if (n < 1)
		n = 1;
	for (long s = skipFirst ? 1 : 0; s <= n; s++)
		Pt(x0 + (dx * s) / n, y0 + (dy * s) / n);
}


// Cross (LowCross.cpp): a stroke that crosses itself - up a diagonal,
// round and straight down through it - is one crossing pair (6); a t's
// stem drawn up and straight back down is the trace coming back along
// itself (9), and the bar laid across it in a second stroke crosses it
// twice, going up and coming down (two dash crossings, 0xa).
static void
TestCross(void)
{
	TraceStart();
	for (long a = 0; a < 2; a++)			// two arches to give the line
	{
		Line(40 + 40 * a, 160, 60 + 40 * a, 200, a != 0);
		Line(60 + 40 * a, 200, 80 + 40 * a, 160, true);
	}
	Line(120, 160, 120, 200, true);
	Line(120, 200, 180, 140, true);			// the diagonal
	Line(180, 140, 180, 120, true);
	Line(180, 120, 140, 120, true);
	Line(140, 120, 140, 200, true);			// down through it
	Line(140, 200, 150, 196, true);
	PenUp();
	LowFixture f;
	low_type* low = &f.low;
	RCSetH(low->rc, 0x90, 0x10);
	EXPECT(BaselineAndScale(low) == 0);
	_SDS_CONTROL_TYPE control;
	memset(&control, 0, sizeof(control));
	low->fSDS = &control;
	EXPECT(CreateSDS(low, 200));
	EXPECT(AnalyzeSteps(low, 4));
	long crossings = 0;
	for (SPEC_TYPE* p = low->fSpecl; p != nil; p = p->next)
		if ((p->mark == 6 || p->mark == 9 || p->mark == 0xa) && p->other == 0)
		{
			fprintf(stderr, "cross: mark %#x points %d..%d (%d, %d) at (%d, %d)\n", p->mark, p->iBeg, p->iEnd,
					p->ipoint0, p->ipoint1, low->fX[p->iBeg], low->fY[p->iBeg]);
			crossings++;
		}
	EXPECT(crossings == 2);					// one pair: the earlier stretch and the later
	DestroySDS(low);

	// a t: a stem with arches either side, and a bar across it
	TraceStart();
	Line(40, 160, 60, 200, false);
	Line(60, 200, 80, 160, true);
	Line(80, 160, 100, 200, true);
	Line(100, 200, 110, 110, true);			// the stem
	Line(110, 110, 110, 200, true);
	Line(110, 200, 140, 160, true);
	Line(140, 160, 160, 200, true);
	Line(160, 200, 180, 160, true);
	PenUp();
	Line(90, 150, 130, 150, false);			// the bar
	PenUp();
	LowFixture g;
	low = &g.low;
	RCSetH(low->rc, 0x90, 0x10);
	EXPECT(BaselineAndScale(low) == 0);
	memset(&control, 0, sizeof(control));
	low->fSDS = &control;
	EXPECT(CreateSDS(low, 200));
	EXPECT(AnalyzeSteps(low, 4));
	long dashCrossings = 0, backAlong = 0;
	for (SPEC_TYPE* p = low->fSpecl; p != nil; p = p->next)
	{
		if (p->mark == 6 || p->mark == 9 || p->mark == 0xa || p->mark == 7)
			fprintf(stderr, "t: mark %#x other %d points %d..%d at (%d, %d)\n", p->mark, p->other, p->iBeg, p->iEnd,
					low->fX[p->iBeg], low->fY[p->iBeg]);
		if (p->mark == 0xa)
			dashCrossings++;
		if (p->mark == 9)
			backAlong++;
	}
	EXPECT(dashCrossings == 4);
	EXPECT(backAlong == 2);
	DestroySDS(low);
}


// lk_begin (LowBegin.cpp): the cursive "uou" given its codes - one stroke,
// its start and end folded into codes (a top: 3), the u's feet and the
// o's foot bottoms (7 or 8), the tops between tops (2 or 3), every one
// with a height band.  The o's loop is a crossing pair (Cross's: the
// Circle pair inside it is dropped by init_proc_XT_ST_CROSS) and where the
// pen goes back over the o's top it is two 9s.
static void
TestCodes(void)
{
	EXPECT(nobrk_right(nil, 3, 2) == 3);
	short yy[8] = { -1, -1, 5, 6, 7, -1, -1, 3 };
	EXPECT(nobrk_right(yy, 0, 7) == 2 && nobrk_left(yy, 6, 0) == 4);
	short ex[8] = { 5, 3, 3, 3, 4, 2, 2, 6 };
	EXPECT(extremum(1, 0, 4, ex) == 2);		// the run 1..3 of threes
	EXPECT(extremum(3, 0, 7, ex) == 7);
	Uou();
	LowFixture f;
	low_type* low = &f.low;
	RCSetH(low->rc, 0x90, 0x10);
	EXPECT(BaselineAndScale(low) == 0);
	_SDS_CONTROL_TYPE control;
	memset(&control, 0, sizeof(control));
	low->fSDS = &control;
	EXPECT(CreateSDS(low, 200));
	EXPECT(AnalyzeSteps(low, 6));
	long tops = 0, bottoms = 0, crossings = 0, ends = 0;
	for (SPEC_TYPE* p = low->fSpecl->next; p != nil; p = p->next)
	{
		fprintf(stderr, "codes: mark %#x code %#x attr %#x other %#x points %d..%d at y %d\n",
				p->mark, p->code, p->attr, p->other, p->iBeg, p->iEnd, low->fY[p->iBeg]);
		if (p->mark == 0x10 || p->mark == 0x20)
		{
			ends++;
			EXPECT(p->code == 3);
		}
		else if (p->mark == 1)
		{
			tops++;
			EXPECT(p->code == 2 || p->code == 3);
		}
		else if (p->mark == 3)
		{
			bottoms++;
			EXPECT(p->code == 7 || p->code == 8);
		}
		else if (p->mark == 6)
			crossings++;
		if (p->mark != 6 && p->mark != 9 && p->mark != 0xa)
			EXPECT((p->attr & 0xf) >= 1 && (p->attr & 0xf) <= 13);
	}
	fprintf(stderr, "codes: %ld ends, %ld tops, %ld bottoms, %ld crossing elements; step %d (%d)\n",
			ends, tops, bottoms, crossings, low->fStep, low->fStepKind);
	EXPECT(ends == 2 && bottoms == 4 && crossings == 2);
	EXPECT(low->fStep > 0);
	DestroySDS(low);
}


// Adjust_I_U (LowAdjust.cpp): a narrow bottom between two tops, coded
// round (8) by process_curves, becomes sharp (7) when it is a V - straight
// sides, a sharp turn across it - and stays round when it is a U.
static long
IUBottomCode(bool vee)
{
	TraceStart();
	Pt(100, 100); Pt(101, 100); Pt(102, 100);					// 1..3 the first top
	for (long s = 1; s <= 20; s++)								// 4..23 down and up
	{
		if (vee)
			Pt((s <= 10) ? 102 + (5 * s) / 10 : 107 + (5 * (s - 10)) / 10, (s <= 10) ? 100 + 4 * s : 140 - 4 * (s - 10));
		else
		{
			double t = 3.14159265358979 * s / 21;
			Pt(102 + lround(10 - 10 * cos(t)), 100 + lround(40 * sin(t)));
		}
	}
	Pt(122, 100); Pt(123, 100); Pt(124, 100);					// 24..26 the second top
	PenUp();
	LowFixture f;
	low_type* low = &f.low;
	InitSpecl(low, 400);
	EXPECT(Mark(low, 0x10, 3, 1, 0, 1, 1, 1, -2) == 0);
	EXPECT(Mark(low, 1, 3, 1, 0, 1, 3, 2, -2) == 0);
	EXPECT(Mark(low, 3, 8, 9, 0, 12, 15, 13, -2) == 0);
	EXPECT(Mark(low, 1, 3, 1, 0, 24, 26, 25, -2) == 0);
	EXPECT(Mark(low, 0x20, 3, 1, 0, 26, 26, 26, -2) == 0);
	Adjust_I_U(low);
	for (SPEC_TYPE* p = low->fSpecl->next; p != nil; p = p->next)
		if (p->mark == 3)
			return p->code;
	return -1;
}

static void
TestIU(void)
{
	long v = IUBottomCode(true);
	long u = IUBottomCode(false);
	fprintf(stderr, "i/u: a V's bottom %ld, a U's %ld\n", v, u);
	EXPECT(v == 7);
	EXPECT(u == 8);
}


// exchange (LowExchange.cpp) and FillXrFeatures (LowXrFeatures.cpp): the
// "uou" taken through lk_begin and Adjust_I_U comes out as a stream of
// xrs that begins and ends with a break (type 1), has the three tops as
// upper extrema and the four feet as lower ones between them, every point
// inside the original trace and every box round its points, and a
// direction and a height class for each.
static void
TestExchange(void)
{
	EXPECT(GetMovementLink(0x23) == 0xc && GetMovementLink(0x26) == 1 && GetMovementLink(0x27) == 0);
	EXPECT(GetCurveLink(9, 0) == 5 && GetCurveLink(9, 1) == 7 && GetCurveLink(40, 0) == 1 && GetCurveLink(40, 1) == 0xb);
	EXPECT(GetAngle(10, 0) == 0 && GetAngle(0, 10) == 8 && GetAngle(-10, 0) == 0x10 && GetAngle(0, -10) == 0x18);
	EXPECT(GetAngle(10, 10) == 4 && GetAngle(-10, -10) == 0x14);
	SDB_TYPE sdb;
	memset(&sdb, 0, sizeof(sdb));
	sdb.distA = 30; sdb.distB = 5; sdb.crook = 25;			// one side's bend too small beside the other's: an arc
	EXPECT(CalculateStickOrArc(&sdb) == 0xa && sdb.distB == 0);
	sdb.distA = 30; sdb.distB = 30; sdb.crook = 25;			// bending both ways: an S or a Z
	EXPECT(CalculateStickOrArc(&sdb) == 0);
	sdb.iA = 5; sdb.iB = 9;
	EXPECT(CalculateLinkLikeSZ(&sdb, -1) == 0xe && CalculateLinkLikeSZ(&sdb, 1) == 0xe);
	xrd_el_type b;
	memset(&b, 0, sizeof(b));
	b.type = 3;
	EXPECT(X_IsBreak(&b));
	b.type = 5;
	EXPECT(!X_IsBreak(&b));

	Uou();
	LowFixture f;
	low_type* low = &f.low;
	RCSetH(low->rc, 0x90, 0x10);
	EXPECT(BaselineAndScale(low) == 0);
	_SDS_CONTROL_TYPE control;
	memset(&control, 0, sizeof(control));
	low->fSDS = &control;
	EXPECT(CreateSDS(low, 200));
	EXPECT(AnalyzeSteps(low, 6));
	Adjust_I_U(low);
	xrdata_type xr;
	static xrd_el_type elements[kXrMaxElements];
	memset(elements, 0, sizeof(elements));
	xr.fLength = 0;
	xr.fSize = kXrMaxElements;
	xr.fElements = elements;
	EXPECT(exchange(low, &xr) == 0);
	long n = xr.fLength;
	long uppers = 0, lowers = 0;
	for (long i = 0; i < n; i++)
	{
		xrd_el_type* e = &elements[i];
		short hot = XrGetH(e->hotpoint), beg = XrGetH(e->begpoint), end = XrGetH(e->endpoint);
		long m = GetXrMetrics(e);
		fprintf(stderr, "xr %2ld: type %#04x attrib %#04x penalty %2d height %2d shift %2d orient %2d link %2d points %d..%d at %d box %d,%d %d,%d\n",
				i, e->type, e->attrib, e->penalty, e->height, e->shift, e->orient, e->link, beg, end, hot,
				XrGetH(e->box + kXrLeft), XrGetH(e->box + kXrTop), XrGetH(e->box + kXrRight), XrGetH(e->box + kXrBottom));
		EXPECT(beg >= 0 && end < gCount && beg <= end);
		if (!X_IsBreak(e))
		{
			EXPECT(XrGetH(e->box + kXrLeft) <= gTrace[beg].x && gTrace[beg].x <= XrGetH(e->box + kXrRight));
			EXPECT(XrGetH(e->box + kXrTop) <= gTrace[end].y && gTrace[end].y <= XrGetH(e->box + kXrBottom));
		}
		EXPECT(e->orient <= 0x1f && e->height <= 15 && e->shift <= 15);
		if (m & 2)
			uppers++;
		if (m & 1)
			lowers++;
	}
	fprintf(stderr, "exchange: %ld xrs, %ld upper, %ld lower\n", n, uppers, lowers);
	EXPECT(n >= 9);
	EXPECT(elements[0].type == 1 && elements[n - 1].type == 1 && elements[n].type == 0);
	EXPECT(uppers >= 3 && lowers >= 4);
	DestroySDS(low);
}


// RestoreColons and PostFindSideExtr (LowRestore.cpp): a "u" followed by a
// colon written as two dots.  The dots, each coded as a dot by lk_begin,
// are found to be a colon - close across, 20 to 160 apart down, their
// heights apart - and put together between two breaks after the u.  And
// the pieces: an arc overlapping the points between two elements is no
// obstacle, anything else is; a dot or an arc is passed over.
static void
DumpSpecl(low_type* low, const char* what)
{
	for (SPEC_TYPE* p = low->fSpecl->next; p != nil; p = p->next)
		fprintf(stderr, "%s: mark %#x code %#x attr %#x other %#x points %d..%d (%d, %d)\n",
				what, p->mark, p->code, p->attr, p->other, p->iBeg, p->iEnd, p->ipoint0, p->ipoint1);
}

static void
TestRestore(void)
{
	TraceStart();
	for (long u = 0; u < 2; u++)
	{
		long x = 100 + 100 * u;
		for (long a = 0; a < 2; a++)
		{
			for (long s = 0; s < 10; s++, x += 2)
				Pt(x, 160 + (40 * s) / 10);
			for (long s = 0; s < 10; s++, x += 2)
				Pt(x, 200 - (40 * s) / 10);
		}
		Pt(x, 160);
		PenUp();
	}
	// the colon, written last, between the two
	Pt(170, 172); Pt(171, 172); Pt(171, 173);
	PenUp();
	Pt(170, 197); Pt(171, 197); Pt(171, 198);
	PenUp();
	LowFixture f;
	low_type* low = &f.low;
	RCSetH(low->rc, 0x90, 0x10);
	EXPECT(BaselineAndScale(low) == 0);
	_SDS_CONTROL_TYPE control;
	memset(&control, 0, sizeof(control));
	low->fSDS = &control;
	EXPECT(CreateSDS(low, 200));
	EXPECT(AnalyzeSteps(low, 6));
	Adjust_I_U(low);
	DumpSpecl(low, "before colons");
	long dots = 0;
	for (SPEC_TYPE* p = low->fSpecl->next; p != nil; p = p->next)
		if (p->code == 0x10)
			dots++;
	EXPECT(dots == 2);
	EXPECT(RestoreColons(low) == 0);
	DumpSpecl(low, "after colons");
	SPEC_TYPE* first = nil;
	for (SPEC_TYPE* p = low->fSpecl->next; p != nil; p = p->next)
		if (p->code == 0x10)
		{
			first = p;
			break;
		}
	// the pair now between the u's: a break before and after it, and the
	// second u after that
	EXPECT(first != nil && first->next != nil && first->next->code == 0x10);
	if (first != nil && first->next != nil && first->next->next != nil)
	{
		UByte before = first->prev->code, after = first->next->next->code;
		EXPECT(before == 0x12 || before == 1 || before == 0x13 || before == 0x14);
		EXPECT(after == 0x12 || after == 1 || after == 0x13 || after == 0x14);
		EXPECT((first->attr & 0xf) <= (first->next->attr & 0xf));
		EXPECT(first->next->next->next != nil && first->next->next->next->mark == 0x10);
	}
	EXPECT(PostFindSideExtr(low) == 1);
	DestroySDS(low);

	SPEC_TYPE list[5];
	memset(list, 0, sizeof(list));
	for (long i = 0; i < 5; i++)
	{
		list[i].prev = i > 0 ? &list[i - 1] : nil;
		list[i].next = i < 4 ? &list[i + 1] : nil;
	}
	list[0].code = 1; list[1].code = 0xe; list[2].code = 0x10; list[3].code = 3; list[4].code = 0x14;
	list[1].iBeg = 10; list[1].iEnd = 20;
	EXPECT(SkipRealAnglesAndPointsAfter(&list[0]) == &list[3]);
	EXPECT(SkipRealAnglesAndPointsBefore(&list[3]) == &list[0]);
	EXPECT(!IsSmthRelevant_InBetween(&list[0], &list[3], 12, 18));
	EXPECT(IsSmthRelevant_InBetween(&list[0], &list[3], 21, 30));
	EXPECT(IsSmthRelevant_InBetween(&list[0], &list[4], 12, 18));
}


// lk_cross (LowLkCross.cpp): the "uou" o's crossing pair is decided -
// no crossing is left uncoded, and the pair is made one element spanning
// both passes.  And the pieces: a point inside, outside and on the
// border of a square; an arc's link by its bend.
static void
TestLkCross(void)
{
	// (the ray the crossings are counted along runs from x = 1)
	short sx[4] = { 2, 12, 12, 2 };
	short sy[4] = { 0, 0, 10, 10 };
	short where = -1;
	EXPECT(IsPointInsideArea(sx, sy, 4, 7, 5, &where) == 0 && where == 1);
	EXPECT(IsPointInsideArea(sx, sy, 4, 15, 5, &where) == 0 && where == 2);
	EXPECT(IsPointInsideArea(sx, sy, 4, 12, 5, &where) == 0 && where == 0);
	EXPECT(IsPointInsideArea(sx, sy, 2, 5, 5, &where) == 1);

	Uou();
	LowFixture f;
	low_type* low = &f.low;
	RCSetH(low->rc, 0x90, 0x10);
	EXPECT(BaselineAndScale(low) == 0);
	_SDS_CONTROL_TYPE control;
	memset(&control, 0, sizeof(control));
	low->fSDS = &control;
	EXPECT(CreateSDS(low, 200));
	EXPECT(AnalyzeSteps(low, 6));
	long before = 0;
	for (SPEC_TYPE* p = low->fSpecl->next; p != nil; p = p->next)
		if (p->mark == 6)
			before++;
	EXPECT(lk_cross(low) == 0);
	long crossings = 0, uncoded = 0;
	for (SPEC_TYPE* p = low->fSpecl->next; p != nil; p = p->next)
	{
		fprintf(stderr, "lk_cross: mark %#x code %#x attr %#x other %#x points %d..%d (%d, %d)\n",
				p->mark, p->code, p->attr, p->other, p->iBeg, p->iEnd, p->ipoint0, p->ipoint1);
		if (p->mark == 6)
		{
			crossings++;
			if (p->code == 0)
				uncoded++;
		}
	}
	fprintf(stderr, "lk_cross: %ld crossing elements before, %ld after\n", before, crossings);
	EXPECT(uncoded == 0);
	EXPECT(crossings < before);
	DestroySDS(low);
}


// lk_duga's passes (LowLkDuga.cpp): a top just after a stroke's start,
// as high and close across, is folded into the start (it takes the
// start's mark and is marked 0x10); a short closed loop is taken out; a
// low stick between two low bottoms is given the high band.
static void
TestLkDuga(void)
{
	TraceStart();
	Pt(100, 100); Pt(101, 99); Pt(102, 99); Pt(103, 100);		// 1..4 the start and a top beside it
	for (long s = 1; s <= 20; s++)								// 5..24 down and up
		Pt(103 + s, (s <= 10) ? 100 + 4 * s : 140 - 4 * (s - 10));
	Pt(124, 100); Pt(125, 100);									// 25..26
	PenUp();
	LowFixture f;
	low_type* low = &f.low;
	InitSpecl(low, 400);
	EXPECT(Mark(low, 0x10, 3, 1, 0, 1, 1, 1, -2) == 0);
	EXPECT(Mark(low, 1, 3, 1, 0, 2, 3, 2, -2) == 0);
	EXPECT(Mark(low, 3, 7, 9, 0, 14, 16, 15, -2) == 0);
	EXPECT(Mark(low, 1, 5, 1, 0, 20, 22, 21, -2) == 0);			// (code 5: a closed loop, too short to keep)
	EXPECT(Mark(low, 0x20, 3, 1, 0, 26, 26, 26, -2) == 0);
	SPEC_TYPE* start = low->fSpecl->next;
	SPEC_TYPE* top = start->next;
	EXPECT(IsTipOK(top, start, low->fX) == 1);
	EXPECT(DyLimit(low, top, start, nil, top->next, 0x1b) >= 0x1b);
	EXPECT(IsDx_Dy_in_tips_OK(top, start, 0x1b, low->fX, low->fY) == 1);
	EXPECT(arcs_processing(low) == 0);
	EXPECT(low->fSpecl->next == top && top->mark == 0x10 && top->code == 3 && (top->other & 0x10));
	EXPECT(top->iBeg == 2);
	EXPECT(delete_CROSS_elements(low) == 0);
	long loops = 0;
	for (SPEC_TYPE* p = low->fSpecl->next; p != nil; p = p->next)
		if (p->code == 5)
			loops++;
	EXPECT(loops == 0);

	// a low stick (3) between two low bottoms given the high band
	InitSpecl(low, 400);
	EXPECT(Mark(low, 0x10, 3, 1, 0, 1, 1, 1, -2) == 0);
	EXPECT(Mark(low, 3, 8, 0x29, 0, 5, 7, 6, -2) == 0);
	EXPECT(Mark(low, 9, 3, 0x25, 0, 10, 12, 11, -2) == 0);
	EXPECT(Mark(low, 3, 8, 0x29, 0, 14, 16, 15, -2) == 0);
	EXPECT(Mark(low, 0x20, 3, 1, 0, 26, 26, 26, -2) == 0);
	EXPECT(check_IUb_IDf_small(low) == 0);
	SPEC_TYPE* stick = low->fSpecl->next->next->next;
	EXPECT(stick->mark == 9 && (stick->attr & 0x30) == 0x10);
}


// The rest of lk_duga (LowLkDuga.cpp) and the geometry it uses: the box
// overlaps; a level stick starting a stroke made an arc and a top that
// starts one going right made an arc over it straight away; a narrow top
// kept a stick by prevent_arcs; and the "uou" through lk_duga as
// AnalyzeLowData runs it, the o left as its loop and every element still
// coded.
static void
TestLkDugaWhole(void)
{
	_RECT a = { 0, 0, 10, 10 }, b = { 5, 5, 15, 15 }, c = { 20, 0, 30, 10 }, in = { 2, 2, 8, 8 };
	EXPECT(xHardOverlapRect(&a, &in, 1) == 1);					// one inside the other
	EXPECT(xHardOverlapRect(&a, &b, 0) == 0);					// only the edges overlap: neither middle inside
	EXPECT(xHardOverlapRect(&a, &c, 0) == 0 && HardOverlapRect(&a, &c, 0) == 0);
	_RECT d = { 4, 4, 14, 14 };
	EXPECT(xHardOverlapRect(&a, &d, 0) == 1 && yHardOverlapRect(&a, &d, 0) == 1 && HardOverlapRect(&a, &d, 1) == 1);
	short hx[3] = { 0, 10, 0 }, hy[3] = { 0, 0, 10 };
	EXPECT(cos_horizline(0, 1, hx, hy) == 100 && cos_horizline(0, 2, hx, hy) == 0);

	// a start (0x10) that is a stick 7 drawn level to the left, twelve
	// points long: an arc 0xc (high) from its rightmost point; and a start
	// that is a top 3 going right by 20: an arc 0xa at once
	TraceStart();
	for (long s = 0; s < 12; s++)
		Pt(130 - 2 * s, 100);									// 1..12
	for (long s = 1; s <= 10; s++)
		Pt(108, 100 + 4 * s);									// 13..22
	PenUp();
	{
		LowFixture f;
		low_type* low = &f.low;
		InitSpecl(low, 400);
		EXPECT(Mark(low, 0x10, 7, 5, 0, 1, 12, 1, -2) == 0);
		EXPECT(Mark(low, 0x20, 3, 5, 0, 22, 22, 22, -2) == 0);
		EXPECT(conv_sticks_to_arcs(low) == 0);
		SPEC_TYPE* e = low->fSpecl->next;
		EXPECT(e->code == 0xc && (e->attr & 0x30) == 0x10 && e->iBeg == 1 && e->ipoint0 == 1);
		InitSpecl(low, 400);
		EXPECT(Mark(low, 0x10, 3, 5, 0, 12, 1, 12, -2) == 0);	// (x[12] - x[1] = -22: not going right)
		EXPECT(Mark(low, 0x10, 3, 5, 0, 1, 1, 1, -2) == 0);		// one point: dx 0, left alone
		EXPECT(conv_sticks_to_arcs(low) == 0);
		EXPECT(low->fSpecl->next->next->code == 3);
	}
	TraceStart();
	for (long s = 0; s < 12; s++)
		Pt(100 + 2 * s, 100);									// 1..12, going right
	PenUp();
	{
		LowFixture f;
		low_type* low = &f.low;
		InitSpecl(low, 400);
		EXPECT(Mark(low, 0x10, 3, 5, 0, 12, 1, 12, -2) == 0);	// a start at x 122 finishing at x 100: dx 22
		SPEC_TYPE* e = low->fSpecl->next;
		EXPECT(conv_sticks_to_arcs(low) == 0);
		EXPECT(e->code == 0xa && (e->attr & 0x30) == 0x20);
		// prevent_arcs: a narrow top marked 1 kept a stick
		InitSpecl(low, 400);
		EXPECT(Mark(low, 1, 2, 5, 0, 1, 3, 2, -2) == 0);
		EXPECT(Mark(low, 1, 2, 5, 0, 1, 12, 2, -2) == 0);		// (22 across: left alone)
		prevent_arcs(low);
		SPEC_TYPE* t = low->fSpecl->next;
		EXPECT(t->code == 3 && t->other == 1 && t->next->code == 2);
		// delete_UD_before_DDL
		InitSpecl(low, 400);
		EXPECT(Mark(low, 3, 8, 0x15, 0, 1, 3, 2, -2) == 0);
		EXPECT(Mark(low, 1, 0x1c, 0x15, 0, 4, 6, 5, -2) == 0);
		EXPECT(delete_UD_before_DDL(low) == 0);
		EXPECT(low->fSpecl->next->code == 0x1c);
	}

	Uou();
	LowFixture f;
	low_type* low = &f.low;
	RCSetH(low->rc, 0x90, 0x10);
	EXPECT(BaselineAndScale(low) == 0);
	_SDS_CONTROL_TYPE control;
	memset(&control, 0, sizeof(control));
	low->fSDS = &control;
	EXPECT(CreateSDS(low, 200));
	EXPECT(AnalyzeSteps(low, 8));
	long loops = 0, crossings = 0, uncoded = 0, n = 0;
	for (SPEC_TYPE* p = low->fSpecl->next; p != nil; p = p->next, n++)
	{
		fprintf(stderr, "lk_duga: mark %#x code %#x attr %#x other %#x points %d..%d (%d, %d)\n",
				p->mark, p->code, p->attr, p->other, p->iBeg, p->iEnd, p->ipoint0, p->ipoint1);
		if (p->code == 0)
			uncoded++;
		if (p->code == 0x22)
			loops++;
		if (p->code == 5)
			crossings++;
		EXPECT(p->next == nil || p->next->prev == p);
	}
	fprintf(stderr, "lk_duga: %ld elements, %ld loops, %ld crossings\n", n, loops, crossings);
	// the o is left as its loop 0x22 (the crossing 5 lk_cross coded, too
	// short to be kept, taken out by delete_CROSS_elements)
	EXPECT(uncoded == 0 && loops == 1 && crossings == 0 && n >= 7);
	DestroySDS(low);
}


// xt_st_zz's passes (LowXtSt.cpp): the helpers on their own; a t's bar
// written after the word and to the left of it found to be a late stroke
// (FindDelayedStroke); two strokes' gap measured across the bands of the
// line (GetDxBetweenStrokes); side-by-side elements made one
// (CheckSequenceOfElements); a break marked 0x44 beside another dropped
// (del_ZZ_HATCH).
static void
TestXtSt(void)
{
	short yy[6] = { -1, 30, 20, 10, 25, -1 };
	EXPECT(iClosestToY(yy, 1, 4, 22) == 2);
	EXPECT(iClosestToY(yy, 0, 4, 22) == -1);
	short cx[12], cy[12];
	for (long i = 0; i < 12; i++)
	{
		cx[i] = (short) (10 * i);
		cy[i] = (short) (i < 6 ? 0x27a0 : 0x27d0);
	}
	short flag = 5;
	EXPECT(CalcDistBetwXr(cx, cy, 0, 4, 6, 10, &flag) == Distance8(40, 0x27a0, 60, 0x27d0) && flag == 0);
	_RECT box;
	short r, l, b, t;
	EXPECT(GetTraceBoxInsideYZone(cx, cy, 0, 11, 0x27c0, 0x27e0, &box, &r, &l, &b, &t) == 1);
	EXPECT(box.left == 60 && box.right == 110 && l == 6 && r == 11);
	EXPECT(GetTraceBoxInsideYZone(cx, cy, 0, 11, 0x2700, 0x2710, &box, &r, &l, &b, &t) == 0 && r == -1);

	// a word stroke from x 100 to 140, then a short level stroke at x 80..90
	// written after it: the bar of a t crossed later
	TraceStart();
	for (long s = 0; s < 20; s++)
		Pt(100 + 2 * s, (s & 1) ? 0x27d0 : 0x27a0);			// 1..20
	PenUp();													// 21
	for (long s = 0; s < 6; s++)
		Pt(80 + 2 * s, 0x27b0);									// 22..27
	PenUp();
	{
		LowFixture f;
		low_type* low = &f.low;
		low->fStep = 10;
		InitSpecl(low, 400);
		EXPECT(Mark(low, 0x10, 3, 5, 0, 1, 1, 1, -2) == 0);
		EXPECT(Mark(low, 3, 7, 9, 0, 9, 11, 10, -2) == 0);
		EXPECT(Mark(low, 0x20, 7, 9, 0, 20, 20, 20, -2) == 0);
		EXPECT(Mark(low, 0, 0x12, 0, 0, 20, 22, -2, -2) == 0);
		EXPECT(Mark(low, 0x10, 3, 5, 0, 22, 22, 22, -2) == 0);
		EXPECT(Mark(low, 0x20, 7, 5, 0, 27, 27, 27, -2) == 0);
		EXPECT(FindDelayedStroke(low) == 0);
		SPEC_TYPE* late = low->fSpecl->next->next->next->next->next;
		EXPECT(late->code == 0xd && late->iBeg == 22 && late->iEnd == 27 && late->next == nil);
		// the gap across: the word reaches furthest right in the bottom
		// band (138) and next in the top one (136); the bar lies in the
		// top band alone, so it is measured against the word's top band
		// (the slant nought) - negative, the bar lying to the left
		low->fSlope = 0;
		EXPECT(GetDxBetweenStrokes(low, 1, 20, 22, 27) == 80 - 136);
	}

	TraceStart();
	for (long s = 0; s < 10; s++)
		Pt(100 + s, 0x27a0);
	PenUp();
	{
		LowFixture f;
		low_type* low = &f.low;
		InitSpecl(low, 400);
		EXPECT(Mark(low, 1, 0x16, 5, 0, 1, 3, 2, -2) == 0);
		EXPECT(Mark(low, 1, 0x16, 5, 0, 4, 6, 5, -2) == 0);
		EXPECT(Mark(low, 1, 0x17, 5, 0, 6, 7, 6, -2) == 0);
		EXPECT(Mark(low, 1, 0x17, 5, 0, 8, 9, 8, -2) == 0);
		EXPECT(CheckSequenceOfElements(low) == 0);
		SPEC_TYPE* a = low->fSpecl->next;
		EXPECT(a->code == 0x16 && a->iBeg == 4 && a->next->code == 0x17 && a->next->iBeg == 6 && a->next->next == nil);
		InitSpecl(low, 400);
		EXPECT(Mark(low, 0x44, 0x13, 7, 0, 1, 2, -2, -2) == 0);
		EXPECT(Mark(low, 0, 0x12, 7, 0, 2, 3, -2, -2) == 0);
		EXPECT(Mark(low, 1, 3, 5, 0, 4, 6, 5, -2) == 0);
		EXPECT(del_ZZ_HATCH(low->fSpecl) == 0);
		EXPECT(low->fSpecl->next->code == 0x12 && low->fSpecl->next->next->code == 3);
		// the top of an i: a stick up followed by a stroke's end coming down
		InitSpecl(low, 400);
		EXPECT(Mark(low, 0x10, 3, 5, 0, 1, 2, 1, -2) == 0);
		EXPECT(Mark(low, 0x20, 7, 9, 0, 8, 9, 9, -2) == 0);
		EXPECT(IsNearI(low->fSpecl->next) == 1 && IsNearI(low->fSpecl->next->next) == 0);
	}
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
	TestGeometry();
	TestLineGlitches();
	TestBaseline();
	TestAnalyzePieces();
	TestPictPieces();
	TestPict();
	TestAngles();
	TestCircle();
	TestSides();
	TestCross();
	TestCodes();
	TestIU();
	TestExchange();
	TestRestore();
	TestLkCross();
	TestLkDuga();
	TestLkDugaWhole();
	TestXtSt();
	if (failures == 0)
		printf("test_LowLevel: all passed\n");
	return failures == 0 ? 0 : 1;
}
