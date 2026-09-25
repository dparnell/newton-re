/*
	File:		recognition/Fragment.cpp

	Contains:	The ligature fragmenter - see Fragment.h.

	Reconstructed from the MP2x00 US ROM; each function cites its
	origin.
*/

#include "Fragment.h"
#include "RosStrokes.h"
#include "RosList.h"
#include "Segment.h"			// SegmentMinStrokeSize
#include "FixedMath.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"

#include <string.h>


// ROM 0x0c106074 RUN
Fixed	RUN[22] = { 0 };


// ROM 0x000cc3d4 FSfree
// What the runs are given back with: one instruction in the ROM, a
// branch straight into `free`.
static void
FSfree(void* p)
{
	DisposPtr((Ptr) p);
}


// The size the writer's hand says a piece should be measured against:
// the length of the last two of the running measurements taken
// together.  Both the cusp pass and the break pass work to a fraction
// of it.
static Fixed
FragmentNominalSize(void)
{
	Fixed x = FixedMultiply(RUN[20], RUN[20]);
	Fixed y = FixedMultiply(RUN[21], RUN[21]);
	return FixedSqrt(x + y);
}


// ROM 0x000cbdcc CalcDeltas
// Every step of the stroke as a (dx, dy, length).
StrokeDelta*
CalcDeltas(const RosStroke* stroke)
{
	if (stroke->fCount < 2)
		return nil;

	long steps = stroke->fCount - 1;
	StrokeDelta* deltas = (StrokeDelta*) RosAllocate(steps * (long) sizeof(StrokeDelta));

	const FPoint* points = stroke->fPoints;
	Fixed x = points[0].x;
	Fixed y = points[0].y;
	for (long i = 0; i < steps; i++)
	{
		Fixed nx = points[i + 1].x;
		Fixed ny = points[i + 1].y;
		deltas[i].fDX = nx - x;
		deltas[i].fDY = ny - y;
		deltas[i].fLength = FixedSqrt(
					FixedMultiply(deltas[i].fDX, deltas[i].fDX)
					+ FixedMultiply(deltas[i].fDY, deltas[i].fDY));
		x = nx;
		y = ny;
	}
	return deltas;
}


// ROM 0x000cc0a4 StrokeIsHorizontal
// Four times wider than it is tall, and big enough to be worth saying
// so about.  (The ROM asks twice over that the height is small against
// the width; the second test implies the first.)
Boolean
StrokeIsHorizontal(const RosStroke* stroke)
{
	Fixed width = stroke->fBounds.right - stroke->fBounds.left;
	Fixed height = stroke->fBounds.bottom - stroke->fBounds.top;
	Fixed biggest = (width < height) ? height : width;
	return SegmentMinStrokeSize() <= biggest
		&& (height >> 2) <= width
		&& height < (width >> 2);
}


// ROM 0x000cc088 IsThisRunGood
// A step that goes rightwards, and more than it goes downwards: the
// shape of a join between two letters.
Boolean
IsThisRunGood(Fixed dx, Fixed dy)
{
	return dx >= 0 && (dx - dy) >= 0;
}


// ROM 0x000cbedc FindGoodRuns
// Every stretch of good steps, appended to the list.  A run starts
// where two good steps follow one another and ends where two bad ones
// do, so a single wobble neither starts nor ends one.
void
FindGoodRuns(const RosStroke* stroke, const StrokeDelta* deltas, List* runs)
{
	long steps = stroke->fCount - 2;
	if (steps <= 0)
		return;

	Boolean inRun = false;
	Boolean wasGood = false;
	StrokeRun* run = nil;
	for (long i = 0; i < steps; i++)
	{
		Boolean good = IsThisRunGood(deltas[i].fDX, deltas[i].fDY);
		Boolean nextGood = (i + 1 < steps)
					? IsThisRunGood(deltas[i + 1].fDX, deltas[i + 1].fDY)
					: true;
		if (wasGood && good && !inRun)
		{
			inRun = true;
			run = (StrokeRun*) RosAllocate((long) sizeof(StrokeRun));
			run->fFirst = i - 1;
			run->fLast = i - 1;
			newton_try
			{
				ListAppendEntry(runs, run);
			}
			newton_catch_all
			{
				DisposPtr((Ptr) run);
				rethrow;
			}
			end_try;
		}
		else if (inRun && !good && !nextGood)
		{
			inRun = false;
			run->fLast = i;
		}
		wasGood = good;
	}
	if (inRun)
		run->fLast = stroke->fCount - 1;
}


