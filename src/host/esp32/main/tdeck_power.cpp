/*
	File:		host/esp32/main/tdeck_power.cpp

	Contains:	The ESP32-S3's power management (docs/host-tdeck-pro.md,
				"Battery life"): ESP-IDF's frequency scaling and automatic
				light sleep (sdkconfig.defaults: PM_ENABLE, tickless idle).

				The processor runs at 80 MHz unless a lock holds it at 240:
				the Newton holds one while it runs and lets it go while its
				idle task waits for the next deadline or the machine is
				asleep (hal/host/Host.h's gHostIdleHook); the drivers take
				their own while they work (SPI, I2C, the WiFi).  When every
				task is waiting, FreeRTOS's idle puts the chip in light
				sleep until the next tick that has work - unless something
				keeps it awake: the USB console while a host is connected
				(ESP-IDF's connection monitor, USJ_NO_AUTO_LS_ON_CONNECTION:
				the USB serial/JTAG does not work across light sleep - and
				since the monitor was seen to let it happen, the USB's power
				as well, tdeck_battery.cpp), and here the 4G module's UART
				while it is awake and the GPS's while a burst of its
				sentences comes (a UART's receiver stops in light sleep,
				and what they send meanwhile would be lost: tdeck_modem.cpp
				lets the module sleep, and tdeck_gps.cpp wakes the chip at
				each burst).  Light sleep can be
				switched off (DevicePowerSet('sleep, nil)).

				For measuring the board on its battery with light sleep
				(tools/tdeck/powermeter.py --usb-away): the USB can be let go
				for a time - the D+ pull-up taken away, so the computer sees
				the board unplugged, its connection monitor sees no frames
				and lets light sleep happen - and taken back, the chip kept
				awake until the computer has found it again; if it has not
				in 15 s the board restarts (so as not to be left out of
				reach: the first try, with the port still held open by the
				program measuring, never came back), and says so with the
				USB's registers at its next start.

				How long the Newton was idle since last asked is counted, for
				DevicePower.  ESP-IDF's headers only.
*/

#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "driver/usb_serial_jtag.h"
#include "soc/usb_serial_jtag_struct.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/system_reg.h"

#include "esp32host.h"

extern void	(*gHostIdleHook)(bool idle);		// hal/host/Host.h

static esp_pm_lock_handle_t	gNewtonRuns = NULL;	// 240 MHz while the Newton runs
static esp_pm_lock_handle_t	gKeptAwake = NULL;	// no light sleep while a keeper wants it
static portMUX_TYPE			gLock = portMUX_INITIALIZER_UNLOCKED;
static unsigned				gKeepers = 0;		// kAwake... bits
static bool					gLightSleep = true;
static bool					gStarted = false;
static SemaphoreHandle_t	gSleepWake = NULL;	// the Newton asleep: given by its wake

// the Newton's idle time since last asked (microseconds), and how often it
// went idle
static int64_t				gIdleSince = 0;
static int64_t				gIdleSum = 0;
static int64_t				gCountedFrom = 0;
static long					gIdleCount = 0;

// what the USB's registers held when it was taken back, kept across a
// restart (printed at the next start when the computer never found it)
static RTC_NOINIT_ATTR uint32_t	gUsbReturn[4];
const uint32_t					kUsbReturnMagic = 0x55534221;


static void
IdleHook(bool idle)
{
	int64_t now = esp_timer_get_time();
	if (idle)
	{
		portENTER_CRITICAL(&gLock);
		gIdleSince = now;
		gIdleCount++;
		portEXIT_CRITICAL(&gLock);
		esp_pm_lock_release(gNewtonRuns);
	}
	else
	{
		esp_pm_lock_acquire(gNewtonRuns);
		portENTER_CRITICAL(&gLock);
		if (gIdleSince != 0)
			gIdleSum += now - gIdleSince;
		gIdleSince = 0;
		portEXIT_CRITICAL(&gLock);
	}
}


static bool
Configure(void)
{
	esp_pm_config_t c = {};
	c.max_freq_mhz = 240;
	c.min_freq_mhz = 80;					// (the APB's own 80 MHz: the peripherals' clocks unchanged)
	c.light_sleep_enable = gLightSleep;
	esp_err_t err = esp_pm_configure(&c);
	if (err != ESP_OK)
		printf("newton: power management not configured (%d)\n", (int) err);
	return err == ESP_OK;
}


