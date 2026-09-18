/*
	File:		stores/StorePipes.cpp

	Contains:	TStoreWritePipe and TStoreReadPipe (StoreObject.h), the
				buffered streams a store object is written and read through,
				with the Unicode text coder for a store object's text.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "StoreObject.h"
#include "UnicodeCompression.h"
#include "OSErrors.h"
#include "NewtonExceptions.h"
#include "NewtonMemory.h"

#include <string.h>

extern TClassInfoRegistry*	gProtocolRegistry;

static const ExceptionName exMessage = (ExceptionName) "evt.ex.msg";

// a callback coder by name through the registry, or (DEVIATION: a
// standalone run of the object system, no registry) from its class info
static TProtocol*
NewCoder(const char* interface, const char* implementation, const TClassInfo* info)
{
	if (gProtocolRegistry != nil)
		return NewByName(interface, implementation);
	return info->New();
}


/* -------------------------------------------------------------------------------
	TStoreWritePipe
------------------------------------------------------------------------------- */

// the compressor's write proc: the pipe is its refcon
// ROM 0x002dcacc CompCallback__FUlPvlUc
static NewtonErr
CompCallback(void* refCon, void* data, ULong size, Boolean isLast)
{
	return ((TStoreWritePipe*) refCon)->CompCallback(data, (long) size, isLast);
}


// ROM 0x002dcc2c __ct__15TStoreWritePipeFv
TStoreWritePipe::TStoreWritePipe()
{
	fData = nil;
	fCompressor = nil;
}


// ROM 0x002dcdfc __dt__15TStoreWritePipeFv
TStoreWritePipe::~TStoreWritePipe()
{
	if (fData != nil && fData != fBuffer)
		delete[] fData;
	if (fCompressor != nil)
		fCompressor->Delete();
}


// ROM 0x002dcc60 Init__15TStoreWritePipeFP13TStoreWrapperUll15CompressionType
// To write size bytes to object id (-1: a new object) of the wrapper's
// store, through the Unicode compressor for kUnicodeCompression.  A
// buffer for the whole object (twice that with a compressor: its input
// and its output) is taken when it can be, and the object assembled in
// it; otherwise the object is written as the small buffer fills, and is
// emptied here first.
void
TStoreWritePipe::Init(TStoreWrapper* wrapper, PSSId id, long size, CompressionType compression)
{
	fWrapper = wrapper;
	fObjectId = id;
	if (compression < kUnicodeCompression)
		fCompressor = nil;
	else
	{
		if (compression == kUnicodeCompression)
		{
			fCompressor = (TCallbackCompressor*) NewCoder("TCallbackCompressor", "TUnicodeCompressor", TUnicodeCompressor::ClassInfo());
			if (fCompressor == nil)
				Throw(exMessage, (void*) "Couldn't create compressor", nil);
			OSErrIf(fCompressor->Init(nil));
		}
		if (fCompressor != nil)
		{
			fCompressor->fWriteProc = ::CompCallback;
			fCompressor->fRefCon = this;
		}
	}
	fObjectSize = size;
	fPosition = 0;
	fBufferCount = 0;
	long wanted = fCompressor != nil ? size * 2 : size;
	if (fData != nil && fData != fBuffer)
		delete[] fData;
	fData = nil;
	if (wanted > kStorePipeBufferSize)
		fData = new char[wanted];
	if (fData != nil)
	{
		fDataSize = size;
		fBuffering = true;
	}
	else
	{
		fData = fBuffer;
		fDataSize = kStorePipeBufferSize;
		fBuffering = wanted <= kStorePipeBufferSize;
	}
	if (fCompressor != nil)
	{
		fCompBuffer = fData + size;
		fCompBufferSize = size;
		fCompCount = 0;
	}
	if (!fBuffering && fObjectId != (PSSId) -1)
	{
		OSErrIf(wrapper->Store()->SetObjectSize(fObjectId, 0));
		OSErrIf(wrapper->Store()->SetObjectSize(fObjectId, fObjectSize));
	}
}


