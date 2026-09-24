/*
	File:		frames/Objects.cpp

	Contains:	The NewtonScript object system's API (the DDK's objects.h):
				allocation, lengths and flags, array and frame slots, frame
				maps (FindOffset and its cache, AddSlot, RemoveSlot, sorted and
				shared maps), paths, clones, classes, strings and reals,
				iteration, the exceptions the object system throws, and
				InitObjects.

	Reconstructed from the MP2x00 US ROM (0x00319510-0x002fb24c and the
	class functions at 0x002bec5c-0x002bedd8); each function cites its
	origin.  Where the ROM inlines a TObjectHeap method (AllocateBinary and
	friends) the wrapper calls it.
*/

#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "REPTranslators.h"
#include "RSSymbols.h"
#include "NSErrors.h"
#include "OSErrors.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"
#include "ROMConstants.h"
#include "UnicodeTables.h"
#include "hal/System.h"
#include "Unicode.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

long	gObjectHeapSize = 0x100000;		// host: the object heap's size (the ROM sizes it by InternalRAMInfo)


/* -------------------------------------------------------------------------------
	Exceptions
	Object-system errors travel as a frame {errorCode, value} whose RefStruct
	is the exception's data (ThrowRefException, DeleteRefStruct); the name
	says which kind.
------------------------------------------------------------------------------- */

// ROM 0x002f5708 DeleteRefStruct__FP9RefStruct
// The destructor of a thrown frame.
void
DeleteRefStruct(RefStruct* r)
{
	delete r;
}


