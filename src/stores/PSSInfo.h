/*
	File:		stores/PSSInfo.h

	Contains:	SPSSStoreInfo, what the PSS manager (stores/flash/
				PSSManager.h) keeps about a store on a card - and what the
				store frames' card methods read (Soups.h: store:CardSlot(),
				store:CardType()).  Here, below both, so that the store
				frames need not know the PSS manager.
*/

#ifndef __PSSINFO_H
#define __PSSINFO_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

class TStore;
class TFlash;
class TCardHandler;

// What the PSS manager keeps about a store on a card (0x50 bytes, four a
// socket), and what a card's store is initialised from (TFlashStore::Init's
// pssInfo).  The field names are ours.
struct SPSSStoreInfo
{
	void			Clear(void);			// ROM 0x00155bbc Clear__13SPSSStoreInfoFv

	TObjectId		fPhysId;			// +00 the device's phys, as the card server named it
	ULong			fField04;			// +04
	ULong			fPhys[2];			// +08 a TUPhys of it (a TUObject: the id and whether it is ours)
	TStore*			fStore;				// +10 the TMuxStore round it
	ULong			fSize;				// +14
	ULong			fSocket;			// +18
	char*			fBase;				// +1C where the device is mapped (gCardPSSVAddr + its offset)
	ULong			fOffset;			// +20 the device's offset in common memory
	ULong			fField24;			// +24
	TCardHandler*	fCardHandler;		// +28
	TFlash*			fFlash;				// +2C a flash device's TFlash
	ULong			fType;				// +30 'flsh', 'sram', 'rom '
	ULong			fField34;			// +34
	UChar			fBadCIS;			// +38 the card's CIS was bad (it is formatted afresh)
	UChar			fInitialized;		// +39 InitializeCardStore has initialised the store
	UChar			fField3A[2];
	ULong			fEnvironment;		// +3C
	TStore*			fRawStore;			// +40 the store itself (a TFlashStore)
	ULong			fField44[3];		// +44
};

#endif	/* __PSSINFO_H */
