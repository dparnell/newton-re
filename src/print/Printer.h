/*
	File:		print/Printer.h

	Contains:	The printing system's two protocols and the dot-matrix
				imaging engine between them.

				TPrinter is what a print job talks to: a print view makes
				one by the printer frame's imagingName (MakePrinter,
				print/PrintView.h), gives it the printer frame's driverName
				(Constructor), opens it and then draws each page into its
				port - a GrafPort at 72 dots an inch the size of the paper -
				between OpenPage and ClosePage, as many times as RepeatPage
				says.  The ROM's one TPrinter for bitmap printers is
				TDotPrinter (the PostScript one, TPSPrinter, is NOT YET).

				TDotPrinter prints a page a band at a time: it asks its
				driver how big a band may be (GetBandPrefs), allocates the
				band and two more buffers of the same size - a mask and a
				scaled pattern - and has the page drawn once per band.  The
				port's drawing procs are its scaling bottlenecks
				(ScaleStdRect and the rest, ScalingBottlenecks.cpp), which
				map each shape from the page's 72 dots an inch to the
				printer's own resolution and draw it into the band through a
				second, phantom port whose bits are the band; a shape drawn
				in a pattern other than white or black is drawn into the
				mask and combined with the pattern scaled up to the band
				(UpdateScalePat, TransferShape), so that a gray at 72 dpi is
				the same gray on a fax.  RepeatPage hands each finished band
				to the driver (ImageBand) and moves on.

				TDotPrinterDriver is the seam a printer's own code plugs
				into (the DDK's DotDrivers.h): the band preferences, the
				page's size and resolution, a band to image, and the job's
				open/close/page brackets.  The ROM's are the fax
				(print/FaxDriver.h), the StyleWriter group, the LaserWriter
				LS and HP PCL - only the fax driver is reconstructed.  A
				modern printer is a new implementation of this protocol.

	Not in the DDK's headers as classes (DotDrivers.h and PrintTypes.h
	declare the driver protocol and the types, which are used as they are);
	TPrinter's interface follows the ROM's glue at 0x00387f2c-0x00387fbc and
	TDotPrinter's class info (tools/newton-rom/analysis/classinfo.py
	--name TDotPrinter); the fields are named from their uses in
	TDotPrinter (0x0020d0f0-0x0020e3f0) and the scaling bottlenecks
	(0x001c7860-0x001c97b4).  The offsets in the comments are the ROM's.
*/

#ifndef __PRINT_PRINTER_H
#define __PRINT_PRINTER_H

#ifndef __PROTOCOLS_H
#include "Protocols.h"
#endif

#ifndef __PRINTTYPES_H
#include "PrintTypes.h"
#endif

#ifndef __PRINTERRORS_H
#include "PrintErrors.h"
#endif

class TPseudoSyncState;
class TDotPrinterDriver;


/*------------------------------------------------------------------------------
	T P r i n t e r
	The interface a print job draws through.  Its first slot is not New but
	Constructor, which takes the driver's name: MakePrinter makes the
	instance by name (NewByName, whose default New does nothing) and then
	constructs it.
------------------------------------------------------------------------------*/

