/*
	File:		host/esp32/main/tdeck_panel.cpp

	Contains:	The T-Deck Pro's e-paper panel showing the Newton's display
				(src/host/esp32/README.md): a task of the board's own, on
				the core the Newton does not use, looks at the display's
				grays (host/HostWindow.h's form: one byte a pixel, 0 white
				.. 255 black) a twentieth of a second at a time and, once
				a change has settled, sends the picture to the panel.

				The panel is Good Display's GDEQ031T10 with a UC8253, driven
				as Good Display's example and GxEPD2's driver for it do
				(GxEPD2_310_GDEQ031T10 - the register values only):
				  - black and white with the waveforms in the controller's
				    OTP (PSR 0x1F 0x0D);
				  - the controller reset (PSR's RST_N - its reset line is
				    not wired) and set up again before every refresh: it
				    keeps a state of its own across refreshes, and without
				    the reset a second partial refresh did nothing;
				  - a full refresh: the picture in both DTM1 and DTM2, CDI
				    0x97 (the full waveform darkens a pixel black in
				    either);
				  - a partial one: the picture before in DTM1 and the new
				    one in DTM2, CDI 0xD7, the OTP waveform at the forced
				    temperature 0x79 (0.65 s), only over the rectangle that
				    changed (PTIN/PTL - the waveform drives every pixel it
				    covers).
				Found by photographing a test pattern with a webcam
				(tools/tdeck/panelprobe.py: 28 set-ups tried, one right),
				after a day of the device spike's set-up (PSR 0x8D, no
				reset) ghosting, inverting and not updating -
				docs/host-tdeck-pro.md.  Never deep sleep: without the reset
				line only a power cycle wakes it.

				Which refresh:
				  - every change: partial, a new page too (a partial
				    refresh over the whole screen takes no longer than
				    one over a word - the waveform's time, 0.65 s - and
				    came out clean, 2026-10-13);
				  - full (clears the ghosts; 3 s, and it flashes): the
				    first picture, a change of more than "bigChange" of
				    the screen (100: none), and a clean-up after partial
				    ones - once the screen has been still for
				    "cleanAfter", or, after "cleanCount" of them, at the
				    first pause of "cleanPause" (so that a long run of
				    typing or scrolling is cleaned up too, but never in
				    the middle of it).  Either clean-up only once the
				    partial ones since the last full one have between
				    them redrawn "cleanArea" of the screen: a clock's
				    minute alone is never cleaned up, so a screen nobody
				    touches is not flashed every minute (the owner's ask,
				    2026-10-13: the battery);
				  - two partial ones over a window, to it inverted and
				    back (1.3 s; CleanArea): the clean-up of less than
				    that - after "localClean" partial ones, at the first
				    pause of "cleanPause", over the rectangle they redrew
				    between them (a clock's digits every so often, not
				    the screen).  Not a full refresh over the window:
				    that greys the white outside it (CleanArea says how
				    it was seen).
				The settings can be changed while it runs
				(HostDevicePanelSet, from NewtonScript DevicePanelSet(name,
				value)); the set-up's own ones are kept so that others can
				be tried on another panel.  A refresh the controller never
				finishes resets it.
				ESP-IDF's headers only (esp32host.h says why).
*/

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

#include "esp32host.h"

// pins (LilyGO's schematic): the SPI bus is shared with the LoRa radio
// (tdeck_lora.cpp) and the SD card, kept deselected
#define PIN_SCK		36
#define PIN_MOSI	33
#define PIN_MISO	47		// (the radio's: the panel only writes)
#define PIN_CS		34
#define PIN_DC		35
#define PIN_BUSY	37		// low while busy
#define PIN_LORA_CS	3
#define PIN_SD_CS	48

// UC8253 commands
#define PSR		0x00
#define POF		0x02
#define PON		0x04
#define DTM1	0x10		// the picture before
#define DRF		0x12
#define DTM2	0x13		// the new picture
#define PTL		0x90		// the partial window: HRST, HRED, VRST (2), VRED (2), PT_SCAN
#define PTIN	0x91		// into partial mode: the data and the refresh are the window's
#define PTOUT	0x92
#define CDI		0x50
#define TRES	0x61
#define CCSET	0xE0
#define TSSET	0xE5

const long		kPanelWidth = 240;
const long		kPanelHeight = 320;
const long		kRowBytes = kPanelWidth / 8;
const long		kFrameBytes = kRowBytes * kPanelHeight;

