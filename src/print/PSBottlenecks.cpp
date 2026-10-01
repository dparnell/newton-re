/*
	File:		print/PSBottlenecks.cpp

	Contains:	The PostScript printer's drawing: the printer port's procs
				(PrStdRect and the rest) and the TPSPrinter methods that turn
				each shape into PostScript (print/PSPrinter.h).

				Every proc starts the same way.  When the port is recording
				a picture with its pen hidden, or a region (or, for a
				polygon, a polygon), the standard proc is called with the
				pen hidden so that the recording gets the shape; then, if
				the job is still going and the pen is visible, the clip path
				is brought up to date (SetClip: "PreC" the box of the clip
				and visible regions' intersection "PoC"), the gray is set
				from the verb's pattern (SetGrayLevel) and the shape is sent:
				filled as a path and "fill" - or "PatternFill" when the
				pattern is not a plain gray, made a PostScript pattern by
				SetCurrentPattern - or framed: inset by half the pen, a path
				stroked by "SclPen", the prolog's procedure that scales a
				unit pen to the port's pen size.  A line is a rectangle
				stroked along its length when it is straight, and a filled
				parallelogram traced around the pen when it is not
				(Draw1QDLine).  Inversion and regions are not supported (a
				comment says so in the PostScript).

				Text goes a style run at a time: the run's font selected as
				its PostScript family's face ("/Helvetica-Bold-Mac 12 SF"),
				the characters escaped into a string and shown with "show"
				- or "awidthshow" when justified - and the characters the
				Mac fonts have but the PostScript ones lack (the maths
				signs) sent in the Symbol font.  Ink words are drawn by their
				own glyphs, which come back here as paths.  A bitmap is sent
				as hex rows for the prolog's `bimage`, its depth through the
				pixel map's gray table.

	Reconstructed from the MP2x00 US ROM (0x00155f28-0x0015a0ac); each
	function cites its origin.  The ROM receives a Point in one register,
	v in its top half; its unaligned loads pick the halves out.
*/

#include "print/PSPrinter.h"
#include "Ports.h"
#include "Rects.h"
#include "Regions.h"
#include "Draw.h"
#include "Shapes.h"
#include "Polygons.h"
#include "Curves.h"
#include "Paths.h"
#include "TextObject.h"
#include "Fonts.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"
#include "FixedMath.h"
#include "utility/Unicode.h"
#include "ink/Ink.h"
#include "ink/InkFont.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

extern RefStruct*	gTransportProgress;			// views/PrintView.cpp: the transport a fax's progress is shown in
void		SetFaxPrintProgress(RefArg transport, long page, long percent);	// views/PrintView.cpp


// a Fixed rounded to the nearest whole number, as a short
static inline long
RoundFixed16(Fixed f)
{
	return (short) ((uint32_t) (f + 0x8000) >> 16);
}


// ROM 0x00155f28 (unnamed) - the current port and its printer
static TPSPrinter*
CurrentPSPrinter(GrafPort** portOut)
{
	GrafPort* port;
	GetPort(&port);
	TPSPrinter* printer = (TPSPrinter*) ((PrintPort*) port)->prObject;
	if (portOut != nil)
		*portOut = port;
	return printer;
}


// ROM 0x00155f58 SwapPoint__FR6FPointT1
static void
SwapPoint(FPoint& a, FPoint& b)
{
	FPoint t = a;
	a = b;
	b = t;
}


/*------------------------------------------------------------------------------
	S e t t i n g   u p
------------------------------------------------------------------------------*/

// ROM 0x00156f34 SetupPSBottlenecks__10TPSPrinterFP8GrafPort
// DEVIATION: the ROM's QDProcs are 0x38 bytes; the host's are pointers.
Boolean
TPSPrinter::SetupPSBottlenecks(GrafPort* port)
{
	QDProcs* procs = (QDProcs*) NewPtr(sizeof(QDProcs));
	if (procs == nil)
		return false;
	SetStdProcs(procs);
	procs->textProc = PrStdText;
	procs->lineProc = PrStdLine;
	procs->rectProc = PrStdRect;
	procs->rRectProc = PrStdRRect;
	procs->ovalProc = PrStdOval;
	procs->arcProc = PrStdArc;
	procs->polyProc = PrStdPoly;
	procs->rgnProc = PrStdRgn;
	procs->bitsProc = PrStdBits;
	procs->commentProc = (PicCommentProc) PrStdComment;
	procs->curveProc = PrStdCurve;
	procs->pathsProc = PrStdPaths;
	port->grafProcs = procs;
	return true;
}


// ROM 0x0015706c TearDownPSBottlenecks__10TPSPrinterFP8GrafPort
void
TPSPrinter::TearDownPSBottlenecks(GrafPort* port)
{
	DisposPtr((Ptr) port->grafProcs);
	port->grafProcs = nil;
}


/*------------------------------------------------------------------------------
	N u m b e r s ,   t h e   p e n ,   t h e   c l i p   a n d   g r a y
------------------------------------------------------------------------------*/

// ROM 0x001568c0 FixedToString__10TPSPrinterFlPc
// A Fixed as PostScript: a whole number as it is, anything else rounded
// to two decimals (the fraction worked out a bit at a time in nine digits).
// ROM BUG, kept: the answer is the text after the minus sign, so a
// negative number printed through the answer prints positive; the callers
// that print the buffer itself print it with its sign.
char*
TPSPrinter::FixedToString(Fixed value, char* string)
{
	uint32_t v = (uint32_t) value;
	if ((int32_t) v < 0)
	{
		v = (uint32_t) -(int32_t) v;
		*string++ = '-';
	}
	if ((uint32_t) (v << 16) == 0)
		sprintf(string, "%d", (int) ((int32_t) v >> 16));
	else
	{
		char digits[12];
		uint32_t fraction = 0;
		uint32_t weight = 500000000;
		v += 0x148;
		for (uint32_t bit = 0x8000; bit != 0; bit >>= 1)
		{
			if (v & bit)
				fraction += weight;
			weight >>= 1;
		}
		sprintf(digits, "%9.9lu", (unsigned long) fraction);
		sprintf(string, "%d.%.2s", (int) ((int32_t) v >> 16), digits);
	}
	return string;
}


// ROM 0x00156970 OffsetFixedPoint__10TPSPrinterFR6FPointlT2
void
TPSPrinter::OffsetFixedPoint(FPoint& pt, Fixed dh, Fixed dv)
{
	pt.x += dh;
	pt.y += dv;
}


// ROM 0x00156990 PositionPen__10TPSPrinterFlT15Point
// The current point moved to (h, v), the middle of the pen there.
void
TPSPrinter::PositionPen(long h, long v, Point pen)
{
	char y[16], x[16];
	char* ys = FixedToString(FixedDivide(ToFixed(pen.v), 0x20000) + ToFixed(v), y);
	char* xs = FixedToString(FixedDivide(ToFixed(pen.h), 0x20000) + ToFixed(h), x);
	sprintf(fBuffer, "%s %s MvTo ", xs, ys);
	SendPSText(fBuffer, false);
}


// ROM 0x00156a28 ResetLineWidth__10TPSPrinterFl
// A line's path finished: stroked at the width SetLineWidth set (and the
// width put back), or filled when it had none - a slanted line is the
// outline of its pen's trace.
void
TPSPrinter::ResetLineWidth(long width)
{
	SendPSText((char*) (width == 0 ? "\rfill\r" : "\rstroke SLW\r"), false);
}


// ROM 0x00156a54 SetupPen__10TPSPrinterF5Point
// The pen SclPen scales to, sent when it changes - and every time when it
// is nought by nought.
void
TPSPrinter::SetupPen(Point pen)
{
	if (pen.h == fPenSize.h && fPenSize.v == pen.v
	 && !(pen.h == 0 && pen.v == 0))
		return;
	fPenSize = pen;
	sprintf(fBuffer, "%d %d Pen\r", pen.h, pen.v);
	SendPSText(fBuffer, false);
}


// ROM 0x00156aec SetClip__10TPSPrinterFP8GrafPort
// The clip path made again when the port's clip or visible region has
// changed: the intersection of their boxes.  (PreC's grestore puts the
// gray back to black.)
void
TPSPrinter::SetClip(GrafPort* port)
{
	Rect* clip = &(*port->clipRgn)->rgnBBox;
	Rect* vis = &(*port->visRgn)->rgnBBox;
	if (EqualRect(clip, &fClipBox) && EqualRect(vis, &fVisBox))
		return;
	fClipBox = *clip;
	fVisBox = *vis;
	Rect box;
	SectRect(clip, vis, &box);
	SendPSText((char*) "PreC ", false);
	Point pen;
	pen.v = 1;
	pen.h = 1;
	SendRectangle(&box, pen);
	SendPSText((char*) "PoC\r", false);
	fGray = 0;
}


