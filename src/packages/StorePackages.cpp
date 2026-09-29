/*
	File:		packages/StorePackages.cpp

	Contains:	A package kept on a store: the relocation data written with
				each page, the page writer, TPackageIterator::Store, the
				page decompressors and the compander over them,
				TLOPackageStore, AllocatePackage/NewPackage and
				BackupPackage (StorePackages.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "StorePackages.h"
#include "PackageManager.h"		// LoadPackage
#include "Compression.h"
#include "ByteOrder.h"
#include "NewtonMemory.h"
#include "NewtonTime.h"			// RealClock
#include "NewtonExceptions.h"
#include "OSErrors.h"
#include "Unicode.h"			// Ustrcmp
#include "UserTasks.h"			// Reboot
#include "Frames.h"
#include "RSSymbols.h"
#include "Pipes.h"

#include <string.h>
#include <stdio.h>

extern const ExceptionName exPipeException;

namespace
{
inline ULong	Align4(ULong n)		{ return (n + 3) & ~(ULong) 3; }
inline ULong	Align8(ULong n)		{ return (n + 7) & ~(ULong) 7; }

// The page relocated to where it is mapped.  DEVIATION: the host maps a
// package unrelocated (kHostPageNotRelocated), and the ROM's own reader of
// that kind, BackupPackage, passes base 0 too; relocating to any other base
// (RelocateFramesInPage 0x000d1b48/0x000d1b50, which moves the frames'
// pointer refs and resolves their import refs) is NOT YET, so only the C
// relocation is applied then.
void
RelocatePage(FrameRelocationHeader* /*header*/, char* page, ULong base, TCRelocator* relocator)
{
	if (base == kHostPageNotRelocated)
		return;
	if (relocator != nil)
		relocator->Relocate(page, base);
}

// The patch package's name (the ROM's initialised global at 0x0c1016e4,
// pointing at the UniChars at 0x0016108c).
const UniChar kPatchPackageName[] = { 'P', 'a', 't', 'c', 'h', 0 };

// Whether the package's name (big-endian UniChars, as a package keeps them
// on every host) is the patch package's.
Boolean
IsPatchPackageName(const UniChar* bigEndian)
{
	const UByte* bytes = (const UByte*) bigEndian;
	for (long i = 0; ; i++)
	{
		UniChar c = GetBigEndianHalf(bytes + 2 * i);
		if (c != kPatchPackageName[i])
			return false;
		if (c == 0)
			return true;
	}
}
}


/*------------------------------------------------------------------------------
	T C R e l o c a t i o n G e n e r a t o r
------------------------------------------------------------------------------*/

// ROM 0x00049e44 __ct__21TCRelocationGeneratorFv
// (The block header's two bytes after the count and its page size are
// never set here: heap rubbish on the MessagePad, nought on the host.)
TCRelocationGenerator::TCRelocationGenerator()
{
	memset(fBlockHeader, 0, sizeof(fBlockHeader));
	fHeader = nil;
	fEntries = nil;
	PutBigEndianWord(fBlockHeader + 0, 0xffffffff);
	PutBigEndianWord(fBlockHeader + 12, 0);
	fBlockHeader[5] = 0;
	fBlockHeader[4] = 0;
	fEntriesEnd = nil;
}


// ROM 0x00049e88 __dt__21TCRelocationGeneratorFv
TCRelocationGenerator::~TCRelocationGenerator()
{}


// ROM 0x00049e94 Init__21TCRelocationGeneratorFP16RelocationHeaderP15RelocationEntry
// The block header's reserved word, page size and link address taken from
// the package's relocation chunk.
NewtonErr
TCRelocationGenerator::Init(RelocationHeader* header, RelocationEntry* entries)
{
	fHeader = header;
	if (header != nil)
	{
		fEntries = entries;
		memcpy(fBlockHeader + 0, header->fReserved, 4);
		memcpy(fBlockHeader + 12, header->fBaseAddress, 4);
		memcpy(fBlockHeader + 8, header->fPageSize, 4);
		fEntriesEnd = (UByte*) entries + (header->RelocationSize() - kRelocationHeaderSize);
	}
	return noErr;
}


// ROM 0x00049ecc GetRelocDataSizeForBlock__21TCRelocationGeneratorFUl
// The size of the C relocation data in front of a page: 0 with no
// relocation at all, the block header alone when no entry is the page's,
// else the header and the page's offsets padded to four.
// ROM BUG kept: the walk's end test compares the entry pointer plus the
// entries' size with the entry pointer itself, so it never ends the walk:
// the entries are walked until one is for this page or a later one, or
// has no offsets - past the last entry, into whatever follows it.
// DEVIATION: the host stops at the end of the entries (what lies beyond
// them on the MessagePad - heap rubbish for a package read from a pipe -
// cannot be reproduced), answering as for a page with no entry.
long
TCRelocationGenerator::GetRelocDataSizeForBlock(ULong block)
{
	UByte* p = (UByte*) fEntries;
	if (fHeader == nil)
		return 0;
	long size = (long) fHeader->RelocationSize();
	for ( ; ; )
	{
		if (!(size - kRelocationHeaderSize > 0) || p + 4 > fEntriesEnd)
			return kCRelocationBlockHeaderSize;
		ULong padded = Align4(GetBigEndianHalf(p + 2));
		ULong page = GetBigEndianHalf(p);
		if (block <= page)
		{
			if (page != block)
				return kCRelocationBlockHeaderSize;
			return (long) padded + kCRelocationBlockHeaderSize;
		}
		p += padded + 4;
		if (padded == 0)
			return kCRelocationBlockHeaderSize;
	}
}


// ROM 0x00049f60 GetRelocDataForBlock__21TCRelocationGeneratorFUlPPcPlT2
// The block header (with the page's entry count) and the page's offsets
// (nil when it has none).  (The same walk as GetRelocDataSizeForBlock.)
NewtonErr
TCRelocationGenerator::GetRelocDataForBlock(ULong block, char** header, long* headerSize, char** offsets)
{
	UByte* p = (UByte*) fEntries;
	if (fHeader == nil)
	{
		*header = nil;
		*headerSize = 0;
		*offsets = nil;
		return noErr;
	}
	*header = (char*) fBlockHeader;
	*headerSize = kCRelocationBlockHeaderSize;
	long size = (long) fHeader->RelocationSize();
	for ( ; ; )
	{
		if (!(size - kRelocationHeaderSize > 0) || p + 4 > fEntriesEnd)
			break;
		ULong count = GetBigEndianHalf(p + 2);
		ULong padded = Align4(count);
		ULong page = GetBigEndianHalf(p);
		if (block <= page)
		{
			if (page == block)
			{
				*offsets = (char*) (p + 4);
				fBlockHeader[5] = (UByte) count;
				fBlockHeader[4] = (UByte) (count >> 8);
				return noErr;
			}
			break;
		}
		p += padded + 4;
		if (padded == 0)
			break;
	}
	*offsets = nil;
	fBlockHeader[5] = 0;
	fBlockHeader[4] = 0;
	return noErr;
}


/*------------------------------------------------------------------------------
	T S i m p l e C R e l o c a t o r
------------------------------------------------------------------------------*/

// ROM 0x0004a0e8 __ct__17TSimpleCRelocatorFv
TSimpleCRelocator::TSimpleCRelocator()
{
	memset(fBlockHeader, 0, sizeof(fBlockHeader));
	fSize = 0;
	fNext = 0;
	fBlockHeader[5] = 0;
	fBlockHeader[4] = 0;
}


