/*
	File:		host/esp32/main/tdeck_input.cpp

	Contains:	The T-Deck Pro's touch panel and keyboard as the Newton's pen
				and keyboard (src/host/esp32/README.md): a task of the
				board's own, on the core the Newton does not use, reads
				both over I2C when they say they have something and hands
				what it finds to the window shims the desktop's windows call
				(host/HostKeyboard.cpp: HostWindowPenDown/Move/Up into the
				tablet, HostWindowKey into the keyboard tool's queue - all
				callable from any thread).

				On the T-Deck Max the touch panel is another chip with
				another protocol and three touch keys below the screen
				(TouchReadMax; docs/host-tdeck-max.md); everything after the
				report is read is the same.

				Touch: the CST328 (as the device spike read it,
				docs/host-tdeck-pro.md) reports in the panel's own pixels,
				which are the display's; the Newton's own pen calibration
				takes out the finger's offset (Prefs, Pen; or the
				calibration screen it asks for).  A finger down is the pen
				down, its moves the pen's, its lifting the pen up; a finger
				that stops being reported for kTouchLostMs is lifted.

				The quick sheet (host/HostQuickSheet.h): a finger that
				lands within kPullZone of the screen's top edge is held
				back from the Newton until it shows what it is - pulled
				kPullDistance down, it is the sheet pulled down
				(HostDeviceSheetPulled, which the Newton's side asks) and
				nothing of it reaches the Newton; lifted first, it is a
				tap there; moved sideways, or held without coming down, it
				is the pen's from where it landed.  While the screen is
				locked (HostDeviceSetLocked: the lock screen up) the top
				edge is the pen's like the rest, the keyboard's presses
				are left out (releases still sent, for keys pressed
				before) and its light is off.

				Keys: the TCA8418's 4 x 10 matrix with the owner's layout
				(lilyphone1's peri_keypad.cpp): the letters, Backspace,
				Enter, space and '$'; the two Shift keys are the Newton's
				Shift; Sym held gives the symbol layer (digits and
				punctuation, each sent as the US key that types it, with
				Shift around it when that key needs it); Alt held gives the
				owner's navigation keys where the layout has them (Esc,
				Home, End, Up, Down, Page Up) and is otherwise the Newton's
				Command key (Alt-C, Alt-V ...: Ctrl, as on the desktop).  A key is sent as a
				Windows virtual key code, as every host's window does.

				Interrupts, not polls, so that the chip can sleep
				(tdeck_power.cpp): the touch panel's INT (GPIO 12) and the
				keyboard's (GPIO 15, the TCA8418's, low until its INT_STAT
				is cleared) each wake the chip from light sleep (a GPIO
				wake-up, low level) and the task with it (an interrupt,
				disarmed until the task has read the chip).  While a finger
				is down the touch panel is read every 10 ms as before, its
				moves and its lifting seen; with nothing touched the task
				waits for an interrupt.  Were the interrupts not to be had,
				both are polled every 10 ms as before.  The console's !pm
				counts them.
				ESP-IDF's headers only (esp32host.h says why).
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_sleep.h"

#include "esp32host.h"
#include "tdeck_board.h"

#define I2C_PORT		I2C_NUM_0
#define PIN_TOUCH_INT	12
#define PIN_KEY_INT		15
#define PIN_BOOT_BUTTON	0				// (lilyphone1's BOARD_BOOT_PIN: low while pressed)
#define PIN_KEY_LED		42
#define ADDR_TOUCH		0x1A
#define ADDR_KEYBOARD	0x34

const long		kPanelWidth = 240;
const long		kPanelHeight = 320;
const int64_t	kTouchLostMs = 150;
const long		kPullZone = 14;			// a finger landing this near the top is held back
const long		kPullDistance = 40;		// ... and pulled this far down is the sheet
const long		kPullSideways = 16;		// ... moved this far sideways first: the pen's
const int64_t	kPullHoldMs = 300;		// ... held this long without coming down: the pen's

static std::atomic<unsigned>	gPulls(0);			// the sheet pulled down, not yet asked for
static std::atomic<bool>		gLocked(false);		// the lock screen up
static std::atomic<bool>		gKeyLight(true);	// the keyboard's light allowed (HostDeviceSetKeyLight)
static bool						gTouchTrace = false;	// the Max's reports on the console (HostDeviceTouchTrace)

// Windows virtual key codes (host/HostKeyboard.cpp maps them to the Newton's)
enum
{
	kVKBack = 0x08, kVKReturn = 0x0d, kVKShift = 0x10, kVKCommand = 0x11, kVKEscape = 0x1b, kVKSpace = 0x20,
	kVKPageUp = 0x21, kVKEnd = 0x23, kVKHome = 0x24, kVKUp = 0x26, kVKDown = 0x28,
	kVKPowerSwitch = 0x7b,				// F12: every host's power switch
	kVKNone = 0x07						// (no key: host/HostKeyboard.cpp maps it to nothing)
};


static esp_err_t
Write(uint8_t address, const uint8_t* data, size_t length)
{
	return i2c_master_write_to_device(I2C_PORT, address, data, length, pdMS_TO_TICKS(20));
}


static esp_err_t
ReadRegister(uint8_t address, const uint8_t* reg, size_t regLength, uint8_t* data, size_t length)
{
	return i2c_master_write_read_device(I2C_PORT, address, reg, regLength, data, length, pdMS_TO_TICKS(20));
}


/*------------------------------------------------------------------------------
	Touch
------------------------------------------------------------------------------*/

