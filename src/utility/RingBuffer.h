/*
	File:		utility/RingBuffer.h

	Contains:	CRingBuffer, the byte ring buffer the serial and comm code
				streams through, its abstract base CBaseRingBuffer (the
				interface CRingPipe and CTaskPipe hold), CRingPipe, the CPipe
				over such a buffer, and CShadowRingBuffer, the form whose
				bytes live in a shared-memory object.

				The buffer holds fSize bytes and wastes one slot, so it can
				tell full from empty by the pointers alone: fGet == fPut is
				empty, and fPut one slot behind fGet (with wrap) is full - so
				a buffer made for n bytes (Init(n)) holds n (GetSize()).
				Bytes go in at fPut (Put/Putn/CopyIn) and come out at fGet
				(Get/Getn/CopyOut/Peek/Next/Skip); the pointers wrap at
				fBufEnd back to fBufStart.  The Compute*Vectors calls give the
				up-to-two contiguous runs a bulk copy crosses the wrap in, and
				Update*Vector advances the pointers over a run.

	The DDK has no header for these; reconstructed from the MP2x00 US ROM
	(0x001acb78-0x001adbe8 and 0x001deec0-0x001dfa68), each function citing
	its origin.  The ROM
	dispatches the primitives through the vtable (so Put asks the virtual
	IsFull, and so on); the reconstruction keeps that, and declares the
	virtuals in the ROM's vtable order (analysis/vtable.py on the CRingBuffer
	vtable) - note that Init is *not* virtual there, and that CopyIn(CPipe*)
	and GetnAt are CRingBuffer's own additions past the end of the base's
	twenty.
*/

#ifndef __RINGBUFFER_H
#define __RINGBUFFER_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

#ifndef __PIPES_H
#include "Pipes.h"
#endif

#ifndef __USERSHAREDMEM_H
#include "UserSharedMem.h"
#endif


// The interface CRingPipe / CTaskPipe hold a pointer to.  The ROM's is a
// TUObject subclass with only a constructor and destructor of its own; the
// primitives are pure virtual and answered by CRingBuffer.
class CBaseRingBuffer
{
public:
					CBaseRingBuffer();				// ROM 0x0003b348 __ct__15CBaseRingBufferFv
	virtual			~CBaseRingBuffer();				// ROM 0x0003b388 __dt__15CBaseRingBufferFv

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
					CRingBuffer();					// ROM 0x001acb78 __ct__11CRingBufferFv
	virtual			~CRingBuffer();					// ROM 0x001acbe0 __dt__11CRingBufferFv

	// not virtual in the ROM: the other ring buffers' Init calls take other arguments
	NewtonErr		Init(long size);				// ROM 0x001ad0f0 Init__11CRingBufferFl - allocate a buffer of size bytes
	NewtonErr		Init(void* buffer, long size, UChar ownsIt, long getOffset, long putOffset);	// ROM 0x001ad5c0 Init__11CRingBufferFPvlUcN22 - over an existing buffer

	virtual int			Peek();						// ROM 0x001ad624 Peek__11CRingBufferFv
	virtual int			Next();						// ROM 0x001ad63c Next__11CRingBufferFv
	virtual NewtonErr	Skip();						// ROM 0x001ad67c Skip__11CRingBufferFv
	virtual int			Get();						// ROM 0x001ad6bc Get__11CRingBufferFv
	virtual int			Getn(UByte* data, long count);				// ROM 0x001ad704 Getn__11CRingBufferFPUcl
	virtual NewtonErr	CopyOut(UByte* data, long& count);			// ROM 0x001ad734 CopyOut__11CRingBufferFPUcRl
	virtual int			Put(int byte);				// ROM 0x001acc40 Put__11CRingBufferFi
	virtual int			Putn(const UByte* data, long count);		// ROM 0x001acc94 Putn__11CRingBufferFPCUcl
	virtual NewtonErr	CopyIn(const UByte* data, long& count);		// ROM 0x001accc4 CopyIn__11CRingBufferFPCUcRl
	virtual void		Reset();					// ROM 0x001ad018 Reset__11CRingBufferFv
	virtual long		GetSize() const;			// ROM 0x001ad028 GetSize__11CRingBufferCFv
	virtual Boolean		AtEOF() const;				// ROM 0x001ad07c AtEOF__11CRingBufferCFv
	virtual Boolean		IsFull() const;				// ROM 0x001ad034 IsFull__11CRingBufferCFv
	virtual Boolean		IsEmpty() const;			// ROM 0x001ad060 IsEmpty__11CRingBufferCFv
	virtual long		FreeCount() const;			// ROM 0x001ad0cc FreeCount__11CRingBufferCFv
	virtual long		DataCount() const;			// ROM 0x001ad158 DataCount__11CRingBufferCFv
	virtual long		UpdatePutVector(long count);	// ROM 0x001ad3e4 UpdatePutVector__11CRingBufferFl
	virtual long		UpdateGetVector(long count);	// ROM 0x001ad318 UpdateGetVector__11CRingBufferFl
	virtual void		ComputePutVectors(UByte*& p1, long& n1, UByte*& p2, long& n2) const;	// ROM 0x001ad178 ComputePutVectors__11CRingBufferCFRPUcRlT1T2
	virtual void		ComputeGetVectors(UByte*& p1, long& n1, UByte*& p2, long& n2) const;	// ROM 0x001ad244 ComputeGetVectors__11CRingBufferCFRPUcRlT1T2

