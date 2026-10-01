/*
	File:		recognition/Segment.cpp

	Contains:	The segment layer - see Segment.h.

	Reconstructed from the MP2x00 US ROM; each function cites its
	origin.
*/

#include "Segment.h"
#include "RosEngine.h"
#include "WordRecog.h"		// FragmentLigatures
#include "FixedGeometry.h"
#include "FixedMath.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"

#include <math.h>


// ROM 0x0c101ae8 (unnamed)
// How much two pieces must agree before the engine runs them together:
// nine tenths when it has been told the writing is joined up, a half
// when it has not.
Fixed	gSegIntegrated = 0;

// ROM 0x0c101ae0 (unnamed)
// How wide a space is taken to be, as a multiple of what it would be
// for a writer of ordinary habits: `SegmentSetWordSpacing` works it out
// from the setting the writer chose.
Fixed	gSegWordSpacing = 0;
// ROM 0x0c101ae4 (unnamed)
// ... and its natural logarithm, in 16.16, kept because the layers
// above add it rather than multiply by it.
Fixed	gSegLogWordSpacing = 0;
// ROM 0x0c101aec (unnamed)
// The threshold that goes with it, interpolated between the three
// below.
Fixed	gSegOnlyThreshold = 0;

// The three it is interpolated between: four tenths at the tightest
// spacing, a half in the middle, seven tenths at the loosest.  They
// live in the initialised RAM area, so the engine may change them.
// ROM 0x0c101af0 MinSegOnlyThreshold
Fixed	MinSegOnlyThreshold = 26214;
// ROM 0x0c101af4 MidSegOnlyThreshold
Fixed	MidSegOnlyThreshold = 32768;
// ROM 0x0c101af8 MaxSegOnlyThreshold
Fixed	MaxSegOnlyThreshold = 45875;


// ROM 0x001d2490 SegmentSetWordSpacing
// The writer's word-spacing setting turned into the two numbers the
// segment layer works to.  `RosettaSetArea` passes `9 - n` for the
// setting `n` the recognition area carries, so it runs 1 to 9 with 5 in
// the middle, and 5 is the writer of ordinary habits: a factor of
// exactly one, and the middle threshold.
//
// Below five it ramps gently: 0.15 + 0.85 x (n/5), so the tightest
// setting still weighs a gap at about a third.  The threshold is
// interpolated the same way, between `MinSegOnlyThreshold` (0.4) and
// `MidSegOnlyThreshold` (0.5).
//
// The logarithm is taken once, here, in double precision, because what
// the layers above want is to *add* it to a score.  This is the only
// floating point in the whole engine.
//
// **A ROM bug, kept.**  Above five the factor is
// `1 + 1.4375 x (n-5)`, but the constant in the ROM is `0x170000` -
// twenty-three - where the pattern of the rest of the routine wants
// `0x17000`, one and seven sixteenths.  The curve it actually
// computes is
//
//     1   2     3     4     5     6     7      8      9
//     .32 .49   .66   .83   1.00  6.75  12.50  18.25  24.00
//
// which jumps by a factor of nearly seven between the middle setting
// and the one next to it, and asks for a gap twenty-four times normal
// at the loosest.  With the extra zero gone it would run 1.00, 1.36,
// 1.72, 2.08, 2.44 and join the lower half smoothly.  The logarithm
// keeps it from being catastrophic - the score term only runs from
// -1.14 to 3.18 - but the top half of the writer's spacing slider does
// not do what the bottom half does.  Ported as it stands.
void
SegmentSetWordSpacing(long spacing)
{
	if (spacing == 5)
	{
		gSegWordSpacing = 0x00010000;
		gSegOnlyThreshold = MidSegOnlyThreshold;
	}
	else if (spacing < 5)
	{
		// n/5, as a fraction - a plain signed divide, as the ROM's
		// `__rt_sdiv` does it, not a FixedDivide
		Fixed part = (Fixed) (((long) (int) ((unsigned int) spacing << 16)) / 5);
		gSegWordSpacing = 0x2666 + FixedMultiply(part, 0xd99a);
		gSegOnlyThreshold = MinSegOnlyThreshold
					+ FixedMultiply(part, MidSegOnlyThreshold - MinSegOnlyThreshold);
	}
	else
	{
		// (n-5)/4, written as the compiler wrote a signed divide by
		// four - the bias is dead here, since the value cannot be
		// negative, but it is what the ROM does.  0x170000 is the bug
		// above: twenty-three, where 0x17000 was surely meant.
		long over = (long) (int) ((unsigned int) (spacing - 5) << 16);
		long biased = (over < 0) ? over + 3 : over;
		gSegWordSpacing = FixedMultiply(biased >> 2, 0x170000) + 0x00010000;
		gSegOnlyThreshold = MidSegOnlyThreshold
					+ FixedMultiply(biased >> 2, MaxSegOnlyThreshold - MidSegOnlyThreshold);
	}

	// the natural log of it, back in 16.16.  The ROM converts to double
	// with FLT, multiplies by 1/65536, calls `log`, multiplies by 65536
	// and comes back with FIX rounding toward zero, which is what a C
	// cast does.
	gSegLogWordSpacing = (Fixed) (long) (log((double) gSegWordSpacing / 65536.0) * 65536.0);
}
// What the layer is holding on to between words: `SegmentMakeSegments`
// is called once per stroke and keeps its working-out here.  The ROM's
// block is 0x44 bytes and is made on the first call; `SegmentQuiesce`
// is what gives it back.
struct SegState
{
	long		fStart;			// +0x00  the first stroke of the piece being built
	long		fBoundsTo;		// +0x04  how far `fBounds` has been accumulated
	long		fCut;			// +0x08  the last stroke of the piece
	long		fLastCut;		// +0x0c  where the piece before it ended
	long		fBreakAt;		// +0x10  how far down the break candidates
	long		fMade;			// +0x14  how many segments have been made
	long		fHasDot;		// +0x18  any stroke in the piece is a dot
	FRect		fBounds;		// +0x1c  the box the piece fills
	FRect		fScratch;		// +0x2c
	Fixed		fAspect;		// +0x3c  how wide against how tall it is now
	Fixed		fPrevAspect;	// +0x40  ... and before the last stroke went in
};

// ROM 0x0c101afc (unnamed)
static SegState*	gSegState = nil;


// ROM 0x001d4cbc SegmentIntegrated
void
SegmentIntegrated(long integrated)
{
	gSegIntegrated = (integrated == 0) ? 0x8000 : 0xe666;
}


// ROM 0x001d0f3c SegmentQuiesce
void
SegmentQuiesce(void)
{
	if (gSegState != nil)
		DisposPtr((Ptr) gSegState);
	gSegState = nil;
}


// ROM 0x001d1890 SegmentMinStrokeSize
// One field of the common info, under another name.
Fixed
SegmentMinStrokeSize(void)
{
	return RosCI->fMinStrokeSize;
}


#pragma mark -
/*--------------------------------------------------------------------
	A segment.
--------------------------------------------------------------------*/

// ROM 0x001d0ed8 SegmentInit
// The three indices start at -1 and the box at nothing.  `fFragment`
// and `fJoinsNext` are *not* set here: `SegmentBoundsDotsEtc` writes
// them from the strokes before anyone reads them.
void
SegmentInit(RosSegment* self)
{
	if (self == nil)
		return;
	self->fFirstStroke = -1;
	self->fCount = 0;
	self->fField04 = -1;
	self->fRealCount = -1;
	self->fStrokes = nil;
	self->fHasDot = 0;
	self->fSmallestStroke = 0;
	SetFixedRect(&self->fBounds, 0, 0, 0, 0);
}


// ROM 0x001d0e68 SegmentCreate
RosSegment*
SegmentCreate(void)
{
	RosSegment* self = (RosSegment*) RosAllocate((long) sizeof(RosSegment));
	if (self == nil)
		return nil;
	SegmentInit(self);
	return self;
}


// ROM 0x001d1cac SegmentDestroy
// The strokes go with it: they are the joined copies the segment made
// for itself, not the caller's.
void
SegmentDestroy(RosSegment* self)
{
	if (self == nil)
		return;
	if (self->fStrokes != nil)
		SLDestroy(self->fStrokes, 1);
	DisposPtr((Ptr) self);
}


// ROM 0x001d1800 SegmentSetStrokes
// The strokes a segment is over.  They are sorted into the order they
// sit on the line and then every run of adjoining pieces is joined
// back into one stroke, so a segment always holds whole strokes even
// though the cutting above it works on fragments.  What it holds is a
// copy: the list it was given is thrown away and its strokes are left
// to whoever owns them.
void
SegmentSetStrokes(RosSegment* self, short count, RosStroke* const* strokes)
{
	RosStrokeList* list = SLCreate(count, strokes);
	newton_try
	{
		SLSort(list);
		self->fStrokes = SLJoinFragments(list);
	}
	cleanup
	{
		SLDestroy(list, 0);
	}
	end_try;
	SLDestroy(list, 0);
}


