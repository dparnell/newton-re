/*
	File:		power/BatteryDriver.cpp

	Contains:	The battery driver's protocol glue and the calls made of the
				driver in use (power/BatteryDriver.h).

				Reconstructed from the MP2x00 US ROM (0x0003b3c8-0x0003b5c8,
				0x00385d98-0x00385e58); each function cites its origin.
*/

#include "BatteryDriver.h"
#include "OSErrors.h"
#include "NewtErrors.h"

PBatteryDriver*	gBatteryDriver = nil;		// ROM 0x0c1008d0 gBatteryDriver


/*------------------------------------------------------------------------------
	P B a t t e r y D r i v e r
------------------------------------------------------------------------------*/

// ROM 0x00385d98 New__14PBatteryDriverSFPc
PBatteryDriver*
PBatteryDriver::New(char* implementation)
{
	PBatteryDriver* p = (PBatteryDriver*) AllocInstanceByName("PBatteryDriver", implementation);
	return p != nil ? (PBatteryDriver*) p->GlueNew() : nil;
}


// ROM 0x00385dc4 Delete__14PBatteryDriverFv
void
PBatteryDriver::Delete()
{
	GlueDelete();
}


/*------------------------------------------------------------------------------
	T h e   d r i v e r   i n   u s e
------------------------------------------------------------------------------*/

// ROM 0x0003b470 BatteryInitialize__Fv
// The machine's own driver ("PMainBatteryDriver") if one has been
// registered, else the MP2x00's Cirrus one registered and made; either
// way initialised.
// NOT YET RECONSTRUCTED: PCirrusBatteryDriver (0x0005853c-0x000597a0, the
// Cirrus chip's ADC and charger - hardware), so a machine with no driver
// of its own has none.
void
BatteryInitialize(void)
{
	gBatteryDriver = (PBatteryDriver*) NewByName("PBatteryDriver", "PMainBatteryDriver");
	if (gBatteryDriver == nil)
	{
		// NOT YET RECONSTRUCTED: PCirrusBatteryDriver::ClassInfo()->Register();
		gBatteryDriver = PBatteryDriver::New((char*) "PCirrusBatteryDriver");
	}
	if (gBatteryDriver != nil)
		gBatteryDriver->Init();
}


// ROM 0x0003b430 BatteryWakeUp__Fv
void
BatteryWakeUp(void)
{
	if (gBatteryDriver != nil)
		gBatteryDriver->WakeUp();
}


// ROM 0x0003b448 BatteryShutDown__Fv
void
BatteryShutDown(void)
{
	if (gBatteryDriver != nil)
		gBatteryDriver->ShutDown();
}


// ROM 0x0003b460 GetBatteryDriver__Fv
PBatteryDriver*
GetBatteryDriver(void)
{
	return gBatteryDriver;
}


// ROM 0x0003b3f4 SleepChargeSupported__Fv
// Whether the driver will charge the battery while the machine sleeps
// (asking it starts the charge).
Boolean
SleepChargeSupported(void)
{
	return gBatteryDriver != nil && gBatteryDriver->StartSleepCharge() == noErr;
}


// ROM 0x0003b508 GetPowerPlantStatus__FUlP16PowerPlantStatus
// A battery's status, as the driver last read it.  ROM QUIRK: with no
// driver the ROM answers what was in r0 - `which` - so battery 0 answers
// noErr with the status untouched; the host keeps that.
NewtonErr
GetPowerPlantStatus(ULong which, PowerPlantStatus* status)
{
	if (gBatteryDriver == nil)
		return (NewtonErr) which;
	return gBatteryDriver->Status(which, status);
}


// ROM 0x0003b554 GetRawPowerPlantStatus__FUlP16PowerPlantStatus
// The same, read afresh.  (The same quirk with no driver.)
NewtonErr
GetRawPowerPlantStatus(ULong which, PowerPlantStatus* status)
{
	if (gBatteryDriver == nil)
		return (NewtonErr) which;
	return gBatteryDriver->RawStatus(which, status);
}


// ROM 0x0003b534 GetPowerPlantCount__Fv
// How many batteries.  DEVIATION: with no driver the ROM answers whatever
// its caller left in r0; the host answers none.
long
GetPowerPlantCount(void)
{
	if (gBatteryDriver == nil)
		return 0;
	return gBatteryDriver->Count();
}


// ROM 0x0003b5ac SetBatteryType__FUlT1
// ROM QUIRK: with no driver, `which` again.
NewtonErr
SetBatteryType(ULong which, ULong type)
{
	if (gBatteryDriver == nil)
		return (NewtonErr) which;
	return gBatteryDriver->SetType(which, type);
}


// ROM 0x0003b580 ConvertRawReading__FUlT1
// ROM QUIRK: with no driver, `channel`.
Fixed
ConvertRawReading(ULong channel, ULong reading)
{
	if (gBatteryDriver == nil)
		return (Fixed) channel;
	return gBatteryDriver->ConvertVoltage(channel, reading);
}
