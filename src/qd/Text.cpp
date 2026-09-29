/*
	File:		qd/Text.cpp

	Contains:	Drawing and measuring text, and laying it out: the options
				(a width to fit, alignment, justification) and paragraphs
				wrapped into a rectangle.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	The ROM composes a chunk's glyphs into a one-bit slab (DrTextChunk
	0x0035c788) and blits it; the host draws each glyph as a region
	through DrawRgn - the same pixels for an unscaled strike (DEVIATION:
	the code).  The ROM's text object (NewText 0x0035bfc4: the text, its
	length, styles, runs, location, options, and the measured widths) is
	the layout's working state here, on the stack (DEVIATION: no object).
*/

#include "Text.h"
#include "TextObject.h"
#include "TextLayout.h"
#include "Draw.h"
#include "FixedMath.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "NativeFunctions.h"
#include "Interpreter.h"
#include "RichString.h"
#include "Unicode.h"
#include "Locale.h"
#include "OSErrors.h"
#include "NewtonExceptions.h"
#include <string.h>

// the ROM's characters
const UniChar kCarriageReturn = 0x0d;
const UniChar kSpace = 0x20;
const UniChar kEllipsis = 0x2026;


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


// ROM 0x0035baa4 MeasureGlyphWidths__Fl
// Every character's advance in its run's font; with a width to fit, the
// text is cut before the first character that would cross it, the width
// so far kept in the options.  ==> the count that fits.
long
MeasureGlyphWidths(const UniChar* chars, long length, StyleRecord** styles, const short* runLengths, TextOptions* options, TextLayout* layout, GrafPort* port)
{
	long fitted = length;
	Fixed limit = (options != nil) ? options->fWidth : 0;
	if (options != nil)
		options->fFittedWidth = 0;
	Fixed width = 0;
	long done = 0;
	long run = 0;
	layout->fSpaces = 0;
	while (done < length)
	{
		long count = (runLengths != nil) ? runLengths[run] : length;
		if (count > length - done)
			count = length - done;
		FontEngineInfo info;
		Boolean opened = OpenFont(&port->portBits, styles[run], 0x10000, 0x10000, &info) != 3;
		for (long i = 0; i < count; i++)
		{
			long index = done + i;
			layout->fRuns[index] = run;
			Fixed advance = 0;
			if (opened)
			{
				info.fGetGlyphInfo(chars[index], 0, &info);
				advance = info.fGlyphAdvance;
			}
			layout->fAdvances[index] = advance;
			if (limit != 0 && index < fitted && width + advance > limit)
				fitted = index;
			width += advance;
			if (chars[index] == kSpace)
				layout->fSpaces++;
		}
		if (opened)
			CloseFont(&info);
		done += count;
		run++;
	}
	if (options != nil)
		options->fFittedWidth = width;
	layout->fCount = length;
	layout->fWidth = width;
	return fitted;
}


// ROM 0x0035b6d8 JustifyText__Fl
// The slack (the width to fit less the text's width) times the options'
// justification spread over the characters but the last: a space gets
// nine shares, another character one - or, for a text wider than the
// width, every character the same.  ==> the offset of the text's start:
// the slack times the alignment.
Fixed
JustifyText(const UniChar* chars, long length, TextOptions* options, TextLayout* layout)
{
	if (options == nil || options->fWidth == 0)
		return 0;
	Fixed slack = options->fWidth - layout->fWidth;
	if (options->fJustification != 0 && length > 1)
	{
		Fixed extra = (options->fJustification == 0x10000) ? slack : FixedMultiply(slack, options->fJustification);
		long spaces = 0;
		for (long i = 0; i < length; i++)
			if (chars[i] == kSpace)
				spaces++;
		Fixed perChar;
		Fixed perSpace;
		if (extra < 0)
		{
			perChar = FixedDivide(extra, (Fixed) ((length - 1) << 16));
			perSpace = perChar;
		}
		else
		{
			perChar = FixedDivide(extra, (Fixed) ((length + spaces * 8 - 1) << 16));
			perSpace = FixedMultiply(perChar, 0x90000);
		}
		for (long i = 0; i < length - 1; i++)
			layout->fAdvances[i] += (chars[i] == kSpace) ? perSpace : perChar;
		layout->fWidth += extra;
		slack -= extra;
	}
	if (options->fAlignment == 0)
		return 0;
	return (options->fAlignment == 0x10000) ? slack : FixedMultiply(slack, options->fAlignment);
}