// The Max's touch panel (a Hynitron part of the CST66xx family by its
// protocol and its chip type, 0xCACA220E; LilyGO's README says CST3530, its
// pin map still CST328) speaks another protocol to the Pro's CST328.  Every
// command is a four-byte register number; the report is at 0xD0070000:
//
//		0-1		the sum of the blocks that follow, plus 0x55, low byte first
//		2		0xFF for touches (0xF0 a gesture, 0xE0 proximity)
//		3		fingers in the low nibble, keys in the high
//		4...	five bytes each, keys first:
//					a key:		byte 4 its number in the low nibble, and
//								down in the high
//					a finger:	x low, y low, pressure, x and y high (the
//								low nibble x's), id low and down high
//
// and is taken with 0xD00002AB.  Read off LilyGO's
// lib/HynTouch/src/hyn_cst66xx.c and tried on the board.
static esp_err_t
TouchMaxCommand(uint32_t command)
{
	uint8_t b[4] = { (uint8_t) (command >> 24), (uint8_t) (command >> 16), (uint8_t) (command >> 8), (uint8_t) command };
	return Write(ADDR_TOUCH, b, 4);
}


static void
TouchMaxStart(void)
{
	// (twice: the first wakes the chip's I2C from low power)
	TouchMaxCommand(0xD0000400);
	vTaskDelay(pdMS_TO_TICKS(1));
	TouchMaxCommand(0xD0000400);
	uint8_t reg[4] = { 0xD0, 0x03, 0x00, 0x00 }, info[50];
	if (ReadRegister(ADDR_TOUCH, reg, 4, info, sizeof(info)) == ESP_OK)
		printf("newton: touch chip %02X%02X%02X%02X, firmware %02X%02X%02X%02X, %d x %d, %d keys\n",
			   info[3], info[2], info[1], info[0], info[35], info[34], info[33], info[32],
			   info[28] | (info[29] << 8), info[30] | (info[31] << 8), info[27]);
	else
		printf("newton: the touch chip does not answer\n");
	TouchMaxCommand(0xD0000000);		// (LilyGO's normal mode: the three of them)
	TouchMaxCommand(0xD0000C00);
	TouchMaxCommand(0xD0000100);
}


static void
TouchStart(void)
{
	HostDeviceBoardLineStart(kLineTouchReset);
	HostDeviceBoardSet(kLineTouchReset, false);
	vTaskDelay(pdMS_TO_TICKS(10));
	HostDeviceBoardSet(kLineTouchReset, true);
	vTaskDelay(pdMS_TO_TICKS(100));
	gpio_set_direction((gpio_num_t) PIN_TOUCH_INT, GPIO_MODE_INPUT);
	gpio_set_pull_mode((gpio_num_t) PIN_TOUCH_INT, GPIO_PULLUP_ONLY);
	if (gBoardMax)
	{
		TouchMaxStart();
		return;
	}
	static const uint8_t normal[2] = { 0xD1, 0x09 };		// normal mode
	Write(ADDR_TOUCH, normal, 2);
}


