// The ligature fragmenter (recognition/Fragment.h): one stroke of
// joined-up writing cut into letters.
//
// The interesting check is the last one - a stroke shaped like `uuu`,
// three arches written without lifting the pen, really is cut into
// three pieces at the joins between them.

#include "Fragment.h"
#include "RosList.h"
#include "RosStrokes.h"
#include "RosEngine.h"
#include "Segment.h"
#include "FixedMath.h"
#include "memory/host/KernelHeap.h"
#include "NewtonMemory.h"
#include "host/RomBugs.h"

#include <stdio.h>
#include <math.h>
#include <string.h>

static Fixed	F(long n)		{ return (Fixed) (int) ((unsigned int) n << 16); }

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


// A stroke from a list of whole-pixel points.
static RosStroke*
Ink(const long (*xy)[2], short count)
{
	static FPoint points[512];
	for (short i = 0; i < count; i++)
	{
		points[i].x = F(xy[i][0]);
		points[i].y = F(xy[i][1]);
	}
	return StrokeCreate(count, points);
}


// `n` arches side by side, each `w` wide and `h` tall, written without
// lifting the pen - which is what joined-up `uuu` looks like to the
// tablet.  The pen goes down the left side of an arch, along the
// bottom and up the right, then straight across to the next.
static RosStroke*
Arches(long n, long w, long h, long steps)
{
	static long xy[512][2];
	long at = 0;
	for (long a = 0; a < n; a++)
	{
		long x0 = a * w;
		for (long s = 0; s <= steps; s++)
		{
			// a half turn of a circle, upside down
			double t = 3.14159265358979 * (double) s / (double) steps;
			xy[at][0] = x0 + (long) ((1.0 - cos(t)) * (double) w / 2.0);
			xy[at][1] = (long) (sin(t) * (double) h);
			at++;
		}
	}
	return Ink((const long (*)[2]) xy, (short) at);
}


