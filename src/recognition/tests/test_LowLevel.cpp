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
	if (failures == 0)
		printf("test_LowLevel: all passed\n");
	return failures == 0 ? 0 : 1;
}
