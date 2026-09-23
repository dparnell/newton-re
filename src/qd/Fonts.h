/*
	File:		qd/Fonts.h

	Contains:	Fonts and text styles.  A font family is a NewtonScript
				frame (the ROM's espy, New York, Geneva and Handwriting in
				Rromfontlist, reached by symbol through vars.fonts: name,
				macFontID, plainData/boldData/italicData/boldItalicData -
				each an 'sfnt' binary, a TrueType container with bitmap
				strikes ('bloc'/'bdat') or, for a printer face such as
				Times, only widths ('hmtx') - and the strikes' sizes).  A
				StyleRecord names a family, a size (16.16) and a face (bits:
				1 bold, 2 italic, 4 underline, 8 outline, 0x10 shadow, 0x20
				condensed, 0x40 extended, 0x80 superscript, 0x100
				subscript), with a pattern to draw in.  OpenFont fills a
				FontEngineInfo for a style: the strike nearest the size (or
				the widths), its metrics, the glyph functions (character to
				glyph through the 'cmap' subtable, glyph metrics and bitmap
				through the strike's index subtables into 'bdat'), and the
				adjustments the faces the font does not have are synthesised
				with (the style table at 0x00380f78: bold smears a pixel and
				widens by one, italic shears, underline takes a position and
				thickness, outline and shadow widen).

	The ROM's layouts: StyleRecord 0x20 bytes, FontEngineInfo 0xc4 bytes
	(the offsets below are the ROM's; the host's pointers are wider).

	Reconstructed from the MP2x00 US ROM (0x000adf28-0x000aef10,
	0x002618b8-0x00261d2c, 0x002e1e60-0x002e2d10, 0x00359be8-0x00359d40,
	0x0035a54c); each function cites its origin.  NOT YET RECONSTRUCTED:
	the four-entry font cache (OpenFont opens afresh), scaled bitmaps
	(a strike is drawn at its own size), the
	PostScript printer's font substitution, the 'font' part handler.
*/

#ifndef __FONTS_H
#define __FONTS_H

#ifndef __PORTS_H
#include "Ports.h"
#endif
#ifndef __OBJECTS_H
#include "objects.h"
#endif

// face bits
const long kBoldFace = 1;
const long kItalicFace = 2;
const long kUnderlineFace = 4;
const long kOutlineFace = 8;
const long kShadowFace = 0x10;
const long kCondensedFace = 0x20;
const long kExtendedFace = 0x40;
const long kSuperscriptFace = 0x80;
const long kSubscriptFace = 0x100;

// a font spec packed into an integer: the ROM font list index in bits
// 0-9, the size in 10-19, the face in 20-29
inline long	PackedFontFamily(long font)		{ return font & 0x3ff; }
inline long	PackedFontSize(long font)		{ return (font & 0xfffff) >> 10; }
inline long	PackedFontFace(long font)		{ return (font & 0x3ff00000) >> 20; }
inline long	PackFont(long family, long size, long face)	{ return (family & 0x3ff) | ((size & 0x3ff) << 10) | ((face & 0x3ff) << 20); }

struct StyleRecord
{
	// The scalars start clear.  The ROM's callers allocate their style
	// records in cleared memory and rely on it: CreateParagraphStyleRecord
	// writes nothing at all into a record whose font spec comes to nil,
	// and DisposeStyleRecord then reads fPattern.  (Host: the ROM's C++
	// leaves an ordinary `new StyleRecord` uninitialised, so a garbage
	// fPattern is disposed as a pattern handle.)
					StyleRecord()
						: fFontSize(0), fFontFace(0), fFontPattern(NILREF),
						  fTransferMode(0), fReserved14(0), fReserved18(0),
						  fPattern(nil)	{ }

	RefStruct		fFontFamily;	// +0x00  the font family frame
	Fixed			fFontSize;		// +0x04  16.16
	long			fFontFace;		// +0x08
	Ref				fFontPattern;	// +0x0c  the pattern as a Ref (AddressToRef), nil for the port's
	long			fTransferMode;	// +0x10  (0: the port's pen mode)
	long			fReserved14;	// +0x14
	long			fReserved18;	// +0x18
	PatternHandle	fPattern;		// +0x1c  the pattern handle CreateTextStyleRecord made (its caller disposes it)
};

struct FontEngineInfo;
typedef long	(*FontMapProc)(long ch, const void* cmap);
typedef void	(*FontGlyphProc)(long ch, long glyph, FontEngineInfo* info);		// ch 0: the glyph as given
typedef long	(*FontReopenProc)(FontEngineInfo* info);
typedef void	(*FontCloseProc)(FontEngineInfo* info);

