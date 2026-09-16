/*
	File:		qd/Text.cpp

	Contains:	Drawing and measuring text.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
	The ROM composes a chunk's glyphs into a one-bit slab (DrTextChunk
	0x00331794) and blits it; the host draws each glyph as a region
	through DrawRgn - the same pixels for an unscaled strike (DEVIATION:
	the code).
*/

#include "Text.h"
#include "Draw.h"
#include "FixedMath.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "NativeFunctions.h"
#include "RichString.h"
#include "Unicode.h"
#include "OSErrors.h"
#include <string.h>


// the glyph's set bits as a region at (left, top) - each row's runs
// become the region's change points
static RgnHandle
GlyphRgn(const FontEngineInfo* info, long left, long top)
{
	RgnHandle rgn = NewRgn();
	if (rgn == nil || info->fGlyphBits == nil || info->fGlyphWidth == 0 || info->fGlyphHeight == 0)
		return rgn;
	long limit = 0x100;
	Handle points = NewHandle(limit);
	if (points == nil)
		return rgn;
	long offset = 0;
	for (long row = 0; row < info->fGlyphHeight; row++)
	{
		const unsigned char* bits = info->fGlyphBits + row * info->fGlyphRowBytes;
		long x = 0;
		while (x < info->fGlyphWidth)
		{
			if (!(bits[x >> 3] & (0x80 >> (x & 7))))
			{
				x++;
				continue;
			}
			long start = x;
			while (x < info->fGlyphWidth && (bits[x >> 3] & (0x80 >> (x & 7))))
				x++;
			Rect run;
			SetRect(&run, left + start, top + row, left + x, top + row + 1);
			PutRect(&run, points, &offset, &limit);
		}
	}
	long count = offset / 4;
	SortPoints((Point*) *points, count);
	CullPoints((Point*) *points, &count);
	PackRgn(points, count, rgn);
	DisposHandle(points);
	return rgn;
}


// ROM 0x0032f2bc DoTextOnce__FPvlPP11StyleRecordPs6FPointP11TextOptionsP14TextBoundsInfoUc
// The text as one text object: drawn when asked, its bounds calculated
// when wanted.  Each run (runLengths, or the whole text for one style)
// is drawn with its style's font from the pen position: every glyph at
// its bearing from the baseline, advancing by its width; the pen's mode
// and the style's pattern (else the port's); bold smeared a pixel to the
// right, an underline the font's offset below the baseline for the run's
// width.  ==> the length.
long
DoTextOnce(const void* text, long length, StyleRecord** styles, const short* runLengths, FPoint where, TextOptions* /*options*/, TextBoundsInfo* bounds, Boolean draw)
{
	GrafPort* port = GetCurrentPort();
	const UniChar* chars = (const UniChar*) text;
	Fixed x = where.x;
	Fixed y = where.y;
	long baseline = (short) ((y + 0x8000) >> 16);
	long maxAscent = 0;
	long maxDescent = 0;
	long done = 0;
	long run = 0;
	while (done < length)
	{
		long count = (runLengths != nil) ? runLengths[run] : length;
		if (count > length - done)
			count = length - done;
		StyleRecord* style = styles[run];
		FontEngineInfo info;
		if (OpenFont(&port->portBits, style, 0x10000, 0x10000, &info) == 3)
		{
			done += count;
			run++;
			continue;
		}
		if (info.fAscent > maxAscent)
			maxAscent = info.fAscent;
		if (info.fDescent > maxDescent)
			maxDescent = info.fDescent;
		long mode = (style->fTransferMode != 0) ? style->fTransferMode : port->pnMode;
		PatternHandle pattern = (style->fPattern != nil) ? style->fPattern : port->fgPat;
		Fixed runStart = x;
		for (long i = 0; i < count; i++)
		{
			info.fGetGlyph(chars[done + i], 0, &info);
			if (draw && info.fGlyphBits != nil && port->pnVis >= 0)
			{
				long left = (short) ((x + 0x8000) >> 16) + info.fGlyphBearingX;
				long top = baseline - info.fGlyphBearingY;
				RgnHandle glyph = GlyphRgn(&info, left, top);
				if (glyph != nil)
				{
					DrawRgn(glyph, mode, pattern);
					if (info.fStyleAdjust[0] != 0)
					{
						OffsetRgn(glyph, info.fStyleAdjust[0], 0);
						DrawRgn(glyph, mode, pattern);
					}
					DisposeRgn(glyph);
				}
			}
			x += info.fGlyphAdvance;
		}
		if (draw && info.fStyleAdjust[3] != 0 && port->pnVis >= 0)
		{
			Rect underline;
			SetRect(&underline, (short) ((runStart + 0x8000) >> 16), baseline + info.fStyleAdjust[2], (short) ((x + 0x8000) >> 16), baseline + info.fStyleAdjust[2] + info.fStyleAdjust[3]);
			DrawRect(&underline, mode, pattern);
		}
		CloseFont(&info);
		done += count;
		run++;
	}
	if (bounds != nil)
	{
		bounds->fLeft = where.x;
		bounds->fRight = x;
		bounds->fTop = y - (Fixed) (maxAscent << 16);
		bounds->fBottom = y + (Fixed) (maxDescent << 16);
		bounds->fBaseline = y;
		bounds->fWidth = x - where.x;
		bounds->fHeight = (Fixed) ((maxAscent + maxDescent) << 16);
	}
	return length;
}