PROTOCOL TPrinter : public TProtocol
{
public:
	VIRTUAL NewtonErr	Constructor(char* driverName) ENDVIRTUAL;			// ROM 0x00387f2c Constructor__8TPrinterFPc
	void				Delete();											// ROM 0x00387f38 Delete__8TPrinterFv

	VIRTUAL NewtonErr	Open(RefArg connectInfo) ENDVIRTUAL;				// ROM 0x00387f54 Open__8TPrinterFRC6RefVar
	VIRTUAL NewtonErr	Close() ENDVIRTUAL;									// ROM 0x00387f60 Close__8TPrinterFv
	VIRTUAL NewtonErr	OpenPage() ENDVIRTUAL;								// ROM 0x00387f6c OpenPage__8TPrinterFv
	VIRTUAL NewtonErr	ClosePage() ENDVIRTUAL;								// ROM 0x00387f78 ClosePage__8TPrinterFv
	VIRTUAL Boolean		RepeatPage() ENDVIRTUAL;							// ROM 0x00387f84 RepeatPage__8TPrinterFv
	VIRTUAL void		CancelJob(Boolean asyncCancel) ENDVIRTUAL;			// ROM 0x00387f90 CancelJob__8TPrinterFUc
	VIRTUAL PrProblemResolution	IsProblemResolved() ENDVIRTUAL;				// ROM 0x00387f9c IsProblemResolved__8TPrinterFv
	VIRTUAL void		SetPortraitOrientation(Boolean portrait) ENDVIRTUAL;	// ROM 0x00387fa8 SetPortraitOrientation__8TPrinterFUc
	VIRTUAL NewtonErr	FaxEndPage(long pageCount) ENDVIRTUAL;				// ROM 0x00387fb4 FaxEndPage__8TPrinterFl

	// what every implementation shares: the port the page is drawn into,
	// the scaling from it to the printer's dots, and the cancelling
	void			OpenPort(const PrPageInfo& info);						// ROM 0x0021bc28 OpenPort__8TPrinterFRC10PrPageInfo
	void			ClosePort();											// ROM 0x0021bde4 ClosePort__8TPrinterFv
	GrafPort*		GetPrinterPort();										// ROM 0x0021bdfc GetPrinterPort__8TPrinterFv
	GrafPort*		GetPort();												// ROM 0x0021be04 GetPort__8TPrinterFv
	ScalerInfo*		GetScalerInfo();										// ROM 0x0021be0c GetScalerInfo__8TPrinterFv
	void			SetScalerInfo(const PrPageInfo& info);					// ROM 0x0021be14 SetScalerInfo__8TPrinterFRC10PrPageInfo
	void			SetupConnect(PrintConnect* connect, RefArg connectInfo);	// ROM 0x0021bf3c SetupConnect__8TPrinterFP12PrintConnectRC6RefVar
	void			DoUserAbort();											// ROM 0x0021c034 DoUserAbort__8TPrinterFv
	Boolean			CheckUserAbort();										// ROM 0x0021c050 CheckUserAbort__8TPrinterFv

	NewtonErr		fError;				// +0x10  what the last call came to
	Boolean			fCancelled;			// +0x14  the user cancelled (DoUserAbort)
	Boolean			fCancelHandled;		// +0x15  ... and the job was told
	TPseudoSyncState*	fBlocked;		// +0x18  a PrReleaseControl(kNoTimeout) waiting for PrRegainControl
	long			fReserved[3];		// +0x1c
	ScalerInfo		fScaler;			// +0x28  the page (72 dpi) and the printer's dots, and the ratio
	PrintPort		fPort;				// +0x40  the port the page is drawn into
};


/*------------------------------------------------------------------------------
	T D o t P r i n t e r D r i v e r
	A bitmap printer's driver (DotDrivers.h).  It has no New: the instance
	is made by name and its fields filled in by TDotPrinter::Constructor.
------------------------------------------------------------------------------*/

PROTOCOL TDotPrinterDriver : public TProtocol
{
public:
	void				Delete();											// ROM 0x00388074 Delete__17TDotPrinterDriverFv
	VIRTUAL NewtonErr	Open() ENDVIRTUAL;									// ROM 0x00388090 Open__17TDotPrinterDriverFv
	VIRTUAL NewtonErr	Close() ENDVIRTUAL;									// ROM 0x0038809c Close__17TDotPrinterDriverFv
	VIRTUAL NewtonErr	OpenPage() ENDVIRTUAL;								// ROM 0x003880a8 OpenPage__17TDotPrinterDriverFv
	VIRTUAL NewtonErr	ClosePage() ENDVIRTUAL;								// ROM 0x003880b4 ClosePage__17TDotPrinterDriverFv
	VIRTUAL NewtonErr	ImageBand(PixelMap* theBand, const Rect* minRect) ENDVIRTUAL;	// ROM 0x003880c0 ImageBand__17TDotPrinterDriverFP8PixelMapPC4Rect
	VIRTUAL void		CancelJob(Boolean asyncCancel) ENDVIRTUAL;			// ROM 0x003880cc CancelJob__17TDotPrinterDriverFUc
	VIRTUAL PrProblemResolution	IsProblemResolved() ENDVIRTUAL;				// ROM 0x003880d8 IsProblemResolved__17TDotPrinterDriverFv
	VIRTUAL void		GetPageInfo(PrPageInfo* info) ENDVIRTUAL;			// ROM 0x003880e4 GetPageInfo__17TDotPrinterDriverFP10PrPageInfo
	VIRTUAL void		GetBandPrefs(DotPrinterPrefs* prefs) ENDVIRTUAL;	// ROM 0x003880f0 GetBandPrefs__17TDotPrinterDriverFP15DotPrinterPrefs
	VIRTUAL NewtonErr	FaxEndPage(long pageCount) ENDVIRTUAL;				// ROM 0x003880fc FaxEndPage__17TDotPrinterDriverFl

	PrintConnect*		fConnect;		// +0x10  the job's connection frame, paper and orientation
	TPrinter*			fPrinter;		// +0x14  the imaging engine driving it
};


/*------------------------------------------------------------------------------
	T D o t P r i n t e r
	The ROM's TPrinter for bitmap printers.
------------------------------------------------------------------------------*/

