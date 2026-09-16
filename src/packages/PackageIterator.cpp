/*
	File:		packages/PackageIterator.cpp

	Contains:	TPrivatePackageIterator and TPackageIterator
				(PackageIterator.h).

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "PackageIterator.h"
#include "OSErrors.h"
#include "NewtonExceptions.h"
#include "NewtonMemory.h"
#include "Unicode.h"
#include <stdlib.h>
#include <string.h>

extern const ExceptionName exPipeException;

// ROM 0x0035481c: the signature's last characters, "0" and "1" (the
// directory format versions this ROM accepts)
static const char kPackageSignatureVersions[2] = { '0', '1' };


// ROM 0x001966b4 IsPackageHeader__FUlT1
// Whether the bytes begin with a package signature (the ROM catches the
// bus and permission aborts of an unreadable address: none here).
Boolean
IsPackageHeader(const void* data, ULong size)
{
	if (size <= 0x33)
		return false;
	const char* bytes = (const char*) data;
	for (int i = 0; i < 7; i++)
		if (bytes[i] != "package"[i])
			return false;
	for (int i = 0; i < 2; i++)
		if (bytes[7] == kPackageSignatureVersions[i])
			return true;
	return false;
}


/*------------------------------------------------------------------------------
	T P r i v a t e P a c k a g e I t e r a t o r
------------------------------------------------------------------------------*/

// ROM 0x001964fc __ct__23TPrivatePackageIteratorFv
TPrivatePackageIterator::TPrivatePackageIterator()
{
	fPackage = nil;
	fDirectory = nil;
	fParts = nil;
	fDirectoryData = nil;
	fPartsOffset = 0;
	fRelocationInfo = nil;
	fRelocationData = nil;
}


// ROM 0x001967bc __dt__23TPrivatePackageIteratorFv
TPrivatePackageIterator::~TPrivatePackageIterator()
{
	DisposeDirectory();
}


// ROM 0x0019693c Init__23TPrivatePackageIteratorFPv
// Over the package at package: the header checked, the entries and data
// located, the relocation chunk found, the parts' offset computed, the
// directory verified.
NewtonErr
TPrivatePackageIterator::Init(void* package)
{
	fPackage = (UByte*) package;
	fDirectory = (PackageDirectory*) package;
	NewtonErr err = CheckHeader();
	if (err == noErr)
	{
		ULong entriesSize = fDirectory->NumParts() * kPartEntrySize;
		ULong dataSize = fDirectory->DirectorySize() - (entriesSize + kPackageDirectorySize);
		err = ComputeSizeOfEntriesAndData(entriesSize, dataSize);
		if (err == noErr)
		{
			ULong relocationSize;
			err = SetupRelocationData(entriesSize + dataSize, &relocationSize);
			if (err == noErr)
				fPartsOffset = entriesSize + dataSize + relocationSize + kPackageDirectorySize;
		}
	}
	if (err == noErr)
		err = VerifyPackage();
	if (err != noErr)
		DisposeDirectory();
	return err;
}


// ROM 0x001967e8 DisposeDirectory__23TPrivatePackageIteratorFv
void
TPrivatePackageIterator::DisposeDirectory(void)
{
	fDirectory = nil;
	fParts = nil;
	fDirectoryData = nil;
	fRelocationInfo = nil;
	fRelocationData = nil;
	fPartsOffset = 0;
}


// ROM 0x00196808 CheckHeader__23TPrivatePackageIteratorFv
NewtonErr
TPrivatePackageIterator::CheckHeader(void)
{
	const char* signature = fDirectory->fSignature;
	for (int i = 0; i < 7; i++)
		if ("package"[i] != signature[i])
			return kError_Bad_Package;
	for (int i = 0; i < 2; i++)
		if (signature[7] == kPackageSignatureVersions[i])
			return noErr;
	return kError_Bad_Package;
}


