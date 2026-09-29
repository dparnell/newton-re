/*
	File:		utility/TaskSafeRingBuffer.cpp

	Contains:	TTaskSafeRingBuffer and TTaskSafeRingPipe
				(TaskSafeRingBuffer.h).

	Reconstructed from the MP2x00 US ROM (0x00250ec8-0x00252054); each
	function cites its origin.
*/

#include "TaskSafeRingBuffer.h"
#include "AppWorld.h"
#include "UserSemaphore.h"
#include "UserGlobals.h"
#include "NewtonMemory.h"
#include "NewtonTime.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"

extern const ExceptionName exPipeException;

// has a timeout (0: none) passed since the deadline was set?
static Boolean
TimedOut(ULong timeout, const TTime& deadline)
{
	if (timeout == 0)
		return false;
	TTime now = GetGlobalTime();
	return CompCompare(&now.time, &deadline.time) > 0;
}


// ROM 0x00250ec8 __ct__19TTaskSafeRingBufferFv
TTaskSafeRingBuffer::TTaskSafeRingBuffer()
{
	fSize = 0;
	fBuffer = nil;
	fBufEnd = nil;
	fPutPtr = nil;
	fGetPtr = nil;
	fOwnerLock = nil;
	fGuard = nil;
	fOwnBuffer = false;
	fAcquireCount = 0;
	fOwner = 0;
	fForkWorld = false;
	fGetSignal = noErr;
	fPutSignal = noErr;
}


// ROM 0x00250f40 __dt__19TTaskSafeRingBufferFv
TTaskSafeRingBuffer::~TTaskSafeRingBuffer()
{
	if (fOwnBuffer && fBuffer != nil)
		DisposePtr((Ptr) fBuffer);
	if (fGuard != nil)
		delete fGuard;
	if (fOwnerLock != nil)
		delete fOwnerLock;
}


// ROM 0x00251618 Init__19TTaskSafeRingBufferFlUc
// A buffer of the size (one byte more, the slot kept free) and the two
// semaphores.  ==> MemError() when a semaphore cannot be made, otherwise
// the second semaphore's Init.
NewtonErr
TTaskSafeRingBuffer::Init(long size, Boolean forkWorld)
{
	fOwnBuffer = true;
	fForkWorld = forkWorld;
	fSize = size + 1;
	fBuffer = (UByte*) NewNamedPtr(fSize, 'trng');
	if (fBuffer != nil)
	{
		fGetPtr = fBuffer;
		fBufEnd = fBuffer + fSize;
		fPutPtr = fBuffer;
		fOwnerLock = new TULockingSemaphore;
		if (fOwnerLock != nil)
		{
			fOwnerLock->Init();
			fGuard = new TULockingSemaphore;
			if (fGuard != nil)
				return fGuard->Init();
		}
	}
	return MemError();
}


/*------------------------------------------------------------------------------
	Ownership
------------------------------------------------------------------------------*/

// ROM 0x00251a98 Acquire__19TTaskSafeRingBufferFv
// The buffer owned by the calling task (again, if it owns it already).
void
TTaskSafeRingBuffer::Acquire() const
{
	TTaskSafeRingBuffer* self = (TTaskSafeRingBuffer*) this;
	fGuard->Acquire(kWaitOnBlock);
	if (fOwner != gCurrentTaskId)
	{
		fGuard->Release();
		fOwnerLock->Acquire(kWaitOnBlock);
		fGuard->Acquire(kWaitOnBlock);
		self->fOwner = gCurrentTaskId;
	}
	else if (fAcquireCount == 0)
	{
		fOwnerLock->Acquire(kWaitOnBlock);
		self->fOwner = gCurrentTaskId;
	}
	self->fAcquireCount++;
	fGuard->Release();
}


// ROM 0x00251b70 Release__19TTaskSafeRingBufferFv
void
TTaskSafeRingBuffer::Release() const
{
	TTaskSafeRingBuffer* self = (TTaskSafeRingBuffer*) this;
	fGuard->Acquire(kWaitOnBlock);
	if (fAcquireCount == 1)
	{
		fOwnerLock->Release();
		self->fOwner = 0;
	}
	self->fAcquireCount--;
	fGuard->Release();
}


// ROM 0x00251b28 CheckGetSignal__19TTaskSafeRingBufferFv
void
TTaskSafeRingBuffer::CheckGetSignal()
{
	if (fGetSignal == noErr)
		return;
	NewtonErr signal = fGetSignal;
	fGetSignal = noErr;
	Throw(exPipeException, (void*) (Long) signal, nil);
}


