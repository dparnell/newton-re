/*
	File:		ink/InkCodec.cpp

	Contains:	The ink codec boundary: the format in a block of ink, and
				the register of codecs that can read and write it.  See
				InkCodec.h.
*/

#include "InkCodec.h"


TInkCodec::~TInkCodec()
{ }


// ROM 0x00280950 GetInkFormat__FPv
// Which form a block of ink is in, from its first byte.  The low nibble
// is 8 in every form the codec's newer header marks; anything else is its
// older format (code book 2, no header).  Of the three the header marks,
// bit 7 marks one and bit 6 another.
long
GetInkFormat(const void* data)
{
	unsigned char first = *(const unsigned char*) data;
	if ((first & 0xf) != 8)
		return kInkFormatOld;
	if ((first & 0x80) != 0)
		return kInkFormatHigh;
	if ((first & 0x40) != 0)
		return kInkFormatWide;
	return kInkFormatCompressed;
}


/*------------------------------------------------------------------------------
	T h e   r e g i s t e r
------------------------------------------------------------------------------*/

// (host: the ROM has one codec and calls it; this is the seam that lets
// a second one be added beside it.)
const long kMaxInkCodecs = 4;

static TInkCodec*	gInkCodecs[kMaxInkCodecs];
static long			gInkCodecCount = 0;


void
RegisterInkCodec(TInkCodec* codec)
{
	if (codec == nil || gInkCodecCount >= kMaxInkCodecs)
		return;
	for (long i = 0; i < gInkCodecCount; i++)
		if (gInkCodecs[i] == codec)
			return;
	gInkCodecs[gInkCodecCount++] = codec;
}


// The first registered that can read what this block is in.
TInkCodec*
InkCodecFor(const void* data)
{
	long format = GetInkFormat(data);
	for (long i = 0; i < gInkCodecCount; i++)
		if (gInkCodecs[i]->CanDecode(format))
			return gInkCodecs[i];
	return nil;
}


// The first registered that writes anything.
TInkCodec*
InkCodecForWriting(void)
{
	for (long i = 0; i < gInkCodecCount; i++)
		if (gInkCodecs[i]->CanEncode())
			return gInkCodecs[i];
	return nil;
}


long
CountInkCodecs(void)
{
	return gInkCodecCount;
}


TInkCodec*
IndexedInkCodec(long index)
{
	return (index >= 0 && index < gInkCodecCount) ? gInkCodecs[index] : nil;
}
