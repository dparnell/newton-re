/*
	File:		hal/Power.h

	Contains:	The power plant: what the machine can say about its
				batteries and the power coming in.  On a Newton this is the
				power manager's business and the rest of the system asks it
				with an RPC ('newt/'pg&e); the structure it sends back is
				`PowerPlantStatus`, whose analogue readings are Fixed and
				whose unknown fields are -1.  A port supplies the readings.

	ROM:		GetBatteryStatus 0x002037bc (the RPC), FBatteryStatus
				0x00203db8 (the frame a script sees)
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
	kWokeBecause		= 1,		// nothing in particular
	kWokeSerialGPI		= 2,
	kWokeAlarm			= 3,
	kWokeUser			= 4,		// the power switch
	kWokeCardLock		= 5,
	kWokeInterconnect	= 7
};

extern "C" {
// which battery (0 is the main one) -> its status; an error leaves it alone
NewtonErr	GetPowerPlantStatus(long which, PowerPlantStatus* status);
// how many batteries the machine has (0 when it cannot say)
long		GetPowerPlantCount(void);
// the machine powered down until something brings it back; ==> the
// power event word saying what did (0 for nothing in particular)
ULong		CyclePower(void);
// that word as one of the kWoke... reasons
long		TranslatePowerEvent(ULong event);
}

#endif	/* __HAL_POWER_H */
