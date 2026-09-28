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
#include <math.h>

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
		EXPECT(seg->fFirstStroke == -1 && seg->fField04 == -1 && seg->fRealCount == -1);
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

	// ---- one stroke measured against its neighbours ----
	{
		// Three strokes side by side, well apart: |  |  |
		RosStroke* three[3];
		three[0] = Steps(F(0), F(0), 0, F(2), 11);
		three[1] = Steps(F(20), F(0), 0, F(2), 11);
		three[2] = Steps(F(40), F(0), 0, F(2), 11);
		for (short i = 0; i < 3; i++)
			SegmentStrokeData(three[i], 0, i, 0);

		short breaks[8];
		short breakCount = 0;
		for (short i = 0; i < 3; i++)
			SegmentStroke(i, 3, three, F(10), F(2), 0, breaks, &breakCount);

		// nothing overlaps anything, so nothing is linked
		for (short i = 0; i < 3; i++)
		{
			EXPECT(three[i]->fLink == 0);
			EXPECT(three[i]->fOverlap == 0);
		}
		// ... and a cut may go in front of the second and the third,
		// but never in front of the first
		EXPECT(breakCount == 2);
		EXPECT(breaks[0] == 1 && breaks[1] == 2);

		for (short i = 0; i < 3; i++)
			StrokeDestroy(three[i]);
	}

	// ---- two strokes that are one letter ----
	{
		// The two halves of an x: they fill the same span of the line,
		// so they are linked outright and no cut goes between them.
		RosStroke* pair[2];
		pair[0] = Steps(F(0), F(0), F(2), F(2), 11);		// down to the right
		pair[1] = Steps(F(20), F(0), -F(2), F(2), 11);	// down to the left
		for (short i = 0; i < 2; i++)
			SegmentStrokeData(pair[i], 0, i, 0);

		short breaks[8];
		short breakCount = 0;
		for (short i = 0; i < 2; i++)
			SegmentStroke(i, 2, pair, F(10), F(4), 0, breaks, &breakCount);

		EXPECT(pair[1]->fOverlap == F(1));		// the same span exactly
		EXPECT(pair[1]->fOverlap > RosCI->fLinkOverlap);
		// the second is linked backwards, the first is marked as the
		// start of the run
		EXPECT(pair[1]->fLink == 3);
		EXPECT(pair[0]->fLink == 1);
		// and no cut goes between them
		EXPECT(breakCount == 0);

		StrokeDestroy(pair[0]);
		StrokeDestroy(pair[1]);
	}

	// ---- and two that are one letter but do not look it ----
	{
		// The upright and the bar of a t.  They cross, which
		// `SegmentCrossed` sees - but `SegmentOverlap` measures how
		// much of the *line* two strokes share, and an upright is one
		// pixel wide, so the bar covers it completely while the upright
		// covers a twenty-first of the bar.  The mean is a little over
		// a half, under every one of the three thresholds, and the two
		// are not linked.  A t is two segments at this stage.
		RosStroke* pair[2];
		pair[0] = Steps(F(20), F(0), 0, F(2), 21);		// upright, x = 20
		pair[1] = Steps(F(10), F(20), F(2), 0, 11);		// bar, y = 20
		for (short i = 0; i < 2; i++)
			SegmentStrokeData(pair[i], 0, i, 0);

		short breaks[8];
		short breakCount = 0;
		for (short i = 0; i < 2; i++)
			SegmentStroke(i, 2, pair, F(10), F(4), 0, breaks, &breakCount);

		EXPECT(pair[1]->fOverlap > F(1) / 2);
		EXPECT(pair[1]->fOverlap < RosCI->fCrossOverlap);
		EXPECT(pair[0]->fLink == 0 && pair[1]->fLink == 0);
		// ... but they share more than half the line, so no cut goes
		// between them either: they are left for the layer above
		EXPECT(pair[1]->fOverlap > RosCI->fBreakOverlap);
		EXPECT(breakCount == 0);

		StrokeDestroy(pair[0]);
		StrokeDestroy(pair[1]);
	}

	// ---- how near the last three strokes come ----
	{
		// Four strokes; the fourth sits right on top of the second, so
		// looking only at the stroke before it would say it is far away
		RosStroke* four[4];
		four[0] = Steps(F(0), F(0), 0, F(2), 11);
		four[1] = Steps(F(20), F(0), 0, F(2), 11);
		four[2] = Steps(F(60), F(0), 0, F(2), 11);
		four[3] = Steps(F(21), F(0), 0, F(2), 11);
		for (short i = 0; i < 4; i++)
			SegmentStrokeData(four[i], 0, i, 0);

		SegmentDistance nearest, previous;
		SegmentMultiStrokeMinDistance(3, four, F(10), 4, &nearest, &previous);
		// the stroke immediately before is forty pixels away ...
		EXPECT(previous.fDistance == F(39));
		EXPECT(previous.fStrokeA == four[2] && previous.fStrokeB == four[3]);
		// ... but two back is one pixel away, and that is the answer
		EXPECT(nearest.fDistance == F(1));
		EXPECT(nearest.fStrokeA == four[1] && nearest.fStrokeB == four[3]);

		// the same question asked of the boxes
		Fixed gap = SegmentMultiStrokeMinDistBoundX(3, four, F(10), 4);
		// the fourth stroke starts to the *left* of the third, so the
		// least gap is a negative one
		EXPECT(gap == -F(39));

		// a stroke with nothing before it is further off than anything
		SegmentMultiStrokeMinDistance(0, four, F(10), 4, &nearest, &previous);
		EXPECT(nearest.fDistance == F(11) && previous.fDistance == F(11));
		EXPECT(nearest.fStrokeA == nil);
		EXPECT(SegmentMultiStrokeMinDistBoundX(0, four, F(10), 4) == F(11));

		for (short i = 0; i < 4; i++)
			StrokeDestroy(four[i]);
	}

	// ---- the whole cutting, end to end ----
	{
		// two letters of two strokes each, an x and an x, well apart
		RosStroke* four[4];
		four[0] = Steps(F(0), F(0), F(2), F(2), 11);	// \ of the first x
		four[1] = Steps(F(20), F(0), -F(2), F(2), 11);	// / of it
		four[2] = Steps(F(60), F(0), F(2), F(2), 11);
		four[3] = Steps(F(80), F(0), -F(2), F(2), 11);
		for (short i = 0; i < 4; i++)
			SegmentStrokeData(four[i], 0, i, 0);

		RosSegment* segments[64];
		short made = SegmentChars(4, four, F(20), segments, 0, nil);

		// the first pass ran: every stroke knows how much of the line it
		// shares with the one before it, and the two halves of each x
		// are linked
		EXPECT(four[0]->fOverlap == 0);		// nothing before it
		EXPECT(four[1]->fOverlap == F(1));	// the same span exactly
		EXPECT(four[2]->fOverlap == 0);		// a clear gap
		EXPECT(four[3]->fOverlap == F(1));
		EXPECT(four[0]->fLink == 1 && four[1]->fLink == 3);
		EXPECT(four[2]->fLink == 1 && four[3]->fLink == 3);

		// ... and the second pass made one segment per letter
		EXPECT(made == 2);
		EXPECT(segments[0]->fFirstStroke == 0 && segments[0]->fCount == 2);
		EXPECT(segments[1]->fFirstStroke == 2 && segments[1]->fCount == 2);
		// a grouping of one stroke would have ended in the middle of a
		// linked pair, so it was not made - and `fRealCount` counts the
		// letters rather than the strokes
		EXPECT(segments[0]->fRealCount == 1 && segments[1]->fRealCount == 1);
		// the segment holds the strokes itself, joined and sorted
		EXPECT(segments[0]->fStrokes->fCount == 2);
		// every stroke knows which segment it landed in
		EXPECT(four[0]->fSegment == 0 && four[1]->fSegment == 0);
		EXPECT(four[2]->fSegment == 1 && four[3]->fSegment == 1);

		for (short i = 0; i < made; i++)
			SegmentDestroy(segments[i]);
		SegmentQuiesce();
		for (short i = 0; i < 4; i++)
			StrokeDestroy(four[i]);
	}

	// ---- and what it makes when it cannot tell ----
	{
		// Three upright strokes five pixels apart: too close for a gap,
		// too narrow to overlap, so nothing is linked and nothing is
		// cut.  What comes back is **every grouping the strokes allow**
		// - one stroke, two, three, then the same from the second
		// stroke, then the third - because the segment layer does not
		// decide where the letters are.  It hands the layer above a
		// lattice to score.
		RosStroke* three[3];
		for (short i = 0; i < 3; i++)
		{
			three[i] = Steps(F(i * 5), F(0), 0, F(2), 11);
			SegmentStrokeData(three[i], 0, i, 0);
		}

		RosSegment* segments[64];
		short made = SegmentChars(3, three, F(20), segments, 0, nil);
		for (short i = 0; i < 3; i++)
			EXPECT(three[i]->fLink == 0);

		EXPECT(made == 6);
		static const short kFirst[6] = { 0, 0, 0, 1, 1, 2 };
		static const short kCount[6] = { 1, 2, 3, 1, 2, 1 };
		for (short i = 0; i < made && i < 6; i++)
		{
			EXPECT(segments[i]->fFirstStroke == kFirst[i]);
			EXPECT(segments[i]->fCount == kCount[i]);
			// nothing was linked, so no grouping was skipped
			EXPECT(segments[i]->fRealCount == kCount[i]);
		}

		for (short i = 0; i < made; i++)
			SegmentDestroy(segments[i]);
		SegmentQuiesce();
		for (short i = 0; i < 3; i++)
			StrokeDestroy(three[i]);
	}

	// ---- the overlaps worked out again inside a segment ----
	{
		// two segments, one of two strokes and one of one
		RosSegment* first = SegmentCreate();
		RosSegment* second = SegmentCreate();
		RosStroke* a[2];
		a[0] = Steps(F(0), F(0), 0, F(2), 11);		// x = 0
		a[1] = Steps(F(0), F(0), F(2), 0, 11);		// x = 0..20
		RosStroke* b[1];
		b[0] = Steps(F(10), F(0), F(1), F(2), 11);	// x = 10..20
		for (short i = 0; i < 2; i++)
			SegmentStrokeData(a[i], 0, i, 0);
		SegmentStrokeData(b[0], 0, 2, 0);
		SegmentSetStrokes(first, 2, a);
		SegmentSetStrokes(second, 1, b);
		first->fCount = 2;
		second->fCount = 1;
		SegmentBoundsDotsEtc(first);
		SegmentBoundsDotsEtc(second);

		// the first segment of a word has nothing in front of it
		SegmentSetStrokeOverlaps(first, nil);
		EXPECT(first->fStrokes->fStrokes[0]->fOverlap == 0);
		// ... and its second stroke is measured against its first
		EXPECT(first->fStrokes->fStrokes[1]->fOverlap > 0);

		// the next segment's first stroke is measured against the *last
		// stroke of the segment before it*, not against whatever was
		// next to it before the strokes were sorted and joined
		SegmentSetStrokeOverlaps(second, first);
		Fixed across = SegmentOverlap(&first->fStrokes->fStrokes[1]->fBounds,
								&second->fStrokes->fStrokes[0]->fBounds);
		EXPECT(second->fStrokes->fStrokes[0]->fOverlap == across);
		EXPECT(across > 0);

		SegmentSetStrokeOverlaps(nil, nil);		// no trouble
		SegmentDestroy(first);
		SegmentDestroy(second);
		StrokeDestroy(a[0]);
		StrokeDestroy(a[1]);
		StrokeDestroy(b[0]);
	}

	// ---- the writer's word spacing ----
	{
		// five is the writer of ordinary habits: a factor of exactly
		// one, the middle threshold, and a logarithm of nought
		SegmentSetWordSpacing(5);
		EXPECT(gSegWordSpacing == F(1));
		EXPECT(gSegOnlyThreshold == MidSegOnlyThreshold);
		EXPECT(gSegLogWordSpacing == 0);

		SegmentSetWordSpacing(1);
		EXPECT(gSegWordSpacing == 0x51eb);		// about a third
		EXPECT(gSegOnlyThreshold > MinSegOnlyThreshold);
		EXPECT(gSegOnlyThreshold < MidSegOnlyThreshold);
		EXPECT(gSegLogWordSpacing < 0);			// the log of a fraction

		SegmentSetWordSpacing(9);
		EXPECT(gSegOnlyThreshold == MaxSegOnlyThreshold);
		EXPECT(gSegLogWordSpacing > 0);

		// ROM BUG, kept: the constant above the middle setting is
		// 0x170000 where 0x17000 was surely meant, so the top half of
		// the slider runs away from the bottom half.  The whole curve:
		static const Fixed kCurve[9] = {
			0x000051eb, 0x00007d70, 0x0000a8f5, 0x0000d47a, 0x00010000,
			0x0006c000, 0x000c8000, 0x00124000, 0x00180000
		};
		for (long n = 1; n <= 9; n++)
		{
			SegmentSetWordSpacing(n);
			EXPECT(gSegWordSpacing == kCurve[n - 1]);
		}
		// the step from 5 to 6 is nearly seven times, where every other
		// step is a fifth or a third
		EXPECT(kCurve[5] > FixedMultiply(kCurve[4], F(6)));
		EXPECT(kCurve[3] < kCurve[4] && kCurve[4] < kCurve[5]);
		// and the loosest asks for twenty-four times normal
		EXPECT(kCurve[8] == F(24));

		// the log really is the log, rounded toward zero as the ROM's
		// FIX instruction rounds it
		SegmentSetWordSpacing(7);
		double expected = log((double) gSegWordSpacing / 65536.0) * 65536.0;
		EXPECT(gSegLogWordSpacing == (Fixed) (long) expected);
	}

	// ---- where one word ends and the next begins ----
	{
		CharInitialize(0);
		SegmentSetWordSpacing(5);			// the writer of ordinary habits
		SegmentIntegrated(0);				// ... whose writing is not joined up

		// The hand: a mean stroke size of twelve pixels, the two spans
		// the vertical test reaches by, and the eight running
		// Gaussians the gap test works to - two pixels between the
		// boxes of two letters of one word and fourteen between two
		// words, eight between their middles and twenty, and the same
		// four again in stroke sizes.  Each is kept as a mean and a
		// mean of the square, so the second of each pair is the mean
		// squared plus the variance.
		static Fixed run[22];
		for (long i = 0; i < 22; i++)
			run[i] = 0;
		run[0] = F(12);
		run[20] = F(12);
		run[21] = F(4);
		static const long kGap[8][2] = {
			{ 2, 1 }, { 14, 4 }, { 8, 2 }, { 20, 5 },
		};
		for (long i = 0; i < 4; i++)
		{
			Fixed m = F(kGap[i][0]);
			Fixed s = F(kGap[i][1]);
			run[2 + i * 2] = m;
			run[3 + i * 2] = FixedMultiply(m, m) + FixedMultiply(s, s);
			// ... and the same, in stroke sizes
			Fixed ms = FixedDivide(m, run[0]);
			Fixed ss = FixedDivide(s, run[0]);
			run[10 + i * 2] = ms;
			run[11 + i * 2] = FixedMultiply(ms, ms) + FixedMultiply(ss, ss);
		}

		// a letter twelve tall in a word of the same size, and the
		// next letter written straight after it
		SegWordRef ref;
		ref.fInk.fLeft = F(20);		ref.fInk.fRight = F(30);
		ref.fInk.fTop = F(50);		ref.fInk.fBottom = F(62);
		ref.fInk.fCentroidX = F(25);	ref.fInk.fCentroidY = F(56);
		ref.fInk.fHeight = F(13);	ref.fInk.fSizeMax = F(13);
		ref.fBodyTop = ref.fInk.fTop;
		ref.fBodyBottom = ref.fInk.fBottom;
		ref.fStrokes = 1;

		SegWordInk next;
		next.fLeft = F(32);			next.fRight = F(42);
		next.fTop = F(50);			next.fBottom = F(62);
		next.fCentroidX = F(37);	next.fCentroidY = F(56);
		next.fHeight = F(13);		next.fSizeMax = F(13);

		Fixed strength = F(1);
		EXPECT(SegmentWord(&next, &ref, F(12), F(12), run, &strength)
			== kSegWordSame);
		// ... and the strength the gap test left is how likely a space
		// was, which here is under the threshold rather than nought
		EXPECT(strength < gSegIntegrated);
		// the ratio of this writing to the running mean was left behind
		EXPECT(gSegSizeRatio > 0);

		// the dot of an `i`, written after the letter and well behind
		// it, is still the same word
		SegWordInk dot;
		dot.fLeft = F(23);			dot.fRight = F(25);
		dot.fTop = F(46);			dot.fBottom = F(48);
		dot.fCentroidX = F(24);		dot.fCentroidY = F(47);
		dot.fHeight = F(3);			dot.fSizeMax = F(3);
		EXPECT(SegmentWordBack(&dot, &ref, F(12), F(12), run, &strength) == false);

		// ... but a stroke a long way to the left of where the word
		// starts is the pen going back to begin something else
		SegWordInk back = next;
		back.fLeft = F(-40);		back.fRight = F(-30);
		back.fCentroidX = F(-35);
		EXPECT(SegmentWordBack(&back, &ref, F(12), F(12), run, &strength));
		EXPECT(strength == F(1));
		EXPECT(SegmentWord(&back, &ref, F(12), F(12), run, &strength)
			== kSegWordWentBack);
		EXPECT(SegmentWordBkVt(&back, &ref, F(12), F(12), run, &strength)
			== kSegWordWentBack);

		// a stroke on the next line down - below the word and left of
		// it, which is what the start of a line looks like
		SegWordInk below;
		below.fLeft = F(18);		below.fRight = F(28);
		below.fTop = F(90);			below.fBottom = F(102);
		below.fCentroidX = F(23);	below.fCentroidY = F(96);
		below.fHeight = F(13);		below.fSizeMax = F(13);
		EXPECT(SegmentWordVert(&below, &ref, F(12), F(12), run, &strength));
		EXPECT(strength == F(1));
		EXPECT(SegmentWord(&below, &ref, F(12), F(12), run, &strength)
			== kSegWordWentDown);
		// and the next letter along is not
		EXPECT(SegmentWordVert(&next, &ref, F(12), F(12), run, &strength) == false);
		EXPECT(strength == 0);

		// an ascender is not a new line: the top moves but the bottom
		// and the middle do not, and all three must move together
		SegWordInk tall;
		tall.fLeft = F(32);			tall.fRight = F(40);
		tall.fTop = F(40);			tall.fBottom = F(62);
		tall.fCentroidX = F(36);	tall.fCentroidY = F(52);
		tall.fHeight = F(23);		tall.fSizeMax = F(23);
		EXPECT(SegmentWordVert(&tall, &ref, F(12), F(12), run, &strength) == false);

		// the next letter along, again: the gap in front of it is the
		// gap this writer leaves between letters, so it is not a space
		EXPECT(SegmentWordXGap(&next, &ref, F(12), F(12), run, &strength) == false);

		// ... but a stroke ninety pixels further on is, and the four
		// questions agree about it
		SegWordInk far = next;
		far.fLeft = F(120);			far.fRight = F(130);
		far.fCentroidX = F(125);
		EXPECT(SegmentWordXGap(&far, &ref, F(12), F(12), run, &strength));
		EXPECT(strength == F(1));
		EXPECT(SegmentWord(&far, &ref, F(12), F(12), run, &strength)
			== kSegWordWideGap);

		// a gap the writer leaves between words reads as one, and the
		// answer is somewhere in between rather than nought or one
		SegWordInk spaced = next;
		spaced.fLeft = F(44);		spaced.fRight = F(54);
		spaced.fCentroidX = F(49);
		EXPECT(SegmentWordXGap(&spaced, &ref, F(12), F(12), run, &strength));
		EXPECT(strength > 0 && strength <= F(1));

		// the nominals are what a writer of ordinary habits does, and
		// they are what a hand nothing has been learnt about falls
		// back to
		EXPECT(kSegGapNominal[0][0] < kSegGapNominal[1][0]);	// within < between
		EXPECT(kSegGapNominal[2][0] < kSegGapNominal[3][0]);
	}

	// ---- the rest of the layer's life ----
	{
		SegmentIntegrated(0);
		SegmentIntegrated(1);
		SegmentQuiesce();
		SegmentQuiesce();			// twice is no trouble
		SegmentDestroy(nil);
		SegmentInit(nil);
		// nothing to cut: no segments (the cutting itself is tested above)
		EXPECT(SegmentChars(0, nil, 0, nil, 0, nil) == 0);
	}

	if (failures == 0)
		printf("test_Segment: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
