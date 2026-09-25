/*
	File:		recognition/GeoContext.cpp

	Contains:	What the geometry between two letters costs - see
				GeoContext.h.

	Reconstructed from the MP2x00 US ROM; each function cites its
	origin.
*/

#include "GeoContext.h"
#include "RosEngine.h"
#include "RosStrokes.h"
#include "Segment.h"
#include "FixedMath.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"

#include <string.h>


// ROM 0x0c100e1c (unnamed)
GeoBox*		gGeoBox1 = nil;
// ROM 0x0c100e20 (unnamed)
GeoBox*		gGeoBox2 = nil;
// ROM 0x0c100e24 (unnamed)
UByte*		gGeoCacheChar1 = nil;
// ROM 0x0c100e28 (unnamed)
UByte*		gGeoCacheChar2 = nil;
// ROM 0x0c100e2c (unnamed)
short*		gGeoCacheScore = nil;
// ROM 0x0c100e0c (unnamed)
long		gGeoCacheCount = 0;
// ROM 0x0c100e30 (unnamed)
RosSegment*	gGeoCacheSeg1 = nil;
// ROM 0x0c100e34 (unnamed)
RosSegment*	gGeoCacheSeg2 = nil;
// ROM 0x0c100e10 (unnamed)
long		gGeoCacheHits = 0;
// ROM 0x0c100e14 (unnamed)
long		gGeoCacheMisses = 0;
// ROM 0x0c100e18 (unnamed)
long		gGeoCacheHighWater = 0;


// A 16.16 number divided by sixty-four, rounding towards nought - the
// ROM's `if (v < 0) v += 63; v >>= 6`.  Everything the geometry
// measures is brought down by this much first, so that the sums and
// the squares below cannot run off the top of a 16.16 number.
static Fixed
GeoScaleDown(Fixed value)
{
	if (value < 0)
		value += 0x3f;
	return value >> 6;
}


// ... and the mean of four of them, rounded the same way.
static Fixed
GeoQuarter(Fixed value)
{
	if (value < 0)
		value += 3;
	return value >> 2;
}


// ROM 0x000d9ad4 GeoCacheAllocate
// The two boxes and the cache of a hundred answers, made once.  The
// ROM writes the five allocations out one after another with the same
// failure test after each; the handler gives back whatever was made
// before the throw goes on.
void
GeoCacheAllocate(void)
{
	if (gGeoCacheScore != nil)
		return;

	newton_try
	{
		gGeoCacheScore = (short*) RosAllocate(kGeoCacheSize * 2);
		gGeoCacheChar1 = (UByte*) RosAllocate(kGeoCacheSize);
		gGeoCacheChar2 = (UByte*) RosAllocate(kGeoCacheSize);
		gGeoBox1 = (GeoBox*) RosAllocate(sizeof(GeoBox));
		gGeoBox2 = (GeoBox*) RosAllocate(sizeof(GeoBox));
	}
	newton_catch_all
	{
		GeoCacheDeallocate();
		rethrow;
	}
	end_try;

	GeoContextClearCache();
}


// ROM 0x000d9c44 GeoCacheDeallocate
void
GeoCacheDeallocate(void)
{
	if (gGeoCacheChar1 != nil)
	{
		DisposPtr((Ptr) gGeoCacheChar1);
		gGeoCacheChar1 = nil;
	}
	if (gGeoCacheChar2 != nil)
	{
		DisposPtr((Ptr) gGeoCacheChar2);
		gGeoCacheChar2 = nil;
	}
	if (gGeoCacheScore != nil)
	{
		DisposPtr((Ptr) gGeoCacheScore);
		gGeoCacheScore = nil;
	}
	if (gGeoBox1 != nil)
	{
		DisposPtr((Ptr) gGeoBox1);
		gGeoBox1 = nil;
	}
	if (gGeoBox2 != nil)
	{
		DisposPtr((Ptr) gGeoBox2);
		gGeoBox2 = nil;
	}
	gGeoCacheCount = 0;
	gGeoCacheSeg1 = nil;
	gGeoCacheSeg2 = nil;
}


// ROM 0x000d9cc8 GeoContextClearCache
// Emptied: nothing in it and no pair of segments it is for.  The
// search calls this whenever it moves on.
void
GeoContextClearCache(void)
{
	gGeoCacheCount = 0;
	gGeoCacheSeg1 = nil;
	gGeoCacheSeg2 = nil;
}


// ROM 0x000d9ce4 GeoCQuiesence
// What the engine is told to do when it is put to sleep: give the
// cache back.  (One instruction in the ROM - a branch straight into
// `GeoCacheDeallocate`.)
void
GeoCQuiesence(void)
{
	GeoCacheDeallocate();
}


