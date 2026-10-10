/*
	File:		qd/HostFonts.h

	Contains:	Fonts the host draws - DEVIATION: the ROM has no such
				engine.  A host that can draw its own system's fonts
				registers a THostFontProvider (Windows: GDI,
				host/win32/HostFontsWin32.cpp); the families chosen from it
				are put into vars.fonts as family frames with a `hostFont`
				slot, which is all the ROM's font menus need to offer them
				(GetAllFontFamilies lists vars.fonts).  OpenFont hands a
				style in such a family to HostOpenFont instead of the
				'sfnt' engine, as it hands an ink word to the ink engine:
				the host's face at the size wanted (never a strike
				stretched), its glyphs through the FontEngineInfo's
				procedures, a character the face lacks (Apple's private-use
				characters among them) taken from the system font.  The
				faces the host has (bold, italic) are its own; the rest are
				synthesised on the slab as for any font.

				Host fonts are off until asked for (the Host preferences
				panel, NEWTON_HOST_FONTS), so that nothing drawn differs
				from the ROM's unless they are.  docs/qd/host-fonts.md.

	Glyphs carry their depth: one bit a pixel is what QuickDraw's text
	drawing takes today (DrText.cpp's one-bit slab); eight bits of coverage
	is what anti-aliased text would be made of, and a provider should be
	able to answer either, though the engine asks only for one bit yet.
*/

#ifndef __HOSTFONTS_H
#define __HOSTFONTS_H

#ifndef __FONTS_H
#include "Fonts.h"
#endif

// A glyph as the host draws it.
struct HostGlyph
{
	long			width;			// the image, pixels (0 x 0: nothing to draw, a space)
	long			height;
	long			bearingX;		// from the pen to the image's left column
	long			bearingY;		// from the baseline up to the image's top row
	Fixed			advance;		// how far the pen moves on, 16.16
	long			depth;			// 1: a bit a pixel, the byte's top bit leftmost; 8: a byte of coverage a pixel, 0 to 255
	long			rowBytes;
	const unsigned char* bits;		// the face's, good until its next GetGlyph
};

// A face's measurements at the size it was opened at, in pixels.
struct HostFaceMetrics
{
	long			ascent;			// the line: above the baseline
	long			descent;		// ... below it, positive
	long			leading;		// ... and between lines
	long			widMax;			// the widest advance
	long			top;			// the most any glyph reaches above the baseline
	long			bottom;			// ... and below it (negative below)
	long			left;			// the furthest any glyph reaches left of the pen (negative: left of it)
};

// A family's face at one size.
class THostFace
{
public:
	virtual			~THostFace() {}
	virtual void	GetMetrics(HostFaceMetrics* metrics) = 0;
	// The character's glyph at the depth asked for (1, or 8 when the
	// provider can) ==> false when the face has no glyph for it.
	virtual bool	GetGlyph(UniChar ch, long depth, HostGlyph* glyph) = 0;
};

// The host's fonts.
class THostFontProvider
{
public:
	virtual			~THostFontProvider() {}
	virtual const char*	Name(void) = 0;									// for messages: "GDI"
	virtual long	CountFamilies(void) = 0;
	virtual bool	GetFamilyName(long index, UniChar* name, long size) = 0;	// size: in characters, the nought included
	virtual bool	HasFamily(const UniChar* family) = 0;
	// The faces the family really has: bit (1 << face) for each face it
	// has of plain (0), bold (1), italic (2) and bold italic (3).
	virtual long	Faces(const UniChar* family) = 0;
	// The family's face (kBoldFace and kItalicFace bits, one it has) at
	// a size in pixels per em, horizontally and vertically.  nil: none.
	virtual THostFace*	OpenFace(const UniChar* family, long face, long pixelsX, long pixelsY) = 0;
	// Families to offer when none have been chosen ("Arial", ...): a
	// nil-terminated list, or nil.
	virtual const char* const*	DefaultFamilies(void) { return nil; }
};

// The provider (nil: the host draws no fonts of its own).
void				SetHostFontProvider(THostFontProvider* provider);
THostFontProvider*	HostFontProvider(void);

// Whether a family frame is the host's.
Boolean		IsHostFontFamily(RefArg family);

// OpenFont's engine for such a family.  ==> 0 opened at the size, 2
// opened and to be scaled (unequal scales), 3 no font.
long		HostOpenFont(PixelMap* pm, StyleRecord* style, RefArg family, Fixed xScale, Fixed yScale, FontEngineInfo* info);

// vars.fonts: the host's families added by name (UTF-8, comma-separated;
// "*" every family the host has; "" none; nil the provider's defaults), and
// taken out again.  ==> how many were added.
long		AddHostFontFamilies(const char* names);
void		RemoveHostFontFamilies(void);
Boolean		HostFontFamiliesAdded(void);

// The family frame for a host family, keyed in vars.fonts by its
// symbol ('|Georgia.host|).
Ref			MakeHostFontFamily(const UniChar* name);

// NewtonScript: HostFontFamilies() - the names of the host's families
// a family symbol can name (nil when the host draws none);
// HostFontsAdded() - the symbols of those in vars.fonts.
void		RegisterHostFontNatives(void);

// Every open face given back (host fonts turned off, a test's provider
// changed).
void		FlushHostFontCache(void);

#endif	/* __HOSTFONTS_H */