// ROM 0x000cc3a4 FindFragmentLength
Fixed
FindFragmentLength(const StrokeDelta* deltas, long first, long last)
{
	Fixed total = 0;
	for (; first < last; first++)
		total += deltas[first].fLength;
	return total;
}


// ROM 0x000cc328 FindBreakPoint
// Where along a stretch a given length is reached, rounded to the
// nearer of the two points it falls between.
long
FindBreakPoint(const StrokeDelta* deltas, long first, long last, Fixed want)
{
	Fixed total = 0;
	for (;;)
	{
		if (last <= first)
			return last;
		total += deltas[first].fLength;
		if (want <= total)
			break;
		first++;
	}
	Fixed before = total - deltas[first].fLength;
	Fixed part = FixedDivide(want - before, total - before);
	return (part < 0x00008001) ? first : first + 1;
}


// ROM 0x000cc420 CheckCusps
// A run that turns sharply one way and then sharply back is really two
// runs.  Each step is measured against the run's own direction as the
// ratio of the cross product to the dot product - the tangent of the
// angle between them - and a cusp is where that ratio, having risen
// past 0.30, comes back down below everything it has been so far.
void
CheckCusps(List* runs, ListEntry* cursor, const FPoint* points,
				const StrokeDelta* deltas)
{
	StrokeRun* run = (StrokeRun*) (cursor != nil ? cursor->fValue : nil);

	Fixed least = FixedMultiply(FragmentNominalSize(),
					(Fixed) gFragmentParams[kFragCuspMinRun]);
	if (FindFragmentLength(deltas, run->fFirst, run->fLast) < least)
		return;

	// the run's own direction, end to end
	FPoint along;
	SubtractFixedPoints(&along, &points[run->fLast], &points[run->fFirst]);

	Fixed gate = (Fixed) gFragmentParams[kFragCuspGate];
	Fixed most = (Fixed) gFragmentParams[kFragCuspMax];
	Fixed lowest = (Fixed) gFragmentParams[kFragCuspMin];
	long cusp = 0;
	long lastTurn = 0;
	for (long i = run->fFirst + 1; i < run->fLast - 2; i++)
	{
		Fixed dot = FixedMultiply(deltas[i].fDX, along.x)
					+ FixedMultiply(deltas[i].fDY, along.y);
		Fixed cross = FixedMultiply(deltas[i].fDX, along.y)
					- FixedMultiply(deltas[i].fDY, along.x);
		Fixed ratio = FixedDivide(cross, dot);
		if (ratio >= 1)
		{
			lastTurn = i;
			if (most < ratio)
				most = ratio;
		}
		else if (ratio < lowest)
		{
			lowest = ratio;
			if (gate < most)
				cusp = lastTurn + 1;
		}
	}

	if (cusp <= 0)
		return;

	// the rest of the run becomes a run of its own, put in after this
	// one
	StrokeRun* rest = (StrokeRun*) RosAllocate((long) sizeof(StrokeRun));
	rest->fFirst = cusp;
	rest->fLast = run->fLast;
	run->fLast = cusp;
	newton_try
	{
		ListAddEntry(runs, &cursor, rest);
	}
	newton_catch_all
	{
		DisposPtr((Ptr) rest);
		rethrow;
	}
	end_try;
}


// ROM 0x000cc3d8 ProcessCusps
// Every run put through `CheckCusps`.  The next entry is taken before
// the check, because the check may put a new one in after this one -
// which is how a run cut in two is itself looked at again.
void
ProcessCusps(const RosStroke* stroke, List* runs, const StrokeDelta* deltas)
{
	ListEntry* entry = runs->fFirst;
	if (entry == nil)
		return;
	do
	{
		ListEntry* next = (entry != nil) ? entry->fNext : nil;
		CheckCusps(runs, entry, stroke->fPoints, deltas);
		entry = next;
	}
	while (entry != nil);
}


// The column of the projection an x falls in.  (The ROM writes this
// out at each of its five uses.)
static long
XProjectionColumn(const XProjection* proj, Fixed x)
{
	long scale = gXProjectionParams[kXProjColumnsPerPixel];
	long scaled = scale * (x - proj->fLeft);
	return (scaled + (scaled < 0 ? -0x4000 : 0x4000)) >> 16;
}


