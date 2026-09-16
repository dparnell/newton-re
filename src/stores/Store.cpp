/*
	File:		stores/Store.cpp

	Contains:	The TStore interface's New(char*)/Delete() glue (Store.h)
				and the registration of the store implementations.
*/

#include "Store.h"
#include "PackageStore.h"
#include "host/HostStore.h"


// ROM 0x0037d2a8 New__6TStoreSFPc
TStore*
TStore::New(const char* implementation)
{
	TStore* p = (TStore*) AllocInstanceByName("TStore", implementation);
	return p != nil ? (TStore*) p->GlueNew() : nil;
}


// ROM 0x0037d2d4 Delete__6TStoreFv
void
TStore::Delete()
{
	GlueDelete();
}


// The implementations registered: the ROM registers TFlashStore and
// TMuxStore in InitPSSManager (0x001575f8) and TPackageStore in
// InitPackageSoups (0x00162a1c); the host has THostStore in place of the
// flash store.
void
RegisterStoreImplementations(void)
{
	TPackageStore::ClassInfo()->Register();
	THostStore::ClassInfo()->Register();
}
