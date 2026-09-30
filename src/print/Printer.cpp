/*
	File:		print/Printer.cpp

	Contains:	TPrinter's shared code: the port a page is drawn into, the
				scaling from it to the printer's dots, the connection frame
				and the cancelling; and the interfaces' glue.

	Reconstructed from the MP2x00 US ROM (0x0021bc28-0x0021c0c8, the glue at
	0x00387f2c-0x003880fc); each function cites its origin.
*/

#include "print/Printer.h"
#include "Ports.h"
#include "Regions.h"
#include "Rects.h"
#include "Frames.h"
#include "RSSymbols.h"
#include "FixedMath.h"

/*------------------------------------------------------------------------------
	T P r i n t e r
------------------------------------------------------------------------------*/

// ROM 0x00387f38 Delete__8TPrinterFv
void
TPrinter::Delete()
{
	GlueDelete();
}


// The page's rectangle at 72 dots an inch, the printer's in its own dots,
// and the ratio between them - how many of the printer's dots one of the
// page's is (TPrinter::OpenPort and SetScalerInfo share it).  The
// rectangles are rounded to the nearest dot.
static void
SetScaler(ScalerInfo* scaler, const PrPageInfo& info, Rect* page72)
{
	Fixed width = FixedDivide(ToFixed(info.printerPageSize.h), info.printerDPI.x);
	Fixed height = FixedDivide(ToFixed(info.printerPageSize.v), info.printerDPI.y);
	page72->top = 0;
	page72->left = 0;
	page72->right = (short) ((FixedMultiply(width, ToFixed(72)) + 0x8000) >> 16);
	page72->bottom = (short) ((FixedMultiply(height, ToFixed(72)) + 0x8000) >> 16);
	Rect dots;
	SetRect(&dots, 0, 0, info.printerPageSize.h, info.printerPageSize.v);
	scaler->fromRect = *page72;
	scaler->toRect = dots;
	scaler->scaleRatios.x = FixedDivide(ToFixed((short) (dots.right - dots.left)), ToFixed((short) (page72->right - page72->left)));
	scaler->scaleRatios.y = FixedDivide(ToFixed((short) (dots.bottom - dots.top)), ToFixed((short) (page72->bottom - page72->top)));
}


// ROM 0x0021bc28 OpenPort__8TPrinterFRC10PrPageInfo
// The port the page is drawn into: the paper at 72 dots an inch, with no
// bits of its own (the scaling bottlenecks draw everything into the
// band), a clip region of every coordinate and its printer object after
// it (PrintPort).
void
TPrinter::OpenPort(const PrPageInfo& info)
{
	Rect page72;
	SetScaler(&fScaler, info, &page72);
	GrafPort* port = GetPrinterPort();
	::OpenPort(port);
	port->portBits.baseAddr = nil;
	port->portBits.bounds = page72;
	port->portBits.pixMapFlags = kPixMapPtr | 1;
	port->portBits.deviceRes.v = 0;
	port->portBits.deviceRes.h = 0;
	port->portBits.grayTable = nil;
	port->portRect = page72;
	RectRgn(port->visRgn, &page72);
	Rect everything;
	SetRect(&everything, -0x7fff, -0x7fff, 0x7fff, 0x7fff);
	RectRgn(port->clipRgn, &everything);
	fPort.prObject = this;
}


// ROM 0x0021bde4 ClosePort__8TPrinterFv
void
TPrinter::ClosePort()
{
	::ClosePort(GetPort());
}


// ROM 0x0021bdfc GetPrinterPort__8TPrinterFv
GrafPort*
TPrinter::GetPrinterPort()
{
	return &fPort.port;
}


// ROM 0x0021be04 GetPort__8TPrinterFv
GrafPort*
TPrinter::GetPort()
{
	return &fPort.port;
}


// ROM 0x0021be0c GetScalerInfo__8TPrinterFv
ScalerInfo*
TPrinter::GetScalerInfo()
{
	return &fScaler;
}


// ROM 0x0021be14 SetScalerInfo__8TPrinterFRC10PrPageInfo
// The scaling worked out again (a page turned the other way).
void
TPrinter::SetScalerInfo(const PrPageInfo& info)
{
	Rect page72;
	SetScaler(&fScaler, info, &page72);
}


// ROM 0x0021bf3c SetupConnect__8TPrinterFP12PrintConnectRC6RefVar
// The job's connection: the frame the print view was given, and the
// paper - A4 when its printerPageBounds say so, U.S. letter otherwise.
void
TPrinter::SetupConnect(PrintConnect* connect, RefArg connectInfo)
{
	Boolean letter = true;
	RefVar bounds(GetFrameSlot(connectInfo, RSSYMprinterpagebounds));
	if (NOTNIL(bounds))
	{
		RefVar id(GetFrameSlot(bounds, RSSYMid));
		letter = !EQ(id, RSSYMa4);
	}
	RefVar paper(letter ? RSSYMletter : RSSYMa4);
	connect->fVersion = 0x10000;
	connect->fConnectInfo = connectInfo;
	connect->fPaperSize = paper;
	connect->fManualFeed = false;
	connect->fPrintQuality = kBest;
	connect->fPortrait = true;
}


// ROM 0x0021c034 DoUserAbort__8TPrinterFv
// The user cancelled: the job is cancelled at once, asynchronously.
void
TPrinter::DoUserAbort()
{
	if (fCancelled)
		return;
	fCancelled = true;
	fCancelHandled = true;
	CancelJob(true);
}


// ROM 0x0021c050 CheckUserAbort__8TPrinterFv
// Whether the job has been cancelled: the first time it is asked after a
// cancel, a printing error (-44000..-44099) is kept and anything else
// becomes kPR_ERR_UserCancel, and the job is told.
Boolean
TPrinter::CheckUserAbort()
{
	if (fCancelled && !fCancelHandled)
	{
		fCancelHandled = true;
		if (!(fError >= kPR_ERR_MINERROR && fError <= kPR_ERR_MAXERROR))
			fError = kPR_ERR_UserCancel;
		CancelJob(false);
	}
	return fError == kPR_ERR_UserCancel;
}


/*------------------------------------------------------------------------------
	T D o t P r i n t e r D r i v e r
------------------------------------------------------------------------------*/

// ROM 0x00388074 Delete__17TDotPrinterDriverFv
void
TDotPrinterDriver::Delete()
{
	GlueDelete();
}
