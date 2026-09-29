/*
	File:		utility/Pipes.cpp

	Contains:	CPipe, PipeCallBack and CBufferPipe (Pipes.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Pipes.h"
#include "ByteOrder.h"
#include "UCErrors.h"
#include "NewtonExceptions.h"
#include "NewtonMemory.h"
#include <string.h>
#include <stdlib.h>

extern const ExceptionName exPipeException;


/*------------------------------------------------------------------------------
	P i p e C a l l B a c k
------------------------------------------------------------------------------*/

// ROM 0x0018a694 __ct__12PipeCallBackFv
PipeCallBack::PipeCallBack()
{
	fReadTotal = -1;
	fWriteTotal = -1;
}


// ROM 0x0018a6d4 __dt__12PipeCallBackFv
PipeCallBack::~PipeCallBack()
{ }


/*------------------------------------------------------------------------------
	C P i p e
------------------------------------------------------------------------------*/

// ROM 0x0018a424 __ct__5CPipeFv
CPipe::CPipe()
{ }


// ROM 0x0018a458 __dt__5CPipeFv
CPipe::~CPipe()
{ }


// ROM 0x0018a50c ResetRead__5CPipeFv
void
CPipe::ResetRead(void)
{
	Throw(exPipeException, (void*) eNotImplemented, nil);
}


// ROM 0x0018a678 ResetWrite__5CPipeFv
void
CPipe::ResetWrite(void)
{
	Throw(exPipeException, (void*) eNotImplemented, nil);
}


// The scalars are read and written as the bytes of the ARM's memory:
// big-endian.  (Host: assembled from bytes; in the ROM the chunk is the
// variable itself.)

// ROM 0x0018a6ec __rs__5CPipeFRc
CPipe&
CPipe::operator>>(char& c)
{
	long count = 1;
	Boolean eof;
	ReadChunk(&c, count, eof);
	return *this;
}


// ROM 0x0018a754 __rs__5CPipeFRSc
CPipe&
CPipe::operator>>(signed char& c)
{
	long count = 1;
	Boolean eof;
	ReadChunk(&c, count, eof);
	return *this;
}


// ROM 0x0018a7bc __rs__5CPipeFRUc
CPipe&
CPipe::operator>>(unsigned char& c)
{
	long count = 1;
	Boolean eof;
	ReadChunk(&c, count, eof);
	return *this;
}


// ROM 0x0018a824 __rs__5CPipeFRs
CPipe&
CPipe::operator>>(short& s)
{
	UByte bytes[2];
	long count = 2;
	Boolean eof;
	ReadChunk(bytes, count, eof);
	s = (short) GetBigEndianHalf(bytes);
	return *this;
}


// ROM 0x0018a88c __rs__5CPipeFRUs
CPipe&
CPipe::operator>>(unsigned short& s)
{
	UByte bytes[2];
	long count = 2;
	Boolean eof;
	ReadChunk(bytes, count, eof);
	s = GetBigEndianHalf(bytes);
	return *this;
}


// ROM 0x0018a8f4 __rs__5CPipeFRl
CPipe&
CPipe::operator>>(long& l)
{
	UByte bytes[4];
	long count = 4;
	Boolean eof;
	ReadChunk(bytes, count, eof);
	l = (long) (Long32) GetBigEndianWord(bytes);
	return *this;
}


// ROM 0x0018a470 __rs__5CPipeFRUl
CPipe&
CPipe::operator>>(unsigned long& l)
{
	UByte bytes[4];
	long count = 4;
	Boolean eof;
	ReadChunk(bytes, count, eof);
	l = GetBigEndianWord(bytes);
	return *this;
}


// ROM 0x0018a4d8 __ls__5CPipeFc
CPipe&
CPipe::operator<<(char c)
{
	WriteChunk(&c, 1, false);
	return *this;
}


// ROM 0x0018a528 __ls__5CPipeFSc
CPipe&
CPipe::operator<<(signed char c)
{
	WriteChunk(&c, 1, false);
	return *this;
}


// ROM 0x0018a55c __ls__5CPipeFUc
CPipe&
CPipe::operator<<(unsigned char c)
{
	WriteChunk(&c, 1, false);
	return *this;
}


