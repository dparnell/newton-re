/*
	File:		text/TXArray.h

	Contains:	The text engine's arrays - the foundation everything in
				`TXView` and `Textension` is built on.

				The Newton has two text systems.  One is the paragraph
				(`views/ParagraphView.h`): a string and a styles array in
				a frame, laid out into lines, which is what a note and a
				name field are.  The other, this one, is a *document*
				engine - styled text with rulers, tabs, page breaks and
				graphics runs, written in ordinary C++ over its own
				storage rather than over the object heap.  It is what the
				Works word processor and the built-in books are drawn
				with, and a script reaches it through `protoTXView`.

				`TXArray` is its growable array of fixed-size elements,
				kept in a relocatable handle.  `TXLongTagArray` is one
				whose elements begin with a long that the array is sorted
				by, so an element can be found by binary search.
				`TXRanges` is one of those whose longs are the *ends* of
				consecutive ranges - element i holds where range i ends,
				so range i runs from element i-1's value to element i's,
				and the whole array covers 0 to the last value with no
				gaps.  That is how the engine records which run of
				characters each style, each line and each paragraph
				covers.

	Reconstructed from the MP2x00 US ROM (0x002306c8-0x00231010,
	0x00234404); each function cites its origin.
*/

#ifndef __TXARRAY_H
#define __TXARRAY_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

typedef long	TXOffset;			// a character offset in the text

// Two offsets: the ROM passes these by pointer to say what a range covers.
struct TXOffsetPair
{
	TXOffset	fStart;				// +0x00
	TXOffset	fEnd;				// +0x04
};

// What SectRanges works out about the ranges a stretch of text crosses.
struct TXSectRanges
{
	long		fFirstIndex;		// +0x00  the range the stretch starts in
	long		fStartOffset;		// +0x04  how far into it the stretch starts
	long		fFirstLen;			// +0x08  how much of that range the stretch covers
	long		fWholeIndex;		// +0x0c  the first range covered end to end
	long		fWholeCount;		// +0x10  how many are covered end to end
	long		fLastIndex;			// +0x14  the range the stretch ends in
	long		fEndRemainder;		// +0x18  how much of that range is left after it
	long		fLastLen;			// +0x1c  how much of it the stretch covers
};

// The base every object in the text engine derives from: a vtable and
// nothing else.  (The ROM's constructor and destructor are both empty.)
class TXVirtualObject
{
public:
					TXVirtualObject();								// ROM 0x002343d0 __ct__15TXVirtualObjectFv
	virtual			~TXVirtualObject();								// ROM 0x00234404 __dt__15TXVirtualObjectFv
};


// A growable array of `elementSize`-byte elements in a relocatable
// handle.  `chunk` is how much room is kept spare: the array grows by
// whole chunks and gives memory back when more than a chunk is unused.
// The ROM's object is 0x18 bytes.
class TXArray : public TXVirtualObject
{
public:
					TXArray(unsigned char elementSize, int chunk);	// ROM 0x002306c8 __ct__7TXArrayFUci
	virtual			~TXArray();										// ROM 0x00230740 __dt__7TXArrayFv

	long			GetCount(void) const		{ return fCount; }
	unsigned char	GetElementSize(void) const	{ return fElementSize; }

	void*			GetElementPtr(long index) const;				// ROM 0x00230f74 GetElementPtr__7TXArrayCFl
	void*			GetLastElementPtr(void) const;					// ROM 0x00230f8c GetLastElementPtr__7TXArrayCFv
	void			Stuff(long index, const void* data, long count);	// ROM 0x00230f98 Stuff__7TXArrayFlPCvT1 - count elements written at the index
	void			CopyTo(long index, long count, void* out) const;	// ROM 0x00230fcc CopyTo__7TXArrayCFlT1Pv

	// `count` elements opened at `at` (-1: the end) and `data` written
	// into them (nil: left as they were); ==> the first of them, or nil
	// when there was no memory.
	void*			Insert(const void* data, long count, long at);	// ROM 0x00230ffc Insert__7TXArrayFPCvlT2
	virtual long	Remove(long at, long count);					// ROM 0x0023078c Remove__7TXArrayFlT1 - ==> the count left
	NewtonErr		Replace(long at, long count, const void* data, long newCount);	// ROM 0x002307f0 Replace__7TXArrayFlT1PCvT1