// ROM 0x002f5730 ThrowRefException__FPcRC6RefVar
// Throw data (a ref) under a name that is an evt.ex... with type.ref data.
//
// With NEWTON_TRACE_EXCEPTIONS set in the environment the script stack is
// printed here, where it still stands - by the time the exception is
// caught and reported the interpreter has unwound and there is nothing
// left to see.  This is how a mistake in one of the ROM's own scripts is
// tracked down to the script that made it.
void
ThrowRefException(ExceptionName name, RefArg data)
{
	if (getenv("NEWTON_TRACE_EXCEPTIONS") != nil && gREPout != nil)
	{
		gREPout->Print("--- %s: ", name);
		PrintObject(data, 0);
		gREPout->Print("\n");
		StackTrace();
	}
	if (!Subexception(name, (ExceptionName) "evt.ex") || !Subexception(name, (ExceptionName) "type.ref"))
	{
		RefVar sym(Intern((char*) name));
		ThrowExFramesWithBadValue(kNSErrBadExceptionName, sym);
	}
	RefStruct* r = new RefStruct(data);		// (a RefStruct's handle survives ClearRefHandles)
	if (r == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	Throw(name, r, (ExceptionDestructor) DeleteRefStruct);
}


// ROM 0x0031a0dc ThrowBadTypeWithFrameData__FlRC6RefVar
void
ThrowBadTypeWithFrameData(NewtonErr errorCode, RefArg value)
{
	RefVar frame(AllocateFrame());
	SetFrameSlot(frame, RSSYMerrorcode, RefVar(MAKEINT(errorCode)));
	SetFrameSlot(frame, RSSYMvalue, value);
	ThrowRefException(exBadTypeWithFrameData, frame);
}


// ROM 0x0031ca58 ThrowExFramesWithBadValue__FlRC6RefVar
void
ThrowExFramesWithBadValue(NewtonErr errorCode, RefArg value)
{
	RefVar frame(AllocateFrame());
	SetFrameSlot(frame, RSSYMerrorcode, RefVar(MAKEINT(errorCode)));
	SetFrameSlot(frame, RSSYMvalue, value);
	ThrowRefException(exFramesWithFrameData, frame);
}


// ROM 0x002f5810 ThrowExInterpreterWithSymbol__FlRC6RefVar
void
ThrowExInterpreterWithSymbol(NewtonErr errorCode, RefArg sym)
{
	RefVar frame(AllocateFrame());
	SetFrameSlot(frame, RSSYMerrorcode, RefVar(MAKEINT(errorCode)));
	SetFrameSlot(frame, RSSYMsymbol, sym);
	ThrowRefException(exInterpreterWithFrameData, frame);
}


// The out-of-bounds frame the array accessors throw: {errorCode, value:
// the array, index} (the ROM builds it inline in each).
static void
ThrowOutOfBounds(Ref array, long index)
{
	RefVar frame(AllocateFrame());
	SetFrameSlot(frame, RSSYMerrorcode, RefVar(MAKEINT(kNSErrOutOfBounds)));
	SetFrameSlot(frame, RSSYMvalue, RefVar(array));
	SetFrameSlot(frame, RSSYMindex, RefVar(MAKEINT(index)));
	ThrowRefException(exFramesWithFrameData, frame);
}


/* -------------------------------------------------------------------------------
	Allocation
------------------------------------------------------------------------------- */

// ROM 0x0031be7c AllocateBinary__FRC6RefVarl
Ref
AllocateBinary(RefArg theClass, long length)
{
	return gHeap->AllocateBinary(theClass, length);
}


// ROM 0x0031be94 AllocateArray__FRC6RefVarl
Ref
AllocateArray(RefArg theClass, long length)
{
	return gHeap->AllocateArray(theClass, length);
}


// ROM 0x001292d0 MakeArray__Fl
// An array of class 'Array (the same length checks as the heap's).
Ref
MakeArray(long length)
{
	return gHeap->AllocateArray(RSSYMarray, length);
}


// ROM 0x001292e0 AddressToRef__FPv
// A pointer as an integer Ref (pointers are word aligned: the tag bits
// are free).
Ref
AddressToRef(void* p)
{
	return (Ref) ((uintptr_t) p & ~(uintptr_t) 3);
}


// ROM 0x001292ec RefToAddress__Fl
void*
RefToAddress(Ref r)
{
	if (!ISINT(r))
		_RINTError(r);
	return (void*) ((uintptr_t) r & ~(uintptr_t) 3);
}


// ROM 0x00129678 SetBoundsRect__FRC6RefVarRC5TRect
// The rect into the frame's left, top, right and bottom slots.
Ref
SetBoundsRect(RefArg frame, const Rect& r)
{
	SetFrameSlot(frame, RSSYMleft, RefVar(MAKEINT(r.left)));
	SetFrameSlot(frame, RSSYMtop, RefVar(MAKEINT(r.top)));
	SetFrameSlot(frame, RSSYMright, RefVar(MAKEINT(r.right)));
	SetFrameSlot(frame, RSSYMbottom, RefVar(MAKEINT(r.bottom)));
	return frame;
}


// ROM 0x0012975c ToObject__FRC5TRect
// A bounds frame (a clone of canonicalRect) for the rect.
Ref
ToObject(const Rect& r)
{
	RefVar frame(Clone(RefVar(Rcanonicalrect)));
	return SetBoundsRect(frame, r);
}


// ROM 0x00128bc0 FromObject__FRC6RefVarRs
// An integer Ref into a short; ==> whether it was one.
static Boolean
FromObject(RefArg obj, short& value)
{
	if (!ISINT(obj))
		return false;
	value = (short) RVALUE(obj);
	return true;
}


// ROM 0x001297a4 FromObject__FRC6RefVarR5TRect
// The rect from a bounds frame's top, left, bottom and right; ==> whether
// all four are integers.
Boolean
FromObject(RefArg obj, Rect& r)
{
	return FromObject(RefVar(GetFrameSlotRef(obj, RSSYMtop)), r.top)
		&& FromObject(RefVar(GetFrameSlotRef(obj, RSSYMleft)), r.left)
		&& FromObject(RefVar(GetFrameSlotRef(obj, RSSYMbottom)), r.bottom)
		&& FromObject(RefVar(GetFrameSlotRef(obj, RSSYMright)), r.right);
}


// ROM 0x0031beac AllocateFrame__Fv
Ref
AllocateFrame(void)
{
	return gHeap->AllocateFrame();
}


// ROM 0x0031bebc AllocateFrameWithMap__FRC6RefVar
Ref
AllocateFrameWithMap(RefArg map)
{
	return gHeap->AllocateFrameWithMap(map);
}


// ROM 0x0031d900 AllocateMapWithTags__FRC6RefVarT1
// A shared map holding the tags given (an array), flagged if _proto is one.
Ref
AllocateMapWithTags(RefArg superMap, RefArg tags)
{
	long count = Length(tags);
	Ref map = gHeap->AllocateMap(superMap, count);
	ObjHeader* tagsObj = OBJ(tags);
	MapFlags(PTRVALUE(map)) = kMapShared;
	Boolean hasProto = false;
	for (long i = 0; i < count; i++)
	{
		Ref tag = ObjArraySlots(tagsObj)[i];
		MapTags(PTRVALUE(map))[i] = tag;
		if (!hasProto && SymbolCompare(tag, RSSYM_proto) == 0)
		{
			hasProto = true;
			MapFlags(PTRVALUE(map)) |= kMapProto;
		}
	}
	return map;
}


// ROM 0x0031d9d0 CollectFrameTags1__FP3MapPlT2
// The tags of a map chain, supermaps first, into tags from *count on.
static void
CollectFrameTags1(ObjHeader* map, Ref* tags, long* count)
{
	if (MapSuperMap(map) != NILREF)
		CollectFrameTags1(OBJ(MapSuperMap(map)), tags, count);
	long n = MapTagCount(map);
	BlockMove(MapTags(map), tags + *count, n * sizeof(Ref));
	*count += n;
}


// ROM 0x0031da3c CollectFrameTags__FRC6RefVar
// An array of a frame's tags, in slot order.
Ref
CollectFrameTags(RefArg frame)
{
	RefVar tags(AllocateArray(RSSYMarray, Length(frame)));
	long count = 0;
	Ref* slots = ObjArraySlots(OBJ(tags));
	ObjHeader* map = OBJ(ObjClass(OBJ(frame)));
	CollectFrameTags1(map, slots, &count);
	return tags;
}


// ROM 0x0031bed0 SetLength__FRC6RefVarl
void
SetLength(RefArg obj, long length)
{
	gHeap->SetLength(obj, length);
}


// ROM 0x0031bf78 Clone__FRC6RefVar
Ref
Clone(RefArg obj)
{
	return gHeap->Clone(obj);
}


// ROM 0x0031bfcc ReplaceObjectRef__FlT1
void
ReplaceObjectRef(Ref target, Ref replacement)
{
	gHeap->ReplaceObject(target, replacement);
}


// ROM 0x0031c538 BlockStatistics__FPcPUlPUc
Ptr
BlockStatistics(Ptr previousBlock, ULong* nextSize, Boolean* isFree)
{
	return (Ptr) gHeap->BlockStatistics((ObjHeader*) previousBlock, nextSize, isFree);
}


// ROM 0x0031c554 HeapBounds__FPPcT1
void
HeapBounds(Ptr* start, Ptr* limit)
{
	*start = gHeap->fStart;
	*limit = gHeap->fEnd;
}


// ROM 0x0031c590 Statistics__FPUlT1
void
Statistics(ULong* freeSpace, ULong* largestFreeBlock)
{
	gHeap->Statistics(freeSpace, largestFreeBlock);
}


// ROM 0x0031c514 Uriah__Fv
// The heap dump.  NOT YET RECONSTRUCTED: TObjectHeap::Uriah (0x002f5e50)
// prints through the printer (InitPrinter), which is not reconstructed.
void
Uriah(void)
{
	gHeap->Uriah();
}


// ROM 0x0031c524 UriahBinaryObjects__Fi
void
UriahBinaryObjects(int printStrings)
{
	gHeap->UriahBinaryObjects(printStrings);
}


void
TObjectHeap::Uriah(void)
{ }


void
TObjectHeap::UriahBinaryObjects(int /*printStrings*/)
{ }


/* -------------------------------------------------------------------------------
	Lengths, flags, data
------------------------------------------------------------------------------- */

// ROM 0x0031e290 ObjectFlags__Fl
ULong
ObjectFlags(Ref obj)
{
	return ObjFlags(OBJ(obj));
}


// ROM 0x0031e2ac Length__Fl
// Slots of an array or frame, bytes of a binary; the last answer is cached.
long
Length(Ref obj)
{
	if (obj == gCacheLengthObj)
		return gCacheLengthLen;
	if (!ISPTR(obj))
	{
		RefVar value(obj);
		ThrowBadTypeWithFrameData(kNSErrUnexpectedImmediate, value);
	}
	gCacheLengthObj = 1;							// ObjectPtr may fault the object in and collect, which clears the cache
	ObjHeader* o = OBJ(obj);
	if (gCacheLengthObj != 0)
		gCacheLengthObj = obj;
	ULong flags = ObjFlags(o);
	long length;
	if ((flags & kObjSlotted) == 0)
	{
		if ((flags & kObjFrame) == 0)
			gCacheLengthLen = length = ObjBinaryLength(o);
		else
		{
			gCacheLengthObj = 0;
			length = ObjIndirectProcs(o)->fLength(ObjIndirectData(o));
		}
	}
	else
		gCacheLengthLen = length = ObjArrayLength(o);
	return length;
}


// ROM 0x0031c8d8 Length__FRC6RefVar
long
Length(RefArg obj)
{
	return Length((Ref) obj);
}


// ROM 0x0031e61c ComputeMapSize__FRC6RefVar
// The slots a frame with this map has: the tags of the map and its
// supermaps.
long
ComputeMapSize(RefArg map)
{
	RefVar superMap(MapSuperMap(OBJ(map)));
	long size = Length(map) - 1;
	if ((Ref) superMap != NILREF)
		size += ComputeMapSize(superMap);
	return size;
}


// ROM 0x00320408 GetMapTags__FlP12SortedMapTag
// The tags of a map and its supermaps, outermost first, each with the
// index of its slot; ==> how many.
long
GetMapTags(Ref map, SortedMapTag* tags)
{
	ObjHeader* o = OBJ(map);
	long index = 0;
	if (MapSuperMap(o) != NILREF)
	{
		index = GetMapTags(MapSuperMap(o), tags);
		tags += index;
	}
	long count = MapTagCount(o);
	Ref* tag = MapTags(o);
	for (long i = 0; i < count; i++)
	{
		tags[i].fTag = tag[i];
		tags[i].fIndex = index++;
	}
	return index;
}


// ROM 0x0032047c CompareSymbols_qsort__FPCvT1
static int
CompareSymbols_qsort(const void* a, const void* b)
{
	return SymbolCompare(((const SortedMapTag*) a)->fTag, ((const SortedMapTag*) b)->fTag);
}


// ROM 0x00320488 GetFrameMapTags__FlP12SortedMapTagUc
// A frame's tags with their slot indexes, sorted by symbol (hash, then
// name) when asked - the order a store keeps a frame's slots in.
void
GetFrameMapTags(Ref frame, SortedMapTag* tags, Boolean sorted)
{
	long count = GetMapTags(ObjClass(OBJ(frame)), tags);
	if (sorted)
		qsort(tags, count, sizeof(SortedMapTag), CompareSymbols_qsort);
}


// ROM 0x0031e684 BinaryData__Fl
Ptr
BinaryData(Ref obj)
{
	ObjHeader* o = OBJ(obj);
	if (ObjIsIndirect(o))
		return ObjIndirectProcs(o)->fDataPtr(ObjIndirectData(o));
	return ObjData(o);
}


// ROM 0x0031e6bc Slots__Fl
Ref*
Slots(Ref obj)
{
	ObjHeader* o = OBJ(obj);
	if ((ObjFlags(o) & kObjSlotted) == 0)
	{
		RefVar value(obj);
		ThrowBadTypeWithFrameData(kNSErrNotAnArray, value);
	}
	return ObjArraySlots(o);
}


// ROM 0x0031c9f0 LockedBinaryPtr__FRC6RefVar
// Lock the binary and answer its data.
Ptr
LockedBinaryPtr(RefArg obj)
{
	LockRef(obj);
	ObjHeader* o = OBJ(obj);
	if (ObjIsIndirect(o))
		return ObjIndirectProcs(o)->fDataPtr(ObjIndirectData(o));
	return ObjData(o);
}


/* -------------------------------------------------------------------------------
	Array slots
------------------------------------------------------------------------------- */

// ROM 0x0031e710 GetArraySlotError__FlT1Pc
// What GetArraySlotRef throws when the fast path fails: not an array, or
// out of bounds.
static Ref
GetArraySlotError(Ref obj, long slot, ObjHeader* o)
{
	if ((ObjFlags(o) & kObjSlotted) == 0)
	{
		RefVar value(obj);
		ThrowBadTypeWithFrameData(kNSErrNotAnArray, value);
	}
	else
		ThrowOutOfBounds(MAKEPTR(o), slot);
	return NILREF;
}


// ROM 0x0031e884 GetArraySlotRef__FlT1
Ref
GetArraySlotRef(Ref obj, long slot)
{
	ObjHeader* o = OBJ(obj);
	if ((ObjFlags(o) & kObjSlotted) != 0 && slot >= 0 && slot < ObjArrayLength(o))
		return ObjArraySlots(o)[slot];
	return GetArraySlotError(obj, slot, o);
}


// ROM 0x0031e8e0 SetArraySlotError__FlT1Pc
// What SetArraySlotRef throws when the fast path fails: not an array,
// read-only, or out of bounds.
static void
SetArraySlotError(Ref obj, long slot, ObjHeader* o)
{
	if ((ObjFlags(o) & kObjSlotted) == 0)
	{
		RefVar value(obj);
		ThrowBadTypeWithFrameData(kNSErrNotAnArray, value);
	}
	else if (slot >= 0 && slot < ObjArrayLength(o))
	{
		RefVar value(obj);
		ThrowExFramesWithBadValue(kNSErrObjectReadOnly, value);
	}
	else
		ThrowOutOfBounds(MAKEPTR(o), slot);
}


// ROM 0x0031ea20 SetArraySlotRef__FlN21
void
SetArraySlotRef(Ref obj, long slot, Ref value)
{
	ObjHeader* o = OBJ(obj);
	if ((ObjFlags(o) & (kObjSlotted | kObjReadOnly)) == kObjSlotted && slot >= 0 && slot < ObjArrayLength(o))
	{
		ObjArraySlots(o)[slot] = value;
		o = OBJ(obj);
		if ((ObjFlags(o) & kObjReadOnly) == 0)
			o->fSizeAndFlags |= kObjDirty;
		return;
	}
	SetArraySlotError(obj, slot, o);
}


// ROM 0x0031bee8 AddArraySlot__FRC6RefVarT1
void
AddArraySlot(RefArg obj, RefArg value)
{
	if (!IsArray(obj))
		ThrowBadTypeWithFrameData(kNSErrNotAnArray, obj);
	if ((ObjectFlags(obj) & kObjReadOnly) != 0)
		ThrowExFramesWithBadValue(kNSErrObjectReadOnly, obj);
	long length = Length(obj);
	gHeap->UnsafeSetArrayLength(obj, length + 1);
	SetArraySlotRef(obj, length, value);
}


/* -------------------------------------------------------------------------------
	Frame maps
	A frame's slots are named by its map: the tags of the map's supermap
	chain (root first) then its own.  FindOffset finds a tag's slot index.
------------------------------------------------------------------------------- */

// ROM 0x00320018 SearchSortedMap__FP3MaplT2
// Binary search of a sorted map's count tags (SymbolCompare order).
long
SearchSortedMap(ObjHeader* map, long count, Ref tag)
{
	long low = 0;
	long high = count - 1;
	Ref* tags = MapTags(map);
	while (low <= high)
	{
		long mid = (low + high) / 2;
		int cmp = SymbolCompare(tag, tags[mid]);
		if (cmp == 0)
			return mid;
		if (cmp < 0)
			high = mid - 1;
		else
			low = mid + 1;
	}
	return -1;
}


// ROM 0x0032007c FindOffset1__FlT1Pl
// Search the map chain from the root down, counting slots: the index of
// tag's slot and the map it was found in (*foundIn NILREF when not found;
// the count is then the frame's length).  Sixteen maps are remembered on
// the way up; a deeper chain recurses on its supermap.
long
FindOffset1(Ref mapRef, Ref tag, Ref* foundIn)
{
	const long kMaxDepth = 16;
	ObjHeader* stack[kMaxDepth];
	long depth = 0;
	long offset = 0;
	ObjHeader* map = OBJ(mapRef);
	while (MapSuperMap(map) != NILREF)
	{
		stack[depth++] = map;
		if (depth == kMaxDepth)
		{
			offset = FindOffset1(MapSuperMap(map), tag, foundIn);
			if (*foundIn != NILREF)
				return offset;
			depth--;											// map itself is searched first, then the ones below it
			break;
		}
		map = OBJ(MapSuperMap(map));
	}
	ULong32 hash = ObjSymbol(PTRVALUE(tag))->fHash;
	for (;;)
	{
		long count = MapTagCount(map);
		if ((MapFlags(map) & kMapSorted) == 0)
		{
			Ref* tags = MapTags(map);
			for (long i = 0; i < count; i++, offset++)
				if (tags[i] == tag || UnsafeSymbolEqual(tags[i], tag, hash))
				{
					*foundIn = MAKEPTR(map);
					return offset;
				}
		}
		else
		{
			long i = SearchSortedMap(map, count, tag);
			if (i >= 0)
			{
				*foundIn = MAKEPTR(map);
				return offset + i;
			}
			offset += count;
		}
		if (--depth < 0)
		{
			*foundIn = NILREF;
			return offset;
		}
		map = stack[depth];
	}
}


// the FindOffset cache: 32 entries of {map, tag, offset}, and the one used last
struct FindOffsetCacheEntry
{
	Ref		fMap;
	Ref		fTag;
	long	fOffset;
};
static FindOffsetCacheEntry		gFindOffsetCache[32];				// 0x0c104cbc
static FindOffsetCacheEntry*	gLastFindOffsetCacheEntry = gFindOffsetCache;	// 0x0c102650


// ROM 0x0031a0b4 FindOffsetCacheClear__Fv
void
FindOffsetCacheClear(void)
{
	for (long i = 0; i < 32; i++)
		gFindOffsetCache[i].fMap = 0;
}


// ROM 0x003201cc FindOffset__FlT1
// The slot index of tag in a frame with this map, -1 if none; cached by
// (map, tag).  _proto is looked for only in maps flagged as having it.
long
FindOffset(Ref map, Ref tag)
{
	if (!ISPTR(tag) || ObjClass(PTRVALUE(tag)) != kSymbolClass)
	{
		RefVar value(tag);
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, value);
	}
	ULong32 hash = ObjSymbol(PTRVALUE(tag))->fHash;
	ULong index = ((ULong) (hash + map) >> 6) & 0x1f;
	if (gLastFindOffsetCacheEntry->fMap == map && gLastFindOffsetCacheEntry->fTag == tag)
		return gLastFindOffsetCacheEntry->fOffset;
	FindOffsetCacheEntry* entry = &gFindOffsetCache[index];
	if (entry->fMap == map && entry->fTag == tag)
	{
		gLastFindOffsetCacheEntry = entry;
		return entry->fOffset;
	}
	long offset = -1;
	Boolean isProto = tag == RSSYM_proto || UnsafeSymbolEqual(tag, RSSYM_proto, ObjSymbol(PTRVALUE(RSSYM_proto))->fHash);
	if (!isProto || (MapFlags(OBJ(map)) & kMapProto) != 0)
	{
		Ref foundIn;
		offset = FindOffset1(map, tag, &foundIn);
		if (foundIn == NILREF)
			offset = -1;
	}
	entry->fTag = tag;
	entry->fOffset = offset;
	entry->fMap = map;
	gLastFindOffsetCacheEntry = entry;
	return offset;
}