// ROM 0x001d4b60 SegmentBoundsDotsEtc
// What is worth knowing about a segment without looking at its strokes
// again: the box they all fill, whether any of them is a dot, whether
// any is a piece of a larger stroke or runs on into the next, and how
// big the *smallest* of them is.
//
// That last one is measured oddly and deliberately: each stroke is
// taken at its larger dimension, and the answer is the least of those.
// So a segment made of a tall letter and a dot is as small as the dot,
// which is what lets the layer above notice that something in the
// piece is too small to be a letter on its own.
void
SegmentBoundsDotsEtc(RosSegment* self)
{
	if (self->fCount <= 0)
		return;

	RosStroke** strokes = self->fStrokes->fStrokes;
	FRect bounds;
	StrokeFindBounds(strokes[0], &bounds);
	CopyFixedRect(&self->fBounds, &bounds);

	Fixed smallest = self->fBounds.right - self->fBounds.left;
	Fixed height = self->fBounds.bottom - self->fBounds.top;
	if (smallest < height)
		smallest = height;

	self->fHasDot = SegmentDot(&bounds);
	self->fFragment = strokes[0]->fFragment;
	self->fJoinsNext = strokes[0]->fJoinsNext;

	for (short i = 1; i < self->fStrokes->fCount; i++)
	{
		StrokeFindBounds(strokes[i], &bounds);
		OrFixedRect(&self->fBounds, &bounds);

		Fixed tall = bounds.bottom - bounds.top;
		Fixed wide = bounds.right - bounds.left;
		Fixed larger = (wide < tall) ? tall : wide;
		if (larger < smallest)
			smallest = larger;

		self->fHasDot |= (UByte) SegmentDot(&bounds);
		self->fFragment |= strokes[i]->fFragment;
		self->fJoinsNext |= strokes[i]->fJoinsNext;
	}
	self->fSmallestStroke = smallest;
}


#pragma mark -
/*--------------------------------------------------------------------
	The measurements the cutting is made of.
--------------------------------------------------------------------*/

// ROM 0x001d4878 SegmentDot2
// Small in **both** directions.  A stroke four pixels wide and forty
// tall is not a dot, which is the whole point of asking twice.
Boolean
SegmentDot2(Fixed left, Fixed top, Fixed right, Fixed bottom)
{
	Fixed least = RosCI->fMinStrokeSize;
	Fixed size = right - left;
	if (size < least)
		size = bottom - top;
	return size < least;
}


// ROM 0x001d4870 SegmentDot
Boolean
SegmentDot(const FRect* bounds)
{
	return SegmentDot2(bounds->left, bounds->top, bounds->right, bounds->bottom);
}


// ROM 0x001d28cc SegmentAspect
// How wide against how tall, each measured inclusively so that a single
// point is one by one rather than nought by nought.
Fixed
SegmentAspect(const FRect* bounds)
{
	FPoint size;
	FixedRectSize(&size, bounds);
	return FixedDivide(size.x + 0x00010000, size.y + 0x00010000);
}


// ROM 0x001d1ce0 SegmentOverlapAr
// How much of the line two spans share, as the mean of the two
// fractions.  A narrow span wholly inside a wide one scores about a
// half: all of itself and a little of the other.  Two spans lying on
// each other score one, and two that do not meet score nothing.
//
// Every width is measured inclusively (`right - left + 1`), so a span
// of one point still has a width to divide by.
Fixed
SegmentOverlapAr(Fixed aLeft, Fixed aRight, Fixed bLeft, Fixed bRight)
{
	Fixed aWide = (aRight - aLeft) + 0x00010000;
	Fixed bWide = (bRight - bLeft) + 0x00010000;
	Fixed ofA, ofB;
	if (bLeft < aLeft)
	{
		// b starts to the left of a
		if (bRight < aLeft)
			return 0;
		if (bRight < aRight)
		{
			// they lap: from a's left to b's right
			Fixed shared = (bRight - aLeft) + 0x00010000;
			ofB = FixedDivide(shared, bWide);
			ofA = FixedDivide(shared, aWide);
		}
		else
		{
			// b covers a entirely
			ofA = 0x00010000;
			ofB = FixedDivide(aWide, bWide);
		}
	}
	else
	{
		// a starts first
		if (aRight < bLeft)
			return 0;
		Fixed shared;
		if (aRight < bRight)
		{
			shared = (aRight - bLeft) + 0x00010000;
			ofB = FixedDivide(shared, bWide);
		}
		else
		{
			// a covers b entirely
			ofB = 0x00010000;
			shared = bWide;
		}
		ofA = FixedDivide(shared, aWide);
	}
	return (ofB + ofA) >> 1;
}


// ROM 0x001d1c94 SegmentOverlap
Fixed
SegmentOverlap(const FRect* a, const FRect* b)
{
	return SegmentOverlapAr(a->left, a->right, b->left, b->right);
}


// ROM 0x001d1db4 SegmentSetStrokeOverlaps
// A segment's strokes told how much of the line each shares with the
// one before it, *now that the segment has them*.
//
// `SegmentStroke` worked this out once already, over the word's strokes
// in the order they were written.  But `SegmentSetStrokes` sorted them
// and joined the pieces the engine had cut, so a stroke's neighbour
// inside the segment is not the stroke that was its neighbour before.
// The first stroke of a segment is measured against the **last stroke
// of the segment before it**, or against nothing at all when it is the
// first segment of the word.
void
SegmentSetStrokeOverlaps(RosSegment* self, const RosSegment* previous)
{
	if (self == nil)
		return;
	RosStrokeList* list = self->fStrokes;
	if (list == nil || list->fCount < 1)
		return;

	FRect* bounds = &list->fStrokes[0]->fBounds;
	if (previous == nil)
		list->fStrokes[0]->fOverlap = 0;
	else
	{
		RosStrokeList* before = previous->fStrokes;
		RosStroke* last = before->fStrokes[before->fCount - 1];
		list->fStrokes[0]->fOverlap = SegmentOverlap(&last->fBounds, bounds);
	}

	for (short i = 1; i < list->fCount; i++)
	{
		RosStroke* stroke = list->fStrokes[i];
		list->fStrokes[i]->fOverlap = SegmentOverlap(bounds, &stroke->fBounds);
		bounds = &stroke->fBounds;
	}
}

// ROM 0x001d2224 SegmentStrokeData
// What the segment layer wants remembered about a stroke as it comes
// in.  `separation` is how far it is from the one before, which the
// layer above works out and the layer below reads.
void
SegmentStrokeData(RosStroke* stroke, UByte how, short index, Fixed separation)
{
	StrokeFindBounds(stroke, &stroke->fBounds);
	stroke->fIsDot = (UByte) SegmentDot(&stroke->fBounds);
	stroke->fField24 = how;
	stroke->fIndex = index;
	stroke->fSeparation = separation;
}


// ROM 0x001d2274 SegmentStrokeMinDistance
// Which two points of two strokes come nearest each other.
//
// The search is over every pair of points, and the thing compared is
// **|dx| + |dy|**, not the real distance: a square root per pair would
// cost more than the answer is worth, and the taxicab distance picks
// the same pair nearly always.  The real distance is worked out once,
// for the pair that won.  A pair that coincides exactly ends the
// search there and then.
void
SegmentStrokeMinDistance(RosStroke* a, RosStroke* b, SegmentDistance* out)
{
	out->fStrokeA = a;
	out->fStrokeB = b;

	// start from the two first points
	Fixed dx = a->fPoints[0].x - b->fPoints[0].x;
	if (dx < 0)
		dx = -dx;
	Fixed dy = a->fPoints[0].y - b->fPoints[0].y;
	if (dy < 0)
		dy = -dy;
	Fixed best = dx + dy;
	long bestA = 0;
	long bestB = 0;
	Boolean exact = false;

	for (long j = 0; j < b->fCount && !exact; j++)
	{
		for (long i = 0; i < a->fCount; i++)
		{
			Fixed px = a->fPoints[i].x - b->fPoints[j].x;
			if (px < 0)
				px = -px;
			Fixed py = a->fPoints[i].y - b->fPoints[j].y;
			if (py < 0)
				py = -py;
			Fixed sum = px + py;
			if (sum < best)
			{
				best = sum;
				bestA = i;
				bestB = j;
				if (sum == 0)
				{
					// they touch: nothing can beat it
					out->fDistance = 0;
					out->fDX = 0;
					out->fDY = 0;
					exact = true;
					break;
				}
			}
		}
	}

	if (!exact)
	{
		Fixed fx = a->fPoints[bestA].x - b->fPoints[bestB].x;
		Fixed fy = a->fPoints[bestA].y - b->fPoints[bestB].y;
		// ROM QUIRK: a plain ARM add (0x001d2428) - FixedMultiply pins a
		// square too big for 16.16 at 0x7fffffff, and two strokes far
		// apart (a stroke dragged across the screen on a page that reads
		// writing) make the sum wrap negative.  Wrapped here as the ARM
		// does (a C++ signed overflow is undefined, and the host's
		// sanitiser stopped a soak on it: tools/host/soak.py).
		Fract square = (Fract) ((ULong32) FixedMultiply(fx, fx) + (ULong32) FixedMultiply(fy, fy));
		out->fDistance = ((Fixed) ((ULong32) FractSquareRoot(square) + 0x40)) >> 7;	// (wrapping too)
		out->fDX = (fx < 0) ? -fx : fx;
		out->fDY = (fy < 0) ? -fy : fy;
	}
	out->fIndexB = (short) bestB;
	out->fIndexA = (short) bestA;
	out->fEitherIsDot = (UByte) (b->fIsDot != 0 || a->fIsDot != 0);
}


