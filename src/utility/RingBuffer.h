/*
	File:		utility/RingBuffer.h

	Contains:	CRingBuffer, the byte ring buffer the serial and comm code
				streams through, and its abstract base CBaseRingBuffer (the
				interface CRingPipe and CTaskPipe hold).

				The buffer holds fSize bytes and wastes one slot, so it can
				tell full from empty by the pointers alone: fGet == fPut is
				empty, and fPut one slot behind fGet (with wrap) is full - so
				a buffer made for n bytes (Init(n)) holds n (GetSize()).
				Bytes go in at fPut (Put/Putn/CopyIn) and come out at fGet
				(Get/Getn/CopyOut/Peek/Next/Skip); the pointers wrap at
				fBufEnd back to fBufStart.  The Compute*Vectors calls give the
				up-to-two contiguous runs a bulk copy crosses the wrap in, and
				Update*Vector advances the pointers over a run.

	The DDK has no header for these; reconstructed from the MP2100 D ROM
	(0x001af060-0x001afc60), each function citing its origin.  The ROM
	dispatches the primitives through the vtable (so Put asks the virtual
	IsFull, and so on); the reconstruction keeps that.  NOT YET: the shared
	forms (MakeShared/UnShare, over a TUObject) and CopyIn from a CPipe.
*/

#ifndef __RINGBUFFER_H
#define __RINGBUFFER_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif


// The interface CRingPipe / CTaskPipe hold a pointer to.  The ROM's is a
// TUObject subclass with only a constructor and destructor of its own; the
// primitives are pure virtual and answered by CRingBuffer.
class CBaseRingBuffer
{
public:
					CBaseRingBuffer();				// ROM 0x0003b3f8 __ct__15CBaseRingBufferFv
	virtual			~CBaseRingBuffer();				// ROM 0x0003b438 __dt__15CBaseRingBufferFv

	virtual NewtonErr	Init(long size) = 0;
	virtual NewtonErr	Init(void* buffer, long size, UChar ownsIt, long getOffset, long putOffset) = 0;
	virtual void		Reset() = 0;
	virtual long		GetSize() const = 0;
	virtual Boolean		IsFull() const = 0;
	virtual Boolean		IsEmpty() const = 0;
	virtual Boolean		AtEOF() const = 0;
	virtual long		FreeCount() const = 0;
	virtual long		DataCount() const = 0;
	virtual void		ComputePutVectors(UByte*& p1, long& n1, UByte*& p2, long& n2) const = 0;
	virtual void		ComputeGetVectors(UByte*& p1, long& n1, UByte*& p2, long& n2) const = 0;
	virtual long		UpdatePutVector(long count) = 0;
	virtual long		UpdateGetVector(long count) = 0;
	virtual int			Put(int byte) = 0;
	virtual int			Putn(const UByte* data, long count) = 0;
	virtual int			Get() = 0;
	virtual int			Getn(UByte* data, long count) = 0;
	virtual int			Peek() = 0;
	virtual int			Next() = 0;
	virtual NewtonErr	Skip() = 0;
	virtual NewtonErr	CopyIn(const UByte* data, long& count) = 0;
	virtual NewtonErr	CopyOut(UByte* data, long& count) = 0;
	virtual int		GetnAt(long offset, UByte* data, long count) = 0;
};


class CRingBuffer : public CBaseRingBuffer
{
public:
					CRingBuffer();					// ROM 0x001af060 __ct__11CRingBufferFv
	virtual			~CRingBuffer();					// ROM 0x001af0c8 __dt__11CRingBufferFv

	NewtonErr		Init(long size);				// ROM 0x001af5d8 Init__11CRingBufferFl - allocate a buffer of size bytes
	NewtonErr		Init(void* buffer, long size, UChar ownsIt, long getOffset, long putOffset);	// ROM 0x001afaa8 Init__11CRingBufferFPvlUcN22 - over an existing buffer
	void			Reset();						// ROM 0x001af500 Reset__11CRingBufferFv
	long			GetSize() const;				// ROM 0x001af510 GetSize__11CRingBufferCFv
	Boolean			IsFull() const;					// ROM 0x001af51c IsFull__11CRingBufferCFv
	Boolean			IsEmpty() const;				// ROM 0x001af548 IsEmpty__11CRingBufferCFv
	Boolean			AtEOF() const;					// ROM 0x001af564 AtEOF__11CRingBufferCFv
	long			FreeCount() const;				// ROM 0x001af5b4 FreeCount__11CRingBufferCFv
	long			DataCount() const;				// ROM 0x001af640 DataCount__11CRingBufferCFv
	void			ComputePutVectors(UByte*& p1, long& n1, UByte*& p2, long& n2) const;	// ROM 0x001af660 ComputePutVectors__11CRingBufferCFRPUcRlT1T2
	void			ComputeGetVectors(UByte*& p1, long& n1, UByte*& p2, long& n2) const;	// ROM 0x001af72c ComputeGetVectors__11CRingBufferCFRPUcRlT1T2
	long			UpdatePutVector(long count);	// ROM 0x001af8cc UpdatePutVector__11CRingBufferFl
	long			UpdateGetVector(long count);	// ROM 0x001af800 UpdateGetVector__11CRingBufferFl
	int				Put(int byte);					// ROM 0x001af128 Put__11CRingBufferFi
	int				Putn(const UByte* data, long count);	// ROM 0x001af17c Putn__11CRingBufferFPCUcl
	int				Get();							// ROM 0x001afba4 Get__11CRingBufferFv
	int				Getn(UByte* data, long count);	// ROM 0x001afbec Getn__11CRingBufferFPUcl
	int				Peek();							// ROM 0x001afb0c Peek__11CRingBufferFv
	int				Next();							// ROM 0x001afb24 Next__11CRingBufferFv
	NewtonErr		Skip();							// ROM 0x001afb64 Skip__11CRingBufferFv
	NewtonErr		CopyIn(const UByte* data, long& count);		// ROM 0x001af1ac CopyIn__11CRingBufferFPCUcRl
	NewtonErr		CopyOut(UByte* data, long& count);			// ROM 0x001afc1c CopyOut__11CRingBufferFPUcRl
	int				GetnAt(long offset, UByte* data, long count);	// ROM 0x001afa28 GetnAt__11CRingBufferFlPUcT1

	UByte*			fBufStart;			// +0x04
	UByte*			fBufEnd;			// +0x08  fBufStart + fSize
	long			fSize;				// +0x0c  the buffer's byte count (usable capacity + 1)
	UByte*			fPut;				// +0x10  where the next Put writes
	UByte*			fGet;				// +0x14  where the next Get reads
	// +0x18 the TUObject the shared forms use - NOT YET
	Boolean			fOwnsBuffer;		// +0x21  the buffer was allocated here and must be freed
};

#endif	/* __RINGBUFFER_H */
