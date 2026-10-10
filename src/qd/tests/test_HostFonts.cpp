// Host fonts test (qd/HostFonts.h): a made-up provider - glyphs that are
// solid boxes - registered, its families put into vars.fonts and taken
// out again, a style in one opened through OpenFont and measured and
// drawn, the faces the host has used and the others synthesised, a
// character the host lacks taken from the system font, a glyph too wide
// for the slab cut to it, eight-bit glyphs made one bit, the face cache,
// unequal scales, and the system font when the host cannot draw the
// family.  Runs over a standalone kernel heap with the ROM's objects
// imported (for the system font and vars.fonts), as test_Text does.
#include "HostFonts.h"
#include "Text.h"
#include "Draw.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "ROMImport.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "Compiler.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "Unicode.h"
#include "RichString.h"
#include "Locale.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

const long kWidth = 128;
const long kHeight = 32;
static unsigned char gBits[kWidth * kHeight / 8];
static PixelMap gMap;
static GrafPort gPort;


static Ref
Eval(const char* source)
{
	RefVar fn(ParseString(RefVar(MakeString(source))));
	return InterpretBlock(fn, RefVar(gVarFrame));
}


static bool
Same(const UniChar* a, const char* b)
{
	for (; *a != 0 && *b != 0; a++, b++)
		if (*a != (UniChar) *b)
			return false;
	return *a == 0 && *b == 0;
}


/*------------------------------------------------------------------------------
	The made-up host: "Boxes" (plain and bold), "Plain" (plain only),
	"Gray" (plain, answering eight-bit glyphs).  A glyph is a box half the
	size wide and as tall as the ascent, a column in from the pen; the
	advance is two more than its width, three in bold.  'W' is three
	sizes wide and starts a size left of the pen; characters from 0xF000
	up are not in any family.
------------------------------------------------------------------------------*/

static long	gFacesOpened = 0;

class TBoxFace : public THostFace
{
public:
	TBoxFace(long face, long pixels, bool gray) : fFace(face), fPixels(pixels), fGray(gray), fBits(nil) { }
	~TBoxFace() { free(fBits); }

	void GetMetrics(HostFaceMetrics* m)
	{
		m->ascent = fPixels * 3 / 4;
		m->descent = fPixels / 4;
		m->leading = 1;
		m->widMax = fPixels;
		m->top = m->ascent;
		m->bottom = -m->descent;
		m->left = 0;
	}

	bool GetGlyph(UniChar ch, long depth, HostGlyph* g)
	{
		if (ch >= 0xF000)
			return false;
		long ascent = fPixels * 3 / 4;
		g->width = (ch == 'W') ? fPixels * 3 : fPixels / 2;
		g->height = ascent;
		g->bearingX = (ch == 'W') ? -fPixels : 1;
		g->bearingY = ascent;
		g->advance = ToFixed(fPixels / 2 + ((fFace & kBoldFace) ? 3 : 2));
		g->depth = fGray ? 8 : 1;
		(void) depth;
		g->rowBytes = fGray ? g->width : (g->width + 7) / 8;
		free(fBits);
		fBits = (unsigned char*) malloc((size_t) (g->rowBytes * g->height));
		for (long y = 0; y < g->height; y++)
			for (long x = 0; x < g->rowBytes; x++)
				fBits[y * g->rowBytes + x] = fGray ? ((x & 1) ? 0x40 : 0xc0) : 0xff;
		if (!fGray && (g->width & 7) != 0)
			for (long y = 0; y < g->height; y++)
				fBits[y * g->rowBytes + g->rowBytes - 1] = (unsigned char) (0xff << (8 - (g->width & 7)));
		g->bits = fBits;
		return true;
	}

	long			fFace;
	long			fPixels;
	bool			fGray;
	unsigned char*	fBits;
};

class TBoxProvider : public THostFontProvider
{
public:
	const char* Name(void) { return "boxes"; }
	long CountFamilies(void) { return 3; }
	bool GetFamilyName(long index, UniChar* name, long size)
	{
		static const char* const kNames[3] = { "Boxes", "Plain", "Gray" };
		if (index < 0 || index > 2 || size < 8)
			return false;
		long i = 0;
		for (const char* s = kNames[index]; *s; s++)
			name[i++] = (UniChar) *s;
		name[i] = 0;
		return true;
	}
	bool HasFamily(const UniChar* family) { return Faces(family) != 0; }
	long Faces(const UniChar* family)
	{
		if (Same(family, "Boxes"))
			return (1 << 0) | (1 << 1);
		if (Same(family, "Plain") || Same(family, "Gray"))
			return 1 << 0;
		return 0;
	}
	THostFace* OpenFace(const UniChar* family, long face, long pixelsX, long /*pixelsY*/)
	{
		if (Faces(family) == 0)
			return nil;
		gFacesOpened++;
		return new TBoxFace(face, pixelsX, Same(family, "Gray"));
	}
	const char* const* DefaultFamilies(void)
	{
		static const char* const kDefaults[] = { "Plain", "Nowhere", nil };
		return kDefaults;
	}
};

