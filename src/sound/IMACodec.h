/*
	File:		sound/IMACodec.h

	Contains:	The IMA/DVI ADPCM sound codec - CompressIMA packs 16-bit
				linear PCM into 4 bits per sample, ExpandIMA unpacks it, and
				CheckState re-syncs a decoder to a block's header.  This is
				the standard IMA ADPCM (an adaptive step size chosen from an
				89-entry table, stepped by a per-nibble index adjustment),
				as the ROM (0x000e82f8-0x000e8628) codes the Newton's
				recorded sound.

				The compressed form is a run of 0x40-sample (kIMABlockSize)
				*blocks*.  A block is a 2-byte big-endian header - the
				block's initial predictor rounded to its top 9 bits, or'd
				with the 7-bit step index - followed by 0x20 bytes of 0x40
				nibbles (the first sample of a pair in the low nibble, the
				second in the high).  Interleaved channels are coded a block
				at a time (srcStride / numChannels select the channel and
				step over the others).

				The PCM the codec reads and writes is host-native `short`s;
				the ROM, big-endian, keeps them big-endian in memory, but
				the sample *values* are what the algorithm preserves (the
				compressed stream's bytes - the nibbles and the header - are
				kept exactly as the ROM lays them, so the two interoperate).

	The DDK has no header for these; reconstructed from the MP2x00 US ROM,
	each function citing its origin.  A TIMACodec (a TSoundCodec) wraps them
	for the sound server (NOT YET).
*/

#ifndef __IMACODEC_H
#define __IMACODEC_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif


const ULong kIMABlockSize = 0x40;			// PCM samples coded per block
const ULong kIMABlockBytes = 0x22;			// a coded block: 2 header + 0x20 nibble bytes
const long  kIMAMaxStepIndex = 0x58;		// the step table's last entry (88)


// A codec's running state: the last reconstructed sample and the current
// step-size index.  Both encoder and decoder track the same values, so a
// stream expands to exactly the samples the compressor reconstructed.
struct IMAState
{
	long	fPredictor;			// +0x00  the last reconstructed 16-bit sample
	short	fStepIndex;			// +0x04  index into the step-size table (0..0x58)

			IMAState() : fPredictor(0), fStepIndex(0) {}
};


// The step-size table (89 entries) and the per-nibble index adjustment
// (16 entries), read from the ROM; the standard IMA tables.
extern const short	kIMAStepTable[89];		// ROM 0x0034f5fc (unnamed)
extern const short	kIMAIndexTable[16];		// ROM 0x0034f5dc (unnamed)


// Pack numSamples 16-bit PCM samples (from src, taking every srcStride'th
// sample starting at channel) into dst as IMA blocks; state carries across
// calls.  numSamples is rounded down to whole blocks.
void	CompressIMA(const short* src, signed char* dst, ULong numSamples, IMAState* state, ULong srcStride, ULong channel);	// ROM 0x000e82f8 CompressIMA__FPsPScUlP8IMAStateN23

// Re-sync state to the block header src points at: if the header's step
// index or its predictor (within 0x80) disagree with state, reset to it.
void	CheckState(const signed char* src, IMAState* state);	// ROM 0x000e84a4 CheckState__FPScP8IMAState

// Unpack numBlocks IMA blocks (from src) into dst; outFormat selects the
// output: 0 = 8-bit unsigned mono, 1 = 8-bit stride 2, 2 = 16-bit,
// 3 = 16-bit stride 2 (a channel of an interleaved pair).  numChannels
// steps the source over the other channels' blocks.
void	ExpandIMA(const signed char* src, void* dst, IMAState* state, ULong numBlocks, ULong numChannels, ULong outFormat);	// ROM 0x000e8500 ExpandIMA__FPScT1P8IMAStateUlN24

#endif	/* __IMACODEC_H */
