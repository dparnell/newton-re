/*
	File:		power/host/HostBatteryDriver.h

	Contains:	The host's battery driver: a PBatteryDriver
				(power/BatteryDriver.h) registered as "PMainBatteryDriver",
				the name the power manager looks for before it makes the
				MP2x00's own Cirrus driver - so it stands where a machine's
				own driver would.  Host only (not in the ROM).

				It reports the host's own battery where the host says what
				it is in a way plain C can read - Linux's
				/sys/class/power_supply (the first battery's capacity and
				status, and whether the mains is on) - and otherwise a
				machine on fresh cells: full, nothing drawn, no mains, at
				room temperature.  The kind of cells is what the machine is
				told (the Prefs' battery picker, SetBatteryType).
*/

#ifndef __HOSTBATTERYDRIVER_H
#define __HOSTBATTERYDRIVER_H

void	HostRegisterBatteryDriver(void);			// before the power manager starts

#endif	/* __HOSTBATTERYDRIVER_H */
