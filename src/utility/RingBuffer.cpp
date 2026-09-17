/*
	File:		utility/RingBuffer.cpp

	Contains:	CBaseRingBuffer and CRingBuffer (RingBuffer.h) - the byte
				ring buffer the comm code streams through.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
	The internal calls go through the virtuals as the ROM's do (Put asks
	IsFull, CopyIn asks ComputePutVectors, ...).
*/

#include "RingBuffer.h"
#include "NewtonMemory.h"
#include "OSErrors.h"


// ROM 0x0003b3f8 __ct__15CBaseRingBufferFv
CBaseRingBuffer::CBaseRingBuffer()
{
}

// ROM 0x0003b438 __dt__15CBaseRingBufferFv
CBaseRingBuffer::~CBaseRingBuffer()
{
}


// ROM 0x001af060 __ct__11CRingBufferFv
CRingBuffer::CRingBuffer()
{
	fBufStart = nil;
	fBufEnd = nil;
	fSize = 0;
	fPut = nil;
	fGet = nil;
	fOwnsBuffer = false;
}


// ROM 0x001af0c8 __dt__11CRingBufferFv
CRingBuffer::~CRingBuffer()
{
	if (fOwnsBuffer && fBufStart != nil)
		DisposePtr((Ptr) fBufStart);
}


// ROM 0x001af5d8 Init__11CRingBufferFl
NewtonErr
CRingBuffer::Init(long size)
{
	if (fOwnsBuffer && fBufStart != nil)
		DisposePtr((Ptr) fBufStart);
	fSize = size + 1;					// one slot is kept free to tell full from empty
	fBufStart = (UByte*) NewPtr(fSize);
	if (fBufStart == nil)
		return MemError();
	fGet = fBufStart;
	fBufEnd = fBufStart + fSize;
	fPut = fBufStart;
	fOwnsBuffer = true;
	return noErr;
}


// ROM 0x001afaa8 Init__11CRingBufferFPvlUcN22
NewtonErr
CRingBuffer::Init(void* buffer, long size, UChar ownsIt, long getOffset, long putOffset)
{
	if (fOwnsBuffer && fBufStart != nil)
		DisposePtr((Ptr) fBufStart);
	fBufStart = (UByte*) buffer;
	fOwnsBuffer = ownsIt;
	fSize = size;
	fBufEnd = (UByte*) buffer + size;
	fGet = (UByte*) buffer + getOffset;
	fPut = (UByte*) buffer + getOffset + putOffset;
	return noErr;
}


// ROM 0x001af500 Reset__11CRingBufferFv
void
CRingBuffer::Reset()
{
	fGet = fBufStart;
	fPut = fBufStart;
}


// ROM 0x001af510 GetSize__11CRingBufferCFv
long
CRingBuffer::GetSize() const
{
	return fSize - 1;
}


// ROM 0x001af51c IsFull__11CRingBufferCFv
Boolean
CRingBuffer::IsFull() const
{
	UByte* next = (fGet == fBufStart) ? fBufEnd : fGet;
	return fPut == next - 1;
}


// ROM 0x001af548 IsEmpty__11CRingBufferCFv
Boolean
CRingBuffer::IsEmpty() const
{
	return fGet == fPut;
}


// ROM 0x001af564 AtEOF__11CRingBufferCFv
Boolean
CRingBuffer::AtEOF() const
{
	return IsEmpty() || IsFull();
}


// ROM 0x001af5b4 FreeCount__11CRingBufferCFv
long
CRingBuffer::FreeCount() const
{
	long count = (fGet - fPut) - 1;
	if (fGet <= fPut)
		count += fSize;
	return count;
}


// ROM 0x001af640 DataCount__11CRingBufferCFv
long
CRingBuffer::DataCount() const
{
	long count = fPut - fGet;
	if (fPut < fGet)
		count += fSize;
	return count;
}


