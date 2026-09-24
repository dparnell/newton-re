/*
	File:		text/TXStream.cpp

	Contains:	The text engine's byte streams: TXStream and its two
				subclasses, and the temporary stream factory.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "TXStream.h"
#include "Frames.h"
#include "NewtonMemory.h"
#include "OSErrors.h"


// ROM 0x0c104e8c gTXTempStreamFactory
TXTempStreamFactory*	gTXTempStreamFactory = nil;


#pragma mark -
/*--------------------------------------------------------------------
	TXStream: a position, and three things a subclass supplies.
--------------------------------------------------------------------*/

// ROM 0x00245ec8 __ct__8TXStreamFv
TXStream::TXStream()
{
	fPosition = 0;
}


// ROM 0x00245f04 __dt__8TXStreamFv
TXStream::~TXStream()
{ }


// ROM 0x0024602c GetPosition__8TXStreamCFv
long
TXStream::GetPosition(void) const
{
	return fPosition;
}


// ROM 0x00246034 SetPosition__8TXStreamFl
void
TXStream::SetPosition(long at)
{
	fPosition = at;
}


// ROM 0x0024603c WriteBytes__8TXStreamFPCvl
// The position only moves when the write worked, so a stream that has
// run out is left where it was.
NewtonErr
TXStream::WriteBytes(const void* data, long count)
{
	NewtonErr err = Write(data, count);
	if (err == noErr)
		fPosition += count;
	return err;
}


// ROM 0x00246070 ReadBytes__8TXStreamFPvl
// A read that would run off the end reads what there is, moves the
// position to the end, and says so with kTXErrEndOfStream - which is how
// a reader that does not know how long a thing is finds out.
NewtonErr
TXStream::ReadBytes(void* into, long count)
{
	long size;
	NewtonErr err = GetSize(&size);
	if (err != noErr)
		return err;

	Boolean clamped = false;
	if (fPosition + count > size)
	{
		count = size - fPosition;
		clamped = true;
	}
	err = Read(into, count);
	if (err != noErr)
		return err;
	fPosition += count;
	if (clamped)
		err = kTXErrEndOfStream;
	return err;
}


#pragma mark -
/*--------------------------------------------------------------------
	TXHandleStream: the bytes in a TXArray.
--------------------------------------------------------------------*/

// ROM 0x002460fc __ct__14TXHandleStreamFv
// Thirty bytes at a time, which is the array's chunk - a stream that is
// written a halfword at a time therefore asks the memory manager once
// every fifteen writes rather than every one.
TXHandleStream::TXHandleStream()
{
	fBytes = new TXArray(1, 0x1e);
}


// ROM 0x00246150 __dt__14TXHandleStreamFv
TXHandleStream::~TXHandleStream()
{
	if (fBytes != nil)
		delete fBytes;
}


// ROM 0x002461a8 GetSize__14TXHandleStreamFPl
NewtonErr
TXHandleStream::GetSize(long* size)
{
	*size = fBytes->GetCount();
	return noErr;
}


// ROM 0x00245f1c Write__14TXHandleStreamFPCvl
// What is written past the end lengthens the array; what is written
// inside it overwrites.  Replace does both at once: as many bytes as
// there are left are replaced by all of the new ones.
NewtonErr
TXHandleStream::Write(const void* data, long count)
{
	long size;
	NewtonErr err = GetSize(&size);
	if (err != noErr)
		return err;

	long at = GetPosition();
	long overwritten = size - at;
	if (overwritten >= count)
		overwritten = count;
	return fBytes->Replace(at, overwritten, data, count);
}


// ROM 0x00245f98 Read__14TXHandleStreamFPvl
// ReadBytes has already cut the count down to what is there, so this
// cannot run off the end - and the copy's answer is not looked at.
NewtonErr
TXHandleStream::Read(void* into, long count)
{
	fBytes->CopyTo(GetPosition(), count, into);
	return noErr;
}


#pragma mark -
/*--------------------------------------------------------------------
	TXBinaryStream: the bytes in a NewtonScript binary.
--------------------------------------------------------------------*/

// ROM 0x0023e174 __ct__14TXBinaryStreamFRC6RefVarUciT2
TXBinaryStream::TXBinaryStream(RefArg binary, Boolean atEnd, int extra, Boolean trim)
{
	fBinary = binary;
	// a stream opened to read starts full; one opened to write starts
	// empty, whatever the binary it is writing into already holds
	fSize = atEnd ? Length(binary) : 0;
	fExtra = extra;
	fTrim = trim;
}


