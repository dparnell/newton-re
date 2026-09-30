/*
	File:		power/host/HostPowerSwitch.h

	Contains:	The host's power switch and backlight button: the platform
				driver's power switch interrupt (TVoyagerPlatform's GPIO line
				and its state machine, NOT YET RECONSTRUCTED) stood in for by
				a host interrupt source (hal/host/HostInterruptSources.h)
				that sends the power manager the press
				(SendPowerSwitchEvent) - so a press reaches it the way the
				MessagePad's does, from an interrupt.

				A press is made by newton's window (F12 the power switch,
				F11 the backlight button - host/HostKeyboard.cpp) or by a
				script (HostPowerSwitch(), HostBacklightButton()).  While
				the machine sleeps the switch wakes it instead
				(hal/host/HostPower.h).  DEVIATION (hardware).
*/

#ifndef __HOSTPOWERSWITCH_H
#define __HOSTPOWERSWITCH_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

void	HostRegisterPowerSwitch(void);			// the interrupt source registered (InitializePowerInterrupt)
void	HostPowerSwitchPress(ULong type);		// 'powr or 'bklt pressed - from any thread
void	HostInstallPowerGlobals(void);			// the scripts' HostPowerSwitch(), HostBacklightButton(), HostWakeAfter(ms), HostSleepCount()

#endif	/* __HOSTPOWERSWITCH_H */
