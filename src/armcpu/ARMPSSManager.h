/*
	File:		armcpu/ARMPSSManager.h

	Contains:	The PSS manager's slots as a card driver package's ARM code
				reaches them through the ROM global gPSSManager
				(ARMPSSManager.cpp; docs/armcpu/README.md, "The PSS
				manager's slots").
*/

#ifndef __ARMPSSMANAGER_H
#define __ARMPSSMANAGER_H

#include <stdint.h>

// the global and the view of the slots mapped (once)
void		InstallARMPSSManager(void);
// A PSS info the ARM code made itself (a driver's own, which may be longer
// than the ROM's 0x50 bytes - ATA Support's store reads past them in Init)
// as its ARM address, the host's shadow of it written back first; 0 for
// any other.
struct SPSSStoreInfo;
uint32_t	ARMStoreInfoAddress(const SPSSStoreInfo* info);

// where the view is in the ARM world (what gPSSManager reads as)
uint32_t	ARMPSSManagerView(void);

#endif	/* __ARMPSSMANAGER_H */