// ROM 0x002dce44 SetPosition__15TStoreWritePipeFl
void
TStoreWritePipe::SetPosition(long position)
{
	fPosition = position;
	if (fBuffering)
		fBufferCount = position;
}


// ROM 0x002dce58 GetDataPtr__15TStoreWritePipeFl
char*
TStoreWritePipe::GetDataPtr(long offset)
{
	return fBuffering ? fData + offset : nil;
}


// ROM 0x002dce94 BufferToObject__15TStoreWritePipeFPcl
// The whole object from a buffer: a new object, or the contents replaced.
void
TStoreWritePipe::BufferToObject(char* data, long size)
{
	if (fObjectId == (PSSId) -1)
		OSErrIf(fWrapper->Store()->NewObject(data, size, &fObjectId));
	else
		OSErrIf(fWrapper->Store()->ReplaceObject(fObjectId, data, size));
}


// ROM 0x002dcad4 CompCallback__15TStoreWritePipeFPvlUc
// The compressor's output: into the output buffer when buffering (the
// object made from it at the end, or at once when it overflows), else
// straight to the object (made or grown as needed).
NewtonErr
TStoreWritePipe::CompCallback(void* data, long count, Boolean isLast)
{
	if (fBuffering)
	{
		if (fCompBufferSize < fCompCount + count)
		{
			// more than expected: what there is becomes the object, the rest is written to it
			BufferToObject(fCompBuffer, fCompCount);
			fBuffering = false;
			fPosition = fCompCount;
			fObjectSize = fCompCount;
			return CompCallback(data, count, isLast);
		}
		BlockMove(data, fCompBuffer + fCompCount, count);
		fCompCount += count;
		if (isLast)
			BufferToObject(fCompBuffer, fCompCount);
		return noErr;
	}
	TStore* store = fWrapper->Store();
	if (fObjectId == (PSSId) -1)
	{
		if (fObjectSize == -1)
			fObjectSize = fPosition + count;
		OSErrIf(store->NewObject(fObjectSize, &fObjectId));
	}
	if (fObjectSize < fPosition + count)
	{
		fObjectSize = fPosition + count;
		OSErrIf(store->SetObjectSize(fObjectId, fObjectSize));
	}
	OSErrIf(store->Write(fObjectId, fPosition, (char*) data, count));
	fPosition += count;
	return noErr;
}


// ROM 0x002dcef0 WriteToStore__15TStoreWritePipeFPcl
// count bytes to the object (or through the compressor).
void
TStoreWritePipe::WriteToStore(char* data, long count)
{
	if (fCompressor != nil)
	{
		OSErrIf(fCompressor->WriteChunk(data, count));
		return;
	}
	if (fBuffering)
		BufferToObject(data, count);
	else
	{
		TStore* store = fWrapper->Store();
		if (fObjectId == (PSSId) -1)
		{
			if (fObjectSize == -1)
				fObjectSize = fPosition + count;
			OSErrIf(store->NewObject(fObjectSize, &fObjectId));
		}
		OSErrIf(store->Write(fObjectId, fPosition, data, count));
	}
	fPosition += count;
}


// ROM 0x002dcfc4 Flush__15TStoreWritePipeFv
void
TStoreWritePipe::Flush(void)
{
	if (fBufferCount == 0)
		return;
	WriteToStore(fData, fBufferCount);
	fBufferCount = 0;
}


// ROM 0x002dcff4 Complete__15TStoreWritePipeFv
// Everything written out; a compressor flushed; an object written
// piecemeal cut to what was written.
void
TStoreWritePipe::Complete(void)
{
	if (fObjectId == (PSSId) -1)
		fObjectSize = -1;
	Flush();
	if (fCompressor != nil)
		OSErrIf(fCompressor->Flush());
	if (!fBuffering)
		OSErrIf(fWrapper->Store()->SetObjectSize(fObjectId, fPosition));
}


