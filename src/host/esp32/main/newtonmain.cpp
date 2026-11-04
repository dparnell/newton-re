/*
	File:		host/esp32/main/newtonmain.cpp

	Contains:	The ESP32-S3 host's Newton side (src/host/esp32/README.md):
				the OS booted (milestone B, docs/host-tdeck-pro.md) - the
				ROM's objects from the baked image mapped from flash, the
				kernel and its tasks on FreeRTOS (the task runtime's
				ESP-IDF branch), the newt world, a 240x320 one-bit display
				shown on the e-paper panel (tdeck_panel.cpp), the touch
				panel as the pen and the keyboard as the keyboard
				(tdeck_input.cpp), the store in RAM - with a NewtonScript
				prompt on the USB console: each line the console types is
				evaluated in the newt world (a 'host/'eval event), its
				result printed.  The desktop's newton (host/newton.cpp) is
				what this follows, its window the board's panel.
*/

#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "Compiler.h"
#include "REPTranslators.h"
#include "ROMImport.h"
#include "NativeFunctions.h"
#include "NewtonExceptions.h"
#include "Boot.h"
#include "UserBoot.h"
#include "NewtWorld.h"
#include "AEventHandler.h"
#include "NameServer.h"
#include "UserPorts.h"
#include "UserTasks.h"
#include "hal/FIQTimer.h"
#include "hal/host/Host.h"
#include "memory/host/KernelHeap.h"
#include "os600/kernel/host/TaskRuntime.h"
#include "host/HostViews.h"
#include "host/HostStores.h"
#include "host/HostKeyboard.h"
#include "hal/host/HostScreen.h"
#include "hal/host/HostPower.h"
#include "power/host/HostPowerSwitch.h"
#include "Unicode.h"
#include "hal/host/HostFlash.h"
#include "LongTime.h"
#include <time.h>
#include "host/HostWifi.h"
#include "NewtonGestalt.h"
#include "comms/host/HostLink.h"
#include "comms/host/HostCellProvider.h"
#include "comms/host/HostMeshApp.h"
#include "host/HostSettings.h"
#include "host/HostQuickSheet.h"
#include "HostSoundDriver.h"
#include "HostAudio.h"
#include "host/HostDeveloper.h"
#include "print/host/HostIPP.h"
#include "host/HostFontProvider.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>			// strcasecmp

#include "esp32host.h"

// the budget the device-sized run found enough (docs/host-tdeck-pro.md):
// the kernel heap in PSRAM with the NewtonScript heap inside it, a 4 MB
// internal flash (held in PSRAM: not kept between starts yet)
const Size		kKernelHeapSize = 3 * 1024 * 1024;
const long		kObjectHeapSize = 1024 * 1024;
const ULong		kFlashSize = 4 * 1024 * 1024;
const long		kDisplayWidth = 240;
const long		kDisplayHeight = 320;
const long		kDisplayDepth = 1;

static long long	gStart;
extern bool			gHostDeviceWifi;		// (esp32main.cpp: the WiFi started)


static void
Step(const char* what)
{
	unsigned long internalFree, psramFree;
	HostDeviceMemory(&internalFree, &psramFree);
	HostDeviceProfileMark(what);
	printf("newton: %s at %lld ms (internal RAM free %lu KB, PSRAM free %lu KB)\n",
		   what, (HostDeviceMicroseconds() - gStart) / 1000, internalFree >> 10, psramFree >> 10);
}


/*------------------------------------------------------------------------------
	NewtonScript for the board
------------------------------------------------------------------------------*/

// DeviceMilliseconds(): the board's clock, for timing NewtonScript on it;
// DeviceMemory(): [internal RAM free, PSRAM free], in KB
static Ref
FDeviceMilliseconds(RefArg /*rcvr*/)
{
	return MAKEINT((long) ((HostDeviceMicroseconds() / 1000) & 0x1fffffff));
}