// ROM 0x0032031c GetTag__FRC6RefVarlPl
// The tag of slot index in a frame with this map (NILREF past the end);
// *baseIndex tells the supermaps' slot count, or -1 when the tag was found.
Ref
GetTag(RefArg map, long index, long* baseIndex)
{
	if ((Ref) map == NILREF)
	{
		if (baseIndex != nil)
			*baseIndex = 0;
		return NILREF;
	}
	RefVar superMap(MapSuperMap(OBJ(map)));
	long superBase = 0;
	Ref tag = GetTag(superMap, index, &superBase);
	if (superBase == -1)
	{
		if (baseIndex != nil)
			*baseIndex = -1;
		return tag;
	}
	long end = superBase + Length(map) - 1;
	if (index < end)
	{
		if (baseIndex != nil)
			*baseIndex = -1;
		return GetArraySlotRef(map, index - superBase + 1);
	}
	if (baseIndex != nil)
		*baseIndex = end;
	return NILREF;
}


// ROM 0x003204d0 ExtendSharedMap__FRC6RefVari
// A new map of length tags over a shared one (which becomes its supermap
// unless it is empty), keeping its _proto flag.
Ref
ExtendSharedMap(RefArg map, int length)
{
	long size = ComputeMapSize(map);
	Ref newMap = gHeap->AllocateObject(ArrayObjSize(length + 1), kObjSlotted);
	ObjHeader* old = OBJ(map);
	ObjHeader* o = PTRVALUE(newMap);
	MapFlags(o) = MapFlags(old) & kMapProto;
	MapSuperMap(o) = (size == 0) ? NILREF : (Ref) map;
	return newMap;
}


// ROM 0x0031f96c SharedFrameMap__FRC6RefVar
// A frame's map, marked shared (so that adding a slot to the frame does
// not change it).
Ref
SharedFrameMap(RefArg frame)
{
	Ref map = ObjClass(OBJ(frame));
	ObjHeader* o = OBJ(map);
	if ((MapFlags(o) & kMapShared) == 0 && (ObjFlags(o) & kObjReadOnly) == 0)
		MapFlags(o) |= kMapShared;
	return map;
}


// ROM 0x0031a19c ConvertToSortedMap__FRC6RefVarl
// Sort a frame's own map's tags (selection sort), permuting the frame's
// slots with them, and flag the map sorted; answers where the tag at
// trackedIndex (an index among the map's tags) went.
long
ConvertToSortedMap(RefArg frame, long trackedIndex)
{
	RefVar map(ObjClass(OBJ(frame)));
	long mapLength = Length(map);
	long base = Length(frame) - (mapLength - 1);			// the frame index of the map's first tag
	RefVar least, candidate, temp;
	for (long i = 0; i < mapLength - 2; i++)
	{
		long slot = i + 1;
		least = GetArraySlotRef(map, slot);
		long leastIndex = i;
		for (long j = slot; j < mapLength - 1; j++)
		{
			candidate = GetArraySlotRef(map, j + 1);
			if (SymbolCompare(least, candidate) > 0)
			{
				least = candidate;
				leastIndex = j;
			}
		}
		if (i != leastIndex)
		{
			SetArraySlotRef(map, leastIndex + 1, GetArraySlotRef(map, slot));
			SetArraySlotRef(map, slot, least);
			temp = GetArraySlotRef(frame, leastIndex + base);
			SetArraySlotRef(frame, leastIndex + base, GetArraySlotRef(frame, i + base));
			SetArraySlotRef(frame, i + base, temp);
			if (i == trackedIndex)
				trackedIndex = leastIndex;
			else if (leastIndex == trackedIndex)
				trackedIndex = i;
		}
	}
	MapFlags(OBJ(ObjClass(OBJ(frame)))) |= kMapSorted;
	FindOffsetCacheClear();
	return trackedIndex;
}


// ROM 0x0031a3b4 AddSlot__FRC6RefVarT1
// A new slot named tag: the frame grows by one; its map gets the tag - in
// place when the map is the frame's own (kept sorted if it is sorted,
// sorted once it passes 20 tags), else through a new map over the shared
// one.  Answers the new slot's index.
long
AddSlot(RefArg frame, RefArg tag)
{
	long index = Length(frame);
	gHeap->UnsafeSetArrayLength(frame, index + 1);
	RefVar map(ObjClass(OBJ(frame)));
	ObjHeader* m = OBJ(map);
	if ((MapFlags(m) & kMapShared) == 0 && (ObjFlags(m) & kObjReadOnly) == 0)
	{
		long slot = Length(map);
		SetLength(map, slot + 1);
		m = OBJ(map);
		long last = slot - 1;										// the tags before this one
		if ((MapFlags(m) & kMapSorted) == 0)
		{
			SetArraySlotRef(map, slot, tag);
			if (slot > 0x14)
			{
				long sorted = ConvertToSortedMap(frame, last);
				index += sorted - last;
				ICacheClearFrame(frame);
			}
		}
		else
		{
			FindOffsetCacheClear();
			ICacheClearFrame(frame);
			Ref* tags = ObjArraySlots(m);								// slot 1 on
			Ref* slots = ObjArraySlots(OBJ(frame));
			long i;
			for (i = last; i > 0; i--)
			{
				if (SymbolCompare(tags[i], tag) <= 0)
					break;
				tags[i + 1] = tags[i];
				slots[index] = slots[index - 1];
				index--;
			}
			tags[i + 1] = tag;
		}
	}
	else
	{
		map = ExtendSharedMap(map, 1);
		ObjClass(OBJ(frame)) = map;
		SetArraySlotRef(map, 1, tag);
	}
	ICacheClearSymbol(tag, SymbolHash(tag));
	if (EQRef(tag, RSSYM_proto))
		MapFlags(OBJ(map)) |= kMapProto;
	Ref mapRef = map;
	Ref tagRef = tag;
	ULong cacheIndex = ((ULong) (ObjSymbol(PTRVALUE(tagRef))->fHash + mapRef) >> 6) & 0x1f;
	FindOffsetCacheEntry* entry = &gFindOffsetCache[cacheIndex];
	if (entry->fMap == mapRef && entry->fTag == tagRef)
	{
		entry->fOffset = index;
		gLastFindOffsetCacheEntry = entry;
	}
	return index;
}


