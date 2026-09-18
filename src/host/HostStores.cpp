/*
	File:		host/HostStores.cpp

	Contains:	HostMountStores (HostStores.h).
*/

#include "HostStores.h"

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
				RegisterTStore(store);
		}
	}
	newton_catch_all
	{
		fprintf(stderr, "[host] the internal store threw %s (%ld) while mounting\n",
				CurrentException()->name, (long) (intptr_t) CurrentException()->data);
	}
	end_try;
}
