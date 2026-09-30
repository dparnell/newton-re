/*
	File:		packages/PartPipe.h

	Contains:	A package read through a pipe rather than out of memory: the
				two ends of the stream a streamed package source is.

				The sender (TPackageLoader, PackageLoader.h) owns a small
				ring buffer (0x100 bytes) and hands its memory out as a
				shared-memory object; it starts a task of its own, the
				'pipe' application world (TPipeApp), which copies bytes
				from the source pipe into that ring buffer when asked
				(TPipeEventHandler).  The package manager reads the same
				memory through a CShadowRingBuffer, and CPartPipe is the
				pipe it reads through: when the ring buffer runs dry it asks
				the 'pipe' world for more with a TPipeEvent - an RPC that
				comes back once the bytes are there - and moves its put
				offset on by what was put in.

				The events a CPartPipe sends (TPipeEvent's selector):

				  0  SetStreamSize - the part about to be read is this long;
				     the ring buffer is emptied (no reply)
				  1  Underflow - put up to this many more bytes in (an RPC;
				     the reply says how many went in and any error)
				  2  SeekEOF - read the rest of the part and throw it away
				     (no reply)
				  3  Close - the package has been read: the 'pipe' world's
				     loop ends (an RPC)

				A part's bytes are only ever read forwards: CPartPipe does not
				seek, and it cannot be written to.

				An endpoint as the source (TEndpointPipe, the comms area's)
				is taken out of the loading world and put into the 'pipe'
				world while it reads, through gEndpointPipeHooks (package_
				manager does not link comms; packages/EndpointPackages.cpp
				fills them in).

	Reconstructed from the MP2x00 US ROM (0x0018259c-0x00182fc4); each
	function cites its origin.
*/

#ifndef __PARTPIPE_H
#define __PARTPIPE_H

#ifndef __PIPES_H
#include "Pipes.h"
#endif
#ifndef __APPWORLD_H
#include "AppWorld.h"
#endif
#ifndef __USERPORTS_H
#include "UserPorts.h"
#endif

class CBaseRingBuffer;
class CRingBuffer;
class CShadowRingBuffer;


// TEndpointPipe::AddToAppWorld and RemoveFromAppWorld (comms/EndpointPipe.h),
// for a pipe that is one; each throws exPipeException with the error.
// DEVIATION: hooks, the comms area being above package_manager.
struct EndpointPipeHooks
{
	void	(*fAddToAppWorld)(CPipe* pipe);
	void	(*fRemoveFromAppWorld)(CPipe* pipe);
};
extern EndpointPipeHooks	gEndpointPipeHooks;

// the hook called; ==> the error an exPipeException out of it carried
NewtonErr	CallEndpointPipeHook(void (*hook)(CPipe*), CPipe* pipe);


// What a TPipeApp is started with (the ROM's is 0x10 bytes, copied into the
// world whole).
struct PipeInfo
{
	CPipe*			fPipe;				// +0x00  where the package comes from
	ULong			fUnused04;			// +0x04  (nothing sets it)
	CRingBuffer*	fBuffer;			// +0x08  the sender's ring buffer, shared with the manager
	Boolean			fIsEndpoint;		// +0x0c  fPipe is a TEndpointPipe
};


// The event a CPartPipe sends the 'pipe' world (0x14 bytes in the ROM).
enum
{
	kPipeSetStreamSize	= 0,
	kPipeUnderflow		= 1,
	kPipeSeekEOF		= 2,
	kPipeClose			= 3
};

class TPipeEvent : public TAEvent
{
public:
					TPipeEvent();													// ROM 0x00182db4 __ct__10TPipeEventFv
					TPipeEvent(ULong selector, long count, long error);				// ROM 0x00182d60 __ct__10TPipeEventFUllT2

	ULong			fSelector;			// +0x08  kPipe...
	long			fCount;				// +0x0c  the size, or the bytes asked for / put in
	long			fError;				// +0x10  the answer
};