// ROM 0x0031a78c ShrinkArray__FRC6RefVarl
// Remove slot index of an array: the slots after it move down, the object
// shrinks by one.
void
ShrinkArray(RefArg array, long index)
{
	long length = Length(array);
	Ref* slots = ObjArraySlots(OBJ(array));
	for (long i = index; i + 1 < length; i++)
		slots[i] = slots[i + 1];
	ObjHeader* o = OBJ(array);
	ULong oldSize = ObjSize(o);
	ULong newSize = ArrayObjSize(length - 1);
	if (oldSize != newSize)
	{
		long oldLength = ObjArrayLength(OBJ(array));
		gHeap->ResizeObject(array, newSize);
		o = OBJ(array);
		if (oldSize < newSize)
			for (long i = oldLength; i < length - 1; i++)
				ObjArraySlots(o)[i] = NILREF;
		DirtyObject(array);
	}
	gCacheLengthObj = 0;
}


// ROM 0x0031a614 ShrinkSharedMap__FRC6RefVarT1l
// A frame's map without the tag at slot index, which was found in foundIn
// (a map of the chain): a new map over foundIn's supermap holding the tags
// of the chain from there on, less the one removed; foundIn's supermap
// itself when nothing would be left.
Ref
ShrinkSharedMap(RefArg map, RefArg foundIn, long index)
{
	RefVar superMap(MapSuperMap(OBJ(foundIn)));
	long base = ((Ref) superMap == NILREF) ? 0 : ComputeMapSize(superMap);
	long count = ComputeMapSize(map) - base - 1;
	Ref result = superMap;
	if (count != 0 || (Ref) superMap == NILREF)
	{
		RefVar newMap(gHeap->AllocateObject(ArrayObjSize(count + 1), kObjSlotted));
		MapFlags(OBJ(newMap)) = 0;
		MapSuperMap(OBJ(newMap)) = superMap;
		RefVar tag;
		for (long i = base; i < index; i++)
		{
			tag = GetTag(map, i, nil);
			SetArraySlotRef(newMap, i + 1 - base, tag);
		}
		for (long i = index + 1; i <= base + count; i++)
		{
			tag = GetTag(map, i, nil);
			SetArraySlotRef(newMap, i - base, tag);
		}
		result = newMap;
	}
	return result;
}


// ROM 0x0031a800 RemoveSlot__FRC6RefVarT1
// Remove a frame's slot.  The map loses the tag: in place when the tag is
// in the frame's own unshared map; a map of one tag is unlinked from the
// chain (the frame or the map below it takes its supermap); else the chain
// from the tag's map down is rebuilt (ShrinkSharedMap).
void
RemoveSlot(RefArg frame, RefArg tag)
{
	ObjHeader* f = OBJ(frame);
	if ((ObjFlags(f) & (kObjSlotted | kObjFrame)) != (kObjSlotted | kObjFrame))
		ThrowBadTypeWithFrameData(kNSErrNotAFrame, frame);
	if ((ObjFlags(f) & kObjReadOnly) != 0)
		ThrowExFramesWithBadValue(kNSErrObjectReadOnly, frame);
	if (!IsSymbol(tag))
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, tag);
	RefVar map(ObjClass(f));
	RefVar foundIn(NILREF);
	Ref found = NILREF;
	long index = FindOffset1(map, tag, &found);
	foundIn = found;
	if ((Ref) foundIn == NILREF)
		return;
	ObjHeader* fm = OBJ(foundIn);
	Boolean done = false;
	Boolean unlinked = false;
	if (Length(foundIn) == 2)
	{
		// the tag's map holds only it: take it out of the chain
		if (!EQRef(foundIn, map))
		{
			RefVar below(map);
			ObjHeader* b = OBJ(below);
			while (!EQRef(MapSuperMap(b), foundIn))
			{
				below = MapSuperMap(b);
				b = OBJ(below);
			}
			if ((MapFlags(b) & kMapShared) == 0 && (ObjFlags(b) & kObjReadOnly) == 0)
			{
				MapSuperMap(b) = MapSuperMap(fm);
				if (SymbolCompare(tag, RSSYM_proto) == 0)
					MapFlags(b) &= ~kMapProto;
				unlinked = true;
			}
			done = unlinked;
		}
		else if (MapSuperMap(fm) != NILREF)
		{
			ObjClass(OBJ(frame)) = MapSuperMap(fm);
			done = true;
		}
	}
	if (!done)
	{
		if (EQRef(foundIn, map) && (MapFlags(fm) & kMapShared) == 0 && (ObjFlags(fm) & kObjReadOnly) == 0)
		{
			RefVar superMap(MapSuperMap(OBJ(map)));
			long base = ((Ref) superMap == NILREF) ? 0 : ComputeMapSize(superMap);
			ShrinkArray(map, index - base + 1);
			if (SymbolCompare(tag, RSSYM_proto) == 0)
				MapFlags(OBJ(foundIn)) &= ~kMapProto;
		}
		else
		{
			RefVar newMap(ShrinkSharedMap(map, foundIn, index));
			if (SymbolCompare(tag, RSSYM_proto) != 0 && (ObjectFlags(newMap) & kObjReadOnly) == 0)
				MapFlags(OBJ(newMap)) |= MapFlags(OBJ(map)) & kMapProto;
			ObjClass(OBJ(frame)) = newMap;
		}
	}
	ShrinkArray(frame, index);
	FindOffsetCacheClear();
	ICacheClear();
}


/* -------------------------------------------------------------------------------
	Frame slots
------------------------------------------------------------------------------- */

// ROM 0x002b5054 GlobalFunctionLookup__Fl
// A global function: from the function frame, else from the ROM's
// built-in functions.
Ref
GlobalFunctionLookup(Ref name)
{
	if (!ISPTR(name) || ObjClass(PTRVALUE(name)) != kSymbolClass)
	{
		RefVar value(name);
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, value);
	}
	Ref functions = gFunctionFrame;
	gFunctionFrame = NILREF;								// (so that the lookups below take the plain path)
	Ref result;
	if (!FrameHasSlotRef(functions, name))
	{
		gFunctionFrame = functions;
		// DEVIATION: the host may run without the ROM's objects, and then
		// there is no built-in functions frame (the ROM always has one)
		result = (gROMBuiltinFunctions == NILREF) ? NILREF : GetFrameSlotRef(gROMBuiltinFunctions, name);
	}
	else
	{
		result = GetFrameSlotRef(functions, name);
		gFunctionFrame = functions;
	}
	return result;
}


// ROM 0x0031ea90 SlowGetFrameSlot__FlT1
// GetFrameSlotRef when the frame is a fault block whose entry is not in
// memory: the slot symbol is locked while the entry is read (ObjectPtr may
// collect), whatever happens.
static Ref
SlowGetFrameSlot(Ref obj, Ref slot)
{
	Ref result = NILREF;
	ExceptionName name = nil;
	void* data = nil;
	ExceptionDestructor destructor = nil;
	newton_try
	{
		LockRef(slot);
		result = GetFrameSlotRef(MAKEPTR(OBJ(obj)), slot);
	}
	newton_catch_all
	{
		name = _info.exception.name;
		data = _info.exception.data;
		destructor = _info.exception.destructor;
	}
	end_try;
	UnlockRef(slot);
	if (name != nil)
		Throw(name, data, destructor);						// the ROM's NextHandler: on to the next handler
	return result;
}


// ROM 0x0031eb18 GetFrameSlotRef__FlT1
// The slot's value, NILREF if the frame has no such slot.  The function
// frame answers global functions.
Ref
GetFrameSlotRef(Ref obj, Ref slot)
{
	if (obj == gFunctionFrame)
		return GlobalFunctionLookup(slot);
	ObjHeader* o = FaultCheckObjectPtr(obj);
	if (o == nil)
	{
		if (gHeap->InHeap(slot))
			return SlowGetFrameSlot(obj, slot);
		o = OBJ(obj);
	}
	if ((ObjFlags(o) & (kObjSlotted | kObjFrame)) != (kObjSlotted | kObjFrame))
	{
		RefVar value(obj);
		ThrowBadTypeWithFrameData(kNSErrNotAFrame, value);
	}
	long index = FindOffset(ObjClass(o), slot);
	if (index == -1)
		return NILREF;
	return ObjArraySlots(o)[index];
}


// ROM 0x0031ebe4 UnsafeGetFrameSlot__FlT1Pl
// The slot's value and whether the frame has the slot; no checks.
Ref
UnsafeGetFrameSlot(Ref frame, Ref tag, long* exists)
{
	if (frame == gFunctionFrame)
	{
		Ref result = GlobalFunctionLookup(tag);
		*exists = result != NILREF;
		return result;
	}
	ObjHeader* o = OBJ(frame);
	long index = FindOffset(ObjClass(o), tag);
	if (index != -1)
	{
		*exists = 1;
		return ObjArraySlots(o)[index];
	}
	*exists = 0;
	return NILREF;
}


// ROM 0x0031ec60 SetFrameSlot__FRC6RefVarN21
// Set a slot, adding it if the frame has none of the name.  Setting _proto
// or _parent invalidates the interpreter's lookup caches.
void
SetFrameSlot(RefArg obj, RefArg slot, RefArg value)
{
	ObjHeader* o = OBJ(obj);
	if ((ObjFlags(o) & (kObjSlotted | kObjFrame)) != (kObjSlotted | kObjFrame))
		ThrowBadTypeWithFrameData(kNSErrNotAFrame, obj);
	if ((ObjFlags(o) & kObjReadOnly) != 0)
		ThrowExFramesWithBadValue(kNSErrObjectReadOnly, obj);
	long index = FindOffset(ObjClass(o), slot);
	if (index == -1)
	{
		index = AddSlot(obj, slot);
		o = OBJ(obj);
	}
	ObjArraySlots(o)[index] = value;
	DirtyObject(obj);
	ULong32 hash = SymbolHash(slot);
	if (hash == ObjSymbol(PTRVALUE(RSSYM_parent))->fHash || hash == ObjSymbol(PTRVALUE(RSSYM_proto))->fHash)
	{
		if (SymbolCompare(slot, RSSYM_parent) == 0 || SymbolCompare(slot, RSSYM_proto) == 0)
			ICacheClear();
	}
}


