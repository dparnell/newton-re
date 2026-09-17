/*
	File:		utility/RingBuffer.h

	Contains:	CRingBuffer, the byte ring buffer the serial and comm code
				streams through, its abstract base CBaseRingBuffer (the
				interface CRingPipe and CTaskPipe hold), and CRingPipe, the
				CPipe over such a buffer.

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
	(0x001af060-0x001b00d0), each function citing its origin.  The ROM
	dispatches the primitives through the vtable (so Put asks the virtual
	IsFull, and so on); the reconstruction keeps that, and declares the
	virtuals in the ROM's vtable order (analysis/vtable.py on the CRingBuffer
	vtable) - note that Init is *not* virtual there, and that CopyIn(CPipe*)
	and GetnAt are CRingBuffer's own additions past the end of the base's
	twenty.  NOT YET: the shared forms (MakeShared/UnShare, over the TUObject
	at +0x18) and CShadowRingBuffer, the form over shared memory.
*/

#ifndef __RINGBUFFER_H
#define __RINGBUFFER_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

#ifndef __PIPES_H
#include "Pipes.h"
#endif


// The interface CRingPipe / CTaskPipe hold a pointer to.  The ROM's is a
// TUObject subclass with only a constructor and destructor of its own; the
// primitives are pure virtual and answered by CRingBuffer.
class CBaseRingBuffer
{
public:
					CBaseRingBuffer();				// ROM 0x0003b3f8 __ct__15CBaseRingBufferFv
	virtual			~CBaseRingBuffer();				// ROM 0x0003b438 __dt__15CBaseRingBufferFv

	// the twenty pure virtuals, in the ROM's vtable order (+0x04 .. +0x50)
	virtual int			Peek() = 0;
	virtual int			Next() = 0;
	virtual NewtonErr	Skip() = 0;
	virtual int			Get() = 0;
	virtual int			Getn(UByte* data, long count) = 0;
	virtual NewtonErr	CopyOut(UByte* data, long& count) = 0;
	virtual int			Put(int byte) = 0;
	virtual int			Putn(const UByte* data, long count) = 0;
	virtual NewtonErr	CopyIn(const UByte* data, long& count) = 0;
	virtual void		Reset() = 0;
	virtual long		GetSize() const = 0;
	virtual Boolean		AtEOF() const = 0;
	virtual Boolean		IsFull() const = 0;
	virtual Boolean		IsEmpty() const = 0;
	virtual long		FreeCount() const = 0;
	virtual long		DataCount() const = 0;
	virtual long		UpdatePutVector(long count) = 0;
	virtual long		UpdateGetVector(long count) = 0;
	virtual void		ComputePutVectors(UByte*& p1, long& n1, UByte*& p2, long& n2) const = 0;
	virtual void		ComputeGetVectors(UByte*& p1, long& n1, UByte*& p2, long& n2) const = 0;
};


class CRingBuffer : public CBaseRingBuffer
{
public:
					CRingBuffer();					// ROM 0x001af060 __ct__11CRingBufferFv
	virtual			~CRingBuffer();					// ROM 0x001af0c8 __dt__11CRingBufferFv

	// not virtual in the ROM: the other ring buffers' Init calls take other arguments
	NewtonErr		Init(long size);				// ROM 0x001af5d8 Init__11CRingBufferFl - allocate a buffer of size bytes
	NewtonErr		Init(void* buffer, long size, UChar ownsIt, long getOffset, long putOffset);	// ROM 0x001afaa8 Init__11CRingBufferFPvlUcN22 - over an existing buffer

