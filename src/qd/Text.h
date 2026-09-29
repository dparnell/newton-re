/*
	File:		qd/Text.h

	Contains:	Drawing and measuring text in a style: DrawTextOnce and
				MeasureTextOnce (the ROM's text objects for a single use:
				NewText, DrawTextObj, the bounds calculation, DisposeText),
				a run of UniChars at a point in the current port under a
				StyleRecord (Fonts.h) - or several runs, each with its style
				- giving back a TextBoundsInfo.  The glyphs come from the
				style's font (its strike's bitmaps, or nothing for a widths
				font), drawn through the pen's mode with the style's or the
				port's pattern; the faces the font lacks are synthesised as
				the font engine says (bold smeared a pixel right, underline
				a line below the baseline, italic sheared a row at a time as the
				ROM shears its slab; outline NOT YET).
				MeasureOnce/MeasureOnceFont answer a string's width, the
				NewtonScript StrFontWidth and Font* functions are here too.
				A TextOptions asks for layout: the characters that fit a
				width (the rest are dropped, as the ROM's MeasureGlyphWidths
				cuts a text object's length), the slack before the text (the
				alignment: a QD flush) and spread between the characters
				(full justification, spaces nine times a character's share:
				JustifyText).  TextBox wraps a rich string into a rectangle
				line by line (DrawSimpleParagraph/DrawSimpleLine: each line
				what fits, cut back to a word boundary - FindWordBreaks, the
				host breaking at spaces where the ROM reads the locale's
				lineBreakTable), aligned by the viewJustify text bits.

	The ROM lays text out into a text object (0x50 bytes: the text, its
	length, styles and run lengths, the location, options, flags, cached
	glyph widths) and draws it a chunk at a time into a one-bit slab that
	is blitted; TextOptions (justification, a width to fit) and the
	TextBoundsInfo the bounds pass fills are the ROM's.  NOT YET
	RECONSTRUCTED: text objects that persist (NewText/DisposeText for a
	caller), justification and the options, ink words, scaled glyphs,
	recording into a picture (StdText).

	Reconstructed from the MP2x00 US ROM (0x00261c40-0x00261d2c,
	0x0035a024-0x0035a54c, 0x0035b32c, 0x0035b6d8, 0x0035baa4,
	0x0017bd2c-0x0017c290, 0x0017d070, 0x000ec09c, 0x000e3550,
	0x001eda9c-0x001edc6c, 0x001f0230); each function cites its origin.
*/

#ifndef __TEXT_H
#define __TEXT_H

#ifndef __FONTS_H
#include "Fonts.h"
#endif

// what DrawTextOnce/MeasureTextOnce answer (the ROM's 0x1c bytes)
struct TextBoundsInfo
{
	Fixed		fLeft;			// +0x00  the text's box in the port
	Fixed		fTop;			// +0x04
	Fixed		fRight;			// +0x08
	Fixed		fBottom;		// +0x0c
	Fixed		fBaseline;		// +0x10
	Fixed		fWidth;			// +0x14  the advance
	Fixed		fHeight;		// +0x18  ascent and descent
};

// the layout options of a text object (the ROM's 0x1c bytes): a width to
// fit, the alignment within it (a QD flush: the fraction of the slack put
// before the text) and the justification (the fraction of the slack spread
// between the characters - spaces nine times as much - for full
// justification)
struct TextOptions
{
	Fixed	fJustification;		// +0x00  0 none, 1.0 full (JustifyText)
	Fixed	fAlignment;			// +0x04  0 left, 0.5 centred, 1.0 right
	Fixed	fWidth;				// +0x08  the width to fit; 0 for none
	long	fReserved;			// +0x0c
	long	fTransferMode;		// +0x10  0 for the port's
	Fixed	fFittedWidth;		// +0x14  ==> the width of the text that fit
	long	fReserved2;			// +0x18
};

void	DrawTextOnce(const void* text, long length, StyleRecord** styles, const short* runLengths, FPoint where, TextOptions* options, TextBoundsInfo* bounds);
void	MeasureTextOnce(const void* text, long length, StyleRecord** styles, const short* runLengths, FPoint where, TextOptions* options, TextBoundsInfo* bounds);
long	DoTextOnce(const void* text, long length, StyleRecord** styles, const short* runLengths, FPoint where, TextOptions* options, TextBoundsInfo* bounds, Boolean draw);	// ==> the characters drawn (those that fit the options' width)
long	MeasureOnce(const UniChar* text, long length, StyleRecord* style);		// the width in pixels
long	MeasureOnceFont(const UniChar* text, long length, RefArg fontSpec);

// rich strings (frames/RichString.h): the text's characters (ink NOT YET)
class TRichString;
void	DrawRichString(TRichString& rich, ULong start, long length, StyleRecord* style, FPoint where, TextOptions* options, TextBoundsInfo* bounds);
long	MeasureRichString(TRichString& rich, ULong start, long length, StyleRecord* style, FPoint where, TextOptions* options, TextBoundsInfo* bounds);	// ==> the characters that fit
Ref		StyledStrTruncate(RefArg str, long width, RefArg fontSpec);		// the string cut to the width with an ellipsis, in place
long	DoRichString(TRichString& rich, ULong start, long length, StyleRecord* style, FPoint where, TextOptions* options, TextBoundsInfo* bounds, Boolean draw);

// paragraphs: a rich string wrapped into lines of a rectangle's width in a
// font spec, aligned by the viewJustify text bits (vjLeftH..vjFullH,
// vjTopV..vjBottomV) and drawn in a transfer mode
Fixed	ConvertToQDFlush(ULong justify, Fixed* justification);				// the alignment for the text bits, and the full justification
void	TextBox(TRichString& rich, RefArg fontSpec, const Rect& box, long hJustify, long vJustify, long transferMode);
void	TextBounds(TRichString& rich, RefArg fontSpec, Rect* box, long hJustify);	// the box's right/bottom (when 0 wide/high) set to the text's
void	DrawSimpleParagraph(TRichString& rich, RefArg fontSpec, Rect* box, long hJustify, Boolean draw, long transferMode);
ULong	DrawSimpleLine(TRichString& rich, ULong start, FPoint* where, StyleRecord** style, TextOptions* options, RefArg breakTable, long* lineWidth, Boolean draw);	// ==> where the next line starts
void	FindWordBreaks(const UniChar* text, ULong length, ULong offset, Boolean forward, RefArg breakTable, ULong* wordStart, ULong* wordEnd);
const UniChar*	SkipUpToTwoSpacesAndCR(const UniChar* text, const UniChar* end);

void	RegisterTextNatives(void);		// StrFontWidth, FontAscent, FontDescent, FontLeading, FontHeight, TextBox, StrTruncate, StyledStrTruncate

// the font natives the picture's shapes measure text with
Ref		FFontAscent(RefArg rcvr, RefArg fontSpec);				// ROM 0x001eda9c FFontAscent__FRC6RefVarT1
Ref		FFontDescent(RefArg rcvr, RefArg fontSpec);				// ROM 0x001edb0c FFontDescent__FRC6RefVarT1
Ref		FStrFontWidth(RefArg rcvr, RefArg str, RefArg fontSpec);	// ROM 0x001f0230 FStrFontWidth__FRC6RefVarN21

#endif	/* __TEXT_H */
