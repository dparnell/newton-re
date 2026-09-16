/*
	File:		frames/ROMImport.cpp

	Contains:	The ROM object importer (ROMImport.h): the ROM's object area
				read into a host object area, refs translated, the constants
				and tables the object system uses set from it.

	The ROM's objects are read with the ROM's layout in mind (ObjectHeap.h
	describes both): a header of two 32-bit words - size << 8 | flags, and
	the GC word - the class slot at +8 and slots or data from +0xc, blocks
	rounded to 4.  Their bytes are big-endian; refs, symbol hashes, reals
	and strings become the host's, other binary data (bitmaps, bytecode,
	sound) keeps the persistent format its readers expect.
*/

#include "ROMImport.h"
#include "ObjectHeap.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "ByteOrder.h"
#include "OSErrors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// the ROM's layout
const ULong kROMObjHeaderSize = 8;
const ULong kROMObjBodySize = 12;
const ULong kROMWord = 4;

struct ROMObjectEntry
{
	ULong32		fROMAddress;		// the object's address in the ROM
	ObjHeader*	fObject;			// its host object
};

static char*			gROMObjectArea = nil;
static char*			gROMObjectAreaEnd = nil;
static ROMObjectEntry*	gROMObjects = nil;
static long				gROMObjectCount = 0;
Ref						gROMSymbolTableRef = NILREF;


Boolean
ROMObjectsImported(void)
{
	return gROMObjectArea != nil;
}


long
ROMObjectCount(void)
{
	return gROMObjectCount;
}


Boolean
InROMObjectArea(Ref r)
{
	return (char*) r >= gROMObjectArea && (char*) r < gROMObjectAreaEnd;
}


// The host object for a ROM address (nil when it is not an object's).
static ObjHeader*
ROMObjectAt(ULong32 address)
{
	long low = 0;
	long high = gROMObjectCount - 1;
	while (low <= high)
	{
		long mid = (low + high) / 2;
		if (gROMObjects[mid].fROMAddress == address)
			return gROMObjects[mid].fObject;
		if (gROMObjects[mid].fROMAddress < address)
			low = mid + 1;
		else
			high = mid - 1;
	}
	return nil;
}


// A ROM ref as a host ref: a pointer into the object area becomes a
// pointer to the host object; everything else (integers, immediates,
// magic pointers) is the same on both.
static Ref
TranslateROMRef(ULong32 ref)
{
	if ((ref & 3) == kTagPointer)
	{
		ObjHeader* o = ROMObjectAt(ref - 1);
		if (o != nil)
			return MAKEPTR(o);
	}
	return (Ref) (Long) (int) ref;			// sign-extended: a negative integer stays negative
}