// the board's WiFi as host/HostWifi.h's provider (tdeck_wifi.cpp)
static long
WifiScan(HostWifiNetwork* networks, long max)
{
	const long kMax = 24;
	DeviceWifiSeen seen[kMax];
	long count = HostDeviceWifiScan(seen, max < kMax ? max : kMax);
	for (long i = 0; i < count; i++)
	{
		memcpy(networks[i].ssid, seen[i].ssid, sizeof(networks[i].ssid));
		networks[i].rssi = seen[i].rssi;
		networks[i].secure = seen[i].secure;
		networks[i].known = seen[i].known;
	}
	return count;
}

static long
WifiKnown(char (*ssids)[kHostWifiSSIDSize], long max)
{
	return HostDeviceWifiKnown(ssids, max);
}

static void
WifiStatus(HostWifiStatusInfo* info)
{
	int state;
	unsigned long address;
	long rssi;
	HostDeviceWifiStatus(&state, info->ssid, &address, &rssi);
	info->state = (HostWifiState) state;
	info->address = address;
	info->rssi = rssi;
}

static const HostWifiProvider	gWifiProvider = { WifiScan, WifiKnown, HostDeviceWifiRemember, HostDeviceWifiForget, WifiStatus };


// the internal flash's writes and erases, to the store partition
static void
StoreWrite(ULong offset, const unsigned char* bytes, ULong size)
{
	HostDeviceStoreWrite((unsigned long) offset, bytes, (unsigned long) size);
}

static void
StoreErase(ULong offset, ULong size, ULong lanes)
{
	HostDeviceStoreErase((unsigned long) offset, (unsigned long) size, (unsigned long) lanes);
}

// DeviceProfileDump(): the boot's profile printed (tdeck_profile.cpp,
// tools/tdeck/profile.py); the profiler stops
static Ref
FDeviceProfileDump(RefArg /*rcvr*/)
{
	HostDeviceProfileDump();
	return NILREF;
}

// DeviceStore(): [the flash words written to the store partition, its erases]
static Ref
FDeviceStore(RefArg /*rcvr*/)
{
	unsigned long writes, erases;
	HostDeviceStoreCounts(&writes, &erases);
	RefVar list(MakeArray(2));
	SetArraySlot(list, 0, RefVar(MAKEINT((long) writes)));
	SetArraySlot(list, 1, RefVar(MAKEINT((long) erases)));
	return list;
}


// DevicePanelSet(name, value): a setting of the panel's refreshes
// (tdeck_panel.cpp) - name a symbol or a string ==> true when there is one
static Ref
FDevicePanelSet(RefArg /*rcvr*/, RefArg name, RefArg value)
{
	char text[32];
	if (IsSymbol(name))
		strncpy(text, SymbolName(name), sizeof(text) - 1);
	else
		ConvertFromUnicode(GetCString(name), text, kMacRomanEncoding, sizeof(text) - 1);
	text[sizeof(text) - 1] = 0;
	return HostDevicePanelSet(text, RINT(value)) ? TRUEREF : NILREF;
}

// DevicePower(): [mA now, the mean mA since last asked, its 1 s samples, mV,
// the Newton's idle time since last asked in tenths of a per cent, how often
// it went idle meanwhile] from the fuel gauge (negative: drawn from the
// battery) and tdeck_power.cpp - the first call starts the sampling; nil
// without a gauge
static Ref
FDevicePower(RefArg /*rcvr*/)
{
	long now, mean, samples, millivolts, idle, idles;
	if (!HostDeviceBatteryCurrent(&now, &mean, &samples, &millivolts))
		return NILREF;
	HostDevicePowerIdle(&idle, &idles);
	RefVar list(MakeArray(6));
	SetArraySlot(list, 0, RefVar(MAKEINT(now)));
	SetArraySlot(list, 1, RefVar(MAKEINT(mean)));
	SetArraySlot(list, 2, RefVar(MAKEINT(samples)));
	SetArraySlot(list, 3, RefVar(MAKEINT(millivolts)));
	SetArraySlot(list, 4, RefVar(MAKEINT(idle)));
	SetArraySlot(list, 5, RefVar(MAKEINT(idles)));
	return list;
}

