/*
	File:		utility/Pipes.cpp

	Contains:	CPipe, PipeCallBack and CBufferPipe (Pipes.h).

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "Pipes.h"
#include "ByteOrder.h"
#include "UCErrors.h"
#include "NewtonExceptions.h"
#include "NewtonMemory.h"

extern const ExceptionName exPipeException;


/*------------------------------------------------------------------------------
	P i p e C a l l B a c k
------------------------------------------------------------------------------*/

// ROM 0x0018c6b4 __ct__12PipeCallBackFv
PipeCallBack::PipeCallBack()
{
	fUnknown04 = -1;
	fUnknown08 = -1;
}


// ROM 0x0018c6f4 __dt__12PipeCallBackFv
PipeCallBack::~PipeCallBack()
{ }


/*------------------------------------------------------------------------------
	C P i p e
------------------------------------------------------------------------------*/

// ROM 0x0018c444 __ct__5CPipeFv
CPipe::CPipe()
{ }


// ROM 0x0018c478 __dt__5CPipeFv
CPipe::~CPipe()
{ }


// ROM 0x0018c52c ResetRead__5CPipeFv
void
CPipe::ResetRead(void)
{
	Throw(exPipeException, (void*) eNotImplemented, nil);
}


// ROM 0x0018c698 ResetWrite__5CPipeFv
void
CPipe::ResetWrite(void)
{
	Throw(exPipeException, (void*) eNotImplemented, nil);
}


// The scalars are read and written as the bytes of the ARM's memory:
// big-endian.  (Host: assembled from bytes; in the ROM the chunk is the
// variable itself.)

// ROM 0x0018c70c __rs__5CPipeFRc
CPipe&
CPipe::operator>>(char& c)
{
	long count = 1;
	Boolean eof;
	ReadChunk(&c, count, eof);
	return *this;
}


// ROM 0x0018c774 __rs__5CPipeFRSc
CPipe&
CPipe::operator>>(signed char& c)
{
	long count = 1;
	Boolean eof;
	ReadChunk(&c, count, eof);
	return *this;
}


// ROM 0x0018c7dc __rs__5CPipeFRUc
CPipe&
CPipe::operator>>(unsigned char& c)
{
	long count = 1;
	Boolean eof;
	ReadChunk(&c, count, eof);
	return *this;
}


// ROM 0x0018c844 __rs__5CPipeFRs
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


// ROM 0x0018c8ac __rs__5CPipeFRUs
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


// ROM 0x0018c914 __rs__5CPipeFRl
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


// ROM 0x0018c490 __rs__5CPipeFRUl
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


// ROM 0x0018c4f8 __ls__5CPipeFc
CPipe&
CPipe::operator<<(char c)
{
	WriteChunk(&c, 1, false);
	return *this;
}


// ROM 0x0018c548 __ls__5CPipeFSc
CPipe&
CPipe::operator<<(signed char c)
{
	WriteChunk(&c, 1, false);
	return *this;
}


// ROM 0x0018c57c __ls__5CPipeFUc
CPipe&
CPipe::operator<<(unsigned char c)
{
	WriteChunk(&c, 1, false);
	return *this;
}


// ROM 0x0018c5b0 __ls__5CPipeFs
CPipe&
CPipe::operator<<(short s)
{
	UByte bytes[2];
	PutBigEndianHalf(bytes, (unsigned short) s);
	WriteChunk(bytes, 2, false);
	return *this;
}


// ROM 0x0018c5f4 __ls__5CPipeFUs
CPipe&
CPipe::operator<<(unsigned short s)
{
	UByte bytes[2];
	PutBigEndianHalf(bytes, s);
	WriteChunk(bytes, 2, false);
	return *this;
}


// ROM 0x0018c638 __ls__5CPipeFl
CPipe&
CPipe::operator<<(long l)
{
	UByte bytes[4];
	PutBigEndianWord(bytes, (unsigned int) l);
	WriteChunk(bytes, 4, false);
	return *this;
}


// ROM 0x0018c668 __ls__5CPipeFUl
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

// ROM 0x0004738c __ct__11CBufferPipeFv
CBufferPipe::CBufferPipe()
{
	fReadBuffer = nil;
	fWriteBuffer = nil;
	fOwnsBuffers = false;
	fReadHitEOF = false;
}


// ROM 0x000473e0 __dt__11CBufferPipeFv
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


// ROM 0x000477fc Init__11CBufferPipeFlT1
// A read segment of readSize bytes (positioned at its end: nothing to
// read yet) or, when that is 0, a write segment of writeSize.
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
	else if (writeSize > 0)
	{
		fWriteBuffer = new CBufferSegment;
		if (fWriteBuffer == nil)
			Throw(exPipeException, (void*) (Long) MemError(), nil);
		NewtonErr err = fWriteBuffer->Init(writeSize);
		if (err != noErr)
			Throw(exPipeException, (void*) (Long) err, nil);
	}
}


// ROM 0x000478f0 Init__11CBufferPipeFP14CBufferSegmentT1Uc
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


// ROM 0x00047c68 ReadSeek__11CBufferPipeFli
long
CBufferPipe::ReadSeek(long offset, int mode)
{
	return Required(fReadBuffer)->Seek(offset, mode);
}


// ROM 0x000474b8 ReadPosition__11CBufferPipeCFv
long
CBufferPipe::ReadPosition(void) const
{
	return Required(fReadBuffer)->Position();
}


// ROM 0x0004745c WriteSeek__11CBufferPipeFli
long
CBufferPipe::WriteSeek(long offset, int mode)
{
	return Required(fWriteBuffer)->Seek(offset, mode);
}


// ROM 0x00047500 WritePosition__11CBufferPipeCFv
long
CBufferPipe::WritePosition(void) const
{
	return Required(fWriteBuffer)->Position();
}


// ROM 0x0004798c ReadChunk__11CBufferPipeFPvRlRUc
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


// ROM 0x00047b5c WriteChunk__11CBufferPipeFPvlUc
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


// ROM 0x00047904 Reset__11CBufferPipeFv
void
CBufferPipe::Reset(void)
{
	ResetRead();
	ResetWrite();
}


// ROM 0x00047930 ResetRead__11CBufferPipeFv
void
CBufferPipe::ResetRead(void)
{
	fReadHitEOF = false;
	if (fReadBuffer != nil)
		fReadBuffer->Reset();
}


// ROM 0x00047978 ResetWrite__11CBufferPipeFv
void
CBufferPipe::ResetWrite(void)
{
	if (fWriteBuffer != nil)
		fWriteBuffer->Reset();
}


// ROM 0x00047548 Peek__11CBufferPipeFUc
int
CBufferPipe::Peek(Boolean /*flag*/)
{
	return Required(fReadBuffer)->Peek();
}


// ROM 0x000475d4 Next__11CBufferPipeFv
int
CBufferPipe::Next(void)
{
	return Required(fReadBuffer)->Next();
}


// ROM 0x00047660 Skip__11CBufferPipeFv
int
CBufferPipe::Skip(void)
{
	return Required(fReadBuffer)->Skip();
}


// ROM 0x000476ec Get__11CBufferPipeFv
int
CBufferPipe::Get(void)
{
	return Required(fReadBuffer)->Get();
}


// ROM 0x00047778 Put__11CBufferPipeFi
int
CBufferPipe::Put(int dataByte)
{
	return Required(fWriteBuffer)->Put(dataByte);
}
