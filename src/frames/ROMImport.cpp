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
#include "OSErrors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static TImportedObjectArea	gROMObjectArea;
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
		err = ImportROMObjects(image, size);
	free(image);
	return err;
}
