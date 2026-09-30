/*
	File:		packages/PartPipe.cpp

	Contains:	The two ends of a streamed package source - see PartPipe.h.

	Reconstructed from the MP2x00 US ROM (0x0018259c-0x00182fc4); each
	function cites its origin.
*/

#include "PartPipe.h"
#include "RingBuffer.h"
#include "AEvents.h"
#include "OSErrors.h"
#include "NewtonExceptions.h"
#include "UserTasks.h"			// GetGlobals

extern const ExceptionName exPipeException;

extern const ExceptionName exPipeException;


/*------------------------------------------------------------------------------
	T P i p e E v e n t
------------------------------------------------------------------------------*/

// ROM 0x00182db4 __ct__10TPipeEventFv
TPipeEvent::TPipeEvent()
{
	fAEventID = kPackageEventId;
	fSelector = 0;
	fCount = 0;
	fError = 0;
}


// ROM 0x00182d60 __ct__10TPipeEventFUllT2
TPipeEvent::TPipeEvent(ULong selector, long count, long error)
{
	fAEventID = kPackageEventId;
	fSelector = selector;
	fError = error;
	fCount = count;
}


/*------------------------------------------------------------------------------
	C P a r t P i p e
------------------------------------------------------------------------------*/

// ROM 0x00182e04 __ct__9CPartPipeFv
CPartPipe::CPartPipe()
{
	fPort = nil;
	fBuffer = nil;
	fOwnsBuffer = false;
	fUnderflowCount = 0;		// (the ROM leaves +0x10 alone until the first underflow)
}


// ROM 0x00182e54 __dt__9CPartPipeFv
// The port object goes; the ring buffer too when the pipe owns it.
CPartPipe::~CPartPipe()
{
	if (fPort != nil)
		delete fPort;
	if (fOwnsBuffer && fBuffer != nil)
		delete fBuffer;
}


// ROM 0x001826cc Init__9CPartPipeFUlP17CShadowRingBufferUc
// Over the shadow of the sender's ring buffer, asking the 'pipe' world at
// the port for more when it runs dry.
void
CPartPipe::Init(ULong portId, CShadowRingBuffer* buffer, UChar ownsBuffer)
{
	fPort = new TUPort(portId);
	fBuffer = buffer;
	fOwnsBuffer = ownsBuffer;
}


// ROM 0x00182974 ReadSeek__9CPartPipeFli
long
CPartPipe::ReadSeek(long /*offset*/, int /*mode*/)
{
	return 0;
}


// ROM 0x001829b0 ReadPosition__9CPartPipeCFv
long
CPartPipe::ReadPosition(void) const
{
	return 0;
}


// ROM 0x001829b8 WriteSeek__9CPartPipeFli
long
CPartPipe::WriteSeek(long /*offset*/, int /*mode*/)
{
	return 0;
}


// ROM 0x001829c0 WritePosition__9CPartPipeCFv
long
CPartPipe::WritePosition(void) const
{
	return 0;
}


// ROM 0x00182ed4 ReadChunk__9CPartPipeFPvRlRUc
// count bytes read: what the ring buffer holds, then more asked for
// (Underflow) until the count is made up.  A ring buffer's error is thrown
// as a pipe exception; -1 (empty) just means ask for more.  count is left
// as it was (all of it read) and eof is never set.
void
CPartPipe::ReadChunk(void* data, long& count, Boolean& eof)
{
	long left = count;
	if (left < 1)
		return;
	NewtonErr err = fBuffer->CopyOut((UByte*) data, left);
	if (err == -1)
		err = noErr;
	for ( ; ; )
	{
		if (err != noErr)
			Throw(exPipeException, (void*) (Long) err, nil);
		for ( ; ; )
		{
			if (left < 1 || err != noErr)
				return;
			fUnderflowCount = left;
			Underflow(left, eof);
			err = fBuffer->CopyOut((UByte*) data + (count - left), left);
			if (err != -1)
				break;
			err = noErr;
		}
	}
}


// ROM 0x00182fc4 WriteChunk__9CPartPipeFPvlUc
void
CPartPipe::WriteChunk(const void* /*data*/, long /*count*/, Boolean /*flush*/)
{ }


// ROM 0x001827f4 FlushRead__9CPartPipeFv
void
CPartPipe::FlushRead(void)
{ }


// ROM 0x001827f8 FlushWrite__9CPartPipeFv
void
CPartPipe::FlushWrite(void)
{ }


