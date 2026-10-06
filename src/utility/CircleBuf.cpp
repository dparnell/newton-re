/*
	File:		utility/CircleBuf.cpp

	Contains:	TCircleBuf (CircleBuf.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "CircleBuf.h"
#include "BufferList.h"
#include "NewtonMemory.h"
#include "host/RomBugs.h"

#include <string.h>


// ROM 0x00057768 __ct__10TCircleBufFv
TCircleBuf::TCircleBuf()
{
	fBuffer = nil;
	fMarkers = nil;
}


// ROM 0x0005779c __dt__10TCircleBufFv
TCircleBuf::~TCircleBuf()
{
	Deallocate();
}


// ROM 0x000577c8 BufferCountToNextMarker__10TCircleBufFPUl
// The bytes there are to read, or to the next marker if that is nearer.
Boolean
TCircleBuf::BufferCountToNextMarker(ULong* count)
{
	Boolean atMarker = false;
	ULong start = fStart;
	ULong end = fEnd;
	ULong size = fBufferSize;
	ULong marker = PeekNextEOMIndex();
	ULong n = end - start;
	if (end < start)
		n += size;
	if (marker != 0xffffffff)
	{
		ULong m = marker - start;
		if (marker < start)
			m += size;
		if (m <= n)
		{
			atMarker = true;
			n = m;
		}
	}
	*count = n;
	return atMarker;
}


// ROM 0x00057828 FlushBytes__10TCircleBufFv
void
TCircleBuf::FlushBytes()
{
	ULong value;
	while (GetEOMMark(&value) != 0xffffffff)
		;
	fStart = fEnd;
}


// ROM 0x0005786c FlushToNextMarker__10TCircleBufFPUl
ULong
TCircleBuf::FlushToNextMarker(ULong* value)
{
	ULong index = GetEOMMark(value);
	if (index == 0xffffffff)
		return kCircleBufNoMarker;
	fStart = index;
	return kCircleBufOK;
}


// ROM 0x00057894 Reset__10TCircleBufFv
void
TCircleBuf::Reset()
{
	fMarkerStart = 0;
	fMarkerEnd = 0;
	fStart = 0;
	fEnd = 0;
}


// ROM 0x000578ac ResetStart__10TCircleBufFv
void
TCircleBuf::ResetStart()
{
	fStart = 0;
}


// ROM 0x000578b8 CopyOut__10TCircleBufFP11CBufferListPUlT2
// As much as there is (to the next marker) into the buffer list; *count
// less what went.  ==> kCircleBufCountExhausted when *count came to
// nought, kCircleBufEOM when a marker was reached (its value in *value).
ULong
TCircleBuf::CopyOut(CBufferList* data, ULong* count, ULong* value)
{
	ULong result = kCircleBufOK;
	ULong n;
	Boolean atMarker = BufferCountToNextMarker(&n);
	if (n != 0)
	{
		ULong toEnd = fBufferSize - fStart;
		UByte* p = fBuffer + fStart;
		ULong got;
		if (toEnd < n)
		{
			got = data->Putn(p, toEnd);
			if (got == toEnd)
				got += data->Putn(fBuffer, n - toEnd);
		}
		else
			got = data->Putn(p, n);
		ULong left = *count;
		*count = left - got;
		if (left - got == 0)
			result = kCircleBufCountExhausted;
		if (!atMarker || got != n)
			UpdateStart(got);
		else
		{
			result = kCircleBufEOM;
			GetEOMMark(value);
			UpdateStart(got);
			if (fFlags & kCircleBufAlignLong)
				GetAlignLong();
		}
	}
	return result;
}


// ROM 0x000579b4 CopyOut__10TCircleBufFPUcPUlT2
// The same into memory: at most *count bytes.
ULong
TCircleBuf::CopyOut(UByte* data, ULong* count, ULong* value)
{
	ULong result = kCircleBufOK;
	ULong want = *count;
	ULong n;
	Boolean atMarker = BufferCountToNextMarker(&n);
	if (n != 0)
	{
		if (want < n)
		{
			atMarker = false;
			result = kCircleBufCountExhausted;
			n = want;
		}
		*count = *count - n;
		ULong toEnd = fBufferSize - fStart;
		UByte* p = fBuffer + fStart;
		ULong m = n;
		if (toEnd < n)
		{
			memcpy(data, p, toEnd);
			data += toEnd;
			p = fBuffer;
			m = n - toEnd;
		}
		memcpy(data, p, m);
		if (!atMarker)
			UpdateStart(n);
		else
		{
			result = kCircleBufEOM;
			GetEOMMark(value);
			UpdateStart(n);
			if (fFlags & kCircleBufAlignLong)
				GetAlignLong();
		}
	}
	return result;
}


// ROM 0x00057aa4 CopyIn__10TCircleBufFP11CBufferListPUl
// As much of the buffer list as there is room for; *count less what came.
ULong
TCircleBuf::CopyIn(CBufferList* data, ULong* count)
{
	ULong result = kCircleBufOK;
	ULong n = BufferSpace();
	if (n != 0)
	{
		ULong toEnd = fBufferSize - fEnd;
		if (fStart == 0)
			toEnd--;
		UByte* p = fBuffer + fEnd;
		ULong got;
		if (toEnd < n)
		{
			got = data->Getn(p, toEnd);
			if (got == toEnd)
				got += data->Getn(fBuffer, n - toEnd);
		}
		else
			got = data->Getn(p, n);
		UpdateEnd(got);
		*count = *count - got;
		if (got == 0)
			result = kCircleBufNothingCopied;
	}
	return result;
}


// ROM 0x00057b5c CopyIn__10TCircleBufFPUcPUlUcUl
// *count bytes, or as many as there is room for (kCircleBufFull); with eom,
// a marker after them when they all went in.
ULong
TCircleBuf::CopyIn(UByte* data, ULong* count, Boolean eom, ULong value)
{
	ULong result = kCircleBufOK;
	ULong n = *count;
	ULong space = BufferSpace();
	if (space < n)
	{
		result = kCircleBufFull;
		n = space;
	}
	if (eom && MarkerSpace() == 0)
		return kCircleBufNoMarkerSpace;
	*count = *count - n;
	if (space != 0)
	{
		ULong size = fBufferSize;
		ULong end = fEnd;
		ULong toEnd = size - end;
		if (fStart == 0)
			toEnd--;
		UByte* p = fBuffer + end;
		ULong m = n;
		if (toEnd < n)
		{
			memcpy(p, data, toEnd);
			data += toEnd;
			p = fBuffer;
			m = n - toEnd;
		}
		memcpy(p, data, m);
		ULong newEnd = end + n;
		if (size <= newEnd)
			newEnd -= size;
		if (eom && *count == 0)
			result = PutEOMMark(newEnd, value);
		fEnd = newEnd;
	}
	return result;
}


// ROM 0x00057c7c GetNextByte__10TCircleBufFPUc
ULong
TCircleBuf::GetNextByte(UByte* byte)
{
	if (fMarkers != nil)
	{
		ULong value;
		return GetNextByte(byte, &value);
	}
	ULong result = kCircleBufEmpty;
	ULong start = fStart;
	if (fEnd != start)
	{
		*byte = fBuffer[start];
		start++;
		if (start == fBufferSize)
			start = 0;
		result = kCircleBufOK;
		fStart = start;
	}
	return result;
}


// ROM 0x00057ce4 Allocate__10TCircleBufFUl
NewtonErr
TCircleBuf::Allocate(ULong size)
{
	return Allocate(size, 0, kCircleBufPlain, 0);
}


// ROM 0x00057d04 GetNextByte__10TCircleBufFPUcPUl
// ==> kCircleBufEOM when the byte is the last before a marker (its value
// in *value).
ULong
TCircleBuf::GetNextByte(UByte* byte, ULong* value)
{
	ULong result = kCircleBufEmpty;
	ULong start = fStart;
	if (fEnd != start)
	{
		*byte = fBuffer[start];
		start++;
		if (start == fBufferSize)
			start = 0;
		result = (PeekNextEOMIndex() == start) ? kCircleBufEOM : kCircleBufOK;
		if (result == kCircleBufEOM)
			GetEOMMark(value);
		fStart = start;
	}
	return result;
}


// ROM 0x00057d7c PeekNextByte__10TCircleBufFPUc
ULong
TCircleBuf::PeekNextByte(UByte* byte)
{
	if (fEnd == fStart)
		return kCircleBufEmpty;
	*byte = fBuffer[fStart];
	return kCircleBufOK;
}


// ROM 0x00057da8 PeekNextByte__10TCircleBufFPUcPUl
ULong
TCircleBuf::PeekNextByte(UByte* byte, ULong* value)
{
	ULong start = fStart;
	if (fEnd == start)
		return kCircleBufEmpty;
	*byte = fBuffer[start];
	start++;
	if (start == fBufferSize)
		start = 0;
	return (PeekNextEOMIndex(value) == start) ? kCircleBufEOM : kCircleBufOK;
}


// ROM 0x00057e00 PutNextByte__10TCircleBufFUc
// (The byte is written before the room is known: a full buffer's free
// byte takes it, and it does not count.)
ULong
TCircleBuf::PutNextByte(UByte byte)
{
	ULong result = kCircleBufFull;
	ULong end = fEnd;
	fBuffer[end] = byte;
	end++;
	if (end == fBufferSize)
		end = 0;
	if (fStart != end)
	{
		result = kCircleBufOK;
		fEnd = end;
	}
	return result;
}


// ROM 0x00057e3c PutNextByte__10TCircleBufFUcUl
// A byte that ends a message: a marker after it.
ULong
TCircleBuf::PutNextByte(UByte byte, ULong value)
{
	if (MarkerSpace() == 0)
		return kCircleBufNoMarkerSpace;
	ULong end = fEnd;
	fBuffer[end] = byte;
	end++;
	if (end == fBufferSize)
		end = 0;
	if (fStart == end)
		return kCircleBufFull;
	ULong result = PutEOMMark(end, value);
	fEnd = end;
	return result;
}


// ROM 0x00057ea8 PutEOM__10TCircleBufFUl
ULong
TCircleBuf::PutEOM(ULong value)
{
	return PutEOMMark(fEnd, value);
}


// ROM 0x00057eb4 PutNextStart__10TCircleBufFv
ULong
TCircleBuf::PutNextStart()
{
	fPutNext = fEnd;
	return kCircleBufOK;
}


// ROM 0x00057ec4 PutFirstPossible__10TCircleBufFUc
ULong
TCircleBuf::PutFirstPossible(UByte byte)
{
	fPutNext = fEnd;
	return PutNextPossible(byte);
}


// ROM 0x00057ed4 PutNextPossible__10TCircleBufFUc
ULong
TCircleBuf::PutNextPossible(UByte byte)
{
	ULong result = kCircleBufFull;
	ULong next = fPutNext;
	fBuffer[next] = byte;
	next++;
	if (next == fBufferSize)
		next = 0;
	if (fStart != next)
	{
		result = kCircleBufOK;
		fPutNext = next;
	}
	return result;
}


// ROM 0x00057f10 PutNextEOM__10TCircleBufFUl
// The tentative bytes committed as a message.
ULong
TCircleBuf::PutNextEOM(ULong value)
{
	ULong result = PutEOMMark(fPutNext, value);
	if (result == kCircleBufOK)
	{
		fEnd = fPutNext;
		if (fFlags & kCircleBufAlignLong)
			PutAlignLong();
	}
	return result;
}


// ROM 0x00057f54 Allocate__10TCircleBufFUliUcT3
// size bytes (rounded up to a long, plus the one kept free), and a ring of
// markers + 1 pairs after them.  A wired buffer is capped at 0xfc0 bytes.
// DEVIATION: the host has no wired or locked memory: every kind is an
// ordinary pointer block, and the object is not locked.  DEVIATION (pointer
// size): a marker is two host ULongs, and the ring starts on a ULong
// boundary after the bytes.
NewtonErr
TCircleBuf::Allocate(ULong size, int markers, UChar type, UChar flags)
{
	if (fBuffer != nil)
		Deallocate();
	NewtonErr err = noErr;
	ULong bytes = (size + 4) & ~3;
	ULong ring = markers + 1;
	ULong markerBytes = (markers != 0) ? ring * 2 * sizeof(ULong) : 0;
	const ULong align = sizeof(ULong) - 1;
	ULong total = ((bytes + align) & ~align) + markerBytes;
	if (type == kCircleBufWired && total > 0xfc0)
	{
		total = 0xfc0;
		bytes = 0xfc0 - markerBytes;
	}
	fBuffer = (UByte*) NewPtr(total);
	if (fBuffer == nil)
		return -10007;			// (0xffffd8e9)
	fBufferType = type;
	fFlags = flags;
	fStart = 0;
	fBufferSize = bytes;
	fEnd = 0;
	fPutNext = 0;
	fMarkerStart = 0;
	fMarkerEnd = 0;
	fMarkers = nil;
	fMarkerCount = 0;
	if (markers != 0)
	{
		fMarkerCount = ring;
		fMarkers = (ULong*) (fBuffer + ((bytes + align) & ~align));
		for (ULong i = 0; i < fMarkerCount; i++)
		{
			fMarkers[i * 2] = 0xffffffff;
			fMarkers[i * 2 + 1] = 0;
		}
	}
	return err;
}


// ROM 0x000580a0 PutNextCommit__10TCircleBufFv
ULong
TCircleBuf::PutNextCommit()
{
	fEnd = fPutNext;
	return kCircleBufOK;
}


// ROM 0x000580b0 PeekFirstLong__10TCircleBufFPUl
// (the long at the put end, read as the machine lays it out)
ULong
TCircleBuf::PeekFirstLong(ULong* value)
{
	memcpy(value, fBuffer + fEnd, sizeof(ULong));
	return kCircleBufOK;
}


// ROM 0x000580c8 GetBytes__10TCircleBufFP10TCircleBuf
// Everything in source moved in, as far as there is room.
ULong
TCircleBuf::GetBytes(TCircleBuf* source)
{
	ULong stop = fStart;
	UByte* buffer = fBuffer;
	ULong size = fBufferSize;
	ULong from = source->fStart;
	ULong fromEnd = source->fEnd;
	UByte* fromBuffer = source->fBuffer;
	ULong fromSize = source->fBufferSize;
	ULong end = fEnd;
	ULong at;
	ULong result;
	ULong lastFrom = from;		// (the fix: where the last byte written came from)
	for (;;)
	{
		at = end;
		if (from == fromEnd)
		{
			result = kCircleBufEmpty;
			break;
		}
		buffer[at] = fromBuffer[from];
		lastFrom = from;
		from++;
		if (from == fromSize)
			from = 0;
		end = at + 1;
		if (end == size)
			end = 0;
		if (end == stop)
		{
			result = kCircleBufFull;
			break;
		}
	}
	// (full, the last byte written is the one kept free and does not
	// count.)  ROM BUG (fixed): source's start is set to its end whether or
	// not it all came - what did not fit is dropped.  The fix leaves in
	// source what did not come: from the byte written into the free place
	// on, when full.
	fEnd = at;
	if (RomBugFixed())
		source->fStart = result == kCircleBufFull ? lastFrom : from;
	else
		source->fStart = fromEnd;
	return result;
}


// ROM 0x0005813c DMABufInfo__10TCircleBufFPUlT1PUcT3
// DEVIATION: the physical address is the host address.
UByte*
TCircleBuf::DMABufInfo(ULong* size, ULong* physical, UChar* type, UChar* flags)
{
	if (physical != nil)
		*physical = (ULong) (uintptr_t) fBuffer;
	if (size != nil)
		*size = fBufferSize;
	if (flags != nil)
		*flags = fFlags;
	if (type != nil)
		*type = fBufferType;
	return fBuffer;
}


// ROM 0x00058198 DMAGetInfo__10TCircleBufFPUl
// Where a DMA read starts, and where it must stop: the end, or the next
// marker if that comes first.
ULong
TCircleBuf::DMAGetInfo(ULong* start)
{
	ULong from = fStart;
	if (start != nil)
		*start = from;
	ULong stop = fEnd;
	ULong marker;
	if (fMarkers != nil && (marker = PeekNextEOMIndex()) != 0xffffffff)
	{
		ULong n = stop - from;
		if (stop < from)
			n += fBufferSize;
		ULong m = marker - from;
		if (marker < from)
			m += fBufferSize;
		if (m <= n)
			stop = marker;
	}
	return stop;
}


// ROM 0x00058200 DMAGetUpdate__10TCircleBufFUl
ULong
TCircleBuf::DMAGetUpdate(ULong start)
{
	ULong value;
	if (fMarkers != nil && PeekNextEOMIndex() == start)
		GetEOMMark(&value);
	fStart = start;
	return kCircleBufOK;
}


// ROM 0x00058250 DMAPutInfo__10TCircleBufFPUlT1
ULong
TCircleBuf::DMAPutInfo(ULong* end, ULong* putNext)
{
	if (end != nil)
		*end = fEnd;
	if (putNext != nil)
		*putNext = fPutNext;
	return fStart;
}


// ROM 0x00058270 DMAPutUpdate__10TCircleBufFUlUcT1
ULong
TCircleBuf::DMAPutUpdate(ULong putNext, Boolean eom, ULong value)
{
	ULong result = kCircleBufOK;
	fPutNext = putNext;
	if (eom)
		result = PutNextEOM(value);
	return result;
}


// ROM 0x000582a0 UpdateStart__10TCircleBufFUl
void
TCircleBuf::UpdateStart(ULong count)
{
	ULong start = fStart + count;
	if (fBufferSize <= start)
		start -= fBufferSize;
	fStart = start;
}


// ROM 0x000582bc UpdateEnd__10TCircleBufFUl
void
TCircleBuf::UpdateEnd(ULong count)
{
	ULong end = fEnd + count;
	if (fBufferSize <= end)
		end -= fBufferSize;
	fEnd = end;
}


// ROM 0x000582d8 Deallocate__10TCircleBufFv
// (the ROM also unlocks the object when Allocate locked it; see Allocate)
void
TCircleBuf::Deallocate()
{
	if (fBuffer == nil)
		return;
	DisposPtr((Ptr) fBuffer);
	fBuffer = nil;
}


// ROM 0x0005833c GetAlignLong__10TCircleBufFv
void
TCircleBuf::GetAlignLong()
{
	ULong rem = fStart & 3;
	if (rem == 0)
		return;
	ULong start = fStart + (4 - rem);
	if (fBufferSize <= start)
		start -= fBufferSize;
	fStart = start;
}


// ROM 0x00058350 PutAlignLong__10TCircleBufFv
void
TCircleBuf::PutAlignLong()
{
	ULong rem = fEnd & 3;
	if (rem == 0)
		return;
	ULong end = fEnd + (4 - rem);
	if (fBufferSize <= end)
		end -= fBufferSize;
	fEnd = end;
}


// ROM 0x00058364 GetEOMMark__10TCircleBufFPUl
// The next marker taken off the ring: ==> its index (0xffffffff: none), its
// value in *value.
ULong
TCircleBuf::GetEOMMark(ULong* value)
{
	if (fMarkers == nil || fMarkerEnd == fMarkerStart)
		return 0xffffffff;
	ULong i = fMarkerStart;
	if (value != nil)
		*value = fMarkers[i * 2 + 1];
	ULong index = fMarkers[i * 2];
	fMarkers[i * 2] = 0xffffffff;
	i++;
	if (fMarkerCount == i)
		i = 0;
	fMarkerStart = i;
	return index;
}


// ROM 0x000583c0 PutEOMMark__10TCircleBufFUlT1
ULong
TCircleBuf::PutEOMMark(ULong index, ULong value)
{
	ULong i = fMarkerEnd;
	ULong next = i + 1;
	if (fMarkerCount == next)
		next = 0;
	if (fMarkerStart == next)
		return kCircleBufNoMarkerSpace;
	fMarkers[i * 2] = index;
	fMarkers[i * 2 + 1] = value;
	fMarkerEnd = next;
	return kCircleBufOK;
}


// ROM 0x0005840c PeekNextEOMIndex__10TCircleBufFv
// (an empty ring's slot says 0xffffffff: GetEOMMark leaves that behind)
ULong
TCircleBuf::PeekNextEOMIndex()
{
	if (fMarkers != nil)
		return fMarkers[fMarkerStart * 2];
	return 0xffffffff;
}


// ROM 0x00058428 PeekNextEOMIndex__10TCircleBufFPUl
ULong
TCircleBuf::PeekNextEOMIndex(ULong* value)
{
	if (fMarkers != nil)
	{
		*value = fMarkers[fMarkerStart * 2 + 1];
		return fMarkers[fMarkerStart * 2];
	}
	return 0xffffffff;
}


// ROM 0x00058458 BufferSpace__10TCircleBufFv
ULong
TCircleBuf::BufferSpace()
{
	ULong n = fStart - fEnd - 1;
	if (fStart <= fEnd)
		n += fBufferSize;
	return n;
}


// ROM 0x0005847c MarkerSpace__10TCircleBufFv
ULong
TCircleBuf::MarkerSpace()
{
	if (fMarkers == nil)
		return 0;
	ULong n = fMarkerStart - fMarkerEnd - 1;
	if (fMarkerStart <= fMarkerEnd)
		n += fMarkerCount;
	return n;
}


// ROM 0x000584b0 MarkerCount__10TCircleBufFv
ULong
TCircleBuf::MarkerCount()
{
	ULong n = fMarkerEnd - fMarkerStart;
	if (fMarkerEnd < fMarkerStart)
		n += fMarkerCount;
	return n;
}


// ROM 0x000584d0 BufferSpace__10TCircleBufFUl
ULong
TCircleBuf::BufferSpace(ULong count)
{
	if (fMarkers != nil && MarkerSpace() == 0)
		return kCircleBufNoMarkerSpace;
	return (BufferSpace() < count) ? kCircleBufFull : kCircleBufOK;
}


// ROM 0x0005851c BufferCount__10TCircleBufFv
ULong
TCircleBuf::BufferCount()
{
	ULong n = fEnd - fStart;
	if (fEnd < fStart)
		n += fBufferSize;
	return n;
}
