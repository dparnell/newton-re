/*
	File:		print/ScalingBottlenecks.cpp

	Contains:	The dot printer's scaling bottlenecks: the drawing procs of
				a printer port (TDotPrinter::Open installs them), which draw
				each shape of the 72-dpi page into the band at the
				printer's resolution.

				Every one of them goes the same way.  A shape drawn while a
				picture, region or polygon is being recorded into the port
				is first given to the standard proc as it is, at 72 dpi, so
				the recording is the page's own; one drawn with the pen
				hidden is drawn nowhere else.  Otherwise SetupPhantomPort
				makes the phantom port current - its bits the band, its clip
				the printer port's clip and visible region scaled up - the
				shape is mapped from the page's rectangle to the printer's
				(MapRect, MapRgn, MapPoly ...), and if it meets the band it
				is drawn: directly, when it is inverted or its pattern is
				all white or all black, and otherwise into the mask in
				black, the pattern scaled up to the band (UpdateScalePat)
				and the two combined into the band in the pen's mode
				(TransferShape) - a 72-dpi pattern drawn at 200 dpi would
				otherwise come out a third of its size.  ReleasePhantomPort
				makes the printer port current again.

	Reconstructed from the MP2x00 US ROM (0x001c7860-0x001c97b4); each
	function cites its origin.
*/

#include "print/Printer.h"
#include "Ports.h"
#include "Regions.h"
#include "Rects.h"
#include "Draw.h"
#include "Shapes.h"
#include "Polygons.h"
#include "Curves.h"
#include "Paths.h"
#include "PicPlay.h"
#include "PicRecord.h"
#include "TextObject.h"
#include "Text.h"
#include "NewtonMemory.h"
#include "FixedMath.h"
#include "toolbox/ByteOrder.h"
#include <string.h>
#include <stdint.h>

extern const unsigned char	kDitherPatterns[128];		// print/DitherTables.cpp: sixteen grays

static void	TransferVerbInfo(TDotPrinter* printer, GrafVerb verb, PatternHandle* pattern);
static void	UpdateScalePat(TDotPrinter* printer, PatternHandle pattern);
static Boolean	DitherPattern(PatternHandle pattern);
static void	ConvertPattern(uint32_t* src, UChar* dst, long depth);
static void	TransferShape(TDotPrinter* printer);
static Boolean	WhiteOrBlackPat(PatternHandle pattern);

void	ScaleStdText(TextObjectRef text, Fixed hScale, Fixed vScale);
void	ScaleStdCurve(GrafVerb verb, curve* c);
void	ScaleStdPaths(GrafVerb verb, pathsHandle p);
void	ScaleStdArc(GrafVerb verb, Rect* r, long startAngle, long arcAngle);
void	ScaleStdBits(PixelMap* src, Rect* srcRect, Rect* dstRect, long mode, RgnHandle mask);
void	ScaleStdLine(Point newPt);
void	ScaleStdOval(GrafVerb verb, Rect* r);
void	ScaleStdPoly(GrafVerb verb, PolyHandle poly);
void	ScaleStdRect(GrafVerb verb, Rect* r);
void	ScaleStdRgn(GrafVerb verb, RgnHandle rgn);
void	ScaleStdRRect(GrafVerb verb, Rect* r, long ovalWidth, long ovalHeight);


// ROM 0x001c7860 SetupScalingBottlenecks__FP8GrafPort
// The port's procs: the standard ones, with every drawing verb replaced.
// DEVIATION: the ROM's QDProcs are 0x38 bytes; the host's are pointers.
Boolean
SetupScalingBottlenecks(GrafPort* port)
{
	QDProcs* procs = (QDProcs*) NewPtr(sizeof(QDProcs));
	if (procs == nil)
		return false;
	SetStdProcs(procs);
	procs->textProc = ScaleStdText;
	procs->lineProc = ScaleStdLine;
	procs->rectProc = ScaleStdRect;
	procs->rRectProc = ScaleStdRRect;
	procs->ovalProc = ScaleStdOval;
	procs->arcProc = ScaleStdArc;
	procs->polyProc = ScaleStdPoly;
	procs->rgnProc = ScaleStdRgn;
	procs->bitsProc = ScaleStdBits;
	procs->curveProc = ScaleStdCurve;
	procs->pathsProc = ScaleStdPaths;
	port->grafProcs = procs;
	return true;
}


// ROM 0x001c791c TearDownScalingBottlenecks__FP8GrafPort
void
TearDownScalingBottlenecks(GrafPort* port)
{
	DisposPtr((Ptr) port->grafProcs);
	port->grafProcs = nil;
}