// ROM 0x000da19c GeoContextAux1
// The two observed boxes brought to a common size and place.
//
// Each box becomes six numbers: its width, its segment's smallest
// stroke, and its four edges with the y values *turned over*, so that
// up is positive as it is in the nominal drawing of a character.  All
// twelve are divided by sixty-four first, because what follows squares
// them.
//
// The four y edges are then moved so that their mean is nought, the
// four x edges likewise, and everything is scaled so that the eight
// edges come to sixteen between them.  What is left is a pair of boxes
// whose *shape and relative placing* is all that remains - which is
// exactly the question the geometry is asked.
void
GeoContextAux1(const FRect* bounds1, const FRect* bounds2,
				Fixed smallest1, Fixed smallest2)
{
	gGeoBox1->fBottom = -bounds1->bottom;
	gGeoBox1->fTop = -bounds1->top;
	gGeoBox1->fLeft = bounds1->left;
	gGeoBox1->fRight = bounds1->right;
	gGeoBox1->fSmallestStroke = smallest1;

	gGeoBox2->fBottom = -bounds2->bottom;
	gGeoBox2->fTop = -bounds2->top;
	gGeoBox2->fLeft = bounds2->left;
	gGeoBox2->fRight = bounds2->right;
	gGeoBox2->fSmallestStroke = smallest2;

	gGeoBox1->fWidth = gGeoBox1->fRight - gGeoBox1->fLeft;
	gGeoBox2->fWidth = gGeoBox2->fRight - gGeoBox2->fLeft;

	Fixed* a = (Fixed*) gGeoBox1;
	Fixed* b = (Fixed*) gGeoBox2;
	for (long i = 0; i < 6; i++)
	{
		a[i] = GeoScaleDown(a[i]);
		b[i] = GeoScaleDown(b[i]);
	}

	// the four y edges centred ...
	Fixed mid = GeoQuarter(gGeoBox1->fBottom + gGeoBox1->fTop
					+ gGeoBox2->fBottom + gGeoBox2->fTop);
	gGeoBox1->fBottom -= mid;
	gGeoBox2->fBottom -= mid;
	gGeoBox1->fTop -= mid;
	gGeoBox2->fTop -= mid;

	// ... and the four x edges
	mid = GeoQuarter(gGeoBox1->fLeft + gGeoBox1->fRight
					+ gGeoBox2->fLeft + gGeoBox2->fRight);
	gGeoBox1->fLeft -= mid;
	gGeoBox2->fLeft -= mid;
	gGeoBox1->fRight -= mid;
	gGeoBox2->fRight -= mid;

	Fixed total = 0;
	for (long i = 2; i < 6; i++)
	{
		Fixed va = a[i];
		Fixed vb = b[i];
		total += (va < 0 ? -va : va) + (vb < 0 ? -vb : vb);
	}
	if (total < 1)
		return;

	// eight over the sum, so the eight edges come to sixteen
	Fixed scale = FixedDivide(0x80000, total) << 1;
	for (long i = 0; i < 6; i++)
	{
		a[i] = FixedMultiply(a[i], scale);
		b[i] = FixedMultiply(b[i], scale);
	}
}


