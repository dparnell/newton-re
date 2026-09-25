/*
	File:		recognition/RosStrokes.cpp

	Contains:	The handwriting engine's strokes and stroke lists - see
				RosStrokes.h.

	The engine allocates everything through `NewNamedPtr` with the tag
	'RoCK', which is how its memory is told apart from everyone else's
	in a heap dump, and throws `evt.ex.abt.stack` when there is none.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "RosStrokes.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"
#include "FixedMath.h"
#include "Render.h"

#include <stdio.h>



// The four characters every block of the engine's memory is tagged with.
const ULong	kRosettaMemoryTag	= 'RoCK';


// ROM 0x0c101950 mtemp
// Where the engine puts what it has just allocated before it looks at
// it.  It is a global rather than a local because the ROM's allocation
// macro writes through it.
static void*	gRosTemp = nil;


// The engine's allocation: tagged, and a throw rather than nil when
// there is none.  (The ROM writes this out at every call site; it is
// one place here.)
void*
RosAllocate(long size)
{
	gRosTemp = NewNamedPtr(size, kRosettaMemoryTag);
	if (gRosTemp == nil)
		Throw(exOutOfStack, (void*) "", nil);
	return gRosTemp;
}


#pragma mark -
/*--------------------------------------------------------------------
	One stroke.
--------------------------------------------------------------------*/

// ROM 0x002007f8 StrokeNew
RosStroke*
StrokeNew(void)
{
	RosStroke* stroke = (RosStroke*) RosAllocate(sizeof(RosStroke));
	if (stroke == nil)
		return nil;
	stroke->fCount = 0;
	stroke->fPoints = nil;
	stroke->fField1c = -1;
	stroke->fField20 = -1;
	SetFixedRect(&stroke->fBounds, 0, 0, 0, 0);
	stroke->fMidX = 0;
	stroke->fIndex = -1;
	stroke->fFragment = 0;
	stroke->fJoinsNext = 0;
	return stroke;
}


// ROM 0x002008a8 StrokeCreate
// The points copied in, and the bounding rectangle and the middle of
// the horizontal range worked out as they go.
RosStroke*
StrokeCreate(short count, const FPoint* points)
{
	RosStroke* stroke = StrokeNew();
	if (stroke == nil || count <= 0)
		return stroke;

	stroke->fCount = count;
	newton_try
	{
		stroke->fPoints = (FPoint*) RosAllocate(count * (long) sizeof(FPoint));
	}
	newton_catch_all
	{
		StrokeDestroy(stroke);
		rethrow;
	}
	end_try;

	Fixed maxX = points[0].x;
	Fixed minX = maxX;
	Fixed minY = points[0].y;
	Fixed maxY = minY;
	for (long i = 0; i < count; i++)
	{
		Fixed x = points[i].x;
		Fixed y = points[i].y;
		stroke->fPoints[i].x = x;
		stroke->fPoints[i].y = y;
		if (x > maxX)
			maxX = x;
		else if (x < minX)
			minX = x;
		if (y > maxY)
			maxY = y;
		else if (y < minY)
			minY = y;
	}
	SetFixedRect(&stroke->fBounds, minX, minY, maxX, maxY);
	// (the ROM adds the two edges and halves them, which wraps on a
	//  stroke drawn past half the Fixed range; no tablet reaches it)
	stroke->fMidX = (Fixed) (((long) (int) ((unsigned int) minX + (unsigned int) maxX)) >> 1);
	return stroke;
}


// ROM 0x00200d58 StrokeSet
// The points handed over rather than copied.  A non-nil `bounds` says
// they are already worked out - it is not read from, which is why it
// can be anything at all.
void
StrokeSet(RosStroke* stroke, short count, FPoint* points, const FRect* bounds)
{
	stroke->fCount = count;
	stroke->fMidX = 0;
	stroke->fPoints = points;
	stroke->fIndex = -1;
	if (bounds == nil)
		StrokeCalcBounds(stroke);
}


// ROM 0x002016ec StrokeDestroy
void
StrokeDestroy(RosStroke* stroke)
{
	// (the engine's `free` is a branch to DisposPtr: its memory is the
	//  Newton's pointer heap, not the C library's)
	if (stroke->fPoints != nil)
		DisposPtr((Ptr) stroke->fPoints);
	DisposPtr((Ptr) stroke);
}


