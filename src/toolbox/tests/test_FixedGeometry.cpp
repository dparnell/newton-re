// Points and rectangles in 16.16 fixed point (toolbox/FixedGeometry.h):
// the geometry the recognition system measures strokes with.
#include "FixedGeometry.h"

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
	// ---- points ----
	FPoint p;
	SetFixedPoint(&p, F(3), F(4));
	EXPECT(p.x == F(3) && p.y == F(4));

	FPoint q;
	CopyFixedPoint(&q, &p);
	EXPECT(q.x == F(3) && q.y == F(4));

	FPoint d;
	SetFixedPoint(&q, F(10), F(1));
	SubtractFixedPoints(&d, &q, &p);
	EXPECT(d.x == F(7) && d.y == F(-3));

	// ---- rectangles ----
	FRect r;
	SetFixedRect(&r, F(1), F(2), F(11), F(22));
	EXPECT(RectIs(r, 1, 2, 11, 22));

	FixedRectSize(&p, &r);
	EXPECT(p.x == F(10) && p.y == F(20));

	FRect copy;
	CopyFixedRect(&copy, &r);
	EXPECT(RectIs(copy, 1, 2, 11, 22));

	// valid is "the edges are the right way round", and an empty one is
	// valid too - which is what lets a bounding box start at nothing
	EXPECT(ValidFixedRect(&r) && !EmptyFixedRect(&r));
	FRect nothing;
	SetFixedRect(&nothing, 0, 0, 0, 0);
	EXPECT(ValidFixedRect(&nothing) && EmptyFixedRect(&nothing));
	FRect upside;
	SetFixedRect(&upside, F(1), F(9), F(11), F(2));
	EXPECT(!ValidFixedRect(&upside));
	FRect backwards;
	SetFixedRect(&backwards, F(11), F(2), F(1), F(9));
	EXPECT(!ValidFixedRect(&backwards));
	// a rectangle of no area is still not "empty": only four noughts are
	FRect flat;
	SetFixedRect(&flat, F(5), F(5), F(5), F(5));
	EXPECT(ValidFixedRect(&flat) && !EmptyFixedRect(&flat));

	// ---- the union ----
	// a bounding box started at nothing simply becomes the first thing
	// put into it
	FRect box;
	SetFixedRect(&box, 0, 0, 0, 0);
	OrFixedRect(&box, &r);
	EXPECT(RectIs(box, 1, 2, 11, 22));

	// ... and then grows
	FRect other;
	SetFixedRect(&other, F(-4), F(5), F(6), F(30));
	OrFixedRect(&box, &other);
	EXPECT(RectIs(box, -4, 2, 11, 30));

	// one wholly inside changes nothing
	FRect inside;
	SetFixedRect(&inside, F(0), F(3), F(5), F(6));
	OrFixedRect(&box, &inside);
	EXPECT(RectIs(box, -4, 2, 11, 30));

	// an empty source is passed over, so an empty stroke does not drag
	// the box down to the origin
	OrFixedRect(&box, &nothing);
	EXPECT(RectIs(box, -4, 2, 11, 30));
	// and so is one whose edges are the wrong way round
	OrFixedRect(&box, &upside);
	EXPECT(RectIs(box, -4, 2, 11, 30));

	// a destination that is not a rectangle becomes the source outright
	FRect bad;
	SetFixedRect(&bad, F(11), F(2), F(1), F(9));
	OrFixedRect(&bad, &r);
	EXPECT(RectIs(bad, 1, 2, 11, 22));

	// ---- scaling ----
	SetFixedRect(&r, F(1), F(2), F(11), F(22));
	XYFixedScaleFixedRect(&r, F(2), F(3));
	EXPECT(RectIs(r, 2, 6, 22, 66));
	// a half on one axis only
	SetFixedRect(&r, F(4), F(8), F(12), F(16));
	XYFixedScaleFixedRect(&r, 0x8000, F(1));
	EXPECT(RectIs(r, 2, 8, 6, 16));

	if (failures == 0)
		printf("test_FixedGeometry: all passed\n");
	else
		printf("test_FixedGeometry: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
