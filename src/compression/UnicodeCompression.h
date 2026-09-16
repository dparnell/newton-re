/*
	File:		compression/UnicodeCompression.h

	Contains:	The Unicode text coder - TUnicodeCompressor and
				TUnicodeDecompressor, callback compressors that shorten
				UniChar text (16-bit characters, high byte first) by sending a
				run of characters from the same 256-character block as its
				high byte, a count and the low bytes.  Not in the DDK;
				follows the ROM (0x00254d28-0x002552b0).

	Only characters whose high byte is in gUnicodeLookupTable (a bitmap of
	the blocks with alphabets in them: 0x00, 0x02-0x06, 0x09-0x0e, 0x10) are
	run-coded; any other character is sent as its two bytes.  A run is at
	most 255 long.  The compressor gathers 128 bytes for its write proc; the
	decompressor reads a byte or a run at a time through its read proc,
	whose underflow flag it keeps.

	Layouts (ROM): TUnicodeCompressor 0x1a4 - TCallbackCompressor, fBuffer
	+0x18 (128), fCount +0x98, fRunLength +0x9c, fRunHigh +0xa0, fRunLow
	+0xa1; TUnicodeDecompressor 0x124 - TCallbackDecompressor, fRunCount
	+0x18, fRunIndex +0x1c, fHigh +0x20, fRunLow +0x21 (256), fSourceDone
	+0x121.
*/

#ifndef __UNICODECOMPRESSION_H
#define __UNICODECOMPRESSION_H

#include "Compression.h"

const ULong kUnicodeBufferSize = 0x80;
const ULong kUnicodeMaxRun = 0xff;


PROTOCOL TUnicodeCompressor : public TCallbackCompressor
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TUnicodeCompressor);

	TUnicodeCompressor*	New();
	void			Delete();

	NewtonErr		Init(void* refCon);
	NewtonErr		Reset();
	NewtonErr		WriteChunk(void* data, long size);
	NewtonErr		Flush();

	NewtonErr		WriteRun();

	UByte			fBuffer[kUnicodeBufferSize];	// +0x18
	ULong			fCount;				// +0x98  bytes in fBuffer
	ULong			fRunLength;			// +0x9c  characters in the run being gathered
	UByte			fRunHigh;			// +0xa0  their high byte
	UByte			fRunLow[0x103];		// +0xa1  their low bytes
};


PROTOCOL TUnicodeDecompressor : public TCallbackDecompressor
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TUnicodeDecompressor);

	TUnicodeDecompressor*	New();
	void			Delete();

	NewtonErr		Init(void* refCon);
	NewtonErr		Reset();
	NewtonErr		ReadChunk(void* into, long* size, Boolean* underflow);

	UByte			fRunCount;			// +0x18  the run being handed out, 0 for none
	ULong			fRunIndex;			// +0x1c  how far through it
	UByte			fHigh;				// +0x20
	UByte			fRunLow[0x100];		// +0x21
	Boolean			fSourceDone;		// +0x121  the read proc's underflow flag
};

#endif	/* __UNICODECOMPRESSION_H */