// ROM 0x00201484 StrokeDuplicate
// Everything copied, points and all: the ROM moves the whole 0x34-byte
// object across and then gives the copy points of its own.
RosStroke*
StrokeDuplicate(const RosStroke* stroke)
{
	RosStroke* copy = StrokeNew();
	if (copy == nil)
		return nil;

	*copy = *stroke;
	copy->fPoints = nil;
	long count = stroke->fCount;
	if (count <= 0)
		return copy;

	FPoint* points = nil;
	newton_try
	{
		points = (FPoint*) RosAllocate(count * (long) sizeof(FPoint));
	}
	newton_catch_all
	{
		StrokeDestroy(copy);
		rethrow;
	}
	end_try;

	const FPoint* from = stroke->fPoints;
	for (long i = 0; i < count; i++)
	{
		points[i].x = from[i].x;
		points[i].y = from[i].y;
	}
	copy->fPoints = points;
	return copy;
}


// ROM 0x00201620 StrokeCalcBounds
// The rectangle round the points, and the middle of the horizontal
// range, worked out afresh.
void
StrokeCalcBounds(RosStroke* stroke)
{
	long count = stroke->fCount;
	if (count == 0)
	{
		SetFixedRect(&stroke->fBounds, 0, 0, 0, 0);
		stroke->fMidX = 0;
		return;
	}

	const FPoint* p = stroke->fPoints;
	Fixed maxX = p->x;
	Fixed minX = maxX;
	Fixed minY = p->y;
	Fixed maxY = minY;
	for (long n = count - 1; n > 0; n--)
	{
		p++;
		Fixed x = p->x;
		Fixed y = p->y;
		if (x > maxX)
			maxX = x;
		else if (x < minX)
			minX = x;
		if (y > maxY)
			maxY = y;
		else if (y < minY)
			minY = y;
	}
	SetFixedRect(&stroke->fBounds, minX, minY, maxX, maxY);
	stroke->fMidX = (Fixed) (((long) (int) ((unsigned int) minX + (unsigned int) maxX)) >> 1);
}


// ROM 0x002015dc StrokeFindBounds
// The bounds asked for.  They are worked out only when what is there is
// not a rectangle at all, which is how a stroke whose points have been
// moved about says so: whoever moved them leaves the rectangle invalid.
void
StrokeFindBounds(RosStroke* stroke, FRect* out)
{
	if (!ValidFixedRect(&stroke->fBounds))
		StrokeCalcBounds(stroke);
	if (&stroke->fBounds != out)
		CopyFixedRect(out, &stroke->fBounds);
}


// ROM 0x002013f0 StrokeScale
void
StrokeScale(RosStroke* stroke, Fixed xScale, Fixed yScale)
{
	if (stroke == nil)
		return;
	long count = stroke->fCount;
	if (count <= 0 || stroke->fPoints == nil)
		return;

	for (FPoint* p = stroke->fPoints; p < stroke->fPoints + stroke->fCount; p++)
	{
		p->x = FixedMultiply(p->x, xScale);
		p->y = FixedMultiply(p->y, yScale);
	}
	StrokeCalcBounds(stroke);
}


// ROM 0x00200d84 StrokeSort
// The strokes of a word put in the order they sit on the line, by the
// middle of each one's horizontal range.  It is an insertion sort, and
// it carries the last stroke's middle along as it goes so that writing
// that was already in order costs one comparison each.
void
StrokeSort(RosStroke** strokes, short count)
{
	if (count < 2)
		return;

	Fixed last = strokes[0]->fMidX;
	for (short i = 1; i < count; )
	{
		RosStroke* stroke = strokes[i];
		Fixed x = stroke->fMidX;
		if (x >= last)
		{
			// already where it belongs
			last = x;
			i = (short) (i + 1);
			continue;
		}

		// back down to where it does belong
		short at = (short) (i - 1);
		while (at >= 0 && strokes[at]->fMidX > x)
			at = (short) (at - 1);
		// ... and everything between moved up one
		for (short j = i; at + 1 < j; j = (short) (j - 1))
			strokes[j] = strokes[j - 1];
		strokes[at + 1] = stroke;

		last = strokes[i]->fMidX;
		i = (short) (i + 1);
	}
}


// One run of strokes that came out of a single stroke the engine cut
// up.  The ROM's is 12 bytes and is made and thrown away inside
// `StrokeSortFrags`; nothing else ever sees one.
struct RosStrokeGroup
{
	short			fCount;			// +0x00
	short			fPad02;
	RosStroke**		fMembers;		// +0x04
	Fixed			fMiddle;		// +0x08  the middle of the whole group
};


