/*
	File:		recognition/DictPartHandler.cpp

	Contains:	TDictPartHandler, the 'dict package part handler
				(DictPartHandler.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "DictPartHandler.h"
#include "Dictionaries.h"
#include "Words.h"
#include "Airus.h"
#include "FramePartHandler.h"
#include "FramesPart.h"
#include "ObjectAreaImport.h"
#include "ObjectStreamer.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "REPTranslators.h"
#include "RSSymbols.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"

extern const ExceptionName exPipeException;

// What Remove gets back: the ids of the dictionaries registered (the
// ROM's is a word holding the ref handle), and on the host the part's
// imported area (DEVIATION, DictPartHandler.h).
struct DictPartRemoveObject
{
	RefStruct*				fDictIds;
	TImportedObjectArea*	fArea;
};


// ROM 0x0008fa44 FDisposeDictionary__FRC6RefVarT1
// The dictionary of that id taken out of the list (Unregister) and its
// Airus dictionary disposed of.  ==> true; nil when there is no such
// dictionary.
Ref
FDisposeDictionary(RefArg /*rcvr*/, RefArg dictId)
{
	ULong id = (ULong) RINT(dictId);
	RefVar frame(FindDictionaryFrame(id));
	dictListEntry* entry = FindDictionaryEntry(id);
	if (entry != nil && NOTNIL(frame))
	{
		Handle dictionary = entry->fDictionary;
		if (dictionary != nil)
		{
			FAirusUnregisterDictionary(frame);
			DisposDictionary(&dictionary);
			return TRUEREF;
		}
	}
	return NILREF;
}


// ROM 0x0008fd5c AddDictionaries__16TDictPartHandlerFRC6RefVarT1
// Each frame of the part's dictionaries.dictionaryList copied into the
// heap with its slot names made the heap's own symbols, registered with
// AddDictionary (as a custom dictionary when it has a non-nil `custom`
// slot), printed on the REP ("Dict-" and the frame), and its dictID added
// to dictIds.  ==> kError_Bad_Package for an empty list.
// (A custom dictionary is registered with the symbol 'custom as its
// custom slot, not the slot's own value.)
NewtonErr
TDictPartHandler::AddDictionaries(RefArg part, RefArg dictIds)
{
	RefVar list(GetFrameSlotRef(part, RSSYMdictionaries));
	list = GetFrameSlotRef(list, RSSYMdictionarylist);
	long count = Length(list);
	if (count == 0)
		return kError_Bad_Package;
	for (long i = 0; i < count; i++)
	{
		RefVar entry(GetArraySlotRef(list, i));
		RefVar copy(AllocateFrame());
		TObjectIterator iter(entry);
		while (!iter.Done())
		{
			RefVar tag(Intern(SymbolName(iter.fTag)));
			SetFrameSlot(copy, tag, iter.fValue);
			iter.Next();
		}
		if (!FrameHasSlot(entry, RSSYMcustom) || ISNIL(GetFrameSlotRef(entry, RSSYMcustom)))
			FAddDictionary(RefVar(NILREF), copy, RefVar(NILREF));
		else
			FAddDictionary(RefVar(NILREF), copy, RSSYMcustom);
		gREPout->Print("Dict-");
		PrintObject(copy, 0);
		gREPout->Print("\r");
		AddArraySlot(dictIds, RefVar(GetFrameSlotRef(copy, RSSYMdictid)));
	}
	return noErr;
}


// ROM 0x0008ffc8 Install__16TDictPartHandlerFRC6PartId10SourceTypeP8PartInfo
// The part's frame (where it lies for a part in memory, the array it
// begins with holding it; read through Expand otherwise) and its
// dictionaries registered; the ids kept for Remove.  ==> kError_Bad_Package
// when the part cannot be read or has no dictionaries.
NewtonErr
TDictPartHandler::Install(const PartId& /*partId*/, SourceType sourceType, PartInfo* partInfo)
{
	RefVar dictIds;
	RefVar part;
	TImportedObjectArea* area = nil;
	if (!IsMemory(sourceType))
	{
		Ref ref = NILREF;
		if (Copy(&ref) != noErr)
			return kError_Bad_Package;
		part = ref;
	}
	else
	{
		// DEVIATION: imported into a host area first (DictPartHandler.h)
		Boolean inROM = false;
		area = ImportPackagePart(GetSourcePtr(), partInfo, &inROM);
		if (area == nil)
			return kError_Bad_Package;
		part = GetArraySlotRef(MAKEPTR(area->fArea), 0);
	}
	dictIds = MakeArray(0);
	if (AddDictionaries(part, dictIds) != noErr)
	{
		if (area != nil)
			RemoveFramesPart(area);
		return kError_Bad_Package;
	}
	DictPartRemoveObject* removeObject = new DictPartRemoveObject;
	if (removeObject != nil)
	{
		removeObject->fDictIds = new RefStruct(NILREF);
		removeObject->fArea = area;
	}
	*removeObject->fDictIds = dictIds;		// (the ROM does not check the allocation either)
	SetRemoveObjPtr((RemoveObjPtr) removeObject);
	return noErr;
}


// ROM 0x00090100 Remove__16TDictPartHandlerFRC6PartIdUll
// Each dictionary the part registered disposed of.  ==> kError_Bad_Package
// when it registered none.
NewtonErr
TDictPartHandler::Remove(const PartId& /*partId*/, PartType /*partType*/, RemoveObjPtr removePtr)
{
	DictPartRemoveObject* removeObject = (DictPartRemoveObject*) removePtr;
	RefVar dictIds(*removeObject->fDictIds);
	TImportedObjectArea* area = removeObject->fArea;
	if (removeObject != nil)
	{
		delete removeObject->fDictIds;
		delete removeObject;
	}
	NewtonErr err = noErr;
	long count = Length(dictIds);
	if (count == 0)
		err = kError_Bad_Package;
	for (long i = 0; i < count; i++)
		FDisposeDictionary(RefVar(NILREF), RefVar(GetArraySlotRef(dictIds, i)));
	// DEVIATION: the imported objects go with the part (DictPartHandler.h)
	if (area != nil)
		RemoveFramesPart(area);
	return err;
}


// ROM 0x000901d8 Expand__16TDictPartHandlerFPvP5CPipeP8PartInfo
// A streamed part's object read (NSOF) into *data.  ==> a pipe
// exception's error; any other exception is passed on.
NewtonErr
TDictPartHandler::Expand(void* data, CPipe* pipe, PartInfo* /*info*/)
{
	NewtonErr err = noErr;
	TObjectReader reader(*pipe);
	newton_try
	{
		*(Ref*) data = reader.Read();
	}
	newton_catch(exPipeException)
	{
		err = (NewtonErr) (Long) CurrentException()->data;
	}
	end_try;
	return err;
}
