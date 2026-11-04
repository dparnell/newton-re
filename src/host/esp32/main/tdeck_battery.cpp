/*
	File:		host/esp32/main/tdeck_battery.cpp

	Contains:	The T-Deck Pro's battery as the Newton's
				(power/host/HostBattery.h): the board's fuel gauge and
				charger, on the I2C bus the touch panel and keyboard use
				(tdeck_input.cpp installs its driver; ESP-IDF's I2C driver
				takes one transaction at a time, so a Newton task may read
				here while the input task polls).

				The gauge is TI's BQ27220 at 0x55 (docs/host-tdeck-pro.md:
				the device spike found it): its standard commands are
				little-endian words - StateOfCharge (0x2C, per cent),
				Voltage (0x08, mV), Current (0x0C, mA, + charging),
				Temperature (0x06, 0.1 K) and BatteryStatus (0x0A: BATTPRES
				bit 3, there is a battery; FC bit 9, it is fully charged;
				DSG bit 0, it is discharging).  The charger is TI's BQ25896
				at 0x6B: its status register REG0B says whether the input
				is good (PG_STAT, bit 2: the USB is the mains) and what it
				is doing (CHRG_STAT, bits 4-3: 0 not charging, 1 pre-
				charge, 2 fast charging, 3 done).  Both are only read.

				With no battery fitted (the board on USB alone) the gauge
				says so and the Newton is told the host has none.

				For measuring what the board draws (DevicePower,
				tools/tdeck/powermeter.py): a sampler task reads Current
				once a second and keeps its mean, and the charger's input
				can be cut (REG00 EN_HIZ, bit 7) so that the board runs from
				its battery while the USB stays plugged in for the console -
				the gauge then sees all of the board's current.  The
				charger's I2C watchdog (REG07 bits 5-4, 40 s) is stopped
				meanwhile, in case it would put REG00 back; at start EN_HIZ
				is cleared, in case the board was reset while on battery.
				ESP-IDF's headers only, and HostBattery.h (plain C++).
*/

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"

#include "esp32host.h"
#include "tdeck_board.h"
#include "HostBattery.h"

#define I2C_PORT		I2C_NUM_0
#define ADDR_GAUGE		0x55

enum
{
	kGaugeTemperature = 0x06,
	kGaugeVoltage = 0x08,
	kGaugeStatus = 0x0A,
	kGaugeCurrent = 0x0C,
	kGaugeStateOfCharge = 0x2C
};

enum
{
	kStatusDischarging = 0x0001,		// DSG
	kStatusPresent = 0x0008,			// BATTPRES
	kStatusFull = 0x0200				// FC
};

const uint8_t	kChargerStatus = 0x0B;	// REG0B
const uint8_t	kChargerInput = 0x00;	// REG00: EN_HIZ, bit 7
const uint8_t	kChargerTimer = 0x07;	// REG07: WATCHDOG, bits 5-4
const uint8_t	kChargerBatteryFet = 0x09;	// REG09: BATFET_DIS, bit 5


// (asleep, the gauge stretches the clock while it wakes - longer than the
// bus's own timeout once allowed for, tdeck_input.cpp: so a word is asked
// for again)
static bool
ReadWord(uint8_t command, uint16_t* value)
{
	uint8_t b[2];
	if (i2c_master_write_read_device(I2C_PORT, ADDR_GAUGE, &command, 1, b, 2, pdMS_TO_TICKS(20)) != ESP_OK
	&& i2c_master_write_read_device(I2C_PORT, ADDR_GAUGE, &command, 1, b, 2, pdMS_TO_TICKS(20)) != ESP_OK)
		return false;
	*value = (uint16_t) (b[0] | (b[1] << 8));
	return true;
}


// The charger: a BQ25896 at 0x6B on the Pro and the first Max boards, an
// SY6970 at 0x6A on later ones - the same registers, for all this file asks
// of them.  Found by HostDeviceBatteryStart.
static uint8_t	gChargerAddress = 0x6B;