// The groups given back, as far as they were made.
static void
DisposeStrokeGroups(RosStrokeGroup** groups, short count)
{
	if (groups == nil)
		return;
	for (short i = 0; i < count && groups[i] != nil; i++)
	{
		if (groups[i]->fMembers != nil)
			DisposPtr((Ptr) groups[i]->fMembers);
		DisposPtr((Ptr) groups[i]);
	}
	DisposPtr((Ptr) groups);
}


// ROM 0x00200e40 StrokeSortFrags
// The strokes put in the order they sit on the line, as `StrokeSort`
// does, except that the pieces of one stroke stay together.
//
// When the engine has cut a stroke in two the pieces must not be
// separated by the sort, however their middles happen to fall - they
// are one piece of writing.  So the array is first gathered into
// groups: a stroke whose `fJoinsNext` is set carries the one after it
// into the same group, and a group ends at the first stroke that does
// not.  Each group is measured as a whole - the leftmost left and the
// rightmost right of everything in it - and it is the *groups* that
// are sorted and then written back out flat.
//
// Nothing at all is done unless the array begins and ends at a group
// boundary, which is the ROM's way of saying "these strokes are a
// whole word": the first must not be a piece cut off something before
// it, and the last must not be waiting for the rest of itself.
//
// (The caller in the word recogniser passes three more arguments - the
// mean stroke size, the baseline and the mean height - which this
// function never looks at; `StrokeSort` beside it takes only these
// two.)
void
StrokeSortFrags(RosStroke** strokes, short count)
{
	if (count < 2)
		return;
	if (strokes[0]->fFragment != 0 || strokes[count - 1]->fJoinsNext != 0)
		return;

	// (`volatile` because the handler below reads it after a longjmp)
	RosStrokeGroup** volatile groups = nil;
	newton_try
	{
		// one group per stroke, because in the worst case none of them
		// join.  The array itself is cleared, so the cleanup below can
		// tell how far the making got.
		gRosTemp = NewPtrClear(count * (long) sizeof(RosStrokeGroup*));
		if (gRosTemp == nil)
			Throw(exOutOfStack, (void*) "", nil);
		SetPtrName((Ptr) gRosTemp, kRosettaMemoryTag);
		groups = (RosStrokeGroup**) gRosTemp;

		for (short i = 0; i < count; i++)
		{
			groups[i] = (RosStrokeGroup*) RosAllocate((long) sizeof(RosStrokeGroup));
			groups[i]->fMembers = nil;
			groups[i]->fMembers = (RosStroke**) RosAllocate(count * (long) sizeof(RosStroke*));
			groups[i]->fCount = 0;
		}

		// the strokes dealt out, a group at a time
		short used = 0;
		for (short i = 0; i < count; i++)
		{
			RosStrokeGroup* group = groups[used];
			group->fMembers[group->fCount] = strokes[i];
			group->fCount = (short) (group->fCount + 1);
			if (strokes[i]->fJoinsNext == 0)
				used = (short) (used + 1);
		}

		// ... and each one measured as a whole
		for (short g = 0; g < used; g++)
		{
			RosStrokeGroup* group = groups[g];
			Fixed left = group->fMembers[0]->fBounds.left;
			Fixed right = group->fMembers[0]->fBounds.right;
			for (short i = 1; i < group->fCount; i++)
			{
				if (group->fMembers[i]->fBounds.left < left)
					left = group->fMembers[i]->fBounds.left;
				if (right < group->fMembers[i]->fBounds.right)
					right = group->fMembers[i]->fBounds.right;
			}
			group->fMiddle = (left + right) >> 1;
		}

		// the same insertion sort as StrokeSort, over the groups
		Fixed last = groups[0]->fMiddle;
		for (short i = 1; i < used; i++)
		{
			Fixed middle = groups[i]->fMiddle;
			if (middle < last)
			{
				RosStrokeGroup* group = groups[i];
				short at = i;
				do
				{
					at = (short) (at - 1);
					if (at < 0)
						break;
				}
				while (middle < groups[at]->fMiddle);
				for (short j = i; at + 1 < j; j = (short) (j - 1))
					groups[j] = groups[j - 1];
				groups[at + 1] = group;
				middle = groups[i]->fMiddle;
			}
			last = middle;
		}

		// ... and written back out flat
		short at = 0;
		for (short g = 0; g < used; g++)
			for (short i = 0; i < groups[g]->fCount; i++)
			{
				if (at < count)
					strokes[at] = groups[g]->fMembers[i];
				at = (short) (at + 1);
			}
	}
	cleanup
	{
		DisposeStrokeGroups(groups, count);
	}
	end_try;

	// the groups given back, as far as they were made: the array was
	// cleared, so the first nil is the end of them
	DisposeStrokeGroups(groups, count);
}

