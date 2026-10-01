/*
	File:		frames/ROMImport.cpp

	Contains:	The ROM object importer (ROMImport.h): the ROM's object area
				read into a host object area (TImportedObjectArea,
				ObjectAreaImport.h), the constants and tables the object
				system uses set from it.
*/

#include "ROMImport.h"
#include "ObjectAreaImport.h"
#include "ObjectHeap.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "ByteOrder.h"
#include "ROMExtension.h"
#include "OSErrors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static TImportedObjectArea	gROMObjectArea;

// the other blocks of ROM data an object file carries (ImportBuiltObjects)
struct BuiltBlock
{
	ULong					fAddress;
	ULong					fLength;
	const unsigned char*	fData;
};
static BuiltBlock*			gBuiltBlocks = nil;
static long					gBuiltBlockCount = 0;

// the objects an object file laid out afresh has moved: the ROM's ref, then
// the ref now, sorted by the first (big-endian words, in the file)
static const unsigned char*	gMoved = nil;
static long					gMovedCount = 0;


// A ROM ref as the object file has it: the ROM's own, unless the object
// was moved.  The constants the C++ names (ROMConstants.h, RSSymbols.h)
// are the ROM's addresses; this is how they find their objects in an area
// laid out afresh.
static ULong32
MovedRef(ULong32 ref)
{
	long lo = 0, hi = gMovedCount - 1;
	while (lo <= hi)
	{
		long mid = (lo + hi) / 2;
		ULong32 old = GetBigEndianWord(gMoved + mid * 8);
		if (old == ref)
			return GetBigEndianWord(gMoved + mid * 8 + 4);
		if (old < ref)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return ref;
}
static const unsigned char*	gROMImageBase = nil;	// the ROM's bytes, address 0 first
static ULong				gROMImageSize = 0;
Ref						gROMSymbolTableRef = NILREF;


Boolean
ROMObjectsImported(void)
{
	return gROMObjectArea.fArea != nil;
}


long
ROMObjectCount(void)
{
	return gROMObjectArea.fCount;
}


void
ROMObjectAreaBounds(char** start, char** end)
{
	*start = gROMObjectArea.fArea;
	*end = gROMObjectArea.fAreaEnd;
}


Boolean
InROMObjectArea(Ref r)
{
	return gROMObjectArea.Contains(r);
}


// A ROM ref as a host ref: a pointer into the ROM's object area becomes
// a pointer to the imported object (nil when it is not one).
Ref
TranslateROMRef(ULong32 ref)
{
	if ((ref & 3) == kTagPointer)
	{
		ref = MovedRef(ref);
		ObjHeader* o = gROMObjectArea.ObjectAt(ref - 1);
		return o == nil ? NILREF : MAKEPTR(o);
	}
	return (Ref) (Long) (int) ref;
}


ULong
ROMMovedAddress(ULong address)
{
	return (ULong) MovedRef((ULong32) address);
}


const void*
ROMBytesAt(ULong address, ULong length)
{
	if (gROMImageBase != nil && address + length <= gROMImageSize && address + length >= address)
		return gROMImageBase + address;
	for (long i = 0; i < gBuiltBlockCount; i++)
		if (address >= gBuiltBlocks[i].fAddress && address + length <= gBuiltBlocks[i].fAddress + gBuiltBlocks[i].fLength)
			return gBuiltBlocks[i].fData + (address - gBuiltBlocks[i].fAddress);
	return nil;
}


long
ROMRegionCount(void)
{
	return (gROMImageBase != nil ? 1 : 0) + gBuiltBlockCount;
}


const void*
ROMRegion(long index, ULong* address, ULong* size)
{
	if (gROMImageBase != nil)
	{
		if (index == 0)
		{
			*address = 0;
			*size = gROMImageSize;
			return gROMImageBase;
		}
		index--;
	}
	if (index < 0 || index >= gBuiltBlockCount)
		return nil;
	*address = gBuiltBlocks[index].fAddress;
	*size = gBuiltBlocks[index].fLength;
	return gBuiltBlocks[index].fData;
}


Boolean
ROMAddressOf(const void* p, ULong* address, const void** regionStart)
{
	const unsigned char* q = (const unsigned char*) p;
	for (long i = 0; i < ROMRegionCount(); i++)
	{
		ULong base, size;
		const unsigned char* region = (const unsigned char*) ROMRegion(i, &base, &size);
		if (region != nil && q >= region && q < region + size)
		{
			if (address != nil)
				*address = base + (ULong) (q - region);
			if (regionStart != nil)
				*regionStart = region;
			return true;
		}
	}
	return false;
}


const void*
ROMImageBase(ULong* size)
{
	if (size != nil)
		*size = gROMImageSize;
	return gROMImageBase;
}

static NewtonErr	ImportObjectArea(const unsigned char* area, ULong base, ULong size, const unsigned char* magic, long mpCount);

NewtonErr
ImportROMObjects(const void* image, ULong imageSize)
{
	const unsigned char* rom = (const unsigned char*) image;
	if (imageSize > 8 && GetBigEndianWord(rom) == 0xe1a00000 && GetBigEndianWord(rom + 4) == 0xe1a00000)
	{
		rom += 0x80;								// an AIF image: the header, then the ROM
		imageSize -= 0x80;
	}
	if (gROMObjectArea.fArea != nil)
		return noErr;
	gROMImageBase = rom;
	gROMImageSize = imageSize;
	if ((ULong) kROMSoupBase + kROMSoupSize > imageSize || kROMMagicPointerTable + kARMWord > imageSize)
		return kError_Bad_Parameters;

	if (kROMMagicPointerTable + kARMWord > imageSize
	 || kROMMagicPointerTable + (GetBigEndianWord(rom + kROMMagicPointerTable) + 1) * kARMWord > imageSize)
		return kError_Bad_Parameters;
	return ImportObjectArea(rom + kROMSoupBase, kROMSoupBase, kROMSoupSize, rom + kROMMagicPointerTable + kARMWord,
							GetBigEndianWord(rom + kROMMagicPointerTable));
}


// The objects and the magic pointers, wherever they came from: the area's
// bytes (the ROM's layout, at kROMSoupBase) and the table's big-endian refs.
static NewtonErr
ImportObjectArea(const unsigned char* area, ULong base, ULong size, const unsigned char* magic, long mpCount)
{
	// the objects (every ref points within the area)
	NewtonErr err = gROMObjectArea.Import(area, base, size, nil, nil);
	if (err != noErr)
		return err;

	// the tables and constants (the ROM's addresses: moved, when the area was
	// laid out afresh)
	gROMSymbolTableRef = gROMObjectArea.TranslateRef(MovedRef(kROMSymbolTable));
	Ref* mpTable = (Ref*) malloc(mpCount * sizeof(Ref));
	if (mpTable == nil)
		return kError_No_Memory;
	for (long j = 0; j < mpCount; j++)
		mpTable[j] = gROMObjectArea.TranslateRef(GetBigEndianWord(magic + j * kARMWord));
	gMagicPointerTables[0] = mpTable;
	gMagicPointerTableCounts[0] = mpCount;
	for (long j = 0; j < gROMConstantCount; j++)
		*gROMConstantEntries[j].fRef = gROMObjectArea.TranslateRef(MovedRef(gROMConstantEntries[j].fROMRef));
	for (long j = 0; j < gRSSymbolCount; j++)
		*gRSSymbolEntries[j].fRef = gROMObjectArea.TranslateRef(MovedRef(gRSSymbolEntries[j].fROMRef));
	gROMBuiltinFunctions = Rbuiltinfunctions;
	return noErr;
}


// The object file the ROM-free track's builder writes (romsrc.py build -o;
// docs/rom-free/README.md): "NewtObjs", then as big-endian words the
// version, the area's base and size, the magic-pointer table's address and
// count; then the area; then the magic pointers; (2) the other blocks of
// ROM data; (3) the objects that are not where the ROM has them.  The
// constants the C++ names are the ROM's addresses, looked up in that last
// table, so an area laid out afresh (an edit that moves objects) needs no
// new build of the host.  No ROM image is behind it, so
// ROMImageBase answers nil: the ROM extension's packages, the recognisers'
// lexicons and the ROM code a package's native code calls are not there.
NewtonErr
ImportBuiltObjects(const void* data, ULong size)
{
	const unsigned char* p = (const unsigned char*) data;
	ULong version = size >= 12 ? GetBigEndianWord(p + 8) : 0;
	if (size < 28 || memcmp(p, "NewtObjs", 8) != 0 || version < 1 || version > 3)
		return kError_Bad_Parameters;
	ULong base = GetBigEndianWord(p + 12);
	ULong areaSize = GetBigEndianWord(p + 16);
	long mpCount = GetBigEndianWord(p + 24);
	if (28 + areaSize + mpCount * kARMWord > size)
		return kError_Bad_Parameters;
	if (gROMObjectArea.fArea != nil)
		return noErr;
	// (version 2) the other blocks of ROM data: the lexicons
	ULong at = 28 + areaSize + mpCount * kARMWord;
	if (version >= 2 && at + kARMWord <= size)
	{
		long count = GetBigEndianWord(p + at);
		at += kARMWord;
		gBuiltBlocks = (BuiltBlock*) calloc(count > 0 ? count : 1, sizeof(BuiltBlock));
		if (gBuiltBlocks == nil)
			return kError_No_Memory;
		for (long i = 0; i < count && at + 2 * kARMWord <= size; i++)
		{
			ULong length = GetBigEndianWord(p + at + kARMWord);
			if (at + 2 * kARMWord + length > size)
				return kError_Bad_Parameters;
			gBuiltBlocks[i].fAddress = GetBigEndianWord(p + at);
			gBuiltBlocks[i].fLength = length;
			gBuiltBlocks[i].fData = p + at + 2 * kARMWord;
			gBuiltBlockCount = i + 1;
			at += 2 * kARMWord + ((length + 3) & ~3);
		}
	}
	// (version 3) the objects laid out afresh: where each that moved is now
	if (version >= 3 && at + kARMWord <= size)
	{
		long count = GetBigEndianWord(p + at);
		if (at + kARMWord + count * 8 > size)
			return kError_Bad_Parameters;
		gMoved = p + at + kARMWord;
		gMovedCount = count;
	}
	return ImportObjectArea(p + 28, base, areaSize, p + 28 + areaSize, mpCount);
}


NewtonErr
ImportBuiltObjectsFromFile(const char* path)
{
	FILE* f = fopen(path, "rb");
	if (f == nil)
		return kError_Bad_Parameters;
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	void* data = malloc(size);
	if (data == nil)
	{
		fclose(f);
		return kError_No_Memory;
	}
	NewtonErr err = fread(data, 1, size, f) == (size_t) size ? noErr : kError_Bad_Parameters;
	fclose(f);
	if (err == noErr)
		err = ImportBuiltObjects(data, size);
	if (err != noErr || !ROMObjectsImported())
		free(data);
	// else kept, as an image is: the imported objects may point into it
	return err;
}


// The ROM extension read in beside the base ROM.
//
// A MessagePad's ROM is one piece: the base ROM and then, at ROM$$Size,
// the extension with the packages the machine boots with.  The files in
// DebugRom keep them apart - "<name> image" is the base ROM as an AIF
// image and "<name> high" the extension - and the extraction tool puts
// them back together into rom.bin (tools/newton-rom/extract_rom.py).  A
// host program handed the image alone would have the objects but none of
// the packages, so the extension is looked for beside it and put where
// the device has it: the header says which address that is.
//
// The region it lands on is the image's debug symbol area, which nothing
// reads at run time and which sits above everything the object importer
// wants (the object area ends at 0x637f28, the extension starts at
// 0x6f2e9c).  No extension file, or one that does not fit, simply leaves
// the ROM without packages.
static void
SpliceROMExtension(const char* imagePath, unsigned char* image, long imageSize)
{
	const char* tail = "image";
	size_t pathLength = strlen(imagePath);
	size_t tailLength = strlen(tail);
	if (pathLength < tailLength || strcmp(imagePath + pathLength - tailLength, tail) != 0)
		return;
	char* rexPath = (char*) malloc(pathLength + 1);
	if (rexPath == nil)
		return;
	memcpy(rexPath, imagePath, pathLength - tailLength);
	strcpy(rexPath + pathLength - tailLength, "high");
	FILE* f = fopen(rexPath, "rb");
	free(rexPath);
	if (f == nil)
		return;
	fseek(f, 0, SEEK_END);
	long rexSize = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char* rex = (unsigned char*) malloc(rexSize);
	if (rex == nil)
	{
		fclose(f);
		return;
	}
	Boolean read = fread(rex, 1, rexSize, f) == (size_t) rexSize;
	fclose(f);
	long romOffset = 0;
	if (imageSize > 8 && GetBigEndianWord(image) == 0xe1a00000 && GetBigEndianWord(image + 4) == 0xe1a00000)
		romOffset = 0x80;							// an AIF image: the header, then the ROM
	if (read && rexSize > 0x24 && GetBigEndianWord(rex) == kRExSignatureA
			 && GetBigEndianWord(rex + 4) == kRExSignatureB)
	{
		ULong start = GetBigEndianWord(rex + 0x20);	// where the device has it
		if ((long) start + rexSize <= imageSize - romOffset)
			memcpy(image + romOffset + start, rex, rexSize);
	}
	free(rex);
}

NewtonErr
ImportROMObjectsFromFile(const char* path)
{
	FILE* f = fopen(path, "rb");
	if (f == nil)
		return kError_Bad_Parameters;
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	void* image = malloc(size);
	if (image == nil)
	{
		fclose(f);
		return kError_No_Memory;
	}
	NewtonErr err = noErr;
	if (fread(image, 1, size, f) != (size_t) size)
		err = kError_Bad_Parameters;
	fclose(f);
	if (err == noErr && size >= 8 && memcmp(image, "NewtObjs", 8) == 0)
	{
		// the ROM-free track's object file in place of an image
		err = ImportBuiltObjects(image, size);
		if (err != noErr || !ROMObjectsImported())
			free(image);
		return err;
	}
	if (err == noErr)
		SpliceROMExtension(path, (unsigned char*) image, size);
	if (err == noErr)
		err = ImportROMObjects(image, size);
	if (err != noErr || !ROMObjectsImported())
		free(image);
	// else the image is kept: the objects point into it, and so do the
	// ROM extension's packages (ROMImageBase)
	return err;
}
