// Host unit test for the dot printer's imaging engine (print/Printer.h):
// TDotPrinter over a driver of the test's own (TTestDotDriver, a 144-dpi
// "printer" that pastes each band it is given into a page of its own),
// made by name as MakePrinter makes one.  A page is drawn through the
// printer port at 72 dpi - black rectangles, a framed rectangle, a line,
// an oval and a gray pattern - once per band, as a print view draws it
// (OpenPage, then the drawing repeated while RepeatPage asks), and the
// page the driver was given is checked pixel by pixel: every shape twice
// the size, the gray a gray, and the bands meeting without a seam.
//
// Making instances by name needs the protocol registry, which is a
// monitor, so the test runs as the kernel services task of a booted OS.

#include "print/Printer.h"
#include "Ports.h"
#include "Regions.h"
#include "Rects.h"
#include "Draw.h"
#include "Shapes.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "NewtonMemory.h"
#include "Boot.h"
#include "UserBoot.h"
#include "host/TaskRuntime.h"
#include "host/RomBugs.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// the page: 400 x 600 dots at 144 dpi, 200 x 300 on the 72-dpi page
static const long kPageWidth = 400, kPageHeight = 600;
static const long kPageRowBytes = kPageWidth / 8;
static UChar	gPage[kPageRowBytes * kPageHeight];
static long		gBands, gOpened, gPages;


/*------------------------------------------------------------------------------
	The test's printer: bands pasted into gPage
------------------------------------------------------------------------------*/

PROTOCOL TTestDotDriver : public TDotPrinterDriver
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TTestDotDriver);
	TTestDotDriver*	New() { return this; }
	void			Delete() { }

	NewtonErr		Open() { gOpened++; return noErr; }
	NewtonErr		Close() { gOpened--; return noErr; }
	NewtonErr		OpenPage() { memset(gPage, 0, sizeof(gPage)); return noErr; }
	NewtonErr		ClosePage() { gPages++; return noErr; }
	NewtonErr		ImageBand(PixelMap* band, const Rect* /*minRect*/)
	{
		gBands++;
		long rows = band->bounds.bottom - band->bounds.top;
		for (long y = 0; y < rows; y++)
		{
			long pageRow = band->bounds.top + y;
			if (pageRow >= kPageHeight)
				break;
			memcpy(gPage + pageRow * kPageRowBytes, band->baseAddr + y * band->rowBytes, kPageRowBytes);
		}
		return noErr;
	}
	void			CancelJob(Boolean) { }
	PrProblemResolution	IsProblemResolved() { return kPrProblemFixed; }
	void			GetPageInfo(PrPageInfo* info)
	{
		info->printerDPI.x = 144 << 16;
		info->printerDPI.y = 144 << 16;
		info->printerPageSize.h = (short) kPageWidth;
		info->printerPageSize.v = (short) kPageHeight;
	}
	void			GetBandPrefs(DotPrinterPrefs* prefs)
	{
		prefs->minBand = 16;
		prefs->optimumBand = 64;			// several bands a page
		prefs->asyncBanding = false;
		prefs->wantMinBounds = false;
	}
	NewtonErr		FaxEndPage(long) { return noErr; }
};

PROTOCOL_IMPL_SOURCE_MACRO(TTestDotDriver)
PROTOCOL_CLASSINFO(TTestDotDriver, "TDotPrinterDriver", "", 0x20000, 0, nil)


static inline int
Pixel(long x, long y)
{
	return (gPage[y * kPageRowBytes + (x >> 3)] >> (7 - (x & 7))) & 1;
}

// how many of a rectangle's dots are black
static long
BlackIn(long left, long top, long right, long bottom)
{
	long n = 0;
	for (long y = top; y < bottom; y++)
		for (long x = left; x < right; x++)
			n += Pixel(x, y);
	return n;
}


// the page, drawn at 72 dpi into the printer port
static void
DrawPage()
{
	Rect r;
	SetRect(&r, 10, 10, 50, 50);
	PaintRect(&r);						// 20..100 on the printer
	SetRect(&r, 60, 20, 190, 36);		// a band boundary (64 dots = 32 on the page) crossed
	PaintRect(&r);
	PenSize(2, 2);
	SetRect(&r, 20, 100, 120, 140);
	FrameRect(&r);						// a frame 4 dots thick
	PenNormal();
	MoveTo(10, 200);
	LineTo(190, 200);					// a line 2 dots thick
	SetRect(&r, 100, 220, 180, 280);
	PaintOval(&r);
	SetRect(&r, 10, 220, 90, 290);
	FillRect(&r, GetStdPattern(grayPat));	// a gray, through the mask and the scaled pattern
}


