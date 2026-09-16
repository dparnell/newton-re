/*
	File:		packages/PackageIterator.h

	Contains:	The package directory and its readers.  A package is a
				directory followed by its parts: the directory is a header
				(PackageDirectory: the signature "package0" or "package1" - the
				newer directory format - the
				type, the flags, the version, the copyright and the name as
				InfoRefs into the directory data, the size, the dates, the
				directory size and the number of parts), one PartEntry per
				part, then the directory data the InfoRefs point into, then,
				when kRelocationFlag is set, the relocation chunk (a
				RelocationHeader and its entries); the parts follow, each at
				its entry's offset.  All words are big-endian.

				TPrivatePackageIterator reads a package in memory (the
				directory is used where it lies); TPackageIterator reads one
				in memory or from a CPipe (the directory read into memory of
				its own) and answers the part infos the package manager
				installs parts from.

				The ROM's layouts: TPrivatePackageIterator 0x20, TPackageIterator
				0x28; the directory structures as in the package.

	Reconstructed from the MP2100 D ROM (0x001964fc-0x00196a10,
	0x0015e7e0-0x0015f638); each function cites its origin.  The format
	is also described by tools/newton-rom/analysis/packages.py, which
	lists the packages built into the ROM extension.
*/

#ifndef __PACKAGEITERATOR_H
#define __PACKAGEITERATOR_H

#ifndef __PACKAGETYPES_H
#include "PackageTypes.h"
#endif
#ifndef __PIPES_H
#include "Pipes.h"
#endif
#ifndef __BYTEORDER_H
#include "ByteOrder.h"
#endif

// the package flags
const ULong kAutoRemoveFlag = 0x80000000;
const ULong kCopyProtectFlag = 0x40000000;
const ULong kNoCompressionFlag = 0x10000000;
const ULong kRelocationFlag = 0x04000000;
const ULong kUseFasterCompressionFlag = 0x02000000;
const ULong kPackageProcessorMask = 0x0000f000;		// 0 or 0x1000 in a package this ROM loads

// the part flags
const ULong kPartKindMask = 0x0000000f;				// kProtocol, kFrames, kRaw
const ULong kAutoLoadPartFlag = 0x00000010;
const ULong kAutoRemovePartFlag = 0x00000020;
const ULong kCompressedFlag = 0x00000040;
const ULong kNotifyFlag = 0x00000080;
const ULong kAutoCopyFlag = 0x00000100;
const ULong kPartProcessorMask = 0x0000f000;

// An InfoRef: a 16-bit offset into the directory data and a 16-bit length.
struct InfoRef
{
	UByte		fOffset[2];
	UByte		fLength[2];

	unsigned int	Offset(void) const		{ return GetBigEndianHalf(fOffset); }
	unsigned int	Length(void) const		{ return GetBigEndianHalf(fLength); }
};

// the directory header (0x34 bytes)
struct PackageDirectory
{
	char		fSignature[8];			// +0x00  "package0" / "package1"
	char		fType[4];				// +0x08
	UByte		fFlags[4];				// +0x0c
	UByte		fVersion[4];			// +0x10
	InfoRef		fCopyright;				// +0x14
	InfoRef		fName;					// +0x18  UniChars with a terminator
	UByte		fSize[4];				// +0x1c  the whole package
	UByte		fCreationDate[4];		// +0x20
	UByte		fModifyDate[4];			// +0x24  (reserved2)
	UByte		fReserved3[4];			// +0x28
	UByte		fDirectorySize[4];		// +0x2c  header, entries and data
	UByte		fNumParts[4];			// +0x30

	ULong		Flags(void) const			{ return GetBigEndianWord(fFlags); }
	ULong		Version(void) const			{ return GetBigEndianWord(fVersion); }
	ULong		Size(void) const			{ return GetBigEndianWord(fSize); }
	ULong		CreationDate(void) const	{ return GetBigEndianWord(fCreationDate); }
	ULong		ModifyDate(void) const		{ return GetBigEndianWord(fModifyDate); }
	ULong		DirectorySize(void) const	{ return GetBigEndianWord(fDirectorySize); }
	ULong		NumParts(void) const		{ return GetBigEndianWord(fNumParts); }
};
const long kPackageDirectorySize = 0x34;

// a part entry (0x20 bytes)
struct PartEntry
{
	UByte		fOffset[4];				// +0x00  from the end of the directory (and relocation chunk)
	UByte		fSize[4];				// +0x04
	UByte		fSize2[4];				// +0x08  the size in memory
	UByte		fType[4];				// +0x0c
	UByte		fReserved[4];			// +0x10
	UByte		fFlags[4];				// +0x14
	InfoRef		fInfo;					// +0x18
	InfoRef		fCompressor;			// +0x1c

	ULong		Offset(void) const			{ return GetBigEndianWord(fOffset); }
	ULong		Size(void) const			{ return GetBigEndianWord(fSize); }
	ULong		SizeInMemory(void) const	{ return GetBigEndianWord(fSize2); }
	ULong		Type(void) const			{ return GetBigEndianWord(fType); }
	ULong		Flags(void) const			{ return GetBigEndianWord(fFlags); }
};
const long kPartEntrySize = 0x20;

