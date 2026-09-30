/*
	File:		print/DotPrinter.cpp

	Contains:	TDotPrinter, the ROM's TPrinter for bitmap printers: the page
				imaged a band at a time through the scaling bottlenecks
				(print/Printer.h).

				A band is a strip of the printer's page as wide as the page
				(portrait) or as tall (landscape, when it is turned before
				the driver sees it) and as deep as the driver's preferences
				and the memory allow: Open asks for the optimum and halves it
				until three buffers of that size (four when the driver takes
				bands asynchronously and a second band is drawn while the
				first is sent) can be had, and gives up below the minimum.
				The page is drawn once per band, the printer port clipped to
				the band's slice of the page (fPageBand) and the phantom
				port's bits the band itself (fBandRect); RepeatPage images
				the band, moves both on and answers whether there is more.

	Reconstructed from the MP2x00 US ROM (0x0020d0f0-0x0020e3f0); each
	function cites its origin.
*/

#include "print/Printer.h"
#include "Ports.h"
#include "Regions.h"
#include "Rects.h"
#include "Frames.h"
#include "NewtonMemory.h"
#include "FixedMath.h"
#include <string.h>
#include <stdint.h>

extern Boolean	gSCPDevicePackageBusy;		// comms/CommManager.cpp: the serial port has a package on it

// how long a busy printer is waited for before it is asked again (the
// ROM's 0x2328000)
static const TTimeout	kPrinterBusyRetry = (TTimeout) 0x2328000;


PROTOCOL_IMPL_SOURCE_MACRO(TDotPrinter)		// ROM 0x0020d0f0 Sizeof__11TDotPrinterSFv
PROTOCOL_CLASSINFO(TDotPrinter, "TPrinter", "", 0x20000, 0, nil)	// ROM 0x00387ffc ClassInfo__11TDotPrinterSFv


// the rows of a band a given number of dots wide: whole words
static inline long
BandRowBytes(long width)
{
	return ((width + 31) & ~31) >> 3;
}


// ROM 0x0020d0f8 Constructor__11TDotPrinterFPc
// The driver made by name, and its connection record made for it.
NewtonErr
TDotPrinter::Constructor(char* driverName)
{
	fError = kPR_ERR_NotFound;
	fCancelled = false;
	fCancelHandled = false;
	fDriver = nil;
	fDriver = (TDotPrinterDriver*) NewByName("TDotPrinterDriver", driverName);
	if (fDriver != nil)
	{
		fError = noErr;
		// (the ROM: operator new of the 0x18-byte record, its two
		// RefStructs made nil)
		PrintConnect* connect = new PrintConnect;
		fDriver->fConnect = connect;
		fDriver->fPrinter = this;
	}
	return fError;
}


// ROM 0x0020d198 SetPortraitOrientation__11TDotPrinterFUc
void
TDotPrinter::SetPortraitOrientation(Boolean portrait)
{
	fDriver->fConnect->fPortrait = portrait;
}


// ROM 0x0020d1a8 CalcMinBounds__11TDotPrinterFPC8PixelMaplP4Rect
// The smallest rectangle round the band's black, found a word at a time:
// the first and last non-zero words give the top and bottom rows, and the
// columns of words scanned from each side the left and right, each a
// multiple of 32 dots (the right one no further than the band's edge).
// An empty band is the empty rectangle at the origin.
void
TDotPrinter::CalcMinBounds(const PixelMap* band, long /*size*/, Rect* bounds)
{
	long top = band->bounds.top;
	long left = band->bounds.left;
	long bottom = band->bounds.bottom;
	long width = band->bounds.right;
	long right = (width + 31) & ~31;
	long rowBytes = band->rowBytes;
	long rowWords = rowBytes >> 2;
	long bytes = rowBytes * (bottom - band->bounds.top);
	const uint32_t* bits = (const uint32_t*) band->baseAddr;
	long words = bytes >> 2;
	const uint32_t* p = bits;
	long n = words;
	long inRow = rowWords;
	for ( ; n > 0; n--)
	{
		if (*p++ != 0)
			break;
		if (--inRow == 0)
		{
			inRow = rowWords;
			top++;
		}
	}
	if (n <= 0)
	{
		right = 0;
		bottom = 0;
		left = 0;
		top = 0;
	}
	else
	{
		p = (const uint32_t*) ((const char*) bits + bytes);
		inRow = rowWords;
		for (n = words; n > 0; n--)
		{
			if (*--p != 0)
				break;
			if (--inRow == 0)
			{
				inRow = rowWords;
				bottom--;
			}
		}
		const uint32_t* firstRow = (const uint32_t*) ((const char*) bits + rowBytes * (top - band->bounds.top));
		// the left: the first column of words with anything in it
		for (long col = rowWords; col > 0; col--)
		{
			const uint32_t* q = firstRow + (rowWords - col);
			long rows;
			for (rows = bottom - top; rows > 0; rows--, q += rowWords)
				if (*q != 0)
					break;
			if (rows > 0)
			{
				left += (rowWords - col) << 5;
				break;
			}
		}
		// the right: the last
		const uint32_t* rowEnd = (const uint32_t*) ((const char*) firstRow + rowBytes);
		for (long col = rowWords; col > 0; col--)
		{
			const uint32_t* q = --rowEnd;
			long rows;
			for (rows = bottom - top; rows > 0; rows--, q += rowWords)
				if (*q != 0)
					break;
			if (rows > 0)
			{
				right -= (rowWords - col) << 5;
				break;
			}
		}
	}
	if (width < right)
		right = width;
	SetRect(bounds, left, top, right, bottom);
}