// ROM 0x0018a590 __ls__5CPipeFs
CPipe&
CPipe::operator<<(short s)
{
	UByte bytes[2];
	PutBigEndianHalf(bytes, (unsigned short) s);
	WriteChunk(bytes, 2, false);
	return *this;
}


// ROM 0x0018a5d4 __ls__5CPipeFUs
CPipe&
CPipe::operator<<(unsigned short s)
{
	UByte bytes[2];
	PutBigEndianHalf(bytes, s);
	WriteChunk(bytes, 2, false);
	return *this;
}


// ROM 0x0018a618 __ls__5CPipeFl
CPipe&
CPipe::operator<<(long l)
{
	UByte bytes[4];
	PutBigEndianWord(bytes, (unsigned int) l);
	WriteChunk(bytes, 4, false);
	return *this;
}


// ROM 0x0018a648 __ls__5CPipeFUl
CPipe&
CPipe::operator<<(unsigned long l)
{
	UByte bytes[4];
	PutBigEndianWord(bytes, (unsigned int) l);
	WriteChunk(bytes, 4, false);
	return *this;
}


/*------------------------------------------------------------------------------
	C B u f f e r P i p e
------------------------------------------------------------------------------*/

// ROM 0x00046abc __ct__11CBufferPipeFv
CBufferPipe::CBufferPipe()
{
	fReadBuffer = nil;
	fWriteBuffer = nil;
	fOwnsBuffers = false;
	fReadHitEOF = false;
}


// ROM 0x00046b10 __dt__11CBufferPipeFv
CBufferPipe::~CBufferPipe()
{
	if (fOwnsBuffers)
	{
		if (fReadBuffer != nil)
			delete fReadBuffer;
		if (fWriteBuffer != nil)
			delete fWriteBuffer;
	}
}


// ROM 0x00046f2c Init__11CBufferPipeFlT1
// A read segment of readSize bytes (positioned at its end: nothing to
// read yet) and a write segment of writeSize (either size 0: none).
void
CBufferPipe::Init(long readSize, long writeSize)
{
	fOwnsBuffers = true;
	fReadHitEOF = false;
	if (readSize > 0)
	{
		fReadBuffer = new CBufferSegment;
		if (fReadBuffer == nil)
			Throw(exPipeException, (void*) (Long) MemError(), nil);
		NewtonErr err = fReadBuffer->Init(readSize);
		if (err != noErr)
			Throw(exPipeException, (void*) (Long) err, nil);
		fReadBuffer->Seek(0, kSeekFromEnd);
	}
	if (writeSize > 0)
	{
		fWriteBuffer = new CBufferSegment;
		if (fWriteBuffer == nil)
			Throw(exPipeException, (void*) (Long) MemError(), nil);
		NewtonErr err = fWriteBuffer->Init(writeSize);
		if (err != noErr)
			Throw(exPipeException, (void*) (Long) err, nil);
	}
}


// ROM 0x00047020 Init__11CBufferPipeFP14CBufferSegmentT1Uc
void
CBufferPipe::Init(CBufferSegment* readBuffer, CBufferSegment* writeBuffer, Boolean ownsBuffers)
{
	fReadBuffer = readBuffer;
	fWriteBuffer = writeBuffer;
	fOwnsBuffers = ownsBuffers;
	Reset();
}


// The segment the operation needs, or the exception.
static CBufferSegment*
Required(CBufferSegment* buffer)
{
	if (buffer == nil)
		Throw(exPipeException, (void*) eNotInitialized, nil);
	return buffer;
}


// ROM 0x00047398 ReadSeek__11CBufferPipeFli
long
CBufferPipe::ReadSeek(long offset, int mode)
{
	return Required(fReadBuffer)->Seek(offset, mode);
}


// ROM 0x00046be8 ReadPosition__11CBufferPipeCFv
long
CBufferPipe::ReadPosition(void) const
{
	return Required(fReadBuffer)->Position();
}


// ROM 0x00046b8c WriteSeek__11CBufferPipeFli
long
CBufferPipe::WriteSeek(long offset, int mode)
{
	return Required(fWriteBuffer)->Seek(offset, mode);
}


// ROM 0x00046c30 WritePosition__11CBufferPipeCFv
long
CBufferPipe::WritePosition(void) const
{
	return Required(fWriteBuffer)->Position();
}