// ROM 0x001d47a4 SegmentCrossed
// Whether two strokes *cross*, as the upright and the bar of a t do,
// rather than merely meeting at their ends, as the two halves of a V
// do.  They cross when the two nearest points are both well inside
// their own strokes - three tenths of the way in from either end, which
// is what `fEndFraction` says - and the strokes come within
// `fLinkDistance` (three pixels) of each other at all.
Boolean
SegmentCrossed(const SegmentDistance* d)
{
	if (d == nil || d->fStrokeB == nil || d->fStrokeA == nil)
		return false;
	long countB = d->fStrokeB->fCount;
	long countA = d->fStrokeA->fCount;
	if (countB == 0 || countA == 0)
		return false;
	if (d->fDistance > RosCI->fLinkDistance)
		return false;

	long marginB = (short) ((int) ((unsigned int) RosCI->fEndFraction * (unsigned int) countB) >> 16);
	long marginA = (short) ((int) ((unsigned int) RosCI->fEndFraction * (unsigned int) countA) >> 16);
	return d->fIndexB >= marginB && d->fIndexB <= countB - marginB
		&& d->fIndexA >= marginA && d->fIndexA <= countA - marginA;
}


// ROM 0x001d1bcc SegmentNonTailLinked
// Whether two strokes that touch are joined somewhere other than end to
// end - the opposite question to `SegmentCrossed`, and the one that
// says a piece of writing is joined up rather than made of separate
// marks.
//
// **A ROM bug, kept.**  Written out, "not tail-linked" is
//
//     (idxB in the middle of B) or (idxA in the middle of A)
//
// which comes to four clauses when it is multiplied out.  The ROM tests
// only two of them, and one of those two uses the margin of the *wrong
// stroke*: `idxB >= marginA` where the symmetry plainly wants
// `idxB >= marginB`.  The effect is that two strokes which really do
// meet end to end are sometimes called non-tail-linked - most often
// when one stroke is much longer than the other, since that is when
// the two margins differ most.  Ported as it stands.
Boolean
SegmentNonTailLinked(const SegmentDistance* d)
{
	if (d == nil || d->fStrokeB == nil || d->fStrokeA == nil)
		return false;
	long countB = d->fStrokeB->fCount;
	long countA = d->fStrokeA->fCount;
	if (countB == 0 || countA == 0)
		return false;
	if (d->fDistance > RosCI->fLinkDistance)
		return false;

	long marginB = (short) ((int) ((unsigned int) RosCI->fEndFraction * (unsigned int) countB) >> 16);
	long marginA = (short) ((int) ((unsigned int) RosCI->fEndFraction * (unsigned int) countA) >> 16);
	long endB = (short) (countB - marginB);
	long endA = (short) (countA - marginA);

	if (!(d->fIndexB <= endB || d->fIndexA >= marginA))
		return false;
	//                        vvvvvvv  the bug: marginB is meant
	if (!(d->fIndexB >= marginA || d->fIndexA <= endA))
		return false;
	return true;
}


#pragma mark -
/*--------------------------------------------------------------------
	One stroke against its neighbours.
--------------------------------------------------------------------*/

// The nearest-approach block for a stroke that has no neighbour to be
// measured against: further away than anything the caller will accept.
static void
SegmentDistanceNothing(SegmentDistance* d, Fixed size)
{
	d->fDistance = size;
	d->fDX = size;
	d->fDY = size;
	d->fStrokeB = nil;
	d->fStrokeA = nil;
	d->fIndexB = 0;
	d->fIndexA = 0;
	d->fEitherIsDot = 0;
}


// ROM 0x001d18a4 SegmentMultiStrokeMinDistance
// How near this stroke comes to any of the **three** strokes before it.
//
// Three, and not one, because a letter is often written in pieces that
// are not consecutive - the bar of a t and the dot of an i are usually
// put in after the rest of the word - so the stroke that belongs with
// this one may be two or three back.  `prev` keeps the plain
// immediately-before answer as well, because the caller wants both.
//
// There is one case that looks the other way.  If the *next* stroke
// starts further left than this one does, the writer has gone back to
// add something, and the pair worth measuring is the one before this
// against that next one rather than against this.
void
SegmentMultiStrokeMinDistance(short index, RosStroke* const* strokes, Fixed size,
						short count, SegmentDistance* out, SegmentDistance* prev)
{
	short next = (short) (index + 1);
	short back1 = (short) (index - 1);
	short back2 = (short) (back1 - 1);
	short back3 = (short) (back2 - 1);

	if (back1 < 0 || count <= index)
	{
		Fixed nothing = size + 0x00010000;
		SegmentDistanceNothing(out, nothing);
		SegmentDistanceNothing(prev, nothing);
		return;
	}

	SegmentStrokeMinDistance(strokes[back1], strokes[index], out);
	*prev = *out;

	if (next < count)
	{
		FRect nextBounds, myBounds;
		StrokeFindBounds(strokes[next], &nextBounds);
		StrokeFindBounds(strokes[index], &myBounds);
		// the writer went back to add something
		if (nextBounds.left < myBounds.left && back1 >= 0)
		{
			SegmentDistance d;
			SegmentStrokeMinDistance(strokes[back1], strokes[next], &d);
			if (d.fDistance < out->fDistance)
				*out = d;
		}
	}
	if (back2 >= 0)
	{
		SegmentDistance d;
		SegmentStrokeMinDistance(strokes[back2], strokes[index], &d);
		if (d.fDistance < out->fDistance)
			*out = d;
	}
	if (back3 >= 0)
	{
		SegmentDistance d;
		SegmentStrokeMinDistance(strokes[back3], strokes[index], &d);
		if (d.fDistance < out->fDistance)
			*out = d;
	}
}


// ROM 0x001d1aa0 SegmentMultiStrokeMinDistBoundX
// The same question asked of the boxes rather than the points: how
// little space there is between this stroke's left edge and the right
// edge of any of the three before it.  A negative answer means they
// overlap along the line.
Fixed
SegmentMultiStrokeMinDistBoundX(short index, RosStroke* const* strokes, Fixed size,
						short count)
{
	short next = (short) (index + 1);
	short back1 = (short) (index - 1);
	short back2 = (short) (back1 - 1);
	short back3 = (short) (back2 - 1);

	FRect myBounds;
	StrokeFindBounds(strokes[index], &myBounds);

	Fixed gap;
	if (back1 < 0 || count <= index)
		gap = size + 0x00010000;
	else
	{
		FRect prevBounds;
		StrokeFindBounds(strokes[back1], &prevBounds);
		gap = myBounds.left - prevBounds.right;
	}

	if (next < count)
	{
		FRect nextBounds;
		StrokeFindBounds(strokes[next], &nextBounds);
		if (nextBounds.left < myBounds.left && back1 >= 0)
		{
			// (the ROM measures against the previous stroke's right
			//  edge, which it still has in hand from just above)
			FRect prevBounds;
			StrokeFindBounds(strokes[back1], &prevBounds);
			if (nextBounds.left - prevBounds.right < gap)
				gap = nextBounds.left - prevBounds.right;
		}
	}
	if (back2 >= 0)
	{
		FRect bounds;
		StrokeFindBounds(strokes[back2], &bounds);
		if (myBounds.left - bounds.right < gap)
			gap = myBounds.left - bounds.right;
	}
	if (back3 >= 0)
	{
		FRect bounds;
		StrokeFindBounds(strokes[back3], &bounds);
		if (myBounds.left - bounds.right < gap)
			gap = myBounds.left - bounds.right;
	}
	return gap;
}