// ROM 0x001968a4 ComputeSizeOfEntriesAndData__23TPrivatePackageIteratorFRUlT1
// The entries follow the header, the data the entries.
NewtonErr
TPrivatePackageIterator::ComputeSizeOfEntriesAndData(ULong& entriesSize, ULong& /*dataSize*/)
{
	fParts = (PartEntry*) (fPackage + kPackageDirectorySize);
	fDirectoryData = fPackage + kPackageDirectorySize + entriesSize;
	return noErr;
}


// ROM 0x001968e0 SetupRelocationData__23TPrivatePackageIteratorFUlPUl
// The relocation chunk (when the flags say there is one) after the
// directory; its reserved word must be 0.  *relocationSize: its size.
NewtonErr
TPrivatePackageIterator::SetupRelocationData(ULong directoryOffset, ULong* relocationSize)
{
	if ((fDirectory->Flags() & kRelocationFlag) == 0)
	{
		fRelocationInfo = nil;
		fRelocationData = nil;
		*relocationSize = 0;
		return noErr;
	}
	fRelocationInfo = (RelocationHeader*) (fPackage + directoryOffset + kPackageDirectorySize);
	if (fRelocationInfo->Reserved() != 0)
	{
		*relocationSize = 0;
		return kError_Bad_Package;
	}
	*relocationSize = fRelocationInfo->RelocationSize();
	return noErr;
}


// ROM 0x001968c8 GetRelocationChunkInfo__23TPrivatePackageIteratorFv
// The relocation entries follow the chunk's first word.
NewtonErr
TPrivatePackageIterator::GetRelocationChunkInfo(void)
{
	if (fRelocationInfo != nil)
		fRelocationData = (UByte*) fRelocationInfo + 4;
	return noErr;
}


// ROM 0x00196a10 VerifyPackage__23TPrivatePackageIteratorFv
// The InfoRefs within the directory, the processor type this ROM's, each
// part's info and compressor within the directory and its offset within
// the package.
NewtonErr
TPrivatePackageIterator::VerifyPackage(void)
{
	Ustrlen(PackageName());
	ULong directorySize = fDirectory->DirectorySize();
	if (fDirectory->fName.Offset() > directorySize)
		return kError_Bad_Package;
	if (fDirectory->fCopyright.Length() != 0 && fDirectory->fCopyright.Offset() > directorySize)
		return kError_Bad_Package;
	ULong processor = fDirectory->Flags() & kPackageProcessorMask;
	if (processor != 0 && processor != 0x1000)
		return kError_Bad_Package;
	ULong numParts = NumberOfParts();
	for (ULong i = 0; i < numParts; i++)
	{
		PartEntry* entry = &fParts[i];
		if ((entry->Flags() & kCompressedFlag) != 0 && entry->fCompressor.Offset() > directorySize)
			return kError_Bad_Package;
		if (entry->fInfo.Length() != 0 && entry->fInfo.Offset() > directorySize)
			return kError_Bad_Package;
		if (entry->Offset() > fDirectory->Size() - directorySize)
			return kError_Bad_Package;
	}
	return noErr;
}


// ROM 0x0019658c NumberOfParts__23TPrivatePackageIteratorFv
ULong
TPrivatePackageIterator::NumberOfParts(void)
{
	return fDirectory == nil ? 0 : fDirectory->NumParts();
}


// ROM 0x001965a0 PackageSize__23TPrivatePackageIteratorFv
ULong
TPrivatePackageIterator::PackageSize(void)
{
	return fDirectory == nil ? 0 : fDirectory->Size();
}


// ROM 0x00196544 PackageName__23TPrivatePackageIteratorFv
// The name as it lies in the directory data: big-endian UniChars with a
// terminator (a host reads them with GetBigEndianHalf).
const UniChar*
TPrivatePackageIterator::PackageName(void)
{
	return (const UniChar*) (fDirectoryData + fDirectory->fName.Offset());
}


// ROM 0x00196558 GetPartDataOffset__23TPrivatePackageIteratorFUl
// Where the part's data lies in the package.
ULong
TPrivatePackageIterator::GetPartDataOffset(ULong partIndex)
{
	if (partIndex < fDirectory->NumParts() && fParts != nil)
		return fPartsOffset + fParts[partIndex].Offset();
	return 0;
}


