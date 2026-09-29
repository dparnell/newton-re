/*
	File:		Reboot.h

	Contains:	Rebooting and restarting.  The reboot reason and a few other
				things survive a warm reboot in gGlobalsThatLiveAcrossReboot
				(SGlobalsThatLiveAcrossReboot, VirtualMemory.h); after too many
				unsuccessful boots in a row the machine powers off instead.

	Reconstructed from:	Reboot 0x000da8fc, CantThrowInUndefinedModeReboot 0x000da9e8,
				Restart 0x000da9fc
*/

#ifndef __REBOOT_H
#define __REBOOT_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

const ULong kMaxUnsuccessfulBoots = 12;		// more than this and Reboot powers off instead of resetting

NewtonErr	Reboot(NewtonErr error, ULong rebootType, Boolean safe);	// GenericSWI 28; safe: honour reboot protection
void		Restart();													// GenericSWI 29

// The machine turned off - everything powered down, the interrupts off -
// and then rebooted with the error as the reason (safe).  What the flash
// code does when it cannot trust the flash any longer.
void		PowerOffAndReboot(NewtonErr error);							// ROM 0x000e6bbc PowerOffAndReboot__Fl
void		CantThrowInUndefinedModeReboot();

#endif	/* __REBOOT_H */
