/*
	File:		GenericSWI.h

	Contains:	GenericSWIHandler, the kernel side of SWI 5: the catch-all system
				call whose selector (r0) picks one of some 75 kernel routines and
				whose r1-r4 are their arguments.  Results beyond r0 are left in the
				calling task's saved r1-r3 for GenericWithReturnSWI to hand back.

				The user side issues it through GenericSWI(selector, ...) /
				GenericWithReturnSWI (UserGlobals.h); many kernel routines are
				dual-mode - they call themselves through it when not in
				supervisor mode.  docs/os600/swi-table.md lists every selector.

	Reconstructed from:	GenericSWIHandler 0x000d9adc
*/

#ifndef __GENERICSWI_H
#define __GENERICSWI_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

#include "os600/GenericSWISelectors.h"

const long kGenericSWI_UnknownSelector = -1;	// what the ROM returns for a selector it has no case for

long	GenericSWIHandler(ULong selector, ULong p1, ULong p2, ULong p3, ULong p4);

#endif	/* __GENERICSWI_H */
