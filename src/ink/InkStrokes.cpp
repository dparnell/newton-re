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
#include "Rects.h"
#include "Shapes.h"			// LineTo
#include "Unit.h"			// FixRect
#include "Locale.h"			// GetPreference
#include "NewtonExceptions.h"
#include "OSErrors.h"
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
	W h e r e   t h e   s t r o k e s   a r e
------------------------------------------------------------------------------*/

// ROM 0x001a36bc UnionBounds__FPP7TStrokeP5TRect
// The box all the strokes together cover.  (The ROM starts with the top
// and the bottom at -32768, which is what says a rectangle holds nothing
// yet, and leaves the sides alone; the host sets all four, because a
// local that is never read on the Newton is still a local here.)
void
UnionBounds(TStroke** strokes, Rect* rect)
{
	SetRect(rect, -0x8000, -0x8000, -0x8000, -0x8000);
	for (long i = 0; strokes[i] != nil; i++)
	{
		Rect one;
		GetStrokeRect(strokes[i], &one);
		UnionRect(rect, &one, rect);
	}
}


// ROM 0x001a3750 OffsetStrokes__FPP7TStrokelT2
// Every stroke of the list moved.
void
OffsetStrokes(TStroke** strokes, long dx, long dy)
{
	for (long i = 0; strokes[i] != nil; i++)
		strokes[i]->Offset(dx, dy);
}


// ROM 0x001a3728 InkBounds__FPP7TStrokeP5TRect
// Where the ink of a list of strokes reaches: their own box, and two
// pixels more each way for the pen.
void
InkBounds(TStroke** strokes, Rect* rect)
{
	UnionBounds(strokes, rect);
	rect->top = (short) (rect->top - kInkSlop);
	rect->left = (short) (rect->left - kInkSlop);
	rect->bottom = (short) (rect->bottom + kInkSlop);
	rect->right = (short) (rect->right + kInkSlop);
}


// ROM 0x00140318 ScaleStrokesForInkWord__FPP7TStrokeP5TRect
// A word written larger than a line of text can hold is brought down to
// fit: two hundred and forty pixels across and sixty down are the most,
// and whichever of the two wants the smaller scale is the one used, so
// the word keeps its shape.  A word that already fits is left alone.
//
// Each stroke is mapped from its own box into that box scaled about the
// word's top-left corner, which moves the strokes as well as shrinking
// them.
void
ScaleStrokesForInkWord(TStroke** strokes, Rect* rect)
{
	Fixed width = (Fixed) ((ULong) (rect->right - rect->left) << 16);
	Fixed height = (Fixed) ((ULong) (rect->bottom - rect->top) << 16);
	Fixed across = 0;
	Fixed down = 0;
	if (width > 0xf00000)
		across = FixedDivide(0xf00000, width);
	if (height > 0x3c0000)
		down = FixedDivide(0x3c0000, height);
	Fixed scale = down;
	if (across != 0 && (down == 0 || across <= down))
		scale = across;
	if (scale == 0)
		return;
	FRect box;
	FixRect(&box, rect);
	for (long i = 0; strokes[i] != nil; i++)
	{
		TStroke* stroke = strokes[i];
		FRect to;
		to.left = FixedMultiply(stroke->fBBox.left - box.left, scale) + box.left;
		to.top = FixedMultiply(stroke->fBBox.top - box.top, scale) + box.top;
		to.right = FixedMultiply(stroke->fBBox.right - box.left, scale) + box.left;
		to.bottom = FixedMultiply(stroke->fBBox.bottom - box.top, scale) + box.top;
		stroke->Map(&to);
	}
	rect->right = (short) (rect->left + RoundFixed(FixedMultiply(width, scale)));
	rect->bottom = (short) (rect->top + RoundFixed(FixedMultiply(height, scale)));
}


/*------------------------------------------------------------------------------
	S t r o k e s   m a d e   i n t o   i n k
------------------------------------------------------------------------------*/

// (the strokes moved so that their box, grown by the pen's two pixels,
// starts at the origin - ink is kept where it was drawn, not where it is
// to go)
static void
MoveToOrigin(TStroke** strokes, Rect* box)
{
	OffsetStrokes(strokes, (long) ((ULong) -(box->left - kInkSlop) << 16),
						   (long) ((ULong) -(box->top - kInkSlop) << 16));
	InsetRect(box, -kInkSlop, -kInkSlop);
}