	// CRingBuffer's own virtuals, past the base's twenty (+0x54, +0x58)
	virtual NewtonErr	CopyIn(CPipe* pipe, long& count);			// ROM 0x001ace00 CopyIn__11CRingBufferFP5CPipeRl
	virtual int			GetnAt(long offset, UByte* data, long count);	// ROM 0x001ad540 GetnAt__11CRingBufferFlPUcT1

	// Hand the buffer's memory out as a shared-memory object, so that another
	// task can read or write it (through a CShadowRingBuffer of its own, say).
	// kSMemNoSizeChangeOnCopyTo is added to the permissions asked for, because
	// a ring buffer writes at a lower offset than the last write every time it
	// wraps and the block's size in use must not follow it down.
	void			MakeShared(ULong permissions);	// ROM 0x001ad4b0 MakeShared__11CRingBufferFUl
	NewtonErr		UnShare();						// ROM 0x001ad508 UnShare__11CRingBufferFv

	UByte*			fBufStart;			// +0x04
	UByte*			fBufEnd;			// +0x08  fBufStart + fSize
	long			fSize;				// +0x0c  the buffer's byte count (usable capacity + 1)
	UByte*			fPut;				// +0x10  where the next Put writes
	UByte*			fGet;				// +0x14  where the next Get reads
	TUSharedMem		fSharedMem;			// +0x18  the buffer handed out by MakeShared
	Boolean			fIsShared;			// +0x20  MakeShared succeeded and UnShare has not run
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
					CRingPipe();					// ROM 0x001ad870 __ct__9CRingPipeFv
	virtual			~CRingPipe();					// ROM 0x001ad8bc __dt__9CRingPipeFv

	void			Init(long size);				// ROM 0x001ad924 Init__9CRingPipeFl - a CRingBuffer of size bytes, owned
	void			Init(CBaseRingBuffer* buffer, UChar ownsIt);	// ROM 0x001ad99c Init__9CRingPipeFP15CBaseRingBufferUc

	virtual long	ReadSeek(long offset, int mode);	// ROM 0x001adbd0 ReadSeek__9CRingPipeFli
	virtual long	ReadPosition(void) const;			// ROM 0x001adbe0 ReadPosition__9CRingPipeCFv
	virtual long	WriteSeek(long offset, int mode);	// ROM 0x001adbd8 WriteSeek__9CRingPipeFli
	virtual long	WritePosition(void) const;			// ROM 0x001ad91c WritePosition__9CRingPipeCFv
	virtual void	ReadChunk(void* data, long& count, Boolean& eof);		// ROM 0x001ad9cc ReadChunk__9CRingPipeFPvRlRUc
	virtual void	WriteChunk(const void* data, long count, Boolean flush);	// ROM 0x001adae4 WriteChunk__9CRingPipeFPvlUc
	virtual void	Reset(void);						// ROM 0x001ad9b0 Reset__9CRingPipeFv

	CBaseRingBuffer*	fBuffer;		// +0x04
	Boolean				fOwnsBuffer;	// +0x08  disposed of with the pipe
	Boolean				fReadHitEOF;	// +0x09  the buffer ran out during a read
};


/*------------------------------------------------------------------------------
	C S h a d o w R i n g B u f f e r
	The same ring buffer, but over a shared-memory object belonging to another
	task: there is no address to write to here, so every byte goes through
	TUSharedMem::CopyToShared / CopyFromShared, and what the buffer keeps are
	offsets into that block rather than pointers.  The base class's virtuals
	hand their vectors back through a UByte*&, so the ROM - and this
	reconstruction with it - puts those offsets through the pointer type; the
	third, untyped pointer is ComputeTempGetVectors, which the class declares
	for itself and so could type honestly.

	The extra pointer is what the "shadow" means: besides fGetOffset, which
	says what has been consumed, there is fTempGetOffset, a speculative read
	position.  TempGetn/TempCopyOut read ahead from it without consuming, and
	TempReset puts it back to fGetOffset - so a reader can look at what is
	coming (a part's header, say) and then decide.  CPartPipe streams a
	package's part through one.

	The class adds no virtuals of its own: everything past the base's twenty
	is an ordinary member.

	The shared-memory object must be made with kSMemNoSizeChangeOnCopyTo
	(SharedTypes.h): a CopyToShared otherwise sets the block's size in use to
	what it has just written (offset + size), and a ring buffer writes at a
	lower offset than the last one every time it wraps - so the block would
	shrink under the buffer and a read past the new end would answer nothing.
------------------------------------------------------------------------------*/

