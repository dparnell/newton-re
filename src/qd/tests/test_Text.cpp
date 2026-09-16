// Text test: the ROM's own fonts (espy - the system font - and Geneva, read
// out of the ROM image's font family frames) opened through the 'sfnt'
// engine, their metrics, "Hello Wg!" drawn in espy 12 with every pixel
// pinned, a bold strike and an underline, bold synthesised by smearing
// where the family has no bold data, measuring, and the NewtonScript
// StrFontWidth/FontAscent/FontDescent/FontHeight.  Runs over a standalone
// kernel heap with the ROM's objects imported.
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

const long kEspy = 0;			// the ROM font list
const long kGeneva = 2;


static Ref
Eval(const char* source)
{
	if (getenv("EVAL_TRACE")) fprintf(stderr, "eval: %s\n", source);
	RefVar fn(ParseString(RefVar(MakeString(source))));
	return InterpretBlock(fn, RefVar(gVarFrame));
}


static void
Clear()
{
	memset(gBits, 0, sizeof(gBits));
}


// the map from (left, top) as rows of '#' and '.', against the picture
static Boolean
PictureIs(long left, long top, long width, long height, const char* picture, const char* what)
{
	Boolean same = true;
	for (long y = 0; y < height; y++)
		for (long x = 0; x < width; x++)
			if ((GetPixel(&gMap, left + x, top + y) != 0) != (picture[y * (width + 1) + x] == '#'))
				same = false;
	if (!same)
	{
		fprintf(stderr, "  %s:\n", what);
		for (long y = 0; y < height; y++)
		{
			fprintf(stderr, "    ");
			for (long x = 0; x < width; x++)
				fputc(GetPixel(&gMap, left + x, top + y) ? '#' : '.', stderr);
			fprintf(stderr, "   %.*s\n", (int) width, picture + y * (width + 1));
		}
	}
	return same;
}


static void
Draw(const char* what, long family, long size, long face, TextBoundsInfo* bounds)
{
	UniChar text[32];
	ConvertToUnicode(what, text, kMacRomanEncoding, 31);
	StyleRecord style;
	CreateTextStyleRecord(RefVar(MAKEINT(PackFont(family, size, face))), &style);
	StyleRecord* styles[1] = { &style };
	FPoint where = { 2 << 16, 14 << 16 };
	DrawTextOnce(text, Ustrlen(text), styles, nil, where, nil, bounds);
	DisposeStyleRecord(&style);
}


static void
TestFonts()
{
	// the system font: espy 12, its metrics
	StyleRecord style;
	CreateTextStyleRecord(RefVar(MAKEINT(PackFont(kEspy, 12, 0))), &style);
	EXPECT(IsFrame(style.fFontFamily) && style.fFontSize == (12 << 16) && style.fFontFace == 0);
	EXPECT(EQRef(GetFrameSlotRef(style.fFontFamily, Intern((char*) "screenSym")), Intern((char*) "espyFont")));
	FontInfo info;
	GetStyleFontInfo(&style, &info);
	EXPECT(info.ascent == 12 && info.descent == 4 && info.leading == 0 && info.widMax == 15);
	// the font engine: the 12 point strike, 'H' and its metrics
	FontEngineInfo engine;
	EXPECT(OpenFont(&gMap, &style, 0x10000, 0x10000, &engine) == 0);
	EXPECT(engine.fScaleX == 0x10000 && engine.fAscent == 12 && engine.fDescent == 4 && engine.fWidthAdjust == 0);
	engine.fGetGlyph('H', 0, &engine);
	EXPECT(engine.fGlyphWidth == 7 && engine.fGlyphHeight == 9 && engine.fGlyphBearingX == 0 && engine.fGlyphBearingY == 9 && engine.fGlyphAdvance == (8 << 16));
	EXPECT(engine.fGlyphBits != nil && engine.fGlyphBits[0] == 0x82 && engine.fGlyphBits[4] == 0xfe);		// #.....# / #######
	engine.fGetGlyph(' ', 0, &engine);
	EXPECT(engine.fGlyphWidth == 0 && engine.fGlyphAdvance == (3 << 16));
	CloseFont(&engine);
	// a size without a strike takes the nearest and reports the scale
	style.fFontSize = 30 << 16;
	EXPECT(OpenFont(&gMap, &style, 0x10000, 0x10000, &engine) == 2 && engine.fScaleX != 0x10000);
	CloseFont(&engine);
	// a frame spec, and the packed spec's size and face
	EXPECT(PackedFontFamily(PackFont(2, 10, 5)) == 2 && PackedFontSize(PackFont(2, 10, 5)) == 10 && PackedFontFace(PackFont(2, 10, 5)) == 5);
	RefVar spec(AllocateFrame());
	SetFrameSlot(spec, RSSYMfamily, RefVar(Intern((char*) "genevaFont")));
	SetFrameSlot(spec, RSSYMsize, RefVar(MAKEINT(10)));
	SetFrameSlot(spec, RSSYMface, RefVar(MAKEINT(kBoldFace)));
	CreateTextStyleRecord(spec, &style);
	EXPECT(EQRef(GetFrameSlotRef(style.fFontFamily, RSSYMname), GetFrameSlotRef(RefVar(GetArraySlotRef(RefVar(Rromfontlist), kGeneva)), RSSYMname)));
	EXPECT(style.fFontSize == (10 << 16) && style.fFontFace == kBoldFace);
	// the font list and lookups
	EXPECT(Length(RefVar(GetROMFontList())) == 4);
	EXPECT(EQRef(SearchFont(3, nil), GetArraySlotRef(RefVar(Rromfontlist), kGeneva)));		// Geneva's Mac id
	EXPECT(EQRef(SearchFont(999, nil), GetFontFamily(RefVar(Rsystemfont))));
	EXPECT(FontRefToCharSize(RefVar(GetArraySlotRef(RefVar(Rromfontlist), kEspy))) == 2);
}


