/*
	File:		host/IMATool.cpp

	Contains:	newtonscript --ima-expand / --ima-compress: a file of the
				Newton's IMA/DVI ADPCM blocks (a sound frame's 'samples
				with codecName "TIMACodec") expanded to 16-bit PCM, and
				16-bit PCM compressed back, by the reconstruction of the
				ROM's own codec (sound/IMACodec.h: ExpandIMA 0x000e8500,
				CompressIMA 0x000e82f8).  The ROM-free track's extractor
				(tools/newton-rom/analysis/romsrc.py) turns a compressed
				sound into a WAV file with the first and makes it back with
				the second; a sound goes into the tree as a WAV only when
				that gives back its very bytes.

	The PCM files are raw big-endian 16-bit mono samples, as the ROM holds
	sound in memory (sound/SampleWords.h); the WAV around them is the
	extractor's business.  Both directions start from a fresh IMAState, as
	a channel does at the start of a sound.

	Not a reconstruction: a host tool.
*/

#include "IMACodec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static unsigned char*
ReadWhole(const char* path, long* size)
{
	FILE* f = fopen(path, "rb");
	if (f == nil)
		return nil;
	fseek(f, 0, SEEK_END);
	*size = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char* data = (unsigned char*) malloc(*size > 0 ? *size : 1);
	if (data != nil && fread(data, 1, *size, f) != (size_t) *size)
	{
		free(data);
		data = nil;
	}
	fclose(f);
	return data;
}


static int
WriteWhole(const char* path, const void* data, long size)
{
	FILE* f = fopen(path, "wb");
	if (f == nil)
		return 1;
	int err = fwrite(data, 1, size, f) != (size_t) size;
	fclose(f);
	return err;
}


// IMA blocks -> 16-bit big-endian PCM (0x40 samples a block)
int
RunIMAExpand(const char* inPath, const char* outPath)
{
	long size = 0;
	unsigned char* in = ReadWhole(inPath, &size);
	if (in == nil || size % kIMABlockBytes != 0)
	{
		fprintf(stderr, "newtonscript: %s is not a run of IMA blocks\n", inPath);
		return 1;
	}
	ULong blocks = size / kIMABlockBytes;
	short* out = (short*) calloc(blocks * kIMABlockSize + 1, sizeof(short));
	IMAState state;
	ExpandIMA((const signed char*) in, out, &state, blocks, 1, 2);
	int err = WriteWhole(outPath, out, blocks * kIMABlockSize * sizeof(short));
	free(in);
	free(out);
	return err;
}


// 16-bit big-endian PCM -> IMA blocks (whole blocks of 0x40 samples)
int
RunIMACompress(const char* inPath, const char* outPath)
{
	long size = 0;
	unsigned char* in = ReadWhole(inPath, &size);
	if (in == nil || size % (kIMABlockSize * 2) != 0)
	{
		fprintf(stderr, "newtonscript: %s is not whole blocks of 16-bit samples\n", inPath);
		return 1;
	}
	ULong samples = size / 2;
	ULong blocks = samples / kIMABlockSize;
	signed char* out = (signed char*) calloc(blocks * kIMABlockBytes + 1, 1);
	IMAState state;
	CompressIMA((const short*) in, out, samples, &state, 1, 0);
	int err = WriteWhole(outPath, out, blocks * kIMABlockBytes);
	free(in);
	free(out);
	return err;
}
