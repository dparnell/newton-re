/*
	File:		KernelGlobals.h

	Contains:	Kernel-wide global variables.  In the ROM these live in the
				kernel data area at 0x0C100800 (RAM_RW / RAM_ZI in Ghidra) and
				carry these names in the symbol table.

				Defined in KernelGlobals.cpp; the boot code initialises them.
*/

#ifndef __KERNELGLOBALS_H
#define __KERNELGLOBALS_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

class TObjectTable;
class TTask;

extern TObjectTable*	gObjectTable;				// 0x0c1010b8  the kernel object table
extern TObjectTable*	gTheMemArchObjTbl;			// 0x0c101254  physical pages (kPhysType/kExtPhysType) live here
extern TTask*			gCurrentTask;				// 0x0c1010e8  task currently running
extern ULong			gNextGlobalUniqueId;		// 0x0c102638  last object number handed out
extern Boolean			gWrappedGlobalUniqueId;		// 0x0c102634  set once the object numbers have wrapped

#endif	/* __KERNELGLOBALS_H */