static void
TestDrawing()
{
	TextBoundsInfo bounds;
	Clear();
	Draw("Hello Wg!", kEspy, 12, 0, &bounds);
	EXPECT(bounds.fWidth == (51 << 16) && bounds.fHeight == (16 << 16) && bounds.fLeft == (2 << 16) && bounds.fRight == (53 << 16));
	EXPECT(bounds.fTop == (2 << 16) && bounds.fBottom == (18 << 16) && bounds.fBaseline == (14 << 16));
	EXPECT(PictureIs(2, 5, 50, 12,
		"#.....#........#.#............#...#...#.........#.\n"
		"#.....#........#.#............#...#...#.........#.\n"
		"#.....#...###..#.#...###......#...#...#...####..#.\n"
		"#.....#..#...#.#.#..#...#......#.#.#.#...#...#..#.\n"
		"#######.#....#.#.#.#.....#.....#.#.#.#..#....#..#.\n"
		"#.....#.######.#.#.#.....#.....#.#.#.#..#....#..#.\n"
		"#.....#.#......#.#.#.....#......#...#...#....#....\n"
		"#.....#..#.....#.#..#...#.......#...#...#...##..#.\n"
		"#.....#...####.#.#...###........#...#....###.#..#.\n"
		".............................................#....\n"
		"............................................#.....\n"
		".........................................###......\n", "Hello Wg! in espy 12"));
	// the bold strike, underlined: the underline a pixel below the baseline
	Clear();
	Draw("Hello", kEspy, 12, kBoldFace | kUnderlineFace, &bounds);
	EXPECT(bounds.fWidth == (37 << 16));
	EXPECT(PictureIs(2, 5, 38, 12,
		"###...###..........###.###............\n"
		"###...###..........###.###............\n"
		"###...###...#####..###.###...#####....\n"
		"###...###..##..###.###.###..###.###...\n"
		"#########.###..###.###.###.###...###..\n"
		"###...###.########.###.###.###...###..\n"
		"###...###.###......###.###.###...###..\n"
		"###...###.####.....###.###..###.###...\n"
		"###...###...######.###.###...#####....\n"
		"......................................\n"
		"#####################################.\n"
		"......................................\n", "Hello in espy 12 bold underlined"));
	// Geneva has no bold data: bold is the plain strike smeared a pixel right
	Clear();
	Draw("Hello", kGeneva, 10, kBoldFace, &bounds);
	EXPECT(PictureIs(2, 6, 32, 8,
		".##..##.......###..###..........\n"
		".##..##........##...##..........\n"
		".##..##..###...##...##...###....\n"
		".######.##.##..##...##..##.##...\n"
		".##..##.#####..##...##..##.##...\n"
		".##..##.##.....##...##..##.##...\n"
		".##..##.##.##..##...##..##.##...\n"
		".##..##..###...##...##...###....\n", "Hello in Geneva 10 bold (smeared)"));
	Clear();
	Draw("Hello", kGeneva, 10, 0, &bounds);
	long plainWidth = bounds.fWidth >> 16;
	Draw("Hello", kGeneva, 10, kBoldFace, &bounds);
	EXPECT((bounds.fWidth >> 16) == plainWidth + 5);			// a pixel wider per glyph
	// the pen's mode and pattern apply; a hidden pen draws nothing
	Clear();
	PaintRect(&gMap.bounds);
	PenMode(patBic);
	Draw("H", kEspy, 12, 0, &bounds);
	PenNormal();
	EXPECT(GetPixel(&gMap, 2, 5) == 0 && GetPixel(&gMap, 3, 5) != 0 && GetPixel(&gMap, 2, 4) != 0);
	Clear();
	HidePen();
	Draw("H", kEspy, 12, 0, &bounds);
	ShowPen();
	EXPECT(GetPixel(&gMap, 2, 5) == 0 && bounds.fWidth == (8 << 16));
	// measuring
	UniChar hello[8];
	ConvertToUnicode("Hello", hello, kMacRomanEncoding, 7);
	StyleRecord style;
	CreateTextStyleRecord(RefVar(MAKEINT(PackFont(kEspy, 12, 0))), &style);
	EXPECT(MeasureOnce(hello, 5, &style) == 27);
	EXPECT(MeasureOnceFont(hello, 5, RefVar(MAKEINT(PackFont(kEspy, 12, 0)))) == 27);
	EXPECT(MeasureOnce(hello, 0, &style) == 0);
	// two runs with two styles: the widths add up
	StyleRecord bold;
	CreateTextStyleRecord(RefVar(MAKEINT(PackFont(kEspy, 12, kBoldFace))), &bold);
	StyleRecord* styles[2] = { &style, &bold };
	short runs[2] = { 2, 3 };
	FPoint origin = { 0, 0 };
	MeasureTextOnce(hello, 5, styles, runs, origin, nil, &bounds);
	EXPECT(bounds.fWidth == (MeasureOnce(hello, 2, &style) + MeasureOnce(hello + 2, 3, &bold)) << 16);
}