// ROM 0x001c7ef0 SetupPhantomPort__Fv
// The current port is a printer's port: its printer's phantom port made
// current, and its clip made again (the printer port's clip within its
// visible region, scaled up) if either has changed since the last time.
// ==> the printer.
TDotPrinter*
SetupPhantomPort(void)
{
	GrafPort* port;
	GetPort(&port);
	TDotPrinter* printer = (TDotPrinter*) ((PrintPort*) port)->prObject;
	SetPort(&printer->fPhantom.port);
	Boolean clipSame = EqualRgn(printer->GetPort()->clipRgn, printer->fClip);
	Boolean visSame = EqualRgn(printer->GetPort()->visRgn, printer->fVis);
	if (!clipSame)
		CopyRgn(printer->GetPort()->clipRgn, printer->fClip);
	if (!visSame)
		CopyRgn(printer->GetPort()->visRgn, printer->fVis);
	if (!clipSame || !visSame)
	{
		RgnHandle clip = printer->fPhantom.port.clipRgn;
		SectRgn(printer->GetPort()->visRgn, printer->fClip, clip);
		MapRgn(clip, &printer->GetScalerInfo()->fromRect, &printer->GetScalerInfo()->toRect);
	}
	return printer;
}


// ROM 0x001c7eb8 ReleasePhantomPort__FP11TDotPrinter
// The phantom port's patterns put back to black on white, and the
// printer's port made current again.
void
ReleasePhantomPort(TDotPrinter* printer)
{
	SetFgPattern(GetStdPattern(blackPat));
	SetBgPattern(GetStdPattern(whitePat));
	SetPort(printer->GetPort());
}


// ROM 0x001c7ff8 TransferVerbInfo__FP11TDotPrinterUcPPP8PixelMap
// What the verb draws with, from the printer port to the phantom port:
// the pen's size (scaled) and mode and the pattern for framing and
// painting, the pattern for filling, the background for erasing.  An
// inversion has no pattern (the callers never ask).
static void
TransferVerbInfo(TDotPrinter* printer, GrafVerb verb, PatternHandle* pattern)
{
	GrafPort* port = printer->GetPort();
	GrafPort* phantom = &printer->fPhantom.port;
	switch (verb)
	{
	case frame:
		phantom->pnSize = port->pnSize;
		ScalePt(&phantom->pnSize, &printer->GetScalerInfo()->fromRect, &printer->GetScalerInfo()->toRect);
		// (and on)
	case paint:
		phantom->pnMode = port->pnMode;
		// (and on)
	case fill:
		phantom->fgPat = port->fgPat;
		*pattern = phantom->fgPat;
		break;
	case erase:
		phantom->bgPat = port->bgPat;
		*pattern = phantom->bgPat;
		break;
	case invert:
	default:
		break;
	}
}


