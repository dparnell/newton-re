/*
	File:		frames/ObjectAreaImport.cpp

	Contains:	TImportedObjectArea (ObjectAreaImport.h): an area of the
				MessagePad's objects read into a host object area.

	Not a reconstruction (see the header).
*/

#include "ObjectAreaImport.h"
#include "ObjectHeap.h"
#include "ByteOrder.h"
#include "OSErrors.h"

#include <stdlib.h>
#include <string.h>


TImportedObjectArea::TImportedObjectArea()
{
	fArea = nil;
	fAreaEnd = nil;
	fObjects = nil;
	fCount = 0;
	fBase = 0;
	fSize = 0;
	fOutside = nil;
	fRefCon = nil;
}


TImportedObjectArea::~TImportedObjectArea()
{
	Dispose();
}


void
TImportedObjectArea::Dispose(void)
{
	free(fArea);
	free(fObjects);
	fArea = nil;
	fAreaEnd = nil;
	fObjects = nil;
	fCount = 0;
}


// The host object for a source address (nil when it is not an object's).
ObjHeader*
TImportedObjectArea::ObjectAt(ULong32 address) const
{
	long low = 0;
	long high = fCount - 1;
	while (low <= high)
	{
		long mid = (low + high) / 2;
		if (fObjects[mid].fAddress == address)
			return fObjects[mid].fObject;
		if (fObjects[mid].fAddress < address)
			low = mid + 1;
		else
			high = mid - 1;
	}
	return nil;
}


// A source ref as a host ref: a pointer into the area becomes a pointer
// to the host object, one outside goes to the translator; everything else
// (integers, immediates, magic pointers) is the same on both, sign-
// extended so that a negative integer stays negative.
Ref
TImportedObjectArea::TranslateRef(ULong32 ref) const
{
	if ((ref & 3) == kTagPointer)
	{
		ObjHeader* o = ObjectAt(ref - 1);
		if (o != nil)
			return MAKEPTR(o);
		if (fOutside != nil)
		{
			Ref outside = fOutside(ref, fRefCon);
			if (outside != NILREF)
				return outside;
		}
	}
	return (Ref) (Long) (int) ref;
}


// The name of the symbol a source ref points at (within the area), nil
// if it is not one.
static const char*
SourceSymbolName(const unsigned char* bytes, ULong32 base, ULong32 size, ULong32 ref)
{
	if ((ref & 3) != kTagPointer || ref - 1 < base || ref - 1 + kARMObjBodySize + 4 > base + size)
		return nil;
	ULong32 obj = ref - 1 - base;
	if (GetBigEndianWord(bytes + obj + kARMObjHeaderSize) != (ULong32) kSymbolClass)
		return nil;
	return (const char*) bytes + obj + kARMObjBodySize + 4;
}


NewtonErr
TImportedObjectArea::Import(const unsigned char* bytes, ULong32 base, ULong32 size, OutsideRefTranslator outside, void* refCon, long align)
{
	Dispose();
	fBase = base;
	fSize = size;
	fOutside = outside;
	fRefCon = refCon;

	// the objects and what they take here
	long count = 0;
	ULong total = 0;
	ULong32 a = 0;
	while (a < size)
	{
		ULong32 header = GetBigEndianWord(bytes + a);
		ULong32 objSize = header >> 8;
		if (objSize < kARMObjBodySize || a + objSize > size)
			return kError_Bad_Parameters;
		ULong hostSize;
		if ((header & kObjSlotted) != 0)
			hostSize = ArrayObjSize((objSize - kARMObjBodySize) / kARMWord);
		else if ((header & kObjFrame) != 0)
			return kError_Bad_Parameters;			// an indirect binary cannot be in a read-only area
		else
			hostSize = BinaryObjSize(objSize - kARMObjBodySize);
		total += AlignedSize(hostSize);
		count++;
		a += (objSize + align - 1) & ~(align - 1);
	}
	if (a != size)
		return kError_Bad_Parameters;

	// the host area: outside the object heap (and the kernel heap)
	char* area = (char*) malloc(total + kObjAlign);
	ImportedObjectEntry* entries = (ImportedObjectEntry*) malloc(count * sizeof(ImportedObjectEntry));
	if (area == nil || entries == nil)
	{
		free(area);
		free(entries);
		return kError_No_Memory;
	}
	fArea = (char*) AlignedSize((ULong) area);
	fAreaEnd = fArea + total;
	fObjects = entries;
	fCount = count;

	// the objects, refs still the source's
	char* p = fArea;
	long i = 0;
	for (a = 0; a < size; i++)
	{
		ULong32 header = GetBigEndianWord(bytes + a);
		ULong32 objSize = header >> 8;
		ObjHeader* o = (ObjHeader*) p;
		entries[i].fAddress = base + a;
		entries[i].fObject = o;
		ULong hostSize;
		if ((header & kObjSlotted) != 0)
		{
			long slots = (objSize - kARMObjBodySize) / kARMWord;
			hostSize = ArrayObjSize(slots);
			Ref* slot = ObjSlots(o);
			for (long j = 0; j <= slots; j++)
				slot[j] = (Ref) GetBigEndianWord(bytes + a + kARMObjHeaderSize + j * kARMWord);
		}
		else
		{
			long length = objSize - kARMObjBodySize;
			hostSize = BinaryObjSize(length);
			ObjClass(o) = (Ref) GetBigEndianWord(bytes + a + kARMObjHeaderSize);
			memcpy(ObjData(o), bytes + a + kARMObjBodySize, length);
		}
		o->fSizeAndFlags = (hostSize << kObjSizeShift) | (header & 0xff);
		o->fGCStuff = 0;
		p += AlignedSize(hostSize);
		a += (objSize + align - 1) & ~(align - 1);
	}

	// translate the refs, and the binary data whose words the host reads
	i = 0;
	for (a = 0; a < size; i++)
	{
		ObjHeader* o = entries[i].fObject;
		ULong32 header = GetBigEndianWord(bytes + a);
		ULong32 objSize = header >> 8;
		if ((header & kObjSlotted) != 0)
		{
			long slots = (objSize - kARMObjBodySize) / kARMWord;
			Ref* slot = ObjSlots(o);
			for (long j = 0; j <= slots; j++)
				slot[j] = TranslateRef((ULong32) slot[j]);
		}
		else
		{
			ULong32 sourceClass = (ULong32) ObjClass(o);
			ObjClass(o) = TranslateRef(sourceClass);
			long length = objSize - kARMObjBodySize;
			if (sourceClass == (ULong32) kSymbolClass)
				ObjSymbol(o)->fHash = GetBigEndianWord(ObjData(o));
			else
			{
				const char* className = SourceSymbolName(bytes, base, size, sourceClass);
				if (className == nil && fOutside != nil)
				{
					// a class in another area (the ROM's 'real or 'string)
					Ref hostClass = ObjClass(o);
					if (ISPTR(hostClass) && IsSymbol(hostClass))
						className = SymbolName(hostClass);
				}
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
		a += (objSize + align - 1) & ~(align - 1);
	}
	return noErr;
}
