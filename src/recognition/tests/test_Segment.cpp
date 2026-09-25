// The segment layer (recognition/Segment.h): what a segment is, and
// the small sharp questions the handwriting engine asks about two
// pieces of ink on its way to deciding where one letter ends and the
// next begins.
#include "Segment.h"
#include "RosEngine.h"
#include "FixedMath.h"
#include "NewtErrors.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static Fixed	F(long n)		{ return (Fixed) (int) ((unsigned int) n << 16); }

static FRect
FR(Fixed left, Fixed top, Fixed right, Fixed bottom)
{
	FRect r;
	r.left = left;	r.top = top;	r.right = right;	r.bottom = bottom;
	return r;
}


// A stroke of evenly spaced points from one place to another.
static RosStroke*
Line(Fixed x0, Fixed y0, Fixed x1, Fixed y1, short points)
{
	FPoint p[64];
	for (short i = 0; i < points; i++)
	{
		Fixed t = FixedDivide(F(i), F(points - 1));
		p[i].x = x0 + FixedMultiply(t, x1 - x0);
		p[i].y = y0 + FixedMultiply(t, y1 - y0);
	}
	return StrokeCreate(points, p);
}



// A stroke of points one unit apart, so that every coordinate is exact.
static RosStroke*
Steps(Fixed x0, Fixed y0, Fixed dx, Fixed dy, short points)
{
	FPoint p[64];
	for (short i = 0; i < points; i++)
	{
		p[i].x = x0 + dx * i;
		p[i].y = y0 + dy * i;
	}
	return StrokeCreate(points, p);
}

