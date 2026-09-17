/*
	File:		utility/RingBuffer.cpp

	Contains:	CBaseRingBuffer, CRingBuffer and CRingPipe (RingBuffer.h) -
				the byte ring buffer the comm code streams through, and the
				pipe over it.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
	The internal calls go through the virtuals as the ROM's do (Put asks
	IsFull, CopyIn asks ComputePutVectors, ...).
*/

#include "RingBuffer.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"

extern const ExceptionName exPipeException;


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


// ROM 0x001af2e8 CopyIn__11CRingBufferFP5CPipeRl
// Fill the free runs straight from a pipe: the pipe reads its chunk into the
// buffer itself, so the bytes are not copied twice.  A pipe exception becomes
// the answer; anything else is passed on.
NewtonErr
CRingBuffer::CopyIn(CPipe* pipe, long& count)
{
	NewtonErr err = noErr;
	if (count != 0)
	{
		UByte* p1; long n1; UByte* p2; long n2;
		ComputePutVectors(p1, n1, p2, n2);
		// BUG (the ROM's): eof is a stack local the ROM never initialises, so
		// when the first run is empty the test below reads whatever was on the
		// stack.  An indeterminate value cannot be reproduced on the host, so
		// the reconstruction starts it false - the case the ROM meant.
		Boolean eof = false;
		if (p2 != nil && n2 > 0)
		{
			if (count < n2)
				n2 = count;
			newton_try
			{
				pipe->ReadChunk(p2, n2, eof);
			}
			newton_catch(exPipeException)
			{
				err = (NewtonErr) (Long) CurrentException()->data;
			}
			end_try;
			if (err != noErr)
				return err;
			count -= n2;				// n2 is what the pipe actually read
			fPut += n2;
			if (fPut == fBufEnd)
				fPut = fBufStart;
		}
		if (p1 != nil && n1 > 0 && !eof)
		{
			if (count < n1)
				n1 = count;
			newton_try
			{
				pipe->ReadChunk(p1, n1, eof);
			}
			newton_catch(exPipeException)
			{
				err = (NewtonErr) (Long) CurrentException()->data;
			}
			end_try;
			if (err != noErr)
				return err;
			count -= n1;
			fPut += n1;
		}
		if (IsFull())
			return -1;
	}
	return err;
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


/*------------------------------------------------------------------------------
	C R i n g P i p e
------------------------------------------------------------------------------*/

// ROM 0x001afd58 __ct__9CRingPipeFv
// fReadHitEOF is left alone, as the ROM leaves it: both Init calls set it
// (the second by way of Reset) before anything reads it.
CRingPipe::CRingPipe()
{
	fBuffer = nil;
	fOwnsBuffer = false;
}


// ROM 0x001afda4 __dt__9CRingPipeFv
CRingPipe::~CRingPipe()
{
	if (fOwnsBuffer && fBuffer != nil)
		delete fBuffer;
}


// ROM 0x001afe0c Init__9CRingPipeFl
// A CRingBuffer of our own, of size bytes.
void
CRingPipe::Init(long size)
{
	fOwnsBuffer = true;
	fReadHitEOF = false;
	CRingBuffer* buffer = new CRingBuffer;
	if (buffer == nil)
		Throw(exPipeException, (void*) (Long) MemError(), nil);
	NewtonErr err = buffer->Init(size);
	if (err != noErr)
		Throw(exPipeException, (void*) (Long) err, nil);
	fBuffer = buffer;
}


// ROM 0x001afe84 Init__9CRingPipeFP15CBaseRingBufferUc
// Over a buffer someone else made (a shared one, say); Reset empties it.
void
CRingPipe::Init(CBaseRingBuffer* buffer, UChar ownsIt)
{
	fBuffer = buffer;
	fOwnsBuffer = ownsIt;
	Reset();
}


// ROM 0x001afe98 Reset__9CRingPipeFv
void
CRingPipe::Reset(void)
{
	fReadHitEOF = false;
	if (fBuffer != nil)
		fBuffer->Reset();
}


// ROM 0x001b00b8 ReadSeek__9CRingPipeFli
long
CRingPipe::ReadSeek(long inOffset, int inMode)
{
	return 0;								// the pipe does not seek
}


// ROM 0x001b00c8 ReadPosition__9CRingPipeCFv
long
CRingPipe::ReadPosition(void) const
{
	return 0;
}


// ROM 0x001b00c0 WriteSeek__9CRingPipeFli
long
CRingPipe::WriteSeek(long inOffset, int inMode)
{
	return 0;
}


// ROM 0x001afe04 WritePosition__9CRingPipeCFv
long
CRingPipe::WritePosition(void) const
{
	return 0;
}


// ROM 0x001afeb4 ReadChunk__9CRingPipeFPvRlRUc
// Drain the buffer into the caller's data; whenever it runs dry before the
// count is met, Underflow is asked for more (and tells us, in fReadHitEOF,
// when there will be no more).  count comes back as what was read, and the
// end-of-data flag is answered once and then forgotten.
void
CRingPipe::ReadChunk(void* data, long& count, Boolean& eof)
{
	long remaining = count;
	if (count > 0)
	{
		NewtonErr err = fBuffer->CopyOut((UByte*) data, remaining);
		if (err != noErr && err != -1)		// -1 is only "the buffer is empty now"
			Throw(exPipeException, (void*) (Long) err, nil);
		while (remaining > 0)
		{
			Underflow(remaining, fReadHitEOF);
			err = fBuffer->CopyOut((UByte*) data + (count - remaining), remaining);
			if (err != noErr && err != -1)
				Throw(exPipeException, (void*) (Long) err, nil);
		}
	}
	eof = fReadHitEOF;
	if (fReadHitEOF)
		fReadHitEOF = false;
	count -= remaining;
}


// ROM 0x001affcc WriteChunk__9CRingPipeFPvlUc
// Fill the buffer from the caller's data; whenever it fills before the count
// is met, Overflow is asked to make room.  A flush at the end is FlushWrite.
void
CRingPipe::WriteChunk(const void* data, long count, Boolean flush)
{
	long remaining = count;
	if (count > 0)
	{
		NewtonErr err = fBuffer->CopyIn((const UByte*) data, remaining);
		if (err != noErr && err != -1)		// -1 is only "the buffer is full now"
			Throw(exPipeException, (void*) (Long) err, nil);
		while (remaining > 0)
		{
			Overflow();
			err = fBuffer->CopyIn((const UByte*) data + (count - remaining), remaining);
			if (err != noErr && err != -1)
				Throw(exPipeException, (void*) (Long) err, nil);
		}
	}
	if (flush)
		FlushWrite();
}