// ROM 0x00157f68 DoSetGray__10TPSPrinterFUc
// The gray, 0 (black) .. 64 (white), sent when it changes.
void
TPSPrinter::DoSetGray(UChar gray)
{
	if (fGray == gray)
		return;
	sprintf(fBuffer, "%d 64 div setgray\r", (int) gray);
	fGray = gray;
	SendPSText(fBuffer, false);
}


// ROM 0x00156778 CountBitsInPattern__10TPSPrinterFPP8PixelMap
// How dark a pattern is, 0 .. 64: the black pixels of its eight rows (the
// first eight bytes of a one-bit pattern), or of a two- or four-bit one
// its pixels' values added up and scaled.
// ROM BUG, kept: the two- and four-bit counts shift both the row and the
// mask, so each step adds the row's first pixel again - as many times as
// the row has steps left - and the others are never counted.
long
TPSPrinter::CountBitsInPattern(PatternHandle pattern)
{
	PixelMap* pm = *pattern;
	long count = 0;
	switch (pm->pixMapFlags & 0xff)
	{
	case 1:
		{
			const UChar* bits = (const UChar*) GetPixelMapBits(pm);
			for (int row = 8; row != 0; row--)
			{
				for (ULong b = *bits++; b != 0; b = (b >> 1) & 0xff)
					count += (b & 1);
			}
		}
		break;
	case 2:
		{
			const UChar* bits = (const UChar*) GetPixelMapBits(pm);
			for (int row = 8; row != 0; row--)
			{
				uint32_t half = (bits[0] << 8) | bits[1];
				bits += 2;
				uint32_t mask = 0xc000;
				uint32_t shift = 14;
				while (half != 0)
				{
					count += (long) ((half & mask) >> shift);
					mask = (mask >> 2) & 0xffff;
					shift = (shift - 2) & 0xffff;
					half = (half >> 2) & 0xffff;
				}
			}
			count = (count + 2) / 3;
		}
		break;
	case 4:
		{
			const UChar* bits = (const UChar*) GetPixelMapBits(pm);
			for (int row = 8; row != 0; row--)
			{
				uint32_t word = ((uint32_t) bits[0] << 24) | (bits[1] << 16) | (bits[2] << 8) | bits[3];
				bits += 4;
				uint32_t mask = 0xf0000000;
				uint32_t shift = 28;
				while (word != 0)
				{
					count += (long) ((word & mask) >> shift);
					mask >>= 4;
					shift -= 4;
					word >>= 4;
				}
			}
			count = (count + 14) / 15;
		}
		break;
	default:
		count = 64;
		break;
	}
	return (short) count;
}


// ROM 0x00156bc8 SetGrayLevel__10TPSPrinterFUcP8GrafPort
// The gray for a verb: the pen's pattern to frame, paint and fill (the
// ROM's printer has no fill pattern), the background's to erase.  A
// pattern of one gray is set as a gray; any other is made the current
// PostScript pattern - its pixels as hex, a one-bit pattern each pixel
// made four bits - and the shape is filled with PatternFill.
void
TPSPrinter::SetGrayLevel(GrafVerb verb, GrafPort* port)
{
	PatternHandle pattern = nil;
	switch (verb)
	{
	case 0:
	case 1:
	case 4:
		pattern = port->fgPat;
		break;
	case 2:
		pattern = port->bgPat;
		break;
	case 3:
		SendPSText((char*) "% --- Invert mode not supported\r", false);
		return;
	default:
		break;
	}
	if (pattern == nil)
		return;
	ULong gray;
	if (MonochromePat(pattern, &gray))
	{
		DoSetGray((UChar) (64 - CountBitsInPattern(pattern)));
		fDoPatternFill = false;
		return;
	}
	HLock((Handle) pattern);
	PixelMap* pm = *pattern;
	long depth = pm->pixMapFlags & 0xff;
	if (depth > 1)
	{
		sprintf(fBuffer, "false %d <", (int) depth);
		SendPSText(fBuffer, false);
		long words = ((*pattern)->rowBytes * 8) / 4;
		const UChar* bits = (const UChar*) GetPixelMapBits(pm);
		for ( ; words != 0; words--)
		{
			uint32_t word = ((uint32_t) bits[0] << 24) | (bits[1] << 16) | (bits[2] << 8) | bits[3];
			bits += 4;
			sprintf(fBuffer, "%08lx", (unsigned long) (uint32_t) ~word);
			SendPSText(fBuffer, false);
		}
	}
	else
	{
		sprintf(fBuffer, "false 4 <");
		SendPSText(fBuffer, false);
		long bytes = (*pattern)->rowBytes * 8;
		const UChar* bits = (const UChar*) GetPixelMapBits(pm);
		for ( ; bytes != 0; bytes--)
		{
			UChar b = (UChar) ~*bits++;
			sprintf(fBuffer, "%08lx", (unsigned long) gFourBitTable[b]);
			SendPSText(fBuffer, false);
		}
	}
	SendPSText((char*) "> SetCurrentPattern\r", false);
	HUnlock((Handle) pattern);
	fDoPatternFill = true;
}


// ROM 0x00157a60 GetDoPatternFill__10TPSPrinterFv
Boolean
TPSPrinter::GetDoPatternFill()
{
	return fDoPatternFill;
}


/*------------------------------------------------------------------------------
	L i n e s
------------------------------------------------------------------------------*/

// ROM 0x00156e04 SetLineWidth__10TPSPrinterF5PointN21
// An upright line stroked as wide as the pen, a level one as tall; a
// slanted one gets no width (its pen's trace is filled instead).
// ==> the width set.
long
TPSPrinter::SetLineWidth(Point from, Point to, Point pen)
{
	long width = 0;
	if (from.h == to.h)
		width = pen.h;
	else if (from.v == to.v)
		width = pen.v;
	if (width != 0)
	{
		sprintf(fBuffer, "CLW %d SLW ", (int) width);
		SendPSText(fBuffer, false);
	}
	return width;
}


// ROM 0x00156e90 SetupLineStrings__10TPSPrinterFRC6FPointT15PointPcN54
// The numbers a slanted line's outline is made of: its two ends, the far
// end moved by the pen, and the near end moved down by it.
void
TPSPrinter::SetupLineStrings(const FPoint& a, const FPoint& b, Point pen, char* ax, char* ay, char* by, char* bxh, char* byv, char* ayv)
{
	FixedToString(a.x, ax);
	FixedToString(a.y, ay);
	FixedToString(b.y, by);
	FixedToString(b.x + ToFixed(pen.h), bxh);
	FixedToString(b.y + ToFixed(pen.v), byv);
	FixedToString(a.y + ToFixed(pen.v), ayv);
}


// ROM 0x00156ffc DoPSLine__10TPSPrinterFRC6FPoint
void
TPSPrinter::DoPSLine(const FPoint& to)
{
	char y[16], x[16];
	char* ys = FixedToString(to.y, y);
	char* xs = FixedToString(to.x, x);
	sprintf(fBuffer, "%s %s LnTo ", xs, ys);
	SendPSText(fBuffer, false);
}