// The Pro's report (register 0xD000, seven bytes): valid when byte 6 is 0xAB;
// the fingers in byte 5's low bits; the first finger's state in byte 0's
// low nibble (6: down), its x and y's high bits in bytes 1 and 2, their
// low nibbles in byte 3 ==> whether there was a report
static bool
TouchReadPro(bool* fingerDown, long* x, long* y)
{
	static const uint8_t reg[2] = { 0xD0, 0x00 };
	uint8_t b[7];
	if (ReadRegister(ADDR_TOUCH, reg, 2, b, 7) != ESP_OK)
		return false;
	static const uint8_t sync[3] = { 0xD0, 0x00, 0xAB };
	Write(ADDR_TOUCH, sync, 3);
	if (b[6] != 0xAB || b[0] == 0xAB)
		return false;
	*fingerDown = (b[5] & 0x7F) > 0 && (b[0] & 0x0F) == 0x06;
	*x = (b[1] << 4) | (b[3] >> 4);
	*y = (b[2] << 4) | (b[3] & 0x0F);
	return true;
}


static const int	kTouchKeys = 3;
static std::atomic<unsigned>	gTouchKeyPresses[kTouchKeys];	// not yet asked for


// a touch key pressed: its action for the Newton's side to take (locked or
// asleep, it only wakes the machine, as a key does)
static void
TouchKey(int key)
{
	if (gLocked)
	{
		HostWindowKey(kVKNone, 1);
		HostWindowKey(kVKNone, 0);
		return;
	}
	HostDeviceBuzz(1);
	gTouchKeyPresses[key]++;
	HostDeviceKick();
}


// The Max's report (above) ==> whether there was one
static bool
TouchReadMax(bool* fingerDown, long* x, long* y)
{
	static const uint8_t reg[4] = { 0xD0, 0x07, 0x00, 0x00 };
	const int kBlock = 5, kMost = 5;
	uint8_t b[4 + kMost * kBlock];
	if (ReadRegister(ADDR_TOUCH, reg, 4, b, 4 + kBlock) != ESP_OK)
		return false;
	int fingers = b[3] & 0x0F, keys = b[3] >> 4;
	bool good = b[2] == 0xFF && fingers + keys <= kMost;
	if (good && fingers + keys > 1)
		good = i2c_master_read_from_device(I2C_PORT, ADDR_TOUCH, b + 4 + kBlock, (fingers + keys - 1) * kBlock, pdMS_TO_TICKS(20)) == ESP_OK;
	if (good)
	{
		uint16_t sum = 0x55;
		for (int k = 0; k < (fingers + keys) * kBlock; k++)
			sum += b[4 + k];
		good = sum == (uint16_t) (b[0] | (b[1] << 8));
	}
	TouchMaxCommand(0xD00002AB);
	// (the chip goes on answering with its last report: shown once)
	static uint8_t shown[4 + kBlock];
	if (gTouchTrace && (!good || fingers + keys != 0) && memcmp(shown + 2, b + 2, 2 + kBlock) != 0)
	{
		memcpy(shown, b, sizeof(shown));
		printf("[touch] %s %02X, %d fingers %d keys:", good ? "report" : "BAD", b[2], fingers, keys);
		for (int k = 0; k < (good ? (fingers + keys) * kBlock : kBlock); k++)
			printf(" %02X", b[4 + k]);
		printf("\n");
	}
	if (!good)
		return false;
	if (keys != 0)
	{
		static bool down[kTouchKeys];
		int key = b[8] & 0x0F;
		bool press = (b[8] >> 4) != 0;
		if (key < kTouchKeys && press != down[key])
		{
			down[key] = press;
			if (press)
				TouchKey(key);
		}
	}
	if (fingers != 0)
	{
		const uint8_t* f = b + 4 + keys * kBlock;
		*fingerDown = (f[4] >> 4) != 0;
		*x = f[0] | ((f[3] & 0x0F) << 8);
		*y = f[1] | ((f[3] & 0xF0) << 4);
	}
	return true;
}


