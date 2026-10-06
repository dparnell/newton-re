/*
	File:		packages/PackagePipe.cpp

	Contains:	CPackagePipe (PackagePipe.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "PackagePipe.h"
#include "PackageIterator.h"
#include "NewtonExceptions.h"
#include "UCErrors.h"
#include "OSErrors.h"
#include "host/RomBugs.h"

#include <string.h>

extern const ExceptionName exPipeException;


// ROM 0x0015fe48 __ct__12CPackagePipeFv
CPackagePipe::CPackagePipe()
{
	fDirectory = nil;
	fIter = nil;
	fPosition = 0;
	fSize = 0;
	fPipe = nil;
}


// ROM 0x0015fea0 __dt__12CPackagePipeFv
CPackagePipe::~CPackagePipe()
{
	if (fIter != nil)
		delete fIter;
	operator delete(fDirectory);
}


// ROM 0x0015fff4 Init__12CPackagePipeFP5CPipe
// The package's directory read from the pipe and copied: the header, the
// part entries, the directory's data and - when there is a relocation
// chunk - its header.  Throws evt.ex.pipe with kError_No_Memory, or with
// the iterator's error for a bad package.
// ROM QUIRK: only the relocation chunk's 0x14-byte header is copied, not
// its entries, so a read of a package with one does not give back the
// bytes the pipe held there.
void
CPackagePipe::Init(CPipe* pipe)
{
	fPipe = pipe;
	fIter = new TPackageIterator(pipe);
	if (fIter == nil)
		Throw(exPipeException, (void*) kError_No_Memory, nil);
	NewtonErr err = fIter->Init();
	if (err != noErr)
		Throw(exPipeException, (void*) (Long) err, nil);
	fDirectory = (UByte*) operator new(fIter->DirectorySize() + (fIter->fRelocationInfo != nil ? kRelocationHeaderSize : 0));
	if (fDirectory == nil)
		Throw(exPipeException, (void*) kError_No_Memory, nil);
	memcpy(fDirectory, fIter->fDirectory, kPackageDirectorySize);
	ULong numParts = fIter->NumberOfParts();
	memcpy(fDirectory + kPackageDirectorySize, fIter->fParts, numParts * kPartEntrySize);
	ULong at = numParts * kPartEntrySize + kPackageDirectorySize;
	memcpy(fDirectory + at, fIter->fDirectoryData, fIter->DirectorySize() - at);
	long size = fIter->DirectorySize();
	if (fIter->fRelocationInfo != nil)
	{
		memcpy(fDirectory + size, fIter->fRelocationInfo, kRelocationHeaderSize);
		size += kRelocationHeaderSize;
	}
	fSize = size;
}


// ROM 0x0015fef8 ReadSeek__12CPackagePipeFli
long
CPackagePipe::ReadSeek(long /*offset*/, int /*mode*/)
{
	return 0;
}


// ROM 0x0015ff00 ReadPosition__12CPackagePipeCFv
long
CPackagePipe::ReadPosition(void) const
{
	return 0;
}


// ROM 0x0015ff08 WriteSeek__12CPackagePipeFli
long
CPackagePipe::WriteSeek(long /*offset*/, int /*mode*/)
{
	return 0;
}


// ROM 0x0015ff10 WritePosition__12CPackagePipeCFv
long
CPackagePipe::WritePosition(void) const
{
	return 0;
}


// ROM 0x0015ff18 ReadChunk__12CPackagePipeFPvRlRUc
// What is left of the directory's copy first, then the pipe.  Throws
// evt.ex.pipe with kError_Bad_Parameters when more is wanted than the copy
// has and there is no pipe.
// ROM BUG (fixed): count comes back as what the pipe gave, not counting
// the bytes that came from the copy - a read served wholly from the copy
// says it read nothing.  The fix: count is the bytes from the copy and
// the pipe together (and a read served wholly from the copy is not at the
// end of the pipe, so eof is false).
void
CPackagePipe::ReadChunk(void* data, long& count, Boolean& eof)
{
	long available = fSize - fPosition;
	long n = count;
	if (available < count)
		n = available;
	if (n != 0)
	{
		memcpy(data, fDirectory + fPosition, n);
		fPosition += n;
		data = (char*) data + n;
		count -= n;
	}
	if (count == 0)
	{
		if (RomBugFixed())
		{
			count = n;
			eof = false;
		}
		return;
	}
	if (fPipe == nil)
		Throw(exPipeException, (void*) kError_Bad_Parameters, nil);
	fPipe->ReadChunk(data, count, eof);
	if (RomBugFixed())
		count += n;
}


// ROM 0x0015ffd8 WriteChunk__12CPackagePipeFPvlUc
void
CPackagePipe::WriteChunk(const void* /*data*/, long /*count*/, Boolean /*flush*/)
{
	Throw(exPipeException, (void*) eNotImplemented, nil);
}


// ROM 0x00160168 FlushRead__12CPackagePipeFv
void
CPackagePipe::FlushRead(void)
{ }


// ROM 0x0016016c FlushWrite__12CPackagePipeFv
void
CPackagePipe::FlushWrite(void)
{
	Throw(exPipeException, (void*) eNotImplemented, nil);
}


// ROM 0x00160188 Reset__12CPackagePipeFv
// Back to the start of the copy; the pipe reset.
void
CPackagePipe::Reset(void)
{
	fPosition = 0;
	fPipe->Reset();
}


// ROM 0x00160130 Overflow__12CPackagePipeFv
void
CPackagePipe::Overflow(void)
{
	Throw(exPipeException, (void*) eNotImplemented, nil);
}


// ROM 0x0016014c Underflow__12CPackagePipeFlRUc
void
CPackagePipe::Underflow(long /*count*/, Boolean& /*eof*/)
{
	Throw(exPipeException, (void*) eNotImplemented, nil);
}
