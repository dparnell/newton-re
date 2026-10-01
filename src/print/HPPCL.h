/*
	File:		print/HPPCL.h

	Contains:	ThpPCL, the ROM's driver for HP's PCL printers: a
				TDotPrinterDriver (print/Printer.h), so the page is drawn by
				TDotPrinter a band at a time and each band comes here as a
				bitmap at 300 dots an inch.

				A page is PCL 5 raster graphics: the job reset ("ESC E",
				inside PJL's universal exit "ESC %-12345X"), 300 dots an
				inch ("ESC *t300R"), the raster's height and width
				("ESC *r3150T", "ESC *r2400S" on letter), raster mode from
				the left margin ("ESC *r0A"), then each row of black as
				"ESC *b2m<n>W" and n bytes of it packed with PackBits (TIFF
				compression, mode 2), white rows skipped with
				"ESC *b<n>Y", and the end ("ESC *rC", "ESC E", "ESC
				%-12345X").

				The bytes go to the printer through a comm endpoint: the
				asynchronous serial service 'aser' at 57600 bps with
				hardware flow control for a DeskWriter (the printer frame's
				prModel 0), or IrDA's IrLPT for the LaserJet 5MP and DeskJet
				340 (prModel 1, 2).  The ROM offers all three in
				AvailablePrinters.

	Reconstructed from the MP2x00 US ROM (0x002e56c0-0x002e6158; the class
	info at 0x00388484); the fields are named from their uses there, the
	offsets in the comments are the ROM's.
*/

#ifndef __PRINT_HPPCL_H
#define __PRINT_HPPCL_H

#ifndef __PRINT_PRINTER_H
#include "print/Printer.h"
#endif

class TEndpoint;

PROTOCOL ThpPCL : public TDotPrinterDriver
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(ThpPCL);

	// (no New of its own: made by name, its fields set up by Open)
	ThpPCL*			New() { return this; }

	void			Delete();									// ROM 0x002e56c8 Delete__6ThpPCLFv
	NewtonErr		Open();										// ROM 0x002e5b90 Open__6ThpPCLFv
	NewtonErr		Close();									// ROM 0x002e5ccc Close__6ThpPCLFv
	NewtonErr		OpenPage();									// ROM 0x002e5d20 OpenPage__6ThpPCLFv
	NewtonErr		ClosePage();								// ROM 0x002e5e50 ClosePage__6ThpPCLFv
	NewtonErr		ImageBand(PixelMap* band, const Rect* minRect);	// ROM 0x002e5ea0 ImageBand__6ThpPCLFP8PixelMapPC4Rect
	void			CancelJob(Boolean asyncCancel);				// ROM 0x002e609c CancelJob__6ThpPCLFUc
	PrProblemResolution	IsProblemResolved();					// ROM 0x002e60b8 IsProblemResolved__6ThpPCLFv
	void			GetPageInfo(PrPageInfo* info);				// ROM 0x002e6104 GetPageInfo__6ThpPCLFP10PrPageInfo
	void			GetBandPrefs(DotPrinterPrefs* prefs);		// ROM 0x002e56d8 GetBandPrefs__6ThpPCLFP15DotPrinterPrefs
	NewtonErr		FaxEndPage(long pageCount);					// ROM 0x002e5cc4 FaxEndPage__6ThpPCLFl

	Boolean			ContinueIO();								// ROM 0x002e56fc ContinueIO__6ThpPCLFv
	PrProblemResolution	DoHandleProblem();						// ROM 0x002e5730 DoHandleProblem__6ThpPCLFv
	Boolean			ErrorIsProblem();							// ROM 0x002e57c8 ErrorIsProblem__6ThpPCLFv
	void			GetStatus();								// ROM 0x002e57f4 GetStatus__6ThpPCLFv
	Boolean			InitializeConnection();						// ROM 0x002e5818 InitializeConnection__6ThpPCLFv
	void			InitializeFields();							// ROM 0x002e5a0c InitializeFields__6ThpPCLFv
	Boolean			PrinterCanPrint();							// ROM 0x002e5afc PrinterCanPrint__6ThpPCLFv
	NewtonErr		ReleaseConnection();						// ROM 0x002e5b64 ReleaseConnection__6ThpPCLFv
	void			SendCommand(char* command);					// ROM 0x002e5c1c SendCommand__6ThpPCLFPc
	void			SendData(char* data, long size);			// ROM 0x002e5c4c SendData__6ThpPCLFPcl

	TEndpoint*		fEndpoint;			// +0x18  the connection to the printer
	NewtonErr		fError;				// +0x1c
	Boolean			fProblemNotFixed;	// +0x20  the user cancelled a problem
	Boolean			fCancelled;			// +0x21  the job was cancelled: the bands left are skipped
	Boolean			fA4;				// +0x22  A4 paper (else U.S. letter)
	long			fBlankRows;			// +0x24  white rows not yet skipped
	long			fRowBytes;			// +0x28  a raster row: 300 bytes (letter), 292 (A4)
	char			fRowCommand[12];	// +0x2c  "ESC *b<n>W", a row without compression
	char			fCommand[12];		// +0x38  a command being put together
	char*			fPackBuffer;		// +0x44  a row packed (fRowBytes + 16 bytes; nil: rows go unpacked)
	long			fPrModel;			// +0x48  the printer frame's prModel: 0 serial, else IrDA
};


// HOST EXTENSION (not in the ROM): a printer the host connects through a
// comm service of its own.  ThpPCL::InitializeConnection asks this hook
// first, with the driver (its fPrModel and its connection frame) and the
// empty option array; when the hook has put the service's options in it
// (answering true) the endpoint is made of those, in place of the serial
// port or IrDA.  Nil on a device.  (print/host/HostIPPTool.cpp sets the
// IPP printer's.)
class TOptionArray;
typedef Boolean (*PrinterServiceHook)(ThpPCL* driver, TOptionArray* options);
extern PrinterServiceHook	gPrinterServiceHook;

// HOST EXTENSION: and when the connection is closed (Close), the hook may
// make what became of the job the job's error - the error closing it is
// handed in, and what it answers takes its place (an IPP printer's refusal,
// which a serial or IrDA connection has no way to report).  Nil on a device.
typedef NewtonErr (*PrinterCloseHook)(ThpPCL* driver, NewtonErr err);
extern PrinterCloseHook		gPrinterCloseHook;

#endif	/* __PRINT_HPPCL_H */