// ROM 0x001d1e98 SegmentStroke
// One stroke measured against its neighbours.  This is the first half
// of the cutting: it decides, for every stroke in turn, how much of
// the line it shares with the stroke before it, whether the two are
// part of one letter, and whether a cut may go in front of it.
//
// **Are they one letter?**  Three thresholds, and the overlap has to
// beat one of them: seven tenths on its own (`fLinkOverlap`), or
// sixty-five hundredths if the two strokes actually *cross*
// (`fCrossOverlap`), or 0.675 if they are joined somewhere other than
// end to end (`fJoinOverlap`).  So the more the strokes are entangled
// the less they need to overlap - which is how a t is one letter while
// two letters that merely lean on each other are two.  Neither stroke
// may be a dot, and they must come within `reach` of each other.
//
// A link is recorded on **both** strokes: 3 on this one, and on the
// one before it 1 if it is the start of a run of linked strokes or 2
// if it is already in the middle of one.
//
// **May a cut go here?**  Only in front of a stroke that is not itself
// linked backwards, and then either because there is a plain gap - the
// horizontal space to the three strokes before is more than a letter's
// width, twice that if a dot is involved - or because the strokes are
// far enough apart *and* lie side by side rather than one above the
// other (both nearest approaches wider than they are tall) *and* the
// overlap is no more than a half.
void
SegmentStroke(short index, short count, RosStroke* const* strokes,
			Fixed size, Fixed reach, UByte how,
			short* breaks, short* breakCount)
{
	if (index < 0)
		return;
	RosStroke* cur = strokes[index];

	if (how != 0)
	{
		// the simple way: the overlap and nothing else
		Fixed overlap = 0;
		cur->fLink = 0;
		cur->fField2a = 0;
		if (index > 0)
		{
			RosStroke* prev = strokes[index - 1];
			if (cur->fIsDot == 0 && prev->fIsDot == 0)
			{
				FRect prevBounds, myBounds;
				StrokeFindBounds(prev, &prevBounds);
				StrokeFindBounds(cur, &myBounds);
				overlap = SegmentOverlap(&prevBounds, &myBounds);
			}
		}
		cur->fOverlap = overlap;
		return;
	}

	SegmentDistance dist;
	dist.fDistance = -0x00010000;
	Fixed overlap = 0;
	Boolean linked = false;

	if (index > 0)
	{
		RosStroke* prev = strokes[index - 1];
		FRect prevBounds, myBounds;
		StrokeFindBounds(prev, &prevBounds);
		StrokeFindBounds(cur, &myBounds);
		overlap = SegmentOverlap(&prevBounds, &myBounds);

		if (cur->fIsDot == 0 && prev->fIsDot == 0)
		{
			// the least of the three thresholds: below it none of them
			// can be beaten, so there is nothing to ask
			Fixed least = RosCI->fLinkOverlap;
			Fixed pair = (RosCI->fCrossOverlap < RosCI->fJoinOverlap)
						? RosCI->fCrossOverlap : RosCI->fJoinOverlap;
			if (pair <= least)
				least = pair;

			if (least < overlap)
			{
				SegmentStrokeMinDistance(prev, cur, &dist);
				if (dist.fDistance <= reach
					&& (RosCI->fLinkOverlap < overlap
						|| (RosCI->fCrossOverlap < overlap && SegmentCrossed(&dist))
						|| (RosCI->fJoinOverlap < overlap && SegmentNonTailLinked(&dist))))
					linked = true;
			}
		}
	}
	cur->fOverlap = overlap;

	if (!linked)
	{
		cur->fLink = 0;
		cur->fField2a = 0;
	}
	else
	{
		cur->fLink = 3;
		cur->fField2a = 0;
		RosStroke* prev = strokes[index - 1];
		// the link word is the two bytes together, so a stroke that is
		// already 1 or 2 is left as it is
		short was = (short) (((short) prev->fField2a << 8) | prev->fLink);
		if (was == 0)
			prev->fLink = 1;
		else if (was == 3)
			prev->fLink = 2;
		else
			return;
		prev->fField2a = 0;
	}

	// a stroke that is linked backwards has no cut in front of it
	if ((((short) cur->fField2a << 8) | cur->fLink) != 0 || index < 1)
		return;

	RosStroke* prev = strokes[index - 1];
	Boolean eitherIsDot = !(cur->fIsDot == 0 && prev->fIsDot == 0);
	Fixed gap = SegmentMultiStrokeMinDistBoundX(index, strokes, size, count);
	Fixed room = eitherIsDot ? FixedMultiply(size, 0x00020000) : size;

	if (gap <= room)
	{
		// no plain gap, so ask the harder question
		SegmentDistance other;
		if (dist.fDistance < 0 || size < dist.fDistance)
			SegmentMultiStrokeMinDistance(index, strokes, size, count, &dist, &other);
		// (`other` is read below only on this path.  When the call is
		//  skipped, `dist.fDistance` is between nought and `size` and
		//  the next test always returns, so it is never read unset.)
		Fixed allow = (dist.fEitherIsDot != 0)
					? FixedMultiply(size, 0x00020000) : size;
		if (allow >= dist.fDistance)
			return;
		// side by side rather than one above the other, both ways of
		// measuring it
		if (!(dist.fDX > dist.fDY && other.fDX > other.fDY))
			return;
		if (RosCI->fBreakOverlap < overlap)
			return;
	}

	breaks[*breakCount] = index;
	*breakCount = (short) (*breakCount + 1);
}


#pragma mark -
/*--------------------------------------------------------------------
	The cutting itself: `SegmentChars` runs `SegmentStroke` over every
	stroke and then `SegmentMakeSegments` over the break candidates it
	left, which is where the cuts are actually made.
--------------------------------------------------------------------*/

// The nominal height the engine measures everything against when it
// has nothing better - the same 18.85 pixels the word recogniser
// starts its run of Gaussians at (`WordRecog.cpp`'s `kNominalHeight`).
static const Fixed	kSegNominalHeight	= 0x0012d999;
// ... and the most the width worked out from it may be stretched.
static const Fixed	kSegWidthLimit		= 0x00028000;		// two and a half


// A fraction of the writing's height, rounded to the nearest pixel,
// but never more than the same fraction of the nominal height stretched
// by two and a half.  Writing much larger than the engine expects
// therefore stops getting proportionally looser.
static Fixed
SegSizeFromHeight(Fixed fraction, Fixed height)
{
	Fixed fromWriting = FixedMultiply(fraction, height) + 0x7fff;
	Fixed limit = FixedMultiply(FixedMultiply(fraction, kSegNominalHeight) + 0x7fff,
							kSegWidthLimit);
	return (limit < fromWriting) ? limit : fromWriting;
}


// ROM 0x001d48a4 SegmentChars
// The strokes of a word cut into characters.
//
// Two widths come out of the writing's height first: how wide a letter
// is taken to be (half the height, never under four pixels) and how
// near two strokes must come to count as touching (a tenth of it,
// never under two).  Then every stroke is measured against its
// neighbours in turn, which leaves a list of the places a cut may go,
// and `SegmentMakeSegments` walks that list and makes the segments.
//
// The height itself is not believed if it is too small: less than half
// of the least stroke size plus the nominal height, and the nominal is
// used instead.
short
SegmentChars(short count, RosStroke** strokes, Fixed meanSize,
			RosSegment** segments, UByte how, void* net)
{
	short* breaks = (short*) RosAllocate(count * (long) sizeof(short));
	short made = 0;
	short breakCount = 0;
	newton_try
	{
		Fixed least = (RosCI->fMinStrokeSize + kSegNominalHeight) >> 1;
		if (meanSize < least)
			meanSize = least;

		// how wide a letter is
		Fixed size = SegSizeFromHeight(RosCI->fCharWidthFraction, meanSize);
		if (size < RosCI->fMinCharWidth)
			size = RosCI->fMinCharWidth;
		// ... and how near two strokes must come to be touching
		Fixed reach = SegSizeFromHeight(RosCI->fReachFraction, meanSize);
		if (reach <= 0x00020000)
			reach = 0x00020000;

		for (short i = 0; i < count; i++)
			SegmentStroke(i, count, strokes, size, reach, how, breaks, &breakCount);
		for (short i = 0; i < count; i++)
			SegmentMakeSegments(i, count, strokes, breaks, breakCount, segments,
							0, how, net);
		made = SegmentMakeSegments((short) (count - 1), count, strokes, breaks,
							breakCount, segments, 1, how, net);
	}
	cleanup
	{
		DisposPtr((Ptr) breaks);
	}
	end_try;
	DisposPtr((Ptr) breaks);
	return made;
}


// How many segments there is room for.  The same number as
// `kWordRecogMaxSegments`, which is the array this writes into.
const long	kSegMaxSegments		= 900;
// How many strokes a piece of writing may have before it is cut
// whatever else the strokes say.  One fewer when the engine has not
// been told to fragment ligatures.
const long	kSegMaxStrokes		= 6;


// The link word is the byte at +0x2b with the byte at +0x2a above it.
// Everything writes nought into the upper one, so the word is the link
// - but the ROM reads it as a halfword, and so does this.
static short
StrokeLink(const RosStroke* stroke)
{
	return (short) (((short) stroke->fField2a << 8) | stroke->fLink);
}


// The state block, made on the first call and given back by
// `SegmentQuiesce`.
static void
SegStateReset(SegState* st)
{
	st->fStart = 0;
	st->fBoundsTo = -1;
	st->fCut = -1;
	st->fLastCut = -1;
	st->fBreakAt = 0;
	st->fMade = 0;
}


