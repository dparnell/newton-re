/*
	File:		ink/InkStrokes.cpp

	Contains:	The glue between the strokes the pen leaves and the codec
				that packs them - the ROM's InkCompress and InkExpand,
				and the two point procs underneath them.

				The codec knows nothing about strokes: it is opened on a
				source that hands points out and a sink that takes them
				(ink/InkCodec.h).  The ROM is built the same way -
				GenericCSCompress puts PGCGetPointProc in place of the
				codec's own source to read a list of TStrokes, and
				GenericCSExpandGuts gives Decode PGCStorePointProc to
				build them - so this file is where the two meet, and the
				only part of the ink area that knows what a stroke is.

				Points cross the boundary in whole tablet units.  A
				stroke keeps them as 16.16 pixels, so they are multiplied
				by the tablet scale (eight) on the way in and divided by
				it on the way out.
*/

#include "Ink.h"
#include "CICCodec.h"
#include "Stroke.h"
#include "StrokeQueue.h"		// gTabScale
#include "Objects.h"
#include "RSSymbols.h"
#include "NewtonMemory.h"
#include "Ports.h"			// RoundFixed
#include "FixedMath.h"

#include <string.h>


// ROM 0x001a3448 DisposeTStrokes__FPP7TStroke
// A list of strokes and the strokes in it given back.
void
DisposeTStrokes(TStroke** strokes)
{
	if (strokes == nil)
		return;
	for (long i = 0; strokes[i] != nil; i++)
		strokes[i]->IDispose();
	DisposPtr((Ptr) strokes);
}


/*------------------------------------------------------------------------------
	S t r o k e s   i n
------------------------------------------------------------------------------*/

// Where PGCGetPointProc has got to: which stroke of the list, and which
// point of it.
struct StrokeSource
{
	TStroke**	fStrokes;		// the list, ended by a nil
	long		fStroke;		// the one being read
	long		fPoint;			// and where in it
};


// ROM 0x00153a60 PGCGetPointProc__FsP6_POINTP4_CDC
// The next point of the list of strokes, in whole tablet units.  A point
// in the same place as the one before is passed over - the codec has
// nothing to do with a pen that stood still - and the end of a stroke is
// answered before the first point of the next.
static short
PGCGetPointProc(short what, InkPoint* pt, void* refCon)
{
	StrokeSource* at = (StrokeSource*) refCon;
	if (what == kInkAskBegin)
	{
		at->fStroke = 0;
		at->fPoint = 0;
		return at->fStrokes != nil && at->fStrokes[0] != nil;
	}
	for (;;)
	{
		TStroke* stroke = at->fStrokes[at->fStroke];
		if (stroke == nil)
			return kInkEnd;
		long count = (long) stroke->fCount;
		if (at->fPoint >= count)
		{
			at->fStroke++;
			at->fPoint = 0;
			return at->fStrokes[at->fStroke] == nil ? kInkEnd : kInkEndStroke;
		}
		// a point in the same place as the one before is not worth
		// sending
		SamplePt* sample = stroke->GetPoint(at->fPoint);
		if (at->fPoint > 0)
		{
			SamplePt* before = stroke->GetPoint(at->fPoint - 1);
			if (SampleX(sample) == SampleX(before) && SampleY(sample) == SampleY(before))
			{
				at->fPoint++;
				continue;
			}
		}
		// (the usual scale is a whole eight, and the ROM multiplies by it
		// rather than going through FixedMultiply, whose intermediate a
		// whole coordinate would overflow)
		pt->x = (short) (gTabScale.x == 0x80000
						 ? (long) ((ULong) SampleX(sample) * 8 + 0x8000) >> 16
						 : RoundFixed(FixedMultiply(SampleX(sample), gTabScale.x)));
		pt->y = (short) (gTabScale.y == 0x80000
						 ? (long) ((ULong) SampleY(sample) * 8 + 0x8000) >> 16
						 : RoundFixed(FixedMultiply(SampleY(sample), gTabScale.y)));
		at->fPoint++;
		return kInkPoint;
	}
}


// ROM 0x00140b78 InkCompress__FPP7TStrokeUc
// A list of strokes packed into a binary: 'ink2 for raw ink, or
// 'inkWord with the word's measurements after it.
//
// NOT YET RECONSTRUCTED: the ink word's eight bytes, which the ROM works
// out from the strokes themselves (GetPackedInkWordInfoFromStrokes
// 0x00140a4c); an ink word made here carries nothing but nought.
Ref
InkCompress(TStroke** strokes, Boolean asWord)
{
	StrokeSource source;
	source.fStrokes = strokes;
	source.fStroke = 0;
	source.fPoint = 0;
	TInkCodec* codec = InkCodecForWriting();
	if (codec == nil)
		return NILREF;
	long size = 0;
	void* bits = codec->Encode(PGCGetPointProc, &source, &size);
	if (bits == nil)
		return NILREF;
	long length = asWord ? size + (long) sizeof(PackedInkWordInfo) : size;
	RefVar ink(AllocateBinary(asWord ? RSSYMinkword : RSSYMink2, length));
	char* data = (char*) BinaryData(ink);
	BlockMove(bits, data, size);
	if (asWord)
		memset(data + size, 0, sizeof(PackedInkWordInfo));
	DisposPtr((Ptr) bits);
	return ink;
}


