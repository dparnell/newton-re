/*
	File:		recognition/Segment.h

	Contains:	The segment layer: how the handwriting engine decides
				where one letter ends and the next begins.

				A `RosSegment` is a piece of writing the engine has
				decided is a character, or part of one: a stroke list,
				the box it fills, and a few things worth knowing about
				it without looking again - whether any of its strokes is
				a dot, how big the smallest of them is, and whether it
				runs on into the piece after it.

				The cutting itself (`SegmentChars` over `SegmentStroke`
				and `SegmentMakeSegments`) is NOT YET.  What is here is
				what a segment *is* and the measurements the cutting is
				made of, which are worth having on their own because
				every one of them is a small, sharp question about two
				pieces of ink:

				* `SegmentDot` - is this small enough in **both**
				  directions to be the dot over an i?
				* `SegmentAspect` - how wide is it against how tall?
				* `SegmentOverlap` - how much of the line do two boxes
				  share, as a fraction of each of them?
				* `SegmentStrokeMinDistance` - which two points of these
				  two strokes come nearest, and how near?
				* `SegmentCrossed` / `SegmentNonTailLinked` - and does
				  that nearest approach happen in the *middle* of both
				  strokes (so they cross, as in a t) or at their ends
				  (so they merely meet, as in a V)?

	Reconstructed from the MP2x00 US ROM (0x001d0e68-0x001d4cbc); each
	function cites its origin.
*/

#ifndef __SEGMENT_H
#define __SEGMENT_H

#ifndef __ROSSTROKES_H
#include "RosStrokes.h"
#endif

// A piece of writing the engine has decided is a character, or part of
// one.  The ROM's object is 0x2c bytes.
struct RosSegment
{
	// Which stroke of the word it starts at, and how many strokes it
	// covers (-1 and 0 when new).
	short			fFirstStroke;	// +0x00
	short			fCount;			// +0x02
	short			fField04;		// +0x04  (-1 when new)
	short			fField06;		// +0x06  (-1 when new)
	RosStrokeList*	fStrokes;		// +0x08
	FRect			fBounds;		// +0x0c  the box all of them fill
	UByte			fHasDot;		// +0x1c  any of them is a dot
	UByte			fPad1d[3];
	Fixed			fSmallestStroke;	// +0x20  see `SegmentBoundsDotsEtc`
	UByte			fFragment;		// +0x24  any of them is a piece of an earlier stroke
	UByte			fJoinsNext;		// +0x25  any of them runs on into the next
	UByte			fPad26[2];		// +0x26
	// How far the first of its strokes was from the stroke before it.
	Fixed			fSeparation;	// +0x28
};

// Where two strokes come nearest each other, and how near.  This is
// what `SegmentStrokeMinDistance` fills in and what `SegmentCrossed`
// and `SegmentNonTailLinked` read.
struct SegmentDistance
{
	Fixed			fDistance;		// +0x00  between the two points
	Fixed			fDX;			// +0x04  |dx| between them
	Fixed			fDY;			// +0x08  |dy|
	RosStroke*		fStrokeB;		// +0x0c  the second stroke it was asked about
	RosStroke*		fStrokeA;		// +0x10  ... and the first
	short			fIndexB;		// +0x14  which point of B
	short			fIndexA;		// +0x16  ... and of A
	UByte			fEitherIsDot;	// +0x18  one of the two is a dot
	UByte			fPad19[3];
};


// A segment, made and given back.  `Destroy` takes its strokes with it.
RosSegment*	SegmentCreate(void);						// ROM 0x001d0e68 SegmentCreate
void	SegmentInit(RosSegment* self);					// ROM 0x001d0ed8 SegmentInit
void	SegmentDestroy(RosSegment* self);				// ROM 0x001d1cac SegmentDestroy
// The strokes it is over: they are sorted into writing order and the
// pieces the engine cut for itself are put back together, so the
// segment holds whole strokes however they arrived.
void	SegmentSetStrokes(RosSegment* self, short count, RosStroke* const* strokes);	// ROM 0x001d1800 SegmentSetStrokes
// The box, the dots and the rest worked out from the strokes it holds.
void	SegmentBoundsDotsEtc(RosSegment* self);			// ROM 0x001d4b60 SegmentBoundsDotsEtc

// Everything the segment layer is holding on to, given back.
void	SegmentQuiesce(void);							// ROM 0x001d0f3c SegmentQuiesce
// Whether the engine is reading writing that runs together: it sets the
// threshold the cutting works to, nine tenths when it is and a half
// when it is not.
void	SegmentIntegrated(long integrated);				// ROM 0x001d4cbc SegmentIntegrated

