/*
	File:		compression/ZippyCompression.cpp

	Contains:	TZippyCompressor, TZippyDecompressor and
				TZippyCallbackCompressor (ZippyCompression.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	StuffBits and ExpandValue move bits with byte windows shifted by the
	bit position in the ROM (big-endian words built on the stack); here
	they are written as the bit-string operations they amount to, which
	produce the same bytes.
*/

#include "ZippyCompression.h"
#include "LZCompression.h"		// fast_copy
#include "ByteOrder.h"
#include "NewtonMemory.h"
#include "NewtErrors.h"
#include "OSErrors.h"

#include <string.h>

// what a decompressed chunk answers when it does not fit
const NewtonErr kZippyErr_DestinationTooSmall = ERRBASE_COMPRESSION - 100;		// (the ROM's -32100, unnamed in the DDK)


/* -------------------------------------------------------------------------------
	TZippyCompressor
------------------------------------------------------------------------------- */

PROTOCOL_IMPL_SOURCE_MACRO(TZippyCompressor)		// ROM 0x00284744 Sizeof__16TZippyCompressorSFv
PROTOCOL_CLASSINFO(TZippyCompressor, "TCompressor", "", 0, 0, nil)	// ROM 0x0038996c ClassInfo__16TZippyCompressorSFv


// ROM 0x0028474c New__16TZippyCompressorFv
TZippyCompressor*
TZippyCompressor::New()
{
	return this;
}


// ROM 0x002849bc Delete__16TZippyCompressorFv
void
TZippyCompressor::Delete()
{ }


// ROM 0x002849c0 Init__16TZippyCompressorFPv
NewtonErr
TZippyCompressor::Init(void* /*refCon*/)
{
	return noErr;
}


// ROM 0x002849c8 Finish__16TZippyCompressorFPvUl
NewtonErr
TZippyCompressor::Finish(void* /*header*/, ULong /*headerSize*/)
{
	return noErr;		// (the ROM leaves r0 = this)
}


// ROM 0x00284758 HeaderSize__16TZippyCompressorFv
ULong
TZippyCompressor::HeaderSize()
{
	return 8;
}


// ROM 0x00284750 EstimatedCompressedSize__16TZippyCompressorFPvUl
ULong
TZippyCompressor::EstimatedCompressedSize(void* /*src*/, ULong srcSize)
{
	return srcSize + 8;
}


// ROM 0x002849cc InitCache__16TZippyCompressorFv
void
TZippyCompressor::InitCache()
{
	fCounter = 0;
	for (long i = 0; i < kZippyCacheSize; i++)
	{
		fUse[i] = -1;
		fCache[i] = 0;
	}
}


// ROM 0x002849f8 CacheAndCompress__16TZippyCompressorFUlP12ByteAccessor
// The code for a word: 0, a cache hit, a partial hit (the first entry that
// matches either way, in index order) or a new word into the least
// recently used entry.  The code's length in bits.
long
TZippyCompressor::CacheAndCompress(ULong32 word, ByteAccessor* code)
{
	code->fByte[4] = 0;
	memset(code->fByte, 0, 4);
	if (word == 0)
		return 2;
	long oldest = 0x7fffffff;
	long oldestIndex = 0;
	long now = ++fCounter;
	ULong32 key = word & 0xffffc007;
	for (long i = 0; i < kZippyCacheSize; i++)
	{
		if (fUse[i] < oldest)
		{
			oldest = fUse[i];
			oldestIndex = i;
		}
		if (fCache[i] == word)
		{
			fUse[i] = now;
			code->fByte[0] = (UByte) (0x80 | (i << 2));
			return 6;
		}
		if ((fCache[i] & 0xffffc007) == key)
		{
			fCache[i] = word;
			fUse[i] = fCounter;
			ULong32 middle = word & 0x3ff8;					// bits 13-3
			code->fByte[0] = (UByte) (0x40 | (i << 2) | ((middle >> 12) & 3));
			code->fByte[1] = (UByte) (middle >> 4);
			code->fByte[2] = (UByte) ((middle << 4) & 0xff);
			return 17;
		}
	}
	fCache[oldestIndex] = word;
	fUse[oldestIndex] = fCounter;
	ULong32 top = (word >> 2) | 0xc0000000;
	code->fByte[0] = (UByte) (top >> 24);
	code->fByte[1] = (UByte) (top >> 16);
	code->fByte[2] = (UByte) (top >> 8);
	code->fByte[3] = (UByte) top;
	code->fByte[4] = (UByte) ((word << 6) & 0xff);
	return 34;
}