// ROM 0x0004a130 __dt__17TSimpleCRelocatorFv
TSimpleCRelocator::~TSimpleCRelocator()
{}


// ROM 0x0004a03c Init__17TSimpleCRelocatorFP6TStoreUlPl
// The block header and the offsets read from the front of the page
// object; *size: how many bytes they take (where the frame header is).
NewtonErr
TSimpleCRelocator::Init(TStore* store, PSSId pageId, long* size)
{
	NewtonErr err = store->Read(pageId, 0, (char*) fBlockHeader, kCRelocationBlockHeaderSize);
	if (err != noErr)
		return err;
	if (GetBigEndianWord(fBlockHeader) != 0)
		return kError_Bad_Package_Version;
	ULong count = GetBigEndianHalf(fBlockHeader + 4);
	if (count != 0)
	{
		fSize = (long) Align4(count);
		if ((err = store->Read(pageId, kCRelocationBlockHeaderSize, (char*) fOffsets, fSize)) != noErr)
			return err;
	}
	fSize = fSize + kCRelocationBlockHeaderSize;
	*size = fSize;
	return noErr;
}


// ROM 0x0004a148 Relocate__17TSimpleCRelocatorFPcUl
// Each word the offsets name moved by the difference between where the
// page is and where the package was linked.
NewtonErr
TSimpleCRelocator::Relocate(char* page, ULong base)
{
	if (GetBigEndianWord(fBlockHeader) != 0)
		return kError_Bad_Package_Version;
	ULong count = GetBigEndianHalf(fBlockHeader + 4);
	ULong32 linkBase = GetBigEndianWord(fBlockHeader + 12);
	for (ULong i = 0; i < count; i++)
	{
		UByte* word = (UByte*) page + fOffsets[i] * 4;
		PutBigEndianWord(word, (ULong32) ((ULong32) base - linkBase + GetBigEndianWord(word)));
	}
	return noErr;
}


// ROM 0x0004a1b4 GetTheNextRelocEntry__17TSimpleCRelocatorFv
// The next relocated word's byte offset in the page; -1 after the last.
long
TSimpleCRelocator::GetTheNextRelocEntry(void)
{
	if (GetBigEndianWord(fBlockHeader) == 0 && (ULong) fNext < GetBigEndianHalf(fBlockHeader + 4))
		return (long) fOffsets[fNext++] << 2;
	return -1;
}


/*------------------------------------------------------------------------------
	T F r a m e R e l o c a t i o n G e n e r a t o r
------------------------------------------------------------------------------*/

// ROM 0x000d17b0 __ct__25TFrameRelocationGeneratorFv
TFrameRelocationGenerator::TFrameRelocationGenerator()
{
	fAlignment = 0;
	fStarted = false;
	fEnd = 0;
	fFirstObject = -1;
	fHeader = 0;
	fObjectSize = 0;
	fRemaining = 0;
	fSlotted = false;
	fPadded = false;
}


// ROM 0x000d17f4 __ct__25TFrameRelocationGeneratorFi
TFrameRelocationGenerator::TFrameRelocationGenerator(int aligned4)
{
	fAlignment = aligned4 != 0;
	fStarted = false;
	fEnd = 0;
	fFirstObject = -1;
	fHeader = 0;
	fObjectSize = 0;
	fRemaining = 0;
	fSlotted = false;
	fPadded = false;
}


// ROM 0x000d1844 Update__25TFrameRelocationGeneratorFlPcT1Uc
// count bytes of the package put at offsetInPage: when they are a frames
// part's, the objects are walked (a header word's size is the object's,
// aligned to four or eight) so that the page's header can say where the
// first whole one starts and what is left over at the end.  A part's
// first chunk decides the alignment: a slotted object 0x10 bytes long (the
// part's array) followed by a word with bit 0 set means four; a chunk of
// that one word leaves it to the next chunk's first word.
void
TFrameRelocationGenerator::Update(long offsetInPage, char* data, long count, UChar isFrames)
{
	if (!isFrames)
	{
		fRemaining = 0;
		fStarted = false;
		return;
	}
	if (!fStarted)
	{
		fStarted = true;
		fFirstObject = offsetInPage;
		ULong w0 = GetBigEndianWord((const UByte*) data);
		fSlotted = (w0 & 1) != 0;
		ULong size;
		if (fSlotted && (w0 >> 8) == 0x10 && count != 4)
		{
			fAlignment = (GetBigEndianWord((const UByte*) data + 4) & 1) != 0;
			size = fAlignment ? Align4(w0 >> 8) : Align8(w0 >> 8);
		}
		else
		{
			fAlignment = (fSlotted && (w0 >> 8) == 0x10) ? 2 : 1;
			size = Align4(w0 >> 8);
		}
		fRemaining = (long) size;
		fObjectSize = (long) size;
		if (fSlotted)
		{
			ULong aligned = fAlignment == 0 ? Align8(w0 >> 8) : Align4(w0 >> 8);
			fPadded = aligned != (w0 >> 8);
		}
		else
			fPadded = false;
		fHeader = (fHeader & 0xfffff03f) | ((ULong) (fAlignment != 0) << 6);
	}
	else if (fAlignment == 2)
	{
		fAlignment = (GetBigEndianWord((const UByte*) data) & 1) != 0 ? 1 : 0;
		fHeader = (fHeader & ~(ULong) 0x40) | ((ULong) (fAlignment != 0) << 6);
	}
	fEnd = offsetInPage + count;
	long remaining = fRemaining;
	if (count < remaining)
	{
		fRemaining = remaining - count;
		return;
	}
	if (fFirstObject == -1)
		fFirstObject = remaining + offsetInPage;
	char* p = data + remaining;
	long left = count - remaining;
	while (left > 0)
	{
		ULong w = GetBigEndianWord((const UByte*) p);
		fObjectSize = (long) (fAlignment == 0 ? Align8(w >> 8) : Align4(w >> 8));
		if (left < fObjectSize)
			break;
		left -= fObjectSize;
		p += fObjectSize;
	}
	if (left != 0)
	{
		fRemaining = fObjectSize - left;
		ULong w = GetBigEndianWord((const UByte*) p);
		fSlotted = (w & 1) != 0;
		fPadded = fSlotted && (ULong) fObjectSize != (w >> 8);
		return;
	}
	fRemaining = 0;
}


// ROM 0x000d1a50 GetHeader__25TFrameRelocationGeneratorFP21FrameRelocationHeader
// The page's header (big-endian), and the generator started on the next
// page with what is left over of the object that crosses into it.
void
TFrameRelocationGenerator::GetHeader(FrameRelocationHeader* header)
{
	ULong h = fHeader;
	if (fFirstObject == -1)
	{
		fFirstObject = fEnd;
		if (fRemaining > 0)
			h &= ~(ULong) 0x80;
	}
	ULong first = ((ULong) fFirstObject >> 2) << 22;
	h = (h & 0xfff) | first | ((((ULong) fEnd >> 2) & 0x3ff) << 12);
	PutBigEndianWord(header->fWord, (ULong32) h);
	fFirstObject = -1;
	fEnd = 0;
	ULong next = (ULong) (fAlignment != 0) << 6;
	if (fRemaining != 0)
	{
		ULong done = (ULong) (fObjectSize - fRemaining) >> 2;
		if (done > 3)
			done = 3;
		next |= 0x800 | ((done & 3) << 9) | ((ULong) (fSlotted & 1) << 8) | ((ULong) (fPadded & 1) << 7);
	}
	fHeader = next;
}


/*------------------------------------------------------------------------------
	T S t o r e P a c k a g e W r i t e r
------------------------------------------------------------------------------*/