// ROM 0x001af660 ComputePutVectors__11CRingBufferCFRPUcRlT1T2
// The up-to-two runs a write may fill: the wrap-around run (from fBufStart)
// in the first pair, the run up to fBufEnd in the second - each nil/0 when
// it does not apply.  The slot before fGet is kept free.
void
CRingBuffer::ComputePutVectors(UByte*& p1, long& n1, UByte*& p2, long& n2) const
{
	UByte* limit = (fGet == fBufStart) ? fBufEnd : fGet;
	limit = limit - 1;					// the last writable slot before fGet
	if (fPut == fGet)
	{
		if (fPut == fBufStart || fBufStart == limit)
		{
			p1 = nil;
			n1 = 0;
		}
		else
		{
			p1 = fBufStart;
			n1 = limit - fBufStart;
		}
		if (fPut <= limit)
		{
			p2 = fPut;
			n2 = limit - fPut;
		}
		else
		{
			p2 = fPut;
			n2 = fBufEnd - fPut;
		}
	}
	else if (fPut == limit)
	{
		p1 = nil;  n1 = 0;
		p2 = nil;  n2 = 0;
	}
	else if (fPut <= limit)
	{
		p1 = nil;  n1 = 0;
		p2 = fPut;
		n2 = limit - fPut;
	}
	else
	{
		p1 = fBufStart;
		n1 = limit - fBufStart;
		p2 = fPut;
		n2 = fBufEnd - fPut;
	}
}


// ROM 0x001af72c ComputeGetVectors__11CRingBufferCFRPUcRlT1T2
// The up-to-two runs a read may take from: the run up to fBufEnd and the
// wrap-around run from fBufStart.
void
CRingBuffer::ComputeGetVectors(UByte*& p1, long& n1, UByte*& p2, long& n2) const
{
	UByte* wrap = (fGet == fBufStart) ? fBufEnd : fGet;
	if (fPut == fGet)					// empty
	{
		p1 = nil;  n1 = 0;
		p2 = nil;  n2 = 0;
		return;
	}
	if (fPut == wrap - 1)
	{
		if (fPut < fGet)
		{
			p1 = fBufStart;
			n1 = fPut - fBufStart;
			p2 = fGet;
			n2 = fBufEnd - fGet;
			return;
		}
	}
	else if (fPut <= fGet)
	{
		p2 = fGet;
		n2 = fBufEnd - fGet;
		p1 = fBufStart;
		n1 = fPut - fBufStart;
		return;
	}
	p1 = nil;  n1 = 0;
	p2 = fGet;
	n2 = fPut - fGet;
}


// ROM 0x001af8cc UpdatePutVector__11CRingBufferFl
long
CRingBuffer::UpdatePutVector(long count)
{
	if (count > 0)
	{
		UByte* p1; long n1; UByte* p2; long n2;
		ComputePutVectors(p1, n1, p2, n2);
		if (p2 != nil && n2 > 0)
		{
			if (count < n2)
				n2 = count;
			count -= n2;
			fPut += n2;
			if (fPut == fBufEnd)
				fPut = fBufStart;
		}
		if (p1 != nil && n1 > 0)
		{
			if (count < n1)
				n1 = count;
			count -= n1;
			fPut += n1;
		}
	}
	return count;
}


// ROM 0x001af800 UpdateGetVector__11CRingBufferFl
long
CRingBuffer::UpdateGetVector(long count)
{
	if (count > 0)
	{
		UByte* p1; long n1; UByte* p2; long n2;
		ComputeGetVectors(p1, n1, p2, n2);
		if (p2 != nil && n2 > 0)
		{
			if (count < n2)
				n2 = count;
			count -= n2;
			fGet += n2;
			if (fGet == fBufEnd)
				fGet = fBufStart;
		}
		if (p1 != nil && n1 > 0)
		{
			if (count < n1)
				n1 = count;
			count -= n1;
			fGet += n1;
		}
	}
	return count;
}