// ROM 0x002dd054 Write__15TStoreWritePipeFPcl
void
TStoreWritePipe::Write(char* data, long count)
{
	if (fDataSize - fBufferCount < count)
	{
		Flush();
		if (fDataSize <= count)
		{
			WriteToStore(data, count);
			return;
		}
	}
	BlockMove(data, fData + fBufferCount, count);
	fBufferCount += count;
}


// ROM 0x002dd0d0 __ls__15TStoreWritePipeFUc
TStoreWritePipe&
TStoreWritePipe::operator<<(UByte b)
{
	if (fDataSize < fBufferCount + 1)
		Flush();
	fData[fBufferCount++] = (char) b;
	return *this;
}


// ROM 0x002dd11c __ls__15TStoreWritePipeFl
// A long: 0..254 in one byte, anything else as 0xff and its four bytes.
TStoreWritePipe&
TStoreWritePipe::operator<<(long l)
{
	if (l < 0 || l > 0xfe)
	{
		*this << (UByte) 0xff;
		ULong32 word = (ULong32) l;
		char bytes[4] = { (char) (word >> 24), (char) (word >> 16), (char) (word >> 8), (char) word };
		Write(bytes, 4);
	}
	else
		*this << (UByte) l;
	return *this;
}


/* -------------------------------------------------------------------------------
	TStoreReadPipe
------------------------------------------------------------------------------- */

// the decompressor's read proc: the pipe is its refcon
static NewtonErr
DecompCallback(void* refCon, void* into, long* size, Boolean* underflow)
{
	return ((TStoreReadPipe*) refCon)->DecompCallback(into, size, underflow);
}


// ROM 0x002dd170 NewDecompressor__F15CompressionTypePFUlPvPlPUc_lUl
// A decompressor for the compression (nil for none), reading through readProc.
TCallbackDecompressor*
NewDecompressor(CompressionType compression, DecompressorReadProcPtr readProc, void* refCon)
{
	TCallbackDecompressor* decompressor = nil;
	if (compression == kUnicodeCompression)
	{
		decompressor = (TCallbackDecompressor*) NewCoder("TCallbackDecompressor", "TUnicodeDecompressor", TUnicodeDecompressor::ClassInfo());
		if (decompressor == nil)
			Throw(exMessage, (void*) "Couldn't create decompressor", nil);
		OSErrIf(decompressor->Init(nil));
		decompressor->fReadProc = readProc;
		decompressor->fRefCon = refCon;
	}
	return decompressor;
}


// ROM 0x002dd2d0 __ct__14TStoreReadPipeFP13TStoreWrapper15CompressionType
TStoreReadPipe::TStoreReadPipe(TStoreWrapper* wrapper, CompressionType compression)
{
	fWrapper = wrapper;
	fObjectId = (PSSId) -1;
	fDecompressor = compression == kNoCompression ? nil : NewDecompressor(compression, ::DecompCallback, this);
	fData = fBuffer;
	fObjectOffset = 0;
	fBytesLeft = 0;
	fBufferPos = 0;
	fBufferEnd = 0;
}


// ROM 0x002dd358 __ct__14TStoreReadPipeFPcl
// Over size bytes in memory.
TStoreReadPipe::TStoreReadPipe(char* data, long size)
{
	fWrapper = nil;
	fObjectId = (PSSId) -1;
	fData = data;
	fDecompressor = nil;
	fObjectOffset = size;
	fBufferPos = 0;
	fBytesLeft = size;
	fBufferEnd = size;
}


// ROM 0x002dd3b0 __dt__14TStoreReadPipeFv
TStoreReadPipe::~TStoreReadPipe()
{
	if (fDecompressor != nil)
		fDecompressor->Delete();
}


// ROM 0x002dd3e4 SetPSSID__14TStoreReadPipeFUl
// The object to read, from its start.
void
TStoreReadPipe::SetPSSID(PSSId id)
{
	fObjectId = id;
	OSErrIf(fWrapper->Store()->GetObjectSize(id, &fBytesLeft));
}


// ROM 0x002dd414 SetPosition__14TStoreReadPipeFl
void
TStoreReadPipe::SetPosition(long position)
{
	fBytesLeft += fObjectOffset - position;
	fObjectOffset = position;
	fBufferPos = 0;
	fBufferEnd = 0;
}


