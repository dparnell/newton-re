/*
	File:		host/esp32/main/esp32main.cpp

	Contains:	The ESP32-S3 host's entry (src/host/esp32/README.md): the
				board's side of the start - the USB serial/JTAG console made
				a blocking stdio stream, the baked ROM objects' partition
				mapped (frames/ROMImport.h), a task with room to boot the OS
				in - before NewtonMainOnDevice (newtonmain.cpp) does the
				Newton's side; and what the Newton's side asks of the board
				meanwhile: its tasks' threads (os600/kernel/host/TaskRuntime.cpp)
				and the console's lines.  ESP-IDF's headers only here, the
				Newton's only there: the two do not mix.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "driver/usb_serial_jtag.h"
#include "esp_vfs_dev.h"

#include "esp32host.h"
#include "tdeck_board.h"

static const void*	gImage = NULL;
bool				gHostDeviceWifi = false;		// (newtonmain.cpp: the WiFi is the host's - HostWifi.h)
static size_t		gImageSize = 0;


// malloc(0) a block rather than nil (main/CMakeLists.txt links every malloc
// through this): the desktops' C libraries answer a block, and code in the
// tree takes nil for out of memory - an extended Gestalt's first buffer is
// 0 bytes, and every one answered nil here
extern "C" void*	__real_malloc(size_t size);

extern "C" void*
__wrap_malloc(size_t size)
{
	return __real_malloc(size > 0 ? size : 1);
}


long long
HostDeviceMicroseconds(void)
{
	return (long long) esp_timer_get_time();
}


void
HostDeviceMemory(unsigned long* internalFree, unsigned long* psramFree)
{
	*internalFree = (unsigned long) heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
	*psramFree = (unsigned long) heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}


/*------------------------------------------------------------------------------
	The Newton's tasks' threads (os600/kernel/host/TaskRuntime.cpp): a FreeRTOS
	task each, its stack in PSRAM (the internal RAM has room for a few, and
	the Newton makes twenty-odd), all on core 1 at one priority - the
	runtime's baton lets one run at a time.  A deleted task's thread marks
	itself ended and suspends itself; the next thread started deletes it and
	gives back its stack and control block.  (Not a task deleting itself:
	FreeRTOS answers eDeleted for one as soon as it is queued for its idle
	task, which then still reads the control block - freed by then, and the
	heap was corrupt.  A suspended task another deletes is gone at once.)
------------------------------------------------------------------------------*/

struct DeviceThread
{
	TaskHandle_t	fHandle;
	StaticTask_t*	fControl;
	StackType_t*	fStack;
	bool			fEnded;
	DeviceThread*	fNext;
};

static DeviceThread*	gThreads = NULL;
static portMUX_TYPE		gThreadsLock = portMUX_INITIALIZER_UNLOCKED;
static const UBaseType_t	kNewtonPriority = 5;
static const BaseType_t		kNewtonCore = 1;


static void
ReapThreads(void)
{
	for (;;)
	{
		DeviceThread* gone = NULL;
		portENTER_CRITICAL(&gThreadsLock);
		for (DeviceThread** p = &gThreads; *p != NULL; p = &(*p)->fNext)
			if ((*p)->fEnded && eTaskGetState((*p)->fHandle) == eSuspended)
			{
				gone = *p;
				*p = gone->fNext;
				break;
			}
		portEXIT_CRITICAL(&gThreadsLock);
		if (gone == NULL)
			return;
		vTaskDelete(gone->fHandle);
		heap_caps_free(gone->fStack);
		heap_caps_free(gone->fControl);
		delete gone;
	}
}


