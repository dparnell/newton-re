/*
	File:		utility/RingBuffer.cpp

	Contains:	CBaseRingBuffer, CRingBuffer, CRingPipe and
				CShadowRingBuffer (RingBuffer.h) - the byte ring buffer the
				comm code streams through, the pipe over it, and the form
				whose bytes live in a shared-memory object.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
	The internal calls go through the virtuals as the ROM's do (Put asks
	IsFull, CopyIn asks ComputePutVectors, ...).
*/

#include "RingBuffer.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"
#include "SharedTypes.h"
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
	fIsShared = false;
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


// ROM 0x001af998 MakeShared__11CRingBufferFUl
// The buffer's own memory becomes a shared-memory object, so that another
// task can reach it (through a CShadowRingBuffer, say).  Note the
// kSMemNoSizeChangeOnCopyTo the permissions gain: a ring buffer writes at a
// lower offset than the last write every time it wraps, and the block's size
// in use must not follow it down.  A second call re-points the existing
// object at the buffer rather than making another.
void
CRingBuffer::MakeShared(ULong permissions)
{
	NewtonErr err = noErr;
	if (fIsShared || (err = fSharedMem.Init()) == noErr)
		err = fSharedMem.SetBuffer(fBufStart, fSize, permissions + kSMemNoSizeChangeOnCopyTo);
	if (err == noErr)
		fIsShared = true;
}


