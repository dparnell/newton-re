// Host unit test for the PostScript printer (print/PSPrinter.h): TPSPrinter
// made by name as MakePrinter makes one, over a driver of the test's own
// (TTestPSDriver, which keeps every byte it is sent), and two pages drawn
// through its port - rectangles, a frame, a line, an oval, a gray and a
// patterned fill, and a line of text in the system font.  The document is
// then read: the DSC structure and the header, each shape as the
// bottlenecks write it, the text shown in Helvetica's Mac-encoded copy, and
// the trailer's page count.  Also FixedToString (and its ROM quirk with a
// negative number), and TPSPAPDriver's status reading, which is not
// AppleTalk's.
//
// Making instances by name needs the protocol registry, which is a
// monitor, so the test runs as the kernel services task of a booted OS;
// the ROM's objects (the fonts, vars.psFonts' families) are imported from
// the object file the build makes.

#include "print/PSPrinter.h"
#include "Ports.h"
#include "Regions.h"
#include "Rects.h"
#include "Draw.h"
#include "Shapes.h"
#include "Text.h"
#include "Fonts.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "ROMImport.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "NativeFunctions.h"
#include "Locale.h"
#include "NewtonMemory.h"
#include "Boot.h"
#include "UserBoot.h"
#include "host/TaskRuntime.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static char*	gDoc = nil;
static size_t	gDocSize = 0;
static long		gOpened, gPages;


/*------------------------------------------------------------------------------
	The test's driver: the document kept
------------------------------------------------------------------------------*/

PROTOCOL TTestPSDriver : public TPSPrinterDriver
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TTestPSDriver);
	TTestPSDriver*	New() { return this; }
	void			Delete() { }

	NewtonErr		Open() { gOpened++; return noErr; }
	NewtonErr		Close(Boolean) { gOpened--; return noErr; }
	NewtonErr		OpenPage() { return noErr; }
	NewtonErr		ClosePage() { gPages++; return noErr; }
	void			CancelJob(Boolean) { }
	PrProblemResolution	IsProblemResolved() { return kPrProblemFixed; }
	NewtonErr		GetStatus() { return noErr; }
	NewtonErr		SendPSText(char* text, ULong& sent, Boolean) { return Keep(text, strlen(text), sent); }
	NewtonErr		RepeatPSPage() { return noErr; }
	NewtonErr		SendPSBinary(char* data, ULong size, ULong& sent) { return Keep(data, size, sent); }
	NewtonErr		RecvPSText(char*, ULong& size) { size = 0; return noErr; }

	NewtonErr		Keep(const char* data, size_t size, ULong& sent)
	{
		gDoc = (char*) realloc(gDoc, gDocSize + size + 1);
		memcpy(gDoc + gDocSize, data, size);
		gDocSize += size;
		gDoc[gDocSize] = 0;
		sent = size;
		return noErr;
	}
};

PROTOCOL_IMPL_SOURCE_MACRO(TTestPSDriver)
PROTOCOL_CLASSINFO(TTestPSDriver, "TPSPrinterDriver", "", 0x20000, 0, nil)


static long
Count(const char* what)
{
	long n = 0;
	for (const char* p = gDoc; (p = strstr(p, what)) != nil; p++)
		n++;
	return n;
}

#define HAS(text)	EXPECT(gDoc != nil && strstr(gDoc, text) != nil)


static void
DrawPage(long page)
{
	Rect r;
	SetRect(&r, 10, 10, 50, 50);
	PaintRect(&r);							// "10 10 50 50 StdRect fill"
	PenSize(2, 2);
	SetRect(&r, 20, 100, 120, 140);
	FrameRect(&r);							// "2 2 Pen", the rectangle's path, SclPen
	PenNormal();
	MoveTo(10, 200);
	LineTo(190, 200);						// a level line, stroked at the pen's height
	SetRect(&r, 100, 220, 180, 280);
	PaintOval(&r);
	SetRect(&r, 10, 220, 90, 290);
	FillRect(&r, GetStdPattern(grayPat));	// a pattern: SetCurrentPattern, PatternFill
	if (page == 1)
	{
		// a line of text in the system font (espy: psName helvetica)
		StyleRecord style;
		style.fFontFamily = GetArraySlotRef(RefVar(Rromfontlist), 0);
		style.fFontSize = ToFixed(12);
		style.fFontFace = 0;
		StyleRecord* styles[1] = { &style };
		const UniChar text[] = { 'H', 'e', 'l', 'l', 'o', ' ', '(', 'P', 'S', ')', 0 };
		FPoint where;
		where.x = ToFixed(20);
		where.y = ToFixed(320);
		DrawTextOnce(text, 10, styles, nil, where, nil, nil);
	}
}


