/*
	File:		host/esp32/main/tdeck_wifi.cpp

	Contains:	The board's WiFi (src/host/esp32/README.md), the provider
				of host/HostWifi.h: the networks to join are remembered in
				NVS (namespace "wifi", up to kMaxKnown, each its SSID and
				password), and the board joins whichever of them it finds -
				it scans, tries the remembered networks in range strongest
				first, and when it has none, or loses the one it had,
				scans again after kRetryMs.  Once joined, the NIE's TCP and
				DNS (comms/host) reach the network through lwIP's sockets,
				as a desktop's reach its own.

				One task of the board's own owns the WiFi: its stack is in
				internal RAM (remembering writes NVS, which writes the
				flash - a task with a PSRAM stack, as the Newton's are,
				must not), and the Newton's tasks hand it a scan, a
				remember or a forget and wait for the answer.  ESP-IDF's
				events (joined, lost) come to it too.
				ESP-IDF's headers only (esp32host.h says why).
*/

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_sntp.h"
#include <sys/time.h>
#include <stdlib.h>

#include "esp32host.h"

const int		kMaxKnown = 8;
const int		kMaxSeen = 24;
const int64_t	kRetryMs = 30000;			// no remembered network in range (or lost): scanned again after this
const int64_t	kJoinTimeoutMs = 20000;		// a join that has not got an address by then has failed

struct KnownNetwork
{
	char	ssid[33];
	char	password[65];
};

struct KnownList
{
	uint32_t		count;
	KnownNetwork	networks[kMaxKnown];
};

enum RequestKind { kScan, kRemember, kForget, kKnown, kRadio, kPowerSave };

struct Request
{
	RequestKind		kind;
	char			ssid[33];
	char			password[65];
	DeviceWifiSeen*	seen;			// kScan: where the networks go
	char			(*ssids)[33];	// kKnown
	long			max;			// (kRadio: on; kPowerSave: the wifi_ps_type_t)
	long			result;
	SemaphoreHandle_t	done;
};

// the state the Newton reads (HostDeviceWifiStatus)
static volatile int		gState = kDeviceWifiOff;
static char				gSSID[33];
static volatile uint32_t	gAddress = 0;

static volatile unsigned long	gClockSyncs = 0;	// (HostDeviceClockSyncs)
static QueueHandle_t	gRequests = NULL;
static QueueHandle_t	gEvents = NULL;			// (an int: what ESP-IDF said)
static KnownList		gKnown;
static bool				gRadioOff = false;		// switched off (HostDeviceWifiSetRadio): nothing joined
enum { kEventJoined = 1, kEventLost };


/*------------------------------------------------------------------------------
	The remembered networks
------------------------------------------------------------------------------*/

static void
LoadKnown(void)
{
	memset(&gKnown, 0, sizeof(gKnown));
	nvs_handle_t nvs;
	if (nvs_open("wifi", NVS_READONLY, &nvs) != ESP_OK)
		return;
	size_t size = sizeof(gKnown);
	if (nvs_get_blob(nvs, "known", &gKnown, &size) != ESP_OK || size != sizeof(gKnown) || gKnown.count > (uint32_t) kMaxKnown)
		memset(&gKnown, 0, sizeof(gKnown));
	nvs_close(nvs);
}


static bool
SaveKnown(void)
{
	nvs_handle_t nvs;
	if (nvs_open("wifi", NVS_READWRITE, &nvs) != ESP_OK)
		return false;
	bool ok = nvs_set_blob(nvs, "known", &gKnown, sizeof(gKnown)) == ESP_OK && nvs_commit(nvs) == ESP_OK;
	nvs_close(nvs);
	return ok;
}


static int
KnownIndex(const char* ssid)
{
	for (uint32_t i = 0; i < gKnown.count; i++)
		if (strcmp(gKnown.networks[i].ssid, ssid) == 0)
			return (int) i;
	return -1;
}


/*------------------------------------------------------------------------------
	Scanning and joining (the manager task)
------------------------------------------------------------------------------*/

static wifi_ap_record_t	gRecords[kMaxSeen];

// ==> how many networks (strongest first: ESP-IDF sorts them), -1 when it
// could not scan
static int
Scan(void)
{
	wifi_scan_config_t config;
	memset(&config, 0, sizeof(config));
	config.show_hidden = false;
	if (esp_wifi_scan_start(&config, true) != ESP_OK)
		return -1;
	uint16_t count = kMaxSeen;
	if (esp_wifi_scan_get_ap_records(&count, gRecords) != ESP_OK)
		return -1;
	return count;
}


