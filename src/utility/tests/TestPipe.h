/*
	File:		utility/tests/TestPipe.h

	Contains:	CTestPipe, the ROM's CMemoryPipe (Pipes.h) for the host
				tests: what is written to its write segment can be read back
				through its read segment (Rewind), and the write segment grows
				when full (Overflow; the ROM's throws).  Host-only.
*/

#ifndef __TESTPIPE_H
#define __TESTPIPE_H

#include "Pipes.h"

// The memory pipe with a growing write segment.
class CTestPipe : public CMemoryPipe
{
public:
					CTestPipe(long size)		{ Init(0, size); fOverflows = 0; fUnderflows = 0; }

	virtual void	Overflow(void)
	{
		fOverflows++;
		long position = fWriteBuffer->Position();
		fWriteBuffer->SetPhysicalSize(fWriteBuffer->GetPhysicalSize() * 2);
		fWriteBuffer->Seek(position, kSeekFromBeginning);
	}
	virtual void	Underflow(long count, Boolean& eof)	{ fUnderflows++; CMemoryPipe::Underflow(count, eof); }

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

#endif	/* __TESTPIPE_H */
