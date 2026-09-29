/*
	File:		utility/TaskSafeRingBuffer.h

	Contains:	TTaskSafeRingBuffer, a byte ring buffer two tasks can share
				- one putting, the other getting - and TTaskSafeRingPipe,
				a CPipe over one.

				The buffer's pointers are guarded by one semaphore; a task
				owns the buffer while it works on it (Acquire/Release, which
				nest), so a task may hold it across several calls.  The
				*Completely calls wait for room or data a pause at a time,
				throwing exPipeException -10021 (kError_Message_Timed_Out)
				when a timeout (0: none) passes; a waiting task in a fork
				family lets the family run meanwhile (the fork-world flag
				of Init).  A signal set by another task (fGetSignal,
				fPutSignal) is thrown by the next get or put.  One slot is
				kept free to tell full from empty; the vectors
				(ComputePutVectors/ComputeGetVectors) are the ROM's, the run
				that starts at the pointer coming back second.

				The pipe reads and writes through the *Completely calls with
				its pause and timeout.  The NTK's nub and its task talk
				through two of these (comms/NTK.h).

				The field names are ours, their order the ROM's (offsets
				noted, for the ROM's 0x34 and 0x14 bytes).

	Reconstructed from the MP2x00 US ROM (0x00250ec8-0x00252054); each
	function cites its origin.
*/

#ifndef __TASKSAFERINGBUFFER_H
#define __TASKSAFERINGBUFFER_H

#include "RingBuffer.h"
#include "Pipes.h"

class TULockingSemaphore;


class TTaskSafeRingBuffer : public CBaseRingBuffer
{
public:
						TTaskSafeRingBuffer();
	virtual				~TTaskSafeRingBuffer();

	NewtonErr			Init(long size, Boolean forkWorld);

	virtual int			Peek();
	virtual int			Next();
	virtual NewtonErr	Skip();
	virtual int			Get();
	virtual int			Getn(UByte* data, long count);
	virtual NewtonErr	CopyOut(UByte* data, long& count);
	virtual int			Put(int byte);
	virtual int			Putn(const UByte* data, long count);
	virtual NewtonErr	CopyIn(const UByte* data, long& count);
	virtual void		Reset();
	virtual long		GetSize() const;
	virtual Boolean		AtEOF() const;
	virtual Boolean		IsFull() const;
	virtual Boolean		IsEmpty() const;
	virtual long		FreeCount() const;
	virtual long		DataCount() const;
	virtual long		UpdatePutVector(long count);
	virtual long		UpdateGetVector(long count);
	virtual void		ComputePutVectors(UByte*& p1, long& n1, UByte*& p2, long& n2) const;
	virtual void		ComputeGetVectors(UByte*& p1, long& n1, UByte*& p2, long& n2) const;

	// TTaskSafeRingBuffer's own virtuals (+0x54 .. +0x60)
	virtual int			GetCompletely(ULong pause, ULong timeout);
	virtual void		GetnCompletely(UByte* data, long count, ULong pause, ULong timeout);
	virtual void		PutCompletely(int byte, ULong pause, ULong timeout);
	virtual void		PutnCompletely(const UByte* data, long count, ULong pause, ULong timeout);

	void				Pause(ULong delay);
	void				Acquire() const;
	void				Release() const;
	void				CheckGetSignal();
	void				CheckPutSignal();

	UByte*				fBuffer;				// +0x04
	UByte*				fBufEnd;				// +0x08
	long				fSize;					// +0x0c  one more than it holds
	UByte*				fPutPtr;				// +0x10
	UByte*				fGetPtr;				// +0x14
	TULockingSemaphore*	fOwnerLock;				// +0x18  held by the owning task
	TULockingSemaphore*	fGuard;					// +0x1c  guards the ownership
	NewtonErr			fGetSignal;				// +0x20
	NewtonErr			fPutSignal;				// +0x24
	long				fAcquireCount;			// +0x28
	TObjectId			fOwner;					// +0x2c
	Boolean				fOwnBuffer;				// +0x30
	Boolean				fForkWorld;				// +0x31  waits let the fork family run
};


class TTaskSafeRingPipe : public CPipe
{
public:
						TTaskSafeRingPipe();
	virtual				~TTaskSafeRingPipe();

	void				Init(long size, ULong pause, ULong timeout, Boolean forkWorld);
	void				Init(TTaskSafeRingBuffer* buffer, Boolean ownBuffer, ULong pause, ULong timeout);

	virtual long		ReadSeek(long offset, int mode);
	virtual long		ReadPosition(void) const;
	virtual long		WriteSeek(long offset, int mode);
	virtual long		WritePosition(void) const;
	virtual void		ReadChunk(void* data, long& count, Boolean& eof);
	virtual void		WriteChunk(const void* data, long count, Boolean flush);
	virtual void		FlushRead(void);
	virtual void		FlushWrite(void);
	virtual void		Reset(void);
	virtual void		Overflow(void);
	virtual void		Underflow(long count, Boolean& eof);

	TTaskSafeRingBuffer*	fBuffer;			// +0x04
	ULong				fPause;					// +0x08
	ULong				fTimeout;				// +0x0c
	Boolean				fOwnBuffer;				// +0x10
};

#endif	/* __TASKSAFERINGBUFFER_H */
