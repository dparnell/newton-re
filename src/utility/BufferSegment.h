/*
	File:		utility/BufferSegment.h

	Contains:	The buffer classes the pipes and the communications code
				read and write through: CMinBuffer (the byte interface: peek,
				get, put, in bulk or one at a time), CBuffer (position, size,
				seeking and hiding), and CBufferSegment, a buffer over one
				block of memory (its own, or given) that can be shared with
				another task (TUSharedMem).  CBufferList (BufferList.h) is a
				list of segments read as one.

				The DDK's BufferSegment.h is the external interface only
				(private constructor, no virtuals: "to prevent external code
				from knowing the size of the object"); this header replaces
				it (sync_ddk_headers.py, REPLACED) with the ROM's classes:
				CMinBuffer 4 bytes, CBuffer 4, CBufferSegment 0x28, the
				virtuals in the ROM's vtable order.

	Reconstructed from the MP2100 D ROM (0x0004644c, 0x00047cc4-
	0x000483a4, 0x00120d3c); each function cites its origin.
*/

#ifndef __BUFFERSEGMENT_H
#define __BUFFERSEGMENT_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif
#ifndef __USERSHAREDMEM_H
#include "UserSharedMem.h"
#endif

// Seek and Hide directions
enum
{
	kSeekFromBeginning = -1,
	kSeekFromHere = 0,
	kSeekFromEnd = 1
};


/*------------------------------------------------------------------------------
	C M i n B u f f e r
	Bytes in and out; the get primitives answer -1 at the end of the data,
	CopyIn/CopyOut move as much as fits, leaving in n what did not, and
	answer -1 when the buffer is then exhausted.
------------------------------------------------------------------------------*/

class CMinBuffer
{
public:
					CMinBuffer();
	virtual			~CMinBuffer();

	// get primitives
	virtual int		Peek(void) = 0;
	virtual int		Next(void) = 0;
	virtual int		Skip(void) = 0;
	virtual int		Get(void) = 0;
	virtual Size	Getn(UByte* p, Size n) = 0;
	virtual int		CopyOut(UByte* p, Size& n) = 0;

	// put primitives
	virtual int		Put(int dataByte) = 0;
	virtual Size	Putn(const UByte* p, Size n) = 0;
	virtual int		CopyIn(const UByte* p, Size& n) = 0;

	// misc
	virtual void	Reset(void) = 0;
	virtual Size	GetSize(void) const = 0;
};


/*------------------------------------------------------------------------------
	C B u f f e r
------------------------------------------------------------------------------*/

class CBuffer : public CMinBuffer
{
public:
					CBuffer();
	virtual			~CBuffer();

	virtual Boolean	AtEOF(void) const = 0;
	virtual Long	Hide(Long count, int dir) = 0;
	virtual Size	Seek(Long off, int dir) = 0;
	virtual Size	Position(void) const = 0;
};


/*------------------------------------------------------------------------------
	C B u f f e r S e g m e n t
	One block of memory: fBuffer..fBufEnd is the block, fBufStart..fBufLimit
	the valid data within it (Hide narrows it from either end), fBufPtr the
	position.
------------------------------------------------------------------------------*/

class CBufferSegment : public CBuffer
{
public:
					CBufferSegment();
	virtual			~CBufferSegment();

	static CBufferSegment*	New(void)		{ return new CBufferSegment; }
	void			Delete(void)			{ delete this; }

	// initialization
	NewtonErr		Init(Size len);												// its own block
	NewtonErr		Init(void* data, Size len, Boolean freeBuffer = false, Size validOff = 0, Long validCount = -1);

	// get primitives
	virtual int		Peek(void);
	virtual int		Next(void);
	virtual int		Skip(void);
	virtual int		Get(void);
	virtual Size	Getn(UByte* p, Size n);
	virtual int		CopyOut(UByte* p, Size& n);

	// put primitives
	virtual int		Put(int dataByte);
	virtual Size	Putn(const UByte* p, Size n);
	virtual int		CopyIn(const UByte* p, Size& n);

	// misc
	virtual void	Reset(void);
	virtual Size	GetSize(void) const;

	// position and size
	virtual Boolean	AtEOF(void) const;
	virtual Long	Hide(Long count, int dir);
	virtual Size	Seek(Long off, int dir);
	virtual Size	Position(void) const;

	// the block
	Size			GetPhysicalSize(void);
	NewtonErr		SetPhysicalSize(Size len);
	NewtonErr		MakeShared(ULong permissions);
	NewtonErr		RestoreShared(ULong permissions);
	NewtonErr		UnShare(void);

	UByte*			fBuffer;			// +0x04  the block
	UByte*			fBufEnd;			// +0x08  its end
	Size			fPhysicalSize;		// +0x0c
	UByte*			fBufStart;			// +0x10  the valid data
	UByte*			fBufPtr;			// +0x14  the position
	UByte*			fBufLimit;			// +0x18  the end of the valid data
	TUSharedMem		fSharedMem;			// +0x1c
	Boolean			fFreeBuffer;		// +0x24  the block is ours to dispose
	Boolean			fIsShared;			// +0x25
};

#endif	/* __BUFFERSEGMENT_H */
