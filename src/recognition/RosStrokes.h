/*
	File:		recognition/RosStrokes.h

	Contains:	The handwriting engine's own strokes, and the lists it
				keeps them in.

				The Newton has a `TStroke` already (recognition/Stroke.h):
				tablet samples packed into a handle, in eighths of a
				pixel, with the pen's timing.  The engine will not have
				it.  It wants a stroke it can scale, smooth, sort and
				measure without asking anyone, so `TRosRecognizer` copies
				every stroke into one of these on the way down
				(`AllocateAndConvertStrokeForRosetta`, RosRecognizer.h)
				and the engine works on its own copy from then on.

				A `Stroke` is a plain array of `FPoint` with its bounding
				rectangle beside it and the middle of its horizontal
				range worked out - that middle is what the strokes of a
				word are sorted by, because the engine wants them in the
				order they sit on the line rather than the order they
				were written in.  `StrokeIndex` remembers the order they
				*were* written in, and `SLSort` puts them back into it.

				A `StrokeList` is a count, an array of those and the
				rectangle round the lot.

				This is the bottom of the engine.  Everything above it -
				the word recogniser, the boxed-character recogniser, the
				feature extraction - works on these two.

	Reconstructed from the MP2x00 US ROM (0x002007f8-0x002016ec); each
	function cites its origin.
*/

#ifndef __ROSSTROKES_H
#define __ROSSTROKES_H

#ifndef __FIXEDGEOMETRY_H
#include "FixedGeometry.h"
#endif

// One stroke, as the engine holds it.  The ROM's object is 0x34 bytes;
// what is past the bounds belongs to the layers above and is named as
// they are read.
struct RosStroke
{
	short		fCount;			// +0x00  how many points
	short		fIndex;			// +0x02  where it came in the writing (-1: not said)
	Fixed		fMidX;			// +0x04  the middle of its horizontal range
	FPoint*		fPoints;		// +0x08
	FRect		fBounds;		// +0x0c
	long		fField1c;		// +0x1c  (-1 when new)
	long		fField20;		// +0x20  (-1 when new)
	UByte		fField24;		// +0x24
	UByte		fIsDot;			// +0x25  the dot over an i or a j
	UByte		fFragment;		// +0x26  a piece cut off a larger stroke
	UByte		fJoinsNext;		// +0x27  the stroke after it is the rest of this one
	UByte		fField28[0x0c];	// +0x28  ... to 0x34
};

// A handful of them, with the rectangle round the lot.  The ROM's
// object is 0x18 bytes.
struct RosStrokeList
{
	short		fCount;			// +0x00
	short		fPad02;
	RosStroke**	fStrokes;		// +0x04
	FRect		fBounds;		// +0x08
};


// An empty stroke; throws `evt.ex.abt.stack` when there is no room.
// The four characters every block of the engine's memory is tagged
// with, and the engine's own allocation: tagged, and a throw rather
// than nil when there is none.  Everything the engine owns comes
// through here.
extern const ULong	kRosettaMemoryTag;
void*		RosAllocate(long size);

RosStroke*	StrokeNew(void);									// ROM 0x002007f8 StrokeNew
// ... and one with the points copied in and its bounds worked out.
RosStroke*	StrokeCreate(short count, const FPoint* points);		// ROM 0x002008a8 StrokeCreate
// The points handed over rather than copied: the stroke does not own
// them.  A non-nil `bounds` only says the bounds are already right - it
// is never read from.
void		StrokeSet(RosStroke* stroke, short count, FPoint* points, const FRect* bounds);	// ROM 0x00200d58 StrokeSet
// Its points and then itself given back.
void		StrokeDestroy(RosStroke* stroke);					// ROM 0x002016ec StrokeDestroy
// A copy of everything, points and all.
RosStroke*	StrokeDuplicate(const RosStroke* stroke);			// ROM 0x00201484 StrokeDuplicate