static bool
ReadCharger(uint8_t* status)
{
	return i2c_master_write_read_device(I2C_PORT, gChargerAddress, &kChargerStatus, 1, status, 1, pdMS_TO_TICKS(20)) == ESP_OK;
}


static bool
ReadChargerRegister(uint8_t reg, uint8_t* value)
{
	return i2c_master_write_read_device(I2C_PORT, gChargerAddress, &reg, 1, value, 1, pdMS_TO_TICKS(20)) == ESP_OK;
}


static bool
WriteChargerRegister(uint8_t reg, uint8_t value)
{
	uint8_t b[2] = { reg, value };
	return i2c_master_write_to_device(I2C_PORT, gChargerAddress, b, 2, pdMS_TO_TICKS(20)) == ESP_OK;
}


// The battery now (HostBatterySource): none when the gauge does not
// answer or says none is fitted.
static bool
ReadBattery(HostBatteryReading* reading)
{
	uint16_t status, soc, value;
	if (!ReadWord(kGaugeStatus, &status) || (status & kStatusPresent) == 0 || !ReadWord(kGaugeStateOfCharge, &soc))
		return false;
	reading->percent = soc > 100 ? 100 : soc;
	if (ReadWord(kGaugeVoltage, &value))
		reading->millivolts = value;
	if (ReadWord(kGaugeCurrent, &value))
		reading->milliamps = (int16_t) value;
	if (ReadWord(kGaugeTemperature, &value))
		reading->tenthsCelsius = (long) value - 2731;
	uint8_t charger;
	if (ReadCharger(&charger))
	{
		reading->mains = (charger & 0x04) != 0;
		switch ((charger >> 3) & 3)
		{
		case 1:
		case 2:	reading->charge = kHostBatteryCharging;	break;
		case 3:	reading->charge = kHostBatteryFull;		break;
		default:
			reading->charge = reading->mains == 1 && (status & kStatusFull) != 0 ? kHostBatteryFull : kHostBatteryDischarging;
			break;
		}
	}
	else
		reading->charge = (status & kStatusDischarging) != 0 ? kHostBatteryDischarging
						: (status & kStatusFull) != 0 ? kHostBatteryFull : kHostBatteryCharging;
	return true;
}


/*------------------------------------------------------------------------------
	Measuring the board's current
------------------------------------------------------------------------------*/

static volatile long	gSampleSum;			// mA, since the mean was last taken
static volatile long	gSampleCount;
static volatile long	gSampleLast;
static uint8_t			gSavedTimer;		// REG07 before the input was cut
static bool				gOnBattery;


