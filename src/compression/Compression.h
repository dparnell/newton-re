/*
	File:		compression/Compression.h

	Contains:	The compression protocols - TCompressor and TDecompressor
				(a buffer at a time) and TCallbackCompressor and
				TCallbackDecompressor (streamed through a callback) - and the
				start-up that registers the ROM's implementations of them:
				LZ (the stores' and packages' compression), Zippy, arithmetic
				and Unicode text coding.  The DDK has no header for these;
				the interfaces follow the ROM's dispatch tables
				(tools/newton-rom/analysis/classinfo.py --name TLZCompressor
				and friends) and the interface glue at 0x0037fdf4-0x00380034.

	The calls' shape: Compress(&outSize, dst, dstSize, src, srcSize) and
	Decompress(&outSize, dst, dstSize, src, srcSize) answer an error; a
	callback compressor takes WriteChunk(data, size) calls and hands each
	compressed block to its fWriteProc(fRefCon, block, size, isLast); a
	callback decompressor answers ReadChunk(into, &size, &underflow) from
	its fReadProc.  The compressed form is the implementation's
	(LZCompression.h describes the LZ one).
*/

#ifndef __COMPRESSION_H
#define __COMPRESSION_H

#ifndef __PROTOCOLS_H
#include "Protocols.h"
#endif


PROTOCOL TCompressor : public TProtocol
{
public:
	static TCompressor*	New(const char* implementation);
	void			Delete();

	VIRTUAL NewtonErr	Init(void* refCon) ENDVIRTUAL;
	VIRTUAL NewtonErr	Compress(ULong* outSize, void* dst, ULong dstSize, void* src, ULong srcSize) ENDVIRTUAL;
	VIRTUAL ULong		EstimatedCompressedSize(void* src, ULong srcSize) ENDVIRTUAL;

	// the interface's own helpers: a Handle's contents, replaced in place
	NewtonErr		Compress(Handle h);
	ULong			EstimatedCompressedSize(Handle h);
};


PROTOCOL TDecompressor : public TProtocol
{
public:
	static TDecompressor*	New(const char* implementation);
	void			Delete();

	VIRTUAL NewtonErr	Init(void* refCon) ENDVIRTUAL;
	VIRTUAL NewtonErr	Decompress(ULong* outSize, void* dst, ULong dstSize, void* src, ULong srcSize) ENDVIRTUAL;
};


// what a callback compressor calls with each compressed block
typedef NewtonErr (*CompressorWriteProcPtr)(void* refCon, void* block, ULong size, Boolean isLast);

PROTOCOL TCallbackCompressor : public TProtocol
{
public:
	static TCallbackCompressor*	New(const char* implementation);
	void			Delete();

	VIRTUAL NewtonErr	Init(void* refCon) ENDVIRTUAL;
	VIRTUAL NewtonErr	Reset() ENDVIRTUAL;
	VIRTUAL NewtonErr	WriteChunk(void* data, long size) ENDVIRTUAL;
	VIRTUAL NewtonErr	Flush() ENDVIRTUAL;

	// set by the client after New (the ROM's callers write them directly)
	CompressorWriteProcPtr	fWriteProc;		// +0x10
	void*			fRefCon;				// +0x14
};


// what a callback decompressor calls for more compressed data
typedef NewtonErr (*DecompressorReadProcPtr)(void* refCon, void* into, long* size, Boolean* underflow);

PROTOCOL TCallbackDecompressor : public TProtocol
{
public:
	static TCallbackDecompressor*	New(const char* implementation);
	void			Delete();

	VIRTUAL NewtonErr	Init(void* refCon) ENDVIRTUAL;
	VIRTUAL NewtonErr	Reset() ENDVIRTUAL;
	VIRTUAL NewtonErr	ReadChunk(void* into, long* size, Boolean* underflow) ENDVIRTUAL;

	DecompressorReadProcPtr	fReadProc;		// +0x10
	void*			fRefCon;				// +0x14
};


// registration of the ROM's implementations (RegisterROMDomainManager)
void	InitializeCompression(void);
void	InitLZDecompression(void);
void	InitArithmeticCompression(void);
void	InitUnicodeCompression(void);
void	InitZippyCompression(void);
void	InitZippyDecompression(void);

#endif	/* __COMPRESSION_H */
