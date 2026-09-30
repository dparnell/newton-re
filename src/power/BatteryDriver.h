/*
	File:		power/BatteryDriver.h

	Contains:	The battery driver: the protocol the power manager reads the
				batteries through (PBatteryDriver), the one it is using
				(gBatteryDriver), and the calls the rest of the system makes
				of it - what each battery says (GetPowerPlantStatus, or a
				fresh reading, GetRawPowerPlantStatus), how many there are,
				what cells they hold, and the driver told when the machine
				goes to sleep and wakes.

				The power manager looks for an implementation called
				"PMainBatteryDriver" first - which is how a machine's own
				driver, in its ROM extension or a package, takes the place
				of the ROM's - and only then makes the MP2x00's own,
				PCirrusBatteryDriver (the Cirrus sound-and-power chip's ADC
				and its charger: NOT YET RECONSTRUCTED, being hardware).
				The host's driver is power/host/HostBatteryDriver.cpp, a
				PMainBatteryDriver.

				Not in the DDK.  Reconstructed from the MP2x00 US ROM
				(0x0003b3c8-0x0003b5c8; the protocol's glue 0x00385d98-
				0x00385e58).
*/

#ifndef __BATTERYDRIVER_H
#define __BATTERYDRIVER_H

#ifndef __PROTOCOLS_H
#include "Protocols.h"
#endif
#ifndef __HAL_POWER_H
#include "hal/Power.h"
#endif

PROTOCOL PBatteryDriver : public TProtocol
{
public:
	static PBatteryDriver*	New(char* implementation);						// ROM 0x00385d98 New__14PBatteryDriverSFPc
	void			Delete();												// ROM 0x00385dc4 Delete__14PBatteryDriverFv

	VIRTUAL NewtonErr	Init(void) ENDVIRTUAL;								// ROM 0x00385de0 Init__14PBatteryDriverFv
	VIRTUAL NewtonErr	WakeUp(void) ENDVIRTUAL;							// ROM 0x00385dec WakeUp__14PBatteryDriverFv
	VIRTUAL NewtonErr	ShutDown(void) ENDVIRTUAL;							// ROM 0x00385df8 ShutDown__14PBatteryDriverFv
	VIRTUAL long		Count(void) ENDVIRTUAL;								// ROM 0x00385e04 Count__14PBatteryDriverFv - how many batteries
	VIRTUAL NewtonErr	Status(ULong which, PowerPlantStatus* status) ENDVIRTUAL;		// ROM 0x00385e10 Status__14PBatteryDriverFUlP16PowerPlantStatus - the reading last taken
	VIRTUAL NewtonErr	RawStatus(ULong which, PowerPlantStatus* status) ENDVIRTUAL;	// ROM 0x00385e1c RawStatus__14PBatteryDriverFUlP16PowerPlantStatus - one taken now
	VIRTUAL NewtonErr	StartSleepCharge(void) ENDVIRTUAL;					// ROM 0x00385e28 StartSleepCharge__14PBatteryDriverFv - noErr: it charges asleep
	VIRTUAL NewtonErr	SetType(ULong which, ULong type) ENDVIRTUAL;		// ROM 0x00385e34 SetType__14PBatteryDriverFUlT1 - a kBattery... kind
	VIRTUAL long		ReadADCVoltage(ULong channel) ENDVIRTUAL;			// ROM 0x00385e40 ReadADCVoltage__14PBatteryDriverFUl
	VIRTUAL Fixed		ConvertVoltage(ULong channel, ULong reading) ENDVIRTUAL;	// ROM 0x00385e4c ConvertVoltage__14PBatteryDriverFUlT1
};

extern PBatteryDriver*	gBatteryDriver;

void		BatteryInitialize(void);										// ROM 0x0003b470 BatteryInitialize__Fv
void		BatteryWakeUp(void);											// ROM 0x0003b430 BatteryWakeUp__Fv
void		BatteryShutDown(void);											// ROM 0x0003b448 BatteryShutDown__Fv
PBatteryDriver*	GetBatteryDriver(void);										// ROM 0x0003b460 GetBatteryDriver__Fv
Boolean		SleepChargeSupported(void);										// ROM 0x0003b3f4 SleepChargeSupported__Fv
NewtonErr	GetPowerPlantStatus(ULong which, PowerPlantStatus* status);		// ROM 0x0003b508 GetPowerPlantStatus__FUlP16PowerPlantStatus
NewtonErr	GetRawPowerPlantStatus(ULong which, PowerPlantStatus* status);	// ROM 0x0003b554 GetRawPowerPlantStatus__FUlP16PowerPlantStatus
long		GetPowerPlantCount(void);										// ROM 0x0003b534 GetPowerPlantCount__Fv
NewtonErr	SetBatteryType(ULong which, ULong type);						// ROM 0x0003b5ac SetBatteryType__FUlT1
Fixed		ConvertRawReading(ULong channel, ULong reading);				// ROM 0x0003b580 ConvertRawReading__FUlT1

#endif	/* __BATTERYDRIVER_H */