// ROM 0x00251b4c CheckPutSignal__19TTaskSafeRingBufferFv
void
TTaskSafeRingBuffer::CheckPutSignal()
{
	if (fPutSignal == noErr)
		return;
	NewtonErr signal = fPutSignal;
	fPutSignal = noErr;
	Throw(exPipeException, (void*) (Long) signal, nil);
}


// ROM 0x00251a24 Pause__19TTaskSafeRingBufferFUl
// A while asleep - in a fork family, a fork made and the family's mutex
// let go meanwhile.
void
TTaskSafeRingBuffer::Pause(ULong delay)
{
	if (fForkWorld)
	{
		NewtonErr err = ((TForkWorld*) GetGlobals())->Fork(nil);
		if (err != noErr)
			Throw(exPipeException, (void*) (Long) err, nil);
		((TForkWorld*) GetGlobals())->ReleaseMutex();
	}
	Sleep(delay);
	if (fForkWorld)
		((TForkWorld*) GetGlobals())->AcquireMutex();
}


/*------------------------------------------------------------------------------
	Getting
------------------------------------------------------------------------------*/

// ROM 0x00251bc0 Peek__19TTaskSafeRingBufferFv
int
TTaskSafeRingBuffer::Peek()
{
	CheckGetSignal();
	Acquire();
	int result = (fGetPtr != fPutPtr) ? *fGetPtr : -1;
	Release();
	return result;
}


// ROM 0x00251c00 Next__19TTaskSafeRingBufferFv
// Past the byte, and the next one.
int
TTaskSafeRingBuffer::Next()
{
	int result;
	CheckGetSignal();
	Acquire();
	if (fGetPtr == fPutPtr)
		result = -1;
	else
	{
		if (++fGetPtr == fBufEnd)
			fGetPtr = fBuffer;
		result = (fGetPtr == fPutPtr) ? -1 : *fGetPtr;
	}
	Release();
	return result;
}


// ROM 0x00251c6c Skip__19TTaskSafeRingBufferFv
NewtonErr
TTaskSafeRingBuffer::Skip()
{
	NewtonErr result = noErr;
	CheckGetSignal();
	Acquire();
	if (fGetPtr == fPutPtr)
		result = -1;
	else
	{
		if (++fGetPtr == fBufEnd)
			fGetPtr = fBuffer;
		if (fGetPtr == fPutPtr)
			result = -1;
	}
	Release();
	return result;
}


// ROM 0x00251cd4 Get__19TTaskSafeRingBufferFv
int
TTaskSafeRingBuffer::Get()
{
	int result;
	CheckGetSignal();
	Acquire();
	if (fPutPtr == fGetPtr)
		result = -1;
	else
	{
		result = *fGetPtr++;
		if (fGetPtr == fBufEnd)
			fGetPtr = fBuffer;
	}
	Release();
	return result;
}


// ROM 0x00250fb4 Getn__19TTaskSafeRingBufferFPUcl
// ==> the bytes got.
int
TTaskSafeRingBuffer::Getn(UByte* data, long count)
{
	CheckGetSignal();
	long left = count;
	CopyOut(data, left);
	return count - left;
}


// ROM 0x002510d8 CopyOut__19TTaskSafeRingBufferFPUcRl
// As much as there is, up to count (which comes back as what is left);
// ==> -1 when the buffer is empty afterwards.
NewtonErr
TTaskSafeRingBuffer::CopyOut(UByte* data, long& count)
{
	NewtonErr result = noErr;
	if (count == 0)
		return result;
	Acquire();
	UByte* p1; long n1; UByte* p2; long n2;
	ComputeGetVectors(p1, n1, p2, n2);
	if (p2 != nil && n2 > 0)
	{
		long n = (count >= n2) ? n2 : count;
		BlockMove(p2, data, n);
		data += n;
		count -= n;
		fGetPtr += n;
		if (fGetPtr == fBufEnd)
			fGetPtr = fBuffer;
	}
	if (p1 != nil && n1 > 0)
	{
		long n = (count >= n1) ? n1 : count;
		BlockMove(p1, data, n);
		count -= n;
		fGetPtr += n;
	}
	if (fPutPtr == fGetPtr)
		result = -1;
	Release();
	return result;
}


// ROM 0x00251d2c GetCompletely__19TTaskSafeRingBufferFUlT1
// A byte, waiting for it a pause at a time.
int
TTaskSafeRingBuffer::GetCompletely(ULong pause, ULong timeout)
{
	CheckGetSignal();
	TTime deadline = TimeFromNow(timeout);
	for (;;)
	{
		int byte = Get();
		if (byte != -1)
			return byte;
		Pause(pause);
		if (TimedOut(timeout, deadline))
			Throw(exPipeException, (void*) kError_Message_Timed_Out, nil);
	}
}