int
main()
{
	InitHostStandaloneHeap();
	CharInitialize(0);

	// ---- the two numbers the layer measures against ----
	{
		// a stroke smaller than this in both directions is a dot, and a
		// cap height smaller than it is not believed
		EXPECT(SegmentMinStrokeSize() == RosCI->fMinStrokeSize);
		EXPECT(SegmentMinStrokeSize() == 0x00048000);	// four and a half pixels
		// three tenths of a stroke at either end counts as *at* the end
		EXPECT(RosCI->fEndFraction == 0x00004ccc);
		// ... and two strokes touch when they come within three pixels
		EXPECT(RosCI->fLinkDistance == F(3));
	}

	// ---- is it a dot? ----
	{
		FRect dot = FR(F(10), F(10), F(12), F(12));
		EXPECT(SegmentDot(&dot));
		// small in one direction only is not a dot: that is the stem of
		// a letter, and the whole point of asking twice
		FRect stem = FR(F(10), F(10), F(13), F(50));
		EXPECT(!SegmentDot(&stem));
		FRect bar = FR(F(10), F(10), F(50), F(13));
		EXPECT(!SegmentDot(&bar));
		FRect letter = FR(F(10), F(10), F(30), F(40));
		EXPECT(!SegmentDot(&letter));
		// exactly the minimum is not small enough
		FRect edge = FR(0, 0, SegmentMinStrokeSize(), SegmentMinStrokeSize());
		EXPECT(!SegmentDot(&edge));
		EXPECT(SegmentDot2(0, 0, SegmentMinStrokeSize() - 1, SegmentMinStrokeSize() - 1));
	}

	// ---- how wide against how tall ----
	{
		// measured inclusively, so a single point is one by one
		FRect point = FR(F(5), F(5), F(5), F(5));
		EXPECT(SegmentAspect(&point) == F(1));
		FRect wide = FR(F(0), F(0), F(19), F(9));
		EXPECT(SegmentAspect(&wide) == F(2));
		FRect tall = FR(F(0), F(0), F(9), F(19));
		EXPECT(SegmentAspect(&tall) == F(1) / 2);
	}

	// ---- how much of the line two boxes share ----
	{
		// lying on each other: all of both
		EXPECT(SegmentOverlapAr(F(0), F(9), F(0), F(9)) == F(1));
		// not meeting at all, either way round
		EXPECT(SegmentOverlapAr(F(0), F(9), F(20), F(29)) == 0);
		EXPECT(SegmentOverlapAr(F(20), F(29), F(0), F(9)) == 0);
		// a narrow box wholly inside a wide one: all of itself and a
		// fifth of the other, so a little over a half
		Fixed inside = SegmentOverlapAr(F(0), F(49), F(20), F(29));
		EXPECT(inside > F(1) / 2 && inside < FixedDivide(F(2), F(3)));
		EXPECT(SegmentOverlapAr(F(20), F(29), F(0), F(49)) == inside);	// symmetric
		// half of each
		Fixed half = SegmentOverlapAr(F(0), F(19), F(10), F(29));
		EXPECT(half > F(1) / 2 - F(1) / 16 && half < F(1) / 2 + F(1) / 16);
		// the rectangle form asks the same question of two boxes
		FRect a = FR(F(0), F(0), F(19), F(40));
		FRect b = FR(F(10), F(50), F(29), F(90));
		EXPECT(SegmentOverlap(&a, &b) == half);		// the tops never come into it
	}

	// ---- which two points come nearest ----
	{
		// an upright and a bar crossing it in the middle, as in a t
		RosStroke* upright = Line(F(20), F(0), F(20), F(40), 21);
		RosStroke* bar = Line(F(10), F(20), F(30), F(20), 11);
		SegmentDistance d;
		SegmentStrokeMinDistance(upright, bar, &d);
		EXPECT(d.fStrokeA == upright && d.fStrokeB == bar);
		EXPECT(d.fDistance == 0);			// they share a point exactly
		EXPECT(d.fDX == 0 && d.fDY == 0);
		EXPECT(d.fIndexA == 10 && d.fIndexB == 5);	// the middle of both
		EXPECT(!d.fEitherIsDot);
		// ... which is what "crossed" means
		EXPECT(SegmentCrossed(&d));
		// and it is *not* a tail link, correctly this time
		EXPECT(SegmentNonTailLinked(&d));

		// two strokes meeting end to end, as in a V
		RosStroke* left = Line(F(0), F(0), F(10), F(20), 11);
		RosStroke* right = Line(F(10), F(20), F(20), F(0), 11);
		SegmentDistance v;
		SegmentStrokeMinDistance(left, right, &v);
		EXPECT(v.fDistance == 0);
		EXPECT(v.fIndexA == 10 && v.fIndexB == 0);	// the end of one, the start of the other
		EXPECT(!SegmentCrossed(&v));				// they meet, they do not cross

		// two strokes that never come near each other
		RosStroke* far1 = Line(F(0), F(0), F(0), F(20), 11);
		RosStroke* far2 = Line(F(100), F(0), F(100), F(20), 11);
		SegmentDistance apart;
		SegmentStrokeMinDistance(far1, far2, &apart);
		EXPECT(apart.fDistance == F(100));
		EXPECT(apart.fDX == F(100) && apart.fDY == 0);
		EXPECT(!SegmentCrossed(&apart));		// too far to be linked at all
		EXPECT(!SegmentNonTailLinked(&apart));

		// a dot is noticed
		RosStroke* aDot = Line(F(50), F(0), F(51), F(1), 2);
		SegmentStrokeData(aDot, 0, 3, F(7));
		EXPECT(aDot->fIsDot);
		EXPECT(aDot->fIndex == 3);
		EXPECT(aDot->fSeparation == F(7));
		SegmentDistance withDot;
		SegmentStrokeMinDistance(upright, aDot, &withDot);
		EXPECT(withDot.fEitherIsDot);

		StrokeDestroy(upright);
		StrokeDestroy(bar);
		StrokeDestroy(left);
		StrokeDestroy(right);
		StrokeDestroy(far1);
		StrokeDestroy(far2);
		StrokeDestroy(aDot);
	}

	// ---- the ROM bug in SegmentNonTailLinked ----
	{
		// The end of a long stroke touching the middle of a shorter
		// one: joined, plainly not end to end, so "non-tail-linked"
		// ought to be true.  The ROM says false, because the clause
		// that should read `idxB >= marginB` reads `idxB >= marginA`
		// and the long stroke's margin is nearly three times the short
		// one's.
		RosStroke* longOne = Steps(F(0), F(0), F(1), 0, 41);
		RosStroke* shortOne = Steps(F(35), -F(8), 0, F(1), 20);
		SegmentDistance d;
		SegmentStrokeMinDistance(longOne, shortOne, &d);
		EXPECT(d.fDistance == 0);
		EXPECT(d.fIndexA == 35 && d.fIndexB == 8);
		// A's point is near its end, B's is in the middle of B.  Three
		// tenths of 41 is 12 and three tenths of 20 is 5, so B's point
		// at 8 is comfortably inside B - and just as comfortably inside
		// what the long stroke's margin would call an end.
		EXPECT(!SegmentCrossed(&d));		// A's point is too near its end
		// ... and this is the bug: it should be true
		EXPECT(!SegmentNonTailLinked(&d));
		StrokeDestroy(longOne);
		StrokeDestroy(shortOne);
	}

	// ---- a segment ----
	{
		RosSegment* seg = SegmentCreate();
		EXPECT(seg != nil);
		EXPECT(seg->fField00 == -1 && seg->fField04 == -1 && seg->fField06 == -1);
		EXPECT(seg->fCount == 0);
		EXPECT(seg->fStrokes == nil);
		EXPECT(seg->fHasDot == 0 && seg->fSmallestStroke == 0);
		EXPECT(seg->fBounds.left == 0 && seg->fBounds.bottom == 0);

		// the two halves of a letter the engine cut for itself, plus the
		// dot over it: an i, written as three pieces
		RosStroke* half1 = Line(F(20), F(10), F(20), F(25), 8);
		RosStroke* half2 = Line(F(20), F(25), F(20), F(40), 8);
		RosStroke* dot = Line(F(20), F(2), F(21), F(3), 2);
		half1->fFragment = 1;		// it is itself a piece of something larger
		half2->fJoinsNext = 1;		// and the stroke after it runs on
		RosStroke* three[3];
		three[0] = half1;	three[1] = half2;	three[2] = dot;

		SegmentSetStrokes(seg, 3, three);
		EXPECT(seg->fStrokes != nil);
		// the two halves adjoin, so they came back as one stroke: three
		// pieces in, two strokes out
		EXPECT(seg->fStrokes->fCount == 2);
		// ... and what the segment holds is its own, not the caller's
		EXPECT(seg->fStrokes->fStrokes[0] != half1);
		EXPECT(seg->fStrokes->fStrokes[0] != half2);
		// the joined stroke has the points of both, less the one they
		// shared
		EXPECT(seg->fStrokes->fStrokes[0]->fCount == 15);
		// the two flags come from the two ends of the run
		EXPECT(seg->fStrokes->fStrokes[0]->fFragment == 1);		// from the first
		EXPECT(seg->fStrokes->fStrokes[0]->fJoinsNext == 1);	// from the last

		seg->fCount = 1;
		SegmentBoundsDotsEtc(seg);
		// the box takes in everything, the dot included
		EXPECT(seg->fBounds.left == F(20) && seg->fBounds.right == F(21));
		EXPECT(seg->fBounds.top == F(2) && seg->fBounds.bottom == F(40));
		// one of the strokes is a dot
		EXPECT(seg->fHasDot);
		// and the smallest stroke, measured by its larger side, is that
		// dot - which is how the layer above notices that something in
		// the piece is too small to be a letter on its own
		EXPECT(seg->fSmallestStroke == F(1));

		SegmentDestroy(seg);
		StrokeDestroy(half1);
		StrokeDestroy(half2);
		StrokeDestroy(dot);
	}

	// ---- the rest of the layer's life ----
	{
		SegmentIntegrated(0);
		SegmentIntegrated(1);
		SegmentQuiesce();
		SegmentQuiesce();			// twice is no trouble
		SegmentDestroy(nil);
		SegmentInit(nil);
		// the cutting itself is NOT YET
		EXPECT(SegmentChars(0, nil, 0, nil, 0, nil) == 0);
	}

	if (failures == 0)
		printf("test_Segment: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