class CShadowRingBuffer : public CBaseRingBuffer
{
public:
					CShadowRingBuffer();			// ROM 0x001deec0 __ct__17CShadowRingBufferFv
	virtual			~CShadowRingBuffer();			// ROM 0x001def1c __dt__17CShadowRingBufferFv

	void			Init(TObjectId sharedMem, long getOffset, long dataCount);	// ROM 0x001df450 Init__17CShadowRingBufferFUllT2

	virtual int			Peek();						// ROM 0x001df948 Peek__17CShadowRingBufferFv
	virtual int			Next();						// ROM 0x001df960 Next__17CShadowRingBufferFv
	virtual NewtonErr	Skip();						// ROM 0x001df99c Skip__17CShadowRingBufferFv
	virtual int			Get();						// ROM 0x001df9dc Get__17CShadowRingBufferFv
	virtual int			Getn(UByte* data, long count);				// ROM 0x001dfa24 Getn__17CShadowRingBufferFPUcl
	virtual NewtonErr	CopyOut(UByte* data, long& count);			// ROM 0x001def68 CopyOut__17CShadowRingBufferFPUcRl
	virtual int			Put(int byte);				// ROM 0x001df21c Put__17CShadowRingBufferFi
	virtual int			Putn(const UByte* data, long count);			// ROM 0x001df27c Putn__17CShadowRingBufferFPCUcl
	virtual NewtonErr	CopyIn(const UByte* data, long& count);		// ROM 0x001df2ac CopyIn__17CShadowRingBufferFPCUcRl
	virtual void		Reset();					// ROM 0x001df3e4 Reset__17CShadowRingBufferFv
	virtual long		GetSize() const;			// ROM 0x001df3f4 GetSize__17CShadowRingBufferCFv
	virtual Boolean		AtEOF() const;				// ROM 0x001df400 AtEOF__17CShadowRingBufferCFv
	virtual Boolean		IsFull() const;				// ROM 0x001df884 IsFull__17CShadowRingBufferCFv
	virtual Boolean		IsEmpty() const;			// ROM 0x001df8ac IsEmpty__17CShadowRingBufferCFv
	virtual long		FreeCount() const;			// ROM 0x001df498 FreeCount__17CShadowRingBufferCFv
	virtual long		DataCount() const;			// ROM 0x001df4b8 DataCount__17CShadowRingBufferCFv
	virtual long		UpdatePutVector(long count);	// ROM 0x001df7d4 UpdatePutVector__17CShadowRingBufferFl
	virtual long		UpdateGetVector(long count);	// ROM 0x001df724 UpdateGetVector__17CShadowRingBufferFl
	virtual void		ComputePutVectors(UByte*& p1, long& n1, UByte*& p2, long& n2) const;	// ROM 0x001df4f8 ComputePutVectors__17CShadowRingBufferCFRPUcRlT1T2
	virtual void		ComputeGetVectors(UByte*& p1, long& n1, UByte*& p2, long& n2) const;	// ROM 0x001df668 ComputeGetVectors__17CShadowRingBufferCFRPUcRlT1T2

	// the class's own members - it adds nothing to the vtable
	UByte			GetByteAt(long offset);				// ROM 0x001df8c4 GetByteAt__17CShadowRingBufferFl
	int				PutByteAt(int byte, long offset);	// ROM 0x001df904 PutByteAt__17CShadowRingBufferFil
	int				TempGetn(UByte* data, long count);	// ROM 0x001df0b0 TempGetn__17CShadowRingBufferFPUcl
	NewtonErr		TempCopyOut(UByte* data, long& count);	// ROM 0x001df0d8 TempCopyOut__17CShadowRingBufferFPUcRl
	void			TempReset();						// ROM 0x001df210 TempReset__17CShadowRingBufferFv
	long			TempDataCount() const;				// ROM 0x001df4d8 TempDataCount__17CShadowRingBufferCFv
	void			ComputeTempGetVectors(ULong& o1, long& n1, ULong& o2, long& n2) const;	// ROM 0x001df5ac ComputeTempGetVectors__17CShadowRingBufferCFRUlRlT1T2

	ULong			fPutOffset;			// +0x04  where the next Put writes
	ULong			fGetOffset;			// +0x08  where the next Get reads
	ULong			fTempGetOffset;		// +0x0c  the speculative read position
	ULong			fSize;				// +0x10  the shared memory's size, the wrap point
	TUSharedMem		fSharedMem;		// +0x14  the block the bytes live in
};

#endif	/* __RINGBUFFER_H */
