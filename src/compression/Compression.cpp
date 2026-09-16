/*
	File:		compression/Compression.cpp

	Contains:	The compression interfaces' glue (Compression.h), the
				TCompressor Handle helpers, and the registration of the ROM's
				implementations.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "Compression.h"
#include "LZCompression.h"
#include "ZippyCompression.h"
#include "ArithmeticCompression.h"
#include "UnicodeCompression.h"
#include "NewtonMemory.h"
#include "OSErrors.h"


/* -------------------------------------------------------------------------------
	The interfaces' New(char*)/Delete() glue (ROM 0x0037fdf4-0x00380034)
------------------------------------------------------------------------------- */

// ROM 0x0037fdf4 New__11TCompressorSFPc
TCompressor*
TCompressor::New(const char* implementation)
{
	TCompressor* p = (TCompressor*) AllocInstanceByName("TCompressor", implementation);
	return p != nil ? (TCompressor*) p->GlueNew() : nil;
}


// ROM 0x0037fe20 Delete__11TCompressorFv
void
TCompressor::Delete()
{
	GlueDelete();
}


// ROM 0x0037ff0c New__13TDecompressorSFPc
TDecompressor*
TDecompressor::New(const char* implementation)
{
	TDecompressor* p = (TDecompressor*) AllocInstanceByName("TDecompressor", implementation);
	return p != nil ? (TDecompressor*) p->GlueNew() : nil;
}


// ROM 0x0037ff38 Delete__13TDecompressorFv
void
TDecompressor::Delete()
{
	GlueDelete();
}


// ROM 0x0037fe7c New__19TCallbackCompressorSFPc
TCallbackCompressor*
TCallbackCompressor::New(const char* implementation)
{
	TCallbackCompressor* p = (TCallbackCompressor*) AllocInstanceByName("TCallbackCompressor", implementation);
	return p != nil ? (TCallbackCompressor*) p->GlueNew() : nil;
}


// ROM 0x0037fea8 Delete__19TCallbackCompressorFv
void
TCallbackCompressor::Delete()
{
	GlueDelete();
}


// ROM 0x0037ff8c New__21TCallbackDecompressorSFPc
TCallbackDecompressor*
TCallbackDecompressor::New(const char* implementation)
{
	TCallbackDecompressor* p = (TCallbackDecompressor*) AllocInstanceByName("TCallbackDecompressor", implementation);
	return p != nil ? (TCallbackDecompressor*) p->GlueNew() : nil;
}


// ROM 0x0037ffb8 Delete__21TCallbackDecompressorFv
void
TCallbackDecompressor::Delete()
{
	GlueDelete();
}


/* -------------------------------------------------------------------------------
	TCompressor over a Handle
------------------------------------------------------------------------------- */

// ROM 0x00071b88 EstimatedCompressedSize__11TCompressorFPPc
ULong
TCompressor::EstimatedCompressedSize(Handle h)
{
	HLock(h);
	ULong size = GetHandleSize(h);
	ULong estimate = EstimatedCompressedSize(*h, size);
	HUnlock(h);
	return estimate;
}


// ROM 0x00071aa4 Compress__11TCompressorFPPc
// The Handle's contents are replaced by their compressed form (through a
// temporary the size of the estimate; the ROM's malloc/free).
NewtonErr
TCompressor::Compress(Handle h)
{
	ULong size = GetHandleSize(h);
	ULong compressedSize = EstimatedCompressedSize(h);
	Ptr buffer = NewPtr(compressedSize);
	NewtonErr err = kError_No_Memory;
	if (buffer != nil)
	{
		HLock(h);
		err = Compress(&compressedSize, buffer, compressedSize, *h, size);
		HUnlock(h);
		if (err == noErr)
		{
			SetHandleSize(h, compressedSize);
			err = kError_No_Memory;
			if (GetHandleSize(h) == compressedSize)
			{
				HLock(h);
				BlockMove(buffer, *h, compressedSize);
				HUnlock(h);
				err = noErr;
			}
		}
		DisposPtr(buffer);
	}
	return err;
}


/* -------------------------------------------------------------------------------
	Registration
------------------------------------------------------------------------------- */

// ROM 0x00101050 InitLZDecompression__Fv
void
InitLZDecompression(void)
{
	TLZDecompressor::ClassInfo()->Register();
}


// ROM 0x00037950 InitArithmeticCompression__Fv
void
InitArithmeticCompression(void)
{
	TArithmeticCompressor::ClassInfo()->Register();
	TArithmeticDecompressor::ClassInfo()->Register();
}


// ROM 0x00254f60 InitUnicodeCompression__Fv
void
InitUnicodeCompression(void)
{
	TUnicodeCompressor::ClassInfo()->Register();
	TUnicodeDecompressor::ClassInfo()->Register();
}


// ROM 0x00282ffc InitZippyCompression__Fv
void
InitZippyCompression(void)
{
	TZippyCallbackCompressor::ClassInfo()->Register();
	TZippyCompressor::ClassInfo()->Register();
	TZippyDecompressor::ClassInfo()->Register();
}


// ROM 0x00283588 InitZippyDecompression__Fv
void
InitZippyDecompression(void)
{
	TZippyDecompressor::ClassInfo()->Register();
}


// ROM 0x00100ac8 InitializeCompression__Fv
// Every compressor of the ROM, in the registry (RegisterROMDomainManager
// calls this before the store decompressors).
void
InitializeCompression(void)
{
	TLZCallbackCompressor::ClassInfo()->Register();
	TLZCompressor::ClassInfo()->Register();
	InitLZDecompression();
	InitArithmeticCompression();
	InitUnicodeCompression();
	TZippyCallbackCompressor::ClassInfo()->Register();
	TZippyCompressor::ClassInfo()->Register();
	TZippyDecompressor::ClassInfo()->Register();
}
