/*
	File:		host/esp32/main/esp32host.h

	Contains:	What the ESP32-S3 host's two halves tell each other - the
				board's (esp32main.cpp, ESP-IDF's headers) and the Newton's
				(newtonmain.cpp, the DDK's) - in plain C types, since the
				two sets of headers do not mix.
*/

#ifndef __ESP32HOST_H
#define __ESP32HOST_H

#include <stddef.h>

// the Newton's side: the baked ROM objects at image (nil: none), then the
// OS booted, with a NewtonScript prompt on the console
void		NewtonMainOnDevice(const void* image, size_t size);

// the board's side
long long	HostDeviceMicroseconds(void);
void		HostDeviceMemory(unsigned long* internalFree, unsigned long* psramFree);
int			HostDeviceReadLine(char* line, size_t size);		// a line the console has typed, if one has come (never waits) ==> whether
void		HostDeviceKick(void);			// the Newton's side told something has come for its console task (a line, the sheet pulled, the clock set): from any task

// the panel (tdeck_panel.cpp): its task started (before the OS boots), then
// given the display's grays to show (one byte a pixel, 0 white .. 255
// black, width per row - host/HostWindow.h's form) once there is a display
void		HostDevicePanelStart(void);
void		HostDevicePanelShow(const unsigned char* grays, long width, long height);
void		HostDevicePanelChanged(void);		// the grays changed (hal/host/HostScreen.h's gHostScreenChanged)
bool		HostDevicePanelSet(const char* name, long value);
// (and the Host preferences panel's settings of it, host/HostWindow.h's
// HostWindowOption and HostWindowSetOption, are answered there)
bool		HostWindowOption(const char* name, long* value);
bool		HostWindowSetOption(const char* name, long value);
// (the display followed as it changes shape - the Newton turned - by asking
// for it each time: the grays, their width and height now)
void		HostDevicePanelFollow(void (*now)(const unsigned char** grays, long* width, long* height));
bool		HostDeviceDisplayTurned(void);
void		HostDevicePanelDump(void);			// what the panel shows, on the console (tools/tdeck/panelshot.py)		// the display wider than tall: drawn turned on the panel, the touch panel turned back	// a refresh setting changed (tdeck_panel.cpp lists them) ==> whether there is one so named
// the touch panel and the keyboard (tdeck_input.cpp): read from the start,
// handed to the window shims below
void		HostDeviceInputStart(void);
void		HostDeviceInputDump(void);			// the console's !pm: interrupts or polls, and how many
bool		HostDeviceSheetPulled(void);		// the quick sheet pulled down from the top edge since last asked (once for each pull) ==> whether
void		HostDeviceSetLocked(bool locked);	// the lock screen up or down: the keyboard left out and its light off meanwhile
void		HostDeviceSetKeyLight(bool on);		// the keyboard's light allowed (on, as at start) or kept off
// the internal flash on the board's `store` partition (tdeck_store.cpp):
// started before the OS boots (==> whether there is one), then opened by
// the Newton's side - its image, read in place, its size, whether it was
// used before - and written and erased through these (from a Newton task)
bool		HostDeviceStoreStart(void);
bool		HostDeviceStoreOpen(unsigned char** bytes, unsigned long* size, bool* existed);
void		HostDeviceStoreWrite(unsigned long offset, const unsigned char* bytes, unsigned long size);
void		HostDeviceStoreErase(unsigned long offset, unsigned long size, unsigned long lanes);
void		HostDeviceStoreCounts(unsigned long* writes, unsigned long* erases);
// the WiFi (tdeck_wifi.cpp): started with the board; the networks it
// sees, the ones it remembers (and joins whichever is in range), how it is
// connected - host/HostWifi.h's provider, from a Newton task
enum { kDeviceWifiOff, kDeviceWifiSearching, kDeviceWifiConnecting, kDeviceWifiConnected };	// (HostWifiState's order)
struct DeviceWifiSeen
{
	char	ssid[33];
	long	rssi;
	bool	secure;
	bool	known;
};
bool		HostDeviceWifiStart(void);
long		HostDeviceWifiScan(DeviceWifiSeen* seen, long max);			// ==> how many, strongest first; -1: could not scan
long		HostDeviceWifiKnown(char (*ssids)[33], long max);
bool		HostDeviceWifiRemember(const char* ssid, const char* password);
bool		HostDeviceWifiForget(const char* ssid);
bool		HostDeviceWifiSetRadio(bool on);			// the radio switched off (nothing joined) or on again (for measuring: DevicePowerSet)
bool		HostDeviceWifiSetPowerSave(int mode);		// ESP-IDF's wifi_ps_type_t: 0 none, 1 min modem (its default), 2 max modem
void		HostDeviceWifiStatus(int* state, char* ssid, unsigned long* address, long* rssi);
unsigned long	HostDeviceClockSyncs(void);		// how many times the board's clock has been set (from the network, or by hand)
void		HostDeviceClockSetTo(long long seconds);	// the board's clock set by hand, in seconds from 1970 (GMT)

