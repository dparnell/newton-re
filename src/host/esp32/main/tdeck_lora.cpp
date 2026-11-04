/*
	File:		host/esp32/main/tdeck_lora.cpp

	Contains:	The board's LoRa radio (the T-Deck Pro's SX1262), given to
				the portable mesh engine (hal/host/HostMesh.h, a MeshCore
				chat node) as its radio: RadioLib's SX1262 driver through
				MeshCore's wrapper for it (helpers/radiolib's
				CustomSX1262Wrapper, as lilyphone1 has it), over a RadioLib
				HAL of the board's own on ESP-IDF's drivers - RadioLib has
				none for the ESP32-S3 - and a task of the board's that runs
				the engine.

				The pins are lilyphone1's (include/utilities.h; LilyGO's
				schematic): the radio's chip select GPIO 3, reset 4, DIO1
				(its interrupt) 5, BUSY 6, on the SPI bus the panel shares
				(SCK 36, MOSI 33, MISO 47: tdeck_panel.cpp's
				HostDeviceSpiBusStart); its supply is switched by GPIO 46
				(BOARD_LORA_EN, high on).  The radio is set up as LilyGO's
				examples have it (main/CMakeLists.txt's SX126X_*): the TCXO
				on DIO3 at 2.4 V, DIO2 driving the antenna's TX/RX switch,
				the boosted receive gain, a 140 mA current limit, the DC-DC
				regulator, 22 dBm.

				The bus: RadioLib drives the chip select itself (a GPIO) and
				brackets each exchange with spiBeginTransaction/End, which
				take the bus (spi_device_acquire_bus) so the panel's
				transfers wait; the bytes go through buffers in internal
				RAM, as the panel's do (DMA cannot be pointed at a task's
				PSRAM stack).

				The radio can be switched off to save the battery (the
				Host preferences panel's "The LoRa radio on":
				HostMeshSetPower): the engine puts it to sleep, then GPIO 46
				goes low; on again, GPIO 46 high and the engine sets it up
				as at boot.  The choice is kept in NVS ("loraOff"), so a
				radio switched off stays off from the next boot on.

				The engine's task has its stack in PSRAM (12 KB, which
				internal RAM has not to spare), so it must not touch the
				flash: the node's blobs (identity, settings, contacts) are
				read and written in NVS (namespace "mesh") by a small task
				with an internal stack, which also does the power switch.
				ESP-IDF's headers and MeshCore's only (esp32host.h says
				why).
*/

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "rom/ets_sys.h"

#include "esp32host.h"
#include "tdeck_board.h"
#include "HostMesh.h"

#include <helpers/radiolib/CustomSX1262Wrapper.h>

#define PIN_LORA_CS		3
#define PIN_LORA_RESET	4
#define PIN_LORA_DIO1	5
#define PIN_LORA_BUSY	6

// (MeshCore's RadioLib wrappers ask Arduino's clock: compat/Arduino.h)
unsigned long
millis(void)
{
	return (unsigned long) (esp_timer_get_time() / 1000);
}


// ----------------------------------------------------------------------
// RadioLib's HAL on ESP-IDF's GPIO, timer and SPI master drivers

static TaskHandle_t		gMeshTask = NULL;		// (its wait, HostMeshPlatform's: cut short by the radio's interrupt)

static void IRAM_ATTR
InterruptTrampoline(void* arg)
{
	((void (*)(void)) arg)();
	if (gMeshTask != NULL)
	{
		BaseType_t woken = pdFALSE;
		vTaskNotifyGiveFromISR(gMeshTask, &woken);
		portYIELD_FROM_ISR(woken);
	}
}

class TDeckRadioHal : public RadioLibHal
{
	spi_device_handle_t		fDevice = NULL;
	uint8_t*				fOut = NULL;		// (internal RAM: DMA-capable)
	uint8_t*				fIn = NULL;
	static const size_t		kChunk = 256;
	void					(*fCallback)(void) = NULL;	// DIO1's (MeshCore's wrapper's setFlag)
	int64_t					fRaisedSince = 0;			// when DIO1 was first seen raised (0: it is not)

public:
	long					fRecoveries = 0;
	TDeckRadioHal() : RadioLibHal(GPIO_MODE_INPUT, GPIO_MODE_OUTPUT, 0, 1, GPIO_INTR_POSEDGE, GPIO_INTR_NEGEDGE) { }

	// (Module::init calls this, not spiBegin: the device is added here)
	void init() override { spiBegin(); }
	void term() override { }