// The name of the ROM symbol a ROM ref points at, nil if it is not one.
static const char*
ROMSymbolName(const unsigned char* rom, ULong32 ref)
{
	if ((ref & 3) != kTagPointer)
		return nil;
	ULong32 obj = ref - 1;
	if (GetBigEndianWord(rom + obj + kROMObjHeaderSize) != (ULong32) kSymbolClass)
		return nil;
	return (const char*) rom + obj + kROMObjBodySize + 4;
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
	if (gROMObjectArea != nil)
		return noErr;
	if ((ULong) kROMSoupBase + kROMSoupSize > imageSize || kROMMagicPointerTable + kROMWord > imageSize)
		return kError_Bad_Parameters;

	// the objects and what they take here
	long count = 0;
	ULong total = 0;
	ULong32 a = kROMSoupBase;
	ULong32 end = kROMSoupBase + kROMSoupSize;
	while (a < end)
	{
		ULong32 header = GetBigEndianWord(rom + a);
		ULong32 size = header >> 8;
		if (size < kROMObjBodySize || a + size > end)
			return kError_Bad_Parameters;
		ULong hostSize;
		if ((header & kObjSlotted) != 0)
			hostSize = ArrayObjSize((size - kROMObjBodySize) / kROMWord);
		else if ((header & kObjFrame) != 0)
			return kError_Bad_Parameters;			// an indirect binary cannot be in ROM
		else
			hostSize = BinaryObjSize(size - kROMObjBodySize);
		total += AlignedSize(hostSize);
		count++;
		a += (size + 3) & ~3;
	}
	if (a != end)
		return kError_Bad_Parameters;

	// the host's ROM: outside the object heap (and the kernel heap - it is
	// the size of the ROM's object area and more)
	char* area = (char*) malloc(total + kObjAlign);
	ROMObjectEntry* entries = (ROMObjectEntry*) malloc(count * sizeof(ROMObjectEntry));
	if (area == nil || entries == nil)
	{
		free(area);
		free(entries);
		return kError_No_Memory;
	}
	gROMObjectArea = (char*) AlignedSize((ULong) area);
	gROMObjectAreaEnd = gROMObjectArea + total;
	gROMObjects = entries;
	gROMObjectCount = count;

	// the objects, refs still the ROM's
	char* p = gROMObjectArea;
	long i = 0;
	for (a = kROMSoupBase; a < end; i++)
	{
		ULong32 header = GetBigEndianWord(rom + a);
		ULong32 size = header >> 8;
		ObjHeader* o = (ObjHeader*) p;
		entries[i].fROMAddress = a;
		entries[i].fObject = o;
		ULong hostSize;
		if ((header & kObjSlotted) != 0)
		{
			long slots = (size - kROMObjBodySize) / kROMWord;
			hostSize = ArrayObjSize(slots);
			Ref* slot = ObjSlots(o);
			for (long j = 0; j <= slots; j++)
				slot[j] = (Ref) GetBigEndianWord(rom + a + kROMObjHeaderSize + j * kROMWord);
		}
		else
		{
			long length = size - kROMObjBodySize;
			hostSize = BinaryObjSize(length);
			ObjClass(o) = (Ref) GetBigEndianWord(rom + a + kROMObjHeaderSize);
			memcpy(ObjData(o), rom + a + kROMObjBodySize, length);
		}
		o->fSizeAndFlags = (hostSize << kObjSizeShift) | (header & 0xff);
		o->fGCStuff = 0;
		p += AlignedSize(hostSize);
		a += (size + 3) & ~3;
	}

	// translate the refs, and the binary data whose words the host reads
	i = 0;
	for (a = kROMSoupBase; a < end; i++)
	{
		ObjHeader* o = entries[i].fObject;
		ULong32 header = GetBigEndianWord(rom + a);
		ULong32 size = header >> 8;
		if ((header & kObjSlotted) != 0)
		{
			long slots = (size - kROMObjBodySize) / kROMWord;
			Ref* slot = ObjSlots(o);
			for (long j = 0; j <= slots; j++)
				slot[j] = TranslateROMRef((ULong32) slot[j]);
		}
		else
		{
			ULong32 romClass = (ULong32) ObjClass(o);
			ObjClass(o) = TranslateROMRef(romClass);
			long length = size - kROMObjBodySize;
			if (romClass == (ULong32) kSymbolClass)
				ObjSymbol(o)->fHash = GetBigEndianWord(ObjData(o));
			else
			{
				const char* className = ROMSymbolName(rom, romClass);
				if (className != nil && strcmp(className, "real") == 0 && length == 8)
				{
					unsigned char* d = (unsigned char*) ObjData(o);
					ULong32 hi = GetBigEndianWord(d);
					ULong32 lo = GetBigEndianWord(d + 4);
					unsigned long long bits = ((unsigned long long) hi << 32) | lo;
					double value;
					memcpy(&value, &bits, sizeof(double));
					memcpy(d, &value, sizeof(double));
				}
				else if (className != nil && (strcmp(className, "string") == 0 || strncmp(className, "string.", 7) == 0))
				{
					UniChar* s = (UniChar*) ObjData(o);
					for (long j = 0; j < length / 2; j++)
						s[j] = GetBigEndianHalf((const unsigned char*) ObjData(o) + j * 2);
				}
			}
		}
		a += (size + 3) & ~3;
	}

	// the tables and constants
	gROMSymbolTableRef = TranslateROMRef(kROMSymbolTable);
	long mpCount = GetBigEndianWord(rom + kROMMagicPointerTable);
	if (kROMMagicPointerTable + (mpCount + 1) * kROMWord > imageSize)
		return kError_Bad_Parameters;
	Ref* mpTable = (Ref*) malloc(mpCount * sizeof(Ref));
	if (mpTable == nil)
		return kError_No_Memory;
	for (long j = 0; j < mpCount; j++)
		mpTable[j] = TranslateROMRef(GetBigEndianWord(rom + kROMMagicPointerTable + (j + 1) * kROMWord));
	gMagicPointerTables[0] = mpTable;
	gMagicPointerTableCounts[0] = mpCount;
	for (long j = 0; j < gROMConstantCount; j++)
		*gROMConstantEntries[j].fRef = TranslateROMRef(gROMConstantEntries[j].fROMRef);
	for (long j = 0; j < gRSSymbolCount; j++)
		*gRSSymbolEntries[j].fRef = TranslateROMRef(gRSSymbolEntries[j].fROMRef);
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