// the settings (HostDevicePanelSet)
static volatile long	gPartialTemperature = 0x79;	// "partialTemperature": TSSET for a partial refresh; 0: every refresh full
static volatile long	gFullTemperature = 0;		// "fullTemperature": TSSET for a full one (0x5A: about a second); 0: the OTP's own (3 s)
static volatile long	gBigChangePercent = 100;	// "bigChange": a change of more of the screen than this is drawn full (100: none)
static volatile long	gCleanAfterMs = 30000;		// "cleanAfter": partial refreshes cleaned up once the screen has been still this long (0: never)
static volatile long	gCleanCount = 30;			// "cleanCount": ... or after this many of them (0: never) ...
static volatile long	gCleanPauseMs = 2000;		// "cleanPause": ... once the screen has been still this long
static volatile long	gCleanAreaPercent = 5;		// "cleanArea": ... and only once the partial ones have redrawn this much of the screen between them (0: any)
static volatile long	gLocalCleanCount = 10;		// "localClean": less than that cleaned up over its rectangle after this many partial ones, at the first pause (0: never)
static volatile long	gTrace = 0;					// "trace": each refresh printed with its time
// (the set-up, to compare others on the panel while it runs - panelprobe.py)
static volatile long	gPSR2 = 0x0D;				// "psr2": PSR's second byte (the device spike had 0x8D)
static volatile long	gCDIPartial = 0xD7;			// "cdiPartial": CDI for a partial refresh (-1: not sent)
static volatile long	gCDIFull = 0x97;			// "cdiFull": CDI for a full one (-1: not sent)
static volatile long	gResetEach = 1;				// "resetEach": the controller reset and set up before each refresh
static volatile long	gOldAfter = 0;				// "oldAfter": 1 a partial refresh sends the new picture to DTM2 before it and to DTM1 after it (GxEPD2's order), rather than both before
static volatile long	gInvertOld = 0;				// "invertOld": the picture before sent inverted
static volatile long	gSwap = 0;					// "swap": the new picture to DTM1 and the one before to DTM2
static volatile long	gWindow = 1;				// "window": a partial refresh only over the rectangle that changed
static volatile long	gResetAsked = 0;			// "reset": the controller reset and set up again, once
static volatile long	gRedraw = 0;				// "redraw": 1 the screen sent again by partial refreshes (via white), 2 by full ones
static volatile long	gTest = 0;					// "test": the test pattern (TestPattern; 3 its last step CleanArea, 2 a full refresh over a window), then the Newton's screen again
const int64_t	kSettleMs = 60;					// a change unchanged this long is shown
const int64_t	kSettleAtMostMs = 400;			// ... and one still changing after this long

static spi_device_handle_t	gSpi;
static uint8_t*				gBounce;			// (DMA reads it: internal RAM)

// the display as it is now (its size changes when the Newton turns it -
// hal/host/HostScreen.h: the same grays, Width() per row)
static void					(*gDisplayNow)(const unsigned char** grays, long* width, long* height) = NULL;
static const unsigned char* volatile	gGrays = NULL;
static volatile long		gGraysWidth, gGraysHeight;

// the task waits for the display to change (HostDevicePanelChanged, which the
// Newton's side calls from hal/host/HostScreen.h's gHostScreenChanged) or for
// a setting, and looks every 50 ms only while a change is still to be shown
// or its ghosts cleared - not every 50 ms whatever, which kept the chip
// awake 11% of the time converting and comparing the same screen
static TaskHandle_t			gPanelTask = NULL;
static volatile uint32_t	gChanges = 0;		// one more each time the grays change


static bool	gInvertSending = false;

static void
Send(uint8_t command, const uint8_t* data, size_t length)
{
	spi_transaction_t t;
	memset(&t, 0, sizeof(t));
	gpio_set_level((gpio_num_t) PIN_DC, 0);
	t.length = 8;
	t.tx_buffer = &command;
	spi_device_polling_transmit(gSpi, &t);
	gpio_set_level((gpio_num_t) PIN_DC, 1);
	// every byte through a buffer DMA can read (the frames are in PSRAM)
	while (length > 0)
	{
		size_t n = length > 4000 ? 4000 : length;
		memcpy(gBounce, data, n);
		if (gInvertSending)
			for (size_t k = 0; k < n; k++)
				gBounce[k] = (uint8_t) ~gBounce[k];
		memset(&t, 0, sizeof(t));
		t.length = n * 8;
		t.tx_buffer = gBounce;
		spi_device_polling_transmit(gSpi, &t);
		data += n;
		length -= n;
	}
}