// The manager's end: a pipe over a shadow of the sender's ring buffer.
class CPartPipe : public CPipe
{
public:
					CPartPipe();									// ROM 0x00182e04 __ct__9CPartPipeFv
	virtual			~CPartPipe();									// ROM 0x00182e54 __dt__9CPartPipeFv

	void			Init(ULong portId, CShadowRingBuffer* buffer, UChar ownsBuffer);	// ROM 0x001826cc Init__9CPartPipeFUlP17CShadowRingBufferUc

	virtual long	ReadSeek(long offset, int mode);				// ROM 0x00182974 ReadSeek__9CPartPipeFli - nothing (0)
	virtual long	ReadPosition(void) const;						// ROM 0x001829b0 ReadPosition__9CPartPipeCFv - 0
	virtual long	WriteSeek(long offset, int mode);				// ROM 0x001829b8 WriteSeek__9CPartPipeFli - nothing (0)
	virtual long	WritePosition(void) const;						// ROM 0x001829c0 WritePosition__9CPartPipeCFv - 0
	virtual void	ReadChunk(void* data, long& count, Boolean& eof);	// ROM 0x00182ed4 ReadChunk__9CPartPipeFPvRlRUc
	virtual void	WriteChunk(const void* data, long count, Boolean flush);	// ROM 0x00182fc4 WriteChunk__9CPartPipeFPvlUc - nothing
	virtual void	FlushRead(void);								// ROM 0x001827f4 FlushRead__9CPartPipeFv - nothing
	virtual void	FlushWrite(void);								// ROM 0x001827f8 FlushWrite__9CPartPipeFv - nothing
	virtual void	Reset(void);									// ROM 0x001827fc Reset__9CPartPipeFv - the ring buffer reset
	virtual void	Overflow(void);									// ROM 0x001826fc Overflow__9CPartPipeFv - nothing
	virtual void	Underflow(long count, Boolean& eof);			// ROM 0x00182700 Underflow__9CPartPipeFlRUc

	void			SetStreamSize(long size);						// ROM 0x00182808 SetStreamSize__9CPartPipeFl
	void			SeekEOF(void);									// ROM 0x0018287c SeekEOF__9CPartPipeFv
	void			Close(void);									// ROM 0x001828f0 Close__9CPartPipeFv

	CBaseRingBuffer*	fBuffer;		// +0x04
	UChar				fOwnsBuffer;	// +0x08
	TUPort*				fPort;			// +0x0c  the 'pipe' world's
	long				fUnderflowCount;	// +0x10  what the last Underflow asked for
};


// The 'pipe' world's handler: the sender's end.
class TPipeEventHandler : public TAEventHandler
{
public:
					TPipeEventHandler(PipeInfo* info);			// ROM 0x00182a2c __ct__17TPipeEventHandlerFP8PipeInfo

	virtual	Boolean	AETestEvent(TAEvent* event);				// ROM 0x00182a84 AETestEvent__17TPipeEventHandlerFP7TAEvent - every event
	virtual	void	AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event);	// ROM 0x00182a8c AEHandlerProc__17TPipeEventHandlerFP10TUMsgTokenPUlP7TAEvent

	PipeInfo*		fInfo;				// +0x14  the world's
	long			fRemaining;			// +0x18  what is left of the part being read (0x7fffffff: not known)
	Boolean			fFailed;			// +0x1c  the source failed: nothing more is read
};


// The task that feeds the ring buffer from the source pipe.
class TPipeApp : public TAppWorld
{
public:
					TPipeApp(const PipeInfo& info, UChar isEndpoint);	// ROM 0x0018259c __ct__8TPipeAppFRC8PipeInfoUc

	virtual ULong	GetSizeOf();								// ROM 0x00182d58 GetSizeOf__8TPipeAppFv
	virtual long	MainConstructor();							// ROM 0x001825f8 MainConstructor__8TPipeAppFv
	virtual void	MainDestructor();							// ROM 0x0018297c MainDestructor__8TPipeAppFv

	TPipeEventHandler*	fHandler;		// +0x70
	PipeInfo			fInfo;			// +0x74
	UChar				fIsEndpoint;	// +0x84
};

#endif	/* __PARTPIPE_H */