// ROM 0x0020d36c TryAllocBands__11TDotPrinterFPPclT2
// The buffers a band needs, all of one size, or none.  ROM BUG: when one
// after the first cannot be had the ones before it are given back but
// left in the array - so a failure anywhere but the first leaves bands[0]
// pointing at a freed block, which Open and OpenPage take for success.
Boolean
TDotPrinter::TryAllocBands(char** bands, long count, long size)
{
	for (long i = 0; i < count; i++)
	{
		bands[i] = NewPtr(size);
		if (bands[i] == nil)
		{
			while (--i >= 0)
				DisposPtr(bands[i]);
			return false;
		}
	}
	return true;
}


// ROM 0x0020d3d8 FaxEndPage__11TDotPrinterFl
// The page count, for a driver of version 2 or later.
NewtonErr
TDotPrinter::FaxEndPage(long pageCount)
{
	if (fDriver->ClassInfo()->Version() >= 0x20000)
		fError = fDriver->FaxEndPage(pageCount);
	return fError;
}


// ROM 0x0020d418 Delete__11TDotPrinterFv
void
TDotPrinter::Delete()
{
	if (fDriver != nil)
	{
		PrintConnect* connect = fDriver->fConnect;
		if (connect != nil)
			delete connect;
		fDriver->Delete();
	}
	gSCPDevicePackageBusy = false;
}


// a band's pixel map: its buffer, its rows and its rectangle
static void
SetBand(PixelMap* band, Ptr bits, long rowBytes, const Rect& bounds)
{
	band->baseAddr = bits;
	band->rowBytes = (short) rowBytes;
	band->bounds = bounds;
	band->pixMapFlags = kPixMapPtr | kPixMapDevDotPrint | 1;
	band->deviceRes.v = 0;
	band->deviceRes.h = 0;
	band->grayTable = nil;
}


// ROM 0x0020d474 Open__11TDotPrinterFRC6RefVar
// The driver opened (a busy printer waited for, ten seconds at a time,
// until it is free or the job is cancelled), the port set up to the
// driver's page, and the bands allocated: the optimum height the driver
// asks for, halved until the buffers can be had.
NewtonErr
TDotPrinter::Open(RefArg connectInfo)
{
	if (CheckUserAbort())
		return fError;
	SetupConnect(fDriver->fConnect, connectInfo);
	do
	{
		gSCPDevicePackageBusy = true;
		fError = fDriver->Open();
		if (fError == kPR_ERR_Busy)
		{
			PrReleaseControl(kPrinterBusyRetry, this);
			CheckUserAbort();
		}
	} while (fError == kPR_ERR_Busy);
	if (fError != noErr)
		return fError;

	PrPageInfo info;
	fDriver->GetPageInfo(&info);
	OpenPort(info);
	GrafPort* port = GetPrinterPort();
	port->portBits.pixMapFlags |= kPixMapDevDotPrint;
	SetupScalingBottlenecks(port);
	fDriver->GetBandPrefs(&fPrefs);
	long count = fPrefs.asyncBanding ? 2 : 1;
	fBandCount = count;
	long rowBytes = BandRowBytes(GetScalerInfo()->toRect.right);
	// ROM BUG: the buffers' pointers are not cleared first, so a driver
	// whose minimum band is above its optimum leaves Open looking at stack
	// rubbish here (DEVIATION: noughts on the host)
	char* buffers[4] = { nil, nil, nil, nil };
	long height = fPrefs.optimumBand;
	long size = 0;
	while (fPrefs.minBand <= height)
	{
		size = rowBytes * height;
		if (TryAllocBands(buffers, count + 2, size))
			break;
		height >>= 1;
	}
	if (buffers[0] == nil)
	{
		fDriver->Close();
		fError = kPR_ERR_NewtonError;
		return fError;
	}
	fBandSize = size;
	SetRect(&fBandRect, 0, 0, GetScalerInfo()->toRect.right, (short) height);
	SetBand(&fBands[0], buffers[0], rowBytes, fBandRect);
	if (fBandCount == 2)
	{
		fBands[1] = fBands[0];
		fBands[1].baseAddr = buffers[3];
	}
	else
		fBands[1].baseAddr = nil;
	fBandHeight = height;
	fBandHeight72 = (short) ((FixedDivide(ToFixed(height), GetScalerInfo()->scaleRatios.y) + 0x8000) >> 16);
	fCurBand = 0;
	fClip = NewRgn();
	fVis = NewRgn();
	fMask = fBands[0];
	fPattern = fBands[0];
	fMask.baseAddr = buffers[1];
	fMaskBits = buffers[1];
	fPattern.baseAddr = buffers[2];
	fScalePat = GetStdPattern(blackPat);
	fPhantom.prObject = this;
	::OpenPort(&fPhantom.port);
	fPhantom.port.portBits = fBands[0];
	fPhantom.port.portRect = fBands[0].bounds;
	return fError;
}


