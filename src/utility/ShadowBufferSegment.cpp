/*
	File:		utility/ShadowBufferSegment.cpp

	Contains:	CShadowBufferSegment (ShadowBufferSegment.h).

	Reconstructed from the MP2x00 US ROM (0x001de9f4-0x001deeb0); each
	function cites its origin.
*/

#include "ShadowBufferSegment.h"
#include "NewtErrors.h"


// ROM 0x001de9f4 __ct__20CShadowBufferSegmentFv
CShadowBufferSegment::CShadowBufferSegment()
{
	fSharedMem.CopyObject(0);
	fSize = 0;
	fStart = 0;
	fPosition = 0;
	fLimit = 0;
}


// ROM 0x001dea5c __dt__20CShadowBufferSegmentFv
CShadowBufferSegment::~CShadowBufferSegment()
{
}


// ROM 0x001ded0c Init__20CShadowBufferSegmentFUllT2
void
CShadowBufferSegment::Init(TObjectId sharedId, Long validOff, Long validCount)
{
	fSharedMem.CopyObject(sharedId);
	fSharedMem.GetSize(&fSize, nil);
	fPosition = validOff;
	fStart = validOff;
	fLimit = (validCount < 0) ? (Long) fSize : validOff + validCount;
}


// ROM 0x001ded5c GetByteAt__20CShadowBufferSegmentFl
int
CShadowBufferSegment::GetByteAt(Long offset)
{
	ULong size = 0;
	UByte b;
	fSharedMem.CopyFromShared(&size, &b, 1, offset, nil);
	return b;
}


// ROM 0x001ded9c PutByteAt__20CShadowBufferSegmentFil
int
CShadowBufferSegment::PutByteAt(int dataByte, Long offset)
{
	UByte b = dataByte;
	if (fSharedMem.CopyToShared(&b, 1, offset, nil) != noErr)
		dataByte = -1;
	return dataByte;
}


// ROM 0x001dede0 Peek__20CShadowBufferSegmentFv
int
CShadowBufferSegment::Peek()
{
	if (fPosition >= fLimit)
		return -1;
	ULong size = 0;
	UByte b;
	fSharedMem.CopyFromShared(&size, &b, 1, fPosition, nil);
	return b;
}


// ROM 0x001dedf8 Next__20CShadowBufferSegmentFv
// Move on a byte and answer the one there - read even when that is the
// limit, one past the valid data (the ROM does not check).
int
CShadowBufferSegment::Next()
{
	Long position = fPosition;
	if (position >= fLimit)
		return -1;
	fPosition = position + 1;
	ULong size = 0;
	UByte b;
	fSharedMem.CopyFromShared(&size, &b, 1, position + 1, nil);
	return b;
}


// ROM 0x001dee18 Skip__20CShadowBufferSegmentFv
// (What it answers is the position it skipped from.)
int
CShadowBufferSegment::Skip()
{
	Long position = fPosition;
	if (position < fLimit)
		fPosition = position + 1;
	else
		position = -1;
	return position;
}


// ROM 0x001dee38 Get__20CShadowBufferSegmentFv
int
CShadowBufferSegment::Get()
{
	Long position = fPosition;
	if (position >= fLimit)
		return -1;
	fPosition = position + 1;
	ULong size = 0;
	UByte b;
	fSharedMem.CopyFromShared(&size, &b, 1, position, nil);
	return b;
}


// ROM 0x001dee58 Getn__20CShadowBufferSegmentFPUcl
Size
CShadowBufferSegment::Getn(UByte* p, Size n)
{
	ULong count = fLimit - fPosition;
	if (n < (Long) count)
		count = n;
	fSharedMem.CopyFromShared(&count, p, count, fPosition, nil);
	fPosition += count;
	return count;
}


// ROM 0x001deaa8 CopyOut__20CShadowBufferSegmentFPUcRl
int
CShadowBufferSegment::CopyOut(UByte* p, Size& n)
{
	n -= Getn(p, n);
	return (fPosition == fLimit) ? -1 : 0;
}


