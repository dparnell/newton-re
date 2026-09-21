/*
	File:		ink/InkCodec.h

	Contains:	The boundary between the ink Newton OS keeps and the
				codec that packs strokes into it.

				This interface is NOT the ROM's - the ROM calls its
				codec directly - but it is the ROM's own seam made
				explicit.  Apple's side of the ink is three layers deep:
				InkCompress/InkExpand/InkDraw/InkMakePaths (Ink.h) call
				CSCompress/CSExpandGroup/CSDraw/CSMakePathsGroup
				(0x001543fc onwards), which call Decode (0x001539c8) and
				GenericCSCompress (0x0015362c), which call
				EncoderOpen/Run/Close and DecoderOpen/Run/Close
				(0x0027f938, 0x0028240c) - the CIC handwriting library,
				not Apple's code.  The line between the third layer and
				the fourth is where the codec stops and the operating
				system begins, and it is exactly a stream interface:
				strokes in and bits out, bits in and a stream of points
				out.

				So that is where TInkCodec sits.  The reconstruction of
				the ROM's codec is the first implementation (CICCodec.h);
				a modern one can be another, without the views, the
				paragraphs or the stores knowing.

				What must not be swapped is the *reading* of ink already
				written: a note written on a real Newton is in the CIC
				format for ever, so that decoder has to stay whatever
				else is added.  The ROM's own GetInkFormat (0x00280950)
				reads a format out of the first byte of the data and
				already tells four of them apart, so choosing a codec per
				object rather than once for the machine is what the
				format was built for.  InkCodecFor does that; a codec
				registered later says which formats it can read.

				A decoder hands its points out through a callback, as the
				ROM's does (PGCStorePointProc builds strokes with it,
				PGCDrawPointProc draws with it, CSMakePathsGroup makes
				paths with it): one traversal, three uses.  The ROM
				passes its decoder context as the callback's third
				argument and has the callback find its own data hanging
				off it; here the caller's reference is passed as itself.
*/

#ifndef __INKCODEC_H
#define __INKCODEC_H

#ifndef __OBJECTS_H
#include "objects.h"
#endif
#ifndef __FIXEDMATH_H
#include "FixedMath.h"
#endif

struct FPoint;
class TStroke;


// What a point sink is told.  The points are in tablet units, as 16.16
// values; a stroke ends with kInkEndStroke and the group with kInkEnd.
// A sink answers 0 to give up.
const short kInkBegin		= 1;
const short kInkEndStroke	= 2;
const short kInkPoint		= 3;
const short kInkEnd			= 4;

typedef short (*InkPointProc)(short what, const FPoint* pt, void* refCon);


// The ink formats GetInkFormat tells apart, by the first byte of the
// data.  0, 1 and 3 are the compressed forms the CIC codec writes; 2 is
// anything else, which is the old uncompressed ink.
const long kInkFormatOld		= 2;
const long kInkFormatCompressed	= 1;
const long kInkFormatHigh		= 0;
const long kInkFormatWide		= 3;

long	GetInkFormat(const void* data);			// ROM 0x00280950 GetInkFormat__FPv


// A codec: something that turns strokes into a block of ink and a block
// of ink back into a stream of points.
class TInkCodec
{
public:
	virtual				~TInkCodec();

	// What it is, for the record and for the tests.
	virtual const char*	Name(void) const = 0;

	// Whether it can read ink of this format (one of the kInkFormat
	// constants), and whether it writes ink at all.
	virtual Boolean		CanDecode(long format) const = 0;
	virtual Boolean		CanEncode(void) const = 0;

	// The ink walked, a point at a time, into the sink.  `group` is
	// which group of strokes to walk (the ROM's CSExpandGroup takes one
	// and clamps anything above 1 to 0).  ==> whether it got to the end.
	virtual Boolean		Decode(const void* data, long size, ULong group,
							   InkPointProc sink, void* refCon) const = 0;

	// The strokes packed into a newly allocated block (NewPtr, the
	// caller disposes of it), and its size.  nil when it cannot.
	virtual void*		Encode(TStroke** strokes, long* outSize) const = 0;
};


// The codecs the machine has.  The first registered that can read a
// format is the one that reads it, and the first registered that can
// write is the one that writes, so the ROM's own goes in first and
// stays the default until something says otherwise.
void		RegisterInkCodec(TInkCodec* codec);
TInkCodec*	InkCodecFor(const void* data);		// by the format in the data
TInkCodec*	InkCodecForWriting(void);
long		CountInkCodecs(void);
TInkCodec*	IndexedInkCodec(long index);

#endif	/* __INKCODEC_H */