// ROM 0x001fbdf0 __ct__19TStorePackageWriterFv
TStorePackageWriter::TStorePackageWriter()
{
	fCompressor = nil;
	fPage = nil;
	fFrameGenerator = nil;
	fCGenerator = nil;
	fFill = 0;
	fWritten = 0;
	fPageIndex = 0;
	fStore = nil;
	fIndexId = 0;
	fPageId = 0;
	fRelocSize = 0;
}


// ROM 0x001fbe38 __dt__19TStorePackageWriterFv
TStorePackageWriter::~TStorePackageWriter()
{
	delete[] fPage;
	delete fFrameGenerator;
	delete fCGenerator;
}


// ROM 0x001fbe8c callback__FUlPvlUc
NewtonErr
StorePackageWriterCallback(void* writer, void* data, ULong size, Boolean /*isLast*/)
{
	return ((TStorePackageWriter*) writer)->WriteCompressedData(data, (long) size);
}


// ROM 0x001fbe90 Init__19TStorePackageWriterFP6TStoreUlT2P19TCallbackCompressorP16RelocationHeaderP15RelocationEntry
// The compressor's output directed here, the page buffer made, the index
// table sized to the package's pages, the generators made.
NewtonErr
TStorePackageWriter::Init(TStore* store, PSSId indexId, ULong packageSize, TCallbackCompressor* compressor,
						  RelocationHeader* relocationHeader, RelocationEntry* relocationEntries)
{
	fCompressor = compressor;
	fPageIndex = 0;
	if (compressor != nil)
	{
		compressor->fWriteProc = StorePackageWriterCallback;
		compressor->fRefCon = this;
	}
	fStore = store;
	fIndexId = indexId;
	fFill = 0;
	fWritten = 0;
	fPage = new char[kCompanderBlockSize];
	if (fPage == nil)
		return kError_No_Memory;
	NewtonErr err = fStore->SetObjectSize(indexId, (long) (((packageSize + 0x3ff) >> 10) << 2));
	if (err != noErr)
		return err;
	fCGenerator = new TCRelocationGenerator;
	if (fCGenerator == nil)
		return kError_No_Memory;
	if ((err = fCGenerator->Init(relocationHeader, relocationEntries)) != noErr)
		return err;
	fRelocSize = fCGenerator->GetRelocDataSizeForBlock(0);
	fFrameGenerator = new TFrameRelocationGenerator;
	return fFrameGenerator == nil ? kError_No_Memory : noErr;
}


// ROM 0x001fbf78 WriteCompressedData__19TStorePackageWriterFPvl
// Compressed bytes added to the page object, after its relocation data.
NewtonErr
TStorePackageWriter::WriteCompressedData(void* data, long size)
{
	NewtonErr err = fStore->SetObjectSize(fPageId, fRelocSize + fWritten + size + 4);
	if (err == noErr && (err = fStore->Write(fPageId, fRelocSize + fWritten + 4, (char*) data, size)) == noErr)
		fWritten += size;
	return err;
}


// ROM 0x001fbff8 WriteChunk__19TStorePackageWriterFPclUc
// size bytes of the package added: each page, once full, made an object -
// the compressed page, its C relocation data and frame header in front -
// and its id put in the index.  On failure everything written is aborted.
NewtonErr
TStorePackageWriter::WriteChunk(char* data, long size, UChar isFrames)
{
	NewtonErr err = noErr;
	if (size == 0)
		return noErr;
	long left = size;
	do
	{
		long n = kCompanderBlockSize - fFill;
		if (left <= n)
			n = left;
		memmove(fPage + fFill, data + (size - left), n);
		fFrameGenerator->Update(fFill, fPage + fFill, n, isFrames);
		fFill += n;
		if (fFill == kCompanderBlockSize)
		{
			fPageId = 0;
			if ((err = fStore->NewWithinTransaction(fRelocSize + 4, &fPageId)) != noErr)
				break;
			if (fCompressor == nil)
				err = WriteCompressedData(fPage, kCompanderBlockSize);
			else
			{
				fCompressor->Reset();
				if ((err = fCompressor->WriteChunk(fPage, kCompanderBlockSize)) != noErr)
					break;
				err = fCompressor->Flush();
			}
			if (err != noErr)
				break;
			if (fRelocSize != 0)
			{
				char* header;
				long headerSize;
				char* offsets;
				if ((err = fCGenerator->GetRelocDataForBlock(fPageIndex, &header, &headerSize, &offsets)) != noErr
				||  (err = fStore->Write(fPageId, 0, header, headerSize)) != noErr
				||  (offsets != nil && (err = fStore->Write(fPageId, headerSize, offsets, fRelocSize - headerSize)) != noErr))
					break;
			}
			FrameRelocationHeader frameHeader;
			fFrameGenerator->GetHeader(&frameHeader);
			if ((err = fStore->Write(fPageId, fRelocSize, (char*) frameHeader.fWord, 4)) != noErr)
				break;
			UByte word[4];
			PutBigEndianWord(word, (ULong32) fPageId);
			if ((err = fStore->Write(fIndexId, (long) (fPageIndex << 2), (char*) word, 4)) != noErr)
				break;
			fFill = 0;
			fPageIndex++;
			fWritten = 0;
			fRelocSize = fCGenerator->GetRelocDataSizeForBlock(fPageIndex);
		}
		left -= n;
	} while (left != 0);
	if (err != noErr)
	{
		if (fPageId != 0)
			fStore->SeparatelyAbort(fPageId);
		Abort();
	}
	return err;
}


// ROM 0x001fc264 Abort__19TStorePackageWriterFv
// The page objects already in the index aborted.
void
TStorePackageWriter::Abort(void)
{
	for (ULong i = 0; i < fPageIndex; i++)
	{
		UByte word[4];
		if (fStore->Read(fIndexId, (long) (i << 2), (char*) word, 4) != noErr)
			return;
		if (fStore->SeparatelyAbort(GetBigEndianWord(word)) != noErr)
			return;
	}
}


// ROM 0x001fc2dc Flush__19TStorePackageWriterFv
// The last, part-filled, page written as WriteChunk writes a full one.
NewtonErr
TStorePackageWriter::Flush(void)
{
	if (fFill == 0)
		return noErr;
	fPageId = 0;
	NewtonErr err = fStore->NewWithinTransaction(fRelocSize + 4, &fPageId);
	if (err == noErr)
	{
		if (fCompressor == nil)
			err = WriteCompressedData(fPage, fFill);
		else
		{
			fCompressor->Reset();
			err = fCompressor->WriteChunk(fPage, fFill);
			if (err == noErr)
				err = fCompressor->Flush();
		}
		if (err == noErr && fRelocSize != 0)
		{
			char* header;
			long headerSize;
			char* offsets;
			if ((err = fCGenerator->GetRelocDataForBlock(fPageIndex, &header, &headerSize, &offsets)) == noErr
			&&  (err = fStore->Write(fPageId, 0, header, headerSize)) == noErr
			&&  offsets != nil)
				err = fStore->Write(fPageId, headerSize, offsets, fRelocSize - headerSize);
		}
		if (err == noErr)
		{
			FrameRelocationHeader frameHeader;
			fFrameGenerator->GetHeader(&frameHeader);
			UByte word[4];
			PutBigEndianWord(word, (ULong32) fPageId);
			if ((err = fStore->Write(fPageId, fRelocSize, (char*) frameHeader.fWord, 4)) == noErr
			&&  (err = fStore->Write(fIndexId, (long) (fPageIndex << 2), (char*) word, 4)) == noErr)
			{
				fPageIndex++;
				fFill = 0;
				fWritten = 0;
				return noErr;
			}
		}
	}
	if (fPageId != 0)
		fStore->SeparatelyAbort(fPageId);
	Abort();
	return err;
}


