/*
	File:		host/esp32/main/tdeck_modem.cpp

	Contains:	The board's cellular module (the T-Deck Pro's 4G variant:
				a SIMCom A7682E), given to the portable engine
				(hal/host/HostCellular.h) as its byte link: UART1 at 115200
				8N1, and a task of the board's own that runs the engine.

				The pins and the start are lilyphone1's (the owner's
				firmware for this board, include/utilities.h and
				src/main.cpp's A7682E_init):

				- GPIO 41 (BOARD_6609_EN) high powers the module (an SGM6609
				  boost converter);
				- GPIO 40 (BOARD_A7682E_PWRKEY) is a button, not a switch:
				  the module starts on a pulse - low, high for 50 ms, low;
				- the module's TXD is the ESP32-S3's GPIO 11 (our RX), its
				  RXD GPIO 10 (our TX);
				- GPIO 7, 8 and 9 are its RI, DTR and RESET (the other
				  T-Deck Pro variant's I2S audio uses the same pins, which
				  is why that variant has no modem); RESET is left alone.

				The module's sleep, so that the chip can sleep too
				(tdeck_power.cpp: a UART stops in light sleep, so the
				module powered kept the chip awake): the engine's setup
				ends with AT+CSCLK=1 (HostCellularLink's setup), after
				which the module sleeps while DTR is high.  DTR is pulled
				low, and the chip kept awake, while the engine talks to
				the module (a write, or bytes from it), with 100 ms for
				its UART to wake; after half a second without a byte either
				way (15 s while what was written has had no answer, and
				never with bytes still in the UART's buffer) DTR goes high
				and the chip may sleep - awake about 0.65 s in each 15 s,
				the engine's status poll.  The module pulls RI low
				for a call, a message or anything else it says by itself:
				RI low wakes the chip from light sleep (a GPIO wake-up)
				and the module's UART with it (DTR low), so that what it
				says is heard.  Until the module has taken AT+CSCLK=1 (a
				module just powered, or one that stopped answering) the
				chip is kept awake while it is powered, as before.

				The module can be switched off to save the battery (the
				Host preferences panel's "The 4G module on":
				HostCellularSetPower): GPIO 41 low after the engine has
				shut it down in order; on again, GPIO 41 high and a press
				of the power key, as at boot.  The choice is kept in NVS
				("cellOff"), so a module switched off stays off from the
				next boot on.  The switching is done by a task of the
				board's own: the Newton's tasks, whose stacks are in PSRAM,
				must not write the flash NVS is on.

				The task's stack is in internal RAM (it is a FreeRTOS task
				the board makes itself, not one of the Newton's): the
				engine's large buffers are HOST_LARGE_STATIC, in PSRAM.
				ESP-IDF's headers only (esp32host.h says why).
*/

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nvs.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_sleep.h"

#include "esp32host.h"
#include "HostCellular.h"

#define PIN_MODEM_POWER		41			// BOARD_6609_EN
#define PIN_MODEM_PWRKEY	40
#define PIN_MODEM_TX		10			// ours, to the module's RXD
#define PIN_MODEM_RX		11			// ours, from the module's TXD
#define PIN_MODEM_RI		7			// low: the module has something to say
#define PIN_MODEM_DTR		8			// high: the module may sleep (after AT+CSCLK=1)

static const uart_port_t	kPort = UART_NUM_1;
static HostCellularLink		gLink;
static QueueHandle_t		gPowerRequests;		// the switch's: on or off (a bool)

static const char			kSleepSetup[] = "AT+CSCLK=1";
const int64_t				kDrowseUs = 500000;		// no byte either way this long: the module let sleep
const int64_t				kReplyDueUs = 15000000;	// (this long when nothing has come since the last write)
static volatile bool		gPowered = false;		// the module's supply on
static volatile bool		gSleepArmed = false;	// it has been told AT+CSCLK=1
static volatile bool		gAwake = true;			// DTR low, the chip kept out of light sleep
static int64_t				gLastTraffic = 0;
static bool					gReplyDue = false;		// written to since the module last said anything


// The module's UART woken (DTR low) and the chip kept awake, for a while
static void
Wake(void)
{
	gLastTraffic = esp_timer_get_time();
	if (gAwake || !gPowered)
		return;
	HostDevicePowerKeepAwake(kAwakeCell, true);
	gpio_set_level((gpio_num_t) PIN_MODEM_DTR, 0);
	gAwake = true;
	vTaskDelay(pdMS_TO_TICKS(100));			// (its UART back: SIMCom asks for some tens of ms)
}


// The module let sleep (DTR high), and the chip with it, after a quiet while
static void
Drowse(void)
{
	if (!gAwake || !gSleepArmed || !gPowered || esp_timer_get_time() - gLastTraffic < (gReplyDue ? kReplyDueUs : kDrowseUs))
		return;
	size_t buffered = 0;
	uart_get_buffered_data_len(kPort, &buffered);
	if (buffered != 0)
		return;
	gpio_set_level((gpio_num_t) PIN_MODEM_DTR, 1);
	gAwake = false;
	HostDevicePowerKeepAwake(kAwakeCell, false);
}


