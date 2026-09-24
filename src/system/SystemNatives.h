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

	The batteries a script sees are FBatteryStatus 0x00203db8 over
	GetBatteryStatus 0x002037bc, whose reading comes from hal/Power.h
	(DEVIATION: the power manager, which the ROM asks with an RPC, is
	NOT YET RECONSTRUCTED).

	FPowerOff 0x00201b00 - the machine asleep and awake again - belongs
	here too, and is in newt/NewtWorld.cpp instead, because it notes the
	time the machine woke in the newt world's gLastWakeupTime, which is
	above this library.  SleepUntilNextWakeup, which it shares with the
	battery check, is here.

	The backlight is here too (FBackLightStatus 0x00201a0c, FBackLight
	0x00201a3c): it is switched through QuickDraw's screen driver
	(qd/Screen.h's SetGrafInfo), but the ROM keeps its natives with the
	machine's rather than the screen's, and FBackLight sends the root
	view a message, which the screen library is below.

	FSetRandomSeed 0x002017a4, which the ROM also keeps here, is in
	frames/Builtins.cpp with the generator it seeds.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#ifndef __SYSTEMNATIVES_H
#define __SYSTEMNATIVES_H

#include "objects.h"

Ref		FGetSerialNumber(RefArg rcvr);
Ref		FBatteryStatus(RefArg rcvr, RefArg which);	// ROM 0x00203db8 FBatteryStatus
Ref		FBackLight(RefArg rcvr, RefArg on);			// ROM 0x00201a3c FBackLight
Ref		FMinimumBatteryCheck(RefArg rcvr);			// ROM 0x002019a0 FMinimumBatteryCheck - the ROM holds no name for it, so only the ROM's own scripts reach it
long	SleepUntilNextWakeup(void);					// ROM 0x002018f8 SleepUntilNextWakeup__Fv - ==> a kWoke... reason (hal/Power.h)

void	RegisterSystemNatives(void);

#endif	/* __SYSTEMNATIVES_H */