// ROM 0x0020d764 Close__11TDotPrinterFv
NewtonErr
TDotPrinter::Close()
{
	fError = fDriver->Close();
	::ClosePort(&fPhantom.port);
	if (fBands[0].baseAddr != nil)
	{
		DisposPtr(fBands[0].baseAddr);
		if (fBandCount > 1)
			DisposPtr(fBands[1].baseAddr);
		DisposPtr(fMaskBits);
		DisposPtr(fPattern.baseAddr);
	}
	DisposeRgn(fClip);
	DisposeRgn(fVis);
	TearDownScalingBottlenecks(GetPort());
	ClosePort();
	return fError;
}


// ROM 0x0020d7e4 OpenPage__11TDotPrinterFv
// A page begun: the port's rectangle turned to the page's orientation
// (taller than wide for portrait), its first band's slice clipped to, and
// the phantom port set on the first band, cleared.  A page turned the
// other way from the last means new bands of the other shape: the old
// ones are given back and the scaling worked out again with the driver's
// page turned.
NewtonErr
TDotPrinter::OpenPage()
{
	long bandIndex = fCurBand;
	GrafPort* port = GetPort();
	if (CheckUserAbort())
		return fError;
	fError = fDriver->OpenPage();
	if (fError != noErr)
		return fError;

	Boolean portrait = fDriver->fConnect->fPortrait;
	if ((portrait && port->portRect.right > port->portRect.bottom)
	 || (!portrait && port->portRect.bottom > port->portRect.right))
	{
		short right = port->portRect.right;
		port->portRect.right = port->portRect.bottom;
		port->portRect.bottom = right;
	}
	port->portBits.bounds = port->portRect;
	port->portBits.rowBytes = (short) BandRowBytes(port->portRect.right);
	// (the ROM's SetPort answers the port it replaces)
	GrafPort* savedPort = GetCurrentPort();
	SetPort(port);
	SetOrigin(0, 0);
	Rect r = port->portRect;
	if (portrait)
		r.bottom = (short) fBandHeight72;
	else
		r.right = (short) fBandHeight72;
	fPageBand = r;
	RectRgn(port->visRgn, &r);
	RectRgn(fClip, &r);
	RectRgn(fVis, &r);
	PenNormal();
	if (fPrefs.asyncBanding || !portrait)
		fBandCount = 2;
	else
		fBandCount = 1;
	bandIndex = (fPrefs.asyncBanding && portrait) ? 1 - bandIndex : 0;

	SetPort(&fPhantom.port);
	r.top = 0;
	r.left = 0;
	PrintPatchpoint();
	ScalerInfo* scaler = GetScalerInfo();
	if ((portrait && scaler->fromRect.right != r.right)
	 || (!portrait && scaler->fromRect.bottom != r.bottom))
	{
		// the page turned: new bands of the other shape
		DisposPtr(fBands[0].baseAddr);
		fBands[0].baseAddr = nil;
		if (fBands[1].baseAddr != nil)
		{
			DisposPtr(fBands[1].baseAddr);
			fBands[1].baseAddr = nil;
		}
		DisposPtr(fMask.baseAddr);
		fMask.baseAddr = nil;
		DisposPtr(fPattern.baseAddr);
		fPattern.baseAddr = nil;
		PrPageInfo info;
		fDriver->GetPageInfo(&info);
		Fixed dpi = info.printerDPI.x;
		info.printerDPI.x = info.printerDPI.y;
		info.printerDPI.y = dpi;
		short v = info.printerPageSize.v;
		info.printerPageSize.v = info.printerPageSize.h;
		info.printerPageSize.h = v;
		SetScalerInfo(info);
		if (portrait)
		{
			r.right = GetScalerInfo()->toRect.right;
			r.bottom = (short) fBandHeight;
		}
		else
		{
			r.right = (short) fBandHeight;
			r.bottom = GetScalerInfo()->toRect.bottom;
		}
		PrintPatchpoint();
		long count = fBandCount + 2;
		long height = fBandHeight;
		long rowBytes = BandRowBytes(r.right);
		long size = rowBytes * r.bottom;
		char* buffers[4];
		while (!TryAllocBands(buffers, count, size))
		{
			height >>= 1;
			if (fPrefs.minBand > height)
				return fError = kPR_ERR_NewtonError;
			if (portrait)
				r.bottom = (short) height;
			else
			{
				r.right = (short) height;
				rowBytes = BandRowBytes(height);
			}
			size = rowBytes * r.bottom;
		}
		fBandHeight = height;
		fBandSize = size;
		Fixed ratio = portrait ? GetScalerInfo()->scaleRatios.y : GetScalerInfo()->scaleRatios.x;
		fBandHeight72 = (short) ((FixedDivide(ToFixed(height), ratio) + 0x8000) >> 16);
		SetBand(&fBands[0], fBands[0].baseAddr, rowBytes, r);
		fMask = fBands[0];
		fPattern = fBands[0];
		fBands[0].baseAddr = buffers[0];
		fMask.baseAddr = buffers[1];
		fMaskBits = buffers[1];
		fPattern.baseAddr = buffers[2];
		if (fBandCount == 2)
		{
			fBands[1] = fBands[0];
			fBands[1].baseAddr = buffers[3];
		}
		Rect pageBand = port->portRect;
		if (portrait)
			pageBand.bottom = (short) fBandHeight72;
		else
			pageBand.right = (short) fBandHeight72;
		fPageBand = pageBand;
		RectRgn(port->visRgn, &pageBand);
		RectRgn(fClip, &pageBand);
		RectRgn(fVis, &pageBand);
		PrintPatchpoint();
		if (!portrait)
		{
			// the band turned for the driver: the mask's buffer when the
			// bands are sent asynchronously (the two alternate), the
			// second band's otherwise
			fTurned = fPrefs.asyncBanding ? fMask : fBands[1];
			fTurned.rowBytes = (short) BandRowBytes(r.bottom);
			fTurned.bounds.bottom = r.right;
			fTurned.bounds.right = r.bottom;
		}
	}
	else if (portrait)
	{
		r.right = scaler->toRect.right;
		r.bottom = (short) fBandHeight;
	}
	else
	{
		r.right = (short) fBandHeight;
		r.bottom = scaler->toRect.bottom;
		fTurned.bounds.top = 0;
		fTurned.bounds.bottom = (short) fBandHeight;
	}
	PrintPatchpoint();
	fBandRect = r;
	fCurBand = bandIndex;
	PixelMap* band;
	if (portrait)
	{
		fBands[bandIndex].bounds = r;
		band = &fBands[bandIndex];
	}
	else
	{
		fBands[0].bounds = r;
		band = &fBands[0];
	}
	SetPortBits(band);
	ZeroBytes(band->baseAddr, fBandSize);
	fPhantom.port.portRect = r;
	RectRgn(fPhantom.port.clipRgn, &r);
	RectRgn(fPhantom.port.visRgn, &r);
	PenNormal();
	fPatOffset.v = fPatOffset.h = 0;
	fPatOffset2.v = fPatOffset2.h = 0;
	fScalePatAlign = -1;
	SetPort(savedPort);
	return fError;
}


