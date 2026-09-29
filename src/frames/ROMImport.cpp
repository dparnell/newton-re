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
		ObjHeader* o = gROMObjectArea.ObjectAt(ref - 1);
		return o == nil ? NILREF : MAKEPTR(o);
	}
	return (Ref) (Long) (int) ref;
}


const void*
ROMImageBase(ULong* size)
{
	if (size != nil)
		*size = gROMImageSize;
	return gROMImageBase;
}

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

	// the objects (every ref points within the area)
	NewtonErr err = gROMObjectArea.Import(rom + kROMSoupBase, kROMSoupBase, kROMSoupSize, nil, nil);
	if (err != noErr)
		return err;

	// the tables and constants
	gROMSymbolTableRef = gROMObjectArea.TranslateRef(kROMSymbolTable);
	long mpCount = GetBigEndianWord(rom + kROMMagicPointerTable);
	if (kROMMagicPointerTable + (mpCount + 1) * kARMWord > imageSize)
		return kError_Bad_Parameters;
	Ref* mpTable = (Ref*) malloc(mpCount * sizeof(Ref));
	if (mpTable == nil)
		return kError_No_Memory;
	for (long j = 0; j < mpCount; j++)
		mpTable[j] = gROMObjectArea.TranslateRef(GetBigEndianWord(rom + kROMMagicPointerTable + (j + 1) * kARMWord));
	gMagicPointerTables[0] = mpTable;
	gMagicPointerTableCounts[0] = mpCount;
	for (long j = 0; j < gROMConstantCount; j++)
		*gROMConstantEntries[j].fRef = gROMObjectArea.TranslateRef(gROMConstantEntries[j].fROMRef);
	for (long j = 0; j < gRSSymbolCount; j++)
		*gRSSymbolEntries[j].fRef = gROMObjectArea.TranslateRef(gRSSymbolEntries[j].fROMRef);
	gROMBuiltinFunctions = Rbuiltinfunctions;
	return noErr;
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
