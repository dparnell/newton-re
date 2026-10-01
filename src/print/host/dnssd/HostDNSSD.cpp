/*
	File:		print/host/dnssd/HostDNSSD.cpp

	Contains:	The host's DNS-SD browse for IPP printers (HostDNSSD.h).

	Host only.
*/

#include "HostDNSSD.h"

#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <mutex>
#include <thread>
#include <chrono>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windns.h>
#endif

static std::mutex			gLock;
static HostDNSSDPrinter		gFound[64];
static int					gFoundCount = 0;
static int					gBusy = 0;
static char*				gInjected = NULL;
static int					gInjectionLooked = 0;


// a printer found, kept unless it is there already (by name)
static void
Found(const HostDNSSDPrinter* printer)
{
	std::lock_guard<std::mutex> hold(gLock);
	for (int i = 0; i < gFoundCount; i++)
		if (strcmp(gFound[i].fName, printer->fName) == 0)
			return;
	if (gFoundCount < (int) (sizeof(gFound) / sizeof(gFound[0])))
		gFound[gFoundCount++] = *printer;
}


// what a pdl key lists: PostScript, PCL
static void
Formats(const char* pdl, HostDNSSDPrinter* printer)
{
	// (media types are case-insensitive: HP's printers say vnd.hp-PCL)
	char lower[512];
	size_t n = 0;
	for (; pdl[n] != 0 && n + 1 < sizeof(lower); n++)
		lower[n] = (char) tolower((unsigned char) pdl[n]);
	lower[n] = 0;
	printer->fPostScript = strstr(lower, "application/postscript") != NULL;
	printer->fPCL = strstr(lower, "application/vnd.hp-pcl") != NULL;
}


// The URI a printer answers at: its address, port and the path its TXT
// record's rp gives (ipp/print when it gives none).
static void
MakeURI(HostDNSSDPrinter* printer, const char* address, int port, const char* rp)
{
	if (rp == NULL || rp[0] == 0)
		rp = "ipp/print";
	while (*rp == '/')
		rp++;
	snprintf(printer->fURI, sizeof(printer->fURI), "ipp://%s:%d/%s", address, port, rp);
}


void
HostDNSSDInject(const char* list)
{
	std::lock_guard<std::mutex> hold(gLock);
	free(gInjected);
	gInjected = (list != NULL && list[0] != 0) ? strdup(list) : NULL;
	gInjectionLooked = 1;
}


// "NAME|URI|FORMATS;..." taken as found
static void
TakeInjected(const char* list)
{
	const char* p = list;
	while (*p != 0)
	{
		const char* end = strchr(p, ';');
		size_t n = end != NULL ? (size_t) (end - p) : strlen(p);
		char entry[512];
		if (n >= sizeof(entry))
			n = sizeof(entry) - 1;
		memcpy(entry, p, n);
		entry[n] = 0;
		char* uri = strchr(entry, '|');
		if (uri != NULL)
		{
			*uri++ = 0;
			char* formats = strchr(uri, '|');
			if (formats != NULL)
				*formats++ = 0;
			HostDNSSDPrinter printer;
			memset(&printer, 0, sizeof(printer));
			snprintf(printer.fName, sizeof(printer.fName), "%s", entry);
			snprintf(printer.fURI, sizeof(printer.fURI), "%s", uri);
			if (formats != NULL)
			{
				printer.fPostScript = strstr(formats, "ps") != NULL;
				printer.fPCL = strstr(formats, "pcl") != NULL;
			}
			Found(&printer);
		}
		p += n;
		if (*p == ';')
			p++;
	}
}


/*------------------------------------------------------------------------------
	W i n d o w s :   D n s S e r v i c e B r o w s e
------------------------------------------------------------------------------*/

#if defined(_WIN32)

struct ResolveContext
{
	HANDLE				fDone;
	HostDNSSDPrinter	fPrinter;
	int					fGot;
};

static char gInstances[64][256];
static int gInstanceCount = 0;
static std::mutex gInstancesLock;

static void
ToUTF8(PCWSTR wide, char* out, size_t size)
{
	if (wide == NULL)
	{
		out[0] = 0;
		return;
	}
	if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, (int) size, NULL, NULL) == 0)
		out[0] = 0;
}

