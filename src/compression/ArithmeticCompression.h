/*
	File:		compression/ArithmeticCompression.h

	Contains:	The arithmetic coder - TArithmeticCompressor and
				TArithmeticDecompressor, callback compressors coding bytes
				with an adaptive (or a given, fixed) order-0 frequency model.
				Not in the DDK; follows the ROM (0x00036d04-0x00038110).

	The coder is Witten, Neal and Cleary's with 32-bit low/range
	arithmetic, done without division: the range is scaled by a 5-bit
	quotient of range / (total * 16), and the decoder finds the symbol by
	a bit-serial division against that quotient.  Symbols are 1..256 for
	the bytes and 257 (kArithmeticEOF) for the end; the model keeps the
	symbols ordered by frequency (fIndexToChar/fCharToIndex swap on each
	update), adds fIncrement (2^18 at the start) to the coded symbol's
	frequency and halves everything when the total passes 2^27.  Bits go
	out least significant first within each byte; the compressor buffers
	128 bytes for its write proc, the decompressor reads 128 at a time
	through its read proc (NewtonErr (*)(void* refCon, void* into, long*
	size, Boolean* underflow)) and reads zeros, four bytes' worth, once the
	source is used up.  Errors from the procs travel as evt.ex.comp
	exceptions with the error as data, caught by WriteChunk/Flush/ReadChunk.

	Layouts (ROM; compressor 0xd4, decompressor 0xd8): TCallback*
	(fWriteProc/fReadProc +0x10, fRefCon +0x14), fLow +0x18, fRange +0x1c,
	fValue +0x20 (decompressor), fBitsToFollow +0x24 (compressor), the
	flags +0x28-0x2a, fCumFreq +0x2c, fCharToIndex +0x30, fIndexToChar
	+0x34, fFreq +0x38, fIncrement +0x3c, fBitBuffer +0x40, fBitsToGo
	+0x44, then the 128-byte buffer (+0x48 / +0x4c after fZeroBytes) and
	its end and cursor pointers.
*/

#ifndef __ARITHMETICCOMPRESSION_H
#define __ARITHMETICCOMPRESSION_H

#include "Compression.h"
#include "Pushpopper.h"		// ULong32
#include "NewtErrors.h"

const long kArithmeticSymbols = 0x101;			// symbols 1..0x100 are the bytes, 0x101 the end
const long kArithmeticEOF = 0x101;
const ULong kArithmeticBufferSize = 0x80;
const ULong32 kArithmeticHalf = 0x80000000;
const ULong32 kArithmeticQuarter = 0x40000000;
const ULong32 kArithmeticMaxTotal = 0x8000000;	// the frequencies are halved past this
const NewtonErr kArithmeticErr_EndOfData = ERRBASE_COMPRESSION - 101;	// (the ROM's -32101, unnamed in the DDK)

// a fixed model, given to Init instead of nil
struct ArithmeticModel
{
	long*			fCumFreq;			// +0x00  kArithmeticSymbols + 1 entries: fCumFreq[0] is the total, decreasing
	long*			fCharToIndex;		// +0x04  256
	UByte*			fIndexToChar;		// +0x08  kArithmeticSymbols + 1
	long*			fFreq;				// +0x0c  kArithmeticSymbols + 1
	Boolean			fAdaptive;			// +0x10  update it while coding
};


PROTOCOL TArithmeticCompressor : public TCallbackCompressor
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TArithmeticCompressor);

	TArithmeticCompressor*	New();
	void			Delete();

	NewtonErr		Init(void* model);
	NewtonErr		Reset();
	NewtonErr		WriteChunk(void* data, long size);
	NewtonErr		Flush();

	void			Cleanup();
	void			StartModel();
	void			UpdateModel(int symbol);
	void			NarrowRegion(int symbol);
	void			PushOutBits();
	void			StartOutputtingBits();
	void			WriteByte(UByte b);
	void			FlushBits();

	ULong32			fLow;				// +0x18
	ULong32			fRange;				// +0x1c
	ULong32			fUnknown20;			// +0x20
	ULong			fBitsToFollow;		// +0x24
	Boolean			fAdaptive;			// +0x28
	Boolean			fOwnsTables;		// +0x29
	long*			fCumFreq;			// +0x2c
	long*			fCharToIndex;		// +0x30
	UByte*			fIndexToChar;		// +0x34
	long*			fFreq;				// +0x38
	ULong32			fIncrement;			// +0x3c
	UByte			fBitBuffer;			// +0x40
	long			fBitsToGo;			// +0x44
	UByte			fBuffer[kArithmeticBufferSize];	// +0x48
	UByte*			fBufferEnd;			// +0xc8
	UByte*			fOut;				// +0xcc
};


PROTOCOL TArithmeticDecompressor : public TCallbackDecompressor
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TArithmeticDecompressor);

	TArithmeticDecompressor*	New();
	void			Delete();

	NewtonErr		Init(void* model);
	NewtonErr		Reset();
	NewtonErr		ReadChunk(void* into, long* size, Boolean* underflow);

	void			Cleanup();
	void			StartModel();
	void			UpdateModel(int symbol);
	void			NarrowRegion(int symbol);
	int				FindSymbol();
	void			DiscardBits();
	void			StartReadingBits();
	UByte			ReadByte();

	ULong32			fLow;				// +0x18
	ULong32			fRange;				// +0x1c
	ULong32			fValue;				// +0x20
	ULong			fUnknown24;			// +0x24
	Boolean			fFirst;				// +0x28  the 32 bits of the value are still to be read
	Boolean			fAdaptive;			// +0x29
	Boolean			fOwnsTables;		// +0x2a
	long*			fCumFreq;			// +0x2c
	long*			fCharToIndex;		// +0x30
	UByte*			fIndexToChar;		// +0x34
	long*			fFreq;				// +0x38
	ULong32			fIncrement;			// +0x3c
	UByte			fBitBuffer;			// +0x40
	long			fBitsToGo;			// +0x44
	long			fZeroBytes;			// +0x48  bytes made up past the end of the data
	UByte			fBuffer[kArithmeticBufferSize];	// +0x4c
	UByte*			fBufferEnd;			// +0xcc
	UByte*			fIn;				// +0xd0
};

#endif	/* __ARITHMETICCOMPRESSION_H */
