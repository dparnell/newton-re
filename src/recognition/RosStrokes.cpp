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
static void*
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
	stroke->fField24[2] = 0;
	stroke->fField24[3] = 0;
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