static long
UartRead(void* /*ref*/, unsigned char* bytes, long size, long timeoutMs)
{
	// (RI low: the module is saying something - its UART, and the chip,
	// woken to hear it)
	if (gpio_get_level((gpio_num_t) PIN_MODEM_RI) == 0)
		Wake();
	else
		Drowse();
	// the first byte waited for, then whatever else has come
	size_t buffered = 0;
	uart_get_buffered_data_len(kPort, &buffered);
	if (buffered == 0)
	{
		int n = uart_read_bytes(kPort, bytes, 1, pdMS_TO_TICKS(timeoutMs));
		if (n <= 0)
			return n < 0 ? -1 : 0;
		gReplyDue = false;
		Wake();
		uart_get_buffered_data_len(kPort, &buffered);
		if (buffered == 0 || size == 1)
			return 1;
		long more = (long) buffered < size - 1 ? (long) buffered : size - 1;
		int m = uart_read_bytes(kPort, bytes + 1, more, 0);
		return 1 + (m > 0 ? m : 0);
	}
	gReplyDue = false;
	Wake();
	long count = (long) buffered < size ? (long) buffered : size;
	int n = uart_read_bytes(kPort, bytes, count, 0);
	return n < 0 ? -1 : n;
}


static void
UartWrite(void* /*ref*/, const void* bytes, long size)
{
	Wake();
	// (the setup's AT+CSCLK=1 going out: the module sleeps when let from
	// now on)
	if (size >= (long) sizeof(kSleepSetup) - 1 && memcmp(bytes, kSleepSetup, sizeof(kSleepSetup) - 1) == 0)
		gSleepArmed = true;
	gReplyDue = true;
	uart_write_bytes(kPort, bytes, size);
}


// The module's supply switched: on, it starts awake and unarmed (it has
// forgotten AT+CSCLK); off, the chip no longer kept awake for it, and its
// RI (which may sink when it is unpowered) no longer a wake-up
static void
SetPowered(bool on)
{
	gPowered = on;
	gSleepArmed = false;
	gAwake = true;
	gpio_set_level((gpio_num_t) PIN_MODEM_DTR, 0);
	HostDevicePowerKeepAwake(kAwakeCell, on);
	if (on)
		gpio_wakeup_enable((gpio_num_t) PIN_MODEM_RI, GPIO_INTR_LOW_LEVEL);
	else
		gpio_wakeup_disable((gpio_num_t) PIN_MODEM_RI);
}


// the network's time: the board's clock set from it, and the Newton's
// from the board's (newtonmain.cpp's DeviceServices loop, as after SNTP)
static void
NetworkTime(long long utcSeconds, long offsetMinutes, bool hasOffset)
{
	printf("[cell] the network's time: %lld (zone %+ld min%s)\n", utcSeconds, offsetMinutes, hasOffset ? "" : ", none given");
	HostDeviceClockSetTo(utcSeconds);
}


// Whether the module answers "AT" within about a second
static bool
Answers(void)
{
	uart_flush_input(kPort);
	uart_write_bytes(kPort, "AT\r\n", 4);
	char reply[64];
	int n = 0;
	TickType_t until = xTaskGetTickCount() + pdMS_TO_TICKS(1000);
	while (xTaskGetTickCount() < until && n < (int) sizeof(reply) - 1)
	{
		int got = uart_read_bytes(kPort, reply + n, 1, pdMS_TO_TICKS(50));
		if (got > 0)
		{
			n += got;
			reply[n] = 0;
			if (strstr(reply, "OK") != NULL)
				return true;
		}
	}
	return false;
}


static void
PressPowerKey(int ms)
{
	gpio_set_level((gpio_num_t) PIN_MODEM_PWRKEY, 0);
	vTaskDelay(pdMS_TO_TICKS(10));
	gpio_set_level((gpio_num_t) PIN_MODEM_PWRKEY, 1);
	vTaskDelay(pdMS_TO_TICKS(ms));
	gpio_set_level((gpio_num_t) PIN_MODEM_PWRKEY, 0);
}


// the choice kept: whether the module was switched off
static bool
KeptOff(void)
{
	nvs_handle_t nvs;
	uint8_t off = 0;
	if (nvs_open("newton", NVS_READONLY, &nvs) == ESP_OK)
	{
		nvs_get_u8(nvs, "cellOff", &off);
		nvs_close(nvs);
	}
	return off != 0;
}


static void
KeepOff(bool off)
{
	nvs_handle_t nvs;
	if (nvs_open("newton", NVS_READWRITE, &nvs) == ESP_OK)
	{
		nvs_set_u8(nvs, "cellOff", off ? 1 : 0);
		nvs_commit(nvs);
		nvs_close(nvs);
	}
}


