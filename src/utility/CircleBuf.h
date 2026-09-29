/*
	File:		utility/CircleBuf.h

	Contains:	TCircleBuf, the circular byte buffer the serial tools keep
				their input and output in (and a DMA channel runs over): a
				block of fBufferSize bytes read from fStart and written at
				fEnd, one byte kept free to tell full from empty, with an
				optional ring of end-of-message markers - each the index in
				the buffer where a message ends and a word that goes with
				it - so a framed tool can keep whole frames in it.  A put
				can be tentative: PutNextStart/PutNextPossible write ahead
				at fPutNext and PutNextCommit (or PutNextEOM) makes them
				count, so a frame that goes wrong half way is simply not
				committed.

				The ROM's own class; its declaration is not in the DDK, so
				the names are ours, the layout (0x28 bytes) the ROM's.
				Results: kCircleBufOK and the kCircleBuf... codes below (the
				ROM's numbers).

	Reconstructed from the MP2x00 US ROM (0x00057768-0x0005851c); each
	function cites its origin.
*/

#ifndef __UTILITY_CIRCLEBUF_H
#define __UTILITY_CIRCLEBUF_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

#ifndef __NEWTERRORS_H
#include "NewtErrors.h"
#endif

class CBufferList;

// what the methods answer
enum
{
	kCircleBufOK = 0,
	kCircleBufEOM = 1,				// the bytes read run to a marker
	kCircleBufEmpty = 2,			// nothing to read
	kCircleBufFull = 3,				// no room for all of it
	kCircleBufNoMarkerSpace = 4,	// the marker ring is full
	kCircleBufNothingCopied = 5,
	kCircleBufCountExhausted = 6,	// the count asked for is used up
	kCircleBufNoMarker = 7
};

// the buffer's memory (Allocate's type)
enum
{
	kCircleBufPlain = 0,
	kCircleBufLocked = 1,
	kCircleBufWired = 2
};

// Allocate's flags
#define kCircleBufAlignLong		0x01	// a message starts on a long boundary
#define kCircleBufLockObject	0x02	// the buffer object itself locked (LockHeapRange)
#define kCircleBufWireObject	0x04	//  and wired

class TCircleBuf
{
public:
					TCircleBuf();
					~TCircleBuf();

	NewtonErr		Allocate(ULong size);
	NewtonErr		Allocate(ULong size, int markers, UChar type, UChar flags);
	void			Deallocate();
	void			Reset();
	void			ResetStart();

	// bytes and markers
	ULong			BufferCount();
	ULong			BufferSpace();
	ULong			BufferSpace(ULong count);				// ==> kCircleBufOK if count fit
	ULong			MarkerCount();
	ULong			MarkerSpace();
	Boolean			BufferCountToNextMarker(ULong* count);	// ==> true if a marker ends it
	ULong			PeekNextEOMIndex();
	ULong			PeekNextEOMIndex(ULong* value);
	ULong			GetEOMMark(ULong* value);
	ULong			PutEOMMark(ULong index, ULong value);
	ULong			PutEOM(ULong value);
	void			FlushBytes();
	ULong			FlushToNextMarker(ULong* value);

	// byte at a time
	ULong			GetNextByte(UByte* byte);
	ULong			GetNextByte(UByte* byte, ULong* value);
	ULong			PeekNextByte(UByte* byte);
	ULong			PeekNextByte(UByte* byte, ULong* value);
	ULong			PutNextByte(UByte byte);
	ULong			PutNextByte(UByte byte, ULong value);
	ULong			PutNextStart();
	ULong			PutFirstPossible(UByte byte);
	ULong			PutNextPossible(UByte byte);
	ULong			PutNextCommit();
	ULong			PutNextEOM(ULong value);
	ULong			PeekFirstLong(ULong* value);

	// blocks
	ULong			CopyIn(CBufferList* data, ULong* count);
	ULong			CopyIn(UByte* data, ULong* count, Boolean eom, ULong value);
	ULong			CopyOut(CBufferList* data, ULong* count, ULong* value);
	ULong			CopyOut(UByte* data, ULong* count, ULong* value);
	ULong			GetBytes(TCircleBuf* source);

	// for a DMA channel
	UByte*			DMABufInfo(ULong* size, ULong* physical, UChar* type, UChar* flags);
	ULong			DMAGetInfo(ULong* start);
	ULong			DMAGetUpdate(ULong start);
	ULong			DMAPutInfo(ULong* end, ULong* putNext);
	ULong			DMAPutUpdate(ULong putNext, Boolean eom, ULong value);
	void			UpdateStart(ULong count);
	void			UpdateEnd(ULong count);
	void			GetAlignLong();
	void			PutAlignLong();

	ULong			fBufferSize;		// +0x00
	UByte*			fBuffer;			// +0x04
	ULong			fStart;				// +0x08  the next byte to read
	ULong			fEnd;				// +0x0c  where the next byte goes
	ULong			fPutNext;			// +0x10  a tentative put's end
	UChar			fBufferType;		// +0x14  kCircleBufPlain, ...
	UChar			fFlags;				// +0x15
	ULong			fMarkerCount;		// +0x18  the marker ring's size (0: none)
	ULong*			fMarkers;			// +0x1c  pairs (index, value), after the bytes
	ULong			fMarkerStart;		// +0x20
	ULong			fMarkerEnd;			// +0x24
};

#endif
