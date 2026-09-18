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
Boolean		IsSuperMode(void);			// in supervisor mode (ROM 0x0038ad90: the CPSR mode bits); a host never is
ULong		GetCPUMode(void);			// the CPSR mode bits (ROM 0x003a4e7c): 0x10 user, 0x11 FIQ, 0x12 IRQ, 0x13 supervisor
ULong		GetRamSize(void);			// bytes of RAM fitted (ROM: TRAMTable::GetRamSize 0x001206ec)
void		GetStackBounds(const void** low, const void** high);	// the current thread of execution's stack (frames OnStack, ROM 0x002f58f0, reads the Newt globals)
NewtonErr	GetSystemSerialNumber(ULong serialNumber[2]);	// the machine's own number, two words (TSerialNumberROM::GetSystemSerialNumber)
}

#endif	/* __HAL_SYSTEM_H */