// ROM 0x000470bc ReadChunk__11CBufferPipeFPvRlRUc
// count bytes from the read segment, Underflow asked for more (it fills
// the segment and sets fReadHitEOF when the source has no more) until
// they are all there or the source is exhausted; count comes back as
// what was read, eof set when the source ran dry.
void
CBufferPipe::ReadChunk(void* data, long& count, Boolean& eof)
{
	CBufferSegment* buffer = Required(fReadBuffer);
	long remaining = count;
	Boolean exhausted = buffer->AtEOF();
	eof = false;
	if (count > 0 && !fReadHitEOF)
	{
		int result = buffer->CopyOut((UByte*) data, remaining);
		if (result == -1)
			exhausted = true;
		else if (result != 0)
			Throw(exPipeException, (void*) (Long) result, nil);
		while (remaining > 0 && !fReadHitEOF)
		{
			Underflow(remaining, fReadHitEOF);
			exhausted = false;
			result = buffer->CopyOut((UByte*) data + (count - remaining), remaining);
			if (result == -1)
				exhausted = true;
			else if (result != 0)
				Throw(exPipeException, (void*) (Long) result, nil);
		}
	}
	if (fReadHitEOF)
	{
		Boolean atEnd = exhausted;
		if (!atEnd && remaining > 0)
		{
			int result = buffer->CopyOut((UByte*) data, remaining);
			if (result == -1)
				atEnd = true;
			else if (result != 0)
				Throw(exPipeException, (void*) (Long) result, nil);
		}
		if (atEnd)
		{
			fReadHitEOF = false;
			eof = true;
		}
	}
	count -= remaining;
}


// ROM 0x0004728c WriteChunk__11CBufferPipeFPvlUc
// count bytes into the write segment, Overflow asked to make room (it
// empties the segment) as it fills; FlushWrite when asked.
void
CBufferPipe::WriteChunk(const void* data, long count, Boolean flush)
{
	CBufferSegment* buffer = Required(fWriteBuffer);
	long remaining = count;
	if (count > 0)
	{
		int result = buffer->CopyIn((const UByte*) data, remaining);
		if (result != -1 && result != 0)
			Throw(exPipeException, (void*) (Long) result, nil);
		while (remaining > 0)
		{
			Overflow();
			result = buffer->CopyIn((const UByte*) data + (count - remaining), remaining);
			if (result != -1 && result != 0)
				Throw(exPipeException, (void*) (Long) result, nil);
		}
	}
	if (flush)
		FlushWrite();
}


// ROM 0x00047034 Reset__11CBufferPipeFv
void
CBufferPipe::Reset(void)
{
	ResetRead();
	ResetWrite();
}


// ROM 0x00047060 ResetRead__11CBufferPipeFv
// The read buffer emptied: reset, then its position put at its end, so
// the first read finds nothing and asks Underflow for data.  (The host's
// version once left the seek out, so a fresh read buffer looked full of
// whatever its block held.)
void
CBufferPipe::ResetRead(void)
{
	fReadHitEOF = false;
	if (fReadBuffer != nil)
	{
		fReadBuffer->Reset();
		fReadBuffer->Seek(0, kSeekFromEnd);
	}
}


// ROM 0x000470a8 ResetWrite__11CBufferPipeFv
void
CBufferPipe::ResetWrite(void)
{
	if (fWriteBuffer != nil)
		fWriteBuffer->Reset();
}


// ROM 0x00046c78 Peek__11CBufferPipeFUc
int
CBufferPipe::Peek(Boolean /*flag*/)
{
	return Required(fReadBuffer)->Peek();
}


// ROM 0x00046d04 Next__11CBufferPipeFv
int
CBufferPipe::Next(void)
{
	return Required(fReadBuffer)->Next();
}


// ROM 0x00046d90 Skip__11CBufferPipeFv
int
CBufferPipe::Skip(void)
{
	return Required(fReadBuffer)->Skip();
}


// ROM 0x00046e1c Get__11CBufferPipeFv
int
CBufferPipe::Get(void)
{
	return Required(fReadBuffer)->Get();
}


// ROM 0x00046ea8 Put__11CBufferPipeFi
int
CBufferPipe::Put(int dataByte)
{
	return Required(fWriteBuffer)->Put(dataByte);
}


/*------------------------------------------------------------------------------
	C M e m o r y P i p e
------------------------------------------------------------------------------*/