static void
Send1(uint8_t command, uint8_t value)
{
	Send(command, &value, 1);
}


// until BUSY goes high ==> whether it did
static bool
WaitBusy(int timeoutMs)
{
	int64_t start = esp_timer_get_time();
	while (gpio_get_level((gpio_num_t) PIN_BUSY) == 0)
	{
		if (esp_timer_get_time() - start > (int64_t) timeoutMs * 1000)
			return false;
		vTaskDelay(1);
	}
	return true;
}


// the controller reset (PSR's RST_N clear: its reset line is not wired),
// set up and powered on ==> whether it answered
static bool
PanelReset(void)
{
	uint8_t reset[2] = { 0x1E, (uint8_t) gPSR2 };
	Send(PSR, reset, 2);
	vTaskDelay(pdMS_TO_TICKS(10));
	if (!WaitBusy(2000))
		return false;
	uint8_t psr[2] = { 0x1F, (uint8_t) gPSR2 };		// black and white, the OTP's waveforms
	Send(PSR, psr, 2);
	uint8_t tres[3] = { (uint8_t) (kPanelWidth & 0xF8), (uint8_t) (kPanelHeight >> 8), (uint8_t) (kPanelHeight & 0xFF) };
	Send(TRES, tres, 3);
	Send(PON, NULL, 0);
	return WaitBusy(2000);
}


// The SPI bus the panel, the LoRa radio and the SD card share (SPI2), every
// chip select parked high first, as LilyGO's examples do: started once, by
// app_main before the panel's task and the radio's start, each of which adds
// its own device (==> whether it is up)
bool
HostDeviceSpiBusStart(void)
{
	static bool started = false;
	if (started)
		return true;
	const int selects[] = { PIN_CS, PIN_LORA_CS, PIN_SD_CS };
	for (int pin : selects)
	{
		gpio_set_direction((gpio_num_t) pin, GPIO_MODE_OUTPUT);
		gpio_set_level((gpio_num_t) pin, 1);
	}
	spi_bus_config_t bus;
	memset(&bus, 0, sizeof(bus));
	bus.mosi_io_num = PIN_MOSI;
	bus.miso_io_num = PIN_MISO;
	bus.sclk_io_num = PIN_SCK;
	bus.quadwp_io_num = -1;
	bus.quadhd_io_num = -1;
	bus.max_transfer_sz = 4096;
	started = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) == ESP_OK;
	if (!started)
		printf("newton: the SPI bus would not start\n");
	return started;
}


