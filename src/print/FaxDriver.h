/*
	File:		print/FaxDriver.h

	Contains:	TFaxDriver, the fax as a printer: a TDotPrinterDriver whose
				bands go down the telephone line (print/Printer.h).

				The print job is a fax job when its printer frame's
				driverName is "TFaxDriver" (the fax transport's printer
				frame is).  Open starts the fax tool over the modem tool,
				dials the connection frame's phoneNumber and waits for the
				session; GetPageInfo answers the page the session agreed
				(200 dots an inch across and 200 or 100 down, letter or A4
				as the paper is), GetBandPrefs bands of 25 lines, taken
				asynchronously, with the smallest rectangle round the black
				wanted; each band imaged is sent as the rows with anything
				in them, blank lines standing for the rest, and a page is
				bracketed by the fax tool's begin- and end-page options,
				with a quarter inch of blank lines at its top and bottom.

				The fax tool is driven through TFaxDriverData, the ROM's
				subclass of comms/fax/FaxToolInterface.h: its ...Complete
				virtuals keep each result and let the waiting print job go
				on (PrRegainControl), the job waiting in PrReleaseControl
				while the band or session is being dealt with.

	Reconstructed from the MP2x00 US ROM (0x0020f044-0x0020fae4); each
	function cites its origin.  The fields are named from their uses (the
	ROM offsets in the comments).
*/

#ifndef __PRINT_FAXDRIVER_H
#define __PRINT_FAXDRIVER_H

#include "print/Printer.h"
#include "comms/fax/FaxToolInterface.h"


class TFaxDriverData : public TFaxToolInterface
{
public:
					TFaxDriverData(ULong serviceId, ULong toolId) : TFaxToolInterface(serviceId, toolId) { }
	virtual			~TFaxDriverData();						// ROM 0x0038b9a0 (unnamed) - TFaxDriverData::~TFaxDriverData

	void			OpenSessionComplete(NewtonErr err, ULong connected, ULong reserved, ULong hRes, ULong vRes);	// ROM 0x0020f6d4 OpenSessionComplete__14TFaxDriverDataFlUlN32
	void			CloseSessionComplete(NewtonErr err);	// ROM 0x0020f70c CloseSessionComplete__14TFaxDriverDataFl
	void			BeginPageComplete(NewtonErr err);		// ROM 0x0020f714 BeginPageComplete__14TFaxDriverDataFl
	void			EndPageComplete(NewtonErr err);			// ROM 0x0020f71c EndPageComplete__14TFaxDriverDataFl
	void			PrintBandComplete(NewtonErr err);		// ROM 0x0020f724 PrintBandComplete__14TFaxDriverDataFl
	// (the ROM's are shared `mov pc,lr` stubs: the driver never answers or receives)
	void			AcceptSessionComplete(NewtonErr, ULong, ULong, ULong) { }
	void			GetBandComplete(NewtonErr, ULong, Boolean) { }
	void			ConfirmReceivedPageComplete(NewtonErr, Boolean) { }

	TPrinter*		fPrinter;			// +0x23c  the imaging engine waiting on the tool
	Boolean			fWaiting;			// +0x240  Open is waiting for the session
	Boolean			fSessionOpen;		// +0x241  the session's open came back (well or not)
	NewtonErr		fOpenErr;			// +0x244
	ULong			fConnected;			// +0x248
	ULong			fReserved;			// +0x24c
	ULong			fHRes;				// +0x250  the session's dots an inch across (204)
	ULong			fVRes;				// +0x254  ... and lines an inch down (98 or 196)
	NewtonErr		fCloseErr;			// +0x258
	NewtonErr		fBeginPageErr;		// +0x25c
	NewtonErr		fEndPageErr;		// +0x260
	NewtonErr		fBandErr;			// +0x264
	Boolean			fBandPending;		// +0x268  a band is on its way to the tool
};


PROTOCOL TFaxDriver : public TDotPrinterDriver
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TFaxDriver);

	// (the class info's default New does nothing)
	TFaxDriver*		New() { return this; }

	void			Delete();								// ROM 0x0020f684 Delete__10TFaxDriverFv
	NewtonErr		Open();									// ROM 0x0020f04c Open__10TFaxDriverFv
	NewtonErr		Close();								// ROM 0x0020f738 Close__10TFaxDriverFv
	NewtonErr		OpenPage();								// ROM 0x0020f7d4 OpenPage__10TFaxDriverFv
	NewtonErr		ClosePage();							// ROM 0x0020f8a0 ClosePage__10TFaxDriverFv
	NewtonErr		ImageBand(PixelMap* theBand, const Rect* minRect);	// ROM 0x0020f8d4 ImageBand__10TFaxDriverFP8PixelMapPC4Rect
	void			CancelJob(Boolean asyncCancel);			// ROM 0x0020fa1c CancelJob__10TFaxDriverFUc
	PrProblemResolution	IsProblemResolved();				// ROM 0x0020fab4 IsProblemResolved__10TFaxDriverFv
	void			GetPageInfo(PrPageInfo* info);			// ROM 0x0020f4e8 GetPageInfo__10TFaxDriverFP10PrPageInfo
	void			GetBandPrefs(DotPrinterPrefs* prefs);	// ROM 0x0020f5e4 GetBandPrefs__10TFaxDriverFP15DotPrinterPrefs
	NewtonErr		FaxEndPage(long pageCount);				// ROM 0x0020f83c FaxEndPage__10TFaxDriverFl

	void			PrintBlankLines(long count);			// ROM 0x0020f604 PrintBlankLines__10TFaxDriverFl
	Boolean			ContinueIO();							// ROM 0x0020fac0 ContinueIO__10TFaxDriverFv

	TFaxDriverData*	fData;				// +0x18  the fax tool
	TOptionArray*	fConfig;			// +0x1c  the services to start
	TOptionArray*	fSessionOptions;	// +0x20  the session's (the page's set-up, dialling, identities)
	Boolean			fPageOpen;			// +0x24
	NewtonErr		fError;				// +0x28
	long			fLinesSent;			// +0x2c  of the page so far (-1 between pages)
	long			fPageLines;			// +0x30  the page's height in lines
};

#endif	/* __PRINT_FAXDRIVER_H */
