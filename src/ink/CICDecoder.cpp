/*
	File:		ink/CICDecoder.cpp

	Contains:	The ROM's ink decoder.  See CICCodec.h.
*/

#include "CICCodec.h"


// ROM 0x00280d88 GetNBit__FP4_DCCUs
// The next n bits of the stream.  A byte is read from its least
// significant bit upwards, and the bits of the answer are filled in the
// same order, so the first bit read is the answer's bit 0.  Two counts
// are kept of how much has been read - one the whole run's and one the
// stroke's - and both are stepped here.
ULong
GetNBit(CICDecoder* decoder, ULong n)
{
	ULong value = 0;
	for (ULong i = 0; i < n; i++)
	{
		ULong at = decoder->fBitPos;
		value |= (ULong) ((decoder->fData[at >> 3] >> (at & 7)) & 1) << i;
		decoder->fBitPos = at + 1;
	}
	decoder->fStrokeBits += n;
	decoder->fTotalBits += n;
	return value;
}


/*------------------------------------------------------------------------------
	T h e   c o d e c
------------------------------------------------------------------------------*/

TCICInkCodec	gCICInkCodec;


const char*
TCICInkCodec::Name(void) const
{
	return "CIC";
}


// The three formats the ROM's encoder writes.  The old uncompressed ink
// is somebody else's.
Boolean
TCICInkCodec::CanDecode(long format) const
{
	return format == kInkFormatCompressed || format == kInkFormatHigh || format == kInkFormatWide;
}


Boolean
TCICInkCodec::CanEncode(void) const
{
	return false;		// NOT YET: EncoderRun and what it calls
}


Boolean
TCICInkCodec::Decode(const void* /*data*/, long /*size*/, ULong /*group*/,
					 InkPointProc /*sink*/, void* /*refCon*/) const
{
	// NOT YET: DecoderRun (0x00282518) - ReadNewStroke, DecodeLongStroke
	// and DecodeShortStroke over the code books
	return false;
}


void*
TCICInkCodec::Encode(TStroke** /*strokes*/, long* outSize) const
{
	if (outSize != nil)
		*outSize = 0;
	return nil;
}


// (host: the ROM has no such call - it has one codec and reaches it by
// name.  A host program makes the register say what it holds.)
void
InitializeInkCodecs(void)
{
	RegisterInkCodec(&gCICInkCodec);
}
