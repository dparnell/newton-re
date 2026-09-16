/*
	File:		compression/ArithmeticCompression.cpp

	Contains:	TArithmeticCompressor and TArithmeticDecompressor
				(ArithmeticCompression.h).

	Reconstructed from the MP2100 D ROM; each function cites its origin.
	The ROM's operator new/delete here are the memory manager's.
*/

#include "ArithmeticCompression.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"

#include <stdio.h>

extern const ExceptionName exCompressionException;		// "evt.ex.comp"

const long kArithmeticTableBytes = (kArithmeticSymbols + 1) * sizeof(long);		// (the ROM's 0x408: 258 words)


// range / (total * 16) to five bits: the coder's stand-in for a division.
// (The ROM accumulates the products bit by bit; this is the same value.)
static ULong32
Quotient5(ULong32 range, ULong32 total16)
{
	ULong32 q = 0;
	ULong32 r = range;
	for (int i = 0; i < 4; i++)
	{
		q <<= 1;
		if (r >= total16)
		{
			r -= total16;
			q |= 1;
		}
		r <<= 1;
	}
	q <<= 1;
	if (r >= total16)
		q |= 1;
	return q;
}


/* -------------------------------------------------------------------------------
	The model (the same code in both coders)
------------------------------------------------------------------------------- */

// ROM 0x00036d30 StartModel__21TArithmeticCompressorFv
// Every symbol equally likely (the increment's worth), bytes in order.
void
TArithmeticCompressor::StartModel()
{
	for (int i = 0; i < 256; i++)
	{
		fCharToIndex[i] = i + 1;
		fIndexToChar[i + 1] = (UByte) i;
	}
	fIncrement = 1;
	ULong32 t;
	do
	{
		t = fIncrement;
		fIncrement = t * 2;
	} while (t * 0x202 < 0x4000001);
	fCumFreq[kArithmeticSymbols] = 0;
	for (int i = kArithmeticSymbols; i > 0; i--)
	{
		fFreq[i] = fIncrement;
		fCumFreq[i - 1] = fCumFreq[i] + fIncrement;
	}
	fFreq[0] = 0;
}


// ROM 0x00036dc8 UpdateModel__21TArithmeticCompressorFi
// The symbol moves to the front of the symbols with its frequency, gains
// the increment; the whole model is halved when the total gets too big.
void
TArithmeticCompressor::UpdateModel(int symbol)
{
	int i = symbol;
	while (fFreq[i] == fFreq[i - 1])
		i--;
	if (i < symbol)
	{
		UByte ci = fIndexToChar[i];
		UByte cs = fIndexToChar[symbol];
		fIndexToChar[i] = cs;
		fIndexToChar[symbol] = ci;
		fCharToIndex[ci] = symbol;
		fCharToIndex[cs] = i;
	}
	fFreq[i] += fIncrement;
	while (i > 0)
	{
		i--;
		fCumFreq[i] += fIncrement;
	}
	if ((ULong32) fCumFreq[0] <= kArithmeticMaxTotal)
		return;
	fCumFreq[kArithmeticSymbols] = 0;
	for (i = kArithmeticSymbols; i > 0; i--)
	{
		ULong32 f = ((ULong32) fFreq[i] + 1) >> 1;
		fFreq[i] = f;
		fCumFreq[i - 1] = fCumFreq[i] + f;
	}
	if (fIncrement > 1)
		fIncrement >>= 1;
}


// ROM 0x00037514 StartModel__23TArithmeticDecompressorFv
void
TArithmeticDecompressor::StartModel()
{
	for (int i = 0; i < 256; i++)
	{
		fCharToIndex[i] = i + 1;
		fIndexToChar[i + 1] = (UByte) i;
	}
	fIncrement = 1;
	ULong32 t;
	do
	{
		t = fIncrement;
		fIncrement = t * 2;
	} while (t * 0x202 < 0x4000001);
	fCumFreq[kArithmeticSymbols] = 0;
	for (int i = kArithmeticSymbols; i > 0; i--)
	{
		fFreq[i] = fIncrement;
		fCumFreq[i - 1] = fCumFreq[i] + fIncrement;
	}
	fFreq[0] = 0;
}