PROTOCOL TDotPrinter : public TPrinter
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TDotPrinter);

	// (the class info's default New is `mov pc,lr`: making one by name
	// does nothing more; Constructor is what sets it up)
	TDotPrinter*	New() { return this; }

	NewtonErr		Constructor(char* driverName);			// ROM 0x0020d0f8 Constructor__11TDotPrinterFPc
	void			Delete();								// ROM 0x0020d418 Delete__11TDotPrinterFv
	NewtonErr		Open(RefArg connectInfo);				// ROM 0x0020d474 Open__11TDotPrinterFRC6RefVar
	NewtonErr		Close();								// ROM 0x0020d764 Close__11TDotPrinterFv
	NewtonErr		OpenPage();								// ROM 0x0020d7e4 OpenPage__11TDotPrinterFv
	NewtonErr		ClosePage();							// ROM 0x0020e3b8 ClosePage__11TDotPrinterFv
	Boolean			RepeatPage();							// ROM 0x0020dee0 RepeatPage__11TDotPrinterFv
	void			CancelJob(Boolean asyncCancel);			// ROM 0x0020e3e4 CancelJob__11TDotPrinterFUc
	PrProblemResolution	IsProblemResolved();				// ROM 0x0020e3f0 IsProblemResolved__11TDotPrinterFv
	void			SetPortraitOrientation(Boolean portrait);	// ROM 0x0020d198 SetPortraitOrientation__11TDotPrinterFUc
	NewtonErr		FaxEndPage(long pageCount);				// ROM 0x0020d3d8 FaxEndPage__11TDotPrinterFl

	void			CalcMinBounds(const PixelMap* band, long size, Rect* bounds);	// ROM 0x0020d1a8 CalcMinBounds__11TDotPrinterFPC8PixelMaplP4Rect
	Boolean			TryAllocBands(char** bands, long count, long size);			// ROM 0x0020d36c TryAllocBands__11TDotPrinterFPPclT2

	TDotPrinterDriver*	fDriver;		// +0x98
	DotPrinterPrefs	fPrefs;				// +0x9c  the driver's band preferences
	PixelMap		fBands[2];			// +0xa8, +0xc4  the band being drawn (two when the driver bands asynchronously)
	long			fBandHeight72;		// +0xe0  a band's height on the page, in its 72 dpi
	long			fBandHeight;		// +0xe4  ... in the printer's dots
	Rect			fPageBand;			// +0xe8  the band's slice of the page (72 dpi)
	Rect			fBandRect;			// +0xf0  ... and of the printer's page
	Point			fPatOffset;			// +0xf8  added to the page band's corner to align the scaled patterns
	Point			fPatOffset2;		// +0xfc
	long			fBandCount;			// +0x100  1 or 2
	long			fCurBand;			// +0x104  the one being drawn
	RgnHandle		fClip;				// +0x108  the printer port's clip and vis regions the phantom port's
	RgnHandle		fVis;				// +0x10c    clip was worked out from
	long			fBandSize;			// +0x110  bytes in each of the buffers
	PixelMap		fMask;				// +0x114  the shape of a patterned drawing
	PixelMap		fPattern;			// +0x130  the pattern scaled up to the band
	PixelMap		fTurned;			// +0x14c  a landscape band turned for the driver
	Ptr				fMaskBits;			// +0x168  the mask's own buffer
	PatternHandle	fScalePat;			// +0x16c  the pattern fPattern is scaled from
	long			fScalePatAlign;		// +0x170  ... and its alignment then (-1: none yet)
	PrintPort		fPhantom;			// +0x174  the port the shapes are drawn into the band through
};


// The scaling bottlenecks: the printer port's drawing procs
Boolean		SetupScalingBottlenecks(GrafPort* port);					// ROM 0x001c7860 SetupScalingBottlenecks__FP8GrafPort
void		TearDownScalingBottlenecks(GrafPort* port);					// ROM 0x001c791c TearDownScalingBottlenecks__FP8GrafPort
TDotPrinter*	SetupPhantomPort(void);									// ROM 0x001c7ef0 SetupPhantomPort__Fv
void		ReleasePhantomPort(TDotPrinter* printer);					// ROM 0x001c7eb8 ReleasePhantomPort__FP11TDotPrinter
void		RotateBits(PixelMap* src, PixelMap* dst);					// ROM 0x001c89c8 RotateBits__FP8PixelMapT1

// The driver callbacks (DriverCallbacks.h), and the print view's hooks
PrProblemResolution	CallHandleProblem(PrintConnect* connect, TPrinter* printer, NewtonErr problem, TTimeout retryTime, Boolean fixedButton);	// ROM 0x0019441c CallHandleProblem__FP12PrintConnectP8TPrinterlUlUc
void		PrReleaseControl(TTimeout howLong, TPrinter* printer);		// ROM 0x00194448 PrReleaseControl__FUlP8TPrinter
void		PrRegainControl(TPrinter* printer);							// ROM 0x001944a4 PrRegainControl__FP8TPrinter
void		PrintPatchpoint(void);										// ROM 0x001941b0 PrintPatchpoint__Fv
void		InitPrintDrivers(void);										// ROM 0x00192f08 InitPrintDrivers__Fv

#endif	/* __PRINT_PRINTER_H */
