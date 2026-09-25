/*
	File:		recognition/Segment.cpp

	Contains:	The segment layer - see Segment.h.

	Reconstructed from the MP2x00 US ROM; each function cites its
	origin.
*/

#include "Segment.h"
#include "RosEngine.h"
#include "FixedGeometry.h"
#include "FixedMath.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"


// ROM 0x0c101ae8 (unnamed)
// How much two pieces must agree before the engine runs them together:
// nine tenths when it has been told the writing is joined up, a half
// when it has not.
static Fixed	gSegIntegrated = 0;
// ROM 0x0c101afc (unnamed)
// What the layer is holding on to between words.  Nothing here makes
// one yet; `SegmentQuiesce` is what gives it back.
static void*	gSegWorkspace = nil;


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
	if (gSegWorkspace != nil)
		DisposPtr((Ptr) gSegWorkspace);
	gSegWorkspace = nil;
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
	self->fField00 = -1;
	self->fCount = 0;
	self->fField04 = -1;
	self->fField06 = -1;
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
		Fract square = FixedMultiply(fx, fx) + FixedMultiply(fy, fy);
		out->fDistance = (FractSquareRoot(square) + 0x40) >> 7;
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
	The cutting itself.  NOT YET: `SegmentChars` runs `SegmentStroke`
	over every stroke to measure it against its neighbours and then
	`SegmentMakeSegments` to decide where the cuts go, which together
	are eight kilobytes and the heart of the layer.
--------------------------------------------------------------------*/

// ROM 0x001d48a4 SegmentChars
short
SegmentChars(short /*count*/, RosStroke** /*strokes*/, Fixed /*meanSize*/,
			RosSegment** /*segments*/, UByte /*how*/, void* /*net*/)
{
	return 0;
}