// (host) A text object measured (MeasureGlyphWidths: the characters that
// fit the options' width - the object's length is cut to them, as the
// ROM's is), laid out (JustifyText) and drawn when asked, its bounds
// calculated when wanted: the work the ROM's DrText and CalcTextBounds
// share.  Each run (the run lengths, or the whole text for one style) is
// drawn with its style's font from the pen position: every glyph at its
// bearing from the baseline, advancing by its width; the options' or the
// pen's mode and the style's pattern (else the port's); bold smeared a
// pixel to the right, an underline the font's offset below the baseline
// for the run's width.  ==> the length drawn.
static long
LayOutText(TextObject* obj, TextBoundsInfo* bounds, Boolean draw)
{
	GrafPort* port = GetCurrentPort();
	const UniChar* chars = (const UniChar*) obj->fText;
	long length = obj->fLength;
	StyleRecord** styles = obj->fStyles;
	const short* runLengths = obj->fRunLengths;
	FPoint where = obj->fLocation;
	TextOptions* options = obj->fOptions;
	if (length < 0)
		length = 0;
	Fixed* advances = (Fixed*) QDNewTempPtr((length + 1) * sizeof(Fixed));
	long* runs = (long*) QDNewTempPtr((length + 1) * sizeof(long));
	if (advances == nil || runs == nil)
	{
		QDDisposeTempPtr(advances);
		QDDisposeTempPtr(runs);
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	}
	TextLayout layout;
	layout.fAdvances = advances;
	layout.fRuns = runs;
	long fitted = MeasureGlyphWidths(chars, length, styles, runLengths, options, &layout, port);
	if (fitted < length)
	{
		// the characters beyond the width are dropped from the layout
		layout.fWidth = 0;
		for (long i = 0; i < fitted; i++)
			layout.fWidth += advances[i];
		length = fitted;
		obj->fLength = fitted;
	}
	Fixed x = where.x + JustifyText(chars, length, options, &layout);
	Fixed start = x;
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
			for (long i = 0; i < count; i++)
				x += advances[done + i];
			done += count;
			run++;
			continue;
		}
		if (info.fAscent > maxAscent)
			maxAscent = info.fAscent;
		if (info.fDescent > maxDescent)
			maxDescent = info.fDescent;
		long mode = (options != nil && options->fTransferMode != 0) ? options->fTransferMode
				  : (style->fTransferMode != 0) ? style->fTransferMode : port->pnMode;
		PatternHandle pattern = (style->fPattern != nil) ? style->fPattern : port->fgPat;
		if ((mode & 8) == 0)
		{
			// a source mode (srcOr, ...): the ROM blits the glyphs' slab as the
			// source; the host's regions take the pattern mode with black
			mode |= 8;
			pattern = GetStdPattern(blackPat);
		}
		Fixed runStart = x;
		for (long i = 0; i < count; i++)
		{
			if (draw && port->pnVis >= 0)
			{
				info.fGetGlyph(chars[done + i], 0, &info);
				if (info.fGlyphBits != nil)
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
			}
			x += advances[done + i];
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
		bounds->fLeft = start;
		bounds->fRight = x;
		bounds->fTop = y - (Fixed) (maxAscent << 16);
		bounds->fBottom = y + (Fixed) (maxDescent << 16);
		bounds->fBaseline = y;
		bounds->fWidth = x - start;
		bounds->fHeight = (Fixed) ((maxAscent + maxDescent) << 16);
	}
	QDDisposeTempPtr(advances);
	QDDisposeTempPtr(runs);
	return length;
}