// ROM 0x001c809c UpdateScalePat__FP11TDotPrinterPP8PixelMap
// The pattern scaled up to fill the band (fPattern): each of its eight
// rows repeated as many times as the scale makes a 72-dpi row, each made
// once from the pattern's byte stretched across the band's width and
// copied after that; the pattern aligned to the page (the band's slice's
// corner) so that it meets itself at the bands' edges.  A pattern of more
// than one bit a pixel is dithered first, and a dithered one - already
// at the printer's scale - is not stretched downwards.  Nothing is done
// when the pattern and its alignment are the ones the band has already.
static void
UpdateScalePat(TDotPrinter* printer, PatternHandle pattern)
{
	Boolean dithered = false;
	Point align;
	align.v = (short) ((printer->fPageBand.top + printer->fPatOffset.v) & 7);
	align.h = (short) ((printer->fPageBand.left + printer->fPatOffset.h) & 7);
	long alignWord = ((long) (unsigned short) align.v << 16) | (unsigned short) align.h;
	PatternHandle copy = CopyPattern(pattern);
	if (((*copy)->pixMapFlags & 0xff) != 1)
		dithered = DitherPattern(copy);
	if (printer->fScalePatAlign != alignWord || !EqualPat(copy, printer->fScalePat))
	{
		printer->fScalePatAlign = alignWord;
		DisposePattern(printer->fScalePat);
		printer->fScalePat = CopyPattern(copy);
		UChar* dst = (UChar*) printer->fPattern.baseAddr;
		UChar* rows[8] = { nil, nil, nil, nil, nil, nil, nil, nil };
		const UChar* patBits = (const UChar*) GetPixelMapBits(*printer->fScalePat);
		UChar shifted[8];
		if (align.h != 0)
		{
			// ROM BUG: each row is turned left by the alignment with the
			// bits that fall off the left put back shifted right by 7 - h
			// rather than 8 - h, so one bit is doubled and one lost
			for (int i = 7; i >= 0; i--)
				shifted[i] = (UChar) ((patBits[i] << align.h) | (patBits[i] >> (7 - align.h)));
			patBits = shifted;
		}
		long height = printer->fBandRect.bottom - printer->fBandRect.top;
		long rowsPerPatternRow;
		if (dithered)
			rowsPerPatternRow = height;
		else
			rowsPerPatternRow = (short) ((FixedMultiply(FixedDivide(ToFixed(1), printer->GetScalerInfo()->scaleRatios.y), ToFixed(height)) + 0x8000) >> 16);
		long rowsLeft = height;
		long patRow = align.v;
		long accumulated = 0;
		long words = printer->fPattern.rowBytes >> 2;
		do
		{
			UChar* src = rows[patRow];
			accumulated += height;
			do
			{
				if (src != nil)
				{
					// the row made already: copied
					memmove(dst, src, words * 4);
					dst += words * 4;
				}
				else
				{
					// the row made: the pattern's byte, as a word, stretched
					// across the band a bit at a time by the inverse scale
					uint32_t step = FixedDivide(ToFixed(1), printer->GetScalerInfo()->scaleRatios.x) & 0xffff;
					src = dst;
					rows[patRow] = dst;
					uint32_t byte = patBits[patRow];
					uint32_t pat = byte | (byte << 8);
					pat |= pat << 16;
					// a shift register of the pattern's bits with a one
					// after them, so that its emptying is noticed
					uint32_t reload = 1 + (pat << 1);
					uint32_t reloadBit = pat >> 31;
					uint32_t shift = 0x80000000;
					uint32_t outBit = 0x80000000;
					uint32_t outWord = 0;
					uint32_t acc = step >> 1;
					long n = words;
					uint32_t bit = shift >> 31;
					shift <<= 1;
					if (bit != 0 && shift == 0)
					{
						shift = reload;
						bit = reloadBit;
					}
					for (;;)
					{
						if (bit != 0)
							outWord |= outBit;
						outBit >>= 1;
						if (outBit == 0)
						{
							outBit = 0x80000000;
							PutBigEndianWord(dst, (unsigned int) outWord);
							dst += 4;
							outWord = 0;
							if (--n == 0)
								break;
						}
						acc += step;
						if ((acc >> 16) == 0)
							continue;
						acc &= 0xffff;
						bit = shift >> 31;
						shift <<= 1;
						if (bit != 0 && shift == 0)
						{
							shift = reload;
							bit = reloadBit;
						}
					}
				}
				accumulated -= rowsPerPatternRow;
				rowsLeft--;
			} while (accumulated > 0 && rowsLeft != 0);
			patRow = (patRow + 1) & 7;
		} while (rowsLeft != 0);
	}
	DisposePattern(copy);
}


// ROM 0x001c83a4 DitherPattern__FPP8PixelMap
// A gray pattern made one bit a pixel: an even gray becomes the one of
// the ROM's sixteen dither patterns its average comes to, anything else
// has each pixel made black or white (ConvertPattern).  ==> whether it was
// an even gray.
static Boolean
DitherPattern(PatternHandle pattern)
{
	long depth = (*pattern)->pixMapFlags & 0xff;
	UChar* bits = (UChar*) GetPixelMapBits(*pattern);
	ULong gray;
	Boolean mono = MonochromePat(pattern, &gray);
	if (!mono)
		ConvertPattern((uint32_t*) bits, bits, depth);
	else
	{
		long level;
		if (depth == 4)
		{
			// eight words of eight 4-bit pixels summed: 0..960, a sixty-fourth
			long sum = 0;
			const UChar* p = bits;
			for (int w = 8; w != 0; w--, p += 4)
			{
				uint32_t word = GetBigEndianWord(p);
				uint32_t mask = 0xf0000000;
				for (int s = 28; mask != 0; mask >>= 4, s -= 4)
					sum += (word & mask) >> s;
			}
			level = sum >> 6;
		}
		else
		{
			// four words of sixteen 2-bit pixels: 0..192, and so 0..3
			long sum = 0;
			const UChar* p = bits;
			for (int w = 4; w != 0; w--, p += 4)
			{
				uint32_t word = GetBigEndianWord(p);
				uint32_t mask = 0xc0000000;
				for (int s = 30; mask != 0; mask >>= 2, s -= 2)
					sum += (word & mask) >> s;
			}
			switch (sum >> 6)
			{
			case 0:  level = 0; break;
			case 1:  level = 3; break;
			case 2:  level = 8; break;
			default: level = 15; break;
			}
		}
		BlockMove(kDitherPatterns + level * 8, bits, 8);
	}
	(*pattern)->rowBytes = 1;
	(*pattern)->pixMapFlags = ((*pattern)->pixMapFlags & ~0xff) | 1;
	return mono;
}