// ROM 0x000375ac UpdateModel__23TArithmeticDecompressorFi
void
TArithmeticDecompressor::UpdateModel(int symbol)
{
	int i = symbol;
	while (fFreq[i] == fFreq[i - 1])
		i--;
	if (i < symbol)
	{
		UByte ci = fIndexToChar[i];
		UByte cs = fIndexToChar[symbol];
		fIndexToChar[i] = cs;
		fIndexToChar[symbol] = ci;
		fCharToIndex[ci] = symbol;
		fCharToIndex[cs] = i;
	}
	fFreq[i] += fIncrement;
	while (i > 0)
	{
		i--;
		fCumFreq[i] += fIncrement;
	}
	if ((ULong32) fCumFreq[0] <= kArithmeticMaxTotal)
		return;
	fCumFreq[kArithmeticSymbols] = 0;
	for (i = kArithmeticSymbols; i > 0; i--)
	{
		ULong32 f = ((ULong32) fFreq[i] + 1) >> 1;
		fFreq[i] = f;
		fCumFreq[i - 1] = fCumFreq[i] + f;
	}
	if (fIncrement > 1)
		fIncrement >>= 1;
}


/* -------------------------------------------------------------------------------
	TArithmeticCompressor
------------------------------------------------------------------------------- */

PROTOCOL_IMPL_SOURCE_MACRO(TArithmeticCompressor)		// ROM 0x00036d04 Sizeof__21TArithmeticCompressorSFv
PROTOCOL_CLASSINFO(TArithmeticCompressor, "TCallbackCompressor", "", 0, 0, nil)	// ROM 0x0037fcec ClassInfo__21TArithmeticCompressorSFv


// ROM 0x00036d0c New__21TArithmeticCompressorFv
TArithmeticCompressor*
TArithmeticCompressor::New()
{
	fCumFreq = nil;
	fIndexToChar = nil;
	fCharToIndex = nil;
	fFreq = nil;
	fAdaptive = false;
	fOwnsTables = true;
	return this;
}


// ROM 0x000371ac Delete__21TArithmeticCompressorFv
// (nothing: the tables are Cleanup's, which no one calls at the end - the ROM)
void
TArithmeticCompressor::Delete()
{ }


// ROM 0x00037970 Cleanup__21TArithmeticCompressorFv
void
TArithmeticCompressor::Cleanup()
{
	if (!fOwnsTables)
		return;
	DisposPtr((Ptr) fCumFreq);
	DisposPtr((Ptr) fIndexToChar);
	DisposPtr((Ptr) fFreq);
	DisposPtr((Ptr) fCharToIndex);
	fCumFreq = nil;
	fIndexToChar = nil;
	fCharToIndex = nil;
	fFreq = nil;
	fAdaptive = false;
	fOwnsTables = true;
}


// ROM 0x000379d0 Init__21TArithmeticCompressorFPv
// With no model, an adaptive one of our own; else the given tables.
NewtonErr
TArithmeticCompressor::Init(void* model)
{
	NewtonErr err = noErr;
	Cleanup();
	if (model == nil)
	{
		fAdaptive = true;
		fOwnsTables = true;
		fCumFreq = (long*) NewPtr(kArithmeticTableBytes);
		fFreq = (long*) NewPtr(kArithmeticTableBytes);
		fIndexToChar = (UByte*) NewPtr(kArithmeticSymbols + 1);
		fCharToIndex = (long*) NewPtr(256 * sizeof(long));
		if (fCumFreq == nil || fFreq == nil || fIndexToChar == nil || fCharToIndex == nil)
			return kError_No_Memory;
	}
	else
	{
		ArithmeticModel* m = (ArithmeticModel*) model;
		fOwnsTables = false;
		fCumFreq = m->fCumFreq;
		fCharToIndex = m->fCharToIndex;
		fIndexToChar = m->fIndexToChar;
		fFreq = m->fFreq;
		fAdaptive = m->fAdaptive;
	}
	Reset();
	if (fCumFreq == nil || fCharToIndex == nil)
		err = kError_Bad_Parameters;
	if (fAdaptive && (fIndexToChar == nil || fFreq == nil))
		err = kError_Bad_Parameters;
	return err;
}