int
HostDeviceStartThread(void (*entry)(void*), void* arg, size_t stackBytes)
{
	ReapThreads();
	DeviceThread* t = new DeviceThread;
	t->fStack = (StackType_t*) heap_caps_malloc(stackBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	t->fControl = (StaticTask_t*) heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
	t->fEnded = false;
	if (t->fStack == NULL || t->fControl == NULL)
	{
		heap_caps_free(t->fStack);
		heap_caps_free(t->fControl);
		delete t;
		return 0;
	}
	// (ESP-IDF's FreeRTOS counts a stack in bytes)
	t->fHandle = xTaskCreateStaticPinnedToCore(entry, "newt", (uint32_t) stackBytes, arg, kNewtonPriority,
											   t->fStack, t->fControl, kNewtonCore);
	portENTER_CRITICAL(&gThreadsLock);
	t->fNext = gThreads;
	gThreads = t;
	portEXIT_CRITICAL(&gThreadsLock);
	return 1;
}


void
HostDeviceEndThread(void)
{
	TaskHandle_t self = xTaskGetCurrentTaskHandle();
	portENTER_CRITICAL(&gThreadsLock);
	for (DeviceThread* t = gThreads; t != NULL; t = t->fNext)
		if (t->fHandle == self)
			t->fEnded = true;
	portEXIT_CRITICAL(&gThreadsLock);
	for (;;)
		vTaskSuspend(NULL);
}


/*------------------------------------------------------------------------------
	The console's lines: read by a task of the board's own, outside the
	Newton (a Newton task blocked in fgets would hold the baton, and stop
	the machine), and queued for the Newton to take when it looks.
------------------------------------------------------------------------------*/

const size_t		kLineSize = 512;
static QueueHandle_t	gLines = NULL;


static void
ConsoleReader(void*)
{
	static char line[kLineSize];
	for (;;)
	{
		if (fgets(line, sizeof(line), stdin) == NULL)
		{
			clearerr(stdin);
			vTaskDelay(pdMS_TO_TICKS(20));
		}
		// the board's own commands, answered here whatever the Newton is
		// doing (a Newton that is stuck cannot be asked through itself):
		// !sound the Max's sound measured (tdeck_audio.cpp),
		// !prof the profile so far (tdeck_profile.cpp), !pm the power
		// management's state and !tasks each task's time since last asked
		// (tdeck_power.cpp), !shot what the panel
		// shows (tdeck_panel.cpp), !time <seconds since 1970, GMT> the clock
		// set by hand (the Newton's too: tdeck_wifi.cpp), !tap <x> <y> the
		// finger on the display at (x, y) for 100 ms (as the touch panel puts
		// it, so a modal dialog can be answered from here), !reset a restart
		else if (strncmp(line, "!prof", 5) == 0)
			HostDeviceProfileDump();
		else if (strncmp(line, "!sound", 6) == 0)
			HostDeviceAudioTest();
		else if (strncmp(line, "!pm", 3) == 0)
			HostDevicePowerDump();
		else if (strncmp(line, "!tasks", 6) == 0)
			HostDevicePowerTasks();
		else if (strncmp(line, "!shot", 5) == 0)
			HostDevicePanelDump();
		else if (strncmp(line, "!time ", 6) == 0)
			HostDeviceClockSetTo(atoll(line + 6));
		else if (strncmp(line, "!tap ", 5) == 0)
		{
			char* rest;
			long x = strtol(line + 5, &rest, 10), y = strtol(rest, NULL, 10);
			HostWindowPenDown(x, y);
			vTaskDelay(pdMS_TO_TICKS(100));
			HostWindowPenUp();
		}
		else if (strncmp(line, "!reset", 6) == 0)
			esp_restart();
		else
		{
			xQueueSend(gLines, line, portMAX_DELAY);
			HostDeviceKick();
		}
	}
}



int
HostDeviceReadLine(char* line, size_t size)
{
	static char got[kLineSize];
	if (gLines == NULL || xQueueReceive(gLines, got, 0) != pdTRUE)
		return 0;
	strncpy(line, got, size - 1);
	line[size - 1] = 0;
	return 1;
}


/*------------------------------------------------------------------------------
	The start
------------------------------------------------------------------------------*/

extern "C" const void* gHostDeviceStackTop;		// (hal/host/System.cpp GetStackBounds)

static void
NewtonTask(void*)
{
	char top;
	gHostDeviceStackTop = &top + 64;				// (the task's frame and a little over)
	NewtonMainOnDevice(gImage, gImageSize);
	printf("newton: the OS has stopped\n");
	for (;;)
		vTaskDelay(pdMS_TO_TICKS(1000));
}


// The baked image is made for one address (frames/ROMImport.h), but ESP-IDF
// maps a partition at the first free page after the program's own and the
// PSRAM's - which moves as the program grows.  So the partition goes at a
// fixed address, kObjectsAddress, with the address space below it up to
// there taken by a mapping of the partition's own first pages (ESP-IDF maps
// flash shared: the partition's whole mapping, which encloses those pages,
// is a new one, above them).  0x3d000000 leaves the program 4 MB to grow
// in; the data space ends at 0x3e000000, 10 MB above.
static const uintptr_t	kObjectsAddress = 0x3d000000;

static const void*
MapObjects(const esp_partition_t* part)
{
	const void* probe;
	esp_partition_mmap_handle_t handle;
	if (esp_partition_mmap(part, 0, 0x10000, ESP_PARTITION_MMAP_DATA, &probe, &handle) != ESP_OK)
		return NULL;
	esp_partition_munmap(handle);
	uintptr_t first = (uintptr_t) probe;
	if (first > kObjectsAddress || kObjectsAddress - first >= part->size)
	{
		printf("newton: the program's mappings reach %p, past the objects' address %p\n", probe, (void*) kObjectsAddress);
		return NULL;
	}
	if (first < kObjectsAddress)
	{
		const void* pad;
		esp_partition_mmap_handle_t padHandle;
		if (esp_partition_mmap(part, 0, kObjectsAddress - first, ESP_PARTITION_MMAP_DATA, &pad, &padHandle) != ESP_OK || pad != probe)
			return NULL;
	}
	const void* image;
	if (esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA, &image, &handle) != ESP_OK)
		return NULL;
	if ((uintptr_t) image != kObjectsAddress)
		printf("newton: the objects partition went to %p, not %p\n", image, (void*) kObjectsAddress);
	return image;
}


