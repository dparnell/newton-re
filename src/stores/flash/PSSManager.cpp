/*
	File:		stores/flash/PSSManager.cpp

	Contains:	The PSS manager's internal half: the internal store made at
				boot - a TFlashStore on the internal flash, wrapped in a
				TMuxStore - and registered as "InRAMStore".

				NOT YET: the TPSSManager world itself (the 'pssm task that
				makes a store for each card the card server announces, and
				the alerts when a card in use goes), a RAM internal store
				(no flash: InternalStoreInfo's RAM sizes, the persistent
				'rams entry), and the reserved block's accessor.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "PSSManager.h"
#include "FlashStore.h"
#include "MuxStore.h"
#include "MemoryAllocator.h"
#include "Soups.h"
#include "NameServer.h"


// ROM 0x0011e250 InternalStoreInfo
// 0: the internal flash's store size (nought without flash); 3: 0x1100000.
// NOT YET: 1 and 2, a RAM internal store's size and its persistent part,
// worked out from the RAM there is when there is no flash.
ULong
InternalStoreInfo(int which)
{
	if (which == 3)
		return 0x1100000;
	ULong size = gInternalFlashStoreSize;
	if (which != 0)
		size = 0;
	return size;
}


// (ROM 0x000453b4 InitCGlobals, the part about the flash)
// At boot, before there is a heap, the internal flash is found once out of
// a borrowed page with its windows mapped (kMapWindows), the reserved
// block read, and the instance thrown away; the instance the store uses
// later relies on those windows.  NOT YET: the reserved block's accessor
// (TReservedBlockAccessor: the calibration and the patches).
// DEVIATION: out of the heap rather than the early-boot page.
NewtonErr
MapInternalFlashWindows(void)
{
	alignas(TNewInternalFlash) char memory[sizeof(TNewInternalFlash)];
	TNewInternalFlash::ClassInfo()->MakeAt(memory);
	TNewInternalFlash* flash = (TNewInternalFlash*) memory;
	NewtonErr err = flash->InitForReservedBlock(THeapAllocator::GetGlobalAllocator(), TNewInternalFlash::kMapWindows);
	flash->CleanUp();
	return err;
}


// ROM 0x001553cc InitPSSManager__FUlT1
// The internal store: its implementations registered, a TFlashStore made
// on the internal flash (when there is one) and wrapped in a TMuxStore,
// formatted if it needs it, and named.
NewtonErr
InitPSSManager(ULong environment, ULong)
{
	TUNameServer nameServer;
	TMuxStore::ClassInfo()->Register();
	TMuxStoreMonitor::ClassInfo()->Register();
	TFlashStore::ClassInfo()->Register();
	TNewInternalFlash::ClassInfo()->Register();
	TStore* mux = TStore::New("TMuxStore");
	TStore* store = TStore::New("TFlashStore");
	if (mux != nil && store != nil)
	{
		mux->SetStore(store, environment);
		ULong size = InternalStoreInfo(0);
		if (size == 0)
			return kError_Call_Not_Implemented;		// NOT YET: a RAM internal store
		TFlash* flash = TFlash::New("TNewInternalFlash");
		((TNewInternalFlash*) flash)->Init(THeapAllocator::GetGlobalAllocator());
		store->Init(nil, size, environment, 0, kFlashStoreUsesTFlash, flash);
		gInRAMStore = store;
		gMuxInRAMStore = mux;
		Boolean needsFormat;
		mux->NeedsFormat(&needsFormat);
		if (needsFormat)
			mux->Format();
		PSSId root;
		mux->GetRootId(&root);
		long rootSize;
		mux->GetObjectSize(root, &rootSize);
		nameServer.RegisterName("InRAMStore", "TStore", (ULong) mux, 0);
		// NOT YET: the TPSSManager world ('pssm)
		return noErr;
	}
	if (mux != nil)
		mux->Delete();
	if (store != nil)
		store->Delete();
	return -108;
}
