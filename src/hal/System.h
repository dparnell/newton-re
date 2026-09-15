/*
	File:		hal/System.h

	Contains:	Whole-machine control: reset, power off, and masking every
				interrupt.  Each hardware or host port provides an implementation.

	ROM:		Reset (the reset vector, 0x0 -> BootOS), DisableAllInterrupts,
				IOPowerOffAll (Voyager platform code)
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
}

#endif	/* __HAL_SYSTEM_H */