/*------------------------------------------------------------------------------
	T P a c k a g e I t e r a t o r : : S t o r e
------------------------------------------------------------------------------*/

// ROM 0x0015c88c Store__16TPackageIteratorFP6TStoreUlP19TCallbackCompressor
NewtonErr
TPackageIterator::Store(TStore* store, PSSId indexId, TCallbackCompressor* compressor)
{
	return Store(store, indexId, compressor, nil);
}


// ROM 0x0015c8b0 Store__16TPackageIteratorFP6TStoreUlP19TCallbackCompressorP11TLOCallback
// The package written to the store's index table a page at a time: the
// directory (its modify date made now), the part entries and the
// directory's data, the relocation chunk, then every part - read from the
// pipe, or copied from memory - frames parts being walked for the page
// headers.
// ROM BUG kept: from memory, every 1K piece of a part is copied from the
// part's start (the source is never advanced), so a part longer than 1K
// is stored as its first 1K over and over.  (A package reaches the store
// through a pipe, which is read properly.)
// Read from a pipe, the progress callback is told each time its frequency
// of bytes has been read: the package's size and name, how many parts, the
// part being read, and how much of the package has come in (counted from
// the directory's and relocation chunk's sizes).  Copied from memory, it is
// never told.
NewtonErr
TPackageIterator::Store(TStore* store, PSSId indexId, TCallbackCompressor* compressor, TLOCallback* callback)
{
	char* buffer = nil;
	TStorePackageWriter writer;
	TLOCallbackInfo progress;
	progress.fAmountRead = 0;
	progress.fCurrentPart = 0;
	progress.fPackageName = PackageName();
	progress.fNumberOfParts = NumberOfParts();
	progress.fPackageSize = PackageSize();
	ULong sinceTold = 0;
	NewtonErr err = GetRelocationChunkInfo();
	if (err == noErr)
		err = writer.Init(store, indexId, PackageSize(), compressor, fRelocationInfo, (RelocationEntry*) fRelocationData);
	if (err != noErr)
		return err;
	buffer = new char[kCompanderBlockSize];
	if (buffer == nil)
		return kError_No_Memory;
	newton_try
	{
		PutBigEndianWord(fDirectory->fModifyDate, (ULong32) RealClock());
	}
	newton_catch("evt.ex")
	{}
	end_try;
	ULong numParts = NumberOfParts();
	if ((err = writer.WriteChunk((char*) fDirectory, kPackageDirectorySize, false)) == noErr
	&&  (err = writer.WriteChunk((char*) fParts, (long) (fDirectory->NumParts() << 5), false)) == noErr
	&&  (err = writer.WriteChunk((char*) fDirectoryData, (long) (fDirectory->DirectorySize() - (fDirectory->NumParts() * 0x20 + kPackageDirectorySize)), false)) == noErr)
	{
		ULong amountRead = fDirectory->DirectorySize();
		if (fRelocationInfo != nil)
		{
			if ((err = writer.WriteChunk((char*) fRelocationInfo, kRelocationHeaderSize, false)) == noErr
			&&  fRelocationData != nil)
				err = writer.WriteChunk((char*) fRelocationData, (long) (fRelocationInfo->RelocationSize() - kRelocationHeaderSize), false);
			amountRead += fRelocationInfo->RelocationSize();
		}
		for (ULong i = 0; err == noErr && i < numParts; i++)
		{
			PartInfo info;
			GetPartInfo(i, &info);
			UChar isFrames = info.kind == kFrames && !info.compressed;
			long left = (long) info.size;
			while (err == noErr && left != 0)
			{
				long n = left > 0x3ff ? kCompanderBlockSize : left;
				volatile NewtonErr readErr = noErr;
				if (!fFromPipe)
				{
					newton_try
					{
						memmove(buffer, (void*) info.data, n);
					}
					newton_catch_all
					{
						writer.Abort();
						readErr = (NewtonErr) (long) (Long) _info.exception.data;
					}
					end_try;
				}
				else
				{
					newton_try
					{
						long count = n;
						Boolean eof;
						fPipe->ReadChunk(buffer, count, eof);
						amountRead += n;
						sinceTold += n;
					}
					newton_catch(exPipeException)
					{
						writer.Abort();
						readErr = (NewtonErr) (long) (Long) _info.exception.data;
					}
					end_try;
					if (readErr == noErr && callback != nil && callback->fFrequency <= sinceTold)
					{
						progress.fCurrentPart = i;
						progress.fAmountRead = amountRead;
						callback->fProc(callback, &progress);
						sinceTold = 0;
					}
				}
				if ((err = readErr) != noErr)
					break;
				err = writer.WriteChunk(buffer, n, isFrames);
				left -= n;
			}
		}
		if (err == noErr)
			err = writer.Flush();
	}
	delete[] buffer;
	return err;
}


/*------------------------------------------------------------------------------
	T h e   d e c o m p r e s s o r s
------------------------------------------------------------------------------*/

PROTOCOL_IMPL_SOURCE_MACRO(TSimpleStoreDecompressor)

// ROM 0x001facf0 New__24TSimpleStoreDecompressorFv
TSimpleStoreDecompressor*
TSimpleStoreDecompressor::New()
{
	fStore = nil;
	return this;
}

// ROM 0x001fae48 Delete__24TSimpleStoreDecompressorFv
void
TSimpleStoreDecompressor::Delete()
{}

// ROM 0x001fae4c Init__24TSimpleStoreDecompressorFP6TStoreUl
NewtonErr
TSimpleStoreDecompressor::Init(TStore* store, ULong /*parameter*/)
{
	fStore = store;
	return noErr;
}

// ROM 0x001fae58 Read__24TSimpleStoreDecompressorFUlPclT1
// The page as it was stored, after its frame header.
NewtonErr
TSimpleStoreDecompressor::Read(ULong pageId, char* buffer, long count, ULong baseAddress)
{
	FrameRelocationHeader header;
	long size;
	NewtonErr err = fStore->Read(pageId, 0, (char*) header.fWord, 4);
	if (err == noErr && (err = fStore->GetObjectSize(pageId, &size)) == noErr)
	{
		if (size - 4 < count)
			count = size - 4;
		if ((err = fStore->Read(pageId, 4, buffer, count)) == noErr)
			RelocatePage(&header, buffer, baseAddress, nil);
	}
	return err;
}


PROTOCOL_IMPL_SOURCE_MACRO(TSimpleRelocStoreDecompressor)

// ROM 0x001f9f0c New__29TSimpleRelocStoreDecompressorFv
TSimpleRelocStoreDecompressor*
TSimpleRelocStoreDecompressor::New()
{
	fStore = nil;
	return this;
}

// ROM 0x001f9f10 Delete__29TSimpleRelocStoreDecompressorFv
void
TSimpleRelocStoreDecompressor::Delete()
{}

// ROM 0x001f9f14 Init__29TSimpleRelocStoreDecompressorFP6TStoreUl
NewtonErr
TSimpleRelocStoreDecompressor::Init(TStore* store, ULong /*parameter*/)
{
	fStore = store;
	return noErr;
}

