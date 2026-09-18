/*
	File:		system/SystemNatives.h

	Contains:	What a script may ask of the machine itself.

				These are the ROM's NewtonScript functions over the
				hardware a Newton has and a host does not: the serial
				number chip, the battery, the backlight, the power switch.
				They sit here rather than in hal/ because they are frames
				code - they answer Refs - and the hardware underneath them
				is reached through hal/System.h, so a port supplies the
				machine and this supplies the script's view of it.

	NOT YET RECONSTRUCTED: the battery (FBatteryRawStatus 0x001ff0a4,
	FBatteryLevel 0x001ff0d4, FMinimumBatteryCheck 0x001ff270 - they read
	a PowerPlantStatus off the power manager), the backlight
	(FBackLightStatus 0x001ff2dc, FBackLight 0x001ff30c), FPowerOff
	0x001ff3d0 and FSetRandomSeed 0x001ff074.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#ifndef __SYSTEMNATIVES_H
#define __SYSTEMNATIVES_H

#include "objects.h"

Ref		FGetSerialNumber(RefArg rcvr);

void	RegisterSystemNatives(void);

#endif	/* __SYSTEMNATIVES_H */
