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

extern "C" NewtonErr
GetPowerPlantStatus(long /*which*/, PowerPlantStatus* status)
{
	if (status == nil)
		return kError_Bad_Parameters;
	status->fBatteryType = kBatteryAlkaline;
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
