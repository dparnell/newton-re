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
				a line below the baseline, italic and outline NOT YET).
				MeasureOnce/MeasureOnceFont answer a string's width, the
				NewtonScript StrFontWidth and Font* functions are here too.

	The ROM lays text out into a text object (0x50 bytes: the text, its
	length, styles and run lengths, the location, options, flags, cached
	glyph widths) and draws it a chunk at a time into a one-bit slab that
	is blitted; TextOptions (justification, a width to fit) and the
	TextBoundsInfo the bounds pass fills are the ROM's.  NOT YET
	RECONSTRUCTED: text objects that persist (NewText/DisposeText for a
	caller), justification and the options, ink words, scaled glyphs,
	recording into a picture (StdText).

	Reconstructed from the MP2100 D ROM (0x0025fd08-0x0025fdf4,
	0x0032eec8-0x0032f3f0, 0x003301d0, 0x001efeb4-0x001f0084,
	0x001f2648); each function cites its origin.
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

struct TextOptions;				// NOT YET RECONSTRUCTED (justification, a width to fit, alignment)

void	DrawTextOnce(const void* text, long length, StyleRecord** styles, const short* runLengths, FPoint where, TextOptions* options, TextBoundsInfo* bounds);
void	MeasureTextOnce(const void* text, long length, StyleRecord** styles, const short* runLengths, FPoint where, TextOptions* options, TextBoundsInfo* bounds);
long	DoTextOnce(const void* text, long length, StyleRecord** styles, const short* runLengths, FPoint where, TextOptions* options, TextBoundsInfo* bounds, Boolean draw);
long	MeasureOnce(const UniChar* text, long length, StyleRecord* style);		// the width in pixels
long	MeasureOnceFont(const UniChar* text, long length, RefArg fontSpec);

void	RegisterTextNatives(void);		// StrFontWidth, FontAscent, FontDescent, FontLeading, FontHeight

#endif	/* __TEXT_H */