// ROM BUG (fixed): TryAllocBands failing on a buffer after the first left
// the ones before it, freed, in the array; fixed, they are cleared
// the largest block NewPtr can give now (the heap may grow to give it)
static Size
LargestPtr(void)
{
	Size low = 0, high = 0x40000000;
	while (high - low > 16)
	{
		Size mid = low + (high - low) / 2;
		Ptr p = NewPtr(mid);
		if (p != nil)
		{
			DisposPtr(p);
			low = mid;
		}
		else
			high = mid;
	}
	return low;
}

static void
TestTryAllocBands(TDotPrinter* printer)
{
	char* bands[4];
	// one block as big as the largest can be had, a second cannot
	Size size = LargestPtr();
	Ptr held = NewPtr(size);
	Size second = LargestPtr();
	DisposPtr(held);
	EXPECT(second < size);
	SetRomBugFixed(false);
	bands[0] = bands[1] = (char*) 1;
	EXPECT(!printer->TryAllocBands(bands, 2, size));
	EXPECT(bands[0] != nil && bands[1] == nil);		// bands[0] freed but left
	SetRomBugFixed(true);
	bands[0] = bands[1] = (char*) 1;
	EXPECT(!printer->TryAllocBands(bands, 2, size));
	EXPECT(bands[0] == nil && bands[1] == nil);
	// and a request that can be met still is
	EXPECT(printer->TryAllocBands(bands, 2, 256));
	EXPECT(bands[0] != nil && bands[1] != nil);
	DisposPtr(bands[0]);
	DisposPtr(bands[1]);
}


static void
PrintScenario(void)
{
	gObjectHeapSize = 0x100000;
	InitObjects();
	InitGraf();
	TTestDotDriver::ClassInfo()->Register();
	TDotPrinter::ClassInfo()->Register();

	TPrinter* printer = (TPrinter*) NewByName("TPrinter", "TDotPrinter");
	EXPECT(printer != nil);
	if (printer == nil)
	{
		HostStopTasks();
		return;
	}
	EXPECT(printer->Constructor((char*) "TTestDotDriver") == noErr);
	RefVar connect(AllocateFrame());
	EXPECT(printer->Open(connect) == noErr);
	EXPECT(gOpened == 1);
	GrafPort* port = printer->GetPort();
	EXPECT(port->portRect.right == 200 && port->portRect.bottom == 300);
	EXPECT(printer->GetScalerInfo()->scaleRatios.x == 2 << 16);
	printer->SetPortraitOrientation(true);
	EXPECT(printer->OpenPage() == noErr);
	long passes = 0;
	do
	{
		SetPort(port);
		DrawPage();
		passes++;
	} while (printer->RepeatPage() && passes < 100);
	EXPECT(printer->ClosePage() == noErr);
	EXPECT(printer->Close() == noErr);
	EXPECT(gOpened == 0 && gPages == 1);
	printf("test_DotPrinter: %ld passes, %ld bands\n", passes, gBands);
	EXPECT(passes == gBands && gBands == (kPageHeight + 63) / 64);

	// the black rectangle, twice the size and nothing round it
	EXPECT(BlackIn(20, 20, 100, 100) == 80 * 80);
	EXPECT(BlackIn(18, 18, 20, 102) == 0 && BlackIn(100, 18, 102, 102) == 0);
	EXPECT(BlackIn(18, 18, 102, 20) == 0 && BlackIn(18, 100, 102, 102) == 0);
	// the one across a band boundary: no seam
	EXPECT(BlackIn(120, 40, 380, 72) == 260 * 32);
	// the frame: 4 dots of black round a white inside
	EXPECT(BlackIn(40, 200, 240, 204) == 200 * 4);
	EXPECT(BlackIn(44, 204, 236, 276) == 0);
	EXPECT(BlackIn(40, 204, 44, 276) == 4 * 72);
	// the line: 2 dots
	EXPECT(BlackIn(20, 400, 380, 402) == 360 * 2);
	EXPECT(BlackIn(20, 398, 380, 400) == 0 && BlackIn(20, 402, 380, 404) == 0);
	// the oval: black in the middle, white in the corners
	EXPECT(BlackIn(270, 490, 290, 510) == 400);
	EXPECT(BlackIn(200, 440, 204, 444) == 0);
	// the gray: about half its dots, and not the 72-dpi checkerboard -
	// the pattern scaled to the printer, two dots for each of the page's
	long gray = BlackIn(20, 440, 180, 580);
	printf("test_DotPrinter: the gray is %ld of %ld dots\n", gray, 160L * 140);
	EXPECT(gray > 160 * 140 * 4 / 10 && gray < 160 * 140 * 6 / 10);
	EXPECT(Pixel(20, 440) == Pixel(21, 440) && Pixel(20, 440) == Pixel(20, 441));

	TestTryAllocBands((TDotPrinter*) printer);

	printer->Delete();
	HostStopTasks();
}


int
main()
{
	gHostKernelServicesTask = PrintScenario;
	OsBoot();
	if (failures == 0)
		printf("test_DotPrinter: all passed\n");
	else
		printf("test_DotPrinter: %d failures\n", failures);
	return failures != 0;
}
