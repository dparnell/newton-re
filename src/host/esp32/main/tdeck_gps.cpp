/*
	File:		host/esp32/main/tdeck_gps.cpp

	Contains:	The board's GPS receiver (the T-Deck Pro's u-blox MIA-M10Q),
				given to the host's GPS receiver (hal/host/HostGPS.h) as its
				source "device": its NMEA read off UART2 and handed on as it
				comes, so that HostGPSStatus has a fix (the Mesh
				application's location) and a Newton GPS application on
				serial port 2 reads the receiver's own sentences.

				The pins and the speed are lilyphone1's (the owner's
				firmware for this board: include/utilities.h,
				src/peripherals/peri_gps.cpp):

				- GPIO 39 (BOARD_GPS_EN) high powers the module;
				- the module's TXD is the ESP32-S3's GPIO 44 (our RX), its
				  RXD GPIO 43 (our TX) - UART0's pins, free: the console is
				  the USB serial/JTAG port (sdkconfig.tdeck-pro);
				- 38400 baud, as lilyphone1 sets the M10 (its default is
				  9600, which is tried when nothing readable comes at 38400).

				The receiver can be switched off to save the battery (the
				Host preferences panel's "The GPS on": HostGPSSetPower):
				GPIO 39 low, the fix forgotten; on again, GPIO 39 high.  The
				choice is kept in NVS ("gpsOff"), so a receiver switched
				off stays off from the next boot on.  The switching is done
				by a task of the board's own: the Newton's tasks, whose
				stacks are in PSRAM, must not write the flash NVS is on.

				The chip sleeps between the receiver's bursts (its NMEA
				comes once a second): the receiver's TXD going low wakes it
				from light sleep (a GPIO wake-up on our RX pin) and an
				interrupt on the same pin takes a lock that keeps it awake
				until the burst has ended (15 ms without a byte), then the
				interrupt is armed again.  The burst is kept short: unless
				a Newton application reads the receiver's sentences, it is
				told to send only GGA and RMC (SetSentences), which the
				fix is read from - about 150 bytes a second, where its
				dozen sentences took 220 ms at 38400 baud.  Nothing was seen lost: across
				4,539 light sleeps the receiver's 1,715 sentences all came
				whole (!pm counts them); were the first characters of a
				burst lost while the chip wakes, its first sentence would
				fail its checksum and go unread, and GGA, RMC and GLL each
				carry the position.  While a Newton
				application has the receiver's serial port open
				(HostGPSPlugged) the chip is kept awake instead, so that it
				reads every sentence whole.

				The receiver's thread is HostGPS's own (std::thread: a
				pthread with a 3 KB internal stack, its buffers static).
				ESP-IDF's headers only (esp32host.h says why).
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_pm.h"
#include "esp_sleep.h"

#include "esp32host.h"
#include "tdeck_board.h"
#include "HostGPS.h"


static const uart_port_t	kPort = UART_NUM_2;
static QueueHandle_t		gPowerRequests;		// the switch's: on or off (a bool)

// a burst of sentences: the chip kept awake from its first edge until it ends
static esp_pm_lock_handle_t	gBurst = NULL;
static volatile bool		gBurstHeld = false;
static volatile int64_t		gBurstAt = 0;		// when the edge came
static SemaphoreHandle_t	gBurstBegan = NULL;	// given by the edge: the reader waits for it between bursts
static int64_t				gLastByte = 0;
const int64_t				kBurstEndUs = 15000;	// no byte this long: the burst is over
const long					kSliceMs = 5;
static volatile bool		gPowered = false;
static int64_t				gPoweredAt = 0;

// the receiver's sentences: all of them while a Newton application reads
// them (the serial port open), else only GGA and RMC - the fix's, which is
// all HostGPS reads (GLL too, but it has nothing GGA and RMC have not) - so
// that a burst is a sixth as long and the chip asleep the sooner
enum { kSentencesUnknown, kSentencesAll, kSentencesFix };
static int					gSentencesSet = kSentencesUnknown;

// for the console's !pm: its bursts (each wakes the chip if asleep) and how
// long they kept it awake, and the sentences heard whole (their checksums
// right) and broken
static volatile unsigned long	gBursts = 0;
static int64_t					gBurstUs = 0;
static unsigned long			gWhole = 0, gBroken = 0;
static char						gSentence[100];
static int						gSentenceLength = -1;	// -1: not in one


// The first edge of a burst (a low level on our RX: the interrupt then
// disarmed until the burst has ended)
static void
BurstStarts(void*)
{
	gpio_intr_disable((gpio_num_t) PIN_GPS_RX);
	gBurstAt = esp_timer_get_time();
	if (!gBurstHeld)
	{
		gBursts++;
		gBurstHeld = true;
		esp_pm_lock_acquire(gBurst);
	}
	BaseType_t woken = pdFALSE;
	xSemaphoreGiveFromISR(gBurstBegan, &woken);
	portYIELD_FROM_ISR(woken);
}


static void
BurstEnds(void)
{
	if (!gBurstHeld)
		return;
	gBurstHeld = false;
	gBurstUs += esp_timer_get_time() - gBurstAt;
	esp_pm_lock_release(gBurst);
	gpio_intr_enable((gpio_num_t) PIN_GPS_RX);
}


// The receiver told which sentences to send (UBX-CFG-VALSET, to its RAM: a
// receiver powered again starts from its defaults, all of them, and is told
// again): CFG-MSGOUT-NMEA_ID_*_UART1, one a fix (1) or none (0)
static void
SetSentences(int which)
{
	static const uint32_t kKeys[6] = {
		0x209100bb,		// GGA
		0x209100ac,		// RMC
		0x209100ca,		// GLL
		0x209100c0,		// GSA
		0x209100c5,		// GSV
		0x209100b1		// VTG
	};
	uint8_t m[8 + 4 + 6 * 5 + 2];
	long n = 0;
	m[n++] = 0xB5; m[n++] = 0x62; m[n++] = 0x06; m[n++] = 0x8A;
	m[n++] = 4 + 6 * 5; m[n++] = 0;
	m[n++] = 0;			// version
	m[n++] = 0x01;		// layers: RAM
	m[n++] = 0; m[n++] = 0;
	for (int k = 0; k < 6; k++)
	{
		for (int b = 0; b < 4; b++)
			m[n++] = (uint8_t) (kKeys[k] >> (8 * b));
		m[n++] = which == kSentencesAll || k < 2 ? 1 : 0;
	}
	uint8_t a = 0, c = 0;
	for (long k = 2; k < n; k++)
	{
		a += m[k];
		c += a;
	}
	m[n++] = a;
	m[n++] = c;
	uart_write_bytes(kPort, m, (size_t) n);
	gSentencesSet = which;
}


// The receiver's output wakes the chip (on) or not (off: an unpowered
// receiver's line is no matter)
static void
ArmWake(bool on)
{
	if (gBurst == NULL)
		return;
	if (on)
	{
		gpio_wakeup_enable((gpio_num_t) PIN_GPS_RX, GPIO_INTR_LOW_LEVEL);
		gpio_intr_enable((gpio_num_t) PIN_GPS_RX);
	}
	else
	{
		gpio_intr_disable((gpio_num_t) PIN_GPS_RX);
		gpio_wakeup_disable((gpio_num_t) PIN_GPS_RX);
		BurstEnds();
		gpio_intr_disable((gpio_num_t) PIN_GPS_RX);
	}
}


static bool
KeptOff(void)
{
	nvs_handle_t nvs;
	uint8_t off = 0;
	if (nvs_open("newton", NVS_READONLY, &nvs) == ESP_OK)
	{
		nvs_get_u8(nvs, "gpsOff", &off);
		nvs_close(nvs);
	}
	return off != 0;
}


// The switch (HostGPSSetPowerSwitch): handed to the power task
static bool
PowerSwitch(bool on)
{
	return xQueueSend(gPowerRequests, &on, 0) == pdTRUE;
}


static void
PowerTask(void*)
{
	for (;;)
	{
		bool on;
		if (xQueueReceive(gPowerRequests, &on, portMAX_DELAY) != pdTRUE)
			continue;
		HostDeviceBoardSet(kLineGpsPower, on);
		gPoweredAt = esp_timer_get_time();
		gSentencesSet = kSentencesUnknown;		// (its defaults again)
		gPowered = on;
		ArmWake(on);
		nvs_handle_t nvs;
		if (nvs_open("newton", NVS_READWRITE, &nvs) == ESP_OK)
		{
			nvs_set_u8(nvs, "gpsOff", on ? 0 : 1);
			nvs_commit(nvs);
			nvs_close(nvs);
		}
		printf("[gps] the receiver switched %s\n", on ? "on" : "off");
	}
}


static void
Count(const unsigned char* bytes, int size)
{
	for (int i = 0; i < size; i++)
	{
		char c = (char) bytes[i];
		if (c == '$')
			gSentenceLength = 0;
		else if (gSentenceLength < 0)
			continue;
		if (c == '\r' || c == '\n')
		{
			gSentence[gSentenceLength] = 0;
			char* star = strchr(gSentence, '*');
			if (star != NULL && strtoul(star + 1, NULL, 16) == HostGPSChecksum(gSentence))
				gWhole++;
			else
				gBroken++;
			gSentenceLength = -1;
		}
		else if (gSentenceLength < (int) sizeof(gSentence) - 1)
			gSentence[gSentenceLength++] = c;
	}
}


void
HostDeviceGpsDump(void)
{
	unsigned long bursts = gBursts;
	printf("newton: GPS %s, %s sentences, %lu bursts of %lld ms on average, sentences %lu whole, %lu broken\n",
		   !gPowered ? "off" : gBurst == NULL ? "on, keeping the chip awake" : HostGPSPlugged() ? "on, read by the Newton (the chip kept awake)" : "on",
		   gSentencesSet == kSentencesAll ? "all" : gSentencesSet == kSentencesFix ? "the fix's" : "its own",
		   bursts, bursts != 0 ? (long long) (gBurstUs / 1000 / (int64_t) bursts) : 0LL, gWhole, gBroken);
}


// Between bursts, nothing until the next one's first edge (gBurstBegan); in a
// burst, in 5 ms slices so as to see its end.  (Slices all the time woke the
// chip every 5 ms - every 20 before - and kept it out of light sleep.)
static long
UartRead(unsigned char* bytes, long size, long timeoutMs)
{
	if (gBurst == NULL)
	{
		int got = uart_read_bytes(kPort, bytes, (uint32_t) size, pdMS_TO_TICKS(timeoutMs));
		if (got > 0)
			Count(bytes, got);
		return got < 0 ? -1 : got;
	}
	bool plugged = HostGPSPlugged();
	if (gBurst != NULL)
		HostDevicePowerKeepAwake(kAwakeGps, gPowered && plugged);
	// (the sentences a Newton application or the fix needs, told once the
	// receiver has said something since it was powered - it is listening)
	int wanted = plugged ? kSentencesAll : kSentencesFix;
	if (gPowered && gSentencesSet != wanted && gLastByte > gPoweredAt)
		SetSentences(wanted);
	int64_t until = esp_timer_get_time() + (int64_t) timeoutMs * 1000;
	for (;;)
	{
		size_t buffered = 0;
		uart_get_buffered_data_len(kPort, &buffered);
		if (!gBurstHeld && buffered == 0)
		{
			int64_t left = until - esp_timer_get_time();
			if (left <= 0 || xSemaphoreTake(gBurstBegan, pdMS_TO_TICKS(left / 1000) + 1) != pdTRUE)
				return 0;
			continue;
		}
		int got = uart_read_bytes(kPort, bytes, (uint32_t) size, pdMS_TO_TICKS(kSliceMs));
		int64_t now = esp_timer_get_time();
		if (got < 0)
			return -1;
		if (got > 0)
		{
			Count(bytes, got);
			gLastByte = now;
			return got;
		}
		int64_t last = gLastByte > gBurstAt ? gLastByte : gBurstAt;
		if (gBurstHeld && now - last >= kBurstEndUs)
			BurstEnds();
		if (now >= until)
			return 0;
	}
}


static void
UartWrite(const unsigned char* bytes, long size)
{
	uart_write_bytes(kPort, bytes, (size_t) size);
}


static const HostGPSDevice	gDevice = { UartRead, UartWrite };


// ==> whether a sentence's start ("$G") comes within ms at the UART's speed
static bool
Hears(long ms)
{
	uart_flush_input(kPort);
	int64_t until = esp_timer_get_time() + ms * 1000;
	char was = 0;
	while (esp_timer_get_time() < until)
	{
		unsigned char bytes[64];
		int got = uart_read_bytes(kPort, bytes, sizeof(bytes), pdMS_TO_TICKS(100));
		for (int i = 0; i < got; i++)
		{
			if (was == '$' && bytes[i] == 'G')
				return true;
			was = (char) bytes[i];
		}
	}
	return false;
}


bool
HostDeviceGpsStart(void)
{
	HostDeviceBoardLineStart(kLineGpsPower);
	bool on = !KeptOff();
	HostDeviceBoardSet(kLineGpsPower, on);
	gPowered = on;

	uart_config_t config = {};
	config.baud_rate = 38400;
	config.data_bits = UART_DATA_8_BITS;
	config.parity = UART_PARITY_DISABLE;
	config.stop_bits = UART_STOP_BITS_1;
	config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
	config.source_clk = UART_SCLK_DEFAULT;
	if (uart_driver_install(kPort, 2048, 0, 0, NULL, 0) != ESP_OK
	||  uart_param_config(kPort, &config) != ESP_OK
	||  uart_set_pin(kPort, PIN_GPS_TX, PIN_GPS_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK)
	{
		printf("[gps] no UART for the receiver\n");
		return false;
	}
	// (the module's speed: lilyphone1's 38400, else its default 9600; a
	// receiver just powered says nothing for a moment)
	vTaskDelay(pdMS_TO_TICKS(300));
	long baud = 38400;
	if (!on)
		printf("[gps] the receiver is switched off (the Host panel)\n");
	else if (!Hears(1500))
	{
		uart_set_baudrate(kPort, 9600);
		baud = Hears(1500) ? 9600 : 38400;
		uart_set_baudrate(kPort, baud);
	}
	// (its UART stops in light sleep: the receiver's first edge wakes the chip)
	esp_err_t err = gpio_install_isr_service(0);
	gBurstBegan = xSemaphoreCreateBinary();
	if ((err == ESP_OK || err == ESP_ERR_INVALID_STATE) && gBurstBegan != NULL
	 && esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "gps", &gBurst) == ESP_OK)
	{
		gpio_intr_disable((gpio_num_t) PIN_GPS_RX);
		gpio_isr_handler_add((gpio_num_t) PIN_GPS_RX, BurstStarts, NULL);
		esp_sleep_enable_gpio_wakeup();
		ArmWake(on);
	}
	else
		HostDevicePowerKeepAwake(kAwakeGps, true);	// (kept awake throughout, as before)
	gPowerRequests = xQueueCreate(4, sizeof(bool));
	if (gPowerRequests != NULL && xTaskCreatePinnedToCore(PowerTask, "gpspower", 3072, NULL, 4, NULL, 0) == pdPASS)
		HostGPSSetPowerSwitch(PowerSwitch, on);
	HostGPSSetDevice(&gDevice);
	char why[96];
	if (!HostGPSStart("device", why, sizeof(why)))
	{
		printf("[gps] the receiver did not start: %s\n", why);
		return false;
	}
	printf("[gps] the receiver at %ld baud\n", baud);
	return true;
}