// ROM 0x000cc8d8 CalcXProjection
// How often the ink crosses each column of the stroke's own width.
//
// The stroke is divided into eight columns to the pixel and every step
// marks the columns it spans.  A step that turns the pen round is
// counted once rather than twice - the two adjustments below - so that
// the top of an `n` does not read as two crossings.
XProjection*
CalcXProjection(const RosStroke* stroke)
{
	if (stroke == nil || stroke->fCount < 2)
		return nil;

	FRect bounds;
	StrokeFindBounds((RosStroke*) stroke, &bounds);

	long scale = gXProjectionParams[kXProjColumnsPerPixel];
	long wide = scale * (bounds.right - bounds.left);
	long columns = ((wide + (wide < 0 ? -0x4000 : 0x4000)) >> 16) + 1;
	if (columns <= 0)
		return nil;

	XProjection* proj = nil;
	short* counts = nil;
	short* values = nil;
	newton_try
	{
		proj = (XProjection*) RosAllocate((long) sizeof(XProjection));
		counts = (short*) RosAllocate(columns * 2);
		values = (short*) RosAllocate((long) stroke->fCount * 2);
	}
	newton_catch_all
	{
		if (proj != nil)
			DisposPtr((Ptr) proj);
		if (counts != nil)
			DisposPtr((Ptr) counts);
		if (values != nil)
			DisposPtr((Ptr) values);
		rethrow;
	}
	end_try;

	proj->fLeft = bounds.left;
	proj->fColumns = (short) columns;
	proj->fCounts = counts;
	proj->fPointValue = values;
	for (long i = 0; i < columns; i++)
		counts[i] = 0;

	const FPoint* points = stroke->fPoints;
	Fixed x = points[0].x;
	long direction = 0;
	for (long i = 1; i < stroke->fCount; i++)
	{
		Fixed next = points[i].x;
		Fixed from = x;
		long lowAdjust;
		long highAdjust;
		if (x < next)
		{
			// the pen turned back towards the right
			lowAdjust = (i != 1 && direction != -1) ? 1 : 0;
			highAdjust = 0;
			direction = 1;
			x = next;
		}
		else if (next < x)
		{
			lowAdjust = 0;
			highAdjust = (i == 1 || direction == 1) ? 0 : -1;
			direction = -1;
			from = next;
		}
		else
		{
			lowAdjust = 0;
			highAdjust = 0;
			direction = 0;
			x = next;
		}

		long first = (short) (lowAdjust + XProjectionColumn(proj, from));
		long last = highAdjust + XProjectionColumn(proj, x);
		if (first < 1)
			first = 0;
		if (last > columns - 1)
			last = columns - 1;
		for (; first <= last; first++)
			counts[first]++;

		x = next;
	}

	// ... and what that count comes to at each point of the stroke
	long column = XProjectionColumn(proj, points[0].x);
	values[0] = counts[column];
	for (long i = 1; i < stroke->fCount; i++)
	{
		long next = XProjectionColumn(proj, points[i].x);
		XProjectionSetPtVals(counts, (short) column, (short) next,
						&values[i - 1], &values[i]);
		column = next;
	}
	return proj;
}


// ROM 0x000ccd9c XProjectionSetPtVals
// What the projection comes to at one point of the stroke.  Where more
// than half the columns the step crossed hold the same lowest count,
// that count is what the step is worth - and, depending which side of
// the middle its mean lies on, it is given to this point or to the one
// before it.
void
XProjectionSetPtVals(const short* counts, short from, short to,
				short* before, short* here)
{
	long lo = from;
	long hi = to;
	if (lo <= hi)
	{
		hi = to;
		lo = from;
	}
	else
	{
		hi = from;
		lo = to;
	}

	long least = counts[lo];
	long howMany = 1;
	long sumOfIndices = lo;
	for (long i = lo + 1; i <= hi; i++)
	{
		long n = counts[i];
		if (n < least)
		{
			howMany = 1;
			least = n;
			sumOfIndices = i;
		}
		else if (n == least)
		{
			howMany = (short) (howMany + 1);
			sumOfIndices += i;
		}
	}

	long mean = sumOfIndices / howMany;
	long half = (short) ((hi - lo + 1) >> 1);
	if (half < howMany)
	{
		long middle = lo + half;
		if ((mean < middle) == (from <= to))
		{
			*here = counts[to];
			if (least < *before)
				*before = (short) least;
			return;
		}
		*here = (short) least;
		return;
	}
	*here = counts[to];
}