// The switch (HostCellularSetPowerSwitch): handed to the power task
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
		if (on)
		{
			gpio_set_level((gpio_num_t) PIN_MODEM_POWER, 1);
			vTaskDelay(pdMS_TO_TICKS(100));
			PressPowerKey(50);
		}
		else
		{
			gpio_set_level((gpio_num_t) PIN_MODEM_PWRKEY, 0);
			gpio_set_level((gpio_num_t) PIN_MODEM_POWER, 0);
		}
		SetPowered(on);
		KeepOff(!on);
		printf("[cell] the module switched %s\n", on ? "on" : "off");
	}
}


static void
CellTask(void*)
{
	if (KeptOff())
	{
		// switched off before: left off, the engine waiting for it
		gpio_set_level((gpio_num_t) PIN_MODEM_POWER, 0);
		SetPowered(false);
		printf("[cell] the module is switched off (the Host panel)\n");
		HostCellularRun(&gLink);
	}
	// the module powered and started as LilyGO's example (and lilyphone1's
	// A7682E_init) do: the supply on, a 50 ms press of the power key; and,
	// as lilyphone1 does when that gets no answer, a longer press (a second)
	gpio_set_level((gpio_num_t) PIN_MODEM_POWER, 1);
	SetPowered(true);
	vTaskDelay(pdMS_TO_TICKS(100));
	printf("[cell] the module's supply (GPIO %d) reads %d\n", PIN_MODEM_POWER, gpio_get_level((gpio_num_t) PIN_MODEM_POWER));
	PressPowerKey(50);
	bool answered = false;
	for (int tries = 0; tries < 2 && !answered; tries++)
	{
		for (int i = 0; i < 15 && !answered; i++)
			answered = Answers();
		if (!answered && tries == 0)
		{
			printf("[cell] no answer to a short press; pressing for a second\n");
			PressPowerKey(1000);
		}
	}
	printf(answered ? "[cell] the module answers\n" : "[cell] the module does not answer yet; the engine keeps asking\n");
	HostCellularRun(&gLink);
}


bool
HostDeviceCellStart(void)
{
	// (a firmware that put the board to sleep may have latched these pins
	// with GPIO hold - lilyphone1's does - which outlasts a reset and
	// ignores every write until released: the module then never gets its
	// supply)
	gpio_hold_dis((gpio_num_t) PIN_MODEM_POWER);
	gpio_hold_dis((gpio_num_t) PIN_MODEM_PWRKEY);
	gpio_deep_sleep_hold_dis();
	gpio_config_t pins = {};
	pins.pin_bit_mask = (1ULL << PIN_MODEM_POWER) | (1ULL << PIN_MODEM_PWRKEY) | (1ULL << PIN_MODEM_DTR);
	pins.mode = GPIO_MODE_INPUT_OUTPUT;			// (read back for the console)
	gpio_config(&pins);
	gpio_set_level((gpio_num_t) PIN_MODEM_PWRKEY, 0);
	gpio_set_level((gpio_num_t) PIN_MODEM_DTR, 0);	// (awake until it has taken AT+CSCLK=1)
	gpio_config_t ri = {};
	ri.pin_bit_mask = 1ULL << PIN_MODEM_RI;
	ri.mode = GPIO_MODE_INPUT;
	ri.pull_up_en = GPIO_PULLUP_ENABLE;
	gpio_config(&ri);
	esp_sleep_enable_gpio_wakeup();				// (RI low wakes the chip: SetPowered)

	uart_config_t config = {};
	config.baud_rate = 115200;
	config.data_bits = UART_DATA_8_BITS;
	config.parity = UART_PARITY_DISABLE;
	config.stop_bits = UART_STOP_BITS_1;
	config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
	config.source_clk = UART_SCLK_DEFAULT;
	if (uart_driver_install(kPort, 2048, 0, 0, NULL, 0) != ESP_OK
	||  uart_param_config(kPort, &config) != ESP_OK
	||  uart_set_pin(kPort, PIN_MODEM_TX, PIN_MODEM_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK)
	{
		printf("[cell] no UART for the module\n");
		return false;
	}
	gLink.ref = NULL;
	gLink.read = UartRead;
	gLink.write = UartWrite;
	gLink.setup = kSleepSetup;
	HostCellularSetClockHandler(NetworkTime);
	gPowerRequests = xQueueCreate(4, sizeof(bool));
	if (gPowerRequests != NULL && xTaskCreatePinnedToCore(PowerTask, "cellpower", 3072, NULL, 4, NULL, 0) == pdPASS)
		HostCellularSetPowerSwitch(PowerSwitch, !KeptOff());
	HostCellularWillRun();					// (the Newton's side boots while the module starts)
	// (core 0, beside the WiFi's task: the Newton's are on core 1)
	if (xTaskCreatePinnedToCore(CellTask, "cell", 8192, NULL, 4, NULL, 0) != pdPASS)
	{
		printf("[cell] no room for the module's task\n");
		return false;
	}
	return true;
}
