/*
	File:		stores/PackageStorePartHandler.cpp

	Contains:	TPackageStorePartHandler and InitPackageSoups
				(PackageStore.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "PackageStore.h"
#include "StoreWrapper.h"
#include "Soups.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "Protocols.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"
#include <stdio.h>


// ROM 0x0016019c __ct__24TPackageStorePartHandlerFv
TPackageStorePartHandler::TPackageStorePartHandler()
{ }


// ROM 0x001601dc Install__24TPackageStorePartHandlerFRC6PartId10SourceTypeP8PartInfo
// The part mounted as a TPackageStore where it lies and its store frame
// added to gPackageStores; the store is what removes it.  ==>
// kError_No_Memory when there is no store to be had, kError_Bad_Package
// for a streamed part or when making the frame throws an evt.ex (the
// store let go again); what the store's Init answers.
NewtonErr
TPackageStorePartHandler::Install(const PartId& /*partId*/, SourceType sourceType, PartInfo* partInfo)
{
	TStore* store = (TStore*) NewByName("TStore", "TPackageStore");
	if (store == nil)
		return kError_No_Memory;
	if (!IsMemory(sourceType))
		return kError_Bad_Package;
	NewtonErr err = store->Init(GetSourcePtr(), partInfo->sizeInMemory, 0, 0, 0, nil);
	if (err != noErr)
		return err;
	newton_try
	{
		RefVar storeObject(MakeStoreObject(store));
		RefVar stores(gPackageStores);
		AddArraySlot(stores, storeObject);
	}
	newton_catch("evt.ex")
	{
		store->Delete();
		err = kError_Bad_Package;
	}
	end_try;
	if (err == noErr)
		SetRemoveObjPtr((RemoveObjPtr) store);
	return err;
}


// ROM 0x001603e0 Remove__24TPackageStorePartHandlerFRC6PartIdUll
// The store frame over the part's store taken out of gPackageStores and
// killed, and its wrapper let go.  ==> kPackageError_Base when the store
// is not in the list.
NewtonErr
TPackageStorePartHandler::Remove(const PartId& /*partId*/, PartType /*partType*/, RemoveObjPtr removePtr)
{
	RefVar storeObject;
	long count = Length(gPackageStores);
	long slot;
	TStoreWrapper* wrapper = nil;
	for (slot = 0; slot < count; slot++)
	{
		storeObject = GetArraySlotRef(gPackageStores, slot);
		wrapper = (TStoreWrapper*) GetFrameSlotRef(storeObject, RSSYMstore);
		if ((RemoveObjPtr) wrapper->Store() == removePtr)
			break;
	}
	if (slot == count)
		return kPackageError_Base;
	RefVar stores(gPackageStores);
	ArrayMunger(stores, slot, 1, RefVar(NILREF), 0, 0);
	KillStoreObject(storeObject);
	if (wrapper != nil)
		delete wrapper;
	return noErr;
}


// ROM 0x00160794 InitPackageSoups__Fv
// The package stores' list made, TPackageStore registered with the
// protocol registry, and a 'soup part handler set up in the world that is
// running (the newt world, whose boot this is part of).
// DEVIATION: the list is a GC root already (InitObjects makes it one on
// the host).  A host program may run the object system with no OS at all
// (the newtonscript tool, the unit tests), which has no protocol registry
// and no package manager: there only the list is made.
NewtonErr
InitPackageSoups(void)
{
	gPackageStores = AllocateArray(RSSYMarray, 0);
	if (gProtocolRegistry == nil)
		return noErr;
	TPackageStore::ClassInfo()->Register();
	TPackageStorePartHandler* handler = new TPackageStorePartHandler;
	return handler->Init('soup');
}
