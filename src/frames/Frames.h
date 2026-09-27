/*
	File:		frames/Frames.h

	Contains:	The NewtonScript object system for its clients: the DDK's
				objects.h (refs, RefVars, objects, symbols, frames, arrays,
				the GC hooks), the symbol constants (RSSymbols.h), the error
				codes (NSErrors.h), and the few ROM functions the DDK header
				does not declare.  InitObjects (objects.h) starts it; the
				memory manager (NewPtr) and the exception handlers must be
				running first.

	The implementation is ObjectHeap.cpp (the heap, refs to objects), GC.cpp
	(the collector), Objects.cpp (the API) and Symbols.cpp; ObjectHeap.h is
	their internal header.  docs/frames/README.md has the notes.
*/

#ifndef __FRAMES_H
#define __FRAMES_H

#ifndef __OBJHEADER_H
#include "ObjHeader.h"
#endif
#ifndef __OBJECTS_H
#include "objects.h"
#endif
#ifndef __RSSYMBOLS_H
#include "RSSymbols.h"
#endif
#ifndef __NSERRORS_H
#include "NSErrors.h"
#endif
#ifndef __NEWTQD_H
#include "NewtQD.h"
#endif

extern long	gObjectHeapSize;			// host: the size InitObjects gives the object heap (the ROM asks InternalRAMInfo)

// beyond objects.h
Ref		AllocateMapWithTags(RefArg superMap, RefArg tags);
Ref		MakeArray(long length);				// an array of class 'Array
long	GetExceptionErr(Exception* exception);	// ROM 0x001466ec GetExceptionErr__FP9Exception - the error code an exception carries
long	FramesException(Exception* exception);	// ROM 0x00129490 FramesException__FP9Exception - a ref exception's errorCode (-1: none), any other's data
Ref		AddressToRef(void* p);				// a pointer as an integer Ref (the ROM's "magic" C objects: views, clippers)
void*	RefToAddress(Ref r);
Ref		MakeString(const UniChar* str, long length);		// a string of the first length characters (ROM 0x0012ac84)
void	ArrayGrowAt(RefArg array, long index, long count);	// count empty slots opened at the index (ROM 0x0012a860)
Ref		Munger(RefArg obj, long start, long count, const void* data, long dataLength);	// count bytes at start replaced by the data, a read-only object cloned; ==> the object (ROM 0x0012b5d0)
Ref		ToObject(const Rect& r);			// a bounds frame {left, top, right, bottom} (a clone of canonicalRect)
Boolean	FromObject(RefArg obj, Rect& r);	// the rect of a bounds frame; ==> whether its four slots are integers
Ref		SetBoundsRect(RefArg frame, const Rect& r);

// a frame's tag with the index of its slot (GetFrameMapTags: the stores sort them)
struct SortedMapTag
{
	Ref		fTag;			// +0x00
	long	fIndex;			// +0x04
};
long	GetMapTags(Ref map, SortedMapTag* tags);
void	GetFrameMapTags(Ref frame, SortedMapTag* tags, Boolean sorted);
Ptr		LockedBinaryPtr(RefArg obj);
void	LockRefArg(RefArg obj);
void	UnlockRefArg(RefArg obj);
long	FrameSlotPosition(Ref frame, Ref tag);
void	SetFramePathFor1XFunctions(RefArg obj, RefArg thePath, RefArg value);
Ref		SharedFrameMap(RefArg frame);
UniChar* CString(RefArg str);
Ref		FStringer(RefArg rcvr, RefArg array);		// (StringNatives.cpp: ROM 0x001fd644) - the elements written one after another
Ref		FStrEqual(RefArg rcvr, RefArg a, RefArg b);	// (StringNatives.cpp: ROM 0x001fedf4) - the same characters, cases apart
Boolean	IsReal(RefArg ref);
Ref		MakeSymbol(char* name);
TObjectIterator*	NewTObjectIterator(RefArg obj);
void	DeleteTObjectIterator(TObjectIterator* iterator);
Boolean	RegisterRangeForDeclawing(ULong start, ULong end);

// the globals frame, which is gVarFrame
Ref		FGetGlobals(RefArg rcvr);			// ROM 0x002b72b8 FGetGlobals

// the sorted-array natives (ArrayNatives.cpp) other units call: BInsert(array, element, test, keyPath, uniqueOnly)
Ref		FBInsert(RefArg rcvr, RefArg array, RefArg element, RefArg test, RefArg keyPath, RefArg uniqueOnly);

// large binaries (NOT YET RECONSTRUCTED: never one; objects.h declares it under hasLargeObjects)
Boolean	IsLargeBinary(RefArg ref);
void	RegisterLargeBinaryNatives(void);	// the seven a script asks of one (Objects.cpp)

#endif	/* __FRAMES_H */