// ROM 0x00159b28 Draw1QDLine__10TPSPrinterFRC6FPoint5PointT1
// A QuickDraw line's path.  A level or upright one is a line down the
// middle of the pen's trace (stroked at the pen's width - SetLineWidth);
// a slanted one is the outline of the pen dragged from one end to the
// other, a six-sided figure (filled).
void
TPSPrinter::Draw1QDLine(const FPoint& fromPt, Point pen, const FPoint& toPt)
{
	FPoint from = fromPt;
	FPoint to = toPt;
	char s1[16], s2[16], s3[16], s4[16], s5[16], s6[16];
	if (to.y == from.y || to.x == from.x)
	{
		if (to.y < from.y || to.x < from.x)
			SwapPoint(from, to);
		if (to.y == from.y)
		{
			// level: along the middle of the pen's height, to the far end
			// of its width
			char* ty = FixedToString(FixedDivide(ToFixed(pen.v), 0x20000) + to.y, s1);
			char* tx = FixedToString(to.x + ToFixed(pen.h), s2);
			char* fy = FixedToString(FixedDivide(ToFixed(pen.v), 0x20000) + from.y, s3);
			char* fx = FixedToString(from.x, s4);
			sprintf(fBuffer, "%s %s MvTo %s %s LnTo ", fx, fy, tx, ty);
		}
		else
		{
			// upright: down the middle of the pen's width, to the far end
			// of its height
			char* ty = FixedToString(to.y + ToFixed(pen.v), s1);
			char* tx = FixedToString(FixedDivide(ToFixed(pen.h), 0x20000) + to.x, s2);
			char* fy = FixedToString(from.y, s3);
			char* fx = FixedToString(FixedDivide(ToFixed(pen.h), 0x20000) + from.x, s4);
			sprintf(fBuffer, "%s %s MvTo %s %s LnTo ", fx, fy, tx, ty);
		}
	}
	else
	{
		if (to.y < from.y)
			SwapPoint(from, to);
		SetupLineStrings(from, to, pen, s1, s2, s3, s4, s5, s6);
		if (to.x < from.x)
			sprintf(fBuffer, "%s %s MvTo %d %d RLnTo %d %d RLnTo %s %s LnTo %d %d RLnTo %d %d RLnTo %s %s LnTo ",
				s1, s2, pen.h, 0, 0, pen.v, s4, s5, -pen.h, 0, 0, -pen.v, s1, s2);
		else
			sprintf(fBuffer, "%s %s MvTo %d %d RLnTo %s %s LnTo %d %d RLnTo %d %d RLnTo %s %s LnTo %d %d RLnTo ",
				s1, s2, pen.h, 0, s4, s3, 0, pen.v, -pen.h, 0, s1, s6, 0, -pen.v);
	}
	SendPSText(fBuffer, false);
}


/*------------------------------------------------------------------------------
	R e c t a n g l e s ,   o v a l s ,   a r c s   a n d   p o l y g o n s
------------------------------------------------------------------------------*/

// ROM 0x00156524 SendRectangle__10TPSPrinterFP4Rect5Point
// A rectangle's path, inset by half the pen.  ROM QUIRK, kept: the second
// and third corners print two of their numbers from FixedToString's
// buffers rather than its answers, so those keep a minus sign the others
// lose.
void
TPSPrinter::SendRectangle(Rect* r, Point pen)
{
	PositionPen(r->left, r->top, pen);
	char a[16], b[16], c[16], d[16];
	char* left = FixedToString(FixedDivide(ToFixed(pen.h), 0x20000) + ToFixed(r->left), a);
	char* bottom = FixedToString(ToFixed(r->bottom) - FixedDivide(ToFixed(pen.v), 0x20000), b);
	char* top = FixedToString(FixedDivide(ToFixed(pen.v), 0x20000) + ToFixed(r->top), c);
	char* right = FixedToString(ToFixed(r->right) - FixedDivide(ToFixed(pen.h), 0x20000), d);
	sprintf(fBuffer, "%s %s LnTo %s %s LnTo %s %s LnTo CP ", right, top, d, bottom, left, b);
	SendPSText(fBuffer, false);
}


// ROM 0x0015666c EmitInsetRect__10TPSPrinterFP4Rect5Point
// A rectangle's four sides, inset by half the pen, for FrameOval.
void
TPSPrinter::EmitInsetRect(Rect* r, Point pen)
{
	char a[16], b[16], c[16], d[16];
	char* right = FixedToString(ToFixed(r->right) - FixedDivide(ToFixed(pen.h), 0x20000), a);
	char* bottom = FixedToString(ToFixed(r->bottom) - FixedDivide(ToFixed(pen.v), 0x20000), b);
	char* left = FixedToString(FixedDivide(ToFixed(pen.h), 0x20000) + ToFixed(r->left), c);
	char* top = FixedToString(FixedDivide(ToFixed(pen.v), 0x20000) + ToFixed(r->top), d);
	sprintf(fBuffer, "%s %s %s %s ", top, left, bottom, right);
	SendPSText(fBuffer, false);
}


// ROM 0x001562ec DrawFillRect__10TPSPrinterFP4Rect
void
TPSPrinter::DrawFillRect(Rect* r)
{
	sprintf(fBuffer, "%d %d %d %d StdRect", r->top, r->left, r->bottom, r->right);
	SendPSText(fBuffer, false);
	sprintf(fBuffer, fDoPatternFill ? " PatternFill\r" : " fill\r");
	SendPSText(fBuffer, false);
}


// ROM 0x001564d8 DrawFrameRect__10TPSPrinterFP4Rect5Point
void
TPSPrinter::DrawFrameRect(Rect* r, Point pen)
{
	SetupPen(pen);
	SendRectangle(r, pen);
	SendPSText((char*) "SclPen\r", false);
}


// ROM 0x00156114 DrawFillOval__10TPSPrinterFP4Rect
void
TPSPrinter::DrawFillOval(Rect* r)
{
	sprintf(fBuffer, "%d %d %d %d 0 360 FrameOval", r->top, r->left, r->bottom, r->right);
	SendPSText(fBuffer, false);
	sprintf(fBuffer, fDoPatternFill ? " PatternFill\r" : " fill\r");
	SendPSText(fBuffer, false);
}


// ROM 0x0015639c DrawFrameOval__10TPSPrinterFP4Rect5Point
void
TPSPrinter::DrawFrameOval(Rect* r, Point pen)
{
	SetupPen(pen);
	EmitInsetRect(r, pen);
	SendPSText((char*) "0 360 FrameOval CP SclPen\r", false);
}


// ROM 0x00159ee0 DrawAnyArc__10TPSPrinterFP4Rect5PointlT3UcT5
// An arc of the oval in r, from startAngle to endAngle (PostScript's
// degrees).  Framed, the oval is inset by half the pen; filled, it is a
// wedge from the middle.  Part of a path (a round rectangle's corner),
// it neither moves to the middle nor fills.
void
TPSPrinter::DrawAnyArc(Rect* r, Point pen, long startAngle, long endAngle, Boolean frame, Boolean partOfPath)
{
	if (partOfPath)
		SetupPen(pen);
	if (frame)
		EmitInsetRect(r, pen);
	else
	{
		if (!partOfPath)
		{
			char y[16], x[16];
			char* ys = FixedToString(FixedDivide((Fixed) (((uint32_t) (uint16_t) r->bottom + (uint16_t) r->top) << 16), 0x20000), y);
			char* xs = FixedToString(FixedDivide((Fixed) (((uint32_t) (uint16_t) r->left + (uint16_t) r->right) << 16), 0x20000), x);
			sprintf(fBuffer, "%s %s MvTo\r", xs, ys);
			SendPSText(fBuffer, false);
		}
		sprintf(fBuffer, "%d %d %d %d ", r->top, r->left, r->bottom, r->right);
		SendPSText(fBuffer, false);
	}
	sprintf(fBuffer, "%d %d FrameOval ", (int) startAngle, (int) endAngle);
	SendPSText(fBuffer, false);
	if (partOfPath)
		return;
	SendPSText((char*) (fDoPatternFill ? "PatternFill\r" : "fill\r"), false);
}


// ROM 0x001561cc DrawFillPoly__10TPSPrinterFPP7Polygon
// A polygon's points as a path, a line break every sixteen.  (The last
// point is not sent: a QuickDraw polygon ends where it began, and the
// fill closes the path.)
void
TPSPrinter::DrawFillPoly(PolyHandle poly)
{
	long count = ((*poly)->polySize - 16) >> 2;
	if (count == 0)
		return;
	Point pt = (*poly)->polyPoints[0];
	sprintf(fBuffer, "%d %d MvTo ", pt.h, pt.v);
	SendPSText(fBuffer, false);
	for (long i = 1; i < count; i++)
	{
		pt = (*poly)->polyPoints[i];
		sprintf(fBuffer, "%d %d LnTo ", pt.h, pt.v);
		if ((i & 0xf) == 0)
			strcat(fBuffer, "\r");
		SendPSText(fBuffer, false);
	}
	SendPSText((char*) (fDoPatternFill ? "PatternFill\r" : "fill\r"), false);
}