// ==> whether a finger is still down (or held back)
static bool
TouchPoll(void)
{
	// the finger: up; held back (landed at the top edge); down (the pen's);
	// the sheet pulled (the rest of it nobody's)
	enum { kUp, kHeld, kDown, kPulled };
	static int state = kUp;
	static long lastX = -1, lastY = -1;
	static long startX = 0, startY = 0;
	static int64_t lastSeen = 0, startAt = 0;
	int64_t now = esp_timer_get_time();
	bool fingerDown = false;
	long x = 0, y = 0;
	bool valid = gBoardMax ? TouchReadMax(&fingerDown, &x, &y) : TouchReadPro(&fingerDown, &x, &y);
	if (valid && fingerDown)
	{
		if (x >= kPanelWidth) x = kPanelWidth - 1;
		if (y >= kPanelHeight) y = kPanelHeight - 1;
	}
	if (valid && fingerDown && HostDeviceDisplayTurned())
	{
		// (the display turned a quarter on the panel - tdeck_panel.cpp's
		//  Convert: panel (px, py) shows display (319 - py, px))
		long dx = kPanelHeight - 1 - y, dy = x;
		x = dx;
		y = dy;
	}
	if (valid && fingerDown)
	{
		lastSeen = now;
		if (state == kUp)
		{
			if (y < kPullZone && !gLocked)
			{
				state = kHeld;
				startX = x;
				startY = y;
				startAt = now;
			}
			else
			{
				state = kDown;
				HostWindowPenDown(x, y);
			}
		}
		else if (state == kHeld)
		{
			if (y - startY >= kPullDistance)
			{
				state = kPulled;
				gPulls++;
				HostDeviceKick();
			}
			else if (labs(x - startX) >= kPullSideways || ((now - startAt) / 1000 >= kPullHoldMs && y - startY < 8))
			{
				state = kDown;
				HostWindowPenDown(startX, startY);
				if (x != startX || y != startY)
					HostWindowPenMove(x, y);
			}
		}
		else if (state == kDown && (x != lastX || y != lastY))
			HostWindowPenMove(x, y);
		lastX = x;
		lastY = y;
	}
	else if (state != kUp && ((valid && !fingerDown) || (now - lastSeen) / 1000 >= kTouchLostMs))
	{
		if (state == kHeld)
		{
			// (lifted before it showed what it was: a tap where it landed)
			HostWindowPenDown(startX, startY);
			HostWindowPenUp();
		}
		else if (state == kDown)
			HostWindowPenUp();
		state = kUp;
	}
	return state != kUp;
}


int
HostDeviceTouchKeyPressed(void)
{
	for (int key = 0; key < kTouchKeys; key++)
	{
		unsigned presses = gTouchKeyPresses[key].load();
		while (presses != 0)
			if (gTouchKeyPresses[key].compare_exchange_weak(presses, presses - 1))
				return key;
	}
	return -1;
}


void
HostDeviceTouchTrace(bool on)
{
	gTouchTrace = on;
}


bool
HostDeviceSheetPulled(void)
{
	unsigned pulls = gPulls.load();
	while (pulls != 0 && !gPulls.compare_exchange_weak(pulls, pulls - 1))
		;
	return pulls != 0;
}


void
HostDeviceSetLocked(bool locked)
{
	gLocked = locked;
	gpio_set_level((gpio_num_t) PIN_KEY_LED, locked || !gKeyLight ? 0 : 1);
	HostDeviceFrontlightLocked(locked);
}


void
HostDeviceSetKeyLight(bool on)
{
	gKeyLight = on;
	gpio_set_level((gpio_num_t) PIN_KEY_LED, gLocked || !on ? 0 : 1);
}


/*------------------------------------------------------------------------------
	Keys
------------------------------------------------------------------------------*/