// ROM 0x00140608 TStrokesToInk__FPP7TStrokeP5TRect
// A sketch: the strokes moved to the origin and packed, and the box they
// came from answered.
Ref
TStrokesToInk(TStroke** strokes, Rect* outRect)
{
	Rect box;
	UnionBounds(strokes, &box);
	MoveToOrigin(strokes, &box);
	if (outRect != nil)
		*outRect = box;
	RefVar ink(InkCompress(strokes, false));
	if (ISNIL(ink))
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	return ink;
}


// ROM 0x001404f0 TStrokesToInkWord__FPP7TStrokeP5TRect
// A word: the same, with the strokes first brought down to a size a line
// of text can hold, and the box given the pen's width on its bottom and
// right because that is where the ink of the last stroke spills.
Ref
TStrokesToInkWord(TStroke** strokes, Rect* outRect)
{
	Rect box;
	UnionBounds(strokes, &box);
	ScaleStrokesForInkWord(strokes, &box);
	MoveToOrigin(strokes, &box);
	if (outRect != nil)
	{
		*outRect = box;
		RefVar size(GetPreference(RefVar(RSSYMuserpensize)));
		short pen = (short) (ISINT(size) ? RVALUE(size) : 0);
		outRect->bottom = (short) (outRect->bottom + pen);
		outRect->right = (short) (outRect->right + pen);
	}
	RefVar ink(InkCompress(strokes, true));
	if (ISNIL(ink))
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
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


/*------------------------------------------------------------------------------
	I n k   d r a w n
------------------------------------------------------------------------------*/

// What PGCDrawPointProc is drawing with: where the ink is to go, how
// much it is to be scaled, and whether the next point starts a line.
struct InkDrawing
{
	Fixed	fX;				// where the ink's origin goes
	Fixed	fY;
	Fixed	fScaleX;		// and what it is scaled by first
	Fixed	fScaleY;
	Boolean	fStarting;		// the next point begins a stroke
};


// (a point of ink, in whole tablet units, brought to a pixel of the
// port: divided by the tablet scale, scaled, and moved)
static long
InkPixel(long value, Fixed scale, Fixed offset, Fixed tabScale)
{
	Fixed at = (Fixed) ((ULong) value << 16);
	if (at < 0)
		at = 0;
	at = tabScale == 0x80000 ? at >> 3 : FixedDivide(at, tabScale);
	if (scale != 0x10000)
		at = FixedMultiply(at, scale);
	return (offset + at + 0x8000) >> 16;
}


// ROM 0x00154194 PGCDrawPointProc__FsP6_POINTP4_DCC
// The points the decoder hands out drawn as they come: the first of a
// stroke moves the pen and the rest are lines from it.
//
// (The ROM keeps twenty points back at a time and draws them in one go,
// which saves calls and nothing else, and has a second way of drawing
// them - InkerLine with a pen of its own - which is the live inker's and
// is NOT YET.)
static short
PGCDrawPointProc(short what, const InkPoint* pt, void* refCon)
{
	InkDrawing* to = (InkDrawing*) refCon;
	switch (what)
	{
	case kInkBegin:
	case kInkEndStroke:
		to->fStarting = true;
		return 1;
	case kInkPoint:
		break;
	default:
		return 1;
	}
	long h = InkPixel(pt->x, to->fScaleX, to->fX, gTabScale.x);
	long v = InkPixel(pt->y, to->fScaleY, to->fY, gTabScale.y);
	if (to->fStarting)
	{
		MoveTo(h, v);
		to->fStarting = false;
	}
	else
		LineTo(h, v);
	return 1;
}


// ROM 0x00153844 GenericCSDraw__FP14CSStrokeHeaderUlllUc
// Ink drawn into the current port at a place and a scale.
void
InkDrawScaled(RefArg ink, ULong group, Fixed x, Fixed y, Fixed scaleX, Fixed scaleY)
{
	if (ISNIL(ink) || !IsBinary(ink))
		return;
	const void* data = BinaryData(ink);
	TInkCodec* codec = InkCodecFor(data);
	if (codec == nil)
		return;
	long size = Length(ink);
	if (IsInkWord(ink))
		size -= (long) sizeof(PackedInkWordInfo);
	InkDrawing to;
	to.fX = x;
	to.fY = y;
	to.fScaleX = scaleX;
	to.fScaleY = scaleY;
	to.fStarting = true;
	codec->Decode(data, size, group, PGCDrawPointProc, &to);
}


// ROM 0x00140cd0 InkDraw__FRC6RefVarUllT3Uc
// The same, at the size it was written.
void
InkDraw(RefArg ink, ULong group, long x, long y)
{
	InkDrawScaled(ink, group, (Fixed) ((ULong) x << 16), (Fixed) ((ULong) y << 16),
				  0x10000, 0x10000);
}