// ROM 0x001c84d8 ConvertPattern__FPUlPUcl
// Each pixel of a 4-bit (8 words) or 2-bit (4 words, two rows each)
// pattern made black if it is darker than half, into the bytes of a
// one-bit pattern.  The two are the same buffer: ROM QUIRK: each pixel's
// word is read again after the byte before it has been written, so the
// first pixels of a word are read from the bits already converted.
static void
ConvertPattern(uint32_t* src, UChar* dst, long depth)
{
	UChar* s = (UChar*) src;
	if (depth == 4)
	{
		for (int w = 8; w != 0; w--, s += 4, dst++)
		{
			uint32_t mask = 0xf0000000;
			int shift = 28;
			UChar bit = 0x80;
			do
			{
				uint32_t pixel = (GetBigEndianWord(s) & mask) >> shift;
				if (pixel <= 7)
					*dst = (UChar) (*dst & ~bit);
				else
					*dst = (UChar) (*dst | bit);
				mask >>= 4;
				shift -= 4;
				bit = (UChar) (bit >> 1);
			} while (mask != 0);
		}
	}
	else
	{
		for (int w = 4; w != 0; w--, s += 4, dst++)
		{
			uint32_t mask = 0xc0000000;
			int shift = 30;
			UChar bit = 0x80;
			do
			{
				uint32_t pixel = (GetBigEndianWord(s) & mask) >> shift;
				if (pixel <= 1)
					*dst = (UChar) (*dst & ~bit);
				else
					*dst = (UChar) (*dst | bit);
				mask >>= 2;
				shift -= 2;
				bit = (UChar) (bit >> 1);
			} while (bit != 0);
			dst++;
			bit = 0x80;
			do
			{
				uint32_t pixel = (GetBigEndianWord(s) & mask) >> shift;
				if (pixel <= 1)
					*dst = (UChar) (*dst & ~bit);
				else
					*dst = (UChar) (*dst | bit);
				mask >>= 2;
				shift -= 2;
				bit = (UChar) (bit >> 1);
			} while (bit != 0);
		}
	}
}


// ROM 0x001c85dc TransferShape__FP11TDotPrinter
// The mask's shape put into the band in the scaled pattern, in the pen's
// mode.  ROM BUG: the four "not" modes' loops never move on in the mask,
// so everything they draw is masked by the band's first 32 dots.
static void
TransferShape(TDotPrinter* printer)
{
	long n = printer->fBandSize >> 2;
	const uint32_t* mask = (const uint32_t*) printer->fMask.baseAddr;
	const uint32_t* pat = (const uint32_t*) printer->fPattern.baseAddr;
	uint32_t* dst = (uint32_t*) printer->fPhantom.port.portBits.baseAddr;
	switch (printer->fPhantom.port.pnMode & 7)
	{
	case 0:		// copy
		for ( ; n > 0; n--, dst++)
		{
			uint32_t m = *mask++;
			*dst = (*dst & ~m) | (*pat++ & m);
		}
		break;
	case 1:		// or
		for ( ; n > 0; n--, dst++)
			*dst = (*pat++ & *mask++) | *dst;
		break;
	case 2:		// xor
		for ( ; n > 0; n--, dst++)
			*dst = (*pat++ & *mask++) ^ *dst;
		break;
	case 3:		// bic
		for ( ; n > 0; n--, dst++)
			*dst = ~(*pat++ & *mask++) & *dst;
		break;
	case 4:		// notCopy
		for ( ; n > 0; n--, dst++)
		{
			uint32_t m = *mask++;
			*dst = (*dst & ~m) | (m & ~*pat++);
		}
		break;
	case 5:		// notOr
		for ( ; n > 0; n--, dst++)
			*dst = (~*pat++ & *mask) | *dst;
		break;
	case 6:		// notXor
		for ( ; n > 0; n--, dst++)
			*dst = (*mask & ~*pat++) ^ *dst;
		break;
	case 7:		// notBic
		for ( ; n > 0; n--, dst++)
			*dst = ~(~*pat++ & *mask) & *dst;
		break;
	}
}


// ROM 0x001c8958 WhiteOrBlackPat__FPP8PixelMap
// Whether a pattern is all white or all black, so a shape can be drawn in
// it directly.  ROM QUIRK: the last byte is not looked at.
static Boolean
WhiteOrBlackPat(PatternHandle pattern)
{
	PixelMap* pm = *pattern;
	const UChar* p = (const UChar*) GetPixelMapBits(pm);
	UChar first = *p++;
	if (first != 0 && first != 0xff)
		return false;
	long n = pm->rowBytes * (pm->bounds.bottom - pm->bounds.top) - 2;
	do
	{
		if (*p++ != first)
			return false;
	} while (--n > 0);
	return true;
}


