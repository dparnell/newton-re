/*
	File:		utility/StdioPipe.cpp

	Contains:	CStdioPipe - see StdioPipe.h.

	Reconstructed from the MP2x00 US ROM (0x001f99e0-0x001f9d1c); each
	function cites its origin.
*/

#include "StdioPipe.h"
#include "NewtonExceptions.h"
#include <stdint.h>

extern const ExceptionName exPipeException;

static void
ThrowPipe(long error)
{
	Throw(exPipeException, (void*) (intptr_t) error, nil);
}


// ROM 0x001f99e0 __ct__10CStdioPipeFPcT1
// The file opened; -1 thrown when it cannot be.
CStdioPipe::CStdioPipe(const char* path, const char* mode)
{
	fFile = fopen(path, mode);
	if (fFile == nil)
		ThrowPipe(-1);
	fDirection = 0;
}


// ROM 0x001f9a64 __dt__10CStdioPipeFv
// Flushed (-5 thrown when it cannot be) and closed (-2).
CStdioPipe::~CStdioPipe()
{
	if (fflush(fFile) != 0)
		ThrowPipe(-5);
	if (fclose(fFile) != 0)
		ThrowPipe(-2);
}


// ROM 0x001f9cd0 ReadSeek__10CStdioPipeFli
// ==> the position after the seek (-6 thrown when it fails).
long
CStdioPipe::ReadSeek(long offset, int mode)
{
	if (fseek(fFile, offset, mode) != 0)
		ThrowPipe(-6);
	return ftell(fFile);
}


// ROM 0x001f9d14 ReadPosition__10CStdioPipeCFv
long
CStdioPipe::ReadPosition(void) const
{
	return ftell(fFile);
}


// ROM 0x001f9aec WriteSeek__10CStdioPipeFli
// As ReadSeek (the ROM's has ftell inlined).
long
CStdioPipe::WriteSeek(long offset, int mode)
{
	if (fseek(fFile, offset, mode) != 0)
		ThrowPipe(-6);
	return ftell(fFile);
}


// ROM 0x001f9b30 WritePosition__10CStdioPipeCFv
long
CStdioPipe::WritePosition(void) const
{
	return ftell(fFile);
}


// ROM 0x001f9b40 ReadChunk__10CStdioPipeFPvRlRUc
// count bytes read (flushed first if the pipe was last written); eof says
// whether the end has been reached.
// ROM QUIRK kept: a read that comes short at the end sets the count and eof
// and then still throws (-3) - so a caller that asks for more than is left
// gets an exception rather than a short count; one that fails throws -4.
void
CStdioPipe::ReadChunk(void* data, long& count, Boolean& eof)
{
	if (fDirection == 2)
		fflush(fFile);
	fDirection = 1;
	size_t got = fread(data, 1, count, fFile);
	if (got < (size_t) count)
	{
		long error;
		if (ferror(fFile) == 0)
		{
			count = (long) got;
			eof = true;
			error = -3;
		}
		else
			error = -4;
		ThrowPipe(error);
	}
	eof = feof(fFile) != 0;
}


// ROM 0x001f9bec WriteChunk__10CStdioPipeFPvlUc
// count bytes written (-5 thrown when they cannot all be), flushed when
// asked.
void
CStdioPipe::WriteChunk(const void* data, long count, Boolean flush)
{
	fDirection = 2;
	if (fwrite(data, 1, count, fFile) < (size_t) count)
		ThrowPipe(-5);
	if (flush && fflush(fFile) != 0)
		ThrowPipe(-5);
}


// ROM 0x001f9c50 Flush__10CStdioPipeFv
void
CStdioPipe::Flush(void)
{
	if (fflush(fFile) != 0)
		ThrowPipe(-5);
}


// ROM 0x001f9b38 FlushRead__10CStdioPipeFv
void
CStdioPipe::FlushRead(void)
{
	Flush();
}


// ROM 0x001f9b3c FlushWrite__10CStdioPipeFv
void
CStdioPipe::FlushWrite(void)
{
	Flush();
}


// ROM 0x001f9c88 Reset__10CStdioPipeFv
// Back to the beginning (-6 thrown when it cannot be).
void
CStdioPipe::Reset(void)
{
	if (fseek(fFile, 0, SEEK_SET) != 0)
		ThrowPipe(-6);
}


// ROM 0x001f9cc8 Overflow__10CStdioPipeFv
void
CStdioPipe::Overflow(void)
{ }


// ROM 0x001f9ccc Underflow__10CStdioPipeFlRUc
void
CStdioPipe::Underflow(long /*count*/, Boolean& /*eof*/)
{ }