// ROM 0x00250ff8 GetnCompletely__19TTaskSafeRingBufferFPUclUlT3
// All count bytes, waiting for them a pause at a time.
void
TTaskSafeRingBuffer::GetnCompletely(UByte* data, long count, ULong pause, ULong timeout)
{
	TTime deadline = TimeFromNow(timeout);
	long offset = 0;
	long left = count;
	if (count == 0)
		return;
	for (;;)
	{
		CheckGetSignal();
		CopyOut(data + offset, left);
		if (left == 0)
			return;
		offset = count - left;
		Pause(pause);
		if (TimedOut(timeout, deadline))
			Throw(exPipeException, (void*) kError_Message_Timed_Out, nil);
		if (left == 0)
			return;
	}
}


/*------------------------------------------------------------------------------
	Putting
------------------------------------------------------------------------------*/

// ROM 0x00251218 Put__19TTaskSafeRingBufferFi
// ==> the byte, or -1 when full.
int
TTaskSafeRingBuffer::Put(int byte)
{
	CheckPutSignal();
	int result = byte;
	Acquire();
	if (IsFull())
		result = -1;
	else
	{
		*fPutPtr++ = byte;
		if (fPutPtr == fBufEnd)
			fPutPtr = fBuffer;
	}
	Release();
	return result;
}


// ROM 0x0025133c Putn__19TTaskSafeRingBufferFPCUcl
// ==> the bytes put.
int
TTaskSafeRingBuffer::Putn(const UByte* data, long count)
{
	CheckPutSignal();
	long left = count;
	CopyIn(data, left);
	return count - left;
}


// ROM 0x00251460 CopyIn__19TTaskSafeRingBufferFPCUcRl
// As much as there is room for, up to count (which comes back as what is
// left); ==> -1 when the buffer is full afterwards.
NewtonErr
TTaskSafeRingBuffer::CopyIn(const UByte* data, long& count)
{
	NewtonErr result = noErr;
	if (count <= 0)
		return result;
	Acquire();
	UByte* p1; long n1; UByte* p2; long n2;
	ComputePutVectors(p1, n1, p2, n2);
	if (p2 != nil && n2 > 0)
	{
		long n = (count >= n2) ? n2 : count;
		BlockMove(data, p2, n);
		data += n;
		count -= n;
		fPutPtr += n;
		if (fPutPtr == fBufEnd)
			fPutPtr = fBuffer;
	}
	if (count > 0 && p1 != nil && n1 > 0)
	{
		long n = (count >= n1) ? n1 : count;
		BlockMove(data, p1, n);
		count -= n;
		fPutPtr += n;
	}
	if (IsFull())
		result = -1;
	Release();
	return result;
}


// ROM 0x00251284 PutCompletely__19TTaskSafeRingBufferFiUlT2
void
TTaskSafeRingBuffer::PutCompletely(int byte, ULong pause, ULong timeout)
{
	CheckPutSignal();
	TTime deadline = TimeFromNow(timeout);
	while (Put(byte) == -1)
	{
		Pause(pause);
		if (TimedOut(timeout, deadline))
			Throw(exPipeException, (void*) kError_Message_Timed_Out, nil);
	}
}


// ROM 0x00251380 PutnCompletely__19TTaskSafeRingBufferFPCUclUlT3
// All count bytes, waiting for room a pause at a time.
void
TTaskSafeRingBuffer::PutnCompletely(const UByte* data, long count, ULong pause, ULong timeout)
{
	TTime deadline = TimeFromNow(timeout);
	long offset = 0;
	long left = count;
	if (count == 0)
		return;
	for (;;)
	{
		CheckPutSignal();
		CopyIn(data + offset, left);
		if (left == 0)
			return;
		offset = count - left;
		Pause(pause);
		if (TimedOut(timeout, deadline))
			Throw(exPipeException, (void*) kError_Message_Timed_Out, nil);
		if (left == 0)
			return;
	}
}


/*------------------------------------------------------------------------------
	The state
------------------------------------------------------------------------------*/

// ROM 0x002515b0 Reset__19TTaskSafeRingBufferFv
void
TTaskSafeRingBuffer::Reset()
{
	CheckPutSignal();
	CheckGetSignal();
	Acquire();
	fGetPtr = fBuffer;
	fPutPtr = fBuffer;
	Release();
}


// ROM 0x002515ec GetSize__19TTaskSafeRingBufferCFv
long
TTaskSafeRingBuffer::GetSize() const
{
	Acquire();
	long size = fSize - 1;
	Release();
	return size;
}