// ROM 0x0032eec8 DrawTextOnce__FPvlPP11StyleRecordPs6FPointP11TextOptionsP14TextBoundsInfo
void
DrawTextOnce(const void* text, long length, StyleRecord** styles, const short* runLengths, FPoint where, TextOptions* options, TextBoundsInfo* bounds)
{
	DoTextOnce(text, length, styles, runLengths, where, options, bounds, true);
}


// ROM 0x0032ef18 MeasureTextOnce__FPvlPP11StyleRecordPs6FPointP11TextOptionsP14TextBoundsInfo
void
MeasureTextOnce(const void* text, long length, StyleRecord** styles, const short* runLengths, FPoint where, TextOptions* options, TextBoundsInfo* bounds)
{
	DoTextOnce(text, length, styles, runLengths, where, options, bounds, false);
}


// ROM 0x0025fd08 MeasureOnce__FPUslP11StyleRecord
// A string's width in the style, in pixels.
long
MeasureOnce(const UniChar* text, long length, StyleRecord* style)
{
	TextBoundsInfo bounds;
	FPoint origin = { 0, 0 };
	StyleRecord* styles[1] = { style };
	MeasureTextOnce(text, length, styles, nil, origin, nil, &bounds);
	return (short) ((bounds.fWidth + 0x8000) >> 16);
}


// ROM 0x0025fd78 MeasureOnceFont__FPUslRC6RefVar
long
MeasureOnceFont(const UniChar* text, long length, RefArg fontSpec)
{
	StyleRecord style;
	CreateTextStyleRecord(fontSpec, &style);
	long width = MeasureOnce(text, length, &style);
	DisposeStyleRecord(&style);
	return width;
}


/*------------------------------------------------------------------------------
	N e w t o n S c r i p t
------------------------------------------------------------------------------*/

// the style's FontInfo for a font spec
static void
FontSpecInfo(RefArg fontSpec, FontInfo* fontInfo)
{
	StyleRecord style;
	CreateTextStyleRecord(fontSpec, &style);
	GetStyleFontInfo(&style, fontInfo);
	DisposeStyleRecord(&style);
}


// ROM 0x001efeb4 FFontAscent__FRC6RefVarT1
static Ref
FFontAscent(RefArg /*rcvr*/, RefArg fontSpec)
{
	FontInfo fontInfo;
	FontSpecInfo(fontSpec, &fontInfo);
	return MAKEINT(fontInfo.ascent);
}


// ROM 0x001eff24 FFontDescent__FRC6RefVarT1
static Ref
FFontDescent(RefArg /*rcvr*/, RefArg fontSpec)
{
	FontInfo fontInfo;
	FontSpecInfo(fontSpec, &fontInfo);
	return MAKEINT(fontInfo.descent);
}


// ROM 0x001eff94 FFontLeading__FRC6RefVarT1
static Ref
FFontLeading(RefArg /*rcvr*/, RefArg fontSpec)
{
	FontInfo fontInfo;
	FontSpecInfo(fontSpec, &fontInfo);
	return MAKEINT(fontInfo.leading);
}


// ROM 0x001f0004 FFontHeight__FRC6RefVarT1
// The ascent, descent and leading together.
static Ref
FFontHeight(RefArg /*rcvr*/, RefArg fontSpec)
{
	FontInfo fontInfo;
	FontSpecInfo(fontSpec, &fontInfo);
	return MAKEINT(fontInfo.ascent + fontInfo.descent + fontInfo.leading);
}


// ROM 0x001f2648 FStrFontWidth__FRC6RefVarN21
// StrFontWidth(string, fontSpec): the string's width in the font, in
// pixels (a rich string's ink NOT YET: its text is measured).
static Ref
FStrFontWidth(RefArg /*rcvr*/, RefArg str, RefArg fontSpec)
{
	TRichString rich(str);
	long length = rich.Length();
	if (length == 0)
		return MAKEINT(0);
	StyleRecord style;
	CreateTextStyleRecord(fontSpec, &style);
	UniChar* text = rich.GrabPtr();
	long width = MeasureOnce(text, length, &style);
	rich.ReleasePtr();
	DisposeStyleRecord(&style);
	return MAKEINT(width);
}


void
RegisterTextNatives(void)
{
	RegisterNativeFunction("FFontAscent__FRC6RefVarT1", (void*) FFontAscent, 1);
	RegisterNativeFunction("FFontDescent__FRC6RefVarT1", (void*) FFontDescent, 1);
	RegisterNativeFunction("FFontLeading__FRC6RefVarT1", (void*) FFontLeading, 1);
	RegisterNativeFunction("FFontHeight__FRC6RefVarT1", (void*) FFontHeight, 1);
	RegisterNativeFunction("FStrFontWidth__FRC6RefVarN21", (void*) FStrFontWidth, 2);
}
