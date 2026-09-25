// What the geometry between two letters costs (recognition/GeoContext.h):
// the nominal drawing of a character against the ink it would be read
// from, the nine measurements, and the quadratic form they are weighed
// through.
//
// The interesting check is the last one: two narrow boxes side by side
// really do read more cheaply as `r` and `n` than as two `m`s, which is
// the whole reason the geometry exists.

#include "GeoContext.h"
#include "RosEngine.h"
#include "Segment.h"
#include "RosStrokes.h"
#include "FixedMath.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static Fixed	F(long n)		{ return (Fixed) (int) ((unsigned int) n << 16); }

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


// A piece of writing, made by hand: only its box and its smallest
// stroke are looked at, and a segment with no stroke list is taken to
// have one stroke.
static void
Ink(RosSegment* seg, long left, long top, long right, long bottom, long smallest)
{
	memset(seg, 0, sizeof(*seg));
	seg->fBounds.left = F(left);
	seg->fBounds.top = F(top);
	seg->fBounds.right = F(right);
	seg->fBounds.bottom = F(bottom);
	seg->fSmallestStroke = F(smallest);
}


static Fixed
Abs(Fixed v)
{
	return (v < 0) ? -v : v;
}


int
main()
{
	InitHostStandaloneHeap();
	CharInitialize(0);

	// ---- the weight matrix ----
	{
		// it is symmetric, which is what makes the quadratic form a
		// Mahalanobis distance rather than an arbitrary weighting
		for (long i = 0; i < kGeoMeasurements; i++)
			for (long j = 0; j < kGeoMeasurements; j++)
				EXPECT(kGeoWeights[i * kGeoMeasurements + j]
					== kGeoWeights[j * kGeoMeasurements + i]);
		// ... and its diagonal is positive, so every measurement on its
		// own costs something
		for (long i = 0; i < kGeoMeasurements; i++)
			EXPECT(kGeoWeights[i * kGeoMeasurements + i] > 0);
	}

	// ---- the nominal drawing of a character ----
	{
		const Fixed* const* p = RosCI->fCharParams;
		// all sixteen tables are there
		for (long i = 0; i < 16; i++)
			EXPECT(p[i] != nil);
		// an `m` is wider than an `i`, and an `M` wider still
		EXPECT(p[kGeoWidth]['m'] > p[kGeoWidth]['i']);
		EXPECT(p[kGeoWidth]['M'] > p[kGeoWidth]['m']);
		// a full stop is the smallest thing there is
		EXPECT(p[kGeoHeight]['.'] < p[kGeoHeight]['o']);
		// an apostrophe's bottom is most of the way up the line
		EXPECT(p[kGeoBottom]['\''] > F(1) / 2);
		EXPECT(p[kGeoBottom]['.'] < F(1) / 10);
		// an `i` written in one stroke has a stroke the height of its
		// stem; written in two, the smallest of them is the dot
		EXPECT(p[kGeoStrokeOne]['i'] > F(1) / 4);
		EXPECT(p[kGeoStrokeMany]['i'] < F(1) / 8);
		// and the geometry is switched on: twenty
		EXPECT(RosCI->fGeoWeight == F(20));
	}

	// ---- the two boxes brought to a common size and place ----
	{
		GeoCacheAllocate();
		EXPECT(gGeoBox1 != nil && gGeoBox2 != nil);
		EXPECT(gGeoCacheChar1 != nil && gGeoCacheChar2 != nil);
		EXPECT(gGeoCacheScore != nil);
		EXPECT(gGeoCacheCount == 0);

		RosSegment a, b;
		Ink(&a, 0, 0, 10, 20, 5);
		Ink(&b, 12, 2, 22, 20, 6);
		GeoContextAux1(&a.fBounds, &b.fBounds, a.fSmallestStroke, b.fSmallestStroke);

		// y is turned over, so up is positive
		EXPECT(gGeoBox1->fBottom < gGeoBox1->fTop);
		// the four y edges and the four x edges each average nought
		Fixed y = gGeoBox1->fBottom + gGeoBox1->fTop
				+ gGeoBox2->fBottom + gGeoBox2->fTop;
		Fixed x = gGeoBox1->fLeft + gGeoBox1->fRight
				+ gGeoBox2->fLeft + gGeoBox2->fRight;
		EXPECT(Abs(y) < F(1) / 16);
		EXPECT(Abs(x) < F(1) / 16);
		// ... and the eight of them come to sixteen between them, so
		// what is left is the shape and nothing of the size
		Fixed sum = Abs(gGeoBox1->fBottom) + Abs(gGeoBox1->fTop)
				+ Abs(gGeoBox2->fBottom) + Abs(gGeoBox2->fTop)
				+ Abs(gGeoBox1->fLeft) + Abs(gGeoBox1->fRight)
				+ Abs(gGeoBox2->fLeft) + Abs(gGeoBox2->fRight);
		EXPECT(Abs(sum - F(16)) < F(1) / 8);
		// the second box is to the right of the first
		EXPECT(gGeoBox2->fLeft > gGeoBox1->fRight);
		// the widths went along with the edges
		EXPECT(gGeoBox1->fWidth > 0 && gGeoBox2->fWidth > 0);
		// twice the ink, twice the answer - the same shape drawn at
		// twice the size normalises to the same numbers
		Fixed wasWidth = gGeoBox1->fWidth;
		Fixed wasLeft = gGeoBox2->fLeft;
		RosSegment a2, b2;
		Ink(&a2, 0, 0, 20, 40, 10);
		Ink(&b2, 24, 4, 44, 40, 12);
		GeoContextAux1(&a2.fBounds, &b2.fBounds, a2.fSmallestStroke, b2.fSmallestStroke);
		EXPECT(Abs(gGeoBox1->fWidth - wasWidth) < F(1) / 8);
		EXPECT(Abs(gGeoBox2->fLeft - wasLeft) < F(1) / 8);
	}

	// ---- what is refused ----
	{
		RosSegment a;
		Ink(&a, 0, 0, 10, 20, 5);
		// no second character at all
		EXPECT(GeoContextPenalty('m', &a, 0, nil, 0) == 0);
		// a character with no ink to be read from
		GeoContextClearCache();
		EXPECT(GeoContextPenalty('m', nil, 'n', &a, 0) == 0);
		// A character the area will not have - a **space**, which the
		// engine does not read: it is given one word at a time, and
		// `VerifyWordSymbols` says so too.  (Nought is not in the
		// legal set either, but it means the start or the end of the
		// word, so it is let through.)
		GeoContextClearCache();
		EXPECT((RosCI->fLegalUse[' ' >> 5] & (1UL << (' ' & 0x1f))) == 0);
		EXPECT(GeoContextPenalty('m', &a, ' ', &a, 0) == 0);
	}

	// ---- the cache ----
	{
		GeoContextClearCache();
		RosSegment a, b;
		Ink(&a, 0, 0, 8, 20, 6);
		Ink(&b, 10, 0, 18, 20, 6);

		long wasHits = gGeoCacheHits;
		long wasMisses = gGeoCacheMisses;
		long first = GeoContextPenalty('r', &a, 'n', &b, 0);
		EXPECT(gGeoCacheCount == 1);
		EXPECT(gGeoCacheMisses == wasMisses + 1);
		EXPECT(gGeoCacheHits == wasHits);
		EXPECT(gGeoCacheSeg1 == &a && gGeoCacheSeg2 == &b);
		// the same question again is answered out of the cache
		EXPECT(GeoContextPenalty('r', &a, 'n', &b, 0) == first);
		EXPECT(gGeoCacheHits == wasHits + 1);
		EXPECT(gGeoCacheCount == 1);
		// another pair of characters about the same ink is another
		// entry, and the boxes are not worked out again
		GeoContextPenalty('m', &a, 'm', &b, 0);
		EXPECT(gGeoCacheCount == 2);
		// ... but another pair of segments empties it
		RosSegment c;
		Ink(&c, 20, 0, 28, 20, 6);
		GeoContextPenalty('r', &a, 'n', &c, 0);
		EXPECT(gGeoCacheCount == 1);
		EXPECT(gGeoCacheSeg2 == &c);
		EXPECT(gGeoCacheHighWater >= 2);
	}

	// ---- `rn` and `m` ----
	{
		// Two narrow boxes side by side.  The classifier sees much the
		// same ink whether the writer meant `rn` or `m`; it is the
		// geometry that says which, because two `m`s would be twice as
		// wide as this and `r` and `n` are just this wide.
		GeoContextClearCache();
		RosSegment a, b;
		Ink(&a, 0, 0, 7, 14, 10);
		Ink(&b, 8, 0, 15, 14, 10);
		long rn = GeoContextPenalty('r', &a, 'n', &b, 0);
		long mm = GeoContextPenalty('m', &a, 'm', &b, 0);
		EXPECT(rn == 478);
		EXPECT(mm == 2487);
		// five times as much, and nearly all of the difference is in
		// the four height residuals: two `m`s would be far wider
		// against their height than this ink is, and both letters
		// are wrong about it the same way, which is what the
		// matrix charges most for
		EXPECT(rn < mm);
		// ... and neither is free, because nothing is written exactly
		// the way the engine draws it
		EXPECT(rn > 0);

		// a full stop is a bad reading of a box the height of the line
		GeoContextClearCache();
		long oo = GeoContextPenalty('o', &a, 'o', &b, 0);
		EXPECT(oo == 315);
		GeoContextClearCache();
		long dots = GeoContextPenalty('.', &a, '.', &b, 0);
		EXPECT(oo < dots);

		// the start of a word: one character weighed against itself,
		// which the second half of the measurements takes no part in
		GeoContextClearCache();
		long alone = GeoContextPenalty(0, nil, 'o', &a, 0);
		EXPECT(alone >= 0);
		// the engine's own stand-in character is held to four times
		// the account, so it is never the cheap way out
		GeoContextClearCache();
		long stand = GeoContextPenalty(0x1f, &a, 'n', &b, 0);
		EXPECT(stand > 0);

		// a score is a short and never more than `never`
		EXPECT(rn < 0x7fff && mm < 0x7fff && dots < 0x7fff);
	}

	// ---- given back ----
	{
		GeoCacheDeallocate();
		EXPECT(gGeoBox1 == nil && gGeoBox2 == nil);
		EXPECT(gGeoCacheScore == nil);
		EXPECT(gGeoCacheCount == 0);
		// ... and made again on demand
		RosSegment a, b;
		Ink(&a, 0, 0, 8, 20, 6);
		Ink(&b, 10, 0, 18, 20, 6);
		GeoContextPenalty('r', &a, 'n', &b, 0);
		EXPECT(gGeoCacheScore != nil);
		GeoCQuiesence();
		EXPECT(gGeoCacheScore == nil);
	}

	if (failures != 0)
	{
		fprintf(stderr, "test_GeoContext: %d failure(s)\n", failures);
		return 1;
	}
	printf("test_GeoContext: all passed\n");
	return 0;
}