// ROM 0x00251788 AtEOF__19TTaskSafeRingBufferCFv
// Empty or full.
Boolean
TTaskSafeRingBuffer::AtEOF() const
{
	return IsEmpty() || IsFull();
}


// ROM 0x002516e0 IsFull__19TTaskSafeRingBufferCFv
Boolean
TTaskSafeRingBuffer::IsFull() const
{
	((TTaskSafeRingBuffer*) this)->CheckPutSignal();
	((TTaskSafeRingBuffer*) this)->CheckGetSignal();
	Acquire();
	UByte* last = ((fGetPtr == fBuffer) ? fBufEnd : fGetPtr) - 1;
	Boolean full = (fPutPtr == last);
	Release();
	return full;
}


// ROM 0x0025173c IsEmpty__19TTaskSafeRingBufferCFv
Boolean
TTaskSafeRingBuffer::IsEmpty() const
{
	((TTaskSafeRingBuffer*) this)->CheckPutSignal();
	((TTaskSafeRingBuffer*) this)->CheckGetSignal();
	Acquire();
	Boolean empty = (fGetPtr == fPutPtr);
	Release();
	return empty;
}


// ROM 0x002517d8 FreeCount__19TTaskSafeRingBufferCFv
long
TTaskSafeRingBuffer::FreeCount() const
{
	((TTaskSafeRingBuffer*) this)->CheckPutSignal();
	((TTaskSafeRingBuffer*) this)->CheckGetSignal();
	Acquire();
	long count = (fGetPtr - fPutPtr) - 1;
	if (fGetPtr <= fPutPtr)
		count += fSize;
	Release();
	return count;
}


// ROM 0x00251828 DataCount__19TTaskSafeRingBufferCFv
long
TTaskSafeRingBuffer::DataCount() const
{
	((TTaskSafeRingBuffer*) this)->CheckPutSignal();
	((TTaskSafeRingBuffer*) this)->CheckGetSignal();
	Acquire();
	long count = fPutPtr - fGetPtr;
	if (fPutPtr < fGetPtr)
		count += fSize;
	Release();
	return count;
}


// ROM 0x00251a14 UpdatePutVector__19TTaskSafeRingBufferFl
long
TTaskSafeRingBuffer::UpdatePutVector(long count)
{
	return 0;
}


// ROM 0x00251a1c UpdateGetVector__19TTaskSafeRingBufferFl
long
TTaskSafeRingBuffer::UpdateGetVector(long count)
{
	return 0;
}


// ROM 0x00251874 ComputePutVectors__19TTaskSafeRingBufferCFRPUcRlT1T2
// The free runs: (p2, n2) from the put pointer, (p1, n1) the wrapped run
// from the buffer's start.
void
TTaskSafeRingBuffer::ComputePutVectors(UByte*& p1, long& n1, UByte*& p2, long& n2) const
{
	UByte* last = (fGetPtr != fBuffer) ? fGetPtr - 1 : fBufEnd - 1;
	UByte* put = fPutPtr;
	if (put == fGetPtr)
	{
		if (put == fBuffer || fBuffer == last)
		{
			p1 = nil;
			n1 = 0;
		}
		else
		{
			p1 = fBuffer;
			n1 = last - fBuffer;
		}
		p2 = fPutPtr;
		n2 = (fPutPtr > last) ? fBufEnd - fPutPtr : last - fPutPtr;
	}
	else if (put == last)
	{
		p2 = nil;
		p1 = nil;
		n2 = 0;
		n1 = 0;
	}
	else if (put < last)
	{
		p1 = nil;
		n1 = 0;
		p2 = fPutPtr;
		n2 = last - fPutPtr;
	}
	else
	{
		p1 = fBuffer;
		n1 = last - fBuffer;
		p2 = fPutPtr;
		n2 = fBufEnd - fPutPtr;
	}
}


// ROM 0x00251940 ComputeGetVectors__19TTaskSafeRingBufferCFRPUcRlT1T2
// The runs of data: (p2, n2) from the get pointer, (p1, n1) the wrapped
// run from the buffer's start.
void
TTaskSafeRingBuffer::ComputeGetVectors(UByte*& p1, long& n1, UByte*& p2, long& n2) const
{
	UByte* get = fGetPtr;
	UByte* last = (get != fBuffer) ? get - 1 : fBufEnd - 1;
	UByte* put = fPutPtr;
	if (put == get)
	{
		p2 = nil;
		p1 = nil;
		n2 = 0;
		n1 = 0;
	}
	else if ((put == last) ? (put < get) : (get >= put))
	{
		p1 = fBuffer;
		n1 = put - fBuffer;
		p2 = get;
		n2 = fBufEnd - get;
	}
	else
	{
		p1 = nil;
		n1 = 0;
		p2 = get;
		n2 = put - get;
	}
}