// ROM 0x001827fc Reset__9CPartPipeFv
void
CPartPipe::Reset(void)
{
	fBuffer->Reset();
}


// ROM 0x001826fc Overflow__9CPartPipeFv
void
CPartPipe::Overflow(void)
{ }


// ROM 0x00182700 Underflow__9CPartPipeFlRUc
// The 'pipe' world asked (an RPC) for count more bytes; the put offset moved
// on by what it put in.  A failed send, the world's error, or nothing at
// all put in when something was asked for (kError_Unexpected_End_Of_Pkg_Part)
// is thrown as a pipe exception.
void
CPartPipe::Underflow(long count, Boolean& /*eof*/)
{
	TPipeEvent event(kPipeUnderflow, count, 0);
	ULong replySize;
	NewtonErr err = fPort->SendRPC(&replySize, &event, sizeof(event), &event, sizeof(event));
	if (err != noErr)
		Throw(exPipeException, (void*) (Long) err, nil);
	if (event.fError != noErr)
		Throw(exPipeException, (void*) (Long) event.fError, nil);
	if (count != 0 && event.fCount == 0)
		Throw(exPipeException, (void*) (Long) kError_Unexpected_End_Of_Pkg_Part, nil);
	fBuffer->UpdatePutVector(event.fCount);
}


// ROM 0x00182808 SetStreamSize__9CPartPipeFl
// The next part's size told to the 'pipe' world (no reply is waited for).
void
CPartPipe::SetStreamSize(long size)
{
	TPipeEvent event(kPipeSetStreamSize, size, 0);
	if (fPort != nil)
		fPort->Send(&event, sizeof(event));
}


// ROM 0x0018287c SeekEOF__9CPartPipeFv
// What is left of the part read and thrown away by the 'pipe' world.
void
CPartPipe::SeekEOF(void)
{
	TPipeEvent event(kPipeSeekEOF, 0, 0);
	if (fPort != nil)
		fPort->Send(&event, sizeof(event));
}


// ROM 0x001828f0 Close__9CPartPipeFv
// The package read: the 'pipe' world told to finish (an RPC).
void
CPartPipe::Close(void)
{
	TPipeEvent event(kPipeClose, 0, 0);
	if (fPort != nil)
	{
		ULong replySize;
		fPort->SendRPC(&replySize, &event, sizeof(event), &event, sizeof(event));
	}
}


/*------------------------------------------------------------------------------
	T P i p e E v e n t H a n d l e r
------------------------------------------------------------------------------*/

// ROM 0x00182a2c __ct__17TPipeEventHandlerFP8PipeInfo
TPipeEventHandler::TPipeEventHandler(PipeInfo* info)
{
	fRemaining = 0x7fffffff;
	fInfo = info;
	fFailed = false;
}


// ROM 0x00182a84 AETestEvent__17TPipeEventHandlerFP7TAEvent
Boolean
TPipeEventHandler::AETestEvent(TAEvent* /*event*/)
{
	return true;
}


