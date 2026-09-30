/*
	File:		power/host/HostBatteryDriver.cpp

	Contains:	The host's battery driver (power/host/HostBatteryDriver.h).
				Host only (not in the ROM).
*/

#include "power/host/HostBatteryDriver.h"
#include "power/BatteryDriver.h"
#include "OSErrors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PROTOCOL PMainBatteryDriver : public PBatteryDriver
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(PMainBatteryDriver);

	PMainBatteryDriver*	New();
	void			Delete();
	NewtonErr		Init(void);
	NewtonErr		WakeUp(void);
	NewtonErr		ShutDown(void);
	long			Count(void);
	NewtonErr		Status(ULong which, PowerPlantStatus* status);
	NewtonErr		RawStatus(ULong which, PowerPlantStatus* status);
	NewtonErr		StartSleepCharge(void);
	NewtonErr		SetType(ULong which, ULong type);
	long			ReadADCVoltage(ULong channel);
	Fixed			ConvertVoltage(ULong channel, ULong reading);

	long			fType;				// the kBattery... kind the machine was told
	PowerPlantStatus	fLast;			// the reading last taken
};

PROTOCOL_IMPL_SOURCE_MACRO(PMainBatteryDriver)
PROTOCOL_CLASSINFO(PMainBatteryDriver, "PBatteryDriver", "", 0, 0, nil)

static const Fixed kFixedOne = 0x00010000;


// The first of a sysfs directory's files as a line, or false.
static bool
ReadLine(const char* path, char* line, size_t size)
{
	FILE* f = fopen(path, "r");
	if (f == nil)
		return false;
	bool ok = fgets(line, (int) size, f) != nil;
	fclose(f);
	if (ok)
		line[strcspn(line, "\r\n")] = 0;
	return ok;
}


// The host's battery, where it can be read: its capacity (per cent),
// whether it is charging or full, and whether the mains is on.
static bool
ReadHostBattery(long* capacity, long* chargeState, long* acPower)
{
	char line[64];
	const char* batteries[] = { "/sys/class/power_supply/BAT0", "/sys/class/power_supply/BAT1" };
	for (size_t i = 0; i < sizeof(batteries) / sizeof(batteries[0]); i++)
	{
		char path[128];
		snprintf(path, sizeof(path), "%s/capacity", batteries[i]);
		if (!ReadLine(path, line, sizeof(line)))
			continue;
		*capacity = strtol(line, nil, 10);
		*chargeState = kChargeDischarging;
		snprintf(path, sizeof(path), "%s/status", batteries[i]);
		if (ReadLine(path, line, sizeof(line)))
		{
			if (strcmp(line, "Charging") == 0)
				*chargeState = kChargeFast;
			else if (strcmp(line, "Full") == 0)
				*chargeState = kChargeTrickle;
		}
		*acPower = 0;
		const char* supplies[] = { "/sys/class/power_supply/AC/online", "/sys/class/power_supply/AC0/online", "/sys/class/power_supply/ADP1/online" };
		for (size_t j = 0; j < sizeof(supplies) / sizeof(supplies[0]); j++)
			if (ReadLine(supplies[j], line, sizeof(line)))
			{
				*acPower = strtol(line, nil, 10) != 0;
				break;
			}
		return true;
	}
	return false;
}


PMainBatteryDriver*
PMainBatteryDriver::New()
{
	fType = kBatteryAlkaline;
	memset(&fLast, 0, sizeof(fLast));
	return this;
}

void		PMainBatteryDriver::Delete()					{ }
NewtonErr	PMainBatteryDriver::Init(void)					{ PowerPlantStatus status; return RawStatus(0, &status); }
NewtonErr	PMainBatteryDriver::WakeUp(void)				{ return noErr; }
NewtonErr	PMainBatteryDriver::ShutDown(void)				{ return noErr; }
long		PMainBatteryDriver::Count(void)					{ return 1; }			// one set of cells, as a MessagePad has
NewtonErr	PMainBatteryDriver::StartSleepCharge(void)		{ return kError_Call_Not_Implemented; }
long		PMainBatteryDriver::ReadADCVoltage(ULong)		{ return 0; }
Fixed		PMainBatteryDriver::ConvertVoltage(ULong, ULong reading)	{ return (Fixed) reading; }


NewtonErr
PMainBatteryDriver::Status(ULong which, PowerPlantStatus* status)
{
	if (which != 0 || status == nil)
		return kError_Bad_Parameters;
	return RawStatus(which, status);
}


// A reading taken now: the host's battery where it says, else full.
NewtonErr
PMainBatteryDriver::RawStatus(ULong which, PowerPlantStatus* status)
{
	if (which != 0 || status == nil)
		return kError_Bad_Parameters;
	long capacity = 100, chargeState = kChargeDischarging, acPower = 0;
	ReadHostBattery(&capacity, &chargeState, &acPower);
	status->fBatteryType = fType;
	status->fBatteryVoltage = (Fixed) (4 * kFixedOne + (2 * kFixedOne) * capacity / 100);		// four cells, 1 to 1.5 V each
	status->fBatteryCapacity = capacity;
	status->fBatteryLow = 0;
	status->fBatteryDead = 0;
	status->fBatteryCurrent = 0;
	status->fACPower = acPower;
	status->fACVoltage = acPower ? 7 * kFixedOne : 0;
	status->fChargeState = chargeState;
	status->fChargeRate = 0;
	status->fChargeCurrent = 0;
	status->fAmbientTemp = 20 * kFixedOne;
	status->fBatteryTemp = 20 * kFixedOne;
	fLast = *status;
	return noErr;
}


NewtonErr
PMainBatteryDriver::SetType(ULong which, ULong type)
{
	if (which != 0)
		return kError_Bad_Parameters;
	fType = (long) type;
	return noErr;
}


void
HostRegisterBatteryDriver(void)
{
	PMainBatteryDriver::ClassInfo()->Register();
}