#pragma mark -
/*--------------------------------------------------------------------
	Measuring and tidying.
--------------------------------------------------------------------*/

// ROM 0x0020000c StrokeCentroid
// The average of the points - which is not the middle of the box: a
// stroke that lingers at one end has its centroid pulled that way, and
// that is what the engine wants of it.  Each point is divided by the
// count before it is added, so a long stroke cannot overflow.
void
StrokeCentroid(const RosStroke* stroke, FPoint* centroid)
{
	Fixed x = 0;
	Fixed y = 0;
	if (stroke == nil || stroke->fCount < 1)
		printf("StrokeCentroid called for NULL or 0-pt stroke\r");
	else
	{
		Fixed share = FixedDivide(0x10000, (Fixed) (int) ((unsigned int) stroke->fCount << 16));
		const FPoint* first = stroke->fPoints;
		const FPoint* p = first + stroke->fCount;
		while (--p >= first)
		{
			x += FixedMultiply(p->x, share);
			y += FixedMultiply(p->y, share);
		}
	}
	centroid->x = x;
	centroid->y = y;
}


// ROM 0x00200570 StrokeSmooth
// A new stroke, each point moved by `weight`/4 of its second difference
// - the point before it, minus twice itself, plus the point after.  A
// negative weight therefore smooths and a positive one sharpens, and
// the two ends are left exactly where they were.
RosStroke*
StrokeSmooth(const RosStroke* stroke, Fixed weight)
{
	RosStroke* out = StrokeNew();
	if (out == nil)
		return nil;

	FPoint* points = nil;
	long count = stroke->fCount;
	newton_try
	{
		if (count > 0)
		{
			const FPoint* in = stroke->fPoints;
			points = (FPoint*) RosAllocate(count * (long) sizeof(FPoint));

			// (the ROM reads in[1] here whatever the count is, and only
			//  uses it when there are three points or more; a stroke of
			//  one would have it read past its own points.  We read it
			//  only when it is there, which is the same answer)
			Fixed prevX = 0, prevY = 0, thisX = 0, thisY = 0, rawX = 0, rawY = 0;
			if (count > 1)
			{
				prevX = FixedMultiply(in[0].x, weight) >> 2;
				prevY = FixedMultiply(in[0].y, weight) >> 2;
				rawX = in[1].x;
				rawY = in[1].y;
				thisX = FixedMultiply(rawX, weight) >> 2;
				thisY = FixedMultiply(rawY, weight) >> 2;
			}
			// the ends stay where they are
			points[0] = in[0];
			points[count - 1] = in[count - 1];

			for (long i = 2; i < count; i++)
			{
				Fixed nextX = in[i].x;
				Fixed nextY = in[i].y;
				Fixed scaledX = FixedMultiply(nextX, weight) >> 2;
				Fixed scaledY = FixedMultiply(nextY, weight) >> 2;
				points[i - 1].x = prevX + rawX - 2 * thisX + scaledX;
				points[i - 1].y = prevY + rawY - 2 * thisY + scaledY;
				prevX = thisX;
				prevY = thisY;
				thisX = scaledX;
				thisY = scaledY;
				rawX = nextX;
				rawY = nextY;
			}
		}
	}
	newton_catch_all
	{
		StrokeDestroy(out);
		rethrow;
	}
	end_try;

	StrokeSet(out, (short) count, points, nil);
	return out;
}


// ROM 0x00200224 StrokeConstrain
// Every point of `stroke` pulled back to within half of `tolerance` of
// the point it came from in `original` - the other half of what
// dequantising is made of: smoothing moves the points, this says how
// far they may go.
RosStroke*
StrokeConstrain(const RosStroke* stroke, const RosStroke* original, Fixed tolerance)
{
	RosStroke* out = StrokeNew();
	if (out == nil)
		return nil;

	FPoint* points = nil;
	long count = original->fCount;
	newton_try
	{
		if (count > 0)
		{
			const FPoint* orig = original->fPoints;
			const FPoint* in = stroke->fPoints;
			Fixed half = tolerance / 2;
			points = (FPoint*) RosAllocate(count * (long) sizeof(FPoint));
			for (long i = 0; i < count; i++)
			{
				Fixed x = in[i].x;
				Fixed y = in[i].y;
				Fixed low = orig[i].x - half;
				Fixed high = orig[i].x + half;
				points[i].x = (x < low) ? low : ((x > high) ? high : x);
				low = orig[i].y - half;
				high = orig[i].y + half;
				points[i].y = (y < low) ? low : ((y > high) ? high : y);
			}
		}
	}
	newton_catch_all
	{
		StrokeDestroy(out);
		rethrow;
	}
	end_try;

	StrokeSet(out, (short) count, points, nil);
	return out;
}


