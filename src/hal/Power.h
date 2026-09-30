/*
	File:		hal/Power.h

	Contains:	The machine's power, as the hardware has it: the readings a
				battery driver reports (`PowerPlantStatus`, whose analogue
				readings are Fixed and whose unknown fields are -1), and the
				platform driver's side of going to sleep - the machine
				turned off until something wakes it, and the word that says
				what did.

				The power manager (power/PowerManager.h) is what the rest of
				the system asks - its 'pg&e world answers the batteries'
				readings through the battery driver (power/BatteryDriver.h)
				and CyclePower puts the machine to sleep over the calls
				below.  A port supplies these; on the MP2x00 they are the
				Voyager platform driver's (TVoyagerPlatform, reached through
				GetPlatformDriver).

	ROM:		TVoyagerPlatform::PowerOffSystem 0x0026c864, PowerOnSystem
				0x0026c898, TranslatePowerEvent 0x0026ca40; the interrupt
				controller's pending and enabled words (0x0F183000,
				0x0F184800) are what CyclePower reads the event from.
*/

#ifndef __HAL_POWER_H
#define __HAL_POWER_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

// the battery kinds (PowerPlantStatus::fBatteryType)
enum
{
	kBatteryUnknown			= 0,
	kBatteryAlkaline		= 1,
	kBatteryNiCd			= 2,
	kBatteryNiMH			= 3,
	kBatteryLithium			= 4
};

// what the charger is doing (PowerPlantStatus::fChargeState)
enum
{
	kChargeDischarging		= 0,
	kChargeTrickle			= 1,
	kChargeFast				= 2,
	kChargeFullyCharged		= 3,
	kChargePreliminary		= 4,
	kChargeTrickleContinuous = 5,
	kChargeDeepToast		= 6
};

// 0x34 bytes, as the power manager sends them; -1 in any field means
// the machine cannot say
struct PowerPlantStatus
{
	long	fBatteryType;		// +0x00
	Fixed	fBatteryVoltage;	// +0x04
	long	fBatteryCapacity;	// +0x08  per cent
	long	fBatteryLow;		// +0x0c
	long	fBatteryDead;		// +0x10
	Fixed	fBatteryCurrent;	// +0x14
	long	fACPower;			// +0x18  0 no, 1 yes
	Fixed	fACVoltage;			// +0x1c
	long	fChargeState;		// +0x20
	Fixed	fChargeRate;		// +0x24
	Fixed	fChargeCurrent;		// +0x28
	Fixed	fAmbientTemp;		// +0x2c
	Fixed	fBatteryTemp;		// +0x30
};

// The bits of a power event, as the machine reports what brought it back
// (TVoyagerPlatform::TranslatePowerEvent 0x0026ca40 reads them)
enum
{
	kPowerEventAlarm		= 0x00000004,	// the real-time clock's alarm
	kPowerEventCardLock		= 0x00008000,	// the card lock switch
	kPowerEventSerialGPI	= 0x00800000,	// the serial port's general-purpose input
	kPowerEventInterconnect	= 0x01000000	// the interconnect port
};

// what a wakeup is put down to (FPowerOff answers the matching symbol)
enum
{
	kWokeBecause		= 1,		// nothing in particular - the power switch among it
	kWokeSerialGPI		= 2,
	kWokeAlarm			= 3,
	kWokeUser			= 4,
	kWokeCardLock		= 5,
	kWokeInterconnect	= 7
};

extern "C" {
// The platform driver's power side.  PowerOffSystem is what the kernel
// runs for the generic system call 0x44: the machine off, returning when
// something wakes it; PowerOnSystem brings it up again (0: it is on);
// PowerEvent reads what woke it - the interrupts that are pending and
// enabled - as the power event word; TranslatePowerEvent makes that word
// one of the kWoke... reasons.
void		PlatformPowerOffSystem(void);
long		PlatformPowerOnSystem(void);
ULong		PlatformPowerEvent(void);
long		PlatformTranslatePowerEvent(ULong event);
}

#endif	/* __HAL_POWER_H */
