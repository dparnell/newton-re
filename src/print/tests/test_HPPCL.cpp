// Host unit test for the HP PCL driver (print/HPPCL.h): ThpPCL's page
// brackets and bands, its endpoint a test one that keeps what is sent.  A
// page opened (the reset, 300 dpi, the raster's size), three bands imaged -
// a blank one, one with black rows in its middle, one cut short by a
// cancel - and closed; the PCL is then read back: the commands in order,
// the white rows skipped by count, and each row of black unpacked from
// PackBits and compared with the band it came from.  Also the page and
// band preferences and the problem codes.

#include "print/HPPCL.h"
#include "Endpoint.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "NewtonMemory.h"
#include "Ports.h"
#include "Draw.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <new>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static unsigned char	gSent[200000];
static size_t			gSentSize = 0;


// An endpoint that only takes what is sent (ThpPCL only ever sends).
class TTestEndpoint : public TEndpoint
{
public:
	Boolean		HandleEvent(ULong, TAEvent*, ULong) { return false; }
	Boolean		HandleComplete(TUMsgToken*, ULong*, TAEvent*) { return false; }
	NewtonErr	AddToAppWorld(void) { return noErr; }
	NewtonErr	RemoveFromAppWorld(void) { return noErr; }
	NewtonErr	Open(ULong) { return noErr; }
	NewtonErr	Close(void) { return noErr; }
	NewtonErr	Abort(void) { return noErr; }
	Boolean		SetSync(Boolean) { return true; }
	NewtonErr	GetProtAddr(TOptionArray*, TOptionArray*, TTimeout) { return noErr; }
	NewtonErr	OptMgmt(ULong, TOptionArray*, TTimeout) { return noErr; }
	NewtonErr	Bind(TOptionArray*, Long*, TTimeout) { return noErr; }
	NewtonErr	UnBind(TTimeout) { return noErr; }
	NewtonErr	Listen(TOptionArray*, TOptionArray*, CBufferSegment*, Long*, TTimeout) { return noErr; }
	NewtonErr	Accept(TEndpoint*, TOptionArray*, TOptionArray*, CBufferSegment*, Long, TTimeout) { return noErr; }
	NewtonErr	Connect(TOptionArray*, TOptionArray*, CBufferSegment*, Long*, TTimeout) { return noErr; }
	NewtonErr	Disconnect(CBufferSegment*, Long, Long) { return noErr; }
	NewtonErr	Release(TTimeout) { return noErr; }
	NewtonErr	Snd(UByte* buf, Size& nBytes, ULong, TTimeout)
	{
		if (gSentSize + nBytes <= sizeof(gSent))
		{
			memcpy(gSent + gSentSize, buf, nBytes);
			gSentSize += nBytes;
		}
		return noErr;
	}
	NewtonErr	Rcv(UByte*, Size&, Size, ULong*, TTimeout) { return noErr; }
	NewtonErr	Snd(CBufferSegment*, ULong, TTimeout) { return noErr; }
	NewtonErr	Rcv(CBufferSegment*, Size, ULong*, TTimeout) { return noErr; }
	NewtonErr	WaitForEvent(TTimeout) { return noErr; }
	NewtonErr	nBind(TOptionArray*, TTimeout, Boolean) { return noErr; }
	NewtonErr	nListen(TOptionArray*, CBufferSegment*, Long*, TTimeout, Boolean) { return noErr; }
	NewtonErr	nAccept(TEndpoint*, TOptionArray*, CBufferSegment*, Long, TTimeout, Boolean) { return noErr; }
	NewtonErr	nConnect(TOptionArray*, CBufferSegment*, Long*, TTimeout, Boolean) { return noErr; }
	NewtonErr	nRelease(TTimeout, Boolean) { return noErr; }
	NewtonErr	nDisconnect(CBufferSegment*, Long, Long, TTimeout, Boolean) { return noErr; }
	NewtonErr	nUnBind(TTimeout, Boolean) { return noErr; }
	NewtonErr	nOptMgmt(ULong, TOptionArray*, TTimeout, Boolean) { return noErr; }
	NewtonErr	nSnd(UByte*, Size*, ULong, TTimeout, Boolean, TOptionArray*) { return noErr; }
	NewtonErr	nRcv(UByte*, Size*, Size, ULong*, TTimeout, Boolean, TOptionArray*) { return noErr; }
	NewtonErr	nSnd(CBufferSegment*, ULong, TTimeout, Boolean, TOptionArray*) { return noErr; }
	NewtonErr	nRcv(CBufferSegment*, Size, ULong*, TTimeout, Boolean, TOptionArray*) { return noErr; }
	NewtonErr	nAbort(Boolean) { return noErr; }
	NewtonErr	Timeout(ULong) { return noErr; }
	Boolean		IsPending(ULong) { return false; }
};


// what was sent, read back as PCL: the rows of the page (a skipped row is
// all white), the commands seen
static const long kWidth = 2400;
static const long kRowBytes = kWidth / 8;
static unsigned char	gPage[100][kRowBytes];
static long				gRows;
static char				gCommands[4000];