// ROM 0x00200404 StrokeDeQuantize
// The tablet reports the pen on a grid, so a slow stroke comes in as a
// staircase.  This takes it off: smooth the stroke, pull every point
// back to within `tolerance` of where it really was, and do it again.
// Each pass rounds the steps a little more without letting the stroke
// wander away from what was written.
RosStroke*
StrokeDeQuantize(const RosStroke* stroke, Fixed weight, Fixed tolerance, short passes)
{
	RosStroke* current = StrokeDuplicate(stroke);
	newton_try
	{
		for (short pass = 0; pass < passes; pass++)
		{
			RosStroke* smoothed = nil;
			newton_try
			{
				smoothed = StrokeSmooth(current, weight);
			}
			cleanup
			{
				StrokeDestroy(current);
			}
			end_try;
			StrokeDestroy(current);

			newton_try
			{
				current = StrokeConstrain(smoothed, stroke, tolerance);
			}
			cleanup
			{
				StrokeDestroy(smoothed);
			}
			end_try;
			StrokeDestroy(smoothed);
		}
	}
	newton_catch_all
	{
		rethrow;
	}
	end_try;
	return current;
}


// ROM 0x002000f4 StrokePreprocess
// What a stroke goes through before the engine looks at it, as a list
// of one: dequantised when asked, then smoothed when asked, and always
// a copy - the caller's stroke is never the one handed back.
RosStrokeList*
StrokePreprocess(RosStroke* stroke, Fixed smoothWeight, Fixed tolerance, short passes)
{
	RosStroke* current = stroke;
	newton_try
	{
		if (tolerance != 0)
			current = StrokeDeQuantize(stroke, smoothWeight, tolerance, passes);
		if (smoothWeight != 0)
		{
			RosStroke* smoothed = StrokeSmooth(current, smoothWeight);
			if (current != stroke)
				StrokeDestroy(current);
			current = smoothed;
		}
		if (current == stroke)
			current = StrokeDuplicate(stroke);
	}
	newton_catch_all
	{
		rethrow;
	}
	end_try;

	RosStroke* one[1];
	one[0] = current;
	return SLCreate(1, one);
}


#pragma mark -
/*--------------------------------------------------------------------
	A list of them.
--------------------------------------------------------------------*/

// ROM 0x00200a24 SLNew
RosStrokeList*
SLNew(void)
{
	RosStrokeList* list = (RosStrokeList*) RosAllocate(sizeof(RosStrokeList));
	if (list == nil)
		return nil;
	list->fCount = 0;
	list->fStrokes = nil;
	SetFixedRect(&list->fBounds, 0, 0, 0, 0);
	return list;
}


// ROM 0x00200ab4 SLCreate
// The strokes are *not* copied - the list holds the same objects - but
// the array of pointers is, and the bounds are grown over them as they
// go in.
RosStrokeList*
SLCreate(short count, RosStroke* const* strokes)
{
	RosStrokeList* list = SLNew();
	if (list == nil)
		return nil;

	list->fCount = count;
	if (count <= 0)
		return list;

	newton_try
	{
		list->fStrokes = (RosStroke**) RosAllocate(count * (long) sizeof(RosStroke*));
	}
	newton_catch_all
	{
		SLDestroy(list, 1);
		rethrow;
	}
	end_try;

	for (long i = 0; i < count; i++)
	{
		list->fStrokes[i] = strokes[i];
		OrFixedRect(&list->fBounds, &strokes[i]->fBounds);
	}
	return list;
}


// ROM 0x00200bd8 SLSet
void
SLSet(RosStrokeList* list, short count, RosStroke** strokes, const FRect* bounds)
{
	list->fCount = count;
	list->fStrokes = strokes;
	if (bounds != nil)
		CopyFixedRect(&list->fBounds, bounds);
	else
		SLCalcBounds(list);
}