// ROM 0x00284b34 StuffBits__16TZippyCompressorFPPUcPll12ByteAccessor
// The code's first count bits appended at *out, bitPosition bits into the
// byte; a byte is cleared when it is started.
void
TZippyCompressor::StuffBits(UByte** out, long* bitPosition, long count, ByteAccessor code)
{
	UByte* p = *out;
	long bit = *bitPosition;
	if (bit == 0)
		*p = 0;
	for (long i = 0; i < count; i++)
	{
		if (bit == 0)
			*p = 0;
		UByte value = (code.fByte[i >> 3] >> (7 - (i & 7))) & 1;
		*p |= (UByte) (value << (7 - bit));
		if (++bit == 8)
		{
			bit = 0;
			p++;
		}
	}
	*out = p;
	*bitPosition = bit;
}


// ROM 0x00284d48 CompressChunk__16TZippyCompressorFPUlPvUlT2T3
// The header, then a code per whole word; stored instead if the codes
// would be longer than the words.
NewtonErr
TZippyCompressor::CompressChunk(ULong* outSize, void* dst, ULong /*dstSize*/, void* src, ULong srcSize)
{
	UByte* header = (UByte*) dst;
	UByte* out = (UByte*) dst + 8;
	long bitPosition = 0;
	InitCache();
	*out = 0;
	ULong words = srcSize >> 2;
	if (words != 0)
	{
		ULong limit = srcSize * 8 + 0x40;
		ULong bits = 0x40;
		const UByte* in = (const UByte*) src;
		for (ULong i = 0; i < words; i++)
		{
			ByteAccessor code;
			long n = CacheAndCompress(GetBigEndianWord(in + 4 * i), &code);
			bits += n;
			if (limit < bits)
			{
				*outSize = srcSize + 8;
				PutBigEndianWord(header, srcSize + 8);
				PutBigEndianWord(header + 4, kZippyStoredHeader);
				fast_copy((UByte*) src, (UByte*) dst + 8, srcSize);
				return noErr;
			}
			StuffBits(&out, &bitPosition, n, code);
		}
	}
	*outSize = out - (UByte*) dst;
	if (bitPosition != 0)
	{
		*out |= (UByte) (0xff >> bitPosition);
		(*outSize)++;
	}
	PutBigEndianWord(header, *outSize);
	PutBigEndianWord(header + 4, kZippyCodedHeader);
	return noErr;
}


// ROM 0x00284e90 Compress__16TZippyCompressorFPUlPvUlT2T3
NewtonErr
TZippyCompressor::Compress(ULong* outSize, void* dst, ULong dstSize, void* src, ULong srcSize)
{
	NewtonErr err = Init(nil);
	if (err != noErr)
		return err;
	return CompressChunk(outSize, dst, dstSize, src, srcSize);
}


/* -------------------------------------------------------------------------------
	TZippyDecompressor
------------------------------------------------------------------------------- */

PROTOCOL_IMPL_SOURCE_MACRO(TZippyDecompressor)		// ROM 0x00284ee8 Sizeof__18TZippyDecompressorSFv
PROTOCOL_CLASSINFO(TZippyDecompressor, "TDecompressor", "", 0, 0, nil)	// ROM 0x00389ac8 ClassInfo__18TZippyDecompressorSFv


// ROM 0x00284ef0 New__18TZippyDecompressorFv
TZippyDecompressor*
TZippyDecompressor::New()
{
	return this;
}


// ROM 0x00284f3c Delete__18TZippyDecompressorFv
void
TZippyDecompressor::Delete()
{ }


// ROM 0x00284f54 Init__18TZippyDecompressorFPv
NewtonErr
TZippyDecompressor::Init(void* /*refCon*/)
{
	return noErr;
}


// ROM 0x00284f40 Finish__18TZippyDecompressorFPvUl
NewtonErr
TZippyDecompressor::Finish(void* /*header*/, ULong /*headerSize*/)
{
	return noErr;		// (the ROM leaves r0 = this)
}


// ROM 0x00284f44 DecompressedLength__18TZippyDecompressorFPvUl
ULong
TZippyDecompressor::DecompressedLength(void* /*src*/, ULong /*srcSize*/)
{
	return kZippyBlockSize;
}