// ROM 0x001563fc DrawFramePoly__10TPSPrinterFPP7Polygon5Point
// A polygon framed a side at a time, each a QuickDraw line.
void
TPSPrinter::DrawFramePoly(PolyHandle poly, Point pen)
{
	long count = ((*poly)->polySize - 16) >> 2;
	for (long i = 0; i < count; i++)
	{
		Point a = (*poly)->polyPoints[i];
		Point b = (*poly)->polyPoints[i + 1];
		FPoint from, to;
		from.x = ToFixed(a.h);
		from.y = ToFixed(a.v);
		to.x = ToFixed(b.h);
		to.y = ToFixed(b.v);
		long width = SetLineWidth(a, b, pen);
		Draw1QDLine(from, pen, to);
		ResetLineWidth(width);
	}
}


/*------------------------------------------------------------------------------
	C u r v e s   a n d   p a t h s
------------------------------------------------------------------------------*/

// ROM 0x001595d0 Draw1Curve__10TPSPrinterFP5curve5PointUc
// A quadratic curve as PostScript's cubic: the two control points a third
// and two thirds of the way, moved by a quarter of the pen when framed.
void
TPSPrinter::Draw1Curve(curve* c, Point pen, Boolean frame)
{
	Fixed half = FixedDivide((Fixed) ((uint32_t) (uint16_t) (pen.v + pen.h) << 16), 0x40000);
	// (the sums wrap as the ARM's do)
	Fixed c1x = FixedDivide((Fixed) ((uint32_t) c->first.x + ((uint32_t) c->control.x << 1)), 0x30000);
	Fixed c1y = FixedDivide((Fixed) ((uint32_t) c->first.y + ((uint32_t) c->control.y << 1)), 0x30000);
	Fixed c2x = FixedDivide((Fixed) ((uint32_t) c->last.x + ((uint32_t) c->control.x << 1)), 0x30000);
	Fixed c2y = FixedDivide((Fixed) ((uint32_t) c->last.y + ((uint32_t) c->control.y << 1)), 0x30000);
	Fixed lx = c->last.x;
	Fixed ly = c->last.y;
	if (frame)
	{
		c1x += half;
		c1y += half;
		c2x += half;
		c2y += half;
		lx += half;
		ly += half;
	}
	char s1[16], s2[16], s3[16], s4[16], s5[16], s6[16];
	char* lys = FixedToString(ly, s1);
	char* lxs = FixedToString(lx, s2);
	char* c2ys = FixedToString(c2y, s3);
	char* c2xs = FixedToString(c2x, s4);
	char* c1ys = FixedToString(c1y, s5);
	char* c1xs = FixedToString(c1x, s6);
	sprintf(fBuffer, "%s %s %s %s %s %s CT\r", c1xs, c1ys, c2xs, c2ys, lxs, lys);
	SendPSText(fBuffer, false);
}


// ROM 0x00155f78 DrawAnyCurve__10TPSPrinterFP5curve5PointUc
void
TPSPrinter::DrawAnyCurve(curve* c, Point pen, Boolean frame)
{
	FPoint first = c->first;
	if (frame)
	{
		Fixed h = FixedDivide(ToFixed(pen.h), 0x20000);
		Fixed v = FixedDivide(ToFixed(pen.v), 0x20000);
		Fixed d = FixedDivide(h + v, 0x20000);
		OffsetFixedPoint(c->first, d, d);
	}
	char y[16], x[16];
	char* ys = FixedToString(c->first.y, y);
	char* xs = FixedToString(c->first.x, x);
	sprintf(fBuffer, "%s %s MvTo ", xs, ys);
	SendPSText(fBuffer, false);
	c->first = first;
	Draw1Curve(c, pen, frame);
	if (frame)
		SendPSText((char*) "stroke\r", false);
	else
		SendPSText((char*) (fDoPatternFill ? "PatternFill\r" : "fill\r"), false);
}


// ROM 0x0015976c CheckEmptyPath__10TPSPrinterFP4path
// Whether every point of a contour is the first (a dot).
Boolean
TPSPrinter::CheckEmptyPath(path* contour)
{
	long n = contour->vectors;
	const point* pts = (const point*) &contour->controlBits[(n + 31) >> 5];
	for (long i = 1; i < n; i++)
		if (pts[i].x != pts[0].x || pts[i].y != pts[0].y)
			return false;
	return true;
}


// ROM 0x001597c0 Draw1Path__10TPSPrinterFP4path5PointUc
// One contour of a paths, a path of its own: a dot as a short level line,
// otherwise its lines and curves (moved by a quarter of the pen when
// framed), closed when it ends where it began, then stroked or filled.
// ==> the next contour.
// ROM QUIRKS, kept: a line's end is moved by the quarter pen when filling
// as well; a curve's end is moved after it is drawn, so only the closing
// test sees it moved, and not for the first segment; and "PatternFIll"
// is misspelt, so a patterned path stops the PostScript with an undefined
// name.
path*
TPSPrinter::Draw1Path(path* contour, Point pen, Boolean frame)
{
	Fixed quarter = FixedDivide((Fixed) ((uint32_t) (uint16_t) (pen.v + pen.h) << 16), 0x40000);
	SendPSText((char*) "newpath\r", false);
	pathWalker walker;
	InitPathWalker(&walker, contour);
	char s1[16], s2[16];
	if (CheckEmptyPath(contour))
	{
		Fixed x = walker.p->x;
		char* ys = FixedToString(walker.p->y + quarter, s1);
		char* xs = FixedToString(x, s2);
		sprintf(fBuffer, "%s %s MvTo ", xs, ys);
		SendPSText(fBuffer, false);
		char* ws = FixedToString(quarter << 1, s2);
		sprintf(fBuffer, "%s 0 RLnTo ", ws);
		SendPSText(fBuffer, false);
	}
	else
	{
		// (the ROM leaves these unset when the contour has no segments at
		// all; the host starts them at nought)
		Fixed startX = 0, startY = 0, lastX = 0, lastY = 0;
		curve cur;
		long lines = 0;
		if (NextPathSegment(&walker))
		{
			cur = walker.c;
			if (frame)
				OffsetFixedPoint(cur.first, quarter, quarter);
			char* ys = FixedToString(cur.first.y, s1);
			char* xs = FixedToString(cur.first.x, s2);
			sprintf(fBuffer, "%s %s MvTo ", xs, ys);
			SendPSText(fBuffer, false);
			startX = cur.first.x;
			startY = cur.first.y;
			if (walker.isLine)
			{
				OffsetFixedPoint(cur.last, quarter, quarter);
				DoPSLine(cur.last);
			}
			else
				Draw1Curve(&cur, pen, frame);
			lastX = cur.last.x;
			lastY = cur.last.y;
		}
		while (NextPathSegment(&walker))
		{
			cur = walker.c;
			if (walker.isLine)
			{
				OffsetFixedPoint(cur.last, quarter, quarter);
				DoPSLine(cur.last);
				if (++lines >= 4)
				{
					lines = 0;
					SendPSText((char*) "\r", false);
				}
			}
			else
			{
				Draw1Curve(&cur, pen, frame);
				OffsetFixedPoint(cur.last, quarter, quarter);
			}
			lastX = cur.last.x;
			lastY = cur.last.y;
		}
		if (lastX == startX && startY == lastY)
			SendPSText((char*) "CP", false);
	}
	if (frame)
		SendPSText((char*) "\rstroke\r", false);
	else
		SendPSText((char*) (fDoPatternFill ? "\rPatternFIll\r" : "\rfill\r"), false);
	return NextPath(contour);
}


// ROM 0x001560a8 DrawAnyPath__10TPSPrinterFPP5paths5PointUc
void
TPSPrinter::DrawAnyPath(paths** p, Point pen, Boolean frame)
{
	long count = (*p)->contours;
	path* contour = &(*p)->contour[0];
	HLock((Handle) p);
	for ( ; count != 0; count--)
		contour = Draw1Path(contour, pen, frame);
	HUnlock((Handle) p);
}


/*------------------------------------------------------------------------------
	T e x t
------------------------------------------------------------------------------*/

// ROM 0x00157090 SetupPSTextMode__10TPSPrinterFl
// The gray text is drawn in when its style has no pattern: white for
// srcBic (3), else black.
UChar
TPSPrinter::SetupPSTextMode(TextObjectRef text)
{
	TextOptions* options;
	GetTextObjField(text, kTextObjOptions, &options);
	long mode = 1;
	if (options != nil)
		mode = options->fTransferMode;
	if (mode == 0 || mode == 1)
		return 0;
	if (mode == 3)
		return 0x40;
	return 0;
}