// ROM 0x001965b4 GetPartInfoDesc__23TPrivatePackageIteratorFUlCP8PartInfo
// The part's entry as a PartInfo (its data left to the caller).
void
TPrivatePackageIterator::GetPartInfoDesc(ULong partIndex, PartInfo* const info)
{
	PartEntry* entry = &fParts[partIndex];
	ULong flags = entry->Flags();
	info->autoLoad = (flags & kAutoLoadPartFlag) != 0;
	info->autoRemove = (flags & kAutoRemovePartFlag) != 0;
	info->compressed = (flags & kCompressedFlag) != 0;
	info->notify = (flags & kNotifyFlag) != 0;
	info->autoCopy = (flags & kAutoCopyFlag) != 0;
	info->kind = flags & kPartKindMask;
	info->type = entry->Type();
	info->size = entry->Size();
	info->sizeInMemory = entry->SizeInMemory();
	info->infoSize = entry->fInfo.Length();
	info->info = fDirectoryData + entry->fInfo.Offset();
	info->compressor = (char*) (fDirectoryData + entry->fCompressor.Offset());
}


// ROM 0x00196660 GetPartInfo__23TPrivatePackageIteratorFUlCP8PartInfo
// The part's info with its data's address in memory.
void
TPrivatePackageIterator::GetPartInfo(ULong partIndex, PartInfo* const info)
{
	if (partIndex >= fDirectory->NumParts())
		return;
	GetPartInfoDesc(partIndex, info);
	info->data = (ULong) (fPackage + GetPartDataOffset(partIndex));
}


/*------------------------------------------------------------------------------
	T P a c k a g e I t e r a t o r
------------------------------------------------------------------------------*/

// ROM 0x0015e7e0 __ct__16TPackageIteratorFP5CPipe
TPackageIterator::TPackageIterator(CPipe* pipe)
{
	fFromPipe = true;
	fPackage = nil;
	fPipe = pipe;
}


// ROM 0x0015e82c __ct__16TPackageIteratorFPv
TPackageIterator::TPackageIterator(void* package)
{
	fFromPipe = false;
	fPipe = nil;
	fPackage = (UByte*) package;
}


// ROM 0x0015ea38 __dt__16TPackageIteratorFv
TPackageIterator::~TPackageIterator()
{
	DisposeDirectory();
}


// count bytes from the pipe into data; the pipe's exceptions come back
// as their error
static NewtonErr
ReadFromPipe(CPipe* pipe, void* data, long count)
{
	volatile NewtonErr err = noErr;
	newton_try
	{
		long size = count;
		Boolean eof;
		pipe->ReadChunk(data, size, eof);
	}
	newton_catch(exPipeException)
	{
		err = (NewtonErr) (long) (Long) _info.exception.data;
	}
	end_try;
	return err;
}


// ROM 0x0015f3d0 Init__16TPackageIteratorFv
// A memory source as the private iterator; a pipe: the header read into
// memory of its own and checked, then the entries and data, the
// relocation chunk, the parts' offset; verified.  Any exception is a bad
// package.
NewtonErr
TPackageIterator::Init(void)
{
	volatile NewtonErr err = noErr;
	newton_try
	{
		if (!fFromPipe)
			err = TPrivatePackageIterator::Init(fPackage);
		else
		{
			fDirectory = (PackageDirectory*) new UByte[kPackageDirectorySize];
			if (fDirectory == nil)
				err = kError_No_Memory;
			else
			{
				err = ReadFromPipe(fPipe, fDirectory, kPackageDirectorySize);
				if (err == noErr && (err = CheckHeader()) == noErr)
				{
					ULong entriesSize = fDirectory->NumParts() * kPartEntrySize;
					ULong dataSize = fDirectory->DirectorySize() - (entriesSize + kPackageDirectorySize);
					err = ComputeSizeOfEntriesAndData(entriesSize, dataSize);
					if (err == noErr)
					{
						ULong relocationSize;
						err = SetupRelocationData(entriesSize + dataSize, &relocationSize);
						if (err == noErr)
							fPartsOffset = entriesSize + dataSize + relocationSize + kPackageDirectorySize;
					}
				}
			}
			if (err == noErr)
				err = VerifyPackage();
		}
	}
	newton_catch(exRootException)
	{
		err = kError_Bad_Package;
	}
	end_try;
	if (err != noErr)
		DisposeDirectory();
	return err;
}