// One grouping of `n` strokes starting at `first` made into a segment.
static void
SegEmit(SegState* st, RosStroke* const* strokes, long first, long n, long skipped,
		RosSegment** segments, void* net)
{
	if (st->fMade >= kSegMaxSegments)
		return;
	newton_try
	{
		RosSegment* seg = SegmentCreate();
		segments[st->fMade] = seg;
		seg->fFirstStroke = (short) first;
		seg->fCount = (short) n;
		seg->fRealCount = (short) (n - skipped);
		seg->fSeparation = strokes[first]->fSeparation;
		// (the ROM hands `net` on as a fourth argument, which
		//  `SegmentSetStrokes` does not take; kept out here)
		(void) net;
		SegmentSetStrokes(seg, (short) n, &strokes[first]);
		SegmentBoundsDotsEtc(seg);
		SegmentSetStrokeOverlaps(seg, (st->fMade < 1) ? nil : segments[st->fMade - 1]);
		st->fMade++;
	}
	cleanup
	{
		for (long i = 0; i < st->fMade; i++)
			SegmentDestroy(segments[i]);
	}
	end_try;
}


// ROM 0x001d0f68 SegmentMakeSegments
// The second pass: the break candidates turned into segments.
//
// It is **incremental**.  `SegmentChars` calls it once per stroke and
// then once more with `last` set, and it keeps its working-out in
// `gSegState` - which is why `SegmentQuiesce` exists at all.  Each call
// folds the new stroke into the box of the piece being built, works out
// the new aspect ratio, and asks whether the piece should end here.
//
// **Three reasons it might.**  The first pass said so, and this
// stroke's index is the next entry in the break candidates.  Or the
// piece has got too wide: the aspect ratio has passed `fCutAspect`
// (one and a half), or `fCutAspectWithDot` (one and three quarters)
// when there is a dot somewhere in it, because a dot has already
// widened the box without being a letter of its own - and it must be
// still growing, the stroke must share less than half the line with
// the one before it, and it must not be a fragment.  Or the piece has
// too many strokes.
//
// A cut may not fall in the middle of a run of linked strokes, so both
// of the last two walk back to the nearest stroke whose link is 0 or 3.
// The too-many-strokes case has a fallback the aspect case does not: if
// there is no such stroke at all it cuts at the stroke before this one
// anyway and **rewrites the links** to make that legal - the engine
// admitting that a run it thought was one letter cannot be.
//
// **What it emits is not a partition.**  For a piece running from
// `fStart` to `fCut` it makes a segment of the first stroke, then of
// the first two, then of the first three, and so on - every grouping
// that the links allow, each starting where the last one did.  The
// layer above is given a lattice of candidate letters and scores them;
// it is not told where the letters are.
short
SegmentMakeSegments(short index, short count, RosStroke* const* strokes,
				const short* breaks, short breakCount, RosSegment** segments,
				long last, UByte how, void* net)
{
	if (index < 0)
		return 0;
	// one fewer stroke to a piece when ligatures are not fragmented
	long maxStrokes = kSegMaxStrokes - ((FragmentLigatures == 0) ? 1 : 0);

	if (gSegState == nil)
	{
		gSegState = (SegState*) RosAllocate((long) sizeof(SegState));
		SegStateReset(gSegState);
	}
	SegState* st = gSegState;

	// 0: nothing decided yet.  1: the piece was cut short.  2: the
	// piece ends at `fCut`.
	long mode;
	if (last == 0)
	{
		if (index == 0)
			SegStateReset(st);

		if (st->fBoundsTo < st->fStart)
		{
			// start the box afresh from the first stroke of the piece
			StrokeFindBounds(strokes[st->fStart], &st->fBounds);
			st->fHasDot = strokes[st->fStart]->fIsDot;
			for (long i = st->fStart + 1; i < index; i++)
			{
				StrokeFindBounds(strokes[i], &st->fScratch);
				OrFixedRect(&st->fBounds, &st->fScratch);
				st->fHasDot |= strokes[i]->fIsDot;
			}
			st->fBoundsTo = st->fStart;
			st->fPrevAspect = 0;
			st->fAspect = SegmentAspect(&st->fBounds);
		}
		if (st->fStart < index)
		{
			// ... and take this stroke into it
			StrokeFindBounds(strokes[index], &st->fScratch);
			OrFixedRect(&st->fBounds, &st->fScratch);
			st->fHasDot |= strokes[index]->fIsDot;
			st->fPrevAspect = st->fAspect;
			st->fAspect = SegmentAspect(&st->fBounds);
		}
		mode = 0;
		st->fCut = st->fLastCut;
	}
	else
	{
		st->fCut = index;
		mode = 2;
	}

	RosStroke* cur = strokes[index];
	ULong how24 = cur->fField24;
	long end;

	// `goto` here because the ROM's three ways of deciding where the
	// piece ends fall through into each other, and writing it any other
	// way would move the order they are tried in.
	if (mode != 0)
		goto haveCut;

	if (how24 < 2)
	{
		if (how24 != 0 || how != 0)
			goto tooMany;

		long at = st->fBreakAt;
		if (at >= breakCount || breaks[at] != index)
		{
			// the first pass did not mark this one, so the only thing
			// that can end the piece here is its shape
			if (how24 != 0 || how != 0
				|| index <= st->fLastCut + 1
				|| (st->fAspect <= RosCI->fCutAspectWithDot
					&& (st->fAspect <= RosCI->fCutAspect || st->fHasDot != 0))
				|| st->fAspect <= st->fPrevAspect
				|| cur->fOverlap > 0x7fff
				|| cur->fFragment != 0)
				goto tooMany;

			if (StrokeLink(cur) < 2)
				st->fCut = index - 1;
			else
			{
				// back to a stroke the cut may legally fall on
				for (long i = index - 2; i >= st->fStart; i--)
				{
					short link = StrokeLink(strokes[i]);
					if (link == 0 || link == 3)
					{
						st->fCut = i;
						break;
					}
				}
				// nothing back there, or it is behind the last cut:
				// fall through to the forced cut instead
				if (st->fCut <= st->fLastCut)
					goto tooMany;
			}
			mode = 1;
			end = st->fStart;
			goto emit;
		}
		st->fBreakAt = at + 1;
	}
	// the first pass marked this stroke, or the writing is boxed
	st->fCut = index - 1;
	mode = 2;
	goto haveCut;

tooMany:
	if ((index - st->fStart) + 1 <= maxStrokes)
		return (short) st->fMade;			// nothing to do yet

	if (StrokeLink(cur) < 2)
		st->fCut = index - 1;
	else
	{
		st->fCut = -1;
		for (long i = index - 2; i >= st->fStart; i--)
		{
			short link = StrokeLink(strokes[i]);
			if (link == 0 || link == 3)
			{
				st->fCut = i;
				break;
			}
		}
		if (st->fCut == -1)
		{
			// Every stroke of the run is linked and there is nowhere
			// legal to cut - but the piece is too long to keep.  So the
			// engine cuts anyway and rewrites the links to suit: the
			// stroke before this one becomes the end of a run, and this
			// one becomes the start of the next (or, if it was already
			// linked backwards, the start of nothing).
			st->fCut = index - 1;
			RosStroke* prev = strokes[index - 1];
			prev->fLink = 3;
			prev->fField2a = 0;
			cur->fLink = (UByte) ((StrokeLink(cur) == 3) ? 0 : 1);
			cur->fField2a = 0;
		}
	}
	mode = 1;
	end = st->fStart;
	goto emit;

haveCut:
	end = st->fCut;

emit:
	{
		// (the ROM carries two flags through the loop.  `started` says a
		//  run of linked strokes has already been given a grouping of
		//  its own, and `seen` is what the *next* stroke of the run
		//  reads; a stroke that is linked backwards, link 3, clears it
		//  again.  What they come to is that only the first stroke of a
		//  linked run may start groupings.)
		ULong started = 0;
		ULong seen = 0;
		for (long i = st->fStart; i <= end; i++)
		{
			short link = StrokeLink(strokes[i]);
			Boolean make = (link == 0);
			if (!make)
			{
				if (link == 1)
					started = seen;
				if (link == 1 && started == 0)
				{
					if (how24 == 0)
					{
						started = 1;
						seen = 1;
					}
					make = true;
				}
				else if (how24 != 0)
				{
					// boxed writing gives every stroke a grouping,
					// linked or not; the flags are left alone because
					// the ROM only sets them when `how24` is nought
					started = how24;
					make = true;
				}
			}

			if (make)
			{
				// every grouping the links allow, each starting here:
				// one stroke, then two, then three ...
				long span = (st->fCut - i) + 1;
				long skipped = 0;
				for (long n = 1; n <= span; n++)
				{
					Boolean take;
					if (how24 == 0)
					{
						short at = StrokeLink(strokes[i + n - 1]);
						// a grouping may not end in the middle of a run
						take = !(at > 0 && at < 3);
					}
					else
					{
						// boxed writing: one grouping, the whole piece
						long lim = (i == st->fStart) ? span : st->fStart;
						take = (i == st->fStart && n == lim);
					}
					if (take)
						SegEmit(st, strokes, i, n, skipped, segments, net);
					else
						skipped++;
				}
			}

			if (StrokeLink(strokes[i]) == 3)
				seen = 0;
			strokes[i]->fSegment = (short) (st->fMade - 1);
		}
	}

	// on to the next piece
	st->fStart = ((mode == 2) ? st->fCut : st->fStart) + 1;
	if (last == 0 && StrokeLink(strokes[st->fStart]) > 1)
	{
		// the next piece may not start in the middle of a linked run
		for (long i = st->fStart; i < index; i++)
		{
			RosStroke* stroke = strokes[i];
			if (StrokeLink(stroke) < 2)
			{
				st->fStart = i;
				break;
			}
			if (i < st->fCut + 1)
				stroke->fSegment = (short) (st->fMade - 1);
		}
		if (StrokeLink(strokes[st->fStart]) > 1)
		{
			// still in one: start after the cut and make that stroke
			// the head of a run, or of nothing
			st->fStart = st->fCut + 1;
			RosStroke* head = strokes[st->fStart];
			head->fLink = (UByte) ((st->fCut + 2 < count
								&& StrokeLink(strokes[st->fCut + 2]) > 1) ? 1 : 0);
			head->fField2a = 0;
		}
	}
	st->fLastCut = st->fCut;
	return (short) st->fMade;
}


