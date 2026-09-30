/*
	File:		host/HostKeyboard.cpp

	Contains:	The host's keyboard tool.
*/

#include "HostKeyboard.h"
#include "Keyboard.h"
#include "Commands.h"
#include "NewtWorld.h"
#include "UserTasks.h"
#include "UserPorts.h"
#include "NameServer.h"
#include "os600/kernel/host/TaskRuntime.h"
#include "HostTablet.h"
#include "hal/host/HostPower.h"
#include "power/host/HostPowerSwitch.h"
#include "HostPackages.h"
#include <atomic>

// the key queue: a ring, one writer (the window), one reader (the task)
struct HostKey { long code; Boolean down; };
const long kHostKeyQueueSize = 256;
static HostKey			gHostKeys[kHostKeyQueueSize];
static std::atomic<long>	gHostKeyHead(0);		// the next to send
static std::atomic<long>	gHostKeyTail(0);		// the next to fill
static std::atomic<bool>	gHostQuit(false);


void
HostKeyboardPush(long keyCode, Boolean down)
{
	long tail = gHostKeyTail.load();
	long next = (tail + 1) % kHostKeyQueueSize;
	if (next == gHostKeyHead.load())
		return;
	gHostKeys[tail].code = keyCode;
	gHostKeys[tail].down = down;
	gHostKeyTail.store(next);
}


void
HostKeyboardQuit(void)
{
	gHostQuit.store(true);
}


// The Newton's key codes (the ADB keyboard's) for the keys of a PC
// keyboard.  The two that are easy to get wrong are Z and Y: the ADB
// code 6 is Z and 16 is Y, which is a QWERTY keyboard's order; they were
// the other way round here, which is where they sit on the German
// QWERTZ one, so typing Z gave a y and Y gave a z.
//
// A key is named by its Windows virtual key code, which for the letters
// and digits is the ASCII code.
long
HostKeyCodeForVirtualKey(long vk)
{
	static const struct { long vk; long code; } kMap[] = {
		{ 'A', 0 }, { 'S', 1 }, { 'D', 2 }, { 'F', 3 }, { 'H', 4 }, { 'G', 5 }, { 'Z', 6 }, { 'X', 7 }, { 'C', 8 }, { 'V', 9 },
		{ 'B', 11 }, { 'Q', 12 }, { 'W', 13 }, { 'E', 14 }, { 'R', 15 }, { 'Y', 16 }, { 'T', 17 },
		{ '1', 18 }, { '2', 19 }, { '3', 20 }, { '4', 21 }, { '6', 22 }, { '5', 23 }, { '9', 25 }, { '7', 26 }, { '8', 28 }, { '0', 29 },
		{ 'O', 31 }, { 'U', 32 }, { 'I', 34 }, { 'P', 35 }, { 0x0d, 36 }, { 'L', 37 }, { 'J', 38 }, { 'K', 40 },
		{ 0xbc, 43 }, { 0xbf, 44 }, { 'N', 45 }, { 'M', 46 }, { 0xbe, 47 }, { 0x09, 48 }, { 0x20, 49 }, { 0x08, 51 }, { 0x1b, 53 },
		{ 0x11, 55 }, { 0x10, 56 }, { 0x14, 57 }, { 0x12, 58 },
		{ 0x25, 123 }, { 0x27, 124 }, { 0x28, 125 }, { 0x26, 126 },
		{ 0x2e, 117 }, { 0x24, 115 }, { 0x23, 119 }, { 0x21, 116 }, { 0x22, 121 }
	};
	for (unsigned i = 0; i < sizeof(kMap) / sizeof(kMap[0]); i++)
		if (kMap[i].vk == vk)
			return kMap[i].code;
	return -1;
}


// The task: the newt world's port found by name, the keyboard connected,
// then every ten milliseconds the queued keys sent as 'keyb events
// (each an RPC the world replies to with the repeat rates); the run ended
// when the window says so.
void
HostKeyboardToolTask(void)
{
	TUNameServer nameServer;
	TObjectId portId = 0;
	ULong spec = 0;
	while (!gHostQuit.load() && nameServer.Lookup("newt", "TUPort", &portId, &spec) != noErr)
		Sleep(20 * kMilliseconds);
	if (portId != 0)
	{
		TUPort newtPort(portId);
		while (!gNewtIsAliveAndWell && !gHostQuit.load())
			Sleep(20 * kMilliseconds);
		KeyboardEvent connected(aeKeyboardConnected, 1);
		KeyboardEvent reply(aeKeyUp, 0);
		ULong replySize = 0;
		newtPort.SendRPC(&replySize, &connected, sizeof(connected), &reply, sizeof(reply));
		while (!gHostQuit.load())
		{
			while (gHostKeyHead.load() != gHostKeyTail.load())
			{
				HostKey key = gHostKeys[gHostKeyHead.load()];
				gHostKeyHead.store((gHostKeyHead.load() + 1) % kHostKeyQueueSize);
				KeyboardEvent event(key.down ? aeKeyDown : aeKeyUp, key.code);
				newtPort.SendRPC(&replySize, &event, sizeof(event), &reply, sizeof(reply));
			}
			HostSendQueuedPackages();		// (a package dropped onto the window)
			Sleep(10 * kMilliseconds);
		}
	}
	HostStopTasks();
}


// the window's shims (host/win32/HostWindow.cpp calls these with C linkage)
extern "C" {

// (the pen is the tablet's own - the host's tablet driver samples it and
//  the calibration applies, hal/host/HostTablet.h; while the journal plays
//  the driver ignores it; while the machine sleeps a tap wakes it -
//  the host's power switch within the pen's reach, hal/host/HostPower.h -
//  and that stroke goes no further)
static std::atomic<bool>	gHostPenWoke(false);

void
HostWindowPenDown(long x, long y)
{
	if (HostPowerAsleep())
	{
		gHostPenWoke.store(true);
		HostPowerWake(kHostPowerEventSwitch);
		return;
	}
	HostTabletRawPenDown(x, y);
}

void
HostWindowPenMove(long x, long y)
{
	if (gHostPenWoke.load())
		return;
	HostTabletRawPenMove(x, y);
}

void
HostWindowPenUp(void)
{
	if (gHostPenWoke.exchange(false))
		return;
	HostTabletRawPenUp();
}

// F12 is the power switch and F11 the backlight button (the Windows
// virtual key codes, whichever host the window is on); while the machine
// sleeps any other key wakes it too, and goes no further
void
HostWindowKey(long virtualKey, int down)
{
	const long kVirtualKeyF11 = 0x7A, kVirtualKeyF12 = 0x7B;
	if (virtualKey == kVirtualKeyF12 || virtualKey == kVirtualKeyF11)
	{
		if (down)
			HostPowerSwitchPress(virtualKey == kVirtualKeyF12 ? 'powr' : 'bklt');
		return;
	}
	if (HostPowerAsleep())
	{
		if (down)
			HostPowerWake(kHostPowerEventSwitch);
		return;
	}
	long code = HostKeyCodeForVirtualKey(virtualKey);
	if (code >= 0)
		HostKeyboardPush(code, down != 0);
}

void
HostWindowClosed(void)
{
	HostKeyboardQuit();
}

// The window's thread has started.  It is not a Newton task, so the
// system-call stubs must refuse it rather than take gCurrentTask for it
// (kernel/host/TaskRuntime.h): everything it does from here - the pen
// records, the key queue - has to be a plain memory write.
void
HostWindowThreadStarted(void)
{
	HostAlienThread();
	HostPowerWindowOpened();			// (a tap or a key can wake the machine now)
}

}