	void pinMode(uint32_t pin, uint32_t mode) override
	{
		if (pin == RADIOLIB_NC)
			return;
		gpio_reset_pin((gpio_num_t) pin);
		gpio_set_direction((gpio_num_t) pin, (gpio_mode_t) mode);
	}

	void digitalWrite(uint32_t pin, uint32_t value) override
	{
		if (pin != RADIOLIB_NC)
			gpio_set_level((gpio_num_t) pin, value);
	}

	uint32_t digitalRead(uint32_t pin) override
	{
		return pin == RADIOLIB_NC ? 0 : gpio_get_level((gpio_num_t) pin);
	}

	void attachInterrupt(uint32_t pin, void (*callback)(void), uint32_t mode) override
	{
		if (pin == RADIOLIB_NC)
			return;
		static bool serviceInstalled = false;
		if (!serviceInstalled)
		{
			esp_err_t err = gpio_install_isr_service(0);
			serviceInstalled = err == ESP_OK || err == ESP_ERR_INVALID_STATE;	// (another driver's already)
		}
		fCallback = callback;
		fRaisedSince = 0;
		gpio_set_intr_type((gpio_num_t) pin, (gpio_int_type_t) mode);
		gpio_isr_handler_add((gpio_num_t) pin, InterruptTrampoline, (void*) callback);
		gpio_intr_enable((gpio_num_t) pin);
	}

	void detachInterrupt(uint32_t pin) override
	{
		if (pin == RADIOLIB_NC)
			return;
		fCallback = NULL;
		gpio_isr_handler_remove((gpio_num_t) pin);
		gpio_set_intr_type((gpio_num_t) pin, GPIO_INTR_DISABLE);
	}

	void delay(RadioLibTime_t ms) override
	{
		if (ms == 0)
			taskYIELD();
		else
			vTaskDelay(pdMS_TO_TICKS(ms) > 0 ? pdMS_TO_TICKS(ms) : 1);
	}

	void delayMicroseconds(RadioLibTime_t us) override { ets_delay_us((uint32_t) us); }
	RadioLibTime_t millis() override { return (RadioLibTime_t) (esp_timer_get_time() / 1000); }
	RadioLibTime_t micros() override { return (RadioLibTime_t) esp_timer_get_time(); }
	long pulseIn(uint32_t, uint32_t, RadioLibTime_t) override { return 0; }		// (not used by the SX126x)
	void yield() override { taskYIELD(); }