// ROM 0x00037ae4 Reset__21TArithmeticCompressorFv
NewtonErr
TArithmeticCompressor::Reset()
{
	if (fAdaptive)
		StartModel();
	StartOutputtingBits();
	fOut = fBuffer;
	fBufferEnd = fBuffer + kArithmeticBufferSize;
	fLow = kArithmeticHalf;
	fRange = kArithmeticHalf;
	fBitsToFollow = 0;
	return noErr;
}


// ROM 0x00036ecc StartOutputtingBits__21TArithmeticCompressorFv
void
TArithmeticCompressor::StartOutputtingBits()
{
	fBitBuffer = 0;
	fBitsToGo = 8;
}


// ROM 0x00036ee0 WriteByte__21TArithmeticCompressorFUc
// Into the buffer; a full buffer goes to the write proc, whose error is
// thrown.
void
TArithmeticCompressor::WriteByte(UByte b)
{
	*fOut++ = b;
	if (fOut < fBufferEnd)
		return;
	NewtonErr err = fWriteProc(fRefCon, fBuffer, kArithmeticBufferSize, false);
	if (err != noErr)
		Throw(exCompressionException, (void*) (Long) err, nil);
	fOut = fBuffer;
}


// ROM 0x00036f4c FlushBits__21TArithmeticCompressorFv
// The buffered bytes, then the last partial byte, marked last.
void
TArithmeticCompressor::FlushBits()
{
	NewtonErr err = fWriteProc(fRefCon, fBuffer, fOut - fBuffer, false);
	if (err != noErr)
		Throw(exCompressionException, (void*) (Long) err, nil);
	fBitBuffer >>= fBitsToGo;
	err = fWriteProc(fRefCon, &fBitBuffer, 1, true);
	if (err != noErr)
		Throw(exCompressionException, (void*) (Long) err, nil);
}


// a bit into the byte being built, least significant first
#define OUTPUT_BIT(bit) \
	do { \
		if (fBitsToGo == 0) { WriteByte(fBitBuffer); fBitsToGo = 8; } \
		fBitBuffer >>= 1; \
		if (bit) fBitBuffer |= 0x80; \
		fBitsToGo--; \
	} while (0)


// ROM 0x00037bf8 NarrowRegion__21TArithmeticCompressorFi
// The interval shrinks to the symbol's share: cumulative frequencies count
// down, so symbol s covers [cumFreq[s], cumFreq[s-1]) of the total.
void
TArithmeticCompressor::NarrowRegion(int symbol)
{
	ULong32 q = Quotient5(fRange, (ULong32) fCumFreq[0] << 4);
	if (symbol == 1)
	{
		ULong32 below = q * (ULong32) fCumFreq[1];
		fLow += below;
		fRange = fRange - below;
	}
	else
	{
		ULong32 below = q * (ULong32) fCumFreq[symbol];
		ULong32 above = q * (ULong32) fCumFreq[symbol - 1];
		fLow += below;
		fRange = above - below;
	}
}


