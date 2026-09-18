/*
	File:		utility/Pipes.h

	Contains:	CPipe, the byte stream the object streamer (NSOF), the
				package loader and the communications code read and write:
				chunks in and out, seeking, and the << and >> operators for
				the scalar types (big-endian, as in the ROM's memory); a
				pipe that runs dry asks its Underflow, one that fills up its
				Overflow.  CBufferPipe is a pipe over two CBufferSegments (one
				read, one written); CMemoryPipe and MemoryPipe are the pipes
				over memory (the object streamer's and the frames part
				handler's); PipeCallBack is what a pipe user gives to be told
				of progress.

				There is no DDK header for these (the Communications DDK's
				Pipes.h is not in the repository); the layouts are the ROM's:
				CPipe 4 bytes, PipeCallBack 0xc, CBufferPipe 0x10, the
				virtuals in the ROM's vtable order.

	Reconstructed from the MP2x00 US ROM (0x0018a424-0x0018a95c,
	0x00046abc-0x000473f4, 0x002d8588, 0x000cf7e8-0x000d16ec); each
	function cites its origin.
*/

#ifndef __PIPES_H
#define __PIPES_H

#ifndef __BUFFERSEGMENT_H
#include "BufferSegment.h"
#endif

// the seek modes (CBufferSegment::Seek's directions)
enum
{
	kSeekFromBeginningPos = -1,
	kSeekFromCurrentPos = 0,
	kSeekFromEndPos = 1
};


/*------------------------------------------------------------------------------
	P i p e C a l l B a c k
	Told how far a transfer has got (Status); the pipe user's to
	subclass.
------------------------------------------------------------------------------*/

class PipeCallBack
{
public:
					PipeCallBack();
	virtual			~PipeCallBack();

	virtual void	Status(long count) = 0;		// NOT YET RECONSTRUCTED: no ROM subclass is; the fields' meaning is ours to find

	long			fUnknown04;			// +0x04  -1
	long			fUnknown08;			// +0x08  -1
};


/*------------------------------------------------------------------------------
	C P i p e
------------------------------------------------------------------------------*/

class CPipe
{
public:
					CPipe();
	virtual			~CPipe();

	virtual long	ReadSeek(long offset, int mode) = 0;
	virtual long	ReadPosition(void) const = 0;
	virtual long	WriteSeek(long offset, int mode) = 0;
	virtual long	WritePosition(void) const = 0;
	virtual void	ReadChunk(void* data, long& count, Boolean& eof) = 0;		// count comes back as what was read
	virtual void	WriteChunk(const void* data, long count, Boolean flush) = 0;
	virtual void	FlushRead(void) = 0;
	virtual void	FlushWrite(void) = 0;
	virtual void	Reset(void) = 0;
	virtual void	ResetRead(void);			// the base ones throw eNotImplemented
	virtual void	ResetWrite(void);
	virtual void	Overflow(void) = 0;								// the write buffer is full
	virtual void	Underflow(long count, Boolean& eof) = 0;		// the read buffer is empty; count more bytes wanted

	// scalars, big-endian
	CPipe&			operator>>(char& c);
	CPipe&			operator>>(signed char& c);
	CPipe&			operator>>(unsigned char& c);
	CPipe&			operator>>(short& s);
	CPipe&			operator>>(unsigned short& s);
	CPipe&			operator>>(long& l);
	CPipe&			operator>>(unsigned long& l);
	CPipe&			operator<<(char c);
	CPipe&			operator<<(signed char c);
	CPipe&			operator<<(unsigned char c);
	CPipe&			operator<<(short s);
	CPipe&			operator<<(unsigned short s);
	CPipe&			operator<<(long l);
	CPipe&			operator<<(unsigned long l);
};


/*------------------------------------------------------------------------------
	C B u f f e r P i p e
	A read segment and a write segment (either may be absent: the
	operations on it throw eNotInitialized); Overflow, Underflow, FlushRead
	and FlushWrite are the subclass's (this class is abstract in the ROM
	too).
------------------------------------------------------------------------------*/

class CBufferPipe : public CPipe
{
public:
					CBufferPipe();
	virtual			~CBufferPipe();

	void			Init(long readSize, long writeSize);		// new segments of these sizes (0: none)
	void			Init(CBufferSegment* readBuffer, CBufferSegment* writeBuffer, Boolean ownsBuffers);

	virtual long	ReadSeek(long offset, int mode);
	virtual long	ReadPosition(void) const;
	virtual long	WriteSeek(long offset, int mode);
	virtual long	WritePosition(void) const;
	virtual void	ReadChunk(void* data, long& count, Boolean& eof);
	virtual void	WriteChunk(const void* data, long count, Boolean flush);
	virtual void	Reset(void);
	virtual void	ResetRead(void);
	virtual void	ResetWrite(void);

	// the read buffer, byte by byte
	int				Peek(Boolean flag);
	int				Next(void);
	int				Skip(void);
	int				Get(void);
	int				Put(int dataByte);

	CBufferSegment*	fReadBuffer;		// +0x04
	CBufferSegment*	fWriteBuffer;		// +0x08
	Boolean			fOwnsBuffers;		// +0x0c  deleted with the pipe
	Boolean			fReadHitEOF;		// +0x0d  Underflow said the source is exhausted
};


/*------------------------------------------------------------------------------
	C M e m o r y P i p e
	A buffer pipe over memory that is all there is: running out of data is
	the end, running out of room an error.
------------------------------------------------------------------------------*/

class CMemoryPipe : public CBufferPipe
{
public:
	virtual void	FlushRead(void);
	virtual void	FlushWrite(void);
	virtual void	Overflow(void);
	virtual void	Underflow(long count, Boolean& eof);
};


/*------------------------------------------------------------------------------
	M e m o r y P i p e
	The frames part handler's pipe over a part in memory: the write
	segment is reused when full, reading past the end waits for nothing.
------------------------------------------------------------------------------*/

class MemoryPipe : public CBufferPipe
{
public:
					MemoryPipe();
	virtual			~MemoryPipe();

	virtual void	FlushRead(void);
	virtual void	FlushWrite(void);
	virtual void	Reset(void);
	virtual void	Overflow(void);
	virtual void	Underflow(long count, Boolean& eof);
};

#endif	/* __PIPES_H */
