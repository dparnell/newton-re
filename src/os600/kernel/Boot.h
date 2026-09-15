/*
	File:		Boot.h

	Contains:	The kernel's boot: OsBoot, entered from the reset code once the
				C globals exist, builds the kernel's objects (object table, memory
				architecture, scheduler, timers, object manager, the null port),
				makes the idle task out of the boot context and the first user
				task ('user', running UserBoot), starts the clocks and becomes the
				idle task (SleepTask).

	Reconstructed from:	OsBoot 0x00149c1c, InitCGlobals 0x00045c84 (the memory object
				database), InitGlobalWorld 0x000fb658,
				InitKernelDomainAndEnvironment 0x000ea698, InitMemArchCore 0x0011e660,
				StartTime 0x0013ec34, RestartTimerOverflowDetect 0x0013ebac,
				TaskInCopyKilled 0x001e22c8, InitSMemManager 0x001e2804,
				SleepTask 0x001ce924
*/

#ifndef __BOOT_H
#define __BOOT_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

class TEnvironment;

void		OsBoot();								// does not return on the MessagePad; on a host, returns when the run ends
void		InitGlobalWorld();
void		InitKernelDomainAndEnvironment();
void		InitMemArchCore();
void		StartTime();
void		SleepTask();							// the idle loop
void		InitMemObjDatabase(ULong ramSize);		// InitCGlobals's part: pick the domain table, build the database

extern void*			gKernelHeap;				// 0x0c101170
extern TObjectId		gKernelDomainId;			// 0x0c101260

#endif	/* __BOOT_H */
