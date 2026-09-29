/*
	File:		hal/System.h

	Contains:	Whole-machine control and identity: reset, power off, masking
				every interrupt, and the machine's serial number.  Each hardware
				or host port provides an implementation.

	ROM:		Reset (the reset vector, 0x0 -> BootOS), DisableAllInterrupts,
				IOPowerOffAll (Voyager platform code),
				TSerialNumberROM::GetSystemSerialNumber 0x001dfb38
*/

#ifndef __HAL_SYSTEM_H
#define __HAL_SYSTEM_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

extern "C" {
NewtonErr	Reset(void);				// reboot the machine; does not return on hardware
void		DisableAllInterrupts(void);
void		IOPowerOffAll(void);
Boolean		IsSuperMode(void);			// in a privileged mode (ROM 0x00394410: the CPSR mode bits are not user); a host is while it delivers an interrupt
ULong		GetCPUMode(void);			// the CPSR mode bits (ROM 0x003a4e7c): 0x10 user, 0x11 FIQ, 0x12 IRQ, 0x13 supervisor
ULong		GetRamSize(void);			// bytes of RAM fitted (ROM: TRAMTable::GetRamSize 0x001206ec)
void		GetStackBounds(const void** low, const void** high);	// the current thread of execution's stack (frames OnStack, ROM 0x002f58f0, reads the Newt globals)
NewtonErr	GetSystemSerialNumber(ULong serialNumber[2]);	// the machine's own number, two words (TSerialNumberROM::GetSystemSerialNumber)
// The processor: its kind (the ROM's LowLevelGetCPUType 0x0001942c reads
// the coprocessor's ID register - 1 an ARM610, 2 an ARM710, 3 a
// StrongARM SA-110) and its clock in MHz as a 16.16 number, which
// InitCGlobals works out by timing LowLevelProcSpeed's loop (0x00019464)
// against the timer: 0xa22f1b (162.2 MHz) for the MP2x00's SA-110, or
// 0x6385a2 (99.5) for a slower one; 20 for an ARM610, 24.9 for a 710.
ULong		LowLevelGetCPUType(void);
Fixed		GetCPUClockSpeed(void);
}

#endif	/* __HAL_SYSTEM_H */
