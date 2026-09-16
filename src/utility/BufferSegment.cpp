/*
	File:		utility/BufferSegment.cpp

	Contains:	CMinBuffer, CBuffer and CBufferSegment (BufferSegment.h).

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "BufferSegment.h"
#include "NewtonMemory.h"
#include "KernelTypes.h"
#include "NewtErrors.h"


/*------------------------------------------------------------------------------
	C M i n B u f f e r
------------------------------------------------------------------------------*/

// ROM 0x00120d3c __ct__10CMinBufferFv
CMinBuffer::CMinBuffer()
{ }


// ROM 0x00120d70 __dt__10CMinBufferFv
CMinBuffer::~CMinBuffer()
{ }


/*------------------------------------------------------------------------------
	C B u f f e r
------------------------------------------------------------------------------*/

// ROM 0x0004644c __ct__7CBufferFv
CBuffer::CBuffer()
{ }


// ROM 0x00048364 __dt__7CBufferFv
CBuffer::~CBuffer()
{ }


/*------------------------------------------------------------------------------
	C B u f f e r S e g m e n t
------------------------------------------------------------------------------*/

// ROM 0x00047cc4 __ct__14CBufferSegmentFv
CBufferSegment::CBufferSegment()
{
	fBuffer = nil;
	fBufEnd = nil;
	fPhysicalSize = 0;
	fBufStart = nil;
	fBufPtr = nil;
	fBufLimit = nil;
	fFreeBuffer = false;
	fIsShared = false;
	fSharedMem = 0;
}


// ROM 0x00047d38 __dt__14CBufferSegmentFv
CBufferSegment::~CBufferSegment()
{
	if (fFreeBuffer && fBuffer != nil)
		DisposPtr((Ptr) fBuffer);
}


// ROM 0x0004803c Init__14CBufferSegmentFl
// Over a new block of len bytes (any block of ours disposed), all of it
// valid; a shared segment's shared memory re-pointed at it.
NewtonErr
CBufferSegment::Init(Size len)
{
	if (fBuffer != nil && fFreeBuffer)
		DisposPtr((Ptr) fBuffer);
	fBuffer = (UByte*) NewPtr(len);
	if (fBuffer == nil)
		return MemError();
	fPhysicalSize = len;
	fBufPtr = fBuffer;
	fBufEnd = fBuffer + len;
	fBufStart = fBuffer;
	fBufLimit = fBuffer + len;
	fFreeBuffer = true;
	if (fIsShared)
		return fSharedMem.SetBuffer(fBuffer, fPhysicalSize, kSMemReadWrite | kSMemNoSizeChangeOnCopyTo);
	return noErr;
}


// ROM 0x000481b8 Init__14CBufferSegmentFPvlUcN22
// Over a given block (ours to dispose when freeBuffer), the valid data
// validCount bytes from validOff (-1: to the end).
NewtonErr
CBufferSegment::Init(void* data, Size len, Boolean freeBuffer, Size validOff, Long validCount)
{
	if (fFreeBuffer && fBuffer != nil)
		DisposPtr((Ptr) fBuffer);
	fPhysicalSize = len;
	fBuffer = (UByte*) data;
	fBufEnd = fBuffer + len;
	fBufPtr = fBuffer + validOff;
	fBufStart = fBufPtr;
	UByte* limit = fBufEnd;
	if (validCount >= 0 && fBufPtr + validCount <= fBufEnd)
		limit = fBufPtr + validCount;
	fBufLimit = limit;
	fFreeBuffer = freeBuffer;
	if (fIsShared)
		return fSharedMem.SetBuffer(fBuffer, fPhysicalSize, kSMemReadWrite | kSMemNoSizeChangeOnCopyTo);
	return noErr;
}


// ROM 0x00048238 Peek__14CBufferSegmentFv
int
CBufferSegment::Peek(void)
{
	if (fBufPtr < fBufLimit)
		return *fBufPtr;
	return -1;
}