bool
HostDevicePowerStart(void)
{
	if (esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "newton", &gNewtonRuns) != ESP_OK
	 || esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "keepers", &gKeptAwake) != ESP_OK)
	{
		printf("newton: no power management locks\n");
		return false;
	}
	esp_pm_lock_acquire(gNewtonRuns);		// (the Newton is starting)
	gSleepWake = xSemaphoreCreateBinary();
	gKeepers = kAwakeUsbPower;				// (until tdeck_battery.cpp has asked the charger)
	esp_pm_lock_acquire(gKeptAwake);
	if (gUsbReturn[0] == kUsbReturnMagic)
		printf("newton: the USB was not found again after it was let go - restarted (conf0 %08lx, RTC USB_CONF %08lx, PERIP_CLK_EN1 %08lx)\n",
			   (unsigned long) gUsbReturn[1], (unsigned long) gUsbReturn[2], (unsigned long) gUsbReturn[3]);
	gUsbReturn[0] = 0;
	gCountedFrom = esp_timer_get_time();
	gHostIdleHook = IdleHook;
	gStarted = Configure();
	return gStarted;
}


void
HostDevicePowerKeepAwake(unsigned who, bool on)
{
	if (gKeptAwake == NULL)
		return;
	portENTER_CRITICAL(&gLock);
	unsigned was = gKeepers;
	gKeepers = on ? was | who : was & ~who;
	unsigned now = gKeepers;
	portEXIT_CRITICAL(&gLock);
	if (was == 0 && now != 0)
		esp_pm_lock_acquire(gKeptAwake);
	else if (was != 0 && now == 0)
		esp_pm_lock_release(gKeptAwake);
}


bool
HostDevicePowerSetLightSleep(bool on)
{
	gLightSleep = on;
	return gStarted && Configure();
}


void
HostDevicePowerIdle(long* perMille, long* idles)
{
	int64_t now = esp_timer_get_time();
	portENTER_CRITICAL(&gLock);
	int64_t sum = gIdleSum;
	if (gIdleSince != 0)
	{
		sum += now - gIdleSince;
		gIdleSince = now;
	}
	int64_t span = now - gCountedFrom;
	*idles = gIdleCount;
	gIdleSum = 0;
	gIdleCount = 0;
	gCountedFrom = now;
	portEXIT_CRITICAL(&gLock);
	*perMille = span > 0 ? (long) (sum * 1000 / span) : 0;
}


// The Newton asleep (hal/host/Power.cpp's PlatformPowerOffSystem): its task
// waits on a semaphore that its wake gives (gSleepWake, made at the start),
// rather than looking every 10 ms
void
HostDevicePowerSleepWait(unsigned long milliseconds)
{
	TickType_t ticks = pdMS_TO_TICKS(milliseconds);
	if (gSleepWake == NULL)
		vTaskDelay(ticks > 0 ? ticks : 1);
	else
		xSemaphoreTake(gSleepWake, ticks > 0 ? ticks : 1);
}


void
HostDevicePowerSleepWake(void)
{
	if (gSleepWake != NULL)
		xSemaphoreGive(gSleepWake);
}


// The console's !pm (esp32main.cpp): what keeps the chip awake, and ESP-IDF's
// profile of its locks and of the time in each mode (PM_PROFILING)
void
HostDevicePowerDump(void)
{
	printf("newton: keepers %02x, USB %s, light sleep %s\n", gKeepers,
		   usb_serial_jtag_is_connected() ? "connected" : "not connected", gLightSleep ? "allowed" : "off");
	HostDeviceGpsDump();
	HostDeviceInputDump();
	esp_pm_dump_locks(stdout);
}