	void spiBegin() override
	{
		if (fDevice != NULL)
			return;
		fOut = (uint8_t*) heap_caps_malloc(kChunk, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
		fIn = (uint8_t*) heap_caps_malloc(kChunk, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
		spi_device_interface_config_t dev;
		memset(&dev, 0, sizeof(dev));
		dev.clock_speed_hz = 4 * 1000 * 1000;
		dev.mode = 0;
		dev.spics_io_num = -1;					// (RadioLib drives GPIO 3 itself)
		dev.queue_size = 1;
		if (fOut == NULL || fIn == NULL || !HostDeviceSpiBusStart() || spi_bus_add_device(SPI2_HOST, &dev, &fDevice) != ESP_OK)
		{
			printf("[mesh] the radio has no SPI device\n");
			fDevice = NULL;
		}
	}

	void spiBeginTransaction() override
	{
		if (fDevice != NULL)
			spi_device_acquire_bus(fDevice, portMAX_DELAY);
	}

	void spiTransfer(uint8_t* out, size_t length, uint8_t* in) override
	{
		if (fDevice == NULL)
		{
			if (in != NULL)
				memset(in, 0, length);
			return;
		}
		while (length > 0)
		{
			size_t n = length > kChunk ? kChunk : length;
			if (out != NULL)
				memcpy(fOut, out, n);
			else
				memset(fOut, 0, n);
			spi_transaction_t t;
			memset(&t, 0, sizeof(t));
			t.length = n * 8;
			t.tx_buffer = fOut;
			t.rx_buffer = fIn;
			spi_device_polling_transmit(fDevice, &t);
			if (in != NULL)
				memcpy(in, fIn, n);
			if (out != NULL)
				out += n;
			if (in != NULL)
				in += n;
			length -= n;
		}
	}

	void spiEndTransaction() override
	{
		if (fDevice != NULL)
			spi_device_release_bus(fDevice);
	}

	void spiEnd() override { }

	// The chip's status byte, read itself: GetStatus (0xC0) answers with it
	// on the byte after the command.  (RadioLib's SX126x::getStatus reads no
	// bytes after the command and so always says 0.)  The engine's thread
	// only, between RadioLib's own exchanges; BUSY is waited for as RadioLib
	// waits for it.
	uint8_t ReadStatus()
	{
		int64_t until = esp_timer_get_time() + 10 * 1000;
		while (gpio_get_level((gpio_num_t) PIN_LORA_BUSY) != 0)
			if (esp_timer_get_time() > until)
				return 0;
		uint8_t out[2] = { 0xC0, 0x00 }, in[2] = { 0, 0 };
		spiBeginTransaction();
		digitalWrite(PIN_LORA_CS, 0);
		spiTransfer(out, 2, in);
		digitalWrite(PIN_LORA_CS, 1);
		spiEndTransaction();
		return in[1];
	}

	// A missed interrupt (the engine's loop, each time round): DIO1 is raised
	// on a packet received or sent and lowered when the engine clears the
	// radio's flags - at most a loop or two later.  Raised for longer, its
	// rising edge never reached the wrapper, which then waits for an edge
	// that cannot come (DIO1 stays up) - deaf until its own next transmit.
	// The edge is handed on, late.
	long Check()
	{
		if (fCallback == NULL || gpio_get_level((gpio_num_t) PIN_LORA_DIO1) == 0)
		{
			fRaisedSince = 0;
			return fRecoveries;
		}
		int64_t now = esp_timer_get_time();
		if (fRaisedSince == 0)
			fRaisedSince = now;
		else if (now - fRaisedSince > 250 * 1000)
		{
			fRecoveries++;
			fRaisedSince = 0;
			printf("[mesh] the radio's interrupt was missed (%ld so far)\n", fRecoveries);
			fCallback();
		}
		return fRecoveries;
	}
};


// ----------------------------------------------------------------------
// The board MeshCore asks of (the RadioLib wrapper's MainBoard)

class TDeckBoard : public mesh::MainBoard
{
public:
	uint16_t	getBattMilliVolts() override { return 0; }		// (not told: the Newton has the battery)
	const char*	getManufacturerName() const override { return "LilyGo T-Deck-Pro"; }
	void		reboot() override { }						// (the Newton reboots the board, not the mesh)
	uint8_t		getStartupReason() const override { return BD_STARTUP_NORMAL; }
	uint32_t	getIRQGpio() override { return PIN_LORA_DIO1; }
};

static TDeckRadioHal		gHal;
static Module				gModule(&gHal, PIN_LORA_CS, PIN_LORA_DIO1, PIN_LORA_RESET, PIN_LORA_BUSY);
static CustomSX1262			gRadio(&gModule);
static TDeckBoard			gBoard;
static CustomSX1262Wrapper	gDriver(gRadio, gBoard);
static bool					gRadioUp = false;
static HostMeshPlatform		gPlatform;


// The engine's radioStart: the radio set up (at boot, or after its power
// came back), or retuned when it is up already
static bool
RadioStart(void*, const HostMeshRadioSettings* settings)
{
	if (!gRadioUp)
	{
		if (!gRadio.std_init(NULL))
		{
			printf("[mesh] the radio would not start\n");
			return false;
		}
		gDriver.begin();
		gRadioUp = true;
	}
	gDriver.setParams(settings->frequency, settings->bandwidth, (uint8_t) settings->spreadingFactor, (uint8_t) settings->codingRate);
	gRadio.setOutputPower(settings->txPower);
	return true;
}


// ... and radioSleep: cold sleep (its settings lost; set up again at power
// on), its interrupt let go - lilyphone1 cut the power to a radio still
// listening, its interrupt still attached
static void
RadioSleep(void*)
{
	if (!gRadioUp)
		return;
	gRadio.sleep(false);
	gHal.detachInterrupt(PIN_LORA_DIO1);
	gRadioUp = false;
}


// ----------------------------------------------------------------------
// The keeper: NVS and the power switch, on a task with an internal stack

enum KeeperWork { kKeeperLoad, kKeeperSave, kKeeperPower };

struct KeeperRequest
{
	KeeperWork		work;
	const char*		name;
	void*			bytes;
	long*			size;		// (a load's: in its room, out what was read)
	bool			on;			// (the power's)
	bool*			done;		// (a load's answer)
};

static QueueHandle_t		gKeeperRequests;
static SemaphoreHandle_t	gKeeperDone;		// (a load waited for)

static bool
KeptOff(void)
{
	nvs_handle_t nvs;
	uint8_t off = 0;
	if (nvs_open("newton", NVS_READONLY, &nvs) == ESP_OK)
	{
		nvs_get_u8(nvs, "loraOff", &off);
		nvs_close(nvs);
	}
	return off != 0;
}


static void
KeeperTask(void*)
{
	for (;;)
	{
		KeeperRequest r;
		if (xQueueReceive(gKeeperRequests, &r, portMAX_DELAY) != pdTRUE)
			continue;
		nvs_handle_t nvs;
		switch (r.work)
		{
		case kKeeperLoad:
		{
			bool found = false;
			size_t size = (size_t) *r.size;
			if (nvs_open("mesh", NVS_READONLY, &nvs) == ESP_OK)
			{
				found = nvs_get_blob(nvs, r.name, r.bytes, &size) == ESP_OK;
				nvs_close(nvs);
			}
			*r.size = found ? (long) size : 0;
			*r.done = found;
			xSemaphoreGive(gKeeperDone);
			break;
		}
		case kKeeperSave:
			if (nvs_open("mesh", NVS_READWRITE, &nvs) == ESP_OK)
			{
				if (nvs_set_blob(nvs, r.name, r.bytes, (size_t) *r.size) != ESP_OK)
					printf("[mesh] the %s could not be kept\n", r.name);
				nvs_commit(nvs);
				nvs_close(nvs);
			}
			heap_caps_free(r.bytes);
			heap_caps_free(r.size);
			break;
		case kKeeperPower:
			HostDeviceBoardSet(kLineLoraPower, r.on);
			if (nvs_open("newton", NVS_READWRITE, &nvs) == ESP_OK)
			{
				nvs_set_u8(nvs, "loraOff", r.on ? 0 : 1);
				nvs_commit(nvs);
				nvs_close(nvs);
			}
			printf("[mesh] the radio switched %s\n", r.on ? "on" : "off");
			break;
		}
	}
}


static bool
Load(void*, const char* name, void* bytes, long* size)
{
	bool found = false;
	KeeperRequest r = { kKeeperLoad, name, bytes, size, false, &found };
	if (xQueueSend(gKeeperRequests, &r, portMAX_DELAY) != pdTRUE)
		return false;
	xSemaphoreTake(gKeeperDone, portMAX_DELAY);
	return found;
}


// (a copy handed over: the engine's buffer is not waited for)
static void
Save(void*, const char* name, const void* bytes, long size)
{
	void* copy = heap_caps_malloc(size > 0 ? size : 1, MALLOC_CAP_SPIRAM);
	long* sizeCopy = (long*) heap_caps_malloc(sizeof(long), MALLOC_CAP_SPIRAM);
	if (copy == NULL || sizeCopy == NULL)
	{
		heap_caps_free(copy);
		heap_caps_free(sizeCopy);
		return;
	}
	memcpy(copy, bytes, size);
	*sizeCopy = size;
	KeeperRequest r = { kKeeperSave, name, copy, sizeCopy, false, NULL };
	if (xQueueSend(gKeeperRequests, &r, portMAX_DELAY) != pdTRUE)
	{
		heap_caps_free(copy);
		heap_caps_free(sizeCopy);
	}
}


// The switch (HostMeshSetPowerSwitch): handed to the keeper
static bool
PowerSwitch(bool on)
{
	KeeperRequest r = { kKeeperPower, NULL, NULL, NULL, on, NULL };
	return xQueueSend(gKeeperRequests, &r, 0) == pdTRUE;
}


static void
Random(void* bytes, size_t size)
{
	esp_fill_random(bytes, size);
}


// (the radio's CRC and read errors: a weak or colliding signal heard)
static long
RadioErrors(void*)
{
	return (long) gDriver.getPacketsRecvErrors();
}


// The radio out of receive (the engine's loop, each time round): the chip's
// own mode read every 100 ms (TDeckRadioHal::ReadStatus, bits 6-4: 2/3 standby, 4 frequency
// synthesis, 5 receiving, 6 transmitting).  Standing by is brief between a
// packet and the next startReceive, or a send and the next; for more than a
// second while MeshCore's wrapper thinks it is receiving, the radio is deaf
// and nothing in MeshCore notices (its own check sees only the wrapper's
// state) - it is put back into receive.
static long			gIdleRestarts = 0;
static long			gIdleMs = 0;
static int64_t		gIdleSince = 0, gLastModeCheck = 0;

static void
CheckMode(void)
{
	int64_t now = esp_timer_get_time();
	if (now - gLastModeCheck < 100 * 1000)
		return;
	gLastModeCheck = now;
	if (!gDriver.isInRecvMode())
	{
		gIdleSince = 0;							// (sending, or between: MeshCore's business)
		return;
	}
	uint8_t mode = (gHal.ReadStatus() >> 4) & 7;
	if (mode == 0)
		return;									// (not read: BUSY stayed up)
	if (mode == 5 || mode == 6)
	{
		gIdleSince = 0;
		return;
	}
	if (gIdleSince == 0)
		gIdleSince = now;
	else if (now - gIdleSince > 1000 * 1000)
	{
		gIdleRestarts++;
		gIdleMs += (long) ((now - gIdleSince) / 1000);
		printf("[mesh] the radio was out of receive (mode %d) for %ld ms: put back (%ld so far)\n", mode,
			   (long) ((now - gIdleSince) / 1000), gIdleRestarts);
		gIdleSince = 0;
		gRadio.startReceive();
	}
}


static long
RadioCheck(void*)
{
	if (!gRadioUp)
		return 0;
	CheckMode();
	return gHal.Check();
}


// (the SX1262's estimate, its register kept until the next packet)
static long
RadioFrequencyError(void*)
{
	return (long) gRadio.getFrequencyError();
}


// the packet just read read out of the radio's buffer again (its length
// and place from the buffer status, which holds until the next packet ends)
static int
RadioReread(void*, const uint8_t* raw, int len, int* first)
{
	uint8_t offset = 0;
	size_t length = gRadio.getPacketLength(true, &offset);
	if ((int) length != len || len > 255)
		return -1;							// (another packet since: not the same one)
	uint8_t again[256];
	if (gRadio.readBuffer(again, (uint8_t) len, offset) != RADIOLIB_ERR_NONE)
		return -1;
	int differs = 0;
	*first = -1;
	for (int k = 0; k < len; k++)
		if (again[k] != raw[k])
		{
			if (differs++ == 0)
				*first = k;
		}
	return differs;
}


static long
RadioIdle(void*, long* idleMs)
{
	*idleMs = gIdleMs;
	return gIdleRestarts;
}


// The engine's wait between looks: a task notification, given by a request
// (Wake) or by DIO1's interrupt (InterruptTrampoline) - so the chip can
// sleep, where the engine's own wait looked every 5 ms.  (A packet that comes
// while the chip sleeps raises DIO1 unseen - an edge does not wake it - and
// is found by the next look, RadioCheck, within the engine's longest wait.)
static void
Wait(void*, long ms)
{
	ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ms) > 0 ? pdMS_TO_TICKS(ms) : 1);
}