// ROM 0x002dd230 DecompCallback__14TStoreReadPipeFPvPlPUc
// The decompressor's input: the next *count bytes of the object (fewer at
// the end, with underflow set).
NewtonErr
TStoreReadPipe::DecompCallback(void* data, long* count, Boolean* underflow)
{
	if (*count < fBytesLeft)
		*underflow = false;
	else
	{
		*count = fBytesLeft;
		*underflow = true;
	}
	OSErrIf(fWrapper->Store()->Read(fObjectId, fObjectOffset, (char*) data, *count));
	fBytesLeft -= *count;
	fObjectOffset += *count;
	return noErr;
}


// ROM 0x002dd440 ReadFromStore__14TStoreReadPipeFPcl
// Up to count bytes from the object (or the decompressor); ==> how many.
long
TStoreReadPipe::ReadFromStore(char* data, long count)
{
	if (fDecompressor == nil)
	{
		if (count > fBytesLeft)
			count = fBytesLeft;
		OSErrIf(fWrapper->Store()->Read(fObjectId, fObjectOffset, data, count));
		fBytesLeft -= count;
		fObjectOffset += count;
		return count;
	}
	Boolean underflow;
	OSErrIf(fDecompressor->ReadChunk(data, &count, &underflow));
	return count;
}


// ROM 0x002dd4f0 FillBuffer__14TStoreReadPipeFv
void
TStoreReadPipe::FillBuffer(void)
{
	fBufferEnd = ReadFromStore(fData, kStoreReadPipeBufferSize);
	fBufferPos = 0;
}


// ROM 0x002dd51c Read__14TStoreReadPipeFPcl
// count bytes: from the buffer, then a large remainder straight from the
// object, or the buffer refilled for a small one.
void
TStoreReadPipe::Read(char* data, long count)
{
	long buffered = fBufferEnd - fBufferPos;
	if (buffered < count)
	{
		if (buffered > 0)
		{
			BlockMove(fData + fBufferPos, data, buffered);
			data += buffered;
			count -= buffered;
			fBufferPos += buffered;
		}
		if (count >= kStoreReadPipeBufferSize)
		{
			ReadFromStore(data, count);
			return;
		}
		FillBuffer();
	}
	BlockMove(fData + fBufferPos, data, count);
	fBufferPos += count;
}


// ROM 0x002dd5c4 Skip__14TStoreReadPipeFl
void
TStoreReadPipe::Skip(long count)
{
	long buffered = fBufferEnd - fBufferPos;
	if (buffered < count)
	{
		if (buffered > 0)
		{
			count -= buffered;
			fBufferPos += buffered;
		}
		for (; count >= kStoreReadPipeBufferSize; count -= kStoreReadPipeBufferSize)
			FillBuffer();
		FillBuffer();
	}
	fBufferPos += count;
}


// ROM 0x002dd720 SkipUByte__14TStoreReadPipeFv
void
TStoreReadPipe::SkipUByte(void)
{
	if (fBufferEnd != fBufferPos)
		fBufferPos++;
	else
		Skip(1);
}


// ROM 0x002dd634 __rs__14TStoreReadPipeFRUc
TStoreReadPipe&
TStoreReadPipe::operator>>(UByte& b)
{
	if (fBufferEnd == fBufferPos)
		Read((char*) &b, 1);
	else
		b = (UByte) fData[fBufferPos++];
	return *this;
}


// ROM 0x002dd6d0 __rs__14TStoreReadPipeFRl
TStoreReadPipe&
TStoreReadPipe::operator>>(long& l)
{
	UByte b;
	*this >> b;
	if (b == 0xff)
	{
		UByte bytes[4];
		Read((char*) bytes, 4);
		l = (long) (Long32) (((ULong32) bytes[0] << 24) | ((ULong32) bytes[1] << 16) | ((ULong32) bytes[2] << 8) | bytes[3]);
	}
	else
		l = b;
	return *this;
}