static void	StartClock(void);

static bool
Join(const KnownNetwork& network)
{
	wifi_config_t config;
	memset(&config, 0, sizeof(config));
	strncpy((char*) config.sta.ssid, network.ssid, sizeof(config.sta.ssid));
	strncpy((char*) config.sta.password, network.password, sizeof(config.sta.password));
	config.sta.threshold.authmode = network.password[0] != 0 ? WIFI_AUTH_WEP : WIFI_AUTH_OPEN;
	if (esp_wifi_set_config(WIFI_IF_STA, &config) != ESP_OK)
		return false;
	strncpy(gSSID, network.ssid, sizeof(gSSID));
	gState = kDeviceWifiConnecting;
	int stale;
	while (xQueueReceive(gEvents, &stale, 0) == pdTRUE)
		;
	if (esp_wifi_connect() != ESP_OK)
		return false;
	// the answer: joined (an address) or lost, or nothing by the timeout
	int event;
	int64_t until = esp_timer_get_time() + kJoinTimeoutMs * 1000;
	while (esp_timer_get_time() < until)
		if (xQueueReceive(gEvents, &event, pdMS_TO_TICKS(500)) == pdTRUE)
		{
			if (event == kEventJoined)
			{
				printf("newton: WiFi: %s joined, address %lu.%lu.%lu.%lu\n", gSSID, (unsigned long) (gAddress >> 24),
					   (unsigned long) ((gAddress >> 16) & 0xFF), (unsigned long) ((gAddress >> 8) & 0xFF), (unsigned long) (gAddress & 0xFF));
				StartClock();
				return true;
			}
			if (event == kEventLost)
				return false;
		}
	esp_wifi_disconnect();
	return false;
}


// the remembered networks in range tried, strongest first ==> whether one
// was joined
static bool
JoinAny(void)
{
	gState = kDeviceWifiSearching;
	if (gKnown.count == 0)
		return false;
	int count = Scan();
	for (int i = 0; i < count; i++)
	{
		int k = KnownIndex((const char*) gRecords[i].ssid);
		if (k < 0)
			continue;
		printf("newton: WiFi: joining %s\n", gKnown.networks[k].ssid);
		if (Join(gKnown.networks[k]))
			return true;
		printf("newton: WiFi: %s would not be joined\n", gKnown.networks[k].ssid);
	}
	gState = kDeviceWifiSearching;
	gSSID[0] = 0;
	return false;
}


static void
Answer(Request* request)
{
	switch (request->kind)
	{
	case kScan:
		{
			// each network once: a network of several access points (a mesh)
			// answers from each, and the strongest - the first, the records
			// being in order - is the one kept
			int count = Scan();
			long n = 0;
			for (int i = 0; i < count && n < request->max; i++)
			{
				bool again = false;
				for (long j = 0; j < n && !again; j++)
					again = strcmp(request->seen[j].ssid, (const char*) gRecords[i].ssid) == 0;
				if (again || gRecords[i].ssid[0] == 0)
					continue;
				DeviceWifiSeen& seen = request->seen[n++];
				strncpy(seen.ssid, (const char*) gRecords[i].ssid, sizeof(seen.ssid));
				seen.ssid[sizeof(seen.ssid) - 1] = 0;
				seen.rssi = gRecords[i].rssi;
				seen.secure = gRecords[i].authmode != WIFI_AUTH_OPEN;
				seen.known = KnownIndex(seen.ssid) >= 0;
			}
			request->result = count < 0 ? -1 : n;
		}
		break;

	case kRemember:
		{
			int k = KnownIndex(request->ssid);
			if (k < 0)
			{
				if (gKnown.count == (uint32_t) kMaxKnown)
				{
					// the oldest forgotten for it
					memmove(&gKnown.networks[0], &gKnown.networks[1], sizeof(KnownNetwork) * (kMaxKnown - 1));
					gKnown.count--;
				}
				k = (int) gKnown.count++;
			}
			strncpy(gKnown.networks[k].ssid, request->ssid, sizeof(gKnown.networks[k].ssid));
			strncpy(gKnown.networks[k].password, request->password, sizeof(gKnown.networks[k].password));
			request->result = SaveKnown();
		}
		break;

	case kForget:
		{
			int k = KnownIndex(request->ssid);
			request->result = k >= 0;
			if (k >= 0)
			{
				memmove(&gKnown.networks[k], &gKnown.networks[k + 1], sizeof(KnownNetwork) * (gKnown.count - k - 1));
				gKnown.count--;
				SaveKnown();
				if (strcmp(gSSID, request->ssid) == 0 && gState != kDeviceWifiSearching)
					esp_wifi_disconnect();
			}
		}
		break;

	case kKnown:
		{
			long n = 0;
			for (uint32_t i = 0; i < gKnown.count && n < request->max; i++, n++)
				strncpy(request->ssids[n], gKnown.networks[i].ssid, 33);
			request->result = n;
		}
		break;

	case kRadio:
		if (request->max != 0 && gRadioOff)
		{
			request->result = esp_wifi_start() == ESP_OK;
			gRadioOff = !request->result;
			gState = kDeviceWifiSearching;
		}
		else if (request->max == 0 && !gRadioOff)
		{
			esp_wifi_disconnect();
			request->result = esp_wifi_stop() == ESP_OK;
			gRadioOff = request->result;
			gState = kDeviceWifiOff;
			gAddress = 0;
			gSSID[0] = 0;
		}
		else
			request->result = true;
		printf("newton: WiFi %s\n", gRadioOff ? "switched off" : "switched on");
		break;

	case kPowerSave:
		request->result = esp_wifi_set_ps((wifi_ps_type_t) request->max) == ESP_OK;
		printf("newton: WiFi power save %s\n", request->max == WIFI_PS_NONE ? "off" : request->max == WIFI_PS_MIN_MODEM ? "min modem" : "max modem");
		break;
	}
}