// the owner's layout: [row][column], the columns as the keyboard is seen
// (the TCA8418 counts them from the right); 0 is a key with no character
static const char kBase[4][10] = {
	{'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'},
	{'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', '\b'},
	{ 0,  'z', 'x', 'c', 'v', 'b', 'n', 'm', '$', '\r'},
	{ 0,   0,   0,   0,   0,   0,   0,  ' ',  0,   0 },
};
static const char kSymbols[4][10] = {
	{'#', '1', '2', '3', '(', ')', '_',  '-', '+', '@'},
	{'*', '4', '5', '6', '/', ':', ';', '\'', '"', '\b'},
	{ 0,  '7', '8', '9', '?', '!', ',',  '.', '$', '\r'},
	{ 0,   0,   0,   0,   0,   0,  '0',  ' ',   0, 0 },
};
static const unsigned char kAlt[4][10] = {
	{kVKEscape, 0, kVKEnd, 0, 0, 0, kVKUp, 0, 0, kVKPageUp},
	{kVKHome, 0, kVKDown, 0, 0, 0, 0, 0, 0, 0},
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
};

// the modifier keys' numbers (an event's key, 0-39)
const int kKeyAlt = 29, kKeyRightShift = 30, kKeySym = 31, kKeyLeftShift = 34;


// a character as the US key that types it ==> its virtual key, and whether
// it is that key shifted
static long
VirtualKeyForCharacter(char c, bool* shifted)
{
	*shifted = false;
	if (c >= 'a' && c <= 'z')
		return c - 'a' + 'A';
	if (c >= '0' && c <= '9')
		return c;
	switch (c)
	{
	case ' ':	return kVKSpace;
	case '\b':	return kVKBack;
	case '\r':	return kVKReturn;
	case ',':	return 0xbc;
	case '.':	return 0xbe;
	case '/':	return 0xbf;
	case '-':	return 0xbd;
	case ';':	return 0xba;
	case '\'':	return 0xde;
	}
	*shifted = true;
	switch (c)
	{
	case '?':	return 0xbf;
	case '_':	return 0xbd;
	case '+':	return 0xbb;
	case ':':	return 0xba;
	case '"':	return 0xde;
	case '!':	return '1';
	case '@':	return '2';
	case '#':	return '3';
	case '$':	return '4';
	case '*':	return '8';
	case '(':	return '9';
	case ')':	return '0';
	}
	*shifted = false;
	return -1;
}


// what a key's press sent, so that its release undoes the same
struct KeySent
{
	long	virtualKey;			// -1: nothing
	bool	addedShift;
	bool	addedCommand;
};
static KeySent	gSent[40];
static bool		gShift = false, gSym = false, gAlt = false;


static void
KeyEvent(int key, bool press)
{
	if (gTouchTrace)
		printf("[key] %d %s\n", key, press ? "down" : "up");
	if (key == kKeyLeftShift || key == kKeyRightShift)
	{
		gShift = press;
		if (!(press && gLocked))
			HostWindowKey(kVKShift, press);
		return;
	}
	if (key == kKeySym)
	{
		gSym = press;
		return;
	}
	if (key == kKeyAlt)
	{
		gAlt = press;
		return;
	}
	KeySent& sent = gSent[key];
	if (press && gLocked)
	{
		// (left out - but a Newton asleep behind the lock screen woken: a
		// key no layout has, which wakes it and types nothing)
		HostWindowKey(kVKNone, 1);
		HostWindowKey(kVKNone, 0);
		sent.virtualKey = -1;
		return;
	}
	if (!press)
	{
		if (sent.virtualKey >= 0)
		{
			HostWindowKey(sent.virtualKey, 0);
			if (sent.addedShift)
				HostWindowKey(kVKShift, 0);
			if (sent.addedCommand)
				HostWindowKey(kVKCommand, 0);
		}
		sent.virtualKey = -1;
		return;
	}
	int row = key / 10, column = 9 - key % 10;
	sent.virtualKey = -1;
	sent.addedShift = sent.addedCommand = false;
	if (gAlt && kAlt[row][column] != 0)
		sent.virtualKey = kAlt[row][column];
	else
	{
		char c = gSym ? kSymbols[row][column] : kBase[row][column];
		if (c == 0)
			return;
		bool shifted;
		sent.virtualKey = VirtualKeyForCharacter(c, &shifted);
		if (sent.virtualKey < 0)
			return;
		sent.addedShift = shifted && !gShift;
		sent.addedCommand = gAlt;
	}
	if (sent.addedCommand)
		HostWindowKey(kVKCommand, 1);
	if (sent.addedShift)
		HostWindowKey(kVKShift, 1);
	HostWindowKey(sent.virtualKey, 1);
}