#pragma mark -
/*--------------------------------------------------------------------
	Where one word ends and the next begins.
--------------------------------------------------------------------*/

// ROM 0x0c100890 xpsvx
// A number `SegmentWordXGap` works out about the gap and writes here
// for the debugger.  Nothing reads it; the probability it is made
// from is dropped on the floor with it.
Fixed	xpsvx = 0;

// ROM 0x0c10089c abs_temp
// Where `WRSegWordXGap` writes how far a short stroke's middle was from
// the running middle before it takes the absolute value, for the
// debugger.  Nothing reads it.
Fixed	abs_temp = 0;

// ROM 0x0c104f9c SegOnly
// The engine has been told to group the writing but not to read it,
// which makes the word break its whole answer and so worth a
// threshold of its own.  `WordRecogAddStroke` copies it out of the
// word recogniser's `fClassifyMode`.
ULong	SegOnly = 0;

// ROM 0x0c101adc (unnamed)
Fixed	gSegSizeRatio = 0;


// How big the writing has turned out, against the running mean of the
// writer's stroke sizes.  Every threshold in the three tests below is
// scaled by a function of this, so all of it is measured in the
// writer's own units rather than in pixels.  The ROM leaves the ratio
// in a global of its own, which nothing reads.
static Fixed
SegWordSizeRatio(Fixed inkSize, Fixed refSize, Fixed startSize, const Fixed* run)
{
	Fixed size = (inkSize < refSize) ? refSize : inkSize;
	if (size <= startSize)
		size = startSize;
	gSegSizeRatio = FixedDivide((size + startSize) >> 1, run[0]);
	return gSegSizeRatio;
}


// ROM 0x001d27d4 SegmentWordBack
// The writer went back.
//
// The new stroke may start to the left of where the reference starts -
// the dot of an `i` and the bar of a `t` are written after the letter
// and well behind it - but only by so much: 2.7 stroke sizes or
// fifteen pixels, whichever is more, scaled by how big this writing
// has turned out.  Further back than that and the pen has gone back to
// begin something else.
Boolean
SegmentWordBack(const SegWordInk* ink, const SegWordRef* ref, Fixed startSize,
				Fixed /*wordSize*/, const Fixed* run, Fixed* strength)
{
	Fixed allow = FixedMultiply(RosCI->fBackGapStrokes, run[0]);
	if (RosCI->fMinBackGap >= allow)
		allow = RosCI->fMinBackGap;

	// ... scaled by a gentle function of the writing's size: twice the
	// ratio while the writing is small, one while it is between a half
	// and twice the mean, and the ratio less one above that - averaged
	// with the ratio's square root, which softens all three
	Fixed ratio = SegWordSizeRatio(ink->fSizeMax, ref->fInk.fSizeMax, startSize, run);
	Fixed lean;
	if (ratio < 0x00008000)
		lean = FixedDivide(ratio, 0x00008000);
	else if (ratio < 0x00020001)
		lean = 0x00010000;
	else
		lean = ratio - 0x00010000;
	allow = FixedMultiply((lean + FixedSqrt(ratio)) >> 1, allow);

	Boolean same = (ref->fInk.fLeft - ink->fRight) <= allow;
	*strength = same ? 0 : 0x00010000;
	return !same;
}