// ROM 0x0015efc0 DisposeDirectory__16TPackageIteratorFv
// A pipe source's directory memory freed.
void
TPackageIterator::DisposeDirectory(void)
{
	if (fFromPipe)
	{
		if (fDirectory != nil)
			delete[] (UByte*) fDirectory;
		if (fParts != nil)
			free(fParts);
		if (fDirectoryData != nil)
			free(fDirectoryData);
		if (fRelocationInfo != nil)
			free(fRelocationInfo);
		if (fRelocationData != nil)
			free(fRelocationData);
	}
	fDirectory = nil;
	fParts = nil;
	fDirectoryData = nil;
	fRelocationInfo = nil;
	fRelocationData = nil;
	fPartsOffset = 0;
}


// ROM 0x0015f024 ComputeSizeOfEntriesAndData__16TPackageIteratorFRUlT1
// A pipe source: the entries and the directory data read into memory.
NewtonErr
TPackageIterator::ComputeSizeOfEntriesAndData(ULong& entriesSize, ULong& dataSize)
{
	if (!fFromPipe)
		return TPrivatePackageIterator::ComputeSizeOfEntriesAndData(entriesSize, dataSize);
	fParts = (PartEntry*) malloc(entriesSize);
	if (fParts == nil)
		return kError_No_Memory;
	NewtonErr err = ReadFromPipe(fPipe, fParts, entriesSize);
	if (err != noErr)
		return err;
	fDirectoryData = (UByte*) malloc(dataSize);
	if (fDirectoryData == nil)
		return kError_No_Memory;
	return ReadFromPipe(fPipe, fDirectoryData, dataSize);
}


// ROM 0x0015f2ac SetupRelocationData__16TPackageIteratorFUlPUl
// A pipe source: the relocation header read into memory.
NewtonErr
TPackageIterator::SetupRelocationData(ULong directoryOffset, ULong* relocationSize)
{
	if ((fDirectory->Flags() & kRelocationFlag) == 0)
	{
		fRelocationInfo = nil;
		fRelocationData = nil;
		*relocationSize = 0;
		return noErr;
	}
	if (!fFromPipe)
		return TPrivatePackageIterator::SetupRelocationData(directoryOffset, relocationSize);
	fRelocationInfo = (RelocationHeader*) malloc(kRelocationHeaderSize);
	if (fRelocationInfo == nil)
		return kError_No_Memory;
	NewtonErr err = ReadFromPipe(fPipe, fRelocationInfo, kRelocationHeaderSize);
	if (fRelocationInfo->Reserved() == 0)
		*relocationSize = fRelocationInfo->RelocationSize();
	else
	{
		err = kError_Bad_Package;
		*relocationSize = 0;
	}
	return err;
}


// ROM 0x0015f1b0 GetRelocationChunkInfo__16TPackageIteratorFv
// A pipe source: the relocation entries read into memory (the header's
// size less the header).
NewtonErr
TPackageIterator::GetRelocationChunkInfo(void)
{
	if (fRelocationInfo == nil)
		return noErr;
	if (!fFromPipe)
		return TPrivatePackageIterator::GetRelocationChunkInfo();
	long size = fRelocationInfo->RelocationSize() - kRelocationHeaderSize;
	fRelocationData = (UByte*) malloc(size);
	if (fRelocationData == nil)
		return kError_No_Memory;
	return ReadFromPipe(fPipe, fRelocationData, size);
}


