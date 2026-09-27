/*
	File:		ink/CICConvert.cpp

	Contains:	The CIC codec's converter: ink of one code book re-encoded
				in another without ever becoming points.  A decoder reads
				the old ink stroke by stroke and segment by segment, and an
				encoder writes each piece straight back out - a long
				stroke's segments keep their four control numbers, a short
				stroke its points - so what comes out is the same drawing
				in the other book's code words.  InkConvert
				(InkStrokes.cpp) is what a script reaches it through.

	Reconstructed from the MP2x00 US ROM (0x00280980-0x00280d88); each
	function cites its origin.
*/

#include "CICCodec.h"
#include "ParaGraph.h"
#include <string.h>


// ROM 0x00280d28 ProcessNewStroke__FP4_CDCP4_DCCPs
// The next stroke's header read, and written again: the kind and where the
// pen starts.
static Boolean
ProcessNewStroke(CICEncoder* encoder, CICDecoder* decoder, short* outKind)
{
	if (ReadNewStroke(decoder, outKind))
	{
		encoder->fTrace[0].x = decoder->fX;
		encoder->fTrace[0].y = decoder->fY;
		if (WriteNewStroke(encoder, *outKind))
			return true;
	}
	return false;
}


// ROM 0x00280bf4 ProcessLongStrokeNear__FP4_CDCP4_DCC
// A long stroke's segments, each read and written with the same ends and
// the same two control numbers each way, until the segment that says the
// stroke goes on (tag 8) is not the one read.
static Boolean
ProcessLongStrokeNear(CICEncoder* encoder, CICDecoder* decoder)
{
	for (;;)
	{
		short tag;
		if (!ReadSegmentNear(decoder, &tag))
			return false;
		encoder->fStrokeX = decoder->fSegStartX;
		encoder->fStrokeY = decoder->fSegStartY;
		encoder->fLastX = decoder->fSegEndX;
		encoder->fLastY = decoder->fSegEndY;
		encoder->fCoefX[2] = decoder->fSegX[2];
		encoder->fCoefY[2] = decoder->fSegY[2];
		encoder->fCoefX[3] = decoder->fSegX[3];
		encoder->fCoefY[3] = decoder->fSegY[3];
		if (!WriteSegment(encoder, tag))
			return false;
		if (tag != 8)
			return true;
	}
}


// ROM 0x00280c98 ProcessShortStrokeNear__FP4_CDCP4_DCC
// A short stroke: its points read and written as they are.
static Boolean
ProcessShortStrokeNear(CICEncoder* encoder, CICDecoder* decoder)
{
	if (ReadShortStroke(decoder))
	{
		ULong count = decoder->fPointCount & 0xffff;
		for (ULong i = 0; i < count; i++)
		{
			encoder->fTrace[i].x = decoder->fPointsX[i];
			encoder->fTrace[i].y = decoder->fPointsY[i];
		}
		encoder->fPointCount = count;
		if (WriteShortStroke(encoder))
			return true;
	}
	return false;
}


// ROM 0x00280b54 ConverterRun__FP4_CDCP4_DCC
// Stroke after stroke until the end of the group.
static Boolean
ConverterRun(CICEncoder* encoder, CICDecoder* decoder)
{
	for (;;)
	{
		short kind = 0;
		if (!ProcessNewStroke(encoder, decoder, &kind))
			return false;
		if (kind == kCICEndOfGroup)
			return true;
		if (kind == kCICLongStroke && !ProcessLongStrokeNear(encoder, decoder))
			return false;
		if (kind == kCICShortStroke && !ProcessShortStrokeNear(encoder, decoder))
			return false;
	}
}


// ROM 0x00280980 ConvertData__FPPvPUiUs
// The ink in *data (*size bytes) re-encoded in the code book `format`.
// Ink already in it is left alone; otherwise *data comes back pointing at
// a new block (HWRMemoryAlloc'd, room for twice the old size and a little)
// and *size at how much of it was written.  An empty or unreadable block
// (GetInkFormat answering nought) is refused.  ==> whether it worked.
//
// DEVIATION: the two contexts are sized from sizeof on the host (the ROM's
// are 0x1f0 and 0xe60 bytes), a host pointer being wider.
Boolean
ConvertData(void** data, ULong* size, UShort format)
{
	CICEncoder* encoder = nil;
	CICDecoder* decoder = nil;
	long inFormat;
	if (data != nil && size != nil && *size > 2 && *data != nil && (inFormat = GetInkFormat(*data)) != 0)
	{
		if (inFormat == format)
			return true;
		decoder = (CICDecoder*) HWRMemoryAlloc(sizeof(CICDecoder));
		if (decoder != nil)
		{
			memset(decoder, 0, sizeof(CICDecoder));
			ULong inSize = *size;
			decoder->fData = (const UByte*) *data;
			decoder->fBitCount = inSize << 3;
			decoder->fFirst = 1;
			encoder = (CICEncoder*) HWRMemoryAlloc(sizeof(CICEncoder));
			if (encoder != nil)
			{
				memset(encoder, 0, sizeof(CICEncoder));
				encoder->fFirst = 1;
				encoder->fBookNumber = format;
				if (EcdrSelectCodeBook(encoder))
				{
					encoder->fSlack = 0;
					encoder->fSlack2 = 0;
					ULong outSize = inSize * 2 + 0x20;
					encoder->fOut = (UByte*) HWRMemoryAlloc(outSize);
					if (encoder->fOut != nil)
					{
						memset(encoder->fOut, 0, outSize);
						encoder->fBitLimit = outSize * 8;
						if (ConverterRun(encoder, decoder))
						{
							long bits = (long) encoder->fHighWater;
							long whole = (bits < 0 ? bits + 7 : bits) >> 3;
							*size = (ULong) ((bits % 8 == 0) ? whole : whole + 1);
							*data = encoder->fOut;
							HWRMemoryFree((Ptr) encoder);
							HWRMemoryFree((Ptr) decoder);
							return true;
						}
					}
				}
			}
		}
		// (ROM: the output block, when there is one, is not given back)
		if (encoder != nil)
			HWRMemoryFree((Ptr) encoder);
		if (decoder != nil)
			HWRMemoryFree((Ptr) decoder);
	}
	return false;
}
