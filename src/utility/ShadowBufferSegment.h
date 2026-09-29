/*
	File:		utility/ShadowBufferSegment.h

	Contains:	CShadowBufferSegment, a buffer over another task's shared-
				memory object: the same start, position and limit as a
				CBufferSegment, but offsets into the object rather than
				addresses, and every byte moved with TUSharedMem::CopyToShared
				and CopyFromShared.  A comm tool reads a client's data through
				one of these when the client sends the data "outside" (as a
				shared-memory object) rather than as a CBufferList in the
				tool's own memory.

	Reconstructed from the MP2x00 US ROM (0x001de9f4-0x001deeb0); each
	function cites its origin.
*/

#ifndef __SHADOWBUFFERSEGMENT_H
#define __SHADOWBUFFERSEGMENT_H

#ifndef __BUFFERSEGMENT_H
#include "BufferSegment.h"
#endif


// 0x1c bytes in the ROM
class CShadowBufferSegment : public CBuffer
{
public:
					CShadowBufferSegment();
	virtual			~CShadowBufferSegment();

	// the object `sharedId`, from byte `validOff` for `validCount` bytes
	// (to the end of the object if validCount is negative)
	NewtonErr		Init(TObjectId sharedId, Long validOff = 0, Long validCount = -1);

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

	int				GetByteAt(Long offset);
	int				PutByteAt(int dataByte, Long offset);

	Long			fStart;				// +0x04  the valid data's start
	Long			fPosition;			// +0x08
	Long			fLimit;				// +0x0c  its end
	ULong			fSize;				// +0x10  the object's size
	TUSharedMem		fSharedMem;			// +0x14
};

#endif	/* __SHADOWBUFFERSEGMENT_H */
