/*
	File:		power/host/HostBattery.h

	Contains:	The host's own battery, as the host's battery driver
				(power/host/HostBatteryDriver.h) reads it: how full it is,
				whether it is charging, whether the mains is on, and what
				the host can say of its voltage, current and temperature.
				Host only (not in the ROM).

				Plain C++ - none of the Newton's headers - so that a
				board's own code (src/host/esp32) can give its fuel gauge.

				A reading comes from the first of:
				  - one pinned by a test or a script (HostSetBattery);
				  - the board's own gauge, where a board registers one
				    (the T-Deck Pro's BQ27220, tdeck_battery.cpp);
				  - the host's operating system: Windows'
				    GetSystemPowerStatus, macOS's IOKit power sources,
				    Linux's /sys/class/power_supply (a desktop's, the
				    reMarkable's) - its supplies found by their type, not
				    by name.
				A host with none (a desktop computer) has no battery to
				report, and the driver reports the machine's own cells
				as it always has.
*/

#ifndef __HOSTBATTERY_H
#define __HOSTBATTERY_H

#include <limits.h>

const long	kHostBatteryUnknown = LONG_MIN;		// a value the host cannot say

enum
{
	kHostBatteryDischarging,		// on the battery (or on the mains, not charging)
	kHostBatteryCharging,
	kHostBatteryFull				// on the mains, charged
};

struct HostBatteryReading
{
	long	percent;			// 0 .. 100
	long	millivolts;			// kHostBatteryUnknown: not said
	long	milliamps;			// into the battery (+) or drawn from it (-); kHostBatteryUnknown
	long	tenthsCelsius;		// the battery's temperature; kHostBatteryUnknown
	int		mains;				// 1 on, 0 off, -1 not said
	int		charge;				// kHostBattery...
};

typedef bool (*HostBatterySource)(HostBatteryReading* reading);	// ==> whether there is a battery

// the reading now (==> whether the host has a battery at all)
bool	HostBatteryRead(HostBatteryReading* reading);

// a board's own gauge, asked before the operating system (nil: none)
void	HostBatterySetSource(HostBatterySource source);

// a reading pinned, for tests and scripts (HostSetBattery): every
// HostBatteryRead answers it until it is unpinned (nil); present false
// pins a host with no battery
void	HostBatteryPin(const HostBatteryReading* reading, bool present = true);

// Linux's power supply class under root ("/sys/class/power_supply"; a
// test gives a tree of its own): the first supply of type Battery that is
// present, and whether any Mains or USB supply is online
bool	HostBatteryReadSysfs(const char* root, HostBatteryReading* reading);

#if defined(__APPLE__)
// macOS's IOKit power sources: the first internal battery that is present
// (power/host/iokit/HostBatteryIOKit.cpp - a file of its own, built without
// the Newton include paths: IOKit's MacTypes.h has a Point, a Rect, a Byte
// and a Debugger of its own, which the DDK's collide with)
bool	HostBatteryReadIOKit(HostBatteryReading* reading);
#endif

#endif	/* __HOSTBATTERY_H */
