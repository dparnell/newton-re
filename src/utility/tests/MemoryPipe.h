/*
	File:		utility/tests/MemoryPipe.h

	Contains:	CMemoryPipe, a CBufferPipe over memory for the host tests:
				what is written to its write segment can be read back through
				its read segment (Rewind); the write segment grows when full
				(Overflow), the read segment has nothing more when empty
				(Underflow).  Host-only; the ROM's concrete pipes are the
				communications' and the package loader's.
*/

#ifndef __MEMORYPIPE_H
#define __MEMORYPIPE_H

#include "Pipes.h"

// A pipe over memory: what is written to its write segment can be read
// back through its read segment (Rewind); the write segment grows when
// full, the read segment has nothing more when empty.
class CMemoryPipe : public CBufferPipe
{
public:
					CMemoryPipe(long size)		{ Init(0, size); fOverflows = 0; fUnderflows = 0; }

	virtual void	FlushRead(void)				{ }
	virtual void	FlushWrite(void)			{ }
	virtual void	Overflow(void)
	{
		fOverflows++;
		long position = fWriteBuffer->Position();
		fWriteBuffer->SetPhysicalSize(fWriteBuffer->GetPhysicalSize() * 2);
		fWriteBuffer->Seek(position, kSeekFromBeginning);
	}
	virtual void	Underflow(long /*count*/, Boolean& eof)	{ fUnderflows++; eof = true; }

	// what was written becomes what is read
	void			Rewind(void)
	{
		long written = fWriteBuffer->Position();
		if (fReadBuffer == nil)
			fReadBuffer = new CBufferSegment;
		fReadBuffer->Init(fWriteBuffer->fBuffer, written, false);
		fReadHitEOF = false;
	}

	long			fOverflows;
	long			fUnderflows;
};

#endif	/* __MEMORYPIPE_H */