static void
Wake(void*)
{
	if (gMeshTask != NULL)
		xTaskNotifyGive(gMeshTask);
}


static void
MeshTask(void*)
{
	gMeshTask = xTaskGetCurrentTaskHandle();
	HostMeshRun(&gPlatform);
}


bool
HostDeviceLoraStart(void)
{
	HostDeviceBoardLineStart(kLineLoraPower);
	bool on = !KeptOff();
	HostDeviceBoardSet(kLineLoraPower, on);
	if (!on)
		printf("[mesh] the radio is switched off (the Host panel)\n");
	vTaskDelay(pdMS_TO_TICKS(10));

	gKeeperRequests = xQueueCreate(8, sizeof(KeeperRequest));
	gKeeperDone = xSemaphoreCreateBinary();
	if (gKeeperRequests == NULL || gKeeperDone == NULL
	 || xTaskCreatePinnedToCore(KeeperTask, "meshkeep", 3072, NULL, 4, NULL, 0) != pdPASS)
	{
		printf("[mesh] no room for the radio's keeper\n");
		return false;
	}
	HostMeshSetRandomSource(Random);
	HostMeshSetPowerSwitch(PowerSwitch, on);

	gPlatform.ref = NULL;
	gPlatform.radio = &gDriver;
	gPlatform.radioStart = RadioStart;
	gPlatform.radioSleep = RadioSleep;
	gPlatform.load = Load;
	gPlatform.save = Save;
	gPlatform.defaultName = "T-Deck Newton";
	gPlatform.radioErrors = RadioErrors;
	gPlatform.radioCheck = RadioCheck;
	gPlatform.radioIdle = RadioIdle;
	gPlatform.radioFrequencyError = RadioFrequencyError;
	gPlatform.radioReread = RadioReread;
	gPlatform.wait = Wait;
	gPlatform.wake = Wake;
	HostMeshWillRun();					// (the Newton's side boots while the radio starts)

	// the engine's task: its stack in PSRAM (core 0, beside the WiFi's and the
	// modem's; the Newton's are on core 1)
	const size_t kStack = 12 * 1024;
	StackType_t* stack = (StackType_t*) heap_caps_malloc(kStack, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	StaticTask_t* control = (StaticTask_t*) heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
	if (stack == NULL || control == NULL
	 || xTaskCreateStaticPinnedToCore(MeshTask, "mesh", kStack, NULL, 5, stack, control, 0) == NULL)
	{
		printf("[mesh] no room for the radio's task\n");
		return false;
	}
	return true;
}