// Espressif's libgcc: whether the unwinder may sort the exception tables'
// entries (a table of 4 bytes an entry, made at the first throw) - off
// unless asked for, and then every C++ throw counts and searches all
// ~22,000 entries one by one: 22 ms a throw, and the task runtime throws
// one at many task switches (TTaskRedirect) - 78% of the boot's minute,
// tools/tdeck/profile.py found
extern "C" void	_Unwind_SetEnableExceptionFdeSorting(unsigned char enable);


extern "C" void
app_main(void)
{
	// the console: blocking reads on stdin, through the USB serial/JTAG driver
	usb_serial_jtag_driver_config_t usb = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
	usb_serial_jtag_driver_install(&usb);
	esp_vfs_usb_serial_jtag_use_driver();
	setvbuf(stdin, NULL, _IONBF, 0);
	setvbuf(stdout, NULL, _IONBF, 0);
	vTaskDelay(pdMS_TO_TICKS(1500));		// (time for a terminal to open the port)

	printf("newton: ESP32-S3, PSRAM %u KB\n", (unsigned) (esp_psram_get_size() >> 10));
	const esp_partition_t* part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t) 0x41, "objects");
	if (part == NULL || (gImage = MapObjects(part)) == NULL)
		printf("newton: the objects partition could not be mapped\n");
	else
	{
		gImageSize = part->size;
		printf("newton: the objects partition mapped at %p (%u KB)\n", gImage, (unsigned) (gImageSize >> 10));
	}
	// the panel and the input first: their drivers want internal RAM, which
	// the OS leaves little of
	_Unwind_SetEnableExceptionFdeSorting(1);
	HostDeviceProfileStart();
	HostDevicePowerStart();				// (before the others: their drivers take its locks)
	HostDeviceBoardStart();				// (which board this is, before any driver takes a pin)
	HostDeviceSpiBusStart();				// (the panel's and the radio's)
	HostDevicePanelStart();
	HostDeviceInputStart();
	HostDeviceBatteryStart();				// (on the input's I2C bus)
	HostDeviceStoreStart();
	gHostDeviceWifi = HostDeviceWifiStart();
	HostDeviceCellStart();
	HostDeviceLoraStart();
	HostDeviceGpsStart();
	gLines = xQueueCreate(8, kLineSize);
	xTaskCreatePinnedToCore(ConsoleReader, "console", 4096, NULL, 4, NULL, 0);
	// the boot runs here, and the run's end is waited for here (the task
	// runtime's HostRunTasks): a task of the Newton's size, its stack in
	// PSRAM as its tasks' are (the internal RAM left has no 48 KB in one)
	if (!HostDeviceStartThread(NewtonTask, NULL, 48 * 1024))
		printf("newton: no room for the Newton's task\n");
}