// ROM 0x00037d2c PushOutBits__21TArithmeticCompressorFv
// Renormalisation: settled top bits go out (with the opposite bits that
// followed an undecided middle), the interval doubles.
void
TArithmeticCompressor::PushOutBits()
{
	while (fRange <= kArithmeticQuarter)
	{
		if (fLow >= kArithmeticHalf)
		{
			OUTPUT_BIT(1);
			while (fBitsToFollow > 0)
			{
				OUTPUT_BIT(0);
				fBitsToFollow--;
			}
			fLow -= kArithmeticHalf;
		}
		else if (fLow + fRange > kArithmeticHalf)
		{
			fBitsToFollow++;
			fLow -= kArithmeticQuarter;
		}
		else
		{
			OUTPUT_BIT(0);
			while (fBitsToFollow > 0)
			{
				OUTPUT_BIT(1);
				fBitsToFollow--;
			}
		}
		fLow <<= 1;
		fRange <<= 1;
	}
}


// ROM 0x00037b34 WriteChunk__21TArithmeticCompressorFPvl
NewtonErr
TArithmeticCompressor::WriteChunk(void* data, long size)
{
	NewtonErr err = noErr;
	newton_try
	{
		for (long i = 0; i < size; i++)
		{
			int symbol = fCharToIndex[((UByte*) data)[i]];
			NarrowRegion(symbol);
			PushOutBits();
			if (fAdaptive)
				UpdateModel(symbol);
		}
	}
	newton_catch(exCompressionException)
	{
		err = (NewtonErr) (Long) CurrentException()->data;
	}
	end_try;
	return err;
}


// ROM 0x00037ed8 Flush__21TArithmeticCompressorFv
// The end symbol, then enough bits to pin the interval, then the buffers.
NewtonErr
TArithmeticCompressor::Flush()
{
	NewtonErr err = noErr;
	newton_try
	{
		NarrowRegion(kArithmeticEOF);
		PushOutBits();
		for (;;)
		{
			if (fLow + (fRange >> 1) >= kArithmeticHalf)
			{
				OUTPUT_BIT(1);
				while (fBitsToFollow > 0)
				{
					OUTPUT_BIT(0);
					fBitsToFollow--;
				}
				ULong32 low = fLow;
				fRange = low + fRange - kArithmeticHalf;
				fLow = low >= kArithmeticHalf ? low - kArithmeticHalf : 0;
			}
			else
			{
				OUTPUT_BIT(0);
				while (fBitsToFollow > 0)
				{
					OUTPUT_BIT(1);
					fBitsToFollow--;
				}
				if (fLow + fRange > kArithmeticHalf)
					fRange = kArithmeticHalf - fLow;
			}
			if (fRange == kArithmeticHalf)
				break;
			fLow <<= 1;
			fRange <<= 1;
		}
		FlushBits();
	}
	newton_catch(exCompressionException)
	{
		err = (NewtonErr) (Long) CurrentException()->data;
	}
	end_try;
	return err;
}


/* -------------------------------------------------------------------------------
	TArithmeticDecompressor
------------------------------------------------------------------------------- */

PROTOCOL_IMPL_SOURCE_MACRO(TArithmeticDecompressor)		// ROM 0x00036fd0 Sizeof__23TArithmeticDecompressorSFv
PROTOCOL_CLASSINFO(TArithmeticDecompressor, "TCallbackDecompressor", "", 0, 0, nil)	// ROM 0x0037fd84 ClassInfo__23TArithmeticDecompressorSFv


// ROM 0x00036fd8 New__23TArithmeticDecompressorFv
TArithmeticDecompressor*
TArithmeticDecompressor::New()
{
	fCumFreq = nil;
	fIndexToChar = nil;
	fCharToIndex = nil;
	fFreq = nil;
	fAdaptive = false;
	fFirst = true;
	// DEVIATION: the ROM leaves fOwnsTables as the memory came (Cleanup reads
	// it before Init has set anything); a fresh instance owns nothing
	fOwnsTables = false;
	return this;
}


// ROM 0x00036ffc Delete__23TArithmeticDecompressorFv
// (sic: the tables go if the model is adaptive, whoever owns them)
void
TArithmeticDecompressor::Delete()
{
	if (!fAdaptive)
		return;
	DisposPtr((Ptr) fCumFreq);
	DisposPtr((Ptr) fIndexToChar);
	DisposPtr((Ptr) fFreq);
	DisposPtr((Ptr) fCharToIndex);
}