// ROM 0x0031ed90 SlowFrameHasSlot__FlT1
// FrameHasSlotRef for a fault block whose entry is not in memory (as
// SlowGetFrameSlot).
static int
SlowFrameHasSlot(Ref obj, Ref slot)
{
	int result = 0;
	ExceptionName name = nil;
	void* data = nil;
	ExceptionDestructor destructor = nil;
	newton_try
	{
		LockRef(slot);
		result = FrameHasSlotRef(MAKEPTR(OBJ(obj)), slot);
	}
	newton_catch_all
	{
		name = _info.exception.name;
		data = _info.exception.data;
		destructor = _info.exception.destructor;
	}
	end_try;
	UnlockRef(slot);
	if (name != nil)
		Throw(name, data, destructor);
	return result;
}


// ROM 0x0031ee18 FrameHasSlotRef__FlT1
int
FrameHasSlotRef(Ref obj, Ref slot)
{
	if (obj == gFunctionFrame)
		return GlobalFunctionLookup(slot) != NILREF;
	ObjHeader* o = FaultCheckObjectPtr(obj);
	if (o == nil)
	{
		if (gHeap->InHeap(slot))
			return SlowFrameHasSlot(obj, slot);
		o = OBJ(obj);
	}
	if ((ObjFlags(o) & (kObjSlotted | kObjFrame)) != (kObjSlotted | kObjFrame))
	{
		RefVar value(obj);
		ThrowBadTypeWithFrameData(kNSErrNotAFrame, value);
	}
	return FindOffset(ObjClass(o), slot) != -1;
}


// ROM 0x0031f90c FrameSlotPosition__FlT1
long
FrameSlotPosition(Ref frame, Ref tag)
{
	ObjHeader* o = OBJ(frame);
	if ((ObjFlags(o) & (kObjSlotted | kObjFrame)) != (kObjSlotted | kObjFrame))
	{
		RefVar value(MAKEPTR(o));
		ThrowBadTypeWithFrameData(kNSErrNotAFrame, value);
	}
	return FindOffset(ObjClass(o), tag);
}


// ROM 0x00300b48 GetProtoVariable__FRC6RefVarT1Pl
// A slot's value up the _proto chain from context; *exists tells whether
// it was found.  NOT YET RECONSTRUCTED: the interpreter's proto caches
// (TICache gProtoCache/gROProtoCache) and TInterpreter::TraceGet - the
// chain is walked every time.
Ref
GetProtoVariable(RefArg context, RefArg name, long* exists)
{
	if ((Ref) context == NILREF)
		ThrowExInterpreterWithSymbol(kNSErrNilContext, name);
	RefVar current(context);
	RefVar map;
	while ((Ref) current != NILREF)
	{
		ObjHeader* o = OBJ(current);
		if ((ObjFlags(o) & (kObjSlotted | kObjFrame)) != (kObjSlotted | kObjFrame))
			ThrowBadTypeWithFrameData(kNSErrNotAFrame, current);
		map = ObjClass(o);
		long index = FindOffset(map, name);
		if (index != -1)
		{
			if (exists != nil)
				*exists = 1;
			return ObjArraySlots(OBJ(current))[index];
		}
		index = FindOffset(map, RSSYM_proto);
		if (index == -1)
			break;
		current = ObjArraySlots(OBJ(current))[index];
	}
	if (exists != nil)
		*exists = 0;
	return NILREF;
}


/* -------------------------------------------------------------------------------
	Paths
	A path is a symbol, an integer, or an array of class pathExpr of those.
------------------------------------------------------------------------------- */

// ROM 0x00319560 IsPathExpr__FRC6RefVar
Boolean
IsPathExpr(RefArg ref)
{
	Ref r = ref;
	if (RTAG(r) == kTagInteger || IsSymbol(r))
		return true;
	if (IsArray(ref) && EQRef(ObjClass(OBJ(r)), RSSYMpathexpr))
		return true;
	return false;
}


// ROM 0x0031eee8 GetFramePath__FRC6RefVarT1
// The value at a path: a symbol looks up the _proto chain (or the global
// functions), an integer indexes an array (NILREF out of bounds), a
// pathExpr array follows its elements in turn (NILREF once nil is met).
Ref
GetFramePath(RefArg obj, RefArg thePath)
{
	if ((Ref) obj != NILREF)
	{
		Ref path = thePath;
		if (IsSymbol(path))
		{
			if ((Ref) obj == gFunctionFrame)
				return GlobalFunctionLookup(path);
			return GetProtoVariable(obj, thePath, nil);
		}
		if (RTAG(path) == kTagInteger)
		{
			long index = RVALUE(path);
			if (IsArray(obj))
			{
				if (index < 0)
					return NILREF;
				ObjHeader* o = OBJ(obj);
				if (index >= ObjArrayLength(o))
					return NILREF;
				return ObjArraySlots(o)[index];
			}
		}
		else if (IsArray(thePath) && EQRef(ClassOf(thePath), RSSYMpathexpr))
		{
			long count = ObjArrayLength(OBJ(path));
			if (count < 1)
				Throw(exFrames, (void*) kNSErrEmptyPath, nil);
			RefVar current(obj);
			RefVar element;
			for (long i = 0; i < count && (Ref) current != NILREF; i++)
			{
				element = ObjArraySlots(OBJ(thePath))[i];
				Ref e = element;
				if (RTAG(e) == kTagInteger)
				{
					if (!IsArray(current))
						ThrowExFramesWithBadValue(kNSErrPathFailed, thePath);
					long index = RVALUE(e);
					ObjHeader* o = OBJ(current);
					if (index >= 0 && index < ObjArrayLength(o))
						current = ObjArraySlots(o)[index];
					else
						current = NILREF;
				}
				else
				{
					if (!IsSymbol(e))
						ThrowExFramesWithBadValue(kNSErrBadSegmentInPath, element);
					if ((Ref) current == gFunctionFrame)
						current = GlobalFunctionLookup(e);
					else
						current = GetProtoVariable(current, element, nil);
				}
			}
			return current;
		}
		else
			ThrowBadTypeWithFrameData(kNSErrNotAPathExpr, thePath);
	}
	ThrowExFramesWithBadValue(kNSErrPathFailed, thePath);
	return NILREF;
}


// ROM 0x0031f248 SetFramePath__FRC6RefVarN21i
// Set the value at a path; frames missing on the way are made (an integer
// element cannot be).  ownSlots (SetFramePathFor1XFunctions) looks in each
// frame's own slots rather than up its _proto chain.
static void
SetFramePath(RefArg obj, RefArg thePath, RefArg value, int ownSlots)
{
	Ref path = thePath;
	if (IsSymbol(path))
	{
		SetFrameSlot(obj, thePath, value);
		return;
	}
	if (RTAG(path) == kTagInteger)
	{
		if (!IsArray(obj))
			ThrowExFramesWithBadValue(kNSErrPathFailed, thePath);
		SetArraySlotRef(obj, RVALUE(path), value);
		return;
	}
	if (IsArray(thePath) && EQRef(ClassOf(thePath), RSSYMpathexpr))
	{
		long last = ObjArrayLength(OBJ(path)) - 1;
		if (last < 0)
			Throw(exFrames, (void*) kNSErrEmptyPath, nil);
		RefVar current(obj);
		RefVar element;
		for (long i = 0; i < last; i++)
		{
			if ((Ref) current == NILREF)
				ThrowExFramesWithBadValue(kNSErrPathFailed, thePath);
			element = GetArraySlotRef(thePath, i);
			Ref e = element;
			if (RTAG(e) == kTagInteger)
			{
				if (!IsArray(current))
					ThrowExFramesWithBadValue(kNSErrPathFailed, thePath);
				current = GetArraySlotRef(current, RVALUE(e));
			}
			else
			{
				if (!IsSymbol(e))
					ThrowExFramesWithBadValue(kNSErrBadSegmentInPath, element);
				long exists = 0;
				Ref next = NILREF;
				if (!ownSlots)
					next = GetProtoVariable(current, element, &exists);
				else
				{
					exists = FrameHasSlotRef(current, e);
					if (exists)
						next = GetFrameSlotRef(current, e);
				}
				if (exists)
					current = next;
				else
				{
					RefVar made;
					if (RTAG(ObjArraySlots(OBJ(thePath))[i + 1]) == kTagInteger)
						ThrowExFramesWithBadValue(kNSErrPathFailed, thePath);
					made = AllocateFrame();
					SetFrameSlot(current, element, made);
					current = made;
				}
			}
		}
		if ((Ref) current == NILREF)
			ThrowExFramesWithBadValue(kNSErrPathFailed, thePath);
		element = ObjArraySlots(OBJ(thePath))[last];
		Ref e = element;
		if (RTAG(e) == kTagInteger)
		{
			if (!IsArray(current))
				ThrowExFramesWithBadValue(kNSErrPathFailed, thePath);
			SetArraySlotRef(current, RVALUE(e), value);
		}
		else
			SetFrameSlot(current, element, value);
		return;
	}
	ThrowBadTypeWithFrameData(kNSErrNotAPathExpr, thePath);
}


// ROM 0x0031f5dc SetFramePath__FRC6RefVarN21
void
SetFramePath(RefArg obj, RefArg thePath, RefArg value)
{
	SetFramePath(obj, thePath, value, 0);
}


