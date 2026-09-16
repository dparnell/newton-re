/*
	File:		compression/ZippyCompression.h

	Contains:	The "Zippy" coder - TZippyCompressor, TZippyDecompressor and
				TZippyCallbackCompressor - a word-oriented cache compressor
				for data made of pointers and small integers (the WK kind).
				Not in the DDK; follows the ROM (0x00282da8-0x00283a00).

	The format.  An 8-byte header - the total length, then 0x10000 for
	coded data or 0x1000000 for stored - and, coded, a bit stream, most
	significant bit first, with one code per 32-bit source word (a
	remainder of bytes is dropped; the block size is 0x400):
	  00                   the word is 0
	  10 iiii              the word is cache entry i
	  01 iiii bbbbbbbbbbb  it differs from entry i only in bits 13-3, given
	  11 w(32)             a new word, replacing the least recently used
	The cache holds 16 words with a use counter each; an entry "matches
	partly" when its bits 31-14 and 2-0 equal the word's.  The stream's
	last byte is padded with 1 bits, which read as an impossible new-word
	code at the end.  If the codes would not beat the stored form the
	block is stored.

	Layouts (ROM 0x94): TProtocol, fCounter +0x10, fUse[16] +0x14,
	fCache[16] +0x54 (both coders); TZippyCallbackCompressor 0x28 as the
	LZ one.  ByteAccessor is the 5-byte code string StuffBits appends.
*/

#ifndef __ZIPPYCOMPRESSION_H
#define __ZIPPYCOMPRESSION_H

#include "Compression.h"
#include "Pushpopper.h"		// ULong32

const long kZippyCacheSize = 16;
const ULong kZippyBlockSize = 0x400;
const ULong kZippyCompressedBufferSize = 0x5dc;
const ULong32 kZippyCodedHeader = 0x10000;
const ULong32 kZippyStoredHeader = 0x1000000;

// a code, up to 34 bits, most significant first
struct ByteAccessor
{
	UByte			fByte[5];
};


PROTOCOL TZippyCompressor : public TCompressor
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TZippyCompressor);

	TZippyCompressor*	New();
	void			Delete();

	NewtonErr		Init(void* refCon);
	NewtonErr		Compress(ULong* outSize, void* dst, ULong dstSize, void* src, ULong srcSize);
	ULong			EstimatedCompressedSize(void* src, ULong srcSize);

	NewtonErr		CompressChunk(ULong* outSize, void* dst, ULong dstSize, void* src, ULong srcSize);
	NewtonErr		Finish(void* header, ULong headerSize);
	ULong			HeaderSize();
	void			InitCache();
	long			CacheAndCompress(ULong32 word, ByteAccessor* code);		// the code's length in bits
	void			StuffBits(UByte** out, long* bitPosition, long count, ByteAccessor code);

	long			fCounter;						// +0x10
	long			fUse[kZippyCacheSize];			// +0x14  when each entry was last used
	ULong32			fCache[kZippyCacheSize];		// +0x54
};


PROTOCOL TZippyDecompressor : public TDecompressor
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TZippyDecompressor);

	TZippyDecompressor*	New();
	void			Delete();

	NewtonErr		Init(void* refCon);
	NewtonErr		Decompress(ULong* outSize, void* dst, ULong dstSize, void* src, ULong srcSize);

	NewtonErr		DecompressChunk(ULong* outSize, void* dst, ULong dstSize, void* src, ULong srcSize);
	NewtonErr		Finish(void* header, ULong headerSize);
	ULong			DecompressedLength(void* src, ULong srcSize);
	ULong			HeaderSize();
	void			InitCache();
	Boolean			ExpandValue(UByte** in, long* bitPosition, UByte* last, ULong32* outWord);	// false at the end

	long			fCounter;						// +0x10
	long			fUse[kZippyCacheSize];			// +0x14
	ULong32			fCache[kZippyCacheSize];		// +0x54
};


PROTOCOL TZippyCallbackCompressor : public TCallbackCompressor
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TZippyCallbackCompressor);

	TZippyCallbackCompressor*	New();
	void			Delete();

	NewtonErr		Init(void* refCon);
	NewtonErr		Reset();
	NewtonErr		WriteChunk(void* data, long size);
	NewtonErr		Flush();

	long			fCount;				// +0x18
	UByte*			fBuffer;			// +0x1c
	UByte*			fCompressed;		// +0x20
	TCompressor*	fCompressor;		// +0x24
};

#endif	/* __ZIPPYCOMPRESSION_H */