static TBoxProvider	gProvider;


static void
Style(const char* family, long size, long face, StyleRecord* style)
{
	RefVar spec(AllocateFrame());
	SetFrameSlot(spec, RSSYMfamily, RefVar(Intern((char*) family)));
	SetFrameSlot(spec, RSSYMsize, RefVar(MAKEINT(size)));
	SetFrameSlot(spec, RSSYMface, RefVar(MAKEINT(face)));
	CreateTextStyleRecord(spec, style);
}


static Ref
Fonts()
{
	return GetFrameSlotRef(RefVar(gVarFrame), RSSYMfonts);
}


static void
TestFamilies()
{
	// nothing of the host's until asked for
	EXPECT(ISNIL(GetFrameSlotRef(RefVar(Fonts()), RefVar(Intern((char*) "Boxes.host")))));
	EXPECT(ISNIL(Eval("HostFontFamilies()")));
	SetHostFontProvider(&gProvider);
	EXPECT(HostFontProvider() == &gProvider);
	RefVar names(Eval("HostFontFamilies()"));
	EXPECT(IsArray(names) && Length(names) == 3);
	// by name (one the host lacks passed over), the defaults, every one
	EXPECT(AddHostFontFamilies("Boxes, Nowhere") == 1);
	RefVar boxes(GetFrameSlotRef(RefVar(Fonts()), RefVar(Intern((char*) "Boxes.host"))));
	EXPECT(IsFrame(boxes) && IsHostFontFamily(boxes));
	EXPECT(Same(GetCString(RefVar(GetFrameSlotRef(boxes, RSSYMname))), "Boxes"));
	EXPECT(EQRef(GetFrameSlotRef(boxes, RSSYMscreensym), Intern((char*) "Boxes.host")));
	EXPECT(Length(RefVar(GetFrameSlotRef(boxes, RefVar(Intern((char*) "userSizes"))))) == 6);
	EXPECT(AddHostFontFamilies(nil) == 1);						// Plain
	EXPECT(AddHostFontFamilies("*") == 1);						// Gray; Boxes and Plain are there
	EXPECT(HostFontFamiliesAdded());
	EXPECT(Length(RefVar(Eval("HostFontsAdded()"))) == 3);
	// the ROM's font menu lists them
	RefVar all(Eval("GetAllFontFamilies()"));
	bool listed = false;
	for (ArrayIndex i = 0; IsArray(all) && i < Length(all); i++)
		if (EQRef(GetArraySlotRef(all, i), Intern((char*) "Boxes.host")))
			listed = true;
	EXPECT(listed);
	// a ROM family is not a host family
	EXPECT(!IsHostFontFamily(RefVar(GetFontFamily(RefVar(Rsystemfont)))));
}