// ROM 0x0023e21c __dt__14TXBinaryStreamFv
// The slack the writing left is given back here, if it was asked for.
// (The RefStruct disposes the handle, which is what the ROM's
// DisposeRefHandle does by hand.)
TXBinaryStream::~TXBinaryStream()
{
	if (fTrim && Length(fBinary) != fSize)
		SetLength(fBinary, fSize);
}


// ROM 0x0023e9b4 GetSize__14TXBinaryStreamFPl
// What has been written, not how long the binary is: the binary is
// always at least that and usually more.
NewtonErr
TXBinaryStream::GetSize(long* size)
{
	*size = fSize;
	return noErr;
}


// ROM 0x0023ec80 Write__14TXBinaryStreamFPCvl
NewtonErr
TXBinaryStream::Write(const void* data, long count)
{
	// (the ROM asks GetPosition twice, once for the offset it writes at
	// and once for the test below; both answer the same thing)
	long at = GetPosition();
	long added = 0;
	if (at == fSize)
	{
		// writing at the end: the stream grows, and the binary with it
		added = count;
		long wanted = fSize + count;
		if (Length(fBinary) < wanted)
		{
			// grown by the slack as well, so the next few writes need
			// not grow it again; a store that cannot find the room
			// throws, and the error is the stream's answer
			NewtonErr err = noErr;
			newton_try
			{
				SetLength(fBinary, fExtra + wanted);
			}
			newton_catch_all
			{
				err = GetExceptionErr(&_info.exception);
			}
			end_try;
			if (err != noErr)
				return err;
		}
	}
	// a write inside what is there overwrites it and the size does not move
	BlockMove(data, BinaryData(fBinary) + at, count);
	fSize += added;
	return noErr;
}


// ROM 0x0023f168 Read__14TXBinaryStreamFPvl
NewtonErr
TXBinaryStream::Read(void* into, long count)
{
	char* data = BinaryData(fBinary);
	BlockMove(data + GetPosition(), into, count);
	return noErr;
}


#pragma mark -
/*--------------------------------------------------------------------
	The temporary stream factory.
--------------------------------------------------------------------*/

// ROM 0x00245fd8 __ct__19TXTempStreamFactoryFv
TXTempStreamFactory::TXTempStreamFactory()
{ }


// ROM 0x0024600c TXSetTempStreamFactory__FP19TXTempStreamFactory
void
TXSetTempStreamFactory(TXTempStreamFactory* factory)
{
	gTXTempStreamFactory = factory;
}


// ROM 0x0024601c TXGetTempStreamFactory__Fv
TXTempStreamFactory*
TXGetTempStreamFactory(void)
{
	return gTXTempStreamFactory;
}


// ROM 0x0023efc8 Create__19TXNewtStreamFactoryFPP8TXStreaml
// Somewhere to put `size` bytes.  A small stream lives in the heap; a
// big one is a compressed large binary on the first store, which is what
// lets a document larger than the heap be worked on at all.
NewtonErr
TXNewtStreamFactory::Create(TXStream** stream, long size)
{
	NewtonErr err = noErr;
	newton_try
	{
		if (size < kTXBigStream)
			*stream = new TXHandleStream;
		else
		{
			// NOT YET RECONSTRUCTED: the large-binary arm.  The ROM tells
			// the busy box it is working (BusyBoxSend(0x33)), takes the
			// first of GetStores(), rounds `size` up to a whole kilobyte
			// and adds two more, and asks FLBAllocCompressed for a
			// 'binary of that length on the store with a
			// "TLZStoreCompander" over it; the result is locked and
			// becomes a TXBinaryStream(binary, false, 0x400, false).
			// Large binaries are not reconstructed yet (see
			// qd/Pictures.cpp), so this answers as the ROM's own does
			// when nothing came of it: no memory.
			*stream = nil;
		}
	}
	newton_catch_all
	{
		err = GetExceptionErr(&_info.exception);
	}
	end_try;

	// a throw says what went wrong; nothing at all is out of memory
	if (err == noErr && *stream == nil)
		err = kError_No_Memory;
	return err;
}