// ROM 0x000ccee8 XProjectionDestroy
XProjection*
XProjectionDestroy(XProjection* proj)
{
	if (proj == nil)
		return nil;
	if (proj->fCounts != nil)
		DisposPtr((Ptr) proj->fCounts);
	if (proj->fPointValue != nil)
		DisposPtr((Ptr) proj->fPointValue);
	DisposPtr((Ptr) proj);
	return nil;
}


// ROM 0x000ccf6c CheckXProjection
// A run narrowed to the longest stretch where the ink crosses least -
// or thrown away when even its best is more than one crossing, because
// a place the pen has been twice is not a place to cut.
void
CheckXProjection(List* runs, ListEntry* cursor, const FPoint* /*points*/,
				const XProjection* proj)
{
	StrokeRun* run = (StrokeRun*) (cursor != nil ? cursor->fValue : nil);
	long first = (short) run->fFirst;
	long last = (short) run->fLast;
	const short* values = proj->fPointValue;

	long least = values[first];
	long most = least;
	for (long i = (short) (first + 1); i <= last; i = (short) (i + 1))
	{
		long n = values[i];
		if (n < least)
			least = n;
		if (most < n)
			most = n;
	}

	if (least > gXProjectionParams[kXProjMaxCrossings])
	{
		ListEntry* at = cursor;
		ListRemoveEntry(runs, &at);
		DisposPtr((Ptr) run);
		return;
	}
	if (most == 1)
		return;

	// The longest stretch holding the lowest count.
	//
	// ROM BUG: a stretch that turns out to be no longer than the best
	// so far is not closed - the count simply carries on over the
	// values that are not the lowest and into the next stretch, so two
	// short stretches with a gap between them can be taken for one
	// long one.  Only a stretch that *beats* the best ends the count.
	//
	// (The ROM also answers a run of two registers it never set when
	// `first` is past `last`, which cannot happen: a run always has
	// its first point before its last.)
	Boolean inRun = false;
	long best = 0;
	long howMany = 0;
	long start = 0;
	long bestFirst = 0;
	long bestLast = 0;
	for (long i = (short) first; i <= last; i = (short) (i + 1))
	{
		if (!inRun)
		{
			if (values[i] == least)
			{
				inRun = true;
				howMany = 1;
				start = i;
			}
		}
		else if (values[i] == least)
			howMany = (short) (howMany + 1);
		else if (best < howMany)
		{
			bestLast = (short) (i - 1);
			bestFirst = start;
			best = howMany;
			inRun = false;
		}
	}
	if (inRun && best < howMany)
	{
		bestLast = last;
		bestFirst = start;
	}
	run->fLast = bestLast;
	run->fFirst = bestFirst;
}


// ROM 0x000ccf24 ProcessXProjection
void
ProcessXProjection(const RosStroke* stroke, List* runs, const XProjection* proj)
{
	ListEntry* entry = runs->fFirst;
	if (entry == nil)
		return;
	do
	{
		ListEntry* next = (entry != nil) ? entry->fNext : nil;
		CheckXProjection(runs, entry, stroke->fPoints, proj);
		entry = next;
	}
	while (entry != nil);
}


// ROM 0x000cc100 DefineBreakPoints
// The surviving runs turned into break points: three quarters of the
// way along each one, with every piece held to at least 0.15 of the
// writing's size.  A break that would leave too little at the end of a
// stroke that is mostly horizontal is walked back until it does not.
void
DefineBreakPoints(const RosStroke* stroke, List* runs, StrokeBreaks* out,
				const StrokeDelta* deltas)
{
	if (runs->fCount == 0)
		return;

	out->fAt = (long*) RosAllocate(runs->fCount * (long) sizeof(long));

	Fixed least = FixedMultiply(FragmentNominalSize(),
					(Fixed) gFragmentParams[kFragMinPiece]);
	long found = 0;
	long previous = 0;
	for (ListEntry* entry = runs->fFirst; entry != nil; entry = entry->fNext)
	{
		StrokeRun* run = (StrokeRun*) entry->fValue;
		long first = run->fFirst;
		long last = run->fLast;
		if (first < 0 || last < first || last >= stroke->fCount)
			continue;

		Fixed along = FixedMultiply(FindFragmentLength(deltas, first, last),
						(Fixed) gFragmentParams[kFragBreakAt]);
		long at = FindBreakPoint(deltas, first, last, along);
		Fixed after = FindFragmentLength(deltas, at, stroke->fCount - 1);
		Fixed before = FindFragmentLength(deltas, previous, at);

		if (after < least && least * 2 <= after + before
			&& StrokeIsHorizontal(stroke))
			// too little left at the end: walk the break back
			for (; after < least && at > 0; at--)
			{
				Fixed step = deltas[at - 1].fLength;
				after += step;
				before -= step;
			}

		// both pieces must be long enough
		Fixed check = (least <= before) ? after : before;
		if (check < least)
			continue;
		// ... and the break must fall strictly after the one before it
		// and strictly short of the end
		if (!(previous < at && at < stroke->fCount - 1))
			continue;
		out->fAt[found++] = at;
		previous = at;
	}
	out->fCount = found;
}