struct FontEngineInfo
{
	long			fAscent;			// +0x00  the strike's line metrics (or the widths font's, scaled)
	long			fDescent;			// +0x04  below the baseline, positive
	long			fLeading;			// +0x08
	long			fWidMax;			// +0x0c
	long			fReserved10;		// +0x10
	long			fReserved14;		// +0x14
	long			fBaselineShift;		// +0x18  superscript raises, subscript lowers (and shrinks the size)
	long			fStyleAdjust[6];	// +0x1c  0 bold smear, 1 italic shear, 2-4 underline offset/thickness/?, 5 outline and shadow
	long			fWidthAdjust;		// +0x34  what the synthesised faces add to every advance
	long			fReserved38;		// +0x38
	Fixed			fScaleX;			// +0x3c  the strike's size to the wanted size (1.0: drawn as is)
	Fixed			fScaleY;			// +0x40
	long			fMinOriginSB;		// +0x44  the strike's horizontal metrics
	long			fMinAdvanceSB;		// +0x48
	long			fMaxBeforeBL;		// +0x4c
	long			fMinAfterBL;		// +0x50
	long			fGlyphHeight;		// +0x54  the current glyph (GetGlyph)
	long			fGlyphWidth;		// +0x58
	long			fGlyphBearingX;		// +0x5c
	long			fGlyphBearingY;		// +0x60  the top row above the baseline
	Fixed			fGlyphAdvance;		// +0x64  16.16, the width adjustment included (GetGlyphInfo)
	const unsigned char* fGlyphBits;	// +0x68  byte-aligned rows
	long			fGlyphRowBytes;		// +0x6c
	FontReopenProc	fReopen;			// +0x70
	FontMapProc		fMap;				// +0x74
	FontGlyphProc	fGetGlyphInfo;		// +0x78
	FontGlyphProc	fGetGlyph;			// +0x7c
	FontCloseProc	fClose;				// +0x80
	long			fScaling;			// +0x84  0 the size as is, 2 scaled, 3 no font
	void*			fInkGlyphBits;		// +0x88  an ink font's rendered glyph (the ROM leaves these two spare for the engine in use)
	long			fInkGlyphSize;		// +0x8c  how many bytes of it
	FontEngineInfo*	fCached;			// +0x90  the font cache's copy (NOT YET: none)
	const char*		fSfnt;				// +0x94  the 'sfnt' data
	const char*		fCmap;				// +0x98  its cmap subtable
	const char*		fStrike;			// +0x9c  the bitmapSizeTable chosen
	const char*		fIndexSubTables;	// +0xa0  the strike's index subtable array
	const char*		fBdat;				// +0xa4
	const char*		fIndexSubTable;		// +0xa8  the current glyph's index subtable
	const char*		fGlyphData;			// +0xac  the current glyph's data
	long			fNumHMetrics;		// +0xb0  a widths font: 'hhea', 'hmtx', the faces' extra width, the scale
	const char*		fHmtx;				// +0xb4
	long			fWidthsAdjust;		// +0xb8
	Fixed			fWidthsScale;		// +0xbc
	RefStruct*		fFontData;			// +0xc0  the 'sfnt' object, locked while open
};

// ROM 0x00377324: the style table the synthesised faces are made with,
// three bytes per face bit from bit 0 - the fStyleAdjust index, what to
// add there, what to add to the width - and at 0x17-0x19 the
// underline's offset, thickness and extra.
extern const unsigned char kStyleTable[0x1c];

// An ink word standing in for a font: the style's family is the ink
// itself (or an integer that is the address of one), and the font has
// the one glyph.
//
// DEVIATION: the ROM's OpenFont calls InkOpenFont straight out.  The
// ink area sits above QuickDraw here - it reaches the strokes through
// the recogniser, which draws through the views - so the opener is
// registered instead (ink/InkFont.h's InitializeInkFont).
typedef long	(*FontInkOpenProc)(PixelMap* pm, StyleRecord* style, RefArg ink,
								   Fixed xScale, Fixed yScale, FontEngineInfo* info);
extern FontInkOpenProc	gInkOpenFont;

// ... and the size and face an ink word is laid out with, which come
// from a glyph made for it.  ==> whether the spec was one.
typedef Boolean	(*FontInkParmsProc)(RefArg fontSpec, long* outSize, long* outFace);
extern FontInkParmsProc	gInkFontParms;

// the font engine
long		FindSFNT(Fixed size, RefArg fontFamily, FontEngineInfo* info, long* face);	// ==> the strike's size, 16.16 (0: no font); face left with what must be synthesised
Ref			ChooseStrike(long face, RefArg fontFamily, long* faceUsed);				// the 'sfnt' for the face
const char*	FindFontTable(const char* sfnt, ULong tag);
long		SFNTOpenFont(PixelMap* pm, StyleRecord* style, RefArg fontFamily, Fixed xScale, Fixed yScale, FontEngineInfo* info);	// ==> 0 opened, 2 scaled, 3 none
long		OpenFont(PixelMap* pm, StyleRecord* style, Fixed xScale, Fixed yScale, FontEngineInfo* info);
void		CloseFont(FontEngineInfo* info);		// host: what info->fClose does
void		InitFonts(void);
Ref			SearchFont(long macFontID, const UniChar* name);		// a font family by Mac id or name; the system font
void		GetStyleFontInfo(StyleRecord* style, FontInfo* fontInfo);
long		FontRefToCharSize(RefArg font);

// styles
void		MakeSimpleStyle(StyleRecord* style, RefArg fontFamily, long size, long face);
void		CreateTextStyleRecord(RefArg fontSpec, StyleRecord* style);		// a packed integer, a font frame or an ink word
void		CopyStyle(StyleRecord* style);							// (ROM: the style's pattern re-made for the cache)
Boolean		EqualStyle(const StyleRecord* a, const StyleRecord* b);
void		DisposeStyleRecord(StyleRecord* style);				// host: the pattern CreateTextStyleRecord made

// a font spec's parts: a packed integer's fields, a font frame's slots
long		GetFontSize(RefArg fontSpec);						// ROM 0x0017aca8 GetFontSize__FRC6RefVar
long		GetFontFace(RefArg fontSpec);						// ROM 0x0017bbc4 GetFontFace__FRC6RefVar
Ref			GetFontFamilySym(RefArg fontSpec);					// nil when there is none
Ref			FamilyNumToSym(long family);						// 'espy, 'newYork, 'geneva, 'handwriting; nil beyond

// the ROM's font list and the system font
Ref			GetROMFontList(void);
Ref			GetFontFamily(RefArg familySymbol);					// through vars.fonts

#endif	/* __FONTS_H */