// ROM 0x0003703c Cleanup__23TArithmeticDecompressorFv
void
TArithmeticDecompressor::Cleanup()
{
	if (!fOwnsTables)
		return;
	DisposPtr((Ptr) fCumFreq);
	DisposPtr((Ptr) fIndexToChar);
	DisposPtr((Ptr) fFreq);
	DisposPtr((Ptr) fCharToIndex);
	fCumFreq = nil;
	fIndexToChar = nil;
	fCharToIndex = nil;
	fFreq = nil;
	fAdaptive = false;
	fOwnsTables = true;
}


// ROM 0x0003709c Init__23TArithmeticDecompressorFPv
NewtonErr
TArithmeticDecompressor::Init(void* model)
{
	NewtonErr err = noErr;
	Cleanup();
	if (model == nil)
	{
		fAdaptive = true;
		// DEVIATION: the tables made here are ours to free (the ROM does not say so)
		fOwnsTables = true;
		fCumFreq = (long*) NewPtr(kArithmeticTableBytes);
		fFreq = (long*) NewPtr(kArithmeticTableBytes);
		fIndexToChar = (UByte*) NewPtr(kArithmeticSymbols + 1);
		fCharToIndex = (long*) NewPtr(256 * sizeof(long));
		if (fCumFreq == nil || fFreq == nil || fIndexToChar == nil || fCharToIndex == nil)
			return kError_No_Memory;
	}
	else
	{
		ArithmeticModel* m = (ArithmeticModel*) model;
		fOwnsTables = false;
		fCumFreq = m->fCumFreq;
		fCharToIndex = m->fCharToIndex;
		fIndexToChar = m->fIndexToChar;
		fFreq = m->fFreq;
		fAdaptive = m->fAdaptive;
	}
	Reset();
	if (fCumFreq == nil || fCharToIndex == nil)
		err = kError_Bad_Parameters;
	if (fAdaptive && (fIndexToChar == nil || fFreq == nil))
		err = kError_Bad_Parameters;
	return err;
}


// ROM 0x000371b0 Reset__23TArithmeticDecompressorFv
NewtonErr
TArithmeticDecompressor::Reset()
{
	if (fAdaptive)
		StartModel();
	StartReadingBits();
	fBufferEnd = fBuffer;
	fIn = fBuffer;
	fFirst = true;
	return noErr;
}


// ROM 0x000376b0 StartReadingBits__23TArithmeticDecompressorFv
void
TArithmeticDecompressor::StartReadingBits()
{
	fBitsToGo = 0;
	fZeroBytes = 0;
}


// ROM 0x00037878 ReadByte__23TArithmeticDecompressorFv
// From the buffer, refilled through the read proc; past the end of the
// data zeros are made up, four bytes' worth, then the end is thrown.
UByte
TArithmeticDecompressor::ReadByte()
{
	if (fIn >= fBufferEnd)
	{
		long size = kArithmeticBufferSize;
		Boolean underflow = false;
		NewtonErr err = fReadProc(fRefCon, fBuffer, &size, &underflow);
		if ((size == 0 && underflow) || (err != noErr && err != kArithmeticErr_EndOfData))
		{
			if (fZeroBytes * 8 > 0x1f)
				Throw(exCompressionException, (void*) (Long) kArithmeticErr_EndOfData, nil);
			fZeroBytes++;
			return 0;
		}
		fIn = fBuffer;
		fBufferEnd = fBuffer + size;
	}
	return *fIn++;
}


// the next bit, least significant first within each byte
#define INPUT_BIT(into) \
	do { \
		if (fBitsToGo == 0) { fBitBuffer = ReadByte(); fBitsToGo = 8; } \
		into = fBitBuffer & 1; \
		fBitBuffer >>= 1; \
		fBitsToGo--; \
	} while (0)