// ROM 0x001579fc UnicodeToDestmap__10TPSPrinterFPcl
// A character in the font's encoding: a byte, or two for a Shift-JIS
// lead byte; a character the encoding lacks is a bullet.
// DEVIATION: an encoding the host has no table for (the ROM's index runs
// past its own table for Shift-JIS, 6, which no U.S. font asks for) gives
// no bytes, so a bullet.
long
TPSPrinter::UnicodeToDestmap(char* chars, long encoding)
{
	UChar bytes[4] = { 0, 0, 0, 0 };
	if (encoding >= 0 && encoding < kNumberOfEncodings)
		ConvertFromUnicode((const UniChar*) chars, bytes, encoding, 1);
	long c = bytes[0];
	if (encoding == 6 && ((c >= 0x81 && c <= 0x9f) || (c >= 0xe0 && c <= 0xfb)))
		c = (c << 8) + bytes[1];
	if (c == 0)
		c = 0xa5;
	return c;
}


// ROM 0x001570e0 DoSelectFont__10TPSPrinterFUc
// The PostScript font for the family, size and face last chosen: the
// screen family's psName looked up in vars.psFonts (the system's
// PostScript font when it has none there, or when there is no family),
// its slot for the face (normal, bold, italic, bolditalic) the font's
// name, the size scaled by the family's psScale and by 0.8 for a
// superscript or subscript - the Mac-encoded copy ("-Mac") when the
// family's encoding is the Mac's and macEncoding says so.
void
TPSPrinter::DoSelectFont(Boolean macEncoding)
{
	RefVar font(fFont);
	Fixed psScale = 0;
	long face = fFontFace;
	RefVar psFonts(GetFrameSlotRef(RefVar(gVarFrame), RSSYMpsfonts));
	if (NOTNIL(font))
	{
		RefVar psName(GetFrameSlotRef(font, RSSYMpsname));
		if (NOTNIL(psName))
		{
			RefVar scale(GetFrameSlotRef(font, RSSYMpsscale));
			if (ISINT(scale))
				psScale = RINT(scale);
			RefVar entry(GetProtoVariable(psFonts, psName, nil));
			if (ISNIL(entry))
				font = GetProtoVariable(psFonts, RefVar(Rsystempsfont), nil);
			else
				font = entry;
		}
	}
	else
		font = GetProtoVariable(psFonts, RefVar(Rsystempsfont), nil);
	RefVar faceSym(NILREF);
	switch (face & 3)
	{
	case 0:	faceSym = RSSYMnormal; break;
	case 1:	faceSym = RSSYMbold; break;
	case 2:	faceSym = RSSYMitalic; break;
	case 3:	faceSym = RSSYMbolditalic; break;
	}
	char name[128];
	RefVar psFontName(GetFrameSlotRef(font, faceSym));
	ConvertFromUnicode(GetCString(psFontName), name, kMacRomanEncoding, 0x7FFFFFFF);
	fEncoding = 1;
	if (FrameHasSlotRef(font, RSSYMprencoding))
		fEncoding = RINT(GetFrameSlotRef(font, RSSYMprencoding));
	Boolean superscript = (face & 0x80) != 0;
	Boolean subscript = (face & 0x100) != 0;
	Fixed size = fFontSize;
	if (superscript || subscript)
		size = FixedMultiply(size, 0xcccd);
	if (psScale != 0)
		size = FixedMultiply(size, psScale);
	char sizeText[16];
	char* sizeString = FixedToString(size, sizeText);
	if (fEncoding != 1 || !macEncoding)
		sprintf(fBuffer, "/%s %s SF\r", name, sizeString);
	else
		sprintf(fBuffer, "/%s-Mac %s SF\r", name, sizeString);
	SendPSText(fBuffer, false);
}


// ROM 0x00157414 FlushBuffer__10TPSPrinterFPcRllT3P11StyleRecordN23
// The characters from start to end shown in the font selected: up to 247
// at a time as a PostScript string, (, ) and \ escaped, by "show" - or by
// "awidthshow" when the text is justified, each character given
// charExtra and each space spaceExtra.  start is moved to end.
// ROM BUGS, kept: the space's extra is taken down by charExtra again for
// every string after the first; and a string cut at 245 bytes ends after a
// character that the next string starts with again.
void
TPSPrinter::FlushBuffer(char* chars, long& start, long end, long charSize, StyleRecord* /*style*/, Fixed charExtra, Fixed spaceExtra)
{
	const long kMaxChars = 0xf7;
	const long kMaxBytes = kMaxChars - 2;
	if (start == end)
		return;
	do
	{
		if (charExtra != 0 || spaceExtra != 0)
		{
			char ax[16], cx[16];
			spaceExtra -= charExtra;
			char* axs = FixedToString(charExtra, ax);
			char* cxs = FixedToString(spaceExtra, cx);
			sprintf(fBuffer, "%s 0 32 %s 0 ", cxs, axs);
			SendPSText(fBuffer, false);
		}
		fBuffer[0] = '(';
		long n = 1;
		long count = end - start;
		if (count > kMaxChars)
			count = kMaxChars;
		for (long i = 0; i < count; i++)
		{
			long c = UnicodeToDestmap(chars + (start + i) * charSize, fEncoding);
			if (c >= 0x100)
			{
				long lead = c >> 8;
				c &= 0xff;
				if (lead == '(' || lead == ')' || lead == '\\')
					fBuffer[n++] = '\\';
				fBuffer[n++] = (char) lead;
			}
			if (c == '(' || c == ')' || c == '\\')
				fBuffer[n++] = '\\';
			fBuffer[n++] = (char) c;
			if (kMaxBytes <= n)
				count = i;
		}
		fBuffer[n] = 0;
		strcat(fBuffer, (charExtra != 0 || spaceExtra != 0) ? ") awidthshow\r" : ") show\r");
		SendPSText(fBuffer, false);
		start += count;
	} while (end - start > 0);
}


// ROM 0x00157628 HandleCharacters__10TPSPrinterFPclRlT2PlP11StyleRecordN22
// A character looked at before it is shown: one of the Mac's maths signs
// (which PostScript's text fonts lack) switches to the Symbol font, a
// change of style to the style's font - the characters before it flushed
// in the font they were meant for.  A family with a noremap slot is
// selected in its own encoding.
void
TPSPrinter::HandleCharacters(char* chars, long index, long& start, long charSize, long* /*widths*/, StyleRecord* style, Fixed charExtra, Fixed spaceExtra)
{
	char* p = chars + charSize * index;
	long c;
	if (charSize > 1)
		c = UnicodeToDestmap(p, 1);
	else
		c = *(UChar*) p;
	Boolean macEncoding = true;
	if (NOTNIL(style->fFontFamily))
	{
		if (NOTNIL(GetFrameSlotRef(style->fFontFamily, RSSYMnoremap)))
			macEncoding = false;
	}
	Boolean symbol = false;
	switch (c)
	{
	case 0xad: case 0xb0: case 0xb2: case 0xb3: case 0xb6: case 0xb7: case 0xb8: case 0xb9:
	case 0xba: case 0xbd: case 0xc3: case 0xc5: case 0xc6: case 0xd7: case 0xf0:
		symbol = true;
		break;
	}
	if (symbol)
	{
		if (EQRef(fFont, Rsymbolfont))
			return;
		fFont = Rsymbolfont;
	}
	else
	{
		if (EQRef(fFont, style->fFontFamily) && fFontSize == style->fFontSize && fFontFace == style->fFontFace)
			return;
		fFont = style->fFontFamily;
	}
	fFontSize = style->fFontSize;
	fFontFace = style->fFontFace;
	FlushBuffer(chars, start, index, charSize, style, charExtra, spaceExtra);
	DoSelectFont(macEncoding);
}


