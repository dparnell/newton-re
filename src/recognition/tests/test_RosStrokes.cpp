// The handwriting engine's own strokes and stroke lists
// (recognition/RosStrokes.h): the bottom of the Rosetta engine, on
// which the word recogniser and the feature extraction are built.
#include "RosStrokes.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// a whole number of pixels as a Fixed
static Fixed	F(long n)		{ return (Fixed) (int) ((unsigned int) n << 16); }

static Boolean
RectIs(const FRect& r, long left, long top, long right, long bottom)
{
	return r.left == F(left) && r.top == F(top)
		&& r.right == F(right) && r.bottom == F(bottom);
}


int
main()
{
	InitHostStandaloneHeap();

	// ---- one stroke ----
	{
		RosStroke* empty = StrokeNew();
		EXPECT(empty != nil);
		EXPECT(empty->fCount == 0 && empty->fPoints == nil);
		EXPECT(empty->fIndex == -1);
		EXPECT(RectIs(empty->fBounds, 0, 0, 0, 0) && empty->fMidX == 0);
		StrokeDestroy(empty);
	}

	// a diagonal with a kink, so the box is not simply its two ends
	static const FPoint kPoints[5] = {
		{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }
	};
	FPoint points[5];
	memcpy(points, kPoints, sizeof(points));
	points[0].x = F(10);	points[0].y = F(20);
	points[1].x = F(4);		points[1].y = F(30);
	points[2].x = F(16);	points[2].y = F(12);
	points[3].x = F(12);	points[3].y = F(40);
	points[4].x = F(8);		points[4].y = F(25);

	RosStroke* stroke = StrokeCreate(5, points);
	EXPECT(stroke != nil);
	EXPECT(stroke->fCount == 5 && stroke->fPoints != nil && stroke->fPoints != points);
	EXPECT(RectIs(stroke->fBounds, 4, 12, 16, 40));
	// the middle of the horizontal range, which is what the strokes of a
	// word are sorted by
	EXPECT(stroke->fMidX == F(10));
	EXPECT(stroke->fPoints[3].x == F(12) && stroke->fPoints[3].y == F(40));

	// the bounds are only worked out again when what is there is not a
	// rectangle
	FRect got;
	stroke->fPoints[0].x = F(-100);		// moved without saying so
	StrokeFindBounds(stroke, &got);
	EXPECT(RectIs(got, 4, 12, 16, 40));	// the old answer stands
	SetFixedRect(&stroke->fBounds, F(9), F(9), F(1), F(1));	// now it is nonsense
	StrokeFindBounds(stroke, &got);
	EXPECT(RectIs(got, -100, 12, 16, 40));
	EXPECT(stroke->fMidX == F(-42));

	// a copy has everything, points and all, but its own array
	RosStroke* copy = StrokeDuplicate(stroke);
	EXPECT(copy != nil);
	EXPECT(copy->fCount == 5 && copy->fPoints != nil && copy->fPoints != stroke->fPoints);
	EXPECT(copy->fPoints[3].x == F(12) && copy->fPoints[3].y == F(40));
	EXPECT(RectIs(copy->fBounds, -100, 12, 16, 40));

	// scaled: each axis by its own amount, and the bounds worked out again
	StrokeScale(copy, F(2), 0x8000);	// twice as wide, half as tall
	EXPECT(copy->fPoints[3].x == F(24) && copy->fPoints[3].y == F(20));
	EXPECT(RectIs(copy->fBounds, -200, 6, 32, 20));
	StrokeDestroy(copy);

	// the points handed over rather than copied
	{
		RosStroke* lent = StrokeNew();
		FPoint own[2];
		own[0].x = F(1);	own[0].y = F(2);
		own[1].x = F(5);	own[1].y = F(8);
		StrokeSet(lent, 2, own, nil);
		EXPECT(lent->fPoints == own);
		EXPECT(RectIs(lent->fBounds, 1, 2, 5, 8) && lent->fMidX == F(3));
		EXPECT(lent->fIndex == -1);
		// a non-nil `bounds` only means "already worked out": it is not
		// read from, so anything at all will do
		SetFixedRect(&lent->fBounds, F(9), F(9), F(9), F(9));
		StrokeSet(lent, 2, own, &lent->fBounds);
		EXPECT(RectIs(lent->fBounds, 9, 9, 9, 9));
		lent->fPoints = nil;			// they are not the stroke's to free
		StrokeDestroy(lent);
	}

	// ---- a list of them ----
	{
		FPoint a[2], b[2], c[2];
		a[0].x = F(30);	a[0].y = F(0);	a[1].x = F(34);	a[1].y = F(10);
		b[0].x = F(0);	b[0].y = F(4);	b[1].x = F(6);	b[1].y = F(14);
		c[0].x = F(15);	c[0].y = F(-2);	c[1].x = F(19);	c[1].y = F(9);
		RosStroke* strokes[3];
		strokes[0] = StrokeCreate(2, a);
		strokes[1] = StrokeCreate(2, b);
		strokes[2] = StrokeCreate(2, c);
		// the order they were written in
		strokes[0]->fIndex = 0;
		strokes[1]->fIndex = 1;
		strokes[2]->fIndex = 2;

		RosStrokeList* list = SLCreate(3, strokes);
		EXPECT(list != nil);
		EXPECT(list->fCount == 3 && list->fStrokes != nil && list->fStrokes != strokes);
		// the strokes themselves are shared, not copied
		EXPECT(list->fStrokes[1] == strokes[1]);
		// and the box is round the lot
		EXPECT(RectIs(list->fBounds, 0, -2, 34, 14));

		// sorted by where they sit on the line rather than when they
		// were written
		StrokeSort(list->fStrokes, list->fCount);
		EXPECT(list->fStrokes[0] == strokes[1]);	// x around 3
		EXPECT(list->fStrokes[1] == strokes[2]);	// x around 17
		EXPECT(list->fStrokes[2] == strokes[0]);	// x around 32

		// ... and back into the order they were written in
		SLSort(list);
		EXPECT(list->fStrokes[0] == strokes[0]);
		EXPECT(list->fStrokes[1] == strokes[1]);
		EXPECT(list->fStrokes[2] == strokes[2]);

		// the bounds are worked out again only when what is there is
		// not a rectangle - and then every stroke's own is too
		SetFixedRect(&list->fBounds, F(5), F(5), F(1), F(1));
		SetFixedRect(&strokes[0]->fBounds, F(5), F(5), F(1), F(1));
		SLFindBounds(list, &got);
		EXPECT(RectIs(got, 0, -2, 34, 14));
		EXPECT(RectIs(strokes[0]->fBounds, 30, 0, 34, 10));

		// the list given back, and the strokes with it
		SLDestroy(list, 1);
	}

	// ---- and with the pieces of a cut stroke kept together ----
	{
		// four strokes, of which the middle two are the two halves of
		// one stroke the engine cut in half.  Their own middles are far
		// apart, so a plain sort would put another stroke between them.
		FPoint p[2];
		RosStroke* strokes[4];
		p[0].x = F(30);	p[0].y = F(0);	p[1].x = F(34);	p[1].y = F(10);
		strokes[3] = StrokeCreate(2, p);			// middle 32
		p[0].x = F(0);	p[0].y = F(0);	p[1].x = F(6);	p[1].y = F(10);
		strokes[0] = StrokeCreate(2, p);			// middle 3
		p[0].x = F(20);	p[0].y = F(0);	p[1].x = F(26);	p[1].y = F(10);
		strokes[1] = StrokeCreate(2, p);			// middle 23
		p[0].x = F(15);	p[0].y = F(0);	p[1].x = F(19);	p[1].y = F(10);
		strokes[2] = StrokeCreate(2, p);			// middle 17
		strokes[0]->fJoinsNext = 1;					// ... and 1 is the rest of 0

		RosStroke* want[4];
		for (long i = 0; i < 4; i++)
			want[i] = strokes[i];

		// the array must begin and end at a group boundary, or nothing
		// is done at all
		strokes[3]->fJoinsNext = 1;
		StrokeSortFrags(strokes, 4);
		EXPECT(strokes[0] == want[0] && strokes[1] == want[1]
			&& strokes[2] == want[2] && strokes[3] == want[3]);
		strokes[3]->fJoinsNext = 0;
		strokes[0]->fFragment = 1;
		StrokeSortFrags(strokes, 4);
		EXPECT(strokes[1] == want[1] && strokes[2] == want[2]);
		strokes[0]->fFragment = 0;

		// the group of two measures 0..26, so its middle is 13 and it
		// comes first; the two halves stay side by side
		StrokeSortFrags(strokes, 4);
		EXPECT(strokes[0] == want[0]);		// the first half
		EXPECT(strokes[1] == want[1]);		// ... and the second, still next to it
		EXPECT(strokes[2] == want[2]);		// middle 17
		EXPECT(strokes[3] == want[3]);		// middle 32

		// where a plain sort would have put the one at 17 between them
		for (long i = 0; i < 4; i++)
			strokes[i] = want[i];
		StrokeSort(strokes, 4);
		EXPECT(strokes[1] == want[2] && strokes[2] == want[1]);

		for (long i = 0; i < 4; i++)
			StrokeDestroy(want[i]);
	}

	// an empty list has an empty box
	{
		RosStrokeList* none = SLCreate(0, nil);
		EXPECT(none != nil && none->fCount == 0 && none->fStrokes == nil);
		SLCalcBounds(none);
		EXPECT(RectIs(none->fBounds, 0, 0, 0, 0));
		SLDestroy(none, 1);
	}

	// ---- measuring and tidying ----
	{
		// the centroid is the average of the points, not the middle of
		// the box: this stroke sits at x=0 three times and x=30 once
		FPoint lop[4];
		lop[0].x = F(0);	lop[0].y = F(0);
		lop[1].x = F(0);	lop[1].y = F(0);
		lop[2].x = F(0);	lop[2].y = F(0);
		lop[3].x = F(40);	lop[3].y = F(0);
		RosStroke* lopsided = StrokeCreate(4, lop);
		FPoint middle;
		StrokeCentroid(lopsided, &middle);
		EXPECT(middle.x == F(10) && middle.y == 0);		// the average
		EXPECT(lopsided->fMidX == F(20));				// the middle of the box
		StrokeDestroy(lopsided);

		// smoothing leaves the ends alone and pulls a spike in.  A
		// weight of -4/4 = -1 takes the whole second difference off, so
		// a single spike is flattened onto the line through its
		// neighbours.
		FPoint spike[3];
		spike[0].x = F(0);	spike[0].y = F(0);
		spike[1].x = F(1);	spike[1].y = F(10);		// the spike
		spike[2].x = F(2);	spike[2].y = F(0);
		RosStroke* rough = StrokeCreate(3, spike);
		RosStroke* smooth = StrokeSmooth(rough, F(-4));
		EXPECT(smooth != nil && smooth->fCount == 3);
		EXPECT(smooth->fPoints[0].x == F(0) && smooth->fPoints[0].y == F(0));
		EXPECT(smooth->fPoints[2].x == F(2) && smooth->fPoints[2].y == F(0));
		// the middle point moved by -(0 - 2*10 + 0) = +20 ... away from
		// the line, because the second difference is negative
		EXPECT(smooth->fPoints[1].y == F(10) + (F(0) - 2 * F(10) + F(0)) * -1);
		// a weight of nought changes nothing at all
		RosStroke* same = StrokeSmooth(rough, 0);
		EXPECT(same->fPoints[1].x == F(1) && same->fPoints[1].y == F(10));
		StrokeDestroy(same);

		// constraining pulls every point back to within half the
		// tolerance of where it was
		RosStroke* held = StrokeConstrain(smooth, rough, F(4));
		EXPECT(held != nil && held->fCount == 3);
		EXPECT(held->fPoints[1].y == F(12));			// 10 + 4/2
		StrokeDestroy(held);
		// a tolerance of nought pins it exactly
		held = StrokeConstrain(smooth, rough, 0);
		EXPECT(held->fPoints[1].y == F(10));
		StrokeDestroy(held);
		StrokeDestroy(smooth);

		// dequantising is the two of them, over and over, and always
		// answers a stroke of its own
		RosStroke* clean = StrokeDeQuantize(rough, F(-4), F(1), 3);
		EXPECT(clean != nil && clean != rough && clean->fCount == 3);
		EXPECT(clean->fPoints[0].y == F(0) && clean->fPoints[2].y == F(0));
		// never further than half the tolerance from where it was
		EXPECT(clean->fPoints[1].y >= F(10) - 0x8000 && clean->fPoints[1].y <= F(10) + 0x8000);
		StrokeDestroy(clean);

		// and the whole of it, as a list of one.  The stroke handed in
		// is never the one handed back.
		RosStrokeList* ready = StrokePreprocess(rough, F(-4), F(-4), F(1), 2);
		EXPECT(ready != nil && ready->fCount == 1);
		EXPECT(ready->fStrokes[0] != rough);
		EXPECT(ready->fStrokes[0]->fCount == 3);
		SLDestroy(ready, 1);
		// the two steps have weights of their own: with only the
		// dequantising asked for, the spike is pulled in but not
		// smoothed again
		ready = StrokePreprocess(rough, 0, F(-4), F(1), 3);
		EXPECT(ready->fStrokes[0] != rough);
		EXPECT(ready->fStrokes[0]->fPoints[1].y >= F(10) - 0x8000
			&& ready->fStrokes[0]->fPoints[1].y <= F(10) + 0x8000);
		SLDestroy(ready, 1);
		// ... and with only the smoothing, the spike moves by the whole
		// second difference
		ready = StrokePreprocess(rough, F(-4), 0, 0, 0);
		EXPECT(ready->fStrokes[0]->fPoints[1].y == F(30));
		SLDestroy(ready, 1);
		// with nothing asked for it is still a copy
		ready = StrokePreprocess(rough, 0, 0, 0, 0);
		EXPECT(ready->fStrokes[0] != rough && ready->fStrokes[0]->fCount == 3);
		EXPECT(ready->fStrokes[0]->fPoints[1].y == F(10));
		SLDestroy(ready, 1);

		StrokeDestroy(rough);
	}

	StrokeDestroy(stroke);

	if (failures == 0)
		printf("test_RosStrokes: all passed\n");
	else
		printf("test_RosStrokes: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