// DevicePowerSet(name, on): 'battery - the charger's input cut, so that the
// board runs from its battery while on USB (for measuring it); 'keyLight -
// the keyboard's light allowed; 'wifi - the WiFi radio on; 'wifiSave - its
// power save, 0 none, 1 min modem (ESP-IDF's default), 2 max modem (ours); 'sleep -
// automatic light sleep allowed; 'usbAway - the USB let go for that many
// seconds (light sleep can then happen; the console comes back after);
// 'touchTrace - the Max's touch chip's reports on the console; 'buzz - its
// motor's effect of that number
// ==> true when taken
static Ref
FDevicePowerSet(RefArg /*rcvr*/, RefArg name, RefArg on)
{
	if (!IsSymbol(name))
		return NILREF;
	const char* what = SymbolName(name);
	if (strcasecmp(what, "battery") == 0)
		return HostDeviceBatteryRunOnBattery(NOTNIL(on)) ? TRUEREF : NILREF;
	if (strcasecmp(what, "keyLight") == 0)
	{
		HostDeviceSetKeyLight(NOTNIL(on));
		return TRUEREF;
	}
	if (strcasecmp(what, "wifi") == 0)
		return gHostDeviceWifi && HostDeviceWifiSetRadio(NOTNIL(on)) ? TRUEREF : NILREF;
	if (strcasecmp(what, "wifiSave") == 0)
		return gHostDeviceWifi && ISINT(on) && HostDeviceWifiSetPowerSave((int) RINT(on)) ? TRUEREF : NILREF;
	if (strcasecmp(what, "sleep") == 0)
		return HostDevicePowerSetLightSleep(NOTNIL(on)) ? TRUEREF : NILREF;
	if (strcasecmp(what, "usbAway") == 0)
		return ISINT(on) && HostDevicePowerUsbAway(RINT(on)) ? TRUEREF : NILREF;
	if (strcasecmp(what, "buzz") == 0 && ISINT(on))
	{
		HostDeviceBuzz((int) RINT(on));
		return TRUEREF;
	}
	if (strcasecmp(what, "touchTrace") == 0)
	{
		HostDeviceTouchTrace(NOTNIL(on));
		return TRUEREF;
	}
	return NILREF;
}

static Ref
FDeviceMemory(RefArg /*rcvr*/)
{
	unsigned long internalFree, psramFree;
	HostDeviceMemory(&internalFree, &psramFree);
	RefVar list(MakeArray(2));
	SetArraySlot(list, 0, RefVar(MAKEINT((long) (internalFree >> 10))));
	SetArraySlot(list, 1, RefVar(MAKEINT((long) (psramFree >> 10))));
	return list;
}


/*------------------------------------------------------------------------------
	The console's lines, evaluated in the newt world: a 'host/'eval event
	sent to the world's port (as the desktop's test of the world does,
	newt/tests/test_Newt.cpp), its handler installed by the world's boot
------------------------------------------------------------------------------*/

struct TDeviceEvalEvent : public TAEvent
{
	const char*	fSource;		// (nil: the quick sheet pulled down - HostQuickSheetPulled, nothing printed)
	bool		fQuiet;			// the result not printed (the board's own: a touch key)
};

class TDeviceEvalHandler : public TAEventHandler
{
public:
	virtual void	AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event)
	{
		TDeviceEvalEvent* eval = (TDeviceEvalEvent*) event;
		if (eval->fSource == nil)
		{
			HostQuickSheetPulled();
			SetReply(*size, event);
			if (token != nil && token->GetReplyId() != 0)
				ReplyImmed();
			return;
		}
		long long t = HostDeviceMicroseconds();
		newton_try
		{
			RefVar fn(ParseString(RefVar(MakeString(eval->fSource))));
			if ((Ref) fn != NILREF)
			{
				RefVar result(InterpretBlock(fn, RefVar(gVarFrame)));
				if (!eval->fQuiet)
					PrintObject(result, 0);
			}
		}
		newton_catch_all
		{
			gREPout->ExceptionNotify(&_info.exception);
		}
		end_try;
		printf("\n(%lld ms)\n> ", (HostDeviceMicroseconds() - t) / 1000);
		SetReply(*size, event);
		if (token != nil && token->GetReplyId() != 0)
			ReplyImmed();
	}
};