// the SPI bus the panel and the LoRa radio share (tdeck_panel.cpp):
// started once, before either, its chip selects parked (==> whether it is up)
bool		HostDeviceSpiBusStart(void);

// the LoRa radio (tdeck_lora.cpp): the SX1262 given to hal/host/HostMesh.h's
// MeshCore node, run on a task of its own (==> whether it was started); the
// mesh's natives then answer for it
bool		HostDeviceLoraStart(void);

// the GPS receiver (tdeck_gps.cpp): powered, its UART handed to
// hal/host/HostGPS.h as the source "device" (==> whether it was started);
// HostGPSStatus then has its fix, and serial port 2 its sentences
bool		HostDeviceGpsStart(void);
void		HostDeviceGpsDump(void);			// the console's !pm: its bursts and sentences

// the cellular module (tdeck_modem.cpp): powered and started with the
// board, its UART handed to hal/host/HostCellular.h's engine on a task of its
// own (==> whether it was started); the engine's natives then answer for it
bool		HostDeviceCellStart(void);

// the battery (tdeck_battery.cpp): the fuel gauge and the charger on the
// input's I2C bus, given to power/host/HostBattery.h as the board's own
// source (started after the input; ==> whether there is a gauge)
bool		HostDeviceBatteryStart(void);
bool		HostDeviceBatteryCurrent(long* now, long* mean, long* samples, long* millivolts);	// mA (+ charging) now, and the mean of the 1 s samples since last asked (the sampler starts at the first call)
bool		HostDeviceBatteryRunOnBattery(bool on);		// the charger's input cut (on) or back: the board on its battery while on USB

// power management (tdeck_power.cpp): frequency scaling and automatic light
// sleep, started first (==> whether ESP-IDF took the configuration); what
// keeps the chip out of light sleep, by bits; light sleep allowed or not;
// the Newton's idle time since last asked; the USB let go for a time
enum
{
	kAwakeGps = 1,							// its UART read by a Newton application (else its bursts wake the chip)
	kAwakeCell = 2,							// the 4G module's
	kAwakeUsb = 4,							// the USB taken back, being enumerated
	kAwakeUsbPower = 8						// on USB power (tdeck_battery.cpp: the USB does not survive light sleep)
};
bool		HostDevicePowerStart(void);
void		HostDevicePowerKeepAwake(unsigned who, bool on);
bool		HostDevicePowerSetLightSleep(bool on);
void		HostDevicePowerIdle(long* perMille, long* idles);
bool		HostDevicePowerUsbAway(long seconds);
void		HostDevicePowerDump(void);					// the console's !pm
void		HostDevicePowerTasks(void);					// the console's !tasks: each task's time since last asked
// the Newton asleep (hal/host/Host.h's gHostSleepWait and gHostSleepWake): its
// task waits up to so long, or until woken - the chip sleeping meanwhile
void		HostDevicePowerSleepWait(unsigned long milliseconds);
void		HostDevicePowerSleepWake(void);

// the sampling profiler of core 1 (tdeck_profile.cpp): started with the
// board, a phase's start marked, the samples printed (and the profiler
// stopped) - tools/tdeck/profile.py reads them
void		HostDeviceProfileStart(void);
void		HostDeviceProfileMark(const char* label);
void		HostDeviceProfileDump(void);

// (the task runtime's threads - os600/kernel/host/TaskRuntime.cpp declares
//  them for itself, extern "C")
extern "C" {
	int		HostDeviceStartThread(void (*entry)(void*), void* arg, size_t stackBytes);
	[[noreturn]] void	HostDeviceEndThread(void);
	// the window's shims (host/HostKeyboard.cpp), as every host's window
	// calls them: the pen in the display's pixels, a key as its Windows
	// virtual key code - from any thread
	void	HostWindowPenDown(long x, long y);
	void	HostWindowPenMove(long x, long y);
	void	HostWindowPenUp(void);
	void	HostWindowKey(long virtualKey, int down);
}

#endif	/* __ESP32HOST_H */