// ROM 0x001f9f20 Read__29TSimpleRelocStoreDecompressorFUlPclT1
// The C relocation data, then the frame header, then the page (the rest
// of the object - count is not looked at).
NewtonErr
TSimpleRelocStoreDecompressor::Read(ULong pageId, char* buffer, long /*count*/, ULong baseAddress)
{
	FrameRelocationHeader header;
	long relocSize;
	long size;
	TSimpleCRelocator relocator;
	NewtonErr err = relocator.Init(fStore, pageId, &relocSize);
	if (err == noErr)
		err = fStore->Read(pageId, relocSize, (char*) header.fWord, 4);
	if (err == noErr)
		err = fStore->GetObjectSize(pageId, &size);
	if (err == noErr && (err = fStore->Read(pageId, relocSize + 4, buffer, (size - 4) - relocSize)) == noErr)
		RelocatePage(&header, buffer, baseAddress, &relocator);
	return err;
}


PROTOCOL_IMPL_SOURCE_MACRO(TLZStoreDecompressor)

// ROM 0x001f9d24 New__20TLZStoreDecompressorFv
TLZStoreDecompressor*
TLZStoreDecompressor::New()
{
	fBuffer = nil;
	fDecompressor = nil;
	fStore = nil;
	return this;
}

// ROM 0x001fa024 Delete__20TLZStoreDecompressorFv
void
TLZStoreDecompressor::Delete()
{
	if (fDecompressor != nil)
		fDecompressor->Delete();
}

// ROM 0x001fa38c Init__20TLZStoreDecompressorFP6TStoreUl
// The parameter is the buffer a compressed page is read into (the shared
// one); ==> kError_No_Memory without one or without an LZ decompressor.
NewtonErr
TLZStoreDecompressor::Init(TStore* store, ULong parameter)
{
	fBuffer = (char*) parameter;
	fStore = store;
	if (parameter != 0)
	{
		fDecompressor = (TDecompressor*) NewByName("TDecompressor", "TLZDecompressor");
		if (fDecompressor != nil)
			return noErr;
	}
	return kError_No_Memory;
}

// ROM 0x001fa5dc Read__20TLZStoreDecompressorFUlPclT1
// The frame header, then the compressed page expanded into 0x400 bytes.
// (The expansion's error is not looked at.)
NewtonErr
TLZStoreDecompressor::Read(ULong pageId, char* buffer, long /*count*/, ULong baseAddress)
{
	FrameRelocationHeader header;
	long size;
	NewtonErr err = fStore->Read(pageId, 0, (char*) header.fWord, 4);
	if (err == noErr && (err = fStore->GetObjectSize(pageId, &size)) == noErr)
	{
		ULong n = (ULong) (size - 4);
		if ((err = fStore->Read(pageId, 4, fBuffer, (long) n)) == noErr)
		{
			fDecompressor->Decompress(&n, buffer, kCompanderBlockSize, fBuffer, n);
			RelocatePage(&header, buffer, baseAddress, nil);
		}
	}
	return err;
}


PROTOCOL_IMPL_SOURCE_MACRO(TLZRelocStoreDecompressor)

// ROM 0x001f9d3c New__25TLZRelocStoreDecompressorFv
TLZRelocStoreDecompressor*
TLZRelocStoreDecompressor::New()
{
	fBuffer = nil;
	fDecompressor = nil;
	fStore = nil;
	return this;
}

// ROM 0x001f9d4c Delete__25TLZRelocStoreDecompressorFv
void
TLZRelocStoreDecompressor::Delete()
{
	if (fDecompressor != nil)
		fDecompressor->Delete();
}

// ROM 0x001f9d5c Init__25TLZRelocStoreDecompressorFP6TStoreUl
NewtonErr
TLZRelocStoreDecompressor::Init(TStore* store, ULong parameter)
{
	fBuffer = (char*) parameter;
	fStore = store;
	if (parameter != 0)
	{
		fDecompressor = (TDecompressor*) NewByName("TDecompressor", "TLZDecompressor");
		if (fDecompressor != nil)
			return noErr;
	}
	return kError_No_Memory;
}

// ROM 0x001f9dcc Read__25TLZRelocStoreDecompressorFUlPclT1
NewtonErr
TLZRelocStoreDecompressor::Read(ULong pageId, char* buffer, long /*count*/, ULong baseAddress)
{
	FrameRelocationHeader header;
	long relocSize;
	long size;
	TSimpleCRelocator relocator;
	NewtonErr err = relocator.Init(fStore, pageId, &relocSize);
	if (err == noErr)
		err = fStore->Read(pageId, relocSize, (char*) header.fWord, 4);
	if (err == noErr)
		err = fStore->GetObjectSize(pageId, &size);
	if (err == noErr)
	{
		ULong n = (ULong) ((size - 4) - relocSize);
		if ((err = fStore->Read(pageId, relocSize + 4, fBuffer, (long) n)) == noErr)
		{
			fDecompressor->Decompress(&n, buffer, kCompanderBlockSize, fBuffer, n);
			RelocatePage(&header, buffer, baseAddress, &relocator);
		}
	}
	return err;
}


PROTOCOL_IMPL_SOURCE_MACRO(TZippyStoreDecompressor)

// ROM 0x001facb8 New__23TZippyStoreDecompressorFv
TZippyStoreDecompressor*
TZippyStoreDecompressor::New()
{
	fBuffer = nil;
	fDecompressor = nil;
	fStore = nil;
	return this;
}

// ROM 0x001facc8 Delete__23TZippyStoreDecompressorFv
void
TZippyStoreDecompressor::Delete()
{
	if (fDecompressor != nil)
		fDecompressor->Delete();
	delete[] fBuffer;
}

// ROM 0x001facf4 Init__23TZippyStoreDecompressorFP6TStoreUl
// A buffer of its own (0x408 bytes: a Zippy page at worst).
NewtonErr
TZippyStoreDecompressor::Init(TStore* store, ULong /*parameter*/)
{
	fStore = store;
	fBuffer = new char[0x408];
	if (fBuffer != nil)
	{
		fDecompressor = (TDecompressor*) NewByName("TDecompressor", "TZippyDecompressor");
		if (fDecompressor != nil)
			return noErr;
	}
	return kError_No_Memory;
}

// ROM 0x001fad78 Read__23TZippyStoreDecompressorFUlPclT1
NewtonErr
TZippyStoreDecompressor::Read(ULong pageId, char* buffer, long /*count*/, ULong baseAddress)
{
	FrameRelocationHeader header;
	long size;
	NewtonErr err = fStore->Read(pageId, 0, (char*) header.fWord, 4);
	if (err == noErr && (err = fStore->GetObjectSize(pageId, &size)) == noErr)
	{
		ULong n = (ULong) (size - 4);
		if ((err = fStore->Read(pageId, 4, fBuffer, (long) n)) == noErr)
		{
			fDecompressor->Decompress(&n, buffer, kCompanderBlockSize, fBuffer, n);
			RelocatePage(&header, buffer, baseAddress, nil);
		}
	}
	return err;
}


PROTOCOL_IMPL_SOURCE_MACRO(TZippyRelocStoreDecompressor)

// ROM 0x001faac0 New__28TZippyRelocStoreDecompressorFv
TZippyRelocStoreDecompressor*
TZippyRelocStoreDecompressor::New()
{
	fBuffer = nil;
	fDecompressor = nil;
	fStore = nil;
	return this;
}

// ROM 0x001faad0 Delete__28TZippyRelocStoreDecompressorFv
void
TZippyRelocStoreDecompressor::Delete()
{
	if (fDecompressor != nil)
		fDecompressor->Delete();
	delete[] fBuffer;
}