// ROM 0x001af128 Put__11CRingBufferFi
int
CRingBuffer::Put(int byte)
{
	if (IsFull())
		return -1;
	*fPut++ = (UByte) byte;
	if (fPut == fBufEnd)
		fPut = fBufStart;
	return byte;
}


// ROM 0x001af17c Putn__11CRingBufferFPCUcl
int
CRingBuffer::Putn(const UByte* data, long count)
{
	long remaining = count;
	CopyIn(data, remaining);
	return count - remaining;
}


// ROM 0x001af1ac CopyIn__11CRingBufferFPCUcRl
NewtonErr
CRingBuffer::CopyIn(const UByte* data, long& count)
{
	if (count != 0)
	{
		UByte* p1; long n1; UByte* p2; long n2;
		ComputePutVectors(p1, n1, p2, n2);
		if (p2 != nil && n2 > 0)
		{
			if (count < n2)
				n2 = count;
			BlockMove(data, p2, n2);
			data += n2;
			count -= n2;
			fPut += n2;
			if (fPut == fBufEnd)
				fPut = fBufStart;
		}
		if (p1 != nil && n1 > 0)
		{
			if (count < n1)
				n1 = count;
			BlockMove(data, p1, n1);
			count -= n1;
			fPut += n1;
		}
		if (IsFull())
			return -1;
	}
	return noErr;
}


// ROM 0x001afba4 Get__11CRingBufferFv
int
CRingBuffer::Get()
{
	if (IsEmpty())
		return -1;
	UByte byte = *fGet++;
	if (fGet == fBufEnd)
		fGet = fBufStart;
	return byte;
}


// ROM 0x001afbec Getn__11CRingBufferFPUcl
int
CRingBuffer::Getn(UByte* data, long count)
{
	long remaining = count;
	CopyOut(data, remaining);
	return count - remaining;
}


// ROM 0x001afc1c CopyOut__11CRingBufferFPUcRl
NewtonErr
CRingBuffer::CopyOut(UByte* data, long& count)
{
	if (count != 0)
	{
		UByte* p1; long n1; UByte* p2; long n2;
		ComputeGetVectors(p1, n1, p2, n2);
		if (p2 != nil && n2 > 0)
		{
			if (count < n2)
				n2 = count;
			BlockMove(p2, data, n2);
			data += n2;
			count -= n2;
			fGet += n2;
			if (fGet == fBufEnd)
				fGet = fBufStart;
		}
		if (p1 != nil && n1 > 0)
		{
			if (count < n1)
				n1 = count;
			BlockMove(p1, data, n1);
			count -= n1;
			fGet += n1;
		}
		if (IsEmpty())
			return -1;
	}
	return noErr;
}


// ROM 0x001afb0c Peek__11CRingBufferFv
int
CRingBuffer::Peek()
{
	if (fGet == fPut)
		return -1;
	return *fGet;
}


// ROM 0x001afb24 Next__11CRingBufferFv
int
CRingBuffer::Next()
{
	if (fGet != fPut)
	{
		fGet++;
		if (fGet == fBufEnd)
			fGet = fBufStart;
		if (fGet != fPut)
			return *fGet;
	}
	return -1;
}


// ROM 0x001afb64 Skip__11CRingBufferFv
NewtonErr
CRingBuffer::Skip()
{
	if (fGet != fPut)
	{
		fGet++;
		if (fGet == fBufEnd)
			fGet = fBufStart;
		if (fGet != fPut)
			return noErr;
	}
	return -1;
}


// ROM 0x001afa28 GetnAt__11CRingBufferFlPUcT1
// Read count bytes that start offset bytes into the data, without
// consuming: fGet is moved forward, Getn reads, then fGet is put back.
int
CRingBuffer::GetnAt(long offset, UByte* data, long count)
{
	int result = 0;
	if (offset < DataCount())
	{
		UByte* savedGet = fGet;
		UByte* at = fGet + offset;
		if (at >= fBufEnd)
			at -= fSize;
		fGet = at;
		result = Getn(data, count);
		fGet = savedGet;
	}
	return result;
}