// ROM 0x0035c530 DrText__FlN21
// The text object drawn - StdText's drawing, after the recording.
// DEVIATION: the ROM composes each chunk of glyphs into a one-bit slab
// (DrTextChunk) and blits it; the host draws a glyph at a time.  NOT YET
// RECONSTRUCTED: the scales (the text is drawn at full size).
void
DrText(TextObjectRef text, Fixed /*hScale*/, Fixed /*vScale*/)
{
	LayOutText(TextObj(text), nil, true);
}


// ROM 0x0035b32c DispatchCalcBounds__FlPv
// The text object's bounds.  DEVIATION: the ROM asks the port's text proc
// for them (the operation 0x200, CalcTextBounds); the host measures.
static void
DispatchCalcBounds(TextObjectRef text, TextBoundsInfo* bounds)
{
	LayOutText(TextObj(text), bounds, false);
}


// ROM 0x0035a418 DoTextOnce__FPvlPP11StyleRecordPs6FPointP11TextOptionsP14TextBoundsInfoUc
// The text as a text object for a single use, made on the stack (not
// flagged as allocated, so DisposeText only drops its caches): drawn
// through the port's text proc when asked, which records it into an open
// picture as well, then measured for the bounds when they are wanted.
// ==> its length after the layout (the characters that fit a width).
// NOT YET RECONSTRUCTED: the options' transfer modes 9 and 10 (a flag of
// the layout's, and 10 dropping the options).
long
DoTextOnce(const void* text, long length, StyleRecord** styles, const short* runLengths, FPoint where, TextOptions* options, TextBoundsInfo* bounds, Boolean draw)
{
	TextObject obj;
	memset(&obj, 0, sizeof(obj));
	TextObject* handle = &obj;
	TextObjectRef ref = (TextObjectRef) &handle;
	obj.fText = text;
	obj.fLength = length;
	obj.fStyles = styles;
	obj.fRunLengths = runLengths;
	obj.fLocation = where;
	obj.fOptions = options;
	// (the ROM makes its two caches here, as temporary blocks, for a text
	//  of up to 0x80 characters; the host has none - DEVIATION)
	obj.fHScale = 0x10000;
	obj.fVScale = 0x10000;
	if (draw)
		DrawTextObj(ref);
	if (bounds != nil)
		DispatchCalcBounds(ref, bounds);
	long drawn = obj.fLength;
	DisposeText(ref);
	return drawn;
}


// ROM 0x0035a024 DrawTextOnce__FPvlPP11StyleRecordPs6FPointP11TextOptionsP14TextBoundsInfo
void
DrawTextOnce(const void* text, long length, StyleRecord** styles, const short* runLengths, FPoint where, TextOptions* options, TextBoundsInfo* bounds)
{
	DoTextOnce(text, length, styles, runLengths, where, options, bounds, true);
}


// ROM 0x0035a074 MeasureTextOnce__FPvlPP11StyleRecordPs6FPointP11TextOptionsP14TextBoundsInfo
void
MeasureTextOnce(const void* text, long length, StyleRecord** styles, const short* runLengths, FPoint where, TextOptions* options, TextBoundsInfo* bounds)
{
	DoTextOnce(text, length, styles, runLengths, where, options, bounds, false);
}


// ROM 0x00261c40 MeasureOnce__FPUslP11StyleRecord
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


// ROM 0x00261cb0 MeasureOnceFont__FPUslRC6RefVar
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
	R i c h   s t r i n g s
------------------------------------------------------------------------------*/