// ROM 0x001af9f0 UnShare__11CRingBufferFv
NewtonErr
CRingBuffer::UnShare()
{
	if (fIsShared)
	{
		fSharedMem.DestroyObject();
		fIsShared = false;
	}
	return noErr;
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


/*------------------------------------------------------------------------------
	C S h a d o w R i n g B u f f e r

	What the ROM's Compute*Vectors hand back here are offsets into the shared
	memory, not addresses; the base class's virtual passes them through a
	UByte*&, so they go through the pointer type both ways.  ULong is
	pointer-sized (NewtonTypes.h), so nothing is lost.
------------------------------------------------------------------------------*/

static inline UByte*	AsVector(ULong offset)	{ return (UByte*) offset; }
static inline ULong		AsOffset(UByte* p)		{ return (ULong) p; }


// ROM 0x001e12d8 __ct__17CShadowRingBufferFv
CShadowRingBuffer::CShadowRingBuffer()
{
	fPutOffset = 0;
	fGetOffset = 0;
	fTempGetOffset = 0;
	fSize = 0;
}


// ROM 0x001e1334 __dt__17CShadowRingBufferFv
CShadowRingBuffer::~CShadowRingBuffer()
{ }


// ROM 0x001e1868 Init__17CShadowRingBufferFUllT2
// Take a copy of the shared-memory object's id and let its size be the
// buffer's; the data already in it starts at getOffset and is dataCount
// bytes long.
void
CShadowRingBuffer::Init(TObjectId sharedMem, long getOffset, long dataCount)
{
	fSharedMem.CopyObject(sharedMem);
	fSharedMem.GetSize(&fSize, nil);
	fGetOffset = getOffset;
	fTempGetOffset = getOffset;
	fPutOffset = getOffset + dataCount;
}


// ROM 0x001e1cdc GetByteAt__17CShadowRingBufferFl
UByte
CShadowRingBuffer::GetByteAt(long offset)
{
	ULong got = 0;
	UByte byte;
	fSharedMem.CopyFromShared(&got, &byte, 1, offset, nil);
	return byte;
}


// ROM 0x001e1d1c PutByteAt__17CShadowRingBufferFil
int
CShadowRingBuffer::PutByteAt(int byte, long offset)
{
	UByte value = (UByte) byte;
	if (fSharedMem.CopyToShared(&value, 1, offset, nil) != noErr)
		return -1;
	return byte;
}


// ROM 0x001e17fc Reset__17CShadowRingBufferFv
// fTempGetOffset is left where it was - TempReset is what puts it back.
void
CShadowRingBuffer::Reset()
{
	fGetOffset = 0;
	fPutOffset = 0;
}


// ROM 0x001e1628 TempReset__17CShadowRingBufferFv
void
CShadowRingBuffer::TempReset()
{
	fTempGetOffset = fGetOffset;
}


// ROM 0x001e180c GetSize__17CShadowRingBufferCFv
long
CShadowRingBuffer::GetSize() const
{
	return fSize - 1;
}


// ROM 0x001e1c9c IsFull__17CShadowRingBufferCFv
Boolean
CShadowRingBuffer::IsFull() const
{
	ULong next = (fGetOffset == 0) ? fSize : fGetOffset;
	return fPutOffset == next - 1;
}


// ROM 0x001e1cc4 IsEmpty__17CShadowRingBufferCFv
Boolean
CShadowRingBuffer::IsEmpty() const
{
	return fGetOffset == fPutOffset;
}


// ROM 0x001e1818 AtEOF__17CShadowRingBufferCFv
Boolean
CShadowRingBuffer::AtEOF() const
{
	return IsEmpty() || IsFull();
}


// ROM 0x001e18b0 FreeCount__17CShadowRingBufferCFv
long
CShadowRingBuffer::FreeCount() const
{
	long count = (long) (fGetOffset - fPutOffset) - 1;
	if (fGetOffset <= fPutOffset)
		count += fSize;
	return count;
}


// ROM 0x001e18d0 DataCount__17CShadowRingBufferCFv
long
CShadowRingBuffer::DataCount() const
{
	long count = (long) (fPutOffset - fGetOffset);
	if (fPutOffset < fGetOffset)
		count += fSize;
	return count;
}


// ROM 0x001e18f0 TempDataCount__17CShadowRingBufferCFv
// What is still ahead of the speculative read position.
long
CShadowRingBuffer::TempDataCount() const
{
	long count = (long) (fPutOffset - fTempGetOffset);
	if (fPutOffset < fTempGetOffset)
		count += fSize;
	return count;
}


// ROM 0x001e1910 ComputePutVectors__17CShadowRingBufferCFRPUcRlT1T2
// CRingBuffer::ComputePutVectors with the buffer starting at offset 0.
void
CShadowRingBuffer::ComputePutVectors(UByte*& p1, long& n1, UByte*& p2, long& n2) const
{
	ULong limit = (fGetOffset == 0) ? fSize : fGetOffset;
	limit = limit - 1;					// the last writable slot before fGetOffset
	if (fPutOffset == fGetOffset)
	{
		if (fPutOffset == 0 || limit == 0)
		{
			p1 = AsVector(0);
			n1 = 0;
		}
		else
		{
			p1 = AsVector(0);
			n1 = limit;
		}
		if (fPutOffset <= limit)
		{
			p2 = AsVector(fPutOffset);
			n2 = limit - fPutOffset;
		}
		else
		{
			p2 = AsVector(fPutOffset);
			n2 = fSize - fPutOffset;
		}
	}
	else if (fPutOffset == limit)
	{
		p1 = AsVector(0);  n1 = 0;
		p2 = AsVector(0);  n2 = 0;
	}
	else if (fPutOffset <= limit)
	{
		p1 = AsVector(0);  n1 = 0;
		p2 = AsVector(fPutOffset);
		n2 = limit - fPutOffset;
	}
	else
	{
		p1 = AsVector(0);
		n1 = limit;
		p2 = AsVector(fPutOffset);
		n2 = fSize - fPutOffset;
	}
}


// ROM 0x001e1a80 ComputeGetVectors__17CShadowRingBufferCFRPUcRlT1T2
void
CShadowRingBuffer::ComputeGetVectors(UByte*& p1, long& n1, UByte*& p2, long& n2) const
{
	ULong wrap = (fGetOffset == 0) ? fSize : fGetOffset;
	if (fPutOffset == fGetOffset)		// empty
	{
		p1 = AsVector(0);  n1 = 0;
		p2 = AsVector(0);  n2 = 0;
		return;
	}
	if (fPutOffset == wrap - 1)
	{
		if (fPutOffset < fGetOffset)
		{
			p1 = AsVector(0);
			n1 = fPutOffset;
			p2 = AsVector(fGetOffset);
			n2 = fSize - fGetOffset;
			return;
		}
	}
	else if (fPutOffset <= fGetOffset)
	{
		p2 = AsVector(fGetOffset);
		n2 = fSize - fGetOffset;
		p1 = AsVector(0);
		n1 = fPutOffset;
		return;
	}
	p1 = AsVector(0);  n1 = 0;
	p2 = AsVector(fGetOffset);
	n2 = fPutOffset - fGetOffset;
}


// ROM 0x001e19c4 ComputeTempGetVectors__17CShadowRingBufferCFRUlRlT1T2
// The same runs, from the speculative read position - and with the offsets
// declared as what they are, this one not being an override.
void
CShadowRingBuffer::ComputeTempGetVectors(ULong& o1, long& n1, ULong& o2, long& n2) const
{
	ULong wrap = (fTempGetOffset == 0) ? fSize : fTempGetOffset;
	if (fPutOffset == fTempGetOffset)
	{
		o1 = 0;  n1 = 0;
		o2 = 0;  n2 = 0;
		return;
	}
	if (fPutOffset == wrap - 1)
	{
		if (fPutOffset < fTempGetOffset)
		{
			o1 = 0;
			n1 = fPutOffset;
			o2 = fTempGetOffset;
			n2 = fSize - fTempGetOffset;
			return;
		}
	}
	else if (fPutOffset <= fTempGetOffset)
	{
		o2 = fTempGetOffset;
		n2 = fSize - fTempGetOffset;
		o1 = 0;
		n1 = fPutOffset;
		return;
	}
	o1 = 0;  n1 = 0;
	o2 = fTempGetOffset;
	n2 = fPutOffset - fTempGetOffset;
}


// ROM 0x001e1bec UpdatePutVector__17CShadowRingBufferFl
long
CShadowRingBuffer::UpdatePutVector(long count)
{
	if (count > 0)
	{
		UByte* p1; long n1; UByte* p2; long n2;
		ComputePutVectors(p1, n1, p2, n2);
		if (n2 > 0)
		{
			if (count < n2)
				n2 = count;
			count -= n2;
			fPutOffset += n2;
			if (fPutOffset == fSize)
				fPutOffset = 0;
		}
		if (n1 > 0)
		{
			if (count < n1)
				n1 = count;
			count -= n1;
			fPutOffset += n1;
		}
	}
	return count;
}


// ROM 0x001e1b3c UpdateGetVector__17CShadowRingBufferFl
long
CShadowRingBuffer::UpdateGetVector(long count)
{
	if (count > 0)
	{
		UByte* p1; long n1; UByte* p2; long n2;
		ComputeGetVectors(p1, n1, p2, n2);
		if (n2 > 0)
		{
			if (count < n2)
				n2 = count;
			count -= n2;
			fGetOffset += n2;
			if (fGetOffset == fSize)
				fGetOffset = 0;
		}
		if (n1 > 0)
		{
			if (count < n1)
				n1 = count;
			count -= n1;
			fGetOffset += n1;
		}
	}
	return count;
}


// ROM 0x001e1634 Put__17CShadowRingBufferFi
int
CShadowRingBuffer::Put(int byte)
{
	ULong next = (fGetOffset == 0) ? fSize : fGetOffset;
	ULong at = fPutOffset;
	if (at == next - 1)					// full
		return -1;
	fPutOffset = at + 1;
	PutByteAt(byte, at);
	if (fPutOffset == fSize)
		fPutOffset = 0;
	return byte;
}


// ROM 0x001e1694 Putn__17CShadowRingBufferFPCUcl
int
CShadowRingBuffer::Putn(const UByte* data, long count)
{
	long remaining = count;
	CopyIn(data, remaining);
	return count - remaining;
}


// ROM 0x001e16c4 CopyIn__17CShadowRingBufferFPCUcRl
NewtonErr
CShadowRingBuffer::CopyIn(const UByte* data, long& count)
{
	if (count > 0)
	{
		UByte* p1; long n1; UByte* p2; long n2;
		ComputePutVectors(p1, n1, p2, n2);
		if (n2 > 0)
		{
			if (count < n2)
				n2 = count;
			fSharedMem.CopyToShared((void*) data, n2, AsOffset(p2), nil);
			data += n2;
			count -= n2;
			fPutOffset += n2;
			if (fPutOffset == fSize)
				fPutOffset = 0;
		}
		if (n1 > 0)
		{
			if (count < n1)
				n1 = count;
			fSharedMem.CopyToShared((void*) data, n1, AsOffset(p1), nil);
			count -= n1;
			fPutOffset += n1;
		}
	}
	return IsFull() ? -1 : noErr;
}


// ROM 0x001e1d60 Peek__17CShadowRingBufferFv
int
CShadowRingBuffer::Peek()
{
	if (fGetOffset == fPutOffset)
		return -1;
	ULong got = 0;
	UByte byte;
	fSharedMem.CopyFromShared(&got, &byte, 1, fGetOffset, nil);
	return byte;
}


// ROM 0x001e1d78 Next__17CShadowRingBufferFv
int
CShadowRingBuffer::Next()
{
	if (fGetOffset != fPutOffset)
	{
		fGetOffset++;
		if (fGetOffset == fSize)
			fGetOffset = 0;
		if (fGetOffset != fPutOffset)
		{
			ULong got = 0;
			UByte byte;
			fSharedMem.CopyFromShared(&got, &byte, 1, fGetOffset, nil);
			return byte;
		}
	}
	return -1;
}


// ROM 0x001e1db4 Skip__17CShadowRingBufferFv
NewtonErr
CShadowRingBuffer::Skip()
{
	if (fGetOffset != fPutOffset)
	{
		fGetOffset++;
		if (fGetOffset == fSize)
			fGetOffset = 0;
		if (fGetOffset != fPutOffset)
			return noErr;
	}
	return -1;
}


// ROM 0x001e1df4 Get__17CShadowRingBufferFv
int
CShadowRingBuffer::Get()
{
	if (fGetOffset == fPutOffset)
		return -1;
	UByte byte = GetByteAt(fGetOffset);
	fGetOffset++;
	if (fGetOffset == fSize)
		fGetOffset = 0;
	return byte;
}


// ROM 0x001e1e3c Getn__17CShadowRingBufferFPUcl
int
CShadowRingBuffer::Getn(UByte* data, long count)
{
	long remaining = count;
	CopyOut(data, remaining);
	return count - remaining;
}


// ROM 0x001e1380 CopyOut__17CShadowRingBufferFPUcRl
NewtonErr
CShadowRingBuffer::CopyOut(UByte* data, long& count)
{
	if (count > 0)
	{
		UByte* p1; long n1; UByte* p2; long n2;
		ComputeGetVectors(p1, n1, p2, n2);
		if (n2 > 0)
		{
			ULong size = (n2 <= count) ? (ULong) n2 : (ULong) count;
			ULong got = size;
			fSharedMem.CopyFromShared(&got, data, size, AsOffset(p2), nil);
			data += got;
			count -= got;
			fGetOffset += got;
			if (fGetOffset == fSize)
				fGetOffset = 0;
		}
		if (n1 > 0)
		{
			if (count < n1)
				n1 = count;
			ULong got = n1;
			fSharedMem.CopyFromShared(&got, data, n1, AsOffset(p1), nil);
			count -= got;
			fGetOffset += got;
		}
	}
	return IsEmpty() ? -1 : noErr;
}


// ROM 0x001e14c8 TempGetn__17CShadowRingBufferFPUcl
int
CShadowRingBuffer::TempGetn(UByte* data, long count)
{
	long remaining = count;
	TempCopyOut(data, remaining);
	return count - remaining;
}


// ROM 0x001e14f0 TempCopyOut__17CShadowRingBufferFPUcRl
// Read ahead without consuming: only fTempGetOffset moves, so the same bytes
// can be read again after TempReset.  The answer is the error from the copy,
// not the buffer's state.
NewtonErr
CShadowRingBuffer::TempCopyOut(UByte* data, long& count)
{
	NewtonErr err = noErr;
	if (count > 0)
	{
		ULong o1; long n1; ULong o2; long n2;
		ComputeTempGetVectors(o1, n1, o2, n2);
		if (n2 > 0)
		{
			if (count < n2)
				n2 = count;
			ULong got = n2;
			err = fSharedMem.CopyFromShared(&got, data, n2, o2, nil);
			data += got;
			count -= got;
			fTempGetOffset += got;
			if (fTempGetOffset == fSize)
				fTempGetOffset = 0;
		}
		if (n1 > 0)
		{
			if (count < n1)
				n1 = count;
			ULong got = n1;
			err = fSharedMem.CopyFromShared(&got, data, n1, o1, nil);
			count -= got;
			fTempGetOffset += got;
		}
	}
	return err;
}
