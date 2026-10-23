/*
	File:		power/host/iokit/HostBatteryIOKit.cpp

	Contains:	macOS's battery for power/host/HostBattery.cpp: IOKit's
				power sources.  A file - and a library - of its own,
				compiled without the Newton include paths or host_compat.h:
				CoreFoundation brings MacTypes.h, whose Point, Rect, Byte,
				Debugger and DebugStr are the DDK's names too.  Host only
				(not in the ROM).
*/

#include "power/host/HostBattery.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>


static bool
DictionaryNumber(CFDictionaryRef dictionary, CFStringRef key, long* value)
{
	CFNumberRef number = (CFNumberRef) CFDictionaryGetValue(dictionary, key);
	if (number == nullptr || CFGetTypeID(number) != CFNumberGetTypeID())
		return false;
	return CFNumberGetValue(number, kCFNumberLongType, value);
}


static int
DictionaryBoolean(CFDictionaryRef dictionary, CFStringRef key)
{
	CFBooleanRef value = (CFBooleanRef) CFDictionaryGetValue(dictionary, key);
	if (value == nullptr || CFGetTypeID(value) != CFBooleanGetTypeID())
		return -1;
	return CFBooleanGetValue(value) ? 1 : 0;
}


// IOKit's power sources: the first internal battery that is present
// (the reading comes cleared: HostBatteryRead)
bool
HostBatteryReadIOKit(HostBatteryReading* reading)
{
	CFTypeRef info = IOPSCopyPowerSourcesInfo();
	if (info == nullptr)
		return false;
	CFArrayRef sources = IOPSCopyPowerSourcesList(info);
	bool found = false;
	if (sources != nullptr)
	{
		for (CFIndex i = 0; i < CFArrayGetCount(sources) && !found; i++)
		{
			CFDictionaryRef source = IOPSGetPowerSourceDescription(info, CFArrayGetValueAtIndex(sources, i));
			if (source == nullptr)
				continue;
			CFStringRef type = (CFStringRef) CFDictionaryGetValue(source, CFSTR(kIOPSTypeKey));
			if (type == nullptr || CFStringCompare(type, CFSTR(kIOPSInternalBatteryType), 0) != kCFCompareEqualTo)
				continue;
			if (DictionaryBoolean(source, CFSTR(kIOPSIsPresentKey)) == 0)
				continue;
			long current, maximum = 100;
			if (!DictionaryNumber(source, CFSTR(kIOPSCurrentCapacityKey), &current))
				continue;
			DictionaryNumber(source, CFSTR(kIOPSMaxCapacityKey), &maximum);
			reading->percent = maximum > 0 ? current * 100 / maximum : current;
			CFStringRef state = (CFStringRef) CFDictionaryGetValue(source, CFSTR(kIOPSPowerSourceStateKey));
			if (state != nullptr)
				reading->mains = CFStringCompare(state, CFSTR(kIOPSACPowerValue), 0) == kCFCompareEqualTo ? 1 : 0;
			if (DictionaryBoolean(source, CFSTR(kIOPSIsChargingKey)) == 1)
				reading->charge = kHostBatteryCharging;
			else if (reading->mains == 1)
				reading->charge = kHostBatteryFull;
			long value;
			if (DictionaryNumber(source, CFSTR(kIOPSVoltageKey), &value))
				reading->millivolts = value;
			if (DictionaryNumber(source, CFSTR(kIOPSCurrentKey), &value))
				reading->milliamps = value;									// (negative discharging)
			found = true;
		}
		CFRelease(sources);
	}
	CFRelease(info);
	return found;
}
