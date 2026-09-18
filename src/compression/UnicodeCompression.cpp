/*
	File:		compression/UnicodeCompression.cpp

	Contains:	TUnicodeCompressor and TUnicodeDecompressor
				(UnicodeCompression.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	gUnicodeLookupTable is in UnicodeTables.cpp, generated from the ROM.
*/

#include "UnicodeCompression.h"
#include "OSErrors.h"

extern const unsigned char	gUnicodeLookupTable[32];

// whether characters with this high byte are run-coded
static inline Boolean
IsRunCoded(UByte high)
{
	return (gUnicodeLookupTable[high >> 3] & (0x80 >> (high & 7))) != 0;
}


/* -------------------------------------------------------------------------------
	TUnicodeCompressor
------------------------------------------------------------------------------- */

PROTOCOL_IMPL_SOURCE_MACRO(TUnicodeCompressor)		// ROM 0x00256c74 Sizeof__18TUnicodeCompressorSFv
PROTOCOL_CLASSINFO(TUnicodeCompressor, "TCallbackCompressor", "", 0, 0, nil)	// ROM 0x00389b50 ClassInfo__18TUnicodeCompressorSFv


// ROM 0x00256c7c New__18TUnicodeCompressorFv
TUnicodeCompressor*
TUnicodeCompressor::New()
{
	fCount = 0;
	fRunLength = 0;
	return this;
}


// ROM 0x00256ecc Delete__18TUnicodeCompressorFv
void
TUnicodeCompressor::Delete()
{ }


// ROM 0x00256ed0 Init__18TUnicodeCompressorFPv
NewtonErr
TUnicodeCompressor::Init(void* /*refCon*/)
{
	return noErr;
}


// ROM 0x00256ed8 Reset__18TUnicodeCompressorFv
NewtonErr
TUnicodeCompressor::Reset()
{
	fCount = 0;
	fRunLength = 0;
	return noErr;
}


// a byte into the buffer, the buffer to the write proc when full (whose
// result the ROM does not look at)
#define PUT_BYTE(b) \
	do { \
		if (fCount > 0x7f) { fWriteProc(fRefCon, fBuffer, fCount, false); fCount = 0; } \
		fBuffer[fCount++] = (b); \
	} while (0)


// ROM 0x00256eec WriteRun__18TUnicodeCompressorFv
// The gathered run: high byte, count, low bytes.
NewtonErr
TUnicodeCompressor::WriteRun()
{
	if (fRunLength != 0)
	{
		PUT_BYTE(fRunHigh);
		PUT_BYTE((UByte) fRunLength);
		for (ULong i = 0; i < fRunLength; i++)
			PUT_BYTE(fRunLow[i]);
		fRunLength = 0;
	}
	return noErr;
}


// ROM 0x0025700c WriteChunk__18TUnicodeCompressorFPvl
// Whole characters only; a character continues the run if its high byte
// is the run's, else the run goes out and a new one starts - or the
// character goes out as is when its block is not run-coded.
NewtonErr
TUnicodeCompressor::WriteChunk(void* data, long size)
{
	if (size & 1)
		return kError_Bad_Parameters;
	const UByte* p = (const UByte*) data;
	for (long i = 0; i < size / 2; i++, p += 2)
	{
		UByte high = p[0];
		UByte low = p[1];
		if (fRunLength != 0)
		{
			if (fRunHigh == high)
			{
				fRunLow[fRunLength++] = low;
				if (fRunLength > 0xfe)
				{
					NewtonErr err = WriteRun();
					if (err != noErr)
						return err;
				}
				continue;
			}
			NewtonErr err = WriteRun();
			if (err != noErr)
				return err;
		}
		if (IsRunCoded(high))
		{
			fRunLow[0] = low;
			fRunLength = 1;
			fRunHigh = high;
		}
		else
		{
			PUT_BYTE(high);
			PUT_BYTE(low);
		}
	}
	return noErr;
}


// ROM 0x0025719c Flush__18TUnicodeCompressorFv
NewtonErr
TUnicodeCompressor::Flush()
{
	NewtonErr err = WriteRun();
	if (err != noErr)
		return err;
	fWriteProc(fRefCon, fBuffer, fCount, true);
	fCount = 0;
	return noErr;		// (the ROM leaves r0 as the write proc's result)
}


/* -------------------------------------------------------------------------------
	TUnicodeDecompressor
------------------------------------------------------------------------------- */

PROTOCOL_IMPL_SOURCE_MACRO(TUnicodeDecompressor)		// ROM 0x002571dc Sizeof__20TUnicodeDecompressorSFv
PROTOCOL_CLASSINFO(TUnicodeDecompressor, "TCallbackDecompressor", "", 0, 0, nil)	// ROM 0x00389be4 ClassInfo__20TUnicodeDecompressorSFv


// ROM 0x002571e4 New__20TUnicodeDecompressorFv
TUnicodeDecompressor*
TUnicodeDecompressor::New()
{
	fRunIndex = 0;
	// DEVIATION: the ROM leaves these as the memory came; ReadChunk reads
	// both before anything has set them
	fRunCount = 0;
	fSourceDone = false;
	return this;
}


// ROM 0x00256c90 Delete__20TUnicodeDecompressorFv
void
TUnicodeDecompressor::Delete()
{ }


// ROM 0x00256c94 Init__20TUnicodeDecompressorFPv
NewtonErr
TUnicodeDecompressor::Init(void* /*refCon*/)
{
	return noErr;
}


// ROM 0x00256c9c Reset__20TUnicodeDecompressorFv
NewtonErr
TUnicodeDecompressor::Reset()
{
	fRunIndex = 0;
	return noErr;
}


// ROM 0x00256cb4 ReadChunk__20TUnicodeDecompressorFPvPlPUc
// Characters into the buffer until it is full or the source is used up
// (*size then tells how many bytes, and *underflow is set).
NewtonErr
TUnicodeDecompressor::ReadChunk(void* into, long* size, Boolean* underflow)
{
	if (*size & 1)
		return kError_Bad_Parameters;
	*underflow = false;
	UByte* out = (UByte*) into;
	long characters = *size / 2;
	for (long i = 0; i < characters; i++, out += 2)
	{
		if (fSourceDone && fRunCount == 0)
		{
			*size = i * 2;
			*underflow = true;
			return noErr;
		}
		if (fRunCount == 0)
		{
			long n = 1;
			NewtonErr err = fReadProc(fRefCon, &fHigh, &n, &fSourceDone);
			if (err != noErr)
				return err;
			n = 1;
			err = fReadProc(fRefCon, &fRunCount, &n, &fSourceDone);
			if (err != noErr)
				return err;
			out[0] = fHigh;
			if (!IsRunCoded(fHigh))
			{
				out[1] = fRunCount;			// not a count: the low byte
				fRunCount = 0;
			}
			else
			{
				n = fRunCount;
				err = fReadProc(fRefCon, fRunLow, &n, &fSourceDone);
				if (err != noErr)
					return err;
				out[1] = fRunLow[fRunIndex++];
				if (fRunCount <= fRunIndex)
				{
					fRunCount = 0;
					fRunIndex = 0;
				}
			}
		}
		else
		{
			out[0] = fHigh;
			out[1] = fRunLow[fRunIndex++];
			if (fRunCount <= fRunIndex)
			{
				fRunCount = 0;
				fRunIndex = 0;
			}
		}
	}
	if (fSourceDone && fRunCount == 0)
		*underflow = true;
	return noErr;
}