static void
ManagerTask(void*)
{
	int64_t nextTry = 0;					// (at once)
	for (;;)
	{
		Request* request;
		if (xQueueReceive(gRequests, &request, pdMS_TO_TICKS(500)) == pdTRUE)
		{
			Answer(request);
			bool tryNow = (request->kind == kRemember || request->kind == kRadio) && gState != kDeviceWifiConnected;
			xSemaphoreGive(request->done);
			if (tryNow)
				nextTry = 0;
		}
		int event;
		while (xQueueReceive(gEvents, &event, 0) == pdTRUE)
			if (event == kEventLost && gState == kDeviceWifiConnected)
			{
				printf("newton: WiFi: %s lost\n", gSSID);
				gState = kDeviceWifiSearching;
				gAddress = 0;
				nextTry = esp_timer_get_time() + 2000 * 1000;
			}
		if (!gRadioOff && gState != kDeviceWifiConnected && esp_timer_get_time() >= nextTry)
		{
			if (!JoinAny())
				nextTry = esp_timer_get_time() + kRetryMs * 1000;
		}
	}
}


// The board's clock set from the network (SNTP, pool.ntp.org) once it has
// joined one, and again each hour (ESP-IDF's default); each time, the
// Newton's own clock is set from it (newtonmain.cpp's services loop asks
// HostDeviceClockSyncs).  The board's clock survives a restart, not a loss
// of power.
static void
OnClockSet(struct timeval*)
{
	gClockSyncs++;
}


static void
StartClock(void)
{
	static bool started = false;
	if (started)
		return;
	started = true;
	esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
	esp_sntp_setservername(0, "pool.ntp.org");
	sntp_set_time_sync_notification_cb(OnClockSet);
	esp_sntp_init();
}


// (on ESP-IDF's event task, whose stack is small - 2304 bytes: a printf or
//  SNTP's start here overflowed it; the rest is the manager's, in Join)
static void
OnEvent(void*, esp_event_base_t base, int32_t id, void* data)
{
	int event = 0;
	if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
	{
		gAddress = 0;
		event = kEventLost;
	}
	else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
	{
		const ip_event_got_ip_t* got = (const ip_event_got_ip_t*) data;
		gAddress = ((uint32_t) esp_ip4_addr1(&got->ip_info.ip) << 24) | ((uint32_t) esp_ip4_addr2(&got->ip_info.ip) << 16)
				 | ((uint32_t) esp_ip4_addr3(&got->ip_info.ip) << 8) | esp_ip4_addr4(&got->ip_info.ip);
		gState = kDeviceWifiConnected;
		event = kEventJoined;
	}
	if (event != 0)
		xQueueSend(gEvents, &event, 0);
}