// ROM 0x0035a164 DoRichString__FR11TRichStringUllP11StyleRecord6FPointP11TextOptionsP14TextBoundsInfoUc
// The rich string's characters from start, drawn or measured.
//
// A plain string, or one whose ink is all outside the range, is one run
// in the caller's style.  Otherwise the range is cut into runs - each
// ink character is a run of its own and the text between two of them is
// a run - and each run gets a copy of the caller's style with one field
// changed: an ink run's "font family" is the *address* of its blob,
// which qd/Fonts.h's OpenFont recognises as an ink word and opens as a
// font of one glyph.  That is what draws a word of writing in the
// middle of a line of text.
//
// The copies are given no pattern of their own, so the runs draw with
// the port's - and the caller's pattern is disposed here, which is odd
// but is what the ROM does.  A caller that made a pattern for its style
// and then disposes the style itself therefore disposes it twice;
// nothing does yet, because a pattern only comes from a colour in the
// font spec and CreateTextStyleRecord does not act on one.
long
DoRichString(TRichString& rich, ULong start, long length, StyleRecord* style, FPoint where, TextOptions* options, TextBoundsInfo* bounds, Boolean draw)
{
	UniChar* text = rich.GrabPtr();
	if (rich.Format() == kRichStringFormatPlain)
	{
		StyleRecord* plain[1] = { style };
		long drawn = DoTextOnce(text + start, length, plain, nil, where, options, bounds, draw);
		rich.ReleasePtr();
		return drawn;
	}
	StyleRecord** styles = nil;
	StyleRecord* records = nil;
	short* lengths = nil;
	void** data = nil;
	long runs = 0;
	if (rich.NumInkWordsInRange(start, (ULong) length) != 0)
	{
		runs = rich.NumInkAndTextRunsInRange(start, (ULong) length);
		data = (void**) QDNewTempPtr(runs * (long) sizeof(void*));
		if (data == nil)
		{
			rich.ReleasePtr();
			return 0;
		}
		lengths = (short*) QDNewTempPtr(runs * (long) sizeof(short));
		if (lengths == nil)
		{
			QDDisposeTempPtr(data);
			rich.ReleasePtr();
			return 0;
		}
		rich.GetLengthsAndDataInRange(start, (ULong) length, lengths, data);
		records = new StyleRecord[runs];
		styles = new StyleRecord*[runs];
		RefVar font(style->fFontFamily);
		for (long i = 0; i < runs; i++)
		{
			styles[i] = &records[i];
			if (data[i] == nil)
				records[i].fFontFamily = font;
			else
				records[i].fFontFamily = RefVar(AddressToRef(data[i]));
			records[i].fFontSize = style->fFontSize;
			records[i].fFontFace = style->fFontFace;
			records[i].fFontPattern = style->fFontPattern;
			records[i].fTransferMode = style->fTransferMode;
			records[i].fReserved14 = style->fReserved14;
			records[i].fReserved18 = style->fReserved18;
			// (the ROM makes its array with a constructor and never
			//  writes this field: the runs draw with the port's pattern)
			records[i].fPattern = nil;
		}
		if (style->fPattern != nil)
			DisposePattern(style->fPattern);
	}
	else
	{
		styles = &style;
		lengths = nil;
	}
	long drawn = DoTextOnce(text + start, length, styles, lengths, where, options, bounds, draw);
	if (records != nil)
	{
		delete[] records;
		delete[] styles;
		QDDisposeTempPtr(lengths);
		QDDisposeTempPtr(data);
	}
	rich.ReleasePtr();
	return drawn;
}


// ROM 0x0035a0c4 DrawRichString__FR11TRichStringUllP11StyleRecord6FPointP11TextOptionsP14TextBoundsInfo
void
DrawRichString(TRichString& rich, ULong start, long length, StyleRecord* style, FPoint where, TextOptions* options, TextBoundsInfo* bounds)
{
	DoRichString(rich, start, length, style, where, options, bounds, true);
}


// ROM 0x0035a114 MeasureRichString__FR11TRichStringUllP11StyleRecord6FPointP11TextOptionsP14TextBoundsInfo
long
MeasureRichString(TRichString& rich, ULong start, long length, StyleRecord* style, FPoint where, TextOptions* options, TextBoundsInfo* bounds)
{
	return DoRichString(rich, start, length, style, where, options, bounds, false);
}


