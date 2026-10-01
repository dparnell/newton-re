/*
	File:		HostPrinter.cpp

	Contains:	The host's printer (print/host/HostPrinter.h): a
				TDotPrinterDriver writing each page to a PNG file, its printer
				frame offered in AvailablePrinters, and the PNG writer.

				Host only (not in the ROM).  The page is kept whole in host
				memory while TDotPrinter hands it over a band at a time, and
				written when the page closes.
*/

#include "print/host/HostPrinter.h"
#include "print/host/HostIPP.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Ports.h"
#include "Interpreter.h"
#include "RSSymbols.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PROTOCOL_IMPL_SOURCE_MACRO(THostPrinterDriver)
PROTOCOL_CLASSINFO(THostPrinterDriver, "TDotPrinterDriver", "", 0x20000, 0, nil)

static char		gPrintDirectory[1024] = "";
static long		gPagesPrinted = 0;

// the printable area (72 dots an inch) and where it starts on the sheet, as
// the ROM's StyleWriter frames give them
static const long	kLetterAreaRight = 576, kLetterAreaBottom = 752;
static const long	kA4AreaRight = 556, kA4AreaBottom = 796;
static const long	kOriginTop = 14, kOriginLeft = 18;


/*------------------------------------------------------------------------------
	The PNG writer: one bit a pixel, gray, deflate's stored blocks
------------------------------------------------------------------------------*/

static uint32_t
CRC32(uint32_t crc, const unsigned char* data, size_t length)
{
	static uint32_t table[256];
	static bool made = false;
	if (!made)
	{
		for (uint32_t n = 0; n < 256; n++)
		{
			uint32_t c = n;
			for (int k = 0; k < 8; k++)
				c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
			table[n] = c;
		}
		made = true;
	}
	crc = ~crc;
	for (size_t i = 0; i < length; i++)
		crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
	return ~crc;
}


static void
PutWord(unsigned char* p, uint32_t v)
{
	p[0] = (unsigned char) (v >> 24);
	p[1] = (unsigned char) (v >> 16);
	p[2] = (unsigned char) (v >> 8);
	p[3] = (unsigned char) v;
}


static bool
WriteChunk(FILE* f, const char* type, const unsigned char* data, size_t length)
{
	unsigned char head[8];
	PutWord(head, (uint32_t) length);
	memcpy(head + 4, type, 4);
	uint32_t crc = CRC32(0, head + 4, 4);
	crc = CRC32(crc, data, length);
	unsigned char tail[4];
	PutWord(tail, crc);
	return fwrite(head, 1, 8, f) == 8
		&& (length == 0 || fwrite(data, 1, length, f) == length)
		&& fwrite(tail, 1, 4, f) == 4;
}


