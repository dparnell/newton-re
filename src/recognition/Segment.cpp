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
	The cutting itself.  NOT YET: `SegmentChars` runs `SegmentStroke`
	over every stroke (done, above) and then `SegmentMakeSegments`
	over the break candidates it left, which is where the cuts are
	actually made.
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


// ROM 0x001d0f68 SegmentMakeSegments
// NOT YET: the 2200 bytes that walk the break candidates and actually
// make the segments.
short
SegmentMakeSegments(short /*index*/, short /*count*/, RosStroke* const* /*strokes*/,
				const short* /*breaks*/, short /*breakCount*/, RosSegment** /*segments*/,
				long /*last*/, UByte /*how*/, void* /*net*/)
{
	return 0;
}