// ROM 0x0015f5b8 VerifyPackage__16TPackageIteratorFv
// (The ROM turns an abort while reading an unmapped package into a bad
// package.)
NewtonErr
TPackageIterator::VerifyPackage(void)
{
	return TPrivatePackageIterator::VerifyPackage();
}


// ROM 0x0015e894 NumberOfParts__16TPackageIteratorFv
ULong
TPackageIterator::NumberOfParts(void)
{
	return TPrivatePackageIterator::NumberOfParts();
}


// ROM 0x0015e914 PackageSize__16TPackageIteratorFv
ULong
TPackageIterator::PackageSize(void)
{
	return TPrivatePackageIterator::PackageSize();
}


// ROM 0x0015e994 DirectorySize__16TPackageIteratorFv
ULong
TPackageIterator::DirectorySize(void)
{
	return fDirectory == nil ? 0 : fDirectory->DirectorySize();
}


// ROM 0x0015e9a8 GetPackageId__16TPackageIteratorFv
// The package's type word ('xxxx' for an NTK application).
ULong
TPackageIterator::GetPackageId(void)
{
	return fDirectory == nil ? 0 : GetBigEndianWord(fDirectory->fType);
}


// ROM 0x0015e9bc GetVersion__16TPackageIteratorFv
ULong
TPackageIterator::GetVersion(void)
{
	return fDirectory == nil ? 0 : fDirectory->Version();
}


// ROM 0x0015e9d0 CreationDate__16TPackageIteratorFv
ULong
TPackageIterator::CreationDate(void)
{
	return fDirectory == nil ? 0 : fDirectory->CreationDate();
}


// ROM 0x0015e9e4 ModifyDate__16TPackageIteratorFv
ULong
TPackageIterator::ModifyDate(void)
{
	return fDirectory == nil ? 0 : fDirectory->ModifyDate();
}


// ROM 0x0015ea70 PackageFlags__16TPackageIteratorFv
ULong
TPackageIterator::PackageFlags(void)
{
	return fDirectory == nil ? 0 : fDirectory->Flags();
}


// ROM 0x0015e9f8 ForDispatchOnly__16TPackageIteratorFv
// The auto-remove bit: a package loaded for dispatching (installing its
// parts) only, not kept.
Boolean
TPackageIterator::ForDispatchOnly(void)
{
	return fDirectory != nil && (fDirectory->Flags() & kAutoRemoveFlag) != 0;
}


// ROM 0x0015ea18 CopyProtected__16TPackageIteratorFv
Boolean
TPackageIterator::CopyProtected(void)
{
	return fDirectory != nil && (fDirectory->Flags() & kCopyProtectFlag) != 0;
}


// ROM 0x0015e874 Copyright__16TPackageIteratorFv
// The copyright as it lies (big-endian UniChars); nil when there is none.
const UniChar*
TPackageIterator::Copyright(void)
{
	if (fDirectory->fCopyright.Length() == 0)
		return nil;
	return (const UniChar*) (fDirectoryData + fDirectory->fCopyright.Offset());
}


// ROM 0x0015ea84 GetPartInfo__16TPackageIteratorFUlCP8PartInfo
// The part's info: its data's address for a memory source, its offset in
// the package for a pipe.
void
TPackageIterator::GetPartInfo(ULong partIndex, PartInfo* const info)
{
	if (partIndex >= fDirectory->NumParts())
		return;
	GetPartInfoDesc(partIndex, info);
	if (!fFromPipe)
		info->data = (ULong) (fPackage + TPrivatePackageIterator::GetPartDataOffset(partIndex));
	else
		info->data = TPrivatePackageIterator::GetPartDataOffset(partIndex);
}


// ROM 0x0015eaec ProcessorTypeOfPart__16TPackageIteratorFUl
ULong
TPackageIterator::ProcessorTypeOfPart(ULong partIndex)
{
	if (partIndex < fDirectory->NumParts())
		return fParts[partIndex].Flags() & kPartProcessorMask;
	return 0;
}