// ROM 0x000da600 GeoContextAux2
// The nine measurements, and what they come to.
//
// The nominal drawing of the two characters is laid out: the first
// with its right edge at nought and its left at minus its width, the
// second `dx` further along, where `dx` is what the two characters
// between them say the gap ought to be.  Then the same centring the
// observed boxes went through, a least-squares fit of the one size
// that brings the observed nearest the nominal, and nine residuals -
// each weighted by what the two characters say that measurement is
// worth about them.
//
// The nine are weighed through a symmetric nine-by-nine matrix as a
// quadratic form, which is a Mahalanobis distance: the residuals are
// weighed against each other, not merely added up.
long
GeoContextAux2(ULong char1, ULong char2, long strokes1, long strokes2,
				long oneCharacter, long adjacency)
{
	const Fixed* const* p = RosCI->fCharParams;
	ULong one = (ULong) oneCharacter & 0xff;

	// how far apart the two nominal boxes should stand
	Fixed dx;
	if (one == 0)
	{
		if (adjacency == 0)
			// two letters of one word: the room the first wants after
			// it and the second before it, halved
			dx = FixedMultiply(0x8000, p[kGeoGapBefore][char2]
							+ p[kGeoGapAfter][char1]);
		else if (adjacency == 1)
			// a boundary: the two boxes laid on one another
			dx = -FixedMultiply(0x8000, p[kGeoWidth][char1]
							+ p[kGeoWidth][char2]);
		else if (adjacency == 2)
			// two words: the room between them, and three eighths of
			// each character's width on top of it
			dx = FixedMultiply(0x8000, p[kGeoGapBefore][char2]
							+ p[kGeoGapAfter][char1])
				+ FixedMultiply(0x6000, p[kGeoWidth][char2]
							+ p[kGeoWidth][char1]);
		else
			// the ROM leaves the register holding its second argument
			// here; nothing ever passes a fourth kind of adjacency.
			dx = (Fixed) char2;
	}
	else
		// one character standing for both: its own box again
		dx = -p[kGeoWidth][char1];

	// the nominal drawing
	Fixed bottom1 = p[kGeoBottom][char1];
	Fixed top1 = p[kGeoHeight][char1] + p[kGeoBottom][char1];
	Fixed left1 = -p[kGeoWidth][char1];
	Fixed right1 = 0;
	Fixed width1 = p[kGeoWidth][char1];
	Fixed stroke1 = p[strokes1 == 1 ? kGeoStrokeOne : kGeoStrokeMany][char1];

	Fixed bottom2 = p[kGeoBottom][char2];
	Fixed top2 = p[kGeoHeight][char2] + p[kGeoBottom][char2];
	Fixed left2 = dx;
	Fixed right2 = p[kGeoWidth][char2] + dx;
	Fixed width2 = p[kGeoWidth][char2];
	Fixed stroke2 = p[strokes2 == 1 ? kGeoStrokeOne : kGeoStrokeMany][char2];

	// centred the same way the observed boxes were
	Fixed mid = GeoQuarter(bottom1 + top1 + bottom2 + top2);
	bottom1 -= mid;
	bottom2 -= mid;
	top1 -= mid;
	top2 -= mid;

	mid = GeoQuarter(left1 + left2 + right2);	// right1 is nought
	left1 -= mid;
	left2 -= mid;
	right1 -= mid;
	right2 -= mid;

	// the one size that brings the observed nearest the nominal: the
	// nominal's own energy over what the two have in common, and never
	// more than life size
	Fixed energy = FixedMultiply(bottom1, bottom1) + FixedMultiply(bottom2, bottom2)
				+ FixedMultiply(top1, top1) + FixedMultiply(top2, top2)
				+ FixedMultiply(left1, left1) + FixedMultiply(left2, left2)
				+ FixedMultiply(right1, right1) + FixedMultiply(right2, right2);
	Fixed common = FixedMultiply(bottom1, gGeoBox1->fBottom)
				+ FixedMultiply(bottom2, gGeoBox2->fBottom)
				+ FixedMultiply(top1, gGeoBox1->fTop)
				+ FixedMultiply(top2, gGeoBox2->fTop)
				+ FixedMultiply(left1, gGeoBox1->fLeft)
				+ FixedMultiply(left2, gGeoBox2->fLeft)
				+ FixedMultiply(right1, gGeoBox1->fRight)
				+ FixedMultiply(right2, gGeoBox2->fRight);
	Fixed scale = (energy < common) ? FixedDivide(energy, common) : 0x00010000;

	Fixed sawWidth1 = FixedMultiply(gGeoBox1->fWidth, scale);
	Fixed sawWidth2 = FixedMultiply(gGeoBox2->fWidth, scale);
	Fixed sawStroke1 = FixedMultiply(gGeoBox1->fSmallestStroke, scale);
	Fixed sawStroke2 = FixedMultiply(gGeoBox2->fSmallestStroke, scale);
	Fixed sawBottom1 = FixedMultiply(gGeoBox1->fBottom, scale);
	Fixed sawBottom2 = FixedMultiply(gGeoBox2->fBottom, scale);
	Fixed sawTop1 = FixedMultiply(gGeoBox1->fTop, scale);
	Fixed sawTop2 = FixedMultiply(gGeoBox2->fTop, scale);
	Fixed sawLeft2 = FixedMultiply(gGeoBox2->fLeft, scale);
	Fixed sawRight1 = FixedMultiply(gGeoBox1->fRight, scale);

	// the gap actually left between the two, before the residuals are
	// taken
	Fixed gap = (adjacency == 0) ? sawLeft2 - sawRight1 : 0;

	sawWidth1 -= width1;
	sawWidth2 -= width2;
	sawStroke1 -= stroke1;
	sawStroke2 -= stroke2;
	sawBottom1 -= bottom1;
	sawBottom2 -= bottom2;
	sawTop1 -= top1;
	sawTop2 -= top2;

	// what each measurement is worth about these two characters: the
	// least either of them has to say about either edge ...
	Fixed worth = p[kGeoWeightBottom][char1];
	if (p[kGeoWeightBottom][char2] < worth)
		worth = p[kGeoWeightBottom][char2];
	if (p[kGeoWeightTop][char1] < worth)
		worth = p[kGeoWeightTop][char1];
	if (p[kGeoWeightTop][char2] < worth)
		worth = p[kGeoWeightTop][char2];
	sawBottom1 = FixedMultiply(sawBottom1, worth);
	sawBottom2 = FixedMultiply(sawBottom2, worth);
	sawTop1 = FixedMultiply(sawTop1, worth);
	sawTop2 = FixedMultiply(sawTop2, worth);

	// ... about their widths ...
	worth = p[kGeoWeightWidth][char1];
	if (p[kGeoWeightWidth][char2] < worth)
		worth = p[kGeoWeightWidth][char2];
	sawWidth1 = FixedMultiply(sawWidth1, worth);
	sawWidth2 = FixedMultiply(sawWidth2, worth);

	// ... and about their smallest stroke, which depends on whether
	// each was written in one stroke or in more
	sawStroke1 = FixedMultiply(sawStroke1,
				p[strokes1 == 1 ? kGeoWeightStrokeOne : kGeoWeightStrokeMany][char1]);
	sawStroke2 = FixedMultiply(sawStroke2,
				p[strokes2 == 1 ? kGeoWeightStrokeOne : kGeoWeightStrokeMany][char2]);

	// The gap is judged against two expectations, the room the first
	// character wants after it and the room the second wants before
	// it.  If they disagree about which way the gap is wrong the
	// measurement is dropped - the gap is somewhere between the two,
	// which is no complaint at all - and otherwise the milder of the
	// two is taken.
	Fixed gapResidual = gap;
	if (adjacency == 0)
	{
		Fixed after = FixedMultiply(gap - p[kGeoGapAfter][char1],
						p[kGeoWeightGapAfter][char1]);
		Fixed before = FixedMultiply(gap - p[kGeoGapBefore][char2],
						p[kGeoWeightGapBefore][char2]);
		if (FixedMultiply(after, before) < 1)
			gapResidual = 0;
		else
		{
			Fixed a = (after < 0) ? -after : after;
			Fixed b = (before < 0) ? -before : before;
			gapResidual = (a < b) ? after : before;
		}
	}

	Fixed v[kGeoMeasurements];
	v[kGeoBottom1] = sawBottom1;
	v[kGeoTop1] = sawTop1;
	v[kGeoWidth1] = sawWidth1;
	v[kGeoStroke1] = sawStroke1;
	if (one == 0)
	{
		v[kGeoBottom2] = sawBottom2;
		v[kGeoTop2] = sawTop2;
		v[kGeoWidth2] = sawWidth2;
		v[kGeoStroke2] = sawStroke2;
	}
	else
	{
		// one character standing for both: the second has nothing of
		// its own to say
		v[kGeoBottom2] = 0;
		v[kGeoTop2] = 0;
		v[kGeoWidth2] = 0;
		v[kGeoStroke2] = 0;
	}
	v[kGeoGap] = (adjacency != 0) ? 0 : gapResidual;

	// the quadratic form
	Fixed total = 0;
	for (long i = 0; i < kGeoMeasurements; i++)
	{
		Fixed inner = 0;
		for (long j = 0; j < kGeoMeasurements; j++)
			inner += FixedMultiply(v[j], kGeoWeights[i * kGeoMeasurements + j]);
		total += FixedMultiply(inner, v[i]);
	}

	// the engine's own stand-in character is held to four times the
	// account
	if (char1 == 0x1f || char2 == 0x1f)
		total = total * 4;

	UShort score = 0x7ffe;
	if (((RosCI->fGeoWeight >> 16) + 1) * ((total >> 16) + 1) < 0x7fff)
		score = (UShort) (((ULong) FixedMultiply(total, RosCI->fGeoWeight)) >> 16);

	if (gGeoCacheCount < kGeoCacheSize)
	{
		gGeoCacheScore[gGeoCacheCount] = (short) score;
		gGeoCacheCount++;
		gGeoCacheMisses++;
		if (gGeoCacheHighWater < gGeoCacheCount)
			gGeoCacheHighWater = gGeoCacheCount;
	}
	return (long) score;
}


