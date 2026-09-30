/*
	File:		HostPrinter.h

	Contains:	The host's printer: a TDotPrinterDriver that "prints" each page
				to a PNG file in a directory of the host's, so that Print from
				any Newton application lands on the host.  It is offered to the
				user as the ROM offers its own printers - a printer frame in
				AvailablePrinters, which the Print slip's "Choose Other
				Printer" lists - and the ROM's own imaging engine, TDotPrinter,
				draws the page for it a band at a time.

				Host only (not in the ROM); portable C and C library only.
*/

#if !defined(__HOSTPRINTER_H)
#define __HOSTPRINTER_H 1

#include "print/Printer.h"

// the page: 300 dots an inch, the whole sheet written (letter 2550 x 3300,
// A4 2480 x 3508), the printable area placed at the printer frame's
// printableOrigin
enum { kHostPrinterDPI = 300 };

// The printer's name in the Print slip, and its driver's name
#define kHostPrinterName		"Host printer (PNG files)"
#define kHostPrinterDriverName	"THostPrinterDriver"

PROTOCOL THostPrinterDriver : public TDotPrinterDriver
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(THostPrinterDriver);

	THostPrinterDriver*	New();
	void			Delete();
	NewtonErr		Open();
	NewtonErr		Close();
	NewtonErr		OpenPage();
	NewtonErr		ClosePage();
	NewtonErr		ImageBand(PixelMap* band, const Rect* minRect);
	void			CancelJob(Boolean asyncCancel);
	PrProblemResolution	IsProblemResolved();
	void			GetPageInfo(PrPageInfo* info);
	void			GetBandPrefs(DotPrinterPrefs* prefs);
	NewtonErr		FaxEndPage(long pageCount);

	unsigned char*	fSheet;			// the whole sheet, one bit a dot, 1 black
	long			fSheetWidth;	// in dots
	long			fSheetHeight;
	long			fSheetRowBytes;
	long			fAreaLeft;		// where the printable area starts on the sheet
	long			fAreaTop;
	long			fAreaWidth;		// the printable area, in dots
	long			fAreaHeight;
	Boolean			fCancelled;
};

// Where the pages go (default: the working directory); each page is
// <dir>/print-NNN.png, NNN counting the pages printed since the program
// started, from 001.
void	HostSetPrintDirectory(const char* dir);

// The driver registered and its printer frame added to AvailablePrinters,
// which the Print slip's printer chooser lists, and the global function
// HostPagesPrinted() (how many pages have been written) defined; run once
// the NewtonScript globals are there (the newt world's PreMain).
void	HostInstallPrinter(void);

// Writes a one-bit image (1 black, rows of rowBytes) as a PNG; answers
// whether it could.  (Deflate's stored blocks: no compression library.)
bool	HostWritePNG1(const char* path, const unsigned char* bits, long width, long height, long rowBytes);

#endif	/* __HOSTPRINTER_H */