/*------------------------------------------------------------------------------
	S t r o k e s   o u t
------------------------------------------------------------------------------*/

// What PGCStorePointProc is building: the strokes so far, the one being
// filled, and where the whole thing is to be put.
struct StrokeSink
{
	TStroke**	fStrokes;		// room for the answers, ended by a nil
	long		fRoom;			// how many that is
	long		fStroke;		// the one being filled
	TStroke*	fCurrent;
	long		fCount;			// how many points it has taken
	long		fOffsetX;		// where the ink is to be put
	long		fOffsetY;
	Boolean		fFailed;
};

// A stroke is made empty - TArray::IArray takes the count as the number
// of entries the array *has*, not the room to keep them in, so a stroke
// made with a count would start with that many points at nought - and
// grows a chunk at a time as the points are added.


static Boolean
StartStroke(StrokeSink* sink)
{
	if (sink->fStroke >= sink->fRoom - 1)
		return false;
	sink->fCurrent = TStroke::Make(0);
	sink->fCount = 0;
	return sink->fCurrent != nil;
}


static void
FinishStroke(StrokeSink* sink)
{
	if (sink->fCurrent == nil)
		return;
	if (sink->fCount == 0)
	{
		sink->fCurrent->IDispose();
		sink->fCurrent = nil;
		return;
	}
	sink->fCurrent->EndStroke();
	sink->fStrokes[sink->fStroke++] = sink->fCurrent;
	sink->fStrokes[sink->fStroke] = nil;
	sink->fCurrent = nil;
}


// ROM 0x00153ec4 PGCStorePointProc__FsP6_POINTP4_DCC
// The points the decoder hands out built back into strokes, in the
// 16.16 pixels a stroke keeps them in and moved to where the ink is
// wanted.
static short
PGCStorePointProc(short what, const InkPoint* pt, void* refCon)
{
	StrokeSink* sink = (StrokeSink*) refCon;
	switch (what)
	{
	case kInkBegin:
		return StartStroke(sink) ? 1 : 0;
	case kInkEndStroke:
		FinishStroke(sink);
		return StartStroke(sink) ? 1 : 0;
	case kInkEnd:
		FinishStroke(sink);
		return 1;
	case kInkPoint:
		break;
	default:
		return 1;
	}
	if (sink->fCurrent == nil)
		return 0;
	TabPt tab;
	long x = pt->x + sink->fOffsetX;
	long y = pt->y + sink->fOffsetY;
	tab.x = gTabScale.x == 0x80000 ? (Fixed) ((ULong) x << 13)
								   : FixedDivide(ToFixed(x), gTabScale.x);
	tab.y = gTabScale.y == 0x80000 ? (Fixed) ((ULong) y << 13)
								   : FixedDivide(ToFixed(y), gTabScale.y);
	tab.z = 0;
	tab.p = 0;
	if (sink->fCurrent->AddPoint(&tab) != 0)
	{
		sink->fFailed = true;
		return 0;
	}
	sink->fCount++;
	return 1;
}


// ROM 0x00140c98 InkExpand__FRC6RefVarUllT3
// A block of ink read back into strokes, moved to (x, y).  The answer is
// a list ended by a nil, made with NewPtr; DisposeTStrokes gives it back.
TStroke**
InkExpand(RefArg ink, ULong group, long x, long y)
{
	if (ISNIL(ink) || !IsBinary(ink))
		return nil;
	const void* data = BinaryData(ink);
	TInkCodec* codec = InkCodecFor(data);
	if (codec == nil)
		return nil;
	long size = Length(ink);
	if (IsInkWord(ink))
		size -= (long) sizeof(PackedInkWordInfo);
	const long kRoom = 64;
	TStroke** strokes = (TStroke**) NewPtrClear((kRoom + 1) * (long) sizeof(TStroke*));
	if (strokes == nil)
		return nil;
	StrokeSink sink;
	memset(&sink, 0, sizeof(sink));
	sink.fStrokes = strokes;
	sink.fRoom = kRoom;
	sink.fOffsetX = x;
	sink.fOffsetY = y;
	codec->Decode(data, size, group, PGCStorePointProc, &sink);
	if (sink.fFailed)
	{
		DisposeTStrokes(strokes);
		return nil;
	}
	return strokes;
}