// ROM 0x001c89c8 RotateBits__FP8PixelMapT1
// A band turned a quarter for a landscape page: each row of the source
// becomes a column of the destination, from its right-hand edge leftwards.
void
RotateBits(PixelMap* src, PixelMap* dst)
{
	long columns = dst->bounds.right - dst->bounds.left;
	long dstRowBytes = dst->rowBytes;
	ZeroBytes(dst->baseAddr, dstRowBytes * (dst->bounds.bottom - dst->bounds.top));
	UChar* dstColumn = (UChar*) dst->baseAddr + dstRowBytes - 1;
	UChar dstBit = 1;
	long srcWidth = src->bounds.right - src->bounds.left;
	long srcRowBytes = src->rowBytes;
	const UChar* srcRow = (const UChar*) GetPixelMapBits(src);
	for ( ; columns != 0; columns--)
	{
		UChar* d = dstColumn;
		const UChar* s = srcRow;
		UChar srcBit = 0x80;
		for (long n = srcWidth; n != 0; n--)
		{
			if (*s & srcBit)
				*d |= dstBit;
			srcBit = (UChar) (srcBit >> 1);
			if (srcBit == 0)
			{
				s++;
				srcBit = 0x80;
			}
			d += dstRowBytes;
		}
		dstBit = (UChar) (dstBit << 1);
		if (dstBit == 0)
		{
			dstColumn--;
			dstBit = 1;
		}
		srcRow += srcRowBytes;
	}
}


/*------------------------------------------------------------------------------
	The procs
------------------------------------------------------------------------------*/

// whether a shape is to be drawn into the port's recording as it is: a
// picture being recorded (the pen hidden by the recording), a region
// (only what is framed), a polygon
static inline Boolean
RecordingPicture(GrafPort* port, long pnVis)
{
	return port->picSave != nil && pnVis == -1;
}

// a shape drawn in a pattern into the band: through the mask, and the
// pattern combined with it after
#define DRAW_PATTERNED(printer, pattern, draw) \
	do { \
		UpdateScalePat(printer, pattern); \
		ZeroBytes(printer->fMask.baseAddr, printer->fBandSize); \
		SetPortBits(&printer->fMask); \
		SetFgPattern(GetStdPattern(blackPat)); \
		draw; \
		SetPortBits(&printer->fBands[printer->fCurBand]); \
		TransferShape(printer); \
	} while (0)


// ROM 0x001c7940 ScaleStdText__FlN21
// Text drawn at the printer's scale: the scales multiplied by the ratio,
// the options' width to fit and the location scaled with them, and both
// put back after.
void
ScaleStdText(TextObjectRef text, Fixed hScale, Fixed vScale)
{
	GrafPort* port;
	GetPort(&port);
	short pnVis = port->pnVis;
	if (RecordingPicture(port, pnVis))
	{
		port->pnVis = -1;
		StdText(text, hScale, vScale);
		port->pnVis = pnVis;
	}
	if (pnVis < 0)
		return;
	TDotPrinter* printer = SetupPhantomPort();
	ScalerInfo* scaler = printer->GetScalerInfo();
	Fixed width = 0;
	hScale = FixedMultiply(hScale, scaler->scaleRatios.x);
	vScale = FixedMultiply(vScale, scaler->scaleRatios.y);
	TextOptions* options;
	GetTextObjField(text, kTextObjOptions, &options);
	if (options != nil)
	{
		width = options->fWidth;
		options->fWidth = FixedMultiply(scaler->scaleRatios.x, width);
		SetTextObjField(text, kTextObjOptions, options);
	}
	FPoint location;
	GetTextObjField(text, kTextObjLocation, &location);
	FPoint scaled = location;
	MapFPoint(&scaled, &scaler->fromRect, &scaler->toRect);
	SetTextObjField(text, kTextObjLocation, &scaled);
	SetTextObjField(text, (TextObjectField) 8, nil);
	StdText(text, hScale, vScale);
	if (options != nil)
	{
		options->fWidth = width;
		SetTextObjField(text, kTextObjOptions, options);
	}
	SetTextObjField(text, kTextObjLocation, &location);
	ReleasePhantomPort(printer);
}


// ROM 0x001c7adc ScaleStdCurve__FUcP5curve
void
ScaleStdCurve(GrafVerb verb, curve* c)
{
	GrafPort* port;
	GetPort(&port);
	short pnVis = port->pnVis;
	if (RecordingPicture(port, pnVis)
	 || (port->rgnSave != nil && verb == frame)
	 || port->polySave != nil)
	{
		port->pnVis = -1;
		StdCurve(verb, c);
		port->pnVis = pnVis;
	}
	if (pnVis < 0)
		return;
	TDotPrinter* printer = SetupPhantomPort();
	curve scaled = *c;
	MapCurve(&scaled, &printer->GetScalerInfo()->fromRect, &printer->GetScalerInfo()->toRect);
	PatternHandle pattern;
	TransferVerbInfo(printer, verb, &pattern);
	Rect bounds, sect;
	GetCurveBounds(&scaled, &bounds);
	if (verb == frame)
	{
		bounds.bottom += printer->fPhantom.port.pnSize.v;
		bounds.right += printer->fPhantom.port.pnSize.h;
	}
	if (SectRect(&bounds, &printer->fPhantom.port.portRect, &sect))
	{
		if (verb == invert || WhiteOrBlackPat(pattern))
			StdCurve(verb, &scaled);
		else
			DRAW_PATTERNED(printer, pattern, StdCurve(verb, &scaled));
	}
	ReleasePhantomPort(printer);
}