// ROM 0x001577f0 EmitText__10TPSPrinterFlPcT1PlP11StyleRecordN21
// A style run's characters shown: raised by three tenths of the size for
// a superscript (lowered for a subscript) and put back after, and
// underlined (the prolog's UL, thicker when bold).
void
TPSPrinter::EmitText(long count, char* chars, long charSize, long* widths, StyleRecord* style, Fixed charExtra, Fixed spaceExtra)
{
	long start = 0;
	Fixed shift = 0;
	Boolean superscript = (style->fFontFace & 0x80) != 0;
	Boolean subscript = (style->fFontFace & 0x100) != 0;
	char text[16];
	if (superscript || subscript)
	{
		shift = FixedMultiply(style->fFontSize, 0x4ccd);
		if (superscript)
			shift = -shift;
		FixedToString(shift, text);
		sprintf(fBuffer, "0 %s rmoveto ", text);
		SendPSText(fBuffer, false);
	}
	if (style->fFontFace & 4)
		SendPSText((char*) "CurPt ", false);
	for (long i = 0; i < count; i++)
		HandleCharacters(chars, i, start, charSize, widths, style, charExtra, spaceExtra);
	FlushBuffer(chars, start, count, charSize, style, charExtra, spaceExtra);
	if (style->fFontFace & 4)
	{
		sprintf(fBuffer, "CurPt %d %d UL\r", (int) RoundFixed16(style->fFontSize), (style->fFontFace & 1) ? 1 : 0);
		SendPSText(fBuffer, false);
	}
	if (superscript || subscript)
	{
		FixedToString(-shift, text);
		sprintf(fBuffer, "0 %s rmoveto ", text);
		SendPSText(fBuffer, false);
	}
}


/*------------------------------------------------------------------------------
	T h e   b o t t l e n e c k s
------------------------------------------------------------------------------*/

// The start every bottleneck shares: a shape the port is recording (a
// picture with the pen hidden, or a region being framed) given to the
// standard proc with the pen hidden.
static inline Boolean
Recording(GrafPort* port, short pnVis, GrafVerb verb)
{
	return (port->picSave != nil && pnVis == -1) || (port->rgnSave != nil && verb == 0);
}


// ROM 0x00157a68 PrStdText__FlN21
// Text: moved to its start (the location, plus where the justification
// starts it), then shown a style run at a time (EmitText), in the run's
// pattern's gray; an ink word is drawn by its glyph, translated to the
// current point.
// DEVIATION: the ROM walks the runs with a TextWalker (ScanNextChunk),
// which the host's text objects do not have - the host's text is taken
// whole (qd/TextObject.h); the runs are walked here from the object's run
// lengths, a chunk being a run.
void
PrStdText(TextObjectRef text, Fixed hScale, Fixed vScale)
{
	GrafPort* port;
	TPSPrinter* printer = CurrentPSPrinter(&port);
	short pnVis = port->pnVis;
	if (port->picSave != nil && pnVis == -1)
	{
		port->pnVis = -1;
		StdText(text, hScale, vScale);
		port->pnVis = pnVis;
	}
	if (!printer->ContinueRendering() || pnVis < 0)
		return;
	printer->SetClip(port);
	SetTextObjField(text, kTextObjSetFlag10000, nil);
	Fixed metrics[3];
	GetTextObjField(text, kTextObjMetrics, metrics);
	FPoint location;
	GetTextObjField(text, kTextObjLocation, &location);
	location.x += metrics[0];
	sprintf(printer->fBuffer, "%d %d MvTo\r", (int) RoundFixed16(location.x), (int) RoundFixed16(location.y));
	printer->SendPSText(printer->fBuffer, false);
	UChar mode = printer->SetupPSTextMode(text);
	long remaining;
	GetTextObjField(text, kTextObjFittedLength, &remaining);

	// the walker
	TextObject* obj = TextObj(text);
	TTextObjectChars characters(obj);
	StyleRecord** styles = obj->fStyles;
	const short* runLengths = obj->fRunLengths;
	long length = obj->fLength;
	long offset = 0;
	long run = 0;
	const long charSize = sizeof(UniChar);
	long count;
	do
	{
		StyleRecord* style = styles[runLengths != nil ? run : 0];
		count = (runLengths != nil) ? runLengths[run] : length - offset;
		if (count > length - offset)
			count = length - offset;
		if (count < 0)
			count = 0;
		char* chars = (char*) (characters.fChars + offset);
		offset += count;
		run++;

		remaining -= count;
		if (remaining < 0)
		{
			count += remaining;
			remaining = 0;
		}
		if (count == 0)
			break;
		UChar gray = mode;
		if (style->fFontPattern != 0)
			gray = (UChar) (0x40 - printer->CountBitsInPattern((PatternHandle) RefToAddress(style->fFontPattern)));
		printer->DoSetGray(gray);
		if (!IsInkWord(style->fFontFamily) && !ISINT((Ref) style->fFontFamily))
			printer->EmitText(count, chars, charSize, nil, style, metrics[1], metrics[2]);
		else
		{
			TInkWordGlyph glyph(style->fFontFamily, (short) RoundFixed16(style->fFontSize), style->fFontFace);
			sprintf(printer->fBuffer, "gsave CurPt %d sub translate\r", (int) glyph.fAscent);
			printer->SendPSText(printer->fBuffer, false);
			glyph.DrawAt(0, glyph.fAscent);
			sprintf(printer->fBuffer, "grestore \r");
			printer->SendPSText(printer->fBuffer, false);
			sprintf(printer->fBuffer, "CurPt MvTo %d 0 rmoveto\r", (int) glyph.fWidth);
			printer->SendPSText(printer->fBuffer, false);
		}
	} while (count != 0);
}


// ROM 0x00157de4 PrStdLine__F5Point
// A line from the pen to the point: drawn by the standard proc (pen
// hidden) when it is being recorded, else just the pen moved; then the
// line's path in the pen's gray.
void
PrStdLine(Point to)
{
	GrafPort* port;
	TPSPrinter* printer = CurrentPSPrinter(&port);
	short pnVis = port->pnVis;
	Point from = port->pnLoc;
	if ((port->picSave != nil && port->pnVis == -1) || port->rgnSave != nil || port->polySave != nil)
	{
		port->pnVis = -1;
		StdLine(to);
		port->pnVis = pnVis;
	}
	else
		MoveTo(to.h, to.v);
	if (!printer->ContinueRendering() || pnVis < 0)
		return;
	printer->SetClip(port);
	printer->DoSetGray((UChar) (0x40 - printer->CountBitsInPattern(port->fgPat)));
	long width = printer->SetLineWidth(from, to, port->pnSize);
	printer->SendPSText((char*) "newpath\r", false);
	FPoint a, b;
	a.x = ToFixed(from.h);
	a.y = ToFixed(from.v);
	b.x = ToFixed(to.h);
	b.y = ToFixed(to.v);
	printer->Draw1QDLine(a, port->pnSize, b);
	printer->ResetLineWidth(width);
}


// ROM 0x00157fc8 PrStdRect__FUcP4Rect
void
PrStdRect(GrafVerb verb, Rect* r)
{
	GrafPort* port;
	TPSPrinter* printer = CurrentPSPrinter(&port);
	short pnVis = port->pnVis;
	if (Recording(port, pnVis, verb))
	{
		port->pnVis = -1;
		StdRect(verb, r);
		port->pnVis = pnVis;
	}
	if (!printer->ContinueRendering() || pnVis < 0 || verb == 3)
		return;
	printer->SetClip(port);
	printer->SetGrayLevel(verb, port);
	printer->SendPSText((char*) "newpath\r", false);
	if (verb == 0)
		printer->DrawFrameRect(r, port->pnSize);
	else if (verb == 1 || verb == 2 || verb == 4)
		printer->DrawFillRect(r);
}