// ROM 0x002d8588 FlushRead__11CMemoryPipeFv
void
CMemoryPipe::FlushRead(void)
{ }


// ROM 0x002d858c FlushWrite__11CMemoryPipeFv
void
CMemoryPipe::FlushWrite(void)
{ }


// ROM 0x002d8590 Overflow__11CMemoryPipeFv
// The memory is full: an error.
void
CMemoryPipe::Overflow(void)
{
	Throw(exPipeException, (void*) -1, nil);
}


// ROM 0x002d85a8 Underflow__11CMemoryPipeFlRUc
// The memory is read: the end.
void
CMemoryPipe::Underflow(long /*count*/, Boolean& eof)
{
	eof = true;
}


/*------------------------------------------------------------------------------
	C N u l l P i p e
------------------------------------------------------------------------------*/

// ROM 0x00147184 __ct__9CNullPipeFl
CNullPipe::CNullPipe(long growBy)
{
	fGrowBy = growBy;
}


// ROM 0x001471cc __dt__9CNullPipeFv
CNullPipe::~CNullPipe()
{ }


// ROM 0x0014720c FlushRead__9CNullPipeFv
// What there is to read thrown away.
void
CNullPipe::FlushRead(void)
{
	if (fReadBuffer == nil)
		return;
	fReadBuffer->Reset();
	fReadBuffer->Seek(0, 1);
}


// ROM 0x0014724c FlushWrite__9CNullPipeFv
void
CNullPipe::FlushWrite(void)
{
	if (fWriteBuffer != nil)
		fWriteBuffer->Reset();
}


// ROM 0x001472d4 Reset__9CNullPipeFv
void
CNullPipe::Reset(void)
{
	CBufferPipe::Reset();
	if (fReadBuffer != nil)
	{
		fReadBuffer->Reset();
		fReadBuffer->Seek(0, 1);
	}
	if (fWriteBuffer != nil)
		fWriteBuffer->Reset();
}


// ROM 0x00147260 Overflow__9CNullPipeFv
// The write segment made bigger by fGrowBy, or (growing by nothing) emptied.
void
CNullPipe::Overflow(void)
{
	if (fWriteBuffer == nil)
		return;
	if (fGrowBy <= 0)
	{
		fWriteBuffer->Reset();
		return;
	}
	NewtonErr err = fWriteBuffer->SetPhysicalSize(fWriteBuffer->GetPhysicalSize() + fGrowBy);
	if (err != noErr)
		Throw(exPipeException, (void*) (Long) err, nil);
}


// ROM 0x001472c8 Underflow__9CNullPipeFlRUc
// Never the end.
void
CNullPipe::Underflow(long /*count*/, Boolean& eof)
{
	eof = false;
}


/*------------------------------------------------------------------------------
	M e m o r y P i p e
------------------------------------------------------------------------------*/

// ROM 0x000cf7e8 __ct__10MemoryPipeFv
MemoryPipe::MemoryPipe()
{ }


// ROM 0x000cf828 __dt__10MemoryPipeFv
MemoryPipe::~MemoryPipe()
{ }


// ROM 0x000cfad0 FlushRead__10MemoryPipeFv
// The read segment's position to its end.
void
MemoryPipe::FlushRead(void)
{
	if (fReadBuffer != nil)
		fReadBuffer->Seek(0, kSeekFromEnd);
}


// ROM 0x000d0918 FlushWrite__10MemoryPipeFv
void
MemoryPipe::FlushWrite(void)
{
	if (fWriteBuffer != nil)
		fWriteBuffer->Reset();
}


// ROM 0x000d16d0 Reset__10MemoryPipeFv
void
MemoryPipe::Reset(void)
{
	CBufferPipe::Reset();
	if (fReadBuffer != nil)
		fReadBuffer->Seek(0, kSeekFromBeginning);
}


// ROM 0x000d1614 Overflow__10MemoryPipeFv
// The write segment written over again.
void
MemoryPipe::Overflow(void)
{
	if (fWriteBuffer != nil)
		fWriteBuffer->Reset();
}


// ROM 0x000d16c4 Underflow__10MemoryPipeFlRUc
// Nothing more comes, but it is not the end.
void
MemoryPipe::Underflow(long /*count*/, Boolean& eof)
{
	eof = false;
}