static bool
ReadBack(void)
{
	memset(gPage, 0, sizeof(gPage));
	gRows = 0;
	gCommands[0] = 0;
	size_t i = 0;
	long compression = 0;
	while (i < gSentSize)
	{
		if (gSent[i] != 0x1b)
			return false;
		// a command: ESC, then the characters up to an upper-case one (or
		// ESC E alone)
		size_t start = i++;
		if (gSent[i] == 'E')
		{
			strcat(gCommands, "E ");
			i++;
			continue;
		}
		if (gSent[i] == '%')
		{
			while (i < gSentSize && gSent[i] != 'X')
				i++;
			strcat(gCommands, "UEL ");
			i++;
			continue;
		}
		char group[3] = { (char) gSent[i], (char) gSent[i + 1], 0 };
		i += 2;
		for ( ; ; )
		{
			long value = strtol((const char*) gSent + i, nil, 10);
			while (gSent[i] == '-' || (gSent[i] >= '0' && gSent[i] <= '9'))
				i++;
			char parameter = (char) gSent[i++];
			char command[32];
			snprintf(command, sizeof(command), "%s%ld%c ", group, value, parameter);
			if (strcmp(group, "*b") == 0 && (parameter == 'W' || parameter == 'Y'))
				snprintf(command, sizeof(command), "*b%c ", parameter);
			strcat(gCommands, command);
			if (parameter == 'm')
				compression = value;
			else if (strcmp(group, "*b") == 0 && parameter == 'Y')
				gRows += value;
			else if (strcmp(group, "*b") == 0 && parameter == 'W')
			{
				// a row: PackBits (mode 2) or as it is
				const unsigned char* p = gSent + i;
				const unsigned char* end = p + value;
				long out = 0;
				if (compression == 2)
				{
					while (p < end)
					{
						int n = (signed char) *p++;
						if (n >= 0)
						{
							for (int k = 0; k <= n; k++)
								gPage[gRows][out++] = *p++;
						}
						else if (n != -128)
						{
							unsigned char b = *p++;
							for (int k = 0; k <= -n; k++)
								gPage[gRows][out++] = b;
						}
					}
				}
				else
					memcpy(gPage[gRows], p, value);
				i += value;
				gRows++;
				compression = 0;
			}
			if (parameter >= 'A' && parameter <= 'Z')
				break;
		}
		(void) start;
	}
	return true;
}


int
main()
{
	InitHostStandaloneHeap();
	gObjectHeapSize = 0x100000;
	InitObjects();

	ThpPCL* driver = new (calloc(1, sizeof(ThpPCL))) ThpPCL;		// (as its class info's MakeAt makes one)
	TTestEndpoint endpoint;
	PrintConnect connect;
	driver->fConnect = &connect;
	driver->fEndpoint = &endpoint;
	driver->fRowBytes = 300;
	driver->fPackBuffer = (char*) NewPtr(300 + 0x10);

	// the preferences and the page
	DotPrinterPrefs prefs;
	driver->GetBandPrefs(&prefs);
	EXPECT(prefs.minBand == 25 && prefs.optimumBand == 50 && !prefs.asyncBanding && prefs.wantMinBounds);
	PrPageInfo info;
	driver->GetPageInfo(&info);
	EXPECT(info.printerDPI.x == ToFixed(300) && info.printerPageSize.h == 2400 && info.printerPageSize.v == 3150);
	driver->fA4 = true;
	driver->GetPageInfo(&info);
	EXPECT(info.printerPageSize.h == 2331 && info.printerPageSize.v == 3324);
	driver->fA4 = false;

	EXPECT(driver->OpenPage() == noErr);
	// three bands of 25 rows: blank; black rows 5..9 of the second (a bar
	// and a pattern); the third after a cancel
	static unsigned char bits[25 * kRowBytes];
	PixelMap band;
	band.baseAddr = (Ptr) bits;
	band.rowBytes = kRowBytes;
	SetRect(&band.bounds, 0, 0, kWidth, 25);
	band.pixMapFlags = kPixMapPtr | 1;
	Rect none = { 0, 0, 0, 0 };
	memset(bits, 0, sizeof(bits));
	EXPECT(driver->ImageBand(&band, &none) == noErr);
	SetRect(&band.bounds, 0, 25, kWidth, 50);
	for (long y = 5; y < 10; y++)
		for (long x = 0; x < kRowBytes; x++)
			bits[y * kRowBytes + x] = (x >= 10 && x < 40) ? 0xff : (y == 7 ? (unsigned char) x : 0);
	Rect black;
	SetRect(&black, 80, 30, 320, 35);
	EXPECT(driver->ImageBand(&band, &black) == noErr);
	driver->CancelJob(true);
	SetRect(&band.bounds, 0, 50, kWidth, 75);
	EXPECT(driver->ImageBand(&band, &black) == noErr);
	EXPECT(driver->ClosePage() == noErr);

	EXPECT(ReadBack());
	printf("test_HPPCL: %ld bytes: %.200s\n", (long) gSentSize, gCommands);
	EXPECT(strncmp(gCommands, "UEL E *t300R *r3150T *r2400S *r0A ", 34) == 0);
	EXPECT(strstr(gCommands, "*b2m *bW") != nil);
	EXPECT(strstr(gCommands, "*r0C E UEL ") != nil);
	// 25 + 5 white rows skipped, five rows of black, then the cancel's
	// skip of the rest (15 + 25)
	EXPECT(gRows == 30 + 5 + 40);
	for (long y = 0; y < 5; y++)
		EXPECT(memcmp(gPage[30 + y], bits + (5 + y) * kRowBytes, kRowBytes) == 0);
	long blank = 0;
	for (long y = 0; y < 30; y++)
		for (long x = 0; x < kRowBytes; x++)
			blank |= gPage[y][x];
	EXPECT(blank == 0);

	// the problems
	driver->fError = kPR_PROB_NoPaper;
	EXPECT(driver->ErrorIsProblem());
	EXPECT(driver->IsProblemResolved() == 1);
	driver->fError = kPR_ERR_LostContact;
	EXPECT(!driver->ContinueIO() && !driver->ErrorIsProblem());
	driver->fError = kPR_ERR_UserCancel;
	EXPECT(driver->ContinueIO());

	if (failures == 0)
		printf("test_HPPCL: all passed\n");
	else
		printf("test_HPPCL: %d failures\n", failures);
	return failures != 0;
}