bool
HostWritePNG1(const char* path, const unsigned char* bits, long width, long height, long rowBytes)
{
	// the filtered image: each row a filter byte (none) and the row, with
	// its bits inverted (the PNG's 0 is black)
	long pngRow = (width + 7) / 8;
	size_t rawSize = (size_t) (pngRow + 1) * height;
	unsigned char* raw = (unsigned char*) malloc(rawSize);
	if (raw == nil)
		return false;
	for (long y = 0; y < height; y++)
	{
		unsigned char* out = raw + (size_t) y * (pngRow + 1);
		out[0] = 0;
		const unsigned char* in = bits + (size_t) y * rowBytes;
		for (long i = 0; i < pngRow; i++)
			out[1 + i] = (unsigned char) ~in[i];
		if (width & 7)
			out[pngRow] |= (unsigned char) (0xff >> (width & 7));		// the pad bits white
	}
	// the zlib stream: stored blocks of at most 65535 bytes, and the
	// Adler-32 of the data
	size_t blocks = (rawSize + 65534) / 65535;
	size_t zSize = 2 + rawSize + blocks * 5 + 4;
	unsigned char* z = (unsigned char*) malloc(zSize);
	if (z == nil)
	{
		free(raw);
		return false;
	}
	unsigned char* p = z;
	*p++ = 0x78;
	*p++ = 0x01;
	uint32_t a = 1, b = 0;
	for (size_t done = 0; done < rawSize; )
	{
		size_t n = rawSize - done > 65535 ? 65535 : rawSize - done;
		*p++ = (unsigned char) (done + n == rawSize ? 1 : 0);
		*p++ = (unsigned char) n;
		*p++ = (unsigned char) (n >> 8);
		*p++ = (unsigned char) ~n;
		*p++ = (unsigned char) (~n >> 8);
		memcpy(p, raw + done, n);
		for (size_t i = 0; i < n; i++)
		{
			a = (a + raw[done + i]) % 65521;
			b = (b + a) % 65521;
		}
		p += n;
		done += n;
	}
	PutWord(p, (b << 16) | a);
	p += 4;
	free(raw);

	FILE* f = fopen(path, "wb");
	bool ok = f != nil;
	if (ok)
	{
		static const unsigned char kSignature[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
		unsigned char header[13];
		PutWord(header, (uint32_t) width);
		PutWord(header + 4, (uint32_t) height);
		header[8] = 1;		// one bit a pixel
		header[9] = 0;		// gray
		header[10] = 0;		// deflate
		header[11] = 0;		// the adaptive filters
		header[12] = 0;		// not interlaced
		ok = fwrite(kSignature, 1, 8, f) == 8
		  && WriteChunk(f, "IHDR", header, sizeof(header))
		  && WriteChunk(f, "IDAT", z, (size_t) (p - z))
		  && WriteChunk(f, "IEND", nil, 0);
		ok = fclose(f) == 0 && ok;
	}
	free(z);
	return ok;
}


/*------------------------------------------------------------------------------
	T H o s t P r i n t e r D r i v e r
------------------------------------------------------------------------------*/

THostPrinterDriver*
THostPrinterDriver::New()
{
	fSheet = nil;
	fSheetWidth = fSheetHeight = fSheetRowBytes = 0;
	fAreaLeft = fAreaTop = fAreaWidth = fAreaHeight = 0;
	fCancelled = false;
	return this;
}


void
THostPrinterDriver::Delete()
{
	free(fSheet);
	fSheet = nil;
}


// A slot of a frame as a long, or the default when it is not an integer.
static long
IntSlot(RefArg frame, RefArg slot, long otherwise)
{
	if (!IsFrame(frame))
		return otherwise;
	RefVar value(GetFrameSlot(frame, slot));
	return ISINT(value) ? RINT(value) : otherwise;
}


// The sheet and the printable area on it worked out from the job's paper
// and the printer frame: the area's size from its printerPageBounds (once
// the Print slip has chosen one of them, a single rectangle) and where it
// starts from its printableOrigin.
NewtonErr
THostPrinterDriver::Open()
{
	fCancelled = false;
	Boolean a4 = EQ(fConnect->fPaperSize, RSSYMa4);
	RefVar info(fConnect->fConnectInfo);
	RefVar bounds(IsFrame(info) ? GetFrameSlot(info, RSSYMprinterpagebounds) : NILREF);
	long right = IntSlot(bounds, RSSYMright, a4 ? kA4AreaRight : kLetterAreaRight);
	long bottom = IntSlot(bounds, RSSYMbottom, a4 ? kA4AreaBottom : kLetterAreaBottom);
	RefVar origin(IsFrame(info) ? GetFrameSlot(info, RefVar(Intern((char*) "printableOrigin"))) : NILREF);
	long top = IntSlot(origin, RSSYMtop, kOriginTop);
	long left = IntSlot(origin, RSSYMleft, kOriginLeft);

	fSheetWidth = a4 ? 2480 : 2550;			// 210 x 297 mm, or 8.5 x 11 inches
	fSheetHeight = a4 ? 3508 : 3300;
	fSheetRowBytes = (fSheetWidth + 7) / 8;
	fAreaLeft = left * kHostPrinterDPI / 72;
	fAreaTop = top * kHostPrinterDPI / 72;
	fAreaWidth = (right * kHostPrinterDPI + 71) / 72;
	fAreaHeight = (bottom * kHostPrinterDPI + 71) / 72;
	if (fAreaLeft + fAreaWidth > fSheetWidth)
		fAreaWidth = fSheetWidth - fAreaLeft;
	if (fAreaTop + fAreaHeight > fSheetHeight)
		fAreaHeight = fSheetHeight - fAreaTop;
	free(fSheet);
	fSheet = (unsigned char*) malloc((size_t) fSheetRowBytes * fSheetHeight);
	return fSheet != nil ? noErr : kPR_ERR_NewtonError;
}


NewtonErr
THostPrinterDriver::Close()
{
	free(fSheet);
	fSheet = nil;
	return noErr;
}


NewtonErr
THostPrinterDriver::OpenPage()
{
	memset(fSheet, 0, (size_t) fSheetRowBytes * fSheetHeight);
	return noErr;
}


// The page written: <dir>/print-NNN.png.
NewtonErr
THostPrinterDriver::ClosePage()
{
	if (fCancelled)
		return noErr;
	long number = gPagesPrinted + 1;
	char path[1100];
	snprintf(path, sizeof(path), "%s%sprint-%03ld.png", gPrintDirectory,
			 gPrintDirectory[0] != 0 ? "/" : "", number);
	if (!HostWritePNG1(path, fSheet, fSheetWidth, fSheetHeight, fSheetRowBytes))
	{
		fprintf(stderr, "[host] printer: could not write %s\n", path);
		return kPR_ERR_NewtonError;
	}
	gPagesPrinted = number;
	printf("[host] printed page %ld to %s\n", number, path);
	fflush(stdout);
	return noErr;
}


// A band pasted into the sheet: its rows are the printable area's rows
// band->bounds.top onwards, its dots the area's from the left.
NewtonErr
THostPrinterDriver::ImageBand(PixelMap* band, const Rect* /*minRect*/)
{
	long rows = band->bounds.bottom - band->bounds.top;
	long shift = fAreaLeft & 7;
	long firstByte = fAreaLeft >> 3;
	long areaBytes = (fAreaWidth + 7) / 8;
	if (areaBytes > band->rowBytes)
		areaBytes = band->rowBytes;
	for (long y = 0; y < rows; y++)
	{
		long areaRow = band->bounds.top + y;
		if (areaRow < 0 || areaRow >= fAreaHeight)
			continue;
		const unsigned char* in = (const unsigned char*) band->baseAddr + (size_t) y * band->rowBytes;
		unsigned char* out = fSheet + (size_t) (fAreaTop + areaRow) * fSheetRowBytes;
		for (long i = 0; i < areaBytes; i++)
		{
			long at = firstByte + i;
			if (at >= fSheetRowBytes)
				break;
			out[at] |= (unsigned char) (in[i] >> shift);
			if (shift != 0 && at + 1 < fSheetRowBytes)
				out[at + 1] |= (unsigned char) (in[i] << (8 - shift));
		}
	}
	return noErr;
}


void
THostPrinterDriver::CancelJob(Boolean /*asyncCancel*/)
{
	fCancelled = true;
}


PrProblemResolution
THostPrinterDriver::IsProblemResolved()
{
	return kPrProblemFixed;
}


void
THostPrinterDriver::GetPageInfo(PrPageInfo* info)
{
	info->printerDPI.x = ToFixed(kHostPrinterDPI);
	info->printerDPI.y = ToFixed(kHostPrinterDPI);
	info->printerPageSize.h = (short) fAreaWidth;
	info->printerPageSize.v = (short) fAreaHeight;
}


// Bands of 200 dots (48 of the page's), halved as far as 25 when the
// memory is short: a multiple of 25 dots is a whole number of the page's
// at 300 dpi, so the bands meet without a seam.
void
THostPrinterDriver::GetBandPrefs(DotPrinterPrefs* prefs)
{
	prefs->minBand = 25;
	prefs->optimumBand = 200;
	prefs->asyncBanding = false;
	prefs->wantMinBounds = false;
}


NewtonErr
THostPrinterDriver::FaxEndPage(long /*pageCount*/)
{
	return noErr;
}


/*------------------------------------------------------------------------------
	I n s t a l l i n g   i t
------------------------------------------------------------------------------*/

void
HostSetPrintDirectory(const char* dir)
{
	snprintf(gPrintDirectory, sizeof(gPrintDirectory), "%s", dir != nil ? dir : "");
	size_t n = strlen(gPrintDirectory);
	while (n > 1 && (gPrintDirectory[n - 1] == '/' || gPrintDirectory[n - 1] == '\\'))
		gPrintDirectory[--n] = 0;
}


static Ref
MakeBounds(long right, long bottom)
{
	RefVar bounds(AllocateFrame());
	SetFrameSlot(bounds, RSSYMbottom, MAKEINT(bottom));
	SetFrameSlot(bounds, RSSYMright, MAKEINT(right));
	return bounds;
}


// HostPagesPrinted(): how many pages the host's printer has written, for a
// script waiting on a print job (ctest host.NewtonHostPrinter)
static Ref
FHostPagesPrinted(RefArg /*rcvr*/)
{
	return MAKEINT(gPagesPrinted);
}


void
HostInstallPrinter(void)
{
	THostPrinterDriver::ClassInfo()->Register();
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostPagesPrinted")), RefVar(MakeCFunction((void*) FHostPagesPrinted, 0, nil)));
	RefVar printers(GetFrameSlot(RefVar(gVarFrame), RSSYMavailableprinters));
	if (!IsArray(printers))
		return;
	RefVar printer(AllocateFrame());
	SetFrameSlot(printer, RSSYMname, MakeString(kHostPrinterName));
	SetFrameSlot(printer, RSSYMdrivername, MakeString(kHostPrinterDriverName));
	SetFrameSlot(printer, RSSYMimagingname, MakeString("TDotPrinter"));
	SetFrameSlot(printer, RSSYMtype, RefVar(Intern((char*) "serialSym")));
	RefVar pageBounds(AllocateFrame());
	SetFrameSlot(pageBounds, RefVar(Intern((char*) "eightByEleven")), MakeBounds(kLetterAreaRight, kLetterAreaBottom));
	SetFrameSlot(pageBounds, RSSYMa4, MakeBounds(kA4AreaRight, kA4AreaBottom));
	SetFrameSlot(printer, RSSYMprinterpagebounds, pageBounds);
	RefVar origin(AllocateFrame());
	SetFrameSlot(origin, RSSYMtop, MAKEINT(kOriginTop));
	SetFrameSlot(origin, RSSYMleft, MAKEINT(kOriginLeft));
	SetFrameSlot(printer, RefVar(Intern((char*) "printableOrigin")), origin);
	AddArraySlot(printers, printer);
	// and the printers on the network, when there is one (HostIPP.h)
	HostInstallIPPPrinters();
}