// ROM 0x001d2900 SegmentWordVert
// The writer went down a line.
//
// Three things are measured against the reference - the middle of the
// ink, the top and the bottom - and all three must have moved out of
// the band allowed for them before the pen is said to have gone
// somewhere else.  The bands come out of the word's own size, with a
// floor of four or five pixels, and are then worked over four times:
//
// * small writing loosens the downward bands, by up to two and a half
//   times, because a small stroke's box says less about where it sits;
//   and when the reference is a *single* stroke the upward bands are
//   loosened with them;
// * a pen that has moved left of the reference tightens them, twice
//   over and further the further it went, because moving left and
//   moving down together is what starting a line looks like;
// * a pen still inside the reference's own span loosens everything by
//   a quarter again;
// * and a pen just short of that span is judged by whether it is also
//   clear of the reference vertically.
Boolean
SegmentWordVert(const SegWordInk* ink, const SegWordRef* ref, Fixed startSize,
				Fixed wordSize, const Fixed* run, Fixed* strength)
{
	Fixed bigger = ink->fSizeMax;
	Fixed smaller = ref->fInk.fSizeMax;
	if (ink->fSizeMax < ref->fInk.fSizeMax)
	{
		bigger = ref->fInk.fSizeMax;
		smaller = ink->fSizeMax;
	}

	// the six bands, out of the word's size and never smaller than
	// four or five pixels
	Fixed upMiddle = FixedMultiply(0x00008000, wordSize);
	if (upMiddle < 0x00040001)
		upMiddle = 0x00040000;
	Fixed upTop = FixedMultiply(0x00008666, wordSize);
	if (upTop < 0x00040001)
		upTop = 0x00040000;
	Fixed upBottom = FixedMultiply(0x00008000, wordSize);
	if (upBottom < 0x00040001)
		upBottom = 0x00040000;
	Fixed downMiddle = FixedMultiply(0x0000b333, wordSize);
	if (downMiddle < 0x00050001)
		downMiddle = 0x00050000;
	Fixed downTop = FixedMultiply(0x0000c000, wordSize);
	if (downTop < 0x00050001)
		downTop = 0x00050000;
	Fixed downBottom = FixedMultiply(0x0000b333, wordSize);
	if (downBottom < 0x00050001)
		downBottom = 0x00050000;

	// scaled by the writing's size - by its square root while it is
	// smaller than the mean, and by the ratio itself above it
	Fixed ratio = SegWordSizeRatio(bigger, startSize, startSize, run);
	Fixed lean = ratio;
	if (ratio < 0x00010001)
		lean = FixedSqrt(ratio);
	upMiddle = FixedMultiply(lean, upMiddle);
	upTop = FixedMultiply(lean, upTop);
	upBottom = FixedMultiply(lean, upBottom);
	downMiddle = FixedMultiply(lean, downMiddle);
	downTop = FixedMultiply(lean, downTop);
	downBottom = FixedMultiply(lean, downBottom);

	// how big a stroke of this writing ought to be.  (The ROM works
	// the ratio out a second time here and does not record it, which
	// is the same number either way.)
	Fixed again = (bigger < startSize) ? startSize : bigger;
	Fixed nominal = FixedMultiply(
					FixedSqrt(FixedDivide((again + startSize) >> 1, run[0])), run[0]);
	Fixed half = FixedMultiply(0x00008000, nominal);
	Fixed floor = FixedMultiply(0x00018000, RosCI->fMinStrokeSize);
	if (half < floor)
		half = floor;

	// small writing: the downward bands are loosened, up to two and a
	// half times
	Fixed loosen;
	if (smaller < half)
	{
		if (RosCI->fMinStrokeSize < smaller)
			loosen = FixedMultiply(FixedDivide(half - smaller,
							half - RosCI->fMinStrokeSize), 0x00018000) + 0x00010000;
		else
			loosen = 0x00028000;
	}
	else
		loosen = 0x00010000;
	downMiddle = FixedMultiply(loosen, downMiddle);
	downTop = FixedMultiply(loosen, downTop);
	downBottom = FixedMultiply(loosen, downBottom);
	if (ref->fStrokes == 1)
	{
		// the reference is one stroke, which says very little about
		// where the writing sits: loosen the upward bands too
		upMiddle = FixedMultiply(loosen, upMiddle);
		upTop = FixedMultiply(loosen, upTop);
		upBottom = FixedMultiply(loosen, upBottom);
	}

	// the pen has moved left of the reference: the upward bands
	// tighten, and further the further it went
	Fixed back = FixedMultiply((Fixed) 0xffff0000, nominal);
	Boolean tall = (ref->fBodyBottom < ink->fBottom)
				|| ((RosCI->fMinStrokeSize + half) >> 1 < ink->fSizeMax);
	if (ink->fCentroidX < ref->fInk.fRight + back && tall)
	{
		Fixed far = FixedMultiply(0x00018000, nominal);
		Fixed tighten, tightenMiddle;
		if (ref->fInk.fRight - far < ink->fCentroidX)
		{
			Fixed how = (ref->fInk.fRight + back) - ink->fCentroidX;
			tighten = FixedMultiply(FixedDivide(how, back + far),
							(Fixed) 0xffff4000) + 0x00010000;
			tightenMiddle = FixedMultiply(FixedDivide(how, back + far),
							(Fixed) 0xffff8ccc) + 0x00010000;
		}
		else
		{
			tighten = 0x00004000;
			tightenMiddle = 0x00008ccc;
		}
		upBottom = FixedMultiply(tighten, upBottom);
		upTop = FixedMultiply(tighten, upTop);
		upMiddle = FixedMultiply(tightenMiddle, upMiddle);
	}

	// ... and further left still, the downward bands with it
	back = FixedMultiply((Fixed) 0xfffe7334, nominal);
	if (ink->fCentroidX < ref->fInk.fRight + back && tall)
	{
		Fixed far = FixedMultiply(0x00028000, nominal);
		Fixed tighten, tightenMiddle;
		if (ref->fInk.fRight - far < ink->fCentroidX)
		{
			Fixed how = (ref->fInk.fRight + back) - ink->fCentroidX;
			tighten = FixedMultiply(FixedDivide(how, back + far),
							(Fixed) 0xffff8000) + 0x00010000;
			tightenMiddle = FixedMultiply(FixedDivide(how, back + far),
							(Fixed) 0xffffd999) + 0x00010000;
		}
		else
		{
			tighten = 0x00008000;
			tightenMiddle = 0x0000d999;
		}
		downBottom = FixedMultiply(tighten, downBottom);
		downTop = FixedMultiply(tighten, downTop);
		downMiddle = FixedMultiply(tightenMiddle, downMiddle);
	}

	// the pen is still inside the reference's own span, which two of
	// the running measurements say how far reaches: everything loosens
	// by a quarter again
	Fixed reach = run[21];
	Fixed span = run[2] + ref->fInk.fRight + reach * 2;
	Fixed spanMiddle = run[6] + ref->fInk.fCentroidX + reach * 2;
	if (spanMiddle <= span)
		spanMiddle = span;
	if (ref->fInk.fRight - (reach >> 3) <= ink->fCentroidX && ink->fCentroidX <= spanMiddle)
	{
		upBottom = FixedMultiply(0x00014666, upBottom);
		downBottom = FixedMultiply(0x00014666, downBottom);
		upTop = FixedMultiply(0x00014666, upTop);
		downTop = FixedMultiply(0x00014666, downTop);
		upMiddle = FixedMultiply(0x00014666, upMiddle);
		downMiddle = FixedMultiply(0x00014666, downMiddle);
	}

	// ... and if it is just short of it, whether it is clear of the
	// reference vertically as well
	Fixed clear = FixedMultiply(0x0000547a, run[20]);
	if (ink->fCentroidX >= ref->fInk.fRight - reach
		&& ink->fCentroidX <= ref->fInk.fRight - (reach >> 3))
	{
		Fixed spread = 0x00018000;
		Boolean above = ink->fCentroidY < ref->fInk.fCentroidY;
		if (above || clear <= ink->fTop - ref->fInk.fBottom)
		{
			Fixed test = above ? ((ink->fBottom - ref->fInk.fTop) - clear)
						: (ink->fCentroidY - ref->fInk.fCentroidY);
			if (test < 0)
				spread = FixedMultiply(0x00018000, 0x00018000);
		}
		else
			spread = FixedMultiply(0x00018000, 0x00018000);
		upBottom = FixedMultiply(spread, upBottom);
		downBottom = FixedMultiply(spread, downBottom);
		upTop = FixedMultiply(spread, upTop);
		downTop = FixedMultiply(spread, downTop);
		upMiddle = FixedMultiply(spread, upMiddle);
		downMiddle = FixedMultiply(spread, downMiddle);
	}

	// The three measurements.  The top and the bottom are taken
	// against three parts of the body band to one of the whole box,
	// which is the same number when the reference is one stroke - the
	// caller passes its top and bottom twice - and the body's when it
	// is a word.
	Fixed top = ink->fTop - ((ref->fBodyTop * 3 + ref->fInk.fTop) >> 2);
	Fixed middle = ink->fCentroidY - ref->fInk.fCentroidY;
	Fixed bottom = ink->fBottom - ((ref->fBodyBottom * 3 + ref->fInk.fBottom) >> 2);

	// all three outside their bands, or it is the same word
	if ((middle < -downMiddle || middle > upMiddle)
		&& (top < -downTop || top > upTop)
		&& (bottom < -downBottom || bottom > upBottom))
	{
		*strength = 0x00010000;
		return true;
	}
	*strength = 0;
	return false;
}




// ROM 0x001d26e8 SegmentWordBkVt
// The first two tests without the third, which is what the engine asks
// when the stroke has already been taken in and only the question of
// whether the *word* ended is left.
long
SegmentWordBkVt(const SegWordInk* ink, const SegWordRef* ref, Fixed startSize,
				Fixed wordSize, const Fixed* run, Fixed* strength)
{
	if (SegmentWordBack(ink, ref, startSize, wordSize, run, strength))
		return kSegWordWentBack;
	if (SegmentWordVert(ink, ref, startSize, wordSize, run, strength))
		return kSegWordWentDown;
	return kSegWordSame;
}


// ROM 0x001d259c SegmentWord
// Whether this stroke begins a new word, and why.  The three tests are
// asked in order and the first that answers wins, so a pen that went
// back is reported as having gone back even if it also went down.
long
SegmentWord(const SegWordInk* ink, const SegWordRef* ref, Fixed startSize,
				Fixed wordSize, const Fixed* run, Fixed* strength)
{
	if (SegmentWordBack(ink, ref, startSize, wordSize, run, strength))
		return kSegWordWentBack;
	if (SegmentWordVert(ink, ref, startSize, wordSize, run, strength))
		return kSegWordWentDown;
	return SegmentWordXGap(ink, ref, startSize, wordSize, run, strength)
			? kSegWordWideGap : kSegWordSame;
}


// The nominal pair for each of the eight gap Gaussians `fRun` keeps -
// what a writer of ordinary habits does - which each measurement is
// pooled towards and then held within a quarter and four times of.
// The first four are in pixels and the second four in stroke sizes;
// within each, the pairs are (within a word, between words) for the
// gap between the boxes and for the gap between the middles of the
// ink.
// ROM 0x001d3064 SegmentWordXGap (the constants are written into it)
const Fixed	kSegGapNominal[8][2] = {
	{ 0x00063851, 0x0003f333 },		// fRun[2]:  the box gap within a word
	{ 0x00171999, 0x000a7851 },		// fRun[4]:  ... and between words
	{ 0x000eb0a3, 0x000835c2 },		// fRun[6]:  the middle gap within a word
	{ 0x0022d1eb, 0x000b8000 },		// fRun[8]:  ... and between words
	{ 0x000057ce, 0x00003738 },		// fRun[10]: the box gap, in stroke sizes
	{ 0x00015212, 0x0000a09d },		// fRun[12]
	{ 0x0000c9db, 0x0000663f },		// fRun[14]: the middle gap, in stroke sizes
	{ 0x0001f5e3, 0x0000a24d },		// fRun[16]
};

// The ratio between the two halves of each pair, which the ROM has as
// a constant where it has the other two as a division worked out on
// the spot.
static const Fixed	kSegGapRatio[4][2] = {
	{ 0x0003b851, 0x0002a666 },		// fRun[4] over fRun[2]
	{ 0x00025eb8, 0x00016666 },		// fRun[8] over fRun[6]
	{ 0x0003d9ad, 0x0002e89a },		// fRun[12] over fRun[10]
	{ 0x00027c7e, 0x0001965f },		// fRun[16] over fRun[14]
};


// One pooled estimate: what this writer's hand says, what a writer of
// ordinary habits does at this size, and what the other three
// measurements of the group say once they are rescaled to this one's
// nominal - averaged, and then held to between a quarter and four
// times the nominal.  `share` is a fifth for the first group, which
// takes all five terms, and a quarter for the second, where the ROM
// works the nominal term out and then leaves it out of the sum.
static Fixed
SegGapPool(Fixed own, Fixed nominal, Fixed sizeFactor, Fixed partner,
				Fixed cross1, Fixed cross2, Fixed share, Boolean useNominal)
{
	Fixed sum = own + partner + cross1 + cross2;
	if (useNominal)
		sum += FixedMultiply(nominal, sizeFactor);
	Fixed pooled = FixedMultiply(share, sum);
	Fixed low = FixedMultiply(0x00004000, nominal);
	if (pooled < low)
		return low;
	Fixed high = FixedMultiply(0x00040000, nominal);
	if (high < pooled)
		return high;
	return pooled;
}