// ROM 0x001faaf8 Init__28TZippyRelocStoreDecompressorFP6TStoreUl
NewtonErr
TZippyRelocStoreDecompressor::Init(TStore* store, ULong /*parameter*/)
{
	fStore = store;
	fBuffer = new char[0x408];
	if (fBuffer != nil)
	{
		fDecompressor = (TDecompressor*) NewByName("TDecompressor", "TZippyDecompressor");
		if (fDecompressor != nil)
			return noErr;
	}
	return kError_No_Memory;
}

// ROM 0x001fab78 Read__28TZippyRelocStoreDecompressorFUlPclT1
NewtonErr
TZippyRelocStoreDecompressor::Read(ULong pageId, char* buffer, long /*count*/, ULong baseAddress)
{
	FrameRelocationHeader header;
	long relocSize;
	long size;
	TSimpleCRelocator relocator;
	NewtonErr err = relocator.Init(fStore, pageId, &relocSize);
	if (err == noErr)
		err = fStore->Read(pageId, relocSize, (char*) header.fWord, 4);
	if (err == noErr)
		err = fStore->GetObjectSize(pageId, &size);
	if (err == noErr)
	{
		ULong n = (ULong) ((size - 4) - relocSize);
		if ((err = fStore->Read(pageId, relocSize + 4, fBuffer, (long) n)) == noErr)
		{
			fDecompressor->Decompress(&n, buffer, kCompanderBlockSize, fBuffer, n);
			RelocatePage(&header, buffer, baseAddress, &relocator);
		}
	}
	return err;
}


/*------------------------------------------------------------------------------
	T S t o r e C o m p a n d e r W r a p p e r
------------------------------------------------------------------------------*/

PROTOCOL_IMPL_SOURCE_MACRO(TStoreCompanderWrapper)

// ROM 0x001fa5cc New__22TStoreCompanderWrapperFv
TStoreCompanderWrapper*
TStoreCompanderWrapper::New()
{
	fStore = nil;
	fRootId = 0;
	fIndexId = 0;
	fDecompressor = nil;
	fName = nil;
	return this;
}

// ROM 0x001fa6ac Delete__22TStoreCompanderWrapperFv
// The decompressor deleted - as its "...Cleanup" implementation when one
// is registered.
void
TStoreCompanderWrapper::Delete()
{
	if (fDecompressor != nil)
	{
		char name[128];
		snprintf(name, sizeof(name), "%sCleanup", fName);
		const TClassInfo* cleanupInfo = ClassInfoByName("TStoreDecompressor", name, 0);
		if (cleanupInfo != nil)
			fDecompressor->SetType(cleanupInfo);
		fDecompressor->Delete();
	}
}

// ROM 0x001fa724 Init__22TStoreCompanderWrapperFP6TStoreUlT2UcT4
NewtonErr
TStoreCompanderWrapper::Init(TStore* /*store*/, ULong /*rootId*/, ULong /*arg3*/, UChar /*readOnly*/, UChar /*shared*/)
{
	return kError_Call_Not_Implemented;
}

// ROM 0x001fa730 Init__22TStoreCompanderWrapperFP6TStorePcUlT3
// Over the named decompressor, the index table read from the root.
NewtonErr
TStoreCompanderWrapper::Init(TStore* store, char* decompressor, ULong rootId, ULong parameter)
{
	NewtonErr err = kError_Bad_Parameters;
	fStore = store;
	fRootId = rootId;
	fName = decompressor;
	fDecompressor = (TStoreDecompressor*) NewByName("TStoreDecompressor", decompressor);
	if (fDecompressor != nil && (err = fDecompressor->Init(store, parameter)) == noErr)
	{
		UByte root[0x14];
		err = store->Read(fRootId, 0, (char*) root, 0x14);
		fIndexId = GetBigEndianWord(root);
	}
	return err;
}

// ROM 0x001fa7e8 BlockSize__22TStoreCompanderWrapperFv
ULong
TStoreCompanderWrapper::BlockSize()
{
	return kCompanderBlockSize;
}

// ROM 0x001fa7f0 Read__22TStoreCompanderWrapperFUlPclT1
// The page holding offset read (and relocated to page).
NewtonErr
TStoreCompanderWrapper::Read(ULong offset, char* buffer, long count, ULong page)
{
	UByte word[4];
	NewtonErr err = fStore->Read(fIndexId, (long) ((offset >> 10) << 2), (char*) word, 4);
	if (err == noErr)
		err = fDecompressor->Read(GetBigEndianWord(word), buffer, count, page);
	return err;
}

// ROM 0x001fa864 Write__22TStoreCompanderWrapperFUlPclT1
NewtonErr
TStoreCompanderWrapper::Write(ULong /*offset*/, char* /*buffer*/, long /*count*/, ULong /*page*/)
{
	return kError_Call_Not_Implemented;
}

// ROM 0x001fa870 DoTransactionAgainst__22TStoreCompanderWrapperFlUl
void
TStoreCompanderWrapper::DoTransactionAgainst(long arg, ULong /*page*/)
{
	LODefaultDoTransaction(fStore, fRootId, fIndexId, arg, false);
}

// ROM 0x001fa89c IsReadOnly__22TStoreCompanderWrapperFv
Boolean
TStoreCompanderWrapper::IsReadOnly()
{
	return true;
}


/*------------------------------------------------------------------------------
	T L O P a c k a g e S t o r e
------------------------------------------------------------------------------*/

PROTOCOL_IMPL_SOURCE_MACRO(TLOPackageStore)

// ROM 0x00102f68 New__15TLOPackageStoreFv
TLOPackageStore*
TLOPackageStore::New()
{
	return this;
}

// ROM 0x00102f6c Delete__15TLOPackageStoreFv
void
TLOPackageStore::Delete()
{}

// ROM 0x001015c4 Init__15TLOPackageStoreFv
NewtonErr
TLOPackageStore::Init()
{
	return noErr;
}

// ROM 0x001015cc Create__15TLOPackageStoreFPUlP6TStoreP5CPipelUcPcPvT4P11TLOCallback
// A root made, and the package read out of the pipe onto the store by
// AllocatePackage with the compressor that goes with the decompressor
// named (none: stored as it is).  The root aborted on failure.  (The size,
// read-only flag and parameters are not used.)
NewtonErr
TLOPackageStore::Create(ULong* id, TStore* store, CPipe* pipe, long /*size*/, UChar /*readOnly*/, char* compander, void* /*parameters*/,
						long /*parametersSize*/, TLOCallback* callback)
{
	*id = 0;
	PSSId rootId = 0;
	NewtonErr err = store->NewWithinTransaction(0, &rootId);
	*id = rootId;
	if (err == noErr)
	{
		TCallbackCompressor* compressor = (TCallbackCompressor*) NewByName("TCallbackCompressor", nil, compander);
		if (compressor == nil && MemError() != noErr)
			err = -7000;
		else
		{
			if (compressor == nil || (err = compressor->Init(nil)) == noErr)
				err = AllocatePackage(pipe, store, *id, compander, nil, 0, compressor, callback);
			if (compressor != nil)
				compressor->Delete();
		}
	}
	if (err != noErr && *id != 0)
		store->SeparatelyAbort(*id);
	return err;
}

// ROM 0x001016c4 CreateFromCompressed__15TLOPackageStoreFPUlP6TStoreP5CPipelUcPcPvT4P11TLOCallback
// NOT YET RECONSTRUCTED: LODefCreateFromComp (stores/LargeObjects.cpp).
NewtonErr
TLOPackageStore::CreateFromCompressed(ULong* /*id*/, TStore* /*store*/, CPipe* /*pipe*/, long /*size*/, UChar /*readOnly*/, char* /*compander*/,
									  void* /*parameters*/, long /*parametersSize*/, TLOCallback* /*callback*/)
{
	return kError_Call_Not_Implemented;
}