static void
SamplerTask(void*)
{
	for (;;)
	{
		uint16_t value;
		if (ReadWord(kGaugeCurrent, &value))
		{
			gSampleLast = (int16_t) value;
			gSampleSum += (int16_t) value;
			gSampleCount++;
		}
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}


bool
HostDeviceBatteryCurrent(long* now, long* mean, long* samples, long* millivolts)
{
	static bool started = false;
	if (!started)
	{
		started = xTaskCreatePinnedToCore(SamplerTask, "powersample", 3072, NULL, 3, NULL, 0) == pdPASS;
		if (!started)
			return false;
	}
	long count = gSampleCount, sum = gSampleSum;
	gSampleCount = 0;
	gSampleSum = 0;
	*now = gSampleLast;
	*samples = count;
	*mean = count != 0 ? sum / count : gSampleLast;
	uint16_t value;
	*millivolts = ReadWord(kGaugeVoltage, &value) ? value : -1;
	return true;
}


bool
HostDeviceBatteryRunOnBattery(bool on)
{
	uint8_t input, timer;
	if (!ReadChargerRegister(kChargerInput, &input) || !ReadChargerRegister(kChargerTimer, &timer))
		return false;
	if (on && !gOnBattery)
	{
		gSavedTimer = timer;
		if (!WriteChargerRegister(kChargerTimer, timer & ~0x30))
			return false;
	}
	if (!WriteChargerRegister(kChargerInput, on ? (input | 0x80) : (input & ~0x80)))
		return false;
	if (!on && gOnBattery)
		WriteChargerRegister(kChargerTimer, gSavedTimer);
	gOnBattery = on;
	printf("newton: the charger's input %s\n", on ? "cut - the board on its battery" : "back - the board on USB");
	return true;
}


// The Max switched off (the quick sheet's Power off): the charger's BATFET
// opened, which takes the battery off everything until the charger's QON pin
// is pressed or USB comes (LilyGO's bq25896_shutdown example).  On USB the
// board stays up, so that is refused.  The touch chip held in reset first, as
// LilyGO's firmware does: cut off running, it can come back half awake.
// The panel keeps what it shows: it is finished first (the Newton has drawn
// the off screen by now), which takes the 6 s of two full refreshes.
// Not really: ==> whether it could be done.  Really: ==> false if it could
// not; if it could, this does not return.
bool
HostDeviceBatteryPowerOff(bool really)
{
	uint8_t charger, fet;
	if (!ReadCharger(&charger) || ((charger & 0x04) != 0 && !gOnBattery) || !ReadChargerRegister(kChargerBatteryFet, &fet))
		return false;
	if (!really)
		return true;
	printf("newton: switching off\n");
	if (!HostDevicePanelFinish())
		printf("newton: the panel did not finish\n");
	HostDeviceBoardSet(kLineTouchReset, false);
	vTaskDelay(pdMS_TO_TICKS(20));
	WriteChargerRegister(kChargerBatteryFet, fet | 0x20);
	vTaskDelay(pdMS_TO_TICKS(2000));
	// (still here: it did not take)
	WriteChargerRegister(kChargerBatteryFet, fet);
	HostDeviceBoardSet(kLineTouchReset, true);
	HostDevicePanelResume();
	printf("newton: still on after the charger was told to switch off\n");
	return false;
}


// The chip kept out of light sleep while the USB gives power (the charger's
// PG_STAT), unless the input was cut to measure: the USB serial/JTAG does
// not survive light sleep, and ESP-IDF's connection monitor, which should
// have held it off, was seen to let it happen on the cable - the USB then
// never answered again, the board otherwise running (2026-11-01).  On USB
// power the battery is no matter.  Asked once a second; a charger that
// does not answer keeps the chip awake.
static void
UsbPowerTask(void*)
{
	for (;;)
	{
		uint8_t charger;
		bool powered = !ReadCharger(&charger) || ((charger & 0x04) != 0 && !gOnBattery);
		HostDevicePowerKeepAwake(kAwakeUsbPower, powered);
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}


bool
HostDeviceBatteryStart(void)
{
	uint8_t probe;
	if (!ReadCharger(&probe))
		gChargerAddress = 0x6A;
	xTaskCreatePinnedToCore(UsbPowerTask, "usbpower", 2560, NULL, 3, NULL, 0);
	HostBatteryReading reading = { 100, kHostBatteryUnknown, kHostBatteryUnknown, kHostBatteryUnknown, -1, kHostBatteryDischarging };
	uint16_t status;
	if (!ReadWord(kGaugeStatus, &status))
	{
		printf("newton: no fuel gauge - no battery reading\n");
		return false;
	}
	uint8_t input;
	if (ReadChargerRegister(kChargerInput, &input) && (input & 0x80) != 0)
	{
		WriteChargerRegister(kChargerInput, input & ~0x80);
		printf("newton: the charger's input was found cut (a reset while measuring?) - put back\n");
	}
	HostBatterySetSource(ReadBattery);
	if (ReadBattery(&reading))
		printf("newton: the battery at %ld%%, %ld mV, %ld mA, %s%s\n", reading.percent, reading.millivolts, reading.milliamps,
			   reading.charge == kHostBatteryCharging ? "charging" : reading.charge == kHostBatteryFull ? "full" : "discharging",
			   reading.mains == 1 ? ", on USB" : "");
	else
		printf("newton: no battery fitted\n");
	return true;
}
