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

				The receiver's thread is HostGPS's own (std::thread: a
				pthread with a 3 KB internal stack, its buffers static).
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

#include "esp32host.h"
#include "HostGPS.h"

#define PIN_GPS_POWER	39				// BOARD_GPS_EN
#define PIN_GPS_TX		43				// ours, to the module's RXD
#define PIN_GPS_RX		44				// ours, from the module's TXD

static const uart_port_t	kPort = UART_NUM_2;
static QueueHandle_t		gPowerRequests;		// the switch's: on or off (a bool)


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
		HostDevicePowerKeepAwake(kAwakeGps, on);
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


static long
UartRead(unsigned char* bytes, long size, long timeoutMs)
{
	int got = uart_read_bytes(kPort, bytes, (uint32_t) size, pdMS_TO_TICKS(timeoutMs));
	return got < 0 ? -1 : got;
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
	HostDevicePowerKeepAwake(kAwakeGps, on);	// (its UART stops in light sleep)

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