// ROM 0x00048250 Next__14CBufferSegmentFv
// The byte after the position, the position moved to it.
int
CBufferSegment::Next(void)
{
	if (fBufPtr < fBufLimit)
	{
		fBufPtr++;
		return *fBufPtr;
	}
	return -1;
}


// ROM 0x00048270 Skip__14CBufferSegmentFv
// The position moved on; -1 at the end.  (The ROM answers the old
// position as an int otherwise, which no caller uses: 0 here.)
int
CBufferSegment::Skip(void)
{
	if (fBufPtr < fBufLimit)
	{
		fBufPtr++;
		return 0;
	}
	return -1;
}


// ROM 0x00048290 Get__14CBufferSegmentFv
int
CBufferSegment::Get(void)
{
	if (fBufPtr < fBufLimit)
		return *fBufPtr++;
	return -1;
}


// ROM 0x000482b4 Getn__14CBufferSegmentFPUcl
// Up to n bytes out; ==> how many.
Size
CBufferSegment::Getn(UByte* p, Size n)
{
	Size count = n;
	if (n > 0)
	{
		count = fBufLimit - fBufPtr;
		if (n < count)
			count = n;
		if (count > 0)
		{
			BlockMove(fBufPtr, p, count);
			fBufPtr += count;
		}
	}
	return count;
}


// ROM 0x0004830c CopyOut__14CBufferSegmentFPUcRl
// Up to n bytes out, n left with how many were not; ==> -1 when the
// data is then exhausted.
int
CBufferSegment::CopyOut(UByte* p, Size& n)
{
	int result = 0;
	if (n > 0)
	{
		n -= Getn(p, n);
		if (fBufPtr == fBufLimit)
			result = -1;
	}
	return result;
}


// ROM 0x00047d98 Put__14CBufferSegmentFi
int
CBufferSegment::Put(int dataByte)
{
	if (fBufPtr < fBufLimit)
	{
		*fBufPtr++ = (UByte) dataByte;
		return dataByte & 0xff;
	}
	return -1;
}


// ROM 0x00047dc0 Putn__14CBufferSegmentFPCUcl
// Up to n bytes in; ==> how many.
Size
CBufferSegment::Putn(const UByte* p, Size n)
{
	Size count = n;
	if (n > 0)
	{
		count = fBufLimit - fBufPtr;
		if (n < count)
			count = n;
		if (count > 0)
		{
			BlockMove(p, fBufPtr, count);
			fBufPtr += count;
		}
	}
	return count;
}


// ROM 0x00047e20 CopyIn__14CBufferSegmentFPCUcRl
// Up to n bytes in, n left with how many were not; ==> -1 when the
// buffer is then full.
int
CBufferSegment::CopyIn(const UByte* p, Size& n)
{
	if (n > 0)
	{
		n -= Putn(p, n);
		if (fBufPtr == fBufLimit)
			return -1;
	}
	return 0;
}


// ROM 0x00047e7c Reset__14CBufferSegmentFv
// The whole block valid again, the position at its start.
void
CBufferSegment::Reset(void)
{
	fBufPtr = fBuffer;
	fBufStart = fBuffer;
	fBufLimit = fBufEnd;
}


// ROM 0x00047fd4 GetSize__14CBufferSegmentCFv
Size
CBufferSegment::GetSize(void) const
{
	return fBufLimit - fBufStart;
}


// ROM 0x00047fe4 AtEOF__14CBufferSegmentCFv
Boolean
CBufferSegment::AtEOF(void) const
{
	return fBufPtr == fBufLimit;
}


// ROM 0x00047e94 Hide__14CBufferSegmentFli
// count bytes hidden at the beginning (dir -1) or the end (dir 1) of
// the valid data (a negative count reveals them), kept within the
// block; the position goes to that end.  ==> the bytes hidden.
Long
CBufferSegment::Hide(Long count, int dir)
{
	if (count == 0)
		return 0;
	if (dir == kSeekFromBeginning)
	{
		UByte* start = fBufStart + count;
		fBufStart = start;
		if (start < fBuffer)
		{
			count += fBuffer - start;
			fBufStart = fBuffer;
		}
		else if (start > fBufEnd)
		{
			count -= start - fBufEnd;
			fBufStart = fBufEnd;
		}
		fBufPtr = fBufStart;
		return count;
	}
	if (dir == kSeekFromEnd)
	{
		UByte* limit = fBufLimit - count;
		fBufLimit = limit;
		if (limit < fBuffer)
		{
			count -= fBuffer - limit;
			fBufLimit = fBuffer;
		}
		else if (limit > fBufEnd)
		{
			count += limit - fBufEnd;
			fBufLimit = fBufEnd;
		}
		fBufPtr = fBufLimit;
		return count;
	}
	return 0;
}


