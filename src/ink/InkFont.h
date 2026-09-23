/*
	File:		ink/InkFont.h

	Contains:	An ink word standing in for a font, which is how a word
				of writing is laid out and drawn among real characters.

				A paragraph keeps an ink word in its text as the single
				character 0xf701, and the style at that character is the
				'inkWord binary itself rather than a font frame.  When
				the text engine opens the font for that style it finds
				the ink, and `InkOpenFont` gives it a FontEngineInfo
				whose one glyph is the writing: the ascent, descent and
				advance width are the word's own measurements, and
				asking for the glyph draws the ink into a bitmap the
				engine blits like any other.  The space character maps
				to a blank glyph a fifth of the line height wide.

				`TInkWordGlyph` is the glyph itself.  It is made for a
				font size and a face, and works out from them what the
				word measures when drawn that size: at the word's own
				size everything comes out of the packed information it
				carries, and at any other size the measurements are
				scaled and the pen is the standard one for the size -
				unless the top bit of the face is set, which asks for
				the word's own pen scaled instead.

				The ink may also arrive as an integer rather than a
				binary.  That is the address of an ink word kept outside
				the object heap, which the recogniser hands over without
				copying (`GetInkWordAddrData`, `GetInkWordAddrInfo`).

	NOT YET RECONSTRUCTED: `TInkWordGlyph::SetFontParms` (the word
	restyled from a font spec, over SetInkWordFontParms), and the
	printing path - on a printer port the ROM draws the word as real
	outlined paths (`CSMakePathsGroup`, `FramePaths`) rather than as
	QuickDraw lines.

	Reconstructed from the MP2x00 US ROM (0x000ada30-0x000adf20,
	0x000dc2a4-0x000dc568); each function cites its origin.
*/

#ifndef __INKFONT_H
#define __INKFONT_H

#include "Ink.h"
#include "Fonts.h"

// The ink word an integer Ref stands for: its data, and the eight bytes
// of measurements at the end of it.  (The length is the first halfword
// of the block, so the measurements are six bytes back from its end.)
void*	GetInkWordAddrData(RefArg ink);						// ROM 0x000dc2a4 GetInkWordAddrData__FRC6RefVar
void	GetInkWordAddrInfo(RefArg ink, InkWordInfo* info);	// ROM 0x000dc2c4 GetInkWordAddrInfo__FRC6RefVarP11InkWordInfo


// A word of writing as one glyph of a font.  The ROM's object is 100
// bytes and its virtuals are declared in its vtable's order.
class TInkWordGlyph
{
public:
					TInkWordGlyph(RefArg ink, ULong fontSize, ULong face);	// ROM 0x000dc314 __ct__13TInkWordGlyphFRC6RefVarUlT2

	// (the ROM's object has no destructor at all: InkCloseFont gives the
	//  ink handle back and frees the block itself, and every caller that
	//  makes one on the stack disposes the handle by hand.  One
	//  destructor here does for all of them.)
					~TInkWordGlyph()						{ delete fInk; fInk = nil; }

	// What the word measures at the size and face it was made for.
	virtual void	ReadMetrics(void);						// ROM 0x000dc394 ReadMetrics__13TInkWordGlyphFv (vtable +0x00)
	// Drawn into the current port with its baseline's left end at
	// (x, y) - the place a glyph is drawn at.
	virtual void	DrawAt(ULong x, ULong y);				// ROM 0x000dc568 DrawAt__13TInkWordGlyphFUlT1 (vtable +0x04)
	// NOT YET: SetFontParms (vtable +0x08)

	RefStruct*	fInk;			// +0x04  the ink word, or an integer that is its address
	ULong		fFace;			// +0x08  the face it is drawn with (-1: the word's own)
	ULong		fFontSize;		// +0x0c  the size it is drawn at (-1: the word's own)
	long		fWidth;			// +0x10  what it measures drawn that way: the advance,
	long		fAscent;		// +0x14  above the baseline,
	long		fDescent;		// +0x18  and below it
	InkWordInfo	fInfo;			// +0x1c  the word's own measurements
	long		fPen;			// +0x58  the pen it is drawn with
	Fixed		fSlop;			// +0x5c  the space left either side of it
	Fixed		fScale;			// +0x60  what the word's own size is multiplied by
};								// 0x64 bytes


// The five things the text engine asks an open font for.
long	InkOpenFont(PixelMap* pm, StyleRecord* style, RefArg ink,
					Fixed xScale, Fixed yScale, FontEngineInfo* info);	// ROM 0x000ada30 InkOpenFont__FP8PixelMapP11StyleRecordRC6RefVarlT4P14FontEngineInfo
long	InkReopenFont(void* info);							// ROM 0x000adcb4 InkReopenFont__FPv
long	InkCharToGlyph(long ch, const void* info);			// ROM 0x000adcbc InkCharToGlyph__FlPv
void	InkGetGlyphInfo(long ch, long glyph, FontEngineInfo* info);	// ROM 0x000adce8 InkGetGlyphInfo__FlT1Pv
void	InkGetGlyph(long ch, long glyph, FontEngineInfo* info);		// ROM 0x000add48 InkGetGlyph__FlT1Pv
void	InkCloseFont(FontEngineInfo* info);					// ROM 0x000aded8 InkCloseFont__FPv

// (host) The opener registered with the font engine - see Fonts.h.
void	InitializeInkFont(void);

#endif	/* __INKFONT_H */