static void
TestNatives()
{
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "espy12")), RefVar(MAKEINT(PackFont(kEspy, 12, 0))));
	EXPECT(RINT(Eval("FontAscent(espy12)")) == 12);
	EXPECT(RINT(Eval("FontDescent(espy12)")) == 4);
	EXPECT(RINT(Eval("FontLeading(espy12)")) == 0);
	EXPECT(RINT(Eval("FontHeight(espy12)")) == 16);
	EXPECT(RINT(Eval("StrFontWidth(\"Hello\", espy12)")) == 27);
	EXPECT(RINT(Eval("StrFontWidth(\"\", espy12)")) == 0);
	EXPECT(RINT(Eval("StrFontWidth(\"Hello\", {family: 'genevaFont, size: 10, face: 0})")) == 25);
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_ROM_BIN) != noErr)
	{
		printf("test_Text: cannot import %s\n", NEWTON_ROM_BIN);
		return 1;
	}
	gObjectHeapSize = 0x100000;
	InitObjects();
	InitGraf();
	InitFonts();
	RegisterTextNatives();
	InstallHostNatives();
	SetFrameSlot(RefVar(gVarFrame), RSSYMvars, RefVar(gVarFrame));
	SetFrameSlot(RefVar(gVarFrame), RSSYMfunctions, RefVar(gFunctionFrame));
	// what the boot makes: vars.fonts, the ROM's font families by symbol
	RefVar fonts(AllocateFrame());
	RefVar list(Rromfontlist);
	for (long i = 0; i < Length(list); i++)
	{
		RefVar family(GetArraySlotRef(list, i));
		SetFrameSlot(fonts, RefVar(GetFrameSlotRef(family, Intern((char*) "screenSym"))), family);
	}
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
		TestFonts();
		TestDrawing();
		TestNatives();
	}
	newton_catch_all
	{
		failures++;
		fprintf(stderr, "FAIL: unhandled exception %s (%ld)\n", _info.exception.name, (long) (Long) _info.exception.data);
	}
	end_try;
	ClosePort(&gPort);
	if (failures == 0)
		printf("test_Text: all passed\n");
	else
		printf("test_Text: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