// ROM 0x00200bfc SLDestroy
void
SLDestroy(RosStrokeList* list, short strokesToo)
{
	if (list == nil)
		return;
	if (list->fStrokes != nil)
	{
		if (strokesToo != 0)
			for (long i = 0; i < list->fCount; i++)
				StrokeDestroy(list->fStrokes[i]);
		DisposPtr((Ptr) list->fStrokes);
	}
	DisposPtr((Ptr) list);
}


// ROM 0x00200cb4 SLCalcBounds
// The rectangle round all of them - and each stroke's own worked out
// again first, because the list is asked this after something has
// moved the points about.
void
SLCalcBounds(RosStrokeList* list)
{
	if (list->fCount == 0)
	{
		SetFixedRect(&list->fBounds, 0, 0, 0, 0);
		return;
	}

	StrokeCalcBounds(list->fStrokes[0]);
	CopyFixedRect(&list->fBounds, &list->fStrokes[0]->fBounds);
	for (long i = 1; i < list->fCount; i++)
	{
		StrokeCalcBounds(list->fStrokes[i]);
		OrFixedRect(&list->fBounds, &list->fStrokes[i]->fBounds);
	}
}


// ROM 0x00200c70 SLFindBounds
void
SLFindBounds(RosStrokeList* list, FRect* out)
{
	if (!ValidFixedRect(&list->fBounds))
		SLCalcBounds(list);
	if (&list->fBounds != out)
		CopyFixedRect(out, &list->fBounds);
}


// ROM 0x00201320 SLSort
// And back into the order they were written in.  The same insertion
// sort as StrokeSort, over `fIndex` rather than the middle - which is
// how a word the engine has been holding in reading order is handed
// back to the recogniser in writing order, so that the strokes of a
// unit still match the strokes that came down.
void
SLSort(RosStrokeList* list)
{
	if (list->fCount < 2)
		return;

	RosStroke** strokes = list->fStrokes;
	short last = strokes[0]->fIndex;
	for (short i = 1; i < list->fCount; )
	{
		RosStroke* stroke = strokes[i];
		short index = stroke->fIndex;
		if (index >= last)
		{
			last = index;
			i = (short) (i + 1);
			continue;
		}

		short at = (short) (i - 1);
		while (at >= 0 && strokes[at]->fIndex > index)
			at = (short) (at - 1);
		for (short j = i; at + 1 < j; j = (short) (j - 1))
			strokes[j] = strokes[j - 1];
		strokes[at + 1] = stroke;

		last = strokes[i]->fIndex;
		i = (short) (i + 1);
	}
}


#pragma mark -
/*--------------------------------------------------------------------
	Drawn, for the classifier to look at.
--------------------------------------------------------------------*/

// ROM 0x001ffe70 StrokeDrawAAAt
// The stroke drawn into the renderer: every point scaled, offset, and
// then multiplied up by the renderer's scale, because the bitmap
// behind the grey grid is that many times bigger.  Rounding is by a
// half before the shift down.  A stroke of one point is a dot.
void
StrokeDrawAAAt(const RosStroke* stroke, RenderAA* aa, Fixed x, Fixed y,
			Fixed xScale, Fixed yScale)
{
	long shift = aa->fShift;
	Fixed px = FixedMultiply(stroke->fPoints[0].x, xScale) + x;
	Fixed py = FixedMultiply(stroke->fPoints[0].y, yScale) + y;
	if (stroke->fCount == 1)
	{
		long dx = (long) (((unsigned int) px << shift) + 0x8000) >> 16;
		long dy = (long) (((unsigned int) py << shift) + 0x8000) >> 16;
		RenderLine(aa->fRec, dx, dy, dx, dy);
		return;
	}
	for (short i = 1; i < stroke->fCount; i++)
	{
		Fixed nx = FixedMultiply(stroke->fPoints[i].x, xScale) + x;
		Fixed ny = FixedMultiply(stroke->fPoints[i].y, yScale) + y;
		RenderLine(aa->fRec,
				(long) (((unsigned int) px << shift) + 0x8000) >> 16,
				(long) (((unsigned int) py << shift) + 0x8000) >> 16,
				(long) (((unsigned int) nx << shift) + 0x8000) >> 16,
				(long) (((unsigned int) ny << shift) + 0x8000) >> 16);
		px = nx;
		py = ny;
	}
}


// ROM 0x001fff98 SLDrawAAAt
void
SLDrawAAAt(const RosStrokeList* list, RenderAA* aa, Fixed x, Fixed y,
		Fixed xScale, Fixed yScale)
{
	for (short i = 0; i < list->fCount; i++)
		StrokeDrawAAAt(list->fStrokes[i], aa, x, y, xScale, yScale);
}
