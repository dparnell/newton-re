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
ImportFramesPart(const void* part, ULong size, ULong32 refBase)
{
	FramesPartArea* entry = new FramesPartArea;
	if (entry == nil)
		return nil;
	if (entry->fArea.Import((const unsigned char*) part, refBase, (ULong32) size, OutsidePartRef, nil) != noErr)
	{
		delete entry;
		return nil;
	}
	entry->fNext = gFramesParts;
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
			delete entry;
			return;
		}
	}
}


Boolean
InFramesPartArea(Ref r)
{
	for (FramesPartArea* entry = gFramesParts; entry != nil; entry = entry->fNext)
		if (entry->fArea.Contains(r))
			return true;
	return false;
}


// ROM 0x000d2898 FramePartToplevelFrame__FPv
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
