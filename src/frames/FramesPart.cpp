/*
	File:		frames/FramesPart.cpp

	Contains:	Frames parts (FramesPart.h): the import of a part's object
				area on the host, and FramePartToplevelFrame.
*/

#include "FramesPart.h"
#include "ROMImport.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "NSErrors.h"
#include "OSErrors.h"
#include <stdlib.h>

// the imported parts
struct FramesPartArea
{
	TImportedObjectArea	fArea;
	FramesPartArea*		fNext;
	Boolean				fProvisional;	// imported only to be looked at (FramesPart.h)
	Boolean				fDoomed;		// removed during a collection, given back after it
};
static FramesPartArea*	gFramesParts = nil;


// A ref that points outside the part: to the ROM's objects, when they
// are imported.
static Ref
OutsidePartRef(ULong32 ref, void* /*refCon*/)
{
	if (!ROMObjectsImported())
		return NILREF;
	return TranslateROMRef(ref);
}


TImportedObjectArea*
ImportFramesPart(const void* part, ULong size, ULong32 refBase, long align)
{
	FramesPartArea* entry = new FramesPartArea;
	if (entry == nil)
		return nil;
	if (entry->fArea.Import((const unsigned char*) part, refBase, (ULong32) size, OutsidePartRef, nil, align) != noErr)
	{
		delete entry;
		return nil;
	}
	entry->fNext = gFramesParts;
	entry->fProvisional = false;
	entry->fDoomed = false;
	gFramesParts = entry;
	return &entry->fArea;
}


// The part's area disposed: every ref into it in the heap becomes
// kDeclawedRef (the ROM declaws a removed package's range the same way).
void
RemoveFramesPart(TImportedObjectArea* area)
{
	for (FramesPartArea** link = &gFramesParts; *link != nil; link = &(*link)->fNext)
	{
		FramesPartArea* entry = *link;
		if (&entry->fArea == area)
		{
			*link = entry->fNext;
			if (gHeap != nil && RegisterRangeForDeclawing((ULong) area->fArea, (ULong) area->fAreaEnd))
				DeclawRefsInRegisteredRanges();
			// the host gives the area's memory back, and the next part
			// imported may be given the same addresses: a map the
			// find-offset cache remembers would then answer for another
			// map's slots (the ROM never reuses a package's range so soon)
			FindOffsetCacheClear();
			delete entry;
			return;
		}
	}
}


void
SetFramesPartProvisional(TImportedObjectArea* area, Boolean provisional)
{
	for (FramesPartArea* entry = gFramesParts; entry != nil; entry = entry->fNext)
		if (&entry->fArea == area)
			entry->fProvisional = provisional;
}


// The areas doomed during a collection given back once it is over (a GC
// proc): their refs were declawed by the collection itself.
static void
FreeDoomedFramesParts(void* /*refCon*/)
{
	for (FramesPartArea** link = &gFramesParts; *link != nil; )
	{
		FramesPartArea* entry = *link;
		if (entry->fDoomed)
		{
			*link = entry->fNext;
			FindOffsetCacheClear();
			delete entry;
		}
		else
			link = &entry->fNext;
	}
}


// A large binary is let go of in the middle of a collection (LBDestroy is
// its finaliser), when the heap cannot be walked again to declaw: the
// area's range is registered for the declawing that collection ends with,
// and the area itself is given back after it.
void
RemoveProvisionalFramesParts(const void* start, const void* end)
{
	const unsigned char* from = (const unsigned char*) start;
	const unsigned char* to = (const unsigned char*) end;
	for (FramesPartArea* entry = gFramesParts; entry != nil; )
	{
		FramesPartArea* next = entry->fNext;
		if (entry->fProvisional && !entry->fDoomed && entry->fArea.fBytes >= from && entry->fArea.fBytes < to)
		{
			if (gHeap != nil && gHeap->fInGC)
			{
				static Boolean registered = false;
				if (!registered)
				{
					GCRegister(&gFramesParts, FreeDoomedFramesParts);
					registered = true;
				}
				entry->fDoomed = true;
				RegisterRangeForDeclawing((ULong) entry->fArea.fArea, (ULong) entry->fArea.fAreaEnd);
			}
			else
				RemoveFramesPart(&entry->fArea);
		}
		entry = next;
	}
}


const void*
FramesPartSource(Ref r)
{
	for (FramesPartArea* entry = gFramesParts; entry != nil; entry = entry->fNext)
		if (!entry->fDoomed && entry->fArea.Contains(r))
			return entry->fArea.fBytes;
	return nil;
}


const unsigned char*
FramesPartObjectSource(Ref r, ULong32* address)
{
	for (FramesPartArea* entry = gFramesParts; entry != nil; entry = entry->fNext)
		if (!entry->fDoomed && entry->fArea.Contains(r))
		{
			ObjHeader* obj = OBJ(r);
			const TImportedObjectArea& area = entry->fArea;
			for (long i = 0; i < area.fCount; i++)
				if (area.fObjects[i].fObject == obj)
				{
					*address = area.fObjects[i].fAddress;
					return area.fBytes + (area.fObjects[i].fAddress - area.fBase);
				}
			return nil;
		}
	return nil;
}


Boolean
InFramesPartArea(Ref r)
{
	for (FramesPartArea* entry = gFramesParts; entry != nil; entry = entry->fNext)
		if (entry->fArea.Contains(r))
			return true;
	return false;
}


TImportedObjectArea*
FindFramesPart(const void* part)
{
	for (FramesPartArea* p = gFramesParts; p != nil; p = p->fNext)
		if (p->fArea.fBytes == (const unsigned char*) part)
			return &p->fArea;
	return nil;
}


// ROM 0x000d1744 FramePartToplevelFrame__FPv
// The frame in the array the part begins with (the array's GC word must
// be clear: a real object, not a page of something else); nil otherwise.
// (The ROM also checks a part in the ROM domain's space is a valid large
// object address.)
Ref
FramePartToplevelFrame(void* part)
{
	ObjHeader* array = (ObjHeader*) part;
	if ((array->fGCStuff & ~(ULong) 1) != 0)
		return NILREF;
	return GetArraySlotRef(MAKEPTR(array), 0);
}
