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
// pointers): the point, y -1 for a pen-up, then what ExtrWordTrace_V
// marks it as.
struct tag_WORD_TRACE
{
	short		x;					// +00
	short		y;					// +02  -1: the pen was lifted
	short		f4;
	short		f6;
};
static_assert(sizeof(tag_WORD_TRACE) == 8, "a tag_WORD_TRACE is 8 bytes, as in the ROM");

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