// ROM 0x000d9ce8 GeoContextPenalty
// What it costs to read one character after another, given the two
// pieces of ink they would be read from.
//
// Most of this is the cache: the search asks the same question over
// and over while it works through one pair of segments, so a hundred
// (first character, second character) answers are kept and thrown away
// whenever the pair of segments changes.  The boxes themselves
// (`GeoContextAux1`) are worked out once per pair, on the first
// question about it.
//
// Either character may be nought, meaning the start or the end of the
// word; then the other stands for both and the second half of the
// measurements is dropped.  `acrossWords` says the two are in
// different words, and 0x1f is the engine's own stand-in character.
long
GeoContextPenalty(UByte before, RosSegment* beforeSeg, UByte now,
				RosSegment* nowSeg, long acrossWords)
{
	long oneCharacter = 0;
	long adjacency = 0;

	// nothing to weigh when the geometry is switched off, or when
	// there is no second character
	if (RosCI->fGeoWeight == 0 || now == 0)
		return 0;

	if (gGeoCacheScore == nil)
		GeoCacheAllocate();

	if (gGeoCacheCount == 0
		|| gGeoCacheSeg1 != beforeSeg || gGeoCacheSeg2 != nowSeg)
	{
		GeoContextClearCache();
		gGeoCacheSeg1 = beforeSeg;
		gGeoCacheSeg2 = nowSeg;
	}
	else
	{
		for (long i = 0; i < gGeoCacheCount; i++)
			if (gGeoCacheChar1[i] == before && gGeoCacheChar2[i] == now)
			{
				gGeoCacheHits++;
				return (long) (UShort) gGeoCacheScore[i];
			}
	}
	// (the ROM searches the cache whether or not it has just emptied
	//  it; with nothing in it the loop does nothing.)
	if (gGeoCacheCount < kGeoCacheSize)
	{
		gGeoCacheChar1[gGeoCacheCount] = before;
		gGeoCacheChar2[gGeoCacheCount] = now;
	}

	// a character that is really there must have ink to be read from
	if ((before != 0 && beforeSeg == nil) || (now != 0 && nowSeg == nil))
		// the ROM builds a complaint here - "GeoContextPenalty:
		// non-null character passed in with null segment" - into a
		// stack buffer and drops it on the floor.
		return 0;

	// the two pieces of ink
	const FRect* bounds1 = nil;
	const FRect* bounds2 = nil;
	Fixed smallest1 = 0;
	Fixed smallest2 = 0;
	long strokes1 = 0;
	long strokes2 = 0;
	if (beforeSeg != nil)
	{
		bounds1 = &beforeSeg->fBounds;
		strokes1 = (beforeSeg->fStrokes == nil) ? 1 : beforeSeg->fStrokes->fCount;
		smallest1 = beforeSeg->fSmallestStroke;
	}
	if (nowSeg != nil)
	{
		bounds2 = &nowSeg->fBounds;
		strokes2 = (nowSeg->fStrokes == nil) ? 1 : nowSeg->fStrokes->fCount;
		smallest2 = nowSeg->fSmallestStroke;
	}
	if ((before != 0 && strokes1 == 0 && before != 0x1f)
		|| (now != 0 && strokes2 == 0 && now != 0x1f))
		// ... and another complaint the ROM drops: "non-null character
		// passed in with no strokes".
		return 0;

	// a character the area will not have cannot be weighed at all
	ULong char1 = before;
	ULong char2 = now;
	if ((char1 != 0 && (RosCI->fLegalUse[char1 >> 5] & (1UL << (char1 & 0x1f))) == 0
			&& char1 != 0x1f)
		|| (char2 != 0 && (RosCI->fLegalUse[char2 >> 5] & (1UL << (char2 & 0x1f))) == 0
			&& char2 != 0x1f))
		// "GeoContextPenalty handed token(s) not in Characteristics"
		return 0;

	if (char1 == 0 || char2 == 0)
	{
		// the start or the end of the word: the character there is
		// weighed against itself
		oneCharacter = 1;
		if (char1 == 0)
		{
			bounds1 = bounds2;
			smallest1 = smallest2;
			char1 = char2;
			strokes1 = strokes2;
		}
		if (char2 == 0)
		{
			bounds2 = bounds1;
			smallest2 = smallest1;
			char2 = char1;
			strokes2 = strokes1;
		}
		adjacency = 1;
	}
	else if (char1 == 0x1f || char2 == 0x1f)
		adjacency = 1;
	if (acrossWords != 0)
		adjacency = 2;

	// the boxes, once per pair of segments
	if (gGeoCacheCount == 0)
		GeoContextAux1(bounds1, bounds2, smallest1, smallest2);
	return GeoContextAux2(char1, char2, strokes1, strokes2, oneCharacter, adjacency);
}