static void
TestFixedToString(TPSPrinter* printer)
{
	char buffer[16];
	EXPECT(strcmp(printer->FixedToString(ToFixed(12), buffer), "12") == 0);
	EXPECT(strcmp(printer->FixedToString(0x18000, buffer), "1.50") == 0);
	EXPECT(strcmp(printer->FixedToString(0x14ccd, buffer), "1.30") == 0);
	// ROM quirk: the answer is past the sign; the buffer has it
	char* answer = printer->FixedToString(-0x18000, buffer);
	EXPECT(strcmp(answer, "1.50") == 0 && strcmp(buffer, "-1.50") == 0);
}


static void
TestStatusStrings(void)
{
	TPSPAPDriver driver;
	driver.fError = -1;
	EXPECT(driver.InterpretPAPString((char*) "%%[ status: busy; source: AppleTalk ]%%", true) == noErr);
	EXPECT(driver.InterpretPAPString((char*) "%%[ status: busy; source: AppleTalk ]%%", false) == kPR_ERR_Busy);
	EXPECT(driver.InterpretPAPString((char*) "%%[ status: idle ]%%", true) == -1);		// all well: the driver's own error
	EXPECT(driver.InterpretPAPString((char*) "%%[ PrinterError: out of paper ]%%", true) == kPR_PROB_NoPaper);
	EXPECT(driver.InterpretPAPString((char*) "%%[ Error: limitcheck; OffendingCommand: x ]%%", true) == -44006);
	EXPECT(driver.InterpretPAPString((char*) "nonsense", true) == kPR_ERR_PrinterError);
	unsigned char pascal[] = "\x0e" "status: jammed";
	EXPECT(driver.InterpretPAPStatusString(pascal, true) == kPR_PROB_Jammed);
}


