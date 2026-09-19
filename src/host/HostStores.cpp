/*
	File:		host/HostStores.cpp

	Contains:	HostMountStores (HostStores.h).
*/

#include "HostStores.h"

#include "FactorySoups.h"
#include "Soups.h"
#include "Store.h"
#include "host/HostStore.h"
#include "ObjectHeap.h"
#include "Frames.h"
#include "OSErrors.h"

#include <stdio.h>


// As much flash as a MessagePad 2100 has, so that a boot which asks how
// much room there is gets an answer of about the right size.
enum { kHostStoreSize = 4 * 1024 * 1024 };

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
		InitQueries();

		TStore* store = (TStore*) THostStore::ClassInfo()->New();
		if (store == nil)
			fprintf(stderr, "[host] no memory for the internal store\n");
		else
		{
			NewtonErr err = store->Init(nil, kHostStoreSize, 0, 0, kStoreIsInternal, nil);
			if (err == noErr)
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