// ROM 0x001eab4c FStyledStrTruncate__FRC6RefVarN31
// The string cut to the width in the font: when it does not fit, the
// characters that fit beside an ellipsis are kept and the ellipsis put
// after them (in place).  ==> the string.
Ref
StyledStrTruncate(RefArg str, long width, RefArg fontSpec)
{
	TRichString rich(str);
	long length = rich.Length();
	StyleRecord style;
	CreateTextStyleRecord(fontSpec, &style);
	TextOptions options;
	memset(&options, 0, sizeof(options));
	options.fTransferMode = srcOr;
	options.fWidth = (Fixed) width << 16;
	FPoint origin;
	origin.x = 0;
	origin.y = 0;
	TextBoundsInfo bounds;
	long fitted = MeasureRichString(rich, 0, length, &style, origin, &options, &bounds);
	if (fitted < length)
	{
		options.fWidth = 0;
		UniChar ellipsis = kEllipsis;
		StyleRecord* styles[1] = { &style };
		MeasureTextOnce(&ellipsis, 1, styles, nil, origin, &options, &bounds);
		options.fWidth = ((Fixed) width << 16) - bounds.fWidth;
		fitted = MeasureRichString(rich, 0, length, &style, origin, &options, &bounds);
		rich.DeleteRange(fitted + 1, rich.Length() - (fitted + 1));
		rich.SetChar(fitted, kEllipsis);
	}
	DisposeStyleRecord(&style);
	return str;
}


// ROM 0x001ea36c FStrTruncate__FRC6RefVarN21
// StrTruncate(str, width): in the receiver's viewFont.
static Ref
FStrTruncate(RefArg rcvr, RefArg str, RefArg width)
{
	RefVar font(GetVariable(rcvr, RSSYMviewfont, nil, 0));
	return StyledStrTruncate(str, RINT(width), font);
}


static Ref
FStyledStrTruncate(RefArg /*rcvr*/, RefArg str, RefArg width, RefArg fontSpec)
{
	return StyledStrTruncate(str, RINT(width), fontSpec);
}


/*------------------------------------------------------------------------------
	P a r a g r a p h s
------------------------------------------------------------------------------*/

// ROM 0x0017d070 ConvertToQDFlush__FUlPl
// The viewJustify text bits as a QD flush: vjLeftH 0, vjRightH 1.0,
// vjCenterH 0.5; vjFullH is flush left with full justification.
Fixed
ConvertToQDFlush(ULong justify, Fixed* justification)
{
	*justification = 0;
	switch (justify & 3)
	{
	case 1:		return 0x10000;
	case 2:		return 0x8000;
	case 3:		*justification = 0x10000;
				return 0;
	default:	return 0;
	}
}


// ROM 0x000ec09c FindWordBreaks__FPUsUlT2Uc6RefVarPUlT6
// The word around the offset: wordStart and wordEnd (offsets into the
// text).  The ROM classifies the characters through the locale's
// lineBreakTable (a binary: a class per character, a state machine for
// breaking before and after); the host breaks at spaces and carriage
// returns (DEVIATION: the table).  Not forward: the character before the
// offset is the one looked at.
void
FindWordBreaks(const UniChar* text, ULong length, ULong offset, Boolean forward, RefArg /*breakTable*/, ULong* wordStart, ULong* wordEnd)
{
	if (text == nil || length == 0)
	{
		*wordStart = 0;
		*wordEnd = 0;
		return;
	}
	if (!forward && offset > 0)
		offset--;
	if (offset >= length)
	{
		*wordStart = length;
		*wordEnd = length;
		return;
	}
	ULong start = offset;
	while (start > 0 && text[start - 1] != kSpace && text[start - 1] != kCarriageReturn)
		start--;
	ULong end = offset;
	while (end < length && text[end] != kSpace && text[end] != kCarriageReturn)
		end++;
	*wordStart = start;
	*wordEnd = end;
}


// ROM 0x0017cbf4 SkipUpToTwoSpacesAndCR__FPUsT1
// After a line: up to two spaces, then a carriage return, are skipped.
const UniChar*
SkipUpToTwoSpacesAndCR(const UniChar* text, const UniChar* end)
{
	for (long i = 0; i < 2 && text < end && *text == kSpace; i++)
		text++;
	if (text < end && *text == kCarriageReturn)
		text++;
	return text;
}


