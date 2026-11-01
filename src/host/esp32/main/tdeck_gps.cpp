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
				until the burst has ended (30 ms without a byte), then the
				interrupt is armed again.  Nothing was seen lost: across
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
#include "nvs.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_pm.h"
#include "esp_sleep.h"

#include "esp32host.h"
#include "HostGPS.h"

#define PIN_GPS_POWER	39				// BOARD_GPS_EN
#define PIN_GPS_TX		43				// ours, to the module's RXD
#define PIN_GPS_RX		44				// ours, from the module's TXD

static const uart_port_t	kPort = UART_NUM_2;
static QueueHandle_t		gPowerRequests;		// the switch's: on or off (a bool)

// a burst of sentences: the chip kept awake from its first edge until it ends
static esp_pm_lock_handle_t	gBurst = NULL;
static volatile bool		gBurstHeld = false;
static volatile int64_t		gBurstAt = 0;		// when the edge came
static int64_t				gLastByte = 0;
const int64_t				kBurstEndUs = 30000;	// no byte this long: the burst is over
const long					kSliceMs = 20;
static volatile bool		gPowered = false;

// for the console's !pm: its bursts (each wakes the chip if asleep), and
// the sentences heard whole (their checksums right) and broken
static volatile unsigned long	gBursts = 0;
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
}


static void
BurstEnds(void)
{
	if (!gBurstHeld)
		return;
	gBurstHeld = false;
	esp_pm_lock_release(gBurst);
	gpio_intr_enable((gpio_num_t) PIN_GPS_RX);
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
		gpio_set_level((gpio_num_t) PIN_GPS_POWER, on ? 1 : 0);
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
	printf("newton: GPS %s, %lu bursts, sentences %lu whole, %lu broken\n",
		   !gPowered ? "off" : gBurst == NULL ? "on, keeping the chip awake" : HostGPSPlugged() ? "on, read by the Newton (the chip kept awake)" : "on",
		   (unsigned long) gBursts, gWhole, gBroken);
}


// (in slices, so as to see a burst's end and let the chip sleep)
static long
UartRead(unsigned char* bytes, long size, long timeoutMs)
{
	if (gBurst != NULL)
		HostDevicePowerKeepAwake(kAwakeGps, gPowered && HostGPSPlugged());
	int64_t until = esp_timer_get_time() + (int64_t) timeoutMs * 1000;
	for (;;)
	{
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
	gpio_hold_dis((gpio_num_t) PIN_GPS_POWER);
	gpio_config_t pins = {};
	pins.pin_bit_mask = 1ULL << PIN_GPS_POWER;
	pins.mode = GPIO_MODE_INPUT_OUTPUT;
	gpio_config(&pins);
	bool on = !KeptOff();
	gpio_set_level((gpio_num_t) PIN_GPS_POWER, on ? 1 : 0);
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
	if ((err == ESP_OK || err == ESP_ERR_INVALID_STATE)
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