// ROM 0x0031f5e4 SetFramePathFor1XFunctions__FRC6RefVarN21
void
SetFramePathFor1XFunctions(RefArg obj, RefArg thePath, RefArg value)
{
	SetFramePath(obj, thePath, value, 1);
}


// ROM 0x0031f5ec FrameHasPath__FRC6RefVarT1
int
FrameHasPath(RefArg obj, RefArg thePath)
{
	if ((Ref) obj == NILREF)
		return 0;
	Ref path = thePath;
	if (IsSymbol(path))
	{
		long exists = 0;
		if ((Ref) obj == gFunctionFrame)
			exists = GlobalFunctionLookup(path) != NILREF;
		else if (IsFrame(obj))
			GetProtoVariable(obj, thePath, &exists);
		return exists != 0;
	}
	if (RTAG(path) == kTagInteger)
	{
		if (IsArray(obj))
			return RVALUE(path) < ObjArrayLength(OBJ(obj));
	}
	else
	{
		if (IsArray(thePath) && EQRef(ClassOf(thePath), RSSYMpathexpr))
		{
			long count = ObjArrayLength(OBJ(path));
			if (count < 1)
				Throw(exFrames, (void*) kNSErrEmptyPath, nil);
			RefVar current(obj);
			RefVar element;
			for (long i = 0; i < count; i++)
			{
				if ((Ref) current == NILREF)
					return 0;
				element = ObjArraySlots(OBJ(thePath))[i];
				Ref e = element;
				if (RTAG(e) == kTagInteger)
				{
					if (!IsArray(current))
						return 0;
					long index = RVALUE(e);
					ObjHeader* o = OBJ(current);
					if (index >= ObjArrayLength(o))
						return 0;
					current = ObjArraySlots(o)[index];
				}
				else
				{
					if (!IsSymbol(e))
						ThrowExFramesWithBadValue(kNSErrBadSegmentInPath, element);
					if (!IsFrame(current))
						return 0;
					if ((Ref) current == gFunctionFrame)
					{
						current = GlobalFunctionLookup(e);
						if ((Ref) current == NILREF)
							return 0;
					}
					else
					{
						long exists = 0;
						current = GetProtoVariable(current, element, &exists);
						if (!exists)
							return 0;
					}
				}
			}
			return 1;
		}
		ThrowBadTypeWithFrameData(kNSErrNotAPathExpr, thePath);
	}
	return 0;
}


/* -------------------------------------------------------------------------------
	Clones
------------------------------------------------------------------------------- */

// an object outside both the heap and the ROM: in a package or a store
// (the ROM's test is an address at or above 0x03800000 that is not the heap's)
static inline Boolean
IsExternalObject(Ref r)
{
	return !gHeap->InHeap(r) && !InROMSymbolSpace(r);
}


// ROM 0x0031faa4 DeepClone1__FRC6RefVarR14TPrecedentsVarT2
// Clone obj and, recursively, the objects its slots refer to; an object
// seen before gets the clone made then.
static Ref
DeepClone1(RefArg obj, TPrecedentsVar& precedents, TPrecedentsVar& clones)
{
	long seen = precedents.Find(obj);
	if (seen != -1)
		return clones.Get(seen);
	precedents.Append(obj);
	RefVar clone(Clone(obj));
	clones.Append(clone);
	if ((ObjectFlags(obj) & kObjSlotted) != 0)
	{
		long length = Length(obj);
		RefVar slot;
		for (long i = 0; i < length; i++)
		{
			slot = ObjArraySlots(OBJ(obj))[i];
			if (ISPTR(slot))
			{
				slot = DeepClone1(slot, precedents, clones);
				ObjArraySlots(OBJ(clone))[i] = slot;
			}
		}
	}
	return clone;
}


// ROM 0x0031fbc8 DeepClone__FRC6RefVar
Ref
DeepClone(RefArg obj)
{
	if (!ISPTR(obj))
		return obj;
	TPrecedentsVar precedents;
	TPrecedentsVar clones;
	return DeepClone1(obj, precedents, clones);
}


// ROM 0x0031fc28 TotalClone1__FRC6RefVarR14TPrecedentsVarT2i
// DeepClone including maps and classes (every ref of the object, the
// class slot first), with symbols in packages interned into RAM.  For
// EnsureInternal (ensureInternal) objects are not copied: read-only ones
// are left alone (answer NILREF), others get their refs fixed in place.
static Ref
TotalClone1(RefArg obj, TPrecedentsVar& precedents, TPrecedentsVar& clones, int ensureInternal)
{
	long seen = precedents.Find(obj);
	if (seen != -1)
		return clones.Get(seen);
	precedents.Append(obj);
	RefVar clone(NILREF);
	if (IsExternalObject(obj) && IsSymbol(obj))
		clone = Intern(SymbolName(obj));
	else if (ensureInternal && !IsExternalObject(obj))
	{
		if ((ObjectFlags(obj) & kObjReadOnly) != 0)
		{
			clones.Append(obj);
			return NILREF;
		}
		clone = obj;
	}
	else
		clone = Clone(obj);
	clones.Append(clone);
	long count = ((ObjectFlags(obj) & kObjSlotted) != 0) ? Length(obj) + 1 : 1;	// the class slot and the slots
	RefVar slot;
	for (long i = 0; i < count; i++)
	{
		slot = ObjSlots(OBJ(obj))[i];
		if (RTAG(slot) == kTagPointer)
		{
			slot = TotalClone1(slot, precedents, clones, ensureInternal);
			if ((Ref) slot != NILREF)
				ObjSlots(OBJ(clone))[i] = slot;
		}
	}
	return clone;
}


// ROM 0x0031fe94 TotalClone__FRC6RefVar
Ref
TotalClone(RefArg obj)
{
	Ref r = obj;
	if (RTAG(r) != kTagPointer)
		return r;
	if (IsSymbol(r))
	{
		if (IsExternalObject(r))
			return Intern(SymbolName(r));
		return r;
	}
	TPrecedentsVar precedents;
	TPrecedentsVar clones;
	return TotalClone1(obj, precedents, clones, 0);
}


// ROM 0x0031ff50 EnsureInternal__FRC6RefVar
// obj with every symbol in a package replaced by the RAM symbol, cloning
// only what is read-only; a package symbol itself is interned.
Ref
EnsureInternal(RefArg obj)
{
	Ref r = obj;
	if (RTAG(r) != kTagPointer)
		return r;
	if (IsSymbol(r))
	{
		if (IsExternalObject(r))
			return Intern(SymbolName(r));
		return r;
	}
	TPrecedentsVar precedents;
	TPrecedentsVar clones;
	Ref result = TotalClone1(obj, precedents, clones, 1);
	if (result == NILREF)
		result = obj;
	return result;
}


/* -------------------------------------------------------------------------------
	Classes
------------------------------------------------------------------------------- */

// ROM 0x0031bff4 ClassOf__FRC6RefVar
// An integer is an int, a character a char, a boolean a boolean; a symbol
// a symbol, other binaries and arrays what their class slot says (a weak
// array a _weakarray); a frame its class slot up the _proto chain: frame
// when there is none, _function or _function.native for the function
// classes.
Ref
ClassOf(RefArg obj)
{
	Ref r = obj;
	ULong tag = RTAG(r);
	if (tag == kTagInteger)
		return RSSYMint;
	if (tag == kTagImmed)
	{
		ULong immedTag = RIMMEDTAG(r);
		if (immedTag == kImmedChar)
			return RSSYMchar;
		if (immedTag == kImmedBoolean)
			return RSSYMboolean;
		return RSSYMweird_immediate;
	}
	if (!ISPTR(r))
		return NILREF;
	ULong flags = ObjectFlags(r);
	if ((flags & kObjSlotted) == 0)
	{
		if (!IsSymbol(r))
			return ObjClass(OBJ(obj));
		return RSSYMsymbol;
	}
	if ((flags & kObjFrame) == 0)
	{
		Ref theClass = ObjClass(OBJ(obj));
		if (theClass != kWeakArrayClass)
			return theClass;
		return RSSYM_weakarray;
	}
	Ref theClass = GetProtoVariable(obj, RSSYMclass, nil);
	if (theClass == NILREF)
		return RSSYMframe;
	if (theClass == kFuncClass)
		return RSSYM_function;
	if (theClass == 0x132 || theClass == 0x232)				// FUNCKIND 1 and 2 of kFuncClass: native functions
		return RSSYM_function_2Enative;
	return theClass;
}


// ROM 0x0031c130 SetClass__FRC6RefVarT1
void
SetClass(RefArg obj, RefArg theClass)
{
	ObjHeader* o = OBJ(obj);
	if ((ObjFlags(o) & kObjReadOnly) != 0)
		ThrowExFramesWithBadValue(kNSErrObjectReadOnly, obj);
	ULong flags = ObjFlags(o);
	if ((flags & kObjSlotted) == 0)
	{
		ObjClass(o) = theClass;
		if ((flags & kObjFrame) != 0)
			ObjIndirectProcs(o)->fSetClass(ObjIndirectData(o), theClass);
	}
	else if ((flags & kObjFrame) == 0)
		ObjClass(o) = theClass;
	else
		SetFrameSlot(obj, RSSYMclass, theClass);
	DirtyObject(obj);
}


// ROM 0x002bec5c IsSubclassRef__FlT1
// Symbols: the same, or super is the empty symbol, or sub is a dotted name
// super prefixes ('string.foo' is a 'string), or super is reached from sub
// through the inheritance frame.  Other classes must be EQ.
int
IsSubclassRef(Ref sub, Ref super)
{
	if (!IsSymbol(sub) || !IsSymbol(super))
		return EQRef(sub, super);
	if (EQRef(sub, super))
		return true;
	const unsigned char* superName = (const unsigned char*) SymbolName(super);
	if (*superName == 0)
		return true;
	const unsigned char* subName = (const unsigned char*) SymbolName(sub);
	if (strchr((const char*) subName, '.') != nil)
	{
		for (;;)
		{
			if (*superName == 0)
				return *subName == 0 || *subName == '.';
			if (*subName == 0)
				return *superName == 0;
			if (toupper(*superName) != toupper(*subName))
				return false;
			superName++;
			subName++;
		}
	}
	while (!EQRef(sub, super))
	{
		sub = GetFrameSlotRef(gInheritanceFrame, sub);
		if (sub == NILREF)
			return false;
	}
	return true;
}