static bool
PanelInit(void)
{
	gpio_set_direction((gpio_num_t) PIN_DC, GPIO_MODE_OUTPUT);
	gpio_set_direction((gpio_num_t) PIN_BUSY, GPIO_MODE_INPUT);
	gpio_set_pull_mode((gpio_num_t) PIN_BUSY, GPIO_PULLUP_ONLY);

	gBounce = (uint8_t*) heap_caps_malloc(4000, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
	if (gBounce == NULL)
		return false;
	if (!HostDeviceSpiBusStart())
		return false;
	spi_device_interface_config_t dev;
	memset(&dev, 0, sizeof(dev));
	dev.clock_speed_hz = 10 * 1000 * 1000;
	dev.mode = 0;
	dev.spics_io_num = PIN_CS;
	dev.queue_size = 1;
	if (spi_bus_add_device(SPI2_HOST, &dev, &gSpi) != ESP_OK)
		return false;
	return PanelReset();
}


// A rectangle of the panel: rows, and columns in bytes (the controller's
// window takes x in whole bytes)
struct PanelArea
{
	long	top, bottom, left, right;
};


// area: a full refresh over that window only (the full waveform, so its
// ghosts cleared, and the rest of the panel left alone - neither flashed
// nor driven)
static void
Refresh(const uint8_t* before, const uint8_t* picture, bool partial, const PanelArea* area = NULL)
{
	// a partial refresh over the bytes that changed only: their rows and
	// byte columns, as a window (x to whole bytes, as the controller takes it)
	long top = 0, bottom = kPanelHeight - 1, left = 0, right = kRowBytes - 1;
	bool window = false;
	if (!partial && area != NULL)
	{
		top = area->top;
		bottom = area->bottom;
		left = area->left;
		right = area->right;
		window = true;
	}
	else if (partial && gWindow != 0)
	{
		top = kPanelHeight;
		bottom = -1;
		left = kRowBytes;
		right = -1;
		for (long y = 0; y < kPanelHeight; y++)
			for (long b = 0; b < kRowBytes; b++)
				if (before[y * kRowBytes + b] != picture[y * kRowBytes + b])
				{
					if (y < top) top = y;
					if (y > bottom) bottom = y;
					if (b < left) left = b;
					if (b > right) right = b;
				}
		if (bottom < 0)
			return;							// (nothing changed)
		window = true;
	}
	if (gResetEach != 0 && !PanelReset())
		printf("newton: the panel did not answer its reset\n");
	long temperature = partial ? gPartialTemperature : gFullTemperature;
	if (temperature != 0)
	{
		Send1(CCSET, 0x02);					// TSFIX: the temperature below
		Send1(TSSET, (uint8_t) temperature);
	}
	else
		Send1(CCSET, 0x00);
	long cdi = partial ? gCDIPartial : gCDIFull;
	if (cdi >= 0)
		Send1(CDI, (uint8_t) cdi);
	const uint8_t* first = gSwap ? picture : before;
	const uint8_t* second = gSwap ? before : picture;
	if (window)
	{
		static uint8_t* part = NULL;
		if (part == NULL)
			part = (uint8_t*) heap_caps_malloc(kFrameBytes, MALLOC_CAP_SPIRAM);
		long x = left * 8, x2 = right * 8 + 7;
		uint8_t ptl[7] = { (uint8_t) x, (uint8_t) x2, (uint8_t) (top >> 8), (uint8_t) top,
						   (uint8_t) (bottom >> 8), (uint8_t) bottom, 0x01 };
		Send(PTIN, NULL, 0);
		Send(PTL, ptl, 7);
		long rowBytes = right - left + 1, rows = bottom - top + 1;
		for (int pass = partial && gOldAfter != 0 ? 1 : 0; pass < 2; pass++)
		{
			// (a full one: the picture in both, as below)
			const uint8_t* frame = !partial ? picture : pass == 0 ? first : second;
			for (long r = 0; r < rows; r++)
				memcpy(part + r * rowBytes, frame + (top + r) * kRowBytes + left, rowBytes);
			gInvertSending = partial && gInvertOld != 0 && (pass == 0) == (gSwap == 0);
			Send(pass == 0 ? DTM1 : DTM2, part, (size_t) (rowBytes * rows));
		}
		gInvertSending = false;
	}
	else if (!partial)
	{
		// a full refresh: the picture in both (the full waveform darkens a
		// pixel black in either - the picture before, sent inverted as a
		// partial one wants it, came out over the new one, 2026-10-11; Good
		// Display's and GxEPD2's full refresh write the image to both too)
		Send(DTM1, picture, kFrameBytes);
		Send(DTM2, picture, kFrameBytes);
	}
	else
	{
		gInvertSending = gInvertOld != 0 && gSwap == 0;
		if (gOldAfter == 0)
			Send(DTM1, first, kFrameBytes);
		gInvertSending = gInvertOld != 0 && gSwap != 0;
		Send(DTM2, second, kFrameBytes);
		gInvertSending = false;
	}
	int64_t started = esp_timer_get_time();
	Send(DRF, NULL, 0);
	vTaskDelay(1);
	const char* kind = partial ? "partial" : "full";
	if (!WaitBusy(15000))
	{
		printf("newton: the panel stayed busy after a %s refresh - reset\n", kind);
		if (!PanelReset())
			printf("newton: the panel did not answer its reset\n");
	}
	else if (gTrace)
		printf("newton: panel %s refresh %lld ms (%ld x %ld at %ld, %ld)\n", kind, (long long) ((esp_timer_get_time() - started) / 1000),
			   (right - left + 1) * 8, bottom - top + 1, left * 8, top);
	if (partial && gOldAfter != 0)
	{
		// the new picture into DTM1 too, the next refresh's picture before
		if (window)
		{
			static uint8_t* part = NULL;
			if (part == NULL)
				part = (uint8_t*) heap_caps_malloc(kFrameBytes, MALLOC_CAP_SPIRAM);
			long rowBytes = right - left + 1, rows = bottom - top + 1;
			for (long r = 0; r < rows; r++)
				memcpy(part + r * rowBytes, picture + (top + r) * kRowBytes + left, rowBytes);
			Send(DTM1, part, (size_t) (rowBytes * rows));
		}
		else
			Send(DTM1, picture, kFrameBytes);
	}
	if (window)
		Send(PTOUT, NULL, 0);
}


// the display's grays as the panel's bits: a bit set is white, gray from
// half black is black; a display of another size is cut to the panel's
// (or left white past its edge).  A display wider than it is tall - the
// Newton turned to landscape (Internet Setup, for one, will not open at
// 240x320 upright) - is drawn a quarter turn round: the display's top at
// the panel's left edge, so the board is held turned clockwise, its
// keyboard on the left (HostDeviceDisplayTurned, which the touch panel
// turns back by; seen with tools/tdeck/panelshot.py, 2026-10-11)
static void
Convert(uint8_t* frame)
{
	if (gDisplayNow != NULL)
	{
		const unsigned char* g;
		long w, h;
		gDisplayNow(&g, &w, &h);
		gGraysWidth = w;
		gGraysHeight = h;
		gGrays = g;
	}
	const unsigned char* grays = gGrays;
	long width = gGraysWidth, height = gGraysHeight;
	memset(frame, 0xFF, kFrameBytes);
	if (width > height)
	{
		// panel (px, py) shows display (319 - py, px)
		for (long py = 0; py < kPanelHeight; py++)
		{
			long dx = kPanelHeight - 1 - py;
			if (dx >= width)
				continue;
			uint8_t* out = frame + py * kRowBytes;
			for (long px = 0; px < kPanelWidth && px < height; px++)
				if (grays[px * width + dx] >= 128)
					out[px >> 3] &= (uint8_t) ~(0x80 >> (px & 7));
		}
		return;
	}
	for (long y = 0; y < kPanelHeight && y < height; y++)
	{
		const unsigned char* row = grays + y * width;
		uint8_t* out = frame + y * kRowBytes;
		for (long x = 0; x < kPanelWidth && x < width; x++)
			if (row[x] >= 128)
				out[x >> 3] &= (uint8_t) ~(0x80 >> (x & 7));
	}
}


// The ghosts over area cleared without touching the rest of the panel: two
// partial refreshes over it, to the picture inverted and back, so that every
// pixel in it is driven through black and white (1.3 s).  A full refresh over
// a window - the full waveform - clears them too, but its common electrode
// is the whole panel's: the white outside the window came out grey (the
// webcam, 2026-10-13: 160 to 135 outside, 153 inside as before)
static void
CleanArea(const uint8_t* shown, const PanelArea& area)
{
	static uint8_t* flipped = NULL;
	if (flipped == NULL)
		flipped = (uint8_t*) heap_caps_malloc(kFrameBytes, MALLOC_CAP_SPIRAM);
	memcpy(flipped, shown, kFrameBytes);
	for (long y = area.top; y <= area.bottom; y++)
		for (long b = area.left; b <= area.right; b++)
			flipped[y * kRowBytes + b] = (uint8_t) ~flipped[y * kRowBytes + b];
	Refresh(shown, flipped, true);
	Refresh(flipped, shown, true);
}


// A pattern whose right result is easy to tell (the settings above
// compared on the panel): (1) a full refresh: the left half black, the right
// white; (2) after three seconds a partial one over the middle (160 x 160 at
// 40, 80), the top half of that square flipped and its bottom half as it
// was; (3) after three more, a partial one back to (1).  Then the Newton's
// screen again, by a full refresh.  shown: the Newton's picture, which the
// last refresh goes back to.  clean: (3) is instead (2) cleaned up over the
// square only (1: CleanArea's flip and back, 2: a full refresh over it) -
// the panel outside the square must stay as it was (camshot.py --every).
static void
TestPattern(uint8_t* shown, int clean)
{
	static uint8_t* a = NULL;
	static uint8_t* b = NULL;
	if (a == NULL)
	{
		a = (uint8_t*) heap_caps_malloc(kFrameBytes, MALLOC_CAP_SPIRAM);
		b = (uint8_t*) heap_caps_malloc(kFrameBytes, MALLOC_CAP_SPIRAM);
	}
	for (long y = 0; y < kPanelHeight; y++)
		for (long x = 0; x < kRowBytes; x++)
			a[y * kRowBytes + x] = x < kRowBytes / 2 ? 0x00 : 0xFF;
	memcpy(b, a, kFrameBytes);
	for (long y = 80; y < 160; y++)
		for (long x = 40 / 8; x < 200 / 8; x++)
			b[y * kRowBytes + x] = (uint8_t) ~a[y * kRowBytes + x];
	// (the bottom half of the square, rows 160-239, unchanged - but inside
	//  the window: the window is the rectangle of what changed, so it is
	//  made to cover them by one changed pixel at its bottom corner)
	b[239 * kRowBytes + 40 / 8] ^= 0x80;
	printf("newton: test (1) full: left black, right white\n");
	Refresh(shown, a, false);
	vTaskDelay(pdMS_TO_TICKS(3000));
	printf("newton: test (2) partial: the middle square's top half flipped, its bottom half as it was\n");
	Refresh(a, b, true);
	vTaskDelay(pdMS_TO_TICKS(3000));
	PanelArea square = { 80, 239, 40 / 8, 200 / 8 - 1 };
	if (clean == 1)
	{
		printf("newton: test (3) the square flipped and back: (2) again\n");
		CleanArea(b, square);
	}
	else if (clean == 2)
	{
		printf("newton: test (3) full over the square: (2) again, only the square flashing\n");
		Refresh(b, b, false, &square);
	}
	else
	{
		printf("newton: test (3) partial: back to (1)\n");
		Refresh(b, a, true);
	}
	vTaskDelay(pdMS_TO_TICKS(3000));
	printf("newton: test done: the Newton's screen again\n");
	Refresh(clean != 0 ? b : a, shown, false);
}


static const uint8_t*	gShown = NULL;		// what the panel shows (HostDevicePanelDump)

static void
PanelTask(void*)
{
	if (!PanelInit())
	{
		printf("newton: the panel did not answer - nothing will be shown\n");
		vTaskSuspend(NULL);
	}
	uint8_t* shown = (uint8_t*) heap_caps_malloc(kFrameBytes, MALLOC_CAP_SPIRAM);
	gShown = shown;
	uint8_t* pending = (uint8_t*) heap_caps_malloc(kFrameBytes, MALLOC_CAP_SPIRAM);
	uint8_t* now = (uint8_t*) heap_caps_malloc(kFrameBytes, MALLOC_CAP_SPIRAM);
	uint8_t* white = (uint8_t*) heap_caps_malloc(kFrameBytes, MALLOC_CAP_SPIRAM);
	memset(white, 0xFF, kFrameBytes);
	// the bytes partial refreshes have redrawn since the last full one
	uint8_t* touched = (uint8_t*) heap_caps_calloc(kFrameBytes, 1, MALLOC_CAP_SPIRAM);
	long touchedCount = 0;
	// whatever the last run left, cleared
	memcpy(shown, white, kFrameBytes);
	Refresh(shown, shown, false);
	memcpy(pending, shown, kFrameBytes);
	bool first = true;
	int partialsSinceFull = 0;
	int64_t changedAt = 0, pendingSince = 0, shownAt = esp_timer_get_time();
	uint32_t converted = gChanges - 1;			// (the first look converts)
	TickType_t wait = pdMS_TO_TICKS(50);
	for (;;)
	{
		ulTaskNotifyTake(pdTRUE, wait);
		wait = pdMS_TO_TICKS(50);				// (each way round that goes on below)
		if (gGrays == NULL)
			continue;
		uint32_t changes = gChanges;
		if (changes != converted)
		{
			converted = changes;
			Convert(now);
		}
		int64_t t = esp_timer_get_time();
		if (gTest != 0)
		{
			int clean = gTest == 3 ? 1 : gTest == 2 ? 2 : 0;
			gTest = 0;
			TestPattern(shown, clean);
			memcpy(pending, shown, kFrameBytes);
			continue;
		}
		if (gResetAsked != 0)
		{
			gResetAsked = 0;
			if (!PanelReset())
				printf("newton: the panel did not answer its reset\n");
		}
		if (gRedraw != 0)
		{
			bool partial = gRedraw == 1;
			gRedraw = 0;
			Refresh(shown, white, partial);
			Refresh(white, now, partial);
			memcpy(shown, now, kFrameBytes);
			memcpy(pending, now, kFrameBytes);
			partialsSinceFull = partial ? 2 : 0;
			memset(touched, partial ? 1 : 0, kFrameBytes);
			touchedCount = partial ? kFrameBytes : 0;
			shownAt = esp_timer_get_time();
			continue;
		}
		if (memcmp(now, pending, kFrameBytes) != 0)
		{
			if (memcmp(pending, shown, kFrameBytes) == 0)
				pendingSince = t;
			memcpy(pending, now, kFrameBytes);
			changedAt = t;
		}
		if (memcmp(pending, shown, kFrameBytes) != 0)
		{
			if ((t - changedAt) / 1000 < kSettleMs && (t - pendingSince) / 1000 < kSettleAtMostMs)
				continue;
			long changed = 0;
			for (long k = 0; k < kFrameBytes; k++)
				changed += shown[k] != pending[k];
			bool partial = !first && gPartialTemperature != 0 && changed * 100 < kFrameBytes * gBigChangePercent;
			if (!partial)
			{
				memset(touched, 0, kFrameBytes);
				touchedCount = 0;
			}
			else
				for (long k = 0; k < kFrameBytes; k++)
					if (shown[k] != pending[k] && !touched[k])
					{
						touched[k] = 1;
						touchedCount++;
					}
			Refresh(shown, pending, partial);
			memcpy(shown, pending, kFrameBytes);
			first = false;
			partialsSinceFull = partial ? partialsSinceFull + 1 : 0;
			shownAt = esp_timer_get_time();
		}
		else if (partialsSinceFull > 0 && touchedCount * 100 >= kFrameBytes * gCleanAreaPercent
				 && ((gCleanAfterMs > 0 && (t - shownAt) / 1000 >= gCleanAfterMs)
					 || (gCleanCount > 0 && partialsSinceFull >= gCleanCount && (t - shownAt) / 1000 >= gCleanPauseMs)))
		{
			// the screen still: the ghosts of the partial refreshes cleared
			Refresh(shown, shown, false);
			partialsSinceFull = 0;
			memset(touched, 0, kFrameBytes);
			touchedCount = 0;
			shownAt = esp_timer_get_time();
		}
		else if (partialsSinceFull > 0 && touchedCount * 100 < kFrameBytes * gCleanAreaPercent
				 && gLocalCleanCount > 0 && partialsSinceFull >= gLocalCleanCount && (t - shownAt) / 1000 >= gCleanPauseMs)
		{
			// a little redrawn many times (a clock): its ghosts cleared over
			// the rectangle it was redrawn in, the rest of the screen left
			PanelArea area = { kPanelHeight, -1, kRowBytes, -1 };
			for (long y = 0; y < kPanelHeight; y++)
				for (long b = 0; b < kRowBytes; b++)
					if (touched[y * kRowBytes + b])
					{
						if (y < area.top) area.top = y;
						if (y > area.bottom) area.bottom = y;
						if (b < area.left) area.left = b;
						if (b > area.right) area.right = b;
					}
			if (area.bottom >= 0)
				CleanArea(shown, area);
			partialsSinceFull = 0;
			memset(touched, 0, kFrameBytes);
			touchedCount = 0;
			shownAt = esp_timer_get_time();
		}
		// the next look: at once on a change or a setting; in 50 ms while a
		// change is still to be shown; when the ghosts' clearing above is
		// due (the screen still that long); else not until a change
		if (memcmp(pending, shown, kFrameBytes) == 0)
		{
			long due = -1;						// ms after shownAt
			bool big = touchedCount * 100 >= kFrameBytes * gCleanAreaPercent;
			if (partialsSinceFull > 0 && big && gCleanAfterMs > 0)
				due = gCleanAfterMs;
			if (partialsSinceFull > 0 && big && gCleanCount > 0 && partialsSinceFull >= gCleanCount && (due < 0 || gCleanPauseMs < due))
				due = gCleanPauseMs;
			if (partialsSinceFull > 0 && !big && gLocalCleanCount > 0 && partialsSinceFull >= gLocalCleanCount)
				due = gCleanPauseMs;
			if (due < 0)
				wait = portMAX_DELAY;
			else
			{
				int64_t left = due - (esp_timer_get_time() - shownAt) / 1000;
				wait = pdMS_TO_TICKS(left > 0 ? left : 0) + 1;
			}
		}
	}
}


void
HostDevicePanelStart(void)
{
	// (its stack in PSRAM, its control block in internal RAM; never ends)
	const size_t kStack = 6 * 1024;
	StackType_t* stack = (StackType_t*) heap_caps_malloc(kStack, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	StaticTask_t* control = (StaticTask_t*) heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
	if (stack == NULL || control == NULL
	|| (gPanelTask = xTaskCreateStaticPinnedToCore(PanelTask, "panel", kStack, NULL, 3, stack, control, 0)) == NULL)
		printf("newton: no room for the panel's task\n");
}


void
HostDevicePanelChanged(void)
{
	gChanges++;
	if (gPanelTask != NULL)
		xTaskNotifyGive(gPanelTask);
}


bool
HostDevicePanelSet(const char* name, long value)
{
	if (strcasecmp(name, "psr2") == 0)
		gPSR2 = value;
	else if (strcasecmp(name, "cdiPartial") == 0)
		gCDIPartial = value;
	else if (strcasecmp(name, "cdiFull") == 0)
		gCDIFull = value;
	else if (strcasecmp(name, "resetEach") == 0)
		gResetEach = value;
	else if (strcasecmp(name, "oldAfter") == 0)
		gOldAfter = value;
	else if (strcasecmp(name, "invertOld") == 0)
		gInvertOld = value;
	else if (strcasecmp(name, "swap") == 0)
		gSwap = value;
	else if (strcasecmp(name, "test") == 0)
		gTest = value;
	else if (strcasecmp(name, "window") == 0)
		gWindow = value;
	else if (strcasecmp(name, "reset") == 0)
		gResetAsked = value;
	else if (strcasecmp(name, "partialTemperature") == 0)
		gPartialTemperature = value;
	else if (strcasecmp(name, "fullTemperature") == 0)
		gFullTemperature = value;
	else if (strcasecmp(name, "bigChange") == 0)
		gBigChangePercent = value;
	else if (strcasecmp(name, "cleanAfter") == 0)
		gCleanAfterMs = value;
	else if (strcasecmp(name, "cleanCount") == 0)
		gCleanCount = value;
	else if (strcasecmp(name, "cleanPause") == 0)
		gCleanPauseMs = value;
	else if (strcasecmp(name, "cleanArea") == 0)
		gCleanAreaPercent = value;
	else if (strcasecmp(name, "localClean") == 0)
		gLocalCleanCount = value;
	else if (strcasecmp(name, "trace") == 0)
		gTrace = value;
	else if (strcasecmp(name, "redraw") == 0)
		gRedraw = value;
	else
		return false;
	if (gPanelTask != NULL)
		xTaskNotifyGive(gPanelTask);			// (a test, a reset or a redraw done at once)
	return true;
}


// The settings the Host preferences panel offers (host/HostSettings.cpp),
// as host/HostWindow.h's window options: a new page drawn in full, the
// clean-ups, and "clearGhosts" (one full refresh of the whole screen)
bool
HostWindowOption(const char* name, long* value)
{
	if (strcmp(name, "bigChange") == 0)
		*value = gBigChangePercent;
	else if (strcmp(name, "cleanCount") == 0)
		*value = gCleanCount;
	else if (strcmp(name, "cleanAfter") == 0)
		*value = gCleanAfterMs;
	else if (strcmp(name, "cleanArea") == 0)
		*value = gCleanAreaPercent;
	else if (strcmp(name, "localClean") == 0)
		*value = gLocalCleanCount;
	else if (strcmp(name, "clearGhosts") == 0)
		*value = 0;
	else
		return false;
	return true;
}


bool
HostWindowSetOption(const char* name, long value)
{
	if (strcmp(name, "clearGhosts") == 0)
		return HostDevicePanelSet("redraw", 2);
	long now;
	return HostWindowOption(name, &now) && HostDevicePanelSet(name, value);
}


void
HostDevicePanelFollow(void (*now)(const unsigned char** grays, long* width, long* height))
{
	gDisplayNow = now;
	now((const unsigned char**) &gGrays, (long*) &gGraysWidth, (long*) &gGraysHeight);
}


bool
HostDeviceDisplayTurned(void)
{
	return gGrays != NULL && gGraysWidth > gGraysHeight;
}


void
HostDevicePanelShow(const unsigned char* grays, long width, long height)
{
	gGraysWidth = width;
	gGraysHeight = height;
	gGrays = grays;
}


// What the panel shows, printed on the console as "shot-begin 240 320", a
// line "shot <hex>" for each row (a bit set is white, the leftmost pixel
// the top bit), "shot-end" - tools/tdeck/panelshot.py makes a picture of it
void
HostDevicePanelDump(void)
{
	if (gShown == NULL)
	{
		printf("shot-none\n");
		return;
	}
	printf("shot-begin %ld %ld\n", kPanelWidth, kPanelHeight);
	for (long y = 0; y < kPanelHeight; y++)
	{
		printf("shot ");
		for (long b = 0; b < kRowBytes; b++)
			printf("%02x", gShown[y * kRowBytes + b]);
		printf("\n");
		if (y % 40 == 39)
			vTaskDelay(1);
	}
	printf("shot-end\n");
}
