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

void	ChunkAllocCtx(void** ctx, rc_type* rc);			// ROM 0x002a65ec ChunkAllocCtx__FPPvP7rc_type
void	ChunkCleanUp(void** ctx);							// ROM 0x002a6404 ChunkCleanUp__FPPv - its three blocks and itself given back, *ctx nil
long	IsChunkNumbers(void* ctx);							// ROM 0x002a65cc IsChunkNumbers__FPv
void	ChunkModifyRC(void* ctx, rc_type* rc);				// ROM 0x002a6464 ChunkModifyRC__FPvP7rc_type
void	ChunkRestoreRC(void* ctx, rc_type* rc);				// ROM 0x002a654c ChunkRestoreRC__FPvP7rc_type
void*	ChunkWriteParamCtx(void* ctx, rc_type* rc, xrdata_type* xr, rec_w_type* readings);	// ROM 0x002a65dc ChunkWriteParamCtx__FPvP7rc_typeP11xrdata_typeP10rec_w_type - ==> &fReadings

#endif	/* __CHUNK_H */