int
main()
{
	InitHostStandaloneHeap();
	CharInitialize(0);
	// the writer's hand: twelve-pixel strokes
	static Fixed run[22];
	for (long i = 0; i < 22; i++)
		run[i] = 0;
	run[0] = F(12);
	run[20] = F(9);
	run[21] = F(9);

	// ---- the steps of a stroke ----
	{
		static const long kLine[4][2] = { {0,0}, {3,4}, {6,8}, {6,8} };
		RosStroke* line = Ink(kLine, 4);
		StrokeDelta* deltas = CalcDeltas(line);
		EXPECT(deltas != nil);
		EXPECT(deltas[0].fDX == F(3) && deltas[0].fDY == F(4));
		// three, four, five
		EXPECT(deltas[0].fLength == F(5));
		EXPECT(deltas[1].fLength == F(5));
		EXPECT(deltas[2].fLength == 0);
		// and how far along it a given length falls
		EXPECT(FindFragmentLength(deltas, 0, 3) == F(10));
		EXPECT(FindFragmentLength(deltas, 1, 1) == 0);
		EXPECT(FindBreakPoint(deltas, 0, 3, F(5)) == 1);
		EXPECT(FindBreakPoint(deltas, 0, 3, F(9)) == 2);
		// past the end answers the end
		EXPECT(FindBreakPoint(deltas, 0, 3, F(100)) == 3);
		DisposPtr((Ptr) deltas);
		StrokeDestroy(line);

		// a stroke of one point has no steps
		static const long kDot[1][2] = { {0,0} };
		RosStroke* dot = Ink(kDot, 1);
		EXPECT(CalcDeltas(dot) == nil);
		StrokeDestroy(dot);
	}

	// ---- the shape of a stroke, and of a step ----
	{
		// a step that goes rightwards, and more than it goes down
		EXPECT(IsThisRunGood(F(5), F(1)));
		EXPECT(IsThisRunGood(F(5), F(5)));
		EXPECT(IsThisRunGood(F(1), F(5)) == false);	// more down than along
		EXPECT(IsThisRunGood(F(-1), 0) == false);	// backwards

		static const long kWide[2][2] = { {0,0}, {40,2} };
		RosStroke* wide = Ink(kWide, 2);
		EXPECT(StrokeIsHorizontal(wide));
		StrokeDestroy(wide);

		static const long kTall[2][2] = { {0,0}, {2,40} };
		RosStroke* tall = Ink(kTall, 2);
		EXPECT(StrokeIsHorizontal(tall) == false);
		StrokeDestroy(tall);

		// ... and something too small to say anything about
		static const long kTiny[2][2] = { {0,0}, {2,0} };
		RosStroke* tiny = Ink(kTiny, 2);
		EXPECT(SegmentMinStrokeSize() > F(2));
		EXPECT(StrokeIsHorizontal(tiny) == false);
		StrokeDestroy(tiny);
	}

	// ---- how often the ink crosses each column ----
	{
		// a plain rightward line crosses every column once
		static const long kLine[5][2] = { {0,0}, {4,0}, {8,0}, {12,0}, {16,0} };
		RosStroke* line = Ink(kLine, 5);
		XProjection* proj = CalcXProjection(line);
		EXPECT(proj != nil);
		EXPECT(proj->fLeft == 0);
		// eight columns to the pixel, and one over
		EXPECT(gXProjectionParams[kXProjColumnsPerPixel] == 8);
		EXPECT(proj->fColumns == 16 * 8 + 1);
		long most = 0;
		for (long i = 0; i < proj->fColumns; i++)
			if (proj->fCounts[i] > most)
				most = proj->fCounts[i];
		EXPECT(most == 1);
		EXPECT(XProjectionDestroy(proj) == nil);
		StrokeDestroy(line);

		// ... but a line drawn out and back crosses each one twice
		static const long kThere[5][2] = { {0,0}, {8,0}, {16,0}, {8,0}, {0,0} };
		RosStroke* there = Ink(kThere, 5);
		proj = CalcXProjection(there);
		EXPECT(proj != nil);
		most = 0;
		for (long i = 1; i < proj->fColumns - 1; i++)
			if (proj->fCounts[i] > most)
				most = proj->fCounts[i];
		EXPECT(most == 2);
		XProjectionDestroy(proj);
		StrokeDestroy(there);

		// a stroke of one point has no projection
		static const long kDot[1][2] = { {0,0} };
		RosStroke* dot = Ink(kDot, 1);
		EXPECT(CalcXProjection(dot) == nil);
		StrokeDestroy(dot);
	}

	// ---- a stretch of a stroke taken out on its own ----
	{
		static const long kLine[5][2] = { {0,0}, {4,0}, {8,0}, {12,0}, {16,0} };
		RosStroke* line = Ink(kLine, 5);

		RosStroke* head = StrokeSubsection(line, 0, 3);
		EXPECT(head->fCount == 3);
		EXPECT(head->fPoints[0].x == 0 && head->fPoints[2].x == F(8));
		// it is the start, so it is not a piece of anything - but the
		// next stroke is the rest of it
		EXPECT(head->fFragment == 0);
		EXPECT(head->fJoinsNext == 1);

		RosStroke* tail = StrokeSubsection(line, 2, 3);
		EXPECT(tail->fCount == 3);
		EXPECT(tail->fFragment == 1);
		EXPECT(tail->fJoinsNext == 0);

		// asking for more than there is answers what there is
		RosStroke* over = StrokeSubsection(line, 3, 9);
		EXPECT(over->fCount == 2);

		StrokeDestroy(head);
		StrokeDestroy(tail);
		StrokeDestroy(over);
		StrokeDestroy(line);
	}

	// ---- a run narrowed to its longest least-crossed stretch ----
	{
		// crossings at each point: stretches of nought at 1-2, 4 and
		// 6-8.  The ROM does not close the one at 4, which loses to 1-2,
		// so it runs on into 6-8 and the run becomes 4-8 (ROM BUG); the
		// fix closes it and the run is the longest stretch, 6-8.
		static short values[10] = { 2, 0, 0, 2, 0, 2, 0, 0, 0, 2 };
		XProjection proj;
		memset(&proj, 0, sizeof(proj));
		proj.fPointValue = values;
		for (int fixed = 0; fixed < 2; fixed++)
		{
			SetRomBugFixed(fixed != 0);
			List* runs = ListCreate();
			StrokeRun* r = (StrokeRun*) NewPtr(sizeof(StrokeRun));
			r->fFirst = 0;
			r->fLast = 9;
			ListAppendEntry(runs, r);
			CheckXProjection(runs, runs->fFirst, nil, &proj);
			EXPECT(runs->fCount == 1);
			if (fixed)
				EXPECT(r->fFirst == 6 && r->fLast == 8);
			else
				EXPECT(r->fFirst == 4 && r->fLast == 8);
			ListDestroy(runs, nil);
			DisposPtr((Ptr) r);
		}
		SetRomBugFixed(true);
	}

	// ---- the good runs of a stroke ----
	{
		// down, along, down: the middle is a join
		static const long kZig[9][2] = {
			{0,0}, {1,8}, {2,16}, {6,16}, {10,16}, {14,16}, {15,8}, {16,0}, {17,0}
		};
		RosStroke* zig = Ink(kZig, 9);
		StrokeDelta* deltas = CalcDeltas(zig);
		List* runs = ListCreate();
		FindGoodRuns(zig, deltas, runs);
		EXPECT(runs->fCount >= 1);
		if (runs->fCount >= 1)
		{
			StrokeRun* first = (StrokeRun*) runs->fFirst->fValue;
			// the level stretch in the middle
			EXPECT(first->fFirst >= 1 && first->fFirst <= 3);
			EXPECT(first->fLast > first->fFirst);
		}
		ListDestroy(runs, nil);
		DisposPtr((Ptr) deltas);
		StrokeDestroy(zig);
	}

	// ---- joined-up writing cut into letters ----
	{
		// three arches written without lifting the pen
		RosStroke* word = Arches(3, 16, 14, 12);
		EXPECT(word->fCount == 39);

		StrokeBreaks breaks;
		FindStrokeFragments(word, &breaks);
		// Three breaks.  Each `u` is thirteen points and they meet at
		// the thirteenth and the twenty-sixth, and the cut lands a
		// little before each meeting - on the rising right-hand side,
		// which is the ligature.  The third `u`'s exit stroke is a
		// ligature too as far as the fragmenter can tell, so it is
		// cut off as well.
		EXPECT(breaks.fCount == 3);
		if (breaks.fCount == 3)
		{
			EXPECT(breaks.fAt[0] > 5 && breaks.fAt[0] < 13);
			EXPECT(breaks.fAt[1] > 18 && breaks.fAt[1] < 26);
			EXPECT(breaks.fAt[2] > 31 && breaks.fAt[2] < 39);
			EXPECT(breaks.fAt[0] < breaks.fAt[1]);
			EXPECT(breaks.fAt[1] < breaks.fAt[2]);
		}
		if (breaks.fAt != nil)
			DisposPtr((Ptr) breaks.fAt);

		// and the whole thing: four strokes, each carrying on from the
		// one before
		RosStrokeList* pieces = FragmentStroke(word, run);
		EXPECT(pieces != nil);
		EXPECT(pieces->fCount == 4);
		if (pieces != nil && pieces->fCount == 4)
		{
			EXPECT(pieces->fStrokes[0]->fFragment == 0);
			EXPECT(pieces->fStrokes[3]->fJoinsNext == 0);
			for (short i = 1; i < 4; i++)
				EXPECT(pieces->fStrokes[i]->fFragment == 1);
			for (short i = 0; i < 3; i++)
				EXPECT(pieces->fStrokes[i]->fJoinsNext == 1);
			// the pieces share the point they were cut at, so they
			// come to three more than the stroke had
			long total = 0;
			for (short i = 0; i < pieces->fCount; i++)
				total += pieces->fStrokes[i]->fCount;
			EXPECT(total == word->fCount + 3);
		}
		SLDestroy(pieces, 1);
		StrokeDestroy(word);
	}

	// ---- a stroke with nothing to cut ----
	{
		// a single upright: one piece, which is the stroke again
		static const long kStem[5][2] = { {0,0}, {0,6}, {0,12}, {0,18}, {0,24} };
		RosStroke* stem = Ink(kStem, 5);
		RosStrokeList* pieces = FragmentStroke(stem, run);
		EXPECT(pieces != nil && pieces->fCount == 1);
		if (pieces != nil && pieces->fCount == 1)
		{
			EXPECT(pieces->fStrokes[0]->fCount == 5);
			EXPECT(pieces->fStrokes[0]->fFragment == 0);
			EXPECT(pieces->fStrokes[0]->fJoinsNext == 0);
		}
		SLDestroy(pieces, 1);
		StrokeDestroy(stem);

		// ... and a stroke of no points at all answers an empty list
		RosStroke* none = StrokeNew();
		RosStrokeList* empty = FragmentStroke(none, run);
		EXPECT(empty != nil && empty->fCount == 0);
		SLDestroy(empty, 1);
		StrokeDestroy(none);
	}

	// the hand was copied in for the passes to reach
	EXPECT(RUN[0] == F(12) && RUN[20] == F(9));

	ListZap();
	if (failures != 0)
	{
		fprintf(stderr, "test_Fragment: %d failure(s)\n", failures);
		return 1;
	}
	printf("test_Fragment: all passed\n");
	return 0;
}