static void
TestOpening()
{
	StyleRecord style;
	Style("Boxes.host", 12, 0, &style);
	EXPECT(IsHostFontFamily(style.fFontFamily) && style.fFontSize == ToFixed(12));
	FontEngineInfo info;
	EXPECT(OpenFont(&gMap, &style, ToFixed(1), ToFixed(1), &info) == 0);
	EXPECT(info.fAscent == 9 && info.fDescent == 3 && info.fLeading == 1 && info.fWidMax == 12);
	EXPECT(info.fScaleX == ToFixed(1) && info.fWidthAdjust == 0);
	info.fGetGlyphInfo('A', 0, &info);
	EXPECT(info.fGlyphAdvance == ToFixed(8));
	info.fGetGlyph('A', 0, &info);
	EXPECT(info.fGlyphWidth == 6 && info.fGlyphHeight == 9 && info.fGlyphBearingX == 1 && info.fGlyphBearingY == 9);
	EXPECT(info.fGlyphRowBytes == 1 && info.fGlyphBits != nil && info.fGlyphBits[0] == 0xfc);
	EXPECT(info.fMap('A', nil) == 'A');
	// a glyph wider than the slab allows cut to it
	info.fGetGlyph('W', 0, &info);
	EXPECT(info.fGlyphBearingX >= info.fMinOriginSB);
	EXPECT(info.fGlyphBearingX + info.fGlyphWidth <= (info.fGlyphAdvance >> 16) - info.fMinAdvanceSB);
	EXPECT(info.fGlyphWidth > 0);
	// a character the host lacks: the system font's glyph
	FontEngineInfo espy;
	StyleRecord system;
	MakeSimpleStyle(&system, RefVar(GetFontFamily(RefVar(Rsystemfont))), ToFixed(12), 0);
	EXPECT(OpenFont(&gMap, &system, ToFixed(1), ToFixed(1), &espy) == 0);
	espy.fGetGlyph(0xFC0B, 0, &espy);
	info.fGetGlyph(0xFC0B, 0, &info);
	EXPECT(info.fGlyphWidth == espy.fGlyphWidth && info.fGlyphHeight == espy.fGlyphHeight);
	EXPECT(info.fGlyphBearingY == espy.fGlyphBearingY && info.fGlyphAdvance == espy.fGlyphAdvance);
	EXPECT(espy.fGlyphBits != nil && info.fGlyphBits != nil && memcmp(info.fGlyphBits, espy.fGlyphBits, (size_t) (espy.fGlyphRowBytes * espy.fGlyphHeight)) == 0);
	CloseFont(&espy);
	CloseFont(&info);
	DisposeStyleRecord(&style);

	// the host's own bold; bold synthesised where it has none; italic
	// synthesised on the plain face
	Style("Boxes.host", 12, kBoldFace, &style);
	EXPECT(OpenFont(&gMap, &style, ToFixed(1), ToFixed(1), &info) == 0);
	info.fGetGlyphInfo('A', 0, &info);
	EXPECT(info.fWidthAdjust == 0 && info.fStyleAdjust[0] == 0 && info.fGlyphAdvance == ToFixed(9));
	CloseFont(&info);
	Style("Plain.host", 12, kBoldFace, &style);
	EXPECT(OpenFont(&gMap, &style, ToFixed(1), ToFixed(1), &info) == 0);
	info.fGetGlyphInfo('A', 0, &info);
	EXPECT(info.fWidthAdjust == 1 && info.fStyleAdjust[0] != 0 && info.fGlyphAdvance == ToFixed(9));
	CloseFont(&info);
	Style("Boxes.host", 12, kItalicFace, &style);
	EXPECT(OpenFont(&gMap, &style, ToFixed(1), ToFixed(1), &info) == 0);
	EXPECT(info.fStyleAdjust[1] != 0);
	CloseFont(&info);
	// superscript: four fifths of the size, raised by three eighths of
	// the ascent
	Style("Boxes.host", 12, kSuperscriptFace, &style);
	EXPECT(OpenFont(&gMap, &style, ToFixed(1), ToFixed(1), &info) == 0);
	EXPECT(info.fBaselineShift == ((10 * 3 / 4) * 3 >> 3) && info.fBaselineShift > 0);
	info.fGetGlyph('A', 0, &info);
	EXPECT(info.fGlyphBearingY == 7 + info.fBaselineShift);
	CloseFont(&info);
	// unequal scales: opened at the size and scaled
	Style("Boxes.host", 12, 0, &style);
	EXPECT(OpenFont(&gMap, &style, ToFixed(2), ToFixed(1), &info) == 2);
	EXPECT(info.fScaleX == ToFixed(2) && info.fScaleY == ToFixed(1) && info.fAscent == 9);
	CloseFont(&info);
	// equal scales: opened at the size scaled
	EXPECT(OpenFont(&gMap, &style, ToFixed(2), ToFixed(2), &info) == 0);
	EXPECT(info.fScaleX == ToFixed(1) && info.fAscent == 18);
	CloseFont(&info);
	// eight-bit glyphs made one bit (coverage of half or more)
	Style("Gray.host", 12, 0, &style);
	EXPECT(OpenFont(&gMap, &style, ToFixed(1), ToFixed(1), &info) == 0);
	info.fGetGlyph('A', 0, &info);
	EXPECT(info.fGlyphWidth == 6 && info.fGlyphBits != nil && info.fGlyphBits[0] == 0xa8);
	CloseFont(&info);
	// the line metrics QuickDraw answers
	Style("Boxes.host", 12, 0, &style);
	FontInfo fi;
	GetStyleFontInfo(&style, &fi);
	EXPECT(fi.ascent == 9 && fi.descent == 3 && fi.leading == 1 && fi.widMax == 12);
}


