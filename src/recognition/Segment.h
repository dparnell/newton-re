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

				The cutting itself is `SegmentChars` over
				`SegmentStroke` and `SegmentMakeSegments`.  Under it are
				what a segment *is* and the measurements the cutting is
				made of, each of them a small, sharp question about two
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
	// How many of its strokes are not continuations of a linked
	// run - so, how many letters the grouping stands for.
	short			fRealCount;		// +0x06  (-1 when new)
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
// The break candidates walked and the segments made.  Called once per
// stroke and then once more with `last` set; it keeps its working-out
// between calls, which is what `SegmentQuiesce` gives back.  What it
// leaves in `segments` is **not a partition** but every grouping of
// strokes the links allow, for the layer above to score.
short	SegmentMakeSegments(short index, short count, RosStroke* const* strokes,
					const short* breaks, short breakCount, RosSegment** segments,
					long last, UByte how, void* net);	// ROM 0x001d0f68 SegmentMakeSegments


/*--------------------------------------------------------------------
	The writer's word spacing.
--------------------------------------------------------------------*/

// How wide a space is taken to be, as a multiple of what it would be
// for a writer of ordinary habits, and its natural logarithm in 16.16 -
// which is the form the layers above want, because they add it.  The
// threshold that goes with it is interpolated between the three named
// values below.
extern Fixed	gSegWordSpacing;						// ROM 0x0c101ae0 (unnamed)
extern Fixed	gSegLogWordSpacing;						// ROM 0x0c101ae4 (unnamed)
// How much two pieces must agree before the engine runs them together,
// and the threshold the gap test works to when the engine is reading:
// nine tenths when the writing has been called joined up, a half when
// it has not.
extern Fixed	gSegIntegrated;							// ROM 0x0c101ae8 (unnamed)
extern Fixed	gSegOnlyThreshold;						// ROM 0x0c101aec (unnamed)
extern Fixed	MinSegOnlyThreshold;					// ROM 0x0c101af0 MinSegOnlyThreshold
extern Fixed	MidSegOnlyThreshold;					// ROM 0x0c101af4 MidSegOnlyThreshold
extern Fixed	MaxSegOnlyThreshold;					// ROM 0x0c101af8 MaxSegOnlyThreshold

// The writer's setting, 1 to 9 with 5 in the middle, turned into those
// numbers.  `RosettaSetArea` is what calls it.
void	SegmentSetWordSpacing(long spacing);				// ROM 0x001d2490 SegmentSetWordSpacing


#pragma mark -
/*--------------------------------------------------------------------
	Where one word ends and the next begins.
--------------------------------------------------------------------*/

// The eight numbers a piece of writing is judged by: the box round it,
// the middle of its ink, and two sizes.  The ROM passes these one
// argument at a time - `SegmentWord` and its three tests take
// twenty-three words between them, four in registers and nineteen on
// the stack - and they are gathered here because they always travel
// together.
struct SegWordInk
{
	Fixed		fLeft;
	Fixed		fRight;
	Fixed		fTop;
	Fixed		fBottom;
	// the middle of the ink, not of the box (`StrokeCentroid`)
	Fixed		fCentroidX;
	Fixed		fCentroidY;
	// how tall it is, and the greater of that and how wide - each with
	// a pixel added, so that a perfectly flat stroke still has a size
	Fixed		fHeight;
	Fixed		fSizeMax;
};

// ... and what a new stroke is compared against, which is either the
// stroke before it or the whole word so far.  The body band is the
// narrower top and bottom that the writing's *body* lies in, without
// its ascenders and descenders; `fStrokes` is how many strokes the
// reference covers, and one of them means a single stroke, which the
// vertical test treats more loosely.
struct SegWordRef
{
	SegWordInk	fInk;
	Fixed		fBodyTop;
	Fixed		fBodyBottom;
	long		fStrokes;
};

// How big the writing has turned out against the running mean, left
// behind by whichever test last worked it out.  Nothing reads it; the
// ROM keeps it for the debugger.
// The engine has been told to group the writing but not to read it.
extern ULong	SegOnly;								// ROM 0x0c104f9c SegOnly
// A number the gap test writes down for the debugger.
extern Fixed	xpsvx;									// ROM 0x0c100890 xpsvx
// ... and another the word recogniser's writes.
extern Fixed	abs_temp;								// ROM 0x0c10089c abs_temp
extern Fixed	gSegSizeRatio;							// ROM 0x0c101adc (unnamed)

// The nominal mean and standard deviation of each of the eight gap
// Gaussians, for a writer of ordinary habits.
extern const Fixed	kSegGapNominal[8][2];

// Whether a stroke begins a new word, and why:
//
// | | |
// |---|---|
// | 0 | the same word |
// | 1 | the gap before it was too wide (`SegmentWordXGap`) |
// | 2 | the writer went back (`SegmentWordBack`) |
// | 3 | the writer went down a line (`SegmentWordVert`) |
//
const long	kSegWordSame		= 0;
const long	kSegWordWideGap		= 1;
const long	kSegWordWentBack	= 2;
const long	kSegWordWentDown	= 3;

// `strength` is set to nought or to one by whichever test answered.
// `startSize` is `fRun[0]` as the word began, `wordSize` is
// `WordRecog::fWordSize`, and `run` is the twenty-two running
// measurements of the hand - every threshold below is scaled by how
// big this writing has turned out against them, so the whole of this
// is measured in the writer's own units rather than in pixels.
long	SegmentWord(const SegWordInk* ink, const SegWordRef* ref, Fixed startSize,
				Fixed wordSize, const Fixed* run, Fixed* strength);	// ROM 0x001d259c SegmentWord
// ... the first two tests without the third.
long	SegmentWordBkVt(const SegWordInk* ink, const SegWordRef* ref, Fixed startSize,
				Fixed wordSize, const Fixed* run, Fixed* strength);	// ROM 0x001d26e8 SegmentWordBkVt

// The writer went back: the new stroke lies to the left of where the
// reference starts by more than the writing's own size allows.
Boolean	SegmentWordBack(const SegWordInk* ink, const SegWordRef* ref, Fixed startSize,
				Fixed wordSize, const Fixed* run, Fixed* strength);	// ROM 0x001d27d4 SegmentWordBack
// The writer went down a line: the top, the bottom and the middle have
// all moved out of the band the reference allows.
Boolean	SegmentWordVert(const SegWordInk* ink, const SegWordRef* ref, Fixed startSize,
				Fixed wordSize, const Fixed* run, Fixed* strength);	// ROM 0x001d2900 SegmentWordVert
// The gap before it was too wide: the gap measured against the eight
// running Gaussians `WordRecog::fRun` keeps about the writer's hand.
Boolean	SegmentWordXGap(const SegWordInk* ink, const SegWordRef* ref, Fixed startSize,
				Fixed wordSize, const Fixed* run, Fixed* strength);	// ROM 0x001d3064 SegmentWordXGap

#endif	/* __SEGMENT_H */