/*------------------------------------------------------------------------------
	TTaskSafeRingPipe
------------------------------------------------------------------------------*/

// ROM 0x00251ddc __ct__17TTaskSafeRingPipeFv
// (Waiting a quarter of a second at a time, with no timeout.)
TTaskSafeRingPipe::TTaskSafeRingPipe()
{
	fBuffer = nil;
	fOwnBuffer = false;
	fTimeout = 0;
	fPause = 250 * kMilliseconds;
}


// ROM 0x00251e38 __dt__17TTaskSafeRingPipeFv
TTaskSafeRingPipe::~TTaskSafeRingPipe()
{
	if (fOwnBuffer && fBuffer != nil)
		delete fBuffer;
}


// ROM 0x00251eb0 Init__17TTaskSafeRingPipeFlUlT2Uc
// A buffer of its own.
void
TTaskSafeRingPipe::Init(long size, ULong pause, ULong timeout, Boolean forkWorld)
{
	fOwnBuffer = true;
	fPause = pause;
	fTimeout = timeout;
	TTaskSafeRingBuffer* buffer = new TTaskSafeRingBuffer;
	if (buffer == nil)
		Throw(exPipeException, (void*) (Long) MemError(), nil);
	NewtonErr err = buffer->Init(size, forkWorld);
	if (err != noErr)
		Throw(exPipeException, (void*) (Long) err, nil);
	fBuffer = buffer;
}


// ROM 0x00251f3c Init__17TTaskSafeRingPipeFP19TTaskSafeRingBufferUcUlT3
// Over another's buffer.
void
TTaskSafeRingPipe::Init(TTaskSafeRingBuffer* buffer, Boolean ownBuffer, ULong pause, ULong timeout)
{
	fBuffer = buffer;
	fOwnBuffer = ownBuffer;
	fTimeout = timeout;
	fPause = pause;
	Reset();
}


// ROM 0x0025203c ReadSeek__17TTaskSafeRingPipeFli
long
TTaskSafeRingPipe::ReadSeek(long offset, int mode)
{
	return 0;
}


// ROM 0x0025204c ReadPosition__17TTaskSafeRingPipeCFv
long
TTaskSafeRingPipe::ReadPosition(void) const
{
	return 0;
}


// ROM 0x00252044 WriteSeek__17TTaskSafeRingPipeFli
long
TTaskSafeRingPipe::WriteSeek(long offset, int mode)
{
	return 0;
}


// ROM 0x00251e98 WritePosition__17TTaskSafeRingPipeCFv
long
TTaskSafeRingPipe::WritePosition(void) const
{
	return 0;
}


// ROM 0x00251f74 ReadChunk__17TTaskSafeRingPipeFPvRlRUc
// All the bytes asked for, waiting for them.
void
TTaskSafeRingPipe::ReadChunk(void* data, long& count, Boolean& eof)
{
	if (count == 1)
		*(UByte*) data = fBuffer->GetCompletely(fPause, fTimeout);
	else if (count > 0)
		fBuffer->GetnCompletely((UByte*) data, count, fPause, fTimeout);
}


// ROM 0x00251fe0 WriteChunk__17TTaskSafeRingPipeFPvlUc
void
TTaskSafeRingPipe::WriteChunk(const void* data, long count, Boolean flush)
{
	if (count == 1)
		fBuffer->PutCompletely(*(const UByte*) data, fPause, fTimeout);
	else if (count > 0)
		fBuffer->PutnCompletely((const UByte*) data, count, fPause, fTimeout);
}


// ROM 0x00251ea8 FlushRead__17TTaskSafeRingPipeFv
void
TTaskSafeRingPipe::FlushRead(void)
{ }


// ROM 0x00251eac FlushWrite__17TTaskSafeRingPipeFv
void
TTaskSafeRingPipe::FlushWrite(void)
{ }


// ROM 0x00251f60 Reset__17TTaskSafeRingPipeFv
void
TTaskSafeRingPipe::Reset(void)
{
	if (fBuffer != nil)
		fBuffer->Reset();
}


// ROM 0x00251ea0 Overflow__17TTaskSafeRingPipeFv
void
TTaskSafeRingPipe::Overflow(void)
{ }


// ROM 0x00251ea4 Underflow__17TTaskSafeRingPipeFlRUc
void
TTaskSafeRingPipe::Underflow(long count, Boolean& eof)
{ }