// ROM 0x002beda8 IsInstance__FRC6RefVarT1
int
IsInstance(RefArg obj, RefArg super)
{
	Ref theClass = ClassOf(obj);
	if (theClass == NILREF)
		return false;
	return IsSubclassRef(theClass, super);
}


// ROM 0x002bedd8 InitClasses__Fv
void
InitClasses(void)
{
	gInheritanceFrame = AllocateFrame();
	AddGCRoot(gInheritanceFrame);
}


/* -------------------------------------------------------------------------------
	Predicates
------------------------------------------------------------------------------- */

// ROM 0x00319874 IsString__FRC6RefVar
Boolean
IsString(RefArg ref)
{
	Ref r = ref;
	if (ISPTR(r) && !IsFaultBlock(r) && (ObjectFlags(r) & kObjSlotted) == 0)
		return IsInstance(ref, RSSYMstring) != 0;
	return false;
}


// ROM 0x003198d4 IsBinary__FRC6RefVar
Boolean
IsBinary(RefArg ref)
{
	Ref r = ref;
	if (ISPTR(r) && !IsFaultBlock(r))
		return (ObjectFlags(r) & kObjSlotted) == 0;
	return false;
}


// ROM 0x0031992c IsArray__FRC6RefVar
Boolean
IsArray(RefArg ref)
{
	Ref r = ref;
	if (ISPTR(r) && !IsFaultBlock(r))
		return (ObjectFlags(r) & (kObjSlotted | kObjFrame)) == kObjSlotted;
	return false;
}


// ROM 0x00319990 IsFrame__FRC6RefVar
// A fault block counts as a frame (it stands for a soup entry).
Boolean
IsFrame(RefArg ref)
{
	Ref r = ref;
	if (!ISPTR(r))
		return false;
	if (IsFaultBlock(r))
		return true;
	return (ObjectFlags(r) & (kObjSlotted | kObjFrame)) == (kObjSlotted | kObjFrame);
}


// ROM 0x003199ec ISREAL__Fl
int
ISREAL(Ref r)
{
	if (!ISPTR(r) || IsFaultBlock(r) || (ObjectFlags(r) & kObjSlotted) != 0)
		return false;
	RefVar obj(r);
	return EQRef(ClassOf(obj), RSSYMreal);
}


// ROM 0x0031952c IsNumber__Fl
Boolean
IsNumber(Ref ref)
{
	return RTAG(ref) == kTagInteger || ISREAL(ref);
}


// ROM 0x0031c8e4 IsNumber__FRC6RefVar
Boolean
IsNumber(RefArg ref)
{
	return IsNumber((Ref) ref);
}


// ROM 0x0031c948 IsReal__FRC6RefVar
Boolean
IsReal(RefArg ref)
{
	return ISREAL(ref) != 0;
}


/* -------------------------------------------------------------------------------
	Strings and reals
	Strings are binaries of class string holding UniChars with a terminating
	0 (utility/Unicode.h has the UniChar functions and the conversions,
	whose encoding tables are NOT YET RECONSTRUCTED - characters are
	widened and narrowed as they are).
------------------------------------------------------------------------------- */

// ROM 0x0031c1e4 MakeString__FPCc
Ref
MakeString(const char* str)
{
	long length = strlen(str);
	RefVar s(AllocateBinary(RSSYMstring, (length + 1) * sizeof(UniChar)));
	UniChar* p = (UniChar*) BinaryData(s);
	for (long i = 0; i <= length; i++)
		p[i] = (UniChar) (unsigned char) str[i];
	return s;
}


// ROM 0x0031c24c MakeString__FPCUs
Ref
MakeString(const UniChar* str)
{
	long length = Ustrlen(str);
	RefVar s(AllocateBinary(RSSYMstring, (length + 1) * sizeof(UniChar)));
	memcpy(BinaryData(s), str, (length + 1) * sizeof(UniChar));
	return s;
}


// ROM 0x00129228 MakeString__FPCUsl
// A string of the first length UniChars (need not be terminated).
Ref
MakeString(const UniChar* str, long length)
{
	RefVar s(AllocateBinary(RSSYMstring, (length + 1) * sizeof(UniChar)));
	UniChar* p = (UniChar*) BinaryData(s);
	memcpy(p, str, length * sizeof(UniChar));
	p[length] = 0;
	return s;
}


// ROM 0x0031c2ac GetCString__FRC6RefVar
// The characters of a string (which must be one).
UniChar*
GetCString(RefArg str)
{
	if (!IsString(str))
		ThrowBadTypeWithFrameData(kNSErrNotAString, str);
	return (UniChar*) BinaryData(str);
}


// ROM 0x0031c2a8 CString__FRC6RefVar
UniChar*
CString(RefArg str)
{
	return GetCString(str);
}


// ROM 0x0031c2e4 ASCIIString__FRC6RefVar
// An asciiString of the string's characters, narrowed.
Ref
ASCIIString(RefArg str)
{
	long length = Length(str) / 2;
	RefVar s(AllocateBinary(RSSYMasciistring, length));
	ConvertFromUnicode((const UniChar*) BinaryData(str), BinaryData(s), kMacRomanEncoding, 0x7fffffff);
	return s;
}


// ROM 0x0031c358 MakeReal__Fd
Ref
MakeReal(double d)
{
	RefVar r(AllocateBinary(RSSYMreal, sizeof(double)));
	memcpy(BinaryData(r), &d, sizeof(double));
	return r;
}


// ROM 0x0031c3f8 CDouble__FRC6RefVar
double
CDouble(RefArg d)
{
	if (!EQRef(ClassOf(d), RSSYMreal))
		ThrowBadTypeWithFrameData(kNSErrNotAReal, d);
	double value;
	memcpy(&value, BinaryData(d), sizeof(double));
	return value;
}


// ROM 0x0031c454 CoerceToInt__FRC6RefVar
long
CoerceToInt(RefArg r)
{
	Ref ref = r;
	if (RTAG(ref) == kTagInteger)
		return RVALUE(ref);
	if (!ISREAL(ref))
	{
		ThrowBadTypeWithFrameData(kNSErrNotANumber, r);
		return 0;
	}
	double value;
	memcpy(&value, BinaryData(ref), sizeof(double));
	return (long) value;
}


// ROM 0x0031c4b4 CoerceToDouble__FRC6RefVar
double
CoerceToDouble(RefArg r)
{
	Ref ref = r;
	if (RTAG(ref) == kTagInteger)
		return (double) RVALUE(ref);
	if (!ISREAL(ref))
	{
		ThrowBadTypeWithFrameData(kNSErrNotANumber, r);
		return 0;
	}
	double value;
	memcpy(&value, BinaryData(ref), sizeof(double));
	return value;
}


/* -------------------------------------------------------------------------------
	Iteration
------------------------------------------------------------------------------- */

// ROM 0x0031abf4 OnStack__FPCv
// Whether p is on the current task's stack.  The ROM compares with its own
// frame and the stack top in the Newt globals; the host's thread stacks
// come from hal (GetStackBounds).
Boolean
OnStack(const void* p)
{
	const void* low;
	const void* high;
	GetStackBounds(&low, &high);
	return p >= low && p <= high;
}


// ROM 0x0031ac3c DisposeTObjectIterator__FPv
// The cleanup an exception runs for an iterator on the stack.
static void
DisposeTObjectIterator(void* iterator)
{
	((TObjectIterator*) iterator)->~TObjectIterator();
}


// ROM 0x0031ac44 __ct__15TObjectIteratorFRC6RefVari
// Over the slots of an array or frame; includeSiblings goes on into the
// frame's _proto chain.  On the stack, the iterator registers an exception
// cleanup so that a Throw frees its RefHandles.
TObjectIterator::TObjectIterator(RefArg obj, int includeSiblings)
{
	if (OnStack(this))
	{
		fCleanup.header.catchType = kExceptionCleanup;
		fCleanup.function = DisposeTObjectIterator;
		fCleanup.object = this;
		AddExceptionHandler(&fCleanup.header);
	}
	else
		fCleanup.function = nil;
	if (!ISPTR(obj) || (ObjectFlags(obj) & kObjSlotted) == 0)
		ThrowBadTypeWithFrameData(kNSErrNotAFrameOrArray, obj);
	fObj = obj;
	fIncludeSiblings = includeSiblings;
	if ((ObjectFlags(obj) & kObjFrame) == 0)
		fMapRef = NILREF;
	else
		fMapRef = ObjClass(OBJ(obj));
	fLength = Length(obj);
	fIndex = -1;
	Next();
}


// ROM 0x0031ae00 __dt__15TObjectIteratorFv
TObjectIterator::~TObjectIterator()
{
	if (fCleanup.function != nil)
		RemoveExceptionHandler(&fCleanup.header);
}


// ROM 0x0031ae64 Next__15TObjectIteratorFv
// On to the next slot (the object may have grown or shrunk meanwhile);
// then, with siblings, into the _proto.  Answers whether there is one.
int
TObjectIterator::Next(void)
{
	long length = Length(fObj);
	if (fLength <= length)
		fIndex++;
	fLength = length;
	if (fIndex < length)
	{
		if ((Ref) fMapRef == NILREF)
			fTag = MAKEINT(fIndex);
		else
			fTag = GetTag(fMapRef, fIndex, nil);
		fValue = GetArraySlotRef(fObj, fIndex);
		return true;
	}
	if (fIncludeSiblings && (Ref) fMapRef != NILREF)
	{
		RefVar proto(GetFrameSlotRef(fObj, RSSYM_proto));
		if ((Ref) proto != NILREF)
		{
			Reset(proto);
			return !Done();
		}
	}
	fValue = NILREF;
	fTag = NILREF;
	return false;
}