// ROM 0x001580e8 PrStdRRect__FUcP4RectlT3
// A round rectangle: its four corners' arcs joined by its sides, a path
// framed with the pen or filled.
void
PrStdRRect(GrafVerb verb, Rect* r, long ovalWidth, long ovalHeight)
{
	GrafPort* port;
	TPSPrinter* printer = CurrentPSPrinter(&port);
	short pnVis = port->pnVis;
	if (Recording(port, pnVis, verb))
	{
		port->pnVis = -1;
		StdRRect(verb, r, ovalWidth, ovalHeight);
		port->pnVis = pnVis;
	}
	if (!printer->ContinueRendering() || pnVis < 0 || verb == 3)
		return;
	Fixed halfWidth = FixedDivide(ToFixed(ovalWidth), 0x20000);
	Fixed halfHeight = FixedDivide(ToFixed(ovalHeight), 0x20000);
	printer->SetClip(port);
	printer->SetGrayLevel(verb, port);
	printer->SendPSText((char*) "newpath\r", false);
	char number[16];
	Rect corner;
	SetRect(&corner, r->right - ovalWidth, r->top, r->right, r->top + ovalHeight);
	printer->DrawAnyArc(&corner, port->pnSize, 270, 360, verb == 0, true);
	sprintf(printer->fBuffer, "0 %s CurPt exch pop sub RLnTo\r", printer->FixedToString(ToFixed(r->bottom) - halfHeight, number));
	printer->SendPSText(printer->fBuffer, false);
	SetRect(&corner, r->right - ovalWidth, r->bottom - ovalHeight, r->right, r->bottom);
	printer->DrawAnyArc(&corner, port->pnSize, 0, 90, verb == 0, true);
	sprintf(printer->fBuffer, "%s CurPt pop sub 0 RLnTo\r", printer->FixedToString(halfWidth + ToFixed(r->left), number));
	printer->SendPSText(printer->fBuffer, false);
	SetRect(&corner, r->left, r->bottom - ovalHeight, r->left + ovalWidth, r->bottom);
	printer->DrawAnyArc(&corner, port->pnSize, 90, 180, verb == 0, true);
	sprintf(printer->fBuffer, "0 %s CurPt exch pop sub RLnTo\r", printer->FixedToString(halfHeight + ToFixed(r->top), number));
	printer->SendPSText(printer->fBuffer, false);
	SetRect(&corner, r->left, r->top, r->left + ovalWidth, r->top + ovalHeight);
	printer->DrawAnyArc(&corner, port->pnSize, 180, 270, verb == 0, true);
	sprintf(printer->fBuffer, "%s CurPt pop sub 0 RLnTo\r", printer->FixedToString(ToFixed(r->right) - halfWidth, number));
	printer->SendPSText(printer->fBuffer, false);
	if (verb == 0)
		printer->SendPSText((char*) "SclPen\r", false);
	else if (verb == 1 || verb == 2 || verb == 4)
		printer->SendPSText((char*) (printer->GetDoPatternFill() ? "PatternFill\r" : "fill\r"), false);
}


// ROM 0x00158524 PrStdOval__FUcP4Rect
void
PrStdOval(GrafVerb verb, Rect* r)
{
	GrafPort* port;
	TPSPrinter* printer = CurrentPSPrinter(&port);
	short pnVis = port->pnVis;
	if (Recording(port, pnVis, verb))
	{
		port->pnVis = -1;
		StdOval(verb, r);
		port->pnVis = pnVis;
	}
	if (!printer->ContinueRendering() || pnVis < 0 || verb == 3)
		return;
	printer->SetClip(port);
	printer->SetGrayLevel(verb, port);
	printer->SendPSText((char*) "newpath\r", false);
	if (verb == 0)
		printer->DrawFrameOval(r, port->pnSize);
	else if (verb == 1 || verb == 2 || verb == 4)
		printer->DrawFillOval(r);
}


// ROM 0x00158644 PrStdArc__FUcP4RectlT3
// An arc: QuickDraw's angles (clockwise from twelve o'clock) made
// PostScript's (from three o'clock, clockwise in the flipped page), the
// whole oval when the arc is a full turn or more.
void
PrStdArc(GrafVerb verb, Rect* r, long startAngle, long arcAngle)
{
	GrafPort* port;
	TPSPrinter* printer = CurrentPSPrinter(&port);
	short pnVis = port->pnVis;
	if (Recording(port, pnVis, verb))
	{
		port->pnVis = -1;
		StdArc(verb, r, startAngle, arcAngle);
		port->pnVis = pnVis;
	}
	if (!printer->ContinueRendering() || pnVis < 0 || verb == 3)
		return;
	printer->SetClip(port);
	printer->SetGrayLevel(verb, port);
	printer->SendPSText((char*) "newpath\r", false);
	long from, to;
	if (arcAngle < 360 && arcAngle > -360)
	{
		from = startAngle - 90;
		to = startAngle + arcAngle - 90;
		if (arcAngle < 0)
		{
			long t = from;
			from = to;
			to = t;
		}
	}
	else
	{
		from = 0;
		to = 360;
	}
	if (verb == 0)
	{
		printer->DrawAnyArc(r, port->pnSize, from, to, true, true);
		printer->SendPSText((char*) "SclPen\r", false);
	}
	else if (verb == 1 || verb == 2 || verb == 4)
		printer->DrawAnyArc(r, printer->fFillPen, from, to, false, false);
}


// ROM 0x001587f4 PrStdPoly__FUcPP7Polygon
void
PrStdPoly(GrafVerb verb, PolyHandle poly)
{
	GrafPort* port;
	TPSPrinter* printer = CurrentPSPrinter(&port);
	short pnVis = port->pnVis;
	if (Recording(port, pnVis, verb) || port->polySave != nil)
	{
		port->pnVis = -1;
		StdPoly(verb, poly);
		port->pnVis = pnVis;
	}
	if (!printer->ContinueRendering() || pnVis < 0 || verb == 3)
		return;
	printer->SetClip(port);
	printer->SetGrayLevel(verb, port);
	printer->SendPSText((char*) "newpath\r", false);
	if (verb == 0)
		printer->DrawFramePoly(poly, port->pnSize);
	else if (verb == 1 || verb == 2 || verb == 4)
		printer->DrawFillPoly(poly);
}


// ROM 0x00158920 PrStdRgn__FUcPP6Region
void
PrStdRgn(GrafVerb /*verb*/, RgnHandle /*rgn*/)
{
	TPSPrinter* printer = CurrentPSPrinter(nil);
	printer->SendPSText((char*) "% --- Regions are not supported\r", false);
}