// ROM 0x00284f4c HeaderSize__18TZippyDecompressorFv
ULong
TZippyDecompressor::HeaderSize()
{
	return 8;
}


// ROM 0x00284f5c InitCache__18TZippyDecompressorFv
void
TZippyDecompressor::InitCache()
{
	fCounter = 0;
	for (long i = 0; i < kZippyCacheSize; i++)
	{
		fUse[i] = -1;
		fCache[i] = 0;
	}
}


// ROM 0x00284f88 ExpandValue__18TZippyDecompressorFPPUcPlPUcPUl
// The next code from *in, bitPosition bits into the byte, and the word it
// stands for; false at the end of the stream (the last byte's padding
// reads as a new-word code, or the stream is used up).
Boolean
TZippyDecompressor::ExpandValue(UByte** in, long* bitPosition, UByte* last, ULong32* outWord)
{
	UByte* p = *in;
	long bit = *bitPosition;
	ULong32 head;
	if (p < last)
		head = ((((ULong32) p[0] << 8) | p[1]) << bit) >> 8;
	else
	{
		if (bit == 7 || (p > last && bit == 0))
			return false;
		head = (ULong32) p[0] << bit;
		if ((head & 0xc0) == 0xc0)
			return false;
	}
	ULong32 kind = head & 0xc0;
	if (kind == 0)
	{
		bit += 2;
		if (bit > 7)
		{
			p++;
			bit %= 8;
		}
		*in = p;
		*bitPosition = bit;
		*outWord = 0;
		return true;
	}
	long index = ((head & 0xff) >> 2) & 0xf;
	if (kind == 0x80)
	{
		bit += 6;
		if (bit > 7)
		{
			p++;
			bit %= 8;
		}
		*in = p;
		*bitPosition = bit;
		fCounter++;
		fUse[index] = fCounter;
		*outWord = fCache[index];
		return true;
	}
	if (kind == 0x40)
	{
		ULong32 window = ((ULong32) p[0] << 24) | ((ULong32) p[1] << 16) | ((ULong32) p[2] << 8);
		long newBit = (bit + 17) % 8;
		*in = p + (newBit == 0 ? 3 : 2);
		*bitPosition = newBit;
		fCounter++;
		fUse[index] = fCounter;
		fCache[index] = (fCache[index] & 0xffffc007) | ((window >> (12 - bit)) & 0x3ff8);
		*outWord = fCache[index];
		return true;
	}
	// a new word: the 32 bits after the code's two
	ULong32 window = ((ULong32) p[0] << 24) | ((ULong32) p[1] << 16) | ((ULong32) p[2] << 8) | p[3];
	ULong32 low16;
	if (bit == 7)
	{
		low16 = ((((ULong32) p[3] << 16) | ((ULong32) p[4] << 8) | p[5]) >> 7) & 0xffff;
		*bitPosition = 1;
		*in = p + 5;
	}
	else
	{
		ULong32 next = ((ULong32) p[1] << 24) | ((ULong32) p[2] << 16) | ((ULong32) p[3] << 8) | p[4];
		low16 = (next >> (8 - (bit + 2))) & 0xffff;
		long newBit = (bit + 2) % 8;
		*bitPosition = newBit;
		*in = p + (newBit != 0 ? 4 : 5);
	}
	ULong32 word = ((((window << (bit + 2)) >> 16) & 0xffff) << 16) | low16;
	fCounter++;
	long oldest = 0x7fffffff;
	long oldestIndex = 0xc0;
	for (long i = 0; i < kZippyCacheSize; i++)
	{
		if (fUse[i] < oldest)
		{
			oldest = fUse[i];
			oldestIndex = i;
		}
	}
	fCache[oldestIndex] = word;
	fUse[oldestIndex] = fCounter;
	*outWord = word;
	return true;
}


