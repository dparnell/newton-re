/*
	File:		power/host/HostBattery.cpp

	Contains:	The host's own battery (power/host/HostBattery.h): a
				reading pinned, a board's gauge, else the operating
				system's.  Host only (not in the ROM).
*/

#include "power/host/HostBattery.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mutex>

#if defined(ESP_PLATFORM)
// (the board's gauge is a source the board registers: tdeck_battery.cpp)
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

static std::mutex			gBatteryLock;
static HostBatterySource	gBatterySource = nullptr;
static bool					gPinned = false;
static bool					gPinnedPresent = false;
static HostBatteryReading	gPinnedReading;


static void
Clear(HostBatteryReading* reading)
{
	reading->percent = 100;
	reading->millivolts = kHostBatteryUnknown;
	reading->milliamps = kHostBatteryUnknown;
	reading->tenthsCelsius = kHostBatteryUnknown;
	reading->mains = -1;
	reading->charge = kHostBatteryDischarging;
}


/*------------------------------------------------------------------------------
	Linux: /sys/class/power_supply
------------------------------------------------------------------------------*/

// A supply's attribute as a line (without its newline), or false.
static bool
ReadAttribute(const char* root, const char* supply, const char* attribute, char* line, size_t size)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/%s/%s", root, supply, attribute);
	FILE* f = fopen(path, "r");
	if (f == nullptr)
		return false;
	bool ok = fgets(line, (int) size, f) != nullptr;
	fclose(f);
	if (ok)
		line[strcspn(line, "\r\n")] = 0;
	return ok;
}


static bool
ReadNumber(const char* root, const char* supply, const char* attribute, long long* value)
{
	char line[64];
	if (!ReadAttribute(root, supply, attribute, line, sizeof(line)) || line[0] == 0)
		return false;
	char* end;
	*value = strtoll(line, &end, 10);
	return end != line;
}


static int
CompareNames(const void* a, const void* b)
{
	return strcmp((const char*) a, (const char*) b);
}


// The battery is the first of type Battery (by name: BAT0 before BAT1)
// that is present and is the system's own - a mouse's or a keyboard's
// battery is a supply too, of scope Device.  Its capacity is its own
// figure where it gives one, else what charge or energy it holds of
// its full.  The kernel means its current to be positive charging, but
// not every driver says so: the sign is taken from the status.  The mains
// is any other supply (Mains, USB, ...) that is online.
bool
HostBatteryReadSysfs(const char* root, HostBatteryReading* reading)
{
	const int kMaxSupplies = 32;
	char names[kMaxSupplies][64];
	int count = 0;
	DIR* dir = opendir(root);
	if (dir == nullptr)
		return false;
	struct dirent* entry;
	while ((entry = readdir(dir)) != nullptr && count < kMaxSupplies)
	{
		if (entry->d_name[0] == '.' || strlen(entry->d_name) >= sizeof(names[0]))
			continue;
		strcpy(names[count++], entry->d_name);
	}
	closedir(dir);
	qsort(names, count, sizeof(names[0]), CompareNames);

	Clear(reading);
	bool found = false;
	int mains = -1;
	char line[64];
	for (int i = 0; i < count; i++)
	{
		if (!ReadAttribute(root, names[i], "type", line, sizeof(line)))
			continue;
		long long value, now, full;
		if (strcmp(line, "Battery") != 0)
		{
			// (a supply with a charge of its own is something's battery,
			// not the mains: the reMarkable's Marker docked is a Wireless
			// supply online, with a capacity)
			if (ReadNumber(root, names[i], "online", &value) && !ReadNumber(root, names[i], "capacity", &now)
			&& !(ReadAttribute(root, names[i], "scope", line, sizeof(line)) && strcmp(line, "Device") == 0))
				mains = (mains == 1 || value != 0) ? 1 : 0;
			continue;
		}
		if (found)
			continue;
		if (ReadAttribute(root, names[i], "scope", line, sizeof(line)) && strcmp(line, "Device") == 0)
			continue;
		if (ReadNumber(root, names[i], "present", &value) && value == 0)
			continue;
		if (ReadNumber(root, names[i], "capacity", &value))
			reading->percent = (long) value;
		else if (ReadNumber(root, names[i], "charge_now", &now) && ReadNumber(root, names[i], "charge_full", &full) && full > 0)
			reading->percent = (long) (now * 100 / full);
		else if (ReadNumber(root, names[i], "energy_now", &now) && ReadNumber(root, names[i], "energy_full", &full) && full > 0)
			reading->percent = (long) (now * 100 / full);
		else
			continue;
		if (reading->percent < 0)
			reading->percent = 0;
		if (reading->percent > 100)
			reading->percent = 100;
		found = true;
		if (ReadAttribute(root, names[i], "status", line, sizeof(line)))
		{
			if (strcmp(line, "Charging") == 0)
				reading->charge = kHostBatteryCharging;
			else if (strcmp(line, "Full") == 0 || strcmp(line, "Not charging") == 0)
				reading->charge = kHostBatteryFull;				// (on the mains, held where it is)
		}
		if (ReadNumber(root, names[i], "voltage_now", &value))
			reading->millivolts = (long) (value / 1000);			// microvolts
		if (ReadNumber(root, names[i], "current_now", &value))
		{
			long milliamps = (long) (value / 1000);					// microamps
			if (milliamps < 0)
				milliamps = -milliamps;
			reading->milliamps = reading->charge == kHostBatteryCharging ? milliamps : -milliamps;
		}
		if (ReadNumber(root, names[i], "temp", &value))
			reading->tenthsCelsius = (long) value;
	}
	if (found)
		reading->mains = mains;
	return found;
}