// ROM 0x0015896c PrStdBits__FP8PixelMapP4RectT2lPP6Region
// A bitmap as an image: translated to the destination and scaled to it
// (the pixel map's resolution taken as 72 dots an inch over it, adjusted
// to come out the destination's size), the transfer inverted (a Newton
// pixel of 1 is black), and the rows sent as hex for the prolog's bimage
// a block of about 20K at a time - realigned to a byte when the source
// starts part way into one, and a deeper map's pixels through its gray
// table.  A fax's bitmap (204 dots an inch) moves the transport's
// progress on.
// ROM BUGS, kept: the source rectangle's top is not looked at (the rows
// sent start at the map's first); a map deeper than 8 bits sends nothing,
// leaves the gsave unmatched and keeps its realigning buffer; and the
// realigning buffer is never given back at all.
// DEVIATION: a pixel map with no gray table - the ROM looks its pixels up
// in whatever lies at address 0 - is sent as it is.
void
PrStdBits(PixelMap* src, Rect* srcRect, Rect* dstRect, long mode, RgnHandle mask)
{
	GrafPort* port;
	TPSPrinter* printer = CurrentPSPrinter(&port);
	UChar* aligned = nil;
	short pnVis = port->pnVis;
	if (port->picSave != nil && pnVis == -1)
	{
		port->pnVis = -1;
		StdBits(src, srcRect, dstRect, mode, mask);
		port->pnVis = pnVis;
	}
	if (!printer->ContinueRendering() || pnVis < 0)
		return;
	Boolean skip = false;		// (the ROM's: never set)
	Boolean isFax = false;
	printer->SetClip(port);
	long srcWidth = srcRect->right - srcRect->left;
	long srcHeight = srcRect->bottom - srcRect->top;
	if (src->deviceRes.v == 204 || src->deviceRes.h == 204)
		isFax = true;
	long depth = src->pixMapFlags & 0xff;
	long rowOut = (depth * srcWidth + 7) >> 3;
	ULong pairs = (ULong) (rowOut + 1) >> 1;
	long hexBytes = rowOut << 1;
	long chunk = src->rowBytes * (0x5000 / src->rowBytes);
	long shift = 0;
	if (depth == 1)
		shift = 3;
	else if (depth == 2)
		shift = 2;
	else if (depth == 4)
		shift = 1;
	long offset = srcRect->left - src->bounds.left;
	UChar* start = (UChar*) GetPixelMapBits(src) + (offset >> shift);
	long leftShift = 0, rightShift = 0;
	if (depth < 8)
	{
		leftShift = (depth * offset) & 7;
		rightShift = 8 - leftShift;
		aligned = (UChar*) NewPtr(rowOut);
		if (aligned == nil)
			Throw(exOutOfMemory, (void*) (long) kError_No_Memory, nil);
	}
	UChar* first = start;
	UChar* end = start + srcHeight * src->rowBytes;
	long progressScale = (end - start) >> 4;
	UChar* hex = (UChar*) NewPtr(hexBytes + 2);
	if (hex == nil)
	{
		if (aligned != nil)
			DisposPtr((Ptr) aligned);
		Throw(exOutOfMemory, (void*) (long) kError_No_Memory, nil);
		return;
	}
	char* buffer = printer->fBuffer;
	printer->SendPSText((char*) "gsave\r", false);
	sprintf(buffer, "%d %d translate\r", dstRect->left, dstRect->top);
	printer->SendPSText(buffer, false);
	long dstWidth = dstRect->right - dstRect->left;
	long dstHeight = dstRect->bottom - dstRect->top;
	Fixed scaleH = (src->deviceRes.h != 0) ? FixedDivide(ToFixed(72), ToFixed(src->deviceRes.h)) : 0x10000;
	Fixed scaleV = (src->deviceRes.v != 0) ? FixedDivide(ToFixed(72), ToFixed(src->deviceRes.v)) : 0x10000;
	long w = RoundFixed16(FixedMultiply(scaleH, ToFixed(srcWidth)));
	if (w != dstWidth)
		scaleH = FixedMultiply(scaleH, FixedDivide(ToFixed(dstWidth), ToFixed(w)));
	long h = RoundFixed16(FixedMultiply(scaleV, ToFixed(srcHeight)));
	if (h != dstHeight)
		scaleV = FixedMultiply(scaleV, FixedDivide(ToFixed(dstHeight), ToFixed(h)));
	sprintf(buffer, "%d %d scale\r", (int) RoundFixed16(FixedMultiply(scaleH, ToFixed(srcWidth))), (int) RoundFixed16(FixedMultiply(scaleV, ToFixed(srcHeight))));
	printer->SendPSText(buffer, false);
	printer->SendPSText((char*) "{1 exch sub} currenttransfer concatprocs settransfer\r", false);
	PrintPatchpoint();
	const UChar* grays = nil;
	if (depth == 1)
		sprintf(buffer, "%d %d %d bimage\r", (int) srcWidth, (int) srcHeight, (int) depth);
	else if (depth > 8)
	{
		if (printer->fLevel == 1 || (src->pixMapFlags & 0x4000000) != 0 || (src->pixMapFlags & 0x2000000) != 0)
		{
			DisposPtr((Ptr) hex);
			return;
		}
		// (a deeper map's own image is NOT YET in the ROM either: the
		// buffer is sent as it stands - the scale again)
	}
	else
	{
		grays = src->grayTable;
		if (printer->fLevel == 1)
			sprintf(buffer, "%d %d %d bimage\r", (int) srcWidth, (int) srcHeight, (int) depth);
	}
	printer->SendPSText(buffer, false);
	PrintPatchpoint();

	// a pixel through the gray table (DEVIATION above: none, as it is)
	#define GRAY(i)	(grays != nil ? grays[i] : (UChar) (i))
	UChar* blockEnd;
	do
	{
		blockEnd = start + chunk;
		if (blockEnd > end)
			blockEnd = end;
		for (UChar* row = start; row < blockEnd; row += src->rowBytes)
		{
			const UChar* in = row;
			if (leftShift != 0)
			{
				for (long i = 0; i < rowOut; i++)
					aligned[i] = (UChar) ((row[i] << leftShift) | (row[i + 1] >> rightShift));
				in = aligned;
			}
			if (!skip)
			{
				UChar* out = hex;
				for (ULong i = 0; i < pairs; i++)
				{
					UChar b0 = *in++;
					UChar b1 = *in++;
					switch (depth)
					{
					case 1:
						break;
					case 2:
						b0 = (UChar) ((((GRAY(b0 >> 6) & 3) << 6) | ((GRAY((b0 & 0x30) >> 4) & 0xf) << 4) | ((GRAY((b0 & 0xc) >> 2) & 0x3f) << 2) | GRAY(b0 & 3)) & 0xff);
						b1 = (UChar) ((((GRAY(b1 >> 6) & 3) << 6) | ((GRAY((b1 & 0x30) >> 4) & 0xf) << 4) | ((GRAY((b1 & 0xc) >> 2) & 0x3f) << 2) | GRAY(b1 & 3)) & 0xff);
						break;
					case 4:
						b0 = (UChar) ((((GRAY(b0 >> 4) << 4) & 0xff) | GRAY(b0 & 0xf)) & 0xff);
						b1 = (UChar) ((((GRAY(b1 >> 4) << 4) & 0xff) | GRAY(b1 & 0xf)) & 0xff);
						break;
					case 8:
						b0 = GRAY(b0);
						b1 = GRAY(b1);
						break;
					default:
						goto send;		// (nothing converted: the buffer as it was)
					}
					*out++ = (UChar) (gPSBinToHex[b0] >> 8);
					*out++ = (UChar) gPSBinToHex[b0];
					*out++ = (UChar) (gPSBinToHex[b1] >> 8);
					*out++ = (UChar) gPSBinToHex[b1];
				}
			send:
				printer->SendPSBinary((char*) hex, hexBytes);
			}
		}
		start += chunk;
		if (isFax)
		{
			Fixed done = FixedDivide(ToFixed((long) ((start - first) >> 4)), ToFixed(progressScale));
			long percent = RoundFixed16(FixedMultiply(ToFixed(100), done));
			if (percent > 0 && percent < 100 && gTransportProgress != nil)
				SetFaxPrintProgress(*gTransportProgress, 0, percent);
		}
	} while (blockEnd < end);
	#undef GRAY
	printer->SendPSText((char*) "\rgrestore\r", false);
	if (hex != nil)
		DisposPtr((Ptr) hex);
}


// ROM 0x00159290 PrStdCurve__FUcP5curve
// A curve, its line width half the pen's height and width added.
// ROM BUG, kept: the width is set without the current one being saved
// first (PrStdPaths says "CLW %s SLW"), so the closing SLW finds nothing
// on the PostScript stack.
void
PrStdCurve(GrafVerb verb, curve* c)
{
	GrafPort* port;
	TPSPrinter* printer = CurrentPSPrinter(&port);
	short pnVis = port->pnVis;
	if (Recording(port, pnVis, verb))
	{
		port->pnVis = -1;
		StdCurve(verb, c);
		port->pnVis = pnVis;
	}
	if (!printer->ContinueRendering() || pnVis < 0 || verb == 3)
		return;
	printer->SetClip(port);
	printer->SetGrayLevel(verb, port);
	char number[16];
	Fixed width = FixedDivide((Fixed) ((uint32_t) (uint16_t) (port->pnSize.h + port->pnSize.v) << 16), 0x20000);
	sprintf(printer->fBuffer, "%s SLW ", printer->FixedToString(width, number));
	printer->SendPSText(printer->fBuffer, false);
	printer->SendPSText((char*) "newpath\r", false);
	if (verb == 0)
		printer->DrawAnyCurve(c, port->pnSize, true);
	else if (verb == 1 || verb == 2 || verb == 4)
		printer->DrawAnyCurve(c, printer->fFillPen, false);
	printer->SendPSText((char*) "SLW\r", false);
}


// ROM 0x0015943c PrStdPaths__FUcPP5paths
// Paths (the outlines ink is drawn as on a printer): framed with a line
// nine sixteenths of the pen's height and width added, or filled.
void
PrStdPaths(GrafVerb verb, pathsHandle p)
{
	GrafPort* port;
	TPSPrinter* printer = CurrentPSPrinter(&port);
	short pnVis = port->pnVis;
	if (Recording(port, pnVis, verb))
	{
		port->pnVis = -1;
		StdPaths(verb, p);
		port->pnVis = pnVis;
	}
	if (!printer->ContinueRendering() || pnVis < 0 || verb == 3)
		return;
	printer->SetClip(port);
	printer->SetGrayLevel(verb, port);
	if (verb == 0)
	{
		char number[16];
		Fixed width = (Fixed) ((uint32_t) (uint16_t) (port->pnSize.h + port->pnSize.v) << 16);
		width >>= 1;
		sprintf(printer->fBuffer, "CLW %s SLW ", printer->FixedToString(width + (width >> 3), number));
		printer->SendPSText(printer->fBuffer, false);
		printer->DrawAnyPath(p, port->pnSize, true);
		printer->SendPSText((char*) "SLW\r", false);
	}
	else if (verb == 1 || verb == 2 || verb == 4)
		printer->DrawAnyPath(p, printer->fFillPen, false);
}


// ROM 0x00159768 PrStdComment__FsT1PPc
// Picture comments mean nothing to the printer.
void
PrStdComment(short /*kind*/, short /*dataSize*/, Handle /*data*/)
{
}