static void
TestDrawing()
{
	StyleRecord style;
	Style("Boxes.host", 12, 0, &style);
	memset(gBits, 0, sizeof(gBits));
	long opened = gFacesOpened;
	UniChar text[3] = { 'A', 'B', 0 };
	StyleRecord* styles[1] = { &style };
	FPoint where = { ToFixed(2), ToFixed(14) };
	TextBoundsInfo bounds;
	DrawTextOnce(text, 2, styles, nil, where, nil, &bounds);
	// two boxes, six wide and nine tall standing on the baseline, a column
	// in from the pen: A at 3-8, B at 11-16 (the advance is eight)
	long set = 0;
	for (long y = 0; y < kHeight; y++)
		for (long x = 0; x < kWidth; x++)
			if (GetPixel(&gMap, x, y))
				set++;
	EXPECT(set == 2 * 6 * 9);
	EXPECT(GetPixel(&gMap, 3, 5) && GetPixel(&gMap, 8, 13) && !GetPixel(&gMap, 9, 10) && !GetPixel(&gMap, 3, 14));
	EXPECT(GetPixel(&gMap, 11, 5) && GetPixel(&gMap, 16, 13) && !GetPixel(&gMap, 17, 10));
	// measured as drawn
	EXPECT(bounds.fWidth == ToFixed(16));
	TextBoundsInfo measured;
	MeasureTextOnce(text, 2, styles, nil, where, nil, &measured);
	EXPECT(measured.fWidth == ToFixed(16));
	// the face was the one open already (the cache)
	EXPECT(gFacesOpened == opened);
	DisposeStyleRecord(&style);
}


static void
TestRemoval()
{
	StyleRecord kept;
	Style("Boxes.host", 12, 0, &kept);
	RemoveHostFontFamilies();
	EXPECT(!HostFontFamiliesAdded());
	EXPECT(ISNIL(GetFrameSlotRef(RefVar(Fonts()), RefVar(Intern((char*) "Boxes.host")))));
	// a spec naming it now has the user's font, here the system font
	StyleRecord style;
	Style("Boxes.host", 12, 0, &style);
	EXPECT(!IsHostFontFamily(style.fFontFamily));
	// a style made before, the host no longer drawing it: the system font
	SetHostFontProvider(nil);
	FontEngineInfo info;
	EXPECT(OpenFont(&gMap, &kept, ToFixed(1), ToFixed(1), &info) == 0);
	EXPECT(info.fAscent == 12 && info.fDescent == 4);			// espy 12
	CloseFont(&info);
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_OBJECTS) != noErr)
	{
		printf("test_HostFonts: cannot import %s\n", NEWTON_OBJECTS);
		return 1;
	}
	gObjectHeapSize = 0x100000;
	InitObjects();
	InitGraf();
	InitFonts();
	RegisterTextNatives();
	InstallHostNatives();
	RegisterHostFontNatives();
	SetFrameSlot(RefVar(gVarFrame), RSSYMvars, RefVar(gVarFrame));
	SetFrameSlot(RefVar(gVarFrame), RSSYMfunctions, RefVar(gFunctionFrame));
	// vars.fonts as the boot makes it (test_Text says how)
	RefVar fonts(AllocateFrame());
	RefVar list(Rromfontlist);
	for (long i = 0; i < Length(list); i++)
		SetFrameSlot(fonts, RefVar(FamilyNumToSym(i)), RefVar(GetArraySlotRef(list, i)));
	SetFrameSlot(RefVar(gVarFrame), RSSYMfonts, fonts);
	SetFrameSlot(RefVar(gVarFrame), RSSYMuserconfiguration, RefVar(AllocateFrame()));
	gMap.baseAddr = (Ptr) gBits;
	gMap.rowBytes = kWidth / 8;
	SetRect(&gMap.bounds, 0, 0, kWidth, kHeight);
	gMap.pixMapFlags = kPixMapPtr | 1;
	gMap.deviceRes.v = kDefaultDPI;
	gMap.deviceRes.h = kDefaultDPI;
	gMap.grayTable = nil;
	OpenPort(&gPort);
	SetPortBits(&gMap);
	gPort.portRect = gMap.bounds;
	RectRgn(gPort.visRgn, &gMap.bounds);
	newton_try
	{
		TestFamilies();
		TestOpening();
		TestDrawing();
		TestRemoval();
	}
	newton_catch_all
	{
		failures++;
		fprintf(stderr, "FAIL: unhandled exception %s\n", _info.exception.name);
	}
	end_try;
	ClosePort(&gPort);
	if (failures == 0)
		printf("test_HostFonts: all passed\n");
	else
		printf("test_HostFonts: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