// ROM 0x001c7cbc ScaleStdPaths__FUcPP5paths
void
ScaleStdPaths(GrafVerb verb, pathsHandle p)
{
	GrafPort* port;
	GetPort(&port);
	short pnVis = port->pnVis;
	if (RecordingPicture(port, pnVis)
	 || (port->rgnSave != nil && verb == frame)
	 || port->polySave != nil)
	{
		port->pnVis = -1;
		StdPaths(verb, p);
		port->pnVis = pnVis;
	}
	if (pnVis < 0)
		return;
	TDotPrinter* printer = SetupPhantomPort();
	pathsHandle scaled = (pathsHandle) NewHandle(SizeOfPaths(p));
	if (scaled != nil)
	{
		CopyPaths(p, scaled);
		MapPaths(scaled, &printer->GetScalerInfo()->fromRect, &printer->GetScalerInfo()->toRect);
		PatternHandle pattern;
		TransferVerbInfo(printer, verb, &pattern);
		Rect bounds, sect;
		GetPathsBounds(scaled, &bounds);
		if (verb == frame)
		{
			bounds.bottom += printer->fPhantom.port.pnSize.v;
			bounds.right += printer->fPhantom.port.pnSize.h;
		}
		if (SectRect(&bounds, &printer->fPhantom.port.portRect, &sect))
		{
			if (verb == invert || WhiteOrBlackPat(pattern))
				StdPaths(verb, scaled);
			else
				DRAW_PATTERNED(printer, pattern, StdPaths(verb, scaled));
		}
		DisposePaths(scaled);
	}
	ReleasePhantomPort(printer);
}


// ROM 0x001c87cc ScaleStdArc__FUcP4RectlT3
void
ScaleStdArc(GrafVerb verb, Rect* r, long startAngle, long arcAngle)
{
	GrafPort* port;
	GetPort(&port);
	short pnVis = port->pnVis;
	if (RecordingPicture(port, pnVis))
	{
		port->pnVis = -1;
		StdArc(verb, r, startAngle, arcAngle);
		port->pnVis = pnVis;
	}
	if (pnVis < 0)
		return;
	TDotPrinter* printer = SetupPhantomPort();
	Rect scaled = *r;
	Rect sect;
	MapRect(&scaled, &printer->GetScalerInfo()->fromRect, &printer->GetScalerInfo()->toRect);
	if (SectRect(&scaled, &printer->fPhantom.port.portRect, &sect))
	{
		PatternHandle pattern;
		TransferVerbInfo(printer, verb, &pattern);
		if (verb == invert || WhiteOrBlackPat(pattern))
			StdArc(verb, &scaled, startAngle, arcAngle);
		else
			DRAW_PATTERNED(printer, pattern, StdArc(verb, &scaled, startAngle, arcAngle));
	}
	ReleasePhantomPort(printer);
}


// ROM 0x001c8ab4 ScaleStdBits__FP8PixelMapP4RectT2lPP6Region
// Bits copied into the band: only the part of the destination the band
// covers (the band's rectangle mapped back to the page), with the source
// cut down to match, and the mask scaled up.  Bits are drawn as they are,
// in no pattern.
void
ScaleStdBits(PixelMap* src, Rect* srcRect, Rect* dstRect, long mode, RgnHandle mask)
{
	GrafPort* port;
	GetPort(&port);
	short pnVis = port->pnVis;
	if (RecordingPicture(port, pnVis))
	{
		port->pnVis = -1;
		StdBits(src, srcRect, dstRect, mode, mask);
		port->pnVis = pnVis;
	}
	if (pnVis < 0)
		return;
	TDotPrinter* printer = SetupPhantomPort();
	Rect band = printer->fPhantom.port.portRect;
	Rect band72;
	SetRect(&band72, band.left, band.top, band.right, band.bottom);
	MapRect(&band72, &printer->GetScalerInfo()->toRect, &printer->GetScalerInfo()->fromRect);
	Rect sect;
	if (SectRect(&band72, dstRect, &sect))
	{
		Rect dst;
		SetRect(&dst, sect.left, sect.top, sect.right, sect.bottom);
		MapRect(&dst, &printer->GetScalerInfo()->fromRect, &printer->GetScalerInfo()->toRect);
		MapRect(&sect, dstRect, srcRect);
		RgnHandle scaledMask = nil;
		if (!IsWideOpenRgn(mask))
		{
			scaledMask = NewRgn();
			CopyRgn(mask, scaledMask);
			MapRgn(scaledMask, &printer->GetScalerInfo()->fromRect, &printer->GetScalerInfo()->toRect);
		}
		StdBits(src, &sect, &dst, mode, scaledMask);
		if (scaledMask != nil)
			DisposeRgn(scaledMask);
	}
	ReleasePhantomPort(printer);
}


