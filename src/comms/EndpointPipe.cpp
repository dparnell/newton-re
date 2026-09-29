/*
	File:		comms/EndpointPipe.cpp

	Contains:	TEndpointPipe (EndpointPipe.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "EndpointPipe.h"
#include "Endpoint.h"
#include "UCErrors.h"
#include "NewtonExceptions.h"

extern const ExceptionName exPipeException;


// ROM 0x000ad0f4 __ct__13TEndpointPipeFv
TEndpointPipe::TEndpointPipe()
{
	fEndpoint = nil;
	fTimeout = 0;
	fFraming = false;
	fCallback = nil;
	fBytesRead = 0;
	fBytesWritten = 0;
	fAborted = false;
}


// ROM 0x000ad704 __dt__13TEndpointPipeFv
TEndpointPipe::~TEndpointPipe()
{ }


// ROM 0x000ad0f0 Init__13TEndpointPipeFP9TEndpointlT2UlUc
// (The ROM's does nothing at all.)
void
TEndpointPipe::Init(TEndpoint* endpoint, long readSize, long writeSize, ULong timeout, Boolean framing)
{ }


// ROM 0x000ad744 Init__13TEndpointPipeFP9TEndpointlT2UlUcP12PipeCallBack
// Segments of the sizes (nought: none), the endpoint, how long a send or
// receive may take, whether the data is framed, and who is told.
void
TEndpointPipe::Init(TEndpoint* endpoint, long readSize, long writeSize, ULong timeout, Boolean framing, PipeCallBack* callback)
{
	CBufferPipe::Init(readSize, writeSize);
	fEndpoint = endpoint;
	fTimeout = timeout;
	fFraming = framing;
	fCallback = callback;
}


// ROM 0x000ad1d0 Overflow__13TEndpointPipeFv
// The write segment full: sent (the more flag set), and emptied.
void
TEndpointPipe::Overflow(void)
{
	if (fEndpoint == nil || fWriteBuffer == nil)
		Throw(exPipeException, (void*) eNotInitialized, nil);
	if (fAborted)
		Throw(exPipeException, (void*) kError_Call_Aborted, nil);
	Size size = fWriteBuffer->GetSize();
	fWriteBuffer->Seek(0, kSeekFromBeginning);
	ULong flags = fFraming ? 3 : 1;
	NewtonErr err = fEndpoint->nSnd(fWriteBuffer, flags, fTimeout, true, nil);
	if (err == noErr)
		fBytesWritten += size;
	else
	{
		fWriteBuffer->Seek(0, kSeekFromEnd);
		Throw(exPipeException, (void*) (Long) err, nil);
	}
	fWriteBuffer->Reset();
	if (fCallback != nil && !fCallback->Status(fBytesRead, fBytesWritten))
		Throw(exPipeException, (void*) kError_Call_Aborted, nil);
}


// ROM 0x000ad328 Underflow__13TEndpointPipeFlRUc
// The read segment empty: count bytes more received (no more than the
// segment holds); the end of a packet is the end.
void
TEndpointPipe::Underflow(long count, Boolean& eof)
{
	if (fEndpoint == nil || fReadBuffer == nil)
		Throw(exPipeException, (void*) eNotInitialized, nil);
	if (fAborted)
		Throw(exPipeException, (void*) kError_Call_Aborted, nil);
	ULong flags = fFraming ? 2 : 0;
	fReadBuffer->Reset();
	if (fReadBuffer->GetSize() < count)
		count = fReadBuffer->GetSize();
	NewtonErr err = fEndpoint->nRcv(fReadBuffer, count, &flags, fTimeout, true, nil);
	if (err == noErr)
		fBytesRead += fReadBuffer->GetSize();
	else
	{
		fReadBuffer->Seek(0, kSeekFromEnd);
		Throw(exPipeException, (void*) (Long) err, nil);
	}
	eof = (flags & 1) == 0;
	fReadBuffer->Seek(0, kSeekFromBeginning);
	if (fCallback != nil && !fCallback->Status(fBytesRead, fBytesWritten))
		Throw(exPipeException, (void*) kError_Call_Aborted, nil);
}


// ROM 0x000ad4d0 FlushWrite__13TEndpointPipeFv
// What has been written sent (the segment cut to it), the more flag clear.
void
TEndpointPipe::FlushWrite(void)
{
	if (fEndpoint == nil || fWriteBuffer == nil)
		Throw(exPipeException, (void*) eNotInitialized, nil);
	if (fAborted)
		Throw(exPipeException, (void*) kError_Call_Aborted, nil);
	if (fWriteBuffer == nil)
		return;
	Size position = fWriteBuffer->Position();
	Size size = fWriteBuffer->GetSize();
	if (position < size)
		fWriteBuffer->Hide(size - position, kSeekFromEnd);
	fWriteBuffer->Seek(0, kSeekFromBeginning);
	ULong flags = fFraming ? 2 : 0;
	NewtonErr err = fEndpoint->nSnd(fWriteBuffer, flags, fTimeout, true, nil);
	if (err != noErr)
	{
		fWriteBuffer->Seek(0, kSeekFromEnd);
		Throw(exPipeException, (void*) (Long) err, nil);
	}
	fWriteBuffer->Reset();
}


// ROM 0x000ad614 FlushRead__13TEndpointPipeFv
// What is left in the read segment thrown away.
void
TEndpointPipe::FlushRead(void)
{
	if (fEndpoint == nil || fReadBuffer == nil)
		Throw(exPipeException, (void*) eNotInitialized, nil);
	if (fAborted)
		Throw(exPipeException, (void*) kError_Call_Aborted, nil);
	fReadBuffer->Reset();
	fReadBuffer->Seek(0, kSeekFromEnd);
	fReadHitEOF = false;
}


// ROM 0x000ad6a8 ResetWrite__13TEndpointPipeFv
void
TEndpointPipe::ResetWrite(void)
{
	fBytesWritten = 0;
	if (fWriteBuffer != nil)
		fWriteBuffer->Reset();
}


// ROM 0x000ad6b4 ResetRead__13TEndpointPipeFv
void
TEndpointPipe::ResetRead(void)
{
	fBytesRead = 0;
	fReadHitEOF = false;
	if (fReadBuffer != nil)
	{
		fReadBuffer->Reset();
		fReadBuffer->Seek(0, kSeekFromEnd);
	}
}


// ROM 0x000ad6c0 Abort__13TEndpointPipeFv
// A synchronous call in progress aborted; otherwise the next use throws.
void
TEndpointPipe::Abort(void)
{
	if (fEndpoint != nil && fEndpoint->IsPending(kSyncCall))
	{
		fEndpoint->nAbort(true);
		return;
	}
	fAborted = true;
}


// ROM 0x000ad7b0 AddToAppWorld__13TEndpointPipeFv
void
TEndpointPipe::AddToAppWorld(void)
{
	if (fEndpoint == nil)
		Throw(exPipeException, (void*) eNotInitialized, nil);
	if (fAborted)
		Throw(exPipeException, (void*) kError_Call_Aborted, nil);
	NewtonErr err = fEndpoint->AddToAppWorld();
	if (err != noErr)
		Throw(exPipeException, (void*) (Long) err, nil);
}


// ROM 0x000ad154 RemoveFromAppWorld__13TEndpointPipeFv
void
TEndpointPipe::RemoveFromAppWorld(void)
{
	if (fEndpoint == nil)
		Throw(exPipeException, (void*) eNotInitialized, nil);
	if (fAborted)
		Throw(exPipeException, (void*) kError_Call_Aborted, nil);
	NewtonErr err = fEndpoint->RemoveFromAppWorld();
	if (err != noErr)
		Throw(exPipeException, (void*) (Long) err, nil);
}


// ROM 0x000ad790 SetTimeout__13TEndpointPipeFUl
void		TEndpointPipe::SetTimeout(ULong timeout)		{ fTimeout = timeout; }
// ROM 0x000ad798 GetTimeout__13TEndpointPipeFv
ULong		TEndpointPipe::GetTimeout(void)					{ return fTimeout; }
// ROM 0x000ad7a0 UseFraming__13TEndpointPipeFUc
void		TEndpointPipe::UseFraming(Boolean framing)		{ fFraming = framing; }
// ROM 0x000ad7a8 UsingFraming__13TEndpointPipeFv
Boolean		TEndpointPipe::UsingFraming(void)				{ return fFraming; }