// the relocation chunk's header (0x14 bytes); the entries follow, each a
// page number, the count of offsets in it, and the offsets (of words to
// relocate within that page)
struct RelocationHeader
{
	UByte		fReserved[4];			// +0x00  0
	UByte		fRelocationSize[4];		// +0x04  the chunk's size, this header included
	UByte		fPageSize[4];			// +0x08
	UByte		fNumEntries[4];			// +0x0c
	UByte		fBaseAddress[4];		// +0x10  the address the package was linked at

	ULong		Reserved(void) const		{ return GetBigEndianWord(fReserved); }
	ULong		RelocationSize(void) const	{ return GetBigEndianWord(fRelocationSize); }
	ULong		PageSize(void) const		{ return GetBigEndianWord(fPageSize); }
	ULong		NumEntries(void) const		{ return GetBigEndianWord(fNumEntries); }
	ULong		BaseAddress(void) const		{ return GetBigEndianWord(fBaseAddress); }
};
const long kRelocationHeaderSize = 0x14;

struct RelocationEntry
{
	UByte		fPageNumber[2];
	UByte		fOffsetCount[2];
	UByte		fOffsets[1];			// [fOffsetCount] bytes: word offsets within the page

	unsigned int	PageNumber(void) const	{ return GetBigEndianHalf(fPageNumber); }
	unsigned int	OffsetCount(void) const	{ return GetBigEndianHalf(fOffsetCount); }
};

Boolean	IsPackageHeader(const void* data, ULong size);		// the signature is there (size at least a header's)


/*------------------------------------------------------------------------------
	T P r i v a t e P a c k a g e I t e r a t o r
	Over a package in memory.
------------------------------------------------------------------------------*/

class TPrivatePackageIterator
{
public:
					TPrivatePackageIterator();
					~TPrivatePackageIterator();

	NewtonErr		Init(void* package);
	void			DisposeDirectory(void);
	NewtonErr		CheckHeader(void);
	NewtonErr		ComputeSizeOfEntriesAndData(ULong& entriesSize, ULong& dataSize);
	NewtonErr		SetupRelocationData(ULong directoryOffset, ULong* relocationSize);
	NewtonErr		GetRelocationChunkInfo(void);
	NewtonErr		VerifyPackage(void);

	ULong			NumberOfParts(void);
	ULong			PackageSize(void);
	const UniChar*	PackageName(void);
	ULong			GetPartDataOffset(ULong partIndex);
	void			GetPartInfoDesc(ULong partIndex, PartInfo* const info);
	void			GetPartInfo(ULong partIndex, PartInfo* const info);

	UByte*			fPackage;				// +0x04  the package's bytes (a memory source)
	PackageDirectory*	fDirectory;			// +0x08
	PartEntry*		fParts;					// +0x0c
	RelocationHeader*	fRelocationInfo;	// +0x10  nil: none
	UByte*			fRelocationData;		// +0x14  the entries
	UByte*			fDirectoryData;			// +0x18
	ULong			fPartsOffset;			// +0x1c  where the parts start in the package
};


/*------------------------------------------------------------------------------
	T P a c k a g e I t e r a t o r
	Over a package in memory or in a pipe.
------------------------------------------------------------------------------*/

class TPackageIterator : public TPrivatePackageIterator
{
public:
					TPackageIterator(CPipe* pipe);
					TPackageIterator(void* package);
					~TPackageIterator();

	NewtonErr		Init(void);
	void			DisposeDirectory(void);
	NewtonErr		ComputeSizeOfEntriesAndData(ULong& entriesSize, ULong& dataSize);
	NewtonErr		SetupRelocationData(ULong directoryOffset, ULong* relocationSize);
	NewtonErr		GetRelocationChunkInfo(void);
	NewtonErr		VerifyPackage(void);

	ULong			NumberOfParts(void);
	ULong			PackageSize(void);
	ULong			DirectorySize(void);
	ULong			GetPackageId(void);
	ULong			GetVersion(void);
	ULong			CreationDate(void);
	ULong			ModifyDate(void);
	ULong			PackageFlags(void);
	Boolean			ForDispatchOnly(void);
	Boolean			CopyProtected(void);
	const UniChar*	Copyright(void);
	const UniChar*	PackageName(void)		{ return TPrivatePackageIterator::PackageName(); }
	void			GetPartInfo(ULong partIndex, PartInfo* const info);
	ULong			ProcessorTypeOfPart(ULong partIndex);
	ULong			GetPartDataOffset(ULong partIndex)	{ return TPrivatePackageIterator::GetPartDataOffset(partIndex); }
	// NOT YET RECONSTRUCTED: Store(TStore*, ULong, TCallbackCompressor*, TLOCallback*) - the package as a large object on a store

	Boolean			fFromPipe;				// +0x20
	CPipe*			fPipe;					// +0x24
};

#endif	/* __PACKAGEITERATOR_H */