static TDeviceEvalHandler*	gEvalHandler = nil;


// the console task woken: a line typed, the quick sheet pulled, the clock
// set (esp32host.h) - it waits on the keyboard's kick, as the keys do
void
HostDeviceKick(void)
{
	HostKeyboardKick();
}


// the machine asleep: waited for as tdeck_power.cpp waits (hal/host/Host.h's
// gHostSleepWait)
static void
SleepWait(ULong milliseconds)
{
	HostDevicePowerSleepWait((unsigned long) milliseconds);
}


// the display as it is now, for the panel (its width and height swap when
// the Newton turns it: hal/host/HostScreen.h)
static void
DisplayNow(const unsigned char** grays, long* width, long* height)
{
	THostScreenDriver* display = HostDisplay();
	*grays = display->Pixels();
	*width = display->Width();
	*height = display->Height();
}


// the world's boot (gNewtHostBoot): the desktop's, headless - the object
// system, the display, the toolbox (host/HostViews.cpp), the timers - and
// the eval handler in the world's task
static void
DeviceBoot(void)
{
	Step("the newt world starting");
	// The board's keyboard is built in, as an eMate 300's is: the keyboard
	// gestalt says so, and the root view, made in HostBootNewtWorld, reads
	// it and takes a keyboard as connected from the start (views/RootView.cpp)
	// - so the system leaves the on-screen keyboards it would open out
	// (Internet Setup's slips, for one) and keeps them for when they are
	// asked for (a keyboard button)
	static UByte builtInKeyboard[4] = { 1, 0, 0, 0 };
	TUGestalt gestalt;
	gestalt.RegisterGestalt(kGestalt_Extended_Base + 10, builtInKeyboard, sizeof(builtInKeyboard));
	HostBootNewtWorld();
	if (HostDisplay() != nil)
	{
		HostDevicePanelFollow(DisplayNow);
		gHostScreenChanged = HostDevicePanelChanged;	// (the panel waits for it)
		HostDevicePanelChanged();
	}
	InitFIQTimer();
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "DeviceMilliseconds")), RefVar(MakeCFunction((void*) FDeviceMilliseconds, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "DeviceMemory")), RefVar(MakeCFunction((void*) FDeviceMemory, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "DevicePanelSet")), RefVar(MakeCFunction((void*) FDevicePanelSet, 2, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "DeviceStore")), RefVar(MakeCFunction((void*) FDeviceStore, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "DeviceProfileDump")), RefVar(MakeCFunction((void*) FDeviceProfileDump, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "DevicePower")), RefVar(MakeCFunction((void*) FDevicePower, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "DevicePowerSet")), RefVar(MakeCFunction((void*) FDevicePowerSet, 2, nil)));
	// results printed on the console (the world's REP out translator
	// otherwise prints nowhere)
	HostInitREP(stdout, nil);
	gEvalHandler = new TDeviceEvalHandler;
	gEvalHandler->Init('eval', 'host');
	Step("the newt world booted");
}