// ROM 0x0020dee0 RepeatPage__11TDotPrinterFv
// The band finished: imaged (the smallest rectangle round its black, if
// the driver wants it; a landscape band turned first), and if there is
// more of the page the next band set up and cleared and the printer port
// clipped to its slice of the page.  ==> whether the page is to be drawn
// again.
Boolean
TDotPrinter::RepeatPage()
{
	if (CheckUserAbort() || fError != noErr)
		return false;
	long bandIndex = fCurBand;
	Boolean portrait = fDriver->fConnect->fPortrait;
	Rect minRect;
	if (fPrefs.wantMinBounds && portrait)
		CalcMinBounds(&fBands[bandIndex], fBandSize, &minRect);
	else
		minRect = fBands[bandIndex].bounds;
	PrintPatchpoint();
	if (portrait)
	{
		fError = fDriver->ImageBand(&fBands[bandIndex], &minRect);
		if (fError != noErr)
			return false;
		fBandRect.top += (short) fBandHeight;
		fBandRect.bottom += (short) fBandHeight;
		if (GetScalerInfo()->toRect.bottom < fBandRect.bottom)
			fBandRect.bottom = GetScalerInfo()->toRect.bottom;
		if (fBandRect.top >= fBandRect.bottom)
			return false;
	}
	else
	{
		RotateBits(&fBands[0], &fTurned);
		fError = fDriver->ImageBand(&fTurned, &fTurned.bounds);
		if (fError != noErr)
			return false;
		fBandRect.left += (short) fBandHeight;
		fBandRect.right += (short) fBandHeight;
		if (GetScalerInfo()->toRect.right < fBandRect.right)
			fBandRect.right = GetScalerInfo()->toRect.right;
		if (fBandRect.left >= fBandRect.right)
			return false;
	}

	Rect r = fBandRect;
	bandIndex = (fPrefs.asyncBanding && portrait) ? 1 - bandIndex : 0;
	fCurBand = bandIndex;
	PrintPatchpoint();
	PixelMap* band;
	if (portrait)
	{
		fBands[bandIndex].bounds = r;
		band = &fBands[bandIndex];
	}
	else
	{
		fTurned.bounds.top = r.left;
		fTurned.bounds.bottom = r.right;
		if (GetScalerInfo()->toRect.right < fTurned.bounds.bottom)
			fTurned.bounds.bottom = GetScalerInfo()->toRect.right;
		if (fPrefs.asyncBanding)
		{
			Ptr bits = fMaskBits;
			if (fMask.baseAddr == bits)
				bits = fBands[1].baseAddr;
			fMask.baseAddr = bits;
			fTurned.baseAddr = bits;
		}
		fBands[0].bounds = r;
		band = &fBands[0];
	}
	fPhantom.port.portBits = *band;
	ZeroBytes(band->baseAddr, fBandSize);
	fPhantom.port.portRect = r;
	RectRgn(fPhantom.port.clipRgn, &r);
	RectRgn(fPhantom.port.visRgn, &r);
	// (the ROM's SetPort answers the port it replaces)
	GrafPort* savedPort = GetCurrentPort();
	SetPort(&fPhantom.port);
	PenNormal();
	PrintPatchpoint();
	fMask.bounds = r;
	fPattern.bounds = r;
	fScalePatAlign = -1;
	if (portrait)
	{
		fPageBand.top += (short) fBandHeight72;
		fPageBand.bottom += (short) fBandHeight72;
		if (GetScalerInfo()->fromRect.bottom < fPageBand.bottom)
			fPageBand.bottom = GetScalerInfo()->fromRect.bottom;
	}
	else
	{
		fPageBand.left += (short) fBandHeight72;
		fPageBand.right += (short) fBandHeight72;
		if (GetScalerInfo()->fromRect.right < fPageBand.right)
			fPageBand.right = GetScalerInfo()->fromRect.right;
	}
	r = fPageBand;
	GrafPort* port = GetPort();
	SetPort(port);
	SetOrigin(0, 0);
	RectRgn(port->visRgn, &r);
	RectRgn(fClip, &r);
	RectRgn(fVis, &r);
	PenNormal();
	fPatOffset.v = fPatOffset.h = 0;
	fPatOffset2.v = fPatOffset2.h = 0;
	SetPort(savedPort);
	return true;
}


// ROM 0x0020e3b8 ClosePage__11TDotPrinterFv
NewtonErr
TDotPrinter::ClosePage()
{
	fError = fDriver->ClosePage();
	CheckUserAbort();
	return fError;
}


// ROM 0x0020e3e4 CancelJob__11TDotPrinterFUc
void
TDotPrinter::CancelJob(Boolean asyncCancel)
{
	fDriver->CancelJob(asyncCancel);
}


// ROM 0x0020e3f0 IsProblemResolved__11TDotPrinterFv
PrProblemResolution
TDotPrinter::IsProblemResolved()
{
	return fDriver->IsProblemResolved();
}