	virtual int			Peek();						// ROM 0x001afb0c Peek__11CRingBufferFv
	virtual int			Next();						// ROM 0x001afb24 Next__11CRingBufferFv
	virtual NewtonErr	Skip();						// ROM 0x001afb64 Skip__11CRingBufferFv
	virtual int			Get();						// ROM 0x001afba4 Get__11CRingBufferFv
	virtual int			Getn(UByte* data, long count);				// ROM 0x001afbec Getn__11CRingBufferFPUcl
	virtual NewtonErr	CopyOut(UByte* data, long& count);			// ROM 0x001afc1c CopyOut__11CRingBufferFPUcRl
	virtual int			Put(int byte);				// ROM 0x001af128 Put__11CRingBufferFi
	virtual int			Putn(const UByte* data, long count);		// ROM 0x001af17c Putn__11CRingBufferFPCUcl
	virtual NewtonErr	CopyIn(const UByte* data, long& count);		// ROM 0x001af1ac CopyIn__11CRingBufferFPCUcRl
	virtual void		Reset();					// ROM 0x001af500 Reset__11CRingBufferFv
	virtual long		GetSize() const;			// ROM 0x001af510 GetSize__11CRingBufferCFv
	virtual Boolean		AtEOF() const;				// ROM 0x001af564 AtEOF__11CRingBufferCFv
	virtual Boolean		IsFull() const;				// ROM 0x001af51c IsFull__11CRingBufferCFv
	virtual Boolean		IsEmpty() const;			// ROM 0x001af548 IsEmpty__11CRingBufferCFv
	virtual long		FreeCount() const;			// ROM 0x001af5b4 FreeCount__11CRingBufferCFv
	virtual long		DataCount() const;			// ROM 0x001af640 DataCount__11CRingBufferCFv
	virtual long		UpdatePutVector(long count);	// ROM 0x001af8cc UpdatePutVector__11CRingBufferFl
	virtual long		UpdateGetVector(long count);	// ROM 0x001af800 UpdateGetVector__11CRingBufferFl
	virtual void		ComputePutVectors(UByte*& p1, long& n1, UByte*& p2, long& n2) const;	// ROM 0x001af660 ComputePutVectors__11CRingBufferCFRPUcRlT1T2
	virtual void		ComputeGetVectors(UByte*& p1, long& n1, UByte*& p2, long& n2) const;	// ROM 0x001af72c ComputeGetVectors__11CRingBufferCFRPUcRlT1T2

	// CRingBuffer's own virtuals, past the base's twenty (+0x54, +0x58)
	virtual NewtonErr	CopyIn(CPipe* pipe, long& count);			// ROM 0x001af2e8 CopyIn__11CRingBufferFP5CPipeRl
	virtual int			GetnAt(long offset, UByte* data, long count);	// ROM 0x001afa28 GetnAt__11CRingBufferFlPUcT1

	UByte*			fBufStart;			// +0x04
	UByte*			fBufEnd;			// +0x08  fBufStart + fSize
	long			fSize;				// +0x0c  the buffer's byte count (usable capacity + 1)
	UByte*			fPut;				// +0x10  where the next Put writes
	UByte*			fGet;				// +0x14  where the next Get reads
	// +0x18 the TUObject the shared forms use - NOT YET
	Boolean			fOwnsBuffer;		// +0x21  the buffer was allocated here and must be freed
};


/*------------------------------------------------------------------------------
	C R i n g P i p e
	A pipe whose bytes live in a ring buffer.  Reading and writing drain and
	fill the buffer; when the buffer runs dry part-way through a read the
	pipe asks the virtual Underflow for more, and when it fills part-way
	through a write it asks Overflow to make room, so that a subclass
	(CTaskPipe, CPartPipe) decides where the bytes come from and go to.
	The class is abstract in the ROM - Underflow, Overflow, FlushRead and
	FlushWrite are pure there - and the pipe does not seek: ReadSeek,
	WriteSeek, ReadPosition and WritePosition all answer 0.
------------------------------------------------------------------------------*/

class CRingPipe : public CPipe
{
public:
					CRingPipe();					// ROM 0x001afd58 __ct__9CRingPipeFv
	virtual			~CRingPipe();					// ROM 0x001afda4 __dt__9CRingPipeFv

	void			Init(long size);				// ROM 0x001afe0c Init__9CRingPipeFl - a CRingBuffer of size bytes, owned
	void			Init(CBaseRingBuffer* buffer, UChar ownsIt);	// ROM 0x001afe84 Init__9CRingPipeFP15CBaseRingBufferUc

	virtual long	ReadSeek(long offset, int mode);	// ROM 0x001b00b8 ReadSeek__9CRingPipeFli
	virtual long	ReadPosition(void) const;			// ROM 0x001b00c8 ReadPosition__9CRingPipeCFv
	virtual long	WriteSeek(long offset, int mode);	// ROM 0x001b00c0 WriteSeek__9CRingPipeFli
	virtual long	WritePosition(void) const;			// ROM 0x001afe04 WritePosition__9CRingPipeCFv
	virtual void	ReadChunk(void* data, long& count, Boolean& eof);		// ROM 0x001afeb4 ReadChunk__9CRingPipeFPvRlRUc
	virtual void	WriteChunk(const void* data, long count, Boolean flush);	// ROM 0x001affcc WriteChunk__9CRingPipeFPvlUc
	virtual void	Reset(void);						// ROM 0x001afe98 Reset__9CRingPipeFv

	CBaseRingBuffer*	fBuffer;		// +0x04
	Boolean				fOwnsBuffer;	// +0x08  disposed of with the pipe
	Boolean				fReadHitEOF;	// +0x09  the buffer ran out during a read
};

#endif	/* __RINGBUFFER_H */