static VOID WINAPI
BrowseCallback(DWORD status, PVOID /*context*/, PDNS_RECORD record)
{
	if (status != ERROR_SUCCESS || record == NULL)
		return;
	for (PDNS_RECORDW r = (PDNS_RECORDW) record; r != NULL; r = r->pNext)
	{
		if (r->wType != DNS_TYPE_PTR)
			continue;
		char name[256];
		ToUTF8(r->Data.PTR.pNameHost, name, sizeof(name));
		std::lock_guard<std::mutex> hold(gInstancesLock);
		int known = 0;
		for (int i = 0; i < gInstanceCount; i++)
			if (strcmp(gInstances[i], name) == 0)
				known = 1;
		if (!known && gInstanceCount < 64)
			strcpy(gInstances[gInstanceCount++], name);
	}
	DnsRecordListFree(record, DnsFreeRecordList);
}

static VOID WINAPI
ResolveCallback(DWORD status, PVOID context, PDNS_SERVICE_INSTANCE instance)
{
	ResolveContext* c = (ResolveContext*) context;
	if (status == ERROR_SUCCESS && instance != NULL)
	{
		char host[256];
		char rp[128] = "";
		char pdl[512] = "";
		ToUTF8(instance->pszHostName, host, sizeof(host));
		if (instance->ip4Address != NULL)
		{
			const unsigned char* a = (const unsigned char*) instance->ip4Address;
			snprintf(host, sizeof(host), "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
		}
		for (DWORD i = 0; i < instance->dwPropertyCount; i++)
		{
			char key[64], value[512];
			ToUTF8(instance->keys[i], key, sizeof(key));
			ToUTF8(instance->values[i], value, sizeof(value));
			if (strcmp(key, "rp") == 0)
				snprintf(rp, sizeof(rp), "%s", value);
			else if (strcmp(key, "pdl") == 0)
				snprintf(pdl, sizeof(pdl), "%s", value);
		}
		MakeURI(&c->fPrinter, host, instance->wPort, rp);
		Formats(pdl, &c->fPrinter);
		c->fGot = 1;
		DnsServiceFreeInstance(instance);
	}
	SetEvent(c->fDone);
}

static void
PlatformBrowse(void)
{
	{
		std::lock_guard<std::mutex> hold(gInstancesLock);
		gInstanceCount = 0;
	}
	DNS_SERVICE_BROWSE_REQUEST request;
	memset(&request, 0, sizeof(request));
	request.Version = DNS_QUERY_REQUEST_VERSION1;
	request.InterfaceIndex = 0;
	request.QueryName = L"_ipp._tcp.local";
	request.pBrowseCallback = BrowseCallback;
	request.pQueryContext = NULL;
	DNS_SERVICE_CANCEL cancel;
	memset(&cancel, 0, sizeof(cancel));
	if (DnsServiceBrowse(&request, &cancel) != DNS_REQUEST_PENDING)
		return;
	Sleep(2000);			// (mDNS answers come within a second or so)
	DnsServiceBrowseCancel(&cancel);
	int count;
	{
		std::lock_guard<std::mutex> hold(gInstancesLock);
		count = gInstanceCount;
	}
	for (int i = 0; i < count; i++)
	{
		WCHAR wide[256];
		if (MultiByteToWideChar(CP_UTF8, 0, gInstances[i], -1, wide, 256) == 0)
			continue;
		ResolveContext context;
		memset(&context, 0, sizeof(context));
		context.fDone = CreateEventW(NULL, TRUE, FALSE, NULL);
		// the instance's name is the part before "._ipp._tcp.local"
		snprintf(context.fPrinter.fName, sizeof(context.fPrinter.fName), "%s", gInstances[i]);
		char* dot = strstr(context.fPrinter.fName, "._ipp.");
		if (dot != NULL)
			*dot = 0;
		DNS_SERVICE_RESOLVE_REQUEST resolve;
		memset(&resolve, 0, sizeof(resolve));
		resolve.Version = DNS_QUERY_REQUEST_VERSION1;
		resolve.QueryName = wide;
		resolve.pResolveCompletionCallback = ResolveCallback;
		resolve.pQueryContext = &context;
		DNS_SERVICE_CANCEL resolveCancel;
		memset(&resolveCancel, 0, sizeof(resolveCancel));
		if (DnsServiceResolve(&resolve, &resolveCancel) == DNS_REQUEST_PENDING)
		{
			if (WaitForSingleObject(context.fDone, 3000) != WAIT_OBJECT_0)
			{
				DnsServiceResolveCancel(&resolveCancel);
				WaitForSingleObject(context.fDone, 1000);
			}
			if (context.fGot)
				Found(&context.fPrinter);
		}
		CloseHandle(context.fDone);
	}
}

#elif defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)

/*------------------------------------------------------------------------------
	L i n u x   a n d   t h e   B S D s :   a v a h i - b r o w s e
------------------------------------------------------------------------------*/

// avahi-browse's parsable output escapes a byte as \DDD (decimal)
static void
Unescape(char* s)
{
	char* out = s;
	while (*s != 0)
	{
		if (s[0] == '\\' && s[1] >= '0' && s[1] <= '9' && s[2] >= '0' && s[2] <= '9' && s[3] >= '0' && s[3] <= '9')
		{
			*out++ = (char) ((s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0'));
			s += 4;
		}
		else
			*out++ = *s++;
	}
	*out = 0;
}

static void
PlatformBrowse(void)
{
	FILE* avahi = popen("avahi-browse --resolve --parsable --terminate _ipp._tcp 2>/dev/null", "r");
	if (avahi == NULL)
		return;
	char line[2048];
	while (fgets(line, sizeof(line), avahi) != NULL)
	{
		// =;interface;protocol;name;type;domain;host;address;port;txt
		if (line[0] != '=')
			continue;
		char* fields[10];
		int n = 0;
		char* p = line;
		while (n < 10)
		{
			fields[n++] = p;
			char* semi = (n < 10) ? strchr(p, ';') : NULL;
			if (semi == NULL)
				break;
			*semi = 0;
			p = semi + 1;
		}
		if (n < 10 || strcmp(fields[2], "IPv4") != 0)
			continue;
		HostDNSSDPrinter printer;
		memset(&printer, 0, sizeof(printer));
		Unescape(fields[3]);
		snprintf(printer.fName, sizeof(printer.fName), "%s", fields[3]);
		const char* txt = fields[9];
		char rp[128] = "";
		const char* r = strstr(txt, "\"rp=");
		if (r != NULL)
		{
			r += 4;
			size_t k = 0;
			while (*r != 0 && *r != '"' && k + 1 < sizeof(rp))
				rp[k++] = *r++;
			rp[k] = 0;
		}
		const char* pdl = strstr(txt, "\"pdl=");
		Formats(pdl != NULL ? pdl : "", &printer);
		MakeURI(&printer, fields[7], atoi(fields[8]), rp);
		Found(&printer);
	}
	pclose(avahi);
}

#else

static void
PlatformBrowse(void)
{
}

#endif


/*------------------------------------------------------------------------------
	T h e   l o o k u p
------------------------------------------------------------------------------*/

void
HostDNSSDStart(void)
{
	char* injected;
	{
		std::lock_guard<std::mutex> hold(gLock);
		if (gBusy)
			return;
		if (!gInjectionLooked)
		{
			const char* list = getenv("NEWTON_FOUND_PRINTERS");
			gInjected = (list != NULL && list[0] != 0) ? strdup(list) : NULL;
			gInjectionLooked = 1;
		}
		gFoundCount = 0;
		injected = gInjected != NULL ? strdup(gInjected) : NULL;
		if (injected == NULL)
			gBusy = 1;
	}
	if (injected != NULL)
	{
		TakeInjected(injected);
		free(injected);
		return;
	}
	std::thread([]() {
		PlatformBrowse();
		std::lock_guard<std::mutex> hold(gLock);
		gBusy = 0;
	}).detach();
}


int
HostDNSSDBusy(void)
{
	std::lock_guard<std::mutex> hold(gLock);
	return gBusy;
}


int
HostDNSSDCount(void)
{
	std::lock_guard<std::mutex> hold(gLock);
	return gFoundCount;
}


int
HostDNSSDGet(int index, HostDNSSDPrinter* printer)
{
	std::lock_guard<std::mutex> hold(gLock);
	if (index < 0 || index >= gFoundCount)
		return 0;
	*printer = gFound[index];
	return 1;
}


int
HostDNSSDFind(const char* name, HostDNSSDPrinter* printer)
{
	std::lock_guard<std::mutex> hold(gLock);
	for (int i = 0; i < gFoundCount; i++)
		if (strcmp(gFound[i].fName, name) == 0)
		{
			*printer = gFound[i];
			return 1;
		}
	return 0;
}


int
HostDNSSDResolve(const char* name, HostDNSSDPrinter* printer, int milliseconds)
{
	if (HostDNSSDFind(name, printer))
		return 1;
	if (!HostDNSSDBusy())
		HostDNSSDStart();
	for (int waited = 0; waited < milliseconds; waited += 50)
	{
		if (HostDNSSDFind(name, printer))
			return 1;
		if (!HostDNSSDBusy())
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	return HostDNSSDFind(name, printer);
}