// the world's PreMain (gNewtHostPreMain), as the desktop's NewtonPreMain
// (host/newton.cpp) does it, of what the board has: the host's link for the
// Newton Internet Enabler (comms/host/HostLink.h), which puts the "Host
// network" module in Internet Setup - without it the NIE has no module and
// Internet Setup will not open
static void
DevicePreMain(void)
{
	HostLinkStart();
	HostCellProviderStart();			// (the 4G module, for the Phone transport: comms/host/HostCellProvider.h)
	HostMeshAppStart();					// (the LoRa radio's mesh node, for the Mesh application: comms/host/HostMeshApp.h)
	HostInstallIPPPrinters();			// (print/host/HostIPP.h: the network's IPP printers, in the Print slip and the Network Printers panel; no PNG host printer on the board)
	HostQuickSheetSetLockHook(HostDeviceSetLocked);
	if (HostDeviceCanPowerOff())
		HostQuickSheetSetPowerOff(HostDeviceBatteryPowerOff);
	// the Newton's sleep (docs/host-lock-screen.md, "Sleep"): the e-paper
	// keeps the lock screen meanwhile, the machine sleeps while it is
	// locked, a key, the touch panel, the BOOT button (tdeck_input.cpp: the
	// power switch), a message or a call (the engines' news) wakes it, and
	// the chip sleeps while it waits (tdeck_power.cpp)
	gHostScreenKeepsPicture = true;
	HostQuickSheetSetLockSleeps(true);
	gHostSleepWait = SleepWait;
	gHostSleepWake = HostDevicePowerSleepWake;
	HostPowerWindowOpened();
	HostInstallPowerGlobals();			// (power/host/HostPowerSwitch.h: HostPowerSwitch(), HostSleepCount(), ...)
	HostInstallQuickSheet();			// (host/HostQuickSheet.h: the sheet pulled down from the top edge - tdeck_input.cpp - and the lock screen)
	HostInstallSettings();				// (the Host preferences panel: the panel's quick updates and clean-ups)
	HostInstallDeveloper();				// (the Developer preferences panel: the profile to the console)
}


// The host's fonts (host/HostFontProvider.h): the board has no font engine
// of its own to give the Newton, so the Host panel offers none
bool
HostFontsAvailable(void)
{
	return false;
}


const char*
HostFontsChosen(void)
{
	return nil;
}