/*------------------------------------------------------------------------------
	The operating system's battery
------------------------------------------------------------------------------*/

#if defined(_WIN32) && !defined(ESP_PLATFORM)

// GetSystemPowerStatus: the percentage, the mains and whether it charges
// (a battery not charging on the mains is held where it is: full)
static bool
ReadSystemBattery(HostBatteryReading* reading)
{
	SYSTEM_POWER_STATUS status;
	if (!GetSystemPowerStatus(&status))
		return false;
	if ((status.BatteryFlag & 128) != 0 || status.BatteryFlag == 255 || status.BatteryLifePercent > 100)
		return false;													// no system battery, or not known
	Clear(reading);
	reading->percent = status.BatteryLifePercent;
	reading->mains = status.ACLineStatus == 1 ? 1 : status.ACLineStatus == 0 ? 0 : -1;
	if ((status.BatteryFlag & 8) != 0)
		reading->charge = kHostBatteryCharging;
	else if (reading->mains == 1)
		reading->charge = kHostBatteryFull;
	return true;
}

#elif defined(__APPLE__)

// IOKit's power sources (power/host/iokit/HostBatteryIOKit.cpp: its headers
// and the DDK's do not mix)
static bool
ReadSystemBattery(HostBatteryReading* reading)
{
	return HostBatteryReadIOKit(reading);
}

#elif !defined(ESP_PLATFORM)

static bool
ReadSystemBattery(HostBatteryReading* reading)
{
	return HostBatteryReadSysfs("/sys/class/power_supply", reading);
}

#else

static bool
ReadSystemBattery(HostBatteryReading*)
{
	return false;
}

#endif


/*------------------------------------------------------------------------------
	The reading
------------------------------------------------------------------------------*/

bool
HostBatteryRead(HostBatteryReading* reading)
{
	HostBatterySource source;
	{
		std::lock_guard<std::mutex> lock(gBatteryLock);
		if (gPinned)
		{
			if (gPinnedPresent)
				*reading = gPinnedReading;
			else
				Clear(reading);
			return gPinnedPresent;
		}
		source = gBatterySource;
	}
	Clear(reading);
	bool present = source != nullptr ? source(reading) : ReadSystemBattery(reading);
	if (!present)
		Clear(reading);
	else if (reading->mains == -1)
		reading->mains = reading->charge == kHostBatteryDischarging ? 0 : 1;		// charging, it is on the mains
	return present;
}


void
HostBatterySetSource(HostBatterySource source)
{
	std::lock_guard<std::mutex> lock(gBatteryLock);
	gBatterySource = source;
}


void
HostBatteryPin(const HostBatteryReading* reading, bool present)
{
	std::lock_guard<std::mutex> lock(gBatteryLock);
	gPinned = reading != nullptr;
	gPinnedPresent = present;
	if (reading != nullptr)
		gPinnedReading = *reading;
}