// The smallest a stroke may be and still be said to go one way rather
// than another - and, because it is the same number, the smallest cap
// height the word recogniser will believe.
Fixed	SegmentMinStrokeSize(void);						// ROM 0x001d1890 SegmentMinStrokeSize

// Small enough in **both** directions to be a dot rather than a mark
// that goes somewhere.  Note that it is both: a stroke may be four
// pixels wide and forty tall without being a dot.
Boolean	SegmentDot(const FRect* bounds);				// ROM 0x001d4870 SegmentDot
Boolean	SegmentDot2(Fixed left, Fixed top, Fixed right, Fixed bottom);	// ROM 0x001d4878 SegmentDot2
// How wide against how tall, each measured inclusively.
Fixed	SegmentAspect(const FRect* bounds);				// ROM 0x001d28cc SegmentAspect

// How much of the line two boxes share: the mean of the two fractions,
// so that a narrow box wholly inside a wide one scores about a half and
// two boxes lying on each other score one.  Nought when they do not
// meet at all.
Fixed	SegmentOverlap(const FRect* a, const FRect* b);	// ROM 0x001d1c94 SegmentOverlap
Fixed	SegmentOverlapAr(Fixed aLeft, Fixed aRight, Fixed bLeft, Fixed bRight);	// ROM 0x001d1ce0 SegmentOverlapAr

// What the segment layer wants remembered about a stroke as it comes
// in: its bounds, whether it is a dot, where it came in the writing and
// how far it is from the one before.
void	SegmentStrokeData(RosStroke* stroke, UByte how, short index, Fixed separation);	// ROM 0x001d2224 SegmentStrokeData

// A segment's strokes told how much of the line each shares with the
// one before it, now that the segment has them in its own order.  The
// first is measured against the last stroke of the segment before it.
void	SegmentSetStrokeOverlaps(RosSegment* self, const RosSegment* previous);	// ROM 0x001d1db4 SegmentSetStrokeOverlaps

// Which two points of two strokes come nearest, by the sum of the two
// distances rather than by the real one - that is worked out only for
// the pair that wins.
void	SegmentStrokeMinDistance(RosStroke* a, RosStroke* b, SegmentDistance* out);	// ROM 0x001d2274 SegmentStrokeMinDistance
// Given that, whether the two strokes cross (both nearest points well
// inside both strokes) ...
Boolean	SegmentCrossed(const SegmentDistance* d);		// ROM 0x001d47a4 SegmentCrossed
// ... or are joined somewhere other than end to end.
Boolean	SegmentNonTailLinked(const SegmentDistance* d);	// ROM 0x001d1bcc SegmentNonTailLinked


// How near a stroke comes to any of the **three** strokes before it,
// and - in `prev` - to the one immediately before it on its own.  A
// stroke with nothing before it answers a distance of `size` plus a
// pixel in both, which is "further than anything the caller cares
// about".
void	SegmentMultiStrokeMinDistance(short index, RosStroke* const* strokes, Fixed size,
					short count, SegmentDistance* out, SegmentDistance* prev);	// ROM 0x001d18a4 SegmentMultiStrokeMinDistance
// The same question about the *horizontal gap*: how little space there
// is between this stroke's left edge and the right edge of any of the
// three before it.
Fixed	SegmentMultiStrokeMinDistBoundX(short index, RosStroke* const* strokes, Fixed size,
					short count);						// ROM 0x001d1aa0 SegmentMultiStrokeMinDistBoundX

// One stroke measured against its neighbours: how much of the line it
// shares with the one before it, whether the two are part of one
// letter, and whether a cut may go in front of it - in which case its
// index is added to `breaks`.
void	SegmentStroke(short index, short count, RosStroke* const* strokes,
					Fixed size, Fixed reach, UByte how,
					short* breaks, short* breakCount);	// ROM 0x001d1e98 SegmentStroke
// The strokes of a word cut into characters; answers how many segments
// were made.  The two widths the cutting works to come out of the
// writing's height here.
short	SegmentChars(short count, RosStroke** strokes, Fixed meanSize,
					RosSegment** segments, UByte how, void* net);	// ROM 0x001d48a4 SegmentChars
// The break candidates walked and the segments made.  NOT YET, so
// `SegmentChars` answers no segments however many breaks it found.
short	SegmentMakeSegments(short index, short count, RosStroke* const* strokes,
					const short* breaks, short breakCount, RosSegment** segments,
					long last, UByte how, void* net);	// ROM 0x001d0f68 SegmentMakeSegments

#endif	/* __SEGMENT_H */