// ROM 0x000cd4cc FindStrokeFragments
// All four passes, and the runs given back afterwards.
void
FindStrokeFragments(RosStroke* stroke, StrokeBreaks* out)
{
	StrokeDelta* deltas = nil;
	XProjection* proj = nil;
	out->fCount = 0;
	out->fAt = nil;

	newton_try
	{
		deltas = CalcDeltas(stroke);
		if (stroke->fCount > 2)
		{
			List* runs = ListCreate();
			newton_try
			{
				FindGoodRuns(stroke, deltas, runs);
				ProcessCusps(stroke, runs, deltas);
				proj = CalcXProjection(stroke);
				ProcessXProjection(stroke, runs, proj);
				DefineBreakPoints(stroke, runs, out, deltas);
			}
			newton_catch_all
			{
				ListDestroy(runs, FSfree);
				rethrow;
			}
			end_try;
			ListDestroy(runs, FSfree);
		}
	}
	newton_catch_all
	{
		if (deltas != nil)
			DisposPtr((Ptr) deltas);
		XProjectionDestroy(proj);
		rethrow;
	}
	end_try;

	if (deltas != nil)
		DisposPtr((Ptr) deltas);
	XProjectionDestroy(proj);
}


// ROM 0x000cd0cc StrokeSubsection
// A stretch of a stroke's points as a stroke of its own.  `fFragment`
// says this is a piece of a larger stroke and `fJoinsNext` that the
// next one is the rest of it, which is how the segment layer puts them
// back together again.
RosStroke*
StrokeSubsection(const RosStroke* stroke, short from, short count)
{
	long first = from;
	long many = count;
	if (stroke->fCount < first + many)
		many = (short) (stroke->fCount - first);

	RosStroke* piece = StrokeCreate((short) many, stroke->fPoints + first);
	if (first != 0)
		piece->fFragment = 1;
	if (first + many != stroke->fCount)
		piece->fJoinsNext = 1;
	return piece;
}


// ROM 0x000cc638 FragmentStroke
// The stroke cut into pieces at the break points the passes found.  A
// stroke of no points answers an empty list; a stroke nothing was
// found in answers one piece, which is the stroke itself again.
RosStrokeList*
FragmentStroke(RosStroke* stroke, const Fixed* run)
{
	RosStrokeList* pieces = nil;
	if (stroke->fCount == 0)
		return SLNew();

	// the writer's hand, which the passes reach as a global
	memcpy(RUN, run, sizeof(RUN));

	StrokeBreaks breaks;
	FindStrokeFragments(stroke, &breaks);

	newton_try
	{
		RosStroke** made = nil;
		newton_try
		{
			// DEVIATION: an array of pointers, sized by `sizeof` on the
			// host, where the ROM has four bytes apiece
			made = (RosStroke**) RosAllocate((breaks.fCount + 1)
							* (long) sizeof(RosStroke*));
			long at = 0;
			long i = 0;
			for (; i < breaks.fCount; i++)
			{
				long next = breaks.fAt[i];
				made[i] = StrokeSubsection(stroke, (short) at,
								(short) (next - at + 1));
				at = next;
			}
			made[i] = StrokeSubsection(stroke, (short) at,
							(short) (stroke->fCount - at));
			pieces = SLNew();
			SLSet(pieces, (short) (breaks.fCount + 1), made, nil);
		}
		newton_catch_all
		{
			rethrow;
		}
		end_try;
	}
	newton_catch_all
	{
		if (breaks.fAt != nil)
			DisposPtr((Ptr) breaks.fAt);
		rethrow;
	}
	end_try;

	if (breaks.fAt != nil)
		DisposPtr((Ptr) breaks.fAt);
	return pieces;
}