// ROM 0x001c8cb4 ScaleStdLine__F5Point
// A line from the pen to the point: the pen moved on the page (recorded,
// if anything is being recorded), and the line drawn between the two ends
// scaled, the pen's size scaled with them.
void
ScaleStdLine(Point newPt)
{
	GrafPort* port;
	GetPort(&port);
	short pnVis = port->pnVis;
	Point pen;
	GetPen(&pen);
	Point to;
	memcpy(&to, &newPt, sizeof(Point));
	if ((port->picSave != nil && port->pnVis == -1)
	 || port->rgnSave != nil || port->polySave != nil)
	{
		port->pnVis = -1;
		StdLine(newPt);
		port->pnVis = pnVis;
	}
	else
		MoveTo(newPt.h, newPt.v);
	if (pnVis < 0)
		return;
	TDotPrinter* printer = SetupPhantomPort();
	// the two ends as the corners of a rectangle, mapped together
	Rect ends;
	ends.top = pen.v;
	ends.left = pen.h;
	ends.bottom = to.v;
	ends.right = to.h;
	MapRect(&ends, &printer->GetScalerInfo()->fromRect, &printer->GetScalerInfo()->toRect);
	PatternHandle pattern;
	TransferVerbInfo(printer, frame, &pattern);
	Rect bounds = ends;
	if (bounds.left > bounds.right)
	{
		short t = bounds.left;
		bounds.left = bounds.right;
		bounds.right = t;
	}
	if (bounds.bottom < bounds.top)
	{
		short t = bounds.top;
		bounds.top = bounds.bottom;
		bounds.bottom = t;
	}
	bounds.bottom += printer->fPhantom.port.pnSize.v;
	bounds.right += printer->fPhantom.port.pnSize.h;
	Rect sect;
	if (SectRect(&bounds, &printer->fPhantom.port.portRect, &sect))
	{
		MoveTo(ends.left, ends.top);
		Point scaledTo;
		scaledTo.v = ends.bottom;
		scaledTo.h = ends.right;
		if (WhiteOrBlackPat(pattern))
			StdLine(scaledTo);
		else
			DRAW_PATTERNED(printer, pattern, StdLine(scaledTo));
	}
	ReleasePhantomPort(printer);
}


// ROM 0x001c8f14 ScaleStdOval__FUcP4Rect
void
ScaleStdOval(GrafVerb verb, Rect* r)
{
	GrafPort* port;
	GetPort(&port);
	short pnVis = port->pnVis;
	if (RecordingPicture(port, pnVis) || (port->rgnSave != nil && verb == frame))
	{
		port->pnVis = -1;
		StdOval(verb, r);
		port->pnVis = pnVis;
	}
	if (pnVis < 0)
		return;
	TDotPrinter* printer = SetupPhantomPort();
	Rect scaled = *r;
	Rect sect;
	MapRect(&scaled, &printer->GetScalerInfo()->fromRect, &printer->GetScalerInfo()->toRect);
	if (SectRect(&scaled, &printer->fPhantom.port.portRect, &sect))
	{
		PatternHandle pattern;
		TransferVerbInfo(printer, verb, &pattern);
		if (verb == invert || WhiteOrBlackPat(pattern))
			StdOval(verb, &scaled);
		else
			DRAW_PATTERNED(printer, pattern, StdOval(verb, &scaled));
	}
	ReleasePhantomPort(printer);
}


// ROM 0x001c9098 ScaleStdPoly__FUcPP7Polygon
// (a polygon that cannot be copied is drawn nowhere, and the phantom port
// is not even set up)
void
ScaleStdPoly(GrafVerb verb, PolyHandle poly)
{
	GrafPort* port;
	GetPort(&port);
	short pnVis = port->pnVis;
	if (RecordingPicture(port, pnVis)
	 || (port->rgnSave != nil && verb == frame)
	 || port->polySave != nil)
	{
		port->pnVis = -1;
		StdPoly(verb, poly);
		port->pnVis = pnVis;
	}
	if (pnVis < 0)
		return;
	long size = (*poly)->polySize;
	PolyHandle scaled = (PolyHandle) NewHandle(size);
	if (scaled == nil)
		return;
	TDotPrinter* printer = SetupPhantomPort();
	BlockMove(*poly, *scaled, size);
	MapPoly(scaled, &printer->GetScalerInfo()->fromRect, &printer->GetScalerInfo()->toRect);
	PatternHandle pattern;
	TransferVerbInfo(printer, verb, &pattern);
	Rect bounds = (*scaled)->polyBBox;
	if (verb == frame)
	{
		bounds.bottom += printer->fPhantom.port.pnSize.v;
		bounds.right += printer->fPhantom.port.pnSize.h;
	}
	Rect sect;
	if (SectRect(&bounds, &printer->fPhantom.port.portRect, &sect))
	{
		if (verb == invert || WhiteOrBlackPat(pattern))
			StdPoly(verb, scaled);
		else
			DRAW_PATTERNED(printer, pattern, StdPoly(verb, scaled));
	}
	ReleasePhantomPort(printer);
	KillPoly(scaled);
}