static void
KeyboardStart(void)
{
	for (int k = 0; k < 40; k++)
		gSent[k].virtualKey = -1;
	if (gBoardMax)
	{
		HostDeviceBoardSet(kLineKeyboardReset, false);
		vTaskDelay(pdMS_TO_TICKS(10));
		HostDeviceBoardSet(kLineKeyboardReset, true);
		vTaskDelay(pdMS_TO_TICKS(10));
	}
	static const uint8_t setup[][2] = {
		{ 0x1D, 0x0F },		// KP_GPIO1: rows 0-3
		{ 0x1E, 0xFF },		// KP_GPIO2: columns 0-7
		{ 0x1F, 0x03 },		// KP_GPIO3: columns 8-9
		{ 0x01, 0x01 },		// CFG: key events
	};
	for (int k = 0; k < 4; k++)
		Write(ADDR_KEYBOARD, setup[k], 2);
	// whatever is in the FIFO, thrown away
	uint8_t reg = 0x03, count = 0, event;
	for (int k = 0; k < 16 && ReadRegister(ADDR_KEYBOARD, &reg, 1, &count, 1) == ESP_OK && (count & 0x0F) != 0; k++)
	{
		uint8_t fifo = 0x04;
		ReadRegister(ADDR_KEYBOARD, &fifo, 1, &event, 1);
	}
	gpio_set_direction((gpio_num_t) PIN_KEY_LED, GPIO_MODE_OUTPUT);
	gpio_set_level((gpio_num_t) PIN_KEY_LED, 1);
}


// the events in the FIFO (their count in KEY_LCK_EC's low nibble): 0x80 for
// a press, then the key's number + 1
static void
KeyboardPoll(void)
{
	uint8_t reg = 0x03, count = 0;
	if (ReadRegister(ADDR_KEYBOARD, &reg, 1, &count, 1) != ESP_OK || (count & 0x0F) == 0)
		return;
	for (int n = count & 0x0F; n > 0; n--)
	{
		uint8_t fifo = 0x04, event = 0;
		if (ReadRegister(ADDR_KEYBOARD, &fifo, 1, &event, 1) != ESP_OK || event == 0)
			break;
		int key = (event & 0x7F) - 1;
		if (key >= 0 && key < 40)
			KeyEvent(key, (event & 0x80) != 0);
	}
	static const uint8_t clear[2] = { 0x02, 0x01 };	// INT_STAT: the key events seen
	Write(ADDR_KEYBOARD, clear, 2);
}


/*------------------------------------------------------------------------------
	The task
------------------------------------------------------------------------------*/

static TaskHandle_t				gInputTask = NULL;
static bool						gInterrupts = false;
static volatile unsigned long	gTouchInterrupts = 0, gKeyInterrupts = 0, gButtonPresses = 0;


// A chip's INT low (or the BOOT button pressed): the interrupt disarmed until
// the task has read it
static void
Interrupt(void* pin)
{
	gpio_intr_disable((gpio_num_t) (intptr_t) pin);
	if ((intptr_t) pin == PIN_TOUCH_INT)
		gTouchInterrupts++;
	else if ((intptr_t) pin == PIN_KEY_INT)
		gKeyInterrupts++;
	else
		gButtonPresses++;
	BaseType_t woken = pdFALSE;
	vTaskNotifyGiveFromISR(gInputTask, &woken);
	portYIELD_FROM_ISR(woken);
}