/*------------------------------------------------------------------------------
	The board's side of HostWifi.h (esp32host.h)
------------------------------------------------------------------------------*/

bool
HostDeviceWifiStart(void)
{
	if (nvs_flash_init() != ESP_OK)
		return false;
	LoadKnown();
	if (esp_netif_init() != ESP_OK)
		return false;
	esp_err_t err = esp_event_loop_create_default();
	if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
		return false;
	esp_netif_create_default_wifi_sta();
	wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
	if ((err = esp_wifi_init(&init)) != ESP_OK)
	{
		printf("newton: no WiFi (%s)\n", esp_err_to_name(err));
		return false;
	}
	esp_wifi_set_storage(WIFI_STORAGE_RAM);			// (the networks are kept by LoadKnown/SaveKnown)
	esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, OnEvent, NULL);
	esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, OnEvent, NULL);
	if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK || esp_wifi_start() != ESP_OK)
		return false;
	gRequests = xQueueCreate(4, sizeof(Request*));
	gEvents = xQueueCreate(8, sizeof(int));
	gState = kDeviceWifiSearching;
	if (gRequests == NULL || gEvents == NULL
	|| xTaskCreatePinnedToCore(ManagerTask, "wifi", 4096, NULL, 4, NULL, 0) != pdPASS)
		return false;
	printf("newton: WiFi started, %u networks remembered\n", (unsigned) gKnown.count);
	return true;
}


static long
Ask(Request& request)
{
	if (gRequests == NULL)
		return -1;
	request.done = xSemaphoreCreateBinary();
	if (request.done == NULL)
		return -1;
	Request* p = &request;
	xQueueSend(gRequests, &p, portMAX_DELAY);
	xSemaphoreTake(request.done, portMAX_DELAY);
	vSemaphoreDelete(request.done);
	return request.result;
}


long
HostDeviceWifiScan(DeviceWifiSeen* seen, long max)
{
	Request request;
	memset(&request, 0, sizeof(request));
	request.kind = kScan;
	request.seen = seen;
	request.max = max;
	return Ask(request);
}


long
HostDeviceWifiKnown(char (*ssids)[33], long max)
{
	Request request;
	memset(&request, 0, sizeof(request));
	request.kind = kKnown;
	request.ssids = ssids;
	request.max = max;
	long n = Ask(request);
	return n < 0 ? 0 : n;
}


bool
HostDeviceWifiRemember(const char* ssid, const char* password)
{
	Request request;
	memset(&request, 0, sizeof(request));
	request.kind = kRemember;
	strncpy(request.ssid, ssid, sizeof(request.ssid) - 1);
	strncpy(request.password, password != NULL ? password : "", sizeof(request.password) - 1);
	return Ask(request) > 0;
}


bool
HostDeviceWifiForget(const char* ssid)
{
	Request request;
	memset(&request, 0, sizeof(request));
	request.kind = kForget;
	strncpy(request.ssid, ssid, sizeof(request.ssid) - 1);
	return Ask(request) > 0;
}


bool
HostDeviceWifiSetRadio(bool on)
{
	Request request;
	memset(&request, 0, sizeof(request));
	request.kind = kRadio;
	request.max = on;
	return Ask(request) > 0;
}


bool
HostDeviceWifiSetPowerSave(int mode)
{
	if (mode < WIFI_PS_NONE || mode > WIFI_PS_MAX_MODEM)
		return false;
	Request request;
	memset(&request, 0, sizeof(request));
	request.kind = kPowerSave;
	request.max = mode;
	return Ask(request) > 0;
}


void
HostDeviceWifiStatus(int* state, char* ssid, unsigned long* address, long* rssi)
{
	*state = gState;
	strncpy(ssid, gState == kDeviceWifiOff ? "" : gSSID, 33);
	*address = gAddress;
	*rssi = 0;
	wifi_ap_record_t ap;
	if (gState == kDeviceWifiConnected && esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
		*rssi = ap.rssi;
}


unsigned long
HostDeviceClockSyncs(void)
{
	return gClockSyncs;
}


// the board's clock set by hand (the console's !time) - as a sync from the
// network is, the Newton's set from it
void
HostDeviceClockSetTo(long long seconds)
{
	struct timeval tv;
	tv.tv_sec = (time_t) seconds;
	tv.tv_usec = 0;
	settimeofday(&tv, NULL);
	gClockSyncs++;
}