// ROM 0x001c92a8 ScaleStdRect__FUcP4Rect
void
ScaleStdRect(GrafVerb verb, Rect* r)
{
	GrafPort* port;
	GetPort(&port);
	short pnVis = port->pnVis;
	if (RecordingPicture(port, pnVis) || (port->rgnSave != nil && verb == frame))
	{
		port->pnVis = -1;
		StdRect(verb, r);
		port->pnVis = pnVis;
	}
	if (pnVis < 0)
		return;
	TDotPrinter* printer = SetupPhantomPort();
	Rect scaled = *r;
	Rect sect;
	MapRect(&scaled, &printer->GetScalerInfo()->fromRect, &printer->GetScalerInfo()->toRect);
	if (SectRect(&scaled, &printer->fPhantom.port.portRect, &sect))
	{
		PatternHandle pattern;
		TransferVerbInfo(printer, verb, &pattern);
		if (verb == invert || WhiteOrBlackPat(pattern))
			StdRect(verb, &scaled);
		else
			DRAW_PATTERNED(printer, pattern, StdRect(verb, &scaled));
	}
	ReleasePhantomPort(printer);
}


// ROM 0x001c942c ScaleStdRgn__FUcPP6Region
// (a region that cannot be copied is drawn nowhere)
void
ScaleStdRgn(GrafVerb verb, RgnHandle rgn)
{
	GrafPort* port;
	GetPort(&port);
	short pnVis = port->pnVis;
	if (RecordingPicture(port, pnVis) || (port->rgnSave != nil && verb == frame))
	{
		port->pnVis = -1;
		StdRgn(verb, rgn);
		port->pnVis = pnVis;
	}
	if (pnVis < 0)
		return;
	TDotPrinter* printer = SetupPhantomPort();
	RgnHandle scaled = NewRgn();
	if (scaled != nil)
	{
		CopyRgn(rgn, scaled);
		MapRgn(scaled, &printer->GetScalerInfo()->fromRect, &printer->GetScalerInfo()->toRect);
		Rect sect;
		if (SectRect(&(*scaled)->rgnBBox, &printer->fPhantom.port.portRect, &sect))
		{
			PatternHandle pattern;
			TransferVerbInfo(printer, verb, &pattern);
			if (verb == invert || WhiteOrBlackPat(pattern))
				StdRgn(verb, scaled);
			else
				DRAW_PATTERNED(printer, pattern, StdRgn(verb, scaled));
		}
		DisposeRgn(scaled);
	}
	ReleasePhantomPort(printer);
}


// ROM 0x001c95c8 ScaleStdRRect__FUcP4RectlT3
// ROM QUIRK: the corners' oval is scaled as a Point with its width in the
// vertical and its height in the horizontal, so on a printer whose two
// resolutions differ (a fax: 204 x 98 or 196 dpi) the corners come out
// scaled the wrong way round.
void
ScaleStdRRect(GrafVerb verb, Rect* r, long ovalWidth, long ovalHeight)
{
	GrafPort* port;
	GetPort(&port);
	short pnVis = port->pnVis;
	if (RecordingPicture(port, pnVis) || (port->rgnSave != nil && verb == frame))
	{
		port->pnVis = -1;
		StdRRect(verb, r, ovalWidth, ovalHeight);
		port->pnVis = pnVis;
	}
	if (pnVis < 0)
		return;
	TDotPrinter* printer = SetupPhantomPort();
	Rect scaled = *r;
	Rect sect;
	MapRect(&scaled, &printer->GetScalerInfo()->fromRect, &printer->GetScalerInfo()->toRect);
	if (SectRect(&scaled, &printer->fPhantom.port.portRect, &sect))
	{
		Point oval;
		oval.h = (short) ovalHeight;
		oval.v = (short) ovalWidth;
		ScalePt(&oval, &printer->GetScalerInfo()->fromRect, &printer->GetScalerInfo()->toRect);
		PatternHandle pattern;
		TransferVerbInfo(printer, verb, &pattern);
		if (verb == invert || WhiteOrBlackPat(pattern))
			StdRRect(verb, &scaled, oval.v, oval.h);
		else
			DRAW_PATTERNED(printer, pattern, StdRRect(verb, &scaled, oval.v, oval.h));
	}
	ReleasePhantomPort(printer);
}