// ROM 0x002852f4 DecompressChunk__18TZippyDecompressorFPUlPvUlT2T3
NewtonErr
TZippyDecompressor::DecompressChunk(ULong* outSize, void* dst, ULong dstSize, void* src, ULong /*srcSize*/)
{
	ULong total = GetBigEndianWord(src);
	UByte* in = (UByte*) src + 8;
	if (GetBigEndianWord((UByte*) src + 4) != kZippyCodedHeader)
	{
		ULong n = total - 8;
		*outSize = n;
		fast_copy(in, (UByte*) dst, n);
		return noErr;
	}
	ULong produced = 0;
	long bitPosition = 0;
	UByte* last = (UByte*) src + total - 1;
	UByte* out = (UByte*) dst;
	InitCache();
	ULong32 word;
	while (ExpandValue(&in, &bitPosition, last, &word))
	{
		produced += 4;
		if (dstSize < produced)
			return kZippyErr_DestinationTooSmall;
		PutBigEndianWord(out, word);
		out += 4;
	}
	*outSize = produced;
	return noErr;
}


// ROM 0x00284ef4 Decompress__18TZippyDecompressorFPUlPvUlT2T3
NewtonErr
TZippyDecompressor::Decompress(ULong* outSize, void* dst, ULong dstSize, void* src, ULong srcSize)
{
	DecompressChunk(outSize, dst, dstSize, src, srcSize);
	return noErr;
}


/* -------------------------------------------------------------------------------
	TZippyCallbackCompressor
------------------------------------------------------------------------------- */

PROTOCOL_IMPL_SOURCE_MACRO(TZippyCallbackCompressor)		// ROM 0x00284760 Sizeof__24TZippyCallbackCompressorSFv
PROTOCOL_CLASSINFO(TZippyCallbackCompressor, "TCallbackCompressor", "TZippyRelocStoreDecompressor\0\0TZippyStoreDecompressor\0\0", 0, 0, nil)	// ROM 0x003899f4 ClassInfo__24TZippyCallbackCompressorSFv


// ROM 0x00284768 New__24TZippyCallbackCompressorFv
TZippyCallbackCompressor*
TZippyCallbackCompressor::New()
{
	fBuffer = nil;
	fCompressed = nil;
	fCompressor = nil;
	return this;
}


// ROM 0x0028477c Delete__24TZippyCallbackCompressorFv
void
TZippyCallbackCompressor::Delete()
{
	DisposPtr((Ptr) fBuffer);
	DisposPtr((Ptr) fCompressed);
	if (fCompressor != nil)
		fCompressor->Delete();
}


// ROM 0x002847b0 Init__24TZippyCallbackCompressorFPv
NewtonErr
TZippyCallbackCompressor::Init(void* /*refCon*/)
{
	NewtonErr err = kError_No_Memory;
	fCompressor = (TCompressor*) NewByName("TCompressor", "TZippyCompressor");
	if (fCompressor != nil)
	{
		fBuffer = (UByte*) NewPtr(kZippyBlockSize);
		fCompressed = (UByte*) NewPtr(kZippyCompressedBufferSize);
		if (fBuffer != nil && fCompressed != nil)
		{
			Reset();
			err = noErr;
		}
	}
	return err;
}


// ROM 0x00284848 Reset__24TZippyCallbackCompressorFv
NewtonErr
TZippyCallbackCompressor::Reset()
{
	fCount = 0;
	return noErr;
}


// ROM 0x00284854 WriteChunk__24TZippyCallbackCompressorFPvl
NewtonErr
TZippyCallbackCompressor::WriteChunk(void* data, long size)
{
	long done = 0;
	while (size != 0)
	{
		long n = kZippyBlockSize - fCount;
		if (size < n)
			n = size;
		BlockMove((char*) data + done, fBuffer + fCount, n);
		fCount += n;
		if (fCount == (long) kZippyBlockSize)
		{
			ULong compressedSize;
			NewtonErr err = fCompressor->Compress(&compressedSize, fCompressed, kZippyCompressedBufferSize, fBuffer, kZippyBlockSize);
			if (err == noErr)
				err = fWriteProc(fRefCon, fCompressed, compressedSize, false);
			if (err != noErr)
				return err;
			fCount = 0;
		}
		size -= n;
		done += n;
	}
	return noErr;
}


// ROM 0x0028492c Flush__24TZippyCallbackCompressorFv
NewtonErr
TZippyCallbackCompressor::Flush()
{
	if (fCount == 0)
		return noErr;
	ULong compressedSize;
	NewtonErr err = fCompressor->Compress(&compressedSize, fCompressed, kZippyCompressedBufferSize, fBuffer, fCount);
	if (err == noErr)
		err = fWriteProc(fRefCon, fCompressed, compressedSize, true);
	return err;
}