// The Newton's clock set from the board's, once the board's has been set
// from the network (tdeck_wifi.cpp): GMT in seconds from 1904, as
// HostBootNewtWorld sets it at boot from the same clock (host/HostViews.cpp)
static void
SetClockFromBoard(void)
{
	const unsigned long long kSecondsFrom1904To1970 = 2082844800ULL;
	time_t now = time(nil);
	TTime gmt((ULong) ((unsigned long long) now + kSecondsFrom1904To1970), kSeconds);
	TURealTimeAlarm::SetTime(gmt);
	struct tm t;
	gmtime_r(&now, &t);
	printf("newton: the clock set: %04d-%02d-%02d %02d:%02d:%02d GMT\n",
		   t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
}


// the kernel services task (gHostKernelServicesTask): the keyboard's keys
// (host/HostKeyboard.cpp's queue, which tdeck_input.cpp fills), the quick
// sheet's pulls and the console's lines to the world, a hundredth of a
// second at a time
static void
DeviceServices(void)
{
	TUNameServer nameServer;
	TObjectId portId = 0;
	ULong spec = 0;
	while (nameServer.Lookup("newt", "TUPort", &portId, &spec) != noErr)
		Sleep(50 * kMilliseconds);
	while (!gNewtIsAliveAndWell)
		Sleep(50 * kMilliseconds);
	Step("the OS is up");
	printf("> ");
	TUPort newtPort(portId);
	static char line[512];
	unsigned long clockSyncs = 0;
	HostKeyboardWaitInit();
	for (;;)
	{
		if (HostDeviceClockSyncs() != clockSyncs)
		{
			clockSyncs = HostDeviceClockSyncs();
			SetClockFromBoard();
		}
		HostKeyboardSendKeys(&newtPort);
		if (HostDeviceSheetPulled())
		{
			TDeviceEvalEvent pulled;
			pulled.fAEventClass = 'host';
			pulled.fAEventID = 'eval';
			pulled.fSource = nil;
			pulled.fQuiet = true;
			ULong replySize = 0;
			newtPort.SendRPC(&replySize, &pulled, sizeof(pulled), &pulled, sizeof(pulled));
		}
		// the Max's touch keys: the ROM's scroll and Overview buttons'
		// commands (romsrc/objects/shared/frames.ns: upButton, ovButton,
		// downButton), left to right up, Overview, down - aeScrollUp 0x2d,
		// aeOverview 0x33, aeScrollDown 0x2e (views/Commands.h)
		for (int key; (key = HostDeviceTouchKeyPressed()) >= 0; )
		{
			static const char* const kTouchKeyCommands[3] = {
				"PostCommand('viewfrontmost, 0x2d); RefreshViews();",
				"PostCommand('viewfrontmost, 0x33);",
				"PostCommand('viewfrontmost, 0x2e); RefreshViews();"
			};
			TDeviceEvalEvent command;
			command.fAEventClass = 'host';
			command.fAEventID = 'eval';
			command.fSource = kTouchKeyCommands[key];
			command.fQuiet = true;
			ULong replySize = 0;
			newtPort.SendRPC(&replySize, &command, sizeof(command), &command, sizeof(command));
		}
		if (HostDeviceReadLine(line, sizeof(line)))
		{
			TDeviceEvalEvent eval;
			eval.fAEventClass = 'host';
			eval.fAEventID = 'eval';
			eval.fSource = line;
			eval.fQuiet = false;
			ULong replySize = 0;
			newtPort.SendRPC(&replySize, &eval, sizeof(eval), &eval, sizeof(eval));
		}
		else
			HostKeyboardWait(kNoTimeout);	// (until a key, a line, a pull or a clock set - HostDeviceKick: it looked every 10 ms, which woke the board a hundred times a second)
	}
}


void
NewtonMainOnDevice(const void* image, size_t size)
{
	gStart = HostDeviceMicroseconds();
	if (gHostDeviceWifi)
		HostWifiSetProvider(&gWifiProvider);
	// every NewtonScript throw printed with its script stack (frames/Objects.cpp
	// reads it at the first throw): the board has no environment to set it in,
	// and the console is where a fault on it is found
	setenv("NEWTON_TRACE_EXCEPTIONS", "1", 1);
	if (image == nil || ImportBakedObjects(image, size) != noErr)
		printf("newton: no baked ROM objects - the OS cannot boot without them\n");
	else
		Step("the ROM's objects (the baked image)");

	HostSetKernelHeapSize(kKernelHeapSize);
	HostUseRealClock(true);
	// the internal flash on the board's store partition (tdeck_store.cpp),
	// the internal store the ROM's flash store on it, kept between starts;
	// without the partition, a store in memory
	static HostFlashBacking backing;
	unsigned char* flashBytes;
	unsigned long flashSize;
	bool existed;
	if (HostDeviceStoreOpen(&flashBytes, &flashSize, &existed))
	{
		backing.bytes = flashBytes;
		backing.size = (ULong) flashSize;
		backing.existed = existed;
		backing.write = StoreWrite;
		backing.erase = StoreErase;
		HostFlashSetBacking(&backing);
		HostSetStoreFile("store partition");		// (any name: the backing is the flash - hal/host/HostFlash.h)
		HostSetNewFlash(backing.size, true);
		Step(existed ? "the store partition (used before)" : "the store partition (new)");
	}
	else
	{
		HostSetStoreFile(nil);
		HostSetNewFlash(kFlashSize, false);
	}
	HostConfigureNewtWorld(nil, kObjectHeapSize, kDisplayWidth, kDisplayHeight, kDisplayDepth);
	gNewtHostBoot = DeviceBoot;
	gNewtHostPreMain = DevicePreMain;
	NewtInstallUserMain();
	gHostKernelServicesTask = DeviceServices;
	// the sound hardware: the Max's codec (tdeck_audio.cpp); a Pro has none (before the OS boots: the sound server looks for it there)
	static const HostSoundBackend kCodec = { HostAudioPlay, HostMicrophoneRecord };
	if (HostAudioOpen(kHostSoundRate) && HostMicrophoneOpen(kHostSoundRate))
		HostInstallSoundDriver(&kCodec);
	Step("the OS booting");
	OsBoot();
}