// The console's !tasks (esp32main.cpp): each task's time on the processor
// since last asked (FreeRTOS's run-time stats, GENERATE_RUN_TIME_STATS), as
// a share of one core's time, largest first - the idle tasks' share is the
// time a core had nothing to do
void
HostDevicePowerTasks(void)
{
	struct Was { TaskHandle_t task; uint32_t runTime; };
	const int kMax = 64;
	static Was was[kMax];
	static int wasCount = 0;
	static int64_t wasAt = 0;
	UBaseType_t room = uxTaskGetNumberOfTasks() + 4;
	if (room > kMax)
		room = kMax;
	TaskStatus_t* tasks = (TaskStatus_t*) malloc(room * sizeof(TaskStatus_t));
	if (tasks == NULL)
		return;
	int count = (int) uxTaskGetSystemState(tasks, room, NULL);
	int64_t now = esp_timer_get_time();
	int64_t span = now - wasAt;
	uint32_t delta[kMax];
	for (int i = 0; i < count; i++)
	{
		uint32_t before = 0;
		for (int j = 0; j < wasCount; j++)
			if (was[j].task == tasks[i].xHandle)
				before = was[j].runTime;
		delta[i] = tasks[i].ulRunTimeCounter - before;
	}
	// (largest first)
	for (int i = 1; i < count; i++)
		for (int j = i; j > 0 && delta[j] > delta[j - 1]; j--)
		{
			uint32_t d = delta[j]; delta[j] = delta[j - 1]; delta[j - 1] = d;
			TaskStatus_t t = tasks[j]; tasks[j] = tasks[j - 1]; tasks[j - 1] = t;
		}
	printf("newton: the tasks' time over the last %lld ms (a share of one core)\n", (long long) (span / 1000));
	for (int i = 0; i < count; i++)
	{
		if (delta[i] == 0)
			continue;
		BaseType_t core = xTaskGetAffinity(tasks[i].xHandle);
		printf("  %-16s core %s %8lu us %5.1f%%\n", tasks[i].pcTaskName, core == tskNO_AFFINITY ? "-" : core == 0 ? "0" : "1",
			   (unsigned long) delta[i], span > 0 ? 100.0 * delta[i] / span : 0.0);
	}
	wasCount = count;
	for (int i = 0; i < count; i++)
		was[i] = { tasks[i].xHandle, tasks[i].ulRunTimeCounter };
	wasAt = now;
	free(tasks);
}


/*------------------------------------------------------------------------------
	The USB let go for a time
------------------------------------------------------------------------------*/

static volatile unsigned	gUsbAwayGeneration = 0;		// one more each time the USB is let go

static void
UsbAwayTask(void* arg)
{
	long seconds = (long) arg;
	printf("newton: the USB let go for %ld s (conf0 %08lx, RTC USB_CONF %08lx, PERIP_CLK_EN1 %08lx)\n", seconds,
		   (unsigned long) USB_SERIAL_JTAG.conf0.val, (unsigned long) REG_READ(RTC_CNTL_USB_CONF_REG), (unsigned long) REG_READ(SYSTEM_PERIP_CLK_EN1_REG));
	vTaskDelay(pdMS_TO_TICKS(2000));		// (that line, and the prompt's answer, sent)
	USB_SERIAL_JTAG.conf0.pad_pull_override = 1;
	USB_SERIAL_JTAG.conf0.dp_pullup = 0;
	vTaskDelay(pdMS_TO_TICKS(seconds * 1000));
	HostDevicePowerKeepAwake(kAwakeUsb, true);
	gUsbReturn[0] = kUsbReturnMagic;
	gUsbReturn[1] = USB_SERIAL_JTAG.conf0.val;
	gUsbReturn[2] = REG_READ(RTC_CNTL_USB_CONF_REG);
	gUsbReturn[3] = REG_READ(SYSTEM_PERIP_CLK_EN1_REG);
	USB_SERIAL_JTAG.conf0.dp_pullup = 1;
	USB_SERIAL_JTAG.conf0.pad_pull_override = 0;
	// (the computer given 15 s to find it - then the connection monitor
	// holds it awake - else the board restarts, so as not to be left
	// unreachable)
	for (int i = 0; i < 150 && !usb_serial_jtag_is_connected(); i++)
		vTaskDelay(pdMS_TO_TICKS(100));
	if (!usb_serial_jtag_is_connected())
		esp_restart();
	gUsbReturn[0] = 0;
	printf("newton: the USB back\n");
	// kept awake now until the USB is let go again, for 10 minutes at most:
	// the cable is in, but while the charger's input is cut for measuring
	// tdeck_battery.cpp does not hold light sleep off, and the USB does not
	// survive it (seen: a measurement's next question never answered)
	unsigned generation = gUsbAwayGeneration;
	for (int i = 0; i < 6000 && gUsbAwayGeneration == generation; i++)
		vTaskDelay(pdMS_TO_TICKS(100));
	HostDevicePowerKeepAwake(kAwakeUsb, false);
	vTaskDelete(NULL);
}


bool
HostDevicePowerUsbAway(long seconds)
{
	if (seconds <= 0)
		return false;
	gUsbAwayGeneration++;
	return xTaskCreate(UsbAwayTask, "usbaway", 3072, (void*) seconds, 5, NULL) == pdPASS;
}