static bool
InterruptsStart(void)
{
	esp_err_t err = gpio_install_isr_service(0);
	if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
		return false;
	gpio_set_direction((gpio_num_t) PIN_KEY_INT, GPIO_MODE_INPUT);
	gpio_set_pull_mode((gpio_num_t) PIN_KEY_INT, GPIO_PULLUP_ONLY);
	gpio_set_direction((gpio_num_t) PIN_BOOT_BUTTON, GPIO_MODE_INPUT);
	gpio_set_pull_mode((gpio_num_t) PIN_BOOT_BUTTON, GPIO_PULLUP_ONLY);
	static const int pins[3] = { PIN_TOUCH_INT, PIN_KEY_INT, PIN_BOOT_BUTTON };
	for (int pin : pins)
	{
		gpio_intr_disable((gpio_num_t) pin);
		if (gpio_wakeup_enable((gpio_num_t) pin, GPIO_INTR_LOW_LEVEL) != ESP_OK
		 || gpio_isr_handler_add((gpio_num_t) pin, Interrupt, (void*) (intptr_t) pin) != ESP_OK)
			return false;
	}
	esp_sleep_enable_gpio_wakeup();
	return true;
}


void
HostDeviceInputDump(void)
{
	printf("newton: input %s, interrupts: touch %lu, keyboard %lu, BOOT button %lu\n", gInterrupts ? "waits for interrupts" : "polled",
		   (unsigned long) gTouchInterrupts, (unsigned long) gKeyInterrupts, (unsigned long) gButtonPresses);
	uint8_t reg, config = 0, status = 0, events = 0;
	reg = 0x01; ReadRegister(ADDR_KEYBOARD, &reg, 1, &config, 1);
	reg = 0x02; ReadRegister(ADDR_KEYBOARD, &reg, 1, &status, 1);
	reg = 0x03; ReadRegister(ADDR_KEYBOARD, &reg, 1, &events, 1);
	printf("newton: lines: touch INT %d, keyboard INT %d, BOOT %d; keyboard CFG %02X INT_STAT %02X events %02X\n",
		   gpio_get_level((gpio_num_t) PIN_TOUCH_INT), gpio_get_level((gpio_num_t) PIN_KEY_INT), gpio_get_level((gpio_num_t) PIN_BOOT_BUTTON),
		   config, status, events);
}


// The BOOT button: the Newton's power switch (host/HostKeyboard.cpp's F12 -
// asleep, it wakes the machine; awake, the power manager puts it to sleep,
// the screen locked first).  A press is taken once, and the button looked
// at again every 50 ms until it is let go ==> whether it is still down.
static bool
ButtonPoll(void)
{
	static bool down = false;
	bool now = gpio_get_level((gpio_num_t) PIN_BOOT_BUTTON) == 0;
	if (now && !down)
	{
		HostWindowKey(kVKPowerSwitch, 1);
		HostWindowKey(kVKPowerSwitch, 0);
	}
	down = now;
	return down;
}


static void
InputTask(void*)
{
	TouchStart();
	KeyboardStart();
	gInterrupts = InterruptsStart();
	if (!gInterrupts)
		printf("newton: no interrupts for the touch panel and keyboard - polled\n");
	for (;;)
	{
		bool touching = TouchPoll();
		KeyboardPoll();
		if (!gInterrupts)
		{
			vTaskDelay(pdMS_TO_TICKS(10));
			continue;
		}
		bool button = ButtonPoll();
		// (a finger down: read again in 10 ms, its moves and its lifting;
		// the button held: in 50 ms; else woken by either chip or the button)
		gpio_intr_enable((gpio_num_t) PIN_KEY_INT);
		if (!touching)
			gpio_intr_enable((gpio_num_t) PIN_TOUCH_INT);
		if (!button)
			gpio_intr_enable((gpio_num_t) PIN_BOOT_BUTTON);
		// (traced, the chips are looked at whether they say so or not, so
		// that a report that comes with no interrupt shows)
		ulTaskNotifyTake(pdTRUE, touching ? pdMS_TO_TICKS(10) : gTouchTrace ? pdMS_TO_TICKS(20) : button ? pdMS_TO_TICKS(50) : portMAX_DELAY);
	}
}


void
HostDeviceInputStart(void)
{
	const size_t kStack = 4 * 1024;
	StackType_t* stack = (StackType_t*) heap_caps_malloc(kStack, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	StaticTask_t* control = (StaticTask_t*) heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
	if (stack == NULL || control == NULL
	|| (gInputTask = xTaskCreateStaticPinnedToCore(InputTask, "input", kStack, NULL, 4, stack, control, 0)) == NULL)
		printf("newton: no room for the input task\n");
}