// ROM 0x00101714 DeleteObject__15TLOPackageStoreFP6TStoreUl
NewtonErr
TLOPackageStore::DeleteObject(TStore* store, PSSId id)
{
	return LODefaultDelete(store, id);
}

// ROM 0x00101720 Duplicate__15TLOPackageStoreFPUlP6TStoreUlT2
NewtonErr
TLOPackageStore::Duplicate(PSSId* newId, TStore* store, PSSId id, TStore* toStore)
{
	return LODefaultDuplicate(newId, store, id, toStore);
}

// ROM 0x0010173c Resize__15TLOPackageStoreFP6TStoreUlT2
NewtonErr
TLOPackageStore::Resize(TStore* /*store*/, PSSId /*id*/, ULong /*size*/)
{
	return -10605;
}

// ROM 0x00101748 StorageSize__15TLOPackageStoreFP6TStoreUl
long
TLOPackageStore::StorageSize(TStore* store, PSSId id)
{
	return LODefaultStorageSize(store, id);
}

// ROM 0x00101754 SizeOfStream__15TLOPackageStoreFP6TStoreUlUc
long
TLOPackageStore::SizeOfStream(TStore* store, PSSId id, UChar compressed)
{
	return LODefaultStreamSize(store, id, compressed);
}

NewtonErr	BackupPackage(CPipe* pipe, TStore* store, PSSId id, TLOCallback* callback);

// ROM 0x0010176c Backup__15TLOPackageStoreFP5CPipeP6TStoreUlUcP11TLOCallback
// Uncompressed: the package itself (BackupPackage); compressed: its store
// objects, the default way.
NewtonErr
TLOPackageStore::Backup(CPipe* pipe, TStore* store, PSSId id, UChar compressed, TLOCallback* callback)
{
	if (!compressed)
		return BackupPackage(pipe, store, id, callback);
	return LODefaultBackup(pipe, store, id, compressed, callback);
}


// ROM 0x00160cdc BackupPackage__FP5CPipeP6TStoreUlP11TLOCallback
// The package written to the pipe as it was installed: mapped (if it is
// not already) to learn its size, then every page read through its own
// decompressor at base 0 and written out, the last one cut to the size.
// ROM BUG kept: a package mapped here is never unmapped.
// NOT YET RECONSTRUCTED: the progress callback.
NewtonErr
BackupPackage(CPipe* pipe, TStore* store, PSSId id, TLOCallback* /*callback*/)
{
	char* page = new char[kCompanderBlockSize];
	char* name = nil;
	char* lzBuffer = nil;
	long packageSize = 0;
	NewtonErr err = page == nil ? kError_No_Memory : noErr;
	if (err == noErr)
	{
		ULong address;
		if (StoreToVAddr(&address, store, id) == noErr || (err = MapLargeObject(&address, store, id, false)) == noErr)
		{
			TPackageIterator iter((void*) address);
			if ((err = iter.Init()) == noErr)
				packageSize = (long) iter.PackageSize();
		}
		UByte root[0x14];
		if (err == noErr && (err = store->Read(id, 0, (char*) root, 0x14)) == noErr)
		{
			if (GetBigEndianWord(root + 0x0c) != 1)
				err = kError_Bad_Package;
			else
			{
				long nameSize;
				PSSId nameId = GetBigEndianWord(root + 4);
				PSSId indexId = GetBigEndianWord(root);
				if ((err = store->GetObjectSize(nameId, &nameSize)) == noErr)
				{
					name = new char[nameSize + 1];
					if (name == nil)
						err = kError_No_Memory;
					else if ((err = store->Read(nameId, 0, name, nameSize)) == noErr)
					{
						name[nameSize] = 0;
						TStoreDecompressor* decompressor = (TStoreDecompressor*) NewByName("TStoreDecompressor", name);
						if (decompressor == nil)
							err = kError_Bad_Parameters;
						else
						{
							ULong parameter = GetBigEndianWord(root + 8);
							if (strcmp(name, "TLZStoreDecompressor") == 0 || strcmp(name, "TLZRelocStoreDecompressor") == 0)
							{
								lzBuffer = new char[kLZCompanderBufferSize];
								parameter = (ULong) lzBuffer;
								err = lzBuffer == nil ? kError_No_Memory : decompressor->Init(store, parameter);
							}
							else
								err = decompressor->Init(store, parameter);
							long indexSize;
							if (err == noErr && (err = store->GetObjectSize(indexId, &indexSize)) == noErr)
							{
								ULong pages = (ULong) indexSize >> 2;
								long done = 0;
								for (ULong i = 0; i < pages; i++)
								{
									UByte word[4];
									if ((err = store->Read(indexId, (long) (i << 2), (char*) word, 4)) != noErr
									||  (err = decompressor->Read(GetBigEndianWord(word), page, kCompanderBlockSize, 0)) != noErr)
										break;
									long n = packageSize - done;
									if (n > kCompanderBlockSize)
										n = kCompanderBlockSize;
									volatile NewtonErr writeErr = noErr;
									newton_try
									{
										pipe->WriteChunk(page, n, false);
									}
									newton_catch(exPipeException)
									{
										writeErr = (NewtonErr) (long) (Long) _info.exception.data;
									}
									end_try;
									if ((err = writeErr) != noErr)
										break;
									done += n;
								}
							}
							decompressor->Delete();
						}
					}
				}
			}
		}
	}
	delete[] page;
	delete[] name;
	delete[] lzBuffer;
	return err;
}


/*------------------------------------------------------------------------------
	A l l o c a t e P a c k a g e ,   N e w P a c k a g e
------------------------------------------------------------------------------*/

