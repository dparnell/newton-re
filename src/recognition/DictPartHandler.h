/*
	File:		recognition/DictPartHandler.h

	Contains:	TDictPartHandler: the package part handler for 'dict parts,
				dictionaries delivered in a package.  The part is a frames
				part whose top-level frame is {dictionaries: {dictionaryList:
				[...]}}; each frame of the list is copied into the heap (its
				slot names made the heap's own symbols) and registered as a
				dictionary with AddDictionary - one that has a true `custom`
				slot as one of the writer's own - and its dictID kept, so
				that when the part goes each one is disposed of again
				(FDisposeDictionary: taken out of the list and its Airus
				dictionary closed).  A part whose list is empty is refused
				(kError_Bad_Package).  The frame part handlers' 'form and
				'auto are frames/FramePartHandler.h's; the newt world
				registers this one between 'book and 'auto.

				DEVIATION: the part's objects are imported into a host area
				first (packages/FramePartHandler.h's ImportPackagePart), and
				the area is kept until the part is removed, since the frames
				registered still refer to the dictionary data in it.

	Reconstructed from the MP2x00 US ROM (0x0008fa44, 0x0008fd5c-0x00090284);
	each function cites its origin.
*/

#ifndef __DICTPARTHANDLER_H
#define __DICTPARTHANDLER_H

#ifndef __PARTHANDLER_H
#include "PartHandler.h"
#endif
#ifndef __OBJECTS_H
#include "objects.h"
#endif

class TDictPartHandler : public TPartHandler
{
public:
	virtual	NewtonErr	Install(const PartId& partId, SourceType sourceType, PartInfo* partInfo);
	virtual	NewtonErr	Remove(const PartId& partId, PartType partType, RemoveObjPtr removePtr);

	NewtonErr			AddDictionaries(RefArg part, RefArg dictIds);

protected:
	virtual	NewtonErr	Expand(void* data, CPipe* pipe, PartInfo* info);
};

Ref		FDisposeDictionary(RefArg rcvr, RefArg dictId);		// ROM 0x0008fa44 FDisposeDictionary__FRC6RefVarT1

#endif	/* __DICTPARTHANDLER_H */
