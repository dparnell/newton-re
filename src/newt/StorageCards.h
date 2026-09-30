/*
	File:		newt/StorageCards.h

	Contains:	The application's half of a storage card (StorageCards.cpp):
				the PSS manager's 'stor and 'rstr events, the card's stores
				mounted and unmounted.
*/

#ifndef __STORAGECARDS_H
#define __STORAGECARDS_H

#ifndef __PSSMANAGER_H
#include "PSSManager.h"
#endif
#ifndef __FRAMES_H
#include "Frames.h"
#endif

void		StorageCardInserted(TNewStoreEvent* event);								// ROM 0x0030e4cc StorageCardInserted__FP14TNewStoreEvent
void		StorageCardRemoved(TNewStoreEvent* event);								// ROM 0x0030ec70 StorageCardRemoved__FP14TNewStoreEvent
NewtonErr	MountStore(TStore* store, SPSSStoreInfo* info);							// ROM 0x0030e658 MountStore__FP6TStoreP13SPSSStoreInfo
NewtonErr	UnmountStore(TStore* store);											// ROM 0x0030edb4 UnmountStore__FP6TStore
Boolean		CheckCardActiveProtocols(TNewStoreEvent* event);						// ROM 0x0030eb5c CheckCardActiveProtocols__FP14TNewStoreEvent
NewtonErr	CheckStoreVersion(TStore* store, int socket, long* version, UChar* convert);	// ROM 0x0030e3ac CheckStoreVersion__FP6TStoreiPlPUc
Ref			HandleCardStoreEvent(RefArg event, RefArg storeObject);				// ROM 0x0030d538 HandleCardStoreEvent__FRC6RefVarT1

#endif	/* __STORAGECARDS_H */