// ROM 0x00182a8c AEHandlerProc__17TPipeEventHandlerFP10TUMsgTokenPUlP7TAEvent
// The manager's CPartPipe asking:
//   SetStreamSize: the part's size noted, the ring buffer emptied;
//   Underflow: the bytes the reader has had taken out of the ring buffer,
//     then as many put in from the source as it asks for, the ring buffer
//     has room for and (when the part's size is known) the part has left -
//     a failure being kError_Unexpected_End_Of_Package, with what did go
//     in; once the source has failed nothing more is read;
//   SeekEOF: the rest of the part read from the source a ring buffer at a
//     time and thrown away;
//   Close: the loop ended (an endpoint put back in its world first).
// Only Underflow and Close are answered; the others were sent without
// waiting.
// ROM QUIRK kept: when the source fails in the middle of a SeekEOF the
// remainder is set to nought and then has the read subtracted from it, so
// the loop goes round once more with a negative count before it stops.
void
TPipeEventHandler::AEHandlerProc(TUMsgToken* /*token*/, ULong* /*size*/, TAEvent* event)
{
	TPipeEvent* pipeEvent = (TPipeEvent*) event;
	CRingBuffer* buffer = fInfo->fBuffer;
	switch (pipeEvent->fSelector)
	{
	case kPipeSetStreamSize:
		fRemaining = pipeEvent->fCount;
		buffer->Reset();
		break;

	case kPipeUnderflow:
		if (!fFailed)
		{
			buffer->UpdateGetVector(buffer->DataCount());
			long room = buffer->FreeCount();
			long wanted;
			if (fRemaining == 0x7fffffff)
			{
				wanted = pipeEvent->fCount;
				if (room <= pipeEvent->fCount)
					wanted = room;
			}
			else
			{
				long n = room;
				if (fRemaining < n)
					n = fRemaining;
				if (pipeEvent->fCount <= n)
					n = pipeEvent->fCount;
				fRemaining = fRemaining - n;
				wanted = n;
			}
			pipeEvent->fCount = wanted;
			pipeEvent->fError = noErr;
			long left = wanted;
			NewtonErr err = buffer->CopyIn(fInfo->fPipe, left);
			if (err != noErr && err != -1)
			{
				pipeEvent->fCount = wanted - left;
				pipeEvent->fError = kError_Unexpected_End_Of_Package;
			}
		}
		else
		{
			pipeEvent->fCount = 0;
			pipeEvent->fError = kError_Unexpected_End_Of_Package;
		}
		SetReply(sizeof(TPipeEvent), event);
		return;

	case kPipeSeekEOF:
	{
		buffer->Reset();
		long remaining = fRemaining;
		while (remaining != 0)
		{
			long n = buffer->FreeCount();
			if (fRemaining < n)
				n = fRemaining;
			long left = n;
			NewtonErr err = buffer->CopyIn(fInfo->fPipe, left);
			if (err != noErr && err != -1)
			{
				fFailed = true;
				fRemaining = 0;
			}
			buffer->Reset();
			remaining = fRemaining - n;
			fRemaining = remaining;
		}
		fRemaining = 0x7fffffff;
		break;
	}

	case kPipeClose:
		// the endpoint let go of by this world (its error, if any, dropped)
		if (fInfo->fIsEndpoint)
			CallEndpointPipeHook(gEndpointPipeHooks.fRemoveFromAppWorld, fInfo->fPipe);
		SetReply(sizeof(TPipeEvent), event);
		ReplyImmed();
		((TAppWorld*) GetGlobals())->AETerminateLoop();
		return;

	default:
		return;
	}
	DeferReply();
}


EndpointPipeHooks	gEndpointPipeHooks = { nil, nil };

// (host) The ROM's try around an endpoint pipe's call: an exPipeException's
// data is the error; any other exception goes on.
NewtonErr
CallEndpointPipeHook(void (*hook)(CPipe*), CPipe* pipe)
{
	NewtonErr err = noErr;
	if (hook == nil)
		return noErr;
	newton_try
	{
		hook(pipe);
	}
	newton_catch(exPipeException)
	{
		err = (NewtonErr) (intptr_t) CurrentException()->data;
	}
	end_try;
	return err;
}


/*------------------------------------------------------------------------------
	T P i p e A p p
------------------------------------------------------------------------------*/

// ROM 0x0018259c __ct__8TPipeAppFRC8PipeInfoUc
TPipeApp::TPipeApp(const PipeInfo& info, UChar isEndpoint)
{
	fHandler = nil;
	fInfo = info;
	fInfo.fIsEndpoint = isEndpoint;
	fIsEndpoint = isEndpoint;
}


// ROM 0x00182d58 GetSizeOf__8TPipeAppFv
// DEVIATION: the ROM's 0x88; the host's object is laid out by its compiler.
ULong
TPipeApp::GetSizeOf()
{
	return sizeof(TPipeApp);
}


// ROM 0x001825f8 MainConstructor__8TPipeAppFv
// In the new task: the world, and the handler for the manager's pipe
// events (an endpoint is taken into the world as well).  ==> the world's
// error, or kError_No_Memory for no handler.
long
TPipeApp::MainConstructor()
{
	long err = TAppWorld::MainConstructor();
	if (err == noErr)
	{
		fHandler = new TPipeEventHandler(&fInfo);
		if (fHandler == nil)
			err = kError_No_Memory;
		else
			fHandler->Init(kPackageEventId, kNewtEventClass);
	}
	// the endpoint taken into this world; its error, or nought, the answer
	// ROM BUG kept: an endpoint's answer replaces the world's own error, so
	// a world that failed to start is answered noErr when the endpoint came in
	if (fIsEndpoint)
		err = CallEndpointPipeHook(gEndpointPipeHooks.fAddToAppWorld, fInfo.fPipe);
	return err;
}


// ROM 0x0018297c MainDestructor__8TPipeAppFv
void
TPipeApp::MainDestructor()
{
	if (fHandler != nil)
		delete fHandler;
	TAppWorld::MainDestructor();
}
