/*
	File:		utility/StdioPipe.h

	Contains:	CStdioPipe - a pipe over a stdio FILE: made with a path and
				a mode as fopen takes them, read and written a chunk at a
				time (fread/fwrite), seeking with fseek.  Every failure
				throws exPipeException, the data saying which: -1 the file
				could not be opened, -2 not closed, -3 a read that came
				short at the end (the count and eof are set first), -4 a
				read that failed, -5 a write or flush that failed, -6 a
				seek that failed.  The ROM's is 0xc bytes (a CPipe, the
				FILE and the direction last used: 1 reading, 2 writing - a
				read after writing flushes first).

				It is how store:SuckPackageOffDeskTop reads a package: on
				the MessagePad the C library's stdio goes over the
				debugging link to the desktop ("dev:StdGetFile" asks the
				desktop for a file).

				DEVIATION: fopen and the rest are the host's own C library,
				so the path is a file of the host's ("dev:StdGetFile",
				which only a desktop answers, cannot be opened, and throws
				-1 as an absent file does).

	Reconstructed from the MP2x00 US ROM (0x001f99e0-0x001f9d1c); each
	function cites its origin.
*/

#ifndef __STDIOPIPE_H
#define __STDIOPIPE_H

#ifndef __PIPES_H
#include "Pipes.h"
#endif

#include <stdio.h>

class CStdioPipe : public CPipe
{
public:
					CStdioPipe(const char* path, const char* mode);		// ROM 0x001f99e0 __ct__10CStdioPipeFPcT1
	virtual			~CStdioPipe();										// ROM 0x001f9a64 __dt__10CStdioPipeFv

	virtual long	ReadSeek(long offset, int mode);					// ROM 0x001f9cd0 ReadSeek__10CStdioPipeFli
	virtual long	ReadPosition(void) const;							// ROM 0x001f9d14 ReadPosition__10CStdioPipeCFv
	virtual long	WriteSeek(long offset, int mode);					// ROM 0x001f9aec WriteSeek__10CStdioPipeFli
	virtual long	WritePosition(void) const;							// ROM 0x001f9b30 WritePosition__10CStdioPipeCFv
	virtual void	ReadChunk(void* data, long& count, Boolean& eof);	// ROM 0x001f9b40 ReadChunk__10CStdioPipeFPvRlRUc
	virtual void	WriteChunk(const void* data, long count, Boolean flush);	// ROM 0x001f9bec WriteChunk__10CStdioPipeFPvlUc
	virtual void	FlushRead(void);									// ROM 0x001f9b38 FlushRead__10CStdioPipeFv
	virtual void	FlushWrite(void);									// ROM 0x001f9b3c FlushWrite__10CStdioPipeFv
	virtual void	Reset(void);										// ROM 0x001f9c88 Reset__10CStdioPipeFv
	virtual void	Overflow(void);										// ROM 0x001f9cc8 Overflow__10CStdioPipeFv
	virtual void	Underflow(long count, Boolean& eof);				// ROM 0x001f9ccc Underflow__10CStdioPipeFlRUc

	void			Flush(void);										// ROM 0x001f9c50 Flush__10CStdioPipeFv

	FILE*			fFile;			// +0x04
	long			fDirection;		// +0x08  1 reading, 2 writing (0 neither yet)
};

#endif	/* __STDIOPIPE_H */
