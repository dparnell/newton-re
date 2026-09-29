/*
	File:		Reboot.cpp

	Contains:	Rebooting and restarting.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	These ROM functions serve both modes: in user mode they issue GenericSWI
	28/29 instead.  That path belongs to the user-side syscall layer; these
	are the kernel-mode bodies.

	The ROM's SGlobalsThatLiveAcrossReboot differs from the DDK's: the boot
	counter it tests is at +0x1c4, the DDK's fUnsuccessfulBootCount at +0x1c8
	(the DDK's CompactState is 4 bytes larger).  The member is used by name.
*/

#include "Reboot.h"
#include "KernelGlobals.h"
#include "OSErrors.h"
#include "VirtualMemory.h"
#include "hal/Atomic.h"
#include "hal/System.h"


// ROM 0x000d9884 Reboot__FlUlUc
// Records why, then resets - unless `safe` and a reboot-protected monitor
// call is in progress, in which case the reboot happens when it ends
// (TMonitor::Release).  A cold-boot request (kRebootMagicNumber) clears a
// recorded reason other than kError_Reboot_Calibration_Missing.
NewtonErr
Reboot(NewtonErr error, ULong rebootType, Boolean safe)
{
	EnterFIQAtomic();
	SGlobalsThatLiveAcrossReboot& g = gGlobalsThatLiveAcrossReboot;
	if (g.fRebootReason != kError_Reboot_Calibration_Missing)
		g.fRebootReason = error;
	g.fMagicNumber = kRebootMagicNumber;
	if (rebootType == kRebootMagicNumber)
	{
		if (error != kError_Sorry_System_Failure && g.fRebootReason != kError_Reboot_Calibration_Missing)
			g.fRebootReason = 0;
	}
	if (safe && gRebootProtectCount != 0)
	{
		gWantReboot = true;
		ExitFIQAtomic();
		return noErr;
	}
	if (g.fUnsuccessfulBootCount > kMaxUnsuccessfulBoots)
	{
		DisableAllInterrupts();
		IOPowerOffAll();
		for (;;)
			;
	}
	return Reset();
}


// ROM 0x000d9970 CantThrowInUndefinedModeReboot
void
CantThrowInUndefinedModeReboot()
{
	EnterFIQAtomic();
	SGlobalsThatLiveAcrossReboot& g = gGlobalsThatLiveAcrossReboot;
	if (g.fRebootReason != kError_Reboot_Calibration_Missing)
		g.fRebootReason = kError_Sorry_System_Error;
	g.fMagicNumber = kRebootMagicNumber;
	if (g.fUnsuccessfulBootCount > kMaxUnsuccessfulBoots)
	{
		DisableAllInterrupts();
		IOPowerOffAll();
		for (;;)
			;
	}
	Reset();
}


// ROM 0x000d9984 Restart__Fv
void
Restart()
{
	EnterFIQAtomic();
	Reset();
}


// ROM 0x000e6bbc PowerOffAndReboot__Fl
// DEVIATION: the ROM first turns the machine off - FIQs off, every I/O
// power supply off, the interrupts disabled, the GPIO interface reset so
// that only the power switch wakes it, PowerOffSystem - which on a host
// is nothing; then it reboots as here.
void
PowerOffAndReboot(NewtonErr error)
{
	Reboot(error, 0, true);
}
