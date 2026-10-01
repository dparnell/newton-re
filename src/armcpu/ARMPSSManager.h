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
// where the view is in the ARM world (what gPSSManager reads as)
uint32_t	ARMPSSManagerView(void);

#endif	/* __ARMPSSMANAGER_H */
