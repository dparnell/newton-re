/*
	File:		host/HostStores.cpp

	Contains:	HostMountStores (HostStores.h).
*/

#include "StoreCompander.h"
#include "HostStores.h"

#include "FactorySoups.h"
#include "Soups.h"
#include "Store.h"
#include "Protocols.h"
#include "host/HostStore.h"
#include "ObjectHeap.h"
#include "Frames.h"
#include "OSErrors.h"

#include <stdio.h>


// As much flash as a MessagePad 2100 has, so that a boot which asks how
// much room there is gets an answer of about the right size.
enum { kHostStoreSize = 4 * 1024 * 1024 };

static const char*	gStoreFile = nil;
static Boolean		gRestored = false;

void
HostSetStoreFile(const char* path)
{
	gStoreFile = path;
}


Boolean
HostStoreWasRestored(void)
{
	return gRestored;
}

static bool	gMounted = false;


// The store prepared the way a used machine's is: the soups the ROM's own
// applications keep, made if they are not there already.  Their names and
// index lists are the ROM's (FactorySoups.h, generated from the packages'
// soup supervisors by analysis/soupdefs.py), so nothing here is invented -
// only the moment is, the applications having made them long before the
// boot on a machine that has ever been switched on.
void
HostPrepareStore(RefArg store)
{
	for (long i = 0; i < gFactorySoupCount; i++)
	{
		RefVar name(MakeString(gFactorySoups[i].fName));
		if (NOTNIL(StoreHasSoup(store, name)))
			continue;
		RefVar indexes(MAKEMAGICPTR(gFactorySoups[i].fIndexes));
		if (!IsArray(indexes))
		{
			fprintf(stderr, "[host] no indexes for the %s soup: the ROM's objects are not in\n",
					gFactorySoups[i].fName);
			continue;
		}
		StoreCreateSoup(store, name, indexes);
	}
}


void
HostMountStores(void)
{
	if (gMounted)
		return;
	gMounted = true;

	// a store that will not mount is worth saying so about, but not worth
	// taking the machine down over: the boot goes on without one
	newton_try
	{
		// the store implementations the rest of the boot makes by name: the
		// ROM registers TFlashStore and TMuxStore in InitPSSManager
		// 0x001575f8 and TPackageStore in InitPackageSoups 0x00162a1c, both
		// of which are NOT YET RECONSTRUCTED.  The package loader needs
		// TPackageStore for a package that carries a store part of its own.
		// (newtonscript mounts a store without the OS running, and there is
		// no registry then - nothing makes a store by name there either)
		// ... and the store companders, which the ROM registers in
		// InitializeStoreDecompressors 0x001fa9fc, called from
		// RegisterROMDomainManager 0x001b0e30 in InitialKSRVTask (NOT YET
		// RECONSTRUCTED): a large binary (a VBO) cannot be made without them.
		if (gProtocolRegistry != nil)
		{
			RegisterStoreImplementations();
			InitializeStoreCompanders();
		}
		InitQueries();

		TStore* store = (TStore*) THostStore::ClassInfo()->New();
		if (store == nil)
			fprintf(stderr, "[host] no memory for the internal store\n");
		else
		{
			NewtonErr err = store->Init(nil, kHostStoreSize, 0, 0, kStoreIsInternal, nil);
			// said before it is formatted: MakeStoreObject signs the internal
			// store with the machine's serial number and any other store at
			// random, and the ROM notifies about a store whose signature does
			// not match the machine's
			SetInternalStore(store);
			// the file it is kept in, if one was named: a store that comes
			// back out of it is a machine that has been used before, and
			// must not be formatted over
			gRestored = ((THostStore*) store)->SetBackingFile(gStoreFile);
			if (err == noErr && !gRestored)
				err = store->Format();
			if (err != noErr)
				fprintf(stderr, "[host] the internal store would not format (%ld)\n", (long) err);
			else
			{
				RegisterTStore(store);
				RefVar stores(GetStores());
				if (IsArray(stores) && Length(stores) > 0)
					HostPrepareStore(RefVar(GetArraySlotRef(stores, 0)));
			}
		}
	}
	newton_catch_all
	{
		fprintf(stderr, "[host] the internal store threw %s (%ld) while mounting\n",
				CurrentException()->name, (long) (intptr_t) CurrentException()->data);
	}
	end_try;
}