/*------------------------------------------------------------------------------
	C P t r P i p e
------------------------------------------------------------------------------*/

// ROM 0x00195518 __ct__8CPtrPipeFv
CPtrPipe::CPtrPipe()
{
	fData = nil;
	fPosition = 0;
	fSize = 0;
	fCallback = nil;
	fOwnsData = false;
}


// ROM 0x00195570 __dt__8CPtrPipeFv
CPtrPipe::~CPtrPipe()
{
	if (fOwnsData)
		free(fData);
}


// ROM 0x001955c0 ReadPosition__8CPtrPipeCFv
long
CPtrPipe::ReadPosition(void) const
{
	return fPosition;
}


// ROM 0x001955c8 WritePosition__8CPtrPipeCFv
long
CPtrPipe::WritePosition(void) const
{
	return fPosition;
}


// ROM 0x001955d0 Overflow__8CPtrPipeFv
void
CPtrPipe::Overflow(void)
{
	Throw(exPipeException, (void*) eOverflow, nil);
}


// ROM 0x001955ec Underflow__8CPtrPipeFlRUc
void
CPtrPipe::Underflow(long count, Boolean& eof)
{
	Throw(exPipeException, (void*) eUnderflow, nil);
}


// ROM 0x00195608 FlushRead__8CPtrPipeFv
void
CPtrPipe::FlushRead(void)
{ }


// ROM 0x0019560c FlushWrite__8CPtrPipeFv
void
CPtrPipe::FlushWrite(void)
{ }


// ROM 0x00195610 Init__8CPtrPipeFlP12PipeCallBack
// A block of size bytes of its own (freed with the pipe).
void
CPtrPipe::Init(long size, PipeCallBack* callback)
{
	if (size < 1)
		Throw(exPipeException, (void*) eBadSize, nil);
	void* data = malloc(size);
	if (data == nil)
		Throw(exPipeException, (void*) (Long) MemError(), nil);
	Init(data, size, true, callback);
}


// ROM 0x00195690 Init__8CPtrPipeFPvlUcP12PipeCallBack
void
CPtrPipe::Init(void* data, long size, Boolean ownsData, PipeCallBack* callback)
{
	fData = (char*) data;
	fPosition = 0;
	fSize = size;
	fOwnsData = ownsData;
	fCallback = callback;
}


// ROM 0x001956b4 Reset__8CPtrPipeFv
void
CPtrPipe::Reset(void)
{
	fPosition = 0;
}


// ROM 0x001956c0 ReadChunk__8CPtrPipeFPvRlRUc
// count bytes (all of them, or eUnderflow).
void
CPtrPipe::ReadChunk(void* data, long& count, Boolean& eof)
{
	eof = false;
	if (fSize - fPosition < count)
		Throw(exPipeException, (void*) eUnderflow, nil);
	memcpy(data, fData + fPosition, count);
	fPosition += count;
}


// ROM 0x00195740 WriteChunk__8CPtrPipeFPvlUc
void
CPtrPipe::WriteChunk(const void* data, long count, Boolean flush)
{
	if (fSize - fPosition < count)
		Throw(exPipeException, (void*) eOverflow, nil);
	memcpy(fData + fPosition, data, count);
	fPosition += count;
}


// ROM 0x001957b0 Seek__8CPtrPipeFli
// (A mode that is none of the three leaves the position where it was.)
long
CPtrPipe::Seek(long offset, int mode)
{
	if (offset == 0)
	{
		if (mode == kSeekFromBeginningPos)
			offset = 0;
		else if (mode == kSeekFromEndPos)
			offset = fSize;
		else
			return fPosition;
	}
	else if (mode != kSeekFromBeginningPos)
	{
		if (mode == kSeekFromCurrentPos)
			offset = fPosition + offset;
		else if (mode == kSeekFromEndPos)
			offset = fSize - offset;
		else
			return fPosition;
	}
	fPosition = offset;
	return fPosition;
}


// ROM 0x00195808 ReadSeek__8CPtrPipeFli
long
CPtrPipe::ReadSeek(long offset, int mode)
{
	return Seek(offset, mode);
}


// ROM 0x0019580c WriteSeek__8CPtrPipeFli
long
CPtrPipe::WriteSeek(long offset, int mode)
{
	return Seek(offset, mode);
}