// The bounding rectangle and the middle worked out afresh from the
// points; `FindBounds` only does it when what is there is not a
// rectangle, and copies the answer out.
void		StrokeCalcBounds(RosStroke* stroke);				// ROM 0x00201620 StrokeCalcBounds
void		StrokeFindBounds(RosStroke* stroke, FRect* out);	// ROM 0x002015dc StrokeFindBounds
// Every point multiplied by the scale of its own axis.
void		StrokeScale(RosStroke* stroke, Fixed xScale, Fixed yScale);	// ROM 0x002013f0 StrokeScale
// Strokes put in the order they sit on the line, by the middle of each
// one's horizontal range.
void		StrokeSort(RosStroke** strokes, short count);		// ROM 0x00200d84 StrokeSort
// ... and the same, except that strokes cut from one stroke stay
// together: the pieces are gathered into groups (a group ends at a
// stroke whose `fJoinsNext` is nought) and the *groups* are put in the
// order they sit on the line.  Nothing is done unless the array starts
// and ends at a group boundary.
void		StrokeSortFrags(RosStroke** strokes, short count);	// ROM 0x00200e40 StrokeSortFrags

// The average of the points, which is not the middle of the box.
void		StrokeCentroid(const RosStroke* stroke, FPoint* centroid);	// ROM 0x0020000c StrokeCentroid
// A copy with every point moved by `weight`/4 of its second
// difference: a negative weight smooths, a positive one sharpens, and
// the two ends stay where they were.
RosStroke*	StrokeSmooth(const RosStroke* stroke, Fixed weight);	// ROM 0x00200570 StrokeSmooth
// A copy with every point pulled back to within half of `tolerance` of
// where it is in `original`.
RosStroke*	StrokeConstrain(const RosStroke* stroke, const RosStroke* original, Fixed tolerance);	// ROM 0x00200224 StrokeConstrain
// The tablet's staircase taken off: smooth, pull back, and again.
RosStroke*	StrokeDeQuantize(const RosStroke* stroke, Fixed weight, Fixed tolerance, short passes);	// ROM 0x00200404 StrokeDeQuantize
// What a stroke goes through before the engine looks at it, as a list
// of one.  The two steps have weights of their own and either is
// skipped when its weight is nought; what comes back is always a copy.
RosStrokeList*	StrokePreprocess(RosStroke* stroke, Fixed smoothWeight,
						Fixed deQuantWeight, Fixed deQuantTolerance, short passes);	// ROM 0x002000f4 StrokePreprocess

// Drawn into the engine's own renderer (`recognition/Render.h`),
// scaled by `xScale`/`yScale` and offset by `x`/`y`, which is how a
// piece of writing becomes the picture the classifier is shown.  A
// stroke of one point is a dot.
struct RenderAA;
void		StrokeDrawAAAt(const RosStroke* stroke, RenderAA* aa, Fixed x, Fixed y,
						Fixed xScale, Fixed yScale);	// ROM 0x001ffe70 StrokeDrawAAAt
void		SLDrawAAAt(const RosStrokeList* list, RenderAA* aa, Fixed x, Fixed y,
						Fixed xScale, Fixed yScale);	// ROM 0x001fff98 SLDrawAAAt

RosStrokeList*	SLNew(void);									// ROM 0x00200a24 SLNew
RosStrokeList*	SLCreate(short count, RosStroke* const* strokes);	// ROM 0x00200ab4 SLCreate
// The array handed over rather than copied; a nil `bounds` asks for
// them to be worked out.
void		SLSet(RosStrokeList* list, short count, RosStroke** strokes, const FRect* bounds);	// ROM 0x00200bd8 SLSet
// `strokesToo` asks for the strokes themselves to be given back as well.
void		SLDestroy(RosStrokeList* list, short strokesToo);	// ROM 0x00200bfc SLDestroy
void		SLCalcBounds(RosStrokeList* list);					// ROM 0x00200cb4 SLCalcBounds
void		SLFindBounds(RosStrokeList* list, FRect* out);		// ROM 0x00200c70 SLFindBounds
// ... and back into the order they were written in.
void		SLSort(RosStrokeList* list);						// ROM 0x00201320 SLSort

#endif	/* __ROSSTROKES_H */
