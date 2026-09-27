/*
	File:		packages/PackagePipe.h

	Contains:	CPackagePipe, a read-only pipe over a package in another
				pipe that gives the package back from its first byte after
				its directory has been read: Init reads the directory (a
				TPackageIterator over the pipe) and keeps a copy of it -
				the header, the part entries, the directory data and the
				relocation chunk's header - and a read is served from the
				copy until it runs out and from the pipe after that.  It
				cannot be written, and Overflow, Underflow and FlushWrite
				throw eNotImplemented.

	The ROM's layout (0x18 bytes, after CPipe's vtable).

	Reconstructed from the MP2x00 US ROM (0x0015fe48-0x0016019c); each
	function cites its origin.
*/

#ifndef __PACKAGEPIPE_H
#define __PACKAGEPIPE_H

#ifndef __PIPES_H
#include "Pipes.h"
#endif

class TPackageIterator;

class CPackagePipe : public CPipe
{
public:
					CPackagePipe();
	virtual			~CPackagePipe();

	void			Init(CPipe* pipe);

	virtual long	ReadSeek(long offset, int mode);
	virtual long	ReadPosition(void) const;
	virtual long	WriteSeek(long offset, int mode);
	virtual long	WritePosition(void) const;
	virtual void	ReadChunk(void* data, long& count, Boolean& eof);
	virtual void	WriteChunk(const void* data, long count, Boolean flush);
	virtual void	FlushRead(void);
	virtual void	FlushWrite(void);
	virtual void	Reset(void);
	virtual void	Overflow(void);
	virtual void	Underflow(long count, Boolean& eof);

	CPipe*				fPipe;			// +0x04
	TPackageIterator*	fIter;			// +0x08
	UByte*				fDirectory;		// +0x0c  the copy
	long				fPosition;		// +0x10  in the copy
	long				fSize;			// +0x14  of the copy
};

#endif	/* __PACKAGEPIPE_H */