// ROM 0x000373e0 NarrowRegion__23TArithmeticDecompressorFi
void
TArithmeticDecompressor::NarrowRegion(int symbol)
{
	ULong32 q = Quotient5(fRange, (ULong32) fCumFreq[0] << 4);
	if (symbol == 1)
	{
		ULong32 below = q * (ULong32) fCumFreq[1];
		fLow += below;
		fRange = fRange - below;
	}
	else
	{
		ULong32 below = q * (ULong32) fCumFreq[symbol];
		ULong32 above = q * (ULong32) fCumFreq[symbol - 1];
		fLow += below;
		fRange = above - below;
	}
}


// ROM 0x000376c0 FindSymbol__23TArithmeticDecompressorFv
// The symbol whose share holds the value: (value - low) / q found bit by
// bit from 2^26 down, the symbol advanced as the cumulative frequency is
// narrowed down.
int
TArithmeticDecompressor::FindSymbol()
{
	ULong32 q = Quotient5(fRange, (ULong32) fCumFreq[0] << 4);
	ULong32 diff = fValue - fLow;
	ULong32 bit = 0x4000000;
	ULong32 scaled = q << 26;
	ULong32 cum = 0;
	int symbol = 1;
	if (fCumFreq[1] == 0)
		return symbol;
	do
	{
		if (diff >= scaled)
		{
			diff -= scaled;
			cum += bit;
		}
		else
		{
			ULong32 bound = cum + bit;
			if ((ULong32) fCumFreq[symbol] >= bound)
			{
				do
				{
					symbol++;
				} while ((ULong32) fCumFreq[symbol] >= bound);
			}
		}
		bit >>= 1;
		scaled >>= 1;
	} while ((ULong32) fCumFreq[symbol] > cum);
	return symbol;
}


// ROM 0x0003779c DiscardBits__23TArithmeticDecompressorFv
// The decoder's renormalisation: settled bits leave low and the value,
// which takes the next bit of input.
void
TArithmeticDecompressor::DiscardBits()
{
	while (fRange <= kArithmeticQuarter)
	{
		if (fLow < kArithmeticHalf)
		{
			if (fLow + fRange > kArithmeticHalf)
			{
				fLow -= kArithmeticQuarter;
				fValue -= kArithmeticQuarter;
			}
		}
		else
		{
			fLow -= kArithmeticHalf;
			fValue -= kArithmeticHalf;
		}
		fLow <<= 1;
		fRange <<= 1;
		fValue <<= 1;
		ULong32 b;
		INPUT_BIT(b);
		fValue += b;
	}
}


// ROM 0x000371f0 ReadChunk__23TArithmeticDecompressorFPvPlPUc
// Up to *size bytes; at the end symbol *size is what was read and
// *underflow is set.  The first call reads the 32-bit value (whose first
// bit is always 1).
NewtonErr
TArithmeticDecompressor::ReadChunk(void* into, long* size, Boolean* underflow)
{
	NewtonErr err = noErr;
	newton_try
	{
		if (fFirst)
		{
			ULong32 b;
			INPUT_BIT(b);
			fValue = b;
			if (b != 1)
				printf("Arithmetic Encoder: Bad input file (1)\r");
			for (int i = 1; i < 32; i++)
			{
				fValue <<= 1;
				INPUT_BIT(b);
				fValue += b;
			}
			fLow = kArithmeticHalf;
			fRange = kArithmeticHalf;
			fFirst = false;
		}
		for (long i = 0; i < *size; i++)
		{
			int symbol = FindSymbol();
			NarrowRegion(symbol);
			DiscardBits();
			if (symbol == kArithmeticEOF)
			{
				*size = i;
				*underflow = true;
				break;
			}
			*underflow = false;
			((UByte*) into)[i] = fIndexToChar[symbol];
			if (fAdaptive)
				UpdateModel(symbol);
		}
	}
	newton_catch(exCompressionException)
	{
		err = (NewtonErr) (Long) CurrentException()->data;
	}
	end_try;
	return err;
}
