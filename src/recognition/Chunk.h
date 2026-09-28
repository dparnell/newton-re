/*
	File:		Chunk.h

	Contains:	The cursive reader's digit and number reader - ParaGraph's
				"chunk" reader - as far as it is reconstructed: the context
				GCTryToRecognize keeps for it and the recognition
				configuration it changes while a word it took for a number
				is read.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	In a field that allows numbers (rc +0xb6) the chunk reader goes
	first: `ChunkProcessor` cuts the writing into chunks, reads the
	digits in them (`Digits`, `SearchDigit_S/K/L`, `New_SearchDigit_V`,
	`SearchNumber`, `FindPound`, `RecognizeZCCW`, ...) and, when it found
	a number, `ChunkModifyRC` narrows the configuration to digits for the
	xr reader; after it `ChunkRestoreRC` puts it back, `ChunkSortAnswers`
	and `ChunkCorrectByLexDB` merge the number readings in.

	NOT YET RECONSTRUCTED: `ChunkProcessor` and everything under it
	(about 100 functions, 146 KB - docs/next-steps.md has the plan),
	`ChunkPatchXrdata`, `ChunkSortAnswers` and `ChunkCorrectByLexDB`.
	Without the processor no number is ever found, so GCTryToRecognize
	gives the context back straight away, as the ROM does for a word that
	is not a number.
*/

#ifndef __CHUNK_H
#define __CHUNK_H

#include "XrDomains.h"

struct xrdata_type;

// The chunk reader's context (ROM 0x48 bytes; DEVIATION: sizeof on the
// host, whose pointers are wider).
struct ChunkCtx
{
	void*			fData;			// +00  (given back by ChunkCleanUp)
	long			f04;
	void*			fData2;			// +08  (given back)
	void*			fData3;			// +0c  (given back)
	long			f10;
	long			fNumbers;		// +14  whether the processor found numbers (IsChunkNumbers)
	long			fNumbersOnly;	// +18  the writing is numbers alone (ChunkModifyRC's second way)
	long			f1C;
	long			fModified;		// +20  the configuration changed (ChunkModifyRC), to be put back
	long			f24;
	long			f28;
	long			f2C;
	UShort			fSaved[5];		// +30  rc +0x02, +0x08, +0x0a, +0x00, +0x90 as they were
	rc_type*		fRC;			// +3c
	xrdata_type*	fXr;			// +40
	rec_w_type*		fReadings;		// +44
};

// A point of the digit reader's own trace (ROM tag_WORD_TRACE, 8 bytes, no
// pointers): the point, y -1 for a pen-up, then a word of flags - what
// ExtrWordTrace_V marks it as (kTraceLow, kTraceHigh) and where
// GetLineApprox finds a stroke begins and ends (kTraceStrokeStart,
// kTraceStrokeEnd).
struct tag_WORD_TRACE
{
	short		x;					// +00
	short		y;					// +02  -1: the pen was lifted
	int32_t		fFlags;				// +04
};
static_assert(sizeof(tag_WORD_TRACE) == 8, "a tag_WORD_TRACE is 8 bytes, as in the ROM");

enum
{
	kTraceLow			= 0x01,		// a turn at the bottom (y at its greatest, the screen's y growing down)
	kTraceHigh			= 0x02,		// a turn at the top
	kTraceStrokeStart	= 0x04,		// the point after a pen-up
	kTraceStrokeEnd		= 0x08		// the point before one
};

// A node of the polyline GetLineApprox fits to the trace (ROM
// tag_wapx_type, 0x1c bytes, no pointers).  fDir is the direction
// (GetDirection's fifteen-degree steps) the line comes into the node from
// - at a corner (kApxCorner) the two directions either side packed as
// in | out << 8 - and fDirOut the one it leaves in.
struct tag_wapx_type
{
	int32_t		fIndex;				// +00  the trace point
	UByte		fFlags;				// +04  kApx...
	UByte		fFirst;				// +05  the first node a split made in its segment (GetLineApprox)
	UByte		f06[2];
	int32_t		fDir;				// +08
	int32_t		fDirOut;			// +0c
	int32_t		x;					// +10  the trace point's
	int32_t		y;					// +14
	int32_t		f18;
};
static_assert(sizeof(tag_wapx_type) == 0x1c, "a tag_wapx_type is 0x1c bytes, as in the ROM");