// How likely it is that a gap this wide is a space rather than a join,
// given the two Gaussians it could have come from.  A gap smaller than
// the within-word mean is certainly a join and one wider than the
// between-words mean is certainly a space; in between, the difference
// of the two squared z-scores is the log-likelihood ratio, and the
// logistic curve turns that into a probability.  The writer's spacing
// setting comes in as `gSegLogWordSpacing`, which is why it is a
// logarithm at all - it is added here rather than multiplied in.
static Fixed
SegGapLikelihood(Fixed measured, Fixed withinMean, Fixed withinSigma,
				Fixed betweenMean, Fixed betweenSigma, Fixed bias,
				Fixed* squaredWithin, Fixed* squaredBetween)
{
	*squaredWithin = 0;
	*squaredBetween = 0;
	if (measured <= withinMean)
		return 0;
	if (measured >= betweenMean)
		return 0x00010000;
	Fixed z = FixedDivide(measured - withinMean, withinSigma);
	*squaredWithin = FixedMultiply(z, z);
	z = FixedDivide(measured - betweenMean, betweenSigma);
	*squaredBetween = FixedMultiply(z, z);
	return ArSigmoid(((*squaredWithin - *squaredBetween) >> 1)
					- gSegLogWordSpacing - bias);
}


// ROM 0x001d3064 SegmentWordXGap
// The gap before this stroke, measured against what this writer's own
// spaces look like.
//
// This is the third and largest of the three word-break tests, and it
// is a small piece of statistics.  `WordRecog::fRun` carries eight
// running Gaussians - kept as a mean and a mean of the square, so a
// standard deviation is one square root away - which are **four
// measurements in two situations**: how far apart two pieces of ink
// are within a word and between words, taken both between the boxes
// and between the middles of the ink, and each of those both in pixels
// and in stroke sizes.
//
// Each of the eight is pooled before it is used: with a nominal for a
// writer of ordinary habits, scaled by how big this writing is, and
// with the other three measurements of its group rescaled to its own
// nominal - because they are all measuring much the same thing and
// four noisy estimates of it are better than one.  The result is then
// held to between a quarter and four times its nominal, so that a few
// strange strokes cannot run the model away.
//
// The gap is then put to all four pairs.  Each answers the probability
// that this is a space, through the logistic of the difference of the
// two squared z-scores; the four are averaged, and if the average
// passes the threshold - `gSegIntegrated` normally, or
// `gSegOnlyThreshold` when the engine has been told only to group the
// writing - a new word begins.
Boolean
SegmentWordXGap(const SegWordInk* ink, const SegWordRef* ref, Fixed /*startSize*/,
				Fixed /*wordSize*/, const Fixed* run, Fixed* strength)
{
	// the standard deviations.  (The ROM works the first one out - the
	// stroke size's, `fRun[0]` and `fRun[1]` - and never uses it.)
	Fixed mean[8];
	Fixed sigma[8];
	FixedSqrt(run[1] - FixedMultiply(run[0], run[0]));
	for (long i = 0; i < 8; i++)
	{
		mean[i] = run[2 + i * 2];
		sigma[i] = FixedSqrt(run[3 + i * 2] - FixedMultiply(mean[i], mean[i]));
	}

	// the writer's spacing, as a logarithm, worked out the first time
	// it is wanted
	if (gSegLogWordSpacing == gSegWordSpacing)
		gSegLogWordSpacing = (Fixed) (long)
					(log((double) gSegWordSpacing / 65536.0) * 65536.0);

	// the two gaps, in pixels and in stroke sizes
	Fixed boxGap = ink->fLeft - ref->fInk.fRight;
	Fixed middleGap = ink->fCentroidX - ref->fInk.fCentroidX;
	Fixed boxGapScaled = FixedDivide(boxGap, run[0]);
	Fixed middleGapScaled = FixedDivide(middleGap, run[0]);

	// how big this writing is: halfway between one and the square root
	// of the stroke size against a nominal 1.178
	Fixed sizeFactor = (FixedSqrt(FixedDivide(run[0], 0x0012d999)) + 0x00010000) >> 1;

	// the eight pooled pairs.  The order the ROM works them out in is
	// the order of the decisions below, and each names its partner -
	// the same measurement in the other situation - by a constant
	// ratio, and the other two by a division worked out on the spot.
	Fixed pooledMean[8];
	Fixed pooledSigma[8];
	static const long kOther[8][2] = {
		{ 2, 3 }, { 3, 2 }, { 0, 1 }, { 1, 0 },		// the pixel group
		{ 6, 7 }, { 7, 6 }, { 4, 5 }, { 5, 4 },		// and the stroke-size group
	};
	for (long i = 0; i < 8; i++)
	{
		long partner = i ^ 1;
		Boolean first = (partner < i);		// this one is the wider of the pair
		const Fixed* ratio = kSegGapRatio[i >> 1];
		Fixed partnerMean, partnerSigma;
		if (first)
		{
			partnerMean = FixedMultiply(ratio[0], mean[partner]);
			partnerSigma = FixedMultiply(ratio[1], sigma[partner]);
		}
		else
		{
			partnerMean = FixedDivide(mean[partner], ratio[0]);
			partnerSigma = FixedDivide(sigma[partner], ratio[1]);
		}
		Fixed crossMean[2];
		Fixed crossSigma[2];
		for (long k = 0; k < 2; k++)
		{
			long other = kOther[i][k];
			crossMean[k] = FixedMultiply(mean[other],
						FixedDivide(kSegGapNominal[i][0], kSegGapNominal[other][0]));
			crossSigma[k] = FixedMultiply(sigma[other],
						FixedDivide(kSegGapNominal[i][1], kSegGapNominal[other][1]));
		}
		// ROM BUG: the stroke-size half works the nominal term out
		// and then leaves it out of the sum, dividing by four rather
		// than five - so those four estimates are pooled with no prior
		// at all.  The two multiplications are made and dropped.
		Boolean useNominal = (i < 4);
		Fixed share = useNominal ? 0x00003333 : 0x00004000;
		pooledMean[i] = SegGapPool(mean[i], kSegGapNominal[i][0], sizeFactor,
						partnerMean, crossMean[0], crossMean[1], share, useNominal);
		pooledSigma[i] = SegGapPool(sigma[i], kSegGapNominal[i][1], sizeFactor,
						partnerSigma, crossSigma[0], crossSigma[1], share, useNominal);
	}

	// the four questions
	Fixed within, between;
	Fixed pBox = SegGapLikelihood(boxGap, pooledMean[0], pooledSigma[0],
					pooledMean[1], pooledSigma[1], 0x00010000, &within, &between);
	// (the ROM turns the same ratio into a score and a probability
	//  here, writes the score into `xpsvx` and drops the rest)
	if (boxGap > pooledMean[0] && boxGap < pooledMean[1])
	{
		Fixed half = FixedMultiply(0x00008000, between - within);
		xpsvx = (half * -500) >> 16;
		Fixed dead = (xpsvx >= kArProbMaxScore) ? 0
					: (xpsvx < 1 ? 0x00010000 : ArProbDecodeLu[(half * -500) >> 19]);
		dead = FixedMultiply(FixedMultiply(gSegWordSpacing, 0x00046dab), dead);
		FixedDivide(0x00010000, dead + 0x00010000);
	}

	Fixed pMiddle = SegGapLikelihood(middleGap, pooledMean[2], pooledSigma[2],
					pooledMean[3], pooledSigma[3], 0x00013333, &within, &between);
	if (middleGap > pooledMean[2] && middleGap < pooledMean[3])
	{
		Fixed half = FixedMultiply(0x00008000, between - within);
		xpsvx = (half * -500) >> 16;
		Fixed dead = (xpsvx >= kArProbMaxScore) ? 0
					: (xpsvx < 1 ? 0x00010000 : ArProbDecodeLu[(half * -500) >> 19]);
		dead = FixedMultiply(FixedMultiply(gSegWordSpacing, 0x0005f326), dead);
		FixedDivide(0x00010000, dead + 0x00010000);
	}

	Fixed pBoxScaled = SegGapLikelihood(boxGapScaled, pooledMean[4], pooledSigma[4],
					pooledMean[5], pooledSigma[5], 0x00010000, &within, &between);
	Fixed pMiddleScaled = SegGapLikelihood(middleGapScaled, pooledMean[6], pooledSigma[6],
					pooledMean[7], pooledSigma[7], 0x00013333, &within, &between);

	Fixed threshold = SegOnly != 0 ? gSegOnlyThreshold : gSegIntegrated;
	Fixed average = (pBox + pMiddle + pBoxScaled + pMiddleScaled) >> 2;
	*strength = average;
	return threshold < average;
}