// ROM 0x0017c0b4 DrawSimpleLine__FR11TRichStringUlP6FPointPP11StyleRecordP11TextOptionsRC6RefVarPlUc
// One line of the paragraph from start: the text up to the carriage
// return (a return alone is an empty line), as many characters as fit the
// options' width, cut back to the start of the word the width falls in
// (FindWordBreaks) unless it falls at a space; drawn when asked, its
// width answered.  ==> where the next line starts: after the spaces and
// return that end this one.
ULong
DrawSimpleLine(TRichString& rich, ULong start, FPoint* where, StyleRecord** style, TextOptions* options, RefArg breakTable, long* lineWidth, Boolean draw)
{
	UniChar* text = rich.GrabPtr();
	ULong total = rich.Length();
	UniChar* line = text + start;
	if (*line == kCarriageReturn)
	{
		*lineWidth = 0;
		rich.ReleasePtr();
		return start + 1;
	}
	long lineLength = 0;
	while (line[lineLength] != 0 && line[lineLength] != kCarriageReturn)
		lineLength++;
	TextBoundsInfo bounds;
	long fitted = DoTextOnce(line, lineLength, style, nil, *where, options, &bounds, false);
	if (fitted < lineLength)
	{
		if (line[fitted - 1] != kSpace && line[fitted] != kSpace)
		{
			ULong wordStart;
			ULong wordEnd;
			FindWordBreaks(line, total - start, fitted, true, breakTable, &wordStart, &wordEnd);
			if (wordStart != 0)
				fitted = wordStart;
		}
		DoTextOnce(line, fitted, style, nil, *where, options, &bounds, false);
	}
	*lineWidth = (short) ((bounds.fWidth + 0x8000) >> 16);
	if (draw)
		DoTextOnce(line, fitted, style, nil, *where, options, nil, true);
	const UniChar* next = line + fitted;
	next = SkipUpToTwoSpacesAndCR(next, text + total);
	rich.ReleasePtr();
	return start + (ULong) (next - line);
}


// ROM 0x0017be44 DrawSimpleParagraph__FR11TRichStringRC6RefVarP5TRectlUcT4
// The rich string wrapped into the box's width (any width for a box 0
// wide) line by line, the lines the font's height (ascent + descent +
// leading) apart from the box's top, as many as fit its height (any
// number for a box 0 high), drawn when asked in the transfer mode; a box
// 0 wide or high gets the text's width or height.
void
DrawSimpleParagraph(TRichString& rich, RefArg fontSpec, Rect* box, long hJustify, Boolean draw, long transferMode)
{
	long width = box->right - box->left;
	long height = box->bottom - box->top;
	long maxHeight = height > 0 ? height : 10000;
	StyleRecord style;
	CreateTextStyleRecord(fontSpec, &style);
	StyleRecord* styles = &style;
	FontInfo fontInfo;
	GetStyleFontInfo(&style, &fontInfo);
	long lineHeight = fontInfo.ascent + fontInfo.descent + fontInfo.leading;
	FPoint where;
	where.x = (Fixed) box->left << 16;
	where.y = (Fixed) (box->top + fontInfo.ascent) << 16;
	TextOptions options;
	memset(&options, 0, sizeof(options));
	options.fAlignment = ConvertToQDFlush(hJustify, &options.fJustification);
	options.fWidth = width > 0 ? (Fixed) width << 16 : 0;
	options.fTransferMode = transferMode;
	RefVar breakTable;
	if (NOTNIL(IntlResources()))				// (the host without a locale: FindWordBreaks needs no table)
		breakTable = GetLocaleSlot(RSSYMlinebreaktable);
	long textHeight = 0;
	long textWidth = 0;
	ULong offset = 0;
	ULong length = rich.Length();
	while (offset < length)
	{
		if (textHeight > maxHeight)
			break;
		textHeight += lineHeight;
		long lineWidth = 0;
		offset = DrawSimpleLine(rich, offset, &where, &styles, &options, breakTable, &lineWidth, draw);
		where.y += (Fixed) lineHeight << 16;
		if (lineWidth > textWidth)
			textWidth = lineWidth;
	}
	if (height <= 0)
		box->bottom = (short) (box->top + textHeight);
	if (width <= 0)
		box->right = (short) (box->left + textWidth);
	DisposeStyleRecord(&style);
}


