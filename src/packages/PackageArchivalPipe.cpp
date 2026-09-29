/*
	File:		packages/PackageArchivalPipe.cpp

	Contains:	CPackageArchivalPipe - see PackageArchivalPipe.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "PackageArchivalPipe.h"
#include "BufferSegment.h"
#include "Soups.h"				// Query, SoupAdd
#include "Cursors.h"			// CursorGotoKey
#include "Entries.h"			// EntryChange
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "NewtonExceptions.h"
#include "NewtonMemory.h"		// MemError

extern const ExceptionName exPipeException;

// the pipe's complaint about a buffer it was not made with
#define kPipeNoBuffer	(-10006)


// ROM 0x0010d190 __ct__20CPackageArchivalPipeFv
CPackageArchivalPipe::CPackageArchivalPipe()
{
	fSoup = new RefStruct;
	fKeys = new RefStruct;
	fCursor = new RefStruct;
	fIndex = 0;
}


// ROM 0x0010d204 __dt__20CPackageArchivalPipeFv
// (the buffers are the CBufferPipe's to give back: Init says it owns them)
CPackageArchivalPipe::~CPackageArchivalPipe()
{
	delete fCursor;
	delete fKeys;
	delete fSoup;
}


// ROM 0x0010d25c Reset__20CPackageArchivalPipeFv
// Both buffers emptied and reading started again at the first key.
void
CPackageArchivalPipe::Reset(void)
{
	CBufferPipe::Reset();
	fIndex = 0;
}


// ROM 0x0010d27c Init__20CPackageArchivalPipeFRC6RefVarT1UcT3
// The soup and the list of keys kept; for reading, a cursor over the soup
// (a query of its default index) and a 4K read buffer; for writing, a 4K
// write buffer.  A buffer that cannot be made throws the pipe exception
// with the error, whatever was made given back first.
void
CPackageArchivalPipe::Init(RefArg soup, RefArg keys, Boolean forReading, Boolean forWriting)
{
	CBufferSegment* readBuffer = nil;
	CBufferSegment* writeBuffer = nil;
	NewtonErr err = noErr;
	*fSoup = soup;
	*fKeys = keys;
	fIndex = 0;
	if (forReading)
	{
		RefVar querySpec(AllocateFrame());
		SetFrameSlot(querySpec, RefVar(RSSYMtype), RefVar(RSSYMindex));
		*fCursor = Query(RefVar(*fSoup), querySpec);
		readBuffer = new CBufferSegment;
		if (readBuffer == nil)
			err = MemError();
		else
			err = readBuffer->Init(0x1000);
	}
	if (err == noErr && forWriting)
	{
		writeBuffer = new CBufferSegment;
		if (writeBuffer == nil)
			err = MemError();
		else
			err = writeBuffer->Init(0x1000);
	}
	if (err == noErr)
	{
		CBufferPipe::Init(readBuffer, writeBuffer, true);
		return;
	}
	if (readBuffer != nil)
		delete readBuffer;
	if (writeBuffer != nil)
		delete writeBuffer;
	Throw(exPipeException, (void*) (Long) err, nil);
}


// ROM 0x0010d40c MakeNewPackageChunk__20CPackageArchivalPipeFl
// A new entry in the soup whose PackageEntry slot is a binary (of class
// 'PackageEntry) of size bytes.  ==> the entry.
Ref
CPackageArchivalPipe::MakeNewPackageChunk(long size)
{
	RefVar entry(AllocateFrame());
	RefVar chunk(AllocateBinary(RefVar(RSSYMpackageentry), size));
	SetFrameSlot(entry, RefVar(RSSYMpackageentry), chunk);
	SoupAdd(RefVar(*fSoup), entry);
	return entry;
}


// ROM 0x0010d488 UpdateKeyList__20CPackageArchivalPipeFRC6RefVar
// The entry's unique id added to the list of keys, which is what reads
// the chunks back in order.
void
CPackageArchivalPipe::UpdateKeyList(RefArg entry)
{
	AddArraySlot(RefVar(*fKeys), RefVar(GetFrameSlot(entry, RefVar(RSSYM_uniqueid))));
}


// ROM 0x0010d4dc GetPackageChunk__20CPackageArchivalPipeFPPUcPUl
// The next key's entry found through the cursor and its PackageEntry
// binary answered as a pointer and a length (the key moved on).  The
// pointer is into the object heap: the caller copies from it at once.
void
CPackageArchivalPipe::GetPackageChunk(UByte** data, ULong* size)
{
	RefVar key(GetArraySlot(RefVar(*fKeys), fIndex));
	RefVar entry(CursorGotoKey(RefVar(*fCursor), key));
	RefVar chunk(GetFrameSlot(entry, RefVar(RSSYMpackageentry)));
	fIndex++;
	*data = (UByte*) BinaryData(chunk);
	*size = Length(chunk);
}


// ROM 0x0010d5a0 FlushRead__20CPackageArchivalPipeFv
// Whatever is left in the read buffer skipped.
void
CPackageArchivalPipe::FlushRead(void)
{
	if (fReadBuffer != nil)
		fReadBuffer->Seek(0, kSeekFromEnd);
}


// ROM 0x0010d5bc FlushWrite__20CPackageArchivalPipeFv
void
CPackageArchivalPipe::FlushWrite(void)
{
	Overflow();
}


// ROM 0x0010d5c4 Overflow__20CPackageArchivalPipeFv
// What is in the write buffer made a new chunk entry - copied into its
// binary, the entry changed on the store and its key recorded - and the
// buffer emptied.  Any exception on the way becomes the pipe exception
// with the same data.
//
// ROM QUIRK kept: the handler takes every exception, and one whose data
// is nought is dropped rather than thrown on.
void
CPackageArchivalPipe::Overflow(void)
{
	if (fWriteBuffer == nil)
	{
		Throw(exPipeException, (void*) kPipeNoBuffer, nil);
		return;
	}
	Size size = fWriteBuffer->Position();
	if (size != 0)
	{
		volatile Long err = 0;
		newton_try
		{
			RefVar entry(MakeNewPackageChunk(size));
			RefVar chunk(GetFrameSlot(entry, RefVar(RSSYMpackageentry)));
			fWriteBuffer->Seek(0, kSeekFromBeginning);
			fWriteBuffer->CopyOut((UByte*) BinaryData(chunk), size);
			EntryChange(entry);
			UpdateKeyList(entry);
		}
		newton_catch_all
		{
			err = (Long) _info.exception.data;
		}
		end_try;
		if (err != 0)
			Throw(exPipeException, (void*) err, nil);
	}
	fWriteBuffer->Reset();
}


// ROM 0x0010d778 Underflow__20CPackageArchivalPipeFlRUc
// The read buffer refilled with the next chunk and wound back to its
// start.  Any exception getting the chunk becomes the pipe exception with
// the same data (running off the end of the keys is one).
//
// ROM QUIRKS kept: the end is never said (eof is left alone - a reader
// asks for no more than the package's own size says there is); the
// handler's test is Subexception(name, ""), which every name passes, and
// an exception whose data is nought is dropped, the buffer then filled
// from whatever the chunk pointer and length held.  DEVIATION: on the host
// those start as nil and nought, where the ROM's are whatever was on the
// stack.
void
CPackageArchivalPipe::Underflow(long /*count*/, Boolean& /*eof*/)
{
	if (fReadBuffer == nil)
	{
		Throw(exPipeException, (void*) kPipeNoBuffer, nil);
		return;
	}
	fReadBuffer->Reset();
	UByte* volatile data = nil;
	volatile ULong size = 0;
	volatile Long err = 0;
	newton_try
	{
		UByte* chunkData;
		ULong chunkSize;
		GetPackageChunk(&chunkData, &chunkSize);
		data = chunkData;
		size = chunkSize;
	}
	newton_catch((ExceptionName) "")
	{
		err = (Long) _info.exception.data;
	}
	end_try;
	if (err != 0)
		Throw(exPipeException, (void*) err, nil);
	fReadBuffer->Putn(data, size);
	fReadBuffer->Seek(0, kSeekFromBeginning);
}
