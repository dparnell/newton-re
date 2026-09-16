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

extern long	gObjectHeapSize;			// host: the size InitObjects gives the object heap (the ROM asks InternalRAMInfo)

// beyond objects.h
Ref		AllocateMapWithTags(RefArg superMap, RefArg tags);

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
Boolean	IsReal(RefArg ref);
Ref		MakeSymbol(char* name);
TObjectIterator*	NewTObjectIterator(RefArg obj);
void	DeleteTObjectIterator(TObjectIterator* iterator);
Boolean	RegisterRangeForDeclawing(ULong start, ULong end);

#endif	/* __FRAMES_H */