	NewtonErr		SetPhysicalCount(long count);					// ROM 0x00230874 SetPhysicalCount__7TXArrayFl - the handle resized
	NewtonErr		Reserve(long count);							// ROM 0x002308b8 Reserve__7TXArrayFl - room for that many more, the count unchanged
	NewtonErr		SetCount(long count);							// ROM 0x002308ec SetCount__7TXArrayFl
	NewtonErr		Compact(void);									// ROM 0x0023093c Compact__7TXArrayFv - the spare room given back
	void			CheckUnusedCount(void);							// ROM 0x002310c8 CheckUnusedCount__7TXArrayFv - more than a chunk unused is given back

	void*			Lock(Boolean moveHigh);							// ROM 0x00230ae0 Lock__7TXArrayFUc - ==> the elements (nested; the first lock moves the handle high)
	void			Unlock(void);									// ROM 0x00230d60 Unlock__7TXArrayFv

	long			fCount;			// +0x04  the elements in use
	unsigned char	fElementSize;	// +0x08
	char			fLockCount;		// +0x09  Lock calls outstanding
	Handle			fData;			// +0x0c
	long			fChunk;			// +0x10  the room kept spare, and the growth step (at least 1)
	long			fPhysicalCount;	// +0x14  the elements the handle has room for
};


// A TXArray whose elements begin with a long, kept in increasing order,
// so that one can be found by binary search.
class TXLongTagArray : public TXArray
{
public:
					TXLongTagArray(unsigned char elementSize, int chunk);	// ROM 0x00230944 __ct__14TXLongTagArrayFUci
	virtual			~TXLongTagArray();								// ROM 0x00230994 __dt__14TXLongTagArrayFv

	// The index of the element whose long is `tag`, or of the first one
	// past it; `found` comes back as the long that was landed on.
	long			Search(long tag, long* found) const;			// ROM 0x002309d4 Search__14TXLongTagArrayCFlPl
	// The index of the first element whose long is greater than `tag`.
	long			SearchBigger(long tag) const;					// ROM 0x00230a94 SearchBigger__14TXLongTagArrayCFl
	// `delta` added to the long of `count` elements from `at` (-1: to the end).
	void			AddToElements(long at, long delta, long count);	// ROM 0x00230b2c AddToElements__14TXLongTagArrayFlN21
};


// A TXLongTagArray whose longs are the ends of consecutive ranges:
// element i holds where range i ends, so range i is [element i-1,
// element i) and range 0 starts at 0.  Nothing is stored for the
// starts, which is why a range's start is its predecessor's end.
class TXRanges : public TXLongTagArray
{
public:
					TXRanges(unsigned char elementSize, int chunk);	// ROM 0x00230b8c __ct__8TXRangesFUci

	virtual NewtonErr FreeData(Boolean compact);					// ROM 0x00230bdc FreeData__8TXRangesFUc - every range dropped (the vtable's third slot)

	TXOffset		GetRangeEnd(long index) const;					// ROM 0x00230c0c GetRangeEnd__8TXRangesCFl
	TXOffset		GetRangeStart(long index) const;				// ROM 0x00230c30 GetRangeStart__8TXRangesCFl
	long			GetRangeLen(long index) const;					// ROM 0x00230c58 GetRangeLen__8TXRangesCFl
	void			GetRangeBounds(long index, TXOffsetPair* bounds) const;	// ROM 0x00230c8c GetRangeBounds__8TXRangesCFlP12TXOffsetPair
	void			SetRangeEnd(long index, TXOffset end);			// ROM 0x00230cc8 SetRangeEnd__8TXRangesFlT1
	void			AddToRangeEnd(long index, long delta);			// ROM 0x00230ce4 AddToRangeEnd__8TXRangesFlT1
	Boolean			IsRangeStart(TXOffset offset, long index) const;	// ROM 0x00230d08 IsRangeStart__8TXRangesCFlT1 (-1: whichever range the offset is in)
	TXOffset		GetLastRangeEnd(void) const;					// ROM 0x00230d84 GetLastRangeEnd__8TXRangesCFv - the end of the last range: what the whole array covers

	// The range an offset falls in.  `atStart` asks for the range the
	// offset *ends*, rather than the one it starts, when it is exactly
	// on a boundary.
	long			OffsetToRangeIndex(TXOffset offset, Boolean atStart) const;	// ROM 0x00230db0 OffsetToRangeIndex__8TXRangesCF8TXOffset
	// What a stretch of text covers: which range it starts in and how
	// far into it, which it ends in and how much is left, and which are
	// covered end to end between them.
	long			SectRanges(TXOffset start, long length, TXSectRanges* sect) const;	// ROM 0x00230e14 SectRanges__8TXRangesCFlT1P12TXSectRanges
};

#endif	/* __TXARRAY_H */
