/*
	File:		hal/host/Power.cpp

	Contains:	The host's power plant (hal/Power.h).

				DEVIATION: a host has no batteries and no power manager to
				ask.  Rather than answer "cannot say" to everything - which
				would leave every slot of the script's battery frame nil and
				any arithmetic over it failing - it reports a plain machine
				running on fresh alkaline cells: a full charge, nothing being
				drawn, no mains, at room temperature.
*/

#include "hal/Power.h"
#include "OSErrors.h"

static const long kFixedOne = 0x00010000;

// What the machine has been told it holds (SetPowerPlantBatteryType); a
// MessagePad keeps this in the power manager, and the battery picker in
// the Prefs slip is what sets it.
static long gHostBatteryType = kBatteryAlkaline;

extern "C" NewtonErr
GetPowerPlantStatus(long /*which*/, PowerPlantStatus* status)
{
	if (status == nil)
		return kError_Bad_Parameters;
	status->fBatteryType = gHostBatteryType;
	status->fBatteryVoltage = 6 * kFixedOne;		// four cells, fresh
	status->fBatteryCapacity = 100;
	status->fBatteryLow = 0;
	status->fBatteryDead = 0;
	status->fBatteryCurrent = 0;
	status->fACPower = 0;
	status->fACVoltage = 0;
	status->fChargeState = kChargeDischarging;
	status->fChargeRate = 0;
	status->fChargeCurrent = 0;
	status->fAmbientTemp = 20 * kFixedOne;
	status->fBatteryTemp = 20 * kFixedOne;
	return noErr;
}


// One set of cells, as a MessagePad has.
extern "C" long
GetPowerPlantCount(void)
{
	return 1;
}


// The cells the machine is told it holds.  A real power manager takes
// this to pick the charge curve it measures the battery against; the
// host has nothing to measure, so it only remembers what it was told and
// answers that again.
extern "C" NewtonErr
SetPowerPlantBatteryType(long /*which*/, long type)
{
	gHostBatteryType = type;
	return noErr;
}

// ROM 0x00192764 CyclePower__Fv
// On a MessagePad this sends the power-off system event, shuts the
// battery, screen and tablet down, waits for any flash erase to finish,
// and then, with the scheduler held and the stack locked, turns the
// system off and on again in a loop until something real wakes it - the
// power switch, the card lock, the serial port's general-purpose input,
// the interconnect, or the real-time clock's alarm.  What woke it is the
// word it answers.
//
// DEVIATION: a host cannot power itself down, so the machine simply does
// not sleep and comes straight back with nothing to report.  The caller
// treats that as an ordinary wakeup, which is what the reconstruction
// needs: FPowerOff notes the time it woke, and that is what puts the
// automatic power-off off until the machine has been idle again.
extern "C" ULong
CyclePower(void)
{
	return 0;
}


// ROM 0x0026ca40 TranslatePowerEvent__16TVoyagerPlatformFUl
// The power event word as a reason.  The interconnect and the card lock
// answer whatever the platform driver was told to call them (the fields
// RegisterPowerSwitchInterrupt and its like fill in), and with nothing
// there they fall through to the tests below them; so does a word with
// neither bit in it.
//
// NOT YET RECONSTRUCTED: the platform driver (TVoyagerPlatform) and the
// two registered reasons, so the fields are always empty here and the
// three plain tests are what is left.
extern "C" long
TranslatePowerEvent(ULong event)
{
	if ((event & kPowerEventSerialGPI) != 0)
		return kWokeSerialGPI;
	if ((event & kPowerEventAlarm) != 0)
		return kWokeAlarm;
	return kWokeBecause;
}