static void
PrintScenario(void)
{
	EXPECT(ImportROMObjectsFromFile(NEWTON_OBJECTS) == noErr);
	gObjectHeapSize = 0x100000;
	InitObjects();
	InitGraf();
	InitFonts();
	InstallHostNatives();
	SetFrameSlot(RefVar(gVarFrame), RSSYMvars, RefVar(gVarFrame));
	SetFrameSlot(RefVar(gVarFrame), RSSYMfunctions, RefVar(gFunctionFrame));
	RefVar fonts(AllocateFrame());
	RefVar list(Rromfontlist);
	for (long i = 0; i < Length(list); i++)
		SetFrameSlot(fonts, RefVar(FamilyNumToSym(i)), RefVar(GetArraySlotRef(list, i)));
	SetFrameSlot(RefVar(gVarFrame), RSSYMfonts, fonts);
	// vars.psFonts as the ROM's globals have it: {_proto: {helvetica: @109,
	// timesRoman: @278, symbol: @272}}
	RefVar psProto(AllocateFrame());
	SetFrameSlot(psProto, RefVar(Intern("helvetica")), RefVar(MAKEMAGICPTR(109)));
	SetFrameSlot(psProto, RefVar(Intern("timesRoman")), RefVar(MAKEMAGICPTR(278)));
	SetFrameSlot(psProto, RefVar(Intern("symbol")), RefVar(MAKEMAGICPTR(272)));
	RefVar psFonts(AllocateFrame());
	SetFrameSlot(psFonts, RSSYM_proto, psProto);
	SetFrameSlot(RefVar(gVarFrame), RSSYMpsfonts, psFonts);
	RefVar config(AllocateFrame());
	SetFrameSlot(config, RSSYMname, MakeString("Ada (Lovelace)"));
	SetFrameSlot(RefVar(gVarFrame), RSSYMuserconfiguration, config);
	// the U.S. locale bundle, for the header's date (as test_Dates has it)
	RefVar intl(AllocateFrame());
	SetFrameSlot(intl, RSSYMcurrentlocalebundle, RefVar(TranslateROMRef(0x004a4d09)));
	SetFrameSlot(RefVar(gVarFrame), RSSYMinternational, intl);
	EXPECT(InitInternationalUtils() == noErr);

	TTestPSDriver::ClassInfo()->Register();
	TPSPrinter::ClassInfo()->Register();
	TPrinter* printer = (TPrinter*) NewByName("TPrinter", "TPSPrinter");
	EXPECT(printer != nil);
	if (printer == nil)
	{
		HostStopTasks();
		return;
	}
	newton_try
	{
		EXPECT(printer->Constructor((char*) "TTestPSDriver") == noErr);
		TestFixedToString((TPSPrinter*) printer);
		TestStatusStrings();
		RefVar connect(AllocateFrame());
		SetFrameSlot(connect, RSSYMtitle, MakeString("A test"));
		EXPECT(printer->Open(connect) == noErr);
		EXPECT(gOpened == 1);
		GrafPort* port = printer->GetPort();
		// the page at 72 dpi: letter's 2400 x 3233 dots at 300 dpi
		EXPECT(port->portRect.right == 576 && port->portRect.bottom == 776);
		for (long page = 1; page <= 2; page++)
		{
			EXPECT(printer->OpenPage() == noErr);
			long passes = 0;
			do
			{
				SetPort(port);
				DrawPage(page);
				passes++;
			} while (printer->RepeatPage() && passes < 10);
			EXPECT(passes == 1);
			EXPECT(printer->ClosePage() == noErr);
		}
		EXPECT(printer->Close() == noErr);
		EXPECT(gOpened == 0 && gPages == 2);
	}
	newton_catch_all
	{
		failures++;
		printf("FAIL: an exception %s\n", _info.exception.name);
	}
	end_try;

	printf("test_PSPrinter: %ld bytes of PostScript\n", (long) gDocSize);
	EXPECT(gDoc != nil && strncmp(gDoc, "%!PS-Adobe-3.0\r", 15) == 0);
	HAS("%%Title: A test\r");
	HAS("%%Creator: Out Box\r");
	HAS("%%For: Ada (Lovelace)\r");
	HAS("(Ada \\(Lovelace\\); document: A test) jn\r");
	HAS("/Bdf { bind def } bind def");			// the prolog
	HAS("/PatternFill");
	HAS("/Helvetica-Mac /Helvetica EncodeFont\r");
	HAS("/Times-Bold-Mac /Times-Bold EncodeFont\r");
	HAS("%%EndProlog\r");
	HAS("\r\r%%Page: 1 1\r\r");
	HAS("\r\r%%Page: 2 2\r\r");
	EXPECT(Count("showpage\r") == 2);
	HAS("%%Trailer\r%%Pages: 2\r%%EOF\r");
	HAS("10 10 50 50 StdRect fill\r");
	HAS("2 2 Pen\r");
	HAS("21 101 MvTo 119 101 LnTo 119 139 LnTo 21 139 LnTo CP SclPen\r");
	HAS("CLW 1 SLW newpath\r10 200.50 MvTo 191 200.50 LnTo \rstroke SLW\r");
	HAS("220 100 280 180 0 360 FrameOval fill\r");
	HAS("> SetCurrentPattern\r");
	HAS("220 10 290 90 StdRect PatternFill\r");
	HAS("/Helvetica-Mac 12 SF\r");
	HAS("(Hello \\(PS\\)) show\r");
	EXPECT(Count("(Hello") == 1);
	if ((failures != 0 || getenv("TEST_PS_SHOW")) && gDoc != nil)
	{
		const char* page = strstr(gDoc, "%%EndSetup");
		printf("%s\n", page != nil ? page : gDoc);
	}
	printer->Delete();
	HostStopTasks();
}


int
main()
{
	gHostKernelServicesTask = PrintScenario;
	OsBoot();
	if (failures == 0)
		printf("test_PSPrinter: all passed\n");
	else
		printf("test_PSPrinter: %d failures\n", failures);
	return failures != 0;
}