enum
{
	kApxStart		= 0x01,			// the trace point was a stroke's start (kTraceStrokeStart)
	kApxEnd			= 0x02,			// a stroke's end (kTraceStrokeEnd)
	kApxHigh		= 0x04,			// a turn at the top (kTraceHigh)
	kApxLow			= 0x08,			// a turn at the bottom (kTraceLow)
	kApxSegStart	= 0x10,			// the first node of a segment between two marked points
	kApxSegEnd		= 0x20,			// the last
	kApxCorner		= 0x40			// the direction turns by 120 degrees or more (SetAllDirections)
};

// The turns of the trace marked (kTraceLow/kTraceHigh), each stroke's
// first point and the turns the pen makes by more than *height (the
// height of the stroke half way up the strokes sorted by height) over
// divisor.  ==> 0, -1 for want of memory or with 100 strokes or more.
long	ExtrWordTrace_V(tag_WORD_TRACE* trace, long count, long divisor, long* height);	// ROM 0x0028877c ExtrWordTrace_V__FP14tag_WORD_TRACEiT2Pi
// The polyline through the marked points of the trace, each segment
// between two of them split at its furthest point until the chord is
// close enough (tolerance: a tenth of the chord over this).  ==> how many
// nodes, *nodes the block of them (the caller gives it back); -1 for want
// of memory or with 200 marked points or more.
long	GetLineApprox(tag_WORD_TRACE* trace, long count, long tolerance, tag_wapx_type** nodes);	// ROM 0x00285dc8 GetLineApprox__FP14tag_WORD_TRACEiT2PP13tag_wapx_type
long	SetAllDirections(tag_WORD_TRACE* trace, tag_wapx_type* nodes, long count);	// ROM 0x00286638 SetAllDirections__FP14tag_WORD_TRACEP13tag_wapx_typei

// The point between i1 and i2 furthest from the chord between them (a
// pen-up breaks a run; the middle of a run of equally far points).
long	v_MostFarFromChord(tag_WORD_TRACE* trace, long i1, long i2);	// ROM 0x0028686c v_MostFarFromChord__FP14tag_WORD_TRACEiT2
// The square of (x, y)'s distance from the segment's line, worked out in
// 32-bit integers without overflowing where it can help it.
long	v_QDistFromChord(long x1, long y1, long x2, long y2, long x, long y);	// ROM 0x0028694c v_QDistFromChord__FiN51

// The direction from (x1, y1) to (x2, y2) (y growing downwards) in
// twenty-four fifteen-degree steps counted anticlockwise from straight up:
// 0 and 23 either side of up, 5 and 6 of left, 11 and 12 of down, 17 and
// 18 of right.
long	GetDirection(long x1, long y1, long x2, long y2);		// ROM 0x0028646c GetDirection__FiN31

void	ChunkAllocCtx(void** ctx, rc_type* rc);			// ROM 0x002a65ec ChunkAllocCtx__FPPvP7rc_type
void	ChunkCleanUp(void** ctx);							// ROM 0x002a6404 ChunkCleanUp__FPPv - its three blocks and itself given back, *ctx nil
long	IsChunkNumbers(void* ctx);							// ROM 0x002a65cc IsChunkNumbers__FPv
void	ChunkModifyRC(void* ctx, rc_type* rc);				// ROM 0x002a6464 ChunkModifyRC__FPvP7rc_type
void	ChunkRestoreRC(void* ctx, rc_type* rc);				// ROM 0x002a654c ChunkRestoreRC__FPvP7rc_type
void*	ChunkWriteParamCtx(void* ctx, rc_type* rc, xrdata_type* xr, rec_w_type* readings);	// ROM 0x002a65dc ChunkWriteParamCtx__FPvP7rc_typeP11xrdata_typeP10rec_w_type - ==> &fReadings

#endif	/* __CHUNK_H */