// ROM 0x0017be14 TextBounds__FR11TRichStringRC6RefVarP5TRectl
void
TextBounds(TRichString& rich, RefArg fontSpec, Rect* box, long hJustify)
{
	DrawSimpleParagraph(rich, fontSpec, box, hJustify, false, 1);
}


// ROM 0x0017bd2c TextBox__FR11TRichStringRC6RefVarRC5TRectlN24
// The rich string drawn in the box: with a vertical justification the
// text is measured first (in a copy of the box with no height) and the
// box moved down by the room left (half of it for vjCenterV, all for
// vjBottomV).
void
TextBox(TRichString& rich, RefArg fontSpec, const Rect& box, long hJustify, long vJustify, long transferMode)
{
	Rect r = box;
	if (vJustify != 0)
	{
		r.bottom = r.top;
		DrawSimpleParagraph(rich, fontSpec, &r, hJustify, false, 1);
		long room = (box.bottom - box.top) - (r.bottom - r.top);
		r = box;
		if (vJustify == 4)
			OffsetRect(&r, 0, room / 2);
		else if (vJustify == 8)
			OffsetRect(&r, 0, room);
	}
	DrawSimpleParagraph(rich, fontSpec, &r, hJustify, true, transferMode);
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


// ROM 0x001eda9c FFontAscent__FRC6RefVarT1
Ref
FFontAscent(RefArg /*rcvr*/, RefArg fontSpec)
{
	FontInfo fontInfo;
	FontSpecInfo(fontSpec, &fontInfo);
	return MAKEINT(fontInfo.ascent);
}


// ROM 0x001edb0c FFontDescent__FRC6RefVarT1
Ref
FFontDescent(RefArg /*rcvr*/, RefArg fontSpec)
{
	FontInfo fontInfo;
	FontSpecInfo(fontSpec, &fontInfo);
	return MAKEINT(fontInfo.descent);
}


// ROM 0x001edb7c FFontLeading__FRC6RefVarT1
static Ref
FFontLeading(RefArg /*rcvr*/, RefArg fontSpec)
{
	FontInfo fontInfo;
	FontSpecInfo(fontSpec, &fontInfo);
	return MAKEINT(fontInfo.leading);
}


// ROM 0x001edbec FFontHeight__FRC6RefVarT1
// The ascent, descent and leading together.
static Ref
FFontHeight(RefArg /*rcvr*/, RefArg fontSpec)
{
	FontInfo fontInfo;
	FontSpecInfo(fontSpec, &fontInfo);
	return MAKEINT(fontInfo.ascent + fontInfo.descent + fontInfo.leading);
}


// ROM 0x001f0230 FStrFontWidth__FRC6RefVarN21
// StrFontWidth(string, fontSpec): the string's width in the font, in
// pixels (a rich string's ink NOT YET: its text is measured).
Ref
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


// ROM 0x000e3550 FTextBox
// view:TextBox(string, {font, justification}, bounds): the string drawn
// in the bounds (relative to the view: its top left added), in the
// style's font, aligned 'left, 'right or 'center.  DEVIATION: the ROM
// takes the receiver's view (FailGetView) for the top left; the host reads
// the receiver's viewBounds frame, and draws at the bounds as they are
// for a receiver without one.
static Ref
FTextBox(RefArg rcvr, RefArg str, RefArg style, RefArg bounds)
{
	if (ISNIL(str) || ISNIL(style))
		return NILREF;
	RefVar font(GetFrameSlotRef(style, RSSYMfont));
	RefVar justification(GetFrameSlotRef(style, RSSYMjustification));
	long hJustify = EQRef(justification, RSSYMleft) ? 0 : EQRef(justification, RSSYMcenter) ? 2 : 1;
	Rect origin;
	SetRect(&origin, 0, 0, 0, 0);
	if (IsFrame(rcvr))
	{
		RefVar viewBounds(GetProtoVariable(rcvr, RSSYMviewbounds, nil));
		if (IsFrame(viewBounds))
			FromObject(viewBounds, origin);
	}
	Rect box;
	if (!IsFrame(bounds) || !FromObject(bounds, box))
		return NILREF;
	OffsetRect(&box, origin.left, origin.top);
	TRichString rich(str);
	TextBox(rich, font, box, hJustify, 0, 1);
	return NILREF;
}


// ROM 0x000e36a4 FTextBounds
// TextBounds(text, fontOrStyle, box): the box's right and bottom moved
// to where the text ends when laid out in it, and the box answered.
// The second argument is a font spec, or a style frame whose `font`
// slot is one (the frame itself when it has no such slot).  Nothing is
// done at all when the text or the font is nil.
static Ref
FTextBounds(RefArg /*rcvr*/, RefArg text, RefArg fontOrStyle, RefArg box)
{
	if (ISNIL(text) || ISNIL(fontOrStyle))
		return box;
	RefVar font;
	if (!ISPTR(fontOrStyle))
		font = fontOrStyle;
	else
	{
		font = GetFrameSlotRef(fontOrStyle, RSSYMfont);
		if (ISNIL(font))
			font = fontOrStyle;
	}
	Rect r;
	FromObject(box, r);
	TRichString rich(text);
	TextBounds(rich, font, &r, 0);
	SetFrameSlot(box, RSSYMright, RefVar(MAKEINT(r.right)));
	SetFrameSlot(box, RSSYMbottom, RefVar(MAKEINT(r.bottom)));
	return box;
}


// ROM 0x001ec198 FGetFontSize
// GetFontSize(spec): the size a font spec asks for.  A frame with no
// `size` anywhere in its protos answers nil rather than the nought
// GetFontSize (Fonts.h) would give it.
static Ref
FGetFontSize(RefArg /*rcvr*/, RefArg spec)
{
	if (IsFrame(spec) && ISNIL(GetProtoVariable(spec, RSSYMsize, nil)))
		return NILREF;
	return MAKEINT(GetFontSize(spec));
}


// ROM 0x001ed1f4 FGetFontFamilySym
// GetFontFamilySym(spec): the family's symbol ('espy, 'newYork, ...).
static Ref
FGetFontFamilySym(RefArg /*rcvr*/, RefArg spec)
{
	return GetFontFamilySym(spec);
}


// ROM 0x001eb20c FStrWidth__FRC6RefVarT1
// view:StrWidth(string) - the string's width in the receiver's own font,
// which is the viewFont variable as the view sees it.
static Ref
FStrWidth(RefArg rcvr, RefArg str)
{
	RefVar font(GetVariable(rcvr, RSSYMviewfont, nil, 0));
	return FStrFontWidth(rcvr, str, font);
}


void
RegisterTextNatives(void)
{
	RegisterNativeFunction("FFontAscent__FRC6RefVarT1", (void*) FFontAscent, 1);
	RegisterNativeFunction("FFontDescent__FRC6RefVarT1", (void*) FFontDescent, 1);
	RegisterNativeFunction("FFontLeading__FRC6RefVarT1", (void*) FFontLeading, 1);
	RegisterNativeFunction("FFontHeight__FRC6RefVarT1", (void*) FFontHeight, 1);
	RegisterNativeFunction("FStrFontWidth__FRC6RefVarN21", (void*) FStrFontWidth, 2);
	RegisterNativeFunction("FTextBox", (void*) FTextBox, 3);
	RegisterNativeFunction("FTextBounds", (void*) FTextBounds, 3);
	RegisterNativeFunction("FStrTruncate__FRC6RefVarN21", (void*) FStrTruncate, 2);
	RegisterNativeFunction("FStyledStrTruncate__FRC6RefVarN31", (void*) FStyledStrTruncate, 3);
	RegisterNativeFunction("FGetFontSize", (void*) FGetFontSize, 1);
	RegisterNativeFunction("FGetFontFamilySym", (void*) FGetFontFamilySym, 1);
	RegisterNativeFunction("FStrWidth__FRC6RefVarT1", (void*) FStrWidth, 1);
}