// ROM 0x00047f40 Seek__14CBufferSegmentFli
// The position moved off bytes from the beginning (dir -1), from here
// (0) or back from the end (1), kept within the valid data.  ==> the
// position.
Size
CBufferSegment::Seek(Long off, int dir)
{
	UByte* start = fBufStart;
	if (off == 0)
	{
		if (dir == kSeekFromBeginning)
			fBufPtr = start;
		else if (dir == kSeekFromEnd)
			fBufPtr = fBufLimit;
	}
	else
	{
		UByte* to;
		if (dir == kSeekFromBeginning)
			to = start + off;
		else if (dir == kSeekFromHere)
			to = fBufPtr + off;
		else if (dir == kSeekFromEnd)
			to = fBufLimit - off;
		else
			to = fBufPtr;
		if (to < start)
			to = start;
		if (to >= fBufLimit)
			to = fBufLimit;
		fBufPtr = to;
	}
	return fBufPtr - start;
}


// ROM 0x00047fbc Position__14CBufferSegmentCFv
Size
CBufferSegment::Position(void) const
{
	if (fBufPtr == nil)
		return 0;
	return fBufPtr - fBufStart;
}


// ROM 0x00048140 GetPhysicalSize__14CBufferSegmentFv
Size
CBufferSegment::GetPhysicalSize(void)
{
	return fPhysicalSize;
}


// ROM 0x00048148 SetPhysicalSize__14CBufferSegmentFl
// The block resized (it may move: the position kept relative), all of
// it valid.
NewtonErr
CBufferSegment::SetPhysicalSize(Size len)
{
	Size position = fBufPtr - fBuffer;
	UByte* block = (UByte*) ReallocPtr((Ptr) fBuffer, len);
	NewtonErr err = MemError();
	if (block == nil || err != noErr)
		return err;
	fPhysicalSize = len;
	fBuffer = block;
	fBufEnd = block + len;
	fBufStart = block;
	fBufLimit = block + len;
	fBufPtr = block + position;
	if (fIsShared)
		return fSharedMem.SetBuffer(fBuffer, fPhysicalSize, kSMemReadWrite | kSMemNoSizeChangeOnCopyTo);
	return noErr;
}


// ROM 0x000480ac MakeShared__14CBufferSegmentFUl
// The block made a shared memory object other tasks can copy to and
// from, with the permissions (kSMemReadOnly or kSMemReadWrite; its size
// fixed).
NewtonErr
CBufferSegment::MakeShared(ULong permissions)
{
	if (fIsShared)
		return noErr;
	NewtonErr err = fSharedMem.Init();
	if (err == noErr)
		err = fSharedMem.SetBuffer(fBuffer, fPhysicalSize, permissions | kSMemNoSizeChangeOnCopyTo);
	if (err == noErr)
		fIsShared = true;
	return err;
}


// ROM 0x00048000 RestoreShared__14CBufferSegmentFUl
// The shared memory object re-pointed at the block.
NewtonErr
CBufferSegment::RestoreShared(ULong permissions)
{
	if (fIsShared)
		return fSharedMem.SetBuffer(fBuffer, fPhysicalSize, permissions | kSMemNoSizeChangeOnCopyTo);
	return noErr;
}


// ROM 0x00048108 UnShare__14CBufferSegmentFv
NewtonErr
CBufferSegment::UnShare(void)
{
	if (fIsShared)
	{
		fSharedMem.DestroyObject();
		fIsShared = false;
	}
	return noErr;
}