// ROM 0x0031afa8 Done__15TObjectIteratorFv
int
TObjectIterator::Done(void)
{
	long length = Length(fObj);
	fLength = length;
	if (!fIncludeSiblings || (Ref) fMapRef == NILREF)
		return fIndex >= length;
	if (fIndex < length)
		return false;
	return GetFrameSlotRef(fObj, RSSYM_proto) == NILREF;
}


// ROM 0x0031b034 Reset__15TObjectIteratorFv
void
TObjectIterator::Reset(void)
{
	fIndex = -1;
	Next();
}


// ROM 0x0031b040 Reset__15TObjectIteratorFRC6RefVar
void
TObjectIterator::Reset(RefArg newObj)
{
	fObj = newObj;
	if ((ObjectFlags(newObj) & kObjFrame) == 0)
		fMapRef = NILREF;
	else
		fMapRef = ObjClass(OBJ(newObj));
	fLength = Length(newObj);
	fIndex = -1;
	Next();
}


// ROM 0x0031c9d8 Tag__15TObjectIteratorFv
Ref
TObjectIterator::Tag(void)
{
	return fTag;
}


// ROM 0x0031c9e4 Value__15TObjectIteratorFv
Ref
TObjectIterator::Value(void)
{
	return fValue;
}


// ROM 0x0031ac2c NewIterator__FRC6RefVar
TObjectIterator*
NewIterator(RefArg obj)
{
	return new TObjectIterator(obj);
}


// ROM 0x0031c994 NewTObjectIterator__FRC6RefVar
TObjectIterator*
NewTObjectIterator(RefArg obj)
{
	return new TObjectIterator(obj);
}


// ROM 0x0031c998 DeleteTObjectIterator__FP15TObjectIterator
void
DeleteTObjectIterator(TObjectIterator* iterator)
{
	if (iterator != nil)
		delete iterator;
}


// ROM 0x0031ab54 MapSlots__FRC6RefVarPFRC6RefVarT1Ul_lUl
// Call func(tag, value, anything) for each slot until it answers other
// than NILREF.
void
MapSlots(RefArg obj, MapSlotsFunction func, ULong anything)
{
	if ((ObjectFlags(obj) & kObjSlotted) == 0)
		return;
	TObjectIterator iter(obj, false);
	while (!iter.Done())
	{
		if (func(iter.fTag, iter.fValue, anything) != NILREF)
			break;
		iter.Next();
	}
}


/* -------------------------------------------------------------------------------
	Start-up
------------------------------------------------------------------------------- */

// ROM 0x0031c5ec PatchMagicPointerTable__Fv
void
PatchMagicPointerTable(void)
{ }


// ROM 0x0031c5f0 InitMagicPointerTables__Fv
// NOT YET RECONSTRUCTED: InitRExMagicPointerTables (0x000d218c) reads the
// REx export tables and resolves their imports (the ROM extension reader).
void
InitMagicPointerTables(void)
{ }


// ROM 0x0031c608 InitObjects__Fv
// The heap (its size from InternalRAMInfo in the ROM, gObjectHeapSize
// here), the global frames, symbols, the printer, classes and the
// interpreter.  NOT YET RECONSTRUCTED: the union soup entry cache
// (MakeEntryCache) and the package store's part handler (TPackageStore,
// TPackageStorePartHandler).
void
InitObjects(void)
{
	gCurrentStackPos = 1;
	gHeap = new TObjectHeap(gObjectHeapSize, 1);
	AddGCRoot(gFunctionFrame);
	gVarFrame = AllocateFrame();
	AddGCRoot(gVarFrame);
	InitMagicPointerTables();
	FindOffsetCacheClear();
	InitSymbols();
	InitPrinter();
	InitClasses();
	InitInterpreter();
	InstallHostScriptBuiltins();
	gStores = AllocateArray(RSSYMarray, 0);
	AddGCRoot(gStores);
	AddGCRoot(gUnionSoups);
	AddGCRoot(gPackageStores);
	gPackageStores = AllocateArray(RSSYMarray, 0);
	InitUnicode();		// (the ROM: TNewtWorld::MainConstructor, after InitObjects; nothing without the ROM's objects)
}


// ROM 0x00101198 IsLargeBinary__FRC6RefVar
// An indirect binary whose procedures are the large binaries' (NOT YET
// RECONSTRUCTED: large binaries - never).
Boolean
IsLargeBinary(RefArg /*ref*/)
{
	return false;
}


/*------------------------------------------------------------------------------
	L a r g e   b i n a r i e s ,   f r o m   a   s c r i p t

	A "VBO" (a virtual binary object) is a binary kept on a store and
	paged in through a compander; a script can ask an object whether it
	is one, which store it is on, what compresses it and how much room it
	takes there, and can throw away the paged-in copy or the changes made
	to it.

	NOT YET RECONSTRUCTED: large binaries themselves (`LBData`, the
	store's large objects).  `IsLargeBinary` is never true on a host, so
	every one of these takes the ordinary-binary arm, which is what the
	ROM answers for anything that is not a VBO: nil, or nothing done.
	The arms below are written as the ROM has them so that the shape of
	each is on record.
------------------------------------------------------------------------------*/

// ROM 0x001011fc FIsLargeBinary
// IsVBO(obj): whether the object is kept on a store.
static Ref
FIsLargeBinary(RefArg /*rcvr*/, RefArg obj)
{
	return MAKEBOOLEAN(IsLargeBinary(obj));
}


// ROM 0x00100c04 FGetBinaryStore
// GetVBOStore(obj): the store frame it lives on - the one of `gStores`
// whose wrapper holds the same TStore - or nil.
static Ref
FGetBinaryStore(RefArg /*rcvr*/, RefArg obj)
{
	if (!IsLargeBinary(obj))
		return NILREF;
	return NILREF;				// NOT YET: LBData::GetStore and the walk of gStores
}


// ROM 0x00100c50 FGetBinaryCompander
// GetVBOCompander(obj): the name of the compander that packs it.
static Ref
FGetBinaryCompander(RefArg /*rcvr*/, RefArg obj)
{
	if (!IsLargeBinary(obj))
		return NILREF;
	return NILREF;				// NOT YET: LOCompanderName off the store
}


// ROM 0x00100d00 FGetBinaryCompanderData
// GetVBOCompanderData(obj): the data that compander was made with.
static Ref
FGetBinaryCompanderData(RefArg /*rcvr*/, RefArg obj)
{
	if (!IsLargeBinary(obj))
		return NILREF;
	return NILREF;				// NOT YET: LOCompanderData off the store
}


// ROM 0x00100e38 FGetBinaryStoredSize
// GetVBOStoredSize(obj): how much room it takes on the store, which is
// not the same as its size in memory because it is compressed there.
//
// BUG (the ROM's): this one does not ask whether the object is a large
// binary at all.  It hands the *binary's own data* to
// StorageSizeOfLargeObject, which reads it as an LBData; for an ordinary
// binary that is whatever the binary happens to contain.
static Ref
FGetBinaryStoredSize(RefArg /*rcvr*/, RefArg obj)
{
	if (!IsLargeBinary(obj))
		return MAKEINT(0);		// NOT YET: StorageSizeOfLargeObject over the binary's data
	return MAKEINT(0);
}


// ROM 0x0010039c FLBClearCache
// ClearVBOCache(obj): the paged-in copy written back and let go.
static Ref
FLBClearCache(RefArg /*rcvr*/, RefArg obj)
{
	if (!IsLargeBinary(obj))
		return NILREF;
	return NILREF;				// NOT YET: FlushLargeObject
}


// ROM 0x001002a4 FLBRollback
// VBOUndoChanges(obj): the changes made since it was paged in thrown
// away, and every Ref into it declawed.
//
// BUG (the ROM's): like GetVBOStoredSize this does not check that the
// object is a large binary first - it reads the LBData out of any object
// it is given.
static Ref
FLBRollback(RefArg /*rcvr*/, RefArg obj)
{
	if (!IsLargeBinary(obj))
		return NILREF;
	return NILREF;				// NOT YET: AbortObject and the declawing
}


void
RegisterLargeBinaryNatives(void)
{
	RegisterNativeFunction("FIsLargeBinary", (void*) FIsLargeBinary, 1);
	RegisterNativeFunction("FGetBinaryStore", (void*) FGetBinaryStore, 1);
	RegisterNativeFunction("FGetBinaryCompander", (void*) FGetBinaryCompander, 1);
	RegisterNativeFunction("FGetBinaryCompanderData", (void*) FGetBinaryCompanderData, 1);
	RegisterNativeFunction("FGetBinaryStoredSize", (void*) FGetBinaryStoredSize, 1);
	RegisterNativeFunction("FLBClearCache", (void*) FLBClearCache, 1);
	RegisterNativeFunction("FLBRollback", (void*) FLBRollback, 1);
}


// ROM 0x001466ec GetExceptionErr__FP9Exception
// The error code an exception carries: out of memory's data (kError_No_Memory
// when none), a frames exception's errorCode slot, a message exception's
// data; else kError_No_Memory... no: -8007 (an unknown exception).
long
GetExceptionErr(Exception* exception)
{
	long err = -8007;
	if (Subexception(exception->name, exOutOfMemory))
	{
		err = (long) (Long) exception->data;
		if (err == 0)
			err = kError_No_Memory;
	}
	else if (Subexception(exception->name, "type.ref"))
	{
		RefVar data(**(Ref**) exception->data);
		if (IsFrame(data))
		{
			RefVar code(GetFrameSlotRef(data, RSSYMerrorcode));
			if (ISINT(code))
				err = RINT(code);
		}
	}
	else if (Subexception(exception->name, "evt.ex.msg"))
		err = (long) (Long) exception->data;
	return err;
}