// ROM 0x00161360 AllocatePackage__FP5CPipeP6TStoreUlPcPvlP19TCallbackCompressorP11TLOCallback
// The root grown to a PackageRoot; the index table, the decompressor's
// name and its parameters made; the package stored through the index; the
// root written, 'paok' last.  A package flagged uncompressed (0x10000000)
// is read back by the simple decompressors and stored with no compressor.
// The patch package is not stored at all: read into a binary and loaded,
// the store aborted, ==> 1.  On failure the objects made are aborted.
// ROM BUG kept: the patch package's relocation chunk, which the iterator
// has already read out of the pipe, is not copied into the binary, and
// the rest is read as if it began at the directory's end.
NewtonErr
AllocatePackage(CPipe* pipe, TStore* store, PSSId rootId, char* decompressor, void* parameters, long parametersSize,
				TCallbackCompressor* compressor, TLOCallback* callback)
{
	PSSId indexId = 0, nameId = 0, parametersId = 0;
	NewtonErr err;
	{
		TPackageIterator iter(pipe);
		err = iter.Init();
		if (err == noErr)
		{
			if (IsPatchPackageName(iter.PackageName()))		// (the ROM's Ustrcmp)
			{
				RefVar binary(AllocateBinary(RSSYMbinary, (long) iter.PackageSize()));
				char* buffer = BinaryData(binary);
				ULong numParts = iter.NumberOfParts();
				memmove(buffer, iter.fDirectory, kPackageDirectorySize);
				memmove(buffer + kPackageDirectorySize, iter.fParts, numParts << 5);
				memmove(buffer + numParts * 0x20 + kPackageDirectorySize, iter.fDirectoryData,
						iter.DirectorySize() - (numParts * 0x20 + kPackageDirectorySize));
				ULong directorySize = iter.DirectorySize();
				volatile NewtonErr readErr = noErr;
				newton_try
				{
					long count = (long) (iter.PackageSize() - directorySize);
					Boolean eof;
					pipe->ReadChunk(buffer + directorySize, count, eof);
				}
				newton_catch(exPipeException)
				{
					readErr = (NewtonErr) (long) (Long) _info.exception.data;
				}
				end_try;
				err = readErr;
				if (err == noErr)
				{
					SourceType type;
					memset(&type, 0, sizeof(type));
					type.format = kFixedMemory;
					ULong packageId;
					err = LoadPackage(BinaryData(binary), type, &packageId);
				}
				store->Abort();
				return err == noErr ? 1 : err;
			}
			if ((err = store->SetObjectSize(rootId, 0x14)) == noErr
			&&  (err = store->NewWithinTransaction(0, &indexId)) == noErr)
			{
				if ((iter.PackageFlags() & 0x10000000) != 0)
				{
					decompressor = (char*) ((iter.PackageFlags() & 0x4000000) == 0 ? "TSimpleStoreDecompressor" : "TSimpleRelocStoreDecompressor");
					parameters = nil;
					parametersSize = 0;
					compressor = nil;
				}
				if ((err = store->NewWithinTransaction((long) strlen(decompressor), &nameId)) == noErr
				&&  (err = store->Write(nameId, 0, decompressor, (long) strlen(decompressor))) == noErr
				&&  (err = store->NewWithinTransaction(parametersSize, &parametersId)) == noErr
				&&  (parametersSize == 0 || (err = store->Write(parametersId, 0, (char*) parameters, parametersSize)) == noErr)
				&&  (err = iter.Store(store, indexId, compressor, callback)) == noErr)
				{
					UByte root[0x14];
					PutBigEndianWord(root + 0x00, (ULong32) indexId);
					PutBigEndianWord(root + 0x04, (ULong32) nameId);
					PutBigEndianWord(root + 0x08, (ULong32) parametersId);
					PutBigEndianWord(root + 0x0c, 1);		// (PackageRoot's constructor)
					PutBigEndianWord(root + 0x10, 'paok');
					err = store->Write(rootId, 0, (char*) root, 0x14);
				}
			}
		}
	}
	if (err != noErr)
	{
		if (parametersId != 0)
			store->SeparatelyAbort(parametersId);
		if (nameId != 0)
			store->SeparatelyAbort(nameId);
		if (indexId != 0)
			store->SeparatelyAbort(indexId);
	}
	return err;
}


// ROM 0x001617b4 AllocatePackage__FP5CPipeP6TStoreUlPcPvlP19TCallbackCompressor
NewtonErr
AllocatePackage(CPipe* pipe, TStore* store, PSSId rootId, char* decompressor, void* parameters, long parametersSize,
				TCallbackCompressor* compressor)
{
	return AllocatePackage(pipe, store, rootId, decompressor, parameters, parametersSize, compressor, nil);
}


// ROM 0x001619f4 NewPackage__FP5CPipeP6TStoreUlPUlPcPvlP19TCallbackCompressor
// The package stored with the store locked, installed from there, and
// committed (a package only dispatched is taken back off the store at
// once); a patch that went in restarts the machine.
// ROM BUGS kept: the store is left locked when the package will not
// install, and when it was the patch package; a failure to commit takes
// the package back off the store but still answers noErr (the unlock's).
NewtonErr
NewPackage(CPipe* pipe, TStore* store, PSSId rootId, ULong* packageId, char* decompressor, void* parameters, long parametersSize,
		   TCallbackCompressor* compressor)
{
	UChar patchInstalled = false;
	UChar forDispatchOnly;
	store->LockStore();
	NewtonErr err = AllocatePackage(pipe, store, rootId, decompressor, parameters, parametersSize, compressor, nil);
	if (err == 1)
	{
		*packageId = 0;
		return noErr;
	}
	if (err != noErr)
	{
		store->Abort();
		return err;
	}
	if ((err = PackageAvailable(store, rootId, packageId, &forDispatchOnly, &patchInstalled)) != noErr)
	{
		DeallocatePackage(store, rootId);
		return err;
	}
	if (forDispatchOnly)
	{
		*packageId = 0;
		DeallocatePackage(store, rootId);
	}
	else
	{
		ULong address;
		if ((err = MapLargeObject(&address, store, rootId, true)) == noErr && (err = CommitObject(address)) == noErr)
			UnmapLargeObject(address);
		else
			DeallocatePackage(store, rootId);
	}
	err = store->UnlockStore();
	if (patchInstalled)
		Reboot(-10077, 0, false);
	return err;
}


/*------------------------------------------------------------------------------
	R e g i s t r a t i o n
------------------------------------------------------------------------------*/

PROTOCOL_CLASSINFO(TSimpleStoreDecompressor, "TStoreDecompressor", "", 0, 0, nil)			// ROM 0x003874f4 ClassInfo__24TSimpleStoreDecompressorSFv
PROTOCOL_CLASSINFO(TLZStoreDecompressor, "TStoreDecompressor", "", 0, 0, nil)				// ROM 0x00387588 ClassInfo__20TLZStoreDecompressorSFv
PROTOCOL_CLASSINFO(TZippyStoreDecompressor, "TStoreDecompressor", "", 0, 0, nil)			// ROM 0x00387618 ClassInfo__23TZippyStoreDecompressorSFv
PROTOCOL_CLASSINFO(TSimpleRelocStoreDecompressor, "TStoreDecompressor", "", 0, 0, nil)		// ROM 0x003876a8 ClassInfo__29TSimpleRelocStoreDecompressorSFv
PROTOCOL_CLASSINFO(TLZRelocStoreDecompressor, "TStoreDecompressor", "", 0, 0, nil)			// ROM 0x00387740 ClassInfo__25TLZRelocStoreDecompressorSFv
PROTOCOL_CLASSINFO(TZippyRelocStoreDecompressor, "TStoreDecompressor", "", 0, 0, nil)		// ROM 0x003877d4 ClassInfo__28TZippyRelocStoreDecompressorSFv
PROTOCOL_CLASSINFO(TStoreCompanderWrapper, "TStoreCompander", "", 0, 0, nil)				// ROM 0x0038786c ClassInfo__22TStoreCompanderWrapperSFv
PROTOCOL_CLASSINFO(TLOPackageStore, "TLrgObjStore",
	"TZippyRelocStoreDecompressor\0\0TZippyStoreDecompressor\0\0TSimpleRelocStoreDecompressor\0\0"
	"TLZRelocStoreDecompressor\0\0TLZStoreDecompressor\0\0TSimpleStoreDecompressor\0\0", 0, 0, nil)	// ROM 0x00387b7c ClassInfo__15TLOPackageStoreSFv


// ROM 0x001fa9fc InitializeStoreDecompressors__Fv
// NOT YET RECONSTRUCTED: TXIPPackageStore and TXIPStoreCompander.  The
// companders and the shared LZ decompressor and buffer are
// InitializeStoreCompanders' (stores/StoreCompander.cpp).
void
InitializeStoreDecompressors(void)
{
	static Boolean done = false;		// (host: HostMountStores may run more than once)
	if (done)
		return;
	done = true;
	TSimpleStoreDecompressor::ClassInfo()->Register();
	TLZStoreDecompressor::ClassInfo()->Register();
	TZippyStoreDecompressor::ClassInfo()->Register();
	TSimpleRelocStoreDecompressor::ClassInfo()->Register();
	TLZRelocStoreDecompressor::ClassInfo()->Register();
	TZippyRelocStoreDecompressor::ClassInfo()->Register();
	TStoreCompanderWrapper::ClassInfo()->Register();
	TLOPackageStore::ClassInfo()->Register();
	InitializeStoreCompanders();
}
