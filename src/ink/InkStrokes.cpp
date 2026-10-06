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
#include "objects.h"
#include "RSSymbols.h"
#include "NewtonMemory.h"
#include "Ports.h"			// RoundFixed
#include "Rects.h"
#include "Shapes.h"			// LineTo
#include "Draw.h"			// InkerLine
#include "Unit.h"			// FixRect
#include "Locale.h"			// GetPreference
#include "Words.h"			// WRecFindBaseline
#include "DrawShape.h"		// MakePolygonForm
#include "RSSymbols.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"
#include "FixedMath.h"
#include "Paths.h"			// DisposePaths
#include "host/RomBugs.h"

#include <string.h>


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


// ROM 0x001543fc CSCompress__FPP7TStrokei
// The strokes packed by the codec that writes (the ROM's goes straight on
// to GenericCSCompress - TCICInkCodec::Encode); ==> a NewPtr block of the
// bits, or nil.
static void*
CSCompress(StrokeSource* source, long* size)
{
	TInkCodec* codec = InkCodecForWriting();
	if (codec == nil)
		return nil;
	return codec->Encode(PGCGetPointProc, source, size);
}


// ROM 0x00140b78 InkCompress__FPP7TStrokeUc
// A list of strokes packed into a binary: 'ink2 for raw ink, or
// 'inkWord with the word's measurements after it.
//
// An ink word carries what GetPackedInkWordInfoFromStrokes measures of
// the strokes after them.
Ref
InkCompress(TStroke** strokes, Boolean asWord)
{
	StrokeSource source;
	source.fStrokes = strokes;
	source.fStroke = 0;
	source.fPoint = 0;
	long size = 0;
	void* bits = CSCompress(&source, &size);
	if (bits == nil)
		return NILREF;
	long length = asWord ? size + (long) sizeof(PackedInkWordInfo) : size;
	RefVar ink(AllocateBinary(asWord ? RSSYMinkword : RSSYMink2, length));
	char* data = (char*) BinaryData(ink);
	BlockMove(bits, data, size);
	if (asWord)
	{
		PackedInkWordInfo info;
		GetPackedInkWordInfoFromStrokes(strokes, &info);
		BlockMove(&info, data + size, sizeof(info));
	}
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


// ROM 0x00140a4c GetPackedInkWordInfoFromStrokes__FPP7TStrokeP17PackedInkWordInfo
// What a word of strokes measures.  The width and the height are its own
// box with the pen's two pixels in them; the ascent is where the
// recogniser says the short letters stand, held down to the height in
// case it says something silly, and the x-height is how far the line they
// reach up to is from that, held down the same way.  The scale is the
// user's inkWordScaling preference as a fraction of a hundred, and the
// pen size is the user's.
void
GetPackedInkWordInfoFromStrokes(TStroke** strokes, PackedInkWordInfo* packed)
{
	Rect box;
	UnionBounds(strokes, &box);
	long height = (box.bottom - box.top) + kInkSlop;
	Point sits[4];
	WRecFindBaseline(strokes, sits);
	long ascent = (short) ((sits[2].v + sits[3].v) >> 1);
	if (ascent > height)
		ascent = height;
	long xHeight = ((sits[1].v + sits[0].v) >> 1) - ascent;
	if (xHeight < 0)
		xHeight = -xHeight;
	if (xHeight > ascent)
		xHeight = ascent;
	RefVar scaling(GetPreference(RefVar(RSSYMinkwordscaling)));
	Fixed scale = FixedDivide((Fixed) ((ULong) (ISINT(scaling) ? RVALUE(scaling) : 0) << 16), 0x640000);
	RefVar size(GetPreference(RefVar(RSSYMuserpensize)));
	ULong pen = (ULong) (ISINT(size) ? RVALUE(size) : 1);
	PackInkWordInfo(packed, (ULong) ((box.right - box.left) + kInkSlop), (ULong) ascent,
					(ULong) (height - ascent), (ULong) xHeight, scale, 0, pen);
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
// how many strokes the expanders have room for (the ROM's 400-byte block)
const long kCSExpandRoom = 100;

struct StrokeSink
{
	TStroke**	fStrokes;		// room for the answers, ended by a nil
	long		fRoom;			// how many that is
	long		fStroke;		// the one being filled
	TStroke*	fCurrent;
	long		fCount;			// how many points it has taken
	Fixed		fOffsetX;		// where the ink is to be put, in pixels
	Fixed		fOffsetY;
	Fixed		fScaleX;		// and what it is scaled by first
	Fixed		fScaleY;
	Boolean		fFailed;
	// the raw form (CSRawExpandGroup): each stroke a handle of 16.16
	// points instead of a TStroke
	Boolean		fRaw;
	Handle*		fHandles;		// the answers, ended by a nil
	Handle		fHandle;		// the one being filled
	long		fAllocated;		// its size
	long		fUsed;			// and how much of it is points
};

// A stroke is made empty - TArray::IArray takes the count as the number
// of entries the array *has*, not the room to keep them in, so a stroke
// made with a count would start with that many points at nought - and
// grows a chunk at a time as the points are added.


// ROM 0x00153d10 BeginStroke__FP14_EXPAND_PARAMS
// A stroke begun, while there is room for it: a TStroke, or in the raw
// form an empty handle.
static Boolean
StartStroke(StrokeSink* sink)
{
	if (sink->fRaw)
	{
		if (sink->fStroke >= sink->fRoom)
			return false;
		sink->fHandle = NewHandle(0);
		if (sink->fHandle == nil)
			return false;
		sink->fHandles[sink->fStroke] = sink->fHandle;
		sink->fUsed = 0;
		sink->fAllocated = 0;
		sink->fCount = 0;
		return true;
	}
	if (sink->fStroke >= sink->fRoom - 1)
		return false;
	sink->fCurrent = TStroke::Make(0);
	sink->fCount = 0;
	return sink->fCurrent != nil;
}


// ROM 0x00153e10 AddStrokePoint__FP14_EXPAND_PARAMSlT2
// A raw point added to the handle, which grows 0x40 bytes at a time (a
// point that will not fit is dropped).
static void
AddRawPoint(StrokeSink* sink, Fixed x, Fixed y)
{
	if (sink->fHandle == nil)
		return;
	if (sink->fAllocated < sink->fUsed + (long) sizeof(point))
	{
		if (SetHandleSize(sink->fHandle, sink->fAllocated + 0x40) != noErr)
			return;
		sink->fAllocated += 0x40;
	}
	point* pt = (point*) (*sink->fHandle + sink->fUsed);
	pt->x = x;
	pt->y = y;
	sink->fUsed += (long) sizeof(point);
}


// ROM 0x00153d94 EndStroke__FP14_EXPAND_PARAMS
// A raw stroke ended: one of a single point has it twice, so that it is
// a line, and the handle is cut to its points.
static void
EndRawStroke(StrokeSink* sink)
{
	if (sink->fUsed == (long) sizeof(point))
	{
		point first = *(point*) *sink->fHandle;
		AddRawPoint(sink, first.x, first.y);
	}
	SetHandleSize(sink->fHandle, sink->fUsed);
	sink->fHandle = nil;
	if (++sink->fStroke < sink->fRoom)
		sink->fHandles[sink->fStroke] = nil;
}


static void
FinishStroke(StrokeSink* sink)
{
	if (sink->fRaw)
	{
		// (the ROM begins a stroke at its first point, so one never ends
		// empty: an empty one here is given back)
		if (sink->fHandle == nil)
			return;
		if (sink->fCount == 0)
		{
			DisposeHandle(sink->fHandle);
			sink->fHandles[sink->fStroke] = nil;
			sink->fHandle = nil;
		}
		else
			EndRawStroke(sink);
		return;
	}
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
	if (sink->fRaw ? sink->fHandle == nil : sink->fCurrent == nil)
		return 0;
	// the point comes in as whole tablet units: a negative one (the ink
	// was written off the left or the top) is brought back to nought,
	// and the rest is the tablet scale out, the scale asked for, and the
	// place the ink is to go - in that order, all in 16.16 pixels.
	TabPt tab;
	Fixed x = ToFixed(pt->x);
	Fixed y = ToFixed(pt->y);
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	x = gTabScale.x == 0x80000 ? (Fixed) ((ULong) x >> 3) : FixedDivide(x, gTabScale.x);
	y = gTabScale.y == 0x80000 ? (Fixed) ((ULong) y >> 3) : FixedDivide(y, gTabScale.y);
	if (sink->fScaleX != 0x10000)
		x = FixedMultiply(x, sink->fScaleX);
	if (sink->fScaleY != 0x10000)
		y = FixedMultiply(y, sink->fScaleY);
	if (sink->fRaw)
	{
		AddRawPoint(sink, AddFixed(x, sink->fOffsetX), AddFixed(y, sink->fOffsetY));
		sink->fCount++;
		return 1;
	}
	tab.x = AddFixed(x, sink->fOffsetX);
	tab.y = AddFixed(y, sink->fOffsetY);
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


// ROM 0x001538bc GenericCSExpandGuts__FP14CSStrokeHeaderPPvUllN34Uc
// The ink read into the list given - TStrokes, or (raw) handles of 16.16
// points - scaled and then moved; a group above 1 is taken as 0.  ==>
// how many strokes there are.
static long
GenericCSExpandGuts(const void* data, long size, void** list, ULong group,
					Fixed x, Fixed y, Fixed scaleX, Fixed scaleY, Boolean strokes)
{
	if (group != 0 && group != 1)
		group = 0;
	StrokeSink sink;
	memset(&sink, 0, sizeof(sink));
	sink.fRaw = !strokes;
	sink.fStrokes = (TStroke**) list;
	sink.fHandles = (Handle*) list;
	sink.fRoom = kCSExpandRoom;
	sink.fOffsetX = x;
	sink.fOffsetY = y;
	sink.fScaleX = scaleX;
	sink.fScaleY = scaleY;
	TInkCodec* codec = data != nil ? InkCodecFor(data) : nil;
	if (codec != nil)
		codec->Decode(data, size, group, PGCStorePointProc, &sink);
	return sink.fStroke;
}


// ROM 0x00153934 GenericCSExpandGroup__FP14CSStrokeHeaderUllN33Uc
// The same into a block of a hundred, cut to what there are and ended by
// a nil.  nil when there is no memory for the block (the strokes are lost
// with it when it cannot be cut, as the ROM's are).
static void**
GenericCSExpandGroup(const void* data, long size, ULong group,
					 Fixed x, Fixed y, Fixed scaleX, Fixed scaleY, Boolean strokes)
{
	void** list = (void**) NewPtr(kCSExpandRoom * (long) sizeof(void*));
	if (list == nil)
		return nil;
	list[0] = nil;
	long count = GenericCSExpandGuts(data, size, list, group, x, y, scaleX, scaleY, strokes);
	void** answer = (void**) ReallocPtr((Ptr) list, (count + 1) * (long) sizeof(void*));
	if (answer == nil)
		return nil;
	answer[count] = nil;
	return answer;
}


// ROM 0x00154420 CSExpandGroup__FP14CSStrokeHeaderUllT3
// The ink as TStrokes at (x, y), 16.16 pixels, the size it was written.
static TStroke**
CSExpandGroup(const void* data, long size, ULong group, Fixed x, Fixed y)
{
	return (TStroke**) GenericCSExpandGroup(data, size, group, x, y, 0x10000, 0x10000, true);
}


// ROM 0x00140c98 InkExpand__FRC6RefVarUllT3
// A block of ink read back into strokes, moved to (x, y).  The answer is
// a list ended by a nil, made with NewPtr; DisposeTStrokes gives it back.
// (A stroke the decoder could not add a point to is kept as far as it
// got, as the ROM keeps it: nothing above the sink asks.)
TStroke**
InkExpand(RefArg ink, ULong group, long x, long y)
{
	if (ISNIL(ink) || !IsBinary(ink))
		return nil;
	long size = Length(ink);
	if (IsInkWord(ink))
		size -= (long) sizeof(PackedInkWordInfo);
	return CSExpandGroup(BinaryData(ink), size, group, (Fixed) ((ULong) x << 16), (Fixed) ((ULong) y << 16));
}


// ROM 0x0015445c CSRawExpandGroup__FP14CSStrokeHeaderUllN33
// The ink read back as raw strokes - each a handle of 16.16 points,
// scaled and then moved.
static Handle*
InkRawExpand(const void* data, long size, ULong group, Fixed x, Fixed y, Fixed scaleX, Fixed scaleY)
{
	return (Handle*) GenericCSExpandGroup(data, size, group, x, y, scaleX, scaleY, false);
}


// ROM 0x001534e8 GenericCSMakePathsGroup__FP14CSStrokeHeaderlN32
// The ink as outlined paths, which is how it goes to a printer: each raw
// stroke's handle made in place a paths of one contour - the count, the
// control bits (all nought: every point on the curve, so a polyline) and
// the points moved up behind them - a stroke there is no room to do
// that for given back and left out.  A nil-ended block of pathsHandles;
// nil when there is no memory for it.
pathsHandle*
InkMakePathsScaled(const void* data, long size, Fixed x, Fixed y, Fixed scaleX, Fixed scaleY)
{
	Handle* list = InkRawExpand(data, size, 0, x, y, scaleX, scaleY);
	if (list == nil)
		return nil;
	long kept = 0;
	for (long i = 0; list[i] != nil; i++)
	{
		Handle h = list[i];
		long bytes = GetHandleSize(h);
		long points = bytes / (long) sizeof(point);
		long words = (points + 31) >> 5;
		long header = words * 4 + 8;
		if (SetHandleSize(h, bytes + header) == noErr)
		{
			memmove(*h + header, *h, bytes);
			paths* p = (paths*) *h;
			p->contours = 1;
			path* contour = p->contour;
			contour->vectors = points;
			for (long w = 0; w < words; w++)
				contour->controlBits[w] = 0;
			contour->controlBits[0] &= 0x7fffffff;
			long last = points - 1;
			contour->controlBits[last >> 5] &= ~(Long32) (0x80000000U >> (last & 0x1f));
			list[kept++] = h;
		}
		else
			DisposePaths((pathsHandle) h);
	}
	list[kept] = nil;
	return (pathsHandle*) list;
}


// ROM 0x00153470 CSMakePathsGroup__FP14CSStrokeHeaderlT2
pathsHandle*
CSMakePathsGroup(const void* data, long size, Fixed x, Fixed y)
{
	return InkMakePathsScaled(data, size, x, y, 0x10000, 0x10000);
}


// ROM 0x0015348c CSMakePathsGroupInRect__FP14CSStrokeHeaderlT2P5FRect
pathsHandle*
CSMakePathsGroupInRect(const void* data, long size, Fixed width, Fixed height, const FRect* to)
{
	return InkMakePathsScaled(data, size, to->left, to->top,
							  FixedDivide(to->right - to->left, width),
							  FixedDivide(to->bottom - to->top, height));
}


// ROM 0x00140d9c InkMakePaths__FRC6RefVarlT2
// A shape's ink as paths at (x, y), the size it was written.
pathsHandle*
InkMakePaths(RefArg ink, long x, long y)
{
	long size = 0;
	const void* data = InkData(ink, &size);
	return CSMakePathsGroup(data, size, ToFixed(x), ToFixed(y));
}


/*------------------------------------------------------------------------------
	I n k   d r a w n
------------------------------------------------------------------------------*/

// What PGCDrawPointProc is drawing with (GenericCSDraw's block): the pen,
// where the ink is to go, how much it is to be scaled, and whether it is
// drawn with the inker's line drawer.
struct InkDrawing
{
	ULong	fPen;			// +00 the pen, both ways (its low half)
	Fixed	fX;				// +04 where the ink's origin goes
	Fixed	fY;				// +08
	Fixed	fScaleX;		// +0C and what it is scaled by first
	Fixed	fScaleY;		// +10
	Boolean	fUseInker;		// +14 the ink is wholly inside a rectangular clip
};

// ... and the points PGCDrawPointProc keeps back (the ROM's _DPINST, 0x68
// bytes, allocated at kInkBegin): up to twenty, drawn in one go.  A point
// is kept as the ROM packs it, v in the high half of a word and h in the
// low, and 0xffffffff means none.
const long	kInkBufferedPoints = 20;

struct InkPointBuffer
{
	long			fStarting;		// +00 the next point begins a stroke
	long			fDone;			// +04 kInkEnd has been seen
	InkDrawing*		fDrawing;		// +08
	ULong32			fDrawn;			// +0C where the last line drawn ended
	ULong32			fLast;			// +10 the last point kept (a repeat is not kept again)
	ULong			fCount;			// +14
	ULong32			fPoints[kInkBufferedPoints];	// +18
};


// ROM 0x0015407c DrawBufferedPoints__FP7_DPINST
// The points kept back drawn: with QuickDraw, a line to each from the
// pen's place (the pen set first), or with the inker's line drawer
// (InkerLine) straight into the port's bits, each line from the end of
// the last - which carries its own pen and needs no clipping, the caller
// having found the ink wholly inside a rectangular clip.  The first point
// of a stroke is where the pen was moved to: QuickDraw starts at the
// second (a lone point drawn as a line to itself - a dot), the inker at
// the first.
static void
DrawBufferedPoints(InkPointBuffer* buffer)
{
	if (buffer->fCount == 0)
		return;
	InkDrawing* drawing = buffer->fDrawing;
	short penSize = (short) drawing->fPen;
	Point pen;
	pen.v = penSize;
	pen.h = penSize;
	ULong32* from = &buffer->fDrawn;
	ULong i = 0;
	if (buffer->fDrawn == 0xffffffff)
	{
		from = &buffer->fPoints[0];
		if (buffer->fCount > 1)
			i = 1;
	}
	ULong32* at = &buffer->fPoints[i];
	GrafPtr port;
	GetPort(&port);
	if (!drawing->fUseInker)
		PenSize(penSize, penSize);
	for ( ; i < buffer->fCount; i++, at++)
	{
		Point to;
		to.v = (short) (*at >> 16);
		to.h = (short) *at;
		if (!drawing->fUseInker)
			LineTo(to.h, to.v);
		else
		{
			Point start;
			start.v = (short) (*from >> 16);
			start.h = (short) *from;
			Rect damaged;
			InkerLine(start, to, &damaged, pen, &port->portBits);
			from = at;
		}
	}
	buffer->fDrawn = *from;
	buffer->fCount = 0;
}


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
// The points the decoder hands out drawn: each brought to a pixel of the
// port and kept back (a repeat of the last one is not kept again), the
// pen moved to the first of a stroke, and the ones kept drawn twenty at a
// time and at the end of each stroke (DrawBufferedPoints).
// (ROM BUG (fixed): kInkEnd gives the buffer back without drawing what is
// still in it; the decoder ends every stroke with kInkEndStroke first,
// which draws them.  The fix draws them at kInkEnd too - nothing, after a
// kInkEndStroke.)
// Host: the buffer is the caller's, beside the drawing block, where the
// ROM allocates it at kInkBegin (HWRMemoryAlloc, answering 0 when it
// cannot) and frees it at kInkEnd.
struct InkDrawState
{
	InkPointBuffer*	fBuffer;		// the ROM's _DCC +4
	InkDrawing*		fDrawing;		// and +0x14
	InkPointBuffer	fStorage;
};

static short
PGCDrawPointProc(short what, const InkPoint* pt, void* refCon)
{
	InkDrawState* state = (InkDrawState*) refCon;
	InkPointBuffer* buffer = state->fBuffer;
	InkDrawing* drawing = buffer != nil ? buffer->fDrawing : nil;
	switch (what)
	{
	case kInkBegin:
		buffer = &state->fStorage;
		state->fBuffer = buffer;
		buffer->fDone = 0;
		buffer->fStarting = 1;
		buffer->fDrawing = state->fDrawing;
		buffer->fCount = 0;
		return 1;
	case kInkEndStroke:
		if (buffer == nil || drawing == nil)
			return 0;
		buffer->fStarting = 1;
		break;
	case kInkPoint:
	{
		if (buffer == nil || drawing == nil)
			return 0;
		if (buffer->fDone)
			return 1;
		long h = InkPixel(pt->x, drawing->fScaleX, drawing->fX, gTabScale.x);
		long v = InkPixel(pt->y, drawing->fScaleY, drawing->fY, gTabScale.y);
		ULong32 packed = ((ULong32) (UShort) v << 16) | (UShort) h;
		if (buffer->fStarting)
		{
			buffer->fStarting = 0;
			MoveTo((short) h, (short) v);
			buffer->fDrawn = 0xffffffff;
			buffer->fLast = 0xffffffff;
		}
		if (buffer->fLast != packed)
		{
			buffer->fPoints[buffer->fCount++] = packed;
			buffer->fLast = packed;
		}
		if (buffer->fCount < kInkBufferedPoints)
			return 1;
		break;
	}
	case kInkEnd:
		if (buffer == nil || drawing == nil)
			return 0;
		buffer->fStarting = 1;
		if (buffer->fDone)
			return 1;
		if (RomBugFixed())
			DrawBufferedPoints(buffer);
		buffer->fDone = 1;
		state->fBuffer = nil;
		return 1;
	default:
		return 1;
	}
	DrawBufferedPoints(buffer);
	return 1;
}


// (host) The packed strokes of an ink object and their length.  The
// ROM's drawing functions are handed the block itself and read the
// length out of its header; this reconstruction's codec seam is given
// the length, so the two are worked out together here.
const void*
InkData(RefArg ink, long* outSize)
{
	if (ISNIL(ink) || !IsBinary(ink))
		return nil;
	long size = Length(ink);
	if (IsInkWord(ink))
		size -= (long) sizeof(PackedInkWordInfo);
	if (outSize != nil)
		*outSize = size;
	return BinaryData(ink);
}


// ROM 0x00153884 GenericCSDraw__FP14CSStrokeHeaderUllN33Uc
// Ink drawn into the current port at a place and a scale.
//
// The pen is the width the lines are drawn with, and is *not* the
// decoder's group: drawing always asks the decoder for every point
// (group 0, which Decode turns into thinning mode 3), and the pen
// travels beside the place and the scale in the block the point proc
// reads, which DrawBufferedPoints sets it from for each batch of points.
void
InkDrawScaled(const void* data, long size, ULong pen, Fixed x, Fixed y,
			  Fixed scaleX, Fixed scaleY, Boolean useInker)
{
	if (data == nil)
		return;
	TInkCodec* codec = InkCodecFor(data);
	if (codec == nil)
		return;
	InkDrawing to;
	to.fPen = pen;
	to.fX = x;
	to.fY = y;
	to.fScaleX = scaleX;
	to.fScaleY = scaleY;
	to.fUseInker = useInker;
	InkDrawState state;
	state.fBuffer = nil;
	state.fDrawing = &to;
	codec->Decode(data, size, 0, PGCDrawPointProc, &state);
}


// ROM 0x001544bc CSDrawInRect__FP14CSStrokeHeaderUllT3P5FRectUc
// Ink drawn stretched out of the size it was made at and into a
// rectangle: the scale is what the rectangle is of that size, in both
// directions, and the ink goes to the rectangle's top-left corner.
void
InkDrawInFRect(const void* data, long size, ULong pen, Fixed width, Fixed height,
			   const FRect* to, Boolean useInker)
{
	if (width == 0 || height == 0)
		return;
	InkDrawScaled(data, size, pen, to->left, to->top,
				  FixedDivide(to->right - to->left, width),
				  FixedDivide(to->bottom - to->top, height), useInker);
}


// ROM 0x00140d14 InkDrawInRect__FRC6RefVarUlP4RectT3Uc
// The same when both boxes are whole pixels.
void
InkDrawInRect(RefArg ink, ULong pen, const Rect* from, const Rect* to, Boolean useInker)
{
	long size = 0;
	const void* data = InkData(ink, &size);
	FRect dst;
	FixRect(&dst, to);
	InkDrawInFRect(data, size, pen, (Fixed) ((ULong) (from->right - from->left) << 16),
				   (Fixed) ((ULong) (from->bottom - from->top) << 16), &dst, useInker);
}


// ROM 0x00153844 GenericCSDraw__FP14CSStrokeHeaderUllT3Uc
// The ink drawn at the size it was written: the same block as the scaled
// one's with the two scales one.
void
GenericCSDraw(const void* data, long size, ULong pen, Fixed x, Fixed y, Boolean useInker)
{
	InkDrawScaled(data, size, pen, x, y, 0x10000, 0x10000, useInker);
}


// ROM 0x00154494 CSDraw__FP14CSStrokeHeaderUllT3Uc
void
CSDraw(const void* data, long size, ULong pen, Fixed x, Fixed y, Boolean useInker)
{
	GenericCSDraw(data, size, pen, x, y, useInker);
}


// ROM 0x00140cd0 InkDraw__FRC6RefVarUllT3Uc
// The same, at the size it was written.
void
InkDraw(RefArg ink, ULong pen, long x, long y, Boolean useInker)
{
	long size = 0;
	const void* data = InkData(ink, &size);
	CSDraw(data, size, pen, (Fixed) ((ULong) x << 16), (Fixed) ((ULong) y << 16), useInker);
}


/*------------------------------------------------------------------------------
	I n k   a s   a   s h a p e
------------------------------------------------------------------------------*/

// (the pen the user writes with, or one when nobody has said)
static long
UserPenSize(void)
{
	RefVar size(GetPreference(RefVar(RSSYMuserpensize)));
	return ISINT(size) ? RVALUE(size) : 1;
}


// ROM 0x001a31bc MakeInkPoly__FPP7TStroke
// A sketch as a shape: the strokes packed into ink, a shape frame of the
// ink verb over the box they came from, and the ink hung off it.
Ref
MakeInkPoly(TStroke** strokes)
{
	Rect box;
	RefVar ink(TStrokesToInk(strokes, &box));
	RefVar form(MakePolygonForm(nil, 0, kInkVerb, box, UserPenSize()));
	SetFrameSlot(form, RSSYMink, ink);
	return form;
}


// ROM 0x001a3250 MakeInkWordPoly__FPP7TStroke
// A word as a shape.  The box is not the one the strokes came out of but
// the one the word's own measurements make - as wide as the word and as
// tall as its ascent and descent together, with the pen in both - so
// that a line of text can put it where it belongs.
Ref
MakeInkWordPoly(TStroke** strokes)
{
	Rect box;
	RefVar ink(TStrokesToInkWord(strokes, &box));
	InkWordInfo info;
	GetInkWordInfo(ink, &info);
	box.right = (short) (box.left + info.fWidth + info.fPenSize);
	box.bottom = (short) (box.top + info.fAscent + info.fDescent + info.fPenSize);
	RefVar form(MakePolygonForm(nil, 0, kInkVerb, box, (long) info.fPenSize));
	SetFrameSlot(form, RSSYMink, ink);
	return form;
}