// ROM 0x001deaf0 Put__20CShadowBufferSegmentFi
int
CShadowBufferSegment::Put(int dataByte)
{
	Long position = fPosition;
	if (position >= fLimit)
		return -1;
	fPosition = position + 1;
	UByte b = dataByte;
	if (fSharedMem.CopyToShared(&b, 1, position, nil) != noErr)
		dataByte = -1;
	return dataByte;
}


// ROM 0x001deb10 Putn__20CShadowBufferSegmentFPCUcl
Size
CShadowBufferSegment::Putn(const UByte* p, Size n)
{
	ULong count = fLimit - fPosition;
	if (n < (Long) count)
		count = n;
	fSharedMem.CopyToShared((void*) p, count, fPosition, nil);
	fPosition += count;
	return count;
}


// ROM 0x001deb64 CopyIn__20CShadowBufferSegmentFPCUcRl
int
CShadowBufferSegment::CopyIn(const UByte* p, Size& n)
{
	n -= Putn(p, n);
	return (fPosition == fLimit) ? -1 : 0;
}


// ROM 0x001debac Reset__20CShadowBufferSegmentFv
// The whole object valid again, at its start.
void
CShadowBufferSegment::Reset()
{
	fPosition = 0;
	fStart = 0;
	fLimit = fSize;
}


// ROM 0x001dece0 GetSize__20CShadowBufferSegmentCFv
Size
CShadowBufferSegment::GetSize() const
{
	return fLimit - fStart;
}


// ROM 0x001decf0 AtEOF__20CShadowBufferSegmentCFv
Boolean
CShadowBufferSegment::AtEOF() const
{
	return fPosition == fLimit;
}


// ROM 0x001debc4 Hide__20CShadowBufferSegmentFli
// Narrow the valid data by count from the front (kSeekFromBeginning) or the
// back (kSeekFromEnd) - widen it for a negative count - within the object;
// the position goes to the new edge.  What it answers is how much moved.
Long
CShadowBufferSegment::Hide(Long count, int dir)
{
	if (count == 0)
		return 0;
	if (dir == kSeekFromBeginning)
	{
		Long start = fStart + count;
		fStart = start;
		if (start < 0)
		{
			count -= start;
			fStart = 0;
		}
		else if ((Long) fSize < start)
		{
			count -= start - (Long) fSize;
			fStart = fSize;
		}
		fPosition = fStart;
	}
	else if (dir == kSeekFromEnd)
	{
		Long limit = fLimit - count;
		fLimit = limit;
		if (limit < 0)
		{
			count += limit;
			fLimit = 0;
		}
		else if ((Long) fSize < limit)
		{
			count += limit - (Long) fSize;
			fLimit = fSize;
		}
		fPosition = fLimit;
	}
	else
		return 0;
	return count;
}


// ROM 0x001dec58 Seek__20CShadowBufferSegmentFli
// What it answers is the position from the start of the valid data.
Size
CShadowBufferSegment::Seek(Long off, int dir)
{
	Long start = fStart;
	Long where;
	if (off == 0)
	{
		if (dir == kSeekFromBeginning)
		{
			fPosition = start;
			return fPosition - start;
		}
		if (dir != kSeekFromEnd)
			return fPosition - start;
		where = fLimit;
	}
	else
	{
		if (dir == kSeekFromBeginning)
			where = start + off;
		else if (dir == kSeekFromHere)
			where = fPosition + off;
		else if (dir == kSeekFromEnd)
			where = fLimit - off;
		else
			where = fPosition;
		if (where < start)
			where = start;
		if (where >= fLimit)
			where = fLimit;
	}
	fPosition = where;
	return fPosition - start;
}


// ROM 0x001decd4 Position__20CShadowBufferSegmentCFv
Size
CShadowBufferSegment::Position() const
{
	return fPosition - fStart;
}
